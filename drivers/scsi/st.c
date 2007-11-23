/*
  SCSI Tape Driver for Linux version 1.1 and newer. See the accompanying
  file README.st for more information.

  History:
  Rewritten from Dwayne Forsyth's SCSI tape driver by Kai Makisara.
  Contribution and ideas from several people including (in alphabetical
  order) Klaus Ehrenfried, Wolfgang Denk, J"org Weule, and
  Eric Youngdale.

  Copyright 1992, 1993, 1994 Kai Makisara
		 email makisara@vtinsx.ins.vtt.fi or Kai.Makisara@vtt.fi

  Last modified: Wed May  4 20:29:42 1994 by root@kai.home
*/

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mtio.h>
#include <linux/ioctl.h>
#include <linux/fcntl.h>
#include <asm/segment.h>
#include <asm/system.h>

#define MAJOR_NR SCSI_TAPE_MAJOR
#include "../block/blk.h"
#include "scsi.h"
#include "scsi_ioctl.h"
#include "st.h"
#include "constants.h"

/* #define DEBUG */

/* #define ST_NOWAIT */

/* #define ST_IN_FILE_POS */

/* #define ST_RECOVERED_WRITE_FATAL */

#define ST_BUFFER_WRITES 1

#define ST_ASYNC_WRITES 1

#define ST_READ_AHEAD 1

#define ST_BLOCK_SIZE 1024

#define ST_MAX_BUFFERS 2

#define ST_BUFFER_BLOCKS 32

#define ST_WRITE_THRESHOLD_BLOCKS 30

#define ST_BUFFER_SIZE (ST_BUFFER_BLOCKS * ST_BLOCK_SIZE)
#define ST_WRITE_THRESHOLD (ST_WRITE_THRESHOLD_BLOCKS * ST_BLOCK_SIZE)

/* The buffer size should fit into the 24 bits for length in the
   6-byte SCSI read and write commands. */
#if ST_BUFFER_SIZE >= (2 << 24 - 1)
#error "Buffer size should not exceed (2 << 24 - 1) bytes!"
#endif

#ifdef DEBUG
static int debugging = 1;
#endif

#define MAX_RETRIES 0
#define MAX_READY_RETRIES 5
#define NO_TAPE  NOT_READY

#define ST_TIMEOUT 9000
#define ST_LONG_TIMEOUT 200000

static int st_nbr_buffers;
static ST_buffer **st_buffers;
static int st_buffer_size = ST_BUFFER_SIZE;
static int st_write_threshold = ST_WRITE_THRESHOLD;
static int st_max_buffers = ST_MAX_BUFFERS;

static Scsi_Tape * scsi_tapes;
int NR_ST=0;
int MAX_ST=0;

static int st_int_ioctl(struct inode * inode,struct file * file,
	     unsigned int cmd_in, unsigned long arg);




/* Convert the result to success code */
	static int
st_chk_result(Scsi_Cmnd * SCpnt)
{
  int dev = SCpnt->request.dev;
  int result = SCpnt->result;
  unsigned char * sense = SCpnt->sense_buffer;
  char *stp;

  if (!result && SCpnt->sense_buffer[0] == 0)
    return 0;
#ifdef DEBUG
  if (debugging) {
    printk("st%d: Error: %x, cmd: %x %x %x %x %x %x Len: %d\n", dev, result,
	   SCpnt->cmnd[0], SCpnt->cmnd[1], SCpnt->cmnd[2],
	   SCpnt->cmnd[3], SCpnt->cmnd[4], SCpnt->cmnd[5],
	   SCpnt->request_bufflen);
    if (driver_byte(result) & DRIVER_SENSE)
      print_sense("st", SCpnt);
  }
#endif
/*  if ((sense[0] & 0x70) == 0x70 &&
       ((sense[2] & 0x80) ))
    return 0; */
  if ((sense[0] & 0x70) == 0x70 &&
      sense[2] == RECOVERED_ERROR
#ifdef ST_RECOVERED_WRITE_FATAL
      && SCpnt->cmnd[0] != WRITE_6
      && SCpnt->cmnd[0] != WRITE_FILEMARKS
#endif
      ) {
    scsi_tapes[dev].recover_count++;
    scsi_tapes[dev].mt_status->mt_erreg += (1 << MT_ST_SOFTERR_SHIFT);
    if (SCpnt->cmnd[0] == READ_6)
      stp = "read";
    else if (SCpnt->cmnd[0] == WRITE_6)
      stp = "write";
    else
      stp = "ioctl";
    printk("st%d: Recovered %s error (%d).\n", dev, stp,
	   scsi_tapes[dev].recover_count);
    return 0;
  }
  return (-EIO);
}


/* Wakeup from interrupt */
	static void
st_sleep_done (Scsi_Cmnd * SCpnt)
{
  int st_nbr, remainder;
  Scsi_Tape * STp;

  if ((st_nbr = SCpnt->request.dev) < NR_ST && st_nbr >= 0) {
    STp = &(scsi_tapes[st_nbr]);
    if ((STp->buffer)->writing &&
	(SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	(SCpnt->sense_buffer[2] & 0x40)) {
      /* EOM at write-behind, has all been written? */
      if ((SCpnt->sense_buffer[0] & 0x80) != 0)
	remainder = (SCpnt->sense_buffer[3] << 24) |
	      (SCpnt->sense_buffer[4] << 16) |
		(SCpnt->sense_buffer[5] << 8) | SCpnt->sense_buffer[6];
      else
	remainder = 0;
      if ((SCpnt->sense_buffer[2] & 0x0f) == VOLUME_OVERFLOW ||
	  remainder > 0)
	(STp->buffer)->last_result = SCpnt->result; /* Error */
      else
	(STp->buffer)->last_result = INT_MAX; /* OK */
    }
    else
      (STp->buffer)->last_result = SCpnt->result;
    (STp->buffer)->last_result_fatal = st_chk_result(SCpnt);
    if ((STp->buffer)->writing)
      SCpnt->request.dev = -1;
    else
      SCpnt->request.dev = 0xffff;
    if ((STp->buffer)->writing <= 0)
      wake_up( &(STp->waiting) );
  }
#ifdef DEBUG
  else if (debugging)
    printk("st?: Illegal interrupt device %x\n", st_nbr);
#endif
}


/* Handle the write-behind checking */
	static void
write_behind_check(int dev)
{
  Scsi_Tape * STp;
  ST_buffer * STbuffer;

  STp = &(scsi_tapes[dev]);
  STbuffer = STp->buffer;

  cli();
  if (STbuffer->last_result < 0) {
    STbuffer->writing = (- STbuffer->writing);
    sleep_on( &(STp->waiting) );
    STbuffer->writing = (- STbuffer->writing);
  }
  sti();

  if (STbuffer->writing < STbuffer->buffer_bytes)
    memcpy(STbuffer->b_data,
	   STbuffer->b_data + STbuffer->writing,
	   STbuffer->buffer_bytes - STbuffer->writing);
  STbuffer->buffer_bytes -= STbuffer->writing;
  if (STp->drv_block >= 0) {
    if (STp->block_size == 0)
      STp->drv_block++;
    else
      STp->drv_block += STbuffer->writing / STp->block_size;
  }
  STbuffer->writing = 0;

  return;
}


/* Back over EOF if it has been inadvertently crossed (ioctl not used because
   it messes up the block number). */
	static int
back_over_eof(int dev)
{
  Scsi_Cmnd *SCpnt;
  Scsi_Tape *STp = &(scsi_tapes[dev]);
  unsigned char cmd[10];

  cmd[0] = SPACE;
  cmd[1] = 0x01; /* Space FileMarks */
  cmd[2] = cmd[3] = cmd[4] = 0xff;  /* -1 filemarks */
  cmd[5] = 0;

  SCpnt = allocate_device(NULL, (STp->device)->index, 1);
  SCpnt->sense_buffer[0] = 0;
  SCpnt->request.dev = dev;
  scsi_do_cmd(SCpnt,
	      (void *) cmd, (void *) (STp->buffer)->b_data, 0,
	      st_sleep_done, ST_TIMEOUT, MAX_RETRIES);

  if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );
  SCpnt->request.dev = -1;

  return (STp->buffer)->last_result_fatal;
}


