/*
 *  linux/fs/open.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/vfs.h>
#include <linux/types.h>
#include <linux/utime.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/tty.h>
#include <linux/time.h>
#include <linux/mm.h>

#include <asm/segment.h>

extern void fcntl_remove_locks(struct task_struct *, struct file *);

asmlinkage int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;
}

asmlinkage int sys_statfs(const char * path, struct statfs * buf)
{
	struct inode * inode;
	int error;

	error = verify_area(VERIFY_WRITE, buf, sizeof(struct statfs));
	if (error)
		return error;
	error = namei(path,&inode);
	if (error)
		return error;
	if (!inode->i_sb->s_op->statfs) {
		iput(inode);
		return -ENOSYS;
	}
	inode->i_sb->s_op->statfs(inode->i_sb, buf);
	iput(inode);
	return 0;
}

asmlinkage int sys_fstatfs(unsigned int fd, struct statfs * buf)
{
	struct inode * inode;
	struct file * file;
	int error;

	error = verify_area(VERIFY_WRITE, buf, sizeof(struct statfs));
	if (error)
		return error;
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (!inode->i_sb->s_op->statfs)
		return -ENOSYS;
	inode->i_sb->s_op->statfs(inode->i_sb, buf);
	return 0;
}

asmlinkage int sys_truncate(const char * path, unsigned int length)
{
	struct inode * inode;
	int error;
	struct iattr newattrs;

	error = namei(path,&inode);
	if (error)
		return error;
	if (S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -EACCES;
	}
	if ((error = permission(inode,MAY_WRITE)) != 0) {
		iput(inode);
		return error;
	}
	if (IS_RDONLY(inode)) {
		iput(inode);
		return -EROFS;
	}
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode)) {
		iput(inode);
		return -EPERM;
	}
	error = get_write_access(inode);
	if (error) {
		iput(inode);
		return error;
	}
	inode->i_size = newattrs.ia_size = length;
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	newattrs.ia_ctime = newattrs.ia_mtime = CURRENT_TIME;
	newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME | ATTR_MTIME;
	inode->i_dirt = 1;
	error = notify_change(inode, &newattrs);
	put_write_access(inode);
	iput(inode);
	return error;
}

asmlinkage int sys_ftruncate(unsigned int fd, unsigned int length)
{
	struct inode * inode;
	struct file * file;
	struct iattr newattrs;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (S_ISDIR(inode->i_mode) || !(file->f_mode & 2))
		return -EACCES;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;
	inode->i_size = newattrs.ia_size = length;
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	newattrs.ia_ctime = newattrs.ia_mtime = CURRENT_TIME;
	newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME | ATTR_MTIME;
	inode->i_dirt = 1;
	return notify_change(inode, &newattrs);
}

/* If times==NULL, set access and modification to current time,
 * must be owner or have write permission.
 * Else, update from *times, must be owner or super user.
 */
asmlinkage int sys_utime(char * filename, struct utimbuf * times)
{
	struct inode * inode;
	long actime,modtime;
	int error;
	unsigned int flags = 0;
	struct iattr newattrs;

	error = namei(filename,&inode);
	if (error)
		return error;
	if (IS_RDONLY(inode)) {
		iput(inode);
		return -EROFS;
	}
	/* Don't worry, the checks are done in inode_change_ok() */
	if (times) {
		error = verify_area(VERIFY_READ, times, sizeof(*times));
		if (error) {
			iput(inode);
			return error;
		}
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
		newattrs.ia_ctime = CURRENT_TIME;
		flags = ATTR_ATIME_SET | ATTR_MTIME_SET;
	} else {
		if ((error = permission(inode,MAY_WRITE)) != 0) {
			iput(inode);
			return error;
		}
		actime = modtime = newattrs.ia_ctime = CURRENT_TIME;
	}
	newattrs.ia_atime = actime;
	newattrs.ia_mtime = modtime;
	newattrs.ia_valid = ATTR_CTIME | ATTR_MTIME | ATTR_ATIME | flags;
	inode->i_dirt = 1;
	error = notify_change(inode, &newattrs);
	iput(inode);
	return error;
}

/*
 * access() needs to use the real uid/gid, not the effective uid/gid.
 * We do this by temporarily setting fsuid/fsgid to the wanted values
 */
