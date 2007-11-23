/*
 *  sbpcd.c   CD-ROM device driver for the whole family of IDE-style
 *            Kotobuki/Matsushita/Panasonic CR-5xx drives for
 *            SoundBlaster ("Pro" or "16 ASP" or compatible) cards
 *            and for "no-sound" interfaces like Lasermate and the
 *            Panasonic CI-101P.
 *            Also for the Longshine LCS-7260 drive.
 *            Also for the IBM "External ISA CD-Rom" drive.
 *            Not for the TEAC CD-55A drive (yet).
 *            Not for the CreativeLabs CD200 drive (who knows?).
 *
 *  NOTE:     This is release 3.3.
 *            It works with my SbPro & drive CR-521 V2.11 from 2/92
 *            and with the CR-562-B V0.75 on a "naked" Panasonic
 *            CI-101P interface. And vice versa. 
 *  
 *
 *  VERSION HISTORY
 *
 *  0.1  initial release, April/May 93, after mcd.c (Martin Harriss)
 *
 *  0.2  the "repeat:"-loop in do_sbpcd_request did not check for
 *       end-of-request_queue (resulting in kernel panic).
 *       Flow control seems stable, but throughput is not better.  
 *
 *  0.3  interrupt locking totally eliminated (maybe "inb" and "outb"
 *       are still locking) - 0.2 made keyboard-type-ahead losses.
 *       check_sbpcd_media_change added (to use by isofs/inode.c)
 *       - but it detects almost nothing.
 *
 *  0.4  use MAJOR 25 definitely.
 *       Almost total re-design to support double-speed drives and
 *       "naked" (no sound) interface cards.
 *       Flow control should be exact now (tell me if not).
 *       Don't occupy the SbPro IRQ line (not needed either); will
 *       live together with Hannu Savolainen's sndkit now.
 *	 Speeded up data transfer to 150 kB/sec, with help from Kai
 *       Makisara, the "provider" of the "mt" tape utility.
 *       Give "SpinUp" command if necessary.
 *       First steps to support up to 4 drives (but currently only one).
 *       Implemented audio capabilities - workman should work, xcdplayer
 *       gives some problems.
 *       This version is still consuming too much CPU time, and
 *       sleeping still has to be worked on.
 *       During "long" implied seeks, it seems possible that a 
 *       ReadStatus command gets ignored. That gives the message
 *       "ResponseStatus timed out" (happens about 6 times here during
 *       a "ls -alR" of the YGGDRASIL LGX-Beta CD). Such a case is
 *       handled without data error, but it should get done better.
 *
 *  0.5  Free CPU during waits (again with help from Kai Makisara).
 *       Made it work together with the LILO/kernel setup standard.
 *       Included auto-probing code, as suggested by YGGDRASIL.
 *       Formal redesign to add DDI debugging.
 *       There are still flaws in IOCTL (workman with double speed drive).
 *
 *  1.0  Added support for all drive ids (0...3, no longer only 0)
 *       and up to 4 drives on one controller.
 *       Added "#define MANY_SESSION" for "old" multi session CDs.
 *
 *  1.1  Do SpinUp for new drives, too.
 *       Revised for clean compile under "old" kernels (pl9).
 *
 *  1.2  Found the "workman with double-speed drive" bug: use the driver's
 *       audio_state, not what the drive is reporting with ReadSubQ.
 *
 *  1.3  Minor cleanups.
 *       Refinements regarding Workman.
 *
 *  1.4  Read XA disks (PhotoCDs) with "old" drives, too (but possibly only
 *       the first session - I could not try a "multi-session" CD yet).
 *       This currently still is too slow (50 kB/sec) - but possibly
 *       the old drives won't do it faster.
 *       Implemented "door (un)lock" for new drives (still does not work
 *       as wanted - no lock possible after an unlock).
 *       Added some debugging printout for the UPC/EAN code - but my drives 
 *       return only zeroes. Is there no UPC/EAN code written?
 *
 *  1.5  Laborate with UPC/EAN code (not better yet).
 *       Adapt to kernel 1.1.8 change (have to explicitly include
 *       <linux/string.h> now).
 *
 *  1.6  Trying to read audio frames as data. Impossible with the current
 *       drive firmware levels, as it seems. Awaiting any hint. ;-)
 *       Changed "door unlock": repeat it until success.
 *       Changed CDROMSTOP routine (stop somewhat "softer" so that Workman
 *       won't get confused).
 *       Added a third interface type: Sequoia S-1000, as used with the SPEA
 *       Media FX sound card. This interface (usable for Sony and Mitsumi 
 *       drives, too) needs a special configuration setup and behaves like a 
 *       LaserMate type after that. Still experimental - I do not have such
 *       an interface.
 *       Use the "variable BLOCK_SIZE" feature (2048). But it does only work
 *       if you give the mount option "block=2048".
 *       The media_check routine is currently disabled; now that it gets
 *       called as it should I fear it must get synchronized for not to
 *       disturb the normal driver's activity.
 *
 *  2.0  Version number bumped - two reasons:
 *       - reading audio tracks as data works now with CR-562 and CR-563. We
 *       currently do it by an IOCTL (yet has to get standardized), one frame
 *       at a time; that is pretty slow. But it works.
 *       - we are maintaining now up to 4 interfaces (each up to 4 drives):
 *       did it the easy way - a different MAJOR (25, 26, ...) and a different
 *       copy of the driver (sbpcd.c, sbpcd2.c, sbpcd3.c, sbpcd4.c - only
 *       distinguished by the value of SBPCD_ISSUE and the driver's name),
 *       and a common sbpcd.h file.
 *       Bettered the "ReadCapacity error" problem with old CR-52x drives (the
 *       drives sometimes need a manual "eject/insert" before work): just
 *       reset the drive and do again. Needs lots of resets here and sometimes
 *       that does not cure, so this can't be the solution.
 *
 *  2.1  Found bug with multisession CDs (accessing frame 16).
 *       "read audio" works now with address type CDROM_MSF, too.
 *       Bigger audio frame buffer: allows reading max. 4 frames at time; this
 *       gives a significant speedup, but reading more than one frame at once
 *       gives missing chunks at each single frame boundary.
 *
 *  2.2  Kernel interface cleanups: timers, init, setup, media check.
 *
 *  2.3  Let "door lock" and "eject" live together.
 *       Implemented "close tray" (done automatically during open).
 *
 *  2.4  Use different names for device registering.
 *       
 *  2.5  Added "#if EJECT" code (default: enabled) to automatically eject
 *       the tray during last call to "sbpcd_release".
 *       Added "#if JUKEBOX" code (default: disabled) to automatically eject
 *       the tray during call to "sbpcd_open" if no disk is in.
 *       Turn on the CD volume of "compatible" sound cards, too; just define
 *       SOUND_BASE (in sbpcd.h) accordingly (default: disabled).
 *
 *  2.6  Nothing new.  
 *       
 *  2.7  Added CDROMEJECT_SW ioctl to set the "EJECT" behavior on the fly:
 *       0 disables, 1 enables auto-ejecting. Useful to keep the tray in
 *       during shutdown.
 *       
 *  2.8  Added first support (still BETA, I need feedback or a drive) for
 *       the Longshine LCS-7260 drives. They appear as double-speed drives
 *       using the "old" command scheme, extended by tray control and door
 *       lock functions.
 *       Found (and fixed preliminary) a flaw with some multisession CDs: we
 *       have to re-direct not only the accesses to frame 16 (the isofs
 *       routines drive it up to max. 100), but also those to the continuation
 *       (repetition) frames (as far as they exist - currently set fix as
 *       16..20).
 *       Changed default of the "JUKEBOX" define. If you use this default,
 *       your tray will eject if you try to mount without a disk in. Next
 *       mount command will insert the tray - so, just insert a disk. ;-)
 *       
 *  2.9  Fulfilled the Longshine LCS-7260 support; with great help and
 *       experiments by Serge Robyns.
 *       First attempts to support the TEAC CD-55A drives; but still not
 *       usable yet.
 *       Implemented the CDROMMULTISESSION ioctl; this is an attempt to handle
 *       multi session CDs more "transparent" (redirection handling has to be
 *       done within the isofs routines, and only for the special purpose of
 *       obtaining the "right" volume descriptor; accesses to the raw device
 *       should not get redirected).
 *
 *  3.0  Just a "normal" increment, with some provisions to do it better. ;-)
 *       Introduced "#define READ_AUDIO" to specify the maximum number of 
 *       audio frames to grab with one request. This defines a buffer size
 *       within kernel space; a value of 0 will reserve no such space and
 *       disable the CDROMREADAUDIO ioctl. A value of 75 enables the reading
 *       of a whole second with one command, but will use a buffer of more
 *       than 172 kB.
 *       Started CD200 support. Drive detection should work, but nothing
 *       more.
 *
 *  3.1  Working to support the CD200 and the Teac CD-55A drives.
 *       AT-BUS style device numbering no longer used: use SCSI style now.
 *       So, the first "found" device has MINOR 0, regardless of the
 *       jumpered drive ID. This implies modifications to the /dev/sbpcd*
 *       entries for some people, but will help the DAU (german TLA, english:
 *       "newbie", maybe ;-) to install his "first" system from a CD.
 *     
 *  3.2  Still testing with CD200 and CD-55A drives.
 *
 *  3.3  Working with CD200 support. Maybe a simple read is already possible.
 *
 *  TODO
 *
 *     disk change detection
 *     allow & synchronize multi-activity
 *        (data + audio + ioctl + disk change, multiple drives)
 *     implement multi-controller-support with a single driver
 *     implement "read all subchannel data" (96 bytes per frame)
 *
 *
 *     special thanks to Kai Makisara (kai.makisara@vtt.fi) for his fine
 *     elaborated speed-up experiments (and the fabulous results!), for
 *     the "push" towards load-free wait loops, and for the extensive mail
 *     thread which brought additional hints and bug fixes.
 * 
 *
 *   Copyright (C) 1993, 1994, 1995  Eberhard Moenkeberg <emoenke@gwdg.de>
 *                               or <eberhard_moenkeberg@rollo.central.de>
 *
 *                  If you change this software, you should mail a .diff
 *                  file with some description lines to emoenke@gwdg.de.
 *                  I want to know about it.
 *
 *                  If you are the editor of a Linux CD, you should
 *                  enable sbpcd.c within your boot floppy kernel and
 *                  send me one of your CDs for free.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2, or (at your option)
 *   any later version.
 *
 *   You should have received a copy of the GNU General Public License
 *   (for example /usr/src/linux/COPYING); if not, write to the Free
 *   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef SBPCD_ISSUE
#define SBPCD_ISSUE 1
#endif SBPCD_ISSUE

#include <linux/config.h>
#include <linux/errno.h>

#include <linux/sched.h>
#include <linux/mm.h>
/* #undef DS */
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/cdrom.h>
#include <linux/ioport.h>
#include <linux/major.h> 
#include <linux/sbpcd.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <stdarg.h>

#if !(SBPCD_ISSUE-1)
#define MAJOR_NR MATSUSHITA_CDROM_MAJOR
#endif
#if !(SBPCD_ISSUE-2)
#define MAJOR_NR MATSUSHITA_CDROM2_MAJOR /* second driver issue */
#endif
#if !(SBPCD_ISSUE-3)
#define MAJOR_NR MATSUSHITA_CDROM3_MAJOR /* third driver issue */
#endif
#if !(SBPCD_ISSUE-4)
#define MAJOR_NR MATSUSHITA_CDROM4_MAJOR /* fourth driver issue */
#endif

#include "blk.h"

#define VERSION "3.3 Eberhard Moenkeberg <emoenke@gwdg.de>"

/*
 * still testing around...
 */
#define TEAC 0 /* set to 1 for TEAC CD-55A detection test (not more) */

#define MULTISESSION_BY_DRIVER 0 /* if set to 0 here, we need the counterpart
                                  * in linux/fs/isofs/inode.c
                                  */
#define READ_AUDIO 4 /* max. number of audio frames to read with one */
                     /* request (allocates n* 2352 bytes kernel memory!) */
#define JUKEBOX 1 /* tray control: eject tray if no disk is in */
#define EJECT 1 /* tray control: eject tray after last use */
#define LONG_TIMING 0 /* test against timeouts with "gold" CDs on CR-521 */
#define MANY_SESSION 0 /* this will conflict with "true" multi-session! */
#undef  FUTURE
#define WORKMAN 1 /* some testing stuff to make it better */
#define CDMKE /* makes timing independent of processor speed */

#undef XA_TEST1
#define XA_TEST2

#define TEST_UPC 0
#define SPEA_TEST 0
#define PRINTK_BUG 0
#define TEST_STI 0

#define DISTRIBUTION 1 /* I use it with a 0 here */

#if 0
#define INLINE
#else
#define INLINE inline
#endif

/*==========================================================================*/
/*
 * provisions for more than 1 driver issues
 * currently up to 4 drivers, expandable
 */
#if !(SBPCD_ISSUE-1)
#define DO_SBPCD_REQUEST(a) do_sbpcd_request(a)
#define SBPCD_INIT(a,b) sbpcd_init(a,b)
#endif
#if !(SBPCD_ISSUE-2)
#define DO_SBPCD_REQUEST(a) do_sbpcd2_request(a)
#define SBPCD_INIT(a,b) sbpcd2_init(a,b)
#endif
#if !(SBPCD_ISSUE-3)
#define DO_SBPCD_REQUEST(a) do_sbpcd3_request(a)
#define SBPCD_INIT(a,b) sbpcd3_init(a,b)
#endif
#if !(SBPCD_ISSUE-4)
#define DO_SBPCD_REQUEST(a) do_sbpcd4_request(a)
#define SBPCD_INIT(a,b) sbpcd4_init(a,b)
#endif
/*==========================================================================*/
#if MANY_SESSION
#undef LONG_TIMING
#define LONG_TIMING 1
#endif
/*==========================================================================*/
#if SBPCD_DIS_IRQ
#define SBPCD_CLI cli()
#define SBPCD_STI sti()
#else
#define SBPCD_CLI
#define SBPCD_STI
#endif SBPCD_DIS_IRQ
/*==========================================================================*/
/*
 * auto-probing address list
 * inspired by Adam J. Richter from Yggdrasil
 *
 * still not good enough - can cause a hang.
 *   example: a NE 2000 ethernet card at 300 will cause a hang probing 310.
 * if that happens, reboot and use the LILO (kernel) command line.
 * The possibly conflicting ethernet card addresses get NOT probed 
 * by default - to minimize the hang possibilities. 
 *
 * The SB Pro addresses get "mirrored" at 0x6xx - to avoid a type error,
 * the 0x2xx-addresses must get checked before 0x6xx.
 *
 * send mail to emoenke@gwdg.de if your interface card is not FULLY
 * represented here.
 */
#if !(SBPCD_ISSUE-1)
static int sbpcd_probe[] = 
{
  CDROM_PORT, SBPRO, /* probe with user's setup first */
#if DISTRIBUTION
  0x230, 1, /* Soundblaster Pro and 16 (default) */
  0x300, 0, /* CI-101P (default), WDH-7001C (default),
	       Galaxy (default), Reveal (one default) */
  0x250, 1, /* OmniCD default, Soundblaster Pro and 16 */
  0x260, 1, /* OmniCD */
  0x320, 0, /* Lasermate, CI-101P, WDH-7001C, Galaxy, Reveal (other default),
               Longshine LCS-6853 (default) */
  0x338, 0, /* Reveal Sound Wave 32 card model #SC600 */
  0x340, 0, /* Mozart sound card (default), Lasermate, CI-101P */
  0x360, 0, /* Lasermate, CI-101P */
  0x270, 1, /* Soundblaster 16 */
  0x670, 0, /* "sound card #9" */
  0x690, 0, /* "sound card #9" */
  0x330, 2, /* SPEA Media FX (default) */
  0x320, 2, /* SPEA Media FX */
  0x340, 2, /* SPEA Media FX */
  0x634, 0, /* some newer sound cards */
  0x638, 0, /* some newer sound cards */
/* due to incomplete address decoding of the SbPro card, these must be last */
  0x630, 0, /* "sound card #9" (default) */
  0x650, 0, /* "sound card #9" */
/*
 * some "hazardous" locations
 * (will stop the bus if a NE2000 ethernet card resides at offset -0x10)
 */
#if 0
  0x330, 0, /* Lasermate, CI-101P, WDH-7001C */
  0x350, 0, /* Lasermate, CI-101P */
  0x350, 2, /* SPEA Media FX */
  0x370, 0, /* Lasermate, CI-101P */
  0x290, 1, /* Soundblaster 16 */
  0x310, 0, /* Lasermate, CI-101P, WDH-7001C */
#endif
#endif DISTRIBUTION
};
#else
static int sbpcd_probe[] = {CDROM_PORT, SBPRO}; /* probe with user's setup only */
#endif

#define NUM_PROBE  (sizeof(sbpcd_probe) / sizeof(int))


/*==========================================================================*/
/*
 * the external references:
 */
#if !(SBPCD_ISSUE-1)
#ifdef CONFIG_SBPCD2
extern unsigned long sbpcd2_init(unsigned long, unsigned long);
#endif
#ifdef CONFIG_SBPCD3
extern unsigned long sbpcd3_init(unsigned long, unsigned long);
#endif
#ifdef CONFIG_SBPCD4
extern unsigned long sbpcd4_init(unsigned long, unsigned long);
#endif
#endif

/*==========================================================================*/
/*==========================================================================*/
/*
 * the forward references:
 */
static void sbp_read_cmd(void);
static int sbp_data(void);
static int cmd_out(int);
static int DiskInfo(void);
static int sbpcd_chk_disk_change(dev_t);

/*==========================================================================*/

/*
 * pattern for printk selection:
 *
 * (1<<DBG_INF)  necessary information
 * (1<<DBG_BSZ)  BLOCK_SIZE trace
 * (1<<DBG_REA)  "read" status trace
 * (1<<DBG_CHK)  "media check" trace
 * (1<<DBG_TIM)  datarate timer test
 * (1<<DBG_INI)  initialization trace
 * (1<<DBG_TOC)  tell TocEntry values
 * (1<<DBG_IOC)  ioctl trace
 * (1<<DBG_STA)  "ResponseStatus" trace
 * (1<<DBG_ERR)  "xx_ReadError" trace
 * (1<<DBG_CMD)  "cmd_out" trace
 * (1<<DBG_WRN)  give explanation before auto-probing
 * (1<<DBG_MUL)  multi session code test
 * (1<<DBG_ID)   "drive_id != 0" test code
 * (1<<DBG_IOX)  some special information
 * (1<<DBG_DID)  drive ID test
 * (1<<DBG_RES)  drive reset info
 * (1<<DBG_SPI)  SpinUp test info
 * (1<<DBG_IOS)  ioctl trace: "subchannel"
 * (1<<DBG_IO2)  ioctl trace: general
 * (1<<DBG_UPC)  show UPC info
 * (1<<DBG_XA)   XA mode debugging
 * (1<<DBG_LCK)  door (un)lock info
 * (1<<DBG_SQ)   dump SubQ frame
 * (1<<DBG_AUD)  "read audio" debugging
 * (1<<DBG_SEQ)  Sequoia interface configuration trace
 * (1<<DBG_LCS)  Longshine LCS-7260 debugging trace
 * (1<<DBG_TEA)  TEAC CD-55A debugging trace
 * (1<<DBG_CD2)  MKE CD200 debugging trace
 * (1<<DBG_000)  unnecessary information
 */
#if DISTRIBUTION
static int sbpcd_debug =  (1<<DBG_INF) |
                          (1<<DBG_WRN) |
                          (1<<DBG_MUL);
#else
static int sbpcd_debug =  (1<<DBG_INF) |
                          (1<<DBG_TOC) |
                          (1<<DBG_MUL) |
                          (1<<DBG_UPC);
#endif DISTRIBUTION

static int sbpcd_ioaddr = CDROM_PORT;	/* default I/O base address */
static int sbpro_type = SBPRO;
static unsigned char setup_done = 0;
static int CDo_command, CDo_reset;
static int CDo_sel_i_d, CDo_enable;
static int CDi_info, CDi_status, CDi_data;
static int MIXER_addr, MIXER_data;
static struct cdrom_msf msf;
static struct cdrom_ti ti;
static struct cdrom_tochdr tochdr;
static struct cdrom_tocentry tocentry;
static struct cdrom_subchnl SC;
static struct cdrom_volctrl volctrl;
static struct cdrom_read_audio read_audio;
static struct cdrom_multisession ms_info;

static char *str_sb = "SoundBlaster";
static char *str_lm = "LaserMate";
static char *str_sp = "SPEA";
char *type;
#if !(SBPCD_ISSUE-1)
static char *major_name="sbpcd";
#endif
#if !(SBPCD_ISSUE-2)
static char *major_name="sbpcd2";
#endif
#if !(SBPCD_ISSUE-3)
static char *major_name="sbpcd3";
#endif
#if !(SBPCD_ISSUE-4)
static char *major_name="sbpcd4";
#endif

