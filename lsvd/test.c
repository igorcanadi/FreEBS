#include "lsvd.h"
#define SIZE 2*1024*1024*1024LL // 2GB

int main() {
    struct lsvd_disk *lsvd = 
        create_lsvd("/scratch/test_disk", SIZE / SECTOR_SIZE);

    if (lsvd == NULL) {
        printf("Doesn't work!\n");
    } else {
        printf("Works!\n");
        close_lsvd(lsvd);
    }

    lsvd = open_lsvd("/scratch/test_disk");

    if (lsvd == NULL) {
        printf("Doesn't work!\n");
    } else {
        printf("Works!\n");
        printf("%lld %d %lld %X\n", lsvd->version, lsvd->sblock->uid,
                lsvd->sblock->size, lsvd->sblock->magic);
        close_lsvd(lsvd);
    }

    return 0;
}
