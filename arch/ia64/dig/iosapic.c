/*
 * Streamlined APIC support.
 *
 * Copyright (C) 1999 Intel Corp.
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999-2000 Hewlett-Packard Co.
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999,2000 Walt Drummond <drummond@valinux.com>
 *
 * 00/04/19	D. Mosberger	Rewritten to mirror more closely the x86 I/O APIC code.
 *				In particular, we now have separate handlers for edge
 *				and level triggered interrupts.
 */
#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/string.h>
#include <linux/irq.h>

#include <asm/acpi-ext.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/iosapic.h>
#include <asm/machvec.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/system.h>

#ifdef	CONFIG_ACPI_KERNEL_CONFIG
# include <asm/acpikcfg.h>
#endif

#undef DEBUG_IRQ_ROUTING

static spinlock_t iosapic_lock = SPIN_LOCK_UNLOCKED;

struct iosapic_vector iosapic_vector[NR_IRQS] = {
	[0 ... NR_IRQS-1] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }
};

/*
 * find the IRQ in the IOSAPIC map for the PCI device on bus/slot/pin
 */
int
iosapic_get_PCI_irq_vector (int bus, int slot, int pci_pin)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		if ((iosapic_bustype(i) == BUS_PCI) &&
		    (iosapic_bus(i) == bus) &&
		    (iosapic_busdata(i) == ((slot << 16) | pci_pin))) {
			return i;
		}
	}
	return -1;
}

static void
set_rte (unsigned long iosapic_addr, int entry, int pol, int trigger, int delivery,
	 long dest, int vector)
{
	u32 low32;
	u32 high32;

	low32 = ((pol << IO_SAPIC_POLARITY_SHIFT) |
		 (trigger << IO_SAPIC_TRIGGER_SHIFT) |
		 (delivery << IO_SAPIC_DELIVERY_SHIFT) |
		 vector);

#ifdef CONFIG_IA64_AZUSA_HACKS
	/* set Flush Disable bit */
	if (iosapic_addr != 0xc0000000fec00000)
		low32 |= (1 << 17);
#endif

	/* dest contains both id and eid */
	high32 = (dest << IO_SAPIC_DEST_SHIFT);	

	writel(IO_SAPIC_RTE_HIGH(entry), iosapic_addr + IO_SAPIC_REG_SELECT);
	writel(high32, iosapic_addr + IO_SAPIC_WINDOW);
	writel(IO_SAPIC_RTE_LOW(entry), iosapic_addr + IO_SAPIC_REG_SELECT);
	writel(low32, iosapic_addr + IO_SAPIC_WINDOW);
}

static void
nop (unsigned int irq)
{
	/* do nothing... */
}

static void 
mask_irq (unsigned int irq)
{
	unsigned long flags, iosapic_addr = iosapic_addr(irq);
	u32 low32;

	spin_lock_irqsave(&iosapic_lock, flags);
	{
		writel(IO_SAPIC_RTE_LOW(iosapic_pin(irq)), iosapic_addr + IO_SAPIC_REG_SELECT);
		low32 = readl(iosapic_addr + IO_SAPIC_WINDOW);

		low32 |= (1 << IO_SAPIC_MASK_SHIFT);    /* Zero only the mask bit */
		writel(low32, iosapic_addr + IO_SAPIC_WINDOW);
	}
	spin_unlock_irqrestore(&iosapic_lock, flags);
}

static void 
unmask_irq (unsigned int irq)
{
	unsigned long flags, iosapic_addr = iosapic_addr(irq);
	u32 low32;

	spin_lock_irqsave(&iosapic_lock, flags);
	{
		writel(IO_SAPIC_RTE_LOW(iosapic_pin(irq)), iosapic_addr + IO_SAPIC_REG_SELECT);
		low32 = readl(iosapic_addr + IO_SAPIC_WINDOW);

		low32 &= ~(1 << IO_SAPIC_MASK_SHIFT);    /* Zero only the mask bit */
		writel(low32, iosapic_addr + IO_SAPIC_WINDOW);
	}
	spin_unlock_irqrestore(&iosapic_lock, flags);
}


static void
iosapic_set_affinity (unsigned int irq, unsigned long mask)
{
	printk("iosapic_set_affinity: not implemented yet\n");
}

/*
 * Handlers for level-triggered interrupts.
 */

static unsigned int
iosapic_startup_level_irq (unsigned int irq)
{
	unmask_irq(irq);
	return 0;
}

static void
iosapic_end_level_irq (unsigned int irq)
{
	writel(irq, iosapic_addr(irq) + IO_SAPIC_EOI);
}

#define iosapic_shutdown_level_irq	mask_irq
#define iosapic_enable_level_irq	unmask_irq
#define iosapic_disable_level_irq	mask_irq
#define iosapic_ack_level_irq		nop

