/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91
 */


#include <linux/config.h>
#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/segment.h>

#define MAJOR_NR 1
#include "blk.h"

char	*rd_start;
int	rd_length = 0;

static void do_rd_request(void)
{
	int	len;
	char	*addr;

repeat:
	INIT_REQUEST;
	addr = rd_start + (CURRENT->sector << 9);
	len = CURRENT->nr_sectors << 9;
	if ((MINOR(CURRENT->dev) != 1) || (addr+len > rd_start+rd_length)) {
		end_request(0);
		goto repeat;
	}
	if (CURRENT-> cmd == WRITE) {
		(void ) memcpy(addr,
			      CURRENT->buffer,
			      len);
	} else if (CURRENT->cmd == READ) {
		(void) memcpy(CURRENT->buffer, 
			      addr,
			      len);
	} else
		panic("unknown ramdisk-command");
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
	NULL			/* no special release code */
};

/*
 * Returns amount of memory which needs to be reserved.
 */
long rd_init(long mem_start, int length)
{
	int	i;
	char	*cp;

	if (register_blkdev(MAJOR_NR,"rd",&rd_fops)) {
		printk("Unable to get major %d for ramdisk\n",MAJOR_NR);
		return 0;
	}
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	rd_start = (char *) mem_start;
	rd_length = length;
	cp = rd_start;
	for (i=0; i < length; i++)
		*cp++ = '\0';
	return(length);
}

/*
 * If the root device is the ram disk, try to load it.
 * In order to do this, the root device is originally set to the
 * floppy, and we later change it to be ram disk.
 */
void rd_load(void)
{
	struct buffer_head *bh;
	struct minix_super_block s;
	int		block = 512;	/* Start at block 512 */
	int		i = 1;
	int		nblocks;
	char		*cp;		/* Move pointer */
	
	if (!rd_length)
		return;
	printk("Ram disk: %d bytes, starting at 0x%x\n", rd_length,
		(int) rd_start);
	if (MAJOR(ROOT_DEV) != 2)
		return;
	bh = breada(ROOT_DEV,block+1,block,block+2,-1);
	if (!bh) {
		printk("Disk error while looking for ramdisk!\n");
		return;
	}
	*((struct minix_super_block *) &s) = *((struct minix_super_block *) bh->b_data);
	brelse(bh);
	if (s.s_magic != MINIX_SUPER_MAGIC)
		/* No ram disk image present, assume normal floppy boot */
		return;
	nblocks = s.s_nzones << s.s_log_zone_size;
	if (nblocks > (rd_length >> BLOCK_SIZE_BITS)) {
		printk("Ram disk image too big!  (%d blocks, %d avail)\n", 
			nblocks, rd_length >> BLOCK_SIZE_BITS);
		return;
	}
	printk("Loading %d bytes into ram disk\n",
		nblocks << BLOCK_SIZE_BITS);
	cp = rd_start;
	while (nblocks) {
		if (nblocks > 2) 
			bh = breada(ROOT_DEV, block, block+1, block+2, -1);
		else
			bh = bread(ROOT_DEV, block, BLOCK_SIZE);
		if (!bh) {
			printk("I/O error on block %d, aborting load\n", 
				block);
			return;
		}
		(void) memcpy(cp, bh->b_data, BLOCK_SIZE);
		brelse(bh);
		if (!(nblocks-- & 15))
			printk(".");
		cp += BLOCK_SIZE;
		block++;
		i++;
	}
	printk("\ndone\n");
	ROOT_DEV=0x0101;
}
