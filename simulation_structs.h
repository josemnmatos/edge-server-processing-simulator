#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#define NUM_PROCESS_INI 3

struct edge_server
{
        char name[20];
        int vCPU_1_capacity;
        int vCPU_2_capacity;
};

typedef struct
{
        int EDGE_SERVER_NUMBER;
        int QUEUE_POS;
        int MAX_WAIT;
        struct edge_server *EDGE_SERVERS;
        pid_t taskmanager[2];
        pid_t c_pid[NUM_PROCESS_INI];
        pid_t *edge_pid;
} shared_memory;