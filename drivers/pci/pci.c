/*
 *	$Id: pci.c,v 1.91 1999/01/21 13:34:01 davem Exp $
 *
 *	PCI Bus Services, see include/linux/pci.h for further explanation.
 *
 *	Copyright 1993 -- 1997 Drew Eckhardt, Frederic Potter,
 *	David Mosberger-Tang
 *
 *	Copyright 1997 -- 1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/ioport.h>

#include <asm/page.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

struct pci_bus *pci_root;
struct pci_dev *pci_devices = NULL;
static struct pci_dev **pci_last_dev_p = &pci_devices;
static int pci_reverse __initdata = 0;

struct pci_dev *
pci_find_slot(unsigned int bus, unsigned int devfn)
{
	struct pci_dev *dev;

	for(dev=pci_devices; dev; dev=dev->next)
		if (dev->bus->number == bus && dev->devfn == devfn)
			break;
	return dev;
}


struct pci_dev *
pci_find_subsys(unsigned int vendor, unsigned int device,
		unsigned int ss_vendor, unsigned int ss_device,
		struct pci_dev *from)
{
	struct pci_dev *dev;

	if (from)
		dev = from->next;
	else
		dev = pci_devices;

	while (dev) {
		if ((vendor == PCI_ANY_ID || dev->vendor == vendor) &&
		    (device == PCI_ANY_ID || dev->device == device) &&
		    (ss_vendor == PCI_ANY_ID || dev->subsystem_vendor == ss_vendor) &&
		    (ss_device == PCI_ANY_ID || dev->subsystem_device == ss_device))
			return dev;
		dev = dev->next;
	}
	return NULL;
}


struct pci_dev *
pci_find_device(unsigned int vendor, unsigned int device, struct pci_dev *from)
{
	return pci_find_subsys(vendor, device, PCI_ANY_ID, PCI_ANY_ID, from);
}


struct pci_dev *
pci_find_class(unsigned int class, struct pci_dev *from)
{
	if (!from)
		from = pci_devices;
	else
		from = from->next;
	while (from && from->class != class)
		from = from->next;
	return from;
}


/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */

static spinlock_t pci_lock = SPIN_LOCK_UNLOCKED;

/*
 *  Wrappers for all PCI configuration access functions.  They just check
 *  alignment, do locking and call the low-level functions pointed to
 *  by pci_dev->ops.
 */

#define PCI_byte_BAD 0
#define PCI_word_BAD (pos & 1)
#define PCI_dword_BAD (pos & 3)

#define PCI_OP(rw,size,type) \
int pci_##rw##_config_##size (struct pci_dev *dev, int pos, type value) \
{									\
	int res;							\
	unsigned long flags;						\
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;	\
	spin_lock_irqsave(&pci_lock, flags);				\
	res = dev->bus->ops->rw##_##size(dev, pos, value);		\
	spin_unlock_irqrestore(&pci_lock, flags);			\
	return res;							\
}

PCI_OP(read, byte, u8 *)
PCI_OP(read, word, u16 *)
PCI_OP(read, dword, u32 *)
PCI_OP(write, byte, u8)
PCI_OP(write, word, u16)
PCI_OP(write, dword, u32)


void
pci_set_master(struct pci_dev *dev)
{
	u16 cmd;
	u8 lat;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (! (cmd & PCI_COMMAND_MASTER)) {
		printk("PCI: Enabling bus mastering for device %s\n", dev->name);
		cmd |= PCI_COMMAND_MASTER;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat < 16) {
		printk("PCI: Increasing latency timer of device %s to 64\n", dev->name);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
	}
}

/*
 * Translate the low bits of the PCI base
 * to the resource type
 */
static inline unsigned int pci_resource_flags(unsigned int flags)
{
	if (flags & PCI_BASE_ADDRESS_SPACE_IO)
		return IORESOURCE_IO | flags;

	if (flags & PCI_BASE_ADDRESS_MEM_PREFETCH)
		return IORESOURCE_MEM | IORESOURCE_PREFETCH;

	return IORESOURCE_MEM;
}

