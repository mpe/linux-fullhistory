/*
 *  arch/ppc/kernel/head.S
 *
 *  $Id: head.S,v 1.114 1998/12/28 10:28:45 paulus Exp $
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *  MPC8xx modifications Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
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
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cache.h>

#ifdef CONFIG_APUS
#include <asm/amigappc.h>
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
#ifndef CONFIG_8xx
/* This instruction is not implemented on the PPC 603 or 601 */
#define tlbia \
	li	r4,128; \
	mtctr	r4; \
	lis	r4,0xC000; \
0:	tlbie	r4; \
	addi	r4,r4,0x1000; \
	bdnz	0b
#endif

/* 601 only have IBAT cr0.eq is set on 601 when using this macro */ 
#define LOAD_BAT(n, offset, reg, RA, RB) \
	lwz	RA,offset+0(reg); \
	lwz	RB,offset+4(reg);	\
	mtspr	IBAT##n##U,RA;	\
	mtspr	IBAT##n##L,RB;	\
	beq	1f;		\
	lwz	RA,offset+8(reg);	\
	lwz	RB,offset+12(reg);	\
	mtspr	DBAT##n##U,RA;	\
	mtspr	DBAT##n##L,RB;	\
1:	 

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
 * If we are booted from MacOS via BootX, we enter with the kernel
 * image loaded somewhere, and the following values in registers:
 *  r3: 'BooX' (0x426f6f58)
 *  r4: virtual address of boot_infos_t
 *  r5: 0
 *
 * APUS
 *   r3: 'APUS'
 *   Linux/m68k style BootInfo structure at &_end.
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
 *
 * MPC8xx
 * This port was done on an MBX board with an 860.  Right now I only
 * support an ELF compressed (zImage) boot from EPPC-Bug because the
 * code there loads up some registers before calling us:
 *   r3: ptr to board info data
 *   r4: initrd_start or if no initrd then 0
 *   r5: initrd_end - unused if r4 is 0
 *   r6: Start of command line string
 *   r7: End of command line string
 *
 * I decided to use conditional compilation instead of checking PVR and
 * adding more processor specific branches around code I don't need.
 * Since this is an embedded processor, I also appreciate any memory
 * savings I can get.
 *
 * The MPC8xx does not have any BATs, but it supports large page sizes.
 * We first initialize the MMU to support 8M byte pages, then load one
 * entry into each of the instruction and data TLBs to map the first
 * 8M 1:1.  I also mapped an additional I/O space 1:1 so we can get to
 * the "internal" processor registers before MMU_init is called.
 *
 * The TLB code currently contains a major hack.  Since I use the condition
 * code register, I have to save and restore it.  I am out of registers, so
 * I just store it in memory location 0 (the TLB handlers are not reentrant).
 * To avoid making any decisions, I need to use the "segment" valid bit
 * in the first level table, but that would require many changes to the
 * Linux page directory/table functions that I don't want to do right now.
 *
 * I used to use SPRG2 for a temporary register in the TLB handler, but it
 * has since been put to other uses.  I now use a hack to save a register
 * and the CCR at memory location 0.....Someday I'll fix this.....
 *	-- Dan
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
	mr	r27,r7
#ifndef CONFIG_8xx
	bl	prom_init
	.globl	__secondary_start
__secondary_start:
/*
 * Use the first pair of BAT registers to map the 1st 16MB
 * of RAM to KERNELBASE.  From this point on we can't safely
 * call OF any more.
 */
	mfspr	r9,PVR
	rlwinm	r9,r9,16,16,31		/* r9 = 1 for 601, 4 for 604 */
	cmpi	0,r9,1
	lis	r11,KERNELBASE@h
	bne	4f
	ori	r11,r11,4		/* set up BAT registers for 601 */
	li	r8,0x7f			/* valid, block length = 8MB */
	oris	r9,r11,0x800000@h	/* set up BAT reg for 2nd 8M */
	oris	r10,r8,0x800000@h	/* set up BAT reg for 2nd 8M */
	mtspr	IBAT0U,r11		/* N.B. 601 has valid bit in */
	mtspr	IBAT0L,r8		/* lower BAT register */
	mtspr	IBAT1U,r9
	mtspr	IBAT1L,r10
	b	5f
4:
#ifndef CONFIG_APUS
	ori	r11,r11,0x1fe		/* set up BAT registers for 604 */
	li	r8,2			/* R/W access */
#else
	ori	r11,r11,0xfe		/* set up an 8MB mapping */
	lis	r8,CYBERBASEp@h
	lwz	r8,0(r8)
	addis	r8,r8,KERNELBASE@h
	addi	r8,r8,2
#endif
	mtspr	DBAT0L,r8		/* N.B. 6xx (not 601) have valid */
	mtspr	DBAT0U,r11		/* bit in upper BAT register */
	mtspr	IBAT0L,r8
	mtspr	IBAT0U,r11
5:	isync

