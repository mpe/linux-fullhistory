/*
 * sys_ppc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 2001 IBM
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <asm/ptrace.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h> 
#include <linux/mm.h> 
#include <linux/file.h> 
#include <linux/signal.h>
#include <linux/resource.h>
#include <linux/times.h>
#include <linux/utsname.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/nfs_fs.h>
#include <linux/smb_fs.h>
#include <linux/smb_mount.h>
#include <linux/ncp_fs.h>
#include <linux/module.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/filter.h>
#include <linux/highmem.h>
#include <linux/highuid.h>
#include <linux/mman.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/icmpv6.h>
#include <linux/sysctl.h>
#include <linux/binfmts.h>
#include <linux/dnotify.h>
#include <linux/security.h>
#include <linux/compat.h>

#include <asm/types.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>

#include <asm/semaphore.h>

#include <net/scm.h>
#include <net/sock.h>
#include <linux/elf.h>
#include <asm/ppcdebug.h>
#include <asm/time.h>
#include <asm/ppc32.h>
#include <asm/mmu_context.h>

struct iovec32 { u32 iov_base; compat_size_t iov_len; };

typedef ssize_t (*io_fn_t)(struct file *, char *, size_t, loff_t *);
typedef ssize_t (*iov_fn_t)(struct file *, const struct iovec *, unsigned long, loff_t *);

static long do_readv_writev32(int type, struct file *file,
			      const struct iovec32 *vector, u32 count)
{
	compat_ssize_t tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack, *ivp;
	struct inode *inode;
	long retval, i;
	io_fn_t fn;
	iov_fn_t fnv;

	/*
	 * SuS says "The readv() function *may* fail if the iovcnt argument
	 * was less than or equal to 0, or greater than {IOV_MAX}.  Linux has
	 * traditionally returned zero for zero segments, so...
	 */
	retval = 0;
	if (count == 0)
		goto out;

	/* First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	retval = -EINVAL;
	if (count > UIO_MAXIOV)
		goto out;
	if (!file->f_op)
		goto out;
	if (count > UIO_FASTIOV) {
		retval = -ENOMEM;
		iov = kmalloc(count*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			goto out;
	}
	retval = -EFAULT;
	if (verify_area(VERIFY_READ, vector, sizeof(struct iovec32)*count))
		goto out;

	/*
	 * Single unix specification:
	 * We should -EINVAL if an element length is not >= 0 and fitting an
	 * ssize_t.  The total length is fitting an ssize_t
	 *
	 * Be careful here because iov_len is a size_t not an ssize_t
	 */
	tot_len = 0;
	i = count;
	ivp = iov;
	retval = -EINVAL;
	while(i > 0) {
		compat_ssize_t tmp = tot_len;
		compat_ssize_t len;
		u32 buf;

		if (__get_user(len, &vector->iov_len) ||
		    __get_user(buf, &vector->iov_base)) {
			retval = -EFAULT;
			goto out;
		}
		if (len < 0)	/* size_t not fitting an compat_ssize_t .. */
			goto out;
		tot_len += len;
		if (tot_len < tmp) /* maths overflow on the compat_ssize_t */
			goto out;
		ivp->iov_base = (void *)A(buf);
		ivp->iov_len = (__kernel_size_t) len;
		vector++;
		ivp++;
		i--;
	}
	if (tot_len == 0) {
		retval = 0;
		goto out;
	}

	inode = file->f_dentry->d_inode;
	/* VERIFY_WRITE actually means a read, as we write to user space */
	retval = locks_verify_area((type == READ
				    ? FLOCK_VERIFY_READ : FLOCK_VERIFY_WRITE),
				   inode, file, file->f_pos, tot_len);
	if (retval)
		goto out;

	if (type == READ) {
		fn = file->f_op->read;
		fnv = file->f_op->readv;
	} else {
		fn = (io_fn_t)file->f_op->write;
		fnv = file->f_op->writev;
	}
	if (fnv) {
		retval = fnv(file, iov, count, &file->f_pos);
		goto out;
	}

	/* Do it by hand, with file-ops */
	ivp = iov;
	while (count > 0) {
		void * base;
		int len, nr;

		base = ivp->iov_base;
		len = ivp->iov_len;
		ivp++;
		count--;

		nr = fn(file, base, len, &file->f_pos);

		if (nr < 0) {
			if (!retval)
				retval = nr;
			break;
		}
		retval += nr;
		if (nr != len)
			break;
	}
out:
	if (iov != iovstack)
		kfree(iov);
	if ((retval + (type == READ)) > 0)
		dnotify_parent(file->f_dentry,
			       (type == READ) ? DN_ACCESS : DN_MODIFY);

	return retval;
}

asmlinkage long sys32_readv(int fd, struct iovec32 *vector, u32 count)
{
	struct file *file;
	int ret = -EBADF;

	file = fget(fd);
	if (!file || !(file->f_mode & FMODE_READ))
		goto out; 

	ret = -EINVAL;
	if (!file->f_op || (!file->f_op->readv && !file->f_op->read))
		goto out;

	ret = do_readv_writev32(READ, file, vector, count);

out:
	if (file)
		fput(file);
	return ret;
}

asmlinkage long sys32_writev(int fd, struct iovec32 *vector, u32 count)
{
	struct file *file;
	int ret = -EBADF;

	file = fget(fd);
	if (!file || !(file->f_mode & FMODE_WRITE))
		goto out;

	ret = -EINVAL;
	if (!file->f_op || (!file->f_op->writev && !file->f_op->write))
		goto out;

	ret = do_readv_writev32(WRITE, file, vector, count);

out:
	if (file)
		fput(file);
	return ret;
}

extern asmlinkage long sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg);
asmlinkage long sys32_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
	{
		struct flock f;
		mm_segment_t old_fs;
		long ret;

		if(get_compat_flock(&f, (struct compat_flock *)arg))
			return -EFAULT;
		old_fs = get_fs(); set_fs (KERNEL_DS);
		ret = sys_fcntl(fd, cmd, (unsigned long)&f);
		set_fs (old_fs);
		if(put_compat_flock(&f, (struct compat_flock *)arg))
			return -EFAULT;
		return ret;
	}
	default:
		return sys_fcntl(fd, cmd, (unsigned long)arg);
	}
}

struct ncp_mount_data32_v3 {
        int version;
        unsigned int ncp_fd;
        compat_uid_t mounted_uid;
        compat_pid_t wdog_pid;
        unsigned char mounted_vol[NCP_VOLNAME_LEN + 1];
        unsigned int time_out;
        unsigned int retry_count;
        unsigned int flags;
        compat_uid_t uid;
        compat_gid_t gid;
        compat_mode_t file_mode;
        compat_mode_t dir_mode;
};

struct ncp_mount_data32_v4 {
	int version;
	/* all members below are "long" in ABI ... i.e. 32bit on sparc32, while 64bits on sparc64 */
	unsigned int flags;
	unsigned int mounted_uid;
	int wdog_pid;

	unsigned int ncp_fd;
	unsigned int time_out;
	unsigned int retry_count;

	unsigned int uid;
	unsigned int gid;
	unsigned int file_mode;
	unsigned int dir_mode;
};

static void *do_ncp_super_data_conv(void *raw_data)
{
	switch (*(int*)raw_data) {
		case NCP_MOUNT_VERSION:
			{
				struct ncp_mount_data news, *n = &news; 
				struct ncp_mount_data32_v3 *n32 = (struct ncp_mount_data32_v3 *)raw_data;

				n->version = n32->version;
				n->ncp_fd = n32->ncp_fd;
				n->mounted_uid = n32->mounted_uid;
				n->wdog_pid = n32->wdog_pid;
				memmove (n->mounted_vol, n32->mounted_vol, sizeof (n32->mounted_vol));
				n->time_out = n32->time_out;
				n->retry_count = n32->retry_count;
				n->flags = n32->flags;
				n->uid = n32->uid;
				n->gid = n32->gid;
				n->file_mode = n32->file_mode;
				n->dir_mode = n32->dir_mode;
				memcpy(raw_data, n, sizeof(*n)); 
			}
			break;
		case NCP_MOUNT_VERSION_V4:
			{
				struct ncp_mount_data_v4 news, *n = &news; 
				struct ncp_mount_data32_v4 *n32 = (struct ncp_mount_data32_v4 *)raw_data;

				n->version = n32->version;
				n->flags = n32->flags;
				n->mounted_uid = n32->mounted_uid;
				n->wdog_pid = n32->wdog_pid;
				n->ncp_fd = n32->ncp_fd;
				n->time_out = n32->time_out;
				n->retry_count = n32->retry_count;
				n->uid = n32->uid;
				n->gid = n32->gid;
				n->file_mode = n32->file_mode;
				n->dir_mode = n32->dir_mode;
				memcpy(raw_data, n, sizeof(*n)); 
			}
			break;
		default:
			/* do not touch unknown structures */
			break;
	}
	return raw_data;
}

struct smb_mount_data32 {
        int version;
        compat_uid_t mounted_uid;
        compat_uid_t uid;
        compat_gid_t gid;
        compat_mode_t file_mode;
        compat_mode_t dir_mode;
};

static void *do_smb_super_data_conv(void *raw_data)
{
	struct smb_mount_data news, *s = &news;
	struct smb_mount_data32 *s32 = (struct smb_mount_data32 *)raw_data;

	if (s32->version != SMB_MOUNT_OLDVERSION)
		goto out;
	s->version = s32->version;
	s->mounted_uid = s32->mounted_uid;
	s->uid = s32->uid;
	s->gid = s32->gid;
	s->file_mode = s32->file_mode;
	s->dir_mode = s32->dir_mode;
	memcpy(raw_data, s, sizeof(struct smb_mount_data)); 
out:
	return raw_data;
}

static int copy_mount_stuff_to_kernel(const void *user, unsigned long *kernel)
{
	int i;
	unsigned long page;
	struct vm_area_struct *vma;

	*kernel = 0;
	if(!user)
		return 0;
	vma = find_vma(current->mm, (unsigned long)user);
	if(!vma || (unsigned long)user < vma->vm_start)
		return -EFAULT;
	if(!(vma->vm_flags & VM_READ))
		return -EFAULT;
	i = vma->vm_end - (unsigned long) user;
	if(PAGE_SIZE <= (unsigned long) i)
		i = PAGE_SIZE - 1;
	if(!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	if(copy_from_user((void *) page, user, i)) {
		free_page(page);
		return -EFAULT;
	}
	*kernel = page;
	return 0;
}

#define SMBFS_NAME	"smbfs"
#define NCPFS_NAME	"ncpfs"

asmlinkage long sys32_mount(char *dev_name, char *dir_name, char *type, unsigned long new_flags, u32 data)
{
	unsigned long type_page = 0;
	unsigned long data_page = 0;
	unsigned long dev_page = 0;
	unsigned long dir_page = 0;
	int err, is_smb, is_ncp;
	
	is_smb = is_ncp = 0;

	err = copy_mount_stuff_to_kernel((const void *)type, &type_page);
	if (err)
		goto out;

	if (!type_page) {
		err = -EINVAL;
		goto out;
	}

	is_smb = !strcmp((char *)type_page, SMBFS_NAME);
	is_ncp = !strcmp((char *)type_page, NCPFS_NAME);

	err = copy_mount_stuff_to_kernel((const void *)AA(data), &data_page);
	if (err)
		goto type_out;

	err = copy_mount_stuff_to_kernel(dev_name, &dev_page);
	if (err)
		goto data_out;

	err = copy_mount_stuff_to_kernel(dir_name, &dir_page);
	if (err)
		goto dev_out;

	if (!is_smb && !is_ncp) {
		lock_kernel();
		err = do_mount((char*)dev_page, (char*)dir_page,
				(char*)type_page, new_flags, (char*)data_page);
		unlock_kernel();
	} else {
		if (is_ncp)
			do_ncp_super_data_conv((void *)data_page);
		else
			do_smb_super_data_conv((void *)data_page);

		lock_kernel();
		err = do_mount((char*)dev_page, (char*)dir_page,
				(char*)type_page, new_flags, (char*)data_page);
		unlock_kernel();
	}
	free_page(dir_page);

dev_out:
	free_page(dev_page);

data_out:
	free_page(data_page);

type_out:
	free_page(type_page);

out:
	return err;
}

/* readdir & getdents */
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+sizeof(u32)-1) & ~(sizeof(u32)-1))

