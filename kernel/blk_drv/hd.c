/*
 *  linux/kernel/hd.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 * 
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *
 *  Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  in the early extended-partition checks and added DM partitions
 */

#include <errno.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>

#define REALLY_SLOW_IO
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR 3
#include "blk.h"

static inline unsigned char CMOS_READ(unsigned char addr)
{
	outb_p(0x80|addr,0x70);
	return inb_p(0x71);
}

#define	HD_DELAY	0

/* Max read/write errors/sector */
#define MAX_ERRORS	7
#define MAX_HD		2

static void recal_intr(void);
static void bad_rw_intr(void);

static int recalibrate = 0;
static int reset = 0;

#if (HD_DELAY > 0)
unsigned long last_req, read_timer();
#endif

/*
 *  This struct defines the HD's and their types.
 */
struct hd_i_struct {
	unsigned int head,sect,cyl,wpcom,lzone,ctl;
	};
#ifdef HD_TYPE
struct hd_i_struct hd_info[] = { HD_TYPE };
#define NR_HD ((sizeof (hd_info))/(sizeof (struct hd_i_struct)))
#else
struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;
#endif

static struct hd_struct {
	long start_sect;
	long nr_sects;
} hd[MAX_HD<<6]={{0,0},};

static int hd_sizes[MAX_HD<<6] = {0, };

#define port_read(port,buf,nr) \
__asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr):"cx","di")

#define port_write(port,buf,nr) \
__asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr):"cx","si")

extern void hd_interrupt(void);
extern void rd_load(void);

static unsigned int current_minor;

/*
 * Create devices for each logical partition in an extended partition.
 * The logical partitions form a linked list, with each entry being
 * a partition table with two entries.  The first entry
 * is the real data partition (with a start relative to the partition
 * table start).  The second is a pointer to the next logical partition
 * (with a start relative to the entire extended partition).
 * We do not create a Linux partition for the partition tables, but
 * only for the actual data partitions.
 */
static void extended_partition(unsigned int dev)
{
	struct buffer_head *bh;
	struct partition *p;
	unsigned long first_sector, this_sector;

	first_sector = hd[MINOR(dev)].start_sect;
	this_sector = first_sector;

	while (1) {
		if ((current_minor & 0x3f) >= 60)
			return;
		if (!(bh = bread(dev,0))) {
			printk("Unable to read partition table of device %04x\n",dev);
			return;
		}
	  /*
	   * This block is from a device that we're about to stomp on.
	   * So make sure nobody thinks this block is usable.
	   */
		bh->b_dirt=0;
		bh->b_uptodate=0;
		if (*(unsigned short *) (bh->b_data+510) == 0xAA55) {
			p = 0x1BE + (void *)bh->b_data;
		/*
		 * Process the first entry, which should be the real
		 * data partition.
		 */
			if (p->sys_ind == EXTENDED_PARTITION ||
			    !(hd[current_minor].nr_sects = p->nr_sects))
				goto done;  /* shouldn't happen */
			hd[current_minor].start_sect = this_sector + p->start_sect;
			printk("  Logical part %d start %d size %d end %d\n\r", 
			       current_minor, hd[current_minor].start_sect, 
			       hd[current_minor].nr_sects,
			       hd[current_minor].start_sect + 
			       hd[current_minor].nr_sects - 1);
			current_minor++;
			p++;
		/*
		 * Process the second entry, which should be a link
		 * to the next logical partition.  Create a minor
		 * for this just long enough to get the next partition
		 * table.  The minor will be reused for the real
		 * data partition.
		 */
			if (p->sys_ind != EXTENDED_PARTITION ||
			    !(hd[current_minor].nr_sects = p->nr_sects))
				goto done;  /* no more logicals in this partition */
			hd[current_minor].start_sect = first_sector + p->start_sect;
			this_sector = first_sector + p->start_sect;
			dev = 0x0300 | current_minor;
			brelse(bh);
		} else
			goto done;
	}
done:
	brelse(bh);
}