/*==========================================================================*/

#if FUTURE
static struct wait_queue *sbp_waitq = NULL;
#endif FUTURE

/*==========================================================================*/
#define SBP_BUFFER_FRAMES 4 /* driver's own read_ahead, data mode */
#define SBP_BUFFER_AUDIO_FRAMES READ_AUDIO /* buffer for read audio mode */

/*==========================================================================*/

static u_char family0[]="MATSHITA"; /* MKE CR-52x */
static u_char family1[]="CR-56"; /* MKE CR-56x */
static u_char family2[]="CD200"; /* MKE CD200 */
static u_char familyL[]="LCS-7260"; /* Longshine LCS-7260 */
static u_char familyT[]="CD-55A"; /* TEAC CD-55A (still unknown)*/

static u_int response_count=0;
static u_int flags_cmd_out;
static u_char cmd_type=0;
static u_char drvcmd[10];
static u_char infobuf[20];
static u_char xa_head_buf[CD_XA_HEAD];
static u_char xa_tail_buf[CD_XA_TAIL];

static u_char busy_data=0, busy_audio=0; /* true semaphores would be safer */
static u_char timed_out=0;
static u_int datarate= 1000000;
static u_int maxtim16=16000000;
static u_int maxtim04= 4000000;
static u_int maxtim02= 2000000;
static u_int maxtim_8=   30000;
#if LONG_TIMING
static u_int maxtim_data= 9000;
#else
static u_int maxtim_data= 3000;
#endif LONG_TIMING
#if DISTRIBUTION
static int n_retries=3;
#else
static int n_retries=1;
#endif
/*==========================================================================*/

static int ndrives=0;
static u_char drv_pattern[4]={speed_auto,speed_auto,speed_auto,speed_auto};
static int sbpcd_blocksizes[NR_SBPCD] = {0, };

/*==========================================================================*/
/*
 * drive space begins here (needed separate for each unit) 
 */
static int d=0; /* DriveStruct index: drive number */

static struct {
  char drv_id; /* "jumpered" drive ID or -1 */
  char drv_sel; /* drive select lines bits */

  char drive_model[9];
  u_char firmware_version[4];
  char f_eject; /* auto-eject flag: 0 or 1 */
  u_char *sbp_buf; /* Pointer to internal data buffer,
                           space allocated during sbpcd_init() */
  int sbp_first_frame;  /* First frame in buffer */
  int sbp_last_frame;   /* Last frame in buffer  */
  int sbp_read_frames;   /* Number of frames being read to buffer */
  int sbp_current;       /* Frame being currently read */

  u_char mode;           /* read_mode: READ_M1, READ_M2, READ_SC, READ_AU */
  u_char *aud_buf;                  /* Pointer to audio data buffer,
                                 space allocated during sbpcd_init() */
  u_char drv_type;
  u_char drv_options;
  u_char status_byte;
  u_char diskstate_flags;
  u_char sense_byte;
  
  u_char CD_changed;
  u_char open_count;
  u_char error_byte;
  
  u_char f_multisession;
  u_int lba_multi;
#if MULTISESSION_BY_DRIVER
  u_int last_redirect;
#endif MULTISESSION_BY_DRIVER
  int first_session;
  int last_session;
  
  u_char audio_state;
  u_int pos_audio_start;
  u_int pos_audio_end;
  char vol_chan0;
  u_char vol_ctrl0;
  char vol_chan1;
  u_char vol_ctrl1;
#if 000 /* no supported drive has it */
  char vol_chan2;
  u_char vol_ctrl2;
  char vol_chan3;
  u_char vol_ctrl3;
#endif 000
  
  u_char SubQ_ctl_adr;
  u_char SubQ_trk;
  u_char SubQ_pnt_idx;
  u_int SubQ_run_tot;
  u_int SubQ_run_trk;
  u_char SubQ_whatisthis;
  
  u_char UPC_ctl_adr;
  u_char UPC_buf[7];
  
  int CDsize_blk;
  int frame_size;
  int CDsize_frm;
  
  u_char xa_byte; /* 0x20: XA capabilities */
  u_char n_first_track; /* binary */
  u_char n_last_track; /* binary (not bcd), 0x01...0x63 */
  u_int size_msf; /* time of whole CD, position of LeadOut track */
  u_int size_blk;
  
  u_char TocEnt_nixbyte; /* em */
  u_char TocEnt_ctl_adr;
  u_char TocEnt_number;
  u_char TocEnt_format; /* em */
  u_int TocEnt_address;
  u_char ored_ctl_adr; /* to detect if CDROM contains data tracks */
  
  struct {
    u_char nixbyte; /* em */
    u_char ctl_adr; /* 0x4x: data, 0x0x: audio */
    u_char number;
    u_char format; /* em */ /* 0x00: lba, 0x01: msf */
    u_int address;
  } TocBuffer[MAX_TRACKS+1]; /* last entry faked */ 
  
  int in_SpinUp;
  
} DriveStruct[NR_SBPCD];

/*
 * drive space ends here (needed separate for each unit)
 */
/*==========================================================================*/
/*==========================================================================*/
/*
 * DDI interface
 */
static void sbpcd_dprintf(int level, char *fmt, ...)
{
  char buff[256];
  va_list args;
  extern int vsprintf(char *buf, const char *fmt, va_list args);

  if (! (sbpcd_debug & (1 << level))) return;

  va_start(args, fmt);
  vsprintf(buff, fmt, args);
  va_end(args);
  printk(buff);
#if PRINTK_BUG
  sti(); /* to avoid possible "printk" bug */
#endif
}

/*
 * DDI interface: runtime trace bit pattern maintenance
 */
static int sbpcd_dbg_ioctl(unsigned long arg, int level)
{
  int val;

  val = get_fs_long((int *) arg);
  switch(val)
    {
    case 0:	/* OFF */
      sbpcd_debug = 0;
      break;

    default:
      if (val >= 128) sbpcd_debug &= ~(1 << (val - 128));
      else sbpcd_debug |= (1 << val);
    }
  return(0);
}

/*==========================================================================*/
/*==========================================================================*/
/*
 * Wait a little while (used for polling the drive).  If in initialization,
 * setting a timeout doesn't work, so just loop for a while.
 */
static INLINE void sbp_sleep(u_int jifs)
{
  sti();
  current->state = TASK_INTERRUPTIBLE;
  current->timeout = jiffies + jifs;
  schedule();
  sti();
}

/*==========================================================================*/
/*==========================================================================*/
/*
 *  convert logical_block_address to m-s-f_number (3 bytes only)
 */
static INLINE void lba2msf(int lba, u_char *msf)
{
  lba += CD_BLOCK_OFFSET;
  msf[0] = lba / (CD_SECS*CD_FRAMES);
  lba %= CD_SECS*CD_FRAMES;
  msf[1] = lba / CD_FRAMES;
  msf[2] = lba % CD_FRAMES;
}
/*==========================================================================*/
/*==========================================================================*/
/*
 *  convert msf-bin to msf-bcd
 */
static INLINE void bin2bcdx(u_char *p)  /* must work only up to 75 or 99 */
{
  *p=((*p/10)<<4)|(*p%10);
}
/*==========================================================================*/
static INLINE u_int blk2msf(u_int blk)
{
  MSF msf;
  u_int mm;

  msf.c[3] = 0;
  msf.c[2] = (blk + CD_BLOCK_OFFSET) / (CD_SECS * CD_FRAMES);
  mm = (blk + CD_BLOCK_OFFSET) % (CD_SECS * CD_FRAMES);
  msf.c[1] = mm / CD_FRAMES;
  msf.c[0] = mm % CD_FRAMES;
  return (msf.n);
}
/*==========================================================================*/
static INLINE u_int make16(u_char rh, u_char rl)
{
  return ((rh<<8)|rl);
}
/*==========================================================================*/
static INLINE u_int make32(u_int rh, u_int rl)
{
  return ((rh<<16)|rl);
}
/*==========================================================================*/
static INLINE u_char swap_nibbles(u_char i)
{
  return ((i<<4)|(i>>4));
}
/*==========================================================================*/
static INLINE u_char byt2bcd(u_char i)
{
  return (((i/10)<<4)+i%10);
}
/*==========================================================================*/
static INLINE u_char bcd2bin(u_char bcd)
{
  return ((bcd>>4)*10+(bcd&0x0F));
}
/*==========================================================================*/
static INLINE int msf2blk(int msfx)
{
  MSF msf;
  int i;

  msf.n=msfx;
  i=(msf.c[2] * CD_SECS + msf.c[1]) * CD_FRAMES + msf.c[0] - CD_BLOCK_OFFSET;
  if (i<0) return (0);
  return (i);
}
/*==========================================================================*/
/*
 *  convert m-s-f_number (3 bytes only) to logical_block_address 
 */