/* Flush the write buffer (never need to write if variable blocksize). */
	static int
flush_write_buffer(int dev)
{
  int offset, transfer, blks;
  int result;
  unsigned char cmd[10];
  Scsi_Cmnd *SCpnt;
  Scsi_Tape *STp = &(scsi_tapes[dev]);

  if ((STp->buffer)->writing) {
    write_behind_check(dev);
    if ((STp->buffer)->last_result_fatal) {
#ifdef DEBUG
      if (debugging)
	printk("st%d: Async write error (flush) %x.\n", dev,
	       (STp->buffer)->last_result);
#endif
      if ((STp->buffer)->last_result == INT_MAX)
	return (-ENOSPC);
      return (-EIO);
    }
  }

  result = 0;
  if (STp->dirty == 1) {
    SCpnt = allocate_device(NULL, (STp->device)->index, 1);

    offset = (STp->buffer)->buffer_bytes;
    transfer = ((offset + STp->block_size - 1) /
		STp->block_size) * STp->block_size;
#ifdef DEBUG
    if (debugging)
      printk("st%d: Flushing %d bytes.\n", dev, transfer);
#endif
    memset((STp->buffer)->b_data + offset, 0, transfer - offset);

    SCpnt->sense_buffer[0] = 0;
    memset(cmd, 0, 10);
    cmd[0] = WRITE_6;
    cmd[1] = 1;
    blks = transfer / STp->block_size;
    cmd[2] = blks >> 16;
    cmd[3] = blks >> 8;
    cmd[4] = blks;
    SCpnt->request.dev = dev;
    scsi_do_cmd (SCpnt,
		 (void *) cmd, (STp->buffer)->b_data, transfer,
		 st_sleep_done, ST_TIMEOUT, MAX_RETRIES);

    if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );

    if ((STp->buffer)->last_result_fatal != 0) {
      printk("st%d: Error on flush.\n", dev);
      if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	  (SCpnt->sense_buffer[2] & 0x40) &&
	  (SCpnt->sense_buffer[2] & 0x0f) != VOLUME_OVERFLOW) {
	STp->dirty = 0;
	(STp->buffer)->buffer_bytes = 0;
	result = (-ENOSPC);
      }
      else
	result = (-EIO);
      STp->drv_block = (-1);
    }
    else {
      if (STp->drv_block >= 0)
	STp->drv_block += blks;
      STp->dirty = 0;
      (STp->buffer)->buffer_bytes = 0;
    }
    SCpnt->request.dev = -1;  /* Mark as not busy */
  }
  return result;
}


/* Flush the tape buffer. The tape will be positioned correctly unless
   seek_next is true. */
	static int
flush_buffer(struct inode * inode, struct file * filp, int seek_next)
{
  int dev;
  int backspace, result;
  Scsi_Tape * STp;
  ST_buffer * STbuffer;

  dev = MINOR(inode->i_rdev) & 127;
  STp = &(scsi_tapes[dev]);
  STbuffer = STp->buffer;

  if (STp->rw == ST_WRITING)  /* Writing */
    return flush_write_buffer(dev);

  if (STp->block_size == 0)
    return 0;

  backspace = ((STp->buffer)->buffer_bytes +
    (STp->buffer)->read_pointer) / STp->block_size -
      ((STp->buffer)->read_pointer + STp->block_size - 1) /
	STp->block_size;
  (STp->buffer)->buffer_bytes = 0;
  (STp->buffer)->read_pointer = 0;
  result = 0;
  if (!seek_next) {
    if ((STp->eof == ST_FM) && !STp->eof_hit) {
      result = back_over_eof(dev); /* Back over the EOF hit */
      if (!result) {
	STp->eof = ST_NOEOF;
	STp->eof_hit = 0;
      }
    }
    if (!result && backspace > 0)
      result = st_int_ioctl(inode, filp, MTBSR, backspace);
  }
  return result;

}


/* Open the device */
	static int
scsi_tape_open(struct inode * inode, struct file * filp)
{
    int dev;
    unsigned short flags;
    int i;
    unsigned char cmd[10];
    Scsi_Cmnd * SCpnt;
    Scsi_Tape * STp;

    dev = MINOR(inode->i_rdev) & 127;
    if (dev >= NR_ST)
      return (-ENXIO);
    STp = &(scsi_tapes[dev]);
    if (STp->in_use) {
      printk("st%d: Device already in use.\n", dev);
      return (-EBUSY);
    }

    /* Allocate buffer for this user */
    for (i=0; i < st_nbr_buffers; i++)
      if (!st_buffers[i]->in_use)
	break;
    if (i >= st_nbr_buffers) {
      printk("st%d: No free buffers.\n", dev);
      return (-EBUSY);
    }
    STp->buffer = st_buffers[i];
    (STp->buffer)->in_use = 1;
    (STp->buffer)->writing = 0;
    STp->in_use = 1;

    flags = filp->f_flags;
    STp->write_prot = ((flags & O_ACCMODE) == O_RDONLY);

    STp->dirty = 0;
    STp->rw = ST_IDLE;
    STp->eof = ST_NOEOF;
    STp->eof_hit = 0;
    STp->recover_count = 0;

    SCpnt = allocate_device(NULL, (STp->device)->index, 1);
    if (!SCpnt) {
      printk("st%d: Tape request not allocated", dev);
      return (-EBUSY);
    }

    SCpnt->sense_buffer[0]=0;
    memset ((void *) &cmd[0], 0, 10);
    cmd[0] = TEST_UNIT_READY;
    SCpnt->request.dev = dev;
    scsi_do_cmd(SCpnt,
                (void *) cmd, (void *) (STp->buffer)->b_data,
                0, st_sleep_done, ST_LONG_TIMEOUT,
		MAX_READY_RETRIES);

    if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );

    if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	(SCpnt->sense_buffer[2] & 0x0f) == UNIT_ATTENTION) { /* New media? */
      (STp->mt_status)->mt_fileno = 0 ;
      SCpnt->sense_buffer[0]=0;
      memset ((void *) &cmd[0], 0, 10);
      cmd[0] = TEST_UNIT_READY;
      SCpnt->request.dev = dev;
      scsi_do_cmd(SCpnt,
		  (void *) cmd, (void *) (STp->buffer)->b_data,
		  0, st_sleep_done, ST_LONG_TIMEOUT,
		  MAX_READY_RETRIES);

      if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );
      (STp->mt_status)->mt_fileno = STp->drv_block = 0;
    }

    if ((STp->buffer)->last_result_fatal != 0) {
      if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	  (SCpnt->sense_buffer[2] & 0x0f) == NO_TAPE) {
        (STp->mt_status)->mt_fileno = STp->drv_block = 0 ;
	printk("st%d: No tape.\n", dev);
      } else {
	printk("st%d: Error %x.\n", dev, SCpnt->result);
        (STp->mt_status)->mt_fileno = STp->drv_block = (-1);
      }
      (STp->buffer)->in_use = 0;
      STp->in_use = 0;
      SCpnt->request.dev = -1;  /* Mark as not busy */
      return (-EIO);
    }

    SCpnt->sense_buffer[0]=0;
    memset ((void *) &cmd[0], 0, 10);
    cmd[0] = READ_BLOCK_LIMITS;
    SCpnt->request.dev = dev;
    scsi_do_cmd(SCpnt,
                (void *) cmd, (void *) (STp->buffer)->b_data,
		6, st_sleep_done, ST_TIMEOUT, MAX_READY_RETRIES);

    if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );

    if (!SCpnt->result && !SCpnt->sense_buffer[0]) {
      STp->max_block = ((STp->buffer)->b_data[1] << 16) |
	((STp->buffer)->b_data[2] << 8) | (STp->buffer)->b_data[3];
      STp->min_block = ((STp->buffer)->b_data[4] << 8) |
	(STp->buffer)->b_data[5];
#ifdef DEBUG
      if (debugging)
	printk("st%d: Block limits %d - %d bytes.\n", dev, STp->min_block,
	       STp->max_block);
#endif
    }
    else {
      STp->min_block = STp->max_block = (-1);
#ifdef DEBUG
      if (debugging)
	printk("st%d: Can't read block limits.\n", dev);
#endif
    }

    SCpnt->sense_buffer[0]=0;
    memset ((void *) &cmd[0], 0, 10);
    cmd[0] = MODE_SENSE;
    cmd[4] = 12;
    SCpnt->request.dev = dev;
    scsi_do_cmd(SCpnt,
                (void *) cmd, (void *) (STp->buffer)->b_data,
                12, st_sleep_done, ST_TIMEOUT, MAX_READY_RETRIES);

    if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );

    if ((STp->buffer)->last_result_fatal != 0) {
#ifdef DEBUG
      if (debugging)
	printk("st%d: No Mode Sense.\n", dev);
#endif
      (STp->buffer)->b_data[2] =
      (STp->buffer)->b_data[3] = 0;
    }
    SCpnt->request.dev = -1;  /* Mark as not busy */