static void check_partition(unsigned int dev)
{
	int i, minor = current_minor;
	struct buffer_head *bh;
	struct partition *p;
	unsigned long first_sector;

	first_sector = hd[MINOR(dev)].start_sect;
	if (!(bh = bread(dev,0))) {
		printk("Unable to read partition table of device %04x\n",dev);
		return;
	}
	printk("Drive %d:\n\r",minor >> 6);
	current_minor += 4;  /* first "extra" minor */
	if (*(unsigned short *) (bh->b_data+510) == 0xAA55) {
		p = 0x1BE + (void *)bh->b_data;
		for (i=1 ; i<=4 ; minor++,i++,p++) {
			if (!(hd[minor].nr_sects = p->nr_sects))
				continue;
			hd[minor].start_sect = first_sector + p->start_sect;
			printk(" part %d start %d size %d end %d \n\r", i, 
			       hd[minor].start_sect, hd[minor].nr_sects, 
			       hd[minor].start_sect + hd[minor].nr_sects - 1);
			if ((current_minor & 0x3f) >= 60)
				continue;
			if (p->sys_ind == EXTENDED_PARTITION) {
				extended_partition(0x0300 | minor);
			}
		}
		/*
		 * check for Disk Manager partition table
		 */
		if (*(unsigned short *) (bh->b_data+0xfc) == 0x55AA) {
			p = 0x1BE + (void *)bh->b_data;
			for (i = 4 ; i < 16 ; i++, current_minor++) {
				p--;
				if ((current_minor & 0x3f) >= 60)
					break;
				if (!(p->start_sect && p->nr_sects))
					continue;
				hd[current_minor].start_sect = p->start_sect;
				hd[current_minor].nr_sects = p->nr_sects;
				printk(" DM part %d start %d size %d end %d\n\r",
				       current_minor,
				       hd[current_minor].start_sect, 
				       hd[current_minor].nr_sects,
				       hd[current_minor].start_sect + 
				       hd[current_minor].nr_sects - 1);
			}
		}
	} else
		printk("Bad partition table on dev %04x\n",dev);
	brelse(bh);
}

/* This may be used only once, enforced by 'static int callable' */
int sys_setup(void * BIOS)
{
	static int callable = 1;
	int i,drive;
	unsigned char cmos_disks;

	if (!callable)
		return -1;
	callable = 0;
#ifndef HD_TYPE
	for (drive=0 ; drive<2 ; drive++) {
		hd_info[drive].cyl = *(unsigned short *) BIOS;
		hd_info[drive].head = *(unsigned char *) (2+BIOS);
		hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
		hd_info[drive].ctl = *(unsigned char *) (8+BIOS);
		hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
		hd_info[drive].sect = *(unsigned char *) (14+BIOS);
		BIOS += 16;
	}

	/*
		We querry CMOS about hard disks : it could be that 
		we have a SCSI/ESDI/etc controller that is BIOS
		compatable with ST-506, and thus showing up in our
		BIOS table, but not register compatable, and therefore
		not present in CMOS.

		Furthurmore, we will assume that our ST-506 drives
		<if any> are the primary drives in the system, and 
		the ones reflected as drive 1 or 2.

		The first drive is stored in the high nibble of CMOS
		byte 0x12, the second in the low nibble.  This will be
		either a 4 bit drive type or 0xf indicating use byte 0x19 
		for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

		Needless to say, a non-zero value means we have 
		an AT controller hard disk for that drive.

		
	*/

	if ((cmos_disks = CMOS_READ(0x12)) & 0xf0)
		if (cmos_disks & 0x0f)
			NR_HD = 2;
		else
			NR_HD = 1;
	else
		NR_HD = 0;
#endif
	for (i = 0 ; i < (MAX_HD<<6) ; i++) {
		hd[i].start_sect = 0;
		hd[i].nr_sects = 0;
	}
	for (i = 0 ; i < NR_HD ; i++)
		hd[i<<6].nr_sects = hd_info[i].head*
				hd_info[i].sect*hd_info[i].cyl;
	for (drive=0 ; drive<NR_HD ; drive++) {
		current_minor = 1+(drive<<6);
		check_partition(0x0300+(drive<<6));
	}
	for (i=0 ; i<(MAX_HD<<6) ; i++)
		hd_sizes[i] = hd[i].nr_sects>>1 ;
	blk_size[MAJOR_NR] = hd_sizes;
	if (NR_HD)
		printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":"");
	rd_load();
	mount_root();
	return (0);
}

#if (HD_DELAY > 0)
unsigned long read_timer(void)
{
	unsigned long t;
	int i;

	cli();
    	outb_p(0xc2, 0x43);
	t = jiffies * 11931 + (inb_p(0x40) & 0x80 ? 5966 : 11932);
	i = inb_p(0x40);
	i |= inb(0x40) << 8;
	sti();
	return(t - i / 2);
}
#endif

