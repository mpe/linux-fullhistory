/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>

#include <sys/stat.h>
#include <sys/vfs.h>

#include <linux/string.h>
#include <linux/sched.h>
#include <linux/kernel.h>

#include <asm/segment.h>

struct file_operations * chrdev_fops[MAX_CHRDEV] = {
	NULL,
};

struct file_operations * blkdev_fops[MAX_BLKDEV] = {
	NULL,
};

int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;
}

int sys_statfs(const char * path, struct statfs * buf)
{
	printk("statfs not implemented\n");
	return -ENOSYS;
}

int sys_fstatfs(unsigned int fd, struct statfs * buf)
{
	printk("fstatfs not implemented\n");
	return -ENOSYS;
}

int sys_truncate(const char * path, unsigned int length)
{
	struct inode * inode;

	if (!(inode = namei(path)))
		return -ENOENT;
	if (!permission(inode,MAY_WRITE)) {
		iput(inode);
		return -EPERM;
	}
	inode->i_size = length;
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	inode->i_atime = inode->i_mtime = CURRENT_TIME;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

int sys_ftruncate(unsigned int fd, unsigned int length)
{
	struct inode * inode;
	struct file * file;

	if (fd >= NR_OPEN || !(file = current->filp[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (!(file->f_flags & 2))
		return -EPERM;
	inode->i_size = length;
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	inode->i_atime = inode->i_mtime = CURRENT_TIME;
	inode->i_dirt = 1;
	return 0;
}

int sys_utime(char * filename, struct utimbuf * times)
{
	struct inode * inode;
	long actime,modtime;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (times) {
		if (current->euid != inode->i_uid &&
		    !permission(inode,MAY_WRITE)) {
			iput(inode);
			return -EPERM;
		}
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else
		actime = modtime = CURRENT_TIME;
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */
int sys_access(const char * filename,int mode)
{
	struct inode * inode;
	int res, i_mode;

	mode &= 0007;
	if (!(inode=namei(filename)))
		return -EACCES;
	i_mode = res = inode->i_mode & 0777;
	iput(inode);
	if (current->uid == inode->i_uid)
		res >>= 6;
	else if (current->gid == inode->i_gid)
		res >>= 6;
	if ((res & 0007 & mode) == mode)
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last. 
	 */
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))
		return 0;
	return -EACCES;
}

int sys_chdir(const char * filename)
{
	struct inode * inode;

	if (!(inode = namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

int sys_chroot(const char * filename)
{
	struct inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	iput(current->root);
	current->root = inode;
	return (0);
}

int sys_fchmod(unsigned int fd, mode_t mode)
{
	struct inode * inode;
	struct file * file;

	if (fd >= NR_OPEN || !(file = current->filp[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if ((current->euid != inode->i_uid) && !suser())
		return -EACCES;
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	return 0;
}

int sys_chmod(const char * filename, mode_t mode)
{
	struct inode * inode;

	if (!(inode = namei(filename)))
		return -ENOENT;
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

int sys_fchown(unsigned int fd, uid_t user, gid_t group)
{
	struct inode * inode;
	struct file * file;

	if (fd >= NR_OPEN || !(file = current->filp[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (!suser())
		return -EACCES;
	inode->i_uid = user;
	inode->i_gid = group;
	inode->i_dirt=1;
	return 0;
}

int sys_chown(const char * filename, uid_t user, gid_t group)
{
	struct inode * inode;

	if (!(inode = namei(filename)))
		return -ENOENT;
	if (!suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_uid = user;
	inode->i_gid = group;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

int sys_open(const char * filename,int flag,int mode)
{
	struct inode * inode;
	struct file * f;
	int i,fd;

	for(fd=0 ; fd<NR_OPEN ; fd++)
		if (!current->filp[fd])
			break;
	if (fd>=NR_OPEN)
		return -EINVAL;
	current->close_on_exec &= ~(1<<fd);
	f=0+file_table;
	for (i=0 ; i<NR_FILE ; i++,f++)
		if (!f->f_count) break;
	if (i>=NR_FILE)
		return -EINVAL;
	(current->filp[fd] = f)->f_count++;
	if ((i = open_namei(filename,flag,mode,&inode))<0) {
		current->filp[fd]=NULL;
		f->f_count=0;
		return i;
	}
	f->f_op = NULL;
	if (inode)
		if (S_ISCHR(inode->i_mode)) {
			i = MAJOR(inode->i_rdev);
			if (i < MAX_CHRDEV)
				f->f_op = chrdev_fops[i];
		} else if (S_ISBLK(inode->i_mode)) {
			i = MAJOR(inode->i_rdev);
			if (i < MAX_CHRDEV)
				f->f_op = blkdev_fops[i];
		}
	f->f_mode = "\001\002\003\000"[flag & O_ACCMODE];
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;
	f->f_pos = 0;
	if (inode->i_op && inode->i_op->open)
		if (i = inode->i_op->open(inode,f)) {
			iput(inode);
			f->f_count=0;
			current->filp[fd]=NULL;
			return i;
		}
	return (fd);
}

int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int sys_close(unsigned int fd)
{	
	struct file * filp;

	if (fd >= NR_OPEN)
		return -EINVAL;
	current->close_on_exec &= ~(1<<fd);
	if (!(filp = current->filp[fd]))
		return -EINVAL;
	current->filp[fd] = NULL;
	if (filp->f_count == 0)
		panic("Close: file count is 0");
	if (--filp->f_count)
		return (0);
	if (filp->f_op && filp->f_op->close)
		return filp->f_op->close(filp->f_inode,filp);
	iput(filp->f_inode);
	return 0;
}
