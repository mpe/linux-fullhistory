/*
 *	I2O block device driver. 
 *
 *	(C) Copyright 1999   Red Hat Software
 *	
 *	Written by Alan Cox, Building Number Three Ltd
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 * 	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	This is a beta test release. Most of the good code was taken
 *	from the nbd driver by Pavel Machek, who in turn took some of it
 *	from loop.c. Isn't free software great for reusability 8)
 *
 *	Fixes:
 *		Steve Ralston:	Multiple device handling error fixes,
 *				Added a queue depth.
 *		Alan Cox:	FC920 has an rmw bug. Dont or in the
 *				end marker.
 *				Removed queue walk, fixed for 64bitness.
 *	To do:
 *		Multiple majors
 *		Serial number scanning to find duplicates for FC multipathing
 *		Set the new max_sectors according to max message size
 *		Use scatter gather chains for bigger I/O sizes
 */

#include <linux/major.h>

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/ioctl.h>
#include <linux/i2o.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/malloc.h>
#include <linux/hdreg.h>

#include <linux/notifier.h>
#include <linux/reboot.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>

#define MAJOR_NR I2O_MAJOR

#include <linux/blk.h>

#define MAX_I2OB	16

#define MAX_I2OB_DEPTH	32

/*
 *	Some of these can be made smaller later
 */

static int i2ob_blksizes[MAX_I2OB<<4];
static int i2ob_hardsizes[MAX_I2OB<<4];
static int i2ob_sizes[MAX_I2OB<<4];
static int i2ob_media_change_flag[MAX_I2OB];
static u32 i2ob_max_sectors[MAX_I2OB<<4];

static int i2ob_context;

static spinlock_t i2ob_lock = SPIN_LOCK_UNLOCKED;

struct i2ob_device
{
	struct i2o_controller *controller;
	struct i2o_device *i2odev;
	int tid;
	int flags;
	int refcnt;
	struct request *head, *tail;
	int done_flag;
};

/*
 *	FIXME:
 *	We should cache align these to avoid ping-ponging lines on SMP
 *	boxes under heavy I/O load...
 */
 
struct i2ob_request
{
	struct i2ob_request *next;
	struct request *req;
	int num;
};


/*
 *	Each I2O disk is one of these.
 */

static struct i2ob_device i2ob_dev[MAX_I2OB<<4];
static int i2ob_devices = 0;
static struct hd_struct i2ob[MAX_I2OB<<4];
static struct gendisk i2ob_gendisk;	/* Declared later */

static atomic_t queue_depth;		/* For flow control later on */
static struct i2ob_request i2ob_queue[MAX_I2OB_DEPTH+1];
static struct i2ob_request *i2ob_qhead;

#define DEBUG( s )
/* #define DEBUG( s ) printk( s ) 
 */

static int i2ob_install_device(struct i2o_controller *, struct i2o_device *, int);
static void i2ob_end_request(struct request *);
static void do_i2ob_request(void);

/*
 *	Get a message
 */

static u32 i2ob_get(struct i2ob_device *dev)
{ 
	struct i2o_controller *c=dev->controller;
	return I2O_POST_READ32(c);
}
 
/*
 *	Turn a Linux block request into an I2O block read/write.
 */

