/* $Id: bootstr.c,v 1.1 1996/12/27 08:49:10 jj Exp $
 * bootstr.c:  Boot string/argument acquisition from the PROM.
 *
 * Copyright(C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright(C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
#include <linux/string.h>
#include <asm/oplib.h>

#define BARG_LEN  256
static char barg_buf[BARG_LEN];
static char fetched = 0;

char *
prom_getbootargs(void)
{
	/* This check saves us from a panic when bootfd patches args. */
	if (fetched) return barg_buf;
	prom_getstring(prom_finddevice ("/chosen"), "bootargs", barg_buf, BARG_LEN);
	fetched = 1;
	return barg_buf;
}
