/* $Id: sigcontext.h,v 1.1 1996/12/26 14:22:36 davem Exp $ */
#ifndef _ASMsparc64_SIGCONTEXT_H
#define _ASMsparc64_SIGCONTEXT_H

#include <asm/ptrace.h>

#define SUNOS_MAXWIN   31

#ifndef __ASSEMBLY__

/* SunOS system call sigstack() uses this arg. */
struct sunos_sigstack {
	unsigned int sig_sp;
	int onstack_flag;
};

/* This is what SunOS does, so shall I. */
struct sigcontext {
	int sigc_onstack;      /* state to restore */
	int sigc_mask;         /* sigmask to restore */
	int sigc_sp;           /* stack pointer */
	int sigc_pc;           /* program counter */
	int sigc_npc;          /* next program counter */
	int sigc_psr;          /* for condition codes etc */
	int sigc_g1;           /* User uses these two registers */
	int sigc_o0;           /* within the trampoline code. */

	/* Now comes information regarding the users window set
	 * at the time of the signal.
	 */
	int sigc_oswins;       /* outstanding windows */

	/* stack ptrs for each regwin buf */
	/* XXX 32-bit ptrs pinhead... */
	char *sigc_spbuf[SUNOS_MAXWIN];

	/* Windows to restore after signal */
	struct reg_window32 sigc_wbuf[SUNOS_MAXWIN];
};

typedef struct {
	struct     pt_regs32 si_regs;
	unsigned   int si_float_regs [64];
	unsigned   int si_fsr;
	unsigned   int si_fpqdepth;
	struct {
		unsigned int *insn_addr;
		unsigned int insn;
	} si_fpqueue [16];
	int si_mask;
} __siginfo32_t;

typedef struct {
	struct     pt_regs si_regs;
	unsigned   long si_float_regs [64];
	unsigned   long si_fsr;
	unsigned   int si_fpqdepth;
	struct {
		unsigned int *insn_addr;
		unsigned int insn;
	} si_fpqueue [16];
	int si_mask;
} __siginfo_t;

#endif /* !(__ASSEMBLY__) */

#endif /* !(_ASMsparc64_SIGCONTEXT_H) */
