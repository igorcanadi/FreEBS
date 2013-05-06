/* FreEBS Driver */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/blk_types.h>
#include <linux/bio.h>
#include <linux/socket.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/tcp_states.h>

#define FREEBS_DEVICE_SIZE (1048576) // in 512-byte sectors

__be32 in_aton(const char *);

#include "freebs.h"
#include "msgs.h"

#define FREEBS_FIRST_MINOR 0
#define FREEBS_MINOR_CNT 16

static u_int freebs_major = 0;

struct freebs_device fbs_dev;

static struct kmem_cache *fbs_req_cache;
static struct kmem_cache *sender_work_cache;

static char *replica_ips[10];
static int num_replicas;
module_param_array(replica_ips, charp, &num_replicas, S_IRUGO);

static void fbs_cleanup(void);

static int fbs_open(struct block_device *bdev, fmode_t mode)
{
    unsigned unit = iminor(bdev->bd_inode);

    if (unit > FREEBS_MINOR_CNT)
        return -ENODEV;
    return 0;
}

static int fbs_close(struct gendisk *disk, fmode_t mode)
{
    return 0;
}

static void cleanup_socks(struct freebs_device *fbs_dev) 
{
    struct freebs_socket *fbs_sock;
    int i;

    for (i = 0; i < fbs_dev->replicas.num_replicas; i++) {
        fbs_sock = &fbs_dev->replicas.replicas[i].data;

        if (freebs_get_data_sock(fbs_sock)) {
            sock_release(fbs_sock->socket);
            fbs_sock->socket = NULL;  /* I'm assuming that sock_release frees the memory --
                                            sucks if I'm wrong */
            freebs_put_data_sock(fbs_sock);
        }
    }
}

static int fbs_recv(struct freebs_socket *fbs_sock, void *buf, size_t size)
{
	mm_segment_t oldfs;
	struct kvec iov = {
		.iov_base = buf,
		.iov_len = size,
	};
	struct msghdr msg = {
		.msg_iovlen = 1,
		.msg_iov = (struct iovec *)&iov,
		.msg_flags = MSG_WAITALL | MSG_NOSIGNAL
	};
	int rv; //, bytesRead;

	oldfs = get_fs();
	set_fs(KERNEL_DS);

	for (;;) {
		rv = sock_recvmsg(fbs_sock->socket, &msg, size, msg.msg_flags);
		if (rv == size)
			break;

		/* Note:
		 * ECONNRESET	other side closed the connection
		 * ERESTARTSYS	(on  sock) we got a signal
		 */

		if (rv < 0) {
			if (rv == -ECONNRESET)
				fbs_err("sock was reset by peer\n");
			else if (rv != -ERESTARTSYS)
				fbs_err("sock_recvmsg returned %d\n", rv);
			break;
		} else if (rv == 0) {
			fbs_info("sock was shut down by peer\n");
            // TODO: do something about it
			break;
		} else	{
			/* signal came in, or peer/link went down,
			 * after we read a partial message
			 */
			/* D_ASSERT(signal_pending(current)); */
			break;
		}
	};

	set_fs(oldfs);

	if (rv != size)
        fbs_debug("partial receive!\n");

	return rv;
}

void enqueue_request(struct list_head *new, struct list_head *queue, rwlock_t *lock)
{
    //unsigned long flags;

    write_lock(lock);//, flags);
    fbs_debug("write: got it\n");
    list_add_tail(new, queue);
    fbs_debug("write: released it\n");
    write_unlock(lock);//, flags);
}

/*
 * Returns true if the request is deleted by this call, false if it was already deleted
 */
bool del_request(struct freebs_request *req, rwlock_t *lock) 
{
    //if (list_empty(&req->queue))
        //return false;
    bool ret = false;
    //unsigned long flags;

    write_lock(lock);//, flags);
    fbs_debug("%d: write: got it\n", req->req_num);
    if (!list_empty(&req->queue)) {
        ret = true;
        list_del_init(&req->queue);
    }
    fbs_debug("%d: write: released it\n", req->req_num);
    write_unlock(lock);//, flags);
    return ret;
}

/**
 * get_request
 * Gets the fbs_request from the queue with the seq_num provided. Removes it from
 * the queue and returns it.
 */