struct old_linux_dirent32 {
	u32		d_ino;
	u32		d_offset;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct readdir_callback32 {
	struct old_linux_dirent32 * dirent;
	int count;
};

static int fillonedir(void * __buf, const char * name, int namlen,
		                  off_t offset, ino_t ino, unsigned int d_type)
{
	struct readdir_callback32 * buf = (struct readdir_callback32 *) __buf;
	struct old_linux_dirent32 * dirent;

	if (buf->count)
		return -EINVAL;
	buf->count++;
	dirent = buf->dirent;
	put_user(ino, &dirent->d_ino);
	put_user(offset, &dirent->d_offset);
	put_user(namlen, &dirent->d_namlen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	return 0;
}

asmlinkage int old32_readdir(unsigned int fd, struct old_linux_dirent32 *dirent, unsigned int count)
{
	int error = -EBADF;
	struct file * file;
	struct readdir_callback32 buf;

	file = fget(fd);
	if (!file)
		goto out;

	buf.count = 0;
	buf.dirent = dirent;

	error = vfs_readdir(file, (filldir_t)fillonedir, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.count;

out_putf:
	fput(file);
out:
	return error;
}

struct linux_dirent32 {
	u32		d_ino;
	u32		d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};

struct getdents_callback32 {
	struct linux_dirent32 * current_dir;
	struct linux_dirent32 * previous;
	int count;
	int error;
};

static int
filldir(void * __buf, const char * name, int namlen, off_t offset, ino_t ino,
		               unsigned int d_type)
{
	struct linux_dirent32 * dirent;
	struct getdents_callback32 * buf = (struct getdents_callback32 *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent)
		put_user(offset, &dirent->d_off);
	dirent = buf->current_dir;
	buf->previous = dirent;
	put_user(ino, &dirent->d_ino);
	put_user(reclen, &dirent->d_reclen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	((char *) dirent) += reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage long sys32_getdents(unsigned int fd, struct linux_dirent32 *dirent, unsigned int count)
{
	struct file * file;
	struct linux_dirent32 * lastdirent;
	struct getdents_callback32 buf;
	int error = -EBADF;

	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, (filldir_t)filldir, &buf);
	if (error < 0)
		goto out_putf;
	lastdirent = buf.previous;
	error = buf.error;
	if(lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}
 out_putf:
	fput(file);

 out:
	return error;
}
/* end of readdir & getdents */



/* 32-bit timeval and related flotsam.  */

/*
 * Ooo, nasty.  We need here to frob 32-bit unsigned longs to
 * 64-bit unsigned longs.
 */
static inline int
get_fd_set32(unsigned long n, unsigned long *fdset, u32 *ufdset)
{
	if (ufdset) {
		unsigned long odd;

		if (verify_area(VERIFY_WRITE, ufdset, n*sizeof(u32)))
			return -EFAULT;

		odd = n & 1UL;
		n &= ~1UL;
		while (n) {
			unsigned long h, l;
			__get_user(l, ufdset);
			__get_user(h, ufdset+1);
			ufdset += 2;
			*fdset++ = h << 32 | l;
			n -= 2;
		}
		if (odd)
			__get_user(*fdset, ufdset);
	} else {
		/* Tricky, must clear full unsigned long in the
		 * kernel fdset at the end, this makes sure that
		 * actually happens.
		 */
		memset(fdset, 0, ((n + 1) & ~1)*sizeof(u32));
	}
	return 0;
}

static inline void
set_fd_set32(unsigned long n, u32 *ufdset, unsigned long *fdset)
{
	unsigned long odd;

	if (!ufdset)
		return;

	odd = n & 1UL;
	n &= ~1UL;
	while (n) {
		unsigned long h, l;
		l = *fdset++;
		h = l >> 32;
		__put_user(l, ufdset);
		__put_user(h, ufdset+1);
		ufdset += 2;
		n -= 2;
	}
	if (odd)
		__put_user(*fdset, ufdset);
}



#define MAX_SELECT_SECONDS ((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)

asmlinkage long sys32_select(int n, u32 *inp, u32 *outp, u32 *exp, u32 tvp_x)
{
	fd_set_bits fds;
	struct compat_timeval *tvp = (struct compat_timeval *)AA(tvp_x);
	char *bits;
	unsigned long nn;
	long timeout;
	int ret, size, max_fdset;

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp) {
		time_t sec, usec;
		if ((ret = verify_area(VERIFY_READ, tvp, sizeof(*tvp)))
		    || (ret = __get_user(sec, &tvp->tv_sec))
		    || (ret = __get_user(usec, &tvp->tv_usec)))
			goto out_nofds;

		ret = -EINVAL;
		if(sec < 0 || usec < 0)
			goto out_nofds;

		if ((unsigned long) sec < MAX_SELECT_SECONDS) {
			timeout = (usec + 1000000/HZ - 1) / (1000000/HZ);
			timeout += sec * (unsigned long) HZ;
		}
	}

	ret = -EINVAL;
	if (n < 0)
		goto out_nofds;

	/* max_fdset can increase, so grab it once to avoid race */
	max_fdset = current->files->max_fdset;
	if (n > max_fdset)
		n = max_fdset;

	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words. 
	 */
	ret = -ENOMEM;
	size = FDS_BYTES(n);
	bits = kmalloc(6 * size, GFP_KERNEL);
	if (!bits)
		goto out_nofds;
	fds.in      = (unsigned long *)  bits;
	fds.out     = (unsigned long *) (bits +   size);
	fds.ex      = (unsigned long *) (bits + 2*size);
	fds.res_in  = (unsigned long *) (bits + 3*size);
	fds.res_out = (unsigned long *) (bits + 4*size);
	fds.res_ex  = (unsigned long *) (bits + 5*size);

	nn = (n + 8*sizeof(u32) - 1) / (8*sizeof(u32));
	if ((ret = get_fd_set32(nn, fds.in, inp)) ||
	    (ret = get_fd_set32(nn, fds.out, outp)) ||
	    (ret = get_fd_set32(nn, fds.ex, exp)))
		goto out;
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);

	ret = do_select(n, &fds, &timeout);

	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		time_t sec = 0, usec = 0;
		if (timeout) {
			sec = timeout / HZ;
			usec = timeout % HZ;
			usec *= (1000000/HZ);
		}
		put_user(sec, &tvp->tv_sec);
		put_user(usec, &tvp->tv_usec);
	}

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	set_fd_set32(nn, inp, fds.res_in);
	set_fd_set32(nn, outp, fds.res_out);
	set_fd_set32(nn, exp, fds.res_ex);
  
out:
	kfree(bits);

out_nofds:
	return ret;
}

/* Note: it is necessary to treat n as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage int ppc32_select(u32 n, u32* inp, u32* outp, u32* exp, u32 tvp_x)
{
	return sys32_select((int)n, inp, outp, exp, tvp_x);
}

int cp_compat_stat(struct kstat *stat, struct compat_stat *statbuf)
{
	int err;

	if (stat->size > MAX_NON_LFS)
		return -EOVERFLOW;

	err  = put_user(stat->dev, &statbuf->st_dev);
	err |= put_user(stat->ino, &statbuf->st_ino);
	err |= put_user(stat->mode, &statbuf->st_mode);
	err |= put_user(stat->nlink, &statbuf->st_nlink);
	err |= put_user(stat->uid, &statbuf->st_uid);
	err |= put_user(stat->gid, &statbuf->st_gid);
	err |= put_user(stat->rdev, &statbuf->st_rdev);
	err |= put_user(stat->size, &statbuf->st_size);
	err |= put_user(stat->atime.tv_sec, &statbuf->st_atime);
	err |= put_user(0, &statbuf->__unused1);
	err |= put_user(stat->mtime.tv_sec, &statbuf->st_mtime);
	err |= put_user(0, &statbuf->__unused2);
	err |= put_user(stat->ctime.tv_sec, &statbuf->st_ctime);
	err |= put_user(0, &statbuf->__unused3);
	err |= put_user(stat->blksize, &statbuf->st_blksize);
	err |= put_user(stat->blocks, &statbuf->st_blocks);
	err |= put_user(0, &statbuf->__unused4[0]);
	err |= put_user(0, &statbuf->__unused4[1]);

	return err;
}

extern asmlinkage long sys_sysfs(int option, unsigned long arg1, unsigned long arg2);

/* Note: it is necessary to treat option as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sysfs(u32 option, u32 arg1, u32 arg2)
{
	return sys_sysfs((int)option, arg1, arg2);
}




extern unsigned long do_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr);
                
asmlinkage unsigned long sys32_mremap(unsigned long addr, unsigned long old_len, unsigned long new_len,
                                    	unsigned long flags, u32 __new_addr)
{
	unsigned long ret = -EINVAL;
	unsigned long new_addr = AA(__new_addr);
	
	if (old_len > 0xf0000000UL || new_len > 0xf0000000UL)
		goto out;
	if (addr > 0xf0000000UL - old_len)
		goto out;
	down_write(&current->mm->mmap_sem);
	if (flags & MREMAP_FIXED) {
		if (new_addr > 0xf0000000UL - new_len)
			goto out_sem;
	} else if (addr > 0xf0000000UL - new_len) {
		ret = -ENOMEM;
		if (!(flags & MREMAP_MAYMOVE))
			goto out_sem;
		new_addr = get_unmapped_area (NULL, addr, new_len, 0, 0);
		if (!new_addr)
			goto out_sem;
		flags |= MREMAP_FIXED;
	}
	ret = do_mremap(addr, old_len, new_len, flags, new_addr);
out_sem:
	up_write(&current->mm->mmap_sem);
out:
	return ret;       
}



/* Handle adjtimex compatability. */
struct timex32 {
	u32 modes;
	s32 offset, freq, maxerror, esterror;
	s32 status, constant, precision, tolerance;
	struct compat_timeval time;
	s32 tick;
	s32 ppsfreq, jitter, shift, stabil;
	s32 jitcnt, calcnt, errcnt, stbcnt;
	s32  :32; s32  :32; s32  :32; s32  :32;
	s32  :32; s32  :32; s32  :32; s32  :32;
	s32  :32; s32  :32; s32  :32; s32  :32;
};

extern int do_adjtimex(struct timex *);
extern void ppc_adjtimex(void);

asmlinkage long sys32_adjtimex(struct timex32 *utp)
{
	struct timex txc;
	int ret;
	
	memset(&txc, 0, sizeof(struct timex));

	if(get_user(txc.modes, &utp->modes) ||
	   __get_user(txc.offset, &utp->offset) ||
	   __get_user(txc.freq, &utp->freq) ||
	   __get_user(txc.maxerror, &utp->maxerror) ||
	   __get_user(txc.esterror, &utp->esterror) ||
	   __get_user(txc.status, &utp->status) ||
	   __get_user(txc.constant, &utp->constant) ||
	   __get_user(txc.precision, &utp->precision) ||
	   __get_user(txc.tolerance, &utp->tolerance) ||
	   __get_user(txc.time.tv_sec, &utp->time.tv_sec) ||
	   __get_user(txc.time.tv_usec, &utp->time.tv_usec) ||
	   __get_user(txc.tick, &utp->tick) ||
	   __get_user(txc.ppsfreq, &utp->ppsfreq) ||
	   __get_user(txc.jitter, &utp->jitter) ||
	   __get_user(txc.shift, &utp->shift) ||
	   __get_user(txc.stabil, &utp->stabil) ||
	   __get_user(txc.jitcnt, &utp->jitcnt) ||
	   __get_user(txc.calcnt, &utp->calcnt) ||
	   __get_user(txc.errcnt, &utp->errcnt) ||
	   __get_user(txc.stbcnt, &utp->stbcnt))
		return -EFAULT;

	ret = do_adjtimex(&txc);

	/* adjust the conversion of TB to time of day to track adjtimex */
	ppc_adjtimex();

	if(put_user(txc.modes, &utp->modes) ||
	   __put_user(txc.offset, &utp->offset) ||
	   __put_user(txc.freq, &utp->freq) ||
	   __put_user(txc.maxerror, &utp->maxerror) ||
	   __put_user(txc.esterror, &utp->esterror) ||
	   __put_user(txc.status, &utp->status) ||
	   __put_user(txc.constant, &utp->constant) ||
	   __put_user(txc.precision, &utp->precision) ||
	   __put_user(txc.tolerance, &utp->tolerance) ||
	   __put_user(txc.time.tv_sec, &utp->time.tv_sec) ||
	   __put_user(txc.time.tv_usec, &utp->time.tv_usec) ||
	   __put_user(txc.tick, &utp->tick) ||
	   __put_user(txc.ppsfreq, &utp->ppsfreq) ||
	   __put_user(txc.jitter, &utp->jitter) ||
	   __put_user(txc.shift, &utp->shift) ||
	   __put_user(txc.stabil, &utp->stabil) ||
	   __put_user(txc.jitcnt, &utp->jitcnt) ||
	   __put_user(txc.calcnt, &utp->calcnt) ||
	   __put_user(txc.errcnt, &utp->errcnt) ||
	   __put_user(txc.stbcnt, &utp->stbcnt))
		ret = -EFAULT;

	return ret;
}

#ifdef CONFIG_MODULES

extern asmlinkage long sys_init_module(void *, unsigned long, const char *);

asmlinkage int sys32_init_module(void *umod, u32 len, const char *uargs)
{
	return sys_init_module(umod, len, uargs);
}

extern asmlinkage long sys_delete_module(const char *, unsigned int);

asmlinkage int sys32_delete_module(const char *name_user, unsigned int flags)
{
	return sys_delete_module(name_user, flags);
}

#else /* CONFIG_MODULES */

asmlinkage int
sys32_init_module(const char *name_user, struct module *mod_user)
{
	return -ENOSYS;
}

asmlinkage int
sys32_delete_module(const char *name_user)
{
	return -ENOSYS;
}

#endif  /* CONFIG_MODULES */

/* Stuff for NFS server syscalls... */
struct nfsctl_svc32 {
	u16			svc32_port;
	s32			svc32_nthreads;
};

struct nfsctl_client32 {
	s8			cl32_ident[NFSCLNT_IDMAX+1];
	s32			cl32_naddr;
	struct in_addr		cl32_addrlist[NFSCLNT_ADDRMAX];
	s32			cl32_fhkeytype;
	s32			cl32_fhkeylen;
	u8			cl32_fhkey[NFSCLNT_KEYMAX];
};

struct nfsctl_export32 {
	s8			ex32_client[NFSCLNT_IDMAX+1];
	s8			ex32_path[NFS_MAXPATHLEN+1];
	compat_dev_t	ex32_dev;
	compat_ino_t	ex32_ino;
	s32			ex32_flags;
	compat_uid_t	ex32_anon_uid;
	compat_gid_t	ex32_anon_gid;
};

struct nfsctl_uidmap32 {
	u32			ug32_ident;   /* char * */
	compat_uid_t	ug32_uidbase;
	s32			ug32_uidlen;
	u32			ug32_udimap;  /* uid_t * */
	compat_uid_t	ug32_gidbase;
	s32			ug32_gidlen;
	u32			ug32_gdimap;  /* gid_t * */
};

struct nfsctl_fhparm32 {
	struct sockaddr		gf32_addr;
	compat_dev_t	gf32_dev;
	compat_ino_t	gf32_ino;
	s32			gf32_version;
};

struct nfsctl_fdparm32 {
	struct sockaddr		gd32_addr;
	s8			gd32_path[NFS_MAXPATHLEN+1];
	s32			gd32_version;
};

struct nfsctl_fsparm32 {
	struct sockaddr		gd32_addr;
	s8			gd32_path[NFS_MAXPATHLEN+1];
	s32			gd32_maxlen;
};

struct nfsctl_arg32 {
	s32			ca32_version;	/* safeguard */
	union {
		struct nfsctl_svc32	u32_svc;
		struct nfsctl_client32	u32_client;
		struct nfsctl_export32	u32_export;
		struct nfsctl_uidmap32	u32_umap;
		struct nfsctl_fhparm32	u32_getfh;
		struct nfsctl_fdparm32	u32_getfd;
		struct nfsctl_fsparm32	u32_getfs;
	} u;
#define ca32_svc	u.u32_svc
#define ca32_client	u.u32_client
#define ca32_export	u.u32_export
#define ca32_umap	u.u32_umap
#define ca32_getfh	u.u32_getfh
#define ca32_getfd	u.u32_getfd
#define ca32_getfs	u.u32_getfs
#define ca32_authd	u.u32_authd
};

union nfsctl_res32 {
	__u8			cr32_getfh[NFS_FHSIZE];
	struct knfsd_fh		cr32_getfs;
};

static int nfs_svc32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= __get_user(karg->ca_svc.svc_port, &arg32->ca32_svc.svc32_port);
	err |= __get_user(karg->ca_svc.svc_nthreads, &arg32->ca32_svc.svc32_nthreads);
	return err;
}

