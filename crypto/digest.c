/*
 * Cryptographic API.
 *
 * Digest operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * The HMAC implementation is derived from USAGI.
 * Copyright (c) 2002 Kazunori Miyazawa <miyazawa@linux-ipv6.org> / USAGI
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#include <linux/crypto.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <asm/scatterlist.h>
#include "internal.h"

static void init(struct crypto_tfm *tfm)
{
	tfm->__crt_alg->cra_digest.dia_init(tfm->crt_ctx);
}

static void update(struct crypto_tfm *tfm,
                   struct scatterlist *sg, unsigned int nsg)
{
	unsigned int i;
	
	for (i = 0; i < nsg; i++) {
		char *p = crypto_kmap(sg[i].page) + sg[i].offset;
		tfm->__crt_alg->cra_digest.dia_update(tfm->crt_ctx,
		                                      p, sg[i].length);
		crypto_kunmap(p);
		crypto_yield(tfm);
	}
}

static void final(struct crypto_tfm *tfm, u8 *out)
{
	tfm->__crt_alg->cra_digest.dia_final(tfm->crt_ctx, out);
}

static void digest(struct crypto_tfm *tfm,
                   struct scatterlist *sg, unsigned int nsg, u8 *out)
{
	unsigned int i;

	tfm->crt_digest.dit_init(tfm);
		
	for (i = 0; i < nsg; i++) {
		char *p = crypto_kmap(sg[i].page) + sg[i].offset;
		tfm->__crt_alg->cra_digest.dia_update(tfm->crt_ctx,
		                                      p, sg[i].length);
		crypto_kunmap(p);
		crypto_yield(tfm);
	}
	crypto_digest_final(tfm, out);
}

static void hmac(struct crypto_tfm *tfm, u8 *key, unsigned int keylen,
                 struct scatterlist *sg, unsigned int nsg, u8 *out)
{
	unsigned int i;
	struct scatterlist tmp;
	char ipad[crypto_tfm_alg_blocksize(tfm) + 1];
	char opad[crypto_tfm_alg_blocksize(tfm) + 1];

	if (keylen > crypto_tfm_alg_blocksize(tfm)) {
		tmp.page = virt_to_page(key);
		tmp.offset = ((long)key & ~PAGE_MASK);
		tmp.length = keylen;
		crypto_digest_digest(tfm, &tmp, 1, key);
		keylen = crypto_tfm_alg_digestsize(tfm);
	}

	memset(ipad, 0, sizeof(ipad));
	memset(opad, 0, sizeof(opad));
	memcpy(ipad, key, keylen);
	memcpy(opad, key, keylen);

	for (i = 0; i < crypto_tfm_alg_blocksize(tfm); i++) {
		ipad[i] ^= 0x36;
		opad[i] ^= 0x5c;
	}

	tmp.page = virt_to_page(ipad);
	tmp.offset = ((long)ipad & ~PAGE_MASK);
	tmp.length = crypto_tfm_alg_blocksize(tfm);

	crypto_digest_init(tfm);
	crypto_digest_update(tfm, &tmp, 1);
	crypto_digest_update(tfm, sg, nsg);
	crypto_digest_final(tfm, out);

	tmp.page = virt_to_page(opad);
	tmp.offset = ((long)opad & ~PAGE_MASK);
	tmp.length = crypto_tfm_alg_blocksize(tfm);

	crypto_digest_init(tfm);
	crypto_digest_update(tfm, &tmp, 1);
	
	tmp.page = virt_to_page(out);
	tmp.offset = ((long)out & ~PAGE_MASK);
	tmp.length = crypto_tfm_alg_digestsize(tfm);
	
	crypto_digest_update(tfm, &tmp, 1);
	crypto_digest_final(tfm, out);
}

int crypto_init_digest_flags(struct crypto_tfm *tfm, u32 flags)
{
	return crypto_cipher_flags(flags) ? -EINVAL : 0;
}

void crypto_init_digest_ops(struct crypto_tfm *tfm)
{
	struct digest_tfm *ops = &tfm->crt_digest;
	
	ops->dit_init   = init;
	ops->dit_update = update;
	ops->dit_final  = final;
	ops->dit_digest = digest;
	ops->dit_hmac   = hmac;
}
