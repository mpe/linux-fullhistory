/*
 * linux/include/asm-arm/procinfo.h
 *
 * Copyright (C) 1996 Russell King
 */

#ifndef __ASM_PROCINFO_H
#define __ASM_PROCINFO_H

#include <asm/proc-fns.h>

#define F_MEMC   (1<<0)
#define F_MMU    (1<<1)
#define F_32BIT  (1<<2)
#define F_CACHE  (1<<3)
#define F_IOEB   (1<<31)

#ifndef __ASSEMBLER__

struct armversions {
	unsigned long id;		/* Processor ID			*/
	unsigned long mask;		/* Processor ID mask		*/
	unsigned long features;		/* Features (see above)		*/
	const char *manu;		/* Manufacturer			*/
	const char *name;		/* Processor name		*/
	const struct processor *proc;	/* Processor-specific ASM	*/
	const char *optname;		/* Optimisation name		*/
};

extern struct armversions armidlist[];
extern int armidindex;

#endif

#endif

