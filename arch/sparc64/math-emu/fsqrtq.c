/* $Id: fsqrtq.c,v 1.5 1999/05/28 13:44:44 jj Exp $
 * arch/sparc64/math-emu/fsqrtq.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "quad.h"

int FSQRTQ(void *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_Q(A); FP_DECL_Q(R);
        
	FP_UNPACK_QP(A, rs2);
	FP_SQRT_Q(R, A);
	FP_PACK_QP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
