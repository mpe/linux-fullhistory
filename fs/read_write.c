/*
 *  linux/fs/read_write.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/malloc.h> 
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

static loff_t default_llseek(struct file *file, loff_t offset, int origin)
{
	long long retval;

	switch (origin) {
		case 2:
			offset += file->f_dentry->d_inode->i_size;
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

static inline loff_t llseek(struct file *file, loff_t offset, int origin)
{
	loff_t (*fn)(struct file *, loff_t, int);

	fn = default_llseek;
	if (file->f_op && file->f_op->llseek)
		fn = file->f_op->llseek;
	return fn(file, offset, origin);
}

asmlinkage off_t sys_lseek(unsigned int fd, off_t offset, unsigned int origin)
{
	off_t retval;
	struct file * file;
	struct dentry * dentry;
	struct inode * inode;

	lock_kernel();
	retval = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad;
	/* N.B. Shouldn't this be ENOENT?? */
	if (!(dentry = file->f_dentry) ||
	    !(inode = dentry->d_inode))
		goto out_putf;
	retval = -EINVAL;
	if (origin <= 2)
		retval = llseek(file, offset, origin);
out_putf:
	fput(file);
bad:
	unlock_kernel();
	return retval;
}

#if !defined(__alpha__)
asmlinkage int sys_llseek(unsigned int fd, unsigned long offset_high,
			  unsigned long offset_low, loff_t * result,
			  unsigned int origin)
{
	int retval;
	struct file * file;
	struct dentry * dentry;
	struct inode * inode;
	loff_t offset;

	lock_kernel();
	retval = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad;
	/* N.B. Shouldn't this be ENOENT?? */
	if (!(dentry = file->f_dentry) ||
	    !(inode = dentry->d_inode))
		goto out_putf;
	retval = -EINVAL;
	if (origin > 2)
		goto out_putf;

	offset = llseek(file, ((loff_t) offset_high << 32) | offset_low,
			origin);

	retval = (int)offset;
	if (offset >= 0) {
		retval = -EFAULT;
		if (!copy_to_user(result, &offset, sizeof(offset)))
			retval = 0;
	}
out_putf:
	fput(file);
bad:
	unlock_kernel();
	return retval;
}
#endif

asmlinkage ssize_t sys_read(unsigned int fd, char * buf, size_t count)
{
	ssize_t ret;
	struct file * file;
	ssize_t (*read)(struct file *, char *, size_t, loff_t *);

	lock_kernel();

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (!(file->f_mode & FMODE_READ))
		goto out;
	ret = locks_verify_area(FLOCK_VERIFY_READ, file->f_dentry->d_inode,
				file, file->f_pos, count);
	if (ret)
		goto out;
	ret = -EINVAL;
	if (!file->f_op || !(read = file->f_op->read))
		goto out;
	ret = read(file, buf, count, &file->f_pos);
out:
	fput(file);
bad_file:
	unlock_kernel();
	return ret;
}

asmlinkage ssize_t sys_write(unsigned int fd, const char * buf, size_t count)
{
	ssize_t ret;
	struct file * file;
	struct inode * inode;
	ssize_t (*write)(struct file *, const char *, size_t, loff_t *);

	lock_kernel();

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (!(file->f_mode & FMODE_WRITE))
		goto out;
	inode = file->f_dentry->d_inode;
	ret = locks_verify_area(FLOCK_VERIFY_WRITE, inode, file,
				file->f_pos, count);
	if (ret)
		goto out;
	ret = -EINVAL;
	if (!file->f_op || !(write = file->f_op->write))
		goto out;

	down(&inode->i_sem);
	ret = write(file, buf, count, &file->f_pos);
	up(&inode->i_sem);
out:
	fput(file);
bad_file:
	unlock_kernel();
	return ret;
}


