#define AZT_VERSION "V1.0"
/*      $Id: aztcd.c,v 1.0 1995/03/25 08:27:11 root Exp $
	linux/drivers/block/aztcd.c - AztechCD268 CDROM driver

	Copyright (C) 1994,1995 Werner Zimmermann (zimmerma@rz.fht-esslingen.de)

	based on Mitsumi CDROM driver by  Martin Hariss and preworks by
	Eberhard Moenkeberg; contains contributions by Joe Nardone and Robby 
	Schirmer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2, or (at your option)
	any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

	HISTORY
	V0.0    Adaption to Adaptec CD268-01A Version 1.3
		Version is PRE_ALPHA, unresolved points:
		1. I use busy wait instead of timer wait in STEN_LOW,DTEN_LOW
		   thus driver causes CPU overhead and is very slow 
		2. could not find a way to stop the drive, when it is
		   in data read mode, therefore I had to set
		   msf.end.min/sec/frame to 0:0:1 (in azt_poll); so only one
		   frame can be read in sequence, this is also the reason for
		3. getting 'timeout in state 4' messages, but nevertheless
		   it works
		W.Zimmermann, Oct. 31, 1994
	V0.1    Version is ALPHA, problems #2 and #3 resolved.  
		W.Zimmermann, Nov. 3, 1994
	V0.2    Modification to some comments, debugging aids for partial test
		with Borland C under DOS eliminated. Timer interrupt wait 
		STEN_LOW_WAIT additionally to busy wait for STEN_LOW implemented; 
		use it only for the 'slow' commands (ACMD_GET_Q_CHANNEL, ACMD_
		SEEK_TO_LEAD_IN), all other commands are so 'fast', that busy 
		waiting seems better to me than interrupt rescheduling.
		Besides that, when used in the wrong place, STEN_LOW_WAIT causes
		kernel panic.
		In function aztPlay command ACMD_PLAY_AUDIO added, should make
		audio functions work. The Aztech drive needs different commands
		to read data tracks and play audio tracks.
		W.Zimmermann, Nov. 8, 1994
	V0.3    Recognition of missing drive during boot up improved (speeded up).
		W.Zimmermann, Nov. 13, 1994
	V0.35   Rewrote the control mechanism in azt_poll (formerly mcd_poll) 
		including removal of all 'goto' commands. :-); 
		J. Nardone, Nov. 14, 1994
	V0.4    Renamed variables and constants to 'azt' instead of 'mcd'; had
		to make some "compatibility" defines in azt.h; please note,
		that the source file was renamed to azt.c, the include file to
		azt.h                
		Speeded up drive recognition during init (will be a little bit 
		slower than before if no drive is installed!); suggested by
		Robby Schirmer.
		read_count declared volatile and set to AZT_BUF_SIZ to make
		drive faster (now 300kB/sec, was 60kB/sec before, measured
		by 'time dd if=/dev/cdrom of=/dev/null bs=2048 count=4096';
		different AZT_BUF_SIZes were test, above 16 no further im-
		provement seems to be possible; suggested by E.Moenkeberg.
		W.Zimmermann, Nov. 18, 1994
	V0.42   Included getAztStatus command in GetQChannelInfo() to allow
		reading Q-channel info on audio disks, if drive is stopped, 
		and some other bug fixes in the audio stuff, suggested by 
		Robby Schirmer.
		Added more ioctls (reading data in mode 1 and mode 2).
		Completely removed the old azt_poll() routine.
		Detection of ORCHID CDS-3110 in aztcd_init implemented.
		Additional debugging aids (see the readme file).
		W.Zimmermann, Dec. 9, 1994  
	V0.50   Autodetection of drives implemented.
		W.Zimmermann, Dec. 12, 1994
	V0.52   Prepared for including in the standard kernel, renamed most
		variables to contain 'azt', included autoconf.h
		W.Zimmermann, Dec. 16, 1994        
	V0.6    Version for being included in the standard Linux kernel.
		Renamed source and header file to aztcd.c and aztcd.h
		W.Zimmermann, Dec. 24, 1994
	V0.7    Changed VERIFY_READ to VERIFY_WRITE in aztcd_ioctl, case
		CDROMREADMODE1 and CDROMREADMODE2; bug fix in the ioctl,
		which causes kernel crashes when playing audio, changed 
		include-files (config.h instead of autoconf.h, removed
		delay.h)
		W.Zimmermann, Jan. 8, 1995
	V0.72   Some more modifications for adaption to the standard kernel.
		W.Zimmermann, Jan. 16, 1995
        V0.80   aztcd is now part of the standard kernel since version 1.1.83.
                Modified the SET_TIMER and CLEAR_TIMER macros to comply with
                the new timer scheme.
                W.Zimmermann, Jan. 21, 1995
        V0.90   Included CDROMVOLCTRL, but with my Aztech drive I can only turn
                the channels on and off. If it works better with your drive, 
                please mail me. Also implemented ACMD_CLOSE for CDROMSTART.
                W.Zimmermann, Jan. 24, 1995
        V1.00   Implemented close and lock tray commands. Patches supplied by
		Frank Racis        
                Added support for loadable MODULEs, so aztcd can now also be
                loaded by insmod and removed by rmmod during run time
                Werner Zimmermann, Mar. 24, 95
	NOTE: 
	Points marked with ??? are questionable !
*/
#include <linux/major.h>
#include <linux/config.h>

#ifdef MODULE
# include <linux/module.h>
# include <linux/version.h>
# ifndef CONFIG_MODVERSIONS
    char kernel_version[]= UTS_RELEASE;
# endif
#endif

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/cdrom.h>
#include <linux/ioport.h>
#include <linux/string.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR AZTECH_CDROM_MAJOR 

#ifdef MODULE
# include "/usr/src/linux/drivers/block/blk.h"
#else
# include "blk.h"
# define MOD_INC_USE_COUNT
# define MOD_DEC_USE_COUNT
#endif

#include <linux/aztcd.h>

static int aztPresent = 0;

#if 0
#define AZT_TEST1 /* <int-..> */
#define AZT_TEST2 /* do_aztcd_request */
#define AZT_TEST3 /* AZT_S_state */
#define AZT_TEST4 /* QUICK_LOOP-counter */
#define AZT_TEST5 /* port(1) state */
#define AZT_DEBUG
#endif

#define CURRENT_VALID \
  (CURRENT && MAJOR(CURRENT -> dev) == MAJOR_NR && CURRENT -> cmd == READ \
   && CURRENT -> sector != -1)

#define AFL_STATUSorDATA (AFL_STATUS | AFL_DATA)
#define AZT_BUF_SIZ 16

static volatile int azt_transfer_is_active=0;

static char azt_buf[2048*AZT_BUF_SIZ];  /*buffer for block size conversion*/
#ifdef AZT_PRIVATE_IOCTLS
static char buf[2336];                  /*separate buffer for the ioctls*/
#endif

static volatile int azt_buf_bn[AZT_BUF_SIZ], azt_next_bn;
static volatile int azt_buf_in, azt_buf_out = -1;
static volatile int azt_error=0;
static int azt_open_count=0;
enum azt_state_e {
  AZT_S_IDLE,    /* 0 */
  AZT_S_START,   /* 1 */
  AZT_S_MODE,    /* 2 */
  AZT_S_READ,    /* 3 */
  AZT_S_DATA,    /* 4 */
  AZT_S_STOP,    /* 5 */
  AZT_S_STOPPING /* 6 */
};
static volatile enum azt_state_e azt_state = AZT_S_IDLE;
#ifdef AZT_TEST3
static volatile enum azt_state_e azt_state_old = AZT_S_STOP;  
static volatile int azt_st_old = 0;
#endif

static int azt_mode = -1;
static int ACMD_DATA_READ= ACMD_PLAY_READ;
static volatile int azt_read_count = 1;

#define READ_TIMEOUT 3000

#define azt_port aztcd  /*needed for the modutils*/
static short azt_port = AZT_BASE_ADDR;

static char  azt_cont = 0;
static char  azt_init_end = 0;