asmlinkage int sys_access(const char * filename, int mode)
{
	struct inode * inode;
	int old_fsuid, old_fsgid;
	int res;

	if (mode != (mode & S_IRWXO))	/* where's F_OK, X_OK, W_OK, R_OK? */
		return -EINVAL;
	old_fsuid = current->fsuid;
	old_fsgid = current->fsgid;
	current->fsuid = current->uid;
	current->fsgid = current->gid;
	res = namei(filename,&inode);
	if (!res) {
		res = permission(inode, mode);
		iput(inode);
	}
	current->fsuid = old_fsuid;
	current->fsgid = old_fsgid;
	return res;
}

asmlinkage int sys_chdir(const char * filename)
{
	struct inode * inode;
	int error;

	error = namei(filename,&inode);
	if (error)
		return error;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	if ((error = permission(inode,MAY_EXEC)) != 0) {
		iput(inode);
		return error;
	}
	iput(current->fs->pwd);
	current->fs->pwd = inode;
	return (0);
}

asmlinkage int sys_fchdir(unsigned int fd)
{
	struct inode * inode;
	struct file * file;
	int error;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode))
		return -ENOTDIR;
	if ((error = permission(inode,MAY_EXEC)) != 0)
		return error;
	iput(current->fs->pwd);
	current->fs->pwd = inode;
	inode->i_count++;
	return (0);
}

asmlinkage int sys_chroot(const char * filename)
{
	struct inode * inode;
	int error;

	error = namei(filename,&inode);
	if (error)
		return error;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	if (!fsuser()) {
		iput(inode);
		return -EPERM;
	}
	iput(current->fs->root);
	current->fs->root = inode;
	return (0);
}

asmlinkage int sys_fchmod(unsigned int fd, mode_t mode)
{
	struct inode * inode;
	struct file * file;
	struct iattr newattrs;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (IS_RDONLY(inode))
		return -EROFS;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;
	if (mode == (mode_t) -1)
		mode = inode->i_mode;
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_ctime = CURRENT_TIME;
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	inode->i_dirt = 1;
	return notify_change(inode, &newattrs);
}

asmlinkage int sys_chmod(const char * filename, mode_t mode)
{
	struct inode * inode;
	int error;
	struct iattr newattrs;

	error = namei(filename,&inode);
	if (error)
		return error;
	if (IS_RDONLY(inode)) {
		iput(inode);
		return -EROFS;
	}
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode)) {
		iput(inode);
		return -EPERM;
	}
	if (mode == (mode_t) -1)
		mode = inode->i_mode;
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_ctime = CURRENT_TIME;
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	inode->i_dirt = 1;
	error = notify_change(inode, &newattrs);
	iput(inode);
	return error;
}

asmlinkage int sys_fchown(unsigned int fd, uid_t user, gid_t group)
{
	struct inode * inode;
	struct file * file;
	struct iattr newattrs;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (IS_RDONLY(inode))
		return -EROFS;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;
	if (user == (uid_t) -1)
		user = inode->i_uid;
	if (group == (gid_t) -1)
		group = inode->i_gid;
	newattrs.ia_mode = inode->i_mode;
	newattrs.ia_uid = user;
	newattrs.ia_gid = group;
	newattrs.ia_ctime = CURRENT_TIME;
	newattrs.ia_valid =  ATTR_UID | ATTR_GID | ATTR_CTIME;
	/*
	 * If the owner has been changed, remove the setuid bit
	 */
	if (user != inode->i_uid && (inode->i_mode & S_ISUID)) {
		newattrs.ia_mode &= ~S_ISUID;
		newattrs.ia_valid |= ATTR_MODE;
	}
	/*
	 * If the group has been changed, remove the setgid bit
	 */
	if (group != inode->i_gid && (inode->i_mode & S_ISGID)) {
		newattrs.ia_mode &= ~S_ISGID;
		newattrs.ia_valid |= ATTR_MODE;
	}
	inode->i_dirt = 1;
	return notify_change(inode, &newattrs);
}