static ssize_t do_readv_writev(int type, struct file *file,
			       const struct iovec * vector,
			       unsigned long count)
{
	typedef ssize_t (*io_fn_t)(struct file *, char *, size_t, loff_t *);

	size_t tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack;
	ssize_t ret, i;
	io_fn_t fn;
	struct inode *inode;

	/*
	 * First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	ret = 0;
	if (!count)
		goto out_nofree;
	ret = -EINVAL;
	if (count > UIO_MAXIOV)
		goto out_nofree;
	if (count > UIO_FASTIOV) {
		ret = -ENOMEM;
		iov = kmalloc(count*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			goto out_nofree;
	}
	ret = -EFAULT;
	if (copy_from_user(iov, vector, count*sizeof(*vector)))
		goto out;

	tot_len = 0;
	for (i = 0 ; i < count ; i++)
		tot_len += iov[i].iov_len;

	inode = file->f_dentry->d_inode;
	/* VERIFY_WRITE actually means a read, as we write to user space */
	ret = locks_verify_area((type == VERIFY_WRITE
				 ? FLOCK_VERIFY_READ : FLOCK_VERIFY_WRITE),
				inode, file, file->f_pos, tot_len);
	if (ret) goto out;

	/*
	 * Then do the actual IO.  Note that sockets need to be handled
	 * specially as they have atomicity guarantees and can handle
	 * iovec's natively
	 */
	if (inode->i_sock) {
		ret = sock_readv_writev(type,inode,file,iov,count,tot_len);
		goto out;
	}

	ret = -EINVAL;
	if (!file->f_op)
		goto out;

	/* VERIFY_WRITE actually means a read, as we write to user space */
	fn = file->f_op->read;
	if (type == VERIFY_READ)
		fn = (io_fn_t) file->f_op->write;		

	ret = 0;
	vector = iov;
	while (count > 0) {
		void * base;
		size_t len;
		ssize_t nr;

		base = vector->iov_base;
		len = vector->iov_len;
		vector++;
		count--;

		nr = fn(file, base, len, &file->f_pos);

		if (nr < 0) {
			if (!ret) ret = nr;
			break;
		}
		ret += nr;
		if (nr != len)
			break;
	}

out:
	if (iov != iovstack)
		kfree(iov);
out_nofree:
	return ret;
}

asmlinkage ssize_t sys_readv(unsigned long fd, const struct iovec * vector,
			     unsigned long count)
{
	struct file * file;
	ssize_t ret;

	lock_kernel();

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (file->f_op && file->f_op->read && (file->f_mode & FMODE_READ))
		ret = do_readv_writev(VERIFY_WRITE, file, vector, count);
	fput(file);

bad_file:
	unlock_kernel();
	return ret;
}

asmlinkage ssize_t sys_writev(unsigned long fd, const struct iovec * vector,
			      unsigned long count)
{
	struct file * file;
	ssize_t ret;

	lock_kernel();

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (file->f_op && file->f_op->write && (file->f_mode & FMODE_WRITE)) {
		down(&file->f_dentry->d_inode->i_sem);
		ret = do_readv_writev(VERIFY_READ, file, vector, count);
		up(&file->f_dentry->d_inode->i_sem);
	}
	fput(file);

bad_file:
	unlock_kernel();
	return ret;
}

/* From the Single Unix Spec: pread & pwrite act like lseek to pos + op +
   lseek back to original location.  They fail just like lseek does on
   non-seekable files.  */

asmlinkage ssize_t sys_pread(unsigned int fd, char * buf,
			     size_t count, loff_t pos)
{
	ssize_t ret;
	struct file * file;
	ssize_t (*read)(struct file *, char *, size_t, loff_t *);

	lock_kernel();

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (!(file->f_mode & FMODE_READ))
		goto out;
	ret = locks_verify_area(FLOCK_VERIFY_READ, file->f_dentry->d_inode,
				file, pos, count);
	if (ret)
		goto out;
	ret = -EINVAL;
	if (!file->f_op || !(read = file->f_op->read))
		goto out;
	if (pos < 0)
		goto out;
	ret = read(file, buf, count, &pos);
out:
	fput(file);
bad_file:
	unlock_kernel();
	return ret;
}

asmlinkage ssize_t sys_pwrite(unsigned int fd, const char * buf,
			      size_t count, loff_t pos)
{
	ssize_t ret;
	struct file * file;
	ssize_t (*write)(struct file *, const char *, size_t, loff_t *);

	lock_kernel();

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (!(file->f_mode & FMODE_WRITE))
		goto out;
	ret = locks_verify_area(FLOCK_VERIFY_WRITE, file->f_dentry->d_inode,
				file, pos, count);
	if (ret)
		goto out;
	ret = -EINVAL;
	if (!file->f_op || !(write = file->f_op->write))
		goto out;
	if (pos < 0)
		goto out;

	down(&file->f_dentry->d_inode->i_sem);
	ret = write(file, buf, count, &pos);
	up(&file->f_dentry->d_inode->i_sem);

out:
	fput(file);
bad_file:
	unlock_kernel();
	return ret;
}
