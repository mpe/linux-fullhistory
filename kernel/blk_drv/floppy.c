/*
 *  linux/kernel/floppy.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 02.12.91 - Changed to static variables to indicate need for reset
 * and recalibrate. This makes some things easier (output_byte reset
 * checking etc), and means less interrupt jumping in case of errors,
 * so the code is hopefully easier to understand.
 */

/*
 * This file is certainly a mess. I've tried my best to get it working,
 * but I don't like programming floppies, and I have only one anyway.
 * Urgel. I should check for more errors, and do more graceful error
 * recovery. Seems there are problems with several drives. I've tried to
 * correct them. No promises. 
 */

/*
 * As with hd.c, all routines within this file can (and will) be called
 * by interrupts, so extreme caution is needed. A hardware interrupt
 * handler may not sleep, or a kernel panic will happen. Thus I cannot
 * call "floppy-on" directly, but have to set a special timer interrupt
 * etc.
 */

/*
 * 28.02.92 - made track-buffering routines, based on the routines written
 * by entropy@wintermute.wpi.edu (Lawrence Foard). Linus.
 */

/*
 * Automatic floppy-detection and formatting written by Werner Almesberger
 * (almesber@nessie.cs.id.ethz.ch), who also corrected some problems with
 * the floppy-change signal detection.
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/fdreg.h>
#include <linux/fd.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <errno.h>

#define MAJOR_NR 2
#include "blk.h"

static unsigned int changed_floppies = 0, fake_change = 0;

static int recalibrate = 0;
static int reset = 0;
static int recover = 0; /* recalibrate immediately after resetting */
static int seek = 0;

extern unsigned char current_DOR;

#define immoutb_p(val,port) \
__asm__("outb %0,%1\n\tjmp 1f\n1:\tjmp 1f\n1:"::"a" ((char) (val)),"i" (port))

#define TYPE(x) ((x)>>2)
#define DRIVE(x) ((x)&0x03)
/*
 * Note that MAX_ERRORS=X doesn't imply that we retry every bad read
 * max X times - some types of errors increase the errorcount by 2 or
 * even 3, so we might actually retry only X/2 times before giving up.
 */
#define MAX_ERRORS 12

/*
 * Maximum disk size (in kilobytes). This default is used whenever the
 * current disk size is unknown.
 */

#define MAX_DISK_SIZE 1440

/*
 * Maximum number of sectors in a track buffer. Track buffering is disabled
 * if tracks are bigger.
 */

#define MAX_BUFFER_SECTORS 18

/*
 * globals used by 'result()'
 */
#define MAX_REPLIES 7
static unsigned char reply_buffer[MAX_REPLIES];
#define ST0 (reply_buffer[0])
#define ST1 (reply_buffer[1])
#define ST2 (reply_buffer[2])
#define ST3 (reply_buffer[3])

/*
 * This struct defines the different floppy types. Unlike minix
 * linux doesn't have a "search for right type"-type, as the code
 * for that is convoluted and weird. I've got enough problems with
 * this driver as it is.
 *
 * The 'stretch' tells if the tracks need to be boubled for some
 * types (ie 360kB diskette in 1.2MB drive etc). Others should
 * be self-explanatory.
 */

static struct floppy_struct floppy_type[] = {
	{    0, 0,0, 0,0,0x00,0x00,0x00,0x00,NULL },	/* no testing */
	{  720, 9,2,40,0,0x2A,0x02,0xDF,0x50,NULL },	/* 360kB PC diskettes */
	{ 2400,15,2,80,0,0x1B,0x00,0xDF,0x54,NULL },	/* 1.2 MB AT-diskettes */
	{  720, 9,2,40,1,0x2A,0x02,0xDF,0x50,NULL },	/* 360kB in 720kB drive */
	{ 1440, 9,2,80,0,0x2A,0x02,0xDF,0x50,NULL },	/* 3.5" 720kB diskette */
	{  720, 9,2,40,1,0x23,0x01,0xDF,0x50,NULL },	/* 360kB in 1.2MB drive */
	{ 1440, 9,2,80,0,0x23,0x01,0xDF,0x50,NULL },	/* 720kB in 1.2MB drive */
	{ 2880,18,2,80,0,0x1B,0x00,0xCF,0x6C,NULL },	/* 1.44MB diskette */
};

