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
#include <linux/file.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>

asmlinkage int sys_statfs(const char * path, struct statfs * buf)
{
	struct dentry * dentry;
	int error;

	lock_kernel();
	dentry = namei(path);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		struct inode * inode = dentry->d_inode;

		error = -ENOSYS;
		if (inode->i_sb->s_op->statfs)
			error = inode->i_sb->s_op->statfs(inode->i_sb, buf, sizeof(struct statfs));

		dput(dentry);
	}
	unlock_kernel();
	return error;
}

asmlinkage int sys_fstatfs(unsigned int fd, struct statfs * buf)
{
	struct inode * inode;
	struct dentry * dentry;
	struct file * file;
	int error;

	lock_kernel();
	error = verify_area(VERIFY_WRITE, buf, sizeof(struct statfs));
	if (error)
		goto out;
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		error = -EBADF;
	else if (!(dentry = file->f_dentry))
		error = -ENOENT;
	else if (!(inode = dentry->d_inode))
		error = -ENOENT;
	else if (!inode->i_sb)
		error = -ENODEV;
	else if (!inode->i_sb->s_op->statfs)
		error = -ENOSYS;
	else
		inode->i_sb->s_op->statfs(inode->i_sb, buf, sizeof(struct statfs));
out:
	unlock_kernel();
	return error;
}

int do_truncate(struct inode *inode, unsigned long length)
{
	int error;
	struct iattr newattrs;

	down(&inode->i_sem);
	newattrs.ia_size = length;
	newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME;
	error = notify_change(inode, &newattrs);
	if (!error) {
		/* truncate virtual mappings of this file */
		vmtruncate(inode, length);
		if (inode->i_op && inode->i_op->truncate)
			inode->i_op->truncate(inode);
	}
	up(&inode->i_sem);
	return error;
}

asmlinkage int sys_truncate(const char * path, unsigned long length)
{
	struct dentry * dentry;
	struct inode * inode;
	int error;

	lock_kernel();
	dentry = namei(path);

	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;
	inode = dentry->d_inode;

	error = -EACCES;
	if (S_ISDIR(inode->i_mode))
		goto dput_and_out;

	error = permission(inode,MAY_WRITE);
	if (error)
		goto dput_and_out;

	error = -EROFS;
	if (IS_RDONLY(inode))
		goto dput_and_out;

	error = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto dput_and_out;

	error = get_write_access(inode);
	if (error)
		goto dput_and_out;

	error = locks_verify_area(FLOCK_VERIFY_WRITE, inode, NULL,
				  length < inode->i_size ? length : inode->i_size,
				  abs(inode->i_size - length));
	if (!error) {
		if (inode->i_sb && inode->i_sb->dq_op)
			inode->i_sb->dq_op->initialize(inode, -1);
		error = do_truncate(inode, length);
	}
	put_write_access(inode);
dput_and_out:
	dput(dentry);
out:
	unlock_kernel();
	return error;
}

asmlinkage int sys_ftruncate(unsigned int fd, unsigned long length)
{
	struct inode * inode;
	struct dentry *dentry;
	struct file * file;
	int error;

	lock_kernel();
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		error = -EBADF;
	else if (!(dentry = file->f_dentry))
		error = -ENOENT;
	else if (!(inode = dentry->d_inode))
		error = -ENOENT;
	else if (S_ISDIR(inode->i_mode) || !(file->f_mode & FMODE_WRITE))
		error = -EACCES;
	else if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		error = -EPERM;
	else {
		error = locks_verify_area(FLOCK_VERIFY_WRITE, inode, file,
					  length<inode->i_size ? length : inode->i_size,
					  abs(inode->i_size - length));
		if (!error)
			error = do_truncate(inode, length);
	}
	unlock_kernel();
	return error;
}

#ifndef __alpha__

/*
 * sys_utime() can be implemented in user-level using sys_utimes().
 * Is this for backwards compatibility?  If so, why not move it
 * into the appropriate arch directory (for those architectures that
 * need it).
 */

/* If times==NULL, set access and modification to current time,
 * must be owner or have write permission.
 * Else, update from *times, must be owner or super user.
 */
asmlinkage int sys_utime(char * filename, struct utimbuf * times)
{
	int error;
	struct dentry * dentry;
	struct inode * inode;
	struct iattr newattrs;

	lock_kernel();
	dentry = namei(filename);

	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;
	inode = dentry->d_inode;

	error = -EROFS;
	if (IS_RDONLY(inode))
		goto dput_and_out;

	/* Don't worry, the checks are done in inode_change_ok() */
	newattrs.ia_valid = ATTR_CTIME | ATTR_MTIME | ATTR_ATIME;
	if (times) {
		error = get_user(newattrs.ia_atime, &times->actime);
		if (!error) 
			error = get_user(newattrs.ia_mtime, &times->modtime);
		if (error)
			goto dput_and_out;

		newattrs.ia_valid |= ATTR_ATIME_SET | ATTR_MTIME_SET;
	} else {
		if (current->fsuid != inode->i_uid &&
		    (error = permission(inode,MAY_WRITE)) != 0)
			goto dput_and_out;
	}
	error = notify_change(inode, &newattrs);
dput_and_out:
	dput(dentry);
out:
	unlock_kernel();
	return error;
}

