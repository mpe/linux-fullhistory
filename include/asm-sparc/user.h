/* $Id: user.h,v 1.3 1995/12/29 21:48:03 davem Exp $
 * asm-sparc/user.h: Core file definitions for the Sparc.
 *
 * Copyright (C) 1995 (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_USER_H
#define _SPARC_USER_H

struct sunos_regs {
	unsigned long psr, pc, npc, y;
	unsigned long regs[15];
};

struct sunos_fpqueue {
	unsigned long *addr;
	unsigned long inst;
};

struct sunos_fp {
	union {
		unsigned long regs[32];
		double reg_dbls[16];
	} fregs;
	unsigned long fsr;
	unsigned long flags;
	unsigned long extra;
	unsigned long fpq_count;
	struct sunos_fpqueue fpq[16];
};

struct sunos_fpu {
	struct sunos_fp fpstatus;
};

/* The SunOS core file header layout. */
struct user {
	unsigned long magic;
	unsigned long len;
	struct sunos_regs regs;
	struct exec uexec;
	int           signal;
	size_t        u_tsize; /* all of these in bytes! */
	size_t        u_dsize;
	size_t        u_ssize;
	char          u_comm[17];
	struct sunos_fpu fpu;
	unsigned long sigcode;   /* Special sigcontext subcode, if any */
};

#define NBPG                   PAGE_SIZE
#define UPAGES                 1
#define HOST_TEXT_START_ADDR   (u.start_code)
#define HOST_DATA_START_ADDR   (u.start_data)
#define HOST_STACK_END_ADDR    (u.start_stack + u.u_ssize * NBPG)
#define SUNOS_CORE_MAGIC       0x080456

#endif /* !(_SPARC_USER_H) */