/* For auto-detection. Each drive type has a pair of formats to try. */

static struct floppy_struct floppy_types[] = {
	{  720, 9,2,40,0,0x2A,0x02,0xDF,0x50,"360k/PC" }, /* 360kB PC diskettes */
	{  720, 9,2,40,0,0x2A,0x02,0xDF,0x50,"360k/PC" }, /* 360kB PC diskettes */
	{ 2400,15,2,80,0,0x1B,0x00,0xDF,0x54,"1.2M" },	  /* 1.2 MB AT-diskettes */
	{  720, 9,2,40,1,0x23,0x01,0xDF,0x50,"360k/AT" }, /* 360kB in 1.2MB drive */
	{ 1440, 9,2,80,0,0x2A,0x02,0xDF,0x50,"720k" },	  /* 3.5" 720kB diskette */
	{ 1440, 9,2,80,0,0x2A,0x02,0xDF,0x50,"720k" },	  /* 3.5" 720kB diskette */
	{ 2880,18,2,80,0,0x1B,0x00,0xCF,0x6C,"1.44M" },	  /* 1.44MB diskette */
	{ 1440, 9,2,80,0,0x2A,0x02,0xDF,0x50,"720k/AT" }, /* 3.5" 720kB diskette */
};

/* Auto-detection: Disk type used until the next media change occurs. */

struct floppy_struct *current_type[4] = { NULL, NULL, NULL, NULL };

/* This type is tried first. */

struct floppy_struct *base_type[4];

/* User-provided type information. current_type points to the respective entry
   of this array. */

struct floppy_struct user_params[4];

static int floppy_sizes[] ={
	MAX_DISK_SIZE, MAX_DISK_SIZE, MAX_DISK_SIZE, MAX_DISK_SIZE,
	 360, 360 ,360, 360,
	1200,1200,1200,1200,
	 360, 360, 360, 360,
	 720, 720, 720, 720,
	 360, 360, 360, 360,
	 720, 720, 720, 720,
	1440,1440,1440,1440
};

/* The driver is trying to determine the correct media format while probing
   is set. rw_interrupts clears it after a successful access. */

static int probing = 0;

/* (User-provided) media information is _not_ discarded after a media change
   if the corresponding keep_data flag is non-zero. Positive values are
   decremented after each probe. */

static int keep_data[4] = { 0,0,0,0 };

/* Announce successful media type detection and media information loss after
   disk changes. */

static ftd_msg[4] = { 1,1,1,1 };

/* Synchronization of FDC access. */

static volatile int format_status = FORMAT_NONE, fdc_busy = 0;
static struct task_struct *fdc_wait = NULL, *format_done = NULL;

/* Errors during formatting are counted here. */

static int format_errors;

/* Format request descriptor. */

static struct format_descr format_req;

/* Current device number. Taken either from the block header or from the
   format request descriptor. */

#define CURRENT_DEVICE (format_status == FORMAT_BUSY ? format_req.device : \
   (CURRENT->dev))

/* Current error count. */

#define CURRENT_ERRORS (format_status == FORMAT_BUSY ? format_errors : \
    (CURRENT->errors))

/*
 * Rate is 0 for 500kb/s, 2 for 300kbps, 1 for 250kbps
 * Spec1 is 0xSH, where S is stepping rate (F=1ms, E=2ms, D=3ms etc),
 * H is head unload time (1=16ms, 2=32ms, etc)
 *
 * Spec2 is (HLD<<1 | ND), where HLD is head load time (1=2ms, 2=4 ms etc)
 * and ND is set means no DMA. Hardcoded to 6 (HLD=6ms, use DMA).
 */

