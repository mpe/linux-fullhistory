  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* 
 * ddv.c - Single AP1000 block driver.
 *
 * This block driver performs io operations to the ddv option
 * board. (Hopefully:)
 *
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/sched.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <asm/ap1000/apreg.h>
#include <asm/ap1000/DdvReqTable.h>

#define MAJOR_NR DDV_MAJOR

#include <linux/blk.h>
#include <linux/genhd.h>
#include <linux/hdreg.h> 

#define DDV_DEBUG 0
#define AIR_DISK 1

#define SECTOR_SIZE 512

/* we can have lots of partitions */
#define PARTN_BITS 6
#define NUM_DDVDEVS (1<<PARTN_BITS)

#define PARDISK_BASE (1<<5) /* partitions above this number are 
			       striped	across all the cells */
#define STRIPE_SHIFT   6
#define STRIPE_SECTORS (1<<STRIPE_SHIFT) /* number of sectors per stripe */

#define MAX_BNUM 16
#define MAX_REQUEST (TABLE_SIZE - 2)
#define REQUEST_LOW 16
#define REQUEST_HIGH 4


/* we fake up a block size larger than the physical block size to try
   to make things a bit more efficient */
#define SECTOR_BLOCK_SHIFT 9

#define SECTOR_MASK ((BLOCK_SIZE >> 9) - 1)

/* try to read ahead a bit */
#define DDV_READ_AHEAD 64

static int have_ddv_board = 1;
static unsigned num_options = 0;
static unsigned this_option = 0;

extern int ddv_get_mlist(unsigned mptr[],int bnum);
extern int ddv_set_request(struct request *req,
			   int request_type,int bnum,int mlist,int len,int offset);
extern void ddv_load_kernel(char *opcodep);
extern int ddv_restart_cpu(void);
extern int ddv_mlist_available(void);
static int ddv_revalidate(kdev_t dev, struct gendisk *gdev);
static void ddv_geninit(struct gendisk *ignored);
static void ddv_release(struct inode * inode, struct file * filp);
static void ddv_request1(void);


static char *ddv_opcodep = NULL;
static struct request *next_request = NULL;

static struct wait_queue * busy_wait = NULL;

static int ddv_blocksizes[NUM_DDVDEVS]; /* in bytes */
int ddv_sect_length[NUM_DDVDEVS]; /* in sectors */
int ddv_blk_length[NUM_DDVDEVS]; /* in blocks */

/* these are used by the ddv_daemon, which services remote disk requests */
static struct remote_request *rem_queue = NULL;
static struct remote_request *rem_queue_end;
static struct wait_queue *ddv_daemon_wait = NULL;

static int opiu_kernel_loaded = 0;

static struct {
	unsigned reads, writes, blocks, rq_started, rq_finished, errors;
	unsigned sectors_read, sectors_written;
} ddv_stats;

static struct hd_struct partition_tables[NUM_DDVDEVS];

static struct gendisk ddv_gendisk = {
	MAJOR_NR,	/* Major number */
	DEVICE_NAME,		/* Major name */
	PARTN_BITS,		/* Bits to shift to get real from partition */
	1 << PARTN_BITS,	/* Number of partitions per real */
	1,	        /* maximum number of real */
#ifdef MODULE
	NULL,		/* called from init_module */
#else
        ddv_geninit,     /* init function */
#endif
	partition_tables,/* hd struct */
	ddv_blk_length,	/* block sizes */
	1,		/* number */
	(void *) NULL,	/* internal */
	NULL		/* next */
};


struct ddv_geometry {
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	unsigned long start;
}; 

static struct ddv_geometry ddv_geometry;


struct remote_request {
	union {
		struct remote_request *next;
		void (*fn)(void);
	} u;
	unsigned bnum; /* how many blocks does this contain */
	struct request *reqp; /* pointer to the request on the original cell */
	unsigned cell; /* what cell is the request from */
	struct request req; /* details of the request */
};


static void ddv_set_optadr(void)
{
	unsigned addr = 0x11000000;
	OPT_IO(OBASE) = addr;
	MSC_IO(MSC_OPTADR) = 
		((addr & 0xff000000)>>16) |
			((OPTION_BASE & 0xf0000000)>>24) | 
				((OPTION_BASE + 0x10000000)>>28); 
	OPT_IO(PRST) = 0;
}

