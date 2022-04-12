CC=gcc
CFLAGS= -Wall -pthread

all: simulator mobile_node

simulator: main.c simulation_structs.h
	$(CC) $(CFLAGS) -o simulator main.c simulation_structs.h

mobile_node: mobile_node.c
	$(CC) $(CFLAGS) -o mobile_node mobile_node.c

clean:
	-rm simulator mobile_node
