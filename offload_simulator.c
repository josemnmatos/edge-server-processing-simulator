
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
void monitor(shared_memory *SM);
void maintenance_manager(shared_memory *SM);
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
sem_t *TMSemaphore;
pthread_mutex_t vcpu_mutex, sm_mutex;
FILE *config_ptr, *log_ptr;
int **fd;
int taskpipe;
pthread_cond_t schedulerCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t taskQueue = PTHREAD_MUTEX_INITIALIZER;

request *requestList;

int numQUEUE;

pid_t sysManpid;







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


      if ((sysManpid = fork()) == 0){
            system_manager(argv[1]);
      }
      if (sysManpid == -1){
            output_str("ERROR CREATING SYSTEM MANAGER\n");
            exit(1);
      
      }
      
      //wait for system manager process to end
      wait(NULL);
      
      //close the rest of req
      free(requestList);
      if (semaphore >= 0)
            sem_close(semaphore);
      
      if (shmid >= 0)
            shmctl(shmid, IPC_RMID, NULL);
      fclose(log_ptr);
      output_str("SIMULATOR CLOSED\n");
      if (outputSemaphore >= 0)
            sem_close(outputSemaphore);
      
      return 0;
      
      
      
}

void system_manager(const char *config_file)
{
      // ignore sigint
      signal(SIGINT, SIG_IGN);

      

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

      SM->shutdown = 0;

      // create msg queue
      assert((SM->queue_id = msgget(IPC_PRIVATE, IPC_CREAT|0700)) != -1);

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
            //not using
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
            //not using
            maintenance_manager(SM);
            exit(0);
      }
      if (SM->c_pid[2] == -1)
      {
            output_str("ERROR CREATING MAINTENANCE_MANAGER\n");

            exit(3);
      }
      
      // handle control c
      signal(SIGINT, sigint_handler);

      //wait for all system manager child processes to end
      for(int j=0;j<NUM_PROCESS_INI;j++){
            wait(NULL);
      }

      exit(0);
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
      
      //dispacher and scheduler will die
      SM->shutdown = 1;

      //close all pipes
      close(taskpipe);

      sem_post(semaphore);
   
}

void monitor(shared_memory *SM)
{
      output_str("MONITOR WORKING\n");
      while(SM->shutdown == 0){
            ;
      }
      output_str("MONITOR CLOSED\n");
}




void task_manager(shared_memory *SM)
{
      output_str("TASK_MANAGER WORKING\n");
      // create a thread for each job
      sem_wait(semaphore);
      pthread_create(&SM->taskmanager[0], NULL, task_manager_scheduler, NULL);
      pthread_create(&SM->taskmanager[1], NULL, task_manager_dispatcher, NULL);

      SM->edge_pid = (pid_t *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(pid_t));
      SM->EDGE_SERVERS = (edge_server *)calloc(SM->EDGE_SERVER_NUMBER, sizeof(edge_server));
      output_str("f\n");
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
      output_str("e\n");
      //alocate memory for requestList
      requestList = (request *) calloc(SM->QUEUE_POS, sizeof(request));


      sem_post(semaphore);
      output_str("d\n");
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
      output_str("c\n");
      
      // read taskpipe and send it to the queue
      if ((taskpipe = open(PIPE_NAME, O_RDONLY |O_NONBLOCK)) < 0)
      {
            output_str("ERROR OPENING NAMED PIPE\n");
            exit(0);
      }

      task tsk;
      request req;
      
      output_str("b\n");
      TMSemaphore = (sem_t *)malloc(sizeof(sem_t *));
      sem_init(TMSemaphore, 1, 1);
      output_str("a\n");
      while (SM->shutdown == 0)
      {     
            
            read(taskpipe, &tsk, sizeof(tsk));
            req.tsk = tsk;
            printf("%d\n",req.tsk.maxExecTimeSecs);
            sem_wait(TMSemaphore);
            
            if (numQUEUE == SM->QUEUE_POS){
                  output_str("FULL QUEUE TASK HAS BEEN DELETED\n");
                  
            }
            else{
                  req.timeOfEntry = time(NULL);
                  //add request at end of queue and signal the scheduler
                  requestList[numQUEUE ++] = req;
                 
                  pthread_cond_broadcast(&schedulerCond);
                  
            } 
            sem_post(TMSemaphore);
            ;
      }

      // wait for the threads to finish
      pthread_join(SM->taskmanager[0], NULL);
      output_str("thread 1 left\n");
      pthread_join(SM->taskmanager[1], NULL);
      output_str("thread 2 left\n");

      //wait for all edge servers to exit
      for(int j=0;j<SM->EDGE_SERVER_NUMBER;j++){
            wait(NULL);
      }


      output_str("TASK_MANAGER CLOSING\n");

      exit(0);
      
}

