#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>
#include<stdbool.h>
#include<inttypes.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<pthread.h>
#include<sys/time.h>

#define MAX_SIZE 4098 	// 2 bytes for header and 4096 bytes for data.
#define S_PORT 60000 		// server port

char username[20];
char friend_name[20];
int socket_fd;
int n;
char buffer[MAX_SIZE];
struct sockaddr_in server_addr, client_addr;
int status = 0;

bool running = true;
bool connected = false;
bool is_server = false;

void send_message(char * message_raw, int type, int ending);
int receive_message(bool timeout);

// Header struct that allows bit fields so that message_type is only 3 bits, etc. 
struct Header{
	unsigned int message_type:3;
	unsigned int line_ending_flag:1;
	unsigned int data_length:12;
};

// Decodes the raw header (first 2 bytes) and returns a header struct. 
struct Header decode_header(char * message){
	union {
		uint8_t header_raw[2];
		struct Header header_struct;
	} header;

	header.header_raw[0] = message[0];
	header.header_raw[1] = message[1];


	return header.header_struct;
}

// Takes message_raw and appends a 2 byte header in message. 
void construct_header(char * message, char * message_raw, int type, int ending, int length){
	
	union {
		uint8_t header_raw[2];
		struct Header header_struct;
	} header;

	header.header_struct.message_type = type;
	header.header_struct.line_ending_flag = ending;
	header.header_struct.data_length = length;
	

	message[0] = header.header_raw[0];
	message[1] = header.header_raw[1];
	strcpy((message + 2), message_raw);
	
}

// Decodes the header and handles the message for different message types. 
int handle_message(char * message){
	struct Header header;
	header = decode_header((char *)message);
	switch(header.message_type){
		case 0: // CHECK
			send_message("", 2, 1);
			break;
		case 1: // REQUEST
			status = 0;
			strcpy(friend_name, (message+2));
			send_message((char *)username, 2, 1);
			receive_message(false);
			if(status == 2){
				printf("%s is trying to connect to you. Is that okay?\n1: yes\n2: no\n", friend_name);
				int choice;
				scanf("%d", &choice);
				if(choice == 1){
					printf("-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-\n");
					printf("                 -connected-                   \n");
					connected = true;
					send_message("", 2, 1);
				}else{
					printf("You rejected the request...\n");
					connected = false;
					send_message("", 3, 1);
				}
			}
			break;
		case 2: // ACK
			if(header.data_length > 0){
				strcpy(friend_name, (message+2));
			}
			break;
		case 3: // REJ
			break;
		case 4: // END
			send_message("", 2, 1);
			status = 0;
			for(int i = 0; i < 4; i++){
				receive_message(true);
				if(status == 2){
					if(connected){
						printf("-disconnected-\n");
						printf("press enter to continue...\n");
					}
					connected = false;
					break;
				}else if(i == 3){
					printf("There was an error with the disconnect, but I will disconnect you anyways :D\n");
				}
			}
			break;
		case 5: // CHAT
			//printf("message_type: %d\n", header.message_type); // TODO: remove this
			//printf("data_length: %d\n", header.data_length); // TODO: remove this
			if(connected){
				if(strlen((char*)message)+2 > 0){
					send_message("", 2, 1); // ack
					printf("[%s] %s\n", friend_name, ((char*)message)+2);
				}
			}
			break;
		default:
			printf("That is not a valid message type\n");
			// TODO: figure out default
		
	}
	return header.message_type;
		
}

