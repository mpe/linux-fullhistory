/*
 *  arch/ppc/kernel/head.S
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *
 *  This file contains the low-level support and setup for the
 *  PowerPC platform, including trap and interrupt dispatch.
 *  Also included here is low-level thread/task switch support.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *	
 */

#include "ppc_asm.tmpl"
#include "ppc_defs.h"
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <linux/sys.h>
#include <linux/errno.h>
#include <linux/config.h>

#ifdef CONFIG_APUS
/* At CYBERBASEp we'll find the following sum:
 * -KERNELBASE+CyberStormMemoryBase
 */
#define CYBERBASEp (0xfff00000)
#endif

/* optimization for 603 to load the tlb directly from the linux table */
#define NO_RELOAD_HTAB 1
	
CACHE_LINE_SIZE = 32
LG_CACHE_LINE_SIZE = 5

#define TOPHYS(x)	(x - KERNELBASE)

/*
 * Macros for storing registers into and loading registers from
 * exception frames.
 */
#define SAVE_GPR(n, base)	stw	n,GPR0+4*(n)(base)
#define SAVE_2GPRS(n, base)	SAVE_GPR(n, base); SAVE_GPR(n+1, base)
#define SAVE_4GPRS(n, base)	SAVE_2GPRS(n, base); SAVE_2GPRS(n+2, base)
#define SAVE_8GPRS(n, base)	SAVE_4GPRS(n, base); SAVE_4GPRS(n+4, base)
#define SAVE_10GPRS(n, base)	SAVE_8GPRS(n, base); SAVE_2GPRS(n+8, base)
#define REST_GPR(n, base)	lwz	n,GPR0+4*(n)(base)
#define REST_2GPRS(n, base)	REST_GPR(n, base); REST_GPR(n+1, base)
#define REST_4GPRS(n, base)	REST_2GPRS(n, base); REST_2GPRS(n+2, base)
#define REST_8GPRS(n, base)	REST_4GPRS(n, base); REST_4GPRS(n+4, base)
#define REST_10GPRS(n, base)	REST_8GPRS(n, base); REST_2GPRS(n+8, base)

#define SAVE_FPR(n, base)	stfd	n,TSS_FPR0+8*(n)(base)
#define SAVE_2FPRS(n, base)	SAVE_FPR(n, base); SAVE_FPR(n+1, base)
#define SAVE_4FPRS(n, base)	SAVE_2FPRS(n, base); SAVE_2FPRS(n+2, base)
#define SAVE_8FPRS(n, base)	SAVE_4FPRS(n, base); SAVE_4FPRS(n+4, base)
#define SAVE_16FPRS(n, base)	SAVE_8FPRS(n, base); SAVE_8FPRS(n+8, base)
#define SAVE_32FPRS(n, base)	SAVE_16FPRS(n, base); SAVE_16FPRS(n+16, base)
#define REST_FPR(n, base)	lfd	n,TSS_FPR0+8*(n)(base)
#define REST_2FPRS(n, base)	REST_FPR(n, base); REST_FPR(n+1, base)
#define REST_4FPRS(n, base)	REST_2FPRS(n, base); REST_2FPRS(n+2, base)
#define REST_8FPRS(n, base)	REST_4FPRS(n, base); REST_4FPRS(n+4, base)
#define REST_16FPRS(n, base)	REST_8FPRS(n, base); REST_8FPRS(n+8, base)
#define REST_32FPRS(n, base)	REST_16FPRS(n, base); REST_16FPRS(n+16, base)

#define SYNC \
	sync; \
	isync

/* This instruction is not implemented on the PPC 603 or 601 */
#define tlbia \
	li	r4,128; \
	mtctr	r4; \
	lis	r4,0xC000; \
0:	tlbie	r4; \
	addi	r4,r4,0x1000; \
	bdnz	0b

#define LOAD_BAT(n, offset, reg, RA, RB) \
	lwz	RA,offset+0(reg); \
	lwz	RB,offset+4(reg);	\
	mtspr	IBAT##n##U,RA;	\
	mtspr	IBAT##n##L,RB;	\
	lwz	RA,offset+8(reg);	\
	lwz	RB,offset+12(reg);	\
	mtspr	DBAT##n##U,RA;	\
	mtspr	DBAT##n##L,RB

	.text
	.globl	_stext
_stext:

/*
 * _start is defined this way because the XCOFF loader in the OpenFirmware
 * on the powermac expects the entry point to be a procedure descriptor.
 */
	.text
	.globl	_start
_start:
	.long	TOPHYS(__start),0,0

/* PMAC
 * Enter here with the kernel text, data and bss loaded starting at
 * 0, running with virtual == physical mapping.
 * r5 points to the prom entry point (the client interface handler
 * address).  Address translation is turned on, with the prom
 * managing the hash table.  Interrupts are disabled.  The stack
 * pointer (r1) points to just below the end of the half-meg region
 * from 0x380000 - 0x400000, which is mapped in already.
 *
 * PREP
 * This is jumped to on prep systems right after the kernel is relocated
 * to its proper place in memory by the boot loader.  The expected layout
 * of the regs is:	
 *   r3: ptr to residual data
 *   r4: initrd_start or if no initrd then 0
 *   r5: initrd_end - unused if r4 is 0
 *   r6: Start of command line string
 *   r7: End of command line string
 *
 * This just gets a minimal mmu environment setup so we can call
 * start_here() to do the real work.
 * -- Cort	
 */
	
	.globl	__start
__start:

/*
 * We have to do any OF calls before we map ourselves to KERNELBASE,
 * because OF may have I/O devices mapped in in that area
 * (particularly on CHRP).
 */
	mr	r31,r3			/* save parameters */
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r29,r7
	bl	prom_init

/*
 * Use the first pair of BAT registers to map the 1st 16MB
 * of RAM to KERNELBASE.
 */
	mfspr	r9,PVR
	rlwinm	r9,r9,16,16,31		/* r9 = 1 for 601, 4 for 604 */
	cmpi	0,r9,1
	lis	r11,KERNELBASE@h
	bne	4f
	ori	r11,r11,4		/* set up BAT registers for 601 */
	li	r8,0x7f
	oris	r9,r11,0x800000@h	/* set up BAT reg for 2nd 8M */
	oris	r10,r8,0x800000@h	/* set up BAT reg for 2nd 8M */
	mtspr	IBAT1U,r9
	mtspr	IBAT1L,r10
	b	5f
4:	ori	r11,r11,0x1ff		/* set up BAT registers for 604 */
#ifndef CONFIG_APUS
	li	r8,2
#else
	lis	r8,CYBERBASEp@h
	lwz	r8,0(r8)
	addis	r8,r8,KERNELBASE@h
	addi	r8,r8,2
#endif
	mtspr	DBAT0U,r11
	mtspr	DBAT0L,r8
5:	mtspr	IBAT0U,r11
	mtspr	IBAT0L,r8
	isync
/*
 * we now have the 1st 16M of ram mapped with the bats.
 * prep needs the mmu to be turned on here, but pmac already has it on.
 * this shouldn't bother the pmac since it just gets turned on again
 * as we jump to our code at KERNELBASE. -- Cort
 */
	mfmsr	r0
	ori	r0,r0,MSR_DR|MSR_IR
	mtspr	SRR1,r0
	lis	r0,start_here@h
	ori	r0,r0,start_here@l
	mtspr	SRR0,r0
	SYNC
	rfi				/* enables MMU */
		
/*
 * GCC sometimes accesses words at negative offsets from the stack
 * pointer, although the SysV ABI says it shouldn't.  To cope with
 * this, we leave this much untouched space on the stack on exception
 * entry.
 */
#define STACK_UNDERHEAD	64
	
/*
 * Macros for storing registers into and loading registers from
 * exception frames.
 */
#define SAVE_GPR(n, base)	stw	n,GPR0+4*(n)(base)
#define SAVE_2GPRS(n, base)	SAVE_GPR(n, base); SAVE_GPR(n+1, base)
#define SAVE_4GPRS(n, base)	SAVE_2GPRS(n, base); SAVE_2GPRS(n+2, base)
#define SAVE_8GPRS(n, base)	SAVE_4GPRS(n, base); SAVE_4GPRS(n+4, base)
#define SAVE_10GPRS(n, base)	SAVE_8GPRS(n, base); SAVE_2GPRS(n+8, base)
#define REST_GPR(n, base)	lwz	n,GPR0+4*(n)(base)
#define REST_2GPRS(n, base)	REST_GPR(n, base); REST_GPR(n+1, base)
#define REST_4GPRS(n, base)	REST_2GPRS(n, base); REST_2GPRS(n+2, base)
#define REST_8GPRS(n, base)	REST_4GPRS(n, base); REST_4GPRS(n+4, base)
#define REST_10GPRS(n, base)	REST_8GPRS(n, base); REST_2GPRS(n+8, base)

#define SAVE_FPR(n, base)	stfd	n,TSS_FPR0+8*(n)(base)
#define SAVE_2FPRS(n, base)	SAVE_FPR(n, base); SAVE_FPR(n+1, base)
#define SAVE_4FPRS(n, base)	SAVE_2FPRS(n, base); SAVE_2FPRS(n+2, base)
#define SAVE_8FPRS(n, base)	SAVE_4FPRS(n, base); SAVE_4FPRS(n+4, base)
#define SAVE_16FPRS(n, base)	SAVE_8FPRS(n, base); SAVE_8FPRS(n+8, base)
#define SAVE_32FPRS(n, base)	SAVE_16FPRS(n, base); SAVE_16FPRS(n+16, base)
#define REST_FPR(n, base)	lfd	n,TSS_FPR0+8*(n)(base)
#define REST_2FPRS(n, base)	REST_FPR(n, base); REST_FPR(n+1, base)
#define REST_4FPRS(n, base)	REST_2FPRS(n, base); REST_2FPRS(n+2, base)
#define REST_8FPRS(n, base)	REST_4FPRS(n, base); REST_4FPRS(n+4, base)
#define REST_16FPRS(n, base)	REST_8FPRS(n, base); REST_8FPRS(n+8, base)
#define REST_32FPRS(n, base)	REST_16FPRS(n, base); REST_16FPRS(n+16, base)

