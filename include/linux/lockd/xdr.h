/*
 * linux/include/linux/lockd/xdr.h
 *
 * XDR types for the NLM protocol
 *
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LOCKD_XDR_H
#define LOCKD_XDR_H

#include <linux/fs.h>
#include <linux/nfs.h>
#include <linux/sunrpc/xdr.h>

extern u32	nlm_granted, nlm_lck_denied, nlm_lck_denied_nolocks,
		nlm_lck_blocked, nlm_lck_denied_grace_period;

/* Lock info passed via NLM */
struct nlm_lock {
	char *			caller;
	struct nfs_fh		fh;
	struct xdr_netobj	oh;
	struct file_lock	fl;
};

/*
 * Generic lockd arguments for all but sm_notify
 */
struct nlm_args {
	u32			cookie;
	struct nlm_lock		lock;
	u32			block;
	u32			reclaim;
	u32			state;
	u32			monitor;
	u32			fsm_access;
	u32			fsm_mode;
};

/*
 * Generic lockd result
 */
struct nlm_res {
	u32			cookie;
	u32			status;
	struct nlm_lock		lock;
};

/*
 * statd callback when client has rebooted
 */
struct nlm_reboot {
	char *		mon;
	int		len;
	u32		state;
	u32		addr;
};

/*
 * Contents of statd callback when monitored host rebooted
 */
#define NLMSVC_XDRSIZE		sizeof(struct nlm_args)

void	nlmxdr_init(void);
int	nlmsvc_decode_testargs(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlmsvc_encode_testres(struct svc_rqst *, u32 *, struct nlm_res *);
int	nlmsvc_decode_lockargs(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlmsvc_decode_cancargs(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlmsvc_decode_unlockargs(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlmsvc_encode_res(struct svc_rqst *, u32 *, struct nlm_res *);
int	nlmsvc_decode_res(struct svc_rqst *, u32 *, struct nlm_res *);
int	nlmsvc_encode_void(struct svc_rqst *, u32 *, void *);
int	nlmsvc_decode_void(struct svc_rqst *, u32 *, void *);
int	nlmsvc_decode_shareargs(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlmsvc_encode_shareres(struct svc_rqst *, u32 *, struct nlm_res *);
int	nlmsvc_decode_notify(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlmsvc_decode_reboot(struct svc_rqst *, u32 *, struct nlm_reboot *);
/*
int	nlmclt_encode_testargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_lockargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_cancargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_unlockargs(struct rpc_rqst *, u32 *, struct nlm_args *);
 */

#endif /* LOCKD_XDR_H */
