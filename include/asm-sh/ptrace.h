#ifndef __ASM_SH_PTRACE_H
#define __ASM_SH_PTRACE_H

/*
 * Copyright (C) 1999  Niibe Yutaka
 *
 */

/*
 * This struct defines the way the registers are stored on the
 * kernel stack during a system call or other kernel entry.
 */
struct pt_regs {
	long syscall_nr;
	unsigned long sr;
	unsigned long sp;
	unsigned long regs[15];
	unsigned long gbr;
	unsigned long mach;
	unsigned long macl;
	unsigned long pr;
	unsigned long pc;
};

#ifdef __KERNEL__
#define user_mode(regs) (((regs)->sr & 0x40000000)==0)
#define instruction_pointer(regs) ((regs)->pc)
extern void show_regs(struct pt_regs *);
#endif

#endif /* __ASM_SH_PTRACE_H */
