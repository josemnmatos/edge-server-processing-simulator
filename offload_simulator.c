// João Maria Campos Donato 2020217878
// José Miguel Norte de Matos 2020217977

#include "simulation_structs.h"

#define PIPE_NAME "TASK_PIPE"
#define PIPE_BUF 64
#define NUM_PROCESS_INI 3

void system_manager(const char *config_file, pid_t sm_pid);
void task_manager(shared_memory *SM);
void *task_manager_scheduler(void *p);
void *task_manager_dispatcher(void *p);
void edge_server_process(shared_memory *SM, int server_number);
void monitor(shared_memory *SM);
void maintenance_manager(int EDGE_SERVER_NUMBER);
void get_running_config(FILE *ptr, shared_memory *SM);
void sigint_handler(int signum);
void sigtstp_handler(int signum);
void output_str(char *s);
void end_sim();
void *vCPU_task(void *p);
void maint_manager_handler(int signum);
void task_manager_handler(int signum);
void edge_server_handler(int signum);
void print_stats();
void *close_handler(void *p);

int shmid;
shared_memory *SM;
sem_t *semaphore;
sem_t *outputSemaphore;
sem_t *TMSemaphore;
pthread_mutex_t sm_mutex;

FILE *config_ptr, *log_ptr;

int **fd;
int taskpipe;
int maintenance_queue_id;

pthread_cond_t schedulerCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t taskQueue = PTHREAD_MUTEX_INITIALIZER;

request *requestList;
request *reqListFinal;

int scheduler = 0;

time_t time1;
time_t time2;
int random1;
int random2;

int *vcpu_time;

// compile with : make all
int main(int argc, char *argv[])
{
      signal(SIGINT, SIG_IGN);
      signal(SIGTSTP, SIG_IGN);

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
      pid_t sysmanpid;

      if ((sysmanpid = fork()) == 0)
      {

            system_manager(argv[1], sysmanpid);
      }
      if (sysmanpid == -1)
      {
            output_str("ERROR CREATING SYSTEM MANAGER\n");
            exit(1);
      }

      // wait for system manager process to end
      wait(NULL);

      // close the rest of req
      free(requestList);
      free(reqListFinal);

      if (semaphore >= 0)
            sem_close(semaphore);
      free(SM->min_waiting);
      free(SM->EDGE_SERVERS);
      free(SM->edge_pid);
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            pthread_cond_destroy(&SM->edgeServerCond[i]);
            pthread_mutex_destroy(&SM->edgeServerMutex[i]);
      }
      free(SM->edgeServerCond);
      free(SM->edgeServerMutex);
      free(SM->taskToProcess);

      pthread_mutex_destroy(&SM->vcpuMutex);
      pthread_cond_destroy(&SM->vcpuCond);

      if (shmid >= 0)
            shmctl(shmid, IPC_RMID, NULL);
      fclose(log_ptr);
      output_str("SIMULATOR CLOSED\n");
      if (outputSemaphore >= 0)
            sem_close(outputSemaphore);

      return 0;
}

//###############################################
// SYSTEM MANAGER
//###############################################

