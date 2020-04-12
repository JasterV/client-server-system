#include "utils.h"

void handler(int sig);
/* REGISTRATION FUNCTIONS */
void attendRegisters(int socket);
void *handlePdu(void *args);
void registerClient(int sock, udp_pdu pdu, struct sockaddr_in address, int clientIndex);
void waitInfo(int sock, int clientIndex);
void handleClientInfo(int sock, udp_pdu info, struct sockaddr_in clientAddress, int clientIndex);
int validInfo(const char *id, const char *randNum, int tcpPort, char *elems, client_info client);
int validElems(char *elems);
/* SEND ALIVE FUNCTIONS */
void handleAlive(int sock, udp_pdu pdu, struct sockaddr_in clientAddress, int clientIndex);
int validAlive(udp_pdu pdu, client_info client);

config cfg;     /* Configuració del servidor */
clients_db cdb; /* Base de dades */

int main(int argc, char const *argv[])
{
    signal(SIGINT, handler);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    memset(&cfg, 0, sizeof(config));
    memset(&cdb, 0, sizeof(clients_db));
    const char *cfgname = "server.cfg";
    const char *dbname = "bbdd_dev.dat";
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
    check(readConfig(&cfg, cfgname),
          "Error llegint el fitxer de configuració.\n");
    check(readDb(&cdb, dbname),
          "Error llegint el fitxer de dispositius.\n");
    check(shareClientsInfo(&cdb), "Error compartint memoria\n");

    int udpSocket, tcpSocket;
    struct sockaddr_in tcpAddress, udpAddress;
    check((udpSocket = socket(AF_INET, SOCK_DGRAM, 0)), "Error al connectar el socket udp.\n");
    check(bindTo(udpSocket, htons(cfg.udpPort), &udpAddress), "Error al bind del udpSocket");
    check((tcpSocket = socket(AF_INET, SOCK_STREAM, 0)), "Error al connectar el socket tcp.\n");
    check(bindTo(tcpSocket, htons(cfg.tcpPort), &tcpAddress), "Error al bind del tcpSocket");

    pid_t regAtt, waitConn, cli;
    if ((regAtt = fork()) == 0)
    {
        printf("Estic atenent registres iuju\n");
        attendRegisters(udpSocket);
        exit(EXIT_SUCCESS);
    }
    else if (regAtt == -1)
    {
        perror("Error creant el procés de registres");
        exit(EXIT_FAILURE);
    }
    if ((waitConn = fork()) == 0)
    {
        printf("Estic esperant connexions wii\n");
        fflush(stdout);
        while (1)
            ;
        exit(EXIT_SUCCESS);
    }
    else if (waitConn == -1)
    {
        perror("Error creant el procés de connexions tcp");
        fflush(stdout);
        kill(regAtt, SIGINT);
        wait(NULL);
        exit(EXIT_FAILURE);
    }
    if ((cli = fork()) == 0)
    {
        printf("He obert la cli\n");
        fflush(stdout);
        while (1)
            ;
        exit(EXIT_SUCCESS);
    }
    else if (cli == -1)
    {
        perror("Error creant el procés de la consola de comandes");
        kill(regAtt, SIGINT);
        wait(NULL);
        kill(waitConn, SIGINT);
        wait(NULL);
        exit(EXIT_FAILURE);
    }
    /* Si qualsevol procés fill acaba
       envia una señal SIGINT a tots els 
       processos */
    wait(NULL);
    printf("Un procés ha mort\n");
    kill(-getpid(), SIGINT);
}

/*--------------------------------------------------*/
/*----------------- ATTEND CLIENTS -----------------*/
/*--------------------------------------------------*/
void attendRegisters(int socket)
{
    reg_thread_args args;
    socklen_t len = sizeof(struct sockaddr_in);
    args.socket = socket;
    while (1)
    {
        pthread_t newThread;
        check(recvfrom(socket, &(args.pdu), sizeof(udp_pdu), 0, (struct sockaddr *)&(args.clientAddress), &len), "Error rebent informació del socket");
        check(pthread_create(&newThread, NULL, handlePdu, &args), "Error creating a thread");
        check(pthread_detach(newThread), "Error detaching a thread");
    }
}

void *handlePdu(void *args)
{
    reg_thread_args reg_args = *((reg_thread_args *)args);
    udp_pdu pdu = reg_args.pdu;
    int attSocket = reg_args.socket;
    struct sockaddr_in clientAddress = reg_args.clientAddress;
    int clientIndex;
    if ((clientIndex = isAuthorized(&cdb, pdu.id)) > -1)
    {
        if (pdu.pack == REG_REQ)
            registerClient(attSocket, pdu, clientAddress, clientIndex);
        else if (pdu.pack == ALIVE)
            handleAlive(attSocket, pdu, clientAddress, clientIndex);
    }
    else
        check(sendPduTo(attSocket, REG_REJ, cfg.id, "00000000", "Dispositiu no autoritzat en el sistema", clientAddress), "Error enviant REG_REJ\n");
    return NULL;
}

