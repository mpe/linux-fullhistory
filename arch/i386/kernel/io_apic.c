/*
 *	Intel IO-APIC support for multi-Pentium hosts.
 *
 *	Copyright (C) 1997, 1998 Ingo Molnar, Hajnalka Szabo
 *
 *	Many thanks to Stig Venaas for trying out countless experimental
 *	patches and reporting/debugging problems patiently!
 */

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "irq.h"

/*
 * volatile is justified in this case, IO-APIC register contents
 * might change spontaneously, GCC should not cache it
 */
#define IO_APIC_BASE ((volatile int *)fix_to_virt(FIX_IO_APIC_BASE))

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

/*
 * # of IRQ routing registers
 */
int nr_ioapic_registers = 0;

enum ioapic_irq_destination_types {
	dest_Fixed = 0,
	dest_LowestPrio = 1,
	dest_ExtINT = 7
};

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

/*
 * MP-BIOS irq configuration table structures:
 */

enum mp_irq_source_types {
	mp_INT = 0,
	mp_NMI = 1,
	mp_SMI = 2,
	mp_ExtINT = 3
};

int mp_irq_entries = 0;				/* # of MP IRQ source entries */
struct mpc_config_intsrc mp_irqs[MAX_IRQ_SOURCES];
						/* MP IRQ source entries */
int mpc_default_type = 0;			/* non-0 if default (table-less)
						   MP configuration */


/*
 * This is performance-critical, we want to do it O(1)
 *
 * the indexing order of this array favors 1:1 mappings
 * between pins and IRQs.
 */

static inline unsigned int io_apic_read(unsigned int reg)
{
	*IO_APIC_BASE = reg;
	return *(IO_APIC_BASE+4);
}

static inline void io_apic_write(unsigned int reg, unsigned int value)
{
	*IO_APIC_BASE = reg;
	*(IO_APIC_BASE+4) = value;
}

/*
 * Re-write a value: to be used for read-modify-write
 * cycles where the read already set up the index register.
 */
static inline void io_apic_modify(unsigned int value)
{
	*(IO_APIC_BASE+4) = value;
}

/*
 * Synchronize the IO-APIC and the CPU by doing
 * a dummy read from the IO-APIC
 */
static inline void io_apic_sync(void)
{
	(void) *(IO_APIC_BASE+4);
}

/*
 * Rough estimation of how many shared IRQs there are, can
 * be changed anytime.
 */
#define MAX_PLUS_SHARED_IRQS NR_IRQS
#define PIN_MAP_SIZE (MAX_PLUS_SHARED_IRQS + NR_IRQS)

static struct irq_pin_list {
	int pin, next;
} irq_2_pin[PIN_MAP_SIZE];

/*
 * The common case is 1:1 IRQ<->pin mappings. Sometimes there are
 * shared ISA-space IRQs, so we have to support them. We are super
 * fast in the common case, and fast for shared ISA-space IRQs.
 */
static void add_pin_to_irq(unsigned int irq, int pin)
{
	static int first_free_entry = NR_IRQS;
	struct irq_pin_list *entry = irq_2_pin + irq;

	while (entry->next)
		entry = irq_2_pin + entry->next;

	if (entry->pin != -1) {
		entry->next = first_free_entry;
		entry = irq_2_pin + entry->next;
		if (++first_free_entry >= PIN_MAP_SIZE)
			panic("io_apic.c: whoops");
	}
	entry->pin = pin;
}

#define DO_ACTION(name,R,ACTION, FINAL)					\
									\
static void name##_IO_APIC_irq(unsigned int irq)			\
{									\
	int pin;							\
	struct irq_pin_list *entry = irq_2_pin + irq;			\
									\
	for (;;) {							\
		unsigned int reg;					\
		pin = entry->pin;					\
		if (pin == -1)						\
			break;						\
		reg = io_apic_read(0x10 + R + pin*2);			\
		reg ACTION;						\
		io_apic_modify(reg);					\
		if (!entry->next)					\
			break;						\
		entry = irq_2_pin + entry->next;			\
	}								\
	FINAL;								\
}

