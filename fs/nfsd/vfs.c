/*
 * linux/fs/nfsd/vfs.c
 *
 * File operations used by nfsd. Some of these have been ripped from
 * other parts of the kernel because they weren't in ksyms.c, others
 * are partial duplicates with added or changed functionality.
 *
 * Note that several functions dget() the dentry upon which they want
 * to act, most notably those that create directory entries. Response
 * dentry's are dput()'d if necessary in the release callback.
 * So if you notice code paths that apparently fail to dput() the
 * dentry, don't worry--they have been taken care of.
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/version.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/ext2_fs.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/unistd.h>
#include <linux/malloc.h>
#include <linux/in.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>

#if LINUX_VERSION_CODE >= 0x020100
#include <asm/uaccess.h>
#endif

extern void fh_update(struct svc_fh*);

#define NFSDDBG_FACILITY		NFSDDBG_FILEOP

/* Open mode for nfsd_open */
#define OPEN_READ	0
#define OPEN_WRITE	1

/* Hack until we have a macro check for mandatory locks. */
#ifndef IS_ISMNDLK
#define IS_ISMNDLK(i)	(((i)->i_mode & (S_ISGID|S_IXGRP)) == S_ISGID)
#endif

/* Check for dir entries '.' and '..' */
#define isdotent(n, l)	(l < 3 && n[0] == '.' && (l == 1 || n[1] == '.'))

/*
 * This is a cache of readahead params that help us choose the proper
 * readahead strategy. Initially, we set all readahead parameters to 0
 * and let the VFS handle things.
 * If you increase the number of cached files very much, you'll need to
 * add a hash table here.
 */
struct raparms {
	struct raparms		*p_next;
	unsigned int		p_count;
	ino_t			p_ino;
	dev_t			p_dev;
	unsigned long		p_reada,
				p_ramax,
				p_raend,
				p_ralen,
				p_rawin;
};

#define FILECACHE_MAX		(2 * NFSD_MAXSERVS)
static struct raparms		raparms[FILECACHE_MAX];
static struct raparms *		raparm_cache = 0;

/*
 * Deny access to certain file systems
 */
static inline int
fs_off_limits(struct super_block *sb)
{
	return !sb || sb->s_magic == NFS_SUPER_MAGIC
	           || sb->s_magic == PROC_SUPER_MAGIC;
}

/*
 * Check whether directory is a mount point, but it is alright if
 * this is precisely the local mount point being exported.
 */
static inline int
nfsd_iscovered(struct dentry *dentry, struct svc_export *exp)
{
	return (dentry != dentry->d_covers &&
		dentry != exp->ex_dentry);
}

/*
 * Look up one component of a pathname.
 * N.B. After this call _both_ fhp and resfh need an fh_put
 */
int
nfsd_lookup(struct svc_rqst *rqstp, struct svc_fh *fhp, const char *name,
					int len, struct svc_fh *resfh)
{
	struct svc_export	*exp;
	struct dentry		*dparent, *dchild;
	int			err;

	dprintk("nfsd: nfsd_lookup(fh %p, %s)\n", SVCFH_DENTRY(fhp), name);

	/* Obtain dentry and export. */
	err = fh_verify(rqstp, fhp, S_IFDIR, MAY_NOP);
	if (err)
		goto out;

	dparent = fhp->fh_dentry;
	exp  = fhp->fh_export;

	err = nfsd_permission(exp, dparent, MAY_EXEC);
	if (err)
		goto out;
	err = nfserr_noent;
	if (fs_off_limits(dparent->d_sb))
		goto out;
	err = nfserr_acces;
	if (nfsd_iscovered(dparent, exp))
		goto out;

	/* Lookup the name, but don't follow links */
	dchild = lookup_dentry(name, dget(dparent), 0);
	if (IS_ERR(dchild))
		goto out_nfserr;
	/*
	 * Make sure we haven't crossed a mount point ...
	 */
	if (dchild->d_sb != dparent->d_sb) {
#ifdef NFSD_PARANOIA
printk("nfsd_lookup: %s/%s crossed mount point!\n", dparent->d_name.name, name);
#endif
		goto out_dput;
	}

	/*
	 * Note: we compose the filehandle now, but as the
	 * dentry may be negative, it may need to be updated.
	 */
	fh_compose(resfh, exp, dchild);
	err = nfserr_noent;
	if (dchild->d_inode)
		err = 0;
out:
	return err;

out_nfserr:
	err = nfserrno(-PTR_ERR(dchild));
	goto out;
out_dput:
	dput(dchild);
	err = nfserr_acces;
	goto out;
}

