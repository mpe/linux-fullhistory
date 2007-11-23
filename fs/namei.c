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
/* [Feb-Apr 2000, AV] Rewrite to the new namespace architecture.
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

/* [Feb-Apr 2000 AV] Complete rewrite. Rules for symlinks:
 *	inside the path - always follow.
 *	in the last component in creation/removal/renaming - never follow.
 *	if LOOKUP_FOLLOW passed - follow.
 *	if the pathname has trailing slashes - follow.
 *	otherwise - don't follow.
 * (applied in that order).
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

	if (inode->i_op && inode->i_op->permission) {
		int retval;
		lock_kernel();
		retval = inode->i_op->permission(inode, mask);
		unlock_kernel();
		return retval;
	}

	if ((mask & S_IWOTH) && IS_RDONLY(inode) &&
		 (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
		return -EROFS; /* Nobody gets write access to a read-only fs */

	if ((mask & S_IWOTH) && IS_IMMUTABLE(inode))
		return -EACCES; /* Nobody gets write access to an immutable file */

	if (current->fsuid == inode->i_uid)
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

void path_release(struct nameidata *nd)
{
	lock_kernel();
	dput(nd->dentry);
	mntput(nd->mnt);
	unlock_kernel();
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

static inline int do_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	int err;
	if (current->link_count >= 32)
		goto loop;
	current->link_count++;
	UPDATE_ATIME(dentry->d_inode);
	err = dentry->d_inode->i_op->follow_link(dentry, nd);
	current->link_count--;
	return err;
loop:
	path_release(nd);
	return -ELOOP;
}

static inline int __follow_up(struct vfsmount **mnt, struct dentry **base)
{
	struct vfsmount *parent=(*mnt)->mnt_parent;
	struct dentry *dentry;
	if (parent == *mnt)
		return 0;
	mntget(parent);
	dentry=dget((*mnt)->mnt_mountpoint);
	dput(*base);
	*base = dentry;
	mntput(*mnt);
	*mnt = parent;
	return 1;
}

int follow_up(struct vfsmount **mnt, struct dentry **dentry)
{
	return __follow_up(mnt, dentry);
}

static inline int __follow_down(struct vfsmount **mnt, struct dentry **dentry)
{
	struct list_head *p = (*dentry)->d_vfsmnt.next;
	while (p != &(*dentry)->d_vfsmnt) {
		struct vfsmount *tmp;
		tmp = list_entry(p, struct vfsmount, mnt_clash);
		if (tmp->mnt_parent == *mnt) {
			*mnt = mntget(tmp);
			mntput(tmp->mnt_parent);
			/* tmp holds the mountpoint, so... */
			dput(*dentry);
			*dentry = dget(tmp->mnt_root);
			return 1;
		}
		p = p->next;
	}
	return 0;
}

int follow_down(struct vfsmount **mnt, struct dentry **dentry)
{
	return __follow_down(mnt,dentry);
}

/*
 * Name resolution.
 *
 * This is the basic name resolution function, turning a pathname
 * into the final dentry.
 *
 * We expect 'base' to be positive and a directory.
 */
