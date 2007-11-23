/*
 * arch/arm/kernel/bios32.c
 *
 * PCI bios-type initialisation for PCI machines
 *
 * Bits taken from various places.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/system.h>

#include "bios32.h"

static int debug_pci;
int have_isa_bridge;

extern void hw_init(void);

void pcibios_report_device_errors(int warn)
{
	struct pci_dev *dev;

	pci_for_each_dev(dev) {
		u16 status;

		pci_read_config_word(dev, PCI_STATUS, &status);

		if ((status & 0xf900) == 0)
			continue;

		pci_write_config_word(dev, PCI_STATUS, status & 0xf900);

		if (warn)
			printk(KERN_DEBUG "PCI: %02X:%02X: status %04X "
			       "on %s\n", dev->bus->number, dev->devfn,
			       status, dev->name);
	}
}

/*
 * We don't use this to fix the device, but initialisation of it.
 * It's not the correct use for this, but it works.  The actions we
 * take are:
 * - enable only IO
 * - set memory region to start at zero
 * - (0x48) enable all memory requests from ISA to be channeled to PCI
 * - (0x42) disable ping-pong (as per errata)
 * - (0x40) enable PCI packet retry
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

	/*
	 * We used to set the arbiter to "park on last master"
	 * (bit 1 set), but unfortunately the CyberPro does not
	 * park the bus.  We must therefore park on CPU.
	 */
	pci_write_config_byte(dev, 0x83, 0x00);

	/*
	 * Rotate priorities of each PCI request
	 */
	pci_write_config_byte(dev, 0x80, 0xe0);
	pci_write_config_byte(dev, 0x81, 0x01);

	/*
	 * Route INTA input to IRQ 11, and set
	 * IRQ11 to be level sensitive.
	 */
	pci_write_config_word(dev, 0x44, 0xb000);
	outb(0x08, 0x4d1);
}

static void __init pci_fixup_unassign(struct pci_dev *dev)
{
	dev->resource[0].end -= dev->resource[0].start;
	dev->resource[0].start = 0;
}

/*
 * Prevent the PCI layer from seeing the resources
 * allocated to this device.  These resources are
 * of no consequence to the PCI layer (they are
 * handled elsewhere).
 */
static void __init pci_fixup_disable(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		dev->resource[i].start = 0;
		dev->resource[i].end   = 0;
		dev->resource[i].flags = 0;
	}
}

/*
 * PCI IDE controllers use non-standard I/O port
 * decoding, respect it.
 */
static void __init pci_fixup_ide_bases(struct pci_dev *dev)
{
	struct resource *r;
	int i;

	if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		r = dev->resource + i;
		if ((r->start & ~0x80) == 0x374) {
			r->start |= 2;
			r->end = r->start;
		}
	}
}

struct pci_fixup pcibios_fixups[] = {
	{
		PCI_FIXUP_HEADER,
		PCI_VENDOR_ID_DEC,	PCI_DEVICE_ID_DEC_21285,
		pci_fixup_disable
	}, {
		PCI_FIXUP_HEADER,
		PCI_VENDOR_ID_WINBOND,	PCI_DEVICE_ID_WINBOND_83C553,
		pci_fixup_83c553
	}, {
		PCI_FIXUP_HEADER,
		PCI_VENDOR_ID_WINBOND2,	PCI_DEVICE_ID_WINBOND2_89C940F,
		pci_fixup_unassign
	}, {
		PCI_FIXUP_HEADER,
		PCI_ANY_ID,		PCI_ANY_ID,
		pci_fixup_ide_bases
	}, { 0 }
};

/*
 * Allocate resources for all PCI devices that have been enabled.
 * We need to do that before we try to fix up anything.
 */
static void __init pcibios_claim_resources(void)
{
	struct pci_dev *dev;
	int idx;

	pci_for_each_dev(dev) {
		for (idx = 0; idx < PCI_NUM_RESOURCES; idx++)
			if (dev->resource[idx].flags &&
			    dev->resource[idx].start)
				pci_claim_resource(dev, idx);
	}
}

void __init
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			struct resource *res, int resource)
{
	unsigned long where, size;
	u32 reg;

	if (debug_pci)
		printk("PCI: Assigning %3s %08lx to %s\n",
			res->flags & IORESOURCE_IO ? "IO" : "MEM",
			res->start, dev->name);

	where = PCI_BASE_ADDRESS_0 + resource * 4;
	size  = res->end - res->start;

	pci_read_config_dword(dev, where, &reg);
	reg = (reg & size) | (((u32)(res->start - root->start)) & ~size);
	pci_write_config_dword(dev, where, reg);
}

void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
	if (debug_pci)
		printk("PCI: Assigning IRQ %02d to %s\n", irq, dev->name);
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

/*
 * Called after each bus is probed, but before its children
 * are examined.
 */
