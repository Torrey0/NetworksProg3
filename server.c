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
#include "serverSlidingWindow.h"
#include "safeUtil.h"
#include "cpe464.h"

typedef enum State STATE;

enum State{
	START, DONE, FILENAME, SEND_DATA, WAIT_ON_ACK, TIMEOUT_ON_ACK,
	WAIT_ON_EOF_ACK, TIMEOUT_ON_EOF_ACK
};

void process_server(int serverSocketNumber);
void process_client(int32_t serverSocketNumber, uint8_t* buf, int32_t recv_len, Connection* client);

STATE filename(Connection* client, slidingWindow** window, uint8_t* buf, int32_t recv_len, int32_t* data_file, int32_t* buf_size, uint32_t seq_num);
STATE send_data(Connection* client, slidingWindow* window, uint8_t* packet, int32_t* packet_len, int32_t data_file, int32_t buf_size, uint32_t* seq_num, int8_t* fileDone);
STATE timeout_on_ack(Connection* client, slidingWindow* window, uint8_t* packet);
// STATE timeout_on_eof_ack(Connection* client, uint8_t* packet, int32_t packet_len);
STATE wait_on_ack(Connection* client, slidingWindow* window, int8_t fileDone, uint32_t serverSeqNum);
// STATE wait_on_eof_ack(Connection* client);
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
	uint8_t flag=0;
	uint32_t seq_num=0;
	int32_t recv_len=0;
	Connection* client=(Connection*) sCalloc(1, sizeof(Connection));


	signal(SIGCHLD, handleZombies);

	while(1){
		//block waiting for a new client
		recv_len = recv_buf(buf, MAX_LEN, serverSocketNumber, client, &flag, &seq_num);
		//TODO: make recv_buf initialize the window??

		if(recv_len !=CRC_ERROR){
			if((pid=fork())<0){
				perror("fork");
				exit(-1);
			}

			if(pid==0){
				//child process handles the client

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
	uint8_t packet[MAX_LEN + sizeof(Header)];
	slidingWindow* window;
	int32_t buf_size = 0;
	uint32_t seq_num = START_SEQ_NUM;
	int8_t fileDone=0;	//set to indicate when the file is done being read, so we dont return to SEND_DATA


	while(state != DONE){
		switch(state){
			case START:
				state=FILENAME;
				break;
			case FILENAME:
				state=filename(client, &window, buf, recv_len, &data_file, &buf_size, seq_num);
				break;
			case SEND_DATA:
				state=send_data(client, window, packet, &packet_len, data_file, buf_size, &seq_num, &fileDone);
				break;
			case WAIT_ON_ACK:
				state=wait_on_ack(client, window, fileDone, seq_num);
				break;
			case TIMEOUT_ON_ACK:
				state=timeout_on_ack(client, window, packet);
				break;
			case DONE:
				break;
			default:
				state=DONE;
				break;
		}
	}
}

STATE filename(Connection* client, slidingWindow** window, uint8_t* buf, int32_t recv_len, int32_t* data_file, int32_t* buf_size, uint32_t seq_num){
	uint8_t response[1];
	char fname[MAX_LEN];
	STATE returnValue=DONE;
	//extract buffer size used for sending data & filename
	memcpy(buf_size, buf, SIZE_OF_BUF_SIZE);										//extract bufferSize
	*buf_size = ntohl(*buf_size);		
	int32_t windowSize;											
	memcpy(&(windowSize), buf+SIZE_OF_BUF_SIZE, sizeof(windowSize));	//extract windowSize
	windowSize = ntohl(windowSize);
	memcpy(fname, &buf[sizeof(*buf_size)+sizeof(windowSize)], recv_len - SIZE_OF_BUF_SIZE-sizeof(int32_t));	//extract name

	//initialize window:
	*window = serverWindowInit(windowSize, *buf_size, seq_num);

	//Create client socket for processing this specific client
	client->sk_num = safeGetUdpSocket();
	setupPollSet();
	addToPollSet(client->sk_num);	//add the client to our poll set

	if(((*data_file) = open(fname, O_RDONLY)) < 0){
		response[0]= FNAME_BAD;
		send_buf(response, 1, client, FNAME_RESP, 0, buf);
		printf("Error file %s not found\n", fname);
		returnValue=DONE;
	}else{
		response[0]= FNAME_OK;
		send_buf(response, 1, client, FNAME_RESP, 0, buf);
		returnValue=SEND_DATA;
	}

	return returnValue;
}

STATE send_data(Connection* client, slidingWindow* window, uint8_t* packet, int32_t* packet_len, int32_t data_file, int32_t buf_size, uint32_t* seq_num, int8_t* fileDone){
	uint8_t buf [buf_size];
	int32_t len_read=0;
	STATE returnValue = DONE;


	while(windowOpen(window)){
		len_read=read(data_file, buf, buf_size);
		if(len_read==0){	//if we reach EOF
			window_send_data(window, buf, 0, client, END_OF_FILE, seq_num, packet, 1);
			*fileDone=1;	//tell wait_on_ack to never return here, no need.
			returnValue=WAIT_ON_ACK;
			break;
		}else if(len_read==-1){
			perror("send_data, read error");
			returnValue=DONE;
		}else{
			window_send_data(window, buf, len_read, client, DATA, seq_num, packet, 0);
			returnValue=WAIT_ON_ACK;
		}
	}

	return returnValue;
}

STATE wait_on_ack(Connection* client, slidingWindow* window, int8_t fileDone, uint32_t serverSeqNum){
	STATE returnValue=DONE;
	uint32_t crc_check=0;
	uint8_t buf[MAX_LEN];
	uint32_t len = MAX_LEN;
	uint8_t flag=0;
	uint32_t seq_num=0;
	static int retryCount=0;
	while(fileDone || !windowOpen(window)){	//while there isn't room on the window, wait for RR's
		if((returnValue=processSelect(client, &retryCount, TIMEOUT_ON_ACK, SEND_DATA, DONE)) == SEND_DATA){
			//we want to wrap recv_buf, not processSelect
			// if((returnValue=windowRecieve(client, &retryCount, TIMEOUT_ON_ACK, SEND_DATA, DONE)) == SEND_DATA){
			crc_check = windowRecieve(window, buf, len, client->sk_num, client, &flag, &seq_num);

			//check for errors
			if(crc_check == CRC_ERROR){
				returnValue = WAIT_ON_ACK;
				retryCount--;	//dont count CRC Errors as re-attempts
			} else if (crc_check==EOF_ACK){
				return DONE;
			} else if(fileDone && serverSeqNum==seq_num){	//file is Done, and the client has RR'ed all our messages
				returnValue=DONE;	//we are done with the client

			} else if(flag!=ACK && flag!=SREJ){
				returnValue=DONE;
			}
		}else{
			return returnValue;
		}
	}

	return returnValue;
}

//these can both be the same
STATE timeout_on_ack(Connection* client, slidingWindow* window, uint8_t* packet){
	windowSendLowest(window, client, packet);	//Attempt to nudge the client by sending the lowest packet
	return WAIT_ON_ACK;
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
