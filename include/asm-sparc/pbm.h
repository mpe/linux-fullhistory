/* $Id: pbm.h,v 1.1 1998/09/22 05:54:44 jj Exp $
 * pbm.h: PCI bus module pseudo driver software state
 *        Adopted from sparc64 by V. Roganov and G. Raiko
 *
 * Original header:
 * pbm.h: U2P PCI bus module pseudo driver software state.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC_PBM_H
#define __SPARC_PBM_H

#include <linux/pci.h>
#include <asm/oplib.h>

struct linux_pbm_info;

/* This is what we use to determine what the PROM has assigned so
 * far, so that we can perform assignments for addresses which
 * were not taken care of by OBP.  See psycho.c for details.
 * Per-PBM these are ordered by start address.
 */
struct pci_vma {
	struct pci_vma			*next;
	struct linux_pbm_info		*pbm;
	unsigned int			start;
	unsigned int			end;
	unsigned int			offset;
	unsigned int			_pad;
};

struct linux_pbm_info {
	struct pci_vma			*IO_assignments;
	struct pci_vma			*MEM_assignments;
	int				prom_node;
	char				prom_name[64];
	struct linux_prom_pci_ranges	pbm_ranges[PROMREG_MAX];
	int				num_pbm_ranges;

	/* Now things for the actual PCI bus probes. */
	unsigned int			pci_first_busno;
	unsigned int			pci_last_busno;
	struct pci_bus			pci_bus;
};

/* PCI devices which are not bridges have this placed in their pci_dev
 * sysdata member.  This makes OBP aware PCI device drivers easier to
 * code.
 */
struct pcidev_cookie {
	struct linux_pbm_info		*pbm;
	int				prom_node;
};

#endif /* !(__SPARC_PBM_H) */
