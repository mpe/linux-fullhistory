/*
 * linux/include/asm-mips/ptrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 by Waldorf GMBH
 * written by Ralf Baechle
 *
 * Machine dependent structs and defines to help the user use
 * the ptrace system call.
 */
#ifndef __ASM_MIPS_PTRACE_H
#define __ASM_MIPS_PTRACE_H

/*
 * This struct defines the way the registers are stored on the stack during a
 * system call/exception. As usual the registers k0/k1 aren't being saved.
 */
struct pt_regs {
	/*
	 * Pad bytes for argument save space on the stack
	 * 20/40 Bytes for 32/64 bit code
	 */
	unsigned long pad0[5];

	/*
	 * saved main processor registers
	 */
	long	        reg1,  reg2,  reg3,  reg4,  reg5,  reg6,  reg7;
	long	 reg8,  reg9, reg10, reg11, reg12, reg13, reg14, reg15;
	long	reg16, reg17, reg18, reg19, reg20, reg21, reg22, reg23;
	long	reg24, reg25,               reg28, reg29, reg30, reg31;

	/*
	 * Saved special registers
	 */
	long	lo;
	long	hi;

	/*
	 * saved cp0 registers
	 */
	unsigned long cp0_status;
	unsigned long cp0_epc;
	unsigned long cp0_cause;

	/*
	 * Some goodies...
	 */
	unsigned long interrupt;
	long orig_reg2;
	long pad1;
};

#ifdef __KERNEL__

/*
 * Does the process account for user or for system time?
 */
#if defined (__R4000__)

#define user_mode(regs) ((regs)->cp0_status & 0x10)

#else /* !defined (__R4000__) */

#define user_mode(regs) (!((regs)->cp0_status & 0x8))

#endif /* !defined (__R4000__) */

#define instruction_pointer(regs) ((regs)->cp0_epc)
extern void show_regs(struct pt_regs *);
#endif

#endif /* __ASM_MIPS_PTRACE_H */
