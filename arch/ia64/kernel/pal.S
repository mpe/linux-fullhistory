/*
 * PAL Firmware support
 * IA-64 Processor Programmers Reference Vol 2
 *
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 David Mosberger <davidm@hpl.hp.com>
 */

	.text
	.psr abi64
	.psr lsb
	.lsb

	.data
pal_entry_point:
	data8 ia64_pal_default_handler
	.text

/*
 * Set the PAL entry point address.  This could be written in C code, but we do it here
 * to keep it all in one module (besides, it's so trivial that it's
 * not a big deal).
 *
 * in0		Address of the PAL entry point (text address, NOT a function descriptor).
 */
	.align 16
	.global ia64_pal_handler_init
	.proc ia64_pal_handler_init
ia64_pal_handler_init:
	alloc r3=ar.pfs,1,0,0,0
	movl r2=pal_entry_point
	;;
	st8 [r2]=in0
	br.ret.sptk.few rp
	
	.endp ia64_pal_handler_init	

/*
 * Default PAL call handler.  This needs to be coded in assembly because it uses
 * the static calling convention, i.e., the RSE may not be used and calls are
 * done via "br.cond" (not "br.call").
 */
	.align 16
	.global ia64_pal_default_handler
	.proc ia64_pal_default_handler
ia64_pal_default_handler:
	mov r8=-1
	br.cond.sptk.few rp

/*
 * Make a PAL call using the static calling convention.
 *
 * in0         Pointer to struct ia64_pal_retval
 * in1         Index of PAL service
 * in2 - in4   Remaning PAL arguments
 *
 */

#ifdef __GCC_MULTIREG_RETVALS__
# define arg0	in0
# define arg1	in1
# define arg2	in2
# define arg3	in3
# define arg4	in4
#else
# define arg0	in1
# define arg1	in2
# define arg2	in3
# define arg3	in4
# define arg4	in5
#endif

	.text
	.psr abi64
	.psr lsb
	.lsb

	.align 16
	.global	ia64_pal_call_static
	.proc ia64_pal_call_static
ia64_pal_call_static:
	alloc	loc0 = ar.pfs,6,90,0,0
	movl	loc2 = pal_entry_point
1:	{
	  mov	r28 = arg0
	  mov	r29 = arg1
	  mov	r8 = ip
	}
	;;
	ld8	loc2 = [loc2]		// loc2 <- entry point
	mov	r30 = arg2
	mov	r31 = arg3
	;;
	mov	loc3 = psr
	mov	loc1 = rp
	adds	r8 = .ret0-1b,r8
	;; 
	rsm	psr.i
	mov	b7 = loc2
	mov	rp = r8
	;; 
	br.cond.sptk.few b7
.ret0:	mov	psr.l = loc3
#ifndef __GCC_MULTIREG_RETVALS__
	st8	[in0] = r8, 8
	;;
	st8	[in0] = r9, 8 
	;;
	st8	[in0] = r10, 8
	;;
	st8	[in0] = r11, 8
#endif
	mov	ar.pfs = loc0
	mov	rp = loc1
	;;
	srlz.d				// seralize restoration of psr.l
	br.ret.sptk.few	b0
	.endp ia64_pal_call_static
