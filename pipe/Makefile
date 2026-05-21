CC = gcc
CFLAGS = -Wall -pthread

all: server client

server: server.c pipe.c common.h
	$(CC) $(CFLAGS) -o server server.c pipe.c

client: client.c pipe.c common.h
	$(CC) $(CFLAGS) -o client client.c pipe.c

clean:
	rm -f server client *.o

.PHONY: all clean
