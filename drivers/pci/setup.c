/*
 *	drivers/pci/setup.c
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

#include <asm/cache.h>


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
	if (root != NULL)
		err = request_resource(root, res);
	if (err) {
		printk(KERN_ERR "PCI: Address space collision on region %d "
		       "of device %s\n", resource, dev->name);
	}

	return err;
}

static void
pdev_assign_unassigned_resources(struct pci_dev *dev, u32 min_io, u32 min_mem)
{
	u32 reg;
	u16 cmd;
	int i;

	DBGC(("PCI assign unassigned: (%s)\n", dev->name));

	pci_read_config_word(dev, PCI_COMMAND, &cmd);

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *root, *res;
		unsigned long size, min;

		res = &dev->resource[i];

		if (res->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		else if (res->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;

		/* If it is already assigned or the resource does
		   not exist, there is nothing to do.  */
		if (res->parent != NULL || res->flags == 0)
			continue;

		/* Determine the root we allocate from.  */
		root = pci_find_parent_resource(dev, res);
		if (root == NULL)
			continue;

		min = (res->flags & IORESOURCE_IO ? min_io : min_mem);
		min += root->start;
		size = res->end - res->start + 1;

		DBGC(("  for root[%lx:%lx] min[%lx] size[%lx]\n",
		      root->start, root->end, min, size));

		if (allocate_resource(root, res, size, min, -1, size, dev) < 0) {
			printk(KERN_ERR
			       "PCI: Failed to allocate resource %d for %s\n",
			       i, dev->name);
			continue;
		}

		DBGC(("  got res[%lx:%lx] for resource %d\n",
		      res->start, res->end, i));

		/* Update PCI config space.  */
		pcibios_update_resource(dev, root, res, i);
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
pci_assign_unassigned_resources(u32 min_io, u32 min_mem)
{
	struct pci_dev *dev;
	for (dev = pci_devices; dev; dev = dev->next)
		pdev_assign_unassigned_resources(dev, min_io, min_mem);
}

#define ROUND_UP(x, a)		(((x) + (a) - 1) & ~((a) - 1))
#define ROUND_DOWN(x, a)	((x) & ~((a) - 1))

