/*
João Maria Campos Donato 2020217878
José Miguel Norte de Matos 2020217977
*/
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <sys/msg.h>

#define NUM_PROCESS_INI 3

typedef struct
{
        char name[20];
        pthread_t vCPU[2];
        int vCPU_1_capacity;
        int vCPU_2_capacity;
        int running;
} edge_server;

typedef struct
{
        int requested_tasks;
        int executed_tasks;
        int total_anwser_time;
        int *executed_tasks_per_server;
        int *maintenance_per_server;
        int unanswered_tasks;

} stats;

typedef struct
{
        int id;
        int thousInstructPerRequest;
        int maxExecTimeSecs;
} task;

typedef struct
{
        task tsk;
        time_t timeOfEntry;
} request;

typedef struct
{
        long msg_type;
        char msg_text[100];
} message;

typedef struct
{
        // config sets
        int EDGE_SERVER_NUMBER;
        int QUEUE_POS;
        int MAX_WAIT;
        // edge server array
        edge_server *EDGE_SERVERS;
        // task manager threads
        pthread_t taskmanager[2];
        // process id's
        pid_t c_pid[NUM_PROCESS_INI];
        pid_t *edge_pid;
        int queue_id;
        // dispacher and scheduler condition to kill
        int shutdown;
        // elements in task queue
        int num_queue;
        // global vcpu performance
        int performance_flag;
        // system stats
        stats simulation_stats;
        // minimum wait time to execute next task
        int minimum_wait_time;
        // conditions
        

        pthread_cond_t monitorCond;
        pthread_cond_t dispatcherCond;
        pthread_cond_t schedulerCond;
        pthread_mutex_t dispatcherMutex;
        pthread_mutex_t monitorMutex;
        pthread_mutex_t schedulerMutex;
        int monitorWork;
        int dispatcherWork;
        int schedulerWork;

} shared_memory;
