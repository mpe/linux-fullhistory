/* $Id: pbm.h,v 1.13 1998/04/20 07:15:11 ecd Exp $
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

extern struct linux_psycho *psycho_root;
extern struct linux_psycho **psycho_index_map;
extern int linux_num_psycho;

static __inline__ struct linux_psycho *
psycho_by_index(int index)
{
	if (index >= linux_num_psycho)
		return NULL;
	return psycho_index_map[index];
}

/* Special PCI IRQ encoding, this just makes life easier for the generic
 * irq registry layer, there is already enough crap in there due to sbus,
 * fhc, and dcookies.
 */
#define PCI_IRQ_IDENT		0x80000000	/* This tells irq.c what we are       */
#define PCI_IRQ_IMAP_OFF	0x7ff00000	/* Offset from first PSYCHO imap      */
#define PCI_IRQ_IMAP_OFF_SHFT	20
#define PCI_IRQ_BUSNO		0x000fc000	/* PSYCHO instance                    */
#define PCI_IRQ_BUSNO_SHFT	14
#define PCI_IRQ_DMA_SYNC	0x00001000	/* IRQ needs DMA sync for APB	      */
#define PCI_IRQ_IGN		0x000007c0	/* PSYCHO "Int Group Number"          */
#define PCI_IRQ_INO		0x0000003f	/* PSYCHO INO                         */

#define PCI_IRQ_P(__irq)	(((__irq) & PCI_IRQ_IDENT) != 0)

extern __inline__ unsigned int pci_irq_encode(unsigned long imap_off,
					      unsigned long psycho_instance,
					      unsigned long ign,
					      unsigned long ino, int dma_sync)
{
	unsigned int irq;

	irq  = PCI_IRQ_IDENT;
	irq |= ((imap_off << PCI_IRQ_IMAP_OFF_SHFT) & PCI_IRQ_IMAP_OFF);
	irq |= ((psycho_instance << PCI_IRQ_BUSNO_SHFT) & PCI_IRQ_BUSNO);
	irq |= ((ign << 6) & PCI_IRQ_IGN);
	irq |= (ino & PCI_IRQ_INO);
	irq |= dma_sync ? PCI_IRQ_DMA_SYNC : 0;

	return irq;
}

/* Used by EBus */
extern unsigned int psycho_irq_build(struct linux_pbm_info *pbm,
				     struct pci_dev *pdev,
				     unsigned int full_ino);

#endif /* !(__SPARC64_PBM_H) */