static int AztTimeout, AztTries;
static struct wait_queue *azt_waitq = NULL; 
static struct timer_list delay_timer = { NULL, NULL, 0, 0, NULL };

static struct azt_DiskInfo DiskInfo;
static struct azt_Toc Toc[MAX_TRACKS];
static struct azt_Play_msf azt_Play;

static int  aztAudioStatus = CDROM_AUDIO_NO_STATUS;
static char aztDiskChanged = 1;
static char aztTocUpToDate = 0;


static void azt_transfer(void);
static void azt_poll(void);
static void azt_invalidate_buffers(void);
static void do_aztcd_request(void);
static void azt_hsg2msf(long hsg, struct msf *msf);
static void azt_bin2bcd(unsigned char *p);
static int  azt_bcd2bin(unsigned char bcd);
static int  aztStatus(void);
static int  getAztStatus(void);
static int  aztSendCmd(int cmd);
static int  sendAztCmd(int cmd, struct azt_Play_msf *params);
static int  aztGetQChannelInfo(struct azt_Toc *qp);
static int  aztUpdateToc(void);
static int  aztGetDiskInfo(void);
static int  aztGetToc(void);
static int  aztGetValue(unsigned char *result);
static void aztStatTimer(void);
static void aztCloseDoor(void);
static void aztLockDoor(void);
static void aztUnlockDoor(void);

static unsigned char aztIndatum;
static unsigned long aztTimeOutCount;

/* Macros for the drive hardware interface handshake, these macros use
   busy waiting */
/* Wait for OP_OK = drive answers with AFL_OP_OK after receiving a command*/
# define OP_OK op_ok()
void op_ok(void)
{ aztTimeOutCount=0; 
  do { aztIndatum=inb(DATA_PORT);
       aztTimeOutCount++;
       if (aztTimeOutCount>=AZT_TIMEOUT)
	{ printk("aztcd: Error Wait OP_OK\n");
	  break;
	}
     } while (aztIndatum!=AFL_OP_OK);
}

/* Wait for PA_OK = drive answers with AFL_PA_OK after receiving parameters*/
# define PA_OK pa_ok()
void pa_ok(void)
{ aztTimeOutCount=0; 
  do { aztIndatum=inb(DATA_PORT);
       aztTimeOutCount++;
       if (aztTimeOutCount>=AZT_TIMEOUT)
	{ printk("aztcd: Error Wait PA_OK\n");
	  break;
	}
     } while (aztIndatum!=AFL_PA_OK);
}
     
/* Wait for STEN=Low = handshake signal 'AFL_.._OK available or command executed*/
# define STEN_LOW  sten_low()
void sten_low(void)
{ aztTimeOutCount=0; 
  do { aztIndatum=inb(STATUS_PORT);
       aztTimeOutCount++;
       if (aztTimeOutCount>=AZT_TIMEOUT)
	{ if (azt_init_end) printk("aztcd: Error Wait STEN_LOW\n");
	  break;
	}
     } while (aztIndatum&AFL_STATUS);
}

/* Wait for DTEN=Low = handshake signal 'Data available'*/
# define DTEN_LOW dten_low()
void dten_low(void)
{ aztTimeOutCount=0; 
  do { aztIndatum=inb(STATUS_PORT);
       aztTimeOutCount++;
       if (aztTimeOutCount>=AZT_TIMEOUT)
	{ printk("aztcd: Error Wait DTEN_OK\n");
	  break;
	}
     } while (aztIndatum&AFL_DATA);
}

/* 
 * Macro for timer wait on STEN=Low, should only be used for 'slow' commands;
 * may cause kernel panic when used in the wrong place
*/
#define STEN_LOW_WAIT   statusAzt()
void statusAzt(void)
{ AztTimeout = AZT_STATUS_DELAY;
  SET_TIMER(aztStatTimer, 1); 
  sleep_on(&azt_waitq);    
  if (AztTimeout <= 0) printk("aztcd: Error Wait STEN_LOW_WAIT\n");
  return;
}

static void aztStatTimer(void)
{       if (!(inb(STATUS_PORT) & AFL_STATUS))
	{       wake_up(&azt_waitq);
		return;
	}
	AztTimeout--;
	if (AztTimeout <= 0)
	{       wake_up(&azt_waitq);
		return;
	}
	SET_TIMER(aztStatTimer, 1);
}


void aztcd_setup(char *str, int *ints)
{  if (ints[0] > 0)
      azt_port = ints[1];
   if (ints[0] > 1)
      azt_cont = ints[2];
}

/*
 * Subroutines to automatically close the door (tray) and 
 * lock it closed when the cd is mounted.  Leave the tray
 * locking as an option
 */
static void aztCloseDoor(void)
{
  aztSendCmd(ACMD_CLOSE);
  STEN_LOW;
  return;
}

static void aztLockDoor(void)
{
#ifdef AZT_ALLOW_TRAY_LOCK
  aztSendCmd(ACMD_LOCK);
  STEN_LOW;
#endif
  return;
}

static void aztUnlockDoor(void)
{
#ifdef AZT_ALLOW_TRAY_LOCK
  aztSendCmd(ACMD_UNLOCK);
  STEN_LOW;
#endif
  return;
}

/* 
 * Send a single command, return -1 on error, else 0
*/
static int aztSendCmd(int cmd)
{  unsigned char data;
   int retry;

#ifdef AZT_DEBUG
   printk("aztcd: Executing command %x\n",cmd);
#endif
   outb(POLLED,MODE_PORT);
   do { if (inb(STATUS_PORT)&AFL_STATUS) break;
	inb(DATA_PORT);    /* if status left from last command, read and */
      } while (1);         /* discard it */
   do { if (inb(STATUS_PORT)&AFL_DATA) break;
	inb(DATA_PORT);    /* if data left from last command, read and */
      } while (1);         /* discard it */
   for (retry=0;retry<AZT_RETRY_ATTEMPTS;retry++)
     { outb((unsigned char) cmd,CMD_PORT);
       STEN_LOW;
       data=inb(DATA_PORT);
       if (data==AFL_OP_OK)
	 { return 0;}           /*OP_OK?*/
       if (data==AFL_OP_ERR)
	 { STEN_LOW;
	   data=inb(DATA_PORT);
	   printk("### Error 1 aztcd: aztSendCmd %x  Error Code %x\n",cmd,data);
	 }
     }
   if (retry>=AZT_RETRY_ATTEMPTS)
     { printk("### Error 2 aztcd: aztSendCmd %x \n",cmd);
       azt_error=0xA5;
     }
   return -1;
}

/*
 * Send a play or read command to the drive, return -1 on error, else 0
*/
static int sendAztCmd(int cmd, struct azt_Play_msf *params)
{  unsigned char data;
   int retry;

#ifdef AZT_DEBUG
   printk("start=%02x:%02x:%02x  end=%02x:%02x:%02x\n", \
	   params->start.min, params->start.sec, params->start.frame, \
	   params->end.min,   params->end.sec,   params->end.frame);
#endif   
   for (retry=0;retry<AZT_RETRY_ATTEMPTS;retry++)
     { aztSendCmd(cmd);
       outb(params -> start.min,CMD_PORT);
       outb(params -> start.sec,CMD_PORT);
       outb(params -> start.frame,CMD_PORT);
       outb(params -> end.min,CMD_PORT);
       outb(params -> end.sec,CMD_PORT);
       outb(params -> end.frame,CMD_PORT);
       STEN_LOW;
       data=inb(DATA_PORT);
       if (data==AFL_PA_OK)
	 { return 0;}           /*PA_OK ?*/
       if (data==AFL_PA_ERR)
	 { STEN_LOW;
	   data=inb(DATA_PORT);
	   printk("### Error 1 aztcd: sendAztCmd %x  Error Code %x\n",cmd,data);
	 }
     }
   if (retry>=AZT_RETRY_ATTEMPTS)
     { printk("### Error 2 aztcd: sendAztCmd %x\n ",cmd);
       azt_error=0xA5;
     }
   return -1;
}


/* 
 * Checking if the media has been changed not yet implemented
*/
static int check_aztcd_media_change(dev_t full_dev)
{ return 0;
}