void __init pci_read_bases(struct pci_dev *dev, unsigned int howmany, int rom)
{
	unsigned int pos, reg, next;
	u32 l, sz, tmp;
	u16 cmd;
	struct resource *res;

	/* Disable IO and memory while we fiddle */
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	tmp = cmd & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
	pci_write_config_word(dev, PCI_COMMAND, tmp);
	
	for(pos=0; pos<howmany; pos = next) {
		next = pos+1;
		res = &dev->resource[pos];
		res->name = dev->name;
		reg = PCI_BASE_ADDRESS_0 + (pos << 2);
		pci_read_config_dword(dev, reg, &l);
		pci_write_config_dword(dev, reg, ~0);
		pci_read_config_dword(dev, reg, &sz);
		pci_write_config_dword(dev, reg, l);
		if (!sz || sz == 0xffffffff)
			continue;
		if (l == 0xffffffff)
			l = 0;
		if ((l & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_MEMORY) {
			res->start = l & PCI_BASE_ADDRESS_MEM_MASK;
			sz = ~(sz & PCI_BASE_ADDRESS_MEM_MASK);
		} else {
			res->start = l & PCI_BASE_ADDRESS_IO_MASK;
			sz = ~(sz & PCI_BASE_ADDRESS_IO_MASK) & 0xffff;
		}
		res->end = res->start + sz;
		res->flags |= (l & 0xf) | pci_resource_flags(l);
		if ((l & (PCI_BASE_ADDRESS_SPACE | PCI_BASE_ADDRESS_MEM_TYPE_MASK))
		    == (PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64)) {
			pci_read_config_dword(dev, reg+4, &l);
			next++;
#if BITS_PER_LONG == 64
			res->start |= ((unsigned long) l) << 32;
			res->end = res->start + sz;
			pci_write_config_dword(dev, reg+4, ~0);
			pci_read_config_dword(dev, reg+4, &tmp);
			pci_write_config_dword(dev, reg+4, l);
			if (l)
				res->end = res->start + (((unsigned long) ~l) << 32);
#else
			if (l) {
				printk("PCI: Unable to handle 64-bit address for device %s\n", dev->name);
				res->start = 0;
				res->flags = 0;
				continue;
			}
#endif
		}
	}
	if (rom) {
		res = &dev->resource[PCI_ROM_RESOURCE];
		pci_read_config_dword(dev, rom, &l);
		pci_write_config_dword(dev, rom, ~PCI_ROM_ADDRESS_ENABLE);
		pci_read_config_dword(dev, rom, &sz);
		pci_write_config_dword(dev, rom, l);
		if (l == 0xffffffff)
			l = 0;
		if (sz && sz != 0xffffffff) {
			res->flags = (l & PCI_ROM_ADDRESS_ENABLE) |
			  IORESOURCE_MEM | IORESOURCE_PREFETCH | IORESOURCE_READONLY | IORESOURCE_CACHEABLE;
			res->start = l & PCI_ROM_ADDRESS_MASK;
			res->end = res->start + (~(sz & PCI_ROM_ADDRESS_MASK));
		}
		res->name = dev->name;
	}
	pci_write_config_word(dev, PCI_COMMAND, cmd);
}

static unsigned int __init pci_do_scan_bus(struct pci_bus *bus)
{
	unsigned int devfn, l, max, class;
	unsigned char cmd, irq, tmp, hdr_type, is_multi = 0;
	struct pci_dev *dev, **bus_last;
	struct pci_bus *child;
	struct pci_dev *dev_cache = NULL;

	DBG("pci_do_scan_bus for bus %d\n", bus->number);
	bus_last = &bus->devices;
	max = bus->secondary;
	for (devfn = 0; devfn < 0xff; ++devfn) {
		if (PCI_FUNC(devfn) && !is_multi) {
			/* not a multi-function device */
			continue;
		}
		if (!dev_cache) {
			dev_cache = kmalloc(sizeof(*dev), GFP_KERNEL);
			if (!dev_cache)
				continue;
		}
		dev = dev_cache;
		memset(dev, 0, sizeof(*dev));
		dev->bus = bus;
		dev->devfn  = devfn;

		if (pci_read_config_byte(dev, PCI_HEADER_TYPE, &hdr_type))
			continue;
		if (!PCI_FUNC(devfn))
			is_multi = hdr_type & 0x80;

		if (pci_read_config_dword(dev, PCI_VENDOR_ID, &l) ||
		    /* some broken boards return 0 if a slot is empty: */
		    l == 0xffffffff || l == 0x00000000 || l == 0x0000ffff || l == 0xffff0000)
			continue;

		dev_cache = NULL;
		dev->vendor = l & 0xffff;
		dev->device = (l >> 16) & 0xffff;
#ifdef CONFIG_PCI_NO_NAMES
		sprintf(dev->name, "pci:%02x:%02x.%d", bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn));
#else
		pci_name_device(dev);
#endif

		/* non-destructively determine if device can be a master: */
		pci_read_config_byte(dev, PCI_COMMAND, &cmd);
		pci_write_config_byte(dev, PCI_COMMAND, cmd | PCI_COMMAND_MASTER);
		pci_read_config_byte(dev, PCI_COMMAND, &tmp);
		dev->master = ((tmp & PCI_COMMAND_MASTER) != 0);
		pci_write_config_byte(dev, PCI_COMMAND, cmd);

		pci_read_config_dword(dev, PCI_CLASS_REVISION, &class);
		class >>= 8;				    /* upper 3 bytes */
		dev->class = class;
		class >>= 8;
		dev->hdr_type = hdr_type;

		switch (hdr_type & 0x7f) {		    /* header type */
		case PCI_HEADER_TYPE_NORMAL:		    /* standard header */
			if (class == PCI_CLASS_BRIDGE_PCI)
				goto bad;
			/*
			 * If the card generates interrupts, read IRQ number
			 * (some architectures change it during pcibios_fixup())
			 */
			pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &irq);
			if (irq)
				pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
			dev->irq = irq;
			/*
			 * read base address registers, again pcibios_fixup() can
			 * tweak these
			 */
			pci_read_bases(dev, 6, PCI_ROM_ADDRESS);
			pci_read_config_word(dev, PCI_SUBSYSTEM_VENDOR_ID, &dev->subsystem_vendor);
			pci_read_config_word(dev, PCI_SUBSYSTEM_ID, &dev->subsystem_device);
			break;
		case PCI_HEADER_TYPE_BRIDGE:		    /* bridge header */
			if (class != PCI_CLASS_BRIDGE_PCI)
				goto bad;
			pci_read_bases(dev, 2, PCI_ROM_ADDRESS1);
			break;
		case PCI_HEADER_TYPE_CARDBUS:		    /* CardBus bridge header */
			if (class != PCI_CLASS_BRIDGE_CARDBUS)
				goto bad;
			pci_read_bases(dev, 1, 0);
			pci_read_config_word(dev, PCI_CB_SUBSYSTEM_VENDOR_ID, &dev->subsystem_vendor);
			pci_read_config_word(dev, PCI_CB_SUBSYSTEM_ID, &dev->subsystem_device);
			break;
		default:				    /* unknown header */
		bad:
			printk(KERN_ERR "PCI: %02x:%02x [%04x/%04x/%06x] has unknown header type %02x, ignoring.\n",
			       bus->number, dev->devfn, dev->vendor, dev->device, class, hdr_type);
			continue;
		}

		DBG("PCI: %02x:%02x [%04x/%04x]\n", bus->number, dev->devfn, dev->vendor, dev->device);

		/*
		 * Put it into the global PCI device chain. It's used to
		 * find devices once everything is set up.
		 */
		if (!pci_reverse) {
			*pci_last_dev_p = dev;
			pci_last_dev_p = &dev->next;
		} else {
			dev->next = pci_devices;
			pci_devices = dev;
		}

		/*
		 * Now insert it into the list of devices held
		 * by the parent bus.
		 */
		*bus_last = dev;
		bus_last = &dev->sibling;

		/* Fix up broken headers */
		pci_fixup_device(PCI_FIXUP_HEADER, dev);

#if 0
		/*
		 * Setting of latency timer in case it was less than 32 was
		 * a great idea, but it confused several broken devices. Grrr.
		 */
		pci_read_config_byte(dev, PCI_LATENCY_TIMER, &tmp);
		if (tmp < 32)
			pci_write_config_byte(dev, PCI_LATENCY_TIMER, 32);
#endif
	}
	if (dev_cache)
		kfree(dev_cache);

	/*
	 * After performing arch-dependent fixup of the bus, look behind
	 * all PCI-to-PCI bridges on this bus.
	 */
	pcibios_fixup_bus(bus);
	for(dev=bus->devices; dev; dev=dev->sibling)
		/*
		 * If it's a bridge, scan the bus behind it.
		 */
		if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
			unsigned int buses;
			unsigned short cr;

			/*
			 * Insert it into the tree of buses.
			 */
			child = kmalloc(sizeof(*child), GFP_KERNEL);
			memset(child, 0, sizeof(*child));
			child->next = bus->children;
			bus->children = child;
			child->self = dev;
			child->parent = bus;
			child->ops = bus->ops;

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
			pci_read_config_word(dev, PCI_COMMAND, &cr);
			pci_write_config_word(dev, PCI_COMMAND, 0x0000);
			pci_write_config_word(dev, PCI_STATUS, 0xffff);
			/*
			 * Read the existing primary/secondary/subordinate bus
			 * number configuration to determine if the PCI bridge
			 * has already been configured by the system.  If so,
			 * do not modify the configuration, merely note it.
			 */
			pci_read_config_dword(dev, PCI_PRIMARY_BUS, &buses);
			if ((buses & 0xFFFFFF) != 0)
			  {
			    unsigned int cmax;

			    child->primary = buses & 0xFF;
			    child->secondary = (buses >> 8) & 0xFF;
			    child->subordinate = (buses >> 16) & 0xFF;
			    child->number = child->secondary;
			    cmax = pci_do_scan_bus(child);
			    if (cmax > max) max = cmax;
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
			    pci_write_config_dword(dev, PCI_PRIMARY_BUS, buses);
			    /*
			     * Now we can scan all subordinate buses:
			     */
			    max = pci_do_scan_bus(child);
			    /*
			     * Set the subordinate bus number to its real
			     * value:
			     */
			    child->subordinate = max;
			    buses = (buses & 0xff00ffff)
			      | ((unsigned int)(child->subordinate) << 16);
			    pci_write_config_dword(dev, PCI_PRIMARY_BUS, buses);
			  }
			pci_write_config_word(dev, PCI_COMMAND, cr);
		}

	/*
	 * We've scanned the bus and so we know all about what's on
	 * the other side of any bridges that may be on this bus plus
	 * any devices.
	 *
	 * Return how far we've got finding sub-buses.
	 */
	DBG("PCI: pci_do_scan_bus returning with max=%02x\n", max);
	return max;
}

struct pci_bus * __init pci_scan_bus(int bus, struct pci_ops *ops, void *sysdata)
{
	struct pci_bus *b;

	b = kmalloc(sizeof(*b), GFP_KERNEL);
	memset(b, 0, sizeof(*b));
	if (pci_root) {
		b->next = pci_root->next;
		pci_root->next = b;
	} else
		pci_root = b;
	b->number = b->secondary = bus;
	b->sysdata = sysdata;
	b->ops = ops;
	b->subordinate = pci_do_scan_bus(b);
	return b;
}

void __init pci_init(void)
{
	struct pci_dev *dev;

	pcibios_init();

	for(dev=pci_devices; dev; dev=dev->next)
		pci_fixup_device(PCI_FIXUP_FINAL, dev);
}

static int __init pci_setup(char *str)
{
	while (str) {
		char *k = strchr(str, ',');
		if (k)
			*k++ = 0;
		if (*str && (str = pcibios_setup(str)) && *str) {
			if (!strcmp(str, "reverse"))
				pci_reverse = 1;
			else printk(KERN_ERR "PCI: Unknown option `%s'\n", str);
		}
		str = k;
	}
	return 1;
}

__setup("pci=", pci_setup);