extern struct RequestTable *RTable;
extern struct OPrintBufArray *PrintBufs;
extern struct OAlignBufArray *AlignBufs;
extern struct DiskInfo *DiskInfo;

static void ddv_release(struct inode * inode, struct file * filp)
{
#if DEBUG
	printk("ddv_release started\n");
#endif
	sync_dev(inode->i_rdev);
#if DEBUG
	printk("ddv_release done\n");
#endif
}


static unsigned in_request = 0;
static unsigned req_queued = 0;

static void ddv_end_request(int uptodate,struct request *req) 
{
	struct buffer_head * bh;

	ddv_stats.rq_finished++;

/*	printk("ddv_end_request(%d,%p)\n",uptodate,req); */

	req->errors = 0;
	if (!uptodate) {
		printk("end_request: I/O error, dev %s, sector %lu\n",
		       kdevname(req->rq_dev), req->sector);
		req->nr_sectors--;
		req->nr_sectors &= ~SECTOR_MASK;
		req->sector += (BLOCK_SIZE / SECTOR_SIZE);
		req->sector &= ~SECTOR_MASK;		
		ddv_stats.errors++;
	}
	
	if ((bh = req->bh) != NULL) {
		req->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		mark_buffer_uptodate(bh, uptodate);
		unlock_buffer(bh);
		if ((bh = req->bh) != NULL) {
			req->current_nr_sectors = bh->b_size >> 9;
			if (req->nr_sectors < req->current_nr_sectors) {
				req->nr_sectors = req->current_nr_sectors;
				printk("end_request: buffer-list destroyed\n");
			}
			req->buffer = bh->b_data;
			printk("WARNING: ddv: more sectors!\n");
			ddv_stats.errors++;
			return;
		}
	}
	if (req->sem != NULL)
		up(req->sem);
	req->rq_status = RQ_INACTIVE;	
	wake_up(&wait_for_request);
}


/* check that a request is all OK to process */
static int request_ok(struct request *req)
{
	int minor;
	if (!req) return 0;

	if (MAJOR(req->rq_dev) != MAJOR_NR)
		panic(DEVICE_NAME ": bad major number\n");
	if (!buffer_locked(req->bh))
		panic(DEVICE_NAME ": block not locked");
	
	minor = MINOR(req->rq_dev);
	if (minor >= NUM_DDVDEVS) {
		printk("ddv_request: Invalid minor (%d)\n", minor);
		return 0;
	}

	if ((req->sector + req->current_nr_sectors) > ddv_sect_length[minor]) {
		printk("ddv: out of range minor=%d offset=%d len=%d sect_length=%d\n",
		       minor,(int)req->sector,(int)req->current_nr_sectors,
		       ddv_sect_length[minor]);
		return 0;
	}

	if (req->cmd != READ && req->cmd != WRITE) {
		printk("unknown request type %d\n",req->cmd);
		return 0;
	}			

	/* it seems to be OK */
	return 1;
}


static void complete_request(struct request *req,int bnum)
{
	while (bnum--) {
		ddv_end_request(1,req);
		req = req->next;
	}
}


static int completion_pointer = 0;

static void check_completion(void)
{
	int i,bnum;
	struct request *req;

	if (!RTable) return;

	for (;
	     (i=completion_pointer) != RTable->ddv_pointer &&
	     RTable->async_info[i].status == DDV_REQ_FREE;
	     completion_pointer = INC_T(completion_pointer))	     
	{
		req = (struct request *)RTable->async_info[i].argv[7];
		bnum = RTable->async_info[i].bnum;
		if (!req || !bnum) {
			printk("%s(%d)\n",__FILE__,__LINE__);
			ddv_stats.errors++;
			continue;
		}
			
		RTable->async_info[i].status = 0;
		RTable->async_info[i].argv[7] = 0;

		complete_request(req,bnum);
		in_request--;		
	}
}


static struct request *get_request_queue(struct request *oldq)
{
	struct request *req,*req2;

	/* skip any non-active or bad requests */
 skip1:
	if (!(req = CURRENT)) 
		return oldq;

	if (req->rq_status != RQ_ACTIVE) {
		CURRENT = req->next;
		goto skip1;
	}