#ifdef DEBUG
    if (debugging)
      printk("st%d: Mode sense. Length %d, medium %x, WBS %x, BLL %d\n", dev,
	     (STp->buffer)->b_data[0], (STp->buffer)->b_data[1],
	     (STp->buffer)->b_data[2], (STp->buffer)->b_data[3]);
#endif

    if ((STp->buffer)->b_data[3] >= 8) {
      STp->drv_buffer = ((STp->buffer)->b_data[2] >> 4) & 7;
      STp->density = (STp->buffer)->b_data[4];
      STp->block_size = (STp->buffer)->b_data[9] * 65536 +
	(STp->buffer)->b_data[10] * 256 + (STp->buffer)->b_data[11];
#ifdef DEBUG
      if (debugging)
	printk("st%d: Density %x, tape length: %x, blocksize: %d, drv buffer: %d\n",
	       dev, STp->density, (STp->buffer)->b_data[5] * 65536 +
	       (STp->buffer)->b_data[6] * 256 + (STp->buffer)->b_data[7],
	       STp->block_size, STp->drv_buffer);
#endif
      if (STp->block_size > st_buffer_size) {
	printk("st%d: Blocksize %d too large for buffer.\n", dev,
	       STp->block_size);
	(STp->buffer)->in_use = 0;
	STp->in_use = 0;
	return (-EIO);
      }

    }
    else
      STp->block_size = 512;  /* "Educated Guess" (?) */

    if (STp->block_size > 0) {
      (STp->buffer)->buffer_blocks = st_buffer_size / STp->block_size;
      (STp->buffer)->buffer_size =
	(STp->buffer)->buffer_blocks * STp->block_size;
    }
    else {
      (STp->buffer)->buffer_blocks = 1;
      (STp->buffer)->buffer_size = st_buffer_size;
    }
    (STp->buffer)->buffer_bytes = (STp->buffer)->read_pointer = 0;

#ifdef DEBUG
    if (debugging)
      printk("st%d: Block size: %d, buffer size: %d (%d blocks).\n", dev,
	     STp->block_size, (STp->buffer)->buffer_size,
	     (STp->buffer)->buffer_blocks);
#endif

    if ((STp->buffer)->b_data[2] & 0x80) {
      STp->write_prot = 1;
#ifdef DEBUG
      if (debugging)
	printk( "st%d: Write protected\n", dev);
#endif
    }

    return 0;
}


/* Close the device*/
	static void
scsi_tape_close(struct inode * inode, struct file * filp)
{
    int dev;
    int result;
    int rewind;
    static unsigned char cmd[10];
    Scsi_Cmnd * SCpnt;
    Scsi_Tape * STp;
   
    dev = MINOR(inode->i_rdev);
    rewind = (dev & 0x80) == 0;
    dev = dev & 127;
    STp = &(scsi_tapes[dev]);

    if ( STp->rw == ST_WRITING) {

      result = flush_write_buffer(dev);

#ifdef DEBUG
      if (debugging)
	printk("st%d: File length %ld bytes.\n", dev, filp->f_pos);
#endif

      if (result == 0 || result == (-ENOSPC)) {
	SCpnt = allocate_device(NULL, (STp->device)->index, 1);

	SCpnt->sense_buffer[0] = 0;
	memset(cmd, 0, 10);
	cmd[0] = WRITE_FILEMARKS;
	cmd[4] = 1;
	SCpnt->request.dev = dev;
	scsi_do_cmd( SCpnt,
		    (void *) cmd, (void *) (STp->buffer)->b_data,
		    0, st_sleep_done, ST_TIMEOUT, MAX_RETRIES);

	if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );

	if ((STp->buffer)->last_result_fatal != 0)
	  printk("st%d: Error on write filemark.\n", dev);
	else {
          (STp->mt_status)->mt_fileno++ ;
	  STp->drv_block = 0;
	}

	SCpnt->request.dev = -1;  /* Mark as not busy */
      }

#ifdef DEBUG
      if (debugging)
	printk("st%d: Buffer flushed, EOF written\n", dev);
#endif
    }
    else if (!rewind) {
#ifndef ST_IN_FILE_POS
      if ((STp->eof == ST_FM) && !STp->eof_hit)
	back_over_eof(dev);
#else
      flush_buffer(inode, filp, 0);
#endif
    }

    if (rewind)
      st_int_ioctl(inode, filp, MTREW, 1);

    (STp->buffer)->in_use = 0;
    STp->in_use = 0;

    return;
}


/* Write command */
	static int
