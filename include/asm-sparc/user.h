/* $Id: user.h,v 1.2 1995/11/25 02:33:15 davem Exp $
 * asm-sparc/user.h: Core file definitions for the Sparc.
 *
 * Copyright (C) 1995 (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_USER_H
#define _SPARC_USER_H

struct user {
	unsigned long regs[24 + 32]; /* locals, ins, globals + fpu regs */
	size_t        u_tsize;
	size_t        u_dsize;
	size_t        u_ssize;
	unsigned long start_code;
	unsigned long start_data;
	unsigned long start_stack;
	int           signal;
	unsigned long *u_ar0;
	unsigned long magic;
	char          u_comm[32];
};

#define NBPG                   PAGE_SIZE
#define UPAGES                 1
#define HOST_TEXT_START_ADDR   (u.start_code)
#define HOST_DATA_START_ADDR   (u.start_data)
#define HOST_STACK_END_ADDR    (u.start_stack + u.u_ssize * NBPG)

#endif /* !(_SPARC_USER_H) */
