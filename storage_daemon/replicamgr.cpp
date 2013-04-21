#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "replicamgr.h"

#define REPLICA_SIZE (1024*1024*1024*1024)  // 1GB

ReplicaManager::ReplicaManager(unsigned n, unsigned r, unsigned w){
    if (n == 0 || (r + w) <= n){
        perror("ReplicaManager: bad input parameters");
        exit(1);
    }

    numReplicas = n;
    numReaders = r;
    numWriters = w;

    replicas = new struct lsvd_disk*[numReplicas];
}

ReplicaManager::~ReplicaManager(){
    for(unsigned i = 0; i < numReplicas; i++){
        close_lsvd(replicas[i]);
    }
    
    delete [] replicas;
}

int ReplicaManager::create(const char *pathname, unsigned size){
    for(unsigned i = 0; i < numReplicas; i++){
        replicas[i] = create_lsvd("/tmp/tmp.dsk", REPLICA_SIZE);
    }
}   

int ReplicaManager::open(const char *pathname){
    for(unsigned i = 0; i < numReplicas; i++){
        replicas[i] = open_lsvd("/tmp/tmp.dsk");
    }
}

int ReplicaManager::read(unsigned offset, unsigned length, unsigned seq_num, char *buffer){
    unsigned fbs_min, fbs_max;      // Byte offsets
    unsigned lsvd_min, lsvd_max;
    unsigned lsvd_off, lsvd_len;    // LSVD sector offsets
    unsigned version;
    int status = 0;

    fbs_min = FBS_SECTORSIZE * offset;
    fbs_max = fbs_min + length * FBS_SECTORSIZE;
    lsvd_min = fbs_min - (fbs_min % LSVD_SECTORSIZE);
    lsvd_max = fbs_max + (LSVD_SECTORSIZE - (fbs_max % LSVD_SECTORSIZE));

    // Read from most recent version no later than seq_num
    
    lsvd_off = lsvd_min / LSVD_SECTORSIZE;
    lsvd_len = (lsvd_max - lsvd_min) / LSVD_SECTORSIZE + 1;

    version = get_version(replicas[0]);
    
    status = read_lsvd(replicas[0], buffer, lsvd_off, lsvd_len, version);
    if(status < 0){
        perror("ERROR LSVD read");
    }

    return status;
}

int ReplicaManager::write(unsigned offset, unsigned length, unsigned seq_num, const char *buffer){
    int status = 0;
    unsigned fbs_min, fbs_max;
    unsigned seg1_off, seg2_off, seg3_off;
    unsigned seg1_bytes, seg2_bytes, seg3_bytes;
    unsigned version;

    char *outbuf = new char[LSVD_SECTORSIZE];

    fbs_min = FBS_SECTORSIZE * offset;
    fbs_max = fbs_min + length * FBS_SECTORSIZE;

    seg1_off = (fbs_min - fbs_min % LSVD_SECTORSIZE);
    seg2_off = seg1_off + LSVD_SECTORSIZE;
    seg3_off = (fbs_max - fbs_max % LSVD_SECTORSIZE);

    if (fbs_min % LSVD_SECTORSIZE){
        version = get_version(replicas[0]);
        status = read_lsvd(replicas[0], &outbuf[0], seg1_off, 1, version);
        // Overwrite
        memcpy(&outbuf[seg2_off-fbs_min], &buffer[0], LSVD_SECTORSIZE-(seg2_off-fbs_min));
        status = write_lsvd(replicas[0], &outbuf[0], 1, 
                seg1_off/LSVD_SECTORSIZE, seq_num);
        if (status < 0) {
            return status;
        }
    } else {
        seg2_off = seg1_off;
    }

    if (fbs_max % LSVD_SECTORSIZE){
        // Read
        version = get_version(replicas[0]);
        status = read_lsvd(replicas[0], &outbuf[0], seg3_off, 1, version);
        memcpy(&outbuf[0], &buffer[seg3_off-fbs_min], fbs_max - seg3_off);
        status = write_lsvd(replicas[0], &outbuf[0], 1, seg3_off/LSVD_SECTORSIZE, seq_num);
        if (status < 0){
            return status;
        }
    } else {
        seg3_off += LSVD_SECTORSIZE;
    }

    if((status = write_lsvd(replicas[0], &outbuf[seg2_off-fbs_min], 
            (seg3_off-seg2_off)/LSVD_SECTORSIZE, 
            seg2_off/LSVD_SECTORSIZE, version)) < 0){
        return status;
    }
    


    return status;
}