extern void floppy_interrupt(void);
extern char tmp_floppy_area[1024];
extern char floppy_track_buffer[512*2*MAX_BUFFER_SECTORS];

static void redo_fd_request(void);

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
#define NO_TRACK 255

static int read_track = 0;	/* flag to indicate if we want to read all track */
static int buffer_track = -1;
static int buffer_drive = -1;
static int cur_spec1 = -1;
static int cur_rate = -1;
static struct floppy_struct * floppy = floppy_type;
static unsigned char current_drive = 255;
static unsigned char sector = 0;
static unsigned char head = 0;
static unsigned char track = 0;
static unsigned char seek_track = 0;
static unsigned char current_track = NO_TRACK;
static unsigned char command = 0;
unsigned char selected = 0;
struct task_struct * wait_on_floppy_select = NULL;

void floppy_deselect(unsigned int nr)
{
	if (nr != (current_DOR & 3))
		printk("floppy_deselect: drive not selected\n\r");
	selected = 0;
	wake_up(&wait_on_floppy_select);
}

void request_done(int uptodate)
{
	timer_active &= ~(1 << FLOPPY_TIMER);
	if (format_status != FORMAT_BUSY) end_request(uptodate);
	else {
		format_status = uptodate ? FORMAT_OKAY : FORMAT_ERROR;
		wake_up(&format_done);
	}
}

/*
 * floppy-change is never called from an interrupt, so we can relax a bit
 * here, sleep etc. Note that floppy-on tries to set current_DOR to point
 * to the desired drive, but it will probably not survive the sleep if
 * several floppies are used at the same time: thus the loop.
 */
int floppy_change(struct buffer_head * bh)
{
	unsigned int mask = 1 << (bh->b_dev & 0x03);

	if (MAJOR(bh->b_dev) != 2) {
		printk("floppy_changed: not a floppy\r\n");
		return 0;
	}
	if (fake_change & mask) {
		fake_change &= ~mask;
/* omitting the next line breaks formatting in a horrible way ... */
		changed_floppies &= ~mask;
		return 1;
	}
	if (changed_floppies & mask) {
		changed_floppies &= ~mask;
		recalibrate = 1;
		return 1;
	}
	if (!bh)
		return 0;
	if (bh->b_dirt)
		ll_rw_block(WRITE,bh);
	else {
		buffer_track = -1;
		bh->b_uptodate = 0;
		ll_rw_block(READ,bh);
	}
	cli();
	while (bh->b_lock)
		sleep_on(&bh->b_wait);
	sti();
	if (changed_floppies & mask) {
		changed_floppies &= ~mask;
		recalibrate = 1;
		return 1;
	}
	return 0;
}

#define copy_buffer(from,to) \
__asm__("cld ; rep ; movsl" \
	::"c" (BLOCK_SIZE/4),"S" ((long)(from)),"D" ((long)(to)) \
	:"cx","di","si")

