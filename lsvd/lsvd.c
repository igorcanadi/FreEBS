#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <fcntl.h>
#include "lsvd.h"

#define COMMIT_RECORD 1
#define DATA_RECORD 2
#define SIZEOF_STO(SIZE) (sizeof(uint64_t) * (SIZE))
#define SIZEOF_CHECKPOINT(SIZE) (2 * sizeof(uint64_t) + \
        SIZEOF_STO(SIZE))
// NUM is 0 for first checkpoint and 1 for second checkpoint
#define CHECKPOINT_OFFSET(NUM, SIZE) (sizeof(struct superblock) + \
        NUM * SIZEOF_CHECKPOINT(SIZE))
// where is the first byte written in a new disk
// new disk starts with superblock and two checkpoint buffers
#define FIRST_RECORD_OFFSET(SIZE) CHECKPOINT_OFFSET(2, SIZE)
#define SIZEOF_DATA_COMMIT(LENGTH) sizeof(struct record_descriptor) * 2 + \
        sizeof(struct data_record) + sizeof(struct commit_record) + \
        LENGTH * SECTOR_SIZE

#define CHECKPOINT_BATCH_SIZE (8*1024*1024) // 8MB

#define CHECKPOINT_INIT          (1 << 0)
#define CHECKPOINT_FORCED        (1 << 1)
#define CHECKPOINT_IN_PROGRESS   (1 << 2)
#define CHECKPOINT_EXIT          (1 << 3)
#define CHECKPOINT_FAILED        (1 << 4)

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

// fletcher's checksum
uint64_t checksum(const struct data_record *dr, const void *data) {
    int i;
    uint32_t sum1 = 0, sum2 = 0;

    for (i = 0; 4*i < sizeof(*dr); ++i) {
        sum2 += sum1 += *(((uint32_t *)dr) + i);
    }

    for (i = 0; 4*i < dr->length * SECTOR_SIZE; ++i) {
        sum2 += sum1 += *(((uint32_t *)data) + i);
    }

    return ((uint64_t)sum2 << 32) | (uint64_t)sum1;
}

// offset is the next free offset on the disk
// has to be called with mutex lock held
// this function is called only when we are creating a disk
int dump_checkpoint(struct lsvd_disk *lsvd, uint64_t offset) {
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
    if (write(lsvd->fd, lsvd->sector_to_offset, SIZEOF_STO(lsvd->sblock->size))
            != SIZEOF_STO(lsvd->sblock->size)) {
        return -1;
    }

    // we NEED to fsync after a checkpoint is written
    if (fsync(lsvd->fd) < 0) {
        return -1;
    }

    return 0;
}

// this is only called from inside the thread
// need to hold the mutex!
int do_checkpoint_thread(struct lsvd_disk *lsvd) {
    uint64_t offset, current_offset;
    size_t i, batch;
    ssize_t bytes_written;

    // figure out where we are going to write offset
    // we're alternating
    if (lsvd->sblock->last_checkpoint ==
            CHECKPOINT_OFFSET(1, lsvd->sblock->size)) {
        lsvd->sblock->last_checkpoint = offset =
            CHECKPOINT_OFFSET(0, lsvd->sblock->size);
    } else {
        lsvd->sblock->last_checkpoint = offset =
            CHECKPOINT_OFFSET(1, lsvd->sblock->size);
    }

    // where are we currently?
    current_offset = lseek(lsvd->fd, 0, SEEK_CUR);

    // write offset
    if (pwrite(lsvd->fd, &current_offset, sizeof(current_offset), offset)
            != sizeof(current_offset)) {
        return -1;
    }
    offset += sizeof(current_offset);

    // write version
    if (pwrite(lsvd->fd, &lsvd->version, sizeof(lsvd->version), offset)
            != sizeof(lsvd->version)) {
        return -1;
    }
    offset += sizeof(lsvd->version);

    // write sector_to_offset map
    for (i = 0; i < SIZEOF_STO(lsvd->sblock->size); i += batch) {
        // let the other kids play, too
        if (pthread_mutex_unlock(&lsvd->mutex) < 0) {
            return -1;
        }

        batch = CHECKPOINT_BATCH_SIZE;
        if (batch + i > SIZEOF_STO(lsvd->sblock->size)) {
            // final batch
            batch = SIZEOF_STO(lsvd->sblock->size) - i;
        }

        // back to us!
        if (pthread_mutex_lock(&lsvd->mutex) < 0) {
            return -1;
        }

        bytes_written = pwrite(lsvd->fd, (char *)lsvd->sector_to_offset + i,
                batch, offset);
        if (bytes_written < 0) {
            return -1;
        }
        offset += bytes_written;
    }

    // let the other kids play, too
    if (pthread_mutex_unlock(&lsvd->mutex) < 0) {
        return -1;
    }
    // back to us!
    if (pthread_mutex_lock(&lsvd->mutex) < 0) {
        return -1;
    }

    // dump checkpoint to disk. this needs to happen if we want consistency
    if (fsync(lsvd->fd) < 0) {
        return -1;
    }

    // write superblock
    // lsvd->sblock->last_checkpoint is already set to the right value earlier
    if (pwrite(lsvd->fd, lsvd->sblock, sizeof(struct superblock), 0) !=
            sizeof(struct superblock)) {
        // well we failed writing the superblock. who knows what that means
        return -1;
    }

    return 0;
}

