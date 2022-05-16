// João Maria Campos Donato 2020217878
// José Miguel Norte de Matos 2020217977

#include "simulation_structs.h"

#define PIPE_NAME "TASK_PIPE"
#define PIPE_BUF 64
#define NUM_PROCESS_INI 3
#define VCPU_STATE_SEM "vcpu_sem"

void system_manager(const char *config_file);
void task_manager();
void *task_manager_scheduler(void *p);
void *task_manager_dispatcher(void *p);
void edge_server_process(int server_number);
void monitor();
void maintenance_manager();
void get_running_config(FILE *ptr);
void sigint_handler(int signum);
void sigtstp_handler(int signum);
void output_str(char *s);
void end_sim();
void *vCPU_task(void *p);
void maint_manager_handler(int signum);
void task_manager_handler(int signum);
void edge_server_handler(int signum);
void monitor_handler(int signum);
void print_stats();
void *close_handler(void *p);
void update_queue(request *queue, request new_element);
void remove_from_queue(request *queue, int index);
void *maintenance_thread_func(void *p);
void *messageQueueReader(void *p);
void *lowest_processing_vcpu(void *p);
void *highest_processing_vcpu(void *p);

int shmid;
shared_memory *SM;
sem_t *semaphore;
sem_t *outputSemaphore;
sem_t *TMSemaphore;
pthread_mutex_t sm_mutex;

FILE *config_ptr, *log_ptr;

int taskpipe;
int message_queue_id;

// pthread_cond_t *schedulerCond;
pthread_mutex_t taskQueueMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t **vcpuCond;
pthread_mutex_t **vcpuMutex;

request *requestList;
request *reqListFinal;

int *maintWork;

// unnamedpipes
int **fd;

int scheduler = 0;

time_t time1;
time_t time2;
int random1;
int random2;

int *vcpu_time;

//--edge_server--
pthread_t *server_thread_for_maintenance;
pthread_cond_t *message_queue_cond;
pthread_mutex_t *message_queue_mutex;
//--------------------
// ---maintenance---
pthread_mutex_t maint_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t max_maint_server = PTHREAD_COND_INITIALIZER;
pthread_mutex_t max_maint_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t *maintenance_thread;
pthread_cond_t *maint_cond;
pthread_mutex_t *maint_cond_mutex;
int maintenance_counter = 0;
int maintenance_now = 0;

//--------------------
// compile with : make all
int main(int argc, char *argv[])
{
      signal(SIGINT, SIG_IGN);
      signal(SIGTSTP, SIG_IGN);

      outputSemaphore = (sem_t *)malloc(sizeof(sem_t *));
      sem_init(outputSemaphore, 1, 1);
      // create log file
      log_ptr = fopen("log.txt", "w");
      output_str("SYSTEM: OFFLOAD SIMULATOR STARTING\n");

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

      printf("CLOSING12345\n");

      // close the rest of req

      free(requestList);

      free(SM->simulation_stats.executed_pserver);
      free(SM->simulation_stats.maintenance_pserver);

      if (semaphore >= 0)
            sem_close(semaphore);

      pthread_mutex_destroy(&taskQueueMutex);

      free(SM->min_waiting);

      free(SM->EDGE_SERVERS);

      free(message_queue_cond);
      free(message_queue_mutex);

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            pthread_cond_destroy(&vcpuCond[i][0]);
            pthread_cond_destroy(&vcpuCond[i][1]);
            pthread_mutex_destroy(&vcpuMutex[i][0]);
            pthread_mutex_destroy(&vcpuMutex[i][1]);
            pthread_cond_destroy(&SM->edgeServerCond[i]);
            pthread_mutex_destroy(&SM->edgeServerMutex[i]);
      }

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            free(vcpuCond[i]);
            free(vcpuMutex[i]);
            free(SM->times_edgeserver[i]);
      }

      free(SM->times_edgeserver);

      free(vcpuCond);

      free(vcpuMutex);

      free(SM->edgeServerCond);

      free(SM->edgeServerMutex);

      unlink(PIPE_NAME);

      shmdt(SM);
      if (shmid >= 0)
            shmctl(shmid, IPC_RMID, NULL);

      output_str("SYSTEM: OFFLOAD SIMULATOR ENDING\n");

      if (outputSemaphore >= 0)
            sem_close(outputSemaphore);


      fclose(log_ptr);

      return 0;
}

//###############################################
// SYSTEM MANAGER
//###############################################

