#include <sys/types.h>


#include "srej.h"
#include "networks.h"
#include "clientSlidingWindow.h"


windowMSG* getStartMessage(slidingWindow* window){
    return window->msgBuffers + (window->startingIndex % window->windowSize);
}
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
    }

    //will need to %windowSize whenever accessing window
    window->startingIndex=0;
    window->highestIndex=0;
    window->expectedSeqNum=startingSeqNum;  
    window->windowSize=windowSize;
    window->clientSeqNum=startingSeqNum;
    return window;
}
int messageReady(slidingWindow* window){
    if(window->startingIndex != window->highestIndex &&               
        (uint_lteMod((getStartMessage(window))->seq_num, window->expectedSeqNum))){
            return moreMSGs;   //another message is ready
        }
    return noMSGs;   //no more messages are ready
}

//this function sends SREJ's for all packets with seq_num: highestIndex's seq_num < seq_num < recvSeq_num
//this is appropraitley called before adding a new out of order packet 
void sendSREJs(slidingWindow* window, Connection* connection, uint32_t recvSeq_num) {
    uint8_t packet[sizeof(uint32_t) + sizeof(Header)];

    // Determine the next expected sequence number after the current highest
    uint32_t lastIndex = (window->highestIndex + window->windowSize - 1) % window->windowSize;
    uint32_t prevHighestSeq = window->msgBuffers[lastIndex].seq_num;

    printf("Sending SREJ: last Index: %d, prevHighestSeq: %d\n", lastIndex, prevHighestSeq);
    // Send SREJs for all sequence numbers between curSeq and recvSeq_num (exclusive)
    for (uint32_t srej_seq = prevHighestSeq + 1; uint_ltMod(srej_seq, recvSeq_num); srej_seq++) {
        // Create and send SREJ packet
        uint32_t nw_srej_seq=htonl(srej_seq);
        send_buf((uint8_t*)&nw_srej_seq, sizeof(uint32_t), connection, SREJ, window->clientSeqNum, packet);
        (window->clientSeqNum)++;
    }
}

void sendRR(slidingWindow* window, Connection* connection){
    uint8_t packet[sizeof(uint32_t) + sizeof(Header)];
    uint32_t nw_expseqNum=htonl(window->expectedSeqNum);
    send_buf((uint8_t*)&nw_expseqNum, sizeof(uint32_t), connection, ACK, window->clientSeqNum, packet);
    (window->clientSeqNum)++;
}

void add_OutOfOrder(slidingWindow* window, uint8_t* buf, uint32_t seq_num, int32_t dataLength) {
    int insertAbsIdx = window->highestIndex;  // default to insert at the end

    // Step 1: Find insertion point by sequence number
    for (int i = window->startingIndex; i != window->highestIndex; i++) {
        uint32_t bufferIdx = i % window->windowSize;
        if (uint_ltMod(seq_num, window->msgBuffers[bufferIdx].seq_num)) {
            insertAbsIdx = i;
            break;
        }
    }

    // Step 2: Shift entries one step forward from highestIndex down to insertAbsIdx
    for (int i = window->highestIndex; i > insertAbsIdx; i--) {
        uint32_t from = (i - 1) % window->windowSize;
        uint32_t to = i % window->windowSize;

        // Shallow copy is safe if windowBuffer is pre-allocated
        window->msgBuffers[to] = window->msgBuffers[from];
    }

    // Step 3: Insert the new message
    uint32_t insertIdx = insertAbsIdx % window->windowSize;
    memcpy(window->msgBuffers[insertIdx].windowBuffer, buf, dataLength);
    window->msgBuffers[insertIdx].windowBufferSize = dataLength;
    window->msgBuffers[insertIdx].seq_num = seq_num;

    // Step 4: Increment highestIndex to reflect the new size
    (window->highestIndex)++;
}

//check for in order msg. If we are waiting on in-order msg, it is expected that we have already sent RR for this msg
int checkInOrderMsgs(slidingWindow* window, uint8_t* buf, int32_t* dataLength){
    if(messageReady(window)){  //we have an inorder msg to send   
            windowMSG* retrievedWindow=window->msgBuffers + (((window->startingIndex)) % window->windowSize);
            memcpy(buf,retrievedWindow->windowBuffer, retrievedWindow->windowBufferSize);   //extract the window msg for client
            (window->startingIndex)++;
            return retrievedWindow->windowBufferSize;
        }
        return -1;
}

void sendEOFAck(slidingWindow* window, Connection* connection){
    uint8_t packet[sizeof(uint32_t) + sizeof(Header)];
    uint8_t emptyMessage;

    send_buf(&emptyMessage, 0, connection, ACK, window->clientSeqNum, packet);
}


//expects that a message is already on the window, and that client will keep calling as long as this returns 1. it is up to the client code to ensure this using poll.
//used by client. return 0 for no more messages. return 1 for more messages, keep reading (these will be messages that have been buffered by server)
//populates buf and dataLenth with the messagewith value if returnValue>0. buf only needs to be data Size
//Client uses the windowBuffer array as a circular queue, indexed by lower == current startingIndex. upper=highest msg we are waiting on.
int windowRecvData(slidingWindow* window, uint8_t* buf, int32_t* dataLength, int32_t len, int32_t sk_num, Connection* connection, uint8_t* flag, uint32_t* seq_num){
    int returnValue;
    if((returnValue=checkInOrderMsgs(window, buf, dataLength)) >0){ //if we are waiting on an inorder msg, just give them that
        return messageReady(window);
    }
    while (1) {     //otherwise, read new msg until we find a msg that doesnt have an error
        *dataLength = recv_buf(buf, len, sk_num, connection, flag, seq_num);
        //check for reasons to ignore this message.
        if (*dataLength == CRC_ERROR) { //check for CRC error
            printf("CRC error: Ignoring corrupted message.\n");
            continue;
        }
    
        if (window->highestIndex - window->startingIndex >= window->windowSize) {   //check if our window is full
            printf("Window full: Ignoring message with seq_num %u.\n", *seq_num);
            continue;
        }
        if(*flag!=DATA && *flag!=END_OF_FILE){  //check if flags valid
            printf("Error: windowRecieve unrecognized flag\n");
            continue;
        }
        if(*seq_num != window->expectedSeqNum){ //check for out of order msg
            sendSREJs(window, connection, *seq_num);
            add_OutOfOrder(window, buf, *seq_num, *dataLength);//if msg is out of order, add it to buffer so we can send it later

            continue;
        }
        break; // Message seems legit, proceed to processing
    }
    if(*flag==END_OF_FILE){ //the client is expected to see this and finish
        //send ACK
        sendEOFAck(window, connection);
        printf("File done\n");
        return 0; 
    }
    if (*dataLength==0){    //no message to read
        printf("warning, msg with no data, or incorrect call of this function perhaps?\n");
        return messageReady(window);
    }
    //otherwise, the msg seems legit and is in order :). Can return directly to client
    int iterIndex=window->startingIndex;
    window->expectedSeqNum++;   //increment seqNum for msgs we are returning
    while (iterIndex != window->highestIndex &&             // Iterate over msgs to send appropraite RR, we still only return the first one here. if we go over multiple msgs here, the skipped msgs will be returned when calling checkInOrderMsgs
            window->msgBuffers[iterIndex % window->windowSize].seq_num == window->expectedSeqNum) {        
        // Advance window
        iterIndex++;
        window->expectedSeqNum++;   //increment seqNum for msgs we are buffering
    }
    sendRR(window, connection);             //send RR up until currently expected msg.
    return messageReady(window);
} 
