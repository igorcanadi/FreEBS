#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "replicamgr.h"


ReplicaManager::ReplicaManager(const char *prev, const char *next){
    // Open sockets for communication with controller and next replica
    int status = 0;
    struct hostent *he;

    pthread_mutex_init(&p_lock, NULL);
    pthread_mutex_init(&n_lock, NULL);

    // Set up connection to prev Replica
    prev_addr.sin_family = AF_INET;
    prev_addr.sin_port=htons(SYNC_PORT);  
    if (prev != NULL && *prev != '\0'){
        pConn == true;
        he = gethostbyname(prev);
        printf("%s\n", prev);
        memcpy(&prev_addr.sin_addr.s_addr, he->h_addr, he->h_length);
        if (conn_prev() < 0){
            perror("ERROR connecting to prev");
            exit(1);
        }
    } else {
        pConn == false;
    }

    // Set up connection to next Replica
    next_addr.sin_family = AF_INET;
    next_addr.sin_port=htons(PROP_PORT);
    if (next != NULL && *prev != '\0'){
        nConn == true;
        he = gethostbyname(next);
        memcpy(&next_addr.sin_addr.s_addr, he->h_addr, he->h_length);
    } else {
        nConn == false;
    }
}    

ReplicaManager::~ReplicaManager(){
    close(pSock);
    close(nSock);
    pthread_mutex_destroy(&p_lock);
    pthread_mutex_destroy(&n_lock);
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
 * conn_prev()  Connect to previous replica
 * */
int ReplicaManager::conn_prev(){
    int sock, status;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        return sock;
    }

    status = connect(sock, (sockaddr *)&prev_addr, sizeof(prev_addr));
    if (status < 0){
        return status;
    }

    pthread_mutex_lock(&p_lock);
    pSock = sock;
    pthread_mutex_lock(&p_lock);

    return sock;
}

/*
 * conn_next()  Connect to next replica
 * */
int ReplicaManager::conn_next(){
    int sock, status;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        return sock;
    }

    status = connect(sock, (sockaddr *)&next_addr, sizeof(next_addr));
    if (status < 0){
        return status;
    }

    pthread_mutex_lock(&n_lock);
    nSock = sock;
    pthread_mutex_unlock(&n_lock);

    return sock;
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
            printf("cache_off:%llu, buff_off:%llu, copy_len:%llu, cache_size:%llu\n", cache_off, 
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
uint64_t ReplicaManager::get_local_version(){
    return get_version(local);
}

// Retrieve writes up to version
char * ReplicaManager::get_writes_since(uint64_t version, size_t *size){
    return get_writes_lsvd(local, version, size);
}

// Send sync request
void ReplicaManager::sync(){
    struct rmgr_sync_request req;
    struct rmgr_sync_response resp;

    char *buf;
    int bytesRead = 0;
    int conn;

    pthread_mutex_lock(&p_lock);
    conn = pSock;
    pthread_mutex_unlock(&p_lock);

    req.command = htons(RMGR_SYNC);
    req.seq_num = htonl((uint32_t)(get_version(local)));

    // Send SYNC message to prev
    if(send(conn, &req, sizeof(req), 0) < 0){
        perror("ERROR sync send");
        return;
    }
    // Receive all writes from prev
    while(1) {
        for(int off = 0; off < sizeof(resp); off += bytesRead){
            if ((bytesRead = recv(conn, &resp + off, sizeof(resp) - off, 0)) < 0){
                perror("ERROR sync recv");
                return;
            }
        }
        resp.seq_num = ntohl(resp.seq_num);
        resp.size = ntohl(resp.size);

        if (resp.seq_num == 0){
            // version too small
            break;
        }

        // Receive data and write to volume
        buf = new char[resp.size];
        for (int buf_off = 0; buf_off < resp.size; buf_off += bytesRead){
            if((bytesRead = recv(conn, &buf + buf_off, sizeof(buf) - buf_off, 0)) < 0){
                delete buf;
                return;
            }
        }
        if (put_writes_lsvd(local, resp.seq_num, buf, resp.size) < 0){
            perror("ERROR lsvd write");
            delete buf;
            return;
        }
    }

    delete buf;
    printf("Sync\n");
}



void ReplicaManager::update(struct in_addr *prev, struct in_addr *next){
    if (prev != NULL && prev_addr.sin_addr.s_addr != prev->s_addr){
        prev_addr.sin_addr.s_addr = prev->s_addr;
        close(pSock);
    }


    if (next != NULL && next_addr.sin_addr.s_addr != next->s_addr){
        next_addr.sin_addr.s_addr = next->s_addr;
        close(nSock);
    }
    
    return;
}

// Send data in buf to next replica
void ReplicaManager::send_next(char * buf, size_t len){
    int conn, off, bytesWritten;
    pthread_mutex_lock(&n_lock);
    conn = nSock;
    pthread_mutex_unlock(&n_lock);
    printf("\n");
    if (conn >= 0){
        for (off = 0; off < len; off += bytesWritten){
            if((bytesWritten = send(conn, buf, len, 0)) < 0){
                perror("Failed send next");
                return;
            }
            printf("send_next wrote %d bytes\n", bytesWritten);
        }
//	printf("send_next %d bytes\n", off);
    }
}

