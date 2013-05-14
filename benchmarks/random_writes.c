#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#define SIZE 5*1024*1024*1024LL // 5GB
#define BLOCK_SIZE 4*1024 // 4KB
#define TOTAL_WRITES 512*1024*1024LL // total of 512MB written

inline uint64_t rdtsc_start(void) {
    unsigned cycles_high, cycles_low;

    __asm__ __volatile__("CPUID\n\t"
            "RDTSC\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t" : "=r" (cycles_high), "=r" (cycles_low):: "%rax", "%rbx", "%rcx", "%rdx");

    return (uint64_t)cycles_high << 32 | (uint64_t)cycles_low;
}

inline uint64_t rdtsc_end(void) {
    unsigned cycles_high, cycles_low;

    __asm__ __volatile__("RDTSCP\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t" : "=r" (cycles_high), "=r" (cycles_low):: "%rax", "%rbx", "%rcx", "%rdx");

    return (uint64_t)cycles_high << 32 | (uint64_t)cycles_low;
}

char buf[BLOCK_SIZE];

int main(int argc, char **argv) {
    int fd, i, j;
    uint64_t start, end, start_offset;
    char *mfile = NULL;

    for (i = 0; i < BLOCK_SIZE; ++i) {
        buf[i] = rand() % (1<<8);
    }

    if (argc < 2) {
        printf("usage: %s filename\n", argv[0]);
        return 1;
    }
    fd = open(argv[1], O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);

    lseek(fd, SIZE-1, SEEK_SET);
    write(fd, "", 1);
    fsync(fd);

    start = rdtsc_start();
    for (i = 0; i < TOTAL_WRITES / BLOCK_SIZE; ++i) {
        start_offset = (long long)(rand() + (((long long)rand())<<32)) % (SIZE - BLOCK_SIZE);
        if (pwrite(fd, buf, BLOCK_SIZE, start_offset) != BLOCK_SIZE) {
            perror("pwrite");
            return 1;
        }
    }

    fsync(fd);
    end = rdtsc_end();
    close(fd);
    printf("%lf\n", (double)(end - start) / 2.4e9);

    return 0;
}
