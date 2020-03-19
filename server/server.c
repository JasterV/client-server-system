#include "common.h"

config *CFG;
client_info **DB;
int udp_socket;
int tcp_socket;

char *getInfo(char *line);
char *getInfoFromLine(FILE *fp);
config *readConfig(const char *filename);
client_info **readDbFile(const char *filename);
void handler(int sig);
void *attendReg(void *arg);

int main(int argc, char const *argv[])
{
    signal(SIGTSTP, handler);
    signal(SIGQUIT, handler);
    signal(SIGINT, handler);
    signal(SIGCHLD, SIG_IGN);

    struct sockaddr_in udp_address;
    struct sockaddr_in tcp_address;

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
    /*Llegim el fitxer de configuració*/
    checkPointer((CFG = readConfig(cfgname)),
                 "Error llegint el fitxer de configuració.\n");
    /*Llegim el fitxer de dispositius*/
    checkPointer((DB = readDbFile(dbname)),
                 "Error llegint el fitxer de dispositius.\n");

    /*Connectem el socket udp al port corresponent*/
    checkInt((udp_socket = socket(AF_INET, SOCK_DGRAM, 0)),
             "Error al connectar el socket udp.\n");
    udp_address.sin_family = AF_INET;
    udp_address.sin_port = htons(CFG->udp_port);
    udp_address.sin_addr.s_addr = INADDR_ANY;
    checkInt(bind(udp_socket, (struct sockaddr *)&udp_address, sizeof(udp_address)),
             "Error al fer el bind del socket udp.\n");

    /*Connectem el socket tcp al port corresponent*/
    checkInt((tcp_socket = socket(AF_INET, SOCK_STREAM, 0)),
             "Error al connectar el socket tcp.\n");
    tcp_address.sin_family = AF_INET;
    tcp_address.sin_port = htons(CFG->tcp_port);
    tcp_address.sin_addr.s_addr = INADDR_ANY;
    checkInt(bind(tcp_socket, (struct sockaddr *)&tcp_address, sizeof(tcp_address)),
             "Error al fer el bind del socket tcp.\n");
    checkInt(listen(tcp_socket, 100), "Error listening.\n");

    pthread_t attendRegisters;
    /*pthread_t waitTcpConnections;*/
    /*pthread_t cli;*/

    pthread_create(&attendRegisters, NULL, attendReg, NULL);
    pthread_join(attendRegisters, NULL);

    free(DB);
    free(CFG);
    exit(EXIT_SUCCESS);
}

void handler(int sig)
{
    exit(EXIT_SUCCESS);
}

void *attendReg(void *arg)
{
    udp_pdu pdu;
    struct sockaddr_in client_address;
    while (1)
    {
        memset(&pdu, 0, sizeof(udp_pdu));
        memset(&client_address, 0, sizeof(struct sockaddr_in));
        recvfrom(udp_socket, &pdu, sizeof(udp_pdu), 0, (struct sockaddr *)&client_address, NULL);
        printf("Paquet rebut\n");
    }
    exit(0);
    return NULL;
}

/*-----------------CONFIG FILES READING------------------*/

config *readConfig(const char *filename)
{
    const char *id, *udp, *tcp;
    FILE *fp;
    if ((fp = fopen(filename, "r")) == NULL)
    {
        return NULL;
    }
    config *cfg = (config *)malloc(sizeof(config));
    if ((id = getInfoFromLine(fp)) == NULL)
    {
        fclose(fp);
        free(cfg);
        return NULL;
    }
    if ((udp = getInfoFromLine(fp)) == NULL)
    {
        fclose(fp);
        free(cfg);
        return NULL;
    }
    if ((tcp = getInfoFromLine(fp)) == NULL)
    {
        fclose(fp);
        free(cfg);
        return NULL;
    }
    cfg->id = id;
    cfg->udp_port = atoi(udp);
    cfg->tcp_port = atoi(tcp);
    fclose(fp);
    return cfg;
}

client_info **readDbFile(const char *filename)
{
    FILE *fp;
    int len = 1;
    if ((fp = fopen(filename, "r")) == NULL)
        return NULL;
    client_info **db = (client_info **)malloc(len * sizeof(client_info *));
    char *line;
    int i = 0;
    while ((line = getLine(fp)) != NULL)
    {
        client_info *client = (client_info *)malloc(sizeof(client_info));
        memset(client, 0, sizeof(client_info));
        client->id = line;
        client->state = DISCONNECTED;
        db[i] = client;
        i++;
        if (i == len)
        {
            len++;
            db = (client_info **)realloc(db, len * (sizeof(client_info *)));
        }
    }
    if (i == 0)
    {
        fclose(fp);
        free(db);
        return NULL;
    }
    fclose(fp);
    db[i] = NULL;
    return db;
}

char *getInfo(char *line)
{
    char *info = strrchr(line, '=');
    info++;
    return info;
}

char *getInfoFromLine(FILE *fp)
{
    char *line, *info;
    line = getLine(fp);
    if (line == NULL)
        return NULL;
    info = trim(getInfo(line));
    return info;
}
