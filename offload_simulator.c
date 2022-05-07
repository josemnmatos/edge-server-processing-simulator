
//João Maria Campos Donato 2020217878
//José Miguel Norte de Matos 2020217977


#include "simulation_structs.h"

#define PIPE_NAME "TASK_PIPE"
#define NUM_PROCESS_INI 3

void system_manager(const char *config_file);
void task_manager(shared_memory *SM);
void *task_manager_scheduler(void *p);
void *task_manager_dispatcher(void *p);
void edge_server_process(shared_memory *SM, int server_number);
void monitor();
void maintenance_manager();
void get_running_config(FILE *ptr, shared_memory *SM);
void show_server_info(edge_server s);
void sigint_handler(int signum);
void output_str(char *s);
void end_sim();
void *vCPU_task(void *p);

int shmid;
shared_memory *SM;
sem_t *semaphore;
sem_t *outputSemaphore;
pthread_mutex_t vcpu_mutex, sm_mutex;
FILE *config_ptr, *log_ptr;
int **fd;
int taskpipe;

// compile with : make all
int main(int argc, char *argv[])
{
      outputSemaphore = (sem_t *)malloc(sizeof(sem_t *));
      sem_init(outputSemaphore, 1, 1);
      // create log file
      log_ptr = fopen("log.txt", "w");
      output_str("OFFLOAD SIMULATOR STARTING\n");

      // check arguments
      if (argc != 2)
      {
            output_str("ERROR: WRONG PARAMETERS - $ offload_simulator {ficheiro de configuração}\n");
            exit(0);
      }

      // create semaphore ?? se calhar passar para dentro de sytem_manager
      semaphore = (sem_t *)malloc(sizeof(sem_t *));
      sem_init(semaphore, 1, 1);

      // system manager
      system_manager(argv[1]);

      return 0;
}

void system_manager(const char *config_file)
{
      // capture sigint
      signal(SIGINT, sigint_handler);

      //********* capture sigtstp for statistics ********

      // create shared memory
      shmid = shmget(IPC_PRIVATE, sizeof(shared_memory), IPC_CREAT | 0700);
      SM = (shared_memory *)shmat(shmid, NULL, 0);

      // open config file and get the running config

      FILE *config_ptr = fopen(config_file, "r");
      if (config_ptr == NULL)
      {

            output_str("ERROR: CAN'T OPEN FILE\n");
      }

      get_running_config(config_ptr, SM);

      //********* validate config file information ********

      output_str("CONFIGURATION SET\n");

      // create msg queue
      // assert((SM->queue_id = msgget(IPC_PRIVATE, IPC_CREAT|0700)) != -1);

      // create named pipe

      if ((mkfifo(PIPE_NAME, O_CREAT | O_EXCL | 0600) < 0) && (errno != EEXIST))
      {
            output_str("CANNOT CREATE PIPE\n");
            exit(0);
      }

      // create the rest of the processes

      // monitor process
      if ((SM->c_pid[0] = fork()) == 0)
      {

            output_str("PROCESS MONITOR CREATED\n");

            monitor();
            exit(0);
      }
      if (SM->c_pid[0] == -1)
      {

            output_str("ERROR CREATING PROCESS MONITOR\n");
            exit(1);
      }
      sleep(1);

      // task_manager process
      if ((SM->c_pid[1] = fork()) == 0)
      {

            output_str("PROCESS TASK_MANAGER CREATED\n");

            task_manager(SM);
            exit(0);
      }
      if (SM->c_pid[1] == -1)
      {
            output_str("ERROR CREATING PROCESS TASK_MANAGER\n");

            exit(2);
      }
      sleep(1);

      // maintenance manager process
      if ((SM->c_pid[2] = fork()) == 0)
      {
            output_str("PROCESS MAINTENANCE_MANAGER CREATED\n");

            maintenance_manager();
            exit(0);
      }
      if (SM->c_pid[2] == -1)
      {
            output_str("ERROR CREATING MAINTENANCE_MANAGER\n");

            exit(3);
      }
      sleep(1);
}

