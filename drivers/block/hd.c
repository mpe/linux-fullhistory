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
 *
 *  IDE IRQ-unmask & drive-id & multiple-mode code added by Mark Lord.
 *
 *  Support for BIOS drive geometry translation added by Mark Lord.
 *   -- hd.c no longer chokes on drives with "more than 16 heads".
 */

#define DEFAULT_MULT_COUNT  0	/* set to 0 to disable multiple mode at boot */
#define DEFAULT_UNMASK_INTR 0	/* set to 0 to *NOT* unmask irq's more often */

#include <asm/irq.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/config.h>
#include <linux/malloc.h>

#define REALLY_SLOW_IO
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR HD_MAJOR
#include "blk.h"

#define HD_IRQ 14

static int revalidate_hddisk(int, int);

static inline unsigned char CMOS_READ(unsigned char addr)
{
	outb_p(addr,0x70);
	return inb_p(0x71);
}

#define	HD_DELAY	0

#define MAX_ERRORS     16	/* Max read/write errors/sector */
#define RESET_FREQ      8	/* Reset controller every 8th retry */
#define RECAL_FREQ      4	/* Recalibrate every 4th retry */
#define MAX_HD		2

static void recal_intr(void);
static void bad_rw_intr(void);

static char recalibrate[MAX_HD] = { 0, };
static char special_op[MAX_HD] = { 0, };
static int access_count[MAX_HD] = {0, };
static char busy[MAX_HD] = {0, };
static struct wait_queue * busy_wait = NULL;

static int reset = 0;
static int hd_error = 0;

/*
 *  This struct defines the HD's and their types.
 */
struct hd_i_struct {
	unsigned int head,sect,cyl,wpcom,lzone,ctl;
	};
static struct hd_driveid *hd_ident_info[MAX_HD];
	
#ifdef HD_TYPE
static struct hd_i_struct hd_info[] = { HD_TYPE };
struct hd_i_struct bios_info[] = { HD_TYPE };
static int NR_HD = ((sizeof (hd_info))/(sizeof (struct hd_i_struct)));
#else
static struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
struct hd_i_struct bios_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;
#endif

static struct hd_struct hd[MAX_HD<<6]={{0,0},};
static int hd_sizes[MAX_HD<<6] = {0, };
static int hd_blocksizes[MAX_HD<<6] = {0, };

#if (HD_DELAY > 0)
unsigned long last_req;

unsigned long read_timer(void)
{
	unsigned long t, flags;
	int i;

	save_flags(flags);
	cli();
	t = jiffies * 11932;
    	outb_p(0, 0x43);
	i = inb_p(0x40);
	i |= inb(0x40) << 8;
	restore_flags(flags);
	return(t - i);
}
#endif

void hd_setup(char *str, int *ints)
{
	int hdind = 0;

	if (ints[0] != 3)
		return;
	if (bios_info[0].head != 0)
		hdind=1;
	bios_info[hdind].head  = hd_info[hdind].head = ints[2];
	bios_info[hdind].sect  = hd_info[hdind].sect = ints[3];
	bios_info[hdind].cyl   = hd_info[hdind].cyl = ints[1];
	bios_info[hdind].wpcom = hd_info[hdind].wpcom = 0;
	bios_info[hdind].lzone = hd_info[hdind].lzone = ints[1];
	bios_info[hdind].ctl   = hd_info[hdind].ctl = (ints[2] > 8 ? 8 : 0);
	NR_HD = hdind+1;
}

static int win_result(void)
{
	int i=inb_p(HD_STATUS);

	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
		== (READY_STAT | SEEK_STAT)) {
	        hd_error = 0;
		return 0; /* ok */
	}
	printk("HD: win_result: status = 0x%02x\n",i);
	if (i&1) {
		hd_error = inb(HD_ERROR);
		printk("HD: win_result: error = 0x%02x\n",hd_error);
	}	
	return 1;
}

static int controller_busy(void);
static int status_ok(void);

