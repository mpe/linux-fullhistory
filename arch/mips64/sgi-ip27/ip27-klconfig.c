/* $Id: ip27-klconfig.c,v 1.1 2000/01/17 23:32:47 ralf Exp $
 *
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/param.h>
#include <linux/timex.h>
#include <linux/mm.h>		

#include <asm/sn/klconfig.h>

lboard_t *find_lboard(unsigned int type)
{
	lboard_t *b;

	for (
b = KL_CONFIG_INFO(get_nasid());
b;
b = KLCF_NEXT(b)) {
		if (KLCF_REMOTE(b))
			continue;		/* Skip remote boards. */

		if (b->brd_type == type)
			return (lboard_t *) b;
	}

	return NULL;
}
