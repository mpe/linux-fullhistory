/*
 *    wd33c93.c - Linux-68k device driver for the Commodore
 *                Amiga A2091/590 SCSI controller card
 *
 * Copyright (c) 1996 John Shifflett, GeoLog Consulting
 *    john@geolog.com
 *    jshiffle@netcom.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * Drew Eckhardt's excellent 'Generic NCR5380' sources from Linux-PC
 * provided much of the inspiration and some of the code for this
 * driver. Everything I know about Amiga DMA was gleaned from careful
 * reading of Hamish Mcdonald's original wd33c93 driver; in fact, I
 * borrowed shamelessly from all over that source. Thanks Hamish!
 *
 * _This_ driver is (I feel) an improvement over the old one in
 * several respects:
 *
 *    -  Target Disconnection/Reconnection  is now supported. Any
 *          system with more than one device active on the SCSI bus
 *          will benefit from this. The driver defaults to what I'm
 *          'adaptive disconnect' - meaning that each command is
 *          evaluated individually as to whether or not it should
 *          be run with the option to disconnect/reselect (if the
 *          device chooses), or as a "SCSI-bus-hog".
 *
 *    -  Synchronous data transfers are now supported. Because of
 *          a few devices that choke after telling the driver that
 *          they can do sync transfers, we don't automatically use
 *          this faster protocol - it can be enabled via the command-
 *          line on a device-by-device basis.
 *
 *    -  Runtime operating parameters can now be specified through
 *       the 'amiboot' or the 'insmod' command line. For amiboot do:
 *          "amiboot [usual stuff] wd33c93=blah,blah,blah"
 *       The defaults should be good for most people. See the comment
 *       for 'setup_strings' below for more details.
 *
 *    -  The old driver relied exclusively on what the Western Digital
 *          docs call "Combination Level 2 Commands", which are a great
 *          idea in that the CPU is relieved of a lot of interrupt
 *          overhead. However, by accepting a certain (user-settable)
 *          amount of additional interrupts, this driver achieves
 *          better control over the SCSI bus, and data transfers are
 *          almost as fast while being much easier to define, track,
 *          and debug.
 *
 *
 * TODO:
 *       more speed. linked commands.
 *
 *
 * People with bug reports, wish-lists, complaints, comments,
 * or improvements are asked to pah-leeez email me (John Shifflett)
 * at john@geolog.com or jshiffle@netcom.com! I'm anxious to get
 * this thing into as good a shape as possible, and I'm positive
 * there are lots of lurking bugs and "Stupid Places".
 *
 */

#include <asm/system.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= 0x010300
#include <linux/blk.h>
#else
#include "../block/blk.h"
#endif

#include "scsi.h"
#include "hosts.h"
#include "wd33c93.h"

#ifdef MODULE
#include <linux/module.h>
#endif

/* Leave this undefined for now - need to make some changes in the
 * a3000/a2019/gvp11 files to get it working right
 */
/*#define PROC_INTERFACE*/     /* add code for /proc/scsi/wd33c93/xxx interface */

#define SYNC_DEBUG         /* extra info on sync negotiation printed */
#define DEBUGGING_ON       /* enable command-line debugging bitmask */
#define DEBUG_DEFAULTS 0   /* default debugging bitmask */

#define WD33C93_VERSION    "1.21"
#define WD33C93_DATE       "20/Apr/1996"

#ifdef DEBUGGING_ON
#define DB(f,a) if (hostdata->args & (f)) a;
#else
#define DB(f,a)
#endif

#define IS_DIR_OUT(cmd) ((cmd)->cmnd[0] == WRITE_6  || \
                         (cmd)->cmnd[0] == WRITE_10 || \
                         (cmd)->cmnd[0] == WRITE_12)


/*
 * setup_strings is an array of strings that define some of the operating
 * parameters and settings for this driver. It is used unless an amiboot
 * or insmod command line has been specified, in which case those settings
 * are combined with the ones here. The driver recognizes the following
 * keywords (lower case required) and arguments:
 *
 * -  nosync:bitmask -bitmask is a byte where the 1st 7 bits correspond with
 *                    the 7 possible SCSI devices. Set a bit to prevent sync
 *                    negotiation on that device. To maintain backwards
 *                    compatibility, a command-line such as "wd33c93=255" will
 *                    be automatically translated to "wd33c93=nosync:0xff".
 * -  period:ns      -ns is the minimum # of nanoseconds in a SCSI data transfer
 *                    period. Default is 500; acceptable values are 250 - 1000.
 * -  disconnect:x   -x = 0 to never allow disconnects, 2 to always allow them.
 *                    x = 1 does 'adaptive' disconnects, which is the default
 *                    and generally the best choice.
 * -  debug:x        -If 'DEBUGGING_ON' is defined, x is a bit mask that causes
 *                    various types of debug output to printed - see the DB_xxx
 *                    defines in wd33c93.h
 * -  clock:x        -x = clock input in MHz for WD33c93 chip. Normal values
 *                    would be from 8 through 20. Default is 8.
 * -  next           -No argument. Used to separate blocks of keywords when
 *                    there's more than one host adapter in the system.
 *
 * Syntax Notes:
 * -  Numeric arguments can be decimal or the '0x' form of hex notation. There
 *    _must_ be a colon between a keyword and its numeric argument, with no
 *    spaces.
 * -  Keywords are separated by commas, no spaces, in the standard kernel
 *    command-line manner, except in the case of 'setup_strings[]' (see
 *    below), which is simply a C array of pointers to char. Each element
 *    in the array is a string comprising one keyword & argument.
 * -  A keyword in the 'nth' comma-separated command-line member will overwrite
 *    the 'nth' element of setup_strings[]. A blank command-line member (in
 *    other words, a comma with no preceding keyword) will _not_ overwrite
 *    the corresponding setup_strings[] element.
 * -  If a keyword is used more than once, the first one applies to the first
 *    SCSI host found, the second to the second card, etc, unless the 'next'
 *    keyword is used to change the order.
 *
 * Some amiboot examples (for insmod, use 'setup_strings' instead of 'wd33c93'):
 * -  wd33c93=nosync:255
 * -  wd33c93=disconnect:2,nosync:0x08,period:250
 * -  wd33c93=debug:0x1c
 */

static char *setup_strings[] =
      {"","","","","","","","","","","",""};

#ifdef PROC_INTERFACE
unsigned long disc_allowed_total;
unsigned long disc_taken_total;
#endif


inline uchar read_wd33c93(wd33c93_regs *regp,uchar reg_num)
{
   regp->SASR = reg_num;
   return(regp->SCMD);
}


#define READ_AUX_STAT() (regp->SASR)


inline void write_wd33c93(wd33c93_regs *regp,uchar reg_num, uchar value)
{
   regp->SASR = reg_num;
   regp->SCMD = value;
}


inline void write_wd33c93_cmd(wd33c93_regs *regp, uchar cmd)
{
   regp->SASR = WD_COMMAND;
   regp->SCMD = cmd;
}


