/*
 * linux/fs/nfsd/lockd.c
 *
 * This file contains all the stubs needed when communicating with lockd.
 * This level of indirection is necessary so we can run nfsd+lockd without
 * requiring the nfs client to be compiled in/loaded, and vice versa.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/lockd/bind.h>

#define NFSDDBG_FACILITY		NFSDDBG_LOCKD

/*
 * Note: we hold the dentry use count while the file is open.
 */
static u32
nlm_fopen(struct svc_rqst *rqstp, struct knfs_fh *f, struct file *filp)
{
	u32		nfserr;
	struct svc_fh	fh;

	/* must initialize before using! */
	fh_init(&fh);
	fh.fh_handle = *f;
	fh.fh_export = NULL;

	nfserr = nfsd_open(rqstp, &fh, S_IFREG, 0, filp);
	if (!nfserr)
		dget(filp->f_dentry);
	fh_put(&fh);
	return nfserr;
}

static void
nlm_fclose(struct file *filp)
{
	nfsd_close(filp);
	dput(filp->f_dentry);
}

struct nlmsvc_binding		nfsd_nlm_ops = {
	exp_readlock,		/* lock export table for reading */
	exp_unlock,		/* unlock export table */
	exp_getclient,		/* look up NFS client */
	nlm_fopen,		/* open file for locking */
	nlm_fclose,		/* close file */
	exp_nlmdetach,		/* lockd shutdown notification */
};

/*
 * When removing an NFS client entry, notify lockd that it is gone.
 * FIXME: We should do the same when unexporting an NFS volume.
 */
void
nfsd_lockd_unexport(struct svc_client *clnt)
{
	nlmsvc_invalidate_client(clnt);
}

void
nfsd_lockd_init(void)
{
	dprintk("nfsd: initializing lockd\n");
	nlmsvc_ops = &nfsd_nlm_ops;
}

void
nfsd_lockd_shutdown(void)
{
	nlmsvc_ops = NULL;
}
