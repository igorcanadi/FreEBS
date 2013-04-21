#ifndef _RMGR_H
#define _RMGR_H


#include "freebs.h"
#include "lsvd.h"

#define FBS_SECTORSIZE  KERNEL_SECTOR_SIZE
#define LSVD_SECTORSIZE SECTOR_SIZE

class ReplicaManager {
    unsigned numReplicas;
    unsigned numReaders;
    unsigned numWriters;

    struct lsvd_disk **replicas;

public:
    ReplicaManager(unsigned n, unsigned r, unsigned w);
    ~ReplicaManager();

    int create(const char *pathname, unsigned size);
    int open(const char *pathname);
    int read(unsigned offset, unsigned length, unsigned seq_num, char *buffer);
    int write(unsigned offset, unsigned length, unsigned seq_num, const char *buffer);

private:
    ReplicaManager();    
    ReplicaManager(ReplicaManager &);
};

#endif
