/* $Id: env.c,v 1.1 1997/06/06 09:36:59 ralf Exp $
 * env.c: ARCS environment variable routines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */

#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/sgialib.h>

char *prom_getenv(char *name)
{
	return romvec->get_evar(name);
}

long prom_setenv(char *name, char *value)
{
	return romvec->set_evar(name, value);
}
