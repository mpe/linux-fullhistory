/* $Id: bootstr.c,v 1.3 1997/03/04 16:27:06 jj Exp $
 * bootstr.c:  Boot string/argument acquisition from the PROM.
 *
 * Copyright(C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright(C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

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
	prom_getstring(prom_chosen_node, "bootargs", barg_buf, BARG_LEN);
	fetched = 1;
	return barg_buf;
}
