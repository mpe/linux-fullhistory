/*
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#ifndef __GCC_MULTIREG_RETVALS__
	/*
	 * gcc currently does not conform to the ia-64 calling
	 * convention as far as returning function values are
	 * concerned.  Instead of returning values up to 32 bytes in
	 * size in r8-r11, gcc returns any value bigger than a
	 * doubleword via a structure that's allocated by the caller
	 * and whose address is passed into the function.  Since
	 * SAL_PROC returns values according to the calling
	 * convention, this stub takes care of copying r8-r11 to the
	 * place where gcc expects them.
	 */
	.text
	.psr abi64
	.psr lsb
	.lsb

	.align 16
	.global ia64_sal_stub
ia64_sal_stub:
	/*
	 * Sheesh, the Cygnus backend passes the pointer to a return value structure in
	 * in0 whereas the HP backend passes it in r8.  Don't you hate those little
	 * differences...
	 */
#ifdef GCC_RETVAL_POINTER_IN_R8
	adds r2=-24,sp
	adds sp=-48,sp
	mov r14=rp
	;;
	st8	[r2]=r8,8	// save pointer to return value
	addl	r3=@ltoff(ia64_sal),gp
	;;
	ld8	r3=[r3]
	st8	[r2]=gp,8	// save global pointer
	;;
	ld8	r3=[r3]		// fetch the value of ia64_sal
	st8	[r2]=r14	// save return pointer
	;;
	ld8	r2=[r3],8	// load function's entry point
	;;
	ld8	gp=[r3]		// load function's global pointer
	;;
	mov	b6=r2
	br.call.sptk.few rp=b6
.ret0:	adds	r2=24,sp
	;;
	ld8	r3=[r2],8	// restore pointer to return value
	;;
	ld8	gp=[r2],8	// restore global pointer
	st8	[r3]=r8,8
	;;
	ld8	r14=[r2]	// restore return pointer
	st8	[r3]=r9,8
	;;
	mov	rp=r14
	st8	[r3]=r10,8
	;;
	st8	[r3]=r11,8
	adds	sp=48,sp
	br.sptk.few rp
#else
	/*
	 * On input:
	 *	in0 = pointer to return value structure
	 *	in1 = index of SAL function to call
	 *	in2..inN = remaining args to SAL call
	 */
	/*
	 * We allocate one input and eight output register such that the br.call instruction
	 * will rename in1-in7 to in0-in6---exactly what we want because SAL doesn't want to
	 * see the pointer to the return value structure.
	 */
	alloc	r15=ar.pfs,1,0,8,0

	adds	r2=-24,sp
	adds	sp=-48,sp
	mov	r14=rp
	;;
	st8	[r2]=r15,8	// save ar.pfs
	addl	r3=@ltoff(ia64_sal),gp
	;;
	ld8	r3=[r3]		// get address of ia64_sal
	st8	[r2]=gp,8	// save global pointer
	;;
	ld8	r3=[r3]		// get value of ia64_sal
	st8	[r2]=r14,8	// save return address (rp)
	;;
	ld8	r2=[r3],8	// load function's entry point
	;;
	ld8	gp=[r3]		// load function's global pointer
	mov	b6=r2
	br.call.sptk.few rp=b6	// make SAL call
.ret0:	adds	r2=24,sp
	;;
	ld8	r15=[r2],8	// restore ar.pfs
	;;
	ld8	gp=[r2],8	// restore global pointer
	st8	[in0]=r8,8	// store 1. dword of return value
	;;
	ld8	r14=[r2]	// restore return address (rp)
	st8	[in0]=r9,8	// store 2. dword of return value
	;;
	mov	rp=r14
	st8	[in0]=r10,8	// store 3. dword of return value
	;;
	st8	[in0]=r11,8
	adds	sp=48,sp	// pop stack frame
	mov	ar.pfs=r15
	br.ret.sptk.few rp
#endif

	.endp ia64_sal_stub
#endif /* __GCC_MULTIREG_RETVALS__ */
