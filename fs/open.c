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
#include <linux/quotaops.h>

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
	struct file * file;
	struct inode * inode;
	struct dentry * dentry;
	struct super_block * sb;
	int error;

	lock_kernel();
	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	error = -ENOENT;
	if (!(dentry = file->f_dentry))
		goto out_putf;
	if (!(inode = dentry->d_inode))
		goto out_putf;
	error = -ENODEV;
	if (!(sb = inode->i_sb))
		goto out_putf;
	error = -ENOSYS;
	if (sb->s_op->statfs)
		error = sb->s_op->statfs(sb, buf, sizeof(struct statfs));
out_putf:
	fput(file);
out:
	unlock_kernel();
	return error;
}

int do_truncate(struct dentry *dentry, unsigned long length)
{
	struct inode *inode = dentry->d_inode;
	int error;
	struct iattr newattrs;

	down(&inode->i_sem);
	newattrs.ia_size = length;
	newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME;
	error = notify_change(dentry, &newattrs);
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
		DQUOT_INIT(inode);
		error = do_truncate(dentry, length);
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
	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	error = -ENOENT;
	if (!(dentry = file->f_dentry))
		goto out_putf;
	if (!(inode = dentry->d_inode))
		goto out_putf;
	error = -EACCES;
	if (S_ISDIR(inode->i_mode) || !(file->f_mode & FMODE_WRITE))
		goto out_putf;
	error = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto out_putf;
	error = locks_verify_area(FLOCK_VERIFY_WRITE, inode, file,
				  length<inode->i_size ? length : inode->i_size,
				  abs(inode->i_size - length));
	if (!error)
		error = do_truncate(dentry, length);
out_putf:
	fput(file);
out:
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
	error = notify_change(dentry, &newattrs);
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
	error = notify_change(dentry, &newattrs);
dput_and_out:
	dput(dentry);
out:
	unlock_kernel();
	return error;
}

/*
 * access() needs to use the real uid/gid, not the effective uid/gid.
 * We do this by temporarily clearing all FS-related capabilities and
 * switching the fsuid/fsgid around to the real ones.
 */
asmlinkage int sys_access(const char * filename, int mode)
{
	struct dentry * dentry;
	int old_fsuid, old_fsgid;
	kernel_cap_t old_cap;
	int res = -EINVAL;

	lock_kernel();
	if (mode != (mode & S_IRWXO))	/* where's F_OK, X_OK, W_OK, R_OK? */
		goto out;
	old_fsuid = current->fsuid;
	old_fsgid = current->fsgid;
	old_cap = current->cap_effective;

	current->fsuid = current->uid;
	current->fsgid = current->gid;

	/* Clear the capabilities if we switch to a non-root user */
	if (current->uid)
		cap_clear(current->cap_effective);

	dentry = namei(filename);
	res = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		res = permission(dentry->d_inode, mode);
		dput(dentry);
	}

	current->fsuid = old_fsuid;
	current->fsgid = old_fsgid;
	current->cap_effective = old_cap;
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
	
	dentry = namei(filename);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;

	inode = dentry->d_inode;

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
	file = fget(fd);
	if (!file)
		goto out;

	error = -ENOENT;
	if (!(dentry = file->f_dentry))
		goto out_putf;
	if (!(inode = dentry->d_inode))
		goto out_putf;

	error = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode))
		goto out_putf;

	error = permission(inode, MAY_EXEC);
	if (!error) {
		struct dentry *tmp = current->fs->pwd;
		current->fs->pwd = dget(dentry);
		dput(tmp);
	}
out_putf:
	fput(file);
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
	
	dentry = namei(filename);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;

	inode = dentry->d_inode;

	error = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode))
		goto dput_and_out;

	error = permission(inode,MAY_EXEC);
	if (error)
		goto dput_and_out;

	error = -EPERM;
	if (!capable(CAP_SYS_CHROOT))
		goto dput_and_out;

	/* exchange dentries */
	tmp = current->fs->root;
	current->fs->root = dentry;
	dentry = tmp;
	error = 0;

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
	int err = -EBADF;
	struct iattr newattrs;

	lock_kernel();
	file = fget(fd);
	if (!file)
		goto out;

	err = -ENOENT;
	if (!(dentry = file->f_dentry))
		goto out_putf;
	if (!(inode = dentry->d_inode))
		goto out_putf;

	err = -EROFS;
	if (IS_RDONLY(inode))
		goto out_putf;
	err = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto out_putf;
	if (mode == (mode_t) -1)
		mode = inode->i_mode;
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	err = notify_change(dentry, &newattrs);

out_putf:
	fput(file);
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
	error = notify_change(dentry, &newattrs);

dput_and_out:
	dput(dentry);
out:
	unlock_kernel();
	return error;
}

static int chown_common(struct dentry * dentry, uid_t user, gid_t group)
{
	struct inode * inode;
	int error;
	struct iattr newattrs;

	error = -ENOENT;
	if (!(inode = dentry->d_inode)) {
		printk("chown_common: NULL inode\n");
		goto out;
	}
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
	error = DQUOT_TRANSFER(dentry, &newattrs);
out:
	return error;
}

