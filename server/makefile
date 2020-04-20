#
#
LDFLAGS= -ansi -pedantic -Wall -lrt -pthread -std=c99  

all: server.c 
	gcc server.c -o server $(LDFLAGS)

clean: 
	-rm -rf server
