/*
 *  linux/fs/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

/* [Feb 1997 T. Schoebel-Theuer] Complete rewrite of the pathname
 * lookup logic.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <asm/semaphore.h>
#include <asm/namei.h>

/* This can be removed after the beta phase. */
#define CACHE_SUPERVISE	/* debug the correctness of dcache entries */
#undef DEBUG		/* some other debugging */


#define ACC_MODE(x) ("\000\004\002\006"[(x)&O_ACCMODE])

/* [Feb-1997 T. Schoebel-Theuer]
 * Fundamental changes in the pathname lookup mechanisms (namei)
 * were necessary because of omirr.  The reason is that omirr needs
 * to know the _real_ pathname, not the user-supplied one, in case
 * of symlinks (and also when transname replacements occur).
 *
 * The new code replaces the old recursive symlink resolution with
 * an iterative one (in case of non-nested symlink chains).  It does
 * this by looking up the symlink name from the particular filesystem,
 * and then follows this name as if it were a user-supplied one.  This
 * is done solely in the VFS level, such that <fs>_follow_link() is not
 * used any more and could be removed in future.  As a side effect,
 * dir_namei(), _namei() and follow_link() are now replaced with a single
 * function lookup_dentry() that can handle all the special cases of the former
 * code.
 *
 * With the new dcache, the pathname is stored at each inode, at least as
 * long as the refcount of the inode is positive.  As a side effect, the
 * size of the dcache depends on the inode cache and thus is dynamic.
 */

/* [24-Feb-97 T. Schoebel-Theuer] Side effects caused by new implementation:
 * New symlink semantics: when open() is called with flags O_CREAT | O_EXCL
 * and the name already exists in form of a symlink, try to create the new
 * name indicated by the symlink. The old code always complained that the
 * name already exists, due to not following the symlink even if its target
 * is non-existant.  The new semantics affects also mknod() and link() when
 * the name is a symlink pointing to a non-existant name.
 *
 * I don't know which semantics is the right one, since I have no access
 * to standards. But I found by trial that HP-UX 9.0 has the full "new"
 * semantics implemented, while SunOS 4.1.1 and Solaris (SunOS 5.4) have the
 * "old" one. Personally, I think the new semantics is much more logical.
 * Note that "ln old new" where "new" is a symlink pointing to a non-existing
 * file does succeed in both HP-UX and SunOs, but not in Solaris
 * and in the old Linux semantics.
 */

static char * quicklist = NULL;
static int quickcount = 0;
struct semaphore quicklock = MUTEX;

/* Tuning: increase locality by reusing same pages again...
 * if quicklist becomes too long on low memory machines, either a limit
 * should be added or after a number of cycles some pages should
 * be released again ...
 */
static inline char * get_page(void)
{
	char * res;
	down(&quicklock);
	res = quicklist;
	if (res) {
#ifdef DEBUG
		char * tmp = res;
		int i;
		for(i=0; i<quickcount; i++)
			tmp = *(char**)tmp;
		if (tmp)
			printk("bad quicklist %x\n", (int)tmp);
#endif
		quicklist = *(char**)res;
		quickcount--;
	}
	else
		res = (char*)__get_free_page(GFP_KERNEL);
	up(&quicklock);
	return res;
}

inline void putname(char * name)
{
	if (name) {
		down(&quicklock);
		*(char**)name = quicklist;
		quicklist = name;
		quickcount++;
		up(&quicklock);
	}
	/* if a quicklist limit is necessary to introduce, call
	 * free_page((unsigned long) name);
	 */
}

/* In order to reduce some races, while at the same time doing additional
 * checking and hopefully speeding things up, we copy filenames to the
 * kernel data space before using them..
 *
 * POSIX.1 2.4: an empty pathname is invalid (ENOENT).
 */
static inline int do_getname(const char *filename, char *page)
{
	int retval;
	unsigned long len = PAGE_SIZE;

	if ((unsigned long) filename >= TASK_SIZE) {
		if (get_fs() != KERNEL_DS)
			return -EFAULT;
	} else if (TASK_SIZE - (unsigned long) filename < PAGE_SIZE)
		len = TASK_SIZE - (unsigned long) filename;

	retval = strncpy_from_user((char *)page, filename, len);
	if (retval > 0) {
		if (retval < len)
			return 0;
		return -ENAMETOOLONG;
	} else if (!retval)
		retval = -ENOENT;
	return retval;
}

