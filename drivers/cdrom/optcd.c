/*	$Id: optcd.c,v 1.3 1995/08/24 19:54:27 root Exp root $
	linux/drivers/block/optcd.c - Optics Storage 8000 AT CDROM driver

	Copyright (C) 1995 Leo Spiekman (spiekman@dutette.et.tudelft.nl)

	Based on Aztech CD268 CDROM driver by Werner Zimmermann and preworks
	by Eberhard Moenkeberg (emoenke@gwdg.de). ISP16 detection and
	configuration by Eric van der Maarel (maarel@marin.nl), with some data
	communicated by Vadim V. Model (vadim@rbrf.msk.su).

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2, or (at your option)
	any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

	History
	14-5-95		v0.0	Plays sound tracks. No reading of data CDs yet.
				Detection of disk change doesn't work.
	21-5-95		v0.1	First ALPHA version. CD can be mounted. The
				device major nr is borrowed from the Aztech
				driver. Speed is around 240 kb/s, as measured
				with "time dd if=/dev/cdrom of=/dev/null \
				bs=2048 count=4096".
	24-6-95		v0.2	Reworked the #defines for the command codes
				and the like, as well as the structure of
				the hardware communication protocol, to
				reflect the "official" documentation, kindly
				supplied by C.K. Tan, Optics Storage Pte. Ltd.
				Also tidied up the state machine somewhat.
	28-6-95		v0.3	Removed the ISP-16 interface code, as this
				should go into its own driver. The driver now
				has its own major nr.
				Disk change detection now seems to work, too.
				This version became part of the standard
				kernel as of version 1.3.7
	24-9-95		v0.4	Re-inserted ISP-16 interface code which I
				copied from sjcd.c, with a few changes.
				Updated README.optcd. Submitted for
				inclusion in 1.3.21
	29-9-95		v0.4a	Fixed bug that prevented compilation as module
*/

#include <linux/major.h>
#include <linux/config.h>

#ifdef MODULE
# include <linux/module.h>
# include <linux/version.h>
# ifndef CONFIG_MODVERSIONS
	char kernel_version[]= UTS_RELEASE;
# endif
#define optcd_init init_module
#else
# define MOD_INC_USE_COUNT
# define MOD_DEC_USE_COUNT
#endif

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/cdrom.h>
#include <linux/ioport.h>
#include <asm/io.h>

#define MAJOR_NR OPTICS_CDROM_MAJOR

# include <linux/blk.h>
#define optcd_port optcd	/* Needed for the modutils. */
# include <linux/optcd.h>


/* Some (Media)Magic */
/* define types of drive the interface on an ISP16 card may be looking at */
#define ISP16_DRIVE_X 0x00
#define ISP16_SONY  0x02
#define ISP16_PANASONIC0 0x02
#define ISP16_SANYO0 0x02
#define ISP16_MITSUMI  0x04
#define ISP16_PANASONIC1 0x06
#define ISP16_SANYO1 0x06
#define ISP16_DRIVE_NOT_USED 0x08  /* not used */
#define ISP16_DRIVE_SET_MASK 0xF1  /* don't change 0-bit or 4-7-bits*/
/* ...for port */
#define ISP16_DRIVE_SET_PORT 0xF8D
/* set io parameters */
#define ISP16_BASE_340  0x00
#define ISP16_BASE_330  0x40
#define ISP16_BASE_360  0x80
#define ISP16_BASE_320  0xC0
#define ISP16_IRQ_X  0x00
#define ISP16_IRQ_5  0x04  /* shouldn't be used due to soundcard conflicts */
#define ISP16_IRQ_7  0x08  /* shouldn't be used due to soundcard conflicts */
#define ISP16_IRQ_3  0x0C
#define ISP16_IRQ_9  0x10
#define ISP16_IRQ_10  0x14
#define ISP16_IRQ_11  0x18
#define ISP16_DMA_X  0x03
#define ISP16_DMA_3  0x00
#define ISP16_DMA_5  0x00
#define ISP16_DMA_6  0x01
#define ISP16_DMA_7  0x02
#define ISP16_IO_SET_MASK  0x20  /* don't change 5-bit */
/* ...for port */
#define ISP16_IO_SET_PORT  0xF8E
/* enable the drive */
#define ISP16_NO_IDE__ENABLE_CDROM_PORT  0xF90  /* ISP16 without IDE interface */
#define ISP16_IDE__ENABLE_CDROM_PORT  0xF91  /* ISP16 with IDE interface */
#define ISP16_ENABLE_CDROM  0x80  /* seven bit */

/* the magic stuff */
#define ISP16_CTRL_PORT  0xF8F
#define ISP16_NO_IDE__CTRL  0xE2  /* ISP16 without IDE interface */
#define ISP16_IDE__CTRL  0xE3  /* ISP16 with IDE interface */

static short isp16_detect(void);
static short isp16_no_ide__detect(void);
static short isp16_with_ide__detect(void);
static short isp16_config( int base, u_char drive_type, int irq, int dma );
static short isp16_type; /* dependent on type of interface card */
static u_char isp16_ctrl;
static u_short isp16_enable_cdrom_port;


static short optcd_port = OPTCD_PORTBASE;

/* Read current status/data availability flags */
inline static int optFlags(void) {
	return inb(STATUS_PORT) & FL_STDT;
}

/* Wait for status available; return TRUE on timeout */
static int sten_low(void) {
	int no_status;
	unsigned long count = 0;
	while ((no_status = (optFlags() & FL_STEN)))
		if (++count >= BUSY_TIMEOUT)
			break;
#ifdef DEBUG_DRIVE_IF
	if (no_status)
		printk("optcd: timeout waiting for STEN low\n");
	else
		printk("optcd: STEN low after %ld\n", count);
#endif
	return no_status;
}

/* Wait for data available; return TRUE on timeout */
static int dten_low(void) {
	int no_data;
	unsigned long count = 0;
	while ((no_data = (optFlags() & FL_DTEN)))
		if (++count >= BUSY_TIMEOUT)
			break;
#ifdef DEBUG_DRIVE_IF
	if (no_data)
		printk("optcd: timeout waiting for DTEN low\n");
	else
		printk("optcd: DTEN low after %ld\n", count);
#endif
	return no_data;
}

/* Facilities for polled waiting for status or data */
static int sleep_timeout;		/* Max amount of time still to sleep */
static unsigned char sleep_flags;	/* Flags read last time around */
static struct wait_queue *waitq = NULL;
static struct timer_list delay_timer = {NULL, NULL, 0, 0, NULL};

/* Timer routine: wake up when either of FL_STEN or FL_DTEN goes down,
 * or when timeout expires. Otherwise wait some more.
 */
static void sleep_timer(void) {
	if ((sleep_flags = optFlags()) != FL_STDT) {
		wake_up(&waitq);
		return;
	}
	if (--sleep_timeout <= 0) {
		wake_up(&waitq);
		return;
	}
	SET_TIMER(sleep_timer, 1);
}

/* Sleep until any of FL_STEN or FL_DTEN go down, or until timeout.
 * sleep_timeout must be set first.
 */
static int sleep_status(void) {
#ifdef DEBUG_DRIVE_IF
	printk("optcd: sleeping %d on status\n", sleep_timeout);
#endif
	if (sleep_timeout <= 0)		/* timeout immediately */
		return FL_STDT;
	if ((sleep_flags = optFlags()) == FL_STDT) {
		SET_TIMER(sleep_timer, 1);
		sleep_on(&waitq);
	}
#ifdef DEBUG_DRIVE_IF
	printk("optcd: woken up with %d to go, flags %d\n",
		sleep_timeout, sleep_flags);
#endif
	return sleep_flags;
}

