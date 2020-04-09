#include "common.h"

void handler(int sig);
void setOptions(int argc, char const *argv[], const char *cfgname, const char *dbname);

int readConfig(config *cfg, const char *filename);
int readDb(clients_db *db, const char *filename);
int shareDb(clients_db cdb);
int shareCFG(config cfg);

int main(int argc, char const *argv[])
{
    signal(SIGTSTP, handler);
    signal(SIGQUIT, handler);
    signal(SIGINT, handler);

    config cfg;
    clients_db cdb;
    int udp_socket;
    int tcp_socket;
    struct sockaddr_in tcp_address;
    struct sockaddr_in udp_address;

    const char *cfgname = "server.cfg", *dbname = "bbdd_dev.dat";
    if (argc > 1)
        setOptions(argc, argv, cfgname, dbname);
    check(readConfig(&cfg, cfgname),
          "Error llegint el fitxer de configuració.\n");
    check(readDb(&cdb, dbname),
          "Error llegint el fitxer de dispositius.\n");
    /* CREACIÓ DEL SOCKET UDP*/
    check((udp_socket = socket(AF_INET, SOCK_DGRAM, 0)), "Error al connectar el socket udp.\n");
    udp_address.sin_family = AF_INET;
    udp_address.sin_port = htons(cfg.udp_port);
    udp_address.sin_addr.s_addr = INADDR_ANY;
    check(bind(udp_socket, (struct sockaddr *)&udp_address, sizeof(struct sockaddr)), "Bind error\n");
    /*CREACIÓ DEL SOCKET TCP*/
    check((tcp_socket = socket(AF_INET, SOCK_STREAM, 0)), "Error al connectar el socket tcp.\n");
    tcp_address.sin_family = AF_INET;
    tcp_address.sin_port = htons(cfg.tcp_port);
    tcp_address.sin_addr.s_addr = INADDR_ANY;
    check(bind(tcp_socket, (struct sockaddr *)&tcp_address, sizeof(struct sockaddr)), "Bind error\n");


    exit(EXIT_SUCCESS);
}

void handler(int sig)
{
    exit(EXIT_SUCCESS);
}

void setOptions(int argc, char const *argv[], const char *cfgname, const char *dbname)
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