struct hw_interrupt_type irq_type_iosapic_level = {
	typename:	"IO-SAPIC-level",
	startup:	iosapic_startup_level_irq,
	shutdown:	iosapic_shutdown_level_irq,
	enable:		iosapic_enable_level_irq,
	disable:	iosapic_disable_level_irq,
	ack:		iosapic_ack_level_irq,
	end:		iosapic_end_level_irq,
	set_affinity:	iosapic_set_affinity
};

/*
 * Handlers for edge-triggered interrupts.
 */

static unsigned int
iosapic_startup_edge_irq (unsigned int irq)
{
	unmask_irq(irq);
	/*
	 * IOSAPIC simply drops interrupts pended while the
	 * corresponding pin was masked, so we can't know if an
	 * interrupt is pending already.  Let's hope not...
	 */
	return 0;
}

static void
iosapic_ack_edge_irq (unsigned int irq)
{
	/*
	 * Once we have recorded IRQ_PENDING already, we can mask the
	 * interrupt for real. This prevents IRQ storms from unhandled
	 * devices.
	 */
	if ((irq_desc[irq].status & (IRQ_PENDING | IRQ_DISABLED)) == (IRQ_PENDING | IRQ_DISABLED))
		mask_irq(irq);
}

#define iosapic_enable_edge_irq		unmask_irq
#define iosapic_disable_edge_irq	nop
#define iosapic_end_edge_irq		nop

struct hw_interrupt_type irq_type_iosapic_edge = {
	typename:	"IO-SAPIC-edge",
	startup:	iosapic_startup_edge_irq,
	shutdown:	iosapic_disable_edge_irq,
	enable:		iosapic_enable_edge_irq,
	disable:	iosapic_disable_edge_irq,
	ack:		iosapic_ack_edge_irq,
	end:		iosapic_end_edge_irq,
	set_affinity:	iosapic_set_affinity
};

unsigned int
iosapic_version (unsigned long base_addr) 
{
	/*
	 * IOSAPIC Version Register return 32 bit structure like:
	 * {
	 *	unsigned int version   : 8;
	 *	unsigned int reserved1 : 8;
	 *	unsigned int pins      : 8;
	 *	unsigned int reserved2 : 8;
	 * }
	 */
	writel(IO_SAPIC_VERSION, base_addr + IO_SAPIC_REG_SELECT);
	return readl(IO_SAPIC_WINDOW + base_addr);
}

void
iosapic_init (unsigned long address, int irqbase)
{
	struct hw_interrupt_type *irq_type;
	struct pci_vector_struct *vectors;
	int i, irq, num_pci_vectors;

	if (irqbase == 0)
		/* 
		 * Map the legacy ISA devices into the IOSAPIC data.
		 * Some of these may get reprogrammed later on with
		 * data from the ACPI Interrupt Source Override table.
		 */
		for (i = 0; i < 16; i++) {
			irq = isa_irq_to_vector(i);
			iosapic_pin(irq) = i; 
			iosapic_bus(irq) = BUS_ISA;
			iosapic_busdata(irq) = 0;
			iosapic_dmode(irq) = IO_SAPIC_LOWEST_PRIORITY;
			iosapic_trigger(irq)  = IO_SAPIC_EDGE;
			iosapic_polarity(irq) = IO_SAPIC_POL_HIGH;
#ifdef DEBUG_IRQ_ROUTING
			printk("ISA: IRQ %02x -> Vector %02x IOSAPIC Pin %d\n",
			       i, irq, iosapic_pin(irq));
#endif
		}

#ifndef CONFIG_IA64_SOFTSDV_HACKS
	/* 
	 * Map the PCI Interrupt data into the ACPI IOSAPIC data using
	 * the info that the bootstrap loader passed to us.
	 */
# ifdef CONFIG_ACPI_KERNEL_CONFIG
	acpi_cf_get_pci_vectors(&vectors, &num_pci_vectors);
# else
	ia64_boot_param.pci_vectors = (__u64) __va(ia64_boot_param.pci_vectors);
	vectors = (struct pci_vector_struct *) ia64_boot_param.pci_vectors;
	num_pci_vectors = ia64_boot_param.num_pci_vectors;
# endif
	for (i = 0; i < num_pci_vectors; i++) {
		irq = vectors[i].irq;
		if (irq < 16)
			irq = isa_irq_to_vector(irq);
		if (iosapic_baseirq(irq) != irqbase)
			continue;

		iosapic_bustype(irq) = BUS_PCI;
		iosapic_pin(irq) = irq - iosapic_baseirq(irq);
		iosapic_bus(irq) = vectors[i].bus;
		/*
		 * Map the PCI slot and pin data into iosapic_busdata()
		 */
		iosapic_busdata(irq) = (vectors[i].pci_id & 0xffff0000) | vectors[i].pin;

		/* Default settings for PCI */
		iosapic_dmode(irq) = IO_SAPIC_LOWEST_PRIORITY;
		iosapic_trigger(irq)  = IO_SAPIC_LEVEL;
		iosapic_polarity(irq) = IO_SAPIC_POL_LOW;

# ifdef DEBUG_IRQ_ROUTING
		printk("PCI: BUS %d Slot %x Pin %x IRQ %02x --> Vector %02x IOSAPIC Pin %d\n", 
		       vectors[i].bus, vectors[i].pci_id>>16, vectors[i].pin, vectors[i].irq, 
		       irq, iosapic_pin(irq));
# endif
	}
#endif /* CONFIG_IA64_SOFTSDV_HACKS */

	for (i = 0; i < NR_IRQS; ++i) {
		if (iosapic_baseirq(i) != irqbase)
			continue;

		if (iosapic_pin(i) != -1) {
			if (iosapic_trigger(i) == IO_SAPIC_LEVEL)
			  irq_type = &irq_type_iosapic_level;
			else
			  irq_type = &irq_type_iosapic_edge;
			if (irq_desc[i].handler != &no_irq_type)
				printk("dig_irq_init: warning: changing vector %d from %s to %s\n",
				       i, irq_desc[i].handler->typename,
				       irq_type->typename);
			irq_desc[i].handler = irq_type;

			/* program the IOSAPIC routing table: */
			set_rte(iosapic_addr(i), iosapic_pin(i), iosapic_polarity(i),
				iosapic_trigger(i), iosapic_dmode(i),
				(ia64_get_lid() >> 16) & 0xffff, i);
		}
	}
}

