/*
 * linux/arch/arm/lib/io.S
 *
 * Copyright (C) 1995, 1996 Russell King
 */
#include <linux/config.h> /* for CONFIG_CPU_nn */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>

		.text
		.align

		.equ	diff_pcio_base, PCIO_BASE - IO_BASE

		.macro	outw2	rd
		mov	r8, \rd, lsl #16
		orr	r8, r8, r8, lsr #16
		str	r8, [r3, r0, lsl #2]
		mov	r8, \rd, lsr #16
		orr	r8, r8, r8, lsl #16
		str	r8, [r3, r0, lsl #2]
		.endm

		.macro	inw2	rd, mask, temp
		ldr	\rd, [r0]
		and	\rd, \rd, \mask
		ldr	\temp, [r0]
		orr	\rd, \rd, \temp, lsl #16
		.endm

		.macro	addr	rd
		tst	\rd, #0x80000000
		mov	\rd, \rd, lsl #2
		add	\rd, \rd, #IO_BASE
		addeq	\rd, \rd, #diff_pcio_base
		.endm

.iosw_bad_align_msg:
		.ascii	"insw: bad buffer alignment (%p), called from %08lX\n\0"
.iosl_warning:
		.ascii	"<4>insl/outsl not implemented, called from %08lX\0"
		.align

/*
 * These make no sense on Acorn machines.
 * Print a warning message.
 */
ENTRY(insl)
ENTRY(outsl)
		adr	r0, .iosl_warning
		mov	r1, lr
		b	SYMBOL_NAME(printk)

.iosw_bad_alignment:
		adr	r0, .iosw_bad_align_msg
		mov	r2, lr
		b	SYMBOL_NAME(panic)


/* Purpose: read a block of data from a hardware register to memory.
 * Proto  : void insw(int from_port, void *to, int len_in_words);
 * Notes  : increment to, 'to' must be 16-bit aligned
 */

.insw_align:	tst	r1, #1
		bne	.iosw_bad_alignment

		ldr	r3, [r0]
		strb	r3, [r1], #1
		mov	r3, r3, lsr #8
		strb	r3, [r1], #1

		subs	r2, r2, #1
		bne	.insw_aligned

ENTRY(insw)
		teq	r2, #0
		RETINSTR(moveq,pc,lr)
		addr	r0
		tst	r1, #3
		bne	.insw_align

.insw_aligned:	mov	ip, #0xff
		orr	ip, ip, ip, lsl #8
		stmfd	sp!, {r4, r5, r6, lr}

		subs	r2, r2, #8
		bmi	.no_insw_8

.insw_8_lp:	ldr	r3, [r0]
		and	r3, r3, ip
		ldr	r4, [r0]
		orr	r3, r3, r4, lsl #16

		ldr	r4, [r0]
		and	r4, r4, ip
		ldr	r5, [r0]
		orr	r4, r4, r5, lsl #16

		ldr	r5, [r0]
		and	r5, r5, ip
		ldr	r6, [r0]
		orr	r5, r5, r6, lsl #16

		ldr	r6, [r0]
		and	r6, r6, ip
		ldr	lr, [r0]
		orr	r6, r6, lr, lsl #16

		stmia	r1!, {r3 - r6}
		subs	r2, r2, #8
		bpl	.insw_8_lp
		tst	r2, #7
		LOADREGS(eqfd, sp!, {r4, r5, r6, pc})

.no_insw_8:	tst	r2, #4
		beq	.no_insw_4

		ldr	r3, [r0]
		and	r3, r3, ip
		ldr	r4, [r0]
		orr	r3, r3, r4, lsl #16

		ldr	r4, [r0]
		and	r4, r4, ip
		ldr	r5, [r0]
		orr	r4, r4, r5, lsl #16

		stmia	r1!, {r3, r4}

.no_insw_4:	tst	r2, #2
		beq	.no_insw_2

		ldr	r3, [r0]
		and	r3, r3, ip
		ldr	r4, [r0]
		orr	r3, r3, r4, lsl #16

		str	r3, [r1], #4

.no_insw_2:	tst	r2, #1
		ldrne	r3, [r0]
		strneb	r3, [r1], #1
		movne	r3, r3, lsr #8
		strneb	r3, [r1]
		LOADREGS(fd, sp!, {r4, r5, r6, pc})

@ Purpose: write a block of data from memory to a hardware register.
@ Proto  : outsw(int to_reg, void *from, int len_in_words);
@ Notes  : increments from

.outsw_align:	tst	r1, #1
		bne	.iosw_bad_alignment

		add	r1, r1, #2

		ldr	r3, [r1, #-4]
		mov	r3, r3, lsr #16
		orr	r3, r3, r3, lsl #16
		str	r3, [r0]
		subs	r2, r2, #1
		bne	.outsw_aligned

ENTRY(outsw)
		teq	r2, #0
		RETINSTR(moveq,pc,lr)
		addr	r0
		tst	r1, #3
		bne	.outsw_align

.outsw_aligned:	stmfd	sp!, {r4, r5, r6, lr}

		subs	r2, r2, #8
		bmi	.no_outsw_8
.outsw_8_lp:	ldmia	r1!, {r3, r4, r5, r6}

		mov	ip, r3, lsl #16
		orr	ip, ip, ip, lsr #16
		str	ip, [r0]

		mov	ip, r3, lsr #16
		orr	ip, ip, ip, lsl #16
		str	ip, [r0]

		mov	ip, r4, lsl #16
		orr	ip, ip, ip, lsr #16
		str	ip, [r0]

		mov	ip, r4, lsr #16
		orr	ip, ip, ip, lsl #16
		str	ip, [r0]

		mov	ip, r5, lsl #16
		orr	ip, ip, ip, lsr #16
		str	ip, [r0]

		mov	ip, r5, lsr #16
		orr	ip, ip, ip, lsl #16
		str	ip, [r0]

		mov	ip, r6, lsl #16
		orr	ip, ip, ip, lsr #16
		str	ip, [r0]

		mov	ip, r6, lsr #16
		orr	ip, ip, ip, lsl #16
		str	ip, [r0]

		subs	r2, r2, #8
		bpl	.outsw_8_lp
		tst	r2, #7
		LOADREGS(eqfd, sp!, {r4, r5, r6, pc})

.no_outsw_8:	tst	r2, #4
		beq	.no_outsw_4

		ldmia	r1!, {r3, r4}

		mov	ip, r3, lsl #16
		orr	ip, ip, ip, lsr #16
		str	ip, [r0]

		mov	ip, r3, lsr #16
		orr	ip, ip, ip, lsl #16
		str	ip, [r0]

		mov	ip, r4, lsl #16
		orr	ip, ip, ip, lsr #16
		str	ip, [r0]

		mov	ip, r4, lsr #16
		orr	ip, ip, ip, lsl #16
		str	ip, [r0]

.no_outsw_4:	tst	r2, #2
		beq	.no_outsw_2

		ldr	r3, [r1], #4

		mov	ip, r3, lsl #16
		orr	ip, ip, ip, lsr #16
		str	ip, [r0]

		mov	ip, r3, lsr #16
		orr	ip, ip, ip, lsl #16
		str	ip, [r0]

.no_outsw_2:	tst	r2, #1

		ldrne	r3, [r1]

		movne	ip, r3, lsl #16
		orrne	ip, ip, ip, lsr #16
		strne	ip, [r0]

		LOADREGS(fd, sp!, {r4, r5, r6, pc})

.insb_align:	rsb	ip, ip, #4
		cmp	ip, r2
		movgt	ip, r2
		cmp	ip, #2
		ldrb	r3, [r0]
		strb	r3, [r1], #1
		ldrgeb	r3, [r0]
		strgeb	r3, [r1], #1
		ldrgtb	r3, [r0]
		strgtb	r3, [r1], #1
		subs	r2, r2, ip
		bne	.insb_aligned

ENTRY(insb)
		teq	r2, #0
		moveq	pc, lr
		addr	r0
		ands	ip, r1, #3
		bne	.insb_align

.insb_aligned:	stmfd	sp!, {r4 - r6, lr}

		subs	r2, r2, #16
		bmi	.insb_no_16

.insb_16_lp:	ldrb	r3, [r0]
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #8
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #16
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #24
		ldrb	r4, [r0]
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #8
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #16
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #24
		ldrb	r5, [r0]
		ldrb	r6, [r0]
		orr	r5, r5, r6, lsl #8
		ldrb	r6, [r0]
		orr	r5, r5, r6, lsl #16
		ldrb	r6, [r0]
		orr	r5, r5, r6, lsl #24
		ldrb	r6, [r0]
		ldrb	ip, [r0]
		orr	r6, r6, ip, lsl #8
		ldrb	ip, [r0]
		orr	r6, r6, ip, lsl #16
		ldrb	ip, [r0]
		orr	r6, r6, ip, lsl #24
		stmia	r1!, {r3 - r6}
		subs	r2, r2, #16
		bpl	.insb_16_lp

		tst	r2, #15
		LOADREGS(eqfd, sp!, {r4 - r6, pc})

.insb_no_16:	tst	r2, #8
		beq	.insb_no_8

		ldrb	r3, [r0]
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #8
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #16
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #24
		ldrb	r4, [r0]
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #8
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #16
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #24
		stmia	r1!, {r3, r4}

.insb_no_8:	tst	r2, #4
		bne	.insb_no_4

		ldrb	r3, [r0]
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #8
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #16
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #24
		str	r3, [r1], #4

.insb_no_4:	ands	r2, r2, #3
		LOADREGS(eqfd, sp!, {r4 - r6, pc})
		cmp	r2, #2
		ldrb	r3, [r0]
		strb	r3, [r1], #1
		ldrgeb	r3, [r0]
		strgeb	r3, [r1], #1
		ldrgtb	r3, [r0]
		strgtb	r3, [r1]
		LOADREGS(fd, sp!, {r4 - r6, pc})



.outsb_align:	rsb	ip, ip, #4
		cmp	ip, r2
		mov	ip, r2
		cmp	ip, #2
		ldrb	r3, [r1], #1
		strb	r3, [r0]
		ldrgeb	r3, [r1], #1
		strgeb	r3, [r0]
		ldrgtb	r3, [r1], #1
		strgtb	r3, [r0]
		subs	r2, r2, ip
		bne	.outsb_aligned

ENTRY(outsb)
		teq	r2, #0
		moveq	pc, lr
		addr	r0
		ands	ip, r1, #3
		bne	.outsb_align

.outsb_aligned:	stmfd	sp!, {r4 - r6, lr}

		subs	r2, r2, #16
		bmi	.outsb_no_16

.outsb_16_lp:	ldmia	r1!, {r3 - r6}
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]

		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]

		strb	r5, [r0]
		mov	r5, r5, lsr #8
		strb	r5, [r0]
		mov	r5, r5, lsr #8
		strb	r5, [r0]
		mov	r5, r5, lsr #8
		strb	r5, [r0]

		strb	r6, [r0]
		mov	r6, r6, lsr #8
		strb	r6, [r0]
		mov	r6, r6, lsr #8
		strb	r6, [r0]
		mov	r6, r6, lsr #8
		strb	r6, [r0]
		subs	r2, r2, #16
		bpl	.outsb_16_lp

		tst	r2, #15
		LOADREGS(eqfd, sp!, {r4 - r6, pc})

.outsb_no_16:	tst	r2, #8
		beq	.outsb_no_8

		ldmia	r1, {r3, r4}
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]

		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]

