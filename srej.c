/* srej.c */

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

#include "pollLib.h"
#include "networks.h"
#include "srej.h"
#include "checksum.h"

int32_t send_buf(uint8_t* buf, uint32_t len, Connection* connection, uint8_t flag, uint32_t seq_num, uint8_t* packet){
    int32_t sentLen=0;
    int32_t sendingLen=0;
    //set up packet: (seq#, crc, flag, data)
    if(len>0){
        memcpy(&packet[sizeof(Header)], buf, len);
    }
        sendingLen=createHeader(len, flag, seq_num, packet);
        sentLen=safeSendto(packet, sendingLen, connection);
        return sentLen;
}

int createHeader(uint32_t len, uint8_t flag, uint32_t seq_num, uint8_t* packet){
    //create the regular header including seq num, flag, checksum
    Header* aHeader=(Header*) packet;
    uint16_t checksum=0;

    seq_num=htonl(seq_num);
    memcpy(&(aHeader->seq_num), &seq_num, sizeof(seq_num));

    aHeader->flag=flag;

    memset(&aHeader->checksum, 0, sizeof(checksum));
    checksum = in_cksum((unsigned short*) packet, len+sizeof(Header));
    memcpy(&aHeader->checksum, &checksum, sizeof(checksum));

    return len+ sizeof(Header);
}

int32_t recv_buf(uint8_t* buf, int32_t len, int32_t recv_sk_num, Connection* connection, uint8_t* flag, uint32_t* seq_num){
    uint8_t data_buf[MAX_LEN];
    int32_t recv_len=0;
    int32_t dataLen=0;

    recv_len=safeRecvfrom(recv_sk_num, data_buf, len, connection);
    dataLen=retrieveHeader(data_buf, recv_len, flag, seq_num);

    //dataLen=-1 for crc error, or dataLen=0 if no data
    if(dataLen>0){
        memcpy(buf, &data_buf[sizeof(Header)], dataLen);
    }

    return dataLen;
}

int retrieveHeader(uint8_t* data_buf, int recv_len, uint8_t* flag, uint32_t* seq_num){
    Header* aHeader= (Header*) data_buf;
    int returnValue=0;

    if(in_cksum((unsigned short *) data_buf, recv_len) != 0){
        returnValue=CRC_ERROR;
    }else{
        *flag=aHeader->flag;
        memcpy(seq_num, &aHeader->seq_num, sizeof(aHeader->seq_num));
        *seq_num = ntohl(*seq_num);

        returnValue = recv_len - sizeof(Header);
    }

    return returnValue;
}

int processSelect(Connection* client, int* retryCount, int selectTimeoutState, int dataReadyState, int doneState){
    //Return:
    //doneState if calling this funciton exceeds MAX_TRIES
    //selectTimeoutState if the select times out without recieving anything
    //dataReadyState if select() return indicating that data is ready for read

    int returnValue=dataReadyState;

    (*retryCount)++;
    if(*retryCount> MAX_TRIES){
        printf("No response for other side for %d attempts, terminating connection\n", MAX_TRIES);
        returnValue=doneState;
    }else{  //poll_call(SHORT_TIME*1000)
        int pollRet=pollCall(SHORT_TIME*1000);
        if(pollRet==-1){    //timeout
            returnValue = selectTimeoutState;
            printf("claimed Timout\n");
        }else if (pollRet==client->sk_num){
            //no data ready
            returnValue=dataReadyState;
            *retryCount=0;
            printf("claimed Mathch");
        } else{
            printf("poll Library issue,  recv msg on fd that shouldnt exist\n");
            returnValue=selectTimeoutState;
        }
        // if(select_call(client->sk_num, SHORT_TIME)==1){
        //     *retryCount=0;
        //     returnValue = dataReadyState;
        // }else{
        //     //no data ready
        //     returnValue=selectTimeoutState;
        // }
    }
    
    return returnValue;
}