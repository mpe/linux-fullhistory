/*
 * include/linux/nfsd/nfsfh.h
 *
 * This file describes the layout of the file handles as passed
 * over the wire.
 *
 * Earlier versions of knfsd used to sign file handles using keyed MD5
 * or SHA. I've removed this code, because it doesn't give you more
 * security than blocking external access to port 2049 on your firewall.
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef NFSD_FH_H
#define NFSD_FH_H

#include <linux/types.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/nfsd/const.h>
#include <linux/nfsd/debug.h>

/*
 * This is the new "dentry style" Linux NFSv2 file handle.
 *
 * The xino and xdev fields are currently used to transport the
 * ino/dev of the exported inode.
 */
struct nfs_fhbase {
	struct dentry		*fb_dentry;
	struct dentry		*fb_dparent;
	unsigned int		fb_dhash;
	unsigned int		fb_dlen;
	ino_t			fb_xino;
	dev_t			fb_xdev;
};

#define NFS_FH_PADDING		(NFS_FHSIZE - sizeof(struct nfs_fhbase))
struct knfs_fh {
	struct nfs_fhbase	fh_base;
	__u8			fh_cookie[NFS_FH_PADDING];
};

#define fh_dentry		fh_base.fb_dentry
#define fh_dparent		fh_base.fb_dparent
#define fh_dhash		fh_base.fb_dhash
#define fh_dlen			fh_base.fb_dlen
#define fh_xino			fh_base.fb_xino
#define fh_xdev			fh_base.fb_xdev


#ifdef __KERNEL__

/*
 * This is the internal representation of an NFS handle used in knfsd.
 * pre_mtime/post_version will be used to support wcc_attr's in NFSv3.
 */
typedef struct svc_fh {
	struct knfs_fh		fh_handle;	/* FH data */
	struct svc_export *	fh_export;	/* export pointer */
	size_t			fh_pre_size;	/* size before operation */
	time_t			fh_pre_mtime;	/* mtime before oper */
	time_t			fh_pre_ctime;	/* ctime before oper */
	unsigned long		fh_post_version;/* inode version after oper */
	unsigned char		fh_locked;	/* inode locked by us */
	unsigned char		fh_dverified;	/* dentry has been checked */
} svc_fh;

/*
 * Shorthand for dprintk()'s
 */
#define SVCFH_DENTRY(f)		((f)->fh_handle.fh_dentry)

/*
 * Function prototypes
 */
u32             fh_verify(struct svc_rqst *, struct svc_fh *, int, int);
void            fh_compose(struct svc_fh *, struct svc_export *, struct dentry *);

static __inline__ struct svc_fh *
fh_copy(struct svc_fh *dst, struct svc_fh *src)
{
	*dst = *src;
	return dst;
}

static __inline__ struct svc_fh *
fh_init(struct svc_fh *fhp)
{
	memset(fhp, 0, sizeof(*fhp));
	return fhp;
}

/*
 * Lock a file handle/inode
 */
static inline void
fh_lock(struct svc_fh *fhp)
{
	struct inode	*inode = fhp->fh_handle.fh_dentry->d_inode;

	/*
	dfprintk(FILEOP, "nfsd: fh_lock(%x/%ld) locked = %d\n",
			SVCFH_DEV(fhp), SVCFH_INO(fhp), fhp->fh_locked);
	 */
	if (!fhp->fh_locked) {
		down(&inode->i_sem);
		if (!fhp->fh_pre_mtime)
			fhp->fh_pre_mtime = inode->i_mtime;
		fhp->fh_locked = 1;
	}
}

/*
 * Unlock a file handle/inode
 */
static inline void
fh_unlock(struct svc_fh *fhp)
{
	struct inode	*inode = fhp->fh_handle.fh_dentry->d_inode;

	if (fhp->fh_locked) {
		if (!fhp->fh_post_version)
			fhp->fh_post_version = inode->i_version;
		fhp->fh_locked = 0;
		up(&inode->i_sem);
	}
}

/*
 * Release an inode
 */
#ifndef NFSD_DEBUG
static inline void
fh_put(struct svc_fh *fhp)
{
	if (fhp->fh_dverified) {
		fh_unlock(fhp);
		dput(fhp->fh_handle.fh_dentry);
		fhp->fh_dverified = 0;
	}
}
#else
#define fh_put(fhp)	__fh_put(fhp, __FILE__, __LINE__)

static inline void
__fh_put(struct svc_fh *fhp, char *file, int line)
{
	struct dentry	*dentry;

	if (!fhp->fh_dverified)
		return;

	dentry = fhp->fh_handle.fh_dentry;
	if (!dentry->d_count) {
		printk("nfsd: trying to free free dentry in %s:%d\n"
		       "      file %s/%s\n",
		       file, line,
		       dentry->d_parent->d_name.name, dentry->d_name.name);
	} else {
		fh_unlock(fhp);
		dput(dentry);
		fhp->fh_dverified = 0;
	}
}
#endif



#endif /* __KERNEL__ */

#endif /* NFSD_FH_H */
