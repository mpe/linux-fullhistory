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
 * Copyright (C) 1995-1999 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/config.h>
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
#ifdef CONFIG_NFSD_V3
#include <linux/nfs3.h>
#include <linux/nfsd/xdr3.h>
#endif /* CONFIG_NFSD_V3 */
#include <linux/nfsd/nfsfh.h>
#include <linux/quotaops.h>

#if LINUX_VERSION_CODE >= 0x020100
#include <asm/uaccess.h>
#endif

#define NFSDDBG_FACILITY		NFSDDBG_FILEOP
#define NFSD_PARANOIA


/* We must ignore files (but only files) which might have mandatory
 * locks on them because there is no way to know if the accesser has
 * the lock.
 */
#define IS_ISMNDLK(i)	(S_ISREG((i)->i_mode) && MANDATORY_LOCK(i))

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

int nfsd_nservers = 0;
#define FILECACHE_MAX		(2 * nfsd_nservers) 
static struct raparms *		raparml = NULL;
static struct raparms *		raparm_cache = NULL;


/*
 * We need to do a check-parent every time
 * after we have locked the parent - to verify
 * that the parent is still our parent and
 * that we are still hashed onto it..
 *
 * This is required in case two processes race
 * on removing (or moving) the same entry: the
 * parent lock will serialize them, but the
 * other process will be too late..
 *
 * Note that this nfsd_check_parent is identical
 * the check_parent in linux/fs/namei.c.
 */
#define nfsd_check_parent(dir, dentry) \
	((dir) == (dentry)->d_parent && !list_empty(&dentry->d_hash))

/*
 * Lock a parent directory following the VFS locking protocol.
 */
int
fh_lock_parent(struct svc_fh *parent_fh, struct dentry *dchild)
{
	fh_lock(parent_fh);
	/*
	 * Make sure the parent->child relationship still holds,
	 * and that the child is still hashed.
	 */
	if (nfsd_check_parent(parent_fh->fh_dentry, dchild))
	    return 0;

	printk(KERN_WARNING
		"fh_lock_parent: %s/%s parent changed or child unhashed\n",
		dchild->d_parent->d_name.name, dchild->d_name.name);

	fh_unlock(parent_fh);
	return nfserr_noent;
}


/*
 * Look up one component of a pathname.
 * N.B. After this call _both_ fhp and resfh need an fh_put
 *
 * If the lookup would cross a mountpoint, and the mounted filesystem
 * is exported to the client with NFSEXP_CROSSMNT, then the lookup is
 * accepted as it stands and the mounted directory is
 * returned. Otherwise the covered directory is returned.
 * NOTE: this mountpoint crossing is not supported properly by all
 *   clients and is explicitly disallowed for NFSv3
 *      NeilBrown <neilb@cse.unsw.edu.au>
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
	err = fh_verify(rqstp, fhp, S_IFDIR, MAY_EXEC);
	if (err)
		goto out;

	dparent = fhp->fh_dentry;
	exp  = fhp->fh_export;

#if 0
	err = nfsd_permission(exp, dparent, MAY_EXEC);
	if (err)
		goto out;
#endif
	err = nfserr_acces;

	/* Lookup the name, but don't follow links */
	dchild = lookup_dentry(name, dget(dparent), 0);
	if (IS_ERR(dchild))
		goto out_nfserr;
	/*
	 * check if we have crossed a mount point ...
	 */
	if (dchild->d_sb != dparent->d_sb) {
		struct svc_export *exp2 = NULL;
		exp2 = exp_get(rqstp->rq_client,
			       dchild->d_inode->i_dev,
			       dchild->d_inode->i_ino);
		if (exp2 && EX_CROSSMNT(exp2))
			/* successfully crossed mount point */
			exp = exp2;
		else if (dchild->d_covers->d_sb == dparent->d_sb) {
			/* stay in the original filesystem */
			struct dentry *tdentry = dget(dchild->d_covers);
			dput(dchild);
			dchild = tdentry;
		} else {
			/* This cannot possibly happen */
			printk("nfsd_lookup: %s/%s impossible mount point!\n", dparent->d_name.name, dchild->d_name.name);
			dput(dchild);
			err = nfserr_acces;
			goto out;

		}
	}
	/*
	 * Note: we compose the file handle now, but as the
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
	kernel_cap_t	saved_cap = 0;

	if (iap->ia_valid & (ATTR_ATIME | ATTR_MTIME | ATTR_SIZE))
		accmode |= MAY_WRITE;
	if (iap->ia_valid & ATTR_SIZE)
		ftype = S_IFREG;

	/* Get inode */
	err = fh_verify(rqstp, fhp, ftype, accmode);
	if (err || !iap->ia_valid)
		goto out;

	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;

	err = inode_change_ok(inode, iap);
	/* could be a "touch" (utimes) request where the user is not the owner but does
	 * have write permission. In this case the user should be allowed to set
	 * both times to the current time.  We could just assume any such SETATTR
	 * is intended to set the times to "now", but we do a couple of simple tests
	 * to increase our confidence.
	 */
