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

#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <asm/semaphore.h>
#include <asm/page.h>
#include <asm/pgtable.h>

/*
 * The bitmask for a lookup event:
 *  - follow links at the end
 *  - require a directory
 *  - ending slashes ok even for nonexistent files
 *  - internal "there are more path compnents" flag
 */
#define LOOKUP_FOLLOW		(1)
#define LOOKUP_DIRECTORY	(2)
#define LOOKUP_SLASHOK		(4)
#define LOOKUP_CONTINUE		(8)

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
static struct dentry * cached_lookup(struct dentry * parent, struct qstr * name)
{
	struct dentry * dentry = d_lookup(parent, name);

	if (dentry && dentry->d_op && dentry->d_op->d_revalidate) {
		if (!dentry->d_op->d_revalidate(dentry) && !d_invalidate(dentry)) {
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
static struct dentry * real_lookup(struct dentry * parent, struct qstr * name)
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
	result = cached_lookup(parent, name);
	if (!result) {
		struct dentry * dentry = d_alloc(parent, name);
		result = ERR_PTR(-ENOMEM);
		if (dentry) {
			int error = dir->i_op->lookup(dir, dentry);
			result = dentry;
			if (error) {
				dput(dentry);
				result = ERR_PTR(error);
			}
		}
	}
	up(&dir->i_sem);
	return result;
}

static struct dentry * do_follow_link(struct dentry *base, struct dentry *dentry, unsigned int follow)
{
	struct inode * inode = dentry->d_inode;

	if (inode && inode->i_op && inode->i_op->follow_link) {
		if (current->link_count < 5) {
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
			dentry = cached_lookup(base, &this);
			if (!dentry) {
				dentry = real_lookup(base, &this);
				if (IS_ERR(dentry))
					break;
			}
		}

		/* Check mountpoints.. */
		dentry = follow_mount(dentry);

		if (!(flags & LOOKUP_FOLLOW))
			break;

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
 * We need to do a check-parent every time
 * after we have locked the parent - to verify
 * that the parent is still our parent and
 * that we are still hashed onto it..
 *
 * This is requied in case two processes race
 * on removing (or moving) the same entry: the
 * parent lock will serialize them, but the
 * other process will be too late..
 */
#define check_parent(dir, dentry) \
	((dir) == (dentry)->d_parent && !list_empty(&dentry->d_hash))

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
	return dir;
}

/*
 * Whee.. Deadlock country. Happily there are only two VFS
 * operations that do this..
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
		} else if ((error = may_create(dir->d_inode, dentry)) == 0) {
			if (!dir->d_inode->i_op || !dir->d_inode->i_op->create)
				error = -EACCES;
			else {
				DQUOT_INIT(dir->d_inode);
				error = dir->d_inode->i_op->create(dir->d_inode, dentry, mode);
				/* Don't check for write permission, don't truncate */
				acc_mode = 0;
				flag &= ~O_TRUNC;
			}
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

struct dentry * do_mknod(const char * filename, int mode, dev_t dev)
{
	int error;
	struct dentry *dir;
	struct dentry *dentry, *retval;

	mode &= ~current->fs->umask;
	dentry = lookup_dentry(filename, NULL, LOOKUP_FOLLOW);
	if (IS_ERR(dentry))
		return dentry;

	dir = lock_parent(dentry);
	error = -ENOENT;
	if (!check_parent(dir, dentry))
		goto exit_lock;

	error = may_create(dir->d_inode, dentry);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->d_inode->i_op || !dir->d_inode->i_op->mknod)
		goto exit_lock;

	DQUOT_INIT(dir->d_inode);
	error = dir->d_inode->i_op->mknod(dir->d_inode, dentry, mode, dev);
exit_lock:
	retval = ERR_PTR(error);
	if (!error)
		retval = dget(dentry);
	unlock_dir(dir);
	dput(dentry);
	return retval;
}

asmlinkage int sys_mknod(const char * filename, int mode, dev_t dev)
{
	int error;
	char * tmp;

	lock_kernel();
	error = -EPERM;
	if (S_ISDIR(mode) || (!S_ISFIFO(mode) && !capable(CAP_SYS_ADMIN)))
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

	error = may_create(dir->d_inode, dentry);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->d_inode->i_op || !dir->d_inode->i_op->mkdir)
		goto exit_lock;

