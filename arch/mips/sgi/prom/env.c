/*
 * env.c: ARCS environment variable routines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: env.c,v 1.2 1998/03/27 08:53:46 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/sgialib.h>

__initfunc(char *prom_getenv(char *name))
{
	return romvec->get_evar(name);
}

__initfunc(long prom_setenv(char *name, char *value))
{
	return romvec->set_evar(name, value);
}