#endif

/* If times==NULL, set access and modification to current time,
 * must be owner or have write permission.
 * Else, update from *times, must be owner or super user.
 */
asmlinkage int sys_utimes(char * filename, struct timeval * utimes)
{
	int error;
	struct dentry * dentry;
	struct inode * inode;
	struct iattr newattrs;

	lock_kernel();
	dentry = namei(filename);

	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;
	inode = dentry->d_inode;

	error = -EROFS;
	if (IS_RDONLY(inode))
		goto dput_and_out;

	/* Don't worry, the checks are done in inode_change_ok() */
	newattrs.ia_valid = ATTR_CTIME | ATTR_MTIME | ATTR_ATIME;
	if (utimes) {
		struct timeval times[2];
		error = -EFAULT;
		if (copy_from_user(&times, utimes, sizeof(times)))
			goto dput_and_out;
		newattrs.ia_atime = times[0].tv_sec;
		newattrs.ia_mtime = times[1].tv_sec;
		newattrs.ia_valid |= ATTR_ATIME_SET | ATTR_MTIME_SET;
	} else {
		if ((error = permission(inode,MAY_WRITE)) != 0)
			goto dput_and_out;
	}
	error = notify_change(inode, &newattrs);
dput_and_out:
	dput(dentry);
out:
	unlock_kernel();
	return error;
}

/*
 * access() needs to use the real uid/gid, not the effective uid/gid.
 * We do this by temporarily setting fsuid/fsgid to the wanted values
 */
asmlinkage int sys_access(const char * filename, int mode)
{
	struct dentry * dentry;
	int old_fsuid, old_fsgid;
	int res = -EINVAL;

	lock_kernel();
	if (mode != (mode & S_IRWXO))	/* where's F_OK, X_OK, W_OK, R_OK? */
		goto out;
	old_fsuid = current->fsuid;
	old_fsgid = current->fsgid;
	current->fsuid = current->uid;
	current->fsgid = current->gid;

	dentry = namei(filename);
	res = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		res = permission(dentry->d_inode, mode);
		dput(dentry);
	}

	current->fsuid = old_fsuid;
	current->fsgid = old_fsgid;
out:
	unlock_kernel();
	return res;
}

asmlinkage int sys_chdir(const char * filename)
{
	int error;
	struct inode *inode;
	struct dentry *dentry, *tmp;

	lock_kernel();
	
	dentry = lookup_dentry(filename, NULL, 1);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;

	error = -ENOENT;
	inode = dentry->d_inode;
	if (!inode)
		goto dput_and_out;

	error = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode))
		goto dput_and_out;

	error = permission(inode,MAY_EXEC);
	if (error)
		goto dput_and_out;

	/* exchange dentries */
	tmp = current->fs->pwd;
	current->fs->pwd = dentry;
	dentry = tmp;

dput_and_out:
	dput(dentry);
out:
	unlock_kernel();
	return error;
}

asmlinkage int sys_fchdir(unsigned int fd)
{
	struct file *file;
	struct dentry *dentry;
	struct inode *inode;
	int error;

	lock_kernel();

	error = -EBADF;
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		goto out;

	error = -ENOENT;
	if (!(dentry = file->f_dentry))
		goto out;
	if (!(inode = dentry->d_inode))
		goto out;

	error = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode))
		goto out;

	error = permission(inode,MAY_EXEC);
	if (error)
		goto out;

	{
		struct dentry *tmp;
	
		tmp = current->fs->pwd;
		current->fs->pwd = dget(dentry);
		dput(tmp);
	}
out:
	unlock_kernel();
	return error;
}

asmlinkage int sys_chroot(const char * filename)
{
	int error;
	struct inode *inode;
	struct dentry *dentry, *tmp;

	lock_kernel();
	
	dentry = lookup_dentry(filename, NULL, 1);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;

	error = -ENOENT;
	inode = dentry->d_inode;
	if (!inode)
		goto dput_and_out;

	error = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode))
		goto dput_and_out;

	error = permission(inode,MAY_EXEC);
	if (error)
		goto dput_and_out;

	error = -EPERM;
	if (!fsuser())
		goto dput_and_out;

	/* exchange dentries */
	tmp = current->fs->root;
	current->fs->root = dentry;
	dentry = tmp;

