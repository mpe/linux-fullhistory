/*
 * arch/arm/kernel/bios32.c
 *
 * PCI bios-type initialisation for PCI machines
 *
 * Bits taken from various places.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/system.h>

int have_isa_bridge;

int (*pci_irq_fixup)(struct pci_dev *dev);

extern struct pci_ops *dc21285_init(int pass);
extern void pcibios_fixup_ebsa285(struct pci_dev *dev);
extern void hw_init(void);

void
pcibios_report_device_errors(void)
{
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		u16 status;

		pci_read_config_word(dev, PCI_STATUS, &status);

		if (status & 0xf900) {
			pci_write_config_word(dev, PCI_STATUS, status & 0xf900);
			printk(KERN_DEBUG "PCI: %02x:%02x status = %X\n",
				dev->bus->number, dev->devfn, status);
		}
	}
}

/*
 * We don't use this to fix the device, but more our initialisation.
 * It's not the correct use for this, but it works.  The actions we
 * take are:
 * - enable only IO
 * - set memory region to start at zero
 * - (0x48) enable all memory requests from ISA to be channeled to PCI
 * - (0x42) disable ping-pong (as per errata)
 * - (0x40) enable PCI packet retry
 * - (0x83) don't use CPU park enable, park on last master, disable GAT bit
 * - (0x80) default rotating priorities
 * - (0x81) rotate bank 4
 */
static void __init pci_fixup_83c553(struct pci_dev *dev)
{
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_SPACE_MEMORY);
	pci_write_config_word(dev, PCI_COMMAND, PCI_COMMAND_IO);

	dev->resource[0].end -= dev->resource[0].start;
	dev->resource[0].start = 0;

	pci_write_config_byte(dev, 0x48, 0xff);
	pci_write_config_byte(dev, 0x42, 0x00);
	pci_write_config_byte(dev, 0x40, 0x22);
	pci_write_config_byte(dev, 0x83, 0x02);
	pci_write_config_byte(dev, 0x80, 0xe0);
	pci_write_config_byte(dev, 0x81, 0x01);
}

struct pci_fixup pcibios_fixups[] = {
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_83C553, pci_fixup_83c553 },
	{ 0 }
};

/*
 * Assign new address to PCI resource.  We hope our resource information
 * is complete.  On the PC, we don't re-assign resources unless we are
 * forced to do so.
 *
 * Expects start=0, end=size-1, flags=resource type.
 */

int __init pcibios_assign_resource(struct pci_dev *dev, int i)
{
	struct resource *r = &dev->resource[i];
	struct resource *pr = pci_find_parent_resource(dev, r);
	unsigned long size = r->end + 1;
	unsigned long flags = 0;

	if (!pr)
		return -EINVAL;
	if (r->flags & IORESOURCE_IO) {
		if (size > 0x100)
			return -EFBIG;
		if (allocate_resource(pr, r, size, 0x9000, ~0, 1024))
			return -EBUSY;
		flags = PCI_BASE_ADDRESS_SPACE_IO;
	} else {
		if (allocate_resource(pr, r, size, 0x00100000, 0x7fffffff, size))
			return -EBUSY;
	}
	if (i < 6)
		pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + 4*i, r->start | flags);
	return 0;
}

/*
 * Assign an address to an I/O range.
 */
static void __init pcibios_fixup_io_addr(struct pci_dev *dev, struct resource *r, int idx)
{
	unsigned int reg = PCI_BASE_ADDRESS_0 + (idx << 2);
	unsigned int size = r->end - r->start + 1;
	u32 try;

	/*
	 * We need to avoid collisions with `mirrored' VGA ports and other strange
	 * ISA hardware, so we always want the addresses kilobyte aligned.
	 */
	if (!size || size > 256) {
		printk(KERN_ERR "PCI: Cannot assign I/O space to %s, "
		       "%d bytes are too much.\n", dev->name, size);
		return;
	}

	if (allocate_resource(&ioport_resource, r, size, 0x9000, ~0, 1024)) {
		printk(KERN_ERR "PCI: Unable to find free %d bytes of I/O "
			"space for %s.\n", size, dev->name);
		return;
	}

	printk("PCI: Assigning I/O space %04lx-%04lx to %s\n",
		r->start, r->end, dev->name);

	pci_write_config_dword(dev, reg, r->start | PCI_BASE_ADDRESS_SPACE_IO);
	pci_read_config_dword(dev, reg, &try);

	if ((try & PCI_BASE_ADDRESS_IO_MASK) != r->start) {
		r->start = 0;
		pci_write_config_dword(dev, reg, 0);
		printk(KERN_ERR "PCI: I/O address setup failed, got %04x\n", try);
	}
}

/*
 * Assign an address to an memory range.
 */
static void __init pcibios_fixup_mem_addr(struct pci_dev *dev, struct resource *r, int idx)
{
	unsigned int reg = PCI_BASE_ADDRESS_0 + (idx << 2);
	unsigned int size = r->end - r->start + 1;
	u32 try;

	if (!size) {
		printk(KERN_ERR "PCI: Cannot assign memory space to %s, "
		       "%d bytes are too much.\n", dev->name, size);
		return;
	}

	if (allocate_resource(&iomem_resource, r, size,
			      0x00100000, 0x0fffffff, 1024)) {
		printk(KERN_ERR "PCI: Unable to find free %d bytes of memory "
			"space for %s.\n", size, dev->name);
		return;
	}

	printk("PCI: Assigning memory space %08lx-%08lx to %s\n",
		r->start, r->end, dev->name);

	pci_write_config_dword(dev, reg, r->start);
	pci_read_config_dword(dev, reg, &try);

	if (try != r->start) {
		r->start = 0;
		pci_write_config_dword(dev, reg, 0);
		printk(KERN_ERR "PCI: memory address setup failed, "
		       "got %08x\n", try);
	}
}

