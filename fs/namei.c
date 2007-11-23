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

#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/quotaops.h>
#include <linux/pagemap.h>
#include <linux/dcache.h>

#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <asm/semaphore.h>
#include <asm/page.h>
#include <asm/pgtable.h>

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
 * this with calls to <fs>_follow_link().
 * As a side effect, dir_namei(), _namei() and follow_link() are now 
 * replaced with a single function lookup_dentry() that can handle all 
 * the special cases of the former code.
 *
 * With the new dcache, the pathname is stored at each inode, at least as
 * long as the refcount of the inode is positive.  As a side effect, the
 * size of the dcache depends on the inode cache and thus is dynamic.
 *
 * [29-Apr-1998 C. Scott Ananian] Updated above description of symlink
 * resolution to correspond with current state of the code.
 *
 * Note that the symlink resolution is not *completely* iterative.
 * There is still a significant amount of tail- and mid- recursion in
 * the algorithm.  Also, note that <fs>_readlink() is not used in
 * lookup_dentry(): lookup_dentry() on the result of <fs>_readlink()
 * may return different results than <fs>_follow_link().  Many virtual
 * filesystems (including /proc) exhibit this behavior.
 */

/* [24-Feb-97 T. Schoebel-Theuer] Side effects caused by new implementation:
 * New symlink semantics: when open() is called with flags O_CREAT | O_EXCL
 * and the name already exists in form of a symlink, try to create the new
 * name indicated by the symlink. The old code always complained that the
 * name already exists, due to not following the symlink even if its target
 * is nonexistent.  The new semantics affects also mknod() and link() when
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

/* [16-Dec-97 Kevin Buhr] For security reasons, we change some symlink
 * semantics.  See the comments in "open_namei" and "do_link" below.
 *
 * [10-Sep-98 Alan Modra] Another symlink change.
 */

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
		if (!segment_eq(get_fs(), KERNEL_DS))
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
	tmp = __getname();
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
	if (((mode & mask & S_IRWXO) == mask) || capable(CAP_DAC_OVERRIDE))
		return 0;
	/* read and search access */
	if ((mask == S_IROTH) ||
	    (S_ISDIR(mode)  && !(mask & ~(S_IROTH | S_IXOTH))))
		if (capable(CAP_DAC_READ_SEARCH))
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
 *
 * WARNING: as soon as we will move get_write_access(), do_mmap() or
 * prepare_binfmt() out of the big lock we will need a spinlock protecting
 * the checks in all 3. For the time being it is not needed.
 */
int get_write_access(struct inode * inode)
{
	if (atomic_read(&inode->i_writecount) < 0)
		return -ETXTBSY;
	atomic_inc(&inode->i_writecount);
	return 0;
}