#ifndef CONFIG_APUS
#define tophys(rd,rs,rt)	addis	rd,rs,-KERNELBASE@h
#define tovirt(rd,rs,rt)	addis	rd,rs,KERNELBASE@h
#else
#define tophys(rd,rs,rt)	 \
	lis	rt,CYBERBASEp@h; \
	lwz	rt,0(rt);	 \
	add	rd,rs,rt
#define tovirt(rd,rs,rt)	 \
	lis	rt,CYBERBASEp@h; \
	lwz	rt,0(rt);	 \
	sub	rd,rs,rt
#endif
	
/*
 * Exception entry code.  This code runs with address translation
 * turned off, i.e. using physical addresses.
 * We assume sprg3 has the physical address of the current
 * task's thread_struct.
 */
#define EXCEPTION_PROLOG	\
	mtspr	SPRG0,r20;	\
	mtspr	SPRG1,r21;	\
	mfcr	r20;		\
	mfspr	r21,SPRG2;		/* exception stack to use from */ \
	cmpwi	0,r21,0;		/* user mode or RTAS */ \
	bne	1f;		\
	tophys(r21,r1,r21);		/* use tophys(kernel sp) otherwise */ \
	subi	r21,r21,INT_FRAME_SIZE+STACK_UNDERHEAD;	/* alloc exc. frame */\
1:	stw	r20,_CCR(r21);		/* save registers */ \
	stw	r22,GPR22(r21);	\
	stw	r23,GPR23(r21);	\
	mfspr	r20,SPRG0;	\
	stw	r20,GPR20(r21);	\
	mfspr	r22,SPRG1;	\
	stw	r22,GPR21(r21);	\
	mflr	r20;		\
	stw	r20,_LINK(r21);	\
	mfctr	r22;		\
	stw	r22,_CTR(r21);	\
	mfspr	r20,XER;	\
	stw	r20,_XER(r21);	\
	mfspr	r22,SRR0;	\
	mfspr	r23,SRR1;	\
	stw	r0,GPR0(r21);	\
	stw	r1,GPR1(r21);	\
	stw	r2,GPR2(r21);	\
	stw	r1,0(r21);	\
	tovirt(r1,r21,r1);		/* set new kernel sp */	\
	SAVE_4GPRS(3, r21);
/*
 * Note: code which follows this uses cr0.eq (set if from kernel),
 * r21, r22 (SRR0), and r23 (SRR1).
 */

/*
 * Exception vectors.
 */
#define STD_EXCEPTION(n, label, hdlr)		\
	. = n;					\
label:						\
	EXCEPTION_PROLOG;			\
	addi	r3,r1,STACK_FRAME_OVERHEAD;	\
	li	r20,MSR_KERNEL;			\
	bl	transfer_to_handler; 		\
	.long	hdlr;				\
	.long	int_return

/* System reset */
	STD_EXCEPTION(0x100, Reset, UnknownException)

/* Machine check */
	STD_EXCEPTION(0x200, MachineCheck, MachineCheckException)

/* Data access exception */
	. = 0x300
DataAccess:
	EXCEPTION_PROLOG
	mfspr	r20,DSISR
	andis.	r0,r20,0xa470		/* weird error? */
	bne	1f			/* if not, try to put a PTE */
	mfspr	r3,DAR			/* into the hash table */
	rlwinm	r4,r23,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	rlwimi	r4,r20,32-23,29,29	/* DSISR_STORE -> _PAGE_RW */
	mfspr	r5,SPRG3		/* phys addr of TSS */
	bl	hash_page
1:	stw	r20,_DSISR(r21)
	mr	r5,r20
	mfspr	r4,DAR
	stw	r4,_DAR(r21)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	do_page_fault
	.long	int_return

/* Instruction access exception */
	. = 0x400
InstructionAccess:
	EXCEPTION_PROLOG
	andis.	r0,r23,0x4000		/* no pte found? */
	beq	1f			/* if so, try to put a PTE */
	mr	r3,r22			/* into the hash table */
	rlwinm	r4,r23,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	mr	r20,r23			/* SRR1 has reason bits */
	mfspr	r5,SPRG3		/* phys addr of TSS */
	bl	hash_page
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	mr	r4,r22
	mr	r5,r23
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	do_page_fault
	.long	int_return

/* External interrupt */
	STD_EXCEPTION(0x500, HardwareInterrupt, do_IRQ)

/* Alignment exception */
	. = 0x600
Alignment:
	EXCEPTION_PROLOG
	mfspr	r4,DAR
	stw	r4,_DAR(r21)
	mfspr	r5,DSISR
	stw	r5,_DSISR(r21)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	AlignmentException
	.long	int_return

/* Program check exception */
	. = 0x700
ProgramCheck:
	EXCEPTION_PROLOG
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	ProgramCheckException
	.long	int_return

/* Floating-point unavailable */
	. = 0x800
FPUnavailable:
	EXCEPTION_PROLOG
	bne	load_up_fpu		/* if from user, just load it up */
	li	r20,MSR_KERNEL
	bl	transfer_to_handler	/* if from kernel, take a trap */
	.long	KernelFP
	.long	int_return

	STD_EXCEPTION(0x900, Decrementer, timer_interrupt)
	STD_EXCEPTION(0xa00, Trap_0a, UnknownException)
	STD_EXCEPTION(0xb00, Trap_0b, UnknownException)

/* System call */
	. = 0xc00
SystemCall:
	EXCEPTION_PROLOG
	stw	r3,ORIG_GPR3(r21)
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	DoSyscall
	.long	int_return

/* Single step - not used on 601 */
	STD_EXCEPTION(0xd00, SingleStep, SingleStepException)

	STD_EXCEPTION(0xe00, Trap_0e, UnknownException)
	STD_EXCEPTION(0xf00, Trap_0f, UnknownException)

/*
 * Handle TLB miss for instruction on 603/603e.
 * Note: we get an alternate set of r0 - r3 to use automatically.
 */
	. = 0x1000
InstructionTLBMiss:
#ifdef NO_RELOAD_HTAB
/*
 * r0:	stored ctr
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	mfctr	r0
	/* Get PTE (linux-style) and check access */
	mfspr	r2,SPRG3
	lwz	r2,PG_TABLES(r2)
	tophys(r2,r2,r3)
	mfspr	r3,IMISS
	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	InstructionAddressInvalid	/* return if no mapping */
	tophys(r2,r2,r1)
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r1,0(r2)		/* get linux-style pte */
	/* setup access flags in r3 */
	mfmsr	r3
	rlwinm	r3,r3,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	ori	r3,r3,1			/* set _PAGE_PRESENT bit in access */
	andc.	r3,r3,r1		/* check access & ~permission */
	bne-	InstructionAddressInvalid /* return if access not permitted */
	ori	r1,r1,0x100		/* set _PAGE_ACCESSED in pte */
	stw	r1,0(r2)		/* update PTE (accessed bit) */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	/* this computation could be done better -- Cort */
	rlwinm	r3,r1,32-9,31,31	/* _PAGE_HWWRITE -> PP lsb */
	rlwimi	r1,r1,32-1,31,31	/* _PAGE_USER -> PP (both bits now) */
	ori	r3,r3,0xe04		/* clear out reserved bits */
	andc	r1,r1,r3		/* PP=2 or 0, when _PAGE_HWWRITE */
	mtspr	RPA,r1
	mfspr	r3,IMISS
	tlbli	r3
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	rfi	
#else
	mfctr	r0		/* Need to save this - CTR can't be touched! */
	mfspr	r2,HASH1	/* Get PTE pointer */
	mfspr	r3,ICMP		/* Partial item compare value */
00:	li	r1,8		/* 8 items / bucket */
	mtctr	r1
	subi	r2,r2,8		/* Preset pointer */
10:	lwzu	r1,8(r2)	/* Get next PTE */
	cmp	0,r1,r3		/* Found entry yet? */
	bdnzf	2,10b		/* Jump back if not, until CTR==0 */
	bne	30f		/* Try secondary hash if CTR==0 */
	lwz	r1,4(r2)	/* Get second word of entry */
20:	mtctr	r0		/* Restore CTR */
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	mfspr	r0,IMISS	/* Set to update TLB */
	mtspr	RPA,r1
	tlbli	r0
	rfi			/* All done */
/* Secondary hash */
30:	andi.	r1,r3,0x40	/* Already doing secondary hash? */
	bne	InstructionAddressInvalid /* Yes - item not in hash table */
	mfspr	r2,HASH2	/* Get hash table pointer */
	ori	r3,r3,0x40	/* Set secondary hash */
	b	00b		/* Try lookup again */
#endif /* NO_RELOAD_HTAB */
InstructionAddressInvalid:
	mfspr	r3,SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */
#ifdef NO_RELOAD_HTAB
	addis	r1,r1,0x2000
#else
	addis	r1,r1,0x4000	/* Set bit 1 -> PTE not found (in HTAB) */
