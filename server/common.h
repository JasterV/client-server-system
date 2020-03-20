#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ctype.h>
#include <string.h>

#include <signal.h>
#include <pthread.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <sys/ipc.h>
#include <sys/shm.h>
/*--------------TIPUS DE PAQUETS-------------*/

/* Paquets de la fase de registre */
unsigned char REG_REQ = 0x00;
unsigned char REG_INFO = 0x01;
unsigned char REG_ACK = 0x02;
unsigned char INFO_ACK = 0x03;
unsigned char REG_NACK = 0x04;
unsigned char INFO_NACK = 0x05;
unsigned char REG_REJ = 0x06;

/* Paquets per a la comunicació periòdica */
unsigned char ALIVE = 0x10;
unsigned char ALIVE_REJ = 0x11;

/* Paquets per a la transferència de dades amb el servidor */
unsigned char SEND_DATA = 0x20;
unsigned char SET_DATA = 0x21;
unsigned char GET_DATA = 0x22;
unsigned char DATA_ACK = 0x23;
unsigned char DATA_NACK = 0x24;
unsigned char DATA_REJ = 0x25;

/*---------------ESTATS----------------*/
unsigned char DISCONNECTED = 0xa0;
unsigned char NOT_REGISTERED = 0xa1;
unsigned char WAIT_ACK_REG = 0xa2;
unsigned char WAIT_INFO = 0xa3;
unsigned char WAIT_ACK_INFO = 0xa4;
unsigned char REGISTERED = 0xa5;
unsigned char SEND_ALIVE = 0xa6;

/*-------------STRUCTS-------------*/
typedef struct client_info
{
    unsigned char state;
    const char *id;
    const char *rand_num;
    const char *ip;
    const char *elems;
    unsigned short tcp_port;
} client_info;

typedef struct clients_db
{
    client_info *clients;
    int length;
} clients_db;

typedef struct udp_pdu
{
    unsigned char pack;
    const char id[13];
    const char rand_num[9];
    const char data[61];
} udp_pdu;

typedef struct tcp_pdu
{
    unsigned char pack;
    const char id[13];
    const char rand_num[9];
    const char elem[8];
    const char value[16];
    const char data[80];
} tcp_pdu;

typedef struct config
{
    const char *id;
    int udp_port;
    int tcp_port;
} config;

/*---------------DEBUG UTILITIES--------------*/
int DEBUG_ON = 0;

void debugPrint(const char *message)
{
    if (DEBUG_ON)
        printf("%s -> %s\n", __TIME__, message);
}

void check(int result, const char *message)
{
    if (result < 0)
    {
        perror(message);
        exit(EXIT_FAILURE);
    }
}

/*-----------READING UTILITIES------------*/
char *getLine(FILE *fp)
{
    size_t len = 3;
    char *line = (char *)malloc(len * sizeof(char));
    int c = fgetc(fp);
    int i = 0;
    while (c != '\n' && c != EOF)
    {
        line[i] = c;
        i++;
        if (i == len)
        {
            len++;
            line = (char *)realloc(line, len * sizeof(char));
        }
        c = fgetc(fp);
    }
    if (i == 0)
    {
        free(line);
        return NULL;
    }
    line[i] = '\0';
    return line;
}

char *trim(char *str)
{
    char *end;
    /*left trim*/
    while (isspace((unsigned char)*str))
        str++;
    if (*str == 0) /*Es tot espais*/
        return str;
    /*Right trim*/
    end = str + strlen(str) - 1;
    while (end > str && (isspace((unsigned char)*end) || (unsigned char)*end == '\n'))
        end--;
    end[1] = '\0';
    return str;
}

char *getCfgLineInfo(FILE *fp)
{
    char *line = getLine(fp);
    if (line == NULL)
        return NULL;
    char *tmp = strrchr(line, '=');
    tmp++;
    char *info = trim(tmp);
    if (strlen(info) <= 0)
        return NULL;
    return info;
}