void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	struct list_head *walk = &bus->devices;
	struct arm_pci_sysdata *sysdata =
			(struct arm_pci_sysdata *)bus->sysdata;
	struct arm_bus_sysdata *busdata;

	if (bus->number < MAX_NR_BUS)
		busdata = sysdata->bus + bus->number;
	else
		BUG();

	/*
	 * Walk the devices on this bus, working out what we can
	 * and can't support.
	 */
	for (walk = walk->next; walk != &bus->devices; walk = walk->next) {
		struct pci_dev *dev = pci_dev_b(walk);
		u16 status;

		pci_read_config_word(dev, PCI_STATUS, &status);

		/*
		 * If this device does not support fast back to back
		 * transfers, the bus as a whole cannot support them.
		 */
		if (!(status & PCI_STATUS_FAST_BACK))
			busdata->features &= ~PCI_COMMAND_FAST_BACK;

		/*
		 * Calculate the maximum devsel latency.
		 */
		if (busdata->maxdevsel < (status & PCI_STATUS_DEVSEL_MASK))
			busdata->maxdevsel = (status & PCI_STATUS_DEVSEL_MASK);

		/*
		 * If this device is an ISA bridge, set the have_isa_bridge
		 * flag.  We will then go looking for things like keyboard,
		 * etc
		 */
		if (dev->class >> 8 == PCI_CLASS_BRIDGE_ISA ||
		    dev->class >> 8 == PCI_CLASS_BRIDGE_EISA)
			have_isa_bridge = !0;
	}

	/*
	 * Now walk the devices again, this time setting them up.
	 */
	walk = &bus->devices;
	for (walk = walk->next; walk != &bus->devices; walk = walk->next) {
		struct pci_dev *dev = pci_dev_b(walk);
		u16 cmd;

		/*
		 * architecture specific hacks. I don't really want
		 * this here, but I don't see any other place for it
		 * to live.  Shame the device doesn't support
		 * capabilities
		 */
		if (machine_is_netwinder() &&
		    dev->vendor == PCI_VENDOR_ID_DEC &&
		    dev->device == PCI_DEVICE_ID_DEC_21142)
			/* Put the chip to sleep in case the driver isn't loaded */
			pci_write_config_dword(dev, 0x40, 0x80000000);

		/*
		 * Set latency timer to 32, and a cache line size to 32 bytes.
		 * Also, set system error enable, parity error enable.
		 * Disable ROM.
		 */
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 32);
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 8);
		pci_read_config_word(dev, PCI_COMMAND, &cmd);

		cmd |= busdata->features;

		pci_write_config_word(dev, PCI_COMMAND, cmd);
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, 0);
	}
}

void __init
pcibios_fixup_pbus_ranges(struct pci_bus *bus, struct pbus_set_ranges_data *ranges)
{
	ranges->io_start -= bus->resource[0]->start;
	ranges->io_end -= bus->resource[0]->start;
	ranges->mem_start -= bus->resource[1]->start;
	ranges->mem_end -= bus->resource[1]->start;
}

static u8 __init no_swizzle(struct pci_dev *dev, u8 *pin)
{
	return 0;
}

/* ebsa285 host-specific stuff */

#ifdef CONFIG_ARCH_EBSA285
static int irqmap_ebsa285[] __initdata = { IRQ_IN1, IRQ_IN0, IRQ_PCI, IRQ_IN3 };

static u8 __init ebsa285_swizzle(struct pci_dev *dev, u8 *pin)
{
	return PCI_SLOT(dev->devfn);
}

static int __init ebsa285_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (dev->vendor == PCI_VENDOR_ID_CONTAQ &&
	    dev->device == PCI_DEVICE_ID_CONTAQ_82C693)
		switch (PCI_FUNC(dev->devfn)) {
			case 1:	return 14;
			case 2:	return 15;
			case 3:	return 12;
		}

	return irqmap_ebsa285[(slot + pin) & 3];
}

static struct hw_pci ebsa285_pci __initdata = {
	dc21285_init,
	0x9000,
	0x00100000,
	ebsa285_swizzle,
	ebsa285_map_irq
};
#endif

#ifdef CONFIG_ARCH_CATS
/* cats host-specific stuff */
static int irqmap_cats[] __initdata = { IRQ_PCI, IRQ_IN0, IRQ_IN1, IRQ_IN3 };

static int __init cats_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (dev->irq >= 128)
		return dev->irq & 0x1f;

	if (dev->irq >= 1 && dev->irq <= 4)
		return irqmap_cats[dev->irq - 1];

	if (dev->irq != 0)
		printk("PCI: device %02x:%02x has unknown irq line %x\n",
		       dev->bus->number, dev->devfn, dev->irq);

	return -1;
}

static struct hw_pci cats_pci __initdata = {
	dc21285_init,
	0x9000,
	0x00100000,
	no_swizzle,
	cats_map_irq
};
#endif

