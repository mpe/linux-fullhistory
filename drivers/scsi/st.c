/*
  SCSI Tape Driver for Linux version 1.1 and newer. See the accompanying
  file README.st for more information.

  History:
  Rewritten from Dwayne Forsyth's SCSI tape driver by Kai Makisara.
  Contribution and ideas from several people including (in alphabetical
  order) Klaus Ehrenfried, Steve Hirsch, Wolfgang Denk, Andreas Koppenh"ofer,
  J"org Weule, and Eric Youngdale.

  Copyright 1992, 1993, 1994, 1995 Kai Makisara
		 email Kai.Makisara@metla.fi

  Last modified: Sat Nov  4 22:23:54 1995 by root@kai.makisara.fi
  Some small formal changes - aeb, 950809
*/

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mtio.h>
#include <linux/ioctl.h>
#include <linux/fcntl.h>
#include <asm/segment.h>
#include <asm/dma.h>
#include <asm/system.h>

/* The driver prints some debugging information on the console if DEBUG
   is defined and non-zero. */
#define DEBUG 0

#define MAJOR_NR SCSI_TAPE_MAJOR
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include "scsi_ioctl.h"
#include "st.h"
#include "constants.h"

/* The default definitions have been moved to st_options.h */

#define ST_BLOCK_SIZE 1024

#include "st_options.h"

#define ST_BUFFER_SIZE (ST_BUFFER_BLOCKS * ST_BLOCK_SIZE)
#define ST_WRITE_THRESHOLD (ST_WRITE_THRESHOLD_BLOCKS * ST_BLOCK_SIZE)

/* The buffer size should fit into the 24 bits for length in the
   6-byte SCSI read and write commands. */
#if ST_BUFFER_SIZE >= (2 << 24 - 1)
#error "Buffer size should not exceed (2 << 24 - 1) bytes!"
#endif

#if DEBUG
static int debugging = 1;
#endif

#define MAX_RETRIES 0
#define MAX_WRITE_RETRIES 0
#define MAX_READY_RETRIES 5
#define NO_TAPE  NOT_READY

#define ST_TIMEOUT (900 * HZ)
#define ST_LONG_TIMEOUT (2000 * HZ)

#define TAPE_NR(x) (MINOR(x) & 127)

static int st_nbr_buffers;
static ST_buffer **st_buffers;
static int st_buffer_size = ST_BUFFER_SIZE;
static int st_write_threshold = ST_WRITE_THRESHOLD;
static int st_max_buffers = ST_MAX_BUFFERS;

Scsi_Tape * scsi_tapes = NULL;

static ST_buffer *new_tape_buffer(int);
static int enlarge_buffer(ST_buffer *, int);
static void normalize_buffer(ST_buffer *);

static int st_init(void);
static int st_attach(Scsi_Device *);
static int st_detect(Scsi_Device *);
static void st_detach(Scsi_Device *);

struct Scsi_Device_Template st_template = {NULL, "tape", "st", NULL, TYPE_TAPE, 
					     SCSI_TAPE_MAJOR, 0, 0, 0, 0,
					     st_detect, st_init,
					     NULL, st_attach, st_detach};

static int st_int_ioctl(struct inode * inode,struct file * file,
	     unsigned int cmd_in, unsigned long arg);




/* Convert the result to success code */
	static int
st_chk_result(Scsi_Cmnd * SCpnt)
{
  int dev = TAPE_NR(SCpnt->request.rq_dev);
  int result = SCpnt->result;
  unsigned char * sense = SCpnt->sense_buffer, scode;
#if DEBUG
  const char *stp;
#endif

  if (!result /* && SCpnt->sense_buffer[0] == 0 */ )
    return 0;
#if DEBUG
  if (debugging) {
    printk("st%d: Error: %x, cmd: %x %x %x %x %x %x Len: %d\n", dev, result,
	   SCpnt->data_cmnd[0], SCpnt->data_cmnd[1], SCpnt->data_cmnd[2],
	   SCpnt->data_cmnd[3], SCpnt->data_cmnd[4], SCpnt->data_cmnd[5],
	   SCpnt->request_bufflen);
    if (driver_byte(result) & DRIVER_SENSE)
      print_sense("st", SCpnt);
    else
      printk("\n");
  }
#endif
  scode = sense[2] & 0x0f;
  if (!(driver_byte(result) & DRIVER_SENSE) ||
      ((sense[0] & 0x70) == 0x70 &&
       scode != NO_SENSE &&
       scode != RECOVERED_ERROR &&
       scode != UNIT_ATTENTION &&
       scode != BLANK_CHECK &&
       scode != VOLUME_OVERFLOW &&
       SCpnt->data_cmnd[0] != MODE_SENSE)) {  /* Abnormal conditions for tape */
    printk("st%d: Error %x. ", dev, result);
#if !DEBUG
    if (driver_byte(result) & DRIVER_SENSE)
      print_sense("st", SCpnt);
    else
      printk("\n");
#endif
  }

  if ((sense[0] & 0x70) == 0x70 &&
      scode == RECOVERED_ERROR
#if ST_RECOVERED_WRITE_FATAL
      && SCpnt->data_cmnd[0] != WRITE_6
      && SCpnt->data_cmnd[0] != WRITE_FILEMARKS
#endif
      ) {
    scsi_tapes[dev].recover_count++;
    scsi_tapes[dev].mt_status->mt_erreg += (1 << MT_ST_SOFTERR_SHIFT);
#if DEBUG
    if (debugging) {  /* This is compiled always on purpose */
      if (SCpnt->data_cmnd[0] == READ_6)
	stp = "read";
      else if (SCpnt->data_cmnd[0] == WRITE_6)
	stp = "write";
      else
	stp = "ioctl";
      printk("st%d: Recovered %s error (%d).\n", dev, stp,
	     scsi_tapes[dev].recover_count);
    }
#endif
    return 0;
  }
  return (-EIO);
}


/* Wakeup from interrupt */
	static void
st_sleep_done (Scsi_Cmnd * SCpnt)
{
  unsigned int st_nbr;
  int remainder;
  Scsi_Tape * STp;

  if ((st_nbr = TAPE_NR(SCpnt->request.rq_dev)) < st_template.nr_dev) {
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
    if ((STp->buffer)->writing) {
      /* Process errors before releasing request */
      (STp->buffer)->last_result_fatal = st_chk_result(SCpnt);
      SCpnt->request.rq_status = RQ_INACTIVE;
    }
    else
      SCpnt->request.rq_status = RQ_SCSI_DONE;

#if DEBUG
    STp->write_pending = 0;
#endif
    up(SCpnt->request.sem);
  }
#if DEBUG
  else if (debugging)
    printk("st?: Illegal interrupt device %x\n", st_nbr);
#endif
}


/* Do the scsi command */
	static Scsi_Cmnd *
st_do_scsi(Scsi_Cmnd *SCpnt, Scsi_Tape *STp, unsigned char *cmd, int bytes,
	   int timeout, int retries)
{
  if (SCpnt == NULL)
    if ((SCpnt = allocate_device(NULL, STp->device, 1)) == NULL) {
      printk("st%d: Can't get SCSI request.\n", TAPE_NR(STp->devt));
      return NULL;
    }

  cmd[1] |= (SCpnt->lun << 5) & 0xe0;
  STp->sem = MUTEX_LOCKED;
  SCpnt->request.sem = &(STp->sem);
  SCpnt->request.rq_status = RQ_SCSI_BUSY;
  SCpnt->request.rq_dev = STp->devt;

  scsi_do_cmd(SCpnt, (void *)cmd, (STp->buffer)->b_data, bytes,
	      st_sleep_done, timeout, retries);

  down(SCpnt->request.sem);

  (STp->buffer)->last_result_fatal = st_chk_result(SCpnt);

  return SCpnt;
}