.outsb_no_8:	tst	r2, #4
		bne	.outsb_no_4

		ldr	r3, [r1], #4
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]

.outsb_no_4:	ands	r2, r2, #3
		LOADREGS(eqfd, sp!, {r4 - r6, pc})
		cmp	r2, #2
		ldrb	r3, [r1], #1
		strb	r3, [r0]
		ldrgeb	r3, [r1], #1
		strgeb	r3, [r0]
		ldrgtb	r3, [r1]
		strgtb	r3, [r0]
		LOADREGS(fd, sp!, {r4 - r6, pc})




@ Purpose: write a memc register
@ Proto  : void memc_write(int register, int value);
@ Returns: nothing

#if defined(CONFIG_CPU_26)
ENTRY(memc_write)
		cmp	r0, #7
		RETINSTR(movgt,pc,lr)
		mov	r0, r0, lsl #17
		mov	r1, r1, lsl #15
		mov	r1, r1, lsr #17
		orr	r0, r0, r1, lsl #2
		add	r0, r0, #0x03600000
		strb	r0, [r0]
		RETINSTR(mov,pc,lr)
#define CPSR2SPSR(rt)
#else
#define CPSR2SPSR(rt) \
		mrs	rt, cpsr; \
		msr	spsr, rt
#endif

@ Purpose: call an expansion card loader to read bytes.
@ Proto  : char read_loader(int offset, char *card_base, char *loader);
@ Returns: byte read