/*
Main function for system manager process
*/
void system_manager(const char *config_file)
{
      // ignore sigint and sigtstp
      signal(SIGINT, SIG_IGN);
      signal(SIGTSTP, SIG_IGN);

      //********* capture sigtstp for statistics ********

      // create shared memory
      shmid = shmget(IPC_PRIVATE, sizeof(shared_memory), IPC_CREAT | 0777);
      SM = (shared_memory *)shmat(shmid, NULL, 0);

      SM->sm_pid = getpid();

      // open config file and get the running config

      FILE *config_ptr = fopen(config_file, "r");
      if (config_ptr == NULL)
      {

            output_str("ERROR: CAN'T OPEN FILE\n");
      }

      get_running_config(config_ptr);

      //********* validate config file information ********

      output_str("SYS.MANAGER: CONFIGURATION SET\n");

      pthread_mutexattr_t attrmutex;
      pthread_condattr_t attrcondv;

      if (pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED) != 0)
            printf("erro mutex attr\n");

      if (pthread_condattr_setpshared(&attrcondv, PTHREAD_PROCESS_SHARED) != 0)
            printf("erro cond attr\n");

      SM->edgeServerCond = (pthread_cond_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pthread_cond_t));
      SM->edgeServerMutex = (pthread_mutex_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pthread_mutex_t));

      vcpuCond = (pthread_cond_t **)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pthread_cond_t *));
      vcpuMutex = (pthread_mutex_t **)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pthread_mutex_t *));
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            vcpuCond[i] = (pthread_cond_t *)calloc(2, sizeof(pthread_cond_t));
            vcpuMutex[i] = (pthread_mutex_t *)calloc(2, sizeof(pthread_mutex_t));
      }

      message_queue_cond = (pthread_cond_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pthread_cond_t));
      message_queue_mutex = (pthread_mutex_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pthread_mutex_t));

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            pthread_cond_init(&message_queue_cond[i], NULL);
            pthread_mutex_init(&message_queue_mutex[i], NULL);
      }

      pthread_cond_init(&SM->monitorCond, &attrcondv);
      pthread_mutex_init(&SM->monitorMutex, &attrmutex);

      pthread_cond_init(&SM->schedulerCond, &attrcondv);
      pthread_mutex_init(&SM->schedulerMutex, &attrmutex);

      pthread_cond_init(&SM->dispatcherCond, &attrcondv);
      pthread_mutex_init(&SM->dispatcherMutex, &attrmutex);

      pthread_cond_init(&SM->continueDispatchCond, &attrcondv);
      pthread_mutex_init(&SM->continueDispatchMutex, &attrmutex);

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            pthread_cond_init(&SM->edgeServerCond[i], &attrcondv);
            pthread_mutex_init(&SM->edgeServerMutex[i], &attrmutex);
      }

      SM->num_queue = 0;
      SM->shutdown = 0;
      SM->min_waiting = (int *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int));
      SM->simulation_stats.executed_pserver = (int *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int));
      SM->simulation_stats.maintenance_pserver = (int *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int));
      SM->times_edgeserver = (int **)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int *));
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            SM->times_edgeserver[i] = (int *)calloc(2, sizeof(int));
      }

      // create msg queue
      assert((message_queue_id = msgget(IPC_PRIVATE, IPC_CREAT | 0700)) != -1);

      // dispatch semaphore

      sem_t *vcpu_sem = sem_open(VCPU_STATE_SEM, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1);

      // create named pipe

      if ((mkfifo(PIPE_NAME, O_CREAT | O_EXCL | 0600) < 0) && (errno != EEXIST))
      {
            output_str("SYS.MANAGER: CANNOT CREATE PIPE\n");
            exit(0);
      }

      // create the rest of the processes

      int p;
      // monitor process
      if ((p = fork()) == 0)
      {

            output_str("SYS.MANAGER: PROCESS MONITOR CREATED\n");
            // not using
            monitor(SM);
            exit(0);
      }
      if (p == -1)
      {

            output_str("SYS.MANAGER: ERROR CREATING PROCESS MONITOR\n");
            exit(1);
      }

      // task_manager process
      if ((p = fork()) == 0)
      {

            output_str("SYS.MANAGER: PROCESS TASK_MANAGER CREATED\n");

            task_manager(SM);
            exit(0);
      }
      if (p == -1)
      {
            output_str("SYS.MANAGER: ERROR CREATING PROCESS TASK_MANAGER\n");

            exit(2);
      }

      // maintenance manager process
      if ((p = fork()) == 0)
      {
            output_str("SYS.MANAGER: PROCESS MAINTENANCE_MANAGER CREATED\n");
            // not using
            maintenance_manager(SM->EDGE_SERVER_NUMBER);
            exit(0);
      }
      if (p == -1)
      {
            output_str("SYS.MANAGER: ERROR CREATING MAINTENANCE_MANAGER\n");

            exit(3);
      }

      // handle control c and ctrl z
      signal(SIGINT, sigint_handler);
      signal(SIGTSTP, sigtstp_handler);
      // wait for all system manager child processes to end
      waitpid(SM->c_pid[0], 0, 0);
      waitpid(SM->c_pid[2], 0, 0);
      waitpid(SM->c_pid[1], 0, 0);

      sem_close(vcpu_sem);

      output_str("AQUI SDADA\n");

      pthread_mutexattr_destroy(&attrmutex);
      pthread_condattr_destroy(&attrcondv);
}

//###############################################
// MONITOR
//###############################################

// monitor says the performance level of the edge servers
// monitor knows which vcpus are available
// activates and deactivates vcpus
// only gonna run when something changes in the queue

/*
Main function for monitor process
*/
void monitor(shared_memory *SM)
{
      SM->c_pid[0] = getpid();
      signal(SIGUSR1, monitor_handler);
      SM->performance_flag = 0;

      float queue_rate;
      output_str("MONITOR: WORKING\n");
      while (1)
      {
            pthread_mutex_lock(&SM->monitorMutex);

            // wait for broadcast from task manager
            while (SM->monitorWork == 0)
            {
                  pthread_cond_wait(&SM->monitorCond, &SM->monitorMutex);
                  SM->monitorWork = 1;
            }
            // check if system is shutting down
            if (SM->shutdown == 1)
            {
                  pthread_mutex_unlock(&SM->monitorMutex);
                  break;
            }
            // do monitor things
            queue_rate = (float)SM->num_queue / (float)SM->QUEUE_POS;
            printf("qr: %f\n", queue_rate);
            // get minimum waiting time on any vcpu
            int minimum_waiting_time = SM->times_edgeserver[0][0];
            // function to see the minimum in min_waiting;
            for (int i = 1; i < SM->EDGE_SERVER_NUMBER; i++)
            {
                  if (SM->times_edgeserver[i][0] < minimum_waiting_time)
                  {
                        minimum_waiting_time = SM->times_edgeserver[i][0];
                  }

                  if (SM->times_edgeserver[i][1] < minimum_waiting_time)
                  {
                        minimum_waiting_time = SM->times_edgeserver[i][1];
                  }
            }

            printf("minimum waiting: %d\n", minimum_waiting_time);

            if (((queue_rate > 0.8) && SM->performance_flag == 0) || (minimum_waiting_time > SM->MAX_WAIT))
            {
                  output_str("MONITOR: EDGE SERVERS IN HIGH PERFORMANCE\n");
                  sem_wait(semaphore);
                  SM->performance_flag = 1;
                  pthread_cond_signal(&SM->dispatcherCond);
                  sem_post(semaphore);
            }
            if (queue_rate < 0.2 && SM->performance_flag == 1)
            {
                  output_str("MONITOR: EDGE SERVERS IN NORMAL PERFORMANCE\n");
                  sem_wait(semaphore);
                  SM->performance_flag = 0;
                  pthread_cond_signal(&SM->dispatcherCond);
                  sem_post(semaphore);
            }
            SM->monitorWork = 0;
            pthread_mutex_unlock(&SM->monitorMutex);
      }
      pthread_cond_destroy(&SM->monitorCond);
      pthread_mutex_destroy(&SM->monitorMutex);
      output_str("MONITOR: CLOSED\n");
      exit(0);
}

