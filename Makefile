# Makefile for CPE464 tcp and udp test code
# updated by Hugh Smith - April 2023

# all target makes UDP test code
# tcpAll target makes the TCP test code


CC= gcc
CFLAGS= -g -Wall -std=gnu99
LIBS = 

OBJS = networks.o srej.o pollLib.o safeUtil.o clientSlidingWindow.o serverSlidingWindow.o gethostbyname.o

#uncomment next two lines if your using sendtoErr() library
# LIBS += libcpe464.2.21.a -lstdc++ -ldl
LIBS += libcpe464.2.21.a -lstdc++ -ldl

CFLAGS += -D__LIBCPE464__


all: rcopyAll

rcopyAll: rcopy server
tcpAll: rcopy server

rcopy: rcopy.c $(OBJS) 
	$(CC) $(CFLAGS) -o rcopy rcopy.c $(OBJS) $(LIBS)

server: server.c $(OBJS) 
	$(CC) $(CFLAGS) -o server server.c  $(OBJS) $(LIBS)

myClient: myClient.c $(OBJS)
	$(CC) $(CFLAGS) -o myClient myClient.c  $(OBJS) $(LIBS)

myServer: myServer.c $(OBJS)
	$(CC) $(CFLAGS) -o myServer myServer.c $(OBJS) $(LIBS)

.c.o:
	gcc -c $(CFLAGS) $< -o $@

cleano:
	rm -f *.o

clean:
	rm -f myServer myClient rcopy server *.o