static int nfs_clnt32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_client.cl_ident[0],
			  &arg32->ca32_client.cl32_ident[0],
			  NFSCLNT_IDMAX);
	err |= __get_user(karg->ca_client.cl_naddr, &arg32->ca32_client.cl32_naddr);
	err |= copy_from_user(&karg->ca_client.cl_addrlist[0],
			  &arg32->ca32_client.cl32_addrlist[0],
			  (sizeof(struct in_addr) * NFSCLNT_ADDRMAX));
	err |= __get_user(karg->ca_client.cl_fhkeytype,
		      &arg32->ca32_client.cl32_fhkeytype);
	err |= __get_user(karg->ca_client.cl_fhkeylen,
		      &arg32->ca32_client.cl32_fhkeylen);
	err |= copy_from_user(&karg->ca_client.cl_fhkey[0],
			  &arg32->ca32_client.cl32_fhkey[0],
			  NFSCLNT_KEYMAX);

	if(err) return -EFAULT;
	return 0;
}

static int nfs_exp32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_export.ex_client[0],
			  &arg32->ca32_export.ex32_client[0],
			  NFSCLNT_IDMAX);
	err |= copy_from_user(&karg->ca_export.ex_path[0],
			  &arg32->ca32_export.ex32_path[0],
			  NFS_MAXPATHLEN);
	err |= __get_user(karg->ca_export.ex_dev,
		      &arg32->ca32_export.ex32_dev);
	err |= __get_user(karg->ca_export.ex_ino,
		      &arg32->ca32_export.ex32_ino);
	err |= __get_user(karg->ca_export.ex_flags,
		      &arg32->ca32_export.ex32_flags);
	err |= __get_user(karg->ca_export.ex_anon_uid,
		      &arg32->ca32_export.ex32_anon_uid);
	err |= __get_user(karg->ca_export.ex_anon_gid,
		      &arg32->ca32_export.ex32_anon_gid);
	karg->ca_export.ex_anon_uid = karg->ca_export.ex_anon_uid;
	karg->ca_export.ex_anon_gid = karg->ca_export.ex_anon_gid;

	if(err) return -EFAULT;
	return 0;
}

static int nfs_uud32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	u32 uaddr;
	int i;
	int err;

	memset(karg, 0, sizeof(*karg));
	if(__get_user(karg->ca_version, &arg32->ca32_version))
		return -EFAULT;
	karg->ca_umap.ug_ident = (char *)get_zeroed_page(GFP_USER);
	if(!karg->ca_umap.ug_ident)
		return -ENOMEM;
	err = __get_user(uaddr, &arg32->ca32_umap.ug32_ident);
	if(strncpy_from_user(karg->ca_umap.ug_ident,
			     (char *)A(uaddr), PAGE_SIZE) <= 0)
		return -EFAULT;
	err |= __get_user(karg->ca_umap.ug_uidbase,
		      &arg32->ca32_umap.ug32_uidbase);
	err |= __get_user(karg->ca_umap.ug_uidlen,
		      &arg32->ca32_umap.ug32_uidlen);
	err |= __get_user(uaddr, &arg32->ca32_umap.ug32_udimap);
	if (err)
		return -EFAULT;
	karg->ca_umap.ug_udimap = kmalloc((sizeof(uid_t) * karg->ca_umap.ug_uidlen),
					  GFP_USER);
	if(!karg->ca_umap.ug_udimap)
		return -ENOMEM;
	for(i = 0; i < karg->ca_umap.ug_uidlen; i++)
		err |= __get_user(karg->ca_umap.ug_udimap[i],
			      &(((compat_uid_t *)A(uaddr))[i]));
	err |= __get_user(karg->ca_umap.ug_gidbase,
		      &arg32->ca32_umap.ug32_gidbase);
	err |= __get_user(karg->ca_umap.ug_uidlen,
		      &arg32->ca32_umap.ug32_gidlen);
	err |= __get_user(uaddr, &arg32->ca32_umap.ug32_gdimap);
	if (err)
		return -EFAULT;
	karg->ca_umap.ug_gdimap = kmalloc((sizeof(gid_t) * karg->ca_umap.ug_uidlen),
					  GFP_USER);
	if(!karg->ca_umap.ug_gdimap)
		return -ENOMEM;
	for(i = 0; i < karg->ca_umap.ug_gidlen; i++)
		err |= __get_user(karg->ca_umap.ug_gdimap[i],
			      &(((compat_gid_t *)A(uaddr))[i]));

	return err;
}

static int nfs_getfh32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfh.gf_addr,
			  &arg32->ca32_getfh.gf32_addr,
			  (sizeof(struct sockaddr)));
	err |= __get_user(karg->ca_getfh.gf_dev,
		      &arg32->ca32_getfh.gf32_dev);
	err |= __get_user(karg->ca_getfh.gf_ino,
		      &arg32->ca32_getfh.gf32_ino);
	err |= __get_user(karg->ca_getfh.gf_version,
		      &arg32->ca32_getfh.gf32_version);

	if(err) return -EFAULT;
	return 0;
}

static int nfs_getfd32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfd.gd_addr,
			  &arg32->ca32_getfd.gd32_addr,
			  (sizeof(struct sockaddr)));
	err |= copy_from_user(&karg->ca_getfd.gd_path,
			  &arg32->ca32_getfd.gd32_path,
			  (NFS_MAXPATHLEN+1));
	err |= __get_user(karg->ca_getfd.gd_version,
		      &arg32->ca32_getfd.gd32_version);

	if(err) return -EFAULT;
	return 0;
}

static int nfs_getfs32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfs.gd_addr,
			  &arg32->ca32_getfs.gd32_addr,
			  (sizeof(struct sockaddr)));
	err |= copy_from_user(&karg->ca_getfs.gd_path,
			  &arg32->ca32_getfs.gd32_path,
			  (NFS_MAXPATHLEN+1));
	err |= __get_user(karg->ca_getfs.gd_maxlen,
		      &arg32->ca32_getfs.gd32_maxlen);

	if(err) return -EFAULT;
	return 0;
}

/* This really doesn't need translations, we are only passing
 * back a union which contains opaque nfs file handle data.
 */
static int nfs_getfh32_res_trans(union nfsctl_res *kres, union nfsctl_res32 *res32)
{
	int err;

	err = copy_to_user(res32, kres, sizeof(*res32));

	if(err) return -EFAULT;
	return 0;
}

/* Note: it is necessary to treat cmd_parm as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
int asmlinkage sys32_nfsservctl(u32 cmd_parm, struct nfsctl_arg32 *arg32, union nfsctl_res32 *res32)
{
  int cmd = (int)cmd_parm;
	struct nfsctl_arg *karg = NULL;
	union nfsctl_res *kres = NULL;
	mm_segment_t oldfs;
	int err;

	karg = kmalloc(sizeof(*karg), GFP_USER);
	if(!karg)
		return -ENOMEM;
	if(res32) {
		kres = kmalloc(sizeof(*kres), GFP_USER);
		if(!kres) {
			kfree(karg);
			return -ENOMEM;
		}
	}
	switch(cmd) {
	case NFSCTL_SVC:
		err = nfs_svc32_trans(karg, arg32);
		break;
	case NFSCTL_ADDCLIENT:
		err = nfs_clnt32_trans(karg, arg32);
		break;
	case NFSCTL_DELCLIENT:
		err = nfs_clnt32_trans(karg, arg32);
		break;
	case NFSCTL_EXPORT:
	case NFSCTL_UNEXPORT:
		err = nfs_exp32_trans(karg, arg32);
		break;
	/* This one is unimplemented, be we're ready for it. */
	case NFSCTL_UGIDUPDATE:
		err = nfs_uud32_trans(karg, arg32);
		break;
	case NFSCTL_GETFH:
		err = nfs_getfh32_trans(karg, arg32);
		break;
	case NFSCTL_GETFD:
		err = nfs_getfd32_trans(karg, arg32);
		break;
	case NFSCTL_GETFS:
		err = nfs_getfs32_trans(karg, arg32);
		break;
	default:
		err = -EINVAL;
		break;
	}
	if(err)
		goto done;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_nfsservctl(cmd, karg, kres);
	set_fs(oldfs);

	if (err)
		goto done;

	if((cmd == NFSCTL_GETFH) ||
	   (cmd == NFSCTL_GETFD) ||
	   (cmd == NFSCTL_GETFS))
		err = nfs_getfh32_res_trans(kres, res32);

done:
	if(karg) {
		if(cmd == NFSCTL_UGIDUPDATE) {
			if(karg->ca_umap.ug_ident)
				kfree(karg->ca_umap.ug_ident);
			if(karg->ca_umap.ug_udimap)
				kfree(karg->ca_umap.ug_udimap);
			if(karg->ca_umap.ug_gdimap)
				kfree(karg->ca_umap.ug_gdimap);
		}
		kfree(karg);
	}
	if(kres)
		kfree(kres);
	return err;
}



/* These are here just in case some old sparc32 binary calls it. */
asmlinkage long sys32_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	
	return -ERESTARTNOHAND;
}



static inline long get_tv32(struct timeval *o, struct compat_timeval *i)
{
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->tv_sec, &i->tv_sec) |
		 __get_user(o->tv_usec, &i->tv_usec)));
}

static inline long put_tv32(struct compat_timeval *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) |
		 __put_user(i->tv_usec, &o->tv_usec)));
}




#define RLIM_INFINITY32	0xffffffff
#define RESOURCE32(x) ((x > RLIM_INFINITY32) ? RLIM_INFINITY32 : x)

struct rlimit32 {
	u32	rlim_cur;
	u32	rlim_max;
};

extern asmlinkage long sys_getrlimit(unsigned int resource, struct rlimit *rlim);
asmlinkage long sys32_getrlimit(unsigned int resource, struct rlimit32 *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_getrlimit(resource, &r);
	set_fs(old_fs);
	if (!ret) {
		ret = put_user(RESOURCE32(r.rlim_cur), &rlim->rlim_cur);
		ret |= __put_user(RESOURCE32(r.rlim_max), &rlim->rlim_max);
	}
	
	return ret;
}

/* Back compatibility for getrlimit. Needed for some apps. */
asmlinkage long sys32_old_getrlimit(unsigned int resource, struct rlimit32* rlim)
{
	struct rlimit   x;    // 64-bit version of the resource limits.
	struct rlimit32 x32;  // 32-bit version of the resource limits.

	if (resource >= RLIM_NLIMITS)
		return -EINVAL;

	memcpy(&x, current->rlim+resource, sizeof(struct rlimit));

	if (x.rlim_cur > RLIM_INFINITY32)
		x32.rlim_cur = RLIM_INFINITY32;
	else
		x32.rlim_cur = x.rlim_cur;

	if (x.rlim_max > RLIM_INFINITY32)
		x32.rlim_max = RLIM_INFINITY32;
	else
		x32.rlim_max = x.rlim_max;

	return (copy_to_user(rlim, &x32, sizeof(x32))) ? (-EFAULT) : 0;
}

extern asmlinkage long sys_setrlimit(unsigned int resource, struct rlimit *rlim);
asmlinkage long sys32_setrlimit(unsigned int resource, struct rlimit32 *rlim)
{
	struct rlimit r;
	long ret;
	mm_segment_t old_fs = get_fs ();
	
	if (resource >= RLIM_NLIMITS) return -EINVAL;	
	if (get_user (r.rlim_cur, &rlim->rlim_cur) ||
	    __get_user (r.rlim_max, &rlim->rlim_max))
		return -EFAULT;
	if (r.rlim_cur >= RLIM_INFINITY32)
		r.rlim_cur = RLIM_INFINITY;
	if (r.rlim_max >= RLIM_INFINITY32)
		r.rlim_max = RLIM_INFINITY;
	set_fs (KERNEL_DS);
	ret = sys_setrlimit(resource, &r);
	set_fs (old_fs);
	
	return ret;
}


struct rusage32 {
        struct compat_timeval ru_utime;
        struct compat_timeval ru_stime;
        s32    ru_maxrss;
        s32    ru_ixrss;
        s32    ru_idrss;
        s32    ru_isrss;
        s32    ru_minflt;
        s32    ru_majflt;
        s32    ru_nswap;
        s32    ru_inblock;
        s32    ru_oublock;
        s32    ru_msgsnd; 
        s32    ru_msgrcv; 
        s32    ru_nsignals;
        s32    ru_nvcsw;
        s32    ru_nivcsw;
};

