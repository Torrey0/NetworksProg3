#include <sys/types.h>


#include "srej.h"
#include "networks.h"
#include "serverSlidingWindow.h"


//initializes static values, only one window per process with current impl, Much like pollLib
slidingWindow* serverWindowInit(int windowSize, int bufferSize, int startingSeqNum){
    slidingWindow* window = malloc(sizeof(slidingWindow));                     //allocate window struct
    window->windowBuffer=calloc(windowSize, sizeof(uint8_t*));   //allocate array of windows
    window->windowBufferSizes=calloc(windowSize, sizeof(*window->windowBufferSizes));
    for(int i=0;i<windowSize;i++){
        (window->windowBuffer)[i] = calloc(bufferSize, 1);       //allocate each window
    }

    //will need to %windowSize whenever accessing window
    window->lower=startingSeqNum;
    window->current=startingSeqNum;
    window->upper=startingSeqNum+windowSize;
    window->windowSize=windowSize;
    return window;
}

//returns false if current==upper, true otherwise
int windowOpen(slidingWindow* window){
    return window->current != window->upper;
}

int receiveACK(slidingWindow* window, uint8_t* buf, int32_t len){
        //extract sequence number be RR'ed
        if(len!=sizeof(int32_t)){
            printf("Error, recieved an ACK with length= %d != size of seq number\n", len);
            return CRC_ERROR;
        }
        int32_t recv_seqNum=0;
        memcpy(&recv_seqNum, buf, sizeof(int32_t));
        recv_seqNum=ntohl(recv_seqNum);
        //compute change (how many packets have been confirmed)
        int change=recv_seqNum - window->lower;   //how many packets we can mark as recv
        if(change<0){
            printf("Warning, recieved an old ACK for frame no longer in window! ignoring\n");
            return CRC_ERROR;
        }
        
        //update Window lower and upper
        window->lower +=change;
        window->upper += change;
    
        if(window->lower>window->current){
            printf("Error: lower>current. In windowRecieve\n");
            exit(-1);
        }
    
        return 0;
}

int receiveSREJ(slidingWindow* window, uint8_t* buf, int32_t len, Connection* connection, uint32_t* seq_num){
    uint8_t sendingPacketBuffer[MAX_LEN + sizeof(Header)];

    
    //extract sequence number be SREJ'ed
    if(len!=sizeof(int32_t)){
        printf("Error, recieved an SREJ with length= %d != size of seq number\n", len);
        return CRC_ERROR;
    }
    int32_t rejected_seqNum=0;
    memcpy(&rejected_seqNum, buf, sizeof(int32_t));
    rejected_seqNum=ntohl(rejected_seqNum);
    //check if the obtained seqNum is in bounds
    if (rejected_seqNum < window->lower || rejected_seqNum >= window->current) {
        printf("Error: Received SREJ for sequence number outside current window. Ignoring.\n");
        return CRC_ERROR;
    }

    int32_t rejectedIndex=rejected_seqNum % window->windowSize;

    //resend the missing packet
    send_buf(window->windowBuffer[rejectedIndex],window->windowBufferSizes[rejectedIndex] ,connection, DATA, rejected_seqNum, sendingPacketBuffer);
    (*seq_num)++;
    return 0;
    
}

//used by server
//window wrapper for recv_buf. return CRC_ERROR if any issues with the packet (not limited to CRC), otherwise returns 0
int windowRecieve(slidingWindow* window, uint8_t* buf, int32_t len, int32_t sk_num, Connection* connection, uint8_t* flag, uint32_t* seq_num){
    int dataLen=recv_buf(buf, len, sk_num, connection, flag, seq_num);  //populates seq_num and flag
    if(dataLen == CRC_ERROR){   //ignore msgs with CRC errors
        printf("CRC error\n");
        return CRC_ERROR;
    }

    if(*flag==EOF_ACK || *flag== ACK ){
        printf("recv ACK\n");
        return receiveACK(window, buf, dataLen);
    } else if(*flag == SREJ){
        printf("recv SREJ\n");
        return receiveSREJ(window, buf, dataLen, connection, seq_num);
    }else{
        printf("Error: windowRecieve unrecognized flag\n");
        return CRC_ERROR; //ignore non ACK or SREJ msgs.
    }
}

//window wrapper for send_buf. sends packet which is created from header + data in buf
int32_t window_send_data(slidingWindow* window, uint8_t* buf, uint32_t len, Connection* connection, uint8_t flag, uint32_t* seq_num, uint8_t* packet){
    
    if(!windowOpen(window)){
        printf("Warning Attempting to send on a full window! Ignoring Request\n");
        return -1;
    }
    int windowIndex= window->current % window->windowSize;
    memcpy(window->windowBuffer[windowIndex],buf, len);
    window->windowBufferSizes[windowIndex] = len;
    window->current++;

    int32_t result = send_buf(buf, len, connection, flag, *seq_num, packet);
    (*seq_num)++;
    return result;
}

