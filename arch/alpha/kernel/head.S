/*
 * alpha/boot/head.S
 *
 * initial boot stuff.. At this point, the bootloader has already
 * switched into OSF/1 PAL-code, and loaded us at the correct address
 * (START_ADDR).  So there isn't much left for us to do: just set up
 * the kernel global pointer and jump to the kernel entry-point.
 */

#define __ASSEMBLY__
#include <asm/system.h>

#define halt call_pal PAL_halt

.globl swapper_pg_dir
.globl _stext
swapper_pg_dir=SWAPPER_PGD

	.set noreorder
	.globl	__start
	.ent	__start
_stext:
__start:
	br	$27,1f
1:	ldgp	$29,0($27)
	/* We need to get current loaded up with our first task...  */
	lda	$8,init_task_union
	/* ... and find our stack ... */
	lda	$30,0x4000($8)
	/* ... and then we can start the kernel.  */
	jsr	$26,start_kernel
	halt
	.end __start

	.align 3
	.globl	wrent
	.ent	wrent
wrent:
	call_pal PAL_wrent
	ret	($26)
	.end wrent

	.align 3
	.globl	wrkgp
	.ent	wrkgp
wrkgp:
	call_pal PAL_wrkgp
	ret	($26)
	.end wrkgp

	.align 3
	.globl	wrusp
	.ent	wrusp
wrusp:
	call_pal PAL_wrusp
	ret	($26)
	.end wrusp

	.align 3
	.globl	rdusp
	.ent	rdusp
rdusp:
	call_pal PAL_rdusp
	ret	($26)
	.end rdusp

	.align 3
	.globl	rdmces
	.ent	rdmces
rdmces:
	call_pal PAL_rdmces
	ret	($26)
	.end rdmces

	.align 3
	.globl	wrmces
	.ent	wrmces
wrmces:
	call_pal PAL_wrmces
	ret	($26)
	.end wrmces

	.align 3
	.globl  whami
	.ent    whami
whami:
	call_pal PAL_whami
	ret     ($26)
	.end whami
 
	.align 3
	.globl  wripir
	.ent    wripir
wripir:
	call_pal PAL_wripir
	ret     ($26)
	.end wripir

	#
	# The following two functions are needed for supporting SRM PALcode
	# on the PC164 (at least), since that PALcode manages the interrupt
	# masking, and we cannot duplicate the effort without causing problems
	#

	.align 3
	.globl	cserve_ena
	.ent	cserve_ena
cserve_ena:
	bis	$16,$16,$17
	lda	$16,52($31)
	call_pal PAL_cserve
	ret	($26)
	.end	cserve_ena

	.align 3
	.globl	cserve_dis
	.ent	cserve_dis
cserve_dis:
	bis	$16,$16,$17
	lda	$16,53($31)
	call_pal PAL_cserve
	ret	($26)
	.end	cserve_dis

	#
	# The following two functions don't need trapb/excb instructions
	# around the mf_fpcr/mt_fpcr instructions because (a) the kernel
	# never generates arithmetic faults and (b) call_pal instructions
	# are implied trap barriers.
	#
	.align 3
	.globl	rdfpcr
	.ent	rdfpcr
rdfpcr:
	lda	$30,-0x10($30)
	stt	$f0,0($30)
	mf_fpcr	$f0
	stt	$f0,8($30)
	ldt	$f0,0($30)
	ldq	$0,8($30)
	lda	$30,0x10($30)
	ret	($26)
	.end	rdfpcr

	.align 3
	.globl	wrfpcr
	.ent	wrfpcr
wrfpcr:
	lda	$30,-0x10($30)
	stt	$f0,0($30)
	stq	$16,8($30)
	ldt	$f0,8($30)
	mt_fpcr	$f0
	ldt	$f0,0($30)
	lda	$30,0x10($30)
	ret	($26)
	.end	wrfpcr
