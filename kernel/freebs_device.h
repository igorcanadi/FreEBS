#ifndef RAMDEVICE_H
#define RAMDEVICE_H

#define FREEBS_SECTOR_SIZE 512

extern int bsdevice_init(void);
extern void bsdevice_cleanup(void);
extern void bsdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors);
extern void bsdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors);
#endif
