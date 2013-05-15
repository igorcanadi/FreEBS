CC=gcc
CFLAGS=-Wall -O2 -pthread -lrt -D_FILE_OFFSET_BITS=64

all: random_writes sequential_writes random_reads sequential_reads

random_writes: random_writes.o lsvd.o
	$(CC) $(CFLAGS) random_writes.o lsvd.o -o random_writes

sequential_writes: sequential_writes.o lsvd.o
	$(CC) $(CFLAGS) sequential_writes.o lsvd.o -o sequential_writes

random_reads: random_reads.o lsvd.o
	$(CC) $(CFLAGS) random_reads.o lsvd.o -o random_reads

sequential_reads: sequential_reads.o lsvd.o
	$(CC) $(CFLAGS) sequential_reads.o lsvd.o -o sequential_reads

lsvd.o: ../lsvd/lsvd.c ../lsvd/lsvd.h
	$(CC) $(CFLAGS) -c ../lsvd/lsvd.c

random_writes.o: random_writes.c ../lsvd/lsvd.h
	$(CC) $(CFLAGS) -c random_writes.c

sequential_writes.o: sequential_writes.c ../lsvd/lsvd.h
	$(CC) $(CFLAGS) -c sequential_writes.c

random_reads.o: random_reads.c ../lsvd/lsvd.h
	$(CC) $(CFLAGS) -c random_reads.c

sequential_reads.o: sequential_reads.c ../lsvd/lsvd.h
	$(CC) $(CFLAGS) -c sequential_reads.c

clean:
	rm  *.o sequential_writes random_writes sequential_reads random_reads
