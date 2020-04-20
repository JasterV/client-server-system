#include "utils.h"

void handler(int sig);
/* ATTEND CLIENT FUNCTIONS */
void attendClients(int socket);
void *handlePdu(void *args);
/* REGISTRATION FUNCTIONS */
void registerClient(int sock, udp_pdu pdu, struct sockaddr_in address, client_info *client);
void waitInfo(int sock, client_info *client);
void handleClientInfo(int sock, udp_pdu info, struct sockaddr_in clientAddress, client_info *client);
/* ALIVES FUNCTIONS */
void handleAlive(int sock, udp_pdu pdu, struct sockaddr_in clientAddress, client_info *client);
void controlAlives();
/* TCP CONNECTIONS FUNCTIONS*/
void tcpConnections(int tcpSocket);
void *handleTcpConnection(void *args);
/* COMMAND LINE INTERFACE FUNCTIONS */
void startCli();
void listClients();
void runConnection(unsigned char pack, char *clientId, char *elemId, char *newValue);
/* AUXILIAR FUNCTIONS */
int storeData(const char *pack, const char *clientId, const char *elem, const char *value);
int isInputElem(const char *elemId);
int validElems(char *elems);
int validCredentials(tcp_pdu pdu, client_info *client);
int validAlive(udp_pdu pdu, client_info *client);
int validInfo(const char *id, const char *randNum, int tcpPort, char *elems, client_info *client);

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
    check((udpSocket = socket(AF_INET, SOCK_DGRAM, 0)), "Error al connectar el socket udp.\n");
    check(bindTo(udpSocket, cfg.udpPort), "Error al bind del udpSocket");
    check((tcpSocket = socket(AF_INET, SOCK_STREAM, 0)), "Error al connectar el socket tcp.\n");
    check(bindTo(tcpSocket, cfg.tcpPort), "Error al bind del tcpSocket");
    pid_t regAtt, alives, waitConn, cli;
    if ((regAtt = fork()) == 0)
        attendClients(udpSocket);
    if ((alives = fork()) == 0)
        controlAlives();
    if ((waitConn = fork()) == 0)
        tcpConnections(tcpSocket);
    if ((cli = fork()) == 0)
        startCli();
    if (cli == -1 || waitConn == -1 || alives == -1 || regAtt == -1)
    {
        perror("Error realitzant un fork al procés principal\n");
        kill(-getpid(), SIGINT);
    }
    /* Si qualsevol procés fill acaba
       envia una señal SIGINT a tots els
       processos */
    wait(NULL);
    kill(-getpid(), SIGINT);
}

/*--------------------------------------------------*/
/*--------------------- CLI ------------------------*/
/*--------------------------------------------------*/

void startCli()
/* Simula l'execució de la consola de comandes */
{
    while (1)
    {
        char input[100] = {'\0'};
        char *command;
        fgets(input, sizeof(input), stdin);
        command = strtok(input, " \n");
        if (command != NULL)
        {
            if (strcmp(command, "list") == 0)
            {
                listClients();
            }
            else if (strcmp(command, "quit") == 0)
                exit(EXIT_SUCCESS);
            else if (strcmp(command, "set") == 0)
            {
                char *clientId = strtok(NULL, " \n");
                char *elemId = strtok(NULL, " \n");
                char *newValue = strtok(NULL, " \n");
                if (clientId == NULL || elemId == NULL || newValue == NULL || strtok(NULL, " \n") != NULL)
                    printf("set <identificador_dispositiu> <identificador_element> <nou_valor>\n");
                else
                    runConnection(SET_DATA, clientId, elemId, newValue);
            }
            else if (strcmp(command, "get") == 0)
            {
                char *clientId = strtok(NULL, " \n");
                char *elemId = strtok(NULL, " \n");
                if (clientId == NULL || elemId == NULL || strtok(NULL, " \n") != NULL)
                    printf("get <identificador_dispositiu> <identificador_element>\n");
                else
                    runConnection(GET_DATA, clientId, elemId, "");
            }
            else
            {
                printf("Comanda %s no reconeguda\n", command);
            }
        }
    }
}

void listClients()
{
    printf("\n-----Id----- --RNDM-- ------ IP ----- ----ESTAT--- --ELEMENTS-------------------------------------------\n");
    for (int i = 0; i < cdb.length; i++)
    {
        client_info client = cdb.clients[i];
        const char *state = client.state == DISCONNECTED ? "DISCONNECTED" : client.state == WAIT_INFO ? "WAIT_INFO" : client.state == SEND_ALIVE ? "SEND_ALIVE" : "\0";
        printf("%-12s %-8s %-15s %-12s %-50s\n", client.id, client.randNum, client.ip, state, client.elems);
    }
    printf("\n");
}