/*
 * We disable IO-APIC IRQs by setting their 'destination CPU mask' to
 * zero. Trick by Ramesh Nalluri.
 */
DO_ACTION( disable, 1, &= 0x00ffffff, io_apic_sync())		/* destination = 0x00 */
DO_ACTION( enable,  1, |= 0xff000000, )				/* destination = 0xff */
DO_ACTION( mask,    0, |= 0x00010000, io_apic_sync())		/* mask = 1 */
DO_ACTION( unmask,  0, &= 0xfffeffff, )				/* mask = 0 */

static void __init clear_IO_APIC_pin(unsigned int pin)
{
	struct IO_APIC_route_entry entry;

	/*
	 * Disable it in the IO-APIC irq-routing table:
	 */
	memset(&entry, 0, sizeof(entry));
	entry.mask = 1;
	io_apic_write(0x10 + 2 * pin, *(((int *)&entry) + 0));
	io_apic_write(0x11 + 2 * pin, *(((int *)&entry) + 1));
}


/*
 * support for broken MP BIOSs, enables hand-redirection of PIRQ0-7 to
 * specific CPU-side IRQs.
 */

#define MAX_PIRQS 8
int pirq_entries [MAX_PIRQS];
int pirqs_enabled;

void __init ioapic_setup(char *str, int *ints)
{
	extern int skip_ioapic_setup;	/* defined in arch/i386/kernel/smp.c */

	skip_ioapic_setup = 1;
}

void __init ioapic_pirq_setup(char *str, int *ints)
{
	int i, max;

	for (i = 0; i < MAX_PIRQS; i++)
		pirq_entries[i] = -1;

	if (!ints) {
		pirqs_enabled = 0;
		printk("PIRQ redirection, trusting MP-BIOS.\n");

	} else {
		pirqs_enabled = 1;
		printk("PIRQ redirection, working around broken MP-BIOS.\n");
		max = MAX_PIRQS;
		if (ints[0] < MAX_PIRQS)
			max = ints[0];

		for (i = 0; i < max; i++) {
			printk("... PIRQ%d -> IRQ %d\n", i, ints[i+1]);
			/*
			 * PIRQs are mapped upside down, usually.
			 */
			pirq_entries[MAX_PIRQS-i-1] = ints[i+1];
		}
	}
}

/*
 * Find the IRQ entry number of a certain pin.
 */
static int __init find_irq_entry(int pin, int type)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++)
		if ( (mp_irqs[i].mpc_irqtype == type) &&
			(mp_irqs[i].mpc_dstirq == pin))

			return i;

	return -1;
}

/*
 * Find the pin to which IRQ0 (ISA) is connected
 */
