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
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/shm.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

extern int do_mount(dev_t, const char *, char *, int, void *);
extern int do_pipe(int *);

extern struct file_operations * get_blkfops(unsigned int);
extern struct file_operations * get_chrfops(unsigned int);

extern dev_t get_unnamed_dev(void);
extern void put_unnamed_dev(dev_t);

extern asmlinkage int sys_umount(char *);
extern asmlinkage int sys_swapon(const char *specialfile, int swap_flags);

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
	unsigned int	d_ino;
	unsigned short	d_reclen;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct osf_dirent_callback {
	struct osf_dirent * dirent;
	long *basep;
	int count;
	int error;
};

static int osf_filldir(void * __buf, char * name, int namlen, off_t offset, ino_t ino)
{
	struct osf_dirent * dirent;
	struct osf_dirent_callback * buf = (struct osf_dirent_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;		/* unly used if we fail */
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
	memcpy_tofs(dirent->d_name, name, namlen);
	put_fs_byte(0, dirent->d_name + namlen);
	((char *) dirent) += reclen;
	buf->dirent = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage int osf_getdirentries(unsigned int fd, struct osf_dirent * dirent,
	unsigned int count, long *basep)
{
	int error;
	struct file * file;
	struct osf_dirent_callback buf;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		return -EBADF;
	if (!file->f_op || !file->f_op->readdir)
		return -ENOTDIR;
	error = verify_area(VERIFY_WRITE, dirent, count);
	if (error)
		return error;
	if (basep) {
		error = verify_area(VERIFY_WRITE, basep, sizeof(long));
		if (error)
			return error;
	}
	buf.dirent = dirent;
	buf.basep = basep;
	buf.count = count;
	buf.error = 0;
	error = file->f_op->readdir(file->f_inode, file, &buf, osf_filldir);
	if (error < 0)
		return error;
	if (count == buf.count)
		return buf.error;
	return count - buf.count;
}

/*
 * Heh. As documented by DEC..
 */
asmlinkage unsigned long sys_madvise(void)
{
	return 0;
}

asmlinkage unsigned long sys_getxuid(int a0, int a1, int a2, int a3, int a4, int a5,
	struct pt_regs regs)
{
	(&regs)->r20 = current->euid;
	return current->uid;
}

asmlinkage unsigned long sys_getxgid(int a0, int a1, int a2, int a3, int a4, int a5,
	struct pt_regs regs)
{
	(&regs)->r20 = current->egid;
	return current->gid;
}

asmlinkage unsigned long sys_getxpid(int a0, int a1, int a2, int a3, int a4, int a5,
	struct pt_regs regs)
{
	(&regs)->r20 = current->p_opptr->pid;
	return current->pid;
}

#define OSF_MAP_ANONYMOUS	0x0010
#define OSF_MAP_FIXED		0x0100
#define OSF_MAP_HASSEMAPHORE	0x0200
#define OSF_MAP_INHERIT		0x0400
#define OSF_MAP_UNALIGNED	0x0800

asmlinkage unsigned long osf_mmap(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long osf_flags, unsigned long fd,
	unsigned long off)
{
	struct file * file = NULL;
	unsigned long flags = osf_flags & 0x0f;

	if (osf_flags & (OSF_MAP_HASSEMAPHORE | OSF_MAP_INHERIT | OSF_MAP_UNALIGNED))
		printk("%s: unimplemented OSF mmap flags %04lx\n", current->comm, osf_flags);
	if (osf_flags & OSF_MAP_FIXED)
		flags |= MAP_FIXED;
	if (osf_flags & OSF_MAP_ANONYMOUS)
		flags |= MAP_ANONYMOUS;
	else {
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			return -EBADF;
	}
	return do_mmap(file, addr, len, prot, flags, off);
}

asmlinkage int osf_statfs(char * path, struct statfs * buffer, unsigned long bufsiz)
{
	struct inode * inode;
	int retval;

	if (bufsiz > sizeof(struct statfs))
		bufsiz = sizeof(struct statfs);
	retval = verify_area(VERIFY_WRITE, buffer, bufsiz);
	if (retval)
		return retval;
	retval = namei(path, &inode);
	if (retval)
		return retval;
	if (!inode->i_sb->s_op->statfs) {
		iput(inode);
		return -ENOSYS;
	}
	inode->i_sb->s_op->statfs(inode->i_sb, buffer, bufsiz);
	iput(inode);
	return 0;
}

asmlinkage int osf_fstatfs(unsigned long fd, struct statfs * buffer, unsigned long bufsiz)
{
	struct file * file;
	struct inode * inode;
	int retval;

	retval = verify_area(VERIFY_WRITE, buffer, bufsiz);
	if (retval)
		return retval;
	if (bufsiz > sizeof(struct statfs))
		bufsiz = sizeof(struct statfs);
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (!inode->i_sb->s_op->statfs)
		return -ENOSYS;
	inode->i_sb->s_op->statfs(inode->i_sb, buffer, bufsiz);
	return 0;
}

/*
 * Uhh.. OSF/1 mount parameters aren't exactly obvious..
 *
 * Although to be frank, neither are the native Linux/i386 ones..
 */
struct ufs_args {
	char * devname;
	int flags;
	uid_t exroot;
};

struct cdfs_args {
	char * devname;
	int flags;
	uid_t exroot;
/*
 * this has lots more here, which linux handles with the option block
 * but I'm too lazy to do the translation into ascii..
 */
};

struct procfs_args {
	char * devname;
	int flags;
	uid_t exroot;
};

static int getdev(const char * name, int rdonly, struct inode ** ino)
{
	dev_t dev;
	struct inode * inode;
	struct file_operations * fops;
	int retval;

	retval = namei(name, &inode);
	if (retval)
		return retval;
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
	if (IS_NODEV(inode)) {
		iput(inode);
		return -EACCES;
	}
	dev = inode->i_rdev;
	if (MAJOR(dev) >= MAX_BLKDEV) {
		iput(inode);
		return -ENXIO;
	}
	fops = get_blkfops(MAJOR(dev));
	if (!fops) {
		iput(inode);
		return -ENODEV;
	}
	if (fops->open) {
		struct file dummy;
		memset(&dummy, 0, sizeof(dummy));
		dummy.f_inode = inode;
		dummy.f_mode = rdonly ? 1 : 3;
		retval = fops->open(inode, &dummy);
		if (retval) {
			iput(inode);
			return retval;
		}
	}
	*ino = inode;
	return 0;
}

static void putdev(struct inode * inode)
{
	struct file_operations * fops;

	fops = get_blkfops(MAJOR(inode->i_rdev));
	if (fops->release)
		fops->release(inode, NULL);
}

/*
 * We can't actually handle ufs yet, so we translate UFS mounts to
 * ext2fs mounts... I wouldn't mind a USF filesystem, but the UFS
 * layout is so braindead it's a major headache doing it..
 */
static int osf_ufs_mount(char * dirname, struct ufs_args * args, int flags)
{
	int retval;
	struct inode * inode;
	struct cdfs_args tmp;

	retval = verify_area(VERIFY_READ, args, sizeof(*args));
	if (retval)
		return retval;
	memcpy_fromfs(&tmp, args, sizeof(tmp));
	retval = getdev(tmp.devname, 0, &inode);
	if (retval)
		return retval;
	retval = do_mount(inode->i_rdev, dirname, "ext2", flags, NULL);
	if (retval)
		putdev(inode);
	iput(inode);
	return retval;
}

static int osf_cdfs_mount(char * dirname, struct cdfs_args * args, int flags)
{
	int retval;
	struct inode * inode;
	struct cdfs_args tmp;

	retval = verify_area(VERIFY_READ, args, sizeof(*args));
	if (retval)
		return retval;
	memcpy_fromfs(&tmp, args, sizeof(tmp));
	retval = getdev(tmp.devname, 1, &inode);
	if (retval)
		return retval;
	retval = do_mount(inode->i_rdev, dirname, "iso9660", flags, NULL);
	if (retval)
		putdev(inode);
	iput(inode);
	return retval;
}

static int osf_procfs_mount(char * dirname, struct procfs_args * args, int flags)
{
	dev_t dev;
	int retval;
	struct procfs_args tmp;

	retval = verify_area(VERIFY_READ, args, sizeof(*args));
	if (retval)
		return retval;
	memcpy_fromfs(&tmp, args, sizeof(tmp));
	dev = get_unnamed_dev();
	if (!dev)
		return -ENODEV;
	retval = do_mount(dev, dirname, "proc", flags, NULL);
	if (retval)
		put_unnamed_dev(dev);
	return retval;
}

asmlinkage int osf_mount(unsigned long typenr, char * path, int flag, void * data)
{
	int retval;

	retval = -EINVAL;
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
	return retval;
}

asmlinkage int osf_umount(char * path, int flag)
{
	return sys_umount(path);
}

/*
 * I don't know what the parameters are: the first one
 * seems to be a timeval pointer, and I suspect the second
 * one is the time remaining.. Ho humm.. No documentation.
 */
asmlinkage int osf_usleep_thread(struct timeval * sleep, struct timeval * remain)
{
	struct timeval tmp;
	unsigned long ticks;
	int retval;

	retval = verify_area(VERIFY_READ, sleep, sizeof(*sleep));
	if (retval)
		return retval;
	if (remain && (retval = verify_area(VERIFY_WRITE, remain, sizeof(*remain))))
		return retval;
	memcpy_fromfs(&tmp, sleep, sizeof(*sleep));
	ticks = tmp.tv_usec;
	ticks = (ticks + (1000000 / HZ) - 1) / (1000000 / HZ);
	ticks += tmp.tv_sec * HZ;
	current->timeout = ticks + jiffies;
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	if (!remain)
		return 0;
	ticks = jiffies;
	if (ticks < current->timeout)
		ticks = current->timeout - ticks;
	else
		ticks = 0;
	current->timeout = 0;
	tmp.tv_sec = ticks / HZ;
	tmp.tv_usec = ticks % HZ;
	memcpy_tofs(remain, &tmp, sizeof(*remain));
	return 0;
}

asmlinkage int osf_utsname(char * name)
{
	int error = verify_area(VERIFY_WRITE, name, 5*32);
	if (error)
		return error;
	memcpy_tofs(name +   0, system_utsname.sysname, 32);
	memcpy_tofs(name +  32, system_utsname.nodename, 32);
	memcpy_tofs(name +  64, system_utsname.release, 32);
	memcpy_tofs(name +  96, system_utsname.version, 32);
	memcpy_tofs(name + 128, system_utsname.machine, 32);
	return 0;
}

asmlinkage int osf_swapon(const char * path, int flags, int lowat, int hiwat)
{
	/* for now, simply ignore lowat and hiwat... */
	return sys_swapon(path, flags);
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

	error = do_pipe(fd);
	if (error)
		return error;
	(&regs)->r20 = fd[1];
	return fd[0];
}

/*
 * For compatibility with OSF/1 only.  Use utsname(2) instead.
 */
asmlinkage int osf_getdomainname(char *name, int namelen)
{
	unsigned len;
	int i, error;

	error = verify_area(VERIFY_WRITE, name, namelen);
	if (error)
		return error;

	len = namelen;
	if (namelen > 32)
	  len = 32;

	for (i = 0; i < len; ++i) {
		put_user(system_utsname.domainname[i], name + i);
		if (system_utsname.domainname[i] == '\0')
		  break;
	}
	return 0;
}


asmlinkage long osf_shmat(int shmid, void *shmaddr, int shmflg)
{
	unsigned long raddr;
	int err;

	err = sys_shmat(shmid, shmaddr, shmflg, &raddr);
	if (err)
		return err;
	/*
	 * This works because all user-level addresses are
	 * non-negative longs!
	 */
	return raddr;
}
