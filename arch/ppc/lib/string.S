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
#include "../kernel/ppc_asm.tmpl"
#include <asm/processor.h>
#include <asm/errno.h>

CACHELINE_BYTES = 32
LG_CACHELINE_BYTES = 5
CACHELINE_MASK = 0x1f
CACHELINE_WORDS = 8

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

/*
 * Use dcbz on the complete cache lines in the destination
 * to set them to zero.  This requires that the destination
 * area is cacheable.  -- paulus
 */
	.globl	cacheable_memzero
cacheable_memzero:
	mr	r5,r4
	li	r4,0
	addi	r6,r3,-4
	cmplwi	0,r5,4
	blt	7f
	stwu	r4,4(r6)
	beqlr
	andi.	r0,r6,3
	add	r5,r0,r5
	subf	r6,r0,r6
	clrlwi	r7,r6,32-LG_CACHELINE_BYTES
	add	r8,r7,r5
	srwi	r9,r8,LG_CACHELINE_BYTES
	addic.	r9,r9,-1	/* total number of complete cachelines */
	ble	2f
	xori	r0,r7,CACHELINE_MASK & ~3
	srwi.	r0,r0,2
	beq	3f
	mtctr	r0
4:	stwu	r4,4(r6)
	bdnz	4b
3:	mtctr	r9
	li	r7,4
10:	dcbz	r7,r6
	addi	r6,r6,CACHELINE_BYTES
	bdnz	10b
	clrlwi	r5,r8,32-LG_CACHELINE_BYTES
	addi	r5,r5,4
2:	srwi	r0,r5,2
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
	srwi	r0,r5,2
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

/*
 * This version uses dcbz on the complete cache lines in the
 * destination area to reduce memory traffic.  This requires that
 * the destination area is cacheable.
 * We only use this version if the source and dest don't overlap.
 * -- paulus.
 */
	.global	cacheable_memcpy
cacheable_memcpy:
	add	r7,r3,r5		/* test if the src & dst overlap */
	add	r8,r4,r5
	cmplw	0,r4,r7
	cmplw	1,r3,r8
	crand	0,0,4			/* cr0.lt &= cr1.lt */
	blt	memcpy			/* if regions overlap */

	addi	r4,r4,-4
	addi	r6,r3,-4
	neg	r0,r3
	andi.	r0,r0,CACHELINE_MASK	/* # bytes to start of cache line */
	beq	58f

	cmplw	0,r5,r0			/* is this more than total to do? */
	blt	63f			/* if not much to do */
	andi.	r8,r0,3			/* get it word-aligned first */
	subf	r5,r0,r5
	mtctr	r8
	beq+	61f
70:	lbz	r9,4(r4)		/* do some bytes */
	stb	r9,4(r6)
	addi	r4,r4,1
	addi	r6,r6,1
	bdnz	70b
61:	srwi.	r0,r0,2
	mtctr	r0
	beq	58f
72:	lwzu	r9,4(r4)		/* do some words */
	stwu	r9,4(r6)
	bdnz	72b

58:	srwi.	r0,r5,LG_CACHELINE_BYTES /* # complete cachelines */
	clrlwi	r5,r5,32-LG_CACHELINE_BYTES
	li	r11,4
	mtctr	r0
	beq	63f
53:	dcbz	r11,r6
	lwz	r7,4(r4)
	lwz	r8,8(r4)
	lwz	r9,12(r4)
	lwzu	r10,16(r4)
	stw	r7,4(r6)
	stw	r8,8(r6)
	stw	r9,12(r6)
	stwu	r10,16(r6)
	lwz	r7,4(r4)
	lwz	r8,8(r4)
	lwz	r9,12(r4)
	lwzu	r10,16(r4)
	stw	r7,4(r6)
	stw	r8,8(r6)
	stw	r9,12(r6)
	stwu	r10,16(r6)
	bdnz	53b

63:	srwi.	r0,r5,2
	mtctr	r0
	beq	64f
30:	lwzu	r0,4(r4)
	stwu	r0,4(r6)
	bdnz	30b

64:	andi.	r0,r5,3
	mtctr	r0
	beq+	65f
40:	lbz	r0,4(r4)
	stb	r0,4(r6)
	addi	r4,r4,1
	addi	r6,r6,1
	bdnz	40b
65:	blr

	.globl	memmove
memmove:
	cmplw	0,r3,r4
	bgt	backwards_memcpy
	/* fall through */

	.globl	memcpy
memcpy:
	srwi.	r7,r5,3
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
	addi	r4,r4,-4
	addi	r6,r3,-4
	neg	r0,r3
	andi.	r0,r0,CACHELINE_MASK	/* # bytes to start of cache line */
	beq	58f

	cmplw	0,r5,r0			/* is this more than total to do? */
	blt	63f			/* if not much to do */
	andi.	r8,r0,3			/* get it word-aligned first */
	mtctr	r8
	beq+	61f
70:	lbz	r9,4(r4)		/* do some bytes */
71:	stb	r9,4(r6)
	addi	r4,r4,1
	addi	r6,r6,1
	bdnz	70b
61:	subf	r5,r0,r5
	srwi.	r0,r0,2
	mtctr	r0
	beq	58f
72:	lwzu	r9,4(r4)		/* do some words */
73:	stwu	r9,4(r6)
	bdnz	72b

