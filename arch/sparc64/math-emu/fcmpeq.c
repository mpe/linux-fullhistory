/* $Id: fcmpeq.c,v 1.5 1999/05/28 13:43:29 jj Exp $
 * arch/sparc64/math-emu/fcmpeq.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "quad.h"

int FCMPEQ(void *rd, void *rs2, void *rs1)
{
	FP_DECL_EX;
	FP_DECL_Q(A); FP_DECL_Q(B);
	long ret;
	long fccno = (long)rd;
	unsigned long fsr;
	
	FP_UNPACK_RAW_QP(A, rs1);
	FP_UNPACK_RAW_QP(B, rs2);
	FP_CMP_Q(ret, B, A, 3);
	if (ret == 3)
		FP_SET_EXCEPTION(FP_EX_INVALID);
	if (!FP_INHIBIT_RESULTS) {
		rd = (void *)(((long)rd)&~3);
		if (ret == -1) ret = 2;
		fsr = current->tss.xfsr[0];
		switch (fccno) {
		case 0: fsr &= ~0xc00; fsr |= (ret << 10); break;
		case 1: fsr &= ~0x300000000UL; fsr |= (ret << 32); break;
		case 2: fsr &= ~0xc00000000UL; fsr |= (ret << 34); break;
		case 3: fsr &= ~0x3000000000UL; fsr |= (ret << 36); break;
		}
		current->tss.xfsr[0] = fsr;
	}
	FP_HANDLE_EXCEPTIONS;
}