void system_manager(const char *config_file, pid_t sm_pid)
{
      // ignore sigint and sigtstp
      signal(SIGINT, SIG_IGN);
      signal(SIGTSTP, SIG_IGN);

      //********* capture sigtstp for statistics ********

      // create shared memory
      shmid = shmget(IPC_PRIVATE, sizeof(shared_memory), IPC_CREAT | 0700);
      SM = (shared_memory *)shmat(shmid, NULL, 0);

      SM->sm_pid = sm_pid;

      // open config file and get the running config

      FILE *config_ptr = fopen(config_file, "r");
      if (config_ptr == NULL)
      {

            output_str("ERROR: CAN'T OPEN FILE\n");
      }

      get_running_config(config_ptr, SM);

      //********* validate config file information ********

      output_str("CONFIGURATION SET\n");

      SM->edgeServerCond = (pthread_cond_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pthread_cond_t));
      SM->edgeServerMutex = (pthread_mutex_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pthread_mutex_t));

      pthread_mutexattr_t attrmutex;
      pthread_condattr_t attrcondv;
      /* Initialize attribute of mutex. */
      pthread_mutexattr_init(&attrmutex);
      pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED);

      /* Initialize attribute of condition variable. */
      pthread_condattr_init(&attrcondv);
      pthread_condattr_setpshared(&attrcondv, PTHREAD_PROCESS_SHARED);

      pthread_cond_init(&SM->monitorCond, &attrcondv);
      pthread_mutex_init(&SM->monitorMutex, &attrmutex);

      pthread_cond_init(&SM->schedulerCond, &attrcondv);
      pthread_mutex_init(&SM->schedulerMutex, &attrmutex);

      pthread_cond_init(&SM->dispatcherCond, &attrcondv);
      pthread_mutex_init(&SM->dispatcherMutex, &attrmutex);

      pthread_cond_init(&SM->vcpuCond, &attrcondv);
      pthread_mutex_init(&SM->vcpuMutex, &attrmutex);

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            pthread_cond_init(&SM->edgeServerCond[i], &attrcondv);
            pthread_mutex_init(&SM->edgeServerMutex[i], &attrmutex);
      }

      SM->num_queue = 0;
      SM->shutdown = 0;
      SM->min_waiting = (int *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int));

      // create msg queue
      assert((maintenance_queue_id = msgget(IPC_PRIVATE, IPC_CREAT | 0700)) != -1);

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
            // not using
            monitor(SM);
            exit(0);
      }
      if (SM->c_pid[0] == -1)
      {

            output_str("ERROR CREATING PROCESS MONITOR\n");
            exit(1);
      }

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

      // maintenance manager process
      if ((SM->c_pid[2] = fork()) == 0)
      {
            output_str("PROCESS MAINTENANCE_MANAGER CREATED\n");
            // not using
            maintenance_manager(SM->EDGE_SERVER_NUMBER);
            exit(0);
      }
      if (SM->c_pid[2] == -1)
      {
            output_str("ERROR CREATING MAINTENANCE_MANAGER\n");

            exit(3);
      }

      // handle control c and ctrl z
      signal(SIGINT, sigint_handler);
      signal(SIGTSTP, sigtstp_handler);

      // wait for all system manager child processes to end
      for (int j = 0; j < NUM_PROCESS_INI; j++)
      {
            wait(NULL);
      }

      exit(0);
}

//###############################################
// MONITOR
//###############################################

// monitor says the performance level of the edge servers
// monitor knows which vcpus are available
// activates and deactivates vcpus

// only gonna run when something changes in the queue

void monitor(shared_memory *SM)
{
      SM->performance_flag = 0;

      float queue_rate;
      output_str("MONITOR WORKING\n");
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
            queue_rate = SM->num_queue / SM->QUEUE_POS;
            // get minimum waiting time on any vcpu
            int minimum_waiting_time = SM->min_waiting[0];
            SM->server = 1;

            // function to see the minimum in min_waiting;
            for (int i = 1; i < SM->EDGE_SERVER_NUMBER; i++)
            {
                  if (SM->min_waiting[i] < minimum_waiting_time)
                  {
                        minimum_waiting_time = SM->min_waiting[i];
                        SM->server = i + 1;
                  }
            }

            if ((queue_rate > 0.8) && SM->performance_flag == 0 && (minimum_waiting_time > SM->MAX_WAIT))
            {
                  output_str("EDGE SERVERS IN HIGH PERFORMANCE\n");
                  sem_wait(semaphore);
                  SM->performance_flag = 1;
                  sem_post(semaphore);
            }
            if (queue_rate < 0.2 && SM->performance_flag == 1)
            {
                  output_str("EDGE SERVERS IN NORMAL PERFORMANCE\n");
                  sem_wait(semaphore);
                  SM->performance_flag = 0;
                  sem_post(semaphore);
            }
            SM->monitorWork = 0;
            pthread_mutex_unlock(&SM->monitorMutex);
      }
      pthread_cond_destroy(&SM->monitorCond);
      pthread_mutex_destroy(&SM->monitorMutex);
      output_str("MONITOR CLOSED\n");
      exit(0);
}

