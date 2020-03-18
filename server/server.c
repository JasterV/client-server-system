#include "common.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <signal.h>

config *CFG;
client_info **DB;
int udp_socket;
int tcp_socket;
struct sockaddr_in udp_address;
struct sockaddr_in tcp_address;

void getInitialData(int, char const *[]);
void handler(int sig);

int main(int argc, char const *argv[])
{
    signal(SIGTSTP, handler);
    signal(SIGQUIT, handler);
    signal(SIGINT, handler);
    signal(SIGCHLD, SIG_IGN);

    /*Llegim els fitxers de configuració i dispositius*/
    getInitialData(argc, argv);
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

    while (1)
    {
        udp_pdu *pdu = (udp_pdu *)malloc(sizeof(udp_pdu *));
        pdu->data = (const char *)malloc(61 * sizeof(char));
        pdu->id = (const char *)malloc(13 * sizeof(char));
        pdu->rand_num = (const char *)malloc(9 * sizeof(char));
        recvfrom(udp_socket, pdu, sizeof(pdu), 0, NULL, NULL);
        printf("Missatge rebut -> pdu: (%d, %s, %s, %s)\n", pdu->pack, pdu->id, pdu->rand_num, pdu->data);
    }

    free(DB);
    free(CFG);
    exit(EXIT_SUCCESS);
}

void handler(int sig)
{
    exit(EXIT_SUCCESS);
}

void getInitialData(int argc, char const *argv[])
{
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
}
