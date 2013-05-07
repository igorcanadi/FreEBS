#ifndef _RMGR_H
#define _RMGR_H

#include <netinet/in.h>
#include "msgs.h"
#include "freebs.h"
#include "lsvd.h"

#define FBS_SECTORSIZE  KERNEL_SECTOR_SIZE
#define LSVD_SECTORSIZE SECTOR_SIZE
#define GB (1073741824)  // 1GB

#define CTRL_PORT   9090    // UDP port for heartbeat messages 
#define FBS_PORT    9000    // TCP port for driver messages
#define SYNC_PORT   9001    // TCP port for sync
#define PROP_PORT   9002    // TCP port for propagation

/*
 * Message structs/enums for replica manager
 * */
enum rmgr_command{
    RMGR_SYNC = 0,  // Synchronize
    RMGR_PROP       // Propagate
};

struct rmgr_sync_request{
    uint16_t command;
    uint32_t seq_num;
} __packed;

struct rmgr_sync_response{
    uint32_t seq_num;
    uint32_t size;
} __packed;

struct ctrl_update_request{
    struct in_addr prev;
    struct in_addr next;
} __packed;

class ReplicaManager {
    struct lsvd_disk *local;

public: 
    ReplicaManager();
    ~ReplicaManager();

    int create(const char *pathname, uint64_t size);
    int open(const char *pathname);
    int read(uint64_t offset, uint64_t length, uint64_t seq_num, char *buffer);
    int write(uint64_t offset, uint64_t length, uint64_t seq_num, const char *buffer);

    // Synchronization functions
    uint64_t get_local_version();
    char *get_writes_since(uint64_t version, size_t *size);
    int put_writes_upto(uint64_t version, char * buf, size_t size);

private:
    ReplicaManager(ReplicaManager &);
};

#endif
