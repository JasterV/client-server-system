#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

typedef struct config
{
    const char *id;
    int udp_port;
    int tcp_port;
} config;

char *trim(char *str)
{
    char *end;
    /*left trim*/
    while (isspace((unsigned char)*str))
        str++;
    if (*str == 0) /*Es tot espais*/
        return str;
    /*Right trim*/
    end = str + strlen(str) - 1;
    while (end > str && (isspace((unsigned char)*end) || (unsigned char)*end == '\n'))
        end--;
    end[1] = '\0';
    return str;
}

char *getInfo(char *line)
{
    char *info = strrchr(line, '=');
    info++;
    return info;
}

char *getLine(FILE *fp)
{
    size_t len = 3;
    char *line = (char *)malloc(len * sizeof(char));
    int c = fgetc(fp);
    int i = 0;
    while (c != '\n' && c != EOF)
    {
        line[i] = c;
        i++;
        if (i == len)
        {
            len++;
            line = (char *)realloc(line, len * sizeof(char));
        }
        c = fgetc(fp);
    }
    line[i] = '\0';
    return line;
}

char *getInfoFromLine(FILE *fp)
{
    char *line = getLine(fp);
    char *info = getInfo(line);
    info = trim(info);
    return info;
}

config *readConfig(const char *filename)
{
    config *cfg = (config *)malloc(sizeof(config));
    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
    {
        perror("Error while opening the file.\n");
        exit(EXIT_FAILURE);
    }
    cfg->id = getInfoFromLine(fp);
    cfg->udp_port = atoi(getInfoFromLine(fp));
    cfg->tcp_port = atoi(getInfoFromLine(fp));
    fclose(fp);
    return cfg;
}