void runConnection(unsigned char pack, char *clientId, char *elemId, char *value)
{
    int clientIndex, sock;
    if ((clientIndex = isAuthorized(&cdb, clientId)) > -1)
    {
        client_info *client = &(cdb.clients[clientIndex]);
        if (client->state == SEND_ALIVE)
        {
            if (hasElem(elemId, client))
            {
                if (pack == SET_DATA && !isInputElem(elemId))
                {
                    printf("L'element %s no es un element d'entrada (actuador)\n", elemId);
                    return;
                }
                check((sock = socket(AF_INET, SOCK_STREAM, 0)), "Error creant un socket\n");
                if (connectTo(sock, client->ip, client->tcpPort) >= 0)
                {
                    check(sendTcp(sock, pack, cfg.id, client->randNum, elemId, value, client->id), "Error en el send\n");
                    fd_set inputs;
                    check(selectIn(sock, &inputs, M), "Error en el select\n");
                    if (FD_ISSET(sock, &inputs))
                    {
                        tcp_pdu response;
                        check(recv(sock, &response, sizeof(tcp_pdu), 0), "Error rebent informació\n");
                        if (response.pack == DATA_ACK)
                        {
                            debugPrint("Dades acceptades");
                            storeData(pack == SET_DATA ? "SET_DATA" : "GET_DATA", client->id, response.elem, response.value);
                        }
                        else if (response.pack == DATA_NACK)
                            debugPrint("Dades no acceptades");
                        else if (response.pack == DATA_REJ)
                        {
                            debugPrint("Dades rebutjades, client passa a l'estat DISCONNECTED");
                            disconnectClient(client);
                        }
                    }
                    else
                        debugPrint("No s'ha rebut cap resposta del client");
                }
                else
                {
                    debugPrint("Ha hagut un error connectant amb el client");
                    disconnectClient(client);
                }
                close(sock);
            }
            else
                printf("\tL'element %s no forma part del client %s\n", elemId, clientId);
        }
        else
            printf("\tEl client %s no es troba connectat\n", clientId);
    }
    else
        printf("\tEl dispositiu introduit no forma part del sistema.\n");
}

int isInputElem(const char *elemId)
{
    int len = strlen(elemId);
    return elemId[len - 1] == 'I';
}

/*--------------------------------------------------*/
/*-------------- HANDLE TCP CONNECTIONS ------------*/
/*--------------------------------------------------*/
void tcpConnections(int tcpSocket)
/* Executa el procés d'espera de connexions tcp */
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
    char debugMessage[55] = {'\0'};
    fd_set inputs;
    check(selectIn(clientSocket, &inputs, TCP_WAIT_TIME), "Error en el select\n");
    if (FD_ISSET(clientSocket, &inputs))
    {
        int clientIndex;
        tcp_pdu pdu;
        check(recv(clientSocket, &pdu, sizeof(tcp_pdu), 0), "Error rebent informació del client\n");
        if (pdu.pack == SEND_DATA)
        {
            if ((clientIndex = isAuthorized(&cdb, pdu.id)) != -1)
            {
                client_info *client = &(cdb.clients[clientIndex]);
                sprintf(debugMessage, "El client %s ha enviat un paquet SEND_DATA", client->id);
                debugPrint(debugMessage);
                if (validCredentials(pdu, client))
                {
                    if (client->state == SEND_ALIVE)
                    {
                        if (hasElem(pdu.elem, client))
                        {
                            int dataStored = storeData("SEND_DATA", client->id, pdu.elem, pdu.value);
                            if (dataStored == 0)
                            {
                                debugPrint("Dades emmagatzemades");
                                check(sendTcp(clientSocket, DATA_ACK, cfg.id, client->randNum, pdu.elem, pdu.value, client->id), "Error en l'enviament de DATA_ACK\n");
                            }
                            else
                            {
                                debugPrint("No s'han pogut emmagatzemar les dades");
                                check(sendTcp(clientSocket, DATA_NACK, cfg.id, client->randNum, pdu.elem, pdu.value, "No s'han pogut emmagatzemar les dades al servidor"), "Error enviant DATA_NACK\n");
                            }
                        }
                        else
                        {
                            debugPrint("L'element especificat en el paquet no forma part del client");
                            check(sendTcp(clientSocket, DATA_NACK, cfg.id, client->randNum, pdu.elem, pdu.value, "L'element no es troba en el dispositiu"), "Error enviant DATA_NACK\n");
                        }
                    }
                    else
                    {
                        debugPrint("El client no es troba en l'estat SEND_ALIVE");
                        disconnectClient(client);
                    }
                }
                else
                {
                    debugPrint("Dades del client incorrectes");
                    check(sendTcp(clientSocket, DATA_REJ, cfg.id, pdu.randNum, pdu.elem, pdu.value, ""), "Error enviant DATA_REJ\n");
                    disconnectClient(client);
                }
            }
            else
            {
                debugPrint("Un client no autoritzat ha intentat enviar un paquet SEND_DATA");
                check(sendTcp(clientSocket, DATA_REJ, cfg.id, pdu.randNum, pdu.elem, pdu.value, ""), "Error enviant DATA_REJ\n");
            }
        }
    }
    return NULL;
}

