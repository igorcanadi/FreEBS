#ifndef _MSGS_H
#define _MSGS_H

#include <linux/types.h>

#define KERNEL_SECTOR_SIZE 512
#define __packed __attribute__((packed))

enum fbs_req_t {
    FBS_WRITE = 1,
    FBS_READ
};

struct fbs_header {
    __be16 command;    // fbs_req_t
    __be32 len;        // length in bytes
    __be32 offset;     // offset in virtual disk in sectors
    __be32 seq_num;    // sequence number of this request
    __be32 req_num;    // each request has unique req_num
} __packed;

enum fbs_status {
    SUCCESS = 0,
    ERROR
};

struct fbs_response {
    __be16  status;   // 0 on success -- see enum fbs_response
    __be32  req_num;
} __packed;

#endif
