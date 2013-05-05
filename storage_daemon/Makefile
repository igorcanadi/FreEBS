CC=g++
CFLAGS=-g -O2 -I../lsvd -pthread -DDEBUG -DSYNC
LDFLAGS=-lrt
all: clean sdaemon test

sdaemon: replicamgr.o lsvd.o connmgr.o sdaemon.cpp 
	$(CC) $(CFLAGS) lsvd.o replicamgr.o sdaemon.cpp connmgr.o $(LDFLAGS) -o sdaemon

install: sdaemon
	scp sdaemon testbed:~/

test: replicamgr.o lsvd.o replica_test.cpp
	$(CC) $(CFLAGS) lsvd.o replicamgr.o $(LDFLAGS) replica_test.cpp -o replica_test
	$(CC) $(CFLAGS) lsvd.o replicamgr.o $(LDFLAGS) driver_dummy.cpp -o driver_dummy

replicamgr.o: replicamgr.h replicamgr.cpp
	$(CC) $(CFLAGS) -c replicamgr.cpp

connmgr.o: connmgr.h connmgr.cpp
	$(CC) $(CFLAGS) -c connmgr.cpp

lsvd.o:
	$(CC) $(CFLAGS) -c ../lsvd/lsvd.c

clean:
	rm -f *.o sdaemon replica_test driver_dummy