st_write(struct inode * inode, struct file * filp, char * buf, int count)
{
    int dev;
    int total, do_count, blks, retval, transfer;
    int write_threshold;
    static unsigned char cmd[10];
    char *b_point;
    Scsi_Cmnd * SCpnt;
    Scsi_Tape * STp;

    dev = MINOR(inode->i_rdev) & 127;
    STp = &(scsi_tapes[dev]);
#ifdef DEBUG
    if (!STp->in_use) {
      printk("st%d: Incorrect device.\n", dev);
      return (-EIO);
    }
#endif

    if (STp->write_prot)
      return (-EACCES);

    if (STp->block_size == 0 && count > st_buffer_size)
      return (-EOVERFLOW);

    if (STp->rw == ST_READING) {
      retval = flush_buffer(inode, filp, 0);
      if (retval)
	return retval;
      STp->rw = ST_WRITING;
    }

    if ((STp->buffer)->writing) {
      write_behind_check(dev);
      if ((STp->buffer)->last_result_fatal) {
#ifdef DEBUG
	if (debugging)
	  printk("st%d: Async write error (write) %x.\n", dev,
		 (STp->buffer)->last_result);
#endif
	if ((STp->buffer)->last_result == INT_MAX) {
	  retval = (-ENOSPC);  /* All has been written */
	  STp->eof = ST_EOM_OK;
	}
	else
	  retval = (-EIO);
	return retval;
      }
    }

    if (STp->eof == ST_EOM_OK)
      return (-ENOSPC);
    else if (STp->eof == ST_EOM_ERROR)
      return (-EIO);

    if (!STp->do_buffer_writes) {
      if (STp->block_size != 0 && (count % STp->block_size) != 0)
	return (-EIO);   /* Write must be integral number of blocks */
      write_threshold = 1;
    }
    else
      write_threshold = (STp->buffer)->buffer_size;
    if (!STp->do_async_writes)
      write_threshold--;

    SCpnt = allocate_device(NULL, (STp->device)->index, 1);

    total = count;

    memset(cmd, 0, 10);
    cmd[0] = WRITE_6;
    cmd[1] = (STp->block_size != 0);

    STp->rw = ST_WRITING;

    b_point = buf;
    while((STp->block_size == 0 && !STp->do_async_writes && count > 0) ||
	  (STp->block_size != 0 &&
	   (STp->buffer)->buffer_bytes + count > write_threshold))
    {
      if (STp->block_size == 0)
	do_count = count;
      else {
	do_count = (STp->buffer)->buffer_size - (STp->buffer)->buffer_bytes;
	if (do_count > count)
	  do_count = count;
      }
      memcpy_fromfs((STp->buffer)->b_data +
		    (STp->buffer)->buffer_bytes, b_point, do_count);

      if (STp->block_size == 0)
        blks = transfer = do_count;
      else {
	blks = ((STp->buffer)->buffer_bytes + do_count) /
	  STp->block_size;
	transfer = blks * STp->block_size;
      }
      cmd[2] = blks >> 16;
      cmd[3] = blks >> 8;
      cmd[4] = blks;
      SCpnt->sense_buffer[0] = 0;
      SCpnt->request.dev = dev;
      scsi_do_cmd (SCpnt,
		   (void *) cmd, (STp->buffer)->b_data, transfer,
		   st_sleep_done, ST_TIMEOUT, MAX_RETRIES);

      if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );

      if ((STp->buffer)->last_result_fatal != 0) {
#ifdef DEBUG
	if (debugging)
	  printk("st%d: Error on write:\n", dev);
#endif
	if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	    (SCpnt->sense_buffer[2] & 0x40)) {
	  if (STp->block_size != 0 && (SCpnt->sense_buffer[0] & 0x80) != 0)
	    transfer = (SCpnt->sense_buffer[3] << 24) |
	      (SCpnt->sense_buffer[4] << 16) |
		(SCpnt->sense_buffer[5] << 8) | SCpnt->sense_buffer[6];
	  else if (STp->block_size == 0 &&
		   (SCpnt->sense_buffer[2] & 0x0f) == VOLUME_OVERFLOW)
	    transfer = do_count;
	  else
	    transfer = 0;
	  if (STp->block_size != 0)
	    transfer *= STp->block_size;
	  if (transfer <= do_count) {
	    filp->f_pos += do_count - transfer;
	    count -= do_count - transfer;
	    if (STp->drv_block >= 0) {
	      if (STp->block_size == 0 && transfer < do_count)
		STp->drv_block++;
	      else if (STp->block_size != 0)
		STp->drv_block += (do_count - transfer) / STp->block_size;
	    }
	    STp->eof = ST_EOM_OK;
	    retval = (-ENOSPC); /* EOM within current request */
#ifdef DEBUG
	    if (debugging)
	      printk("st%d: EOM with %d bytes unwritten.\n",
		     dev, transfer);
#endif
	  }
	  else {
	    STp->eof = ST_EOM_ERROR;
	    STp->drv_block = (-1);    /* Too cautious? */
	    retval = (-EIO); /* EOM for old data */
#ifdef DEBUG
	    if (debugging)
	      printk("st%d: EOM with lost data.\n", dev);
#endif
	  }
	}
	else {
	  STp->drv_block = (-1);    /* Too cautious? */
	  retval = (-EIO);
	}

	SCpnt->request.dev = -1;  /* Mark as not busy */
	(STp->buffer)->buffer_bytes = 0;
	STp->dirty = 0;
	if (count < total)
	  return total - count;
	else
	  return retval;
      }
      filp->f_pos += do_count;
      b_point += do_count;
      count -= do_count;
      if (STp->drv_block >= 0) {
	if (STp->block_size == 0)
	  STp->drv_block++;
	else
	  STp->drv_block += blks;
      }
      (STp->buffer)->buffer_bytes = 0;
      STp->dirty = 0;
    }
    if (count != 0) {
      STp->dirty = 1;
      memcpy_fromfs((STp->buffer)->b_data +
		    (STp->buffer)->buffer_bytes,b_point,count);
      filp->f_pos += count;
      (STp->buffer)->buffer_bytes += count;
      count = 0;
    }

    if ((STp->buffer)->last_result_fatal != 0) {
      SCpnt->request.dev = -1;
      return (STp->buffer)->last_result_fatal;
    }

    if (STp->do_async_writes &&
	((STp->buffer)->buffer_bytes >= STp->write_threshold ||
	 STp->block_size == 0) ) {
      /* Schedule an asynchronous write */
      if (STp->block_size == 0)
	(STp->buffer)->writing = (STp->buffer)->buffer_bytes;
      else
	(STp->buffer)->writing = ((STp->buffer)->buffer_bytes /
	  STp->block_size) * STp->block_size;
      STp->dirty = 0;

      if (STp->block_size == 0)
	blks = (STp->buffer)->writing;
      else
	blks = (STp->buffer)->writing / STp->block_size;
      cmd[2] = blks >> 16;
      cmd[3] = blks >> 8;
      cmd[4] = blks;
      SCpnt->result = (STp->buffer)->last_result = -1;
      SCpnt->sense_buffer[0] = 0;
      SCpnt->request.dev = dev;
      scsi_do_cmd (SCpnt,
		   (void *) cmd, (STp->buffer)->b_data,
		   (STp->buffer)->writing,
		   st_sleep_done, ST_TIMEOUT, MAX_RETRIES);
    }
    else
      SCpnt->request.dev = -1;  /* Mark as not busy */

    return( total);
}   


/* Read command */
	static int
