/*
 *  linux/amiga/amiflop.c
 *
 *  Copyright (C) 1993  Greg Harp
 *  Portions of this driver are based on code contributed by Brad Pepers
 *  
 *  revised 28.5.95 by Joerg Dorchain
 *  - now no bugs(?) any more for both HD & DD
 *  - added support for 40 Track 5.25" drives, 80-track hopefully behaves
 *    like 3.5" dd (no way to test - are there any 5.25" drives out there
 *    that work on an A4000?)
 *  - wrote formatting routine (maybe dirty, but works)
 *
 *  june/july 1995 added ms-dos support by Joerg Dorchain
 *  (portions based on messydos.device and various contributors)
 *  - currently only 9 and 18 sector disks
 *
 *  - fixed a bug with the internal trackbuffer when using multiple 
 *    disks the same time
 *  - made formatting a bit safer
 *  - added command line and machine based default for "silent" df0
 *
 *  december 1995 adapted for 1.2.13pl4 by Joerg Dorchain
 *  - works but I think it's inefficient. (look in redo_fd_request)
 *    But the changes were very efficient. (only three and a half lines)
 *
 *  january 1995 added special ioctl for tracking down read/write problems
 *  - usage ioctl(d, RAW_TRACK, ptr); the raw track buffer (MFM-encoded data
 *    is copied to area. (area should be large enough since no checking is
 *    done - 30K is currently sufficient). return the actual size of the
 *    trackbuffer
 *  - replaced udelays() by a timer (CIAA timer B) for the waits 
 *    needed for the disk mechanic.
 *
 *  revised Marts 3rd, 1996 by Jes Sorensen for use in the 1.3.28 kernel.
 *  - Minor changes to accept the kdev_t.
 *  - Replaced some more udelays with ms_delays. Udelay is just a loop,
 *    and so the delay will be different depending on the given
 *    processor :-(
 *  - The driver could use a major cleanup because of the new
 *    major/minor handling that came with kdev_t. It seems to work for
 *    the time being, but I can't guarantee that it will stay like
 *    that when we start using 16 (24?) bit minors.
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/fd.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/mm.h>

#include <asm/amifdreg.h>
#include <asm/amifd.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/irq.h>
#include <asm/bootinfo.h>
#include <asm/amigatypes.h>

#define MAJOR_NR FLOPPY_MAJOR
#include <linux/blk.h>

#undef DEBUG /* print _LOTS_ of infos */

#define RAW_IOCTL
#ifdef RAW_IOCTL
#define IOCTL_RAW_TRACK 0x5254524B  /* 'RTRK' */
#endif

/* prototypes */

static int amiga_read(int,unsigned char *, unsigned long, int);
static void amiga_write(int, unsigned long, unsigned char *, int);
static int dos_read(int, unsigned char *, unsigned long, int);
static void dos_write(int, unsigned long, unsigned char *,int);
static ushort dos_crc(void *, int, int, int);
static void fd_probe(int);


/*
 *  Defines
 */
#define MAX_SECTORS	22

/*
 *  Error codes
 */
#define FD_OK		0	/* operation succeeded */
#define FD_ERROR	-1	/* general error (seek, read, write, etc) */
#define FD_NOUNIT	1	/* unit does not exist */
#define FD_UNITBUSY	2	/* unit already active */
#define FD_NOTACTIVE	3	/* unit is not active */
#define FD_NOTREADY	4	/* unit is not ready (motor not on/no disk) */

/*
 *  Floppy ID values
 */
#define FD_NODRIVE	0x00000000  /* response when no unit is present */
#define FD_DD_3 	0xffffffff  /* double-density 3.5" (880K) drive */
#define FD_HD_3 	0x55555555  /* high-density 3.5" (1760K) drive */
#define FD_DD_5 	0xaaaaaaaa  /* double-density 5.25" (440K) drive */

static int fd_def_df0 = 0;     /* default for df0 if it doesn't identify */


/*
 *  Macros
 */
#define MOTOR_ON	(ciab.prb &= ~DSKMOTOR)
#define MOTOR_OFF	(ciab.prb |= DSKMOTOR)
#define SELECT(mask)    (ciab.prb &= ~mask)
#define DESELECT(mask)  (ciab.prb |= mask)
#define SELMASK(drive)  (1 << (3 + (drive & 3)))

#define DRIVE(x) ((x) & 3)
#define PROBE(x) ((x) >> 2) & 1)
#define TYPE(x)  ((x) >> 3) & 2)
#define DATA(x)  ((x) >> 5) & 3)

static struct fd_drive_type drive_types[] = {
/*  code	name	   tr he   rdsz   wrsz sm pc1 pc2 sd  st st*/
/*  warning: times are now in milliseconds (ms)                    */
 { FD_DD_3,	"DD 3.5", 160, 2, 14716, 13630, 1, 80,161, 3, 18, 1},
 { FD_HD_3,	"HD 3.5", 160, 2, 28344, 27258, 2, 80,161, 3, 18, 1},
 { FD_DD_5,	"DD 5.25", 80, 2, 14716, 13630, 1, 40, 81, 6, 30, 2},
 { FD_NODRIVE, "No Drive", 0, 0,     0,     0, 0,  0,  0,  0,  0, 0}
};
static int num_dr_types = sizeof(drive_types) / sizeof(drive_types[0]);

/* defaults for 3 1/2" HD-Disks */
static int floppy_sizes[256]={880,880,880,880,720,720,720,};
static int floppy_blocksizes[256]={0,};
/* hardsector size assumed to be 512 */

static struct fd_data_type data_types[] = {
  { "Amiga", 11 , amiga_read, amiga_write},
  { "MS-Dos", 9, dos_read, dos_write}
};
static int num_da_types = sizeof(data_types) / sizeof(data_types[0]);

/* current info on each unit */
static struct amiga_floppy_struct unit[FD_MAX_UNITS];

static struct timer_list flush_track_timer;
static struct timer_list post_write_timer;
static struct timer_list motor_on_timer;
static struct timer_list motor_off_timer[FD_MAX_UNITS];
static int on_attempts;

/* track buffer */
static int lastdrive = -1;
static int savedtrack = -1;
static int writepending = 0;
static int writefromint = 0;
static unsigned char trackdata[MAX_SECTORS * 512];
static char *raw_buf;

#define RAW_BUF_SIZE 30000  /* size of raw disk data */

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
static char block_flag = 0;
static int selected = 0;
static struct wait_queue *wait_fd_block = NULL;

/* Synchronization of FDC access. */
static volatile int fdc_busy = 0;
static struct wait_queue *fdc_wait = NULL;
static struct wait_queue *motor_wait = NULL;

/* MS-Dos MFM Coding tables (should go quick and easy) */
static unsigned char mfmencode[16]={
  0x2a, 0x29, 0x24, 0x25, 0x12, 0x11, 0x14, 0x15,
  0x4a, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55
};
static unsigned char mfmdecode[128];

/* floppy internal millisecond timer stuff */
static struct semaphore ms_sem = MUTEX;
static struct wait_queue *ms_wait = NULL;
#define MS_TICKS ((amiga_eclock+50)/1000)

static void ms_isr(int irq, struct pt_regs *fp, void *dummy)
{
wake_up(&ms_wait);
}

/* with the semaphore waits are queued up 
   A more generic routine would do a schedule a la timer.device */
