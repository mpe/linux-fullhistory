/* $Id: fqtoi.c,v 1.4 1999/05/28 13:44:26 jj Exp $
 * arch/sparc64/math-emu/fqtoi.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#define FP_ROUNDMODE FP_RND_ZERO
#include "sfp-util.h"
#include "soft-fp.h"
#include "quad.h"

int FQTOI(int *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_Q(A);
	int r;

	FP_UNPACK_QP(A, rs2);
	FP_TO_INT_Q(r, A, 32, 1);
	if (!FP_INHIBIT_RESULTS)
		*rd = r;
	FP_HANDLE_EXCEPTIONS;
}
