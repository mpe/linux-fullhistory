/* $Id: fsubd.c,v 1.4 1999/05/28 13:45:04 jj Exp $
 * arch/sparc64/math-emu/fsubd.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "double.h"

int FSUBD(void *rd, void *rs2, void *rs1)
{
	FP_DECL_EX;
	FP_DECL_D(A); FP_DECL_D(B); FP_DECL_D(R);

	FP_UNPACK_DP(A, rs1);
	FP_UNPACK_DP(B, rs2);
	if (B_c != FP_CLS_NAN)
		B_s ^= 1;
	FP_ADD_D(R, A, B);
	FP_PACK_DP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
