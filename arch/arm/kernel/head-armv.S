/*
 * linux/arch/arm/kernel/head32.S
 *
 * Copyright (C) 1994, 1995, 1996, 1997 Russell King
 *
 * Kernel 32 bit startup code for ARM6 / ARM7 / StrongARM
 */
#include <linux/config.h>
#include <linux/linkage.h>

#define DEBUG

		.text
		.align

		.globl	SYMBOL_NAME(swapper_pg_dir)
		.equ	SYMBOL_NAME(swapper_pg_dir),	0xc0004000

		.globl	__stext
/*
 * Entry point and restart point.  Entry *must* be called with r0 == 0,
 * MMU off.  Note! These should be unique!!! Please read Documentation/ARM-README
 * for more information.
 *
 *  r1 = 0 -> ebsa110  (Ram @ 0x00000000)
 *  r1 = 1 -> RPC      (Ram @ 0x10000000)
 *  r1 = 2 -> ebsit    (???)
 *  r1 = 3 -> nexuspci
 *  r1 = 4 -> ebsa285  (Ram @ 0x00000000)
 */
ENTRY(stext)
ENTRY(_stext)
__entry:
		teq	r0, #0					@ check for illegal entry...
		bne	.Lerror					@ loop indefinitely
		cmp	r1, #5					@ Unknown machine architecture
		bge	.Lerror
@
@ First thing to do is to get the page tables set up so that we can call the kernel
@ in the correct place.  This is relocatable code...
@
		mrc	p15, 0, r9, c0, c0			@ get Processor ID
