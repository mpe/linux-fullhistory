/*
 * Copyright (C) 1992,1995 by
 * Digital Equipment Corporation, Maynard, Massachusetts.
 * This file may be redistributed according to the terms of the
 * GNU General Public License.
 */
#ifndef __ieee_math_h__
#define __ieee_math_h__

#include <asm/fpu.h>

#define ROUND_SHIFT	6		/* make space for trap-enable bits */
#define RM(f)		(((f) >> ROUND_SHIFT) & 0x3)

#define ROUND_CHOP 	(FPCR_DYN_CHOPPED >> FPCR_DYN_SHIFT)
#define ROUND_NINF 	(FPCR_DYN_MINUS   >> FPCR_DYN_SHIFT)
#define ROUND_NEAR 	(FPCR_DYN_NORMAL  >> FPCR_DYN_SHIFT)
#define ROUND_PINF 	(FPCR_DYN_PLUS    >> FPCR_DYN_SHIFT)

extern unsigned long ieee_CVTST (int rm, unsigned long a, unsigned long *b);
extern unsigned long ieee_CVTTS (int rm, unsigned long a, unsigned long *b);
extern unsigned long ieee_CVTQS (int rm, unsigned long a, unsigned long *b);
extern unsigned long ieee_CVTQT (int rm, unsigned long a, unsigned long *b);
extern unsigned long ieee_CVTTQ (int rm, unsigned long a, unsigned long *b);

extern unsigned long ieee_CMPTEQ (unsigned long a, unsigned long b,
				  unsigned long *c);
extern unsigned long ieee_CMPTLT (unsigned long a, unsigned long b,
				  unsigned long *c);
extern unsigned long ieee_CMPTLE (unsigned long a, unsigned long b,
				  unsigned long *c);
extern unsigned long ieee_CMPTUN (unsigned long a, unsigned long b,
				  unsigned long *c);

extern unsigned long ieee_ADDS (int rm, unsigned long a, unsigned long b,
				unsigned long *c);
extern unsigned long ieee_ADDT (int rm, unsigned long a, unsigned long b,
				unsigned long *c);
extern unsigned long ieee_SUBS (int rm, unsigned long a, unsigned long b,
				unsigned long *c);
extern unsigned long ieee_SUBT (int rm, unsigned long a, unsigned long b,
				unsigned long *c);
extern unsigned long ieee_MULS (int rm, unsigned long a, unsigned long b,
				unsigned long *c);
extern unsigned long ieee_MULT (int rm, unsigned long a, unsigned long b,
				unsigned long *c);
extern unsigned long ieee_DIVS (int rm, unsigned long a, unsigned long b,
				unsigned long *c);
extern unsigned long ieee_DIVT (int rm, unsigned long a, unsigned long b,
				unsigned long *c);

#endif /* __ieee_math_h__ */
