/*
 *  linux/fs/read_write.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/uio.h>

#include <asm/segment.h>

asmlinkage int sys_lseek(unsigned int fd, off_t offset, unsigned int origin)
{
	struct file * file;
	int tmp = -1;

	if (fd >= NR_OPEN || !(file=current->files->fd[fd]) || !(file->f_inode))
		return -EBADF;
	if (origin > 2)
		return -EINVAL;
	if (file->f_op && file->f_op->lseek)
		return file->f_op->lseek(file->f_inode,file,offset,origin);

/* this is the default handler if no lseek handler is present */
	switch (origin) {
		case 0:
			tmp = offset;
			break;
		case 1:
			tmp = file->f_pos + offset;
			break;
		case 2:
			if (!file->f_inode)
				return -EINVAL;
			tmp = file->f_inode->i_size + offset;
			break;
	}
	if (tmp < 0)
		return -EINVAL;
	if (tmp != file->f_pos) {
		file->f_pos = tmp;
		file->f_reada = 0;
		file->f_version = ++event;
	}
	return file->f_pos;
}

asmlinkage int sys_llseek(unsigned int fd, unsigned long offset_high,
			  unsigned long offset_low, loff_t * result,
			  unsigned int origin)
{
	struct file * file;
	loff_t tmp = -1;
	loff_t offset;
	int err;

	if (fd >= NR_OPEN || !(file=current->files->fd[fd]) || !(file->f_inode))
		return -EBADF;
	if (origin > 2)
		return -EINVAL;
	if ((err = verify_area(VERIFY_WRITE, result, sizeof(loff_t))))
		return err;
	offset = (loff_t) (((unsigned long long) offset_high << 32) | offset_low);

	/* if there is a fs-specific handler, we can't just ignore it.. */
	/* accept llseek() only for the signed long subset of long long */
	if (file->f_op && file->f_op->lseek) {
		if (offset != (long) offset)
			return -EINVAL;
		return file->f_op->lseek(file->f_inode,file,offset,origin);
	}

	switch (origin) {
		case 0:
			tmp = offset;
			break;
		case 1:
			tmp = file->f_pos + offset;
			break;
		case 2:
			if (!file->f_inode)
				return -EINVAL;
			tmp = file->f_inode->i_size + offset;
			break;
	}
	if (tmp < 0)
		return -EINVAL;
	if (tmp != file->f_pos) {
		file->f_pos = tmp;
		file->f_reada = 0;
		file->f_version = ++event;
	}
	memcpy_tofs(result, &file->f_pos, sizeof(loff_t));
	return 0;
}

asmlinkage int sys_read(unsigned int fd,char * buf,unsigned int count)
{
	int error;
	struct file * file;
	struct inode * inode;

	if (fd>=NR_OPEN || !(file=current->files->fd[fd]) || !(inode=file->f_inode))
		return -EBADF;
	if (!(file->f_mode & 1))
		return -EBADF;
	if (!file->f_op || !file->f_op->read)
		return -EINVAL;
	if (!count)
		return 0;
	error = verify_area(VERIFY_WRITE,buf,count);
	if (error)
		return error;
	return file->f_op->read(inode,file,buf,count);
}

asmlinkage int sys_write(unsigned int fd,char * buf,unsigned int count)
{
	int error;
	struct file * file;
	struct inode * inode;
	int written;
	
	if (fd>=NR_OPEN || !(file=current->files->fd[fd]) || !(inode=file->f_inode))
		return -EBADF;
	if (!(file->f_mode & 2))
		return -EBADF;
	if (!file->f_op || !file->f_op->write)
		return -EINVAL;
	if (!count)
		return 0;
	error = verify_area(VERIFY_READ,buf,count);
	if (error)
		return error;
	written = file->f_op->write(inode,file,buf,count);
	/*
	 * If data has been written to the file, remove the setuid and
	 * the setgid bits
	 */
	if (written > 0 && !suser() && (inode->i_mode & (S_ISUID | S_ISGID))) {
		struct iattr newattrs;
		newattrs.ia_mode = inode->i_mode & ~(S_ISUID | S_ISGID);
		newattrs.ia_valid = ATTR_MODE;
		notify_change(inode, &newattrs);
	}
	return written;
}

/*
 * OSF/1 (and SunOS) readv/writev emulation.
 *
 * NOTE! This is not really the way it should be done,
 * but it should be good enough for TCP connections,
 * notably X11 ;-)
 */
asmlinkage int sys_readv(unsigned long fd, const struct iovec * vector, long count)
{
	int retval;
	struct file * file;
	struct inode * inode;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]) || !(inode = file->f_inode))
		return -EBADF;
	if (!(file->f_mode & 1))
		return -EBADF;
	if (!file->f_op || !file->f_op->read)
		return -EINVAL;
	if (!count)
		return 0;
	retval = verify_area(VERIFY_READ, vector, count*sizeof(*vector));
	if (retval)
		return retval;

	while (count > 0) {
		void * base;
		int len, nr;

		base = get_user(&vector->iov_base);
		len = get_user(&vector->iov_len);
		vector++;
		count--;
		nr = verify_area(VERIFY_WRITE, base, len);
		if (!nr)
			nr = file->f_op->read(inode, file, base, len);
		if (nr < 0) {
			if (retval)
				return retval;
			return nr;
		}
		retval += nr;
		if (nr != len)
			break;
	}
	return retval;
}

asmlinkage int sys_writev(unsigned long fd, const struct iovec * vector, long count)
{
	int retval;
	struct file * file;
	struct inode * inode;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]) || !(inode = file->f_inode))
		return -EBADF;
	if (!(file->f_mode & 2))
		return -EBADF;
	if (!file->f_op || !file->f_op->write)
		return -EINVAL;
	if (!count)
		return 0;
	retval = verify_area(VERIFY_READ, vector, count*sizeof(*vector));
	if (retval)
		return retval;

	while (count > 0) {
		void * base;
		int len, nr;

		base = get_user(&vector->iov_base);
		len = get_user(&vector->iov_len);
		vector++;
		count--;
		nr = verify_area(VERIFY_READ, base, len);
		if (!nr)
			nr = file->f_op->write(inode, file, base, len);
		if (nr < 0) {
			if (retval)
				return retval;
			return nr;
		}
		retval += nr;
		if (nr != len)
			break;
	}
	return retval;
}
