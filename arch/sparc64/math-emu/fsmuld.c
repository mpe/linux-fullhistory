/* $Id: fsmuld.c,v 1.4 1999/05/28 13:44:37 jj Exp $
 * arch/sparc64/math-emu/fsmuld.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "double.h"
#include "single.h"

int FSMULD(void *rd, void *rs2, void *rs1)
{
	FP_DECL_EX;
	FP_DECL_S(IN); FP_DECL_D(A); FP_DECL_D(B); FP_DECL_D(R);

	FP_UNPACK_SP(IN, rs1);
	FP_CONV(D,S,1,1,A,IN);
	FP_UNPACK_SP(IN, rs2);
	FP_CONV(D,S,1,1,B,IN);
	FP_MUL_D(R, A, B);
	FP_PACK_DP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
