/*
 *  linux/fs/open.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/utime.h>
#include <linux/file.h>
#include <linux/smp_lock.h>
#include <linux/quotaops.h>

#include <asm/uaccess.h>

asmlinkage long sys_statfs(const char * path, struct statfs * buf)
{
	struct dentry * dentry;
	int error;

	lock_kernel();
	dentry = namei(path);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		struct statfs tmp;
		error = vfs_statfs(dentry->d_inode->i_sb, &tmp);
		if (!error && copy_to_user(buf, &tmp, sizeof(struct statfs)))
			error = -EFAULT;
		dput(dentry);
	}
	unlock_kernel();
	return error;
}

asmlinkage long sys_fstatfs(unsigned int fd, struct statfs * buf)
{
	struct file * file;
	struct statfs tmp;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	lock_kernel();
	error = vfs_statfs(file->f_dentry->d_inode->i_sb, &tmp);
	if (!error && copy_to_user(buf, &tmp, sizeof(struct statfs)))
		error = -EFAULT;
	unlock_kernel();
	fput(file);
out:
	return error;
}

int do_truncate(struct dentry *dentry, loff_t length)
{
	struct inode *inode = dentry->d_inode;
	int error;
	struct iattr newattrs;

	/* Not pretty: "inode->i_size" shouldn't really be signed. But it is. */
	if (length < 0)
		return -EINVAL;

	down(&inode->i_sem);
	newattrs.ia_size = length;
	newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME;
	error = notify_change(dentry, &newattrs);
	up(&inode->i_sem);
	return error;
}

static inline long do_sys_truncate(const char * path, loff_t length)
{
	struct dentry * dentry;
	struct inode * inode;
	int error;

	lock_kernel();

	error = -EINVAL;
	if (length < 0)	/* sorry, but loff_t says... */
		goto out;

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

	error = locks_verify_truncate(inode, NULL, length);
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

asmlinkage long sys_truncate(const char * path, unsigned long length)
{
	return do_sys_truncate(path, length);
}

static inline long do_sys_ftruncate(unsigned int fd, loff_t length)
{
	struct inode * inode;
	struct dentry *dentry;
	struct file * file;
	int error;

	error = -EINVAL;
	if (length < 0)
		goto out;
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
	error = locks_verify_truncate(inode, file, length);
	lock_kernel();
	if (!error)
		error = do_truncate(dentry, length);
	unlock_kernel();
out_putf:
	fput(file);
out:
	return error;
}

asmlinkage long sys_ftruncate(unsigned int fd, unsigned long length)
{
	return do_sys_ftruncate(fd, length);
}

/* LFS versions of truncate are only needed on 32 bit machines */
#if BITS_PER_LONG == 32
asmlinkage long sys_truncate64(const char * path, loff_t length)
{
	return do_sys_truncate(path, length);
}

asmlinkage long sys_ftruncate64(unsigned int fd, loff_t length)
{
	return do_sys_ftruncate(fd, length);
}
#endif

#if !(defined(__alpha__) || defined(__ia64__))

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
asmlinkage long sys_utime(char * filename, struct utimbuf * times)
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
asmlinkage long sys_utimes(char * filename, struct timeval * utimes)
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
asmlinkage long sys_access(const char * filename, int mode)
{
	struct dentry * dentry;
	int old_fsuid, old_fsgid;
	kernel_cap_t old_cap;
	int res;

	if (mode & ~S_IRWXO)	/* where's F_OK, X_OK, W_OK, R_OK? */
		return -EINVAL;

	old_fsuid = current->fsuid;
	old_fsgid = current->fsgid;
	old_cap = current->cap_effective;

	current->fsuid = current->uid;
	current->fsgid = current->gid;

	/* Clear the capabilities if we switch to a non-root user */
	if (current->uid)
		cap_clear(current->cap_effective);
	else
		current->cap_effective = current->cap_permitted;

	lock_kernel();		
	dentry = namei(filename);
	res = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		res = permission(dentry->d_inode, mode);
		/* SuS v2 requires we report a read only fs too */
		if(!res && (mode & S_IWOTH) && IS_RDONLY(dentry->d_inode))
			res = -EROFS;
		dput(dentry);
	}
	unlock_kernel();

	current->fsuid = old_fsuid;
	current->fsgid = old_fsgid;
	current->cap_effective = old_cap;

	return res;
}