static void ms_delay(int ms)
{
  int ticks;
  if (ms > 0) {
    down(&ms_sem);
    ticks=MS_TICKS*ms-1;
    ciaa.tblo=ticks%256;
    ciaa.tbhi=ticks/256;
    ciaa.crb=0x19; /* count clock, force load, one-shot, start */
    sleep_on(&ms_wait);
    up(&ms_sem);
  }
}

/*
 * Functions
 */
/*======================================================================
  Turn off the motor of the given drive.  Unit must already be active.
  Returns standard floppy error code.
======================================================================*/
static void fd_motor_off(unsigned long drive)
{
	unsigned long flags;
	unsigned char prb = ~0;

	drive&=3;
	save_flags(flags);
	cli();

	if (unit[drive].track % 2 != 0)
		prb &= ~DSKSIDE;
	ciab.prb |= (SELMASK(0)|SELMASK(1)|SELMASK(2)|SELMASK(3));
	ciab.prb = prb;
	prb &= ~SELMASK(drive);
	ciab.prb = prb;
	udelay (1);
	prb |= (SELMASK(0)|SELMASK(1)|SELMASK(2)|SELMASK(3));
	ciab.prb = prb;
	selected = -1;
	unit[drive].motor = 0;

	restore_flags(flags);
}

static void motor_on_callback(unsigned long nr)
{
  nr &= 3;

	if (!(ciaa.pra & DSKRDY) || --on_attempts == 0) {
		unit[nr].motor = 1;
		wake_up (&motor_wait);
	} else {
		motor_on_timer.expires = jiffies + HZ/10;
		add_timer(&motor_on_timer);
	}
}

static int motor_on(int nr)
{
	unsigned long flags;
	unsigned char prb = ~0;

	nr &= 3;
	save_flags (flags);
	cli();
	del_timer(motor_off_timer + nr);

	if (!unit[nr].motor) {
		del_timer(&motor_on_timer);
		motor_on_timer.data = nr;
		motor_on_timer.expires = jiffies + HZ/2;
		add_timer(&motor_on_timer);
		on_attempts = 10;


		prb &= ~DSKMOTOR;
		if (unit[nr].track % 2 != 0)
			prb &= ~DSKSIDE;
		ciab.prb |= (SELMASK(0)|SELMASK(1)|SELMASK(2)|SELMASK(3));
		ciab.prb = prb;
		prb &= ~SELMASK(nr);
		ciab.prb = prb;
		selected = nr;

		while (!unit[nr].motor)
			sleep_on (&motor_wait);
	}
	restore_flags(flags);

	if (on_attempts == 0) {
		printk ("motor_on failed, turning motor off\n");
		fd_motor_off (nr);
		return 0;
	}

	return 1;
}

static void floppy_off (unsigned int nr)
{
	nr&=3;
	del_timer(motor_off_timer+nr);
	motor_off_timer[nr].expires = jiffies + 3*HZ;
	add_timer(motor_off_timer+nr);
}

static void fd_select (int drive)
{
	unsigned char prb = ~0;

	drive&=3;
	if (drive == selected)
		return;
	selected = drive;

	if (unit[drive].track % 2 != 0)
		prb &= ~DSKSIDE;
	if (unit[drive].motor == 1)
		prb &= ~DSKMOTOR;
	ciab.prb |= (SELMASK(0)|SELMASK(1)|SELMASK(2)|SELMASK(3));
	ciab.prb = prb;
	prb &= ~SELMASK(drive);
	ciab.prb = prb;
}

static void fd_deselect (int drive)
{
	unsigned char prb;
	unsigned long flags;

	drive&=3;
	if (drive != selected)
		return;

	save_flags (flags);
	sti();

	selected = -1;

	prb = ciab.prb;
	prb |= (SELMASK(0)|SELMASK(1)|SELMASK(2)|SELMASK(3));
	ciab.prb = prb;

	restore_flags (flags);

}

/*======================================================================
  Seek the drive to track 0.
  The drive must be active and the motor must be running.
  Returns standard floppy error code.
======================================================================*/
static int fd_calibrate(int drive)
{
	unsigned char prb;
	int n;

	drive &= 3;
	if (!motor_on (drive))
		return 0;
	fd_select (drive);
	prb = ciab.prb;
	prb |= DSKSIDE;
	prb &= ~DSKDIREC;
	ciab.prb = prb;
	for (n = unit[drive].type->tracks/4; n != 0; --n) {
		if (ciaa.pra & DSKTRACK0)
			break;
		prb &= ~DSKSTEP;
		ciab.prb = prb;
		prb |= DSKSTEP;
		ms_delay (2);
		ciab.prb = prb;
		ms_delay(unit[drive].type->step_delay);
	}
	ms_delay (unit[drive].type->settle_time);
	prb |= DSKDIREC;
	n = unit[drive].type->tracks/2 + 20;
	for (;;) {
		prb &= ~DSKSTEP;
		ciab.prb = prb;
		prb |= DSKSTEP;
		ms_delay (2);
		ciab.prb = prb;
		ms_delay(unit[drive].type->step_delay + 1);
		if ((ciaa.pra & DSKTRACK0) == 0)
			break;
		if (--n == 0) {
			printk ("calibrate failed, turning motor off\n");
			fd_motor_off (drive);
			unit[drive].track = -1;
			return 0;
		}
	}
	unit[drive].track = 0;
	ms_delay(unit[drive].type->settle_time);

	return 1;
}

/*======================================================================
  Seek the drive to the requested cylinder.
  The drive must have been calibrated at some point before this.
  The drive must also be active and the motor must be running.
======================================================================*/
static int fd_seek(int drive, int track)
{
	unsigned char prb;
	int cnt;

	drive &= 3;
	if (unit[drive].track == track)
		return 1;
	if (!motor_on(drive))
		return 0;
	fd_select (drive);
	if (unit[drive].track < 0 && !fd_calibrate(drive))
		return 0;

	cnt = unit[drive].track/2 - track/2;
	prb = ciab.prb;
	prb |= DSKSIDE | DSKDIREC;
	if (track % 2 != 0)
		prb &= ~DSKSIDE;
	if (cnt < 0) {
		cnt = - cnt;
		prb &= ~DSKDIREC;
	}
	ciab.prb = prb;
	if (track % 2 != unit[drive].track % 2)
		ms_delay (unit[drive].type->side_time);
	unit[drive].track = track;
	if (cnt == 0)
		return 1;
	do {
		prb &= ~DSKSTEP;
		ciab.prb = prb;
		prb |= DSKSTEP;
		ms_delay (1);
		ciab.prb = prb;
		ms_delay (unit[drive].type->step_delay);
	} while (--cnt != 0);
	ms_delay (unit[drive].type->settle_time);

	return 1;
}

static void encode(unsigned long data, unsigned long *dest)
{
  unsigned long data2;

  data &= 0x55555555;
  data2 = data ^ 0x55555555;
  data |= ((data2 >> 1) | 0x80000000) & (data2 << 1);

  if (*(dest - 1) & 0x00000001)
    data &= 0x7FFFFFFF;

  *dest = data;
}

static void encode_block(unsigned long *dest, unsigned long *src, int len)
{
  int cnt, to_cnt = 0;
  unsigned long data;

  /* odd bits */
  for (cnt = 0; cnt < len / 4; cnt++) {
    data = src[cnt] >> 1;
    encode(data, dest + to_cnt++);
  }

  /* even bits */
  for (cnt = 0; cnt < len / 4; cnt++) {
    data = src[cnt];
    encode(data, dest + to_cnt++);
  }
}

