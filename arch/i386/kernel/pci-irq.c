/*
 *	Low-Level PCI Support for PC -- Routing of Interrupts
 *
 *	(c) 1999--2000 Martin Mares <mj@suse.cz>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/io.h>

#include "pci-i386.h"

extern int skip_ioapic_setup;

#define PIRQ_SIGNATURE	(('$' << 0) + ('P' << 8) + ('I' << 16) + ('R' << 24))
#define PIRQ_VERSION 0x0100

static struct irq_routing_table *pirq_table;

/*
 * Never use: 0, 1, 2 (timer, keyboard, and cascade)
 * Avoid using: 13, 14 and 15 (FP error and IDE).
 * Penalize: 3, 4, 7, 12 (known ISA uses: serial, parallel and mouse)
 */
unsigned int pcibios_irq_mask = 0xfff8;

static unsigned pirq_penalty[16] = {
	10000, 10000, 10000, 100, 100, 0, 0, 100,
	0, 0, 0, 0, 100, 1000, 1000, 1000
};

struct irq_router {
	char *name;
	u16 vendor, device;
	int (*get)(struct pci_dev *router, struct pci_dev *dev, int pirq);
	int (*set)(struct pci_dev *router, struct pci_dev *dev, int pirq, int new);
};

/*
 *  Search 0xf0000 -- 0xfffff for the PCI IRQ Routing Table.
 */

static struct irq_routing_table * __init pirq_find_routing_table(void)
{
	u8 *addr;
	struct irq_routing_table *rt;
	int i;
	u8 sum;

	for(addr = (u8 *) __va(0xf0000); addr < (u8 *) __va(0x100000); addr += 16) {
		rt = (struct irq_routing_table *) addr;
		if (rt->signature != PIRQ_SIGNATURE ||
		    rt->version != PIRQ_VERSION ||
		    rt->size % 16 ||
		    rt->size < sizeof(struct irq_routing_table))
			continue;
		sum = 0;
		for(i=0; i<rt->size; i++)
			sum += addr[i];
		if (!sum) {
			DBG("PCI: Interrupt Routing Table found at 0x%p\n", rt);
			return rt;
		}
	}
	return NULL;
}

/*
 *  If we have a IRQ routing table, use it to search for peer host
 *  bridges.  It's a gross hack, but since there are no other known
 *  ways how to get a list of buses, we have to go this way.
 */

static void __init pirq_peer_trick(void)
{
	struct irq_routing_table *rt = pirq_table;
	u8 busmap[256];
	int i;
	struct irq_info *e;

	memset(busmap, 0, sizeof(busmap));
	for(i=0; i < (rt->size - sizeof(struct irq_routing_table)) / sizeof(struct irq_info); i++) {
		e = &rt->slots[i];
#ifdef DEBUG
		{
			int j;
			DBG("%02x:%02x slot=%02x", e->bus, e->devfn/8, e->slot);
			for(j=0; j<4; j++)
				DBG(" %d:%02x/%04x", j, e->irq[j].link, e->irq[j].bitmap);
			DBG("\n");
		}
#endif
		busmap[e->bus] = 1;
	}
	for(i=1; i<256; i++)
		/*
		 *  It might be a secondary bus, but in this case its parent is already
		 *  known (ascending bus order) and therefore pci_scan_bus returns immediately.
		 */
		if (busmap[i] && pci_scan_bus(i, pci_root_bus->ops, NULL))
			printk("PCI: Discovered primary peer bus %02x [IRQ]\n", i);
	pcibios_last_bus = -1;
}

/*
 *  Code for querying and setting of IRQ routes on various interrupt routers.
 */

static void eisa_set_level_irq(unsigned int irq)
{
	unsigned char mask = 1 << (irq & 7);
	unsigned int port = 0x4d0 + (irq >> 3);
	unsigned char val = inb(port);

	if (!(val & mask)) {
		DBG(" -> edge");
		outb(val | mask, port);
	}
}

static int pirq_ali_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	static unsigned char irqmap[16] = {
		0, 8, 0, 2, 4, 5, 7, 6, 0, 1, 3, 9, 11, 0, 13, 15
	};
	unsigned int val = irqmap[irq];
	pirq--;
	if (val && pirq < 8) {
		u8 x;
		unsigned reg = 0x48 + (pirq >> 1);
		pci_read_config_byte(router, reg, &x);
		x = (pirq & 1) ? ((x & 0x0f) | (val << 4)) : ((x & 0xf0) | val);
		pci_write_config_byte(router, reg, x);
		eisa_set_level_irq(irq);
		return 1;
	}
	return 0;
}

static int pirq_piix_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	u8 x;
	pci_read_config_byte(router, pirq, &x);
	return (x < 16) ? x : 0;
}

static int pirq_piix_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	pci_write_config_byte(router, pirq, irq);
	return 1;
}