static INLINE int msf2lba(u_char *msf)
{
  int i;

  i=(msf[0] * CD_SECS + msf[1]) * CD_FRAMES + msf[2] - CD_BLOCK_OFFSET;
  if (i<0) return (0);
  return (i);
}
/*==========================================================================*/
/* evaluate xx_ReadError code (still mysterious) */ 
static int sta2err(int sta)
{
  if (sta<=2) return (sta);
  if (sta==0x05) return (-4); /* CRC error */
  if (sta==0x06) return (-6); /* seek error */
  if (sta==0x0d) return (-6); /* seek error */
  if (sta==0x0e) return (-3); /* unknown command */
  if (sta==0x14) return (-3); /* unknown command */
  if (sta==0x0c) return (-11); /* read fault */
  if (sta==0x0f) return (-11); /* read fault */
  if (sta==0x10) return (-11); /* read fault */
  if (sta>=0x16) return (-12); /* general failure */
  DriveStruct[d].CD_changed=0xFF;
  if (sta==0x11) return (-15); /* invalid disk change (LCS: removed) */
  if (famL_drive)
    if (sta==0x12) return (-15); /* invalid disk change (inserted) */
  return (-2); /* drive not ready */
}
/*==========================================================================*/
static INLINE void clr_cmdbuf(void)
{
  int i;

  for (i=0;i<10;i++) drvcmd[i]=0;
  cmd_type=0;
}
/*==========================================================================*/
static void mark_timeout(unsigned long i)
{
  timed_out=1;
  DPRINTF((DBG_TIM,"SBPCD: timer expired.\n"));
}
/*==========================================================================*/
static struct timer_list delay_timer = { NULL, NULL, 0, 0, mark_timeout};
#if 0
static struct timer_list data_timer = { NULL, NULL, 0, 0, mark_timeout};
static struct timer_list audio_timer = { NULL, NULL, 0, 0, mark_timeout};
#endif
/*==========================================================================*/
static void flush_status(void)
{
#ifdef CDMKE
  int i;

  if (current == task[0])
    for (i=maxtim02;i!=0;i--) inb(CDi_status);
  else 
    {
      sbp_sleep(150);
      for (i=maxtim_data;i!=0;i--) inb(CDi_status);
    }
#else
  timed_out=0;
#if 0
  del_timer(&delay_timer);
#endif
  delay_timer.expires = 150;
  add_timer(&delay_timer);
  do { }
  while (!timed_out);
#if 0
  del_timer(&delay_timer);
#endif 0
  inb(CDi_status);
#endif CDMKE
}
/*==========================================================================*/
static int CDi_stat_loop(void)
{
  int i,j;
  u_long timeout;
  
  if (current == task[0])
    for(i=maxtim16;i!=0;i--)
      {
	j=inb(CDi_status);
        if (!(j&s_not_data_ready)) return (j);
        if (!(j&s_not_result_ready)) return (j);
        if (fam0L_drive) if (j&s_attention) return (j);
      }
  else
    for(timeout = jiffies + 1000, i=maxtim_data; timeout > jiffies; )
      {
	for ( ;i!=0;i--)
	  {
	    j=inb(CDi_status);
	    if (!(j&s_not_data_ready)) return (j);
	    if (!(j&s_not_result_ready)) return (j);
	    if (fam0L_drive) if (j&s_attention) return (j);
	  }
	sbp_sleep(1);
	i = 1;
      }
  DPRINTF((DBG_LCS,"SBPCD: CDi_stat_loop failed\n"));
  return (-1);
}
/*==========================================================================*/
#if TEAC-X
/*==========================================================================*/
static int tst_DataReady(void)
{
  int i;
  
  i=inb(CDi_status);
  if (i&s_not_data_ready) return (0);
  return (1);
}
/*==========================================================================*/
static int tst_ResultReady(void)
{
  int i;
  
  i=inb(CDi_status);
  if (i&s_not_result_ready) return (0);
  return (1);
}
/*==========================================================================*/
static int tst_Attention(void)
{
  int i;
  
  i=inb(CDi_status);
  if (i&s_attention) return (1);
  return (0);
}
/*==========================================================================*/
#endif TEAC-X
/*==========================================================================*/
static int ResponseInfo(void)
{
  int i,j, st=0;
  u_long timeout;

  DPRINTF((DBG_000,"SBPCD: ResponseInfo entered.\n"));
  if (current == task[0])
    for (i=0;i<response_count;i++)
      {
	for (j=maxtim_8;j!=0;j--)
	  {
	    st=inb(CDi_status);
	    if (!(st&s_not_result_ready)) break;
	  }
	if (j==0)
	  {
	    DPRINTF((DBG_SEQ,"SBPCD: ResponseInfo: not_result_ready (got %d of %d bytes).\n", i, response_count));
	    return (-1);
	  }
	infobuf[i]=inb(CDi_info);
      }
  else 
    {
      for (i=0, timeout = jiffies + 100; i < response_count; i++) 
	{
	  for (j=maxtim_data; ; )
	    {
	      for ( ;j!=0;j-- )
		{
		  st=inb(CDi_status);
		  if (!(st&s_not_result_ready)) break;
		}
	      if (j != 0 || timeout <= jiffies) break;
	      sbp_sleep(0);
	      j = 1;
	    }
	  if (timeout <= jiffies) return (-1);
	  infobuf[i]=inb(CDi_info);
	}
    }
  DPRINTF((DBG_000,"SBPCD: ResponseInfo: done.\n"));
  return (0);
}
/*==========================================================================*/
static int EvaluateStatus(int st)
{
  if (fam0_drive)
    {
      DriveStruct[d].status_byte=0;
      if (st&p_caddin_old) DriveStruct[d].status_byte |= p_door_closed|p_caddy_in;
      if (st&p_spinning) DriveStruct[d].status_byte |= p_spinning;
      if (st&p_check) DriveStruct[d].status_byte |= p_check;
      if (st&p_busy_old) DriveStruct[d].status_byte |= p_busy_new;
      if (st&p_disk_ok) DriveStruct[d].status_byte |= p_disk_ok;
    }
  else if (famL_drive)
    {
      DriveStruct[d].status_byte=0;
      if (st&p_caddin_old) DriveStruct[d].status_byte |= p_disk_ok|p_caddy_in;
      if (st&p_spinning) DriveStruct[d].status_byte |= p_spinning;
      if (st&p_check) DriveStruct[d].status_byte |= p_check;
      if (st&p_busy_old) DriveStruct[d].status_byte |= p_busy_new;
      if (st&p_lcs_door_closed) DriveStruct[d].status_byte |= p_door_closed;
      if (st&p_lcs_door_locked) DriveStruct[d].status_byte |= p_door_locked;
      }
  else if (fam1_drive)
    {
      DriveStruct[d].status_byte=st;
      st=p_success_old; /* for new drives: fake "successful" bit of old drives */
    }
  else /* CD200, CD-55A */
    {
    }
  return (st);
}
/*==========================================================================*/
static int ResponseStatus(void)
{
  int i,j;
  u_long timeout;

  DPRINTF((DBG_STA,"SBPCD: doing ResponseStatus...\n"));

  if (current == task[0])
    {
      if (flags_cmd_out & f_respo3) j = maxtim_8;
      else if (flags_cmd_out&f_respo2) j=maxtim16;
      else j=maxtim04;
      for (;j!=0;j--)
	{
	  i=inb(CDi_status);
	  if (!(i&s_not_result_ready)) break;
	}
    }
  else
    {
      if (flags_cmd_out & f_respo3) timeout = jiffies;
      else if (flags_cmd_out & f_respo2) timeout = jiffies + 1600;
      else timeout = jiffies + 400;
      j=maxtim_8;
      do
	{
	  for ( ;j!=0;j--)
	    { 
	      i=inb(CDi_status);
	      if (!(i&s_not_result_ready)) break;
	    }
	  if (j != 0 || timeout <= jiffies) break;
	  sbp_sleep(0);
	  j = 1;
	}
      while (1);
    }
  if (j==0) 
    { if ((flags_cmd_out & f_respo3) == 0)
	DPRINTF((DBG_STA,"SBPCD: ResponseStatus: timeout.\n"));
      EvaluateStatus(0);
      return (-1);
    }
  i=inb(CDi_info);
  DPRINTF((DBG_STA,"SBPCD: ResponseStatus: response %2X.\n", i));
  i=EvaluateStatus(i);
  return (i);
}
/*==========================================================================*/
static void xx_ReadStatus(void)
{
  int i;

  DPRINTF((DBG_STA,"SBPCD: giving xx_ReadStatus command\n"));
  SBPCD_CLI;
  if (fam0L_drive) OUT(CDo_command,CMD0_STATUS);
  else if (fam1_drive) OUT(CDo_command,CMD1_STATUS);
  else /* CD200, CD-55A */
    {
    }
  if (!fam0L_drive) for (i=0;i<6;i++) OUT(CDo_command,0);
  SBPCD_STI;
}
/*==========================================================================*/
static int xx_ReadError(void)
{
  int i;

  clr_cmdbuf();
  DPRINTF((DBG_ERR,"SBPCD: giving xx_ReadError command.\n"));
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_READ_ERR;
      response_count=8;
      flags_cmd_out=f_putcmd|f_ResponseStatus;
    }
  else if (fam0L_drive)
    {
      drvcmd[0]=CMD0_READ_ERR;
      response_count=6;
      if (famL_drive)
	flags_cmd_out=f_putcmd;
      else
	flags_cmd_out=f_putcmd|f_getsta|f_ResponseStatus;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_READ_ERR;
      response_count=6;
      flags_cmd_out=f_putcmd;
    }
  else /* CD-55A */
    {
      return (-1);
      drvcmd[0]=CMDT_READ_ERR;
      response_count=5;
      flags_cmd_out=f_putcmd;
    }
  i=cmd_out(7);
  DriveStruct[d].error_byte=0;
  DPRINTF((DBG_ERR,"SBPCD: xx_ReadError: cmd_out(82) returns %d (%02X)\n",i,i));
  if (i<0) return (i);
  if (fam0_drive) i=1;
  else i=2;
  DriveStruct[d].error_byte=infobuf[i];
  DPRINTF((DBG_ERR,"SBPCD: xx_ReadError: infobuf[%d] is %d (%02X)\n",i,DriveStruct[d].error_byte,DriveStruct[d].error_byte));
  i=sta2err(infobuf[i]);
  return (i);
}
/*==========================================================================*/
static int cmd_out(int len)
{
  int i=0;

  if (flags_cmd_out&f_putcmd)
    { 
      DPRINTF((DBG_CMD,"SBPCD: cmd_out: put"));
      for (i=0;i<len;i++) DPRINTF((DBG_CMD," %02X",drvcmd[i]));
      DPRINTF((DBG_CMD,"\n"));

      cli();
      for (i=0;i<len;i++) OUT(CDo_command,drvcmd[i]);
      sti();
    }
  if (response_count!=0)
    {
      if (cmd_type!=0)
	{
	  if (sbpro_type==1) OUT(CDo_sel_i_d,1);
	  DPRINTF((DBG_INF,"SBPCD: misleaded to try ResponseData.\n"));
	  if (sbpro_type==1) OUT(CDo_sel_i_d,0);
	  return (-22);
	}
      else i=ResponseInfo();
      if (i<0) return (-9);
    }
  if (DriveStruct[d].in_SpinUp != 0) DPRINTF((DBG_SPI,"SBPCD: to CDi_stat_loop.\n"));
  if (flags_cmd_out&f_lopsta)
    {
      i=CDi_stat_loop();
      if ((i<0)||!(i&s_attention)) return (-9);
    }
  if (!(flags_cmd_out&f_getsta)) goto LOC_229;
  
LOC_228:
  if (DriveStruct[d].in_SpinUp != 0) DPRINTF((DBG_SPI,"SBPCD: to xx_ReadStatus.\n"));
  xx_ReadStatus();

LOC_229:
  if (flags_cmd_out&f_ResponseStatus) 
    {
      if (DriveStruct[d].in_SpinUp != 0) DPRINTF((DBG_SPI,"SBPCD: to ResponseStatus.\n"));
      i=ResponseStatus();
                   /* builds status_byte, returns orig. status or p_busy_new */
      if (i<0) return (-9);
      if (flags_cmd_out&(f_bit1|f_wait_if_busy))
	{
	  if (!st_check)
	    {
	      if (flags_cmd_out&f_bit1) if (i&p_success_old) goto LOC_232;
	      if (!(flags_cmd_out&f_wait_if_busy)) goto LOC_228;
	      if (!st_busy) goto LOC_228;
	    }
	}
    }
LOC_232:
  if (!(flags_cmd_out&f_obey_p_check)) return (0);
  if (!st_check) return (0);
  if (DriveStruct[d].in_SpinUp != 0) DPRINTF((DBG_SPI,"SBPCD: to xx_ReadError.\n"));
  i=xx_ReadError();
  if (DriveStruct[d].in_SpinUp != 0) DPRINTF((DBG_SPI,"SBPCD: to cmd_out OK.\n"));
  return (i);
}
/*==========================================================================*/
static int xx_Seek(u_int pos, char f_blk_msf)
{
  int i;

  clr_cmdbuf();
  if (f_blk_msf>1) return (-3);
  if (fam0_drive)
    {
      drvcmd[0]=CMD0_SEEK;
      if (f_blk_msf==1) pos=msf2blk(pos);
      drvcmd[2]=(pos>>16)&0x00FF;
      drvcmd[3]=(pos>>8)&0x00FF;
      drvcmd[4]=pos&0x00FF;
      flags_cmd_out = f_putcmd | f_respo2 | f_lopsta | f_getsta |
                      f_ResponseStatus | f_obey_p_check | f_bit1;
    }
  else if (fam1L_drive)
    {
      drvcmd[0]=CMD1_SEEK; /* same as CMD1_ and CMDL_ */
      if (f_blk_msf==0) pos=blk2msf(pos);
      drvcmd[1]=(pos>>16)&0x00FF;
      drvcmd[2]=(pos>>8)&0x00FF;
      drvcmd[3]=pos&0x00FF;
      if (famL_drive)
	flags_cmd_out=f_putcmd|f_respo2|f_lopsta|f_getsta|f_ResponseStatus|f_obey_p_check|f_bit1;
      else
	flags_cmd_out=f_putcmd|f_respo2|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_SEEK;
      if (f_blk_msf==0) pos=blk2msf(pos);
      drvcmd[2]=(pos>>16)&0x00FF;
      drvcmd[3]=(pos>>16)&0x00FF;
      drvcmd[4]=(pos>>8)&0x00FF;
      drvcmd[5]=pos&0x00FF;
      flags_cmd_out=f_putcmd|f_ResponseStatus;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  response_count=0;
  i=cmd_out(7);
  return (i);
}
/*==========================================================================*/
static int xx_SpinUp(void)
{
  int i;

  DPRINTF((DBG_SPI,"SBPCD: SpinUp.\n"));
  DriveStruct[d].in_SpinUp = 1;
  clr_cmdbuf();
  if (fam0L_drive)
    {
      drvcmd[0]=CMD0_SPINUP;
      flags_cmd_out=f_putcmd|f_respo2|f_lopsta|f_getsta|
	f_ResponseStatus|f_obey_p_check|f_bit1;
    }
  else if (fam1_drive)
    {
      drvcmd[0]=CMD1_SPINUP;
      flags_cmd_out=f_putcmd|f_respo2|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_TRAY_CTL;
      drvcmd[4]=0x01;
      flags_cmd_out=f_putcmd|f_respo2|f_ResponseStatus|f_obey_p_check;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  response_count=0;
  i=cmd_out(7);
  DriveStruct[d].in_SpinUp = 0;
  return (i);
}
/*==========================================================================*/
static int yy_SpinDown(void)
{
  int i;

  if (fam0_drive) return (-3);
  clr_cmdbuf();
  response_count=0;
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_SPINDOWN;
      flags_cmd_out=f_putcmd|f_respo2|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_TRAY_CTL;
      drvcmd[4]=0x02; /* "eject" */
      flags_cmd_out=f_putcmd|f_ResponseStatus;
    }
  else if (famL_drive)
    {
      drvcmd[0]=CMDL_SPINDOWN;
      drvcmd[1]=1;
      flags_cmd_out=f_putcmd|f_respo2|f_lopsta|f_getsta|f_ResponseStatus|f_obey_p_check|f_bit1;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  i=cmd_out(7);
  return (i);
}
/*==========================================================================*/
static int yy_SetSpeed(u_char speed, u_char x1, u_char x2)
{
  int i;

  if (fam0L_drive) return (-3);
  clr_cmdbuf();
  response_count=0;
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_SETMODE;
      drvcmd[1]=0x03;
      drvcmd[2]=speed;
      drvcmd[3]=x1;
      drvcmd[4]=x2;
      flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_SETSPEED;
      if (speed&speed_auto)
	{
	  drvcmd[2]=0xFF;
	  drvcmd[3]=0xFF;
	}
      else
	{
	  drvcmd[2]=0;
	  drvcmd[3]=150;
	}
      flags_cmd_out=f_putcmd|f_ResponseStatus;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  i=cmd_out(7);
  return (i);
}
/*==========================================================================*/
static int xx_SetVolume(void)
{
  int i;
  u_char channel0,channel1,volume0,volume1;
  u_char control0,value0,control1,value1;

  DriveStruct[d].diskstate_flags &= ~volume_bit;
  clr_cmdbuf();
  channel0=DriveStruct[d].vol_chan0;
  volume0=DriveStruct[d].vol_ctrl0;
  channel1=control1=DriveStruct[d].vol_chan1;
  volume1=value1=DriveStruct[d].vol_ctrl1;
  control0=value0=0;

  if (((DriveStruct[d].drv_options&audio_mono)!=0)&&(DriveStruct[d].drv_type>=drv_211))
    {
      if ((volume0!=0)&&(volume1==0))
	{
	  volume1=volume0;
	  channel1=channel0;
	}
      else if ((volume0==0)&&(volume1!=0))
	{
	  volume0=volume1;
	  channel0=channel1;
	}
    }
  if (channel0>1)
    {
      channel0=0;
      volume0=0;
    }
  if (channel1>1)
    {
      channel1=1;
      volume1=0;
    }
  
  if (fam1_drive)
    {
      control0=channel0+1;
      control1=channel1+1;
      value0=(volume0>volume1)?volume0:volume1;
      value1=value0;
      if (volume0==0) control0=0;
      if (volume1==0) control1=0;
      drvcmd[0]=CMD1_SETMODE;
      drvcmd[1]=0x05;
      drvcmd[3]=control0;
      drvcmd[4]=value0;
      drvcmd[5]=control1;
      drvcmd[6]=value1;
      flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      control0=channel0+1;
      control1=channel1+1;
      value0=(volume0>volume1)?volume0:volume1;
      value1=value0;
      if (volume0==0) control0=0;
      if (volume1==0) control1=0;
      drvcmd[0]=CMD2_SETMODE;
      drvcmd[1]=0x0E;
      drvcmd[3]=control0;
      drvcmd[4]=value0;
      drvcmd[5]=control1;
      drvcmd[6]=value1;
      flags_cmd_out=f_putcmd|f_ResponseStatus;
    }
  else if (famL_drive)
    {
      if ((volume0==0)||(channel0!=0)) control0 |= 0x80;
      if ((volume1==0)||(channel1!=1)) control0 |= 0x40;
      if (volume0|volume1) value0=0x80;
      drvcmd[0]=CMDL_SETMODE;
      drvcmd[1]=0x03;
      drvcmd[4]=control0;
      drvcmd[5]=value0;
      flags_cmd_out=f_putcmd|f_lopsta|f_getsta|f_ResponseStatus|f_obey_p_check|f_bit1;
    }
  else if (fam0_drive) /* different firmware levels */
    {
      if (DriveStruct[d].drv_type>=drv_300)
	{
	  control0=volume0&0xFC;
	  value0=volume1&0xFC;
	  if ((volume0!=0)&&(volume0<4)) control0 |= 0x04;
	  if ((volume1!=0)&&(volume1<4)) value0 |= 0x04;
	  if (channel0!=0) control0 |= 0x01;
	  if (channel1==1) value0 |= 0x01;
	}
      else
	{
	  value0=(volume0>volume1)?volume0:volume1;
	  if (DriveStruct[d].drv_type<drv_211)
	    {
	      if (channel0!=0)
		{
		  i=channel1;
		  channel1=channel0;
		  channel0=i;
		  i=volume1;
		  volume1=volume0;
		  volume0=i;
		}
	      if (channel0==channel1)
		{
		  if (channel0==0)
		    {
		      channel1=1;
		      volume1=0;
		      volume0=value0;
		    }
		  else
		    {
		      channel0=0;
		      volume0=0;
		      volume1=value0;
		    }
		}
	    }

	  if ((volume0!=0)&&(volume1!=0))
	    {
	      if (volume0==0xFF) volume1=0xFF;
	      else if (volume1==0xFF) volume0=0xFF;
	    }
	  else if (DriveStruct[d].drv_type<drv_201) volume0=volume1=value0;

	  if (DriveStruct[d].drv_type>=drv_201)
	    {
	      if (volume0==0) control0 |= 0x80;
	      if (volume1==0) control0 |= 0x40;
	    }
	  if (DriveStruct[d].drv_type>=drv_211)
	    {
	      if (channel0!=0) control0 |= 0x20;
	      if (channel1!=1) control0 |= 0x10;
	    }
	}
      drvcmd[0]=CMD0_SETMODE;
      drvcmd[1]=0x83;
      drvcmd[4]=control0;
      drvcmd[5]=value0;
      flags_cmd_out=f_putcmd|f_getsta|f_ResponseStatus|f_obey_p_check;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  response_count=0;
  i=cmd_out(7);
  if (i>0) return (i);
  DriveStruct[d].diskstate_flags |= volume_bit;
  return (0);
}
/*==========================================================================*/
static int GetStatus(void)
{
  int i;

  flags_cmd_out=f_getsta|f_ResponseStatus|f_obey_p_check;
  response_count=0;
  cmd_type=0;
  i=cmd_out(7);
  return (i);
}
/*==========================================================================*/
static int xy_DriveReset(void)
{
  int i;

  DPRINTF((DBG_RES,"SBPCD: xy_DriveReset called.\n"));
  clr_cmdbuf();
  response_count=0;
  if (fam0L_drive) OUT(CDo_reset,0x00);
  else if (fam1_drive)
    {
      drvcmd[0]=CMD1_RESET;
      flags_cmd_out=f_putcmd;
      i=cmd_out(7);
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_RESET;
      flags_cmd_out=f_putcmd;
      i=cmd_out(7);
      OUT(CDo_reset,0x00);
    }
  else /* CD-55A */
    {
      return (-1);
    }
  if (famL_drive) sbp_sleep(500); /* wait 5 seconds */
  else sbp_sleep(100); /* wait a second */
  flush_status();
  i=GetStatus();
  if (i>=0) return -1;
  if (DriveStruct[d].error_byte!=aud_12) return -1;
  return (0);
}
/*==========================================================================*/
static int SetSpeed(void)
{
  int i, speed;

  if (!(DriveStruct[d].drv_options&(speed_auto|speed_300|speed_150))) return (0);
  speed=speed_auto;
  if (!(DriveStruct[d].drv_options&speed_auto))
    {
      speed |= speed_300;
      if (!(DriveStruct[d].drv_options&speed_300)) speed=0;
    }
  i=yy_SetSpeed(speed,0,0);
  return (i);
}
/*==========================================================================*/
static int DriveReset(void)
{
  int i;

  i=xy_DriveReset();
  if (i<0) return (-2);
  do
    {
      i=GetStatus();
      if ((i<0)&&(i!=-15)) return (-2); /* i!=-15 is from sta2err */
      if (!st_caddy_in) break;
    }
  while (!st_diskok);
#if 000
  DriveStruct[d].CD_changed=1;
#endif
  if ((st_door_closed) && (st_caddy_in))
    {
      i=DiskInfo();
      if (i<0) return (-2);
    }
  return (0);
}
/*==========================================================================*/
static int xx_Pause_Resume(int pau_res)
{
  int i;

  clr_cmdbuf();
  response_count=0;
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_PAU_RES;
      if (pau_res!=1) drvcmd[1]=0x80;
      flags_cmd_out=f_putcmd|f_respo2|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_PAU_RES;
      if (pau_res!=1) drvcmd[2]=0x01;
      flags_cmd_out=f_putcmd|f_ResponseStatus;
    }
  else if (fam0L_drive)
    {
      drvcmd[0]=CMD0_PAU_RES;
      if (pau_res!=1) drvcmd[1]=0x80;
      if (famL_drive)
	flags_cmd_out=f_putcmd|f_respo2|f_lopsta|f_getsta|f_ResponseStatus|
	  f_obey_p_check|f_bit1;
      else
	flags_cmd_out=f_putcmd|f_respo2|f_lopsta|f_getsta|f_ResponseStatus|
	  f_obey_p_check;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  i=cmd_out(7);
  return (i);
}
/*==========================================================================*/
static int yy_LockDoor(char lock)
{
  int i;

  if (fam0_drive) return (0);
  DPRINTF((DBG_LCK,"SBPCD: yy_LockDoor: %d (drive %d)\n", lock, d));
  DPRINTF((DBG_LCS,"SBPCD: p_door_locked bit %d before\n", st_door_locked));
  clr_cmdbuf();
  response_count=0;
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_LOCK_CTL;
      if (lock==1) drvcmd[1]=0x01;
      flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_LOCK_CTL;
      if (lock==1) drvcmd[4]=0x01;
      flags_cmd_out=f_putcmd|f_ResponseStatus;
    }
  else if (famL_drive)
    {
      drvcmd[0]=CMDL_LOCK_CTL;
      if (lock==1) drvcmd[1]=0x01;
      flags_cmd_out=f_putcmd|f_respo2|f_lopsta|f_getsta|f_ResponseStatus|f_obey_p_check|f_bit1;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  i=cmd_out(7);
  DPRINTF((DBG_LCS,"SBPCD: p_door_locked bit %d after\n", st_door_locked));
  return (i);
}
/*==========================================================================*/
static int yy_CloseTray(void)
{
  int i;

  if (fam0_drive) return (0);
  DPRINTF((DBG_LCK,"SBPCD: yy_CloseTray (drive %d)\n", d));
  DPRINTF((DBG_LCS,"SBPCD: p_door_closed bit %d before\n", st_door_closed));

  clr_cmdbuf();
  response_count=0;
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_TRAY_CTL;
      flags_cmd_out=f_putcmd|f_respo2|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_TRAY_CTL;
      drvcmd[1]=0x01;
      drvcmd[4]=0x03; /* "insert" */
      flags_cmd_out=f_putcmd|f_ResponseStatus;
    }
  else if (famL_drive)
    {
      drvcmd[0]=CMDL_TRAY_CTL;
      flags_cmd_out=f_putcmd|f_respo2|f_lopsta|f_getsta|
 	f_ResponseStatus|f_obey_p_check|f_bit1;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  i=cmd_out(7);
  DPRINTF((DBG_LCS,"SBPCD: p_door_closed bit %d after\n", st_door_closed));
  return (i);
}
/*==========================================================================*/
static int xx_ReadSubQ(void)
{
  int i,j;

  DriveStruct[d].diskstate_flags &= ~subq_bit;
  for (j=255;j>0;j--)
    {
      clr_cmdbuf();
      if (fam1_drive)
	{
	  drvcmd[0]=CMD1_READSUBQ;
	  flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
	  response_count=11;
	}
      else if (fam2_drive)
	{
	  drvcmd[0]=CMD2_READSUBQ;
	  drvcmd[1]=0x02;
	  drvcmd[3]=0x01;
	  flags_cmd_out=f_putcmd;
	  response_count=10;
	}
      else if (fam0L_drive)
	{
	  drvcmd[0]=CMD0_READSUBQ;
	  drvcmd[1]=0x02;
 	  if (famL_drive)
 	    flags_cmd_out=f_putcmd;
 	  else
 	    flags_cmd_out=f_putcmd|f_getsta|f_ResponseStatus|f_obey_p_check;
	  response_count=13;
	}
      else /* CD-55A */
	{
	  return (-1);
	}
      i=cmd_out(7);
      if (i<0) return (i);
      DPRINTF((DBG_SQ,"SBPCD: xx_ReadSubQ:"));
      for (i=0;i<response_count;i++)
	{
	  DPRINTF((DBG_SQ," %02X", infobuf[i]));
	}
      DPRINTF((DBG_SQ,"\n"));
      if (infobuf[0]!=0) break;
      if ((!st_spinning) || (j==1))
	{
	  DriveStruct[d].SubQ_ctl_adr=DriveStruct[d].SubQ_trk=DriveStruct[d].SubQ_pnt_idx=DriveStruct[d].SubQ_whatisthis=0;
	  DriveStruct[d].SubQ_run_tot=DriveStruct[d].SubQ_run_trk=0;
	  return (0);
	}
    }
  DriveStruct[d].SubQ_ctl_adr=swap_nibbles(infobuf[1]);
  DriveStruct[d].SubQ_trk=byt2bcd(infobuf[2]);
  DriveStruct[d].SubQ_pnt_idx=byt2bcd(infobuf[3]);
  i=4;
  if (fam0L_drive) i=5;
  DriveStruct[d].SubQ_run_tot=make32(make16(0,infobuf[i]),make16(infobuf[i+1],infobuf[i+2])); /* msf-bin */
  i=7;
  if (fam0L_drive) i=9;
  DriveStruct[d].SubQ_run_trk=make32(make16(0,infobuf[i]),make16(infobuf[i+1],infobuf[i+2])); /* msf-bin */
  DriveStruct[d].SubQ_whatisthis=infobuf[i+3];
  DriveStruct[d].diskstate_flags |= subq_bit;
  return (0);
}
/*==========================================================================*/
static int xx_ModeSense(void)
{
  int i;

  if (fam2_drive) return (0);
  DriveStruct[d].diskstate_flags &= ~frame_size_bit;
  clr_cmdbuf();
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_GETMODE;
      flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
      response_count=5;
    }
  else if (fam0L_drive)
    {
      drvcmd[0]=CMD0_GETMODE;
      if (famL_drive)
	flags_cmd_out=f_putcmd;
      else
	flags_cmd_out=f_putcmd|f_getsta|f_ResponseStatus|f_obey_p_check;
      response_count=2;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  i=cmd_out(7);
  if (i<0) return (i);
  i=0;
  if (fam1_drive) DriveStruct[d].sense_byte=infobuf[i++];
  else if (fam0L_drive) DriveStruct[d].sense_byte=0;
  else /* CD-55A */
    {
    }
  DriveStruct[d].frame_size=make16(infobuf[i],infobuf[i+1]);

  DPRINTF((DBG_XA,"SBPCD: xx_ModeSense: "));
  for (i=0;i<(fam1_drive?5:2);i++)
    {
      DPRINTF((DBG_XA,"%02X ", infobuf[i]));
    }
  DPRINTF((DBG_XA,"\n"));

  DriveStruct[d].diskstate_flags |= frame_size_bit;
  return (0);
}
/*==========================================================================*/
/*==========================================================================*/
static int xx_ModeSelect(int framesize)
{
  int i;

  if (fam2_drive) return (0);
  DriveStruct[d].diskstate_flags &= ~frame_size_bit;
  clr_cmdbuf();
  DriveStruct[d].frame_size=framesize;
  if (framesize==CD_FRAMESIZE_RAW) DriveStruct[d].sense_byte=0x82;
  else DriveStruct[d].sense_byte=0x00;

  DPRINTF((DBG_XA,"SBPCD: xx_ModeSelect: %02X %04X\n",
	   DriveStruct[d].sense_byte, DriveStruct[d].frame_size));

  if (fam1_drive)
    {
      drvcmd[0]=CMD1_SETMODE;
      drvcmd[1]=0x00;
      drvcmd[2]=DriveStruct[d].sense_byte;
      drvcmd[3]=(DriveStruct[d].frame_size>>8)&0xFF;
      drvcmd[4]=DriveStruct[d].frame_size&0xFF;
      flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam0L_drive)
    {
      drvcmd[0]=CMD0_SETMODE;
      drvcmd[1]=0x00;
      drvcmd[2]=(DriveStruct[d].frame_size>>8)&0xFF;
      drvcmd[3]=DriveStruct[d].frame_size&0xFF;
      drvcmd[4]=0x00;
      if(famL_drive)
	flags_cmd_out=f_putcmd|f_lopsta|f_getsta|f_ResponseStatus|f_obey_p_check;
      else
	flags_cmd_out=f_putcmd|f_getsta|f_ResponseStatus|f_obey_p_check;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  response_count=0;
  i=cmd_out(7);
  if (i<0) return (i);
  DriveStruct[d].diskstate_flags |= frame_size_bit;
  return (0);
}
/*==========================================================================*/
#if 0000
static int xx_TellVolume(void)
{
  int i;
  u_char switches;
  u_char chan0,vol0,chan1,vol1;

  DriveStruct[d].diskstate_flags &= ~volume_bit;
  clr_cmdbuf();
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_GETMODE;
      drvcmd[1]=0x05;
      response_count=5;
      flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_GETMODE;
      drvcmd[1]=0x0E;
      response_count=5;
      flags_cmd_out=f_putcmd;
    }
  else if (fam0L_drive)
    {
      drvcmd[0]=CMD0_GETMODE;
      drvcmd[1]=0x03;
      response_count=2;
      if(famL_drive)
	flags_cmd_out=f_putcmd;
      else
	flags_cmd_out=f_putcmd|f_getsta|f_ResponseStatus|f_obey_p_check;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  i=cmd_out(7);
  if (i<0) return (i);
  if (fam1_drive)
    {
      chan0=infobuf[1]&0x0F;
      vol0=infobuf[2];
      chan1=infobuf[3]&0x0F;
      vol1=infobuf[4];
      if (chan0==0)
	{
	  chan0=1;
	  vol0=0;
	}
      if (chan1==0)
	{
	  chan1=2;
	  vol1=0;
	}
      chan0 >>= 1;
      chan1 >>= 1;
    }
  else if (fam2_drive)
    {
      chan0=infobuf[1];
      vol0=infobuf[2];
      chan1=infobuf[3];
      vol1=infobuf[4];
    }
  else if (famL_drive)
    {
      chan0=0;
      chan1=1;
      vol0=vol1=infobuf[1];
      switches=infobuf[0];
      if ((switches&0x80)!=0) chan0=1;
      if ((switches&0x40)!=0) chan1=0;
    }
  else if (fam0_drive) /* different firmware levels */
    {
      chan0=0;
      chan1=1;
      vol0=vol1=infobuf[1];
      if (DriveStruct[d].drv_type>=drv_201)
	{
	  if (DriveStruct[d].drv_type<drv_300)
	    {
	      switches=infobuf[0];
	      if ((switches&0x80)!=0) vol0=0;
	      if ((switches&0x40)!=0) vol1=0;
	      if (DriveStruct[d].drv_type>=drv_211)
		{
		  if ((switches&0x20)!=0) chan0=1;
		  if ((switches&0x10)!=0) chan1=0;
		}
            }
          else
	    {
              vol0=infobuf[0];
              if ((vol0&0x01)!=0) chan0=1;
              if ((vol1&0x01)==0) chan1=0;
              vol0 &= 0xFC;
              vol1 &= 0xFC;
              if (vol0!=0) vol0 += 3;
              if (vol1!=0) vol1 += 3;
            }
        }
    }
  else /* CD-55A */
    {
      return (-1);
    }
  DriveStruct[d].vol_chan0=chan0;
  DriveStruct[d].vol_ctrl0=vol0;
  DriveStruct[d].vol_chan1=chan1;
  DriveStruct[d].vol_ctrl1=vol1;
  DriveStruct[d].vol_chan2=2;
  DriveStruct[d].vol_ctrl2=0xFF;
  DriveStruct[d].vol_chan3=3;
  DriveStruct[d].vol_ctrl3=0xFF;
  DriveStruct[d].diskstate_flags |= volume_bit;
  return (0);
}
#endif
/*==========================================================================*/
static int xx_ReadCapacity(void)
{
  int i;

  if (famL_drive) return (0);
  DriveStruct[d].diskstate_flags &= ~cd_size_bit;
  clr_cmdbuf();
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_CAPACITY;
      response_count=5;
      flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_CAPACITY;
      response_count=8;
      flags_cmd_out=f_putcmd;
    }
  else if (fam0_drive)
    {
      drvcmd[0]=CMD0_CAPACITY;
      response_count=5;
      if(famL_drive)
	flags_cmd_out=f_putcmd;
      else
	flags_cmd_out=f_putcmd|f_getsta|f_ResponseStatus|f_obey_p_check;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  i=cmd_out(7);
  if (i<0) return (i);
  if (fam1_drive) DriveStruct[d].CDsize_frm=msf2blk(make32(make16(0,infobuf[0]),make16(infobuf[1],infobuf[2])))+CD_BLOCK_OFFSET;
  else if (fam0_drive) DriveStruct[d].CDsize_frm=make32(make16(0,infobuf[0]),make16(infobuf[1],infobuf[2]));
  else if (fam2_drive) DriveStruct[d].CDsize_frm=make32(make16(infobuf[0],infobuf[1]),make16(infobuf[2],infobuf[3]));
  DriveStruct[d].diskstate_flags |= cd_size_bit;
  return (0);
}
/*==========================================================================*/
static int xx_ReadTocDescr(void)
{
  int i;

  DriveStruct[d].diskstate_flags &= ~toc_bit;
  clr_cmdbuf();
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_DISKINFO;
      response_count=6;
      flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam0L_drive)
    {
      drvcmd[0]=CMD0_DISKINFO;
      response_count=6;
      if(famL_drive)
	flags_cmd_out=f_putcmd;
      else
	flags_cmd_out=f_putcmd|f_getsta|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      /* possibly longer timeout periods necessary */
      DriveStruct[d].f_multisession=0;
      drvcmd[0]=CMD2_DISKINFO;
      drvcmd[1]=0x02;
      drvcmd[2]=0xAB;
      drvcmd[3]=0xFF; /* session */
      response_count=8;
      flags_cmd_out=f_putcmd;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  i=cmd_out(7);
  if (i<0) return (i);
  DriveStruct[d].xa_byte=infobuf[0];
  if (fam2_drive)
    {
      DriveStruct[d].first_session=infobuf[1];
      DriveStruct[d].last_session=infobuf[2];
      DriveStruct[d].n_first_track=infobuf[3];
      DriveStruct[d].n_last_track=infobuf[4];
      if (DriveStruct[d].first_session!=DriveStruct[d].last_session)
	{
	  DriveStruct[d].f_multisession=1;
	  DriveStruct[d].lba_multi=msf2blk(make32(make16(0,infobuf[5]),make16(infobuf[6],infobuf[7])));
	}
#if 0
      if (DriveStruct[d].first_session!=DriveStruct[d].last_session)
	{
	  if (DriveStruct[d].last_session<=20)
	    zwanzig=DriveStruct[d].last_session+1;
	  else zwanzig=20;
	  for (count=DriveStruct[d].first_session;count<zwanzig;count++)
	    {
	      drvcmd[0]=CMD2_DISKINFO;
	      drvcmd[1]=0x02;
	      drvcmd[2]=0xAB;
	      drvcmd[3]=count;
	      response_count=8;
	      flags_cmd_out=f_putcmd;
	      i=cmd_out(7);
	      if (i<0) return (i);
	      DriveStruct[d].msf_multi_n[count]=make32(make16(0,infobuf[5]),make16(infobuf[6],infobuf[7]));
	    }
	  DriveStruct[d].diskstate_flags |= multisession_bit;
	}
#endif
      drvcmd[0]=CMD2_DISKINFO;
      drvcmd[1]=0x02;
      drvcmd[2]=0xAA;
      drvcmd[3]=0xFF;
      response_count=5;
      flags_cmd_out=f_putcmd;
      i=cmd_out(7);
      if (i<0) return (i);
      DriveStruct[d].size_msf=make32(make16(0,infobuf[2]),make16(infobuf[3],infobuf[4]));
      DriveStruct[d].size_blk=msf2blk(DriveStruct[d].size_msf);
    }
  else
    {
      DriveStruct[d].n_first_track=infobuf[1];
      DriveStruct[d].n_last_track=infobuf[2];
      DriveStruct[d].size_msf=make32(make16(0,infobuf[3]),make16(infobuf[4],infobuf[5]));
      DriveStruct[d].size_blk=msf2blk(DriveStruct[d].size_msf);
      if (famL_drive) DriveStruct[d].CDsize_frm=DriveStruct[d].size_blk+1;
    }
  DriveStruct[d].diskstate_flags |= toc_bit;
  DPRINTF((DBG_TOC,"SBPCD: TocDesc: %02X %02X %02X %08X\n",
	   DriveStruct[d].xa_byte,
	   DriveStruct[d].n_first_track,
	   DriveStruct[d].n_last_track,
	   DriveStruct[d].size_msf));
  return (0);
}
/*==========================================================================*/
static int xx_ReadTocEntry(int num)
{
  int i;

  clr_cmdbuf();
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_READTOC;
      response_count=8;
      flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
    }
  else if (fam2_drive)
    {
      /* possibly longer timeout periods necessary */
      drvcmd[0]=CMD2_DISKINFO;
      drvcmd[1]=0x02;
      response_count=5;
      flags_cmd_out=f_putcmd;
    }
  else if (fam0L_drive)
    {
      drvcmd[0]=CMD0_READTOC;
      drvcmd[1]=0x02;
      response_count=8;
      if(famL_drive)
	flags_cmd_out=f_putcmd;
      else
	flags_cmd_out=f_putcmd|f_getsta|f_ResponseStatus|f_obey_p_check;
    }
  else /* CD-55A */
    {
      return (-1);
    }
  drvcmd[2]=num;
  i=cmd_out(7);
  if (i<0) return (i);
  if (fam2_drive) i=0;
  else
    {
      DriveStruct[d].TocEnt_nixbyte=infobuf[0];
      i=1;
    }
  DriveStruct[d].TocEnt_ctl_adr=swap_nibbles(infobuf[i++]);
  if (!(fam2_drive))
    {
      DriveStruct[d].TocEnt_number=infobuf[i++];
      DriveStruct[d].TocEnt_format=infobuf[i];
    }
  if (fam1_drive) i=4;
  else if (fam0L_drive) i=5;
  else if (fam2_drive) i=2;
  DriveStruct[d].TocEnt_address=make32(make16(0,infobuf[i]),
				       make16(infobuf[i+1],infobuf[i+2]));
  DPRINTF((DBG_TOC,"SBPCD: TocEntry: %02X %02X %02X %02X %08X\n",
	   DriveStruct[d].TocEnt_nixbyte, DriveStruct[d].TocEnt_ctl_adr,
	   DriveStruct[d].TocEnt_number, DriveStruct[d].TocEnt_format,
	   DriveStruct[d].TocEnt_address));
  return (0);
}
/*==========================================================================*/
static int xx_ReadPacket(void)
{
  int i;

  clr_cmdbuf();
  drvcmd[0]=CMD0_PACKET;
  drvcmd[1]=response_count;
  if(famL_drive)
    flags_cmd_out=f_putcmd;
  else if (fam01_drive)
    flags_cmd_out=f_putcmd|f_getsta|f_ResponseStatus|f_obey_p_check;
  else /* CD200, CD-55A */
    {
      return (-1);
    }
  i=cmd_out(7);
  return (i);
}
/*==========================================================================*/
static int convert_UPC(u_char *p)
{
  int i;

  p++;
  if (fam0L_drive) p[13]=0;
  for (i=0;i<7;i++)
    {
      if (fam1_drive) DriveStruct[d].UPC_buf[i]=swap_nibbles(*p++);
      else if (fam0L_drive)
	{
	  DriveStruct[d].UPC_buf[i]=((*p++)<<4)&0xFF;
	  DriveStruct[d].UPC_buf[i] |= *p++;
	}
      else /* CD200, CD-55A */
	{
	  return (-1);
	}
    }
  DriveStruct[d].UPC_buf[6] &= 0xF0;
  return (0);
}
/*==========================================================================*/
static int xx_ReadUPC(void)
{
  int i;
#if TEST_UPC
  int block, checksum;
#endif TEST_UPC

  DriveStruct[d].diskstate_flags &= ~upc_bit;
#if TEST_UPC
  for (block=CD_BLOCK_OFFSET+1;block<CD_BLOCK_OFFSET+200;block++)
    {
#endif TEST_UPC
      clr_cmdbuf();
      if (fam1_drive)
	{
	  drvcmd[0]=CMD1_READ_UPC;
#if TEST_UPC
	  drvcmd[1]=(block>>16)&0xFF;
	  drvcmd[2]=(block>>8)&0xFF;
	  drvcmd[3]=block&0xFF;
#endif TEST_UPC
	  response_count=8;
	  flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
	}
      else if (fam0L_drive)
	{
	  drvcmd[0]=CMD0_READ_UPC;
#if TEST_UPC
	  drvcmd[2]=(block>>16)&0xFF;
	  drvcmd[3]=(block>>8)&0xFF;
	  drvcmd[4]=block&0xFF;
#endif TEST_UPC
	  response_count=0;
	  flags_cmd_out=f_putcmd|f_lopsta|f_getsta|f_ResponseStatus|f_obey_p_check|f_bit1;
	}
      else /* CD200, CD-55A */
	{
	  return (-1);
	}
      i=cmd_out(7);
      if (i<0) return (i);
      if (fam0L_drive)
	{
	  response_count=16;
 	  if (famL_drive) flags_cmd_out=f_putcmd;
	  i=xx_ReadPacket();
	  if (i<0) return (i);
	}
#if TEST_UPC
      checksum=0;
#endif TEST_UPC
      DPRINTF((DBG_UPC,"SBPCD: UPC info: "));
      for (i=0;i<(fam1_drive?8:16);i++)
	{
#if TEST_UPC
	  checksum |= infobuf[i];
#endif TEST_UPC
	  DPRINTF((DBG_UPC,"%02X ", infobuf[i]));
	}
      DPRINTF((DBG_UPC,"\n"));
#if TEST_UPC
      if ((checksum&0x7F)!=0) break;
    }
#endif TEST_UPC
  DriveStruct[d].UPC_ctl_adr=0;
  if (fam1_drive) i=0;
  else i=2;
  if ((infobuf[i]&0x80)!=0)
    {
      convert_UPC(&infobuf[i]);
      DriveStruct[d].UPC_ctl_adr = (DriveStruct[d].TocEnt_ctl_adr & 0xF0) | 0x02;
    }

  DPRINTF((DBG_UPC,"SBPCD: UPC code: "));
  DPRINTF((DBG_UPC,"(%02X) ", DriveStruct[d].UPC_ctl_adr));
  for (i=0;i<7;i++)
    {
      DPRINTF((DBG_UPC,"%02X ", DriveStruct[d].UPC_buf[i]));
    }
  DPRINTF((DBG_UPC,"\n"));

  DriveStruct[d].diskstate_flags |= upc_bit;
  return (0);
}
/*==========================================================================*/
static int yy_CheckMultiSession(void)
{
  int i;

  if (fam2_drive) return (0);
  DriveStruct[d].f_multisession=0;
  DriveStruct[d].lba_multi=0;
  if (fam0_drive) return (0);
  clr_cmdbuf();
  if (fam1_drive)
    {
      drvcmd[0]=CMD1_MULTISESS;
      response_count=6;
      flags_cmd_out=f_putcmd|f_ResponseStatus|f_obey_p_check;
      i=cmd_out(7);
      if (i<0) return (i);
      if ((infobuf[0]&0x80)!=0)
	{
	  DriveStruct[d].f_multisession=1;
	  DriveStruct[d].lba_multi=msf2blk(make32(make16(0,infobuf[1]),
						  make16(infobuf[2],infobuf[3])));
#if MULTISESSION_BY_DRIVER
	  DriveStruct[d].last_redirect=19;
#endif MULTISESSION_BY_DRIVER
	  DPRINTF((DBG_MUL,"SBPCD: MultiSession CD detected: %02X %02X %02X %02X %02X %02X (%d)\n",
		   infobuf[0], infobuf[1], infobuf[2],
		   infobuf[3], infobuf[4], infobuf[5],
		   DriveStruct[d].lba_multi));
	}
    }
  else if (famL_drive)
    {
      drvcmd[0]=CMDL_MULTISESS;
      drvcmd[1]=3;
      drvcmd[2]=1;
      response_count=8;
      flags_cmd_out=f_putcmd;
      i=cmd_out(7);
      if (i<0) return (i);
      DriveStruct[d].lba_multi=msf2blk(make32(make16(0,infobuf[5]),
					      make16(infobuf[6],infobuf[7])));
      DPRINTF((DBG_MUL,"SBPCD: MultiSession Info: %02X %02X %02X %02X %02X %02X %02X %02X (%d)\n",
	       infobuf[0], infobuf[1], infobuf[2], infobuf[3],
	       infobuf[4], infobuf[5], infobuf[6], infobuf[7],
	       DriveStruct[d].lba_multi));
      if (DriveStruct[d].lba_multi>200)
	{
	  DPRINTF((DBG_MUL,"SBPCD: MultiSession base: %06X\n", DriveStruct[d].lba_multi));
	  DriveStruct[d].f_multisession=1;
#if MULTISESSION_BY_DRIVER
	  DriveStruct[d].last_redirect=19;
#endif MULTISESSION_BY_DRIVER
	}
    }
  else /* CD-55A */
    {
      return (-1);
    }
  return (0);
}
/*==========================================================================*/
#if FUTURE
static int yy_SubChanInfo(int frame, int count, u_char *buffer)
/* "frame" is a RED BOOK (msf-bin) address */
{
  int i;

  if (fam0L_drive) return (-ENOSYS); /* drive firmware lacks it */
#if 0
  if (DriveStruct[d].audio_state!=audio_playing) return (-ENODATA);
#endif
  clr_cmdbuf();
  drvcmd[0]=CMD1_SUBCHANINF;
  drvcmd[1]=(frame>>16)&0xFF;
  drvcmd[2]=(frame>>8)&0xFF;
  drvcmd[3]=frame&0xFF;
  drvcmd[5]=(count>>8)&0xFF;
  drvcmd[6]=count&0xFF;
  flags_cmd_out=f_putcmd|f_respo2|f_ResponseStatus|f_obey_p_check;
  cmd_type=READ_SC;
  DriveStruct[d].frame_size=CD_FRAMESIZE_SUB;
  i=cmd_out(7); /* which buffer to use? */
  return (i);
}
#endif FUTURE
/*==========================================================================*/
static void check_datarate(void)
{
#ifdef CDMKE
  int i=0;

  DPRINTF((DBG_IOX,"SBPCD: check_datarate entered.\n"));
  datarate=0;
#if TEST_STI
  for (i=0;i<=1000;i++) printk(".");
#endif
  /* set a timer to make (timed_out!=0) after 1.1 seconds */
#if 0
  del_timer(&delay_timer);
#endif
  delay_timer.expires = 110;
  timed_out=0;
  add_timer(&delay_timer);
  DPRINTF((DBG_TIM,"SBPCD: timer started (110).\n"));
  do
    {
      i=inb(CDi_status);
      datarate++;
#if 00000
      if (datarate>0x0FFFFFFF) break;
#endif 00000
    }
  while (!timed_out); /* originally looping for 1.1 seconds */
#if 0
  del_timer(&delay_timer);
#endif 0
  DPRINTF((DBG_TIM,"SBPCD: datarate: %d\n", datarate));
  if (datarate<65536) datarate=65536;

  maxtim16=datarate*16;
  maxtim04=datarate*4;
  maxtim02=datarate*2;
  maxtim_8=datarate/32;
#if LONG_TIMING
  maxtim_data=datarate/100;
#else
  maxtim_data=datarate/300;
#endif LONG_TIMING
  DPRINTF((DBG_TIM,"SBPCD: maxtim_8 %d, maxtim_data %d.\n",
	   maxtim_8, maxtim_data));
#endif CDMKE
}
/*==========================================================================*/
#if TEAC-X
/*==========================================================================*/
static void teac_reset(int drv_id)
{
  int i;

  OUT(CDo_sel_i_d,0);
  OUT(CDo_enable,drv_id);
  OUT(CDo_command,CMDT_RESET);
  for (i=0;i<9;i++) OUT(CDo_command,0);
  DPRINTF((DBG_TEA,"SBPCD: TEAC soft reset.\n"));
  sbp_sleep(100); /* wait a second */
}
/*==========================================================================*/
static int look_for_TEAC_drive(int drv_id)
{
  int i;
  if (sbpro_type!=1) teac_reset(drv_id);

  OUT(CDo_enable,drv_id);
  OUT(CDo_sel_i_d,0);
  i=inb(CDi_status);
  if (i&s_not_result_ready) return (-1); /* drive not present */
  i=inb(CDi_info);
  DPRINTF((DBG_TEA,"SBPCD: TEAC look_for_drive: %02X.\n",i));
  if (i!=0x55) return (-2); /* something else present */
  return (1); /* drive found */
}
/*==========================================================================*/
static int find_teac_drives(void)
{
  int i, j, found;

  found=0;
  for (i=0;i<4;i++)
    {
      j=look_for_TEAC_drive(i);
      if (j<1) continue;
      found++;
      DPRINTF((DBG_TEA,"SBPCD: TEAC drive (id=%d) found.\n",i));
    }
  return (found);
}
/*==========================================================================*/
#endif TEAC-X
/*==========================================================================*/
/*==========================================================================*/
#ifdef CD200

#if 0

static int xx_ReadVersion(int fam)
{
  int i;

  clr_cmdbuf();
  clear_response_buffer(13);
  if (fam==1)
    {
      drvcmd[0]=CMD0_READ_VER; /* same as CMD1_ and CMDL_ */
      response_count=13;
      i=do_cmd(f_putcmd|f_lopsta|f_getsta|f_ResponseStatus);
    }
  else if (fam==2)
    {
      drvcmd[0]=CMD2_READ_VER;
      response_count=12;
      i=do_cmd(f_putcmd);
    }
  else return (-1);
  return (i);
}

static int yy_ReadError(int fam)
{
  int i;

  clr_cmdbuf();
  response_count=9;
  clr_respo_buf(9);
  if (fam==1)
    {
      drvcmd[0]=CMD0_READ_ERR; /* same as CMD1_ and CMDL_ */
      i=do_cmd(f_putcmd|f_lopsta|f_getsta|f_ResponseStatus);
    }
  else if (fam==2)
    {
      drvcmd[0]=CMD2_READ_ERR;
      i=do_cmd(f_putcmd);
    }
  else return (-1);
  return (i);
}
#endif

#endif CD200
/*==========================================================================*/
static void ask_mail(void)
{
  int i;

  printk("SBPCD: please mail the following lines to emoenke@gwdg.de:\n");
  printk("SBPCD: infobuf = \"");
  for (i=0;i<12;i++) printk("%02X ",infobuf[i]);
  printk("\"\nSBPCD: infobuf = \"");
  for (i=0;i<12;i++) printk("%c",infobuf[i]);
  printk("\"\n");
}
/*==========================================================================*/
static int check_version(void)
{
  int i, j;
  u_char lcs_firm[][4]={"A4F4","A E1"};

  DPRINTF((DBG_INI,"SBPCD: check_version entered.\n"));
  DriveStruct[d].drv_type=0;

#if TEAC
  /* check for CD-55A */
  clr_cmdbuf();
  drvcmd[0]=CMDT_READ_ERR;
  response_count=5;
  flags_cmd_out=f_putcmd;
  i=cmd_out(7);
  if (i<0) DPRINTF((DBG_INI,"SBPCD: CMDT_READERR returns %d (ok anyway).\n",i));
  /* read drive version */
  clr_cmdbuf();
  for (i=0;i<12;i++) infobuf[i]=0;
  if (sbpro_type==1) OUT(CDo_sel_i_d,0);
  response_count=12; /* may be too much */
  drvcmd[0]=CMDT_READ_VER;
  drvcmd[4]=response_count;
  flags_cmd_out=f_putcmd;
  i=cmd_out(10); /* possibly only 6 */
  if (i<0) DPRINTF((DBG_INI,"SBPCD: CMDT_READ_VER returns %d\n",i));
#else
  /* check for CD200 */
  clr_cmdbuf();
  drvcmd[0]=CMD2_READ_ERR;
  response_count=9;
  flags_cmd_out=f_putcmd;
  i=cmd_out(7);
  if (i<0) DPRINTF((DBG_INI,"SBPCD: CMD2_READERR returns %d (ok anyway).\n",i));
  /* read drive version */
  clr_cmdbuf();
  for (i=0;i<12;i++) infobuf[i]=0;
  if (sbpro_type==1) OUT(CDo_sel_i_d,0);
#if 0
  OUT(CDo_reset,0);
  sbp_sleep(600);
  OUT(CDo_enable,DriveStruct[d].drv_sel);
#endif
  drvcmd[0]=CMD2_READ_VER;
  response_count=12;
  flags_cmd_out=f_putcmd;
  i=cmd_out(7);
  if (i<0) DPRINTF((DBG_INI,"SBPCD: CMD2_READ_VER returns %d\n",i));
#endif TEAC
  if (i>=0) /* either from CD200 or CD-55A */
    {
      for (i=0, j=0;i<12;i++) j+=infobuf[i];
      if (j)
	{
	  DPRINTF((DBG_ID,"SBPCD: infobuf = \""));
	  for (i=0;i<12;i++) DPRINTF((DBG_ID,"%02X ",infobuf[i]));
	  for (i=0;i<12;i++) DPRINTF((DBG_ID,"%c",infobuf[i]));
	  DPRINTF((DBG_ID,"\"\n"));
	}
      for (i=0;i<5;i++) if (infobuf[i]!=family2[i]) break;
      if (i==5)
	{
	  DriveStruct[d].drive_model[0]='C';
	  DriveStruct[d].drive_model[1]='D';
	  DriveStruct[d].drive_model[2]='2';
	  DriveStruct[d].drive_model[3]='0';
	  DriveStruct[d].drive_model[4]='0';
	  DriveStruct[d].drive_model[5]=infobuf[i++];
	  DriveStruct[d].drive_model[6]=infobuf[i++];
	  DriveStruct[d].drive_model[7]=0;
	  DriveStruct[d].drv_type=drv_fam2;
	}
      else
	{
	  for (i=0;i<5;i++) if (infobuf[i]!=familyT[i]) break;
	  if (i==5)
	    {
	      DriveStruct[d].drive_model[0]='C';
	      DriveStruct[d].drive_model[1]='D';
	      DriveStruct[d].drive_model[2]='-';
	      DriveStruct[d].drive_model[3]='5';
	      DriveStruct[d].drive_model[4]='5';
	      DriveStruct[d].drive_model[5]='A';
	      DriveStruct[d].drive_model[6]=infobuf[i++];
	      DriveStruct[d].drive_model[7]=infobuf[i++];
	      DriveStruct[d].drive_model[8]=0;
	    }
	  DriveStruct[d].drv_type=drv_famT; /* assumed, not sure here */
	}
    }

  if (!DriveStruct[d].drv_type)
    {
      /* check for CR-52x, CR-56x and LCS-7260 */
      /* clear any pending error state */
      clr_cmdbuf();
      drvcmd[0]=CMD0_READ_ERR; /* same as CMD1_ and CMDL_ */
      response_count=9;
      flags_cmd_out=f_putcmd;
      i=cmd_out(7);
      if (i<0) DPRINTF((DBG_INI,"SBPCD: CMD0_READERR returns %d (ok anyway).\n",i));
      /* read drive version */
      clr_cmdbuf();
      for (i=0;i<12;i++) infobuf[i]=0;
      drvcmd[0]=CMD0_READ_VER; /* same as CMD1_ and CMDL_ */
      response_count=12;
      flags_cmd_out=f_putcmd;
      i=cmd_out(7);
      if (i<0) DPRINTF((DBG_INI,"SBPCD: CMD0_READ_VER returns %d\n",i));

      for (i=0, j=0;i<12;i++) j+=infobuf[i];
      if (j)
	{
	  DPRINTF((DBG_ID,"SBPCD: infobuf = \""));
	  for (i=0;i<12;i++) DPRINTF((DBG_ID,"%02X ",infobuf[i]));
	  for (i=0;i<12;i++) DPRINTF((DBG_ID,"%c",infobuf[i]));
	  DPRINTF((DBG_ID,"\"\n"));
	}

      for (i=0;i<4;i++) if (infobuf[i]!=family1[i]) break;
      if (i==4)
	{
	  DriveStruct[d].drive_model[0]='C';
	  DriveStruct[d].drive_model[1]='R';
	  DriveStruct[d].drive_model[2]='-';
	  DriveStruct[d].drive_model[3]='5';
	  DriveStruct[d].drive_model[4]=infobuf[i++];
	  DriveStruct[d].drive_model[5]=infobuf[i++];
	  DriveStruct[d].drive_model[6]=0;
	  DriveStruct[d].drv_type=drv_fam1;
	}
      if (!DriveStruct[d].drv_type)
	{
	  for (i=0;i<8;i++) if (infobuf[i]!=family0[i]) break;
	  if (i==8)
	    {
	      DriveStruct[d].drive_model[0]='C';
	      DriveStruct[d].drive_model[1]='R';
	      DriveStruct[d].drive_model[2]='-';
	      DriveStruct[d].drive_model[3]='5';
	      DriveStruct[d].drive_model[4]='2';
	      DriveStruct[d].drive_model[5]='x';
	      DriveStruct[d].drive_model[6]=0;
	      DriveStruct[d].drv_type=drv_fam0;
	    }
	}
      if (!DriveStruct[d].drv_type)
	{
	  for (i=0;i<8;i++) if (infobuf[i]!=familyL[i]) break;
	  if (i==8)
	    {
	      for (j=0;j<8;j++)
		DriveStruct[d].drive_model[j]=infobuf[j];
	      DriveStruct[d].drive_model[8]=0;
	      DriveStruct[d].drv_type=drv_famL;
	    }
	}
    }
  if (!DriveStruct[d].drv_type)
    {
      DPRINTF((DBG_INI,"SBPCD: check_version: error.\n"));
      return (-1);
    }
  for (j=0;j<4;j++) DriveStruct[d].firmware_version[j]=infobuf[i+j];
  if (famL_drive)
    {
      for (i=0;i<2;i++)
	{
	  for (j=0;j<4;j++)
	    if (DriveStruct[d].firmware_version[j]!=lcs_firm[i][j]) break;
	  if (j==4) break;
	}
      if (j!=4) ask_mail();
      DriveStruct[d].drv_type=drv_260;
    }
  else if (famT_drive)
    {
      printk("\n\nSBPCD: possibly TEAC CD-55A present.\n");
      printk("SBPCD: support is not fulfilled yet - drive gets ignored.\n");
      ask_mail();
      DriveStruct[d].drv_type=0;
      return (-1);
    }
  else
    {
      j = (DriveStruct[d].firmware_version[0] & 0x0F) * 100 +
	(DriveStruct[d].firmware_version[2] & 0x0F) *10 +
	  (DriveStruct[d].firmware_version[3] & 0x0F);
      if (fam0_drive)
	{
	  if (j<200) DriveStruct[d].drv_type=drv_199;
	  else if (j<201) DriveStruct[d].drv_type=drv_200;
	  else if (j<210) DriveStruct[d].drv_type=drv_201;
	  else if (j<211) DriveStruct[d].drv_type=drv_210;
	  else if (j<300) DriveStruct[d].drv_type=drv_211;
	  else if (j>=300) DriveStruct[d].drv_type=drv_300;
	}
      else if (fam1_drive)
	{
	  if (j<100) DriveStruct[d].drv_type=drv_099;
	  else DriveStruct[d].drv_type=drv_100;
	}
      else if (fam2_drive)
	{
	  printk("\n\nSBPCD: new drive CD200 (%s)detected.\n",
		 DriveStruct[d].firmware_version);
	  printk("SBPCD: support is not fulfilled yet.\n");
	  if (j!=101) /* only 1.01 known at time */
	    ask_mail();
	}
    }
  DPRINTF((DBG_LCS,"SBPCD: drive type %02X\n",DriveStruct[d].drv_type));
  DPRINTF((DBG_INI,"SBPCD: check_version done.\n"));
  return (0);
}
/*==========================================================================*/
static int switch_drive(int i)
{
  d=i;
  if (DriveStruct[d].drv_id!=-1)
    {
      OUT(CDo_enable,DriveStruct[d].drv_sel);
      DPRINTF((DBG_DID,"SBPCD: drive %d (ID=%d) activated.\n", i, DriveStruct[d].drv_id));
      return (0);
    }
  else return (-1);
}
/*==========================================================================*/
/*
 * probe for the presence of drives on the selected controller
 */
static int check_drives(void)
{
  int i, j;
  char *printk_header="";

  DPRINTF((DBG_INI,"SBPCD: check_drives entered.\n"));

  ndrives=0;
  for (j=0;j<NR_SBPCD;j++)
    {
      DriveStruct[ndrives].drv_id=j;
      if (sbpro_type==1) DriveStruct[ndrives].drv_sel=(j&0x01)<<1|(j&0x02)>>1;
      else DriveStruct[ndrives].drv_sel=j;
      switch_drive(ndrives);
      DPRINTF((DBG_INI,"SBPCD: check_drives: drive %d (ID=%d) activated.\n",ndrives,j));
      i=check_version();
      if (i<0) DPRINTF((DBG_INI,"SBPCD: check_version returns %d.\n",i));
      else
	{
	  DriveStruct[d].drv_options=drv_pattern[j];
	  if (fam0L_drive) DriveStruct[d].drv_options&=~(speed_auto|speed_300|speed_150);
	  printk("%sDrive %d (ID=%d): %.9s (%.4s)\n", printk_header, d,
		 DriveStruct[d].drv_id,
		 DriveStruct[d].drive_model,
		 DriveStruct[d].firmware_version);
	  printk_header="       - ";
	  ndrives++;
	}

    }
  for (j=ndrives;j<NR_SBPCD;j++) DriveStruct[j].drv_id=-1;
  if (ndrives==0) return (-1);
  return (0);
}
/*==========================================================================*/
#if 000
static void timewait(void)
{
  int i;
  for (i=0; i<65500; i++);
}
#endif 000
/*==========================================================================*/
#if FUTURE
/*
 *  obtain if requested service disturbs current audio state
 */            
static int obey_audio_state(u_char audio_state, u_char func,u_char subfunc)
{
  switch (audio_state)                   /* audio status from controller  */
    {
    case aud_11: /* "audio play in progress" */
    case audx11:
      switch (func)                      /* DOS command code */
	{
	case cmd_07: /* input flush  */
	case cmd_0d: /* open device  */
	case cmd_0e: /* close device */
	case cmd_0c: /* ioctl output */
	  return (1);
	case cmd_03: /* ioctl input  */
	  switch (subfunc)
	    /* DOS ioctl input subfunction */
	    {
	    case cxi_00:
	    case cxi_06:
	    case cxi_09:
	      return (1);
	    default:
	      return (ERROR15);
	    }
	  return (1);
	default:
	  return (ERROR15);
	}
      return (1);
    case aud_12:                  /* "audio play paused"      */
    case audx12:
      return (1);
    default:
      return (2);
    }
}
#endif FUTURE
/*==========================================================================*/
/* allowed is only
 * ioctl_o, flush_input, open_device, close_device, 
 * tell_address, tell_volume, tell_capabiliti,
 * tell_framesize, tell_CD_changed, tell_audio_posi
 */
static int check_allowed1(u_char func1, u_char func2)
{
#if 000
  if (func1==ioctl_o) return (0);
  if (func1==read_long) return (-1);
  if (func1==read_long_prefetch) return (-1);
  if (func1==seek) return (-1);
  if (func1==audio_play) return (-1);
  if (func1==audio_pause) return (-1);
  if (func1==audio_resume) return (-1);
  if (func1!=ioctl_i) return (0);
  if (func2==tell_SubQ_run_tot) return (-1);
  if (func2==tell_cdsize) return (-1);
  if (func2==tell_TocDescrip) return (-1);
  if (func2==tell_TocEntry) return (-1);
  if (func2==tell_subQ_info) return (-1);
  if (fam1_drive) if (func2==tell_SubChanInfo) return (-1);
  if (func2==tell_UPC) return (-1);
#else
  return (0);
#endif 000
}
/*==========================================================================*/
static int check_allowed2(u_char func1, u_char func2)
{
#if 000
  if (func1==read_long) return (-1);
  if (func1==read_long_prefetch) return (-1);
  if (func1==seek) return (-1);
  if (func1==audio_play) return (-1);
  if (func1!=ioctl_o) return (0);
  if (fam1_drive)
    {
      if (func2==EjectDisk) return (-1);
      if (func2==CloseTray) return (-1);
    }
#else
  return (0);
#endif 000
}
/*==========================================================================*/
static int check_allowed3(u_char func1, u_char func2)
{
#if 000
  if (func1==ioctl_i)
    {
      if (func2==tell_address) return (0);
      if (func2==tell_capabiliti) return (0);
      if (func2==tell_CD_changed) return (0);
      if (fam0L_drive) if (func2==tell_SubChanInfo) return (0);
      return (-1);
    }
  if (func1==ioctl_o)
    {
      if (func2==DriveReset) return (0);
      if (fam0L_drive)
	{
	  if (func2==EjectDisk) return (0);
	  if (func2==LockDoor) return (0);
	  if (func2==CloseTray) return (0);
	}
      return (-1);
    }
  if (func1==flush_input) return (-1);
  if (func1==read_long) return (-1);
  if (func1==read_long_prefetch) return (-1);
  if (func1==seek) return (-1);
  if (func1==audio_play) return (-1);
  if (func1==audio_pause) return (-1);
  if (func1==audio_resume) return (-1);
#else
  return (0);
#endif 000
}
/*==========================================================================*/
static int seek_pos_audio_end(void)
{
  int i;

  i=msf2blk(DriveStruct[d].pos_audio_end)-1;
  if (i<0) return (-1);
  i=xx_Seek(i,0);
  return (i);
}
/*==========================================================================*/
static int ReadToC(void)
{
  int i, j;
  DriveStruct[d].diskstate_flags &= ~toc_bit;
  DriveStruct[d].ored_ctl_adr=0;
  for (j=DriveStruct[d].n_first_track;j<=DriveStruct[d].n_last_track;j++)
    {
      i=xx_ReadTocEntry(j);
      if (i<0) return (i);
      DriveStruct[d].TocBuffer[j].nixbyte=DriveStruct[d].TocEnt_nixbyte;
      DriveStruct[d].TocBuffer[j].ctl_adr=DriveStruct[d].TocEnt_ctl_adr;
      DriveStruct[d].TocBuffer[j].number=DriveStruct[d].TocEnt_number;
      DriveStruct[d].TocBuffer[j].format=DriveStruct[d].TocEnt_format;
      DriveStruct[d].TocBuffer[j].address=DriveStruct[d].TocEnt_address;
      DriveStruct[d].ored_ctl_adr |= DriveStruct[d].TocEnt_ctl_adr;
    }
/* fake entry for LeadOut Track */
  DriveStruct[d].TocBuffer[j].nixbyte=0;
  DriveStruct[d].TocBuffer[j].ctl_adr=0;
  DriveStruct[d].TocBuffer[j].number=CDROM_LEADOUT;
  DriveStruct[d].TocBuffer[j].format=0;
  DriveStruct[d].TocBuffer[j].address=DriveStruct[d].size_msf;

  DriveStruct[d].diskstate_flags |= toc_bit;
  return (0);
}
/*==========================================================================*/
static int DiskInfo(void)
{
  int i, j;

      DriveStruct[d].mode=READ_M1;

#undef LOOP_COUNT
#define LOOP_COUNT 20 /* needed for some "old" drives */

  for (j=1;j<LOOP_COUNT;j++)
    {
      i=SetSpeed();
      if (i<0)
	{
	  DPRINTF((DBG_INF,"SBPCD: DiskInfo: SetSpeed returns %d\n", i));
	  continue;
	}
      i=xx_ModeSense();
      if (i<0)
	{
	  DPRINTF((DBG_INF,"SBPCD: DiskInfo: xx_ModeSense returns %d\n", i));
	  continue;
	}
      i=xx_ReadCapacity();
      if (i>=0) break;
      DPRINTF((DBG_INF,"SBPCD: DiskInfo: ReadCapacity #%d returns %d\n", j, i));
      i=DriveReset();
    }
  if (j==LOOP_COUNT) return (-2); /* give up */

  i=xx_ReadTocDescr();
  if (i<0)
    {
      DPRINTF((DBG_INF,"SBPCD: DiskInfo: ReadTocDescr returns %d\n", i));
      return (i);
    }
  i=ReadToC();
  if (i<0)
    {
      DPRINTF((DBG_INF,"SBPCD: DiskInfo: ReadToC returns %d\n", i));
      return (i);
    }
  i=yy_CheckMultiSession();
  if (i<0)
    {
      DPRINTF((DBG_INF,"SBPCD: DiskInfo: yy_CheckMultiSession returns %d\n", i));
      return (i);
    }
  i=xx_ReadTocEntry(DriveStruct[d].n_first_track);
  if (i<0)
    {
      DPRINTF((DBG_INF,"SBPCD: DiskInfo: xx_ReadTocEntry(1) returns %d\n", i));
      return (i);
    }
  i=xx_ReadUPC();
  if (i<0)
    {
      DPRINTF((DBG_INF,"SBPCD: DiskInfo: xx_ReadUPC returns %d\n", i));
      return (i);
    }
#ifdef XA_TEST2
  if ((fam0L_drive) && (DriveStruct[d].xa_byte==0x20)) /* XA disk with old drive */
      {
	xx_ModeSelect(CD_FRAMESIZE_XA);
	xx_ModeSense();
      }
#endif XA_TEST2

  return (0);
}
/*==========================================================================*/
/*
 *  called always if driver gets entered
 *  returns 0 or ERROR2 or ERROR15
 */
static int prepare(u_char func, u_char subfunc)
{
  int i;

  if (fam0L_drive)
    {
      i=inb(CDi_status);
      if (i&s_attention) GetStatus();
    }
  else if (fam1_drive) GetStatus();
  else /* CD200, CD-55A */
    {
    }
  if (DriveStruct[d].CD_changed==0xFF)
    {
#if MANY_SESSION
#else
      DriveStruct[d].diskstate_flags=0;
#endif MANY_SESSION
      DriveStruct[d].audio_state=0;
      if (!st_diskok)
	{
	  i=check_allowed1(func,subfunc);
	  if (i<0) return (-2);
	}
      else 
	{
	  i=check_allowed3(func,subfunc);
	  if (i<0)
	    {
	      DriveStruct[d].CD_changed=1;
	      return (-15);
	    }
	}
    }
  else
    {
      if (!st_diskok)
	{
#if MANY_SESSION
#else
	  DriveStruct[d].diskstate_flags=0;
#endif MANY_SESSION
	  DriveStruct[d].audio_state=0;
	  i=check_allowed1(func,subfunc);
	  if (i<0) return (-2);
	}
      else
	{ 
	  if (st_busy)
	    {
	      if (DriveStruct[d].audio_state!=audio_pausing)
		{
		  i=check_allowed2(func,subfunc);
		  if (i<0) return (-2);
		}
	    }
	  else
	    {
	      if (DriveStruct[d].audio_state==audio_playing) seek_pos_audio_end();
	      DriveStruct[d].audio_state=0;
	    }
	  if (!frame_size_valid)
	    {
	      i=DiskInfo();
	      if (i<0)
		{
#if MANY_SESSION
#else
		  DriveStruct[d].diskstate_flags=0;
#endif MANY_SESSION
		  DriveStruct[d].audio_state=0;
		  i=check_allowed1(func,subfunc);
		  if (i<0) return (-2);
		}
	    }
	}
    }
  return (0);
}
/*==========================================================================*/
static int xx_PlayAudio(int pos_audio_start,int pos_audio_end)
{
  int i, n;

  if (DriveStruct[d].audio_state==audio_playing) return (-EINVAL);
  clr_cmdbuf();
  if (famL_drive)
    {
      drvcmd[0]=CMDL_PLAY;
      i=msf2blk(pos_audio_start);
      n=msf2blk(pos_audio_end)+1-i;
      drvcmd[1]=(i>>16)&0x00FF;
      drvcmd[2]=(i>>8)&0x00FF;
      drvcmd[3]=i&0x00FF;
      drvcmd[4]=(n>>16)&0x00FF;
      drvcmd[5]=(n>>8)&0x00FF;
      drvcmd[6]=n&0x00FF;
      flags_cmd_out = f_putcmd | f_respo2 | f_lopsta | f_getsta |
	f_ResponseStatus | f_obey_p_check | f_wait_if_busy;
    }
  else
    {
      if (fam1_drive)
	{
	  drvcmd[0]=CMD1_PLAY_MSF;
	  flags_cmd_out = f_putcmd | f_respo2 | f_ResponseStatus |
	    f_obey_p_check | f_wait_if_busy;
	}
      else if (fam0_drive)
	{
	  drvcmd[0]=CMD0_PLAY_MSF;
	  flags_cmd_out = f_putcmd | f_respo2 | f_lopsta | f_getsta |
	    f_ResponseStatus | f_obey_p_check | f_wait_if_busy;
	}
      else /* CD200, CD-55A */
	{
	}
      drvcmd[1]=(pos_audio_start>>16)&0x00FF;
      drvcmd[2]=(pos_audio_start>>8)&0x00FF;
      drvcmd[3]=pos_audio_start&0x00FF;
      drvcmd[4]=(pos_audio_end>>16)&0x00FF;
      drvcmd[5]=(pos_audio_end>>8)&0x00FF;
      drvcmd[6]=pos_audio_end&0x00FF;
    }
  response_count=0;
  i=cmd_out(7);
  return (i);
}
/*==========================================================================*/
/*==========================================================================*/
/*==========================================================================*/
/*
 * Check the results of the "get status" command.
 */
static int sbp_status(void)
{
  int st;

  st=ResponseStatus();
  if (st<0)
    {
      DPRINTF((DBG_INF,"SBPCD: sbp_status: timeout.\n"));
      return (0);
    }

  if (!st_spinning) DPRINTF((DBG_SPI,"SBPCD: motor got off - ignoring.\n"));

  if (st_check) 
    {
      DPRINTF((DBG_INF,"SBPCD: st_check detected - retrying.\n"));
      return (0);
    }
  if (!st_door_closed)
    {
      DPRINTF((DBG_INF,"SBPCD: door is open - retrying.\n"));
      return (0);
    }
  if (!st_caddy_in)
    {
      DPRINTF((DBG_INF,"SBPCD: disk removed - retrying.\n"));
      return (0);
    }
  if (!st_diskok) 
    {
      DPRINTF((DBG_INF,"SBPCD: !st_diskok detected - retrying.\n"));
      return (0);
    }
  if (st_busy) 
    {
      DPRINTF((DBG_INF,"SBPCD: st_busy detected - retrying.\n"));
      return (0);
    }
  return (1);
}
/*==========================================================================*/

/*==========================================================================*/
/*==========================================================================*/
/*
 * ioctl support
 */
static int sbpcd_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg)
{
  int i, st;

  DPRINTF((DBG_IO2,"SBPCD: ioctl(%d, 0x%08lX, 0x%08lX)\n",
				MINOR(inode->i_rdev), cmd, arg));
  if (!inode) return (-EINVAL);
  i=MINOR(inode->i_rdev);
  if ((i<0) || (i>=NR_SBPCD) || (DriveStruct[i].drv_id==-1))
    {
      printk("SBPCD: ioctl: bad device: %04X\n", inode->i_rdev);
      return (-ENXIO);             /* no such drive */
    }
  switch_drive(i);

  st=GetStatus();
  if (st<0) return (-EIO);
  
  if (!toc_valid)
    {
      i=DiskInfo();
      if (i<0) return (-EIO);	/* error reading TOC */
    }
  
  DPRINTF((DBG_IO2,"SBPCD: ioctl: device %d, request %04X\n",i,cmd));
  switch (cmd) 		/* Sun-compatible */
    {
    case DDIOCSDBG:		/* DDI Debug */
      if (! suser()) return (-EPERM);
      i = verify_area(VERIFY_READ, (int *) arg, sizeof(int));
      if (i>=0) i=sbpcd_dbg_ioctl(arg,1);
      return (i);

    case CDROMPAUSE:     /* Pause the drive */
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMPAUSE entered.\n"));
      /* pause the drive unit when it is currently in PLAY mode,         */
      /* or reset the starting and ending locations when in PAUSED mode. */
      /* If applicable, at the next stopping point it reaches            */
      /* the drive will discontinue playing.                             */
      switch (DriveStruct[d].audio_state)
	{
	case audio_playing:
	  if (famL_drive) i=xx_ReadSubQ();
	  else i=xx_Pause_Resume(1);
	  if (i<0) return (-EIO);
	  if (famL_drive) i=xx_Pause_Resume(1);
	  else i=xx_ReadSubQ();
	  if (i<0) return (-EIO);
	  DriveStruct[d].pos_audio_start=DriveStruct[d].SubQ_run_tot;
	  DriveStruct[d].audio_state=audio_pausing;
	  return (0);
	case audio_pausing:
	  i=xx_Seek(DriveStruct[d].pos_audio_start,1);
	  if (i<0) return (-EIO);
	  return (0);
	default:
	  return (-EINVAL);
	}
      
    case CDROMRESUME: /* resume paused audio play */
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMRESUME entered.\n"));
      /* resume playing audio tracks when a previous PLAY AUDIO call has  */
      /* been paused with a PAUSE command.                                */
      /* It will resume playing from the location saved in SubQ_run_tot.  */
      if (DriveStruct[d].audio_state!=audio_pausing) return -EINVAL;
      if (famL_drive)
	i=xx_PlayAudio(DriveStruct[d].pos_audio_start,
		       DriveStruct[d].pos_audio_end);
      else i=xx_Pause_Resume(3);
      if (i<0) return (-EIO);
      DriveStruct[d].audio_state=audio_playing;
      return (0);

    case CDROMPLAYMSF:
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMPLAYMSF entered.\n"));
      if (DriveStruct[d].audio_state==audio_playing)
	{
	  i=xx_Pause_Resume(1);
	  if (i<0) return (-EIO);
	  i=xx_ReadSubQ();
	  if (i<0) return (-EIO);
	  DriveStruct[d].pos_audio_start=DriveStruct[d].SubQ_run_tot;
	  i=xx_Seek(DriveStruct[d].pos_audio_start,1);
	}
      st=verify_area(VERIFY_READ, (void *) arg, sizeof(struct cdrom_msf));
      if (st) return (st);
      memcpy_fromfs(&msf, (void *) arg, sizeof(struct cdrom_msf));
      /* values come as msf-bin */
      DriveStruct[d].pos_audio_start = (msf.cdmsf_min0<<16) |
                        (msf.cdmsf_sec0<<8) |
                        msf.cdmsf_frame0;
      DriveStruct[d].pos_audio_end = (msf.cdmsf_min1<<16) |
                      (msf.cdmsf_sec1<<8) |
                      msf.cdmsf_frame1;
      DPRINTF((DBG_IOX,"SBPCD: ioctl: CDROMPLAYMSF %08X %08X\n",
	                       DriveStruct[d].pos_audio_start,DriveStruct[d].pos_audio_end));
      i=xx_PlayAudio(DriveStruct[d].pos_audio_start,DriveStruct[d].pos_audio_end);
      DPRINTF((DBG_IOC,"SBPCD: ioctl: xx_PlayAudio returns %d\n",i));
#if 0
      if (i<0) return (-EIO);
#endif 0
      DriveStruct[d].audio_state=audio_playing;
      return (0);

    case CDROMPLAYTRKIND: /* Play a track.  This currently ignores index. */
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMPLAYTRKIND entered.\n"));
      if (DriveStruct[d].audio_state==audio_playing)
	{
	  DPRINTF((DBG_IOX,"SBPCD: CDROMPLAYTRKIND: already audio_playing.\n"));
	  return (0);
	  return (-EINVAL);
	}
      st=verify_area(VERIFY_READ,(void *) arg,sizeof(struct cdrom_ti));
      if (st<0)
	{
	  DPRINTF((DBG_IOX,"SBPCD: CDROMPLAYTRKIND: verify_area error.\n"));
	  return (st);
	}
      memcpy_fromfs(&ti,(void *) arg,sizeof(struct cdrom_ti));
      DPRINTF((DBG_IOX,"SBPCD: ioctl: trk0: %d, ind0: %d, trk1:%d, ind1:%d\n",
	     ti.cdti_trk0,ti.cdti_ind0,ti.cdti_trk1,ti.cdti_ind1));
      if (ti.cdti_trk0<DriveStruct[d].n_first_track) return (-EINVAL);
      if (ti.cdti_trk0>DriveStruct[d].n_last_track) return (-EINVAL);
      if (ti.cdti_trk1<ti.cdti_trk0) ti.cdti_trk1=ti.cdti_trk0;
      if (ti.cdti_trk1>DriveStruct[d].n_last_track) ti.cdti_trk1=DriveStruct[d].n_last_track;
      DriveStruct[d].pos_audio_start=DriveStruct[d].TocBuffer[ti.cdti_trk0].address;
      DriveStruct[d].pos_audio_end=DriveStruct[d].TocBuffer[ti.cdti_trk1+1].address;
      i=xx_PlayAudio(DriveStruct[d].pos_audio_start,DriveStruct[d].pos_audio_end);
#if 0
      if (i<0) return (-EIO);
#endif 0
      DriveStruct[d].audio_state=audio_playing;
      return (0);
	    
    case CDROMREADTOCHDR:        /* Read the table of contents header */
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMREADTOCHDR entered.\n"));
      tochdr.cdth_trk0=DriveStruct[d].n_first_track;
      tochdr.cdth_trk1=DriveStruct[d].n_last_track;
      st=verify_area(VERIFY_WRITE, (void *) arg, sizeof(struct cdrom_tochdr));
      if (st) return (st);
      memcpy_tofs((void *) arg, &tochdr, sizeof(struct cdrom_tochdr));
      return (0);

    case CDROMREADTOCENTRY:      /* Read an entry in the table of contents */
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMREADTOCENTRY entered.\n"));
      st=verify_area(VERIFY_READ, (void *) arg, sizeof(struct cdrom_tocentry));
      if (st) return (st);
      memcpy_fromfs(&tocentry, (void *) arg, sizeof(struct cdrom_tocentry));
      i=tocentry.cdte_track;
      if (i==CDROM_LEADOUT) i=DriveStruct[d].n_last_track+1;
      else if (i<DriveStruct[d].n_first_track||i>DriveStruct[d].n_last_track) return (-EINVAL);
      tocentry.cdte_adr=DriveStruct[d].TocBuffer[i].ctl_adr&0x0F;
      tocentry.cdte_ctrl=(DriveStruct[d].TocBuffer[i].ctl_adr>>4)&0x0F;
      tocentry.cdte_datamode=DriveStruct[d].TocBuffer[i].format;
      if (tocentry.cdte_format==CDROM_MSF) /* MSF-bin required */
	{ tocentry.cdte_addr.msf.minute=(DriveStruct[d].TocBuffer[i].address>>16)&0x00FF;
	  tocentry.cdte_addr.msf.second=(DriveStruct[d].TocBuffer[i].address>>8)&0x00FF;
	  tocentry.cdte_addr.msf.frame=DriveStruct[d].TocBuffer[i].address&0x00FF;
	}
      else if (tocentry.cdte_format==CDROM_LBA) /* blk required */
	tocentry.cdte_addr.lba=msf2blk(DriveStruct[d].TocBuffer[i].address);
      else return (-EINVAL);
      st=verify_area(VERIFY_WRITE,(void *) arg, sizeof(struct cdrom_tocentry));
      if (st) return (st);
      memcpy_tofs((void *) arg, &tocentry, sizeof(struct cdrom_tocentry));
      return (0);

    case CDROMSTOP:      /* Spin down the drive */
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMSTOP entered.\n"));
      i=xx_Pause_Resume(1);
      DriveStruct[d].audio_state=0;
      return (0);

    case CDROMSTART:  /* Spin up the drive */
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMSTART entered.\n"));
      xx_SpinUp();
      DriveStruct[d].audio_state=0;
      return (0);
      
    case CDROMEJECT:
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMEJECT entered.\n"));
      if (fam0_drive) return (0);
      do i=yy_LockDoor(0);
      while (i!=0);
      DriveStruct[d].open_count=0; /* to get it locked next time again */
      i=yy_SpinDown();
      DPRINTF((DBG_IOX,"SBPCD: ioctl: yy_SpinDown returned %d.\n", i));
      if (i<0) return (-EIO);
      DriveStruct[d].CD_changed=0xFF;
      DriveStruct[d].diskstate_flags=0;
      DriveStruct[d].audio_state=0;
      return (0);
      
    case CDROMEJECT_SW:
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMEJECT_SW entered.\n"));
      if (fam0_drive) return (0);
      DriveStruct[d].f_eject=arg;
      return (0);
      
    case CDROMVOLCTRL:   /* Volume control */
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMVOLCTRL entered.\n"));
      st=verify_area(VERIFY_READ,(void *) arg,sizeof(volctrl));
      if (st) return (st);
      memcpy_fromfs(&volctrl,(char *) arg,sizeof(volctrl));
      DriveStruct[d].vol_chan0=0;
      DriveStruct[d].vol_ctrl0=volctrl.channel0;
      DriveStruct[d].vol_chan1=1;
      DriveStruct[d].vol_ctrl1=volctrl.channel1;
      i=xx_SetVolume();
      return (0);

    case CDROMSUBCHNL:   /* Get subchannel info */
      DPRINTF((DBG_IOS,"SBPCD: ioctl: CDROMSUBCHNL entered.\n"));
      if ((st_spinning)||(!subq_valid)) { i=xx_ReadSubQ();
					  if (i<0) return (-EIO);
					}
      st=verify_area(VERIFY_WRITE, (void *) arg, sizeof(struct cdrom_subchnl));
      if (st)	return (st);
      memcpy_fromfs(&SC, (void *) arg, sizeof(struct cdrom_subchnl));
      switch (DriveStruct[d].audio_state)
	{
	case audio_playing:
	  SC.cdsc_audiostatus=CDROM_AUDIO_PLAY;
	  break;
	case audio_pausing:
	  SC.cdsc_audiostatus=CDROM_AUDIO_PAUSED;
	  break;
	default:
	  SC.cdsc_audiostatus=CDROM_AUDIO_NO_STATUS;
	  break;
	}
      SC.cdsc_adr=DriveStruct[d].SubQ_ctl_adr;
      SC.cdsc_ctrl=DriveStruct[d].SubQ_ctl_adr>>4;
      SC.cdsc_trk=bcd2bin(DriveStruct[d].SubQ_trk);
      SC.cdsc_ind=bcd2bin(DriveStruct[d].SubQ_pnt_idx);
      if (SC.cdsc_format==CDROM_LBA)
	{
	  SC.cdsc_absaddr.lba=msf2blk(DriveStruct[d].SubQ_run_tot);
	  SC.cdsc_reladdr.lba=msf2blk(DriveStruct[d].SubQ_run_trk);
	}
      else /* not only if (SC.cdsc_format==CDROM_MSF) */
	{
	  SC.cdsc_absaddr.msf.minute=(DriveStruct[d].SubQ_run_tot>>16)&0x00FF;
	  SC.cdsc_absaddr.msf.second=(DriveStruct[d].SubQ_run_tot>>8)&0x00FF;
	  SC.cdsc_absaddr.msf.frame=DriveStruct[d].SubQ_run_tot&0x00FF;
	  SC.cdsc_reladdr.msf.minute=(DriveStruct[d].SubQ_run_trk>>16)&0x00FF;
	  SC.cdsc_reladdr.msf.second=(DriveStruct[d].SubQ_run_trk>>8)&0x00FF;
	  SC.cdsc_reladdr.msf.frame=DriveStruct[d].SubQ_run_trk&0x00FF;
	}
      memcpy_tofs((void *) arg, &SC, sizeof(struct cdrom_subchnl));
      DPRINTF((DBG_IOS,"SBPCD: CDROMSUBCHNL: %1X %02X %08X %08X %02X %02X %06X %06X\n",
	       SC.cdsc_format,SC.cdsc_audiostatus,
	       SC.cdsc_adr,SC.cdsc_ctrl,
	       SC.cdsc_trk,SC.cdsc_ind,
	       SC.cdsc_absaddr,SC.cdsc_reladdr));
      return (0);

    case CDROMREADMODE1:
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMREADMODE1 requested.\n"));
      xx_ModeSelect(CD_FRAMESIZE);
      xx_ModeSense();
      DriveStruct[d].mode=READ_M1;
      return (0);

    case CDROMREADMODE2: /* not usable at the moment */
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMREADMODE2 requested.\n"));
      xx_ModeSelect(CD_FRAMESIZE_XA);
      xx_ModeSense();
      DriveStruct[d].mode=READ_M2;
      return (0);

    case CDROMREADAUDIO:
      { /* start of CDROMREADAUDIO */
	int i=0, j=0, frame, block;
	u_int try=0;
	u_long timeout;
	u_char *p;
	u_int data_tries = 0;
	u_int data_waits = 0;
	u_int data_retrying = 0;
	int status_tries;
	int error_flag;

	DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMREADAUDIO requested.\n"));
	if (fam0_drive) return (-EINVAL);
	if (famL_drive) return (-EINVAL);
	if (fam2_drive) return (-EINVAL);
	if (famT_drive) return (-EINVAL);
	if (DriveStruct[d].aud_buf==NULL) return (-EINVAL);
	i=verify_area(VERIFY_READ, (void *) arg, sizeof(struct cdrom_read_audio));
	if (i) return (i);
	memcpy_fromfs(&read_audio, (void *) arg, sizeof(struct cdrom_read_audio));
	if (read_audio.nframes>SBP_BUFFER_AUDIO_FRAMES) return (-EINVAL);
	i=verify_area(VERIFY_WRITE, read_audio.buf,
		      read_audio.nframes*CD_FRAMESIZE_RAW);
	if (i) return (i);

	if (read_audio.addr_format==CDROM_MSF) /* MSF-bin specification of where to start */
	  block=msf2lba(&read_audio.addr.msf.minute);
	else if (read_audio.addr_format==CDROM_LBA) /* lba specification of where to start */
	  block=read_audio.addr.lba;
	else return (-EINVAL);
	i=yy_SetSpeed(speed_150,0,0);
	if (i) DPRINTF((DBG_AUD,"SBPCD: read_audio: SetSpeed error %d\n", i));
	DPRINTF((DBG_AUD,"SBPCD: read_audio: lba: %d, msf: %06X\n",
		 block, blk2msf(block)));
	DPRINTF((DBG_AUD,"SBPCD: read_audio: before xx_ReadStatus.\n"));
	while (busy_data) sbp_sleep(10); /* wait a bit */
	busy_audio=1;
	error_flag=0;
	for (data_tries=5; data_tries>0; data_tries--)
	  {
	    DPRINTF((DBG_AUD,"SBPCD: data_tries=%d ...\n", data_tries));
	    DriveStruct[d].mode=READ_AU;
	    xx_ModeSelect(CD_FRAMESIZE_RAW);
	    xx_ModeSense();
	    for (status_tries=3; status_tries > 0; status_tries--)
	      {
		flags_cmd_out |= f_respo3;
		xx_ReadStatus();
		if (sbp_status() != 0) break;
		sbp_sleep(1);    /* wait a bit, try again */
	      }
	    if (status_tries == 0)
	      {
		DPRINTF((DBG_AUD,"SBPCD: read_audio: sbp_status: failed after 3 tries.\n"));
		continue;
	      }
	    DPRINTF((DBG_AUD,"SBPCD: read_audio: sbp_status: ok.\n"));
	    
	    flags_cmd_out = f_putcmd | f_respo2 | f_ResponseStatus | f_obey_p_check;
	    if (fam0L_drive)
	      {
		flags_cmd_out |= f_lopsta | f_getsta | f_bit1;
		cmd_type=READ_M2;
		drvcmd[0]=CMD0_READ_XA; /* "read XA frames", old drives */
		drvcmd[1]=(block>>16)&0x000000ff;
		drvcmd[2]=(block>>8)&0x000000ff;
		drvcmd[3]=block&0x000000ff;
		drvcmd[4]=0;
		drvcmd[5]=read_audio.nframes; /* # of frames */
		drvcmd[6]=0;
	      }
	    else if (fam1_drive)
	      {
		drvcmd[0]=CMD1_READ; /* "read frames", new drives */
		lba2msf(block,&drvcmd[1]); /* msf-bin format required */
		drvcmd[4]=0;
		drvcmd[5]=0;
		drvcmd[6]=read_audio.nframes; /* # of frames */
	      }
	    else /* CD200, CD-55A */
	      {
	      }
	    DPRINTF((DBG_AUD,"SBPCD: read_audio: before giving \"read\" command.\n"));
	    for (i=0;i<7;i++) OUT(CDo_command,drvcmd[i]);
	    sbp_sleep(0);
	    DPRINTF((DBG_AUD,"SBPCD: read_audio: after giving \"read\" command.\n"));
	    for (frame=1;frame<2 && !error_flag; frame++)
	      {
		try=maxtim_data;
		for (timeout=jiffies+900; ; )
		  {
		    for ( ; try!=0;try--)
		      {
			j=inb(CDi_status);
			if (!(j&s_not_data_ready)) break;
			if (!(j&s_not_result_ready)) break;
			if (fam0L_drive) if (j&s_attention) break;
		      }
		    if (try != 0 || timeout <= jiffies) break;
		    if (data_retrying == 0) data_waits++;
		    data_retrying = 1;
		    sbp_sleep(1);
		    try = 1;
		  }
		if (try==0)
		  {
		    DPRINTF((DBG_INF,"SBPCD: read_audio: sbp_data: CDi_status timeout.\n"));
		    error_flag++;
		    break;
		  }
		DPRINTF((DBG_AUD,"SBPCD: read_audio: sbp_data: CDi_status ok.\n"));
		if (j&s_not_data_ready)
		  {
		    printk("SBPCD: read_audio: sbp_data: DATA_READY timeout.\n");
		    error_flag++;
		    break;
		  }
		DPRINTF((DBG_AUD,"SBPCD: read_audio: before reading data.\n"));
		error_flag=0;
		p = DriveStruct[d].aud_buf;
		if (sbpro_type==1) OUT(CDo_sel_i_d,1);
#if 0
		cli();
#endif
		READ_DATA(CDi_data, p, read_audio.nframes*CD_FRAMESIZE_RAW);
#if 0
		sti();
#endif
		if (sbpro_type==1) OUT(CDo_sel_i_d,0);
		data_retrying = 0;
	      }
	    DPRINTF((DBG_AUD,"SBPCD: read_audio: after reading data.\n"));
	    if (error_flag)    /* must have been spurious D_RDY or (ATTN&&!D_RDY) */
	      {
		DPRINTF((DBG_AUD,"SBPCD: read_audio: read aborted by drive\n"));
#if 0000
		i=DriveReset();                /* ugly fix to prevent a hang */
#else
		i=xx_ReadError();
#endif 0000
		continue;
	      }
	    if (fam0L_drive)
	      {
		i=maxtim_data;
		for (timeout=jiffies+900; timeout > jiffies; timeout--)
		  {
		    for ( ;i!=0;i--)
		      {
			j=inb(CDi_status);
			if (!(j&s_not_data_ready)) break;
			if (!(j&s_not_result_ready)) break;
			if (j&s_attention) break;
		      }
		    if (i != 0 || timeout <= jiffies) break;
		    sbp_sleep(0);
		    i = 1;
		  }
		if (i==0) { DPRINTF((DBG_AUD,"SBPCD: read_audio: STATUS TIMEOUT AFTER READ")); }
		if (!(j&s_attention))
		  {
		    DPRINTF((DBG_AUD,"SBPCD: read_audio: sbp_data: timeout waiting DRV_ATTN - retrying\n"));
		    i=DriveReset();  /* ugly fix to prevent a hang */
		  continue;
		  }
	      }
	    do
	      {
		if (fam0L_drive) xx_ReadStatus();
		i=ResponseStatus();  /* builds status_byte, returns orig. status (old) or faked p_success_old (new) */
		if (i<0) { DPRINTF((DBG_AUD,
				    "SBPCD: read_audio: xx_ReadStatus error after read: %02X\n",
				    DriveStruct[d].status_byte));
			   continue; /* FIXME */
			 }
	      }
	    while ((fam0L_drive)&&(!st_check)&&(!(i&p_success_old)));
	    if (st_check)
	      {
		i=xx_ReadError();
		DPRINTF((DBG_AUD,"SBPCD: read_audio: xx_ReadError was necessary after read: %02X\n",i));
		continue;
	      }
	    memcpy_tofs((u_char *) read_audio.buf,
			(u_char *) DriveStruct[d].aud_buf,
			read_audio.nframes*CD_FRAMESIZE_RAW);
	    DPRINTF((DBG_AUD,"SBPCD: read_audio: memcpy_tofs done.\n"));
	    break;
	  }
	xx_ModeSelect(CD_FRAMESIZE);
	xx_ModeSense();
	DriveStruct[d].mode=READ_M1;
	busy_audio=0;
	if (data_tries == 0)
	  {
	    DPRINTF((DBG_AUD,"SBPCD: read_audio: failed after 5 tries.\n"));
	    return (-8);
	  }
	DPRINTF((DBG_AUD,"SBPCD: read_audio: successful return.\n"));
	return (0);
      } /* end of CDROMREADAUDIO */

    case CDROMMULTISESSION: /* tell start-of-last-session */
      DPRINTF((DBG_IOC,"SBPCD: ioctl: CDROMMULTISESSION entered.\n"));
      st=verify_area(VERIFY_READ, (void *) arg, sizeof(struct cdrom_multisession));
      if (st) return (st);
      memcpy_fromfs(&ms_info, (void *) arg, sizeof(struct cdrom_multisession));
      if (ms_info.addr_format==CDROM_MSF) /* MSF-bin requested */
	lba2msf(DriveStruct[d].lba_multi,&ms_info.addr.msf.minute);
      else if (ms_info.addr_format==CDROM_LBA) /* lba requested */
	ms_info.addr.lba=DriveStruct[d].lba_multi;
      else return (-EINVAL);
      if (DriveStruct[d].f_multisession) ms_info.xa_flag=1; /* valid redirection address */
      else ms_info.xa_flag=0; /* invalid redirection address */
      st=verify_area(VERIFY_WRITE,(void *) arg, sizeof(struct cdrom_multisession));
      if (st) return (st);
      memcpy_tofs((void *) arg, &ms_info, sizeof(struct cdrom_multisession));
      DPRINTF((DBG_MUL,"SBPCD: ioctl: CDROMMULTISESSION done (%d, %08X).\n",
	       ms_info.xa_flag, ms_info.addr.lba));
      return (0);

    case BLKRASET:
      if(!suser())  return -EACCES;
      if(!inode->i_rdev) return -EINVAL;
      if(arg > 0xff) return -EINVAL;
      read_ahead[MAJOR(inode->i_rdev)] = arg;
      return (0);

    default:
      DPRINTF((DBG_IOC,"SBPCD: ioctl: unknown function request %04X\n", cmd));
      return (-EINVAL);
    } /* end switch(cmd) */
}
/*==========================================================================*/
/*
 *  Take care of the different block sizes between cdrom and Linux.
 */
static void sbp_transfer(void)
{
  long offs;
  
  while ( (CURRENT->nr_sectors > 0) &&
	  (CURRENT->sector/4 >= DriveStruct[d].sbp_first_frame) &&
	  (CURRENT->sector/4 <= DriveStruct[d].sbp_last_frame) )
    {
      offs = (CURRENT->sector - DriveStruct[d].sbp_first_frame * 4) * 512;
      memcpy(CURRENT->buffer, DriveStruct[d].sbp_buf + offs, 512);
      CURRENT->nr_sectors--;
      CURRENT->sector++;
      CURRENT->buffer += 512;
    }
}
/*==========================================================================*/
/*
 *  I/O request routine, called from Linux kernel.
 */
static void DO_SBPCD_REQUEST(void)
{
  u_int block;
  u_int nsect;
  int i, status_tries, data_tries;
  
request_loop:

  sti();

  if ((CURRENT==NULL)||(CURRENT->dev<0)) goto done;
  if (CURRENT -> sector == -1) goto done;

  i = MINOR(CURRENT->dev);
  if ( (i<0) || (i>=NR_SBPCD) || (DriveStruct[i].drv_id==-1))
    {
      printk("SBPCD: do_request: bad device: %04X\n", CURRENT->dev);
      goto done;
    }
  switch_drive(i);

  INIT_REQUEST;
  block = CURRENT->sector; /* always numbered as 512-byte-pieces */
  nsect = CURRENT->nr_sectors; /* always counted as 512-byte-pieces */
  if (CURRENT->cmd != READ)
    {
      printk("SBPCD: bad cmd %d\n", CURRENT->cmd);
      end_request(0);
      goto request_loop;
    }

  DPRINTF((DBG_BSZ,"SBPCD: read sector %d (%d sectors)\n", block, nsect));
#if 0
  DPRINTF((DBG_MUL,"SBPCD: read LBA %d\n", block/4));
#endif

  sbp_transfer();
  /* if we satisfied the request from the buffer, we're done. */
  if (CURRENT->nr_sectors == 0)
    {
      end_request(1);
      goto request_loop;
    }

  i=prepare(0,0); /* at moment not really a hassle check, but ... */
  if (i!=0)
    DPRINTF((DBG_INF,"SBPCD: \"prepare\" tells error %d -- ignored\n", i));

  while (busy_audio) sbp_sleep(100); /* wait a bit */
  busy_data=1;

  if (!st_spinning) xx_SpinUp();

#ifdef XA_TEST1
  if ((fam0L_drive) && (DriveStruct[d].xa_byte==0x20)) /* XA disk with old drive */
      {
	xx_ModeSelect(CD_FRAMESIZE_XA);
	xx_ModeSense();
      }
#endif XA_TEST1

  for (data_tries=n_retries; data_tries > 0; data_tries--)
    {
      for (status_tries=3; status_tries > 0; status_tries--)
	{
	  flags_cmd_out |= f_respo3;
	  xx_ReadStatus();
	  if (sbp_status() != 0) break;
	  if (st_check) xx_ReadError();
	  sbp_sleep(1);    /* wait a bit, try again */
	}
      if (status_tries == 0)
	{
	  DPRINTF((DBG_INF,"SBPCD: sbp_status: failed after 3 tries\n"));
	  break;
	}

      sbp_read_cmd();
      sbp_sleep(0);
      if (sbp_data() != 0)
	{
	  end_request(1);
	  goto request_loop;
	}
    }
  
  end_request(0);
  sbp_sleep(10);    /* wait a bit, try again */
  goto request_loop;

done:
  busy_data=0;
  return;
}
/*==========================================================================*/
/*
 *  build and send the READ command.
 *  Maybe it would be better to "set mode1" before ...
 */
static void sbp_read_cmd(void)
{
  int i;
  int block;

  DriveStruct[d].sbp_first_frame=DriveStruct[d].sbp_last_frame=-1;      /* purge buffer */
  block=CURRENT->sector/4;

#if MULTISESSION_BY_DRIVER
  if (!fam0_drive)
    {
#if MANY_SESSION
      DPRINTF((DBG_MUL,"SBPCD: read MSF %08X\n", blk2msf(block)));
      if (DriveStruct[d].f_multisession)
	{
	  DPRINTF((DBG_MUL,"SBPCD: ManySession: use %08X for %08X (msf)\n",
		         blk2msf(DriveStruct[d].lba_multi+block),
                         blk2msf(block)));
	  block=DriveStruct[d].lba_multi+block;
	}
#else
      if ((block<=DriveStruct[d].last_redirect)
	  && (DriveStruct[d].f_multisession))
	  {
	    DPRINTF((DBG_MUL,"SBPCD: MultiSession: use %08X for %08X (msf)\n",
		     blk2msf(DriveStruct[d].lba_multi+block),
		     blk2msf(block)));
	    block=DriveStruct[d].lba_multi+block;
	  }
#endif MANY_SESSION
    }
#endif MULTISESSION_BY_DRIVER

  if (block+SBP_BUFFER_FRAMES <= DriveStruct[d].CDsize_frm)
    DriveStruct[d].sbp_read_frames = SBP_BUFFER_FRAMES;
  else
    {
      DriveStruct[d].sbp_read_frames=DriveStruct[d].CDsize_frm-block;
                                      /* avoid reading past end of data */
      if (DriveStruct[d].sbp_read_frames < 1)
	{
	  DPRINTF((DBG_INF,"SBPCD: requested frame %d, CD size %d ???\n",
		        block, DriveStruct[d].CDsize_frm));
	  DriveStruct[d].sbp_read_frames=1;
	}
    }
  DriveStruct[d].sbp_current = 0;

  flags_cmd_out = f_putcmd |
                  f_respo2 |
                  f_ResponseStatus |
                  f_obey_p_check;

  if (fam0L_drive)
    {
      flags_cmd_out |= f_lopsta | f_getsta | f_bit1;
      if (DriveStruct[d].xa_byte==0x20)
	{
	  cmd_type=READ_M2;
	  drvcmd[0]=CMD0_READ_XA; /* "read XA frames", old drives */
	  drvcmd[1]=(block>>16)&0x000000ff;
	  drvcmd[2]=(block>>8)&0x000000ff;
	  drvcmd[3]=block&0x000000ff;
	  drvcmd[4]=0;
	  drvcmd[5]=DriveStruct[d].sbp_read_frames;
	  drvcmd[6]=0;
	}
      else
	{
	  drvcmd[0]=CMD0_READ; /* "read frames", old drives */
	  
	  if (DriveStruct[d].drv_type>=drv_201)
	    {
	      lba2msf(block,&drvcmd[1]); /* msf-bcd format required */
	      bin2bcdx(&drvcmd[1]);
	      bin2bcdx(&drvcmd[2]);
	      bin2bcdx(&drvcmd[3]);
	    }
	  else
	    {
	      drvcmd[1]=(block>>16)&0x000000ff;
	      drvcmd[2]=(block>>8)&0x000000ff;
	      drvcmd[3]=block&0x000000ff;
	    }
	  drvcmd[4]=0;
	  drvcmd[5]=DriveStruct[d].sbp_read_frames;
	  drvcmd[6]=(DriveStruct[d].drv_type<drv_201)?0:2; /* flag "lba or msf-bcd format" */
	}
    }
  else if (fam1_drive)
    {
      drvcmd[0]=CMD1_READ;
      lba2msf(block,&drvcmd[1]); /* msf-bin format required */
      drvcmd[4]=0;
      drvcmd[5]=0;
      drvcmd[6]=DriveStruct[d].sbp_read_frames;
    }
  else if (fam2_drive)
    {
      drvcmd[0]=CMD2_READ;
      lba2msf(block,&drvcmd[1]); /* msf-bin format required */
      drvcmd[4]=0;
      drvcmd[5]=DriveStruct[d].sbp_read_frames;
      drvcmd[6]=0x02;
    }
  else /* CD-55A */
    {
    }
  SBPCD_CLI;
  for (i=0;i<7;i++) OUT(CDo_command,drvcmd[i]);
  SBPCD_STI;

  return;
}
/*==========================================================================*/
/*
 *  Check the completion of the read-data command.  On success, read
 *  the SBP_BUFFER_FRAMES * 2048 bytes of data from the disk into buffer.
 */
static int sbp_data(void)
{
  int i=0, j=0, frame;
  u_int try=0;
  u_long timeout;
  u_char *p;
  u_int data_tries = 0;
  u_int data_waits = 0;
  u_int data_retrying = 0;
  int error_flag;
  int xa_count;
  error_flag=0;

  for (frame=DriveStruct[d].sbp_current;frame<DriveStruct[d].sbp_read_frames&&!error_flag; frame++)
    {
      SBPCD_CLI;
      try=maxtim_data;
#if LONG_TIMING
      for (timeout=jiffies+900; ; )
#else
      for (timeout=jiffies+ ( (famL_drive)? 300:100 ); ; )
#endif
	{
	  for ( ; try!=0;try--)
	    {
	      j=inb(CDi_status);
	      if (!(j&s_not_data_ready)) break;
	      if (!(j&s_not_result_ready)) break;
	      if (fam0L_drive) if (j&s_attention) break;
	    }
	  if (try != 0 || timeout <= jiffies) break;
	  if (data_retrying == 0) data_waits++;
	  data_retrying = 1;
	  sbp_sleep(1);
	  try = 1;
	}
      if (try==0)
	{
	  DPRINTF((DBG_INF,"SBPCD: sbp_data: CDi_status timeout.\n"));
	  error_flag++;
	  break;
	}

      if (j&s_not_data_ready)
	{
	  if ((DriveStruct[d].ored_ctl_adr&0x40)==0)
	    printk("SBPCD: CD contains no data tracks.\n");
	  else printk("SBPCD: sbp_data: DATA_READY timeout.\n");
	  error_flag++;
	  break;
	}

      SBPCD_STI;
      error_flag=0;
      p = DriveStruct[d].sbp_buf + frame *  CD_FRAMESIZE;

      if (sbpro_type==1) OUT(CDo_sel_i_d,1);
      if (cmd_type==READ_M2) READ_DATA(CDi_data, xa_head_buf, CD_XA_HEAD);
      READ_DATA(CDi_data, p, CD_FRAMESIZE);
      if (cmd_type==READ_M2) READ_DATA(CDi_data, xa_tail_buf, CD_XA_TAIL);
      if (sbpro_type==1) OUT(CDo_sel_i_d,0);
      DriveStruct[d].sbp_current++;
      if (cmd_type==READ_M2)
	{
	  DPRINTF((DBG_XA,"SBPCD: xa_head:"));
	  for (xa_count=0;xa_count<CD_XA_HEAD;xa_count++)
	    DPRINTF((DBG_XA," %02X", xa_head_buf[xa_count]));
	  DPRINTF((DBG_XA,"\n"));
	}
      data_tries++;
      data_retrying = 0;
      if (data_tries >= 1000)
	{
	  DPRINTF((DBG_INF,"SBPCD: info: %d waits in %d frames.\n",
		        data_waits, data_tries));
	  data_waits = data_tries = 0;
	}
    }
  SBPCD_STI;
  
  if (error_flag)    /* must have been spurious D_RDY or (ATTN&&!D_RDY) */
    {
      DPRINTF((DBG_INF,"SBPCD: read aborted by drive\n"));
#if 0
      i=DriveReset();                /* ugly fix to prevent a hang */
#else
      i=xx_ReadError();
#endif
      return (0);
    }

  if (fam0L_drive)
    {
      SBPCD_CLI;
      i=maxtim_data;
      for (timeout=jiffies+100; timeout > jiffies; timeout--)
	{
	  for ( ;i!=0;i--)
	    {
	      j=inb(CDi_status);
	      if (!(j&s_not_data_ready)) break;
	      if (!(j&s_not_result_ready)) break;
	      if (j&s_attention) break;
	    }
	  if (i != 0 || timeout <= jiffies) break;
	  sbp_sleep(0);
	  i = 1;
	}
      if (i==0) { DPRINTF((DBG_INF,"SBPCD: STATUS TIMEOUT AFTER READ")); }
      if (!(j&s_attention))
	{
	  DPRINTF((DBG_INF,"SBPCD: sbp_data: timeout waiting DRV_ATTN - retrying\n"));
	  i=DriveReset();  /* ugly fix to prevent a hang */
	  SBPCD_STI;
	  return (0);
	}
      SBPCD_STI;
    }

  do
    {
      if (fam0L_drive) xx_ReadStatus();
      i=ResponseStatus();  /* builds status_byte, returns orig. status (old) or faked p_success_old (new) */
      if (i<0) { DPRINTF((DBG_INF,"SBPCD: xx_ReadStatus error after read: %02X\n",
			       DriveStruct[d].status_byte));
		 return (0);
	       }
    }
  while ((fam0L_drive)&&(!st_check)&&(!(i&p_success_old)));
  if (st_check)
    {
      i=xx_ReadError();
      DPRINTF((DBG_INF,"SBPCD: xx_ReadError was necessary after read: %02X\n",i));
      return (0);
    }

  DriveStruct[d].sbp_first_frame = CURRENT -> sector / 4;
  DriveStruct[d].sbp_last_frame = DriveStruct[d].sbp_first_frame + DriveStruct[d].sbp_read_frames - 1;
  sbp_transfer();
  return (1);
}
/*==========================================================================*/
/*==========================================================================*/
/*
 *  Open the device special file.  Check that a disk is in. Read TOC.
 */
static int sbpcd_open(struct inode *ip, struct file *fp)
{
  int i;

  i = MINOR(ip->i_rdev);
  if ((i<0) || (i>=NR_SBPCD) || (DriveStruct[i].drv_id==-1))
    {
      printk("SBPCD: open: bad device: %04X\n", ip->i_rdev);
      return (-ENXIO);             /* no such drive */
    }
  if (fp->f_mode & 2)
	  return -EROFS;
  
  switch_drive(i);

  flags_cmd_out |= f_respo2;
  xx_ReadStatus();                         /* command: give 1-byte status */
  i=ResponseStatus();
  if (!st_door_closed)
    {
      yy_CloseTray();
      flags_cmd_out |= f_respo2;
      xx_ReadStatus();
      i=ResponseStatus();
    }
  if (!st_spinning)
    {
      xx_SpinUp();
      flags_cmd_out |= f_respo2;
      xx_ReadStatus();
      i=ResponseStatus();
    }
  if (i<0)
    {
      DPRINTF((DBG_INF,"SBPCD: sbpcd_open: xx_ReadStatus timed out\n"));
      return (-EIO);                  /* drive doesn't respond */
    }
  DPRINTF((DBG_STA,"SBPCD: sbpcd_open: status %02X\n", DriveStruct[d].status_byte));
  if (!st_door_closed||!st_caddy_in)
    {
      printk("SBPCD: sbpcd_open: no disk in drive\n");
#if JUKEBOX
      do
	i=yy_LockDoor(0);
      while (i!=0);
      if (!fam0_drive) yy_SpinDown(); /* eject tray */
#endif
      return (-ENXIO);
    }
/*
 * try to keep an "open" counter here and lock the door if 0->1.
 */
  DPRINTF((DBG_LCK,"SBPCD: open_count: %d -> %d\n",
	   DriveStruct[d].open_count,DriveStruct[d].open_count+1));
  if (++DriveStruct[d].open_count==1)
    {
      do
	i=yy_LockDoor(1);
      while (i!=0);
    }
  if (!st_spinning) xx_SpinUp();

  i=DiskInfo();
  if ((DriveStruct[d].ored_ctl_adr&0x40)==0)
    DPRINTF((DBG_INF,"SBPCD: CD contains no data tracks.\n"));
  return (0);
}
/*==========================================================================*/
/*
 *  On close, we flush all sbp blocks from the buffer cache.
 */
static void sbpcd_release(struct inode * ip, struct file * file)
{
  int i;

  i = MINOR(ip->i_rdev);
  if ((i<0) || (i>=NR_SBPCD) || (DriveStruct[i].drv_id==-1))
    {
      printk("SBPCD: release: bad device: %04X\n", ip->i_rdev);
      return;
    }
  switch_drive(i);

  DriveStruct[d].sbp_first_frame=DriveStruct[d].sbp_last_frame=-1;
  sync_dev(ip->i_rdev);                   /* nonsense if read only device? */
  invalidate_buffers(ip->i_rdev);

/*
 * try to keep an "open" counter here and unlock the door if 1->0.
 */
  DPRINTF((DBG_LCK,"SBPCD: open_count: %d -> %d\n",
	   DriveStruct[d].open_count,DriveStruct[d].open_count-1));
  if (DriveStruct[d].open_count!=0) /* CDROMEJECT may have been done */
    {
      if (--DriveStruct[d].open_count==0) 
	{
	  do
	    i=yy_LockDoor(0);
	  while (i!=0);
	  if (DriveStruct[d].f_eject) yy_SpinDown();
	  DriveStruct[d].diskstate_flags &= ~cd_size_bit;
	}
    }
}
/*==========================================================================*/
/*
 *
 */
static struct file_operations sbpcd_fops =
{
  NULL,                   /* lseek - default */
  block_read,             /* read - general block-dev read */
  block_write,            /* write - general block-dev write */
  NULL,                   /* readdir - bad */
  NULL,                   /* select */
  sbpcd_ioctl,            /* ioctl */
  NULL,                   /* mmap */
  sbpcd_open,             /* open */
  sbpcd_release,          /* release */
  NULL,                   /* fsync */
  NULL,                   /* fasync */
  sbpcd_chk_disk_change,  /* media_change */
  NULL                    /* revalidate */
};
/*==========================================================================*/
/*
 * accept "kernel command line" parameters 
 * (suggested by Peter MacDonald with SLS 1.03)
 *
 * This is only implemented for the first controller. Should be enough to
 * allow installing with a "strange" distribution kernel.
 *
 * use: tell LILO:
 *                 sbpcd=0x230,SoundBlaster
 *             or
 *                 sbpcd=0x300,LaserMate
 *             or
 *                 sbpcd=0x330,SPEA
 *
 * (upper/lower case sensitive here!!!).
 *
 * the address value has to be the TRUE CDROM PORT ADDRESS -
 * not the soundcard base address.
 *
 */
#if (SBPCD_ISSUE-1)
static
#endif
void sbpcd_setup(char *s, int *p)
{
  setup_done++;
  DPRINTF((DBG_INI,"SBPCD: sbpcd_setup called with %04X,%s\n",p[1], s));
  sbpro_type=0;
  if (!strcmp(s,str_sb)) sbpro_type=1;
  else if (!strcmp(s,str_sp)) sbpro_type=2;
  if (p[0]>0) sbpcd_ioaddr=p[1];

  CDo_command=sbpcd_ioaddr;
  CDi_info=sbpcd_ioaddr;
  CDi_status=sbpcd_ioaddr+1;
  CDo_sel_i_d=sbpcd_ioaddr+1;
  CDo_reset=sbpcd_ioaddr+2;
  CDo_enable=sbpcd_ioaddr+3; 
  if (sbpro_type==1)
    {
      MIXER_addr=sbpcd_ioaddr-0x10+0x04;
      MIXER_data=sbpcd_ioaddr-0x10+0x05;
      CDi_data=sbpcd_ioaddr;
    }
  else CDi_data=sbpcd_ioaddr+2;
}
/*==========================================================================*/
/*
 * Sequoia S-1000 CD-ROM Interface Configuration
 * as used within SPEA Media FX card
 * The SPEA soundcard has to get jumpered for 
 *     -> interface type "Matsushita/Panasonic" (not Sony or Mitsumi)
 *     -> I/O base address (0x320, 0x330, 0x340, 0x350)
 */
static int config_spea(void)
{
  int n_ports=0x10; /* 2:0x00, 8:0x10, 16:0x20, 32:0x30 */
/* What is n_ports? Number of addresses or base address offset? */
  int irq_number=0; /* 2:0x01, 7:0x03, 12:0x05, 15:0x07, OFF:0x00 */
  int dma_channel=0; /* 0:0x08, 1:0x18, 3:0x38, 5:0x58, 6:0x68, 7:0x78, OFF: 0x00 */
  int dack_polarity=0; /* L:0x00, H:0x80 */
  int drq_polarity=0x40; /* L:0x00, H:0x40 */

  int i;

#define SPEA_REG_1 sbpcd_ioaddr+4
#define SPEA_REG_2 sbpcd_ioaddr+5

  OUT(SPEA_REG_1,0xFF);
  i=inb(SPEA_REG_1);
  if (i!=0x0F)
    {
      DPRINTF((DBG_SEQ,"SBPCD: no SPEA interface at %04X present.\n",
	       sbpcd_ioaddr));
      return (-1); /* no interface found */
    }
  OUT(SPEA_REG_1,0x04);
  OUT(SPEA_REG_2,0xC0);

  OUT(SPEA_REG_1,0x05);
  OUT(SPEA_REG_2,0x10|drq_polarity|dack_polarity);

#if 1
#define SPEA_PATTERN 0x80
#else
#define SPEA_PATTERN 0x00
#endif
  OUT(SPEA_REG_1,0x06);
  OUT(SPEA_REG_2,dma_channel|irq_number|SPEA_PATTERN);
  OUT(SPEA_REG_2,dma_channel|irq_number|SPEA_PATTERN);

  OUT(SPEA_REG_1,0x09);
  i=(inb(SPEA_REG_2)&0xCF)|n_ports;
  OUT(SPEA_REG_2,i);

  sbpro_type = 0; /* acts like a LaserMate interface now */
  DPRINTF((DBG_SEQ,"SBPCD: found SPEA interface at %04X.\n",
	   sbpcd_ioaddr));
  return (0);
}
/*==========================================================================*/
/*
 *  Test for presence of drive and initialize it.  Called at boot time.
 */
unsigned long SBPCD_INIT(u_long mem_start, u_long mem_end)
{
  int i=0, j=0;
  int addr[2]={1, CDROM_PORT};
  int port_index;
   
  sti(); /* necessary, has consequences for other drivers' init routines */

  DPRINTF((DBG_INF,"SBPCD version %s\n", VERSION));

  if (!setup_done)
    {
      DPRINTF((DBG_INF,"SBPCD: Looking for Matsushita, Panasonic, CreativeLabs, IBM, Longshine, TEAC CD-ROM drives\n"));
      DPRINTF((DBG_WRN,"SBPCD: \n"));
      DPRINTF((DBG_WRN,"SBPCD: = = = = = = = = = = W A R N I N G = = = = = = = = = =\n"));
      DPRINTF((DBG_WRN,"SBPCD: Auto-Probing can cause a hang (f.e. touching an ethernet card).\n"));
      DPRINTF((DBG_WRN,"SBPCD: If that happens, you have to reboot and use the\n"));
      DPRINTF((DBG_WRN,"SBPCD: LILO (kernel) command line feature like:\n"));
      DPRINTF((DBG_WRN,"SBPCD: \n"));
      DPRINTF((DBG_WRN,"SBPCD:    LILO boot: linux sbpcd=0x230,SoundBlaster\n"));
      DPRINTF((DBG_WRN,"SBPCD: or like:\n"));
      DPRINTF((DBG_WRN,"SBPCD:    LILO boot: linux sbpcd=0x300,LaserMate\n"));
      DPRINTF((DBG_WRN,"SBPCD: or like:\n"));
      DPRINTF((DBG_WRN,"SBPCD:    LILO boot: linux sbpcd=0x330,SPEA\n"));
      DPRINTF((DBG_WRN,"SBPCD: \n"));
      DPRINTF((DBG_WRN,"SBPCD: with your REAL address.\n"));
      DPRINTF((DBG_WRN,"SBPCD: = = = = = = = = = = END of WARNING = = = = = = = = = =\n"));
      DPRINTF((DBG_WRN,"SBPCD: \n"));
    }
  sbpcd_probe[0]=sbpcd_ioaddr; /* possibly changed by kernel command line */
  sbpcd_probe[1]=sbpro_type; /* possibly changed by kernel command line */

  for (port_index=0;port_index<NUM_PROBE;port_index+=2)
    {
      addr[1]=sbpcd_probe[port_index];
      if (check_region(addr[1],4))
	{
	  DPRINTF((DBG_INI,"SBPCD: check_region: %03X is not free.\n",addr[1]));
	  continue;
	}
      if (sbpcd_probe[port_index+1]==0) type=str_lm;
      else if (sbpcd_probe[port_index+1]==1) type=str_sb;
      else type=str_sp;
      sbpcd_setup(type, addr);
      DPRINTF((DBG_INF,"SBPCD: Trying to detect a %s CD-ROM drive at 0x%X.\n", type, CDo_command));
      DPRINTF((DBG_INF,"SBPCD: - "));
      if (sbpcd_probe[port_index+1]==2)
	{
	  i=config_spea();
	  if (i<0)
	    {
	      DPRINTF((DBG_INF,"\n"));
	      continue;
	    }
	}
#if TEAC
      i=find_teac_drives();
      if (i>0)
	{
	  DPRINTF((DBG_INF,"SBPCD: found %d TEAC drives. A wonder.\n",i));
	  DPRINTF((DBG_INF,"SBPCD: - "));
	}
#endif TEAC
      i=check_drives();
      DPRINTF((DBG_INI,"SBPCD: check_drives done.\n"));
      if (i>=0) break; /* drive found */
      DPRINTF((DBG_INF,"\n"));
    } /* end of cycling through the set of possible I/O port addresses */

  if (ndrives==0)
    {
      printk("SBPCD: No drive found.\n");
#if PRINTK_BUG
      sti(); /* to avoid possible "printk" bug */
#endif
      goto init_done;
    }

  if (port_index>0)
    {
      printk("SBPCD: You should configure sbpcd.h for your hardware.\n");
#if PRINTK_BUG
      sti(); /* to avoid possible "printk" bug */
#endif
    }

  printk("SBPCD: %d %s CD-ROM drive(s) at 0x%04X.\n",
	   ndrives, type, CDo_command);
#if PRINTK_BUG
  sti(); /* to avoid possible "printk" bug */
#endif
  check_datarate();
  DPRINTF((DBG_INI,"SBPCD: check_datarate done.\n"));

   if (!famL_drive)
   {
     OUT(CDo_reset,0);
     sbp_sleep(100);
   }

  for (j=0;j<NR_SBPCD;j++)
    {
      if (DriveStruct[j].drv_id==-1) continue;
      switch_drive(j);
      if (!famL_drive) xy_DriveReset();
      if (!st_spinning) xx_SpinUp();
      DriveStruct[d].sbp_first_frame = -1;  /* First frame in buffer */
      DriveStruct[d].sbp_last_frame = -1;   /* Last frame in buffer  */
      DriveStruct[d].sbp_read_frames = 0;   /* Number of frames being read to buffer */
      DriveStruct[d].sbp_current = 0;       /* Frame being currently read */
      DriveStruct[d].CD_changed=1;
      DriveStruct[d].frame_size=CD_FRAMESIZE;
#if EJECT
      if (!fam0_drive) DriveStruct[d].f_eject=1;
      else DriveStruct[d].f_eject=0;
#else
      DriveStruct[d].f_eject=0;
#endif

      xx_ReadStatus();
      i=ResponseStatus();  /* returns orig. status or p_busy_new */
      if (i<0)
	DPRINTF((DBG_INF,"SBPCD: init: ResponseStatus returns %02X\n",i));
      else
	{
	  if (st_check)
	    {
	      i=xx_ReadError();
	      DPRINTF((DBG_INI,"SBPCD: init: xx_ReadError returns %d\n",i));
	    }
	}
      DPRINTF((DBG_INI,"SBPCD: init: first GetStatus: %d\n",i));
      DPRINTF((DBG_LCS,"SBPCD: init: first GetStatus: error_byte=%d\n",
	       DriveStruct[d].error_byte));
      if (DriveStruct[d].error_byte==aud_12)
	{
	  do { i=GetStatus();
	       DPRINTF((DBG_INI,"SBPCD: init: second GetStatus: %02X\n",i));
	       DPRINTF((DBG_LCS,
			"SBPCD: init: second GetStatus: error_byte=%d\n",
			DriveStruct[d].error_byte));
	       if (i<0) break;
	       if (!st_caddy_in) break;
	     }
	  while (!st_diskok);
	}
      i=SetSpeed();
      if (i>=0) DriveStruct[d].CD_changed=1;
    }

/*
 * Turn on the CD audio channels.
 * For "compatible" soundcards (with "SBPRO 0" or "SBPRO 2"), the addresses
 * are obtained from SOUND_BASE (see sbpcd.h).
 */
  if ((sbpro_type==1) || (SOUND_BASE))
    {
      if (sbpro_type!=1)
	{
	  MIXER_addr=SOUND_BASE+0x04; /* sound card's address register */
	  MIXER_data=SOUND_BASE+0x05; /* sound card's data register */
	}
      OUT(MIXER_addr,MIXER_CD_Volume); /* select SB Pro mixer register */
      OUT(MIXER_data,0xCC); /* one nibble per channel, max. value: 0xFF */
    }

  if (register_blkdev(MAJOR_NR, major_name, &sbpcd_fops) != 0)
    {
      printk("SBPCD: Can't get MAJOR %d for Matsushita CDROM\n", MAJOR_NR);
#if PRINTK_BUG
      sti(); /* to avoid possible "printk" bug */
#endif
      goto init_done;
    }
  blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
  read_ahead[MAJOR_NR] = SBP_BUFFER_FRAMES * (CD_FRAMESIZE / 512);
  
  request_region(CDo_command,4,major_name);

  for (j=0;j<NR_SBPCD;j++)
    {
      if (DriveStruct[j].drv_id==-1) continue;
      switch_drive(j);
/*
 * allocate memory for the frame buffers
 */ 
      DriveStruct[j].sbp_buf=(u_char *)mem_start;
      mem_start += SBP_BUFFER_FRAMES*CD_FRAMESIZE;
      if ((fam1_drive) && (SBP_BUFFER_AUDIO_FRAMES>0))
	{
	  DriveStruct[j].aud_buf=(u_char *)mem_start;
	  mem_start += SBP_BUFFER_AUDIO_FRAMES*CD_FRAMESIZE_RAW;
	}
      else DriveStruct[j].aud_buf=NULL;
/*
 * set the block size
 */
      sbpcd_blocksizes[j]=CD_FRAMESIZE;
    }
  blksize_size[MAJOR_NR]=sbpcd_blocksizes;

init_done:
#if !(SBPCD_ISSUE-1)
#ifdef CONFIG_SBPCD2
  mem_start=sbpcd2_init(mem_start, mem_end);
#endif
#ifdef CONFIG_SBPCD3
  mem_start=sbpcd3_init(mem_start, mem_end);
#endif
#ifdef CONFIG_SBPCD4
  mem_start=sbpcd4_init(mem_start, mem_end);
#endif
#endif
#if !(SBPCD_ISSUE-1)
  DPRINTF((DBG_INF,"SBPCD: init done.\n"));
#endif
  return (mem_start);
}
/*==========================================================================*/
/*
 * Check if the media has changed in the CD-ROM drive.
 * used externally (isofs/inode.c, fs/buffer.c)
 * Currently disabled (has to get "synchronized").
 */
static int sbpcd_chk_disk_change(dev_t full_dev)
{
  int i, st;

  DPRINTF((DBG_CHK,"SBPCD: media_check (%d) called\n", MINOR(full_dev)));
  return (0); /* "busy" test necessary before we really can check */

  i=MINOR(full_dev);
  if ( (i<0) || (i>=NR_SBPCD) || (DriveStruct[i].drv_id==-1) )
    {
      printk("SBPCD: media_check: invalid device %04X.\n", full_dev);
      return (-1);
    }

  switch_drive(i);
  
  xx_ReadStatus();                         /* command: give 1-byte status */
  st=ResponseStatus();
  DPRINTF((DBG_CHK,"SBPCD: media_check: %02X\n",DriveStruct[d].status_byte));
  if (st<0)
    {
      DPRINTF((DBG_INF,"SBPCD: media_check: ResponseStatus error.\n"));
      return (1); /* status not obtainable */
    }
  if (DriveStruct[d].CD_changed==0xFF) DPRINTF((DBG_CHK,"SBPCD: media_check: \"changed\" assumed.\n"));
  if (!st_spinning) DPRINTF((DBG_CHK,"SBPCD: media_check: motor off.\n"));
  if (!st_door_closed)
    {
      DPRINTF((DBG_CHK,"SBPCD: media_check: door open.\n"));
      DriveStruct[d].CD_changed=0xFF;
    }
  if (!st_caddy_in)
    {
      DPRINTF((DBG_CHK,"SBPCD: media_check: no disk in drive.\n"));
      DriveStruct[d].CD_changed=0xFF;
    }
  if (!st_diskok) DPRINTF((DBG_CHK,"SBPCD: media_check: !st_diskok.\n"));
  
#if 0000
  if (DriveStruct[d].CD_changed==0xFF)
    {
      DriveStruct[d].CD_changed=1;
      return (1); /* driver had a change detected before */
    }
#endif 0000 /* seems to give additional errors at the moment */

  if (!st_diskok) return (1); /* disk not o.k. */
  if (!st_caddy_in) return (1); /* disk removed */
  if (!st_door_closed) return (1); /* door open */
  return (0);
}
/*==========================================================================*/