	if (!request_ok(req)) {
		ddv_end_request(0,req);
		CURRENT = req->next;
		goto skip1;
	}

	/* now grab as many as we can */
	req_queued++;

	for (req2 = req;
	     req2->next && 
	     req2->next->rq_status == RQ_ACTIVE &&
	     request_ok(req2->next);
	     req2 = req2->next) 
		req_queued++;

	/* leave CURRENT pointing at the bad ones */
	CURRENT = req2->next;

	/* chop our list at that point */
	req2->next = NULL;

	if (!oldq)
		return req;

	for (req2=oldq;req2->next;req2=req2->next) ;

	req2->next = req;

	return oldq;	
}


static void ddv_rem_complete(struct remote_request *rem)
{
	unsigned flags;
	int bnum = rem->bnum;
	struct request *req = rem->reqp;

	complete_request(req,bnum);
	in_request--;

	save_flags(flags); cli();
	ddv_request1();
	restore_flags(flags);
}


/*
 * The background ddv daemon. This receives remote disk requests
 * and processes them via the normal block operations
 */
static int ddv_daemon(void *unused)
{
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "ddv_daemon");
	spin_lock_irq(&current->sigmask_lock);
	sigfillset(&current->blocked); /* block all signals */
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
  
	/* Give it a realtime priority. */
	current->policy = SCHED_FIFO;
	current->priority = 32;  /* Fixme --- we need to standardise our
				    namings for POSIX.4 realtime scheduling
				    priorities.  */
  
	printk("Started ddv_daemon\n");
  
	while (1) {
		struct remote_request *rem;
		unsigned flags;
		struct buffer_head *bhlist[MAX_BNUM*4];
		int i,j,minor,len,shift,offset;

		save_flags(flags); cli();

		while (!rem_queue) {
			spin_lock_irq(&current->sigmask_lock);
			flush_signals(current);
			spin_unlock_irq(&current->sigmask_lock);
			interruptible_sleep_on(&ddv_daemon_wait);
		}

		rem = rem_queue;
		rem_queue = rem->u.next;
		restore_flags(flags);
			

		minor = MINOR(rem->req.rq_dev);
		len = rem->req.current_nr_sectors;
		offset = rem->req.sector;

		/* work out the conversion to the local block size from
		   sectors */
		for (shift=0;
		     (SECTOR_SIZE<<shift) != ddv_blocksizes[minor];
		     shift++) ;

		/* do the request */
		for (i=0; len; i++) {
			bhlist[i] = getblk(rem->req.rq_dev, 
					   offset >> shift,
					   ddv_blocksizes[minor]);
			if (!buffer_uptodate(bhlist[i]))
				ll_rw_block(READ,1,&bhlist[i]);
			offset += 1<<shift;
			len -= 1<<shift;
		}
			    
		for (j=0;j<i;j++)
			if (!buffer_uptodate(bhlist[j]))
				wait_on_buffer(bhlist[j]);
		
		
		/* put() the data */
		

		/* release the buffers */
		for (j=0;j<i;j++)
			brelse(bhlist[j]);

		/* tell the originator that its done */
		rem->u.fn = ddv_rem_complete;
		tnet_rpc(rem->cell,rem,sizeof(int)*3,1);
	}
}


/* receive a remote disk request */
static void ddv_rem_queue(char *data,unsigned size)
{
	unsigned flags;
	struct remote_request *rem = (struct remote_request *)
		kmalloc(size,GFP_ATOMIC);

	if (!rem) {
		/* oh bugger! */
		ddv_stats.errors++;
		return;
	}

	memcpy(rem,data,size);
	rem->u.next = NULL;

	save_flags(flags); cli();

	/* add it to our remote request queue */
	if (!rem_queue)
		rem_queue = rem;
	else
		rem_queue_end->u.next = rem;
	rem_queue_end = rem;

	restore_flags(flags);

	wake_up(&ddv_daemon_wait);  
}


/* which disk should this request go to */
static inline unsigned pardisk_num(struct request *req)
{
	int minor = MINOR(req->rq_dev);
	unsigned stripe;
	unsigned cell;

	if (minor < PARDISK_BASE)
		return this_option;

	stripe = req->sector >> STRIPE_SHIFT;
	cell = stripe % num_options;

	return cell;
}