/* Handle the write-behind checking */
	static void
write_behind_check(Scsi_Tape *STp)
{
  ST_buffer * STbuffer;

  STbuffer = STp->buffer;

#if DEBUG
  if (STp->write_pending)
    STp->nbr_waits++;
  else
    STp->nbr_finished++;
#endif

  down(&(STp->sem));

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
back_over_eof(Scsi_Tape *STp)
{
  Scsi_Cmnd *SCpnt;
  unsigned char cmd[10];

  cmd[0] = SPACE;
  cmd[1] = 0x01; /* Space FileMarks */
  cmd[2] = cmd[3] = cmd[4] = 0xff;  /* -1 filemarks */
  cmd[5] = 0;

  SCpnt = st_do_scsi(NULL, STp, cmd, 0, ST_TIMEOUT, MAX_RETRIES);
  if (!SCpnt)
    return (-EBUSY);

  SCpnt->request.rq_status = RQ_INACTIVE;
  if ((STp->buffer)->last_result != 0) {
    printk("st%d: Backing over filemark failed.\n", TAPE_NR(STp->devt));
    if ((STp->mt_status)->mt_fileno >= 0)
      (STp->mt_status)->mt_fileno += 1;
    (STp->mt_status)->mt_blkno = 0;
  }

  return (STp->buffer)->last_result_fatal;
}


/* Flush the write buffer (never need to write if variable blocksize). */
	static int
flush_write_buffer(Scsi_Tape *STp)
{
  int offset, transfer, blks;
  int result;
  unsigned char cmd[10];
  Scsi_Cmnd *SCpnt;

  if ((STp->buffer)->writing) {
    write_behind_check(STp);
    if ((STp->buffer)->last_result_fatal) {
#if DEBUG
      if (debugging)
	printk("st%d: Async write error (flush) %x.\n", TAPE_NR(STp->devt),
	       (STp->buffer)->last_result);
#endif
      if ((STp->buffer)->last_result == INT_MAX)
	return (-ENOSPC);
      return (-EIO);
    }
  }

  if (STp->block_size == 0)
    return 0;

  result = 0;
  if (STp->dirty == 1) {

    offset = (STp->buffer)->buffer_bytes;
    transfer = ((offset + STp->block_size - 1) /
		STp->block_size) * STp->block_size;
#if DEBUG
    if (debugging)
      printk("st%d: Flushing %d bytes.\n", TAPE_NR(STp->devt), transfer);
#endif
    memset((STp->buffer)->b_data + offset, 0, transfer - offset);

    memset(cmd, 0, 10);
    cmd[0] = WRITE_6;
    cmd[1] = 1;
    blks = transfer / STp->block_size;
    cmd[2] = blks >> 16;
    cmd[3] = blks >> 8;
    cmd[4] = blks;

    SCpnt = st_do_scsi(NULL, STp, cmd, transfer, ST_TIMEOUT, MAX_WRITE_RETRIES);
    if (!SCpnt)
      return (-EBUSY);

    if ((STp->buffer)->last_result_fatal != 0) {
      printk("st%d: Error on flush.\n", TAPE_NR(STp->devt));
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
    SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
  }
  return result;
}


/* Flush the tape buffer. The tape will be positioned correctly unless
   seek_next is true. */
	static int
flush_buffer(struct inode * inode, struct file * filp, int seek_next)
{
  int backspace, result;
  Scsi_Tape * STp;
  ST_buffer * STbuffer;
  int dev = TAPE_NR(inode->i_rdev);

  STp = &(scsi_tapes[dev]);
  STbuffer = STp->buffer;

  /*
   * If there was a bus reset, block further access
   * to this device.
   */
  if( STp->device->was_reset )
    return (-EIO);

  if (STp->ready != ST_READY)
    return 0;

  if (STp->rw == ST_WRITING)  /* Writing */
    return flush_write_buffer(STp);

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
      result = back_over_eof(STp); /* Back over the EOF hit */
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
    unsigned short flags;
    int i;
    unsigned char cmd[10];
    Scsi_Cmnd * SCpnt;
    Scsi_Tape * STp;
    int dev = TAPE_NR(inode->i_rdev);

    if (dev >= st_template.dev_max || !scsi_tapes[dev].device)
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
      STp->buffer = new_tape_buffer(FALSE);
      if (STp->buffer == NULL) {
	printk("st%d: No free buffers.\n", dev);
	return (-EBUSY);
      }
    }
    else
      STp->buffer = st_buffers[i];
    (STp->buffer)->in_use = 1;
    (STp->buffer)->writing = 0;
    STp->in_use = 1;

    flags = filp->f_flags;
    STp->write_prot = ((flags & O_ACCMODE) == O_RDONLY);

    STp->dirty = 0;
    STp->rw = ST_IDLE;
    STp->ready = ST_READY;
    if (STp->eof != ST_EOD)  /* Save EOD across opens */
      STp->eof = ST_NOEOF;
    STp->eof_hit = 0;
    STp->recover_count = 0;
#if DEBUG
    STp->nbr_waits = STp->nbr_finished = 0;
#endif

    memset ((void *) &cmd[0], 0, 10);
    cmd[0] = TEST_UNIT_READY;

    SCpnt = st_do_scsi(NULL, STp, cmd, 0, ST_LONG_TIMEOUT, MAX_READY_RETRIES);
    if (!SCpnt)
      return (-EBUSY);

    if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	(SCpnt->sense_buffer[2] & 0x0f) == UNIT_ATTENTION) { /* New media? */
      (STp->mt_status)->mt_fileno = 0 ;
      memset ((void *) &cmd[0], 0, 10);
      cmd[0] = TEST_UNIT_READY;

      SCpnt = st_do_scsi(SCpnt, STp, cmd, 0, ST_LONG_TIMEOUT, MAX_READY_RETRIES);

      (STp->mt_status)->mt_fileno = STp->drv_block = 0;
      STp->eof = ST_NOEOF;
      (STp->device)->was_reset = 0;
    }

    if ((STp->buffer)->last_result_fatal != 0) {
      if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	  (SCpnt->sense_buffer[2] & 0x0f) == NO_TAPE) {
	(STp->mt_status)->mt_fileno = STp->drv_block = 0 ;
	printk("st%d: No tape.\n", dev);
	STp->ready = ST_NO_TAPE;
      } else {
	(STp->mt_status)->mt_fileno = STp->drv_block = (-1);
	STp->ready = ST_NOT_READY;
      }
      SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
      STp->density = 0;   	/* Clear the erroneous "residue" */
      STp->write_prot = 0;
      STp->block_size = 0;
      STp->eof = ST_NOEOF;
      (STp->mt_status)->mt_fileno = STp->drv_block = 0;
      STp->door_locked = ST_UNLOCKED;
      if (scsi_tapes[dev].device->host->hostt->usage_count)
	(*scsi_tapes[dev].device->host->hostt->usage_count)++;
      if(st_template.usage_count) (*st_template.usage_count)++;
      return 0;
    }

    memset ((void *) &cmd[0], 0, 10);
    cmd[0] = READ_BLOCK_LIMITS;

    SCpnt = st_do_scsi(SCpnt, STp, cmd, 6, ST_TIMEOUT, MAX_READY_RETRIES);

    if (!SCpnt->result && !SCpnt->sense_buffer[0]) {
      STp->max_block = ((STp->buffer)->b_data[1] << 16) |
	((STp->buffer)->b_data[2] << 8) | (STp->buffer)->b_data[3];
      STp->min_block = ((STp->buffer)->b_data[4] << 8) |
	(STp->buffer)->b_data[5];
#if DEBUG
      if (debugging)
	printk("st%d: Block limits %d - %d bytes.\n", dev, STp->min_block,
	       STp->max_block);
#endif
    }
    else {
      STp->min_block = STp->max_block = (-1);
#if DEBUG
      if (debugging)
	printk("st%d: Can't read block limits.\n", dev);
#endif
    }

    memset ((void *) &cmd[0], 0, 10);
    cmd[0] = MODE_SENSE;
    cmd[4] = 12;

    SCpnt = st_do_scsi(SCpnt, STp, cmd, 12, ST_TIMEOUT, MAX_READY_RETRIES);

    if ((STp->buffer)->last_result_fatal != 0) {
#if DEBUG
      if (debugging)
	printk("st%d: No Mode Sense.\n", dev);
#endif
      STp->block_size = ST_DEFAULT_BLOCK;  /* Educated guess (?) */
      (STp->buffer)->last_result_fatal = 0;  /* Prevent error propagation */
      STp->drv_write_prot = 0;
    }
    else {

#if DEBUG
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
#if DEBUG
	if (debugging)
	  printk("st%d: Density %x, tape length: %x, drv buffer: %d\n",
		 dev, STp->density, (STp->buffer)->b_data[5] * 65536 +
		 (STp->buffer)->b_data[6] * 256 + (STp->buffer)->b_data[7],
		 STp->drv_buffer);
#endif
      }

      if (STp->block_size > (STp->buffer)->buffer_size &&
	  !enlarge_buffer(STp->buffer, STp->block_size)) {
	printk("st%d: Blocksize %d too large for buffer.\n", dev,
	       STp->block_size);
	(STp->buffer)->in_use = 0;
	STp->in_use = 0;
	return (-EIO);
      }
      STp->drv_write_prot = ((STp->buffer)->b_data[2] & 0x80) != 0;
    }
    SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

    if (STp->block_size > 0)
      (STp->buffer)->buffer_blocks = st_buffer_size / STp->block_size;
    else
      (STp->buffer)->buffer_blocks = 1;
    (STp->buffer)->buffer_bytes = (STp->buffer)->read_pointer = 0;

