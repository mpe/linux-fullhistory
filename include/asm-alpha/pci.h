#ifndef __ALPHA_PCI_H
#define __ALPHA_PCI_H

#include <linux/config.h>
#include <linux/pci.h>


/*
 * The following structure is used to manage multiple PCI busses.
 *
 * XXX: We should solve this problem in an architecture independent
 * way, rather than hacking something up here.
 */

struct linux_hose_info {
        struct pci_bus                  pci_bus;
	struct linux_hose_info         *next;
	unsigned long                   pci_io_space;
	unsigned long                   pci_mem_space;
	unsigned long                   pci_config_space;
	unsigned long                   pci_sparse_space;
        unsigned int                    pci_first_busno;
        unsigned int                    pci_last_busno;
	unsigned int                    pci_hose_index;
};

/* This is indexed by a pseudo- PCI bus number to obtain the real deal.  */
extern struct linux_hose_info *bus2hose[256];

/* Create a handle that is OR-ed into the reported I/O space address
   for a device.  We use this later to find the bus a device lives on.  */

#if defined(CONFIG_ALPHA_GENERIC) \
    || defined(CONFIG_ALPHA_MCPCIA) \
    || defined(CONFIG_ALPHA_TSUNAMI)

#define PCI_HANDLE(bus)   ((bus2hose[bus]->pci_hose_index & 3UL) << 32)
#define DEV_IS_ON_PRIMARY(dev) \
  (bus2hose[(dev)->bus->number]->pci_first_busno == (dev)->bus->number)

#else

#define PCI_HANDLE(bus)         0
#define DEV_IS_ON_PRIMARY(dev)  ((dev)->bus->number == 0)

#endif /* Multiple busses */

#endif /* __ALPHA_PCI_H */
