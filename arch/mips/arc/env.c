/*
 * env.c: ARCS environment variable routines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: env.c,v 1.2 1999/10/09 00:00:57 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/sgialib.h>

char * __init prom_getenv(char *name)
{
	return romvec->get_evar(name);
}

long __init prom_setenv(char *name, char *value)
{
	return romvec->set_evar(name, value);
}
