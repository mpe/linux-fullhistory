#include <linux/linkage.h>
#include <asm/hardware.h>

		.equ	pcio_high, PCIO_BASE & 0xff000000
		.equ	pcio_low,  PCIO_BASE & 0x00ffffff

		.macro	ioaddr, rd,rn
		add	\rd, \rn, #pcio_high
		.if	pcio_low
		add	\rd, \rd, #pcio_low
		.endif
		.endm

ENTRY(insl)
		ioaddr	r0, r0
		ands	ip, r1, #3
		bne	2f

1:		ldr	r3, [r0]
		str	r3, [r1], #4
		subs	r2, r2, #1
		bne	1b
		mov	pc, lr

2:		cmp	ip, #2
		ldr	ip, [r0]
		blt	4f
		bgt	6f

		strh	ip, [r1], #2
		mov	ip, ip, lsr #16
3:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #16
		strne	ip, [r1], #4
		movne	ip, r3, lsr #16
		bne	3b
		strh	ip, [r1], #2
		mov	pc, lr

4:		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strh	ip, [r1], #2
		mov	ip, ip, lsr #16
5:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #8
		strne	ip, [r1], #4
		movne	ip, r3, lsr #24
		bne	5b
		strb	ip, [r1], #1
		mov	pc, lr

6:		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
7:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #24
		strne	ip, [r1], #4
		movne	ip, r3, lsr #8
		bne	7b
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strh	ip, [r1], #2
		mov	pc, lr

ENTRY(outsl)
		ioaddr	r0, r0
		ands	ip, r1, #3
		bne	2f

1:		ldr	r3, [r1], #4
		str	r3, [r0]
		subs	r2, r2, #1
		bne	1b
		mov	pc, lr

2:		bic	r1, r1, #3
		cmp	ip, #2
		ldr	ip, [r1], #4
		mov	ip, ip, lsr #16
		blt	4f
		bgt	5f

3:		ldr	r3, [r1], #4
		orr	ip, ip, r3, lsl #16
		str	ip, [r0]
		mov	ip, r3, lsr #16
		subs	r2, r2, #1
		bne	3b
		mov	pc, lr

4:		ldr	r3, [r1], #4
		orr	ip, ip, r3, lsl #8
		str	ip, [r0]
		mov	ip, r3, lsr #24
		subs	r2, r2, #1
		bne	4b
		mov	pc, lr

5:		ldr	r3, [r1], #4
		orr	ip, ip, r3, lsl #24
		str	ip, [r0]
		mov	ip, r3, lsr #8
		subs	r2, r2, #1
		bne	5b
		mov	pc, lr

		/* Nobody could say these are optimal, but not to worry. */

ENTRY(outswb)
		mov	r2, r2, lsr #1
ENTRY(outsw)
		ioaddr	r0, r0
1:		subs	r2, r2, #1
		ldrgeh	r3, [r1], #2
		strgeh	r3, [r0]
		bgt	1b
		mov	pc, lr

ENTRY(inswb)
		mov	r2, r2, lsr #1
ENTRY(insw)
		stmfd	sp!, {r4, r5, lr}
		ioaddr	r0, r0
						@ + 8 + 9 +10 +11 +12 +13 +14 +15 +16 +17
		subs	ip, r2, #8
		blo	too_little
						@ + 0 + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9
		ands	lr, r1, #3		@ check alignment
		beq	1f

		ldrh	r3, [r0]
		strh	r3, [r1], #2
		sub	ip, ip, #1
		cmn	ip, #8
		blo	too_little

1:		ldrh	r2, [r0]
		ldrh	r3, [r0]
		orr	r2, r2, r3, lsl #16
		ldrh	r3, [r0]
		ldrh	r4, [r0]
		orr	r3, r3, r4, lsl #16
		ldrh	r4, [r0]
		ldrh	r5, [r0]
		orr	r4, r4, r5, lsl #16
		ldrh	r5, [r0]
		ldrh	lr, [r0]
		orr	r5, r5, lr, lsl #16
		stmia	r1!, {r2, r3, r4, r5}
		subs	ip, ip, #8
						@ - 8 - 7 - 6 - 5 - 4 - 3 - 2 - 1 + 0 + 1
		bhs	1b
						@ - 8 - 7 - 6 - 5 - 4 - 3 - 2 - 1 - 8 - 7
		cmn	ip, #4
		ldrhsh	r2, [r0]		@ ... ... ... ... - 4 - 3 - 2 - 1 ... ...
		ldrhsh	r3, [r0]
		orrhs	r2, r2, r3, lsl #16
		ldrhsh	r3, [r0]
		ldrhsh	r4, [r0]
		orrhs	r3, r3, r4, lsl #16
		stmhsia	r1!, {r2, r3}

		tst	ip, #2
		ldrneh	r2, [r0]		@ ... ... - 6 - 5 ... ... - 2 - 1 ... ...
		ldrneh	r3, [r0]
		orrne	r2, r2, r3, lsl #16
		strne	r2, [r1], #4

		tst	ip, #1
		ldrneh	r2, [r0]
		strneh	r2, [r1], #2

		ldmfd	sp!, {r4, r5, pc}

too_little:	subs	r2, r2, #1
		ldrgeh	r3, [r0]
		strgeh	r3, [r1], #2
		bgt	too_little

		ldmfd	sp!, {r4, r5, pc}


ENTRY(insb)
		ioaddr	r0, r0
1:		teq	r2, #0
		ldrneb	r3, [r0]
		strneb	r3, [r1], #1
		subne	r2, r2, #1
		bne	1b
		mov	pc, lr


ENTRY(outsb)
		ioaddr	r0, r0
1:		teq	r2, #0
		ldrneb	r3, [r1], #1
		strneb	r3, [r0]
		subne	r2, r2, #1
		bne	1b
		mov	pc, lr
