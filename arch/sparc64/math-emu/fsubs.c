/* $Id: fsubs.c,v 1.5 1999/08/02 14:08:07 jj Exp $
 * arch/sparc64/math-emu/fsubs.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "single.h"

int FSUBS(void *rd, void *rs2, void *rs1)
{
	FP_DECL_EX;
	FP_DECL_S(A); FP_DECL_S(B); FP_DECL_S(R);

	FP_UNPACK_SP(A, rs1);
	FP_UNPACK_SP(B, rs2);
	FP_SUB_S(R, A, B);
	FP_PACK_SP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
