/*
  SCSI Tape Driver for Linux version 1.1 and newer. See the accompanying
  file README.st for more information.

  History:
  Rewritten from Dwayne Forsyth's SCSI tape driver by Kai Makisara.
  Contribution and ideas from several people including (in alphabetical
  order) Klaus Ehrenfried, Wolfgang Denk, Steve Hirsch, Andreas Koppenh"ofer,
  Eyal Lebedinsky, J"org Weule, and Eric Youngdale.

  Copyright 1992 - 1996 Kai Makisara
		 email Kai.Makisara@metla.fi

  Last modified: Sun Jun 30 15:26:23 1996 by root@kai.makisara.fi
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

/* The message level for the debug messages is currently set to KERN_NOTICE
   so that people can easily see the messages. Later when the debugging messages
   in the drivers are more widely classified, this may be changed to KERN_DEBUG. */
#define ST_DEB_MSG  KERN_NOTICE

#define MAJOR_NR SCSI_TAPE_MAJOR
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>
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
#define ST_LONG_TIMEOUT (14000 * HZ)

#define TAPE_NR(x) (MINOR(x) & ~(128 | ST_MODE_MASK))
#define TAPE_MODE(x) ((MINOR(x) & ST_MODE_MASK) >> ST_MODE_SHIFT)

/* Internal ioctl to set both density (uppermost 8 bits) and blocksize (lower
   24 bits) */
#define SET_DENS_AND_BLK 0x10001

static int st_nbr_buffers;
static ST_buffer **st_buffers;
static int st_buffer_size = ST_BUFFER_SIZE;
static int st_write_threshold = ST_WRITE_THRESHOLD;
static int st_max_buffers = ST_MAX_BUFFERS;

Scsi_Tape * scsi_tapes = NULL;

static int modes_defined = FALSE;

static ST_buffer *new_tape_buffer(int, int);
static int enlarge_buffer(ST_buffer *, int, int);
static void normalize_buffer(ST_buffer *);

static int st_init(void);
static int st_attach(Scsi_Device *);
static int st_detect(Scsi_Device *);
static void st_detach(Scsi_Device *);

struct Scsi_Device_Template st_template = {NULL, "tape", "st", NULL, TYPE_TAPE, 
					     SCSI_TAPE_MAJOR, 0, 0, 0, 0,
					     st_detect, st_init,
					     NULL, st_attach, st_detach};

static int st_compression(Scsi_Tape *, int);

static int find_partition(struct inode *);
static int update_partition(struct inode *);

static int st_int_ioctl(struct inode * inode, unsigned int cmd_in,
			unsigned long arg);




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
    printk(ST_DEB_MSG "st%d: Error: %x, cmd: %x %x %x %x %x %x Len: %d\n",
	   dev, result,
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

#if !DEBUG
  if (!(driver_byte(result) & DRIVER_SENSE) ||
      ((sense[0] & 0x70) == 0x70 &&
       scode != NO_SENSE &&
       scode != RECOVERED_ERROR &&
/*       scode != UNIT_ATTENTION && */
       scode != BLANK_CHECK &&
       scode != VOLUME_OVERFLOW &&
       SCpnt->data_cmnd[0] != MODE_SENSE &&
       SCpnt->data_cmnd[0] != TEST_UNIT_READY)) { /* Abnormal conditions for tape */
    if (driver_byte(result) & DRIVER_SENSE) {
      printk(KERN_WARNING "st%d: Error with sense data: ", dev);
      print_sense("st", SCpnt);
    }
    else
      printk(KERN_WARNING "st%d: Error %x.\n", dev, result);
  }
#endif

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
    if (debugging) {
      if (SCpnt->data_cmnd[0] == READ_6)
	stp = "read";
      else if (SCpnt->data_cmnd[0] == WRITE_6)
	stp = "write";
      else
	stp = "ioctl";
      printk(ST_DEB_MSG "st%d: Recovered %s error (%d).\n", dev, stp,
	     scsi_tapes[dev].recover_count);
    }
#endif
    if ((sense[2] & 0xe0) == 0)
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
    printk(KERN_ERR "st?: Illegal interrupt device %x\n", st_nbr);
#endif
}


/* Do the scsi command */
	static Scsi_Cmnd *
st_do_scsi(Scsi_Cmnd *SCpnt, Scsi_Tape *STp, unsigned char *cmd, int bytes,
	   int timeout, int retries)
{
  if (SCpnt == NULL)
    if ((SCpnt = allocate_device(NULL, STp->device, 1)) == NULL) {
      printk(KERN_ERR "st%d: Can't get SCSI request.\n", TAPE_NR(STp->devt));
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
    printk(KERN_ERR "st%d: Backing over filemark failed.\n", TAPE_NR(STp->devt));
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
	printk(ST_DEB_MSG "st%d: Async write error (flush) %x.\n",
	       TAPE_NR(STp->devt), (STp->buffer)->last_result);
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
      printk(ST_DEB_MSG "st%d: Flushing %d bytes.\n", TAPE_NR(STp->devt), transfer);
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
      if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	  (SCpnt->sense_buffer[2] & 0x40) &&
	  (SCpnt->sense_buffer[2] & 0x0f) == NO_SENSE) {
	STp->dirty = 0;
	(STp->buffer)->buffer_bytes = 0;
	result = (-ENOSPC);
      }
      else {
	printk(KERN_ERR "st%d: Error on flush.\n", TAPE_NR(STp->devt));
	result = (-EIO);
      }
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

  if (STp->ps[STp->partition].rw == ST_WRITING)  /* Writing */
    return flush_write_buffer(STp);

  if (STp->block_size == 0) {
    STp->eof = ST_NOEOF;
    STp->eof_hit = 0;
    return 0;
  }

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
      result = st_int_ioctl(inode, MTBSR, backspace);
  }
  else if ((STp->eof == ST_FM) && !STp->eof_hit) {
    if ((STp->mt_status)->mt_fileno >= 0)
      (STp->mt_status)->mt_fileno++;
    STp->drv_block = 0;
  }

  return result;

}

/* Set the mode parameters */
	static int
set_mode_densblk(struct inode * inode, Scsi_Tape *STp, ST_mode *STm)
{
    int set_it = FALSE;
    unsigned long arg;
    int dev = TAPE_NR(inode->i_rdev);

    if (!STp->density_changed &&
	STm->default_density >= 0 &&
	STm->default_density != STp->density) {
      arg = STm->default_density;
      set_it = TRUE;
    }
    else
      arg = STp->density;
    arg <<= MT_ST_DENSITY_SHIFT;
    if (!STp->blksize_changed &&
	STm->default_blksize >= 0 &&
	STm->default_blksize != STp->block_size) {
      arg |= STm->default_blksize;
      set_it = TRUE;
    }
    else
      arg |= STp->block_size;
    if (set_it &&
	st_int_ioctl(inode, SET_DENS_AND_BLK, arg)) {
      printk(KERN_WARNING
	     "st%d: Can't set default block size to %d bytes and density %x.\n",
	     dev, STm->default_blksize, STm->default_density);
      if (modes_defined)
	return (-EINVAL);
    }
    return 0;
}


/* Open the device */
	static int
scsi_tape_open(struct inode * inode, struct file * filp)
{
    unsigned short flags;
    int i, need_dma_buffer, new_session = FALSE;
    unsigned char cmd[10];
    Scsi_Cmnd * SCpnt;
    Scsi_Tape * STp;
    ST_mode * STm;
    int dev = TAPE_NR(inode->i_rdev);
    int mode = TAPE_MODE(inode->i_rdev);

    if (dev >= st_template.dev_max || !scsi_tapes[dev].device)
      return (-ENXIO);
    STp = &(scsi_tapes[dev]);
    if (STp->in_use) {
#if DEBUG
      printk(ST_DEB_MSG "st%d: Device already in use.\n", dev);
#endif
      return (-EBUSY);
    }
    STp->rew_at_close = (MINOR(inode->i_rdev) & 0x80) == 0;

    if (mode != STp->current_mode) {
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: Mode change from %d to %d.\n",
	       dev, STp->current_mode, mode);
#endif
      new_session = TRUE;
      STp->current_mode = mode;
    }
    STm = &(STp->modes[STp->current_mode]);

    /* Allocate buffer for this user */
    need_dma_buffer = STp->restr_dma;
    for (i=0; i < st_nbr_buffers; i++)
      if (!st_buffers[i]->in_use &&
	  (!need_dma_buffer || st_buffers[i]->dma))
	break;
    if (i >= st_nbr_buffers) {
      STp->buffer = new_tape_buffer(FALSE, need_dma_buffer);
      if (STp->buffer == NULL) {
	printk(KERN_WARNING "st%d: Can't allocate tape buffer.\n", dev);
	return (-EBUSY);
      }
    }
    else
      STp->buffer = st_buffers[i];
    (STp->buffer)->in_use = 1;
    (STp->buffer)->writing = 0;

    flags = filp->f_flags;
    STp->write_prot = ((flags & O_ACCMODE) == O_RDONLY);

    STp->dirty = 0;
    for (i=0; i < ST_NBR_PARTITIONS; i++)
      STp->ps[i].rw = ST_IDLE;
    STp->ready = ST_READY;
    if (STp->eof != ST_EOD)  /* Save EOD across opens */
      STp->eof = ST_NOEOF;
    STp->eof_hit = 0;
    STp->recover_count = 0;