#ifdef CONFIG_APUS
	/* Unfortunately the APUS specific instructions bloat the
	 * code so it cannot fit in the 0x100 bytes available. We have
	 * to do it the crude way. */

	/* Map 0xfff00000 so we can access VTOP/PTOV constant when
	   MMU is enabled. */
	lis	r8,0xfff0
	ori	r11,r8,0x2		/* r/w */
	ori	r8,r8,0x2		/* 128KB, supervisor */
	mtspr	DBAT3U,r8
	mtspr	DBAT3L,r11
	
	/* Copy exception code to exception vector base. */
	lis	r3,KERNELBASE@h
	tophys(r4,r3,r5)
	lis	r3,0xfff0		/* Copy to 0xfff00000 on APUS */
	li	r5,0x4000		/* # bytes of memory to copy */
	li	r6,0
	bl	copy_and_flush		/* copy the first 0x4000 bytes */
#else /* CONFIG_APUS */
/*
 * We need to run with _start at physical address 0.
 * On CHRP, we are loaded at 0x10000 since OF on CHRP uses
 * the exception vectors at 0 (and therefore this copy
 * overwrites OF's exception vectors with our own).
 * If the MMU is already turned on, we copy stuff to KERNELBASE,
 * otherwise we copy it to 0.
 */
	bl	reloc_offset
	mr	r26,r3
	addis	r4,r3,KERNELBASE@h	/* current address of _start */
	cmpwi	0,r4,0			/* are we already running at 0? */
	beq	2f			/* assume it's OK if so */
	li	r3,0
	mfmsr	r0
	andi.	r0,r0,MSR_DR		/* MMU enabled? */
	beq	relocate_kernel
	lis	r3,KERNELBASE@h		/* if so, are we */
	cmpw	0,r4,r3			/* already running at KERNELBASE? */
	bne	relocate_kernel
2:
#endif /* CONFIG_APUS */
/*
 * we now have the 1st 16M of ram mapped with the bats.
 * prep needs the mmu to be turned on here, but pmac already has it on.
 * this shouldn't bother the pmac since it just gets turned on again
 * as we jump to our code at KERNELBASE. -- Cort
 */
	
#else /* CONFIG_8xx */
	tlbia			/* Invalidate all TLB entries */
	li	r8, 0
	mtspr	MI_CTR, r8	/* Set instruction control to zero */
	lis	r8, MD_RESETVAL@h
	mtspr	MD_CTR, r8	/* Set data TLB control */

	/* Now map the lower 8 Meg into the TLBs.  For this quick hack,
	 * we can load the instruction and data TLB registers with the
	 * same values.
	 */
	lis	r8, KERNELBASE@h	/* Create vaddr for TLB */
	ori	r8, r8, MI_EVALID	/* Mark it valid */
	mtspr	MI_EPN, r8
	mtspr	MD_EPN, r8
	li	r8, MI_PS8MEG		/* Set 8M byte page */
	ori	r8, r8, MI_SVALID	/* Make it valid */
	mtspr	MI_TWC, r8
	mtspr	MD_TWC, r8
	li	r8, MI_BOOTINIT		/* Create RPN for address 0 */
	mtspr	MI_RPN, r8		/* Store TLB entry */
	mtspr	MD_RPN, r8
	lis	r8, MI_Kp@h		/* Set the protection mode */
	mtspr	MI_AP, r8
	mtspr	MD_AP, r8
#ifdef CONFIG_MBX
	/* Map another 8 MByte at 0xfa000000 to get the processor
	 * internal registers (among other things).
	 */
	lis	r8, 0xfa000000@h	/* Create vaddr for TLB */
	ori	r8, r8, MD_EVALID	/* Mark it valid */
	mtspr	MD_EPN, r8
	li	r8, MD_PS8MEG		/* Set 8M byte page */
	ori	r8, r8, MD_SVALID	/* Make it valid */
	mtspr	MD_TWC, r8
	lis	r8, 0xfa000000@h	/* Create paddr for TLB */
	ori	r8, r8, MI_BOOTINIT|0x2 /* Inhibit cache -- Cort */
	mtspr	MD_RPN, r8
#endif

	/* Since the cache is enabled according to the information we
	 * just loaded into the TLB, invalidate and enable the caches here.
	 * We should probably check/set other modes....later.
	 */
	lis	r8, IDC_INVALL@h
	mtspr	IC_CST, r8
	mtspr	DC_CST, r8
	lis	r8, IDC_ENABLE@h
	mtspr	IC_CST, r8
#if 0
	mtspr	DC_CST, r8
#else
	/* I still have a bug somewhere because the Ethernet driver
	 * does not want to work with copyback enabled.  For now,
	 * at least enable write through.
	 */
	lis	r8, DC_SFWT@h
	mtspr	DC_CST, r8
	lis	r8, IDC_ENABLE@h
	mtspr	DC_CST, r8
#endif

/* We now have the lower 8 Meg mapped into TLB entries, and the caches
 * ready to work.
 */
#endif /* CONFIG_8xx */

