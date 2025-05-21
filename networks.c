
// Hugh Smith April 2017
// Network code to support TCP/UDP client and server connections

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

#include "networks.h"
#include "cpe464.h"
#include "gethostbyname.h"
#include "pollLib.h"


int safeGetUdpSocket(){
	int socketNumber=0;
	if((socketNumber=socket(AF_INET6, SOCK_DGRAM, 0)) < 0){
		perror("safeGetUdpSocket(), socket(), call: ");
		exit(-1);
	}
	return socketNumber;
}

int safeSendto(uint8_t* packet, uint32_t len, Connection* to){
	int send_len=0;
	if((send_len=sendtoErr(to->sk_num, packet, len, 0, (struct sockaddr*) &(to->remote), to->len))<0){
		perror("in safeSendto(), sendto() call");
		exit(-1);
	}
	return send_len;
}

int safeRecvfrom(int recv_sk_num, uint8_t* packet, int len, Connection* from){
	int recv_len=0;
	from->len = sizeof(struct sockaddr_in6);
	if((recv_len=recvfrom(recv_sk_num, packet, len, 0, (struct sockaddr*) &(from->remote), &from->len))<0){
		perror("in safeRecvfrom, recvfrom call");
		exit(-1);
	}

	return recv_len;
}

int udpServerSetup(int portNumber){
	struct sockaddr_in6 server;
	int socketNumber = 0;
	int serverAddrLen = 0;

	//create socket
	socketNumber=safeGetUdpSocket();
	//setup socket
	server.sin6_family=AF_INET6;	//IPv4 or IPv6 family
	server.sin6_addr=in6addr_any;	//use an local IP addres
	server.sin6_port=htons(portNumber);

	//bind the name (address) to port
	if(bind(socketNumber, (struct sockaddr*) &server, sizeof(server))<0){
		perror("bind() call error");
		exit(-1);
	}
	//get port number;
	serverAddrLen=sizeof(server);
	getsockname(socketNumber, (struct sockaddr*) &server, (socklen_t *) &serverAddrLen);
	printf("Server using Port #: %d\n", ntohs(server.sin6_port));
	
	return socketNumber;
}

int udpClientSetup(char* hostName, int portNumber, Connection* connection){
	memset(&connection->remote, 0, sizeof(struct sockaddr_in6));
	connection->sk_num=0;
	connection->len=sizeof(struct sockaddr_in6);
	connection->remote.sin6_family= AF_INET6;
	connection->remote.sin6_port=htons(portNumber);
	//create the socket
	connection->sk_num=safeGetUdpSocket();

	if(gethostbyname6(hostName, &connection->remote) == NULL){
		printf("Host not found: %s\n", hostName);
		return -1;
	}
	//add them to the poll set
	setupPollSet();	//setup poll set
	
	addToPollSet(connection->sk_num);

	printf("Server info - ");
	printIPv6Info(&connection->remote);
	return 0;
}

//block forever if seconds=-1
int select_call(int32_t socket_num, int32_t seconds){
	//only works on 1-scoket (one fd), but thats all that is needed for this program, so we do be forking
	fd_set fdvar;
	struct timeval aTimeout;
	struct timeval* timeout=NULL;

	//if seconds is -1, interpret that as blocking forever, (so leave timeout NULL);
	if(seconds!=-1){
		aTimeout.tv_sec=seconds;
		aTimeout.tv_usec=0;
		timeout=&aTimeout;
	}

	FD_ZERO(&fdvar);	//reset variables
	FD_SET(socket_num, &fdvar);

	if(select(socket_num+1, (fd_set*) &fdvar, NULL, NULL, timeout) <0){
		perror("select");
		exit(-1);
	}

	if(FD_ISSET(socket_num, &fdvar)){
		return 1;
	}else{
		return 0;
	}
}

void printIPv6Info(struct sockaddr_in6* client){
	char ipString[INET6_ADDRSTRLEN];

	inet_ntop(AF_INET6, &client->sin6_addr, ipString, sizeof(ipString));
	printf("IP: %s Port: %d\n", ipString, ntohs(client->sin6_port));
}
