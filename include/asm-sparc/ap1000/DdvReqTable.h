  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 *	Request table size
 */

#define	TABLE_SIZE	200

/*
 *	Indirect memory address table size
 */

#define	MTABLE_SIZE	1000

static inline int INC_T(int a)
{
	return (++a == TABLE_SIZE?0:a);
}

static inline int INC_ML(int a)
{
	return (++a == MTABLE_SIZE?0:a);
}

/*
 *	Status of requiest table
 */

#define	DDV_ERROR_RETURN	0
#define	DDV_NORMAL_RETURN	1
#define	DDV_REQ_FREE		2
#define DDV_DISKREAD_REQ	3
#define	DDV_DISKWRITE_REQ	4
#define DDV_RAWREAD_REQ		5
#define	DDV_RAWWRITE_REQ	6
#define DDV_CACHEPOSTALL_REQ    11
#define DDV_CACHEFLUSHALL_REQ    12
#define DDV_CAPACITY_REQ        13

/*
 *      Specify type of interrupt (set by opiu in PBUF1)
 */

#define DDV_PRINTK_INTR  1
#define DDV_MLIST_INTR   2
#define DDV_READY_INTR   3
#define DDV_REQCOMP_INTR 4 

struct  RequestInformation {
	volatile int	status;
	int rtn;
	unsigned bnum;
	int argv[8];
};

struct DiskInfo {
  u_long blocks;
  u_long blk_size;
  int pad[8];
  unsigned ptrs[4];
};

struct RequestTable{
  volatile unsigned cell_pointer;	/* Cell requiest pointer */
  volatile unsigned ddv_pointer;	/* DDV operation pointer */
  struct  RequestInformation async_info[TABLE_SIZE];
  volatile unsigned start_mtable;
  volatile unsigned end_mtable;
  unsigned mtable[MTABLE_SIZE];
};

#define PRINT_BUFS 32

struct OPrintBuf {
  char *fmt;
  int args[6];
};

struct OPrintBufArray {
  volatile unsigned option_counter;
  volatile unsigned cell_counter;
  struct OPrintBuf bufs[PRINT_BUFS];  
};

#define ALIGN_SIZE 16
#define ALIGN_BUFS 128
#define ALIGN_BUF_SIZE 1024

struct AlignBuf {
  char *dest;
  unsigned size;
  int offset;
  char buf[ALIGN_BUF_SIZE+2*ALIGN_SIZE];
};

struct OAlignBufArray {
  volatile unsigned option_counter;
  volatile unsigned cell_counter;
  struct AlignBuf bufs[ALIGN_BUFS];
};