ENTRY(ecard_loader_read)
		stmfd	sp!, {r4 - r12, lr}
		mov	r11, r1
		mov	r1, r0
		CPSR2SPSR(r0)
		mov	lr, pc
		mov	pc, r2
		LOADREGS(fd, sp!, {r4 - r12, pc})

@ Purpose: call an expansion card loader to reset the card
@ Proto  : void read_loader(int card_base, char *loader);
@ Returns: byte read

ENTRY(ecard_loader_reset)
		stmfd	sp!, {r4 - r12, lr}
		mov	r11, r0
		CPSR2SPSR(r0)
		mov	lr, pc
		add	pc, r1, #8
		LOADREGS(fd, sp!, {r4 - r12, pc})


#if 0
		mov	r2, r2, lsl#1
		mov	ip, sp
		stmfd	sp!, {r4 - r10, fp, ip, lr, pc}
		sub	fp, ip, #4
		addr	r3, r0
		add	r0, r3, r0, lsl #2
		tst	r1, #3
		beq	Linswok
		tst	r1, #1
		bne	Linsw_notaligned
		cmp	r2, #1
		ldrge	r4, [r0]
		strgeb	r4, [r1], #1
		movgt	r4, r4, LSR#8
		strgtb	r4, [r1], #1
		LOADREGS(leea, fp, {r4 - r10, fp, sp, pc})
		sub	r2, r2, #2