static void __init
pbus_set_ranges(struct pci_bus *bus, struct pbus_set_ranges_data *outer)
{
	struct pbus_set_ranges_data inner;
	struct pci_bus *child;
	struct pci_dev *dev;

	inner.found_vga = 0;
	inner.mem_start = inner.io_start = ~0UL;
	inner.mem_end = inner.io_end = 0;

	/* Collect information about how our direct children are layed out. */
	for (dev = bus->devices; dev; dev = dev->sibling) {
		int i;
		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *res = &dev->resource[i];
			if (res->flags & IORESOURCE_IO) {
				if (res->start < inner.io_start)
					inner.io_start = res->start;
				if (res->end > inner.io_end)
					inner.io_end = res->end;
			} else if (res->flags & IORESOURCE_MEM) {
				if (res->start < inner.mem_start)
					inner.mem_start = res->start;
				if (res->end > inner.mem_end)
					inner.mem_end = res->end;
			}
		}
                if ((dev->class >> 8) == PCI_CLASS_DISPLAY_VGA)
                        inner.found_vga = 1;
	}

	/* And for all of the sub-busses.  */
	for (child = bus->children; child; child = child->next)
		pbus_set_ranges(child, &inner);

	/* Align the values.  */
	inner.io_start = ROUND_DOWN(inner.io_start, 4*1024);
	inner.io_end = ROUND_UP(inner.io_end, 4*1024);

	inner.mem_start = ROUND_DOWN(inner.mem_start, 1*1024*1024);
	inner.mem_end = ROUND_UP(inner.mem_end, 1*1024*1024);

	pcibios_fixup_pbus_ranges(bus, &inner);

	/* Configure the bridge, if possible.  */
	if (bus->self) {
		struct pci_dev *bridge = bus->self;
		u32 l;

                /* Set up the top and bottom of the PCI I/O segment
                   for this bus.  */
                pci_read_config_dword(bridge, PCI_IO_BASE, &l);
                l &= 0xffff0000;
                l |= (inner.io_start >> 8) & 0x00f0;
		l |= (inner.io_end - 1) & 0xf000;
                pci_write_config_dword(bridge, PCI_IO_BASE, l);

                /*
                 * Clear out the upper 16 bits of IO base/limit.
                 * Clear out the upper 32 bits of PREF base/limit.
                 */
                pci_write_config_dword(bridge, PCI_IO_BASE_UPPER16, 0);
                pci_write_config_dword(bridge, PCI_PREF_BASE_UPPER32, 0);
                pci_write_config_dword(bridge, PCI_PREF_LIMIT_UPPER32, 0);

                /* Set up the top and bottom of the PCI Memory segment
                   for this bus.  */
                l = (inner.mem_start & 0xfff00000) >> 16;
		l |= (inner.mem_end - 1) & 0xfff00000;
                pci_write_config_dword(bridge, PCI_MEMORY_BASE, l);

                /*
                 * Turn off downstream PF memory address range, unless
                 * there is a VGA behind this bridge, in which case, we
                 * enable the PREFETCH range to include BIOS ROM at C0000.
                 *
                 * NOTE: this is a bit of a hack, done with PREFETCH for
                 * simplicity, rather than having to add it into the above
                 * non-PREFETCH range, which could then be bigger than we want.
                 * We might assume that we could relocate the BIOS ROM, but
                 * that would depend on having it found by those who need it
                 * (the DEC BIOS emulator would find it, but I do not know
                 * about the Xservers). So, we do it this way for now... ;-)
                 */
                l = (inner.found_vga) ? 0 : 0x0000ffff;
                pci_write_config_dword(bridge, PCI_PREF_MEMORY_BASE, l);

                /*
                 * Tell bridge that there is an ISA bus in the system,
                 * and (possibly) a VGA as well.
                 */
                l = (inner.found_vga) ? 0x0c : 0x04;
                pci_write_config_byte(bridge, PCI_BRIDGE_CONTROL, l);

                /*
                 * Clear status bits,
                 * turn on I/O    enable (for downstream I/O),
                 * turn on memory enable (for downstream memory),
                 * turn on master enable (for upstream memory and I/O).
                 */
                pci_write_config_dword(bridge, PCI_COMMAND, 0xffff0007);
	}

	if (outer) {
		outer->found_vga |= inner.found_vga;
		if (inner.io_start < outer->io_start)
			outer->io_start = inner.io_start;
		if (inner.io_end > outer->io_end)
			outer->io_end = inner.io_end;
		if (inner.mem_start < outer->mem_start)
			outer->mem_start = inner.mem_start;
		if (inner.mem_end > outer->mem_end)
			outer->mem_end = inner.mem_end;
	}
}

void __init
pci_set_bus_ranges(void)
{
	struct pci_bus *bus;
	for (bus = pci_root; bus; bus = bus->next)
		pbus_set_ranges(bus, NULL);
}

static void
pdev_fixup_irq(struct pci_dev *dev,
	       u8 (*swizzle)(struct pci_dev *, u8 *),
	       int (*map_irq)(struct pci_dev *, u8, u8))
{
	u8 pin, slot;
	int irq;

	/* If this device is not on the primary bus, we need to figure out
	   which interrupt pin it will come in on.   We know which slot it
	   will come in on 'cos that slot is where the bridge is.   Each
	   time the interrupt line passes through a PCI-PCI bridge we must
	   apply the swizzle function.  */

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	/* Cope with 0 and illegal. */
	if (pin == 0 || pin > 4)
		pin = 1;

	/* Follow the chain of bridges, swizzling as we go.  */
	slot = (*swizzle)(dev, &pin);

	irq = (*map_irq)(dev, slot, pin);
	if (irq == -1)
		irq = 0;
	dev->irq = irq;

	DBGC(("PCI fixup irq: (%s) got %d\n", dev->name, dev->irq));

	/* Always tell the device, so the driver knows what is
	   the real IRQ to use; the device does not use it. */
	pcibios_update_irq(dev, irq);
}

void __init
pci_fixup_irqs(u8 (*swizzle)(struct pci_dev *, u8 *),
	       int (*map_irq)(struct pci_dev *, u8, u8))
{
	struct pci_dev *dev;
	for (dev = pci_devices; dev; dev = dev->next)
		pdev_fixup_irq(dev, swizzle, map_irq);
}

int
pcibios_enable_device(struct pci_dev *dev)
{
	return 0;
}
