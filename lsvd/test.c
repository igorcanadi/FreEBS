#include "lsvd.h"

int main() {
    struct lsvd_disk *lsvd = 
        create_lsvd("/scratch/test_disk", 2*1024*1024*1024LL);

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
        printf("%lld %lld %lld %X\n", lsvd->version, lsvd->sblock->uid,
                lsvd->sblock->size, lsvd->sblock->magic);
        close_lsvd(lsvd);
    }

    return 0;
}
