#ifndef __SLIDING_WINDOW_H__
#define __SLIDING_WINDOW_H__

#include <sys/types.h>

#include "networks.h"

typedef enum {
    noMSGs = 0,
    moreMSGs = 1,
    EOF_STATUS =2
} MsgStatus;

typedef struct windowMessage windowMSG;

typedef struct sWindow slidingWindow;

//window is essentially just a circular array buffer. buffer will have O(n) lookup for mathcing sq
struct sWindow{
    uint32_t expectedSeqNum;  //our current highest RR, anything larger than this goes straight to the buffer
     uint32_t highestUnReadSeqNum; //the seq num the user has read is not necessarily caught up with what the buffer is expecting, it may fall behind if multiple msgs become readable when a new one arrives that was causing a hold-up

    uint32_t highestSeqRecv;
    // uint64_t startingIndex; //current bounds for circular array (msgBuffers)
    // uint64_t highestIndex;
    windowMSG* msgBuffers;
    int windowSize; //windowSize=length of the msgBuffer
    uint32_t clientSeqNum;  //incremented every time the client sends a msg. tbh not really used for anything
};

struct windowMessage{
    uint8_t* windowBuffer;     //array of length windowSize. Containing buffers of size bufferSize
    int32_t windowBufferSize; //how may bytes of data each windowBuffer contains
    uint32_t seq_num;   //when we recieve an old sequence number, use this to find it
    int8_t valid;
    uint8_t flag;
};

//initializes values, returns a window to be passed back to these Functions later
slidingWindow* clientWindowInit(int windowSize, int32_t bufferSize, uint32_t startingSeqNum);

//used by client
//recieves Data, also sends ACKS and SREJ's
int windowRecvData(slidingWindow* window, uint8_t* buf, int32_t* dataLen, int32_t len, int32_t recv_sk_num, Connection* connection, uint8_t* flag, uint32_t* seq_num);



#endif