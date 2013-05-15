#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "../lsvd/lsvd.h"
#define SIZE 5*1024*1024*1024LL // 5GB
#define BLOCK_SIZE 40*1024 // 40KB

//#define LSVD

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
#ifdef LSVD
    struct lsvd_disk *lsvd;
    uint64_t v;
#else
    int fd;
#endif
    long long i;
    uint64_t start, end;

    printf("did you delete the cache?\n");
    if (argc < 2) {
        printf("usage: %s filename\n", argv[0]);
        return 1;
    }
#ifdef LSVD
    lsvd = open_lsvd(argv[1]);
    v = get_version(lsvd);
#else
    fd = open(argv[1], O_RDWR);
#endif

    start = rdtsc_start();
    for (i = 0; i < SIZE / BLOCK_SIZE; ++i) {
#ifdef LSVD
        assert(read_lsvd(lsvd, buf, 
                    BLOCK_SIZE/SECTOR_SIZE, 
                    i * (BLOCK_SIZE/SECTOR_SIZE), v) == 0);
#else
        assert(read(fd, buf, BLOCK_SIZE) != BLOCK_SIZE);
#endif
    }

    end = rdtsc_end();
#ifdef LSVD
    close_lsvd(lsvd);
#else
    close(fd);
#endif
    printf("%lf\n", (double)(end - start) / 2.4e9);

    return 0;
}