static int i2ob_send(u32 m, struct i2ob_device *dev, struct i2ob_request *ireq, u32 base, int unit)
{
	struct i2o_controller *c = dev->controller;
	int tid = dev->tid;
	u32 *msg;
	u32 *mptr;
	u64 offset;
	struct request *req = ireq->req;
	struct buffer_head *bh = req->bh;
	static int old_qd = 2;
	int count = req->nr_sectors<<9;
	
	/*
	 *	Build a message
	 */
	
	msg = bus_to_virt(c->mem_offset + m);
	
	msg[2] = i2ob_context|(unit<<8);
	msg[3] = ireq->num;
	msg[5] = req->nr_sectors << 9;
	
	/* This can be optimised later - just want to be sure its right for
	   starters */
	offset = ((u64)(req->sector+base)) << 9;
	msg[6] = offset & 0xFFFFFFFF;
	msg[7] = (offset>>32);
	mptr=msg+8;
	
	if(req->cmd == READ)
	{
		msg[1] = I2O_CMD_BLOCK_READ<<24|HOST_TID<<12|tid;
		/* We don't yet do cache/readahead and other magic */
		msg[4] = 1<<16;	
		while(bh!=NULL)
		{
			/*
			 *	Its best to do this in one not or it in
			 *	later. mptr is in PCI space so fast to write
			 *	sucky to read.
			 */
			if(bh->b_reqnext)
				*mptr++ = 0x10000000|(bh->b_size);
			else
				*mptr++ = 0xD0000000|(bh->b_size);
	
			*mptr++ = virt_to_bus(bh->b_data);
			count -= bh->b_size;
			bh = bh->b_reqnext;
		}
	}
	else if(req->cmd == WRITE)
	{
		msg[1] = I2O_CMD_BLOCK_WRITE<<24|HOST_TID<<12|tid;
		msg[4] = 1<<16;
		while(bh!=NULL)
		{
			if(bh->b_reqnext)
				*mptr++ = 0x14000000|(bh->b_size);
			else
				*mptr++ = 0xD4000000|(bh->b_size);
			count -= bh->b_size;
			*mptr++ = virt_to_bus(bh->b_data);
			bh = bh->b_reqnext;
		}
	}
	msg[0] = I2O_MESSAGE_SIZE(mptr-msg) | SGL_OFFSET_8;
	
	if(req->current_nr_sectors > 8)
		printk("Gathered sectors %ld.\n", 
			req->current_nr_sectors);
			
	if(count != 0)
	{
		printk("Request count botched by %d.\n", count);
		msg[5] -= count;
	}

	i2o_post_message(c,m);
	atomic_inc(&queue_depth);
	if(atomic_read(&queue_depth)>old_qd)
		old_qd=atomic_read(&queue_depth);
	return 0;
}

/*
 *	Remove a request from the _locked_ request list. We update both the
 *	list chain and if this is the last item the tail pointer. Caller
 *	must hold the lock.
 */
 
static inline void i2ob_unhook_request(struct i2ob_request *ireq)
{
	ireq->next = i2ob_qhead;
	i2ob_qhead = ireq;
}

/*
 *	Request completion handler
 */
 
static void i2ob_end_request(struct request *req)
{
	/*
	 * Loop until all of the buffers that are linked
	 * to this request have been marked updated and
	 * unlocked.
	 */

//	printk("ending request %p: ", req);
	while (end_that_request_first( req, !req->errors, "i2o block" ))
	{
//		printk(" +\n");
	}

	/*
	 * It is now ok to complete the request.
	 */
	
//	printk("finishing ");
	end_that_request_last( req );
//	printk("done\n");
}


/*
 *	OSM reply handler. This gets all the message replies
 */

