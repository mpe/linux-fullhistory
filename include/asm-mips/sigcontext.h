/*
 * include/asm-mips/sigcontext.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997 by Ralf Baechle
 *
 * $Id: sigcontext.h,v 1.5 1997/12/14 18:57:19 ralf Exp $
 */
#ifndef __ASM_MIPS_SIGCONTEXT_H
#define __ASM_MIPS_SIGCONTEXT_H

/*
 * Keep this struct definition in sync with the sigcontext fragment
 * in arch/mips/tools/offset.c
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

	unsigned long      sc_sigset[4];	/* kernel's sigset_t */
};

#endif /* __ASM_MIPS_SIGCONTEXT_H */
