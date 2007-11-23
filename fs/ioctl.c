/*
 *  linux/fs/ioctl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>

int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	int block;

	if (fd >= NR_OPEN || !(filp = current->filp[fd]))
		return -EBADF;
	if (S_ISREG(filp->f_inode->i_mode) && cmd == BMAP_IOCTL &&
	    filp->f_inode->i_op->bmap) {
		block = get_fs_long((long *) arg);
		block = filp->f_inode->i_op->bmap(filp->f_inode,block);
		put_fs_long(block,(long *) arg);
		return 0;
	}
	if (filp->f_op && filp->f_op->ioctl)
		return filp->f_op->ioctl(filp->f_inode, filp, cmd,arg);
	return -EINVAL;
}