/* Sleep until status available; return TRUE on timeout */
inline static int sleep_sten_low(void) {
	int flags;
	sleep_timeout = SLEEP_TIMEOUT;
	flags = sleep_status();
#ifdef DEBUG_DRIVE_IF
	if (!(flags & FL_DTEN))
		printk("optcd: DTEN while waiting for STEN\n");
#endif
	return flags & FL_STEN;
}

/* Sleep until data available; return TRUE on timeout */
inline static int sleep_dten_low(void) {
	int flags;
	sleep_timeout = SLEEP_TIMEOUT;
	flags = sleep_status();
#ifdef DEBUG_DRIVE_IF
	if (!(flags & FL_STEN))
		printk("optcd: STEN while waiting for DTEN\n");
#endif
	return flags & FL_DTEN;
}

/* Send command code. Return <0 indicates error */
static int optSendCmd(int cmd) {
	unsigned char ack;
#if defined(DEBUG_DRIVE_IF)||defined(DEBUG_COMMANDS)
	printk("optcd: executing command 0x%02x\n", cmd);
#endif
	outb(HCON_DTS, HCON_PORT);	/* Enable Suspend Data Transfer */
	outb(cmd, COMIN_PORT);		/* Send command code */
	if (sten_low())			/* Wait for status available */
		return -ERR_IF_CMD_TIMEOUT;
	ack = inb(DATA_PORT);		/* read command acknowledge */
#ifdef DEBUG_DRIVE_IF
	printk("optcd: acknowledge code 0x%02x\n", ack);
#endif
	outb(HCON_SDRQB, HCON_PORT);	/* Disable Suspend Data Transfer */
	return ack==ST_OP_OK ? 0 : -ack;
}

/* Send command parameters. Return <0 indicates error */
static int optSendParams(struct opt_Play_msf *params) {
	unsigned char ack;
#if defined(DEBUG_DRIVE_IF)||defined(DEBUG_COMMANDS)
	printk("optcd: params %02x:%02x:%02x %02x:%02x:%02x\n",
		params->start.min, params->start.sec, params->start.frame,
		params->end.min, params->end.sec, params->end.frame);
#endif
	outb(params -> start.min, COMIN_PORT);
	outb(params -> start.sec, COMIN_PORT);
	outb(params -> start.frame, COMIN_PORT);
	outb(params -> end.min, COMIN_PORT);
	outb(params -> end.sec, COMIN_PORT);
	outb(params -> end.frame, COMIN_PORT);
	if (sten_low())			/* Wait for status available */
		return -ERR_IF_CMD_TIMEOUT;
	ack = inb(DATA_PORT);		/* read command acknowledge */
#ifdef DEBUG_DRIVE_IF
	printk("optcd: acknowledge code 0x%02x\n", ack);
#endif
	return ack==ST_PA_OK ? 0 : -ack;
}

/* Return execution status for quick response commands, i.e. busy wait.
 * Return value <0 indicates timeout.
 */
static int optGetExecStatus(void) {
	unsigned char exec_status;
	if (sten_low())			/* Wait for status available */
		return -ERR_IF_CMD_TIMEOUT;
	exec_status = inb(DATA_PORT);	/* read command execution status */
#ifdef DEBUG_DRIVE_IF
	printk("optcd: returned execution status: 0x%02x\n", exec_status);
#endif
	return exec_status;
}

/* Return execution status for slow commands. Only use when no data is
 * expected. Return value <0 indicates timeout.
 */
static int optSleepTillExecStatus(void) {
	unsigned char exec_status;
	if (sleep_sten_low())		/* Wait for status available */
		return -ERR_IF_CMD_TIMEOUT;
	exec_status = inb(DATA_PORT);	/* read command execution status */
#ifdef DEBUG_DRIVE_IF
	printk("optcd: returned execution status: 0x%02x\n", exec_status);
#endif
	return exec_status;
}

/* Fetch status that has previously been waited for. <0 means not available */
inline static int optStatus(void) {
	unsigned char status;
	if (optFlags() & FL_STEN)
		return -ERR_IF_NOSTAT;
	status = inb(DATA_PORT);
#ifdef DEBUG_DRIVE_IF
	printk("optcd: read status: 0x%02x\n", status);
#endif
	return status;
}

/* Wait for extra byte of data that a command returns */
static int optGetData(void) {
	unsigned char data;
	if (sten_low())
		return -ERR_IF_DATA_TIMEOUT;
	data = inb(DATA_PORT);
#ifdef DEBUG_DRIVE_IF
	printk("optcd: read data: 0x%02x\n", data);
#endif
	return data;
}

/* Read data that has previously been waited for. */
inline static void optReadData(char *buf, int n) {
	insb(DATA_PORT, buf, n);
}

/* Flush status and data fifos */
inline static void optFlushData(void) {
	while (optFlags() != FL_STDT)
		inb(DATA_PORT);
}

/* Write something to RESET_PORT and wait. Return TRUE upon success. */
static int optResetDrive(void) {
	unsigned long count = 0;
	int flags;
#ifdef DEBUG_DRIVE_IF
	printk("optcd: reset drive\n");
#endif
	outb(0, RESET_PORT);
	while (++count < RESET_WAIT)
		inb(DATA_PORT);
	count = 0;
	while ((flags = (inb(STATUS_PORT) & FL_RESET)) != FL_RESET)
		if (++count >= BUSY_TIMEOUT)
			break;
#ifdef DEBUG_DRIVE_IF
	if (flags == FL_RESET)
		printk("optcd: drive reset\n");
	else
		printk("optcd: reset failed\n");
#endif
	if (flags != FL_RESET)
		return 0;		/* Reset failed */
	outb(HCON_SDRQB, HCON_PORT);	/* Disable Suspend Data Transfer */
	return 1;			/* Reset succeeded */
}


/* Command protocol */

/* Send a simple command and wait for response */
inline static int optCmd(int cmd) {
	int ack = optSendCmd(cmd);
	if (ack < 0)
		return ack;
	if (cmd < COMFETCH)		/* Quick response command */
		return optGetExecStatus();
	else				/* Slow command */
		return optSleepTillExecStatus();
}

/* Send a command with parameters and wait for response */
inline static int optPlayCmd(int cmd, struct opt_Play_msf *params) {
	int ack = optSendCmd(cmd);
	if (ack < 0)
		return ack;
	if ((ack = optSendParams(params)) < 0)
		return ack;
	return optSleepTillExecStatus();
}

/* Send a command with parameters. Don't wait for the response,
 * which consists of the data blocks read. */
inline static int optReadCmd(int cmd, struct opt_Play_msf *params) {
	int ack = optSendCmd(cmd);
	if (ack < 0)
		return ack;
	return optSendParams(params);
}


/* Address conversion routines */

/* Binary to BCD (2 digits) */
inline static unsigned char bin2bcd(unsigned char p) {
#ifdef DEBUG_CONV
	if (p > 99)
		printk("optcd: error bin2bcd %d\n", p);
#endif
	return (p % 10) | ((p / 10) << 4);
}

/* Linear address to minute, second, frame form */
static void hsg2msf(long hsg, struct msf *msf) {
	hsg += 150;
	msf -> min = hsg / 4500;
	hsg %= 4500;
	msf -> sec = hsg / 75;
	msf -> frame = hsg % 75;
#ifdef DEBUG_CONV
	if (msf -> min >= 70)
		printk("optcd: Error hsg2msf address Minutes\n");
	if (msf -> sec >= 60)
		printk("optcd: Error hsg2msf address Seconds\n");
	if (msf -> frame >= 75)
		printk("optcd: Error hsg2msf address Frames\n");
#endif
	msf -> min = bin2bcd(msf -> min);	/* convert to BCD */
	msf -> sec = bin2bcd(msf -> sec);
	msf -> frame = bin2bcd(msf -> frame);
}