int path_walk(const char * name, struct nameidata *nd)
{
	struct dentry *dentry;
	struct inode *inode;
	int err;
	unsigned int lookup_flags = nd->flags;

	while (*name=='/')
		name++;
	if (!*name)
		goto return_base;

	inode = nd->dentry->d_inode;
	if (current->link_count)
		lookup_flags = LOOKUP_FOLLOW;

	/* At this point we know we have a real path component. */
	for(;;) {
		unsigned long hash;
		struct qstr this;
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
		if (!c)
			goto last_component;
		while (*++name == '/');
		if (!*name)
			goto last_with_slashes;

		/*
		 * "." and ".." are special - ".." especially so because it has
		 * to be able to know about the current root directory and
		 * parent relationships.
		 */
		if (this.name[0] == '.') switch (this.len) {
			default:
				break;
			case 2:	
				if (this.name[1] != '.')
					break;
				while (1) {
					if (nd->dentry == current->fs->root &&
					    nd->mnt == current->fs->rootmnt)
						break;
					if (nd->dentry != nd->mnt->mnt_root) {
						dentry = dget(nd->dentry->d_parent);
						dput(nd->dentry);
						nd->dentry = dentry;
						break;
					}
					if (!__follow_up(&nd->mnt, &nd->dentry))
						break;
				}
				inode = nd->dentry->d_inode;
				/* fallthrough */
			case 1:
				continue;
		}
		/*
		 * See if the low-level filesystem might want
		 * to use its own hash..
		 */
		if (nd->dentry->d_op && nd->dentry->d_op->d_hash) {
			err = nd->dentry->d_op->d_hash(nd->dentry, &this);
			if (err < 0)
				break;
		}
		/* This does the actual lookups.. */
		dentry = cached_lookup(nd->dentry, &this, LOOKUP_CONTINUE);
		if (!dentry) {
			dentry = real_lookup(nd->dentry, &this, LOOKUP_CONTINUE);
			err = PTR_ERR(dentry);
			if (IS_ERR(dentry))
				break;
		}
		/* Check mountpoints.. */
		while (d_mountpoint(dentry) && __follow_down(&nd->mnt, &dentry))
			;

		err = -ENOENT;
		inode = dentry->d_inode;
		if (!inode)
			goto out_dput;
		err = -ENOTDIR; 
		if (!inode->i_op)
			goto out_dput;

		if (inode->i_op->follow_link) {
			err = do_follow_link(dentry, nd);
			dput(dentry);
			if (err)
				goto return_err;
			err = -ENOENT;
			inode = nd->dentry->d_inode;
			if (!inode)
				break;
			err = -ENOTDIR; 
			if (!inode->i_op)
				break;
		} else {
			dput(nd->dentry);
			nd->dentry = dentry;
		}
		err = -ENOTDIR; 
		if (!inode->i_op->lookup)
			break;
		continue;
		/* here ends the main loop */

last_with_slashes:
		lookup_flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;
last_component:
		if (lookup_flags & LOOKUP_PARENT)
			goto lookup_parent;
		if (this.name[0] == '.') switch (this.len) {
			default:
				break;
			case 2:	
				if (this.name[1] != '.')
					break;
				while (1) {
					if (nd->dentry == current->fs->root &&
					    nd->mnt == current->fs->rootmnt)
						break;
					if (nd->dentry != nd->mnt->mnt_root) {
						dentry = dget(nd->dentry->d_parent);
						dput(nd->dentry);
						nd->dentry = dentry;
						break;
					}
					if (!__follow_up(&nd->mnt, &nd->dentry))
						break;
				}
				inode = nd->dentry->d_inode;
				/* fallthrough */
			case 1:
				goto return_base;
		}
		if (nd->dentry->d_op && nd->dentry->d_op->d_hash) {
			err = nd->dentry->d_op->d_hash(nd->dentry, &this);
			if (err < 0)
				break;
		}
		dentry = cached_lookup(nd->dentry, &this, 0);
		if (!dentry) {
			dentry = real_lookup(nd->dentry, &this, 0);
			err = PTR_ERR(dentry);
			if (IS_ERR(dentry))
				break;
		}
		while (d_mountpoint(dentry) && __follow_down(&nd->mnt, &dentry))
			;
		inode = dentry->d_inode;
		if ((lookup_flags & LOOKUP_FOLLOW)
		    && inode && inode->i_op && inode->i_op->follow_link) {
			err = do_follow_link(dentry, nd);
			dput(dentry);
			if (err)
				goto return_err;
			inode = nd->dentry->d_inode;
		} else {
			dput(nd->dentry);
			nd->dentry = dentry;
		}
		err = -ENOENT;
		if (!inode)
			goto no_inode;
		if (lookup_flags & LOOKUP_DIRECTORY) {
			err = -ENOTDIR; 
			if (!inode->i_op || !inode->i_op->lookup)
				break;
		}
		goto return_base;
no_inode:
		err = -ENOENT;
		if (lookup_flags & (LOOKUP_POSITIVE|LOOKUP_DIRECTORY))
			break;
		goto return_base;
lookup_parent:
		nd->last = this;
		nd->last_type = LAST_NORM;
		if (this.name[0] != '.')
			goto return_base;
		if (this.len == 1)
			nd->last_type = LAST_DOT;
		else if (this.len == 2 && this.name[1] == '.')
			nd->last_type = LAST_DOTDOT;
return_base:
		return 0;
out_dput:
		dput(dentry);
		break;
	}
	path_release(nd);
return_err:
	return err;
}

