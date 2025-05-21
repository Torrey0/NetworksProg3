/* Server.c */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>

#include "networks.h"
#include "srej.h"
#include "pollLib.h"

#include "cpe464.h"

typedef enum State STATE;

enum State{
	START, DONE, FILENAME, SEND_DATA, WAIT_ON_ACK, TIMEOUT_ON_ACK,
	WAIT_ON_EOF_ACK, TIMEOUT_ON_EOF_ACK
};

void process_server(int serverSocketNumber);
void process_client(int32_t serverSocketNumber, uint8_t* buf, int32_t recv_len, Connection* client);

STATE filename(Connection* client, uint8_t* buf, int32_t recv_len, int32_t* data_file, int32_t* buf_size);
STATE send_data(Connection* client, uint8_t* packet, int32_t* packet_len, int32_t data_file, int32_t buf_size, uint32_t* seq_num);
STATE timeout_on_ack(Connection* client, uint8_t* packet, int32_t packet_len);
STATE timeout_on_eof_ack(Connection* client, uint8_t* packet, int32_t packet_len);
STATE wait_on_ack(Connection* client);
STATE wait_on_eof_ack(Connection* client);
int processArgs(int argc, char** argv);
void handleZombies(int sig);

int main(int argc, char* argv[]){
	int32_t serverSocketNumber=0;
	int portNumber=processArgs(argc, argv);

	sendtoErr_init(atof(argv[1]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);

	serverSocketNumber = udpServerSetup(portNumber);

	process_server(serverSocketNumber);
	
	return 0;
}

void process_server(int serverSocketNumber){
	pid_t pid=0;
	uint8_t buf[MAX_LEN];
	Connection* client=(Connection*) calloc(1, sizeof(Connection));

	uint8_t flag=0;
	uint32_t seq_num=0;
	int32_t recv_len=0;

	signal(SIGCHLD, handleZombies);

	while(1){
		//block waiting for a new client
		printf("waiting for new client\n");
		recv_len = recv_buf(buf, MAX_LEN, serverSocketNumber, client, &flag, &seq_num);
		//TODO: make recv_buf initialize the window??

		if(recv_len !=CRC_ERROR){
			if((pid=fork())<0){
				perror("fork");
				exit(-1);
			}

			if(pid==0){
				//child process handles the client
				printf("Child fork() - child pid: %d\n", getpid());

				// setupPollSet();	//setup poll set
				printf("clientSock: %d, serverSock: %d\n", client->sk_num, serverSocketNumber);


				//update poll set and sockets
				// addToPollSet(client->sk_num);	//add the client to our poll set
				// removeFromPollSet(serverSocketNumber);	//remove the mane server from the poll set
				// close(serverSocketNumber);

				process_client(serverSocketNumber, buf, recv_len, client);
				exit(0);
			}
		}

	}
}

void process_client(int32_t serverSocketNumber, uint8_t* buf, int32_t recv_len, Connection* client){
	STATE state = START;
	int32_t data_file=0;
	int32_t packet_len=0;
	uint8_t packet[MAX_LEN];
	int32_t buf_size = 0;
	uint32_t seq_num = START_SEQ_NUM;



	while(state != DONE){
		switch(state){
			case START:
				state=FILENAME;
				break;
			case FILENAME:
				state=filename(client, buf, recv_len, &data_file, &buf_size);
				break;
			case SEND_DATA:
				state=send_data(client, packet, &packet_len, data_file, buf_size, &seq_num);
				break;
			case WAIT_ON_ACK:
				state=wait_on_ack(client);
				break;
			case TIMEOUT_ON_ACK:
				state=timeout_on_ack(client, packet, packet_len);
				break;
			case WAIT_ON_EOF_ACK:
				printf("\n\n\nat EOF STate\n\n\n");
				state=wait_on_eof_ack(client);
				break;
			case TIMEOUT_ON_EOF_ACK:
				state=timeout_on_eof_ack(client, packet, packet_len);
				break;
			case DONE:
				break;
			default:
				printf("In default, there is an error somehow!\n");
				state=DONE;
				break;
		}
	}
}

STATE filename(Connection* client, uint8_t* buf, int32_t recv_len, int32_t* data_file, int32_t* buf_size){
	uint8_t response[1];
	char fname[MAX_LEN];
	STATE returnValue=DONE;

	//extract buffer size used for sending data & filename
	memcpy(buf_size, buf, SIZE_OF_BUF_SIZE);
	*buf_size = ntohl(*buf_size);
	memcpy(fname, &buf[sizeof(*buf_size)], recv_len - SIZE_OF_BUF_SIZE);

	//Create client socket for processing this specific client
	client->sk_num = safeGetUdpSocket();
	setupPollSet();
	addToPollSet(client->sk_num);	//add the client to our poll set

	if(((*data_file) = open(fname, O_RDONLY)) < 0){
		send_buf(response, 0, client, FNAME_BAD, 0, buf);
		returnValue=DONE;
	}else{
		send_buf(response, 0, client, FNAME_OK, 0, buf);
		returnValue=SEND_DATA;
	}

	return returnValue;
}

STATE send_data(Connection* client, uint8_t* packet, int32_t* packet_len, int32_t data_file, int32_t buf_size, uint32_t* seq_num){
	uint8_t buf [MAX_LEN];
	int32_t len_read=0;
	STATE returnValue = DONE;


	len_read=read(data_file, buf, buf_size);

	switch(len_read){
		case -1:
			perror("send_data, read error");
			returnValue=DONE;
			break;
		case 0:
			printf("\nI read 0\n");
			(*packet_len) = send_buf(buf, 1, client, END_OF_FILE, *seq_num, packet);
			returnValue=WAIT_ON_EOF_ACK;
			break;
		default:
			(*packet_len) = send_buf(buf, len_read, client, DATA, *seq_num, packet);
			(*seq_num)++;
			returnValue=WAIT_ON_ACK;
			break;
	}

	return returnValue;
}

STATE wait_on_ack(Connection* client){
	STATE returnValue=DONE;
	uint32_t crc_check=0;
	uint8_t buf[MAX_LEN];
	uint32_t len = MAX_LEN;
	uint8_t flag=0;
	uint32_t seq_num=0;
	static int retryCount=0;

	if((returnValue=processSelect(client, &retryCount, TIMEOUT_ON_ACK, SEND_DATA, DONE)) == SEND_DATA){
		crc_check = recv_buf(buf, len, client->sk_num, client, &flag, &seq_num);

		//check for errors
		if(crc_check == CRC_ERROR){
			returnValue = WAIT_ON_ACK;
		} else if(flag!=ACK){
			printf("in wait_on_ack, but didn't recv ACK flag. Should never happen!\n");
			returnValue=DONE;
		}
	}

	return returnValue;
}

STATE wait_on_eof_ack(Connection* client){
	STATE returnValue=DONE;
	uint32_t crc_check=0;
	uint8_t buf[MAX_LEN];
	uint32_t len = MAX_LEN;
	uint8_t flag=0;
	uint32_t seq_num=0;
	static int retryCount=0;

	if((returnValue = processSelect(client, &retryCount, TIMEOUT_ON_EOF_ACK, DONE, DONE)) == DONE){
		crc_check=recv_buf(buf, len, client->sk_num, client, &flag, &seq_num);

		//check for errors
		if(crc_check == CRC_ERROR){
			returnValue=WAIT_ON_EOF_ACK;
		} else if(flag!=EOF_ACK){
			printf("in wait_on_ack, but didn't recv ACK flag. Should never happen!\n");
			returnValue=DONE;
		} else{
			printf("File transfer completed succesfully\n");
			returnValue=DONE;
		}
	}

	return returnValue;
}

STATE timeout_on_ack(Connection* client, uint8_t* packet, int32_t packet_len){
	safeSendto(packet, packet_len, client);

	return WAIT_ON_ACK;
}

STATE timeout_on_eof_ack(Connection* client, uint8_t* packet, int32_t packet_len){
	safeSendto(packet, packet_len, client);

	return WAIT_ON_EOF_ACK;
}

int processArgs(int argc, char** argv){
	int portNumber=0;
	if (argc<2 ||argc>3){
		printf("Usage %s error_rate [port number]\n", argv[0]);
		exit(-1);
	}

	if(argc==3){
		portNumber=atoi(argv[2]);
	}else{
		portNumber=0;
	}
	return portNumber;
}

void handleZombies(int sig){
	int stat=0;
	while(waitpid(-1, &stat, WNOHANG) > 0){}
}
