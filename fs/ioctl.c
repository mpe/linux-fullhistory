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
		{
			struct address_space *mapping = inode->i_mapping;
			int res;
			/* do we support this mess? */
			if (!mapping->a_ops->bmap)
				return -EINVAL;
			if (!capable(CAP_SYS_RAWIO))
				return -EPERM;
			if ((error = get_user(block, (int *) arg)) != 0)
				return error;

			res = mapping->a_ops->bmap(mapping, block);
			return put_user(res, (int *) arg);
		}
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


asmlinkage long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	unsigned int flag;
	int on, error = -EBADF;

	filp = fget(fd);
	if (!filp)
		goto out;
	error = 0;
	lock_kernel();
	switch (cmd) {
		case FIOCLEX:
			FD_SET(fd, current->files->close_on_exec);
			break;

		case FIONCLEX:
			FD_CLR(fd, current->files->close_on_exec);
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

		case FIOASYNC:
			if ((error = get_user(on, (int *)arg)) != 0)
				break;
			flag = on ? FASYNC : 0;

			/* Did FASYNC state change ? */
			if ((flag ^ filp->f_flags) & FASYNC) {
				if (filp->f_op && filp->f_op->fasync)
					filp->f_op->fasync(fd, filp, on); 
			}
			if (on)
				filp->f_flags |= FASYNC;
			else
				filp->f_flags &= ~FASYNC;
			break;

		default:
			error = -ENOTTY;
			if (S_ISREG(filp->f_dentry->d_inode->i_mode))
				error = file_ioctl(filp, cmd, arg);
			else if (filp->f_op && filp->f_op->ioctl)
				error = filp->f_op->ioctl(filp->f_dentry->d_inode, filp, cmd, arg);
	}
	fput(filp);
	unlock_kernel();

out:
	return error;
}