struct freebs_request *get_request(struct list_head *queue, rwlock_t *lock, int req_num) {
    struct list_head *pos;
    struct freebs_request *req = NULL;
    bool found = false;

    read_lock(lock);//, flags);
    fbs_debug("%d: read: got it\n", req_num);
    list_for_each(pos, queue) {
        if (pos == LIST_POISON1 || pos == LIST_POISON2)
            fbs_debug("poison!\n");
        req = list_entry(pos, struct freebs_request, queue);
        if (!req)
            fbs_debug("req null!\n");
        if (req->req_num == req_num) {
            found = true;
            break;
        }
    }
    fbs_debug("%d: read: released it\n", req_num);
    read_unlock(lock);//, flags);

    if (found)
        return req;
    else
        return NULL;
}

/**
 * complete_read
 * Completes a read request by reading data from the socket and putting it into
 * the buffer requested.
 */
int complete_read(struct freebs_socket *fbs_sock, struct request *req) {
    struct bio_vec *bv;
    struct req_iterator iter;
    u8 *buffer;
    int rv, bytes_read = 0;

    rq_for_each_segment(bv, req, iter) {
        buffer = page_address(bv->bv_page) + bv->bv_offset;
        rv = fbs_recv(fbs_sock, buffer, bv->bv_len);
        if (rv != bv->bv_len) {
            printk(KERN_ERR "couldn't complete read!\n");
            if (rv < 0)
                return rv;
            else
                return -1; // THINK: what should we return here?
        }
        bytes_read += rv;
    }

    return bytes_read;
}

int try_connect_again(struct freebs_device *fbs_dev)
{
    struct socket *sock = fbs_dev->replicas.replicas[0].data.socket;
    struct sockaddr_in *servaddr = &fbs_dev->replicas.replicas[0].data.servaddr;

    while (sock->ops->connect(sock, 
                               (struct sockaddr *)servaddr, 
                               sizeof(struct sockaddr), 
                               O_RDWR)) {
        pr_err("failed to reconnect\n");
    }

    return 0;
}

void fail_request(struct freebs_request *req)
{
    struct freebs_device *fbs_dev = req->fbs_dev;

    fbs_debug("failing request %d, seq_num %d\n", req->req_num, req->seq_num);
    blk_end_request_all(req->req, -1);
    del_request(req, &fbs_dev->in_flight_l);
    kmem_cache_free(fbs_req_cache, req);
}

void fail_all_requests(struct freebs_device *fbs_dev)
{
    struct freebs_request *req;

    list_for_each_entry(req, &fbs_dev->in_flight, queue)
        fail_request(req);
        //blk_end_request_all(req->req, -1);
}

static void _bsdevice_cleanup(struct freebs_device *fbs_dev)
{
    cleanup_socks(fbs_dev);
    //blk_stop_queue(fbs_dev->fbs_queue);
    fail_all_requests(fbs_dev);
}

/**
 * freebs_receiver
 * This is the receiver thread; on startup, it loops, reading from the socket
 * and completing requests.
 */
//void freebs_receiver(struct work_struct *work) 
int freebs_receiver(void *private)
{
    struct receiver_data *data = private;
    struct freebs_device *fbs_dev = data->fbs_dev;
    int replica_num = data->replica;
    struct replica *replica = &fbs_dev->replicas.replicas[replica_num];
    struct freebs_socket *fbs_sock = &replica->data;
    struct fbs_response res;
    struct freebs_request *req;
    int rv, bytesRead, status = -1;
    uint32_t req_num;
    
    for (;;) {
        bytesRead = 0;
        do {
            fbs_debug("trying... %d\n", bytesRead);
            rv = fbs_recv(fbs_sock, &res + bytesRead, sizeof(struct fbs_response) - bytesRead);
            if (rv < 0) 
                return -1;
            bytesRead += rv;
        } while (bytesRead != sizeof(struct fbs_response));
        req_num = be32_to_cpu(res.req_num);
        fbs_debug("completing req %d\n", req_num);
        req = get_request(&fbs_dev->in_flight, &fbs_dev->in_flight_l, req_num);
        if (!req) {
            pr_err("freebs: unexpected request completion! skipping...\n");
            continue;
        }
        if (res.status == 0) {
            if (rq_data_dir(req->req) == READ) {
                /* read request returning */
                if (unlikely(complete_read(fbs_sock, req->req) < 0))
                    status = -1;
                else
                    status = 0;
                del_request(req, &fbs_dev->in_flight_l);
            } else {
                if (atomic_inc_return(&req->num_commits) >= fbs_dev->quorum) {
                    if (!del_request(req, &fbs_dev->in_flight_l))
                        continue;
                    status = 0;
                } else {
                    continue;
                }
            }
        } 
        blk_end_request_all(req->req, status); 
        kmem_cache_free(fbs_req_cache, req);
    }

    //_bsdevice_cleanup(fbs_dev);
}