unsigned long checksum(unsigned long *addr, int len)
{
	unsigned long csum = 0;

	len /= sizeof(*addr);
	while (len-- > 0)
		csum ^= *addr++;
	csum = ((csum>>1) & 0x55555555)  ^  (csum & 0x55555555);

	return csum;
}

struct header {
	unsigned char magic;
	unsigned char track;
	unsigned char sect;
	unsigned char ord;
	unsigned char labels[16];
	unsigned long hdrchk;
	unsigned long datachk;
};

static unsigned long *putsec(int disk, unsigned long *raw, int track, int cnt,
			     unsigned char *data)
{
	struct header hdr;
	int i;

	if (!AMIGAHW_PRESENT(AMI_FLOPPY))
	    return 0;

	disk&=3;
	*raw = (raw[-1]&1) ? 0x2AAAAAAA : 0xAAAAAAAA;
	raw++;
	*raw++ = 0x44894489;

	hdr.magic = 0xFF;
	hdr.track = track;
	hdr.sect = cnt;
	hdr.ord = unit[disk].sects-cnt;
	for (i = 0; i < 16; i++)
		hdr.labels[i] = 0;
	hdr.hdrchk = checksum((ulong *)&hdr,
			      (char *)&hdr.hdrchk-(char *)&hdr);
	hdr.datachk = checksum((ulong *)data, 512);

	encode_block(raw, (ulong *)&hdr.magic, 4);
	raw += 2;
	encode_block(raw, (ulong *)&hdr.labels, 16);
	raw += 8;
	encode_block(raw, (ulong *)&hdr.hdrchk, 4);
	raw += 2;
	encode_block(raw, (ulong *)&hdr.datachk, 4);
	raw += 2;
	encode_block(raw, (ulong *)data, 512);
	raw += 256;

	return raw;
}


/*==========================================================================
  amiga_write converts track/labels data to raw track data
==========================================================================*/
static void amiga_write(int disk, unsigned long raw, unsigned char *data,
			int track)
{
	int cnt;
	unsigned long *ptr = (unsigned long *)raw;

	disk&=3;
	/* gap space */
	for (cnt = 0; cnt < 415 * unit[disk].type->sect_mult; cnt++)
		*ptr++ = 0xaaaaaaaa;

	/* sectors */
	for (cnt = 0; cnt < unit[disk].sects; cnt++)
		ptr = putsec (disk, ptr, track, cnt, data + cnt*512);
	*(ushort *)ptr = (ptr[-1]&1) ? 0x2AA8 : 0xAAA8;
	raw = (unsigned long)ptr + 2;
}

static unsigned long decode (unsigned long *data, unsigned long *raw,
				   int len)
{
	ulong *odd, *even;

	/* convert length from bytes to longwords */
	len >>= 2;
	odd = raw;
	even = odd + len;

	/* prepare return pointer */
	raw += len * 2;

	do {
		*data++ = ((*odd++ & 0x55555555) << 1) | (*even++ & 0x55555555);
	} while (--len != 0);

	return (ulong)raw;
}

#define MFM_NOSYNC	1
#define MFM_HEADER	2
#define MFM_DATA	3
#define MFM_TRACK	4

/*==========================================================================
 scan_sync - looks for the next start of sector marked by a sync. d3 is the
		sector number (10..0). When d3 = 10, can't be certain of a
		starting sync.
==========================================================================*/
static unsigned long scan_sync(unsigned long raw, unsigned long end)
{
	ushort *ptr = (ushort *)raw, *endp = (ushort *)end;

	while (ptr < endp && *ptr++ != 0x4489)
		;
	if (ptr < endp) {
		while (*ptr == 0x4489 && ptr < endp)
			ptr++;
		return (ulong)ptr;
	}
	return 0;
}

/*==========================================================================
  amiga_read reads a raw track of data into a track buffer
==========================================================================*/
static int amiga_read(int drive, unsigned char *track_data,
		      unsigned long raw, int track)
{
	unsigned long end;
	int scnt;
	unsigned long csum;
	struct header hdr;

	drive&=3;
	end = raw + unit[drive].type->read_size;

	for (scnt = 0;scnt < unit[drive].sects; scnt++) {
		if (!(raw = scan_sync(raw, end))) {
			printk ("can't find sync for sector %d\n", scnt);
			return MFM_NOSYNC;
		}

		raw = decode ((ulong *)&hdr.magic, (ulong *)raw, 4);
		raw = decode ((ulong *)&hdr.labels, (ulong *)raw, 16);
		raw = decode ((ulong *)&hdr.hdrchk, (ulong *)raw, 4);
		raw = decode ((ulong *)&hdr.datachk, (ulong *)raw, 4);
		csum = checksum((ulong *)&hdr,
				(char *)&hdr.hdrchk-(char *)&hdr);

#ifdef DEBUG
		printk ("(%x,%d,%d,%d) (%lx,%lx,%lx,%lx) %lx %lx\n",
			hdr.magic, hdr.track, hdr.sect, hdr.ord,
			*(ulong *)&hdr.labels[0], *(ulong *)&hdr.labels[4],
			*(ulong *)&hdr.labels[8], *(ulong *)&hdr.labels[12],
			hdr.hdrchk, hdr.datachk);
#endif

		if (hdr.hdrchk != csum) {
			printk("MFM_HEADER: %08lx,%08lx\n", hdr.hdrchk, csum);
			return MFM_HEADER;
		}

		/* verify track */
		if (hdr.track != track) {
			printk("MFM_TRACK: %d, %d\n", hdr.track, track);
			return MFM_TRACK;
		}

		raw = decode ((ulong *)(track_data + hdr.sect*512),
			      (ulong *)raw, 512);
		csum = checksum((ulong *)(track_data + hdr.sect*512), 512);

		if (hdr.datachk != csum) {
			printk("MFM_DATA: (%x:%d:%d:%d) sc=%d %lx, %lx\n",
			       hdr.magic, hdr.track, hdr.sect, hdr.ord, scnt,
			       hdr.datachk, csum);
			printk ("data=(%lx,%lx,%lx,%lx)\n",
				((ulong *)(track_data+hdr.sect*512))[0],
				((ulong *)(track_data+hdr.sect*512))[1],
				((ulong *)(track_data+hdr.sect*512))[2],
				((ulong *)(track_data+hdr.sect*512))[3]);
			return MFM_DATA;
		}
	}

	return 0;
}

struct dos_header {
unsigned char track,   /* 0-80 */
              side,    /* 0-1 */
              sec,     /* 0-...*/
              len_desc;/* 2 */
unsigned short crc;     /* on 68000 we got an alignment problem, 
                           but this compiler solves it  by adding silently 
                           adding a pad byte so data won't fit
                           and this cost about 3h to discover.... */
unsigned char gap1[22];     /* for longword-alignedness (0x4e) */
};

/* crc routines are borrowed from the messydos-handler  */

static inline ushort dos_hdr_crc (struct dos_header *hdr)
{
return dos_crc(&(hdr->track), 0xb2, 0x30, 3); /* precomputed magic */
}

static inline ushort dos_data_crc(unsigned char *data)
{
return dos_crc(data, 0xe2, 0x95 ,511); /* precomputed magic */
}