/* returns 1 if everything is done */
static int __emul_lookup_dentry(const char *name, struct nameidata *nd)
{
	nd->mnt = mntget(current->fs->altrootmnt);
	nd->dentry = dget(current->fs->altroot);
	if (path_walk(name, nd))
		return 0;

	if (!nd->dentry->d_inode) {
		struct nameidata nd_root;
		nd_root.last_type = LAST_ROOT;
		nd_root.flags = nd->flags;
		nd_root.mnt = mntget(current->fs->rootmnt);
		nd_root.dentry = dget(current->fs->root);
		if (path_walk(name, &nd_root))
			return 1;
		if (nd_root.dentry->d_inode) {
			path_release(nd);
			nd->dentry = nd_root.dentry;
			nd->mnt = nd_root.mnt;
			nd->last = nd_root.last;
			return 1;
		}
		path_release(&nd_root);
	}
	return 1;
}

void set_fs_altroot(void)
{
	char *emul = __emul_prefix();
	struct nameidata nd;
	struct vfsmount *mnt = NULL, *oldmnt;
	struct dentry *dentry = NULL, *olddentry;
	if (emul) {
		nd.mnt = mntget(current->fs->rootmnt);
		nd.dentry = dget(current->fs->root);
		nd.flags = LOOKUP_FOLLOW|LOOKUP_DIRECTORY|LOOKUP_POSITIVE;
		if (path_walk(emul,&nd) == 0) {
			mnt = nd.mnt;
			dentry = nd.dentry;
		}
	}
	oldmnt = current->fs->altrootmnt;
	olddentry = current->fs->altroot;
	current->fs->altrootmnt = mnt;
	current->fs->altroot = dentry;
	if (olddentry) {
		dput(olddentry);
		mntput(oldmnt);
	}
}

static inline int
walk_init_root(const char *name, struct nameidata *nd)
{
	if (current->fs->altroot && !(nd->flags & LOOKUP_NOALT))
		if (__emul_lookup_dentry(name,nd))
			return 0;
	nd->mnt = mntget(current->fs->rootmnt);
	nd->dentry = dget(current->fs->root);
	return 1;
}

int path_init(const char *name,unsigned int flags,struct nameidata *nd)
{
	nd->last_type = LAST_ROOT; /* if there are only slashes... */
	nd->flags = flags;
	if (*name=='/')
		return walk_init_root(name,nd);
	nd->mnt = mntget(current->fs->pwdmnt);
	nd->dentry = dget(current->fs->pwd);
	return 1;
}

/*
 * Restricted form of lookup. Doesn't follow links, single-component only,
 * needs parent already locked. Doesn't follow mounts.
 */
struct dentry * lookup_hash(struct qstr *name, struct dentry * base)
{
	struct dentry * dentry;
	struct inode *inode;
	int err;

	inode = base->d_inode;
	err = permission(inode, MAY_EXEC);
	dentry = ERR_PTR(err);
	if (err)
		goto out;

	/*
	 * See if the low-level filesystem might want
	 * to use its own hash..
	 */
	if (base->d_op && base->d_op->d_hash) {
		err = base->d_op->d_hash(base, name);
		dentry = ERR_PTR(err);
		if (err < 0)
			goto out;
	}

	dentry = cached_lookup(base, name, 0);
	if (!dentry) {
		struct dentry *new = d_alloc(base, name);
		dentry = ERR_PTR(-ENOMEM);
		if (!new)
			goto out;
		dentry = inode->i_op->lookup(inode, new);
		if (!dentry)
			dentry = new;
		else
			dput(new);
	}
out:
	return dentry;
}

struct dentry * lookup_one(const char * name, struct dentry * base)
{
	unsigned long hash;
	struct qstr this;
	unsigned int c;

	this.name = name;
	c = *(const unsigned char *)name;
	if (!c)
		goto access;

	hash = init_name_hash();
	do {
		name++;
		if (c == '/')
			goto access;
		hash = partial_name_hash(c, hash);
		c = *(const unsigned char *)name;
	} while (c);
	this.len = name - (const char *) this.name;
	this.hash = end_name_hash(hash);