@
@ Read processor ID register (CP#15, CR0).
@ NOTE: ARM2 & ARM250 cause an undefined instruction exception...
@ Values are:
@		XX01XXXX = ARMv4 architecture (StrongARM)
@		XX00XXXX = ARMv3 architecture
@		4156061X = ARM 610
@ 		4156030X = ARM 3
@		4156025X = ARM 250
@		4156020X = ARM 2
@
		adr	r10, .LCProcTypes
1:		ldmia	r10!, {r5, r6, r8}			@ Get Set, Mask, MMU Flags
		teq	r5, #0					@ End of list?
		beq	.Lerror
		eor	r5, r5, r9
		tst	r5, r6
		addne	r10, r10, #8
		bne	1b

		adr	r4, .LCMachTypes
		add	r4, r4, r1, lsl #4
		ldmia	r4, {r4, r5, r6}
/*
 * r4 = page dir in physical ram
 * r5 = physical address of start of RAM
 * r6 = I/O address
 */
		mov	r0, r4
		mov	r1, #0
		add	r2, r0, #0x4000
1:		str	r1, [r0], #4				@ Clear page table
		teq	r0, r2
		bne	1b
/*
 * Add enough entries to allow the kernel to be called.
 * It will sort out the real mapping in paging_init.
 * We map in 2MB of memory into 0xC0000000 - 0xC0200000
 */
		add	r0, r4, #0x3000
		mov	r1, #0x0000000c				@ SECT_CACHEABLE | SECT_BUFFERABLE
		orr	r1, r1, r8
		add	r1, r1, r5
		str	r1, [r0], #4
		add	r1, r1, #1 << 20
		str	r1, [r0], #4
		add	r1, r1, #1 << 20
@
@ Map in IO space
@
		add	r0, r4, #0x3800
		orr	r1, r6, r8
		add	r2, r0, #0x0800
1:		str	r1, [r0], #4
		add	r1, r1, #1 << 20
		teq	r0, r2
		bne	1b
@
@ Map in screen at 0x02000000 & SCREEN2_BASE
@
		teq	r5, #0
		addne	r0, r4, #0x80				@ 02000000
		movne	r1, #0x02000000
		orrne	r1, r1, r8
		strne	r1, [r0]
		addne	r0, r4, #0x3600				@ d8000000
		strne	r1, [r0]
@
@ The following should work on both v3 and v4 implementations
@
		mov	lr, pc
		mov	pc, r10					@ Call processor flush (returns ctrl reg)
		adr	r5, __entry
		sub	r10, r10, r5				@ Make r10 PIC
		ldr	lr, .Lbranch
		mcr	p15, 0, r0, c1, c0			@ Enable MMU & caches.  In 3 instructions
								@ we lose this page!
		mov	pc, lr

.Lerror:	mov	r0, #0x02000000
		mov	r1, #0x11
		orr	r1, r1, r1, lsl #8
		orr	r1, r1, r1, lsl #16
		str	r1, [r0], #4
		str	r1, [r0], #4
		str	r1, [r0], #4
		str	r1, [r0], #4
		b	.Lerror

.Lbranch:	.long	.Lalready_done_mmap			@ Real address of routine

		@ EBSA110 (pg dir phys, phys ram start, phys i/o)
.LCMachTypes:	.long	SYMBOL_NAME(swapper_pg_dir) - 0xc0000000	@ Address of page tables (physical)
		.long	0					@ Address of RAM
		.long	0xe0000000				@ I/O address
		.long	0

		@ RPC
		.long	SYMBOL_NAME(swapper_pg_dir) - 0xc0000000 + 0x10000000
		.long	0x10000000
		.long	0x03000000
		.long	0

		@ EBSIT ???
		.long	SYMBOL_NAME(swapper_pg_dir) - 0xc0000000
		.long	0
		.long	0xe0000000
		.long	0

		@ NexusPCI
		.long	SYMBOL_NAME(swapper_pg_dir) - 0xc0000000 + 0x40000000
		.long	0x40000000
		.long	0x10000000
		.long	0

		@ EBSA285
		.long	SYMBOL_NAME(swapper_pg_dir) - 0xc0000000	@ Address of page tables (physical)
		.long	0					@ Address of RAM
		.long	0x24000000				@ I/O base address (0x42000000 -> 0xFE000000)
		.long	0



.LCProcTypes:	@ ARM6 / 610
		.long	0x41560600
		.long	0xffffff00
		.long	0x00000c12
		b	.Larmv3_flush_early			@ arm v3 flush & ctrl early setup
		mov	pc, lr

		@ ARM7 / 710
		.long	0x41007000
		.long	0xfffff000
		.long	0x00000c12
		b	.Larmv3_flush_late			@ arm v3 flush & ctrl late setup
		mov	pc, lr

		@ StrongARM
		.long	0x4401a100
		.long	0xfffffff0
		.long	0x00000c02
		b	.Larmv4_flush_early
		b	.Lsa_fastclock

		.long	0

.LC0:		.long	SYMBOL_NAME(__bss_start)
		.long	SYMBOL_NAME(arm_id)
		.long	SYMBOL_NAME(_end)
		.long	SYMBOL_NAME(init_task_union)+8192
		.align

.Larmv3_flush_early:
		mov	r0, #0
		mcr	p15, 0, r0, c7, c0			@ flush caches on v3
		mcr	p15, 0, r0, c5, c0			@ flush TLBs on v3
		mcr	p15, 0, r4, c2, c0			@ load page table pointer
		mov	r0, #0x1f				@ Domains 0, 1 = client
		mcr	p15, 0, r0, c3, c0			@ load domain access register
		mov	r0, #0x3d				@ ....S..DPWC.M
		orr	r0, r0, #0x100
		mov	pc, lr

.Larmv3_flush_late:
		mov	r0, #0
		mcr	p15, 0, r0, c7, c0			@ flush caches on v3
		mcr	p15, 0, r0, c5, c0			@ flush TLBs on v3
		mcr	p15, 0, r4, c2, c0			@ load page table pointer
		mov	r0, #0x1f				@ Domains 0, 1 = client
		mcr	p15, 0, r0, c3, c0			@ load domain access register
		mov	r0, #0x7d				@ ....S.LDPWC.M
		orr	r0, r0, #0x100
		mov	pc, lr

.Larmv4_flush_early:
		mov	r0, #0
		mcr	p15, 0, r0, c7, c7			@ flush I,D caches on v4
		mcr	p15, 0, r0, c7, c10, 4			@ drain write buffer on v4
		mcr	p15, 0, r0, c8, c7			@ flush I,D TLBs on v4
		mcr	p15, 0, r4, c2, c0			@ load page table pointer
		mov	r0, #0x1f				@ Domains 0, 1 = client
		mcr	p15, 0, r0, c3, c0			@ load domain access register
		mrc	p15, 0, r0, c1, c0			@ get control register v4
		bic	r0, r0, #0x0e00
		bic	r0, r0, #0x0002
		orr	r0, r0, #0x003d				@ I...S..DPWC.M
		orr	r0, r0, #0x1100				@ v4 supports separate I cache
		mov	pc, lr

.Lsa_fastclock:	mcr	p15, 0, r4, c15, c1, 2			@ Enable clock switching
		mov	pc, lr

.Lalready_done_mmap:
		adr	r5, __entry				@ Add base back in
		add	r10, r10, r5
		adr	r5, .LC0
		ldmia	r5, {r5, r6, r8, sp}			@ Setup stack
		mov	r4, #0
1:		cmp	r5, r8					@ Clear BSS
		strcc	r4, [r5],#4
		bcc	1b

		str	r9, [r6]				@ Save processor ID
		mov	lr, pc
		add	pc, r10, #4				@ Call post-processor init
		mov	fp, #0
		b	SYMBOL_NAME(start_kernel)

#ifdef DEBUG
/*
 * Some debugging routines (useful if you've got MM problems and
 * printk isn't working).  For DEBUGGING ONLY!!!
 */
#if defined(CONFIG_ARCH_RPC)
		.macro	addruart,rx
		mov	\rx, #0xe0000000
		orr	\rx, \rx, #0x00010000
		orr	\rx, \rx, #0x00000fe0
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		.macro	busyuart,rd,rx
1002:		ldrb	\rd, [\rx, #0x14]
		and	\rd, \rd, #0x60
		bne	1002b
		.endm

		.macro	waituart,rd,rx
1001:		ldrb	\rd, [\rx, #0x18]
		tst	\rd, #0x10
		beq	1001b
		.endm

#elif defined(CONFIG_ARCH_EBSA110)
		.macro	addruart,rx
		mov	\rx, #0xf0000000
		orr	\rx, \rx, #0x00000be0
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		.macro	busyuart,rd,rx
1002:		ldrb	\rd, [\rx, #0x14]
		and	\rd, \rd, #0x60
		bne	1002b
		.endm

		.macro	waituart,rd,rx
1001:		ldrb	\rd, [\rx, #0x18]
		tst	\rd, #0x10
		beq	1001b
		.endm

#elif defined(CONFIG_ARCH_EBSA285)
		.macro	addruart,rx
		mov	\rx, #0xfe000000
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x160]	@ UARTDR
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #0x178]	@ UARTFLG
		tst	\rd, #1 << 3
		bne	1001b
		.endm

		.macro	waituart,rd,rx
		.endm

#elif defined(CONFIG_ARCH_NEXUSPCI)
		.macro	addruart,rx
		ldr	\rx, =0xfff00000
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0xc]
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #0x4]
		tst	\rd, #1 << 0
		bne	1001b
		.endm

		.macro	waituart,rd,rx
		.endm
#else
#error Unknown architecture
#endif

/*
 * Useful debugging routines
 */
ENTRY(printhex8)
		mov	r1, #8
		b	printhex

ENTRY(printhex4)
		mov	r1, #4
		b	printhex

ENTRY(printhex2)
		mov	r1, #2
printhex:	ldr	r2, =hexbuf
		add	r3, r2, r1
		mov	r1, #0
		strb	r1, [r3]
1:		and	r1, r0, #15
		mov	r0, r0, lsr #4
		cmp	r1, #10
		addlt	r1, r1, #'0'
		addge	r1, r1, #'a' - 10
		strb	r1, [r3, #-1]!
		teq	r3, r2
		bne	1b
		mov	r0, r2

ENTRY(printascii)
		addruart r3
		b	2f
1:		waituart r2, r3
		senduart r1, r3
		busyuart r2, r3
		teq	r1, #'\n'
		moveq	r1, #'\r'
		beq	1b
2:		teq	r0, #0
		ldrneb	r1, [r0], #1
		teqne	r1, #0
		bne	1b
		mov	pc, lr

ENTRY(printch)
		addruart r3
		mov	r1, r0
		mov	r0, #0
		b	1b

		.ltorg

		.bss
hexbuf:		.space 16

#endif

		.text
		.align	13
ENTRY(this_must_match_init_task)
