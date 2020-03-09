#
#
LDFLAGS= -Wall	
SOLARISLD= -lnsl -lsocket

all: 
	@echo "Per compilar executar: "
	@echo "		make sistema	"
	@echo "On sistema es un de: linux,solaris,altres"


linux: server.c
	gcc -o server server.c -DLINUX $(LDFLAGS)
	
solaris: server.c
	gcc -o server server.c -DSOLARIS $(LDFLAGS) $(SOLARISLD)
	
altres: server.c
	gcc -o server server.c $(LDFLAGS)


clean:
	-rm -fr server
