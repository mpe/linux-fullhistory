/*
 *	Intel IO-APIC support for multi-pentium hosts.
 *
 *	Copyright (C) 1997, 1998 Ingo Molnar, Hajnalka Szabo
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

/*
 * volatile is justified in this case, it might change
 * spontaneously, GCC should not cache it
 */
#define IO_APIC_BASE ((volatile int *)0xfec00000)

enum mp_irq_source_types {
    mp_INT = 0,
    mp_NMI = 1,
    mp_SMI = 2,
    mp_ExtINT = 3
};

enum ioapic_irq_destination_types {
    dest_Fixed = 0,
    dest_LowestPrio = 1,
    dest_ExtINT = 7
};

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
					 * 111: ExtINT
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
int mpc_default_type = 0;			/* non-0 if default (table-less)
						   MP configuration */


/*
 * This is performance-critical, we want to do it O(1)
 */
static int irq_2_pin[NR_IRQS];

unsigned int io_apic_read (unsigned int reg)
{
	*IO_APIC_BASE = reg;
	return *(IO_APIC_BASE+4);
}

void io_apic_write (unsigned int reg, unsigned int value)
{
	*IO_APIC_BASE = reg;
	*(IO_APIC_BASE+4) = value;
}

/*
 * Syncronize the IO-APIC and the CPU by doing
 * a dummy read from the IO-APIC
 */
static inline void io_apic_sync(void)
{
	(void) *(IO_APIC_BASE+4);
}

/*
 * We disable IO-APIC IRQs by setting their 'destination CPU mask' to
 * zero. Trick, trick.
 */
void disable_IO_APIC_irq(unsigned int irq)
{
	int pin = irq_2_pin[irq];
	struct IO_APIC_route_entry entry;

	if (pin != -1) {
		*(((int *)&entry)+1) = io_apic_read(0x11+pin*2);
		entry.dest.logical.logical_dest = 0x0;
		io_apic_write(0x11+2*pin, *(((int *)&entry)+1));
		io_apic_sync();
	}
}

void enable_IO_APIC_irq(unsigned int irq)
{
	int pin = irq_2_pin[irq];
	struct IO_APIC_route_entry entry;

	if (pin != -1) {
		*(((int *)&entry)+1) = io_apic_read(0x11+pin*2);
		entry.dest.logical.logical_dest = 0xff;
		io_apic_write(0x11+2*pin, *(((int *)&entry)+1));
	}
}

void mask_IO_APIC_irq(unsigned int irq)
{
	int pin = irq_2_pin[irq];
	struct IO_APIC_route_entry entry;

	if (pin != -1) {
		*(((int *)&entry)+0) = io_apic_read(0x10+pin*2);
		entry.mask = 1;
		io_apic_write(0x10+2*pin, *(((int *)&entry)+0));
		io_apic_sync();
	}
}

void unmask_IO_APIC_irq(unsigned int irq)
{
	int pin = irq_2_pin[irq];
	struct IO_APIC_route_entry entry;

	if (pin != -1) {
		*(((int *)&entry)+0) = io_apic_read(0x10+pin*2);
		entry.mask = 0;
		io_apic_write(0x10+2*pin, *(((int *)&entry)+0));
	}
}

void clear_IO_APIC_pin (unsigned int pin)
{
	struct IO_APIC_route_entry entry;

	/*
	 * Disable it in the IO-APIC irq-routing table:
	 */
	memset(&entry, 0, sizeof(entry));
	entry.mask = 1;
	io_apic_write(0x10+2*pin, *(((int *)&entry)+0));
	io_apic_write(0x11+2*pin, *(((int *)&entry)+1));
}


/*
 * support for broken MP BIOSes, enables hand-redirection of PIRQ0-7 to
 * specific CPU-side IRQs.
 */

#define MAX_PIRQS 8
int pirq_entries [MAX_PIRQS];
int pirqs_enabled;

