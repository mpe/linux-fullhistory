#ifndef __ASM_SH_SIGCONTEXT_H
#define __ASM_SH_SIGCONTEXT_H

struct sigcontext {
	unsigned long	oldmask;

	/* CPU registers */
	unsigned long sc_regs[15];
	unsigned long sc_gbr;
	unsigned long sc_mach;
	unsigned long sc_macl;
	unsigned long sc_pr;
	unsigned long sc_sp;
	unsigned long sc_sr;
	unsigned long sc_pc;
};

#endif /* __ASM_SH_SIGCONTEXT_H */