void
dig_irq_init (void)
{
	/*
	 * Disable the compatibility mode interrupts (8259 style), needs IN/OUT support
	 * enabled.
	 */
	outb(0xff, 0xA1);
	outb(0xff, 0x21);
}

void
dig_pci_fixup (void)
{
	struct	pci_dev	*dev;
	int		irq;
	unsigned char 	pin;

	pci_for_each_dev(dev) {
		pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
		if (pin) {
			pin--;          /* interrupt pins are numbered starting from 1 */
			irq = iosapic_get_PCI_irq_vector(dev->bus->number, PCI_SLOT(dev->devfn),
							 pin);
			if (irq < 0 && dev->bus->parent) { /* go back to the bridge */
				struct pci_dev * bridge = dev->bus->self;

				/* allow for multiple bridges on an adapter */
				do {
					/* do the bridge swizzle... */
					pin = (pin + PCI_SLOT(dev->devfn)) % 4;
					irq = iosapic_get_PCI_irq_vector(bridge->bus->number,
									 PCI_SLOT(bridge->devfn), pin);
				} while (irq < 0 && (bridge = bridge->bus->self));
				if (irq >= 0)
					printk(KERN_WARNING
					       "PCI: using PPB(B%d,I%d,P%d) to get irq %02x\n",
					       bridge->bus->number, PCI_SLOT(bridge->devfn),
					       pin, irq);
				else
					printk(KERN_WARNING
					       "PCI: Couldn't map irq for B%d,I%d,P%d\n",
					       bridge->bus->number, PCI_SLOT(bridge->devfn),
					       pin);
			}
			if (irq >= 0) {
				printk("PCI->APIC IRQ transform: (B%d,I%d,P%d) -> %02x\n",
				       dev->bus->number, PCI_SLOT(dev->devfn), pin, irq);
				dev->irq = irq;
			}
		}
		/*
		 * Nothing to fixup
		 * Fix out-of-range IRQ numbers
		 */
		if (dev->irq >= NR_IRQS)
			dev->irq = 15;	/* Spurious interrupts */
	}
}

/*
 * Register an IOSAPIC discovered via ACPI.
 */
void __init
dig_register_iosapic (acpi_entry_iosapic_t *iosapic)
{
	unsigned int ver, v;
	int l, max_pin;

	ver = iosapic_version((unsigned long) ioremap(iosapic->address, 0));
	max_pin = (ver >> 16) & 0xff;
	
	printk("IOSAPIC Version %x.%x: address 0x%lx IRQs 0x%x - 0x%x\n", 
	       (ver & 0xf0) >> 4, (ver & 0x0f), iosapic->address, 
	       iosapic->irq_base, iosapic->irq_base + max_pin);
	
	for (l = 0; l <= max_pin; l++) {
		v = iosapic->irq_base + l;
		if (v < 16)
			v = isa_irq_to_vector(v);
		if (v > IA64_MAX_VECTORED_IRQ) {
			printk("    !!! bad IOSAPIC interrupt vector: %u\n", v);
			continue;
		}
		/* XXX Check for IOSAPIC collisions */
		iosapic_addr(v) = (unsigned long) ioremap(iosapic->address, 0);
		iosapic_baseirq(v) = iosapic->irq_base;
	}
	iosapic_init(iosapic->address, iosapic->irq_base);
}