/* Two BCD digits to binary */
inline static int bcd2bin(unsigned char bcd) {
	return (bcd >> 4) * 10 + (bcd & 0x0f);
}

/* Minute, second, frame address to linear address */
static long msf2hsg(struct msf *mp) {
#ifdef DEBUG_CONV
	if (mp -> min >= 70)
		printk("optcd: Error msf2hsg address Minutes\n");
	if (mp -> sec >= 60)
		printk("optcd: Error msf2hsg address Seconds\n");
	if (mp -> frame >= 75)
		printk("optcd: Error msf2hsg address Frames\n");
#endif
	return bcd2bin(mp -> frame)
		+ bcd2bin(mp -> sec) * 75
		+ bcd2bin(mp -> min) * 4500
		- 150;
}


/* Drive status and table of contents */

static int optAudioStatus = CDROM_AUDIO_NO_STATUS;
static char optDiskChanged = 1;
static char optTocUpToDate = 0;
static struct opt_DiskInfo DiskInfo;
static struct opt_Toc Toc[MAX_TRACKS];

/* Get CDROM status, flagging completion of audio play and disk changes. */
static int optGetStatus(void) {
	int st;
	if ((st = optCmd(COMIOCTLISTAT)) < 0)
		return st;
	if (st == 0xff)
		return -ERR_IF_NOSTAT;
	if (((st & ST_MODE_BITS) != ST_M_AUDIO) &&
		(optAudioStatus == CDROM_AUDIO_PLAY)) {
		optAudioStatus = CDROM_AUDIO_COMPLETED;
	}
	if (st & ST_DSK_CHG) {
		optDiskChanged = 1;
		optTocUpToDate = 0;
		optAudioStatus = CDROM_AUDIO_NO_STATUS;
	}
	return st;
}

/*
 * Read the current Q-channel info. Also used for reading the
 * table of contents.
 */
static int optGetQChannelInfo(struct opt_Toc *qp) {
	int st;
#ifdef DEBUG_TOC
	printk("optcd: starting optGetQChannelInfo\n");
#endif
	if ((st = optGetStatus()) < 0)
		return st;
	if ((st = optCmd(COMSUBQ)) < 0)
		return st;
	if ((qp -> ctrl_addr = st = optGetData()), st < 0) return st;
	if ((qp -> track = st = optGetData()), st < 0) return st;
	if ((qp -> pointIndex = st = optGetData()), st < 0) return st;
	if ((qp -> trackTime.min = st = optGetData()), st < 0) return st;
	if ((qp -> trackTime.sec = st = optGetData()), st < 0) return st;
	if ((qp -> trackTime.frame = st = optGetData()), st < 0) return st;
	if ((st = optGetData()) < 0) return st;		/* byte not used */
	if ((qp -> diskTime.min = st = optGetData()), st < 0) return st;
	if ((qp -> diskTime.sec = st = optGetData()), st < 0) return st;
	if ((qp -> diskTime.frame = st = optGetData()), st < 0) return st;
#ifdef DEBUG_TOC
	printk("optcd: exiting optGetQChannelInfo\n");
#endif
	return 0;
}

#define QINFO_FIRSTTRACK	0xa0
#define QINFO_LASTTRACK		0xa1
#define QINFO_DISKLENGTH	0xa2

static int optGetDiskInfo(void) {
	int st, limit;
	unsigned char test = 0;
	struct opt_Toc qInfo;
#ifdef DEBUG_TOC
	printk("optcd: starting optGetDiskInfo\n");
#endif
	optDiskChanged = 0;
	if ((st = optCmd(COMLEADIN)) < 0)
		return st;
	for (limit = 300; (limit > 0) && (test != 0x0f); limit--) {
		if ((st = optGetQChannelInfo(&qInfo)) < 0)
			return st;
		switch (qInfo.pointIndex) {
		case QINFO_FIRSTTRACK:
			DiskInfo.first = bcd2bin(qInfo.diskTime.min);
#ifdef DEBUG_TOC
			printk("optcd: got first: %d\n", DiskInfo.first);
#endif
			test |= 0x01;
			break;
		case QINFO_LASTTRACK:
			DiskInfo.last = bcd2bin(qInfo.diskTime.min);
#ifdef DEBUG_TOC
			printk("optcd: got last: %d\n", DiskInfo.last);
#endif
			test |= 0x02;
			break;
		case QINFO_DISKLENGTH:
			DiskInfo.diskLength.min = qInfo.diskTime.min;
			DiskInfo.diskLength.sec = qInfo.diskTime.sec-2;
			DiskInfo.diskLength.frame = qInfo.diskTime.frame;
#ifdef DEBUG_TOC
			printk("optcd: got length: %x:%x.%x\n",
				DiskInfo.diskLength.min,
				DiskInfo.diskLength.sec,
				DiskInfo.diskLength.frame);
#endif
			test |= 0x04;
			break;
		default:
			if ((test & 0x01)	/* Got no of first track */
			 && (qInfo.pointIndex == DiskInfo.first)) {
				/* StartTime of First Track */
				DiskInfo.firstTrack.min = qInfo.diskTime.min;
				DiskInfo.firstTrack.sec = qInfo.diskTime.sec;
				DiskInfo.firstTrack.frame = qInfo.diskTime.frame;
#ifdef DEBUG_TOC
			printk("optcd: got start: %x:%x.%x\n",
				DiskInfo.firstTrack.min,
				DiskInfo.firstTrack.sec,
				DiskInfo.firstTrack.frame);
#endif
				test |= 0x08;
			}
		}
	}
#ifdef DEBUG_TOC
	printk("optcd: exiting optGetDiskInfo\n");
#endif
	if (test != 0x0f)
		return -ERR_TOC_MISSINGINFO;
	return 0;
}

static int optGetToc(void) {	/* Presumes we have got DiskInfo */
	int st, count, px, limit;
	struct opt_Toc qInfo;
#ifdef DEBUG_TOC
	int i;
	printk("optcd: starting optGetToc\n");
#endif
	for (count = 0; count < MAX_TRACKS; count++)
		Toc[count].pointIndex = 0;
	if ((st = optCmd(COMLEADIN)) < 0)
		return st;
	st = 0;
	count = DiskInfo.last + 3;
	for (limit = 300; (limit > 0) && (count > 0); limit--) {
		if ((st = optGetQChannelInfo(&qInfo)) < 0)
			break;
		px = bcd2bin(qInfo.pointIndex);
		if (px > 0 && px < MAX_TRACKS && qInfo.track == 0)
			if (Toc[px].pointIndex == 0) {
				Toc[px] = qInfo;
				count--;
			}
	}
	Toc[DiskInfo.last + 1].diskTime = DiskInfo.diskLength;
#ifdef DEBUG_TOC
	printk("optcd: exiting optGetToc\n");
	for (i = 1; i <= DiskInfo.last + 1; i++)
		printk("i = %3d ctl-adr = %02x track %2d px "
			"%02x %02x:%02x.%02x %02x:%02x.%02x\n",
			i, Toc[i].ctrl_addr,
			Toc[i].track,
			Toc[i].pointIndex,
			Toc[i].trackTime.min,
			Toc[i].trackTime.sec,
			Toc[i].trackTime.frame,
			Toc[i].diskTime.min,
			Toc[i].diskTime.sec,
			Toc[i].diskTime.frame);
	for (i = 100; i < 103; i++)
		printk("i = %3d ctl-adr = %02x track %2d px "
			"%02x %02x:%02x.%02x %02x:%02x.%02x\n",
			i, Toc[i].ctrl_addr,
			Toc[i].track,
			Toc[i].pointIndex,
			Toc[i].trackTime.min,
			Toc[i].trackTime.sec,
			Toc[i].trackTime.frame,
			Toc[i].diskTime.min,
			Toc[i].diskTime.sec,
			Toc[i].diskTime.frame);
#endif
	return count ? -ERR_TOC_MISSINGENTRY : 0;
}