/*
Function to handle the config.txt file and define running variables
*/
void get_running_config(FILE *ptr, shared_memory *SM)
{
      char c[30];
      // file doesnt exist or path is wrong
      if (ptr == NULL)
      {
            output_str("Error opening configuration file.");

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

                  if (configs[0] < 1 || configs[1] < 0 || configs[2] < 2)
                  {
                        output_str("INVALID VALUES IN CONFIGURATON FILE\n");
                        exit(0);
                  }

                  SM->QUEUE_POS = configs[0];
                  SM->MAX_WAIT = configs[1];
                  SM->EDGE_SERVER_NUMBER = configs[2];
                  SM->EDGE_SERVERS = calloc(SM->EDGE_SERVER_NUMBER, sizeof(edge_server));
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

void show_server_info(edge_server s)
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
      sem_wait(outputSemaphore);
      fprintf(log_ptr, "%02d:%02d:%02d ", time_now->tm_hour, time_now->tm_min, time_now->tm_sec);
      fprintf(log_ptr, "%s", s);
      fflush(log_ptr);
      sem_post(outputSemaphore);
      // terminal output
      printf("%02d:%02d:%02d ", time_now->tm_hour, time_now->tm_min, time_now->tm_sec);
      printf("%s", s);
}

/*
Function to clear and free all elements needed and end simulation
*/
void end_sim()
{

      output_str("SIMULATOR CLOSING\n");
      // code to clear
      sem_wait(semaphore);

      int i = 0;
      while (i < (1 + NUM_PROCESS_INI))
            kill(SM->c_pid[i++], 0);
      while (wait(NULL) != -1)
            ;
      close(taskpipe);
      int f = 0;
      while (f < (1 + SM->EDGE_SERVER_NUMBER))
            kill(SM->edge_pid[f++], 0);
      while (wait(NULL) != -1)
            ;

      sem_post(semaphore);

      if (semaphore >= 0)
            sem_close(semaphore);
      output_str("SIMULATOR CLOSED\n");
      if (outputSemaphore >= 0)
            sem_close(outputSemaphore);

      if (shmid >= 0)
            shmctl(shmid, IPC_RMID, NULL);
      fclose(log_ptr);
      exit(1);
}

void monitor()
{
      output_str("MONITOR WORKING\n");
}

void task_manager(shared_memory *SM)
{
      // create a thread for each job
      sem_wait(semaphore);
      pthread_create(&SM->taskmanager[0], NULL, task_manager_scheduler, NULL);
      pthread_create(&SM->taskmanager[1], NULL, task_manager_dispatcher, NULL);

      SM->edge_pid = (pid_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pid_t));
      SM->EDGE_SERVERS = (edge_server *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(edge_server));
      // create SM->EDGE_SERVER_NUMBER number of pipes

      fd = (int **)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int *));
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            fd[i] = (int *)calloc(2, sizeof(int));
            if (fd[i] == NULL)
            {
                  output_str("ERROR ALLOCATING MEMORY FOR UNAMED PIPE\n");
            }
      }

      sem_post(semaphore);

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            pid_t current_pid = SM->edge_pid[i];
            if ((current_pid = fork()) == 0)
            {
                  // do what edge servers do
                  pipe(fd[i]);
                  edge_server_process(SM, i);
                  free(fd[i]);
                  exit(0);
            }
            else if (current_pid == -1)
            {
                  output_str("ERROR CREATING EDGE SERVER\n");
            }
      }
      // read taskpipe

      if ((taskpipe = open(PIPE_NAME, O_RDONLY)) < 0)
      {
            output_str("ERROR OPENING NAMED PIPE\n");
            exit(0);
      }
      task tsk;
      while (1)
      {
            read(taskpipe, &tsk, sizeof(tsk));
            printf("%d\n", tsk.thousInstructPerRequest);
      }

      // wait for the threads to finish
      pthread_join(SM->taskmanager[0], NULL);
      pthread_join(SM->taskmanager[1], NULL);

      // Não sei se esta bem
      free(fd);
}

void *task_manager_scheduler(void *p)
{
      output_str("TASK_MANAGER_SCHEDULER WORKING\n");
      pthread_exit(NULL);
}

void *task_manager_dispatcher(void *p)
{
      output_str("TASK_MANAGER_DISPATCHER WORKING\n");
      pthread_exit(NULL);
}

void edge_server_process(shared_memory *SM, int server_number)
{
      // creates threads for each cpu
      sem_wait(semaphore);

      pthread_create(&SM->EDGE_SERVERS[server_number].vCPU[0], NULL, &vCPU_task, NULL);
      pthread_create(&SM->EDGE_SERVERS[server_number].vCPU[1], NULL, &vCPU_task, NULL);

      sem_post(semaphore);

      pthread_join(SM->EDGE_SERVERS[server_number].vCPU[0], NULL);
      pthread_join(SM->EDGE_SERVERS[server_number].vCPU[1], NULL);
}

void *vCPU_task(void *p)
{
      pthread_mutex_lock(&vcpu_mutex);
      char msg[60];
      sprintf(msg, "VPCU TASK COMPLETE BY THREAD %ld\n", pthread_self());

      output_str(msg);

      pthread_mutex_unlock(&vcpu_mutex);
      pthread_exit(NULL);
      return NULL;
}

void maintenance_manager()
{
      output_str("MAINTENANCE MANAGER WORKING\n");
}

void sigint_handler(int signum)
{
      output_str("^C PRESSED. CLOSING PROGRAM.\n");
      end_sim();
}