/* excerpt from the messydos-device           
; The CRC is computed not only over the actual data, but including
; the SYNC mark (3 * $a1) and the 'ID/DATA - Address Mark' ($fe/$fb).
; As we don't read or encode these fields into our buffers, we have to
; preload the registers containing the CRC with the values they would have
; after stepping over these fields.
;
; How CRCs "really" work:
;
; First, you should regard a bitstring as a series of coefficients of
; polynomials. We calculate with these polynomials in modulo-2
; arithmetic, in which both add and subtract are done the same as
; exclusive-or. Now, we modify our data (a very long polynomial) in
; such a way that it becomes divisible by the CCITT-standard 16-bit
;		 16   12   5
; polynomial:	x  + x	+ x + 1, represented by $11021. The easiest
; way to do this would be to multiply (using proper arithmetic) our
; datablock with $11021. So we have:
;   data * $11021		 =
;   data * ($10000 + $1021)      =
;   data * $10000 + data * $1021
; The left part of this is simple: Just add two 0 bytes. But then
; the right part (data $1021) remains difficult and even could have
; a carry into the left part. The solution is to use a modified
; multiplication, which has a result that is not correct, but with
; a difference of any multiple of $11021. We then only need to keep
; the 16 least significant bits of the result.
;
; The following algorithm does this for us:
;
;   unsigned char *data, c, crclo, crchi;
;   while (not done) {
;	c = *data++ + crchi;
;	crchi = (@ c) >> 8 + crclo;
;	crclo = @ c;
;   }
;
; Remember, + is done with EOR, the @ operator is in two tables (high
; and low byte separately), which is calculated as
;
;      $1021 * (c & $F0)
;  xor $1021 * (c & $0F)
;  xor $1021 * (c >> 4)         (* is regular multiplication)
;
;
; Anyway, the end result is the same as the remainder of the division of
; the data by $11021. I am afraid I need to study theory a bit more...


my only works was to code this from manx to C....

*/

static ushort dos_crc(void * data_a3, int data_d0, int data_d1, int data_d3)
{
static unsigned char CRCTable1[] = {
	0x00,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x81,0x91,0xa1,0xb1,0xc1,0xd1,0xe1,0xf1,
	0x12,0x02,0x32,0x22,0x52,0x42,0x72,0x62,0x93,0x83,0xb3,0xa3,0xd3,0xc3,0xf3,0xe3,
	0x24,0x34,0x04,0x14,0x64,0x74,0x44,0x54,0xa5,0xb5,0x85,0x95,0xe5,0xf5,0xc5,0xd5,
	0x36,0x26,0x16,0x06,0x76,0x66,0x56,0x46,0xb7,0xa7,0x97,0x87,0xf7,0xe7,0xd7,0xc7,
	0x48,0x58,0x68,0x78,0x08,0x18,0x28,0x38,0xc9,0xd9,0xe9,0xf9,0x89,0x99,0xa9,0xb9,
	0x5a,0x4a,0x7a,0x6a,0x1a,0x0a,0x3a,0x2a,0xdb,0xcb,0xfb,0xeb,0x9b,0x8b,0xbb,0xab,
	0x6c,0x7c,0x4c,0x5c,0x2c,0x3c,0x0c,0x1c,0xed,0xfd,0xcd,0xdd,0xad,0xbd,0x8d,0x9d,
	0x7e,0x6e,0x5e,0x4e,0x3e,0x2e,0x1e,0x0e,0xff,0xef,0xdf,0xcf,0xbf,0xaf,0x9f,0x8f,
	0x91,0x81,0xb1,0xa1,0xd1,0xc1,0xf1,0xe1,0x10,0x00,0x30,0x20,0x50,0x40,0x70,0x60,
	0x83,0x93,0xa3,0xb3,0xc3,0xd3,0xe3,0xf3,0x02,0x12,0x22,0x32,0x42,0x52,0x62,0x72,
	0xb5,0xa5,0x95,0x85,0xf5,0xe5,0xd5,0xc5,0x34,0x24,0x14,0x04,0x74,0x64,0x54,0x44,
	0xa7,0xb7,0x87,0x97,0xe7,0xf7,0xc7,0xd7,0x26,0x36,0x06,0x16,0x66,0x76,0x46,0x56,
	0xd9,0xc9,0xf9,0xe9,0x99,0x89,0xb9,0xa9,0x58,0x48,0x78,0x68,0x18,0x08,0x38,0x28,
	0xcb,0xdb,0xeb,0xfb,0x8b,0x9b,0xab,0xbb,0x4a,0x5a,0x6a,0x7a,0x0a,0x1a,0x2a,0x3a,
	0xfd,0xed,0xdd,0xcd,0xbd,0xad,0x9d,0x8d,0x7c,0x6c,0x5c,0x4c,0x3c,0x2c,0x1c,0x0c,
	0xef,0xff,0xcf,0xdf,0xaf,0xbf,0x8f,0x9f,0x6e,0x7e,0x4e,0x5e,0x2e,0x3e,0x0e,0x1e
};

static unsigned char CRCTable2[] = {
	0x00,0x21,0x42,0x63,0x84,0xa5,0xc6,0xe7,0x08,0x29,0x4a,0x6b,0x8c,0xad,0xce,0xef,
	0x31,0x10,0x73,0x52,0xb5,0x94,0xf7,0xd6,0x39,0x18,0x7b,0x5a,0xbd,0x9c,0xff,0xde,
	0x62,0x43,0x20,0x01,0xe6,0xc7,0xa4,0x85,0x6a,0x4b,0x28,0x09,0xee,0xcf,0xac,0x8d,
	0x53,0x72,0x11,0x30,0xd7,0xf6,0x95,0xb4,0x5b,0x7a,0x19,0x38,0xdf,0xfe,0x9d,0xbc,
	0xc4,0xe5,0x86,0xa7,0x40,0x61,0x02,0x23,0xcc,0xed,0x8e,0xaf,0x48,0x69,0x0a,0x2b,
	0xf5,0xd4,0xb7,0x96,0x71,0x50,0x33,0x12,0xfd,0xdc,0xbf,0x9e,0x79,0x58,0x3b,0x1a,
	0xa6,0x87,0xe4,0xc5,0x22,0x03,0x60,0x41,0xae,0x8f,0xec,0xcd,0x2a,0x0b,0x68,0x49,
	0x97,0xb6,0xd5,0xf4,0x13,0x32,0x51,0x70,0x9f,0xbe,0xdd,0xfc,0x1b,0x3a,0x59,0x78,
	0x88,0xa9,0xca,0xeb,0x0c,0x2d,0x4e,0x6f,0x80,0xa1,0xc2,0xe3,0x04,0x25,0x46,0x67,
	0xb9,0x98,0xfb,0xda,0x3d,0x1c,0x7f,0x5e,0xb1,0x90,0xf3,0xd2,0x35,0x14,0x77,0x56,
	0xea,0xcb,0xa8,0x89,0x6e,0x4f,0x2c,0x0d,0xe2,0xc3,0xa0,0x81,0x66,0x47,0x24,0x05,
	0xdb,0xfa,0x99,0xb8,0x5f,0x7e,0x1d,0x3c,0xd3,0xf2,0x91,0xb0,0x57,0x76,0x15,0x34,
	0x4c,0x6d,0x0e,0x2f,0xc8,0xe9,0x8a,0xab,0x44,0x65,0x06,0x27,0xc0,0xe1,0x82,0xa3,
	0x7d,0x5c,0x3f,0x1e,0xf9,0xd8,0xbb,0x9a,0x75,0x54,0x37,0x16,0xf1,0xd0,0xb3,0x92,
	0x2e,0x0f,0x6c,0x4d,0xaa,0x8b,0xe8,0xc9,0x26,0x07,0x64,0x45,0xa2,0x83,0xe0,0xc1,
	0x1f,0x3e,0x5d,0x7c,0x9b,0xba,0xd9,0xf8,0x17,0x36,0x55,0x74,0x93,0xb2,0xd1,0xf0
};

/* look at the asm-code - what looks in C a bit strange is almost as good as handmade */
register int i;
register unsigned char *CRCT1, *CRCT2, *data, c, crch, crcl;

CRCT1=CRCTable1;
CRCT2=CRCTable2;
data=data_a3;
crcl=data_d1;
crch=data_d0;
for (i=data_d3; i>=0; i--) {
  c = (*data++) ^ crch;
  crch = CRCT1[c] ^ crcl;
  crcl = CRCT2[c];
}
return (crch<<8)|crcl;
}