#if DEBUG
    if (debugging)
      printk("st%d: Block size: %d, buffer size: %d (%d blocks).\n", dev,
	     STp->block_size, (STp->buffer)->buffer_size,
	     (STp->buffer)->buffer_blocks);
#endif

    if (STp->drv_write_prot) {
      STp->write_prot = 1;
#if DEBUG
      if (debugging)
	printk( "st%d: Write protected\n", dev);
#endif
      if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
	(STp->buffer)->in_use = 0;
	STp->buffer = 0;
	STp->in_use = 0;
	return (-EROFS);
      }
    }

    if (scsi_tapes[dev].device->host->hostt->usage_count)
      (*scsi_tapes[dev].device->host->hostt->usage_count)++;
    if(st_template.usage_count) (*st_template.usage_count)++;

    return 0;
}


/* Close the device*/
	static void
scsi_tape_close(struct inode * inode, struct file * filp)
{
    int result;
    int rewind;
    static unsigned char cmd[10];
    Scsi_Cmnd * SCpnt;
    Scsi_Tape * STp;
    kdev_t devt = inode->i_rdev;
    int dev;

    dev = TAPE_NR(devt);
    rewind = (MINOR(devt) & 0x80) == 0;

    STp = &(scsi_tapes[dev]);

    if ( STp->rw == ST_WRITING && !(STp->device)->was_reset) {

      result = flush_write_buffer(STp);

#if DEBUG
      if (debugging) {
	printk("st%d: File length %ld bytes.\n", dev, (long)(filp->f_pos));
	printk("st%d: Async write waits %d, finished %d.\n", dev,
	       STp->nbr_waits, STp->nbr_finished);
      }
#endif

      if (result == 0 || result == (-ENOSPC)) {

	memset(cmd, 0, 10);
	cmd[0] = WRITE_FILEMARKS;
	cmd[4] = 1 + STp->two_fm;

	SCpnt = st_do_scsi(NULL, STp, cmd, 0, ST_TIMEOUT, MAX_WRITE_RETRIES);
	if (!SCpnt)
	  return;

	SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

	if ((STp->buffer)->last_result_fatal != 0)
	  printk("st%d: Error on write filemark.\n", dev);
	else {
	  if ((STp->mt_status)->mt_fileno >= 0)
	      (STp->mt_status)->mt_fileno++ ;
	  STp->drv_block = 0;
	  if (STp->two_fm)
	    back_over_eof(STp);
	}
      }

#if DEBUG
      if (debugging)
	printk("st%d: Buffer flushed, %d EOF(s) written\n", dev, cmd[4]);
#endif
    }
    else if (!rewind) {
#if ST_IN_FILE_POS
      flush_buffer(inode, filp, 0);
#else
      if ((STp->eof == ST_FM) && !STp->eof_hit)
	back_over_eof(STp);
#endif
    }

    if (rewind)
      st_int_ioctl(inode, filp, MTREW, 1);

    if (STp->door_locked == ST_LOCKED_AUTO)
      st_int_ioctl(inode, filp, MTUNLOCK, 0);

    if (STp->buffer != NULL) {
      normalize_buffer(STp->buffer);
      (STp->buffer)->in_use = 0;
    }
    STp->in_use = 0;

    if (scsi_tapes[dev].device->host->hostt->usage_count)
      (*scsi_tapes[dev].device->host->hostt->usage_count)--;
    if(st_template.usage_count) (*st_template.usage_count)--;

    return;
}


/* Write command */
	static int
