/* $Id: fxtoq.c,v 1.5 1999/05/28 13:45:15 jj Exp $
 * arch/sparc64/math-emu/fxtoq.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "quad.h"

int FXTOQ(void *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_Q(R);
	long a = *(long *)rs2;

	FP_FROM_INT_Q(R, a, 64, long);
	FP_PACK_QP(rd, R);
	return 0;
}
