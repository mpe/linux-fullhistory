/* $Id: fitoq.c,v 1.5 1999/05/28 13:44:06 jj Exp $
 * arch/sparc64/math-emu/fitoq.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "quad.h"

int FITOQ(void *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_Q(R);
	int a = *(int *)rs2;

	FP_FROM_INT_Q(R, a, 32, int);
	FP_PACK_QP(rd, R);
	return 0;
}
