/*
 *  linux/fs/proc/kmsg.c
 *
 *  Copyright (C) 1992  by Linus Torvalds
 *
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/poll.h>

#include <asm/uaccess.h>
#include <asm/io.h>

extern unsigned long log_size;
extern struct wait_queue * log_wait;

asmlinkage int sys_syslog(int type, char * bug, int count);

static int kmsg_open(struct inode * inode, struct file * file)
{
	return sys_syslog(1,NULL,0);
}

static void kmsg_release(struct inode * inode, struct file * file)
{
	(void) sys_syslog(0,NULL,0);
}

static long kmsg_read(struct inode * inode, struct file * file,
	char * buf, unsigned long count)
{
	return sys_syslog(2,buf,count);
}

static unsigned int kmsg_poll(struct file *file, poll_table * wait)
{
	poll_wait(&log_wait, wait);
	if (log_size)
		return POLLIN | POLLRDNORM;
	return 0;
}


static struct file_operations proc_kmsg_operations = {
	NULL,		/* kmsg_lseek */
	kmsg_read,
	NULL,		/* kmsg_write */
	NULL,		/* kmsg_readdir */
	kmsg_poll,	/* kmsg_poll */
	NULL,		/* kmsg_ioctl */
	NULL,		/* mmap */
	kmsg_open,
	kmsg_release,
	NULL		/* can't fsync */
};

struct inode_operations proc_kmsg_inode_operations = {
	&proc_kmsg_operations,	/* default base directory file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};
