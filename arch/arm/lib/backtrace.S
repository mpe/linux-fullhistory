/*
 * linux/arch/arm/lib/backtrace.S
 *
 * Copyright (C) 1995, 1996 Russell King
 */
#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text

@ fp is 0 or stack frame

#define frame	r4
#define next	r5
#define save	r6
#define mask	r7
#define offset	r8

ENTRY(__backtrace)
		mov	r1, #0x10
		mov	r0, fp

ENTRY(c_backtrace)
		stmfd	sp!, {r4 - r8, lr}	@ Save an extra register so we have a location...
#ifdef CONFIG_CPU_32
		tst	r1, #0x10		@ 26 or 32-bit?
		moveq	mask, #0xfc000003
		movne	mask, #0
#else
		mov	mask, #0xfc000003
#endif
		tst	mask, r0
		movne	r0, #0
		movs	frame, r0
1:		moveq	r0, #-2
		LOADREGS(eqfd, sp!, {r4 - r8, pc})

2:		stmfd	sp!, {pc}			@ calculate offset of PC in STMIA instruction
		ldr	r0, [sp], #4
		adr	r1, 2b - 4
		sub	offset, r0, r1

3:		tst	frame, mask			@ Check for address exceptions...
		bne	1b

		ldmda	frame, {r0, r1, r2, r3}		@ fp, sp, lr, pc
		mov	next, r0

		sub	save, r3, offset		@ Correct PC for prefetching
		bic	save, save, mask
		adr	r0, .Lfe
		mov	r1, save
		bic	r2, r2, mask
		bl	SYMBOL_NAME(printk)

		sub	r0, frame, #16
		ldr	r1, [save, #4]
		mov	r3, r1, lsr #10
		ldr	r2, .Ldsi+4
		teq	r3, r2			@ Check for stmia sp!, {args}
		addeq	save, save, #4		@ next instruction
		bleq	.Ldumpstm

		ldr	r1, [save, #4]		@ Get 'stmia sp!, {rlist, fp, ip, lr, pc}' instruction
		mov	r3, r1, lsr #10
		ldr	r2, .Ldsi
		teq	r3, r2
		bleq	.Ldumpstm

		teq	frame, next
		movne	frame, next
		teqne	frame, #0
		bne	3b
		LOADREGS(fd, sp!, {r4 - r8, pc})


#define instr r4
#define reg   r5
#define stack r6

.Ldumpstm:	stmfd	sp!, {instr, reg, stack, lr}
		mov	stack, r0
		mov	instr, r1
		mov	reg, #9

1:		mov	r3, #1
		tst	instr, r3, lsl reg
		beq	2f
		ldr	r2, [stack], #-4
		mov	r1, reg
		adr	r0, .Lfp
		bl	SYMBOL_NAME(printk)
2:		subs	reg, reg, #1
		bpl	1b

		mov	r0, stack
		LOADREGS(fd, sp!, {instr, reg, stack, pc})

.Lfe:		.ascii	"Function entered at [<%p>] from [<%p>]\n"
		.byte 0
.Lfp:		.ascii	"  r%d = %p\n"
		.byte 0
		.align
.Ldsi:		.word	0x00e92dd8 >> 2
		.word	0x00e92d00 >> 2
