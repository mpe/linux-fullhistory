#define __ASSEMBLER__
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


ENTRY(outsw)
ENTRY(outswb)
		mov	pc, lr

ENTRY(insw)
ENTRY(inswb)
		mov	pc, lr

