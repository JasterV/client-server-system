#include "common.h"

void handler(int sig);
void check(int result, const char *message);
int isAuthorized(const char *id);
void setOptions(int argc, char const *argv[], const char *cfgname, const char *dbname);
int bind_to(int socket, uint16_t port, struct sockaddr_in *address);
void shareClientsInfo(int shmid);

void attend_registers(int socket);
int getPort(int fd);

config cfg;
clients_db cdb;

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
    /*LLEGIM ELS FITXERS DE CONFIGURACIO*/
    check(readConfig(&cfg, cfgname),
          "Error llegint el fitxer de configuració.\n");
    check(readDb(&cdb, dbname),
          "Error llegint el fitxer de dispositius.\n");
    /* COMPARTIM LA LLISTA DE CLIENTS */
    int shmid = shmget(IPC_PRIVATE, cdb.length * sizeof(client_info), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    shareClientsInfo(shmid);
    /* CREEM ELS SOCKETS I FEM BIND */
    int udp_socket, tcp_socket;
    struct sockaddr_in tcp_address, udp_address;
    check((udp_socket = socket(AF_INET, SOCK_DGRAM, 0)), "Error al connectar el socket udp.\n");
    check(bind_to(udp_socket, htons(cfg.udp_port), &udp_address), "Error al bind del udp_socket");
    check((tcp_socket = socket(AF_INET, SOCK_STREAM, 0)), "Error al connectar el socket tcp.\n");
    check(bind_to(tcp_socket, htons(cfg.tcp_port), &tcp_address), "Error al bind del tcp_socket");
    
    /* CREEM UN PROCÉS PER L'ATENCIÓ DE PETICIONS DE REGISTRE I ALIVES */
    pid_t reg_att;
    if ((reg_att = fork()) == 0)
    {
        printf("Estic atenent registres iuju\n");
        attend_registers(udp_socket);
        exit(EXIT_SUCCESS);
    }
    else if (reg_att == -1)
    {
        perror("Error creant el procés de registres");
        exit(EXIT_FAILURE);
    }
    /* CREEM UN PROCÉS PER LA RECEPCIÓ DE CONNEXIONS TCP */
    pid_t wait_conn;
    if ((wait_conn = fork()) == 0)
    {
        printf("Estic esperant connexions wii\n");
        fflush(stdout);
        while (1)
            ;
        exit(EXIT_SUCCESS);
    }
    else if (wait_conn == -1)
    {
        perror("Error creant el procés de connexions tcp");
        fflush(stdout);
        kill(reg_att, SIGINT);
        wait(NULL);
        exit(EXIT_FAILURE);
    }
    /*  CREEM UN PROCÉS PER LA CONSOLA DE COMANDES */
    pid_t cli;
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
        kill(reg_att, SIGINT);
        wait(NULL);
        kill(wait_conn, SIGINT);
        wait(NULL);
        exit(EXIT_FAILURE);
    }
    /* Si qualsevol procés fill acaba
       envia una señal SIGINT a tots els 
       processos */
    printf("Parent pid: %d\n", getpid());
    wait(NULL);
    printf("Un procés ha mort\n");
    kill(-getpid(), SIGINT);
}

/*--------------------------------------------------*/
/*----------------- MAIN PROCESSES -----------------*/
/*--------------------------------------------------*/

void attend_registers(int socket)
{
    struct sockaddr_in client_address;
    socklen_t len = sizeof(struct sockaddr_in);
    udp_pdu pdu;
    while (1)
    {
        check(recvfrom(socket, &pdu, sizeof(udp_pdu), 0, (struct sockaddr *)&client_address, &len), "Error rebent informació del socket");
        printf("Client ip: %s, server udp port: %d\n", inet_ntoa(client_address.sin_addr), getPort(socket));
    }
}

/*-------------------------------------------------*/
/*-----------------AUXILIAR FUNCTIONS--------------*/
/*-------------------------------------------------*/

void shareClientsInfo(int shmid)
{
    client_info *tmp = (client_info *)shmat(shmid, (void *)0, 0);
    for (int i = 0; i < cdb.length; i++)
    {
        tmp[i] = cdb.clients[i];
    }
    free(cdb.clients);
    cdb.clients = tmp;
}

int isAuthorized(const char *id)
{
    for (int i = 0; i < cdb.length; i++)
        if (strcmp(id, cdb.clients[i].id) == 0)
            return i;
    return -1;
}

int bind_to(int socket, uint16_t port, struct sockaddr_in *address)
{
    address->sin_family = AF_INET;
    address->sin_port = port;
    address->sin_addr.s_addr = INADDR_ANY;
    return bind(socket, (struct sockaddr *)address, sizeof(struct sockaddr));
}

int getPort(int fd){
    struct sockaddr_in address;
    socklen_t len;
    int port = 0;
    memset(&address, 0, sizeof(struct sockaddr_in));
    check(getsockname(fd, (struct sockaddr *)&address, &len), "Error en getsockname");
    port = ntohs(address.sin_port);
    return port;
}

void check(int result, const char *message)
{
    if (result < 0)
    {
        perror(message);
        exit(EXIT_FAILURE);
    }
}

void handler(int sig)
{
    while (wait(NULL) > 0)
        ;
    printf("procés %d mor\n", getpid());
    shmdt(cdb.clients);
    exit(EXIT_SUCCESS);
}
