/* $Id: fsqrts.c,v 1.4 1999/05/28 13:44:48 jj Exp $
 * arch/sparc64/math-emu/fsqrts.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "single.h"

int FSQRTS(void *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_S(A); FP_DECL_S(R);
        
	FP_UNPACK_SP(A, rs2);
	FP_SQRT_S(R, A);
	FP_PACK_SP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
