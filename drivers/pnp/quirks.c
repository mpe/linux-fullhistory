/*
 *  This file contains quirk handling code for ISAPnP devices
 *  Some devices do not report all their resources, and need to have extra
 *  resources added. This is most easily accomplished at initialisation time
 *  when building up the resource structure for the first time.
 *
 *  Copyright (c) 2000 Peter Denison <peterd@pnd-pc.demon.co.uk>
 *
 *  Heavily based on PCI quirks handling which is
 *
 *  Copyright (c) 1999 Martin Mares <mj@suse.cz>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/isapnp.h>
#include <linux/string.h>


static void __init quirk_awe32_resources(struct pci_dev *dev)
{
	struct isapnp_port *port, *port2, *port3;
	struct isapnp_resources *res = dev->sysdata;

	/*
	 * Unfortunately the isapnp_add_port_resource is too tightly bound
	 * into the PnP discovery sequence, and cannot be used. Link in the
	 * two extra ports (at offset 0x400 and 0x800 from the one given) by
	 * hand.
	 */
	for ( ; res ; res = res->alt ) {
		port2 = isapnp_alloc(sizeof(struct isapnp_port));
		port3 = isapnp_alloc(sizeof(struct isapnp_port));
		if (!port2 || !port3)
			return;
		port = res->port;
		memcpy(port2, port, sizeof(struct isapnp_port));
		memcpy(port3, port, sizeof(struct isapnp_port));
		port->next = port2;
		port2->next = port3;
		port2->min += 0x400;
		port2->max += 0x400;
		port3->min += 0x800;
		port3->max += 0x800;
	}
	printk(KERN_INFO "ISAPnP: AWE32 quirk - adding two ports\n");
}


/*
 *  ISAPnP Quirks
 *  Cards or devices that need some tweaking due to broken hardware
 */

static struct isapnp_fixup isapnp_fixups[] __initdata = {
	{ ISAPNP_VENDOR('C','T','L'), ISAPNP_DEVICE(0x0021),
		quirk_awe32_resources },
	{ ISAPNP_VENDOR('C','T','L'), ISAPNP_DEVICE(0x0022),
		quirk_awe32_resources },
	{ ISAPNP_VENDOR('C','T','L'), ISAPNP_DEVICE(0x0023),
		quirk_awe32_resources },
	{ 0 }
};

void isapnp_fixup_device(struct pci_dev *dev)
{
	int i = 0;

	while (isapnp_fixups[i].vendor != 0) {
		if ((isapnp_fixups[i].vendor == dev->vendor) &&
		    (isapnp_fixups[i].device == dev->device)) {
			printk(KERN_DEBUG "PnP: Calling quirk for %02x:%02x\n",
			       dev->bus->number, dev->devfn);
			isapnp_fixups[i].quirk_function(dev);
		}
		i++;
	}
}

