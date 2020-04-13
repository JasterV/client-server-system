#include "utils.h"

void handler(int sig);
void attendRegisters(int socket);
void *handlePdu(void *args);
void registerClient(int sock, udp_pdu pdu, struct sockaddr_in address, int clientIndex);
void waitInfo(int sock, int clientIndex);
void handleClientInfo(int sock, udp_pdu info, struct sockaddr_in clientAddress, int clientIndex);
int validInfo(const char *id, const char *randNum, int tcpPort, char *elems, client_info client);
int validElems(char *elems);
void handleAlive(int sock, udp_pdu pdu, struct sockaddr_in clientAddress, int clientIndex);
int validAlive(udp_pdu pdu, client_info client);
void controlAlives();
void tcpConnections(int tcpSocket);
void *handleTcpConnection(void *args);
int validCredentials(tcp_pdu pdu, client_info client);

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
    pid_t regAtt, alives, waitConn, cli;
    if ((regAtt = fork()) == 0)
        attendRegisters(udpSocket);
    if ((alives = fork()) == 0)
        controlAlives();
    if ((waitConn = fork()) == 0)
        tcpConnections(tcpSocket);
    if ((cli = fork()) == 0)
    {
        while (1)
            ;
    }
    if (cli == -1 || waitConn == -1 || alives == -1 || regAtt == -1)
    {
        perror("Error realitzant un fork al procés principal\n");
        kill(-getpid(), SIGINT);
    }
    /* Si qualsevol procés fill acaba
       envia una señal SIGINT a tots els 
       processos */
    wait(NULL);
    printf("Un procés ha mort\n");
    kill(-getpid(), SIGINT);
}

/*--------------------------------------------------*/
/*-------------- HANDLE TCP CONNECTIONS ------------*/
/*--------------------------------------------------*/
void tcpConnections(int tcpSocket)
{
    check(listen(tcpSocket, SERVER_BACKLOG), "Error escoltant per el port tcp\n");
    while (1)
    {
        int clientSocket;
        pthread_t newThread;
        check((clientSocket = accept(tcpSocket, NULL, NULL)), "Ha hagut un error al acceptar un socket, el màxim permes està definit a la llibreria 'utils.h'\n");
        check(pthread_create(&newThread, NULL, handleTcpConnection, &clientSocket), "Error creating a thread");
        check(pthread_detach(newThread), "Error detaching a thread");
    }
}
void *handleTcpConnection(void *args)
{
    int clientSocket = *((int *)args);
    fd_set inputs;
    check(selectIn(clientSocket, &inputs, TCP_WAIT_TIME), "Error en el select\n");
    if (FD_ISSET(clientSocket, &inputs))
    {
        int clientIndex;
        tcp_pdu pdu;
        check(recv(clientSocket, &pdu, sizeof(tcp_pdu), 0), "Error rebent informació del client\n");
        if (pdu.pack == SEND_DATA)
        {
            if ((clientIndex = isAuthorized(&cdb, pdu.id)) != -1 && validCredentials(pdu, cdb.clients[clientIndex]))
            {
                client_info client = cdb.clients[clientIndex];
                if (client.state == SEND_ALIVE)
                {
                    if (hasElem(pdu.elem, client))
                    {

                        check(sendTcp(clientSocket, DATA_ACK, cfg.id, client.randNum, pdu.elem, pdu.value, client.id), "Error en l'enviament de DATA_ACK\n");
                    }
                    else
                        check(sendTcp(clientSocket, DATA_NACK, cfg.id, client.randNum, pdu.elem, pdu.value, "L'element no es troba en el dispositiu"), "Error enviant DATA_NACK\n");
                }
                else
                    disconnectClient(&cdb, clientIndex);
            }
            else
            {
                check(sendTcp(clientSocket, DATA_REJ, cfg.id, pdu.randNum, pdu.elem, pdu.value, ""), "Error enviant DATA_REJ\n");
                disconnectClient(&cdb, clientIndex);
            }
        }
    }
    close(clientSocket);
    return NULL;
}

int validCredentials(tcp_pdu pdu, client_info client)
{
    return strcmp(pdu.randNum, client.randNum) == 0;
}

