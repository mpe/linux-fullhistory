/*
 * SKIPLINK	An implementation of a loadable kernel mode driver providing
 *		multiple kernel/user space bidirectional communications links.
 *
 * 		Author: 	Alan Cox <alan@cymru.net>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 * 
 */

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/lp.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/skbuff.h>

#include <net/netlink.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

static int (*netlink_handler[MAX_LINKS])(struct sk_buff *skb);
static struct sk_buff_head skb_queue_rd[MAX_LINKS]; 
static int rdq_size[MAX_LINKS];
static struct wait_queue *read_space_wait[MAX_LINKS];

static int active_map = 0;
static int open_map = 0;

/*
 *	Device operations
 */
 
/*
 *	Default write handler.
 */
 
static int netlink_err(struct sk_buff *skb)
{
	kfree_skb(skb, FREE_READ);
	return -EUNATCH;
}

/*
 *	Exported do nothing receiver for one way
 *	interfaces.
 */
  
int netlink_donothing(struct sk_buff *skb)
{
	kfree_skb(skb, FREE_READ);
	return -EINVAL;
}

static int netlink_select(struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
	unsigned int minor = MINOR(inode->i_rdev);
	switch (sel_type) {
	case SEL_IN:
		if (skb_peek(&skb_queue_rd[minor])!=NULL)
			return 1;
		select_wait(&read_space_wait[minor], wait);
		break;
	case SEL_OUT:
		return 1;
	}
	return 0;
}

/*
 *	Write a message to the kernel side of a communication link
 */
 
static int netlink_write(struct inode * inode, struct file * file, const char * buf, int count)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct sk_buff *skb;
	skb=alloc_skb(count, GFP_KERNEL);
	skb->free=1;
	memcpy_fromfs(skb_put(skb,count),buf, count);
	return (netlink_handler[minor])(skb);
}

/*
 *	Read a message from the kernel side of the communication link
 */

static int netlink_read(struct inode * inode, struct file * file, char * buf, int count)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct sk_buff *skb;
	cli();
	while((skb=skb_dequeue(&skb_queue_rd[minor]))==NULL)
	{
		if(file->f_flags&O_NONBLOCK)
		{
			sti();
			return -EAGAIN;
		}
		interruptible_sleep_on(&read_space_wait[minor]);
		if(current->signal & ~current->blocked)
		{
			sti();
			return -ERESTARTSYS;
		}
	}
	rdq_size[minor]-=skb->len;
	sti();
	if(skb->len<count)
		count=skb->len;
	memcpy_tofs(buf,skb->data,count);
	kfree_skb(skb, FREE_READ);
	return count;
}

static int netlink_lseek(struct inode * inode, struct file * file,
		    off_t offset, int origin)
{
	return -ESPIPE;
}

static int netlink_open(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	
	if(minor>=MAX_LINKS)
		return -ENODEV;
	if(open_map&(1<<minor))
		return -EBUSY;
	if(active_map&(1<<minor))
	{
		open_map|=(1<<minor);
		MOD_INC_USE_COUNT;
		return 0;
	}
	return -EUNATCH;
}

static void netlink_release(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	open_map&=~(1<<minor);	
	MOD_DEC_USE_COUNT;
}


static int netlink_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int retval = 0;

	if (minor >= MAX_LINKS)
		return -ENODEV;
	switch ( cmd ) {
		default:
			retval = -EINVAL;
	}
	return retval;
}


static struct file_operations netlink_fops = {
	netlink_lseek,
	netlink_read,
	netlink_write,
	NULL,		/* netlink_readdir */
	netlink_select,
	netlink_ioctl,
	NULL,		/* netlink_mmap */
	netlink_open,
	netlink_release
};

/*
 *	We export these functions to other modules. They provide a 
 *	complete set of kernel non-blocking support for message
 *	queueing.
 */
 
int netlink_attach(int unit, int (*function)(struct sk_buff *skb))
{
	if(unit>=MAX_LINKS)
		return -ENODEV;
	if(active_map&(1<<unit))
		return -EBUSY;
	active_map|=(1<<unit);
	netlink_handler[unit]=function;
	return 0;
}

void netlink_detach(int unit)
{
	active_map&=~(1<<unit);
	netlink_handler[unit]=netlink_err;
}

int netlink_post(int unit, struct sk_buff *skb)
{
	unsigned long flags;
	int ret=-EUNATCH;
	if(open_map&(1<<unit))
	{
		save_flags(flags);
		cli();
		if(rdq_size[unit]+skb->len>MAX_QBYTES)
			ret=-EAGAIN;
		else
		{	
			skb_queue_tail(&skb_queue_rd[unit], skb);
			rdq_size[unit]+=skb->len;
			ret=0;
			wake_up_interruptible(&read_space_wait[unit]);
		}
		restore_flags(flags);
	}
	return ret;
}

int init_netlink(void)
{
	int ct;

	if(register_chrdev(NETLINK_MAJOR,"netlink", &netlink_fops)) {
		printk(KERN_ERR "netlink: unable to get major %d\n", NETLINK_MAJOR);
		return -EIO;
	}
	for(ct=0;ct<MAX_LINKS;ct++)
	{
		skb_queue_head_init(&skb_queue_rd[ct]);
		netlink_handler[ct]=netlink_err;
	}
	return 0;
}

#ifdef MODULE

int init_module(void)
{
	printk(KERN_INFO "Network Kernel/User communications module 0.03\n");
	return init_netlink();
}

void cleanup_module(void)
{
	unregister_chrdev(NET_MAJOR,"netlink");
}

#endif
