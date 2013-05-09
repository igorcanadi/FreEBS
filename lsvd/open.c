#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include "lsvd.h"
#define SIZE 2*1024*1024*1024LL // 2GB
#define MAX_SECTORS_TEST 1024*16

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

int main() {
    uint64_t start, end;
    struct lsvd_disk *lsvd;

    start = rdtsc_start();
    lsvd = open_lsvd("/scratch/test_disk");
    end = rdtsc_end();
    printf("%llu\n", get_version(lsvd));
    close_lsvd(lsvd);

    printf("%llu\n", end - start);

    return 0;
}
