/* strncpy_from_user.S: Sparc strncpy from userspace.
 *
 *  Copyright(C) 1996 David S. Miller
 */

#include <asm/cprefix.h>
#include <asm/ptrace.h>

	.text
	.align	4

	/* Must return:
	 *
	 * -EFAULT		for an exception
	 * count		if we hit the buffer limit
	 * bytes copied		if we hit a null byte
	 */

	.globl	C_LABEL(__strncpy_from_user)
C_LABEL(__strncpy_from_user):
	/* %o0=dest, %o1=src, %o2=count */
	ld	[%g6 + THREAD_EX_COUNT], %g1
	set	strncpy_user_failure, %g2
	add	%g1, 1, %g3
	st	%o7, [%g6 + THREAD_EX_PC]
	st	%g3, [%g6 + THREAD_EX_COUNT]
	st	%g2, [%g6 + THREAD_EX_EXPC]

	mov	%o2, %o3
1:
	subcc	%o2, 1, %o2
	bneg	2f
	 nop

	ldub	[%o1], %o4
	add	%o0, 1, %o0
	cmp	%o4, 0
	add	%o1, 1, %o1
	bne	1b
	 stb	%o4, [%o0 - 1]
2:
	add	%o2, 1, %o0
	st	%g1, [%g6 + THREAD_EX_COUNT]
	retl
	 sub	%o3, %o0, %o0

strncpy_user_failure:
	jmpl	%g3 + 0x8, %g0
	 mov	%g1, %o0
