  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */

/* aplib kernel interface definition */

#ifndef _APLIB_H_
#define _APLIB_H_

struct aplib_struct {
	unsigned *ringbuf;
	unsigned write_pointer, read_pointer; /* in words */
	unsigned ringbuf_size; /* in words */
	unsigned rbuf_counter; /* read messages */
	unsigned rbuf_flag1, rbuf_flag2; /* received messages */
	unsigned *physical_cid; /* logical to physical mapping */
	unsigned *rel_cid;      /* logical to relative (RTC) mapping */
	unsigned numcells; /* number of logical cells */
	unsigned numcells_x; /* number of logical cells in x direction */
	unsigned numcells_y; /* number of logical cells in y direction */
	unsigned cid, tid;      /* this cells logical cell ID and task ID */
	unsigned cidx, cidy;    /* logical cell id in x and y direction */
	unsigned ack_flag, ack_request;
	unsigned ok_x, ok_y, ok_xy; /* whether hardware x, y and xy sends are allowed */
};


/*
 * the system ringbuffer structure
 * this is also the old way that tasks accessed the MSC hardware
 */
struct ringbuf_struct {
	void *ringbuf; /* pointer to the ringbuf */
	void *shared; /* pointer to the shared page */
	int order; /* arg to __get_free_pages */
	unsigned write_ptr; /* write pointer into the ringbuf */
	unsigned vaddr; /* base virtual address of ringbuf for task */
	unsigned frag_count; /* how many words in the frag queue */
	unsigned frag_len; /* how many words expected in the frag queue */
	unsigned sq_fragment[16]; /* if the task switches part way through
				     an op then shove the partial op here */
};


#define APLIB_INIT    1
#define APLIB_SYNC    2
#define APLIB_GET     3
#define APLIB_PUT     4
#define APLIB_SEND    5
#define APLIB_PROBE   6
#define APLIB_POLL    7
#define APLIB_XSEND   8
#define APLIB_YSEND   9
#define APLIB_XYSEND 10
#define APLIB_XPUT   11
#define APLIB_YPUT   12
#define APLIB_XYPUT  13


/* message kinds */
#define RBUF_SYSTEM  0
#define RBUF_SEND    1
#define RBUF_X_BRD   2
#define RBUF_Y_BRD   3
#define RBUF_XY_BRD  4
#define RBUF_RPC     5
#define RBUF_GET     6
#define RBUF_MPI     7
#define RBUF_BIGSEND 8
#define RBUF_SEEN   0xE
#define RBUF_READ   0xF

#define APLIB_PAGE_BASE 0xd0000000
#define APLIB_PAGE_LEN  8192

struct aplib_init {
	unsigned numcells, cid;
	unsigned numcells_x, numcells_y;
	unsigned *phys_cells; /* physical cell list */
	unsigned *ringbuffer;  /* pointer to user supplied ring buffer */
	unsigned ringbuf_size; /* in words */
};


struct aplib_putget {
	unsigned cid;
	unsigned *src_addr, *dest_addr;
	unsigned size; /* in words */
	unsigned *dest_flag, *src_flag;
	unsigned ack;
};


struct aplib_send {
	/* the ordering here is actually quite important - the parts to be
	   read by the bigrecv function must be in the first 24 bytes */
	unsigned src_addr;
	unsigned size;
	unsigned info1, info2;
	unsigned flag_addr;
	volatile unsigned flag;
	unsigned type;
	unsigned tag;
	unsigned cid;
};

#ifdef __KERNEL__
#define MAX_PUT_SIZE (1024*1024 - 1) /* in words */
#define SMALL_SEND_THRESHOLD 128


#endif

#endif /* _APLIB_H_ */