//###############################################
// TASK MANAGER
//###############################################

/*
Main function for maintenance manager process
*/
void task_manager()
{
      SM->c_pid[1] = getpid();
      // handler to shutdown task manager process
      signal(SIGUSR1, SIG_IGN);

      // init vcpus
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            SM->EDGE_SERVERS[i].vcpu[0].free = 1;
            SM->EDGE_SERVERS[i].vcpu[1].free = 1;
      }

      output_str("TASK_MANAGER: WORKING\n");
      // create a thread for each job
      sem_wait(semaphore);
      pthread_create(&SM->taskmanager[0], NULL, task_manager_scheduler, NULL);
      pthread_create(&SM->taskmanager[1], NULL, task_manager_dispatcher, NULL);

      // maintenance threads for each of the edge servers
      server_thread_for_maintenance = (pthread_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pthread_t));

      // alocate memory for sleep time vcpu
      vcpu_time = (int *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int));

      SM->edge_pid = (pid_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pid_t));

      SM->taskToProcess = (int **)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int *));

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            SM->taskToProcess[i] = (int *)calloc(2, sizeof(int));
      }

      // create SM->EDGE_SERVER_NUMBER number of pipes
      fd = (int **)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int *));
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            fd[i] = (int *)calloc(2, sizeof(int));
            if (fd[i] == NULL)
            {
                  output_str("TASK_MANAGER: ERROR ALLOCATING MEMORY FOR UNNAMED PIPE\n");
            }
      }

      // alocate memory for requestList
      requestList = (request *)calloc(SM->QUEUE_POS, sizeof(request));

      sem_post(semaphore);

      message creation;
      strcpy(creation.msg_text, "EDGE SERVER CREATED");
      creation.msg_type = 1;

      // create SM->EDGE_SERVER_NUMBER edge servers
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            // inform maint manager that edge server will be created
            msgsnd(message_queue_id, &creation, sizeof(message), 0);
            pipe(fd[i]);
            if (fork() == 0)
            {
                  // do what edge servers do
                  close(fd[i][1]);
                  edge_server_process(i);
                  free(fd[i]);
                  exit(0);
            }
            else if (SM->edge_pid[i] == -1)
            {
                  output_str("ERROR CREATING EDGE SERVER\n");
            }
      }

      // read taskpipe and send it to the queue
      if ((taskpipe = open(PIPE_NAME, O_RDWR)) < 0)
      {

            output_str("TASK_MANAGER: ERROR OPENING NAMED PIPE\n");
            exit(0);
      }

      signal(SIGUSR1, task_manager_handler);

      // inform maint manager ALL edge servers were created
      strcpy(creation.msg_text, "END");
      creation.msg_type = 2;
      msgsnd(message_queue_id, &creation, sizeof(message), 0);

      pthread_join(SM->taskmanager[1], NULL);
      output_str("TASK_MANAGER: DISPATCHER CLOSED\n");

      // wait for all edge servers to exit
      for (int j = 0; j < SM->EDGE_SERVER_NUMBER; j++)
      {
            wait(NULL);
      }
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            free(SM->times_edgeserver[i]);
      }

      // destroy mutex
      // pthread_cond_destroy(SM->continueDispatchCond);
      // pthread_mutex_destroy(SM->continueDispatchMutex);
      free(SM->times_edgeserver);
      free(vcpu_time);

      output_str("TASK_MANAGER: CLOSING\n");

      // pthread_cond_destroy(&schedulerCond);

      exit(0);
}

/*
Thread function for task manager scheduler
*/
void *task_manager_scheduler(void *p)
{

      output_str("TASK_MANAGER: TASK_MANAGER_SCHEDULER WORKING\n");

      char task_read_string[PIPE_BUF];

      request req;
      req.tsk.id = 0;
      int nread;

      while (1)
      {
            strcpy(task_read_string, "");
            nread = read(taskpipe, task_read_string, sizeof(task_read_string));

            if (nread <= 0 || errno == EINTR)
            {

                  printf("%s\n", strerror(errno));
            }

            if (SM->shutdown == 1)
            {

                  break;
            }

            // read until pipe closes
            // only goes down here if it reads something -- pipe is open on blocking mode

            task_read_string[nread] = '\0';
            task_read_string[strcspn(task_read_string, "\n")] = 0;

            // handle string read
            if (strcmp(task_read_string, "EXIT") == 0)
            {
                  // send SIGINT to system manager
                  kill(SM->sm_pid, SIGINT);
                  fflush(NULL);
                  continue;
            }
            else if (strcmp(task_read_string, "STATS") == 0)
            {
                  // send SIGTSTP to system manager
                  kill(SM->sm_pid, SIGTSTP);
                  fflush(NULL);
                  continue;
            }

            else // handle the received task string
            {

                  char *token = strtok(task_read_string, ":");
                  req.tsk.thousInstructPerRequest = atoi(token);
                  token = strtok(NULL, ":");
                  req.tsk.maxExecTimeSecs = atoi(token);
                  req.timeOfEntry = time(NULL);

                  pthread_mutex_lock(&taskQueueMutex);

                  if (SM->num_queue > SM->QUEUE_POS)
                  {
                        output_str("TASK_MANAGER: FULL QUEUE-TASK HAS BEEN DELETED\n");
                  }
                  else
                  {

                        // add request at right position of queue and see if the time has already passed
                        update_queue(requestList, req);

                        // TODO ^^
                        req.tsk.id++;
                        SM->simulation_stats.requested_tasks++;
                  }
                  SM->monitorWork = 1;
                  pthread_cond_broadcast(&SM->monitorCond);

                  pthread_mutex_unlock(&taskQueueMutex);

                  // signal dispatcher
                  if (SM->num_queue == 1)
                  {
                        pthread_cond_signal(&SM->dispatcherCond);
                  }
            }
      }
      output_str("TASK_MANAGER: SCHEDULER LEAVING\n");
      pthread_exit(NULL);
}