st_read(struct inode * inode, struct file * filp, char * buf, int count)
{
    int dev;
    int total;
    int transfer, blks, bytes;
    static unsigned char cmd[10];
    Scsi_Cmnd * SCpnt;
    Scsi_Tape * STp;

    dev = MINOR(inode->i_rdev) & 127;
    STp = &(scsi_tapes[dev]);
#ifdef DEBUG
    if (!STp->in_use) {
      printk("st%d: Incorrect device.\n", dev);
      return (-EIO);
    }
#endif

    if (STp->block_size == 0 && count > st_buffer_size)
      return (-EOVERFLOW);

    if (!(STp->do_read_ahead) && STp->block_size != 0 &&
	(count % STp->block_size) != 0)
      return (-EIO);	/* Read must be integral number of blocks */

    if (STp->rw == ST_WRITING) {
      transfer = flush_buffer(inode, filp, 0);
      if (transfer)
	return transfer;
      STp->rw = ST_READING;
    }

#ifdef DEBUG
    if (debugging && STp->eof != ST_NOEOF)
      printk("st%d: EOF flag up. Bytes %d\n", dev,
	     (STp->buffer)->buffer_bytes);
#endif
    if (((STp->buffer)->buffer_bytes == 0) &&
	STp->eof == ST_EOM_OK)  /* EOM or Blank Check */
      return (-EIO);

    STp->rw = ST_READING;

    SCpnt = allocate_device(NULL, (STp->device)->index, 1);

    for (total = 0; total < count; ) {

      if ((STp->buffer)->buffer_bytes == 0 &&
	  STp->eof == ST_NOEOF) {

	memset(cmd, 0, 10);
	cmd[0] = READ_6;
	cmd[1] = (STp->block_size != 0);
	if (STp->block_size == 0)
	  blks = bytes = count;
	else {
	  if (STp->do_read_ahead) {
	    blks = (STp->buffer)->buffer_blocks;
	    bytes = blks * STp->block_size;
	  }
	  else {
	    bytes = count;
	    if (bytes > st_buffer_size)
	      bytes = st_buffer_size;
	    blks = bytes / STp->block_size;
	    bytes = blks * STp->block_size;
	  }
	}
	cmd[2] = blks >> 16;
	cmd[3] = blks >> 8;
	cmd[4] = blks;

	SCpnt->sense_buffer[0] = 0;
	SCpnt->request.dev = dev;
	scsi_do_cmd (SCpnt,
		     (void *) cmd, (STp->buffer)->b_data,
		     (STp->buffer)->buffer_size,
		     st_sleep_done, ST_TIMEOUT, MAX_RETRIES);

	if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );

	(STp->buffer)->read_pointer = 0;
	STp->eof_hit = 0;

	if ((STp->buffer)->last_result_fatal) {
#ifdef DEBUG
	  if (debugging)
	    printk("st%d: Sense: %2x %2x %2x %2x %2x %2x %2x %2x\n", dev,
		   SCpnt->sense_buffer[0], SCpnt->sense_buffer[1],
		   SCpnt->sense_buffer[2], SCpnt->sense_buffer[3],
		   SCpnt->sense_buffer[4], SCpnt->sense_buffer[5],
		   SCpnt->sense_buffer[6], SCpnt->sense_buffer[7]);
#endif
	  if ((SCpnt->sense_buffer[0] & 0x70) == 0x70) { /* extended sense */

	    if ((SCpnt->sense_buffer[2] & 0xe0) != 0) { /* EOF, EOM, or ILI */

	      if ((SCpnt->sense_buffer[0] & 0x80) != 0)
		transfer = (SCpnt->sense_buffer[3] << 24) |
		  (SCpnt->sense_buffer[4] << 16) |
		    (SCpnt->sense_buffer[5] << 8) | SCpnt->sense_buffer[6];
	      else
		transfer = 0;
	      if (STp->block_size == 0 &&
		  (SCpnt->sense_buffer[2] & 0x0f) == MEDIUM_ERROR)
		transfer = bytes;

	      if (SCpnt->sense_buffer[2] & 0x20) {
		if (STp->block_size == 0) {
		  if (transfer <= 0)
		    transfer = 0;
		  (STp->buffer)->buffer_bytes = bytes - transfer;
		}
		else {
		  printk("st%d: Incorrect block size.\n", dev);
		  SCpnt->request.dev = -1;  /* Mark as not busy */
		  return (-EIO);
		}
	      }
	      else if (SCpnt->sense_buffer[2] & 0x40) {
		STp->eof = ST_EOM_OK;
		if (STp->block_size == 0)
		  (STp->buffer)->buffer_bytes = bytes - transfer;
		else
		  (STp->buffer)->buffer_bytes =
		    bytes - transfer * STp->block_size;
#ifdef DEBUG
		if (debugging)
		  printk("st%d: EOM detected (%d bytes read).\n", dev,
			 (STp->buffer)->buffer_bytes);
#endif
	      }
	      else if (SCpnt->sense_buffer[2] & 0x80) {
		STp->eof = ST_FM;
		if (STp->block_size == 0)
		  (STp->buffer)->buffer_bytes = 0;
		else
		  (STp->buffer)->buffer_bytes =
		    bytes - transfer * STp->block_size;
#ifdef DEBUG
		if (debugging)
		  printk(
		    "st%d: EOF detected (%d bytes read, transferred %d bytes).\n",
			 dev, (STp->buffer)->buffer_bytes, total);
#endif
	      } /* end of EOF, EOM, ILI test */
	    }
	    else { /* nonzero sense key */
#ifdef DEBUG
	      if (debugging)
		printk("st%d: Tape error while reading.\n", dev);
#endif
	      SCpnt->request.dev = -1;
	      STp->drv_block = (-1);
	      if (total)
		return total;
	      else
		return -EIO;
	    }
	  }
	  else {
	    transfer = (STp->buffer)->last_result_fatal;
	    SCpnt->request.dev = -1;  /* Mark as not busy */
	    return transfer;
	  }
	}
	else /* Read successful */
	  (STp->buffer)->buffer_bytes = bytes;

	if (STp->drv_block >= 0) {
	  if (STp->block_size == 0)
	    STp->drv_block++;
	  else
	    STp->drv_block += (STp->buffer)->buffer_bytes / STp->block_size;
	}

      } /* if ((STp->buffer)->buffer_bytes == 0 &&
	   STp->eof == ST_NOEOF) */

      if ((STp->buffer)->buffer_bytes > 0) {
#ifdef DEBUG
	if (debugging && STp->eof != ST_NOEOF)
	  printk("st%d: EOF up. Left %d, needed %d.\n", dev,
		 (STp->buffer)->buffer_bytes, count - total);
#endif
	transfer = (STp->buffer)->buffer_bytes < count - total ?
	  (STp->buffer)->buffer_bytes : count - total;
	memcpy_tofs(buf, (STp->buffer)->b_data +
		    (STp->buffer)->read_pointer,transfer);
	filp->f_pos += transfer;
	buf += transfer;
	total += transfer;
	(STp->buffer)->buffer_bytes -= transfer;
	(STp->buffer)->read_pointer += transfer;
      }
      else if (STp->eof != ST_NOEOF) {
	STp->eof_hit = 1;
	SCpnt->request.dev = -1;  /* Mark as not busy */
	if (total == 0 && STp->eof == ST_FM) {
	  STp->eof = 0;
	  STp->drv_block = 0;
	  (STp->mt_status)->mt_fileno++;
	}
	if (total == 0 && STp->eof == ST_EOM_OK)
	  return (-EIO);  /* ST_EOM_ERROR not used in read */
	return total;
      }

      if (STp->block_size == 0)
	count = total;  /* Read only one variable length block */

    } /* for (total = 0; total < count; ) */

    SCpnt->request.dev = -1;  /* Mark as not busy */

    return total;
}



/* Set the driver options */
	static int
