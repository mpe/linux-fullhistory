/*
 *	$Id: oldproc.c,v 1.24 1998/10/11 15:13:04 mj Exp $
 *
 *	Backward-compatible procfs interface for PCI.
 *
 *	Copyright 1993, 1994, 1995, 1997 Drew Eckhardt, Frederic Potter,
 *	David Mosberger-Tang, Martin Mares
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

struct pci_device_info {
	unsigned short device;
	unsigned short seen;
	const char *name;
};

struct pci_vendor_info {
	unsigned short vendor;
	unsigned short nr;
	const char *name;
	struct pci_device_info *devices;
};

/*
 * This is ridiculous, but we want the strings in
 * the .init section so that they don't take up
 * real memory.. Parse the same file multiple times
 * to get all the info.
 */
#define VENDOR( vendor, name )		static const char __vendorstr_##vendor[] __initdata = name;
#define ENDVENDOR()
#define DEVICE( vendor, device, name ) 	static const char __devicestr_##vendor##device[] __initdata = name;
#include "devlist.h"


#define VENDOR( vendor, name )		static struct pci_device_info __devices_##vendor[] __initdata = {
#define ENDVENDOR()			};
#define DEVICE( vendor, device, name )	{ 0x##device, 0, __devicestr_##vendor##device },
#include "devlist.h"

static const struct pci_vendor_info __initdata pci_vendor_list[] = {
#define VENDOR( vendor, name )		{ 0x##vendor, sizeof(__devices_##vendor) / sizeof(struct pci_device_info), __vendorstr_##vendor, __devices_##vendor },
#define ENDVENDOR()
#define DEVICE( vendor, device, name )
#include "devlist.h"
};

#define VENDORS (sizeof(pci_vendor_list)/sizeof(struct pci_vendor_info))

void __init pci_name_device(struct pci_dev *dev)
{
	const struct pci_vendor_info *vendor_p = pci_vendor_list;
	int i = VENDORS;
	char *name = dev->name;

	do {
		if (vendor_p->vendor == dev->vendor)
			goto match_vendor;
		vendor_p++;
	} while (--i);

	/* Couldn't find either the vendor nor the device */
	sprintf(name, "PCI device %04x:%04x", dev->vendor, dev->device);
	return;

	match_vendor: {
		struct pci_device_info *device_p = vendor_p->devices;
		int i = vendor_p->nr;

		while (i > 0) {
			if (device_p->device == dev->device)
				goto match_device;
			device_p++;
			i--;
		}

		/* Ok, found the vendor, but unknown device */
		sprintf(name, "PCI device %04x:%04x (%s)", dev->vendor, dev->device, vendor_p->name);
		return;

		/* Full match */
		match_device: {
			char *n = name + sprintf(name, "%s %s", vendor_p->name, device_p->name);
			int nr = device_p->seen + 1;
			device_p->seen = nr;
			if (nr > 1)
				sprintf(n, " (#%d)", nr);
		}
	}
}
