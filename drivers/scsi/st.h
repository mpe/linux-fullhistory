
#ifndef _ST_H
	#define _ST_H
/*
	$Header: /usr/src/linux/kernel/blk_drv/scsi/RCS/st.h,v 1.1 1992/04/24 18:01:50 root Exp root $
*/

#ifndef _SCSI_H
#include "scsi.h"
#endif

typedef struct {
  int in_use;
  int buffer_size;
  int buffer_blocks;
  int buffer_bytes;
  int read_pointer;
  int writing;
  int last_result;
  int last_result_fatal;
  unsigned char b_data[1];
} ST_buffer;

typedef struct {
  unsigned capacity;
  struct wait_queue * waiting;
  Scsi_Device* device;
  unsigned char dirty;
  unsigned char rw;
  unsigned char ready;
  unsigned char eof;
  unsigned char write_prot;
  unsigned char drv_write_prot;
  unsigned char in_use;
  unsigned char eof_hit;
  unsigned char drv_buffer;
  unsigned char do_buffer_writes;
  unsigned char do_async_writes;
  unsigned char do_read_ahead;
  unsigned char two_fm;
  unsigned char fast_mteom;
  unsigned char density;
  ST_buffer * buffer;
  int block_size;
  int min_block;
  int max_block;
  int write_threshold;
  int recover_count;
  int drv_block;	/* The block where the drive head is */
  unsigned char moves_after_eof;
  unsigned char at_sm;
  struct mtget * mt_status;
  Scsi_Cmnd SCpnt;
} Scsi_Tape;

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

/* Positioning SCSI-commands for Tandberg, etc. drives */
#define	QFA_REQUEST_BLOCK	0x02
#define	QFA_SEEK_BLOCK		0x0c

#endif

