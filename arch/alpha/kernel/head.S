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
#include <linux/fd.h>

#define halt .long PAL_halt

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
	.long PAL_wrent
	ret ($26)
	.end wrent

	.align 3
	.globl	wrkgp
	.ent	wrkgp
wrkgp:
	.long PAL_wrkgp
	ret ($26)
	.end wrkgp

	.align 3
	.globl	wrusp
	.ent	wrusp
wrusp:
	.long PAL_wrusp
	ret ($26)
	.end wrusp

	.align 3
	.globl	rdusp
	.ent	rdusp
rdusp:
	.long PAL_rdusp
	ret ($26)
	.end rdusp

	.align 3
	.globl	tbi
	.ent	tbi
tbi:
	.long PAL_tbi
	ret ($26)
	.end tbi

	.align 3
	.globl	imb
	.ent	imb
imb:
	.long PAL_imb
	ret ($26)
	.end imb

	.align 3
	.globl	rdmces
	.ent	rdmces
rdmces:
	call_pal PAL_rdmces
	ret ($26)
	.end rdmces

	.align 3
	.globl	wrmces
	.ent	wrmces
wrmces:
	call_pal PAL_wrmces
	ret ($26)
	.end wrmces