static int optUpdateToc(void) {
#ifdef DEBUG_TOC
	printk("optcd: starting optUpdateToc\n");
#endif
	if (optTocUpToDate)
		return 0;
	if (optGetDiskInfo() < 0)
		return -EIO;
	if (optGetToc() < 0)
		return -EIO;
	optTocUpToDate = 1;
#ifdef DEBUG_TOC
	printk("optcd: exiting optUpdateToc\n");
#endif
	return 0;
}


/* Buffers */

#define OPT_BUF_SIZ		16
#define OPT_BLOCKSIZE		2048
#define OPT_BLOCKSIZE_RAW	2336
#define OPT_BLOCKSIZE_ALL	2646
#define OPT_NOBUF		-1

/* Buffer for block size conversion. */
static char opt_buf[OPT_BLOCKSIZE*OPT_BUF_SIZ];
static volatile int opt_buf_bn[OPT_BUF_SIZ], opt_next_bn;
static volatile int opt_buf_in = 0, opt_buf_out = OPT_NOBUF;

inline static void opt_invalidate_buffers(void) {
	int i;
#ifdef DEBUG_BUFFERS
	printk("optcd: executing opt_invalidate_buffers\n");
#endif
	for (i = 0; i < OPT_BUF_SIZ; i++)
		opt_buf_bn[i] = OPT_NOBUF;
	opt_buf_out = OPT_NOBUF;
}

/*
 * Take care of the different block sizes between cdrom and Linux.
 * When Linux gets variable block sizes this will probably go away.
 */
static void opt_transfer(void) {
#if (defined DEBUG_BUFFERS) || (defined DEBUG_REQUEST)
	printk("optcd: executing opt_transfer\n");
#endif
	if (!CURRENT_VALID)
		return;
	while (CURRENT -> nr_sectors) {
		int bn = CURRENT -> sector / 4;
		int i, offs, nr_sectors;
		for (i = 0; i < OPT_BUF_SIZ && opt_buf_bn[i] != bn; ++i);
#ifdef DEBUG_REQUEST
		printk("optcd: found %d\n", i);
#endif
		if (i >= OPT_BUF_SIZ) {
			opt_buf_out = OPT_NOBUF;
			break;
		}
		offs = (i * 4 + (CURRENT -> sector & 3)) * 512;
		nr_sectors = 4 - (CURRENT -> sector & 3);
		if (opt_buf_out != i) {
			opt_buf_out = i;
			if (opt_buf_bn[i] != bn) {
				opt_buf_out = OPT_NOBUF;
				continue;
			}
		}
		if (nr_sectors > CURRENT -> nr_sectors)
			nr_sectors = CURRENT -> nr_sectors;
		memcpy(CURRENT -> buffer, opt_buf + offs, nr_sectors * 512);
		CURRENT -> nr_sectors -= nr_sectors;
		CURRENT -> sector += nr_sectors;
		CURRENT -> buffer += nr_sectors * 512;
	}
}


/* State machine for reading disk blocks */

enum opt_state_e {
	OPT_S_IDLE,	/* 0 */
	OPT_S_START,	/* 1 */
	OPT_S_READ,	/* 2 */
	OPT_S_DATA,	/* 3 */
	OPT_S_STOP,	/* 4 */
	OPT_S_STOPPING	/* 5 */
};

static volatile enum opt_state_e opt_state = OPT_S_IDLE;
#ifdef DEBUG_STATE
static volatile enum opt_state_e opt_state_old = OPT_S_STOP;
static volatile int opt_st_old = 0;
static volatile long opt_state_n = 0;
#endif

static volatile int opt_transfer_is_active = 0;
static volatile int opt_error = 0;	/* do something with this?? */
static int optTries;			/* ibid?? */

