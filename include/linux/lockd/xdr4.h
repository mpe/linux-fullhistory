/*
 * linux/include/linux/lockd/xdr.h
 *
 * XDR types for the NLM protocol
 *
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LOCKD_XDR4_H
#define LOCKD_XDR4_H

#include <linux/fs.h>
#include <linux/nfs.h>
#include <linux/sunrpc/xdr.h>
#include <linux/lockd/xdr.h>

/* error codes new to NLMv4 */
extern u32	nlm4_deadlock, nlm4_rofs, nlm4_stale_fh, nlm4_fbig, nlm4_failed;


int	nlm4svc_decode_testargs(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlm4svc_encode_testres(struct svc_rqst *, u32 *, struct nlm_res *);
int	nlm4svc_decode_lockargs(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlm4svc_decode_cancargs(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlm4svc_decode_unlockargs(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlm4svc_encode_res(struct svc_rqst *, u32 *, struct nlm_res *);
int	nlm4svc_decode_res(struct svc_rqst *, u32 *, struct nlm_res *);
int	nlm4svc_encode_void(struct svc_rqst *, u32 *, void *);
int	nlm4svc_decode_void(struct svc_rqst *, u32 *, void *);
int	nlm4svc_decode_shareargs(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlm4svc_encode_shareres(struct svc_rqst *, u32 *, struct nlm_res *);
int	nlm4svc_decode_notify(struct svc_rqst *, u32 *, struct nlm_args *);
int	nlm4svc_decode_reboot(struct svc_rqst *, u32 *, struct nlm_reboot *);
/*
int	nlmclt_encode_testargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_lockargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_cancargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_unlockargs(struct rpc_rqst *, u32 *, struct nlm_args *);
 */

#endif /* LOCKD_XDR4_H */
