#include <stdio.h>
#include "lsvd.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: %s old_lsvd_file new_lsvd_file\n", argv[0]);
    }

    return cleanup_lsvd(argv[1], argv[2]);
}