//checks and organizes queue according to maxExecutiontime and arrival time to queue
//needs a cond to only be active when a new msg arrives 


void *task_manager_scheduler(void *p)
{
      output_str("TASK_MANAGER_SCHEDULER WORKING\n");
      
      while(SM->shutdown == 0){
            pthread_mutex_lock(&taskQueue);
            /*
            while(1){
                  pthread_cond_wait(&schedulerCond, &taskQueue);
                  break;
            }*/
            //organizar a fila
            request temp;
            for (int i =0; i< numQUEUE; i++){
                  for (int j = i+1; j< numQUEUE; j++){
                        if (requestList[i].tsk.maxExecTimeSecs > requestList[j].tsk.maxExecTimeSecs){
                              temp = requestList[i];
                              requestList[i] = requestList[j];
                              requestList[j] = temp;
                        }
                        else if (requestList[i].tsk.maxExecTimeSecs == requestList[j].tsk.maxExecTimeSecs)
                        {
                              if(requestList[i].timeOfEntry > requestList[j].timeOfEntry){
                                    temp = requestList[i];
                                    requestList[i] = requestList[j];
                                    requestList[j] = temp;
                              }
                        }
                        
                  }
                  
            }
            
            pthread_mutex_unlock(&taskQueue);
      
      }
      output_str("TASK_MANAGER_SCHEDULER CLOSED\n");
      pthread_exit(NULL);
      
}

//checks the task with most priority can be executed by a vcpu in time inferior to MaxEXECTIME 
//this thread is only activated if a vcpu is free 
//precisamos de um cond p saber se ha algum vcpu livre
void *task_manager_dispatcher(void *p)
{
      output_str("TASK_MANAGER_DISPATCHER WORKING\n");
      
      time_t timenow;
      while(SM->shutdown == 0){
            // se vier condiçao do end sim acaba
            ;
      }
            
      output_str("TASK_MANAGER_DISPATCHER CLOSED\n");
      pthread_exit(NULL);
      
}

void edge_server_process(shared_memory *SM, int server_number)
{     
      //notify startup to maintenance manager


      pthread_mutex_t vcpu_lock = PTHREAD_MUTEX_INITIALIZER;
      pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
      int lower_processing_vcpu_state=0;

      // creates threads for each cpu
      sem_wait(semaphore);
      pthread_create(&SM->EDGE_SERVERS[server_number].vCPU[0], NULL, &vCPU_task, NULL);
      pthread_create(&SM->EDGE_SERVERS[server_number].vCPU[1], NULL, &vCPU_task, NULL);
      sem_post(semaphore);





      pthread_join(SM->EDGE_SERVERS[server_number].vCPU[0], NULL);
      pthread_join(SM->EDGE_SERVERS[server_number].vCPU[1], NULL);

      //clean
      pthread_cond_destroy(&cond);
      pthread_mutex_destroy(&vcpu_lock);

      exit(0);
}

void *vCPU_task(void *p)
{
      //do for the task that is still going to finish
      do{
      pthread_mutex_lock(&vcpu_mutex);
      //char msg[60];
      //sprintf(msg, "VPCU TASK COMPLETE BY THREAD %ld\n", pthread_self());

      //output_str(msg);
      

      pthread_mutex_unlock(&vcpu_mutex);
      }while(SM->shutdown == 0);
      
      pthread_exit(NULL);
}

void maintenance_manager(shared_memory *SM)
{
      output_str("MAINTENANCE MANAGER WORKING\n");
      while(SM->shutdown==0){}
      output_str("MAINTENANCE MANAGER CLOSED\n");
      exit(0);
}

void sigint_handler(int signum)
{
      output_str("^C PRESSED. CLOSING PROGRAM.\n");
      end_sim();
}