#endif /* NO_RELOAD_HTAB */
	mtspr	DSISR,r1	/* (shouldn't be needed) */
	mtctr	r0		/* Restore CTR */
	andi.	r2,r3,0xFFFF	/* Clear upper bits of SRR1 */
	or	r2,r2,r1
	mtspr	SRR1,r2
	mfspr	r1,IMISS	/* Get failing address */
	rlwinm.	r2,r2,0,31,31	/* Check for little endian access */
	rlwimi	r2,r2,1,30,30	/* change 1 -> 3 */
	xor	r1,r1,r2
	mtspr	DAR,r1		/* Set fault address */
	mfmsr	r0		/* Restore "normal" registers */
	xoris	r0,r0,MSR_TGPR>>16
	mtcrf	0x80,r3		/* Restore CR0 */
	sync			/* Some chip revs have problems here... */
	mtmsr	r0
	b	InstructionAccess

/*
 * Handle TLB miss for DATA Load operation on 603/603e
 */
	. = 0x1100
DataLoadTLBMiss:
#ifdef NO_RELOAD_HTAB
/*
 * r0:	stored ctr
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	mfctr	r0
	/* Get PTE (linux-style) and check access */
	mfspr	r2,SPRG3
	lwz	r2,PG_TABLES(r2)		
	tophys(r2,r2,r3)
	mfspr	r3,DMISS
	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	DataAddressInvalid	/* return if no mapping */
	tophys(r2,r2,r1)
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r1,0(r2)		/* get linux-style pte */
	/* setup access flags in r3 */
	mfmsr	r3
	rlwinm	r3,r3,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	ori	r3,r3,1			/* set _PAGE_PRESENT bit in access */
	/* save r2 and use it as scratch for the andc. */	
	andc.	r3,r3,r1		/* check access & ~permission */
	bne-	DataAddressInvalid	/* return if access not permitted */
	ori	r1,r1,0x100		/* set _PAGE_ACCESSED in pte */
	stw	r1,0(r2)		/* update PTE (accessed bit) */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	/* this computation could be done better -- Cort */
	rlwinm	r3,r1,32-9,31,31	/* _PAGE_HWWRITE -> PP lsb */
	rlwimi	r1,r1,32-1,31,31	/* _PAGE_USER -> PP (both bits now) */
	ori	r3,r3,0xe04		/* clear out reserved bits */
	andc	r1,r1,r3		/* PP=2 or 0, when _PAGE_HWWRITE */
	mtspr	RPA,r1
	mfspr	r3,DMISS
	tlbld	r3
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	rfi	
#else
	mfctr	r0		/* Need to save this - CTR can't be touched! */
	mfspr	r2,HASH1	/* Get PTE pointer */
	mfspr	r3,DCMP		/* Partial item compare value */
00:	li	r1,8		/* 8 items / bucket */
	mtctr	r1
	subi	r2,r2,8		/* Preset pointer */
10:	lwzu	r1,8(r2)	/* Get next PTE */
	cmp	0,r1,r3		/* Found entry yet? */
	bdnzf	2,10b		/* Jump back if not, until CTR==0 */
	bne	30f		/* Try secondary hash if CTR==0 */
	lwz	r1,4(r2)	/* Get second word of entry */
20:	mtctr	r0		/* Restore CTR */
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	mfspr	r0,DMISS	/* Set to update TLB */
	mtspr	RPA,r1
	tlbld	r0
	rfi			/* All done */
/* Secondary hash */
30:	andi.	r1,r3,0x40	/* Already doing secondary hash? */
	bne	DataAddressInvalid /* Yes - item not in hash table */
	mfspr	r2,HASH2	/* Get hash table pointer */
	ori	r3,r3,0x40	/* Set secondary hash */
	b	00b		/* Try lookup again */
#endif /* NO_RELOAD_HTAB */
DataAddressInvalid:
	mfspr	r3,SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */
#ifdef NO_RELOAD_HTAB
	addis	r1,r1,0x2000
#else
	addis	r1,r1,0x4000	/* Set bit 1 -> PTE not found */
#endif /* NO_RELOAD_HTAB */
	mtspr	DSISR,r1
	mtctr	r0		/* Restore CTR */
	andi.	r2,r3,0xFFFF	/* Clear upper bits of SRR1 */
	mtspr	SRR1,r2
	mfspr	r1,DMISS	/* Get failing address */
	rlwinm.	r2,r2,0,31,31	/* Check for little endian access */
	beq	20f		/* Jump if big endian */
	xori	r1,r1,3
20:	mtspr	DAR,r1		/* Set fault address */
	mfmsr	r0		/* Restore "normal" registers */
	xoris	r0,r0,MSR_TGPR>>16
	mtcrf	0x80,r3		/* Restore CR0 */
	sync			/* Some chip revs have problems here... */
	mtmsr	r0
	b	DataAccess

/*
 * Handle TLB miss for DATA Store on 603/603e
 */
	. = 0x1200
DataStoreTLBMiss:
#ifdef NO_RELOAD_HTAB
/*
 * r0:	stored ctr
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	mfctr	r0
	/* Get PTE (linux-style) and check access */
	mfspr	r2,SPRG3
	lwz	r2,PG_TABLES(r2)		
	tophys(r2,r2,r3)
	mfspr	r3,DMISS
	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	DataAddressInvalid	/* return if no mapping */
	tophys(r2,r2,r1)
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r1,0(r2)		/* get linux-style pte */
	/* setup access flags in r3 */
	mfmsr	r3
	rlwinm	r3,r3,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	ori	r3,r3,0x5		/* _PAGE_PRESENT|_PAGE_RW */
	/* save r2 and use it as scratch for the andc. */	
	andc.	r3,r3,r1		/* check access & ~permission */
	bne-	DataAddressInvalid	/* return if access not permitted */
	ori	r1,r1,0x384		/* set _PAGE_ACCESSED|_PAGE_DIRTY|_PAGE_RW|_PAGE_HWWRITE in pte */
	stw	r1,0(r2)		/* update PTE (accessed bit) */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	/* this computation could be done better -- Cort */
	rlwinm	r3,r1,32-9,31,31	/* _PAGE_HWWRITE -> PP lsb */
	rlwimi	r1,r1,32-1,31,31	/* _PAGE_USER -> PP (both bits now) */
	ori	r3,r3,0xe04		/* clear out reserved bits */
	andc	r1,r1,r3		/* PP=2 or 0, when _PAGE_HWWRITE */
	mtspr	RPA,r1
	mfspr	r3,DMISS
	tlbld	r3
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	rfi	
#else	
	mfctr	r0		/* Need to save this - CTR can't be touched! */
	mfspr	r2,HASH1	/* Get PTE pointer */
	mfspr	r3,DCMP		/* Partial item compare value */
00:	li	r1,8		/* 8 items / bucket */
	mtctr	r1
	subi	r2,r2,8		/* Preset pointer */
10:	lwzu	r1,8(r2)	/* Get next PTE */
	cmp	0,r1,r3		/* Found entry yet? */
	bdnzf	2,10b		/* Jump back if not, until CTR==0 */
	bne	30f		/* Try secondary hash if CTR==0 */
	lwz	r1,4(r2)	/* Get second word of entry */
20:	mtctr	r0		/* Restore CTR */
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	mfspr	r0,DMISS	/* Set to update TLB */
	mtspr	RPA,r1
	tlbld	r0
	rfi			/* All done */	
/* Secondary hash */
30:	andi.	r1,r3,0x40	/* Already doing secondary hash? */
	bne	DataAddressInvalid /* Yes - item not in hash table */
	mfspr	r2,HASH2	/* Get hash table pointer */
	ori	r3,r3,0x40	/* Set secondary hash */
	b	00b			/* Try lookup again */
#endif /* NO_RELOAD_HTAB */
	

/* Instruction address breakpoint exception (on 603/604) */
	STD_EXCEPTION(0x1300, Trap_13, InstructionBreakpoint)

/* System management exception (603?) */
	STD_EXCEPTION(0x1400, Trap_14, UnknownException)

	STD_EXCEPTION(0x1500, Trap_15, UnknownException)
	STD_EXCEPTION(0x1600, Trap_16, UnknownException)
	STD_EXCEPTION(0x1700, Trap_17, UnknownException)
	STD_EXCEPTION(0x1800, Trap_18, UnknownException)
	STD_EXCEPTION(0x1900, Trap_19, UnknownException)
	STD_EXCEPTION(0x1a00, Trap_1a, UnknownException)
	STD_EXCEPTION(0x1b00, Trap_1b, UnknownException)
	STD_EXCEPTION(0x1c00, Trap_1c, UnknownException)
	STD_EXCEPTION(0x1d00, Trap_1d, UnknownException)
	STD_EXCEPTION(0x1e00, Trap_1e, UnknownException)
	STD_EXCEPTION(0x1f00, Trap_1f, UnknownException)

/* Run mode exception */
	STD_EXCEPTION(0x2000, RunMode, RunModeException)

	STD_EXCEPTION(0x2100, Trap_21, UnknownException)
	STD_EXCEPTION(0x2200, Trap_22, UnknownException)
	STD_EXCEPTION(0x2300, Trap_23, UnknownException)
	STD_EXCEPTION(0x2400, Trap_24, UnknownException)
	STD_EXCEPTION(0x2500, Trap_25, UnknownException)
	STD_EXCEPTION(0x2600, Trap_26, UnknownException)
	STD_EXCEPTION(0x2700, Trap_27, UnknownException)
	STD_EXCEPTION(0x2800, Trap_28, UnknownException)
	STD_EXCEPTION(0x2900, Trap_29, UnknownException)
	STD_EXCEPTION(0x2a00, Trap_2a, UnknownException)
	STD_EXCEPTION(0x2b00, Trap_2b, UnknownException)
	STD_EXCEPTION(0x2c00, Trap_2c, UnknownException)
	STD_EXCEPTION(0x2d00, Trap_2d, UnknownException)
	STD_EXCEPTION(0x2e00, Trap_2e, UnknownException)
	STD_EXCEPTION(0x2f00, Trap_2f, UnknownException)

	. = 0x3000

