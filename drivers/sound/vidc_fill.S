/*
 * sound/vidc_fill.S
 *
 * Filler routines for DMA buffers
 *
 * Copyright (C) 1997 Russell King
 */
#define __ASSEMBLY__
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>

		.text

ENTRY(vidc_fill_1x8)
		mov	ip, #0xff00
1:		cmp	r0, r1
		bge	SYMBOL_NAME(vidc_clear)
		ldrb	r4, [r0], #1
		and	r4, ip, r4, lsl #8
		orr	r4, r4, r4, lsl #16
		str	r4, [r2], #4
		cmp	r2, r3
		blt	1b
		mov	pc, lr

ENTRY(vidc_fill_2x8)
		mov	ip, #0xff00
1:		cmp	r0, r1
		bge	SYMBOL_NAME(vidc_clear)
		ldr	r4, [r0], #2
		and	r5, r4, ip
		and	r4, ip, r4, lsl #8
		orr	r4, r4, r5, lsl #16
		orr	r4, r4, r4, lsr #8
		str	r4, [r2], #4
		cmp	r2, r3
		blt	1b
		mov	pc, lr

ENTRY(vidc_fill_1x16)
		mov	ip, #0xff00
		orr	ip, ip, ip, lsr #8
1:		cmp	r0, r1
		bge	SYMBOL_NAME(vidc_clear)
		ldr	r5, [r0], #2
		and	r4, r5, ip
		orr	r4, r4, r4, lsl #16
		str	r4, [r2], #4
		cmp	r0, r1
		addlt	r0, r0, #2
		andlt	r4, r5, ip, lsl #16
		orrlt	r4, r4, r4, lsr #16
		strlt	r4, [r2], #4
		cmp	r2, r3
		blt	1b
		mov	pc, lr

ENTRY(vidc_fill_2x16)
		mov	ip, #0xff00
		orr	ip, ip, ip, lsr #8
1:		cmp	r0, r1
		bge	SYMBOL_NAME(vidc_clear)
		ldr	r4, [r0], #4
		str	r4, [r2], #4
		cmp	r0, r1
		ldrlt	r4, [r0], #4
		strlt	r4, [r2], #4
		cmp	r2, r3
		blt	1b
		mov	pc, lr

ENTRY(vidc_fill_noaudio)
		mov	r0, #0
		mov	r1, #0
2:		mov	r4, #0
		mov	r5, #0
1:		cmp	r2, r3
		stmltia	r2!, {r0, r1, r4, r5}
		blt	1b
		mov	pc, lr

ENTRY(vidc_clear)
		mov	r0, #0
		mov	r1, #0
		tst	r2, #4
		str	r0, [r2], #4
		tst	r2, #8
		stmia	r2!, {r0, r1}
		b	2b

/*
 * Call filler routines with:
 *  r0 = phys address
 *  r1 = phys end
 *  r2 = buffer
 * Returns:
 *  r0 = new buffer address
 *  r2 = new buffer finish
 *  r4 = corrupted
 *  r5 = corrupted
 *  ip = corrupted
 */

ENTRY(vidc_sound_dma_irq)
		stmfd	sp!, {r4 - r9, lr}
		ldr	r9, =SYMBOL_NAME(dma_start)
		ldmia	r9, {r0, r1, r2, r3, r4, r5}
		teq	r1, #0
		adreq	r4, SYMBOL_NAME(vidc_fill_noaudio)
		moveq	r8, #1 << 31
		movne	r8, #0
		mov	ip, #IOMD_BASE & 0xff000000
		orr	ip, ip, #IOMD_BASE & 0x00ff0000
		ldrb	r7, [ip, #IOMD_SD0ST]
		tst	r7, #DMA_ST_OFL			@ Check for overrun
		eorne	r7, r7, #DMA_ST_AB
		tst	r7, #DMA_ST_AB
		moveq	r2, r3				@ DMAing A, update B
		add	r3, r2, r5			@ End of DMA buffer
		add	r1, r1, r0			@ End of virtual DMA buffer
		mov	lr, pc
		mov	pc, r4				@ Call fill routine
		sub	r1, r1, r0			@ Remaining length
		stmia	r9, {r0, r1}
		mov	r0, #0
		tst	r2, #4				@ Round buffer up to 4 words
		strne	r0, [r2], #4
		tst	r2, #8
		strne	r0, [r2], #4
		strne	r0, [r2], #4
		sub	r2, r2, #16
		mov	r2, r2, lsl #20
		movs	r2, r2, lsr #20
		orreq	r2, r2, #1 << 30		@ Set L bit
		orr	r2, r2, r8
		ldmdb	r9, {r3, r4, r5}
		tst	r7, #DMA_ST_AB
		mov	ip, #IOMD_BASE & 0xff000000
		orr	ip, ip, #IOMD_BASE & 0x00ff0000
		streq	r4, [ip, #IOMD_SD0CURB]
		strne	r5, [ip, #IOMD_SD0CURA]
		streq	r2, [ip, #IOMD_SD0ENDB]
		strne	r2, [ip, #IOMD_SD0ENDA]
		ldr	r6, [ip, #IOMD_SD0ST]
		tst	r6, #DMA_ST_OFL
		bne	1f
		tst	r7, #DMA_ST_AB
		strne	r4, [ip, #IOMD_SD0CURB]
		streq	r5, [ip, #IOMD_SD0CURA]
		strne	r2, [ip, #IOMD_SD0ENDB]
		streq	r2, [ip, #IOMD_SD0ENDA]
1:		teq	r8, #0
		mov	r0, #0x10
		strneb	r0, [ip, #IOMD_SD0CR]
		teqeq	r1, #0
		ldmfd	sp!, {r4 - r9, lr}
		moveq	pc, r3				@ Call interrupt routine
		mov	pc, lr

		.data
		.globl	SYMBOL_NAME(dma_interrupt)
SYMBOL_NAME(dma_interrupt):
		.long	0
		.globl	SYMBOL_NAME(dma_pbuf)
SYMBOL_NAME(dma_pbuf):
		.long	0
		.long	0
		.globl	SYMBOL_NAME(dma_start)
SYMBOL_NAME(dma_start):
		.long	0
		.globl	SYMBOL_NAME(dma_count)
SYMBOL_NAME(dma_count):
		.long	0
		.globl	SYMBOL_NAME(dma_buf)
SYMBOL_NAME(dma_buf):
		.long	0
		.long	0
		.globl	SYMBOL_NAME(vidc_filler)
SYMBOL_NAME(vidc_filler):
		.long	SYMBOL_NAME(vidc_fill_noaudio)
		.globl	SYMBOL_NAME(dma_bufsize)
SYMBOL_NAME(dma_bufsize):
		.long	0x1000
