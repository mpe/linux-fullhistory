#include "ppc_defs.h"

	.text
/*
 * This code may be executed by a bootstrap process.  If so, the
 * purpose is to relocate the loaded image to it's final location
 * in memory.
 *    R3: End of image
 *    R4: Start of image - 0x400
 *
 */
	.globl	start
start:
	addi	r4,r4,0x400	/* Point at start of image */
	lis	r5,start@h	/* Load address */
	ori	r5,r5,start@l
	subi	r4,r4,4		/* Adjust for auto-increment */
	subi	r5,r5,4
	subi	r3,r3,4
00:	lwzu	r0,4(r4)	/* Fast move */
	stwu	r0,4(r5)
	cmp	0,r3,r4
	bne	00b
	lis	r5,continue@h	/* Actual code starts here */
	ori	r5,r5,continue@l
	mtlr	r5
	blr

continue:
	bl	decompress_kernel
	li	r5,0x100	/* Kernel code starts here */
	mtlr	r5
	blr	