static void opt_poll(void) {
	static int optTimeout;
	static volatile int opt_read_count = 1;
	int st = 0;
	int loop_ctl = 1;
	int skip = 0;

	if (opt_error) {
		printk("optcd: I/O error 0x%02x\n", opt_error);
		opt_invalidate_buffers();
#ifdef WARN_IF_READ_FAILURE
		if (optTries == 5)
			printk("optcd: read block %d failed; audio disk?\n",
			        opt_next_bn);
#endif
		if (!optTries--) {
			printk("optcd: read block %d failed; Giving up\n",
			       opt_next_bn);
			if (opt_transfer_is_active) {
				optTries = 0;
				loop_ctl = 0;
			}
			if (CURRENT_VALID)
				end_request(0);
			optTries = 5;
		}
		opt_error = 0;
		opt_state = OPT_S_STOP;
	}

	while (loop_ctl)
	{
		loop_ctl = 0; /* each case must flip this back to 1 if we want
		                 to come back up here */
#ifdef DEBUG_STATE
		if (opt_state == opt_state_old)
			opt_state_n++;
		else {
			opt_state_old = opt_state;
			if (++opt_state_n > 1)
				printk("optcd: %ld times in previous state\n",
					opt_state_n);
			printk("optcd: state %d\n", opt_state);
			opt_state_n = 0;
		}
#endif
		switch (opt_state) {
		case OPT_S_IDLE:
			return;
		case OPT_S_START:
			if (optSendCmd(COMDRVST))
				return;
			opt_state = OPT_S_READ;
			optTimeout = 3000;
			break;
		case OPT_S_READ: {
			struct opt_Play_msf msf;
			if (!skip) {
				if ((st = optStatus()) < 0)
					break;
				if (st & ST_DSK_CHG) {
					optDiskChanged = 1;
					optTocUpToDate = 0;
					opt_invalidate_buffers();
				}
			}
			skip = 0;
			if ((st & ST_DOOR_OPEN) || (st & ST_DRVERR)) {
				optDiskChanged = 1;
				optTocUpToDate = 0;
				printk((st & ST_DOOR_OPEN)
				       ? "optcd: door open\n"
				       : "optcd: disk removed\n");
				if (opt_transfer_is_active) {
					opt_state = OPT_S_START;
					loop_ctl = 1;
					break;
				}
				opt_state = OPT_S_IDLE;
				while (CURRENT_VALID)
					end_request(0);
				return;
			}
			if (!CURRENT_VALID) {
				opt_state = OPT_S_STOP;
				loop_ctl = 1;
				break;
			}
			opt_next_bn = CURRENT -> sector / 4;
			hsg2msf(opt_next_bn, &msf.start);
			opt_read_count = OPT_BUF_SIZ;
			msf.end.min = 0;
			msf.end.sec = 0;
			msf.end.frame = opt_read_count;
#ifdef DEBUG_REQUEST
			printk("optcd: reading %x:%x.%x %x:%x.%x\n",
				msf.start.min,
				msf.start.sec,
				msf.start.frame,
				msf.end.min,
				msf.end.sec,
				msf.end.frame);
			printk("optcd: opt_next_bn:%d opt_buf_in:%d opt_buf_out:%d opt_buf_bn:%d\n",
				opt_next_bn,
				opt_buf_in,
				opt_buf_out,
				opt_buf_bn[opt_buf_in]);
#endif
			optReadCmd(COMREAD, &msf);
			opt_state = OPT_S_DATA;
			optTimeout = READ_TIMEOUT;
			break;
		}
		case OPT_S_DATA:
			st = optFlags() & (FL_STEN|FL_DTEN);
#ifdef DEBUG_STATE
			if (st != opt_st_old) {
				opt_st_old = st;
				printk("optcd: st:%x\n", st);
			}
			if (st == FL_STEN)
				printk("timeout cnt: %d\n", optTimeout);
#endif
			switch (st) {
			case FL_DTEN:
#ifdef WARN_IF_READ_FAILURE
				if (optTries == 5)
					printk("optcd: read block %d failed; audio disk?\n",
					       opt_next_bn);
#endif
				if (!optTries--) {
					printk("optcd: read block %d failed; Giving up\n",
					       opt_next_bn);
					if (opt_transfer_is_active) {
						optTries = 0;
						break;
					}
					if (CURRENT_VALID)
						end_request(0);
					optTries = 5;
				}
				opt_state = OPT_S_START;
				optTimeout = READ_TIMEOUT;
				loop_ctl = 1;
			case (FL_STEN|FL_DTEN):
				break;
			default:
				optTries = 5;
				if (!CURRENT_VALID && opt_buf_in == opt_buf_out) {
					opt_state = OPT_S_STOP;
					loop_ctl = 1;
					break;
				}
				if (opt_read_count<=0)
					printk("optcd: warning - try to read 0 frames\n");
				while (opt_read_count) {
					opt_buf_bn[opt_buf_in] = OPT_NOBUF;
					if (dten_low()) { /* should be no waiting here!?? */
						printk("read_count:%d CURRENT->nr_sectors:%ld opt_buf_in:%d\n",
							opt_read_count,
							CURRENT->nr_sectors,
							opt_buf_in);
						printk("opt_transfer_is_active:%x\n",
							opt_transfer_is_active);
						opt_read_count = 0;
						opt_state = OPT_S_STOP;
						loop_ctl = 1;
						end_request(0);
						break;
					}
					optReadData(opt_buf+OPT_BLOCKSIZE*opt_buf_in, OPT_BLOCKSIZE);
					opt_read_count--;
#ifdef DEBUG_REQUEST
					printk("OPT_S_DATA; ---I've read data- read_count: %d\n",
					       opt_read_count);
					printk("opt_next_bn:%d  opt_buf_in:%d opt_buf_out:%d  opt_buf_bn:%d\n",
					       opt_next_bn,
					       opt_buf_in,
					       opt_buf_out,
					       opt_buf_bn[opt_buf_in]);
#endif
					opt_buf_bn[opt_buf_in] = opt_next_bn++;
					if (opt_buf_out == OPT_NOBUF)
						opt_buf_out = opt_buf_in;
					opt_buf_in = opt_buf_in + 1 ==
						OPT_BUF_SIZ ? 0 : opt_buf_in + 1;
				}
				if (!opt_transfer_is_active) {
					while (CURRENT_VALID) {
						opt_transfer();
						if (CURRENT -> nr_sectors == 0)
							end_request(1);
						else
							break;
					}
				}

				if (CURRENT_VALID
				    && (CURRENT -> sector / 4 < opt_next_bn ||
				    CURRENT -> sector / 4 >
				     opt_next_bn + OPT_BUF_SIZ)) {
					opt_state = OPT_S_STOP;
					loop_ctl = 1;
					break;
				}
				optTimeout = READ_TIMEOUT;
				if (opt_read_count == 0) {
					opt_state = OPT_S_STOP;
					loop_ctl = 1;
					break;
				}
			}
			break;
		case OPT_S_STOP:
			if (opt_read_count != 0)
				printk("optcd: discard data=%x frames\n",
					opt_read_count);
			while (opt_read_count != 0) {
				optFlushData();
				opt_read_count--;
			}
			if (optSendCmd(COMDRVST))
				return;
			opt_state = OPT_S_STOPPING;
			optTimeout = 1000;
			break;
		case OPT_S_STOPPING:
			if ((st = optStatus()) < 0 && optTimeout)
					break;
			if ((st != -1) && (st & ST_DSK_CHG)) {
				optDiskChanged = 1;
				optTocUpToDate = 0;
				opt_invalidate_buffers();
			}
			if (CURRENT_VALID) {
				if (st != -1) {
					opt_state = OPT_S_READ;
					loop_ctl = 1;
					skip = 1;
					break;
				} else {
					opt_state = OPT_S_START;
					optTimeout = 1;
				}
			} else {
				opt_state = OPT_S_IDLE;
				return;
			}
			break;
		default:
			printk("optcd: invalid state %d\n", opt_state);
			return;
		} /* case */
	} /* while */

	if (!optTimeout--) {
		printk("optcd: timeout in state %d\n", opt_state);
		opt_state = OPT_S_STOP;
		if (optCmd(COMSTOP) < 0)
			return;
	}

	SET_TIMER(opt_poll, 1);
}


static void do_optcd_request(void) {
#ifdef DEBUG_REQUEST
	printk("optcd: do_optcd_request(%ld+%ld)\n",
	       CURRENT -> sector, CURRENT -> nr_sectors);
#endif
	opt_transfer_is_active = 1;
	while (CURRENT_VALID) {
		if (CURRENT->bh) {
			if (!CURRENT->bh->b_lock)
				panic(DEVICE_NAME ": block not locked");
		}
		opt_transfer();	/* First try to transfer block from buffers */
		if (CURRENT -> nr_sectors == 0) {
			end_request(1);
		} else {	/* Want to read a block not in buffer */
			opt_buf_out = OPT_NOBUF;
			if (opt_state == OPT_S_IDLE) {
				/* Should this block the request queue?? */
				if (optUpdateToc() < 0) {
					while (CURRENT_VALID)
						end_request(0);
					break;
				}
				/* Start state machine */
				opt_state = OPT_S_START;
				optTries = 5;
				SET_TIMER(opt_poll, 1);	/* why not start right away?? */
			}
			break;
		}
	}
	opt_transfer_is_active = 0;
#ifdef DEBUG_REQUEST
	printk("opt_next_bn:%d  opt_buf_in:%d opt_buf_out:%d  opt_buf_bn:%d\n",
	       opt_next_bn, opt_buf_in, opt_buf_out, opt_buf_bn[opt_buf_in]);
	printk("optcd: do_optcd_request ends\n");
#endif
}


/* VFS calls */

