/*
 * linux/fs/nfsd/nfsfh.c
 *
 * NFS server filehandle treatment.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/stat.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>

#define NFSDDBG_FACILITY		NFSDDBG_FH

/*
 * Get the inode version number
 */
static inline int
nfsd_iversion(struct inode *inode)
{
	if (inode->i_sb->s_magic == EXT2_SUPER_MAGIC)
		return inode->u.ext2_i.i_version;
	return 0;
}

/*
 * Get the inode given a file handle.
 */
u32
fh_lookup(struct svc_rqst *rqstp, struct svc_fh *fhp, int type, int access)
{
	struct svc_export *exp;
	struct inode	*inode;
	struct knfs_fh	*fh = &fhp->fh_handle;

	/* Already checked */
	if (fhp->fh_inode)
		return 0;

	dprintk("nfsd: fh_lookup(exp %x/%ld fh %x/%ld)\n",
			fh->fh_xdev, fh->fh_xino, fh->fh_dev, fh->fh_ino);

	/* Make sure that clients don't cheat */
	if (fh->fh_dev != fh->fh_xdev) {
		printk(KERN_NOTICE "nfsd: fh with bad dev fields "
				"(%x != %x) from %08lx:%d\n",
				fh->fh_dev, fh->fh_xdev,
				ntohl(rqstp->rq_addr.sin_addr.s_addr),
				ntohs(rqstp->rq_addr.sin_port));
		return nfserr_perm;
	}

	/* Look up the export entry */
	exp = exp_get(rqstp->rq_client, fh->fh_xdev, fh->fh_xino);
	if (!exp) {
		/* nfsdstats.fhstale++; */
		return nfserr_stale; /* export entry revoked */
	}

	/* Check if the request originated from a secure port. */
	if (!rqstp->rq_secure && EX_SECURE(exp)) {
		printk(KERN_WARNING
			"nfsd: request from insecure port (%08lx:%d)!\n",
				ntohl(rqstp->rq_addr.sin_addr.s_addr),
				ntohs(rqstp->rq_addr.sin_port));
		return nfserr_perm;
	}

	/* Set user creds if we haven't done so already */
	nfsd_setuser(rqstp, exp);

	/* Get the inode */
	if (!(inode = nfsd_iget(fh->fh_dev, fh->fh_ino))
	 || !inode->i_nlink || fh->fh_version != nfsd_iversion(inode)) {
		if (inode)
			iput(inode);
		/* nfsdstats.fhstale++; */
		return nfserr_stale;	/* unlinked in the meanwhile */
	}

	/* This is basically what wait_on_inode does */
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	fhp->fh_inode  = inode;
	fhp->fh_export = exp;

	/* Type check. The correct error return for type mismatches
	 * does not seem to be generally agreed upon. SunOS seems to
	 * use EISDIR if file isn't S_IFREG; a comment in the NFSv3
	 * spec says this is incorrect (implementation notes for the
	 * write call).
	 */
	if (type > 0 && (inode->i_mode & S_IFMT) != type)
		return (type == S_IFDIR)? nfserr_notdir : nfserr_isdir;
	if (type < 0 && (inode->i_mode & S_IFMT) == -type)
		return (type == -S_IFDIR)? nfserr_notdir : nfserr_isdir;

	/* Finally, check access permissions */
	return nfsd_permission(fhp->fh_export, inode, access);
}

/*
 * Compose file handle for NFS reply.
 */
void
fh_compose(struct svc_fh *fhp, struct svc_export *exp, struct inode *inode)
{
	dprintk("nfsd: fh_compose(exp %x/%ld fh %x/%ld)\n",
			exp->ex_dev, exp->ex_ino, inode->i_dev, inode->i_ino);

	fh_init(fhp);			/* initialize empty fh */
	fhp->fh_inode = inode;
	fhp->fh_export = exp;
	fhp->fh_handle.fh_dev = inode->i_dev;
	fhp->fh_handle.fh_ino = inode->i_ino;
	fhp->fh_handle.fh_xdev = exp->ex_dev;
	fhp->fh_handle.fh_xino = exp->ex_ino;
	fhp->fh_handle.fh_version = nfsd_iversion(inode);
}
