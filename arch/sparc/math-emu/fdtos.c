/* $Id: fdtos.c,v 1.9 1999/05/28 13:42:03 jj Exp $
 * arch/sparc/math-emu/fdtos.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1998 Peter Maydell (pmaydell@chiark.greenend.org.uk)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "double.h"
#include "single.h"

int FDTOS(void *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_D(A); FP_DECL_S(R);

	FP_UNPACK_DP(A, rs2);
	FP_CONV(S,D,1,2,R,A);
	FP_PACK_SP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
