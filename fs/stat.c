/*
 *  linux/fs/stat.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

/*
 * Revalidate the inode. This is required for proper NFS attribute caching.
 */
static __inline__ int
do_revalidate(struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;
	if (inode->i_op && inode->i_op->revalidate)
		return inode->i_op->revalidate(dentry);
	return 0;
}


#if !defined(__alpha__) && !defined(__sparc__)

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
static int cp_old_stat(struct inode * inode, struct __old_kernel_stat * statbuf)
{
	static int warncount = 5;
	struct __old_kernel_stat tmp;

	if (warncount) {
		warncount--;
		printk("VFS: Warning: %s using old stat() call. Recompile your binary.\n",
			current->comm);
	}

	tmp.st_dev = kdev_t_to_nr(inode->i_dev);
	tmp.st_ino = inode->i_ino;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlink;
	tmp.st_uid = inode->i_uid;
	tmp.st_gid = inode->i_gid;
	tmp.st_rdev = kdev_t_to_nr(inode->i_rdev);
	tmp.st_size = inode->i_size;
	tmp.st_atime = inode->i_atime;
	tmp.st_mtime = inode->i_mtime;
	tmp.st_ctime = inode->i_ctime;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

#endif

static int cp_new_stat(struct inode * inode, struct stat * statbuf)
{
	struct stat tmp;
	unsigned int blocks, indirect;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = kdev_t_to_nr(inode->i_dev);
	tmp.st_ino = inode->i_ino;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlink;
	tmp.st_uid = inode->i_uid;
	tmp.st_gid = inode->i_gid;
	tmp.st_rdev = kdev_t_to_nr(inode->i_rdev);
	tmp.st_size = inode->i_size;
	tmp.st_atime = inode->i_atime;
	tmp.st_mtime = inode->i_mtime;
	tmp.st_ctime = inode->i_ctime;
/*
 * st_blocks and st_blksize are approximated with a simple algorithm if
 * they aren't supported directly by the filesystem. The minix and msdos
 * filesystems don't keep track of blocks, so they would either have to
 * be counted explicitly (by delving into the file itself), or by using
 * this simple algorithm to get a reasonable (although not 100% accurate)
 * value.
 */

/*
 * Use minix fs values for the number of direct and indirect blocks.  The
 * count is now exact for the minix fs except that it counts zero blocks.
 * Everything is in units of BLOCK_SIZE until the assignment to
 * tmp.st_blksize.
 */
#define D_B   7
#define I_B   (BLOCK_SIZE / sizeof(unsigned short))

	if (!inode->i_blksize) {
		blocks = (tmp.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
		if (blocks > D_B) {
			indirect = (blocks - D_B + I_B - 1) / I_B;
			blocks += indirect;
			if (indirect > 1) {
				indirect = (indirect - 1 + I_B - 1) / I_B;
				blocks += indirect;
				if (indirect > 1)
					blocks++;
			}
		}
		tmp.st_blocks = (BLOCK_SIZE / 512) * blocks;
		tmp.st_blksize = BLOCK_SIZE;
	} else {
		tmp.st_blocks = inode->i_blocks;
		tmp.st_blksize = inode->i_blksize;
	}
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}


#if !defined(__alpha__) && !defined(__sparc__)
/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
asmlinkage int sys_stat(char * filename, struct __old_kernel_stat * statbuf)
{
	struct dentry * dentry;
	int error;

	lock_kernel();
	dentry = namei(filename);

	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		error = do_revalidate(dentry);
		if (!error)
			error = cp_old_stat(dentry->d_inode, statbuf);

		dput(dentry);
	}
	unlock_kernel();
	return error;
}
#endif

asmlinkage int sys_newstat(char * filename, struct stat * statbuf)
{
	struct dentry * dentry;
	int error;

	lock_kernel();
	dentry = namei(filename);

	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		error = do_revalidate(dentry);
		if (!error)
			error = cp_new_stat(dentry->d_inode, statbuf);

		dput(dentry);
	}
	unlock_kernel();
	return error;
}

#if !defined(__alpha__) && !defined(__sparc__)

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
asmlinkage int sys_lstat(char * filename, struct __old_kernel_stat * statbuf)
{
	struct dentry * dentry;
	int error;

	lock_kernel();
	dentry = lnamei(filename);

	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		error = do_revalidate(dentry);
		if (!error)
			error = cp_old_stat(dentry->d_inode, statbuf);

		dput(dentry);
	}
	unlock_kernel();
	return error;
}

#endif

asmlinkage int sys_newlstat(char * filename, struct stat * statbuf)
{
	struct dentry * dentry;
	int error;

	lock_kernel();
	dentry = lnamei(filename);

	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		error = do_revalidate(dentry);
		if (!error)
			error = cp_new_stat(dentry->d_inode, statbuf);

		dput(dentry);
	}
	unlock_kernel();
	return error;
}

#if !defined(__alpha__) && !defined(__sparc__)

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
asmlinkage int sys_fstat(unsigned int fd, struct __old_kernel_stat * statbuf)
{
	struct file * f;
	int err = -EBADF;

	lock_kernel();
	f = fget(fd);
	if (f) {
		struct dentry * dentry = f->f_dentry;

		err = do_revalidate(dentry);
		if (!err)
			err = cp_old_stat(dentry->d_inode, statbuf);
		fput(f);
	}
	unlock_kernel();
	return err;
}

#endif

asmlinkage int sys_newfstat(unsigned int fd, struct stat * statbuf)
{
	struct file * f;
	int err = -EBADF;

	lock_kernel();
	f = fget(fd);
	if (f) {
		struct dentry * dentry = f->f_dentry;

		err = do_revalidate(dentry);
		if (!err)
			err = cp_new_stat(dentry->d_inode, statbuf);
		fput(f);
	}
	unlock_kernel();
	return err;
}

asmlinkage int sys_readlink(const char * path, char * buf, int bufsiz)
{
	struct dentry * dentry;
	int error;

	if (bufsiz <= 0)
		return -EINVAL;

	lock_kernel();
	dentry = lnamei(path);

	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		struct inode * inode = dentry->d_inode;

		error = -EINVAL;
		if (inode->i_op && inode->i_op->readlink &&
		    !(error = do_revalidate(dentry))) {
			UPDATE_ATIME(inode);
			error = inode->i_op->readlink(dentry, buf, bufsiz);
		}
		dput(dentry);
	}
	unlock_kernel();
	return error;
}
