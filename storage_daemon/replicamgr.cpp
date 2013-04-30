#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>

#include "replicamgr.h"


ReplicaManager::ReplicaManager(const char *ctrl, const char *prev, 
        const char *next){
    // Open sockets for communication with controller and next replica
    int status = 0;
    struct hostent *he;

    if ((pSock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("ERROR opening socket");
        exit(1);
    }

    if ((nSock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("ERROR opening socket");
        exit(1);
    }

    // Set up connection to prev Replica
    prev_addr.sin_family = AF_INET;
    prev_addr.sin_port=htons(SYNC_PORT);  
    if (prev != NULL && *prev != '\0'){
        he = gethostbyname(prev);
        memcpy(&prev_addr.sin_addr.s_addr, he->h_addr, he->h_length);
        conn_prev();
        isPrimary == false;
    } else {
        isPrimary == true;
    }

    // Set up connection to next Replica
    next_addr.sin_family = AF_INET;
    next_addr.sin_port=htons(SYNC_PORT);  
    if (next != NULL && *prev != '\0'){
        he = gethostbyname(next);
        memcpy(&next_addr.sin_addr.s_addr, he->h_addr, he->h_length);
//        next_addr.sin_addr.s_addr = INADDR_ANY;
        bind_next();
    }
}    

ReplicaManager::~ReplicaManager(){
    close(cSock);
    close(nSock);
    close_lsvd(local);
}

/*
 * create()
 *  pathname    path to the lsvd_disk
 *  size        lsvd_volume size in sectors
 * */
int ReplicaManager::create(const char *pathname, uint64_t size){
    local = create_lsvd(pathname, size);
}   

/*
 * open()
 *  pathname     path to the lsvd_disk
 * */
int ReplicaManager::open(const char *pathname){
    local = open_lsvd(pathname);
}

/*
 * conn_prev()    Connect to previous replica
 * */
int ReplicaManager::conn_prev(){
    return connect(pSock, (sockaddr *)&prev_addr, sizeof(prev_addr));
}

/*
 * bind_next()  Set up port to listen to next replica
 * */
int ReplicaManager::bind_next(){
    int status = 0;
    if ((status = bind(nSock, (struct sockaddr *) &next_addr, 
                    sizeof(next_addr))) < 0){
        return status;
    }
    
    listen(nSock, 5);
    return status;
}

/*
 * accept_next()    Accept connection
 * */
int ReplicaManager::accept_next(){
    sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    return accept(nSock, (struct sockaddr *) &cli_addr, &clilen);
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
 
    lsvd_min = fbs_min - (fbs_min % LSVD_SECTORSIZE);
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
    printf("READ: fbs_min:%lu fbs_max:%lu lsvd_min:%lu lsvd_max:%lu\n", 
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
 
    lsvd_min = fbs_min - (fbs_min % LSVD_SECTORSIZE);
    if (fbs_max % LSVD_SECTORSIZE){
        lsvd_max = fbs_max + (LSVD_SECTORSIZE - (fbs_max % LSVD_SECTORSIZE));
    } else {
        lsvd_max = fbs_max;
    }

    lsvd_off = lsvd_min / LSVD_SECTORSIZE;
    lsvd_len = (lsvd_max - lsvd_min) / LSVD_SECTORSIZE;

    version = get_version(local);
#ifdef DEBUG
    printf("WRITE fbs_min:%lu fbs_max:%lu lsvd_min:%lu lsvd_max:%lu\n", 
            fbs_min, fbs_max, lsvd_min, lsvd_max);
#endif

    // Aligned write
    if(lsvd_min == fbs_min && lsvd_max == fbs_max){
        status = write_lsvd(local, buffer, lsvd_len, lsvd_off, seq_num);
    } else {    // Unaligned
        uint64_t cache_off = 0; // Cache byte offset
        uint64_t buff_off = 0;  // Buffer byte offset
        uint64_t copy_len = 0;       // How much to copy

        cache_size = lsvd_max - lsvd_min;
        cache = new char[cache_size];

        // Unaligned in the low end of memory
        if (lsvd_min != fbs_min){
            if ((status = read_lsvd(local, &cache[0], 1, lsvd_off, version)) < 0){
                perror("ERROR LSVD read fail");
                return status;
            }
            cache_off = fbs_min - lsvd_min;
            buff_off = 0;
            if (lsvd_len == 1){
                copy_len = fbs_max - fbs_min;
            } else {
                copy_len = LSVD_SECTORSIZE - cache_off;
            }
#ifdef DEBUG
            printf("cache_off:%lu, buff_off:%lu, copy_len:%lu, cache_size\n", cache_off, 
                    buff_off, copy_len, cache_size);
#endif
            memcpy(&cache[cache_off], &buffer[buff_off], copy_len);
        }
        
        memcpy(&cache[cache_off+copy_len], &buffer[buff_off + copy_len], 
                fbs_max-fbs_min-copy_len-buff_off);

        // Unaligned at the high end of memory
        if (lsvd_max != fbs_max && lsvd_len > 1){
            if ((status = read_lsvd(local, &cache[cache_size-LSVD_SECTORSIZE],
                            1, lsvd_off+lsvd_len-1, version)) < 0){
                perror("ERROR LSVD read fail");
                return status;
            }
            cache_off = cache_size - LSVD_SECTORSIZE;
            buff_off = length * FBS_SECTORSIZE - (fbs_max % LSVD_SECTORSIZE);
            copy_len = fbs_max % LSVD_SECTORSIZE;
#ifdef DEBUG
            printf("cache_off:%lu, buff_off:%lu, copy_len:%lu, cache_size:%lu\n", cache_off, 
                    buff_off, copy_len, cache_size);
#endif
            memcpy(&cache[cache_off], &buffer[buff_off], copy_len);
        }
        status = write_lsvd(local, cache, lsvd_len, lsvd_off, seq_num);
        delete [] cache;
    }

    if (status < 0){
        perror("ERROR LSVD write fail");
    }
    
    return status;
}

// Retrieve versions of a predecessor
void ReplicaManager::sync(){
    struct rmgr_sync_request req;
    struct rmgr_sync_response resp;

    char *buf;
    uint64_t buf_len;
    int bytesRead = 0;

    req.command = RMGR_SYNC;
    req.seq_num = get_version(local);

    if(send(pSock, &req, sizeof(req), 0) < 0){
        // Problem syncing to predecessor
        perror("ERROR sync send");
        return;
    }
    while(req.seq_num != 0){
        if (recv(pSock, &resp, sizeof(resp), 0) < 0){
            perror("ERROR sync recv");
            return;
        }
        resp.seq_num = ntohl(resp.seq_num);
        resp.offset = ntohl(resp.offset);
        resp.length = ntohl(resp.length);

        buf_len = resp.length * LSVD_SECTORSIZE;
        buf = new char[buf_len];
        // Recieve and apply data
        for (int buf_off = 0; buf_off < buf_len; buf_off += bytesRead){
            if((bytesRead = recv(pSock, &buf[buf_off], sizeof(buf), 0)) < 0){
                delete buf;
                return;
            }
        }
        if (write_lsvd(local, buf, resp.length, resp.offset, resp.seq_num) < 0){
            perror("ERROR lsvd write");
            delete buf;
            return;
        }
    }

    delete buf;
    printf("Sync\n");
}

uint64_t ReplicaManager::get_local_version(){
#if DEBUG
    printf("get_version\n");
#endif
    return get_version(local);
}

void ReplicaManager::update(struct in_addr &prev, struct in_addr &next){
    if (prev_addr.sin_addr.s_addr != prev.s_addr){
        prev_addr.sin_addr.s_addr = prev.s_addr;
        close(pSock);
        if ((pSock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
            perror("ERROR socket reopen");
            exit(1); 
        }
        if (conn_prev() < 0){
            perror("ERROR socket reconnect");
        }
    }

    if (next_addr.sin_addr.s_addr != next.s_addr){
        next_addr.sin_addr.s_addr = next.s_addr;
        close(nSock);
        if ((nSock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
           perror("ERROR socket reopen");
           exit(1); 
        }
        if (bind_next() < 0){
            perror("ERROR socket reconnect");
            exit(1);
        }
    }
    
    return;
}