/* used in azt_poll to poll the status, expects another program to issue a 
 * ACMD_GET_STATUS directly before 
 */
static int aztStatus(void)  
{       int st;
	int i;

	i = inb(STATUS_PORT) & AFL_STATUS;   /* is STEN=0?    ???*/
	if (!i)
	{
		st = inb(DATA_PORT) & 0xFF;
		return st;
	}
	else
		return -1;
}

/*
 * Get the drive status
 */
static int getAztStatus(void)
{       int st;

	if (aztSendCmd(ACMD_GET_STATUS)) return -1;
	STEN_LOW;
	st = inb(DATA_PORT) & 0xFF;
#ifdef AZT_DEBUG
	printk("aztcd: Status = %x\n",st);
#endif
	if ((st == 0xFF)||(st&AST_CMD_CHECK))
	 { printk("aztcd: AST_CMD_CHECK error or no status available\n");
	   return -1;
	 }

	if (((st&AST_MODE_BITS)!=AST_BUSY) && (aztAudioStatus == CDROM_AUDIO_PLAY))
		/* XXX might be an error? look at q-channel? */
		aztAudioStatus = CDROM_AUDIO_COMPLETED;

	if (st & AST_DSK_CHG)
	{
		aztDiskChanged = 1;
		aztTocUpToDate = 0;
		aztAudioStatus = CDROM_AUDIO_NO_STATUS;
	}
	return st;
}


/*
 * Send a 'Play' command and get the status.  Use only from the top half.
 */
static int aztPlay(struct azt_Play_msf *arg)
{       if (sendAztCmd(ACMD_PLAY_AUDIO, arg) < 0) return -1;
	return 0;
}


long azt_msf2hsg(struct msf *mp)
{
#ifdef AZT_DEBUG
	if (mp->min  >=70) printk("aztcd: Error msf2hsg address Minutes\n");
	if (mp->sec  >=60) printk("aztcd: Error msf2hsg address Seconds\n");
	if (mp->frame>=75) printk("aztcd: Error msf2hsg address Frames\n");
#endif
	return azt_bcd2bin(mp -> frame)
		+ azt_bcd2bin(mp -> sec) * 75
		+ azt_bcd2bin(mp -> min) * 4500
		- 150;
}

