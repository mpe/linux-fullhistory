/* strncpy_from_user.S: Sparc64 strncpy from userspace.
 *
 *  Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/asi.h>
#include <asm/errno.h>

	.text
	.align	4

	/* Must return:
	 *
	 * -EFAULT		for an exception
	 * count		if we hit the buffer limit
	 * bytes copied		if we hit a null byte
	 */

	.globl	__strncpy_from_user
__strncpy_from_user:
	/* %o0=dest, %o1=src, %o2=count */
	brlez,pn %o2, 3f
	 add	%o1, %o2, %o1
	sub	%g0, %o2, %o3
	add	%o0, %o2, %o0
10:
	lduba	[%o1 + %o3] ASI_S, %o4
1:
	brz,pn	%o4, 2f
	 stb	%o4, [%o0 + %o3]
	addcc	%o3, 1, %o3
	bne,pt	%xcc, 1b
11:
	 lduba	[%o1 + %o3] ASI_S, %o4
	retl
	 mov	%o2, %o0
2:
	add	%o3, 1, %o3
	retl
	 add	%o2, %o3, %o0
3:
	retl
	 clr	%o0

	.section .fixup,#alloc,#execinstr
	.align	4
4:
	retl
	 mov	-EFAULT, %o0

	.section __ex_table,#alloc
	.align	4
	.word	10b, 4b
	.word	11b, 4b