static int put_rusage (struct rusage32 *ru, struct rusage *r)
{
	int err;
	
	err = put_user (r->ru_utime.tv_sec, &ru->ru_utime.tv_sec);
	err |= __put_user (r->ru_utime.tv_usec, &ru->ru_utime.tv_usec);
	err |= __put_user (r->ru_stime.tv_sec, &ru->ru_stime.tv_sec);
	err |= __put_user (r->ru_stime.tv_usec, &ru->ru_stime.tv_usec);
	err |= __put_user (r->ru_maxrss, &ru->ru_maxrss);
	err |= __put_user (r->ru_ixrss, &ru->ru_ixrss);
	err |= __put_user (r->ru_idrss, &ru->ru_idrss);
	err |= __put_user (r->ru_isrss, &ru->ru_isrss);
	err |= __put_user (r->ru_minflt, &ru->ru_minflt);
	err |= __put_user (r->ru_majflt, &ru->ru_majflt);
	err |= __put_user (r->ru_nswap, &ru->ru_nswap);
	err |= __put_user (r->ru_inblock, &ru->ru_inblock);
	err |= __put_user (r->ru_oublock, &ru->ru_oublock);
	err |= __put_user (r->ru_msgsnd, &ru->ru_msgsnd);
	err |= __put_user (r->ru_msgrcv, &ru->ru_msgrcv);
	err |= __put_user (r->ru_nsignals, &ru->ru_nsignals);
	err |= __put_user (r->ru_nvcsw, &ru->ru_nvcsw);
	err |= __put_user (r->ru_nivcsw, &ru->ru_nivcsw);
	return err;
}


extern asmlinkage long sys_getrusage(int who, struct rusage *ru);

/* Note: it is necessary to treat who as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getrusage(u32 who, struct rusage32 *ru)
{
	struct rusage r;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_getrusage((int)who, &r);
	set_fs (old_fs);
	if (put_rusage (ru, &r)) 
		return -EFAULT;

	return ret;
}




struct sysinfo32 {
        s32 uptime;
        u32 loads[3];
        u32 totalram;
        u32 freeram;
        u32 sharedram;
        u32 bufferram;
        u32 totalswap;
        u32 freeswap;
        unsigned short procs;
        char _f[22];
};

extern asmlinkage long sys_sysinfo(struct sysinfo *info);

asmlinkage long sys32_sysinfo(struct sysinfo32 *info)
{
	struct sysinfo s;
	int ret, err;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sysinfo(&s);
	set_fs (old_fs);
	err = put_user (s.uptime, &info->uptime);
	err |= __put_user (s.loads[0], &info->loads[0]);
	err |= __put_user (s.loads[1], &info->loads[1]);
	err |= __put_user (s.loads[2], &info->loads[2]);
	err |= __put_user (s.totalram, &info->totalram);
	err |= __put_user (s.freeram, &info->freeram);
	err |= __put_user (s.sharedram, &info->sharedram);
	err |= __put_user (s.bufferram, &info->bufferram);
	err |= __put_user (s.totalswap, &info->totalswap);
	err |= __put_user (s.freeswap, &info->freeswap);
	err |= __put_user (s.procs, &info->procs);
	if (err)
		return -EFAULT;
	
	return ret;
}




/* Translations due to time_t size differences.  Which affects all
   sorts of things, like timeval and itimerval.  */
extern struct timezone sys_tz;
extern int do_sys_settimeofday(struct timeval *tv, struct timezone *tz);

asmlinkage long sys32_gettimeofday(struct compat_timeval *tv, struct timezone *tz)
{
	if (tv) {
		struct timeval ktv;
		do_gettimeofday(&ktv);
		if (put_tv32(tv, &ktv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}
	
	return 0;
}



asmlinkage long sys32_settimeofday(struct compat_timeval *tv, struct timezone *tz)
{
	struct timeval ktv;
	struct timezone ktz;
	
 	if (tv) {
		if (get_tv32(&ktv, tv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &ktv : NULL, tz ? &ktz : NULL);
}


struct msgbuf32 { s32 mtype; char mtext[1]; };

struct semid_ds32 {
	struct ipc_perm sem_perm;
	compat_time_t sem_otime;
	compat_time_t sem_ctime;
	u32 sem_base;
	u32 sem_pending;
	u32 sem_pending_last;
	u32 undo;
	unsigned short sem_nsems;
};

struct semid64_ds32 {
	struct ipc64_perm sem_perm;
	unsigned int __unused1;
	compat_time_t sem_otime;
	unsigned int __unused2;
	compat_time_t sem_ctime;
	u32 sem_nsems;
	u32 __unused3;
	u32 __unused4;
};

struct msqid_ds32
{
	struct ipc_perm msg_perm;
	u32 msg_first;
	u32 msg_last;
	compat_time_t msg_stime;
	compat_time_t msg_rtime;
	compat_time_t msg_ctime;
	u32 msg_lcbytes;
	u32 msg_lqbytes;
	unsigned short msg_cbytes;
	unsigned short msg_qnum;
	unsigned short msg_qbytes;
	compat_ipc_pid_t msg_lspid;
	compat_ipc_pid_t msg_lrpid;
};

struct msqid64_ds32 {
	struct ipc64_perm msg_perm;
	unsigned int __unused1;
	compat_time_t msg_stime;
	unsigned int __unused2;
	compat_time_t msg_rtime;
	unsigned int __unused3;
	compat_time_t msg_ctime;
	unsigned int msg_cbytes;
	unsigned int msg_qnum;
	unsigned int msg_qbytes;
	compat_pid_t msg_lspid;
	compat_pid_t msg_lrpid;
	unsigned int __unused4;
	unsigned int __unused5;
};

struct shmid_ds32 {
	struct ipc_perm shm_perm;
	int shm_segsz;
	compat_time_t shm_atime;
	compat_time_t shm_dtime;
	compat_time_t shm_ctime;
	compat_ipc_pid_t shm_cpid;
	compat_ipc_pid_t shm_lpid;
	unsigned short shm_nattch;
	unsigned short __unused;
	unsigned int __unused2;
	unsigned int __unused3;
};

struct shmid64_ds32 {
	struct ipc64_perm shm_perm;
	unsigned int __unused1;
	compat_time_t shm_atime;
	unsigned int __unused2;
	compat_time_t shm_dtime;
	unsigned int __unused3;
	compat_time_t shm_ctime;
	unsigned int __unused4;
	compat_size_t shm_segsz;
	compat_pid_t shm_cpid;
	compat_pid_t shm_lpid;
	unsigned int shm_nattch;
	unsigned int __unused5;
	unsigned int __unused6;
};

/*
 * sys32_ipc() is the de-multiplexer for the SysV IPC calls in 32bit
 * emulation..
 *
 * This is really horribly ugly.
 */
static long do_sys32_semctl(int first, int second, int third, void *uptr)
{
	union semun fourth;
	u32 pad;
	int err, err2;
	mm_segment_t old_fs;

	if (!uptr)
		return -EINVAL;
	err = -EFAULT;
	if (get_user(pad, (u32 *)uptr))
		return err;
	if (third == SETVAL)
		fourth.val = (int)pad;
	else
		fourth.__pad = (void *)A(pad);
	switch (third & (~IPC_64)) {

	case IPC_INFO:
	case IPC_RMID:
	case SEM_INFO:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
	case SETALL:
	case SETVAL:
		err = sys_semctl(first, second, third, fourth);
		break;

	case IPC_STAT:
	case SEM_STAT:
		if (third & IPC_64) {
			struct semid64_ds s64;
			struct semid64_ds32 *usp;

			usp = (struct semid64_ds32 *)A(pad);
			fourth.__pad = &s64;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);
			err2 = copy_to_user(&usp->sem_perm, &s64.sem_perm,
					    sizeof(struct ipc64_perm));
			err2 |= __put_user(s64.sem_otime, &usp->sem_otime);
			err2 |= __put_user(s64.sem_ctime, &usp->sem_ctime);
			err2 |= __put_user(s64.sem_nsems, &usp->sem_nsems);
			if (err2)
				err = -EFAULT;
		} else {
			struct semid_ds s;
			struct semid_ds32 *usp;

			usp = (struct semid_ds32 *)A(pad);
			fourth.__pad = &s;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);
			err2 = copy_to_user(&usp->sem_perm, &s.sem_perm,
					    sizeof(struct ipc_perm));
			err2 |= __put_user(s.sem_otime, &usp->sem_otime);
			err2 |= __put_user(s.sem_ctime, &usp->sem_ctime);
			err2 |= __put_user(s.sem_nsems, &usp->sem_nsems);
			if (err2)
				err = -EFAULT;
		}
		break;
 
	case IPC_SET:
		if (third & IPC_64) {
			struct semid64_ds s64;
			struct semid64_ds32 *usp;

			usp = (struct semid64_ds32 *)A(pad);

			err = get_user(s64.sem_perm.uid, &usp->sem_perm.uid);
			err |= __get_user(s64.sem_perm.gid,
					  &usp->sem_perm.gid);
			err |= __get_user(s64.sem_perm.mode,
					  &usp->sem_perm.mode);
			if (err)
				goto out;
			fourth.__pad = &s64;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);

		} else {
			struct semid_ds s;
			struct semid_ds32 *usp;

			usp = (struct semid_ds32 *)A(pad);

			err = get_user(s.sem_perm.uid, &usp->sem_perm.uid);
			err |= __get_user(s.sem_perm.gid,
					  &usp->sem_perm.gid);
			err |= __get_user(s.sem_perm.mode,
					  &usp->sem_perm.mode);
			if (err)
				goto out;
			fourth.__pad = &s;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);
		}
		break;
	default:
		err = -EINVAL;
	}
out:
	return err;
}

#define MAXBUF (64*1024)

static int 
do_sys32_msgsnd(int first, int second, int third, void *uptr)
{
	struct msgbuf *p;
	struct msgbuf32 *up = (struct msgbuf32 *)uptr;
	mm_segment_t old_fs;
	int err;

	if (second < 0 || (second >= MAXBUF-sizeof(struct msgbuf)))
		return -EINVAL;

	p = kmalloc(second + sizeof(struct msgbuf), GFP_USER);
	if (!p)
		return -ENOMEM;
	err = get_user(p->mtype, &up->mtype);
	err |= copy_from_user(p->mtext, &up->mtext, second);
	if (err) {
		err = -EFAULT;
		goto out;
	}
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_msgsnd(first, p, second, third);
	set_fs(old_fs);
out:
	kfree(p);
	return err;
}

static int
do_sys32_msgrcv(int first, int second, int msgtyp, int third,
		int version, void *uptr)
{
	struct msgbuf32 *up;
	struct msgbuf *p;
	mm_segment_t old_fs;
	int err;

	if (second < 0 || (second >= MAXBUF-sizeof(struct msgbuf)))
		return -EINVAL;

	if (!version) {
		struct ipc_kludge_32 *uipck = (struct ipc_kludge_32 *)uptr;
		struct ipc_kludge_32 ipck;

		err = -EINVAL;
		if (!uptr)
			goto out;
		err = -EFAULT;
		if (copy_from_user(&ipck, uipck, sizeof(struct ipc_kludge_32)))
			goto out;
		uptr = (void *)A(ipck.msgp);
		msgtyp = ipck.msgtyp;
	}
	err = -ENOMEM;
	p = kmalloc(second + sizeof (struct msgbuf), GFP_USER);
	if (!p)
		goto out;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_msgrcv(first, p, second, msgtyp, third);
	set_fs(old_fs);
	if (err < 0)
		goto free_then_out;
	up = (struct msgbuf32 *)uptr;
	if (put_user(p->mtype, &up->mtype) ||
	    copy_to_user(&up->mtext, p->mtext, err))
		err = -EFAULT;
free_then_out:
	kfree(p);
out:
	return err;
}

static int
do_sys32_msgctl(int first, int second, void *uptr)
{
	int err = -EINVAL, err2;
	mm_segment_t old_fs;

	switch (second & (~IPC_64)) {

	case IPC_INFO:
	case IPC_RMID:
	case MSG_INFO:
		err = sys_msgctl(first, second, (struct msqid_ds *)uptr);
		break;

	case IPC_SET:
		if (second & IPC_64) {
			struct msqid64_ds m64;
			struct msqid64_ds32 *up = (struct msqid64_ds32 *)uptr;

			err2 = copy_from_user(&m64.msg_perm, &up->msg_perm,
					      sizeof(struct ipc64_perm));
			err2 |= __get_user(m64.msg_qbytes, &up->msg_qbytes);
			if (err2) {
				err = -EFAULT;
				break;
			}
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second,
					 (struct msqid_ds *)&m64);
			set_fs(old_fs);
		} else {
			struct msqid_ds m;
			struct msqid_ds32 *up = (struct msqid_ds32 *)uptr;

			err2 = copy_from_user(&m.msg_perm, &up->msg_perm,
					      sizeof(struct ipc_perm));
			err2 |= __get_user(m.msg_qbytes, &up->msg_qbytes);
			if (err2) {
				err = -EFAULT;
				break;
			}
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second, &m);
			set_fs(old_fs);
		}
		break;

	case IPC_STAT:
	case MSG_STAT:
		if (second & IPC_64) {
			struct msqid64_ds m64;
			struct msqid64_ds32 *up = (struct msqid64_ds32 *)uptr;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second,
					 (struct msqid_ds *)&m64);
			set_fs(old_fs);

			err2 = copy_to_user(&up->msg_perm, &m64.msg_perm,
					    sizeof(struct ipc64_perm));
 			err2 |= __put_user(m64.msg_stime, &up->msg_stime);
			err2 |= __put_user(m64.msg_rtime, &up->msg_rtime);
			err2 |= __put_user(m64.msg_ctime, &up->msg_ctime);
			err2 |= __put_user(m64.msg_cbytes, &up->msg_cbytes);
			err2 |= __put_user(m64.msg_qnum, &up->msg_qnum);
			err2 |= __put_user(m64.msg_qbytes, &up->msg_qbytes);
			err2 |= __put_user(m64.msg_lspid, &up->msg_lspid);
			err2 |= __put_user(m64.msg_lrpid, &up->msg_lrpid);
			if (err2)
				err = -EFAULT;
		} else {
			struct msqid64_ds m;
			struct msqid_ds32 *up = (struct msqid_ds32 *)uptr;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second, (struct msqid_ds *)&m);
			set_fs(old_fs);

			err2 = copy_to_user(&up->msg_perm, &m.msg_perm,
					    sizeof(struct ipc_perm));
 			err2 |= __put_user(m.msg_stime, &up->msg_stime);
			err2 |= __put_user(m.msg_rtime, &up->msg_rtime);
			err2 |= __put_user(m.msg_ctime, &up->msg_ctime);
			err2 |= __put_user(m.msg_cbytes, &up->msg_cbytes);
			err2 |= __put_user(m.msg_qnum, &up->msg_qnum);
			err2 |= __put_user(m.msg_qbytes, &up->msg_qbytes);
			err2 |= __put_user(m.msg_lspid, &up->msg_lspid);
			err2 |= __put_user(m.msg_lrpid, &up->msg_lrpid);
			if (err2)
				err = -EFAULT;
		}
		break;
	}
	return err;
}