__initfunc(void ioapic_pirq_setup(char *str, int *ints))
{
	int i, max;

	for (i=0; i<MAX_PIRQS; i++)
		pirq_entries[i]=-1;

	if (!ints) {
		pirqs_enabled=0;
		printk("PIRQ redirection SETUP, trusting MP-BIOS.\n");

	} else {
		pirqs_enabled=1;
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

/*
 * Find the irq entry nr of a certain pin.
 */
__initfunc(static int find_irq_entry(int pin, int type))
{
	int i;

	for (i=0; i<mp_irq_entries; i++)
		if ( (mp_irqs[i].mpc_irqtype == type) &&
			(mp_irqs[i].mpc_dstirq == pin))

			return i;

	return -1;
}

/*
 * Find the pin to which IRQ0 (ISA) is connected
 */
__initfunc(int find_timer_pin (int type))
{
	int i;

	for (i=0; i<mp_irq_entries; i++) {
		int lbus = mp_irqs[i].mpc_srcbus;

		if ((mp_bus_id_to_type[lbus] == MP_BUS_ISA) &&
		    (mp_irqs[i].mpc_irqtype == type) &&
		    (mp_irqs[i].mpc_srcbusirq == 0x00))

			return mp_irqs[i].mpc_dstirq;
	}
	return -1;
}

/*
 * Find a specific PCI IRQ entry.
 * Not an initfunc, possibly needed by modules
 */
int IO_APIC_get_PCI_irq_vector (int bus, int slot, int pci_pin)
{
	int i;

	for (i=0; i<mp_irq_entries; i++) {
		int lbus = mp_irqs[i].mpc_srcbus;

		if (IO_APIC_IRQ(mp_irqs[i].mpc_dstirq) &&
		    (mp_bus_id_to_type[lbus] == MP_BUS_PCI) &&
		    !mp_irqs[i].mpc_irqtype &&
		    (bus == mp_bus_id_to_pci_bus[mp_irqs[i].mpc_srcbus]) &&
		    (slot == ((mp_irqs[i].mpc_srcbusirq >> 2) & 0x1f)) &&
		    (pci_pin == (mp_irqs[i].mpc_srcbusirq & 3)))

			return mp_irqs[i].mpc_dstirq;
	}
	return -1;
}

/*
 * There are broken mptables which register ISA+high-active+level IRQs,
 * these are illegal and are converted here to ISA+high-active+edge
 * IRQ sources. Careful, ISA+low-active+level is another broken entry
 * type, it represents PCI IRQs 'embedded into an ISA bus', they have
 * to be accepted. Yes, ugh.
 */

static int MPBIOS_polarity(int idx)
{
	int bus = mp_irqs[idx].mpc_srcbus;
	int polarity;

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
					polarity = 0;
					break;
				}
				case MP_BUS_PCI: /* PCI pin */
				{
					polarity = 1;
					break;
				}
				default:
				{
					printk("broken BIOS!!\n");
					polarity = 1;
					break;
				}
			}
			break;
		}
		case 1: /* high active */
		{
			polarity = 0;
			break;
		}
		case 2: /* reserved */
		{
			printk("broken BIOS!!\n");
			polarity = 1;
			break;
		}
		case 3: /* low active */
		{
			polarity = 1;
			break;
		}
		default: /* invalid */
		{
			printk("broken BIOS!!\n");
			polarity = 1;
			break;
		}
	}
	return polarity;
}


static int MPBIOS_trigger(int idx)
{
	int bus = mp_irqs[idx].mpc_srcbus;
	int trigger;

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
					trigger = 0;
					break;
				}
				case MP_BUS_PCI: /* PCI pin, level */
				{
					trigger = 1;
					break;
				}
				default:
				{
					printk("broken BIOS!!\n");
					trigger = 1;
					break;
				}
			}
			break;
		}
		case 1: /* edge */
		{
			trigger = 0;
			break;
		}
		case 2: /* reserved */
		{
			printk("broken BIOS!!\n");
			trigger = 1;
			break;
		}
		case 3: /* level */
		{
			trigger = 1;
			break;
		}
		default: /* invalid */
		{
			printk("broken BIOS!!\n");
			trigger = 0;
			break;
		}
	}
	return trigger;
}

