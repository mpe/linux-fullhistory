/*
 * String handling functions for PowerPC.
 *
 * Copyright (C) 1996 Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include "../kernel/ppc_asm.tmpl"
#include <asm/processor.h>
#include <asm/errno.h>

	.globl	strcpy
strcpy:
	addi	r5,r3,-1
	addi	r4,r4,-1
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	stbu	r0,1(r5)
	bne	1b
	blr

	.globl	strncpy
strncpy:
	cmpwi	0,r5,0
	beqlr
	mtctr	r5
	addi	r6,r3,-1
	addi	r4,r4,-1
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	stbu	r0,1(r6)
	bdnzf	2,1b		/* dec ctr, branch if ctr != 0 && !cr0.eq */
	blr

	.globl	strcat
strcat:
	addi	r5,r3,-1
	addi	r4,r4,-1
1:	lbzu	r0,1(r5)
	cmpwi	0,r0,0
	bne	1b
	addi	r5,r5,-1
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	stbu	r0,1(r5)
	bne	1b
	blr

	.globl	strcmp
strcmp:
	addi	r5,r3,-1
	addi	r4,r4,-1
1:	lbzu	r3,1(r5)
	cmpwi	1,r3,0
	lbzu	r0,1(r4)
	subf.	r3,r0,r3
	beqlr	1
	beq	1b
	blr

	.globl	strlen
strlen:
	addi	r4,r3,-1
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	bne	1b
	subf	r3,r3,r4
	blr

	.globl	memset
memset:
	rlwimi	r4,r4,8,16,23
	rlwimi	r4,r4,16,0,15
	addi	r6,r3,-4
	cmplwi	0,r5,4
	blt	7f
	stwu	r4,4(r6)
	beqlr
	andi.	r0,r6,3
	add	r5,r0,r5
	subf	r6,r0,r6
	rlwinm	r0,r5,32-2,2,31
	mtctr	r0
	bdz	6f
1:	stwu	r4,4(r6)
	bdnz	1b
6:	andi.	r5,r5,3
7:	cmpwi	0,r5,0
	beqlr
	mtctr	r5
	addi	r6,r6,3
8:	stbu	r4,1(r6)
	bdnz	8b
	blr

	.globl	bcopy
bcopy:
	mr	r6,r3
	mr	r3,r4
	mr	r4,r6
	b	memcpy

	.globl	memmove
memmove:
	cmplw	0,r3,r4
	bgt	backwards_memcpy
	/* fall through */

	.globl	memcpy
memcpy:
	rlwinm.	r7,r5,32-3,3,31		/* r0 = r5 >> 3 */
	addi	r6,r3,-4
	addi	r4,r4,-4
	beq	2f			/* if less than 8 bytes to do */
	andi.	r0,r6,3			/* get dest word aligned */
	mtctr	r7
	bne	5f
1:	lwz	r7,4(r4)
	lwzu	r8,8(r4)
	stw	r7,4(r6)
	stwu	r8,8(r6)
	bdnz	1b
	andi.	r5,r5,7
2:	cmplwi	0,r5,4
	blt	3f
	lwzu	r0,4(r4)
	addi	r5,r5,-4
	stwu	r0,4(r6)
3:	cmpwi	0,r5,0
	beqlr
	mtctr	r5
	addi	r4,r4,3
	addi	r6,r6,3
4:	lbzu	r0,1(r4)
	stbu	r0,1(r6)
	bdnz	4b
	blr
5:	subfic	r0,r0,4
	mtctr	r0
6:	lbz	r7,4(r4)
	addi	r4,r4,1
	stb	r7,4(r6)
	addi	r6,r6,1
	bdnz	6b
	subf	r5,r0,r5
	rlwinm.	r7,r5,32-3,3,31
	beq	2b
	mtctr	r7
	b	1b

	.globl	backwards_memcpy
