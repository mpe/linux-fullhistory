  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* 
 * ap.c - Single AP1000 block driver.
 *
 * (C) dwalsh, Pious project, DCS, ANU 1996
 *
 * This block driver is designed to simply to perform
 * io operations to the hosts file system. 
 *
 * Heavily modified by tridge
 *
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/ap1000/apservice.h>

#define AP_DEBUG 0

#define MAJOR_NR APBLOCK_MAJOR
#define AP_DRIVER 1
#include <linux/blk.h>

#define NUM_APDEVS 8
#define MAX_REQUESTS 1

static struct wait_queue * busy_wait = NULL;

static int ap_blocksizes[NUM_APDEVS];
static int ap_length[NUM_APDEVS];
static int ap_fds[NUM_APDEVS];

#define SECTOR_BLOCK_SHIFT 9
#define AP_BLOCK_SHIFT 12 /* 4k blocks */
#define AP_BLOCK_SIZE (1<<AP_BLOCK_SHIFT)

static volatile int request_count = 0;

static void ap_release(struct inode * inode, struct file * filp)
{
	MOD_DEC_USE_COUNT;
}

static void ap_request(void)
{
  struct cap_request creq;
  unsigned int minor;
  int offset, len;
  struct request *req;

  if (request_count >= MAX_REQUESTS) return;

repeat:  

  if (!CURRENT) {
    return;
  }

  if (MAJOR(CURRENT->rq_dev) != MAJOR_NR) {
    panic(DEVICE_NAME ": request list destroyed");
  }
  if (CURRENT->bh) {
    if (!buffer_locked(CURRENT->bh)) {
      panic(DEVICE_NAME ": block not locked");
    }
  }

  req = CURRENT;

  minor = MINOR(req->rq_dev);

  if (minor >= NUM_APDEVS) {
    printk("apblock: request for invalid minor %d\n",minor);
    end_request(0);
    goto repeat;
  }

  offset = req->sector;
  len = req->current_nr_sectors;

  if ((offset + len) > ap_length[minor]) {
    printk("apblock: request for invalid sectors %d -> %d\n",
	   offset,offset+len);
    end_request(0);
    goto repeat;
  }

  if (ap_fds[minor] == -1) {
    printk("apblock: minor %d not open\n",minor);
    end_request(0);
    goto repeat;
  }

  /* convert to our units */
  offset <<= SECTOR_BLOCK_SHIFT;
  len <<= SECTOR_BLOCK_SHIFT;
  
  /* setup a request for the host */
  creq.cid = mpp_cid();
  creq.size = sizeof(creq);
  creq.header = 0;
  creq.data[0] = (int)(req);
  creq.data[1] = ap_fds[minor];
  creq.data[2] = offset;
  creq.data[3] = len;

  switch (req->cmd) {
  case READ:
#if AP_DEBUG
    printk("apblock: read req=0x%x len=%d offset=%d\n",
	   req,len,offset);
#endif
    creq.type = REQ_BREAD;
    if (bif_queue(&creq,0,0)) {
      return;
    }
    break;

  case WRITE:
#if AP_DEBUG
    printk("apblock: write req=0x%x len=%d offset=%d\n",
	   req,len,offset);
#endif
    creq.type = REQ_BWRITE;
    creq.size += len;
    if (bif_queue_nocopy(&creq,req->buffer,creq.size - sizeof(creq))) {
      return;
    }
    break;

  default:
    printk("apblock: unknown ap op %d\n",req->cmd);
    end_request(0);
    return;
  }

  if (++request_count < MAX_REQUESTS)
    goto repeat;
}

/* this is called by ap1000/bif.c when a read/write has completed */
void ap_complete(struct cap_request *creq)
{  
#if AP_DEBUG
  struct request *req = (struct request *)(creq->data[0]);

  printk("request 0x%x complete\n",req);
#endif
  end_request(1);
  request_count--;
  ap_request();
}