/*
 * This code finishes saving the registers to the exception frame
 * and jumps to the appropriate handler for the exception, turning
 * on address translation.
 */
	.globl	transfer_to_handler
transfer_to_handler:
	stw	r22,_NIP(r21)
	stw	r23,_MSR(r21)
	SAVE_GPR(7, r21)
	SAVE_4GPRS(8, r21)
	SAVE_8GPRS(12, r21)
	SAVE_8GPRS(24, r21)
	andi.	r23,r23,MSR_PR
	mfspr	r23,SPRG3		/* if from user, fix up tss.regs */
	beq	2f
	addi	r24,r1,STACK_FRAME_OVERHEAD
	stw	r24,PT_REGS(r23)
2:	addi	r2,r23,-TSS		/* set r2 to current */
	tovirt(r2,r2,r23)
	mflr	r23
	andi.	r24,r23,0x3f00		/* get vector offset */
	stw	r24,TRAP(r21)
	li	r22,0
	stw	r22,RESULT(r21)
	mtspr	SPRG2,r22		/* r1 is now kernel sp */
	addi	r24,r2,TASK_STRUCT_SIZE	/* check for kernel stack overflow */
	cmplw	0,r1,r2
	cmplw	1,r1,r24
	crand	1,1,4
	bgt	stack_ovf		/* if r2 < r1 < r2+TASK_STRUCT_SIZE */
	lwz	r24,0(r23)		/* virtual address of handler */
	lwz	r23,4(r23)		/* where to go when done */
	mtspr	SRR0,r24
	mtspr	SRR1,r20
	mtlr	r23
	SYNC
	rfi				/* jump to handler, enable MMU */

/*
 * On kernel stack overflow, load up an initial stack pointer
 * and call StackOverflow(regs), which should not return.
 */
stack_ovf:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lis	r1,init_task_union@ha
	addi	r1,r1,init_task_union@l
	addi	r1,r1,TASK_UNION_SIZE-STACK_FRAME_OVERHEAD
	lis	r24,StackOverflow@ha
	addi	r24,r24,StackOverflow@l
	li	r20,MSR_KERNEL
	mtspr	SRR0,r24
	mtspr	SRR1,r20
	SYNC
	rfi

/*
 * Continuation of the floating-point unavailable handler.
 */
load_up_fpu:

/*
 * Disable FP for the task which had the FPU previously,
 * and save its floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 */
#ifndef CONFIG_APUS
	lis	r6,-KERNELBASE@h
#else
	lis	r6,CYBERBASEp@h
	lwz	r6,0(r6)
#endif
	addis	r3,r6,last_task_used_math@ha
	lwz	r4,last_task_used_math@l(r3)
	mfmsr	r5
	ori	r5,r5,MSR_FP
	SYNC
	mtmsr	r5			/* enable use of fpu now */
	SYNC
	cmpi	0,r4,0
	beq	1f
	add	r4,r4,r6
	addi	r4,r4,TSS	        /* want TSS of last_task_used_math */
	SAVE_32FPRS(0, r4)
	mffs	fr0
	stfd	fr0,TSS_FPSCR-4(r4)
	lwz	r5,PT_REGS(r4)
	add	r5,r5,r6
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r20,MSR_FP
	andc	r4,r4,r20		/* disable FP for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)

1:	ori	r23,r23,MSR_FP		/* enable use of FP after return */
	mfspr	r5,SPRG3		/* current task's TSS (phys) */
	lfd	fr0,TSS_FPSCR-4(r5)
	mtfsf	0xff,fr0
	REST_32FPRS(0, r5)
	subi	r4,r5,TSS
	sub	r4,r4,r6
	stw	r4,last_task_used_math@l(r3)
	/* restore registers and return */
	lwz	r3,_CCR(r21)
	lwz	r4,_LINK(r21)
	mtcrf	0xff,r3
	mtlr	r4
	REST_GPR(1, r21)
	REST_4GPRS(3, r21)
	/* we haven't used ctr or xer */
	mtspr	SRR1,r23
	mtspr	SRR0,r22
	REST_GPR(20, r21)
	REST_2GPRS(22, r21)
	lwz	r21,GPR21(r21)
	SYNC
	rfi

/*
 * Load a PTE into the hash table, if possible.
 * The address is in r3, and r4 contains access flags:
 * _PAGE_USER (4) if a user-mode access, ored with
 * _PAGE_RW (2) if a write.  r20 contains DSISR or SRR1,
 * so bit 1 (0x40000000) is set if the exception was due
 * to no matching PTE being found in the hash table.
 * r5 contains the physical address of the current task's tss.
 *
 * Returns to the caller if the access is illegal or there is no
 * mapping for the address.  Otherwise it places an appropriate PTE
 * in the hash table and returns from the exception.
 * Uses r0, r2 - r6, ctr, lr.
 *
 * For speed, 4 of the instructions get patched once the size and
 * physical address of the hash table are known.  These definitions
 * of Hash_base and Hash_bits below are just an example.
 */
/*
 * Note that the 603s won't come here, since the 603
 * loads tlb directly into the tlb from the linux tables, while
 * others (601, 604, etc.) call hash_page() to load entries from
 * the linux tables into the hash table.  -- Cort
 */	
Hash_base = 0x180000
Hash_bits = 12				/* e.g. 256kB hash table */
Hash_msk = (((1 << Hash_bits) - 1) * 64)

	.globl	hash_page
hash_page:
#ifdef __SMP__
	lis	r6,hash_table_lock@h
	ori	r6,r6,hash_table_lock@l
	tophys(r6,r6,r2)
1011:	lwarx	r0,0,r6
	stwcx.	r6,0,r6
	bne-	1011b
	cmpi	0,r0,0
	bne	1011b
#endif /* __SMP__ */	
	/* Get PTE (linux-style) and check access */
	lwz	r5,PG_TABLES(r5)		
	tophys(r5,r5,r2)		/* convert to phys addr */
	rlwimi	r5,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r5,0(r5)		/* get pmd entry */
	rlwinm.	r5,r5,0,0,19		/* extract address of pte page */
	beq-	hash_page_out		/* return if no mapping */
	tophys(r2,r5,r2)
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r6,0(r2)		/* get linux-style pte */
	ori	r4,r4,1			/* set _PAGE_PRESENT bit in access */
	andc.	r0,r4,r6		/* check access & ~permission */
	bne-	hash_page_out		/* return if access not permitted */

	ori	r6,r6,0x100		/* set _PAGE_ACCESSED in pte */
	rlwinm	r5,r4,5,24,24		/* _PAGE_RW access -> _PAGE_DIRTY */
	rlwimi	r5,r4,7,22,22		/* _PAGE_RW -> _PAGE_HWWRITE */
	or	r6,r6,r5
	stw	r6,0(r2)		/* update PTE (accessed/dirty bits) */

	/* Convert linux-style PTE to low word of PPC-style PTE */
	rlwinm	r4,r6,32-9,31,31	/* _PAGE_HWWRITE -> PP lsb */
	rlwimi	r6,r6,32-1,31,31	/* _PAGE_USER -> PP (both bits now) */
	ori	r4,r4,0xe04		/* clear out reserved bits */
	andc	r6,r6,r4		/* PP=2 or 0, when _PAGE_HWWRITE */

	/* Construct the high word of the PPC-style PTE */
	mfsrin	r5,r3			/* get segment reg for segment */
	rlwinm	r5,r5,7,1,24		/* put VSID in 0x7fffff80 bits */
	oris	r5,r5,0x8000		/* set V (valid) bit */
	rlwimi	r5,r3,10,26,31		/* put in API (abbrev page index) */

	/* Get the address of the primary PTE group in the hash table */
	.globl	hash_page_patch_A
hash_page_patch_A:
	lis	r4,Hash_base@h		/* base address of hash table */
	rlwimi	r4,r5,32-1,26-Hash_bits,25	/* (VSID & hash_mask) << 6 */
	rlwinm	r0,r3,32-6,26-Hash_bits,25	/* (PI & hash_mask) << 6 */
	xor	r4,r4,r0		/* make primary hash */

	/* See whether it was a PTE not found exception or a
	   protection violation. */
	andis.	r0,r20,0x4000
	li	r2,8			/* PTEs/group */
	bne	10f			/* no PTE: go look for an empty slot */
	tlbie	r3			/* invalidate TLB entry */

	/* Search the primary PTEG for a PTE whose 1st word matches r5 */
	mtctr	r2
	addi	r3,r4,-8
1:	lwzu	r0,8(r3)		/* get next PTE */
	cmp	0,r0,r5
	bdnzf	2,1b			/* loop while ctr != 0 && !cr0.eq */
	beq+	found_slot

	/* Search the secondary PTEG for a matching PTE */
	ori	r5,r5,0x40		/* set H (secondary hash) bit */
	.globl	hash_page_patch_B
hash_page_patch_B:
	xoris	r3,r4,Hash_msk>>16	/* compute secondary hash */
	xori	r3,r3,0xffc0
	addi	r3,r3,-8
	mtctr	r2
2:	lwzu	r0,8(r3)
	cmp	0,r0,r5
	bdnzf	2,2b
	beq+	found_slot
	xori	r5,r5,0x40		/* clear H bit again */

	/* Search the primary PTEG for an empty slot */
10:	mtctr	r2
	addi	r3,r4,-8		/* search primary PTEG */
