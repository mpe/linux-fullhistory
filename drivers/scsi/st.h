
#ifndef _ST_H
	#define _ST_H
/*
	$Header: /usr/src/linux/kernel/blk_drv/scsi/RCS/st.h,v 1.1 1992/04/24 18:01:50 root Exp root $
*/

#ifndef _SCSI_H
#include "scsi.h"
#endif

/* The tape buffer descriptor. */
typedef struct {
  unsigned char in_use;
  unsigned char dma;	/* DMA-able buffer */
  int buffer_size;
  int buffer_blocks;
  int buffer_bytes;
  int read_pointer;
  int writing;
  int last_result;
  int last_result_fatal;
  unsigned char *b_data;
  int orig_size;
  unsigned char *orig_b_data;
} ST_buffer;


/* The tape mode definition */
typedef struct {
  unsigned char defined;
  unsigned char do_async_writes;
  unsigned char do_buffer_writes;
  unsigned char do_read_ahead;
  unsigned char defaults_for_writes;
  unsigned char default_compression; /* 0 = don't touch, etc */
  short default_density; /* Forced density, -1 = no value */
  int default_blksize;	/* Forced blocksize, -1 = no value */
} ST_mode;

#define ST_NBR_MODE_BITS 2
#define ST_NBR_MODES (1 << ST_NBR_MODE_BITS)
#define ST_MODE_SHIFT (7 - ST_NBR_MODE_BITS)
#define ST_MODE_MASK ((ST_NBR_MODES - 1) << ST_MODE_SHIFT)

/* The status related to each partition */
typedef struct {
  unsigned char rw;
  unsigned char moves_after_eof;
  unsigned char at_sm;
  unsigned char last_block_valid;
  u32 last_block_visited;
} ST_partstat;

#define ST_NBR_PARTITIONS 4

/* The tape drive descriptor */
typedef struct {
  kdev_t devt;
  unsigned capacity;
  struct wait_queue * waiting;
  Scsi_Device* device;
  Scsi_Cmnd SCpnt;
  struct semaphore sem;
  ST_buffer * buffer;

  /* Drive characteristics */
  unsigned char omit_blklims;
  unsigned char do_auto_lock;
  unsigned char can_bsr;
  unsigned char can_partitions;
  unsigned char two_fm;
  unsigned char fast_mteom;
  unsigned char restr_dma;
  unsigned char scsi2_logical;
  unsigned char default_drvbuffer;  /* 0xff = don't touch, value 3 bits */
  int write_threshold;

  /* Mode characteristics */
  ST_mode modes[ST_NBR_MODES];
  int current_mode;

  /* Status variables */
  int partition;
  int new_partition;
  int nbr_partitions;    /* zero until partition support enabled */
  ST_partstat ps[ST_NBR_PARTITIONS];
  unsigned char dirty;
  unsigned char ready;
  unsigned char eof;
  unsigned char write_prot;
  unsigned char drv_write_prot;
  unsigned char in_use;
  unsigned char eof_hit;
  unsigned char blksize_changed;
  unsigned char density_changed;
  unsigned char compression_changed;
  unsigned char drv_buffer;
  unsigned char density;
  unsigned char door_locked;
  unsigned char rew_at_close;
  int block_size;
  int min_block;
  int max_block;
  int recover_count;
  int drv_block;	/* The block where the drive head is */
  struct mtget * mt_status;

#if DEBUG
  unsigned char write_pending;
  int nbr_finished;
  int nbr_waits;
#endif
} Scsi_Tape;

extern Scsi_Tape * scsi_tapes;

/* Values of eof */
#define	ST_NOEOF	0
#define	ST_FM		1
#define	ST_EOM_OK	2
#define ST_EOM_ERROR	3
#define ST_EOD		4

/* Values of rw */
#define	ST_IDLE		0
#define	ST_READING	1
#define	ST_WRITING	2

/* Values of ready state */
#define ST_READY	0
#define ST_NOT_READY	1
#define ST_NO_TAPE	2

/* Values for door lock state */
#define ST_UNLOCKED	0
#define ST_LOCKED_EXPLICIT 1
#define ST_LOCKED_AUTO  2
#define ST_LOCK_FAILS   3

/* Positioning SCSI-commands for Tandberg, etc. drives */
#define	QFA_REQUEST_BLOCK	0x02
#define	QFA_SEEK_BLOCK		0x0c

/* Setting the binary options */
#define ST_DONT_TOUCH  0
#define ST_NO          1
#define ST_YES         2

#endif