static int opt_ioctl(struct inode *ip, struct file *fp,
			unsigned int cmd, unsigned long arg) {
	static struct opt_Play_msf opt_Play;	/* pause position */
	int err;
#ifdef DEBUG_VFS
	printk("optcd: starting opt_ioctl, command 0x%x\n", cmd);
#endif
	if (!ip)
		return -EINVAL;
	if (optGetStatus() < 0)
		return -EIO;
	if ((err = optUpdateToc()) < 0)
		return err;

	switch (cmd) {
	case CDROMPAUSE: {
		struct opt_Toc qInfo;

		if (optAudioStatus != CDROM_AUDIO_PLAY)
			return -EINVAL;
		if (optGetQChannelInfo(&qInfo) < 0) {
			/* didn't get q channel info */
			optAudioStatus = CDROM_AUDIO_NO_STATUS;
			return 0;
		}
		opt_Play.start = qInfo.diskTime;	/* restart point */
		if (optCmd(COMPAUSEON) < 0)
			return -EIO;
		optAudioStatus = CDROM_AUDIO_PAUSED;
		break;
	}
	case CDROMRESUME:
		if (optAudioStatus != CDROM_AUDIO_PAUSED)
			return -EINVAL;
		if (optPlayCmd(COMPLAY, &opt_Play) < 0) {
			optAudioStatus = CDROM_AUDIO_ERROR;
			return -EIO;
		}
		optAudioStatus = CDROM_AUDIO_PLAY;
		break;
	case CDROMPLAYMSF: {
		int st;
		struct cdrom_msf msf;

		if ((st = verify_area(VERIFY_READ, (void *) arg, sizeof msf)))
			return st;
		memcpy_fromfs(&msf, (void *) arg, sizeof msf);
		opt_Play.start.min = bin2bcd(msf.cdmsf_min0);
		opt_Play.start.sec = bin2bcd(msf.cdmsf_sec0);
		opt_Play.start.frame = bin2bcd(msf.cdmsf_frame0);
		opt_Play.end.min = bin2bcd(msf.cdmsf_min1);
		opt_Play.end.sec = bin2bcd(msf.cdmsf_sec1);
		opt_Play.end.frame = bin2bcd(msf.cdmsf_frame1);
		if (optPlayCmd(COMPLAY, &opt_Play) < 0) {
			optAudioStatus = CDROM_AUDIO_ERROR;
			return -EIO;
		}
		optAudioStatus = CDROM_AUDIO_PLAY;
		break;
	}
	case CDROMPLAYTRKIND: {
		int st;
		struct cdrom_ti ti;

		if ((st = verify_area(VERIFY_READ, (void *) arg, sizeof ti)))
			return st;
		memcpy_fromfs(&ti, (void *) arg, sizeof ti);
		if (ti.cdti_trk0 < DiskInfo.first
			|| ti.cdti_trk0 > DiskInfo.last
			|| ti.cdti_trk1 < ti.cdti_trk0)
			return -EINVAL;
		if (ti.cdti_trk1 > DiskInfo.last)
			ti.cdti_trk1 = DiskInfo.last;
		opt_Play.start = Toc[ti.cdti_trk0].diskTime;
		opt_Play.end = Toc[ti.cdti_trk1 + 1].diskTime;
#ifdef DEBUG_VFS
		printk("optcd: play %02x:%02x.%02x to %02x:%02x.%02x\n",
			opt_Play.start.min,
			opt_Play.start.sec,
			opt_Play.start.frame,
			opt_Play.end.min,
			opt_Play.end.sec,
			opt_Play.end.frame);
#endif
		if (optPlayCmd(COMPLAY, &opt_Play) < 0) {
			optAudioStatus = CDROM_AUDIO_ERROR;
			return -EIO;
		}
		optAudioStatus = CDROM_AUDIO_PLAY;
		break;
	}
	case CDROMREADTOCHDR: {		/* Read the table of contents header. */
		int st;
		struct cdrom_tochdr tocHdr;

		if ((st = verify_area(VERIFY_WRITE,(void *)arg,sizeof tocHdr)))
			return st;
		if (!optTocUpToDate)
			optGetDiskInfo();
		tocHdr.cdth_trk0 = DiskInfo.first;
		tocHdr.cdth_trk1 = DiskInfo.last;
		memcpy_tofs((void *) arg, &tocHdr, sizeof tocHdr);
		break;
	}
	case CDROMREADTOCENTRY: {	/* Read a table of contents entry. */
		int st;
		struct cdrom_tocentry entry;
		struct opt_Toc *tocPtr;

		if ((st = verify_area(VERIFY_READ, (void *) arg, sizeof entry)))
			return st;
		if ((st = verify_area(VERIFY_WRITE, (void *) arg, sizeof entry)))
			return st;
		memcpy_fromfs(&entry, (void *) arg, sizeof entry);
		if (!optTocUpToDate)
			optGetDiskInfo();
		if (entry.cdte_track == CDROM_LEADOUT)
			tocPtr = &Toc[DiskInfo.last + 1];
		else if (entry.cdte_track > DiskInfo.last
			|| entry.cdte_track < DiskInfo.first)
			return -EINVAL;
		else
			tocPtr = &Toc[entry.cdte_track];
		entry.cdte_adr = tocPtr -> ctrl_addr;
		entry.cdte_ctrl = tocPtr -> ctrl_addr >> 4;
		switch (entry.cdte_format) {
		case CDROM_LBA:
			entry.cdte_addr.lba = msf2hsg(&tocPtr -> diskTime);
			break;
		case CDROM_MSF:
			entry.cdte_addr.msf.minute =
				bcd2bin(tocPtr -> diskTime.min);
			entry.cdte_addr.msf.second =
				bcd2bin(tocPtr -> diskTime.sec);
			entry.cdte_addr.msf.frame =
				bcd2bin(tocPtr -> diskTime.frame);
			break;
		default:
			return -EINVAL;
		}
		memcpy_tofs((void *) arg, &entry, sizeof entry);
		break;
	}
	case CDROMSTOP:
		optCmd(COMSTOP);
		optAudioStatus = CDROM_AUDIO_NO_STATUS;
		break;
	case CDROMSTART:
		optCmd(COMCLOSE);	/* What else can we do? */
		break;
	case CDROMEJECT:
		optCmd(COMUNLOCK);
		optCmd(COMOPEN);
		break;
	case CDROMVOLCTRL: {
		int st;
		struct cdrom_volctrl volctrl;

		if ((st = verify_area(VERIFY_READ, (void *) arg,
				sizeof(volctrl))))
			return st;
		memcpy_fromfs(&volctrl, (char *) arg, sizeof(volctrl));
		opt_Play.start.min = 0x10;
		opt_Play.start.sec = 0x32;
		opt_Play.start.frame = volctrl.channel0;
		opt_Play.end.min = volctrl.channel1;
		opt_Play.end.sec = volctrl.channel2;
		opt_Play.end.frame = volctrl.channel3;
		if (optPlayCmd(COMCHCTRL, &opt_Play) < 0)
			return -EIO;
		break;
	}
	case CDROMSUBCHNL: {	/* Get subchannel info */
		int st;
		struct cdrom_subchnl subchnl;
		struct opt_Toc qInfo;

		if ((st = verify_area(VERIFY_READ,
				(void *) arg, sizeof subchnl)))
			return st;
		if ((st = verify_area(VERIFY_WRITE,
				(void *) arg, sizeof subchnl)))
			return st;
		memcpy_fromfs(&subchnl, (void *) arg, sizeof subchnl);
		if (optGetQChannelInfo(&qInfo) < 0)
			return -EIO;
		subchnl.cdsc_audiostatus = optAudioStatus;
		subchnl.cdsc_adr = qInfo.ctrl_addr;
		subchnl.cdsc_ctrl = qInfo.ctrl_addr >> 4;
		subchnl.cdsc_trk = bcd2bin(qInfo.track);
		subchnl.cdsc_ind = bcd2bin(qInfo.pointIndex);
		switch (subchnl.cdsc_format) {
		case CDROM_LBA:
			subchnl.cdsc_absaddr.lba = msf2hsg(&qInfo.diskTime);
			subchnl.cdsc_reladdr.lba = msf2hsg(&qInfo.trackTime);
			break;
		case CDROM_MSF:
			subchnl.cdsc_absaddr.msf.minute =
				bcd2bin(qInfo.diskTime.min);
			subchnl.cdsc_absaddr.msf.second =
				bcd2bin(qInfo.diskTime.sec);
			subchnl.cdsc_absaddr.msf.frame =
				bcd2bin(qInfo.diskTime.frame);
			subchnl.cdsc_reladdr.msf.minute =
				bcd2bin(qInfo.trackTime.min);
			subchnl.cdsc_reladdr.msf.second =
				bcd2bin(qInfo.trackTime.sec);
			subchnl.cdsc_reladdr.msf.frame =
				bcd2bin(qInfo.trackTime.frame);
			break;
		default:
			return -EINVAL;
		}
		memcpy_tofs((void *) arg, &subchnl, sizeof subchnl);
		break;
	}
	case CDROMREADMODE1: {
		int st;
		struct cdrom_msf msf;
		char buf[OPT_BLOCKSIZE];

		if ((st = verify_area(VERIFY_READ, (void *) arg, sizeof msf)))
			return st;
		if ((st = verify_area(VERIFY_WRITE,(void *)arg,OPT_BLOCKSIZE)))
			return st;
		memcpy_fromfs(&msf, (void *) arg, sizeof msf);
		opt_Play.start.min = bin2bcd(msf.cdmsf_min0);
		opt_Play.start.sec = bin2bcd(msf.cdmsf_sec0);
		opt_Play.start.frame = bin2bcd(msf.cdmsf_frame0);
		opt_Play.end.min = 0;
		opt_Play.end.sec = 0;
		opt_Play.end.frame = 1;	/* read only one frame */
		st = optReadCmd(COMREAD, &opt_Play);
#ifdef DEBUG_VFS
		printk("optcd: COMREAD status 0x%x\n", st);
#endif
		sleep_dten_low();	/* error checking here?? */
		optReadData(buf, OPT_BLOCKSIZE);
		memcpy_tofs((void *) arg, &buf, OPT_BLOCKSIZE);
		break;
	}
	case CDROMMULTISESSION:
		return -EINVAL; /* unluckily, not implemented yet */

	default:
		return -EINVAL;
	}
#ifdef DEBUG_VFS
	printk("optcd: exiting opt_ioctl\n");
#endif
	return 0;
}