/*--------------------------------------------------*/
/*----------------- REGISTER CLIENTS ---------------*/
/*--------------------------------------------------*/
void registerClient(int sock, udp_pdu pdu, struct sockaddr_in address, int clientIndex)
{
    /* COMPROVEM LES DADES DE LA PDU I DEL CLIENT */
    if (strcmp(pdu.randNum, "00000000") != 0 || strcmp(pdu.data, "") != 0)
    {
        disconnectClient(&cdb, clientIndex);
        check(sendPduTo(sock, REG_REJ, cfg.id, "00000000", "Dades incorrectes", address), "Error enviant REG_REJ\n");
        return;
    }
    if (cdb.clients[clientIndex].state != DISCONNECTED)
    {
        disconnectClient(&cdb, clientIndex);
        return;
    }
    char randNum[9] = {'\0'}, data[61] = {'\0'};
    int newSocket, port;
    struct sockaddr_in newAddress;
    /* GENEREM NOMBRE ALEATORI I OBRIM UN NOU PORT */
    sprintf(randNum, "%d", generateRandNum(10000000, 99999999));
    check((newSocket = socket(AF_INET, SOCK_DGRAM, 0)), "Error al connectar el socket udp.\n");
    check(bindTo(newSocket, 0, &newAddress), "Error al bind del udpSocket");
    check((port = getPort(newSocket)), "Error agafant port\n");
    /* ENVIEM REG_ACK */
    sprintf(data, "%d", port);
    check(sendPduTo(newSocket, REG_ACK, cfg.id, randNum, data, address), "Error enviant REG_ACK\n");
    cdb.clients[clientIndex].state = WAIT_INFO;
    strcpy(cdb.clients[clientIndex].randNum, randNum);
    waitInfo(newSocket, clientIndex);
    close(newSocket);
}

void waitInfo(int sock, int clientIndex)
{
    struct timeval timeout;
    timeout.tv_sec = INFO_WAIT_TIME;
    timeout.tv_usec = 0;
    fd_set inputs;
    FD_ZERO(&inputs);
    FD_SET(sock, &inputs);
    check(select(sock + 1, &inputs, NULL, NULL, &timeout), "Error realitzant el select");
    if (FD_ISSET(sock, &inputs))
    {
        udp_pdu info;
        struct sockaddr_in infoAddress;
        socklen_t len = sizeof(struct sockaddr_in);
        memset(&info, 0, sizeof(udp_pdu));
        check(recvfrom(sock, &info, sizeof(udp_pdu), 0, (struct sockaddr *)&infoAddress, &len), "Error al rebre l'informació");
        if (info.pack == REG_INFO)
            handleClientInfo(sock, info, infoAddress, clientIndex);
        else
            disconnectClient(&cdb, clientIndex);
    }
    else
        disconnectClient(&cdb, clientIndex);
}

void handleClientInfo(int sock, udp_pdu info, struct sockaddr_in clientAddress, int clientIndex)
{
    int tcpPort = atoi(strtok(info.data, ","));
    char *elems = strtok(NULL, ",");
    if (validInfo(info.id, info.randNum, tcpPort, elems, cdb.clients[clientIndex]))
    {
        char tcp_port[6];
        sprintf(tcp_port, "%d", cfg.tcpPort);
        check(sendPduTo(sock, INFO_ACK, cfg.id, cdb.clients[clientIndex].randNum, tcp_port, clientAddress), "Error enviant INFO_NACK\n");
        cdb.clients[clientIndex].tcpPort = tcpPort;
        strcpy(cdb.clients[clientIndex].elems, elems);
        cdb.clients[clientIndex].state = REGISTERED;
    }
    else
    {
        check(sendPduTo(sock, INFO_NACK, cfg.id, cdb.clients[clientIndex].randNum, "Dades incorrectes\n", clientAddress), "Error enviant INFO_NACK\n");
        disconnectClient(&cdb, clientIndex);
    }
}

/*--------------------------------------------------*/
/*----------------- SEND ALIVES --------------------*/
/*--------------------------------------------------*/
void handleAlive(int sock, udp_pdu pdu, struct sockaddr_in clientAddress, int clientIndex)
{
    if (cdb.clients[clientIndex].state == REGISTERED || cdb.clients[clientIndex].state == SEND_ALIVE)
    {
        if (validAlive(pdu, cdb.clients[clientIndex]))
        {
            check(sendPduTo(sock, ALIVE, cfg.id, cdb.clients[clientIndex].randNum, cdb.clients[clientIndex].id, clientAddress), "Error enviant ALIVE\n");
            if (cdb.clients[clientIndex].state == REGISTERED)
                cdb.clients[clientIndex].state = SEND_ALIVE;
        }
        else
        {
            check(sendPduTo(sock, ALIVE_REJ, cfg.id, cdb.clients[clientIndex].randNum, "Dades del paquet ALIVE incorrectes\n", clientAddress), "Error enviant ALIVE_REJ\n");
            disconnectClient(&cdb, clientIndex);
        }
    }
    else
    {
        check(sendPduTo(sock, ALIVE_REJ, cfg.id, cdb.clients[clientIndex].randNum, "El client no es troba en l'estat correcte\n", clientAddress), "Error enviant ALIVE_REJ\n");
        disconnectClient(&cdb, clientIndex);
    }
}

/*-------------------------------------------------*/
/*-----------------AUXILIAR FUNCTIONS--------------*/
/*-------------------------------------------------*/

void handler(int sig)
{
    while (wait(NULL) > 0)
        ;
    printf("procés %d mor\n", getpid());
    shmdt(cdb.clients);
    exit(EXIT_SUCCESS);
}

int validAlive(udp_pdu pdu, client_info client)
{
    return strcmp(pdu.id, client.id) == 0 && strcmp(pdu.randNum, client.randNum) == 0 && strcmp(pdu.data, "") == 0;
}

int validInfo(const char *id, const char *randNum, int tcpPort, char *elems, client_info client)
{
    return strcmp(id, client.id) == 0 &&
           strcmp(randNum, client.randNum) == 0 &&
           tcpPort >= 1024 &&
           tcpPort <= 65535 &&
           validElems(elems);
}

int validElems(char *elems)
{
    return strlen(elems) > 0;
}
