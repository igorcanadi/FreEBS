/* Disk on FreEBS Driver */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/errno.h>

#include "freebs.h"

#define FREEBS_FIRST_MINOR 0
#define FREEBS_MINOR_CNT 16

static u_int freebs_major = 0;

struct fbs_header {
    u8  command;
    u64 len;        // length in bytes
    u32 offset;     // offset in virtual disk in bytes
};

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

/*
 * Actual Data transfer
 */
static int fbs_transfer(struct request *req)
{
    //struct freebs_device *dev = (struct freebs_device *)(req->rq_disk->private_data);

    struct bio_vec *bv;
    struct req_iterator iter;
    struct fbs_header hdr;
    sector_t sector_offset;
    unsigned int sectors;
    u8 *buffer;

    int dir = rq_data_dir(req);
    sector_t start_sector = blk_rq_pos(req);
    unsigned int sector_cnt = blk_rq_sectors(req);
    int ret = 0;

    if (dir == WRITE)
        hdr.command = FBS_WRITE;
    else
        hdr.command = FBS_READ;
    hdr.len = sector_cnt * FREEBS_SECTOR_SIZE;
    hdr.offset = start_sector;

    freebs_send(&fbs_dev, fbs_dev.data.socket, &hdr, sizeof(hdr), 0);

    //printk(KERN_DEBUG "freebs: Dir:%d; Sec:%lld; Cnt:%d\n", dir, start_sector, sector_cnt);

    sector_offset = 0;
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
        printk(KERN_DEBUG "freebs: Sector Offset: %lld; Buffer: %p; Length: %d sectors\n",
               (long long int) sector_offset, buffer, sectors);
        freebs_send(&fbs_dev, fbs_dev.data.socket, buffer, sectors * FREEBS_SECTOR_SIZE, 0);
        sector_offset += sectors;
    }
    if (sector_offset != sector_cnt) {
        printk(KERN_ERR "freebs: bio info doesn't match with the request info");
        ret = -EIO;
    }

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
        __blk_end_request_all(req, ret);
        //__blk_end_request(req, ret, blk_rq_bytes(req));
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

    /* Set up our FreEBS Device */
    if ((ret = bsdevice_init()) < 0) {
        return ret;
    }
    fbs_dev.size = ret;

    /* Get Registered */
    freebs_major = register_blkdev(freebs_major, "freebs");
    if (freebs_major <= 0) {
        printk(KERN_ERR "freebs: Unable to get Major Number\n");
        bsdevice_cleanup();
        return -EBUSY;
    }

    /* Get a request queue (here queue is created) */
    spin_lock_init(&fbs_dev.lock);
    fbs_dev.fbs_queue = blk_init_queue(fbs_request, &fbs_dev.lock);
    if (fbs_dev.fbs_queue == NULL) {
        printk(KERN_ERR "freebs: blk_init_queue failure\n");
        unregister_blkdev(freebs_major, "freebs");
        bsdevice_cleanup();
        return -ENOMEM;
    }

    /*
     * Add the gendisk structure
     * By using this memory allocation is involved,
     * the minor number we need to pass bcz the device
     * will support this much partitions
     */
    fbs_dev.fbs_disk = alloc_disk(FREEBS_MINOR_CNT);
    if (!fbs_dev.fbs_disk) {
        printk(KERN_ERR "freebs: alloc_disk failure\n");
        blk_cleanup_queue(fbs_dev.fbs_queue);
        unregister_blkdev(freebs_major, "freebs");
        bsdevice_cleanup();
        return -ENOMEM;
    }

    /* Setting the major number */
    fbs_dev.fbs_disk->major = freebs_major;
    /* Setting the first mior number */
    fbs_dev.fbs_disk->first_minor = FREEBS_FIRST_MINOR;
    /* Initializing the device operations */
    fbs_dev.fbs_disk->fops = &fbs_fops;
    /* Driver-specific own internal data */
    fbs_dev.fbs_disk->private_data = &fbs_dev;
    fbs_dev.fbs_disk->queue = fbs_dev.fbs_queue;
    /*
     * You do not want partition information to show up in
     * cat /proc/partitions set this flags
     */
    //fbs_dev.fbs_disk->flags = GENHD_FL_SUPPRESS_PARTITION_INFO;
    sprintf(fbs_dev.fbs_disk->disk_name, "freebs");
    /* Setting the capacity of the device in its gendisk structure */
    set_capacity(fbs_dev.fbs_disk, fbs_dev.size);

    /* Adding the disk to the system */
    add_disk(fbs_dev.fbs_disk);
    /* Now the disk is "live" */
    printk(KERN_INFO "freebs: FreEBS driver initialised (%d sectors; %d bytes)\n",
           fbs_dev.size, fbs_dev.size * FREEBS_SECTOR_SIZE);

    return 0;
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
    bsdevice_cleanup();
}

