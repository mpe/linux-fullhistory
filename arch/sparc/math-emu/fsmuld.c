/* $Id: fsmuld.c,v 1.9 1999/05/28 13:42:12 jj Exp $
 * arch/sparc/math-emu/fsmuld.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1998 Peter Maydell (pmaydell@chiark.greenend.org.uk)
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
	FP_CONV(D,S,2,1,A,IN);
	FP_UNPACK_SP(IN, rs2);
	FP_CONV(D,S,2,1,B,IN);
	FP_MUL_D(R, A, B);
	FP_PACK_DP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