/*--------------------------------------------------*/
/*-------------- CONTROL ALIVE CLIENTS -------------*/
/*--------------------------------------------------*/
void controlAlives()
{
    while (1)
    {
        time_t now = time(NULL);
        for (int i = 0; i < cdb.length; i++)
        {
            if (cdb.clients[i].lastAlive != -1)
            {
                time_t diff = difftime(now, cdb.clients[i].lastAlive);
                if (cdb.clients[i].state == REGISTERED)
                {
                    if (diff >= 3)
                    {
                        char debugMessage[60] = {'\0'};
                        sprintf(debugMessage, "No s'ha rebut el primer ALIVE del client %s", cdb.clients[i].id);
                        debugPrint(debugMessage);
                        disconnectClient(&cdb, i);
                    }
                }
                else if (cdb.clients[i].state == SEND_ALIVE)
                {
                    if (diff >= 6)
                    {
                        char debugMessage[60] = {'\0'};
                        sprintf(debugMessage, "El client %s no ha enviat 3 ALIVES consecutius", cdb.clients[i].id);
                        debugPrint(debugMessage);
                        disconnectClient(&cdb, i);
                    }
                }
            }
        }
    }
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
        check(sendUdp(attSocket, REG_REJ, cfg.id, "00000000", "Dispositiu no autoritzat en el sistema", clientAddress), "Error enviant REG_REJ\n");
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
        check(sendUdp(sock, REG_REJ, cfg.id, "00000000", "Dades incorrectes", address), "Error enviant REG_REJ\n");
        return;
    }
    if (cdb.clients[clientIndex].state != DISCONNECTED)
    {
        disconnectClient(&cdb, clientIndex);
        return;
    }
    char randNum[9] = {'\0'}, data[61] = {'\0'}, debugMessage[60] = {'\0'};
    int newSocket, port;
    struct sockaddr_in newAddress;
    /* GENEREM NOMBRE ALEATORI I OBRIM UN NOU PORT */
    sprintf(randNum, "%d", generateRandNum(10000000, 99999999));
    check((newSocket = socket(AF_INET, SOCK_DGRAM, 0)), "Error al connectar el socket udp.\n");
    check(bindTo(newSocket, 0, &newAddress), "Error al bind del udpSocket");
    check((port = getPort(newSocket)), "Error agafant port\n");
    /* ENVIEM REG_ACK */
    sprintf(data, "%d", port);
    check(sendUdp(newSocket, REG_ACK, cfg.id, randNum, data, address), "Error enviant REG_ACK\n");
    cdb.clients[clientIndex].state = WAIT_INFO;
    strcpy(cdb.clients[clientIndex].randNum, randNum);
    /* COMENCEM PROCÉS INFO */
    sprintf(debugMessage, "Client %s pasa a l'estat WAIT_INFO", cdb.clients[clientIndex].id);
    debugPrint(debugMessage);
    waitInfo(newSocket, clientIndex);
    close(newSocket);
}

void waitInfo(int sock, int clientIndex)
{
    fd_set inputs;
    check(selectIn(sock, &inputs, INFO_WAIT_TIME), "Error en el select\n");
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
        char tcp_port[6], debugMessage[60] = {'\0'};
        sprintf(tcp_port, "%d", cfg.tcpPort);
        check(sendUdp(sock, INFO_ACK, cfg.id, cdb.clients[clientIndex].randNum, tcp_port, clientAddress), "Error enviant INFO_NACK\n");
        cdb.clients[clientIndex].tcpPort = tcpPort;
        strcpy(cdb.clients[clientIndex].elems, elems);
        cdb.clients[clientIndex].lastAlive = time(NULL);
        cdb.clients[clientIndex].state = REGISTERED;
        sprintf(debugMessage, "Client %s pasa a l'estat REGISTERED", cdb.clients[clientIndex].id);
        debugPrint(debugMessage);
    }
    else
    {
        check(sendUdp(sock, INFO_NACK, cfg.id, cdb.clients[clientIndex].randNum, "Dades incorrectes\n", clientAddress), "Error enviant INFO_NACK\n");
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
            cdb.clients[clientIndex].lastAlive = time(NULL);
            check(sendUdp(sock, ALIVE, cfg.id, cdb.clients[clientIndex].randNum, cdb.clients[clientIndex].id, clientAddress), "Error enviant ALIVE\n");
            if (cdb.clients[clientIndex].state == REGISTERED)
            {
                char debugMessage[60] = {'\0'};
                sprintf(debugMessage, "Client %s pasa a l'estat SEND_ALIVE", cdb.clients[clientIndex].id);
                debugPrint(debugMessage);
                cdb.clients[clientIndex].state = SEND_ALIVE;
            }
        }
        else
        {
            check(sendUdp(sock, ALIVE_REJ, cfg.id, cdb.clients[clientIndex].randNum, "Dades del paquet ALIVE incorrectes\n", clientAddress), "Error enviant ALIVE_REJ\n");
            disconnectClient(&cdb, clientIndex);
        }
    }
    else
        disconnectClient(&cdb, clientIndex);
}

/*-------------------------------------------------*/
/*-----------------AUXILIAR FUNCTIONS--------------*/
/*-------------------------------------------------*/

void handler(int sig)
{
    while (wait(NULL) > 0)
        ;
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
