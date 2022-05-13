/*
João Maria Campos Donato 2020217878
José Miguel Norte de Matos 2020217977
*/
#include "simulation_structs.h"

#define PIPE_NAME "TASK_PIPE"
#define PIPE_BUF 64

typedef struct
{
        int noOfRequests;
        int intervalBetwRequests;
        int thousInstructPerRequest;
        int maxExecTimeSecs;
} offload;

/*
$ mobile_node {nº pedidos a gerar} {intervalo entre pedidos em ms}
{milhares de instruções de cada pedido} {tempo máximo para execução}
*/

int validateInput(char *s);
void send_request(offload off);

int main(int argc, char *argv[])
{
        /* code */
        offload off;
        if (argc != 5)
        {
                printf("Command format wrong! Should be:\n");
                printf("$ mobile_node {nº pedidos a gerar} {intervalo entre pedidos em ms} {milhares de instruções de cada pedido} {tempo máximo para execução}\n");
                exit(1);
        }

        // verify inputs
        for (int i = 1; i < argc; i++)
        {
                if (validateInput(argv[i]) == 0)
                {
                        exit(1);
                }
        }
        // define the offload request
        off.noOfRequests = atoi(argv[1]);
        off.intervalBetwRequests = atoi(argv[2]);
        off.thousInstructPerRequest = atoi(argv[3]);
        off.maxExecTimeSecs = atoi(argv[4]);

        pid_t offload_process;
        if ((offload_process = fork()) == 0)
        { // send request through task pipe
                send_request(off);
        }
        if (offload_process == -1)
        {

                printf("ERROR: SENDING OFFLOAD TASKS\n");
                exit(1);
        }

        wait(NULL);

        return 0;
}

void send_request(offload off)
{
        // Tarefa: ID tarefa:Nº de instruções (em milhares):Tempo máximo para execução

        int tasks_sent = 0;
        int fd;
        task message;
        message.maxExecTimeSecs = off.maxExecTimeSecs;
        message.thousInstructPerRequest = off.thousInstructPerRequest;
        char message_str[PIPE_BUF];

        if ((fd = open(PIPE_NAME, O_WRONLY)) < 0)
        {
                printf("ERROR: OPENING PIPE FOR WRITING\n");
                exit(0);
        }

        while (1)
        {
                if (tasks_sent == off.noOfRequests)
                        break;
                // message.id = tasks_sent;
                sprintf(message_str, "%d:%d", message.thousInstructPerRequest, message.maxExecTimeSecs);
                if (write(fd, message_str, strlen(message_str)) == -1)
                {
                        printf("ERROR: PIPE DOES NOT EXIST\n");
                        exit(1);
                }
                printf("Sent task number: %d\n", tasks_sent);
                tasks_sent++;
                sleep(off.intervalBetwRequests / 1000);
        }
        close(fd);
        exit(0);
}

int validateInput(char *s)
{
        for (int i = 0; i < strlen(s); i++)
        {
                /* code */
                if (!isdigit(s[i]))
                {
                        printf("Invalid input: %s\n", s);
                        return 0;
                }
        }
        return 1;
}