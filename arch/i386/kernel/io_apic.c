/*
 *	Intel IO-APIC support for multi-pentium hosts.
 *
 *	(c) 1997 Ingo Molnar, Hajnalka Szabo
 *
 *	Many thanks to Stig Venaas for trying out countless experimental
 *	patches and reporting/debugging problems patiently!
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <asm/i82489.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/io.h>

#include "irq.h"

#define IO_APIC_BASE 0xfec00000

/*
 * volatile is justified in this case, it might change
 * spontaneously, GCC should not cache it
 */
volatile unsigned int * io_apic_reg = NULL;

/*
 * The structure of the IO-APIC:
 */
struct IO_APIC_reg_00 {
	__u32	__reserved_2	: 24,
		ID		:  4,
		__reserved_1	:  4;
} __attribute__ ((packed));

struct IO_APIC_reg_01 {
	__u32	version		:  8,
		__reserved_2	:  8,
		entries		:  8,
		__reserved_1	:  8;
} __attribute__ ((packed));

struct IO_APIC_reg_02 {
	__u32	__reserved_2	: 24,
		arbitration	:  4,
		__reserved_1	:  4;
} __attribute__ ((packed));

struct IO_APIC_route_entry {
	__u32	vector		:  8,
		delivery_mode	:  3,	/* 000: FIXED
					 * 001: lowest prio
					 * 111: ExtInt
					 */
		dest_mode	:  1,	/* 0: physical, 1: logical */
		delivery_status	:  1,
		polarity	:  1,
		irr		:  1,
		trigger		:  1,	/* 0: edge, 1: level */
		mask		:  1,	/* 0: enabled, 1: disabled */
		__reserved_2	: 15;

	union {		struct { __u32
					__reserved_1	: 24,
					physical_dest	:  4,
					__reserved_2	:  4;
			} physical;

			struct { __u32
					__reserved_1	: 24,
					logical_dest	:  8;
			} logical;
	} dest;

} __attribute__ ((packed));

#define UNEXPECTED_IO_APIC()						\
	{								\
		printk(" WARNING: unexpected IO-APIC, please mail\n");	\
		printk("          to linux-smp@vger.rutgers.edu\n");	\
	}

int nr_ioapic_registers = 0;			/* # of IRQ routing registers */
int mp_irq_entries = 0;				/* # of MP IRQ source entries */
struct mpc_config_intsrc mp_irqs[MAX_IRQ_SOURCES];
						/* MP IRQ source entries */

unsigned int io_apic_read (unsigned int reg)
{
	*io_apic_reg = reg;
	return *(io_apic_reg+4);
}

void io_apic_write (unsigned int reg, unsigned int value)
{
	*io_apic_reg = reg;
	*(io_apic_reg+4) = value;
}

void enable_IO_APIC_irq (int irq)
{
	struct IO_APIC_route_entry entry;

	/*
	 * Enable it in the IO-APIC irq-routing table:
	 */
	*(((int *)&entry)+0) = io_apic_read(0x10+irq*2);
	entry.mask = 0;
	io_apic_write(0x10+2*irq, *(((int *)&entry)+0));
}

void disable_IO_APIC_irq (int irq)
{
	struct IO_APIC_route_entry entry;

	/*
	 * Disable it in the IO-APIC irq-routing table:
	 */
	*(((int *)&entry)+0) = io_apic_read(0x10+irq*2);
	entry.mask = 1;
	io_apic_write(0x10+2*irq, *(((int *)&entry)+0));
}

void clear_IO_APIC_irq (int irq)
{
	struct IO_APIC_route_entry entry;

	/*
	 * Disable it in the IO-APIC irq-routing table:
	 */
	memset(&entry, 0, sizeof(entry));
	entry.mask = 1;
	io_apic_write(0x10+2*irq, *(((int *)&entry)+0));
	io_apic_write(0x11+2*irq, *(((int *)&entry)+1));
}

/*
 * support for broken MP BIOSes, enables hand-redirection of PIRQ0-3 to
 * specific CPU-side IRQs.
 */

#define MAX_PIRQS 4
int pirq_entries [MAX_PIRQS];

void ioapic_pirq_setup(char *str, int *ints)
{
	int i, max;

	for (i=0; i<MAX_PIRQS; i++)
		pirq_entries[i]=-1;

	if (!ints)
		printk("PIRQ redirection SETUP, trusting MP-BIOS.\n");
	else {
		printk("PIRQ redirection SETUP, working around broken MP-BIOS.\n");
		max = MAX_PIRQS;
		if (ints[0] < MAX_PIRQS)
			max = ints[0];

		for (i=0; i < max; i++) {
			printk("... PIRQ%d -> IRQ %d\n", i, ints[i+1]);
			/*
			 * PIRQs are mapped upside down, usually.
			 */
			pirq_entries[MAX_PIRQS-i-1]=ints[i+1];
		}
	}
}

