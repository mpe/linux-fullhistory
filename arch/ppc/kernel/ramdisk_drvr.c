/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91
 *
 * Modifications by Fred N. van Kempen to allow for bootable root
 * disks (which are used in LINUX/Pro).  Also some cleanups.  03/03/93
 */


#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>

#define MAJOR_NR  MEM_MAJOR
#include <linux/blk.h>

#define RAMDISK_MINOR	1

char	*rd_start;
int	rd_length = 0;
static int rd_blocksizes[2] = {0, 0};

static void rd_request(void)
{
	int	len;
	char	*addr;

repeat:
	INIT_REQUEST;
	addr = rd_start + (CURRENT->sector << 9);
	len = CURRENT->current_nr_sectors << 9;

	if (CURRENT-> cmd == WRITE) {
		(void ) memcpy(addr,
			      CURRENT->buffer,
			      len);
	} else if (CURRENT->cmd == READ) {
		(void) memcpy(CURRENT->buffer, 
			      addr,
			      len);
	} else
		panic("RAMDISK: unknown RAM disk command !\n");
	end_request(1);
	goto repeat;
}

static struct file_operations rd_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	block_fsync		/* fsync */
};

void
rd_preloaded_init(char *start, int length)
{
	int	i;

	if (register_blkdev(MEM_MAJOR,"rd",&rd_fops)) {
		printk("RAMDISK: Unable to get major %d.\n", MEM_MAJOR);
		return 0;
	}
	blk_dev[MEM_MAJOR].request_fn = DEVICE_REQUEST;
	rd_start = start;
	rd_length = length;
	for(i=0;i<2;i++) rd_blocksizes[i] = 1024;
	blksize_size[MAJOR_NR] = rd_blocksizes;
	/* We loaded the file system image.  Prepare for mounting it. */
	ROOT_DEV = to_kdev_t((MEM_MAJOR << 8) | RAMDISK_MINOR);
}