turn_on_mmu:
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

/* Data access exception.
 * This is "never generated" by the MPC8xx.  We jump to it for other
 * translation errors.
 */
	. = 0x300
DataAccess:
	EXCEPTION_PROLOG
	mfspr	r20,DSISR
#ifndef CONFIG_8xx
	andis.	r0,r20,0xa470		/* weird error? */
	bne	1f			/* if not, try to put a PTE */
	mfspr	r3,DAR			/* into the hash table */
	rlwinm	r4,r23,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	rlwimi	r4,r20,32-23,29,29	/* DSISR_STORE -> _PAGE_RW */
	mfspr	r5,SPRG3		/* phys addr of TSS */
	bl	hash_page
#endif
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

/* Instruction access exception.
 * This is "never generated" by the MPC8xx.  We jump to it for other
 * translation errors.
 */
	. = 0x400
InstructionAccess:
	EXCEPTION_PROLOG
#ifndef CONFIG_8xx
	andis.	r0,r23,0x4000		/* no pte found? */
	beq	1f			/* if so, try to put a PTE */
	mr	r3,r22			/* into the hash table */
	rlwinm	r4,r23,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	mr	r20,r23			/* SRR1 has reason bits */
	mfspr	r5,SPRG3		/* phys addr of TSS */
	bl	hash_page
#endif
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	mr	r4,r22
	mr	r5,r23
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	do_page_fault
	.long	int_return

/* External interrupt */
	. = 0x500;
HardwareInterrupt:
	EXCEPTION_PROLOG;
#ifdef CONFIG_APUS
	/* This is horrible, but there's no way around it. Enable the
	data cache so the IRQ hardware register can be accessed
	without cache intervention. Then disable interrupts and get
	the current emulated m68k IPL value. */
	
	mfmsr	20
	xori	r20,r20,MSR_DR
	sync
	mtmsr	r20
	sync

	lis	r3,APUS_IPL_EMU@h

	li	r20,(IPLEMU_SETRESET|IPLEMU_DISABLEINT)
	stb	r20,APUS_IPL_EMU@l(r3)
	eieio

	lbz	r3,APUS_IPL_EMU@l(r3)

	mfmsr	r20
	xori	r20,r20,MSR_DR
	sync
	mtmsr	r20
	sync

	stw	r3,(_CCR+4)(r21);
#endif
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	li	r4,0
	bl	transfer_to_handler
	.long	do_IRQ;
	.long	int_return
	

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

#ifndef CONFIG_8xx
/* Floating-point unavailable */
	. = 0x800
FPUnavailable:
	EXCEPTION_PROLOG
	bne	load_up_fpu		/* if from user, just load it up */
	li	r20,MSR_KERNEL
	bl	transfer_to_handler	/* if from kernel, take a trap */
	.long	KernelFP
	.long	int_return
#else
/* No FPU on MPC8xx.  This exception is not supposed to happen.
*/
	STD_EXCEPTION(0x800, FPUnavailable, UnknownException)
#endif

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

#ifndef CONFIG_8xx
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
#else
/* On the MPC8xx, this is a software emulation interrupt.  It occurs
 * for all unimplemented and illegal instructions.
 */
	STD_EXCEPTION(0x1000, SoftEmu, SoftwareEmulation)
#endif

/*
 * Handle TLB miss for DATA Load operation on 603/603e
 */
	. = 0x1100
#ifndef CONFIG_8xx
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
#else
/*
 * For the MPC8xx, this is a software tablewalk to load the instruction
 * TLB.  It is modelled after the example in the Motorola manual.  The task
 * switch loads the M_TWB register with the pointer to the first level table.
 * If we discover there is no second level table (the value is zero), the
 * plan was to load that into the TLB, which causes another fault into the
 * TLB Error interrupt where we can handle such problems.  However, that did
 * not work, so if we discover there is no second level table, we restore
 * registers and branch to the error exception.  We have to use the MD_xxx
 * registers for the tablewalk because the equivalent MI_xxx registers
 * only perform the attribute functions.
 */
