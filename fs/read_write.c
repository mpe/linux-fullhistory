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
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/uio.h>

#include <asm/segment.h>

asmlinkage long sys_lseek(unsigned int fd, off_t offset, unsigned int origin)
{
	struct file * file;
	long tmp = -1;

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

asmlinkage int sys_read(unsigned int fd,char * buf,int count)
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
	if (count <= 0)
		return 0;
	error = locks_verify_area(FLOCK_VERIFY_READ,inode,file,file->f_pos,count);
	if (error)
		return error;
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
	error = locks_verify_area(FLOCK_VERIFY_WRITE,inode,file,file->f_pos,count);
	if (error)
		return error;
	error = verify_area(VERIFY_READ,buf,count);
	if (error)
		return error;
	/*
	 * If data has been written to the file, remove the setuid and
	 * the setgid bits. We do it anyway otherwise there is an
	 * extremely exploitable race - does your OS get it right |->
	 *
	 * Set ATTR_FORCE so it will always be changed.
	 */
	if (!suser() && (inode->i_mode & (S_ISUID | S_ISGID))) {
		struct iattr newattrs;
		/*
		 * Don't turn off setgid if no group execute. This special
		 * case marks candidates for mandatory locking.
		 */
		newattrs.ia_mode = inode->i_mode &
			~(S_ISUID | ((inode->i_mode & S_IXGRP) ? S_ISGID : 0));
		newattrs.ia_valid = ATTR_CTIME | ATTR_MODE | ATTR_FORCE;
		notify_change(inode, &newattrs);
	}

	down(&inode->i_sem);
	written = file->f_op->write(inode,file,buf,count);
	up(&inode->i_sem);
	return written;
}

static int sock_readv_writev(int type, struct inode * inode, struct file * file,
	const struct iovec * iov, long count, long size)
{
	struct msghdr msg;
	struct socket *sock;

	sock = &inode->u.socket_i;
	if (!sock->ops)
		return -EOPNOTSUPP;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_iov = (struct iovec *) iov;
	msg.msg_iovlen = count;

	/* read() does a VERIFY_WRITE */
	if (type == VERIFY_WRITE) {
		if (!sock->ops->recvmsg)
			return -EOPNOTSUPP;
		return sock->ops->recvmsg(sock, &msg, size,
			(file->f_flags & O_NONBLOCK), 0, NULL);
	}
	if (!sock->ops->sendmsg)
		return -EOPNOTSUPP;
	return sock->ops->sendmsg(sock, &msg, size,
		(file->f_flags & O_NONBLOCK), 0);
}

typedef int (*IO_fn_t)(struct inode *, struct file *, char *, int);

static int do_readv_writev(int type, struct inode * inode, struct file * file,
	const struct iovec * vector, unsigned long count)
{
	size_t tot_len;
	struct iovec iov[UIO_MAXIOV];
	int retval, i;
	IO_fn_t fn;

	/*
	 * First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	if (!count)
		return 0;
	if (count > UIO_MAXIOV)
		return -EINVAL;
	retval = verify_area(VERIFY_READ, vector, count*sizeof(*vector));
	if (retval)
		return retval;
	memcpy_fromfs(iov, vector, count*sizeof(*vector));
	tot_len = 0;
	for (i = 0 ; i < count ; i++) {
		tot_len += iov[i].iov_len;
		retval = verify_area(type, iov[i].iov_base, iov[i].iov_len);
		if (retval)
			return retval;
	}

	retval = locks_verify_area(type == VERIFY_READ ? FLOCK_VERIFY_READ : FLOCK_VERIFY_WRITE,
				   inode, file, file->f_pos, tot_len);
	if (retval)
		return retval;

	/*
	 * Then do the actual IO.  Note that sockets need to be handled
	 * specially as they have atomicity guarantees and can handle
	 * iovec's natively
	 */
	if (inode->i_sock)
		return sock_readv_writev(type, inode, file, iov, count, tot_len);

	if (!file->f_op)
		return -EINVAL;
	/* VERIFY_WRITE actually means a read, as we write to user space */
	fn = file->f_op->read;
	if (type == VERIFY_READ)
		fn = (IO_fn_t) file->f_op->write;		
	vector = iov;
	while (count > 0) {
		void * base;
		int len, nr;

		base = vector->iov_base;
		len = vector->iov_len;
		vector++;
		count--;
		nr = fn(inode, file, base, len);
		if (nr < 0) {
			if (retval)
				break;
			retval = nr;
			break;
		}
		retval += nr;
		if (nr != len)
			break;
	}
	return retval;
}

asmlinkage int sys_readv(unsigned long fd, const struct iovec * vector, long count)
{
	struct file * file;
	struct inode * inode;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]) || !(inode = file->f_inode))
		return -EBADF;
	if (!(file->f_mode & 1))
		return -EBADF;
	return do_readv_writev(VERIFY_WRITE, inode, file, vector, count);
}

asmlinkage int sys_writev(unsigned long fd, const struct iovec * vector, long count)
{
	int error;
	struct file * file;
	struct inode * inode;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]) || !(inode = file->f_inode))
		return -EBADF;
	if (!(file->f_mode & 2))
		return -EBADF;
	down(&inode->i_sem);
	error = do_readv_writev(VERIFY_READ, inode, file, vector, count);
	up(&inode->i_sem);
	return error;
}
