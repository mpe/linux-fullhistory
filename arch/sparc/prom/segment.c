/* $Id: segment.c,v 1.6 1998/03/09 14:04:27 jj Exp $
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

extern void restore_current(void);

/* Set physical segment 'segment' at virtual address 'vaddr' in
 * context 'ctx'.
 */
void
prom_putsegment(int ctx, unsigned long vaddr, int segment)
{
	unsigned long flags;
	save_flags(flags); cli();
	(*(romvec->pv_setctxt))(ctx, (char *) vaddr, segment);
	restore_current();
	restore_flags(flags);
	return;
}
