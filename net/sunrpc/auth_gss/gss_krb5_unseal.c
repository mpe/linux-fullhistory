/*
 *  linux/net/sunrpc/gss_krb5_unseal.c
 *
 *  Adapted from MIT Kerberos 5-1.2.1 lib/gssapi/krb5/k5unseal.c
 *
 *  Copyright (c) 2000 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson   <andros@umich.edu>
 */

/*
 * Copyright 1993 by OpenVision Technologies, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/crypto.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif


/* message_buffer is an input if MIC and an output if WRAP. */

u32
krb5_read_token(struct krb5_ctx *ctx,
		struct xdr_netobj *read_token,
		struct xdr_netobj *message_buffer,
		int *qop_state, int toktype)
{
	s32			code;
	int			tmsglen = 0;
	int			conflen = 0;
	int			signalg;
	int			sealalg;
	struct xdr_netobj	token = {.len = 0, .data = NULL};
	s32			checksum_type;
	struct xdr_netobj	cksum;
	struct xdr_netobj	md5cksum = {.len = 0, .data = NULL};
	struct xdr_netobj	plaind;
	char			*data_ptr;
	s32			now;
	unsigned char		*plain = NULL;
	int			cksum_len = 0;
	int			plainlen = 0;
	int			direction;
	s32			seqnum;
	unsigned char		*ptr = (unsigned char *)read_token->data;
	int			bodysize;
	u32			ret = GSS_S_DEFECTIVE_TOKEN;

	dprintk("RPC: krb5_read_token\n");

	if (g_verify_token_header((struct xdr_netobj *) &ctx->mech_used,
					&bodysize, &ptr, toktype,
					read_token->len))
		goto out;

	if (toktype == KG_TOK_WRAP_MSG) {
		message_buffer->len = 0;
		message_buffer->data = NULL;
	}

	/* get the sign and seal algorithms */

	signalg = ptr[0] + (ptr[1] << 8);
	sealalg = ptr[2] + (ptr[3] << 8);

	/* Sanity checks */

	if ((ptr[4] != 0xff) || (ptr[5] != 0xff))
		goto out;

	if (((toktype != KG_TOK_WRAP_MSG) && (sealalg != 0xffff)) ||
	    ((toktype == KG_TOK_WRAP_MSG) && (sealalg == 0xffff)))
		goto out;

	/* in the current spec, there is only one valid seal algorithm per
	   key type, so a simple comparison is ok */

	if ((toktype == KG_TOK_WRAP_MSG) && !(sealalg == ctx->sealalg))
		goto out;

	/* there are several mappings of seal algorithms to sign algorithms,
	   but few enough that we can try them all. */

	if ((ctx->sealalg == SEAL_ALG_NONE && signalg > 1) ||
	    (ctx->sealalg == SEAL_ALG_1 && signalg != SGN_ALG_3) ||
	    (ctx->sealalg == SEAL_ALG_DES3KD &&
	     signalg != SGN_ALG_HMAC_SHA1_DES3_KD))
		goto out;

	/* starting with a single alg */
	switch (signalg) {
	case SGN_ALG_DES_MAC_MD5:
		cksum_len = 8;
		break;
	default:
		goto out;
	}

	if (toktype == KG_TOK_WRAP_MSG)
		tmsglen = bodysize - (14 + cksum_len);

	/* get the token parameters */

	/* decode the message, if WRAP */

	if (toktype == KG_TOK_WRAP_MSG) {
		dprintk("RPC: krb5_read_token KG_TOK_WRAP_MSG\n");

		plain = kmalloc(tmsglen, GFP_KERNEL);
		ret = GSS_S_FAILURE;
		if (plain ==  NULL)
			goto out;

		code = krb5_decrypt(ctx->enc, NULL,
				   ptr + 14 + cksum_len, plain,
				   tmsglen);
		if (code)
			goto out;

		plainlen = tmsglen;

		conflen = crypto_tfm_alg_blocksize(ctx->enc);
		token.len = tmsglen - conflen - plain[tmsglen - 1];

		if (token.len) {
			token.data = kmalloc(token.len, GFP_KERNEL);
			if (token.data == NULL)
				goto out;
			memcpy(token.data, plain + conflen, token.len);
		}

	} else if (toktype == KG_TOK_MIC_MSG) {
		dprintk("RPC: krb5_read_token KG_TOK_MIC_MSG\n");
		token = *message_buffer;
		plain = token.data;
		plainlen = token.len;
	} else {
		token.len = 0;
		token.data = NULL;
		plain = token.data;
		plainlen = token.len;
	}

	dprintk("RPC krb5_read_token: token.len %d plainlen %d\n", token.len,
		plainlen);

	/* compute the checksum of the message */

	/* initialize the the cksum */
	switch (signalg) {
	case SGN_ALG_DES_MAC_MD5:
		checksum_type = CKSUMTYPE_RSA_MD5;
		break;
	default:
		ret = GSS_S_DEFECTIVE_TOKEN;
		goto out;
	}

	switch (signalg) {
	case SGN_ALG_DES_MAC_MD5:
		dprintk("RPC krb5_read_token SGN_ALG_DES_MAC_MD5\n");
		/* compute the checksum of the message.
		 * 8 = bytes of token body to be checksummed according to spec 
		 */

		data_ptr = kmalloc(8 + plainlen, GFP_KERNEL);
		ret = GSS_S_FAILURE;
		if (!data_ptr)
			goto out;

		memcpy(data_ptr, ptr - 2, 8);
		memcpy(data_ptr + 8, plain, plainlen);

		plaind.len = 8 + plainlen;
		plaind.data = data_ptr;

		code = krb5_make_checksum(checksum_type,
					    &plaind, &md5cksum);

		kfree(data_ptr);

		if (code)
			goto out;

		code = krb5_encrypt(ctx->seq, NULL, md5cksum.data,
					  md5cksum.data, 16);
		if (code)
			goto out;

		if (signalg == 0)
			cksum.len = 8;
		else
			cksum.len = 16;
		cksum.data = md5cksum.data + 16 - cksum.len;

		dprintk
		    ("RPC: krb5_read_token: memcmp digest cksum.len %d:\n",
		     cksum.len);
		dprintk("          md5cksum.data\n");
		print_hexl((u32 *) md5cksum.data, 16, 0);
		dprintk("          cksum.data:\n");
		print_hexl((u32 *) cksum.data, cksum.len, 0);
		{
			u32 *p;

			(u8 *) p = ptr + 14;
			dprintk("          ptr+14:\n");
			print_hexl(p, cksum.len, 0);
		}

		code = memcmp(cksum.data, ptr + 14, cksum.len);
		break;
	default:
		ret = GSS_S_DEFECTIVE_TOKEN;
		goto out;
	}

	ret = GSS_S_BAD_SIG;
	if (code)
		goto out;

	/* it got through unscathed.  Make sure the context is unexpired */

	if (toktype == KG_TOK_WRAP_MSG)
		*message_buffer = token;

	if (qop_state)
		*qop_state = GSS_C_QOP_DEFAULT;

	now = jiffies;

	ret = GSS_S_CONTEXT_EXPIRED;
	if (now > ctx->endtime)
		goto out;

	/* do sequencing checks */

	ret = GSS_S_BAD_SIG;
	if ((code = krb5_get_seq_num(ctx->seq, ptr + 14, ptr + 6, &direction,
				   &seqnum)))
		goto out;

	if ((ctx->initiate && direction != 0xff) ||
	    (!ctx->initiate && direction != 0))
		goto out;

	ret = GSS_S_COMPLETE;
out:
	if (md5cksum.data) kfree(md5cksum.data);
	if (toktype == KG_TOK_WRAP_MSG) {
		if (plain) kfree(plain);
		if (ret && token.data) kfree(token.data);
	}
	return ret;
}
