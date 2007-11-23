/*
 *  linux/kernel/hd.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
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

#include <linux/config.h>
#ifdef CONFIG_BLK_DEV_HD

#define HD_IRQ 14

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>

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

static struct hd_struct hd[MAX_HD<<6]={{0,0},};
static int hd_sizes[MAX_HD<<6] = {0, };

#define port_read(port,buf,nr) \
__asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr):"cx","di")

#define port_write(port,buf,nr) \
__asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr):"cx","si")

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
		return 0; /* ok */
	printk("HD: win_result: status = 0x%02x\n",i);
	if (i&1) {
		i=inb(HD_ERROR);
		printk("HD: win_result: error = 0x%02x\n",i);
	}	
	return 1;
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
	printk("HD controller times out, status = 0x%02x\n\r",c);
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
	sti();
	printk("Unexpected HD interrupt\n\r");
	SET_TIMER;
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

static inline int wait_DRQ(void)
{
	int retries = 100000;

	while (--retries > 0)
		if (inb_p(HD_STATUS) & DRQ_STAT)
			return 0;
	return -1;
}

#define STAT_MASK (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT)
#define STAT_OK (READY_STAT | SEEK_STAT)

static void read_intr(void)
{
	int i;

	i = (unsigned) inb_p(HD_STATUS);
	if ((i & STAT_MASK) != STAT_OK) {
		printk("HD: read_intr: status = 0x%02x\n",i);
		goto bad_read;
	}
	if (wait_DRQ()) {
		printk("HD: read_intr: no DRQ\n");
		goto bad_read;
	}
	port_read(HD_DATA,CURRENT->buffer,256);
	i = (unsigned) inb_p(HD_STATUS);
	if (!(i & BUSY_STAT))
		if ((i & STAT_MASK) != STAT_OK) {
			printk("HD: read_intr: second status = 0x%02x\n",i);
			goto bad_read;
		}
	CURRENT->errors = 0;
	CURRENT->buffer += 512;
	CURRENT->sector++;
	i = --CURRENT->nr_sectors;
	--CURRENT->current_nr_sectors;
#ifdef DEBUG
	printk("hd%d : sector = %d, %d remaining to buffer = %08x\n",
		MINOR(CURRENT->dev), CURRENT->sector, i, CURRENT-> 
		buffer);
#endif
	if (!i || (CURRENT->bh && !SUBSECTOR(i)))
		end_request(1);
	if (i > 0) {
		SET_INTR(&read_intr);
		sti();
		return;
	}
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	do_hd_request();
	return;
bad_read:
	if (i & ERR_STAT) {
		i = (unsigned) inb(HD_ERROR);
		printk("HD: read_intr: error = 0x%02x\n",i);
	}
	bad_rw_intr();
	do_hd_request();
	return;
}

static void write_intr(void)
{
	int i;

	i = (unsigned) inb_p(HD_STATUS);
	if ((i & STAT_MASK) != STAT_OK) {
		printk("HD: write_intr: status = 0x%02x\n",i);
		goto bad_write;
	}
	if (CURRENT->nr_sectors > 1 && wait_DRQ()) {
		printk("HD: write_intr: no DRQ\n");
		goto bad_write;
	}
	CURRENT->sector++;
	i = --CURRENT->nr_sectors;
	--CURRENT->current_nr_sectors;
	CURRENT->buffer += 512;
	if (!i || (CURRENT->bh && !SUBSECTOR(i)))
		end_request(1);
	if (i > 0) {
		SET_INTR(&write_intr);
		port_write(HD_DATA,CURRENT->buffer,256);
		sti();
	} else {
#if (HD_DELAY > 0)
		last_req = read_timer();
#endif
		do_hd_request();
	}
	return;
bad_write:
	sti();
	if (i & ERR_STAT) {
		i = (unsigned) inb(HD_ERROR);
		printk("HD: write_intr: error = 0x%02x\n",i);
	}
	bad_rw_intr();
	cli();
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
	sti();
	DEVICE_INTR = NULL;
	reset = 1;
	if (!CURRENT)
		return;
	printk("HD timeout\n\r");
	cli();
	if (++CURRENT->errors >= MAX_ERRORS) {
#ifdef DEBUG
		printk("hd : too many errors.\n");
#endif
		end_request(0);
	}

	do_hd_request();
}

