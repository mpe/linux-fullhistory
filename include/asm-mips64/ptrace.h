/* $Id: ptrace.h,v 1.3 1999/12/04 03:59:12 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_PTRACE_H
#define _ASM_PTRACE_H

#include <linux/types.h>

/* 0 - 31 are integer registers, 32 - 63 are fp registers.  */
#define FPR_BASE	32
#define PC		64
#define CAUSE		65
#define BADVADDR	66
#define MMHI		67
#define MMLO		68
#define FPC_CSR		69
#define FPC_EIR		70

#ifndef _LANGUAGE_ASSEMBLY

#define abi64_no_regargs						\
	unsigned long __dummy0,						\
	unsigned long __dummy1,						\
	unsigned long __dummy2,						\
	unsigned long __dummy3,						\
	unsigned long __dummy4,						\
	unsigned long __dummy5,						\
	unsigned long __dummy6,						\
	unsigned long __dummy7

/*
 * This struct defines the way the registers are stored on the stack during a
 * system call/exception. As usual the registers k0/k1 aren't being saved.
 */
struct pt_regs {
	/* Saved main processor registers. */
	unsigned long regs[32];

	/* Other saved registers. */
	unsigned long lo;
	unsigned long hi;

	/*
	 * saved cp0 registers
	 */
	unsigned long cp0_epc;
	unsigned long cp0_badvaddr;
	unsigned long cp0_status;
	unsigned long cp0_cause;
};

#endif /* !(_LANGUAGE_ASSEMBLY__) */

#ifdef _LANGUAGE_ASSEMBLY
#include <asm/offset.h>
#endif /* (_LANGUAGE_ASSEMBLY__) */

#ifdef __KERNEL__

#ifndef _LANGUAGE_ASSEMBLY
#define instruction_pointer(regs) ((regs)->cp0_epc)

extern void (*_show_regs)(struct pt_regs *);
#define show_regs(regs)	_show_regs(regs)

#endif /* !(_LANGUAGE_ASSEMBLY__) */

#endif

#endif /* _ASM_PTRACE_H */