static int controller_ready(unsigned int drive, unsigned int head)
{
	int retry = 100;

	do {
		if (controller_busy() & BUSY_STAT)
			return 0;
		outb_p(0xA0 | (drive<<4) | head, HD_CURRENT);
		if (status_ok())
			return 1;
	} while (--retry);
	return 0;
}

static int status_ok(void)
{
	unsigned char status = inb_p(HD_STATUS);

	if (status & BUSY_STAT)
		return 1;
	if (status & WRERR_STAT)
		return 0;
	if (!(status & READY_STAT))
		return 0;
	if (!(status & SEEK_STAT))
		return 0;
	return 1;
}

static int controller_busy(void)
{
	int retries = 100000;
	unsigned char status;

	do {
		status = inb_p(HD_STATUS);
	} while ((status & BUSY_STAT) && --retries);
	return status;
}

static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
		unsigned int head,unsigned int cyl,unsigned int cmd,
		void (*intr_addr)(void))
{
	unsigned short port;

#ifdef DEBUG
	if (drive>1 || head>15) {
		printk("bad drive mapping, trying to access drive=%d, cyl=%d, head=%d, sect=%d\n",
			drive, cyl, head, sect);
		panic("harddisk driver problem");
	}
#endif
#if (HD_DELAY > 0)
	while (read_timer() - last_req < HD_DELAY)
		/* nothing */;
#endif
	if (reset)
		return;
	if (!controller_ready(drive, head)) {
		special_op[drive] += reset = 1;
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

static void hd_request (void);
static unsigned int identified  [MAX_HD] = {0,}; /* 1 = drive ID already displayed   */
static unsigned int unmask_intr [MAX_HD] = {0,}; /* 1 = unmask IRQs during I/O       */
static unsigned int max_mult    [MAX_HD] = {0,}; /* max sectors for MultMode         */
static unsigned int mult_req    [MAX_HD] = {0,}; /* requested MultMode count         */
static unsigned int mult_count  [MAX_HD] = {0,}; /* currently enabled MultMode count */
static struct request WCURRENT;

static void rawstring (char *prefix, unsigned char *s, int n)
{
	if (prefix)
		printk(prefix);
	if (s && *s) {
		int i;
		for (i=0; i < n && s[i^1] == ' '; ++i); /* skip blanks */
		for (; i < n && s[i^1]; ++i)		/* flip bytes */
			if (s[i^1] != ' ' || ((i+1) < n && s[(i+1)^1] != ' '))
				printk("%c",s[i^1]);
	}
}

static void identify_intr(void)
{
	unsigned int dev = DEVICE_NR(CURRENT->dev);
	unsigned short stat = inb_p(HD_STATUS);
	struct hd_driveid id;

	if (unmask_intr[dev])
		sti();
	if (stat & (BUSY_STAT|ERR_STAT))
		printk ("  hd%c: identity unknown\n", dev+'a');
	else {
		insw(HD_DATA, (char *)&id, sizeof(id)/2); /* get ID bytes */
		max_mult[dev] = id.max_multsect;
		if ((id.cur_valid&1) && id.cur_cyls && id.cur_heads && (id.cur_heads <= 16) && id.cur_sectors) {
			/*
			 * Extract the physical drive geometry for our use.
			 * Note that we purposely do *not* update the bios_info.
			 * This way, programs that use it (like fdisk) will 
			 * still have the same logical view as the BIOS does,
			 * which keeps the partition table from being screwed.
			 */
			hd_info[dev].cyl  = id.cur_cyls;
			hd_info[dev].head = id.cur_heads;
			hd_info[dev].sect = id.cur_sectors; 
		}
		printk ("  hd%c: ", dev+'a');
		rawstring(NULL, id.model, sizeof(id.model));
		printk (", %dMB w/%dKB Cache, CHS=%d/%d/%d, MaxMult=%d\n",
			id.cyls*id.heads*id.sectors/2048, id.buf_size/2,
			hd_info[dev].cyl, hd_info[dev].head, hd_info[dev].sect, id.max_multsect);
		/* save drive info for later query via HDIO_GETIDENTITY */
		if (NULL != (hd_ident_info[dev] = (struct hd_driveid *)kmalloc(sizeof(id),GFP_ATOMIC)))
			*hd_ident_info[dev] = id;
		
		/* flush remaining 384 (reserved/undefined) ID bytes: */
		insw(HD_DATA,(char *)&id,sizeof(id)/2);
		insw(HD_DATA,(char *)&id,sizeof(id)/2);
		insw(HD_DATA,(char *)&id,sizeof(id)/2);
	}
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	hd_request();
	return;
}

static void set_multmode_intr(void)
{
	unsigned int dev = DEVICE_NR(CURRENT->dev), stat = inb_p(HD_STATUS);

	if (unmask_intr[dev])
		sti();
	if (stat & (BUSY_STAT|ERR_STAT)) {
		mult_req[dev] = mult_count[dev] = 0;
		printk ("  hd%c: set multiple mode failed\n", dev+'a');
	} else {
		if ((mult_count[dev] = mult_req[dev]))
			printk ("  hd%c: enabled %d-sector multiple mode\n",
				dev+'a', mult_count[dev]);
		else
			printk ("  hd%c: disabled multiple mode\n", dev+'a');
	}
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	hd_request();
	return;
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
	printk("HD controller times out, status = 0x%02x\n",c);
	return 1;
}

static void reset_controller(void)
{
	int	i;

	printk(KERN_DEBUG "HD-controller reset\n");
	outb_p(4,HD_CMD);
	for(i = 0; i < 1000; i++) nop();
	outb(hd_info[0].ctl & 0x0f ,HD_CMD);
	if (drive_busy())
		printk("HD-controller still busy\n");
	if ((hd_error = inb(HD_ERROR)) != 1)
		printk("HD-controller reset failed: %02x\n",hd_error);
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
	if (++i < NR_HD) {
		if (unmask_intr[i]) {
			printk("hd%c: disabled irq-unmasking\n",i+'a');
			unmask_intr[i] = 0;
		}
		if (mult_req[i] || mult_count[i]) {
			printk("hd%c: disabled multiple mode\n",i+'a');
			mult_req[i] = mult_count[i] = 0;
		}
		hd_out(i,hd_info[i].sect,hd_info[i].sect,hd_info[i].head-1,
			hd_info[i].cyl,WIN_SPECIFY,&reset_hd);
		if (reset)
			goto repeat;
	} else
		hd_request();
}

/*
 * Ok, don't know what to do with the unexpected interrupts: on some machines
 * doing a reset and a retry seems to result in an eternal loop. Right now I
 * ignore it, and just set the timeout.
 */
void unexpected_hd_interrupt(void)
{
	sti();
	printk(KERN_DEBUG "Unexpected HD interrupt\n");
	SET_TIMER;
}

/*
 * bad_rw_intr() now tries to be a bit smarter and does things
 * according to the error returned by the controller.
 * -Mika Liljeberg (liljeber@cs.Helsinki.FI)
 */
static void bad_rw_intr(void)
{
	int dev;

	if (!CURRENT)
		return;
	dev = MINOR(CURRENT->dev) >> 6;
	if (++CURRENT->errors >= MAX_ERRORS || (hd_error & BBD_ERR)) {
		end_request(0);
		special_op[dev] += recalibrate[dev] = 1;
	} else if (CURRENT->errors % RESET_FREQ == 0)
		special_op[dev] += reset = 1;
	else if ((hd_error & TRK0_ERR) || CURRENT->errors % RECAL_FREQ == 0)
		special_op[dev] += recalibrate[dev] = 1;
	/* Otherwise just retry */
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
	unsigned int dev = DEVICE_NR(CURRENT->dev);
	int i, retries = 100000, msect = mult_count[dev], nsect;

	if (unmask_intr[dev])
		sti();			/* permit other IRQs during xfer */
	do {
		i = (unsigned) inb_p(HD_STATUS);
		if (i & BUSY_STAT)
			continue;
		if ((i & STAT_MASK) != STAT_OK)
			break;
		if (i & DRQ_STAT)
			goto ok_to_read;
	} while (--retries > 0);
	sti();
	printk("hd%c: read_intr: status = 0x%02x\n",dev+'a',i);
	if (i & ERR_STAT) {
		hd_error = (unsigned) inb(HD_ERROR);
		printk("hd%c: read_intr: error = 0x%02x\n",dev+'a',hd_error);
	}
	bad_rw_intr();
	hd_request();
	return;
ok_to_read:
	if (msect) {
		if ((nsect = CURRENT->current_nr_sectors) > msect)
			nsect = msect;
		msect -= nsect;
	} else
		nsect = 1;
	insw(HD_DATA,CURRENT->buffer,nsect<<8);
	CURRENT->sector += nsect;
	CURRENT->buffer += nsect<<9;
	CURRENT->errors = 0;
	i = (CURRENT->nr_sectors -= nsect);

#ifdef DEBUG
	printk("hd%c: read: sectors(%ld-%ld), remaining=%ld, buffer=%08lx\n",
		dev+'a', CURRENT->sector, CURRENT->sector+nsect,
		CURRENT->nr_sectors, (long) CURRENT->buffer+(nsect<<9));
#endif
	if ((CURRENT->current_nr_sectors -= nsect) <= 0)
		end_request(1);
	if (i > 0) {
		if (msect)
			goto ok_to_read;
		SET_INTR(&read_intr);
		return;
	}
	(void) inb_p(HD_STATUS);
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	if (CURRENT)
		hd_request();
	return;
}

static inline void multwrite (unsigned int dev)
{
	unsigned int mcount = mult_count[dev];

	while (mcount--) {
		outsw(HD_DATA,WCURRENT.buffer,256);
		if (!--WCURRENT.nr_sectors)
			return;
		WCURRENT.buffer += 512;
		if (!--WCURRENT.current_nr_sectors) {
			WCURRENT.bh = WCURRENT.bh->b_reqnext;
			if (WCURRENT.bh == NULL)
				panic("buffer list corrupted\n");
			WCURRENT.current_nr_sectors = WCURRENT.bh->b_size>>9;
			WCURRENT.buffer             = WCURRENT.bh->b_data;
		}
	}
}

static void multwrite_intr(void)
{
	int i;
	unsigned int dev = DEVICE_NR(WCURRENT.dev);

	if (unmask_intr[dev])
		sti();
	if (((i = inb_p(HD_STATUS)) & STAT_MASK) == STAT_OK) {
		if (i & DRQ_STAT) {
			if (WCURRENT.nr_sectors) {
				multwrite(dev);
				SET_INTR(&multwrite_intr);
				return;
			}
		} else {
			if (!WCURRENT.nr_sectors) {	/* all done? */
				for (i = CURRENT->nr_sectors; i > 0;){
					i -= CURRENT->current_nr_sectors;
					end_request(1);
				}
#if (HD_DELAY > 0)
				last_req = read_timer();
#endif
				if (CURRENT)
					hd_request();
				return;
			}
		}
	}
	sti();
	printk("hd%c: multwrite_intr: status = 0x%02x\n",dev+'a',i);
	if (i & ERR_STAT) {
		hd_error = (unsigned) inb(HD_ERROR);
		printk("hd:%c multwrite_intr: error = 0x%02x\n",dev+'a',hd_error);
	}
	bad_rw_intr();
	hd_request();
}

static void write_intr(void)
{
	int i;
	int retries = 100000;

	if (unmask_intr[DEVICE_NR(WCURRENT.dev)])
		sti();
	do {
		i = (unsigned) inb_p(HD_STATUS);
		if (i & BUSY_STAT)
			continue;
		if ((i & STAT_MASK) != STAT_OK)
			break;
		if ((CURRENT->nr_sectors <= 1) || (i & DRQ_STAT))
			goto ok_to_write;
	} while (--retries > 0);
	sti();
	printk("HD: write_intr: status = 0x%02x\n",i);
	if (i & ERR_STAT) {
		hd_error = (unsigned) inb(HD_ERROR);
		printk("HD: write_intr: error = 0x%02x\n",hd_error);
	}
	bad_rw_intr();
	hd_request();
	return;
ok_to_write:
	CURRENT->sector++;
	i = --CURRENT->nr_sectors;
	--CURRENT->current_nr_sectors;
	CURRENT->buffer += 512;
	if (!i || (CURRENT->bh && !SUBSECTOR(i)))
		end_request(1);
	if (i > 0) {
		SET_INTR(&write_intr);
		outsw(HD_DATA,CURRENT->buffer,256);
		sti();
	} else {
#if (HD_DELAY > 0)
		last_req = read_timer();
#endif
		hd_request();
	}
	return;
}

static void recal_intr(void)
{
	if (win_result())
		bad_rw_intr();
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	hd_request();
}

/*
 * This is another of the error-routines I don't know what to do with. The
 * best idea seems to just set reset, and start all over again.
 */
static void hd_times_out(void)
{
	DEVICE_INTR = NULL;
	sti();
	reset = 1;
	if (!CURRENT)
		return;
	special_op [DEVICE_NR(CURRENT->dev)] ++;
	printk(KERN_DEBUG "HD timeout\n");
	cli();
	if (++CURRENT->errors >= MAX_ERRORS) {
#ifdef DEBUG
		printk("hd : too many errors.\n");
#endif
		end_request(0);
	}

	hd_request();
}

/*
 * The driver has been modified to enable interrupts a bit more: in order to
 * do this we first (a) disable the timeout-interrupt and (b) clear the
 * device-interrupt. This way the interrupts won't mess with out code (the
 * worst that can happen is that an unexpected HD-interrupt comes in and
 * sets the "reset" variable and starts the timer)
 */
static void hd_request(void)
{
	unsigned int block,dev;
	unsigned int sec,head,cyl,track;
	unsigned int nsect;

	if (CURRENT && CURRENT->dev < 0) return;

	if (DEVICE_INTR)
		return;
repeat:
	timer_active &= ~(1<<HD_TIMER);
	sti();
	INIT_REQUEST;
	dev = MINOR(CURRENT->dev);
	block = CURRENT->sector;
	nsect = CURRENT->nr_sectors;
	if (dev >= (NR_HD<<6) || block >= hd[dev].nr_sects) {
#ifdef DEBUG
		printk("hd : attempted read for sector %d past end of device at sector %d.\n",
		   	block, hd[dev].nr_sects);
#endif
		end_request(0);
		goto repeat;
	}
	block += hd[dev].start_sect;
	dev >>= 6;
	sec = block % hd_info[dev].sect + 1;
	track = block / hd_info[dev].sect;
	head = track % hd_info[dev].head;
	cyl = track / hd_info[dev].head;
#ifdef DEBUG
	printk("hd%c : cyl = %d, head = %d, sector = %d, buffer = %08x\n",
		dev+'a', cyl, head, sec, CURRENT->buffer);
#endif
	if (!unmask_intr[dev])
		cli();
	if (special_op[dev]) {	/* we use "special_op" to reduce overhead on r/w */
		if (reset) {
			int i;
	
			for (i=0; i < NR_HD; i++)
				special_op[i] = recalibrate[i] = 1;
			cli(); /* better play it safe, as resets are the last resort */
			reset_hd();
			return;
		}
		if (recalibrate[dev]) {
			recalibrate[dev] = 0;
			hd_out(dev,hd_info[dev].sect,0,0,0,WIN_RESTORE,&recal_intr);
			if (reset)
				goto repeat;
			return;
		}	
		if (!identified[dev]) {
			identified[dev]  = 1;
			unmask_intr[dev] = DEFAULT_UNMASK_INTR;
			mult_req[dev]    = DEFAULT_MULT_COUNT;
			hd_out(dev,0,0,0,0,WIN_IDENTIFY,&identify_intr);
			if (reset)
				goto repeat;
			return;
		}
		if (mult_req[dev] != mult_count[dev]) {
			hd_out(dev,mult_req[dev],0,0,0,WIN_SETMULT,&set_multmode_intr);
			if (reset)
				goto repeat;
			return;
		}
		if (hd_info[dev].head > 16) {
			printk ("hd%c: cannot handle device with more than 16 heads - giving up\n", dev+'a');
			end_request(0);
			goto repeat;
		}
		--special_op[dev];
	} /* special_op[dev] */
	if (CURRENT->cmd == READ) {
		unsigned int cmd = mult_count[dev] > 1 ? WIN_MULTREAD : WIN_READ;
		hd_out(dev,nsect,sec,head,cyl,cmd,&read_intr);
		if (reset)
			goto repeat;
#ifdef DEBUG
		printk("hd%c: reading %d sectors(%ld-%ld), buffer=%08lx\n",
			dev+'a', nsect, CURRENT->sector,
			CURRENT->sector+nsect-1, (long) CURRENT->buffer);
#endif
		return;
	}
	if (CURRENT->cmd == WRITE) {
		if (mult_count[dev])
			hd_out(dev,nsect,sec,head,cyl,WIN_MULTWRITE,&multwrite_intr);
		else
			hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
		if (reset)
			goto repeat;
#ifdef DEBUG
		printk("hd%c: writing %d sectors(%ld-%ld), buffer=%08lx\n",
			dev+'a', nsect, CURRENT->sector,
			CURRENT->sector+nsect-1, (long) CURRENT->buffer);
#endif
		if (wait_DRQ()) {
			printk("hd%c: hd_request: no DRQ\n", dev+'a');
			bad_rw_intr();
			goto repeat;
		}
		if (mult_count[dev]) {
			WCURRENT = *CURRENT;
			multwrite(dev);
		} else {
			outsw(HD_DATA,CURRENT->buffer,256);
		}
		return;
	}
	panic("unknown hd-command");
}

static void do_hd_request (void)
{
	disable_irq(HD_IRQ);
	hd_request();
	enable_irq(HD_IRQ);
}

static int hd_ioctl(struct inode * inode, struct file * file,
	unsigned int cmd, unsigned long arg)
{
	struct hd_geometry *loc = (struct hd_geometry *) arg;
	int dev, err;

	if ((!inode) || (!inode->i_rdev))
		return -EINVAL;
	dev = MINOR(inode->i_rdev) >> 6;
	if (dev >= NR_HD)
		return -EINVAL;
	switch (cmd) {
		case HDIO_GETGEO:
			if (!loc)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, loc, sizeof(*loc));
			if (err)
				return err;
			put_fs_byte(bios_info[dev].head,
				(char *) &loc->heads);
			put_fs_byte(bios_info[dev].sect,
				(char *) &loc->sectors);
			put_fs_word(bios_info[dev].cyl,
				(short *) &loc->cylinders);
			put_fs_long(hd[MINOR(inode->i_rdev)].start_sect,
				(long *) &loc->start);
			return 0;
		case BLKRASET:
			if(!suser())  return -EACCES;
			if(arg > 0xff) return -EINVAL;
			read_ahead[MAJOR(inode->i_rdev)] = arg;
			return 0;
		case BLKRAGET:
			if (!arg)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
			if (err)
				return err;
			put_fs_long(read_ahead[MAJOR(inode->i_rdev)],(long *) arg);
			return 0;
         	case BLKGETSIZE:   /* Return device size */
			if (!arg)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
			if (err)
				return err;
			put_fs_long(hd[MINOR(inode->i_rdev)].nr_sects, (long *) arg);
			return 0;
		case BLKFLSBUF:
			if(!suser())  return -EACCES;
			fsync_dev(inode->i_rdev);
			invalidate_buffers(inode->i_rdev);
			return 0;

		case BLKRRPART: /* Re-read partition tables */
			return revalidate_hddisk(inode->i_rdev, 1);

		case HDIO_SETUNMASKINTR:
			if (!suser()) return -EACCES;
			if (!arg)  return -EINVAL;
			if (MINOR(inode->i_rdev) & 0x3F) return -EINVAL;
			err = verify_area(VERIFY_READ, (long *) arg, sizeof(long));
			if (err)
				return err;
			unmask_intr[dev] = get_fs_long((long *) arg);
			return 0;

                case HDIO_GETUNMASKINTR:
			if (!arg)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
			if (err)
				return err;
			put_fs_long(unmask_intr[dev], (long *) arg);
			return 0;

                case HDIO_GETMULTCOUNT:
			if (!arg)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
			if (err)
				return err;
			put_fs_long(mult_count[dev], (long *) arg);
			return 0;

		case HDIO_SETMULTCOUNT:
		{
			unsigned long flags;
			if (!suser()) return -EACCES;
			if (!arg)  return -EINVAL;
			if (MINOR(inode->i_rdev) & 0x3F) return -EINVAL;
			err = verify_area(VERIFY_READ, (long *) arg, sizeof(long));
			if (err)
				return err;
			arg = get_fs_long((long *) arg);
			save_flags(flags);
			cli();	/* a prior request might still be in progress */
			if (arg > max_mult[dev])
				err = -EINVAL;	/* out of range for device */
			else if (mult_req[dev] != mult_count[dev]) {
				++special_op[dev];
				err = -EBUSY;	/* busy, try again */
			} else {
				mult_req[dev] = arg;
				++special_op[dev];
				err = 0;
			}
			restore_flags(flags);
			return err;
		}
		case HDIO_GETIDENTITY:
			if (!arg)  return -EINVAL;
			if (MINOR(inode->i_rdev) & 0x3F) return -EINVAL;
			if (hd_ident_info[dev] == NULL)  return -ENOMSG;
			err = verify_area(VERIFY_WRITE, (char *) arg, sizeof(struct hd_driveid));
			if (err)
				return err;
			memcpy_tofs((char *)arg, (char *) hd_ident_info[dev], sizeof(struct hd_driveid));

		RO_IOCTLS(inode->i_rdev,arg);
		default:
			return -EINVAL;
	}
}

static int hd_open(struct inode * inode, struct file * filp)
{
	int target;
	target =  DEVICE_NR(MINOR(inode->i_rdev));

	while (busy[target])
		sleep_on(&busy_wait);
	access_count[target]++;
	return 0;
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */
static void hd_release(struct inode * inode, struct file * file)
{
        int target;
	sync_dev(inode->i_rdev);

	target =  DEVICE_NR(MINOR(inode->i_rdev));
	access_count[target]--;

}

static void hd_geninit(void);

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
	(void *) bios_info,	/* internal */
	NULL		/* next */
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
 * This is the harddisk IRQ description. The SA_INTERRUPT in sa_flags
 * means we run the IRQ-handler with interrupts disabled: this is bad for
 * interrupt latency, but anything else has led to problems on some
 * machines...
 *
 * We enable interrupts in some of the routines after making sure it's
 * safe.
 */
static void hd_geninit(void)
{
	int drive, i;
	extern struct drive_info drive_info;
	unsigned char *BIOS = (unsigned char *) &drive_info;
	int cmos_disks;

	if (!NR_HD) {	   
		for (drive=0 ; drive<2 ; drive++) {
			bios_info[drive].cyl   = hd_info[drive].cyl = *(unsigned short *) BIOS;
			bios_info[drive].head  = hd_info[drive].head = *(2+BIOS);
			bios_info[drive].wpcom = hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
			bios_info[drive].ctl   = hd_info[drive].ctl = *(8+BIOS);
			bios_info[drive].lzone = hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
			bios_info[drive].sect  = hd_info[drive].sect = *(14+BIOS);
#ifdef does_not_work_for_everybody_with_scsi_but_helps_ibm_vp
			if (hd_info[drive].cyl && NR_HD == drive)
				NR_HD++;
#endif
			BIOS += 16;
		}

	/*
		We query CMOS about hard disks : it could be that 
		we have a SCSI/ESDI/etc controller that is BIOS
		compatible with ST-506, and thus showing up in our
		BIOS table, but not register compatible, and therefore
		not present in CMOS.

		Furthermore, we will assume that our ST-506 drives
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
	}
	i = NR_HD;
	while (i-- > 0) {
		hd[i<<6].nr_sects = 0;
		if (bios_info[i].head > 16) {
			/*
			 * The newer E-IDE BIOSs handle drives larger than 1024
			 * cylinders by increasing the number of logical heads
			 * to keep the number of logical cylinders below the
			 * sacred INT13 limit of 1024 (10 bits).  If that is
			 * what's happening here, we'll find out and correct
			 * it later when "identifying" the drive.
			 */
			printk("hd.c: IDE/ST-506 disk with more than 16 heads detected.\n");
			printk("  (hd%c: cyl=%d, sect=%d, head=%d)\n", i+'a',
				bios_info[i].cyl,
				bios_info[i].sect,
				bios_info[i].head);
		}
		hd[i<<6].nr_sects = bios_info[i].head *
				bios_info[i].sect * bios_info[i].cyl;
		hd_ident_info[i] = NULL;
		special_op[i] = 1;
	}
	if (NR_HD) {
		if (request_irq(HD_IRQ, hd_interrupt, SA_INTERRUPT, "hd")) {
			printk("hd.c: unable to get IRQ%d for the harddisk driver\n",HD_IRQ);
			NR_HD = 0;
		}
	}
	hd_gendisk.nr_real = NR_HD;

	for(i=0;i<(MAX_HD << 6);i++) hd_blocksizes[i] = 1024;
	blksize_size[MAJOR_NR] = hd_blocksizes;
}

static struct file_operations hd_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	hd_ioctl,		/* ioctl */
	NULL,			/* mmap */
	hd_open,		/* open */
	hd_release,		/* release */
	block_fsync		/* fsync */
};