1:	lwzu	r0,8(r3)		/* get next PTE */
	srwi.	r0,r0,31		/* only want to check valid bit */
	bdnzf	2,1b			/* loop while ctr != 0 && !cr0.eq */
	beq+	found_empty

	/* Search the secondary PTEG for an empty slot */
	ori	r5,r5,0x40		/* set H (secondary hash) bit */
	.globl	hash_page_patch_C
hash_page_patch_C:
	xoris	r3,r4,Hash_msk>>16	/* compute secondary hash */
	xori	r3,r3,0xffc0
	addi	r3,r3,-8
	mtctr	r2
2:	lwzu	r0,8(r3)
	srwi.	r0,r0,31		/* only want to check valid bit */
	bdnzf	2,2b
	beq+	found_empty

	/* Choose an arbitrary slot in the primary PTEG to overwrite */
#if 0
	xori	r5,r5,0x40		/* clear H bit again */
	lwz	r2,next_slot@l(0)
	addi	r2,r2,8
	andi.	r2,r2,0x38
	stw	r2,next_slot@l(0)
	add	r3,r4,r2
#else
	/* now, allow 2nd hash as well as 1st */
	lwz	r2,next_slot@l(0)
	addi	r2,r2,8
	andi.	r2,r2,0x78
	stw	r2,next_slot@l(0)
	cmpi	0,0,r2,0x38             /* if it's the 2nd hash */
	bgt	second_evict
first_evict:
	xori	r5,r5,0x40		/* clear H bit again */
	add	r3,r4,r2
	b	11f
second_evict:
	.globl	hash_page_patch_D
hash_page_patch_D:	
	xoris	r3,r4,Hash_msk>>16	/* compute secondary hash */
	xori	r3,r3,0xffc0
	subi	r2,r2,0x40
	addi	r3,r3,r2
#endif
11:		
	/* update counter of evicted pages */
	lis	r2,htab_evicts@h
	ori	r2,r2,htab_evicts@l
	tophys(r2,r2,r4)
	lwz	r4,0(r2)
	addi	r4,r4,1
	stw	r4,0(r2)

	/* Store PTE in PTEG */
found_empty:
	stw	r5,0(r3)
found_slot:
	stw	r6,4(r3)

/*
 * Update the hash table miss count.  We only want misses here
 * that _are_ valid addresses and have a pte otherwise we don't
 * count it as a reload.  do_page_fault() takes care of bad addrs
 * and entries that need linux-style pte's created.
 *
 * safe to use r2 here since we're not using it as current yet 
 * update the htab misses count
 *   -- Cort
 */
	lis	r2,htab_reloads@h
	ori	r2,r2,htab_reloads@l
	tophys(r2,r2,r3)
	lwz	r3,0(r2)
	addi	r3,r3,1
	stw	r3,0(r2)
	SYNC

	/* Return from the exception */
	lwz	r3,_CCR(r21)
	lwz	r4,_LINK(r21)
	lwz	r5,_CTR(r21)
	mtcrf	0xff,r3
	mtlr	r4
	mtctr	r5
#ifdef __SMP__
	lis	r5,hash_table_lock@h
	ori	r5,r5,hash_table_lock@l
	tophys(r5,r5,r6)
	li	r6,0
	stw	r6,0(r5)
#endif /* __SMP__ */	
	REST_GPR(0, r21)
	REST_2GPRS(1, r21)
	REST_4GPRS(3, r21)
	/* we haven't used xer */
	mtspr	SRR1,r23
	mtspr	SRR0,r22
	REST_GPR(20, r21)
	REST_2GPRS(22, r21)
	lwz	r21,GPR21(r21)
	SYNC
	rfi
	
hash_page_out:
#ifdef __SMP__
	lis	r5,hash_table_lock@h
	ori	r5,r5,hash_table_lock@l
	tophys(r5,r5,r6)
	li	r6,0
	stw	r6,0(r5)
#endif /* __SMP__ */	
	blr
next_slot:
	.long	0

#ifdef CONFIG_APUS
	/* On APUS the first 0x4000 bytes of the kernel	will be mapped
	 * at a different physical address than the rest. For this
	 * reason, the exception code cannot use relative branches to
	 * access the code below.
	 */
	. = 0x4000
#endif

/*
 * This is where the main kernel code starts.
 */

start_here:
	/*
	 * Enable caches and 604-specific features if necessary.
	 */
	mfspr	r9,PVR
	rlwinm	r9,r9,16,16,31
	cmpi	0,r9,1
	beq	4f			/* not needed for 601 */
	mfspr	r11,HID0
	andi.	r0,r11,HID0_DCE
	ori	r11,r11,HID0_ICE|HID0_DCE
	ori	r8,r11,HID0_ICFI
	bne	3f			/* don't invalidate the D-cache */
	ori	r8,r8,HID0_DCI		/* unless it wasn't enabled */
3:
	/* turn on dpm for 603 */
	cmpi	0,r9,3
	bne	10f
	oris	r11,r11,HID0_DPM@h
10:
	sync
	mtspr	HID0,r8			/* enable and invalidate caches */
	sync
	mtspr	HID0,r11		/* enable caches */
	sync
	isync
	cmpi	0,r9,4			/* check for 604 */
	cmpi	1,r9,9			/* or 604e */
	cmpi	2,r9,10			/* or mach5 */
	cror	2,2,6
	cror	2,2,10
	bne	4f
	ori	r11,r11,HID0_SIED|HID0_BHTE /* for 604[e], enable */
	bne	2,5f
	ori	r11,r11,HID0_BTCD
5:	mtspr	HID0,r11		/* superscalar exec & br history tbl */
4:
	/* ptr to current */
	lis	r2,init_task_union@h
	ori	r2,r2,init_task_union@l
	/* stack */
	addi	r1,r2,TASK_UNION_SIZE
	li	r0,0
	stwu	r0,-STACK_FRAME_OVERHEAD(r1)

	/* Clear out the BSS */
	lis	r11,_end@ha
	addi	r11,r11,_end@l
	lis	r8,__bss_start@ha
	addi	r8,r8,__bss_start@l
	subf	r11,r8,r11
	addi	r11,r11,3
	rlwinm.	r11,r11,30,2,31
	beq	2f
	addi	r8,r8,-4
	mtctr	r11
	li	r0,0
3:	stwu	r0,4(r8)
	bdnz	3b
2:
/*
 * Decide what sort of machine this is and initialize the MMU.
 */
	mr	r3,r31
	mr	r4,r30
	mr	r5,r29
	mr	r6,r28
	mr	r7,r27
	bl	identify_machine
	bl	MMU_init

/*
 * Go back to running unmapped so we can load up new values
 * for SDR1 (hash table pointer) and the segment registers
 * and change to using our exception vectors.
 */
	lis	r6,_SDR1@ha
	lwz	r6,_SDR1@l(r6)
	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4,r3)
	li	r3,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	mtspr	SRR0,r4
	mtspr	SRR1,r3
	rfi
/* Load up the kernel context */
2:
	SYNC			/* Force all PTE updates to finish */
	tlbia			/* Clear all TLB entries */
	mtspr	SDR1,r6
	li	r0,16		/* load up segment register values */
	mtctr	r0		/* for context 0 */
	lis	r3,0x2000	/* Ku = 1, VSID = 0 */
	li	r4,0
3:	mtsrin	r3,r4
	addi	r3,r3,1		/* increment VSID */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b
	
/* Load the BAT registers with the values set up by MMU_init.
   MMU_init takes care of whether we're on a 601 or not. */
	lis	r3,BATS@ha
	addi	r3,r3,BATS@l
	tophys(r3,r3,r4)
	LOAD_BAT(0,0,r3,r4,r5)
	LOAD_BAT(1,16,r3,r4,r5)
	LOAD_BAT(2,32,r3,r4,r5)
	LOAD_BAT(3,48,r3,r4,r5)

/* Set up for using our exception vectors */
	/* ptr to phys current tss */
	tophys(r4,r2,r4)
	addi	r4,r4,TSS	/* init task's TSS */
	mtspr	SPRG3,r4
	li	r3,0
	mtspr	SPRG2,r3	/* 0 => r1 has kernel sp */

/* On CHRP copy exception vectors down to 0 */
	lis	r5,_stext@ha
	addi	r5,r5,_stext@l
	addis	r5,r5,-KERNELBASE@h
	cmpwi	0,r5,0
	beq	77f		/* vectors are already at 0 */
	li	r3,0x1000
	mtctr	r3
	li	r4,-4
	addi	r5,r5,-4
74:	lwzu	r0,4(r5)
	stwu	r0,4(r4)
	bdnz	74b
	/* need to flush/invalidate caches too */
	li	r3,0x4000/CACHE_LINE_SIZE
	li	r4,0
	mtctr	r3
73:	dcbst	0,r4
	addi	r4,r4,CACHE_LINE_SIZE
	bdnz	73b
	sync
	li	r4,0
	mtctr	r3
72:	icbi	0,r4
	addi	r4,r4,CACHE_LINE_SIZE
	bdnz	72b
	sync
	isync
77:

/* Now turn on the MMU for real! */
	li	r4,MSR_KERNEL
	lis	r3,start_kernel@h
	ori	r3,r3,start_kernel@l
	mtspr	SRR0,r3
	mtspr	SRR1,r4
	rfi			/* enable MMU and jump to start_kernel */


	.globl reset_SDR1
reset_SDR1:
	lis	r6,_SDR1@ha
	lwz	r6,_SDR1@l(r6)
	mfmsr	r5
	li	r4,0
	ori	r4,r4,MSR_EE|MSR_IR|MSR_DR
	andc	r3,r5,r4
	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4,r5)
	mtspr	SRR0,r4
	mtspr	SRR1,r3
	rfi