#define BOTH_TIME_SET (ATTR_ATIME_SET | ATTR_MTIME_SET)
#define	MAX_TOUCH_TIME_ERROR (30*60)
	if (err
	    && (iap->ia_valid & BOTH_TIME_SET) == BOTH_TIME_SET
	    && iap->ia_mtime == iap->ia_ctime
	    ) {
	    /* looks good.  now just make sure time is in the right ballpark.
	     * solaris, at least, doesn't seem to care what the time request is
	     */
	    time_t delta = iap->ia_atime - CURRENT_TIME;
	    if (delta<0) delta = -delta;
	    if (delta < MAX_TOUCH_TIME_ERROR) {
		/* turn off ATTR_[AM]TIME_SET but leave ATTR_[AM]TIME
		 * this will cause notify_change to setthese times to "now"
		 */
		iap->ia_valid &= ~BOTH_TIME_SET;
		err = inode_change_ok(inode, iap);
	    }
	}
	    
	if (err)
		goto out_nfserr;

	/* The size case is special. It changes the file as well as the attributes.  */
	if (iap->ia_valid & ATTR_SIZE) {
if (!S_ISREG(inode->i_mode))
printk("nfsd_setattr: size change??\n");
		if (iap->ia_size < inode->i_size) {
			err = nfsd_permission(fhp->fh_export, dentry, MAY_TRUNC);
			if (err)
				goto out;
		}
		err = get_write_access(inode);
		if (err)
			goto out_nfserr;

		err = locks_verify_area(FLOCK_VERIFY_WRITE, inode, NULL,
				  iap->ia_size<inode->i_size ? iap->ia_size : inode->i_size,
				  abs(inode->i_size - iap->ia_size));

		if (err)
			goto out_nfserr;
		DQUOT_INIT(inode);
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


	iap->ia_valid |= ATTR_CTIME;
	if (current->fsuid != 0) {
		saved_cap = current->cap_effective;
		cap_clear(current->cap_effective);
	}
	if (iap->ia_valid & ATTR_SIZE) {
		fh_lock(fhp);
		err = notify_change(dentry, iap);
		if (!err) {
			vmtruncate(inode,iap->ia_size);		
			if (inode->i_op && inode->i_op->truncate)
				inode->i_op->truncate(inode);
		}
		fh_unlock(fhp);
		put_write_access(inode);
	}
	else
		err = notify_change(dentry, iap);
	if (current->fsuid != 0)
		current->cap_effective = saved_cap;
	if (err)
		goto out_nfserr;
	if (EX_ISSYNC(fhp->fh_export))
		write_inode_now(inode);
	err = 0;

	/* Don't unlock inode; the nfssvc_release functions are supposed
	 * to do this. */
out:
	return err;

out_nfserr:
	err = nfserrno(-err);
	goto out;
}

#ifdef CONFIG_NFSD_V3
/*
 * Check server access rights to a file system object
 */
struct accessmap {
	u32		access;
	int		how;
};
static struct accessmap	nfs3_regaccess[] = {
    {	NFS3_ACCESS_READ,	MAY_READ			},
    {	NFS3_ACCESS_EXECUTE,	MAY_EXEC			},
    {	NFS3_ACCESS_MODIFY,	MAY_WRITE|MAY_TRUNC		},
    {	NFS3_ACCESS_EXTEND,	MAY_WRITE			},

    {	0,			0				}
};

static struct accessmap	nfs3_diraccess[] = {
    {	NFS3_ACCESS_READ,	MAY_READ			},
    {	NFS3_ACCESS_LOOKUP,	MAY_EXEC			},
    {	NFS3_ACCESS_MODIFY,	MAY_EXEC|MAY_WRITE|MAY_TRUNC	},
    {	NFS3_ACCESS_EXTEND,	MAY_EXEC|MAY_WRITE		},
    {	NFS3_ACCESS_DELETE,	MAY_REMOVE			},

    {	0,			0				}
};

static struct accessmap	nfs3_anyaccess[] = {
    /* XXX: should we try to cover read/write here for clients that
     * rely on us to do their access checking for special files? */

    {	0,			0				}
};