//###############################################
// TASK MANAGER
//###############################################

void task_manager(shared_memory *SM) //nao ta a funcionar bem so lê uma vez e não fecha se ainda estiver a ler fica bloqueado
{
      // handler to shutdown task manager process
      signal(SIGUSR1, task_manager_handler);

      output_str("TASK_MANAGER WORKING\n");
      // create a thread for each job
      sem_wait(semaphore);
      pthread_create(&SM->taskmanager[0], NULL, task_manager_scheduler, NULL);
      pthread_create(&SM->taskmanager[1], NULL, task_manager_dispatcher, NULL);

      // alocate memory for sleep time vcpu
      vcpu_time = (int *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int));

      SM->edge_pid = (pid_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pid_t));
      SM->EDGE_SERVERS = (edge_server *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(edge_server));
      SM->taskToProcess = (int *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int));

      // create SM->EDGE_SERVER_NUMBER number of pipes
      fd = (int **)calloc(SM->EDGE_SERVER_NUMBER, sizeof(int *));
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            fd[i] = (int *)calloc(2, sizeof(int));
            if (fd[i] == NULL)
            {
                  output_str("ERROR ALLOCATING MEMORY FOR UNNAMED PIPE\n");
            }
      }

      // alocate memory for requestList
      requestList = (request *)calloc(SM->QUEUE_POS, sizeof(request));
      reqListFinal = (request *)calloc(SM->QUEUE_POS, sizeof(request));

      sem_post(semaphore);

      // create SM->EDGE_SERVER_NUMBER edge servers
      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            if ((SM->edge_pid[i] = fork()) == 0)
            {
                  // do what edge servers do
                  pipe(fd[i]);
                  edge_server_process(SM, i);
                  free(fd[i]);
                  exit(0);
            }
            else if (SM->edge_pid[i] == -1)
            {
                  output_str("ERROR CREATING EDGE SERVER\n");
            }
      }

      TMSemaphore = (sem_t *)malloc(sizeof(sem_t *));
      sem_init(TMSemaphore, 1, 1);

      int nread;
      // read taskpipe and send it to the queue
      if ((taskpipe = open(PIPE_NAME, O_RDWR)) < 0)
      {

            output_str("ERROR OPENING NAMED PIPE\n");
            exit(0);
      }

      char *task_read_string;
      task tsk;
      request req;

      while (1)
      {
            
            nread = read(taskpipe, task_read_string, PIPE_BUF);
            

            // read until pipe closes
            if (nread <= 0 || errno == EINTR)
            {
                  break;
            }
            // only goes down here if it reads something -- pipe is open on blocking mode
            if (SM->shutdown == 1)
            {
                  break;
            }
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
            // handle the received task string
            char *str_id, *str_tips;
            char str_maxet[] = "";
            str_id = strsep(&task_read_string, ":");
            str_tips = strsep(&task_read_string, ":");
            strcpy(str_maxet, task_read_string);

            tsk.id = atoi(str_id);
            tsk.thousInstructPerRequest = atoi(str_tips);
            tsk.maxExecTimeSecs = atoi(str_maxet);
            printf("%d\n", tsk.id);

            req.tsk = tsk;

            sem_wait(TMSemaphore);

            if (SM->num_queue > SM->QUEUE_POS)
            {
                  output_str("FULL QUEUE: TASK HAS BEEN DELETED\n");
            }
            else
            {
                  req.timeOfEntry = time(NULL);
                  // add request at end of queue and signal the scheduler
                  requestList[SM->num_queue++] = req; 
                  

                  pthread_cond_signal(&schedulerCond);
                  pthread_cond_signal(&SM->monitorCond);
            }
            sem_post(TMSemaphore);

            sem_wait(TMSemaphore);
            pthread_cond_signal(&SM->dispatcherCond);
            sem_post(TMSemaphore);
      }

      // wait for the threads to finish
      pthread_join(SM->taskmanager[0], NULL);
      output_str("TASK_MANAGER_SCHEDULER CLOSED\n");

      pthread_join(SM->taskmanager[1], NULL);
      output_str("TASK_MANAGER_DISPATCHER CLOSED\n");

      // wait for all edge servers to exit
      for (int j = 0; j < SM->EDGE_SERVER_NUMBER; j++)
      {
            wait(NULL);
      }

      free(vcpu_time);

      output_str("TASK_MANAGER CLOSING\n");

      pthread_cond_destroy(&schedulerCond);

      exit(0);
}
// checks and organizes queue according to maxExecutiontime and arrival time to queue
// needs a cond to only be active when a new msg arrives

