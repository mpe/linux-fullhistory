/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/root.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *  Copyright 1999 Jeremy Fitzhardinge <jeremy@goop.org>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/param.h>
#include "autofs_i.h"

static int autofs4_dir_readdir(struct file *,void *,filldir_t);
static struct dentry *autofs4_dir_lookup(struct inode *,struct dentry *);
static int autofs4_dir_symlink(struct inode *,struct dentry *,const char *);
static int autofs4_dir_unlink(struct inode *,struct dentry *);
static int autofs4_dir_rmdir(struct inode *,struct dentry *);
static int autofs4_dir_mkdir(struct inode *,struct dentry *,int);
static int autofs4_root_ioctl(struct inode *, struct file *,unsigned int,unsigned long);
static struct dentry *autofs4_root_lookup(struct inode *,struct dentry *);

struct file_operations autofs4_root_operations = {
	read:		generic_read_dir,
	readdir:	autofs4_dir_readdir,
	ioctl:		autofs4_root_ioctl,
};

struct file_operations autofs4_dir_operations = {
	read:		generic_read_dir,
	readdir:	autofs4_dir_readdir,
};

struct inode_operations autofs4_root_inode_operations = {
	lookup:		autofs4_root_lookup,
	unlink:		autofs4_dir_unlink,
	symlink:	autofs4_dir_symlink,
	mkdir:		autofs4_dir_mkdir,
	rmdir:		autofs4_dir_rmdir,
};

struct inode_operations autofs4_dir_inode_operations = {
	lookup:		autofs4_dir_lookup,
	unlink:		autofs4_dir_unlink,
	symlink:	autofs4_dir_symlink,
	mkdir:		autofs4_dir_mkdir,
	rmdir:		autofs4_dir_rmdir,
};

static inline struct dentry *nth_child(struct dentry *dir, int nr)
{
	struct list_head *tmp = dir->d_subdirs.next;

	while(tmp != &dir->d_subdirs) {
		if (nr-- == 0)
			return list_entry(tmp, struct dentry, d_child);
		tmp = tmp->next;
	}
	return NULL;
}

