#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "../lsvd/lsvd.h"
#define SIZE (5*1024*1024*1024LL) // 5GB
#define BLOCK_SIZE (4*1024) // 4KB
#define TOTAL_WRITES (128*1024*1024LL) // total of 128MB written

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
    uint64_t start, end, start_offset;

    for (i = 0; i < BLOCK_SIZE; ++i) {
        buf[i] = rand() % (1<<8);
    }

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
    for (i = 0; i < TOTAL_WRITES / BLOCK_SIZE; ++i) {
        start_offset = (long long)(rand() + (((long long)rand())<<32)) % ((SIZE - BLOCK_SIZE)/SECTOR_SIZE);
#ifdef LSVD
        assert(write_lsvd(lsvd, buf, BLOCK_SIZE / SECTOR_SIZE, start_offset, v+i+1) == 0);
#else
        assert(pwrite(fd, buf, BLOCK_SIZE, start_offset * SECTOR_SIZE) == BLOCK_SIZE);
#endif
    }

#ifdef LSVD
    fsync_lsvd(lsvd);
#else
    fsync(fd);
#endif
    end = rdtsc_end();
#ifdef LSVD
    close_lsvd(lsvd);
#else
    close(fd);
#endif
    printf("%lf\n", (double)(end - start) / 2.4e9);

    return 0;
}
