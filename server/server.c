#include "common.h"

void handler(int sig);
void analizeArgs(int argc, char const *argv[], const char *cfgname, const char *dbname);

int readConfig(config *cfg, const char *filename);
int readDb(clients_db *db, const char *filename);
int shareDb();
int shareClientsInfo();

void disconnectClient(int index);

int sendRegRej(int socket, const char *data, struct sockaddr_in client_address);
int sendRegAck(int socket, struct sockaddr_in client_address, char rand_num[9], int udp_port);
int sendInfoNack(int socket, char rand_num[9], struct sockaddr_in client_address);
int sendInfoAck(int socket, char rand_num[9], struct sockaddr_in client_address);

void *attendReg(void *arg);
void attendClient(int sock, udp_pdu client_req, struct sockaddr_in client_address, int client_index);
void getInfoFromData(char *data, char *tcp_port, char *elems);
int isAuthorized(const char *id);

config cfg;
clients_db *cdb;
char debugMessage[100];

int main(int argc, char const *argv[])
{
    signal(SIGTSTP, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGINT, handler);
    signal(SIGCHLD, SIG_IGN);

    int udp_socket;
    int tcp_socket;
    struct sockaddr_in tcp_address;
    struct sockaddr_in udp_address;

    shareDb(); /*Compartim la estructura clients_db en memoria*/
    const char *cfgname = "server.cfg", *dbname = "bbdd_dev.dat";
    if (argc > 1)
        analizeArgs(argc, argv, cfgname, dbname);
    check(readConfig(&cfg, cfgname),
          "Error llegint el fitxer de configuració.\n");
    check(readDb(cdb, dbname),
          "Error llegint el fitxer de dispositius.\n");
    /*Un cop es sap quants dispositius estan registrats 
     a la base de dades, compartim la estructura clients_info en memoria*/
    shareClientsInfo();
    /* CREACIÓ DEL SOCKET UDP*/
    check((udp_socket = socket(AF_INET, SOCK_DGRAM, 0)), "Error al connectar el socket udp.\n");
    check(bindToPort(udp_socket, &udp_address, cfg.udp_port), "Error al fer el bind del socket udp.\n");
    /*CREACIÓ DEL SOCKET TCP*/
    check((tcp_socket = socket(AF_INET, SOCK_STREAM, 0)), "Error al connectar el socket tcp.\n");
    check(bindToPort(tcp_socket, &tcp_address, cfg.tcp_port), "Error al fer el bind del socket tcp.\n");
    check(listen(tcp_socket, 100), "Error listening.\n");

    pthread_t attendRegisters;
    /*pthread_t waitTcpConnections;*/
    /*pthread_t cli;*/
    pthread_create(&attendRegisters, NULL, attendReg, &udp_socket);
    pthread_join(attendRegisters, NULL);

    exit(EXIT_SUCCESS);
}

void handler(int sig)
{
    exit(EXIT_SUCCESS);
}

void analizeArgs(int argc, char const *argv[], const char *cfgname, const char *dbname)
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