static int
do_sys32_shmat(int first, int second, int third, int version, void *uptr)
{
	unsigned long raddr;
	u32 *uaddr = (u32 *)A((u32)third);
	int err = -EINVAL;

	if (version == 1)
		return err;
	err = sys_shmat(first, uptr, second, &raddr);
	if (err)
		return err;
	err = put_user(raddr, uaddr);
	return err;
}

static int
do_sys32_shmctl(int first, int second, void *uptr)
{
	int err = -EINVAL, err2;
	mm_segment_t old_fs;

	switch (second & (~IPC_64)) {

	case IPC_INFO:
	case IPC_RMID:
	case SHM_LOCK:
	case SHM_UNLOCK:
		err = sys_shmctl(first, second, (struct shmid_ds *)uptr);
		break;
	case IPC_SET:
		if (second & IPC_64) {
			struct shmid64_ds32 *up = (struct shmid64_ds32 *)uptr;
			struct shmid64_ds s64;

			err = get_user(s64.shm_perm.uid, &up->shm_perm.uid);
			err |= __get_user(s64.shm_perm.gid, &up->shm_perm.gid);
			err |= __get_user(s64.shm_perm.mode,
					  &up->shm_perm.mode);
			if (err)
				break;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second,
					 (struct shmid_ds *)&s64);
			set_fs(old_fs);
		} else {
			struct shmid_ds32 *up = (struct shmid_ds32 *)uptr;
			struct shmid_ds s;

			err = get_user(s.shm_perm.uid, &up->shm_perm.uid);
			err |= __get_user(s.shm_perm.gid, &up->shm_perm.gid);
			err |= __get_user(s.shm_perm.mode, &up->shm_perm.mode);
			if (err)
				break;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second, &s);
			set_fs(old_fs);
		}
		break;

	case IPC_STAT:
	case SHM_STAT:
		if (second & IPC_64) {
			struct shmid64_ds32 *up = (struct shmid64_ds32 *)uptr;
			struct shmid64_ds s64;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second,
					 (struct shmid_ds *)&s64);
			set_fs(old_fs);
			if (err < 0)
				break;

			err2 = copy_to_user(&up->shm_perm, &s64.shm_perm,
					    sizeof(struct ipc64_perm));
			err2 |= __put_user(s64.shm_atime, &up->shm_atime);
			err2 |= __put_user(s64.shm_dtime, &up->shm_dtime);
			err2 |= __put_user(s64.shm_ctime, &up->shm_ctime);
			err2 |= __put_user(s64.shm_segsz, &up->shm_segsz);
			err2 |= __put_user(s64.shm_nattch, &up->shm_nattch);
			err2 |= __put_user(s64.shm_cpid, &up->shm_cpid);
			err2 |= __put_user(s64.shm_lpid, &up->shm_lpid);
			if (err2)
				err = -EFAULT;
		} else {
			struct shmid_ds32 *up = (struct shmid_ds32 *)uptr;
			struct shmid_ds s;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second, &s);
			set_fs(old_fs);
			if (err < 0)
				break;

			err2 = copy_to_user(&up->shm_perm, &s.shm_perm,
					    sizeof(struct ipc_perm));
			err2 |= __put_user (s.shm_atime, &up->shm_atime);
			err2 |= __put_user (s.shm_dtime, &up->shm_dtime);
			err2 |= __put_user (s.shm_ctime, &up->shm_ctime);
			err2 |= __put_user (s.shm_segsz, &up->shm_segsz);
			err2 |= __put_user (s.shm_nattch, &up->shm_nattch);
			err2 |= __put_user (s.shm_cpid, &up->shm_cpid);
			err2 |= __put_user (s.shm_lpid, &up->shm_lpid);
			if (err2)
				err = -EFAULT;
		}
		break;

	case SHM_INFO: {
		struct shm_info si;
		struct shm_info32 {
			int used_ids;
			u32 shm_tot, shm_rss, shm_swp;
			u32 swap_attempts, swap_successes;
		} *uip = (struct shm_info32 *)uptr;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_shmctl(first, second, (struct shmid_ds *)&si);
		set_fs(old_fs);
		if (err < 0)
			break;
		err2 = put_user(si.used_ids, &uip->used_ids);
		err2 |= __put_user(si.shm_tot, &uip->shm_tot);
		err2 |= __put_user(si.shm_rss, &uip->shm_rss);
		err2 |= __put_user(si.shm_swp, &uip->shm_swp);
		err2 |= __put_user(si.swap_attempts, &uip->swap_attempts);
		err2 |= __put_user(si.swap_successes, &uip->swap_successes);
		if (err2)
			err = -EFAULT;
		break;
	}
	}
	return err;
}

/*
 * Note: it is necessary to treat first_parm, second_parm, and
 * third_parm as unsigned ints, with the corresponding cast to a
 * signed int to insure that the proper conversion (sign extension)
 * between the register representation of a signed int (msr in 32-bit
 * mode) and the register representation of a signed int (msr in
 * 64-bit mode) is performed.
 */