InstructionTLBMiss:
	mtspr	M_TW, r20	/* Save a couple of working registers */
	mfcr	r20
	stw	r20, 0(r0)
	stw	r21, 4(r0)
	mfspr	r20, SRR0	/* Get effective address of fault */
	mtspr	MD_EPN, r20	/* Have to use MD_EPN for walk, MI_EPN can't */
	mfspr	r20, M_TWB	/* Get level 1 table entry address */
	lwz	r21, 0(r20)	/* Get the level 1 entry */
	rlwinm.	r20, r21,0,0,20	/* Extract page descriptor page address */
	beq	2f		/* If zero, don't try to find a pte */

	/* We have a pte table, so load the MI_TWC with the attributes
	 * for this page, which has only bit 31 set.
	 */
	tophys(r21,r21,0)
	ori	r21,r21,1		/* Set valid bit */
	mtspr	MI_TWC, r21	/* Set page attributes */
	mtspr	MD_TWC, r21	/* Load pte table base address */
	mfspr	r21, MD_TWC	/* ....and get the pte address */
	lwz	r21, 0(r21)	/* Get the pte */

	/* Set four subpage valid bits (24, 25, 26, and 27).
	 * Since we currently run MI_CTR.PPCS = 0, the manual says,
	 *	"If the page size is larger than 4k byte, then all the
	 *	 4 bits should have the same value."
	 * I don't really know what to do if the page size is 4k Bytes,
	 * but I know setting them all to 0 does not work, and setting them
	 * all to 1 does, so that is the way it is right now.
	 * BTW, these four bits map to the software only bits in the
	 * linux page table.  I used to turn them all of, but now just
	 * set them all for the hardware.
	li	r20, 0x00f0
	andc	r20, r21, r20
	ori	r20, r20, 0x0080
	 */
	ori	r20, r21, 0x00f0

	mtspr	MI_RPN, r20	/* Update TLB entry */

	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
	rfi

2:	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
	b	InstructionAccess
#endif /* CONFIG_8xx */
	
/*
 * Handle TLB miss for DATA Store on 603/603e
 */
	. = 0x1200
DataStoreTLBMiss:
#ifndef CONFIG_8xx
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
#else /* CONFIG_8xx */	
	mtspr	M_TW, r20	/* Save a couple of working registers */
	mfcr	r20
	stw	r20, 0(r0)
	stw	r21, 4(r0)
	mfspr	r20, M_TWB	/* Get level 1 table entry address */
	lwz	r21, 0(r20)	/* Get the level 1 entry */
	rlwinm.	r20, r21,0,0,20	/* Extract page descriptor page address */
	beq	2f		/* If zero, don't try to find a pte */

	/* We have a pte table, so load fetch the pte from the table.
	 */
	tophys(r21, r21, 0)
	ori	r21, r21, 1	/* Set valid bit in physical L2 page */
	mtspr	MD_TWC, r21	/* Load pte table base address */
	mfspr	r21, MD_TWC	/* ....and get the pte address */
	lwz	r21, 0(r21)	/* Get the pte */

	/* Set four subpage valid bits (24, 25, 26, and 27).
	 * Since we currently run MD_CTR.PPCS = 0, the manual says,
	 *	"If the page size is larger than 4k byte, then all the
	 *	 4 bits should have the same value."
	 * I don't really know what to do if the page size is 4k Bytes,
	 * but I know setting them all to 0 does not work, and setting them
	 * all to 1 does, so that is the way it is right now.
	 * BTW, these four bits map to the software only bits in the
	 * linux page table.  I used to turn them all of, but now just
	 * set them all for the hardware.
	li	r20, 0x00f0
	andc	r20, r21, r20
	ori	r20, r20, 0x0080
	 */
	ori	r20, r21, 0x00f0

	mtspr	MD_RPN, r20	/* Update TLB entry */

	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
	rfi

2:	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
	b	DataAccess
#endif /* CONFIG_8xx */

#ifndef CONFIG_8xx
/* Instruction address breakpoint exception (on 603/604) */
	STD_EXCEPTION(0x1300, Trap_13, InstructionBreakpoint)
#else

/* This is an instruction TLB error on the MPC8xx.  This could be due
 * to many reasons, such as executing guarded memory or illegal instruction
 * addresses.  There is nothing to do but handle a big time error fault.
 */
	. = 0x1300
InstructionTLBError:
	b	InstructionAccess
#endif

/* System management exception (603?) */
#ifndef CONFIG_8xx
	STD_EXCEPTION(0x1400, Trap_14, UnknownException)
#else

/* This is the data TLB error on the MPC8xx.  This could be due to
 * many reasons, including a dirty update to a pte.  We can catch that
 * one here, but anything else is an error.  First, we track down the
 * Linux pte.  If it is valid, write access is allowed, but the
 * page dirty bit is not set, we will set it and reload the TLB.  For
 * any other case, we bail out to a higher level function that can
 * handle it.
 */
	. = 0x1400