/*
Auxiliary function for task manager scheduler
*/
void remove_from_queue(request queue[], int index)
{
      int i;
      for (i = index; i < SM->num_queue - 1; i++)
      {
            queue[i] = queue[i + 1];
      }
}

/*
Auxiliary function for task manager scheduler
*/
void update_queue(request queue[], request new_element)
{
      // insert at 0
      if (SM->num_queue == 0)
      {
            queue[SM->num_queue] = new_element;
            SM->num_queue++;
            return;
      }

      // insert at end
      SM->num_queue++;
      queue[SM->num_queue] = new_element;

      for (int i = 0; i < SM->num_queue; i++)
      {
            // check if time passed
            if (queue[i].tsk.maxExecTimeSecs < time(NULL) - queue[i].timeOfEntry)
            {
                  remove_from_queue(queue, i);
            }
      }

      float i_key, j_key;
      int i, j;
      for (i = 1; i < SM->num_queue; i++)
      {
            i_key = time(NULL) - queue[i].timeOfEntry - queue[i].tsk.maxExecTimeSecs;

            j = i - 1;

            j_key = time(NULL) - queue[j].timeOfEntry - queue[j].tsk.maxExecTimeSecs;

            while (j >= 0 && j_key > i_key)
            {
                  queue[j + 1] = queue[j];
                  j = j - 1;
            }
            queue[j + 1] = queue[i];
      }
}

/*
Thread function for task manager dispatcher
*/
void *task_manager_dispatcher(void *p)
{
      output_str("TASK_MANAGER: DISPATCHER WORKING\n");
      int vcpu1_instruction_capacity;
      int task_instructions;
      int processing_time;
      request most_priority;
      int available;

      sem_t *vcpu_sem = sem_open(VCPU_STATE_SEM, 0);

      while (1)
      {
            // this thread is only activated if a vcpu is free
            pthread_mutex_lock(&SM->dispatcherMutex);
            while (SM->dispatcherWork == 0)
            { // condition to check if any is free
                  pthread_cond_wait(&SM->dispatcherCond, &SM->dispatcherMutex);

                  // check if system is shutting down
                  if (SM->shutdown == 1)
                  {
                        break;
                  }

                  SM->dispatcherWork = 1;

                  if (SM->num_queue <= 0)
                  {
                        SM->dispatcherWork = 0;
                  }
            }
            // check if system is shutting down
            if (SM->shutdown == 1)
            {
                  pthread_mutex_unlock(&SM->dispatcherMutex);
                  break;
            }
            output_str("TASK_MANAGER: DISPATCHER EXECUTING\n");

            // do dispatcher things
            most_priority = requestList[0];

            int i;

            SM->continue_dispatch = 0;

            for (i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
            {

                  // checks the task with most priority can be executed by a vcpu in time inferior to MaxEXECTIME
                  // if edge server is on maintenance skip
                  if (SM->EDGE_SERVERS[i].stopped == 1)
                  {
                        continue;
                  }

                  // check vcpu 1, if yes dispatch
                  sem_wait(vcpu_sem);
                  available = SM->EDGE_SERVERS[i].vcpu[0].free;
                  sem_post(vcpu_sem);
                  if (available == 1)
                  {

                        vcpu1_instruction_capacity = SM->EDGE_SERVERS[i].vcpu[0].capacity * (1000000);

                        task_instructions = most_priority.tsk.thousInstructPerRequest * (1000);

                        processing_time = (int)(task_instructions / vcpu1_instruction_capacity);
                        char processing_time_string[PIPE_BUF];
                        // string with processing time and vcpu number to execute
                        sprintf(processing_time_string, "%d:0", processing_time);

                        // dispatch task to edge server

                        if (processing_time <= ((time(NULL) - most_priority.timeOfEntry) + most_priority.tsk.maxExecTimeSecs))
                        {
                              // write to pipe for execution on vcpu 1

                              write(fd[i][1], &processing_time_string, PIPE_BUF);

                              sem_wait(vcpu_sem);
                              SM->EDGE_SERVERS[i].vcpu[0].free = 0;
                              sem_post(vcpu_sem);

                              // pthread_cond_broadcast(&SM->edgeServerCond[i]);
                              char print[60];
                              sprintf(print, "TASK_MANAGER: TASK DISPATCHED TO SERVER %d\n", i);
                              output_str(print);
                              pthread_mutex_unlock(&SM->dispatcherMutex);

                              break;
                        }
                  }
                  

                  sem_wait(vcpu_sem);
                  available = SM->EDGE_SERVERS[i].vcpu[1].free;
                  sem_post(vcpu_sem);
                  // check vcpu 2, if yes and cpu 2 is on, dispatch
                  if (SM->performance_flag == 1)
                  {
                        if (available == 1)
                        {
                              int vcpu2_instruction_capacity = SM->EDGE_SERVERS[i].vcpu[0].capacity * (1000000);
                              int processing_time = (int)(task_instructions / vcpu2_instruction_capacity);
                              char processing_time_string[PIPE_BUF];
                              // string with processing time and vcpu number to execute
                              sprintf(processing_time_string, "%d:1", processing_time);

                              if (processing_time <= ((time(NULL) - most_priority.timeOfEntry) + most_priority.tsk.maxExecTimeSecs))
                              {
                                    // write to pipe for execution on vcpu 2

                                    write(fd[i][1], &processing_time_string, PIPE_BUF);

                                    sem_wait(vcpu_sem);
                                    SM->EDGE_SERVERS[i].vcpu[1].free = 0;
                                    sem_post(vcpu_sem);

                                    char print[60];
                                    sprintf(print, "TASK_MANAGER: TASK DISPATCHED TO SERVER %d\n", i);
                                    output_str(print);
                                    pthread_mutex_unlock(&SM->dispatcherMutex);
                                    break;
                              }
                        }
                  }
                  

            }
            // if task not dispatched, delete
            if (i == SM->EDGE_SERVER_NUMBER)
            {
                  output_str("TASK_MANAGER: ALL EDGE SERVERS ARE FULL\n");
            }
            int j;
            // delete task on position 0 «

            for (j = 0; j < SM->num_queue; j++)
            {
                  if (most_priority.tsk.id == requestList[j].tsk.id)
                  {
                        remove_from_queue(requestList, j);
                        SM->num_queue--;
                        break;
                  }
            }

            SM->dispatcherWork = 0;
            pthread_mutex_unlock(&SM->dispatcherMutex);
      }

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            close(fd[i][1]);
      }

      sem_close(vcpu_sem);
      pthread_exit(NULL);
}

