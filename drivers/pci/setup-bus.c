/*
 *	drivers/pci/setup-bus.c
 *
 * Extruded from code written by
 *      Dave Rusling (david.rusling@reo.mts.dec.com)
 *      David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *
 * Support routines for initializing a PCI subsystem.
 */

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


#define ROUND_UP(x, a)		(((x) + (a) - 1) & ~((a) - 1))
#define ROUND_DOWN(x, a)	((x) & ~((a) - 1))

static void __init
pbus_set_ranges(struct pci_bus *bus, struct pbus_set_ranges_data *outer)
{
	struct pbus_set_ranges_data inner;
	struct pci_dev *dev;
	struct list_head *ln;

	inner.found_vga = 0;
	inner.mem_start = inner.io_start = ~0UL;
	inner.mem_end = inner.io_end = 0;

	/* Collect information about how our direct children are layed out. */
	for (ln=bus->devices.next; ln != &bus->devices; ln=ln->next) {
		int i;
		dev = pci_dev_b(ln);
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
	for (ln=bus->children.next; ln != &bus->children; ln=ln->next)
		pbus_set_ranges(pci_bus_b(ln), &inner);

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
	struct list_head *ln;

	for(ln=pci_root_buses.next; ln != &pci_root_buses; ln=ln->next)
		pbus_set_ranges(pci_bus_b(ln), NULL);
}
