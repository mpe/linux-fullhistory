/*
 * Streamlined APIC support.
 *
 * Copyright (C) 1999 Intel Corp.
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999-2000 Hewlett-Packard Co.
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999,2000 Walt Drummond <drummond@valinux.com>
 */
#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/string.h>

#include <asm/io.h>
#include <asm/iosapic.h>
#include <asm/irq.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/delay.h>
#include <asm/processor.h>

#undef DEBUG_IRQ_ROUTING

/*
 * IRQ vectors 0..15 are treated as the legacy interrupts of the PC-AT
 * platform.  No new drivers should ever ask for specific irqs, but we
 * provide compatibility here in case there is an old driver that does
 * ask for specific irqs (serial, keyboard, stuff like that).  Since
 * IA-64 doesn't allow irq 0..15 to be used for external interrupts
 * anyhow, this in no way prevents us from doing the Right Thing
 * with new drivers.
 */
struct iosapic_vector iosapic_vector[NR_IRQS] = {
	[0 ... NR_IRQS-1] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }
};

#ifndef CONFIG_IA64_IRQ_ACPI
/*
 * Defines the default interrupt routing information for the LION platform
 * XXX - this information should be obtained from the ACPI and hardcoded since
 * we do not have ACPI AML support.
 */

struct intr_routing_entry intr_routing[] = {
      {0,0,0,2,0,0,0,0},
      {0,0,1,1,0,0,0,0},
      {0,0,2,0xff,0,0,0,0},
      {0,0,3,3,0,0,0,0},
      {0,0,4,4,0,0,0,0},
      {0,0,5,5,0,0,0,0},
      {0,0,6,6,0,0,0,0},
      {0,0,7,7,0,0,0,0},
      {0,0,8,8,0,0,0,0},
      {0,0,9,9,0,0,0,0},
      {0,0,10,10,0,0,0,0},
      {0,0,11,11,0,0,0,0},
      {0,0,12,12,0,0,0,0},
      {0,0,13,13,0,0,0,0},
      {0,0,14,14,0,0,0,0},
      {0,0,15,15,0,0,0,0},
#ifdef CONFIG_IA64_LION_HACKS
      {1, 0, 0x04, 16, 0, 0, 1, 1},	/* bus 0, device id 1, INTA */
      {1, 0, 0x05, 26, 0, 0, 1, 1},	/* bus 0, device id 1, INTB */
      {1, 0, 0x06, 36, 0, 0, 1, 1},	/* bus 0, device id 1, INTC */
      {1, 0, 0x07, 42, 0, 0, 1, 1},	/* bus 0, device id 1, INTD */

      {1, 0, 0x08, 17, 0, 0, 1, 1},	/* bus 0, device id 2, INTA */
      {1, 0, 0x09, 27, 0, 0, 1, 1},	/* bus 0, device id 2, INTB */
      {1, 0, 0x0a, 37, 0, 0, 1, 1},	/* bus 0, device id 2, INTC */
      {1, 0, 0x0b, 42, 0, 0, 1, 1},	/* bus 0, device id 2, INTD */

      {1, 0, 0x0f, 50, 0, 0, 1, 1},	/* bus 0, device id 3, INTD */

      {1, 0, 0x14, 51, 0, 0, 1, 1},	/* bus 0, device id 5, INTA */

      {1, 0, 0x18, 49, 0, 0, 1, 1},	/* bus 0, device id 6, INTA */

      {1, 1, 0x04, 18, 0, 0, 1, 1},	/* bus 1, device id 1, INTA */
      {1, 1, 0x05, 28, 0, 0, 1, 1},	/* bus 1, device id 1, INTB */
      {1, 1, 0x06, 38, 0, 0, 1, 1},	/* bus 1, device id 1, INTC */
      {1, 1, 0x07, 43, 0, 0, 1, 1},	/* bus 1, device id 1, INTD */

      {1, 1, 0x08, 48, 0, 0, 1, 1},	/* bus 1, device id 2, INTA */

      {1, 1, 0x0c, 19, 0, 0, 1, 1},	/* bus 1, device id 3, INTA */
      {1, 1, 0x0d, 29, 0, 0, 1, 1},	/* bus 1, device id 3, INTB */
      {1, 1, 0x0e, 38, 0, 0, 1, 1},	/* bus 1, device id 3, INTC */
      {1, 1, 0x0f, 44, 0, 0, 1, 1},	/* bus 1, device id 3, INTD */