asmlinkage long sys32_ipc(u32 call, u32 first_parm, u32 second_parm, u32 third_parm, u32 ptr, u32 fifth)
{
	int first  = (int)first_parm;
	int second = (int)second_parm;
	int third  = (int)third_parm;
	int version, err;
	
	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {

	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		err = sys_semop(first, (struct sembuf *)AA(ptr),
				second);
		break;
	case SEMGET:
		err = sys_semget(first, second, third);
		break;
	case SEMCTL:
		err = do_sys32_semctl(first, second, third,
				      (void *)AA(ptr));
		break;

	case MSGSND:
		err = do_sys32_msgsnd(first, second, third,
				      (void *)AA(ptr));
		break;
	case MSGRCV:
		err = do_sys32_msgrcv(first, second, fifth, third,
				      version, (void *)AA(ptr));
		break;
	case MSGGET:
		err = sys_msgget((key_t)first, second);
		break;
	case MSGCTL:
		err = do_sys32_msgctl(first, second, (void *)AA(ptr));
		break;

	case SHMAT:
		err = do_sys32_shmat(first, second, third,
				     version, (void *)AA(ptr));
		break;
	case SHMDT: 
		err = sys_shmdt((char *)AA(ptr));
		break;
	case SHMGET:
		err = sys_shmget(first, second, third);
		break;
	case SHMCTL:
		err = do_sys32_shmctl(first, second, (void *)AA(ptr));
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

extern asmlinkage ssize_t sys_sendfile(int out_fd, int in_fd, off_t* offset, size_t count);

/* Note: it is necessary to treat out_fd and in_fd as unsigned ints, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sendfile(u32 out_fd, u32 in_fd, compat_off_t* offset, u32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;
	
	if (offset && get_user(of, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile((int)out_fd, (int)in_fd, offset ? &of : NULL, count);
	set_fs(old_fs);
	
	if (offset && put_user(of, offset))
		return -EFAULT;
		
	return ret;
}

extern asmlinkage ssize_t sys_sendfile64(int out_fd, int in_fd, loff_t *offset, size_t count);

asmlinkage int sys32_sendfile64(int out_fd, int in_fd, compat_loff_t *offset, s32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	loff_t lof;
	
	if (offset && get_user(lof, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile64(out_fd, in_fd, offset ? &lof : NULL, count);
	set_fs(old_fs);
	
	if (offset && put_user(lof, offset))
		return -EFAULT;
		
	return ret;
}

extern asmlinkage int sys_setsockopt(int fd, int level, int optname, char *optval, int optlen);

static int do_set_attach_filter(int fd, int level, int optname,
				char *optval, int optlen)
{
	struct sock_fprog32 {
		__u16 len;
		__u32 filter;
	} *fprog32 = (struct sock_fprog32 *)optval;
	struct sock_fprog kfprog;
	struct sock_filter *kfilter;
	unsigned int fsize;
	mm_segment_t old_fs;
	__u32 uptr;
	int ret;

	if (get_user(kfprog.len, &fprog32->len) ||
	    __get_user(uptr, &fprog32->filter))
		return -EFAULT;

	kfprog.filter = (struct sock_filter *)A(uptr);
	fsize = kfprog.len * sizeof(struct sock_filter);

	kfilter = (struct sock_filter *)kmalloc(fsize, GFP_KERNEL);
	if (kfilter == NULL)
		return -ENOMEM;

	if (copy_from_user(kfilter, kfprog.filter, fsize)) {
		kfree(kfilter);
		return -EFAULT;
	}

	kfprog.filter = kfilter;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_setsockopt(fd, level, optname,
			     (char *)&kfprog, sizeof(kfprog));
	set_fs(old_fs);

	kfree(kfilter);

	return ret;
}

static int do_set_icmpv6_filter(int fd, int level, int optname,
				char *optval, int optlen)
{
	struct icmp6_filter kfilter;
	mm_segment_t old_fs;
	int ret, i;

	if (copy_from_user(&kfilter, optval, sizeof(kfilter)))
		return -EFAULT;


	for (i = 0; i < 8; i += 2) {
		u32 tmp = kfilter.data[i];

		kfilter.data[i] = kfilter.data[i + 1];
		kfilter.data[i + 1] = tmp;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_setsockopt(fd, level, optname,
			     (char *) &kfilter, sizeof(kfilter));
	set_fs(old_fs);

	return ret;
}

static int do_set_sock_timeout(int fd, int level, int optname, char *optval, int optlen)
{
	struct compat_timeval *up = (struct compat_timeval *) optval;
	struct timeval ktime;
	mm_segment_t old_fs;
	int err;

	if (optlen < sizeof(*up))
		return -EINVAL;
	if (get_user(ktime.tv_sec, &up->tv_sec) ||
	    __get_user(ktime.tv_usec, &up->tv_usec))
		return -EFAULT;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_setsockopt(fd, level, optname, (char *) &ktime, sizeof(ktime));
	set_fs(old_fs);

	return err;
}

asmlinkage long sys32_setsockopt(int fd, int level, int optname, char* optval, int optlen)
{
	if (optname == SO_ATTACH_FILTER)
		return do_set_attach_filter(fd, level, optname,
					    optval, optlen);
	if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO)
		return do_set_sock_timeout(fd, level, optname, optval, optlen);
	if (level == SOL_ICMPV6 && optname == ICMPV6_FILTER)
		return do_set_icmpv6_filter(fd, level, optname,
					    optval, optlen);

	return sys_setsockopt(fd, level, optname, optval, optlen);
}

extern asmlinkage long sys_getsockopt(int fd, int level, int optname,
				      char *optval, int *optlen);

static int do_get_sock_timeout(int fd, int level, int optname, char *optval, int *optlen)
{
	struct compat_timeval *up = (struct compat_timeval *) optval;
	struct timeval ktime;
	mm_segment_t old_fs;
	int len, err;

	if (get_user(len, optlen))
		return -EFAULT;
	if (len < sizeof(*up))
		return -EINVAL;
	len = sizeof(ktime);
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_getsockopt(fd, level, optname, (char *) &ktime, &len);
	set_fs(old_fs);

	if (!err) {
		if (put_user(sizeof(*up), optlen) ||
		    put_user(ktime.tv_sec, &up->tv_sec) ||
		    __put_user(ktime.tv_usec, &up->tv_usec))
			err = -EFAULT;
	}
	return err;
}

asmlinkage int sys32_getsockopt(int fd, int level, int optname,
				char *optval, int *optlen)
{
	if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO)
		return do_get_sock_timeout(fd, level, optname, optval, optlen);
	return sys_getsockopt(fd, level, optname, optval, optlen);
}

#define MAX_SOCK_ADDR	128		/* 108 for Unix domain -  16 for IP, 16 for IPX, 24 for IPv6, about 80 for AX.25 */
#define __CMSG32_NXTHDR(ctl, len, cmsg, cmsglen) __cmsg32_nxthdr((ctl),(len),(cmsg),(cmsglen))
#define CMSG32_NXTHDR(mhdr, cmsg, cmsglen) cmsg32_nxthdr((mhdr), (cmsg), (cmsglen))

#define CMSG32_ALIGN(len) ( ((len)+sizeof(int)-1) & ~(sizeof(int)-1) )

#define CMSG32_DATA(cmsg)	((void *)((char *)(cmsg) + CMSG32_ALIGN(sizeof(struct cmsghdr32))))
#define CMSG32_SPACE(len) (CMSG32_ALIGN(sizeof(struct cmsghdr32)) + CMSG32_ALIGN(len))
#define CMSG32_LEN(len) (CMSG32_ALIGN(sizeof(struct cmsghdr32)) + (len))
#define __CMSG32_FIRSTHDR(ctl,len) ((len) >= sizeof(struct cmsghdr32) ? \
				    (struct cmsghdr32 *)(ctl) : \
				    (struct cmsghdr32 *)NULL)
#define CMSG32_FIRSTHDR(msg)	__CMSG32_FIRSTHDR((msg)->msg_control, (msg)->msg_controllen)

struct msghdr32
{
	u32               msg_name;
	int               msg_namelen;
	u32               msg_iov;
	compat_size_t msg_iovlen;
	u32               msg_control;
	compat_size_t msg_controllen;
	unsigned          msg_flags;
};

struct cmsghdr32
{
	compat_size_t cmsg_len;
	int               cmsg_level;
	int               cmsg_type;
};

__inline__ struct cmsghdr32 *__cmsg32_nxthdr(void *__ctl, __kernel_size_t __size,
					     struct cmsghdr32 *__cmsg, int __cmsg_len)
{
	struct cmsghdr32 * __ptr;

	__ptr = (struct cmsghdr32 *)(((unsigned char *) __cmsg) +
				     CMSG32_ALIGN(__cmsg_len));
	if ((unsigned long)((char*)(__ptr+1) - (char *) __ctl) > __size)
		return NULL;

	return __ptr;
}

__inline__ struct cmsghdr32 *cmsg32_nxthdr (struct msghdr *__msg,
					    struct cmsghdr32 *__cmsg,
					    int __cmsg_len)
{
	return __cmsg32_nxthdr(__msg->msg_control, __msg->msg_controllen,
			       __cmsg, __cmsg_len);
}

static inline int msghdr_from_user32_to_kern(struct msghdr *kmsg, struct msghdr32 *umsg)
{
	u32 tmp1, tmp2, tmp3;
	int err;

	err = get_user(tmp1, &umsg->msg_name);
	err |= __get_user(tmp2, &umsg->msg_iov);
	err |= __get_user(tmp3, &umsg->msg_control);
	if (err)
		return -EFAULT;

	kmsg->msg_name = (void *)A(tmp1);
	kmsg->msg_iov = (struct iovec *)A(tmp2);
	kmsg->msg_control = (void *)A(tmp3);

	err = get_user(kmsg->msg_namelen, &umsg->msg_namelen);
	err |= get_user(kmsg->msg_iovlen, &umsg->msg_iovlen);
	err |= get_user(kmsg->msg_controllen, &umsg->msg_controllen);
	err |= get_user(kmsg->msg_flags, &umsg->msg_flags);
	
	return err;
}

static inline int iov_from_user32_to_kern(struct iovec *kiov,
					  struct iovec32 *uiov32,
					  int niov)
{
	int tot_len = 0;

	while(niov > 0) {
		u32 len, buf;

		if(get_user(len, &uiov32->iov_len) ||
		   get_user(buf, &uiov32->iov_base)) {
			tot_len = -EFAULT;
			break;
		}
		tot_len += len;
		kiov->iov_base = (void *)A(buf);
		kiov->iov_len = (__kernel_size_t) len;
		uiov32++;
		kiov++;
		niov--;
	}
	return tot_len;
}

/* I've named the args so it is easy to tell whose space the pointers are in. */
static int verify_iovec32(struct msghdr *kern_msg, struct iovec *kern_iov,
			  char *kern_address, int mode)
{
	int tot_len;

	if(kern_msg->msg_namelen) {
		if(mode==VERIFY_READ) {
			int err = move_addr_to_kernel(kern_msg->msg_name,
						      kern_msg->msg_namelen,
						      kern_address);
			if(err < 0)
				return err;
		}
		kern_msg->msg_name = kern_address;
	} else
		kern_msg->msg_name = NULL;

	if(kern_msg->msg_iovlen > UIO_FASTIOV) {
		kern_iov = kmalloc(kern_msg->msg_iovlen * sizeof(struct iovec),
				   GFP_KERNEL);
		if(!kern_iov)
			return -ENOMEM;
	}

	tot_len = iov_from_user32_to_kern(kern_iov,
					  (struct iovec32 *)kern_msg->msg_iov,
					  kern_msg->msg_iovlen);
	if(tot_len >= 0)
		kern_msg->msg_iov = kern_iov;
	else if(kern_msg->msg_iovlen > UIO_FASTIOV)
		kfree(kern_iov);

	return tot_len;
}

/* There is a lot of hair here because the alignment rules (and
 * thus placement) of cmsg headers and length are different for
 * 32-bit apps.  -DaveM
 */
static int cmsghdr_from_user32_to_kern(struct msghdr *kmsg,
				       unsigned char *stackbuf, int stackbuf_size)
{
	struct cmsghdr32 *ucmsg;
	struct cmsghdr *kcmsg, *kcmsg_base;
	compat_size_t ucmlen;
	__kernel_size_t kcmlen, tmp;

	kcmlen = 0;
	kcmsg_base = kcmsg = (struct cmsghdr *)stackbuf;
	ucmsg = CMSG32_FIRSTHDR(kmsg);
	while(ucmsg != NULL) {
		if(get_user(ucmlen, &ucmsg->cmsg_len))
			return -EFAULT;

		/* Catch bogons. */
		if(CMSG32_ALIGN(ucmlen) <
		   CMSG32_ALIGN(sizeof(struct cmsghdr32)))
			return -EINVAL;
		if((unsigned long)(((char *)ucmsg - (char *)kmsg->msg_control)
				   + ucmlen) > kmsg->msg_controllen)
			return -EINVAL;

		tmp = ((ucmlen - CMSG32_ALIGN(sizeof(*ucmsg))) +
		       CMSG_ALIGN(sizeof(struct cmsghdr)));
		kcmlen += tmp;
		ucmsg = CMSG32_NXTHDR(kmsg, ucmsg, ucmlen);
	}
	if (kcmlen == 0)
		return -EINVAL;

	/* The kcmlen holds the 64-bit version of the control length.
	 * It may not be modified as we do not stick it into the kmsg
	 * until we have successfully copied over all of the data
	 * from the user.
	 */
	if (kcmlen > stackbuf_size)
		kcmsg_base = kcmsg = kmalloc(kcmlen, GFP_KERNEL);
	if (kcmsg == NULL)
		return -ENOBUFS;

	/* Now copy them over neatly. */
	memset(kcmsg, 0, kcmlen);
	ucmsg = CMSG32_FIRSTHDR(kmsg);
	while (ucmsg != NULL) {
		__get_user(ucmlen, &ucmsg->cmsg_len);
		tmp = ((ucmlen - CMSG32_ALIGN(sizeof(*ucmsg))) +
		       CMSG_ALIGN(sizeof(struct cmsghdr)));
		kcmsg->cmsg_len = tmp;
		__get_user(kcmsg->cmsg_level, &ucmsg->cmsg_level);
		__get_user(kcmsg->cmsg_type, &ucmsg->cmsg_type);

		/* Copy over the data. */
		if(copy_from_user(CMSG_DATA(kcmsg),
				  CMSG32_DATA(ucmsg),
				  (ucmlen - CMSG32_ALIGN(sizeof(*ucmsg)))))
			goto out_free_efault;

		/* Advance. */
		kcmsg = (struct cmsghdr *)((char *)kcmsg + CMSG_ALIGN(tmp));
		ucmsg = CMSG32_NXTHDR(kmsg, ucmsg, ucmlen);
	}

	/* Ok, looks like we made it.  Hook it up and return success. */
	kmsg->msg_control = kcmsg_base;
	kmsg->msg_controllen = kcmlen;
	return 0;

out_free_efault:
	if(kcmsg_base != (struct cmsghdr *)stackbuf)
		kfree(kcmsg_base);
	return -EFAULT;
}

asmlinkage long sys32_sendmsg(int fd, struct msghdr32* user_msg, unsigned int user_flags)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	struct iovec iov[UIO_FASTIOV];
	unsigned char ctl[sizeof(struct cmsghdr) + 20];
	unsigned char *ctl_buf = ctl;
	struct msghdr kern_msg;
	int err, total_len;
	
	if(msghdr_from_user32_to_kern(&kern_msg, user_msg))
		return -EFAULT;
	if(kern_msg.msg_iovlen > UIO_MAXIOV)
		return -EINVAL;
	err = verify_iovec32(&kern_msg, iov, address, VERIFY_READ);
	if (err < 0)
		goto out;
	total_len = err;

	if(kern_msg.msg_controllen) {
		err = cmsghdr_from_user32_to_kern(&kern_msg, ctl, sizeof(ctl));
		if(err)
			goto out_freeiov;
		ctl_buf = kern_msg.msg_control;
	}
	kern_msg.msg_flags = user_flags;

	sock = sockfd_lookup(fd, &err);
	if (sock != NULL) {
		if (sock->file->f_flags & O_NONBLOCK)
			kern_msg.msg_flags |= MSG_DONTWAIT;
		err = sock_sendmsg(sock, &kern_msg, total_len);
		sockfd_put(sock);
	}

	/* N.B. Use kfree here, as kern_msg.msg_controllen might change? */
	if(ctl_buf != ctl)
		kfree(ctl_buf);
out_freeiov:
	if(kern_msg.msg_iov != iov)
		kfree(kern_msg.msg_iov);
out:
	return err;
}

static void put_cmsg32(struct msghdr *kmsg, int level, int type,
		       int len, void *data)
{
	struct cmsghdr32 *cm = (struct cmsghdr32 *) kmsg->msg_control;
	struct cmsghdr32 cmhdr;
	int cmlen = CMSG32_LEN(len);

	if (cm == NULL || kmsg->msg_controllen < sizeof(*cm)) {
		kmsg->msg_flags |= MSG_CTRUNC;
		return;
	}

	if (kmsg->msg_controllen < cmlen) {
		kmsg->msg_flags |= MSG_CTRUNC;
		cmlen = kmsg->msg_controllen;
	}
	cmhdr.cmsg_level = level;
	cmhdr.cmsg_type = type;
	cmhdr.cmsg_len = cmlen;

	if (copy_to_user(cm, &cmhdr, sizeof cmhdr))
		return;
	if (copy_to_user(CMSG32_DATA(cm), data, cmlen - sizeof(struct cmsghdr32)))
		return;
	cmlen = CMSG32_SPACE(len);
	kmsg->msg_control += cmlen;
	kmsg->msg_controllen -= cmlen;
}


static void scm_detach_fds32(struct msghdr *kmsg, struct scm_cookie *scm)
{
	struct cmsghdr32 *cm = (struct cmsghdr32 *) kmsg->msg_control;
	int fdmax = (kmsg->msg_controllen - sizeof(struct cmsghdr32)) / sizeof(int);
	int fdnum = scm->fp->count;
	struct file **fp = scm->fp->fp;
	int *cmfptr;
	int err = 0, i;

	if (fdnum < fdmax)
		fdmax = fdnum;

	for (i = 0, cmfptr = (int *) CMSG32_DATA(cm); i < fdmax; i++, cmfptr++) {
		int new_fd;
		err = get_unused_fd();
		if (err < 0)
			break;
		new_fd = err;
		err = put_user(new_fd, cmfptr);
		if (err) {
			put_unused_fd(new_fd);
			break;
		}
		/* Bump the usage count and install the file. */
		get_file(fp[i]);
		fd_install(new_fd, fp[i]);
	}

	if (i > 0) {
		int cmlen = CMSG32_LEN(i * sizeof(int));
		if (!err)
			err = put_user(SOL_SOCKET, &cm->cmsg_level);
		if (!err)
			err = put_user(SCM_RIGHTS, &cm->cmsg_type);
		if (!err)
			err = put_user(cmlen, &cm->cmsg_len);
		if (!err) {
			cmlen = CMSG32_SPACE(i * sizeof(int));
			kmsg->msg_control += cmlen;
			kmsg->msg_controllen -= cmlen;
		}
	}
	if (i < fdnum)
		kmsg->msg_flags |= MSG_CTRUNC;

	/*
	 * All of the files that fit in the message have had their
	 * usage counts incremented, so we just free the list.
	 */
	__scm_destroy(scm);
}

/* In these cases we (currently) can just copy to data over verbatim
 * because all CMSGs created by the kernel have well defined types which
 * have the same layout in both the 32-bit and 64-bit API.  One must add
 * some special cased conversions here if we start sending control messages
 * with incompatible types.
 *
 * SCM_RIGHTS and SCM_CREDENTIALS are done by hand in recvmsg32 right after
 * we do our work.  The remaining cases are:
 *
 * SOL_IP	IP_PKTINFO	struct in_pktinfo	32-bit clean
 *		IP_TTL		int			32-bit clean
 *		IP_TOS		__u8			32-bit clean
 *		IP_RECVOPTS	variable length		32-bit clean
 *		IP_RETOPTS	variable length		32-bit clean
 *		(these last two are clean because the types are defined
 *		 by the IPv4 protocol)
 *		IP_RECVERR	struct sock_extended_err +
 *				struct sockaddr_in	32-bit clean
 * SOL_IPV6	IPV6_RECVERR	struct sock_extended_err +
 *				struct sockaddr_in6	32-bit clean
 *		IPV6_PKTINFO	struct in6_pktinfo	32-bit clean
 *		IPV6_HOPLIMIT	int			32-bit clean
 *		IPV6_FLOWINFO	u32			32-bit clean
 *		IPV6_HOPOPTS	ipv6 hop exthdr		32-bit clean
 *		IPV6_DSTOPTS	ipv6 dst exthdr(s)	32-bit clean
 *		IPV6_RTHDR	ipv6 routing exthdr	32-bit clean
 *		IPV6_AUTHHDR	ipv6 auth exthdr	32-bit clean
 */