static int optPresent = 0;
static int opt_open_count = 0;

/* Open device special file; check that a disk is in. */
static int opt_open(struct inode *ip, struct file *fp) {
#ifdef DEBUG_VFS
	printk("optcd: starting opt_open\n");
#endif
	if (!optPresent)
		return -ENXIO;		/* no hardware */
	if (!opt_open_count && opt_state == OPT_S_IDLE) {
		int st;
		opt_invalidate_buffers();
		if ((st = optGetStatus()) < 0)
			return -EIO;
		if (st & ST_DOOR_OPEN) {
			optCmd(COMCLOSE);			/* close door */
			if ((st = optGetStatus()) < 0)		/* try again */
				return -EIO;
		}
		if (st & (ST_DOOR_OPEN|ST_DRVERR)) {
			printk("optcd: no disk or door open\n");
			return -EIO;
		}
		if (optUpdateToc() < 0)
			return -EIO;
	}
	opt_open_count++;
	MOD_INC_USE_COUNT;
	optCmd(COMLOCK);		/* Lock door */
#ifdef DEBUG_VFS
	printk("optcd: exiting opt_open\n");
#endif
	return 0;
}

/* Release device special file; flush all blocks from the buffer cache */
static void opt_release(struct inode *ip, struct file *fp) {
#ifdef DEBUG_VFS
	printk("optcd: executing opt_release\n");
	printk("inode: %p, inode -> i_rdev: 0x%x, file: %p\n",
		ip, ip -> i_rdev, fp);
#endif
	if (!--opt_open_count) {
		opt_invalidate_buffers();
		sync_dev(ip -> i_rdev);
		invalidate_buffers(ip -> i_rdev);
		CLEAR_TIMER;
		optCmd(COMUNLOCK);	/* Unlock door */
	}
	MOD_DEC_USE_COUNT;
}


/* Initialisation */

static int version_ok(void) {
	char devname[100];
	int count, i, ch;

	if (optCmd(COMVERSION) < 0)
		return 0;
	if ((count = optGetData()) < 0)
		return 0;
	for (i = 0, ch = -1; count > 0; count--) {
		if ((ch = optGetData()) < 0)
			break;
		if (i < 99)
			devname[i++] = ch;
	}
	devname[i] = '\0';
	if (ch < 0)
		return 0;
	printk("optcd: Device %s detected\n", devname);
	return ((devname[0] == 'D')
	     && (devname[1] == 'O')
	     && (devname[2] == 'L')
	     && (devname[3] == 'P')
	     && (devname[4] == 'H')
	     && (devname[5] == 'I')
	     && (devname[6] == 'N'));
}


static struct file_operations opt_fops = {
	NULL,		/* lseek - default */
	block_read,	/* read - general block-dev read */
	block_write,	/* write - general block-dev write */
	NULL,		/* readdir - bad */
	NULL,		/* select */
	opt_ioctl,	/* ioctl */
	NULL,		/* mmap */
	opt_open,	/* open */
	opt_release,	/* release */
	NULL,		/* fsync */
	NULL,		/* fasync */
	NULL,		/* media change */
	NULL		/* revalidate */
};


/* Get kernel parameter when used as a kernel driver */
void optcd_setup(char *str, int *ints) {
	if (ints[0] > 0)
		optcd_port = ints[1];
}

/*
 * Test for presence of drive and initialize it. Called at boot time.
 */

int optcd_init(void) {
	if (optcd_port <= 0) {
		printk("optcd: no Optics Storage CDROM Initialization\n");
		return -EIO;
	}
	if (check_region(optcd_port, 4)) {
		printk("optcd: conflict, I/O port 0x%x already used\n",
			optcd_port);
		return -EIO;
	}

	if (!check_region(ISP16_DRIVE_SET_PORT, 5)) {
	/* If someone else has'nt already reserved these ports,
	   probe for an ISP16 interface card, and enable SONY mode
	   with no interrupts and no DMA. (As far as I know, all optics
	   drives come with a SONY interface.) */
  if ( (isp16_type=isp16_detect()) < 0 )
    printk( "No ISP16 cdrom interface found.\n" );
  else {
    u_char expected_drive;

    printk( "ISP16 cdrom interface (%s optional IDE) detected.\n",
      (isp16_type==2)?"with":"without" );

    expected_drive = (isp16_type?ISP16_SANYO1:ISP16_SANYO0);

    if ( isp16_config( optcd_port, ISP16_SONY, 0, 0 ) < 0 ) {
      printk( "ISP16 cdrom interface has not been properly configured.\n" );
      return -EIO;
    }
  }
	}

	if (!optResetDrive()) {
		printk("optcd: drive at 0x%x not ready\n", optcd_port);
		return -EIO;
	}
	if (!version_ok()) {
		printk("optcd: unknown drive detected; aborting\n");
		return -EIO;
	}
	if (optCmd(COMINITDOUBLE) < 0) {
		printk("optcd: cannot init double speed mode\n");
		return -EIO;
	}
	if (register_blkdev(MAJOR_NR, "optcd", &opt_fops) != 0)
	{
		printk("optcd: unable to get major %d\n", MAJOR_NR);
		return -EIO;
	}
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	read_ahead[MAJOR_NR] = 4;
	request_region(optcd_port, 4, "optcd");
	optPresent = 1;
	printk("optcd: 8000 AT CDROM at 0x%x\n", optcd_port);
	return 0;
}