st_set_options(struct inode * inode, long options)
{
  int dev, value;
  Scsi_Tape *STp;

  dev = MINOR(inode->i_rdev) & 127;
  STp = &(scsi_tapes[dev]);
  if ((options & MT_ST_OPTIONS) == MT_ST_BOOLEANS) {
    STp->do_buffer_writes = (options & MT_ST_BUFFER_WRITES) != 0;
    STp->do_async_writes  = (options & MT_ST_ASYNC_WRITES) != 0;
    STp->do_read_ahead    = (options & MT_ST_READ_AHEAD) != 0;
#ifdef DEBUG
    debugging = (options & MT_ST_DEBUGGING) != 0;
    printk(
"st%d: options: buffer writes: %d, async writes: %d, read ahead: %d\n",
	   dev, STp->do_buffer_writes, STp->do_async_writes,
	   STp->do_read_ahead);
    printk("              debugging: %d\n", debugging);
#endif
  }
  else if ((options & MT_ST_OPTIONS) == MT_ST_WRITE_THRESHOLD) {
    value = (options & ~MT_ST_OPTIONS) * ST_BLOCK_SIZE;
    if (value < 1 || value > st_buffer_size) {
      printk("st: Write threshold %d too small or too large.\n",
	     value);
      return (-EIO);
    }
    STp->write_threshold = value;
#ifdef DEBUG
    printk("st%d: Write threshold set to %d bytes.\n", dev,
	   STp->write_threshold);
#endif
  }
  else
    return (-EIO);

  return 0;
}


/* Internal ioctl function */
	static int