st_write(struct inode * inode, struct file * filp, const char * buf, int count)
{
    int total, do_count, blks, retval, transfer;
    int write_threshold;
    int doing_write = 0;
    static unsigned char cmd[10];
    const char *b_point;
    Scsi_Cmnd * SCpnt = NULL;
    Scsi_Tape * STp;
    int dev = TAPE_NR(inode->i_rdev);

    STp = &(scsi_tapes[dev]);
    if (STp->ready != ST_READY)
      return (-EIO);

    /*
     * If there was a bus reset, block further access
     * to this device.
     */
    if( STp->device->was_reset )
      return (-EIO);

#if DEBUG
    if (!STp->in_use) {
      printk("st%d: Incorrect device.\n", dev);
      return (-EIO);
    }
#endif

    if (STp->write_prot)
      return (-EACCES);

    if (STp->block_size == 0 &&
	count > (STp->buffer)->buffer_size &&
	!enlarge_buffer(STp->buffer, count))
      return (-EOVERFLOW);

    if (STp->do_auto_lock && STp->door_locked == ST_UNLOCKED &&
	!st_int_ioctl(inode, filp, MTLOCK, 0))
      STp->door_locked = ST_LOCKED_AUTO;

    if (STp->rw == ST_READING) {
      retval = flush_buffer(inode, filp, 0);
      if (retval)
	return retval;
      STp->rw = ST_WRITING;
    }

    if (STp->moves_after_eof < 255)
      STp->moves_after_eof++;

    if ((STp->buffer)->writing) {
      write_behind_check(STp);
      if ((STp->buffer)->last_result_fatal) {
#if DEBUG
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
      write_threshold = (STp->buffer)->buffer_blocks * STp->block_size;
    if (!STp->do_async_writes)
      write_threshold--;

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
      doing_write = 1;
      if (STp->block_size == 0)
	do_count = count;
      else {
	do_count = (STp->buffer)->buffer_blocks * STp->block_size -
	  (STp->buffer)->buffer_bytes;
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

      SCpnt = st_do_scsi(SCpnt, STp, cmd, transfer, ST_TIMEOUT, MAX_WRITE_RETRIES);
      if (!SCpnt)
	return (-EBUSY);

      if ((STp->buffer)->last_result_fatal != 0) {
#if DEBUG
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
#if DEBUG
	    if (debugging)
	      printk("st%d: EOM with %d bytes unwritten.\n",
		     dev, transfer);
#endif
	  }
	  else {
	    STp->eof = ST_EOM_ERROR;
	    STp->drv_block = (-1);    /* Too cautious? */
	    retval = (-EIO); /* EOM for old data */
#if DEBUG
	    if (debugging)
	      printk("st%d: EOM with lost data.\n", dev);
#endif
	  }
	}
	else {
	  STp->drv_block = (-1);    /* Too cautious? */
	  retval = (-EIO);
	}

	SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
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

    if (doing_write && (STp->buffer)->last_result_fatal != 0) {
      SCpnt->request.rq_status = RQ_INACTIVE;
      return (STp->buffer)->last_result_fatal;
    }

    if (STp->do_async_writes &&
	((STp->buffer)->buffer_bytes >= STp->write_threshold ||
	 STp->block_size == 0) ) {
      /* Schedule an asynchronous write */
      if (!SCpnt) {
	SCpnt = allocate_device(NULL, STp->device, 1);
	if (!SCpnt)
	  return (-EBUSY);
      }
      if (STp->block_size == 0)
	(STp->buffer)->writing = (STp->buffer)->buffer_bytes;
      else
	(STp->buffer)->writing = ((STp->buffer)->buffer_bytes /
	  STp->block_size) * STp->block_size;
      STp->dirty = !((STp->buffer)->writing ==
		     (STp->buffer)->buffer_bytes);

      if (STp->block_size == 0)
	blks = (STp->buffer)->writing;
      else
	blks = (STp->buffer)->writing / STp->block_size;
      cmd[2] = blks >> 16;
      cmd[3] = blks >> 8;
      cmd[4] = blks;
      STp->sem = MUTEX_LOCKED;
      SCpnt->request.sem = &(STp->sem);
      SCpnt->request.rq_status = RQ_SCSI_BUSY;
      SCpnt->request.rq_dev = STp->devt;
#if DEBUG
      STp->write_pending = 1;
#endif

      scsi_do_cmd (SCpnt,
		   (void *) cmd, (STp->buffer)->b_data,
		   (STp->buffer)->writing,
		   st_sleep_done, ST_TIMEOUT, MAX_WRITE_RETRIES);
    }
    else if (SCpnt != NULL)
      SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

    STp->at_sm &= (total != 0);
    return( total);
}   


/* Read command */
	static int
st_read(struct inode * inode, struct file * filp, char * buf, int count)
{
    int total;
    int transfer, blks, bytes;
    static unsigned char cmd[10];
    Scsi_Cmnd * SCpnt = NULL;
    Scsi_Tape * STp;
    int dev = TAPE_NR(inode->i_rdev);

    STp = &(scsi_tapes[dev]);
    if (STp->ready != ST_READY)
      return (-EIO);
#if DEBUG
    if (!STp->in_use) {
      printk("st%d: Incorrect device.\n", dev);
      return (-EIO);
    }
#endif

    if (STp->block_size == 0 &&
	count > (STp->buffer)->buffer_size &&
	!enlarge_buffer(STp->buffer, count))
      return (-EOVERFLOW);

    if (!(STp->do_read_ahead) && STp->block_size != 0 &&
	(count % STp->block_size) != 0)
      return (-EIO);	/* Read must be integral number of blocks */

    if (STp->do_auto_lock && STp->door_locked == ST_UNLOCKED &&
	!st_int_ioctl(inode, filp, MTLOCK, 0))
      STp->door_locked = ST_LOCKED_AUTO;

    if (STp->rw == ST_WRITING) {
      transfer = flush_buffer(inode, filp, 0);
      if (transfer)
	return transfer;
      STp->rw = ST_READING;
    }
    if (STp->moves_after_eof < 255)
      STp->moves_after_eof++;

#if DEBUG
    if (debugging && STp->eof != ST_NOEOF)
      printk("st%d: EOF flag up. Bytes %d\n", dev,
	     (STp->buffer)->buffer_bytes);
#endif
    if (((STp->buffer)->buffer_bytes == 0) &&
	(STp->eof == ST_EOM_OK || STp->eof == ST_EOD))
      return (-EIO);  /* EOM or Blank Check */

    STp->rw = ST_READING;

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
	    if (bytes > (STp->buffer)->buffer_size)
	      bytes = (STp->buffer)->buffer_size;
	    blks = bytes / STp->block_size;
	    bytes = blks * STp->block_size;
	  }
	}
	cmd[2] = blks >> 16;
	cmd[3] = blks >> 8;
	cmd[4] = blks;

	SCpnt = st_do_scsi(SCpnt, STp, cmd, bytes, ST_TIMEOUT, MAX_RETRIES);
	if (!SCpnt)
	  return (-EBUSY);

	(STp->buffer)->read_pointer = 0;
	STp->eof_hit = 0;
	STp->at_sm = 0;

	if ((STp->buffer)->last_result_fatal) {
#if DEBUG
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
		  SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
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
#if DEBUG
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
#if DEBUG
		if (debugging)
		  printk(
		    "st%d: EOF detected (%d bytes read, transferred %d bytes).\n",
			 dev, (STp->buffer)->buffer_bytes, total);
#endif
	      }
	    } /* end of EOF, EOM, ILI test */
	    else { /* nonzero sense key */
#if DEBUG
	      if (debugging)
		printk("st%d: Tape error while reading.\n", dev);
#endif
	      SCpnt->request.rq_status = RQ_INACTIVE;
	      STp->drv_block = (-1);
	      if (total)
		return total;
	      else if (STp->moves_after_eof == 1 &&
		       (SCpnt->sense_buffer[2] & 0x0f) == BLANK_CHECK) {
#if DEBUG
		if (debugging)
		  printk("st%d: Zero returned for first BLANK CHECK after EOF.\n",
			 dev);
#endif
		STp->eof = ST_EOD;
		return 0; /* First BLANK_CHECK after EOF */
	      }
	      else
		return -EIO;
	    }
	  } /* End of extended sense test */
	  else {
	    transfer = (STp->buffer)->last_result_fatal;
	    SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
	    return transfer;
	  }
	} /* End of error handling */
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
#if DEBUG
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
	if (SCpnt != NULL)
	  SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
	if (total == 0 && STp->eof == ST_FM) {
	  STp->eof = ST_NOEOF;
	  STp->drv_block = 0;
	  if (STp->moves_after_eof > 1)
	    STp->moves_after_eof = 0;
	  if ((STp->mt_status)->mt_fileno >= 0)
	    (STp->mt_status)->mt_fileno++;
	}
	if (total == 0 && STp->eof == ST_EOM_OK)
	  return (-EIO);  /* ST_EOM_ERROR not used in read */
	return total;
      }

      if (STp->block_size == 0)
	count = total;  /* Read only one variable length block */

    } /* for (total = 0; total < count; ) */

    if (SCpnt != NULL)
      SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

    return total;
}



/* Set the driver options */
	static int