2:	/* load new SDR1 */
	tlbia
	mtspr	SDR1,r6
	/* turn the mmu back on */
	mflr	r3
	mtspr	SRR0,r3
	mtspr	SRR1,r5
	rfi

/*
 * FP unavailable trap from kernel - print a message, but let
 * the task use FP in the kernel until it returns to user mode.
 */
KernelFP:
	lwz	r3,_MSR(r1)
	ori	r3,r3,MSR_FP
	stw	r3,_MSR(r1)		/* enable use of FP after return */
	lis	r3,86f@h
	ori	r3,r3,86f@l
	mr	r4,r2			/* current */
	lwz	r5,_NIP(r1)
	bl	printk
	b	int_return
86:	.string	"floating point used in kernel (task=%p, pc=%x)\n"
	.align	4

/*
 * Disable FP for the task which had the FPU previously,
 * and save its floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 */
	.globl	giveup_fpu
giveup_fpu:
	lis	r3,last_task_used_math@ha
	lwz	r4,last_task_used_math@l(r3)
	mfmsr	r5
	ori	r5,r5,MSR_FP
	SYNC
	mtmsr	r5			/* enable use of fpu now */
	SYNC
	cmpi	0,r4,0
	beqlr-				/* if no previous owner, done */
	addi	r4,r4,TSS	        /* want TSS of last_task_used_math */
	li	r5,0
	stw	r5,last_task_used_math@l(r3)
	SAVE_32FPRS(0, r4)
	mffs	fr0
	stfd	fr0,TSS_FPSCR-4(r4)
	lwz	r5,PT_REGS(r4)
	lwz	r3,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r4,MSR_FP
	andc	r3,r3,r4		/* disable FP for previous task */
	stw	r3,_MSR-STACK_FRAME_OVERHEAD(r5)
	blr

/*
 * Handle a system call.
 */
DoSyscall:
	stw	r0,TSS+LAST_SYSCALL(r2)
	lwz	r11,_CCR(r1)	/* Clear SO bit in CR */
	lis	r10,0x1000
	andc	r11,r11,r10
	stw	r11,_CCR(r1)
#ifdef SHOW_SYSCALLS
#ifdef SHOW_SYSCALLS_TASK
	lis	r31,show_syscalls_task@ha
	lwz	r31,show_syscalls_task@l(r31)
	cmp	0,r2,r31
	bne	1f
#endif
	lis	r3,7f@ha
	addi	r3,r3,7f@l
	lwz	r4,GPR0(r1)
	lwz	r5,GPR3(r1)
	lwz	r6,GPR4(r1)
	lwz	r7,GPR5(r1)
	lwz	r8,GPR6(r1)
	mr	r9,r2
	bl	printk
	lwz	r0,GPR0(r1)
	lwz	r3,GPR3(r1)
	lwz	r4,GPR4(r1)
	lwz	r5,GPR5(r1)
	lwz	r6,GPR6(r1)
	lwz	r7,GPR7(r1)
	lwz	r8,GPR8(r1)
1:
#endif /* SHOW_SYSCALLS */
	cmpi	0,r0,0x7777	/* Special case for 'sys_sigreturn' */
	beq-	10f
	lwz	r10,TASK_FLAGS(r2)
	andi.	r10,r10,PF_TRACESYS
	bne-	50f
	cmpli	0,r0,NR_syscalls
	bge-	66f
	lis	r10,sys_call_table@h
	ori	r10,r10,sys_call_table@l
	slwi	r0,r0,2
	lwzx	r10,r10,r0	/* Fetch system call handler [ptr] */
	cmpi	0,r10,0
	beq-	66f
	mtlr	r10
	addi	r9,r1,STACK_FRAME_OVERHEAD
	blrl			/* Call handler */
	.globl	syscall_ret_1
syscall_ret_1:
20:	stw	r3,RESULT(r1)	/* Save result */
#ifdef SHOW_SYSCALLS
#ifdef SHOW_SYSCALLS_TASK
	cmp	0,r2,r31
	bne	91f
#endif
	mr	r4,r3
	lis	r3,79f@ha
	addi	r3,r3,79f@l
	bl	printk
	lwz	r3,RESULT(r1)
91:
#endif
	li	r10,-_LAST_ERRNO
	cmpl	0,r3,r10
	blt	30f
	neg	r3,r3
	cmpi	0,r3,ERESTARTNOHAND
	bne	22f
	li	r3,EINTR
22:	lwz	r10,_CCR(r1)	/* Set SO bit in CR */
	oris	r10,r10,0x1000
	stw	r10,_CCR(r1)
30:	stw	r3,GPR3(r1)	/* Update return value */
	b	int_return
66:	li	r3,ENOSYS
	b	22b
/* sys_sigreturn */
10:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	sys_sigreturn
	cmpi	0,r3,0		/* Check for restarted system call */
	bge	int_return
	b	20b
/* Traced system call support */
50:	bl	syscall_trace
	lwz	r0,GPR0(r1)	/* Restore original registers */
	lwz	r3,GPR3(r1)
	lwz	r4,GPR4(r1)
	lwz	r5,GPR5(r1)
	lwz	r6,GPR6(r1)
	lwz	r7,GPR7(r1)
	lwz	r8,GPR8(r1)
	lwz	r9,GPR9(r1)
	cmpli	0,r0,NR_syscalls
	bge-	66f
	lis	r10,sys_call_table@h
	ori	r10,r10,sys_call_table@l
	slwi	r0,r0,2
	lwzx	r10,r10,r0	/* Fetch system call handler [ptr] */
	cmpi	0,r10,0
	beq-	66f
	mtlr	r10
	addi	r9,r1,STACK_FRAME_OVERHEAD
	blrl			/* Call handler */
	.globl	syscall_ret_2
syscall_ret_2:
	stw	r3,RESULT(r1)	/* Save result */	
	stw	r3,GPR0(r1)	/* temporary gross hack to make strace work */
	li	r10,-_LAST_ERRNO
	cmpl	0,r3,r10
	blt	60f
	neg	r3,r3
	cmpi	0,r3,ERESTARTNOHAND
	bne	52f
	li	r3,EINTR
52:	lwz	r10,_CCR(r1)	/* Set SO bit in CR */
	oris	r10,r10,0x1000
	stw	r10,_CCR(r1)
60:	stw	r3,GPR3(r1)	/* Update return value */
	bl	syscall_trace
	b	int_return
66:	li	r3,ENOSYS
	b	52b
#ifdef SHOW_SYSCALLS
7:	.string	"syscall %d(%x, %x, %x, %x), current=%p\n"
79:	.string	" -> %x\n"
	.align	2
#endif

/*
 * This routine switches between two different tasks.  The process
 * state of one is saved on its kernel stack.  Then the state
 * of the other is restored from its kernel stack.  The memory
 * management hardware is updated to the second process's state.
 * Finally, we can return to the second process, via int_return.
 * On entry, r3 points to the TSS for the current task, r4
 * points to the TSS for the new task, and r5 contains the
 * MMU context number for the new task.
 *
 * Note: there are two ways to get to the "going out" portion
 * of this code; either by coming in via the entry (_switch)
 * or via "fork" which must set up an environment equivalent
 * to the "_switch" path.  If you change this (or in particular, the
 * SAVE_REGS macro), you'll have to change the fork code also.
 *
 * The code which creates the new task context is in 'copy_thread'
 * in arch/ppc/kernel/process.c
 */	
_GLOBAL(_switch)
	stwu	r1,-INT_FRAME_SIZE-STACK_UNDERHEAD(r1)
	stw	r0,GPR0(r1)
	lwz	r0,0(r1)
	stw	r0,GPR1(r1)
	SAVE_10GPRS(2, r1)
	SAVE_10GPRS(12, r1)
	SAVE_10GPRS(22, r1)
	mflr	r20		/* Return to switch caller */
	mfmsr	r22
	li	r0,MSR_FP	/* Disable floating-point */
	andc	r22,r22,r0
	stw	r20,_NIP(r1)
	stw	r22,_MSR(r1)
	stw	r20,_LINK(r1)
	mfcr	r20
	mfctr	r22
	mfspr	r23,XER
	stw	r20,_CCR(r1)
	stw	r22,_CTR(r1)
	stw	r23,_XER(r1)
	li	r0,0x0ff0
	stw	r0,TRAP(r1)
	stw	r1,KSP(r3)	/* Set old stack pointer */
	sync
	tophys(r0,r4,r3)
	mtspr	SPRG3,r0	/* Update current TSS phys addr */
	SYNC
	lwz	r1,KSP(r4)	/* Load new stack pointer */
	addi	r2,r4,-TSS	/* Update current */
	/* Set up segment registers for new task */
	rlwinm	r5,r5,4,8,27	/* VSID = context << 4 */
	addis	r5,r5,0x6000	/* Set Ks, Ku bits */
	li	r0,8		/* TASK_SIZE / SEGMENT_SIZE */
	mtctr	r0
	li	r3,0
3:	mtsrin	r5,r3
	addi	r5,r5,1		/* next VSID */
	addis	r3,r3,0x1000	/* address of next segment */
	bdnz	3b
	SYNC

/* FALL THROUGH into int_return */

/*
 * Trap exit.
 */
	.globl	ret_from_syscall
ret_from_syscall:	
	.globl	int_return
int_return:
0:	mfmsr	r30		/* Disable interrupts */
	li	r4,0
	ori	r4,r4,MSR_EE
	andc	r30,r30,r4
	SYNC			/* Some chip revs need this... */
	mtmsr	r30
	SYNC
	lwz	r5,_MSR(r1)
	and.	r5,r5,r4
	beq	2f