int
nfsd_access(struct svc_rqst *rqstp, struct svc_fh *fhp, u32 *access)
{
	struct accessmap	*map;
	struct svc_export	*export;
	struct dentry		*dentry;
	u32			query, result = 0;
	int			error;

	error = fh_verify(rqstp, fhp, 0, MAY_NOP);
	if (error < 0)
		goto out;

	export = fhp->fh_export;
	dentry = fhp->fh_dentry;

	if (S_ISREG(dentry->d_inode->i_mode)) {
		map = nfs3_regaccess;
	} else if (S_ISDIR(dentry->d_inode->i_mode)) {
		map = nfs3_diraccess;
	} else {
		map = nfs3_anyaccess;
	}

	query = *access;
	while (map->access) {
		if (map->access & query) {
			error = nfsd_permission(export, dentry, map->how);
			if (error == 0)
				result |= map->access;
			else if (error != -EPERM)
				goto out;
		}
		map++;
	}
	*access = result;

out:
	return error;
}
#endif



/*
 * Open an existing file or directory.
 * The access argument indicates the type of open (read/write/lock)
 * N.B. After this call fhp needs an fh_put
 */
int
nfsd_open(struct svc_rqst *rqstp, struct svc_fh *fhp, int type,
			int access, struct file *filp)
{
	struct dentry	*dentry;
	struct inode	*inode;
	int		err;

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

	if ((access & MAY_WRITE) && (err = get_write_access(inode)) != 0)
		goto out_nfserr;

	memset(filp, 0, sizeof(*filp));
	filp->f_op    = inode->i_op->default_file_ops;
	atomic_set(&filp->f_count, 1);
	filp->f_dentry = dentry;
	if (access & MAY_WRITE) {
		filp->f_flags = O_WRONLY;
		filp->f_mode  = FMODE_WRITE;
		DQUOT_INIT(inode);
	} else {
		filp->f_flags = O_RDONLY;
		filp->f_mode  = FMODE_READ;
	}

	err = 0;
	if (filp->f_op && filp->f_op->open) {
		err = filp->f_op->open(inode, filp);
		if (err) {
			if (access & MAY_WRITE)
				put_write_access(inode);

			/* I nearly added put_filp() call here, but this filp
			 * is really on callers stack frame. -DaveM
			 */
			atomic_dec(&filp->f_count);
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
	if (filp->f_mode & FMODE_WRITE) {
		put_write_access(inode);
	}
}

/*
 * Sync a file
 * As this calls fsync (not fdatasync) there is no need for a write_inode
 * after it.
 */
void
nfsd_sync(struct file *filp)
{
	dprintk("nfsd: sync file %s\n", filp->f_dentry->d_name.name);
	down(&filp->f_dentry->d_inode->i_sem);
	filp->f_op->fsync(filp, filp->f_dentry);
	up(&filp->f_dentry->d_inode->i_sem);
}

void
nfsd_sync_dir(struct dentry *dp)
{
	struct inode *inode = dp->d_inode;
	int (*fsync) (struct file *, struct dentry *);
	
	if (inode->i_op->default_file_ops
	    && (fsync = inode->i_op->default_file_ops->fsync)) {
		fsync(NULL, dp);
	}
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
nfsd_read(struct svc_rqst *rqstp, struct svc_fh *fhp, loff_t offset,
          char *buf, unsigned long *count)
{
	struct raparms	*ra;
	mm_segment_t	oldfs;
	int		err;
	struct file	file;

	err = nfsd_open(rqstp, fhp, S_IFREG, MAY_READ, &file);
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
#ifdef CONFIG_QUOTA
	uid_t			saved_euid;
#endif

	if (!cnt)
		goto out;
	err = nfsd_open(rqstp, fhp, S_IFREG, MAY_WRITE, &file);
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
	 *  -   The file system doesn't support fsync().
	 * When gathered writes have been configured for this volume,
	 * flushing the data to disk is handled separately below.
	 */
#ifdef CONFIG_NFSD_V3
	if (rqstp->rq_vers == 2)
		stable = EX_ISSYNC(exp);
	else if (file.f_op->fsync == 0)
	       stable = 1;
	if (stable && !EX_WGATHER(exp))
		file.f_flags |= O_SYNC;
#else
	if ((stable || (stable = EX_ISSYNC(exp))) && !EX_WGATHER(exp))
		file.f_flags |= O_SYNC;
#endif /* CONFIG_NFSD_V3 */

	file.f_pos = offset;		/* set write offset */

	/* Write the data. */
	oldfs = get_fs(); set_fs(KERNEL_DS);
#ifdef CONFIG_QUOTA
	/* This is for disk quota. */
	saved_euid = current->euid;
	current->euid = current->fsuid;
	err = file.f_op->write(&file, buf, cnt, &file.f_pos);
	current->euid = saved_euid;
#else
	err = file.f_op->write(&file, buf, cnt, &file.f_pos);
#endif
	set_fs(oldfs);

	/* clear setuid/setgid flag after write */
	if (err >= 0 && (inode->i_mode & (S_ISUID | S_ISGID))) {
		struct iattr	ia;
		kernel_cap_t	saved_cap = 0;

		ia.ia_valid = ATTR_MODE;
		ia.ia_mode  = inode->i_mode & ~(S_ISUID | S_ISGID);
		if (current->fsuid != 0) {
			saved_cap = current->cap_effective;
			cap_clear(current->cap_effective);
		}
		notify_change(dentry, &ia);
		if (current->fsuid != 0)
			current->cap_effective = saved_cap;
	}

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
		if (EX_WGATHER(exp) && (atomic_read(&inode->i_writecount) > 1
		 || (last_ino == inode->i_ino && last_dev == inode->i_dev))) {
#if 0
			interruptible_sleep_on_timeout(&inode->i_wait, 10 * HZ / 1000);
#else
			dprintk("nfsd: write defer %d\n", current->pid);
/* FIXME: Olaf commented this out [gam3] */
			schedule_timeout((HZ+99)/100);
			dprintk("nfsd: write resume %d\n", current->pid);
#endif
		}

		if (inode->i_state & I_DIRTY) {
			dprintk("nfsd: write sync %d\n", current->pid);
			nfsd_sync(&file);
		}
		wake_up(&inode->i_wait);
		last_ino = inode->i_ino;
		last_dev = inode->i_dev;
	}

	dprintk("nfsd: write complete err=%d\n", err);
	if (err >= 0)
		err = 0;
	else 
		err = nfserrno(-err);
out_close:
	nfsd_close(&file);
out:
	return err;
}


#ifdef CONFIG_NFSD_V3
/*
 * Commit all pendig writes to stable storage.
 * Strictly speaking, we could sync just indicated the file region here,
 * but there's currently no way we can ask the VFS to do so.
 *
 * We lock the file to make sure we return full WCC data to the client.
 */
int
nfsd_commit(struct svc_rqst *rqstp, struct svc_fh *fhp,
               off_t offset, unsigned long count)
{
	struct file	file;
	int		err;

	if ((err = nfsd_open(rqstp, fhp, S_IFREG, MAY_WRITE, &file)) != 0)
		return err;

	if (file.f_op && file.f_op->fsync) {
		nfsd_sync(&file);
	} else {
		err = nfserr_notsupp;
	}

	nfsd_close(&file);
	return err;
}
#endif /* CONFIG_NFSD_V3 */

/*
 * Create a file (regular, directory, device, fifo); UNIX sockets 
 * not yet implemented.
 * If the response fh has been verified, the parent directory should
 * already be locked. Note that the parent directory is left locked.
 *
 * N.B. Every call to nfsd_create needs an fh_put for _both_ fhp and resfhp
 */
int
nfsd_create(struct svc_rqst *rqstp, struct svc_fh *fhp,
		char *fname, int flen, struct iattr *iap,
		int type, dev_t rdev, struct svc_fh *resfhp)
{
	struct dentry	*dentry, *dchild;
	struct inode	*dirp;
	nfsd_dirop_t	opfunc = NULL;
	int		err;

	err = nfserr_perm;
	if (!flen)
		goto out;

	err = fh_verify(rqstp, fhp, S_IFDIR, MAY_CREATE);
	if (err)
		goto out;

	dentry = fhp->fh_dentry;
	dirp = dentry->d_inode;

	err = nfserr_notdir;
	if(!dirp->i_op || !dirp->i_op->lookup)
		goto out;
	/*
	 * Check whether the response file handle has been verified yet.
	 * If it has, the parent directory should already be locked.
	 */
	if (!resfhp->fh_dverified) {
		/* called from nfsd_proc_mkdir, or possibly nfsd3_proc_create */
		dchild = lookup_dentry(fname, dget(dentry), 0);
		err = PTR_ERR(dchild);
		if (IS_ERR(dchild))
			goto out_nfserr;
		fh_compose(resfhp, fhp->fh_export, dchild);
		/* Lock the parent and check for errors ... */
		err = fh_lock_parent(fhp, dchild);
		if (err)
			goto out;
	} else {
		/* called from nfsd_proc_create */
		dchild = resfhp->fh_dentry;
		if (!fhp->fh_locked) {
			/* not actually possible */
			printk(KERN_ERR
				"nfsd_create: parent %s/%s not locked!\n",
				dentry->d_parent->d_name.name,
				dentry->d_name.name);
			err = -EIO;
			goto out;
		}
	}
	/*
	 * Make sure the child dentry is still negative ...
	 */
	err = nfserr_exist;
	if (dchild->d_inode) {
		dprintk("nfsd_create: dentry %s/%s not negative!\n",
			dentry->d_name.name, dchild->d_name.name);
		goto out; 
	}

	/*
	 * Get the dir op function pointer.
	 */
	err = nfserr_perm;
	switch (type) {
	case S_IFREG:
		opfunc = (nfsd_dirop_t) dirp->i_op->create;
		break;
	case S_IFDIR:
		opfunc = (nfsd_dirop_t) dirp->i_op->mkdir;
		break;
	case S_IFCHR:
	case S_IFBLK:
		/* The client is _NOT_ required to do security enforcement */
		if(!capable(CAP_SYS_ADMIN))
		{
			err = -EPERM;
			goto out;
		}
	case S_IFIFO:
	case S_IFSOCK:
		opfunc = dirp->i_op->mknod;
		break;
	default:
	        printk("nfsd: bad file type %o in nfsd_create\n", type);
		err = nfserr_inval;
	}
	if (!opfunc)
		goto out;

	if (!(iap->ia_valid & ATTR_MODE))
		iap->ia_mode = 0;
	iap->ia_mode = (iap->ia_mode & S_IALLUGO) | type;

	/*
	 * Call the dir op function to create the object.
	 */
	DQUOT_INIT(dirp);
	err = opfunc(dirp, dchild, iap->ia_mode, rdev);
	if (err < 0)
		goto out_nfserr;

	if (EX_ISSYNC(fhp->fh_export)) {
		nfsd_sync_dir(dentry);
		write_inode_now(dchild->d_inode);
	}

	/*
	 * Update the file handle to get the new inode info.
	 */
	fh_update(resfhp);

	/* Set file attributes. Mode has already been set and
	 * setting uid/gid works only for root. Irix appears to
	 * send along the gid when it tries to implement setgid
	 * directories via NFS.
	 */
	err = 0;
	if ((iap->ia_valid &= ~(ATTR_UID|ATTR_GID|ATTR_MODE)) != 0)
		err = nfsd_setattr(rqstp, resfhp, iap);
out:
	return err;

out_nfserr:
	err = nfserrno(-err);
	goto out;
}

#ifdef CONFIG_NFSD_V3
/*
 * NFSv3 version of nfsd_create
 */
int
nfsd_create_v3(struct svc_rqst *rqstp, struct svc_fh *fhp,
		char *fname, int flen, struct iattr *iap,
		struct svc_fh *resfhp, int createmode, u32 *verifier)
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

	/* Get all the sanity checks out of the way before
	 * we lock the parent. */
	err = nfserr_notdir;
	if(!dirp->i_op || !dirp->i_op->lookup)
		goto out;
	err = nfserr_perm;
	if(!dirp->i_op->create)
		goto out;

	/*
	 * Compose the response file handle.
	 */
	dchild = lookup_dentry(fname, dget(dentry), 0);
	err = PTR_ERR(dchild);
	if(IS_ERR(dchild))
		goto out_nfserr;
	fh_compose(resfhp, fhp->fh_export, dchild);

	/*
	 * We must lock the directory before we check for the inode.
	 */
	err = fh_lock_parent(fhp, dchild);
	if (err)
	    goto out;

	if (dchild->d_inode) {
		err = 0;

		if (resfhp->fh_handle.fh_ino == 0)
		     /* inode might have been instantiated while we slept */
		    fh_update(resfhp);
		
		switch (createmode) {
		case NFS3_CREATE_UNCHECKED:
			if (! S_ISREG(dchild->d_inode->i_mode))
				err = nfserr_exist;
			else {
				iap->ia_valid &= ATTR_SIZE;
				goto set_attr;
			}
			break;
		case NFS3_CREATE_EXCLUSIVE:
			if (   dchild->d_inode->i_mtime == verifier[0]
			    && dchild->d_inode->i_atime == verifier[1]
			    && dchild->d_inode->i_mode == S_IFREG
			    && dchild->d_inode->i_size == 0 )
				break;
			 /* fallthru */
		case NFS3_CREATE_GUARDED:
			err = nfserr_exist;
		}
		goto out;
	}

	err = dirp->i_op->create(dirp, dchild, iap->ia_mode);
	if (err < 0)
		goto out_nfserr;

	if (EX_ISSYNC(fhp->fh_export)) {
		nfsd_sync_dir(dentry);
		/* setattr will sync the child (or not) */
	}

	/*
	 * Update the filehandle to get the new inode info.
	 */
	fh_update(resfhp);
	err = 0;

	if (createmode == NFS3_CREATE_EXCLUSIVE) {
		/* Cram the verifier into atime/mtime */
		iap->ia_valid = ATTR_MTIME|ATTR_ATIME|ATTR_MTIME_SET|ATTR_ATIME_SET;
		iap->ia_mtime = verifier[0];
		iap->ia_atime = verifier[1];
	}

	/* Set file attributes. Mode has already been set and
	 * setting uid/gid works only for root. Irix appears to
	 * send along the gid when it tries to implement setgid
	 * directories via NFS. Clear out all that cruft.
	 */
 set_attr:
	if ((iap->ia_valid &= ~(ATTR_UID|ATTR_GID|ATTR_MODE)) != 0)
 		err = nfsd_setattr(rqstp, resfhp, iap);

 out:
	fh_unlock(fhp);
 	return err;
 
 out_nfserr:
	err = nfserrno(-err);
	goto out;
}
#endif /* CONFIG_NFSD_V3 */

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
	kernel_cap_t	saved_cap = 0;

	err = fh_verify(rqstp, fhp, S_IFREG, MAY_WRITE | MAY_TRUNC);
	if (err)
		goto out;

	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;

	err = get_write_access(inode);
	if (err)
		goto out_nfserr;
	err = locks_verify_area(FLOCK_VERIFY_WRITE, inode, NULL,
				  size<inode->i_size ? size : inode->i_size,
				  abs(inode->i_size - size));
	if (err)
		goto out_nfserr;

	/* Things look sane, lock and do it. */
	fh_lock(fhp);
	DQUOT_INIT(inode);
	newattrs.ia_size = size;
	newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME;
	if (current->fsuid != 0) {
		saved_cap = current->cap_effective;
		cap_clear(current->cap_effective);
	}
	err = notify_change(dentry, &newattrs);
	if (!err) {
		vmtruncate(inode, size);
		if (inode->i_op && inode->i_op->truncate)
			inode->i_op->truncate(inode);
	}
	if (current->fsuid != 0)
		current->cap_effective = saved_cap;
	put_write_access(inode);
	if (EX_ISSYNC(fhp->fh_export))
		nfsd_sync_dir(dentry);
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
	/* N.B. Why does this call need a get_fs()??
	 * Remove the set_fs and watch the fireworks:-) --okir
	 */

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
				struct svc_fh *resfhp,
				struct iattr *iap)
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
	dirp = dentry->d_inode;
	if (!dirp->i_op	|| !dirp->i_op->symlink)
		goto out;

	dnew = lookup_dentry(fname, dget(dentry), 0);
	err = PTR_ERR(dnew);
	if (IS_ERR(dnew))
		goto out_nfserr;

	/*
	 * Lock the parent before checking for existence
	 */
	err = fh_lock_parent(fhp, dnew);
	if (err)
		goto out_compose;

	err = nfserr_exist;
	if (!dnew->d_inode) {
		DQUOT_INIT(dirp);
		err = dirp->i_op->symlink(dirp, dnew, path);
		if (!err) {
			if (EX_ISSYNC(fhp->fh_export))
				nfsd_sync_dir(dentry);
			if (iap) {
				iap->ia_valid &= ATTR_MODE /* ~(ATTR_MODE|ATTR_UID|ATTR_GID)*/;
				if (iap->ia_valid) {
					iap->ia_valid |= ATTR_CTIME;
					iap->ia_mode = (iap->ia_mode&S_IALLUGO)
						| S_IFLNK;
					err = notify_change(dnew, iap);
					if (!err && EX_ISSYNC(fhp->fh_export))
						write_inode_now(dentry->d_inode);
			       }
			}
		} else
			err = nfserrno(-err);
	}
	fh_unlock(fhp);

	/* Compose the fh so the dentry will be freed ... */