asmlinkage long sys_chdir(const char * filename)
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

asmlinkage long sys_fchdir(unsigned int fd)
{
	struct file *file;
	struct dentry *dentry;
	struct inode *inode;
	int error;

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

	lock_kernel();
	error = permission(inode, MAY_EXEC);
	if (!error) {
		struct dentry *tmp = current->fs->pwd;
		current->fs->pwd = dget(dentry);
		dput(tmp);
	}
	unlock_kernel();
out_putf:
	fput(file);
out:
	return error;
}

asmlinkage long sys_chroot(const char * filename)
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

asmlinkage long sys_fchmod(unsigned int fd, mode_t mode)
{
	struct inode * inode;
	struct dentry * dentry;
	struct file * file;
	int err = -EBADF;
	struct iattr newattrs;

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
	lock_kernel();
	err = notify_change(dentry, &newattrs);
	unlock_kernel();

out_putf:
	fput(file);
out:
	return err;
}

asmlinkage long sys_chmod(const char * filename, mode_t mode)
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
	 * If the user or group of a non-directory has been changed by a
	 * non-root user, remove the setuid bit.
	 * 19981026	David C Niemi <niemi@tux.org>
	 *
	 * Changed this to apply to all users, including root, to avoid
	 * some races. This is the behavior we had in 2.0. The check for
	 * non-root was definitely wrong for 2.2 anyway, as it should
	 * have been using CAP_FSETID rather than fsuid -- 19990830 SD.
	 */
	if ((inode->i_mode & S_ISUID) == S_ISUID &&
		!S_ISDIR(inode->i_mode))
	{
		newattrs.ia_mode &= ~S_ISUID;
		newattrs.ia_valid |= ATTR_MODE;
	}
	/*
	 * Likewise, if the user or group of a non-directory has been changed
	 * by a non-root user, remove the setgid bit UNLESS there is no group
	 * execute bit (this would be a file marked for mandatory locking).
	 * 19981026	David C Niemi <niemi@tux.org>
	 *
	 * Removed the fsuid check (see the comment above) -- 19990830 SD.
	 */
	if (((inode->i_mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) 
		&& !S_ISDIR(inode->i_mode))
	{
		newattrs.ia_mode &= ~S_ISGID;
		newattrs.ia_valid |= ATTR_MODE;
	}
	error = DQUOT_TRANSFER(dentry, &newattrs);
out:
	return error;
}

asmlinkage long sys_chown(const char * filename, uid_t user, gid_t group)
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

asmlinkage long sys_lchown(const char * filename, uid_t user, gid_t group)
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


asmlinkage long sys_fchown(unsigned int fd, uid_t user, gid_t group)
{
	struct dentry * dentry;
	struct file * file;
	int error = -EBADF;

	file = fget(fd);
	if (!file)
		goto out;
	error = -ENOENT;
	lock_kernel();
	if ((dentry = file->f_dentry) != NULL)
		error = chown_common(dentry, user, group);
	unlock_kernel();
	fput(file);

out:
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
struct file *filp_open(const char * filename, int flags, int mode, struct dentry * base)
{
	struct dentry * dentry;
	int flag,error;

	flag = flags;
	if ((flag+1) & O_ACCMODE)
		flag++;
	if (flag & O_TRUNC)
		flag |= 2;

	dentry = __open_namei(filename, flag, mode, base);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry))
		return dentry_open(dentry, flags);

	return ERR_PTR(error);
}

struct file *dentry_open(struct dentry *dentry, int flags)
{
	struct file * f;
	struct inode *inode;
	int error;

	error = -ENFILE;
	f = get_empty_filp();
	if (!f)
		goto cleanup_dentry;
	f->f_flags = flags;
	f->f_mode = (flags+1) & O_ACCMODE;
	inode = dentry->d_inode;
	if (f->f_mode & FMODE_WRITE) {
		error = get_write_access(inode);
		if (error)
			goto cleanup_dentry;
	}

	f->f_dentry = dentry;
	f->f_pos = 0;
	f->f_reada = 0;
	f->f_op = inode->i_fop;
	if (inode->i_sb)
		file_move(f, &inode->i_sb->s_files);
	if (f->f_op && f->f_op->open) {
		error = f->f_op->open(inode,f);
		if (error)
			goto cleanup_all;
	}
	f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	return f;

cleanup_all:
	if (f->f_mode & FMODE_WRITE)
		put_write_access(inode);
	f->f_dentry = NULL;
cleanup_dentry:
	dput(dentry);
	put_filp(f);
	return ERR_PTR(error);
}

