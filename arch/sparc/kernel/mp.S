/* mp.S:  Multiprocessor low-level routines on the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/head.h>
#include <asm/psr.h>
#include <asm/asi.h>
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
	/* Set up a sane %psr -- PIL<0xf> S<0x1> PS<0x1> CWP<0x0> */
	set	(PSR_PIL | PSR_S | PSR_PS), %g1
	wr	%g1, 0x0, %psr		! traps off though
	WRITE_PAUSE

	/* Our %wim is one behind CWP */
	wr	%g0, 0x2, %wim

	rd	%tbr, %g4
	or	%g0, 0x3, %g5
	sll	%g5, 20, %g5
	and	%g4, %g5, %g4		! Mask cpu-id bits

	/* Give ourselves a stack. */
	set	PERCPU_VADDR, %g1
	add	%g1, %g4, %g1
	set	PERCPU_KSTACK_OFFSET, %g5
	add	%g1, %g5, %g1
	set	0x1000, %g5
	add	%g1, %g5, %g1		! end of stack
	sub	%g1, (96+96+80), %g1	! set up a frame
	andn	%g1, 0x7, %g1
	or	%g1, 0x0, %fp		! bottom of frame
	add	%fp, (96+80), %sp	! top of frame

	/* Set up per-cpu trap table pointer.  In actuality, the virtual
	 * address for the trap table on every cpu points to the same
	 * physical address, this virtual address is only used for cpu
	 * identification purposes.
	 */
#if 0
/*	set	PERCPU_VADDR, %g1 */
/*	add	%g1, %g4, %g1 */
/*	add	%g1, PERCPU_TBR_OFFSET, %g1 */
	set	C_LABEL(thiscpus_tbr), %g1
	ld	[%g1], %g1
	wr	%g1, 0x0, %tbr
	WRITE_PAUSE
#else
	set	C_LABEL(trapbase), %g3
	wr	%g3, 0x0, %tbr
	WRITE_PAUSE
#endif

	/* Turn on traps (PSR_ET). */
	rd	%psr, %g1
	wr	%g1, PSR_ET, %psr	! traps on

#if 0
1:	nop
	b	1b
	nop
#endif

	/* Call C-code to do the rest of the real work. */
	call	C_LABEL(sparc_cpu_init)
	nop

	/* Call cpu-idle routine so we can start it up later on. */
	call	C_LABEL(sparc_cpu_idle)
	nop

	/* Done... This cpu should me spinning in a test loop.
	 * If execution gets here, something really bad happened.
	 */
	call	C_LABEL(prom_halt)	! Seems reasonable...
	nop

