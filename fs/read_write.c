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
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/uio.h>

#include <asm/segment.h>

static long long default_llseek(struct inode *inode,
	struct file *file,
	long long offset,
	int origin)
{
	long long retval;

	switch (origin) {
		case 2:
			offset += inode->i_size;
			break;
		case 1:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset >= 0) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_reada = 0;
			file->f_version = ++event;
		}
		retval = offset;
	}
	return retval;
}

static inline long long llseek(struct inode * inode, struct file *file,
	long long offset, unsigned int origin)
{
	long long (*fn)(struct inode *, struct file *, long long, int);

	fn = default_llseek;
	if (file->f_op && file->f_op->llseek)
		fn = file->f_op->llseek;
	return fn(inode, file, offset, origin);
}

asmlinkage long sys_lseek(unsigned int fd, off_t offset, unsigned int origin)
{
	long retval;
	struct file * file;
	struct inode * inode;

	retval = -EBADF;
	if (fd >= NR_OPEN ||
	    !(file = current->files->fd[fd]) ||
	    !(inode = file->f_inode))
		goto bad;
	retval = -EINVAL;
	if (origin > 2)
		goto bad;
	retval = llseek(inode, file, offset, origin);
bad:
	return retval;
}

asmlinkage int sys_llseek(unsigned int fd, unsigned long offset_high,
			  unsigned long offset_low, loff_t * result,
			  unsigned int origin)
{
	long retval;
	struct file * file;
	struct inode * inode;
	long long offset;

	retval = -EBADF;
	if (fd >= NR_OPEN ||
	    !(file = current->files->fd[fd]) ||
	    !(inode = file->f_inode))
		goto bad;
	retval = -EINVAL;
	if (origin > 2)
		goto bad;

	retval = verify_area(VERIFY_WRITE, result, sizeof(offset));
	if (retval)
		goto bad;

	offset = llseek(inode, file,
		(((unsigned long long) offset_high << 32) | offset_low),
		origin);

	retval = offset;
	if (offset >= 0) {
		put_user(offset, result);
		retval = 0;
	}
bad:
	return retval;
}

asmlinkage long sys_read(unsigned int fd, char * buf, unsigned long count)
{
	int error;
	struct file * file;
	struct inode * inode;
	long (*read)(struct inode *, struct file *, char *, unsigned long);

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	inode = file->f_inode;
	if (!inode)
		goto out;
	error = -EBADF;
	if (!(file->f_mode & 1))
		goto out;
	error = locks_verify_area(FLOCK_VERIFY_READ,inode,file,file->f_pos,count);
	if (error)
		goto out;
	error = verify_area(VERIFY_WRITE,buf,count);
	if (error)
		goto out;
	error = -EINVAL;
	if (!file->f_op || !(read = file->f_op->read))
		goto out;
	error = read(inode,file,buf,count);
out:
	fput(file, inode);
bad_file:
	return error;
}

asmlinkage long sys_write(unsigned int fd, const char * buf, unsigned long count)
{
	int error;
	struct file * file;
	struct inode * inode;
	long (*write)(struct inode *, struct file *, const char *, unsigned long);

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	inode = file->f_inode;
	if (!inode)
		goto out;
	if (!(file->f_mode & 2))
		goto out;
	error = locks_verify_area(FLOCK_VERIFY_WRITE,inode,file,file->f_pos,count);
	if (error)
		goto out;
	error = verify_area(VERIFY_READ,buf,count);
	if (error)
		goto out;
	error = -EINVAL;
	if (!file->f_op || !(write = file->f_op->write))
		goto out;
	down(&inode->i_sem);
	error = write(inode,file,buf,count);
	up(&inode->i_sem);
out:
	fput(file, inode);
bad_file:
	return error;
}

static long sock_readv_writev(int type, struct inode * inode, struct file * file,
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

typedef long (*IO_fn_t)(struct inode *, struct file *, char *, unsigned long);

static long do_readv_writev(int type, struct inode * inode, struct file * file,
	const struct iovec * vector, unsigned long count)
{
	unsigned long tot_len;
	struct iovec iov[UIO_MAXIOV];
	long retval, i;
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

asmlinkage long sys_readv(unsigned long fd, const struct iovec * vector, unsigned long count)
{
	struct file * file;
	struct inode * inode;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]) || !(inode = file->f_inode))
		return -EBADF;
	if (!(file->f_mode & 1))
		return -EBADF;
	return do_readv_writev(VERIFY_WRITE, inode, file, vector, count);
}

asmlinkage long sys_writev(unsigned long fd, const struct iovec * vector, unsigned long count)
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