out_compose:
	fh_compose(resfhp, fhp->fh_export, dnew);
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
	err = fh_verify(rqstp, tfhp, -S_IFDIR, MAY_NOP);
	if (err)
		goto out;

	err = nfserr_perm;
	if (!len)
		goto out;

	ddir = ffhp->fh_dentry;
	dirp = ddir->d_inode;

	dnew = lookup_dentry(fname, dget(ddir), 0);
	err = PTR_ERR(dnew);
	if (IS_ERR(dnew))
		goto out_nfserr;
	/*
	 * Lock the parent before checking for existence
	 */
	err = fh_lock_parent(ffhp, dnew);
	if (err)
		goto out_dput;

	err = nfserr_exist;
	if (dnew->d_inode)
		goto out_unlock;

	dold = tfhp->fh_dentry;
	dest = dold->d_inode;

	err = (rqstp->rq_vers == 2) ? nfserr_acces : nfserr_xdev;
	if (dirp->i_dev != dest->i_dev)
		goto out_unlock;

	err = nfserr_perm;
	if (IS_IMMUTABLE(dest) || IS_APPEND(dest))
		goto out_unlock;
	if (!dirp->i_op || !dirp->i_op->link)
		goto out_unlock;

	DQUOT_INIT(dirp);
	err = dirp->i_op->link(dold, dirp, dnew);
	if (!err) {
		if (EX_ISSYNC(ffhp->fh_export)) {
			nfsd_sync_dir(ddir);
			write_inode_now(dest);
		}
	} else
		err = nfserrno(-err);

