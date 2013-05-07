#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "replicamgr.h"


ReplicaManager::ReplicaManager(){}

ReplicaManager::~ReplicaManager(){
    close_lsvd(local);
}

/*
 * create()
 *  pathname    path to the lsvd_disk
 *  size        lsvd_volume size in sectors
 * */
int ReplicaManager::create(const char *pathname, uint64_t size){
    local = create_lsvd(pathname, size);
    if (local == NULL){
        return -1;
    }
    return 0;
}   

/*
 * open()
 *  pathname     path to the lsvd_disk
 * */
int ReplicaManager::open(const char *pathname){
    local = open_lsvd(pathname);
    if (local == NULL){
        return -1;
    }
    return 0;
}

/*
 * read()
 *  offset      freebs sector offset
 *  length      freebs read length in sectors
 *  seq_num     Used for versioning
 *  buffer      Destination buffer
 * */
int ReplicaManager::read(uint64_t offset, uint64_t length, uint64_t seq_num, char *buffer){
    uint64_t fbs_min, fbs_max;      // Byte offsets. Max = exclusive limit
    uint64_t lsvd_min, lsvd_max;
    uint64_t lsvd_off, lsvd_len;    // LSVD sector offsets
    uint64_t version;
    int status = 0;
    
    char *cache;   // Can change this later to be the cache/buffer

    fbs_min = FBS_SECTORSIZE * offset;
    fbs_max = fbs_min + length * FBS_SECTORSIZE;

    if(fbs_min % LSVD_SECTORSIZE){ 
        lsvd_min = fbs_min - (fbs_min % LSVD_SECTORSIZE);
    } else {
        lsvd_min = fbs_min;
    }
    if (fbs_max % LSVD_SECTORSIZE){
        lsvd_max = fbs_max + (LSVD_SECTORSIZE - (fbs_max % LSVD_SECTORSIZE));
    } else {
        lsvd_max = fbs_max;
    }

    // Starting values
    lsvd_off = lsvd_min / LSVD_SECTORSIZE;
    lsvd_len = (lsvd_max - lsvd_min) / LSVD_SECTORSIZE;
    version = get_version(local);

#ifdef DEBUG
    printf("READ: fbs_min:%llu fbs_max:%llu lsvd_min:%llu lsvd_max:%llu\n", 
            fbs_min, fbs_max, lsvd_min, lsvd_max);
#endif
    
    // TODO: make this part of some kind of buffer pool so we don't alloc all the time
    cache = new char[lsvd_max-lsvd_min];

    if ((status = read_lsvd(local, cache, lsvd_len, lsvd_off, version)) < 0){
        perror("ERROR LSVD read");
    }

    memcpy(buffer, &cache[fbs_min-lsvd_min], fbs_max-fbs_min);
    
    delete cache;

    return status;
}

int ReplicaManager::write(uint64_t offset, uint64_t length, uint64_t seq_num, const char *buffer){
    uint64_t fbs_min, fbs_max;      // Byte offsets. Max = exclusive limit
    uint64_t lsvd_min, lsvd_max;
    uint64_t lsvd_off, lsvd_len;    // LSVD sector offsets
    uint64_t version;
    int status = 0;
 
    char *cache;
    uint64_t cache_size;

    fbs_min = FBS_SECTORSIZE * offset;
    fbs_max = fbs_min + length * FBS_SECTORSIZE;

    if (fbs_min % LSVD_SECTORSIZE){
        lsvd_min = fbs_min - (fbs_min % LSVD_SECTORSIZE);
    } else {
        lsvd_min = fbs_min;
    }
    if (fbs_max % LSVD_SECTORSIZE){
        lsvd_max = fbs_max + (LSVD_SECTORSIZE - (fbs_max % LSVD_SECTORSIZE));
    } else {
        lsvd_max = fbs_max;
    }

    lsvd_off = lsvd_min / LSVD_SECTORSIZE;
    lsvd_len = (lsvd_max - lsvd_min) / LSVD_SECTORSIZE;

    version = get_version(local);
#ifdef DEBUG
    printf("WRITE fbs_min:%llu fbs_max:%llu lsvd_min:%llu lsvd_max:%llu\n", 
            fbs_min, fbs_max, lsvd_min, lsvd_max);
#endif

    // Aligned write
    if(lsvd_min == fbs_min && lsvd_max == fbs_max){
        if ((status = write_lsvd(local, buffer, lsvd_len, lsvd_off, seq_num)) < 0){
            perror("ERROR ReplicaManager LSVD write fail");
        }
        return status;
    }


    // Unaligned

    uint64_t cache_off = 0; // Cache byte offset
    uint64_t buff_off = 0; // Buffer byte offset
    uint64_t copy_len = 0; // How much to copy

    cache_size = lsvd_max - lsvd_min;
    cache = new char[cache_size];

    try {
        // Unaligned in the low end of memory
        if (lsvd_min != fbs_min) {
            if ((status = read_lsvd(local, &cache[0], 1, lsvd_off, version))
                    < 0) {
                throw status;
            }
            cache_off = fbs_min - lsvd_min;
            buff_off = 0;
            if (lsvd_len == 1) {
                copy_len = fbs_max - fbs_min;
            } else {
                copy_len = LSVD_SECTORSIZE - cache_off;
            }
#ifdef DEBUG
            printf("cache_off:%llu, buff_off:%llu, copy_len:%llu, cache_size:%llu\n", cache_off,
                    buff_off, copy_len, cache_size);
#endif
            memcpy(&cache[cache_off], &buffer[buff_off], copy_len);
        }

        memcpy(&cache[cache_off + copy_len], &buffer[buff_off + copy_len],
                fbs_max - fbs_min - copy_len - buff_off);

        // Unaligned at the high end of memory
        if (lsvd_max != fbs_max && lsvd_len > 1) {
            if ((status = read_lsvd(local, &cache[cache_size - LSVD_SECTORSIZE],
                    1, lsvd_off + lsvd_len - 1, version)) < 0) {
                throw status;
            }
            cache_off = cache_size - LSVD_SECTORSIZE;
            buff_off = length * FBS_SECTORSIZE - (fbs_max % LSVD_SECTORSIZE);
            copy_len = fbs_max % LSVD_SECTORSIZE;
#ifdef DEBUG
            printf("cache_off:%llu, buff_off:%llu, copy_len:%llu, cache_size:%llu\n", cache_off,
                    buff_off, copy_len, cache_size);
#endif
            memcpy(&cache[cache_off], &buffer[buff_off], copy_len);
        }
        if ((status = write_lsvd(local, cache, lsvd_len, lsvd_off, seq_num)) < 0){
            throw status;
        }
    } catch(int e){
        perror("ERROR ReplicaManager LSVD write fail");
        delete [] cache;
    }
    
    return status;
}

// Retrieve versions of a predecessor
uint64_t ReplicaManager::get_local_version(){
    return get_version(local);
}

// Retrieve writes up to version
char * ReplicaManager::get_writes_since(uint64_t version, size_t *size){
    return get_writes_lsvd(local, version, size);
}

int ReplicaManager::put_writes_since(char * buf, size_t size){
    return put_writes_lsvd(local, get_version(local), buf, size);
}