backwards_memcpy:
	rlwinm.	r7,r5,32-3,3,31		/* r0 = r5 >> 3 */
	add	r6,r3,r5
	add	r4,r4,r5
	beq	2f
	andi.	r0,r6,3
	mtctr	r7
	bne	5f
1:	lwz	r7,-4(r4)
	lwzu	r8,-8(r4)
	stw	r7,-4(r6)
	stwu	r8,-8(r6)
	bdnz	1b
	andi.	r5,r5,7
2:	cmplwi	0,r5,4
	blt	3f
	lwzu	r0,-4(r4)
	subi	r5,r5,4
	stwu	r0,-4(r6)
3:	cmpwi	0,r5,0
	beqlr
	mtctr	r5
4:	lbzu	r0,-1(r4)
	stbu	r0,-1(r6)
	bdnz	4b
	blr
5:	mtctr	r0
6:	lbzu	r7,-1(r4)
	stbu	r7,-1(r6)
	bdnz	6b
	subf	r5,r0,r5
	rlwinm.	r7,r5,32-3,3,31
	beq	2b
	mtctr	r7
	b	1b
	
	.globl	memcmp
memcmp:
	cmpwi	0,r5,0
	ble-	2f
	mtctr	r5
	addi	r6,r3,-1
	addi	r4,r4,-1
1:	lbzu	r3,1(r6)
	lbzu	r0,1(r4)
	subf.	r3,r0,r3
	bdnzt	2,1b
	blr
2:	li	r3,0
	blr

	.global	memchr
memchr:
	cmpwi	0,r5,0
	ble-	2f
	mtctr	r5
	addi	r3,r3,-1
1:	lbzu	r0,1(r3)
	cmpw	0,r0,r4
	bdnzf	2,1b
	beqlr
2:	li	r3,0
	blr

	.globl	__copy_tofrom_user
__copy_tofrom_user:
	srwi.	r7,r5,3
	addi	r6,r3,-4
	addi	r4,r4,-4
	li	r3,0		/* success return value */
	beq	2f		/* if less than 8 bytes to do */
	andi.	r0,r6,3		/* get dest word aligned */
	mtctr	r7
	bne	5f
1:	lwz	r7,4(r4)
11:	lwzu	r8,8(r4)
12:	stw	r7,4(r6)
13:	stwu	r8,8(r6)
	bdnz	1b
	andi.	r5,r5,7
2:	cmplwi	0,r5,4
	blt	3f
14:	lwzu	r0,4(r4)
	addi	r5,r5,-4
15:	stwu	r0,4(r6)
3:	cmpwi	0,r5,0		/* do 1 byte at a time for the remainder */
	beqlr
	mtctr	r5
	addi	r4,r4,3
	addi	r6,r6,3
4:	lbzu	r0,1(r4)
16:	stbu	r0,1(r6)
	bdnz	4b
	blr
5:	subfic	r0,r0,4		/* copy bytes until we have the */
	mtctr	r0		/* destination 4-byte aligned */
	subf	r5,r0,r5
6:	lbz	r7,4(r4)
	addi	r4,r4,1
17:	stb	r7,4(r6)
	addi	r6,r6,1
	bdnz	6b
	srwi.	r7,r5,3
	beq	2b
	mtctr	r7
	b	1b
/* we come here on a fault in the 8-byte-at-a-time loop */
88:	subi	r4,r4,8		/* compensate for the lwzu */
98:	mfctr	r0
	rlwimi	r5,r0,3,0,28	/* use the byte-at-a-time loop to */
	b	3b		/* copy up to the byte at fault */
/* here on a write fault in the single-word copy */
96:	subi	r4,r4,4
	b	3b
/* here on a read fault in the initial single-byte copy */
90:	mfctr	r3
	add	r3,r3,r5
	b	70f
/* here on a read fault in the final single-byte copy */
99:	mfctr	r3
	subi	r6,r6,3
