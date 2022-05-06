/*
João Maria Campos Donato 2020217878
José Miguel Norte de Matos 2020217977
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
void send_requests(offload off);

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
        // define the request
        off.noOfRequests = atoi(argv[1]);
        off.intervalBetwRequests = atoi(argv[2]);
        off.thousInstructPerRequest = atoi(argv[3]);
        off.maxExecTimeSecs = atoi(argv[4]);

        pid_t offload_process;
        if ((offload_process = fork()) == 0)
        { // send requests through task pipe
                send_requests(off);
        }
        if (offload_process == -1)
        {

                output_str("ERROR SENDING OFFLOAD TASKS\n");
                exit(1);
        }

        return 0;
}

void send_requests(offload off)
{
        int requests_sent = 0;
        while (1)
        {
                // check if all requests have been sent, if yes break

                // check if task pipe exists, if not exit

                // send request (with semaphore?)

                // wait- interval between requests
        }
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