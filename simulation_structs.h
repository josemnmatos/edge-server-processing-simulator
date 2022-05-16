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
        int total_server_number;
        int server_number;
} maint_thread_info;

typedef struct
{
        int vcpu_number;
        int server_number;
        sem_t *free_sem;

} vcpu_info;

typedef struct
{
        pthread_t thread;
        int capacity;
        volatile int free;
        
} vcpu;

typedef struct
{
        char name[20];
        vcpu vcpu[2];
        int executed_tasks;
        int maintenance_counter;
        int stopped;
        int stopped_vcpus;

} edge_server;

typedef struct
{
        int requested_tasks;
        int executed_tasks;
        int total_anwser_time;
        int unanswered_tasks;
        int *executed_pserver;
        int *maintenance_pserver;

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
        char msg_text[48];
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
        // minimum waiting time
        int *min_waiting;
        // process id's
        pid_t sm_pid;
        pid_t c_pid[NUM_PROCESS_INI];
        pid_t *edge_pid;
        // dispacher and scheduler condition to kill
        int shutdown;
        // elements in task queue
        int num_queue;
        // global vcpu performance
        int performance_flag;
        // system stats
        stats simulation_stats;
        // vcpu times tasks
        int **times_edgeserver;

        // conditions

        int **taskToProcess;
        pthread_cond_t *edgeServerCond;
        pthread_mutex_t *edgeServerMutex;

        pthread_cond_t monitorCond;
        pthread_cond_t dispatcherCond;
        pthread_cond_t schedulerCond;
        pthread_mutex_t dispatcherMutex;
        pthread_mutex_t monitorMutex;
        pthread_mutex_t schedulerMutex;

        pthread_mutex_t *stoppedMutex;

        pthread_cond_t continueDispatchCond;
        pthread_mutex_t continueDispatchMutex;

        int continue_dispatch;

        int monitorWork;
        int dispatcherWork;
        int schedulerWork;
        int vcpuWork;

} shared_memory;
