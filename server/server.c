#include "common.h"

int readConfig(config *cfg, const char *filename);
int readDb(clients_db *db, const char *filename);
void handler(int sig);
void *attendReg(void *arg);
int shareDb();
int shareClientsInfo();

config cfg;
clients_db *cdb;

int main(int argc, char const *argv[])
{
    signal(SIGTSTP, handler);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, handler);

    /*Compartim la estructura clients_db en memoria*/
    shareDb();
    /*Llegim els fitxers de configuració i dispositius*/
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
    /*Llegim els fitxer de configuració i dispositius*/
    check(readConfig(&cfg, cfgname),
          "Error llegint el fitxer de configuració.\n");
    check(readDb(cdb, dbname),
          "Error llegint el fitxer de dispositius.\n");
    /*Un cop es sap quants dispositius estan registrats 
     a la base de dades, compartim la estructura clients_info en memoria*/
    shareClientsInfo();
    /* CREACIÓ DEL SOCKET UDP*/
    int udp_socket;
    struct sockaddr_in udp_address;
    check((udp_socket = socket(AF_INET, SOCK_DGRAM, 0)),
          "Error al connectar el socket udp.\n");
    udp_address.sin_family = AF_INET;
    udp_address.sin_port = htons(cfg.udp_port);
    udp_address.sin_addr.s_addr = INADDR_ANY;
    check(bind(udp_socket, (struct sockaddr *)&udp_address, sizeof(udp_address)),
          "Error al fer el bind del socket udp.\n");
    /*CREACIÓ DEL SOCKET TCP*/
    int tcp_socket;
    struct sockaddr_in tcp_address;
    check((tcp_socket = socket(AF_INET, SOCK_STREAM, 0)),
          "Error al connectar el socket tcp.\n");
    tcp_address.sin_family = AF_INET;
    tcp_address.sin_port = htons(cfg.tcp_port);
    tcp_address.sin_addr.s_addr = INADDR_ANY;
    check(bind(tcp_socket, (struct sockaddr *)&tcp_address, sizeof(tcp_address)),
          "Error al fer el bind del socket tcp.\n");
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

void *attendReg(void *arg)
{
    int udp_socket = *(int *)arg;
    udp_pdu pdu;
    struct sockaddr_in client_address;

    memset(&pdu, 0, sizeof(udp_pdu));
    memset(&client_address, 0, sizeof(struct sockaddr_in));
    recvfrom(udp_socket, &pdu, sizeof(udp_pdu), 0, (struct sockaddr *)&client_address, NULL);
    printf("Paquet rebut\n");
    if (fork() == 0)
    {
        while (1)
        {
            sleep(1);
            cdb->clients[0].tcp_port += 1;
        }
    }
    return NULL;
}

/*-----------------CONFIG FILES READING------------------*/

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