static int aztcd_ioctl(struct inode *ip, struct file *fp, unsigned int cmd, unsigned long arg)
{       int i, st;
	struct azt_Toc qInfo;
	struct cdrom_ti ti;
	struct cdrom_tochdr tocHdr;
	struct cdrom_msf msf;
	struct cdrom_tocentry entry;
	struct azt_Toc *tocPtr;            
	struct cdrom_subchnl subchnl;
        struct cdrom_volctrl volctrl;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztcd_ioctl - Command:%x\n",cmd);
#endif
	if (!ip) return -EINVAL;
	if (getAztStatus()<0) return -EIO;
	if (!aztTocUpToDate)
	{ if ((i=aztUpdateToc())<0) return i; /* error reading TOC */
	}

	switch (cmd)
	{
	case CDROMSTART:     /* Spin up the drive. Don't know, what to do,
	                        at least close the tray */
#ifdef AZT_PRIVATE_IOCTLS 
	        if (aztSendCmd(ACMD_CLOSE)) return -1;
	        STEN_LOW_WAIT;
#endif
		break;
	case CDROMSTOP:      /* Spin down the drive */
		if (aztSendCmd(ACMD_STOP)) return -1;
		STEN_LOW_WAIT;
		/* should we do anything if it fails? */
		aztAudioStatus = CDROM_AUDIO_NO_STATUS;
		break;
	case CDROMPAUSE:     /* Pause the drive */
                if (aztAudioStatus != CDROM_AUDIO_PLAY) return -EINVAL; 

		if (aztGetQChannelInfo(&qInfo) < 0)
		{ /* didn't get q channel info */
		  aztAudioStatus = CDROM_AUDIO_NO_STATUS;
		  return 0;
		}
		azt_Play.start = qInfo.diskTime;        /* remember restart point */
		if (aztSendCmd(ACMD_PAUSE)) return -1;
		STEN_LOW_WAIT;
		aztAudioStatus = CDROM_AUDIO_PAUSED;
		break;
	case CDROMRESUME:    /* Play it again, Sam */
		if (aztAudioStatus != CDROM_AUDIO_PAUSED) return -EINVAL;
		/* restart the drive at the saved position. */
		i = aztPlay(&azt_Play);
		if (i < 0)
		{ aztAudioStatus = CDROM_AUDIO_ERROR;
		  return -EIO;
		}
		aztAudioStatus = CDROM_AUDIO_PLAY;
		break;
	case CDROMPLAYTRKIND:     /* Play a track.  This currently ignores index. */
		st = verify_area(VERIFY_READ, (void *) arg, sizeof ti);
		if (st) return st;
		memcpy_fromfs(&ti, (void *) arg, sizeof ti);
		if (ti.cdti_trk0 < DiskInfo.first
			|| ti.cdti_trk0 > DiskInfo.last
			|| ti.cdti_trk1 < ti.cdti_trk0)
		{ return -EINVAL;
		}
		if (ti.cdti_trk1 > DiskInfo.last)
		   ti. cdti_trk1 = DiskInfo.last;
		azt_Play.start = Toc[ti.cdti_trk0].diskTime;
		azt_Play.end = Toc[ti.cdti_trk1 + 1].diskTime;
#ifdef AZT_DEBUG
printk("aztcd play: %02x:%02x.%02x to %02x:%02x.%02x\n",
	azt_Play.start.min, azt_Play.start.sec, azt_Play.start.frame,
	azt_Play.end.min, azt_Play.end.sec, azt_Play.end.frame);
#endif
		i = aztPlay(&azt_Play);
		if (i < 0)
		{ aztAudioStatus = CDROM_AUDIO_ERROR;
		  return -EIO;
		}
		aztAudioStatus = CDROM_AUDIO_PLAY;
		break;
	case CDROMPLAYMSF:   /* Play starting at the given MSF address. */
/*              if (aztAudioStatus == CDROM_AUDIO_PLAY) 
		{ if (aztSendCmd(ACMD_STOP)) return -1;
		  STEN_LOW;
		  aztAudioStatus = CDROM_AUDIO_NO_STATUS;
		}
*/
		st = verify_area(VERIFY_READ, (void *) arg, sizeof msf);
		if (st) return st;
		memcpy_fromfs(&msf, (void *) arg, sizeof msf);
		/* convert to bcd */
		azt_bin2bcd(&msf.cdmsf_min0);
		azt_bin2bcd(&msf.cdmsf_sec0);
		azt_bin2bcd(&msf.cdmsf_frame0);
		azt_bin2bcd(&msf.cdmsf_min1);
		azt_bin2bcd(&msf.cdmsf_sec1);
		azt_bin2bcd(&msf.cdmsf_frame1);
		azt_Play.start.min = msf.cdmsf_min0;
		azt_Play.start.sec = msf.cdmsf_sec0;
		azt_Play.start.frame = msf.cdmsf_frame0;
		azt_Play.end.min = msf.cdmsf_min1;
		azt_Play.end.sec = msf.cdmsf_sec1;
		azt_Play.end.frame = msf.cdmsf_frame1;
#ifdef AZT_DEBUG
printk("aztcd play: %02x:%02x.%02x to %02x:%02x.%02x\n",
azt_Play.start.min, azt_Play.start.sec, azt_Play.start.frame,
azt_Play.end.min, azt_Play.end.sec, azt_Play.end.frame);
#endif
		i = aztPlay(&azt_Play);
		if (i < 0)
		{ aztAudioStatus = CDROM_AUDIO_ERROR;
		  return -EIO;
		}
		aztAudioStatus = CDROM_AUDIO_PLAY;
		break;

	case CDROMREADTOCHDR:        /* Read the table of contents header */
		st = verify_area(VERIFY_WRITE, (void *) arg, sizeof tocHdr);
		if (st) return st;
		tocHdr.cdth_trk0 = DiskInfo.first;
		tocHdr.cdth_trk1 = DiskInfo.last;
		memcpy_tofs((void *) arg, &tocHdr, sizeof tocHdr);
		break;
	case CDROMREADTOCENTRY:      /* Read an entry in the table of contents */
		st = verify_area(VERIFY_READ, (void *) arg, sizeof entry);
		if (st) return st;
		st = verify_area(VERIFY_WRITE, (void *) arg, sizeof entry);
		if (st) return st;
		memcpy_fromfs(&entry, (void *) arg, sizeof entry);
		if (!aztTocUpToDate) aztGetDiskInfo();
		if (entry.cdte_track == CDROM_LEADOUT)
		  tocPtr = &Toc[DiskInfo.last + 1];   /* ??? */
		else if (entry.cdte_track > DiskInfo.last
				|| entry.cdte_track < DiskInfo.first)
		{ return -EINVAL;
		}
		else 
		  tocPtr = &Toc[entry.cdte_track];
		entry.cdte_adr = tocPtr -> ctrl_addr;
		entry.cdte_ctrl = tocPtr -> ctrl_addr >> 4;
		if (entry.cdte_format == CDROM_LBA)
		  entry.cdte_addr.lba = azt_msf2hsg(&tocPtr -> diskTime);
		else if (entry.cdte_format == CDROM_MSF)
		{ entry.cdte_addr.msf.minute = azt_bcd2bin(tocPtr -> diskTime.min);
		  entry.cdte_addr.msf.second = azt_bcd2bin(tocPtr -> diskTime.sec);
		  entry.cdte_addr.msf.frame  = azt_bcd2bin(tocPtr -> diskTime.frame);
		}
		else
		{ return -EINVAL;
		}
		memcpy_tofs((void *) arg, &entry, sizeof entry);
		break;
	case CDROMSUBCHNL:   /* Get subchannel info */
		st = verify_area(VERIFY_READ, (void *) arg, sizeof subchnl);
		if (st) return st;
		st = verify_area(VERIFY_WRITE, (void *) arg, sizeof subchnl);
		if (st) return st;
		memcpy_fromfs(&subchnl, (void *) arg, sizeof subchnl);
		if (aztGetQChannelInfo(&qInfo) < 0)
		  return -EIO;
		subchnl.cdsc_audiostatus = aztAudioStatus;
		subchnl.cdsc_adr = qInfo.ctrl_addr;
		subchnl.cdsc_ctrl = qInfo.ctrl_addr >> 4;
		subchnl.cdsc_trk = azt_bcd2bin(qInfo.track);
		subchnl.cdsc_ind = azt_bcd2bin(qInfo.pointIndex);
		if (subchnl.cdsc_format == CDROM_LBA)
		{ subchnl.cdsc_absaddr.lba = azt_msf2hsg(&qInfo.diskTime);
		  subchnl.cdsc_reladdr.lba = azt_msf2hsg(&qInfo.trackTime);
		}
		else if (subchnl.cdsc_format == CDROM_MSF)
		{ subchnl.cdsc_absaddr.msf.minute = azt_bcd2bin(qInfo.diskTime.min);
		  subchnl.cdsc_absaddr.msf.second = azt_bcd2bin(qInfo.diskTime.sec);
		  subchnl.cdsc_absaddr.msf.frame = azt_bcd2bin(qInfo.diskTime.frame);
		  subchnl.cdsc_reladdr.msf.minute = azt_bcd2bin(qInfo.trackTime.min);
		  subchnl.cdsc_reladdr.msf.second = azt_bcd2bin(qInfo.trackTime.sec);
		  subchnl.cdsc_reladdr.msf.frame  = azt_bcd2bin(qInfo.trackTime.frame);
		}
		else
		  return -EINVAL;
		memcpy_tofs((void *) arg, &subchnl, sizeof subchnl);
		break;
	case CDROMVOLCTRL:   /* Volume control 
	 * With my Aztech CD268-01A volume control does not work, I can only
	   turn the channels on (any value !=0) or off (value==0). Maybe it
           works better with your drive */
                st=verify_area(VERIFY_READ,(void *) arg, sizeof(volctrl));
                if (st) return (st);
                memcpy_fromfs(&volctrl,(char *) arg,sizeof(volctrl));
		azt_Play.start.min = 0x21;
		azt_Play.start.sec = 0x84;
		azt_Play.start.frame = volctrl.channel0;
		azt_Play.end.min =     volctrl.channel1;
		azt_Play.end.sec =     volctrl.channel2;
		azt_Play.end.frame =   volctrl.channel3;
                sendAztCmd(ACMD_SET_VOLUME, &azt_Play);
                STEN_LOW_WAIT;
                break;
	case CDROMEJECT:
                aztUnlockDoor(); /* Assume user knows what they're doing */
	       /* all drives can at least stop! */
		if (aztAudioStatus == CDROM_AUDIO_PLAY) 
		{ if (aztSendCmd(ACMD_STOP)) return -1;
		  STEN_LOW_WAIT;
		}
		if (aztSendCmd(ACMD_EJECT)) return -1;
		aztAudioStatus = CDROM_AUDIO_NO_STATUS;
		break;
	case CDROMREADMODE1: /*read data in mode 1 (2048 Bytes)*/
	case CDROMREADMODE2: /*read data in mode 2 (2336 Bytes)*/
/*Take care, the following code is not compatible with other CD-ROM drivers,
  use it at your own risk with cdplay.c. Normally it is not activated, as 
  AZT_PRIVATE_IOCTLS is not defined
*/                  
#ifdef AZT_PRIVATE_IOCTLS 
		{ st = verify_area(VERIFY_READ,  (void *) arg, sizeof msf);
		  if (st) return st;
		  st = verify_area(VERIFY_WRITE, (void *) arg, sizeof buf);
		  if (st) return st;
		  memcpy_fromfs(&msf, (void *) arg, sizeof msf);
		  /* convert to bcd */
		  azt_bin2bcd(&msf.cdmsf_min0);
		  azt_bin2bcd(&msf.cdmsf_sec0);
		  azt_bin2bcd(&msf.cdmsf_frame0);
		  msf.cdmsf_min1=0;
		  msf.cdmsf_sec1=0;
		  msf.cdmsf_frame1=1; /*read only one frame*/
		  azt_Play.start.min = msf.cdmsf_min0;
		  azt_Play.start.sec = msf.cdmsf_sec0;
		  azt_Play.start.frame = msf.cdmsf_frame0;
		  azt_Play.end.min = msf.cdmsf_min1;
		  azt_Play.end.sec = msf.cdmsf_sec1;
		  azt_Play.end.frame = msf.cdmsf_frame1;
		  if (cmd==CDROMREADMODE1)
		  { sendAztCmd(ACMD_DATA_READ, &azt_Play);
		    DTEN_LOW;
		    insb(DATA_PORT,buf,2048);
		    memcpy_tofs((void *) arg, &buf, 2048);
		  }
		  else /*CDROMREADMODE2*/
		  { sendAztCmd(ACMD_DATA_READ_RAW, &azt_Play);
		    DTEN_LOW;
		    insb(DATA_PORT,buf,2336);
		    memcpy_tofs((void *) arg, &buf, 2336);
		  }
		 } 
#endif  /*end of incompatible code*/               
		break;
	default:
		return -EINVAL;
	}
#ifdef AZT_DEBUG
	printk("aztcd: exiting aztcd_ioctl\n");
#endif
	return 0;
}


/*
 * Take care of the different block sizes between cdrom and Linux.
 * When Linux gets variable block sizes this will probably go away.
 */
