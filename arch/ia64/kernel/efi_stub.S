/*
 * EFI call stub.
 *
 * Copyright (C) 1999 David Mosberger <davidm@hpl.hp.com>
 *
 * This stub allows us to make EFI calls in physical mode with interrupts
 * turned off.  We need this because we can't call SetVirtualMap() until
 * the kernel has booted far enough to allow allocation of struct vma_struct
 * entries (which we would need to map stuff with memory attributes other
 * than uncached or writeback...).  Since the GetTime() service gets called
 * earlier than that, we need to be able to make physical mode EFI calls from
 * the kernel.
 */

/*
 * PSR settings as per SAL spec (Chapter 8 in the "IA-64 System
 * Abstraction Layer Specification", revision 2.6e).  Note that
 * psr.dfl and psr.dfh MUST be cleared, despite what this manual says.
 * Otherwise, SAL dies whenever it's trying to do an IA-32 BIOS call
 * (the br.ia instruction fails unless psr.dfl and psr.dfh are
 * cleared).  Fortunately, SAL promises not to touch the floating
 * point regs, so at least we don't have to save f2-f127.
 */
#define PSR_BITS_TO_CLEAR						\
	(IA64_PSR_I | IA64_PSR_IT | IA64_PSR_DT | IA64_PSR_RT |		\
	 IA64_PSR_DD | IA64_PSR_SS | IA64_PSR_RI | IA64_PSR_ED |	\
	 IA64_PSR_DFL | IA64_PSR_DFH)

#define PSR_BITS_TO_SET							\
	(IA64_PSR_BN)

#include <asm/processor.h>

	.text
	.psr abi64
	.psr lsb
	.lsb

	.text

/*
 * Switch execution mode from virtual to physical or vice versa.
 *
 * Inputs:
 *	r16 = new psr to establish
 */
	.proc switch_mode
switch_mode:
 {
	alloc r2=ar.pfs,0,0,0,0
	rsm psr.i | psr.ic		// disable interrupts and interrupt collection
	mov r15=ip
 }
	;;
 {
	flushrs				// must be first insn in group
	srlz.i
	shr.u r19=r15,61		// r19 <- top 3 bits of current IP
 }
	;;
	mov cr.ipsr=r16			// set new PSR
	add r3=1f-switch_mode,r15
	xor r15=0x7,r19			// flip the region bits

	mov r17=ar.bsp
	mov r14=rp			// get return address into a general register

	// switch RSE backing store:
	;;
	dep r17=r15,r17,61,3		// make ar.bsp physical or virtual
	mov r18=ar.rnat			// save ar.rnat
	;;
	mov ar.bspstore=r17		// this steps on ar.rnat
	dep r3=r15,r3,61,3		// make rfi return address physical or virtual
	;;
	mov cr.iip=r3
	mov cr.ifs=r0
	dep sp=r15,sp,61,3		// make stack pointer physical or virtual
	;;
	mov ar.rnat=r18			// restore ar.rnat
	dep r14=r15,r14,61,3		// make function return address physical or virtual
	rfi				// must be last insn in group
	;;
1:	mov rp=r14
	br.ret.sptk.few rp
	.endp switch_mode

/*
 * Inputs:
 *	in0 = address of function descriptor of EFI routine to call
 *	in1..in7 = arguments to routine
 *
 * Outputs:
 *	r8 = EFI_STATUS returned by called function
 */

	.global efi_call_phys
	.proc efi_call_phys
efi_call_phys:

	alloc loc0=ar.pfs,8,5,7,0
	ld8 r2=[in0],8			// load EFI function's entry point
	mov loc1=rp
	;;
	mov loc2=gp			// save global pointer
	mov loc4=ar.rsc			// save RSE configuration
	mov ar.rsc=r0			// put RSE in enforced lazy, LE mode
	;;

	ld8 gp=[in0]			// load EFI function's global pointer
	mov out0=in1
	mov out1=in2
	movl r16=PSR_BITS_TO_CLEAR

	mov loc3=psr			// save processor status word
	movl r17=PSR_BITS_TO_SET
	;;
	mov out2=in3
	or loc3=loc3,r17
	mov b6=r2
	;;
	andcm r16=loc3,r16		// get psr with IT, DT, and RT bits cleared
	mov out3=in4
	br.call.sptk.few rp=switch_mode
.ret0:
	mov out4=in5
	mov out5=in6
	mov out6=in7
	br.call.sptk.few rp=b6		// call the EFI function
.ret1:
	mov ar.rsc=r0			// put RSE in enforced lazy, LE mode
	mov r16=loc3
	br.call.sptk.few rp=switch_mode	// return to virtual mode
.ret2:
	mov ar.rsc=loc4			// restore RSE configuration
	mov ar.pfs=loc0
	mov rp=loc1
	mov gp=loc2
	br.ret.sptk.few rp
	
	.endp efi_call_phys
