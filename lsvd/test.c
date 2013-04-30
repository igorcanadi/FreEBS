#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include "lsvd.h"
#define SIZE 2*1024*1024LL // 2MB

char data[2*SECTOR_SIZE];
char buffer[2*SECTOR_SIZE];

int main() {
    int i;
    struct lsvd_disk *lsvd;
    char *writes_buf;
    size_t size;

    for (i = 0; i < 2*SECTOR_SIZE; ++i) {
        data[i] = 'a' + rand() % 26;
    }

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

    for (i = 0; i < 2*SECTOR_SIZE; ++i) {
        data[i] = 'a' + rand() % 26;
    }
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

    printf("All seems to work!\n");

    return 0;
}
