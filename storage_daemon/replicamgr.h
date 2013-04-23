#ifndef _RMGR_H
#define _RMGR_H

#include "freebs.h"
#include "lsvd.h"

#define FBS_SECTORSIZE  KERNEL_SECTOR_SIZE
#define LSVD_SECTORSIZE SECTOR_SIZE
#define GB (1073741824)  // 1GB

class ReplicaManager {
    uint64_t numReplicas;
    uint64_t numReaders;
    uint64_t numWriters;

    struct lsvd_disk *local;
    // TODO: Data structure for keeping track of other replicas
    // <ip address, last ack'd version>
public:
    ReplicaManager(uint64_t n, uint64_t r, uint64_t w);
    ~ReplicaManager();

    int create(const char *pathname, uint64_t size);
    int open(const char *pathname);
    int read(uint64_t offset, uint64_t length, uint64_t seq_num, char *buffer);
    int write(uint64_t offset, uint64_t length, uint64_t seq_num, const char *buffer);

private:
    ReplicaManager();    
    ReplicaManager(ReplicaManager &);
};

#endif