/* clear out the rest of the destination: r3 bytes starting at 4(r6) */
70:	li	r0,0
	mr.	r5,r3
	beq	76f
71:	andi.	r4,r6,3
	beq	72f
77:	stb	r0,4(r6)
	addi	r6,r6,1
	addic.	r5,r5,-1
	bne	71b
72:	srwi.	r7,r5,2
	beq	73f
	mtctr	r7
74:	stwu	r0,4(r6)
	bdnz	74b
73:	andi.	r5,r5,3
	beq	76f
	mtctr	r5
	addi	r6,r6,3
75:	stbu	r0,1(r6)
	bdnz	75b
76:	blr
/* here on a write fault in the initial single-byte copy */
80:	mfctr	r3
	add	r3,r3,r5
	blr
/* here on a write fault in the final single-byte copy */
81:	mfctr	r3
	blr

.section __ex_table,"a"
	.align	2
	.long	1b,98b
	.long	11b,98b
	.long	12b,88b
	.long	13b,88b
	.long	14b,3b
	.long	15b,96b
	.long	4b,99b
	.long	16b,81b
	.long	6b,90b
	.long	17b,80b
	.long	77b,76b
	.long	74b,76b
	.long	75b,76b
.text

#undef CLEAR_USE_DCBZ 1
#undef CLEAR_NO_CACHE 1
	.globl	__clear_user
__clear_user:
	addi	r6,r3,-4
	li	r3,0
	li	r5,0
	cmplwi	0,r4,4
	blt	7f
	/* clear a single word */
11:	stwu	r5,4(r6)
	beqlr
#if defined(CLEAR_NO_CACHE) && defined (CONFIG_6xx)
	/* 
	 * no reason to turn off the cache for a single word 
	 * or a few bytes -- Cort 
	 */
	mfspr	r7,HID0
	ori	r8,r7,HID0_DLOCK
	mtspr	HID0,r8
#endif /* CLEAR_NO_CACHE */
	/* clear word sized chunks */
	andi.	r0,r6,3
	add	r4,r0,r4
	subf	r6,r0,r6
	/*rlwinm	r0,r4,32-2,2,31*/
	srwi	r0,r4,2
	mtctr	r0
	bdz	6f
1:	stwu	r5,4(r6)
	bdnz	1b
6:	andi.	r4,r4,3
#if defined(CLEAR_NO_CACHE) && defined (CONFIG_6xx)
	/* restore the original state of HID0 in case cache was off -- Cort */
	mtspr	HID0,r7
#endif /* CLEAR_NO_CACHE */	
	/* clear byte sized chunks */
7:	cmpwi	0,r4,0
	beqlr
	mtctr	r4
	addi	r6,r6,3
8:	stbu	r5,1(r6)
	bdnz	8b
	blr
99:	li	r3,-EFAULT
	blr
.section __ex_table,"a"
	.align	2
	.long	11b,99b
	.long	1b,99b
#ifdef CLEAR_USE_DCBZ
	/*.long	66b,99b*/
#endif		
	.long	8b,99b
.text

	.globl	__strncpy_from_user
__strncpy_from_user:
	addi	r6,r3,-1
	addi	r4,r4,-1
	cmpwi	0,r5,0
	beq	2f
	mtctr	r5
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	stbu	r0,1(r6)
	bdnzf	2,1b		/* dec ctr, branch if ctr != 0 && !cr0.eq */
	beq	3f
2:	addi	r6,r6,1
3:	subf	r3,r3,r6
	blr
99:	li	r3,-EFAULT
	blr
.section __ex_table,"a"
	.align	2
	.long	1b,99b
.text

	.globl	strlen_user
strlen_user:
	addi	r4,r3,-1
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	bne	1b
	subf	r3,r3,r4
	addi	r3,r3,1
	blr
99:	li	r3,0
	blr
.section __ex_table,"a"
	.align	2
	.long	1b,99b
