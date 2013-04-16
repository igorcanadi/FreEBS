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
#include <net/sock.h>

#define FREEBS_DEVICE_SIZE 1024000 /* sectors */
/* So, total device size = 2048 * 512 bytes = 1024 KiB = 1 MiB */

__be32 in_aton(const char *);

#include "freebs.h"

#define FREEBS_FIRST_MINOR 0
#define FREEBS_MINOR_CNT 16

static u_int freebs_major = 0;

struct freebs_device fbs_dev;

static int fbs_open(struct block_device *bdev, fmode_t mode)
{
    unsigned unit = iminor(bdev->bd_inode);

    printk(KERN_INFO "freebs: Device is opened\n");
    printk(KERN_INFO "freebs: Inode number is %d\n", unit);

    if (unit > FREEBS_MINOR_CNT)
        return -ENODEV;
    return 0;
}

static int fbs_close(struct gendisk *disk, fmode_t mode)
{
    printk(KERN_INFO "freebs: Device is closed\n");
    return 0;
}

static int fbs_recv(struct freebs_device *fbs_dev, void *buf, size_t size)
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
	int rv;

	oldfs = get_fs();
	set_fs(KERNEL_DS);

	for (;;) {
		rv = sock_recvmsg(fbs_dev->data.socket, &msg, size, msg.msg_flags);
		if (rv == size)
			break;

		/* Note:
		 * ECONNRESET	other side closed the connection
		 * ERESTARTSYS	(on  sock) we got a signal
		 */

		if (rv < 0) {
			if (rv == -ECONNRESET)
				dev_info(DEV, "sock was reset by peer\n");
			else if (rv != -ERESTARTSYS)
				dev_err(DEV, "sock_recvmsg returned %d\n", rv);
			break;
		} else if (rv == 0) {
			dev_info(DEV, "sock was shut down by peer\n");
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

    /*
     * TODO: deal with rv != size
	if (rv != size)
		fbs_force_state(mdev, NS(conn, C_BROKEN_PIPE));
    */

	return rv;
}

void enqueue_request(struct list_head *new, struct list_head *queue, struct mutex *mutex)
{
    mutex_lock(mutex);
    list_add_tail(new, queue);
    mutex_unlock(mutex);
}

/**
 * get_request
 * Gets the fbs_request from the queue with the seq_num provided. Removes it from
 * the queue and returns it.
 */
struct freebs_request *get_request(struct list_head *queue, struct mutex *mutex, int seq_num) {
    struct list_head *pos;
    struct freebs_request *req = NULL;
    bool found = false;

    mutex_lock(mutex);
    list_for_each(pos, queue) {
        req = list_entry(pos, struct freebs_request, queue);
        if (req->seq_num == seq_num) {
            list_del(&req->queue);
            found = true;
            break;
        }
    }
    mutex_unlock(mutex);

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
int complete_read(struct freebs_device *fbs_dev, struct request *req) {
    struct bio_vec *bv;
    struct req_iterator iter;
    u8 *buffer;
    int rv, bytes_read = 0;

    rq_for_each_segment(bv, req, iter) {
        buffer = page_address(bv->bv_page) + bv->bv_offset;
        rv = fbs_recv(fbs_dev, buffer, bv->bv_len);
        if (rv != bv->bv_len) {
            printk(KERN_ERR "couldn't complete read!");
            if (rv < 0)
                return rv;
            else
                return -1; // THINK: what should we return here?
        }
        bytes_read += rv;
    }

    return bytes_read;
}

/**
 * freebs_receiver
 * This is the receiver thread; on startup, it loops, reading from the socket
 * and completing requests.
 */
int freebs_receiver(void *data)
{
    struct freebs_device *fbs_dev = data;
    struct fbs_response res;
    struct freebs_request *req;
    //struct socket *sock = fbs_dev->data.socket;
    int rv, status;
    uint32_t seq_num;
    
    for (;;) {
        rv = fbs_recv(fbs_dev, &res, sizeof(struct fbs_response));
        if (rv == sizeof(struct fbs_response)) {
            seq_num = be32_to_cpu(res.seq_num);
            req = get_request(&fbs_dev->in_flight, &fbs_dev->in_flight_l, seq_num);
            if (res.status == 0) {
                if (rq_data_dir(req->req) == READ) {
                    /* read request returning */
                    if (complete_read(fbs_dev, req->req) < 0)
                        status = -1;
                    else
                        status = 0;
                }
                else {
                    status = 0;
                }
            } else {
                status = -1;
            }
            blk_end_request_all(req->req, status); //res.status == 0 ? 0 : -1);
            kfree(req);
        }
    }
}

int freebs_sender(void *data) 
{
    struct freebs_device *fbs_dev = data;
    struct freebs_request *fbs_req;
    struct request *req;
    struct bio_vec *bv;
    struct req_iterator iter;
    struct fbs_header hdr;
    sector_t sector_offset;
    unsigned int sectors;
    u8 *buffer;
    int ok, dir;

    unsigned int sector_cnt;

    while (!down_interruptible(&fbs_dev->rq_queue_sem)) {
        mutex_lock(&fbs_dev->rq_mutex);
        if (list_empty(&fbs_dev->rq_queue)) {
            mutex_unlock(&fbs_dev->rq_mutex);
            continue;
        }
        do {
            fbs_req = list_first_entry(&fbs_dev->rq_queue, struct freebs_request, queue);
            list_del(&fbs_req->queue);
            mutex_unlock(&fbs_dev->rq_mutex);
            mutex_lock(&fbs_dev->in_flight_l);
            list_add_tail(&fbs_req->queue, &fbs_dev->in_flight);
            mutex_unlock(&fbs_dev->in_flight_l);

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

            freebs_get_data_sock(fbs_dev);
            ok = sizeof(hdr) == freebs_send(fbs_dev, fbs_dev->data.socket, &hdr, sizeof(hdr), 0);
            // TODO: do something about ok

            sector_offset = 0;
            if (dir == WRITE) {
                rq_for_each_segment(bv, req, iter) {
                    buffer = page_address(bv->bv_page) + bv->bv_offset;
                    if (bv->bv_len % FREEBS_SECTOR_SIZE != 0) 
                        printk(KERN_ERR "freebs: Should never happen: "
                               "bio size (%d) is not a multiple of FREEBS_SECTOR_SIZE (%d).\n"
                               "This may lead to data truncation.\n",
                               bv->bv_len, FREEBS_SECTOR_SIZE);
                    sectors = bv->bv_len / FREEBS_SECTOR_SIZE;
                    //printk(KERN_DEBUG "freebs: Sector Offset: %lld; Buffer: %p; Length: %d sectors\n",
                           //(long long int) sector_offset, buffer, sectors);
                    //printk(KERN_DEBUG "sending data...");
                    freebs_send(fbs_dev, fbs_dev->data.socket, buffer, sectors * FREEBS_SECTOR_SIZE, 0);
                    sector_offset += sectors;
                }
                if (sector_offset != sector_cnt) 
                    printk(KERN_ERR "freebs: bio info doesn't match with the request info");
            }
            freebs_put_data_sock(fbs_dev);
        } while (!list_empty(&fbs_dev->rq_queue));
    }
    return 0;
}

/*
 * Actual Data transfer
 */
static int fbs_transfer(struct request *req)
{
    //struct freebs_device *dev = (struct freebs_device *)(req->rq_disk->private_data);

    /*
    struct bio_vec *bv;
    struct req_iterator iter;
    struct fbs_header hdr;
    sector_t sector_offset;
    unsigned int sectors;
    u8 *buffer;
    int ok;

    int dir = rq_data_dir(req);
    */
    struct freebs_device *fbs_dev = req->rq_disk->private_data;
    struct freebs_request *fbs_req;
    sector_t start_sector = blk_rq_pos(req);
    unsigned int sector_cnt = blk_rq_sectors(req);
    int ret = 0;

    /* create and populate freebs_request struct */
    /* TODO: use kmemcache instead of kmalloc */
    if (!(fbs_req = kmalloc(sizeof(struct freebs_request), GFP_ATOMIC))) {
        return -ENOMEM;
    }
    fbs_req->fbs_dev = fbs_dev;
    fbs_req->sector = start_sector;
    fbs_req->size = sector_cnt * FREEBS_SECTOR_SIZE;
    fbs_req->req = req;
    fbs_req->seq_num = atomic_add_return(1, &fbs_dev->packet_seq);
    enqueue_request(&fbs_req->queue, &fbs_dev->rq_queue, &fbs_dev->rq_mutex);
    up(&fbs_dev->rq_queue_sem);

    /*
    if (dir == WRITE)
        hdr.command = cpu_to_be16(FBS_WRITE);
    else
        hdr.command = cpu_to_be16(FBS_READ);
    hdr.len = cpu_to_be32(fbs_req->size);
    hdr.offset = cpu_to_be32(fbs_req->sector);
    hdr.seq_num = cpu_to_be32(fbs_req->seq_num);

    freebs_get_data_sock(fbs_dev);
    ok = sizeof(hdr) == freebs_send(fbs_dev, fbs_dev->data.socket, &hdr, sizeof(hdr), 0);
    // TODO: do something about ok

    sector_offset = 0;
    if (dir == WRITE) {
        rq_for_each_segment(bv, req, iter) {
            buffer = page_address(bv->bv_page) + bv->bv_offset;
            if (bv->bv_len % FREEBS_SECTOR_SIZE != 0) {
                printk(KERN_ERR "freebs: Should never happen: "
                       "bio size (%d) is not a multiple of FREEBS_SECTOR_SIZE (%d).\n"
                       "This may lead to data truncation.\n",
                       bv->bv_len, FREEBS_SECTOR_SIZE);
                ret = -EIO;
            }
            sectors = bv->bv_len / FREEBS_SECTOR_SIZE;
            //printk(KERN_DEBUG "freebs: Sector Offset: %lld; Buffer: %p; Length: %d sectors\n",
                   //(long long int) sector_offset, buffer, sectors);
            //printk(KERN_DEBUG "sending data...");
            freebs_send(fbs_dev, fbs_dev->data.socket, buffer, sectors * FREEBS_SECTOR_SIZE, 0);
            sector_offset += sectors;
        }
        if (sector_offset != sector_cnt) {
            printk(KERN_ERR "freebs: bio info doesn't match with the request info");
            ret = -EIO;
        }
    }
    freebs_put_data_sock(fbs_dev);
    */

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
    int ret = bsdevice_init(&fbs_dev);

    if (ret > 0)
        return 0;

    return ret;
}
/*
 * This is the unregistration and uninitialization section of the FreEBS block
 * device driver
 */
static void __exit fbs_cleanup(void)
{
    del_gendisk(fbs_dev.fbs_disk);
    put_disk(fbs_dev.fbs_disk);
    blk_cleanup_queue(fbs_dev.fbs_queue);
    unregister_blkdev(freebs_major, "freebs");
    bsdevice_cleanup(&fbs_dev);
}

int bsdevice_init(struct freebs_device *fbs_dev)
{
    int r;
    struct sockaddr_in *servaddr = &fbs_dev->data.servaddr;
    struct socket *sock;

    freebs_init_socks(fbs_dev);

    memset(servaddr, 0, sizeof(struct sockaddr_in));
    r = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
    if (r) {
        printk(KERN_ERR "error creating socket: %d", r);
        return r;
    }
    fbs_dev->data.socket = sock;
    servaddr->sin_family = AF_INET;
    servaddr->sin_port = htons(9000);
    servaddr->sin_addr.s_addr = in_aton("127.0.0.1");

    r = sock->ops->connect(sock, (struct sockaddr *)servaddr, sizeof(struct sockaddr), O_RDWR);

    if (r) {
        printk(KERN_ERR "error connecting: %d", r);
        return r;
    }

    INIT_LIST_HEAD(&fbs_dev->in_flight);
    INIT_LIST_HEAD(&fbs_dev->rq_queue);
    sema_init(&fbs_dev->rq_queue_sem, 0);
    mutex_init(&fbs_dev->in_flight_l);
    mutex_init(&fbs_dev->rq_mutex);
    //up(&fbs_dev->rq_queue_sem);

    kthread_run(freebs_receiver, fbs_dev, "fbs");
    kthread_run(freebs_sender, fbs_dev, "fbs");

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
           fbs_dev->size, fbs_dev->size * FREEBS_SECTOR_SIZE);

    return FREEBS_DEVICE_SIZE;
}

void bsdevice_cleanup(struct freebs_device *fbs_dev)
{
    if (fbs_dev->data.socket)
        sock_release(fbs_dev->data.socket);
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

void freebs_init_socks(struct freebs_device *fbs_dev)
{
    mutex_init(&fbs_dev->data.mutex);
}

module_init(fbs_init);
module_exit(fbs_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Paton, Becky Lam, and Igor Canadi <jpaton2@gmail.com>");
MODULE_DESCRIPTION("FreEBS Block Driver");
MODULE_ALIAS_BLOCKDEV_MAJOR(freebs_major);