/* check if a 2nd request can be tacked onto the first */
static inline int contiguous(struct request *req1,struct request *req2)
{
	if (req2->cmd != req1->cmd ||
	    req2->rq_dev != req1->rq_dev ||
	    req2->sector != req1->sector + req1->current_nr_sectors ||
	    req2->current_nr_sectors != req1->current_nr_sectors)
		return 0;
	if (pardisk_num(req1) != pardisk_num(req2))
		return 0;
	return 1;
}

static void ddv_request1(void)
{
	struct request *req,*req1,*req2;
	unsigned offset,len,req_num,mlist,bnum,available=0;
	static unsigned mptrs[MAX_BNUM];
	unsigned cell;

	if (in_request > REQUEST_HIGH)
		return;

	next_request = get_request_queue(next_request);

	while ((req = next_request)) {
		int minor;

		if (in_request >= MAX_REQUEST)
			return;

		if (in_request>1 && req_queued<REQUEST_LOW)
			return;
	
		/* make sure we have room for a request */
		available = ddv_mlist_available();
		if (available < 1) return;
		if (available > MAX_BNUM)
			available = MAX_BNUM;

		offset = req->sector;
		len = req->current_nr_sectors;
		minor = MINOR(req->rq_dev);
		  
		mptrs[0] = (int)req->buffer;

		for (bnum=1,req1=req,req2=req->next;
		     req2 && bnum<available && contiguous(req1,req2);
		     req1=req2,req2=req2->next) {
			mptrs[bnum++] = (int)req2->buffer;
		}

		next_request = req2;


		req_queued -= bnum;
		ddv_stats.blocks += bnum;
		ddv_stats.rq_started += bnum;

		if (req->cmd == READ) {
			ddv_stats.reads++;
			ddv_stats.sectors_read += len*bnum;
		} else {
			ddv_stats.writes++;
			ddv_stats.sectors_written += len*bnum;
		}

		if (minor >= PARDISK_BASE) {
			/* translate the request to the normal partition */
			unsigned stripe;
			minor -= PARDISK_BASE;

			stripe = offset >> STRIPE_SHIFT;
			stripe /= num_options;
			offset = (stripe << STRIPE_SHIFT) + 
				(offset & ((1<<STRIPE_SHIFT)-1));
#if AIR_DISK 
			/* like an air-guitar :-) */
			complete_request(req,bnum);
			continue;
#endif
		}

		if ((cell=pardisk_num(req)) != this_option) {
			/* its a remote request */
			struct remote_request *rem;
			unsigned *remlist;
			unsigned size = sizeof(*rem) + sizeof(int)*bnum;

			rem = (struct remote_request *)kmalloc(size,GFP_ATOMIC);
			if (!rem) {
				/* hopefully we can get it on the next go */
				return;
			}
			remlist = (unsigned *)(rem+1);
			
			rem->u.fn = ddv_rem_queue;
			rem->cell = this_option;
			rem->bnum = bnum;
			rem->req = *req;
			rem->reqp = req;
			rem->req.rq_dev = MKDEV(MAJOR_NR,minor);
			rem->req.sector = offset;
			memcpy(remlist,mptrs,sizeof(mptrs[0])*bnum);
			
			if (tnet_rpc(cell,rem,size,1) != 0) {
				kfree_s(rem,size);
				return;
			}
		} else {
			/* its a local request */
			if ((mlist = ddv_get_mlist(mptrs,bnum)) == -1) {
				ddv_stats.errors++;
				panic("ddv: mlist corrupted");
			}

			req_num = RTable->cell_pointer;
			RTable->async_info[req_num].status = 
				req->cmd==READ?DDV_RAWREAD_REQ:DDV_RAWWRITE_REQ;
			RTable->async_info[req_num].bnum = bnum;
			RTable->async_info[req_num].argv[0] = mlist;
			RTable->async_info[req_num].argv[1] = len;
			RTable->async_info[req_num].argv[2] = offset + 
				partition_tables[minor].start_sect;
			RTable->async_info[req_num].argv[3] = bnum;
			RTable->async_info[req_num].argv[7] = (unsigned)req;
			RTable->cell_pointer = INC_T(RTable->cell_pointer);
			
		}

		in_request++;	
	}
}


