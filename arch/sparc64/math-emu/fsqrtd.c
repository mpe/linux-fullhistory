/* $Id: fsqrtd.c,v 1.4 1999/05/28 13:44:39 jj Exp $
 * arch/sparc64/math-emu/fsqrtd.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "double.h"

int FSQRTD(void *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_D(A); FP_DECL_D(R);
        
	FP_UNPACK_DP(A, rs2);
	FP_SQRT_D(R, A);
	FP_PACK_DP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