static int autofs4_dir_readdir(struct file *filp, void *dirent,
			      filldir_t filldir)
{
	struct autofs_sb_info *sbi;
	struct autofs_info *ino;
	struct dentry *dentry = filp->f_dentry;
	struct dentry *dent_ptr;
	struct inode *dir = dentry->d_inode;
	struct list_head *cursor;
	off_t nr;

	sbi = autofs4_sbi(dir->i_sb);
	ino = autofs4_dentry_ino(dentry);
	nr = filp->f_pos;

	switch(nr)
	{
	case 0:
		if (filldir(dirent, ".", 1, nr, dir->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, nr, dentry->d_covers->d_parent->d_inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	default:
		dent_ptr = nth_child(dentry, nr-2);
		if (dent_ptr == NULL)
			break;

		cursor = &dent_ptr->d_child;

		while(cursor != &dentry->d_subdirs) {
			dent_ptr = list_entry(cursor, struct dentry, d_child);
			if (dent_ptr->d_inode &&
			    filldir(dirent, dent_ptr->d_name.name, dent_ptr->d_name.len, nr,
				    dent_ptr->d_inode->i_ino) < 0)
				return 0;
			filp->f_pos = ++nr;
			cursor = cursor->next;
		}
		break;
	}

	return 0;
}

/* Update usage from here to top of tree, so that scan of
   top-level directories will give a useful result */
static void autofs4_update_usage(struct dentry *dentry)
{
	struct dentry *top = dentry->d_sb->s_root;

	for(; dentry != top; dentry = dentry->d_parent) {
		struct autofs_info *ino = autofs4_dentry_ino(dentry->d_covers);

		if (ino) {
			update_atime(dentry->d_inode);
			ino->last_used = jiffies;
		}
	}
}

static int try_to_fill_dentry(struct dentry *dentry, 
			      struct super_block *sb,
			      struct autofs_sb_info *sbi)
{
	struct autofs_info *de_info = autofs4_dentry_ino(dentry);
	int status = 0;

	/* Block on any pending expiry here; invalidate the dentry
           when expiration is done to trigger mount request with a new
           dentry */
	if (de_info && (de_info->flags & AUTOFS_INF_EXPIRING)) {
		DPRINTK(("try_to_fill_entry: waiting for expire %p name=%.*s, flags&PENDING=%s de_info=%p de_info->flags=%x\n",
			 dentry, dentry->d_name.len, dentry->d_name.name, 
			 dentry->d_flags & DCACHE_AUTOFS_PENDING?"t":"f",
			 de_info, de_info?de_info->flags:0));
		status = autofs4_wait(sbi, &dentry->d_name, NFY_NONE);
		
		DPRINTK(("try_to_fill_entry: expire done status=%d\n", status));
		
		return 0;
	}

	DPRINTK(("try_to_fill_entry: dentry=%p %.*s ino=%p\n", 
		 dentry, dentry->d_name.len, dentry->d_name.name, dentry->d_inode));

	/* Wait for a pending mount, triggering one if there isn't one already */
	while(dentry->d_inode == NULL) {
		DPRINTK(("try_to_fill_entry: waiting for mount name=%.*s, de_info=%p de_info->flags=%x\n",
			 dentry->d_name.len, dentry->d_name.name, 
			 de_info, de_info?de_info->flags:0));
		status = autofs4_wait(sbi, &dentry->d_name, NFY_MOUNT);
		 
		DPRINTK(("try_to_fill_entry: mount done status=%d\n", status));

		if (status && dentry->d_inode)
			return 0; /* Try to get the kernel to invalidate this dentry */
		
		/* Turn this into a real negative dentry? */
		if (status == -ENOENT) {
			dentry->d_time = jiffies + AUTOFS_NEGATIVE_TIMEOUT;
			dentry->d_flags &= ~DCACHE_AUTOFS_PENDING;
			return 1;
		} else if (status) {
			/* Return a negative dentry, but leave it "pending" */
			return 1;
		}
		status = autofs4_wait(sbi, &dentry->d_name, NFY_MOUNT);
	}

	/* If this is an unused directory that isn't a mount point,
	   bitch at the daemon and fix it in user space */
	if (S_ISDIR(dentry->d_inode->i_mode) &&
	    dentry->d_mounts == dentry && 
	    list_empty(&dentry->d_subdirs)) {
		DPRINTK(("try_to_fill_entry: mounting existing dir\n"));
		return autofs4_wait(sbi, &dentry->d_name, NFY_MOUNT) == 0;
	}

	/* We don't update the usages for the autofs daemon itself, this
	   is necessary for recursive autofs mounts */
	if (!autofs4_oz_mode(sbi))
		autofs4_update_usage(dentry);

	dentry->d_flags &= ~DCACHE_AUTOFS_PENDING;
	return 1;
}


/*
 * Revalidate is called on every cache lookup.  Some of those
 * cache lookups may actually happen while the dentry is not
 * yet completely filled in, and revalidate has to delay such
 * lookups..
 */
static int autofs4_root_revalidate(struct dentry * dentry, int flags)
{
	struct inode * dir = dentry->d_parent->d_inode;
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino;
	int oz_mode = autofs4_oz_mode(sbi);

	/* Pending dentry */
	if (autofs4_ispending(dentry)) {
		if (autofs4_oz_mode(sbi))
			return 1;
		else
			return try_to_fill_dentry(dentry, dir->i_sb, sbi);
	}

	/* Negative dentry.. invalidate if "old" */
	if (dentry->d_inode == NULL)
		return (dentry->d_time - jiffies <= AUTOFS_NEGATIVE_TIMEOUT);

	ino = autofs4_dentry_ino(dentry);

	/* Check for a non-mountpoint directory with no contents */
	if (S_ISDIR(dentry->d_inode->i_mode) &&
	    dentry->d_mounts == dentry && 
	    list_empty(&dentry->d_subdirs)) {
		DPRINTK(("autofs_root_revalidate: dentry=%p %.*s, emptydir\n",
			 dentry, dentry->d_name.len, dentry->d_name.name));
		if (autofs4_oz_mode(sbi))
			return 1;
		else
			return try_to_fill_dentry(dentry, dir->i_sb, sbi);
	}

	/* Update the usage list */
	if (!oz_mode)
		autofs4_update_usage(dentry);

	return 1;
}

static int autofs4_revalidate(struct dentry *dentry, int flags)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);

	if (!autofs4_oz_mode(sbi))
		autofs4_update_usage(dentry);

	return 1;
}