#ifdef MODULE
void cleanup_module(void) {
	if (MOD_IN_USE) {
		printk("optcd: module in use - can't remove it.\n");
	return;
	}
	if ((unregister_blkdev(MAJOR_NR, "optcd") == -EINVAL)) {
		printk("optcd: what's that: can't unregister\n");
		return;
	}
	release_region(optcd_port, 4);
	printk("optcd: module released.\n");
}
#endif MODULE


/*
 * -- ISP16 detection and configuration
 *
 *    Copyright (c) 1995, Eric van der Maarel <maarel@marin.nl>
 *
 *    Version 0.5
 *
 *    Detect cdrom interface on ISP16 soundcard.
 *    Configure cdrom interface.
 *
 *    Algorithm for the card with no IDE support option taken
 *    from the CDSETUP.SYS driver for MSDOS,
 *    by OPTi Computers, version 2.03.
 *    Algorithm for the IDE supporting ISP16 as communicated
 *    to me by Vadim Model and Leo Spiekman.
 *
 *    Use, modifification or redistribution of this software is
 *    allowed under the terms of the GPL.
 *
 */


#define ISP16_IN(p) (outb(isp16_ctrl,ISP16_CTRL_PORT), inb(p))
#define ISP16_OUT(p,b) (outb(isp16_ctrl,ISP16_CTRL_PORT), outb(b,p))

static short
isp16_detect(void)
{

  if ( !( isp16_with_ide__detect() < 0 ) )
    return(2);
  else
    return( isp16_no_ide__detect() );
}

static short
isp16_no_ide__detect(void)
{
  u_char ctrl;
  u_char enable_cdrom;
  u_char io;
  short i = -1;

  isp16_ctrl = ISP16_NO_IDE__CTRL;
  isp16_enable_cdrom_port = ISP16_NO_IDE__ENABLE_CDROM_PORT;

  /* read' and write' are a special read and write, respectively */

  /* read' ISP16_CTRL_PORT, clear last two bits and write' back the result */
  ctrl = ISP16_IN( ISP16_CTRL_PORT ) & 0xFC;
  ISP16_OUT( ISP16_CTRL_PORT, ctrl );

  /* read' 3,4 and 5-bit from the cdrom enable port */
  enable_cdrom = ISP16_IN( ISP16_NO_IDE__ENABLE_CDROM_PORT ) & 0x38;

  if ( !(enable_cdrom & 0x20) ) {  /* 5-bit not set */
    /* read' last 2 bits of ISP16_IO_SET_PORT */
    io = ISP16_IN( ISP16_IO_SET_PORT ) & 0x03;
    if ( ((io&0x01)<<1) == (io&0x02) ) {  /* bits are the same */
      if ( io == 0 ) {  /* ...the same and 0 */
        i = 0;
        enable_cdrom |= 0x20;
      }
      else {  /* ...the same and 1 */  /* my card, first time 'round */
        i = 1;
        enable_cdrom |= 0x28;
      }
      ISP16_OUT( ISP16_NO_IDE__ENABLE_CDROM_PORT, enable_cdrom );
    }
    else {  /* bits are not the same */
      ISP16_OUT( ISP16_CTRL_PORT, ctrl );
      return(i); /* -> not detected: possibly incorrect conclusion */
    }
  }
  else if ( enable_cdrom == 0x20 )
    i = 0;
  else if ( enable_cdrom == 0x28 )  /* my card, already initialised */
    i = 1;

  ISP16_OUT( ISP16_CTRL_PORT, ctrl );

  return(i);
}

static short
isp16_with_ide__detect(void)
{
  u_char ctrl;
  u_char tmp;

  isp16_ctrl = ISP16_IDE__CTRL;
  isp16_enable_cdrom_port = ISP16_IDE__ENABLE_CDROM_PORT;

  /* read' and write' are a special read and write, respectively */

  /* read' ISP16_CTRL_PORT and save */
  ctrl = ISP16_IN( ISP16_CTRL_PORT );

  /* write' zero to the ctrl port and get response */
  ISP16_OUT( ISP16_CTRL_PORT, 0 );
  tmp = ISP16_IN( ISP16_CTRL_PORT );

  if ( tmp != 2 )  /* isp16 with ide option not detected */
    return(-1);

  /* restore ctrl port value */
  ISP16_OUT( ISP16_CTRL_PORT, ctrl );
  
  return(2);
}

static short
isp16_config( int base, u_char drive_type, int irq, int dma )
{
  u_char base_code;
  u_char irq_code;
  u_char dma_code;
  u_char i;

  if ( (drive_type == ISP16_MITSUMI) && (dma != 0) )
    printk( "Mitsumi cdrom drive has no dma support.\n" );

  switch (base) {
  case 0x340: base_code = ISP16_BASE_340; break;
  case 0x330: base_code = ISP16_BASE_330; break;
  case 0x360: base_code = ISP16_BASE_360; break;
  case 0x320: base_code = ISP16_BASE_320; break;
  default:
    printk( "Base address 0x%03X not supported by cdrom interface on ISP16.\n", base );
    return(-1);
  }
  switch (irq) {
  case 0: irq_code = ISP16_IRQ_X; break; /* disable irq */
  case 5: irq_code = ISP16_IRQ_5;
          printk( "Irq 5 shouldn't be used by cdrom interface on ISP16,"
            " due to possible conflicts with the soundcard.\n");
          break;
  case 7: irq_code = ISP16_IRQ_7;
          printk( "Irq 7 shouldn't be used by cdrom interface on ISP16,"
            " due to possible conflicts with the soundcard.\n");
          break;
  case 3: irq_code = ISP16_IRQ_3; break;
  case 9: irq_code = ISP16_IRQ_9; break;
  case 10: irq_code = ISP16_IRQ_10; break;
  case 11: irq_code = ISP16_IRQ_11; break;
  default:
    printk( "Irq %d not supported by cdrom interface on ISP16.\n", irq );
    return(-1);
  }
  switch (dma) {
  case 0: dma_code = ISP16_DMA_X; break;  /* disable dma */
  case 1: printk( "Dma 1 cannot be used by cdrom interface on ISP16,"
            " due to conflict with the soundcard.\n");
          return(-1); break;
  case 3: dma_code = ISP16_DMA_3; break;
  case 5: dma_code = ISP16_DMA_5; break;
  case 6: dma_code = ISP16_DMA_6; break;
  case 7: dma_code = ISP16_DMA_7; break;
  default:
    printk( "Dma %d not supported by cdrom interface on ISP16.\n", dma );
    return(-1);
  }

  if ( drive_type != ISP16_SONY && drive_type != ISP16_PANASONIC0 &&
    drive_type != ISP16_PANASONIC1 && drive_type != ISP16_SANYO0 &&
    drive_type != ISP16_SANYO1 && drive_type != ISP16_MITSUMI &&
    drive_type != ISP16_DRIVE_X ) {
    printk( "Drive type (code 0x%02X) not supported by cdrom"
     " interface on ISP16.\n", drive_type );
    return(-1);
  }

  /* set type of interface */
  i = ISP16_IN(ISP16_DRIVE_SET_PORT) & ISP16_DRIVE_SET_MASK;  /* clear some bits */
  ISP16_OUT( ISP16_DRIVE_SET_PORT, i|drive_type );

  /* enable cdrom on interface with ide support */
  if ( isp16_type > 1 )
    ISP16_OUT( isp16_enable_cdrom_port, ISP16_ENABLE_CDROM );

  /* set base address, irq and dma */
  i = ISP16_IN(ISP16_IO_SET_PORT) & ISP16_IO_SET_MASK;  /* keep some bits */
  ISP16_OUT( ISP16_IO_SET_PORT, i|base_code|irq_code|dma_code );

  return(0);
}