/*--------------------------------------------------*/
/*-------------- CONTROL ALIVE CLIENTS -------------*/
/*--------------------------------------------------*/
void controlAlives()
/* Comprova que cada client segueix operatiu */
{
    while (1)
    {
        time_t now = time(NULL);
        for (int i = 0; i < cdb.length; i++)
        {
            client_info *client = &(cdb.clients[i]);
            if (client->lastAlive != -1)
            {
                time_t diff = difftime(now, client->lastAlive);
                if (client->state == REGISTERED)
                {
                    if (diff >= FIRST_ALIVE_TIMEOUT)
                    {
                        char debugMessage[60] = {'\0'};
                        sprintf(debugMessage, "No s'ha rebut el primer ALIVE del client %s", client->id);
                        debugPrint(debugMessage);
                        disconnectClient(client);
                    }
                }
                else if (client->state == SEND_ALIVE)
                {
                    if (diff >= ALIVE_TIMEOUT * 3)
                    {
                        char debugMessage[60] = {'\0'};
                        sprintf(debugMessage, "El client %s no ha enviat 3 ALIVES consecutius", client->id);
                        debugPrint(debugMessage);
                        disconnectClient(client);
                    }
                }
            }
        }
    }
}

/*--------------------------------------------------*/
/*----------------- ATTEND CLIENTS -----------------*/
/*--------------------------------------------------*/
void attendClients(int socket)
/* Espera a rebre paquets UDP dels clients */
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
        client_info *client = &(cdb.clients[clientIndex]);
        if (pdu.pack == REG_REQ)
            registerClient(attSocket, pdu, clientAddress, client);
        else if (pdu.pack == ALIVE)
            handleAlive(attSocket, pdu, clientAddress, client);
    }
    else
        check(sendUdp(attSocket, REG_REJ, cfg.id, "00000000", "Dispositiu no autoritzat en el sistema", clientAddress), "Error enviant REG_REJ\n");
    return NULL;
}

/*--------------------------------------------------*/
/*----------------- REGISTER CLIENTS ---------------*/
/*--------------------------------------------------*/
void registerClient(int sock, udp_pdu pdu, struct sockaddr_in address, client_info *client)
{
    /* COMPROVEM LES DADES DE LA PDU I DEL CLIENT */
    if (strcmp(pdu.randNum, "00000000") != 0 || strcmp(pdu.data, "") != 0)
    {
        disconnectClient(client);
        check(sendUdp(sock, REG_REJ, cfg.id, "00000000", "Dades incorrectes", address), "Error enviant REG_REJ\n");
        return;
    }
    if (client->state != DISCONNECTED)
    {
        disconnectClient(client);
        return;
    }
    char randNum[9] = {'\0'}, data[61] = {'\0'}, debugMessage[60] = {'\0'};
    int newSocket, port;
    /* GENEREM NOMBRE ALEATORI I OBRIM UN NOU PORT */
    sprintf(randNum, "%d", generateRandNum(10000000, 99999999));
    check((newSocket = socket(AF_INET, SOCK_DGRAM, 0)), "Error al connectar el socket udp.\n");
    check(bindTo(newSocket, 0), "Error al bind del udpSocket");
    check((port = getPort(newSocket)), "Error agafant port\n");
    sprintf(data, "%d", port);
    check(sendUdp(newSocket, REG_ACK, cfg.id, randNum, data, address), "Error enviant REG_ACK\n");
    sprintf(debugMessage, "Client %s pasa a l'estat WAIT_INFO", client->id);
    debugPrint(debugMessage);
    client->state = WAIT_INFO;
    strcpy(client->randNum, randNum);
    waitInfo(newSocket, client);
    close(newSocket);
}