/*
 * Set various file attributes.
 * N.B. After this call fhp needs an fh_put
 */
int
nfsd_setattr(struct svc_rqst *rqstp, struct svc_fh *fhp, struct iattr *iap)
{
	struct dentry	*dentry;
	struct inode	*inode;
	int		accmode = MAY_SATTR;
	int		ftype = 0;
	int		imode;
	int		err;

	if (iap->ia_valid & (ATTR_ATIME | ATTR_MTIME | ATTR_SIZE))
		accmode |= MAY_WRITE;
	if (iap->ia_valid & ATTR_SIZE)
		ftype = S_IFREG;

	/* Get inode */
	err = fh_verify(rqstp, fhp, ftype, accmode);
	if (err)
		goto out;

	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;

	/* The size case is special... */
	if ((iap->ia_valid & ATTR_SIZE) && S_ISREG(inode->i_mode)) {
		if (iap->ia_size < inode->i_size) {
			err = nfsd_permission(fhp->fh_export, dentry, MAY_TRUNC);
			if (err != 0)
				goto out;
		}
		if ((err = get_write_access(inode)) != 0)
			return nfserrno(-err);
		inode->i_size = iap->ia_size;
		if (inode->i_op && inode->i_op->truncate)
			inode->i_op->truncate(inode);
		mark_inode_dirty(inode);
		put_write_access(inode);
		iap->ia_valid &= ATTR_SIZE;
		iap->ia_valid |= ATTR_MTIME;
		iap->ia_mtime = CURRENT_TIME;
	}

	imode = inode->i_mode;
	if (iap->ia_valid & ATTR_MODE) {
		iap->ia_mode &= S_IALLUGO;
		imode = iap->ia_mode |= (imode & ~S_IALLUGO);
	}

	/* Revoke setuid/setgid bit on chown/chgrp */
	if ((iap->ia_valid & ATTR_UID) && (imode & S_ISUID)
	 && iap->ia_uid != inode->i_uid) {
		iap->ia_valid |= ATTR_MODE;
		iap->ia_mode = imode &= ~S_ISUID;
	}
	if ((iap->ia_valid & ATTR_GID) && (imode & S_ISGID)
	 && iap->ia_gid != inode->i_gid) {
		iap->ia_valid |= ATTR_MODE;
		iap->ia_mode = imode &= ~S_ISGID;
	}

	/* Change the attributes. */
	if (iap->ia_valid) {
		iap->ia_valid |= ATTR_CTIME;
		iap->ia_ctime = CURRENT_TIME;
		err = notify_change(dentry, iap);
		if (err)
			return nfserrno(-err);
		if (EX_ISSYNC(fhp->fh_export))
			write_inode_now(inode);
	}
	err = 0;
out:
	return err;
}

/*
 * Open an existing file or directory.
 * The wflag argument indicates write access.
 * N.B. After this call fhp needs an fh_put
 */
int
nfsd_open(struct svc_rqst *rqstp, struct svc_fh *fhp, int type,
			int wflag, struct file *filp)
{
	struct dentry	*dentry;
	struct inode	*inode;
	int		access, err;

	access = wflag? MAY_WRITE : MAY_READ;
	err = fh_verify(rqstp, fhp, type, access);
	if (err)
		goto out;

	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;

	/* Disallow access to files with the append-only bit set or
	 * with mandatory locking enabled
	 */
	err = nfserr_perm;
	if (IS_APPEND(inode) || IS_ISMNDLK(inode))
		goto out;
	if (!inode->i_op || !inode->i_op->default_file_ops)
		goto out;

	if (wflag && (err = get_write_access(inode)) != 0)
		goto out_nfserr;

	memset(filp, 0, sizeof(*filp));
	filp->f_op    = inode->i_op->default_file_ops;
	filp->f_count = 1;
	filp->f_flags = wflag? O_WRONLY : O_RDONLY;
	filp->f_mode  = wflag? FMODE_WRITE : FMODE_READ;
	filp->f_dentry = dentry;

	err = 0;
	if (filp->f_op && filp->f_op->open) {
		err = filp->f_op->open(inode, filp);
		if (err) {
			if (wflag)
				put_write_access(inode);

			/* I nearly added put_filp() call here, but this filp
			 * is really on callers stack frame. -DaveM
			 */
			filp->f_count--;
		}
	}
out_nfserr:
	if (err)
		err = nfserrno(-err);
out:
	return err;
}

/*
 * Close a file.
 */