//###############################################
// EDGE SERVER
//###############################################

/*
Main function for edge server process
*/
void edge_server_process(int server_number)
{
      sem_t *vcpu_sem;
      vcpu_sem = sem_open(VCPU_STATE_SEM, 0);

      signal(SIGUSR1, edge_server_handler);

      vcpu_info *arg1 = malloc(sizeof(vcpu_info));
      vcpu_info *arg2 = malloc(sizeof(vcpu_info));

      arg1->server_number = server_number;
      arg2->server_number = server_number;
      arg1->vcpu_number = 0;
      arg2->vcpu_number = 1;
      arg1->free_sem = vcpu_sem;
      arg2->free_sem = vcpu_sem;

      // creates threads for each cpu
      sem_wait(semaphore);
      SM->edge_pid[server_number] = getpid();
      // check which vcpu has the lowest processing capacity

      // initiate cond and mutex for each vcpu of current edge server
      for (int i = 0; i < 2; i++)
      {
            pthread_mutex_init(&vcpuMutex[server_number][i], NULL);
            pthread_cond_init(&vcpuCond[server_number][i], NULL);
      }

      pthread_create(&SM->EDGE_SERVERS[server_number].vcpu[0].thread, NULL, &vCPU_task, arg1);
      pthread_create(&SM->EDGE_SERVERS[server_number].vcpu[1].thread, NULL, &vCPU_task, arg2);
      SM->EDGE_SERVERS[server_number].vcpu[0].free = 1;
      SM->EDGE_SERVERS[server_number].vcpu[1].free = 1;

      pthread_cond_broadcast(&SM->dispatcherCond);

      int *arg3 = malloc(sizeof(int));
      *arg3 = server_number;
      // create message queue reader thread
      pthread_create(&server_thread_for_maintenance[server_number], NULL, messageQueueReader, arg3);

      sem_post(semaphore);

      char to_execute[PIPE_BUF];
      int time_to_run;
      int vcpu_to_run;
      close(fd[server_number][1]);

      while (1) // nao é espera ativa pq esta aberto p escrita do outro lado mas onde se meter a leitura do message queue
      {
            pthread_mutex_lock(&SM->edgeServerMutex[server_number]);
            to_execute[0] = 0;

            int nread = read(fd[server_number][0], to_execute, PIPE_BUF);
            SM->continue_dispatch = 1;
            pthread_cond_broadcast(&SM->continueDispatchCond);

            // signal dispatcher that edge server has received task

            if (SM->shutdown == 1)
            {
                  break;
            }

            if (nread < 0)
            {
                  output_str("EDGE SERVER: NOT READING ANYMORE\n");
                  break;
            }
            if (nread > 0)
            {

                  to_execute[nread] = '\0';
                  to_execute[strcspn(to_execute, "\n")] = 0;
                  char *token = strtok(to_execute, ":");
                  time_to_run = atoi(token);
                  token = strtok(NULL, ":");
                  vcpu_to_run = atoi(token);

                  printf("edge server %d read task with processing time %d on vcpu %d.\n", server_number, time_to_run, vcpu_to_run);

                  // check which vcpu is going to be ran and assign the instruction to it
                  sem_wait(vcpu_sem);
                  SM->times_edgeserver[server_number][vcpu_to_run] = time_to_run;
                  SM->EDGE_SERVERS[server_number].vcpu[vcpu_to_run].free = 0;
                  sem_post(vcpu_sem);

                  pthread_cond_broadcast(&vcpuCond[server_number][vcpu_to_run]);
            }
            pthread_mutex_unlock(&SM->edgeServerMutex[server_number]);
      }
      close(fd[server_number][0]);

      // clean
      pthread_join(SM->EDGE_SERVERS[server_number].vcpu[0].thread, NULL);
      pthread_join(SM->EDGE_SERVERS[server_number].vcpu[1].thread, NULL);
      // cancel maintenance thread

      pthread_cancel(server_thread_for_maintenance[server_number]);

      pthread_cond_destroy(&message_queue_cond[server_number]);
      pthread_mutex_destroy(&message_queue_mutex[server_number]);

      sem_close(vcpu_sem);

      output_str("EDGE SERVER: LEFT\n");
      exit(0);
}

