/*
 * linux/arch/arm/kernel/entry-armv.S
 *
 * Copyright (C) 1996,1997,1998 Russell King.
 * ARM700 fix by Matthew Godbolt (linux-user@willothewisp.demon.co.uk)
 *
 * Low-level vector interface routines
 *
 * Note:  there is a StrongARM bug in the STMIA rn, {regs}^ instruction that causes
 * it to save wrong values...  Be aware!
 */
#include <linux/config.h> /* for CONFIG_ARCH_xxxx */
#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/errno.h>
#include <asm/hardware.h>
#include <asm/arch/irqs.h>

#include "../lib/constants.h"

		.text

#define PF_TRACESYS	0x20

@ Bad Abort numbers
@ -----------------
@
#define BAD_PREFETCH	0
#define BAD_DATA	1
#define BAD_ADDREXCPTN	2
#define BAD_IRQ		3
#define BAD_UNDEFINSTR	4

@ OS version number used in SWIs
@  RISC OS is 0
@  RISC iX is 8
@
#define OS_NUMBER	9

@
@ Stack format (ensured by USER_* and SVC_*)
@
#define S_FRAME_SIZE	72
#define S_OLD_R0	68
#define S_PSR		64
#define S_PC		60
#define S_LR		56
#define S_SP		52
#define S_IP		48
#define S_FP		44
#define S_R10		40
#define S_R9		36
#define S_R8		32
#define S_R7		28
#define S_R6		24
#define S_R5		20
#define S_R4		16
#define S_R3		12
#define S_R2		8
#define S_R1		4
#define S_R0		0

