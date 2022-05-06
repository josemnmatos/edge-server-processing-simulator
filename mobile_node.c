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
} request;

/*
$ mobile_node {nº pedidos a gerar} {intervalo entre pedidos em ms}
{milhares de instruções de cada pedido} {tempo máximo para execução}
*/

int validateInput(char *s);

int main(int argc, char *argv[])
{
        /* code */
        request req;
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
        // define o request
        req.noOfRequests = atoi(argv[1]);
        req.intervalBetwRequests = atoi(argv[2]);
        req.thousInstructPerRequest = atoi(argv[3]);
        req.maxExecTimeSecs = atoi(argv[4]);
        // escrever o request no task pipe

        return 0;
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