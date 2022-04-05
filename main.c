#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

#define PIPE_NAME "TASK_PIPE"
#define NUM_EDGESERVERS_THREADS 2

void *system_manager(void *p);
void *task_manager(void *p);
void *edge_server(void *p);
void *monitor(void *p);
void *maintenance_manager(void *p);

int main(int argc, char const *argv[])
{
      /* code */
      return 0;
}
