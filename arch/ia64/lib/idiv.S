/*
 * Integer division routine.
 *
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */
/* Simple integer division.  It uses the straight forward division
   algorithm.  This may not be the absolutely fastest way to do it,
   but it's not horrible either.  According to ski, the worst case
   scenario of dividing 0xffffffffffffffff by 1 takes 133 cycles.

   An alternative would be to use an algorithm similar to the
   floating point division algorithm (Newton-Raphson iteration),
   but that approach is rather tricky (one has to be very careful
   to get the last bit right...).

   While this algorithm is straight-forward, it does use a couple
   of neat ia-64 specific tricks:

	- it uses the floating point unit to determine the initial
	  shift amount (shift = floor(ld(x)) - floor(ld(y)))

	- it uses predication to avoid a branch in the case where
	  x < y (this is what p8 is used for)

	- it uses rotating registers and the br.ctop branch to
	  implement a software-pipelined loop that's unrolled
	  twice (without any code expansion!)

	- the code is relatively well scheduled to avoid unnecessary
	  nops while maximizing parallelism
*/

#include <linux/config.h>
#include <asm/break.h>

	.text
	.psr abi64
#ifdef __BIG_ENDIAN__
	.psr msb
	.msb
#else
	.psr lsb
	.lsb
#endif

#ifdef MODULO
# define OP	mod
# define Q	r9
# define R	r8
#else
# define OP div
# define Q	r8
# define R	r9
#endif

#ifdef SINGLE
# define PREC si
#else
# define PREC di
#endif

#ifdef UNSIGNED
# define SGN		u
# define INT_TO_FP(a,b)	fma.s0 a=b,f1,f0
# define FP_TO_INT(a,b)	fcvt.fxu.trunc.s0 a=b
#else
# define SGN
# define INT_TO_FP(a,b)	fcvt.xf a=b
# define FP_TO_INT(a,b)	fcvt.fx.trunc.s0 a=b
#endif

#define PASTE1(a,b)	a##b
#define PASTE(a,b)	PASTE1(a,b)
#define NAME		PASTE(PASTE(__,SGN),PASTE(OP,PASTE(PREC,3)))

	.align 32
	.global NAME
	.proc NAME
NAME:

	alloc r2=ar.pfs,2,6,0,8
	mov r18=pr
#ifdef SINGLE
# ifdef UNSIGNED
	zxt4 in0=in0
	zxt4 in1=in1
# else
	sxt4 in0=in0
	sxt4 in1=in1
# endif
	;;
#endif

#ifndef UNSIGNED
	cmp.lt p6,p0=in0,r0	// x negative?
	cmp.lt p7,p0=in1,r0	// y negative?
	;;
(p6)	sub in0=r0,in0		// make x positive
(p7)	sub in1=r0,in1		// ditto for y
	;;
#endif

	setf.sig f8=in0
	mov r3=ar.lc		// save ar.lc
	setf.sig f9=in1
	;;
	mov Q=0			// initialize q
	mov R=in0		// stash away x in a static register
	mov r16=1		// r16 = 1
	INT_TO_FP(f8,f8)
	cmp.eq p8,p0=0,in0	// x==0?
	cmp.eq p9,p0=0,in1	// y==0?
	;;
	INT_TO_FP(f9,f9)
(p8)	br.dpnt.few .L3
(p9)	break __IA64_BREAK_KDB	// attempted division by zero (should never happen)
	mov ar.ec=r0		// epilogue count = 0
	;;
	getf.exp r14=f8		// r14 = exponent of x
	getf.exp r15=f9		// r15 = exponent of y
	mov ar.lc=r0		// loop count = 0
	;;
	sub r17=r14,r15		// r17 = (exp of x - exp y) = shift amount
	cmp.ge p8,p0=r14,r15
	;;

	.rotr y[2], mask[2]	// in0 and in1 may no longer be valid after
				// the first write to a rotating register!

(p8)	shl y[1]=in1,r17	// y[1]    = y<<shift
(p8)	shl mask[1]=r16,r17	// mask[1] = 1<<shift

(p8)	mov ar.lc=r17		// loop count = r17
	;;
.L1:
(p8)	cmp.geu.unc p9,p0=R,y[1]// p9 = (x >= y[1])
(p8)	shr.u mask[0]=mask[1],1	// prepare mask[0] and y[0] for next
(p8)	shr.u y[0]=y[1],1	// iteration
	;;
(p9)	sub R=R,y[1]		// if (x >= y[1]), subtract y[1] from x
(p9)	add Q=Q,mask[1]		// and set corresponding bit in q (Q)
	br.ctop.dptk.few .L1	// repeated unless ar.lc-- == 0
	;;
.L2:
#ifndef UNSIGNED
# ifdef MODULO
(p6)	sub R=r0,R		// set sign of remainder according to x
# else
(p6)	sub Q=r0,Q		// set sign of quotient
	;;
(p7)	sub Q=r0,Q
# endif
#endif
.L3:
	mov ar.pfs=r2		// restore ar.pfs
	mov ar.lc=r3		// restore ar.lc
	mov pr=r18,0xffffffffffff0000	// restore p16-p63
	br.ret.sptk.few rp