/*-------------------------------------------------------*/
/*------------THREADS I OPERACIONS DEL SERVIDOR----------*/
/*-------------------------------------------------------*/
void *attendReg(void *arg)
{
    int udp_socket = *(int *)arg;
    udp_pdu pdu;
    struct sockaddr_in client_address;
    socklen_t len = sizeof(struct sockaddr_in);
    pid_t pid;
    while (1)
    {
        int client_index;
        check(recvfrom(udp_socket, &pdu, sizeof(udp_pdu), 0, (struct sockaddr *)&client_address, &len), "Error rebent informació");
        if ((client_index = isAuthorized(pdu.id)) < 0)
        {
            check(sendRegRej(udp_socket, "Client no registrat a la base de dades\n", client_address), "Error enviar REG_REJ");
            disconnectClient(client_index);
        }
        else
        {
            if (pdu.pack == ALIVE)
            {
                if ((pid = fork()) == 0)
                    sendAlive(udp_socket, pdu, client_address, client_index);
                else if (pid == -1)
                {
                    perror("Error creant un nou procés.\n");
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                if ((pid = fork()) == 0)
                    attendClient(udp_socket, pdu, client_address, client_index);
                else if (pid == -1)
                {
                    perror("Error creant un nou procés.\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
    return NULL;
}

void disconnectClient(int index)
{
    cdb->clients[index].elems = NULL;
    cdb->clients[index].ip = NULL;
    cdb->clients[index].rand_num = NULL;
    cdb->clients[index].tcp_port = 0;
    cdb->clients[index].state = DISCONNECTED;
}

void attendClient(int sock, udp_pdu client_req, struct sockaddr_in client_address, int client_index)
{
    /* COMPROVEM SI LES DADES REBUDES SON CORRECTES I EL CLIENT ESTÀ AUTORITZAT */
    if ((strcmp("00000000", client_req.rand_num) != 0 ||
         strcmp("", client_req.data) != 0))
    {
        check(sendRegRej(sock, "Dades incorrectes\n", client_address), "Error enviar REG_REJ");
        disconnectClient(client_index);
        memset(debugMessage, 0, sizeof(debugMessage));
        sprintf(debugMessage, "Client %s passa a l'estat DISCONNECTED\n", cdb->clients[client_index].id);
        debugPrint(debugMessage);
        exit(0);
    }
    if (cdb->clients[client_index].state != DISCONNECTED)
    {
        disconnectClient(client_index);
        perror("Bad client state\n");
        exit(0);
    }
    /* OBRIM UN NOU PORT UDP, GENEREM NOMBRE ALEATORI I ENVIEM REG_ACK */
    int udp_port;
    char rand_num[9];
    struct sockaddr_in new_address;
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    check((udp_port = bindAndGetFreePort(udp_sock, &new_address)), "Error binding\n");
    int num = generateRandNum(10000000, 99999999);
    sprintf(rand_num, "%d", num);
    check(sendRegAck(sock, client_address, rand_num, udp_port), "Error sending REG_ACK\n");

    /* GUARDEM LES DADES DEL CLIENT */
    cdb->clients[client_index].rand_num = rand_num;
    cdb->clients[client_index].state = WAIT_INFO;
    cdb->clients[client_index].ip = inet_ntoa(client_address.sin_addr);
    memset(debugMessage, 0, sizeof(debugMessage));
    sprintf(debugMessage, "Client %s passa a l'estat WAIT INFO\n", cdb->clients[client_index].id);
    debugPrint(debugMessage);

    /* ESPEREM A REBRE EL PAQUET REG_INFO PER EL NOU PORT UDP */
    fd_set inputs;
    struct timeval timeout;
    int sret;
    FD_ZERO(&inputs);
    FD_SET(udp_sock, &inputs);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    check((sret = select(udp_sock + 1, &inputs, NULL, NULL, &timeout)), "Error al select\n");
    if (!FD_ISSET(udp_sock, &inputs))
    {
        disconnectClient(client_index);
        memset(debugMessage, 0, sizeof(debugMessage));
        printf("Al select\n");
        sprintf(debugMessage, "Client %s passa a l'estat DISCONNECTED\n", cdb->clients[client_index].id);
        debugPrint(debugMessage);
        exit(0);
    }

    /* REBEM EL PAQUET I ENVIEM RESPOSTA*/
    udp_pdu info;
    struct sockaddr_in info_address;
    socklen_t len = sizeof(struct sockaddr_in);
    check(recvfrom(udp_sock, &info, sizeof(udp_pdu), 0, (struct sockaddr *)&info_address, &len), "Error rebent el paquet REG_INFO");
    if (info.pack != REG_INFO ||
        strcmp(info.id, cdb->clients[client_index].id) != 0 ||
        strcmp(info.rand_num, cdb->clients[client_index].rand_num) != 0 ||
        strcmp(inet_ntoa(info_address.sin_addr), cdb->clients[client_index].ip) != 0)
    {
        disconnectClient(client_index);
        check(sendInfoNack(udp_sock, rand_num, client_address), "Error enviant INFO_NACK");
        exit(0);
    }
    check(sendInfoAck(udp_sock, rand_num, client_address), "Error enviant INFO_ACK");
    cdb->clients[client_index].state = REGISTERED;
    memset(debugMessage, 0, sizeof(debugMessage));
    sprintf(debugMessage, "Client %s passa a l'estat REGISTERED\n", cdb->clients[client_index].id);
    debugPrint(debugMessage);

    char *elems = NULL;
    char *client_tcp_port = NULL;
    getInfoFromData(info.data, client_tcp_port, elems);
    cdb->clients[client_index].elems = elems;
    cdb->clients[client_index].tcp_port = atoi(client_tcp_port);
    exit(0);
}

void getInfoFromData(char *data, char *tcp_port, char *elems)
{
    char *token;
    const char del[2] = ",";
    token = strtok(data, del);
    tcp_port = token;
    token = strtok(NULL, del);
    elems = token;
}

int sendRegAck(int socket, struct sockaddr_in client_address, char rand_num[9], int udp_port)
{
    udp_pdu response;
    char data[61];
    sprintf(data, "%d", udp_port);
    response.pack = REG_ACK;
    strcpy(response.id, cfg.id);
    strcpy(response.rand_num, rand_num);
    strcpy(response.data, data);
    return sendto(socket, &response, sizeof(udp_pdu), 0, (struct sockaddr *)&client_address, sizeof(client_address));
}

int sendRegRej(int socket, const char *data, struct sockaddr_in client_address)
{
    udp_pdu response;
    response.pack = REG_REJ;
    strcpy(response.id, cfg.id);
    strcpy(response.rand_num, "00000000");
    strcpy(response.data, data);
    return sendto(socket, &response, sizeof(udp_pdu), 0, (struct sockaddr *)&client_address, sizeof(client_address));
}

int sendInfoAck(int socket, char rand_num[9], struct sockaddr_in client_address)
{
    udp_pdu response;
    char data[61];
    sprintf(data, "%d", cfg.tcp_port);
    response.pack = INFO_ACK;
    strcpy(response.id, cfg.id);
    strcpy(response.rand_num, rand_num);
    strcpy(response.data, data);
    return sendto(socket, &response, sizeof(udp_pdu), 0, (struct sockaddr *)&client_address, sizeof(client_address));
}

int sendInfoNack(int socket, char rand_num[9], struct sockaddr_in client_address)
{
    udp_pdu response;
    response.pack = INFO_NACK;
    strcpy(response.id, cfg.id);
    strcpy(response.rand_num, rand_num);
    strcpy(response.data, "Dades o paquet incorrectes\n");
    return sendto(socket, &response, sizeof(udp_pdu), 0, (struct sockaddr *)&client_address, sizeof(client_address));
}

int isAuthorized(const char *id)
{
    for (int i = 0; i < cdb->length; i++)
        if (strcmp(id, cdb->clients[i].id) == 0)
            return i;
    return -1;
}

/*-------------------------------------------------------*/
/*------------OPERACIONS DE MEMORIA COMPARTIDA-----------*/
/*-------------------------------------------------------*/
int shareDb()
{
    /*Compartim memoria*/
    int db_shmid;
    check((db_shmid = shmget(IPC_PRIVATE, sizeof(clients_db), IPC_CREAT | 0775)), "Error al compartir memoria\n");
    if ((cdb = (clients_db *)shmat(db_shmid, NULL, 0)) == NULL)
    {
        perror("Error al mapejar memoria\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int shareClientsInfo()
{
    int cl_shmid;
    client_info *tmp;
    check((cl_shmid = shmget(IPC_PRIVATE, cdb->length * sizeof(client_info), IPC_CREAT | 0775)), "Error al compartir memoria\n");
    if ((tmp = (client_info *)shmat(cl_shmid, NULL, 0)) == NULL)
    {
        perror("Error al mapejar memoria\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < cdb->length; i++)
        tmp[i] = cdb->clients[i];
    free(cdb->clients);
    cdb->clients = tmp;
    return 0;
}

/*-------------------------------------------------------*/
/*-----------------CONFIG FILES READING------------------*/
/*-------------------------------------------------------*/
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
    cfg->udp_port = atoi(attrs[1]);
    cfg->tcp_port = atoi(attrs[2]);
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
            clients = realloc(clients, len * sizeof(client_info));
        }
        client_info client;
        memset(&client, 0, sizeof(client_info));
        client.id = line;
        client.state = DISCONNECTED;
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