static void ddv_request(void)
{
	cli();
	ddv_request1();
	sti();
}


static void check_printbufs(void)
{  
	int i;

	if (!PrintBufs) return;

	while (PrintBufs->option_counter != PrintBufs->cell_counter) {
		i = PrintBufs->cell_counter;
		printk("opiu (%d): ",i); 
		if (((unsigned)PrintBufs->bufs[i].fmt) > 0x100000)
			printk("Error: bad format in printk at %p\n", 
			       PrintBufs->bufs[i].fmt);
		else
			printk(PrintBufs->bufs[i].fmt + OPIBUS_BASE,
			       PrintBufs->bufs[i].args[0],
			       PrintBufs->bufs[i].args[1],
			       PrintBufs->bufs[i].args[2],
			       PrintBufs->bufs[i].args[3],
			       PrintBufs->bufs[i].args[4],
			       PrintBufs->bufs[i].args[5]);
		if (++PrintBufs->cell_counter == PRINT_BUFS)
			PrintBufs->cell_counter = 0;
	}
}

static void ddv_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	save_flags(flags); cli();
	OPT_IO(IRC1) = 0x80000000;

	check_printbufs();
	check_completion();

	ddv_request1();
	restore_flags(flags);
}

static int ddv_open(struct inode * inode, struct file * filp)
{
	int minor = MINOR(inode->i_rdev);

	if (!have_ddv_board || minor >= NUM_DDVDEVS)
		return -ENODEV;

	if (minor >= PARDISK_BASE) {
		ddv_sect_length[minor] = ddv_sect_length[minor - PARDISK_BASE];
		ddv_blk_length[minor] = ddv_blk_length[minor - PARDISK_BASE];
	}

	return 0;
}


static void ddv_open_reply(struct cap_request *creq)
{
	int size = creq->size - sizeof(*creq);
	ddv_opcodep = (char *)kmalloc(size,GFP_ATOMIC);
	read_bif(ddv_opcodep, size);
#if DEBUG
	printk("received opiu kernel of size %d\n",size);
#endif
	if (size == 0)
		have_ddv_board = 0;
	wake_up(&busy_wait);	
}


static void ddv_load_opiu(void)
{
	int i;
	struct cap_request creq;

	/* if the opiu kernel is already loaded then we don't do anything */
	if (!have_ddv_board || opiu_kernel_loaded)
		return;
  
	bif_register_request(REQ_DDVOPEN,ddv_open_reply);

	/* send the open request to the front end */
	creq.cid = mpp_cid();
	creq.type = REQ_DDVOPEN;
	creq.header = 0;
	creq.size = sizeof(creq);
	
	bif_queue(&creq,0,0);
	
	ddv_set_optadr();	

	while (!ddv_opcodep)
		sleep_on(&busy_wait);

	if (!have_ddv_board)
		return;

	ddv_load_kernel(ddv_opcodep);
	
	kfree(ddv_opcodep);
	ddv_opcodep = NULL;
	
	if (ddv_restart_cpu()) 
		return;

	ddv_sect_length[0] = DiskInfo->blocks;
	ddv_blk_length[0] = DiskInfo->blocks >> 1;
	ddv_blocksizes[0] = BLOCK_SIZE;

	ddv_geometry.cylinders = ddv_sect_length[0] / 
		(ddv_geometry.heads*ddv_geometry.sectors);

	ddv_gendisk.part[0].start_sect = 0;
	ddv_gendisk.part[0].nr_sects = ddv_sect_length[0];

	resetup_one_dev(&ddv_gendisk, 0);
	
	for (i=0;i<PARDISK_BASE;i++) {
		ddv_sect_length[i] = ddv_gendisk.part[i].nr_sects;
		ddv_blk_length[i] = ddv_gendisk.part[i].nr_sects >> 1;
	}

	/* setup the parallel partitions by multiplying the normal
	   partition by the number of options */
	for (;i<NUM_DDVDEVS;i++) {
		ddv_sect_length[i] = ddv_sect_length[i-PARDISK_BASE]*num_options;
		ddv_blk_length[i] = ddv_blk_length[i-PARDISK_BASE]*num_options;
		ddv_gendisk.part[i].start_sect = ddv_gendisk.part[i-PARDISK_BASE].start_sect;
		ddv_gendisk.part[i].nr_sects = ddv_sect_length[i];
	}


	opiu_kernel_loaded = 1;       
}


