CC=g++
CFLAGS=-O2 -I../lsvd
LDFLAGS=

all: clean sdaemon

sdaemon: replicamgr.o lsvd.o sdaemon.cpp 
	$(CC) $(CFLAGS) lsvd.o replicamgr.o sdaemon.cpp -o sdaemon

install: sdaemon
	scp sdaemon testbed:~/

replicamgr.o: replicamgr.h replicamgr.cpp
	$(CC) $(CFLAGS) -c replicamgr.cpp

lsvd.o:
	$(CC) $(CFLAGS) -c ../lsvd/lsvd.c

clean:
	rm -f *.o test sdaemon