/*
Thread function for each vcpu
*/
void *vCPU_task(void *p)
{

      vcpu_info info = *((vcpu_info *)p);
      // TODO: know what vcpu it is recieves as argument in create
      // do for the task that is still going, to finish
      int time_to_sleep;

      while (1)
      {
            // wait until vcpu has been assigned a task or is shutting down
            pthread_cond_signal(&SM->dispatcherCond);
            pthread_mutex_lock(&vcpuMutex[info.server_number][info.vcpu_number]);
            while (SM->times_edgeserver[info.server_number][info.vcpu_number] == 0)
            {
                  SM->EDGE_SERVERS[info.server_number].stopped_vcpus++;
                  pthread_cond_broadcast(&message_queue_cond[info.server_number]);
                  pthread_cond_wait(&vcpuCond[info.server_number][info.vcpu_number], &vcpuMutex[info.server_number][info.vcpu_number]);
                  if (SM->shutdown == 1)
                  {
                        pthread_mutex_unlock(&vcpuMutex[info.server_number][info.vcpu_number]);
                        break;
                  }
                  //
                  SM->EDGE_SERVERS[info.server_number].stopped_vcpus--;
            }
            sem_wait(info.free_sem);
            SM->EDGE_SERVERS[info.server_number].vcpu[info.vcpu_number].free = 0;
            sem_post(info.free_sem);
            // check if is shutting down
            if (SM->shutdown == 1)
            {
                  pthread_mutex_unlock(&vcpuMutex[info.server_number][info.vcpu_number]);
                  break;
            }

            // perform task

            time_to_sleep = SM->times_edgeserver[info.server_number][info.vcpu_number];
            printf("time to sleep %d\n", time_to_sleep);
            sleep(time_to_sleep);
            char print[80];
            sprintf(print, "SERVER %d - VCPU %d : FINISHED TASK IN %d SECONDS\n", info.server_number, info.vcpu_number + 1, time_to_sleep);
            output_str(print);
            SM->simulation_stats.executed_pserver[info.server_number]++;
            // cond signal to check if its in maintenance
            while (SM->EDGE_SERVERS[info.server_number].stopped == 1)
            {
                  SM->EDGE_SERVERS[info.server_number].stopped_vcpus++;
                  pthread_cond_broadcast(&message_queue_cond[info.server_number]);

                  pthread_cond_wait(&vcpuCond[info.server_number][info.vcpu_number], &vcpuMutex[info.server_number][info.vcpu_number]);
                  if (SM->shutdown == 1)
                  {
                        pthread_mutex_unlock(&vcpuMutex[info.server_number][info.vcpu_number]);
                        break;
                  }

                  SM->EDGE_SERVERS[info.server_number].stopped_vcpus--;
            }

            // check again if shutting down in case simulation begins shutting down while vcpu performing task
            if (SM->shutdown == 1)
            {
                  pthread_mutex_unlock(&vcpuMutex[info.server_number][info.vcpu_number]);
                  break;
            }

            sprintf(print, "SERVER %d - VCPU %d : READY FOR NEXT TASK\n", info.server_number + 1, info.vcpu_number + 1);
            output_str(print);

            // reset the time so it waits for next vcpu task assignment
            pthread_mutex_unlock(&vcpuMutex[info.server_number][info.vcpu_number]);

            sem_wait(info.free_sem);
            SM->taskToProcess[info.server_number][info.vcpu_number] = 0;
            SM->times_edgeserver[info.server_number][info.vcpu_number] = 0;
            SM->EDGE_SERVERS[info.server_number].vcpu[info.vcpu_number].free = 1;
            sem_post(info.free_sem);
      }
      output_str("EDGE SERVER: VCPU LEFT\n");
      free(p);
      pthread_exit(NULL);
}

/*
Thread function to read maintenance msgs from msg queue parallel to edge server
*/
void *messageQueueReader(void *p)
{
      int server_number = *((int *)p);
      output_str("EDGE SERVER: MAINTENANCE THREAD IN SERVER BEGAN\n");
      int SEND_MSG_TYPE = server_number + 1 + SM->EDGE_SERVER_NUMBER;

      int RECEIVE_MSG_TYPE = server_number + 1;

      message ready, receive;
      strcpy(ready.msg_text, "READY");
      ready.msg_type = SEND_MSG_TYPE;
      while (1)
      {
            // block until it receives maintenance message
            msgrcv(message_queue_id, &receive, sizeof(message), RECEIVE_MSG_TYPE, 0);

            if (strcmp(receive.msg_text, "MAINTENANCE") == 0)
            {
                  pthread_mutex_lock(&message_queue_mutex[server_number]);
                  // wait for tasks to end and send ready message
                  SM->EDGE_SERVERS[server_number].stopped = 1;

                  // server is ready for maintenance
                  msgsnd(message_queue_id, &ready, sizeof(message), 0);
                  // wait for continue
                  msgrcv(message_queue_id, &receive, sizeof(message), RECEIVE_MSG_TYPE, 0);
                  if (strcmp(receive.msg_text, "CONTINUE") == 0)
                  {
                        SM->EDGE_SERVERS[server_number].stopped = 0;
                        // signal vcpus to continue
                        pthread_cond_signal(&vcpuCond[server_number][0]);
                        pthread_cond_signal(&vcpuCond[server_number][1]);
                  }

                  SM->EDGE_SERVERS[server_number].maintenance_counter++;
                  SM->simulation_stats.maintenance_pserver[server_number]++;
                  pthread_mutex_unlock(&message_queue_mutex[server_number]);
            }
            else
            {
                  continue;
            }
      }
      free(p);
      pthread_exit(NULL);
}

//###############################################
// MAINTENANCE MANAGER
//###############################################