asmlinkage int sys_chown(const char * filename, uid_t user, gid_t group)
{
	struct inode * inode;
	int error;
	struct iattr newattrs;

	error = lnamei(filename,&inode);
	if (error)
		return error;
	if (IS_RDONLY(inode)) {
		iput(inode);
		return -EROFS;
	}
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode)) {
		iput(inode);
		return -EPERM;
	}
	if (user == (uid_t) -1)
		user = inode->i_uid;
	if (group == (gid_t) -1)
		group = inode->i_gid;
	newattrs.ia_mode = inode->i_mode;
	newattrs.ia_uid = user;
	newattrs.ia_gid = group;
	newattrs.ia_ctime = CURRENT_TIME;
	newattrs.ia_valid =  ATTR_UID | ATTR_GID | ATTR_CTIME;
	/*
	 * If the owner has been changed, remove the setuid bit
	 */
	if (user != inode->i_uid && (inode->i_mode & S_ISUID)) {
		newattrs.ia_mode &= ~S_ISUID;
		newattrs.ia_valid |= ATTR_MODE;
	}
	/*
	 * If the group has been changed, remove the setgid bit
	 */
	if (group != inode->i_gid && (inode->i_mode & S_ISGID)) {
		newattrs.ia_mode &= ~S_ISGID;
		newattrs.ia_valid |= ATTR_MODE;
	}
	inode->i_dirt = 1;
	error = notify_change(inode, &newattrs);
	iput(inode);
	return(error);
}

/*
 * Note that while the flag value (low two bits) for sys_open means:
 *	00 - read-only
 *	01 - write-only
 *	10 - read-write
 *	11 - special
 * it is changed into
 *	00 - no permissions needed
 *	01 - read-permission
 *	10 - write-permission
 *	11 - read-write
 * for the internal routines (ie open_namei()/follow_link() etc). 00 is
 * used by symlinks.
 */
int do_open(const char * filename,int flags,int mode)
{
	struct inode * inode;
	struct file * f;
	int flag,error,fd;

	for(fd=0; fd<NR_OPEN && fd<current->rlim[RLIMIT_NOFILE].rlim_cur; fd++)
		if (!current->files->fd[fd])
			break;
	if (fd>=NR_OPEN || fd>=current->rlim[RLIMIT_NOFILE].rlim_cur)
		return -EMFILE;
	FD_CLR(fd,&current->files->close_on_exec);
	f = get_empty_filp();
	if (!f)
		return -ENFILE;
	current->files->fd[fd] = f;
	f->f_flags = flag = flags;
	f->f_mode = (flag+1) & O_ACCMODE;
	if (f->f_mode)
		flag++;
	if (flag & (O_TRUNC | O_CREAT))
		flag |= 2;
	error = open_namei(filename,flag,mode,&inode,NULL);
	if (!error && (f->f_mode & 2)) {
		error = get_write_access(inode);
		if (error)
			iput(inode);
	}
	if (error) {
		current->files->fd[fd]=NULL;
		f->f_count--;
		return error;
	}

	f->f_inode = inode;
	f->f_pos = 0;
	f->f_reada = 0;
	f->f_op = NULL;
	if (inode->i_op)
		f->f_op = inode->i_op->default_file_ops;
	if (f->f_op && f->f_op->open) {
		error = f->f_op->open(inode,f);
		if (error) {
			if (f->f_mode & 2) put_write_access(inode);
			iput(inode);
			f->f_count--;
			current->files->fd[fd]=NULL;
			return error;
		}
	}
	f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);
	return (fd);
}

asmlinkage int sys_open(const char * filename,int flags,int mode)
{
	char * tmp;
	int error;

	error = getname(filename, &tmp);
	if (error)
		return error;
	error = do_open(tmp,flags,mode);
	putname(tmp);
	return error;
}

asmlinkage int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int close_fp(struct file *filp)
{
	struct inode *inode;

	if (filp->f_count == 0) {
		printk("VFS: Close: file count is 0\n");
		return 0;
	}
	inode = filp->f_inode;
	if (inode)
		fcntl_remove_locks(current, filp);
	if (filp->f_count > 1) {
		filp->f_count--;
		return 0;
	}
	if (filp->f_op && filp->f_op->release)
		filp->f_op->release(inode,filp);
	filp->f_count--;
	filp->f_inode = NULL;
	if (filp->f_mode & 2) put_write_access(inode);
	iput(inode);
	return 0;
}

asmlinkage int sys_close(unsigned int fd)
{	
	struct file * filp;

	if (fd >= NR_OPEN)
		return -EBADF;
	FD_CLR(fd, &current->files->close_on_exec);
	if (!(filp = current->files->fd[fd]))
		return -EBADF;
	current->files->fd[fd] = NULL;
	return (close_fp (filp));
}

/*
 * This routine simulates a hangup on the tty, to arrange that users
 * are given clean terminals at login time.
 */
asmlinkage int sys_vhangup(void)
{
	if (!suser())
		return -EPERM;
	/* If there is a controlling tty, hang it up */
	if (current->tty)
		tty_vhangup(current->tty);
	return 0;
}
