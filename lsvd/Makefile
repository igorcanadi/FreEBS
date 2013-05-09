CC=gcc
CFLAGS=-Wall -O2 -pthread -lrt -D_FILE_OFFSET_BITS=64

all: test open write

test: test.o lsvd.o
	$(CC) $(CFLAGS) test.o lsvd.o -o lsvd_test

open: open.o lsvd.o
	$(CC) $(CFLAGS) open.o lsvd.o -o open

write: write.o lsvd.o
	$(CC) $(CFLAGS) write.o lsvd.o -o write

lsvd.o: lsvd.c lsvd.h
	$(CC) $(CFLAGS) -c lsvd.c

test.o: test.c lsvd.h
	$(CC) $(CFLAGS) -c test.c

write.o: write.c lsvd.h
	$(CC) $(CFLAGS) -c write.c

open.o: open.c lsvd.h
	$(CC) $(CFLAGS) -c open.c

clean:
	rm -r *.o lsvd_test open write
