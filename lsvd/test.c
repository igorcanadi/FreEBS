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

    for (i = 0; i < 2*SECTOR_SIZE; ++i) {
        data[i] = 'a' + rand() % 26;
    }

    lsvd = create_lsvd("/scratch/test_disk", SIZE / SECTOR_SIZE);
    assert(lsvd != NULL);
    assert(write_lsvd(lsvd, data, 2, 11, 1) == 0);
    assert(close_lsvd(lsvd) == 0);

    lsvd = open_lsvd("/scratch/test_disk");
    assert(lsvd != NULL);
    assert(get_version(lsvd) == 1);
    assert(lsvd->sblock->size == SIZE / SECTOR_SIZE);
    assert(read_lsvd(lsvd, buffer, 2, 11, 1) == 0);
    assert(strncmp(data, buffer, SECTOR_SIZE) == 0);
    assert(close_lsvd(lsvd) == 0);

    printf("All seems to work!\n");

    return 0;
}
