/* $Id: fqtos.c,v 1.9 1999/05/28 13:42:10 jj Exp $
 * arch/sparc/math-emu/fqtos.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1998 Peter Maydell (pmaydell@chiark.greenend.org.uk)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "quad.h"
#include "single.h"

int FQTOS(void *rd, void *rs2)
{
	FP_DECL_EX;
	FP_DECL_Q(A); FP_DECL_S(R);

	FP_UNPACK_QP(A, rs2);
	FP_CONV(S,Q,1,4,R,A);
	FP_PACK_SP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}