static void setup_DMA(void)
{
	unsigned long addr,count;

	if (command == FD_FORMAT) {
		addr = (long) tmp_floppy_area;
		count = floppy->sect*4;
	}
	else {
		addr = (long) CURRENT->buffer;
		count = 1024;
	}
	if (read_track) {
/* mark buffer-track bad, in case all this fails.. */
		buffer_drive = buffer_track = -1;
		count = floppy->sect*2*512;
		addr = (long) floppy_track_buffer;
	} else if (addr >= 0x100000) {
		addr = (long) tmp_floppy_area;
		if (command == FD_WRITE)
			copy_buffer(CURRENT->buffer,tmp_floppy_area);
	}
/* mask DMA 2 */
	cli();
	immoutb_p(4|2,10);
/* output command byte. I don't know why, but everyone (minix, */
/* sanches & canton) output this twice, first to 12 then to 11 */
 	__asm__("outb %%al,$12\n\tjmp 1f\n1:\tjmp 1f\n1:\t"
	"outb %%al,$11\n\tjmp 1f\n1:\tjmp 1f\n1:"::
	"a" ((char) ((command == FD_READ)?DMA_READ:DMA_WRITE)));
/* 8 low bits of addr */
	immoutb_p(addr,4);
	addr >>= 8;
/* bits 8-15 of addr */
	immoutb_p(addr,4);
	addr >>= 8;
/* bits 16-19 of addr */
	immoutb_p(addr,0x81);
/* low 8 bits of count-1 */
	count--;
	immoutb_p(count,5);
	count >>= 8;
/* high 8 bits of count-1 */
	immoutb_p(count,5);
/* activate DMA 2 */
	immoutb_p(0|2,10);
	sti();
}

static void output_byte(char byte)
{
	int counter;
	unsigned char status;

	if (reset)
		return;
	for(counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS) & (STATUS_READY | STATUS_DIR);
		if (status == STATUS_READY) {
			outb(byte,FD_DATA);
			return;
		}
	}
	current_track = NO_TRACK;
	reset = 1;
	printk("Unable to send byte to FDC\n\r");
}

static int result(void)
{
	int i = 0, counter, status;

	if (reset)
		return -1;
	for (counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS)&(STATUS_DIR|STATUS_READY|STATUS_BUSY);
		if (status == STATUS_READY)
			return i;
		if (status == (STATUS_DIR|STATUS_READY|STATUS_BUSY)) {
			if (i >= MAX_REPLIES)
				break;
			reply_buffer[i++] = inb_p(FD_DATA);
		}
	}
	reset = 1;
	current_track = NO_TRACK;
	printk("Getstatus times out\n\r");
	return -1;
}

static void bad_flp_intr(void)
{
	current_track = NO_TRACK;
	CURRENT_ERRORS++;
	if (CURRENT_ERRORS > MAX_ERRORS) {
		floppy_deselect(current_drive);
		request_done(0);
	}
	if (CURRENT_ERRORS > MAX_ERRORS/2)
		reset = 1;
	else
		recalibrate = 1;
}	

/*
 * Ok, this interrupt is called after a DMA read/write has succeeded,
 * so we check the results, and copy any buffers.
 */
static void rw_interrupt(void)
{
	char * buffer_area;

	if (result() != 7 || (ST0 & 0xf8) || (ST1 & 0xbf) || (ST2 & 0x73)) {
		if (ST1 & 0x02) {
			printk("Drive %d is write protected\n\r",current_drive);
			floppy_deselect(current_drive);
			request_done(0);
		} else
			bad_flp_intr();
		redo_fd_request();
		return;
	}
	if (probing) {
		int drive = MINOR(CURRENT->dev);

		if (ftd_msg[drive])
			printk("Auto-detected floppy type %s in fd%d\r\n",
			    floppy->name,drive);
		current_type[drive] = floppy;
		floppy_sizes[drive] = floppy->size >> 1;
		probing = 0;
	}
	if (read_track) {
		buffer_track = seek_track;
		buffer_drive = current_drive;
		buffer_area = floppy_track_buffer +
			((sector-1 + head*floppy->sect)<<9);
		copy_buffer(buffer_area,CURRENT->buffer);
	} else if (command == FD_READ &&
		(unsigned long)(CURRENT->buffer) >= 0x100000)
		copy_buffer(tmp_floppy_area,CURRENT->buffer);
	floppy_deselect(current_drive);
	request_done(1);
	redo_fd_request();
}

/*
 * We try to read tracks, but if we get too many errors, we
 * go back to reading just one sector at a time.
 *
 * This means we should be able to read a sector even if there
 * are other bad sectors on this track.
 */
