#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include "lsvd.h"
#define SIZE 20*1024*1024*1024LL // 20GB

volatile sig_atomic_t eflag = 0;

void handleExit(int sig){
    eflag = 1;
}

void generate_random(char *buf, int len) {
    int i;
    for (i = 0; i < len; ++i) {
        buf[i] = 'a' + rand() % 26;
    }
}

int main() {
    uint64_t i;
    struct lsvd_disk *lsvd;
    struct sigaction act;
    char *data = (char *)malloc(SECTOR_SIZE * 10);

    act.sa_handler = handleExit;
    sigemptyset(&act.sa_mask);
    sigaddset(&act.sa_mask, SIGINT);
    sigaddset(&act.sa_mask, SIGTERM);
    sigaddset(&act.sa_mask, SIGQUIT);
    sigaddset(&act.sa_mask, SIGABRT);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGABRT, &act, NULL);

    generate_random(data, SECTOR_SIZE * 10);
    lsvd = create_lsvd("/scratch/test_disk", SIZE / SECTOR_SIZE);

    for (i = 0; ; ++i) {
        if (eflag == 1) {
            printf("%llu\n", get_version(lsvd));
            return 0;
        }
        assert(write_lsvd(lsvd, data, 10, rand() % (SIZE / SECTOR_SIZE - 15), i+1) == 0);
    }

    close_lsvd(lsvd);
    free(data);

    return 0;
}