static inline unsigned char dos_decode_byte(ushort word)
{
register ushort w2;
register unsigned char byte;
register unsigned char *dec = mfmdecode;

w2=word;
w2>>=8;
w2&=127;
byte = dec[w2];
byte <<= 4;
w2 = word & 127;
byte |= dec[w2];
return byte;
}

static unsigned long dos_decode(unsigned char *data, unsigned short *raw, int len)
{
int i;

for (i = 0; i < len; i++)
  *data++=dos_decode_byte(*raw++);
return ((ulong)raw);
}

#ifdef DEBUG
static void dbg(unsigned long ptr)
{
printk("raw data @%08lx: %08lx, %08lx ,%08lx, %08lx\n",ptr,
  ((ulong *)ptr)[0],((ulong *)ptr)[1],((ulong *)ptr)[2],((ulong *)ptr)[3]);
}
#endif

/*******************************************************************
   this reads a raw track of data into trackbuffer for ms-disks 
*******************************************************************/
static int dos_read(int drive, unsigned char *track_data,
	unsigned long raw, int track)
{
  unsigned long end;
  int scnt;
  unsigned short crc,data_crc[2];
  struct dos_header hdr;

  drive&=3;
  end = raw + unit[drive].type->read_size;

  for (scnt=0;scnt<unit[drive].sects;scnt++) {
  do { /* search for the right sync of each sec-hdr */
    if (!(raw = scan_sync (raw, end))) {
      printk("dos_read: no hdr sync on track %d, unit %d for sector %d\n",
        track,drive,scnt);
      return MFM_NOSYNC;
    }
#ifdef DEBUG
  dbg(raw);
#endif
  } while (*((ushort *)raw)!=0x5554); /* loop usually only once done */
  raw+=2; /* skip over headermark */
  raw = dos_decode((unsigned char *)&hdr,(ushort *) raw,8);
  crc = dos_hdr_crc(&hdr);

#ifdef DEBUG
  printk("(%3d,%d,%2d,%d) %x\n", hdr.track, hdr.side,
     hdr.sec, hdr.len_desc, hdr.crc);
#endif

  if (crc != hdr.crc) {
    printk("dos_read: MFM_HEADER %04x,%04x\n", hdr.crc, crc);
    return MFM_HEADER;
  }
  if (hdr.track != track/unit[drive].type->heads) {
    printk("dos_read: MFM_TRACK %d, %d\n", hdr.track,
      track/unit[drive].type->heads);
    return MFM_TRACK;
  }

  if (hdr.side != track%unit[drive].type->heads) {
    printk("dos_read: MFM_SIDE %d, %d\n", hdr.side,
      track%unit[drive].type->heads);
    return MFM_TRACK;
  }

  if (hdr.len_desc != 2) {
    printk("dos_read: unknown sector len descriptor %d\n", hdr.len_desc);
    return MFM_DATA;
  }
#ifdef DEBUG
  printk("hdr accepted\n");
#endif
  if (!(raw = scan_sync (raw, end))) {
    printk("dos_read: no data sync on track %d, unit %d for sector%d, disk sector %d\n",
      track, drive, scnt, hdr.sec);
    return MFM_NOSYNC;
  }
#ifdef DEBUG
  dbg(raw);
#endif

  if (*((ushort *)raw)!=0x5545) {
    printk("dos_read: no data mark after sync (%d,%d,%d,%d) sc=%d\n",
      hdr.track,hdr.side,hdr.sec,hdr.len_desc,scnt);
    return MFM_NOSYNC;
  }

  raw+=2;  /* skip data mark (included in checksum) */
  raw = dos_decode((unsigned char *)(track_data + (hdr.sec - 1) * 512), (ushort *) raw, 512);
  raw = dos_decode((unsigned char  *)data_crc,(ushort *) raw,4);
  crc = dos_data_crc(track_data + (hdr.sec - 1) * 512);

  if (crc != data_crc[0]) {
    printk("dos_read: MFM_DATA (%d,%d,%d,%d) sc=%d, %x %x\n",
      hdr.track, hdr.side, hdr.sec, hdr.len_desc,
      scnt,data_crc[0], crc);
    printk("data=(%lx,%lx,%lx,%lx,...)\n",
      ((ulong *)(track_data+(hdr.sec-1)*512))[0],
      ((ulong *)(track_data+(hdr.sec-1)*512))[1],
      ((ulong *)(track_data+(hdr.sec-1)*512))[2],
      ((ulong *)(track_data+(hdr.sec-1)*512))[3]);
    return MFM_DATA;
  }
  }
 return 0;
}

static inline ushort dos_encode_byte(unsigned char byte)
{
register unsigned char *enc, b2, b1;
register ushort word;

enc=mfmencode;
b1=byte;
b2=b1>>4;
b1&=15;
word=enc[b2] <<8 | enc [b1];
return (word|((word&(256|64)) ? 0: 128));
}

static void dos_encode_block(ushort *dest, unsigned char *src, int len)
{
int i;

for (i = 0; i < len; i++) {
  *dest=dos_encode_byte(*src++);
  *dest|=((dest[-1]&1)||(*dest&0x4000))? 0: 0x8000;
  dest++;
}
}

static unsigned long *ms_putsec(int drive, unsigned long *raw, int track, int cnt,
   unsigned char *data)
{
static struct dos_header hdr={0,0,0,2,0,
  {78,78,78,78,78,78,78,78,78,78,78,78,78,78,78,78,78,78,78,78,78,78}};
int i;
static ushort crc[2]={0,0x4e4e};

drive&=3;
/* id gap 1 */
/* the MFM word before is always 9254 */
for(i=0;i<6;i++)
  *raw++=0xaaaaaaaa;
/* 3 sync + 1 headermark */
*raw++=0x44894489;
*raw++=0x44895554;

/* fill in the variable parts of the header */
hdr.track=track/unit[drive].type->heads;
hdr.side=track%unit[drive].type->heads;
hdr.sec=cnt+1;
hdr.crc=dos_hdr_crc(&hdr);

/* header (without "magic") and id gap 2*/
dos_encode_block((ushort *)raw,(unsigned char *) &hdr.track,28);
raw+=14;

/*id gap 3 */
for(i=0;i<6;i++)
  *raw++=0xaaaaaaaa;

/* 3 syncs and 1 datamark */
*raw++=0x44894489;
*raw++=0x44895545;

/* data */
dos_encode_block((ushort *)raw,(unsigned char *)data,512);
raw+=256;

/*data crc + jd's special gap (long words :-/) */
crc[0]=dos_data_crc(data);
dos_encode_block((ushort *) raw,(unsigned char *)crc,4);
raw+=2;

/* data gap */
for(i=0;i<38;i++)
  *raw++=0x92549254;

return raw; /* wrote 652 MFM words */
}


