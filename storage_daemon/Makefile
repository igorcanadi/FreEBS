CC=g++
CFLAGS=-g -O2 -I../lsvd -pthread #-DDEBUG
LDFLAGS=

all: clean sdaemon test

sdaemon: replicamgr.o lsvd.o sdaemon.cpp 
	$(CC) $(CFLAGS) lsvd.o replicamgr.o sdaemon.cpp -o sdaemon

install: sdaemon
	scp sdaemon testbed:~/

test: replicamgr.o lsvd.o replica_test.cpp
	$(CC) $(CFLAGS) lsvd.o replicamgr.o replica_test.cpp -o replica_test

replicamgr.o: replicamgr.h replicamgr.cpp
	$(CC) $(CFLAGS) -c replicamgr.cpp

lsvd.o:
	$(CC) $(CFLAGS) -c ../lsvd/lsvd.c

clean:
	rm -f *.o sdaemon replica_test
