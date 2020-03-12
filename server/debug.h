#include <stdio.h>

int DEBUG_ON = 0;

void debugPrint(const char *message)
{
    if (DEBUG_ON)
        printf("%s -> %s\n", __TIME__, message);
}