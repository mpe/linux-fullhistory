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
swapper_pg_dir=SWAPPER_PGD

	.set noreorder
	.globl	__start
	.ent	__start
__start:
	br	$27,1f
1:	ldgp	$29,0($27)
	lda	$27,start_kernel
	jsr	$26,($27),start_kernel
	halt
	.end __start

	.align 5
	.globl	wrent
	.ent	wrent
wrent:
	.long PAL_wrent
	ret ($26)
	.end wrent

	.align 5
	.globl	wrkgp
	.ent	wrkgp
wrkgp:
	.long PAL_wrkgp
	ret ($26)
	.end wrkgp

	.align 5
	.globl	wrusp
	.ent	wrusp
wrusp:
	.long PAL_wrusp
	ret ($26)
	.end wrusp

	.align 5
	.globl	rdusp
	.ent	rdusp
rdusp:
	.long PAL_rdusp
	ret ($26)
	.end rdusp

.align 9
.globl floppy_track_buffer
floppy_track_buffer:
	.space 512*2*MAX_BUFFER_SECTORS,1