static void azt_transfer(void)
{ 
#ifdef AZT_TEST
  printk("aztcd: executing azt_transfer\n");
#endif
  if (CURRENT_VALID) {
    while (CURRENT -> nr_sectors) {
      int bn = CURRENT -> sector / 4;
      int i;
      for (i = 0; i < AZT_BUF_SIZ && azt_buf_bn[i] != bn; ++i)
	;
      if (i < AZT_BUF_SIZ) {
	int offs = (i * 4 + (CURRENT -> sector & 3)) * 512;
	int nr_sectors = 4 - (CURRENT -> sector & 3);
	if (azt_buf_out != i) {
	  azt_buf_out = i;
	  if (azt_buf_bn[i] != bn) {
	    azt_buf_out = -1;
	    continue;
	  }
	}
	if (nr_sectors > CURRENT -> nr_sectors)
	  nr_sectors = CURRENT -> nr_sectors;
	memcpy(CURRENT -> buffer, azt_buf + offs, nr_sectors * 512);
	CURRENT -> nr_sectors -= nr_sectors;
	CURRENT -> sector += nr_sectors;
	CURRENT -> buffer += nr_sectors * 512;
      } else {
	azt_buf_out = -1;
	break;
      }
    }
  }
}


static void do_aztcd_request(void)
{
#ifdef AZT_TEST
  printk(" do_aztcd_request(%ld+%ld)\n", CURRENT -> sector, CURRENT -> nr_sectors);
#endif
  azt_transfer_is_active = 1;
  while (CURRENT_VALID) {
    if (CURRENT->bh) {
      if (!CURRENT->bh->b_lock)
	panic(DEVICE_NAME ": block not locked");
    }
    azt_transfer();
    if (CURRENT -> nr_sectors == 0) {
      end_request(1);
    } else {
      azt_buf_out = -1;         /* Want to read a block not in buffer */
      if (azt_state == AZT_S_IDLE) {
	if (!aztTocUpToDate) {
	  if (aztUpdateToc() < 0) {
	    while (CURRENT_VALID)
	      end_request(0);
	    break;
	  }
	}
	azt_state = AZT_S_START;
	AztTries = 5;
	SET_TIMER(azt_poll, 1);
      }
      break;
    }
  }
  azt_transfer_is_active = 0;
#ifdef AZT_TEST2
  printk("azt_next_bn:%x  azt_buf_in:%x azt_buf_out:%x  azt_buf_bn:%x\n", \
	  azt_next_bn, azt_buf_in, azt_buf_out, azt_buf_bn[azt_buf_in]);
  printk(" do_aztcd_request ends\n");
#endif

}