static void autofs4_dentry_release(struct dentry *de)
{
	struct autofs_info *inf = autofs4_dentry_ino(de);

	DPRINTK(("autofs4_dentry_release: releasing %p\n", de));

	de->d_fsdata = NULL;
	if (inf) {
		inf->dentry = NULL;
		inf->inode = NULL;

		autofs4_free_ino(inf);
	}
}

/* For dentries of directories in the root dir */
static struct dentry_operations autofs4_root_dentry_operations = {
	d_revalidate:	autofs4_root_revalidate,	/* d_revalidate */
	d_release:	autofs4_dentry_release,
};

/* For other dentries */
static struct dentry_operations autofs4_dentry_operations = {
	d_revalidate:	autofs4_revalidate,	/* d_revalidate */
	d_release:	autofs4_dentry_release,
};

/* Lookups in non-root dirs never find anything - if it's there, it's
   already in the dcache */
static struct dentry *autofs4_dir_lookup(struct inode *dir, struct dentry *dentry)
{
#if 0
	DPRINTK(("autofs_dir_lookup: ignoring lookup of %.*s/%.*s\n",
		 dentry->d_parent->d_name.len, dentry->d_parent->d_name.name,
		 dentry->d_name.len, dentry->d_name.name));
#endif

	dentry->d_fsdata = NULL;
	d_add(dentry, NULL);
	return NULL;
}

/* Lookups in the root directory */
static struct dentry *autofs4_root_lookup(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi;
	int oz_mode;

	DPRINTK(("autofs_root_lookup: name = %.*s\n", 
		 dentry->d_name.len, dentry->d_name.name));

	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENOENT);/* File name too long to exist */

	sbi = autofs4_sbi(dir->i_sb);

	oz_mode = autofs4_oz_mode(sbi);
	DPRINTK(("autofs_lookup: pid = %u, pgrp = %u, catatonic = %d, oz_mode = %d\n",
		 current->pid, current->pgrp, sbi->catatonic, oz_mode));

	/*
	 * Mark the dentry incomplete, but add it. This is needed so
	 * that the VFS layer knows about the dentry, and we can count
	 * on catching any lookups through the revalidate.
	 *
	 * Let all the hard work be done by the revalidate function that
	 * needs to be able to do this anyway..
	 *
	 * We need to do this before we release the directory semaphore.
	 */
	if (dir->i_ino == AUTOFS_ROOT_INO)
		dentry->d_op = &autofs4_root_dentry_operations;
	else
		dentry->d_op = &autofs4_dentry_operations;

	dentry->d_flags |= DCACHE_AUTOFS_PENDING;
	dentry->d_fsdata = NULL;
	d_add(dentry, NULL);

	if (dentry->d_op && dentry->d_op->d_revalidate) {
		up(&dir->i_sem);
		(dentry->d_op->d_revalidate)(dentry, 0);
		down(&dir->i_sem);
	}

	/*
	 * If we are still pending, check if we had to handle
	 * a signal. If so we can force a restart..
	 */
	if (dentry->d_flags & DCACHE_AUTOFS_PENDING) {
		if (signal_pending(current))
			return ERR_PTR(-ERESTARTNOINTR);
	}

	/*
	 * If this dentry is unhashed, then we shouldn't honour this
	 * lookup even if the dentry is positive.  Returning ENOENT here
	 * doesn't do the right thing for all system calls, but it should
	 * be OK for the operations we permit from an autofs.
	 */
	if ( dentry->d_inode && list_empty(&dentry->d_hash) )
		return ERR_PTR(-ENOENT);

	return NULL;
}