#ifdef CONFIG_ARCH_NETWINDER
/* netwinder host-specific stuff */
static int __init netwinder_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
#define DEV(v,d) ((v)<<16|(d))
	switch (DEV(dev->vendor, dev->device)) {
	case DEV(PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_21142):
	case DEV(PCI_VENDOR_ID_NCR, PCI_DEVICE_ID_NCR_53C885):
	case DEV(PCI_VENDOR_ID_NCR, PCI_DEVICE_ID_NCR_YELLOWFIN):
		return IRQ_NETWINDER_ETHER100;

	case DEV(PCI_VENDOR_ID_WINBOND2, 0x5a5a):
		return IRQ_NETWINDER_ETHER10;

	case DEV(PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_83C553):
		return 0;

	case DEV(PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_82C105):
		return IRQ_ISA_HARDDISK1;

	case DEV(PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_2000):
	case DEV(PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_2010):
	case DEV(PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_5000):
		return IRQ_NETWINDER_VGA;

	case DEV(PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_21285):
		return 0;

	default:
		printk(KERN_ERR "PCI: %02X:%02X [%04X:%04X] unknown device\n",
			dev->bus->number, dev->devfn,
			dev->vendor, dev->device);
		return 0;
	}
}

static struct hw_pci netwinder_pci __initdata = {
	dc21285_init,
	0x9000,
	0x00100000,
	no_swizzle,
	netwinder_map_irq
};
#endif

#ifdef CONFIG_ARCH_PERSONAL_SERVER
static int irqmap_personal_server[] __initdata = {
	IRQ_IN0, IRQ_IN1, IRQ_IN2, IRQ_IN3, 0, 0, 0,
	IRQ_DOORBELLHOST, IRQ_DMA1, IRQ_DMA2, IRQ_PCI
};

static int __init personal_server_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	unsigned char line;

	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &line);

	if (line > 0x40 && line <= 0x5f) {
		/* line corresponds to the bit controlling this interrupt
		 * in the footbridge.  Ignore the first 8 interrupt bits,
		 * look up the rest in the map.  IN0 is bit number 8
		 */
		return irqmap_personal_server[(line & 0x1f) - 8];
	} else if (line == 0) {
		/* no interrupt */
		return 0;
	} else
		return irqmap_personal_server[(line - 1) & 3];
}

static struct hw_pci personal_server_pci __initdata = {
	dc21285_init,
	0x9000,
	0x00100000,
	no_swizzle,
	personal_server_map_irq
};

#endif

#ifdef CONFIG_ARCH_NEXUSPCI
/*
 * Owing to a PCB cockup, issue A backplanes are wired thus:
 *
 * Slot 1    2    3    4    5   Bridge
 * IRQ  D    C    B    A    A
 *      A    D    C    B    B
 *      B    A    D    C    C
 *      C    B    A    D    D
 *
 * ID A31  A30  A29  A28  A27   A26
 */

static int irqmap_ftv[] __initdata = { IRQ_PCI_A, IRQ_PCI_B, IRQ_PCI_C, IRQ_PCI_D };

static int __init ftv_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return irqmap_ftv[(slot + pin) & 3];
}

/* ftv host-specific stuff */
static struct hw_pci ftv_pci __initdata = {
	plx90x0_init,
	0x9000,
	0x00100000,
	no_swizzle,
	ftv_map_irq
};
#endif

void __init pcibios_init(void)
{
	struct hw_pci *hw_pci = NULL;

	do {
#ifdef CONFIG_ARCH_EBSA285
		if (machine_is_ebsa285()) {
			hw_pci = &ebsa285_pci;
			break;
		}
#endif
#ifdef CONFIG_ARCH_CATS
		if (machine_is_cats()) {
			hw_pci = &cats_pci;
			break;
		}
#endif
#ifdef CONFIG_ARCH_NETWINDER
		if (machine_is_netwinder()) {
			hw_pci = &netwinder_pci;
			break;
		}
#endif
#ifdef CONFIG_ARCH_PERSONAL_SERVER
		if (machine_is_personal_server()) {
			hw_pci = &personal_server_pci;
			break;
		}
#endif
#ifdef CONFIG_ARCH_NEXUSPCI
		if (machine_is_nexuspci()) {
			hw_pci = &ftv_pci;
			break;
		}
#endif
	} while (0);			

	if (hw_pci == NULL)
		return;

	/*
	 * Set up the host bridge, and scan the bus.
	 */
	hw_pci->init();

	/*
	 * Other architectures don't seem to do this... should we?
	 */
	pcibios_claim_resources();

	/*
	 * Assign any unassigned resources.  Note that we really ought to
	 * have min/max stuff here - max mem address is 0x0fffffff
	 */
	pci_assign_unassigned_resources();
	pci_fixup_irqs(hw_pci->swizzle, hw_pci->map_irq);
	pci_set_bus_ranges();

#ifdef CONFIG_FOOTBRIDGE
	/*
	 * Initialise any other hardware after we've got the PCI bus
	 * initialised.  We may need the PCI bus to talk to this other
	 * hardware.
	 */
	hw_init();
#endif
}

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "debug")) {
		debug_pci = 1;
		return NULL;
	}
	return str;
}

void pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
}

void pcibios_set_master(struct pci_dev *dev)
{
}

int pcibios_enable_device(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx = 0; idx < 6; idx++) {
		r = dev->resource + idx;
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because"
			       " of resource collisions\n", dev->slot_name);
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		printk("PCI: enabling device %s (%04x -> %04x)\n",
		       dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}