inline void setup_rw_floppy(void)
{
	setup_DMA();
	do_floppy = rw_interrupt;
	output_byte(command);
	if (command != FD_FORMAT) {
		if (read_track) {
			output_byte(current_drive);
			output_byte(track);
			output_byte(0);
			output_byte(1);
		} else {
			output_byte(head<<2 | current_drive);
			output_byte(track);
			output_byte(head);
			output_byte(sector);
		}
		output_byte(2);		/* sector size = 512 */
		output_byte(floppy->sect);
		output_byte(floppy->gap);
		output_byte(0xFF);	/* sector size (0xff when n!=0 ?) */
	} else {
		output_byte(head<<2 | current_drive);
		output_byte(2);
		output_byte(floppy->sect);
		output_byte(floppy->fmt_gap);
		output_byte(FD_FILL_BYTE);
	}
	if (reset)
		redo_fd_request();
}

/*
 * This is the routine called after every seek (or recalibrate) interrupt
 * from the floppy controller. Note that the "unexpected interrupt" routine
 * also does a recalibrate, but doesn't come here.
 */
static void seek_interrupt(void)
{
/* sense drive status */
	output_byte(FD_SENSEI);
	if (result() != 2 || (ST0 & 0xF8) != 0x20 || ST1 != seek_track) {
		recalibrate = 1;
		bad_flp_intr();
		redo_fd_request();
		return;
	}
	current_track = ST1;
	setup_rw_floppy();
}

/*
 * This routine is called when everything should be correctly set up
 * for the transfer (ie floppy motor is on and the correct floppy is
 * selected).
 */
static void transfer(void)
{
	read_track = (command == FD_READ) && (CURRENT_ERRORS < 4) &&
	    (floppy->sect <= MAX_BUFFER_SECTORS);
	if (cur_spec1 != floppy->spec1) {
		cur_spec1 = floppy->spec1;
		output_byte(FD_SPECIFY);
		output_byte(cur_spec1);		/* hut etc */
		output_byte(6);			/* Head load time =6ms, DMA */
	}
	if (cur_rate != floppy->rate)
		outb_p(cur_rate = floppy->rate,FD_DCR);
	if (reset) {
		redo_fd_request();
		return;
	}
	if (!seek) {
		setup_rw_floppy();
		return;
	}
	do_floppy = seek_interrupt;
	output_byte(FD_SEEK);
	if (read_track)
		output_byte(current_drive);
	else
		output_byte((head<<2) | current_drive);
	output_byte(seek_track);
	if (reset)
		redo_fd_request();
}

/*
 * Special case - used after a unexpected interrupt (or reset)
 */

static void recalibrate_floppy();

static void recal_interrupt(void)
{
	output_byte(FD_SENSEI);
	current_track = NO_TRACK;
	if (result()!=2 || (ST0 & 0xE0) == 0x60)
		reset = 1;
/* Recalibrate until track 0 is reached. Might help on some errors. */
	if ((ST0 & 0x10) == 0x10) recalibrate_floppy();
	else redo_fd_request();
}

void unexpected_floppy_interrupt(void)
{
	current_track = NO_TRACK;
	output_byte(FD_SENSEI);
	if (result()!=2 || (ST0 & 0xE0) == 0x60)
		reset = 1;
	else
		recalibrate = 1;
}

static void recalibrate_floppy(void)
{
	recalibrate = 0;
	current_track = 0;
	do_floppy = recal_interrupt;
	output_byte(FD_RECALIBRATE);
	output_byte(head<<2 | current_drive);
	if (reset)
		redo_fd_request();
}

static void reset_interrupt(void)
{
	output_byte(FD_SENSEI);
	(void) result();
	output_byte(FD_SPECIFY);
	output_byte(cur_spec1);		/* hut etc */
	output_byte(6);			/* Head load time =6ms, DMA */
	if (!recover) redo_fd_request();
	else {
		recalibrate_floppy();
		recover = 0;
	}
}

