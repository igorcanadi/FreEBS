#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <fcntl.h>
#include "lsvd.h"

#define COMMIT_RECORD 1
#define DATA_RECORD 2
#define SIZEOF_CHECKPOINT(SIZE) (2 * sizeof(uint64_t) + sizeof(uint64_t) * (SIZE))
// where is the first byte written in a new disk
// new disk starts with superblock and two checkpoint buffers
#define NEW_DISK_OFFSET(SIZE) (sizeof(struct superblock) + \
    2 * SIZEOF_CHECKPOINT(SIZE))

struct record_descriptor {
    uint32_t magic;
    uint32_t type;
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
    // which version does this commit define
    uint64_t version;
    // checksum of <data_record, data>
    uint64_t checksum;
};

uint64_t checksum(const struct data_record *dr, const char *data) {
    // TODO
    return 0;
}

// offset is the next free offset on the disk
// has to be called with mutex lock held
int dump_checkpoint(struct lsvd_disk *lsvd, uint64_t offset) {
    // TODO make this spread out

    // write offset
    if (write(lsvd->fd, &offset, sizeof(offset)) != sizeof(offset)) {
        return -1;
    }
    // write version
    if (write(lsvd->fd, &lsvd->version, sizeof(lsvd->version)) !=
            sizeof(lsvd->version)) {
        return -1;
    }
    // write sector_to_offset map
    if (write(lsvd->fd, lsvd->sector_to_offset, sizeof(uint64_t) *
            lsvd->sblock->size) != sizeof(uint64_t) * lsvd->sblock->size) {
        return -1;
    }

    return 0;
}

// reads from current fd pointer
// at the end, the pointer is at next un-checkpointed block
// has to be called with mutex lock held
int read_checkpoint(struct lsvd_disk *lsvd) {
    uint64_t offset;

    // read offset
    if (read(lsvd->fd, &offset, sizeof(offset)) != sizeof(offset)) {
        return -1;
    }
    // read version
    if (read(lsvd->fd, &lsvd->version, sizeof(lsvd->version)) !=
            sizeof(lsvd->version)) {
        return -1;
    }
    // read sector_to_offset map
    if (read(lsvd->fd, lsvd->sector_to_offset, sizeof(uint64_t) *
            lsvd->sblock->size) != sizeof(uint64_t) *
            lsvd->sblock->size) {
        return -1;
    }


    // seek to first un-checkpointed block
    if (lseek(lsvd->fd, offset, SEEK_SET) < 0) {
        return -1;
    }

    return 0;
}

struct lsvd_disk *create_lsvd(const char *pathname, uint64_t size) {
    struct lsvd_disk *lsvd;

    // initialize lsvd_disk struct
    lsvd = (struct lsvd_disk *) malloc(sizeof(struct lsvd_disk));
    if (lsvd == NULL) {
        return NULL;
    }

    // initialize mutex
    if (pthread_mutex_init(&lsvd->mutex, NULL) < 0) {
        goto cleanup_lsvd;
    }

    // initialize superblock
    lsvd->sblock = (struct superblock *) malloc(sizeof(struct superblock));
    if (lsvd->sblock == NULL) {
        goto cleanup_mutex;
    }

    // open the disk!
    lsvd->fd = open(pathname, O_CREAT | O_TRUNC | O_RDWR,
            S_IRUSR | S_IWUSR);
    if (lsvd->fd == -1) {
        goto cleanup_sb;
    }

    // construct superblock
    lsvd->sblock->uid = rand(); // unique ID
    lsvd->sblock->size = size;
    lsvd->sblock->last_checkpoint = sizeof(struct superblock);
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

    // offset is the next free space after two checkpoints get written
    if (dump_checkpoint(lsvd, NEW_DISK_OFFSET(lsvd->sblock->size)) < 0) {
        goto cleanup_sto;
    }
    // we need two checkpoints, the other one is used for writing out a new
    // checkpoint without blocking
    if (dump_checkpoint(lsvd, NEW_DISK_OFFSET(lsvd->sblock->size)) < 0) {
        goto cleanup_sto;
    }

    // we're good!
    return lsvd;

cleanup_sto:
    free(lsvd->sector_to_offset);
cleanup_file:
    close(lsvd->fd);
cleanup_sb:
    free(lsvd->sblock);
cleanup_mutex:
    pthread_mutex_destroy(&lsvd->mutex);
cleanup_lsvd:
    free(lsvd);
    return NULL;
}