	return lookup_hash(&this, base);
access:
	return ERR_PTR(-EACCES);
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
int __user_walk(const char *name, unsigned flags, struct nameidata *nd)
{
	char *tmp;
	int err;

	tmp = getname(name);
	err = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		err = 0;
		lock_kernel();
		if (path_init(tmp, flags, nd))
			err = path_walk(tmp, nd);
		unlock_kernel();
		putname(tmp);
	}
	return err;
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
	if (IS_DEADDIR(dir))
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
		if (d_mountpoint(victim))
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
	if (IS_DEADDIR(dir))
		return -ENOENT;
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
int open_namei(const char * pathname, int flag, int mode, struct nameidata *nd)
{
	int acc_mode, error = 0;
	struct inode *inode;
	struct dentry *dentry;

	acc_mode = ACC_MODE(flag);
	if (!(flag & O_CREAT)) {
		if (path_init(pathname, lookup_flags(flag), nd))
			error = path_walk(pathname, nd);
		if (error)
			return error;

		dentry = nd->dentry;
	} else {
		struct dentry *dir;

		if (path_init(pathname, LOOKUP_PARENT, nd))
			error = path_walk(pathname, nd);
		if (error)
			return error;
		/*
		 * It's not obvious that open(".", O_CREAT, foo) should
		 * fail, but it's even less obvious that it should succeed.
		 * Since O_CREAT means an intention to create the thing and
		 * open(2) had never created directories, count it as caller's
		 * luserdom and let him sod off - -EISDIR it is.
		 */
		error = -EISDIR;
		if (nd->last_type != LAST_NORM)
			goto exit;
		/* same for foo/ */
		if (nd->last.name[nd->last.len])
			goto exit;

		dir = nd->dentry;
		down(&dir->d_inode->i_sem);

		dentry = lookup_hash(&nd->last, nd->dentry);
		error = PTR_ERR(dentry);
		if (IS_ERR(dentry)) {
			up(&dir->d_inode->i_sem);
			goto exit;
		}

		if (dentry->d_inode) {
			up(&dir->d_inode->i_sem);
			error = -EEXIST;
			if (flag & O_EXCL)
				goto exit_dput;
			if (dentry->d_inode->i_op &&
			    dentry->d_inode->i_op->follow_link) {
				/*
				 * With O_EXCL it would be -EEXIST.
				 * If symlink is a dangling one it's -ENOENT.
				 * Otherwise we open the object it points to.
				 */
				error = do_follow_link(dentry, nd);
				dput(dentry);
				if (error)
					return error;
				dentry = nd->dentry;
			} else {
				dput(nd->dentry);
				nd->dentry = dentry;
			}
			error = -EISDIR;
			if (dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode))
				goto exit;
		} else {
			error = vfs_create(dir->d_inode, dentry, mode);
			up(&dir->d_inode->i_sem);
			/* Don't check for write permission, don't truncate */
			acc_mode = 0;
			flag &= ~O_TRUNC;
			dput(nd->dentry);
			nd->dentry = dentry;
			if (error)
				goto exit;
		}
	}

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

	return 0;

exit_dput:
	dput(dentry);
exit:
	path_release(nd);
	return error;
}

static struct dentry *lookup_create(struct nameidata *nd, int is_dir)
{
	struct dentry *dentry;

