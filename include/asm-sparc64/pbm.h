/* $Id: pbm.h,v 1.4 1997/08/15 06:44:52 davem Exp $
 * pbm.h: U2P PCI bus module pseudo driver software state.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_PBM_H
#define __SPARC64_PBM_H

#include <linux/bios32.h>
#include <linux/pci.h>

#include <asm/psycho.h>
#include <asm/oplib.h>

struct linux_psycho;

struct linux_pbm_info {
	struct linux_psycho		*parent;
	unsigned long			*pbm_IO;
	unsigned long			*pbm_mem;
	int				prom_node;
	char				prom_name[64];
	struct linux_prom_pci_ranges	pbm_ranges[PROMREG_MAX];
	int				num_pbm_ranges;

	/* Now things for the actual PCI bus probes. */
	unsigned int			pci_first_busno;
	unsigned int			pci_last_busno;
	struct pci_bus			pci_bus;
};

struct linux_psycho {
	struct linux_psycho		*next;
	struct psycho_regs		*psycho_regs;
	unsigned long			*pci_config_space;
	unsigned long			*pci_IO_space;
	unsigned long			*pci_mem_space;
	u32				upa_portid;
	struct linux_pbm_info		pbm_A;
	struct linux_pbm_info		pbm_B;
};

extern struct linux_psycho *psycho_root;

#endif /* !(__SPARC64_PBM_H) */