void freebs_sender(struct work_struct *work) 
{
    struct sender_work *sender_work = container_of(work, struct sender_work, work);
    struct freebs_request *fbs_req = sender_work->fbs_req;
    struct freebs_socket *fbs_sock = sender_work->fbs_sock;
    struct freebs_device *fbs_dev = fbs_req->fbs_dev;
    struct request *req;
    struct bio_vec *bv;
    struct req_iterator iter;
    struct fbs_header hdr;
    sector_t sector_offset;
    sector_t sectors;
    u8 *buffer;
    int dir, ok;
    sector_t sector_cnt;

    enqueue_request(&fbs_req->queue, &fbs_dev->in_flight, &fbs_dev->in_flight_l);

    req = fbs_req->req;
    dir = rq_data_dir(req);
    if (dir == WRITE) 
        hdr.command = cpu_to_be16(FBS_WRITE);
    else
        hdr.command = cpu_to_be16(FBS_READ);
    sector_cnt = blk_rq_sectors(req);
    hdr.len = cpu_to_be32(fbs_req->size);
    hdr.offset = cpu_to_be32(fbs_req->sector);
    hdr.seq_num = cpu_to_be32(fbs_req->seq_num);
    hdr.req_num = cpu_to_be32(fbs_req->req_num);

    fbs_debug("sending %d\n", fbs_req->req_num);
    printk(KERN_DEBUG "freebs: Sector Offset: %lld; Length: %u bytes\n",
           (long long int) fbs_req->sector, fbs_req->size);

    if(!freebs_get_data_sock(fbs_sock)) 
        goto fail;

    ok = sizeof(hdr) == freebs_send(fbs_dev, fbs_sock->socket, &hdr, sizeof(hdr), 0);
    if (!ok) 
        goto fail;

    sector_offset = 0;
    if (dir == WRITE) {
        rq_for_each_segment(bv, req, iter) {
            buffer = page_address(bv->bv_page) + bv->bv_offset;
            if (bv->bv_len % KERNEL_SECTOR_SIZE != 0) 
                printk(KERN_ERR "freebs: Should never happen: "
                       "bio size (%d) is not a multiple of KERNEL_SECTOR_SIZE (%d).\n"
                       "This may lead to data truncation.\n",
                       bv->bv_len, KERNEL_SECTOR_SIZE);
            sectors = bv->bv_len / KERNEL_SECTOR_SIZE;
            ok = sectors * KERNEL_SECTOR_SIZE == freebs_send(fbs_dev, fbs_sock->socket, buffer, sectors * KERNEL_SECTOR_SIZE, 0);
            if (!ok) 
                goto fail;
            sector_offset += sectors;
        }
        if (sector_offset != sector_cnt) 
            printk(KERN_ERR "freebs: bio info doesn't match with the request info\n");
    }
    freebs_put_data_sock(fbs_sock);
    kmem_cache_free(sender_work_cache, sender_work);
    return;

fail:
    printk(KERN_ERR "freebs: send failed\n");
    freebs_put_data_sock(fbs_sock);
    fail_request(fbs_req);
}

static struct freebs_request *new_freebs_request(void)
{
    struct freebs_request *fbs_req;
    if (!(fbs_req = kmem_cache_alloc(fbs_req_cache, GFP_KERNEL)))
        return NULL;
    atomic_set(&fbs_req->num_commits, 0);
    return fbs_req;
}

