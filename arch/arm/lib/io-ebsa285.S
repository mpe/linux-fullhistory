#include <linux/linkage.h>

ENTRY(insl)
		add	r0, r0, #0xff000000
		add	r0, r0, #0x00e00000
		ands	ip, r1, #3
		bne	2f

1:		ldr	r3, [r0]
		str	r3, [r1], #4
		subs	r2, r2, #1
		bne	1b
		mov	pc, lr

2:		cmp	ip, #2
		ldr	ip, [r0]
		blt	3f
		bgt	4f

		strh	ip, [r1], #2
		mov	ip, ip, lsr #16
1:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #16
		strne	ip, [r1], #4
		movne	ip, r3, lsr #16
		bne	1b
		strh	ip, [r1], #2
		mov	pc, lr

3:		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strh	ip, [r1], #2
		mov	ip, ip, lsr #16
1:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #8
		strne	ip, [r1], #4
		movne	ip, r3, lsr #24
		bne	1b
		strb	ip, [r1], #1
		mov	pc, lr

4:		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
1:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #24
		strne	ip, [r1], #4
		movne	ip, r3, lsr #8
		bne	1b
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strh	ip, [r1], #2
		mov	pc, lr

ENTRY(outsl)
		add	r0, r0, #0xff000000
		add	r0, r0, #0x00e00000
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
		blt	3f
		bgt	4f

1:		ldr	r3, [r1], #4
		orr	ip, ip, r3, lsl #16
		str	ip, [r0]
		mov	ip, r3, lsr #16
		subs	r2, r2, #1
		bne	1b
		mov	pc, lr

3:		ldr	r3, [r1], #4
		orr	ip, ip, r3, lsl #8
		str	ip, [r0]
		mov	ip, r3, lsr #24
		subs	r2, r2, #1
		bne	3b
		mov	pc, lr

4:		ldr	r3, [r1], #4
		orr	ip, ip, r3, lsl #24
		str	ip, [r0]
		mov	ip, r3, lsr #8
		subs	r2, r2, #1
		bne	4b
		mov	pc, lr

		/* Nobody could say these are optimal, but not to worry. */

ENTRY(outswb)
		mov	r2, r2, lsr #1
ENTRY(outsw)
		add	r0, r0, #0xff000000
		add	r0, r0, #0x00e00000
1:		subs	r2, r2, #1
		ldrgeh	r3, [r1], #2
		strgeh	r3, [r0]
		bgt	1b
		mov	pc, lr

ENTRY(inswb)
		mov	r2, r2, lsr #1
ENTRY(insw)
		stmfd	sp!, {r4, r5, lr}
		add	r0, r0, #0xff000000
		add	r0, r0, #0x00e00000
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
		add	r0, r0, #0xff000000
		add	r0, r0, #0x00e00000
1:		teq	r2, #0
		ldrneb	r3, [r0]
		strneb	r3, [r1], #1
		subne	r2, r2, #1
		bne	1b
		mov	pc, lr


ENTRY(outsb)
		add	r0, r0, #0xff000000
		add	r0, r0, #0x00e00000
1:		teq	r2, #0
		ldrneb	r3, [r1], #1
		strneb	r3, [r0]
		subne	r2, r2, #1
		bne	1b
		mov	pc, lr
