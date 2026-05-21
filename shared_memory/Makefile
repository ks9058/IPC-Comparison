CC = gcc
CFLAGS = -Wall -pthread

all: server client

server: server.c shared_memory.c common.h
	$(CC) $(CFLAGS) -o server server.c shared_memory.c

client: client.c shared_memory.c common.h
	$(CC) $(CFLAGS) -o client client.c shared_memory.c

clean:
	rm -f server client *.o

.PHONY: all clean
