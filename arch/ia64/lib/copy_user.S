/*
 * This routine copies a linear memory buffer across the user/kernel boundary.  When
 * reading a byte from the source causes a fault, the remainder of the destination
 * buffer is zeroed out.  Note that this can happen only when copying from user
 * to kernel memory and we do this to absolutely guarantee that the
 * kernel doesn't operate on random data.
 *
 * This file is derived from arch/alpha/lib/copy_user.S.
 *
 * Inputs:
 *	in0:	address of destination buffer
 *	in1:	address of source buffer
 *	in2:	length of buffer in bytes
 * Outputs:
 *	r8:	number of bytes that didn't get copied due to a fault
 * 
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#define EXI(x...)				\
99:	x;					\
	.section __ex_table,"a";		\
	data4 @gprel(99b);			\
	data4 .Lexit_in-99b;			\
	.previous

#define EXO(x...)				\
99:	x;					\
	.section __ex_table,"a";		\
	data4 @gprel(99b);			\
	data4 .Lexit_out-99b;			\
	.previous

	.text
	.psr abi64
	.psr lsb
	.lsb

	.align 32
	.global __copy_user
	.proc __copy_user
__copy_user:
	alloc r10=ar.pfs,3,0,0,0
	mov r9=ar.lc		// save ar.lc
	mov ar.lc=in2		// set ar.lc to length of buffer
	br.sptk.few .Lentr

	// XXX braindead copy loop---this needs to be optimized
.Loop1:
	EXI(ld1 r8=[in1],1)
	;;
	EXO(st1 [in0]=r8,1)
.Lentr:	br.cloop.dptk.few .Loop1	// repeat unless ar.lc--==0
	;;			// avoid RAW on ar.lc
.Lexit_out:
	mov r8=ar.lc		// return how many bytes we _didn't_ copy
	mov ar.lc=r9
	br.ret.sptk.few rp

.Lexit_in:
	// clear the remainder of the buffer:
	mov r8=ar.lc	// return how many bytes we _didn't_ copy
.Loop2:
	st1 [in0]=r0,1	// this cannot fault because we get here only on user->kernel copies
	br.cloop.dptk.few .Loop2
	;;				// avoid RAW on ar.lc
	mov ar.lc=r9
	br.ret.sptk.few rp

	.endp __copy_user
