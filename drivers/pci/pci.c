/*
 *	$Id: pci.c,v 1.55 1997/12/27 12:17:54 mj Exp $
 *
 *	PCI services that are built on top of the BIOS32 service.
 *
 *	Copyright 1993, 1994, 1995, 1997 Drew Eckhardt, Frederic Potter,
 *	David Mosberger-Tang, Martin Mares
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/page.h>

struct pci_bus pci_root;
struct pci_dev *pci_devices = 0;

#undef DEBUG

/*
 * pci_malloc() returns initialized memory of size SIZE.  Can be
 * used only while pci_init() is active.
 */
__initfunc(static void *pci_malloc(long size, unsigned long *mem_startp))
{
	void *mem;

	mem = (void*) *mem_startp;
	*mem_startp += (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
	memset(mem, 0, size);
	return mem;
}


const char *pcibios_strerror(int error)
{
	static char buf[32];

	switch (error) {
		case PCIBIOS_SUCCESSFUL:
		case PCIBIOS_BAD_VENDOR_ID:
			return "SUCCESSFUL";

		case PCIBIOS_FUNC_NOT_SUPPORTED:
			return "FUNC_NOT_SUPPORTED";

		case PCIBIOS_DEVICE_NOT_FOUND:
			return "DEVICE_NOT_FOUND";

		case PCIBIOS_BAD_REGISTER_NUMBER:
			return "BAD_REGISTER_NUMBER";

                case PCIBIOS_SET_FAILED:          
			return "SET_FAILED";

                case PCIBIOS_BUFFER_TOO_SMALL:    
			return "BUFFER_TOO_SMALL";

		default:
			sprintf (buf, "PCI ERROR 0x%x", error);
			return buf;
	}
}


unsigned int pci_scan_bus(struct pci_bus *bus, unsigned long *mem_startp)
{
	unsigned int devfn, l, max, class;
	unsigned char cmd, irq, tmp, hdr_type, is_multi = 0;
	struct pci_dev *dev;
	struct pci_bus *child;
	int reg;

#ifdef DEBUG
	printk("pci_scan_bus for bus %d\n", bus->number);
#endif

	max = bus->secondary;
	for (devfn = 0; devfn < 0xff; ++devfn) {
		if (PCI_FUNC(devfn) && !is_multi) {
			/* not a multi-function device */
			continue;
		}
		pcibios_read_config_byte(bus->number, devfn, PCI_HEADER_TYPE, &hdr_type);
		if (!PCI_FUNC(devfn))
			is_multi = hdr_type & 0x80;

		pcibios_read_config_dword(bus->number, devfn, PCI_VENDOR_ID, &l);
		/* some broken boards return 0 if a slot is empty: */
		if (l == 0xffffffff || l == 0x00000000) {
			hdr_type = 0;
			continue;
		}

		dev = pci_malloc(sizeof(*dev), mem_startp);
		dev->bus = bus;
		dev->devfn  = devfn;
		dev->vendor = l & 0xffff;
		dev->device = (l >> 16) & 0xffff;

		/* non-destructively determine if device can be a master: */
		pcibios_read_config_byte(bus->number, devfn, PCI_COMMAND, &cmd);
		pcibios_write_config_byte(bus->number, devfn, PCI_COMMAND, cmd | PCI_COMMAND_MASTER);
		pcibios_read_config_byte(bus->number, devfn, PCI_COMMAND, &tmp);
		dev->master = ((tmp & PCI_COMMAND_MASTER) != 0);
		pcibios_write_config_byte(bus->number, devfn, PCI_COMMAND, cmd);

		pcibios_read_config_dword(bus->number, devfn, PCI_CLASS_REVISION, &class);
		class >>= 8;				    /* upper 3 bytes */
		dev->class = class;

		switch (hdr_type & 0x7f) {		    /* header type */
		case 0:					    /* standard header */
			if (class >> 8 == PCI_CLASS_BRIDGE_PCI)
				goto bad;
			/* read irq level (may be changed during pcibios_fixup()): */
			pcibios_read_config_byte(bus->number, dev->devfn, PCI_INTERRUPT_LINE, &irq);
			dev->irq = irq;
			/*
			 * read base address registers, again pcibios_fixup() can
			 * tweak these
			 */
			for (reg = 0; reg < 6; reg++) {
				pcibios_read_config_dword(bus->number, devfn, PCI_BASE_ADDRESS_0 + (reg << 2), &l);
				dev->base_address[reg] = (l == 0xffffffff) ? 0 : l;
			}
			break;
		case 1:					    /* bridge header */
			if (class >> 8 != PCI_CLASS_BRIDGE_PCI)
				goto bad;
			for (reg = 0; reg < 2; reg++) {
				pcibios_read_config_dword(bus->number, devfn, PCI_BASE_ADDRESS_0 + (reg << 2), &l);
				dev->base_address[reg] = (l == 0xffffffff) ? 0 : l;
			}
			break;
		default:				    /* unknown header */
		bad:
			printk(KERN_ERR "PCI: %02x:%02x [%04x/%04x/%06x] has unknown header type %02x, ignoring.\n",
			       bus->number, dev->devfn, dev->vendor, dev->device, class, hdr_type);
			continue;
		}

#ifdef DEBUG
		printk("PCI: %02x:%02x [%04x/%04x]\n",
		       bus->number, dev->devfn, dev->vendor, dev->device);
#endif

		/*
		 * Put it into the global PCI device chain. It's used to
		 * find devices once everything is set up.
		 */
		dev->next = pci_devices;
		pci_devices = dev;

		/*
		 * Now insert it into the list of devices held
		 * by the parent bus.
		 */
		dev->sibling = bus->devices;
		bus->devices = dev;

		/*
		 * If it's a bridge, scan the bus behind it.
		 */
		if (class >> 8 == PCI_CLASS_BRIDGE_PCI) {
			unsigned int buses;
			unsigned short cr;

			/*
			 * Insert it into the tree of buses.
			 */
			child = pci_malloc(sizeof(*child), mem_startp);
			child->next = bus->children;
			bus->children = child;
			child->self = dev;
			child->parent = bus;

			/*
			 * Set up the primary, secondary and subordinate
			 * bus numbers.
			 */
			child->number = child->secondary = ++max;
			child->primary = bus->secondary;
			child->subordinate = 0xff;
			/*
			 * Clear all status bits and turn off memory,
			 * I/O and master enables.
			 */
			pcibios_read_config_word(bus->number, devfn, PCI_COMMAND, &cr);
			pcibios_write_config_word(bus->number, devfn, PCI_COMMAND, 0x0000);
			pcibios_write_config_word(bus->number, devfn, PCI_STATUS, 0xffff);
			/*
			 * Read the existing primary/secondary/subordinate bus
			 * number configuration to determine if the PCI bridge
			 * has already been configured by the system.  If so,
			 * do not modify the configuration, merely note it.
			 */
			pcibios_read_config_dword(bus->number, devfn, PCI_PRIMARY_BUS, &buses);
			if ((buses & 0xFFFFFF) != 0)
			  {
			    child->primary = buses & 0xFF;
			    child->secondary = (buses >> 8) & 0xFF;
			    child->subordinate = (buses >> 16) & 0xFF;
			    child->number = child->secondary;
			    max = pci_scan_bus(child, mem_startp);
			  }
			else
			  {
			    /*
			     * Configure the bus numbers for this bridge:
			     */
			    buses &= 0xff000000;
			    buses |=
			      (((unsigned int)(child->primary)     <<  0) |
			       ((unsigned int)(child->secondary)   <<  8) |
			       ((unsigned int)(child->subordinate) << 16));
			    pcibios_write_config_dword(bus->number, devfn, PCI_PRIMARY_BUS, buses);
			    /*
			     * Now we can scan all subordinate buses:
			     */
			    max = pci_scan_bus(child, mem_startp);
			    /*
			     * Set the subordinate bus number to its real
			     * value:
			     */
			    child->subordinate = max;
			    buses = (buses & 0xff00ffff)
			      | ((unsigned int)(child->subordinate) << 16);
			    pcibios_write_config_dword(bus->number, devfn, PCI_PRIMARY_BUS, buses);
			  }
			pcibios_write_config_word(bus->number, devfn, PCI_COMMAND, cr);
		}
	}
	/*
	 * We've scanned the bus and so we know all about what's on
	 * the other side of any bridges that may be on this bus plus
	 * any devices.
	 *
	 * Return how far we've got finding sub-buses.
	 */
#ifdef DEBUG
	printk("PCI: pci_scan_bus returning with max=%02x\n", max);
#endif
	return max;
}


__initfunc(unsigned long pci_init (unsigned long mem_start, unsigned long mem_end))
{
	mem_start = pcibios_init(mem_start, mem_end);

	if (!pcibios_present()) {
		printk("PCI: No PCI bus detected\n");
		return mem_start;
	}

	printk("Probing PCI hardware.\n");

	memset(&pci_root, 0, sizeof(pci_root));
	pci_root.subordinate = pci_scan_bus(&pci_root, &mem_start);

	/* give BIOS a chance to apply platform specific fixes: */
	mem_start = pcibios_fixup(mem_start, mem_end);

#ifdef CONFIG_PCI_OPTIMIZE
	pci_quirks_init();
#endif

	return mem_start;
}