// has to be called with mutex lock held
int recover_lsvd_state(struct lsvd_disk *lsvd) {
    off_t current;
    ssize_t bytes_read;
    struct record_descriptor rd;
    struct commit_record cr;
    struct data_record dr;
    char *data;
    uint64_t dr_checksum;
    uint64_t i;

    if (read_checkpoint(lsvd) < 0) {
        return -1;
    }


    while (1) {
        // figure out where we are
        if ((current = lseek(lsvd->fd, 0, SEEK_CUR)) < 0) {
            return -1;
        }

        // read in record descriptor
        bytes_read = read(lsvd->fd, &rd, sizeof(rd));
        if (bytes_read != sizeof(rd) || rd.magic != MAGIC_NUMBER) {
            break;
        }

        if (rd.type == DATA_RECORD) {
            // read data record
            bytes_read = read(lsvd->fd, &dr, sizeof(dr));
            if (bytes_read != sizeof(dr)) {
                break;
            }
            // read data
            data = (char *) malloc(dr.length * SECTOR_SIZE);
            bytes_read = read(lsvd->fd, data, dr.length * SECTOR_SIZE);
            if (bytes_read != dr.length * SECTOR_SIZE) {
                break;
            }

            dr_checksum = checksum(&dr, data);
        } else if (rd.type == COMMIT_RECORD) {
            // we're assuming DATA_RECORD came before this and there
            // are meaningful values in dr and dr_checksum

            // read commit record
            bytes_read = read(lsvd->fd, &cr, sizeof(cr));
            if (bytes_read != sizeof(cr)) {
                break;
            }

            // check checksum
            if (cr.checksum != dr_checksum) {
                break;
            }

            // update sector to offset map
            for (i = 0; i < dr.length; ++i) {
                *(lsvd->sector_to_offset + (dr.disk_offset + i)) =
                    cr.data_record_offset + sizeof(struct data_record) +
                    i * SECTOR_SIZE;
            }

            // update version
            lsvd->version = cr.version;
        }
    }

    // rewind
    if (lseek(lsvd->fd, current, SEEK_SET) < 0) {
        return -1;
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

    // initialize mutex. we don't need to lock it since nobody
    // can access it yet
    if (pthread_mutex_init(&lsvd->mutex, NULL) < 0) {
        goto cleanup_lsvd;
    }

    // initialize superblock
    lsvd->sblock = (struct superblock *) malloc(sizeof(struct superblock));
    if (lsvd->sblock == NULL) {
        goto cleanup_mutex;
    }

    // open the disk!
    lsvd->fd = open(pathname, O_RDWR);
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
cleanup_mutex:
    pthread_mutex_destroy(&lsvd->mutex);
cleanup_lsvd:
    free(lsvd);
    return NULL;
}

// length and offset are in sectors
// version has to be current_version+1, otherwise returns error
// returns 0 on OK, -1 on error
int write_lsvd(struct lsvd_disk *lsvd, const void *buf,
        uint64_t length, uint64_t offset, uint64_t version) {
    uint64_t i;
    struct data_record dr;
    struct commit_record cr;
    struct record_descriptor rd;
    int ret = 0;

    if (pthread_mutex_lock(&lsvd->mutex) < 0) {
        return -1;
    }

    if (version != lsvd->version + 1) {
        // we got an out-of-order write!
        ret = -1;
        goto unlock;
    }

    // construct data record
    dr.disk_offset = offset;
    dr.length = length;

    // construct record descriptor for data record
    rd.magic = MAGIC_NUMBER;
    rd.type = DATA_RECORD;

    // write record descriptor for data record
    if (write(lsvd->fd, &rd, sizeof(rd)) != sizeof(rd)) {
        ret = -1;
        goto unlock;
    }

    // construct commit record
    if ((cr.data_record_offset = lseek(lsvd->fd, 0, SEEK_CUR)) < 0) {
        ret = -1;
        goto unlock;
    }
    cr.version = version;
    cr.checksum = checksum(&dr, buf);

    // write data record
    if (write(lsvd->fd, &dr, sizeof(dr)) != sizeof(dr)) {
        ret = -1;
        goto unlock;
    }
    // write data
    if (write(lsvd->fd, buf, dr.length * SECTOR_SIZE) !=
            dr.length * SECTOR_SIZE) {
        ret = -1;
        goto unlock;
    }

    // construct record descriptor for commit record
    // magic is already set
    rd.type = COMMIT_RECORD;

    // write record descriptor for commit record
    if (write(lsvd->fd, &rd, sizeof(rd)) != sizeof(rd)) {
        ret = -1;
        goto unlock;
    }

    // write commit record
    if (write(lsvd->fd, &cr, sizeof(cr)) != sizeof(cr)) {
        ret = -1;
        goto unlock;
    }

    // fsync
    if (fsync(lsvd->fd) < 0) {
        ret = -1;
        goto unlock;
    }
    // the data is now on disk!

    // update sector_to_offset
    for (i = 0; i < dr.length; ++i) {
        *(lsvd->sector_to_offset + (dr.disk_offset + i)) =
            cr.data_record_offset + sizeof(struct data_record) +
            i * SECTOR_SIZE;
    }
    // bump the version
    lsvd->version = version;

unlock:
    if (pthread_mutex_unlock(&lsvd->mutex) < 0) {
        return -1;
    }

    return ret;
}

// length and offset in sectors
// version has to be current_version, otherwise returns error
// returns 0 on OK, -1 on error
int read_lsvd(struct lsvd_disk *lsvd, void *buf,
        uint64_t length, uint64_t offset, uint64_t version) {
    uint64_t file_offset;
    uint64_t i;
    int ret = 0;

    if (pthread_mutex_lock(&lsvd->mutex) < 0) {
        return -1;
    }

    if (version != lsvd->version) {
        // we don't have this version, noooo!
        ret = -1;
        goto unlock;
    }

    for (i = 0; i < length; ++i) {
        file_offset = *(lsvd->sector_to_offset + offset + i);
        if (file_offset == 0) {
            // nobody wrote anything here. we can just return all zeros
            if (memset(buf + i * SECTOR_SIZE, 0, SECTOR_SIZE) < 0) {
                ret = -1;
                goto unlock;
            }
            continue;
        }
        if (pread(lsvd->fd, buf + i * SECTOR_SIZE, SECTOR_SIZE, file_offset)
                != SECTOR_SIZE) {
            ret = -1;
            goto unlock;
        }
    }

unlock:
    if (pthread_mutex_unlock(&lsvd->mutex) < 0) {
        return -1;
    }

    return ret;
}

uint64_t get_version(struct lsvd_disk *lsvd) {
    // i hope i don't have to lock this
    return lsvd->version;
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
