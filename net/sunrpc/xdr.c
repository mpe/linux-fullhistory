/*
 * linux/net/sunrpc/xdr.c
 *
 * Generic XDR support.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/msg_prot.h>

u32	rpc_success, rpc_prog_unavail, rpc_prog_mismatch, rpc_proc_unavail,
	rpc_garbage_args, rpc_system_err;
u32	rpc_auth_ok, rpc_autherr_badcred, rpc_autherr_rejectedcred,
	rpc_autherr_badverf, rpc_autherr_rejectedverf, rpc_autherr_tooweak;
u32	xdr_zero, xdr_one, xdr_two;

void
xdr_init(void)
{
	static int	inited = 0;

	if (inited)
		return;

	xdr_zero = htonl(0);
	xdr_one = htonl(1);
	xdr_two = htonl(2);

	rpc_success = htonl(RPC_SUCCESS);
	rpc_prog_unavail = htonl(RPC_PROG_UNAVAIL);
	rpc_prog_mismatch = htonl(RPC_PROG_MISMATCH);
	rpc_proc_unavail = htonl(RPC_PROC_UNAVAIL);
	rpc_garbage_args = htonl(RPC_GARBAGE_ARGS);
	rpc_system_err = htonl(RPC_SYSTEM_ERR);

	rpc_auth_ok = htonl(RPC_AUTH_OK);
	rpc_autherr_badcred = htonl(RPC_AUTH_BADCRED);
	rpc_autherr_rejectedcred = htonl(RPC_AUTH_REJECTEDCRED);
	rpc_autherr_badverf = htonl(RPC_AUTH_BADVERF);
	rpc_autherr_rejectedverf = htonl(RPC_AUTH_REJECTEDVERF);
	rpc_autherr_tooweak = htonl(RPC_AUTH_TOOWEAK);

	inited = 1;
}

/*
 * XDR functions for basic NFS types
 */
u32 *
xdr_encode_netobj(u32 *p, const struct xdr_netobj *obj)
{
	unsigned int	quadlen = XDR_QUADLEN(obj->len);

	*p++ = htonl(obj->len);
	p[quadlen-1] = 0;	/* zero trailing bytes */
	memcpy(p, obj->data, obj->len);
	return p + XDR_QUADLEN(obj->len);
}

u32 *
xdr_decode_netobj_fixed(u32 *p, void *obj, unsigned int len)
{
	if (ntohl(*p++) != len)
		return NULL;
	memcpy(obj, p, len);
	return p + XDR_QUADLEN(len);
}

u32 *
xdr_decode_netobj(u32 *p, struct xdr_netobj *obj)
{
	unsigned int	len;

	if ((len = ntohl(*p++)) > XDR_MAX_NETOBJ)
		return NULL;
	obj->len  = len;
	obj->data = (u8 *) p;
	return p + XDR_QUADLEN(len);
}

u32 *
xdr_encode_string(u32 *p, const char *string)
{
	int len = strlen(string);
	int quadlen = XDR_QUADLEN(len);

	p[quadlen] = 0;
	*p++ = htonl(len);
	memcpy(p, string, len);
	return p + quadlen;
}

u32 *
xdr_decode_string(u32 *p, char **sp, int *lenp, int maxlen)
{
	unsigned int	len;
	char		*string;

	if ((len = ntohl(*p++)) > maxlen)
		return NULL;
	if (lenp)
		*lenp = len;
	if ((len % 4) != 0) {
		string = (char *) p;
	} else {
		string = (char *) (p - 1);
		memmove(string, p, len);
	}
	string[len] = '\0';
	*sp = string;
	return p + XDR_QUADLEN(len);
}

