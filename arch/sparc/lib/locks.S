/* $Id: locks.S,v 1.15 1998/10/14 09:18:55 jj Exp $
 * locks.S: SMP low-level lock primitives on Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Anton Blanchard (anton@progsoc.uts.edu.au)
 * Copyright (C) 1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#include <asm/cprefix.h>
#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/smp.h>
#include <asm/spinlock.h>

	.text
	.align	4

	/* This is called when the initial acquisition attempt of a spin
	 * lock fails.  The calling convention is weird, return address
	 * is in %o7 as usual but we agree with the caller to only touch
	 * and use %g2 as a temporary.  We are passed a ptr to the lock
	 * itself in %g1, %g4 must be restored into %o7 when we return,
	 * and the caller wants us to return to him at three instructions
	 * previous to the call instruction which got us here.  See how
	 * this is used in asm/spinlock.h if what I just said confuses
	 * you to no end.
	 */
	.globl	___spinlock_waitfor
___spinlock_waitfor:
1:	orcc	%g2, 0x0, %g0
	bne,a	1b
	 ldub	[%g1], %g2
	ldstub	[%g1], %g2
	jmpl	%o7 - 12, %g0
	 mov	%g4, %o7

	/* Read/writer locks, as usual this is overly clever to make it
	 * as fast as possible.
	 */

	/* caches... */
___rw_read_enter_spin_on_wlock:
	orcc	%g2, 0x0, %g0
	be,a	___rw_read_enter
	 ldstub	[%g1 + 3], %g2
	b	___rw_read_enter_spin_on_wlock
	 ldub	[%g1 + 3], %g2
___rw_read_exit_spin_on_wlock:
	orcc	%g2, 0x0, %g0
	be,a	___rw_read_exit
	 ldstub	[%g1 + 3], %g2
	b	___rw_read_exit_spin_on_wlock
	 ldub	[%g1 + 3], %g2
___rw_write_enter_spin_on_wlock:
	orcc	%g2, 0x0, %g0
	be,a	___rw_write_enter
	 ldstub	[%g1 + 3], %g2
	b	___rw_write_enter_spin_on_wlock
	 ld	[%g1], %g2

	.globl	___rw_read_enter
___rw_read_enter:
	orcc	%g2, 0x0, %g0
	bne,a	___rw_read_enter_spin_on_wlock
	 ldub	[%g1 + 3], %g2
	ld	[%g1], %g2
	add	%g2, 1, %g2
	st	%g2, [%g1]
	retl
	 mov	%g4, %o7

	.globl	___rw_read_exit
___rw_read_exit:
	orcc	%g2, 0x0, %g0
	bne,a	___rw_read_exit_spin_on_wlock
	 ldub	[%g1 + 3], %g2
	ld	[%g1], %g2
	sub	%g2, 0x1ff, %g2
	st	%g2, [%g1]
	retl
	 mov	%g4, %o7

	.globl	___rw_write_enter
___rw_write_enter:
	orcc	%g2, 0x0, %g0
	bne	___rw_write_enter_spin_on_wlock
	 ld	[%g1], %g2
	andncc	%g2, 0xff, %g0
	bne,a	___rw_write_enter_spin_on_wlock
	 stb	%g0, [%g1 + 3]
	retl
	 mov	%g4, %o7
