/*
 * include/asm-mips/asm.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Ralf Baechle
 *
 * Some useful macros for MIPS assembler code
 *
 * Some of the routines below contain useless nops that will be optimized
 * away by gas in -O mode. These nops are however required to fill delay
 * slots in noreorder mode.
 */
#ifndef	__ASM_ASM_H
#define	__ASM_ASM_H

#include <linux/config.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>

#ifndef CAT
#ifdef __STDC__
#define __CAT(str1,str2) str1##str2
#else
#define __CAT(str1,str2) str1/**/str2
#endif
#define CAT(str1,str2) __CAT(str1,str2)
#endif

/*
 * Macros to handle different pointer sizes for 32/64-bit code
 */
#if __mips == 3
#define PTR	.quad
#define PTRSIZE	8
#define PTRLOG	3
#define lp	ld
#else
#define PTR	.word
#define PTRSIZE	4
#define PTRLOG	2
#define lp	lw
#endif

/*
 * ELF specific declarations
 */
#ifdef __ELF__
#define TYPE(symbol,_type)                              \
			.type	symbol,@_type
#define SIZE(symbol,_size)                              \
			.size	symbol,_size
#else
#define TYPE(symbol,type)
#define SIZE(symbol,size)
#endif

/*
 * PIC specific declarations
 * Not used for the kernel but here seems to be the right place.
 */
#ifdef __PIC__
#define CPRESTORE(register)                             \
			.cprestore register
#define CPADD(register)                                 \
			.cpadd	register
#define CPLOAD(register)                                \
			.cpload	register
#else
#define CPRESTORE(register)
#define CPADD(register)
#define CPLOAD(register)
#endif

/*
 * LEAF - declare leaf routine
 */
#define	LEAF(symbol)                                    \
			.globl	symbol;                 \
			.align	2;                      \
			TYPE(symbol,function);          \
			.ent	symbol,0;               \
symbol:			.frame	sp,0,ra

/*
 * NESTED - declare nested routine entry point
 */
#define	NESTED(symbol, framesize, rpc)                  \
			.globl	symbol;                 \
			.align	2;                      \
			TYPE(symbol,function);          \
			.ent	symbol,0;               \
symbol:			.frame	sp, framesize, rpc

/*
 * END - mark end of function
 */
#define	END(function)                                   \
			.end	function;		\
			SIZE(function,.-function)

/*
 * EXPORT - export definition of symbol
 */
#define	EXPORT(symbol)                                  \
			.globl	symbol;                 \
symbol:

/*
 * ABS - export absolute symbol
 */
#define	ABS(symbol,value)                               \
			.globl	symbol;                 \
symbol			=	value

#define	PANIC(msg)                                      \
			la	a0,8f;                  \
			jal	panic;                  \
			nop;                            \
9:			b	9b;                     \
			nop;                            \
			TEXT(msg)

/*
 * Print formated string
 */
#define PRINT(string)                                   \
			la	a0,8f;                  \
			jal	printk;                 \
			nop;                            \
			TEXT(string)

#define	TEXT(msg)                                       \
			.data;                          \
8:			.asciiz	msg;                    \
			.text

/*
 * Build text tables
 */
#define TTABLE(string)                                  \
		.text;                                  \
		.word	1f;                             \
		.data;                                  \
1:		.asciz	string;                         \
		.text;

/*
 * Move to kernel mode and disable interrupts
 * Set cp0 enable bit as sign that we're running on the kernel stack
 * Use with .set noat!
 * Note that the mtc0 will be effective on R4000 pipeline stage 7. This
 * means that another three instructions will be executed with interrupts
 * disabled.
 */
#define CLI                                             \
		mfc0	AT,CP0_STATUS;                  \
		li	t0,ST0_CU0|0x1f;                \
		or	AT,t0;                          \
		xori	AT,0x1f;                        \
		mtc0	AT,CP0_STATUS;			\

/*
 * Move to kernel mode and enable interrupts
 * Set cp0 enable bit as sign that we're running on the kernel stack
 * Use with .set noat!
 * Note that the mtc0 will be effective on R4000 pipeline stage 7. This
 * means that another three instructions will be executed with interrupts
 * disabled.  Arch/mips/kernel/r4xx0.S makes use of this fact.
 */
#define STI                                             \
		mfc0	AT,CP0_STATUS;                  \
		li	t0,ST0_CU0|0x1f;                \
		or	AT,t0;                          \
		xori	AT,0x1e;                        \
		mtc0	AT,CP0_STATUS;			\

/*
 * Special nop to fill load delay slots
 */
#ifndef __R4000__
#define NOP     nop
#else
#define NOP
#endif

/*
 * Return from exception
 */
#if defined (CONFIG_CPU_R3000)
#define ERET rfe
#elif defined (CONFIG_CPU_R4X00) || defined (CONFIG_CPU_R4600)
#define ERET                                            \
		.set	mips3;                          \
		eret;                                   \
		.set	mips0
#else
#error "Implement ERET macro!"
#endif

/*
 * R8000/R10000 (MIPS ISA IV) pref instruction.
 * Use with .set noreorder only!
 */
#if defined (CONFIG_CPU_R8000) || defined(CONFIG_CPU_R10000)
#define PREF(hint,addr)                                 \
		pref	hint,addr
#define PREFX(hint,addr)                                \
		prefx	hint,addr
#else
#define PREF
#define PREFX
#endif

/*
 * R8000/R10000 (MIPS ISA IV) movn/movz instructions and
 * equivalents for old CPUs. Use with .set noreorder only!
 */
#if defined (CONFIG_CPU_R8000) || defined (CONFIG_CPU_R10000)
#define MOVN(rd,rs,rt)                                  \
		movn	rd,rs,rt
#define MOVZ(rd,rs,rt)                                  \
		movz	rd,rs,rt
#elif defined (CONFIG_CPU_R4000) || defined (CONFIG_CPU_R6000)
#define MOVN(rd,rs,rt)                                  \
		bnezl	rt,9f                           \
		move	rd,rs                           \
9:
#define MOVZ(rd,rs,rt)                                  \
		beqzl	rt,9f                           \
		movz	rd,rt                           \
9:
#else /* R2000, R3000 */
#define MOVN(rd,rs,rt)                                  \
		beqz	rt,9f                           \
		nop                                     \
		move	rd,rs                           \
9:
#define MOVZ(rd,rs,rt)                                  \
		bneqz	rt,9f                           \
		nop                                     \
		movz	rd,rt                           \
9:
#endif

#endif /* __ASM_ASM_H */
