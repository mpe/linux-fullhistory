/* $Id: bare.S,v 1.2 1995/11/25 00:57:39 davem Exp $
 * base.S:      Ugly low-level boot program entry code.  The job of this
 *              module is to parse the boot flags, try to mount the remote
 *              root filesystem and load the kernel into virtual memory.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include "bare.h"
#include <asm/ptrace.h>

	.data
	.globl C_LABEL(romvec)
	.globl C_LABEL(idp_ptr)

C_LABEL(romvec):
	.word 0
C_LABEL(idp_ptr):
	.word 0

	.text
	.align 8
	.globl C_LABEL(first_adr_in_text)

C_LABEL(first_adr_in_text):

	/* Grrr, boot block, scratching my head... */
	.globl C_LABEL(b_block)       /* Start of actual boot block */
	.globl C_LABEL(b_block_size)  /* In bytes */
	.globl C_LABEL(b_block_cksum) /* Checksum of boot block bytes */

	b	start_of_execution    /* XXX Hack */
	nop

	.align	8
C_LABEL(b_block):	
	.skip	(BOOTBLOCK_NENTRIES * BOOTBLOCK_ENTSIZE)

C_LABEL(b_block_size):
	.word	0

C_LABEL(b_block_cksum):
	.word	0

/* Ok, the prom has left in %o0 the PROM pointer.  We leave it here
 * for when we jump into the kernel.  So save out of this window before
 * you dick with %o0.  As far as I know we could be loaded *anywhere*, so
 * we relocate ourselves to the "linked" location.  Self modifying code rules.
 */

start_of_execution:
	sethi	%hi(C_LABEL(first_adr_in_text)), %o1		! This is our top
	or	%o1, %lo(C_LABEL(first_adr_in_text)), %o1	! of stack too.
	sub	%o1, STACKFRAME_SZ, %o1
	add	%o1, 0x7, %o1
	andn	%o1, 0x7, %o1
	save	%o1, 0x0, %sp					! save is an add
here:
	call	there
	sethi	%hi(here), %o4
there:	
	sub	%o7, here-C_LABEL(first_adr_in_text), %o5
	or	%o4, %lo(here), %o4
	cmp	%o4, %o7
	be	loaded_ok
	nop

	/* Gotta relocate, compute our size sans bss segment. */
	set	C_LABEL(edata)+4, %o3
	set	C_LABEL(first_adr_in_text), %o2
	sub	%o3, %o2, %o3
rel_loop:
	ld	[%o5], %o4
	add	%o5, 0x4, %o5
	st	%o4, [%o2]
	subcc	%o3, 0x4, %o3
	bg	rel_loop
	add	%o2, 0x4, %o2

	/* Pray that we are now in a sane place in memory */
	sethi	%hi(loaded_ok), %o2
	or	%o2, %lo(loaded_ok), %o2
	jmp	%o2
	nop

loaded_ok:
	/* Save the PROM pointer */
	sethi	%hi(C_LABEL(romvec)), %o1
	or	%o1, %lo(C_LABEL(romvec)), %o1
	st	%i0, [%o1]

	/* Build a PSR we can live with */
	rd	%psr, %o1

#if 0
	andn	%o1, PSR_PIL, %o1
	sethi	%hi(SANE_PSR), %g4
	or	%g4, %lo(SANE_PSR), %g4
	or	%o1, %g4, %o1
#endif

	/* V8 book says this works to calculate num_windows */
	sethi	%hi(0xffffffff), %g2
	rd	%wim, %g3
	or	%g2, %lo(0xffffffff), %g2
	wr	%g2, 0x0, %wim
	WRITE_PAUSE

	rd	%wim, %g4
	WRITE_PAUSE

	wr	%g3, 0x0, %wim
	WRITE_PAUSE

	/* Restore old %psr */
	wr	%o1, 0x0, %psr
	WRITE_PAUSE

	or	%g0, 0x0, %g3
1:
	srl	%g4, 0x1, %g4
	subcc	%g4, 0x0, %g0
	bne	1b
	add	%g3, 0x1, %g3
	
	/* %g3 now contains nwindows */
	sethi	%hi(C_LABEL(nwindows)), %o4
	st	%g3, [%o4 + %lo(C_LABEL(nwindows))]

	/* Now zero out our bss segment, lord knows the nasty prom monster
	 * didn't do it for us.
	 */
	sethi	%hi(C_LABEL(end)), %g1
	or	%g1, %lo(C_LABEL(end)), %g1
	add	%g1, 0x4, %g1
	sethi	%hi(C_LABEL(edata)), %g2
	or	%g2, %lo(C_LABEL(edata)), %g2

	/* Slow, inefficient, who cares, this is messy boot code */
bzero_bss_loop:
	st	%g0, [%g2]
	add	%g2, 0x4, %g2
	cmp	%g2, %g1
	bl	bzero_bss_loop
	nop

	call	C_LABEL(init_me)	! Fun with imperical constants and prom
	nop

	/* Dump back into the prom */
get_me_out_of_here:
	set	C_LABEL(romvec), %g2
	ld	[%g2], %g2
	ld	[%g2 + 0x74], %g2
	restore
	call	%g2
	nop



