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
 *	Now netlink devices are emulated on the top of netlink sockets
 *	by compatibility reasons. Remove this file after a period. --ANK
 *
 */

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/poll.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>

static unsigned open_map = 0;
static struct socket *netlink_user[MAX_LINKS];

/*
 *	Device operations
 */
 
static unsigned int netlink_poll(struct file *file, poll_table * wait)
{
	struct socket *sock = netlink_user[MINOR(file->f_dentry->d_inode->i_rdev)];

	if (sock->ops->poll==NULL)
		return 0;
	return sock->ops->poll(file, sock, wait);
}

/*
 *	Write a message to the kernel side of a communication link
 */
 
static ssize_t netlink_write(struct file * file, const char * buf,
			     size_t count, loff_t *pos)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct socket *sock = netlink_user[MINOR(inode->i_rdev)];
	struct msghdr msg;
	struct iovec iov;

	iov.iov_base = (void*)buf;
	iov.iov_len = count;
	msg.msg_name=NULL;
	msg.msg_namelen=0;
	msg.msg_controllen=0;
	msg.msg_flags=0;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;

	return sock_sendmsg(sock, &msg, count);
}

/*
 *	Read a message from the kernel side of the communication link
 */

static ssize_t netlink_read(struct file * file, char * buf,
			    size_t count, loff_t *pos)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct socket *sock = netlink_user[MINOR(inode->i_rdev)];
	struct msghdr msg;
	struct iovec iov;

	iov.iov_base = buf;
	iov.iov_len = count;
	msg.msg_name=NULL;
	msg.msg_namelen=0;
	msg.msg_controllen=0;
	msg.msg_flags=0;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	if (file->f_flags&O_NONBLOCK)
		msg.msg_flags=MSG_DONTWAIT;

	return sock_recvmsg(sock, &msg, count, msg.msg_flags);
}

static loff_t netlink_lseek(struct file * file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static int netlink_open(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct socket *sock;
	struct sockaddr_nl nladdr;
	int err;
	
	if (minor>=MAX_LINKS)
		return -ENODEV;
	if (open_map&(1<<minor))
		return -EBUSY;

	open_map |= (1<<minor);
	MOD_INC_USE_COUNT;
	
	err = -EINVAL;
	if (net_families[PF_NETLINK]==NULL)
  		goto out;

	err = -ENFILE;
	if (!(sock = sock_alloc())) 
		goto out;

	sock->type = SOCK_RAW;

	if ((err = net_families[PF_NETLINK]->create(sock, minor)) < 0) 
	{
		sock_release(sock);
		goto out;
	}

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	nladdr.nl_groups = ~0;
	if ((err = sock->ops->bind(sock, (struct sockaddr*)&nladdr, sizeof(nladdr))) < 0) {
		sock_release(sock);
		goto out;
	}

	netlink_user[minor] = sock;
	return 0;

out:
	open_map &= ~(1<<minor);
	MOD_DEC_USE_COUNT;
	return err;
}

static int netlink_release(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct socket *sock = netlink_user[minor];

	netlink_user[minor] = NULL;
	open_map &= ~(1<<minor);
	sock_release(sock);
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
	NULL,		/* flush */
	netlink_release
};

__initfunc(int init_netlink(void))
{
	if (register_chrdev(NETLINK_MAJOR,"netlink", &netlink_fops)) {
		printk(KERN_ERR "netlink: unable to get major %d\n", NETLINK_MAJOR);
		return -EIO;
	}
	return 0;
}

#ifdef MODULE

int init_module(void)
{
	printk(KERN_INFO "Network Kernel/User communications module 0.04\n");
	return init_netlink();
}

void cleanup_module(void)
{
	unregister_chrdev(NET_MAJOR,"netlink");
}

#endif
