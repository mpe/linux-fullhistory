/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Tom May, <ftom@netcom.com>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 * Changes:     Ingo Molnar, converted to 2.1 exception handling
 *              Istvan Marosi, fixed lots of bugs in the converted
 *                             assembly code.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#define ASSEMBLY
#include <asm/smp.h>
#include <asm/errno.h>

/*
 * Copy from ds while checksumming, otherwise like csum_partial
 *
 * ok, from now on better close your eyes. This file is a top canditate for
 * the 'ugliest place in LinuxLand' award.
 *
 * The macros SRC_* and DST_* specify the type of access for the instruction.
 * thus we can call a custom exception handler for all access types.
 *
 * Damn damn damn. We mess up the stack frame thus we have to have exception
 * handlers for all these cases:
 *
 * 1) source fault + clean stack
 * 2) source fault + 32 bits pushed to the stack
 * 3) destination fault + clean stack
 * 4) destination fault + 32 bits pushed to stack
 *
 * {the _S postfix below means 'clean up the stack'}
 */

#define SRC(x...)				\
	9999:  x;				\
	.section __ex_table, "a";		\
	.long 9999b, src_access_fault;		\
	.previous

#define SRC_S(x...)				\
	9999: x;				\
	.section __ex_table, "a";		\
	.long 9999b, src_access_fault_stack;	\
	.previous

#define DST(x...)				\
	9999: x;				\
	.section __ex_table, "a";		\
	.long 9999b, dst_access_fault;		\
	.previous

#define DST_S(x...)				\
	9999: x;				\
	.section __ex_table, "a";		\
	.long 9999b, dst_access_fault_stack;	\
	.previous

ENTRY(__csum_partial_copy_i386_generic)

# As we are a .S, we have to get all parameters from the stack ourselves :(
#
# stack layout after register saving (dont break it ..):
#
# char *src --------------> 0x14(%esp)
# char *dst --------------> 0x18(%esp)
# int len ----------------> 0x1c(%esp)
# int sum ----------------> 0x20(%esp)
# int *src_err_ptr -------> 0x24(%esp)
# int *dst_err_ptr -------> 0x28(%esp)

		pushl %edi
		pushl %esi
		pushl %ebx
		pushl %ecx

		movl 0x14(%esp), %esi
		movl 0x18(%esp), %edi
		movl 0x1c(%esp), %ecx
		movl 0x20(%esp), %eax

		testl $2, %edi		# Check alignment. 
		jz 2f			# Jump if alignment is ok.
		subl $2, %ecx		# Alignment uses up two bytes.
		jae 1f			# Jump if we had at least two bytes.
		addl $2, %ecx		# ecx was < 2.  Deal with it.
		jmp 4f
SRC(	1:	movw (%esi), %bx				)
		addl $2, %esi
DST(		movw %bx, (%edi)				)
		addl $2, %edi
		addw %bx, %ax	
		adcl $0, %eax
	2:
		pushl %ecx

# from here on we have 32 bits on the stack.

		shrl $5, %ecx
		jz 2f
		testl %esi, %esi
SRC_S(	1:	movl (%esi), %ebx				)
SRC_S(		movl 4(%esi), %edx				)
		adcl %ebx, %eax
DST_S(		movl %ebx, (%edi)				)
		adcl %edx, %eax
DST_S(		movl %edx, 4(%edi)				)

SRC_S(		movl 8(%esi), %ebx				)
SRC_S(		movl 12(%esi), %edx				)
		adcl %ebx, %eax
DST_S(		movl %ebx, 8(%edi)				)
		adcl %edx, %eax
DST_S(		movl %edx, 12(%edi)				)

SRC_S(		movl 16(%esi), %ebx 				)
SRC_S(		movl 20(%esi), %edx				)
		adcl %ebx, %eax
DST_S(		movl %ebx, 16(%edi)				)
		adcl %edx, %eax
DST_S(		movl %edx, 20(%edi)				)

SRC_S(		movl 24(%esi), %ebx				)
SRC_S(		movl 28(%esi), %edx				)
		adcl %ebx, %eax
DST_S(		movl %ebx, 24(%edi)				)
		adcl %edx, %eax
DST_S(		movl %edx, 28(%edi)				)

# lea cannot fault, it's just pointer preparation:

		lea 32(%esi), %esi
		lea 32(%edi), %edi
		dec %ecx
		jne 1b
		adcl $0, %eax
	2:	popl %edx

# from here on we have a clean stack again

		movl %edx, %ecx
		andl $0x1c, %edx
		je 4f
		shrl $2, %edx		# This clears CF

SRC(	3:	movl (%esi), %ebx )
		lea 4(%esi), %esi
		adcl %ebx, %eax
DST(		movl %ebx, (%edi) )
		lea 4(%edi), %edi
		dec %edx
		jne 3b

		adcl $0, %eax
	4:	andl $3, %ecx
		jz 7f
		cmpl $2, %ecx
		jb 5f
SRC(		movw (%esi), %cx				)
SRC(		leal 2(%esi), %esi				)
DST(		movw %cx, (%edi)				)
DST(		leal 2(%edi), %edi				)
		je 6f
		shll $16,%ecx
SRC(	5:	movb (%esi), %cl				)
DST(		movb %cl, (%edi)				)
	6:	addl %ecx, %eax
		adcl $0, %eax
	7:

end_of_body:
		popl %ecx
		popl %ebx
		popl %esi
		popl %edi

		ret

# Exception handlers:
################################################

.section .fixup, "a"

common_src_fixup:

# the _from_user() stuff has to zero out the kernel buffer, to prevent
# sending out random kernel data.

# This inefficient but safe. We do not care too much about exception handler
# efficiency, they are totally dead code usually.

# We cannot use %ecx easily, as it could contain 'real length', or
# 'real length/32'. So we simply reload %ecx from our parameter block
# and zero the whole buffer. This means we set sum = input sum too.

# We assume that writing into %edi is safe. So if we are ever so crazy to
# do some kind of user_to_user checksum with copying, be prepared to fix 
# this ...

	movl    0x24(%esp), %ebx
	movl	$(-EFAULT), %eax
	movl	%eax, (%ebx)

	movl	0x18(%esp), %edi
	movl	0x1c(%esp), %ecx

	xorl	%eax,%eax
        cld
        rep
        stosb

	movl	0x20(%esp), %eax

	jmp	end_of_body

src_access_fault:
	jmp	common_src_fixup

src_access_fault_stack:
	popl	%ebx 		# <--- clean up the stack
	jmp	common_src_fixup

# Currently we do not copy and checksum towards user space,
# but the code is ready anyways ...


common_dst_fixup:
	movl    0x28(%esp), %ebx
	movl	$(-EFAULT), (%ebx)
	movl	0x20(%esp), %eax

	jmp	end_of_body

dst_access_fault:
	jmp	common_dst_fixup

dst_access_fault_stack:
	popl	%ebx 		# <--- clean up the stack
	jmp	common_dst_fixup

.previous


