/*
 *  linux/arch/alpha/kernel/osf_sys.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles some of the stranger OSF/1 system call interfaces.
 * Some of the system calls expect a non-C calling standard, others have
 * special parameter blocks..
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/shm.h>

#include <asm/fpu.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/sysinfo.h>

extern int do_mount(kdev_t, const char *, const char *, char *, int, void *);
extern int do_pipe(int *);

extern struct file_operations *get_blkfops(unsigned int);
extern struct file_operations *get_chrfops(unsigned int);

extern kdev_t get_unnamed_dev(void);
extern void put_unnamed_dev(kdev_t);

extern asmlinkage int sys_umount(char *);
extern asmlinkage int sys_swapon(const char *specialfile, int swap_flags);
extern asmlinkage unsigned long sys_brk(unsigned long);

/*
 * Brk needs to return an error.  Still support Linux's brk(0) query idiom,
 * which OSF programs just shouldn't be doing.  We're still not quite
 * identical to OSF as we don't return 0 on success, but doing otherwise
 * would require changes to libc.  Hopefully this is good enough.
 */
asmlinkage unsigned long osf_brk(unsigned long brk)
{
	unsigned long retval = sys_brk(brk);
	if (brk && brk != retval)
		retval = -ENOMEM;
	return retval;
}
 
/*
 * This is pure guess-work..
 */
asmlinkage int osf_set_program_attributes(
	unsigned long text_start, unsigned long text_len,
	unsigned long bss_start, unsigned long bss_len)
{
	struct mm_struct *mm;

	lock_kernel();
	mm = current->mm;
	mm->end_code = bss_start + bss_len;
	mm->brk = bss_start + bss_len;
	printk("set_program_attributes(%lx %lx %lx %lx)\n",
		text_start, text_len, bss_start, bss_len);
	unlock_kernel();
	return 0;
}

/*
 * OSF/1 directory handling functions...
 *
 * The "getdents()" interface is much more sane: the "basep" stuff is
 * braindamage (it can't really handle filesystems where the directory
 * offset differences aren't the same as "d_reclen").
 */
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+3) & ~3)

struct osf_dirent {
	unsigned int d_ino;
	unsigned short d_reclen;
	unsigned short d_namlen;
	char d_name[1];
};

struct osf_dirent_callback {
	struct osf_dirent *dirent;
	long *basep;
	int count;
	int error;
};

