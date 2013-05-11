CC=gcc
CFLAGS=-Wall -O2 -pthread -lrt -D_FILE_OFFSET_BITS=64

all: test open write cleanup

test: test.o lsvd.o
	$(CC) $(CFLAGS) test.o lsvd.o -o lsvd_test

open: open.o lsvd.o
	$(CC) $(CFLAGS) open.o lsvd.o -o open

write: write.o lsvd.o
	$(CC) $(CFLAGS) write.o lsvd.o -o write

cleanup: cleanup.o lsvd.o
	$(CC) $(CFLAGS) cleanup.o lsvd.o -o cleanup

lsvd.o: lsvd.c lsvd.h
	$(CC) $(CFLAGS) -c lsvd.c

test.o: test.c lsvd.h
	$(CC) $(CFLAGS) -c test.c

write.o: write.c lsvd.h
	$(CC) $(CFLAGS) -c write.c

open.o: open.c lsvd.h
	$(CC) $(CFLAGS) -c open.c

cleanup.o: cleanup.c lsvd.h
	$(CC) $(CFLAGS) -c cleanup.c

clean:
	rm -r *.o lsvd_test open write