void *task_manager_scheduler(void *p)
{
      output_str("TASK_MANAGER_SCHEDULER WORKING\n");
      time_t timenow;

      while (1)
      {
            pthread_mutex_lock(&SM->schedulerMutex);

            while (SM->schedulerWork == 0)
            {
                  pthread_cond_wait(&SM->schedulerCond, &SM->schedulerMutex);
                  SM->schedulerWork = 1;
            }
            // check if system shutting down
            if (SM->shutdown == 1)
            {
                  pthread_mutex_unlock(&SM->schedulerMutex);
                  break;
            }
            // organizar a fila tarefa com maior prioridade vai ficar na prieira posiçao
            request temp;
            if (SM->num_queue > 1)
            {
                  for (int i = 0; i < SM->num_queue; i++)
                  {
                        for (int j = i + 1; j < SM->num_queue; j++)
                        {
                              if (requestList[i].tsk.maxExecTimeSecs > requestList[j].tsk.maxExecTimeSecs)
                              {
                                    temp = requestList[i];
                                    requestList[i] = requestList[j];
                                    requestList[j] = temp;
                              }
                              else if (requestList[i].tsk.maxExecTimeSecs == requestList[j].tsk.maxExecTimeSecs)
                              {
                                    if (requestList[i].timeOfEntry > requestList[j].timeOfEntry)
                                    {
                                          temp = requestList[i];
                                          requestList[i] = requestList[j];
                                          requestList[j] = temp;
                                    }
                              }
                        }
                  }
            }

            int j = 0;
            int numQueueOriginal = SM->num_queue;

            for (int i = 0; i < numQueueOriginal; i++)
            {
                  timenow = time(NULL);
                  if ((requestList[i].timeOfEntry + requestList[i].tsk.maxExecTimeSecs) < timenow)
                  {
                        reqListFinal[j] = requestList[i];
                        j++;
                  }
                  else
                  {
                        SM->num_queue--;
                  }
            }

            SM->schedulerWork = 0;

            pthread_mutex_unlock(&SM->schedulerMutex);
      }
      pthread_exit(NULL);
}

// checks the task with most priority can be executed by a vcpu in time inferior to MaxEXECTIME
// this thread is only activated if a vcpu is free
// precisamos de um cond p saber se ha algum vcpu livre

