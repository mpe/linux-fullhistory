/*
 *	drivers/pci/setup-res.c
 *
 * Extruded from code written by
 *      Dave Rusling (david.rusling@reo.mts.dec.com)
 *      David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *
 * Support routines for initializing a PCI subsystem.
 */

/* fixed for multiple pci buses, 1999 Andrea Arcangeli <andrea@suse.de> */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/cache.h>


#define DEBUG_CONFIG 0
#if DEBUG_CONFIG
# define DBGC(args)     printk args
#else
# define DBGC(args)
#endif


int __init
pci_claim_resource(struct pci_dev *dev, int resource)
{
        struct resource *res = &dev->resource[resource];
	struct resource *root = pci_find_parent_resource(dev, res);
	int err;

	err = -EINVAL;
	if (root != NULL) {
		err = request_resource(root, res);
		if (err) {
			printk(KERN_ERR "PCI: Address space collision on "
			       "region %d of device %s [%lx:%lx]\n",
			       resource, dev->name, res->start, res->end);
		}
	} else {
		printk(KERN_ERR "PCI: No parent found for region %d "
		       "of device %s\n", resource, dev->name);
	}

	return err;
}

/*
 * Given the PCI bus a device resides on, try to
 * find an acceptable resource allocation for a
 * specific device resource..
 */
static int pci_assign_bus_resource(const struct pci_bus *bus,
	struct pci_dev *dev,
	struct resource *res,
	unsigned long size,
	unsigned long min,
	unsigned int type_mask)
{
	int i;

	type_mask |= IORESOURCE_IO | IORESOURCE_MEM;
	for (i = 0 ; i < 4; i++) {
		struct resource *r = bus->resource[i];
		if (!r)
			continue;

		/* type_mask must match */
		if ((res->flags ^ r->flags) & type_mask)
			continue;

		/* We cannot allocate a non-prefetching resource from a pre-fetching area */
		if ((r->flags & IORESOURCE_PREFETCH) && !(res->flags & IORESOURCE_PREFETCH))
			continue;

		/* Ok, try it out.. */
		if (allocate_resource(r, res, size, min, -1, size, pcibios_align_resource, dev) < 0)
			continue;

		/* Update PCI config space.  */
		pcibios_update_resource(dev, r, res, i);
		return 0;
	}
	return -EBUSY;
}

int 
pci_assign_resource(struct pci_dev *dev, int i)
{
	const struct pci_bus *bus = dev->bus;
	struct resource *res = dev->resource + i;
	unsigned long size, min;

	size = res->end - res->start + 1;
	min = (res->flags & IORESOURCE_IO) ? PCIBIOS_MIN_IO : PCIBIOS_MIN_MEM;

	/* First, try exact prefetching match.. */
	if (pci_assign_bus_resource(bus, dev, res, size, min, IORESOURCE_PREFETCH) < 0) {
		/*
		 * That failed.
		 *
		 * But a prefetching area can handle a non-prefetching
		 * window (it will just not perform as well).
		 */
		if (!(res->flags & IORESOURCE_PREFETCH) || pci_assign_bus_resource(bus, dev, res, size, min, 0) < 0) {
			printk(KERN_ERR "PCI: Failed to allocate resource %d for %s\n", i, dev->name);
			return -EBUSY;
		}
	}

	DBGC(("  got res[%lx:%lx] for resource %d\n", res->start, res->end, i));

	return 0;
}

void
pdev_assign_unassigned_resources(struct pci_dev *dev)
{
	u32 reg;
	u16 cmd;
	int i;

	DBGC(("PCI assign unassigned: (%s)\n", dev->name));

	pci_read_config_word(dev, PCI_COMMAND, &cmd);

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = &dev->resource[i];

		if (res->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		else if (res->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;

		/* If it is already assigned or the resource does
		   not exist, there is nothing to do.  */
		if (res->parent != NULL || res->flags == 0)
			continue;

		pci_assign_resource(dev, i);
	}

	/* Special case, disable the ROM.  Several devices act funny
	   (ie. do not respond to memory space writes) when it is left
	   enabled.  A good example are QlogicISP adapters.  */

	if (dev->rom_base_reg) {
		pci_read_config_dword(dev, dev->rom_base_reg, &reg);
		reg &= ~PCI_ROM_ADDRESS_ENABLE;
		pci_write_config_dword(dev, dev->rom_base_reg, reg);
		dev->resource[PCI_ROM_RESOURCE].flags &= ~PCI_ROM_ADDRESS_ENABLE;
	}

	/* All of these (may) have I/O scattered all around and may not
	   use I/O base address registers at all.  So we just have to
	   always enable IO to these devices.  */
	if ((dev->class >> 8) == PCI_CLASS_NOT_DEFINED
	    || (dev->class >> 8) == PCI_CLASS_NOT_DEFINED_VGA
	    || (dev->class >> 8) == PCI_CLASS_STORAGE_IDE
	    || (dev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
		cmd |= PCI_COMMAND_IO;
	}

	/* ??? Always turn on bus mastering.  If the device doesn't support
	   it, the bit will go into the bucket. */
	cmd |= PCI_COMMAND_MASTER;

	/* Enable the appropriate bits in the PCI command register.  */
	pci_write_config_word(dev, PCI_COMMAND, cmd);

	DBGC(("  cmd reg 0x%x\n", cmd));

	/* If this is a PCI bridge, set the cache line correctly.  */
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE,
				      (L1_CACHE_BYTES / sizeof(u32)));
	}
}

void __init
pci_assign_unassigned_resources(void)
{
	struct pci_dev *dev;

	pci_for_each_dev(dev) {
		pdev_assign_unassigned_resources(dev);
	}
}