58:	srwi.	r0,r5,LG_CACHELINE_BYTES /* # complete cachelines */
	clrlwi	r5,r5,32-LG_CACHELINE_BYTES
	li	r11,4
	mtctr	r0
	beq	63f
53:	dcbz	r11,r6
10:	lwz	r7,4(r4)
11:	lwz	r8,8(r4)
12:	lwz	r9,12(r4)
13:	lwzu	r10,16(r4)
14:	stw	r7,4(r6)
15:	stw	r8,8(r6)
16:	stw	r9,12(r6)
17:	stwu	r10,16(r6)
20:	lwz	r7,4(r4)
21:	lwz	r8,8(r4)
22:	lwz	r9,12(r4)
23:	lwzu	r10,16(r4)
24:	stw	r7,4(r6)
25:	stw	r8,8(r6)
26:	stw	r9,12(r6)
27:	stwu	r10,16(r6)
	bdnz	53b

63:	srwi.	r0,r5,2
	mtctr	r0
	beq	64f
30:	lwzu	r0,4(r4)
31:	stwu	r0,4(r6)
	bdnz	30b

64:	andi.	r0,r5,3
	mtctr	r0
	beq+	65f
40:	lbz	r0,4(r4)
41:	stb	r0,4(r6)
	addi	r4,r4,1
	addi	r6,r6,1
	bdnz	40b
65:	li	r3,0
	blr

/* read fault, initial single-byte copy */
100:	li	r4,0
	b	90f
/* write fault, initial single-byte copy */
101:	li	r4,1
90:	subf	r5,r8,r5
	li	r3,0
	b	99f
/* read fault, initial word copy */
102:	li	r4,0
	b	91f
/* write fault, initial word copy */
103:	li	r4,1
91:	li	r3,2
	b	99f
/* read fault in 2nd half of cacheline loop */
106:	addi	r5,r5,-16
/* read fault in 1st half of cacheline loop */
104:	li	r4,0
	b	92f
/* write fault in 2nd half of cacheline loop */
107:	addi	r5,r5,-16
/* fault on dcbz (effectively a write fault) */
/* or write fault in 1st half of cacheline loop */
105:	li	r4,1
92:	li	r3,LG_CACHELINE_BYTES
	b	99f
/* read fault in final word loop */
108:	li	r4,0
	b	93f
/* write fault in final word loop */
109:	li	r4,1
93:	andi.	r5,r5,3
	li	r3,2
	b	99f
/* read fault in final byte loop */
110:	li	r4,0
	b	94f
/* write fault in final byte loop */
111:	li	r4,1
94:	li	r5,0
	li	r3,0
/*
 * At this stage the number of bytes not copied is
 * r5 + (ctr << r3), and r4 is 0 for read or 1 for write.
 */
99:	mfctr	r0
	slw	r3,r0,r3
	add	r3,r3,r5
	cmpwi	0,r4,0
	bne	120f
/* for read fault, clear out the destination: r3 bytes starting at 4(r6) */
	srwi.	r0,r3,2
	li	r9,0
	mtctr	r0
	beq	113f
112:	stwu	r9,4(r6)
	bdnz	112b
113:	andi.	r0,r3,3
	mtctr	r0
	beq	120f
114:	stb	r9,4(r6)
	addi	r6,r6,1
	bdnz	114b
120:	blr

.section __ex_table,"a"
	.align	2
	.long	70b,100b
	.long	71b,101b
	.long	72b,102b
	.long	73b,103b
	.long	53b,105b
	.long	10b,104b
	.long	11b,104b
	.long	12b,104b
	.long	13b,104b
	.long	14b,105b
	.long	15b,105b
	.long	16b,105b
	.long	17b,105b
	.long	20b,106b
	.long	21b,106b
	.long	22b,106b
	.long	23b,106b
	.long	24b,107b
	.long	25b,107b
	.long	26b,107b
	.long	27b,107b
	.long	30b,108b
	.long	31b,109b
	.long	40b,110b
	.long	41b,111b
	.long	112b,120b
	.long	114b,120b
.text

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
	/* clear word sized chunks */
	andi.	r0,r6,3
	add	r4,r0,r4
	subf	r6,r0,r6
	srwi	r0,r4,2
	mtctr	r0
	bdz	6f
1:	stwu	r5,4(r6)
	bdnz	1b
6:	andi.	r4,r4,3
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

/* r3 = str, r4 = len (> 0), r5 = top (highest addr) */
	.globl	__strnlen_user
__strnlen_user:
	addi	r7,r3,-1
	subf	r6,r7,r5	/* top+1 - str */
	cmplw	0,r4,r6
	bge	0f
	mr	r6,r4
0:	mtctr	r6		/* ctr = min(len, top - str) */
1:	lbzu	r0,1(r7)	/* get next byte */
	cmpwi	0,r0,0
	bdnzf	2,1b		/* loop if --ctr != 0 && byte != 0 */
	addi	r7,r7,1
	subf	r3,r3,r7	/* number of bytes we have looked at */
	beqlr			/* return if we found a 0 byte */
	cmpw	0,r3,r4		/* did we look at all len bytes? */
	blt	99f		/* if not, must have hit top */
	addi	r3,r4,1		/* return len + 1 to indicate no null found */
	blr
99:	li	r3,0		/* bad address, return 0 */
	blr
.section __ex_table,"a"
	.align	2
	.long	1b,99b
