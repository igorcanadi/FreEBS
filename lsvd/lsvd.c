#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include "lsvd.h"

#define CHECKPOINT_RECORD 1
#define COMMIT_RECORD 2
#define DATA_RECORD 3

struct record_descriptor {
    uint64_t magic;
    uint64_t type;
    uint64_t length;
};

struct checkpoint_record {
    uint64_t version;
    uint64_t *sector_to_offset;
};

struct data_record {
    // where on disk are we writing (in sectors)
    uint64_t disk_offset;
    // how big is the write (in sectors). 
    uint64_t length;
    // length sectors worth of data
};

struct commit_record {
    // where is the data record that are we commiting (in the file)
    uint64_t data_record_offset;
    // where on disk are we writing (in sectors)
    // has to be same as data_record->disk_offset
    uint64_t disk_offset;
    // how big is the write (in sectors). 
    // has to be same as data_record->length
    uint64_t length;
    // which version does this commit define
    uint64_t version;
};

struct lsvd_disk *create_lsvd(const char *pathname, uint64_t size) {
    struct lsvd_disk *lsvd;

    // initialize lsvd_disk struct
    lsvd = (struct lsvd_disk *) malloc(sizeof(struct lsvd_disk));
    if (lsvd == NULL) {
        return NULL;
    }

    // initialize superblock
    lsvd->sblock = (struct superblock *) malloc(sizeof(struct superblock));
    if (lsvd->sblock == NULL) {
        goto cleanup_lsvd;
    }

    // open the disk!
    lsvd->fd = open(pathname, O_SYNC | O_CREAT | O_TRUNC | O_RDWR, 
            S_IRUSR | S_IWUSR); 
    if (lsvd->fd == -1) {
        goto cleanup_sb;
    }
    
    // construct superblock
    lsvd->sblock->uid = ((uint64_t)rand()) << 32 | rand(); // unique ID
    lsvd->sblock->size = size;
    lsvd->sblock->last_checkpoint = 0;
    lsvd->sblock->magic = MAGIC_NUMBER;
    // write the superblock
    if (write(lsvd->fd, lsvd->sblock, sizeof(struct superblock)) != 
            sizeof(struct superblock)) {
        goto cleanup_sb;
    }

    // initialize sector to offset map
    lsvd->sector_to_offset = 
        (uint64_t *) malloc(sizeof(uint64_t) * lsvd->sblock->size);
    if (lsvd->sector_to_offset == NULL) {
        goto cleanup_file;
    }

    // initialize version
    lsvd->version = 0;

    // we're good!
    return lsvd;

cleanup_file:
    close(lsvd->fd);
cleanup_sb:
    free(lsvd->sblock);
cleanup_lsvd:
    free(lsvd);
    return NULL;
}

int recover_lsvd_state(struct lsvd_disk *lsvd) {
    // TODO read from checkpoint to speed up recovery
    off_t current;
    ssize_t bytes_read;
    struct record_descriptor rd;
    struct commit_record cr;
    uint64_t i;

    while (1) {
        if ((current = lseek(lsvd->fd, 0, SEEK_CUR)) < 0) {
            return -1;
        }
        bytes_read = read(lsvd->fd, &rd, sizeof(rd));
        if (bytes_read != sizeof(rd) || 
                rd.magic != MAGIC_NUMBER) {
            // error. go back, looks like we're done
            if (lseek(lsvd->fd, current, SEEK_SET) < 0) {
                return -1;
            }
            return 0;
        }

        if (rd.type == COMMIT_RECORD) {
            bytes_read = read(lsvd->fd, &cr, sizeof(cr));
            if (bytes_read != sizeof(cr)) {
                return -1;
            }
            // TODO maybe use data from data record instead of replicating
            // it in commit record
            for (i = 0; i < cr.length; ++i) {
                // update sector to offset map
                *(lsvd->sector_to_offset + (cr.disk_offset + i)) = 
                    cr.data_record_offset + sizeof(struct data_record) + 
                    i * SECTOR_SIZE;
            }
        }
        if (lseek(lsvd->fd, rd.length, SEEK_CUR) < 0) {
            return -1;
        }
    }

    return 0;
}

struct lsvd_disk *open_lsvd(const char *pathname) {
    struct lsvd_disk *lsvd;
   
    // initialize lsvd_disk struct
    lsvd = (struct lsvd_disk *) malloc(sizeof(struct lsvd_disk));
    if (lsvd == NULL) {
        return NULL;
    }

    // initialize superblock
    lsvd->sblock = (struct superblock *) malloc(sizeof(struct superblock));
    if (lsvd->sblock == NULL) {
        goto cleanup_lsvd;
    }

    // open the disk!
    lsvd->fd = open(pathname, O_SYNC | O_RDWR); 
    if (lsvd->fd == -1) {
        goto cleanup_sb;
    }

    // read the superblock
    if (read(lsvd->fd, lsvd->sblock, sizeof(struct superblock)) != 
            sizeof(struct superblock)) {
        goto cleanup_sb;
    }
    // check the magic
    if (lsvd->sblock->magic != MAGIC_NUMBER) {
        goto cleanup_file;
    }

    // initialize sector to offset map
    lsvd->sector_to_offset = 
        (uint64_t *) malloc(sizeof(uint64_t) * lsvd->sblock->size);
    if (lsvd->sector_to_offset == NULL) {
        goto cleanup_file;
    }
    
    if (recover_lsvd_state(lsvd) < 0) {
        goto cleanup_so;
    }

    // we're good!
    return lsvd;

cleanup_so:
    free(lsvd->sector_to_offset);
cleanup_file:
    close(lsvd->fd);
cleanup_sb:
    free(lsvd->sblock);
cleanup_lsvd:
    free(lsvd);
    return NULL;
}

int close_lsvd(struct lsvd_disk *lsvd) {
    int ret;
    // TODO maybe do checkpoint here?
    ret = close(lsvd->fd);
    free(lsvd->sblock);
    free(lsvd->sector_to_offset);
    free(lsvd);
    return ret;
}
