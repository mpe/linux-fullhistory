/* cm206.c. A linux-driver for the cm206 cdrom player with cm260 adapter card.
   Copyright (c) 1995 David van Leeuwen.
   
     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 2 of the License, or
     (at your option) any later version.
     
     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.
     
     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

History:
 Started 25 jan 1994. Waiting for documentation...
 22 feb 1995: 0.1a first reasonably safe polling driver.
	      Two major bugs, one in read_sector and one in 
	      do_cm206_request, happened to cancel!
 25 feb 1995: 0.2a first reasonable interrupt driven version of above.
              uart writes are still done in polling mode. 
 25 feb 1995: 0.21a writes also in interrupt mode, still some
	      small bugs to be found... Larger buffer. 
  2 mrt 1995: 0.22 Bug found (cd-> nowhere, interrupt was called in
              initialization), read_ahead of 16. Timeouts implemented.
	      unclear if they do something...
  7 mrt 1995: 0.23 Start of background read-ahead.
 18 mrt 1995: 0.24 Working background read-ahead. (still problems)
 26 mrt 1995: 0.25 Multi-session ioctl added (kernel v1.2).
              Statistics implemented, though separate stats206.h.
	      Accessible trough ioctl 0x1000 (just a number).
	      Hard to choose between v1.2 development and 1.1.75.
	      Bottom-half doesn't work with 1.2...
	      0.25a: fixed... typo. Still problems...
  1 apr 1995: 0.26 Module support added. Most bugs found. Use kernel 1.2.n.
  5 apr 1995: 0.27 Auto-probe for the adapter card base address.
              Auto-probe for the adaptor card irq line.
  7 apr 1995: 0.28 Added lilo setup support for base address and irq.
              Use major number 32 (not in this source), officially
	      assigned to this driver.
  9 apr 1995: 0.29 Added very limited audio support. Toc_header, stop, pause,
              resume, eject. Play_track ignores track info, because we can't 
	      read a table-of-contents entry. Toc_entry is implemented
	      as a `placebo' function: always returns start of disc. 
  3 may 1995: 0.30 Audio support completed. The get_toc_entry function
              is implemented as a binary search. 
 15 may 1995: 0.31 More work on audio stuff. Workman is not easy to 
              satisfy; changed binary search into linear search.
	      Auto-probe for base address somewhat relaxed.
  1 jun 1995: 0.32 Removed probe_irq_on/off for module version.
 10 jun 1995: 0.33 Workman still behaves funny, but you should be
              able to eject and substitute another disc.

 An adaption of 0.33 is included in linux-1.3.7 by Eberhard Moenkeberg

 18 jul 1996: 0.34 Patch by Heiko Eissfeldt included, mainly considering 
              verify_area's in the ioctls. Some bugs introduced by 
	      EM considering the base port and irq fixed. 
 * 
 * Parts of the code are based upon lmscd.c written by Kai Petzke,
 * sbpcd.c written by Eberhard Moenkeberg, and mcd.c by Martin
 * Harriss, but any off-the-shelf dynamic programming algorithm won't
 * be able to find them.
 *
 * The cm206 drive interface and the cm260 adapter card seem to be 
 * sufficiently different from their cm205/cm250 counterparts
 * in order to write a complete new driver.
 * 
 * I call all routines connected to the Linux kernel something
 * with `cm206' in it, as this stuff is too series-dependent. 
 * 
 * Currently, my limited knowledge is based on:
 * - The Linux Kernel Hacker's guide, v. 0.5 , by Michael J. Johnson
 * - Linux Kernel Programmierung, by Michael Beck and others
 * - Philips/LMS cm206 and cm226 product specification
 * - Philips/LMS cm260 product specification
 *
 *                       David van Leeuwen, david@tm.tno.nl.  */
#define VERSION "0.34"

#ifdef MODULE			/* OK, so some of this is stolen */
#include <linux/module.h>	
#include <linux/version.h>
#include <linux/malloc.h>
#ifndef CONFIG_MODVERSIONS
char kernel_version[]=UTS_RELEASE;
#endif
#else 
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif MODULE

#include <linux/errno.h>	/* These include what we really need */
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/cdrom.h>
#include <linux/ioport.h>
#include <linux/mm.h>

#include <asm/io.h>

#define MAJOR_NR CM206_CDROM_MAJOR
#include "blk.h"
#include <linux/cm206.h>

/* This variable defines whether or not to probe for adapter base port 
   address and interrupt request. It can be overridden by the boot 
   parameter `auto'.
*/
static int auto_probe=1;	/* Yes, why not? */

static int cm206_base = CM206_BASE;
static int cm206_irq = CM206_IRQ; 

#undef DEBUG
#undef DEBUG_SECTORS
#define STATISTICS
#undef AUTO_PROBE_MODULE

#define POLLOOP 10000
#define READ_AHEAD 1		/* defines private buffer, waste! */
#define BACK_AHEAD 1		/* defines adapter-read ahead */
#define DATA_TIMEOUT 300	/* measured in jiffies (10 ms) */
#define UART_TIMEOUT 5
#define DSB_TIMEOUT 700		/* time for the slowest command to finish */

#define RAW_SECTOR_SIZE 2352	/* ok, is also defined in cdrom.h */
#define ISO_SECTOR_SIZE 2048

