#ifndef _FREEBS_H
#define  _FREEBS_H

#include <linux/types.h>
#include "msgs.h"

struct fbs_request {
    unsigned short command;
    unsigned int len;
    unsigned int offset;
    unsigned int seq_num;
    unsigned int req_num;
};

struct resp_data {
    struct fbs_response header;
    char *data;
    unsigned numBytes;
};

#endif    
