/* $Id: sigcontext.h,v 1.8 1996/03/01 07:20:59 davem Exp $ */
#ifndef _ASMsparc_SIGCONTEXT_H
#define _ASMsparc_SIGCONTEXT_H

#include <asm/ptrace.h>

#define SUNOS_MAXWIN   31

#ifndef __ASSEMBLY__

/* SunOS system call sigstack() uses this arg. */
struct sunos_sigstack {
	unsigned long sig_sp;
	int onstack_flag;
};

/* This is what SunOS does, so shall I. */
struct sigcontext_struct {
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
#endif /* !(__ASSEMBLY__) */

#endif /* !(_ASMsparc_SIGCONTEXT_H) */