static int pirq_via_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	u8 x;
	int reg = 0x55 + (pirq >> 1);
	pci_read_config_byte(router, reg, &x);
	return (pirq & 1) ? (x >> 4) : (x & 0x0f);
}

static int pirq_via_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	u8 x;
	int reg = 0x55 + (pirq >> 1);
	pci_read_config_byte(router, reg, &x);
	x = (pirq & 1) ? ((x & 0x0f) | (irq << 4)) : ((x & 0xf0) | irq);
	pci_write_config_byte(router, reg, x);
	return 1;
}

static int pirq_opti_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	u8 x;
	int reg = 0xb8 + (pirq >> 5);
	pci_read_config_byte(router, reg, &x);
	return (pirq & 0x10) ? (x >> 4) : (x & 0x0f);
}

static int pirq_opti_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	u8 x;
	int reg = 0xb8 + (pirq >> 5);
	pci_read_config_byte(router, reg, &x);
	x = (pirq & 0x10) ? ((x & 0x0f) | (irq << 4)) : ((x & 0xf0) | irq);
	pci_write_config_byte(router, reg, x);
	return 1;
}

#ifdef CONFIG_PCI_BIOS

static int pirq_bios_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	struct pci_dev *bridge;
	int pin = pci_get_interrupt_pin(dev, &bridge);
	return pcibios_set_irq_routing(bridge, pin, irq);
}

static struct irq_router pirq_bios_router =
	{ "BIOS", 0, 0, NULL, pirq_bios_set };

#endif

static struct irq_router pirq_routers[] = {
	{ "PIIX", PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371FB_0, pirq_piix_get, pirq_piix_set },
	{ "PIIX", PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_0, pirq_piix_get, pirq_piix_set },
	{ "PIIX", PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_0, pirq_piix_get, pirq_piix_set },
	{ "ALI", PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533, NULL, pirq_ali_set },
	{ "VIA", PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_0, pirq_via_get, pirq_via_set },
	{ "VIA", PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C596, pirq_via_get, pirq_via_set },
	{ "VIA", PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686, pirq_via_get, pirq_via_set },
	{ "OPTI", PCI_VENDOR_ID_OPTI, PCI_DEVICE_ID_OPTI_82C700, pirq_opti_get, pirq_opti_set },
	{ "default", 0, 0, NULL, NULL }
};

static struct irq_router *pirq_router;
static struct pci_dev *pirq_router_dev;

static void __init pirq_find_router(void)
{
	struct irq_routing_table *rt = pirq_table;
	u16 rvendor, rdevice;
	struct irq_router *r;

#ifdef CONFIG_PCI_BIOS
	if (!rt->signature) {
		printk("PCI: Using BIOS for IRQ routing\n");
		pirq_router = &pirq_bios_router;
		return;
	}
#endif
	if (!(pirq_router_dev = pci_find_slot(rt->rtr_bus, rt->rtr_devfn))) {
		DBG("PCI: Interrupt router not found\n");
		return;
	}
	if (rt->rtr_vendor) {
		rvendor = rt->rtr_vendor;
		rdevice = rt->rtr_device;
	} else {
		/*
		 * Several BIOSes forget to set the router type. In such cases, we
		 * use chip vendor/device. This doesn't guarantee us semantics of
		 * PIRQ values, but was found to work in practice and it's still
		 * better than not trying.
		 */
		DBG("PCI: Guessed interrupt router ID from %s\n", pirq_router_dev->slot_name);
		rvendor = pirq_router_dev->vendor;
		rdevice = pirq_router_dev->device;
	}
	for(r=pirq_routers; r->vendor; r++)
		if (r->vendor == rvendor && r->device == rdevice)
			break;
	pirq_router = r;
	printk("PCI: Using IRQ router %s [%04x/%04x] at %s\n", r->name,
	       rvendor, rdevice, pirq_router_dev->slot_name);
}

static struct irq_info *pirq_get_info(struct pci_dev *dev, int pin)
{
	struct irq_routing_table *rt = pirq_table;
	int entries = (rt->size - sizeof(struct irq_routing_table)) / sizeof(struct irq_info);
	struct irq_info *info;

	for (info = rt->slots; entries--; info++)
		if (info->bus == dev->bus->number && PCI_SLOT(info->devfn) == PCI_SLOT(dev->devfn))
			return info;
	return NULL;
}