#if DEBUG
    STp->nbr_waits = STp->nbr_finished = 0;
#endif

    if (scsi_tapes[dev].device->host->hostt->usage_count)
	(*scsi_tapes[dev].device->host->hostt->usage_count)++;
    if(st_template.usage_count) (*st_template.usage_count)++;

    memset ((void *) &cmd[0], 0, 10);
    cmd[0] = TEST_UNIT_READY;

    SCpnt = st_do_scsi(NULL, STp, cmd, 0, ST_LONG_TIMEOUT, MAX_READY_RETRIES);
    if (!SCpnt) {
      if (scsi_tapes[dev].device->host->hostt->usage_count)
	  (*scsi_tapes[dev].device->host->hostt->usage_count)--;
      if(st_template.usage_count) (*st_template.usage_count)--;
      return (-EBUSY);
    }

    if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	(SCpnt->sense_buffer[2] & 0x0f) == UNIT_ATTENTION) { /* New media? */
      (STp->mt_status)->mt_fileno = 0 ;
      memset ((void *) &cmd[0], 0, 10);
      cmd[0] = TEST_UNIT_READY;

      SCpnt = st_do_scsi(SCpnt, STp, cmd, 0, ST_LONG_TIMEOUT, MAX_READY_RETRIES);

      (STp->mt_status)->mt_fileno = STp->drv_block = 0;
      STp->eof = ST_NOEOF;
      (STp->device)->was_reset = 0;
      STp->partition = STp->new_partition = 0;
      if (STp->can_partitions)
	STp->nbr_partitions = 1;  /* This guess will be updated later if necessary */
      for (i=0; i < ST_NBR_PARTITIONS; i++) {
	STp->ps[i].rw = ST_IDLE;
	STp->ps[i].moves_after_eof = 1;
	STp->ps[i].at_sm = 0;
	STp->ps[i].last_block_valid = FALSE;
      }
      new_session = TRUE;
    }

    if ((STp->buffer)->last_result_fatal != 0) {
      if ((SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
	  (SCpnt->sense_buffer[2] & 0x0f) == NO_TAPE) {
	(STp->mt_status)->mt_fileno = STp->drv_block = 0 ;
	printk(KERN_NOTICE "st%d: No tape.\n", dev);
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
      STp->partition = STp->new_partition = 0;
      STp->door_locked = ST_UNLOCKED;
      STp->in_use = 1;
      return 0;
    }

    if (STp->omit_blklims)
      STp->min_block = STp->max_block = (-1);
    else {
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
	  printk(ST_DEB_MSG "st%d: Block limits %d - %d bytes.\n", dev, STp->min_block,
		 STp->max_block);
#endif
      }
      else {
	STp->min_block = STp->max_block = (-1);
#if DEBUG
	if (debugging)
	  printk(ST_DEB_MSG "st%d: Can't read block limits.\n", dev);
#endif
      }
    }

    memset ((void *) &cmd[0], 0, 10);
    cmd[0] = MODE_SENSE;
    cmd[4] = 12;

    SCpnt = st_do_scsi(SCpnt, STp, cmd, 12, ST_TIMEOUT, MAX_READY_RETRIES);

    if ((STp->buffer)->last_result_fatal != 0) {
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: No Mode Sense.\n", dev);
#endif
      STp->block_size = ST_DEFAULT_BLOCK;  /* Educated guess (?) */
      (STp->buffer)->last_result_fatal = 0;  /* Prevent error propagation */
      STp->drv_write_prot = 0;
    }
    else {

#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: Mode sense. Length %d, medium %x, WBS %x, BLL %d\n",
	       dev,
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
	  printk(ST_DEB_MSG "st%d: Density %x, tape length: %x, drv buffer: %d\n",
		 dev, STp->density, (STp->buffer)->b_data[5] * 65536 +
		 (STp->buffer)->b_data[6] * 256 + (STp->buffer)->b_data[7],
		 STp->drv_buffer);
#endif
      }

      if (STp->block_size > (STp->buffer)->buffer_size &&
	  !enlarge_buffer(STp->buffer, STp->block_size, STp->restr_dma)) {
	printk(KERN_NOTICE "st%d: Blocksize %d too large for buffer.\n", dev,
	       STp->block_size);
	(STp->buffer)->in_use = 0;
	STp->buffer = NULL;
	if (scsi_tapes[dev].device->host->hostt->usage_count)
	    (*scsi_tapes[dev].device->host->hostt->usage_count)--;
	if(st_template.usage_count) (*st_template.usage_count)--;
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
      printk(ST_DEB_MSG "st%d: Block size: %d, buffer size: %d (%d blocks).\n", dev,
	     STp->block_size, (STp->buffer)->buffer_size,
	     (STp->buffer)->buffer_blocks);
#endif

    if (STp->drv_write_prot) {
      STp->write_prot = 1;
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: Write protected\n", dev);
#endif
      if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
	(STp->buffer)->in_use = 0;
	STp->buffer = NULL;
	if (scsi_tapes[dev].device->host->hostt->usage_count)
	    (*scsi_tapes[dev].device->host->hostt->usage_count)--;
	if(st_template.usage_count) (*st_template.usage_count)--;
	return (-EROFS);
      }
    }

    if (STp->can_partitions && STp->nbr_partitions < 1) {
      /* This code is reached when the device is opened for the first time
	 after the driver has been initialized with tape in the drive and the
	 partition support has been enabled. */
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: Updating partition number in status.\n", dev);
#endif
      if ((STp->partition = find_partition(inode)) < 0) {
	(STp->buffer)->in_use = 0;
	STp->buffer = NULL;
	if (scsi_tapes[dev].device->host->hostt->usage_count)
	    (*scsi_tapes[dev].device->host->hostt->usage_count)--;
	if(st_template.usage_count) (*st_template.usage_count)--;
	return STp->partition;
      }
      STp->new_partition = STp->partition;
      STp->nbr_partitions = 1;  /* This guess will be updated when necessary */
    }

    if (new_session) {  /* Change the drive parameters for the new mode */
      STp->density_changed = STp->blksize_changed = FALSE;
      STp->compression_changed = FALSE;
      if (!(STm->defaults_for_writes) &&
	  (i = set_mode_densblk(inode, STp, STm)) < 0) {
	(STp->buffer)->in_use = 0;
	STp->buffer = NULL;
	if (scsi_tapes[dev].device->host->hostt->usage_count)
	    (*scsi_tapes[dev].device->host->hostt->usage_count)--;
	if(st_template.usage_count) (*st_template.usage_count)--;
	return i;
      }
      if (STp->default_drvbuffer != 0xff) {
	if (st_int_ioctl(inode, MTSETDRVBUFFER, STp->default_drvbuffer))
	  printk(KERN_WARNING "st%d: Can't set default drive buffering to %d.\n",
		 dev, STp->default_drvbuffer);
      }
    }

    STp->in_use = 1;

    return 0;
}


/* Close the device*/
	static void