static int controller_ready(void)
{
	int retries = 100000;

	while (--retries && (inb_p(HD_STATUS)&0x80))
		/* nothing */;
	if (!retries)
		printk("controller_ready: status = %02x\n\r",
			(unsigned char) inb_p(HD_STATUS));
	return (retries);
}

static int win_result(void)
{
	int i=inb_p(HD_STATUS);

	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
		== (READY_STAT | SEEK_STAT))
		return(0); /* ok */
	if (i&1)
		i=inb(HD_ERROR);
	return (1);
}

static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
		unsigned int head,unsigned int cyl,unsigned int cmd,
		void (*intr_addr)(void))
{
	unsigned short port;

	if (drive>1 || head>15)
		panic("Trying to write bad sector");
#if (HD_DELAY > 0)
	while (read_timer() - last_req < HD_DELAY)
		/* nothing */;
#endif
	if (reset || !controller_ready()) {
		reset = 1;
		return;
	}
	SET_INTR(intr_addr);
	outb_p(hd_info[drive].ctl,HD_CMD);
	port=HD_DATA;
	outb_p(hd_info[drive].wpcom>>2,++port);
	outb_p(nsect,++port);
	outb_p(sect,++port);
	outb_p(cyl,++port);
	outb_p(cyl>>8,++port);
	outb_p(0xA0|(drive<<4)|head,++port);
	outb_p(cmd,++port);
}

static int drive_busy(void)
{
	unsigned int i;
	unsigned char c;

	for (i = 0; i < 500000 ; i++) {
		c = inb_p(HD_STATUS);
		c &= (BUSY_STAT | READY_STAT | SEEK_STAT);
		if (c == (READY_STAT | SEEK_STAT))
			return 0;
	}
	printk("HD controller times out, c=%02x\n\r",c);
	return(1);
}

static void reset_controller(void)
{
	int	i;

	printk("HD-controller reset\r\n");
	outb(4,HD_CMD);
	for(i = 0; i < 1000; i++) nop();
	outb(hd_info[0].ctl & 0x0f ,HD_CMD);
	if (drive_busy())
		printk("HD-controller still busy\n\r");
	if ((i = inb(HD_ERROR)) != 1)
		printk("HD-controller reset failed: %02x\n\r",i);
}

static void reset_hd(void)
{
	static int i;

repeat:
	if (reset) {
		reset = 0;
		i = -1;
		reset_controller();
	} else if (win_result()) {
		bad_rw_intr();
		if (reset)
			goto repeat;
	}
	i++;
	if (i < NR_HD) {
		hd_out(i,hd_info[i].sect,hd_info[i].sect,hd_info[i].head-1,
			hd_info[i].cyl,WIN_SPECIFY,&reset_hd);
		if (reset)
			goto repeat;
	} else
		do_hd_request();
}

/*
 * Ok, don't know what to do with the unexpected interrupts: on some machines
 * doing a reset and a retry seems to result in an eternal loop. Right now I
 * ignore it, and just set the timeout.
 */
void unexpected_hd_interrupt(void)
{
	printk("Unexpected HD interrupt\n\r");
	SET_TIMER;
#if 0
	reset = 1;
	do_hd_request();
#endif
}

static void bad_rw_intr(void)
{
	if (!CURRENT)
		return;
	if (++CURRENT->errors >= MAX_ERRORS)
		end_request(0);
	else if (CURRENT->errors > MAX_ERRORS/2)
		reset = 1;
	else
		recalibrate = 1;
}

#define STAT_MASK (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT)
#define STAT_OK (READY_STAT | SEEK_STAT)

static void read_intr(void)
{
	int i;

	i = (unsigned) inb_p(HD_STATUS);
	if (!(i & DRQ_STAT))
		goto bad_read;
	if ((i & STAT_MASK) != STAT_OK)
		goto bad_read;
	port_read(HD_DATA,CURRENT->buffer,256);
	i = (unsigned) inb_p(HD_STATUS);
	if (!(i & BUSY_STAT))
		if ((i & STAT_MASK) != STAT_OK)
			goto bad_read;
	CURRENT->errors = 0;
	CURRENT->buffer += 512;
	CURRENT->sector++;
	i = --CURRENT->nr_sectors;
	if (!i || (CURRENT->bh && !(i&1)))
		end_request(1);
	if (i > 0) {
		SET_INTR(&read_intr);
		return;
	}
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	do_hd_request();
	return;
bad_read:
	if (i & ERR_STAT)
		i = (unsigned) inb(HD_ERROR);
	bad_rw_intr();
	do_hd_request();
	return;
}

