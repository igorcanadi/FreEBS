#include <stdio.h>
#include <stdint.h>

#define SECTOR_SIZE 512
#define MAX_SIZE (1 << 40) // in sectors -> 512TB
// Magic is defined somewhere as 64-bit and somewhere as 32-bit
// It has to do with compiler-dependent struct alignment.
// The actual number fits in 32 bits
#define MAGIC_NUMBER 0x60C0FFEE

struct superblock {
    uint64_t magic; // magic number. 0x60C0FFEE. 
                    // has to be 64-bit because of aligmnent
    uint64_t uid; // unique ID
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
};

struct lsvd_disk *create_lsvd(const char *pathname, uint64_t size);
struct lsvd_disk *open_lsvd(const char *pathname);
int close_lsvd(struct lsvd_disk *lsvd);