st_int_ioctl(struct inode * inode,struct file * file,
	     unsigned int cmd_in, unsigned long arg)
{
   int dev = MINOR(inode->i_rdev);
   int timeout = ST_LONG_TIMEOUT;
   long ltmp;
   int ioctl_result;
   unsigned char cmd[10];
   Scsi_Cmnd * SCpnt;
   Scsi_Tape * STp;
   int fileno, blkno, undone, datalen;

   dev = dev & 127;
   STp = &(scsi_tapes[dev]);
   fileno = (STp->mt_status)->mt_fileno ;
   blkno = STp->drv_block;

   memset(cmd, 0, 10);
   datalen = 0;
   switch (cmd_in) {
     case MTFSF:
     case MTFSFM:
       cmd[0] = SPACE;
       cmd[1] = 0x01; /* Space FileMarks */
       cmd[2] = (arg >> 16);
       cmd[3] = (arg >> 8);
       cmd[4] = arg;
#ifdef DEBUG
       if (debugging)
	 printk("st%d: Spacing tape forward over %d filemarks.\n", dev,
		cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
#endif
       fileno += arg;
       blkno = 0;
       break; 
     case MTBSF:
     case MTBSFM:
       cmd[0] = SPACE;
       cmd[1] = 0x01; /* Space FileMarks */
       ltmp = (-arg);
       cmd[2] = (ltmp >> 16);
       cmd[3] = (ltmp >> 8);
       cmd[4] = ltmp;
#ifdef DEBUG
       if (debugging) {
	 if (cmd[2] & 0x80)
	   ltmp = 0xff000000;
	 ltmp = ltmp | (cmd[2] << 16) | (cmd[3] << 8) | cmd[4];
	 printk("st%d: Spacing tape backward over %ld filemarks.\n", dev, (-ltmp));
       }
#endif
       fileno -= arg;
       blkno = (-1);  /* We can't know the block number */
       break; 
      case MTFSR:
       cmd[0] = SPACE;
       cmd[1] = 0x00; /* Space Blocks */
       cmd[2] = (arg >> 16);
       cmd[3] = (arg >> 8);
       cmd[4] = arg;
#ifdef DEBUG
       if (debugging)
	 printk("st%d: Spacing tape forward %d blocks.\n", dev,
		cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
#endif
       if (blkno >= 0)
	 blkno += arg;
       break; 
     case MTBSR:
       cmd[0] = SPACE;
       cmd[1] = 0x00; /* Space Blocks */
       ltmp = (-arg);
       cmd[2] = (ltmp >> 16);
       cmd[3] = (ltmp >> 8);
       cmd[4] = ltmp;
#ifdef DEBUG
       if (debugging) {
	 if (cmd[2] & 0x80)
	   ltmp = 0xff000000;
	 ltmp = ltmp | (cmd[2] << 16) | (cmd[3] << 8) | cmd[4];
	 printk("st%d: Spacing tape backward %ld blocks.\n", dev, (-ltmp));
       }
#endif
       if (blkno >= 0)
	 blkno -= arg;
       break; 
     case MTWEOF:
       if (STp->write_prot)
	 return (-EACCES);
       cmd[0] = WRITE_FILEMARKS;
       cmd[2] = (arg >> 16);
       cmd[3] = (arg >> 8);
       cmd[4] = arg;
       timeout = ST_TIMEOUT;
#ifdef DEBUG
       if (debugging)
	 printk("st%d: Writing %d filemarks.\n", dev,
		cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
#endif
       fileno += arg;
       blkno = 0;
       break; 
     case MTREW:
       cmd[0] = REZERO_UNIT;
#ifdef ST_NOWAIT
       cmd[1] = 1;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#endif
#ifdef DEBUG
       if (debugging)
	 printk("st%d: Rewinding tape.\n", dev);
#endif
       fileno = blkno = 0 ;
       break; 
     case MTOFFL:
       cmd[0] = START_STOP;
#ifdef ST_NOWAIT
       cmd[1] = 1;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#endif
#ifdef DEBUG
       if (debugging)
	 printk("st%d: Unloading tape.\n", dev);
#endif
       fileno = blkno = 0 ;
       break; 
     case MTNOP:
#ifdef DEBUG
       if (debugging)
	 printk("st%d: No op on tape.\n", dev);
#endif
       return 0;  /* Should do something ? */
       break;
     case MTRETEN:
       cmd[0] = START_STOP;
#ifdef ST_NOWAIT
       cmd[1] = 1;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#endif
       cmd[4] = 3;
#ifdef DEBUG
       if (debugging)
	 printk("st%d: Retensioning tape.\n", dev);
#endif
       fileno = blkno = 0 ;
       break; 
     case MTEOM:
       /* space to the end of tape */
       ioctl_result = st_int_ioctl(inode, file, MTFSF, 0x3fff);
       fileno = (STp->mt_status)->mt_fileno ;
       /* The next lines would hide the number of spaced FileMarks
          That's why I inserted the previous lines. I had no luck
	  with detecting EOM with FSF, so we go now to EOM.
          Joerg Weule */
       cmd[0] = SPACE;
       cmd[1] = 3;
#ifdef DEBUG
       if (debugging)
	 printk("st%d: Spacing to end of recorded medium.\n", dev);
#endif
       blkno = (-1);
       break; 
     case MTERASE:
       if (STp->write_prot)
	 return (-EACCES);
       cmd[0] = ERASE;
       cmd[1] = 1;  /* To the end of tape */
#ifdef DEBUG
       if (debugging)
	 printk("st%d: Erasing tape.\n", dev);
#endif
       fileno = blkno = 0 ;
       break;
     case MTSEEK:
       if ((STp->device)->scsi_level < SCSI_2) {
	 cmd[0] = QFA_SEEK_BLOCK;
	 cmd[2] = (arg >> 16);
	 cmd[3] = (arg >> 8);
	 cmd[4] = arg;
	 cmd[5] = 0;
       }
       else {
	 cmd[0] = SEEK_10;
	 cmd[1] = 4;
	 cmd[3] = (arg >> 24);
	 cmd[4] = (arg >> 16);
	 cmd[5] = (arg >> 8);
	 cmd[6] = arg;
       }
#ifdef ST_NOWAIT
       cmd[1] |= 1;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#endif
#ifdef DEBUG
       if (debugging)
	 printk("st%d: Seeking tape to block %ld.\n", dev, arg);
#endif
       fileno = blkno = (-1);
       break;
     case MTSETBLK:  /* Set block length */
     case MTSETDENSITY: /* Set tape density */
     case MTSETDRVBUFFER: /* Set drive buffering */
       if (STp->dirty || (STp->buffer)->buffer_bytes != 0)
	 return (-EIO);   /* Not allowed if data in buffer */
       if (cmd_in == MTSETBLK &&
	   arg != 0 &&
	   (arg < STp->min_block || arg > STp->max_block ||
	    arg > st_buffer_size)) {
	 printk("st%d: Illegal block size.\n", dev);
	 return (-EINVAL);
       }
       cmd[0] = MODE_SELECT;
       cmd[4] = datalen = 12;

       memset((STp->buffer)->b_data, 0, 12);
       if (cmd_in == MTSETDRVBUFFER)
	 (STp->buffer)->b_data[2] = (arg & 7) << 4;
       else
	 (STp->buffer)->b_data[2] = 
	   STp->drv_buffer << 4;
       (STp->buffer)->b_data[3] = 8;     /* block descriptor length */
       if (cmd_in == MTSETDENSITY)
	 (STp->buffer)->b_data[4] = arg;
       else
	 (STp->buffer)->b_data[4] = STp->density;
       if (cmd_in == MTSETBLK)
	 ltmp = arg;
       else
	 ltmp = STp->block_size;
       (STp->buffer)->b_data[9] = (ltmp >> 16);
       (STp->buffer)->b_data[10] = (ltmp >> 8);
       (STp->buffer)->b_data[11] = ltmp;
       timeout = ST_TIMEOUT;
#ifdef DEBUG
       if (debugging) {
	 if (cmd_in == MTSETBLK)
	   printk("st%d: Setting block size to %d bytes.\n", dev,
		  (STp->buffer)->b_data[9] * 65536 +
		  (STp->buffer)->b_data[10] * 256 +
		  (STp->buffer)->b_data[11]);
	 else if (cmd_in == MTSETDENSITY)
	   printk("st%d: Setting density code to %x.\n", dev,
		  (STp->buffer)->b_data[4]);
	 else
	   printk("st%d: Setting drive buffer code to %d.\n", dev,
		  ((STp->buffer)->b_data[2] >> 4) & 7);
       }
#endif
       break;
     default:
       printk("st%d: Unknown st_ioctl command %x.\n", dev, cmd_in);
       return (-ENOSYS);
     }

   SCpnt = allocate_device(NULL, (STp->device)->index, 1);
   SCpnt->sense_buffer[0] = 0;
   SCpnt->request.dev = dev;
   scsi_do_cmd(SCpnt,
	       (void *) cmd, (void *) (STp->buffer)->b_data, datalen,
	       st_sleep_done, timeout, MAX_RETRIES);

   if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );

   ioctl_result = (STp->buffer)->last_result_fatal;

   SCpnt->request.dev = -1;  /* Mark as not busy */

   if (!ioctl_result) {
     STp->drv_block = blkno;
     (STp->mt_status)->mt_fileno = fileno;
     if (cmd_in == MTBSFM)
       ioctl_result = st_int_ioctl(inode, file, MTFSF, 1);
     else if (cmd_in == MTFSFM)
       ioctl_result = st_int_ioctl(inode, file, MTBSF, 1);
     else if (cmd_in == MTSETBLK) {
       STp->block_size = arg;
       if (arg != 0) {
	 (STp->buffer)->buffer_blocks =
	   st_buffer_size / STp->block_size;
	 (STp->buffer)->buffer_size =
	   (STp->buffer)->buffer_blocks * STp->block_size;
       }
       else {
	 (STp->buffer)->buffer_blocks = 1;
	 (STp->buffer)->buffer_size = st_buffer_size;
       }
       (STp->buffer)->buffer_bytes =
	 (STp->buffer)->read_pointer = 0;
     }
     else if (cmd_in == MTSETDRVBUFFER)
       STp->drv_buffer = arg;
     else if (cmd_in == MTSETDENSITY)
       STp->density = arg;
     else if (cmd_in == MTEOM || cmd_in == MTWEOF) {
       STp->eof = ST_EOM_OK;
       STp->eof_hit = 0;
     }
     else if (cmd_in != MTSETBLK && cmd_in != MTNOP) {
       STp->eof = ST_NOEOF;
       STp->eof_hit = 0;
     }
   } else {
     if (SCpnt->sense_buffer[2] & 0x40) {
       STp->eof = ST_EOM_OK;
       STp->eof_hit = 0;
       STp->drv_block = 0;
     }
     undone = (
          (SCpnt->sense_buffer[3] << 24) +
          (SCpnt->sense_buffer[4] << 16) +
          (SCpnt->sense_buffer[5] << 8) +
          SCpnt->sense_buffer[6] );
     if ( (cmd_in == MTFSF) || (cmd_in == MTFSFM) )
       (STp->mt_status)->mt_fileno = fileno - undone ;
     else if ( (cmd_in == MTBSF) || (cmd_in == MTBSFM) )
       (STp->mt_status)->mt_fileno = fileno + undone ;
     else if (cmd_in == MTFSR) {
       if (blkno >= undone)
	 STp->drv_block = blkno - undone;
       else
	 STp->drv_block = (-1);
     }
     else if (cmd_in == MTBSR && blkno >= 0) {
       if (blkno >= 0)
	 STp->drv_block = blkno + undone;
       else
	 STp->drv_block = (-1);
     }
   }

   return ioctl_result ;
}



/* The ioctl command */
	static int
st_ioctl(struct inode * inode,struct file * file,
	 unsigned int cmd_in, unsigned long arg)
{
   int dev = MINOR(inode->i_rdev);
   int i, cmd, result;
   struct mtop mtc;
   struct mtpos mt_pos;
   unsigned char scmd[10];
   Scsi_Cmnd *SCpnt;
   Scsi_Tape *STp;

   dev = dev & 127;
   STp = &(scsi_tapes[dev]);
#ifdef DEBUG
   if (debugging && !STp->in_use) {
     printk("st%d: Incorrect device.\n", dev);
     return (-EIO);
   }
#endif

   cmd = cmd_in & IOCCMD_MASK;
   if (cmd == (MTIOCTOP & IOCCMD_MASK)) {

     if (((cmd_in & IOCSIZE_MASK) >> IOCSIZE_SHIFT) != sizeof(mtc))
       return (-EINVAL);

     i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(mtc));
     if (i)
        return i;

     memcpy_fromfs((char *) &mtc, (char *)arg, sizeof(struct mtop));

     i = flush_buffer(inode, file, mtc.mt_op == MTSEEK ||
		      mtc.mt_op == MTREW || mtc.mt_op == MTOFFL ||
		      mtc.mt_op == MTRETEN || mtc.mt_op == MTEOM);
     if (i < 0)
       return i;

     if (mtc.mt_op == MTSETDRVBUFFER &&
	 (mtc.mt_count & MT_ST_OPTIONS) != 0)
       return st_set_options(inode, mtc.mt_count);
     else
       return st_int_ioctl(inode, file, mtc.mt_op, mtc.mt_count);
   }
   else if (cmd == (MTIOCGET & IOCCMD_MASK)) {

     if (((cmd_in & IOCSIZE_MASK) >> IOCSIZE_SHIFT) != sizeof(struct mtget))
       return (-EINVAL);
     i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct mtget));
     if (i)
       return i;

     (STp->mt_status)->mt_dsreg =
       ((STp->block_size << MT_ST_BLKSIZE_SHIFT) & MT_ST_BLKSIZE_MASK) |
       ((STp->density << MT_ST_DENSITY_SHIFT) & MT_ST_DENSITY_MASK);
     (STp->mt_status)->mt_blkno = STp->drv_block;
     if (STp->block_size != 0) {
       if (STp->rw == ST_WRITING)
	 (STp->mt_status)->mt_blkno +=
	   (STp->buffer)->buffer_bytes / STp->block_size;
       else if (STp->rw == ST_READING)
	 (STp->mt_status)->mt_blkno -= ((STp->buffer)->buffer_bytes +
	   STp->block_size - 1) / STp->block_size;
     }

     memcpy_tofs((char *)arg, (char *)(STp->mt_status),
		 sizeof(struct mtget));

     (STp->mt_status)->mt_erreg = 0;  /* Clear after read */
     return 0;
   }
   else if (cmd == (MTIOCPOS & IOCCMD_MASK)) {
#ifdef DEBUG
     if (debugging)
       printk("st%d: get tape position.\n", dev);
#endif
     if (((cmd_in & IOCSIZE_MASK) >> IOCSIZE_SHIFT) != sizeof(struct mtpos))
       return (-EINVAL);

     i = flush_buffer(inode, file, 0);
     if (i < 0)
       return i;

     i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct mtpos));
     if (i)
       return i;

     SCpnt = allocate_device(NULL, (STp->device)->index, 1);

     SCpnt->sense_buffer[0]=0;
     memset (scmd, 0, 10);
     if ((STp->device)->scsi_level < SCSI_2) {
       scmd[0] = QFA_REQUEST_BLOCK;
       scmd[4] = 3;
     }
     else {
       scmd[0] = READ_POSITION;
       scmd[1] = 1;
     }
     SCpnt->request.dev = dev;
     SCpnt->sense_buffer[0] = 0;
     scsi_do_cmd(SCpnt,
		 (void *) scmd, (void *) (STp->buffer)->b_data,
		 20, st_sleep_done, ST_TIMEOUT, MAX_READY_RETRIES);

     if (SCpnt->request.dev == dev) sleep_on( &(STp->waiting) );
     
     if ((STp->buffer)->last_result_fatal != 0) {
       mt_pos.mt_blkno = (-1);
#ifdef DEBUG
       if (debugging)
	 printk("st%d: Can't read tape position.\n", dev);
#endif
       result = (-EIO);
     }
     else {
       result = 0;
       if ((STp->device)->scsi_level < SCSI_2)
	 mt_pos.mt_blkno = ((STp->buffer)->b_data[0] << 16) 
	   + ((STp->buffer)->b_data[1] << 8) 
	     + (STp->buffer)->b_data[2];
       else
	 mt_pos.mt_blkno = ((STp->buffer)->b_data[4] << 24)
	   + ((STp->buffer)->b_data[5] << 16) 
	     + ((STp->buffer)->b_data[6] << 8) 
	       + (STp->buffer)->b_data[7];

     }

     SCpnt->request.dev = -1;  /* Mark as not busy */

     memcpy_tofs((char *)arg, (char *) (&mt_pos), sizeof(struct mtpos));
     return result;
   }
   else
     return scsi_ioctl(STp->device, cmd_in, (void *) arg);
}


