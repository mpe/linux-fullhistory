/* $Id: segment.c,v 1.4 1997/05/01 01:41:33 davem Exp $
 * segment.c:  Prom routine to map segments in other contexts before
 *             a standalone is completely mapped.  This is for sun4 and
 *             sun4c architectures only.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/openprom.h>
#include <asm/oplib.h>

/* Set physical segment 'segment' at virtual address 'vaddr' in
 * context 'ctx'.
 */
void
prom_putsegment(int ctx, unsigned long vaddr, int segment)
{
	unsigned long flags;
	save_flags(flags); cli();
	(*(romvec->pv_setctxt))(ctx, (char *) vaddr, segment);
	__asm__ __volatile__("ld [%0], %%g6\n\t" : :
			     "r" (&current_set[hard_smp_processor_id()]) :
			     "memory");
	restore_flags(flags);
	return;
}
