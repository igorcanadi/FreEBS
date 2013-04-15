#ifndef _FREEBS_H
#define  _FREEBS_H

#include <linux/mutex.h>
#include <linux/genhd.h>
#include <linux/in.h>

#define fbs_info(fmt, ...) \
  pr_info("%s:%d - " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

struct freebs_socket {
    struct mutex mutex;
    struct sockaddr_in servaddr;
    struct socket    *socket;
};

struct freebs_request {
    struct freebs_device *fbs_dev;
    sector_t sector;      // in sectors
    unsigned int size;    // in bytes
    int seq_num;
    struct request *req;
    struct list_head    in_flight;
    struct list_head    rq_queue;
};

/* to shorten dev_warn(DEV, "msg"); and relatives statements */
#define DEV (disk_to_dev(fbs_dev->fbs_disk))

#define D_ASSERT(exp)	if (!(exp)) \
	 dev_err(DEV, "ASSERT( " #exp " ) in %s:%d\n", __FILE__, __LINE__)

#define ERR_IF(exp) if (({						\
	int _b = (exp) != 0;						\
	if (_b) dev_err(DEV, "ASSERT FAILED: %s: (%s) in %s:%d\n",	\
			__func__, #exp, __FILE__, __LINE__);		\
	_b;								\
	}))

#define FREEBS_SECTOR_SIZE 512

extern int bsdevice_init(struct freebs_device *);
extern void bsdevice_cleanup(struct freebs_device *);

/*
 * The internal structure representation of our device
 */
struct freebs_device {
    /* Size is the size of the device (in sectors) */
    unsigned int size;
    /* For exclusive access to our request queue */
    spinlock_t lock;
    /* Our request queue */
    struct request_queue *fbs_queue;
    /* This is kernel's representation of an individual disk device */
    struct gendisk *fbs_disk;
    struct freebs_socket data;
    atomic_t            packet_seq;
    struct list_head    in_flight;    /* requests that have been sent to replica
                                         manager but have not been completed */
    struct mutex        in_flight_l;
    struct list_head    rq_queue;
    struct mutex        rq_mutex;
    struct semaphore    rq_queue_sem;
};

enum fbs_req_t {
    FBS_WRITE = 1,
    FBS_READ
};

struct fbs_header {
    __be16 command;    // fbs_req_t
    __be32 len;        // length in bytes
    __be32 offset;     // offset in virtual disk in sectors
    __be32 seq_num;    // sequence number of this request
} __packed;

enum fbs_status {
    SUCCESS = 0
};

struct fbs_response {
    __be16  status;   // 0 on success -- see enum fbs_response
    __be32  seq_num;
} __packed;

#define __packed __attribute__((packed))

struct p_header_only {
    //u16	  magic;	/* use DRBD_MAGIC_BIG here */
    u16	  command;
    u32	  length;	/* Use only 24 bits of that. Ignore the highest 8 bit. */
    u8	  payload[0];
} __packed;

union p_header {
    struct p_header_only h;
};

/* returns 1 if it was successful,
 * returns 0 if there was no data socket.
 * so wherever you are going to use the data.socket, e.g. do
 * if (!freebs_get_data_sock(fbs_dev))
 *	return 0;
 *	CODE();
 * freebs_put_data_sock(fbs_dev);
 */
static inline int freebs_get_data_sock(struct freebs_device *fbs_dev)
{
    mutex_lock(&fbs_dev->data.mutex);
    /* freebs_disconnect() could have called freebs_free_sock()
     * while we were waiting in down()... */
    if (unlikely(fbs_dev->data.socket == NULL)) {
        mutex_unlock(&fbs_dev->data.mutex);
        return 0;
    }
    return 1;
}

static inline void freebs_put_data_sock(struct freebs_device *fbs_dev)
{
    mutex_unlock(&fbs_dev->data.mutex);
}

int freebs_send(struct freebs_device *, struct socket *,
                void *, size_t, unsigned);
void freebs_init_socks(struct freebs_device *);

#endif
