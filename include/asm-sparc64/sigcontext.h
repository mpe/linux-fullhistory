/* $Id: sigcontext.h,v 1.4 1997/04/04 00:50:28 davem Exp $ */
#ifndef __SPARC64_SIGCONTEXT_H
#define __SPARC64_SIGCONTEXT_H

#include <asm/ptrace.h>

/* XXX This gets exported to userland as well as kernel, it is probably
 * XXX riddled with many hard to find 32-bit binary compatability issues.
 * XXX Signals and this file need to be investigated heavily. -DaveM
 */

#define SUNOS_MAXWIN   31

#ifndef __ASSEMBLY__

/* SunOS system call sigstack() uses this arg. */
struct sunos_sigstack {
	unsigned int sig_sp;
	int onstack_flag;
};

/* This is what SunOS does, so shall I. */
struct sigcontext32 {
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
	unsigned sigc_spbuf[SUNOS_MAXWIN];

	/* Windows to restore after signal */
	struct reg_window32 sigc_wbuf[SUNOS_MAXWIN];
};

/* This is what SunOS doesn't, so we have to write this alone. */
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
	char *sigc_spbuf[SUNOS_MAXWIN];

	/* Windows to restore after signal */
	struct reg_window sigc_wbuf[SUNOS_MAXWIN];
};

typedef struct {
	struct pt_regs32	si_regs;
	int			si_mask;
} __siginfo32_t;

typedef struct {
	unsigned int si_float_regs [32];
	unsigned int si_fsr;
	unsigned int si_fpqdepth;
	struct {
		unsigned int *insn_addr;
		unsigned int insn;
	} si_fpqueue [16];
} __siginfo_fpu32_t;


typedef struct {
	struct     pt_regs si_regs;
	int si_mask;
} __siginfo_t;

typedef struct {
	unsigned   int si_float_regs [64];
	unsigned   long si_fsr;
	unsigned   int si_fpqdepth;
	struct {
		unsigned int *insn_addr;
		unsigned int insn;
	} si_fpqueue [16];
} __siginfo_fpu_t;

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_SIGCONTEXT_H) */