static void i2o_block_reply(struct i2o_handler *h, struct i2o_controller *c, struct i2o_message *msg)
{
	struct i2ob_request *ireq;
	u8 st;
	u32 *m = (u32 *)msg;
	u8 unit = (m[2]>>8)&0xF0;	/* low 4 bits are partition */
	
	if(m[0] & (1<<13))
	{
		printk("IOP fail.\n");
		printk("From %d To %d Cmd %d.\n",
			(m[1]>>12)&0xFFF,
			m[1]&0xFFF,
			m[1]>>24);
		printk("Failure Code %d.\n", m[4]>>24);
		if(m[4]&(1<<16))
			printk("Format error.\n");
		if(m[4]&(1<<17))
			printk("Path error.\n");
		if(m[4]&(1<<18))
			printk("Path State.\n");
		if(m[4]&(1<<18))
			printk("Congestion.\n");
		
		m=(u32 *)bus_to_virt(m[7]);
		printk("Failing message is %p.\n", m);
		
		/* We need to up the request failure count here and maybe
		   abort it */
		ireq=&i2ob_queue[m[3]];
		/* Now flush the message by making it a NOP */
		m[0]&=0x00FFFFFF;
		m[0]|=(I2O_CMD_UTIL_NOP)<<24;
		i2o_post_message(c,virt_to_bus(m));
		
	}
	else
	{
		if(m[2]&0x80000000)
		{
			int * ptr = (int *)m[3];
			if(m[4]>>24)
				*ptr = -1;
			else
				*ptr = 1;
			return;
		}
		/*
		 *	Lets see what is cooking. We stuffed the
		 *	request in the context.
		 */
		 
		ireq=&i2ob_queue[m[3]];
		st=m[4]>>24;
	
		if(st!=0)
		{
			printk(KERN_ERR "i2ob: error %08X\n", m[4]);
			/*
			 *	Now error out the request block
			 */
			ireq->req->errors++;	
		}
	}
	/*
	 *	Dequeue the request.
	 */
	
	spin_lock(&io_request_lock);
	spin_lock(&i2ob_lock);
	i2ob_unhook_request(ireq);
	i2ob_end_request(ireq->req);
	
	/*
	 *	We may be able to do more I/O
	 */
	 
	atomic_dec(&queue_depth);
	do_i2ob_request();
	spin_unlock(&i2ob_lock);
	spin_unlock(&io_request_lock);
}

static struct i2o_handler i2o_block_handler =
{
	i2o_block_reply,
	"I2O Block OSM",
	0,
	I2O_CLASS_RANDOM_BLOCK_STORAGE
};


/*
 *	The I2O block driver is listed as one of those that pulls the
 *	front entry off the queue before processing it. This is important
 *	to remember here. If we drop the io lock then CURRENT will change
 *	on us. We must unlink CURRENT in this routine before we return, if
 *	we use it.
 */

static void do_i2ob_request(void)
{
	struct request *req;
	struct i2ob_request *ireq;
	int unit;
	struct i2ob_device *dev;
	u32 m;

	while (CURRENT) {
		/*
		 *	On an IRQ completion if there is an inactive
		 *	request on the queue head it means it isnt yet
		 *	ready to dispatch.
		 */
		if(CURRENT->rq_status == RQ_INACTIVE)
			return;
			
		/*
		 *	Queue depths probably belong with some kind of
		 *	generic IOP commit control. Certainly its not right
		 *	its global!
		 */
		if(atomic_read(&queue_depth)>=MAX_I2OB_DEPTH)
			break;

		req = CURRENT;
		unit = MINOR(req->rq_dev);
		dev = &i2ob_dev[(unit&0xF0)];
		/* Get a message */
		m = i2ob_get(dev);
		/* No messages -> punt 
		   FIXME: if we have no messages, and there are no messages 
		   we deadlock now. Need a timer/callback ?? */
		if(m==0xFFFFFFFF)
		{
			printk("i2ob: no messages!\n");
			break;
		}
		req->errors = 0;
		CURRENT = CURRENT->next;
		req->next = NULL;
		req->sem = NULL;
		
		ireq = i2ob_qhead;
		i2ob_qhead = ireq->next;
		ireq->req = req;

		i2ob_send(m, dev, ireq, i2ob[unit].start_sect, (unit&0xF0));
	}
}

static void i2ob_request(void)
{
	unsigned long flags;
	spin_lock_irqsave(&i2ob_lock, flags);
	do_i2ob_request();
	spin_unlock_irqrestore(&i2ob_lock, flags);
}	

/*
 *	SCSI-CAM for ioctl geometry mapping
 *	Duplicated with SCSI - this should be moved into somewhere common
 *	perhaps genhd ?
 */
 
