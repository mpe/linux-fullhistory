/*
 * linux/include/asm-arm/procinfo.h
 *
 * Copyright (C) 1996 Russell King
 */

#ifndef __ASM_PROCINFO_H
#define __ASM_PROCINFO_H

#include <asm/proc-fns.h>

#ifndef __ASSEMBLER__

#define HWCAP_SWP	(1 << 0)
#define HWCAP_HALF	(1 << 1)

struct armversions {
	const unsigned long id;		/* Processor ID			*/
	const unsigned long mask;	/* Processor ID mask		*/
	const char *manu;		/* Manufacturer			*/
	const char *name;		/* Processor name		*/
	const char *arch_vsn;		/* Architecture version		*/
	const char *elf_vsn;		/* ELF library version		*/
	const int hwcap;		/* ELF HWCAP			*/
	const struct processor *proc;	/* Processor-specific ASM	*/
};

extern const struct armversions armidlist[];
extern int armidindex;

#endif

#endif

