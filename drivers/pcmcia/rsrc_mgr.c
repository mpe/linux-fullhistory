/*
 * rsrc_mgr.c -- Resource management routines and/or wrappers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include "cs_internal.h"


#ifdef CONFIG_PCMCIA_PROBE

static int adjust_irq(struct pcmcia_socket *s, adjust_t *adj)
{
	int irq;
	u32 mask;

	irq = adj->resource.irq.IRQ;
	if ((irq < 0) || (irq > 15))
		return CS_BAD_IRQ;

	if (adj->Action != REMOVE_MANAGED_RESOURCE)
		return 0;

	mask = 1 << irq;

	if (!(s->irq_mask & mask))
		return 0;

	s->irq_mask &= ~mask;

	return 0;
}

#else

static inline int adjust_irq(struct pcmcia_socket *s, adjust_t *adj) {
	return CS_SUCCESS;
}

#endif


int pcmcia_adjust_resource_info(adjust_t *adj)
{
	struct pcmcia_socket *s;
	int ret = CS_UNSUPPORTED_FUNCTION;

	down_read(&pcmcia_socket_list_rwsem);
	list_for_each_entry(s, &pcmcia_socket_list, socket_list) {

		if (adj->Resource == RES_IRQ)
			ret = adjust_irq(s, adj);

		else if (s->resource_ops->adjust_resource)
			ret = s->resource_ops->adjust_resource(s, adj);
	}
	up_read(&pcmcia_socket_list_rwsem);

	return (ret);
}
EXPORT_SYMBOL(pcmcia_adjust_resource_info);

void pcmcia_validate_mem(struct pcmcia_socket *s)
{
	if (s->resource_ops->validate_mem)
		s->resource_ops->validate_mem(s);
}
EXPORT_SYMBOL(pcmcia_validate_mem);

int adjust_io_region(struct resource *res, unsigned long r_start,
		     unsigned long r_end, struct pcmcia_socket *s)
{
	if (s->resource_ops->adjust_io_region)
		return s->resource_ops->adjust_io_region(res, r_start, r_end, s);
	return -ENOMEM;
}

struct resource *find_io_region(unsigned long base, int num,
		   unsigned long align, struct pcmcia_socket *s)
{
	if (s->resource_ops->find_io)
		return s->resource_ops->find_io(base, num, align, s);
	return NULL;
}

struct resource *find_mem_region(u_long base, u_long num, u_long align,
				 int low, struct pcmcia_socket *s)
{
	if (s->resource_ops->find_mem)
		return s->resource_ops->find_mem(base, num, align, low, s);
	return NULL;
}

void release_resource_db(struct pcmcia_socket *s)
{
	if (s->resource_ops->exit)
		s->resource_ops->exit(s);
}


struct pccard_resource_ops pccard_static_ops = {
	.validate_mem = NULL,
	.adjust_io_region = NULL,
	.find_io = NULL,
	.find_mem = NULL,
	.adjust_resource = NULL,
	.exit = NULL,
};
EXPORT_SYMBOL(pccard_static_ops);