static int __init find_timer_pin(int type)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++) {
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
int IO_APIC_get_PCI_irq_vector(int bus, int slot, int pci_pin)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++) {
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
 * Unclear documentation on what a "conforming ISA interrupt" means.
 *
 * Should we, or should we not, take the ELCR register into account?
 * It's part of the EISA specification, but maybe it should only be
 * used if the interrupt is actually marked as EISA?
 *
 * Oh, well. Don't do it until somebody tells us what the right thing
 * to do is..
 */
#undef USE_ELCR_TRIGGER_LEVEL
#ifdef USE_ELCR_TRIGGER_LEVEL

/*
 * ISA Edge/Level control register, ELCR
 */
static int __init EISA_ELCR(unsigned int irq)
{
	if (irq < 16) {
		unsigned int port = 0x4d0 + (irq >> 3);
		return (inb(port) >> (irq & 7)) & 1;
	}
	printk("Broken MPtable reports ISA irq %d\n", irq);
	return 0;
}	

#define default_ISA_trigger(idx)	(EISA_ELCR(mp_irqs[idx].mpc_dstirq))
#define default_ISA_polarity(idx)	(0)

#else

#define default_ISA_trigger(idx)	(0)
#define default_ISA_polarity(idx)	(0)

#endif

static int __init MPBIOS_polarity(int idx)
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
					polarity = default_ISA_polarity(idx);
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

static int __init MPBIOS_trigger(int idx)
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
				case MP_BUS_ISA:
				{
					trigger = default_ISA_trigger(idx);
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

static inline int irq_polarity(int idx)
{
	return MPBIOS_polarity(idx);
}

static inline int irq_trigger(int idx)
{
	return MPBIOS_trigger(idx);
}

static int __init pin_2_irq(int idx, int pin)
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
	if ((pin >= 16) && (pin <= 23)) {
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

static inline int IO_APIC_irq_trigger(int irq)
{
	int idx, pin;

	for (pin = 0; pin < nr_ioapic_registers; pin++) {
		idx = find_irq_entry(pin,mp_INT);
		if ((idx != -1) && (irq == pin_2_irq(idx,pin)))
			return irq_trigger(idx);
	}
	/*
	 * nonexistent IRQs are edge default
	 */
	return 0;
}

int irq_vector[NR_IRQS] = { IRQ0_TRAP_VECTOR , 0 };

static int __init assign_irq_vector(int irq)
{
	static int current_vector = IRQ0_TRAP_VECTOR, offset = 0;
	if (IO_APIC_VECTOR(irq) > 0)
		return IO_APIC_VECTOR(irq);
	current_vector += 8;
	if (current_vector > 0xFE) {
		offset++;
		current_vector = IRQ0_TRAP_VECTOR + offset;
		printk("WARNING: ASSIGN_IRQ_VECTOR wrapped back to %02X\n",
		       current_vector);
	}
	IO_APIC_VECTOR(irq) = current_vector;
	return current_vector;
}

void __init setup_IO_APIC_irqs(void)
{
	struct IO_APIC_route_entry entry;
	int pin, idx, bus, irq, first_notcon = 1;

	printk("init IO_APIC IRQs\n");

	for (pin = 0; pin < nr_ioapic_registers; pin++) {

		/*
		 * add it to the IO-APIC irq-routing table:
		 */
		memset(&entry,0,sizeof(entry));

		entry.delivery_mode = dest_LowestPrio;
		entry.dest_mode = 1;			/* logical delivery */
		entry.mask = 0;				/* enable IRQ */
		entry.dest.logical.logical_dest = 0;	/* but no route */

		idx = find_irq_entry(pin,mp_INT);
		if (idx == -1) {
			if (first_notcon) {
				printk(" IO-APIC pin %d", pin);
				first_notcon = 0;
			} else
				printk(", %d", pin);
			continue;
		}

		entry.trigger = irq_trigger(idx);
		entry.polarity = irq_polarity(idx);

		if (irq_trigger(idx)) {
			entry.trigger = 1;
			entry.mask = 1;
			entry.dest.logical.logical_dest = 0xff;
		}

		irq = pin_2_irq(idx,pin);
		add_pin_to_irq(irq, pin);

		if (!IO_APIC_IRQ(irq))
			continue;

		entry.vector = assign_irq_vector(irq);

		bus = mp_irqs[idx].mpc_srcbus;

		io_apic_write(0x11+2*pin, *(((int *)&entry)+1));
		io_apic_write(0x10+2*pin, *(((int *)&entry)+0));
	}

	if (!first_notcon)
		printk(" not connected.\n");
}

/*
 * Set up a certain pin as ExtINT delivered interrupt
 */
void __init setup_ExtINT_pin(unsigned int pin)
{
	struct IO_APIC_route_entry entry;

	/*
	 * add it to the IO-APIC irq-routing table:
	 */
	memset(&entry,0,sizeof(entry));

	entry.delivery_mode = dest_ExtINT;
	entry.dest_mode = 1;				/* logical delivery */
	entry.mask = 0;					/* unmask IRQ now */
	entry.dest.logical.logical_dest = 0x01;		/* logical CPU #0 */

	entry.vector = 0;				/* it's ignored */

	entry.polarity = 0;
	entry.trigger = 0;

	io_apic_write(0x10+2*pin, *(((int *)&entry)+0));
	io_apic_write(0x11+2*pin, *(((int *)&entry)+1));
}

void __init UNEXPECTED_IO_APIC(void)
{
	printk(" WARNING: unexpected IO-APIC, please mail\n");
	printk("          to linux-smp@vger.rutgers.edu\n");
}

void __init print_IO_APIC(void)
{
	int i;
	struct IO_APIC_reg_00 reg_00;
	struct IO_APIC_reg_01 reg_01;
	struct IO_APIC_reg_02 reg_02;

 	printk("number of MP IRQ sources: %d.\n", mp_irq_entries);
 	printk("number of IO-APIC registers: %d.\n", nr_ioapic_registers);

	*(int *)&reg_00 = io_apic_read(0);
	*(int *)&reg_01 = io_apic_read(1);
	*(int *)&reg_02 = io_apic_read(2);

	/*
	 * We are a bit conservative about what we expect.  We have to
	 * know about every hardware change ASAP.
	 */
	printk("testing the IO APIC.......................\n");

	printk(".... register #00: %08X\n", *(int *)&reg_00);
	printk(".......    : physical APIC id: %02X\n", reg_00.ID);
	if (reg_00.__reserved_1 || reg_00.__reserved_2)
		UNEXPECTED_IO_APIC();

	printk(".... register #01: %08X\n", *(int *)&reg_01);
	printk(".......     : max redirection entries: %04X\n", reg_01.entries);
	if (	(reg_01.entries != 0x0f) && /* ISA-only Neptune boards */
		(reg_01.entries != 0x17) && /* ISA+PCI boards */
		(reg_01.entries != 0x3F)    /* Xeon boards */
	)
		UNEXPECTED_IO_APIC();
	if (reg_01.entries == 0x0f)
		printk(".......       [IO-APIC cannot route PCI PIRQ 0-3]\n");

	printk(".......     : IO APIC version: %04X\n", reg_01.version);
	if (	(reg_01.version != 0x10) && /* oldest IO-APICs */
		(reg_01.version != 0x11) && /* Pentium/Pro IO-APICs */
		(reg_01.version != 0x13)    /* Xeon IO-APICs */
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

	for (i = 0; i <= reg_01.entries; i++) {
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

	printk(KERN_DEBUG "IRQ to pin mappings:\n");
	for (i = 0; i < NR_IRQS; i++) {
		struct irq_pin_list *entry = irq_2_pin + i;
		if (entry->pin < 0)
			continue;
		printk(KERN_DEBUG "IRQ%d ", i);
		for (;;) {
			printk("-> %d", entry->pin);
			if (!entry->next)
				break;
			entry = irq_2_pin + entry->next;
		}
		printk("\n");
	}

	printk(".................................... done.\n");

	return;
}

static void __init init_sym_mode(void)
{
	int i, pin;

	for (i = 0; i < PIN_MAP_SIZE; i++) {
		irq_2_pin[i].pin = -1;
		irq_2_pin[i].next = 0;
	}
	if (!pirqs_enabled)
		for (i = 0; i < MAX_PIRQS; i++)
			pirq_entries[i] =- 1;

	printk("enabling symmetric IO mode... ");

	outb(0x70, 0x22);
	outb(0x01, 0x23);

	printk("...done.\n");

	/*
	 * The number of IO-APIC IRQ registers (== #pins):
	 */
	{
		struct IO_APIC_reg_01 reg_01;

		*(int *)&reg_01 = io_apic_read(1);
		nr_ioapic_registers = reg_01.entries+1;
	}

	/*
	 * Do not trust the IO-APIC being empty at bootup
	 */
	for (pin = 0; pin < nr_ioapic_registers; pin++)
		clear_IO_APIC_pin(pin);
}

/*
 * Not an initfunc, needed by the reboot code
 */
void init_pic_mode(void)
{
	printk("disabling symmetric IO mode... ");
		outb_p(0x70, 0x22);
		outb_p(0x00, 0x23);
	printk("...done.\n");
}

char ioapic_OEM_ID [16];
char ioapic_Product_ID [16];

struct ioapic_list_entry {
	char * oem_id;
	char * product_id;
};

struct ioapic_list_entry __initdata ioapic_whitelist [] = {

	{ "INTEL   "	, 	"PR440FX     "	},
	{ "INTEL   "	,	"82440FX     "	},
	{ "AIR     "	,	"KDI         "	},
	{ 0		,	0		}
};

struct ioapic_list_entry __initdata ioapic_blacklist [] = {

	{ "OEM00000"	,	"PROD00000000"	},
	{ 0		,	0		}
};

static int __init in_ioapic_list(struct ioapic_list_entry * table)
{
	for ( ; table->oem_id ; table++)
		if ((!strcmp(table->oem_id,ioapic_OEM_ID)) &&
		    (!strcmp(table->product_id,ioapic_Product_ID)))
			return 1;
	return 0;
}

static int __init ioapic_whitelisted(void)
{
/*
 * Right now, whitelist everything to see whether the new parsing
 * routines really do work for everybody.
 */
#if 1
	return 1;
#else
	return in_ioapic_list(ioapic_whitelist);
#endif
}

static int __init ioapic_blacklisted(void)
{
	return in_ioapic_list(ioapic_blacklist);
}

static void __init setup_ioapic_id(void)
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
	printk("...changing IO-APIC physical APIC ID to 2...\n");
	reg_00.ID = 0x2;
	io_apic_write(0, *(int *)&reg_00);

	/*
	 * Sanity check
	 */
	*(int *)&reg_00 = io_apic_read(0);
	if (reg_00.ID != 0x2)
		panic("could not set ID");
}

static void __init construct_default_ISA_mptable(void)
{
	int i, pos = 0;

	for (i = 0; i < 16; i++) {
		if (!IO_APIC_IRQ(i))
			continue;

		mp_irqs[pos].mpc_irqtype = mp_INT;
		mp_irqs[pos].mpc_irqflag = 0;		/* default */
		mp_irqs[pos].mpc_srcbus = MP_BUS_ISA;
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
static int __init timer_irq_works(void)
{
	unsigned int t1 = jiffies;

	sti();
	mdelay(100);

	if (jiffies-t1>1)
		return 1;

	return 0;
}

/*
 * In the SMP+IOAPIC case it might happen that there are an unspecified
 * number of pending IRQ events unhandled. These cases are very rare,
 * so we 'resend' these IRQs via IPIs, to the same CPU. It's much
 * better to do it this way as thus we do not have to be aware of
 * 'pending' interrupts in the IRQ path, except at this point.
 */
static inline void self_IPI(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned int status = desc->status;

	if ((status & (IRQ_PENDING | IRQ_REPLAY)) == IRQ_PENDING) {
		desc->status = status | IRQ_REPLAY;
		send_IPI_self(IO_APIC_VECTOR(irq));
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
 * Starting up a edge-triggered IO-APIC interrupt is
 * nasty - we need to make sure that we get the edge.
 * If it is already asserted for some reason, we need
 * to fake an edge by marking it IRQ_PENDING..
 *
 * This is not complete - we should be able to fake
 * an edge even if it isn't on the 8259A...
 */

static void startup_edge_ioapic_irq(unsigned int irq)
{
	if (irq < 16) {
		disable_8259A_irq(irq);
		if (i8259A_irq_pending(irq))
			irq_desc[irq].status |= IRQ_PENDING;
	}
	enable_edge_ioapic_irq(irq);
}

#define shutdown_edge_ioapic_irq	disable_edge_ioapic_irq

/*
 * Level triggered interrupts can just be masked,
 * and shutting down and starting up the interrupt
 * is the same as enabling and disabling them.
 */
#define startup_level_ioapic_irq	unmask_IO_APIC_irq
#define shutdown_level_ioapic_irq	mask_IO_APIC_irq
#define enable_level_ioapic_irq		unmask_IO_APIC_irq
#define disable_level_ioapic_irq	mask_IO_APIC_irq

static void do_edge_ioapic_IRQ(unsigned int irq, struct pt_regs * regs)
{
	irq_desc_t *desc = irq_desc + irq;
	struct irqaction * action;
	unsigned int status;

	spin_lock(&irq_controller_lock);

	/*
	 * Edge triggered IRQs can be acknowledged immediately
	 * and do not need to be masked.
	 */
	ack_APIC_irq();
	status = desc->status & ~IRQ_REPLAY;
	status |= IRQ_PENDING;

	/*
	 * If the IRQ is disabled for whatever reason, we cannot
	 * use the action we have.
	 */
	action = NULL;
	if (!(status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		action = desc->action;
		status &= ~IRQ_PENDING;
	}
	desc->status = status | IRQ_INPROGRESS;
	spin_unlock(&irq_controller_lock);

	/*
	 * If there is no IRQ handler or it was disabled, exit early.
	 */
	if (!action)
		return;

	/*
	 * Edge triggered interrupts need to remember
	 * pending events.
	 */
	for (;;) {
		handle_IRQ_event(irq, regs, action);

		spin_lock(&irq_controller_lock);
		if (!(desc->status & IRQ_PENDING))
			break;
		desc->status &= ~IRQ_PENDING;
		spin_unlock(&irq_controller_lock);
	}
	desc->status &= ~IRQ_INPROGRESS;
	spin_unlock(&irq_controller_lock);
}

static void do_level_ioapic_IRQ(unsigned int irq, struct pt_regs * regs)
{
	irq_desc_t *desc = irq_desc + irq;
	struct irqaction * action;
	unsigned int status;

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
	status = desc->status & ~IRQ_REPLAY;

	/*
	 * If the IRQ is disabled for whatever reason, we must
	 * not enter the IRQ action.
	 */
	action = NULL;
	if (!(status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		action = desc->action;
	}
	desc->status = status | IRQ_INPROGRESS;

	ack_APIC_irq();
	spin_unlock(&irq_controller_lock);

	/* Exit early if we had no action or it was disabled */
	if (!action)
		return;

	handle_IRQ_event(irq, regs, action);

	spin_lock(&irq_controller_lock);
	desc->status &= ~IRQ_INPROGRESS;
	if (!(desc->status & IRQ_DISABLED))
		unmask_IO_APIC_irq(irq);
	spin_unlock(&irq_controller_lock);
}

/*
 * Level and edge triggered IO-APIC interrupts need different handling,
 * so we use two separate IRQ descriptors. Edge triggered IRQs can be
 * handled with the level-triggered descriptor, but that one has slightly
 * more overhead. Level-triggered interrupts cannot be handled with the
 * edge-triggered handler, without risking IRQ storms and other ugly
 * races.
 */

static struct hw_interrupt_type ioapic_edge_irq_type = {
	"IO-APIC-edge",
	startup_edge_ioapic_irq,
	shutdown_edge_ioapic_irq,
	do_edge_ioapic_IRQ,
	enable_edge_ioapic_irq,
	disable_edge_ioapic_irq
};

static struct hw_interrupt_type ioapic_level_irq_type = {
	"IO-APIC-level",
	startup_level_ioapic_irq,
	shutdown_level_ioapic_irq,
	do_level_ioapic_IRQ,
	enable_level_ioapic_irq,
	disable_level_ioapic_irq
};

static inline void init_IO_APIC_traps(void)
{
	int i;
	/*
	 * NOTE! The local APIC isn't very good at handling
	 * multiple interrupts at the same interrupt level.
	 * As the interrupt level is determined by taking the
	 * vector number and shifting that right by 4, we
	 * want to spread these out a bit so that they don't
	 * all fall in the same interrupt level.
	 *
	 * Also, we've got to be careful not to trash gate
	 * 0x80, because int 0x80 is hm, kind of importantish. ;)
	 */
	for (i = 0; i < NR_IRQS ; i++) {
		if (IO_APIC_IRQ(i)) {
			if (IO_APIC_irq_trigger(i))
				irq_desc[i].handler = &ioapic_level_irq_type;
			else
				irq_desc[i].handler = &ioapic_edge_irq_type;
			/*
			 * disable it in the 8259A:
			 */
			if (i < 16)
				disable_8259A_irq(i);
		}
	}
}

/*
 * This code may look a bit paranoid, but it's supposed to cooperate with
 * a wide range of boards and BIOS bugs.  Fortunately only the timer IRQ
 * is so screwy.  Thanks to Brian Perkins for testing/hacking this beast
 * fanatically on his truly buggy board.
 */
static inline void check_timer(void)
{
	int pin1, pin2;

	pin1 = find_timer_pin(mp_INT);
	pin2 = find_timer_pin(mp_ExtINT);
	enable_IO_APIC_irq(0);
	if (!timer_irq_works()) {

		if (pin1 != -1)
			printk("..MP-BIOS bug: 8254 timer not connected to IO-APIC\n");
		printk("...trying to set up timer as ExtINT... ");

		if (pin2 != -1) {
			printk(".. (found pin %d) ...", pin2);
			setup_ExtINT_pin(pin2);
			make_8259A_irq(0);
		}

		if (!timer_irq_works()) {
			printk(" failed.\n");
			printk("...trying to set up timer as BP IRQ...");
			/*
			 * Just in case ...
			 */
			if (pin1 != -1)
				clear_IO_APIC_pin(pin1);
			if (pin2 != -1)
				clear_IO_APIC_pin(pin2);

			make_8259A_irq(0);

			if (!timer_irq_works()) {
				printk(" failed.\n");
				panic("IO-APIC + timer doesn't work!");
			}
		}
		printk(" works.\n");
	}
}

/*
 *
 * IRQ's that are handled by the old PIC in all cases:
 * - IRQ2 is the cascade IRQ, and cannot be a io-apic IRQ.
 *   Linux doesn't really care, as it's not actually used
 *   for any interrupt handling anyway.
 * - IRQ13 is the FPU error IRQ, and may be connected
 *   directly from the FPU to the old PIC. Linux doesn't
 *   really care, because Linux doesn't want to use IRQ13
 *   anyway (exception 16 is the proper FPU error signal)
 *
 * Additionally, something is definitely wrong with irq9
 * on PIIX4 boards.
 */
#define PIC_IRQS	((1<<2)|(1<<13))

void __init setup_IO_APIC(void)
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
		io_apic_irqs = ~PIC_IRQS;
	} else {
		if (ioapic_blacklisted())
			printk(" blacklisted board, DISABLING IO-APIC IRQs\n");
		else
			printk(" unlisted board, DISABLING IO-APIC IRQs\n");

		printk(" see Documentation/IO-APIC.txt to enable them\n");
		io_apic_irqs = 0;
	}

	/*
	 * If there are no explicit MP IRQ entries, it's either one of the
	 * default configuration types or we are broken. In both cases it's
	 * fine to set up most of the low 16 IO-APIC pins to ISA defaults.
	 */
	if (!mp_irq_entries) {
		printk("no explicit IRQ entries, using default mptable\n");
		construct_default_ISA_mptable();
	}

	init_IO_APIC_traps();

	/*
	 * Set up the IO-APIC IRQ routing table by parsing the MP-BIOS
	 * mptable:
	 */
	setup_IO_APIC_irqs();
	init_IRQ_SMP();
	check_timer();

	print_IO_APIC();
}
