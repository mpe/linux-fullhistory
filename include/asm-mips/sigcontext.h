#ifndef __ASM_MIPS_SIGCONTEXT_H
#define __ASM_MIPS_SIGCONTEXT_H

#ifdef __LANGUAGE_ASSEMBLY__

#define SC_REGMASK	0
#define SC_STATUS	4
#define SC_PC		8
#define SC_REGS		16
#define SC_FPREGS	272
#define SC_OWNEDFP	528
#define SC_FPC_CSR	532
#define SC_FPC_EIR	536
#define SC_SSFLAGS	540
#define SC_MDHI		544
#define SC_MDLO		552

#endif

#if defined(__LANGUAGE_C__) || \
    defined(_LANGUAGE_C) || \
    defined(__LANGUAGE_C_PLUS_PLUS__) || \
    defined(__LANGUAGE_OBJECTIVE_C__)

/*
 * Whenever this structure is changed you must update the offsets in
 * arch/mips/mips<isa>/fp-context.S.
 */
struct sigcontext {
	unsigned int       sc_regmask;		/* Unused */
	unsigned int       sc_status;
	unsigned long long sc_pc;
	unsigned long long sc_regs[32];
	unsigned long long sc_fpregs[32];	/* Unused */
	unsigned int       sc_ownedfp;
	unsigned int       sc_fpc_csr;		/* Unused */
	unsigned int       sc_fpc_eir;		/* Unused */
	unsigned int       sc_ssflags;		/* Unused */
	unsigned long long sc_mdhi;
	unsigned long long sc_mdlo;

	unsigned int       sc_cause;		/* Unused */
	unsigned int       sc_badvaddr;		/* Unused */

	sigset_t           sc_sigset;
	unsigned long      __pad0[3];		/* pad for constant size */
};
#endif

#endif /* __ASM_MIPS_SIGCONTEXT_H */
