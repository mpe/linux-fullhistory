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
 * Perform sanity checks on the dentry in a client's file handle.
 */
u32
fh_verify(struct svc_rqst *rqstp, struct svc_fh *fhp, int type, int access)
{
	struct svc_export *exp;
	struct dentry	*dentry;
	struct inode	*inode;
	struct knfs_fh	*fh = &fhp->fh_handle;

	if(fhp->fh_dverified)
		return 0;

	dprintk("nfsd: fh_lookup(exp %x/%d fh %p)\n",
			fh->fh_xdev, fh->fh_xino, fh->fh_dentry);

	/* Look up the export entry. */
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

	/* Set user creds if we haven't done so already. */
	nfsd_setuser(rqstp, exp);

	dentry = fh->fh_dentry;

	if(!d_validate(dentry, fh->fh_dparent, fh->fh_dhash, fh->fh_dlen) ||
	   !(inode = dentry->d_inode) ||
	   !inode->i_nlink) {
		/* Currently we cannot tell the difference between
		 * a bogus pointer and a true unlink between fh
		 * uses.  But who cares about accurate error reporting
		 * to buggy/malicious clients... -DaveM
		 */

		/* nfsdstats.fhstale++; */
		return nfserr_stale;
	}

	dget(dentry);
	fhp->fh_dverified = 1;
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

	/* Finally, check access permissions. */
	return nfsd_permission(fhp->fh_export, dentry, access);
}

/*
 * Compose file handle for NFS reply.
 */
void
fh_compose(struct svc_fh *fhp, struct svc_export *exp, struct dentry *dentry)
{
	dprintk("nfsd: fh_compose(exp %x/%d dentry %p)\n",
			exp->ex_dev, exp->ex_ino, dentry);

	fh_init(fhp);			/* initialize empty fh */
	fhp->fh_handle.fh_dentry = dentry;
	fhp->fh_handle.fh_dparent = dentry->d_parent;
	fhp->fh_handle.fh_dhash = dentry->d_name.hash;
	fhp->fh_handle.fh_dlen = dentry->d_name.len;
	fhp->fh_handle.fh_xdev = exp->ex_dev;
	fhp->fh_handle.fh_xino = exp->ex_ino;
	fhp->fh_export = exp;

	/* We stuck it there, we know it's good. */
	fhp->fh_dverified = 1;
}