static void cmsg32_recvmsg_fixup(struct msghdr *kmsg, unsigned long orig_cmsg_uptr)
{
	unsigned char *workbuf, *wp;
	unsigned long bufsz, space_avail;
	struct cmsghdr *ucmsg;

	bufsz = ((unsigned long)kmsg->msg_control) - orig_cmsg_uptr;
	space_avail = kmsg->msg_controllen + bufsz;
	wp = workbuf = kmalloc(bufsz, GFP_KERNEL);
	if(workbuf == NULL)
		goto fail;

	/* To make this more sane we assume the kernel sends back properly
	 * formatted control messages.  Because of how the kernel will truncate
	 * the cmsg_len for MSG_TRUNC cases, we need not check that case either.
	 */
	ucmsg = (struct cmsghdr *) orig_cmsg_uptr;
	while(((unsigned long)ucmsg) <=
	      (((unsigned long)kmsg->msg_control) - sizeof(struct cmsghdr))) {
		struct cmsghdr32 *kcmsg32 = (struct cmsghdr32 *) wp;
		int clen64, clen32;

		/* UCMSG is the 64-bit format CMSG entry in user-space.
		 * KCMSG32 is within the kernel space temporary buffer
		 * we use to convert into a 32-bit style CMSG.
		 */
		__get_user(kcmsg32->cmsg_len, &ucmsg->cmsg_len);
		__get_user(kcmsg32->cmsg_level, &ucmsg->cmsg_level);
		__get_user(kcmsg32->cmsg_type, &ucmsg->cmsg_type);

		clen64 = kcmsg32->cmsg_len;
		if (kcmsg32->cmsg_level == SOL_SOCKET &&
		    kcmsg32->cmsg_type == SO_TIMESTAMP) {
			struct timeval tv;
			struct compat_timeval *tv32;

			if (clen64 != CMSG_LEN(sizeof(struct timeval))) {
				kfree(workbuf);
				goto fail;
			}
			copy_from_user(&tv, CMSG_DATA(ucmsg), sizeof(tv));
			tv32 = (struct compat_timeval *) CMSG32_DATA(kcmsg32);
			tv32->tv_sec = tv.tv_sec;
			tv32->tv_usec = tv.tv_usec;
			clen32 = sizeof(*tv32) +
				CMSG32_ALIGN(sizeof(struct cmsghdr32));
		} else {
			copy_from_user(CMSG32_DATA(kcmsg32), CMSG_DATA(ucmsg),
				       clen64 - CMSG_ALIGN(sizeof(*ucmsg)));
			clen32 = ((clen64 - CMSG_ALIGN(sizeof(*ucmsg))) +
				  CMSG32_ALIGN(sizeof(struct cmsghdr32)));
		}
		kcmsg32->cmsg_len = clen32;

		switch (kcmsg32->cmsg_type) {
			/*
			 * The timestamp type's data needs to be converted
			 * from 64-bit time values to 32-bit time values
			 */
		case SO_TIMESTAMP: {
			compat_time_t* ptr_time32 = CMSG32_DATA(kcmsg32);
			__kernel_time_t*   ptr_time   = CMSG_DATA(ucmsg);
			*ptr_time32     = *ptr_time;
			*(ptr_time32+1) = *(ptr_time+1);
			kcmsg32->cmsg_len -= 2*(sizeof(__kernel_time_t) -
						sizeof(compat_time_t));
		}
		default:;
		}

		ucmsg = (struct cmsghdr *) (((char *)ucmsg) + CMSG_ALIGN(clen64));
		wp = (((char *)kcmsg32) + CMSG32_ALIGN(kcmsg32->cmsg_len));
	}

	/* Copy back fixed up data, and adjust pointers. */
	bufsz = (wp - workbuf);
	copy_to_user((void *)orig_cmsg_uptr, workbuf, bufsz);

	kmsg->msg_control = (struct cmsghdr *)
		(((char *)orig_cmsg_uptr) + bufsz);
	kmsg->msg_controllen = space_avail - bufsz;

	kfree(workbuf);
	return;

fail:
	/* If we leave the 64-bit format CMSG chunks in there,
	 * the application could get confused and crash.  So to
	 * ensure greater recovery, we report no CMSGs.
	 */
	kmsg->msg_controllen += bufsz;
	kmsg->msg_control = (void *) orig_cmsg_uptr;
}

asmlinkage long sys32_recvmsg(int fd, struct msghdr32* user_msg, unsigned int user_flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct msghdr kern_msg;
	char addr[MAX_SOCK_ADDR];
	struct socket *sock;
	struct iovec *iov = iovstack;
	struct sockaddr *uaddr;
	int *uaddr_len;
	unsigned long cmsg_ptr;
	int err, total_len, len = 0;
	
	if(msghdr_from_user32_to_kern(&kern_msg, user_msg))
		return -EFAULT;
	if(kern_msg.msg_iovlen > UIO_MAXIOV)
		return -EINVAL;

	uaddr = kern_msg.msg_name;
	uaddr_len = &user_msg->msg_namelen;
	err = verify_iovec32(&kern_msg, iov, addr, VERIFY_WRITE);
	if (err < 0)
		goto out;
	total_len = err;

	cmsg_ptr = (unsigned long) kern_msg.msg_control;
	kern_msg.msg_flags = 0;

	sock = sockfd_lookup(fd, &err);
	if (sock != NULL) {
		struct sock_iocb *si;
		struct kiocb iocb;

		if (sock->file->f_flags & O_NONBLOCK)
			user_flags |= MSG_DONTWAIT;

		init_sync_kiocb(&iocb, NULL);
		si = kiocb_to_siocb(&iocb);
		si->sock = sock;
		si->scm = &si->async_scm;
		si->msg = &kern_msg;
		si->size = total_len;
		si->flags = user_flags;
		memset(si->scm, 0, sizeof(*si->scm));

		err = sock->ops->recvmsg(&iocb, sock, &kern_msg, total_len,
					 user_flags, si->scm);
		if (-EIOCBQUEUED == err)
			err = wait_on_sync_kiocb(&iocb);

		if(err >= 0) {
			len = err;
			if(!kern_msg.msg_control) {
				if(sock->passcred || si->scm->fp)
					kern_msg.msg_flags |= MSG_CTRUNC;
				if(si->scm->fp)
					__scm_destroy(si->scm);
			} else {
				/* If recvmsg processing itself placed some
				 * control messages into user space, it's is
				 * using 64-bit CMSG processing, so we need
				 * to fix it up before we tack on more stuff.
				 */
				if((unsigned long) kern_msg.msg_control != cmsg_ptr)
					cmsg32_recvmsg_fixup(&kern_msg, cmsg_ptr);

				/* Wheee... */
				if(sock->passcred)
					put_cmsg32(&kern_msg,
						   SOL_SOCKET, SCM_CREDENTIALS,
						   sizeof(si->scm->creds),
						   &si->scm->creds);
				if(si->scm->fp != NULL)
					scm_detach_fds32(&kern_msg, si->scm);
			}
		}
		sockfd_put(sock);
	}

  if (uaddr != NULL && err >= 0 && kern_msg.msg_namelen)
		err = move_addr_to_user(addr, kern_msg.msg_namelen, uaddr, uaddr_len);
	if(cmsg_ptr != 0 && err >= 0) {
		unsigned long ucmsg_ptr = ((unsigned long)kern_msg.msg_control);
		compat_size_t uclen = (compat_size_t) (ucmsg_ptr - cmsg_ptr);
		err |= __put_user(uclen, &user_msg->msg_controllen);
	}
	if(err >= 0)
		err = __put_user(kern_msg.msg_flags, &user_msg->msg_flags);
	if(kern_msg.msg_iov != iov)
		kfree(kern_msg.msg_iov);
out:
	if(err < 0)
		return err;
	
	return len;
}

/*
 * count32() counts the number of arguments/envelopes
 */
static int count32(u32 * argv, int max)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			u32 p; int error;

			error = get_user(p,argv);
			if (error)
				return error;
			if (!p)
				break;
			argv++;
			if (++i > max)
				return -E2BIG;
		}
	}
	return i;
}

/*
 * 'copy_string32()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 */
static int copy_strings32(int argc, u32 * argv, struct linux_binprm *bprm)
{
	while (argc-- > 0) {
		u32 str;
		int len;
		unsigned long pos;

		if (get_user(str, argv + argc) ||
		    !str ||
		    !(len = strnlen_user((char *)A(str), bprm->p)))
			return -EFAULT;

		if (bprm->p < len)
			return -E2BIG;

		bprm->p -= len;

		pos = bprm->p;
		while (len) {
			char *kaddr;
			struct page *page;
			int offset, bytes_to_copy, new, err;

			offset = pos % PAGE_SIZE;
			page = bprm->page[pos / PAGE_SIZE];
			new = 0;
			if (!page) {
				page = alloc_page(GFP_USER);
				bprm->page[pos / PAGE_SIZE] = page;
				if (!page)
					return -ENOMEM;
				new = 1;
			}
			kaddr = (char *)kmap(page);

			if (new && offset)
				memset(kaddr, 0, offset);
			bytes_to_copy = PAGE_SIZE - offset;
			if (bytes_to_copy > len) {
				bytes_to_copy = len;
				if (new)
					memset(kaddr+offset+len, 0,
					       PAGE_SIZE-offset-len);
			}

			err = copy_from_user(kaddr + offset, (char *)A(str),
					     bytes_to_copy);
			flush_page_to_ram(page);
			kunmap((unsigned long)kaddr);

			if (err)
				return -EFAULT;

			pos += bytes_to_copy;
			str += bytes_to_copy;
			len -= bytes_to_copy;
		}
	}
	return 0;
}

/*
 * sys32_execve() executes a new program.
 */
static int do_execve32(char * filename, u32 * argv, u32 * envp, struct pt_regs * regs)
{
	struct linux_binprm bprm;
	struct file * file;
	int retval;
	int i;

	file = open_exec(filename);

	retval = PTR_ERR(file);
	if (IS_ERR(file))
		return retval;

	bprm.p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);
	memset(bprm.page, 0, MAX_ARG_PAGES * sizeof(bprm.page[0]));

	bprm.file = file;
	bprm.filename = filename;
	bprm.sh_bang = 0;
	bprm.loader = 0;
	bprm.exec = 0;
	bprm.security = NULL;
	bprm.mm = mm_alloc();
	retval = -ENOMEM;
	if (!bprm.mm)
		goto out_file;

	retval = init_new_context(current, bprm.mm);
	if (retval < 0)
		goto out_mm;

	bprm.argc = count32(argv, bprm.p / sizeof(u32));
	if ((retval = bprm.argc) < 0)
		goto out_mm;

	bprm.envc = count32(envp, bprm.p / sizeof(u32));
	if ((retval = bprm.envc) < 0)
		goto out_mm;

	retval = security_bprm_alloc(&bprm);
	if (retval)
		goto out;

	retval = prepare_binprm(&bprm);
	if (retval < 0) 
		goto out; 

	retval = copy_strings_kernel(1, &bprm.filename, &bprm);
	if (retval < 0) 
		goto out; 

	bprm.exec = bprm.p;
	retval = copy_strings32(bprm.envc, envp, &bprm);
	if (retval < 0) 
		goto out; 

	retval = copy_strings32(bprm.argc, argv, &bprm);
	if (retval < 0) 
		goto out; 

	retval = search_binary_handler(&bprm,regs);
	if (retval >= 0) {
		/* execve success */
		security_bprm_free(&bprm);
		return retval;
	}

out:
	/* Something went wrong, return the inode and free the argument pages*/
	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		struct page * page = bprm.page[i];
		if (page)
			__free_page(page);
	}

	if (bprm.security)
		security_bprm_free(&bprm);

out_mm:
	mmdrop(bprm.mm);

out_file:
	if (bprm.file) {
		allow_write_access(bprm.file);
		fput(bprm.file);
	}
	return retval;
}

asmlinkage long sys32_execve(unsigned long a0, unsigned long a1, unsigned long a2,
			                       unsigned long a3, unsigned long a4, unsigned long a5,
			                       struct pt_regs *regs)
{
	int error;
	char * filename;
	
	filename = getname((char *) a0);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	if (regs->msr & MSR_FP)
		giveup_fpu(current);

	error = do_execve32(filename, (u32*) a1, (u32*) a2, regs);

	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);

out:
	return error;
}

/* Set up a thread for executing a new program. */
void start_thread32(struct pt_regs* regs, unsigned long nip, unsigned long sp)
{
	set_fs(USER_DS);
	memset(regs->gpr, 0, sizeof(regs->gpr));
	memset(&regs->ctr, 0, 4 * sizeof(regs->ctr));
	regs->nip = nip;
	regs->gpr[1] = sp;
	regs->msr = MSR_USER32;
	if (last_task_used_math == current)
		last_task_used_math = 0;
	current->thread.fpscr = 0;
}

extern asmlinkage int sys_prctl(int option, unsigned long arg2, unsigned long arg3,
				unsigned long arg4, unsigned long arg5);