int find_irq_entry(int pin)
{
	int i;

	for (i=mp_irq_entries-1; i>=0; i--) {
		if (mp_irqs[i].mpc_dstirq == pin)
			return i;
	}
	return -1;
}

void setup_IO_APIC_irqs (void)
{
	struct IO_APIC_route_entry entry;
	int i, idx, bus, irq, first_notcon=1;

	printk("init IO_APIC IRQs\n");

	for (i=0; i<nr_ioapic_registers; i++) {

		/*
		 * add it to the IO-APIC irq-routing table:
		 */
		memset(&entry,0,sizeof(entry));

		entry.delivery_mode = 1;		/* lowest prio */
		entry.dest_mode = 1;			/* logical delivery */
		entry.mask = 0;				/* enable IRQ */
		entry.dest.logical.logical_dest = 0xff;	/* all CPUs */

		idx = find_irq_entry(i);
		if (idx == -1) {
			if (first_notcon) {
				printk(" IO-APIC pin %d", i);
				first_notcon=0;
			} else
				printk(", %d", i);
			continue;
		}
		bus = mp_irqs[idx].mpc_srcbus;

		switch (mp_bus_id_to_type[bus])
		{
			case MP_BUS_ISA: /* ISA pin */
			{
				irq = mp_irqs[idx].mpc_srcbusirq;
				break;
			}
			case MP_BUS_PCI: /* PCI pin */
			{
				irq = mp_irqs[idx].mpc_srcbusirq >> 2;
				if (irq>=16)
					printk("WARNING: MP BIOS says PIRQ%d is redirected to %d, suspicious.\n",idx-16, irq); 
				break;
			}
			default:
			{
				printk("unknown bus type %d.\n",bus); 
				irq = 0;
				break;
			}
		}

		/*
		 * PCI IRQ redirection. Yes, limits are hardcoded.
		 */
		if ((i>=16) && (i<=19)) {
			if (pirq_entries[i-16] != -1) {
				if (!pirq_entries[i-16]) {
					printk("disabling PIRQ%d\n", i-16);
				} else {
					irq = pirq_entries[i-16];
					printk("using PIRQ%d -> IRQ %d\n",
							i-16, irq);
				}
			}
		}

		if (!IO_APIC_IRQ(irq))
			continue;

		entry.vector = IO_APIC_GATE_OFFSET + (irq<<3);

		/*
		 * Determine IRQ line polarity (high active or low active):
		 */
		switch (mp_irqs[idx].mpc_irqflag & 3)
		{
			case 0: /* conforms, ie. bus-type dependent polarity */
			{
				switch (mp_bus_id_to_type[bus])
				{
					case MP_BUS_ISA: /* ISA pin */
					{
						entry.polarity = 0;
						break;
					}
					case MP_BUS_PCI: /* PCI pin */
					{
						entry.polarity = 1;
						break;
					}
					default:
					{
						printk("broken BIOS!!\n");
						break;
					}
				}
				break;
			}
			case 1: /* high active */
			{
				entry.polarity = 0;
				break;
			}
			case 2: /* reserved */
			{
				printk("broken BIOS!!\n");
				break;
			}
			case 3: /* low active */
			{
				entry.polarity = 1;
				break;
			}
		}

		/*
		 * Determine IRQ trigger mode (edge or level sensitive):
		 */
		switch ((mp_irqs[idx].mpc_irqflag>>2) & 3)
		{
			case 0: /* conforms, ie. bus-type dependent */
			{
				switch (mp_bus_id_to_type[bus])
				{
					case MP_BUS_ISA: /* ISA pin, edge */
					{
						entry.trigger = 0;
						break;
					}
					case MP_BUS_PCI: /* PCI pin, level */
					{
						entry.trigger = 1;
						break;
					}
					default:
					{
						printk("broken BIOS!!\n");
						break;
					}
				}
				break;
			}
			case 1: /* edge */
			{
				entry.trigger = 0;
				break;
			}
			case 2: /* reserved */
			{
				printk("broken BIOS!!\n");
				break;
			}
			case 3: /* level */
			{
				entry.trigger = 1;
				break;
			}
		}

		io_apic_write(0x10+2*i, *(((int *)&entry)+0));
		io_apic_write(0x11+2*i, *(((int *)&entry)+1));
	}

	if (!first_notcon)
		printk(" not connected.\n");
}

