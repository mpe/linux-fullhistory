/* $Id: fstoi.c,v 1.3 1999/05/28 13:44:54 jj Exp $
 * arch/sparc64/math-emu/fstoi.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#define FP_ROUNDMODE FP_RND_ZERO
#include "sfp-util.h"
#include "soft-fp.h"
#include "single.h"

int FSTOI(int *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_S(A);
	int r;

	FP_UNPACK_SP(A, rs2);
	FP_TO_INT_S(r, A, 32, 1);
	if (!FP_INHIBIT_RESULTS)
		*rd = r;
	FP_HANDLE_EXCEPTIONS;
}