/* Note: it is necessary to treat option as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_prctl(u32 option, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	return sys_prctl((int)option,
			 (unsigned long) arg2,
			 (unsigned long) arg3,
			 (unsigned long) arg4,
			 (unsigned long) arg5);
}

extern asmlinkage int sys_sched_rr_get_interval(pid_t pid, struct timespec *interval);

/* Note: it is necessary to treat pid as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage int sys32_sched_rr_get_interval(u32 pid, struct compat_timespec *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval((int)pid, &t);
	set_fs (old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_pciconfig_read(unsigned long bus, unsigned long dfn, unsigned long off,
					 unsigned long len, unsigned char *buf);

asmlinkage int sys32_pciconfig_read(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_read((unsigned long) bus,
				  (unsigned long) dfn,
				  (unsigned long) off,
				  (unsigned long) len,
				  (unsigned char *)AA(ubuf));
}




extern asmlinkage int sys_pciconfig_write(unsigned long bus, unsigned long dfn, unsigned long off,
					                                unsigned long len, unsigned char *buf);

asmlinkage int sys32_pciconfig_write(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_write((unsigned long) bus,
				   (unsigned long) dfn,
				   (unsigned long) off,
				   (unsigned long) len,
				   (unsigned char *)AA(ubuf));
}

extern asmlinkage int sys_newuname(struct new_utsname * name);

asmlinkage int ppc64_newuname(struct new_utsname * name)
{
	int errno = sys_newuname(name);

	if (current->personality == PER_LINUX32 && !errno) {
		if(copy_to_user(name->machine, "ppc\0\0", 8)) {
			errno = -EFAULT;
		}
	}
	return errno;
}

extern asmlinkage long sys_personality(unsigned long);

asmlinkage int ppc64_personality(unsigned long personality)
{
	int ret;
	if (current->personality == PER_LINUX32 && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}



extern asmlinkage long sys_access(const char * filename, int mode);

/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_access(const char * filename, u32 mode)
{
	return sys_access(filename, (int)mode);
}


extern asmlinkage long sys_creat(const char * pathname, int mode);

/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_creat(const char * pathname, u32 mode)
{
	return sys_creat(pathname, (int)mode);
}


extern asmlinkage long sys_wait4(pid_t pid, unsigned int * stat_addr, int options, struct rusage * ru);

/* Note: it is necessary to treat pid and options as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_wait4(u32 pid, unsigned int * stat_addr, u32 options, struct rusage * ru)
{
	if (!ru)
		return sys_wait4((int)pid, stat_addr, options, NULL);
	else {
		struct rusage r;
		int ret;
		unsigned int status;
		mm_segment_t old_fs = get_fs();
		
		set_fs (KERNEL_DS);
		ret = sys_wait4((int)pid, stat_addr ? &status : NULL, options, &r);
		set_fs (old_fs);
		if (put_rusage ((struct rusage32 *)ru, &r)) return -EFAULT;
		if (stat_addr && put_user (status, stat_addr))
			return -EFAULT;
		return ret;
	}
	
}


extern asmlinkage long sys_waitpid(pid_t pid, unsigned int * stat_addr, int options);

/* Note: it is necessary to treat pid and options as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_waitpid(u32 pid, unsigned int * stat_addr, u32 options)
{
	return sys_waitpid((int)pid, stat_addr, (int)options);
}


extern asmlinkage long sys_getgroups(int gidsetsize, gid_t *grouplist);

/* Note: it is necessary to treat gidsetsize as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getgroups(u32 gidsetsize, gid_t *grouplist)
{
	return sys_getgroups((int)gidsetsize, grouplist);
}


extern asmlinkage long sys_getpgid(pid_t pid);

/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getpgid(u32 pid)
{
	return sys_getpgid((int)pid);
}


extern asmlinkage long sys_getpriority(int which, int who);

/* Note: it is necessary to treat which and who as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getpriority(u32 which, u32 who)
{
	return sys_getpriority((int)which, (int)who);
}


extern asmlinkage long sys_getsid(pid_t pid);

/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getsid(u32 pid)
{
	return sys_getsid((int)pid);
}




extern asmlinkage long sys_kill(int pid, int sig);

/* Note: it is necessary to treat pid and sig as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_kill(u32 pid, u32 sig)
{
	return sys_kill((int)pid, (int)sig);
}


extern asmlinkage long sys_mkdir(const char * pathname, int mode);

/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_mkdir(const char * pathname, u32 mode)
{
	return sys_mkdir(pathname, (int)mode);
}


extern asmlinkage long sys_mlockall(int flags);

/* Note: it is necessary to treat flags as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_mlockall(u32 flags)
{
	return sys_mlockall((int)flags);
}


extern asmlinkage long sys_msync(unsigned long start, size_t len, int flags);

/* Note: it is necessary to treat flags as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_msync(unsigned long start, size_t len, u32 flags)
{
	return sys_msync(start, len, (int)flags);
}


extern asmlinkage long sys_nice(int increment);

/* Note: it is necessary to treat increment as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_nice(u32 increment)
{
	return sys_nice((int)increment);
}

/*
 * This is just a version for 32-bit applications which does
 * not force O_LARGEFILE on.
 */
long sys32_open(const char * filename, int flags, int mode)
{
	char * tmp;
	int fd, error;

	tmp = getname(filename);
	fd = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		fd = get_unused_fd();
		if (fd >= 0) {
			struct file * f = filp_open(tmp, flags, mode);
			error = PTR_ERR(f);
			if (IS_ERR(f))
				goto out_error;
			fd_install(fd, f);
		}
out:
		putname(tmp);
	}
	return fd;

out_error:
	put_unused_fd(fd);
	fd = error;
	goto out;
}

extern asmlinkage long sys_readlink(const char * path, char * buf, int bufsiz);

/* Note: it is necessary to treat bufsiz as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_readlink(const char * path, char * buf, u32 bufsiz)
{
	return sys_readlink(path, buf, (int)bufsiz);
}

extern asmlinkage long sys_sched_get_priority_max(int policy);

/* Note: it is necessary to treat option as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_get_priority_max(u32 policy)
{
	return sys_sched_get_priority_max((int)policy);
}


extern asmlinkage long sys_sched_get_priority_min(int policy);

/* Note: it is necessary to treat policy as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_get_priority_min(u32 policy)
{
	return sys_sched_get_priority_min((int)policy);
}


extern asmlinkage long sys_sched_getparam(pid_t pid, struct sched_param *param);

/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_getparam(u32 pid, struct sched_param *param)
{
	return sys_sched_getparam((int)pid, param);
}


extern asmlinkage long sys_sched_getscheduler(pid_t pid);

/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_getscheduler(u32 pid)
{
	return sys_sched_getscheduler((int)pid);
}


extern asmlinkage long sys_sched_setparam(pid_t pid, struct sched_param *param);

/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_setparam(u32 pid, struct sched_param *param)
{
	return sys_sched_setparam((int)pid, param);
}


extern asmlinkage long sys_sched_setscheduler(pid_t pid, int policy, struct sched_param *param);

/* Note: it is necessary to treat pid and policy as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_setscheduler(u32 pid, u32 policy, struct sched_param *param)
{
	return sys_sched_setscheduler((int)pid, (int)policy, param);
}


extern asmlinkage long sys_setdomainname(char *name, int len);

/* Note: it is necessary to treat len as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_setdomainname(char *name, u32 len)
{
	return sys_setdomainname(name, (int)len);
}


extern asmlinkage long sys_setgroups(int gidsetsize, gid_t *grouplist);

/* Note: it is necessary to treat gidsetsize as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_setgroups(u32 gidsetsize, gid_t *grouplist)
{
	return sys_setgroups((int)gidsetsize, grouplist);
}


extern asmlinkage long sys_sethostname(char *name, int len);

/* Note: it is necessary to treat len as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sethostname(char *name, u32 len)
{
	return sys_sethostname(name, (int)len);
}


extern asmlinkage long sys_setpgid(pid_t pid, pid_t pgid);

/* Note: it is necessary to treat pid and pgid as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_setpgid(u32 pid, u32 pgid)
{
	return sys_setpgid((int)pid, (int)pgid);
}


extern asmlinkage long sys_setpriority(int which, int who, int niceval);

/* Note: it is necessary to treat which, who, and niceval as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_setpriority(u32 which, u32 who, u32 niceval)
{
	return sys_setpriority((int)which, (int)who, (int)niceval);
}


extern asmlinkage long sys_ssetmask(int newmask);

/* Note: it is necessary to treat newmask as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_ssetmask(u32 newmask)
{
	return sys_ssetmask((int) newmask);
}


extern asmlinkage long sys_syslog(int type, char * buf, int len);

/* Note: it is necessary to treat type and len as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_syslog(u32 type, char * buf, u32 len)
{
	return sys_syslog((int)type, buf, (int)len);
}


extern asmlinkage long sys_umask(int mask);

/* Note: it is necessary to treat mask as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_umask(u32 mask)
{
	return sys_umask((int)mask);
}


extern asmlinkage long sys_umount(char * name, int flags);

/* Note: it is necessary to treat flags as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_umount(char * name, u32 flags)
{
	return sys_umount(name, (int)flags);
}


extern ssize_t sys_pread64(unsigned int fd, char *buf, size_t count,
			   loff_t pos);

extern ssize_t sys_pwrite64(unsigned int fd, const char *buf, size_t count,
			    loff_t pos);

compat_ssize_t sys32_pread64(unsigned int fd, char *ubuf, compat_size_t count,
			u32 reg6, u32 poshi, u32 poslo)
{
	return sys_pread64(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

compat_ssize_t sys32_pwrite64(unsigned int fd, char *ubuf, compat_size_t count,
			 u32 reg6, u32 poshi, u32 poslo)
{
	return sys_pwrite64(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

extern ssize_t sys_readahead(int fd, loff_t offset, size_t count);

compat_ssize_t sys32_readahead(int fd, u32 r4, u32 offhi, u32 offlo, u32 count)
{
        return sys_readahead(fd, ((loff_t)offhi << 32) | offlo, AA(count));
}

extern asmlinkage long sys_truncate(const char * path, unsigned long length);
extern asmlinkage long sys_ftruncate(unsigned int fd, unsigned long length);

asmlinkage int sys32_truncate64(const char * path, u32 reg4, unsigned long high, unsigned long low)
{
     	if ((int)high < 0)
		return -EINVAL;
	else
		return sys_truncate(path, (high << 32) | low);
}

asmlinkage int sys32_ftruncate64(unsigned int fd, u32 reg4, unsigned long high, unsigned long low)
{
     	if ((int)high < 0)
		return -EINVAL;
	else
		return sys_ftruncate(fd, (high << 32) | low);
}

asmlinkage long sys32_fcntl64(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	if (cmd >= F_GETLK64 && cmd <= F_SETLKW64)
		return sys_fcntl(fd, cmd + F_GETLK - F_GETLK64, arg);
	return sys32_fcntl(fd, cmd, arg);
}

struct __sysctl_args32 {
	u32 name;
	int nlen;
	u32 oldval;
	u32 oldlenp;
	u32 newval;
	u32 newlen;
	u32 __unused[4];
};

extern asmlinkage long sys32_sysctl(struct __sysctl_args32 *args)
{
	struct __sysctl_args32 tmp;
	int error;
	size_t oldlen, *oldlenp = NULL;
	unsigned long addr = (((long)&args->__unused[0]) + 7) & ~7;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	if (tmp.oldval && tmp.oldlenp) {
		/* Duh, this is ugly and might not work if sysctl_args
		   is in read-only memory, but do_sysctl does indirectly
		   a lot of uaccess in both directions and we'd have to
		   basically copy the whole sysctl.c here, and
		   glibc's __sysctl uses rw memory for the structure
		   anyway.  */
		if (get_user(oldlen, (u32 *)A(tmp.oldlenp)) ||
		    put_user(oldlen, (size_t *)addr))
			return -EFAULT;
		oldlenp = (size_t *)addr;
	}

	lock_kernel();
	error = do_sysctl((int *)A(tmp.name), tmp.nlen, (void *)A(tmp.oldval),
			  oldlenp, (void *)A(tmp.newval), tmp.newlen);
	unlock_kernel();
	if (oldlenp) {
		if (!error) {
			if (get_user(oldlen, (size_t *)addr) ||
			    put_user(oldlen, (u32 *)A(tmp.oldlenp)))
				error = -EFAULT;
		}
		copy_to_user(args->__unused, tmp.__unused, sizeof(tmp.__unused));
	}
	return error;
}

asmlinkage long sys32_time(compat_time_t* tloc)
{
	compat_time_t secs;

	struct timeval tv;

	do_gettimeofday( &tv );
	secs = tv.tv_sec;

	if (tloc) {
		if (put_user(secs,tloc))
			secs = -EFAULT;
	}

	return secs;
}

extern asmlinkage int sys_sched_setaffinity(pid_t pid, unsigned int len,
					    unsigned long *user_mask_ptr);

asmlinkage int sys32_sched_setaffinity(compat_pid_t pid, unsigned int len,
				       u32 *user_mask_ptr)
{
	unsigned long kernel_mask;
	mm_segment_t old_fs;
	int ret;

	if (get_user(kernel_mask, user_mask_ptr))
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_sched_setaffinity(pid,
				    /* XXX Nice api... */
				    sizeof(kernel_mask),
				    &kernel_mask);
	set_fs(old_fs);

	return ret;
}

extern asmlinkage int sys_sched_getaffinity(pid_t pid, unsigned int len,
					    unsigned long *user_mask_ptr);

asmlinkage int sys32_sched_getaffinity(compat_pid_t pid, unsigned int len,
				       u32 *user_mask_ptr)
{
	unsigned long kernel_mask;
	mm_segment_t old_fs;
	int ret;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_sched_getaffinity(pid,
				    /* XXX Nice api... */
				    sizeof(kernel_mask),
				    &kernel_mask);
	set_fs(old_fs);

	if (ret > 0) {
		if (put_user(kernel_mask, user_mask_ptr))
			ret = -EFAULT;
	}

	return ret;
}

int sys32_olduname(struct oldold_utsname * name)
{
	int error;
	
	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;
  
	down_read(&uts_sem);
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	error -= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	error -= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	error -= __put_user(0,name->release+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	error -= __put_user(0,name->version+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->machine,&system_utsname.machine,__OLD_UTS_LEN);
	error = __put_user(0,name->machine+__OLD_UTS_LEN);
	up_read(&uts_sem);

	error = error ? -EFAULT : 0;
	
	return error;
}
extern unsigned long sys_mmap(unsigned long addr, size_t len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, off_t offset);

unsigned long sys32_mmap2(unsigned long addr, size_t len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff)
{
	/* This should remain 12 even if PAGE_SIZE changes */
	return sys_mmap(addr, len, prot, flags, fd, pgoff << 12);
}

extern int sys_lookup_dcookie(u64 cookie64, char *buf, size_t len);

long sys32_lookup_dcookie(u32 cookie_high, u32 cookie_low, char *buf,
			  size_t len)
{
	return sys_lookup_dcookie((u64)cookie_high << 32 | cookie_low,
				  buf, len);
}
