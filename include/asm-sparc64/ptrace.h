/* $Id: ptrace.h,v 1.1 1996/12/12 11:59:35 davem Exp $ */
#ifndef _SPARC64_PTRACE_H
#define _SPARC64_PTRACE_H

#include <asm/pstate.h>

/* This struct defines the way the registers are stored on the 
 * stack during a system call and basically all traps.
 */

#ifndef __ASSEMBLY__

struct pt_regs {
	unsigned long u_regs[16]; /* globals and ins */
	unsigned long tstate;
	unsigned long tpc;
	unsigned long tnpc;
	unsigned long y;
};

#define UREG_G0        0
#define UREG_G1        1
#define UREG_G2        2
#define UREG_G3        3
#define UREG_G4        4
#define UREG_G5        5
#define UREG_G6        6
#define UREG_G7        7
#define UREG_I0        8
#define UREG_I1        9
#define UREG_I2        10
#define UREG_I3        11
#define UREG_I4        12
#define UREG_I5        13
#define UREG_I6        14
#define UREG_I7        15
#define UREG_FP        UREG_I6
#define UREG_RETPC     UREG_I7

/* A V9 register window */
struct reg_window {
	unsigned long locals[8];
	unsigned long ins[8];
};

/* A 32-bit register window. */
struct reg_window_32 {
	unsigned int locals[8];
	unsigned int ins[8];
};

/* A V9 Sparc stack frame */
struct sparc_stackf {
	unsigned long locals[8];
        unsigned long ins[6];
	struct sparc_stackf *fp;
	unsigned long callers_pc;
	char *structptr;
	unsigned long xargs[6];
	unsigned long xxargs[1];
};	

/* A 32-bit Sparc stack frame */
struct sparc_stackf_32 {
	unsigned int locals[8];
        unsigned int ins[6];
	unsigned int fp;
	unsigned int callers_pc;
	unsigned int structptr;
	unsigned int xargs[6];
	unsigned int xxargs[1];
};	

#define TRACEREG_SZ   sizeof(struct pt_regs)
#define STACKFRAME_SZ sizeof(struct sparc_stackf)
#define REGWIN_SZ     sizeof(struct reg_window)

#ifdef __KERNEL__
#define user_mode(regs) (!((regs)->tstate & PSR_PS))
#define instruction_pointer(regs) ((regs)->tpc)
extern void show_regs(struct pt_regs *);
#endif

#else /* __ASSEMBLY__ */
/* For assembly code. */
#define TRACEREG_SZ       0x50
#define STACKFRAME_SZ     0x60
#define REGWIN_SZ         0x40
#endif