/*
 * reset is done by pulling bit 2 of DOR low for a while.
 */
static void reset_floppy(void)
{
	int i;

	do_floppy = reset_interrupt;
	reset = 0;
	current_track = NO_TRACK;
	cur_spec1 = -1;
	cur_rate = -1;
	recalibrate = 1;
	printk("Reset-floppy called\n\r");
	cli();
	outb_p(current_DOR & ~0x04,FD_DOR);
	for (i=0 ; i<1000 ; i++)
		__asm__("nop");
	outb(current_DOR,FD_DOR);
	sti();
}

static void floppy_shutdown(void)
{
	cli();
	request_done(0);
	recover = 1;
	reset_floppy();
	sti();
}

static void shake_done(void)
{
	current_track = NO_TRACK;
	if (inb(FD_DIR) & 0x80) request_done(0);
	redo_fd_request();
}

static int retry_recal(void (*proc)(void))
{
	output_byte(FD_SENSEI);
	if (result() == 2 && (ST0 & 0x10) != 0x10) return 0;
	do_floppy = proc;
	output_byte(FD_RECALIBRATE);
	output_byte(head<<2 | current_drive);
	return 1;
}

static void shake_zero(void)
{
	if (!retry_recal(shake_zero)) shake_done();
}

static void shake_one(void)
{
	if (retry_recal(shake_one)) return;
	do_floppy = shake_done;
	output_byte(FD_SEEK);
	output_byte(head << 2 | current_drive);
	output_byte(1);
}

static void floppy_on_interrupt(void)
{
	if (inb(FD_DIR) & 0x80) {
		changed_floppies |= 1<<current_drive;
		buffer_track = -1;
		if (keep_data[current_drive]) {
			if (keep_data[current_drive] > 0)
				keep_data[current_drive]--;
		}
		else {
			if (ftd_msg[current_drive] && current_type[
			    current_drive] != NULL)
				printk("Disk type is undefined after disk "
				    "change in fd%d\r\n",current_drive);
			current_type[current_drive] = NULL;
			floppy_sizes[current_drive] = MAX_DISK_SIZE;
		}
/* Forcing the drive to seek makes the "media changed" condition go away.
   There should be a cleaner solution for that ... */
		if (!reset && !recalibrate) {
			do_floppy = (current_track && current_track != NO_TRACK)
			    ?  shake_zero : shake_one;
			output_byte(FD_RECALIBRATE);
			output_byte(head<<2 | current_drive);
			return;
		}
	}
	if (reset) {
		reset_floppy();
		return;
	}
	if (recalibrate) {
		recalibrate_floppy();
		return;
	}
/* We cannot do a floppy-select, as that might sleep. We just force it */
	selected = 1;
	if (current_drive != (current_DOR & 3)) {
		seek = 1;
		current_track = NO_TRACK;
		current_DOR &= 0xFC;
		current_DOR |= current_drive;
		outb(current_DOR,FD_DOR);
		add_timer(2,&transfer);
	} else
		transfer();
}

static void setup_format_params(void)
{
    unsigned char *here = (unsigned char *) tmp_floppy_area;
    int count;

    for (count = 1; count <= floppy->sect; count++) {
	*here++ = track;
	*here++ = head;
	*here++ = count;
	*here++ = 2; /* 512 bytes */
    }
}