asmlinkage int sys_chown(const char * filename, uid_t user, gid_t group)
{
	struct dentry * dentry;
	int error;

	lock_kernel();
	dentry = namei(filename);

	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		error = chown_common(dentry, user, group);
		dput(dentry);
	}
	unlock_kernel();
	return error;
}

asmlinkage int sys_lchown(const char * filename, uid_t user, gid_t group)
{
	struct dentry * dentry;
	int error;

	lock_kernel();
	dentry = lnamei(filename);

	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		error = chown_common(dentry, user, group);
		dput(dentry);
	}
	unlock_kernel();
	return error;
}


asmlinkage int sys_fchown(unsigned int fd, uid_t user, gid_t group)
{
	struct dentry * dentry;
	struct file * file;
	int error = -EBADF;

	lock_kernel();
	file = fget(fd);
	if (!file)
		goto out;
	error = -ENOENT;
	if ((dentry = file->f_dentry) != NULL)
		error = chown_common(dentry, user, group);
	fput(file);

out:
	unlock_kernel();
	return error;
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
static int do_open(const char * filename, int flags, int mode, int fd)
{
	struct inode * inode;
	struct dentry * dentry;
	struct file * f;
	int flag,error;

	error = -ENFILE;
	f = get_empty_filp();
	if (!f)
		goto out;
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

	fd_install(fd, f);
	return 0;

cleanup_all:
	if (f->f_mode & FMODE_WRITE)
		put_write_access(inode);
cleanup_dentry:
	f->f_dentry = NULL;
	dput(dentry);
cleanup_file:
	put_filp(f);
out:
	return error;
}

/*
 * Find an empty file descriptor entry, and mark it busy.
 */
int get_unused_fd(void)
{
	struct files_struct * files = current->files;
	int fd, error;

	error = -EMFILE;
	fd = find_first_zero_bit(&files->open_fds, NR_OPEN);
	/*
	 * N.B. For clone tasks sharing a files structure, this test
	 * will limit the total number of files that can be opened.
	 */
	if (fd >= current->rlim[RLIMIT_NOFILE].rlim_cur)
		goto out;

	/* Check here for fd > files->max_fds to do dynamic expansion */

	FD_SET(fd, &files->open_fds);
	FD_CLR(fd, &files->close_on_exec);
#if 1
	/* Sanity check */
	if (files->fd[fd] != NULL) {
		printk("get_unused_fd: slot %d not NULL!\n", fd);
		files->fd[fd] = NULL;
	}
#endif
	error = fd;

out:
	return error;
}

inline void put_unused_fd(unsigned int fd)
{
	FD_CLR(fd, &current->files->open_fds);
}

asmlinkage int sys_open(const char * filename, int flags, int mode)
{
	char * tmp;
	int fd, error;

	lock_kernel();
	fd = get_unused_fd();
	if (fd < 0)
		goto out;

	tmp = getname(filename);
	error = PTR_ERR(tmp);
	if (IS_ERR(tmp))
		goto out_fail;
	error = do_open(tmp, flags, mode, fd);
	putname(tmp);
	if (error)
		goto out_fail;
out:
	unlock_kernel();
	return fd;

out_fail:
	put_unused_fd(fd);
	fd = error;
	goto out;
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

/*
 * Called when retiring the last use of a file pointer.
 */
int __fput(struct file *filp)
{
	struct dentry * dentry = filp->f_dentry;
	struct inode * inode = dentry->d_inode;
	int	error = 0;

	if (filp->f_op && filp->f_op->release)
		error = filp->f_op->release(inode, filp);
	filp->f_dentry = NULL;
	if (filp->f_mode & FMODE_WRITE)
		put_write_access(inode);
	dput(dentry);
	return error;
}

/*
 * "id" is the POSIX thread ID. We use the
 * files pointer for this..
 */
int close_fp(struct file *filp, fl_owner_t id)
{
	struct dentry *dentry = filp->f_dentry;

	if (filp->f_count == 0) {
		printk("VFS: Close: file count is 0\n");
		return 0;
	}
	if (dentry->d_inode)
		locks_remove_posix(filp, id);
	return fput(filp);
}

/*
 * Careful here! We test whether the file pointer is NULL before
 * releasing the fd. This ensures that one clone task can't release
 * an fd while another clone is opening it.
 */
asmlinkage int sys_close(unsigned int fd)
{
	int error;
	struct file * filp;

	lock_kernel();
	error = -EBADF;
	filp = fcheck(fd);
	if (filp) {
		struct files_struct * files = current->files;
		files->fd[fd] = NULL;
		put_unused_fd(fd);
		FD_CLR(fd, &files->close_on_exec);
		error = close_fp(filp, files);
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
	if (!capable(CAP_SYS_TTY_CONFIG))
		goto out;
	/* If there is a controlling tty, hang it up */
	if (current->tty)
		tty_vhangup(current->tty);
	ret = 0;
out:
	unlock_kernel();
	return ret;
}
