/*
 * Just like strncpy() except for the return value.  If no fault occurs during
 * the copying, the number of bytes copied is returned.  If a fault occurs,
 * -EFAULT is returned.
 *
 * Inputs:
 *	in0:	address of destination buffer
 *	in1:	address of string to be copied
 *	in2:	length of buffer in bytes
 * Outputs:
 *	r8:	-EFAULT in case of fault or number of bytes copied if no fault
 * 
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#define EX(x...)				\
99:	x;					\
	.section __ex_table,"a";		\
	data4 @gprel(99b);			\
	data4 .Lexit-99b;			\
	.previous

	.text
	.psr abi64
	.psr lsb
	.lsb

	.align 32
	.global __strncpy_from_user
	.proc __strncpy_from_user
__strncpy_from_user:
	alloc r11=ar.pfs,3,0,0,0
	mov r9=in1
	add r10=in1,in2

	// XXX braindead copy loop---this needs to be optimized
.Loop1:
	EX(ld1 r8=[in1],1)
	;;
	st1 [in0]=r8,1
	cmp.ltu p6,p0=in1,r10
	;;
(p6)	cmp.ne.and p6,p0=r8,r0
	;;
(p6)	br.cond.dpnt.few .Loop1

1:	sub r8=in1,r9		// length of string (including NUL character)
.Lexit:
	mov ar.pfs=r11
	br.ret.sptk.few rp

	.endp __strncpy_from_user