static void redo_fd_request(void)
{
	unsigned int block;
	char * buffer_area;
	int device;

repeat:
	if (format_status == FORMAT_WAIT) format_status = FORMAT_BUSY;
	if (format_status != FORMAT_BUSY) {
		if (!CURRENT) {
			if (!fdc_busy) panic("FDC access conflict");
			fdc_busy = 0;
			wake_up(&fdc_wait);
			CLEAR_INTR;
			return;
		}
		if (MAJOR(CURRENT->dev) != MAJOR_NR)
			panic(DEVICE_NAME ": request list destroyed"); \
		if (CURRENT->bh) {
			if (!CURRENT->bh->b_lock)
				panic(DEVICE_NAME ": block not locked");
		}
	}
	seek = 0;
	probing = 0;
	device = MINOR(CURRENT_DEVICE);
	if (device > 3)
		floppy = (device >> 2) + floppy_type;
	else { /* Auto-detection */
		if ((floppy = current_type[device & 3]) == NULL) {
			probing = 1;
			if ((floppy = base_type[device & 3]) ==
			    NULL) {
				request_done(0);
				goto repeat;
			}
			floppy += CURRENT_ERRORS & 1;
		}
	}
	if (format_status != FORMAT_BUSY) {
		if (current_drive != CURRENT_DEV)
			current_track = NO_TRACK;
		current_drive = CURRENT_DEV;
		block = CURRENT->sector;
		if (block+2 > floppy->size) {
			request_done(0);
			goto repeat;
		}
		sector = block % floppy->sect;
		block /= floppy->sect;
		head = block % floppy->head;
		track = block / floppy->head;
		seek_track = track << floppy->stretch;
		if (CURRENT->cmd == READ)
			command = FD_READ;
		else if (CURRENT->cmd == WRITE)
			command = FD_WRITE;
		else {
			printk("do_fd_request: unknown command\n");
			request_done(0);
			goto repeat;
		}
	}
	else {
		if (current_drive != (format_req.device & 3))
			current_track = NO_TRACK;
		current_drive = format_req.device & 3;
		if (((unsigned) format_req.track) >= floppy->track ||
		    (format_req.head & 0xfffe) || probing) {
			request_done(0);
			goto repeat;
		}
		head = format_req.head;
		track = format_req.track;
		seek_track = track << floppy->stretch;
		if (seek_track == buffer_track) buffer_track = -1;
		command = FD_FORMAT;
		setup_format_params();
	}
	timer_table[FLOPPY_TIMER].expires = jiffies+10*HZ;
	timer_active |= 1 << FLOPPY_TIMER;
	if ((seek_track == buffer_track) &&
	 (current_drive == buffer_drive)) {
		buffer_area = floppy_track_buffer +
			((sector + head*floppy->sect)<<9);
		if (command == FD_READ) {
			copy_buffer(buffer_area,CURRENT->buffer);
			request_done(1);
			goto repeat;
		} else if (command == FD_WRITE)
			copy_buffer(CURRENT->buffer,buffer_area);
	}
	if (seek_track != current_track)
		seek = 1;
	sector++;
	add_timer(ticks_to_floppy_on(current_drive),&floppy_on_interrupt);
}

void do_fd_request(void)
{
	cli();
	while (fdc_busy) sleep_on(&fdc_wait);
	fdc_busy = 1;
	sti();
	redo_fd_request();
}

