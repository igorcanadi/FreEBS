#ifndef _RMGR_H
#define _RMGR_H

#include <netinet/in.h>
#include "freebs.h"
#include "lsvd.h"

#define FBS_SECTORSIZE  KERNEL_SECTOR_SIZE
#define LSVD_SECTORSIZE SECTOR_SIZE
#define GB (1073741824)  // 1GB

#define CTRL_PORT   9090    // UDP port for heartbeat messages 
#define FBS_PORT    9000    // TCP port for driver messages
#define SYNC_PORT   9001    // TCP port for replica-replica, replica-controller

/*
 * Message structs/enums for replica manager
 * */
enum rmgr_command{
    RMGR_SYNC = 0
};

struct rmgr_sync_request{
    uint16_t command;
    uint32_t seq_num;
} __packed;

struct rmgr_sync_response{
    uint32_t seq_num;
    uint32_t offset;
    uint32_t length;
};

struct mgr_update_request{
    struct in_addr prev;
    struct in_addr next;
};

class ReplicaManager {
    bool isPrimary;
    struct lsvd_disk *local;

    struct sockaddr_in ctrl_addr;  // Controller info
    struct sockaddr_in prev_addr;  // Prev replica info
    struct sockaddr_in next_addr;  // Next replica info

    int cSock;  // UDP Socket to comm with controller
    int pSock;  // TCP Socket to comm with prev replica
    int nSock;  // TCP Socket to comm with next replica
public: 
    ReplicaManager(const char *ctrl, const char *prev, const char *next);
    ~ReplicaManager();

    int conn_prev();
    int bind_next();
    int accept_next();

    int create(const char *pathname, uint64_t size);
    int open(const char *pathname);
    int read(uint64_t offset, uint64_t length, uint64_t seq_num, char *buffer);
    int write(uint64_t offset, uint64_t length, uint64_t seq_num, const char *buffer);

    // Synchronization functions
    void send_heartbeat();
    void sync();
    void update(struct in_addr &prev, struct in_addr &next);
    uint64_t get_local_version();

private:
    ReplicaManager();    
    ReplicaManager(ReplicaManager &);
};

#endif
