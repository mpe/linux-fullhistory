/*
 * linux/include/asm-mips/ptrace.h
 *
 * machine dependent structs and defines to help the user use
 * the ptrace system call.
 */
#ifndef __ASM_MIPS_PTRACE_H
#define __ASM_MIPS_PTRACE_H

/*
 * use ptrace (3 or 6, pid, PT_EXCL, data); to read or write
 * the processes registers.
 *
 * This defines/structures correspond to the register layout on stack -
 * if the order here is changed, it needs to be updated in
 * arch/mips/fork.c:copy_process, asm/mips/signal.c:do_signal,
 * asm-mips/ptrace.c, include/asm-mips/ptrace.h.
 */

#include <asm/stackframe.h>

/*
 * This struct defines the way the registers are stored on the 
 * stack during a system call/exception. As usual the registers
 * k0/k1 aren't being saved.
 */
struct pt_regs {
	/*
	 * Pad bytes for argument save space on the stack
	 */
	unsigned long pad0[FR_REG1/sizeof(unsigned long)];

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
};

/*
 * Does the process account for user or for system time?
 */
#if defined (__R4000__)

#define user_mode(regs) (!((regs)->cp0_status & 0x18))

#else /* !defined (__R4000__) */

#error "#define user_mode(regs) for R3000!"

#endif /* !defined (__R4000__) */

#endif /* __ASM_MIPS_PTRACE_H */
