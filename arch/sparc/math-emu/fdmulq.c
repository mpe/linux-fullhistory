/* $Id: fdmulq.c,v 1.9 1999/05/28 13:41:56 jj Exp $
 * arch/sparc/math-emu/fdmulq.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1998 Peter Maydell (pmaydell@chiark.greenend.org.uk)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "quad.h"
#include "double.h"

int FDMULQ(void *rd, void *rs2, void *rs1)
{
	FP_DECL_EX;
	FP_DECL_D(IN); FP_DECL_Q(A); FP_DECL_Q(B); FP_DECL_Q(R);

	FP_UNPACK_DP(IN, rs1);
	FP_CONV(Q,D,4,2,A,IN);
	FP_UNPACK_DP(IN, rs2);
	FP_CONV(Q,D,4,2,B,IN);
	FP_MUL_Q(R, A, B);
	FP_PACK_QP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
