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
FILE *log_ptr;
FILE *config_ptr;
time_t now;
struct tm *time_now;

void *system_manager(void *p);
void *task_manager(void *p);
void *edge_server_process(void *p);
void *monitor(void *p);
void *maintenance_manager(void *p);
void get_running_config(FILE *ptr);
void show_server_info(struct edge_server s);
void output_str(char *s);

// compile with : gcc -Wall -pthread main.c edge_server.h -o test

int main(int argc, char const *argv[])
{
      /* code */
      // output_str("OFFLOAD SIMULATOR STARTING\n");
      pthread_t thread1;
      pthread_create(&thread1, NULL, system_manager, NULL);
      pthread_join(thread1, NULL);
      return 0;
}

void *system_manager(void *p)
{
      // create log file
      log_ptr = fopen("log.txt", "w");
      output_str("OFFLOAD SIMULATOR STARTING\n");
      fclose(log_ptr);
      // open config file
      config_ptr = fopen("config.txt", "r");
      get_running_config(config_ptr);
      // output_str("CONFIGURATION SET\n");
      fclose(config_ptr);
      pthread_exit(NULL);
}

/*
Function to handle the config.txt file and define running variables
*/
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
                  int x;
                  printf("1\n");
                  // define edge servers one by one
                  for (x = 0; x < EDGE_SERVER_NUMBER; x++)
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
                        strncpy(EDGE_SERVERS[x].name, server_name, 20);
                        EDGE_SERVERS[x].vCPU_1_capacity = vCPU_capacities[0];
                        EDGE_SERVERS[x].vCPU_2_capacity = vCPU_capacities[1];
                        //-----------------------
                        show_server_info(EDGE_SERVERS[x]);
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
      time(&now);
      time_now = localtime(&now);
      // log file output
      log_ptr = fopen("log.txt", "a");
      fprintf(log_ptr, "%02d:%02d:%02d ", time_now->tm_hour, time_now->tm_min, time_now->tm_sec);
      fprintf(log_ptr, "%s", s);
      fclose(log_ptr);
      // terminal output
      printf("%02d:%02d:%02d ", time_now->tm_hour, time_now->tm_min, time_now->tm_sec);
      printf("%s", s);
}