/**************************************************************
  builds encoded track data from trackbuffer data
**************************************************************/
static void dos_write(int disk, unsigned long raw, unsigned char *data,
    int track)
{
int cnt;
unsigned long *ptr=(unsigned long *)raw;

disk&=3;
/* really gap4 + indexgap , but we write it first and round it up */
for (cnt=0;cnt<425;cnt++)
  *ptr++=0x92549254;

/* the following is just guessed */
if (unit[disk].type->sect_mult==2)  /* check for HD-Disks */
  for(cnt=0;cnt<473;cnt++)
    *ptr++=0x92549254;

/* now the index marks...*/
for (cnt=0;cnt<20;cnt++)
  *ptr++=0x92549254;
for (cnt=0;cnt<6;cnt++)
  *ptr++=0xaaaaaaaa;
*ptr++=0x52245224;
*ptr++=0x52245552;
for (cnt=0;cnt<20;cnt++)
  *ptr++=0x92549254;

/* sectors */
for(cnt=0;cnt<unit[disk].sects;cnt++)
  ptr=ms_putsec(disk,ptr,track,cnt,data+cnt*512);

*(ushort *)ptr = 0xaaa8; /* MFM word before is always 0x9254 */
}

/*
 * Note that MAX_ERRORS=X doesn't imply that we retry every bad read
 * max X times - some types of errors increase the errorcount by 2 or
 * even 3, so we might actually retry only X/2 times before giving up.
 */
#define MAX_ERRORS 12

/*
 * The driver is trying to determine the correct media format
 * while probing is set. rw_interrupt() clears it after a
 * successful access.
 */
static int probing = 0;

/* Prevent "aliased" accesses. */
static fd_ref[4] = { 0,0,0,0 };
static fd_device[4] = { 0,0,0,0 };

/*
 * Current device number. Taken either from the block header or from the
 * format request descriptor.
 */
#define CURRENT_DEVICE (CURRENT->rq_dev)

/* Current error count. */
#define CURRENT_ERRORS (CURRENT->errors)

static void request_done(int uptodate)
{
  timer_active &= ~(1 << FLOPPY_TIMER);
  end_request(uptodate);
}

/*
 * floppy-change is never called from an interrupt, so we can relax a bit
 * here, sleep etc. Note that floppy-on tries to set current_DOR to point
 * to the desired drive, but it will probably not survive the sleep if
 * several floppies are used at the same time: thus the loop.
 */
static int amiga_floppy_change(kdev_t dev)
{
	int drive = dev & 3;
	int changed;

	if (MAJOR(dev) != MAJOR_NR) {
		printk("floppy_change: not a floppy\n");
		return 0;
	}

	fd_select (drive);
	changed = !(ciaa.pra & DSKCHANGE);
	fd_deselect (drive);

	if (changed) {
		fd_probe(dev);
		unit[drive].track = -1;
		selected = -1;
		savedtrack = -1;
		writepending = 0; /* if this was true before, too bad! */
		writefromint = 0;
		return 1;
	}
	return 0;
}

static __inline__ void copy_buffer(void *from, void *to)
{
  ulong *p1,*p2;
  int cnt;

  p1 = (ulong *)from;
  p2 = (ulong *)to;

  for (cnt = 0; cnt < 512/4; cnt++)
    *p2++ = *p1++;
}

static void raw_read(int drive, int track, char *ptrack, int len)
{
	drive&=3;
	/* setup adkcon bits correctly */
	custom.adkcon = ADK_MSBSYNC;
	custom.adkcon = ADK_SETCLR|ADK_WORDSYNC|ADK_FAST;

	custom.dsksync = MFM_SYNC;

	custom.dsklen = 0;
#if 0
	ms_delay (unit[drive].type->side_time);
#endif
	custom.dskptr = (u_char *)ZTWO_PADDR((u_char *)ptrack);
	custom.dsklen = len/sizeof(short) | DSKLEN_DMAEN;
	custom.dsklen = len/sizeof(short) | DSKLEN_DMAEN;

	block_flag = 1;

	while (block_flag == 1)
		sleep_on (&wait_fd_block);

	custom.dsklen = 0;
}

static int raw_write(int drive, int track, char *ptrack, int len)
{
	ushort adk;

	drive&=3;
	if ((ciaa.pra & DSKPROT) == 0)
		return 0;

	/* clear adkcon bits */
	custom.adkcon = ADK_PRECOMP1|ADK_PRECOMP0|ADK_WORDSYNC|ADK_MSBSYNC;
	/* set appropriate adkcon bits */
	adk = ADK_SETCLR|ADK_FAST;
	if ((ulong)track >= unit[drive].type->precomp2)
		adk |= ADK_PRECOMP1;
	else if ((ulong)track >= unit[drive].type->precomp1)
		adk |= ADK_PRECOMP0;
	custom.adkcon = adk;

	custom.dsklen = DSKLEN_WRITE;
#if 0
	ms_delay (unit[drive].type->side_time);
#endif
	custom.dskptr = (u_char *)ZTWO_PADDR((u_char *)ptrack);
	custom.dsklen = len/sizeof(short) | DSKLEN_DMAEN|DSKLEN_WRITE;
	custom.dsklen = len/sizeof(short) | DSKLEN_DMAEN|DSKLEN_WRITE;

	block_flag = 2;
	return 1;
}

static void post_write (unsigned long dummy)
{
  custom.dsklen = 0;
  writepending = 0;
  writefromint = 0;
}

static int get_track(int drive, int track)
{
	int error;

	drive&=3;
	if ((lastdrive == drive) && (savedtrack == track))
		return 0;

	lastdrive = drive;
	raw_read(drive, track, raw_buf, unit[drive].type->read_size);
	savedtrack = -1;
	error = (*unit[drive].dtype->read_fkt)(drive, trackdata, (unsigned long)raw_buf, track);
	switch (error) {
	    case 0:
		savedtrack = track;
		return 0;
	    case MFM_TRACK:
		unit[drive].track = -1;
		/* fall through */
	    default:
		return -1;
	}
}

static void flush_track_callback(unsigned long nr)
{
  nr&=3;
  writefromint = 1;
  (*unit[nr].dtype->write_fkt)(nr, (unsigned long)raw_buf, trackdata, savedtrack);
  if (!raw_write(nr, savedtrack, raw_buf, unit[nr].type->write_size)) {
    printk ("floppy disk write protected\n");
    writefromint = 0;
    writepending = 0;
  }
}

static int non_int_flush_track (unsigned long nr)
{
unsigned long flags;

  nr&=3;
  writefromint = 0;
  del_timer(&post_write_timer);
  save_flags(flags);
  cli();
  if (writepending != 2) {
    restore_flags(flags);
    (*unit[nr].dtype->write_fkt)(nr, (unsigned long)raw_buf, trackdata, savedtrack);
    if (!raw_write(nr, savedtrack, raw_buf, unit[nr].type->write_size)) {
      printk ("floppy disk write protected in write!\n");
      writepending = 0;
      return 0;
    }
    while (block_flag == 2)
      sleep_on (&wait_fd_block);
  }
  else
    restore_flags(flags);
  ms_delay(2); /* 2 ms post_write delay */
  post_write(0);
  return 1;
}