inline uchar read_1_byte(wd33c93_regs *regp)
{
uchar asr;
uchar x = 0;

   write_wd33c93(regp, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
   write_wd33c93_cmd(regp, WD_CMD_TRANS_INFO|0x80);
   do {
      asr = READ_AUX_STAT();
      if (asr & ASR_DBR)
         x = read_wd33c93(regp, WD_DATA);
      } while (!(asr & ASR_INT));
   return x;
}


void write_wd33c93_count(wd33c93_regs *regp,unsigned long value)
{
   regp->SASR = WD_TRANSFER_COUNT_MSB;
   regp->SCMD = value >> 16;
   regp->SCMD = value >> 8;
   regp->SCMD = value;
}


unsigned long read_wd33c93_count(wd33c93_regs *regp)
{
unsigned long value;

   regp->SASR = WD_TRANSFER_COUNT_MSB;
   value = regp->SCMD << 16;
   value |= regp->SCMD << 8;
   value |= regp->SCMD;
   return value;
}



static struct sx_period sx_table[] = {
   {  1, 0x20},
   {252, 0x20},
   {376, 0x30},
   {500, 0x40},
   {624, 0x50},
   {752, 0x60},
   {876, 0x70},
   {1000,0x00},
   {0,   0} };

int round_period(unsigned int period)
{
int x;

   for (x=1; sx_table[x].period_ns; x++) {
      if ((period <= sx_table[x-0].period_ns) &&
          (period >  sx_table[x-1].period_ns)) {
         return x;
         }
      }
   return 7;
}

uchar calc_sync_xfer(unsigned int period, unsigned int offset)
{
uchar result;

   period *= 4;   /* convert SDTR code to ns */
   result = sx_table[round_period(period)].reg_value;
   result |= (offset < OPTIMUM_SX_OFF)?offset:OPTIMUM_SX_OFF;
   return result;
}



void wd33c93_execute(struct Scsi_Host *instance);

int wd33c93_queuecommand (Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
struct WD33C93_hostdata *hostdata;
Scsi_Cmnd *tmp;
unsigned long flags;


   save_flags(flags);
   cli();
   hostdata = (struct WD33C93_hostdata *)cmd->host->hostdata;

DB(DB_QUEUE_COMMAND,printk("Q-%d-%02x-%ld( ",cmd->target,cmd->cmnd[0],cmd->pid))

/* Set up a few fields in the Scsi_Cmnd structure for our own use:
 *  - host_scribble is the pointer to the next cmd in the input queue
 *  - scsi_done points to the routine we call when a cmd is finished
 *  - result is what you'd expect
 */

   cmd->host_scribble = NULL;
   cmd->scsi_done = done;
   cmd->result = 0;

/* We use the Scsi_Pointer structure that's included with each command
 * as a scratchpad (as it's intended to be used!). The handy thing about
 * the SCp.xxx fields is that they're always associated with a given
 * cmd, and are preserved across disconnect-reselect. This means we
 * can pretty much ignore SAVE_POINTERS and RESTORE_POINTERS messages
 * if we keep all the critical pointers and counters in SCp:
 *  - SCp.ptr is the pointer into the RAM buffer
 *  - SCp.this_residual is the size of that buffer
 *  - SCp.buffer points to the current scatter-gather buffer
 *  - SCp.buffers_residual tells us how many S.G. buffers there are
 *  - SCp.have_data_in is not used
 *  - SCp.sent_command is not used
 *  - SCp.phase records this command's SRCID_ER bit setting
 */

   if (cmd->use_sg) {
      cmd->SCp.buffer = (struct scatterlist *)cmd->buffer;
      cmd->SCp.buffers_residual = cmd->use_sg - 1;
      cmd->SCp.ptr = (char *)cmd->SCp.buffer->address;
      cmd->SCp.this_residual = cmd->SCp.buffer->length;
      }
   else {
      cmd->SCp.buffer = NULL;
      cmd->SCp.buffers_residual = 0;
      cmd->SCp.ptr = (char *)cmd->request_buffer;
      cmd->SCp.this_residual = cmd->request_bufflen;
      }

/* Preset the command status to GOOD, since that's the normal case */

   cmd->SCp.Status = GOOD;

   /*
    * Add the cmd to the end of 'input_Q'. Note that REQUEST SENSE
    * commands are added to the head of the queue so that the desired
    * sense data is not lost before REQUEST_SENSE executes.
    */

   if (!(hostdata->input_Q) || (cmd->cmnd[0] == REQUEST_SENSE)) {
      cmd->host_scribble = (uchar *)hostdata->input_Q;
      hostdata->input_Q = cmd;
      }
   else {   /* find the end of the queue */
      for (tmp=(Scsi_Cmnd *)hostdata->input_Q; tmp->host_scribble;
            tmp=(Scsi_Cmnd *)tmp->host_scribble)
         ;
      tmp->host_scribble = (uchar *)cmd;
      }

/* We know that there's at least one command in 'input_Q' now.
 * Go see if any of them are runnable!
 */

   wd33c93_execute(cmd->host);

DB(DB_QUEUE_COMMAND,printk(")Q-%ld ",cmd->pid))

   restore_flags(flags);
   return 0;
}



/*
 * This routine attempts to start a scsi command. If the host_card is
 * already connected, we give up immediately. Otherwise, look through
 * the input_Q, using the first command we find that's intended
 * for a currently non-busy target/lun.
 */
void wd33c93_execute (struct Scsi_Host *instance)
{
struct WD33C93_hostdata *hostdata;
wd33c93_regs *regp;
Scsi_Cmnd *cmd, *prev;
unsigned long flags;
int i;


   save_flags(flags);
   cli();
   hostdata = (struct WD33C93_hostdata *)instance->hostdata;
   regp = hostdata->regp;

DB(DB_EXECUTE,printk("EX("))

   if (hostdata->selecting || hostdata->connected) {

DB(DB_EXECUTE,printk(")EX-0 "))

      restore_flags(flags);
      return;
      }

    /*
     * Search through the input_Q for a command destined
     * for an idle target/lun.
     */

   cmd = (Scsi_Cmnd *)hostdata->input_Q;
   prev = 0;
   while (cmd) {
      if (!(hostdata->busy[cmd->target] & (1 << cmd->lun)))
         break;
      prev = cmd;
      cmd = (Scsi_Cmnd *)cmd->host_scribble;
      }

   /* quit if queue empty or all possible targets are busy */

   if (!cmd) {

DB(DB_EXECUTE,printk(")EX-1 "))

      restore_flags(flags);
      return;
      }

   /*  remove command from queue */
   
   if (prev)
      prev->host_scribble = cmd->host_scribble;
   else
      hostdata->input_Q = (Scsi_Cmnd *)cmd->host_scribble;

   /*
    * Start the selection process
    */

   if (IS_DIR_OUT(cmd))
      write_wd33c93(regp, WD_DESTINATION_ID, cmd->target);
   else
      write_wd33c93(regp, WD_DESTINATION_ID, cmd->target | DSTID_DPD);

/* Now we need to figure out whether or not this command is a good
 * candidate for disconnect/reselect. We guess to the best of our
 * ability, based on a set of hierarchical rules. When several
 * devices are operating simultaneously, disconnects are usually
 * an advantage. In a single device system, or if only 1 device
 * is being accessed, transfers usually go faster if disconnects
 * are not allowed:
 *
 * + Commands should NEVER disconnect if hostdata->disconnect =
 *   DIS_NEVER (this holds for tape drives also), and ALWAYS
 *   disconnect if hostdata->disconnect = DIS_ALWAYS.
 * + Tape drive commands should always be allowed to disconnect.
 * + Disconnect should be allowed if disconnected_Q isn't empty.
 * + Commands should NOT disconnect if input_Q is empty.
 * + Disconnect should be allowed if there are commands in input_Q
 *   for a different target/lun. In this case, the other commands
 *   should be made disconnect-able, if not already.
 *
 * I know, I know - this code would flunk me out of any
 * "C Programming 101" class ever offered. But it's easy
 * to change around and experiment with for now.
 */

   cmd->SCp.phase = 0;  /* assume no disconnect */
   if (hostdata->disconnect == DIS_NEVER)
      goto no;
   if (hostdata->disconnect == DIS_ALWAYS)
      goto yes;
   if (cmd->device->type == 1)   /* tape drive? */
      goto yes;
   if (hostdata->disconnected_Q) /* other commands disconnected? */
      goto yes;
   if (!(hostdata->input_Q))     /* input_Q empty? */
      goto no;
   for (prev=(Scsi_Cmnd *)hostdata->input_Q; prev;
         prev=(Scsi_Cmnd *)prev->host_scribble) {
      if ((prev->target != cmd->target) || (prev->lun != cmd->lun)) {
         for (prev=(Scsi_Cmnd *)hostdata->input_Q; prev;
               prev=(Scsi_Cmnd *)prev->host_scribble)
            prev->SCp.phase = 1;
         goto yes;
         }
      }
   goto no;

yes:
   cmd->SCp.phase = 1;

#ifdef PROC_INTERFACE
   disc_allowed_total++;
#endif

no:
   write_wd33c93(regp, WD_SOURCE_ID, ((cmd->SCp.phase)?SRCID_ER:0));

   write_wd33c93(regp, WD_TARGET_LUN, cmd->lun);
   write_wd33c93(regp,WD_SYNCHRONOUS_TRANSFER,hostdata->sync_xfer[cmd->target]);
   hostdata->busy[cmd->target] |= (1 << cmd->lun);

   if ((hostdata->level2 == L2_NONE) ||
       (hostdata->sync_stat[cmd->target] == SS_UNSET)) {

         /*
          * Do a 'Select-With-ATN' command. This will end with
          * one of the following interrupts:
          *    CSR_RESEL_AM:  failure - can try again later.
          *    CSR_TIMEOUT:   failure - give up.
          *    CSR_SELECT:    success - proceed.
          */

      hostdata->selecting = cmd;

/* Every target has its own synchronous transfer setting, kept in the
 * sync_xfer array, and a corresponding status byte in sync_stat[].
 * Each target's sync_stat[] entry is initialized to SX_UNSET, and its
 * sync_xfer[] entry is initialized to the default/safe value. SS_UNSET
 * means that the parameters are undetermined as yet, and that we
 * need to send an SDTR message to this device after selection is
 * complete. We set SS_FIRST to tell the interrupt routine to do so,
 * unless we've been asked not to try synchronous transfers on this
 * target (and _all_ luns within it): In this case we set SS_SET to
 * make the defaults final.
 */
      if (hostdata->sync_stat[cmd->target] == SS_UNSET) {
         if (hostdata->no_sync & (1 << cmd->target))
            hostdata->sync_stat[cmd->target] = SS_SET;
         else
            hostdata->sync_stat[cmd->target] = SS_FIRST;
         }
      hostdata->state = S_SELECTING;
      write_wd33c93_count(regp,0); /* guarantee a DATA_PHASE interrupt */
      write_wd33c93_cmd(regp, WD_CMD_SEL_ATN);
      }

   else {

         /*
          * Do a 'Select-With-ATN-Xfer' command. This will end with
          * one of the following interrupts:
          *    CSR_RESEL_AM:  failure - can try again later.
          *    CSR_TIMEOUT:   failure - give up.
          *    anything else: success - proceed.
          */

      hostdata->connected = cmd;
      write_wd33c93(regp, WD_COMMAND_PHASE, 0);

   /* copy command_descriptor_block into WD chip
    * (take advantage of auto-incrementing)
    */

      regp->SASR = WD_CDB_1;
      for (i=0; i<cmd->cmd_len; i++)
         regp->SCMD = cmd->cmnd[i];

   /* The wd33c93 only knows about Group 0, 1, and 5 commands when
    * it's doing a 'select-and-transfer'. To be safe, we write the
    * size of the CDB into the OWN_ID register for every case. This
    * way there won't be problems with vendor-unique, audio, etc.
    */

      write_wd33c93(regp, WD_OWN_ID, cmd->cmd_len);

   /* When doing a non-disconnect command, we can save ourselves a DATA
    * phase interrupt later by setting everything up now.
    */

      if (cmd->SCp.phase == 0) {
         if (hostdata->dma_setup(cmd,
                     (IS_DIR_OUT(cmd))?DATA_OUT_DIR:DATA_IN_DIR))
            write_wd33c93_count(regp,0); /* guarantee a DATA_PHASE interrupt */
         else {
            write_wd33c93_count(regp, cmd->SCp.this_residual);
            write_wd33c93(regp,WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_DMA);
            hostdata->dma = D_DMA_RUNNING;
            }
         }
      else
         write_wd33c93_count(regp,0); /* guarantee a DATA_PHASE interrupt */

      hostdata->state = S_RUNNING_LEVEL2;
      write_wd33c93_cmd(regp, WD_CMD_SEL_ATN_XFER);
      }

   /*
    * Since the SCSI bus can handle only 1 connection at a time,
    * we get out of here now. If the selection fails, or when
    * the command disconnects, we'll come back to this routine
    * to search the input_Q again...
    */
      
DB(DB_EXECUTE,printk("%s%ld)EX-2 ",(cmd->SCp.phase)?"d:":"",cmd->pid))

   restore_flags(flags);
}



void transfer_pio(wd33c93_regs *regp, uchar *buf, int cnt,
                  int data_in_dir, struct WD33C93_hostdata *hostdata)
{
uchar asr;

DB(DB_TRANSFER,printk("(%p,%d,%s)",buf,cnt,data_in_dir?"in":"out"))

   write_wd33c93(regp, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
   write_wd33c93_count(regp,cnt);
   write_wd33c93_cmd(regp, WD_CMD_TRANS_INFO);
   if (data_in_dir) {
      do {
         asr = READ_AUX_STAT();
         if (asr & ASR_DBR)
            *buf++ = read_wd33c93(regp, WD_DATA);
         } while (!(asr & ASR_INT));
      }
   else {
      do {
         asr = READ_AUX_STAT();
         if (asr & ASR_DBR)
            write_wd33c93(regp, WD_DATA, *buf++);
         } while (!(asr & ASR_INT));
      }

   /* Note: we are returning with the interrupt UN-cleared.
   * Since (presumably) an entire I/O operation has
   * completed, the bus phase is probably different, and
   * the interrupt routine will discover this when it
   * responds to the uncleared int.
   */

}



void transfer_bytes(wd33c93_regs *regp, Scsi_Cmnd *cmd, int data_in_dir)
{
struct WD33C93_hostdata *hostdata;

   hostdata = (struct WD33C93_hostdata *)cmd->host->hostdata;

/* Normally, you'd expect 'this_residual' to be non-zero here.
 * In a series of scatter-gather transfers, however, this
 * routine will usually be called with 'this_residual' equal
 * to 0 and 'buffers_residual' non-zero. This means that a
 * previous transfer completed, clearing 'this_residual', and
 * now we need to setup the next scatter-gather buffer as the
 * source or destination for THIS transfer.
 */
   if (!cmd->SCp.this_residual && cmd->SCp.buffers_residual) {
      ++cmd->SCp.buffer;
      --cmd->SCp.buffers_residual;
      cmd->SCp.this_residual = cmd->SCp.buffer->length;
      cmd->SCp.ptr = cmd->SCp.buffer->address;
      }

   write_wd33c93(regp,WD_SYNCHRONOUS_TRANSFER,hostdata->sync_xfer[cmd->target]);

/* 'dma_setup()' will return TRUE if we can't do DMA. */

   if (hostdata->dma_setup(cmd, data_in_dir)) {
      transfer_pio(regp, (uchar *)&cmd->SCp.ptr, cmd->SCp.this_residual,
                         data_in_dir, hostdata);
      }

/* We are able to do DMA (in fact, the Amiga hardware is
 * already going!), so start up the wd33c93 in DMA mode.
 * We set 'hostdata->dma' = D_DMA_RUNNING so that when the
 * transfer completes and causes an interrupt, we're
 * reminded to tell the Amiga to shut down its end. We'll
 * postpone the updating of 'this_residual' and 'ptr'
 * until then.
 */

   else {
      write_wd33c93(regp, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_DMA);
      write_wd33c93_count(regp,cmd->SCp.this_residual);

      if ((hostdata->level2 >= L2_DATA) || (cmd->SCp.phase == 0)) {
         write_wd33c93(regp, WD_COMMAND_PHASE, 0x45);
         write_wd33c93_cmd(regp, WD_CMD_SEL_ATN_XFER);
         hostdata->state = S_RUNNING_LEVEL2;
         }
      else
         write_wd33c93_cmd(regp, WD_CMD_TRANS_INFO);

      hostdata->dma = D_DMA_RUNNING;
      }
}



void wd33c93_intr (struct Scsi_Host *instance)
{
struct WD33C93_hostdata *hostdata;
Scsi_Cmnd *patch, *cmd;
wd33c93_regs *regp;
unsigned long flags;
uchar asr, sr, phs, id, lun, *ucp, msg;
unsigned long length;


   hostdata = (struct WD33C93_hostdata *)instance->hostdata;
   regp = hostdata->regp;

   asr = READ_AUX_STAT();
   if (!(asr & ASR_INT) || (asr & ASR_BSY))
      return;

/* OK - it should be safe to re-enable system interrupts */

   save_flags(flags);
   sti();

   cmd = (Scsi_Cmnd *)hostdata->connected;   /* assume we're connected */
   sr = read_wd33c93(regp, WD_SCSI_STATUS);  /* clear the interrupt */
   phs = read_wd33c93(regp, WD_COMMAND_PHASE);

DB(DB_INTR,printk("{%02x:%02x-",asr,sr))

/* After starting a DMA transfer, the next interrupt
 * is guaranteed to be in response to completion of
 * the transfer. Since the Amiga DMA hardware runs in
 * in an open-ended fashion, it needs to be told when
 * to stop; do that here if D_DMA_RUNNING is true.
 * Also, we have to update 'this_residual' and 'ptr'
 * based on the contents of the TRANSFER_COUNT register,
 * in case the device decided to do an intermediate
 * disconnect (a device may do this if it has to do a
 * seek, or just to be nice and let other devices have
 * some bus time during long transfers). After doing
 * whatever is needed, we go on and service the WD3393
 * interrupt normally.
 */

   if (hostdata->dma == D_DMA_RUNNING) {
DB(DB_TRANSFER,printk("[%p/%d:",cmd->SCp.ptr,cmd->SCp.this_residual))
      hostdata->dma_stop(cmd->host, cmd, 1);
      hostdata->dma = D_DMA_OFF;
      length = cmd->SCp.this_residual;
      cmd->SCp.this_residual = read_wd33c93_count(regp);
      cmd->SCp.ptr += (length - cmd->SCp.this_residual);
DB(DB_TRANSFER,printk("%p/%d]",cmd->SCp.ptr,cmd->SCp.this_residual))
      }

/* Respond to the specific WD3393 interrupt - there are quite a few! */

   switch (sr) {

      case CSR_TIMEOUT:
DB(DB_INTR,printk("TIMEOUT"))

         cli();
         if (hostdata->state == S_RUNNING_LEVEL2)
            hostdata->connected = NULL;
         else {
            cmd = (Scsi_Cmnd *)hostdata->selecting;   /* get a valid cmd */
            hostdata->selecting = NULL;
            }

         cmd->result = DID_NO_CONNECT << 16;
         hostdata->busy[cmd->target] &= ~(1 << cmd->lun);
         hostdata->state = S_UNCONNECTED;
         cmd->scsi_done(cmd);

/* We are not connected to a target - check to see if there
 * are commands waiting to be executed.
 */

         sti();
         wd33c93_execute(instance);
         break;


/* Note: this interrupt should not occur in a LEVEL2 command */

      case CSR_SELECT:
         cli();
DB(DB_INTR,printk("SELECT"))
         hostdata->connected = cmd = (Scsi_Cmnd *)hostdata->selecting;
         hostdata->selecting = NULL;

      /* construct an IDENTIFY message with correct disconnect bit */

         hostdata->outgoing_msg[0] = (0x80 | 0x00 | cmd->lun);
         if (cmd->SCp.phase)
            hostdata->outgoing_msg[0] |= 0x40;

         if (hostdata->sync_stat[cmd->target] == SS_FIRST) {
#ifdef SYNC_DEBUG
printk(" sending SDTR ");
#endif

            hostdata->sync_stat[cmd->target] = SS_WAITING;

      /* tack on a 2nd message to ask about synchronous transfers */

            hostdata->outgoing_msg[1] = EXTENDED_MESSAGE;
            hostdata->outgoing_msg[2] = 3;
            hostdata->outgoing_msg[3] = EXTENDED_SDTR;
            hostdata->outgoing_msg[4] = OPTIMUM_SX_PER/4;
            hostdata->outgoing_msg[5] = OPTIMUM_SX_OFF;
            hostdata->outgoing_len = 6;
            }
         else
            hostdata->outgoing_len = 1;

         hostdata->state = S_CONNECTED;
         break;


      case CSR_XFER_DONE|PHS_DATA_IN:
      case CSR_UNEXP    |PHS_DATA_IN:
      case CSR_SRV_REQ  |PHS_DATA_IN:
DB(DB_INTR,printk("IN-%d.%d",cmd->SCp.this_residual,cmd->SCp.buffers_residual))
         transfer_bytes(regp, cmd, DATA_IN_DIR);
         if (hostdata->state != S_RUNNING_LEVEL2)
            hostdata->state = S_CONNECTED;
         break;


      case CSR_XFER_DONE|PHS_DATA_OUT:
      case CSR_UNEXP    |PHS_DATA_OUT:
      case CSR_SRV_REQ  |PHS_DATA_OUT:
DB(DB_INTR,printk("OUT-%d.%d",cmd->SCp.this_residual,cmd->SCp.buffers_residual))
         transfer_bytes(regp, cmd, DATA_OUT_DIR);
         if (hostdata->state != S_RUNNING_LEVEL2)
            hostdata->state = S_CONNECTED;
         break;


/* Note: this interrupt should not occur in a LEVEL2 command */

      case CSR_XFER_DONE|PHS_COMMAND:
      case CSR_UNEXP    |PHS_COMMAND:
      case CSR_SRV_REQ  |PHS_COMMAND:
DB(DB_INTR,printk("CMND-%02x,%ld",cmd->cmnd[0],cmd->pid))
         transfer_pio(regp, cmd->cmnd, cmd->cmd_len, DATA_OUT_DIR, hostdata);
         hostdata->state = S_CONNECTED;
         break;


      case CSR_XFER_DONE|PHS_STATUS:
      case CSR_UNEXP    |PHS_STATUS:
      case CSR_SRV_REQ  |PHS_STATUS:
DB(DB_INTR,printk("STATUS"))

         cmd->SCp.Status = read_1_byte(regp);
         if (hostdata->level2 >= L2_BASIC) {
            sr = read_wd33c93(regp, WD_SCSI_STATUS);  /* clear interrupt */
            hostdata->state = S_RUNNING_LEVEL2;
            write_wd33c93(regp, WD_COMMAND_PHASE, 0x50);
            write_wd33c93_cmd(regp, WD_CMD_SEL_ATN_XFER);
            }
         else {
DB(DB_INTR,printk("=%02x",cmd->SCp.Status))
            hostdata->state = S_CONNECTED;
            }
         break;


      case CSR_XFER_DONE|PHS_MESS_IN:
      case CSR_UNEXP    |PHS_MESS_IN:
      case CSR_SRV_REQ  |PHS_MESS_IN:
DB(DB_INTR,printk("MSG_IN="))

         cli();
         msg = read_1_byte(regp);
         sr = read_wd33c93(regp, WD_SCSI_STATUS);  /* clear interrupt */

         hostdata->incoming_msg[hostdata->incoming_ptr] = msg;
         if (hostdata->incoming_msg[0] == EXTENDED_MESSAGE)
            msg = EXTENDED_MESSAGE;
         else
            hostdata->incoming_ptr = 0;

         cmd->SCp.Message = msg;
         switch (msg) {

            case COMMAND_COMPLETE:
DB(DB_INTR,printk("CCMP-%ld",cmd->pid))
               write_wd33c93_cmd(regp,WD_CMD_NEGATE_ACK);
               hostdata->state = S_PRE_CMP_DISC;
               break;

            case SAVE_POINTERS:
DB(DB_INTR,printk("SDP"))
               write_wd33c93_cmd(regp,WD_CMD_NEGATE_ACK);
               hostdata->state = S_CONNECTED;
               break;

            case RESTORE_POINTERS:
DB(DB_INTR,printk("RDP"))
               if (hostdata->level2 >= L2_BASIC) {
                  write_wd33c93(regp, WD_COMMAND_PHASE, 0x45);
                  write_wd33c93_cmd(regp, WD_CMD_SEL_ATN_XFER);
                  hostdata->state = S_RUNNING_LEVEL2;
                  }
               else {
                  write_wd33c93_cmd(regp, WD_CMD_NEGATE_ACK);
                  hostdata->state = S_CONNECTED;
                  }
               break;

            case DISCONNECT:
DB(DB_INTR,printk("DIS"))
               cmd->device->disconnect = 1;
               write_wd33c93_cmd(regp,WD_CMD_NEGATE_ACK);
               hostdata->state = S_PRE_TMP_DISC;
               break;

            case MESSAGE_REJECT:
DB(DB_INTR,printk("REJ"))
#ifdef SYNC_DEBUG
printk("-REJ-");
#endif
               if (hostdata->sync_stat[cmd->target] == SS_WAITING)
                  hostdata->sync_stat[cmd->target] = SS_SET;
               write_wd33c93_cmd(regp,WD_CMD_NEGATE_ACK);
               hostdata->state = S_CONNECTED;
               break;

            case EXTENDED_MESSAGE:
DB(DB_INTR,printk("EXT"))

               ucp = hostdata->incoming_msg;

#ifdef SYNC_DEBUG
printk("%02x",ucp[hostdata->incoming_ptr]);
#endif
         /* Is this the last byte of the extended message? */

               if ((hostdata->incoming_ptr >= 2) &&
                   (hostdata->incoming_ptr == (ucp[1] + 1))) {

                  switch (ucp[2]) {   /* what's the EXTENDED code? */
                     case EXTENDED_SDTR:
                        id = calc_sync_xfer(ucp[3],ucp[4]);
                        if (hostdata->sync_stat[cmd->target] != SS_WAITING) {

/* A device has sent an unsolicited SDTR message; rather than go
 * through the effort of decoding it and then figuring out what
 * our reply should be, we're just gonna say that we have a
 * synchronous fifo depth of 0. This will result in asynchronous
 * transfers - not ideal but so much easier.
 * Actually, this is OK because it assures us that if we don't
 * specifically ask for sync transfers, we won't do any.
 */

                           write_wd33c93_cmd(regp,WD_CMD_ASSERT_ATN); /* want MESS_OUT */
                           hostdata->outgoing_msg[0] = EXTENDED_MESSAGE;
                           hostdata->outgoing_msg[1] = 3;
                           hostdata->outgoing_msg[2] = EXTENDED_SDTR;
                           hostdata->outgoing_msg[3] = hostdata->default_sx_per/4;
                           hostdata->outgoing_msg[4] = 0;
                           hostdata->outgoing_len = 5;
                           hostdata->sync_xfer[cmd->target] =
                                       calc_sync_xfer(hostdata->default_sx_per/4,0);
                           }
                        else {
                           hostdata->sync_xfer[cmd->target] = id;
                           }
#ifdef SYNC_DEBUG
printk("sync_xfer=%02x",hostdata->sync_xfer[cmd->target]);
#endif
                        hostdata->sync_stat[cmd->target] = SS_SET;
                        write_wd33c93_cmd(regp,WD_CMD_NEGATE_ACK);
                        hostdata->state = S_CONNECTED;
                        break;
                     case EXTENDED_WDTR:
                        write_wd33c93_cmd(regp,WD_CMD_ASSERT_ATN); /* want MESS_OUT */
                        printk("sending WDTR ");
                        hostdata->outgoing_msg[0] = EXTENDED_MESSAGE;
                        hostdata->outgoing_msg[1] = 2;
                        hostdata->outgoing_msg[2] = EXTENDED_WDTR;
                        hostdata->outgoing_msg[3] = 0;   /* 8 bit transfer width */
                        hostdata->outgoing_len = 4;
                        write_wd33c93_cmd(regp,WD_CMD_NEGATE_ACK);
                        hostdata->state = S_CONNECTED;
                        break;
                     default:
                        write_wd33c93_cmd(regp,WD_CMD_ASSERT_ATN); /* want MESS_OUT */
                        printk("Rejecting Unknown Extended Message(%02x). ",ucp[2]);
                        hostdata->outgoing_msg[0] = MESSAGE_REJECT;
                        hostdata->outgoing_len = 1;
                        write_wd33c93_cmd(regp,WD_CMD_NEGATE_ACK);
                        hostdata->state = S_CONNECTED;
                        break;
                     }
                  hostdata->incoming_ptr = 0;
                  }

         /* We need to read more MESS_IN bytes for the extended message */

               else {
                  hostdata->incoming_ptr++;
                  write_wd33c93_cmd(regp,WD_CMD_NEGATE_ACK);
                  hostdata->state = S_CONNECTED;
                  }
               break;

            default:
               printk("Rejecting Unknown Message(%02x) ",msg);
               write_wd33c93_cmd(regp,WD_CMD_ASSERT_ATN); /* want MESS_OUT */
               hostdata->outgoing_msg[0] = MESSAGE_REJECT;
               hostdata->outgoing_len = 1;
               write_wd33c93_cmd(regp,WD_CMD_NEGATE_ACK);
               hostdata->state = S_CONNECTED;
            }
         break;


/* Note: this interrupt will occur only after a LEVEL2 command */

      case CSR_SEL_XFER_DONE:
         cli();

/* Make sure that reselection is enabled at this point - it may
 * have been turned off for the command that just completed.
 */

         write_wd33c93(regp,WD_SOURCE_ID, SRCID_ER);
         if (phs == 0x60) {
DB(DB_INTR,printk("SX-DONE-%ld",cmd->pid))
            cmd->SCp.Message = COMMAND_COMPLETE;
            lun = read_wd33c93(regp, WD_TARGET_LUN);
            if (cmd->SCp.Status == GOOD)
               cmd->SCp.Status = lun;
            hostdata->connected = NULL;
            if (cmd->cmnd[0] != REQUEST_SENSE)
               cmd->result = cmd->SCp.Status | (cmd->SCp.Message << 8);
            else if (cmd->SCp.Status != GOOD)
               cmd->result = (cmd->result & 0x00ffff) | (DID_ERROR << 16);
            hostdata->busy[cmd->target] &= ~(1 << cmd->lun);
            hostdata->state = S_UNCONNECTED;
            cmd->scsi_done(cmd);

/* We are no longer  connected to a target - check to see if
 * there are commands waiting to be executed.
 */

            wd33c93_execute(instance);
            }
         else {
            printk("%02x:%02x:%02x-%ld: Unknown SEL_XFER_DONE phase!!---",asr,sr,phs,cmd->pid);
            }
         break;


/* Note: this interrupt will occur only after a LEVEL2 command */

      case CSR_SDP:
DB(DB_INTR,printk("SDP"))
            hostdata->state = S_RUNNING_LEVEL2;
            write_wd33c93(regp, WD_COMMAND_PHASE, 0x41);
            write_wd33c93_cmd(regp, WD_CMD_SEL_ATN_XFER);
         break;


      case CSR_XFER_DONE|PHS_MESS_OUT:
      case CSR_UNEXP    |PHS_MESS_OUT:
      case CSR_SRV_REQ  |PHS_MESS_OUT:
DB(DB_INTR,printk("MSG_OUT="))

/* To get here, we've probably requested MESSAGE_OUT and have
 * already put the correct bytes in outgoing_msg[] and filled
 * in outgoing_len. We simply send them out to the SCSI bus.
 * Sometimes we get MESSAGE_OUT phase when we're not expecting
 * it - like when our SDTR message is rejected by a target. Some
 * targets send the REJECT before receiving all of the extended
 * message, and then seem to go back to MESSAGE_OUT for a byte
 * or two. Not sure why, or if I'm doing something wrong to
 * cause this to happen. Regardless, it seems that sending
 * NOP messages in these situations results in no harm and
 * makes everyone happy.
 */

         if (hostdata->outgoing_len == 0) {
            hostdata->outgoing_len = 1;
            hostdata->outgoing_msg[0] = NOP;
            }
         transfer_pio(regp, hostdata->outgoing_msg, hostdata->outgoing_len,
                            DATA_OUT_DIR, hostdata);
DB(DB_INTR,printk("%02x",hostdata->outgoing_msg[0]))
         hostdata->outgoing_len = 0;
         hostdata->state = S_CONNECTED;
         break;


      case CSR_UNEXP_DISC:

/* I think I've seen this after a request-sense that was in response
 * to an error condition, but not sure. We certainly need to do
 * something when we get this interrupt - the question is 'what?'.
 * Let's think positively, and assume some command has finished
 * in a legal manner (like a command that provokes a request-sense),
 * so we treat it as a normal command-complete-disconnect.
 */

         cli();

/* Make sure that reselection is enabled at this point - it may
 * have been turned off for the command that just completed.
 */

         write_wd33c93(regp,WD_SOURCE_ID, SRCID_ER);
         if (cmd == NULL) {
            printk(" - Already disconnected! ");
            hostdata->state = S_UNCONNECTED;
            return;
            }
DB(DB_INTR,printk("UNEXP_DISC-%ld",cmd->pid))
         hostdata->connected = NULL;
         hostdata->busy[cmd->target] &= ~(1 << cmd->lun);
         hostdata->state = S_UNCONNECTED;
         if (cmd->cmnd[0] != REQUEST_SENSE)
            cmd->result = cmd->SCp.Status | (cmd->SCp.Message << 8);
         else if (cmd->SCp.Status != GOOD)
            cmd->result = (cmd->result & 0x00ffff) | (DID_ERROR << 16);
         cmd->scsi_done(cmd);

/* We are no longer connected to a target - check to see if
 * there are commands waiting to be executed.
 */

         wd33c93_execute(instance);
         break;


      case CSR_DISC:
         cli();

/* Make sure that reselection is enabled at this point - it may
 * have been turned off for the command that just completed.
 */

         write_wd33c93(regp,WD_SOURCE_ID, SRCID_ER);
DB(DB_INTR,printk("DISC-%ld",cmd->pid))
         if (cmd == NULL) {
            printk(" - Already disconnected! ");
            hostdata->state = S_UNCONNECTED;
            }
         switch (hostdata->state) {
            case S_PRE_CMP_DISC:
               hostdata->connected = NULL;
               hostdata->busy[cmd->target] &= ~(1 << cmd->lun);
               hostdata->state = S_UNCONNECTED;
               if (cmd->cmnd[0] != REQUEST_SENSE)
                  cmd->result = cmd->SCp.Status | (cmd->SCp.Message << 8);
               else if (cmd->SCp.Status != GOOD)
                  cmd->result = (cmd->result & 0x00ffff) | (DID_ERROR << 16);
               cmd->scsi_done(cmd);
               break;
            case S_PRE_TMP_DISC:
            case S_RUNNING_LEVEL2:
               cmd->host_scribble = (uchar *)hostdata->disconnected_Q;
               hostdata->disconnected_Q = cmd;
               hostdata->connected = NULL;
               hostdata->state = S_UNCONNECTED;

#ifdef PROC_INTERFACE
               disc_taken_total++;
#endif

               break;
            default:
               printk("*** Unexpected DISCONNECT interrupt! ***");
               hostdata->state = S_UNCONNECTED;
            }

/* We are no longer connected to a target - check to see if
 * there are commands waiting to be executed.
 */

         wd33c93_execute(instance);
         break;


      case CSR_RESEL_AM:
DB(DB_INTR,printk("RESEL"))

         cli();

   /* First we have to make sure this reselection didn't */
   /* happen during Arbitration/Selection of some other device. */
   /* If yes, put losing command back on top of input_Q. */

         if (hostdata->level2 <= L2_NONE) {

            if (hostdata->selecting) {
               cmd = (Scsi_Cmnd *)hostdata->selecting;
               hostdata->selecting = NULL;
               hostdata->busy[cmd->target] &= ~(1 << cmd->lun);
               cmd->host_scribble = (uchar *)hostdata->input_Q;
               hostdata->input_Q = cmd;
               }
            }

         else {

            if (cmd) {
               if (phs == 0x00) {
                  hostdata->busy[cmd->target] &= ~(1 << cmd->lun);
                  cmd->host_scribble = (uchar *)hostdata->input_Q;
                  hostdata->input_Q = cmd;
                  }
               else {
                  printk("---%02x:%02x:%02x-TROUBLE: Intrusive ReSelect!---",asr,sr,phs);
                  while (1)
                     printk("\r");
                  }
               }

            }

   /* OK - find out which device reselected us. */

         id = read_wd33c93(regp, WD_SOURCE_ID);
         id &= SRCID_MASK;

   /* and extract the lun from the ID message. (Note that we don't
    * bother to check for a valid message here - I guess this is
    * not the right way to go, but...)
    */

         lun = read_wd33c93(regp, WD_DATA);
         if (hostdata->level2 < L2_RESELECT)
            write_wd33c93_cmd(regp,WD_CMD_NEGATE_ACK);
         lun &= 7;

   /* Now we look for the command that's reconnecting. */

         cmd = (Scsi_Cmnd *)hostdata->disconnected_Q;
         patch = NULL;
         while (cmd) {
            if (id == cmd->target && lun == cmd->lun)
               break;
            patch = cmd;
            cmd = (Scsi_Cmnd *)cmd->host_scribble;
            }

   /* Hmm. Couldn't find a valid command.... What to do? */

         if (!cmd) {
            printk("---TROUBLE: target %d.%d not in disconnect queue---",id,lun);
            return;
            }

   /* Ok, found the command - now start it up again. */

         if (patch)
            patch->host_scribble = cmd->host_scribble;
         else
            hostdata->disconnected_Q = (Scsi_Cmnd *)cmd->host_scribble;
         hostdata->connected = cmd;

   /* We don't need to worry about 'initialize_SCp()' or 'hostdata->busy[]'
    * because these things are preserved over a disconnect.
    * But we DO need to fix the DPD bit so it's correct for this command.
    */

         if (IS_DIR_OUT(cmd))
            write_wd33c93(regp, WD_DESTINATION_ID, cmd->target);
         else
            write_wd33c93(regp, WD_DESTINATION_ID, cmd->target | DSTID_DPD);
         if (hostdata->level2 >= L2_RESELECT) {
            write_wd33c93_count(regp, 0);  /* we want a DATA_PHASE interrupt */
            write_wd33c93(regp, WD_COMMAND_PHASE, 0x45);
            write_wd33c93_cmd(regp, WD_CMD_SEL_ATN_XFER);
            hostdata->state = S_RUNNING_LEVEL2;
            }
         else
            hostdata->state = S_CONNECTED;

DB(DB_INTR,printk("-%ld",cmd->pid))
         break;
         
      default:
         printk("--UNKNOWN INTERRUPT:%02x:%02x:%02x--",asr,sr,phs);
      }

   restore_flags(flags);

DB(DB_INTR,printk("} "))

}



void reset_wd33c93(struct Scsi_Host *instance)
{
struct WD33C93_hostdata *hostdata;
wd33c93_regs *regp;
uchar sr;

   hostdata = (struct WD33C93_hostdata *)instance->hostdata;
   regp = hostdata->regp;

   write_wd33c93(regp, WD_OWN_ID, OWNID_EAF | OWNID_RAF |
                 instance->this_id | hostdata->clock_freq);
   write_wd33c93(regp, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
   write_wd33c93(regp, WD_SYNCHRONOUS_TRANSFER,
                 calc_sync_xfer(hostdata->default_sx_per/4,DEFAULT_SX_OFF));
   write_wd33c93(regp, WD_COMMAND, WD_CMD_RESET);

   while (!(READ_AUX_STAT() & ASR_INT))
      ;
   sr = read_wd33c93(regp, WD_SCSI_STATUS);

   hostdata->microcode = read_wd33c93(regp, WD_CDB_1);
   if (sr == 0x00)
      hostdata->chip = C_WD33C93;
   else if (sr == 0x01) {
      write_wd33c93(regp, WD_QUEUE_TAG, 0xa5);  /* any random number */
      sr = read_wd33c93(regp, WD_QUEUE_TAG);
      if (sr == 0xa5) {
         hostdata->chip = C_WD33C93B;
         write_wd33c93(regp, WD_QUEUE_TAG, 0);
         }
      else
         hostdata->chip = C_WD33C93A;
      }
   else
      hostdata->chip = C_UNKNOWN_CHIP;

   write_wd33c93(regp, WD_TIMEOUT_PERIOD, TIMEOUT_PERIOD_VALUE);
   write_wd33c93(regp, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
}



#if LINUX_VERSION_CODE >= 0x010300
int wd33c93_reset(Scsi_Cmnd *SCpnt, unsigned int reset_flags)
#else
int wd33c93_reset(Scsi_Cmnd *SCpnt)
#endif
{
unsigned long flags;
struct Scsi_Host *instance;
struct WD33C93_hostdata *hostdata;
int i;

   instance = SCpnt->host;
   hostdata = (struct WD33C93_hostdata *)instance->hostdata;

   printk("scsi%d: reset. ", instance->host_no);
   save_flags(flags);
   cli();

   ((struct WD33C93_hostdata *)instance->hostdata)->dma_stop(instance,NULL,0);
   for (i = 0; i < 8; i++) {
      hostdata->busy[i] = 0;
      hostdata->sync_xfer[i] = calc_sync_xfer(DEFAULT_SX_PER/4,DEFAULT_SX_OFF);
      hostdata->sync_stat[i] = SS_UNSET;  /* using default sync values */
      }
   hostdata->input_Q = NULL;
   hostdata->selecting = NULL;
   hostdata->connected = NULL;
   hostdata->disconnected_Q = NULL;
   hostdata->state = S_UNCONNECTED;
   hostdata->dma = D_DMA_OFF;
   hostdata->incoming_ptr = 0;
   hostdata->outgoing_len = 0;

   reset_wd33c93(instance);
   SCpnt->result = DID_RESET << 16;
   restore_flags(flags);
   return 0;
}



int wd33c93_abort (Scsi_Cmnd *cmd)
{
struct Scsi_Host *instance;
struct WD33C93_hostdata *hostdata;
wd33c93_regs *regp;
Scsi_Cmnd *tmp, *prev;
unsigned long flags;

   save_flags (flags);
   cli();

   instance = cmd->host;
   hostdata = (struct WD33C93_hostdata *)instance->hostdata;
   regp = hostdata->regp;

/*
 * Case 1 : If the command hasn't been issued yet, we simply remove it
 *     from the input_Q.
 */

   tmp = (Scsi_Cmnd *)hostdata->input_Q;
   prev = 0;
   while (tmp) {
      if (tmp == cmd) {
         if (prev)
            prev->host_scribble = cmd->host_scribble;
         cmd->host_scribble = NULL;
         cmd->result = DID_ABORT << 16;
         printk("scsi%d: Abort - removing command %ld from input_Q. ",
           instance->host_no, cmd->pid);
         cmd->scsi_done(cmd);
         restore_flags(flags);
         return SCSI_ABORT_SUCCESS;
         }
      prev = tmp;
      tmp = (Scsi_Cmnd *)tmp->host_scribble;
      }

/*
 * Case 2 : If the command is connected, we're going to fail the abort
 *     and let the high level SCSI driver retry at a later time or
 *     issue a reset.
 *
 *     Timeouts, and therefore aborted commands, will be highly unlikely
 *     and handling them cleanly in this situation would make the common
 *     case of noresets less efficient, and would pollute our code.  So,
 *     we fail.
 */

   if (hostdata->connected == cmd) {
      uchar sr, asr;
      unsigned long timeout;

      printk("scsi%d: Aborting connected command %ld - ",
              instance->host_no, cmd->pid);

      printk("stopping DMA - ");
      if (hostdata->dma == D_DMA_RUNNING) {
         hostdata->dma_stop(instance, cmd, 0);
         hostdata->dma = D_DMA_OFF;
         }

      printk("sending wd33c93 ABORT command - ");
      write_wd33c93(regp, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
      write_wd33c93_cmd(regp, WD_CMD_ABORT);

/* Now we have to attempt to flush out the FIFO... */

      printk("flushing fifo - ");
      timeout = 1000000;
      do {
         asr = READ_AUX_STAT();
         if (asr & ASR_DBR)
            read_wd33c93(regp, WD_DATA);
         } while (!(asr & ASR_INT) && timeout-- > 0);
      sr = read_wd33c93(regp, WD_SCSI_STATUS);
      printk("asr=%02x, sr=%02x, %ld bytes un-transferred (timeout=%ld) - ",
             asr, sr, read_wd33c93_count(regp), timeout);

   /*
    * Abort command processed.
    * Still connected.
    * We must disconnect.
    */

      printk("sending wd33c93 DISCONNECT command - ");
      write_wd33c93_cmd(regp, WD_CMD_DISCONNECT);

      timeout = 1000000;
      asr = READ_AUX_STAT();
      while ((asr & ASR_CIP) && timeout-- > 0)
         asr = READ_AUX_STAT();
      sr = read_wd33c93(regp, WD_SCSI_STATUS);
      printk("asr=%02x, sr=%02x.",asr,sr);

      hostdata->busy[cmd->target] &= ~(1 << cmd->lun);
      hostdata->connected = NULL;
      hostdata->state = S_UNCONNECTED;
      cmd->result = DID_ABORT << 16;
      cmd->scsi_done(cmd);

/*      sti();*/
      wd33c93_execute (instance);

      restore_flags(flags);
      return SCSI_ABORT_SUCCESS;
      }

/*
 * Case 3: If the command is currently disconnected from the bus,
 * we're not going to expend much effort here: Let's just return
 * an ABORT_SNOOZE and hope for the best...
 */

   tmp = (Scsi_Cmnd *)hostdata->disconnected_Q;
   while (tmp) {
      if (tmp == cmd) {
         printk("scsi%d: Abort - command %ld found on disconnected_Q - ",
                 instance->host_no, cmd->pid);
         printk("returning ABORT_SNOOZE. ");
         restore_flags(flags);
         return SCSI_ABORT_SNOOZE;
         }
      tmp = (Scsi_Cmnd *)tmp->host_scribble;
      }

/*
 * Case 4 : If we reached this point, the command was not found in any of
 *     the queues.
 *
 * We probably reached this point because of an unlikely race condition
 * between the command completing successfully and the abortion code,
 * so we won't panic, but we will notify the user in case something really
 * broke.
 */

/*   sti();*/
   wd33c93_execute (instance);

   restore_flags(flags);
   printk("scsi%d: warning : SCSI command probably completed successfully"
      "         before abortion. ", instance->host_no);
   return SCSI_ABORT_NOT_RUNNING;
}



#define MAX_WD33C93_HOSTS 4
#define MAX_SETUP_STRINGS (sizeof(setup_strings) / sizeof(char *))
#define SETUP_BUFFER_SIZE 200
static char setup_buffer[SETUP_BUFFER_SIZE];
static char setup_used[MAX_SETUP_STRINGS];

void wd33c93_setup (char *str, int *ints)
{
int i,x;
char *p1,*p2;

   /* The kernel does some processing of the command-line before calling
    * this function: If it begins with any decimal or hex number arguments,
    * ints[0] = how many numbers found and ints[1] through [n] are the values
    * themselves. str points to where the non-numeric arguments (if any)
    * start: We do our own parsing of those. We construct synthetic 'nosync'
    * keywords out of numeric args (to maintain compatibility with older
    * versions) and then add the rest of the arguments.
    */

   p1 = setup_buffer;
   *p1 = '\0';
   if (ints[0]) {
      for (i=0; i<ints[0]; i++) {
         x = vsprintf(p1,"nosync:0x%02x,",&(ints[i+1]));
         p1 += x;
         }
      }
   if (str)
      strncpy(p1, str, SETUP_BUFFER_SIZE - strlen(setup_buffer));
   setup_buffer[SETUP_BUFFER_SIZE - 1] = '\0';
   p1 = setup_buffer;
   i = 0;
   while (*p1 && (i < MAX_SETUP_STRINGS)) {
      p2 = strchr(p1, ',');
      if (p2) {
         *p2 = '\0';
         if (p1 != p2)
            setup_strings[i] = p1;
         p1 = p2 + 1;
         i++;
         }
      else {
         setup_strings[i] = p1;
         break;
         }
      }
   for (i=0; i<MAX_SETUP_STRINGS; i++)
      setup_used[i] = 0;
}


/* check_setup_strings() returns index if key found, 0 if not
 */

int check_setup_strings(char *key, int *flags, int *val, char *buf)
{
int x;
char *cp;

   for  (x=0; x<MAX_SETUP_STRINGS; x++) {
      if (setup_used[x])
         continue;
      if (!strncmp(setup_strings[x], key, strlen(key)))
         break;
      if (!strncmp(setup_strings[x], "next", strlen("next")))
         return 0;
      }
   if (x == MAX_SETUP_STRINGS)
      return 0;
   setup_used[x] = 1;
   cp = setup_strings[x] + strlen(key);
   *val = -1;
   if (*cp != ':')
      return ++x;
   cp++;
   if ((*cp >= '0') && (*cp <= '9')) {
      *val = simple_strtoul(cp,NULL,0);
      }
   return ++x;
}



void wd33c93_init (struct Scsi_Host *instance, wd33c93_regs *regs,
         dma_setup_t setup, dma_stop_t stop, int clock_freq)
{
struct WD33C93_hostdata *hostdata;
int i;
int flags;
int val;
char buf[32];

   hostdata = (struct WD33C93_hostdata *)instance->hostdata;

   hostdata->regp = regs;
   hostdata->clock_freq = clock_freq;
   hostdata->dma_setup = setup;
   hostdata->dma_stop = stop;
   hostdata->dma_bounce_buffer = NULL;
   hostdata->dma_bounce_len = 0;
   for (i = 0; i < 8; i++) {
      hostdata->busy[i] = 0;
      hostdata->sync_xfer[i] = calc_sync_xfer(DEFAULT_SX_PER/4,DEFAULT_SX_OFF);
      hostdata->sync_stat[i] = SS_UNSET;  /* using default sync values */
      }
   hostdata->input_Q = NULL;
   hostdata->selecting = NULL;
   hostdata->connected = NULL;
   hostdata->disconnected_Q = NULL;
   hostdata->state = S_UNCONNECTED;
   hostdata->dma = D_DMA_OFF;
   hostdata->level2 = L2_BASIC;
   hostdata->disconnect = DIS_ADAPTIVE;
   hostdata->args = DEBUG_DEFAULTS;
   hostdata->incoming_ptr = 0;
   hostdata->outgoing_len = 0;
   hostdata->default_sx_per = DEFAULT_SX_PER;
   hostdata->no_sync = 0xff;     /* sync defaults to off */

#ifdef PROC_INTERFACE
   hostdata->proc = PR_VERSION|PR_INFO|PR_TOTALS|
                    PR_CONNECTED|PR_INPUTQ|PR_DISCQ|
                    PR_STOP;

   disc_allowed_total = 0;
   disc_taken_total = 0;
#endif


   if (check_setup_strings("nosync",&flags,&val,buf))
      hostdata->no_sync = val;

   if (check_setup_strings("period",&flags,&val,buf))
      hostdata->default_sx_per = sx_table[round_period((unsigned int)val)].period_ns;

   if (check_setup_strings("disconnect",&flags,&val,buf)) {
      if ((val >= DIS_NEVER) && (val <= DIS_ALWAYS))
         hostdata->disconnect = val;
      else
         hostdata->disconnect = DIS_ADAPTIVE;
      }

   if (check_setup_strings("debug",&flags,&val,buf))
      hostdata->args = val & DB_MASK;

   if (check_setup_strings("clock",&flags,&val,buf)) {
      if (val>7 && val<11)
         val = WD33C93_FS_8_10;
      else if (val>11 && val<16)
         val = WD33C93_FS_12_15;
      else if (val>15 && val<21)
         val = WD33C93_FS_16_20;
      else
         val = WD33C93_FS_8_10;
      hostdata->clock_freq = val;
      }

   if ((i = check_setup_strings("next",&flags,&val,buf))) {
      while (i)
         setup_used[--i] = 1;
      }

#ifdef PROC_INTERFACE
   if (check_setup_strings("proc",&flags,&val,buf))
      hostdata->proc = val;
#endif


   cli();
   reset_wd33c93(instance);
   sti();

   printk("wd33c93-%d: chip=%s microcode=%02x\n",instance->host_no,
         (hostdata->chip==C_WD33C93)?"WD33c93":
         (hostdata->chip==C_WD33C93A)?"WD33c93A":
         (hostdata->chip==C_WD33C93B)?"WD33c93B":"unknown",
         hostdata->microcode);

#ifdef DEBUGGING_ON
   printk("wd33c93-%d: setup_strings=",instance->host_no);
   for (i=0; i<MAX_SETUP_STRINGS; i++)
      printk("%s,",setup_strings[i]);
   printk("\n");
   printk("wd33c93-%d: debug_flags = %04x\n",instance->host_no,hostdata->args);
#endif
   printk("wd33c93-%d: driver version %s - %s\n",instance->host_no,
                     WD33C93_VERSION,WD33C93_DATE);
   printk("wd33c93-%d: compiled on %s at %s\n",instance->host_no,
                     __DATE__,__TIME__);
}


int wd33c93_proc_info(char *buf, char **start, off_t off, int len, int hn, int in)
{

#ifdef PROC_INTERFACE

char *bp;
char tbuf[128];
unsigned long flags;
struct Scsi_Host *instance;
struct WD33C93_hostdata *hd;
Scsi_Cmnd *cmd;
int x,i;
static int stop = 0;

   for (instance=instance_list; instance; instance=instance->next) {
      if (instance->host_no == hn)
         break;
      }
   if (!instance) {
      printk("*** Hmm... Can't find host #%d!\n",hn);
      return (-ESRCH);
      }
   hd = (struct WD33C93_hostdata *)instance->hostdata;

/* If 'in' is TRUE we need to _read_ the proc file. We accept the following
 * keywords (same format as command-line, but only ONE per read):
 *    debug
 *    disconnect
 *    period
 *    resync
 *    proc
 */

   if (in) {
      buf[len] = '\0';
      bp = buf;
      if (!strncmp(bp,"debug:",6)) {
         bp += 6;
         hd->args = simple_strtoul(bp,NULL,0) & DB_MASK;
         }
      else if (!strncmp(bp,"disconnect:",11)) {
         bp += 11;
         x = simple_strtoul(bp,NULL,0);
         if (x < DIS_NEVER || x > DIS_ALWAYS)
            x = DIS_ADAPTIVE;
         hd->disconnect = x;
         }
      else if (!strncmp(bp,"period:",7)) {
         bp += 7;
         x = simple_strtoul(bp,NULL,0);
         hd->default_sx_per = sx_table[round_period((unsigned int)x)].period_ns;
         }
      else if (!strncmp(bp,"resync:",7)) {
         bp += 7;
         x = simple_strtoul(bp,NULL,0);
         for (i=0; i<7; i++)
            if (x & (1<<i))
               hd->sync_stat[i] = SS_UNSET;
         }
      else if (!strncmp(bp,"proc:",5)) {
         bp += 5;
         hd->proc = simple_strtoul(bp,NULL,0);
         }
      return len;
      }

   save_flags(flags);
   cli();
   bp = buf;
   *bp = '\0';
   if (hd->proc & PR_VERSION) {
      sprintf(tbuf,"\nVersion %s - %s. Compiled %s %s",
            WD33C93_VERSION,WD33C93_DATE,__DATE__,__TIME__);
      strcat(bp,tbuf);
      }
   if (hd->proc & PR_INFO) {
      ;
      }
   if (hd->proc & PR_TOTALS) {
      sprintf(tbuf,"\n%ld disc_allowed, %ld disc_taken",
            disc_allowed_total,disc_taken_total);
      strcat(bp,tbuf);
      }
   if (hd->proc & PR_CONNECTED) {
      strcat(bp,"\nconnected:     ");
      if (hd->connected) {
         cmd = (Scsi_Cmnd *)hd->connected;
         sprintf(tbuf," %ld-%d:%d(%02x)",
               cmd->pid, cmd->target, cmd->lun, cmd->cmnd[0]);
         strcat(bp,tbuf);
         }
      }
   if (hd->proc & PR_INPUTQ) {
      strcat(bp,"\ninput_Q:       ");
      cmd = (Scsi_Cmnd *)hd->input_Q;
      while (cmd) {
         sprintf(tbuf," %ld-%d:%d(%02x)",
               cmd->pid, cmd->target, cmd->lun, cmd->cmnd[0]);
         strcat(bp,tbuf);
         cmd = (Scsi_Cmnd *)cmd->host_scribble;
         }
      }
   if (hd->proc & PR_DISCQ) {
      strcat(bp,"\ndisconnected_Q:");
      cmd = (Scsi_Cmnd *)hd->disconnected_Q;
      while (cmd) {
         sprintf(tbuf," %ld-%d:%d(%02x)",
               cmd->pid, cmd->target, cmd->lun, cmd->cmnd[0]);
         strcat(bp,tbuf);
         cmd = (Scsi_Cmnd *)cmd->host_scribble;
         }
      }
   strcat(bp,"\n");
   restore_flags(flags);
   *start = buf;
   if (stop) {
      stop = 0;
      return 0;
      }
   if (off > 0x40000)   /* ALWAYS stop after 256k bytes have been read */
      stop = 1;;
   if (hd->proc & PR_STOP)    /* stop every other time */
      stop = 1;
   return strlen(bp);

#else    /* PROC_INTERFACE */

   return 0;

#endif   /* PROC_INTERFACE */

}


#ifdef MODULE

Scsi_Host_Template driver_template = WD33C93;

#include "scsi_module.c"

#endif

