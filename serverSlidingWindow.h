#ifndef __SLIDING_WINDOW_H__
#define __SLIDING_WINDOW_H__

#include <sys/types.h>

#include "networks.h"

// Connection;
typedef struct sWindow slidingWindow;

struct sWindow{
	int64_t lower;
    int64_t current;
    int64_t upper;

    uint8_t** windowBuffer;     //array of length windowSize. Containing buffers of size bufferSize
    int32_t* windowBufferSizes; //how may bytes of data each windowBuffer contains
    int windowSize;
};

//initializes values, returns a window to be passed back to these Functions later
slidingWindow* serverWindowInit(int windowSize, int bufferSize, int startingSeqNum);

//returns false if current==upper, true otherwise. Used to check if the server should keep sending messages
int windowOpen(slidingWindow* window);

//window wrapper for recv_buf. expects something to be there already when it is called. (ie, it is up to the user of the API to use poll)
int windowRecieve(slidingWindow* window, uint8_t* buf, int32_t len, int32_t recv_sk_num, Connection* connection, uint8_t* flag, uint32_t* seq_num);

//window wrapper for send_buf. user of API calls this to send data, which they should do so long as windowOpen returns true.
int32_t window_send_data(slidingWindow* window, uint8_t* buf, uint32_t len, Connection* connection, uint8_t flag, uint32_t* seq_num, uint8_t* packet);

//used to nudge the client in the event of timeout by sending lowest packet that has not been RR'ed
void windowSendLowest(slidingWindow* window, Connection* connection, uint8_t* packet);

#endif