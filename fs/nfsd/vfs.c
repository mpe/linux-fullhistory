/*
 * linux/fs/nfsd/vfs.c
 *
 * File operations used by nfsd. Some of these have been ripped from
 * other parts of the kernel because they weren't in ksyms.c, others
 * are partial duplicates with added or changed functionality.
 *
 * Note that several functions lock the inode upon which they want
 * to act, most notably those that create directory entries. The
 * unlock operation can take place either by calling fh_unlock within
 * the function directly, or at a later time in fh_put(). So if you
 * notice code paths that apparently fail to unlock the inode, don't
 * worry--they have been taken care of.
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

#define NFSDDBG_FACILITY		NFSDDBG_FILEOP

/* Symbol not exported */
static struct super_block *get_super(dev_t dev);

/* Open mode for nfsd_open */
#define OPEN_READ	0
#define OPEN_WRITE	1

/* Hack until we have a macro check for mandatory locks. */
#ifndef IS_ISMNDLK
#define IS_ISMNDLK(i)	(((i)->i_mode & (S_ISGID|S_ISVTX)) == S_ISGID)
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
	struct raparms *	p_next;
	unsigned int		p_count;
	dev_t			p_dev;
	ino_t			p_ino;
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
 * Check whether directory is a mount point
 */
static inline int
nfsd_iscovered(struct inode *inode)
{
	return inode->i_mount != NULL;
}

/*
 * Look up one component of a pathname.
 */
int
nfsd_lookup(struct svc_rqst *rqstp, struct svc_fh *fhp, const char *name,
					int len, struct svc_fh *resfh)
{
	struct svc_export	*exp;
	struct super_block	*sb;
	struct inode		*dirp, *inode;
	int			perm, err, dotdot = 0;

	dprintk("nfsd: nfsd_lookup(fh %x/%ld, %s)\n",
			SVCFH_DEV(fhp), SVCFH_INO(fhp), name);

	/* Obtain inode and export */
	if ((err = fh_lookup(rqstp, fhp, S_IFDIR, MAY_NOP)) != 0)
		return err;
	dirp = fhp->fh_inode;
	exp  = fhp->fh_export;

	/* check permissions before traversing mount-points */
	perm = nfsd_permission(exp, dirp, MAY_EXEC);

	dotdot = (len == 2 && name[0] == '.' && name[1] == '.');
	if (dotdot) {
		if (dirp == current->fs->root) {
			dirp->i_count++;
			*resfh = *fhp;
			return 0;
		}

		if (dirp->i_dev == exp->ex_dev && dirp->i_ino == exp->ex_ino) {
			dirp->i_count++;
			*resfh = *fhp;
			return 0;
		}
	} else if (len == 1 && name[0] == '.') {
		len = 0;
	} else if (fs_off_limits(dirp->i_sb)) {
		/* No lookups on NFS mounts and procfs */
		return nfserr_noent;
	} else if (nfsd_iscovered(dirp)) {
		/* broken NFS client */
		return nfserr_acces;
	}
	if (!dirp->i_op || !dirp->i_op->lookup)
		return nfserr_notdir;
 	if (perm != 0)
		return perm;
	if (!len) {
		dirp->i_count++;
		*resfh = *fhp;
		return 0;
	}

	dirp->i_count++;		/* lookup eats the dirp inode */
	err = dirp->i_op->lookup(dirp, name, len, &inode);

	if (err)
		return nfserrno(-err);

	/* Note that lookup() has already done a call to iget() so that
	 * the inode returned never refers to an inode covered by a mount.
	 * When this has happened, return the covered inode.
	 */
	if (!dotdot && (sb = inode->i_sb) && (inode == sb->s_mounted)) {
		iput(inode);
		inode = sb->s_covered;
		inode->i_count++;
	}

	fh_compose(resfh, exp, inode);
	return 0;
}

/*
 * Set various file attributes.
 */