void waitInfo(int sock, client_info *client)
/* Executem el procés de rebuda d'informació del client */
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
            handleClientInfo(sock, info, infoAddress, client);
        else
            disconnectClient(client);
    }
    else
        disconnectClient(client);
}

void handleClientInfo(int sock, udp_pdu info, struct sockaddr_in clientAddress, client_info *client)
{
    int tcpPort = atoi(strtok(info.data, ","));
    char *elems = strtok(NULL, ",");
    if (validInfo(info.id, info.randNum, tcpPort, elems, client))
    {
        char data[6], debugMessage[60] = {'\0'};
        sprintf(data, "%d", cfg.tcpPort);
        check(sendUdp(sock, INFO_ACK, cfg.id, client->randNum, data, clientAddress), "Error enviant INFO_NACK\n");
        /* Registrem les dades del client */
        strcpy(client->elems, elems);
        strcpy(client->ip, inet_ntoa(clientAddress.sin_addr));
        client->tcpPort = tcpPort;
        client->lastAlive = time(NULL);
        client->state = REGISTERED;
        sprintf(debugMessage, "Client %s pasa a l'estat REGISTERED", client->id);
        debugPrint(debugMessage);
    }
    else
    {
        check(sendUdp(sock, INFO_NACK, cfg.id, client->randNum, "Dades incorrectes\n", clientAddress), "Error enviant INFO_NACK\n");
        disconnectClient(client);
    }
}

/*--------------------------------------------------*/
/*----------------- SEND ALIVES --------------------*/
/*--------------------------------------------------*/
void handleAlive(int sock, udp_pdu pdu, struct sockaddr_in clientAddress, client_info *client)
{
    if (client->state == REGISTERED || client->state == SEND_ALIVE)
    {
        if (validAlive(pdu, client))
        {
            client->lastAlive = time(NULL);
            check(sendUdp(sock, ALIVE, cfg.id, client->randNum, client->id, clientAddress), "Error enviant ALIVE\n");
            if (client->state == REGISTERED)
            {
                char debugMessage[60] = {'\0'};
                sprintf(debugMessage, "Client %s pasa a l'estat SEND_ALIVE", client->id);
                debugPrint(debugMessage);
                client->state = SEND_ALIVE;
            }
        }
        else
        {
            check(sendUdp(sock, ALIVE_REJ, cfg.id, client->randNum, "Dades del paquet ALIVE incorrectes\n", clientAddress), "Error enviant ALIVE_REJ\n");
            disconnectClient(client);
        }
    }
    else
        disconnectClient(client);
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

int storeData(const char *pack, const char *clientId,
              const char *elem, const char *value)
{
    char filename[17] = {'\0'};
    sprintf(filename, "%s.data", clientId);
    FILE *fp = fopen(filename, "a");
    if (fp == NULL)
        return -1;
    time_t t = time(NULL);
    struct tm date = *localtime(&t);
    if (fprintf(fp, "%d-%d-%d;%s;%s;%s;%s\n", date.tm_year + 1900, date.tm_mon + 1, date.tm_mday, __TIME__, pack, elem, value) == -1)
        return -1;
    fflush(fp);
    return 0;
}

int validAlive(udp_pdu pdu, client_info *client)
{
    return strcmp(pdu.id, client->id) == 0 &&
           strcmp(pdu.randNum, client->randNum) == 0 &&
           strcmp(pdu.data, "") == 0;
}

int validInfo(const char *id, const char *randNum, int tcpPort, char *elems, client_info *client)
{
    return strcmp(id, client->id) == 0 &&
           strcmp(randNum, client->randNum) == 0 &&
           tcpPort >= 1024 &&
           tcpPort <= 65535 &&
           validElems(elems);
}

int validCredentials(tcp_pdu pdu, client_info *client)
{
    return strcmp(pdu.randNum, client->randNum) == 0;
}

int validElems(char *elems)
{
    int numElems = 0;
    char cpy[50] = {'\0'};
    strcpy(cpy, elems);
    char *elem = strtok(cpy, ";");
    while (elem != NULL)
    {
        numElems++;
        if (strlen(elem) != 7)
            return 0;
        if (!isdigit(elem[4]))
            return 0;
        if (elem[6] != 'I' && elem[6] != 'O')
            return 0;
        if (!isalpha(elem[0]) || !isupper(elem[0]) ||
            !isalpha(elem[1]) || !isupper(elem[1]) ||
            !isalpha(elem[2]) || !isupper(elem[2]))
        {
            return 0;
        }
        elem = strtok(NULL, ";");
    }
    if (numElems == 0)
        return 0;
    return 1;
}