#ifndef _ASM_PCI_BRIDGE_H
#define _ASM_PCI_BRIDGE_H

unsigned long pmac_find_bridges(unsigned long, unsigned long);

/*
 * pci_io_base returns the memory address at which you can access
 * the I/O space for PCI bus number `bus' (or NULL on error).
 */
void *pci_io_base(unsigned int bus);

/*
 * pci_device_loc returns the bus number and device/function number
 * for a device on a PCI bus, given its device_node struct.
 * It returns 0 if OK, -1 on error.
 */
int pci_device_loc(struct device_node *dev, unsigned char *bus_ptr,
		   unsigned char *devfn_ptr);

struct bridge_data {
	volatile unsigned int *cfg_addr;
	volatile unsigned char *cfg_data;
	void *io_base;
	int bus_number;
	int max_bus;
	struct bridge_data *next;
	struct device_node *node;
};

#endif
