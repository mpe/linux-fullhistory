/*
 * NETLINK	An implementation of a loadable kernel mode driver providing
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
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#include <net/netlink.h>

#include <asm/poll.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

static int (*netlink_handler[MAX_LINKS])(int minor, struct sk_buff *skb);
static struct sk_buff_head skb_queue_rd[MAX_LINKS]; 
static int rdq_size[MAX_LINKS];
static struct wait_queue *read_space_wait[MAX_LINKS];

static unsigned long active_map = 0;
static unsigned long open_map = 0;

/*
 *	Device operations
 */
 
/*
 *	Default write handler.
 */
 
static int netlink_err(int minor, struct sk_buff *skb)
{
	kfree_skb(skb, FREE_READ);
	return -EUNATCH;
}

/*
 *	Exported do nothing receiver for one way
 *	interfaces.
 */
  
int netlink_donothing(int minor, struct sk_buff *skb)
{
	kfree_skb(skb, FREE_READ);
	return -EINVAL;
}

static unsigned int netlink_poll(struct file *file, poll_table * wait)
{
	unsigned int mask;
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);

	poll_wait(&read_space_wait[minor], wait);
	mask = POLLOUT | POLLWRNORM;
	if (skb_peek(&skb_queue_rd[minor]))
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

/*
 *	Write a message to the kernel side of a communication link
 */
 
static ssize_t netlink_write(struct file * file, const char * buf,
			size_t count,loff_t *ppos)
{
	int err; 
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	struct sk_buff *skb;
	skb=alloc_skb(count, GFP_KERNEL);
	err = copy_from_user(skb_put(skb,count),buf, count);
	return err ? -EFAULT : (netlink_handler[minor])(minor,skb);
}

/*
 *	Read a message from the kernel side of the communication link
 */

static ssize_t  netlink_read(struct file * file, char * buf,
			 size_t count,loff_t *ppos)
{
	int err; 
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
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
		if(signal_pending(current))
		{
			sti();
			return -ERESTARTSYS;
		}
	}
	rdq_size[minor]-=skb->len;
	sti();
	if(skb->len<count)
		count=skb->len;
	err = copy_to_user(buf,skb->data,count);
	kfree_skb(skb, FREE_READ);
	return err ? -EFAULT : count;
}

static long long netlink_lseek(struct file * file, long long offset, int origin)
{
	return -ESPIPE;
}

static int netlink_open(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	
	if(minor>=MAX_LINKS)
		return -ENODEV;
	if(active_map&(1<<minor))
	{
		if (file->f_mode & FMODE_READ)
		{
			if (open_map&(1<<minor))
				return -EBUSY;
			open_map|=(1<<minor);
		}
		MOD_INC_USE_COUNT;
		return 0;
	}
	return -EUNATCH;
}

