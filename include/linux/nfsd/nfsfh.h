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

#ifndef _LINUX_NFSD_FH_H
#define _LINUX_NFSD_FH_H

#include <asm/types.h>
#ifdef __KERNEL__
# include <linux/types.h>
# include <linux/string.h>
# include <linux/fs.h>
#endif
#include <linux/nfsd/const.h>
#include <linux/nfsd/debug.h>

/*
 * This is the new "dentry style" Linux NFSv2 file handle.
 *
 * The xino and xdev fields are currently used to transport the
 * ino/dev of the exported inode.
 */
struct nfs_fhbase {
	struct dentry *	fb_dentry;	/* dentry cookie */
	__u32		fb_ino;		/* our inode number */
	__u32		fb_dirino;	/* dir inode number */
	__u32		fb_dev;		/* our device */
	__u32		fb_xdev;
	__u32		fb_xino;
	__u32		fb_generation;
};

#define NFS_FH_PADDING		(NFS_FHSIZE - sizeof(struct nfs_fhbase))
struct knfs_fh {
	struct nfs_fhbase	fh_base;
	__u8			fh_cookie[NFS_FH_PADDING];
};

#define fh_dcookie		fh_base.fb_dentry
#define fh_ino			fh_base.fb_ino
#define fh_dirino		fh_base.fb_dirino
#define fh_dev			fh_base.fb_dev
#define fh_xdev			fh_base.fb_xdev
#define fh_xino			fh_base.fb_xino
#define fh_generation		fh_base.fb_generation

#ifdef __KERNEL__

/*
 * Conversion macros for the filehandle fields.
 */
extern inline __u32 kdev_t_to_u32(kdev_t dev)
{
	return (__u32) dev;
}

extern inline kdev_t u32_to_kdev_t(__u32 udev)
{
	return (kdev_t) udev;
}

extern inline __u32 ino_t_to_u32(ino_t ino)
{
	return (__u32) ino;
}

extern inline ino_t u32_to_ino_t(__u32 uino)
{
	return (ino_t) uino;
}

/*
 * This is the internal representation of an NFS handle used in knfsd.
 * pre_mtime/post_version will be used to support wcc_attr's in NFSv3.
 */
typedef struct svc_fh {
	struct knfs_fh		fh_handle;	/* FH data */
	struct dentry *		fh_dentry;	/* validated dentry */
	struct svc_export *	fh_export;	/* export pointer */
#ifdef CONFIG_NFSD_V3
	unsigned char		fh_post_saved;	/* post-op attrs saved */
	unsigned char		fh_pre_saved;	/* pre-op attrs saved */
#endif /* CONFIG_NFSD_V3 */
	unsigned char		fh_locked;	/* inode locked by us */
	unsigned char		fh_dverified;	/* dentry has been checked */

#ifdef CONFIG_NFSD_V3
	/* Pre-op attributes saved during fh_lock */
	__u64			fh_pre_size;	/* size before operation */
	time_t			fh_pre_mtime;	/* mtime before oper */
	time_t			fh_pre_ctime;	/* ctime before oper */

	/* Post-op attributes saved in fh_unlock */
	umode_t			fh_post_mode;	/* i_mode */
	nlink_t			fh_post_nlink;	/* i_nlink */
	uid_t			fh_post_uid;	/* i_uid */
	gid_t			fh_post_gid;	/* i_gid */
	__u64			fh_post_size;	/* i_size */
	unsigned long		fh_post_blocks; /* i_blocks */
	unsigned long		fh_post_blksize;/* i_blksize */
	kdev_t			fh_post_rdev;	/* i_rdev */
	time_t			fh_post_atime;	/* i_atime */
	time_t			fh_post_mtime;	/* i_mtime */
	time_t			fh_post_ctime;	/* i_ctime */
#endif /* CONFIG_NFSD_V3 */

} svc_fh;

/*
 * Shorthand for dprintk()'s
 */
#define SVCFH_DENTRY(f)		((f)->fh_dentry)
#define SVCFH_INO(f)		((f)->fh_handle.fh_ino)
#define SVCFH_DEV(f)		((f)->fh_handle.fh_dev)

/*
 * Function prototypes
 */
