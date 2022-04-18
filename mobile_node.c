/*
João Maria Campos Donato 2020217878
José Miguel Norte de Matos 2020217977
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
$ mobile_node {nº pedidos a gerar} {intervalo entre pedidos em ms}
{milhares de instruções de cada pedido} {tempo máximo para execução}
*/

int validateInput(char *s);

int main(int argc, char *argv[])
{
        /* code */
        int numberOfRequests, intervalBetweenRequestsMS, thousandInstructionsPerRequest, maxExecutionTimeS;
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
        // define variables
        numberOfRequests = atoi(argv[1]);
        intervalBetweenRequestsMS = atoi(argv[2]);
        thousandInstructionsPerRequest = atoi(argv[3]);
        maxExecutionTimeS = atoi(argv[4]);

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