      {1, 1, 0x10, 20, 0, 0, 1, 1},	/* bus 1, device id 4, INTA */
      {1, 1, 0x11, 30, 0, 0, 1, 1},	/* bus 1, device id 4, INTB */
      {1, 1, 0x12, 39, 0, 0, 1, 1},	/* bus 1, device id 4, INTC */
      {1, 1, 0x13, 45, 0, 0, 1, 1},	/* bus 1, device id 4, INTD */

      {1, 2, 0x04, 21, 0, 0, 1, 1},	/* bus 2, device id 1, INTA */
      {1, 2, 0x05, 31, 0, 0, 1, 1},	/* bus 2, device id 1, INTB */
      {1, 2, 0x06, 39, 0, 0, 1, 1},	/* bus 2, device id 1, INTC */
      {1, 2, 0x07, 45, 0, 0, 1, 1},	/* bus 2, device id 1, INTD */

      {1, 2, 0x08, 22, 0, 0, 1, 1},	/* bus 2, device id 2, INTA */
      {1, 2, 0x09, 32, 0, 0, 1, 1},	/* bus 2, device id 2, INTB */
      {1, 2, 0x0a, 40, 0, 0, 1, 1},	/* bus 2, device id 2, INTC */
      {1, 2, 0x0b, 46, 0, 0, 1, 1},	/* bus 2, device id 2, INTD */

      {1, 2, 0x0c, 23, 0, 0, 1, 1},	/* bus 2, device id 3, INTA */
      {1, 2, 0x0d, 33, 0, 0, 1, 1},	/* bus 2, device id 3, INTB */
      {1, 2, 0x0e, 40, 0, 0, 1, 1},	/* bus 2, device id 3, INTC */
      {1, 2, 0x0f, 46, 0, 0, 1, 1},	/* bus 2, device id 3, INTD */

      {1, 3, 0x04, 24, 0, 0, 1, 1},	/* bus 3, device id 1, INTA */
      {1, 3, 0x05, 34, 0, 0, 1, 1},	/* bus 3, device id 1, INTB */
      {1, 3, 0x06, 41, 0, 0, 1, 1},	/* bus 3, device id 1, INTC */
      {1, 3, 0x07, 47, 0, 0, 1, 1},	/* bus 3, device id 1, INTD */

      {1, 3, 0x08, 25, 0, 0, 1, 1},	/* bus 3, device id 2, INTA */
      {1, 3, 0x09, 35, 0, 0, 1, 1},	/* bus 3, device id 2, INTB */
      {1, 3, 0x0a, 41, 0, 0, 1, 1},	/* bus 3, device id 2, INTC */
      {1, 3, 0x0b, 47, 0, 0, 1, 1},	/* bus 3, device id 2, INTD */
#else
      /*
       * BigSur platform, bus 0, device 1,2,4 and bus 1 device 0-3
       */
      {1,1,0x0,19,0,0,1,1},		/* bus 1, device id 0, INTA */
      {1,1,0x1,18,0,0,1,1},		/* bus 1, device id 0, INTB */
      {1,1,0x2,17,0,0,1,1},		/* bus 1, device id 0, INTC */
      {1,1,0x3,16,0,0,1,1},		/* bus 1, device id 0, INTD */

      {1,1,0x4,23,0,0,1,1},		/* bus 1, device id 1, INTA */
      {1,1,0x5,22,0,0,1,1},		/* bus 1, device id 1, INTB */
      {1,1,0x6,21,0,0,1,1},		/* bus 1, device id 1, INTC */
      {1,1,0x7,20,0,0,1,1},		/* bus 1, device id 1, INTD */

      {1,1,0x8,27,0,0,1,1},		/* bus 1, device id 2, INTA */
      {1,1,0x9,26,0,0,1,1},		/* bus 1, device id 2, INTB */
      {1,1,0xa,25,0,0,1,1},		/* bus 1, device id 2, INTC */
      {1,1,0xb,24,0,0,1,1},		/* bus 1, device id 2, INTD */

      {1,1,0xc,31,0,0,1,1},		/* bus 1, device id 3, INTA */
      {1,1,0xd,30,0,0,1,1},		/* bus 1, device id 3, INTB */
      {1,1,0xe,29,0,0,1,1},		/* bus 1, device id 3, INTC */
      {1,1,0xf,28,0,0,1,1},		/* bus 1, device id 3, INTD */

      {1,0,0x4,35,0,0,1,1},		/* bus 0, device id 1, INTA */
      {1,0,0x5,34,0,0,1,1},		/* bus 0, device id 1, INTB */
      {1,0,0x6,33,0,0,1,1},		/* bus 0, device id 1, INTC */
      {1,0,0x7,32,0,0,1,1},		/* bus 0, device id 1, INTD */

