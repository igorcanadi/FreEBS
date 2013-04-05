#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/string.h>

#include "freebs_device.h"

#define FREEBS_DEVICE_SIZE 1024 /* sectors */
/* So, total device size = 1024 * 512 bytes = 512 KiB */

/* Array where the disk stores its data */
static u8 *dev_data;

int bsdevice_init(void)
{
	dev_data = vmalloc(FREEBS_DEVICE_SIZE * FREEBS_SECTOR_SIZE);
	if (dev_data == NULL)
		return -ENOMEM;
	return FREEBS_DEVICE_SIZE;
}

void bsdevice_cleanup(void)
{
	vfree(dev_data);
}

void bsdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	memcpy(dev_data + sector_off * FREEBS_SECTOR_SIZE, buffer,
		sectors * FREEBS_SECTOR_SIZE);
}
void bsdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	memcpy(buffer, dev_data + sector_off * FREEBS_SECTOR_SIZE,
		sectors * FREEBS_SECTOR_SIZE);
}
