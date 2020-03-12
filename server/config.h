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
    if (i == 0)
    {
        free(line);
        return NULL;
    }
    line[i] = '\0';
    return line;
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

config *readConfig(const char *filename)
{
    const char *id, *udp, *tcp;
    FILE *fp = fopen(filename, "r");
    config *cfg = (config *)malloc(sizeof(config));
    if (fp == NULL)
    {
        free(cfg);
        return NULL;
    }
    if ((id = getInfoFromLine(fp)) == NULL)
    {
        free(cfg);
        return NULL;
    }
    if ((udp = getInfoFromLine(fp)) == NULL)
    {
        free(cfg);
        return NULL;
    }
    if ((tcp = getInfoFromLine(fp)) == NULL)
    {
        free(cfg);
        return NULL;
    }
    cfg->id = id;
    cfg->udp_port = atoi(udp);
    cfg->tcp_port = atoi(tcp);
    fclose(fp);
    return cfg;
}

char **readDb(FILE *fp)
{
    size_t len = 1;
    char **db = (char **)malloc(len * sizeof(char *));
    char *line;
    int i = 0;
    while ((line = getLine(fp)) != NULL)
    {
        db[i] = line;
        i++;
        if (i == len)
        {
            len++;
            db = (char **)realloc(db, len * (sizeof(char *)));
        }
    }
    if (i == 0)
    {
        free(db);
        return NULL;
    }
    db[i] = "\0";
    return db;
}

char **readDbFile(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
        return NULL;
    return readDb(fp);
}