static int autofs4_dir_symlink(struct inode *dir, 
			       struct dentry *dentry,
			       const char *symname)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct inode *inode;
	char *cp;

	DPRINTK(("autofs_dir_symlink: %s <- %.*s\n", symname, 
		 dentry->d_name.len, dentry->d_name.name));

	if (!S_ISDIR(dir->i_mode))
		return -ENOTDIR;

	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	if (dentry->d_name.len > NAME_MAX)
		return -ENAMETOOLONG;

	if (dentry->d_inode != NULL)
		return -EEXIST;

	ino = autofs4_init_ino(ino, sbi, S_IFLNK | 0555);
	if (ino == NULL)
		return -ENOSPC;

	ino->size = strlen(symname);
	ino->u.symlink = cp = kmalloc(ino->size + 1, GFP_KERNEL);

	if (cp == NULL) {
		kfree(ino);
		return -ENOSPC;
	}

	strcpy(cp, symname);

	autofs4_ihash_insert(&sbi->ihash, ino);
	inode = iget(dir->i_sb,ino->ino);
	d_instantiate(dentry, inode);

	if (dir->i_ino == AUTOFS_ROOT_INO)
		dentry->d_op = &autofs4_root_dentry_operations;
	else
		dentry->d_op = &autofs4_dentry_operations;

	dentry->d_fsdata = ino;
	ino->dentry = dget(dentry);
	ino->inode = inode;

	dir->i_mtime = CURRENT_TIME;

	return 0;
}

/*
 * NOTE!
 *
 * Normal filesystems would do a "d_delete()" to tell the VFS dcache
 * that the file no longer exists. However, doing that means that the
 * VFS layer can turn the dentry into a negative dentry.  We don't want
 * this, because since the unlink is probably the result of an expire.
 * We simply d_drop it, which allows the dentry lookup to remount it
 * if necessary.
 *
 * If a process is blocked on the dentry waiting for the expire to finish,
 * it will invalidate the dentry and try to mount with a new one.
 *
 * Also see autofs_dir_rmdir().. 
 */
static int autofs4_dir_unlink(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);

	if (!S_ISDIR(dir->i_mode))
		return -ENOTDIR;

	if (dentry->d_inode == NULL)
		return -ENOENT;
	
	/* This allows root to remove symlinks */
	if ( !autofs4_oz_mode(sbi) && !capable(CAP_SYS_ADMIN) )
		return -EACCES;

	dput(ino->dentry);

	dentry->d_inode->i_size = 0;
	dentry->d_inode->i_nlink = 0;

	dir->i_mtime = CURRENT_TIME;

	DPRINTK(("autofs_dir_unlink: unlinking %p %.*s, count=%d\n",
		dentry, dentry->d_name.len, dentry->d_name.name, dentry->d_count));

	d_drop(dentry);
	
	return 0;
}

static int autofs4_dir_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);

	if (!S_ISDIR(dir->i_mode))
		return -ENOTDIR;

	if (dentry->d_inode == NULL)
		return -ENOENT;
	
	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	if (!list_empty(&dentry->d_subdirs))
		return -ENOTEMPTY;

	dput(ino->dentry);

	dentry->d_inode->i_size = 0;
	dentry->d_inode->i_nlink = 0;

	if (dir->i_nlink)
		dir->i_nlink--;

	DPRINTK(("autofs_dir_rmdir: rmdir %p %.*s, count=%d\n",
		dentry, dentry->d_name.len, dentry->d_name.name, dentry->d_count));

	d_drop(dentry);

	return 0;
}