unsigned long hd_init(unsigned long mem_start, unsigned long mem_end)
{
	if (register_blkdev(MAJOR_NR,"hd",&hd_fops)) {
		printk("Unable to get major %d for harddisk\n",MAJOR_NR);
		return mem_start;
	}
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	read_ahead[MAJOR_NR] = 8;		/* 8 sector (4kB) read-ahead */
	hd_gendisk.next = gendisk_head;
	gendisk_head = &hd_gendisk;
	timer_table[HD_TIMER].fn = hd_times_out;
	return mem_start;
}

#define DEVICE_BUSY busy[target]
#define USAGE access_count[target]
#define CAPACITY (bios_info[target].head*bios_info[target].sect*bios_info[target].cyl)
/* We assume that the the bios parameters do not change, so the disk capacity
   will not change */
#undef MAYBE_REINIT
#define GENDISK_STRUCT hd_gendisk

/*
 * This routine is called to flush all partitions and partition tables
 * for a changed scsi disk, and then re-read the new partition table.
 * If we are revalidating a disk because of a media change, then we
 * enter with usage == 0.  If we are using an ioctl, we automatically have
 * usage == 1 (we need an open channel to use an ioctl :-), so this
 * is our limit.
 */
static int revalidate_hddisk(int dev, int maxusage)
{
	int target, major;
	struct gendisk * gdev;
	int max_p;
	int start;
	int i;
	long flags;

	target =  DEVICE_NR(MINOR(dev));
	gdev = &GENDISK_STRUCT;

	save_flags(flags);
	cli();
	if (DEVICE_BUSY || USAGE > maxusage) {
		restore_flags(flags);
		return -EBUSY;
	};
	DEVICE_BUSY = 1;
	restore_flags(flags);

	max_p = gdev->max_p;
	start = target << gdev->minor_shift;
	major = MAJOR_NR << 8;

	for (i=max_p - 1; i >=0 ; i--) {
		sync_dev(major | start | i);
		invalidate_inodes(major | start | i);
		invalidate_buffers(major | start | i);
		gdev->part[start+i].start_sect = 0;
		gdev->part[start+i].nr_sects = 0;
	};

#ifdef MAYBE_REINIT
	MAYBE_REINIT;
#endif

	gdev->part[start].nr_sects = CAPACITY;
	resetup_one_dev(gdev, target);

	DEVICE_BUSY = 0;
	wake_up(&busy_wait);
	return 0;
}

