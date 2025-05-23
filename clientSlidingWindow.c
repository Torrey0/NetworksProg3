#include <sys/types.h>


#include "srej.h"
#include "networks.h"
#include "clientSlidingWindow.h"


//important in case sequence numbers or indices wrap around 2^32
int uint_lteMod(uint32_t a, uint32_t b) {
    return ((int32_t)(a - b)) <= 0;
}
int uint_ltMod(uint32_t a, uint32_t b) {
    return ((int32_t)(a - b)) < 0;
}

//assumes startingSeqNum is same for both server and client. for this prog it is (they are both 1)
slidingWindow* clientWindowInit(int windowSize, int32_t bufferSize, uint32_t startingSeqNum){
    slidingWindow* window = malloc(sizeof(slidingWindow));                     //allocate window struct
    window->msgBuffers=calloc(windowSize, sizeof(windowMSG));   //allocate array of windows

    for(int i=0;i<windowSize;i++){   //allocate buffer
        ((window->msgBuffers)[i]).windowBuffer = calloc(bufferSize, 1);      
        ((window->msgBuffers)[i]).valid=0;
    }   

    //will need to %windowSize whenever accessing window
    window->highestUnReadSeqNum=startingSeqNum;
    window->expectedSeqNum=startingSeqNum;  
    window->highestSeqRecv=startingSeqNum-1;
    window->windowSize=windowSize;
    window->clientSeqNum=startingSeqNum;
    return window;
}

int messageReady(slidingWindow* window){
    if(window->highestUnReadSeqNum != window->expectedSeqNum){
        return moreMSGs;
    }else{
        return noMSGs;
    }
}

//this function sends SREJ's for all packets with seq_num: highestIndex's seq_num < seq_num < recvSeq_num
//this is appropraitley called before adding a new out of order packet 
void sendSREJs(slidingWindow* window, Connection* connection, uint32_t recvSeq_num) {
    uint8_t packet[sizeof(uint32_t) + sizeof(Header)];
    int iter=window->highestSeqRecv+1;
    for(;uint_ltMod(iter, recvSeq_num); iter++){
        uint32_t nw_seqNumRej= htonl(iter);
        send_buf((uint8_t*)&nw_seqNumRej, sizeof(uint32_t), connection, SREJ, window->clientSeqNum, packet);
        (window->clientSeqNum)++;
    }
    if(uint_ltMod(window->highestSeqRecv, recvSeq_num)){
        window->highestSeqRecv=recvSeq_num;
    }
}

//sendRR for the current expectedSeqNum
void sendRR(slidingWindow* window, Connection* connection){
    uint8_t packet[sizeof(uint32_t) + sizeof(Header)];
    if(window->expectedSeqNum>2000000){
        printf("Pause: sending RR for %d\n", window->expectedSeqNum);
        while(1);
    }
    uint32_t nw_expseqNum=htonl(window->expectedSeqNum);
    send_buf((uint8_t*)&nw_expseqNum, sizeof(uint32_t), connection, ACK, window->clientSeqNum, packet);
    (window->clientSeqNum)++;
}

void add_OutOfOrder(slidingWindow* window, uint8_t* buf, uint32_t seq_num, int32_t dataLength) {
    //Insert the new message
    uint32_t insertIndex = seq_num % window->windowSize;
    memcpy(window->msgBuffers[insertIndex].windowBuffer, buf, dataLength);
    window->msgBuffers[insertIndex].windowBufferSize = dataLength;
    window->msgBuffers[insertIndex].seq_num = seq_num;
    window->msgBuffers[insertIndex].valid = 1;
    printf("setting out of order\n");
}

//check for in order msg. If we are waiting on in-order msg, it is expected that we have already sent RR for this msg
int checkInOrderMsgs(slidingWindow* window, uint8_t* buf, int32_t* dataLength){
    if(messageReady(window)){  //we have an inorder msg to send   
            uint32_t unReadIndex=((window->highestUnReadSeqNum)) % window->windowSize;
                        printf("providing message no: %d from out of order\n\n", window->highestUnReadSeqNum);
            *dataLength=window->msgBuffers[unReadIndex].windowBufferSize;
            memcpy(buf,window->msgBuffers[unReadIndex].windowBuffer, *dataLength);   //extract the window msg for client
            window->msgBuffers[unReadIndex].valid=0;
            (window->highestUnReadSeqNum)++;    //increment our highestUnreadSeqNum

            return 1;
        }
        return -1;
}

