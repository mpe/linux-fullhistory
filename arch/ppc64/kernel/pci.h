/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#ifndef __PPC_KERNEL_PCI_H__
#define __PPC_KERNEL_PCI_H__

#include <linux/pci.h>
#include <asm/pci-bridge.h>

extern unsigned long isa_io_base;

extern void pci_setup_pci_controller(struct pci_controller *hose);
extern void pci_setup_phb_io(struct pci_controller *hose, int primary);
extern void pci_setup_phb_io_dynamic(struct pci_controller *hose, int primary);


extern struct list_head hose_list;
extern int global_phb_number;

extern unsigned long find_and_init_phbs(void);

extern struct pci_dev *ppc64_isabridge_dev;	/* may be NULL if no ISA bus */

/* PCI device_node operations */
struct device_node;
typedef void *(*traverse_func)(struct device_node *me, void *data);
void *traverse_pci_devices(struct device_node *start, traverse_func pre,
		void *data);

void pci_devs_phb_init(void);
void pci_devs_phb_init_dynamic(struct pci_controller *phb);
struct device_node *fetch_dev_dn(struct pci_dev *dev);

/* PCI address cache management routines */
void pci_addr_cache_insert_device(struct pci_dev *dev);
void pci_addr_cache_remove_device(struct pci_dev *dev);

/* From pSeries_pci.h */
void init_pci_config_tokens (void);
unsigned long get_phb_buid (struct device_node *);

extern unsigned long pci_probe_only;
extern unsigned long pci_assign_all_buses;
extern int pci_read_irq_line(struct pci_dev *pci_dev);

#endif /* __PPC_KERNEL_PCI_H__ */