#define _PCI_REGION_IO	1
#define _PCI_REGION_MEM	2

/*
 * Fix up one PCI devices regions, enables and interrupt lines
 */
static void __init pcibios_fixup_device(struct pci_dev *dev, u16 *cmd)
{
	int i, has_regions = 0;

	/*
	 * Fix up the regions.  Any regions which aren't allocated
	 * are given a free region.
	 */
	for (i = 0; i < 6; i++) {
		struct resource *r = dev->resource + i;

		if (r->flags & IORESOURCE_IO) {
			has_regions |= _PCI_REGION_IO;

			if (!r->start || r->end == 0xffffffff)
				pcibios_fixup_io_addr(dev, r, i);
		} else if (r->end) {
			has_regions |= _PCI_REGION_MEM;

			if (!r->start)
				pcibios_fixup_mem_addr(dev, r, i);
		}
	}

	switch (dev->class >> 8) {
	case PCI_CLASS_BRIDGE_ISA:
	case PCI_CLASS_BRIDGE_EISA:
		/*
		 * If this device is an ISA bridge, set the have_isa_bridge
		 * flag.  We will then go looking for things like keyboard,
		 * etc
		 */
		have_isa_bridge = !0;
		/* FALL THROUGH */

	default:
		/*
		 * Don't enable VGA-compatible cards since they have
		 * fixed I/O and memory space.
		 *
		 * Don't enabled disabled IDE interfaces either because
		 * some BIOSes may reallocate the same address when they
		 * find that no devices are attached. 
		 */
		if (has_regions & _PCI_REGION_IO &&
		    !((*cmd) & PCI_COMMAND_IO)) {
			printk("PCI: Enabling I/O for %s\n", dev->name);
			*cmd |= PCI_COMMAND_IO;
		}

		if (has_regions & _PCI_REGION_MEM &&
		    !((*cmd) & PCI_COMMAND_MEMORY)) {
			printk("PCI: Enabling memory for %s\n", dev->name);
			*cmd |= PCI_COMMAND_MEMORY;
		}
	}
}

/*
 * Fix base addresses, I/O and memory enables and IRQ's
 */
static void __init pcibios_fixup_devices(void)
{
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		u16 cmd;

		/*
		 * architecture specific hacks.
		 * I don't really want this here,
		 * but I don't see any other place
		 * for it to live.
		 */
		if (machine_is_netwinder() &&
		    dev->vendor == PCI_VENDOR_ID_DEC &&
		    dev->device == PCI_DEVICE_ID_DEC_21142)
			/* Put the chip to sleep in case the driver isn't loaded */
			pci_write_config_dword(dev, 0x40, 0x80000000);

		/*
		 * Set latency timer to 32, and a cache line size to 32 bytes.
		 * Also, set system error enable, parity error enable, and
		 * fast back to back transaction enable.  Disable ROM.
		 */
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 32);
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 8);
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, 0);
		pci_read_config_word(dev, PCI_COMMAND, &cmd);

		cmd |= PCI_COMMAND_FAST_BACK | PCI_COMMAND_SERR |
		       PCI_COMMAND_PARITY;

		pcibios_fixup_device(dev, &cmd);

		pci_write_config_word(dev, PCI_COMMAND, cmd);
		pci_read_config_word(dev, PCI_COMMAND, &cmd);

		/*
		 * now fixup the IRQs, if required
		 */
		if (pci_irq_fixup)
			dev->irq = pci_irq_fixup(dev);

		/*
		 * If any remaining IRQs are weird, fix it now.
		 */
		if (dev->irq >= NR_IRQS)
			dev->irq = 0;

		/*
		 * catch any drivers still reading this from the
		 * device itself.  This can be removed once
		 * all drivers are fixed. (are there any?)
		 */
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}
}

/*
 * Allocate resources for all PCI devices that have been enabled.
 * We need to do that before we try to fix up anything.
 */
static void __init pcibios_claim_resources(void)
{
	struct pci_dev *dev;
	int idx;

	for (dev = pci_devices; dev; dev = dev->next)
		for (idx = 0; idx < PCI_NUM_RESOURCES; idx++) {
			struct resource *a, *r = &dev->resource[idx];

			/*
			 * Ignore regions that start at 0 or
			 * end at 0xffffffff
			 */
			if (!r->start || r->end == 0xffffffff)
				continue;

			if (r->flags & IORESOURCE_IO)
				a = &ioport_resource;
			else
				a = &iomem_resource;

			if (request_resource(a, r) < 0)
				printk(KERN_ERR "PCI: Address space collision "
					"on region %d of %s\n",
					idx, dev->name);
				/* We probably should disable the region,
				 * shouldn't we?
				 */
		}
}

/*
 * Called after each bus is probed, but before its children
 * are examined.
 *
 * No fixup of bus required
 */
void __init pcibios_fixup_bus(struct pci_bus *bus)
{
}

void __init pcibios_init(void)
{
	struct pci_ops *ops;

	/*
	 * Pre-initialisation.  Set up the host bridge.
	 */
	ops = dc21285_init(0);

	printk("PCI: Probing PCI hardware\n");

	pci_scan_bus(0, ops, NULL);
	pcibios_claim_resources();
	pcibios_fixup_devices();

	/*
	 * Now clear down any PCI error IRQs and
	 * register the error handler
	 */
	dc21285_init(1);

	/*
	 * Initialise any other hardware after we've
	 * got the PCI bus initialised.  We may need
	 * the PCI bus to talk to this other hardware.
	 */
	hw_init();
}

char * __init pcibios_setup(char *str)
{
	return str;
}