static struct sender_work *new_sender_work(void)
{
    struct sender_work *sender_work;
    if (!(sender_work = kmem_cache_alloc(sender_work_cache, GFP_KERNEL)))
        return NULL;
    INIT_WORK(&sender_work->work, freebs_sender);
    return sender_work;
}

/*
 * Actual Data transfer
 */
static int fbs_transfer(struct request *req)
{
    struct freebs_device *fbs_dev = req->rq_disk->private_data;
    struct freebs_request *fbs_req;
    struct sender_work *sender_work;
    sector_t start_sector = blk_rq_pos(req);
    sector_t sector_cnt = blk_rq_sectors(req);
    int ret = 0;

    /* create and populate freebs_request struct */
    if (!(fbs_req = new_freebs_request()))
        return -ENOMEM;
    if (!(sender_work = new_sender_work()))
        return -ENOMEM;
    sender_work->fbs_req = fbs_req;
    sender_work->fbs_sock = primary(fbs_dev);
    fbs_req->fbs_dev = fbs_dev;
    fbs_req->sector = start_sector;
    fbs_req->size = sector_cnt * KERNEL_SECTOR_SIZE;
    fbs_req->req = req;
    if (rq_data_dir(req) == WRITE) 
        fbs_req->seq_num = atomic_add_return(1, &fbs_dev->packet_seq);
    else
        fbs_req->seq_num = atomic_read(&fbs_dev->packet_seq);
    fbs_req->req_num = atomic_add_return(1, &fbs_dev->req_num);
    fbs_debug("enqueueing request num %d\n", fbs_req->req_num);
    queue_work(fbs_dev->replicas.replicas[0].data.work_queue, &sender_work->work);

    return ret;
}

/*
 * Represents a block I/O request for us to execute
 */
static void fbs_request(struct request_queue *q)
{
    struct request *req;
    int ret;

    /* Gets the current request from the dispatch queue */
    while ((req = blk_fetch_request(q)) != NULL) {
#if 0
        /*
         * This function tells us whether we are looking at a filesystem request
         * - one that moves block of data
         */
        if (!blk_fs_request(req)) {
            printk(KERN_NOTICE "freebs: Skip non-fs request\n");
            /* We pass 0 to indicate that we successfully completed the request */
            __blk_end_request_all(req, 0);
            //__blk_end_request(req, 0, blk_rq_bytes(req));
            continue;
        }
#endif
        ret = fbs_transfer(req);
        if (ret < 0) {
            blk_end_request_all(req, ret);
        }
    }
}

/*
 * These are the file operations that performed on the FreEBS block device
 */
static struct block_device_operations fbs_fops = {
    .owner = THIS_MODULE,
    .open = fbs_open,
    .release = fbs_close,
};

/*
 * This is the registration and initialization section of the FreEBS block device
 * driver
 */
static int __init fbs_init(void)
{
    int ret;

    fbs_req_cache = kmem_cache_create("freebs_request", sizeof(struct freebs_request),
            0, SLAB_POISON | SLAB_RED_ZONE, NULL);
    if (!fbs_req_cache)
        return -ENOMEM;
   
    sender_work_cache = kmem_cache_create("sender_work", sizeof(struct sender_work),
            0, SLAB_POISON | SLAB_RED_ZONE, NULL);
    if (!sender_work_cache)
        return -ENOMEM;
   
    ret = bsdevice_init(&fbs_dev);

    if (ret > 0)
        return 0;

    return ret;
}

/*
 * This is the unregistration and uninitialization section of the FreEBS block
 * device driver
 */
static void fbs_cleanup(void)
{
    unregister_blkdev(freebs_major, "freebs");
    bsdevice_cleanup(&fbs_dev);
    kmem_cache_destroy(fbs_req_cache);
}

