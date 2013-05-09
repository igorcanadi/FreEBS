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

void generate_random(char *buf, int len) {
    int i;
    for (i = 0; i < len; ++i) {
        buf[i] = 'a' + rand() % 26;
    }
}

void basic_test() {
    struct lsvd_disk *lsvd;
    char *writes_buf;
    size_t size;
    char *data, *buffer;
    data = (char *)malloc(2*SECTOR_SIZE);
    buffer = (char *)malloc(2*SECTOR_SIZE);

    generate_random(data, 2*SECTOR_SIZE);

    lsvd = create_lsvd("/scratch/test_disk", SIZE / SECTOR_SIZE);
    assert(lsvd != NULL);
    assert(write_lsvd(lsvd, data, 2, 11, 1) == 0);
    assert(write_lsvd(lsvd, data, 2, 11, 2) == 0);
    assert(write_lsvd(lsvd, data, 1, 11, 3) == 0);
    assert(close_lsvd(lsvd) == 0);

    lsvd = open_lsvd("/scratch/test_disk");
    assert(lsvd != NULL);
    assert(get_version(lsvd) == 3);
    assert(lsvd->sblock->size == SIZE / SECTOR_SIZE);
    assert(read_lsvd(lsvd, buffer, 2, 11, 3) == 0);
    assert(strncmp(data, buffer, 2*SECTOR_SIZE) == 0);
    assert(close_lsvd(lsvd) == 0);

    assert(cleanup_lsvd("/scratch/test_disk", "/scratch/test_disk_smaller")
            == 0);

    lsvd = open_lsvd("/scratch/test_disk_smaller");
    assert(lsvd != NULL);
    assert(get_version(lsvd) == 3);
    assert(lsvd->sblock->size == SIZE / SECTOR_SIZE);
    assert(read_lsvd(lsvd, buffer, 2, 11, 3) == 0);
    assert(strncmp(data, buffer, 2*SECTOR_SIZE) == 0);

    generate_random(data, 2*SECTOR_SIZE);
    // add new writes
    assert(write_lsvd(lsvd, data, 2, 10, 4) == 0);
    assert(write_lsvd(lsvd, data, 1, 12, 5) == 0);

    assert(read_lsvd(lsvd, buffer, 2, 10, 5) == 0);
    assert(strncmp(data, buffer, 2*SECTOR_SIZE) == 0);
    assert(read_lsvd(lsvd, buffer, 1, 12, 5) == 0);
    assert(strncmp(data, buffer, SECTOR_SIZE) == 0);
    writes_buf = get_writes_lsvd(lsvd, 4, &size);
    assert(writes_buf != NULL);
    assert(close_lsvd(lsvd) == 0);

    lsvd = open_lsvd("/scratch/test_disk");
    assert(put_writes_lsvd(lsvd, 4, writes_buf, size) == 0);
    assert(close_lsvd(lsvd) == 0);

    lsvd = open_lsvd("/scratch/test_disk");
    assert(read_lsvd(lsvd, buffer, 1, 10, 5) == 0);
    assert(strncmp(data, buffer, 2*SECTOR_SIZE) == 0);
    assert(read_lsvd(lsvd, buffer, 1, 12, 5) == 0);
    assert(strncmp(data, buffer, SECTOR_SIZE) == 0);
    assert(close_lsvd(lsvd) == 0);
    free(writes_buf);

    free(data);
    free(buffer);

    printf("All seems to work!\n");
}

void measure_speed_of_one_write(struct lsvd_disk *lsvd, char *data) {
    int i, version = 0;
    uint64_t start_time, end_time;

    for (i = 1; i <= MAX_SECTORS_TEST; ++i) {
        start_time = rdtsc_start();
        assert(write_lsvd(lsvd, data, 1, 0, ++version) == 0);
        end_time = rdtsc_end();
        printf("%d %llu\n", i, end_time - start_time);
    }
}

void measure_increasing_buffer_sizes(struct lsvd_disk *lsvd, char *data) {
    int i, version = 0;
    uint64_t start_time, end_time;

    for (i = 1; i <= MAX_SECTORS_TEST; ++i) {
        start_time = rdtsc_start();
        assert(write_lsvd(lsvd, data, i, 0, ++version) == 0);
        end_time = rdtsc_end();
        printf("%d %llu\n", i, end_time - start_time);
    }
}

void measure_constant_buffer_sizes(struct lsvd_disk *lsvd, char *data) {
    int i, j, version = 0;
    uint64_t start_time, end_time;

    for (i = 1; i <= MAX_SECTORS_TEST; ++i) {
        start_time = rdtsc_start();
        for (j = 0; j < i; ++j) {
            assert(write_lsvd(lsvd, data, 1, j, ++version) == 0);
        }
        end_time = rdtsc_end();
        printf("%d %llu\n", i, end_time - start_time);
    }
}


void benchmarks() {
    struct lsvd_disk *lsvd;
    char *data = (char *)malloc(MAX_SECTORS_TEST*SECTOR_SIZE);

    generate_random(data, MAX_SECTORS_TEST*SECTOR_SIZE);
    lsvd = create_lsvd("/scratch/test_disk", SIZE / SECTOR_SIZE);

    //measure_speed_of_one_write(lsvd, data);
    //measure_increasing_buffer_sizes(lsvd, data);
    measure_constant_buffer_sizes(lsvd, data);

    close_lsvd(lsvd);
    free(data);
}

void test_checkpointing() {
    int i;
    struct lsvd_disk *lsvd;
    char *data = (char *)malloc(SECTOR_SIZE * 100);

    generate_random(data, SECTOR_SIZE * 100);
    lsvd = create_lsvd("/scratch/test_disk", SIZE / SECTOR_SIZE);

    for (i = 0; i < 50000; ++i) {
        assert(write_lsvd(lsvd, data, 100, 0, i+1) == 0);
    }

    close_lsvd(lsvd);
    free(data);
}

int main() {
    //basic_test();
    benchmarks();
    //test_checkpointing();

    return 0;
}