int
nfsd_setattr(struct svc_rqst *rqstp, struct svc_fh *fhp, struct iattr *iap)
{
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
	if ((err = fh_lookup(rqstp, fhp, ftype, accmode)) != 0)
		return err;

	fh_lock(fhp);			/* lock inode */
	inode = fhp->fh_inode;

	/* The size case is special... */
	if ((iap->ia_valid & ATTR_SIZE) && S_ISREG(inode->i_mode)) {
		if (iap->ia_size < inode->i_size) {
			err = nfsd_permission(fhp->fh_export, inode, MAY_TRUNC);
			if (err != 0)
				return err;
		}
		if ((err = get_write_access(inode)) != 0)
			return nfserrno(-err);
		inode->i_size = iap->ia_size;
		if (inode->i_op && inode->i_op->truncate)
			inode->i_op->truncate(inode);
		inode->i_dirt = 1;
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
		err = nfsd_notify_change(inode, iap);
		if (err)
			return nfserrno(-err);
		if (EX_ISSYNC(fhp->fh_export))
			nfsd_write_inode(inode);
	}

	return 0;
}

/*
 * Open an existing file or directory.
 * The wflag argument indicates write access.
 */
int
nfsd_open(struct svc_rqst *rqstp, struct svc_fh *fhp, int type,
			int wflag, struct file *filp)
{
	struct inode	*inode;
	int		access, err;

	access = wflag? MAY_WRITE : MAY_READ;
	if ((err = fh_lookup(rqstp, fhp, type, access)) != 0)
		return err;
	inode = fhp->fh_inode;

	/* Disallow access to files with the append-only bit set or
	 * with mandatory locking enabled */
	if (IS_APPEND(inode) || IS_ISMNDLK(inode))
		return nfserr_perm;
	if (!inode->i_op || !inode->i_op->default_file_ops)
		return nfserr_perm;

	if (wflag && (err = get_write_access(inode)) != 0)
		return nfserrno(-err);

	memset(filp, 0, sizeof(*filp));
	filp->f_op    = inode->i_op->default_file_ops;
	filp->f_count = 1;
	filp->f_flags = wflag? O_WRONLY : O_RDONLY;
	filp->f_mode  = wflag? FMODE_WRITE : FMODE_READ;
	filp->f_inode = inode;

	if (filp->f_op->open) {
		err = filp->f_op->open(inode, filp);
		if (err) {
			if (wflag)
				put_write_access(inode);
			filp->f_count--;
			return nfserrno(-err);
		}
	}

	inode->i_count++;
	return 0;
}

/*
 * Close a file.
 */
void
nfsd_close(struct file *filp)
{
	struct inode	*inode;

	inode = filp->f_inode;
	if (!inode->i_count)
		printk(KERN_WARNING "nfsd: inode count == 0!\n");
	if (filp->f_op && filp->f_op->release)
		filp->f_op->release(inode, filp);
	if (filp->f_mode & FMODE_WRITE)
		put_write_access(inode);
	iput(inode);
}

/*
 * Sync a file
 */
void
nfsd_sync(struct inode *inode, struct file *filp)
{
	filp->f_op->fsync(inode, filp);
}

/*
 * Obtain the readahead parameters for the given file
 */
