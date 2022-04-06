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
#include "edge_server.h"

#define PIPE_NAME "TASK_PIPE"


int shmid;



FILE *log_ptr, *config_ptr;

typedef struct {
      int EDGE_SERVER_NUMBER;
      int QUEUE_POS;
      int MAX_WAIT;
      struct edge_server *EDGE_SERVERS;
}shared_memory;

shared_memory *SM;

void system_manager(shared_memory * SM);
void *task_manager(void *p);
void *edge_server_process(void *p);
void *monitor(void *p);
void *maintenance_manager(void *p);
void get_running_config(FILE *ptr, shared_memory  *SM);
void show_server_info(struct edge_server s);
void output_str(char *s);
void end_sim();

// compile with : gcc -Wall -pthread main.c edge_server.h -o test

int main()
{
      /* code */
      output_str("OFFLOAD SIMULATOR STARTING\n");
      //create Shared Memory
      shmid = shmget(IPC_PRIVATE, sizeof(shared_memory), IPC_CREAT | 0700);
      SM = (shared_memory*) shmat(shmid, NULL, 0);

      system_manager(SM);

      end_sim();
      return 0;
}

void system_manager(shared_memory * SM)
{
      // create log file
      log_ptr = fopen("log.txt", "w");
      fclose(log_ptr);
      // open config file
      config_ptr = fopen("config.txt", "r");
      get_running_config(config_ptr, SM);
      output_str("CONFIGURATION SET\n");

}

/*
Function to handle the config.txt file and define running variables
*/
void get_running_config(FILE *ptr, shared_memory  *SM)
{
      char c[30];
      // file doesnt exist or path is wrong
      if (ptr == NULL)
      {
            printf("Error opening configuration file.");
            exit(1);
      }
      // QUEUE_POS, MAX_WAIT, EDGE_SERVER_NUMBER
      int configs[3], i;
      i = 0;
      // read through file
      while (!feof(ptr))
      {
            // adds to config array
            if (i < 3)
            {
                  fgets(c, 30, ptr);
                  // removes newline
                  c[strcspn(c, "\n")] = 0;
                  configs[i] = atoi(c);
                  i++;
            }
            else
            {
                  // define general configs
                  SM->QUEUE_POS = configs[0];
                  SM->MAX_WAIT = configs[1];
                  SM->EDGE_SERVER_NUMBER = configs[2];
                  SM->EDGE_SERVERS = calloc(SM->EDGE_SERVER_NUMBER, sizeof(struct edge_server));
                  char *server_name;
                  int vCPU_capacities[2];
                  int x;
                  // define edge servers one by one
                  for (x = 0; x < SM->EDGE_SERVER_NUMBER; x++)
                  {
                        fgets(c, 30, ptr);
                        // removes newline
                        c[strcspn(c, "\n")] = 0;
                        // process line
                        char *token = strtok(c, ",");
                        server_name = token;
                        token = strtok(NULL, ",");
                        vCPU_capacities[0] = atoi(token);
                        token = strtok(NULL, ",");
                        vCPU_capacities[1] = atoi(token);
                        // define server characteristics
                        strncpy(SM->EDGE_SERVERS[x].name, server_name, 20);
                        SM->EDGE_SERVERS[x].vCPU_1_capacity = vCPU_capacities[0];
                        SM->EDGE_SERVERS[x].vCPU_2_capacity = vCPU_capacities[1];
                        // notify that server is ready
                        char a[] = " READY\n";
                        char b[40];
                        strncpy(b, SM->EDGE_SERVERS[x].name, 40);
                        strcat(b, a);
                        output_str(b);
                  }
            }
      }
      fclose(ptr);
}

void show_server_info(struct edge_server s)
{
      printf("Name: %s\nvCPU1 Capacity:%d \nvCPU1 Capacity:%d\n", s.name, s.vCPU_1_capacity, s.vCPU_2_capacity);
}

/*
Function to synchronize terminal and log file output
*/
void output_str(char *s)
{
      time_t now;
      struct tm *time_now;
      time(&now);
      time_now = localtime(&now);
      // log file output
      log_ptr = fopen("log.txt", "a");
      fprintf(log_ptr, "%02d:%02d:%02d ", time_now->tm_hour, time_now->tm_min, time_now->tm_sec);
      fprintf(log_ptr, "%s", s);
      // terminal output
      printf("%02d:%02d:%02d ", time_now->tm_hour, time_now->tm_min, time_now->tm_sec);
      printf("%s", s);
}

/*
Function to clear and free all things needed and end simulation
*/
void end_sim()
{
      output_str("SIMULATOR CLOSING\n");
      // code to clear
      free(SM->EDGE_SERVERS);
      if (shmid >=0)
            shmctl(shmid, IPC_RMID, NULL);
      output_str("SIMULATOR CLOSED\n");
      exit(1);
}