DataTLBError:
	mtspr	M_TW, r20	/* Save a couple of working registers */
	mfcr	r20
	stw	r20, 0(r0)
	stw	r21, 4(r0)

	/* First, make sure this was a store operation.
	*/
	mfspr	r20, DSISR
	andis.	r21, r20, 0x0200	/* If set, indicates store op */
	beq	2f

	mfspr	r20, M_TWB	/* Get level 1 table entry address */
	lwz	r21, 0(r20)	/* Get the level 1 entry */
	rlwinm.	r20, r21,0,0,20	/* Extract page descriptor page address */
	beq	2f		/* If zero, bail */

	/* We have a pte table, so fetch the pte from the table.
	 */
	tophys(r21, r21, 0)
	ori	r21, r21, 1		/* Set valid bit in physical L2 page */
	mtspr	MD_TWC, r21		/* Load pte table base address */
	mfspr	r21, MD_TWC		/* ....and get the pte address */
	lwz	r21, 0(r21)		/* Get the pte */

	andi.	r20, r21, _PAGE_RW	/* Is it writeable? */
	beq	2f			/* Bail out if not */

	ori	r21, r21, _PAGE_DIRTY	/* Update changed bit */
	mfspr	r20, MD_TWC		/* Get pte address again */
	stw	r21, 0(r20)		/* and update pte in table */

	/* Set four subpage valid bits (24, 25, 26, and 27).
	 * Since we currently run MD_CTR.PPCS = 0, the manual says,
	 *	"If the page size is larger than 4k byte, then all the
	 *	 4 bits should have the same value."
	 * I don't really know what to do if the page size is 4k Bytes,
	 * but I know setting them all to 0 does not work, and setting them
	 * all to 1 does, so that is the way it is right now.
	 * BTW, these four bits map to the software only bits in the
	 * linux page table.  I used to turn them all of, but now just
	 * set them all for the hardware.
	li	r20, 0x00f0
	andc	r20, r21, r20
	ori	r20, r20, 0x0080
	 */
	ori	r20, r21, 0x00f0

	mtspr	MD_RPN, r20	/* Update TLB entry */

	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
	rfi
2:
	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
	b	DataAccess
#endif /* CONFIG_8xx */

	STD_EXCEPTION(0x1500, Trap_15, UnknownException)
	STD_EXCEPTION(0x1600, Trap_16, UnknownException)
	STD_EXCEPTION(0x1700, Trap_17, TAUException)
	STD_EXCEPTION(0x1800, Trap_18, UnknownException)
	STD_EXCEPTION(0x1900, Trap_19, UnknownException)
	STD_EXCEPTION(0x1a00, Trap_1a, UnknownException)
	STD_EXCEPTION(0x1b00, Trap_1b, UnknownException)
/* On the MPC8xx, these next four traps are used for development
 * support of breakpoints and such.  Someday I will get around to
 * using them.
 */
	STD_EXCEPTION(0x1c00, Trap_1c, UnknownException)
	STD_EXCEPTION(0x1d00, Trap_1d, UnknownException)
	STD_EXCEPTION(0x1e00, Trap_1e, UnknownException)
	STD_EXCEPTION(0x1f00, Trap_1f, UnknownException)

#ifndef CONFIG_8xx
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
#else
	. = 0x2000
#endif

/*
 * This code finishes saving the registers to the exception frame
 * and jumps to the appropriate handler for the exception, turning
 * on address translation.
 */
	.globl	transfer_to_handler
transfer_to_handler:
	stw	r22,_NIP(r21)
	lis	r22,MSR_POW@h
	andc	r23,r23,r22
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

#ifndef CONFIG_8xx
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
Hash_base = 0x180000
Hash_bits = 12				/* e.g. 256kB hash table */
Hash_msk = (((1 << Hash_bits) - 1) * 64)
	
	.globl	hash_table_lock
hash_table_lock:
.long	0
	
	.globl	hash_page
hash_page:
#ifdef __SMP__
	lis	r2,hash_table_lock@h
	ori	r2,r2,hash_table_lock@l
	tophys(r2,r2,r6)
	lis	r6,100000000@h
	mtctr	r6
	lwz	r0,PROCESSOR-TSS(r5)
	or	r0,r0,r6
10:	lwarx	r6,0,r2
	cmpi	0,r6,0
	bne-	12f
	stwcx.	r0,0,r2
	beq+	11f
12:	cmpw	r6,r0
	bdnzf	2,10b
	tw	31,31,31
11:
#endif
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
#ifdef __SMP__
	tlbsync
#endif

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

	/*
	 * Choose an arbitrary slot in the primary PTEG to overwrite.
	 * Since both the primary and secondary PTEGs are full, and we
	 * have no information that the PTEs in the primary PTEG are
	 * more important or useful than those in the secondary PTEG,
	 * and we know there is a definite (although small) speed
	 * advantage to putting the PTE in the primary PTEG, we always
	 * put the PTE in the primary PTEG.
	 */
	xori	r5,r5,0x40		/* clear H bit again */
	lwz	r2,next_slot@l(0)
	addi	r2,r2,8
	andi.	r2,r2,0x38
	stw	r2,next_slot@l(0)
	add	r3,r4,r2
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
	SYNC
/*
 * These nop's seem to be necessary to avoid getting a machine
 * check on the rfi on 601 processors.
 */
	nop
	nop

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

#ifdef __SMP__
	lis	r2,hash_table_lock@ha
	tophys(r2,r2,r6)
	li	r0,0
	stw	r0,hash_table_lock@l(r2)
#endif

	/* Return from the exception */
	lwz	r3,_CCR(r21)
	lwz	r4,_LINK(r21)
	lwz	r5,_CTR(r21)
	mtcrf	0xff,r3
	mtlr	r4
	mtctr	r5
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
	lis	r2,hash_table_lock@ha
	tophys(r2,r2,r6)
	li	r0,0
	stw	r0,hash_table_lock@l(r2)