module_init(fbs_init);
module_exit(fbs_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Paton, Becky Lam, and Igor Canadi <jpaton2@gmail.com>");
MODULE_DESCRIPTION("FreEBS Block Driver");
MODULE_ALIAS_BLOCKDEV_MAJOR(freebs_major);
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in.h>

#define FREEBS_DEVICE_SIZE 1024 /* sectors */
/* So, total device size = 1024 * 512 bytes = 512 KiB */

__be32 in_aton(const char *);

int bsdevice_init(void)
{
    int r;
    struct sockaddr_in *servaddr = &fbs_dev.data.servaddr;
    struct socket *sock;

    memset(servaddr, 0, sizeof(struct sockaddr_in));
    r = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
    if (r) {
        printk(KERN_ERR "error creating socket: %d", r);
        return r;
    }
    fbs_dev.data.socket = sock;
    servaddr->sin_family = AF_INET;
    servaddr->sin_port = htons(9000);
    servaddr->sin_addr.s_addr = in_aton("192.168.56.10");

    r = sock->ops->connect(sock, (struct sockaddr *)servaddr, sizeof(struct sockaddr), O_RDWR);
    if (r) {
        printk(KERN_ERR "error connecting: %d", r);
        return r;
    }
    /* TODO: add error handling once I figure out where to find the damn documentation
     * for these functions */

    return FREEBS_DEVICE_SIZE;
}

void bsdevice_cleanup(void)
{
    if (fbs_dev.data.socket)
        sock_release(fbs_dev.data.socket);
}


#include <linux/blk_types.h>
#include <linux/bio.h>
#include <linux/socket.h>
#include <net/sock.h>

#define FBS_WRITE (1 << 0)

bool disable_sendpage = false;

/*
int freebs_send_write(struct socket *sock, void *buf,
        unsigned int nr_sectors)
{
    struct freebs_header fbshdr;
    fbshdr.flags = FBS_WRITE;
    fbshdr.nr_sectors = cpu_to_be32(nr_sectors);
    freebs_send(sock, &fbshdr, sizeof(fbshdr), MSG_MORE);
    freebs_send(sock, buf, nr_sectors * KERNEL_SECTOR_SIZE, NULL);
}
*/

/* I understand drbd_no_send_page, but I don't understand drbd_send_page, which
 * seems much more complicated. So I am going with this one.
 * -- Jim
 */
/* The idea of sendpage seems to be to put some kind of reference
 * to the page into the skb, and to hand it over to the NIC. In
 * this process get_page() gets called.
 *
 * As soon as the page was really sent over the network put_page()
 * gets called by some part of the network layer. [ NIC driver? ]
 *
 * [ get_page() / put_page() increment/decrement the count. If count
 *   reaches 0 the page will be freed. ]
 *
 * This works nicely with pages from FSs.
 * But this means that in protocol A we might signal IO completion too early!
 *
 * In order not to corrupt data during a resync we must make sure
 * that we do not reuse our own buffer pages (EEs) to early, therefore
 * we have the net_ee list.
 *
 * XFS seems to have problems, still, it submits pages with page_count == 0!
 * As a workaround, we disable sendpage on pages
 * with page_count == 0 or PageSlab.
 */
static int _freebs_no_send_page(struct freebs_device *fbs_dev, struct page *page,
                                int offset, size_t size, unsigned msg_flags)
{
    int sent = freebs_send(fbs_dev, fbs_dev->data.socket, kmap(page) + offset, size, msg_flags);
    kunmap(page);
    /*
    if (sent == size)
        fbs_dev->send_cnt += size>>9;
        */
    return sent == size;
}

/*
static int _freebs_send_bio(struct freebs_device *fbs_dev, struct bio *bio)
{
	struct bio_vec *bvec;
	int i;
	* hint all but last page with MSG_MORE *
	bio_for_each_segment(bvec, bio, i) {
		if (!_freebs_no_send_page(fbs_dev, bvec->bv_page,
				     bvec->bv_offset, bvec->bv_len,
				     i == bio->bi_vcnt -1 ? 0 : MSG_MORE))
			return 0;
	}
	return 1;
}
*/

/*
static inline void freebs_update_congested(struct freebs_device *fbs_dev)
{
    struct sock *sk = fbs_dev->data.socket->sk;
    if (sk->sk_wmem_queued > sk->sk_sndbuf * 4 / 5)
        set_bit(NET_CONGESTED, &fbs_dev->flags);
}
*/

static int _freebs_send_page(struct freebs_device *fbs_dev, struct page *page,
                             int offset, size_t size, unsigned msg_flags)
{
    mm_segment_t oldfs = get_fs();
    int sent, ok;
    int len = size;

    /* e.g. XFS meta- & log-data is in slab pages, which have a
     * page_count of 0 and/or have PageSlab() set.
     * we cannot use send_page for those, as that does get_page();
     * put_page(); and would cause either a VM_BUG directly, or
     * __page_cache_release a page that would actually still be referenced
     * by someone, leading to some obscure delayed Oops somewhere else. */
    if (disable_sendpage || (page_count(page) < 1) || PageSlab(page))
        return _freebs_no_send_page(fbs_dev, page, offset, size, msg_flags);

    msg_flags |= MSG_NOSIGNAL;
    //freebs_update_congested(fbs_dev);
    set_fs(KERNEL_DS);
    do {
        sent = fbs_dev->data.socket->ops->sendpage(fbs_dev->data.socket, page,
                                                offset, len,
                                                msg_flags);
        if (sent == -EAGAIN) {
            /*
            if (we_should_drop_the_connection(fbs_dev,
                                              fbs_dev->data.socket))
                break;
            else
                continue;
                */
            continue;
        }
        if (sent <= 0) {
            dev_warn(DEV, "%s: size=%d len=%d sent=%d\n",
                     __func__, (int)size, len, sent);
            break;
        }
        len    -= sent;
        offset += sent;
    } while (len > 0 /* THINK && fbs_dev->cstate >= C_CONNECTED*/);
    set_fs(oldfs);
    //clear_bit(NET_CONGESTED, &fbs_dev->flags);

    ok = (len == 0);
    /*
    if (likely(ok))
        fbs_dev->send_cnt += size>>9;
        */
    return ok;
}


static int _freebs_send_zc_bio(struct freebs_device *fbs_dev, struct bio *bio)
{
    struct bio_vec *bvec;
    int i;
    /* hint all but last page with MSG_MORE */
    bio_for_each_segment(bvec, bio, i) {
        if (!_freebs_send_page(fbs_dev, bvec->bv_page,
                               bvec->bv_offset, bvec->bv_len,
                               i == bio->bi_vcnt -1 ? 0 : MSG_MORE))
            return 0;
    }
    return 1;
}

/* Used to send write requests
 * R_PRIMARY -> Peer	(P_DATA)
 */
int freebs_send_dblock(struct freebs_device *fbs_dev, struct freebs_request *req)
{
    int ok = 1;
    struct p_data p;
    //unsigned int dp_flags = 0;
    //void *dgb;
    //int dgs;

    if (!freebs_get_data_sock(fbs_dev))
        return 0;

    /*
    dgs = (fbs_dev->agreed_pro_version >= 87 && fbs_dev->integrity_w_tfm) ?
    crypto_hash_digestsize(fbs_dev->integrity_w_tfm) : 0;

    if (req->size <= DRBD_MAX_SIZE_H80_PACKET) {
    p.head.h80.magic   = BE_DRBD_MAGIC;
    p.head.h80.command = cpu_to_be16(P_DATA);
    p.head.h80.length  =
    	cpu_to_be16(sizeof(p) - sizeof(union p_header) + dgs + req->size);
    } else {
    p.head.h95.magic   = BE_DRBD_MAGIC_BIG;
    p.head.h95.command = cpu_to_be16(P_DATA);
    p.head.h95.length  =
    	cpu_to_be32(sizeof(p) - sizeof(union p_header) + dgs + req->size);
    }
    */
    p.head.h.command = cpu_to_be16(P_DATA);
    p.head.h.length  =
        cpu_to_be32(sizeof(p) - sizeof(union p_header) + /*dgs +*/ req->size);

    p.sector   = cpu_to_be64(req->sector);
    p.block_id = (unsigned long)req;
    p.seq_num  = cpu_to_be32(atomic_add_return(1, &fbs_dev->packet_seq));

    /*
    dp_flags = bio_flags_to_wire(fbs_dev, req->master_bio->bi_rw);

    if (fbs_dev->state.conn >= C_SYNC_SOURCE &&
      fbs_dev->state.conn <= C_PAUSED_SYNC_T)
    dp_flags |= DP_MAY_SET_IN_SYNC;

    p.dp_flags = cpu_to_be32(dp_flags);
    set_bit(UNPLUG_REMOTE, &fbs_dev->flags);
    */
    ok = (sizeof(p) ==
          freebs_send(fbs_dev, fbs_dev->data.socket, &p, sizeof(p), 0));
    /*
    if (ok && dgs) {
    dgb = fbs_dev->int_dig_out;
    drbd_csum_bio(fbs_dev, fbs_dev->integrity_w_tfm, req->master_bio, dgb);
    ok = dgs == drbd_send(fbs_dev, fbs_dev->data.socket, dgb, dgs, 0);
    }
    */
    if (ok) {
        /* For protocol A, we have to memcpy the payload into
         * socket buffers, as we may complete right away
         * as soon as we handed it over to tcp, at which point the data
         * pages may become invalid.
         *
         * For data-integrity enabled, we copy it as well, so we can be
         * sure that even if the bio pages may still be modified, it
         * won't change the data on the wire, thus if the digest checks
         * out ok after sending on this side, but does not fit on the
         * receiving side, we sure have detected corruption elsewhere.
        if (fbs_dev->net_conf->wire_protocol == DRBD_PROT_A || dgs)
        	ok = _drbd_send_bio(fbs_dev, req->master_bio);
        else
        	ok = _drbd_send_zc_bio(fbs_dev, req->master_bio);
         */
        ok = _freebs_send_zc_bio(fbs_dev, req->master_bio);

        /* double check digest, sometimes buffers have been modified in flight. */
        //if (dgs > 0 && dgs <= 64) {
        /* 64 byte, 512 bit, is the largest digest size
         * currently supported in kernel crypto.
        unsigned char digest[64];
        drbd_csum_bio(fbs_dev, fbs_dev->integrity_w_tfm, req->master_bio, digest);
        if (memcmp(fbs_dev->int_dig_out, digest, dgs)) {
        	dev_warn(DEV,
        		"Digest mismatch, buffer modified by upper layers during write: %llus +%u\n",
        		(unsigned long long)req->sector, req->size);
        }
        }*/ /* else if (dgs > 64) {
... Be noisy about digest too large ...
} */
    }

    freebs_put_data_sock(fbs_dev);

    return ok;
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