u32	fh_verify(struct svc_rqst *, struct svc_fh *, int, int);
void	fh_compose(struct svc_fh *, struct svc_export *, struct dentry *);
void	fh_update(struct svc_fh *);
void	fh_put(struct svc_fh *);
void	nfsd_fh_flush(kdev_t);
void	nfsd_fh_init(void);
void	nfsd_fh_free(void);

static __inline__ struct svc_fh *
fh_copy(struct svc_fh *dst, struct svc_fh *src)
{
	if (src->fh_dverified || src->fh_locked) {
		struct dentry *dentry = src->fh_dentry;
		printk(KERN_ERR "fh_copy: copying %s/%s, already verified!\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
	}
			
	*dst = *src;
	return dst;
}

static __inline__ struct svc_fh *
fh_init(struct svc_fh *fhp)
{
	memset(fhp, 0, sizeof(*fhp));
	return fhp;
}

#ifdef CONFIG_NFSD_V3
/*
 * Fill in the pre_op attr for the wcc data
 */
static inline void
fill_pre_wcc(struct svc_fh *fhp)
{
        struct inode    *inode;

        inode = fhp->fh_dentry->d_inode;
        if (!fhp->fh_pre_saved) {
                fhp->fh_pre_mtime = inode->i_mtime;
                        fhp->fh_pre_ctime = inode->i_ctime;
                        fhp->fh_pre_size  = inode->i_size;
                        fhp->fh_pre_saved = 1;
        }
        fhp->fh_locked = 1;
}

/*
 * Fill in the post_op attr for the wcc data
 */
static inline void
fill_post_wcc(struct svc_fh *fhp)
{
        struct inode    *inode = fhp->fh_dentry->d_inode;

        if (fhp->fh_post_saved)
                printk("nfsd: inode locked twice during operation.\n");

        fhp->fh_post_mode       = inode->i_mode;
        fhp->fh_post_nlink      = inode->i_nlink;
        fhp->fh_post_uid        = inode->i_uid;
        fhp->fh_post_gid        = inode->i_gid;
        fhp->fh_post_size       = inode->i_size;
        fhp->fh_post_blksize    = inode->i_blksize;
        fhp->fh_post_blocks     = inode->i_blocks;
        fhp->fh_post_rdev       = inode->i_rdev;
        fhp->fh_post_atime      = inode->i_atime;
        fhp->fh_post_mtime      = inode->i_mtime;
        fhp->fh_post_ctime      = inode->i_ctime;
        fhp->fh_post_saved      = 1;
        fhp->fh_locked          = 0;
}
#endif /* CONFIG_NFSD_V3 */


/*
 * Lock a file handle/inode
 */
static inline void
fh_lock(struct svc_fh *fhp)
{
	struct dentry	*dentry = fhp->fh_dentry;
	struct inode	*inode;

	dfprintk(FILEOP, "nfsd: fh_lock(%x/%ld) locked = %d\n",
			SVCFH_DEV(fhp), (long)SVCFH_INO(fhp), fhp->fh_locked);

	if (!fhp->fh_dverified) {
		printk(KERN_ERR "fh_lock: fh not verified!\n");
		return;
	}
	if (fhp->fh_locked) {
		printk(KERN_WARNING "fh_lock: %s/%s already locked!\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
		return;
	}

	inode = dentry->d_inode;
	down(&inode->i_sem);
#ifdef CONFIG_NFSD_V3
	fill_pre_wcc(fhp);
#else
	fhp->fh_locked = 1;
#endif /* CONFIG_NFSD_V3 */
}

/*
 * Unlock a file handle/inode
 */
static inline void
fh_unlock(struct svc_fh *fhp)
{
	if (!fhp->fh_dverified)
		printk(KERN_ERR "fh_unlock: fh not verified!\n");

	if (fhp->fh_locked) {
#ifdef CONFIG_NFSD_V3
		fill_post_wcc(fhp);
		up(&fhp->fh_dentry->d_inode->i_sem);
#else
		struct dentry *dentry = fhp->fh_dentry;
		struct inode *inode = dentry->d_inode;

		fhp->fh_locked = 0;
		up(&inode->i_sem);
#endif /* CONFIG_NFSD_V3 */
	}
}
#endif /* __KERNEL__ */


#endif /* _LINUX_NFSD_FH_H */