#endif		
	blr
next_slot:
	.long	0

load_up_fpu:
/*
 * Disable FP for the task which had the FPU previously,
 * and save its floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 * On SMP we know the fpu is free, since we give it up every
 * switch.  -- Cort
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
/*
 * All the saving of last_task_used_math is handled
 * by a switch_to() call to smp_giveup_fpu() in SMP so 
 * last_task_used_math is not used.
 * -- Cort
 */
#ifndef __SMP__
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
	li	r20,MSR_FP|MSR_FE0|MSR_FE1
	andc	r4,r4,r20		/* disable FP for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
#endif /* __SMP__ */
1:	ori	r23,r23,MSR_FP|MSR_FE0|MSR_FE1	/* enable use of FP after return */
	mfspr	r5,SPRG3		/* current task's TSS (phys) */
	lfd	fr0,TSS_FPSCR-4(r5)
	mtfsf	0xff,fr0
	REST_32FPRS(0, r5)
	subi	r4,r5,TSS
	sub	r4,r4,r6
#ifndef __SMP__
	stw	r4,last_task_used_math@l(r3)
#endif /* __SMP__ */
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
/* smp_giveup_fpu() takes an arg to tell it where to save the fpu
 * regs since last_task_used_math can't be trusted (many many race
 * conditions). -- Cort
 */
	.globl	smp_giveup_fpu
smp_giveup_fpu:	
	mr	r4,r3
	b	12f
	.globl	giveup_fpu
giveup_fpu:
	lis	r3,last_task_used_math@ha
	lwz	r4,last_task_used_math@l(r3)
12:		
	mfmsr	r5
	ori	r5,r5,MSR_FP
	SYNC
	mtmsr	r5			/* enable use of fpu now */
	SYNC
	cmpi	0,r4,0
	beqlr-				/* if no previous owner, done */
	addi	r4,r4,TSS	        /* want TSS of last_task_used_math */
#ifndef __SMP__
	li	r5,0
	stw	r5,last_task_used_math@l(r3)
#endif /* __SMP__ */
	SAVE_32FPRS(0, r4)
	mffs	fr0
	stfd	fr0,TSS_FPSCR-4(r4)
	lwz	r5,PT_REGS(r4)
	lwz	r3,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r4,MSR_FP|MSR_FE0|MSR_FE1
	andc	r3,r3,r4		/* disable FP for previous task */
	stw	r3,_MSR-STACK_FRAME_OVERHEAD(r5)
#else  /* CONFIG_8xx */
	.globl	giveup_fpu
giveup_fpu:
#endif /* CONFIG_8xx */
	blr

/*
 * This code is jumped to from the startup code to copy
 * the kernel image to physical address 0.
 */
relocate_kernel:
	lis	r9,0x426f		/* if booted from BootX, don't */
	addi	r9,r9,0x6f58		/* translate source addr */
	cmpw	r31,r9			/* (we have to on chrp) */
	beq	7f
	rlwinm	r4,r4,0,8,31		/* translate source address */
	add	r4,r4,r3		/* to region mapped with BATs */
7:	addis	r9,r26,klimit@ha	/* fetch klimit */
	lwz	r25,klimit@l(r9)
	addis	r25,r25,-KERNELBASE@h
	li	r6,0			/* Destination offset */
	li	r5,0x4000		/* # bytes of memory to copy */
	bl	copy_and_flush		/* copy the first 0x4000 bytes */
	addi	r0,r3,4f@l		/* jump to the address of 4f */
	mtctr	r0			/* in copy and do the rest. */
	bctr				/* jump to the copy */
4:	mr	r5,r25
	bl	copy_and_flush		/* copy the rest */
	b	turn_on_mmu

/*
 * Copy routine used to copy the kernel to start at physical address 0
 * and flush and invalidate the caches as needed.
 * r3 = dest addr, r4 = source addr, r5 = copy limit, r6 = start offset
 * on exit, r3, r4, r5 are unchanged, r6 is updated to be >= r5.
 */
copy_and_flush:
	addi	r5,r5,-4
	addi	r6,r6,-4
4:	li	r0,8
	mtctr	r0
3:	addi	r6,r6,4			/* copy a cache line */
	lwzx	r0,r6,r4
	stwx	r0,r6,r3
	bdnz	3b
	dcbst	r6,r3			/* write it to memory */
	sync
	icbi	r6,r3			/* flush the icache line */
	cmplw	0,r6,r5
	blt	4b
	isync
	addi	r5,r5,4
	addi	r6,r6,4
	blr

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
#ifndef CONFIG_8xx
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
#endif /* CONFIG_8xx */
#ifdef __SMP__
	/* if we're the second cpu stack and r2 are different
	* and we want to not clear the bss -- Cort */
	lis	r5,first_cpu_booted@h
	ori	r5,r5,first_cpu_booted@l
	lwz	r5,0(r5)
	cmpi	0,r5,0
	beq	99f

	/* get current */
	lis	r2,current_set@h
	ori	r2,r2,current_set@l
	addi	r2,r2,4
	lwz	r2,0(r2)
	
	b	10f
