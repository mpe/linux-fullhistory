/* $Id: fdtoi.c,v 1.3 1999/05/28 13:43:52 jj Exp $
 * arch/sparc64/math-emu/fdtoi.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#define FP_ROUNDMODE FP_RND_ZERO
#include "sfp-util.h"
#include "soft-fp.h"
#include "double.h"

int FDTOI(int *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_D(A);
	int r;

	FP_UNPACK_DP(A, rs2);
	FP_TO_INT_D(r, A, 32, 1);
	if (!FP_INHIBIT_RESULTS)
		*rd = r;
	FP_HANDLE_EXCEPTIONS;
}