static void i2o_block_biosparam(
	unsigned long capacity,
	unsigned short *cyls,
	unsigned char *hds,
	unsigned char *secs) 
{ 
	unsigned long heads, sectors, cylinders, temp; 

	cylinders = 1024L;			/* Set number of cylinders to max */ 
	sectors = 62L;      			/* Maximize sectors per track */ 

	temp = cylinders * sectors;		/* Compute divisor for heads */ 
	heads = capacity / temp;		/* Compute value for number of heads */
	if (capacity % temp) {			/* If no remainder, done! */ 
    		heads++;                	/* Else, increment number of heads */ 
    		temp = cylinders * heads;	/* Compute divisor for sectors */ 
    		sectors = capacity / temp;	/* Compute value for sectors per
						       track */ 
	    	if (capacity % temp) {		/* If no remainder, done! */ 
			sectors++;                  /* Else, increment number of sectors */ 
	      		temp = heads * sectors;	/* Compute divisor for cylinders */
	      		cylinders = capacity / temp;/* Compute number of cylinders */ 
		} 
	} 
	/* if something went wrong, then apparently we have to return
	   a geometry with more than 1024 cylinders */
	if (cylinders == 0 || heads > 255 || sectors > 63 || cylinders >1023) 
	{
		unsigned long temp_cyl;
		
		heads = 64;
		sectors = 32;
		temp_cyl = capacity / (heads * sectors);
		if (temp_cyl > 1024) 
		{
			heads = 255;
			sectors = 63;
		}
		cylinders = capacity / (heads * sectors);
	}
	*cyls = (unsigned int) cylinders;	/* Stuff return values */ 
	*secs = (unsigned int) sectors; 
	*hds  = (unsigned int) heads; 
} 

/*
 *	Rescan the partition tables
 */
 
static int do_i2ob_revalidate(kdev_t dev, int maxu)
{
	int minor=MINOR(dev);
	int i;
	
	minor&=0xF0;
	
	i2ob_dev[minor].refcnt++;
	if(i2ob_dev[minor].refcnt>maxu+1)
	{
		i2ob_dev[minor].refcnt--;
		return -EBUSY;
	}
	
	for( i = 15; i>=0 ; i--)
	{
		int m = minor+i;
		kdev_t d = MKDEV(MAJOR_NR, m);
		struct super_block *sb = get_super(d);
		
		sync_dev(d);
		if(sb)
			invalidate_inodes(sb);
		invalidate_buffers(d);
		i2ob_gendisk.part[m].start_sect = 0;
		i2ob_gendisk.part[m].nr_sects = 0;
	}

	/*
	 *	Do a physical check and then reconfigure
	 */
	 
	i2ob_install_device(i2ob_dev[minor].controller, i2ob_dev[minor].i2odev,
		minor);
	i2ob_dev[minor].refcnt--;
	return 0;
}

/*
 *	Issue device specific ioctl calls.
 */

static int i2ob_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct i2ob_device *dev;
	int minor;

	/* Anyone capable of this syscall can do *real bad* things */

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!inode)
		return -EINVAL;
	minor = MINOR(inode->i_rdev);
	if (minor >= (MAX_I2OB<<4))
		return -ENODEV;

	dev = &i2ob_dev[minor];
	switch (cmd) {
		case BLKRASET:
			if(!capable(CAP_SYS_ADMIN))  return -EACCES;
			if(arg > 0xff) return -EINVAL;
			read_ahead[MAJOR(inode->i_rdev)] = arg;
			return 0;

		case BLKRAGET:
			if (!arg)  return -EINVAL;
			return put_user(read_ahead[MAJOR(inode->i_rdev)],
					(long *) arg); 
		case BLKGETSIZE:
			return put_user(i2ob[minor].nr_sects, (long *) arg);

		case BLKFLSBUF:
			if(!capable(CAP_SYS_ADMIN))
				return -EACCES;

			fsync_dev(inode->i_rdev);
			invalidate_buffers(inode->i_rdev);
			return 0;
			
		case HDIO_GETGEO:
		{
			struct hd_geometry g;
			int u=minor&0xF0;
			i2o_block_biosparam(i2ob_sizes[u]<<1, 
				&g.cylinders, &g.heads, &g.sectors);
			g.start = i2ob[minor].start_sect;
			return copy_to_user((void *)arg,&g, sizeof(g))?-EFAULT:0;
		}
	
		case BLKRRPART:
			if(!capable(CAP_SYS_ADMIN))
				return -EACCES;
			return do_i2ob_revalidate(inode->i_rdev,1);
			
		default:
			return blk_ioctl(inode->i_rdev, cmd, arg);
	}
}