// esta funçao ta mal por enquanto não sei com qual tem de comunicar para ver se ta algum livre
void *task_manager_dispatcher(void *p)
{
      output_str("TASK_MANAGER_DISPATCHER WORKING\n");

      while (1)
      {
            // this thread is only activated if a vcpu is free
            pthread_mutex_lock(&SM->dispatcherMutex);
            while (SM->dispatcherWork == 0)
            { // condition to check if any is free
                  pthread_cond_wait(&SM->dispatcherCond, &SM->dispatcherMutex);
                  SM->dispatcherWork = 1;
            }
            // check if system is shutting down
            if (SM->shutdown == 1)
            {
                  pthread_mutex_unlock(&SM->dispatcherMutex);
                  break;
            }
            // do dispatcher things
            request most_priority = reqListFinal[0];
            int i;
            for (i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
            {
                  // checks the task with most priority can be executed by a vcpu in time inferior to MaxEXECTIME
                  // if edge server is on maintenance skip
                  if (SM->EDGE_SERVERS[i].stopped == 1)
                  {
                        pthread_mutex_unlock(&SM->dispatcherMutex);
                        continue;
                  }
                  // check vcpu 1, if yes dispatch
                  int vcpu1_instruction_capacity = SM->EDGE_SERVERS[i].vCPU_1_capacity * (1000000);
                  int task_instructions = most_priority.tsk.thousInstructPerRequest * (1000);
                  float processing_time = task_instructions / vcpu1_instruction_capacity;
                  // dispatch task to edge server
                  if (processing_time <= (time(NULL) - most_priority.timeOfEntry))
                  {
                        // write to pipe for execution on vcpu 1
                        close(fd[i][0]);
                        write(fd[i][1], &most_priority.tsk, sizeof(most_priority.tsk));
                        SM->taskToProcess[i] = 1;
                        pthread_cond_broadcast(&SM->edgeServerCond[i]);
                        output_str("TASK DISPATCHED\n");
                        pthread_mutex_unlock(&SM->dispatcherMutex);
                        break;
                  }

                  // check vcpu 2, if yes and cpu 2 is on, dispatch
                  if (SM->performance_flag == 1)
                  {
                        int vcpu2_instruction_capacity = SM->EDGE_SERVERS[i].vCPU_1_capacity * (1000000);
                        float processing_time = task_instructions / vcpu2_instruction_capacity;
                        if (processing_time <= (time(NULL) - most_priority.timeOfEntry))
                        {
                              // write to pipe for execution on vcpu 2
                              close(fd[i][0]);
                              write(fd[i][1], &most_priority.tsk, sizeof(most_priority.tsk));
                              SM->taskToProcess[i] = 1;
                              pthread_cond_broadcast(&SM->edgeServerCond[i]);
                              output_str("TASK DISPATCHED\n");
                              pthread_mutex_unlock(&SM->dispatcherMutex);
                              break;
                        }
                  }
            }
            // if task not dispatched, delete
            if (i == SM->EDGE_SERVER_NUMBER)
            {
                  int j;
                  // delete task on position 0
                  for (j = 0; j < SM->num_queue; j++)
                  {
                        if (most_priority.tsk.id == requestList[j].tsk.id)
                        {
                              break;
                        }
                  }

                  for (int p = j; p < SM->num_queue - 1; p++)
                  {
                        requestList[p] = requestList[p + 1];
                  }
                  SM->num_queue--;
                  output_str("TASK ELIMINATED: MAX EXEC TIME EXCEEDED\n");
            }
            SM->dispatcherWork = 0;
            pthread_mutex_unlock(&SM->dispatcherMutex);
      }
      pthread_exit(NULL);
}

//###############################################
// EDGE SERVERS
//###############################################

void edge_server_process(shared_memory *SM, int server_number)
{

      int stopped = 0;

      // notify startup to maintenance manager
      signal(SIGUSR1, edge_server_handler);

      // int lower_processing_vcpu_state = 0;

      vcpu_info *arg1 = malloc(sizeof(vcpu_info));
      vcpu_info *arg2 = malloc(sizeof(vcpu_info));

      arg1->server_number = server_number;
      arg2->server_number = server_number;
      arg1->vcpu_number = 1;
      arg2->vcpu_number = 2;

      // creates threads for each cpu
      sem_wait(semaphore);
      // check which vcpu has the lowest processing capacity

      if (SM->EDGE_SERVERS[server_number].vCPU_1_capacity <= SM->EDGE_SERVERS[server_number].vCPU_2_capacity)
      {
            pthread_create(&SM->EDGE_SERVERS[server_number].vCPU[0], NULL, &vCPU_task, arg1);
            pthread_create(&SM->EDGE_SERVERS[server_number].vCPU[1], NULL, &vCPU_task, arg2);
      }
      else
      {
            pthread_create(&SM->EDGE_SERVERS[server_number].vCPU[0], NULL, &vCPU_task, arg2);
            pthread_create(&SM->EDGE_SERVERS[server_number].vCPU[1], NULL, &vCPU_task, arg1);
      }
      sem_post(semaphore);

      task to_execute;

      while (1)
      {
            // wait for signal from dispatcher that task has been sent
            pthread_mutex_lock(&SM->edgeServerMutex[server_number]);

            while (SM->taskToProcess[server_number] == 0)
            {
                  // output_str("entered cond\n");
                  pthread_cond_wait(&SM->edgeServerCond[server_number], &SM->edgeServerMutex[server_number]);
                  //output_str("left cond\n");
            }

            close(fd[server_number][1]);
            int nbytes = read(fd[server_number][0], &to_execute, sizeof(task));
            if (nbytes <= 0)
            {
                  output_str("EDGE SERVER NOT READING ANYMORE\n");
                  break;
            }

            // send to vcpu

            pthread_mutex_unlock(&SM->edgeServerMutex[server_number]);
      }

      // clean
      pthread_join(SM->EDGE_SERVERS[server_number].vCPU[0], NULL);
      pthread_join(SM->EDGE_SERVERS[server_number].vCPU[1], NULL);

      // output_str("edge server left\n");
      exit(0);
}

void *vCPU_task(void *p)
{
      vcpu_info info = *((vcpu_info *)p);
      // TODO: know what vcpu it is recieves as argument in create
      // do for the task that is still going, to finish

      while (1)
      {
            pthread_mutex_lock(&SM->vcpuMutex);

            // check if edge server has entered maintenance
            while (SM->EDGE_SERVERS[info.server_number].stopped == 1)
            {
                  pthread_cond_wait(&SM->vcpuCond, &SM->vcpuMutex);
            }
            if (SM->shutdown == 1)
            {
                  pthread_mutex_unlock(&SM->vcpuMutex);
                  break;
            }
            // condition to lock highest performing vcpu when on normal mode
            while (SM->performance_flag == 0 && info.vcpu_number == 2)
            {
                  pthread_cond_wait(&SM->vcpuCond, &SM->vcpuMutex);
            }
            if (SM->shutdown == 1)
            {
                  pthread_mutex_unlock(&SM->vcpuMutex);
                  break;
            }
            // process

            // char msg[60];
            // sprintf(msg, "VPCU TASK COMPLETE BY THREAD %ld\n", pthread_self());

            // output_str(msg);

            pthread_mutex_unlock(&SM->vcpuMutex);
      }
      output_str("vcpu left\n");
      free(p);
      pthread_exit(NULL);
}

//###############################################
// MAINTENANCE MANAGER
//###############################################

void maintenance_manager(int EDGE_SERVER_NUMBER)
{
      signal(SIGUSR1, maint_manager_handler);
      output_str("MAINTENANCE MANAGER WORKING\n");
      int maintenance_counter = 0;
      message send, receive;
      while (1)
      {

            // send message to server
            sprintf(send.msg_text, "MAINTENANCE;%d", maintenance_counter);
            // one edge server at a time
            send.msg_type = maintenance_counter % EDGE_SERVER_NUMBER;
            msgsnd(maintenance_queue_id, &send, sizeof(send), 0);
            // wait on message from server of type to enter maintenance
            msgrcv(maintenance_queue_id, &receive, sizeof(receive), send.msg_type, 0);
            if (strcmp(receive.msg_text, "READY") == 0)
            {
                  // enter maintenance
                  random1 = (rand() % 5) + 1;
                  time1 = time(NULL);
                  sleep(random1);
                  // send message to continue
                  strcpy(send.msg_text, "CONTINUE");
                  msgsnd(maintenance_queue_id, &send, sizeof(send), 0);
                  maintenance_counter++;
                  // maintenance interval
                  random2 = (rand() % 5) + 1;
                  time2 = time(NULL);
                  sleep(random2);
            }
            else
            {
                  output_str("SERVER MAINTENANCE FAILED");
                  exit(0);
            }
      }

      output_str("MAINTENANCE MANAGER CLOSED\n");
      exit(0);
}

//###############################################
// SIGNAL HANDLERS
//###############################################

void edge_server_handler(int signum)
{
      output_str("EDGE SERVER LEAVING\n");
       for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            SM->taskToProcess[i] = 1;
            pthread_cond_broadcast(&SM->edgeServerCond[i]);
      }

      //exit(0);
}