out_unlock:
	fh_unlock(ffhp);
out_dput:
	dput(dnew);
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

	err = (rqstp->rq_vers == 2) ? nfserr_acces : nfserr_xdev;
	if (fdir->i_dev != tdir->i_dev)
		goto out;

	/* N.B. We shouldn't need this ... dentry layer handles it */
	err = nfserr_perm;
	if (!flen || (fname[0] == '.' && 
	    (flen == 1 || (flen == 2 && fname[1] == '.'))) ||
	    !tlen || (tname[0] == '.' && 
	    (tlen == 1 || (tlen == 2 && tname[1] == '.'))))
		goto out;

	odentry = lookup_dentry(fname, dget(fdentry), 0);
	err = PTR_ERR(odentry);
	if (IS_ERR(odentry))
		goto out_nfserr;

	err = -ENOENT;
	if (!odentry->d_inode)
		goto out_dput_old;

	ndentry = lookup_dentry(tname, dget(tdentry), 0);
	err = PTR_ERR(ndentry);
	if (IS_ERR(ndentry))
		goto out_dput_old;

	/*
	 * Lock the parent directories.
	 */
	nfsd_double_down(&tdir->i_sem, &fdir->i_sem);

#ifdef CONFIG_NFSD_V3
	/* Fill in the pre-op attr for the wcc data for both 
	 * tdir and fdir
	 */ 
	fill_pre_wcc(ffhp);
	fill_pre_wcc(tfhp);
