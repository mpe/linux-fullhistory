/* 
 * linux/arch/arm/boot/compressed/head-sa1100.S
 * 
 * Copyright (C) 1999 Nicolas Pitre <nico@cam.org>
 * 
 * SA1100 specific tweaks.  This is merged with head.S by the linker.
 */

#include <linux/config.h>

#ifndef CONFIG_ARCH_SA1100
#error What am I doing here...
#endif

		.section        ".start", #alloc, #execinstr

__SA1100_start:

		@ Preserve r0/r1 i.e. kernel entry values
		mov	r8, r0
		mov	r9, r1

#if	defined( CONFIG_SA1100_ASSABET ) || \
	defined( CONFIG_SA1100_BRUTUS ) || \
	defined( CONFIG_SA1100_THINCLIENT )
@ Booting from Angel -- need to enter SVC mode
#define angel_SWIreason_EnterSVC 0x17   /* from arm.h, in angel source */
#define angel_SWI_ARM (0x123456)
		mov	r0, #angel_SWIreason_EnterSVC
		swi	#angel_SWI_ARM

		@ turn off interrupts to prevent the angel from running
		mrs	r0, cpsr
		orr	r0, r0, #0xc0
		msr	cpsr_c, r0
#endif

#ifdef CONFIG_SA1100_VICTOR
		@ Copy cmdline to 0xc0000000
		mov	r1, #0xc0000000
		cmp	r0, #0
		moveq	r2, #0
1:		ldrneb	r2, [r0], #1
		cmpne	r2, #0
		strb	r2, [r1], #1
		bne	1b
#endif

		@ Data cache might be active.
		@ Be sure to flush kernel binary out of the cache,
		@ whatever state it is, before it is turned off.
		@ This is done by fetching through currently executed
		@ memory to be sure we hit the same cache.
		bic	r2, pc, #0x1f
		add	r3, r2, #0x4000		@ 16 kb is quite enough...
1:		ldr	r0, [r2], #32
		teq	r2, r3
		bne	1b
		mcr	p15, 0, r0, c7, c10, 4	@ drain WB
		mcr	p15, 0, r0, c7, c7, 0	@ flush I & D caches

		@ disabling MMU and caches
		mrc	p15, 0, r0, c1, c0, 0	@ read control reg
		bic	r0, r0, #0x0d		@ clear WB, DC, MMU
		bic	r0, r0, #0x1000		@ clear Icache
		mcr	p15, 0, r0, c1, c0, 0

#if	defined( CONFIG_SA1100_ASSABET ) || \
	defined( CONFIG_SA1100_BRUTUS )
		@ Initialize UART1 for early serial communication
		@ since UART3 is used by angelboot.  It is routed to
		@ alternate GPIO functions on Brutus.
		b	1f

#ifdef CONFIG_SA1100_BRUTUS
#define ALT_UART
#endif

GPIO_BASE:	.long	0x90040000
#define GPDR	0x04
#define GAFR	0x1c

PPC_BASE:	.long	0x90060000
#define PPAR	0x08

UART1_BASE:	.long	0x80010000
#define UTCR0           0x00
#define UTCR1           0x04
#define UTCR2           0x08
#define UTCR3           0x0c
#define UTSR0           0x1c
#define UTSR1           0x20

#define BAUD_DIV_230400	0x000
#define BAUD_DIV_115200	0x001
#define BAUD_DIV_57600	0x003
#define BAUD_DIV_38400	0x005
#define BAUD_DIV_19200	0x00b
#define BAUD_DIV_9600	0x017
#define BAUD_DIV	BAUD_DIV_9600

1:		
#ifdef ALT_UART
		ldr	r0, GPIO_BASE
		ldr	r1, [r0, #GPDR]
		bic	r1, r1, #1<<15
		orr	r1, r1, #1<<14
		str	r1, [r0, #GPDR]
		ldr	r1, [r0, #GAFR]
		orr	r1, r1, #(1<<15)|(1<<14)
		str	r1, [r0, #GAFR]
		ldr	r0, PPC_BASE
		ldr	r1, [r0, #PPAR]
		orr	r1, r1, #1<<12
		str	r1, [r0, #PPAR]
#endif
		ldr	r0, UART1_BASE
1:		ldr	r1, [r0, #UTSR1]
		tst	r1, #1<<0	@ TBY
		bne	1b
		mov	r1, #0
		str	r1, [r0, #UTCR3]
		mov	r1, #0x08	@ 8N1
		str	r1, [r0, #UTCR0]
		mov	r1, #BAUD_DIV
		str	r1, [r0, #UTCR2]
		mov	r1, r1, lsr #8
		str	r1, [r0, #UTCR1]
		mov	r1, #0x03	@ RXE + TXE
		str	r1, [r0, #UTCR3]
		mov	r1, #0xff	@ flush status reg
		str	r1, [r0, #UTSR0]
#endif

		@ Restore initial r0/r1
		mov	r0, r8
		mov	r1, r9