#ifdef STATISTICS		/* keep track of errors in counters */
#include <linux/stats206.h>
#define stats(i) ++cd->stats[st_ ## i]; \
                 cd->last_stat[st_ ## i] = cd->stat_counter++;
#else
#define stats(i) (void) 0
#endif

#ifdef DEBUG			/* from lmscd.c */
#define debug(a) printk a
#else
#define debug(a) (void) 0
#endif

typedef unsigned char uch;	/* 8-bits */
typedef unsigned short ush;	/* 16-bits */

struct toc_struct{
  uch track, fsm[3], q0;
};

struct cm206_struct {
  ush intr_ds;	 /* data status read on last interrupt */
  ush intr_ls;	 /* uart line status read on last interrupt*/
  uch intr_ur;			/* uart receive buffer */
  uch dsb, cc;	 /* drive status byte and condition (error) code */
  uch fool;
  int command;			/* command to be written to te uart */
  int openfiles;
  ush sector[READ_AHEAD*RAW_SECTOR_SIZE/2]; /* buffered cd-sector */
  int sector_first, sector_last;	/* range of these sector */
  struct wait_queue * uart;	/* wait for interrupt */
  struct wait_queue * data;
  struct timer_list timer;	/* time-out */
  char timed_out;
  signed char max_sectors;
  char wait_back;		/* we're waiting for a background-read */
  char background;		/* is a read going on in the background? */
  int adapter_first;		/* if so, that's the starting sector */
  int adapter_last;
  char fifo_overflowed;
  uch disc_status[7];		/* result of get_disc_status command */
#ifdef STATISTICS
  int stats[NR_STATS];
  int last_stat[NR_STATS];	/* `time' at which stat was stat */
  int stat_counter;
#endif  
  struct toc_struct toc[101];	/* The whole table of contents + lead-out */
  uch q[10];			/* Last read q-channel info */
  uch audio_status[5];		/* last read position on pause */
};

#define DISC_STATUS cd->disc_status[0]
#define FIRST_TRACK cd->disc_status[1]
#define LAST_TRACK cd->disc_status[2]
#define PAUSED cd->audio_status[0] /* misuse this memory byte! */
#define PLAY_TO cd->toc[0]	/* toc[0] records end-time in play */

static struct cm206_struct * cd;

/* First, we define some polling functions. These are actually
   only being used in the initialization. */

void send_command_polled(int command)
{
  int loop=POLLOOP;
  while (!(inw(r_line_status) & ls_transmitter_buffer_empty) && loop>0) 
    --loop;
  outw(command, r_uart_transmit);
}

uch receive_echo_polled(void)
{
  int loop=POLLOOP;
  while (!(inw(r_line_status) & ls_receive_buffer_full) && loop>0) --loop;
  return ((uch) inw(r_uart_receive));
}

uch send_receive_polled(int command)
{
  send_command_polled(command);
  return receive_echo_polled();
}

/* The interrupt handler. When the cm260 generates an interrupt, very
   much care has to be taken in reading out the registers in the right
   order; in case of a receive_buffer_full interrupt, first the
   uart_receive must be read, and then the line status again to
   de-assert the interrupt line. It took me a couple of hours to find
   this out:-( 

   The function reset_cm206 appears to cause an interrupt, because
   pulling up the INIT line clears both the uart-write-buffer /and/
   the uart-write-buffer-empty mask. We call this a `lost interrupt,'
   as there seems so reason for this to happen.
*/

static void cm206_interrupt(int sig, struct pt_regs * regs) /* you rang? */
{
  volatile ush fool;
    cd->intr_ds = inw(r_data_status); /* resets data_ready, data_error,
					 crc_error, sync_error, toc_ready 
					 interrupts */
    cd->intr_ls = inw(r_line_status); /* resets overrun bit */
    /* receive buffer full? */
    if (cd->intr_ls & ls_receive_buffer_full) {	
      cd->intr_ur = inb(r_uart_receive); /* get order right! */
      cd->intr_ls = inw(r_line_status); /* resets rbf interrupt */
      if (!cd->background && cd->uart) wake_up_interruptible(&cd->uart);
    }
    /* data ready in fifo? */
    else if (cd->intr_ds & ds_data_ready) { 
      if (cd->background) ++cd->adapter_last;
      if ((cd->wait_back || !cd->background) && cd->data) 
	  wake_up_interruptible(&cd->data);
      stats(data_ready);
    }
    /* ready to issue a write command? */
    else if (cd->command && cd->intr_ls & ls_transmitter_buffer_empty) {
      outw(dc_normal | (inw(r_data_status) & 0x7f), r_data_control);
      outw(cd->command, r_uart_transmit);
      cd->command=0;
      if (!cd->background) wake_up_interruptible(&cd->uart);
    }
    /* now treat errors (at least, identify them for debugging) */
    else if (cd->intr_ds & ds_fifo_overflow) {
      debug(("Fifo overflow at sectors 0x%x\n", cd->sector_first));
      fool = inw(r_fifo_output_buffer);	/* de-assert the interrupt */
      cd->fifo_overflowed=1;	/* signal one word less should be read */
      stats(fifo_overflow);
    }
    else if (cd->intr_ds & ds_data_error) {
      debug(("Data error at sector 0x%x\n", cd->sector_first));
      stats(data_error);
    }
    else if (cd->intr_ds & ds_crc_error) {
      debug(("CRC error at sector 0x%x\n", cd->sector_first));
      stats(crc_error);
    }
    else if (cd->intr_ds & ds_sync_error) {
      debug(("Sync at sector 0x%x\n", cd->sector_first));
      stats(sync_error);
    }
    else if (cd->intr_ds & ds_toc_ready) {
				/* do something appropiate */
    }
    /* couldn't see why this interrupt, maybe due to init */
    else {			
      outw(dc_normal | READ_AHEAD, r_data_control);
      stats(lost_intr);
    }
  if (cd->background && (cd->adapter_last-cd->adapter_first == cd->max_sectors
      || cd->fifo_overflowed))
    mark_bh(CM206_BH);	/* issue a stop read command */
  stats(interrupt);
}

/* we have put the address of the wait queue in who */
void cm206_timeout(unsigned long who)
{
  cd->timed_out = 1;
  wake_up_interruptible((struct wait_queue **) who);
}

/* This function returns 1 if a timeout occurred, 0 if an interrupt
   happened */
int sleep_or_timeout(struct wait_queue ** wait, int timeout)
{
  cd->timer.data=(unsigned long) wait;
  cd->timer.expires = jiffies + timeout;
  add_timer(&cd->timer);
  interruptible_sleep_on(wait);
  del_timer(&cd->timer);
  if (cd->timed_out) {
    cd->timed_out = 0;
    return 1;
  }
  else return 0;
}

void cm206_delay(int jiffies) 
{
  struct wait_queue * wait = NULL;
  sleep_or_timeout(&wait, jiffies);
}

void send_command(int command)
{
  if (!(inw(r_line_status) & ls_transmitter_buffer_empty)) {
    cd->command = command;
    cli();			/* don't interrupt before sleep */
    outw(dc_mask_sync_error | dc_no_stop_on_error | 
	 (inw(r_data_status) & 0x7f), r_data_control);
    /* interrupt routine sends command */
    if (sleep_or_timeout(&cd->uart, UART_TIMEOUT)) {
      debug(("Time out on write-buffer\n"));
      stats(write_timeout);
      outw(command, r_uart_transmit);
    }
  }
  else outw(command, r_uart_transmit);
}

uch receive_echo(void)
{
  if (!(inw(r_line_status) & ls_receive_buffer_full) &&
      sleep_or_timeout(&cd->uart, UART_TIMEOUT)) {
    debug(("Time out on receive-buffer\n"));
    stats(receive_timeout);
    return ((uch) inw(r_uart_receive));
  }
  return cd->intr_ur;
}

inline uch send_receive(int command)
{
  send_command(command);
  return receive_echo();
}

uch wait_dsb(void)
{
  if (!(inw(r_line_status) & ls_receive_buffer_full) &&
      sleep_or_timeout(&cd->uart, DSB_TIMEOUT)) {
    debug(("Time out on Drive Status Byte\n"));
    stats(dsb_timeout);
    return ((uch) inw(r_uart_receive));
  }
  return cd->intr_ur;
}

int type_0_command(int command, int expect_dsb)
{
  int e;
  if (command != (e=send_receive(command))) {
    debug(("command 0x%x echoed as 0x%x\n", command, e));
    stats(echo);
    return -1;
  }
  if (expect_dsb) {
    cd->dsb = wait_dsb();	/* wait for command to finish */
  }
  return 0;
}

int type_1_command(int command, int bytes, uch * status) /* returns info */
{
  int i;
  if (type_0_command(command,0)) return -1;
  for(i=0; i<bytes; i++) 
    status[i] = send_receive(c_gimme);
  return 0;
}  

/* This function resets the adapter card. We'd better not do this too */
/* often, because it tends to generate `lost interrupts.' */
void reset_cm260(void)
{
  outw(dc_normal | dc_initialize | READ_AHEAD, r_data_control);
  udelay(10);			/* 3.3 mu sec minimum */
  outw(dc_normal | READ_AHEAD, r_data_control);
}

/* fsm: frame-sec-min from linear address */
void fsm(int lba, uch * fsm) 
{
  fsm[0] = lba % 75;
  lba /= 75; lba += 2;
  fsm[1] = lba % 60; fsm[2] = lba / 60;
}

inline int fsm2lba(uch * fsm) 
{
  return fsm[0] + 75*(fsm[1]-2 + 60*fsm[2]);
}

inline int f_s_m2lba(uch f, uch s, uch m)
{
  return f + 75*(s-2 + 60*m);
}

int start_read(int start) 
{
  uch read_sector[4] = {c_read_data, };
  int i, e;

  fsm(start, &read_sector[1]);
  for (i=0; i<4; i++) 
    if (read_sector[i] != (e=send_receive(read_sector[i]))) {
      debug(("read_sector: %x echoes %x\n", read_sector[i], e));
      stats(echo);
      return -1;
    }
  return 0;
}

int stop_read(void)
{
  type_0_command(c_stop,0);
  if(receive_echo() != 0xff) {
    debug(("c_stop didn't send 0xff\n"));
    stats(stop_0xff);
    return -1;
  }
  return 0;
}  

/* This function starts to read sectors in adapter memory, the
   interrupt routine should stop the read. In fact, the bottom_half
   routine takes care of this. Set a flag `background' in the cd
   struct to indicate the process. */

int read_background(int start, int reading)
{
  if (cd->background) return -1; /* can't do twice */
  outw(dc_normal | BACK_AHEAD, r_data_control);
  if (!reading && start_read(start)) return -2;
  cd->adapter_first = cd->adapter_last = start; 
  cd->background = 1;		/* flag a read is going on */
  return 0;
}

int read_sector(int start)
{
  if (cd->background) {
    cd->background=0;
    cd->adapter_last = -1;	/* invalidate adapter memory */
    stop_read();
  }
  cd->fifo_overflowed=0;
  reset_cm260();		/* empty fifo etc. */
  if (start_read(start)) return -1;
  if (sleep_or_timeout(&cd->data, DATA_TIMEOUT)) {
    debug(("Read timed out sector 0x%x\n", start));
    stats(read_timeout);
    stop_read();
    return -3;		
  }
  insw(r_fifo_output_buffer, cd->sector, READ_AHEAD*RAW_SECTOR_SIZE/2);
  if (read_background(start+READ_AHEAD,1)) stats(read_background);
  cd->sector_first = start; cd->sector_last = start+READ_AHEAD;
  stats(read_restarted);
  return 0;
}

/* The function of bottom-half is to send a stop command to the drive
   This isn't easy because the routine is not `owned' by any process;
   we can't go to sleep! The variable cd->background gives the status:
   0 no read pending
   1 a read is pending
   2 c_stop waits for write_buffer_empty
   3 c_stop waits for receive_buffer_full: echo
   4 c_stop waits for receive_buffer_full: 0xff
*/

void cm206_bh(void * unused)
{
  debug(("bh: %d\n", cd->background));
  switch (cd->background) {
  case 1:
    stats(bh);
    if (!(cd->intr_ls & ls_transmitter_buffer_empty)) {
      cd->command = c_stop;
      outw(dc_mask_sync_error | dc_no_stop_on_error | 
	   (inw(r_data_status) & 0x7f), r_data_control);
      cd->background=2;
      break;			/* we'd better not time-out here! */
    }
    else outw(c_stop, r_uart_transmit);
    /* fall into case 2: */
  case 2:			
    /* the write has been satisfied by interrupt routine */
    cd->background=3;
    break;
  case 3:
    if (cd->intr_ur != c_stop) {
      debug(("cm206_bh: c_stop echoed 0x%x\n", cd->intr_ur));
      stats(echo);
    }
    cd->background++;
    break;
  case 4:
    if (cd->intr_ur != 0xff) {
      debug(("cm206_bh: c_stop reacted with 0x%x\n", cd->intr_ur));
      stats(stop_0xff);
    }
    cd->background=0;
  }
}

void get_drive_status(void)
{
  uch status[2];
  type_1_command(c_drive_status, 2, status); /* this might be done faster */
  cd->dsb=status[0];
  cd->cc=status[1];
}

void get_disc_status(void)
{
  if (type_1_command(c_disc_status, 7, cd->disc_status)) {
    debug(("get_disc_status: error\n"));
  }
}

static int cm206_open(struct inode *ip, struct file *fp)
{
  if (!cd->openfiles) {
    cd->background=0;
    reset_cm260();
    cd->adapter_last = -1;	/* invalidate adapter memory */
    cd->sector_last = -1;
    get_drive_status();
    if (cd->dsb & dsb_tray_not_closed) {
      int i=0;
      type_0_command(c_close_tray, 1);
      while (i++<10 && cd->dsb & dsb_drive_not_ready) {
	cm206_delay(100);
	get_drive_status();
      }
    }
    if (cd->dsb & (dsb_not_useful)) return -EIO;
    if (!(cd->dsb & dsb_disc_present)) return -ENODATA;
    if (cd->dsb & dsb_possible_media_change) {
      memset(cd->toc, 0, sizeof(cd->toc));
      memset(cd->audio_status, 0, sizeof(cd->audio_status));
    }
    get_disc_status();
    type_0_command(c_lock_tray,1);
    if (!(cd->dsb & dsb_tray_locked)) {
      debug(("Couldn't lock tray\n"));
    }
#if 0
    if (!(DISC_STATUS & cds_all_audio))
      read_background(16,0);	/* do something useful */
#endif
  }
  ++cd->openfiles; MOD_INC_USE_COUNT;
  stats(open);
  return 0;
}

static void cm206_release(struct inode *ip, struct file *fp)
{
  if (cd->openfiles==1) {
    if (cd->background) {
      cd->background=0;
      stop_read();
    }
    type_0_command(c_unlock_tray,1);
    cd->sector_last = -1;	/* Make our internal buffer invalid */
    FIRST_TRACK = 0;	/* No valid disc status */
    sync_dev(ip -> i_rdev);	/* These two lines are stolen */
    invalidate_buffers(ip -> i_rdev);
  }
  --cd->openfiles; MOD_DEC_USE_COUNT;
}

/* Empty buffer empties $sectors$ sectors of the adapter card buffer,
 * and then reads a sector in kernel memory.  */
void empty_buffer(int sectors) 
{
  while (sectors>=0) {
    insw(r_fifo_output_buffer, cd->sector + cd->fifo_overflowed, 
	 RAW_SECTOR_SIZE/2 - cd->fifo_overflowed);
    --sectors;
    ++cd->adapter_first;	/* update the current adapter sector */
    cd->fifo_overflowed=0;	/* reset overflow bit */
    stats(sector_transferred);
  } 
  cd->sector_first=cd->adapter_first-1;
  cd->sector_last=cd->adapter_first; /* update the buffer sector */
}

/* try_adapter. This function determines of the requested sector is is
   in adapter memory, or will appear there soon. Returns 0 upon
   success */
int try_adapter(int sector)
{
  if (cd->adapter_first <= sector && sector < cd->adapter_last) { 
    /* sector is in adapter memory */
    empty_buffer(sector - cd->adapter_first);
    return 0;
  }
  else if (cd->background==1 && cd->adapter_first <= sector
	   && sector < cd->adapter_first+cd->max_sectors) {
    /* a read is going on, we can wait for it */
    cd->wait_back=1;
    while (sector >= cd->adapter_last) {
      if (sleep_or_timeout(&cd->data, DATA_TIMEOUT)) {
	debug(("Timed out during background wait: %d %d %d %d\n", sector, 
	       cd->adapter_last, cd->adapter_first, cd->background));
	stats(back_read_timeout);
	cd->wait_back=0;
	return -1;
      }
    }
    cd->wait_back=0;
    empty_buffer(sector - cd->adapter_first);
    return 0;
  }
  else return -2;
}

/* This is not a very smart implementation. We could optimize for 
   consecutive block numbers. I'm not conviced this would really
   bring down the processor load. */
static void do_cm206_request(void)
{
  long int i, cd_sec_no;
  int quarter, error; 
  uch * source, * dest;
  
  while(1) {	 /* repeat until all requests have been satisfied */
    INIT_REQUEST;
    if (CURRENT == NULL || CURRENT->dev == -1) return;
    if (CURRENT->cmd != READ) {
      debug(("Non-read command %d on cdrom\n", CURRENT->cmd));
      end_request(0);
      continue;
    }
    error=0;
    for (i=0; i<CURRENT->nr_sectors; i++) {
      cd_sec_no = (CURRENT->sector+i)/4; /* 4 times 512 bytes */
      quarter = (CURRENT->sector+i) % 4; 
      dest = CURRENT->buffer + i*512;
      /* is already in buffer memory? */
      if (cd->sector_first <= cd_sec_no && cd_sec_no < cd->sector_last) {
	source = ((uch *) cd->sector) + 16 + 
	  quarter*512 + (cd_sec_no-cd->sector_first)*RAW_SECTOR_SIZE;
 	memcpy(dest, source, 512); 
      }
      else if (!try_adapter(cd_sec_no) || !read_sector(cd_sec_no)) {
	source =  ((uch *) cd->sector)+16+quarter*512;
	memcpy(dest, source, 512); 
      }
      else {
	error=1;
      }
    }
    end_request(!error);
  }
}

int get_multi_session_info(struct cdrom_multisession * mssp)
{
  if (!FIRST_TRACK) get_disc_status();
  if (mssp) {
    if (DISC_STATUS & cds_multi_session) { /* multi-session */
      if (mssp->addr_format == CDROM_LBA)
      	mssp->addr.lba = fsm2lba(&cd->disc_status[3]);
      else {
      	mssp->addr.msf.frame = cd->disc_status[3];
      	mssp->addr.msf.second = cd->disc_status[4];
      	mssp->addr.msf.minute = cd->disc_status[5];
      }
      mssp->xa_flag = 1;
    } else {
      mssp->xa_flag = 0;
    }
    return 1;
  }
  return 0;
}

/* Audio support. I've tried very hard, but the cm206 drive doesn't 
   seem to have a get_toc (table-of-contents) function, while i'm
   pretty sure it must read the toc upon disc insertion. Therefore
   this function has been implemented through a binary search 
   strategy. All track starts that happen to be found are stored in
   cd->toc[], for future use. 

   I've spent a whole day on a bug that only shows under Workman---
   I don't get it. Tried everything, nothing works. If workman asks
   for track# 0xaa, it'll get the wrong time back. Any other program
   receives the correct value. I'm stymied.
*/

/* seek seeks to address lba. It does wait to arrive there. */
void seek(int lba)
{
  int i;
  uch seek_command[4]={c_seek, };
  
  fsm(lba, &seek_command[1]);
  for (i=0; i<4; i++) type_0_command(seek_command[i], 0);
  cd->dsb = wait_dsb();
}

uch bcdbin(unsigned char bcd)	/* stolen from mcd.c! */
{
  return (bcd >> 4)*10 + (bcd & 0xf);
} 

inline uch normalize_track(uch track) 
{
  if (track<1) return 1;
  if (track>LAST_TRACK) return LAST_TRACK+1;
  return track;
}

/* This function does a binary search for track start. It records all
 * tracks seen in the process. Input $track$ must be between 1 and
 * #-of-tracks+1 */
int get_toc_lba(uch track)
{
  int max=74*60*75-150, min=0;
  int i, lba, l, old_lba=0;
  uch * q = cd->q;
  uch ct;			/* current track */
  int binary=0;
  const skip = 3*60*75;

  for (i=track; i>0; i--) if (cd->toc[i].track) {
    min = fsm2lba(cd->toc[i].fsm);
    break;
  }
  lba = min + skip;		/* 3 minutes */
  do {
    seek(lba); 
    type_1_command(c_read_current_q, 10, q);
    ct = normalize_track(q[1]);
    if (!cd->toc[ct].track) {
      l = q[9]-bcdbin(q[5]) + 75*(q[8]-bcdbin(q[4])-2 + 
				  60*(q[7]-bcdbin(q[3])));
      cd->toc[ct].track=q[1];	/* lead out still 0xaa */
      fsm(l, cd->toc[ct].fsm);
      cd->toc[ct].q0 = q[0];	/* contains adr and ctrl info */
/*
      if (ct==LAST_TRACK+1) 
	printk("Leadout %x %x %x %x %d %d %d \n", q[1], q[3], q[4], q[5],
	       q[7], q[8], q[9]);
*/
      if (ct==track) return l;
    }
    old_lba=lba;
    if (binary) {
      if (ct < track) min = lba; else max = lba;
      lba = (min+max)/2; 
    } else {
      if(ct < track) lba += skip;
      else {
	binary=1;
	max = lba; min = lba - skip;
	lba = (min+max)/2;
      }
    }
  } while (lba!=old_lba);
  return lba;
}

void update_toc_entry(uch track) 
{
  track = normalize_track(track);
  if (!cd->toc[track].track) get_toc_lba(track);
}

int read_toc_header(struct cdrom_tochdr * hp)
{
  if (!FIRST_TRACK) get_disc_status();
  if (hp && DISC_STATUS & cds_all_audio) { /* all audio */
    int i;
    hp->cdth_trk0 = FIRST_TRACK;
    hp->cdth_trk1 = LAST_TRACK;
    cd->toc[1].track=1;		/* fill in first track position */
    for (i=0; i<3; i++) cd->toc[1].fsm[i] = cd->disc_status[3+i];
    update_toc_entry(LAST_TRACK+1);		/* find most entries */
    return 1;
  }
  return 0;
}  

void play_from_to_msf(struct cdrom_msf* msfp)
{
  uch play_command[] = {c_play, 
	   msfp->cdmsf_frame0, msfp->cdmsf_sec0, msfp->cdmsf_min0,
	   msfp->cdmsf_frame1, msfp->cdmsf_sec1, msfp->cdmsf_min1, 2, 2};
  int i;
  for (i=0; i<9; i++) type_0_command(play_command[i], 0);
  for (i=0; i<3; i++) 
    PLAY_TO.fsm[i] = play_command[i+4];
  PLAY_TO.track = 0;		/* say no track end */
  cd->dsb = wait_dsb();
}  

void play_from_to_track(int from, int to)
{
  uch play_command[8] = {c_play, };
  int i;

  if (from==0) {		/* continue paused play */
    for (i=0; i<3; i++) { 
      play_command[i+1] = cd->audio_status[i+2];
      play_command[i+4] = PLAY_TO.fsm[i];
    }
  } else {
    update_toc_entry(from); update_toc_entry(to+1);
    for (i=0; i<3; i++) {
      play_command[i+1] = cd->toc[from].fsm[i];
      PLAY_TO.fsm[i] = play_command[i+4] = cd->toc[to+1].fsm[i];
    }
    PLAY_TO.track = to; 
  }
  for (i=0; i<7; i++) type_0_command(play_command[i],0);
  for (i=0; i<2; i++) type_0_command(0x2, 0); /* volume */
  cd->dsb = wait_dsb();
}

int get_current_q(struct cdrom_subchnl * qp)
{
  int i;
  uch * q = cd->q;
  if (type_1_command(c_read_current_q, 10, q)) return 0;
/*  q[0] = bcdbin(q[0]); Don't think so! */
  for (i=2; i<6; i++) q[i]=bcdbin(q[i]); 
  qp->cdsc_adr = q[0] & 0xf; qp->cdsc_ctrl = q[0] >> 4;	/* from mcd.c */
  qp->cdsc_trk = q[1];  qp->cdsc_ind = q[2];
  if (qp->cdsc_format == CDROM_MSF) {
    qp->cdsc_reladdr.msf.minute = q[3];
    qp->cdsc_reladdr.msf.second = q[4];
    qp->cdsc_reladdr.msf.frame = q[5];
    qp->cdsc_absaddr.msf.minute = q[7];
    qp->cdsc_absaddr.msf.second = q[8];
    qp->cdsc_absaddr.msf.frame = q[9];
  } else {
    qp->cdsc_reladdr.lba = f_s_m2lba(q[5], q[4], q[3]);
    qp->cdsc_absaddr.lba = f_s_m2lba(q[9], q[8], q[7]);
  }
  get_drive_status();
  if (cd->dsb & dsb_play_in_progress) 
    qp->cdsc_audiostatus = CDROM_AUDIO_PLAY ;
  else if (PAUSED) 
    qp->cdsc_audiostatus = CDROM_AUDIO_PAUSED;
  else qp->cdsc_audiostatus = CDROM_AUDIO_NO_STATUS;
  return 1;
}

void get_toc_entry(struct cdrom_tocentry * ep)
{
  uch track = normalize_track(ep->cdte_track);
  update_toc_entry(track);
  if (ep->cdte_format == CDROM_MSF) {
    ep->cdte_addr.msf.frame = cd->toc[track].fsm[0];
    ep->cdte_addr.msf.second = cd->toc[track].fsm[1];
    ep->cdte_addr.msf.minute = cd->toc[track].fsm[2];
  } 
  else ep->cdte_addr.lba = fsm2lba(cd->toc[track].fsm);
  ep->cdte_adr = cd->toc[track].q0 & 0xf; 
  ep->cdte_ctrl = cd->toc[track].q0 >> 4;
  ep->cdte_datamode=0;
}
  
/* Ioctl. I have made the statistics accessible through an ioctl
   call. The constant is defined in cm206.h, it shouldn't clash with
   the standard Linux ioctls. Multisession info is gathered at
   run-time, this may turn out to be slow. */

static int cm206_ioctl(struct inode * inode, struct file * file, 
		       unsigned int cmd, unsigned long arg)
{
  switch (cmd) {
#ifdef STATISTICS
  case CM206CTL_GET_STAT:
    if (arg >= NR_STATS) return -EINVAL;
    else return cd->stats[arg];
  case CM206CTL_GET_LAST_STAT:
    if (arg >= NR_STATS) return -EINVAL;
    else return cd->last_stat[arg];
#endif    
  case CDROMMULTISESSION: {
    struct cdrom_multisession ms_info;
    int st;
    stats(ioctl_multisession);

    st=verify_area(VERIFY_WRITE, (void *) arg, 
		   sizeof(struct cdrom_multisession));
    if (st) return (st);
    memcpy_fromfs(&ms_info, (struct cdrom_multisession *) arg,
		  sizeof(struct cdrom_multisession));
    get_multi_session_info(&ms_info);
    memcpy_tofs((struct cdrom_multisession *) arg, &ms_info, 
		  sizeof(struct cdrom_multisession));
    return 0;
  }
  case CDROMRESET:		/* If needed, it's probably too late anyway */
    stop_read();
    reset_cm260();
    outw(dc_normal | dc_break | READ_AHEAD, r_data_control);
    udelay(1000);		/* 750 musec minimum */
    outw(dc_normal | READ_AHEAD, r_data_control);
    cd->sector_last = -1;	/* flag no data buffered */
    cd->adapter_last = -1;    
    return 0;
  }

  get_drive_status();
  if (cd->dsb & (dsb_drive_not_ready | dsb_tray_not_closed) )
    return -EAGAIN; 

  switch (cmd) {
  case CDROMREADTOCHDR: {
    struct cdrom_tochdr header;
    int st;

    st=verify_area(VERIFY_WRITE, (void *) arg, sizeof(header));
    if (st) return (st);
    if (read_toc_header(&header)) {
      memcpy_tofs((struct cdrom_tochdr *) arg, &header, sizeof(header));
      return 0;
    }
    else return -ENODATA;
  }
  case CDROMREADTOCENTRY: {	
    struct cdrom_tocentry entry;
    int st;

    st=verify_area(VERIFY_WRITE, (void *) arg, sizeof(entry));
    if (st) return (st);
    memcpy_fromfs(&entry, (struct cdrom_tocentry *) arg, sizeof entry);
    get_toc_entry(&entry);
    memcpy_tofs((struct cdrom_tocentry *) arg, &entry, sizeof entry);
    return 0;
  }
  case CDROMPLAYMSF: {
    struct cdrom_msf msf;
    int st;

    st=verify_area(VERIFY_READ, (void *) arg, sizeof(msf));
    if (st) return (st);
    memcpy_fromfs(&msf, (struct cdrom_mdf *) arg, sizeof msf);
    play_from_to_msf(&msf);
    return 0;
  }
  case CDROMPLAYTRKIND: {
    struct cdrom_ti track_index;
    int st;

    st=verify_area(VERIFY_READ, (void *) arg, sizeof(track_index));
    if (st) return (st);
    memcpy_fromfs(&track_index, (struct cdrom_ti *) arg, sizeof(track_index));
    play_from_to_track(track_index.cdti_trk0, track_index.cdti_trk1);
    return 0;
  }
  case CDROMSTOP: 
    PAUSED=0;
    if (cd->dsb & dsb_play_in_progress) return type_0_command(c_stop, 1);
    return 0;
  case CDROMPAUSE: 
    if (cd->dsb & dsb_play_in_progress) {
      type_0_command(c_stop, 1);
      type_1_command(c_audio_status, 5, cd->audio_status);
      PAUSED=1;	/* say we're paused */
    }
    return 0;
  case CDROMRESUME:
    if (PAUSED) play_from_to_track(0,0);
    PAUSED=0;
    return 0;
  case CDROMEJECT:
    PAUSED=0;
    if (cd->openfiles == 1) {	/* Must do an open before an eject! */
      type_0_command(c_open_tray,1);
      memset(cd->toc, 0, sizeof(cd->toc));
      memset(cd->disc_status, 0, sizeof(cd->disc_status));
      return 0;
    }
    else return -EBUSY;
  case CDROMSTART:
  case CDROMVOLCTRL:
    return 0;
  case CDROMSUBCHNL: {
    struct cdrom_subchnl q;
    int st;

    st=verify_area(VERIFY_WRITE, (void *) arg, sizeof(q));
    if (st) return (st);
    memcpy_fromfs(&q, (struct cdrom_subchnl *) arg, sizeof q);
    if (get_current_q(&q)) {
      memcpy_tofs((struct cdrom_subchnl *) arg, &q, sizeof q);
      return 0;
    }
    else return -cmd;
  }
  case CDROM_GET_UPC: {
    uch upc[10];
    int st;

    st=verify_area(VERIFY_WRITE, (void *) arg, 8);
    if (st) return (st);
    if (type_1_command(c_read_upc, 10, upc)) return -EIO;
    memcpy_tofs((uch *) arg, &upc[1], 8);
    return 0;
  } 
  default:
    debug(("Unknown ioctl call 0x%x\n", cmd));
    return -EINVAL;
  }
}     

/* from lmscd.c */
static struct file_operations cm206_fops = {
	NULL,			/* lseek */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir */
	NULL,			/* select */
	cm206_ioctl,		/* ioctl */
	NULL,			/* mmap */
	cm206_open,		/* open */
	cm206_release,		/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* media_change */
	NULL			/* revalidate */
};

/* This routine gets called during init if thing go wrong, can be used
 * in cleanup_module as well. */
void cleanup(int level)
{
  switch (level) {
  case 4: 
    if (unregister_blkdev(MAJOR_NR, "cm206")) {
      printk("Can't unregister cm206\n");
      return;
    }
  case 3: 
    free_irq(cm206_irq);
  case 2: 
  case 1: 
#ifdef MODULE
    kfree(cd);
#endif
    release_region(cm206_base, 16);
  default:
  }
}

/* This function probes for the adapter card. It returns the base
   address if it has found the adapter card. One can specify a base 
   port to probe specifically, or 0 which means span all possible
   bases. 

   Linus says it is too dangerous to use writes for probing, so we
   stick with pure reads for a while. Hope that 8 possible ranges,
   check_region, 15 bits of one port and 6 of another make things
   likely enough to accept the region on the first hit...
 */
int probe_base_port(int base)
{
  int b=0x300, e=0x370;		/* this is the range of start addresses */
  volatile int fool;
#if 0
  const pattern1=0x65, pattern2=0x1a;
#endif

  if (base) b=e=base;
  for (base=b; base<=e; base += 0x10) {
    if (check_region(base, 0x10)) continue;
    fool = inw(base+2);		/* empty possibly uart_receive_buffer */
    if((inw(base+6) & 0xffef) != 0x0001 || /* line_status */
       (inw(base) & 0xad00) != 0) /* data status */
      continue;
#if 0				/* writes... dangerous... */
    outw(dc_normal | pattern1, base+8); 
    if ((inw(base) & 0x7f) != pattern1) continue;
    outw(dc_normal | pattern2, base+8);
    if ((inw(base) & 0x7f) != pattern2) continue;
    outw(dc_normal | READ_AHEAD, base+8);
#endif
    return(base);
  }
  return 0;
}

#if !defined(MODULE) || defined(AUTO_PROBE_MODULE)
/* Probe for irq# nr. If nr==0, probe for all possible irq's. */
int probe_irq(int nr) {
  int irqs, irq;
  outw(dc_normal | READ_AHEAD, r_data_control);	/* disable irq-generation */
  sti(); 
  irqs = probe_irq_on();
  reset_cm260();		/* causes interrupt */
  udelay(10);			/* wait for it */
  irq = probe_irq_off(irqs);
  outw(dc_normal | READ_AHEAD, r_data_control);	/* services interrupt */
  if (nr && irq!=nr && irq>0) return 0;	/* wrong interrupt happened */
  else return irq;
}
#endif

#ifdef MODULE
#define OK  0
#define ERROR  -EIO

static int cm206[2] = {0,0};	/* for compatible `insmod' parameter passing */
void parse_options(void) 
{
  int i;
  for (i=0; i<2; i++) {
    if (0x300 <= cm206[i] && i<= 0x370 && cm206[i] % 0x10 == 0) {
      cm206_base = cm206[i];
      auto_probe=0;
    }
    else if (3 <= cm206[i] && cm206[i] <= 15) {
      cm206_irq = cm206[i];
      auto_probe=0;
    }
  }
}

#else MODULE

#define OK  mem_start+size
#define ERROR  mem_start

#endif MODULE

#ifdef MODULE
int init_module(void)
#else 
unsigned long cm206_init(unsigned long mem_start, unsigned long mem_end)
#endif
{
  uch e=0;
  long int size=sizeof(struct cm206_struct);

  printk("cm206: v" VERSION);
#if defined(MODULE) 
  parse_options();
#if !defined(AUTO_PROBE_MODULE)
   auto_probe=0;
#endif
#endif
  cm206_base = probe_base_port(auto_probe ? 0 : cm206_base);
  if (!cm206_base) {
    printk(" can't find adapter!\n");
    return ERROR;
  }
  printk(" adapter at 0x%x", cm206_base);
  request_region(cm206_base, 16, "cm206");
#ifdef MODULE
  cd = (struct cm206_struct *) kmalloc(size, GFP_KERNEL);
  if (!cd) return ERROR;
#else 
  cd = (struct cm206_struct *) mem_start;
#endif
  /* Now we have found the adaptor card, try to reset it. As we have
   * found out earlier, this process generates an interrupt as well,
   * so we might just exploit that fact for irq probing! */
#if !defined(MODULE) || defined(AUTO_PROBE_MODULE)
  cm206_irq = probe_irq(auto_probe ? 0 : cm206_irq);	
  if (cm206_irq<=0) {
    printk("can't find IRQ!\n");
    cleanup(1);
    return ERROR;
  }
  else printk(" IRQ %d found\n", cm206_irq);
#else
  reset_cm260();
  printk(" using IRQ %d\n", cm206_irq);
#endif
  if (send_receive_polled(c_drive_configuration) != c_drive_configuration) 
    {
      printk(" drive not there\n");
      cleanup(1);
      return ERROR;
    }
  e = send_receive_polled(c_gimme);
  printk("Firmware revision %d", e & dcf_revision_code);
  if (e & dcf_transfer_rate) printk(" double");
  else printk(" single");
  printk(" speed drive");
  if (e & dcf_motorized_tray) printk(", motorized tray");
  if (request_irq(cm206_irq, cm206_interrupt, 0, "cm206")) {
    printk("\nUnable to reserve IRQ---aborted\n");
    cleanup(2);
    return ERROR;
  }
  printk(".\n");
  if (register_blkdev(MAJOR_NR, "cm206", &cm206_fops) != 0) {
    printk("Cannot register for major %d!\n", MAJOR_NR);
    cleanup(3);
    return ERROR;
  }
  blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
  read_ahead[MAJOR_NR] = 16;	/* reads ahead what? */
  bh_base[CM206_BH].routine = cm206_bh;
  enable_bh(CM206_BH);

  memset(cd, 0, sizeof(*cd));	/* give'm some reasonable value */
  cd->sector_last = -1;		/* flag no data buffered */
  cd->adapter_last = -1;
  cd->timer.function = cm206_timeout;
  cd->max_sectors = (inw(r_data_status) & ds_ram_size) ? 24 : 97;
  printk("%d kB adapter memory available, "  
	 " %ld bytes kernel memory used.\n", cd->max_sectors*2, size);
  return OK;
}
#undef OK
#undef ERROR

#ifdef MODULE
void cleanup_module(void)
{
  cleanup(4);
  printk("cm206 removed\n");
}
      
#else MODULE

/* This setup function accepts either `auto' or numbers in the range
 * 3--11 (for irq) or 0x300--0x370 (for base port) or both. */
void cm206_setup(char *s, int *p)
{
  int i;
  if (!strcmp(s, "auto")) auto_probe=1;
  for(i=1; i<=p[0]; i++) {
    if (0x300 <= p[i] && i<= 0x370 && p[i] % 0x10 == 0) {
      cm206_base = p[i];
      auto_probe = 0;
    }
    else if (3 <= p[i] && p[i] <= 15) {
      cm206_irq = p[i];
      auto_probe = 0;
    }
  }
}
#endif MODULE
