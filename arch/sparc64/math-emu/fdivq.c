/* $Id: fdivq.c,v 1.4 1999/05/28 13:43:41 jj Exp $
 * arch/sparc64/math-emu/fdivq.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "quad.h"

int FDIVQ(void *rd, void *rs2, void *rs1)
{
	FP_DECL_EX;
	FP_DECL_Q(A); FP_DECL_Q(B); FP_DECL_Q(R);

	FP_UNPACK_QP(A, rs1);
	FP_UNPACK_QP(B, rs2);
	FP_DIV_Q(R, A, B);
	FP_PACK_QP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