scsi_tape_close(struct inode * inode, struct file * filp)
{
    int result;
    static unsigned char cmd[10];
    Scsi_Cmnd * SCpnt;
    Scsi_Tape * STp;
    kdev_t devt = inode->i_rdev;
    int dev;

    dev = TAPE_NR(devt);
    STp = &(scsi_tapes[dev]);

    if (STp->can_partitions &&
	update_partition(inode) < 0) {
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: update_partition at close failed.\n", dev);
#endif
      goto out;
    }

    if ( STp->ps[STp->partition].rw == ST_WRITING && !(STp->device)->was_reset) {

      result = flush_write_buffer(STp);

#if DEBUG
      if (debugging) {
	printk(ST_DEB_MSG "st%d: File length %ld bytes.\n",
	       dev, (long)(filp->f_pos));
	printk(ST_DEB_MSG "st%d: Async write waits %d, finished %d.\n",
	       dev, STp->nbr_waits, STp->nbr_finished);
      }
#endif

      if (result == 0 || result == (-ENOSPC)) {

	memset(cmd, 0, 10);
	cmd[0] = WRITE_FILEMARKS;
	cmd[4] = 1 + STp->two_fm;

	SCpnt = st_do_scsi(NULL, STp, cmd, 0, ST_TIMEOUT, MAX_WRITE_RETRIES);
	if (!SCpnt)
	  goto out;

	SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

	if ((STp->buffer)->last_result_fatal != 0 &&
	    ((SCpnt->sense_buffer[0] & 0x70) != 0x70 ||
	     (SCpnt->sense_buffer[2] & 0x4f) != 0x40 ||
	     ((SCpnt->sense_buffer[0] & 0x80) != 0 &&
	      (SCpnt->sense_buffer[3] | SCpnt->sense_buffer[4] |
	       SCpnt->sense_buffer[5] |
	       SCpnt->sense_buffer[6]) == 0)))  /* Filter out successful write at EOM */
	  printk(KERN_ERR "st%d: Error on write filemark.\n", dev);
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
	printk(ST_DEB_MSG "st%d: Buffer flushed, %d EOF(s) written\n",
	       dev, cmd[4]);
#endif
    }
    else if (!STp->rew_at_close) {
      if (STp->can_bsr)
	flush_buffer(inode, filp, 0);
      else if ((STp->eof == ST_FM) && !STp->eof_hit)
	back_over_eof(STp);
    }

out:
    if (STp->rew_at_close)
      st_int_ioctl(inode, MTREW, 1);

    if (STp->door_locked == ST_LOCKED_AUTO)
      st_int_ioctl(inode, MTUNLOCK, 0);

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
    ST_mode * STm;
    ST_partstat * STps;
    int dev = TAPE_NR(inode->i_rdev);

    STp = &(scsi_tapes[dev]);
    if (STp->ready != ST_READY)
      return (-EIO);
    STm = &(STp->modes[STp->current_mode]);
    if (!STm->defined)
      return (-ENXIO);

    /*
     * If there was a bus reset, block further access
     * to this device.
     */
    if( STp->device->was_reset )
      return (-EIO);

#if DEBUG
    if (!STp->in_use) {
      printk(ST_DEB_MSG "st%d: Incorrect device.\n", dev);
      return (-EIO);
    }
#endif

    if (STp->can_partitions &&
	(retval = update_partition(inode)) < 0)
      return retval;
    STps = &(STp->ps[STp->partition]);

    if (STp->write_prot)
      return (-EACCES);

    if (STp->block_size == 0 &&
       count > (STp->buffer)->buffer_size &&
       !enlarge_buffer(STp->buffer, count, STp->restr_dma))
      return (-EOVERFLOW);

    if (STp->do_auto_lock && STp->door_locked == ST_UNLOCKED &&
       !st_int_ioctl(inode, MTLOCK, 0))
      STp->door_locked = ST_LOCKED_AUTO;

    if (STps->rw == ST_READING) {
      retval = flush_buffer(inode, filp, 0);
      if (retval)
	return retval;
      STps->rw = ST_WRITING;
    }
    else if (STps->rw != ST_WRITING &&
	     (STp->mt_status)->mt_fileno == 0 && STp->drv_block == 0) {
      if ((retval = set_mode_densblk(inode, STp, STm)) < 0)
	return retval;
      if (STm->default_compression != ST_DONT_TOUCH &&
	  !(STp->compression_changed)) {
	if (st_compression(STp, (STm->default_compression == ST_YES))) {
	  printk(KERN_WARNING "st%d: Can't set default compression.\n",
		 dev);
	  if (modes_defined)
	    return (-EINVAL);
	}
      }
    }

    if (STps->moves_after_eof < 255)
      STps->moves_after_eof++;

    if ((STp->buffer)->writing) {
      write_behind_check(STp);
      if ((STp->buffer)->last_result_fatal) {
#if DEBUG
	if (debugging)
	  printk(ST_DEB_MSG "st%d: Async write error (write) %x.\n", dev,
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

    if (!STm->do_buffer_writes) {
      if (STp->block_size != 0 && (count % STp->block_size) != 0)
	return (-EIO);   /* Write must be integral number of blocks */
      write_threshold = 1;
    }
    else
      write_threshold = (STp->buffer)->buffer_blocks * STp->block_size;
    if (!STm->do_async_writes)
      write_threshold--;

    total = count;

    memset(cmd, 0, 10);
    cmd[0] = WRITE_6;
    cmd[1] = (STp->block_size != 0);

    STps->rw = ST_WRITING;

    b_point = buf;
    while((STp->block_size == 0 && !STm->do_async_writes && count > 0) ||
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
	  printk(ST_DEB_MSG "st%d: Error on write:\n", dev);
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
	      printk(ST_DEB_MSG "st%d: EOM with %d bytes unwritten.\n",
		     dev, transfer);
#endif
	  }
	  else {
	    STp->eof = ST_EOM_ERROR;
	    STp->drv_block = (-1);    /* Too cautious? */
	    retval = (-EIO); /* EOM for old data */
#if DEBUG
	    if (debugging)
	      printk(ST_DEB_MSG "st%d: EOM with lost data.\n", dev);
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

    if (STm->do_async_writes &&
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

    STps->at_sm &= (total == 0);
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
    ST_mode * STm;
    ST_partstat * STps;
    int dev = TAPE_NR(inode->i_rdev);

    STp = &(scsi_tapes[dev]);
    if (STp->ready != ST_READY)
      return (-EIO);
    STm = &(STp->modes[STp->current_mode]);
    if (!STm->defined)
      return (-ENXIO);
#if DEBUG
    if (!STp->in_use) {
      printk(ST_DEB_MSG "st%d: Incorrect device.\n", dev);
      return (-EIO);
    }
#endif

    if (STp->can_partitions &&
	(total = update_partition(inode)) < 0)
      return total;
    STps = &(STp->ps[STp->partition]);

    if (STp->block_size == 0 &&
	count > (STp->buffer)->buffer_size &&
	!enlarge_buffer(STp->buffer, count, STp->restr_dma))
      return (-EOVERFLOW);

    if (!(STm->do_read_ahead) && STp->block_size != 0 &&
	(count % STp->block_size) != 0)
      return (-EIO);	/* Read must be integral number of blocks */

    if (STp->do_auto_lock && STp->door_locked == ST_UNLOCKED &&
	!st_int_ioctl(inode, MTLOCK, 0))
      STp->door_locked = ST_LOCKED_AUTO;

    if (STps->rw == ST_WRITING) {
      transfer = flush_buffer(inode, filp, 0);
      if (transfer)
	return transfer;
      STps->rw = ST_READING;
    }
    if (STps->moves_after_eof < 255)
      STps->moves_after_eof++;

#if DEBUG
    if (debugging && STp->eof != ST_NOEOF)
      printk(ST_DEB_MSG "st%d: EOF flag up. Bytes %d\n", dev,
	     (STp->buffer)->buffer_bytes);
#endif
    if (((STp->buffer)->buffer_bytes == 0) &&
	(STp->eof == ST_EOM_OK || STp->eof == ST_EOD))
      return (-EIO);  /* EOM or Blank Check */

    STps->rw = ST_READING;

    for (total = 0; total < count; ) {

      if ((STp->buffer)->buffer_bytes == 0 &&
	  STp->eof == ST_NOEOF) {

	memset(cmd, 0, 10);
	cmd[0] = READ_6;
	cmd[1] = (STp->block_size != 0);
	if (STp->block_size == 0)
	  blks = bytes = count;
	else {
	  if (STm->do_read_ahead) {
	    blks = (STp->buffer)->buffer_blocks;
	    bytes = blks * STp->block_size;
	  }
	  else {
	    bytes = count - total;
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
	STps->at_sm = 0;

	if ((STp->buffer)->last_result_fatal) {
#if DEBUG
	  if (debugging)
	    printk(ST_DEB_MSG "st%d: Sense: %2x %2x %2x %2x %2x %2x %2x %2x\n",
		   dev,
		   SCpnt->sense_buffer[0], SCpnt->sense_buffer[1],
		   SCpnt->sense_buffer[2], SCpnt->sense_buffer[3],
		   SCpnt->sense_buffer[4], SCpnt->sense_buffer[5],
		   SCpnt->sense_buffer[6], SCpnt->sense_buffer[7]);
#endif
	  if ((SCpnt->sense_buffer[0] & 0x70) == 0x70) { /* extended sense */

	    if ((SCpnt->sense_buffer[2] & 0x0f) == BLANK_CHECK)
		SCpnt->sense_buffer[2] &= 0xcf; /* No need for EOM in this case */

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
		  SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
		  if (transfer == blks) {  /* We did not get anything, signal error */
		    printk(KERN_NOTICE "st%d: Incorrect block size.\n", dev);
		    if (STp->drv_block >= 0)
		      STp->drv_block += blks - transfer + 1;
		    st_int_ioctl(inode, MTBSR, 1);
		    return (-EIO);
		  }
		  /* We have some data, deliver it */
		  (STp->buffer)->buffer_bytes = (blks - transfer) * STp->block_size;
#if DEBUG
		  if (debugging)
		    printk(ST_DEB_MSG "st%d: ILI but enough data received %d %d.\n",
			   dev, count - total, (STp->buffer)->buffer_bytes);
#endif
		  if (count - total > (STp->buffer)->buffer_bytes)
		    count = total + (STp->buffer)->buffer_bytes;
		  if (STp->drv_block >= 0)
		    STp->drv_block += 1;
		  if (st_int_ioctl(inode, MTBSR, 1))
		    return (-EIO);
		  SCpnt = NULL;
		}
	      }
	      else if (SCpnt->sense_buffer[2] & 0x80) { /* FM overrides EOM */
		STp->eof = ST_FM;
		if (STp->block_size == 0)
		  (STp->buffer)->buffer_bytes = 0;
		else
		  (STp->buffer)->buffer_bytes =
		    bytes - transfer * STp->block_size;
#if DEBUG
		if (debugging)
		  printk(ST_DEB_MSG
		    "st%d: EOF detected (%d bytes read, transferred %d bytes).\n",
			 dev, (STp->buffer)->buffer_bytes, total);
#endif
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
		  printk(ST_DEB_MSG "st%d: EOM detected (%d bytes read).\n", dev,
			 (STp->buffer)->buffer_bytes);
#endif
	      }
	    } /* end of EOF, EOM, ILI test */
	    else { /* nonzero sense key */
#if DEBUG
	      if (debugging)
		printk(ST_DEB_MSG "st%d: Tape error while reading.\n", dev);
#endif
	      SCpnt->request.rq_status = RQ_INACTIVE;
	      STp->drv_block = (-1);
	      if (total)
		return total;
	      else if (STps->moves_after_eof == 1 &&
		       (SCpnt->sense_buffer[2] & 0x0f) == BLANK_CHECK) {
#if DEBUG
		if (debugging)
		  printk(ST_DEB_MSG
			 "st%d: Zero returned for first BLANK CHECK after EOF.\n",
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
	  printk(ST_DEB_MSG "st%d: EOF up. Left %d, needed %d.\n", dev,
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
	  if (STps->moves_after_eof > 1)
	    STps->moves_after_eof = 0;
	  if ((STp->mt_status)->mt_fileno >= 0)
	    (STp->mt_status)->mt_fileno++;
	}
	if (total == 0 && STp->eof == ST_EOM_OK)
	  return (-ENOSPC);  /* ST_EOM_ERROR not used in read */
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
	static void
st_log_options(Scsi_Tape *STp, ST_mode *STm, int dev)
{
  printk(KERN_INFO
"st%d: Mode %d options: buffer writes: %d, async writes: %d, read ahead: %d\n",
	 dev, STp->current_mode, STm->do_buffer_writes, STm->do_async_writes,
	 STm->do_read_ahead);
  printk(KERN_INFO
"st%d:    can bsr: %d, two FMs: %d, fast mteom: %d, auto lock: %d,\n",
	 dev, STp->can_bsr, STp->two_fm, STp->fast_mteom, STp->do_auto_lock);
  printk(KERN_INFO
"st%d:    defs for wr: %d, no block limits: %d, partitions: %d, s2 log: %d\n",
	 dev, STm->defaults_for_writes, STp->omit_blklims, STp->can_partitions,
	 STp->scsi2_logical);
#if DEBUG
  printk(KERN_INFO
	 "st%d:    debugging: %d\n",
	 dev, debugging);
#endif
}

  
	static int
st_set_options(struct inode * inode, long options)
{
  int value;
  long code;
  Scsi_Tape *STp;
  ST_mode *STm;
  int dev = TAPE_NR(inode->i_rdev);

  STp = &(scsi_tapes[dev]);
  STm = &(STp->modes[STp->current_mode]);
  if (!STm->defined) {
    memcpy(STm, &(STp->modes[0]), sizeof(ST_mode));
    modes_defined = TRUE;
#if DEBUG
    if (debugging)
      printk(ST_DEB_MSG "st%d: Initialized mode %d definition from mode 0\n",
	     dev, STp->current_mode);
#endif
  }

  code = options & MT_ST_OPTIONS;
  if (code == MT_ST_BOOLEANS) {
    STm->do_buffer_writes = (options & MT_ST_BUFFER_WRITES) != 0;
    STm->do_async_writes  = (options & MT_ST_ASYNC_WRITES) != 0;
    STm->defaults_for_writes = (options & MT_ST_DEF_WRITES) != 0;
    STm->do_read_ahead    = (options & MT_ST_READ_AHEAD) != 0;
    STp->two_fm		  = (options & MT_ST_TWO_FM) != 0;
    STp->fast_mteom	  = (options & MT_ST_FAST_MTEOM) != 0;
    STp->do_auto_lock     = (options & MT_ST_AUTO_LOCK) != 0;
    STp->can_bsr          = (options & MT_ST_CAN_BSR) != 0;
    STp->omit_blklims	  = (options & MT_ST_NO_BLKLIMS) != 0;
    if ((STp->device)->scsi_level >= SCSI_2)
      STp->can_partitions = (options & MT_ST_CAN_PARTITIONS) != 0;
    STp->scsi2_logical    = (options & MT_ST_SCSI2LOGICAL) != 0;
#if DEBUG
    debugging = (options & MT_ST_DEBUGGING) != 0;
#endif
    st_log_options(STp, STm, dev);
  }
  else if (code == MT_ST_SETBOOLEANS || code == MT_ST_CLEARBOOLEANS) {
    value = (code == MT_ST_SETBOOLEANS);
    if ((options & MT_ST_BUFFER_WRITES) != 0)
      STm->do_buffer_writes = value;
    if ((options & MT_ST_ASYNC_WRITES) != 0)
      STm->do_async_writes = value;
    if ((options & MT_ST_DEF_WRITES) != 0)
      STm->defaults_for_writes = value;
    if ((options & MT_ST_READ_AHEAD) != 0)
      STm->do_read_ahead = value;
    if ((options & MT_ST_TWO_FM) != 0)
      STp->two_fm = value;
    if ((options & MT_ST_FAST_MTEOM) != 0)
      STp->fast_mteom = value;
    if ((options & MT_ST_AUTO_LOCK) != 0)
      STp->do_auto_lock = value;
    if ((options & MT_ST_CAN_BSR) != 0)
      STp->can_bsr = value;
    if ((options & MT_ST_NO_BLKLIMS) != 0)
      STp->omit_blklims = value;
    if ((STp->device)->scsi_level >= SCSI_2 &&
	(options & MT_ST_CAN_PARTITIONS) != 0)
      STp->can_partitions = value;
    if ((options & MT_ST_SCSI2LOGICAL) != 0)
      STp->scsi2_logical = value;
#if DEBUG
    if ((options & MT_ST_DEBUGGING) != 0)
      debugging = value;
#endif
    st_log_options(STp, STm, dev);
  }
  else if (code == MT_ST_WRITE_THRESHOLD) {
    value = (options & ~MT_ST_OPTIONS) * ST_BLOCK_SIZE;
    if (value < 1 || value > st_buffer_size) {
      printk(KERN_WARNING "st%d: Write threshold %d too small or too large.\n",
	     dev, value);
      return (-EIO);
    }
    STp->write_threshold = value;
    printk(KERN_INFO "st%d: Write threshold set to %d bytes.\n",
	   dev, value);
  }
  else if (code == MT_ST_DEF_BLKSIZE) {
    value = (options & ~MT_ST_OPTIONS);
    if (value == ~MT_ST_OPTIONS) {
      STm->default_blksize = (-1);
      printk(KERN_INFO "st%d: Default block size disabled.\n", dev);
    }
    else {
      STm->default_blksize = value;
      printk(KERN_INFO "st%d: Default block size set to %d bytes.\n",
	     dev, STm->default_blksize);
    }
  }
  else if (code == MT_ST_DEF_OPTIONS) {
    code = (options & ~MT_ST_CLEAR_DEFAULT);
    value = (options & MT_ST_CLEAR_DEFAULT);
    if (code == MT_ST_DEF_DENSITY) {
      if (value == MT_ST_CLEAR_DEFAULT) {
	STm->default_density = (-1);
	printk(KERN_INFO "st%d: Density default disabled.\n", dev);
      }
      else {
	STm->default_density = value & 0xff;
	printk(KERN_INFO "st%d: Density default set to %x\n",
	       dev, STm->default_density);
      }
    }
    else if (code == MT_ST_DEF_DRVBUFFER) {
      if (value == MT_ST_CLEAR_DEFAULT) {
	STp->default_drvbuffer = 0xff;
	printk(KERN_INFO "st%d: Drive buffer default disabled.\n", dev);
      }
      else {
	STp->default_drvbuffer = value & 7;
	printk(KERN_INFO "st%d: Drive buffer default set to %x\n",
	       dev, STp->default_drvbuffer);
      }
    }
    else if (code == MT_ST_DEF_COMPRESSION) {
      if (value == MT_ST_CLEAR_DEFAULT) {
	STm->default_compression = ST_DONT_TOUCH;
	printk(KERN_INFO "st%d: Compression default disabled.\n", dev);
      }
      else {
	STm->default_compression = (value & 1 ? ST_YES : ST_NO);
	printk(KERN_INFO "st%d: Compression default set to %x\n",
	       dev, (value & 1));
      }
    }
  }
  else
    return (-EIO);

  return 0;
}


#define COMPRESSION_PAGE        0x0f
#define COMPRESSION_PAGE_LENGTH 16

#define MODE_HEADER_LENGTH  4

#define DCE_MASK  0x80
#define DCC_MASK  0x40
#define RED_MASK  0x60


/* Control the compression with mode page 15. Algorithm not changed if zero. */
	static int
st_compression(Scsi_Tape * STp, int state)
{
  int dev;
  unsigned char cmd[10];
  Scsi_Cmnd * SCpnt = NULL;

  if (STp->ready != ST_READY)
    return (-EIO);

  /* Read the current page contents */
  memset(cmd, 0, 10);
  cmd[0] = MODE_SENSE;
  cmd[1] = 8;
  cmd[2] = COMPRESSION_PAGE;
  cmd[4] = COMPRESSION_PAGE_LENGTH + MODE_HEADER_LENGTH;

  SCpnt = st_do_scsi(SCpnt, STp, cmd, cmd[4], ST_TIMEOUT, 0);
  if (SCpnt == NULL)
    return (-EBUSY);
  dev = TAPE_NR(SCpnt->request.rq_dev);

  if ((STp->buffer)->last_result_fatal != 0) {
#if DEBUG
    if (debugging)
      printk(ST_DEB_MSG "st%d: Compression mode page not supported.\n", dev);
#endif
    SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
    return (-EIO);
  }
#if DEBUG
  if (debugging)
    printk(ST_DEB_MSG "st%d: Compression state is %d.\n", dev,
	   ((STp->buffer)->b_data[MODE_HEADER_LENGTH + 2] & DCE_MASK ? 1 : 0));
#endif

  /* Check if compression can be changed */
  if (((STp->buffer)->b_data[MODE_HEADER_LENGTH + 2] & DCC_MASK) == 0) {
#if DEBUG
    if (debugging)
      printk(ST_DEB_MSG "st%d: Compression not supported.\n", dev);
#endif
    SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
    return (-EIO);
  }

  /* Do the change */
  if (state)
    (STp->buffer)->b_data[MODE_HEADER_LENGTH + 2] |= DCE_MASK;
  else
    (STp->buffer)->b_data[MODE_HEADER_LENGTH + 2] &= ~DCE_MASK;

  memset(cmd, 0, 10);
  cmd[0] = MODE_SELECT;
  cmd[1] = 0x10;
  cmd[4] = COMPRESSION_PAGE_LENGTH + MODE_HEADER_LENGTH;

  (STp->buffer)->b_data[0] = 0;  /* Reserved data length */
  (STp->buffer)->b_data[1] = 0;  /* Reserved media type byte */
  (STp->buffer)->b_data[MODE_HEADER_LENGTH] &= 0x3f;
  SCpnt = st_do_scsi(SCpnt, STp, cmd, cmd[4], ST_TIMEOUT, 0);

  if ((STp->buffer)->last_result_fatal != 0) {
#if DEBUG
    if (debugging)
      printk(ST_DEB_MSG "st%d: Compression change failed.\n", dev);
#endif
    SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
    return (-EIO);
  }

#if DEBUG
  if (debugging)
    printk(ST_DEB_MSG "st%d: Compression state changed to %d.\n",
	   dev, state);
#endif

  SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */
  STp->compression_changed = TRUE;
  return 0;
}


/* Internal ioctl function */
	static int
st_int_ioctl(struct inode * inode,
	     unsigned int cmd_in, unsigned long arg)
{
   int timeout = ST_LONG_TIMEOUT;
   long ltmp;
   int i, ioctl_result;
   unsigned char cmd[10];
   Scsi_Cmnd * SCpnt;
   Scsi_Tape * STp;
   ST_partstat * STps;
   int fileno, blkno, at_sm, undone, datalen;
   int dev = TAPE_NR(inode->i_rdev);

   STp = &(scsi_tapes[dev]);
   if (STp->ready != ST_READY && cmd_in != MTLOAD)
     return (-EIO);
   STps = &(STp->ps[STp->partition]);
   fileno = (STp->mt_status)->mt_fileno ;
   blkno = STp->drv_block;
   at_sm = STps->at_sm;

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
	 printk(ST_DEB_MSG "st%d: Spacing tape forward over %d filemarks.\n",
		dev, cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
#endif
       if (fileno >= 0)
	 fileno += arg;
       blkno = 0;
       at_sm &= (arg == 0);
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
	 printk(ST_DEB_MSG "st%d: Spacing tape backward over %ld filemarks.\n",
		dev, (-ltmp));
       }
#endif
       if (fileno >= 0)
	 fileno -= arg;
       blkno = (-1);  /* We can't know the block number */
       at_sm &= (arg == 0);
       break; 
     case MTFSR:
       cmd[0] = SPACE;
       cmd[1] = 0x00; /* Space Blocks */
       cmd[2] = (arg >> 16);
       cmd[3] = (arg >> 8);
       cmd[4] = arg;
#if DEBUG
       if (debugging)
	 printk(ST_DEB_MSG "st%d: Spacing tape forward %d blocks.\n", dev,
		cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
#endif
       if (blkno >= 0)
	 blkno += arg;
       at_sm &= (arg == 0);
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
	 printk(ST_DEB_MSG "st%d: Spacing tape backward %ld blocks.\n", dev, (-ltmp));
       }
#endif
       if (blkno >= 0)
	 blkno -= arg;
       at_sm &= (arg == 0);
       break; 
     case MTFSS:
       cmd[0] = SPACE;
       cmd[1] = 0x04; /* Space Setmarks */
       cmd[2] = (arg >> 16);
       cmd[3] = (arg >> 8);
       cmd[4] = arg;
#if DEBUG
       if (debugging)
	 printk(ST_DEB_MSG "st%d: Spacing tape forward %d setmarks.\n", dev,
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
	 printk(ST_DEB_MSG "st%d: Spacing tape backward %ld setmarks.\n",
		dev, (-ltmp));
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
	   printk(ST_DEB_MSG "st%d: Writing %d filemarks.\n", dev,
		  cmd[2] * 65536 + cmd[3] * 256 + cmd[4]);
	 else
	   printk(ST_DEB_MSG "st%d: Writing %d setmarks.\n", dev,
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
	 printk(ST_DEB_MSG "st%d: Rewinding tape.\n", dev);
#endif
       fileno = blkno = at_sm = 0 ;
       break; 
     case MTOFFL:
     case MTLOAD:
     case MTUNLOAD:
       cmd[0] = START_STOP;
       if (cmd_in == MTLOAD)
	 cmd[4] |= 1;
#if ST_NOWAIT
       cmd[1] = 1;  /* Don't wait for completion */
       timeout = ST_TIMEOUT;
#else
       timeout = ST_LONG_TIMEOUT * 8;
#endif
#if DEBUG
       if (debugging) {
	 if (cmd_in != MTLOAD)
	   printk(ST_DEB_MSG "st%d: Unloading tape.\n", dev);
	 else
	   printk(ST_DEB_MSG "st%d: Loading tape.\n", dev);
       }
#endif
       fileno = blkno = at_sm = 0 ;
       break; 
     case MTNOP:
#if DEBUG
       if (debugging)
	 printk(ST_DEB_MSG "st%d: No op on tape.\n", dev);
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
	 printk(ST_DEB_MSG "st%d: Retensioning tape.\n", dev);
#endif
       fileno = blkno = at_sm = 0;
       break; 
     case MTEOM:
       if (!STp->fast_mteom) {
	 /* space to the end of tape */
	 ioctl_result = st_int_ioctl(inode, MTFSF, 0x3fff);
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
	 printk(ST_DEB_MSG "st%d: Spacing to end of recorded medium.\n", dev);
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
	 printk(ST_DEB_MSG "st%d: Erasing tape.\n", dev);
#endif
       fileno = blkno = at_sm = 0 ;
       break;
     case MTLOCK:
       cmd[0] = ALLOW_MEDIUM_REMOVAL;
       cmd[4] = SCSI_REMOVAL_PREVENT;
#if DEBUG
       if (debugging)
	 printk(ST_DEB_MSG "st%d: Locking drive door.\n", dev);
#endif;
       break;
     case MTUNLOCK:
       cmd[0] = ALLOW_MEDIUM_REMOVAL;
       cmd[4] = SCSI_REMOVAL_ALLOW;
#if DEBUG
       if (debugging)
	 printk(ST_DEB_MSG "st%d: Unlocking drive door.\n", dev);
#endif;
       break;
     case MTSETBLK:  /* Set block length */
     case MTSETDENSITY: /* Set tape density */
     case MTSETDRVBUFFER: /* Set drive buffering */
     case SET_DENS_AND_BLK: /* Set density and block size */
       if (STp->dirty || (STp->buffer)->buffer_bytes != 0)
	 return (-EIO);   /* Not allowed if data in buffer */
       if ((cmd_in == MTSETBLK || cmd_in == SET_DENS_AND_BLK) &&
	   (arg & MT_ST_BLKSIZE_MASK) != 0 &&
	   ((arg & MT_ST_BLKSIZE_MASK) < STp->min_block ||
	    (arg & MT_ST_BLKSIZE_MASK) > STp->max_block ||
	    (arg & MT_ST_BLKSIZE_MASK) > st_buffer_size)) {
	 printk(KERN_WARNING "st%d: Illegal block size.\n", dev);
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
       if (cmd_in == MTSETDENSITY) {
	 (STp->buffer)->b_data[4] = arg;
	 STp->density_changed = TRUE;  /* At least we tried ;-) */
       }
       else if (cmd_in == SET_DENS_AND_BLK)
	 (STp->buffer)->b_data[4] = arg >> 24;
       else
	 (STp->buffer)->b_data[4] = STp->density;
       if (cmd_in == MTSETBLK || cmd_in == SET_DENS_AND_BLK) {
	 ltmp = arg & MT_ST_BLKSIZE_MASK;
	 if (cmd_in == MTSETBLK)
	   STp->blksize_changed = TRUE;  /* At least we tried ;-) */
       }
       else
	 ltmp = STp->block_size;
       (STp->buffer)->b_data[9] = (ltmp >> 16);
       (STp->buffer)->b_data[10] = (ltmp >> 8);
       (STp->buffer)->b_data[11] = ltmp;
       timeout = ST_TIMEOUT;
#if DEBUG
       if (debugging) {
	 if (cmd_in == MTSETBLK || cmd_in == SET_DENS_AND_BLK)
	   printk(ST_DEB_MSG "st%d: Setting block size to %d bytes.\n", dev,
		  (STp->buffer)->b_data[9] * 65536 +
		  (STp->buffer)->b_data[10] * 256 +
		  (STp->buffer)->b_data[11]);
	 if (cmd_in == MTSETDENSITY || cmd_in == SET_DENS_AND_BLK)
	   printk(ST_DEB_MSG "st%d: Setting density code to %x.\n", dev,
		  (STp->buffer)->b_data[4]);
	 if (cmd_in == MTSETDRVBUFFER)
	   printk(ST_DEB_MSG "st%d: Setting drive buffer code to %d.\n", dev,
		  ((STp->buffer)->b_data[2] >> 4) & 7);
       }
#endif
       break;
     default:
       return (-ENOSYS);
     }

   SCpnt = st_do_scsi(NULL, STp, cmd, datalen, timeout, MAX_RETRIES);
   if (!SCpnt)
     return (-EBUSY);

   ioctl_result = (STp->buffer)->last_result_fatal;

   SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

   if (cmd_in == MTFSF)
     STps->moves_after_eof = 0;
   else if (cmd_in != MTLOAD)
     STps->moves_after_eof = 1;
   if (!ioctl_result) {  /* SCSI command successful */
     STp->drv_block = blkno;
     (STp->mt_status)->mt_fileno = fileno;
     STps->at_sm = at_sm;
     if (cmd_in == MTLOCK)
       STp->door_locked = ST_LOCKED_EXPLICIT;
     else if (cmd_in == MTUNLOCK)
       STp->door_locked = ST_UNLOCKED;
     if (cmd_in == MTBSFM)
       ioctl_result = st_int_ioctl(inode, MTFSF, 1);
     else if (cmd_in == MTFSFM)
       ioctl_result = st_int_ioctl(inode, MTBSF, 1);
     else if (cmd_in == MTSETBLK || cmd_in == SET_DENS_AND_BLK) {
       STp->block_size = arg & MT_ST_BLKSIZE_MASK;
       if (STp->block_size != 0)
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
     if (cmd_in == SET_DENS_AND_BLK)
       STp->density = arg >> MT_ST_DENSITY_SHIFT;
     if (cmd_in == MTOFFL || cmd_in == MTUNLOAD)
       STp->rew_at_close = 0;
     else if (cmd_in == MTLOAD) {
       STp->rew_at_close = (MINOR(inode->i_rdev) & 0x80) == 0;
       for (i=0; i < ST_NBR_PARTITIONS; i++) {
	 STp->ps[i].rw = ST_IDLE;
	 STp->ps[i].moves_after_eof = 1;
	 STp->ps[i].last_block_valid = FALSE;
       }
       STp->partition = 0;
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
     if (cmd_in == MTWEOF &&
       (SCpnt->sense_buffer[0] & 0x70) == 0x70 &&
       (SCpnt->sense_buffer[2] & 0x4f) == 0x40 &&
       ((SCpnt->sense_buffer[0] & 0x80) == 0 || undone == 0)) {
       ioctl_result = 0;  /* EOF written succesfully at EOM */
       if (fileno >= 0)
	 fileno++;
       (STp->mt_status)->mt_fileno = fileno;
     }
     else if ( (cmd_in == MTFSF) || (cmd_in == MTFSFM) ) {
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
     else if (cmd_in == MTEOM) {
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


/* Get the tape position. If bt == 2, arg points into a kernel space mt_loc
    structure. */

	static int
get_location(struct inode * inode, unsigned int *block, int *partition,
	     int logical)
{
    Scsi_Tape *STp;
    int dev = TAPE_NR(inode->i_rdev);
    int result;
    unsigned char scmd[10];
    Scsi_Cmnd *SCpnt;

    STp = &(scsi_tapes[dev]);
    if (STp->ready != ST_READY)
      return (-EIO);

    memset (scmd, 0, 10);
    if ((STp->device)->scsi_level < SCSI_2) {
      scmd[0] = QFA_REQUEST_BLOCK;
      scmd[4] = 3;
    }
    else {
      scmd[0] = READ_POSITION;
      if (!logical && !STp->scsi2_logical)
	scmd[1] = 1;
    }
    SCpnt = st_do_scsi(NULL, STp, scmd, 20, ST_TIMEOUT, MAX_READY_RETRIES);
    if (!SCpnt)
      return (-EBUSY);

    if ((STp->buffer)->last_result_fatal != 0 ||
	((STp->buffer)->b_data[0] & 4)) {
      *block = *partition = 0;
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: Can't read tape position.\n", dev);
#endif
      result = (-EIO);
    }
    else {
      result = 0;
      if ((STp->device)->scsi_level < SCSI_2) {
	*block = ((STp->buffer)->b_data[0] << 16) 
	+ ((STp->buffer)->b_data[1] << 8) 
	+ (STp->buffer)->b_data[2];
	*partition = 0;
      }
      else {
	*block = ((STp->buffer)->b_data[4] << 24)
	  + ((STp->buffer)->b_data[5] << 16) 
	  + ((STp->buffer)->b_data[6] << 8) 
	  + (STp->buffer)->b_data[7];
	*partition = (STp->buffer)->b_data[1];
	if (((STp->buffer)->b_data[0] & 0x80) &&
	    (STp->buffer)->b_data[1] == 0) /* BOP of partition 0 */
	  STp->drv_block = (STp->mt_status)->mt_fileno = 0;
      }
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: Got tape pos. blk %d part %d.\n", dev,
	       *block, *partition);
#endif

    }
    SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

    return result;
}


/* Set the tape block and partition. Negative partition means that only the
   block should be set in vendor specific way. */
	static int
set_location(struct inode * inode, unsigned int block, int partition,
	     int logical)
{
    Scsi_Tape *STp;
    ST_partstat *STps;
    int dev = TAPE_NR(inode->i_rdev);
    int result, p;
    unsigned int blk;
    int timeout = ST_LONG_TIMEOUT;
    unsigned char scmd[10];
    Scsi_Cmnd *SCpnt;

    STp = &(scsi_tapes[dev]);
    if (STp->ready != ST_READY)
      return (-EIO);
    STps = &(STp->ps[STp->partition]);

#if DEBUG
    if (debugging)
      printk(ST_DEB_MSG "st%d: Setting block to %d and partition to %d.\n",
	     dev, block, partition);
    if (partition < 0)
      return (-EIO);
#endif

    /* Update the location at the partition we are leaving */
    if ((!STp->can_partitions && partition != 0) ||
	partition >= ST_NBR_PARTITIONS)
      return (-EINVAL);
    if (partition != STp->partition) {
      if (get_location(inode, &blk, &p, 1))
	STps->last_block_valid = FALSE;
      else {
	STps->last_block_valid = TRUE;
	STps->last_block_visited = blk;
#if DEBUG
	if (debugging)
	  printk(ST_DEB_MSG "st%d: Visited block %d for partition %d saved.\n",
		 dev, blk, STp->partition);
#endif
      }
    }

    memset (scmd, 0, 10);
    if ((STp->device)->scsi_level < SCSI_2) {
      scmd[0] = QFA_SEEK_BLOCK;
      scmd[2] = (block >> 16);
      scmd[3] = (block >> 8);
      scmd[4] = block;
      scmd[5] = 0;
    }
    else {
      scmd[0] = SEEK_10;
      scmd[3] = (block >> 24);
      scmd[4] = (block >> 16);
      scmd[5] = (block >> 8);
      scmd[6] = block;
      if (!logical && !STp->scsi2_logical)
	scmd[1] = 4;
      if (STp->partition != partition) {
	scmd[1] |= 2;
	scmd[8] = partition;
#if DEBUG
	if (debugging)
	  printk(ST_DEB_MSG "st%d: Trying to change partition from %d to %d\n",
		 dev, STp->partition, partition);
#endif
      }
     }
#if ST_NOWAIT
    scmd[1] |= 1;  /* Don't wait for completion */
    timeout = ST_TIMEOUT;
#endif

    SCpnt = st_do_scsi(NULL, STp, scmd, 20, timeout, MAX_READY_RETRIES);
    if (!SCpnt)
      return (-EBUSY);

    STp->drv_block = (STp->mt_status)->mt_fileno = (-1);
    if ((STp->buffer)->last_result_fatal != 0) {
      result = (-EIO);
      if (STp->can_partitions &&
	  (STp->device)->scsi_level >= SCSI_2 &&
	  (p = find_partition(inode)) >= 0)
	STp->partition = p;
    }
    else {
      if (STp->can_partitions) {
	STp->partition = partition;
	STps = &(STp->ps[partition]);
	if (!STps->last_block_valid ||
	    STps->last_block_visited != block) {
	  STps->moves_after_eof = 1;
	  STps->at_sm = 0;
	  STps->rw = ST_IDLE;
	}
      }
      else
	STps->at_sm = 0;
      if (block == 0)
	STp->drv_block = (STp->mt_status)->mt_fileno = 0;
      result = 0;
    }
    SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

    return result;
}


/* Find the current partition number for the drive status. Called from open and
   returns either partition number of negative error code. */
	static int
find_partition(struct inode *inode)
{
    int i, partition;
    unsigned int block;

    if ((i = get_location(inode, &block, &partition, 1)) < 0)
      return i;
    if (partition >= ST_NBR_PARTITIONS)
      return (-EIO);
    return partition;
}


/* Change the partition if necessary */
	static int
update_partition(struct inode * inode)
{
    int dev = TAPE_NR(inode->i_rdev);
    Scsi_Tape *STp;
    ST_partstat *STps;

    STp = &(scsi_tapes[dev]);
    if (STp->partition == STp->new_partition)
      return 0;
    STps = &(STp->ps[STp->new_partition]);
    if (!STps->last_block_valid)
      STps->last_block_visited = 0;
    return set_location(inode, STps->last_block_visited, STp->new_partition, 1);
}

/* Functions for reading and writing the medium partition mode page. These
   seem to work with Wangtek 6200HS and HP C1533A. */

#define PART_PAGE   0x11
#define PART_PAGE_LENGTH 10

/* Get the number of partitions on the tape. As a side effect reads the
   mode page into the tape buffer. */
	static int
nbr_partitions(struct inode * inode)
{
    int dev = TAPE_NR(inode->i_rdev), result;
    Scsi_Tape *STp;
    Scsi_Cmnd * SCpnt = NULL;
    unsigned char cmd[10];

    STp = &(scsi_tapes[dev]);
    if (STp->ready != ST_READY)
      return (-EIO);

    memset ((void *) &cmd[0], 0, 10);
    cmd[0] = MODE_SENSE;
    cmd[1] = 8;   /* Page format */
    cmd[2] = PART_PAGE;
    cmd[4] = 200;

    SCpnt = st_do_scsi(SCpnt, STp, cmd, 200, ST_TIMEOUT, MAX_READY_RETRIES);
    if (SCpnt == NULL)
      return (-EBUSY);
    SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

    if ((STp->buffer)->last_result_fatal != 0) {
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: Can't read medium partition page.\n", dev);
#endif
      result = (-EIO);
    }
    else {
      result = (STp->buffer)->b_data[MODE_HEADER_LENGTH + 3] + 1;
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: Number of partitions %d.\n", dev, result);
#endif
    }

    return result;
}


/* Partition the tape into two partitions if size > 0 or one partition if
   size == 0 */
	static int
partition_tape(struct inode * inode, int size)
{
    int dev = TAPE_NR(inode->i_rdev), result;
    int length;
    Scsi_Tape *STp;
    Scsi_Cmnd * SCpnt = NULL;
    unsigned char cmd[10], *bp;

    if ((result = nbr_partitions(inode)) < 0)
      return result;
    STp = &(scsi_tapes[dev]);

    /* The mode page is in the buffer. Let's modify it and write it. */
    bp = &((STp->buffer)->b_data[0]);
    if (size <= 0) {
      length = 8;
      bp[MODE_HEADER_LENGTH + 3] = 0;
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: Formatting tape with one partition.\n", dev);
#endif
    }
    else {
      length = 10;
      bp[MODE_HEADER_LENGTH + 3] = 1;
      bp[MODE_HEADER_LENGTH + 8] = (size >> 8) & 0xff;
      bp[MODE_HEADER_LENGTH + 9] = size & 0xff;
#if DEBUG
      if (debugging)
	printk(ST_DEB_MSG "st%d: Formatting tape with two partition (1 = %d MB).\n",
	       dev, size);
#endif
    }
    bp[MODE_HEADER_LENGTH + 6] = 0;
    bp[MODE_HEADER_LENGTH + 7] = 0;
    bp[MODE_HEADER_LENGTH + 4] = 0x30;   /* IDP | PSUM = MB */

    bp[0] = 0;
    bp[1] = 0;
    bp[MODE_HEADER_LENGTH] &= 0x3f;
    bp[MODE_HEADER_LENGTH + 1] = length - 2;

    memset(cmd, 0, 10);
    cmd[0] = MODE_SELECT;
    cmd[1] = 0x10;
    cmd[4] = length + MODE_HEADER_LENGTH;

    SCpnt = st_do_scsi(SCpnt, STp, cmd, cmd[4], ST_LONG_TIMEOUT, MAX_READY_RETRIES);
    if (SCpnt == NULL)
      return (-EBUSY);
    SCpnt->request.rq_status = RQ_INACTIVE;  /* Mark as not busy */

    if ((STp->buffer)->last_result_fatal != 0) {
      printk(KERN_INFO "st%d: Partitioning of tape failed.\n", dev);
      result = (-EIO);
    }
    else
      result = 0;

    return result;
}



/* The ioctl command */
	static int
st_ioctl(struct inode * inode,struct file * file,
	 unsigned int cmd_in, unsigned long arg)
{
   int i, cmd_nr, cmd_type, bt;
   unsigned int blk;
   struct mtop mtc;
   struct mtpos mt_pos;
   Scsi_Tape *STp;
   ST_mode *STm;
   ST_partstat *STps;
   int dev = TAPE_NR(inode->i_rdev);

   STp = &(scsi_tapes[dev]);
#if DEBUG
   if (debugging && !STp->in_use) {
     printk(ST_DEB_MSG "st%d: Incorrect device.\n", dev);
     return (-EIO);
   }
#endif
   STm = &(STp->modes[STp->current_mode]);
   STps = &(STp->ps[STp->partition]);

   cmd_type = _IOC_TYPE(cmd_in);
   cmd_nr   = _IOC_NR(cmd_in);

   if (cmd_type == _IOC_TYPE(MTIOCTOP) && cmd_nr == _IOC_NR(MTIOCTOP)) {
     if (_IOC_SIZE(cmd_in) != sizeof(mtc))
       return (-EINVAL);

     i = verify_area(VERIFY_READ, (void *)arg, sizeof(mtc));
     if (i)
	return i;

     memcpy_fromfs((char *) &mtc, (char *)arg, sizeof(struct mtop));

     if (mtc.mt_op == MTSETDRVBUFFER && !suser()) {
       printk(KERN_WARNING "st%d: MTSETDRVBUFFER only allowed for root.\n", dev);
       return (-EPERM);
     }
     if (!STm->defined &&
	 (mtc.mt_op != MTSETDRVBUFFER && (mtc.mt_count & MT_ST_OPTIONS) == 0))
       return (-ENXIO);

     if (!(STp->device)->was_reset) {

       if (STp->eof_hit) {
	 if (mtc.mt_op == MTFSF || mtc.mt_op == MTEOM) {
	   mtc.mt_count -= 1;
	   if ((STp->mt_status)->mt_fileno >= 0)
	     (STp->mt_status)->mt_fileno += 1;
	 }
	 else if (mtc.mt_op == MTBSF) {
	   mtc.mt_count += 1;
	   if ((STp->mt_status)->mt_fileno >= 0)
	     (STp->mt_status)->mt_fileno += 1;
	 }
       }

       i = flush_buffer(inode, file, mtc.mt_op == MTSEEK ||
			mtc.mt_op == MTREW || mtc.mt_op == MTOFFL ||
			mtc.mt_op == MTRETEN || mtc.mt_op == MTEOM ||
			mtc.mt_op == MTLOCK || mtc.mt_op == MTLOAD ||
			mtc.mt_op == MTCOMPRESSION);
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
	 if (st_int_ioctl(inode, MTLOCK, 0)) {
	   printk(KERN_NOTICE "st%d: Could not relock door after bus reset.\n",
		  dev);
	   STp->door_locked = ST_UNLOCKED;
	 }
       }
     }

     if (mtc.mt_op != MTNOP && mtc.mt_op != MTSETBLK &&
	 mtc.mt_op != MTSETDENSITY && mtc.mt_op != MTWSM &&
	 mtc.mt_op != MTSETDRVBUFFER && mtc.mt_op != MTSEEK &&
	 mtc.mt_op != MTSETPART)
       STps->rw = ST_IDLE;  /* Prevent automatic WEOF */

     if (mtc.mt_op == MTOFFL && STp->door_locked != ST_UNLOCKED)
       st_int_ioctl(inode, MTUNLOCK, 0);  /* Ignore result! */

     if (mtc.mt_op == MTSETDRVBUFFER &&
	 (mtc.mt_count & MT_ST_OPTIONS) != 0)
       return st_set_options(inode, mtc.mt_count);
     if (mtc.mt_op == MTSETPART) {
       if (!STp->can_partitions ||
	   mtc.mt_count < 0 || mtc.mt_count >= ST_NBR_PARTITIONS)
	 return (-EINVAL);
       if (mtc.mt_count >= STp->nbr_partitions &&
	   (STp->nbr_partitions = nbr_partitions(inode)) < 0)
	 return (-EIO);
       if (mtc.mt_count >= STp->nbr_partitions)
	 return (-EINVAL);
       STp->new_partition = mtc.mt_count;
       return 0;
     }
     if (mtc.mt_op == MTMKPART) {
       if (!STp->can_partitions)
	 return (-EINVAL);
       if ((i = st_int_ioctl(inode, MTREW, 0)) < 0 ||
	   (i = partition_tape(inode, mtc.mt_count)) < 0)
	 return i;
       for (i=0; i < ST_NBR_PARTITIONS; i++) {
	 STp->ps[i].rw = ST_IDLE;
	 STp->ps[i].moves_after_eof = 1;
	 STp->ps[i].at_sm = 0;
	 STp->ps[i].last_block_valid = FALSE;
       }
       STp->partition = STp->new_partition = 0;
       STp->nbr_partitions = 1;  /* Bad guess ?-) */
       STp->drv_block = (STp->mt_status)->mt_fileno = 0;
       return 0;
     }
     if (mtc.mt_op == MTSEEK) {
       i = set_location(inode, mtc.mt_count, STp->new_partition, 0);
       if (!STp->can_partitions)
	   STp->ps[0].rw = ST_IDLE;
       return i;
     }
     if (STp->can_partitions && STp->ready == ST_READY &&
	 (i = update_partition(inode)) < 0)
       return i;
     if (mtc.mt_op == MTCOMPRESSION)
       return st_compression(STp, (mtc.mt_count & 1));
     else
       return st_int_ioctl(inode, mtc.mt_op, mtc.mt_count);
   }

   if (!STm->defined)
     return (-ENXIO);

   if ((i = flush_buffer(inode, file, FALSE)) < 0)
     return i;
   if (STp->can_partitions &&
       (i = update_partition(inode)) < 0)
     return i;

   if (cmd_type == _IOC_TYPE(MTIOCGET) && cmd_nr == _IOC_NR(MTIOCGET)) {

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
       if (STps->rw == ST_WRITING)
	 (STp->mt_status)->mt_blkno +=
	   (STp->buffer)->buffer_bytes / STp->block_size;
       else if (STps->rw == ST_READING)
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
     (STp->mt_status)->mt_resid = STp->partition;
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
     if (STps->at_sm)
       (STp->mt_status)->mt_gstat |= GMT_SM(0xffffffff);
     if (STm->do_async_writes || (STm->do_buffer_writes && STp->block_size != 0) ||
	 STp->drv_buffer != 0)
       (STp->mt_status)->mt_gstat |= GMT_IM_REP_EN(0xffffffff);

     memcpy_tofs((char *)arg, (char *)(STp->mt_status),
		 sizeof(struct mtget));

     (STp->mt_status)->mt_erreg = 0;  /* Clear after read */
     return 0;
   } /* End of MTIOCGET */

   if (cmd_type == _IOC_TYPE(MTIOCPOS) && cmd_nr == _IOC_NR(MTIOCPOS)) {
     if (_IOC_SIZE(cmd_in) != sizeof(struct mtpos))
       return (-EINVAL);
     if ((i = get_location(inode, &blk, &bt, 0)) < 0)
       return i;
     i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(mt_pos));
     if (i)
	return i;
     mt_pos.mt_blkno = blk;
     memcpy_tofs((char *)arg, (char *) (&mt_pos), sizeof(struct mtpos));
     return 0;
   }

   return scsi_ioctl(STp->device, cmd_in, (void *) arg);
}


/* Try to allocate a new tape buffer */
	static ST_buffer *
new_tape_buffer( int from_initialization, int need_dma )
{
  int priority, a_size;
  ST_buffer *tb;

  if (st_nbr_buffers >= st_template.dev_max)
    return NULL;  /* Should never happen */

  if (from_initialization) {
    priority = GFP_ATOMIC;
    a_size = st_buffer_size;
  }
  else {
    priority = GFP_KERNEL;
    for (a_size = PAGE_SIZE; a_size < st_buffer_size; a_size <<= 1)
      ; /* Make sure we allocate efficiently */
  }
  tb = (ST_buffer *)scsi_init_malloc(sizeof(ST_buffer), priority);
  if (tb) {
    if (need_dma)
      priority |= GFP_DMA;
    tb->b_data = (unsigned char *)scsi_init_malloc(a_size, priority);
    if (!tb->b_data) {
      scsi_init_free((char *)tb, sizeof(ST_buffer));
      tb = NULL;
    }
  }
  if (!tb) {
    printk(KERN_NOTICE "st: Can't allocate new tape buffer (nbr %d).\n",
	   st_nbr_buffers);
    return NULL;
  }
#if DEBUG
  if (debugging)
    printk(ST_DEB_MSG
	   "st: Allocated tape buffer %d (%d bytes, dma: %d, a: %p).\n",
	   st_nbr_buffers, a_size, need_dma, tb->b_data);
#endif
  tb->in_use = 0;
  tb->dma = need_dma;
  tb->buffer_size = a_size;
  tb->writing = 0;
  tb->orig_b_data = NULL;
  st_buffers[st_nbr_buffers++] = tb;
  return tb;
}


/* Try to allocate a temporary enlarged tape buffer */
	static int
enlarge_buffer(ST_buffer *STbuffer, int new_size, int need_dma)
{
  int a_size, priority;
  unsigned char *tbd;

  normalize_buffer(STbuffer);

  for (a_size = PAGE_SIZE; a_size < new_size; a_size <<= 1)
    ;  /* Make sure that we allocate efficiently */

  priority = GFP_KERNEL;
  if (need_dma)
    priority |= GFP_DMA;
  tbd = (unsigned char *)scsi_init_malloc(a_size, priority);
  if (!tbd)
    return FALSE;
#if DEBUG
  if (debugging)
    printk(ST_DEB_MSG
	   "st: Buffer at %p enlarged to %d bytes (dma: %d, a: %p).\n",
	   STbuffer->b_data, a_size, need_dma, tbd);
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
    printk(ST_DEB_MSG "st: Buffer at %p normalized to %d bytes.\n",
	   STbuffer->b_data, STbuffer->buffer_size);
#endif
}


/* Set the boot options. Syntax: st=xxx,yyy
   where xxx is buffer size in 1024 byte blocks and yyy is write threshold
   in 1024 byte blocks. */
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
   ST_mode * STm;
   ST_partstat * STps;
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
   tpnt->eof = ST_NOEOF;
   tpnt->waiting = NULL;
   tpnt->in_use = 0;
   tpnt->drv_buffer = 1;  /* Try buffering if no mode sense */
   tpnt->restr_dma = (SDp->host)->unchecked_isa_dma;
   tpnt->density = 0;
   tpnt->do_auto_lock = ST_AUTO_LOCK;
   tpnt->can_bsr = ST_IN_FILE_POS;
   tpnt->can_partitions = 0;
   tpnt->two_fm = ST_TWO_FM;
   tpnt->fast_mteom = ST_FAST_MTEOM;
   tpnt->scsi2_logical = 0;
   tpnt->write_threshold = st_write_threshold;
   tpnt->default_drvbuffer = 0xff; /* No forced buffering */
   tpnt->partition = 0;
   tpnt->new_partition = 0;
   tpnt->nbr_partitions = 0;
   tpnt->drv_block = (-1);
   (tpnt->mt_status)->mt_fileno = (tpnt->mt_status)->mt_blkno = (-1);

   for (i=0; i < ST_NBR_MODES; i++) {
     STm = &(tpnt->modes[i]);
     STm->defined = FALSE;
     STm->defaults_for_writes = 0;
     STm->do_async_writes = ST_ASYNC_WRITES;
     STm->do_buffer_writes = ST_BUFFER_WRITES;
     STm->do_read_ahead = ST_READ_AHEAD;
     STm->default_compression = ST_DONT_TOUCH;
     STm->default_blksize = (-1);  /* No forced size */
     STm->default_density = (-1);  /* No forced density */
   }

   for (i=0; i < ST_NBR_PARTITIONS; i++) {
     STps = &(tpnt->ps[i]);
     STps->rw = ST_IDLE;
     STps->moves_after_eof = 1;
     STps->at_sm = 0;
     STps->last_block_valid = FALSE;
   }

   tpnt->current_mode = 0;
   tpnt->modes[0].defined = TRUE;

   tpnt->density_changed = tpnt->compression_changed =
     tpnt->blksize_changed = FALSE;

   st_template.nr_dev++;
   return 0;
};

static int st_detect(Scsi_Device * SDp)
{
  if(SDp->type != TYPE_TAPE) return 0;

  printk(KERN_INFO
	 "Detected scsi tape st%d at scsi%d, channel %d, id %d, lun %d\n", 
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
      printk(KERN_ERR "Unable to get major %d for SCSI tapes\n",MAJOR_NR);
      return 1;
    }
    st_registered++;
  }

  if (scsi_tapes) return 0;
  st_template.dev_max = st_template.dev_noticed + ST_EXTRA_DEVS;
  if (st_template.dev_max < ST_MAX_TAPES)
    st_template.dev_max = ST_MAX_TAPES;
  if (st_template.dev_max > 128 / ST_NBR_MODES)
    printk(KERN_INFO "st: Only %d tapes accessible.\n", 128 / ST_NBR_MODES);
  scsi_tapes =
    (Scsi_Tape *) scsi_init_malloc(st_template.dev_max * sizeof(Scsi_Tape),
				   GFP_ATOMIC);
  if (scsi_tapes == NULL) {
    printk(KERN_ERR "Unable to allocate descriptors for SCSI tapes.\n");
    unregister_chrdev(SCSI_TAPE_MAJOR, "st");
    return 1;
  }

#if DEBUG
  printk(ST_DEB_MSG "st: Buffer size %d bytes, write threshold %d bytes.\n",
	 st_buffer_size, st_write_threshold);
#endif

  memset(scsi_tapes, 0, st_template.dev_max * sizeof(Scsi_Tape));
  for (i=0; i < st_template.dev_max; ++i) {
    STp = &(scsi_tapes[i]);
    STp->capacity = 0xfffff;
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
    printk(KERN_ERR "Unable to allocate tape buffer pointers.\n");
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
    if (!new_tape_buffer(TRUE, TRUE)) {
      if (i == 0) {
#if 0
	printk(KERN_ERR "Can't continue without at least one tape buffer.\n");
	unregister_chrdev(SCSI_TAPE_MAJOR, "st");
	scsi_init_free((char *) st_buffers,
		       st_template.dev_max * sizeof(ST_buffer *));
	scsi_init_free((char *) scsi_tapes,
		       st_template.dev_max * sizeof(Scsi_Tape));
	return 1;
#else
	printk(KERN_INFO "No tape buffers allocated at initialization.\n");
	break;
#endif
      }
      printk(KERN_INFO "Number of tape buffers adjusted.\n");
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
  printk(KERN_INFO "st: Unloaded.\n");
}
#endif /* MODULE */