/*
 * This routine is called to flush all partitions and partition tables
 * for a changed disk, and then re-read the new partition table.
 */
static int ddv_revalidate(kdev_t dev, struct gendisk *gdev)
{
	int target;
	int max_p;
	int start;
	int i;

	target = DEVICE_NR(dev);

	max_p = gdev->max_p;
	start = target << gdev->minor_shift;

	printk("ddv_revalidate dev=%d target=%d max_p=%d start=%d\n",
	       dev,target,max_p,start);

	for (i=max_p - 1; i >=0 ; i--) {
		int minor = start + i;
		kdev_t devi = MKDEV(gdev->major, minor);
		sync_dev(devi);
		invalidate_inodes(devi);
		invalidate_buffers(devi);
		gdev->part[minor].start_sect = 0;
		gdev->part[minor].nr_sects = 0;
	};

	ddv_sect_length[start] = DiskInfo->blocks;
	ddv_blk_length[start] = DiskInfo->blocks >> 1;

	gdev->part[start].nr_sects = ddv_sect_length[start];
	resetup_one_dev(gdev, target);

	printk("sect_length[%d]=%d blk_length[%d]=%d\n",
	       start,ddv_sect_length[start],
	       start,ddv_blk_length[start]);

	for (i=0;i<max_p;i++) {
		ddv_sect_length[start+i] = gdev->part[start+i].nr_sects;
		ddv_blk_length[start+i] = gdev->part[start+i].nr_sects >> 1;
		if (gdev->part[start+i].nr_sects)
			printk("partition[%d] start=%d length=%d\n",i,
			       (int)gdev->part[start+i].start_sect,
			       (int)gdev->part[start+i].nr_sects);
	}

	return 0;
}




static int ddv_ioctl(struct inode *inode, struct file *file, 
		    unsigned int cmd, unsigned long arg)
{
	int err;
	struct ddv_geometry *loc = (struct ddv_geometry *) arg;
	int dev;
	int minor = MINOR(inode->i_rdev);
  
	if ((!inode) || !(inode->i_rdev))
		return -EINVAL;
	dev = DEVICE_NR(inode->i_rdev);
#if DEBUG
	printk("ddv_ioctl: cmd=%x dev=%x minor=%d\n", cmd, dev, minor);
#endif
	switch (cmd) {
	case HDIO_GETGEO:
		printk("\tHDIO_GETGEO\n");
		if (!loc)  return -EINVAL;
		if (put_user(ddv_geometry.heads, (char *) &loc->heads)) return -EFAULT;
		if (put_user(ddv_geometry.sectors, (char *) &loc->sectors)) return -EFAULT;
		if (put_user(ddv_geometry.cylinders, (short *) &loc->cylinders)) return -EFAULT;
		if (put_user(ddv_geometry.start, (long *) &loc->start)) return -EFAULT;
		return 0;
  
	case HDIO_GET_MULTCOUNT :
		printk("\tHDIO_GET_MULTCOUNT\n");
		return -EINVAL;

	case HDIO_GET_IDENTITY :
		printk("\tHDIO_GET_IDENTITY\n");
		return -EINVAL;

	case HDIO_GET_NOWERR :
		printk("\tHDIO_GET_NOWERR\n");
		return -EINVAL;

	case HDIO_SET_NOWERR :
		printk("\tHDIO_SET_NOWERR\n");
		return -EINVAL;

	case BLKRRPART:
		printk("\tBLKRRPART\n");
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		return ddv_revalidate(inode->i_rdev,&ddv_gendisk);

	case BLKGETSIZE:   /* Return device size */
		if (put_user(ddv_sect_length[minor],(long *) arg)) return -EFAULT;
#if DEBUG
		printk("BLKGETSIZE gave %d\n",ddv_sect_length[minor]);
#endif
		return 0;
    
	default:
		printk("ddv_ioctl: Invalid cmd=%d(0x%x)\n", cmd, cmd);
		return -EINVAL;
	};
}

