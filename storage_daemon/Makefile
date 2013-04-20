CC=g++
CFLAGS=-O2 -I../lsvd
LDFLAGS=

all: clean sdaemon

sdaemon: sdaemon.cpp 
	$(CC) $(CFLAGS) sdaemon.cpp -o sdaemon

install: sdaemon
	scp sdaemon testbed:~/

lsvd.o:
	$(CC) $(CFLAGS) -c ../lsvd/lsvd.c

clean:
	rm -f *.o test