int bsdevice_init(struct freebs_device *fbs_dev)
{
    int r;

    if(freebs_init_socks(fbs_dev))
        return -1;

    if ((r = establish_connections(fbs_dev))) {
        printk(KERN_ERR "error connecting: %d\n", r);
        return r;
    }
    fbs_debug("connected\n");

    INIT_LIST_HEAD(&fbs_dev->in_flight);
    rwlock_init(&fbs_dev->in_flight_l);

    //fbs_dev->receiver = kthread_run(freebs_receiver, fbs_dev, "fbs");

    fbs_dev->size = FREEBS_DEVICE_SIZE;

    /* Get Registered */
    freebs_major = register_blkdev(freebs_major, "freebs");
    if (freebs_major <= 0) {
        printk(KERN_ERR "freebs: Unable to get Major Number\n");
        bsdevice_cleanup(fbs_dev);
        return -EBUSY;
    }

    /* Get a request queue (here queue is created) */
    spin_lock_init(&fbs_dev->lock);
    fbs_dev->fbs_queue = blk_init_queue(fbs_request, &fbs_dev->lock);
    if (fbs_dev->fbs_queue == NULL) {
        printk(KERN_ERR "freebs: blk_init_queue failure\n");
        unregister_blkdev(freebs_major, "freebs");
        bsdevice_cleanup(fbs_dev);
        return -ENOMEM;
    }

    /*
     * Add the gendisk structure
     * By using this memory allocation is involved,
     * the minor number we need to pass bcz the device
     * will support this much partitions
     */
    fbs_dev->fbs_disk = alloc_disk(FREEBS_MINOR_CNT);
    if (!fbs_dev->fbs_disk) {
        printk(KERN_ERR "freebs: alloc_disk failure\n");
        blk_cleanup_queue(fbs_dev->fbs_queue);
        unregister_blkdev(freebs_major, "freebs");
        bsdevice_cleanup(fbs_dev);
        return -ENOMEM;
    }

    /* Setting the major number */
    fbs_dev->fbs_disk->major = freebs_major;
    /* Setting the first mior number */
    fbs_dev->fbs_disk->first_minor = FREEBS_FIRST_MINOR;
    /* Initializing the device operations */
    fbs_dev->fbs_disk->fops = &fbs_fops;
    /* Driver-specific own internal data */
    fbs_dev->fbs_disk->private_data = fbs_dev;
    fbs_dev->fbs_disk->queue = fbs_dev->fbs_queue;
    atomic_set(&fbs_dev->packet_seq, 0);
    /*
     * You do not want partition information to show up in
     * cat /proc/partitions set this flags
     */
    //fbs_dev->fbs_disk->flags = GENHD_FL_SUPPRESS_PARTITION_INFO;
    sprintf(fbs_dev->fbs_disk->disk_name, "freebs");
    /* Setting the capacity of the device in its gendisk structure */
    set_capacity(fbs_dev->fbs_disk, fbs_dev->size);

    /* Adding the disk to the system */
    add_disk(fbs_dev->fbs_disk);
    /* Now the disk is "live" */
    printk(KERN_INFO "freebs: FreEBS driver initialised (%d sectors; %d bytes)\n",
           fbs_dev->size, fbs_dev->size * KERNEL_SECTOR_SIZE);

    return FREEBS_DEVICE_SIZE;
}

void bsdevice_cleanup(struct freebs_device *fbs_dev)
{
    _bsdevice_cleanup(fbs_dev);
    flush_workqueue(fbs_dev->replicas.replicas[0].data.work_queue);
    destroy_workqueue(fbs_dev->replicas.replicas[0].data.work_queue); 
    del_gendisk(fbs_dev->fbs_disk);
    put_disk(fbs_dev->fbs_disk);
    blk_cleanup_queue(fbs_dev->fbs_queue);
}

/*
 * you must have down()ed the appropriate [m]sock_mutex elsewhere!
 *
 * buf = buffer if write, null if read
 * direction = true for write, false for read
 * size = size in bytes
 */
