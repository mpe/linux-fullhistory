/*
 * sys_parisc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 2000-2001 Hewlett Packard Company
 * Copyright (C) 2000 John Marvin
 * Copyright (C) 2001 Matthew Wilcox
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment. Based heavily on sys_ia32.c and sys_sparc32.c.
 */

#include <linux/config.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h> 
#include <linux/mm.h> 
#include <linux/file.h> 
#include <linux/signal.h>
#include <linux/resource.h>
#include <linux/times.h>
#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/nfs_fs.h>
#include <linux/ncp_fs.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/highmem.h>
#include <linux/highuid.h>
#include <linux/mman.h>
#include <linux/binfmts.h>
#include <linux/namei.h>
#include <linux/vfs.h>
#include <linux/ptrace.h>
#include <linux/swap.h>

#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/mmu_context.h>

#include "sys32.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(x)	printk x
#else
#define DBG(x)
#endif

/*
 * count32() counts the number of arguments/envelopes. It is basically
 *           a copy of count() from fs/exec.c, except that it works
 *           with 32 bit argv and envp pointers.
 */

static int count32(u32 *argv, int max)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			u32 p;
			int error;

			error = get_user(p,argv);
			if (error)
				return error;
			if (!p)
				break;
			argv++;
			if(++i > max)
				return -E2BIG;
		}
	}
	return i;
}


/*
 * copy_strings32() is basically a copy of copy_strings() from fs/exec.c
 *                  except that it works with 32 bit argv and envp pointers.
 */