#ifdef IOC_BASE
/* IOC / IOMD based hardware */
		.equ	ioc_base_high, IOC_BASE & 0xff000000
		.equ	ioc_base_low, IOC_BASE & 0x00ff0000
		.macro	disable_fiq
		mov	r12, #ioc_base_high
		.if	ioc_base_low
		orr	r12, r12, #ioc_base_low
		.endif
		strb	r12, [r12, #0x38]	@ Disable FIQ register
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base
		mov	r4, #ioc_base_high		@ point at IOC
		.if	ioc_base_low
		orr	r4, r4, #ioc_base_low
		.endif
		ldrb	\irqstat, [r4, #0x24]		@ get high priority first
		adr	\base, irq_prio_h
		teq	\irqstat, #0
#ifdef IOMD_BASE
		ldreqb	\irqstat, [r4, #0x1f4]		@ get dma
		adreq	\base, irq_prio_d
		teqeq	\irqstat, #0
#endif
		ldreqb	\irqstat, [r4, #0x14]		@ get low priority
		adreq	\base, irq_prio_l

		teq	\irqstat, #0
		ldrneb	\irqnr, [\base, \irqstat]	@ get IRQ number
		.endm

/*
 * Interrupt table (incorporates priority)
 */
		.macro	irq_prio_table
irq_prio_l:	.byte	 0, 0, 1, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 4, 0, 1, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
#ifdef IOMD_BASE
irq_prio_d:	.byte	 0,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	20,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	23,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	23,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
#endif
irq_prio_h:	.byte	 0, 8, 9, 8,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	12, 8, 9, 8,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	14,14,14,14,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	14,14,14,14,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.endm

#elif defined(CONFIG_ARCH_EBSA110)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base
		mov	r4, #0xf3000000
		ldrb	\irqstat, [r4]			@ get interrupts
		adr	\base, irq_prio_ebsa110

		teq	\irqstat, #0
		ldrneb	\irqnr, [\base, \irqstat]	@ get IRQ number
		.endm

		.macro	irq_prio_table
irq_prio_ebsa110:
		.byte	 0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 2, 2, 2, 2
		.byte	 4, 4, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 2, 2, 2, 2
		.byte	 5, 5, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 2, 2, 2, 2
		.byte	 5, 5, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 2, 2, 2, 2

		.byte	 6, 6, 6, 6, 2, 2, 2, 2, 3, 3, 6, 6, 2, 2, 2, 2
		.byte	 6, 6, 6, 6, 2, 2, 2, 2, 3, 3, 6, 6, 2, 2, 2, 2
		.byte	 6, 6, 6, 6, 2, 2, 2, 2, 3, 3, 6, 6, 2, 2, 2, 2
		.byte	 6, 6, 6, 6, 2, 2, 2, 2, 3, 3, 6, 6, 2, 2, 2, 2

		.byte	 7, 0, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 2, 2, 2, 2
		.byte	 4, 4, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 2, 2, 2, 2
		.byte	 5, 5, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 2, 2, 2, 2
		.byte	 5, 5, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 2, 2, 2, 2

		.byte	 6, 6, 6, 6, 2, 2, 2, 2, 3, 3, 6, 6, 2, 2, 2, 2
		.byte	 6, 6, 6, 6, 2, 2, 2, 2, 3, 3, 6, 6, 2, 2, 2, 2
		.byte	 6, 6, 6, 6, 2, 2, 2, 2, 3, 3, 6, 6, 2, 2, 2, 2
		.byte	 6, 6, 6, 6, 2, 2, 2, 2, 3, 3, 6, 6, 2, 2, 2, 2
		.endm

#elif defined(CONFIG_ARCH_EBSA285)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base
		mov	r4, #0xfe000000
		ldr	\irqstat, [r4, #0x180]		@ get interrupts
		mov	\irqnr, #0
1001:		tst	\irqstat, #1
		addeq	\irqnr, \irqnr, #1
		moveq	\irqstat, \irqstat, lsr #1
		tsteq	\irqnr, #32
		beq	1001b
		teq	\irqnr, #32
		.endm

		.macro	irq_prio_table
		.endm

#elif defined(CONFIG_ARCH_NEXUSPCI)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base
		ldr	r4, =0xffe00000
		ldr	\irqstat, [r4, #0x180]		@ get interrupts
		mov	\irqnr, #0
1001:		tst	\irqstat, #1
		addeq	\irqnr, \irqnr, #1
		moveq	\irqstat, \irqstat, lsr #1
		tsteq	\irqnr, #32
		beq	1001b
		teq	\irqnr, #32
		.endm

		.macro	irq_prio_table
		.endm

#elif defined(CONFIG_ARCH_VNC)

		.macro	disable_fiq
		.endm

		.equ	pci_iack_high, PCI_IACK & 0xff000000
		.equ	pci_iack_low,  PCI_IACK & 0x00ff0000

		.macro	get_irqnr_and_base, irqnr, irqstat, base
		mov	r4, #IO_BASE_ARM_CSR
		ldr	\irqstat, [r4, #CSR_IRQ_STATUS]	@ just show us the unmasked ones

		@ run through hard priorities
		@ timer
		tst	\irqstat, #IRQ_MASK_TIMER0
		movne	\irqnr, #IRQ_TIMER0
		bne	1001f

		@ ether10
		tst	\irqstat, #IRQ_MASK_ETHER10
		movne	\irqnr, #IRQ_ETHER10
		bne	1001f

		@ ether100
		tst	\irqstat, #IRQ_MASK_ETHER100
		movne	\irqnr, #IRQ_ETHER100
		bne	1001f

		@ video compressor
		tst	\irqstat, #IRQ_MASK_VIDCOMP
		movne	\irqnr, #IRQ_VIDCOMP
		bne	1001f

		@ now try all the PIC sources
		@ determine whether we have an irq
		tst	\irqstat, #IRQ_MASK_EXTERN_IRQ
		beq	1002f
		mov	r4, #pci_iack_high
		orr	r4, r4, #pci_iack_low
		ldrb	\irqnr, [r4]			@ get the IACK byte
		b	1001f

1002:		@ PCI errors
		tst	\irqstat, #IRQ_MASK_PCI_ERR
		movne	\irqnr, #IRQ_PCI_ERR
		bne	1001f

		@ softint
		tst	\irqstat, #IRQ_MASK_SOFTIRQ
		movne	\irqnr, #IRQ_SOFTIRQ
		bne	1001f

		@ debug uart
		tst	\irqstat, #IRQ_MASK_UART_DEBUG
		movne	\irqnr, #IRQ_CONRX
		bne	1001f

		@ watchdog
		tst	\irqstat, #IRQ_MASK_WATCHDOG
		movne	\irqnr, #IRQ_WATCHDOG

1001:		@ If Z is set, then we will not enter an interrupt
		.endm

		.macro	irq_prio_table
		.endm

#else
#error Unknown architecture
#endif

/*============================================================================
 * For entry-common.S
 */

		.macro	save_user_regs
		sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ Calling r0 - r12
		add	r8, sp, #S_PC
		stmdb	r8, {sp, lr}^			@ Calling sp, lr
		mov	r7, r0
		mrs	r6, spsr
		mov	r5, lr
		stmia	r8, {r5, r6, r7}		@ Save calling PC, CPSR, OLD_R0
		.endm

		.macro	restore_user_regs
		mrs	r0, cpsr			@ disable IRQs
		orr	r0, r0, #I_BIT
		msr	cpsr, r0
		ldr	r0, [sp, #S_PSR]		@ Get calling cpsr
		msr	spsr, r0			@ save in spsr_svc
		ldmia	sp, {r0 - lr}^			@ Get calling r0 - lr
		mov	r0, r0
		add	sp, sp, #S_PC
		ldr	lr, [sp], #S_FRAME_SIZE - S_PC	@ Get PC and jump over PC, PSR, OLD_R0
		movs	pc, lr				@ return & move spsr_svc into cpsr
		.endm

		.macro	mask_pc, rd, rm
		.endm

		/* If we're optimising for StrongARM the resulting code won't 
		   run on an ARM7 and we can save a couple of instructions.  
									--pb */
#ifdef __ARM_ARCH_4__
		.macro	arm700_bug_check, instr, temp
		.endm
#else
		.macro	arm700_bug_check, instr, temp
		and	\temp, \instr, #0x0f000000	@ check for SWI
		teq	\temp, #0x0f000000
		bne	.Larm700bug
		.endm
#endif

		.macro	enable_irqs, temp
		mrs	\temp, cpsr
		bic	\temp, \temp, #I_BIT
		msr	cpsr, \temp
		.endm

		.macro	initialise_traps_extra
		mrs	r0, cpsr
		bic	r0, r0, #31
		orr	r0, r0, #0xd3
		msr	cpsr, r0
		.endm


#ifndef __ARM_ARCH_4__
.Larm700bug:	str	lr, [r8]
		ldr	r0, [sp, #S_PSR]		@ Get calling cpsr
		msr	spsr, r0
		ldmia	sp, {r0 - lr}^			@ Get calling r0 - lr
		mov	r0, r0
		add	sp, sp, #S_PC
		ldr	lr, [sp], #S_FRAME_SIZE - S_PC	@ Get PC and jump over PC, PSR, OLD_R0
		movs	pc, lr
#endif

		.macro	get_current_task, rd
		mov	\rd, sp, lsr #13
		mov	\rd, \rd, lsl #13
		.endm

		/*
		 * Like adr, but force SVC mode (if required)
		 */
		.macro	adrsvc, cond, reg, label
		adr\cond	\reg, \label
		.endm

/*=============================================================================
 * Address exception handler
 *-----------------------------------------------------------------------------
 * These aren't too critical.
 * (they're not supposed to happen, and won't happen in 32-bit mode).
 */

vector_addrexcptn:
		b	vector_addrexcptn

/*=============================================================================
 * Undefined FIQs
 *-----------------------------------------------------------------------------
 * Enter in FIQ mode, spsr = ANY CPSR, lr = ANY PC
 * MUST PRESERVE SVC SPSR, but need to switch to SVC mode to show our msg.
 * Basically to switch modes, we *HAVE* to clobber one register...  brain
 * damage alert!  I don't think that we can execute any code in here in any
 * other mode than FIQ...  Ok you can switch to another mode, but you can't
 * get out of that mode without clobbering one register.
 */
_unexp_fiq:	disable_fiq
		subs	pc, lr, #4

/*=============================================================================
 * Interrupt entry dispatcher
 *-----------------------------------------------------------------------------
 * Enter in IRQ mode, spsr = SVC/USR CPSR, lr = SVC/USR PC
 */
vector_IRQ:	@
		@ save mode specific registers
		@
		ldr	r13, .LCirq
		sub	lr, lr, #4
		str	lr, [r13]			@ save lr_IRQ
		mrs	lr, spsr
		str	lr, [r13, #4]			@ save spsr_IRQ
		@
		@ now branch to the relevent MODE handling routine
		@
		mrs	sp, cpsr			@ switch to SVC mode
		bic	sp, sp, #31
		orr	sp, sp, #0x13
		msr	spsr, sp
		and	lr, lr, #15
		cmp	lr, #4
		addlts	pc, pc, lr, lsl #2		@ Changes mode and branches
		b	__irq_invalid			@  4 - 15
		b	__irq_usr			@  0  (USR_26 / USR_32)
		b	__irq_invalid			@  1  (FIQ_26 / FIQ_32)
		b	__irq_invalid			@  2  (IRQ_26 / IRQ_32)
		b	__irq_svc			@  3  (SVC_26 / SVC_32)
/*
 *------------------------------------------------------------------------------------------------
 * Undef instr entry dispatcher - dispatches it to the correct handler for the processor mode
 *------------------------------------------------------------------------------------------------
 * Enter in UND mode, spsr = SVC/USR CPSR, lr = SVC/USR PC
 */
.LCirq:		.word	__temp_irq
.LCund:		.word	__temp_und
.LCabt:		.word	__temp_abt

vector_undefinstr:
		@
		@ save mode specific registers
		@
		ldr	r13, [pc, #.LCund - . - 8]
		str	lr, [r13]
		mrs	lr, spsr
		str	lr, [r13, #4]
		@
		@ now branch to the relevent MODE handling routine
		@
		mrs	sp, cpsr
		bic	sp, sp, #31
		orr	sp, sp, #0x13
		msr	spsr, sp
		and	lr, lr, #15
		cmp	lr, #4
		addlts	pc, pc, lr, lsl #2		@ Changes mode and branches
		b	__und_invalid			@  4 - 15
		b	__und_usr			@  0 (USR_26 / USR_32)
		b	__und_invalid			@  1 (FIQ_26 / FIQ_32)
		b	__und_invalid			@  2 (IRQ_26 / IRQ_32)
		b	__und_svc			@  3 (SVC_26 / SVC_32)
/*
 *------------------------------------------------------------------------------------------------
 * Prefetch abort dispatcher - dispatches it to the correct handler for the processor mode
 *------------------------------------------------------------------------------------------------
 * Enter in ABT mode, spsr = USR CPSR, lr = USR PC
 */
vector_prefetch:
		@
		@ save mode specific registers
		@
		sub	lr, lr, #4
		ldr	r13, .LCabt
		str	lr, [r13]
		mrs	lr, spsr
		str	lr, [r13, #4]
		@
		@ now branch to the relevent MODE handling routine
		@
		mrs	sp, cpsr
		bic	sp, sp, #31
		orr	sp, sp, #0x13
		msr	spsr, sp
		and	lr, lr, #15
		adds	pc, pc, lr, lsl #2		@ Changes mode and branches
		b	__pabt_invalid			@  4 - 15
		b	__pabt_usr			@  0  (USR_26 / USR_32)
		b	__pabt_invalid			@  1  (FIQ_26 / FIQ_32)
		b	__pabt_invalid			@  2  (IRQ_26 / IRQ_32)
		b	__pabt_invalid			@  3  (SVC_26 / SVC_32)
/*
 *------------------------------------------------------------------------------------------------
 * Data abort dispatcher - dispatches it to the correct handler for the processor mode
 *------------------------------------------------------------------------------------------------
 * Enter in ABT mode, spsr = USR CPSR, lr = USR PC
 */
vector_data:	@
		@ save mode specific registers
		@
		sub	lr, lr, #8
		ldr	r13, .LCabt
		str	lr, [r13]
		mrs	lr, spsr
		str	lr, [r13, #4]
		@
		@ now branch to the relevent MODE handling routine
		@
		mrs	sp, cpsr
		bic	sp, sp, #31
		orr	sp, sp, #0x13
		msr	spsr, sp
		and	lr, lr, #15
		cmp	lr, #4
		addlts	pc, pc, lr, lsl #2		@ Changes mode & branches
		b	__dabt_invalid			@  4 - 15
		b	__dabt_usr			@  0  (USR_26 / USR_32)
		b	__dabt_invalid			@  1  (FIQ_26 / FIQ_32)
		b	__dabt_invalid			@  2  (IRQ_26 / IRQ_32)
		b	__dabt_svc			@  3  (SVC_26 / SVC_32)

/*=============================================================================
 * Prefetch abort handler
 *-----------------------------------------------------------------------------
 */
pabtmsg:	.ascii	"Pabt: %08lX\n\0"
		.align
__pabt_usr:	sub	sp, sp, #S_FRAME_SIZE		@ Allocate frame size in one go
		stmia	sp, {r0 - r12}			@ Save r0 - r12
		add	r8, sp, #S_PC
		stmdb	r8, {sp, lr}^			@ Save sp_usr lr_usr
		ldr	r4, .LCabt
		ldmia	r4, {r5 - r7}			@ Get USR pc, cpsr
		stmia	r8, {r5 - r7}			@ Save USR pc, cpsr, old_r0

		mrs	r7, cpsr			@ Enable interrupts if they were
		bic	r7, r7, #I_BIT			@ previously
		msr	cpsr, r7
		mov	r0, r5				@ address (pc)
		mov	r1, sp				@ regs
		bl	SYMBOL_NAME(do_PrefetchAbort)	@ call abort handler
		teq	r0, #0				@ Does this still apply???
		bne	ret_from_exception		@ Return from exception
#ifdef DEBUG_UNDEF
		adr	r0, t
		bl	SYMBOL_NAME(printk)
#endif
		mov	r0, r5
		mov	r1, sp
		and	r2, r6, #31
		bl	SYMBOL_NAME(do_undefinstr)
		ldr	lr, [sp, #S_PSR]		@ Get USR cpsr
		msr	spsr, lr
		ldmia	sp, {r0 - pc}^			@ Restore USR registers

__pabt_invalid:	sub	sp, sp, #S_FRAME_SIZE		@ Allocate frame size in one go
		stmia	sp, {r0 - lr}			@ Save XXX r0 - lr
		mov	r7, r0				@ OLD R0
		ldr	r4, .LCabt
		ldmia	r4, {r5 - r7}			@ Get XXX pc, cpsr
		add	r4, sp, #S_PC
		stmia	r4, {r5 - r7}			@ Save XXX pc, cpsr, old_r0
		mov	r0, sp				@ Prefetch aborts are definitely *not*
		mov	r1, #BAD_PREFETCH		@ allowed in non-user modes.  We cant
		and	r2, r6, #31			@ recover from this problem.
		b	SYMBOL_NAME(bad_mode)

#ifdef DEBUG_UNDEF
t:		.ascii "*** undef ***\r\n\0"
		.align
#endif

/*=============================================================================
 * Data abort handler code
 *-----------------------------------------------------------------------------
 */
.LCprocfns:	.word	SYMBOL_NAME(processor)

__dabt_usr:	sub	sp, sp, #S_FRAME_SIZE		@ Allocate frame size in one go
		stmia	sp, {r0 - r12}			@ save r0 - r12
		add	r3, sp, #S_PC
		stmdb	r3, {sp, lr}^
		ldr	r0, .LCabt
		ldmia	r0, {r0 - r2}			@ Get USR pc, cpsr
		stmia	r3, {r0 - r2}			@ Save USR pc, cpsr, old_r0
		mov	fp, #0
		mrs	r2, cpsr			@ Enable interrupts if they were
		bic	r2, r2, #I_BIT			@ previously
		msr	cpsr, r2
		ldr	r2, .LCprocfns
		mov	lr, pc
		ldr	pc, [r2, #8]			@ call processor specific code
		mov	r3, sp
		bl	SYMBOL_NAME(do_DataAbort)
		b	ret_from_sys_call

__dabt_svc:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ save r0 - r12
		ldr	r2, .LCabt
		add	r0, sp, #S_FRAME_SIZE
		add	r5, sp, #S_SP
		mov	r1, lr
		ldmia	r2, {r2 - r4}			@ get pc, cpsr
		stmia	r5, {r0 - r4}			@ save sp_SVC, lr_SVC, pc, cpsr, old_ro
		tst	r3, #I_BIT
		mrseq	r0, cpsr			@ Enable interrupts if they were
		biceq	r0, r0, #I_BIT			@ previously
		msreq	cpsr, r0
		mov	r0, r2
		ldr	r2, .LCprocfns
		mov	lr, pc
		ldr	pc, [r2, #8]			@ call processor specific code
		mov	r3, sp
		bl	SYMBOL_NAME(do_DataAbort)
		ldr	r0, [sp, #S_PSR]
		msr	spsr, r0
		ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr

__dabt_invalid:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - lr}			@ Save SVC r0 - lr [lr *should* be intact]
		mov	r7, r0
		ldr	r4, .LCabt
		ldmia	r4, {r5, r6}			@ Get SVC pc, cpsr
		add	r4, sp, #S_PC
		stmia	r4, {r5, r6, r7}		@ Save SVC pc, cpsr, old_r0
		mov	r0, sp
		mov	r1, #BAD_DATA
		and	r2, r6, #31
		b	SYMBOL_NAME(bad_mode)

/*=============================================================================
 * Interrupt (IRQ) handler
 *-----------------------------------------------------------------------------
 */
__irq_usr:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ save r0 - r12
		add	r8, sp, #S_PC
		stmdb	r8, {sp, lr}^
		ldr	r4, .LCirq
		ldmia	r4, {r5 - r7}			@ get saved PC, SPSR
		stmia	r8, {r5 - r7}			@ save pc, psr, old_r0
1:		get_irqnr_and_base r0, r6, r5
		movne	r1, sp
		@
		@ routine called with r0 = irq number, r1 = struct pt_regs *
		@
		adrsvc	ne, lr, 1b
		bne	do_IRQ
		b	ret_with_reschedule

		irq_prio_table

__irq_svc:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ save r0 - r12
		mov	r6, lr
		ldr	r7, .LCirq
		ldmia	r7, {r7 - r9}
		add	r5, sp, #S_FRAME_SIZE
		add	r4, sp, #S_SP
		stmia	r4, {r5, r6, r7, r8, r9}	@ save sp_SVC, lr_SVC, pc, cpsr, old_ro
1:		get_irqnr_and_base r0, r6, r5
		movne	r1, sp
		@
		@ routine called with r0 = irq number, r1 = struct pt_regs *
		@
		adrsvc	ne, lr, 1b
		bne	do_IRQ
		ldr	r0, [sp, #S_PSR]
		msr	spsr, r0
		ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr

__irq_invalid:	sub	sp, sp, #S_FRAME_SIZE	@ Allocate space on stack for frame
		stmfd	sp, {r0 - lr}		@ Save r0 - lr
		mov	r7, #-1
		ldr	r4, .LCirq
		ldmia	r4, {r5, r6}		@ get saved pc, psr
		add	r4, sp, #S_PC
		stmia	r4, {r5, r6, r7}
		mov	fp, #0
		mov	r0, sp
		mov	r1, #BAD_IRQ
		b	SYMBOL_NAME(bad_mode)

/*=============================================================================
 * Undefined instruction handler
 *-----------------------------------------------------------------------------
 * Handles floating point instructions
 */
.LC2:		.word	SYMBOL_NAME(fp_enter)

__und_usr:	sub	sp, sp, #S_FRAME_SIZE		@ Allocate frame size in one go
		stmia	sp, {r0 - r12}			@ Save r0 - r12
		add	r8, sp, #S_PC
		stmdb	r8, {sp, lr}^			@ Save user r0 - r12
		ldr	r4, .LCund
		ldmia	r4, {r5 - r7}
		stmia	r8, {r5 - r7}			@ Save USR pc, cpsr, old_r0
		mov	fp, #0

		adrsvc	al, r9, ret_from_exception	@ r9  = normal FP return
		adrsvc	al, lr, fpundefinstr		@ lr  = undefined instr return

1:		get_current_task r10
		mov	r8, #1
		strb	r8, [r10, #TSK_USED_MATH]	@ set current->used_math
		add	r10, r10, #TSS_FPESAVE		@ r10 = workspace
		ldr	r4, .LC2
		ldr	pc, [r4]			@ Call FP module USR entry point

__und_svc:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ save r0 - r12
		mov	r6, lr
		ldr	r7, .LCund
		ldmia	r7, {r7 - r9}
		add	r5, sp, #S_FRAME_SIZE
		add	r4, sp, #S_SP
		stmia	r4, {r5 - r9}			@ save sp_SVC, lr_SVC, pc, cpsr, old_ro

		adrsvc	al, r9, 3f			@ r9  = normal FP return
		bl	1b				@ lr  = undefined instr return

		mov	r0, r5				@ unsigned long pc
		mov	r1, sp				@ struct pt_regs *regs
		bl	SYMBOL_NAME(do_undefinstr)

3:		ldr	lr, [sp, #S_PSR]		@ Get SVC cpsr
		msr	spsr, lr
		ldmia	sp, {r0 - pc}^			@ Restore SVC registers

fpundefinstr:	mov	r0, lr
		mov	r1, sp
		mrs	r4, cpsr			@ Enable interrupts
		bic	r4, r4, #I_BIT
		msr	cpsr, r4
		adrsvc	al, lr, ret_from_exception
		b	SYMBOL_NAME(do_undefinstr)

__und_invalid:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - lr}
		mov	r7, r0
		ldr	r4, .LCund
		ldmia	r4, {r5, r6}			@ Get UND/IRQ/FIQ/ABT pc, cpsr
		add	r4, sp, #S_PC
		stmia	r4, {r5, r6, r7}		@ Save UND/IRQ/FIQ/ABT pc, cpsr, old_r0
		mov	r0, sp				@ struct pt_regs *regs
		mov	r1, #BAD_UNDEFINSTR		@ int reason
		and	r2, r6, #31			@ int mode
		b	SYMBOL_NAME(bad_mode)		@ Does not ever return...

/* We get here if an undefined instruction happens and the floating
 * point emulator is not present.  If the offending instruction was
 * a WFS, we just perform a normal return as if we had emulated the
 * operation.  This is a hack to allow some basic userland binaries
 * to run so that the emulator module proper can be loaded. --philb
 */
fpe_not_present:
		adr	r10, wfs_mask_data
		ldmia	r10, {r4, r5, r6, r7, r8}
		ldr	r10, [sp, #S_PC]		@ Load PC
		sub	r10, r10, #4
		mask_pc	r10, r10
		ldrt	r10, [r10]			@ get instruction
		and	r5, r10, r5
		teq	r5, r4				@ Is it WFS?
		moveq	pc, r9
		and	r5, r10, r8
		teq	r5, r6				@ Is it LDF/STF on sp or fp?
		teqne	r5, r7
		movne	pc, lr
		tst	r10, #0x00200000		@ Does it have WB
		moveq	pc, r9
		and	r4, r10, #255			@ get offset
		and	r6, r10, #0x000f0000
		tst	r10, #0x00800000		@ +/-
		rsbeq	r4, r4, #0
		ldr	r5, [sp, r6, lsr #14]		@ Load reg
		add	r5, r5, r4, lsl #2
		str	r5, [sp, r6, lsr #14]		@ Save reg
		mov	pc, r9

wfs_mask_data:	.word	0x0e200110			@ WFS
		.word	0x0fff0fff
		.word	0x0d0d0100			@ LDF [sp]/STF [sp]
		.word	0x0d0b0100			@ LDF [fp]/STF [fp]
		.word	0x0f0f0f00

#include "entry-common.S"

		.data

__temp_irq:	.word	0				@ saved lr_irq
		.word	0				@ saved spsr_irq
		.word	-1				@ old_r0
__temp_und:	.word	0				@ Saved lr_und
		.word	0				@ Saved spsr_und
		.word	-1				@ old_r0
__temp_abt:	.word	0				@ Saved lr_abt
		.word	0				@ Saved spsr_abt
		.word	-1				@ old_r0
