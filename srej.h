/* Server.h */

#ifndef __SREJ_H__
#define __SREJ_H__

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
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
#include "checksum.h"

#define MAX_LEN 1400    //1400 max payload len for this ass.
#define SIZE_OF_BUF_SIZE 4
#define START_SEQ_NUM 1
#define MAX_TRIES 10
#define LONG_TIME 10
#define SHORT_TIME 1

#pragma pack(1)

typedef struct header Header;

struct header{
    uint32_t seq_num;
    uint16_t checksum;
    uint8_t flag;
};

enum FLAG{
    DATA=16, RESENT_SREJ=17, RESENT_TIMEOUT=18, ACK=5, SREJ=6, FNAME=8, FNAME_RESP=9, END_OF_FILE=10, CRC_ERROR=-1, EOF_ACK=11,
};
//data to be placed in FNAME_RESP packets to indicate status
#define FNAME_OK 9
#define FNAME_BAD 10

int8_t isData(uint8_t flag);
int32_t send_buf(uint8_t* buf, uint32_t len, Connection* connection, uint8_t flag, uint32_t seq_num, uint8_t* packet);

int createHeader(uint32_t len, uint8_t flag, uint32_t seq_num, uint8_t* packet);

int32_t recv_buf(uint8_t* buf, int32_t len, int32_t recv_sk_num, Connection* connection, uint8_t* flag, uint32_t* seq_num);

int retrieveHeader(uint8_t* data_buf, int recv_len, uint8_t* flag, uint32_t* seq_num);

int processSelect(Connection* client, int* retryCount, int selectTimeoutState, int dataReadyState, int doneState);

#endif