	DQUOT_INIT(dir->d_inode);
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

int vfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	error = may_delete(dir, dentry, 1);
	if (error)
		return error;

	if (!dir->i_op || !dir->i_op->rmdir)
		return -EPERM;

	DQUOT_INIT(dir);

	/*
	 * We try to drop the dentry early: we should have
	 * a usage count of 2 if we're the only user of this
	 * dentry, and if that is true (possibly after pruning
	 * the dcache), then we drop the dentry now.
	 *
	 * A low-level filesystem can, if it choses, legally
	 * do a
	 *
	 *	if (!list_empty(&dentry->d_hash))
	 *		return -EBUSY;
	 *
	 * if it cannot handle the case of removing a directory
	 * that is still in use by something else..
	 */
	switch (dentry->d_count) {
	default:
		shrink_dcache_parent(dentry);
		if (dentry->d_count != 2)
			break;
	case 2:
		d_drop(dentry);
	}

	error = dir->i_op->rmdir(dir, dentry);

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

	error = -ENOENT;
	if (!dentry->d_inode)
		goto exit_dput;

	dir = dget(dentry->d_parent);

	/*
	 * The dentry->d_count stuff confuses d_delete() enough to
	 * not kill the inode from under us while it is locked. This
	 * wouldn't be needed, except the dentry semaphore is really
	 * in the inode, not in the dentry..
	 */
	dentry->d_count++;
	double_lock(dir, dentry);

	error = -ENOENT;
	if (check_parent(dir, dentry))
		error = vfs_rmdir(dir->d_inode, dentry);

	double_unlock(dentry, dir);
exit_dput:
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

int vfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;

	error = may_delete(dir, dentry, 0);
	if (error)
		goto exit_lock;

	if (!dir->i_op || !dir->i_op->unlink)
		goto exit_lock;

	DQUOT_INIT(dir);

	error = dir->i_op->unlink(dir, dentry);

exit_lock:
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
	error = -ENOENT;
	if (check_parent(dir, dentry))
		error = vfs_unlink(dir->d_inode, dentry);

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
	error = -ENOENT;
	if (!check_parent(dir, dentry))
		goto exit_lock;

	error = may_create(dir->d_inode, dentry);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->d_inode->i_op || !dir->d_inode->i_op->symlink)
		goto exit_lock;

	DQUOT_INIT(dir->d_inode);
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

	error = -ENOENT;
	inode = old_dentry->d_inode;
	if (!inode)
		goto exit_lock;

	error = may_create(dir->d_inode, new_dentry);
	if (error)
		goto exit_lock;

	error = -EXDEV;
	if (dir->d_inode->i_dev != inode->i_dev)
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

	DQUOT_INIT(dir->d_inode);
	error = dir->d_inode->i_op->link(old_dentry, dir->d_inode, new_dentry);

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

int vfs_rename(struct inode *old_dir, struct dentry *old_dentry,
	       struct inode *new_dir, struct dentry *new_dentry)
{
	int error;
	int isdir;

	isdir = S_ISDIR(old_dentry->d_inode->i_mode);

	error = may_delete(old_dir, old_dentry, isdir); /* XXX */
	if (error)
		return error;

	if (new_dir->i_dev != old_dir->i_dev)
		return -EXDEV;

	if (!new_dentry->d_inode)
		error = may_create(new_dir, new_dentry);
	else
		error = may_delete(new_dir, new_dentry, isdir);
	if (error)
		return error;

	if (!old_dir->i_op || !old_dir->i_op->rename)
		return -EPERM;

	DQUOT_INIT(old_dir);
	DQUOT_INIT(new_dir);
	error = old_dir->i_op->rename(old_dir, old_dentry, new_dir, new_dentry);

	return error;
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