/*
 * The driver has been modified to enable interrupts a bit more: in order to
 * do this we first (a) disable the timeout-interrupt and (b) clear the
 * device-interrupt. This way the interrupts won't mess with out code (the
 * worst that can happen is that an unexpected HD-interrupt comes in and
 * sets the "reset" variable and starts the timer)
 */
static void do_hd_request(void)
{
	unsigned int block,dev;
	unsigned int sec,head,cyl;
	unsigned int nsect;

repeat:
	DEVICE_INTR = NULL;
	timer_active &= ~(1<<HD_TIMER);
	sti();
	INIT_REQUEST;
	dev = MINOR(CURRENT->dev);
	block = CURRENT->sector;
	nsect = CURRENT->nr_sectors;
	if (dev >= (NR_HD<<6) || block >= hd[dev].nr_sects) {
#ifdef DEBUG
		printk("hd%d : attempted read for sector %d past end of device at sector %d.\n",
		   	block, hd[dev].nr_sects);
#endif
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
#ifdef DEBUG
	printk("hd%d : cyl = %d, head = %d, sector = %d, buffer = %08x\n",
		dev, cyl, head, sec, CURRENT->buffer);
#endif
	cli();
	if (reset) {
		recalibrate = 1;
		reset_hd();
		sti();
		return;
	}
	if (recalibrate) {
		recalibrate = 0;
		hd_out(dev,hd_info[dev].sect,0,0,0,WIN_RESTORE,&recal_intr);
		if (reset)
			goto repeat;
		sti();
		return;
	}	
	if (CURRENT->cmd == WRITE) {
		hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
		if (reset)
			goto repeat;
		if (wait_DRQ()) {
			printk("HD: do_hd_request: no DRQ\n");
			bad_rw_intr();
			goto repeat;
		}
		port_write(HD_DATA,CURRENT->buffer,256);
		sti();
	} else if (CURRENT->cmd == READ) {
		hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
		if (reset)
			goto repeat;
		sti();
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
			put_fs_long(hd[MINOR(inode->i_rdev)].start_sect,
				(long *) &loc->start);
			return 0;
		RO_IOCTLS(inode->i_rdev,arg);
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


static void hd_geninit();

static struct gendisk hd_gendisk = {
	MAJOR_NR,	/* Major number */	
	"hd",		/* Major name */
	6,		/* Bits to shift to get real from partition */
	1 << 6,		/* Number of partitions per real */
	MAX_HD,		/* maximum number of real */
	hd_geninit,	/* init function */
	hd,		/* hd struct */
	hd_sizes,	/* block sizes */
	0,		/* number */
	(void *) hd_info,	/* internal */
	NULL		/* next */
};
	
static void hd_geninit(void)
{
	int drive;
#ifndef HD_TYPE
	extern struct drive_info drive_info;
	void *BIOS = (void *) &drive_info;
	int cmos_disks, i;
	   
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

	for (i = 0 ; i < NR_HD ; i++)
		hd[i<<6].nr_sects = hd_info[i].head*
				hd_info[i].sect*hd_info[i].cyl;

	hd_gendisk.nr_real = NR_HD;
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

static void hd_interrupt(int unused)
{
	void (*handler)(void) = DEVICE_INTR;

	DEVICE_INTR = NULL;
	timer_active &= ~(1<<HD_TIMER);
	if (!handler)
		handler = unexpected_hd_interrupt;
	handler();
	sti();
}

/*
 * This is the harddisk IRQ descruption. The SA_INTERRUPT in sa_flags
 * means we run the IRQ-handler with interrupts disabled: this is bad for
 * interrupt latency, but anything else has led to problems on some
 * machines...
 *
 * We enable interrupts in some of the routines after making sure it's
 * safe.
 */
static struct sigaction hd_sigaction = {
	hd_interrupt,
	0,
	SA_INTERRUPT,
	NULL
};

void hd_init(void)
{
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	blkdev_fops[MAJOR_NR] = &hd_fops;
	hd_gendisk.next = gendisk_head;
	gendisk_head = &hd_gendisk;
	if (irqaction(HD_IRQ,&hd_sigaction))
		printk("Unable to get IRQ%d for the harddisk driver\n",HD_IRQ);
	timer_table[HD_TIMER].fn = hd_times_out;
}

#endif