static void azt_poll(void)
{
    int st = 0;
    int loop_ctl = 1;
    int skip = 0;

    if (azt_error) {                             /* ???*/
	if (aztSendCmd(ACMD_GET_ERROR)) return;
	STEN_LOW;
	azt_error=inb(DATA_PORT)&0xFF;
	printk("aztcd: I/O error 0x%02x\n", azt_error);
	azt_invalidate_buffers();
#ifdef WARN_IF_READ_FAILURE
	if (AztTries == 5)
	  printk("aztcd: read of block %d failed - maybe audio disk?\n", azt_next_bn);
#endif
	if (!AztTries--) {
	  printk("aztcd: read of block %d failed, maybe audio disk? Giving up\n", azt_next_bn);
	  if (azt_transfer_is_active) {
	    AztTries = 0;
	    loop_ctl = 0;
	  }
	  if (CURRENT_VALID)
	    end_request(0);
	  AztTries = 5;
	}
    azt_error = 0;
    azt_state = AZT_S_STOP;
    }

    while (loop_ctl)
    {
      loop_ctl = 0;   /* each case must flip this back to 1 if we want
			 to come back up here */
      switch (azt_state) {

	case AZT_S_IDLE:
#ifdef AZT_TEST3
	  if (azt_state!=azt_state_old) {
	    azt_state_old=azt_state;
	    printk("AZT_S_IDLE\n");
	    }
#endif
	  return;

	case AZT_S_START:
#ifdef AZT_TEST3
	  if (azt_state!=azt_state_old) {
	    azt_state_old=azt_state;
	    printk("AZT_S_START\n");
	  }
#endif

	  if(aztSendCmd(ACMD_GET_STATUS)) return;  /*result will be checked by aztStatus() */
	  azt_state = azt_mode == 1 ? AZT_S_READ : AZT_S_MODE;
	  AztTimeout = 3000;
	  break;

	case AZT_S_MODE:
#ifdef AZT_TEST3
	  if (azt_state!=azt_state_old) {
	    azt_state_old=azt_state;
	    printk("AZT_S_MODE\n");
	  }
#endif
	  if (!skip) {
	    if ((st = aztStatus()) != -1) {
	      if (st & AST_DSK_CHG) {
		aztDiskChanged = 1;
		aztTocUpToDate = 0;
		azt_invalidate_buffers();
	      }
	    } else break;
	  }
	  skip = 0;

	  if ((st & AST_DOOR_OPEN)||(st & AST_NOT_READY)) {
	    aztDiskChanged = 1;
	    aztTocUpToDate = 0;
	    printk((st & AST_DOOR_OPEN) ? "aztcd: door open\n" : "aztcd: disk removed\n");
	    if (azt_transfer_is_active) {
	      azt_state = AZT_S_START;
	      loop_ctl = 1;   /* goto immediately */
	      break;
	    }
	    azt_state = AZT_S_IDLE;
	    while (CURRENT_VALID)
	      end_request(0);
	    return;
	  }
					/*???*/
	  if (aztSendCmd(ACMD_SET_MODE)) return;
	  outb(0x01, DATA_PORT);            /*Mode 1*/
	  PA_OK;
	  STEN_LOW;
	  if (aztSendCmd(ACMD_GET_STATUS)) return;
	  azt_mode = 1;
	  azt_state = AZT_S_READ;
	  AztTimeout = 3000;

	  break;


	case AZT_S_READ:
#ifdef AZT_TEST3
	  if (azt_state!=azt_state_old)  {
	    azt_state_old=azt_state;
	    printk("AZT_S_READ\n");
	  }
#endif
	  if (!skip) {
	      if ((st = aztStatus()) != -1) {
		if (st & AST_DSK_CHG) {
		aztDiskChanged = 1;
		aztTocUpToDate = 0;
		azt_invalidate_buffers();
		}
	      } else break;
	  } 
	    
	  skip = 0;
	  if ((st & AST_DOOR_OPEN)||(st & AST_NOT_READY)) {
	    aztDiskChanged = 1;
	    aztTocUpToDate = 0;
	    printk((st & AST_DOOR_OPEN) ? "aztcd: door open\n" : "aztcd: disk removed\n");
	    if (azt_transfer_is_active) {
	      azt_state = AZT_S_START;
	      loop_ctl = 1;
	      break;
	    }
	    azt_state = AZT_S_IDLE;
	    while (CURRENT_VALID)
	    end_request(0);
	    return;
	  }

	  if (CURRENT_VALID) {
	    struct azt_Play_msf msf;
	    azt_next_bn = CURRENT -> sector / 4;
	    azt_hsg2msf(azt_next_bn, &msf.start);
	    azt_read_count=AZT_BUF_SIZ;    /*??? fast, because we read ahead*/
/*          azt_read_count= CURRENT->nr_sectors;      slow
*/
	    msf.end.min = 0;
	    msf.end.sec = 0;            
	    msf.end.frame = azt_read_count ;/*Mitsumi here reads 0xffffff sectors*/
#ifdef AZT_TEST3
	    printk("---reading msf-address %x:%x:%x  %x:%x:%x\n",msf.start.min,msf.start.sec,msf.start.frame,msf.end.min,msf.end.sec,msf.end.frame);
	    printk("azt_next_bn:%x  azt_buf_in:%x azt_buf_out:%x  azt_buf_bn:%x\n", \
		    azt_next_bn, azt_buf_in, azt_buf_out, azt_buf_bn[azt_buf_in]);
#endif 
	    sendAztCmd(ACMD_DATA_READ, &msf);
	    azt_state = AZT_S_DATA;
	    AztTimeout = READ_TIMEOUT;
	  } else {
	    azt_state = AZT_S_STOP;
	    loop_ctl = 1;
	    break;
	  }

	  break;


	case AZT_S_DATA:
#ifdef AZT_TEST3
	  if (azt_state!=azt_state_old)  {
	    azt_state_old=azt_state;
	    printk("AZT_S_DATA\n");
	  }
#endif

	  st = inb(STATUS_PORT) & AFL_STATUSorDATA;   /*???*/

	  switch (st) {

	    case AFL_DATA:
#ifdef AZT_TEST3
	      if (st!=azt_st_old)  {
		azt_st_old=st; 
		printk("---AFL_DATA st:%x\n",st);
	      }
#endif
#ifdef WARN_IF_READ_FAILURE
	      if (AztTries == 5)
		printk("aztcd: read of block %d failed - maybe audio disk?\n", azt_next_bn);
#endif
	      if (!AztTries--) {
		printk("aztcd: read of block %d failed, maybe audio disk ? Giving up\n", azt_next_bn);
		if (azt_transfer_is_active) {
		  AztTries = 0;
		  break;
		}
		if (CURRENT_VALID)
		  end_request(0);
		AztTries = 5;
	      }
	      azt_state = AZT_S_START;
	      AztTimeout = READ_TIMEOUT;
	      loop_ctl = 1;
	      break;

	    case AFL_STATUSorDATA:
#ifdef AZT_TEST3
	      if (st!=azt_st_old)  {
		azt_st_old=st;
		printk("---AFL_STATUSorDATA st:%x\n",st);
	      }
#endif
	      break;

	    default:
#ifdef AZT_TEST3
	      if (st!=azt_st_old)  {
		azt_st_old=st;
		printk("---default: st:%x\n",st);
	      }
#endif
	      AztTries = 5;
	      if (!CURRENT_VALID && azt_buf_in == azt_buf_out) {
		azt_state = AZT_S_STOP;
		loop_ctl = 1;
		break;
	      }
	      if (azt_read_count<=0)
		printk("aztcd: warning - try to read 0 frames\n");
	      while (azt_read_count)      /*??? fast read ahead loop*/
	       { azt_buf_bn[azt_buf_in] = -1;
		 DTEN_LOW;                      /*??? unsolved problem, very
						      seldom we get timeouts
						      here, don't now the real
						      reason. With my drive this
						      sometimes also happens with
						      Aztech's original driver under
						      DOS. Is it a hardware bug? 
						      I tried to recover from such
						      situations here. Zimmermann*/
		 if (aztTimeOutCount>=AZT_TIMEOUT) 
		  { printk("read_count:%d CURRENT->nr_sectors:%ld azt_buf_in:%d\n", azt_read_count,CURRENT->nr_sectors,azt_buf_in);
		    printk("azt_transfer_is_active:%x\n",azt_transfer_is_active);
		    azt_read_count=0;
		    azt_state = AZT_S_STOP;
		    loop_ctl = 1;
		    end_request(1);  /*should we have here (1) or (0)? */
		  }
		 else
		  { insb(DATA_PORT, azt_buf + 2048 * azt_buf_in, 2048);
		    azt_read_count--;
#ifdef AZT_TEST3
		    printk("AZT_S_DATA; ---I've read data- read_count: %d\n",azt_read_count);
		    printk("azt_next_bn:%d  azt_buf_in:%d azt_buf_out:%d  azt_buf_bn:%d\n", \
			 azt_next_bn, azt_buf_in, azt_buf_out, azt_buf_bn[azt_buf_in]);
#endif
		    azt_buf_bn[azt_buf_in] = azt_next_bn++;
		    if (azt_buf_out == -1)
		      azt_buf_out = azt_buf_in;
		    azt_buf_in = azt_buf_in + 1 == AZT_BUF_SIZ ? 0 : azt_buf_in + 1;
		  }
	       }
	      if (!azt_transfer_is_active) {
		while (CURRENT_VALID) {
		  azt_transfer();
		  if (CURRENT -> nr_sectors == 0)
		    end_request(1);
		  else
		    break;
		}
	      }

	      if (CURRENT_VALID
		&& (CURRENT -> sector / 4 < azt_next_bn ||
		CURRENT -> sector / 4 > azt_next_bn + AZT_BUF_SIZ)) {
		azt_state = AZT_S_STOP;
		loop_ctl = 1;
		break;
	      }
	      AztTimeout = READ_TIMEOUT;   
	      if (azt_read_count==0) {
		azt_state = AZT_S_STOP;   /*???*/
		loop_ctl = 1;
		break;           
	      } 
	      break;
	    }
    break;


	case AZT_S_STOP:
#ifdef AZT_TEST3
	  if (azt_state!=azt_state_old) {
	    azt_state_old=azt_state;
	    printk("AZT_S_STOP\n");
	  }
#endif
	  if (azt_read_count!=0) printk("aztcd: discard data=%x frames\n",azt_read_count);  /*???*/
	  while (azt_read_count!=0) {
	    int i;
	    if ( !(inb(STATUS_PORT) & AFL_DATA) ) {
	      for (i=0; i<2048; i++) {
		inb(DATA_PORT);
	      }
	    }
	    azt_read_count--;
	  }  
	  if (aztSendCmd(ACMD_GET_STATUS)) return;
	  azt_state = AZT_S_STOPPING;
	  AztTimeout = 1000;
	  break;

	case AZT_S_STOPPING:
#ifdef AZT_TEST3
	  if (azt_state!=azt_state_old) {
	    azt_state_old=azt_state;
	    printk("AZT_S_STOPPING\n");
	  }
#endif

	  if ((st = aztStatus()) == -1 && AztTimeout)
	    break;

	  if ((st != -1) && (st & AST_DSK_CHG)) {
	    aztDiskChanged = 1;
	    aztTocUpToDate = 0;
	    azt_invalidate_buffers();
	  }


#ifdef AZT_TEST3
	  printk("CURRENT_VALID %d azt_mode %d\n",
	     CURRENT_VALID, azt_mode);
#endif

	  if (CURRENT_VALID) {
	    if (st != -1) {
	      if (azt_mode == 1) {
		azt_state = AZT_S_READ;
		loop_ctl = 1;
		skip = 1;
		break;
	      } else {
		azt_state = AZT_S_MODE;
		loop_ctl = 1;
		skip = 1;
		break;
	      }
	    } else {
	      azt_state = AZT_S_START;
	      AztTimeout = 1;
	    }
	  } else {
	    azt_state = AZT_S_IDLE;
	    return;
	  }
	  break;

	default:
	  printk("aztcd: invalid state %d\n", azt_state);
	  return;
      }  /* case */
    } /* while */
  

   if (!AztTimeout--) 
    { printk("aztcd: timeout in state %d\n", azt_state);
      azt_state = AZT_S_STOP;
      if (aztSendCmd(ACMD_STOP)) return; 
      STEN_LOW_WAIT;    
    };

  SET_TIMER(azt_poll, 1);
}

static void azt_invalidate_buffers(void)
{ int i;

#ifdef AZT_DEBUG
  printk("aztcd: executing azt_invalidate_buffers\n");
#endif
  for (i = 0; i < AZT_BUF_SIZ; ++i)
    azt_buf_bn[i] = -1;
  azt_buf_out = -1;
}

/*
 * Open the device special file.  Check that a disk is in.
 */
int aztcd_open(struct inode *ip, struct file *fp)
{       int st;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztcd_open\n");
#endif
	if (aztPresent == 0)
		return -ENXIO;                  /* no hardware */

	if (!azt_open_count && azt_state == AZT_S_IDLE) {

	azt_invalidate_buffers();

	st = getAztStatus();                       /* check drive status */
	if (st == -1)
		return -EIO;                    /* drive doesn't respond */

        if (st&AST_DOOR_OPEN)
          {
            /* close door, then get the status again. */
            aztCloseDoor();     
            st = getAztStatus();
          }        

	if ((st&AST_DOOR_OPEN)||(st&AST_NOT_READY)) /* no disk in drive or door open*/
	{       /* Door should be closed, probably no disk in drive */
		printk("aztcd: no disk in drive or door open\n");
		return -EIO;
	}

	if (aztUpdateToc() < 0)
		return -EIO;

	}
	++azt_open_count;
        MOD_INC_USE_COUNT;
	aztLockDoor();
#ifdef AZT_DEBUG
	printk("aztcd: exiting aztcd_open\n");
#endif
	return 0;
}