static int netlink_release(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	if (file->f_mode & FMODE_READ)
		open_map&=~(1<<minor);
	MOD_DEC_USE_COUNT;
	return 0;
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
	netlink_poll,
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
 
int netlink_attach(int unit, int (*function)(int minor, struct sk_buff *skb))
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


/*
 *	"High" level netlink interface. (ANK)
 *	
 *	Features:
 *		- standard message format.
 *		- pseudo-reliable delivery. Messages can be still lost, but
 *		  user level will know that they were lost and can
 *		  recover (f.e. gated could reread FIB and device list)
 *		- messages are batched.
 *		- if user is not attached, we do not make useless work.
 *
 *	Examples:
 *		- netlink_post equivalent (but with pseudo-reliable delivery)
 *			ctl.nlmsg_delay = 0;
 *			ctl.nlmsg_maxsize = <one message size>;
 *			....
 *			msg = nlmsg_send(&ctl, ...);
 *			if (msg) {
 *				... make it ...
 *				nlmsg_transmit(&ctl);
 *			}
 *
 *		- batched messages.
 *		  	if nlmsg_delay==0, messages are delivered only
 *			by nlmsg_transmit, or when batch is completed,
 *			otherwise nlmsg_transmit is noop (only starts
 *			timer)
 *
 *			ctl.nlmsg_delay = ...;
 *			ctl.nlmsg_maxsize = <one batch size>;
 *			....
 *			msg = nlmsg_send(&ctl, ...);
 *			if (msg)
 *				... make it ...
 *			....
 *			msg = nlmsg_send(&ctl, ...);
 *			if (msg)
 *				... make it ...
 *			....
 *			if (ctl.nlmsg_skb)
 *				nlmsg_transmit(&ctl);
 *
 */

/*
 *	Try to deliver queued messages.
 *	If the delivery fails (netlink is not attached or congested),
 *	do not free skb to avoid useless new message creation.
 *
 *	Notes:
 *		- timer should be already stopped.
 *		- NET SPL.
 */

void nlmsg_flush(struct nlmsg_ctl *ctl)
{
	if (ctl->nlmsg_skb == NULL)
		return;

	if (netlink_post(ctl->nlmsg_unit, ctl->nlmsg_skb) == 0)
	{
		ctl->nlmsg_skb = NULL;
		return;
	}

	ctl->nlmsg_timer.expires = jiffies + NLMSG_RECOVERY_TIMEO;
	ctl->nlmsg_timer.data = (unsigned long)ctl;
	ctl->nlmsg_timer.function = (void (*)(unsigned long))nlmsg_flush;
	add_timer(&ctl->nlmsg_timer);
	return;
}


/*
 *	Allocate room for new message. If it is impossible,
 *	start "overrun" mode and return NULL.
 *
 *	Notes:
 *		- NET SPL.
 */

void* nlmsg_send(struct nlmsg_ctl *ctl, unsigned long type, int len,
		 unsigned long seq, unsigned long pid)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int	rlen;

	static __inline__ void nlmsg_lost(struct nlmsg_ctl *ctl,
					  unsigned long seq)
	{
		if (!ctl->nlmsg_overrun)
		{
			ctl->nlmsg_overrun_start = seq;
			ctl->nlmsg_overrun_end = seq;
			ctl->nlmsg_overrun = 1;
			return;
		}
		if (!ctl->nlmsg_overrun_start)
			ctl->nlmsg_overrun_start = seq;
		if (seq)
			ctl->nlmsg_overrun_end = seq;
	}

	if (!(open_map&(1<<ctl->nlmsg_unit)))
	{
		nlmsg_lost(ctl, seq);
		return NULL;
	}

	rlen = NLMSG_ALIGN(len + sizeof(struct nlmsghdr));

	if (rlen > ctl->nlmsg_maxsize)
	{
		printk(KERN_ERR "nlmsg_send: too big message\n");
		return NULL;
	}

	if ((skb=ctl->nlmsg_skb) == NULL || skb_tailroom(skb) < rlen)
	{
		if (skb)
		{
			ctl->nlmsg_force++;
			nlmsg_flush(ctl);
			ctl->nlmsg_force--;
		}

		if (ctl->nlmsg_skb ||
		    (skb=alloc_skb(ctl->nlmsg_maxsize, GFP_ATOMIC)) == NULL)
		{
			printk (KERN_WARNING "nlmsg at unit %d overrunned\n", ctl->nlmsg_unit);
			nlmsg_lost(ctl, seq);
			return NULL;
		}

		ctl->nlmsg_skb = skb;

		if (ctl->nlmsg_overrun)
		{
			int *seqp;
			nlh = (struct nlmsghdr*)skb_put(skb, sizeof(struct nlmsghdr) + 2*sizeof(unsigned long));
			nlh->nlmsg_type = NLMSG_OVERRUN;
			nlh->nlmsg_len = sizeof(struct nlmsghdr) + 2*sizeof(unsigned long);
			nlh->nlmsg_seq = 0;
			nlh->nlmsg_pid = 0;
			seqp = (int*)nlh->nlmsg_data;
			seqp[0] = ctl->nlmsg_overrun_start;
			seqp[1] = ctl->nlmsg_overrun_end;
			ctl->nlmsg_overrun = 0;
		}
		if (ctl->nlmsg_timer.function)
		{
			del_timer(&ctl->nlmsg_timer);
			ctl->nlmsg_timer.function = NULL;
		}
		if (ctl->nlmsg_delay)
		{
			ctl->nlmsg_timer.expires = jiffies + ctl->nlmsg_delay;
			ctl->nlmsg_timer.function = (void (*)(unsigned long))nlmsg_flush;
			ctl->nlmsg_timer.data = (unsigned long)ctl;
			add_timer(&ctl->nlmsg_timer);
		}
	}

	nlh = (struct nlmsghdr*)skb_put(skb, rlen);
	nlh->nlmsg_type = type;
	nlh->nlmsg_len = sizeof(struct nlmsghdr) + len;
	nlh->nlmsg_seq = seq;
	nlh->nlmsg_pid = pid;
	return nlh->nlmsg_data;
}

/*
 *	Kick message queue.
 *	Two modes:
 *		- synchronous (delay==0). Messages are delivered immediately.
 *		- delayed. Do not deliver, but start delivery timer.
 */

void nlmsg_transmit(struct nlmsg_ctl *ctl)
{
	start_bh_atomic();

	if (!ctl->nlmsg_delay)
	{
		if (ctl->nlmsg_timer.function)
		{
			del_timer(&ctl->nlmsg_timer);
			ctl->nlmsg_timer.function = NULL;
		}
		ctl->nlmsg_force++;
		nlmsg_flush(ctl);
		ctl->nlmsg_force--;
		end_bh_atomic();
		return;
	}
	if (!ctl->nlmsg_timer.function)
	{
		ctl->nlmsg_timer.expires = jiffies + ctl->nlmsg_delay;
		ctl->nlmsg_timer.function = (void (*)(unsigned long))nlmsg_flush;
		ctl->nlmsg_timer.data = (unsigned long)ctl;
		add_timer(&ctl->nlmsg_timer);
	}

	end_bh_atomic();
}


__initfunc(int init_netlink(void))
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
	printk(KERN_INFO "Network Kernel/User communications module 0.05\n");
	return init_netlink();
}

void cleanup_module(void)
{
	unregister_chrdev(NET_MAJOR,"netlink");
}

#endif