static int fd_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
    unsigned int param)
{
	int drive,cnt,okay;
	struct floppy_struct *this;

	if (!suser()) return -EPERM;
	drive = MINOR(inode->i_rdev);
	switch (cmd) {
		case FDFMTBEG:
			return 0;
		case FDFMTEND:
			cli();
			fake_change |= 1 << (drive & 3);
			sti();
			drive &= 3;
			cmd = FDCLRPRM;
			break;
		case FDGETPRM:
			if (drive > 3) this = &floppy_type[drive >> 2];
			else if ((this = current_type[drive & 3]) == NULL)
				    return -ENODEV;
			verify_area((void *) param,sizeof(struct floppy_struct));
			for (cnt = 0; cnt < sizeof(struct floppy_struct); cnt++)
				put_fs_byte(((char *) this)[cnt],
				    (char *) param+cnt);
			return 0;
		case FDFMTTRK:
			cli();
			while (format_status != FORMAT_NONE)
				sleep_on(&format_done);
			for (cnt = 0; cnt < sizeof(struct format_descr); cnt++)
				((char *) &format_req)[cnt] = get_fs_byte(
				    (char *) param+cnt);
			format_req.device = drive;
			format_status = FORMAT_WAIT;
			format_errors = 0;
			while (format_status != FORMAT_OKAY && format_status !=
			    FORMAT_ERROR) {
				if (fdc_busy) sleep_on(&fdc_wait);
				else {
					fdc_busy = 1;
					redo_fd_request();
				}
			}
			while (format_status != FORMAT_OKAY && format_status !=
			    FORMAT_ERROR)
				sleep_on(&format_done);
			sti();
			okay = format_status == FORMAT_OKAY;
			format_status = FORMAT_NONE;
			wake_up(&format_done);
			return okay ? 0 : -EIO;
 	}
	if (drive < 0 || drive > 3) return -EINVAL;
	switch (cmd) {
		case FDCLRPRM:
			current_type[drive] = NULL;
			floppy_sizes[drive] = MAX_DISK_SIZE;
			keep_data[drive] = 0;
			break;
		case FDSETPRM:
		case FDDEFPRM:
			for (cnt = 0; cnt < sizeof(struct floppy_struct); cnt++)
				((char *) &user_params[drive])[cnt] =
				    get_fs_byte((char *) param+cnt);
			current_type[drive] = &user_params[drive];
			floppy_sizes[drive] = user_params[drive].size >> 1;
			if (cmd == FDDEFPRM) keep_data[drive] = -1;
			else {
				cli();
				while (fdc_busy) sleep_on(&fdc_wait);
				fdc_busy = 1;
				sti();
				outb_p((current_DOR & 0xfc) | drive |
				    (0x10 << drive),FD_DOR);
				for (cnt = 0; cnt < 1000; cnt++) __asm__("nop");
				keep_data[drive] = (inb(FD_DIR) & 0x80) ? 1 : 0;
				outb_p(current_DOR,FD_DOR);
				fdc_busy = 0;
				wake_up(&fdc_wait);
			}
			break;
		case FDMSGON:
			ftd_msg[drive] = 1;
			break;
		case FDMSGOFF:
			ftd_msg[drive] = 0;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

static struct floppy_struct *find_base(int drive,int code)
{
	struct floppy_struct *base;

	if (code > 0 && code < 5) {
		base = &floppy_types[(code-1)*2];
		printk("fd%d is %s",drive,base->name);
		return base;
	}
	printk("fd%d is unknown type %d",drive,code);
	return NULL;
}

static void config_types(void)
{
	printk("Floppy drive(s): ");
	base_type[0] = find_base(0,(CMOS_READ(0x10) >> 4) & 15);
	if (((CMOS_READ(0x14) >> 6) & 1) == 0)
		base_type[1] = NULL;
	else {
		printk(", ");
		base_type[1] = find_base(1,CMOS_READ(0x10) & 15);
	}
	base_type[2] = base_type[3] = NULL;
	printk("\r\n");
}

static int floppy_open(struct inode * inode, struct file * filp)
{
	if (filp->f_mode)
		check_disk_change(inode->i_rdev);
	return 0;
}

static void floppy_release(struct inode * inode, struct file * filp)
{
	sync_dev(inode->i_rdev);
}

static struct file_operations floppy_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	fd_ioctl,		/* ioctl */
	floppy_open,		/* open */
	floppy_release		/* release */
};

void floppy_init(void)
{
	outb(current_DOR,FD_DOR);
	blk_size[MAJOR_NR] = floppy_sizes;
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	blkdev_fops[MAJOR_NR] = &floppy_fops;
	timer_table[FLOPPY_TIMER].fn = floppy_shutdown;
	timer_active &= ~(1 << FLOPPY_TIMER);
	config_types();
	set_intr_gate(0x26,&floppy_interrupt);
	outb(inb_p(0x21)&~0x40,0x21);
}