/*
 * On close, we flush all azt blocks from the buffer cache.
 */
static void aztcd_release(struct inode * inode, struct file * file)
{ 
#ifdef AZT_DEBUG
  printk("aztcd: executing aztcd_release\n");
  printk("inode: %p, inode->i_rdev: %x    file: %p\n",inode,inode->i_rdev,file);
#endif
  MOD_DEC_USE_COUNT;
  if (!--azt_open_count) {
	azt_invalidate_buffers();
	sync_dev(inode->i_rdev);             /*??? isn't it a read only dev?*/
	invalidate_buffers(inode -> i_rdev);
	aztUnlockDoor();
        CLEAR_TIMER;
  }
  return;
}


static struct file_operations azt_fops = {
	NULL,                   /* lseek - default */
	block_read,             /* read - general block-dev read */
	block_write,            /* write - general block-dev write */
	NULL,                   /* readdir - bad */
	NULL,                   /* select */
	aztcd_ioctl,            /* ioctl */
	NULL,                   /* mmap */
	aztcd_open,             /* open */
	aztcd_release,          /* release */
	NULL,                   /* fsync */
	NULL,                   /* fasync*/
	check_aztcd_media_change, /*media change*/
	NULL                    /* revalidate*/
};

/*
 * Test for presence of drive and initialize it.  Called at boot time.
 */
#ifndef MODULE 
unsigned long aztcd_init(unsigned long mem_start, unsigned long mem_end)
#else
int init_module(void)
#endif
{       long int count, max_count;
	unsigned char result[50];
	int st;

	if (azt_port <= 0) {
	  printk("aztcd: no Aztech CD-ROM Initialization");
#ifndef MODULE
	  return (mem_start);
#else
          return -EIO;
#endif	  
	}
	printk("Aztech CD-ROM Init: Aztech, Orchid, Okano, Wearnes CD-ROM Driver\n");
	printk("Aztech CD-ROM Init: (C) 1994,1995 Werner Zimmermann\n");
	printk("Aztech CD-ROM Init: DriverVersion=%s  BaseAddress=0x%x \n",AZT_VERSION,azt_port);

	if (check_region(azt_port, 4)) {
	  printk("aztcd: conflict, I/O port (%X) already used\n",
		 azt_port);
#ifndef MODULE
	  return (mem_start);
#else
          return -EIO;
#endif	  
	}

	/* check for card */
	outb(POLLED,MODE_PORT);                 /*???*/
	inb(CMD_PORT);
	inb(CMD_PORT);
	outb(ACMD_GET_VERSION,CMD_PORT); /*Try to get version info*/
	STEN_LOW;
	if (inb(DATA_PORT)!=AFL_OP_OK)   /*OP_OK? If not, reset and try again*/
	 { printk("aztcd: drive reset - please wait\n");
	   for (count=0;count<50;count++)
	     { inb(STATUS_PORT);    /*removing all data from earlier tries*/
	       inb(DATA_PORT);
	     }
	   outb(POLLED,MODE_PORT);              /*???*/
	   inb(CMD_PORT);
	   inb(CMD_PORT);
	   outb(ACMD_SOFT_RESET,CMD_PORT);   /*send reset*/
	   STEN_LOW;
	   if (inb(DATA_PORT)!=AFL_OP_OK)    /*OP_OK?*/
	    { printk("aztcd: no AZTECH CD-ROM drive found\n");
#ifndef MODULE
	      return (mem_start);
#else
              return -EIO;
#endif	     
	    } 
	   for (count = 0; count < AZT_TIMEOUT; count++);  /* delay a bit */
	   if ((st=getAztStatus())==-1)
	    { printk("aztcd: Drive Status Error Status=%x\n",st);
#ifndef MODULE
	      return (mem_start);
#else
              return -EIO;
#endif	      
	    }
#ifdef AZT_DEBUG
	   printk("aztcd: Status = %x\n",st);
#endif
	   outb(POLLED,MODE_PORT);              /*???*/
	   inb(CMD_PORT);
	   inb(CMD_PORT);
	   outb(ACMD_GET_VERSION,CMD_PORT); /*GetVersion*/
	   STEN_LOW;
	   OP_OK;
	 } 
	azt_init_end=1;
	STEN_LOW;
	result[0]=inb(DATA_PORT);        /*reading in a null byte???*/
	for (count=1;count<50;count++)   /*Reading version string*/
	 { aztTimeOutCount=0;            /*here we must implement STEN_LOW differently*/
	   do { aztIndatum=inb(STATUS_PORT);/*because we want to exit by timeout*/
		aztTimeOutCount++; 
		if (aztTimeOutCount>=AZT_FAST_TIMEOUT) break; 
	      } while (aztIndatum&AFL_STATUS); 
	   if (aztTimeOutCount>=AZT_FAST_TIMEOUT) break;  /*all chars read?*/
	   result[count]=inb(DATA_PORT);
	 }
	if (count>30) max_count=30;  /*print max.30 chars of the version string*/
	else          max_count=count;
	printk("Aztech CD-ROM Init: FirmwareVersion=");
	for (count=1;count<max_count;count++) printk("%c",result[count]);
	printk("<<<\n");

	if ((result[1]=='A')&&(result[2]=='Z')&&(result[3]=='T'))
	 { printk("Aztech CD-ROM Init: AZTECH drive detected\n"); /*AZTECH*/    
	 }
	else if ((result[2]=='C')&&(result[3]=='D')&&(result[4]=='D'))
	 { printk("Aztech CD-ROM Init: ORCHID or WEARNES drive detected\n"); /*ORCHID or WEARNES*/
	 }
	else                                               /*OTHERS or none*/
	 { printk("Aztech CD-ROM Init: : unknown drive or firmware version detected\n");
	   printk("                      azt may not run stable, if you want to try anyhow,\n");
	   printk("                      boot with: aztcd=base_address,0x79\n");
	   if ((azt_cont!=0x79))     
	     { printk("Aztech CD-ROM Init: FirmwareVersion=");
	       for (count=1;count<5;count++) printk("%c",result[count]);
	       printk("\n");
	       printk("Aztech CD-ROM Init: Aborted\n");
#ifndef MODULE
	       return (mem_start);
#else
               return -EIO;
#endif 	          
	     }
	 }
	if (register_blkdev(MAJOR_NR, "aztcd", &azt_fops) != 0)
	{
		printk("aztcd: Unable to get major %d for Aztech CD-ROM\n",
		       MAJOR_NR);
#ifndef MODULE		       
		return (mem_start);
#else
                return -EIO;
#endif		
	}
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	read_ahead[MAJOR_NR] = 4;

	request_region(azt_port, 4, "aztcd");

	azt_invalidate_buffers();
	aztPresent = 1;
	aztCloseDoor();
	printk("Aztech CD-ROM Init: End\n");
#ifndef MODULE
	return (mem_start);
#else
        return (0);
#endif
}


static void azt_hsg2msf(long hsg, struct msf *msf)
{       hsg += 150;
	msf -> min = hsg / 4500;
	hsg %= 4500;
	msf -> sec = hsg / 75;
	msf -> frame = hsg % 75;
#ifdef AZT_DEBUG
	if (msf->min  >=70) printk("aztcd: Error hsg2msf address Minutes\n");
	if (msf->sec  >=60) printk("aztcd: Error hsg2msf address Seconds\n");
	if (msf->frame>=75) printk("aztcd: Error hsg2msf address Frames\n");
#endif
	azt_bin2bcd(&msf -> min);           /* convert to BCD */
	azt_bin2bcd(&msf -> sec);
	azt_bin2bcd(&msf -> frame);
}


static void azt_bin2bcd(unsigned char *p)
{       int u, t;

	u = *p % 10;
	t = *p / 10;
	*p = u | (t << 4);
}

static int azt_bcd2bin(unsigned char bcd)
{       return (bcd >> 4) * 10 + (bcd & 0xF);
}



/*
 * Read a value from the drive.  Should return quickly, so a busy wait
 * is used to avoid excessive rescheduling. The read command itself must
 * be issued with aztSendCmd() directly before
 */
