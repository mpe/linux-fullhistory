/*
 * Integer division routine.
 *
 * Copyright (C) 1999-2000 Hewlett-Packard Co
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/asmmacro.h>

/*
 * Compute a 64-bit unsigned integer quotient.
 *
 * Use reciprocal approximation and Newton-Raphson iteration to compute the
 * quotient.  frcpa gives 8.6 significant bits, so we need 3 iterations
 * to get more than the 64 bits of precision that we need for DImode.
 *
 * Must use max precision for the reciprocal computations to get 64 bits of
 * precision.
 *
 * r32 holds the dividend.  r33 holds the divisor.
 */

#ifdef MODULO
# define OP	mod
#else
# define OP	div
#endif

#ifdef UNSIGNED
# define SGN	u
# define INT_TO_FP(a,b)	fcvt.xuf.s1 a=b
# define FP_TO_INT(a,b)	fcvt.fxu.trunc.s1 a=b
#else
# define SGN
# define INT_TO_FP(a,b)	fcvt.xf a=b
# define FP_TO_INT(a,b)	fcvt.fx.trunc.s1 a=b
#endif

#define PASTE1(a,b)	a##b
#define PASTE(a,b)	PASTE1(a,b)
#define NAME		PASTE(PASTE(__,SGN),PASTE(OP,di3))

GLOBAL_ENTRY(NAME)
	UNW(.prologue)
	.regstk 2,0,0,0
	// Transfer inputs to FP registers.
	setf.sig f8 = in0
	setf.sig f9 = in1
	UNW(.fframe 16)
	UNW(.save.f 0x20)
	stf.spill [sp] = f17,-16

	// Convert the inputs to FP, to avoid FP software-assist faults.
	INT_TO_FP(f8, f8)
	;;

	UNW(.save.f 0x10)
	stf.spill [sp] = f16
	UNW(.body)
	INT_TO_FP(f9, f9)
	;;
	frcpa.s1 f17, p6 = f8, f9	// y = frcpa(b)
	;;
	/*
	 * This is the magic algorithm described in Section 8.6.2 of "IA-64
	 * and Elementary Functions" by Peter Markstein; HP Professional Books
	 * (http://www.hp.com/go/retailbooks/)
	 */
(p6)	fmpy.s1 f7 = f8, f17		// q = a*y
(p6)	fnma.s1 f6 = f9, f17, f1	// e = -b*y + 1 
	;;
(p6)	fma.s1 f16 = f7, f6, f7		// q1 = q*e + q
(p6)	fmpy.s1 f7 = f6, f6		// e1 = e*e
	;;
(p6)	fma.s1 f16 = f16, f7, f16	// q2 = q1*e1 + q1
(p6)	fma.s1 f6 = f17, f6, f17	// y1 = y*e + y 
	;;
(p6)	fma.s1 f6 = f6, f7, f6		// y2 = y1*e1 + y1
(p6)	fnma.s1 f7 = f9, f16, f8	// r = -b*q2 + a 
	;;
(p6)	fma.s1 f17 = f7, f6, f16	// q3 = r*y2 + q2
	;;
#ifdef MODULO
	FP_TO_INT(f17, f17)		// round quotient to an unsigned integer
	;;
	INT_TO_FP(f17, f17)		// renormalize
	;;
	fnma.s1 f17 = f17, f9, f8	// compute remainder
	;;
#endif
	UNW(.restore sp)
	ldf.fill f16 = [sp], 16
	FP_TO_INT(f8, f17)		// round result to an (unsigned) integer
	;;
	ldf.fill f17 = [sp]
	getf.sig r8 = f8		// transfer result to result register
	br.ret.sptk rp
END(NAME)
