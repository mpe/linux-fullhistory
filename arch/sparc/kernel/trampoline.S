/* $Id: trampoline.S,v 1.3 1996/04/03 02:15:05 davem Exp $
 * mp.S:  Multiprocessor low-level routines on the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/head.h>
#include <asm/psr.h>
#include <asm/asi.h>
#include <asm/ptrace.h>
#include <asm/vaddrs.h>
#include <asm/contregs.h>


	.text
	.align 4

/* When we start up a cpu for the first time it enters this routine.
 * This initializes the chip from whatever state the prom left it
 * in and sets PIL in %psr to 15, no irqs.
 */

	.globl C_LABEL(sparc_cpu_startup)
C_LABEL(sparc_cpu_startup):
cpu1_startup:
	sethi	%hi(C_LABEL(trapbase_cpu1)), %g7
	or	%g7, %lo(C_LABEL(trapbase_cpu1)), %g7
	sethi	%hi(C_LABEL(cpu1_stack)), %g6
	or	%g6, %lo(C_LABEL(cpu1_stack)), %g6
	b	1f
	 nop	

cpu2_startup:
	sethi	%hi(C_LABEL(trapbase_cpu2)), %g7
	or	%g7, %lo(C_LABEL(trapbase_cpu2)), %g7
	sethi	%hi(C_LABEL(cpu2_stack)), %g6
	or	%g6, %lo(C_LABEL(cpu2_stack)), %g6
	b	1f
	 nop	

cpu3_startup:
	sethi	%hi(C_LABEL(trapbase_cpu3)), %g7
	or	%g7, %lo(C_LABEL(trapbase_cpu3)), %g7
	sethi	%hi(C_LABEL(cpu3_stack)), %g6
	or	%g6, %lo(C_LABEL(cpu3_stack)), %g6
	b	1f
	 nop	

1:
	/* Set up a sane %psr -- PIL<0xf> S<0x1> PS<0x1> CWP<0x0> */
	set	(PSR_PIL | PSR_S | PSR_PS), %g1
	wr	%g1, 0x0, %psr		! traps off though
	WRITE_PAUSE

	/* Our %wim is one behind CWP */
	mov	2, %g1
	wr	%g1, 0x0, %wim
	WRITE_PAUSE

	/* This identifies "this cpu". */
	wr	%g7, 0x0, %tbr
	WRITE_PAUSE

	/* Give ourselves a stack. */
	set	0x2000, %g5
	add	%g6, %g5, %g6		! end of stack
	sub	%g6, REGWIN_SZ, %sp
	mov	0, %fp

	/* Turn on traps (PSR_ET). */
	rd	%psr, %g1
	wr	%g1, PSR_ET, %psr	! traps on
	WRITE_PAUSE

	/* Init our caches, etc. */
	set	C_LABEL(poke_srmmu), %g5
	ld	[%g5], %g5
	call	%g5
	 nop

	/* Start this processor. */
	call	C_LABEL(smp_callin)
	 nop

	call	C_LABEL(cpu_idle)
	 mov	0, %o0

	call	C_LABEL(cpu_panic)
	 nop
