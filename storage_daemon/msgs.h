#include <linux/kernel.h>

#define FBS_SECTORSIZE 512

enum fbs_req_t {
    FBS_WRITE = 1,
    FBS_READ
};

enum fbs_status_t {
    FBS_SUCCESS = 0,
    FBS_ERROR
};

struct fbs_header {
    __be16 command; // fbs_req_t
    __be32 len;     // length in bytes
    __be32 offset;  // Offset in virtual disk
    __be32 seq_num; // Sequence number for each request
};

struct fbs_response {
    __be16 status;  // 0 on success, > 0 on failure
    __be32 seq_num; // 
};