char * getname(const char * filename)
{
	char *tmp, *result;

	result = ERR_PTR(-ENOMEM);
	tmp = get_page();
	if (tmp)  {
		int retval = do_getname(filename, tmp);

		result = tmp;
		if (retval < 0) {
			putname(tmp);
			result = ERR_PTR(retval);
		}
	}
	return result;
}

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * We use "fsuid" for this, letting us set arbitrary permissions
 * for filesystem access without changing the "normal" uids which
 * are used for other things..
 */
int permission(struct inode * inode,int mask)
{
	int mode = inode->i_mode;

	if (inode->i_op && inode->i_op->permission)
		return inode->i_op->permission(inode, mask);
	else if ((mask & S_IWOTH) && IS_RDONLY(inode) &&
		 (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
		return -EROFS; /* Nobody gets write access to a read-only fs */
	else if ((mask & S_IWOTH) && IS_IMMUTABLE(inode))
		return -EACCES; /* Nobody gets write access to an immutable file */
	else if (current->fsuid == inode->i_uid)
		mode >>= 6;
	else if (in_group_p(inode->i_gid))
		mode >>= 3;
	if (((mode & mask & 0007) == mask) || fsuser())
		return 0;
	return -EACCES;
}

/*
 * get_write_access() gets write permission for a file.
 * put_write_access() releases this write permission.
 * This is used for regular files.
 * We cannot support write (and maybe mmap read-write shared) accesses and
 * MAP_DENYWRITE mmappings simultaneously. The i_writecount field of an inode
 * can have the following values:
 * 0: no writers, no VM_DENYWRITE mappings
 * < 0: (-i_writecount) vm_area_structs with VM_DENYWRITE set exist
 * > 0: (i_writecount) users are writing to the file.
 */
int get_write_access(struct inode * inode)
{
	if (inode->i_writecount < 0)
		return -ETXTBSY;
	inode->i_writecount++;
	return 0;
}

void put_write_access(struct inode * inode)
{
	inode->i_writecount--;
}

/*
 * This is called when everything else fails, and we actually have
 * to go to the low-level filesystem to find out what we should do..
 *
 * We get the directory semaphore, and after getting that we also
 * make sure that nobody added the entry to the dcache in the meantime..
 */
static struct dentry * real_lookup(struct dentry * parent, struct qstr * name)
{
	struct dentry * result;
	struct inode *dir = parent->d_inode;

	down(&dir->i_sem);
	result = d_lookup(parent, name);
	if (!result) {
		struct dentry * dentry = d_alloc(parent, name);
		int error = dir->i_op->lookup(dir, dentry);
		result = ERR_PTR(error);
		if (!error)
			result = dget(dentry->d_mounts);
		dput(dentry);
	}
	up(&dir->i_sem);
	return result;
}

/*
 * Internal lookup() using the new generic dcache.
 *
 * Note the revalidation: we have to drop the dcache
 * lock when we revalidate, so we need to update the
 * counts around it.
 */
static struct dentry * cached_lookup(struct dentry * parent, struct qstr * name)
{
	struct dentry * dentry = d_lookup(parent, name);

	if (dentry && dentry->d_op && dentry->d_op->d_revalidate) {
		int validated, (*revalidate)(struct dentry *) = dentry->d_op->d_revalidate;

		validated = revalidate(dentry) || d_invalidate(dentry);
		if (!validated) {
			dput(dentry);
			dentry = NULL;
		}
	}
	return dentry;
}

/*
 * "." and ".." are special - ".." especially so because it has to be able
 * to know about the current root directory and parent relationships
 */
static struct dentry * reserved_lookup(struct dentry * parent, struct qstr * name)
{
	struct dentry *result = NULL;
	if (name->name[0] == '.') {
		switch (name->len) {
		default:
			break;
		case 2:	
			if (name->name[1] != '.')
				break;

			if (parent != current->fs->root)
				parent = parent->d_covers->d_parent;
			/* fallthrough */
		case 1:
			result = parent;
		}
	}
	return dget(result);
}

/* In difference to the former version, lookup() no longer eats the dir. */
static inline struct dentry * lookup(struct dentry * dir, struct qstr * name)
{
	struct dentry * result;

	result = reserved_lookup(dir, name);
	if (result)
		goto done;

	result = cached_lookup(dir, name);
	if (result)
		goto done;

	result = real_lookup(dir, name);

done:
	return result;
}

static struct dentry * do_follow_link(struct dentry *base, struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;

	if (inode && inode->i_op && inode->i_op->follow_link) {
		if (current->link_count < 5) {
			struct dentry * result;

			current->link_count++;
			/* This eats the base */
			result = inode->i_op->follow_link(inode, base);
			current->link_count--;
			dput(dentry);
			return result;
		}
		dput(dentry);
		dentry = ERR_PTR(-ELOOP);
	}
	dput(base);
	return dentry;
}

/*
 * Name resolution.
 *
 * This is the basic name resolution function, turning a pathname
 * into the final dentry.
 */
struct dentry * lookup_dentry(const char * name, struct dentry * base, int follow_link)
{
	struct dentry * dentry;

	if (*name == '/') {
		if (base)
			dput(base);
		base = dget(current->fs->root);
		do {
			name++;
		} while (*name == '/');
	} else if (!base) {
		base = dget(current->fs->pwd);
	}

	if (!*name)
		goto return_base;

	/* At this point we know we have a real path component. */
	for(;;) {
		int len, err;
		unsigned long hash;
		struct qstr this;
		struct inode *inode;
		char c, follow;

		dentry = ERR_PTR(-ENOENT);
		inode = base->d_inode;
		if (!inode)
			break;

		dentry = ERR_PTR(-ENOTDIR);
		if (!inode->i_op || !inode->i_op->lookup)
			break;

		err = permission(inode, MAY_EXEC);
		dentry = ERR_PTR(err);
 		if (err)
			break;

		this.name = name;
		len = 0;
		c = *name;

		hash = init_name_hash();
		do {
			len++; name++;
			hash = partial_name_hash(c, hash);
			c = *name;
		} while (c && (c != '/'));
		this.len = len;
		this.hash = end_name_hash(hash);

		/* remove trailing slashes? */
		follow = follow_link;
		if (c) {
			follow |= c;
			do {
				c = *++name;
			} while (c == '/');
		}

		/*
		 * See if the low-level filesystem might want
		 * to use its own hash..
		 */
		if (base->d_op && base->d_op->d_hash) {
			int error;
			error = base->d_op->d_hash(base, &this);
			if (error < 0) {
				dentry = ERR_PTR(error);
				break;
			}
		}

		dentry = lookup(base, &this);
		if (IS_ERR(dentry))
			break;

		if (!follow)
			break;

		base = do_follow_link(base, dentry);
		if (c && !IS_ERR(base))
			continue;

return_base:
		return base;
	}
	dput(base);
	return dentry;
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 *
 * namei exists in two versions: namei/lnamei. The only difference is
 * that namei follows links, while lnamei does not.
 */
struct dentry * __namei(const char *pathname, int follow_link)
{
	char *name;
	struct dentry *dentry;

	name = getname(pathname);
	dentry = (struct dentry *) name;
	if (!IS_ERR(name)) {
		dentry = lookup_dentry(name, NULL, follow_link);
		putname(name);
		if (!IS_ERR(dentry)) {
			if (!dentry->d_inode) {
				dput(dentry);
				dentry = ERR_PTR(-ENOENT);
			}
		}
	}
	return dentry;
}

static inline struct dentry *get_parent(struct dentry *dentry)
{
	return dget(dentry->d_parent);
}

static inline void unlock_dir(struct dentry *dir)
{
	up(&dir->d_inode->i_sem);
	dput(dir);
}

/*
 * Locking the parent is needed to:
 *  - serialize directory operations
 *  - make sure the parent doesn't change from
 *    under us in the middle of an operation.
 *
 * NOTE! Right now we'd rather use a "struct inode"
 * for this, but as I expect things to move toward
 * using dentries instead for most things it is
 * probably better to start with the conceptually
 * better interface of relying on a path of dentries.
 */
static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir = dget(dentry->d_parent);

	down(&dir->d_inode->i_sem);

	/* Un-hashed or moved?  Punt if so.. */
	if (dir != dentry->d_parent || list_empty(&dentry->d_hash)) {
		if (dir != dentry) {
			unlock_dir(dir);
			dir = ERR_PTR(-ENOENT);
		}
	}
	return dir;
}

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 *
 * Note that the low bits of "flag" aren't the same as in the open
 * system call - they are 00 - no permissions needed
 *			  01 - read permission needed
 *			  10 - write permission needed
 *			  11 - read/write permissions needed
 * which is a lot more logical, and also allows the "no perm" needed
 * for symlinks (where the permissions are checked later).
 */
struct dentry * open_namei(const char * pathname, int flag, int mode)
{
	int acc_mode, error;
	struct inode *inode;
	struct dentry *dentry;

	mode &= S_IALLUGO & ~current->fs->umask;
	mode |= S_IFREG;

	dentry = lookup_dentry(pathname, NULL, 1);
	if (IS_ERR(dentry))
		return dentry;

	acc_mode = ACC_MODE(flag);
	if (flag & O_CREAT) {
		struct dentry *dir;

		dir = lock_parent(dentry);
		error = PTR_ERR(dir);
		if (IS_ERR(dir))
			goto exit;
		/*
		 * The existence test must be done _after_ getting the directory
		 * semaphore - the dentry might otherwise change.
		 */
		if (dentry->d_inode) {
			error = 0;
			if (flag & O_EXCL)
				error = -EEXIST;
		} else if (IS_RDONLY(dir->d_inode))
			error = -EROFS;
		else if (!dir->d_inode->i_op || !dir->d_inode->i_op->create)
			error = -EACCES;
		else if ((error = permission(dir->d_inode,MAY_WRITE | MAY_EXEC)) == 0) {
			if (dir->d_inode->i_sb && dir->d_inode->i_sb->dq_op)
				dir->d_inode->i_sb->dq_op->initialize(dir->d_inode, -1);
			error = dir->d_inode->i_op->create(dir->d_inode, dentry, mode);
			/* Don't check for write permission, don't truncate */
			acc_mode = 0;
			flag &= ~O_TRUNC;
		}
		unlock_dir(dir);
		if (error)
			goto exit;
	}

	error = -ENOENT;
	inode = dentry->d_inode;
	if (!inode)
		goto exit;

	error = -EISDIR;
	if (S_ISDIR(inode->i_mode) && (flag & FMODE_WRITE))
		goto exit;

	error = permission(inode,acc_mode);
	if (error)
		goto exit;

	/*
	 * FIFO's, sockets and device files are special: they don't
	 * actually live on the filesystem itself, and as such you
	 * can write to them even if the filesystem is read-only.
	 */
	if (S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
	    	flag &= ~O_TRUNC;
	} else if (S_ISBLK(inode->i_mode) || S_ISCHR(inode->i_mode)) {
		error = -EACCES;
		if (IS_NODEV(inode))
			goto exit;

		flag &= ~O_TRUNC;
	} else {
		error = -EROFS;
		if (IS_RDONLY(inode) && (flag & 2))
			goto exit;
	}
	/*
	 * An append-only file must be opened in append mode for writing.
	 */
	error = -EPERM;
	if (IS_APPEND(inode) && ((flag & FMODE_WRITE) && !(flag & O_APPEND)))
		goto exit;

	if (flag & O_TRUNC) {
		error = get_write_access(inode);
		if (error)
			goto exit;

		/*
		 * Refuse to truncate files with mandatory locks held on them.
		 */
		error = locks_verify_locked(inode);
		if (!error) {
			if (inode->i_sb && inode->i_sb->dq_op)
				inode->i_sb->dq_op->initialize(inode, -1);
			
			error = do_truncate(inode, 0);
		}
		put_write_access(inode);
		if (error)
			goto exit;
	} else
		if (flag & FMODE_WRITE)
			if (inode->i_sb && inode->i_sb->dq_op)
				inode->i_sb->dq_op->initialize(inode, -1);

	return dentry;

exit:
	dput(dentry);
	return ERR_PTR(error);
}

struct dentry * do_mknod(const char * filename, int mode, dev_t dev)
{
	int error;
	struct dentry *dir;
	struct dentry *dentry, *retval;

	mode &= ~current->fs->umask;
	dentry = lookup_dentry(filename, NULL, 1);
	if (IS_ERR(dentry))
		return dentry;

	dir = lock_parent(dentry);
	retval = dir;
	if (IS_ERR(dir))
		goto exit;

	retval = ERR_PTR(-EEXIST);
	if (dentry->d_inode)
		goto exit_lock;

	retval = ERR_PTR(-EROFS);
	if (IS_RDONLY(dir->d_inode))
		goto exit_lock;

	error = permission(dir->d_inode,MAY_WRITE | MAY_EXEC);
	retval = ERR_PTR(error);
	if (error)
		goto exit_lock;

	retval = ERR_PTR(-EPERM);
	if (!dir->d_inode->i_op || !dir->d_inode->i_op->mknod)
		goto exit_lock;

	if (dir->d_inode->i_sb && dir->d_inode->i_sb->dq_op)
		dir->d_inode->i_sb->dq_op->initialize(dir->d_inode, -1);
	error = dir->d_inode->i_op->mknod(dir->d_inode, dentry, mode, dev);
	retval = ERR_PTR(error);
	if (!error)
		retval = dget(dentry);

exit_lock:
	unlock_dir(dir);
exit:
	dput(dentry);
	return retval;
}

asmlinkage int sys_mknod(const char * filename, int mode, dev_t dev)
{
	int error;
	char * tmp;

	lock_kernel();
	error = -EPERM;
	if (S_ISDIR(mode) || (!S_ISFIFO(mode) && !fsuser()))
		goto out;
	error = -EINVAL;
	switch (mode & S_IFMT) {
	case 0:
		mode |= S_IFREG;
		break;
	case S_IFREG: case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
		break;
	default:
		goto out;
	}
	tmp = getname(filename);
	error = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		struct dentry * dentry = do_mknod(tmp,mode,dev);
		putname(tmp);
		error = PTR_ERR(dentry);
		if (!IS_ERR(dentry)) {
			dput(dentry);
			error = 0;
		}
	}
out:
	unlock_kernel();
	return error;
}

/*
 * Look out: this function may change a normal dentry
 * into a directory dentry (different size)..
 */
static inline int do_mkdir(const char * pathname, int mode)
{
	int error;
	struct dentry *dir;
	struct dentry *dentry;

	dentry = lookup_dentry(pathname, NULL, 1);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	dir = lock_parent(dentry);

	error = PTR_ERR(dir);
	if (IS_ERR(dir))
		goto exit;

	error = -EEXIST;
	if (dentry->d_inode)
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(dir->d_inode))
		goto exit_lock;

	error = permission(dir->d_inode,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->d_inode->i_op || !dir->d_inode->i_op->mkdir)
		goto exit_lock;

	if (dir->d_inode->i_sb && dir->d_inode->i_sb->dq_op)
		dir->d_inode->i_sb->dq_op->initialize(dir->d_inode, -1);
	mode &= 0777 & ~current->fs->umask;
	error = dir->d_inode->i_op->mkdir(dir->d_inode, dentry, mode);

exit_lock:
	unlock_dir(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage int sys_mkdir(const char * pathname, int mode)
{
	int error;
	char * tmp;

	lock_kernel();
	tmp = getname(pathname);
	error = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		error = do_mkdir(tmp,mode);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

static inline int do_rmdir(const char * name)
{
	int error;
	struct dentry *dir;
	struct dentry *dentry;

	dentry = lookup_dentry(name, NULL, 0);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	dir = lock_parent(dentry);

	error = PTR_ERR(dir);
	if (IS_ERR(dir))
		goto exit;

	error = -ENOENT;
	if (!dentry->d_inode)
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(dir->d_inode))
		goto exit_lock;

	error = permission(dir->d_inode,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	/*
	 * A subdirectory cannot be removed from an append-only directory.
	 */
	error = -EPERM;
	if (IS_APPEND(dir->d_inode))
		goto exit_lock;

	/* Disallow removals of mountpoints. */
	error = -EBUSY;
	if (dentry == dir)
		goto exit_lock;

	error = -EPERM;
	if (!dir->d_inode->i_op || !dir->d_inode->i_op->rmdir)
		goto exit_lock;

	if (dir->d_inode->i_sb && dir->d_inode->i_sb->dq_op)
		dir->d_inode->i_sb->dq_op->initialize(dir->d_inode, -1);

	error = dir->d_inode->i_op->rmdir(dir->d_inode, dentry);

exit_lock:
        unlock_dir(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage int sys_rmdir(const char * pathname)
{
	int error;
	char * tmp;

	lock_kernel();
	tmp = getname(pathname);
	error = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		error = do_rmdir(tmp);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

static inline int do_unlink(const char * name)
{
	int error;
	struct dentry *dir;
	struct dentry *dentry;

	dentry = lookup_dentry(name, NULL, 0);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	dir = lock_parent(dentry);

	error = PTR_ERR(dir);
	if (IS_ERR(dir))
		goto exit;

	error = -ENOENT;
	if (!dentry->d_inode)
		goto exit_lock;

	/* Mount point? */
	error = -EBUSY;
	if (dentry == dir)
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(dir->d_inode))
		goto exit_lock;

	error = permission(dir->d_inode,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	/*
	 * A file cannot be removed from an append-only directory.
	 */
	error = -EPERM;
	if (IS_APPEND(dir->d_inode))
		goto exit_lock;

	error = -EPERM;
	if (!dir->d_inode->i_op || !dir->d_inode->i_op->unlink)
		goto exit_lock;

	if (dir->d_inode->i_sb && dir->d_inode->i_sb->dq_op)
		dir->d_inode->i_sb->dq_op->initialize(dir->d_inode, -1);

	error = dir->d_inode->i_op->unlink(dir->d_inode, dentry);

exit_lock:
        unlock_dir(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage int sys_unlink(const char * pathname)
{
	int error;
	char * tmp;

	lock_kernel();
	tmp = getname(pathname);
	error = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		error = do_unlink(tmp);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

static inline int do_symlink(const char * oldname, const char * newname)
{
	int error;
	struct dentry *dir;
	struct dentry *dentry;

	dentry = lookup_dentry(newname, NULL, 0);

	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	dir = lock_parent(dentry);

	error = PTR_ERR(dir);
	if (IS_ERR(dir))
		goto exit;

	error = -EEXIST;
	if (dentry->d_inode)
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(dir->d_inode))
		goto exit_lock;

	error = permission(dir->d_inode,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->d_inode->i_op || !dir->d_inode->i_op->symlink)
		goto exit_lock;

	if (dir->d_inode->i_sb && dir->d_inode->i_sb->dq_op)
		dir->d_inode->i_sb->dq_op->initialize(dir->d_inode, -1);
	error = dir->d_inode->i_op->symlink(dir->d_inode, dentry, oldname);

exit_lock:
	unlock_dir(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage int sys_symlink(const char * oldname, const char * newname)
{
	int error;
	char * from;

	lock_kernel();
	from = getname(oldname);
	error = PTR_ERR(from);
	if (!IS_ERR(from)) {
		char * to;
		to = getname(newname);
		error = PTR_ERR(to);
		if (!IS_ERR(to)) {
			error = do_symlink(from,to);
			putname(to);
		}
		putname(from);
	}
	unlock_kernel();
	return error;
}

static inline int do_link(const char * oldname, const char * newname)
{
	struct dentry *old_dentry, *new_dentry, *dir;
	struct inode *inode;
	int error;

	old_dentry = lookup_dentry(oldname, NULL, 1);
	error = PTR_ERR(old_dentry);
	if (IS_ERR(old_dentry))
		goto exit;

	new_dentry = lookup_dentry(newname, NULL, 1);
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto exit_old;

	dir = lock_parent(new_dentry);

	error = PTR_ERR(dir);
	if (IS_ERR(dir))
		goto exit;

	error = -ENOENT;
	inode = old_dentry->d_inode;
	if (!inode)
		goto exit_lock;

	error = -EEXIST;
	if (new_dentry->d_inode)
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(dir->d_inode))
		goto exit_lock;

	error = -EXDEV;
	if (dir->d_inode->i_dev != inode->i_dev)
		goto exit_lock;

	error = permission(dir->d_inode, MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	/*
	 * A link to an append-only or immutable file cannot be created.
	 */
	error = -EPERM;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		goto exit_lock;

	error = -EPERM;
	if (!dir->d_inode->i_op || !dir->d_inode->i_op->link)
		goto exit_lock;

	if (dir->d_inode->i_sb && dir->d_inode->i_sb->dq_op)
		dir->d_inode->i_sb->dq_op->initialize(dir->d_inode, -1);
	error = dir->d_inode->i_op->link(inode, dir->d_inode, new_dentry);

exit_lock:
	unlock_dir(dir);
	dput(new_dentry);
exit_old:
	dput(old_dentry);
exit:
	return error;
}

asmlinkage int sys_link(const char * oldname, const char * newname)
{
	int error;
	char * from;

	lock_kernel();
	from = getname(oldname);
	error = PTR_ERR(from);
	if (!IS_ERR(from)) {
		char * to;
		to = getname(newname);
		error = PTR_ERR(to);
		if (!IS_ERR(to)) {
			error = do_link(from,to);
			putname(to);
		}
		putname(from);
	}
	unlock_kernel();
	return error;
}

/*
 * Whee.. Deadlock country. Happily there is only one VFS
 * operation that does this..
 */
static inline void double_lock(struct dentry *d1, struct dentry *d2)
{
	struct semaphore *s1 = &d1->d_inode->i_sem;
	struct semaphore *s2 = &d2->d_inode->i_sem;

	if (s1 != s2) {
		if ((unsigned long) s1 < (unsigned long) s2) {
			struct semaphore *tmp = s2;
			s2 = s1; s1 = tmp;
		}
		down(s1);
	}
	down(s2);
}

static inline void double_unlock(struct dentry *d1, struct dentry *d2)
{
	struct semaphore *s1 = &d1->d_inode->i_sem;
	struct semaphore *s2 = &d2->d_inode->i_sem;

	up(s1);
	if (s1 != s2)
		up(s2);
	dput(d1);
	dput(d2);
}

static inline int do_rename(const char * oldname, const char * newname)
{
	int error;
	struct dentry * old_dir, * new_dir;
	struct dentry * old_dentry, *new_dentry;

	old_dentry = lookup_dentry(oldname, NULL, 0);

	error = PTR_ERR(old_dentry);
	if (IS_ERR(old_dentry))
		goto exit;

	new_dentry = lookup_dentry(newname, NULL, 0);

	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto exit_old;

	new_dir = get_parent(new_dentry);
	old_dir = get_parent(old_dentry);

	double_lock(new_dir, old_dir);

	error = -ENOENT;
	if (!old_dentry->d_inode)
		goto exit_lock;

	error = permission(old_dir->d_inode,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;
	error = permission(new_dir->d_inode,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	/* Disallow moves of mountpoints. */
	error = -EBUSY;
	if (old_dir == old_dentry || new_dir == new_dentry)
		goto exit_lock;

	error = -EXDEV;
	if (new_dir->d_inode->i_dev != old_dir->d_inode->i_dev)
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(new_dir->d_inode) || IS_RDONLY(old_dir->d_inode))
		goto exit_lock;

	/*
	 * A file cannot be removed from an append-only directory.
	 */
	error = -EPERM;
	if (IS_APPEND(old_dir->d_inode))
		goto exit_lock;

	error = -EPERM;
	if (!old_dir->d_inode->i_op || !old_dir->d_inode->i_op->rename)
		goto exit_lock;

	if (new_dir->d_inode->i_sb && new_dir->d_inode->i_sb->dq_op)
		new_dir->d_inode->i_sb->dq_op->initialize(new_dir->d_inode, -1);
	error = old_dir->d_inode->i_op->rename(old_dir->d_inode, old_dentry, new_dir->d_inode, new_dentry);

exit_lock:
	double_unlock(new_dir, old_dir);
	dput(new_dentry);
exit_old:
	dput(old_dentry);
exit:
	return error;
}

asmlinkage int sys_rename(const char * oldname, const char * newname)
{
	int error;
	char * from;

	lock_kernel();
	from = getname(oldname);
	error = PTR_ERR(from);
	if (!IS_ERR(from)) {
		char * to;
		to = getname(newname);
		error = PTR_ERR(to);
		if (!IS_ERR(to)) {
			error = do_rename(from,to);
			putname(to);
		}
		putname(from);
	}
	unlock_kernel();
	return error;
}