static int trigger_flag_broken (int idx)
{
	int bus = mp_irqs[idx].mpc_srcbus;
	int polarity = MPBIOS_polarity(idx);
	int trigger = MPBIOS_trigger(idx);

	if ( (mp_bus_id_to_type[bus] == MP_BUS_ISA) &&
		(polarity == 0) /* active-high */ &&
		(trigger == 1) /* level */ )

		return 1; /* broken */

	return 0;
}

static int irq_polarity (int idx)
{
	/*
	 * There are no known BIOS bugs wrt polarity.                yet.
	 */
	return MPBIOS_polarity(idx);
}

static int irq_trigger (int idx)
{
	int trigger = MPBIOS_trigger(idx);

	if (trigger_flag_broken (idx))
		trigger = 0;
	return trigger;
}

static int pin_2_irq (int idx, int pin)
{
	int irq;
	int bus = mp_irqs[idx].mpc_srcbus;

	/*
	 * Debugging check, we are in big trouble if this message pops up!
	 */
	if (mp_irqs[idx].mpc_dstirq != pin)
		printk("broken BIOS or MPTABLE parser, ayiee!!\n");

	switch (mp_bus_id_to_type[bus])
	{
		case MP_BUS_ISA: /* ISA pin */
		{
			irq = mp_irqs[idx].mpc_srcbusirq;
			break;
		}
		case MP_BUS_PCI: /* PCI pin */
		{
			/*
			 * PCI IRQs are 'directly mapped'
			 */
			irq = pin;
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
	 * PCI IRQ command line redirection. Yes, limits are hardcoded.
	 */
	if ((pin>=16) && (pin<=23)) {
		if (pirq_entries[pin-16] != -1) {
			if (!pirq_entries[pin-16]) {
				printk("disabling PIRQ%d\n", pin-16);
			} else {
				irq = pirq_entries[pin-16];
				printk("using PIRQ%d -> IRQ %d\n",
						pin-16, irq);
			}
		}
	}
	return irq;
}

int IO_APIC_irq_trigger (int irq)
{
	int idx, pin;

	for (pin=0; pin<nr_ioapic_registers; pin++) {
		idx = find_irq_entry(pin,mp_INT);
		if ((idx != -1) && (irq == pin_2_irq(idx,pin)))
			return (irq_trigger(idx));
	}
	/*
	 * nonexistant IRQs are edge default
	 */
	return 0;
}

__initfunc(void setup_IO_APIC_irqs (void))
{
	struct IO_APIC_route_entry entry;
	int pin, idx, bus, irq, first_notcon=1;

	printk("init IO_APIC IRQs\n");

	for (pin=0; pin<nr_ioapic_registers; pin++) {

		/*
		 * add it to the IO-APIC irq-routing table:
		 */
		memset(&entry,0,sizeof(entry));

		entry.delivery_mode = dest_LowestPrio;
		entry.dest_mode = 1;			/* logical delivery */
		entry.mask = 0;				/* enable IRQ */
		entry.dest.logical.logical_dest = 0xff;	/* all CPUs */

		idx = find_irq_entry(pin,mp_INT);
		if (idx == -1) {
			if (first_notcon) {
				printk(" IO-APIC pin %d", pin);
				first_notcon=0;
			} else
				printk(", %d", pin);
			continue;
		}

		entry.trigger = irq_trigger(idx);
		entry.polarity = irq_polarity(idx);

		irq = pin_2_irq(idx,pin);
		irq_2_pin[irq] = pin;

		if (!IO_APIC_IRQ(irq))
			continue;

		entry.vector = IO_APIC_VECTOR(irq);

		bus = mp_irqs[idx].mpc_srcbus;

		if (trigger_flag_broken (idx))
			printk("broken BIOS, changing pin %d to edge\n", pin);

		io_apic_write(0x10+2*pin, *(((int *)&entry)+0));
		io_apic_write(0x11+2*pin, *(((int *)&entry)+1));
	}

	if (!first_notcon)
		printk(" not connected.\n");
}

__initfunc(void setup_IO_APIC_irq_ISA_default (unsigned int irq))
{
	struct IO_APIC_route_entry entry;

	/*
	 * add it to the IO-APIC irq-routing table:
	 */
	memset(&entry,0,sizeof(entry));

	entry.delivery_mode = dest_LowestPrio;		/* lowest prio */
	entry.dest_mode = 1;				/* logical delivery */
	entry.mask = 0;					/* unmask IRQ now */
	entry.dest.logical.logical_dest = 0xff;		/* all CPUs */

	entry.vector = IO_APIC_VECTOR(irq);

	entry.polarity=0;
	entry.trigger=0;

	io_apic_write(0x10+2*irq, *(((int *)&entry)+0));
	io_apic_write(0x11+2*irq, *(((int *)&entry)+1));
}

/*
 * Set up a certain pin as ExtINT delivered interrupt
 */
__initfunc(void setup_ExtINT_pin (unsigned int pin))
{
	struct IO_APIC_route_entry entry;

	/*
	 * add it to the IO-APIC irq-routing table:
	 */
	memset(&entry,0,sizeof(entry));

	entry.delivery_mode = dest_ExtINT;
	entry.dest_mode = 1;				/* logical delivery */
	entry.mask = 0;					/* unmask IRQ now */
	entry.dest.logical.logical_dest = 0xff;		/* all CPUs */

	entry.vector = IO_APIC_VECTOR(pin);		/* it's ignored */

	entry.polarity=0;
	entry.trigger=0;

	io_apic_write(0x10+2*pin, *(((int *)&entry)+0));
	io_apic_write(0x11+2*pin, *(((int *)&entry)+1));
}

void print_IO_APIC (void)
{
	int i;
	struct IO_APIC_reg_00 reg_00;
	struct IO_APIC_reg_01 reg_01;
	struct IO_APIC_reg_02 reg_02;

 	printk("nr of MP irq sources: %d.\n", mp_irq_entries);
 	printk("nr of IO-APIC registers: %d.\n", nr_ioapic_registers);

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

	printk(" NR Log Phy ");
	printk("Mask Trig IRR Pol Stat Dest Deli Vect:   \n");

	for (i=0; i<=reg_01.entries; i++) {
		struct IO_APIC_route_entry entry;

		*(((int *)&entry)+0) = io_apic_read(0x10+i*2);
		*(((int *)&entry)+1) = io_apic_read(0x11+i*2);

		printk(" %02x %03X %02X  ",
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

	printk("IRQ to pin mappings:\n");
	for (i=0; i<NR_IRQS; i++)
		printk("%d->%d ", i, irq_2_pin[i]);
	printk("\n");

	printk(".................................... done.\n");

	return;
}

__initfunc(static void init_sym_mode (void))
{
	int i, pin;

	for (i=0; i<NR_IRQS; i++)
		irq_2_pin[i] = -1;
	if (!pirqs_enabled)
		for (i=0; i<MAX_PIRQS; i++)
			pirq_entries[i]=-1;

	printk("enabling Symmetric IO mode ... ");

	outb(0x70, 0x22);
	outb(0x01, 0x23);

	printk("...done.\n");

	/*
	 * The number of IO-APIC irq-registers (== #pins):
	 */
	{
		struct IO_APIC_reg_01 reg_01;

		*(int *)&reg_01 = io_apic_read(1);
		nr_ioapic_registers = reg_01.entries+1;
	}

	/*
	 * Do not trust the IO-APIC being empty at bootup
	 */
	for (pin=0; pin<nr_ioapic_registers; pin++)
		clear_IO_APIC_pin (pin);
}

/*
 * Not an initfunc, needed by the reboot code
 */
void init_pic_mode (void)
{
	printk("disabling Symmetric IO mode ... ");
		outb_p (0x70, 0x22);
		outb_p (0x00, 0x23);
	printk("...done.\n");
}

char ioapic_OEM_ID [16];
char ioapic_Product_ID [16];

struct ioapic_list_entry {
	char * oem_id;
	char * product_id;
};

struct ioapic_list_entry ioapic_whitelist [] = {

	{ "INTEL   "	, 	"PR440FX     "	},
	{ "INTEL   "	,	"82440FX     "	},
	{ "AIR     "	,	"KDI         "	},
	{ 0		,	0		}
};

struct ioapic_list_entry ioapic_blacklist [] = {

	{ "OEM00000"	,	"PROD00000000"	},
	{ 0		,	0		}
};

__initfunc(static int in_ioapic_list (struct ioapic_list_entry * table))
{
	for (;table->oem_id; table++)
		if ((!strcmp(table->oem_id,ioapic_OEM_ID)) &&
		    (!strcmp(table->product_id,ioapic_Product_ID)))
			return 1;
	return 0;
}

__initfunc(static int ioapic_whitelisted (void))
{
/*
 * Right now, whitelist everything to see whether the new parsing
 * routines really do work for everybody..
 */
#if 1
	return 1;
#else
	return in_ioapic_list(ioapic_whitelist);
#endif
}

__initfunc(static int ioapic_blacklisted (void))
{
	return in_ioapic_list(ioapic_blacklist);
}

__initfunc(static void setup_ioapic_id (void))
{
	struct IO_APIC_reg_00 reg_00;

	/*
	 * 'default' mptable configurations mean a hardwired setup,
	 * 2 CPUs, 16 APIC registers. IO-APIC ID is usually set to 0,
	 * setting it to ID 2 should be fine.
	 */

	/*
	 * Sanity check, is ID 2 really free? Every APIC in the
	 * system must have a unique ID or we get lots of nice
	 * 'stuck on smp_invalidate_needed IPI wait' messages.
	 */
	if (cpu_present_map & (1<<0x2))
		panic("APIC ID 2 already used");

	/*
	 * Set the ID
	 */
	*(int *)&reg_00 = io_apic_read(0);
	printk("... changing IO-APIC physical APIC ID to 2 ...\n");
	reg_00.ID = 0x2;
	io_apic_write(0, *(int *)&reg_00);

	/*
	 * Sanity check
	 */
	*(int *)&reg_00 = io_apic_read(0);
	if (reg_00.ID != 0x2)
		panic("could not set ID");
}

__initfunc(static void construct_default_ISA_mptable (void))
{
	int i, pos=0;

	for (i=0; i<16; i++) {
		if (!IO_APIC_IRQ(i))
			continue;

		mp_irqs[pos].mpc_irqtype = 0;
		mp_irqs[pos].mpc_irqflag = 0;
		mp_irqs[pos].mpc_srcbus = 0;
		mp_irqs[pos].mpc_srcbusirq = i;
		mp_irqs[pos].mpc_dstapic = 0;
		mp_irqs[pos].mpc_dstirq = i;
		pos++;
	}
	mp_irq_entries = pos;
	mp_bus_id_to_type[0] = MP_BUS_ISA;

	/*
	 * MP specification 1.4 defines some extra rules for default
	 * configurations, fix them up here:
	 */
	
	switch (mpc_default_type)
	{
		case 2:
			break;
		default:
		/*
		 * pin 2 is IRQ0:
		 */
			mp_irqs[0].mpc_dstirq = 2;
	}

	setup_ioapic_id();
}

/*
 * There is a nasty bug in some older SMP boards, their mptable lies
 * about the timer IRQ. We do the following to work around the situation:
 *
 *	- timer IRQ defaults to IO-APIC IRQ
 *	- if this function detects that timer IRQs are defunct, then we fall
 *	  back to ISA timer IRQs
 */
__initfunc(static int timer_irq_works (void))
{
	unsigned int t1=jiffies;
	unsigned long flags;

	save_flags(flags);
	sti();

	udelay(10*10000);

	if (jiffies-t1>1)
		return 1;

	return 0;
}

#ifdef __SMP__

/*
 * In the SMP+IOAPIC case it might happen that there are an unspecified
 * number of pending IRQ events unhandled. These cases are very rare,
 * so we 'resend' these IRQs via IPIs, to the same CPU. It's much
 * better to do it this way as thus we do not have to be aware of
 * 'pending' interrupts in the IRQ path, except at this point.
 */
static inline void self_IPI (unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;

	if (desc->events && !desc->ipi) {
		desc->ipi = 1;
		send_IPI(APIC_DEST_SELF, IO_APIC_VECTOR(irq));
	}
}

/*
 * Edge triggered needs to resend any interrupt
 * that was delayed.
 */
static void enable_edge_ioapic_irq(unsigned int irq)
{
	self_IPI(irq);
	enable_IO_APIC_irq(irq);
}

static void disable_edge_ioapic_irq(unsigned int irq)
{
	disable_IO_APIC_irq(irq);
}

/*
 * Level triggered interrupts can just be masked
 */
static void enable_level_ioapic_irq(unsigned int irq)
{
	unmask_IO_APIC_irq(irq);
}

static void disable_level_ioapic_irq(unsigned int irq)
{
	mask_IO_APIC_irq(irq);
}

/*
 * Enter and exit the irq handler context..
 */
static inline void enter_ioapic_irq(int cpu)
{
	hardirq_enter(cpu);
	while (test_bit(0,&global_irq_lock)) barrier();
}

static inline void exit_ioapic_irq(int cpu)
{
	hardirq_exit(cpu);
	release_irqlock(cpu);
}	

static void do_edge_ioapic_IRQ(unsigned int irq, int cpu, struct pt_regs * regs)
{
	irq_desc_t *desc = irq_desc + irq;
	struct irqaction * action;

	spin_lock(&irq_controller_lock);

	/*
	 * Edge triggered IRQs can be acked immediately
	 * and do not need to be masked.
	 */
	ack_APIC_irq();
	desc->ipi = 0;
	desc->events = 1;

	/*
	 * If the irq is disabled for whatever reason, we cannot
	 * use the action we have..
	 */
	action = NULL;
	if (!(desc->status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		action = desc->action;
		desc->status = IRQ_INPROGRESS;
		desc->events = 0;
	}
	spin_unlock(&irq_controller_lock);

	/*
	 * If there is no IRQ handler or it was disabled, exit early.
	 */
	if (!action)
		return;

	enter_ioapic_irq(cpu);

	/*
	 * Edge triggered interrupts need to remember
	 * pending events..
	 */
	for (;;) {
		int pending;

		handle_IRQ_event(irq, regs);

		spin_lock(&irq_controller_lock);
		pending = desc->events;
		desc->events = 0;
		if (!pending)
			break;
		spin_unlock(&irq_controller_lock);
	}
	desc->status &= IRQ_DISABLED;
	spin_unlock(&irq_controller_lock);

	exit_ioapic_irq(cpu);
}

static void do_level_ioapic_IRQ (unsigned int irq, int cpu,
						 struct pt_regs * regs)
{
	irq_desc_t *desc = irq_desc + irq;
	struct irqaction * action;

	spin_lock(&irq_controller_lock);
	/*
	 * In the level triggered case we first disable the IRQ
	 * in the IO-APIC, then we 'early ACK' the IRQ, then we
	 * handle it and enable the IRQ when finished.
	 *
	 * disable has to happen before the ACK, to avoid IRQ storms.
	 * So this all has to be within the spinlock.
	 */
	mask_IO_APIC_irq(irq);

	desc->ipi = 0;

	/*
	 * If the irq is disabled for whatever reason, we must
	 * not enter the irq action.
	 */
	action = NULL;
	if (!(desc->status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		action = desc->action;
		desc->status = IRQ_INPROGRESS;
	}

	ack_APIC_irq();
	spin_unlock(&irq_controller_lock);

	/* Exit early if we had no action or it was disabled */
	if (!action)
		return;

	enter_ioapic_irq(cpu);

	handle_IRQ_event(irq, regs);

	spin_lock(&irq_controller_lock);
	desc->status &= ~IRQ_INPROGRESS;
	if (!desc->status)
		unmask_IO_APIC_irq(irq);
	spin_unlock(&irq_controller_lock);

	exit_ioapic_irq(cpu);
}

/*
 * Level and edge triggered IO-APIC interrupts need different handling,
 * so we use two separate irq descriptors. edge triggered IRQs can be
 * handled with the level-triggered descriptor, but that one has slightly
 * more overhead. Level-triggered interrupts cannot be handled with the
 * edge-triggered handler, without risking IRQ storms and other ugly
 * races.
 */

static struct hw_interrupt_type ioapic_edge_irq_type = {
	"IO-APIC-edge",
	do_edge_ioapic_IRQ,
	enable_edge_ioapic_irq,
	disable_edge_ioapic_irq
};

static struct hw_interrupt_type ioapic_level_irq_type = {
	"IO-APIC-level",
	do_level_ioapic_IRQ,
	enable_level_ioapic_irq,
	disable_level_ioapic_irq
};

void init_IO_APIC_traps(void)
{
	int i;
	/*
	 * NOTE! The local APIC isn't very good at handling
	 * multiple interrupts at the same interrupt level.
	 * As the interrupt level is determined by taking the
	 * vector number and shifting that right by 4, we
	 * want to spread these out a bit so that they don't
	 * all fall in the same interrupt level
	 *
	 * also, we've got to be careful not to trash gate
	 * 0x80, because int 0x80 is hm, kindof importantish ;)
	 */
	for (i = 0; i < NR_IRQS ; i++) {
		if ((IO_APIC_VECTOR(i) <= 0xfe)  /* HACK */ &&
		    (IO_APIC_IRQ(i))) {
			if (IO_APIC_irq_trigger(i))
				irq_desc[i].handler = &ioapic_level_irq_type;
			else
				irq_desc[i].handler = &ioapic_edge_irq_type;
			/*
			 * disable it in the 8259A:
			 */
			cached_irq_mask |= 1 << i;
			if (i < 16)
				set_8259A_irq_mask(i);
		}
	}
}
#endif

/*
 * This code may look a bit paranoid, but it's supposed to cooperate with
 * a wide range of boards and BIOS bugs ... fortunately only the timer IRQ
 * is so screwy. Thanks to Brian Perkins for testing/hacking this beast
 * fanatically on his truly bugged board.
 */
__initfunc(static void check_timer (void))
{
	int pin1, pin2;

	pin1 = find_timer_pin (mp_INT);
	pin2 = find_timer_pin (mp_ExtINT);

	if (!timer_irq_works ()) {
		if (pin1 != -1)
			printk("..MP-BIOS bug: 8254 timer not connected to IO-APIC\n");
		printk("..trying to set up timer as ExtINT ... ");

		if (pin2 != -1) {
			printk(".. (found pin %d) ...", pin2);
			setup_ExtINT_pin (pin2);
			make_8259A_irq(0);
		}

		if (!timer_irq_works ()) {
			printk(" failed.\n");
			printk("..trying to set up timer as BP irq ...");
			/*
			 * Just in case ...
			 */
			if (pin1 != -1)
				clear_IO_APIC_pin (pin1);
			if (pin2 != -1)
				clear_IO_APIC_pin (pin2);

			make_8259A_irq(0);

			if (!timer_irq_works ()) {
				printk(" failed.\n");
				panic("IO-APIC + timer doesnt work!");
			}
		}
		printk(" works.\n");
	}
}

__initfunc(void setup_IO_APIC (void))
{
	init_sym_mode();

	/*
	 * Determine the range of IRQs handled by the IO-APIC. The
	 * following boards can be fully enabled:
	 *
	 * - whitelisted ones
	 * - those which have no PCI pins connected
	 * - those for which the user has specified a pirq= parameter
	 */
	if (	ioapic_whitelisted() ||
		(nr_ioapic_registers == 16) ||
		pirqs_enabled)
	{
		printk("ENABLING IO-APIC IRQs\n");
		io_apic_irqs = ~((1<<2)|(1<<13));
	} else {
		if (ioapic_blacklisted())
			printk(" blacklisted board, DISABLING IO-APIC IRQs\n");
		else
			printk(" unlisted board, DISABLING IO-APIC IRQs\n");

		printk(" see Documentation/IO-APIC.txt to enable them\n");
		io_apic_irqs = 0;
	}

	/*
	 * If there are no explicit mp irq entries: it's either one of the
	 * default configuration types or we are broken. In both cases it's
	 * fine to set up most of the low 16 IO-APIC pins to ISA defaults.
	 */
	if (!mp_irq_entries) {
		printk("no explicit IRQ entries, using default mptable\n");
		construct_default_ISA_mptable();
	}

	init_IO_APIC_traps();

	/*
	 * Set up the IO-APIC irq routing table by parsing the MP-BIOS
	 * mptable:
	 */
	setup_IO_APIC_irqs ();
	check_timer();
 
	print_IO_APIC();
}