static int osf_filldir(void *__buf, const char *name, int namlen, off_t offset, ino_t ino)
{
	struct osf_dirent *dirent;
	struct osf_dirent_callback *buf = (struct osf_dirent_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail */
	if (reclen > buf->count)
		return -EINVAL;
	if (buf->basep) {
		put_user(offset, buf->basep);
		buf->basep = NULL;
	}
	dirent = buf->dirent;
	put_user(ino, &dirent->d_ino);
	put_user(namlen, &dirent->d_namlen);
	put_user(reclen, &dirent->d_reclen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	((char *) dirent) += reclen;
	buf->dirent = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage int osf_getdirentries(unsigned int fd, struct osf_dirent *dirent,
				 unsigned int count, long *basep)
{
	int error;
	struct file *file;
	struct osf_dirent_callback buf;

	error = -EBADF;
	if (fd >= NR_OPEN)
		goto out;

	file = current->files->fd[fd];
	if (!file)
		goto out;

	buf.dirent = dirent;
	buf.basep = basep;
	buf.count = count;
	buf.error = 0;

	error = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;

	error = file->f_op->readdir(file, &buf, osf_filldir);
	if (error < 0)
		goto out;

	error = buf.error;
	if (count == buf.count)
		goto out;

	error = count - buf.count;
out:
	return error;
}

/*
 * Alpha syscall convention has no problem returning negative
 * values:
 */
asmlinkage int osf_getpriority(int which, int who, int a2, int a3, int a4,
			       int a5, struct pt_regs regs)
{
	extern int sys_getpriority(int, int);
	int prio;

	/*
	 * We don't need to acquire the kernel lock here, because
	 * all of these operations are local. sys_getpriority
	 * will get the lock as required..
	 */
	prio = sys_getpriority(which, who);
	if (prio >= 0) {
		regs.r0 = 0;		/* special return: no errors */
		prio = 20 - prio;
	}
	return prio;
}


/*
 * Heh. As documented by DEC..
 */
asmlinkage unsigned long sys_madvise(void)
{
	return 0;
}

/*
 * No need to acquire the kernel lock, we're local..
 */
asmlinkage unsigned long sys_getxuid(int a0, int a1, int a2, int a3, int a4, int a5,
				     struct pt_regs regs)
{
	struct task_struct * tsk = current;
	(&regs)->r20 = tsk->euid;
	return tsk->uid;
}

asmlinkage unsigned long sys_getxgid(int a0, int a1, int a2, int a3, int a4, int a5,
				     struct pt_regs regs)
{
	struct task_struct * tsk = current;
	(&regs)->r20 = tsk->egid;
	return tsk->gid;
}

asmlinkage unsigned long sys_getxpid(int a0, int a1, int a2, int a3, int a4, int a5,
				     struct pt_regs regs)
{
	struct task_struct *tsk = current;

	/* 
	 * This isn't strictly "local" any more and we should actually
	 * acquire the kernel lock. The "p_opptr" pointer might change
	 * if the parent goes away (or due to ptrace). But any race
	 * isn't actually going to matter, as if the parent happens
	 * to change we can happily return either of the pids.
	 */
	(&regs)->r20 = tsk->p_opptr->pid;
	return tsk->pid;
}

asmlinkage unsigned long osf_mmap(unsigned long addr, unsigned long len,
	       unsigned long prot, unsigned long flags, unsigned long fd,
				  unsigned long off)
{
	struct file *file = NULL;
	unsigned long ret = -EBADF;

	lock_kernel();
	if (flags & (MAP_HASSEMAPHORE | MAP_INHERIT | MAP_UNALIGNED))
		printk("%s: unimplemented OSF mmap flags %04lx\n", current->comm, flags);
	if (!(flags & MAP_ANONYMOUS)) {
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			goto out;
	}
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	ret = do_mmap(file, addr, len, prot, flags, off);
out:
	unlock_kernel();
	return ret;
}


/*
 * The OSF/1 statfs structure is much larger, but this should
 * match the beginning, at least.
 */
struct osf_statfs {
	short f_type;
	short f_flags;
	int f_fsize;
	int f_bsize;
	int f_blocks;
	int f_bfree;
	int f_bavail;
	int f_files;
	int f_ffree;
	__kernel_fsid_t f_fsid;
} *osf_stat;

static int linux_to_osf_statfs(struct statfs *linux_stat, struct osf_statfs *osf_stat, unsigned long bufsiz)
{
	struct osf_statfs tmp_stat;

	tmp_stat.f_type = linux_stat->f_type;
	tmp_stat.f_flags = 0;	/* mount flags */
	/* Linux doesn't provide a "fundamental filesystem block size": */
	tmp_stat.f_fsize = linux_stat->f_bsize;
	tmp_stat.f_bsize = linux_stat->f_bsize;
	tmp_stat.f_blocks = linux_stat->f_blocks;
	tmp_stat.f_bfree = linux_stat->f_bfree;
	tmp_stat.f_bavail = linux_stat->f_bavail;
	tmp_stat.f_files = linux_stat->f_files;
	tmp_stat.f_ffree = linux_stat->f_ffree;
	tmp_stat.f_fsid = linux_stat->f_fsid;
	if (bufsiz > sizeof(tmp_stat))
		bufsiz = sizeof(tmp_stat);
	return copy_to_user(osf_stat, &tmp_stat, bufsiz) ? -EFAULT : 0;
}

static int do_osf_statfs(struct dentry * dentry, struct osf_statfs *buffer, unsigned long bufsiz)
{
	struct statfs linux_stat;
	struct inode * inode = dentry->d_inode;
	struct super_block * sb = inode->i_sb;
	int error;

	error = -ENOSYS;
	if (sb->s_op->statfs) {
		set_fs(KERNEL_DS);
		error = sb->s_op->statfs(sb, &linux_stat, sizeof(linux_stat));
		set_fs(USER_DS);
		if (!error)
			error = linux_to_osf_statfs(&linux_stat, buffer, bufsiz);
	}
	return error;	
}

asmlinkage int osf_statfs(char *path, struct osf_statfs *buffer, unsigned long bufsiz)
{
	struct dentry *dentry;
	int retval;

	lock_kernel();
	dentry = namei(path);
	retval = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		retval = do_osf_statfs(dentry, buffer, bufsiz);
		dput(dentry);
	}
	unlock_kernel();
	return retval;
}

asmlinkage int osf_fstatfs(unsigned long fd, struct osf_statfs *buffer, unsigned long bufsiz)
{
	struct file *file;
	struct dentry *dentry;
	int retval;

	lock_kernel();
	retval = -EBADF;
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		goto out;
	dentry = file->f_dentry;
	if (dentry)
		retval = do_osf_statfs(dentry, buffer, bufsiz);
out:
	unlock_kernel();
	return retval;
}

/*
 * Uhh.. OSF/1 mount parameters aren't exactly obvious..
 *
 * Although to be frank, neither are the native Linux/i386 ones..
 */
struct ufs_args {
	char *devname;
	int flags;
	uid_t exroot;
};

struct cdfs_args {
	char *devname;
	int flags;
	uid_t exroot;
/*
 * this has lots more here, which linux handles with the option block
 * but I'm too lazy to do the translation into ascii..
 */
};

struct procfs_args {
	char *devname;
	int flags;
	uid_t exroot;
};

static int getdev(const char *name, int rdonly, struct dentry **dp)
{
	kdev_t dev;
	struct dentry *dentry;
	struct inode *inode;
	struct file_operations *fops;
	int retval;

	dentry = namei(name);
	retval = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		return retval;

	inode = dentry->d_inode;
	if (!S_ISBLK(inode->i_mode)) {
		dput(dentry);
		return -ENOTBLK;
	}
	if (IS_NODEV(inode)) {
		dput(dentry);
		return -EACCES;
	}
	dev = inode->i_rdev;
	if (MAJOR(dev) >= MAX_BLKDEV) {
		dput(dentry);
		return -ENXIO;
	}
	fops = get_blkfops(MAJOR(dev));
	if (!fops) {
		dput(dentry);
		return -ENODEV;
	}
	if (fops->open) {
		struct file dummy;
		memset(&dummy, 0, sizeof(dummy));
		dummy.f_dentry = dentry;
		dummy.f_mode = rdonly ? 1 : 3;
		retval = fops->open(inode, &dummy);
		if (retval) {
			dput(dentry);
			return retval;
		}
	}
	*dp = dentry;
	return 0;
}

static void putdev(struct dentry *dentry)
{
	struct file_operations *fops;

	fops = get_blkfops(MAJOR(dentry->d_inode->i_rdev));
	if (fops->release)
		fops->release(dentry->d_inode, NULL);
}

/*
 * We can't actually handle ufs yet, so we translate UFS mounts to
 * ext2fs mounts... I wouldn't mind a UFS filesystem, but the UFS
 * layout is so braindead it's a major headache doing it..
 */
static int osf_ufs_mount(char *dirname, struct ufs_args *args, int flags)
{
	int retval;
	struct dentry *dentry;
	struct cdfs_args tmp;

	retval = verify_area(VERIFY_READ, args, sizeof(*args));
	if (retval)
		return retval;
	copy_from_user(&tmp, args, sizeof(tmp));
	retval = getdev(tmp.devname, 0, &dentry);
	if (retval)
		return retval;
	retval = do_mount(dentry->d_inode->i_rdev, tmp.devname, dirname, "ext2", flags, NULL);
	if (retval)
		putdev(dentry);
	dput(dentry);
	return retval;
}

static int osf_cdfs_mount(char *dirname, struct cdfs_args *args, int flags)
{
	int retval;
	struct dentry * dentry;
	struct cdfs_args tmp;

	retval = verify_area(VERIFY_READ, args, sizeof(*args));
	if (retval)
		return retval;
	copy_from_user(&tmp, args, sizeof(tmp));
	retval = getdev(tmp.devname, 1, &dentry);
	if (retval)
		return retval;
	retval = do_mount(dentry->d_inode->i_rdev, tmp.devname, dirname, "iso9660", flags, NULL);
	if (retval)
		putdev(dentry);
	dput(dentry);
	return retval;
}

static int osf_procfs_mount(char *dirname, struct procfs_args *args, int flags)
{
	kdev_t dev;
	int retval;
	struct procfs_args tmp;

	retval = verify_area(VERIFY_READ, args, sizeof(*args));
	if (retval)
		return retval;
	copy_from_user(&tmp, args, sizeof(tmp));
	dev = get_unnamed_dev();
	if (!dev)
		return -ENODEV;
	retval = do_mount(dev, "", dirname, "proc", flags, NULL);
	if (retval)
		put_unnamed_dev(dev);
	return retval;
}

asmlinkage int osf_mount(unsigned long typenr, char *path, int flag, void *data)
{
	int retval = -EINVAL;

	lock_kernel();
	switch (typenr) {
	case 1:
		retval = osf_ufs_mount(path, (struct ufs_args *) data, flag);
		break;
	case 6:
		retval = osf_cdfs_mount(path, (struct cdfs_args *) data, flag);
		break;
	case 9:
		retval = osf_procfs_mount(path, (struct procfs_args *) data, flag);
		break;
	default:
		printk("osf_mount(%ld, %x)\n", typenr, flag);
	}
	unlock_kernel();
	return retval;
}

asmlinkage int osf_umount(char *path, int flag)
{
	int ret;

	lock_kernel();
	ret = sys_umount(path);
	unlock_kernel();
	return ret;
}

/*
 * I don't know what the parameters are: the first one
 * seems to be a timeval pointer, and I suspect the second
 * one is the time remaining.. Ho humm.. No documentation.
 */
asmlinkage int osf_usleep_thread(struct timeval *sleep, struct timeval *remain)
{
	struct timeval tmp;
	unsigned long ticks;
	int retval;

	lock_kernel();
	retval = verify_area(VERIFY_READ, sleep, sizeof(*sleep));
	if (retval)
		goto out;
	if (remain && (retval = verify_area(VERIFY_WRITE, remain, sizeof(*remain))))
		goto out;
	copy_from_user(&tmp, sleep, sizeof(*sleep));
	ticks = tmp.tv_usec;
	ticks = (ticks + (1000000 / HZ) - 1) / (1000000 / HZ);
	ticks += tmp.tv_sec * HZ;
	current->timeout = ticks + jiffies;
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	retval = 0;
	if (!remain)
		goto out;
	ticks = jiffies;
	if (ticks < current->timeout)
		ticks = current->timeout - ticks;
	else
		ticks = 0;
	current->timeout = 0;
	tmp.tv_sec = ticks / HZ;
	tmp.tv_usec = ticks % HZ;
	copy_to_user(remain, &tmp, sizeof(*remain));
out:
	unlock_kernel();
	return retval;
}

asmlinkage int osf_utsname(char *name)
{
	int error;

	lock_kernel();
	error = verify_area(VERIFY_WRITE, name, 5 * 32);
	if (error)
		goto out;
	copy_to_user(name + 0, system_utsname.sysname, 32);
	copy_to_user(name + 32, system_utsname.nodename, 32);
	copy_to_user(name + 64, system_utsname.release, 32);
	copy_to_user(name + 96, system_utsname.version, 32);
	copy_to_user(name + 128, system_utsname.machine, 32);
out:
	unlock_kernel();
	return error;
}

asmlinkage int osf_swapon(const char *path, int flags, int lowat, int hiwat)
{
	int ret;

	/* for now, simply ignore lowat and hiwat... */
	lock_kernel();
	ret = sys_swapon(path, flags);
	unlock_kernel();
	return ret;
}

asmlinkage unsigned long sys_getpagesize(void)
{
	return PAGE_SIZE;
}

asmlinkage unsigned long sys_getdtablesize(void)
{
	return NR_OPEN;
}

asmlinkage int sys_pipe(int a0, int a1, int a2, int a3, int a4, int a5,
			struct pt_regs regs)
{
	int fd[2];
	int error;

	lock_kernel();
	error = do_pipe(fd);
	if (error)
		goto out;
	(&regs)->r20 = fd[1];
	error = fd[0];
out:
	unlock_kernel();
	return error;
}

/*
 * For compatibility with OSF/1 only.  Use utsname(2) instead.
 */
asmlinkage int osf_getdomainname(char *name, int namelen)
{
	unsigned len;
	int i, error;

	lock_kernel();
	error = verify_area(VERIFY_WRITE, name, namelen);
	if (error)
		goto out;

	len = namelen;
	if (namelen > 32)
		len = 32;

	for (i = 0; i < len; ++i) {
		put_user(system_utsname.domainname[i], name + i);
		if (system_utsname.domainname[i] == '\0')
			break;
	}
out:
	unlock_kernel();
	return error;
}


asmlinkage long osf_shmat(int shmid, void *shmaddr, int shmflg)
{
	unsigned long raddr;
	long err;

	lock_kernel();
	err = sys_shmat(shmid, shmaddr, shmflg, &raddr);
	if (err)
		goto out;
	/*
	 * This works because all user-level addresses are
	 * non-negative longs!
	 */
	err = raddr;
out:
	unlock_kernel();
	return err;
}


/*
 * The following stuff should move into a header file should it ever
 * be labeled "officially supported."  Right now, there is just enough
 * support to avoid applications (such as tar) printing error
 * messages.  The attributes are not really implemented.
 */

/*
 * Values for Property list entry flag
 */
#define PLE_PROPAGATE_ON_COPY		0x1	/* cp(1) will copy entry
						   by default */
#define PLE_FLAG_MASK			0x1	/* Valid flag values */
#define PLE_FLAG_ALL			-1	/* All flag value */

struct proplistname_args {
	unsigned int pl_mask;
	unsigned int pl_numnames;
	char **pl_names;
};

union pl_args {
	struct setargs {
		char *path;
		long follow;
		long nbytes;
		char *buf;
	} set;
	struct fsetargs {
		long fd;
		long nbytes;
		char *buf;
	} fset;
	struct getargs {
		char *path;
		long follow;
		struct proplistname_args *name_args;
		long nbytes;
		char *buf;
		int *min_buf_size;
	} get;
	struct fgetargs {
		long fd;
		struct proplistname_args *name_args;
		long nbytes;
		char *buf;
		int *min_buf_size;
	} fget;
	struct delargs {
		char *path;
		long follow;
		struct proplistname_args *name_args;
	} del;
	struct fdelargs {
		long fd;
		struct proplistname_args *name_args;
	} fdel;
};

enum pl_code {
	PL_SET = 1, PL_FSET = 2,
	PL_GET = 3, PL_FGET = 4,
	PL_DEL = 5, PL_FDEL = 6
};

asmlinkage long osf_proplist_syscall(enum pl_code code, union pl_args *args)
{
	long error;
	int *min_buf_size_ptr;

	lock_kernel();
	switch (code) {
	case PL_SET:
		error = verify_area(VERIFY_READ, &args->set.nbytes,
				    sizeof(args->set.nbytes));
		if (!error)
			error = args->set.nbytes;
		break;
	case PL_FSET:
		error = verify_area(VERIFY_READ, &args->fset.nbytes,
				    sizeof(args->fset.nbytes));
		if (!error)
			error = args->fset.nbytes;
		break;
	case PL_GET:
		get_user(min_buf_size_ptr, &args->get.min_buf_size);
		error = verify_area(VERIFY_WRITE, min_buf_size_ptr,
				    sizeof(*min_buf_size_ptr));
		if (!error)
			put_user(0, min_buf_size_ptr);
		break;
	case PL_FGET:
		get_user(min_buf_size_ptr, &args->fget.min_buf_size);
		error = verify_area(VERIFY_WRITE, min_buf_size_ptr,
				    sizeof(*min_buf_size_ptr));
		if (!error)
			put_user(0, min_buf_size_ptr);
		break;
	case PL_DEL:
	case PL_FDEL:
		error = 0;
		break;
	default:
		error = -EOPNOTSUPP;
		break;
	};
	unlock_kernel();
	return error;
}

/*
 * The Linux kernel isn't good at returning values that look
 * like negative longs (they are mistaken as error values).
 * Until that is fixed, we need this little workaround for
 * create_module() because it's one of the few system calls
 * that return kernel addresses (which are negative).
 */
asmlinkage unsigned long alpha_create_module(char *module_name, unsigned long size,
					  int a3, int a4, int a5, int a6,
					     struct pt_regs regs)
{
	asmlinkage unsigned long sys_create_module(char *, unsigned long);
	long retval;

	lock_kernel();
	retval = sys_create_module(module_name, size);
	/*
	 * we get either a module address or an error number,
	 * and we know the error number is a small negative
	 * number, while the address is always negative but
	 * much larger.
	 */
	if (retval + 1000 > 0)
		goto out;

	/* tell entry.S:syscall_error that this is NOT an error: */
	regs.r0 = 0;
out:
	unlock_kernel();
	return retval;
}

asmlinkage long osf_sysinfo(int command, char *buf, long count)
{
	static char * sysinfo_table[] = {
		system_utsname.sysname,
		system_utsname.nodename,
		system_utsname.release,
		system_utsname.version,
		system_utsname.machine,
		"alpha",	/* instruction set architecture */
		"dummy",	/* hardware serial number */
		"dummy",	/* hardware manufacturer */
		"dummy",	/* secure RPC domain */
	};
	unsigned long offset;
	char *res;
	long len, err = -EINVAL;

	lock_kernel();
	offset = command-1;
	if (offset >= sizeof(sysinfo_table)/sizeof(char *)) {
		/* Digital unix has a few unpublished interfaces here */
		printk("sysinfo(%d)", command);
		goto out;
	}
	res = sysinfo_table[offset];
	len = strlen(res)+1;
	if (len > count)
		len = count;
	if (copy_to_user(buf, res, len))
		err = -EFAULT;
	else
		err = 0;
out:
	unlock_kernel();
	return err;
}

asmlinkage unsigned long osf_getsysinfo(unsigned long op, void *buffer,
					unsigned long nbytes,
					int *start, void *arg)
{
	unsigned long w;

	switch (op) {
	case GSI_IEEE_FP_CONTROL:
		/* Return current software fp control & status bits.  */
		w = current->tss.flags & IEEE_SW_MASK;
		if (put_user(w, (unsigned long *) buffer))
			return -EFAULT;
		return 0;

	case GSI_IEEE_STATE_AT_SIGNAL:
		/*
		 * Not sure anybody will ever use this weird stuff.  These
		 * ops can be used (under OSF/1) to set the fpcr that should
		 * be used when a signal handler starts executing.
		 */
		break;

 	case GSI_UACPROC:
 		w = (current->tss.flags >> UAC_SHIFT) & UAC_BITMASK;
 		if (put_user(w, (unsigned int *)buffer))
 			return -EFAULT;
 		return 0;

	default:
		break;
	}

	return -EOPNOTSUPP;
}

/* Dummy functions for now */
#define wrfpcr(x)	do { } while (0)
#define rdfpcr()	0

asmlinkage unsigned long osf_setsysinfo(unsigned long op, void *buffer,
					unsigned long nbytes,
					int *start, void *arg)
{
	switch (op) {
	case SSI_IEEE_FP_CONTROL: {
		unsigned long swcr, fpcr;

		/* 
		 * Alpha Architecture Handbook 4.7.7.3:
		 * To be fully IEEE compiant, we must track the current IEEE
		 * exception state in software, because spurrious bits can be
		 * set in the trap shadow of a software-complete insn.
		 */

		/* Update softare trap enable bits.  */
		if (get_user(swcr, (unsigned long *)buffer))
			return -EFAULT;
		current->tss.flags &= ~IEEE_SW_MASK;
		current->tss.flags |= swcr & IEEE_SW_MASK;

		/* Update the real fpcr.  For exceptions that are disabled in
		   software but have not been seen, enable the exception in
		   hardware so that we can update our software status mask.  */
		fpcr = rdfpcr() & (~FPCR_MASK | FPCR_DYN_MASK);
		fpcr = ieee_swcr_to_fpcr(swcr | (~swcr & IEEE_STATUS_MASK)>>16);
		wrfpcr(fpcr);
		   
		return 0;
	}

	case SSI_IEEE_STATE_AT_SIGNAL:
	case SSI_IEEE_IGNORE_STATE_AT_SIGNAL:
		/*
		 * Not sure anybody will ever use this weird stuff.  These
		 * ops can be used (under OSF/1) to set the fpcr that should
		 * be used when a signal handler starts executing.
		 */
		break;

 	case SSI_NVPAIRS: {
		unsigned long v, w, i;
		
 		for (i = 0; i < nbytes; ++i) {
 			if (get_user(v, 2*i + (unsigned int *)buffer))
 				return -EFAULT;
 			if (get_user(w, 2*i + 1 + (unsigned int *)buffer))
 				return -EFAULT;
 			switch (v) {
 			case SSIN_UACPROC:
 				current->tss.flags &=
 					~(UAC_BITMASK << UAC_SHIFT);
 				current->tss.flags |=
 					(w & UAC_BITMASK) << UAC_SHIFT;
 				break;
 
 			default:
 				return -EOPNOTSUPP;
 			}
 		}
 		return 0;
	}
 
	default:
		break;
	}

	return -EOPNOTSUPP;
}