99:	
#endif /* __SMP__ */
	/* ptr to current */
	lis	r2,init_task_union@h
	ori	r2,r2,init_task_union@l
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
#ifdef __SMP__	
10:
#endif /* __SMP__ */
	
	/* stack */
	addi	r1,r2,TASK_UNION_SIZE
	li	r0,0
	stwu	r0,-STACK_FRAME_OVERHEAD(r1)
	
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
 * On the 8xx, all we have to do is invalidate the TLB to clear
 * the old 8M byte TLB mappings and load the page table base register.
 */
#ifndef CONFIG_8xx
	lis	r6,_SDR1@ha
	lwz	r6,_SDR1@l(r6)
#else
	/* The right way to do this would be to track it down through
	 * init's TSS like the context switch code does, but this is
	 * easier......until someone changes init's static structures.
	 */
	lis	r6, swapper_pg_dir@h
	tophys(r6,r6,0)
	ori	r6, r6, swapper_pg_dir@l
	mtspr	M_TWB, r6
#endif
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
#ifdef __SMP__
	tlbsync
#endif
#ifndef CONFIG_8xx
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
	mfpvr	r3
	srwi	r3,r3,16
	cmpwi	r3,1
	lis	r3,BATS@ha
	addi	r3,r3,BATS@l
	tophys(r3,r3,r4)
	LOAD_BAT(0,0,r3,r4,r5)
	LOAD_BAT(1,16,r3,r4,r5)
	LOAD_BAT(2,32,r3,r4,r5)
	LOAD_BAT(3,48,r3,r4,r5)
#endif /* CONFIG_8xx */
/* Set up for using our exception vectors */
	/* ptr to phys current tss */
	tophys(r4,r2,r4)
	addi	r4,r4,TSS	/* init task's TSS */
	mtspr	SPRG3,r4
	li	r3,0
	mtspr	SPRG2,r3	/* 0 => r1 has kernel sp */
/* Now turn on the MMU for real! */
	li	r4,MSR_KERNEL
	lis	r3,start_kernel@h
	ori	r3,r3,start_kernel@l
#ifdef __SMP__
	/* the second time through here we go to
	 * start_secondary(). -- Cort
	 */
	lis	r5,first_cpu_booted@h
	ori	r5,r5,first_cpu_booted@l
	tophys(r5,r5,r3)
	lwz	r5,0(r5)
	cmpi	0,r5,0
	beq	10f
	lis	r3,start_secondary@h
	ori	r3,r3,start_secondary@l
10:	
#endif /* __SMP__ */
	mtspr	SRR0,r3
	mtspr	SRR1,r4
	rfi			/* enable MMU and jump to start_kernel */

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
 *
 * The MPC8xx has something that currently happens "automagically."
 * Unshared user space address translations are subject to ASID (context)
 * match.  During each task switch, the ASID is incremented.  We can
 * guarantee (I hope :-) that no entries currently match this ASID
 * because every task will cause at least a TLB entry to be loaded for
 * the first instruction and data access, plus the kernel running will
 * have displaced several more TLBs.  The MMU contains 32 entries for
 * each TLB, and there are 16 contexts, so we just need to make sure
 * two pages get replaced for every context switch, which currently
 * happens.  There are other TLB management techniques that I will
 * eventually implement, but this is the easiest for now.  -- Dan
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
#ifndef CONFIG_8xx
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
#else
/* On the MPC8xx, we place the physical address of the new task
 * page directory loaded into the MMU base register, and set the
 * ASID compare register with the new "context".
 */
        lwz     r3,MM-TSS(r4)           /* Get virtual address of mm */
        lwz     r3,PGD(r3)              /* get new->mm->pgd */
        addis   r3,r3,-KERNELBASE@h     /* convert to phys addr */
        mtspr   M_TWB, r3               /* Update MMU base address */
        mtspr   M_CASID, r5             /* Update context */
        tlbia
#endif
	SYNC

/* FALL THROUGH into int_return */
#ifdef __SMP__
	/* call schedule_tail if this is the first time for a child process */
	lwz	r5,TSS_SMP_FORK_RET(r4)
	cmpi	0,r5,0
	beq+	int_return
	li	r3,0
	stw	r3,TSS_SMP_FORK_RET(r4)
	bl	schedule_tail
#endif /* __SMP__ */

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
3:	lis	r4,n_lost_interrupts@ha
	lwz	r4,n_lost_interrupts@l(r4)
	cmpi	0,r4,0
	beq+	1f
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_IRQ
	.globl	lost_irq_ret
lost_irq_ret:
	b	3b
1:	lis	r4,bh_mask@ha
	lwz	r4,bh_mask@l(r4)
	lis	r5,bh_active@ha
	lwz	r5,bh_active@l(r5)
	and.	r4,r4,r5
	beq+	2f
	bl	do_bottom_half
	.globl	do_bottom_half_ret