/* Set the boot options. Syntax: st=xxx,yyy
   where xxx is buffer size in 512 byte blocks and yyy is write threshold
   in 512 byte blocks. */
	void
st_setup(char *str, int *ints)
{
  if (ints[0] > 0 && ints[1] > 0)
    st_buffer_size = ints[1] * ST_BLOCK_SIZE;
  if (ints[0] > 1 && ints[2] > 0) {
    st_write_threshold = ints[2] * ST_BLOCK_SIZE;
    if (st_write_threshold > st_buffer_size)
      st_write_threshold = st_buffer_size;
  }
  if (ints[0] > 2 && ints[3] > 0)
    st_max_buffers = ints[3];
}


static struct file_operations st_fops = {
   NULL,            /* lseek - default */
   st_read,         /* read - general block-dev read */
   st_write,        /* write - general block-dev write */
   NULL,            /* readdir - bad */
   NULL,            /* select */
   st_ioctl,        /* ioctl */
   NULL,            /* mmap */
   scsi_tape_open,  /* open */
   scsi_tape_close, /* release */
   NULL		    /* fsync */
};

void st_attach(Scsi_Device * SDp){
  scsi_tapes[NR_ST++].device = SDp;
  if(NR_ST > MAX_ST) panic ("scsi_devices corrupt (st)");
};

unsigned long st_init1(unsigned long mem_start, unsigned long mem_end){
  scsi_tapes = (Scsi_Tape *) mem_start;
  mem_start += MAX_ST * sizeof(Scsi_Tape);
  return mem_start;
};

/* Driver initialization */
unsigned long st_init(unsigned long mem_start, unsigned long mem_end)
{
  int i, dev_nbr;
  Scsi_Tape * STp;

  if (register_chrdev(MAJOR_NR,"st",&st_fops)) {
    printk("Unable to get major %d for SCSI tapes\n",MAJOR_NR);
    return mem_start;
  }
  if (NR_ST == 0) return mem_start;

#ifdef DEBUG
  printk("st: Buffer size %d bytes, write threshold %d bytes.\n",
	 st_buffer_size, st_write_threshold);
#endif

  for (i=0, dev_nbr=(-1); i < NR_ST; ++i) {
    STp = &(scsi_tapes[i]);
    STp->capacity = 0xfffff;
    STp->dirty = 0;
    STp->rw = ST_IDLE;
    STp->eof = ST_NOEOF;
    STp->waiting = NULL;
    STp->in_use = 0;
    STp->drv_buffer = 1;  /* Try buffering if no mode sense */
    STp->density = 0;
    STp->do_buffer_writes = ST_BUFFER_WRITES;
    STp->do_async_writes = ST_ASYNC_WRITES;
    STp->do_read_ahead = ST_READ_AHEAD;
    STp->write_threshold = st_write_threshold;
    STp->drv_block = 0;
    STp->mt_status = (struct mtget *) mem_start;
    mem_start += sizeof(struct mtget);
    /* Initialize status */
    memset((void *) scsi_tapes[i].mt_status, 0, sizeof(struct mtget));
    for (dev_nbr++; dev_nbr < NR_SCSI_DEVICES; dev_nbr++)
      if (scsi_devices[dev_nbr].type == TYPE_TAPE)
	break;
    if (dev_nbr >= NR_SCSI_DEVICES)
      printk("st%d: ERROR: Not found in scsi chain.\n", i);
    else {
      if (scsi_devices[dev_nbr].scsi_level <= 2)
	STp->mt_status->mt_type = MT_ISSCSI1;
      else
	STp->mt_status->mt_type = MT_ISSCSI2;
    }
  }

  /* Allocate the buffers */
  st_nbr_buffers = NR_ST;
  if (st_nbr_buffers > st_max_buffers)
    st_nbr_buffers = st_max_buffers;
  st_buffers = (ST_buffer **)mem_start;
  mem_start += st_nbr_buffers * sizeof(ST_buffer *);
  for (i=0; i < st_nbr_buffers; i++) {
    st_buffers[i] = (ST_buffer *) mem_start;
#ifdef DEBUG
/*    printk("st: Buffer address: %p\n", st_buffers[i]); */
#endif
    mem_start += sizeof(ST_buffer) - 1 + st_buffer_size;
    st_buffers[i]->in_use = 0;
    st_buffers[i]->writing = 0;
  }

  return mem_start;
}