/*
Main function for maintenance manager process
*/
void maintenance_manager()
{
      SM->c_pid[2] = getpid();
      signal(SIGUSR1, maint_manager_handler);
      output_str("MAINTENANCE MANAGER: WORKING\n");

      message server_creation;
      int EDGE_SERVER_NUMBER = 0;
      // while not all edge servers are created receive msg
      while (1)
      {
            msgrcv(message_queue_id, &server_creation, sizeof(message), -2, 0);
            if (strcmp(server_creation.msg_text, "EDGE SERVER CREATED") == 0)
            {
                  EDGE_SERVER_NUMBER++;
            }
            else if (strcmp(server_creation.msg_text, "END") == 0)
            {
                  char print[60];
                  sprintf(print, "MAINTENANCE MANAGER: ALL EDGE SERVERS CREATED-%d IN TOTAL\n", EDGE_SERVER_NUMBER);
                  output_str(print);
                  break;
            }
      }

      // allocate memory
      maintWork = (int *)calloc(EDGE_SERVER_NUMBER, sizeof(int));
      maint_cond = (pthread_cond_t *)calloc(EDGE_SERVER_NUMBER, sizeof(pthread_cond_t));
      maint_cond_mutex = (pthread_mutex_t *)calloc(EDGE_SERVER_NUMBER, sizeof(pthread_mutex_t));
      maintenance_thread = (pthread_t *)calloc(EDGE_SERVER_NUMBER, sizeof(pthread_t));

      /// create threads to maintain each server
      for (int i = 0; i < EDGE_SERVER_NUMBER; i++)
      {
            pthread_cond_init(&maint_cond[i], NULL);
            pthread_mutex_init(&maint_cond_mutex[i], NULL);
            maint_thread_info *arg1 = malloc(sizeof(maint_thread_info));
            arg1->server_number = i;
            arg1->total_server_number = EDGE_SERVER_NUMBER;

            pthread_create(&maintenance_thread[i], NULL, &maintenance_thread_func, arg1);
      }
      int chosen_server;

      // loop to indicate threads to enter maintenance
      while (1)
      {
            // interval until next maintenance
            int time_to_next_maintenance = (rand() % 5) + 1;
            time2 = time(NULL);
            sleep(time_to_next_maintenance);

            pthread_mutex_lock(&max_maint_mutex);
            while (maintenance_now >= EDGE_SERVER_NUMBER - 1)
            {
                  pthread_cond_wait(&max_maint_server, &max_maint_mutex);
            }
            pthread_mutex_unlock(&max_maint_mutex);

            pthread_mutex_lock(&maint_mutex);
            // make sure not all servers are in maintenance

            // pick server for maintenance

            chosen_server = maintenance_counter % EDGE_SERVER_NUMBER;

            pthread_mutex_unlock(&maint_mutex);
            // signal cond for maintenance thread of chosen server and change flag
            maintWork[chosen_server] = 1;
            pthread_cond_broadcast(&maint_cond[chosen_server]);
      }

      for (int i = 0; i < EDGE_SERVER_NUMBER; i++)
      {
            pthread_join(maintenance_thread[i], NULL);
      }

      pthread_mutex_destroy(&maint_mutex);
      pthread_mutex_destroy(&max_maint_mutex);
      pthread_cond_destroy(&max_maint_server);

      free(maint_cond);
      free(maint_cond_mutex);
      free(maintWork);
      free(maintenance_thread);

      output_str("MAINTENANCE MANAGER: CLOSED\n");
      exit(0);
}

/*
Thread function to manage each server's maintenance
*/
void *maintenance_thread_func(void *p)
{

      maint_thread_info info = *((maint_thread_info *)p);
      // set message characteristics
      int RECEIVE_MSG_TYPE = info.server_number + 1 + info.total_server_number;

      int SEND_MSG_TYPE = info.server_number + 1;

      message enter_maintenance, server_continue, receive;
      strcpy(enter_maintenance.msg_text, "MAINTENANCE");
      enter_maintenance.msg_type = SEND_MSG_TYPE;
      strcpy(server_continue.msg_text, "CONTINUE");
      server_continue.msg_type = SEND_MSG_TYPE;
      char print[40];

      while (1)
      {
            pthread_mutex_lock(&maint_cond_mutex[info.server_number]);
            while (maintWork[info.server_number] == 0)
            {
                  pthread_cond_wait(&maint_cond[info.server_number], &maint_cond_mutex[info.server_number]);
            }
            pthread_mutex_unlock(&maint_cond_mutex[info.server_number]);

            if (SM->shutdown == 1)
            {
                  break;
            }
            // increment maintenance now counter
            pthread_mutex_lock(&maint_mutex);
            maintenance_now++;
            maintenance_counter++;
            pthread_mutex_unlock(&maint_mutex);

            // put server to maintenance
            msgsnd(message_queue_id, &enter_maintenance, sizeof(message), 0); // 0 to block if theres no space available
            // wait for ready message
            msgrcv(message_queue_id, &receive, sizeof(message), RECEIVE_MSG_TYPE, 0);
            printf("MESSAGE RECEIVED: %s\n", receive.msg_text);
            // check received message from server
            if (strcmp(receive.msg_text, "READY") == 0)
            {
                  sprintf(print, "EDGE SERVER %d ENTERED MAINTENANCE\n", info.server_number);
                  output_str(print);
                  SM->EDGE_SERVERS[info.server_number].stopped = 1;

                  // maintain
                  random1 = (rand() % 5) + 1;
                  time1 = time(NULL);
                  sleep(random1);

                  // send message to continue
                  msgsnd(message_queue_id, &server_continue, sizeof(message), 0);
                  sprintf(print, "EDGE SERVER %d LEFT MAINTENANCE\n", info.server_number);
                  SM->EDGE_SERVERS[info.server_number].stopped = 0;
                  output_str(print);

                  // increase maint counter and decrease current maint counter
                  pthread_mutex_lock(&maint_mutex);
                  maintenance_now--;
                  pthread_cond_signal(&max_maint_server);
                  // set flag to 0 so it locks in the cond wait
                  maintWork[info.server_number] = 0;
                  pthread_mutex_unlock(&maint_mutex);
            }
            else
            {
                  output_str("MAINTENANCE FAILED\n");
                  pthread_mutex_lock(&maint_mutex);
                  maintenance_now--;
                  pthread_cond_signal(&max_maint_server);
                  maintWork[info.server_number] = 0;
                  pthread_mutex_unlock(&maint_mutex);
            }
      }
      free(p);
      pthread_exit(NULL);
}