3:	lis	r4,lost_interrupts@ha
	lwz	r4,lost_interrupts@l(r4)
	cmpi	0,r4,0
	beq+	1f
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_IRQ
	b	3b
1:	lis	r4,bh_mask@ha
	lwz	r4,bh_mask@l(r4)
	lis	r5,bh_active@ha
	lwz	r5,bh_active@l(r5)
	and.	r4,r4,r5
	beq+	2f
	bl	do_bottom_half
	SYNC
	mtmsr	r30		/* disable interrupts again */
	SYNC
2:	lwz	r3,_MSR(r1)	/* Returning to user mode? */
	andi.	r3,r3,MSR_PR
	beq+	10f		/* if so, check need_resched and signals */
	lis	r3,need_resched@ha
	lwz	r3,need_resched@l(r3)
	cmpi	0,r3,0		/* check need_resched flag */
	beq+	7f
	bl	schedule
	b	0b
7:	lwz	r5,SIGPENDING(r2) /* Check for pending unblocked signals */
	cmpwi	0,r5,0
	beq+	8f
	li	r3,0
	addi	r4,r1,STACK_FRAME_OVERHEAD
	bl	do_signal
	b	0b
8:	addi	r4,r1,INT_FRAME_SIZE+STACK_UNDERHEAD	/* size of frame */
	stw	r4,TSS+KSP(r2)	/* save kernel stack pointer */
	tophys(r3,r1,r3)
	mtspr	SPRG2,r3	/* phys exception stack pointer */
10:	lwz	r2,_CTR(r1)
	lwz	r0,_LINK(r1)
	mtctr	r2
	mtlr	r0
	lwz	r2,_XER(r1)
	lwz	r0,_CCR(r1)
	mtspr	XER,r2
	mtcrf	0xFF,r0
	REST_10GPRS(3, r1)
	REST_10GPRS(13, r1)
	REST_8GPRS(23, r1)
	REST_GPR(31, r1)
	lwz	r2,_NIP(r1)	/* Restore environment */
	lwz	r0,_MSR(r1)
	mtspr	SRR0,r2
	mtspr	SRR1,r0
	lwz	r0,GPR0(r1)
	lwz	r2,GPR2(r1)
	lwz	r1,GPR1(r1)
	SYNC
	rfi

/*
 * Fake an interrupt from kernel mode.
 * This is used when enable_irq loses an interrupt.
 * We only fill in the stack frame minimally.
 */
_GLOBAL(fake_interrupt)
	mflr	r0
	stw	r0,4(r1)
	stwu	r1,-INT_FRAME_SIZE-STACK_UNDERHEAD(r1)
	stw	r0,_NIP(r1)
	stw	r0,_LINK(r1)
	mfmsr	r3
	stw	r3,_MSR(r1)
	li	r0,0x0fac
	stw	r0,TRAP(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_IRQ
	addi	r1,r1,INT_FRAME_SIZE+STACK_UNDERHEAD
	lwz	r0,4(r1)
	mtlr	r0
	blr

/*
 * Set up the segment registers for a new context.
 */
_GLOBAL(set_context)
	rlwinm	r3,r3,4,8,27	/* VSID = context << 4 */
	addis	r3,r3,0x6000	/* Set Ks, Ku bits */
	li	r0,8		/* TASK_SIZE / SEGMENT_SIZE */
	mtctr	r0
	li	r4,0
3:	mtsrin	r3,r4
	addi	r3,r3,1		/* next VSID */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b
	SYNC
	blr

/*
 * Flush instruction cache.
 * This is a no-op on the 601.
 */
_GLOBAL(flush_instruction_cache)
	mfspr	r3,PVR
	rlwinm	r3,r3,16,16,31
	cmpi	0,r3,1
	beqlr			/* for 601, do nothing */
	/* 603/604 processor - use invalidate-all bit in HID0 */
	mfspr	r3,HID0
	ori	r3,r3,HID0_ICFI
	mtspr	HID0,r3
	SYNC
	blr

/*
 * Write any modified data cache blocks out to memory
 * and invalidate the corresponding instruction cache blocks.
 * This is a no-op on the 601.
 *
 * flush_icache_range(unsigned long start, unsigned long stop)
 */
_GLOBAL(flush_icache_range)
	mfspr	r5,PVR
	rlwinm	r5,r5,16,16,31
	cmpi	0,r5,1
	beqlr				/* for 601, do nothing */
	li	r5,CACHE_LINE_SIZE-1
	andc	r3,r3,r5
	subf	r4,r3,r4
	add	r4,r4,r5
	srwi.	r4,r4,LG_CACHE_LINE_SIZE
	beqlr
	mtctr	r4
	mr	r6,r3
1:	dcbst	0,r3
	addi	r3,r3,CACHE_LINE_SIZE
	bdnz	1b
	sync				/* wait for dcbst's to get to ram */
	mtctr	r4
2:	icbi	0,r6
	addi	r6,r6,CACHE_LINE_SIZE
	bdnz	2b
	sync
	isync
	blr

/*
 * Flush a particular page from the DATA cache
 * Note: this is necessary because the instruction cache does *not*
 * snoop from the data cache.
 * This is a no-op on the 601 which has a unified cache.
 *
 *	void flush_page_to_ram(void *page)
 */
_GLOBAL(flush_page_to_ram)
	mfspr	r5,PVR
	rlwinm	r5,r5,16,16,31
	cmpi	0,r5,1
	beqlr				/* for 601, do nothing */
	li	r4,0x0FFF
	andc	r3,r3,r4		/* Get page base address */
	li	r4,4096/CACHE_LINE_SIZE	/* Number of lines in a page */
	mtctr	r4
	mr	r6,r3
0:	dcbst	0,r3			/* Write line to ram */
	addi	r3,r3,CACHE_LINE_SIZE
	bdnz	0b
	sync
	mtctr	r4
1:	icbi	0,r6
	addi	r6,r6,CACHE_LINE_SIZE
	bdnz	1b
	sync
	isync
	blr

/*
 * Flush entries from the hash table with VSIDs in the range
 * given.
 */
_GLOBAL(flush_hash_segments)
#ifdef NO_RELOAD_HTAB
/*
 * Bitmask of PVR numbers of 603-like chips,
 * for which we don't use the hash table at all.
 */
#define PVR_603_LIKE	0x13000000	/* bits 3, 6, 7 set */

	mfspr	r0,PVR
	rlwinm	r0,r0,16,27,31
	lis	r9,PVR_603_LIKE@h
	rlwnm.	r0,r9,r0,0,0
	bne	99f
#endif /* NO_RELOAD_HTAB */
#ifdef __SMP__
	lis	r6,hash_table_lock@h
	ori	r6,r6,hash_table_lock@l
1011:	lwarx	r0,0,r6
	stwcx.	r6,0,r6
	bne-	1011b
	cmpi	0,r0,0
	bne	1011b
#endif /* __SMP__ */	
	rlwinm	r3,r3,7,1,24		/* put VSID lower limit in position */
	oris	r3,r3,0x8000		/* set V bit */
	rlwinm	r4,r4,7,1,24		/* put VSID upper limit in position */
	oris	r4,r4,0x8000
	ori	r4,r4,0x7f
	lis	r5,Hash@ha
	lwz	r5,Hash@l(r5)		/* base of hash table */
	lis	r6,Hash_size@ha
	lwz	r6,Hash_size@l(r6)	/* size in bytes */
	srwi	r6,r6,3			/* # PTEs */
	mtctr	r6
	addi	r5,r5,-8
	li	r0,0
1:	lwzu	r6,8(r5)		/* get next tag word */
	cmplw	0,r6,r3
	cmplw	1,r6,r4
	cror	0,0,5			/* set cr0.lt if out of range */
	blt	2f			/* branch if out of range */
	stw	r0,0(r5)		/* invalidate entry */
2:	bdnz	1b			/* continue with loop */
	sync
#ifdef __SMP__
	lis	r5,hash_table_lock@h
	ori	r5,r5,hash_table_lock@l
	li	r6,0
	stw	r6,0(r5)
#endif /* __SMP__ */	
99:	tlbia
	isync
	blr

/*
 * Flush the entry for a particular page from the hash table.
 *
 * flush_hash_page(unsigned context, unsigned long va)
 */
_GLOBAL(flush_hash_page)
#ifdef NO_RELOAD_HTAB
	mfspr	r0,PVR
	rlwinm	r0,r0,16,27,31
	lis	r9,PVR_603_LIKE@h
	rlwnm.	r0,r9,r0,0,0
	bne	99f
#endif /* NO_RELOAD_HTAB */		
#ifdef __SMP__
	lis	r6,hash_table_lock@h
	ori	r6,r6,hash_table_lock@l
1011:	lwarx	r0,0,r6
	stwcx.	r6,0,r6
	bne-	1011b
	cmpi	0,r0,0
	bne	1011b
#endif /* __SMP__ */	
	rlwinm	r3,r3,11,1,20		/* put context into vsid */
	rlwimi	r3,r4,11,21,24		/* put top 4 bits of va into vsid */
	oris	r3,r3,0x8000		/* set V (valid) bit */
	rlwimi	r3,r4,10,26,31		/* put in API (abbrev page index) */
	rlwinm	r7,r4,32-6,10,25	/* get page index << 6 */
	rlwinm	r5,r3,32-1,7,25		/* vsid << 6 */
	xor	r7,r7,r5		/* primary hash << 6 */
	lis	r5,Hash_mask@ha
	lwz	r5,Hash_mask@l(r5)	/* hash mask */
	slwi	r5,r5,6			/*  << 6 */
	and	r7,r7,r5
	lis	r6,Hash@ha
	lwz	r6,Hash@l(r6)		/* hash table base */
	add	r6,r6,r7		/* address of primary PTEG */
	li	r8,8
	mtctr	r8
	addi	r7,r6,-8
1:	lwzu	r0,8(r7)		/* get next PTE */
	cmpw	0,r0,r3			/* see if tag matches */
	bdnzf	2,1b			/* while --ctr != 0 && !cr0.eq */
	beq	3f			/* if we found it */
	ori	r3,r3,0x40		/* set H (alt. hash) bit */
	xor	r6,r6,r5		/* address of secondary PTEG */
	mtctr	r8
	addi	r7,r6,-8
2:	lwzu	r0,8(r7)		/* get next PTE */
	cmpw	0,r0,r3			/* see if tag matches */
	bdnzf	2,2b			/* while --ctr != 0 && !cr0.eq */
	bne	4f			/* if we didn't find it */
3:	li	r0,0
	stw	r0,0(r7)		/* invalidate entry */
4:	sync
#ifdef __SMP__
	lis	r5,hash_table_lock@h
	ori	r5,r5,hash_table_lock@l
	li	r6,0
	stw	r6,0(r5)
#endif /* __SMP__ */	
99:	tlbie	r4			/* in hw tlb too */
	isync
	blr

/*
 * This routine is just here to keep GCC happy - sigh...
 */	
_GLOBAL(__main)
	blr

/*
 * On CHRP, the Run-Time Abstraction Services (RTAS) have to be
 * called with the MMU off.
 */
	.globl	enter_rtas
enter_rtas:
	stwu	r1,-16(r1)
	mflr	r0
	stw	r0,20(r1)
	addis	r3,r3,-KERNELBASE@h
	lis	r4,rtas_data@ha
	lwz	r4,rtas_data@l(r4)
	lis	r6,1f@ha	/* physical return address for rtas */
	addi	r6,r6,1f@l
	addis	r6,r6,-KERNELBASE@h
	subi	r7,r1,INT_FRAME_SIZE+STACK_UNDERHEAD
	addis	r7,r7,-KERNELBASE@h
	lis	r8,rtas_entry@ha
	lwz	r8,rtas_entry@l(r8)
	mfmsr	r9
	stw	r9,8(r1)
	li	r0,0
	ori	r0,r0,MSR_EE|MSR_SE|MSR_BE
	andc	r0,r9,r0
	andi.	r9,r9,MSR_ME|MSR_RI
	sync			/* disable interrupts so SRR0/1 */
	mtmsr	r0		/* don't get trashed */
	mtlr	r6
	mtspr	SPRG2,r7
	mtspr	SRR0,r8
	mtspr	SRR1,r9
	rfi
1:	addis	r9,r1,-KERNELBASE@h
	lwz	r8,20(r9)	/* get return address */
	lwz	r9,8(r9)	/* original msr value */
	li	r0,0
	mtspr	SPRG2,r0
	mtspr	SRR0,r8
	mtspr	SRR1,r9
	rfi			/* return to caller */

	
	.globl amhere
amhere:	.long 0
	
#ifdef __SMP__
/*
 * Secondary processor begins executing here.
 */
	.globl	secondary_entry
secondary_entry:
	lis	r0,amhere@h
	ori	r0,r0,amhere@l
	addis	r0,r0,-KERNELBASE@h
	stw	r0,0(r0)
	sync
	isync	
	/* just like __start() with a few changes -- Cort */
	mfspr	r9,PVR
	rlwinm	r9,r9,16,16,31		/* r9 = 1 for 601, 4 for 604 */
	cmpi	0,r9,1
	lis	r11,KERNELBASE@h
	bne	4f
	ori	r11,r11,4		/* set up BAT registers for 601 */
	li	r8,0x7f
	oris	r9,r11,0x800000@h	/* set up BAT reg for 2nd 8M */
	oris	r10,r8,0x800000@h	/* set up BAT reg for 2nd 8M */
	mtspr	IBAT1U,r9
	mtspr	IBAT1L,r10
	b	5f
4:	ori	r11,r11,0x1ff		/* set up BAT registers for 604 */
	li	r8,2
	mtspr	DBAT0U,r11
	mtspr	DBAT0L,r8
5:	mtspr	IBAT0U,r11
	mtspr	IBAT0L,r8
	isync
/*
 * we now have the 1st 16M of ram mapped with the bats.
 * prep needs the mmu to be turned on here, but pmac already has it on.
 * this shouldn't bother the pmac since it just gets turned on again
 * as we jump to our code at KERNELBASE. -- Cort
 */
	mfmsr	r0
	ori	r0,r0,MSR_DR|MSR_IR
	mtspr	SRR1,r0
	lis	r0,100f@h
	ori	r0,r0,100f@l
	mtspr	SRR0,r0
	SYNC
	rfi				/* enables MMU */
100:
	/*
	 * Enable caches and 604-specific features if necessary.
	 */
	mfspr	r9,PVR
	rlwinm	r9,r9,16,16,31
	cmpi	0,r9,1
	beq	4f			/* not needed for 601 */
	mfspr	r11,HID0
	andi.	r0,r11,HID0_DCE
	ori	r11,r11,HID0_ICE|HID0_DCE
	ori	r8,r11,HID0_ICFI
	bne	3f			/* don't invalidate the D-cache */
	ori	r8,r8,HID0_DCI		/* unless it wasn't enabled */
3:
	/* turn on dpm for 603 */
	cmpi	0,r9,3
	bne	10f
	oris	r11,r11,HID0_DPM@h
10:
	sync
	mtspr	HID0,r8			/* enable and invalidate caches */
	sync
	mtspr	HID0,r11		/* enable caches */
	sync
	isync
	cmpi	0,r9,4			/* check for 604 */
	cmpi	1,r9,9			/* or 604e */
	cmpi	2,r9,10			/* or mach5 */
	cror	2,2,6
	cror	2,2,10
	bne	4f
	ori	r11,r11,HID0_SIED|HID0_BHTE /* for 604[e], enable */
	bne	2,5f
	ori	r11,r11,HID0_BTCD
5:	mtspr	HID0,r11		/* superscalar exec & br history tbl */
4:
	/* get ptr to current */
	lis	r2,current_set@h
	ori	r2,r2,current_set@l
	/* assume we're second processor for now */
	lwz	r2,4(r2)
	/* stack */
	addi	r1,r2,TASK_UNION_SIZE
	li	r0,0
	stwu	r0,-STACK_FRAME_OVERHEAD(r1)
		
/*
 * init_MMU on the first processor has setup the variables
 * for us - all we need to do is load them -- Cort 
 */
	
/*
 * Go back to running unmapped so we can load up new values
 * for SDR1 (hash table pointer) and the segment registers
 * and change to using our exception vectors.
 */
	lis	r6,_SDR1@ha
	lwz	r6,_SDR1@l(r6)
	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4,r3)
	li	r3,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	mtspr	SRR0,r4
	mtspr	SRR1,r3
	rfi
