/*
 * linux/arch/parisc/kernel/sys_hpux.c
 *
 * implements HPUX syscalls.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <asm/errno.h>
#include <asm/uaccess.h>

int hpux_execve(struct pt_regs *regs)
{
	int error;
	char *filename;

	filename = getname((char *) regs->gr[26]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;

	error = do_execve(filename, (char **) regs->gr[25],
		(char **)regs->gr[24], regs);

	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);

out:
	return error;
}

struct hpux_dirent {
	long	d_off_pad; /* we only have a 32-bit off_t */
	long	d_off;
	ino_t	d_ino;
	short	d_reclen;
	short	d_namlen;
	char	d_name[1];
};

struct getdents_callback {
	struct hpux_dirent *current_dir;
	struct hpux_dirent *previous;
	int count;
	int error;
};

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+sizeof(long)-1) & ~(sizeof(long)-1))

static int filldir(void * __buf, const char * name, int namlen, loff_t offset, ino_t ino)
{
	struct hpux_dirent * dirent;
	struct getdents_callback * buf = (struct getdents_callback *) __buf;
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
	put_user(namlen, &dirent->d_namlen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	((char *) dirent) += reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}

#undef NAME_OFFSET
#undef ROUND_UP

int hpux_getdents(unsigned int fd, struct hpux_dirent *dirent, unsigned int count)
{
	struct file * file;
	struct dentry * dentry;
	struct inode * inode;
	struct hpux_dirent * lastdirent;
	struct getdents_callback buf;
	int error;

	lock_kernel();
	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	dentry = file->f_dentry;
	if (!dentry)
		goto out_putf;

	inode = dentry->d_inode;
	if (!inode)
		goto out_putf;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out_putf;

	/*
	 * Get the inode's semaphore to prevent changes
	 * to the directory while we read it.
	 */
	down(&inode->i_sem);
	error = file->f_op->readdir(file, &buf, filldir);
	up(&inode->i_sem);
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
	unlock_kernel();
	return error;
}

int hpux_mount(const char *fs, const char *path, int mflag,
		const char *fstype, const char *dataptr, int datalen)
{
	return -ENOSYS;
}

static int cp_hpux_stat(struct kstat *stat, struct hpux_stat64 *statbuf)
{
	struct hpux_stat64 tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = stat->dev;
	tmp.st_ino = stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	tmp.st_uid = stat->uid;
	tmp.st_gid = stat->gid;
	tmp.st_rdev = stat->rdev;
	tmp.st_size = stat->size;
	tmp.st_atime = stat->atime;
	tmp.st_mtime = stat->mtime;
	tmp.st_ctime = stat->ctime;
	tmp.st_blocks = stat->blocks;
	tmp.st_blksize = stat->blksize;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

long hpux_stat64(const char *path, struct hpux_stat64 *buf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_hpux_stat(&stat, statbuf);

	return error;
}

long hpux_fstat64(unsigned int fd, struct hpux_stat64 *statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_hpux_stat(&stat, statbuf);

	return error;
}

long hpux_lstat64(char *filename, struct hpux_stat64 *statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_hpux_stat(&stat, statbuf);

	return error;
}
