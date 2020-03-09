#include <stdio.h>
#include <stdlib.h>
#include "config.h"

int main(int argc, char const *argv[])
{
    config *cfg = readConfig("server.cfg");
    printf("%s\n%d\n%d\n", cfg->id, cfg->udp_port, cfg->tcp_port);
    exit(EXIT_SUCCESS);
}
