/* $Id: irqlock.S,v 1.4 1997/05/01 02:26:54 davem Exp $
 * irqlock.S: High performance IRQ global locking and interrupt entry.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/psr.h>
#include <asm/smp.h>

	.text
	.align	4

	/* This is incredibly insane... */
	.globl	___irq_enter
___irq_enter:
	sethi	%hi(local_irq_count), %g2
	sll	%g1, 2, %g1
	or	%g2, %lo(local_irq_count), %g2
	ld	[%g2 + %g1], %g3
	sethi	%hi(global_irq_count), %g5
	add	%g3, 1, %g3
	or	%g5, %lo(global_irq_count), %g5
	st	%g3, [%g2 + %g1]
1:
	ldstub	[%g5 + 3], %g2
	orcc	%g2, 0x0, %g0
	bne	1b
	 ld	[%g5], %g3
	sra	%g3, 8, %g3
	add	%g3, 1, %g3
	sll	%g3, 8, %g3
	st	%g3, [%g5]
	sethi	%hi(global_irq_lock), %g1
	ldub	[%g1 + %lo(global_irq_lock)], %g2
1:	
	orcc	%g2, 0x0, %g0
	bne,a	1b
	 ldub	[%g1 + %lo(global_irq_lock)], %g2
___irq_enter_out:
	jmpl	%o7, %g0
	 mov	%g4, %o7

	.globl	___irq_exit
___irq_exit:
	rd	%psr, %g3
	sethi	%hi(global_irq_count), %g1
	or	%g3, PSR_PIL, %g3
	or	%g1, %lo(global_irq_count), %g1
	wr	%g3, 0x0, %psr
	sethi	%hi(local_irq_count), %g2
	sll	%g7, 2, %g7
	or	%g2, %lo(local_irq_count), %g2
	ld	[%g2 + %g7], %g3
1:	
	ldstub	[%g1 + 3], %g5
	orcc	%g5, 0x0, %g0
	bne	1b
	 ld	[%g1], %g5
	sra	%g5, 8, %g5
	sub	%g5, 1, %g5
	sll	%g5, 8, %g5
	st	%g5, [%g1]
	sub	%g3, 1, %g3
	sethi	%hi(global_irq_holder), %g1
	st	%g3, [%g2 + %g7]
	srl	%g7, 2, %g7
	ldub	[%g1 + %lo(global_irq_holder)], %g5
	cmp	%g5, %g7
	bne	___irq_enter_out
	 mov	NO_PROC_ID, %g2
	stb	%g2, [%g1 + %lo(global_irq_holder)]
	sethi	%hi(global_irq_lock), %g5
	b	___irq_enter_out
	 stb	%g0, [%g5 + %lo(global_irq_lock)]

	/* Weird calling conventions... %g7=flags, %g4=%prev_o7
	 * Very clever for the __global_sti case, the inline which
	 * gets us here clears %g7 and it just works.
	 */
	.globl	___global_restore_flags, ___global_sti, ___global_cli
___global_restore_flags:
	bne,a	___global_cli
	 rd	%tbr, %g7
	rd	%tbr, %g2

___global_sti:
	sethi	%hi(global_irq_holder), %g1
	sethi	%hi(global_irq_lock), %g3
	srl	%g2, 12, %g2
	ldub	[%g1 + %lo(global_irq_holder)], %g5
	and	%g2, 3, %g2
	cmp	%g5, %g2
	bne	1f
	 mov	NO_PROC_ID, %g5
	stb	%g5, [%g1 + %lo(global_irq_holder)]
	stb	%g0, [%g3 + %lo(global_irq_lock)]
1:
	rd	%psr, %g3
	andcc	%g7, 2, %g0
	bne,a	1f
	 or	%g3, PSR_PIL, %g3
	andn	%g3, PSR_PIL, %g3
1:
	wr	%g3, 0x0, %psr
	nop
__global_cli_out:			! All togther now... "fuuunnnnn"
	retl
	 mov	%g4, %o7

__spin_on_global_irq_lock:
	orcc	%g2, 0x0, %g0
	bne,a	__spin_on_global_irq_lock
	 ldub	[%g1], %g2
	b,a	1f

	/* This is a royal pain in the ass to make fast... 8-( */
___global_cli:
	sethi	%hi(global_irq_lock), %g5
	srl	%g7, 12, %g7
	sethi	%hi(global_irq_holder), %g3
	and	%g7, 3, %g7
	ldub	[%g3 + %lo(global_irq_holder)], %g1
	rd	%psr, %g2
	cmp	%g1, %g7
	or	%g2, PSR_PIL, %g2
	be	__global_cli_out
	 wr	%g2, 0x0, %psr			! XXX some sparcs may choke on this...
	sethi	%hi(local_irq_count), %g3
	or	%g3, %lo(local_irq_count), %g3
	or	%g5, %lo(global_irq_lock), %g1
1:
	ldstub	[%g1], %g2
	orcc	%g2, 0x0, %g0
	bne,a	__spin_on_global_irq_lock
	 ldub	[%g1], %g2
__wait_on_irq:
	sll	%g7, 2, %g7
	ld	[%g3 + %g7], %g2
	sethi	%hi(global_irq_count), %g1
	or	%g1, %lo(global_irq_count), %g1
	srl	%g7, 2, %g7
	ld	[%g1], %g5
	sra	%g5, 8, %g5
__wait_on_irq_loop:
	cmp	%g5, %g2
	sethi	%hi(global_irq_holder), %g3
	be,a	__global_cli_out	! Mamamia, Mamamia, this is the fast path
	 stb	%g7, [%g3 + %lo(global_irq_holder)]
1:
	ldstub	[%g1 + 3], %g3
	orcc	%g3, 0x0, %g0
	bne	1b
	 ld	[%g1], %g3
	sra	%g3, 8, %g3
	sub	%g3, %g2, %g3
	sll	%g3, 8, %g3
	st	%g3, [%g1]
	sethi	%hi(global_irq_lock), %g3
	stb	%g0, [%g3 + %lo(global_irq_lock)]
0:
	ld	[%g1], %g5
9:
	ldub	[%g3 + %lo(global_irq_lock)], %g3
	sra	%g5, 8, %g5
	orcc	%g3, %g5, %g0
	bne	0b
	 sethi	%hi(global_irq_lock), %g3
	ldstub	[%g3 + %lo(global_irq_lock)], %g5
	orcc	%g5, 0x0, %g0
	bne,a	9b
	 ld	[%g1], %g5
1:
	ldstub	[%g1 + 3], %g3
	orcc	%g3, 0x0, %g0
	bne	1b
	 ld	[%g1], %g3
	sra	%g3, 8, %g3
	add	%g3, %g2, %g5
	sll	%g5, 8, %g3
	b	__wait_on_irq_loop
	 st	%g3, [%g1]

#if 0 /* XXX I'm not delirious enough to debug this yet. */
	 add	%o7, (8 + (__wait_on_irq_loop - . - 4)), %o7	! AIEEEEE
#endif
