/* $Id: sun4c_vac.c,v 1.5 1995/11/25 00:59:43 davem Exp $
 * vac.c:   Routines for flushing various amount of the Sparc VAC
 *          (virtual address cache) on the sun4c.
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>

#include <asm/asi.h>
#include <asm/contregs.h>
#include <asm/vac-ops.h>
#include <asm/page.h>

struct sun4c_vac_props sun4c_vacinfo;

/* Invalidate the entire sun4c VAC, it must be off at this point */
void
sun4c_flush_all(void)
{
	unsigned long begin, end;

	if(sun4c_vacinfo.on)
		panic("SUN4C: AIEEE, trying to invalidate vac while"
                      " it is on.");

	/* Clear 'valid' bit in all cache line tags */
	begin = AC_CACHETAGS;
	end = (AC_CACHETAGS + sun4c_vacinfo.num_bytes);
	while(begin < end) {
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
				     "r" (begin), "i" (ASI_CONTROL));
		begin += sun4c_vacinfo.linesize;
	}
	return;
}