void *checkpoint_thread(void *lsvd_v) {
    struct timespec ts;
    int ret = 0;
    struct lsvd_disk *lsvd = (struct lsvd_disk *)lsvd_v;

    if (pthread_mutex_lock(&lsvd->mutex) < 0) {
        lsvd->checkpoint_state |= CHECKPOINT_FAILED;
        pthread_exit(NULL);
    }

    // set initialized flag - we have the thread running!
    lsvd->checkpoint_state |= CHECKPOINT_INIT;

    while (1) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += CHECKPOINT_FREQUENCY;

        // if timeout isn't done (ret will not be zero when timeout is done)
        // if checkpoint is not forced
        while (ret == 0 && !(lsvd->checkpoint_state & CHECKPOINT_FORCED)) {
            if (lsvd->checkpoint_state & CHECKPOINT_EXIT) {
                // exit has been initiated
                pthread_mutex_unlock(&lsvd->mutex);
                pthread_exit(NULL);
            }
            ret = pthread_cond_timedwait(&lsvd->checkpoint_cond,
                    &lsvd->mutex, &ts);
        }

        // clear forced flag
        lsvd->checkpoint_state &= ~CHECKPOINT_FORCED;
        // set in progress flag
        lsvd->checkpoint_state |= CHECKPOINT_IN_PROGRESS;

        // CHECKPOINT
        if (do_checkpoint_thread(lsvd) < 0) {
            // set failed flag
            lsvd->checkpoint_state |= CHECKPOINT_FAILED;
        }

        // clear in progress flag
        lsvd->checkpoint_state &= ~CHECKPOINT_IN_PROGRESS;
    }
}

// has to be called with mutex lock held
int start_checkpoint_thread(struct lsvd_disk *lsvd) {
    if (pthread_cond_init(&lsvd->checkpoint_cond, NULL) != 0) {
        return -1;
    }

    lsvd->checkpoint_state = 0;

    if (pthread_create(&lsvd->checkpoint_t, NULL, checkpoint_thread,
                (void *)lsvd) != 0) {
        pthread_cond_destroy(&lsvd->checkpoint_cond);
        return -1;
    }
    return 0;
}

// has to be called with mutex lock held
int force_checkpoint(struct lsvd_disk *lsvd) {
    // force checkpoint flag
    lsvd->checkpoint_state |= CHECKPOINT_FORCED;

    // wake up the thread
    if (pthread_cond_signal(&lsvd->checkpoint_cond) != 0) {
        return -1;
    }

    return 0;
}