Linswok:	mov	ip, #0xFF
		orr	ip, ip, ip, lsl #8
Linswlp:	subs	r2, r2, #64
		bmi	Linsw_toosmall
		IN(r3)
		IN(r4)
		IN(r5)
		IN(r6)
		IN(r7)
		IN(r8)
		IN(r9)
		IN(r10)
		stmia	r1!, {r3 - r10}
		IN(r3)
		IN(r4)
		IN(r5)
		IN(r6)
		IN(r7)
		IN(r8)
		IN(r9)
		IN(r10)
		stmia	r1!, {r3 - r10}
		bne	Linswlp
		LOADREGS(ea, fp, {r4 - r10, fp, sp, pc})
Linsw_toosmall:
		adds	r2, r2, #32
		bmi	Linsw_toosmall2
Linsw2lp:	IN(r3)
		IN(r4)
		IN(r5)
		IN(r6)
		IN(r7)
		IN(r8)
		IN(r9)
		IN(r10)
		stmia	r1!, {r3 - r10}
		LOADREGS(eqea, fp, {r4 - r10, fp, sp, pc})
		b	Linsw_notaligned
Linsw_toosmall2:
		add	r2, r2, #32
Linsw_notaligned:
		cmp	r2, #1
		LOADREGS(ltea, fp, {r4 - r10, fp, sp, pc})
		ldr	r4, [r0]
		strb	r4, [r1], #1
		movgt	r4, r4, LSR#8
		strgtb	r4, [r1], #1
		subs	r2, r2, #2
		bgt	Linsw_notaligned
		LOADREGS(ea, fp, {r4 - r10, fp, sp, pc})


ENTRY(outsw)
		mov	r2, r2, lsl#1
		mov	ip, sp
		stmfd	sp!, {r4 - r8, fp, ip, lr, pc}
		sub	fp, ip, #4
		addr	r3, r0
		tst	r1, #2
		beq	1f
		ldr	r4, [r1], #2
		mov	r4, r4, lsl #16
		orr	r4, r4, r4, lsr #16
		str	r4, [r3, r0, lsl #2]
		subs	r2, r2, #2
		LOADREGS(eqea, fp, {r4 - r8, fp, sp, pc})
1:		subs	r2, r2, #32
		blt	2f
		ldmia	r1!, {r4, r5, r6, r7}
		OUT(r4)
		OUT(r5)
		OUT(r6)
		OUT(r7)
		ldmia	r1!, {r4, r5, r6, r7}
		OUT(r4)
		OUT(r5)
		OUT(r6)
		OUT(r7)
		bne	1b
		LOADREGS(ea, fp, {r4 - r8, fp, sp, pc})
2:		adds	r2, r2, #32
		LOADREGS(eqea, fp, {r4 - r8, fp, sp, pc})
3:		ldr	r4, [r1],#2
		mov	r4, r4, lsl#16
		orr	r4, r4, r4, lsr#16
		str	r4, [r3, r0, lsl#2]
		subs	r2, r2, #2
		bgt	3b
		LOADREGS(ea, fp, {r4 - r8, fp, sp, pc})

#endif