      {1,0,0x8,39,0,0,1,1},		/* bus 0, device id 2, INTA */
      {1,0,0x9,38,0,0,1,1},		/* bus 0, device id 2, INTB */
      {1,0,0xa,37,0,0,1,1},		/* bus 0, device id 2, INTC */
      {1,0,0xb,36,0,0,1,1},		/* bus 0, device id 2, INTD */

      {1,0,0x10,43,0,0,1,1},		/* bus 0, device id 4, INTA */
      {1,0,0x11,42,0,0,1,1},		/* bus 0, device id 4, INTB */
      {1,0,0x12,41,0,0,1,1},		/* bus 0, device id 4, INTC */
      {1,0,0x13,40,0,0,1,1},		/* bus 0, device id 4, INTD */

      {1,0,0x14,17,0,0,1,1},		/* bus 0, device id 5, INTA */
      {1,0,0x18,18,0,0,1,1},		/* bus 0, device id 6, INTA */
      {1,0,0x1c,19,0,0,1,1},		/* bus 0, device id 7, INTA */
#endif
      {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
};

int
iosapic_get_PCI_irq_vector(int bus, int slot, int pci_pin)
{
	int     i = -1;

	while (intr_routing[++i].srcbus != 0xff) {
		if (intr_routing[i].srcbus == BUS_PCI) {
			if ((intr_routing[i].srcbusirq == ((slot << 2) | pci_pin)) 
			    && (intr_routing[i].srcbusno == bus)) {
				return(intr_routing[i].iosapic_pin);
			}
		}
	}
	return -1;
}

#else /* CONFIG_IA64_IRQ_ACPI */

/*
 * find the IRQ in the IOSAPIC map for the PCI device on bus/slot/pin
 */
int
iosapic_get_PCI_irq_vector(int bus, int slot, int pci_pin)
{
	int	i;

	for (i = 0; i < NR_IRQS; i++) {
		if ((iosapic_bustype(i) == BUS_PCI) &&
		    (iosapic_bus(i) == bus) &&
		    (iosapic_busdata(i) == ((slot << 16) | pci_pin))) {
			return i;
		}
	}

	return -1;
}
#endif /* !CONFIG_IA64_IRQ_ACPI */

static void
set_rte (unsigned long iosapic_addr, int entry, int pol, int trigger, int delivery,
	 long dest, int vector)
{
	int low32;
	int high32;

	low32 = ((pol << IO_SAPIC_POLARITY_SHIFT) |
		 (trigger << IO_SAPIC_TRIGGER_SHIFT) |
		 (delivery << IO_SAPIC_DELIVERY_SHIFT) |
		 vector);

	/* dest contains both id and eid */
	high32 = (dest << IO_SAPIC_DEST_SHIFT);	

	/*
	 * program the rte 
	 */
	writel(IO_SAPIC_RTE_HIGH(entry), iosapic_addr + IO_SAPIC_REG_SELECT);
	writel(high32, iosapic_addr + IO_SAPIC_WINDOW);
	writel(IO_SAPIC_RTE_LOW(entry), iosapic_addr + IO_SAPIC_REG_SELECT);
	writel(low32, iosapic_addr + IO_SAPIC_WINDOW);
}


static void 
enable_pin (unsigned int pin, unsigned long iosapic_addr)
{
        int low32;

        writel(IO_SAPIC_RTE_LOW(pin), iosapic_addr + IO_SAPIC_REG_SELECT);
        low32 = readl(iosapic_addr + IO_SAPIC_WINDOW);

        low32 &= ~(1 << IO_SAPIC_MASK_SHIFT);    /* Zero only the mask bit */
        writel(low32, iosapic_addr + IO_SAPIC_WINDOW);
}


static void 
disable_pin (unsigned int pin, unsigned long iosapic_addr)
{
        int low32;

        writel(IO_SAPIC_RTE_LOW(pin), iosapic_addr + IO_SAPIC_REG_SELECT);
        low32 = readl(iosapic_addr + IO_SAPIC_WINDOW);

        low32 |= (1 << IO_SAPIC_MASK_SHIFT);     /* Set only the mask bit */
        writel(low32, iosapic_addr + IO_SAPIC_WINDOW);
}

#define iosapic_shutdown_irq	iosapic_disable_irq

static void
iosapic_enable_irq (unsigned int irq)
{
	int pin = iosapic_pin(irq);

	if (pin < 0)
		/* happens during irq auto probing... */
		return;
	enable_pin(pin, iosapic_addr(irq));
}

static void
iosapic_disable_irq (unsigned int irq)
{
	int pin = iosapic_pin(irq);

	if (pin < 0)
		return;
	disable_pin(pin, iosapic_addr(irq));
}

unsigned int
iosapic_version(unsigned long base_addr) 
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

static int
iosapic_handle_irq (unsigned int irq, struct pt_regs *regs)
{
	struct irqaction *action = 0;
	struct irq_desc *id = irq_desc + irq;
	unsigned int status;
	int retval;

	spin_lock(&irq_controller_lock);
	{
		status = id->status;

		/* do we need to do something IOSAPIC-specific to ACK the irq here??? */
		/* Yes, but only level-triggered interrupts. We'll do that later */
		if ((status & IRQ_INPROGRESS) == 0 && (status & IRQ_ENABLED) != 0) {
			action = id->action;
			status |= IRQ_INPROGRESS;
		}
		id->status = status & ~(IRQ_REPLAY | IRQ_WAITING);
	}
	spin_unlock(&irq_controller_lock);

	if (!action) {
		if (!(id->status & IRQ_AUTODETECT))
			printk("iosapic_handle_irq: unexpected interrupt %u;"
			       "disabling it (status=%x)\n", irq, id->status);
		/*
		 * If we don't have a handler, disable the pin so we
		 * won't get any further interrupts (until
		 * re-enabled).  --davidm 99/12/17
		 */
		iosapic_disable_irq(irq);
		return 0;
	}

	retval = invoke_irq_handlers (irq, regs, action);

	if (iosapic_trigger(irq) == IO_SAPIC_LEVEL)	/* ACK Level trigger interrupts */
		writel(irq, iosapic_addr(irq) + IO_SAPIC_EOI);

	spin_lock(&irq_controller_lock);
	{
		status = (id->status & ~IRQ_INPROGRESS);
		id->status = status;
	}
	spin_unlock(&irq_controller_lock);

	return retval;
}

void __init
iosapic_init (unsigned long addr)
{
	int	i;
#ifdef CONFIG_IA64_IRQ_ACPI
	struct pci_vector_struct *vectors;
	int     irq;
#else 
	int     vector;
#endif

	/*
	 * Disable all local interrupts
	 */

	ia64_set_itv(0, 1);
	ia64_set_lrr0(0, 1);	
	ia64_set_lrr1(0, 1);	

	/*
	 * Disable the compatibility mode interrupts (8259 style), needs IN/OUT support
	 * enabled.
	 */

	outb(0xff, 0xA1);
	outb(0xff, 0x21);

#if defined(CONFIG_IA64_SOFTSDV_HACKS)
	memset(iosapic_vector, 0x0, sizeof(iosapic_vector));
	for (i = 0; i < NR_IRQS; i++) {
		iosapic_pin(i) = 0xff;
		iosapic_addr(i) = (unsigned long) ioremap(IO_SAPIC_DEFAULT_ADDR, 0);
	}
	/* XXX this should come from systab or some such: */
	iosapic_pin(TIMER_IRQ) = 5;	/* System Clock Interrupt */
	iosapic_pin(0x40) = 3;	/* Keyboard */
	iosapic_pin(0x92) = 9;	/* COM1 Serial Port */
	iosapic_pin(0x80) = 4;	/* Periodic Interrupt */
	iosapic_pin(0xc0) = 2;	/* Mouse */
	iosapic_pin(0xe0) = 1;	/* IDE Disk */
	iosapic_pin(0xf0) = 6;	/* E-IDE CDROM */
	iosapic_pin(0xa0) = 10;	/* Real PCI Interrupt */
#elif !defined(CONFIG_IA64_IRQ_ACPI)
	/*
	 * For systems where the routing info in ACPI is
	 * unavailable/wrong, use the intr_routing information to
	 * initialize the iosapic array
	 */
	i = -1;
	while (intr_routing[++i].srcbus != 0xff) {
		if (intr_routing[i].srcbus == BUS_ISA) {
			vector = map_legacy_irq(intr_routing[i].srcbusirq);
		} else if (intr_routing[i].srcbus == BUS_PCI) {
			vector = intr_routing[i].iosapic_pin;
		} else {
			printk("unknown bus type %d for intr_routing[%d]\n",
                               intr_routing[i].srcbus, i);
			continue;
		}
		iosapic_pin(vector) = intr_routing[i].iosapic_pin;
		iosapic_dmode(vector) = intr_routing[i].mode;
		iosapic_polarity(vector) = intr_routing[i].polarity;
		iosapic_trigger(vector) = intr_routing[i].trigger;
# ifdef DEBUG_IRQ_ROUTING
		printk("irq[0x%x(0x%x)]:0x%x, %d, %d, %d\n", vector, intr_routing[i].srcbusirq,
		       iosapic_pin(vector), iosapic_dmode(vector), iosapic_polarity(vector),
		       iosapic_trigger(vector));
# endif
	}
#else /* !defined(CONFIG_IA64_SOFTSDV_HACKS) && !defined(CONFIG_IA64_IRQ_ACPI) */
	/* 
	 * Map the legacy ISA devices into the IOAPIC data; We'll override these
	 * later with data from the ACPI Interrupt Source Override table.
	 *
	 * Huh, the Lion w/ FPSWA firmware has entries for _all_ of the legacy IRQs, 
	 * including those that are not different from PC/AT standard.  I don't know
	 * if this is a bug in the other firmware or not.  I'm going to leave this code 
	 * here, so that this works on BigSur but will go ask Intel. --wfd 2000-Jan-19
	 *
	 */
	for (i =0 ; i < IA64_MIN_VECTORED_IRQ; i++) {
		irq = map_legacy_irq(i);
		iosapic_pin(irq) = i; 
		iosapic_bus(irq) = BUS_ISA;
		iosapic_busdata(irq) = 0;
		iosapic_dmode(irq) = IO_SAPIC_LOWEST_PRIORITY;
		iosapic_trigger(irq)  = IO_SAPIC_EDGE;
		iosapic_polarity(irq) = IO_SAPIC_POL_HIGH;
#ifdef DEBUG_IRQ_ROUTING
		printk("ISA: IRQ %02x -> Vector %02x IOSAPIC Pin %d\n", i, irq, iosapic_pin(irq));
#endif
	}

	/* 
	 * Map the PCI Interrupt data into the ACPI IOSAPIC data using
	 * the info that the bootstrap loader passed to us.
	 */
	ia64_boot_param.pci_vectors = (__u64) __va(ia64_boot_param.pci_vectors);
	vectors = (struct pci_vector_struct *) ia64_boot_param.pci_vectors;
	for (i = 0; i < ia64_boot_param.num_pci_vectors; i++) {
		irq = map_legacy_irq(vectors[i].irq);

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

#ifdef DEBUG_IRQ_ROUTING
		printk("PCI: BUS %d Slot %x Pin %x IRQ %02x --> Vector %02x IOSAPIC Pin %d\n", 
		       vectors[i].bus, vectors[i].pci_id>>16, vectors[i].pin, vectors[i].irq, 
		       irq, iosapic_pin(irq));
#endif
	}
#endif /* !CONFIG_IA64_IRQ_ACPI */
}

static void
iosapic_startup_irq (unsigned int irq)
{
	int pin;

	if (irq == TIMER_IRQ)
		return;
	pin = iosapic_pin(irq);
	if (pin < 0)
		/* happens during irq auto probing... */
		return;
	set_rte(iosapic_addr(irq), pin, iosapic_polarity(irq), iosapic_trigger(irq), 
		iosapic_dmode(irq), (ia64_get_lid() >> 16) & 0xffff, irq);
	enable_pin(pin, iosapic_addr(irq));
}

struct hw_interrupt_type irq_type_iosapic = {
	"IOSAPIC",
	iosapic_init,
	iosapic_startup_irq,
	iosapic_shutdown_irq,
	iosapic_handle_irq,
	iosapic_enable_irq,
	iosapic_disable_irq
};

void
dig_irq_init (struct irq_desc desc[NR_IRQS])
{
	int i;

	/*
	 * Claim all non-legacy irq vectors as ours unless they're
	 * claimed by someone else already (e.g., timer or IPI are
	 * handled internally).
	 */
	for (i = IA64_MIN_VECTORED_IRQ; i <= IA64_MAX_VECTORED_IRQ; ++i) {
		if (irq_desc[i].handler == &irq_type_default)
			irq_desc[i].handler = &irq_type_iosapic;
	}
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

				/* do the bridge swizzle... */
				pin = (pin + PCI_SLOT(dev->devfn)) % 4;
				irq = iosapic_get_PCI_irq_vector(bridge->bus->number,
								 PCI_SLOT(bridge->devfn), pin);
				if (irq >= 0)
					printk(KERN_WARNING
					       "PCI: using PPB(B%d,I%d,P%d) to get irq %02x\n",
					       bridge->bus->number, PCI_SLOT(bridge->devfn),
					       pin, irq);
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
