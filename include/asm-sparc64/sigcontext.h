/* $Id: sigcontext.h,v 1.11 1998/10/06 09:28:37 jj Exp $ */
#ifndef __SPARC64_SIGCONTEXT_H
#define __SPARC64_SIGCONTEXT_H

#include <asm/ptrace.h>

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
	unsigned sigc_spbuf[SUNOS_MAXWIN];

	/* Windows to restore after signal */
	struct reg_window32 sigc_wbuf[SUNOS_MAXWIN];
};

/* This is what SunOS doesn't, so we have to write this alone. */
struct sigcontext {
	int sigc_onstack;      /* state to restore */
	int sigc_mask;         /* sigmask to restore */
	unsigned long sigc_sp;   /* stack pointer */
	unsigned long sigc_pc;   /* program counter */
	unsigned long sigc_npc;  /* next program counter */
	unsigned long sigc_psr;  /* for condition codes etc */
	unsigned long sigc_g1;   /* User uses these two registers */
	unsigned long sigc_o0;   /* within the trampoline code. */

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
	struct     pt_regs si_regs;
	long	   si_mask;
} __siginfo_t;

typedef struct {
	unsigned   int si_float_regs [64];
	unsigned   long si_fsr;
	unsigned   long si_gsr;
	unsigned   long si_fprs;
} __siginfo_fpu_t;

/* This magic should be in g_upper[0] for all upper parts
   to be valid.  */
#define SIGINFO_EXTRA_V8PLUS_MAGIC	0x130e269
typedef struct {
	unsigned   int g_upper[8];
	unsigned   int o_upper[8];
} siginfo_extra_v8plus_t;

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_SIGCONTEXT_H) */
