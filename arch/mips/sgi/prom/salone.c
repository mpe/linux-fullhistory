/* $Id: salone.c,v 1.1.1.1 1997/06/01 03:16:40 ralf Exp $
 * salone.c: Routines to load into memory and execute stand-along
 *           program images using ARCS PROM firmware.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */

#include <asm/sgialib.h>

long prom_load(char *name, unsigned long end, unsigned long *pc, unsigned long *eaddr)
{
	return romvec->load(name, end, pc, eaddr);
}

long prom_invoke(unsigned long pc, unsigned long sp, long argc,
		 char **argv, char **envp)
{
	return romvec->invoke(pc, sp, argc, argv, envp);
}

long prom_exec(char *name, long argc, char **argv, char **envp)
{
	return romvec->exec(name, argc, argv, envp);
}
