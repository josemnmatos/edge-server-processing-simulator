#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include "edge_server.h"

#define PIPE_NAME "TASK_PIPE"
int EDGE_SERVER_NUMBER;
int QUEUE_POS;
int MAX_WAIT;

void *system_manager(void *p);
void *task_manager(void *p);
void *edge_server_process(void *p);
void *monitor(void *p);
void *maintenance_manager(void *p);
void get_running_config(FILE *ptr);

int main(int argc, char const *argv[])
{
      /* code */
      pthread_t thread1;
      pthread_create(&thread1, NULL, system_manager, NULL);
      pthread_join(thread1, NULL);
      return 0;
}

void *system_manager(void *p)
{
      FILE *ptr;
      // open config file
      ptr = fopen("config.txt", "r");
      get_running_config(ptr);
      pthread_exit(NULL);
}

void get_running_config(FILE *ptr)
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
                  QUEUE_POS = configs[0];
                  MAX_WAIT = configs[1];
                  EDGE_SERVER_NUMBER = configs[2];
                  printf("%d\n", QUEUE_POS);
                  printf("%d\n", MAX_WAIT);
                  printf("%d\n", EDGE_SERVER_NUMBER);
                  struct edge_server EDGE_SERVERS[EDGE_SERVER_NUMBER];
                  char *server_name;
                  int vCPU_capacities[2];
                  int l, x;
                  // define edge servers one by one
                  for (x = 0; x < EDGE_SERVER_NUMBER; x++)
                  {
                        l = 0;
                        fgets(c, 30, ptr);
                        // removes newline
                        c[strcspn(c, "\n")] = 0;
                        // process line
                        char *token = strtok(c, ",");
                        server_name = token;
                        while (token != NULL)
                        {
                              token = strtok(NULL, ",");
                              vCPU_capacities[l] = atoi(token);
                              l++;
                        }
                        // define server characteristics
                        struct edge_server server;
                        strncpy(server.name, server_name, 20);
                        server.vCPU_1_capacity = vCPU_capacities[0];
                        server.vCPU_2_capacity = vCPU_capacities[1];
                        // add server to server array
                        EDGE_SERVERS[x] = server;
                        printf("1");
                  }
            }
      }
      fclose(ptr);
}