dput_and_out:
	dput(dentry);
out:
	unlock_kernel();
	return error;
}

asmlinkage int sys_fchmod(unsigned int fd, mode_t mode)
{
	struct inode * inode;
	struct dentry * dentry;
	struct file * file;
	struct iattr newattrs;
	int err = -EBADF;

	lock_kernel();
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		goto out;
	err = -ENOENT;
	if (!(dentry = file->f_dentry))
		goto out;
	if (!(inode = dentry->d_inode))
		goto out;
	err = -EROFS;
	if (IS_RDONLY(inode))
		goto out;
	err = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto out;
	if (mode == (mode_t) -1)
		mode = inode->i_mode;
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	err = notify_change(inode, &newattrs);
out:
	unlock_kernel();
	return err;
}

asmlinkage int sys_chmod(const char * filename, mode_t mode)
{
	struct dentry * dentry;
	struct inode * inode;
	int error;
	struct iattr newattrs;

	lock_kernel();
	dentry = namei(filename);

	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;
	inode = dentry->d_inode;

	error = -EROFS;
	if (IS_RDONLY(inode))
		goto dput_and_out;

	error = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto dput_and_out;

	if (mode == (mode_t) -1)
		mode = inode->i_mode;
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	error = notify_change(inode, &newattrs);

dput_and_out:
	dput(dentry);
out:
	unlock_kernel();
	return error;
}

asmlinkage int sys_fchown(unsigned int fd, uid_t user, gid_t group)
{
	struct inode * inode;
	struct dentry * dentry;
	struct file * file;
	struct iattr newattrs;
	int error = -EBADF;

	lock_kernel();
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		goto out;
	error = -ENOENT;
	if (!(dentry = file->f_dentry))
		goto out;
	if (!(inode = dentry->d_inode))
		goto out;
	error = -EROFS;
	if (IS_RDONLY(inode))
		goto out;
	error = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto out;
	if (user == (uid_t) -1)
		user = inode->i_uid;
	if (group == (gid_t) -1)
		group = inode->i_gid;
	newattrs.ia_mode = inode->i_mode;
	newattrs.ia_uid = user;
	newattrs.ia_gid = group;
	newattrs.ia_valid =  ATTR_UID | ATTR_GID | ATTR_CTIME;
	/*
	 * If the owner has been changed, remove the setuid bit
	 */
	if (inode->i_mode & S_ISUID) {
		newattrs.ia_mode &= ~S_ISUID;
		newattrs.ia_valid |= ATTR_MODE;
	}
	/*
	 * If the group has been changed, remove the setgid bit
	 *
	 * Don't remove the setgid bit if no group execute bit.
	 * This is a file marked for mandatory locking.
	 */
	if (((inode->i_mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP))) {
		newattrs.ia_mode &= ~S_ISGID;
		newattrs.ia_valid |= ATTR_MODE;
	}
	if (inode->i_sb && inode->i_sb->dq_op) {
		inode->i_sb->dq_op->initialize(inode, -1);
		error = -EDQUOT;
		if (inode->i_sb->dq_op->transfer(inode, &newattrs, 0))
			goto out;
		error = notify_change(inode, &newattrs);
		if (error)
			inode->i_sb->dq_op->transfer(inode, &newattrs, 1);
	} else
		error = notify_change(inode, &newattrs);
out:
	unlock_kernel();
	return error;
}

