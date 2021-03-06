#ifndef _FREEBS_H
#define  _FREEBS_H

#include <linux/mutex.h>
#include <linux/genhd.h>
#include <linux/in.h>
#include <linux/workqueue.h>

#define fbs_printk(type, fmt, ...) \
  printk(type "%s:%d - " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define fbs_info(fmt, ...) \
  fbs_printk(KERN_INFO, fmt, ##__VA_ARGS__) //"%s:%d - " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define fbs_err(fmt, ...) \
  fbs_printk(KERN_ERR, fmt, ##__VA_ARGS__)

#define fbs_debug(fmt, ...) \
  //fbs_printk(KERN_DEBUG, fmt, ##__VA_ARGS__)

struct freebs_socket {
    struct mutex mutex;
    struct sockaddr_in servaddr;
    struct socket    *socket;
    struct workqueue_struct *work_queue; /* queue of freebs_requests to send */
    struct task_struct *receiver;
};

//typedef unsigned int sector_t;

struct freebs_request {
    struct freebs_device *fbs_dev;
    sector_t sector;      // in sectors
    unsigned int size;    // in bytes
    unsigned int seq_num;
    unsigned int req_num;
    struct request *req;
    struct list_head    queue;
    atomic_t num_commits;
};

struct sender_work {
    struct freebs_request *fbs_req;
    struct freebs_socket *fbs_sock;
    struct work_struct work;
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

//#define FREEBS_SECTOR_SIZE 16384
#define KERNEL_SECTOR_SIZE 512

/*
static inline fbs_sector_t kernel_to_freebs(unsigned int sect)
{
  return (sect * KERNEL_SECTOR_SIZE) / FREEBS_SECTOR_SIZE;
}

static inline unsigned int freebs_to_bytes(fbs_sector_t sectors) 
{
  return sectors * FREEBS_SECTOR_SIZE;
}

static inline fbs_sector_t bytes_to_freebs(unsigned int bytes)
{
  return bytes / FREEBS_SECTOR_SIZE;
}
*/

#ifdef DEBUG
#define print_req(req) printk(KERN_DEBUG "fbs_req: sector %u, %u bytes, %d seq_num\n", \
    fbs_req->sector, fbs_req->size, fbs_req->seq_num);
#else
#define print_req(req)
#endif

extern int bsdevice_init(struct freebs_device *);
extern void bsdevice_cleanup(struct freebs_device *);

struct freebs_device;

/*
 * This represents data passed to each receiver thread
 */
struct receiver_data {
    struct freebs_device *fbs_dev;
    int replica;
};

/*
 * Represents a replica manager
 */
struct replica {
    uint64_t seq_num;
    struct freebs_socket data;
    struct receiver_data receiver_data;
};

/*
 * Represents the list of replicas, the first one being the primary
 */
struct replica_list {
    struct replica *replicas;
    int num_replicas;
};

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
    atomic_t            packet_seq;
    atomic_t            req_num;
    struct list_head    in_flight;    /* requests that have been sent to replica
                                         manager but have not been completed */
    struct mutex        in_flight_l;
    //struct task_struct  *receiver;
    //struct task_struct  *sender;
    struct replica_list replicas;
    int quorum; /* how many replicas must claim to have committed the write
                   for it to be a success */
};

static inline struct freebs_socket *primary(struct freebs_device *fbs_dev)
{
    return &fbs_dev->replicas.replicas[0].data;
}

/* returns 1 if it was successful,
 * returns 0 if there was no data socket.
 * so wherever you are going to use the data.socket, e.g. do
 * if (!freebs_get_data_sock(fbs_dev))
 *	return 0;
 *	CODE();
 * freebs_put_data_sock(fbs_dev);
 */
static inline int freebs_get_data_sock(struct freebs_socket *fbs_sock)
{
    mutex_lock(&fbs_sock->mutex);
    /* freebs_disconnect() could have called freebs_free_sock()
     * while we were waiting in down()... */
    if (unlikely(fbs_sock->socket == NULL)) {
        mutex_unlock(&fbs_sock->mutex);
        return 0;
    }
    return 1;
}

static inline void freebs_put_data_sock(struct freebs_socket *fbs_sock)
{
    mutex_unlock(&fbs_sock->mutex);
}

int freebs_send(struct freebs_device *, struct socket *,
                void *, size_t, unsigned);
int freebs_init_socks(struct freebs_device *);
int establish_connections(struct freebs_device *);

#endif
