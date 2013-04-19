#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

#define SECTOR_SIZE 4096
#define MAX_SIZE (1 << 40) // in sectors -> 512TB
// Magic is defined somewhere as 64-bit and somewhere as 32-bit
// It has to do with compiler-dependent struct alignment.
// The actual number fits in 32 bits
#define MAGIC_NUMBER 0x60C0FFEE

struct superblock {
    uint32_t magic; // magic number. 0x60C0FFEE.
    uint32_t uid; // unique ID
    uint64_t size; // size in sectors
    uint64_t last_checkpoint; // pointer to last checkpoint.
                              // if 0 then no last checkpoint
};

struct lsvd_disk {
    // opened file descriptor. pointer is always on
    // first byte of free space
    int fd;
    uint64_t version;
    uint64_t *sector_to_offset;
    struct superblock *sblock;
    pthread_mutex_t mutex;
};

struct lsvd_disk *create_lsvd(const char *pathname, uint64_t size);
struct lsvd_disk *open_lsvd(const char *pathname);
int read_lsvd(struct lsvd_disk *lsvd, void *buf,
        uint64_t length, uint64_t offset, uint64_t version);
int write_lsvd(struct lsvd_disk *lsvd, const void *buf,
        uint64_t length, uint64_t offset, uint64_t version);
uint64_t get_version(struct lsvd_disk *lsvd);
int close_lsvd(struct lsvd_disk *lsvd);