void sendEOFAck(slidingWindow* window, Connection* connection){
    uint8_t packet[sizeof(uint32_t) + sizeof(Header)];
    uint8_t emptyMessage;

    send_buf(&emptyMessage, 0, connection, EOF_ACK, window->clientSeqNum, packet);
}


//expects that a message is already on the window, and that client will keep calling as long as this returns 1. it is up to the client code to ensure this using poll.
//used by client. return 0 for no more messages. return 1 for more messages, keep reading (these will be messages that have been buffered by server)
//populates buf and dataLenth with the messagewith value if returnValue>0. buf only needs to be data Size
//Client uses the windowBuffer array as a circular queue, indexed by lower == current startingIndex. upper=highest msg we are waiting on.
int windowRecvData(slidingWindow* window, uint8_t* buf, int32_t* dataLength, int32_t len, int32_t sk_num, Connection* connection, uint8_t* flag, uint32_t* seq_num){
    int returnValue;
    printf("windo info: expected: %d highestRec: %d highestUnread: %d clientSeqnum: %d\n", window->expectedSeqNum, window->highestSeqRecv, window->highestUnReadSeqNum, window->clientSeqNum);
    if((returnValue=checkInOrderMsgs(window, buf, dataLength)) >0){ //if we are waiting on an inorder msg, just give them that
        printf("flushing\n");

        return messageReady(window);
    }
    //otherwise, read new msg 
    *dataLength = recv_buf(buf, len, sk_num, connection, flag, seq_num);
    

    //check for reasons to ignore this message.
    if (*dataLength == CRC_ERROR) { //check for CRC error
        printf("CRC error: Ignoring corrupted message.\n");
        return messageReady(window);
    }

        if(uint_ltMod(*seq_num, window->expectedSeqNum)){   //ignore if seq_num too small (likely from server nudge). send an RR to indicate what we are currently waiting for
        sendRR(window, connection);
        printf("recieved potential nudge\n");
        return messageReady(window);
    }

    if((!isData(*flag)) && *flag!=END_OF_FILE){  //check if flags valid
        printf("Error: windowRecieve unrecognized flag\n");
        *dataLength=CRC_ERROR;
        return messageReady(window);
    }
    if(*seq_num != window->expectedSeqNum){ //check for out of order msg
        printf("out of order\n");
        sendSREJs(window, connection, *seq_num);
        add_OutOfOrder(window, buf, *seq_num, *dataLength);//if msg is out of order, add it to buffer so we can send it later
        *dataLength=CRC_ERROR;
        return messageReady(window);
    }

    //otherwise this message is in order and ready for the client
    if(*flag==END_OF_FILE){ //the client is expected to see this and finish
        //send ACK
        sendEOFAck(window, connection);
        printf("File done\n");
        *dataLength=0;
        return 0; 
    }
    if (*dataLength==0){    //no message to read
        printf("warning, msg with no data, or incorrect call of this function perhaps?\n");
        return messageReady(window);
    }
    //otherwise, the msg seems legit and is in order :). Can return directly to client
    int iterIndex=(*seq_num)+1;
    (window->expectedSeqNum)++;   //increment seqNum for msgs we are returning
    (window->highestUnReadSeqNum)++;
    while (//iterIndex != window->highestSeqRecv &&             // Iterate over msgs to send appropraite RR, we still only return the first one here. if we go over multiple msgs here, the skipped msgs will be returned when calling checkInOrderMsgs
            (window->msgBuffers[iterIndex % window->windowSize]).valid) {        //I think can just check if valid?
                printf("\nsucessful itering\n");
        // Advance window
        iterIndex++;
        (window->expectedSeqNum)++;   //increment seqNum for msgs we are buffering

    }
    sendRR(window, connection);             //send RR up until currently expected msg.
    if(uint_ltMod(window->highestSeqRecv, *seq_num)){   //update our highest recvSeqNum
        window->highestSeqRecv=*seq_num;
    }
    printf("providing message no: %d from in order\n\n", *seq_num);
    return messageReady(window);
} 