static void redo_fd_request(void)
{
	unsigned int block, track, sector;
	int device, drive, cnt;
	struct amiga_floppy_struct *floppy;
	char *data;
	unsigned long flags;

	if (CURRENT && CURRENT->rq_status == RQ_INACTIVE){
		return;
	}

    repeat:
	if (!CURRENT) {
		if (!fdc_busy)
			printk("FDC access conflict!");
		fdc_busy = 0;
		wake_up(&fdc_wait);
		CLEAR_INTR;
		return;
	}

	if (MAJOR(CURRENT->rq_dev) != MAJOR_NR)
		panic(DEVICE_NAME ": request list destroyed");

	if (CURRENT->bh && !buffer_locked(CURRENT->bh))
		panic(DEVICE_NAME ": block not locked");

	probing = 0;
	device = MINOR(CURRENT_DEVICE);
	if (device > 3) {
		/* manual selection */
		drive = device & 3;
		floppy = unit + drive;
	} else {
		/* Auto-detection */
		/* printk("redo_fd_request: can't handle auto detect\n");*/
		/* printk("redo_fd_request: default to normal\n");*/
		drive = device & 3;
		floppy = unit + drive;
	}

	save_flags (flags);
	cli();
	if (drive != selected && writepending) {
	  del_timer (&flush_track_timer);
	  restore_flags (flags);
	  if (!non_int_flush_track (selected)) {
	    end_request(0);
	    goto repeat;
	  }
	} else
	  restore_flags (flags);

 /* Here someone could investigate to be more efficient */
	for (cnt = 0; cnt < CURRENT->current_nr_sectors; cnt++) { 
#ifdef DEBUG
		printk("fd: sector %d + %d requested\n",CURRENT->sector,cnt);
#endif
		block = CURRENT->sector + cnt;
		if ((int)block > floppy->blocks) {
			request_done(0);
			goto repeat;
		}

		track = block / floppy->sects;
		sector = block % floppy->sects;
		data = CURRENT->buffer + 512 * cnt;

		save_flags (flags);
		cli();
		if (track != savedtrack && writepending) {
		  del_timer (&flush_track_timer);
		  restore_flags (flags);
		  if (!non_int_flush_track (selected)) {
		    end_request(0);
		    goto repeat;
		  }
		} else
		  restore_flags (flags);

		switch (CURRENT->cmd) {
		    case READ:
			if (!motor_on (drive)) {
				end_request(0);
				goto repeat;
			}
			fd_select (drive);
			if (!fd_seek(drive, track)) {
				end_request(0);
				goto repeat;
			}
			if (get_track(drive, track) == -1) {
				end_request(0);
				goto repeat;
			}
			copy_buffer(trackdata + sector * 512, data);
			break;

		    case WRITE:
			if (!motor_on (drive)) {
				end_request(0);
				goto repeat;
			}
			fd_select (drive);
			if (!fd_seek(drive, track)) {
				end_request(0);
				goto repeat;
			}
			if (get_track(drive, track) == -1) {
				end_request(0);
				goto repeat;
			}
			copy_buffer(data, trackdata + sector * 512);
			/*
			 * setup a callback to write the track buffer
			 * after a short (1 tick) delay.
			 */
			save_flags (flags);
			cli();

			if (writepending)
			    /* reset the timer */
			    del_timer (&flush_track_timer);
			    
			writepending = 1;
			flush_track_timer.data = drive;
			flush_track_timer.expires = jiffies + 1;
			add_timer (&flush_track_timer);
			restore_flags (flags);
			break;

		    default:
			printk("do_fd_request: unknown command\n");
			request_done(0);
			goto repeat;
		}
	}
	CURRENT->nr_sectors -= CURRENT->current_nr_sectors;
	CURRENT->sector += CURRENT->current_nr_sectors;

	request_done(1);
	goto repeat;
}

static void do_fd_request(void)
{
unsigned long flags;

	save_flags(flags);
	cli();
	while (fdc_busy) sleep_on(&fdc_wait);
	fdc_busy = 1;
	restore_flags(flags); /* sti(); */
	redo_fd_request();
}

static int fd_ioctl(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long param)
{
	int drive = inode->i_rdev & 3;
	static struct floppy_struct getprm;
	int error;

	switch(cmd)
	 {
	  case FDFMTBEG:
	    if (fd_ref[drive] > 1)
	      return -EBUSY;
	    fsync_dev(inode->i_rdev);
	    if (motor_on(drive) == 0)
	      return -ENODEV;
	    if (fd_calibrate(drive) == 0)
	      return -ENXIO;
	    floppy_off(drive);
	    break;
	  case FDFMTTRK:
	    if (param < unit[drive].type->tracks)
	     {
	      fd_select(drive);
	      if (fd_seek(drive,param)!=0)
	       {
	        savedtrack=param;
	        memset(trackdata,FD_FILL_BYTE,unit[drive].sects*512);
	        non_int_flush_track(drive);
	       }
	      floppy_off(drive);
	     }
	    else
	      return -EINVAL;
	    break;
	  case FDFMTEND:
	    floppy_off(drive);
	    invalidate_inodes(inode->i_rdev);
	    invalidate_buffers(inode->i_rdev);
	    break;
	  case FDGETPRM:
	    error = verify_area(VERIFY_WRITE, (void *)param,
				sizeof(struct floppy_struct));
	    if (error)
	      return error;
	    memset((void *)&getprm, 0, sizeof (getprm));
	    getprm.track=unit[drive].type->tracks/unit[drive].type->heads;
	    getprm.head=unit[drive].type->heads;
	    getprm.sect=unit[drive].sects;
	    getprm.size=unit[drive].blocks;
	    memcpy_tofs((void *)param,(void *)&getprm,sizeof(struct floppy_struct));
	    break;
	  case BLKGETSIZE:
            error = verify_area(VERIFY_WRITE, (void *)param,
            			sizeof(long));
            if (error)
              return error;
            put_fs_long(unit[drive].blocks,(long *)param);
            break;
          case FDSETPRM:
          case FDDEFPRM:
            return -EINVAL;
          case FDFLUSH:
            if ((drive == selected) && (writepending)) {
              del_timer (&flush_track_timer);
              non_int_flush_track(selected);
            }
            break;
#ifdef RAW_IOCTL
	  case IOCTL_RAW_TRACK:
	    error = verify_area(VERIFY_WRITE, (void *)param,
	      unit[drive].type->read_size);
	    if (error)
	      return error;
	    memcpy_tofs((void *)param, raw_buf, unit[drive].type->read_size);
	    return unit[drive].type->read_size;
#endif
	  default:
	    printk("fd_ioctl: unknown cmd %d for drive %d.",cmd,drive);
	    return -ENOSYS;
         }
	return 0;
}

/*======================================================================
  Return unit ID number of given disk
======================================================================*/
static unsigned long get_drive_id(int drive)
{
	int i;
	ulong id = 0;

  	drive&=3;
	/* set up for ID */
	MOTOR_ON;
	udelay(2);
	SELECT(SELMASK(drive));
	udelay(2);
	DESELECT(SELMASK(drive));
	udelay(2);
	MOTOR_OFF;
	udelay(2);
	SELECT(SELMASK(drive));
	udelay(2);
	DESELECT(SELMASK(drive));
	udelay(2);

	/* loop and read disk ID */
	for (i=0; i<32; i++) {
		SELECT(SELMASK(drive));
		udelay(2);

		/* read and store value of DSKRDY */
		id <<= 1;
		id |= (ciaa.pra & DSKRDY) ? 0 : 1;	/* cia regs are low-active! */

		DESELECT(SELMASK(drive));
	}

	selected = -1;

        /*
         * RB: At least A500/A2000's df0: don't identify themselves.
         * As every (real) Amiga has at least a 3.5" DD drive as df0:
         * we default to that if df0: doesn't identify as a certain
         * type.
         */
        if(drive == 0 && id == FD_NODRIVE)
         {
                id = fd_def_df0;
                printk("fd: drive 0 didn't identify, setting default %08lx\n",(ulong)fd_def_df0);
         }
	/* return the ID value */
	return (id);
}

