/*
 * linux/fs/nfs/mount_clnt.c
 *
 * MOUNT client to support NFSroot.
 *
 * Copyright (C) 1997, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/uio.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs_fs.h>

#ifdef RPC_DEBUG
# define NFSDBG_FACILITY	NFSDBG_ROOT
#endif

/*
#define MOUNT_PROGRAM		100005
#define MOUNT_VERSION		1
#define MOUNT_MNT		1
#define MOUNT_UMNT		3
 */

static struct rpc_clnt *	mnt_create(char *, struct sockaddr_in *);
extern struct rpc_program	mnt_program;

struct mnt_fhstatus {
	unsigned int		status;
	struct nfs_fh *		fh;
};

/*
 * Obtain an NFS file handle for the given host and path
 */
int
nfs_mount(struct sockaddr_in *addr, char *path, struct nfs_fh *fh)
{
	struct rpc_clnt		*mnt_clnt;
	struct mnt_fhstatus	result = { 0, fh };
	char			hostname[32];
	int			status;

	dprintk("NFS:      nfs_mount(%08x:%s)\n",
			(unsigned)ntohl(addr->sin_addr.s_addr), path);

	strcpy(hostname, in_ntoa(addr->sin_addr.s_addr));
	if (!(mnt_clnt = mnt_create(hostname, addr)))
		return -EACCES;

	status = rpc_call(mnt_clnt, NFS_MNTPROC_MNT, path, &result, 0);
	return status < 0? status : (result.status? -EACCES : 0);
}

static struct rpc_clnt *
mnt_create(char *hostname, struct sockaddr_in *srvaddr)
{
	struct rpc_xprt	*xprt;
	struct rpc_clnt	*clnt;

	if (!(xprt = xprt_create_proto(IPPROTO_UDP, srvaddr, NULL)))
		return NULL;

	clnt = rpc_create_client(xprt, hostname,
				&mnt_program, NFS_MNT_VERSION,
				RPC_AUTH_NULL);
	if (!clnt) {
		xprt_destroy(xprt);
	} else {
		clnt->cl_softrtry = 1;
		clnt->cl_chatty   = 1;
		clnt->cl_oneshot  = 1;
		clnt->cl_intr = 1;
	}
	return clnt;
}

/*
 * XDR encode/decode functions for MOUNT
 */
static int
xdr_error(struct rpc_rqst *req, u32 *p, void *dummy)
{
	return -EIO;
}

static int
xdr_encode_dirpath(struct rpc_rqst *req, u32 *p, const char *path)
{
	p = xdr_encode_string(p, path);

	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int
xdr_decode_fhstatus(struct rpc_rqst *req, u32 *p, struct mnt_fhstatus *res)
{
	if ((res->status = ntohl(*p++)) == 0)
		memcpy(res->fh, p, sizeof(*res->fh));
	return 0;
}

#define MNT_dirpath_sz		(1 + 256)
#define MNT_fhstatus_sz		(1 + 8)

static struct rpc_procinfo	mnt_procedures[2] = {
	{ "mnt_null",
		(kxdrproc_t) xdr_error,	
		(kxdrproc_t) xdr_error,	0, 0 },
	{ "mnt_mount",
		(kxdrproc_t) xdr_encode_dirpath,	
		(kxdrproc_t) xdr_decode_fhstatus,
		MNT_dirpath_sz, MNT_fhstatus_sz },
};

static struct rpc_version	mnt_version1 = {
	1, 2, mnt_procedures
};

static struct rpc_version *	mnt_version[] = {
	NULL,
	&mnt_version1,
};

static struct rpc_stat		mnt_stats;

struct rpc_program	mnt_program = {
	"mount",
	NFS_MNT_PROGRAM,
	sizeof(mnt_version)/sizeof(mnt_version[0]),
	mnt_version,
	&mnt_stats,
};
