#ifndef _I386_PTRACE_H
#define _I386_PTRACE_H


/* this struct defines the way the registers are stored on the 
   stack during a system call. */

struct pt_regs {
	unsigned long ps;
	unsigned long pc;
	unsigned long gp;
	unsigned long a0;
	unsigned long a1;
	unsigned long a2;
};

#define user_mode(regs) ((regs)->ps & 8)

#endif
