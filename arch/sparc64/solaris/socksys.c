/* $Id: socksys.c,v 1.2 1997/09/08 11:29:38 jj Exp $
 * socksys.c: /dev/inet/ stuff for Solaris emulation.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1995, 1996 Mike Jagdis (jaggy@purplet.demon.co.uk)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/file.h>

#include <asm/uaccess.h>
#include <asm/termios.h>

#include "conv.h"

extern asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, 
	unsigned long arg);
	
static int af_inet_protocols[] = {
IPPROTO_ICMP, IPPROTO_ICMP, IPPROTO_IGMP, IPPROTO_IPIP, IPPROTO_TCP,
IPPROTO_EGP, IPPROTO_PUP, IPPROTO_UDP, IPPROTO_IDP, IPPROTO_RAW,
0, 0, 0, 0, 0, 0,
};

static struct file_operations socksys_file_ops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	NULL,		/* open */
	NULL,		/* release */
};

static int socksys_open(struct inode * inode, struct file * filp)
{
	int family, type, protocol, fd;
	struct dentry *dentry;
	int (*sys_socket)(int,int,int) =
		(int (*)(int,int,int))SUNOS(97);
	
	family = ((MINOR(inode->i_rdev) >> 4) & 0xf);
	switch (family) {
	case AF_UNIX:
		type = SOCK_STREAM;
		protocol = 0;
		break;
	case AF_INET:
		protocol = af_inet_protocols[MINOR(inode->i_rdev) & 0xf];
		switch (protocol) {
		case IPPROTO_TCP: type = SOCK_STREAM; break;
		case IPPROTO_UDP: type = SOCK_DGRAM; break;
		default: type = SOCK_RAW; break;
		}
		break;
	default:
		type = SOCK_RAW;
		protocol = 0;
		break;
	}
	fd = sys_socket(family, type, protocol);
	if (fd < 0) return fd;
	dentry = filp->f_dentry;
	filp->f_dentry = current->files->fd[fd]->f_dentry;
	filp->f_dentry->d_inode->i_rdev = inode->i_rdev;
	filp->f_dentry->d_inode->i_flock = inode->i_flock;
	filp->f_dentry->d_inode->u.socket_i.file = filp;
	filp->f_op = &socksys_file_ops;
	dput(dentry);
	FD_CLR(fd, &current->files->close_on_exec);
	FD_CLR(fd, &current->files->open_fds);
	put_filp(current->files->fd[fd]);
	current->files->fd[fd] = NULL;
	return 0;
}

static int socksys_release(struct inode * inode, struct file * filp)
{
	return 0;
}

static unsigned int socksys_poll(struct file * filp, poll_table * wait)
{
	return 0;
}
	
static struct file_operations socksys_fops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	socksys_open,	/* open */
	socksys_release,/* release */
};

__initfunc(int
init_socksys(void))
{
	int ret;
	int (*sys_socket)(int,int,int) =
		(int (*)(int,int,int))SUNOS(97);
	int (*sys_close)(unsigned int) = 
		(int (*)(unsigned int))SYS(close);
	
	ret = register_chrdev (30, "socksys", &socksys_fops);
	if (ret < 0) {
		printk ("Couldn't register socksys character device\n");
		return ret;
	}
	ret = sys_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ret < 0) {
		printk ("Couldn't create socket\n");
		return ret;
	}
	socksys_file_ops = *current->files->fd[ret]->f_op;
	sys_close(ret);
	socksys_file_ops.poll = socksys_poll;
	socksys_file_ops.release = socksys_release;
	return 0;
}

void
cleanup_socksys(void)
{
	if (unregister_chrdev (30, "socksys"))
		printk ("Couldn't unregister socksys character device\n");
}
