/*
 *	linux/arch/alpha/kernel/pci.c
 *
 * Extruded from code written by
 *	Dave Rusling (david.rusling@reo.mts.dec.com)
 *	David Mosberger (davidm@cs.arizona.edu)
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/pci.h>
#include <asm/machvec.h>

#include "proto.h"
#include "pci_impl.h"


/*
 * Some string constants used by the various core logics. 
 */

const char *const pci_io_names[] = {
	"PCI IO bus 0", "PCI IO bus 1", "PCI IO bus 2", "PCI IO bus 3"
};

const char *const pci_mem_names[] = {
	"PCI mem bus 0", "PCI mem bus 1", "PCI mem bus 2", "PCI mem bus 3"
};

const char pci_hae0_name[] = "HAE0";


/*
 * The PCI controler list.
 */

struct pci_controler *hose_head, **hose_tail = &hose_head;
struct pci_controler *probing_hose;

/*
 * Quirks.
 */

static void __init
quirk_eisa_bridge(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_EISA;
}

static void __init
quirk_isa_bridge(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_ISA;
}

struct pci_fixup pcibios_fixups[] __initdata = {
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82375,
	  quirk_eisa_bridge },
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82378,
	  quirk_isa_bridge },
	{ 0 }
};


/* 
 * Pre-layout host-independant device initialization.
 */

static void __init
pci_assign_special(void)
{
	struct pci_dev *dev;

	/* The first three resources of the Cypress IDE controler need
	   to remain unchanged.  So allocate them as-is.  */
	dev = NULL;
	while ((dev = pci_find_device(PCI_VENDOR_ID_CONTAQ,
				       PCI_DEVICE_ID_CONTAQ_82C693, dev))) {
		if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
			pci_record_assignment(dev, 0);
			pci_record_assignment(dev, 1);
			pci_record_assignment(dev, 2);
		}
	}
}


void __init
pcibios_init(void)
{
	if (!alpha_mv.init_pci)
		return;
	alpha_mv.init_pci();
}

char * __init
pcibios_setup(char *str)
{
	return str;
}

void __init
pcibios_fixup_bus(struct pci_bus *bus)
{
	/* Propogate hose info into the subordinate devices.  */

	struct pci_dev *dev;
	void *sysdata;

	sysdata = (bus->parent ? bus->parent->sysdata : bus->sysdata);
	for (dev = bus->devices; dev; dev = dev->sibling)
		dev->sysdata = sysdata;
}

void __init
pcibios_base_address_update(struct pci_dev *dev, int resource)
{
	struct pci_controler *hose = dev->sysdata;
        struct resource *res = &dev->resource[resource];
        unsigned long base, where, size;
        u32 reg;

        if (res->flags & IORESOURCE_IO)
                base = hose->io_space->start;
        else
                base = hose->mem_space->start;

        where = PCI_BASE_ADDRESS_0 + (resource * 4);
        size = res->end - res->start;
        pci_read_config_dword(dev, where, &reg);
        reg = (reg & size) | (((u32)(res->start - base)) & ~size);
        pci_write_config_dword(dev, where, reg);

	/* ??? FIXME -- record old value for shutdown.  */
}

void __init
pcibios_irq_update(struct pci_dev *dev, u8 irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);

	/* ??? FIXME -- record old value for shutdown.  */
}

/* Most Alphas have straight-forward swizzling needs.  */

u8 __init
common_swizzle(struct pci_dev *dev, u8 *pinp)
{
	struct pci_controler *hose = dev->sysdata;

	if (dev->bus->number != hose->first_busno) {
		u8 pin = *pinp;
		do {
			pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
			/* Move up the chain of bridges. */
			dev = dev->bus->self;
		} while (dev->bus->self);
		*pinp = pin;

		/* The slot is the slot of the last bridge. */
	}

	return PCI_SLOT(dev->devfn);
}

void __init
common_init_pci(void)
{
	struct pci_controler *hose;
	struct pci_bus *bus;
	int next_busno;

	/* Scan all of the recorded PCI controlers.  */
	for (next_busno = 0, hose = hose_head; hose; hose = hose->next) {
		hose->first_busno = next_busno;
		hose->last_busno = 0xff;
		probing_hose = hose;
		bus = pci_scan_bus(next_busno, alpha_mv.pci_ops, hose);
		hose->bus = bus;
		next_busno = hose->last_busno = bus->subordinate;
		next_busno += 1;
	}
	probing_hose = NULL;

	pci_assign_special();
	pci_assign_unassigned(alpha_mv.min_io_address,
			      alpha_mv.min_mem_address);
	pci_fixup_irq(alpha_mv.pci_swizzle, alpha_mv.pci_map_irq);
	pci_set_bus_ranges();
}


struct pci_controler * __init
alloc_pci_controler(unsigned long *mem_start)
{
	unsigned long start = *mem_start;
	struct pci_controler *hose;
	if (start & 31)
		start = (start | 31) + 1;
	hose = (void *) start;
	start = (unsigned long) (hose + 1);
	*mem_start = start;

	memset(hose, 0, sizeof(*hose));

	*hose_tail = hose;
	hose_tail = &hose->next;

	return hose;
}

struct resource * __init
alloc_resource(unsigned long *mem_start)
{
	unsigned long start = *mem_start;
	struct resource *res;
	if (start & 31)
		start = (start | 31) + 1;
	res = (void *) start;
	start = (unsigned long) (res + 1);
	*mem_start = start;

	memset(res, 0, sizeof(*res));

	return res;
}