void setup_IO_APIC_irq_ISA_default (int irq)
{
	struct IO_APIC_route_entry entry;

	/*
	 * add it to the IO-APIC irq-routing table:
	 */
	memset(&entry,0,sizeof(entry));

	entry.delivery_mode = 1;			/* lowest prio */
	entry.dest_mode = 1;				/* logical delivery */
	entry.mask = 1;					/* unmask IRQ now */
	entry.dest.logical.logical_dest = 0xff;		/* all CPUs */

	entry.vector = IO_APIC_GATE_OFFSET + (irq<<3);

	entry.polarity=0;
	entry.trigger=0;

	io_apic_write(0x10+2*irq, *(((int *)&entry)+0));
	io_apic_write(0x11+2*irq, *(((int *)&entry)+1));
}

void setup_IO_APIC_irq (int irq)
{
}

void print_IO_APIC (void)
{
	int i;
	struct IO_APIC_reg_00 reg_00;
	struct IO_APIC_reg_01 reg_01;
	struct IO_APIC_reg_02 reg_02;

	*(int *)&reg_00 = io_apic_read(0);
	*(int *)&reg_01 = io_apic_read(1);
	*(int *)&reg_02 = io_apic_read(2);

	/*
	 * We are a bit conservative about what we expect, we have to
	 * know about every HW change ASAP ...
	 */
	printk("testing the IO APIC.......................\n");

	printk(".... register #00: %08X\n", *(int *)&reg_00);
	printk(".......    : physical APIC id: %02X\n", reg_00.ID);
	if (reg_00.__reserved_1 || reg_00.__reserved_2)
		UNEXPECTED_IO_APIC();

	printk(".... register #01: %08X\n", *(int *)&reg_01);
	printk(".......     : max redirection entries: %04X\n", reg_01.entries);
	if (	(reg_01.entries != 0x0f) && /* ISA-only Neptune boards */
		(reg_01.entries != 0x17)    /* ISA+PCI boards */
	)
		UNEXPECTED_IO_APIC();
	if (reg_01.entries == 0x0f)
		printk(".......       [IO-APIC cannot route PCI PIRQ 0-3]\n");

	printk(".......     : IO APIC version: %04X\n", reg_01.version);
	if (	(reg_01.version != 0x10) && /* oldest IO-APICs */
		(reg_01.version != 0x11)  /* my IO-APIC */
	)
		UNEXPECTED_IO_APIC();
	if (reg_01.__reserved_1 || reg_01.__reserved_2)
		UNEXPECTED_IO_APIC();

	printk(".... register #02: %08X\n", *(int *)&reg_02);
	printk(".......     : arbitration: %02X\n", reg_02.arbitration);
	if (reg_02.__reserved_1 || reg_02.__reserved_2)
		UNEXPECTED_IO_APIC();

	printk(".... IRQ redirection table:\n");

	printk(" NR  Log Phy ");
	printk("Mask Trig IRR Pol Stat Dest Deli Vect:   \n");

	for (i=0; i<=reg_01.entries; i++) {
		struct IO_APIC_route_entry entry;

		*(((int *)&entry)+0) = io_apic_read(0x10+i*2);
		*(((int *)&entry)+1) = io_apic_read(0x11+i*2);

		printk(" %02x  %03X  %02X   ",
			i,
			entry.dest.logical.logical_dest,
			entry.dest.physical.physical_dest
		);

		printk("%1d    %1d    %1d   %1d   %1d    %1d    %1d    %02X\n",
			entry.mask,
			entry.trigger,
			entry.irr,
			entry.polarity,
			entry.delivery_status,
			entry.dest_mode,
			entry.delivery_mode,
			entry.vector
		);
	}

	printk(".................................... done.\n");

	return;
}

void init_sym_mode (void)
{
	printk("enabling Symmetric IO mode ... ");
		outb (0x70, 0x22);
		outb (0x01, 0x23);
	printk("...done.\n");
}

void setup_IO_APIC (void)
{
	int i;
	/*
	 *      Map the IO APIC into kernel space
	 */

	printk("mapping IO APIC from standard address.\n");
	io_apic_reg = ioremap_nocache(IO_APIC_BASE,4096);
	printk("new virtual address: %p.\n",io_apic_reg);

	init_sym_mode();
	{
		struct IO_APIC_reg_01 reg_01;

		*(int *)&reg_01 = io_apic_read(1);
		nr_ioapic_registers = reg_01.entries+1;
	}

	init_IO_APIC_traps();

	/*
	 * do not trust the IO-APIC being empty at bootup
	 */
	for (i=0; i<nr_ioapic_registers; i++)
		clear_IO_APIC_irq (i);

#if DEBUG_1
	for (i=0; i<16; i++)
		if (IO_APIC_IRQ(i))
			setup_IO_APIC_irq_ISA_default (i);
#endif

	setup_IO_APIC_irqs ();

	printk("nr of MP irq sources: %d.\n", mp_irq_entries);
	printk("nr of IOAPIC registers: %d.\n", nr_ioapic_registers);
	print_IO_APIC();
}

