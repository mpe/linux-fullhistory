/* $Id: ebus.h,v 1.2 1997/08/17 22:40:07 ecd Exp $
 * ebus.h: PCI to Ebus pseudo driver software state.
 *
 * Copyright (C) 1997 Eddie C. Dost (ecd@skynet.be)
 */

#ifndef __SPARC64_EBUS_H
#define __SPARC64_EBUS_H

#include <asm/oplib.h>

struct linux_ebus_device {
	struct linux_ebus_device	*next;
	struct linux_ebus		*parent;
	int				 prom_node;
	char				 prom_name[64];
	unsigned long			 base_address[PROMREG_MAX];
	int				 num_addrs;
	unsigned int			 irqs[PROMINTR_MAX];
	int				 num_irqs;
};
	
struct linux_ebus {
	struct linux_ebus		*next;
	struct linux_ebus_device	*devices;
	struct linux_pbm_info		*parent;
	struct pci_dev			*self;
	int				 prom_node;
	char				 prom_name[64];
	struct linux_prom_ebus_ranges	 ebus_ranges[PROMREG_MAX];
	int				 num_ebus_ranges;
};

extern struct linux_ebus		*ebus_chain;

extern unsigned long ebus_init(unsigned long, unsigned long);

#define for_each_ebus(bus)						\
        for((bus) = ebus_chain; (bus); (bus) = (bus)->next)

#define for_each_ebusdev(dev, bus)					\
        for((dev) = (bus)->devices; (dev); (dev) = (dev)->next)

#define for_all_ebusdev(dev, bus)					\
	for ((bus) = ebus_chain, ((dev) = (bus) ? (bus)->devices : 0);	\
	     (bus); ((dev) = (dev)->next ? (dev)->next :		\
	     ((bus) = (bus)->next, (bus) ? (bus)->devices : 0)))

#endif /* !(__SPARC64_EBUS_H) */