// Sends message using UDP on socket_fd. 
void send_message(char * message_raw, int type, int ending){
	char message[4098];
	construct_header((char *)message, (char *)message_raw, type, ending, strlen(message_raw));
	if(is_server){
		sendto(socket_fd, (const char *)message, strlen(message_raw)+2, 0, (const struct sockaddr *)&client_addr, sizeof(client_addr));
	}else{
		sendto(socket_fd, (const char *)message, strlen(message_raw)+2, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
	}
}

// Waits for message using UDP and stores in the public variables 'buffer'.
// If timeout == true then stop waiting for message after 500,000 microseconds. 
int receive_message(bool timeout){
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if(timeout){
		tv.tv_usec = 500000;
	}

	if(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
		perror("error with setsockopt()");
	}
	
	if(is_server){
		int addr_len = sizeof(client_addr);
		n = recvfrom(socket_fd, (char *)buffer, MAX_SIZE, MSG_WAITALL, (struct sockaddr * )&client_addr, &(addr_len));
	}else{
		int addr_len = sizeof(server_addr);
		n = recvfrom(socket_fd, (char *)buffer, MAX_SIZE, MSG_WAITALL, (struct sockaddr * )&server_addr, &(addr_len));
	}

	buffer[n] = '\0';

	int val = handle_message((char *)buffer);
	status = val;
	return val;
	
}

bool in_command_mode = false;

// Handles commands when the user adds '/' as the first character. 
void command(char * c){
	in_command_mode = true;
	char quit[5] = "/quit";
	if(!(strcmp(c, quit))){
		for(int i = 0; i < 4; i++){
			send_message("", 4, 1);
			receive_message(true);
			if(status == 2){
				send_message("", 2, 1);
				printf("-disconnected-\n");
				connected = false;
				break;
			}else if(i < 3){
				send_message("", 4, 1);
			}else if(i == 3){
				connected = false;
				// can't quit because there is no connection anyways
			}
		}
		connected = false;
	}
	in_command_mode = true;
}

// Thread so that the client can receive messages in parallel. 
void *receive_thread(){
	while(connected && !in_command_mode){	
		receive_message(false);
	}
}

// Thread so that the server can send messages in parallel. 
void *send_thread(){
	while(connected){
		char message_raw[4096];
		fgets(message_raw, sizeof(message_raw), stdin);
		size_t index = strcspn(message_raw, "\n");
		message_raw[index] = '\0';
		if(message_raw[0] == '/'){
			command(message_raw);
		}else{
			if(strlen(message_raw) > 0){
				printf("[%s] %s\n", username, message_raw);
				send_message((char *)message_raw, 5, 1);
			}
		}
	}	
}

// This function acts as a client. 
void chat_connect(){
	FILE *file;
	char *line = NULL;
	char friends[100][100];
	char ips[100][100];
	size_t length = 0;
	file = fopen("./friends.txt", "r");
	if(file == NULL){
		printf("You don't have any friends added. Try adding a friend before trying to connect.\n");
		exit(EXIT_SUCCESS);
	}
	
	printf("What friend would you like to talk to?\n");
	
	int num_friend = 0;
	int num_ip = 0;
	int num_line = 0;
	while(getline(&line, &length, file) != -1){
		num_line++;
		if(num_line % 2 != 0){
			strcpy(friends[num_friend], line);
			num_friend++;
			printf("%d: %s", num_friend, line);
		}else{
			strcpy(ips[num_ip], line);
			num_ip++;
		}
	}	
	fclose(file);

	int choice = 0;
	scanf("%d", &choice);
	if(choice > num_ip){
		printf("That is not a correct number...");
		exit(EXIT_SUCCESS);
	}


	// create the UDP socket
	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	
	// clear server address
	memset(&server_addr, 0, sizeof(server_addr));
	memset(&client_addr, 0, sizeof(client_addr));

	// server information
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(ips[choice-1]);
	strcpy(friend_name, friends[choice-1]);
	server_addr.sin_port = htons(S_PORT);
	
	// request
	send_message((char *)username, 1, 1);
	for(int i = 0; i < 4; i++){
		receive_message(true);
		if(status == 2){
			// TODO: ack
			status = 0;
			send_message("", 2, 1);
			printf("Waiting for a response...\n");
			receive_message(false);
			if(status == 2){
				printf("-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-\n");
				printf("                 -connected-                   \n");
				connected = true;
				break;
			}else if(status == 3){
				printf("%s rejected your request...\n", friend_name);
				connected = false;
				break;
			}
		}else if(i < 3){
			send_message((char *)username, 1, 1);
		}else if(i == 3){
			printf("Your friend did not respond, sorry...\n");
		}
	}
	
	if(connected){
		pthread_t thread_id;
		pthread_create(&thread_id, NULL, receive_thread, NULL);
		
		// Main loop for sending from the client. 
		while(connected){
			char message_raw[4096];
			fgets(message_raw, sizeof(message_raw), stdin);
			size_t index = strcspn(message_raw, "\n");
			message_raw[index] = '\0';

			if(message_raw[0] == '/'){
				command(message_raw);
			}else if(strlen(message_raw) > 0){
				printf("[%s] %s\n", username, message_raw);
				send_message((char *)message_raw, 5, 1);
			}
		}
		pthread_join(thread_id, NULL);
	}
}


// This function acts as a server. 
void chat_wait(){
	is_server = true;

	// create the UDP socket
	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	
	// clear addresses
	memset(&server_addr, 0, sizeof(server_addr));
	memset(&client_addr, 0, sizeof(client_addr));

	// server information
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(S_PORT);
	
	// bind socket to server address
	bind(socket_fd, (const struct sockaddr *)&server_addr, sizeof(server_addr));


	// request receiving
	printf("\n");
	while(!connected){
		printf("waiting for a connection...\n");
		receive_message(false);
	}

	pthread_t thread_id;
	pthread_create(&thread_id, NULL, send_thread, NULL);
	
	// Main loop for receiving from server after successful connection. 
	while(connected){
		receive_message(false);
	}

	pthread_join(thread_id, NULL);
}

// Send a check to another user's IP address. 
void chat_check(){
	FILE *file;
	char *line = NULL;
	char *friends[100];
	char *ips[100];
	size_t length = 0;
	file = fopen("./friends.txt", "r");
	if(file == NULL){
		printf("You don't have any friends added. Try adding a friend before trying to connect.\n");
		exit(EXIT_SUCCESS);
	}
	
	printf("What friend would you like to check?\n");
	
	int num_friend = 0;
	int num_ip = 0;
	int num_line = 0;
	while(getline(&line, &length, file) != -1){
		num_line++;
		if(num_line % 2 != 0){
			friends[num_friend] = line;
			num_friend++;
			printf("%d: %s", num_friend, line);
		}else{
			ips[num_ip] = line;
			num_ip++;
		}
	}	
	fclose(file);

	int choice = 0;
	scanf("%d", &choice);
	if(choice > num_ip){
		printf("That is not a correct number...");
		exit(EXIT_SUCCESS);
	}
	
	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	
	// clear server address
	memset(&server_addr, 0, sizeof(server_addr));
	memset(&client_addr, 0, sizeof(client_addr));

	// server information
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(ips[choice-1]);
	strcpy(friend_name, friends[choice-1]);
	server_addr.sin_port = htons(S_PORT);

	send_message("", 0, 1);
	for(int i = 0; i < 4; i++){
		if(receive_message(true) == 2){
			printf("Your friend is online!\n");
			break;
		}else if(i < 3){
			send_message("", 0, 1);
		}if(i == 3){
			printf("Your friend is not online!\n");
		}
	}

	
}

// Add a user by knowing their IP address. Info saved in friends.txt
int chat_add(){
	char friend_name[20];
	char friend_ip_raw[20];
	char buff[INET_ADDRSTRLEN];

	in_addr_t friend_addr;

	printf("Input your friend's name: \n");
	scanf("%s", friend_name);
	printf("Input your friend's ip-address: \n");
	scanf("%s", friend_ip_raw);
	friend_addr = inet_addr(friend_ip_raw);

	if(inet_ntop(AF_INET, &friend_addr, buff, sizeof(buff))){
		//printf("inet addr: %s\n", buff);
		// save the information into a file
		FILE *file;
		if(file = fopen("./friends.txt", "a")){
			fprintf(file, "%s\n%s\n", friend_name, buff);
			printf("Your friend, %s, was added successfully!\n", friend_name);
			return 1;
		}else{
			return 0;
		}
	}else{
		return 0;
	}
}

// Initialize the chat application. Runs the main menu. 
void init(){
	printf("What is your username?\n");
	scanf("%s", username);
	while(running){
		int choice;
		printf("Welcome to the chat application! What would you like to do?\n");
		printf("1. Connect to a friend\n");
		printf("2. Wait for a friend to connect\n");
		printf("3. Check if a friend is online\n");
		printf("4. Add friends\n");
		printf("5. Exit\n");
		scanf("%d",&choice);
		switch(choice){
			case 1:
				chat_connect();
				break;
			case 2:
				chat_wait();
				break;
			case 3:
				chat_check();
				break;
			case 4:
				if(!chat_add()){
					printf("There was an error with adding your friend...\n");	
					running = false;
				}
				break;
			case 5:
				exit(EXIT_SUCCESS);
				break;
			default:
				printf("That was not a valid choice\n");
		}
		printf("\n---------------------------------------------------------------------------\n");
	}
}


int main(){	
	init();	
	return 0;
}