#endif /* CONFIG_NFSD_V3 */

	err = -ENOENT;
	/* GAM3 check for parent changes after locking. */
	if (nfsd_check_parent(fdentry, odentry) &&
	    nfsd_check_parent(tdentry, ndentry)) {

		err = vfs_rename(fdir, odentry, tdir, ndentry);
		if (!err && EX_ISSYNC(tfhp->fh_export)) {
			nfsd_sync_dir(tdentry);
			nfsd_sync_dir(fdentry);
		}
	} else
		dprintk("nfsd: Caught race in nfsd_rename");

#ifdef CONFIG_NFSD_V3
        /* Fill in the post-op attr for the wcc data for both 
         * tdir and fdir
         */
	fill_post_wcc(ffhp);
	fill_post_wcc(tfhp);
#endif /* CONFIG_NFSD_V3 */
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

	if (!rdentry->d_inode) {
		dput(rdentry);
		err = nfserr_noent;
		goto out;
	}


	if (type != S_IFDIR) {
		/* It's UNLINK */

		err = fh_lock_parent(fhp, rdentry);
		if (err)
			goto out;

		err = vfs_unlink(dirp, rdentry);

		fh_unlock(fhp);

		dput(rdentry);

	} else {
		/* It's RMDIR */
		/* See comments in fs/namei.c:do_rmdir */

		rdentry->d_count++;
		nfsd_double_down(&dirp->i_sem, &rdentry->d_inode->i_sem);

#ifdef CONFIG_NFSD_V3
		fill_pre_wcc(fhp);
#else
		fhp->fh_locked = 1;
#endif /* CONFIG_NFSD_V3 */

		err = -ENOENT;
		if (nfsd_check_parent(dentry, rdentry))
			err = vfs_rmdir(dirp, rdentry);

		rdentry->d_count--;
#ifdef CONFIG_NFSD_V3
		fill_post_wcc(fhp);
#else
		fhp->fh_locked = 0;
#endif /* CONFIG_NFSD_V3 */
		nfsd_double_up(&dirp->i_sem, &rdentry->d_inode->i_sem);

		dput(rdentry);
	}

	if (err)
		goto out_nfserr;
	if (EX_ISSYNC(fhp->fh_export)) {
		down(&dentry->d_inode->i_sem);
		nfsd_sync_dir(dentry);
		up(&dentry->d_inode->i_sem);
	}