void
nfsd_close(struct file *filp)
{
	struct dentry	*dentry = filp->f_dentry;
	struct inode	*inode = dentry->d_inode;

	if (!inode->i_count)
		printk(KERN_WARNING "nfsd: inode count == 0!\n");
	if (!dentry->d_count)
		printk(KERN_WARNING "nfsd: wheee, %s/%s d_count == 0!\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
	if (filp->f_op && filp->f_op->release)
		filp->f_op->release(inode, filp);
	if (filp->f_mode & FMODE_WRITE)
		put_write_access(inode);
}

/*
 * Sync a file
 */
void
nfsd_sync(struct inode *inode, struct file *filp)
{
	filp->f_op->fsync(filp, filp->f_dentry);
}

/*
 * Obtain the readahead parameters for the file
 * specified by (dev, ino).
 */
static inline struct raparms *
nfsd_get_raparms(dev_t dev, ino_t ino)
{
	struct raparms	*ra, **rap, **frap = NULL;

	for (rap = &raparm_cache; (ra = *rap); rap = &ra->p_next) {
		if (ra->p_ino == ino && ra->p_dev == dev)
			goto found;
		if (ra->p_count == 0)
			frap = rap;
	}
	if (!frap)
		return NULL;
	rap = frap;
	ra = *frap;
	memset(ra, 0, sizeof(*ra));
	ra->p_dev = dev;
	ra->p_ino = ino;
found:
	if (rap != &raparm_cache) {
		*rap = ra->p_next;
		ra->p_next   = raparm_cache;
		raparm_cache = ra;
	}
	ra->p_count++;
	return ra;
}

/*
 * Read data from a file. count must contain the requested read count
 * on entry. On return, *count contains the number of bytes actually read.
 * N.B. After this call fhp needs an fh_put
 */
int
nfsd_read(struct svc_rqst *rqstp, struct svc_fh *fhp, loff_t offset, char *buf,
						unsigned long *count)
{
	struct raparms	*ra;
	mm_segment_t	oldfs;
	int		err;
	struct file	file;

	err = nfsd_open(rqstp, fhp, S_IFREG, OPEN_READ, &file);
	if (err)
		goto out;
	err = nfserr_perm;
	if (!file.f_op->read)
		goto out_close;

	/* Get readahead parameters */
	ra = nfsd_get_raparms(fhp->fh_handle.fh_dev, fhp->fh_handle.fh_ino);
	if (ra) {
		file.f_reada = ra->p_reada;
		file.f_ramax = ra->p_ramax;
		file.f_raend = ra->p_raend;
		file.f_ralen = ra->p_ralen;
		file.f_rawin = ra->p_rawin;
	}
	file.f_pos = offset;

	oldfs = get_fs(); set_fs(KERNEL_DS);
	err = file.f_op->read(&file, buf, *count, &file.f_pos);
	set_fs(oldfs);

	/* Write back readahead params */
	if (ra != NULL) {
		dprintk("nfsd: raparms %ld %ld %ld %ld %ld\n",
			file.f_reada, file.f_ramax, file.f_raend,
			file.f_ralen, file.f_rawin);
		ra->p_reada = file.f_reada;
		ra->p_ramax = file.f_ramax;
		ra->p_raend = file.f_raend;
		ra->p_ralen = file.f_ralen;
		ra->p_rawin = file.f_rawin;
		ra->p_count -= 1;
	}

	if (err >= 0) {
		*count = err;
		err = 0;
	} else 
		err = nfserrno(-err);
out_close:
	nfsd_close(&file);
out:
	return err;
}

/*
 * Write data to a file.
 * The stable flag requests synchronous writes.
 * N.B. After this call fhp needs an fh_put
 */
int
nfsd_write(struct svc_rqst *rqstp, struct svc_fh *fhp, loff_t offset,
				char *buf, unsigned long cnt, int stable)
{
	struct svc_export	*exp;
	struct file		file;
	struct dentry		*dentry;
	struct inode		*inode;
	mm_segment_t		oldfs;
	int			err = 0;

	if (!cnt)
		goto out;
	err = nfsd_open(rqstp, fhp, S_IFREG, OPEN_WRITE, &file);
	if (err)
		goto out;
	err = nfserr_perm;
	if (!file.f_op->write)
		goto out_close;

	dentry = file.f_dentry;
	inode = dentry->d_inode;
	exp   = fhp->fh_export;

	/*
	 * Request sync writes if
	 *  -	the sync export option has been set, or
	 *  -	the client requested O_SYNC behavior (NFSv3 feature).
	 * When gathered writes have been configured for this volume,
	 * flushing the data to disk is handled separately below.
	 */
	if ((stable || (stable = EX_ISSYNC(exp))) && !EX_WGATHER(exp))
		file.f_flags |= O_SYNC;

	fh_lock(fhp);			/* lock inode */
	file.f_pos = offset;		/* set write offset */

	/* Write the data. */
	oldfs = get_fs(); set_fs(KERNEL_DS);
	err = file.f_op->write(&file, buf, cnt, &file.f_pos);
	set_fs(oldfs);

	/* clear setuid/setgid flag after write */
	if (err >= 0 && (inode->i_mode & (S_ISUID | S_ISGID))) {
		struct iattr	ia;

		ia.ia_valid = ATTR_MODE;
		ia.ia_mode  = inode->i_mode & ~(S_ISUID | S_ISGID);
		notify_change(dentry, &ia);
	}

	fh_unlock(fhp);			/* unlock inode */

	if (err >= 0 && stable) {
		static unsigned long	last_ino = 0;
		static kdev_t		last_dev = NODEV;

		/*
		 * Gathered writes: If another process is currently
		 * writing to the file, there's a high chance
		 * this is another nfsd (triggered by a bulk write
		 * from a client's biod). Rather than syncing the
		 * file with each write request, we sleep for 10 msec.
		 *
		 * I don't know if this roughly approximates
		 * C. Juszak's idea of gathered writes, but it's a
		 * nice and simple solution (IMHO), and it seems to
		 * work:-)
		 */
		if (EX_WGATHER(exp) && (inode->i_writecount > 1
		 || (last_ino == inode->i_ino && last_dev == inode->i_dev))) {
#if 0
			current->timeout = jiffies + 10 * HZ / 1000;
			interruptible_sleep_on(&inode->i_wait);
#else
			dprintk("nfsd: write defer %d\n", current->pid);
			need_resched = 1;
			current->timeout = jiffies + HZ / 100;
			schedule();
			dprintk("nfsd: write resume %d\n", current->pid);
#endif
		}

		if (inode->i_state & I_DIRTY) {
			dprintk("nfsd: write sync %d\n", current->pid);
			nfsd_sync(inode, &file);
			write_inode_now(inode);
		}
		wake_up(&inode->i_wait);
		last_ino = inode->i_ino;
		last_dev = inode->i_dev;
	}

	dprintk("nfsd: write complete\n");
	if (err >= 0)
		err = 0;
	else 
		err = nfserrno(-err);
out_close:
	nfsd_close(&file);
out:
	return err;
}

/*
 * Create a file (regular, directory, device, fifo).
 * UNIX sockets not yet implemented.
 * N.B. Every call to nfsd_create needs an fh_put for _both_ fhp and resfhp
 */
int
nfsd_create(struct svc_rqst *rqstp, struct svc_fh *fhp,
		char *fname, int flen, struct iattr *iap,
		int type, dev_t rdev, struct svc_fh *resfhp)
{
	struct dentry	*dentry, *dchild;
	struct inode	*dirp;
	int		err;

	err = nfserr_perm;
	if (!flen)
		goto out;
	if (!(iap->ia_valid & ATTR_MODE))
		iap->ia_mode = 0;
	err = fh_verify(rqstp, fhp, S_IFDIR, MAY_CREATE);
	if (err)
		goto out;

	dentry = fhp->fh_dentry;
	dirp = dentry->d_inode;

	/* Get all the sanity checks out of the way before we lock the parent. */
	err = nfserr_notdir;
	if(!dirp->i_op || !dirp->i_op->lookup)
		goto out;
	err = nfserr_perm;
	if (type == S_IFREG) {
		if(!dirp->i_op->create)
			goto out;
	} else if(type == S_IFDIR) {
		if(!dirp->i_op->mkdir)
			goto out;
	} else if((type == S_IFCHR) || (type == S_IFBLK) || (type == S_IFIFO)) {
		if(!dirp->i_op->mknod)
			goto out;
	} else {
		goto out;
	}

	/*
	 * The response filehandle may have been setup already ...
	 */
	if (!resfhp->fh_dverified) {
		dchild = lookup_dentry(fname, dget(dentry), 0);
		err = PTR_ERR(dchild);
		if(IS_ERR(dchild))
			goto out_nfserr;
		fh_compose(resfhp, fhp->fh_export, dchild);
	} else
		dchild = resfhp->fh_dentry;
	/*
	 * Make sure the child dentry is still negative ...
	 */
	if (dchild->d_inode) {
		printk("nfsd_create: dentry %s/%s not negative!\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
	}

	/* Looks good, lock the directory. */
	fh_lock(fhp);
	switch (type) {
	case S_IFREG:
		err = dirp->i_op->create(dirp, dchild, iap->ia_mode);
		break;
	case S_IFDIR:
		err = dirp->i_op->mkdir(dirp, dchild, iap->ia_mode);
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
		err = dirp->i_op->mknod(dirp, dchild, iap->ia_mode, rdev);
		break;
	}
	fh_unlock(fhp);

	if (err < 0)
		goto out_nfserr;
	if (EX_ISSYNC(fhp->fh_export))
		write_inode_now(dirp);

	/*
	 * Update the filehandle to get the new inode info.
	 */
	fh_update(resfhp);

	/* Set file attributes. Mode has already been set and
	 * setting uid/gid works only for root. Irix appears to
	 * send along the gid when it tries to implement setgid
	 * directories via NFS.
	 */
	err = 0;
	if ((iap->ia_valid &= (ATTR_UID|ATTR_GID|ATTR_MODE)) != 0)
		err = nfsd_setattr(rqstp, resfhp, iap);
out:
	return err;

out_nfserr:
	err = nfserrno(-err);
	goto out;
}

/*
 * Truncate a file.
 * The calling routines must make sure to update the ctime
 * field and call notify_change.
 *
 * XXX Nobody calls this thing? -DaveM
 * N.B. After this call fhp needs an fh_put
 */
int
nfsd_truncate(struct svc_rqst *rqstp, struct svc_fh *fhp, unsigned long size)
{
	struct dentry	*dentry;
	struct inode	*inode;
	struct iattr	newattrs;
	int		err;

	err = fh_verify(rqstp, fhp, S_IFREG, MAY_WRITE | MAY_TRUNC);
	if (err)
		goto out;

	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;

	err = get_write_access(inode);
	if (err)
		goto out_nfserr;

	/* Things look sane, lock and do it. */
	fh_lock(fhp);
	newattrs.ia_size = size;
	newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME;
	err = notify_change(dentry, &newattrs);
	if (!err) {
		vmtruncate(inode, size);
		if (inode->i_op && inode->i_op->truncate)
			inode->i_op->truncate(inode);
	}
	put_write_access(inode);
	fh_unlock(fhp);
out_nfserr:
	if (err)
		err = nfserrno(-err);
out:
	return err;
}

/*
 * Read a symlink. On entry, *lenp must contain the maximum path length that
 * fits into the buffer. On return, it contains the true length.
 * N.B. After this call fhp needs an fh_put
 */
int
nfsd_readlink(struct svc_rqst *rqstp, struct svc_fh *fhp, char *buf, int *lenp)
{
	struct dentry	*dentry;
	struct inode	*inode;
	mm_segment_t	oldfs;
	int		err;

	err = fh_verify(rqstp, fhp, S_IFLNK, MAY_READ);
	if (err)
		goto out;

	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;

	err = nfserr_inval;
	if (!inode->i_op || !inode->i_op->readlink)
		goto out;

	UPDATE_ATIME(inode);
	/* N.B. Why does this call need a get_fs()?? */
	oldfs = get_fs(); set_fs(KERNEL_DS);
	err = inode->i_op->readlink(dentry, buf, *lenp);
	set_fs(oldfs);

	if (err < 0)
		goto out_nfserr;
	*lenp = err;
	err = 0;
out:
	return err;

out_nfserr:
	err = nfserrno(-err);
	goto out;
}

/*
 * Create a symlink and look up its inode
 * N.B. After this call _both_ fhp and resfhp need an fh_put
 */
int
nfsd_symlink(struct svc_rqst *rqstp, struct svc_fh *fhp,
				char *fname, int flen,
				char *path,  int plen,
				struct svc_fh *resfhp)
{
	struct dentry	*dentry, *dnew;
	struct inode	*dirp;
	int		err;

	err = nfserr_noent;
	if (!flen || !plen)
		goto out;

	err = fh_verify(rqstp, fhp, S_IFDIR, MAY_CREATE);
	if (err)
		goto out;
	dentry = fhp->fh_dentry;

	err = nfserr_perm;
	if (nfsd_iscovered(dentry, fhp->fh_export))
		goto out;
	dirp = dentry->d_inode;
	if (!dirp->i_op	|| !dirp->i_op->symlink)
		goto out;

	dnew = lookup_dentry(fname, dget(dentry), 0);
	err = PTR_ERR(dnew);
	if (IS_ERR(dnew))
		goto out_nfserr;

	err = -EEXIST;
	if (!dnew->d_inode) {
		fh_lock(fhp);
		err = dirp->i_op->symlink(dirp, dnew, path);
		fh_unlock(fhp);
		if (!err) {
			if (EX_ISSYNC(fhp->fh_export))
				write_inode_now(dirp);
		}
	}
	fh_compose(resfhp, fhp->fh_export, dnew);
	if (err)
		goto out_nfserr;
out:
	return err;

out_nfserr:
	err = nfserrno(-err);
	goto out;
}

/*
 * Create a hardlink
 * N.B. After this call _both_ ffhp and tfhp need an fh_put
 */
int
nfsd_link(struct svc_rqst *rqstp, struct svc_fh *ffhp,
				char *fname, int len, struct svc_fh *tfhp)
{
	struct dentry	*ddir, *dnew, *dold;
	struct inode	*dirp, *dest;
	int		err;

	err = fh_verify(rqstp, ffhp, S_IFDIR, MAY_CREATE);
	if (err)
		goto out;
	err = fh_verify(rqstp, tfhp, S_IFREG, MAY_NOP);
	if (err)
		goto out;

	ddir = ffhp->fh_dentry;
	dirp = ddir->d_inode;

	dnew = lookup_dentry(fname, dget(ddir), 0);
	err = PTR_ERR(dnew);
	if (IS_ERR(dnew))
		goto out_nfserr;

	err = -EEXIST;
	if (dnew->d_inode)
		goto dput_and_out;

	err = -EPERM;
	if (!len)
		goto dput_and_out;

	dold = tfhp->fh_dentry;
	dest = dold->d_inode;

	err = -EACCES;
	if (nfsd_iscovered(ddir, ffhp->fh_export))
		goto dput_and_out;
	if (dirp->i_dev != dest->i_dev)
		goto dput_and_out;	/* FIXME: nxdev for NFSv3 */

	err = -EPERM;
	if (IS_IMMUTABLE(dest) /* || IS_APPEND(dest) */ )
		goto dput_and_out;
	if (!dirp->i_op || !dirp->i_op->link)
		goto dput_and_out;

	fh_lock(ffhp);
	err = dirp->i_op->link(dold, dirp, dnew);
	fh_unlock(ffhp);

	if (!err && EX_ISSYNC(ffhp->fh_export)) {
		write_inode_now(dirp);
		write_inode_now(dest);
	}
dput_and_out:
	dput(dnew);
	if (err)
		goto out_nfserr;
out:
	return err;

out_nfserr:
	err = nfserrno(-err);
	goto out;
}

/*
 * This follows the model of double_lock() in the VFS.
 */
static inline void nfsd_double_down(struct semaphore *s1, struct semaphore *s2)
{
	if (s1 != s2) {
		if ((unsigned long) s1 < (unsigned long) s2) {
			struct semaphore *tmp = s1;
			s1 = s2;
			s2 = tmp;
		}
		down(s1);
	}
	down(s2);
}

static inline void nfsd_double_up(struct semaphore *s1, struct semaphore *s2)
{
	up(s1);
	if (s1 != s2)
		up(s2);
}

/*
 * Rename a file
 * N.B. After this call _both_ ffhp and tfhp need an fh_put
 */
int
nfsd_rename(struct svc_rqst *rqstp, struct svc_fh *ffhp, char *fname, int flen,
			    struct svc_fh *tfhp, char *tname, int tlen)
{
	struct dentry	*fdentry, *tdentry, *odentry, *ndentry;
	struct inode	*fdir, *tdir;
	int		err;

	err = fh_verify(rqstp, ffhp, S_IFDIR, MAY_REMOVE);
	if (err)
		goto out;
	err = fh_verify(rqstp, tfhp, S_IFDIR, MAY_CREATE);
	if (err)
		goto out;

	fdentry = ffhp->fh_dentry;
	fdir = fdentry->d_inode;

	tdentry = tfhp->fh_dentry;
	tdir = tdentry->d_inode;

	/* N.B. We shouldn't need this ... dentry layer handles it */
	err = nfserr_perm;
	if (!flen || (fname[0] == '.' && 
	    (flen == 1 || (flen == 2 && fname[1] == '.'))) ||
	    !tlen || (tname[0] == '.' && 
	    (tlen == 1 || (tlen == 2 && tname[1] == '.'))))
		goto out;

	err = -EXDEV;
	if (fdir->i_dev != tdir->i_dev)
		goto out_nfserr;
	err = -EPERM;
	if (!fdir->i_op || !fdir->i_op->rename)
		goto out_nfserr;

	odentry = lookup_dentry(fname, dget(fdentry), 0);
	err = PTR_ERR(odentry);
	if (IS_ERR(odentry))
		goto out_nfserr;

	ndentry = lookup_dentry(tname, dget(tdentry), 0);
	err = PTR_ERR(ndentry);
	if (IS_ERR(ndentry))
		goto out_dput_old;

	/*
	 * Lock the parent directories.
	 */
	nfsd_double_down(&tdir->i_sem, &fdir->i_sem);
	/* N.B. check for parent changes after locking?? */

	err = fdir->i_op->rename(fdir, odentry, tdir, ndentry);
	if (!err && EX_ISSYNC(tfhp->fh_export)) {
		write_inode_now(fdir);
		write_inode_now(tdir);
	}
	nfsd_double_up(&tdir->i_sem, &fdir->i_sem);
	dput(ndentry);

out_dput_old:
	dput(odentry);
	if (err)
		goto out_nfserr;
out:
	return err;

out_nfserr:
	err = nfserrno(-err);
	goto out;
}

/*
 * Unlink a file or directory
 * N.B. After this call fhp needs an fh_put
 */
int
nfsd_unlink(struct svc_rqst *rqstp, struct svc_fh *fhp, int type,
				char *fname, int flen)
{
	struct dentry	*dentry, *rdentry;
	struct inode	*dirp;
	int		err;

	/* N.B. We shouldn't need this test ... handled by dentry layer */
	err = nfserr_acces;
	if (!flen || isdotent(fname, flen))
		goto out;
	err = fh_verify(rqstp, fhp, S_IFDIR, MAY_REMOVE);
	if (err)
		goto out;

	dentry = fhp->fh_dentry;
	dirp = dentry->d_inode;

	rdentry = lookup_dentry(fname, dget(dentry), 0);
	err = PTR_ERR(rdentry);
	if (IS_ERR(rdentry))
		goto out_nfserr;

	fh_lock(fhp);
	if (type == S_IFDIR) {
		err = -ENOTDIR;
		if (dirp->i_op && dirp->i_op->rmdir)
			err = dirp->i_op->rmdir(dirp, rdentry);
	} else {
		err = -EPERM;
		if (dirp->i_op && dirp->i_op->unlink)
			err = dirp->i_op->unlink(dirp, rdentry);
	}
	fh_unlock(fhp);

	dput(rdentry);
	if (err)
		goto out_nfserr;
	if (EX_ISSYNC(fhp->fh_export))
		write_inode_now(dirp);
out:
	return err;

out_nfserr:
	err = nfserrno(-err);
	goto out;
}

/*
 * Read entries from a directory.
 */
int
nfsd_readdir(struct svc_rqst *rqstp, struct svc_fh *fhp, loff_t offset, 
			encode_dent_fn func, u32 *buffer, int *countp)
{
	struct inode	*inode;
	u32		*p;
	int		oldlen, eof, err;
	struct file	file;
	struct readdir_cd cd;

	err = 0;
	if (offset > ~(u32) 0)
		goto out;

	err = nfsd_open(rqstp, fhp, S_IFDIR, OPEN_READ, &file);
	if (err)
		goto out;

	err = nfserr_notdir;
	if (!file.f_op->readdir)
		goto out_close;
	file.f_pos = offset;

	/* Set up the readdir context */
	memset(&cd, 0, sizeof(cd));
	cd.rqstp  = rqstp;
	cd.buffer = buffer;
	cd.buflen = *countp >> 2;

	/*
	 * Read the directory entries. This silly loop is necessary because
	 * readdir() is not guaranteed to fill up the entire buffer, but
	 * may choose to do less.
	 */
	inode = file.f_dentry->d_inode;
	while (1) {
		oldlen = cd.buflen;

		/*
		dprintk("nfsd: f_op->readdir(%x/%ld @ %d) buflen = %d (%d)\n",
			file.f_inode->i_dev, file.f_inode->i_ino,
			(int) file.f_pos, (int) oldlen, (int) cd.buflen);
		 */
		down(&inode->i_sem);
		err = file.f_op->readdir(&file, &cd, (filldir_t) func);
		up(&inode->i_sem);
		if (err < 0)
			goto out_nfserr;
		if (oldlen == cd.buflen)
			break;
		if (cd.eob)
			break;
	}

	/* If we didn't fill the buffer completely, we're at EOF */
	eof = !cd.eob;

	/* Hewlett Packard ignores the eof flag on READDIR. Some
	 * fs-specific readdir implementations seem to reset f_pos to 0
	 * at EOF however, causing an endless loop. */
	if (cd.offset && !eof)
		*cd.offset = htonl(file.f_pos);

	p = cd.buffer;
	*p++ = 0;			/* no more entries */
	*p++ = htonl(eof);		/* end of directory */
	*countp = (caddr_t) p - (caddr_t) buffer;

	dprintk("nfsd: readdir result %d bytes, eof %d offset %ld\n",
				*countp, eof,
				cd.offset? ntohl(*cd.offset) : -1);
	err = 0;
out_close:
	nfsd_close(&file);
out:
	return err;

out_nfserr:
	err = nfserrno(-err);
	goto out_close;
}

/*
 * Get file system stats
 * N.B. After this call fhp needs an fh_put
 */
int
nfsd_statfs(struct svc_rqst *rqstp, struct svc_fh *fhp, struct statfs *stat)
{
	struct dentry		*dentry;
	struct inode		*inode;
	struct super_block	*sb;
	mm_segment_t		oldfs;
	int			err;

	err = fh_verify(rqstp, fhp, 0, MAY_NOP);
	if (err)
		goto out;
	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;

	err = nfserr_io;
	if (!(sb = inode->i_sb) || !sb->s_op->statfs)
		goto out;

	oldfs = get_fs();
	set_fs (KERNEL_DS);
	sb->s_op->statfs(sb, stat, sizeof(*stat));
	set_fs (oldfs);
	err = 0;

out:
	return err;
}

/*
 * Check for a user's access permissions to this inode.
 */
int
nfsd_permission(struct svc_export *exp, struct dentry *dentry, int acc)
{
	struct inode	*inode = dentry->d_inode;
	int		err;

	if (acc == MAY_NOP)
		return 0;

	/*
	dprintk("nfsd: permission 0x%x%s%s%s%s%s mode 0%o%s%s%s\n",
		acc,
		(acc & MAY_READ)?	" read"  : "",
		(acc & MAY_WRITE)?	" write" : "",
		(acc & MAY_EXEC)?	" exec"  : "",
		(acc & MAY_SATTR)?	" sattr" : "",
		(acc & MAY_TRUNC)?	" trunc" : "",
		inode->i_mode,
		IS_IMMUTABLE(inode)?	" immut" : "",
		IS_APPEND(inode)?	" append" : "",
		IS_RDONLY(inode)?	" ro" : "");
	dprintk("      owner %d/%d user %d/%d\n",
		inode->i_uid, inode->i_gid, current->fsuid, current->fsgid);
	 */

	if (acc & (MAY_WRITE | MAY_SATTR | MAY_TRUNC)) {
		if (EX_RDONLY(exp) || IS_RDONLY(inode))
			return nfserr_rofs;
		if (S_ISDIR(inode->i_mode) && nfsd_iscovered(dentry, exp))
			return nfserr_perm;
		if (/* (acc & MAY_WRITE) && */ IS_IMMUTABLE(inode))
			return nfserr_perm;
	}
	if ((acc & MAY_TRUNC) && IS_APPEND(inode))
		return nfserr_perm;

	/*
	 * The file owner always gets access permission. This is to make
	 * file access work even when the client has done a fchmod(fd, 0).
	 *
	 * However, `cp foo bar' should fail nevertheless when bar is
	 * readonly. A sensible way to do this might be to reject all
	 * attempts to truncate a read-only file, because a creat() call
	 * always implies file truncation.
	 */
	if (inode->i_uid == current->fsuid /* && !(acc & MAY_TRUNC) */)
		return 0;

	err = permission(inode, acc & (MAY_READ|MAY_WRITE|MAY_EXEC));

	/* Allow read access to binaries even when mode 111 */
	if (err == -EPERM && S_ISREG(inode->i_mode) && acc == MAY_READ)
		err = permission(inode, MAY_EXEC);

	return err? nfserrno(-err) : 0;
}

/*
 * Initialize readahead param cache
 */
void
nfsd_racache_init(void)
{
	int	i;

	if (raparm_cache)
		return;
	memset(raparms, 0, sizeof(raparms));
	for (i = 0; i < FILECACHE_MAX - 1; i++) {
		raparms[i].p_next = raparms + i + 1;
	}
	raparm_cache = raparms;
}
