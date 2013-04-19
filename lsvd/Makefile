CC=gcc
CFLAGS=-Wall -O2

all: test

test: test.o lsvd.o
	$(CC) $(CFLAGS) test.o lsvd.o -o lsvd_test

lsvd.o: lsvd.c lsvd.h
	$(CC) $(CFLAGS) -c lsvd.c

test.o: test.c lsvd.h
	$(CC) $(CFLAGS) -c test.c

clean:
	rm -r *.o lsvd_test
