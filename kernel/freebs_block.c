/* Disk on FreEBS Driver */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/errno.h>

#include "freebs_device.h"

#define FREEBS_FIRST_MINOR 0
#define FREEBS_MINOR_CNT 16

static u_int freebs_major = 0;

/* 
 * The internal structure representation of our device
 */
static struct freebs_device
{
	/* Size is the size of the device (in sectors) */
	unsigned int size;
	/* For exclusive access to our request queue */
	spinlock_t lock;
	/* Our request queue */
	struct request_queue *fbs_queue;
	/* This is kernel's representation of an individual disk device */
	struct gendisk *fbs_disk;
} fbs_dev;

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

	int dir = rq_data_dir(req);
	sector_t start_sector = blk_rq_pos(req);
	unsigned int sector_cnt = blk_rq_sectors(req);

	struct bio_vec *bv;
	struct req_iterator iter;

	sector_t sector_offset;
	unsigned int sectors;
	u8 *buffer;

	int ret = 0;

	//printk(KERN_DEBUG "freebs: Dir:%d; Sec:%lld; Cnt:%d\n", dir, start_sector, sector_cnt);

	sector_offset = 0;
	rq_for_each_segment(bv, req, iter)
	{
		buffer = page_address(bv->bv_page) + bv->bv_offset;
		if (bv->bv_len % FREEBS_SECTOR_SIZE != 0)
		{
			printk(KERN_ERR "freebs: Should never happen: "
				"bio size (%d) is not a multiple of FREEBS_SECTOR_SIZE (%d).\n"
				"This may lead to data truncation.\n",
				bv->bv_len, FREEBS_SECTOR_SIZE);
			ret = -EIO;
		}
		sectors = bv->bv_len / FREEBS_SECTOR_SIZE;
		printk(KERN_DEBUG "freebs: Sector Offset: %lld; Buffer: %p; Length: %d sectors\n",
			(long long int) sector_offset, buffer, sectors);
		if (dir == WRITE) /* Write to the device */
		{
			bsdevice_write(start_sector + sector_offset, buffer, sectors);
		}
		else /* Read from the device */
		{
			bsdevice_read(start_sector + sector_offset, buffer, sectors);
		}
		sector_offset += sectors;
	}
	if (sector_offset != sector_cnt)
	{
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
	while ((req = blk_fetch_request(q)) != NULL)
	{
#if 0
		/*
		 * This function tells us whether we are looking at a filesystem request
		 * - one that moves block of data
		 */
		if (!blk_fs_request(req))
		{
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
static struct block_device_operations fbs_fops =
{
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
	if ((ret = bsdevice_init()) < 0)
	{
		return ret;
	}
	fbs_dev.size = ret;

	/* Get Registered */
	freebs_major = register_blkdev(freebs_major, "freebs");
	if (freebs_major <= 0)
	{
		printk(KERN_ERR "freebs: Unable to get Major Number\n");
		bsdevice_cleanup();
		return -EBUSY;
	}

	/* Get a request queue (here queue is created) */
	spin_lock_init(&fbs_dev.lock);
	fbs_dev.fbs_queue = blk_init_queue(fbs_request, &fbs_dev.lock);
	if (fbs_dev.fbs_queue == NULL)
	{
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
	if (!fbs_dev.fbs_disk)
	{
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