/* this is called by ap1000/bif.c to find a buffer to put a BREAD into 
 using DMA */
char *ap_buffer(struct cap_request *creq)
{  
  struct request *req = (struct request *)(creq->data[0]);

  return(req->buffer);
}


static int ap_open(struct inode * inode, struct file * filp)
{
  struct cap_request creq;
  int minor;
  minor =  DEVICE_NR(inode->i_rdev);
  
#if AP_DEBUG
  printk("ap_open: minor=%x\n", minor);
#endif

  if (minor >= NUM_APDEVS)
    return -ENODEV;

  /* if its already open then don't do anything */
  if (ap_fds[minor] != -1)
    return 0;

  /* send the open request to the front end */
  creq.cid = mpp_cid();
  creq.type = REQ_BOPEN;
  creq.header = 0;
  creq.size = sizeof(creq);
  creq.data[0] = minor;

  bif_queue(&creq,0,0);

  /* wait for the reply */
  while (ap_fds[minor] == -1)
    sleep_on(&busy_wait);

  return 0;
}


static int ap_ioctl(struct inode *inode, struct file *file, 
		    unsigned int cmd, unsigned long arg)
{
	if (!inode || !inode->i_rdev) 	
		return -EINVAL;

	switch (cmd) {
	case BLKGETSIZE:   /* Return device size */
		if (put_user(ap_length[MINOR(inode->i_rdev)],(long *) arg))
			return -EFAULT;
		return 0;
    
	default:
		break;
	};
	
	return 0;
}


/* this is called by ap1000/bif.c when a open reply comes in */
void ap_open_reply(struct cap_request *creq)
{
  int minor = creq->data[0];

  ap_fds[minor] = creq->data[1];
  ap_length[minor] = creq->data[2] >> SECTOR_BLOCK_SHIFT;

#if AP_DEBUG
  printk("ap opened minor %d length=%d fd=%d\n",
	 minor,ap_length[minor],ap_fds[minor]);
#endif

  wake_up(&busy_wait);  
} 

static struct file_operations ap_fops = {
	NULL,                   /* lseek - default */
	block_read,             /* read - general block-dev read */
	block_write,            /* write - general block-dev write */
	NULL,                   /* readdir - bad */
	NULL,                   /* poll */
	ap_ioctl,               /* ioctl */
	NULL,                   /* mmap */
	ap_open,                /* open */
	NULL,			/* flush */
	ap_release,		/* module needs to decrement use count */
	block_fsync,            /* fsync */
};


int ap_init(void)
{
  int i;
  static int done = 0;

  if (done) return(1);

  if (register_blkdev(MAJOR_NR,"apblock",&ap_fops)) {
    printk("ap: unable to get major %d for ap block dev\n",MAJOR_NR);
    return -1;
  }
  printk("ap_init: register dev %d\n", MAJOR_NR);
  blk_dev[MAJOR_NR].request_fn = &ap_request;

  for (i=0;i<NUM_APDEVS;i++) {
    ap_blocksizes[i] = AP_BLOCK_SIZE;
    ap_length[i] = 0;
    ap_fds[i] = -1;
  }

  blksize_size[MAJOR_NR] = ap_blocksizes;

  read_ahead[MAJOR_NR] = 32; /* 16k read ahead */

  return(0);
}

/* loadable module support */

#ifdef MODULE

int init_module(void)
{
	int error = ap_init();
	if (!error)
		printk(KERN_INFO "APBLOCK: Loaded as module.\n");
	return error;
}

/* Before freeing the module, invalidate all of the protected buffers! */
void cleanup_module(void)
{
	int i;

	for (i = 0 ; i < NUM_APDEVS; i++)
		invalidate_buffers(MKDEV(MAJOR_NR, i));

	unregister_blkdev( MAJOR_NR, "apblock" );
	blk_dev[MAJOR_NR].request_fn = 0;
}

#endif  /* MODULE */
