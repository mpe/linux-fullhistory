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

extern struct pci_controller* pci_alloc_pci_controller(enum phb_types controller_type);
extern struct pci_controller* pci_find_hose_for_OF_device(struct device_node* node);

extern struct pci_controller* hose_head;
extern struct pci_controller** hose_tail;
/* PHB's are also in a table. */
#define PCI_MAX_PHB 64
extern int  global_phb_number;
extern struct pci_controller *phbtab[];

/*******************************************************************
 * Platform functions that are brand specific implementation. 
 *******************************************************************/
extern unsigned long find_and_init_phbs(void);

extern void   ppc64_pcibios_init(void);

extern struct pci_dev *ppc64_isabridge_dev;	/* may be NULL if no ISA bus */

/*******************************************************************
 * PCI device_node operations
 *******************************************************************/
struct device_node;
typedef void *(*traverse_func)(struct device_node *me, void *data);
void *traverse_pci_devices(struct device_node *start, traverse_func pre, traverse_func post, void *data);
void *traverse_all_pci_devices(traverse_func pre);

struct pci_dev *pci_find_dev_by_addr(unsigned long addr);
void pci_devs_phb_init(void);
void pci_fix_bus_sysdata(void);
struct device_node *fetch_dev_dn(struct pci_dev *dev);

void iSeries_pcibios_init_early(void);
void pSeries_pcibios_init_early(void);
void pSeries_pcibios_init(void);

/*******************************************************************
 * Helper macros for extracting data from pci structures.  
 *   PCI_GET_PHB_PTR(struct pci_dev*)    returns the Phb pointer.
 *   PCI_GET_PHB_NUMBER(struct pci_dev*) returns the Phb number.
 *   PCI_GET_BUS_NUMBER(struct pci_dev*) returns the bus number.
 *******************************************************************/
#define PCI_GET_PHB_PTR(dev)    (((struct device_node *)(dev)->sysdata)->phb)
#define PCI_GET_PHB_NUMBER(dev) (((dev)->bus->number&0x00FFFF00)>>8)
#define PCI_GET_BUS_NUMBER(dev) ((dev)->bus->number&0x0000FF)

/*******************************************************************
 * Debugging  Routines.
 *******************************************************************/
extern void dumpResources(struct resource* Resource);
extern void dumpPci_Controller(struct pci_controller* phb);
extern void dumpPci_Bus(struct pci_bus* Pci_Bus);
extern void dumpPci_Dev(struct pci_dev* Pci_Dev);

#endif /* __PPC_KERNEL_PCI_H__ */