/*
 *	Close the block device down
 */
 
static int i2ob_release(struct inode *inode, struct file *file)
{
	struct i2ob_device *dev;
	int minor;

	minor = MINOR(inode->i_rdev);
	if (minor >= (MAX_I2OB<<4))
		return -ENODEV;
	sync_dev(inode->i_rdev);
	dev = &i2ob_dev[(minor&0xF0)];
	if (dev->refcnt <= 0)
		printk(KERN_ALERT "i2ob_release: refcount(%d) <= 0\n", dev->refcnt);
	dev->refcnt--;
	if(dev->refcnt==0)
	{
		/*
		 *	Flush the onboard cache on unmount
		 */
		u32 msg[5];
		int *query_done = &dev->done_flag;
		msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
		msg[1] = I2O_CMD_BLOCK_CFLUSH<<24|HOST_TID<<12|dev->tid;
		msg[2] = i2ob_context|0x80000000;
		msg[3] = (u32)query_done;
		msg[4] = 60<<16;
		i2o_post_wait(dev->controller, dev->tid, msg, 20, query_done,2);
		/*
		 *	Unlock the media
		 */
		msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
		msg[1] = I2O_CMD_BLOCK_MUNLOCK<<24|HOST_TID<<12|dev->tid;
		msg[2] = i2ob_context|0x80000000;
		msg[3] = (u32)query_done;
		msg[4] = -1;
		i2o_post_wait(dev->controller, dev->tid, msg, 20, query_done,2);
	
		/*
 		 * Now unclaim the device.
		 */
		if (i2o_release_device(dev->i2odev, &i2o_block_handler, I2O_CLAIM_PRIMARY)<0)
			printk(KERN_ERR "i2ob_release: controller rejected unclaim.\n");

	}
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	Open the block device.
 */
 
static int i2ob_open(struct inode *inode, struct file *file)
{
	int minor;
	struct i2ob_device *dev;
	
	if (!inode)
		return -EINVAL;
	minor = MINOR(inode->i_rdev);
	if (minor >= MAX_I2OB<<4)
		return -ENODEV;
	dev=&i2ob_dev[(minor&0xF0)];

	if(dev->refcnt++==0)
	{ 
		u32 msg[6];
		int *query_done;
		
		
		if(i2o_claim_device(dev->i2odev, &i2o_block_handler, I2O_CLAIM_PRIMARY)<0)
		{
			dev->refcnt--;
			return -EBUSY;
		}
		
		query_done = &dev->done_flag;
		/*
		 *	Mount the media if needed. Note that we don't use
		 *	the lock bit. Since we have to issue a lock if it
		 *	refuses a mount (quite possible) then we might as
		 *	well just send two messages out.
		 */
		msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;		
		msg[1] = I2O_CMD_BLOCK_MMOUNT<<24|HOST_TID<<12|dev->tid;
		msg[2] = i2ob_context|0x80000000;
		msg[3] = (u32)query_done;
		msg[4] = -1;
		msg[5] = 0;
		i2o_post_wait(dev->controller, dev->tid, msg, 24, query_done,2);
		/*
		 *	Lock the media
		 */
		msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
		msg[1] = I2O_CMD_BLOCK_MLOCK<<24|HOST_TID<<12|dev->tid;
		msg[2] = i2ob_context|0x80000000;
		msg[3] = (u32)query_done;
		msg[4] = -1;
		i2o_post_wait(dev->controller, dev->tid, msg, 20, query_done,2);
	}		
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 *	Issue a device query
 */
 
static int i2ob_query_device(struct i2ob_device *dev, int table, 
	int field, void *buf, int buflen)
{
	return i2o_query_scalar(dev->controller, dev->tid, i2ob_context,
		table, field, buf, buflen, &dev->done_flag);
}


/*
 *	Install the I2O block device we found.
 */
 
static int i2ob_install_device(struct i2o_controller *c, struct i2o_device *d, int unit)
{
	u64 size;
	u32 blocksize;
	u32 limit;
	u8 type;
	u32 flags, status;
	struct i2ob_device *dev=&i2ob_dev[unit];
	int i;

	/*
	 *	Ask for the current media data. If that isn't supported
	 *	then we ask for the device capacity data
	 */
	 
	if(i2ob_query_device(dev, 0x0004, 1, &blocksize, 4) != 0
	  || i2ob_query_device(dev, 0x0004, 0, &size, 8) !=0 )
	{
		i2ob_query_device(dev, 0x0000, 3, &blocksize, 4);
		i2ob_query_device(dev, 0x0000, 4, &size, 8);
	}
	
	i2ob_query_device(dev, 0x0000, 5, &flags, 4);
	i2ob_query_device(dev, 0x0000, 6, &status, 4);
	i2ob_sizes[unit] = (int)(size>>10);
	i2ob_hardsizes[unit] = blocksize;
	i2ob_gendisk.part[unit].nr_sects = i2ob_sizes[unit];

	limit=4096;	/* 8 deep scatter gather */

	printk("Byte limit is %d.\n", limit);
	
	for(i=unit;i<=unit+15;i++)
		i2ob_max_sectors[i]=(limit>>9);
	
	i2ob[unit].nr_sects = (int)(size>>9);

	i2ob_query_device(dev, 0x0000, 0, &type, 1);
	
	sprintf(d->dev_name, "%s%c", i2ob_gendisk.major_name, 'a' + (unit>>4));

	printk("%s: ", d->dev_name);
	if(status&(1<<10))
		printk("RAID ");
	switch(type)
	{
		case 0: printk("Disk Storage");break;
		case 4: printk("WORM");break;
		case 5: printk("CD-ROM");break;
		case 7:	printk("Optical device");break;
		default:
			printk("Type %d", type);
	}
	if(((flags & (1<<3)) && !(status & (1<<3))) ||
	   ((flags & (1<<4)) && !(status & (1<<4))))
	{
		printk(" Not loaded.\n");
		return 0;
	}
	printk(" %dMb, %d byte sectors",
		(int)(size>>20), blocksize);
	if(status&(1<<0))
	{
		u32 cachesize;
		i2ob_query_device(dev, 0x0003, 0, &cachesize, 4);
		cachesize>>=10;
		if(cachesize>4095)
			printk(", %dMb cache", cachesize>>10);
		else
			printk(", %dKb cache", cachesize);
	}
	printk(".\n");
	printk("%s: Maximum sectors/read set to %d.\n", 
		d->dev_name, i2ob_max_sectors[unit]);
	resetup_one_dev(&i2ob_gendisk, unit>>4);
	return 0;
}

static void i2ob_probe(void)
{
	int i;
	int unit = 0;
	int warned = 0;
		
	for(i=0; i< MAX_I2O_CONTROLLERS; i++)
	{
		struct i2o_controller *c=i2o_find_controller(i);
		struct i2o_device *d;
	
		if(c==NULL)
			continue;

		for(d=c->devices;d!=NULL;d=d->next)
		{
			if(d->class!=I2O_CLASS_RANDOM_BLOCK_STORAGE)
				continue;

			if(unit<MAX_I2OB<<4)
			{
 				/*
				 * Get the device and fill in the
				 * Tid and controller.
				 */
				struct i2ob_device *dev=&i2ob_dev[unit];
				dev->i2odev = d; 
				dev->controller = c;
				dev->tid = d->id;
 
				/*
				 * Insure the device can be claimed
				 * before installing it.
				 */
				if(i2o_claim_device(dev->i2odev, &i2o_block_handler, I2O_CLAIM_PRIMARY )==0)
				{
					printk(KERN_INFO "Claimed Dev %p Tid %d Unit %d\n",dev,dev->tid,unit);
					i2ob_install_device(c,d,unit);
                                        unit+=16;
 
					/*
					 * Now that the device has been
					 * installed, unclaim it so that
					 * it can be claimed by either
					 * the block or scsi driver.
					 */
					if(i2o_release_device(dev->i2odev, &i2o_block_handler, I2O_CLAIM_PRIMARY))
						printk(KERN_INFO "Could not unclaim Dev %p Tid %d\n",dev,dev->tid);
				}
				else
					printk(KERN_INFO "TID %d not claimed\n",dev->tid);
			}
			else
			{
				if(!warned++)
					printk("i2o_block: too many controllers, registering only %d.\n", unit>>4);
			}
		}
	}
	i2ob_devices = unit;
}

/*
 *	Have we seen a media change ?
 */
 
static int i2ob_media_change(kdev_t dev)
{
	int i=MINOR(dev);
	i>>=4;
	if(i2ob_media_change_flag[i])
	{
		i2ob_media_change_flag[i]=0;
		return 1;
	}
	return 0;
}

static int i2ob_revalidate(kdev_t dev)
{
	return do_i2ob_revalidate(dev, 0);
}

static int i2ob_reboot_event(struct notifier_block *n, unsigned long code, void *p)
{
	int i;
	
	if(code != SYS_RESTART && code != SYS_HALT && code != SYS_POWER_OFF)
		return NOTIFY_DONE;
	for(i=0;i<MAX_I2OB;i++)
	{
		struct i2ob_device *dev=&i2ob_dev[(i<<4)];
		
		if(dev->refcnt!=0)
		{
			/*
			 *	Flush the onboard cache on power down
			 *	also unlock the media
			 */
			u32 msg[5];
			int *query_done = &dev->done_flag;
			msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
			msg[1] = I2O_CMD_BLOCK_CFLUSH<<24|HOST_TID<<12|dev->tid;
			msg[2] = i2ob_context|0x80000000;
			msg[3] = (u32)query_done;
			msg[4] = 60<<16;
			i2o_post_wait(dev->controller, dev->tid, msg, 20, query_done,2);
			/*
			 *	Unlock the media
			 */
			msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
			msg[1] = I2O_CMD_BLOCK_MUNLOCK<<24|HOST_TID<<12|dev->tid;
			msg[2] = i2ob_context|0x80000000;
			msg[3] = (u32)query_done;
			msg[4] = -1;
			i2o_post_wait(dev->controller, dev->tid, msg, 20, query_done,2);
		}
	}	
	return NOTIFY_DONE;
}

struct notifier_block i2ob_reboot_notifier =
{
	i2ob_reboot_event,
	NULL,
	0
};

static struct file_operations i2ob_fops =
{
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	i2ob_ioctl,		/* ioctl */
	NULL,			/* mmap */
	i2ob_open,		/* open */
	NULL,			/* flush */
	i2ob_release,		/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	i2ob_media_change,	/* Media Change */
	i2ob_revalidate,	/* Revalidate */
	NULL			/* File locks */
};

/*
 *	Partitioning
 */
 
static void i2ob_geninit(struct gendisk *gd)
{
}
	
static struct gendisk i2ob_gendisk = 
{
	MAJOR_NR,
	"i2ohd",
	4,
	1<<4,
	MAX_I2OB,
	i2ob_geninit,
	i2ob,
	i2ob_sizes,
	0,
	NULL,
	NULL
};

/*
 * And here should be modules and kernel interface 
 *  (Just smiley confuses emacs :-)
 */

#ifdef MODULE
#define i2o_block_init init_module
#endif

int i2o_block_init(void)
{
	int i;

	printk(KERN_INFO "I2O block device OSM v0.07. (C) 1999 Red Hat Software.\n");
	
	/*
	 *	Register the block device interfaces
	 */

	if (register_blkdev(MAJOR_NR, "i2o_block", &i2ob_fops)) {
		printk("Unable to get major number %d for i2o_block\n",
		       MAJOR_NR);
		return -EIO;
	}
#ifdef MODULE
	printk("i2o_block: registered device at major %d\n", MAJOR_NR);
#endif

	/*
	 *	Now fill in the boiler plate
	 */
	 
	blksize_size[MAJOR_NR] = i2ob_blksizes;
	hardsect_size[MAJOR_NR] = i2ob_hardsizes;
	blk_size[MAJOR_NR] = i2ob_sizes;
	max_sectors[MAJOR_NR] = i2ob_max_sectors;
	
	blk_dev[MAJOR_NR].request_fn = i2ob_request;
	for (i = 0; i < MAX_I2OB << 4; i++) {
		i2ob_dev[i].refcnt = 0;
		i2ob_dev[i].flags = 0;
		i2ob_dev[i].controller = NULL;
		i2ob_dev[i].i2odev = NULL;
		i2ob_dev[i].tid = 0;
		i2ob_dev[i].head = NULL;
		i2ob_dev[i].tail = NULL;
		i2ob_blksizes[i] = 1024;
		i2ob_max_sectors[i] = 2;
	}
	
	/*
	 *	Set up the queue
	 */
	
	for(i = 0; i< MAX_I2OB_DEPTH; i++)
	{
		i2ob_queue[i].next = &i2ob_queue[i+1];
		i2ob_queue[i].num = i;
	}
	
	/* Queue is MAX_I2OB + 1... */
	i2ob_queue[i].next = NULL;
	i2ob_qhead = &i2ob_queue[0];
	
	/*
	 *	Register the OSM handler as we will need this to probe for
	 *	drives, geometry and other goodies.
	 */

	if(i2o_install_handler(&i2o_block_handler)<0)
	{
		unregister_blkdev(MAJOR_NR, "i2o_block");
		printk(KERN_ERR "i2o_block: unable to register OSM.\n");
		return -EINVAL;
	}
	i2ob_context = i2o_block_handler.context;	 

	/*
	 *	Finally see what is actually plugged in to our controllers
	 */

	i2ob_probe();
	
	register_reboot_notifier(&i2ob_reboot_notifier);
	return 0;
}

#ifdef MODULE

EXPORT_NO_SYMBOLS;
MODULE_AUTHOR("Red Hat Software");
MODULE_DESCRIPTION("I2O Block Device OSM");

void cleanup_module(void)
{
	struct gendisk **gdp;
	
	unregister_reboot_notifier(&i2ob_reboot_notifier);
	
	/*
	 *	Flush the OSM
	 */

	i2o_remove_handler(&i2o_block_handler);
		 
	/*
	 *	Return the block device
	 */
	if (unregister_blkdev(MAJOR_NR, "i2o_block") != 0)
		printk("i2o_block: cleanup_module failed\n");

	/*
	 *	Why isnt register/unregister gendisk in the kernel ???
	 */

	for (gdp = &gendisk_head; *gdp; gdp = &((*gdp)->next))
		if (*gdp == &i2ob_gendisk)
			break;

}
#endif
