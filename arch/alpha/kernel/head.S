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
	lda	$27,start_kernel
	jsr	$26,($27),start_kernel
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
	.globl	tbi
	.ent	tbi
tbi:
	call_pal PAL_tbi
	ret	($26)
	.end tbi

	.align 3
	.globl	imb
	.ent	imb
imb:
	call_pal PAL_imb
	ret	($26)
	.end imb

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

#define ex_count 0($16)
#define ex_r9	 8($16)
#define ex_r10	16($16)
#define ex_r11	24($16)
#define ex_r12	32($16)
#define ex_r13	40($16)
#define ex_r14	48($16)
#define ex_r15	56($16)
#define ex_r26	64($16)
#define ex_r30	72($16)

	.align 3
	.globl	__exception
	.ent	__exception
__exception:
	ldq	$1,ex_count
	bis	$31,$31,$0	/* return 0 */
	addq	$1,1,$2
	bne	$1,1f		/* don't save state if orig_count != 0 */
	stq	$9,ex_r9
	stq	$10,ex_r10
	stq	$11,ex_r11
	stq	$12,ex_r12
	stq	$13,ex_r13
	stq	$14,ex_r14
	stq	$15,ex_r15
	stq	$26,ex_r26
	stq	$30,ex_r30
1:	stq	$2,ex_count
	ret	($26)
	.end	__exception

	.align 3
	.globl	__handle_exception
	.ent	__handle_exception
__handle_exception:
	ldq	$9,ex_r9
	ldq	$10,ex_r10
	ldq	$11,ex_r11
	ldq	$12,ex_r12
	ldq	$13,ex_r13
	ldq	$14,ex_r14
	ldq	$15,ex_r15
	ldq	$26,ex_r26
	ldq	$30,ex_r30
	bis	$31,1,$0	/* return 1 */
	ret	($26)
	.end	__handle_exception
