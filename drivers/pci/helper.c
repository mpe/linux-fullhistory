/*
 * $Id$
 *
 * drivers/pci/helper.c
 *
 * Copyright 1999 Jeff Garzik <jgarzik@mandrakesoft.com>
 * This software is free.  See the file COPYING for licensing details.
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>


int pci_simple_probe (const struct pci_simple_probe_entry *list,
		      size_t match_limit, pci_simple_probe_callback cb,
		      void *drvr_data)
{
	struct pci_dev *dev;
	const struct pci_simple_probe_entry *ent;
	size_t matches = 0;
	unsigned short vendor, device;
	int rc;

	if (!list || !cb)
		return -1;

	dev = pci_find_device (PCI_ANY_ID, PCI_ANY_ID, NULL);
	while (dev) {
		ent = list;
		while (ent->vendor && ent->device) {
			vendor = ent->vendor;
			device = ent->device;

			if (((vendor != 0xFFFF) &&
			     (vendor != dev->vendor)) ||
			    ((device != 0xFFFF) &&
			     (device != dev->device))) {
			     	ent++;
				continue;
			}
			    
			if (((ent->subsys_vendor) &&
			     (ent->subsys_vendor != dev->subsystem_vendor)) ||
			    ((ent->subsys_device) &&
			     (ent->subsys_device != dev->subsystem_device))) {
			     	ent++;
				continue;
			}
			    
			rc = (* cb) (dev, matches, ent, drvr_data);
			if (rc < 0)
				return rc;

			matches++;

			if (match_limit && match_limit == matches)
				return matches;

			break; /* stop list search on first match */
		}

		dev = pci_find_device (PCI_ANY_ID, PCI_ANY_ID, dev);
	}

	return matches;
}


