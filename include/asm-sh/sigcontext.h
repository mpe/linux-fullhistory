#ifndef __ASM_SH_SIGCONTEXT_H
#define __ASM_SH_SIGCONTEXT_H

struct sigcontext {
	unsigned long	oldmask;

	/* CPU registers */
	unsigned long   u_regs[16];
	unsigned long gbr;
	unsigned long mach;
	unsigned long macl;
	unsigned long pr;
	unsigned long sr;
	unsigned long pc;
};

#endif /* __ASM_SH_SIGCONTEXT_H */