	down(&nd->dentry->d_inode->i_sem);
	dentry = ERR_PTR(-EEXIST);
	if (nd->last_type != LAST_NORM)
		goto fail;
	dentry = lookup_hash(&nd->last, nd->dentry);
	if (IS_ERR(dentry))
		goto fail;
	if (!is_dir && nd->last.name[nd->last.len] && !dentry->d_inode)
		goto enoent;
	return dentry;
enoent:
	dput(dentry);
	dentry = ERR_PTR(-ENOENT);
fail:
	return dentry;
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

asmlinkage long sys_mknod(const char * filename, int mode, dev_t dev)
{
	int error = 0;
	char * tmp;
	struct dentry * dentry;
	struct nameidata nd;

	if (S_ISDIR(mode))
		return -EPERM;
	tmp = getname(filename);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	lock_kernel();
	if (path_init(tmp, LOOKUP_PARENT, &nd))
		error = path_walk(tmp, &nd);
	if (error)
		goto out;
	dentry = lookup_create(&nd, 0);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		switch (mode & S_IFMT) {
		case 0: case S_IFREG:
			error = vfs_create(nd.dentry->d_inode,dentry,mode);
			break;
		case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
			error = vfs_mknod(nd.dentry->d_inode,dentry,mode,dev);
			break;
		case S_IFDIR:
			error = -EPERM;
			break;
		default:
			error = -EINVAL;
		}
		dput(dentry);
	}
	up(&nd.dentry->d_inode->i_sem);
	path_release(&nd);
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

asmlinkage long sys_mkdir(const char * pathname, int mode)
{
	int error = 0;
	char * tmp;

	tmp = getname(pathname);
	error = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		struct dentry *dentry;
		struct nameidata nd;

		lock_kernel();
		if (path_init(tmp, LOOKUP_PARENT, &nd))
			error = path_walk(tmp, &nd);
		if (error)
			goto out;
		dentry = lookup_create(&nd, 1);
		error = PTR_ERR(dentry);
		if (!IS_ERR(dentry)) {
			error = vfs_mkdir(nd.dentry->d_inode, dentry, mode);
			dput(dentry);
		}
		up(&nd.dentry->d_inode->i_sem);
		path_release(&nd);
out:
		unlock_kernel();
		putname(tmp);
	}

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
	if (!error)
		dentry->d_inode->i_flags |= S_DEAD;
	double_up(&dir->i_zombie, &dentry->d_inode->i_zombie);
	if (!error)
		d_delete(dentry);
	dput(dentry);

	return error;
}

asmlinkage long sys_rmdir(const char * pathname)
{
	int error = 0;
	char * name;
	struct dentry *dentry;
	struct nameidata nd;

	name = getname(pathname);
	if(IS_ERR(name))
		return PTR_ERR(name);
	lock_kernel();

	if (path_init(name, LOOKUP_PARENT, &nd))
		error = path_walk(name, &nd);
	if (error)
		goto exit;

	switch(nd.last_type) {
		case LAST_DOTDOT:
			error = -ENOTEMPTY;
			goto exit1;
		case LAST_ROOT: case LAST_DOT:
			error = -EBUSY;
			goto exit1;
	}
	down(&nd.dentry->d_inode->i_sem);
	dentry = lookup_hash(&nd.last, nd.dentry);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		error = vfs_rmdir(nd.dentry->d_inode, dentry);
		dput(dentry);
	}
	up(&nd.dentry->d_inode->i_sem);
exit1:
	path_release(&nd);
exit:
	unlock_kernel();
	putname(name);
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
			if (!error)
				d_delete(dentry);
		}
	}
	up(&dir->i_zombie);
	return error;
}

asmlinkage long sys_unlink(const char * pathname)
{
	int error = 0;
	char * name;
	struct dentry *dentry;
	struct nameidata nd;

	name = getname(pathname);
	if(IS_ERR(name))
		return PTR_ERR(name);
	lock_kernel();

	if (path_init(name, LOOKUP_PARENT, &nd))
		error = path_walk(name, &nd);
	if (error)
		goto exit;
	error = -EISDIR;
	if (nd.last_type != LAST_NORM)
		goto exit1;
	down(&nd.dentry->d_inode->i_sem);
	dentry = lookup_hash(&nd.last, nd.dentry);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		/* Why not before? Because we want correct error value */
		if (nd.last.name[nd.last.len])
			goto slashes;
		error = vfs_unlink(nd.dentry->d_inode, dentry);
	exit2:
		dput(dentry);
	}
	up(&nd.dentry->d_inode->i_sem);
exit1:
	path_release(&nd);
exit:
	unlock_kernel();
	putname(name);

	return error;

slashes:
	error = !dentry->d_inode ? -ENOENT :
		S_ISDIR(dentry->d_inode->i_mode) ? -EISDIR : -ENOTDIR;
	goto exit2;
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

