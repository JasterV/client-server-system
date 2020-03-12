#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "debug.h"

config *CFG;
const char **DB;

int main(int argc, char const *argv[])
{
    const char *cfgname = "server.cfg";
    const char *dbname = "bbdd_dev.dat";
    /*Analitzem els arguments*/
    if (argc > 1)
    {
        const char *option = argv[1];
        if (strcmp(option, "-c") == 0)
        {
            if (argc != 3)
            {
                printf("%s -c <filename>\n", argv[0]);
                exit(EXIT_SUCCESS);
            }
            cfgname = argv[2];
        }
        else if (strcmp(option, "-d") == 0)
            DEBUG_ON = 1;
        else if (strcmp(option, "-u") == 0)
        {
            if (argc != 3)
            {
                printf("%s -u <filename>\n", argv[0]);
                exit(EXIT_SUCCESS);
            }
            dbname = argv[2];
        }
    }
    /*Llegim el fitxer de configuració*/
    if ((CFG = readConfig(cfgname)) == NULL)
    {
        perror("Error llegint el fitxer de configuració.\n");
        exit(EXIT_FAILURE);
    }
    /*Llegim el fitxer de dispositius*/
    if ((DB = (const char **)readDbFile(dbname)) == NULL)
    {
        perror("Error llegint el fitxer de dispositius.\n");
        exit(EXIT_FAILURE);
    }


    
    free(DB);
    free(CFG);
    exit(EXIT_SUCCESS);
}