int pcibios_lookup_irq(struct pci_dev *dev, int assign)
{
	struct irq_info *info;
	int i, pirq, pin, newirq;
	int irq = 0;
	u32 mask;
	struct irq_router *r = pirq_router;
	struct pci_dev *dev2, *d;
	char *msg = NULL;

	if (!pirq_table)
		return 0;

	/* Find IRQ routing entry */
	pin = pci_get_interrupt_pin(dev, &d);
	if (pin < 0) {
		DBG(" -> no interrupt pin\n");
		return 0;
	}
	DBG("IRQ for %s(%d) via %s", dev->slot_name, pin, d->slot_name);
	info = pirq_get_info(d, pin);
	if (!info) {
		DBG(" -> not found in routing table\n");
		return 0;
	}
	pirq = info->irq[pin].link;
	mask = info->irq[pin].bitmap;
	if (!pirq) {
		DBG(" -> not routed\n");
		return 0;
	}
	DBG(" -> PIRQ %02x, mask %04x, excl %04x", pirq, mask, pirq_table->exclusive_irqs);
	mask &= pcibios_irq_mask;

	/* Find the best IRQ to assign */
	newirq = 0;
	for (i = 0; i < 16; i++) {
		if (!(mask & (1 << i)))
			continue;
		if (pirq_penalty[i] < pirq_penalty[newirq])
			newirq = i;
	}
	DBG(" -> newirq=%d", newirq);

	/* Try to get current IRQ */
	if (r->get && (irq = r->get(pirq_router_dev, d, pirq))) {
		DBG(" -> got IRQ %d\n", irq);
		msg = "Found";
	} else if (assign && newirq && r->set && (dev->class >> 8) != PCI_CLASS_DISPLAY_VGA) {
		DBG(" -> assigning IRQ %d", newirq);
		if (r->set(pirq_router_dev, d, pirq, newirq)) {
			DBG(" ... OK\n");
			msg = "Assigned";
			irq = newirq;
		}
	}

	if (!irq) {
		DBG(" ... failed\n");
		if (newirq && mask == (1 << newirq)) {
			msg = "Guessed";
			irq = newirq;
		} else
			return 0;
	}
	printk("PCI: %s IRQ %d for device %s\n", msg, irq, dev->slot_name);

	/* Update IRQ for all devices with the same pirq value */
	pci_for_each_dev(dev2) {
		if ((pin = pci_get_interrupt_pin(dev2, &d)) >= 0 &&
		    (info = pirq_get_info(d, pin)) &&
		    info->irq[pin].link == pirq) {
			dev2->irq = irq;
			pirq_penalty[irq]++;
			if (dev != dev2)
				printk("PCI: The same IRQ used for device %s\n", dev2->slot_name);
		}
	}
	return 1;
}

void __init pcibios_fixup_irqs(void)
{
	struct pci_dev *dev;
	u8 pin;

	DBG("PCI: IRQ fixup\n");
	pirq_table = pirq_find_routing_table();
#ifdef CONFIG_PCI_BIOS
	if (!pirq_table && (pci_probe & PCI_BIOS_IRQ_SCAN))
		pirq_table = pcibios_get_irq_routing_table();
#endif
	if (pirq_table) {
		pirq_peer_trick();
		pirq_find_router();
	}

	pci_for_each_dev(dev) {
		/*
		 * If the BIOS has set an out of range IRQ number, just ignore it.
		 * Also keep track of which IRQ's are already in use.
		 */
		if (dev->irq >= 16) {
			DBG("%s: ignoring bogus IRQ %d\n", dev->slot_name, dev->irq);
			dev->irq = 0;
		}
		pirq_penalty[dev->irq]++;
	}

	pci_for_each_dev(dev) {
		pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
#if defined(CONFIG_X86_IO_APIC)
		/*
		 * Recalculate IRQ numbers if we use the I/O APIC.
		 */
		if (!skip_ioapic_setup)
		{
			int irq;

			if (pin) {
				pin--;		/* interrupt pins are numbered starting from 1 */
				irq = IO_APIC_get_PCI_irq_vector(dev->bus->number, PCI_SLOT(dev->devfn), pin);
/*
 * Will be removed completely if things work out well with fuzzy parsing
 */
#if 0
				if (irq < 0 && dev->bus->parent) { /* go back to the bridge */
					struct pci_dev * bridge = dev->bus->self;

					pin = (pin + PCI_SLOT(dev->devfn)) % 4;
					irq = IO_APIC_get_PCI_irq_vector(bridge->bus->number, 
							PCI_SLOT(bridge->devfn), pin);
					if (irq >= 0)
						printk(KERN_WARNING "PCI: using PPB(B%d,I%d,P%d) to get irq %d\n", 
							bridge->bus->number, PCI_SLOT(bridge->devfn), pin, irq);
				}
#endif
				if (irq >= 0) {
					printk("PCI->APIC IRQ transform: (B%d,I%d,P%d) -> %d\n",
						dev->bus->number, PCI_SLOT(dev->devfn), pin, irq);
					dev->irq = irq;
				}
			}
			pirq_table = NULL;	/* Avoid automatic IRQ assignment */
		}
#endif
		/*
		 * Still no IRQ? Try to lookup one...
		 */
		if (pin && !dev->irq)
			pcibios_lookup_irq(dev, 0);
	}
}
