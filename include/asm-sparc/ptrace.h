#ifndef _SPARC_PTRACE_H
#define _SPARC_PTRACE_H

/* I have not looked enough into how this should be done. Without playing
 * lots of tricks to optimize I think we need to save the whole register
 * window frame plus the floating-point registers. We'll see...
 */

/* this struct defines the way the registers are stored on the 
   stack during a system call. */

struct pt_regs {
	unsigned long ps;    /* previous supervisor, same as alpha I believe */
	unsigned long pc;    /* current and next program counter */
	unsigned long npc;
	unsigned long sp;    /* stack and frame pointer */
	unsigned long fp;
	unsigned long psr;   /* for condition codes */
	unsigned long nuwin; /* number of user windows */
	/* not sure yet whether all regs are necessary
	 * but this is how it is traditionally done on the sparc.
	 */
	unsigned long u_regs[24*16];
	unsigned long f_regs[64];    /* yuck yuck yuck */
};

#ifdef __KERNEL__
#define user_mode(regs) (0x0)  /* if previous supervisor is 0, came from user */
extern void show_regs(struct pt_regs *);
#endif

#endif