asmlinkage int sys_chown(const char * filename, uid_t user, gid_t group)
{
	struct dentry * dentry;
	struct inode * inode;
	int error;
	struct iattr newattrs;

	lock_kernel();
	dentry = namei(filename);

	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;
	inode = dentry->d_inode;

	error = -EROFS;
	if (IS_RDONLY(inode))
		goto dput_and_out;

	error = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto dput_and_out;

	if (user == (uid_t) -1)
		user = inode->i_uid;
	if (group == (gid_t) -1)
		group = inode->i_gid;
	newattrs.ia_mode = inode->i_mode;
	newattrs.ia_uid = user;
	newattrs.ia_gid = group;
	newattrs.ia_valid =  ATTR_UID | ATTR_GID | ATTR_CTIME;
	/*
	 * If the owner has been changed, remove the setuid bit
	 */
	if (inode->i_mode & S_ISUID) {
		newattrs.ia_mode &= ~S_ISUID;
		newattrs.ia_valid |= ATTR_MODE;
	}
	/*
	 * If the group has been changed, remove the setgid bit
	 *
	 * Don't remove the setgid bit if no group execute bit.
	 * This is a file marked for mandatory locking.
	 */
	if (((inode->i_mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP))) {
		newattrs.ia_mode &= ~S_ISGID;
		newattrs.ia_valid |= ATTR_MODE;
	}
	if (inode->i_sb->dq_op) {
		inode->i_sb->dq_op->initialize(inode, -1);
		error = -EDQUOT;
		if (inode->i_sb->dq_op->transfer(inode, &newattrs, 0))
			goto dput_and_out;
		error = notify_change(inode, &newattrs);
		if (error)
			inode->i_sb->dq_op->transfer(inode, &newattrs, 1);
	} else
		error = notify_change(inode, &newattrs);
dput_and_out:
	dput(dentry);
out:
	unlock_kernel();
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
static int do_open(const char * filename,int flags,int mode, int fd)
{
	struct inode * inode;
	struct dentry * dentry;
	struct file * f;
	int flag,error;

	f = get_empty_filp();
	if (!f)
		return -ENFILE;
	f->f_flags = flag = flags;
	f->f_mode = (flag+1) & O_ACCMODE;
	if (f->f_mode)
		flag++;
	if (flag & O_TRUNC)
		flag |= 2;
	dentry = open_namei(filename,flag,mode);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto cleanup_file;
	inode = dentry->d_inode;
	if (f->f_mode & FMODE_WRITE) {
		error = get_write_access(inode);
		if (error)
			goto cleanup_dentry;
	}

	f->f_dentry = dentry;
	f->f_pos = 0;
	f->f_reada = 0;
	f->f_op = NULL;
	if (inode->i_op)
		f->f_op = inode->i_op->default_file_ops;
	if (f->f_op && f->f_op->open) {
		error = f->f_op->open(inode,f);
		if (error)
			goto cleanup_all;
	}
	f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	current->files->fd[fd] = f;
	return 0;

cleanup_all:
	if (f->f_mode & FMODE_WRITE)
		put_write_access(inode);
cleanup_dentry:
	dput(dentry);
cleanup_file:
	put_filp(f);
	return error;
}

/*
 * Find an empty file descriptor entry, and mark it busy
 */
int get_unused_fd(void)
{
	int fd;
	struct files_struct * files = current->files;

	fd = find_first_zero_bit(&files->open_fds, NR_OPEN);
	if (fd < current->rlim[RLIMIT_NOFILE].rlim_cur) {
		FD_SET(fd, &files->open_fds);
		FD_CLR(fd, &files->close_on_exec);
		return fd;
	}
	return -EMFILE;
}

inline void put_unused_fd(int fd)
{
	FD_CLR(fd, &current->files->open_fds);
}

asmlinkage int sys_open(const char * filename,int flags,int mode)
{
	char * tmp;
	int fd, error;

	lock_kernel();
	error = get_unused_fd();
	if (error < 0)
		goto out;

	fd = error;
	tmp = getname(filename);
	error = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		error = do_open(tmp,flags,mode,fd);
		putname(tmp);
		if (!error) {
			error = fd;
			goto out;
		}
	}
	put_unused_fd(fd);
out:
	unlock_kernel();
	return error;
}

#ifndef __alpha__

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
asmlinkage int sys_creat(const char * pathname, int mode)
{
	int ret;

	lock_kernel();
	ret = sys_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
	unlock_kernel();
	return ret;
}

#endif

int __fput(struct file *filp)
{
	int	error = 0;
	struct dentry * dentry = filp->f_dentry;
	struct inode * inode = dentry->d_inode;

	if (filp->f_op && filp->f_op->release)
		error = filp->f_op->release(inode,filp);
	filp->f_dentry = NULL;
	if (filp->f_mode & FMODE_WRITE)
		put_write_access(inode);
	dput(dentry);
	return error;
}

int close_fp(struct file *filp)
{
	struct dentry *dentry;
	struct inode *inode;

	if (filp->f_count == 0) {
		printk("VFS: Close: file count is 0\n");
		return 0;
	}
	dentry = filp->f_dentry;
	inode = dentry->d_inode;
	if (inode)
		locks_remove_locks(current, filp);
	return fput(filp);
}

asmlinkage int sys_close(unsigned int fd)
{
	int error;
	struct file * filp;
	struct files_struct * files;

	lock_kernel();
	files = current->files;
	error = -EBADF;
	if (fd < NR_OPEN && (filp = files->fd[fd]) != NULL) {
		put_unused_fd(fd);
		FD_CLR(fd, &files->close_on_exec);
		files->fd[fd] = NULL;
		error = close_fp(filp);
	}
	unlock_kernel();
	return error;
}

/*
 * This routine simulates a hangup on the tty, to arrange that users
 * are given clean terminals at login time.
 */
asmlinkage int sys_vhangup(void)
{
	int ret = -EPERM;

	lock_kernel();
	if (!suser())
		goto out;
	/* If there is a controlling tty, hang it up */
	if (current->tty)
		tty_vhangup(current->tty);
	ret = 0;
out:
	unlock_kernel();
	return ret;
}
