/* $Id: locks.S,v 1.2 1997/03/10 12:28:02 jj Exp $
 * locks.S: SMP low-level lock primitives on Sparc64.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/ptrace.h>

	.text
	.align	4

	.globl	__spinlock_waitfor
__spinlock_waitfor:
1:	orcc	%g2, 0x0, %g0
	bne	1b
	 ldub	[%g1], %g2
	ldstub	[%g1], %g2
	jmpl	%o7 - 12, %g0
	 mov	%g5, %o7

	.globl	___become_idt
___become_idt:
#if 0 /* Don't know how to do this on the Ultra yet... */
#endif
	jmpl	%o7 + 8, %g0
	 mov	%g5, %o7

___lk_busy_spin:
	orcc	%g2, 0, %g0
	bne	___lk_busy_spin
	 ldub	[%g1 + 0], %g2
	b	1f
	 ldstub	[%g1 + 0], %g2

	.globl	___lock_kernel
___lock_kernel:
	addcc	%g2, -1, %g2
	rdpr	%pil, %g3
	bcs,a	9f
	 st	%g2, [%g6 + AOFF_task_lock_depth]
	wrpr	15, %pil
	ldstub	[%g1 + 0], %g2
1:	orcc	%g2, 0, %g0
	bne,a	___lk_busy_spin
	 ldub	[%g1 + 0], %g2
	ldub	[%g1 + 2], %g2
	cmp	%g2, %g5
	be	2f
	 stb	%g5, [%g1 + 1]
	stb	%g5, [%g1 + 2]
#ifdef __SMP__
	/* XXX Figure out how to become interrupt receiver in SMP system. */
#endif
2:	mov	-1, %g2
	st	%g2, [%g6 + AOFF_task_lock_depth]
	wrpr	%g3, %pil
9:	jmpl	%o7 + 0x8, %g0
	 mov	%g5, %o7

#undef NO_PROC_ID
#define NO_PROC_ID	0xff

	.globl	___unlock_kernel
___unlock_kernel:
	addcc	%g2, 1, %g2
	rdpr	%pil, %g3
	bne,a	1f
	 st	%g2, [%g6 + AOFF_task_lock_depth]
	wrpr	15, %pil
	mov	NO_PROC_ID, %g2
	stb	%g2, [%g1 + 1]
	stb	%g0, [%g1 + 0]
	st	%g0, [%g6 + AOFF_task_lock_depth]
	wrpr	%g3, %pil
1:	jmpl	%o7 + 0x8, %g0
	 mov	%g5, %o7
	