//###############################################
// SIGNAL HANDLERS
//###############################################

/*
Handler function for controlled exit of edge server
*/
void edge_server_handler(int signum)
{
      pid_t this = getpid();

      int i;
      for (i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {

            if (this == SM->edge_pid[i])
            {
                  break;
            }
      }

      SM->EDGE_SERVERS[i].stopped = 0;
      SM->times_edgeserver[i][0] = 1;
      SM->times_edgeserver[i][1] = 1;
      pthread_cond_broadcast(&vcpuCond[i][0]);
      pthread_cond_broadcast(&vcpuCond[i][1]);

      SM->taskToProcess[i][0] = 1;
      SM->taskToProcess[i][1] = 1;
      pthread_cond_broadcast(&SM->edgeServerCond[i]);
      output_str("EDGE SERVER: LEAVING\n");
}

/*
Handler function for sigtstp
*/
void sigtstp_handler(int signum)
{

      output_str("SYSTEM: ^Z PRESSED. PRINTING STATISTICS.\n");
      sem_wait(outputSemaphore);
      SM->simulation_stats.unanswered_tasks = SM->num_queue;
      print_stats();

      sem_post(outputSemaphore);
}

/*
Function to print current system stats
*/
void print_stats()
{

      printf("Number of requested tasks: %d\n", SM->simulation_stats.requested_tasks);

      printf("Number of executed tasks: %d\n", SM->simulation_stats.executed_tasks);
      printf("Number of unanswered tasks: %d\n", SM->simulation_stats.unanswered_tasks);

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            printf("Server %d executed: %d tasks\n", i + 1, SM->simulation_stats.executed_pserver[i]);
      }
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            printf("Server %d went on maintenence: %d times\n", i + 1, SM->simulation_stats.maintenance_pserver[i]);
      }
}

/*
Handler function for sigint signal
*/
void sigint_handler(int signum)
{
      output_str("SYSTEM: ^C PRESSED. CLOSING PROGRAM.\n");
      output_str("SYSTEM: WAITING FOR EDGESERVERS TO FINISH\n");
      end_sim();
}

/*
Handler function for task manager controlled exit
*/
void task_manager_handler(int signum)
{

      // close all pipes
      close(taskpipe);
      output_str("TASK MANAGER: TASK_PIPE CLOSED\n");
      pthread_cancel(SM->taskmanager[0]);
      SM->dispatcherWork = 1;
      pthread_cond_broadcast(&SM->dispatcherCond);
      output_str("TASK MANAGER: SCHEDULER LEFT\n");
}

/*
Handler function for maint manager controlled exit
*/
void maint_manager_handler(int signum)
{

      time_t time_passed1;
      time_t time_passed2;

      time_passed1 = time(NULL) - time1;
      time_passed2 = time(NULL) - time2;

      if (random2 > time_passed2)
      {
            output_str("MAINTENANCE MANAGER: WAITING FOR MAINTENANCE TO FINISH ON SOME EDGE SERVER\n");
            if (random1 > time_passed1)
            {
                  sleep(random1 - time_passed1 + random2 - time_passed2);
            }
            else
            {
                  sleep(random2 - time_passed2);
            }
      }
      output_str("MAINTENANCE MANAGER: LEAVING\n");
      exit(0);
}

/*
Handler function for monitor exit
*/
void monitor_handler(int signum)
{
      output_str("MONITOR: LEFT\n");
      exit(0);
}

//###############################################
// CLEANUP
//###############################################

/*
Function to clear and free all elements needed and end simulation
*/
void end_sim()
{

      output_str("SYSTEM: SIMULATOR STARTED CLOSING\n");
      // code to clear
      sem_wait(semaphore);

      // dispacher and
      SM->shutdown = 1;
      SM->performance_flag = 1;

      // signal processes to check condition variables
      // signal tm
      kill(SM->taskmanager[0], SIGUSR1);

      // scheduler leaves with task pipe closure

      // signal dispatcher to leave
      SM->continue_dispatch = 1;
      pthread_cond_broadcast(&SM->continueDispatchCond);
      SM->dispatcherWork = 1;
      pthread_cond_broadcast(&SM->dispatcherCond);

      // signal edge servers
      SM->monitorWork = 1;

      if (fork() == 0)
      {
            for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
            {
                  kill(SM->edge_pid[i], SIGUSR1);
            }
            exit(0);
      }

      sem_post(semaphore);

      sem_unlink(VCPU_STATE_SEM);
}

//###############################################
// CONFIG FILE
//###############################################
/*
Function to handle the config.txt file and define running variables
*/
void get_running_config(FILE *ptr)
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
                        // define vcpu 0 as the lower processing one
                        if (vCPU_capacities[0] <= vCPU_capacities[1])
                        {
                              SM->EDGE_SERVERS[x].vcpu[0].capacity = vCPU_capacities[0];
                              SM->EDGE_SERVERS[x].vcpu[1].capacity = vCPU_capacities[1];
                        }
                        else
                        {
                              SM->EDGE_SERVERS[x].vcpu[0].capacity = vCPU_capacities[1];
                              SM->EDGE_SERVERS[x].vcpu[1].capacity = vCPU_capacities[0];
                        }

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

//###############################################
// LOG FILE
//###############################################
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