out:
	return err;

out_nfserr:
	err = nfserrno(-err);
	goto out;
}

/*
 * Read entries from a directory.
 * The verifier is an NFSv3 thing we ignore for now.
 */
int
nfsd_readdir(struct svc_rqst *rqstp, struct svc_fh *fhp, loff_t offset, 
             encode_dent_fn func, u32 *buffer, int *countp, u32 *verf)
{
	struct inode	*inode;
	u32		*p;
	int		oldlen, eof, err;
	struct file	file;
	struct readdir_cd cd;

	err = 0;
	if (offset > ~(u32) 0)
		goto out;

	err = nfsd_open(rqstp, fhp, S_IFDIR, MAY_READ, &file);
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
	cd.buflen = *countp; /* count of words */
	cd.dirfh  = fhp;

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

	if (cd.offset) {
#ifdef CONFIG_NFSD_V3
		if (rqstp->rq_vers == 3)
			(void)enc64(cd.offset, file.f_pos);
		else
#endif /* CONFIG_NFSD_V3 */
			*cd.offset = htonl(file.f_pos);
	}

	p = cd.buffer;
	*p++ = 0;			/* no more entries */
	*p++ = htonl(eof);		/* end of directory */
	*countp = (caddr_t) p - (caddr_t) buffer;

	dprintk("nfsd: readdir result %d bytes, eof %d offset %d\n",
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
	kernel_cap_t	saved_cap = 0;

	if (acc == MAY_NOP)
		return 0;
#if 0
	dprintk("nfsd: permission 0x%x%s%s%s%s%s%s mode 0%o%s%s%s\n",
		acc,
		(acc & MAY_READ)?	" read"  : "",
		(acc & MAY_WRITE)?	" write" : "",
		(acc & MAY_EXEC)?	" exec"  : "",
		(acc & MAY_SATTR)?	" sattr" : "",
		(acc & MAY_TRUNC)?	" trunc" : "",
		(acc & MAY_LOCK)?	" lock"  : "",
		inode->i_mode,
		IS_IMMUTABLE(inode)?	" immut" : "",
		IS_APPEND(inode)?	" append" : "",
		IS_RDONLY(inode)?	" ro" : "");
	dprintk("      owner %d/%d user %d/%d\n",
		inode->i_uid, inode->i_gid, current->fsuid, current->fsgid);
#endif

	if (acc & (MAY_WRITE | MAY_SATTR | MAY_TRUNC)) {
		if (EX_RDONLY(exp) || IS_RDONLY(inode))
			return nfserr_rofs;
		if (/* (acc & MAY_WRITE) && */ IS_IMMUTABLE(inode))
			return nfserr_perm;
	}
	if ((acc & MAY_TRUNC) && IS_APPEND(inode))
		return nfserr_perm;

	if (acc & MAY_LOCK) {
		/* If we cannot rely on authentication in NLM requests,
		 * just allow locks, others require read permission
		 */
		if (exp->ex_flags & NFSEXP_NOAUTHNLM)
			return 0;
		else
			acc = MAY_READ;
	}
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

	if (current->fsuid != 0) {
		saved_cap = current->cap_effective;
		cap_clear(current->cap_effective);
	}

	err = permission(inode, acc & (MAY_READ|MAY_WRITE|MAY_EXEC));

	/* Allow read access to binaries even when mode 111 */
	if (err == -EACCES && S_ISREG(inode->i_mode) && acc == MAY_READ)
		err = permission(inode, MAY_EXEC);

	if (current->fsuid != 0)
		current->cap_effective = saved_cap;

	return err? nfserrno(-err) : 0;
}

void
nfsd_racache_shutdown(void)
{
	if (!raparm_cache)
		return;
	dprintk("nfsd: freeing %d readahead buffers.\n", FILECACHE_MAX);
	kfree(raparml);
	nfsd_nservers = 0;
	raparm_cache = raparml = NULL;
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
	raparml = kmalloc(sizeof(struct raparms) * FILECACHE_MAX, GFP_KERNEL);

	if (raparml != NULL) {
		dprintk("nfsd: allocating %d readahead buffers.\n",
			FILECACHE_MAX);
		memset(raparml, 0, sizeof(struct raparms) * FILECACHE_MAX);
		for (i = 0; i < FILECACHE_MAX - 1; i++) {
			raparml[i].p_next = raparml + i + 1;
		}
		raparm_cache = raparml;
	} else {
		printk(KERN_WARNING
		       "nfsd: Could not allocate memory read-ahead cache.\n");
		nfsd_nservers = 0;
	}
}
