/*
 *  linux/fs/ioctl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/termios.h>
#include <linux/fcntl.h> /* for f_flags values */

static int file_ioctl(struct file *filp,unsigned int cmd,unsigned long arg)
{
	int error;
	int block;

	switch (cmd) {
		case FIBMAP:
			if (filp->f_inode->i_op == NULL)
				return -EBADF;
		    	if (filp->f_inode->i_op->bmap == NULL)
				return -EINVAL;
			error = verify_area(VERIFY_WRITE,(void *) arg,4);
			if (error)
				return error;
			get_user(block, (int *) arg);
			block = filp->f_inode->i_op->bmap(filp->f_inode,block);
			put_user(block,(int *) arg);
			return 0;
		case FIGETBSZ:
			if (filp->f_inode->i_sb == NULL)
				return -EBADF;
			error = verify_area(VERIFY_WRITE,(void *) arg,4);
			if (error)
				return error;
			put_user(filp->f_inode->i_sb->s_blocksize, (int *) arg);
			return 0;
		case FIONREAD:
			error = verify_area(VERIFY_WRITE,(void *) arg,sizeof(int));
			if (error)
				return error;
			put_user(filp->f_inode->i_size - filp->f_pos, (int *) arg);
			return 0;
	}
	if (filp->f_op && filp->f_op->ioctl)
		return filp->f_op->ioctl(filp->f_inode, filp, cmd, arg);
	return -ENOTTY;
}


asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	int on;

	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		return -EBADF;
	switch (cmd) {
		case FIOCLEX:
			FD_SET(fd, &current->files->close_on_exec);
			return 0;

		case FIONCLEX:
			FD_CLR(fd, &current->files->close_on_exec);
			return 0;

		case FIONBIO:
			on = verify_area(VERIFY_READ, (unsigned int *)arg,
				sizeof(unsigned int));
			if(on)	
				return on;
			get_user(on, (unsigned int *) arg);
			if (on)
				filp->f_flags |= O_NONBLOCK;
			else
				filp->f_flags &= ~O_NONBLOCK;
			return 0;

		case FIOASYNC: /* O_SYNC is not yet implemented,
				  but it's here for completeness. */
			on = verify_area(VERIFY_READ, (unsigned int *)arg,
				sizeof(unsigned int));
			if(on)	
				return on;
			get_user(on, (unsigned int *) arg);
			if (on)
				filp->f_flags |= O_SYNC;
			else
				filp->f_flags &= ~O_SYNC;
			return 0;

		default:
			if (filp->f_inode && S_ISREG(filp->f_inode->i_mode))
				return file_ioctl(filp, cmd, arg);

			if (filp->f_op && filp->f_op->ioctl)
				return filp->f_op->ioctl(filp->f_inode, filp, cmd, arg);

			return -ENOTTY;
	}
}