/*
 * Find an empty file descriptor entry, and mark it busy.
 */
int get_unused_fd(void)
{
	struct files_struct * files = current->files;
	int fd, error;

  	error = -EMFILE;
	write_lock(&files->file_lock);

repeat:
 	fd = find_next_zero_bit(files->open_fds, 
				current->files->max_fdset, 
				files->next_fd);

	/*
	 * N.B. For clone tasks sharing a files structure, this test
	 * will limit the total number of files that can be opened.
	 */
	if (fd >= current->rlim[RLIMIT_NOFILE].rlim_cur)
		goto out;

	/* Do we need to expand the fdset array? */
	if (fd >= current->files->max_fdset) {
		error = expand_fdset(files, fd);
		if (!error) {
			error = -EMFILE;
			goto repeat;
		}
		goto out;
	}
	
	/* 
	 * Check whether we need to expand the fd array.
	 */
	if (fd >= files->max_fds) {
		error = expand_fd_array(files, fd);
		if (!error) {
			error = -EMFILE;
			goto repeat;
		}
		goto out;
	}

	FD_SET(fd, files->open_fds);
	FD_CLR(fd, files->close_on_exec);
	files->next_fd = fd + 1;
#if 1
	/* Sanity check */
	if (files->fd[fd] != NULL) {
		printk("get_unused_fd: slot %d not NULL!\n", fd);
		files->fd[fd] = NULL;
	}
#endif
	error = fd;

out:
	write_unlock(&files->file_lock);
	return error;
}

inline void put_unused_fd(unsigned int fd)
{
	write_lock(&current->files->file_lock);
	FD_CLR(fd, current->files->open_fds);
	if (fd < current->files->next_fd)
		current->files->next_fd = fd;
	write_unlock(&current->files->file_lock);
}

asmlinkage long sys_open(const char * filename, int flags, int mode)
{
	char * tmp;
	int fd, error;

#if BITS_PER_LONG != 32
	flags |= O_LARGEFILE;
#endif
	tmp = getname(filename);
	fd = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		fd = get_unused_fd();
		if (fd >= 0) {
			struct file * f;
			lock_kernel();
			f = filp_open(tmp, flags, mode, NULL);
			unlock_kernel();
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

#ifndef __alpha__

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
asmlinkage long sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

#endif

/*
 * "id" is the POSIX thread ID. We use the
 * files pointer for this..
 */
int filp_close(struct file *filp, fl_owner_t id)
{
	int retval;
	struct dentry *dentry = filp->f_dentry;

	if (!file_count(filp)) {
		printk("VFS: Close: file count is 0\n");
		return 0;
	}
	retval = 0;
	if (filp->f_op && filp->f_op->flush)
		retval = filp->f_op->flush(filp);
	if (dentry->d_inode)
		locks_remove_posix(filp, id);
	fput(filp);
	return retval;
}

/*
 * Careful here! We test whether the file pointer is NULL before
 * releasing the fd. This ensures that one clone task can't release
 * an fd while another clone is opening it.
 *
 * The "release" argument tells us whether or not to mark the fd as free
 * or not in the open-files bitmap.  dup2 uses this to retain the fd
 * without races.
 */
int do_close(unsigned int fd, int release)
{
	int error;
	struct file * filp;
	struct files_struct * files = current->files;

	error = -EBADF;
	write_lock(&files->file_lock);
	filp = frip(fd);
	if (!filp)
		goto out_unlock;
	FD_CLR(fd, files->close_on_exec);
	write_unlock(&files->file_lock);
	if (release)
		put_unused_fd(fd);
	lock_kernel();
	error = filp_close(filp, files);
	unlock_kernel();
out:
	return error;
out_unlock:
	write_unlock(&files->file_lock);
	goto out;
}

asmlinkage long sys_close(unsigned int fd)
{
	return do_close(fd, 1);
}

/*
 * This routine simulates a hangup on the tty, to arrange that users
 * are given clean terminals at login time.
 */
asmlinkage long sys_vhangup(void)
{
	if (capable(CAP_SYS_TTY_CONFIG)) {
		tty_vhangup(current->tty);
		return 0;
	}
	return -EPERM;
}
