#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include <ctype.h>
#include <string.h>

#include <signal.h>
#include <pthread.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <time.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>

#define SERVER_BACKLOG 100
#define INFO_WAIT_TIME 2
#define TCP_WAIT_TIME 3
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
    char id[13];
    char randNum[9];
    char elems[50];
    unsigned short tcpPort;
    time_t lastAlive;
} client_info;

typedef struct clients_db
{
    client_info *clients;
    int length;
} clients_db;

typedef struct udp_pdu
{
    unsigned char pack;
    char id[13];
    char randNum[9];
    char data[61];
} udp_pdu;

typedef struct tcp_pdu
{
    unsigned char pack;
    char id[13];
    char randNum[9];
    char elem[8];
    char value[16];
    char data[80];
} tcp_pdu;

typedef struct config
{
    char *id;
    int udpPort;
    int tcpPort;
} config;

typedef struct reg_thread_args
{
    udp_pdu pdu;
    int socket;
    struct sockaddr_in clientAddress;
} reg_thread_args;

int generateRandNum(int min, int max)
{
    srand(time(0));
    return rand() % (max + 1 - min) + min;
}

/*--------------------------------------------*/
/*---------------DEBUG UTILS------------------*/
/*--------------------------------------------*/
int DEBUG_ON = 0;

void debugPrint(const char *message)
{
    if (DEBUG_ON)
    {
        printf("%s => %s\n", __TIME__, message);
        fflush(stdout);
    }
}

void check(int result, const char *message)
{
    if (result < 0)
    {
        perror(message);
        exit(EXIT_FAILURE);
    }
}

/*--------------------------------------------*/
/*---------------DATABASE UTILS---------------*/
/*--------------------------------------------*/

int isAuthorized(clients_db *db, const char *id)
{
    for (int i = 0; i < db->length; i++)
        if (strcmp(id, db->clients[i].id) == 0)
            return i;
    return -1;
}

void disconnectClient(clients_db *db, int index)
{
    char debugMessage[60] = {'\0'};
    sprintf(debugMessage, "Client %s pasa a l'estat DISCONNECTED", db->clients[index].id);
    debugPrint(debugMessage);
    db->clients[index].state = DISCONNECTED;
    memset(&(db->clients[index].elems), '\0', sizeof((db->clients[index].elems)));
    strcpy(db->clients[index].randNum, "00000000");
    db->clients[index].tcpPort = -1;
    db->clients[index].lastAlive = -1;
}

int shareClientsInfo(clients_db *db)
{
    int shmid;
    if ((shmid = shmget(IPC_PRIVATE, db->length * sizeof(client_info), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR)) == -1)
        return -1;
    client_info *tmp;
    if ((tmp = shmat(shmid, (void *)0, 0)) == (void *)-1)
        return -1;
    for (int i = 0; i < db->length; i++)
        tmp[i] = db->clients[i];
    free(db->clients);
    db->clients = tmp;
    return 0;
}

int hasElem(const char *elem, client_info client)
{
    char cpy[50] = {'\0'};
    strcpy(cpy, client.elems);
    char *token = strtok(cpy, ";");
    while (token != NULL)
    {
        if (strcmp(elem, token) == 0)
            return 1;
        token = strtok(NULL, ";");
    }
    return 0;
}

/*--------------------------------------------*/
/*---------------SOCKETS UTILS----------------*/
/*--------------------------------------------*/
int sendUdp(int sock, unsigned char pack, char id[13], char randNum[9], char data[61], struct sockaddr_in address)
{
    udp_pdu pdu;
    pdu.pack = pack;
    strcpy(pdu.id, id);
    strcpy(pdu.randNum, randNum);
    strcpy(pdu.data, data);
    return sendto(sock, &pdu, sizeof(udp_pdu), 0, (struct sockaddr *)&address, sizeof(struct sockaddr_in));
}

int sendTcp(int sock, unsigned char pack, char id[13], char randNum[9], char elem[8], char value[16], char info[80])
{
    tcp_pdu pdu;
    pdu.pack = pack;
    strcpy(pdu.id, id);
    strcpy(pdu.randNum, randNum);
    strcpy(pdu.elem, elem);
    strcpy(pdu.value, value);
    strcpy(pdu.data, info);
    return send(sock, &pdu, sizeof(tcp_pdu), 0);
}

int selectIn(int sock, fd_set *inputs, int timeout)
{
    struct timeval t;
    t.tv_sec = timeout;
    t.tv_usec = 0;
    FD_ZERO(inputs);
    FD_SET(sock, inputs);
    return select(sock + 1, inputs, NULL, NULL, &t);
}

int bindTo(int socket, uint16_t port, struct sockaddr_in *address)
{
    address->sin_family = AF_INET;
    address->sin_port = port;
    address->sin_addr.s_addr = INADDR_ANY;
    return bind(socket, (struct sockaddr *)address, sizeof(struct sockaddr_in));
}

int getPort(int fd)
{
    struct sockaddr_in address;
    socklen_t len = sizeof(struct sockaddr_in);
    int port = 0;
    memset(&address, 0, sizeof(struct sockaddr_in));
    if (getsockname(fd, (struct sockaddr *)&address, &len) == -1)
        return -1;
    port = ntohs(address.sin_port);
    return port;
}

/*----------------------------------------*/
/*-------------READING UTILS--------------*/
/*----------------------------------------*/
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

int readConfig(config *cfg, const char *filename)
{
    char *attrs[3];
    FILE *fp;
    if ((fp = fopen(filename, "r")) == NULL)
        return -1;
    for (int i = 0; i < 3; i++)
    {
        if ((attrs[i] = getCfgLineInfo(fp)) == NULL)
        {
            fclose(fp);
            return -1;
        }
    }
    cfg->id = attrs[0];
    cfg->udpPort = atoi(attrs[1]);
    cfg->tcpPort = atoi(attrs[2]);
    fclose(fp);
    return 0;
}

int readDb(clients_db *db, const char *filename)
{
    FILE *fp;
    if ((fp = fopen(filename, "r")) == NULL)
        return -1;
    client_info *clients = (client_info *)malloc(sizeof(client_info));
    char *line;
    int len = 1, i = 0;
    while ((line = getLine(fp)) != NULL)
    {
        if (i == len)
        {
            len++;
            clients = (client_info *)realloc(clients, len * sizeof(client_info));
        }
        client_info client;
        memset(&client, 0, sizeof(client_info));
        strcpy(client.id, line);
        client.state = DISCONNECTED;
        strcpy(client.randNum, "00000000");
        client.tcpPort = -1;
        client.lastAlive = (time_t)-1;
        clients[i] = client;
        i++;
    }
    if (i == 0)
    {
        fclose(fp);
        free(clients);
        return -1;
    }
    fclose(fp);
    db->clients = clients;
    db->length = len;
    return 0;
}