static void fd_probe(int dev)
{
	unsigned long code;
	int type;
	int drive;
	int system;

	drive = dev & 3;
	code = get_drive_id(drive);

	/* get drive type */
	unit[drive].type = NULL;
	for (type = 0; type < num_dr_types; type++)
		if (drive_types[type].code == code)
			break;

	if (type >= num_dr_types) {
		printk("fd_probe: unsupported drive type %08lx found\n",
		       code);
		return;
	}

	unit[drive].type = &drive_types[type];
	unit[drive].track = -1;

	unit[drive].disk = -1;
	unit[drive].motor = 0;
	unit[drive].busy = 0;
	unit[drive].status = -1;


	system=(dev & 4)>>2;
	unit[drive].dtype=&data_types[system];
	unit[drive].sects=data_types[system].sects*unit[drive].type->sect_mult;
	unit[drive].blocks=unit[drive].type->heads*unit[drive].type->tracks*
	    unit[drive].sects;

	floppy_sizes[MINOR(dev)] = unit[drive].blocks >> 1;

}

static void probe_drives(void)
{
	int drive,found;

	printk("FD: probing units\nfound ");
	found=0;
	for(drive=0;drive<FD_MAX_UNITS;drive++) {
	  fd_probe(drive);
	  if (unit[drive].type->code != FD_NODRIVE) {
	    printk("fd%d ",drive);
	    found=1;
	  }
	}
	printk("%s\n",(found==0)?" no drives":"");
}

/*
 * floppy_open check for aliasing (/dev/fd0 can be the same as
 * /dev/PS0 etc), and disallows simultaneous access to the same
 * drive with different device numbers.
 */
static int floppy_open(struct inode *inode, struct file *filp)
{
  int drive;
  int old_dev;
  int system;

  drive = inode->i_rdev & 3;
  old_dev = fd_device[drive];

  if (fd_ref[drive])
    if (old_dev != inode->i_rdev)
      return -EBUSY;

  if (unit[drive].type->code == FD_NODRIVE)
    return -ENODEV;

  fd_ref[drive]++;
  fd_device[drive] = inode->i_rdev;

  if (old_dev && old_dev != inode->i_rdev)
    invalidate_buffers(old_dev);

  if (filp && filp->f_mode)
    check_disk_change(inode->i_rdev);

  if (filp && (filp->f_flags & (O_WRONLY|O_RDWR))) {
	  int wrprot;

	  fd_select (drive);
	  wrprot = !(ciaa.pra & DSKPROT);
	  fd_deselect (drive);

	  if (wrprot)
		  return -EROFS;
  }

  system=(inode->i_rdev & 4)>>2;
  unit[drive].dtype=&data_types[system];
  unit[drive].sects=data_types[system].sects*unit[drive].type->sect_mult;
  unit[drive].blocks=unit[drive].type->heads*unit[drive].type->tracks*
        unit[drive].sects;

printk("fd%d: accessing %s-disk with %s-layout\n",drive,unit[drive].type->name,
  data_types[system].name);

  return 0;
}

static void floppy_release(struct inode * inode, struct file * filp)
{
  unsigned long flags;

  fsync_dev(inode->i_rdev);
  invalidate_inodes(inode->i_rdev);
  invalidate_buffers(inode->i_rdev);
  save_flags (flags);
  cli();
  if ((inode->i_rdev & 3) == selected && writepending) {
    del_timer (&flush_track_timer);
    restore_flags (flags);
    non_int_flush_track (selected);
  } else
    restore_flags (flags);
  
  if (!fd_ref[inode->i_rdev & 3]--) {
    printk("floppy_release with fd_ref == 0");
    fd_ref[inode->i_rdev & 3] = 0;
  }
}

void amiga_floppy_setup (char *str, int *ints)
{
printk ("amiflop: Setting default df0 to %x\n", ints[1]);
fd_def_df0 = ints[1];
}

static struct file_operations floppy_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	fd_ioctl,		/* ioctl */
	NULL,			/* mmap */
	floppy_open,		/* open */
	floppy_release, 	/* release */
	block_fsync,		/* fsync */
	NULL,			/* fasync */
	amiga_floppy_change,	/* check_media_change */
	NULL,			/* revalidate */
};

static void fd_block_done(int irq, struct pt_regs *fp, void *dummy)
{
  if (block_flag)
    custom.dsklen = 0x4000;

  block_flag = 0;
  wake_up (&wait_fd_block);

  if (writefromint) {
    /* 
     * if it was a write from an interrupt,
     * we will call post_write from here
     */
    writepending = 2;
    post_write_timer.expires = 1; /* at least 2 ms */
    add_timer(&post_write_timer);
  }

}

int amiga_floppy_init(void)
{
  int i;

  if (!AMIGAHW_PRESENT(AMI_FLOPPY))
    return -ENXIO;

  if (register_blkdev(MAJOR_NR,"fd",&floppy_fops)) {
    printk("Unable to get major %d for floppy\n",MAJOR_NR);
    return -EBUSY;
  }

  /* initialize variables */
  motor_on_timer.next = NULL;
  motor_on_timer.prev = NULL;
  motor_on_timer.expires = 0;
  motor_on_timer.data = 0;
  motor_on_timer.function = motor_on_callback;
  for (i = 0; i < FD_MAX_UNITS; i++) {
	  motor_off_timer[i].next = NULL;
	  motor_off_timer[i].prev = NULL;
	  motor_off_timer[i].expires = 0;
	  motor_off_timer[i].data = i;
	  motor_off_timer[i].function = fd_motor_off;

	  unit[i].track = -1;
  }

  flush_track_timer.next = NULL;
  flush_track_timer.prev = NULL;
  flush_track_timer.expires = 0;
  flush_track_timer.data = 0;
  flush_track_timer.function = flush_track_callback;

  post_write_timer.next = NULL;
  post_write_timer.prev = NULL;
  post_write_timer.expires = 0;
  post_write_timer.data = 0;
  post_write_timer.function = post_write;
  
  blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
  blksize_size[MAJOR_NR] = floppy_blocksizes;
  blk_size[MAJOR_NR] = floppy_sizes;


  timer_table[FLOPPY_TIMER].fn = NULL;
  timer_active &= ~(1 << FLOPPY_TIMER);

  if (fd_def_df0==0) {
    if ((boot_info.bi_amiga.model == AMI_3000) ||
        (boot_info.bi_amiga.model == AMI_4000))
      fd_def_df0=FD_HD_3;
    else
      fd_def_df0=FD_DD_3;
  }

  probe_drives();

  raw_buf = (char *)amiga_chip_alloc (RAW_BUF_SIZE);

  for (i = 0; i < 128; i++)
    mfmdecode[i]=255;
  for (i = 0; i < 16; i++)
    mfmdecode[mfmencode[i]]=i;

  /* make sure that disk DMA is enabled */
  custom.dmacon = DMAF_SETCLR | DMAF_DISK;

  add_isr(IRQ_FLOPPY, fd_block_done, 0, NULL, "floppy_dma");
  add_isr(IRQ_AMIGA_CIAA_TB, ms_isr, 0, NULL, "floppy_timer");

  return 0;
}