st_set_options(struct inode * inode, long options)
{
  int value;
  Scsi_Tape *STp;
  int dev = TAPE_NR(inode->i_rdev);

  STp = &(scsi_tapes[dev]);
  if ((options & MT_ST_OPTIONS) == MT_ST_BOOLEANS) {
    STp->do_buffer_writes = (options & MT_ST_BUFFER_WRITES) != 0;
    STp->do_async_writes  = (options & MT_ST_ASYNC_WRITES) != 0;
    STp->do_read_ahead    = (options & MT_ST_READ_AHEAD) != 0;
    STp->two_fm		  = (options & MT_ST_TWO_FM) != 0;
    STp->fast_mteom	  = (options & MT_ST_FAST_MTEOM) != 0;
    STp->do_auto_lock     = (options & MT_ST_AUTO_LOCK) != 0;
#if DEBUG
    debugging = (options & MT_ST_DEBUGGING) != 0;
    printk(
"st%d: options: buffer writes: %d, async writes: %d, read ahead: %d\n",
	   dev, STp->do_buffer_writes, STp->do_async_writes,
	   STp->do_read_ahead);
    printk("              two FMs: %d, fast mteom: %d auto lock: %d, debugging: %d\n",
	   STp->two_fm, STp->fast_mteom, STp->do_auto_lock, debugging);
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
#if DEBUG
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
   int timeout = ST_LONG_TIMEOUT;
   long ltmp;
   int ioctl_result;
   unsigned char cmd[10];
   Scsi_Cmnd * SCpnt;
   Scsi_Tape * STp;
   int fileno, blkno, at_sm, undone, datalen;
   int dev = TAPE_NR(inode->i_rdev);

   STp = &(scsi_tapes[dev]);
   if (STp->ready != ST_READY && cmd_in != MTLOAD)
     return (-EIO);
   fileno = (STp->mt_status)->mt_fileno ;
   blkno = STp->drv_block;
   at_sm = STp->at_sm;

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
#if DEBUG
       if (debugging)
	 printk("st%d: Spacing tape forward over %d filemarks.\n", dev,
		cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
#endif
       if (fileno >= 0)
	 fileno += arg;
       blkno = 0;
       at_sm &= (arg != 0);
       break; 
     case MTBSF:
     case MTBSFM:
       cmd[0] = SPACE;
       cmd[1] = 0x01; /* Space FileMarks */
       ltmp = (-arg);
       cmd[2] = (ltmp >> 16);
       cmd[3] = (ltmp >> 8);
       cmd[4] = ltmp;
#if DEBUG
       if (debugging) {
	 if (cmd[2] & 0x80)
	   ltmp = 0xff000000;
	 ltmp = ltmp | (cmd[2] << 16) | (cmd[3] << 8) | cmd[4];
	 printk("st%d: Spacing tape backward over %ld filemarks.\n", dev, (-ltmp));
       }
#endif
       if (fileno >= 0)
	 fileno -= arg;
       blkno = (-1);  /* We can't know the block number */
       at_sm &= (arg != 0);
       break; 
     case MTFSR:
       cmd[0] = SPACE;
       cmd[1] = 0x00; /* Space Blocks */
       cmd[2] = (arg >> 16);
       cmd[3] = (arg >> 8);
       cmd[4] = arg;
#if DEBUG
       if (debugging)
	 printk("st%d: Spacing tape forward %d blocks.\n", dev,
		cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
#endif
       if (blkno >= 0)
	 blkno += arg;
       at_sm &= (arg != 0);
       break; 
     case MTBSR:
       cmd[0] = SPACE;
       cmd[1] = 0x00; /* Space Blocks */
       ltmp = (-arg);
       cmd[2] = (ltmp >> 16);
       cmd[3] = (ltmp >> 8);
       cmd[4] = ltmp;
#if DEBUG
       if (debugging) {
	 if (cmd[2] & 0x80)
	   ltmp = 0xff000000;
	 ltmp = ltmp | (cmd[2] << 16) | (cmd[3] << 8) | cmd[4];
	 printk("st%d: Spacing tape backward %ld blocks.\n", dev, (-ltmp));
       }
#endif
       if (blkno >= 0)
	 blkno -= arg;
       at_sm &= (arg != 0);
       break; 
     case MTFSS:
       cmd[0] = SPACE;
       cmd[1] = 0x04; /* Space Setmarks */
       cmd[2] = (arg >> 16);
       cmd[3] = (arg >> 8);
       cmd[4] = arg;
#if DEBUG
       if (debugging)
	 printk("st%d: Spacing tape forward %d setmarks.\n", dev,
		cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
#endif
       if (arg != 0) {
	 blkno = fileno = (-1);
	 at_sm = 1;
       }
       break; 
     case MTBSS:
       cmd[0] = SPACE;
       cmd[1] = 0x04; /* Space Setmarks */
       ltmp = (-arg);
       cmd[2] = (ltmp >> 16);
       cmd[3] = (ltmp >> 8);
       cmd[4] = ltmp;
#if DEBUG
       if (debugging) {
	 if (cmd[2] & 0x80)
	   ltmp = 0xff000000;
	 ltmp = ltmp | (cmd[2] << 16) | (cmd[3] << 8) | cmd[4];
	 printk("st%d: Spacing tape backward %ld setmarks.\n", dev, (-ltmp));
       }
#endif
       if (arg != 0) {
	 blkno = fileno = (-1);
	 at_sm = 1;
       }
       break; 
     case MTWEOF:
     case MTWSM:
       if (STp->write_prot)
	 return (-EACCES);
       cmd[0] = WRITE_FILEMARKS;
       if (cmd_in == MTWSM)
	 cmd[1] = 2;
       cmd[2] = (arg >> 16);
       cmd[3] = (arg >> 8);
       cmd[4] = arg;
       timeout = ST_TIMEOUT;
#if DEBUG
       if (debugging) {
	 if (cmd_in == MTWEOF)
	   printk("st%d: Writing %d filemarks.\n", dev,
		  cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
	 else
	   printk("st%d: Writing %d setmarks.\n", dev,
		  cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
       }
#endif
       if (fileno >= 0)
	 fileno += arg;
       blkno = 0;
       at_sm = (cmd_in == MTWSM);
       break; 
     case MTREW:
       cmd[0] = REZERO_UNIT;
#if ST_NOWAIT
       cmd[1] = 1;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#endif
#if DEBUG
       if (debugging)
	 printk("st%d: Rewinding tape.\n", dev);
#endif
       fileno = blkno = at_sm = 0 ;
       break; 
     case MTOFFL:
       cmd[0] = START_STOP;
#if ST_NOWAIT
       cmd[1] = 1;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#endif
#if DEBUG
       if (debugging)
	 printk("st%d: Unloading tape.\n", dev);
#endif
       fileno = blkno = at_sm = 0 ;
       break; 
     case MTNOP:
#if DEBUG
       if (debugging)
	 printk("st%d: No op on tape.\n", dev);
#endif
       return 0;  /* Should do something ? */
       break;
     case MTRETEN:
       cmd[0] = START_STOP;
#if ST_NOWAIT
       cmd[1] = 1;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#endif
       cmd[4] = 3;
#if DEBUG
       if (debugging)
	 printk("st%d: Retensioning tape.\n", dev);
#endif
       fileno = blkno = at_sm = 0;
       break; 
     case MTEOM:
       if (!STp->fast_mteom) {
	 /* space to the end of tape */
	 ioctl_result = st_int_ioctl(inode, file, MTFSF, 0x3fff);
	 fileno = (STp->mt_status)->mt_fileno ;
	 if (STp->eof == ST_EOD || STp->eof == ST_EOM_OK)
	   return 0;
	 /* The next lines would hide the number of spaced FileMarks
	    That's why I inserted the previous lines. I had no luck
	    with detecting EOM with FSF, so we go now to EOM.
	    Joerg Weule */
       }
       else
	 fileno = (-1);
       cmd[0] = SPACE;
       cmd[1] = 3;
#if DEBUG
       if (debugging)
	 printk("st%d: Spacing to end of recorded medium.\n", dev);
#endif
       blkno = 0;
       at_sm = 0;
       break; 
     case MTERASE:
       if (STp->write_prot)
	 return (-EACCES);
       cmd[0] = ERASE;
       cmd[1] = 1;  /* To the end of tape */
#if ST_NOWAIT
       cmd[1] |= 2;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#else
       timeout = ST_LONG_TIMEOUT * 8;
#endif
#if DEBUG
       if (debugging)
	 printk("st%d: Erasing tape.\n", dev);
#endif
       fileno = blkno = at_sm = 0 ;
       break;
     case MTLOCK:
       cmd[0] = ALLOW_MEDIUM_REMOVAL;
       cmd[4] = SCSI_REMOVAL_PREVENT;
#if DEBUG
       if (debugging)
	 printk("st%d: Locking drive door.\n", dev);
#endif;
       break;
     case MTUNLOCK:
       cmd[0] = ALLOW_MEDIUM_REMOVAL;
       cmd[4] = SCSI_REMOVAL_ALLOW;
#if DEBUG
       if (debugging)
	 printk("st%d: Unlocking drive door.\n", dev);
#endif;
       break;
     case MTLOAD:
     case MTUNLOAD:
       cmd[0] = START_STOP;
       if (cmd_in == MTLOAD)
	 cmd[4] |= 1;
#if ST_NOWAIT
       cmd[1] |= 2;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#else
       timeout = ST_LONG_TIMEOUT * 8;
#endif
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
#if ST_NOWAIT
       cmd[1] |= 1;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#endif
#if DEBUG
       if (debugging)
	 printk("st%d: Seeking tape to block %ld.\n", dev, arg);
#endif
       fileno = blkno = (-1);
       at_sm = 0;
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
#if DEBUG
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

   SCpnt = st_do_scsi(NULL, STp, cmd, datalen, timeout, MAX_RETRIES);
   if (!SCpnt)
     return (-EBUSY);

   ioctl_result = (STp->buffer)->last_result_fatal;

   SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

   if (cmd_in == MTFSF)
     STp->moves_after_eof = 0;
   else
     STp->moves_after_eof = 1;
   if (!ioctl_result) {  /* SCSI command successful */
     if (cmd_in != MTSEEK) {
       STp->drv_block = blkno;
       (STp->mt_status)->mt_fileno = fileno;
       STp->at_sm = at_sm;
     }
     else {
       STp->drv_block = (STp->mt_status)->mt_fileno = (-1);
       STp->at_sm = 0;
     }
     if (cmd_in == MTLOCK)
       STp->door_locked = ST_LOCKED_EXPLICIT;
     else if (cmd_in == MTUNLOCK)
       STp->door_locked = ST_UNLOCKED;
     if (cmd_in == MTBSFM)
       ioctl_result = st_int_ioctl(inode, file, MTFSF, 1);
     else if (cmd_in == MTFSFM)
       ioctl_result = st_int_ioctl(inode, file, MTBSF, 1);
     else if (cmd_in == MTSETBLK) {
       STp->block_size = arg;
       if (arg != 0)
	 (STp->buffer)->buffer_blocks =
	   (STp->buffer)->buffer_size / STp->block_size;
       (STp->buffer)->buffer_bytes = (STp->buffer)->read_pointer = 0;
     }
     else if (cmd_in == MTSETDRVBUFFER)
       STp->drv_buffer = (arg & 7);
     else if (cmd_in == MTSETDENSITY)
       STp->density = arg;
     else if (cmd_in == MTEOM) {
       STp->eof = ST_EOD;
       STp->eof_hit = 0;
     }
     else if (cmd_in != MTSETBLK && cmd_in != MTNOP) {
       STp->eof = ST_NOEOF;
       STp->eof_hit = 0;
     }
   } else {  /* SCSI command was not completely successful */
     if (SCpnt->sense_buffer[2] & 0x40) {
       if (cmd_in != MTBSF && cmd_in != MTBSFM &&
	   cmd_in != MTBSR && cmd_in != MTBSS)
	 STp->eof = ST_EOM_OK;
       STp->eof_hit = 0;
       STp->drv_block = 0;
     }
     undone = (
	  (SCpnt->sense_buffer[3] << 24) +
	  (SCpnt->sense_buffer[4] << 16) +
	  (SCpnt->sense_buffer[5] << 8) +
	  SCpnt->sense_buffer[6] );
     if ( (cmd_in == MTFSF) || (cmd_in == MTFSFM) ) {
       if (fileno >= 0)
	 (STp->mt_status)->mt_fileno = fileno - undone ;
       else
	 (STp->mt_status)->mt_fileno = fileno;
       STp->drv_block = 0;
     }
     else if ( (cmd_in == MTBSF) || (cmd_in == MTBSFM) ) {
       (STp->mt_status)->mt_fileno = fileno + undone ;
       STp->drv_block = 0;
     }
     else if (cmd_in == MTFSR) {
       if (SCpnt->sense_buffer[2] & 0x80) { /* Hit filemark */
	 (STp->mt_status)->mt_fileno++;
	 STp->drv_block = 0;
       }
       else {
	 if (blkno >= undone)
	   STp->drv_block = blkno - undone;
	 else
	   STp->drv_block = (-1);
       }
     }
     else if (cmd_in == MTBSR) {
       if (SCpnt->sense_buffer[2] & 0x80) { /* Hit filemark */
	 (STp->mt_status)->mt_fileno--;
	 STp->drv_block = (-1);
       }
       else {
	 if (blkno >= 0)
	   STp->drv_block = blkno + undone;
	 else
	   STp->drv_block = (-1);
       }
     }
     else if (cmd_in == MTEOM || cmd_in == MTSEEK) {
       (STp->mt_status)->mt_fileno = (-1);
       STp->drv_block = (-1);
     }
     if (STp->eof == ST_NOEOF &&
	 (SCpnt->sense_buffer[2] & 0x0f) == BLANK_CHECK)
       STp->eof = ST_EOD;
     if (cmd_in == MTLOCK)
       STp->door_locked = ST_LOCK_FAILS;
   }

   return ioctl_result;
}



/* The ioctl command */
	static int
st_ioctl(struct inode * inode,struct file * file,
	 unsigned int cmd_in, unsigned long arg)
{
   int i, cmd_nr, cmd_type, result;
   struct mtop mtc;
   struct mtpos mt_pos;
   unsigned char scmd[10];
   Scsi_Cmnd *SCpnt;
   Scsi_Tape *STp;
   int dev = TAPE_NR(inode->i_rdev);

   STp = &(scsi_tapes[dev]);
#if DEBUG
   if (debugging && !STp->in_use) {
     printk("st%d: Incorrect device.\n", dev);
     return (-EIO);
   }
#endif

   /*
    * If this is something intended for the lower layer, just pass it
    * through.
    */
   if( cmd_in == SCSI_IOCTL_GET_IDLUN || cmd_in == SCSI_IOCTL_PROBE_HOST )
     {
       return scsi_ioctl(STp->device, cmd_in, (void *) arg);
     }
   
   cmd_type = _IOC_TYPE(cmd_in);
   cmd_nr   = _IOC_NR(cmd_in);
   if (cmd_type == _IOC_TYPE(MTIOCTOP) && cmd_nr == _IOC_NR(MTIOCTOP)) {
     if (_IOC_SIZE(cmd_in) != sizeof(mtc))
       return (-EINVAL);

     i = verify_area(VERIFY_READ, (void *)arg, sizeof(mtc));
     if (i)
	return i;

     memcpy_fromfs((char *) &mtc, (char *)arg, sizeof(struct mtop));

     if (!(STp->device)->was_reset) {
       i = flush_buffer(inode, file, mtc.mt_op == MTSEEK ||
			mtc.mt_op == MTREW || mtc.mt_op == MTOFFL ||
			mtc.mt_op == MTRETEN || mtc.mt_op == MTEOM ||
			mtc.mt_op == MTLOCK || mtc.mt_op == MTLOAD);
       if (i < 0)
	 return i;
     }
     else {
       /*
	* If there was a bus reset, block further access
	* to this device.  If the user wants to rewind the tape,
	* then reset the flag and allow access again.
	*/
       if(mtc.mt_op != MTREW && 
	  mtc.mt_op != MTOFFL &&
	  mtc.mt_op != MTRETEN && 
	  mtc.mt_op != MTERASE &&
	  mtc.mt_op != MTSEEK &&
	  mtc.mt_op != MTEOM)
	 return (-EIO);
       STp->device->was_reset = 0;
       if (STp->door_locked != ST_UNLOCKED &&
	   STp->door_locked != ST_LOCK_FAILS) {
	 if (st_int_ioctl(inode, file, MTLOCK, 0)) {
	   printk("st%d: Could not relock door after bus reset.\n", dev);
	   STp->door_locked = ST_UNLOCKED;
	 }
       }
     }

     if (mtc.mt_op != MTNOP && mtc.mt_op != MTSETBLK &&
	 mtc.mt_op != MTSETDENSITY && mtc.mt_op != MTWSM &&
	 mtc.mt_op != MTSETDRVBUFFER)
       STp->rw = ST_IDLE;  /* Prevent automatic WEOF */

     if (mtc.mt_op == MTOFFL && STp->door_locked != ST_UNLOCKED)
       st_int_ioctl(inode, file, MTUNLOCK, 0);  /* Ignore result! */

     if (mtc.mt_op == MTSETDRVBUFFER &&
	 (mtc.mt_count & MT_ST_OPTIONS) != 0)
       return st_set_options(inode, mtc.mt_count);
     else
       return st_int_ioctl(inode, file, mtc.mt_op, mtc.mt_count);
   }
   else if (cmd_type == _IOC_TYPE(MTIOCGET) && cmd_nr == _IOC_NR(MTIOCGET)) {

     if (_IOC_SIZE(cmd_in) != sizeof(struct mtget))
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

     (STp->mt_status)->mt_gstat = 0;
     if (STp->drv_write_prot)
       (STp->mt_status)->mt_gstat |= GMT_WR_PROT(0xffffffff);
     if ((STp->mt_status)->mt_blkno == 0) {
       if ((STp->mt_status)->mt_fileno == 0)
	 (STp->mt_status)->mt_gstat |= GMT_BOT(0xffffffff);
       else
	 (STp->mt_status)->mt_gstat |= GMT_EOF(0xffffffff);
     }
     if (STp->eof == ST_EOM_OK || STp->eof == ST_EOM_ERROR)
       (STp->mt_status)->mt_gstat |= GMT_EOT(0xffffffff);
     else if (STp->eof == ST_EOD)
       (STp->mt_status)->mt_gstat |= GMT_EOD(0xffffffff);
     if (STp->density == 1)
       (STp->mt_status)->mt_gstat |= GMT_D_800(0xffffffff);
     else if (STp->density == 2)
       (STp->mt_status)->mt_gstat |= GMT_D_1600(0xffffffff);
     else if (STp->density == 3)
       (STp->mt_status)->mt_gstat |= GMT_D_6250(0xffffffff);
     if (STp->ready == ST_READY)
       (STp->mt_status)->mt_gstat |= GMT_ONLINE(0xffffffff);
     if (STp->ready == ST_NO_TAPE)
       (STp->mt_status)->mt_gstat |= GMT_DR_OPEN(0xffffffff);
     if (STp->at_sm)
       (STp->mt_status)->mt_gstat |= GMT_SM(0xffffffff);

     memcpy_tofs((char *)arg, (char *)(STp->mt_status),
		 sizeof(struct mtget));

     (STp->mt_status)->mt_erreg = 0;  /* Clear after read */
     return 0;
   }
   else if (cmd_type == _IOC_TYPE(MTIOCPOS) && cmd_nr == _IOC_NR(MTIOCPOS)) {
     if (STp->ready != ST_READY)
       return (-EIO);
#if DEBUG
     if (debugging)
       printk("st%d: get tape position.\n", dev);
#endif
     if (_IOC_SIZE(cmd_in) != sizeof(struct mtpos))
       return (-EINVAL);

     i = flush_buffer(inode, file, 0);
     if (i < 0)
       return i;

     i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct mtpos));
     if (i)
       return i;

     memset (scmd, 0, 10);
     if ((STp->device)->scsi_level < SCSI_2) {
       scmd[0] = QFA_REQUEST_BLOCK;
       scmd[4] = 3;
     }
     else {
       scmd[0] = READ_POSITION;
       scmd[1] = 1;
     }
     SCpnt = st_do_scsi(NULL, STp, scmd, 20, ST_TIMEOUT, MAX_READY_RETRIES);
     if (!SCpnt)
       return (-EBUSY);

     if ((STp->buffer)->last_result_fatal != 0) {
       mt_pos.mt_blkno = (-1);
#if DEBUG
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

     SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

     memcpy_tofs((char *)arg, (char *) (&mt_pos), sizeof(struct mtpos));
     return result;
   }
   else
     return scsi_ioctl(STp->device, cmd_in, (void *) arg);
}


/* Try to allocate a new tape buffer */
	static ST_buffer *
new_tape_buffer( int from_initialization )
{
  int priority, a_size;
  ST_buffer *tb;

  if (st_nbr_buffers >= st_template.dev_max)
    return NULL;  /* Should never happen */

  if (from_initialization) {
    priority = GFP_ATOMIC | GFP_DMA;
    a_size = st_buffer_size;
  }
  else {
    priority = GFP_KERNEL | GFP_DMA;
    for (a_size = PAGE_SIZE; a_size < st_buffer_size; a_size <<= 1)
      ; /* Make sure we allocate efficiently */
  }
  tb = (ST_buffer *)scsi_init_malloc(sizeof(ST_buffer), priority);
  if (tb) {
    tb->b_data = (unsigned char *)scsi_init_malloc(a_size, priority);
    if (!tb->b_data) {
      scsi_init_free((char *)tb, sizeof(ST_buffer));
      tb = NULL;
    }
  }
  if (!tb) {
    printk("st: Can't allocate new tape buffer (nbr %d).\n", st_nbr_buffers);
    return NULL;
  }

#if DEBUG
  if (debugging)
    printk("st: Allocated tape buffer %d (%d bytes).\n", st_nbr_buffers,
	   a_size);
#endif
  tb->in_use = 0;
  tb->buffer_size = a_size;
  tb->writing = 0;
  tb->orig_b_data = NULL;
  st_buffers[st_nbr_buffers++] = tb;
  return tb;
}


/* Try to allocate a temporary enlarged tape buffer */
	static int
enlarge_buffer(ST_buffer *STbuffer, int new_size)
{
  int a_size;
  unsigned char *tbd;

  normalize_buffer(STbuffer);

  for (a_size = PAGE_SIZE; a_size < new_size; a_size <<= 1)
    ;  /* Make sure that we allocate efficiently */

  tbd = (unsigned char *)scsi_init_malloc(a_size, GFP_DMA | GFP_KERNEL);
  if (!tbd)
    return FALSE;

#if DEBUG
  if (debugging)
    printk("st: Buffer enlarged to %d bytes.\n", a_size);
#endif

  STbuffer->orig_b_data = STbuffer->b_data;
  STbuffer->orig_size = STbuffer->buffer_size;
  STbuffer->b_data = tbd;
  STbuffer->buffer_size = a_size;
  return TRUE;
}


/* Release the extra buffer */
	static void
normalize_buffer(ST_buffer *STbuffer)
{
  if (STbuffer->orig_b_data == NULL)
    return;

  scsi_init_free(STbuffer->b_data, STbuffer->buffer_size);
  STbuffer->b_data = STbuffer->orig_b_data;
  STbuffer->orig_b_data = NULL;
  STbuffer->buffer_size = STbuffer->orig_size;

#if DEBUG
  if (debugging)
    printk("st: Buffer normalized to %d bytes.\n", STbuffer->buffer_size);
#endif
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

static int st_attach(Scsi_Device * SDp){
   Scsi_Tape * tpnt;
   int i;

   if(SDp->type != TYPE_TAPE) return 1;

   if(st_template.nr_dev >= st_template.dev_max) 
     {
     	SDp->attached--;
     	return 1;
     }

   for(tpnt = scsi_tapes, i=0; i<st_template.dev_max; i++, tpnt++) 
     if(!tpnt->device) break;

   if(i >= st_template.dev_max) panic ("scsi_devices corrupt (st)");

   scsi_tapes[i].device = SDp;
   if (SDp->scsi_level <= 2)
     scsi_tapes[i].mt_status->mt_type = MT_ISSCSI1;
   else
     scsi_tapes[i].mt_status->mt_type = MT_ISSCSI2;

   tpnt->devt = MKDEV(SCSI_TAPE_MAJOR, i);
   tpnt->dirty = 0;
   tpnt->rw = ST_IDLE;
   tpnt->eof = ST_NOEOF;
   tpnt->waiting = NULL;
   tpnt->in_use = 0;
   tpnt->drv_buffer = 1;  /* Try buffering if no mode sense */
   tpnt->density = 0;
   tpnt->do_buffer_writes = ST_BUFFER_WRITES;
   tpnt->do_async_writes = ST_ASYNC_WRITES;
   tpnt->do_read_ahead = ST_READ_AHEAD;
   tpnt->do_auto_lock = ST_AUTO_LOCK;
   tpnt->two_fm = ST_TWO_FM;
   tpnt->fast_mteom = ST_FAST_MTEOM;
   tpnt->write_threshold = st_write_threshold;
   tpnt->drv_block = 0;
   tpnt->moves_after_eof = 1;
   tpnt->at_sm = 0;

   st_template.nr_dev++;
   return 0;
};

static int st_detect(Scsi_Device * SDp)
{
  if(SDp->type != TYPE_TAPE) return 0;

  printk("Detected scsi tape st%d at scsi%d, channel %d, id %d, lun %d\n", 
	 st_template.dev_noticed++,
	 SDp->host->host_no, SDp->channel, SDp->id, SDp->lun); 
  
  return 1;
}

static int st_registered = 0;

/* Driver initialization */
static int st_init()
{
  int i;
  Scsi_Tape * STp;
#if !ST_RUNTIME_BUFFERS
  int target_nbr;
#endif

  if (st_template.dev_noticed == 0) return 0;

  if(!st_registered) {
    if (register_chrdev(SCSI_TAPE_MAJOR,"st",&st_fops)) {
      printk("Unable to get major %d for SCSI tapes\n",MAJOR_NR);
      return 1;
    }
    st_registered++;
  }

  if (scsi_tapes) return 0;
  st_template.dev_max = st_template.dev_noticed + ST_EXTRA_DEVS;
  if (st_template.dev_max < ST_MAX_TAPES)
    st_template.dev_max = ST_MAX_TAPES;
  scsi_tapes =
    (Scsi_Tape *) scsi_init_malloc(st_template.dev_max * sizeof(Scsi_Tape),
				   GFP_ATOMIC);
  if (scsi_tapes == NULL) {
    printk("Unable to allocate descriptors for SCSI tapes.\n");
    unregister_chrdev(SCSI_TAPE_MAJOR, "st");
    return 1;
  }

#if DEBUG
  printk("st: Buffer size %d bytes, write threshold %d bytes.\n",
	 st_buffer_size, st_write_threshold);
#endif

  for (i=0; i < st_template.dev_max; ++i) {
    STp = &(scsi_tapes[i]);
    STp->device = NULL;
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
    STp->do_auto_lock = ST_AUTO_LOCK;
    STp->two_fm = ST_TWO_FM;
    STp->fast_mteom = ST_FAST_MTEOM;
    STp->write_threshold = st_write_threshold;
    STp->drv_block = 0;
    STp->moves_after_eof = 1;
    STp->at_sm = 0;
    STp->mt_status = (struct mtget *) scsi_init_malloc(sizeof(struct mtget),
						       GFP_ATOMIC);
    /* Initialize status */
    memset((void *) scsi_tapes[i].mt_status, 0, sizeof(struct mtget));
  }

  /* Allocate the buffers */
  st_buffers =
    (ST_buffer **) scsi_init_malloc(st_template.dev_max * sizeof(ST_buffer *),
				    GFP_ATOMIC);
  if (st_buffers == NULL) {
    printk("Unable to allocate tape buffer pointers.\n");
    unregister_chrdev(SCSI_TAPE_MAJOR, "st");
    scsi_init_free((char *) scsi_tapes,
		   st_template.dev_max * sizeof(Scsi_Tape));
    return 1;
  }

#if ST_RUNTIME_BUFFERS
  st_nbr_buffers = 0;
#else
  target_nbr = st_template.dev_noticed;
  if (target_nbr < ST_EXTRA_DEVS)
    target_nbr = ST_EXTRA_DEVS;
  if (target_nbr > st_max_buffers)
    target_nbr = st_max_buffers;

  for (i=st_nbr_buffers=0; i < target_nbr; i++) {
    if (!new_tape_buffer(TRUE)) {
      if (i == 0) {
	printk("Can't continue without at least one tape buffer.\n");
	unregister_chrdev(SCSI_TAPE_MAJOR, "st");
	scsi_init_free((char *) st_buffers,
		       st_template.dev_max * sizeof(ST_buffer *));
	scsi_init_free((char *) scsi_tapes,
		       st_template.dev_max * sizeof(Scsi_Tape));
	return 1;
      }
      printk("Number of tape buffers adjusted.\n");
      break;
    }
  }
#endif
  return 0;
}

static void st_detach(Scsi_Device * SDp)
{
  Scsi_Tape * tpnt;
  int i;
  
  for(tpnt = scsi_tapes, i=0; i<st_template.dev_max; i++, tpnt++) 
    if(tpnt->device == SDp) {
      tpnt->device = NULL;
      SDp->attached--;
      st_template.nr_dev--;
      st_template.dev_noticed--;
      return;
    }
  return;
}


#ifdef MODULE

int init_module(void) {
  st_template.usage_count = &mod_use_count_;
  return scsi_register_module(MODULE_SCSI_DEV, &st_template);
}

void cleanup_module( void) 
{
  int i;

  scsi_unregister_module(MODULE_SCSI_DEV, &st_template);
  unregister_chrdev(SCSI_TAPE_MAJOR, "st");
  st_registered--;
  if(scsi_tapes != NULL) {
    scsi_init_free((char *) scsi_tapes,
		   st_template.dev_max * sizeof(Scsi_Tape));

    if (st_buffers != NULL) {
      for (i=0; i < st_nbr_buffers; i++)
	if (st_buffers[i] != NULL) {
	  scsi_init_free((char *) st_buffers[i]->b_data,
			 st_buffers[i]->buffer_size);
	  scsi_init_free((char *) st_buffers[i], sizeof(ST_buffer));
	}

      scsi_init_free((char *) st_buffers,
		     st_template.dev_max * sizeof(ST_buffer *));
    }
  }
  st_template.dev_max = 0;
}
#endif /* MODULE */