do_bottom_half_ret:
	SYNC
	mtmsr	r30		/* disable interrupts again */
	SYNC
2:	lwz	r3,_MSR(r1)	/* Returning to user mode? */
	andi.	r3,r3,MSR_PR
	beq+	10f		/* if so, check need_resched and signals */
	lwz	r3,NEED_RESCHED(r2)
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
	.globl	do_signal_ret
do_signal_ret:
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
	li	r4,1
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
#ifndef CONFIG_8xx	
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
	beq+	99f
	tlbia
	isync
	blr
99:
#endif /* NO_RELOAD_HTAB */
#ifdef __SMP__
	/* Note - we had better not do anything which could generate
	   a hash table miss while we have the hash table locked,
	   or we'll get a deadlock.  -paulus */
	mfmsr	r10
	sync
	rlwinm	r0,r10,0,17,15	/* clear bit 16 (MSR_EE) */
	mtmsr	r0
	SYNC
	lis	r9,hash_table_lock@h
	ori	r9,r9,hash_table_lock@l
	lwz	r8,PROCESSOR(r2)
	oris	r8,r8,8
10:	lwarx	r6,0,r9
	cmpi	0,r6,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
#endif
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
	tlbia
	isync
#ifdef __SMP__
	tlbsync
	lis	r3,hash_table_lock@ha
	li	r0,0
	stw	r0,hash_table_lock@l(r3)
	mtmsr	r10
	SYNC
#endif
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
	beq+	99f
	tlbie	r4			/* in hw tlb too */
	isync
	blr
99:
#endif /* NO_RELOAD_HTAB */		
#ifdef __SMP__
	/* Note - we had better not do anything which could generate
	   a hash table miss while we have the hash table locked,
	   or we'll get a deadlock.  -paulus */
	mfmsr	r10
	sync
	rlwinm	r0,r10,0,17,15		/* clear bit 16 (MSR_EE) */
	mtmsr	r0
	SYNC
	lis	r9,hash_table_lock@h
	ori	r9,r9,hash_table_lock@l
	lwz	r8,PROCESSOR(r2)
	oris	r8,r8,9
10:	lwarx	r6,0,r9
	cmpi	0,r6,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
#endif
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
	tlbie	r4			/* in hw tlb too */
	isync
#ifdef __SMP__
	tlbsync
	lis	r3,hash_table_lock@h
	li	r0,0
	stw	r0,hash_table_lock@l(r3)
	mtmsr	r10
	SYNC
#endif
	blr
#endif /* CONFIG_8xx */

/*
 * This routine is just here to keep GCC happy - sigh...
 */	
_GLOBAL(__main)
	blr

/*
 * PROM code for specific machines follows.  Put it 
 * here so it's easy to add arch-specific sections later.
 * -- Cort
 */
	
#ifndef CONFIG_8xx		
/*
 * On CHRP, the Run-Time Abstraction Services (RTAS) have to be
 * called with the MMU off.
 */
	.globl	enter_rtas
enter_rtas:
	stwu	r1,-16(r1)
	mflr	r0
	stw	r0,20(r1)
	lis	r4,rtas_data@ha
	lwz	r4,rtas_data@l(r4)
	addis	r4,r4,-KERNELBASE@h
	lis	r6,1f@ha	/* physical return address for rtas */
	addi	r6,r6,1f@l
	addis	r6,r6,-KERNELBASE@h
	subi	r7,r1,INT_FRAME_SIZE+STACK_UNDERHEAD
	addis	r7,r7,-KERNELBASE@h
	lis	r8,rtas_entry@ha
	lwz	r8,rtas_entry@l(r8)
	addis	r5,r8,-KERNELBASE@h
	mfmsr	r9
	stw	r9,8(r1)
	ori	r0,r0,MSR_EE|MSR_SE|MSR_BE
	andc	r0,r9,r0
	andi.	r9,r9,MSR_ME|MSR_RI
	sync			/* disable interrupts so SRR0/1 */
	mtmsr	r0		/* don't get trashed */
	li	r6,0	
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
#endif /* CONFIG_8xx */

#ifdef CONFIG_MBX
/* Jump into the system reset for the MBX rom.
 * We first disable the MMU, and then jump to the ROM reset address.
 *
 * This does not work, don't bother trying.  There is no place in
 * the ROM we can jump to cause a reset.  We will have to program
 * a watchdog of some type that we don't service to cause a processor
 * reset.
 */
	.globl	MBX_gorom
MBX_gorom:
	li	r3,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	lis	r4,2f@h
	addis	r4,r4,-KERNELBASE@h
	ori	r4,r4,2f@l
	mtspr	SRR0,r4
	mtspr	SRR1,r3
	rfi
2:
	lis	r4, 0xfe000000@h
	addi	r4, r4, 0xfe000000@l
	mtlr	r4
	blr
#endif /* CONFIG_MBX */
	
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
