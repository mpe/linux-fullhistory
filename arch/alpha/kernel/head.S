/*
 * alpha/boot/head.S
 *
 * initial boot stuff.. At this point, the bootloader has already
 * switched into OSF/1 PAL-code, and loaded us at the correct address
 * (START_ADDR).  So there isn't much left for us to do: just set up
 * the kernel global pointer and jump to the kernel entry-point.
 */

#include <asm/system.h>

.globl swapper_pg_dir
.globl _stext
swapper_pg_dir=SWAPPER_PGD

	.set noreorder
	.globl	__start
	.ent	__start
_stext:
__start:
	.prologue 0
	br	$27,1f
1:	ldgp	$29,0($27)
	/* We need to get current loaded up with our first task...  */
	lda	$8,init_task_union
	/* ... and find our stack ... */
	lda	$30,0x4000($8)
	/* ... and then we can start the kernel.  */
	jsr	$26,start_kernel
	call_pal PAL_halt
	.end __start

#ifdef __SMP__
	.align 3
	.globl	__start_cpu
	.ent	__start_cpu
	/* On entry here from SRM console, the HWPCB of this processor
	   has been loaded, and $27 contains the task pointer */
__start_cpu:
	.prologue 0
	/* First order of business, load the GP */
	br	$26,1f
1:	ldgp	$29,0($26)
	/* We need to get current loaded up with our first task...  */
	mov	$27,$8
	/* Set FEN */
	lda	$16,1($31)
	call_pal PAL_wrfen
	/* ... and then we can start the processor.  */
	jsr	$26,start_secondary
	call_pal PAL_halt
	.end __start_cpu
#endif /* __SMP__ */

	.align 3
	.globl	wrent
	.ent	wrent
wrent:
	.prologue 0
	call_pal PAL_wrent
	ret	($26)
	.end wrent

	.align 3
	.globl	wrkgp
	.ent	wrkgp
wrkgp:
	.prologue 0
	call_pal PAL_wrkgp
	ret	($26)
	.end wrkgp

	.align 3
	.globl	wrusp
	.ent	wrusp
wrusp:
	.prologue 0
	call_pal PAL_wrusp
	ret	($26)
	.end wrusp

	.align 3
	.globl	rdusp
	.ent	rdusp
rdusp:
	.prologue 0
	call_pal PAL_rdusp
	ret	($26)
	.end rdusp

	.align 3
	.globl	rdmces
	.ent	rdmces
rdmces:
	.prologue 0
	call_pal PAL_rdmces
	ret	($26)
	.end rdmces

	.align 3
	.globl	wrmces
	.ent	wrmces
wrmces:
	.prologue 0
	call_pal PAL_wrmces
	ret	($26)
	.end wrmces

	.align 3
	.globl  whami
	.ent    whami
whami:
	.prologue 0
	call_pal PAL_whami
	ret     ($26)
	.end whami
 
	.align 3
	.globl  wripir
	.ent    wripir
wripir:
	.prologue 0
	call_pal PAL_wripir
	ret     ($26)
	.end wripir

	.align 3
	.globl wrvptptr
	.ent wrvptptr
wrvptptr:
	.prologue 0
	call_pal PAL_wrvptptr
	ret	($26)
	.end wrvptptr

	#
	# The following two functions are needed for supporting SRM PALcode
	# on the PC164 (at least), since that PALcode manages the interrupt
	# masking, and we cannot duplicate the effort without causing problems
	#

	.align 3
	.globl	cserve_ena
	.ent	cserve_ena
cserve_ena:
	.prologue 0
	bis	$16,$16,$17
	lda	$16,52($31)
	call_pal PAL_cserve
	ret	($26)
	.end	cserve_ena

	.align 3
	.globl	cserve_dis
	.ent	cserve_dis
cserve_dis:
	.prologue 0
	bis	$16,$16,$17
	lda	$16,53($31)
	call_pal PAL_cserve
	ret	($26)
	.end	cserve_dis

	#
	# It is handy, on occasion, to make halt actually just loop. 
	# Putting it here means we dont have to recompile the whole
	# kernel.
	#

	.align 3
	.globl	halt
	.ent	halt
halt:
	.prologue 0
	call_pal PAL_halt
	.end	halt