static void write_intr(void)
{
	int i;

	i = (unsigned) inb_p(HD_STATUS);
	if ((i & STAT_MASK) != STAT_OK)
		goto bad_write;
	if (CURRENT->nr_sectors > 1 && !(i & DRQ_STAT))
		goto bad_write;
	CURRENT->sector++;
	i = --CURRENT->nr_sectors;
	CURRENT->buffer += 512;
	if (!i || (CURRENT->bh && !(i & 1)))
		end_request(1);
	if (i > 0) {
		SET_INTR(&write_intr);
		port_write(HD_DATA,CURRENT->buffer,256);
	} else {
#if (HD_DELAY > 0)
		last_req = read_timer();
#endif
		do_hd_request();
	}
	return;
bad_write:
	if (i & ERR_STAT)
		i = (unsigned) inb(HD_ERROR);
	bad_rw_intr();
	do_hd_request();
	return;
}

static void recal_intr(void)
{
	if (win_result())
		bad_rw_intr();
	do_hd_request();
}

/*
 * This is another of the error-routines I don't know what to do with. The
 * best idea seems to just set reset, and start all over again.
 */
static void hd_times_out(void)
{	
	do_hd = NULL;
	reset = 1;
	if (!CURRENT)
		return;
	printk("HD timeout\n\r");
	cli();
	if (++CURRENT->errors >= MAX_ERRORS)
		end_request(0);
	do_hd_request();
}

static void do_hd_request(void)
{
	int i,r;
	unsigned int block,dev;
	unsigned int sec,head,cyl;
	unsigned int nsect;

	INIT_REQUEST;
	dev = MINOR(CURRENT->dev);
	block = CURRENT->sector;
	nsect = CURRENT->nr_sectors;
	if (dev >= (NR_HD<<6) || block >= hd[dev].nr_sects) {
		end_request(0);
		goto repeat;
	}
	block += hd[dev].start_sect;
	dev >>= 6;
	sec = block % hd_info[dev].sect;
	block /= hd_info[dev].sect;
	head = block % hd_info[dev].head;
	cyl = block / hd_info[dev].head;
	sec++;
	if (reset) {
		recalibrate = 1;
		reset_hd();
		return;
	}
	if (recalibrate) {
		recalibrate = 0;
		hd_out(dev,hd_info[dev].sect,0,0,0,WIN_RESTORE,&recal_intr);
		if (reset)
			goto repeat;
		return;
	}	
	if (CURRENT->cmd == WRITE) {
		hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
		if (reset)
			goto repeat;
		for(i=0 ; i<10000 && !(r=inb_p(HD_STATUS)&DRQ_STAT) ; i++)
			/* nothing */ ;
		if (!r) {
			bad_rw_intr();
			goto repeat;
		}
		port_write(HD_DATA,CURRENT->buffer,256);
	} else if (CURRENT->cmd == READ) {
		hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
		if (reset)
			goto repeat;
	} else
		panic("unknown hd-command");
}

static int hd_ioctl(struct inode * inode, struct file * file,
	unsigned int cmd, unsigned int arg)
{
	struct hd_geometry *loc = (void *) arg;
	int dev;

	if (!loc || !inode)
		return -EINVAL;
	dev = MINOR(inode->i_rdev) >> 6;
	if (dev >= NR_HD)
		return -EINVAL;
	switch (cmd) {
		case HDIO_REQ:
			verify_area(loc, sizeof(*loc));
			put_fs_byte(hd_info[dev].head,
				(char *) &loc->heads);
			put_fs_byte(hd_info[dev].sect,
				(char *) &loc->sectors);
			put_fs_word(hd_info[dev].cyl,
				(short *) &loc->cylinders);
			return 0;
		default:
			return -EINVAL;
	}
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */
static void hd_release(struct inode * inode, struct file * file)
{
	sync_dev(inode->i_rdev);
}

static struct file_operations hd_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	hd_ioctl,		/* ioctl */
	NULL,			/* no special open code */
	hd_release		/* release */
};

void hd_init(void)
{
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	blkdev_fops[MAJOR_NR] = &hd_fops;
	set_intr_gate(0x2E,&hd_interrupt);
	outb_p(inb_p(0x21)&0xfb,0x21);
	outb(inb_p(0xA1)&0xbf,0xA1);
	timer_table[HD_TIMER].fn = hd_times_out;
}