// has to be called with mutex lock held
// returns only after the checkpoint is done
int stop_checkpoint_thread(struct lsvd_disk *lsvd) {
    // initiated exit flag
    lsvd->checkpoint_state |= CHECKPOINT_EXIT;

    if (pthread_cond_signal(&lsvd->checkpoint_cond) != 0) {
        return -1;
    }

    if (pthread_mutex_unlock(&lsvd->mutex) < 0) {
        return -1;
    }

    if (pthread_join(lsvd->checkpoint_t, NULL) != 0) {
        return -1;
    }

    if (pthread_mutex_lock(&lsvd->mutex) < 0) {
        return -1;
    }

    // clean up
    pthread_cond_destroy(&lsvd->checkpoint_cond);

    return 0;
}

// reads from current fd pointer
// at the end, the pointer is at next un-checkpointed block
// has to be called with mutex lock held
int read_checkpoint(struct lsvd_disk *lsvd) {
    uint64_t offset;

    if (lseek(lsvd->fd, lsvd->sblock->last_checkpoint, SEEK_SET) < 0) {
        return -1;
    }

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
    lsvd->sblock->last_checkpoint = CHECKPOINT_OFFSET(0, lsvd->sblock->size);
    lsvd->sblock->magic = MAGIC_NUMBER;
    // write the superblock
    if (write(lsvd->fd, lsvd->sblock, sizeof(struct superblock)) !=
            sizeof(struct superblock)) {
        goto cleanup_sb;
    }

    // initialize sector to offset map
    lsvd->sector_to_offset =
        (uint64_t *) calloc(sizeof(uint64_t), lsvd->sblock->size);
    if (lsvd->sector_to_offset == NULL) {
        goto cleanup_file;
    }

    // initialize version
    lsvd->version = 0;

    // offset is the next free space after two checkpoints get written
    if (dump_checkpoint(lsvd, FIRST_RECORD_OFFSET(lsvd->sblock->size)) < 0) {
        goto cleanup_sto;
    }
    // we need two checkpoints, the other one is used for writing out a new
    // checkpoint without blocking
    if (dump_checkpoint(lsvd, FIRST_RECORD_OFFSET(lsvd->sblock->size)) < 0) {
        goto cleanup_sto;
    }

    if (start_checkpoint_thread(lsvd) < 0) {
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
    uint64_t i;
    // just to eliminate warning, 0 value should never be used
    uint64_t dr_checksum = 0;

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

            free(data);
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

    if (start_checkpoint_thread(lsvd) < 0) {
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

int copy_data(int src_fd, int dest_fd, size_t size, off_t offset) {
    int ret = 0;
    char *data = (char *) malloc(size);
    if (data == NULL) {
        return -1;
    }
    if (pread(src_fd, data, size, offset) != size) {
        ret = -1;
        goto free_data;
    }
    if (write(dest_fd, data, size) != size) {
        ret = -1;
    }

free_data:
    free(data);
    return ret;
}

// get rid of unused sectors in the file
int cleanup_lsvd(const char *old_pathname, const char *new_pathname) {
    struct lsvd_disk *lsvd;
    uint64_t *new_sector_to_offset;
    uint64_t new_current;
    off_t current;
    ssize_t bytes_read;
    struct record_descriptor rd;
    struct commit_record cr;
    struct data_record dr;
    uint64_t i;
    int is_used;
    uint64_t version = 0;
    int ret = 0;
    int new_fd = 0;

    // open old disk
    if ((lsvd = open_lsvd(old_pathname)) == NULL) {
        return -1;
    }

    // open new disk
    new_fd = open(new_pathname, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
    if (new_fd < 0) {
        ret = -1;
        goto close_old;
    }

    if (pthread_mutex_lock(&lsvd->mutex) < 0) {
        ret = -1;
        goto close_new;
    }

    // copy superblock and checkpoints
    if (lseek(lsvd->fd, 0, SEEK_SET) < 0) {
        ret = -1;
        goto unlock;
    }
    if (copy_data(lsvd->fd, new_fd, FIRST_RECORD_OFFSET(lsvd->sblock->size),
                0) < 0) {
        ret = -1;
        goto unlock;
    }

    // allocate new sector to offset map
    new_sector_to_offset =
        (uint64_t *) calloc(sizeof(uint64_t), lsvd->sblock->size);
    if (new_sector_to_offset == NULL) {
        ret = -1;
        goto unlock;
    }

    // this is where we should be
    current = FIRST_RECORD_OFFSET(lsvd->sblock->size);
    new_current = FIRST_RECORD_OFFSET(lsvd->sblock->size);

    while (version < lsvd->version) {
        // seek to current
        if (lseek(lsvd->fd, current, SEEK_SET) < 0) {
            ret = -1;
            break;
        }

        // read in record descriptor
        bytes_read = read(lsvd->fd, &rd, sizeof(rd));
        if (bytes_read != sizeof(rd) || rd.magic != MAGIC_NUMBER) {
            ret = -1;
            break;
        }

        if (rd.type != DATA_RECORD) {
            // something went wrong here
            ret = -1;
            break;
        }

        // read data record
        bytes_read = read(lsvd->fd, &dr, sizeof(dr));
        if (bytes_read != sizeof(dr)) {
            ret = -1;
            break;
        }

        // check if used
        is_used = 0;
        for (i = 0; i < dr.length; ++i) {
            if (*(lsvd->sector_to_offset + (dr.disk_offset + i)) <=
                    current + sizeof(rd) + sizeof(dr) + i * SECTOR_SIZE) {
                is_used = 1;
                break;
            }
        }

        // if used, copy the data
        if (is_used) {
            // update new_sector_to_offset
            for (i = 0; i < dr.length; ++i) {
                *(new_sector_to_offset + (dr.disk_offset + i)) =
                    new_current + sizeof(rd) + sizeof(dr) + i * SECTOR_SIZE;
            }
            // copy data over
            if (copy_data(lsvd->fd, new_fd,
                        SIZEOF_DATA_COMMIT(dr.length), current) < 0) {
                ret = -1;
                break;
            }
            new_current += SIZEOF_DATA_COMMIT(dr.length);
        }

        // read commit record
        bytes_read = pread(lsvd->fd, &cr, sizeof(cr), current +
                SIZEOF_DATA_COMMIT(dr.length) - sizeof(cr));
        if (bytes_read != sizeof(cr)) {
            ret = -1;
            break;
        }
        // update version
        version = cr.version;

        // update current
        current += SIZEOF_DATA_COMMIT(dr.length);
    }

    if (ret == 0) {
        // -------- write checkpoint ---------
        if (lseek(new_fd, lsvd->sblock->last_checkpoint, SEEK_SET) < 0) {
            ret = -1;
            goto clear_sector_to_offset;
        }
        // write offset
        if (write(new_fd, &new_current, sizeof(new_current)) !=
                sizeof(new_current)) {
            ret = -1;
            goto clear_sector_to_offset;
        }
        // write version
        if (write(new_fd, &version, sizeof(version)) != sizeof(version)) {
            ret = -1;
            goto clear_sector_to_offset;
        }
        // write sector_to_offset map
        if (write(new_fd, new_sector_to_offset, SIZEOF_STO(lsvd->sblock->size))
                != SIZEOF_STO(lsvd->sblock->size)) {
            ret = -1;
            goto clear_sector_to_offset;
        }
    }

clear_sector_to_offset:
    free(new_sector_to_offset);
unlock:
    pthread_mutex_unlock(&lsvd->mutex);
close_new:
    close(new_fd);
close_old:
    close_lsvd(lsvd);

    return ret;
}

// length and offset are in sectors
// version has to be current_version+1, otherwise returns error
// returns 0 on OK, -1 on error
int write_lsvd(struct lsvd_disk *lsvd, const char *buf,
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
int read_lsvd(struct lsvd_disk *lsvd, char *buf,
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

int fsync_lsvd(struct lsvd_disk *lsvd) {
    int ret = 0;
    if (pthread_mutex_lock(&lsvd->mutex) < 0) {
        return -1;
    }

    ret = fsync(lsvd->fd);

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
    if (pthread_mutex_lock(&lsvd->mutex) < 0) {
        return -1;
    }
    force_checkpoint(lsvd);
    stop_checkpoint_thread(lsvd);
    pthread_mutex_destroy(&lsvd->mutex);

    ret = close(lsvd->fd);
    free(lsvd->sblock);
    free(lsvd->sector_to_offset);
    free(lsvd);
    return ret;
}