asmlinkage long sys_symlink(const char * oldname, const char * newname)
{
	int error = 0;
	char * from;
	char * to;

	from = getname(oldname);
	if(IS_ERR(from))
		return PTR_ERR(from);
	to = getname(newname);
	error = PTR_ERR(to);
	if (!IS_ERR(to)) {
		struct dentry *dentry;
		struct nameidata nd;

		lock_kernel();
		if (path_init(to, LOOKUP_PARENT, &nd))
			error = path_walk(to, &nd);
		if (error)
			goto out;
		dentry = lookup_create(&nd, 0);
		error = PTR_ERR(dentry);
		if (!IS_ERR(dentry)) {
			error = vfs_symlink(nd.dentry->d_inode, dentry, from);
			dput(dentry);
		}
		up(&nd.dentry->d_inode->i_sem);
		path_release(&nd);
out:
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

/*
 * Hardlinks are often used in delicate situations.  We avoid
 * security-related surprises by not following symlinks on the
 * newname.  --KAB
 *
 * We don't follow them on the oldname either to be compatible
 * with linux 2.0, and to avoid hard-linking to directories
 * and other special files.  --ADM
 */
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
		struct dentry *new_dentry;
		struct nameidata nd, old_nd;

		lock_kernel();
		error = 0;
		if (path_init(from, LOOKUP_POSITIVE, &old_nd))
			error = path_walk(from, &old_nd);
		if (error)
			goto exit;
		if (path_init(to, LOOKUP_PARENT, &nd))
			error = path_walk(to, &nd);
		if (error)
			goto out;
		error = -EXDEV;
		if (old_nd.mnt != nd.mnt)
			goto out;
		new_dentry = lookup_create(&nd, 0);
		error = PTR_ERR(new_dentry);
		if (!IS_ERR(new_dentry)) {
			error = vfs_link(old_nd.dentry, nd.dentry->d_inode, new_dentry);
			dput(new_dentry);
		}
		up(&nd.dentry->d_inode->i_sem);
		path_release(&nd);
out:
		path_release(&old_nd);
exit:
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
		if (!error)
			target->i_flags |= S_DEAD;
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
	int error = 0;
	struct dentry * old_dir, * new_dir;
	struct dentry * old_dentry, *new_dentry;
	struct nameidata oldnd, newnd;

	if (path_init(oldname, LOOKUP_PARENT, &oldnd))
		error = path_walk(oldname, &oldnd);

	if (error)
		goto exit;

	if (path_init(newname, LOOKUP_PARENT, &newnd))
		error = path_walk(newname, &newnd);
	if (error)
		goto exit1;

	error = -EXDEV;
	if (oldnd.mnt != newnd.mnt)
		goto exit2;

	old_dir = oldnd.dentry;
	error = -EBUSY;
	if (oldnd.last_type != LAST_NORM)
		goto exit2;

	new_dir = newnd.dentry;
	if (newnd.last_type != LAST_NORM)
		goto exit2;

	double_lock(new_dir, old_dir);

	old_dentry = lookup_hash(&oldnd.last, old_dir);
	error = PTR_ERR(old_dentry);
	if (IS_ERR(old_dentry))
		goto exit3;
	/* source must exist */
	error = -ENOENT;
	if (!old_dentry->d_inode)
		goto exit4;
	/* unless the source is a directory trailing slashes give -ENOTDIR */
	if (!S_ISDIR(old_dentry->d_inode->i_mode)) {
		error = -ENOTDIR;
		if (oldnd.last.name[oldnd.last.len])
			goto exit4;
		if (newnd.last.name[newnd.last.len])
			goto exit4;
	}
	new_dentry = lookup_hash(&newnd.last, new_dir);
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto exit4;

	error = vfs_rename(old_dir->d_inode, old_dentry,
				   new_dir->d_inode, new_dentry);

	dput(new_dentry);
exit4:
	dput(old_dentry);
exit3:
	double_up(&new_dir->d_inode->i_sem, &old_dir->d_inode->i_sem);
exit2:
	path_release(&newnd);
exit1:
	path_release(&oldnd);
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
	int len;

	len = PTR_ERR(link);
	if (IS_ERR(link))
		goto out;

	len = strlen(link);
	if (len > (unsigned) buflen)
		len = buflen;
	if (copy_to_user(buffer, link, len))
		len = -EFAULT;
out:
	return len;
}

static inline int
__vfs_follow_link(struct nameidata *nd, const char *link)
{
	if (IS_ERR(link))
		goto fail;

	if (*link == '/') {
		path_release(nd);
		if (!walk_init_root(link, nd))
			/* weird __emul_prefix() stuff did it */
			return 0;
	}
	return path_walk(link, nd);

fail:
	path_release(nd);
	return PTR_ERR(link);
}

int vfs_follow_link(struct nameidata *nd, const char *link)
{
	return __vfs_follow_link(nd, link);
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

int page_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct page *page = NULL;
	char *s = page_getlink(dentry, &page);
	int res = __vfs_follow_link(nd, s);
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