static int aztGetValue(unsigned char *result)
{       int s;

	STEN_LOW;
	if (aztTimeOutCount>=AZT_TIMEOUT)
	{       printk("aztcd: aztGetValue timeout\n");
		return -1;
	}
	s = inb(DATA_PORT) & 0xFF;
	*result = (unsigned char) s;
	return 0;
}


/*
 * Read the current Q-channel info.  Also used for reading the
 * table of contents.
 */
int aztGetQChannelInfo(struct azt_Toc *qp)
{       unsigned char notUsed;
	int st;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztGetQChannelInfo\n");
#endif
	if ((st=getAztStatus())==-1) return -1;
	if (aztSendCmd(ACMD_GET_Q_CHANNEL))          return -1;
	STEN_LOW_WAIT;
	if (aztGetValue(&notUsed) <0)                return -1; /*Nullbyte ein-*/
							     /*lesen ???*/
	if ((st&AST_MODE_BITS)==AST_INITIAL)
	 { qp->ctrl_addr=0;      /* when audio stop ACMD_GET_Q_CHANNEL returns */
	   qp->track=0;          /* only one byte with Aztech drives */
	   qp->pointIndex=0;
	   qp->trackTime.min=0;
	   qp->trackTime.sec=0;
	   qp->trackTime.frame=0;
	   qp->diskTime.min=0;
	   qp->diskTime.sec=0;
	   qp->diskTime.frame=0;
	   return 0;  
	 }
	else
	 { if (aztGetValue(&qp -> ctrl_addr) < 0)       return -1;
	   if (aztGetValue(&qp -> track) < 0)           return -1;
	   if (aztGetValue(&qp -> pointIndex) < 0)      return -1;
	   if (aztGetValue(&qp -> trackTime.min) < 0)   return -1;
	   if (aztGetValue(&qp -> trackTime.sec) < 0)   return -1;
	   if (aztGetValue(&qp -> trackTime.frame) < 0) return -1;
	   if (aztGetValue(&notUsed) < 0)               return -1;
	   if (aztGetValue(&qp -> diskTime.min) < 0)    return -1;
	   if (aztGetValue(&qp -> diskTime.sec) < 0)    return -1;
	   if (aztGetValue(&qp -> diskTime.frame) < 0)  return -1;
	 }
#ifdef AZT_DEBUG
	printk("aztcd: exiting aztGetQChannelInfo\n");
#endif
	return 0;
}

/*
 * Read the table of contents (TOC) and TOC header if necessary
 */
static int aztUpdateToc()
{
#ifdef AZT_DEBUG
	printk("aztcd: starting aztUpdateToc\n");
#endif  
	if (aztTocUpToDate)
		return 0;

	if (aztGetDiskInfo() < 0)
		return -EIO;

	if (aztGetToc() < 0)
		return -EIO;

	aztTocUpToDate = 1;
#ifdef AZT_DEBUG
	printk("aztcd: exiting aztUpdateToc\n");
#endif
	return 0;
}


/*
 * Read the table of contents header
 */
static int aztGetDiskInfo()
{ int limit;
  unsigned char test;
  struct azt_Toc qInfo;

#ifdef AZT_DEBUG
  printk("aztcd: starting aztGetDiskInfo\n");
#endif
  if (aztSendCmd(ACMD_SEEK_TO_LEADIN)) return -1;
  STEN_LOW_WAIT;
  test=0;
  for (limit=300;limit>0;limit--)
   {  if (aztGetQChannelInfo(&qInfo)<0) return -1;
      if (qInfo.pointIndex==0xA0)   /*Number of FirstTrack*/
	{ DiskInfo.first=qInfo.diskTime.min;
	  DiskInfo.first = azt_bcd2bin(DiskInfo.first);
	  test=test|0x01;
	}
      if (qInfo.pointIndex==0xA1)   /*Number of LastTrack*/
	{ DiskInfo.last=qInfo.diskTime.min;
	  DiskInfo.last  = azt_bcd2bin(DiskInfo.last);
	  test=test|0x02;
	}
      if (qInfo.pointIndex==0xA2)   /*DiskLength*/
	{ DiskInfo.diskLength.min=qInfo.diskTime.min;
	  DiskInfo.diskLength.sec=qInfo.diskTime.sec-2;
	  DiskInfo.diskLength.frame=qInfo.diskTime.frame;
	  test=test|0x04;
	}
      if ((qInfo.pointIndex==DiskInfo.first)&&(test&0x01))   /*StartTime of First Track*/
	{ DiskInfo.firstTrack.min=qInfo.diskTime.min;
	  DiskInfo.firstTrack.sec=qInfo.diskTime.sec;
	  DiskInfo.firstTrack.frame=qInfo.diskTime.frame;
	  test=test|0x08;
	}
      if (test==0x0F) break;
   }
#ifdef AZT_DEBUG
printk ("aztcd: exiting aztGetDiskInfo\n");
printk("Disk Info: first %d last %d length %02x:%02x.%02x first %02x:%02x.%02x\n",
	DiskInfo.first,
	DiskInfo.last,
	DiskInfo.diskLength.min,
	DiskInfo.diskLength.sec,
	DiskInfo.diskLength.frame,
	DiskInfo.firstTrack.min,
	DiskInfo.firstTrack.sec,
	DiskInfo.firstTrack.frame);
#endif
  if (test!=0x0F) return -1;
  return 0;
}


/*
 * Read the table of contents (TOC)
 */
static int aztGetToc()
{       int i, px;
	int limit;
	struct azt_Toc qInfo;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztGetToc\n");
#endif
	for (i = 0; i < MAX_TRACKS; i++)
		Toc[i].pointIndex = 0;

	i = DiskInfo.last + 3;

/* Is there a good reason to stop motor before TOC read?
	if (aztSendCmd(ACMD_STOP)) return -1;
	STEN_LOW_WAIT;
*/

	azt_mode = 0x05;
	if (aztSendCmd(ACMD_SEEK_TO_LEADIN)) return -1; /*???*/
	STEN_LOW_WAIT;

	for (limit = 300; limit > 0; limit--)
	{
		if (aztGetQChannelInfo(&qInfo) < 0)
			break;

		px = azt_bcd2bin(qInfo.pointIndex);
		if (px > 0 && px < MAX_TRACKS && qInfo.track == 0)
			if (Toc[px].pointIndex == 0)
			{
				Toc[px] = qInfo;
				i--;
			}

		if (i <= 0)
			break;
	}

	Toc[DiskInfo.last + 1].diskTime = DiskInfo.diskLength;

#ifdef AZT_DEBUG
printk("aztcd: exiting aztGetToc\n");
for (i = 1; i <= DiskInfo.last+1; i++)
printk("i = %2d ctl-adr = %02X track %2d px %02X %02X:%02X.%02X    %02X:%02X.%02X\n",
i, Toc[i].ctrl_addr, Toc[i].track, Toc[i].pointIndex,
Toc[i].trackTime.min, Toc[i].trackTime.sec, Toc[i].trackTime.frame,
Toc[i].diskTime.min, Toc[i].diskTime.sec, Toc[i].diskTime.frame);
for (i = 100; i < 103; i++)
printk("i = %2d ctl-adr = %02X track %2d px %02X %02X:%02X.%02X    %02X:%02X.%02X\n",
i, Toc[i].ctrl_addr, Toc[i].track, Toc[i].pointIndex,
Toc[i].trackTime.min, Toc[i].trackTime.sec, Toc[i].trackTime.frame,
Toc[i].diskTime.min, Toc[i].diskTime.sec, Toc[i].diskTime.frame);
#endif

	return limit > 0 ? 0 : -1;
}

#ifdef MODULE
void cleanup_module(void)
{ if (MOD_IN_USE)
    { printk("aztcd module in use - can't remove it.\n");
      return;
    }
  if ((unregister_blkdev(MAJOR_NR, "aztcd") == -EINVAL))    
    { printk("What's that: can't unregister aztcd\n");
      return;
    }
   release_region(azt_port,4);
   printk("aztcd module released.\n");
}   
#endif MODULE
