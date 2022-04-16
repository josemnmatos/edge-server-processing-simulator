CC=gcc
CFLAGS= -Wall -pthread

all: offload_simulator mobile_node

offload_simulator: offload_simulator.c simulation_structs.h
	$(CC) $(CFLAGS) -o offload_simulator offload_simulator.c simulation_structs.h

mobile_node: mobile_node.c
	$(CC) $(CFLAGS) -o mobile_node mobile_node.c

clean:
	-rm offload_simulator mobile_node
