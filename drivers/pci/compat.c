/*
 *	$Id: compat.c,v 1.1 1998/02/16 10:35:50 mj Exp $
 *
 *	PCI Bus Services -- Function For Backward Compatibility
 *
 *	Copyright 1998 Martin Mares
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>

int
pcibios_find_class(unsigned int class, unsigned short index, unsigned char *bus, unsigned char *devfn)
{
	struct pci_dev *dev = NULL;
	int cnt = 0;

	while ((dev = pci_find_class(class, dev)))
		if (index == cnt++) {
			*bus = dev->bus->number;
			*devfn = dev->devfn;
			return PCIBIOS_SUCCESSFUL;
		}
	return PCIBIOS_DEVICE_NOT_FOUND;
}


int
pcibios_find_device(unsigned short vendor, unsigned short device, unsigned short index,
		    unsigned char *bus, unsigned char *devfn)
{
	struct pci_dev *dev = NULL;
	int cnt = 0;

	while ((dev = pci_find_device(vendor, device, dev)))
		if (index == cnt++) {
			*bus = dev->bus->number;
			*devfn = dev->devfn;
			return PCIBIOS_SUCCESSFUL;
		}
	return PCIBIOS_DEVICE_NOT_FOUND;
}
