
// 	Writen - HMS April 2017
//  Supports TCP and UDP - both client and server


#ifndef __NETWORKS_H__
#define __NETWORKS_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BACKLOG 10

typedef struct connection Connection;

struct connection{
    int32_t sk_num;
    struct sockaddr_in6 remote;
    uint32_t len;
};

int safeGetUdpSocket();
int udpServerSetup(int portNumber);
int udpClientSetup(char* hostname, int portNum, Connection* connection);
int select_call(int32_t socketNum, int32_t seconds);
int safeSendto(uint8_t* packet, uint32_t len, Connection* connection);
int safeRecvfrom(int recv_sk_num, uint8_t* packet, int len, Connection* from);

//just for printing:
void printIPv6Info(struct sockaddr_in6* client);

#endif