static struct file_operations ddv_fops = {
        NULL,                   /* lseek - default */
        block_read,         /* read */
        block_write,        /* write */
        NULL,                   /* readdir - bad */
        NULL,                   /* poll */
        ddv_ioctl,               /* ioctl */
        NULL,                   /* mmap */
        ddv_open,                /* open */
	NULL,			/* flush */
	ddv_release,
        block_fsync          /* fsync */
};


static void ddv_status(void)
{
	if (!have_ddv_board) {
		printk("no ddv board\n");
		return;
	}

	printk("
in_request %u   req_queued %u
MTable:    start=%u end=%u
Requests:  started=%u finished=%u
Requests:  completion_pointer=%u ddv_pointer=%u cell_pointer=%u
PrintBufs: option_counter=%u cell_counter=%u
ddv_stats: reads=%u writes=%u blocks=%u
ddv_stats: sectors_read=%u sectors_written=%u
CURRENT=%p next_request=%p errors=%u
",
	       in_request,req_queued,
	       RTable->start_mtable,RTable->end_mtable,
	       ddv_stats.rq_started,ddv_stats.rq_finished,
	       completion_pointer,RTable->ddv_pointer,RTable->cell_pointer,
	       PrintBufs->option_counter,PrintBufs->cell_counter,
	       ddv_stats.reads,ddv_stats.writes,ddv_stats.blocks,
	       ddv_stats.sectors_read,ddv_stats.sectors_written,
	       CURRENT,next_request,
	       ddv_stats.errors);	
}


int ddv_init(void)
{
	int cid;
	
	cid = mpp_cid();
	
	if (register_blkdev(MAJOR_NR,DEVICE_NAME,&ddv_fops)) {
		printk("ap: unable to get major %d for ap block dev\n",
		       MAJOR_NR);
		return -1;
	}

	printk("ddv_init: register dev %d\n", MAJOR_NR);
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	read_ahead[MAJOR_NR] = DDV_READ_AHEAD;
	
	bif_add_debug_key('d',ddv_status,"DDV status");
	ddv_gendisk.next = gendisk_head;
	gendisk_head = &ddv_gendisk;

	num_options = mpp_num_cells();
	this_option = mpp_cid();

	kernel_thread(ddv_daemon, NULL, 0);

	return(0);
}


static void ddv_geninit(struct gendisk *ignored)
{
	int i;
	static int done = 0;

	if (done)
		printk("ddv_geninit already done!\n");

	done = 1;

	printk("ddv_geninit\n");

	/* request interrupt line 2 */
	if (request_irq(APOPT0_IRQ,ddv_interrupt,SA_INTERRUPT,"apddv",NULL)) {
		printk("Failed to install ddv interrupt handler\n");
	}
	
	for (i=0;i<NUM_DDVDEVS;i++) {
		ddv_blocksizes[i] = BLOCK_SIZE;
		ddv_sect_length[i] = 0;
		ddv_blk_length[i] = 0;
	}

	ddv_geometry.heads = 32;
	ddv_geometry.sectors = 32;
	ddv_geometry.cylinders = 1;
	ddv_geometry.start = 0;
	
	blksize_size[MAJOR_NR] = ddv_blocksizes;

	ddv_load_opiu();
}


/* loadable module support */

#ifdef MODULE

int init_module(void)
{
	int error = ddv_init();
	if (!error) {
		ddv_geninit(&(struct gendisk) { 0,0,0,0,0,0,0,0,0,0,0 });
		printk(KERN_INFO "DDV: Loaded as module.\n");
	}
	return error;
}

/* Before freeing the module, invalidate all of the protected buffers! */
void cleanup_module(void)
{
	int i;
	struct gendisk ** gdp;

	for (i = 0 ; i < NUM_DDVDEVS; i++)
		invalidate_buffers(MKDEV(MAJOR_NR, i));

	/* reset the opiu */
	OPT_IO(OPIU_OP) = OPIU_RESET; 
	OPT_IO(PRST) = PRST_IRST; 

	unregister_blkdev( MAJOR_NR, DEVICE_NAME );
	for (gdp = &gendisk_head; *gdp; gdp = &((*gdp)->next))
		if (*gdp == &ddv_gendisk)
			break;
	if (*gdp)
		*gdp = (*gdp)->next;
	free_irq(APOPT0_IRQ, NULL);
	blk_dev[MAJOR_NR].request_fn = 0;
}

#endif  /* MODULE */


