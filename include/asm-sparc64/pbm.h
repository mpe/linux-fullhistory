/* $Id: pbm.h,v 1.14 1998/05/29 06:00:40 ecd Exp $
 * pbm.h: U2P PCI bus module pseudo driver software state.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_PBM_H
#define __SPARC64_PBM_H

#include <linux/pci.h>

#include <asm/psycho.h>
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

struct linux_psycho;

struct linux_pbm_info {
	struct linux_psycho		*parent;
	struct pci_vma			*IO_assignments;
	struct pci_vma			*MEM_assignments;
	int				prom_node;
	char				prom_name[64];
	struct linux_prom_pci_ranges	pbm_ranges[PROMREG_MAX];
	int				num_pbm_ranges;
	struct linux_prom_pci_intmap	pbm_intmap[PROMREG_MAX];
	int				num_pbm_intmap;
	struct linux_prom_pci_intmask	pbm_intmask;

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
	int				index;
	struct linux_pbm_info		pbm_A;
	struct linux_pbm_info		pbm_B;

	/* Now things for the actual PCI bus probes. */
	unsigned int			pci_first_busno;
	unsigned int			pci_last_busno;
	struct pci_bus			*pci_bus;
};

/* PCI devices which are not bridges have this placed in their pci_dev
 * sysdata member.  This makes OBP aware PCI device drivers easier to
 * code.
 */
struct pcidev_cookie {
	struct linux_pbm_info		*pbm;
	int				prom_node;
};


#define PCI_IRQ_IGN	0x000007c0	/* PSYCHO "Int Group Number". */
#define PCI_IRQ_INO	0x0000003f	/* PSYCHO INO.                */

/* Used by EBus */
extern unsigned int psycho_irq_build(struct linux_pbm_info *pbm,
				     struct pci_dev *pdev,
				     unsigned int full_ino);

#endif /* !(__SPARC64_PBM_H) */
