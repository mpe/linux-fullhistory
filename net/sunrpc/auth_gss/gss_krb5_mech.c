/*
 *  linux/net/sunrpc/gss_krb5_mech.c
 *
 *  Copyright (c) 2001 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@umich.edu>
 *  J. Bruce Fields <bfields@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sunrpc/auth.h>
#include <linux/in.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/sunrpc/xdr.h>
#include <linux/crypto.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

static const void *
simple_get_bytes(const void *p, const void *end, void *res, int len)
{
	const void *q = (const void *)((const char *)p + len);
	if (unlikely(q > end || q < p))
		return ERR_PTR(-EFAULT);
	memcpy(res, p, len);
	return q;
}

static const void *
simple_get_netobj(const void *p, const void *end, struct xdr_netobj *res)
{
	const void *q;
	unsigned int len;

	p = simple_get_bytes(p, end, &len, sizeof(len));
	if (IS_ERR(p))
		return p;
	q = (const void *)((const char *)p + len);
	if (unlikely(q > end || q < p))
		return ERR_PTR(-EFAULT);
	res->data = kmalloc(len, GFP_KERNEL);
	if (unlikely(res->data == NULL))
		return ERR_PTR(-ENOMEM);
	memcpy(res->data, p, len);
	res->len = len;
	return q;
}

static inline const void *
get_key(const void *p, const void *end, struct crypto_tfm **res)
{
	struct xdr_netobj	key;
	int			alg, alg_mode;
	char			*alg_name;

	p = simple_get_bytes(p, end, &alg, sizeof(alg));
	if (IS_ERR(p))
		goto out_err;
	p = simple_get_netobj(p, end, &key);
	if (IS_ERR(p))
		goto out_err;

	switch (alg) {
		case ENCTYPE_DES_CBC_RAW:
			alg_name = "des";
			alg_mode = CRYPTO_TFM_MODE_CBC;
			break;
		default:
			dprintk("RPC:      get_key: unsupported algorithm %d\n", alg);
			goto out_err_free_key;
	}
	if (!(*res = crypto_alloc_tfm(alg_name, alg_mode)))
		goto out_err_free_key;
	if (crypto_cipher_setkey(*res, key.data, key.len))
		goto out_err_free_tfm;

	kfree(key.data);
	return p;

out_err_free_tfm:
	crypto_free_tfm(*res);
out_err_free_key:
	kfree(key.data);
	p = ERR_PTR(-EINVAL);
out_err:
	return p;
}

static int
gss_import_sec_context_kerberos(const void *p,
				size_t len,
				struct gss_ctx *ctx_id)
{
	const void *end = (const void *)((const char *)p + len);
	struct	krb5_ctx *ctx;

	if (!(ctx = kmalloc(sizeof(*ctx), GFP_KERNEL)))
		goto out_err;
	memset(ctx, 0, sizeof(*ctx));

	p = simple_get_bytes(p, end, &ctx->initiate, sizeof(ctx->initiate));
	if (IS_ERR(p))
		goto out_err_free_ctx;
	p = simple_get_bytes(p, end, &ctx->seed_init, sizeof(ctx->seed_init));
	if (IS_ERR(p))
		goto out_err_free_ctx;
	p = simple_get_bytes(p, end, ctx->seed, sizeof(ctx->seed));
	if (IS_ERR(p))
		goto out_err_free_ctx;
	p = simple_get_bytes(p, end, &ctx->signalg, sizeof(ctx->signalg));
	if (IS_ERR(p))
		goto out_err_free_ctx;
	p = simple_get_bytes(p, end, &ctx->sealalg, sizeof(ctx->sealalg));
	if (IS_ERR(p))
		goto out_err_free_ctx;
	p = simple_get_bytes(p, end, &ctx->endtime, sizeof(ctx->endtime));
	if (IS_ERR(p))
		goto out_err_free_ctx;
	p = simple_get_bytes(p, end, &ctx->seq_send, sizeof(ctx->seq_send));
	if (IS_ERR(p))
		goto out_err_free_ctx;
	p = simple_get_netobj(p, end, &ctx->mech_used);
	if (IS_ERR(p))
		goto out_err_free_ctx;
	p = get_key(p, end, &ctx->enc);
	if (IS_ERR(p))
		goto out_err_free_mech;
	p = get_key(p, end, &ctx->seq);
	if (IS_ERR(p))
		goto out_err_free_key1;
	if (p != end) {
		p = ERR_PTR(-EFAULT);
		goto out_err_free_key2;
	}

	ctx_id->internal_ctx_id = ctx;
	dprintk("RPC:      Succesfully imported new context.\n");
	return 0;

out_err_free_key2:
	crypto_free_tfm(ctx->seq);
out_err_free_key1:
	crypto_free_tfm(ctx->enc);
out_err_free_mech:
	kfree(ctx->mech_used.data);
out_err_free_ctx:
	kfree(ctx);
out_err:
	return PTR_ERR(p);
}

static void
gss_delete_sec_context_kerberos(void *internal_ctx) {
	struct krb5_ctx *kctx = internal_ctx;

	if (kctx->seq)
		crypto_free_tfm(kctx->seq);
	if (kctx->enc)
		crypto_free_tfm(kctx->enc);
	if (kctx->mech_used.data)
		kfree(kctx->mech_used.data);
	kfree(kctx);
}

static u32
gss_verify_mic_kerberos(struct gss_ctx		*ctx,
			struct xdr_buf		*message,
			struct xdr_netobj	*mic_token,
			u32			*qstate) {
	u32 maj_stat = 0;
	int qop_state;
	struct krb5_ctx *kctx = ctx->internal_ctx_id;

	maj_stat = krb5_read_token(kctx, mic_token, message, &qop_state,
				   KG_TOK_MIC_MSG);
	if (!maj_stat && qop_state)
	    *qstate = qop_state;

	dprintk("RPC:      gss_verify_mic_kerberos returning %d\n", maj_stat);
	return maj_stat;
}

static u32
gss_get_mic_kerberos(struct gss_ctx	*ctx,
		     u32		qop,
		     struct xdr_buf 	*message,
		     struct xdr_netobj	*mic_token) {
	u32 err = 0;
	struct krb5_ctx *kctx = ctx->internal_ctx_id;

	err = krb5_make_token(kctx, qop, message, mic_token, KG_TOK_MIC_MSG);

	dprintk("RPC:      gss_get_mic_kerberos returning %d\n",err);

	return err;
}

static struct gss_api_ops gss_kerberos_ops = {
	.gss_import_sec_context	= gss_import_sec_context_kerberos,
	.gss_get_mic		= gss_get_mic_kerberos,
	.gss_verify_mic		= gss_verify_mic_kerberos,
	.gss_delete_sec_context	= gss_delete_sec_context_kerberos,
};

static struct pf_desc gss_kerberos_pfs[] = {
	[0] = {
		.pseudoflavor = RPC_AUTH_GSS_KRB5,
		.service = RPC_GSS_SVC_NONE,
		.name = "krb5",
	},
	[1] = {
		.pseudoflavor = RPC_AUTH_GSS_KRB5I,
		.service = RPC_GSS_SVC_INTEGRITY,
		.name = "krb5i",
	},
};

static struct gss_api_mech gss_kerberos_mech = {
	.gm_name	= "krb5",
	.gm_owner	= THIS_MODULE,
	.gm_ops		= &gss_kerberos_ops,
	.gm_pf_num	= ARRAY_SIZE(gss_kerberos_pfs),
	.gm_pfs		= gss_kerberos_pfs,
};

static int __init init_kerberos_module(void)
{
	int status;

	status = gss_mech_register(&gss_kerberos_mech);
	if (status)
		printk("Failed to register kerberos gss mechanism!\n");
	return status;
}

static void __exit cleanup_kerberos_module(void)
{
	gss_mech_unregister(&gss_kerberos_mech);
}

MODULE_LICENSE("GPL");
module_init(init_kerberos_module);
module_exit(cleanup_kerberos_module);
