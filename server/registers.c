#include "common.h"

config *cfg;
clients_db *cdb;
char buffer[100] = {'\0'};

int main(int argc, char const *argv[])
{
    key_t key = SHM_CFG_KEY;
    sprintf(buffer, "%d", key);
    write(0, buffer, strlen(buffer));
    int shmid = shmget(key, sizeof(config), SHM_R | SHM_W);
    cfg = shmat(shmid, NULL, 0);

    memset(buffer, 0, sizeof(buffer));
    sprintf(buffer, "%s %d %d\n hola", cfg->id, cfg->udp_port, cfg->tcp_port);
    write(0, buffer, strlen(buffer));

    exit(EXIT_SUCCESS);
}