/* Load up the kernel context */
2:
	SYNC			/* Force all PTE updates to finish */
	tlbia			/* Clear all TLB entries */
	mtspr	SDR1,r6
	li	r0,16		/* load up segment register values */
	mtctr	r0		/* for context 0 */
	lis	r3,0x2000	/* Ku = 1, VSID = 0 */
	li	r4,0
3:	mtsrin	r3,r4
	addi	r3,r3,1		/* increment VSID */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b
	
/* Load the BAT registers with the values set up by MMU_init.
   MMU_init takes care of whether we're on a 601 or not. */
	lis	r3,BATS@ha
	addi	r3,r3,BATS@l
	tophys(r3,r3,r4)
	LOAD_BAT(0,0,r3,r4,r5)
	LOAD_BAT(1,16,r3,r4,r5)
	LOAD_BAT(2,32,r3,r4,r5)
	LOAD_BAT(3,48,r3,r4,r5)

/* Set up for using our exception vectors */
	/* ptr to phys current tss */
	tophys(r4,r2,r4)
	addi	r4,r4,TSS	/* init task's TSS */
	mtspr	SPRG3,r4
	li	r3,0
	mtspr	SPRG2,r3	/* 0 => r1 has kernel sp */

	/* need to flush/invalidate caches too */
	li	r3,0x4000/CACHE_LINE_SIZE
	li	r4,0
	mtctr	r3
73:	dcbst	0,r4
	addi	r4,r4,CACHE_LINE_SIZE
	bdnz	73b
	sync
	li	r4,0
	mtctr	r3
72:	icbi	0,r4
	addi	r4,r4,CACHE_LINE_SIZE
	bdnz	72b
	sync
	isync
77:
/* Now turn on the MMU for real! */
	li	r4,MSR_KERNEL
	lis	r3,start_secondary@h
	ori	r3,r3,start_secondary@l
	mtspr	SRR0,r3
	mtspr	SRR1,r4
	rfi			/* enable MMU and jump to start_kernel */
/* should never return */
	.long 0
#endif /* __SMP__ */
	
/*
 * We put a few things here that have to be page-aligned.
 * This stuff goes at the beginning of the data segment,
 * which is page-aligned.
 */
	.data
	.globl	sdata
sdata:
	.globl	empty_zero_page
empty_zero_page:
	.space	4096

	.globl	swapper_pg_dir
swapper_pg_dir:
	.space	4096	

/*
 * This space gets a copy of optional info passed to us by the bootstrap
 * Used to pass parameters into the kernel like root=/dev/sda1, etc.
 */	
	.globl	cmd_line
cmd_line:
	.space	512