static inline struct raparms *
nfsd_get_raparms(dev_t dev, ino_t ino)
{
	struct raparms	*ra, **rap, **frap = NULL;

	for (rap = &raparm_cache; (ra = *rap); rap = &ra->p_next) {
		if (ra->p_dev != dev || ra->p_ino != ino) {
			if (ra->p_count == 0)
				frap = rap;
		} else
			goto found;
	}
	if (!frap)
		return NULL;
	rap = frap;
	ra = *frap;
	memset(ra, 0, sizeof(*ra));
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
 */
int
nfsd_read(struct svc_rqst *rqstp, struct svc_fh *fhp, loff_t offset, char *buf,
						unsigned long *count)
{
	struct raparms	*ra;
	struct inode	*inode;
	struct file	file;
	unsigned long	oldfs;
	int		err;

	if ((err = nfsd_open(rqstp, fhp, S_IFREG, OPEN_READ, &file)) != 0)
		return err;
	inode = file.f_inode;
	if (!file.f_op->read) {
		nfsd_close(&file);
		return nfserr_perm;
	}

	/* Get readahead parameters */
	if ((ra = nfsd_get_raparms(inode->i_dev, inode->i_ino)) != NULL) {
		file.f_reada = ra->p_reada;
		file.f_ramax = ra->p_ramax;
		file.f_raend = ra->p_raend;
		file.f_ralen = ra->p_ralen;
		file.f_rawin = ra->p_rawin;
	}
	file.f_pos = offset;

	oldfs = get_fs(); set_fs(KERNEL_DS);
	err = file.f_op->read(file.f_inode, &file, buf, *count);
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

	nfsd_close(&file);

	if (err < 0)
		return nfserrno(-err);
	*count = err;
	return 0;
}

/*
 * Write data to a file.
 * The stable flag requests synchronous writes.
 */
int
nfsd_write(struct svc_rqst *rqstp, struct svc_fh *fhp, loff_t offset,
				char *buf, unsigned long cnt, int stable)
{
	struct svc_export	*exp;
	struct file		file;
	struct inode		*inode;
	unsigned long		oldfs;
	int			err;

	if (!cnt)
		return 0;
	if ((err = nfsd_open(rqstp, fhp, S_IFREG, OPEN_WRITE, &file)) != 0)
		return err;
	if (!file.f_op->write) {
		nfsd_close(&file);
		return nfserr_perm;
	}

	inode = fhp->fh_inode;
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
	err = file.f_op->write(inode, &file, buf, cnt);
	set_fs(oldfs);

	/* clear setuid/setgid flag after write */
	if (err >= 0 && (inode->i_mode & (S_ISUID | S_ISGID))) {
		struct iattr	ia;

		ia.ia_valid = ATTR_MODE;
		ia.ia_mode  = inode->i_mode & ~(S_ISUID | S_ISGID);
		nfsd_notify_change(inode, &ia);
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

		if (inode->i_dirt) {
			dprintk("nfsd: write sync %d\n", current->pid);
			nfsd_sync(inode, &file);
			nfsd_write_inode(inode);
		}
		wake_up(&inode->i_wait);
		last_ino = inode->i_ino;
		last_dev = inode->i_dev;
	}

	nfsd_close(&file);

	dprintk("nfsd: write complete\n");
	return (err < 0)? nfserrno(-err) : 0;
}

/*
 * Create a file (regular, directory, device, fifo).
 * UNIX sockets not yet implemented.
 */
int
nfsd_create(struct svc_rqst *rqstp, struct svc_fh *fhp,
		char *fname, int flen, struct iattr *iap,
		int type, dev_t rdev, struct svc_fh *resfhp)
{
	struct inode	*dirp, *inode = NULL;
	int		err;

	if (!flen)
		return nfserr_perm;

	if (!(iap->ia_valid & ATTR_MODE))
		iap->ia_mode = 0;

	if ((err = fh_lookup(rqstp, fhp, S_IFDIR, MAY_CREATE)) != 0)
		return err;

	fh_lock(fhp);			/* lock directory */
	dirp = fhp->fh_inode;
	dirp->i_count++;		/* dirop eats the inode */

	switch (type) {
	case S_IFREG:
		if (!dirp->i_op || !dirp->i_op->create)
			return nfserr_perm;
		err = dirp->i_op->create(dirp, fname, flen,
					 iap->ia_mode, &inode);
		break;
	case S_IFDIR:
		if (!dirp->i_op || !dirp->i_op->mkdir)
			return nfserr_perm;
		err = dirp->i_op->mkdir(dirp, fname, flen, iap->ia_mode);
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
		if (!dirp->i_op || !dirp->i_op->mknod)
			return nfserr_perm;
		err = dirp->i_op->mknod(dirp, fname, flen, iap->ia_mode, rdev);
		break;
	default:
		iput(dirp);
		err = -EACCES;
	}

	fh_unlock(fhp);

	if (err < 0)
		return nfserrno(-err);

	/*
	 * If the VFS call doesn't return the inode, look it up now.
	 */
	if (inode == NULL) {
		dirp->i_count++;
		err = dirp->i_op->lookup(dirp, fname, flen, &inode);
		if (err < 0)
			return -nfserrno(err);	/* Huh?! */
	}

	if (EX_ISSYNC(fhp->fh_export))
		nfsd_write_inode(dirp);

	/* Assemble the file handle for the newly created file */
	fh_compose(resfhp, fhp->fh_export, inode);

	/* Set file attributes. Mode has already been set and
	 * setting uid/gid works only for root. Irix appears to
	 * send along the gid when it tries to implement setgid
	 * directories via NFS.
	 */
	if ((iap->ia_valid &= (ATTR_UID|ATTR_GID|ATTR_MODE)) != 0) {
		if ((err = nfsd_setattr(rqstp, resfhp, iap)) != 0) {
			fh_put(resfhp);
			return err;
		}
	}

	return 0;
}

/*
 * Truncate a file.
 * The calling routines must make sure to update the ctime
 * field and call notify_change.
 */
int
nfsd_truncate(struct svc_rqst *rqstp, struct svc_fh *fhp, unsigned long size)
{
	struct inode	*inode;
	int		err;

	if ((err = fh_lookup(rqstp, fhp, S_IFREG, MAY_WRITE|MAY_TRUNC)) != 0)
		return err;

	fh_lock(fhp);			/* lock inode if not yet locked */
	inode = fhp->fh_inode;

	if ((err = get_write_access(inode)) != 0)
		return nfserrno(-err);
	inode->i_size = size;
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	inode->i_dirt = 1;
	put_write_access(inode);

	fh_unlock(fhp);

	return 0;
}

/*
 * Read a symlink. On entry, *lenp must contain the maximum path length that
 * fits into the buffer. On return, it contains the true length.
 */
int
nfsd_readlink(struct svc_rqst *rqstp, struct svc_fh *fhp, char *buf, int *lenp)
{
	struct inode	*inode;
	unsigned long	oldfs;
	int		err;

	if ((err = fh_lookup(rqstp, fhp, S_IFLNK, MAY_READ)) != 0)
		return err;
	inode = fhp->fh_inode;

	if (!inode->i_op || !inode->i_op->readlink)
		return nfserr_io;

	inode->i_count++;
	oldfs = get_fs(); set_fs(KERNEL_DS);
	err = inode->i_op->readlink(inode, buf, *lenp);
	set_fs(oldfs);

	if (err < 0)
		return nfserrno(-err);
	*lenp = err;

	return 0;
}

/*
 * Create a symlink and look up its inode
 */
int
nfsd_symlink(struct svc_rqst *rqstp, struct svc_fh *fhp,
				char *fname, int flen,
				char *path,  int plen,
				struct svc_fh *resfhp)
{
	struct inode	*dirp, *inode;
	int		err;

	if (!flen || !plen)
		return nfserr_noent;

	if ((err = fh_lookup(rqstp, fhp, S_IFDIR, MAY_CREATE)) != 0)
		return err;

	dirp = fhp->fh_inode;
	if (nfsd_iscovered(dirp))
		return nfserr_perm;
	if (!dirp->i_op || !dirp->i_op->symlink)
		return nfserr_perm;

	fh_lock(fhp);			/* lock inode */
	dirp->i_count++;
	err = dirp->i_op->symlink(dirp, fname, flen, path);
	fh_unlock(fhp);			/* unlock inode */

	if (err)
		return nfserrno(-err);

	if (EX_ISSYNC(fhp->fh_export))
		nfsd_write_inode(dirp);

	/*
	 * Okay, now look up the inode of the new symlink.
	 */
	dirp->i_count++;		/* lookup eats the dirp inode */
	err = dirp->i_op->lookup(dirp, fname, flen, &inode);
	if (err)
		return nfserrno(-err);

	fh_compose(resfhp, fhp->fh_export, inode);
	return 0;
}

/*
 * Create a hardlink
 */
int
nfsd_link(struct svc_rqst *rqstp, struct svc_fh *ffhp,
				char *fname, int len, struct svc_fh *tfhp)
{
	struct inode	*dirp, *dest;
	int		err;

	if ((err = fh_lookup(rqstp, ffhp, S_IFDIR, MAY_CREATE) != 0) ||
	    (err = fh_lookup(rqstp, tfhp, S_IFREG, MAY_NOP)) != 0)
		return err;
	dirp = ffhp->fh_inode;
	dest = tfhp->fh_inode;

	if (!len)
		return nfserr_perm;
	if (nfsd_iscovered(dirp))
		return nfserr_acces;
	if (dirp->i_dev != dest->i_dev)
		return nfserr_acces;	/* FIXME: nxdev for NFSv3 */
	if (IS_IMMUTABLE(dest) /* || IS_APPEND(dest) */ )
		return nfserr_perm;
	if (!dirp->i_op || !dirp->i_op->link)
		return nfserr_perm;

	fh_lock(ffhp);			/* lock directory inode */
	dirp->i_count++;
	err = dirp->i_op->link(dest, dirp, fname, len);
	fh_unlock(ffhp);		/* unlock inode */

	if (!err && EX_ISSYNC(ffhp->fh_export)) {
		nfsd_write_inode(dirp);
		nfsd_write_inode(dest);
	}

	return err? nfserrno(-err) : 0;
}

/*
 * Rename a file
 */
int
nfsd_rename(struct svc_rqst *rqstp, struct svc_fh *ffhp, char *fname, int flen,
			    struct svc_fh *tfhp, char *tname, int tlen)
{
	struct inode	*fdir, *tdir;
	int		err;

	if ((err = fh_lookup(rqstp, ffhp, S_IFDIR, MAY_REMOVE) != 0)
	 || (err = fh_lookup(rqstp, tfhp, S_IFDIR, MAY_CREATE)) != 0)
		return err;
	fdir = ffhp->fh_inode;
	tdir = tfhp->fh_inode;

	if (!flen || (fname[0] == '.' && 
	    (flen == 1 || (flen == 2 && fname[1] == '.'))) ||
	    !tlen || (tname[0] == '.' && 
	    (tlen == 1 || (tlen == 2 && tname[1] == '.'))))
		return nfserr_perm;

	if (fdir->i_dev != tdir->i_dev)
		return nfserr_acces;	/* nfserr_nxdev */
	if (!fdir->i_op || !fdir->i_op->rename)
		return nfserr_perm;

	fh_lock(tfhp);			/* lock destination directory */
	tdir->i_count++;
	fdir->i_count++;
	err = fdir->i_op->rename(fdir, fname, flen, tdir, tname, tlen, 0);
	fh_unlock(tfhp);		/* unlock inode */

	if (!err && EX_ISSYNC(tfhp->fh_export)) {
		nfsd_write_inode(fdir);
		nfsd_write_inode(tdir);
	}

	return err? nfserrno(-err) : 0;
}

/*
 * Unlink a file or directory
 */
int
nfsd_unlink(struct svc_rqst *rqstp, struct svc_fh *fhp, int type,
				char *fname, int flen)
{
	struct inode	*dirp;
	int		err;

	if (!flen || isdotent(fname, flen))
		return nfserr_acces;

	if ((err = fh_lookup(rqstp, fhp, S_IFDIR, MAY_REMOVE)) != 0)
		return err;

	fh_lock(fhp);			/* lock inode */
	dirp = fhp->fh_inode;

	if (type == S_IFDIR) {
		if (!dirp->i_op || !dirp->i_op->rmdir)
			return nfserr_notdir;
		dirp->i_count++;
		err = dirp->i_op->rmdir(dirp, fname, flen);
	} else {	/* other than S_IFDIR */
		if (!dirp->i_op || !dirp->i_op->unlink)
			return nfserr_perm;
		dirp->i_count++;
		err = dirp->i_op->unlink(dirp, fname, flen);
	}

	fh_unlock(fhp);			/* unlock inode */
	if (!err && EX_ISSYNC(fhp->fh_export))
		nfsd_write_inode(dirp);

	return err? nfserrno(-err) : 0;
}

/*
 * Read entries from a directory.
 */
int
nfsd_readdir(struct svc_rqst *rqstp, struct svc_fh *fhp, loff_t offset, 
			encode_dent_fn func, u32 *buffer, int *countp)
{
	struct readdir_cd cd;
	struct file	file;
	u32		*p;
	int		oldlen, eof, err;

	if (offset > ~(u32) 0)
		return 0;

	if ((err = nfsd_open(rqstp, fhp, S_IFDIR, OPEN_READ, &file)) != 0)
		return err;

	if (!file.f_op->readdir) {
		nfsd_close(&file);
		return nfserr_notdir;
	}
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
	do {
		oldlen = cd.buflen;

		/*
		dprintk("nfsd: f_op->readdir(%x/%ld @ %d) buflen = %d (%d)\n",
			file.f_inode->i_dev, file.f_inode->i_ino,
			(int) file.f_pos, (int) oldlen, (int) cd.buflen);
		 */
		err = file.f_op->readdir(file.f_inode, &file,
					 &cd, (filldir_t) func);

		if (err < 0) {
			nfsd_close(&file);
			return nfserrno(-err);
		}
		if (oldlen == cd.buflen)
			break;
	} while (oldlen != cd.buflen && !cd.eob);

	/* If we didn't fill the buffer completely, we're at EOF */
	eof = !cd.eob;

	/* Hewlett Packard ignores the eof flag on READDIR. Some
	 * fs-specific readdir implementations seem to reset f_pos to 0
	 * at EOF however, causing an endless loop. */
	if (cd.offset && !eof)
		*cd.offset = htonl(file.f_pos);

	/* Close the file */
	nfsd_close(&file);

	p = cd.buffer;
	*p++ = 0;			/* no more entries */
	*p++ = htonl(eof);		/* end of directory */
	*countp = (caddr_t) p - (caddr_t) buffer;

	dprintk("nfsd: readdir result %d bytes, eof %d offset %ld\n",
				*countp, eof,
				cd.offset? ntohl(*cd.offset) : -1);
	return 0;
}

/*
 * Get file system stats
 */
int
nfsd_statfs(struct svc_rqst *rqstp, struct svc_fh *fhp, struct statfs *stat)
{
	struct inode		*inode;
	struct super_block	*sb;
	unsigned long		oldfs;
	int			err;

	if ((err = fh_lookup(rqstp, fhp, 0, MAY_NOP)) != 0)
		return err;
	inode = fhp->fh_inode;

	if (!(sb = inode->i_sb) || !sb->s_op->statfs)
		return nfserr_io;

	oldfs = get_fs();
	set_fs (KERNEL_DS);
	sb->s_op->statfs(sb, stat, sizeof(*stat));
	set_fs (oldfs);

	return 0;
}

/*
 * Check for a user's access permissions to this inode.
 */
int
nfsd_permission(struct svc_export *exp, struct inode *inode, int acc)
{
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
		if (S_ISDIR(inode->i_mode) && nfsd_iscovered(inode))
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
 * Look up the inode for a given FH.
 */
struct inode *
nfsd_iget(dev_t dev, ino_t ino)
{
	struct super_block	*sb;

	if (!(sb = get_super(dev)) || fs_off_limits(sb))
		return NULL;
	return __iget(sb, ino, 0);
}

/*
 * Write the inode if dirty (copy of fs/inode.c:write_inode)
 */
void
nfsd_write_inode(struct inode *inode)
{
	struct super_block	*sb = inode->i_sb;

	if (!inode->i_dirt)
		return;
	while (inode->i_lock) {
		sleep_on(&inode->i_wait);
		if (!inode->i_dirt)
			return;
	}
	if (!sb || !sb->s_op || !sb->s_op->write_inode) {
		inode->i_dirt = 0;
		return;
	}
	inode->i_lock = 1;
	sb->s_op->write_inode(inode);
	inode->i_lock = 0;
	wake_up(&inode->i_wait);
}

/*
 * Look up the root inode of the parent fs.
 * We have to go through iget in order to allow for wait_on_inode.
 */
int
nfsd_parentdev(dev_t* devp)
{
	struct super_block	*sb;

	if (!(sb = get_super(*devp)) || !sb->s_covered)
		return 0;
	if (*devp == sb->s_covered->i_dev)
		return 0;
	*devp = sb->s_covered->i_dev;
	return 1;
}

/* Duplicated here from fs/super.c because it's not exported */
static struct super_block *
get_super(dev_t dev)
{
	struct super_block *s;

	if (!dev)
		return NULL;
	s = 0 + super_blocks;
	while (s < NR_SUPER + super_blocks)
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0 + super_blocks;
		} else
			s++;
	return NULL;
}

/*
 * This is a copy from fs/inode.c because it wasn't exported.
 */
int
nfsd_notify_change(struct inode *inode, struct iattr *attr)
{
	int retval;

	if (inode->i_sb && inode->i_sb->s_op &&
	    inode->i_sb->s_op->notify_change)
		return inode->i_sb->s_op->notify_change(inode, attr);

	if ((retval = inode_change_ok(inode, attr)) != 0)
		return retval;

	inode_setattr(inode, attr);
	return 0;
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
