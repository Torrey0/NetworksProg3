// Client side - UDP Code				    
// By Hugh Smith	4/1/2017		

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

// #include "gethostbyname.h"
#include "pollLib.h"
#include "networks.h"
#include "cpe464.h"
#include "clientSlidingWindow.h"

#include "srej.h"


typedef enum State STATE;
enum State{
	DONE, FILENAME, RECV_DATA, FILE_OK, START_STATE
};


void processFile(char* argv[]);
STATE start_state(char** argv, Connection* server, uint32_t* clientSeqNum, slidingWindow** window);
STATE filename(char* fname, int32_t buf_size, Connection* server);
STATE recv_data(int32_t output_file, Connection* server, uint32_t* clientSeqNum, slidingWindow* window);
STATE file_ok(int* outputFileFd, char* outputFileName);
void check_args(int argc, char** argv);



int main (int argc, char *argv[])
 {
	check_args(argc, argv);

	sendtoErr_init(atof(argv[4]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);

	processFile(argv);
	return 0;
}

void processFile(char* argv[]){
	//argv needed to get file names, server name and server port number
	Connection* server=(Connection*) calloc(1,sizeof(Connection));
	slidingWindow* window=NULL;	//will be set in processFile
	uint32_t clientSeqNum=0;
	int32_t output_file_fd=0;
	STATE state = START_STATE;

	while(state!= DONE){
		switch(state){
			case START_STATE:
				state = start_state(argv, server, &clientSeqNum, &window);
				break;

			case FILENAME:
				state=filename(argv[1], atoi(argv[3]), server);
				break;
			
			case FILE_OK:
				state=file_ok(&output_file_fd, argv[2]);
				break;
			
			case RECV_DATA:
				state=recv_data(output_file_fd, server, &clientSeqNum, window);
				break;
			
			case DONE:
				break;
			
			default:
				printf("ERROR - in default state");
				break;
		}
	}
}

STATE start_state(char** argv, Connection* server, uint32_t* clientSeqNum, slidingWindow** window){
	//Returns FILENAME if no error, otherwise DONE (did 10+ connects & cannot connect to server)
	uint8_t packet[MAX_LEN + sizeof(Header)];
	uint8_t buf[MAX_LEN];
	int fileNameLen=strlen(argv[1]);
	STATE returnValue = FILENAME;
	uint32_t bufferSize=0;
	// printf("in start state\n");
	//if we have previously connected to server (but failed), close it before new reconnect attempts
	if(server->sk_num>0){
		printf("closing old server\n");
		removeFromPollSet(server->sk_num);
		close(server->sk_num);
	}else{
		printf("pollSetInit\n");
		setupPollSet();
	}

	if(udpClientSetup(argv[5], atoi(argv[6]), server) <0){
		//couldn't connect to sevrer
		printf("couldnt connect to server\n");
		returnValue=DONE;
	}else{
		int32_t windowSize=5;	//for now, just use hardocded value on client, can do input later.
		//put in buffer size (for sending data) and filename
		bufferSize=htonl(atoi(argv[3]));
		*window=clientWindowInit(windowSize,atoi(argv[3]), START_SEQ_NUM);

		windowSize = htonl(windowSize);
		memcpy(buf, &bufferSize, SIZE_OF_BUF_SIZE);				//claim bufferSize
		memcpy(buf+SIZE_OF_BUF_SIZE, &windowSize,sizeof(int32_t));
		memcpy(&buf[SIZE_OF_BUF_SIZE + sizeof(int32_t)], argv[1], fileNameLen);	//claim fileName
		printIPv6Info(&server->remote);

		addToPollSet(server->sk_num);	//add the client to our poll set



		send_buf(buf, fileNameLen + SIZE_OF_BUF_SIZE+sizeof(int32_t), server, FNAME, *clientSeqNum, packet);
		(*clientSeqNum)++;

		returnValue=FILENAME;
	}

	return returnValue;
}



STATE filename(char* fname, int32_t buf_size, Connection* server){
	//Send the file name, get response.
	//return START_STATE if no reply from server, DONE if bad filename, FILE_OK if good to go :)
	
	int returnValue=START_STATE;
	uint8_t packet[MAX_LEN + sizeof(Header)];
	uint8_t flag=0;
	uint32_t seq_num=0;
	int32_t recv_check=0;
	static int retryCount=0;

	if((returnValue = processSelect(server, &retryCount, START_STATE, FILE_OK, DONE)) == FILE_OK){
		recv_check = recv_buf(packet, MAX_LEN, server->sk_num, server, &flag, &seq_num);
		
		//check for errors
		if(recv_check == CRC_ERROR){
			returnValue=START_STATE;
			printf("no server resp\n");
		}else if (flag==FNAME_BAD){
			printf("File %s not found\n", fname);
			returnValue = DONE;
		}else if(flag==DATA){
			returnValue=FILE_OK;	//were getting data b4 anticipated, just role with it :)
		}
	}

	return returnValue;
}

//open file for copying data.
STATE file_ok(int* outputFileFd, char* outputFileName){
	STATE returnValue = DONE;

	if((*outputFileFd = open(outputFileName, O_CREAT | O_TRUNC | O_WRONLY, 0600))<0){
		perror("File open error: ");
		returnValue = DONE;
	}else{
		returnValue=RECV_DATA;
	}
	return returnValue;
}

STATE recv_data(int32_t output_file, Connection* server, uint32_t* clientSeqNum, slidingWindow* window){
	uint32_t seq_num=0;
	// uint32_t ackSeqNum=0;
	uint8_t flag=0;
	int32_t data_len=0;
	uint8_t data_buf[MAX_LEN];
	// uint8_t packet[MAX_LEN];
	printf("entering recv state\n\n!\n");
	if(pollCall(LONG_TIME * 1000) == 0){
		printf("Timeout after 10 seconds, server must be gone.\n");
		return DONE;
	}
	while ( (windowRecvData(window, data_buf, &data_len, MAX_LEN, server->sk_num, server, &flag, &seq_num)) ==moreMSGs){	//while the 
		if(flag==END_OF_FILE){
			printf("File done\n");
			close(output_file);
			return DONE;
		} else{
			// //send ACK
			// ackSeqNum=htonl(seq_num);
			// send_buf((uint8_t*) &ackSeqNum, sizeof(ackSeqNum), server, ACK, *clientSeqNum, packet);
			// (*clientSeqNum)++;
			// //write to the file
			printf("writting to file\n");
			write(output_file, data_buf, data_len);	//somehow &data_buf worked earlier i think? try for now without
		}
	}
			if(flag==END_OF_FILE){
			printf("File done\n");
			close(output_file);
			return DONE;
		} else{
			// //send ACK
			// ackSeqNum=htonl(seq_num);
			// send_buf((uint8_t*) &ackSeqNum, sizeof(ackSeqNum), server, ACK, *clientSeqNum, packet);
			// (*clientSeqNum)++;
			// //write to the file
			printf("writting to file\n");
			write(output_file, data_buf, data_len);	//somehow &data_buf worked earlier i think? try for now without
		}
	
	// if(data_len == CRC_ERROR){	//ignore if CRC is wrong
	// 	return RECV_DATA;
	// }
	// if(flag==END_OF_FILE){
	// 	//send ACK
	// 	send_buf(packet, 1, server, EOF_ACK, *clientSeqNum, packet);
	// 	(*clientSeqNum)++;
	// 	printf("File done\n");
	// 	return DONE;
	// } else{
	// 	//send ACK
	// 	ackSeqNum=htonl(seq_num);
	// 	send_buf((uint8_t*) &ackSeqNum, sizeof(ackSeqNum), server, ACK, *clientSeqNum, packet);
	// 	(*clientSeqNum)++;
	// }

	// if(seq_num == expected_seq_num){
	// 	expected_seq_num++;
	// 	write(output_file, &data_buf, data_len);
	// }

	return RECV_DATA;
}

void check_args(int argc, char** argv){
	if(argc!=7){
		printf("Usage %s fromFile toFile buffer_size error_rate hostname port\n", argv[0]);
		exit(-1);
	}

	if(strlen(argv[1]) > 1000){
		printf("FROM filename too long, needs to be less than 1000 and is: %ld\n", strlen(argv[1]));
		exit(-1);
	}

	if(strlen(argv[2]) > 1000){
		printf("TO filename too long, needs to be less than 1000 and is: %ld\n", strlen(argv[2]));
		exit(-1);
	}

	if(atoi(argv[3]) < 400 || atoi(argv[3]) > 1400){
		printf("Buffer size needs to be between 400 and 1400 and is: %d\n", atoi(argv[3]));
		exit(-1);
	}

	if(atof(argv[4]) < 0 || atof(argv[4]) >=1){
		printf("Error rate needs to be between 0 and less than 1 and is: %f\n", atof(argv[4]));
		exit(-1);
	}
}