static int autofs4_dir_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct inode *inode;

	if (!S_ISDIR(dir->i_mode))
		return -ENOTDIR;

	if ( !autofs4_oz_mode(sbi) )
		return -EACCES;

	if ( dentry->d_inode != NULL )
		return -EEXIST;
	
	if ( dentry->d_name.len > NAME_MAX )
		return -ENAMETOOLONG;

	DPRINTK(("autofs_dir_mkdir: dentry %p, creating %.*s\n",
		 dentry, dentry->d_name.len, dentry->d_name.name));

	ino = autofs4_init_ino(ino, sbi, S_IFDIR | 0555);
	if (ino == NULL)
		return -ENOSPC;

	autofs4_ihash_insert(&sbi->ihash, ino);

	inode = iget(dir->i_sb, ino->ino);
	d_instantiate(dentry, inode);

	if (dir->i_ino == AUTOFS_ROOT_INO)
		dentry->d_op = &autofs4_root_dentry_operations;
	else
		dentry->d_op = &autofs4_dentry_operations;

	dentry->d_fsdata = ino;
	ino->dentry = dget(dentry);
	ino->inode = inode;
	dir->i_nlink++;
	dir->i_mtime = CURRENT_TIME;

	return 0;
}

/* Get/set timeout ioctl() operation */
static inline int autofs4_get_set_timeout(struct autofs_sb_info *sbi,
					 unsigned long *p)
{
	int rv;
	unsigned long ntimeout;

	if ( (rv = get_user(ntimeout, p)) ||
	     (rv = put_user(sbi->exp_timeout/HZ, p)) )
		return rv;

	if ( ntimeout > ULONG_MAX/HZ )
		sbi->exp_timeout = 0;
	else
		sbi->exp_timeout = ntimeout * HZ;

	return 0;
}

/* Return protocol version */
static inline int autofs4_get_protover(struct autofs_sb_info *sbi, int *p)
{
	return put_user(sbi->version, p);
}

/* Identify autofs_dentries - this is so we can tell if there's
   an extra dentry refcount or not.  We only hold a refcount on the
   dentry if its non-negative (ie, d_inode != NULL)
*/
int is_autofs4_dentry(struct dentry *dentry)
{
	return dentry && dentry->d_inode &&
		(dentry->d_op == &autofs4_root_dentry_operations ||
		 dentry->d_op == &autofs4_dentry_operations) &&
		dentry->d_fsdata != NULL;
}

/*
 * ioctl()'s on the root directory is the chief method for the daemon to
 * generate kernel reactions
 */
static int autofs4_root_ioctl(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg)
{
	struct autofs_sb_info *sbi = autofs4_sbi(inode->i_sb);

	DPRINTK(("autofs_ioctl: cmd = 0x%08x, arg = 0x%08lx, sbi = %p, pgrp = %u\n",
		 cmd,arg,sbi,current->pgrp));

	if ( _IOC_TYPE(cmd) != _IOC_TYPE(AUTOFS_IOC_FIRST) ||
	     _IOC_NR(cmd) - _IOC_NR(AUTOFS_IOC_FIRST) >= AUTOFS_IOC_COUNT )
		return -ENOTTY;
	
	if ( !autofs4_oz_mode(sbi) && !capable(CAP_SYS_ADMIN) )
		return -EPERM;
	
	switch(cmd) {
	case AUTOFS_IOC_READY:	/* Wait queue: go ahead and retry */
		return autofs4_wait_release(sbi,(autofs_wqt_t)arg,0);
	case AUTOFS_IOC_FAIL:	/* Wait queue: fail with ENOENT */
		return autofs4_wait_release(sbi,(autofs_wqt_t)arg,-ENOENT);
	case AUTOFS_IOC_CATATONIC: /* Enter catatonic mode (daemon shutdown) */
		autofs4_catatonic_mode(sbi);
		return 0;
	case AUTOFS_IOC_PROTOVER: /* Get protocol version */
		return autofs4_get_protover(sbi, (int *)arg);
	case AUTOFS_IOC_SETTIMEOUT:
		return autofs4_get_set_timeout(sbi,(unsigned long *)arg);

	/* return a single thing to expire */
	case AUTOFS_IOC_EXPIRE:
		return autofs4_expire_run(inode->i_sb,sbi,
					 (struct autofs_packet_expire *)arg);
	/* same as above, but can send multiple expires through pipe */
	case AUTOFS_IOC_EXPIRE_MULTI:
		return autofs4_expire_multi(inode->i_sb, sbi, (int *)arg);

	default:
		return -ENOSYS;
	}
}