void put_write_access(struct inode * inode)
{
	atomic_dec(&inode->i_writecount);
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

/*
 * Internal lookup() using the new generic dcache.
 */
static struct dentry * cached_lookup(struct dentry * parent, struct qstr * name, int flags)
{
	struct dentry * dentry = d_lookup(parent, name);

	if (dentry && dentry->d_op && dentry->d_op->d_revalidate) {
		if (!dentry->d_op->d_revalidate(dentry, flags) && !d_invalidate(dentry)) {
			dput(dentry);
			dentry = NULL;
		}
	}
	return dentry;
}

/*
 * This is called when everything else fails, and we actually have
 * to go to the low-level filesystem to find out what we should do..
 *
 * We get the directory semaphore, and after getting that we also
 * make sure that nobody added the entry to the dcache in the meantime..
 */
static struct dentry * real_lookup(struct dentry * parent, struct qstr * name, int flags)
{
	struct dentry * result;
	struct inode *dir = parent->d_inode;

	down(&dir->i_sem);
	/*
	 * First re-do the cached lookup just in case it was created
	 * while we waited for the directory semaphore..
	 *
	 * FIXME! This could use version numbering or similar to
	 * avoid unnecessary cache lookups.
	 */
	result = d_lookup(parent, name);
	if (!result) {
		struct dentry * dentry = d_alloc(parent, name);
		result = ERR_PTR(-ENOMEM);
		if (dentry) {
			result = dir->i_op->lookup(dir, dentry);
			if (result)
				dput(dentry);
			else
				result = dentry;
		}
		up(&dir->i_sem);
		return result;
	}

	/*
	 * Uhhuh! Nasty case: the cache was re-populated while
	 * we waited on the semaphore. Need to revalidate, but
	 * we're going to return this entry regardless (same
	 * as if it was busy).
	 */
	up(&dir->i_sem);
	if (result->d_op && result->d_op->d_revalidate)
		result->d_op->d_revalidate(result, flags);
	return result;
}

static struct dentry * do_follow_link(struct dentry *base, struct dentry *dentry, unsigned int follow)
{
	struct inode * inode = dentry->d_inode;

	if ((follow & LOOKUP_FOLLOW)
	    && inode && inode->i_op && inode->i_op->follow_link) {
		if (current->link_count < 32) {
			struct dentry * result;

			current->link_count++;
			/* This eats the base */
			result = inode->i_op->follow_link(dentry, base, follow);
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

static inline struct dentry * follow_mount(struct dentry * dentry)
{
	struct dentry * mnt = dentry->d_mounts;

	if (mnt != dentry) {
		dget(mnt);
		dput(dentry);
		dentry = mnt;
	}
	return dentry;
}

/*
 * Name resolution.
 *
 * This is the basic name resolution function, turning a pathname
 * into the final dentry.
 */
struct dentry * lookup_dentry(const char * name, struct dentry * base, unsigned int lookup_flags)
{
	struct dentry * dentry;
	struct inode *inode;

	if (*name == '/') {
		if (base)
			dput(base);
		do {
			name++;
		} while (*name == '/');
		__prefix_lookup_dentry(name, lookup_flags);
		base = dget(current->fs->root);
	} else if (!base) {
		base = dget(current->fs->pwd);
	}

	if (!*name)
		goto return_base;

	inode = base->d_inode;
	lookup_flags &= LOOKUP_FOLLOW | LOOKUP_DIRECTORY | LOOKUP_SLASHOK;

	/* At this point we know we have a real path component. */
	for(;;) {
		int err;
		unsigned long hash;
		struct qstr this;
		unsigned int flags;
		unsigned int c;

		err = permission(inode, MAY_EXEC);
		dentry = ERR_PTR(err);
 		if (err)
			break;

		this.name = name;
		c = *(const unsigned char *)name;

		hash = init_name_hash();
		do {
			name++;
			hash = partial_name_hash(c, hash);
			c = *(const unsigned char *)name;
		} while (c && (c != '/'));
		this.len = name - (const char *) this.name;
		this.hash = end_name_hash(hash);

		/* remove trailing slashes? */
		flags = lookup_flags;
		if (c) {
			char tmp;

			flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;
			do {
				tmp = *++name;
			} while (tmp == '/');
			if (tmp)
				flags |= LOOKUP_CONTINUE;
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

		/* This does the actual lookups.. */
		dentry = reserved_lookup(base, &this);
		if (!dentry) {
			dentry = cached_lookup(base, &this, flags);
			if (!dentry) {
				dentry = real_lookup(base, &this, flags);
				if (IS_ERR(dentry))
					break;
			}

			/* Check mountpoints.. */
			dentry = follow_mount(dentry);
		}

		base = do_follow_link(base, dentry, flags);
		if (IS_ERR(base))
			goto return_base;

		inode = base->d_inode;
		if (flags & LOOKUP_DIRECTORY) {
			if (!inode)
				goto no_inode;
			dentry = ERR_PTR(-ENOTDIR); 
			if (!inode->i_op || !inode->i_op->lookup)
				break;
			if (flags & LOOKUP_CONTINUE)
				continue;
		}
return_base:
		return base;
/*
 * The case of a nonexisting file is special.
 *
 * In the middle of a pathname lookup (ie when
 * LOOKUP_CONTINUE is set), it's an obvious
 * error and returns ENOENT.
 *
 * At the end of a pathname lookup it's legal,
 * and we return a negative dentry. However, we
 * get here only if there were trailing slashes,
 * which is legal only if we know it's supposed
 * to be a directory (ie "mkdir"). Thus the
 * LOOKUP_SLASHOK flag.
 */
no_inode:
		dentry = ERR_PTR(-ENOENT);
		if (flags & LOOKUP_CONTINUE)
			break;
		if (flags & LOOKUP_SLASHOK)
			goto return_base;
		break;
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
struct dentry * __namei(const char *pathname, unsigned int lookup_flags)
{
	char *name;
	struct dentry *dentry;

	name = getname(pathname);
	dentry = (struct dentry *) name;
	if (!IS_ERR(name)) {
		dentry = lookup_dentry(name, NULL, lookup_flags);
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

/*
 * It's inline, so penalty for filesystems that don't use sticky bit is
 * minimal.
 */
static inline int check_sticky(struct inode *dir, struct inode *inode)
{
	if (!(dir->i_mode & S_ISVTX))
		return 0;
	if (inode->i_uid == current->fsuid)
		return 0;
	if (dir->i_uid == current->fsuid)
		return 0;
	return !capable(CAP_FOWNER);
}

/*
 *	Check whether we can remove a link victim from directory dir, check
 *  whether the type of victim is right.
 *  1. We can't do it if dir is read-only (done in permission())
 *  2. We should have write and exec permissions on dir
 *  3. We can't remove anything from append-only dir
 *  4. We can't do anything with immutable dir (done in permission())
 *  5. If the sticky bit on dir is set we should either
 *	a. be owner of dir, or
 *	b. be owner of victim, or
 *	c. have CAP_FOWNER capability
 *  6. If the victim is append-only or immutable we can't do antyhing with
 *     links pointing to it.
 *  7. If we were asked to remove a directory and victim isn't one - ENOTDIR.
 *  8. If we were asked to remove a non-directory and victim isn't one - EISDIR.
 *  9. We can't remove a root or mountpoint.
 */
static inline int may_delete(struct inode *dir,struct dentry *victim, int isdir)
{
	int error;
	if (!victim->d_inode || victim->d_parent->d_inode != dir)
		return -ENOENT;
	error = permission(dir,MAY_WRITE | MAY_EXEC);
	if (error)
		return error;
	if (IS_APPEND(dir))
		return -EPERM;
	if (check_sticky(dir, victim->d_inode)||IS_APPEND(victim->d_inode)||
	    IS_IMMUTABLE(victim->d_inode))
		return -EPERM;
	if (isdir) {
		if (!S_ISDIR(victim->d_inode->i_mode))
			return -ENOTDIR;
		if (IS_ROOT(victim))
			return -EBUSY;
		if (victim->d_mounts != victim->d_covers)
			return -EBUSY;
	} else if (S_ISDIR(victim->d_inode->i_mode))
		return -EISDIR;
	return 0;
}

/*	Check whether we can create an object with dentry child in directory
 *  dir.
 *  1. We can't do it if child already exists (open has special treatment for
 *     this case, but since we are inlined it's OK)
 *  2. We can't do it if dir is read-only (done in permission())
 *  3. We should have write and exec permissions on dir
 *  4. We can't do it if dir is immutable (done in permission())
 */
static inline int may_create(struct inode *dir, struct dentry *child) {
	if (child->d_inode)
		return -EEXIST;
	return permission(dir,MAY_WRITE | MAY_EXEC);
}

/* 
 * Special case: O_CREAT|O_EXCL implies O_NOFOLLOW for security
 * reasons.
 *
 * O_DIRECTORY translates into forcing a directory lookup.
 */
static inline int lookup_flags(unsigned int f)
{
	unsigned long retval = LOOKUP_FOLLOW;

	if (f & O_NOFOLLOW)
		retval &= ~LOOKUP_FOLLOW;
	
	if ((f & (O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL))
		retval &= ~LOOKUP_FOLLOW;
	
	if (f & O_DIRECTORY)
		retval |= LOOKUP_DIRECTORY;
	
	return retval;
}

int vfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;

	mode &= S_IALLUGO & ~current->fs->umask;
	mode |= S_IFREG;

	down(&dir->i_zombie);
	error = may_create(dir, dentry);
	if (error)
		goto exit_lock;

	error = -EACCES;	/* shouldn't it be ENOSYS? */
	if (!dir->i_op || !dir->i_op->create)
		goto exit_lock;

	DQUOT_INIT(dir);
	error = dir->i_op->create(dir, dentry, mode);
exit_lock:
	up(&dir->i_zombie);
	return error;
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

	dentry = lookup_dentry(pathname, NULL, lookup_flags(flag));
	if (IS_ERR(dentry))
		return dentry;

	acc_mode = ACC_MODE(flag);
	if (flag & O_CREAT) {
		struct dentry *dir;

		if (dentry->d_inode) {
			if (!(flag & O_EXCL))
				goto nocreate;
			error = -EEXIST;
			goto exit;
		}

		dir = lock_parent(dentry);
		if (!check_parent(dir, dentry)) {
			/*
			 * Really nasty race happened. What's the 
			 * right error code? We had a dentry, but
			 * before we could use it it was removed
			 * by somebody else. We could just re-try
			 * everything, I guess.
			 *
			 * ENOENT is definitely wrong.
			 */
			error = -ENOENT;
			unlock_dir(dir);
			goto exit;
		}

		/*
		 * Somebody might have created the file while we
		 * waited for the directory lock.. So we have to
		 * re-do the existence test.
		 */
		if (dentry->d_inode) {
			error = 0;
			if (flag & O_EXCL)
				error = -EEXIST;
		} else {
			error = vfs_create(dir->d_inode, dentry,mode);
			/* Don't check for write permission, don't truncate */
			acc_mode = 0;
			flag &= ~O_TRUNC;
		}
		unlock_dir(dir);
		if (error)
			goto exit;
	}

nocreate:
	error = -ENOENT;
	inode = dentry->d_inode;
	if (!inode)
		goto exit;

	error = -ELOOP;
	if (S_ISLNK(inode->i_mode))
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
	if (IS_APPEND(inode)) {
		if  ((flag & FMODE_WRITE) && !(flag & O_APPEND))
			goto exit;
		if (flag & O_TRUNC)
			goto exit;
	}

	if (flag & O_TRUNC) {
		error = get_write_access(inode);
		if (error)
			goto exit;

		/*
		 * Refuse to truncate files with mandatory locks held on them.
		 */
		error = locks_verify_locked(inode);
		if (!error) {
			DQUOT_INIT(inode);
			
			error = do_truncate(dentry, 0);
		}
		put_write_access(inode);
		if (error)
			goto exit;
	} else
		if (flag & FMODE_WRITE)
			DQUOT_INIT(inode);

	return dentry;

exit:
	dput(dentry);
	return ERR_PTR(error);
}

int vfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	int error = -EPERM;

	mode &= ~current->fs->umask;

	down(&dir->i_zombie);
	if ((S_ISCHR(mode) || S_ISBLK(mode)) && !capable(CAP_MKNOD))
		goto exit_lock;

	error = may_create(dir, dentry);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->i_op || !dir->i_op->mknod)
		goto exit_lock;

	DQUOT_INIT(dir);
	error = dir->i_op->mknod(dir, dentry, mode, dev);
exit_lock:
	up(&dir->i_zombie);
	return error;
}

struct dentry * do_mknod(const char * filename, int mode, dev_t dev)
{
	int error;
	struct dentry *dir;
	struct dentry *dentry, *retval;

	dentry = lookup_dentry(filename, NULL, 0);
	if (IS_ERR(dentry))
		return dentry;

	dir = lock_parent(dentry);
	error = -ENOENT;
	if (!check_parent(dir, dentry))
		goto exit_lock;

	error = vfs_mknod(dir->d_inode, dentry, mode, dev);

exit_lock:
	retval = ERR_PTR(error);
	if (!error)
		retval = dget(dentry);
	unlock_dir(dir);
	dput(dentry);
	return retval;
}

asmlinkage long sys_mknod(const char * filename, int mode, dev_t dev)
{
	int error;
	char * tmp;
	struct dentry * dentry, *dir;

	if (S_ISDIR(mode))
		return -EPERM;
	tmp = getname(filename);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	lock_kernel();
	dentry = lookup_dentry(tmp, NULL, 0);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;
	dir = lock_parent(dentry);
	error = -ENOENT;
	if (!check_parent(dir, dentry))
		goto out_unlock;
	switch (mode & S_IFMT) {
	case 0: case S_IFREG:
		error = vfs_create(dir->d_inode, dentry, mode);
		break;
	case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
		error = vfs_mknod(dir->d_inode, dentry, mode, dev);
		break;
	case S_IFDIR:
		error = -EPERM;
		break;
	default:
		error = -EINVAL;
	}
out_unlock:
	unlock_dir(dir);
	dput(dentry);
out:
	unlock_kernel();
	putname(tmp);

	return error;
}

int vfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;

	down(&dir->i_zombie);
	error = may_create(dir, dentry);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->i_op || !dir->i_op->mkdir)
		goto exit_lock;

	DQUOT_INIT(dir);
	mode &= (S_IRWXUGO|S_ISVTX) & ~current->fs->umask;
	error = dir->i_op->mkdir(dir, dentry, mode);

exit_lock:
	up(&dir->i_zombie);
	return error;
}

static inline int do_mkdir(const char * pathname, int mode)
{
	int error;
	struct dentry *dir;
	struct dentry *dentry;

	dentry = lookup_dentry(pathname, NULL, LOOKUP_SLASHOK);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	/*
	 * EEXIST is kind of a strange error code to
	 * return, but basically if the dentry was moved
	 * or unlinked while we locked the parent, we
	 * do know that it _did_ exist before, and as
	 * such it makes perfect sense.. In contrast,
	 * ENOENT doesn't make sense for mkdir.
	 */
	dir = lock_parent(dentry);
	error = -EEXIST;
	if (!check_parent(dir, dentry))
		goto exit_lock;

	error = vfs_mkdir(dir->d_inode, dentry, mode);

exit_lock:
	unlock_dir(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage long sys_mkdir(const char * pathname, int mode)
{
	int error;
	char * tmp;

	tmp = getname(pathname);
	if(IS_ERR(tmp))
		return PTR_ERR(tmp);
	lock_kernel();
	error = do_mkdir(tmp,mode);
	unlock_kernel();
	putname(tmp);

	return error;
}

/*
 * We try to drop the dentry early: we should have
 * a usage count of 2 if we're the only user of this
 * dentry, and if that is true (possibly after pruning
 * the dcache), then we drop the dentry now.
 *
 * A low-level filesystem can, if it choses, legally
 * do a
 *
 *	if (!d_unhashed(dentry))
 *		return -EBUSY;
 *
 * if it cannot handle the case of removing a directory
 * that is still in use by something else..
 */
static void d_unhash(struct dentry *dentry)
{
	dget(dentry);
	switch (dentry->d_count) {
	default:
		shrink_dcache_parent(dentry);
		if (dentry->d_count != 2)
			break;
	case 2:
		d_drop(dentry);
	}
}

int vfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	error = may_delete(dir, dentry, 1);
	if (error)
		return error;

	if (!dir->i_op || !dir->i_op->rmdir)
		return -EPERM;

	DQUOT_INIT(dir);

	double_down(&dir->i_zombie, &dentry->d_inode->i_zombie);
	d_unhash(dentry);
	error = dir->i_op->rmdir(dir, dentry);
	double_up(&dir->i_zombie, &dentry->d_inode->i_zombie);
	dput(dentry);

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
	error = -ENOENT;
	if (check_parent(dir, dentry))
		error = vfs_rmdir(dir->d_inode, dentry);
	unlock_dir(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage long sys_rmdir(const char * pathname)
{
	int error;
	char * tmp;

	tmp = getname(pathname);
	if(IS_ERR(tmp))
		return PTR_ERR(tmp);
	lock_kernel();
	error = do_rmdir(tmp);
	unlock_kernel();

	putname(tmp);

	return error;
}

int vfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;

	down(&dir->i_zombie);
	error = may_delete(dir, dentry, 0);
	if (!error) {
		error = -EPERM;
		if (dir->i_op && dir->i_op->unlink) {
			DQUOT_INIT(dir);
			error = dir->i_op->unlink(dir, dentry);
		}
	}
	up(&dir->i_zombie);
	return error;
}

int do_unlink(const char * name)
{
	int error;
	struct dentry *dir;
	struct dentry *dentry;

	dentry = lookup_dentry(name, NULL, 0);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	dir = lock_parent(dentry);
	error = -ENOENT;
	if (check_parent(dir, dentry))
		error = vfs_unlink(dir->d_inode, dentry);

        unlock_dir(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage long sys_unlink(const char * pathname)
{
	int error;
	char * tmp;

	tmp = getname(pathname);
	if(IS_ERR(tmp))
		return PTR_ERR(tmp);
	lock_kernel();
	error = do_unlink(tmp);
	unlock_kernel();
	putname(tmp);

	return error;
}

int vfs_symlink(struct inode *dir, struct dentry *dentry, const char *oldname)
{
	int error;

	down(&dir->i_zombie);
	error = may_create(dir, dentry);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->i_op || !dir->i_op->symlink)
		goto exit_lock;

	DQUOT_INIT(dir);
	error = dir->i_op->symlink(dir, dentry, oldname);

exit_lock:
	up(&dir->i_zombie);
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
	error = -ENOENT;
	if (!check_parent(dir, dentry))
		goto exit_lock;

	error = vfs_symlink(dir->d_inode, dentry, oldname);

exit_lock:
	unlock_dir(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage long sys_symlink(const char * oldname, const char * newname)
{
	int error;
	char * from;
	char * to;

	from = getname(oldname);
	if(IS_ERR(from))
		return PTR_ERR(from);
	to = getname(newname);
	error = PTR_ERR(to);
	if (!IS_ERR(to)) {
		lock_kernel();
		error = do_symlink(from,to);
		unlock_kernel();
		putname(to);
	}
	putname(from);
	return error;
}

int vfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry)
{
	struct inode *inode;
	int error;

	down(&dir->i_zombie);
	error = -ENOENT;
	inode = old_dentry->d_inode;
	if (!inode)
		goto exit_lock;

	error = may_create(dir, new_dentry);
	if (error)
		goto exit_lock;

	error = -EXDEV;
	if (dir->i_dev != inode->i_dev)
		goto exit_lock;

	/*
	 * A link to an append-only or immutable file cannot be created.
	 */
	error = -EPERM;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		goto exit_lock;
	if (!dir->i_op || !dir->i_op->link)
		goto exit_lock;

	DQUOT_INIT(dir);
	error = dir->i_op->link(old_dentry, dir, new_dentry);

exit_lock:
	up(&dir->i_zombie);
	return error;
}

static inline int do_link(const char * oldname, const char * newname)
{
	struct dentry *old_dentry, *new_dentry, *dir;
	int error;

	/*
	 * Hardlinks are often used in delicate situations.  We avoid
	 * security-related surprises by not following symlinks on the
	 * newname.  --KAB
	 *
	 * We don't follow them on the oldname either to be compatible
	 * with linux 2.0, and to avoid hard-linking to directories
	 * and other special files.  --ADM
	 */
	old_dentry = lookup_dentry(oldname, NULL, 0);
	error = PTR_ERR(old_dentry);
	if (IS_ERR(old_dentry))
		goto exit;

	new_dentry = lookup_dentry(newname, NULL, 0);
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto exit_old;

	dir = lock_parent(new_dentry);
	error = -ENOENT;
	if (!check_parent(dir, new_dentry))
		goto exit_lock;

	error = vfs_link(old_dentry, dir->d_inode, new_dentry);

exit_lock:
	unlock_dir(dir);
	dput(new_dentry);
exit_old:
	dput(old_dentry);
exit:
	return error;
}

asmlinkage long sys_link(const char * oldname, const char * newname)
{
	int error;
	char * from;
		char * to;

	from = getname(oldname);
	if(IS_ERR(from))
		return PTR_ERR(from);
	to = getname(newname);
	error = PTR_ERR(to);
	if (!IS_ERR(to)) {
		lock_kernel();
		error = do_link(from,to);
		unlock_kernel();
		putname(to);
	}
	putname(from);

	return error;
}

/*
 * The worst of all namespace operations - renaming directory. "Perverted"
 * doesn't even start to describe it. Somebody in UCB had a heck of a trip...
 * Problems:
 *	a) we can get into loop creation. Check is done in is_subdir().
 *	b) race potential - two innocent renames can create a loop together.
 *	   That's where 4.4 screws up. Current fix: serialization on
 *	   sb->s_vfs_rename_sem. We might be more accurate, but that's another
 *	   story.
 *	c) we have to lock _three_ objects - parents and victim (if it exists).
 *	   And that - after we got ->i_sem on parents (until then we don't know
 *	   whether the target exists at all, let alone whether it is a directory
 *	   or not). Solution: ->i_zombie. Taken only after ->i_sem. Always taken
 *	   on link creation/removal of any kind. And taken (without ->i_sem) on
 *	   directory that will be removed (both in rmdir() and here).
 *	d) some filesystems don't support opened-but-unlinked directories,
 *	   either because of layout or because they are not ready to deal with
 *	   all cases correctly. The latter will be fixed (taking this sort of
 *	   stuff into VFS), but the former is not going away. Solution: the same
 *	   trick as in rmdir().
 *	e) conversion from fhandle to dentry may come in the wrong moment - when
 *	   we are removing the target. Solution: we will have to grab ->i_zombie
 *	   in the fhandle_to_dentry code. [FIXME - current nfsfh.c relies on
 *	   ->i_sem on parents, which works but leads to some truely excessive
 *	   locking].
 */
int vfs_rename_dir(struct inode *old_dir, struct dentry *old_dentry,
	       struct inode *new_dir, struct dentry *new_dentry)
{
	int error;
	struct inode *target;

	if (old_dentry->d_inode == new_dentry->d_inode)
		return 0;

	error = may_delete(old_dir, old_dentry, 1);
	if (error)
		return error;

	if (new_dir->i_dev != old_dir->i_dev)
		return -EXDEV;

	if (!new_dentry->d_inode)
		error = may_create(new_dir, new_dentry);
	else
		error = may_delete(new_dir, new_dentry, 1);
	if (error)
		return error;

	if (!old_dir->i_op || !old_dir->i_op->rename)
		return -EPERM;

	/*
	 * If we are going to change the parent - check write permissions,
	 * we'll need to flip '..'.
	 */
	if (new_dir != old_dir) {
		error = permission(old_dentry->d_inode, MAY_WRITE);
	}
	if (error)
		return error;

	DQUOT_INIT(old_dir);
	DQUOT_INIT(new_dir);
	down(&old_dir->i_sb->s_vfs_rename_sem);
	error = -EINVAL;
	if (is_subdir(new_dentry, old_dentry))
		goto out_unlock;
	target = new_dentry->d_inode;
	if (target) { /* Hastur! Hastur! Hastur! */
		triple_down(&old_dir->i_zombie,
			    &new_dir->i_zombie,
			    &target->i_zombie);
		d_unhash(new_dentry);
	} else
		double_down(&old_dir->i_zombie,
			    &new_dir->i_zombie);
	error = old_dir->i_op->rename(old_dir, old_dentry, new_dir, new_dentry);
	if (target) {
		triple_up(&old_dir->i_zombie,
			  &new_dir->i_zombie,
			  &target->i_zombie);
		d_rehash(new_dentry);
		dput(new_dentry);
	} else
		double_up(&old_dir->i_zombie,
			  &new_dir->i_zombie);
		
	if (!error)
		d_move(old_dentry,new_dentry);
out_unlock:
	up(&old_dir->i_sb->s_vfs_rename_sem);
	return error;
}

int vfs_rename_other(struct inode *old_dir, struct dentry *old_dentry,
	       struct inode *new_dir, struct dentry *new_dentry)
{
	int error;

	if (old_dentry->d_inode == new_dentry->d_inode)
		return 0;

	error = may_delete(old_dir, old_dentry, 0);
	if (error)
		return error;

	if (new_dir->i_dev != old_dir->i_dev)
		return -EXDEV;

	if (!new_dentry->d_inode)
		error = may_create(new_dir, new_dentry);
	else
		error = may_delete(new_dir, new_dentry, 0);
	if (error)
		return error;

	if (!old_dir->i_op || !old_dir->i_op->rename)
		return -EPERM;

	DQUOT_INIT(old_dir);
	DQUOT_INIT(new_dir);
	double_down(&old_dir->i_zombie, &new_dir->i_zombie);
	error = old_dir->i_op->rename(old_dir, old_dentry, new_dir, new_dentry);
	double_up(&old_dir->i_zombie, &new_dir->i_zombie);
	if (error)
		return error;
	/* The following d_move() should become unconditional */
	if (!(old_dir->i_sb->s_flags & MS_ODD_RENAME)) {
		d_move(old_dentry, new_dentry);
	}
	return 0;
}

int vfs_rename(struct inode *old_dir, struct dentry *old_dentry,
	       struct inode *new_dir, struct dentry *new_dentry)
{
	if (S_ISDIR(old_dentry->d_inode->i_mode))
		return vfs_rename_dir(old_dir,old_dentry,new_dir,new_dentry);
	else
		return vfs_rename_other(old_dir,old_dentry,new_dir,new_dentry);
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

	error = -ENOENT;
	if (!old_dentry->d_inode)
		goto exit_old;

	{
		unsigned int flags = 0;
		if (S_ISDIR(old_dentry->d_inode->i_mode))
			flags = LOOKUP_SLASHOK;
		new_dentry = lookup_dentry(newname, NULL, flags);
	}

	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto exit_old;

	new_dir = get_parent(new_dentry);
	old_dir = get_parent(old_dentry);

	double_lock(new_dir, old_dir);

	error = -ENOENT;
	if (check_parent(old_dir, old_dentry) && check_parent(new_dir, new_dentry))
		error = vfs_rename(old_dir->d_inode, old_dentry,
				   new_dir->d_inode, new_dentry);

	double_unlock(new_dir, old_dir);
	dput(new_dentry);
exit_old:
	dput(old_dentry);
exit:
	return error;
}

asmlinkage long sys_rename(const char * oldname, const char * newname)
{
	int error;
	char * from;
	char * to;

	from = getname(oldname);
	if(IS_ERR(from))
		return PTR_ERR(from);
	to = getname(newname);
	error = PTR_ERR(to);
	if (!IS_ERR(to)) {
		lock_kernel();
		error = do_rename(from,to);
		unlock_kernel();
		putname(to);
	}
	putname(from);
	return error;
}

int vfs_readlink(struct dentry *dentry, char *buffer, int buflen, const char *link)
{
	u32 len;

	len = PTR_ERR(link);
	if (IS_ERR(link))
		goto out;

	len = strlen(link);
	if (len > buflen)
		len = buflen;
	copy_to_user(buffer, link, len);
out:
	return len;
}

static inline struct dentry *
__vfs_follow_link(struct dentry *dentry, struct dentry *base,
		unsigned follow, const char *link)
{
	struct dentry *result;
	UPDATE_ATIME(dentry->d_inode);

	if (IS_ERR(link))
		goto fail;

	result = lookup_dentry(link, base, follow);
	return result;

fail:
	dput(base);
	return (struct dentry *)link;
}

struct dentry *
vfs_follow_link(struct dentry *dentry, struct dentry *base,
unsigned int follow, const char *link)
{
	return __vfs_follow_link(dentry,base,follow,link);
}

/* get the link contents into pagecache */
static char *page_getlink(struct dentry * dentry, struct page **ppage)
{
	struct page * page;
	struct address_space *mapping = dentry->d_inode->i_mapping;
	page = read_cache_page(mapping, 0, (filler_t *)mapping->a_ops->readpage,
				dentry);
	if (IS_ERR(page))
		goto sync_fail;
	wait_on_page(page);
	if (!Page_Uptodate(page))
		goto async_fail;
	*ppage = page;
	return (char*) kmap(page);

async_fail:
	page_cache_release(page);
	return ERR_PTR(-EIO);

sync_fail:
	return (char*)page;
}

int page_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct page *page = NULL;
	char *s = page_getlink(dentry, &page);
	int res = vfs_readlink(dentry,buffer,buflen,s);
	if (page) {
		kunmap(page);
		page_cache_release(page);
	}
	return res;
}

struct dentry *
page_follow_link(struct dentry *dentry, struct dentry *base, unsigned int follow)
{
	struct page *page = NULL;
	char *s = page_getlink(dentry, &page);
	struct dentry *res = __vfs_follow_link(dentry,base,follow,s);
	if (page) {
		kunmap(page);
		page_cache_release(page);
	}
	return res;
}

struct inode_operations page_symlink_inode_operations = {
	readlink:	page_readlink,
	follow_link:	page_follow_link,
};