static int copy_strings32(int argc, u32 *argv, struct linux_binprm *bprm)
{
	while (argc-- > 0) {
		u32 str;
		int len;
		unsigned long pos;

		if (get_user(str, argv + argc) ||
		    !str ||
		    !(len = strnlen_user((char *)compat_ptr(str), bprm->p)))
			return -EFAULT;

		if (bprm->p < len) 
			return -E2BIG; 

		bprm->p -= len;

		pos = bprm->p;
		while (len > 0) {
			char *kaddr;
			int i, new, err;
			struct page *page;
			int offset, bytes_to_copy;

			offset = pos % PAGE_SIZE;
			i = pos/PAGE_SIZE;
			page = bprm->page[i];
			new = 0;
			if (!page) {
				page = alloc_page(GFP_HIGHUSER);
				bprm->page[i] = page;
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
					memset(kaddr+offset+len, 0, PAGE_SIZE-offset-len);
			}
			err = copy_from_user(kaddr + offset, (char *)compat_ptr(str), bytes_to_copy);
			flush_dcache_page(page);
			kunmap(page);

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
 * do_execve32() is mostly a copy of do_execve(), with the exception
 * that it processes 32 bit argv and envp pointers.
 */

static inline int 
do_execve32(char * filename, u32 * argv, u32 * envp, struct pt_regs * regs)
{
	struct linux_binprm bprm;
	struct file *file;
	int retval;
	int i;

	file = open_exec(filename);

	retval = PTR_ERR(file);
	if (IS_ERR(file))
		return retval;

	bprm.p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);
	memset(bprm.page, 0, MAX_ARG_PAGES*sizeof(bprm.page[0]));

	DBG(("do_execve32(%s, %p, %p, %p)\n", filename, argv, envp, regs));

	bprm.file = file;
	bprm.filename = filename;
	bprm.interp = filename;
	bprm.sh_bang = 0;
	bprm.loader = 0;
	bprm.exec = 0;

	bprm.mm = mm_alloc();
	retval = -ENOMEM;
	if (!bprm.mm)
		goto out_file;

	retval = init_new_context(current, bprm.mm);
	if (retval < 0)
		goto out_mm;

	if ((bprm.argc = count32(argv, bprm.p / sizeof(u32))) < 0) 
		goto out_mm;

	if ((bprm.envc = count32(envp, bprm.p / sizeof(u32))) < 0) 
		goto out_mm;

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
	if (retval >= 0)
		/* execve success */
		return retval;

out:
	/* Something went wrong, return the inode and free the argument pages*/
	for (i = 0; i < MAX_ARG_PAGES; i++) {
		struct page *page = bprm.page[i];
		if (page)
			__free_page(page);
	}

out_mm:
	mmdrop(bprm.mm);

out_file:
	if (bprm.file) {
		allow_write_access(bprm.file);
		fput(bprm.file);
	}

	return retval;
}

/*
 * sys32_execve() executes a new program.
 */

asmlinkage int sys32_execve(struct pt_regs *regs)
{
	int error;
	char *filename;

	DBG(("sys32_execve(%p) r26 = 0x%lx\n", regs, regs->gr[26]));
	filename = getname((char *) regs->gr[26]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve32(filename, (u32 *) regs->gr[25],
		(u32 *) regs->gr[24], regs);
	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);
out:

	return error;
}

asmlinkage long sys32_unimplemented(int r26, int r25, int r24, int r23,
	int r22, int r21, int r20)
{
    printk(KERN_ERR "%s(%d): Unimplemented 32 on 64 syscall #%d!\n", 
    	current->comm, current->pid, r20);
    return -ENOSYS;
}

#ifdef CONFIG_SYSCTL

struct __sysctl_args32 {
	u32 name;
	int nlen;
	u32 oldval;
	u32 oldlenp;
	u32 newval;
	u32 newlen;
	u32 __unused[4];
};

asmlinkage long sys32_sysctl(struct __sysctl_args32 *args)
{
	struct __sysctl_args32 tmp;
	int error;
	unsigned int oldlen32;
	size_t oldlen, *oldlenp = NULL;
	unsigned long addr = (((long)&args->__unused[0]) + 7) & ~7;
	extern int do_sysctl(int *name, int nlen, void *oldval, size_t *oldlenp,
	       void *newval, size_t newlen);

	DBG(("sysctl32(%p)\n", args));

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	if (tmp.oldval && tmp.oldlenp) {
		/* Duh, this is ugly and might not work if sysctl_args
		   is in read-only memory, but do_sysctl does indirectly
		   a lot of uaccess in both directions and we'd have to
		   basically copy the whole sysctl.c here, and
		   glibc's __sysctl uses rw memory for the structure
		   anyway.  */
		/* a possibly better hack than this, which will avoid the
		 * problem if the struct is read only, is to push the
		 * 'oldlen' value out to the user's stack instead. -PB
		 */
		if (get_user(oldlen32, (u32 *)(u64)tmp.oldlenp))
			return -EFAULT;
		oldlen = oldlen32;
		if (put_user(oldlen, (size_t *)addr))
			return -EFAULT;
		oldlenp = (size_t *)addr;
	}

	lock_kernel();
	error = do_sysctl((int *)(u64)tmp.name, tmp.nlen, (void *)(u64)tmp.oldval,
			  oldlenp, (void *)(u64)tmp.newval, tmp.newlen);
	unlock_kernel();
	if (oldlenp) {
		if (!error) {
			if (get_user(oldlen, (size_t *)addr)) {
				error = -EFAULT;
			} else {
				oldlen32 = oldlen;
				if (put_user(oldlen32, (u32 *)(u64)tmp.oldlenp))
					error = -EFAULT;
			}
		}
		if (copy_to_user(args->__unused, tmp.__unused, sizeof(tmp.__unused)))
			error = -EFAULT;
	}
	return error;
}

#else /* CONFIG_SYSCTL */

asmlinkage long sys32_sysctl(struct __sysctl_args *args)
{
	return -ENOSYS;
}
#endif /* CONFIG_SYSCTL */

asmlinkage long sys32_sched_rr_get_interval(pid_t pid,
	struct compat_timespec *interval)
{
	struct timespec t;
	int ret;
	extern asmlinkage long sys_sched_rr_get_interval(pid_t pid, struct timespec *interval);
	
	KERNEL_SYSCALL(ret, sys_sched_rr_get_interval, pid, &t);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

static int
put_compat_timeval(struct compat_timeval *u, struct timeval *t)
{
	struct compat_timeval t32;
	t32.tv_sec = t->tv_sec;
	t32.tv_usec = t->tv_usec;
	return copy_to_user(u, &t32, sizeof t32);
}

static inline long get_ts32(struct timespec *o, struct compat_timeval *i)
{
	long usec;

	if (__get_user(o->tv_sec, &i->tv_sec))
		return -EFAULT;
	if (__get_user(usec, &i->tv_usec))
		return -EFAULT;
	o->tv_nsec = usec * 1000;
	return 0;
}

asmlinkage long sys32_time(compat_time_t *tloc)
{
    time_t now = get_seconds();
    compat_time_t now32 = now;

    if (tloc)
    	if (put_user(now32, tloc))
		now32 = -EFAULT;

    return now32;
}

asmlinkage int
sys32_gettimeofday(struct compat_timeval *tv, struct timezone *tz)
{
    extern void do_gettimeofday(struct timeval *tv);

    if (tv) {
	    struct timeval ktv;
	    do_gettimeofday(&ktv);
	    if (put_compat_timeval(tv, &ktv))
		    return -EFAULT;
    }
    if (tz) {
	    extern struct timezone sys_tz;
	    if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
		    return -EFAULT;
    }
    return 0;
}

asmlinkage 
int sys32_settimeofday(struct compat_timeval *tv, struct timezone *tz)
{
	struct timespec kts;
	struct timezone ktz;

 	if (tv) {
		if (get_ts32(&kts, tv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &kts : NULL, tz ? &ktz : NULL);
}

int cp_compat_stat(struct kstat *stat, struct compat_stat *statbuf)
{
	int err;

	if (stat->size > MAX_NON_LFS || !new_valid_dev(stat->dev) ||
	    !new_valid_dev(stat->rdev))
		return -EOVERFLOW;

	err  = put_user(new_encode_dev(stat->dev), &statbuf->st_dev);
	err |= put_user(stat->ino, &statbuf->st_ino);
	err |= put_user(stat->mode, &statbuf->st_mode);
	err |= put_user(stat->nlink, &statbuf->st_nlink);
	err |= put_user(0, &statbuf->st_reserved1);
	err |= put_user(0, &statbuf->st_reserved2);
	err |= put_user(new_encode_dev(stat->rdev), &statbuf->st_rdev);
	err |= put_user(stat->size, &statbuf->st_size);
	err |= put_user(stat->atime.tv_sec, &statbuf->st_atime);
	err |= put_user(stat->atime.tv_nsec, &statbuf->st_atime_nsec);
	err |= put_user(stat->mtime.tv_sec, &statbuf->st_mtime);
	err |= put_user(stat->mtime.tv_nsec, &statbuf->st_mtime_nsec);
	err |= put_user(stat->ctime.tv_sec, &statbuf->st_ctime);
	err |= put_user(stat->ctime.tv_nsec, &statbuf->st_ctime_nsec);
	err |= put_user(stat->blksize, &statbuf->st_blksize);
	err |= put_user(stat->blocks, &statbuf->st_blocks);
	err |= put_user(0, &statbuf->__unused1);
	err |= put_user(0, &statbuf->__unused2);
	err |= put_user(0, &statbuf->__unused3);
	err |= put_user(0, &statbuf->__unused4);
	err |= put_user(0, &statbuf->__unused5);
	err |= put_user(0, &statbuf->st_fstype); /* not avail */
	err |= put_user(0, &statbuf->st_realdev); /* not avail */
	err |= put_user(0, &statbuf->st_basemode); /* not avail */
	err |= put_user(0, &statbuf->st_spareshort);
	err |= put_user(stat->uid, &statbuf->st_uid);
	err |= put_user(stat->gid, &statbuf->st_gid);
	err |= put_user(0, &statbuf->st_spare4[0]);
	err |= put_user(0, &statbuf->st_spare4[1]);
	err |= put_user(0, &statbuf->st_spare4[2]);

	return err;
}

struct linux32_dirent {
	u32		d_ino;
	compat_off_t	d_off;
	u16		d_reclen;
	char		d_name[1];
};

struct old_linux32_dirent {
	u32	d_ino;
	u32	d_offset;
	u16	d_namlen;
	char	d_name[1];
};

struct getdents32_callback {
	struct linux32_dirent * current_dir;
	struct linux32_dirent * previous;
	int count;
	int error;
};

struct readdir32_callback {
	struct old_linux32_dirent * dirent;
	int count;
};

#define ROUND_UP(x,a)	((__typeof__(x))(((unsigned long)(x) + ((a) - 1)) & ~((a) - 1)))
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
static int
filldir32 (void *__buf, const char *name, int namlen, loff_t offset, ino_t ino,
	   unsigned int d_type)
{
	struct linux32_dirent * dirent;
	struct getdents32_callback * buf = (struct getdents32_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1, 4);

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

asmlinkage long
sys32_getdents (unsigned int fd, void * dirent, unsigned int count)
{
	struct file * file;
	struct linux32_dirent * lastdirent;
	struct getdents32_callback buf;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = (struct linux32_dirent *) dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, filldir32, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}

static int
fillonedir32 (void * __buf, const char * name, int namlen, loff_t offset, ino_t ino,
	      unsigned int d_type)
{
	struct readdir32_callback * buf = (struct readdir32_callback *) __buf;
	struct old_linux32_dirent * dirent;

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

asmlinkage long
sys32_readdir (unsigned int fd, void * dirent, unsigned int count)
{
	int error;
	struct file * file;
	struct readdir32_callback buf;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.count = 0;
	buf.dirent = dirent;

	error = vfs_readdir(file, fillonedir32, &buf);
	if (error >= 0)
		error = buf.count;
	fput(file);
out:
	return error;
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

asmlinkage int sys32_mount(char *dev_name, char *dir_name, char *type, unsigned long new_flags, u32 data)
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

	err = copy_mount_stuff_to_kernel((const void *)(unsigned long)data, &data_page);
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
			panic("NCP mounts not yet supported 32/64 parisc");
			/* do_ncp_super_data_conv((void *)data_page); */
		else {
			panic("SMB mounts not yet supported 32/64 parisc");
			/* do_smb_super_data_conv((void *)data_page); */
		}

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


/* readv/writev stolen from mips64 */
typedef ssize_t (*IO_fn_t)(struct file *, char *, size_t, loff_t *);

static long
do_readv_writev32(int type, struct file *file, const struct compat_iovec *vector,
		  u32 count)
{
	unsigned long tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack, *ivp;
	struct inode *inode;
	long retval, i;
	IO_fn_t fn;

	/* First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	if (!count)
		return 0;
	if(verify_area(VERIFY_READ, vector, sizeof(struct compat_iovec)*count))
		return -EFAULT;
	if (count > UIO_MAXIOV)
		return -EINVAL;
	if (count > UIO_FASTIOV) {
		iov = kmalloc(count*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			return -ENOMEM;
	}

	tot_len = 0;
	i = count;
	ivp = iov;
	while (i > 0) {
		u32 len;
		u32 buf;

		__get_user(len, &vector->iov_len);
		__get_user(buf, &vector->iov_base);
		tot_len += len;
		ivp->iov_base = compat_ptr(buf);
		ivp->iov_len = (compat_size_t) len;
		vector++;
		ivp++;
		i--;
	}

	inode = file->f_dentry->d_inode;
	/* VERIFY_WRITE actually means a read, as we write to user space */
	retval = locks_verify_area((type == VERIFY_WRITE
				    ? FLOCK_VERIFY_READ : FLOCK_VERIFY_WRITE),
				   inode, file, file->f_pos, tot_len);
	if (retval) {
		if (iov != iovstack)
			kfree(iov);
		return retval;
	}

	/* Then do the actual IO.  Note that sockets need to be handled
	 * specially as they have atomicity guarantees and can handle
	 * iovec's natively
	 */
	if (inode->i_sock) {
		int err;
		err = sock_readv_writev(type, inode, file, iov, count, tot_len);
		if (iov != iovstack)
			kfree(iov);
		return err;
	}

	if (!file->f_op) {
		if (iov != iovstack)
			kfree(iov);
		return -EINVAL;
	}
	/* VERIFY_WRITE actually means a read, as we write to user space */
	fn = file->f_op->read;
	if (type == VERIFY_READ)
		fn = (IO_fn_t) file->f_op->write;		
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
			if (retval)
				break;
			retval = nr;
			break;
		}
		retval += nr;
		if (nr != len)
			break;
	}
	if (iov != iovstack)
		kfree(iov);

	return retval;
}

asmlinkage long
sys32_readv(int fd, struct compat_iovec *vector, u32 count)
{
	struct file *file;
	ssize_t ret;

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (file->f_op && (file->f_mode & FMODE_READ) &&
	    (file->f_op->readv || file->f_op->read))
		ret = do_readv_writev32(VERIFY_WRITE, file, vector, count);

	fput(file);

bad_file:
	return ret;
}

asmlinkage long
sys32_writev(int fd, struct compat_iovec *vector, u32 count)
{
	struct file *file;
	ssize_t ret;

	ret = -EBADF;
	file = fget(fd);
	if(!file)
		goto bad_file;
	if (file->f_op && (file->f_mode & FMODE_WRITE) &&
	    (file->f_op->writev || file->f_op->write))
	        ret = do_readv_writev32(VERIFY_READ, file, vector, count);
	fput(file);

bad_file:
	return ret;
}

/*** copied from mips64 ***/
/*
 * Ooo, nasty.  We need here to frob 32-bit unsigned longs to
 * 64-bit unsigned longs.
 */

static inline int
get_fd_set32(unsigned long n, u32 *ufdset, unsigned long *fdset)
{
	n = (n + 8*sizeof(u32) - 1) / (8*sizeof(u32));
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
	n = (n + 8*sizeof(u32) - 1) / (8*sizeof(u32));

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

/*** This is a virtual copy of sys_select from fs/select.c and probably
 *** should be compared to it from time to time
 ***/
static inline void *select_bits_alloc(int size)
{
	return kmalloc(6 * size, GFP_KERNEL);
}

static inline void select_bits_free(void *bits, int size)
{
	kfree(bits);
}

/*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
#define MAX_SELECT_SECONDS \
	((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)
#define DIVIDE_ROUND_UP(x,y) (((x)+(y)-1)/(y))

asmlinkage long
sys32_select(int n, u32 *inp, u32 *outp, u32 *exp, struct compat_timeval *tvp)
{
	fd_set_bits fds;
	char *bits;
	long timeout;
	int ret, size, err;

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp) {
		struct compat_timeval tv32;
		time_t sec, usec;

		if ((ret = copy_from_user(&tv32, tvp, sizeof tv32)))
			goto out_nofds;

		sec = tv32.tv_sec;
		usec = tv32.tv_usec;

		ret = -EINVAL;
		if (sec < 0 || usec < 0)
			goto out_nofds;

		if ((unsigned long) sec < MAX_SELECT_SECONDS) {
			timeout = DIVIDE_ROUND_UP(usec, 1000000/HZ);
			timeout += sec * (unsigned long) HZ;
		}
	}

	ret = -EINVAL;
	if (n < 0)
		goto out_nofds;

	if (n > current->files->max_fdset)
		n = current->files->max_fdset;

	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words. 
	 */
	ret = -ENOMEM;
	size = FDS_BYTES(n);
	bits = select_bits_alloc(size);
	if (!bits)
		goto out_nofds;
	fds.in      = (unsigned long *)  bits;
	fds.out     = (unsigned long *) (bits +   size);
	fds.ex      = (unsigned long *) (bits + 2*size);
	fds.res_in  = (unsigned long *) (bits + 3*size);
	fds.res_out = (unsigned long *) (bits + 4*size);
	fds.res_ex  = (unsigned long *) (bits + 5*size);

	if ((ret = get_fd_set32(n, inp, fds.in)) ||
	    (ret = get_fd_set32(n, outp, fds.out)) ||
	    (ret = get_fd_set32(n, exp, fds.ex)))
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
		err = put_user(sec, &tvp->tv_sec);
		err |= __put_user(usec, &tvp->tv_usec);
		if (err)
			ret = -EFAULT;
	}

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	set_fd_set32(n, inp, fds.res_in);
	set_fd_set32(n, outp, fds.res_out);
	set_fd_set32(n, exp, fds.res_ex);

out:
	select_bits_free(bits, size);
out_nofds:
	return ret;
}

struct msgbuf32 {
    int mtype;
    char mtext[1];
};

asmlinkage long sys32_msgsnd(int msqid,
				struct msgbuf32 *umsgp32,
				size_t msgsz, int msgflg)
{
	struct msgbuf *mb;
	struct msgbuf32 mb32;
	int err;

	if ((mb = kmalloc(msgsz + sizeof *mb + 4, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	err = get_user(mb32.mtype, &umsgp32->mtype);
	mb->mtype = mb32.mtype;
	err |= copy_from_user(mb->mtext, &umsgp32->mtext, msgsz);

	if (err)
		err = -EFAULT;
	else
		KERNEL_SYSCALL(err, sys_msgsnd, msqid, mb, msgsz, msgflg);

	kfree(mb);
	return err;
}

asmlinkage long sys32_msgrcv(int msqid,
				struct msgbuf32 *umsgp32,
				size_t msgsz, long msgtyp, int msgflg)
{
	struct msgbuf *mb;
	struct msgbuf32 mb32;
	int err, len;

	if ((mb = kmalloc(msgsz + sizeof *mb + 4, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	KERNEL_SYSCALL(err, sys_msgrcv, msqid, mb, msgsz, msgtyp, msgflg);

	if (err >= 0) {
		len = err;
		mb32.mtype = mb->mtype;
		err = put_user(mb32.mtype, &umsgp32->mtype);
		err |= copy_to_user(&umsgp32->mtext, mb->mtext, len);
		if (err)
			err = -EFAULT;
		else
			err = len;
	}

	kfree(mb);
	return err;
}


extern asmlinkage ssize_t sys_sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
asmlinkage int sys32_sendfile(int out_fd, int in_fd, compat_off_t *offset, s32 count)
{
        mm_segment_t old_fs = get_fs();
        int ret;
        off_t of;

        if (offset && get_user(of, offset))
                return -EFAULT;

        set_fs(KERNEL_DS);
        ret = sys_sendfile(out_fd, in_fd, offset ? &of : NULL, count);
        set_fs(old_fs);

        if (offset && put_user(of, offset))
                return -EFAULT;

        return ret;
}

/* EXPORT/UNEXPORT */
struct nfsctl_export32 {
	char		ex_client[NFSCLNT_IDMAX+1];
	char		ex_path[NFS_MAXPATHLEN+1];
	__kernel_old_dev_t ex_dev;
	compat_ino_t	ex_ino;
	int		ex_flags;
	__kernel_uid_t	ex_anon_uid;
	__kernel_gid_t	ex_anon_gid;
};

struct nfsctl_arg32 {
	int			ca_version;	/* safeguard */
	/* wide kernel places this union on 8-byte boundary, narrow on 4 */
	union {
		struct nfsctl_svc	u_svc;
		struct nfsctl_client	u_client;
		struct nfsctl_export32	u_export;
		struct nfsctl_fdparm	u_getfd;
		struct nfsctl_fsparm	u_getfs;
	} u;
};

asmlinkage int sys32_nfsservctl(int cmd, void *argp, void *resp)
{
	int ret, tmp;
	struct nfsctl_arg32 n32;
	struct nfsctl_arg n;

	ret = copy_from_user(&n, argp, sizeof n.ca_version);
	if (ret != 0)
		return ret;

	/* adjust argp to point at the union inside the user's n32 struct */
	tmp = (unsigned long)&n32.u - (unsigned long)&n32;
	argp = (void *)((unsigned long)argp + tmp);
	switch(cmd) {
	case NFSCTL_SVC:
		ret = copy_from_user(&n.u, argp, sizeof n.u.u_svc);
		break;

	case NFSCTL_ADDCLIENT:
	case NFSCTL_DELCLIENT:
		ret = copy_from_user(&n.u, argp, sizeof n.u.u_client);
		break;

	case NFSCTL_GETFD:
		ret = copy_from_user(&n.u, argp, sizeof n.u.u_getfd);
		break;

	case NFSCTL_GETFS:
		ret = copy_from_user(&n.u, argp, sizeof n.u.u_getfs);
		break;

	case NFSCTL_UNEXPORT:		/* nfsctl_export */
	case NFSCTL_EXPORT:		/* nfsctl_export */
		ret = copy_from_user(&n32.u, argp, sizeof n32.u.u_export);
#undef CP
#define CP(x)	n.u.u_export.ex_##x = n32.u.u_export.ex_##x
		memcpy(n.u.u_export.ex_client, n32.u.u_export.ex_client, sizeof n32.u.u_export.ex_client);
		memcpy(n.u.u_export.ex_path, n32.u.u_export.ex_path, sizeof n32.u.u_export.ex_path);
		CP(dev);
		CP(ino);
		CP(flags);
		CP(anon_uid);
		CP(anon_gid);
		break;

	default:
		/* lockd probes for some other values (0x10000);
		 * so don't BUG() */
		ret = -EINVAL;
		break;
	}

	if (ret == 0) {
		unsigned char rbuf[NFS_FHSIZE + sizeof (struct knfsd_fh)];
		KERNEL_SYSCALL(ret, sys_nfsservctl, cmd, &n, &rbuf);
		if (cmd == NFSCTL_GETFD) {
			ret = copy_to_user(resp, rbuf, NFS_FHSIZE);
		} else if (cmd == NFSCTL_GETFS) {
			ret = copy_to_user(resp, rbuf, sizeof (struct knfsd_fh));
		}
	}

	return ret;
}

extern asmlinkage ssize_t sys_sendfile64(int out_fd, int in_fd, loff_t *offset, size_t count);
typedef long __kernel_loff_t32;		/* move this to asm/posix_types.h? */

asmlinkage int sys32_sendfile64(int out_fd, int in_fd, __kernel_loff_t32 *offset, s32 count)
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


struct timex32 {
	unsigned int modes;	/* mode selector */
	int offset;		/* time offset (usec) */
	int freq;		/* frequency offset (scaled ppm) */
	int maxerror;		/* maximum error (usec) */
	int esterror;		/* estimated error (usec) */
	int status;		/* clock command/status */
	int constant;		/* pll time constant */
	int precision;		/* clock precision (usec) (read only) */
	int tolerance;		/* clock frequency tolerance (ppm)
				 * (read only)
				 */
	struct compat_timeval time;	/* (read only) */
	int tick;		/* (modified) usecs between clock ticks */

	int ppsfreq;           /* pps frequency (scaled ppm) (ro) */
	int jitter;            /* pps jitter (us) (ro) */
	int shift;              /* interval duration (s) (shift) (ro) */
	int stabil;            /* pps stability (scaled ppm) (ro) */
	int jitcnt;            /* jitter limit exceeded (ro) */
	int calcnt;            /* calibration intervals (ro) */
	int errcnt;            /* calibration errors (ro) */
	int stbcnt;            /* stability limit exceeded (ro) */

	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
};

asmlinkage long sys32_adjtimex(struct timex32 *txc_p32)
{
	struct timex txc;
	struct timex32 t32;
	int ret;
	extern int do_adjtimex(struct timex *txc);

	if(copy_from_user(&t32, txc_p32, sizeof(struct timex32)))
		return -EFAULT;
#undef CP
#define CP(x) txc.x = t32.x
	CP(modes); CP(offset); CP(freq); CP(maxerror); CP(esterror);
	CP(status); CP(constant); CP(precision); CP(tolerance);
	CP(time.tv_sec); CP(time.tv_usec); CP(tick); CP(ppsfreq); CP(jitter);
	CP(shift); CP(stabil); CP(jitcnt); CP(calcnt); CP(errcnt);
	CP(stbcnt);
	ret = do_adjtimex(&txc);
#undef CP
#define CP(x) t32.x = txc.x
	CP(modes); CP(offset); CP(freq); CP(maxerror); CP(esterror);
	CP(status); CP(constant); CP(precision); CP(tolerance);
	CP(time.tv_sec); CP(time.tv_usec); CP(tick); CP(ppsfreq); CP(jitter);
	CP(shift); CP(stabil); CP(jitcnt); CP(calcnt); CP(errcnt);
	CP(stbcnt);
	return copy_to_user(txc_p32, &t32, sizeof(struct timex32)) ? -EFAULT : ret;
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
	u32 totalhigh;
	u32 freehigh;
	u32 mem_unit;
	char _f[12];
};

/* We used to call sys_sysinfo and translate the result.  But sys_sysinfo
 * undoes the good work done elsewhere, and rather than undoing the
 * damage, I decided to just duplicate the code from sys_sysinfo here.
 */

asmlinkage int sys32_sysinfo(struct sysinfo32 *info)
{
	struct sysinfo val;
	int err;
	unsigned long seq;

	/* We don't need a memset here because we copy the
	 * struct to userspace once element at a time.
	 */

	do {
		seq = read_seqbegin(&xtime_lock);
		val.uptime = jiffies / HZ;

		val.loads[0] = avenrun[0] << (SI_LOAD_SHIFT - FSHIFT);
		val.loads[1] = avenrun[1] << (SI_LOAD_SHIFT - FSHIFT);
		val.loads[2] = avenrun[2] << (SI_LOAD_SHIFT - FSHIFT);

		val.procs = nr_threads;
	} while (read_seqretry(&xtime_lock, seq));


	si_meminfo(&val);
	si_swapinfo(&val);
	
	err = put_user (val.uptime, &info->uptime);
	err |= __put_user (val.loads[0], &info->loads[0]);
	err |= __put_user (val.loads[1], &info->loads[1]);
	err |= __put_user (val.loads[2], &info->loads[2]);
	err |= __put_user (val.totalram, &info->totalram);
	err |= __put_user (val.freeram, &info->freeram);
	err |= __put_user (val.sharedram, &info->sharedram);
	err |= __put_user (val.bufferram, &info->bufferram);
	err |= __put_user (val.totalswap, &info->totalswap);
	err |= __put_user (val.freeswap, &info->freeswap);
	err |= __put_user (val.procs, &info->procs);
	err |= __put_user (val.totalhigh, &info->totalhigh);
	err |= __put_user (val.freehigh, &info->freehigh);
	err |= __put_user (val.mem_unit, &info->mem_unit);
	return err ? -EFAULT : 0;
}


/* lseek() needs a wrapper because 'offset' can be negative, but the top
 * half of the argument has been zeroed by syscall.S.
 */

extern asmlinkage off_t sys_lseek(unsigned int fd, off_t offset, unsigned int origin);

asmlinkage int sys32_lseek(unsigned int fd, int offset, unsigned int origin)
{
	return sys_lseek(fd, offset, origin);
}

asmlinkage long sys32_semctl(int semid, int semnum, int cmd, union semun arg)
{
        union semun u;
	
        if (cmd == SETVAL) {
                /* Ugh.  arg is a union of int,ptr,ptr,ptr, so is 8 bytes.
                 * The int should be in the first 4, but our argument
                 * frobbing has left it in the last 4.
                 */
                u.val = *((int *)&arg + 1);
                return sys_semctl (semid, semnum, cmd, u);
	}
	return sys_semctl (semid, semnum, cmd, arg);
}

extern long sys_lookup_dcookie(u64 cookie64, char *buf, size_t len);

long sys32_lookup_dcookie(u32 cookie_high, u32 cookie_low, char *buf,
			  size_t len)
{
	return sys_lookup_dcookie((u64)cookie_high << 32 | cookie_low,
				  buf, len);
}
