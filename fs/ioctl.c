/*
 *  linux/fs/ioctl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/file.h>

#include <asm/uaccess.h>

static int file_ioctl(struct file *filp,unsigned int cmd,unsigned long arg)
{
	int error;
	int block;
	struct inode * inode = filp->f_dentry->d_inode;

	switch (cmd) {
		case FIBMAP:
			if (inode->i_op == NULL)
				return -EBADF;
		    	if (inode->i_op->bmap == NULL)
				return -EINVAL;
			if ((error = get_user(block, (int *) arg)) != 0)
				return error;
			block = inode->i_op->bmap(inode,block);
			return put_user(block, (int *) arg);
		case FIGETBSZ:
			if (inode->i_sb == NULL)
				return -EBADF;
			return put_user(inode->i_sb->s_blocksize, (int *) arg);
		case FIONREAD:
			return put_user(inode->i_size - filp->f_pos, (int *) arg);
	}
	if (filp->f_op && filp->f_op->ioctl)
		return filp->f_op->ioctl(inode, filp, cmd, arg);
	return -ENOTTY;
}


asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	unsigned int flag;
	int on, error = -EBADF;

	lock_kernel();
	filp = fget(fd);
	if (!filp)
		goto out;
	error = 0;
	switch (cmd) {
		case FIOCLEX:
			FD_SET(fd, &current->files->close_on_exec);
			break;

		case FIONCLEX:
			FD_CLR(fd, &current->files->close_on_exec);
			break;

		case FIONBIO:
			if ((error = get_user(on, (int *)arg)) != 0)
				break;
			flag = O_NONBLOCK;
#ifdef __sparc__
			/* SunOS compatibility item. */
			if(O_NONBLOCK != O_NDELAY)
				flag |= O_NDELAY;
#endif
			if (on)
				filp->f_flags |= flag;
			else
				filp->f_flags &= ~flag;
			break;

		case FIOASYNC: /* O_SYNC is not yet implemented,
				  but it's here for completeness. */
			if ((error = get_user(on, (int *)arg)) != 0)
				break;
			if (on)
				filp->f_flags |= O_SYNC;
			else
				filp->f_flags &= ~O_SYNC;
			break;

		default:
			error = -ENOTTY;
			if (!filp->f_dentry || !filp->f_dentry->d_inode)
				error = -ENOENT;
			else if (S_ISREG(filp->f_dentry->d_inode->i_mode))
				error = file_ioctl(filp, cmd, arg);
			else if (filp->f_op && filp->f_op->ioctl)
				error = filp->f_op->ioctl(filp->f_dentry->d_inode, filp, cmd, arg);
	}
	fput(filp);

out:
	unlock_kernel();
	return error;
}