int freebs_send(struct freebs_device *fbs_dev, struct socket *sock,
                void *buf, size_t size, unsigned msg_flags)
{
    struct kvec iov;
    struct msghdr msg;
    int rv, sent = 0;

    if (!sock)
        return -1000;

    iov.iov_base = buf;
    iov.iov_len  = size;

    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags      = msg_flags | MSG_NOSIGNAL;

    do {
        /* STRANGE
         * tcp_sendmsg does _not_ use its size parameter at all ?
         *
         * -EAGAIN on timeout, -EINTR on signal.
         */
        /* THINK
         * do we need to block DRBD_SIG if sock == &meta.socket ??
         * otherwise wake_asender() might interrupt some send_*Ack !
         */
        rv = kernel_sendmsg(sock, &msg, &iov, 1, size);
        if (rv == -EAGAIN) {
            /*
            if (we_should_drop_the_connection(fbs_dev, sock))
            break;
            else
            */
            continue;
        }
        D_ASSERT(rv != 0);
        if (rv == -EINTR) {
            flush_signals(current);
            rv = 0;
        }
        if (rv < 0)
            break;
        sent += rv;
        iov.iov_base += rv;
        iov.iov_len  -= rv;
    } while (sent < size);

    if (rv <= 0) {
        if (rv != -EAGAIN) {
            dev_err(DEV, "sendmsg returned %d\n",
                    rv);
            //drbd_force_state(fbs_dev, NS(conn, C_BROKEN_PIPE));
        } //else
        //drbd_force_state(fbs_dev, NS(conn, C_TIMEOUT));
    }

    return sent;
}

/*
 * Tries to establish a connection with each device. Returns 0
 * on success, -1 on failure.
 */
int establish_connections(struct freebs_device *fbs_dev) 
{
    struct sockaddr_in *servaddr;
    struct freebs_socket *fbs_sock;
    struct socket *sock;
    int r, i;

    for (i = 0; i < num_replicas; i++) {
        fbs_debug("connecting to %d...\n", i);
        fbs_sock = &fbs_dev->replicas.replicas[i].data;
        sock = fbs_sock->socket;
        servaddr = &fbs_sock->servaddr;

        r = sock->ops->connect(sock, (struct sockaddr *)servaddr, sizeof(struct sockaddr), O_RDWR);

        if (r) {
            printk(KERN_ERR "error connecting: %d\n", r);
            return r;
        }

        if (!(fbs_sock->receiver = 
                    kthread_run(freebs_receiver, 
                        &fbs_dev->replicas.replicas[i].receiver_data, 
                        "fbs-recv-%d", i)))
            return -ENOMEM;

        fbs_sock->work_queue = create_singlethread_workqueue("fbs_send");
        if (!fbs_sock->work_queue) {
            fbs_err("error creating workqueue\n");
            return -1;
        }
    }

    return 0;
}

/*
 * Returns 0 on success, -1 on failure
 */
int freebs_init_socks(struct freebs_device *fbs_dev)
{
    struct sockaddr_in *servaddr;
    struct replica *replica;
    struct socket *sock;
    int r, i;

    if (num_replicas < 1) {
        fbs_err("must specify at least one replica!");
        return -1;
    }
    fbs_debug("initializing %d replicas...\n", num_replicas);
    if (!(fbs_dev->replicas.replicas = kzalloc(sizeof(struct replica) * num_replicas,
                    GFP_KERNEL)))
        goto replica_fail;
    fbs_debug("used %llu bytes\n", (unsigned long long) sizeof(struct replica) * num_replicas);
    fbs_dev->replicas.num_replicas = num_replicas;
    for (i = 0; i < num_replicas; i++) {
        replica = &fbs_dev->replicas.replicas[i];
        servaddr = &replica->data.servaddr;
        memset(servaddr, 0, sizeof(struct sockaddr_in));
        servaddr->sin_family = AF_INET;
        servaddr->sin_port = htons(9000);
        servaddr->sin_addr.s_addr = in_aton(replica_ips[i]);
        sock = NULL;
        r = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
        if (r) {
            printk(KERN_ERR "error creating socket: %d", r);
            goto replica_fail;
        }
        replica->data.socket = sock;
        mutex_init(&replica->data.mutex);
        replica->receiver_data.fbs_dev = fbs_dev;
        replica->receiver_data.replica = i;
    }

    fbs_dev->quorum = num_replicas / 2 + 1;

    return 0;
    
replica_fail:
    fbs_debug("failed to initialize replicas!\n");
    // TODO: free memory and return an error
    return -1;
}

module_init(fbs_init);
module_exit(fbs_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Paton, Becky Lam, and Igor Canadi <jpaton2@gmail.com>");
MODULE_DESCRIPTION("FreEBS Block Driver");
MODULE_ALIAS_BLOCKDEV_MAJOR(freebs_major);
