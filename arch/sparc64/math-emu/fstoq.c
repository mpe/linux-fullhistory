/* $Id: fstoq.c,v 1.4 1999/05/28 13:44:58 jj Exp $
 * arch/sparc64/math-emu/fstoq.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "quad.h"
#include "single.h"

int FSTOQ(void *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_S(A); FP_DECL_Q(R);

	FP_UNPACK_SP(A, rs2);
	FP_CONV(Q,S,2,1,R,A);
	FP_PACK_QP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