void sigtstp_handler(int signum)
{

      output_str("^Z PRESSED. PRINTING STATISTICS.\n");
      sem_wait(outputSemaphore);
      SM->simulation_stats.unanswered_tasks = SM->num_queue;
      print_stats();
      // rest

      sem_post(outputSemaphore);
}

void print_stats()
{
      printf("Number of requested tasks: %d\n", SM->simulation_stats.requested_tasks);
      printf("Number of executed tasks: %d\n", SM->simulation_stats.executed_tasks);
      printf("Number of unanswered tasks: %d\n", SM->simulation_stats.unanswered_tasks);
}

void sigint_handler(int signum)
{
      output_str("^C PRESSED. CLOSING PROGRAM.\n");
      output_str("WAITING FOR EDGESERVERS TO FINISH\n");
      end_sim();
}

void task_manager_handler(int signum)
{

      // close all pipes
      close(taskpipe);
}

void maint_manager_handler(int signum)
{

      time_t time_passed1;
      time_t time_passed2;

      time_passed1 = time(NULL) - time1;
      time_passed2 = time(NULL) - time2;

      if (random2 > time_passed2)
      {
            output_str("WAITING FOR MAINTENANCE TO FINISH ON SOME EDGE SERVER\n");
            if (random1 > time_passed1)
            {
                  sleep(random1 - time_passed1 + random2 - time_passed2);
            }
            else
            {
                  sleep(random2 - time_passed2);
            }
      }

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

      output_str("SIMULATOR CLOSING\n");
      // code to clear
      sem_wait(semaphore);

      // dispacher and
      SM->shutdown = 1;

      // for both cpus to check shutdown condition

      // signal processes to check condition variables

      SM->dispatcherWork = 1;
      pthread_cond_broadcast(&SM->dispatcherCond);
      SM->schedulerWork = 1;
      pthread_cond_broadcast(&SM->schedulerCond);

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            if (SM->EDGE_SERVERS[i].stopped == 1)
            {
                  SM->EDGE_SERVERS[i].stopped = 0;
                  pthread_cond_broadcast(&SM->vcpuCond);
            }
      }

      SM->performance_flag = 1;
      pthread_cond_broadcast(&SM->vcpuCond);

     
      // signal tm
      kill(SM->c_pid[1], SIGUSR1);
      // signal mm
      kill(SM->c_pid[2], SIGUSR1);
      // signal edge servers

      for (int i = 0; i < SM->EDGE_SERVER_NUMBER; i++)
      {
            kill(SM->edge_pid[i], SIGUSR1);
      }

      SM->monitorWork = 1;
      pthread_cond_broadcast(&SM->monitorCond);

      sem_post(semaphore);
}

//###############################################
// CONFIG FILE
//###############################################
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
