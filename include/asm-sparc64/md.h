/* $Id: md.h,v 1.2 1997/12/27 16:28:38 jj Exp $
 * md.h: High speed xor_block operation for RAID4/5 
 *            utilizing the UltraSparc Visual Instruction Set.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
#ifndef __ASM_MD_H
#define __ASM_MD_H

#include <asm/head.h>
#include <asm/asi.h>

#define HAVE_ARCH_XORBLOCK

#define MD_XORBLOCK_ALIGNMENT	64

/*	void __xor_block (char *dest, char *src, long len)
 *	{
 *		while (len--) *dest++ ^= *src++;
 * 	}
 *
 *	Requirements:
 *	!(((long)dest | (long)src) & (MD_XORBLOCK_ALIGNMENT - 1)) &&
 *	!(len & 127) && len >= 256
 */

static inline void __xor_block (char *dest, char *src, long len)
{
	__asm__ __volatile__ ("
	wr	%%g0, %3, %%fprs
	wr	%%g0, %4, %%asi
	membar	#LoadStore|#StoreLoad|#StoreStore
	sub	%2, 128, %2
	ldda	[%0] %4, %%f0
	ldda	[%1] %4, %%f16
1:	ldda	[%0 + 64] %%asi, %%f32
	fxor	%%f0, %%f16, %%f16
	fxor	%%f2, %%f18, %%f18
	fxor	%%f4, %%f20, %%f20
	fxor	%%f6, %%f22, %%f22
	fxor	%%f8, %%f24, %%f24
	fxor	%%f10, %%f26, %%f26
	fxor	%%f12, %%f28, %%f28
	fxor	%%f14, %%f30, %%f30
	stda	%%f16, [%0] %4
	ldda	[%1 + 64] %%asi, %%f48
	ldda	[%0 + 128] %%asi, %%f0
	fxor	%%f32, %%f48, %%f48
	fxor	%%f34, %%f50, %%f50
	add	%0, 128, %0
	fxor	%%f36, %%f52, %%f52
	add	%1, 128, %1
	fxor	%%f38, %%f54, %%f54
	subcc	%2, 128, %2
	fxor	%%f40, %%f56, %%f56
	fxor	%%f42, %%f58, %%f58
	fxor	%%f44, %%f60, %%f60
	fxor	%%f46, %%f62, %%f62
	stda	%%f48, [%0 - 64] %%asi
	bne,pt	%%xcc, 1b
	 ldda	[%1] %4, %%f16
	ldda	[%0 + 64] %%asi, %%f32
	fxor	%%f0, %%f16, %%f16
	fxor	%%f2, %%f18, %%f18
	fxor	%%f4, %%f20, %%f20
	fxor	%%f6, %%f22, %%f22
	fxor	%%f8, %%f24, %%f24
	fxor	%%f10, %%f26, %%f26
	fxor	%%f12, %%f28, %%f28
	fxor	%%f14, %%f30, %%f30
	stda	%%f16, [%0] %4
	ldda	[%1 + 64] %%asi, %%f48
	membar	#Sync
	fxor	%%f32, %%f48, %%f48
	fxor	%%f34, %%f50, %%f50
	fxor	%%f36, %%f52, %%f52
	fxor	%%f38, %%f54, %%f54
	fxor	%%f40, %%f56, %%f56
	fxor	%%f42, %%f58, %%f58
	fxor	%%f44, %%f60, %%f60
	fxor	%%f46, %%f62, %%f62
	stda	%%f48, [%0 + 64] %%asi
	membar	#Sync|#StoreStore|#StoreLoad
	wr	%%g0, 0, %%fprs
	" : :
	"r" (dest), "r" (src), "r" (len), "i" (FPRS_FEF), "i" (ASI_BLK_P) :
	"cc", "memory");
}

#endif /* __ASM_MD_H */
