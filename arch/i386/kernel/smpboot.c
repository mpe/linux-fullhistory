/*
 *	Intel MP v1.1/v1.4 specification compliant parsing routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998, 1999 Ingo Molnar <mingo@redhat.com>
 *
 *	Much of the core SMP work is based on previous work by Thomas Radke, to
 *	whom a great many thanks are extended.
 *
 *	Thanks to Intel for making available several different Pentium,
 *	Pentium Pro and Pentium-II/Xeon MP machines.
 *	Original development of Linux SMP code supported by Caldera.
 *
 *	This code is released under the GNU public license version 2 or
 *	later.
 *
 *	Fixes
 *		Felix Koop	:	NR_CPUS used properly
 *		Jose Renau	:	Handle single CPU case.
 *		Alan Cox	:	By repeated request 8) - Total BogoMIP report.
 *		Greg Wright	:	Fix for kernel stacks panic.
 *		Erich Boleyn	:	MP v1.4 and additional changes.
 *	Matthias Sattler	:	Changes for 2.1 kernel map.
 *	Michel Lespinasse	:	Changes for 2.1 kernel map.
 *	Michael Chastain	:	Change trampoline.S to gnu as.
 *		Alan Cox	:	Dumb bug: 'B' step PPro's are fine
 *		Ingo Molnar	:	Added APIC timers, based on code
 *					from Jose Renau
 *		Alan Cox	:	Added EBDA scanning
 *		Ingo Molnar	:	various cleanups and rewrites
 *		Tigran Aivazian	:	fixed "0.00 in /proc/uptime on SMP" bug.
 *	Maciej W. Rozycki	:	Bits for genuine 82489DX timers
 */

#include <linux/config.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/smp_lock.h>
#include <linux/irq.h>
#include <linux/bootmem.h>

#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <asm/mtrr.h>
#include <asm/pgalloc.h>

/* Set if we find a B stepping CPU			*/
static int smp_b_stepping = 0;

/* Setup configured maximum number of CPUs to activate */
static int max_cpus = -1;
/* 1 if "noapic" boot option passed */
int skip_ioapic_setup = 0;

/* Total count of live CPUs */
int smp_num_cpus = 1;
/* Internal processor count */
static unsigned int num_processors = 1;

/* Have we found an SMP box */
int smp_found_config = 0;

/* Bitmask of physically existing CPUs */
unsigned long cpu_present_map = 0;
/* Bitmask of currently online CPUs */
unsigned long cpu_online_map = 0;

/* which CPU maps to which logical number */
volatile int cpu_number_map[NR_CPUS];
/* which logical number maps to which CPU */
volatile int __cpu_logical_map[NR_CPUS];

static volatile unsigned long cpu_callin_map = 0;
static volatile unsigned long cpu_callout_map = 0;

/* Per CPU bogomips and other parameters */
struct cpuinfo_x86 cpu_data[NR_CPUS];
/* Processor that is doing the boot up */
static unsigned int boot_cpu_id = 0;

/* Set when the idlers are all forked */
int smp_threads_ready = 0;

/*
 * Various Linux-internal data structures created from the
 * MP-table.
 */
int apic_version [NR_CPUS];
int mp_bus_id_to_type [MAX_MP_BUSSES] = { -1, };
extern int nr_ioapics;
extern struct mpc_config_ioapic mp_ioapics [MAX_IO_APICS];
extern int mp_irq_entries;
extern struct mpc_config_intsrc mp_irqs [MAX_IRQ_SOURCES];
extern int mpc_default_type;
int mp_bus_id_to_pci_bus [MAX_MP_BUSSES] = { -1, };
int mp_current_pci_id = 0;
unsigned long mp_lapic_addr = 0;
int pic_mode;

extern void cache_APIC_registers (void);

#define SMP_DEBUG 1

#if SMP_DEBUG
#define dprintk(x...) printk(##x)
#else
#define dprintk(x...)
#endif

/*
 * IA s/w dev Vol 3, Section 7.4
 */
#define APIC_DEFAULT_PHYS_BASE 0xfee00000

/*
 * Setup routine for controlling SMP activation
 *
 * Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 * activation entirely (the MPS table probe still happens, though).
 *
 * Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 * greater than 0, limits the maximum number of CPUs activated in
 * SMP mode to <NUM>.
 */

static int __init nosmp(char *str)
{
	max_cpus = 0;
	return 1;
}

__setup("nosmp", nosmp);

static int __init maxcpus(char *str)
{
	get_option(&str, &max_cpus);
	return 1;
}

__setup("maxcpus=", maxcpus);

/*
 * Intel MP BIOS table parsing routines:
 */

#ifndef CONFIG_X86_VISWS_APIC
/*
 * Checksum an MP configuration block.
 */

static int __init mpf_checksum(unsigned char *mp, int len)
{
	int sum=0;
	while(len--)
		sum+=*mp++;
	return sum&0xFF;
}

/*
 * Processor encoding in an MP configuration block
 */

static char __init *mpc_family(int family,int model)
{
	static char n[32];
	static char *model_defs[]=
	{
		"80486DX","80486DX",
		"80486SX","80486DX/2 or 80487",
		"80486SL","80486SX/2",
		"Unknown","80486DX/2-WB",
		"80486DX/4","80486DX/4-WB"
	};

	switch (family) {
		case 0x04:
			if (model < 10)
				return model_defs[model];
			break;

		case 0x05:
			return("Pentium(tm)");

		case 0x06:
			return("Pentium(tm) Pro");

		case 0x0F:
			if (model == 0x0F)
				return("Special controller");
	}
	sprintf(n,"Unknown CPU [%d:%d]",family, model);
	return n;
}

static void __init MP_processor_info (struct mpc_config_processor *m)
{
	int ver;

	if (!(m->mpc_cpuflag & CPU_ENABLED))
		return;

	printk("Processor #%d %s APIC version %d\n",
		m->mpc_apicid,
		mpc_family(	(m->mpc_cpufeature & CPU_FAMILY_MASK)>>8 ,
				(m->mpc_cpufeature & CPU_MODEL_MASK)>>4),
		m->mpc_apicver);

#ifdef SMP_DEBUG
	if (m->mpc_featureflag&(1<<0))
		printk("    Floating point unit present.\n");
	if (m->mpc_featureflag&(1<<7))
		printk("    Machine Exception supported.\n");
	if (m->mpc_featureflag&(1<<8))
		printk("    64 bit compare & exchange supported.\n");
	if (m->mpc_featureflag&(1<<9))
		printk("    Internal APIC present.\n");
#endif

	if (m->mpc_cpuflag & CPU_BOOTPROCESSOR) {
		dprintk("    Bootup CPU\n");
		boot_cpu_id = m->mpc_apicid;
	} else
		/* Boot CPU already counted */
		num_processors++;

	if (m->mpc_apicid > NR_CPUS) {
		printk("Processor #%d unused. (Max %d processors).\n",
			m->mpc_apicid, NR_CPUS);
		return;
	}
	ver = m->mpc_apicver;

	cpu_present_map |= (1<<m->mpc_apicid);
	/*
	 * Validate version
	 */
	if (ver == 0x0) {
		printk("BIOS bug, APIC version is 0 for CPU#%d! fixing up to 0x10. (tell your hw vendor)\n", m->mpc_apicid);
		ver = 0x10;
	}
	apic_version[m->mpc_apicid] = ver;
}

static void __init MP_bus_info (struct mpc_config_bus *m)
{
	char str[7];

	memcpy(str, m->mpc_bustype, 6);
	str[6] = 0;
	dprintk("Bus #%d is %s\n", m->mpc_busid, str);

	if (strncmp(str, "ISA", 3) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_ISA;
	} else {
	if (strncmp(str, "EISA", 4) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_EISA;
	} else {
	if (strncmp(str, "PCI", 3) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_PCI;
		mp_bus_id_to_pci_bus[m->mpc_busid] = mp_current_pci_id;
		mp_current_pci_id++;
	} else {
		printk("Unknown bustype %s\n", str);
		panic("cannot handle bus - mail to linux-smp@vger.rutgers.edu");
	} } }
}

static void __init MP_ioapic_info (struct mpc_config_ioapic *m)
{
	if (!(m->mpc_flags & MPC_APIC_USABLE))
		return;

	printk("I/O APIC #%d Version %d at 0x%lX.\n",
		m->mpc_apicid, m->mpc_apicver, m->mpc_apicaddr);
	if (nr_ioapics >= MAX_IO_APICS) {
		printk("Max # of I/O APICs (%d) exceeded (found %d).\n",
			MAX_IO_APICS, nr_ioapics);
		panic("Recompile kernel with bigger MAX_IO_APICS!.\n");
	}
	mp_ioapics[nr_ioapics] = *m;
	nr_ioapics++;
}

static void __init MP_intsrc_info (struct mpc_config_intsrc *m)
{
	mp_irqs [mp_irq_entries] = *m;
	if (++mp_irq_entries == MAX_IRQ_SOURCES)
		panic("Max # of irq sources exceeded!!\n");
}

static void __init MP_lintsrc_info (struct mpc_config_lintsrc *m)
{
	/*
	 * Well it seems all SMP boards in existence
	 * use ExtINT/LVT1 == LINT0 and
	 * NMI/LVT2 == LINT1 - the following check
	 * will show us if this assumptions is false.
	 * Until then we do not have to add baggage.
	 */
	if ((m->mpc_irqtype == mp_ExtINT) &&
		(m->mpc_destapiclint != 0))
			BUG();
	if ((m->mpc_irqtype == mp_NMI) &&
		(m->mpc_destapiclint != 1))
			BUG();
}

/*
 * Read/parse the MPC
 */

static int __init smp_read_mpc(struct mp_config_table *mpc)
{
	char str[16];
	int count=sizeof(*mpc);
	unsigned char *mpt=((unsigned char *)mpc)+count;

	if (memcmp(mpc->mpc_signature,MPC_SIGNATURE,4))
	{
		panic("SMP mptable: bad signature [%c%c%c%c]!\n",
			mpc->mpc_signature[0],
			mpc->mpc_signature[1],
			mpc->mpc_signature[2],
			mpc->mpc_signature[3]);
		return 1;
	}
	if (mpf_checksum((unsigned char *)mpc,mpc->mpc_length))
	{
		panic("SMP mptable: checksum error!\n");
		return 1;
	}
	if (mpc->mpc_spec!=0x01 && mpc->mpc_spec!=0x04)
	{
		printk("Bad Config Table version (%d)!!\n",mpc->mpc_spec);
		return 1;
	}
	memcpy(str,mpc->mpc_oem,8);
	str[8]=0;
	printk("OEM ID: %s ",str);

	memcpy(str,mpc->mpc_productid,12);
	str[12]=0;
	printk("Product ID: %s ",str);

	printk("APIC at: 0x%lX\n",mpc->mpc_lapic);

	/* save the local APIC address, it might be non-default */
	mp_lapic_addr = mpc->mpc_lapic;

	/*
	 *	Now process the configuration blocks.
	 */
	while (count < mpc->mpc_length) {
		switch(*mpt) {
			case MP_PROCESSOR:
			{
				struct mpc_config_processor *m=
					(struct mpc_config_processor *)mpt;
				MP_processor_info(m);
				mpt += sizeof(*m);
				count += sizeof(*m);
				break;
			}
			case MP_BUS:
			{
				struct mpc_config_bus *m=
					(struct mpc_config_bus *)mpt;
				MP_bus_info(m);
				mpt += sizeof(*m);
				count += sizeof(*m);
				break;
			}
			case MP_IOAPIC:
			{
				struct mpc_config_ioapic *m=
					(struct mpc_config_ioapic *)mpt;
				MP_ioapic_info(m);
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			case MP_INTSRC:
			{
				struct mpc_config_intsrc *m=
					(struct mpc_config_intsrc *)mpt;

				MP_intsrc_info(m);
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			case MP_LINTSRC:
			{
				struct mpc_config_lintsrc *m=
					(struct mpc_config_lintsrc *)mpt;
				MP_lintsrc_info(m);
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
		}
	}
	return num_processors;
}

/*
 * Scan the memory blocks for an SMP configuration block.
 */
static int __init smp_get_mpf(struct intel_mp_floating *mpf)
{
	printk("Intel MultiProcessor Specification v1.%d\n", mpf->mpf_specification);
	if (mpf->mpf_feature2 & (1<<7)) {
		printk("    IMCR and PIC compatibility mode.\n");
		pic_mode = 1;
	} else {
		printk("    Virtual Wire compatibility mode.\n");
		pic_mode = 0;
	}
	smp_found_config = 1;
	/*
	 * default CPU id - if it's different in the mptable
	 * then we change it before first using it.
	 */
	boot_cpu_id = 0;
	/*
	 * Now see if we need to read further.
	 */
	if (mpf->mpf_feature1 != 0) {
		/*
		 * local APIC has default address
		 */
		mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;

		/*
		 * 2 CPUs, numbered 0 & 1.
		 */
		cpu_present_map = 3;
		num_processors = 2;

		nr_ioapics = 1;
		mp_ioapics[0].mpc_apicaddr = 0xFEC00000;
		/*
		 * Save the default type number, we
		 * need it later to set the IO-APIC
		 * up properly:
		 */
		mpc_default_type = mpf->mpf_feature1;

		printk("Bus #0 is ");
	}

	switch (mpf->mpf_feature1) {
		case 1:
		case 5:
			printk("ISA\n");
			break;
		case 2:
			printk("EISA with no IRQ0 and no IRQ13 DMA chaining\n");
			break;
		case 6:
		case 3:
			printk("EISA\n");
			break;
		case 4:
		case 7:
			printk("MCA\n");
			break;
		case 0:
			if (!mpf->mpf_physptr)
				BUG();
			break;
		default:
			printk("???\nUnknown standard configuration %d\n",
				mpf->mpf_feature1);
			return 1;
	}
	if (mpf->mpf_feature1 > 4) {
		printk("Bus #1 is PCI\n");

		/*
		 * Set local APIC version to the integrated form.
		 * It's initialized to zero otherwise, representing
		 * a discrete 82489DX.
		 */
		apic_version[0] = 0x10;
		apic_version[1] = 0x10;
	}
	/*
	 * Read the physical hardware table. Anything here will override the
	 * defaults.
	 */
	if (mpf->mpf_physptr)
		smp_read_mpc((void *)mpf->mpf_physptr);

	__cpu_logical_map[0] = boot_cpu_id;
	global_irq_holder = boot_cpu_id;
	current->processor = boot_cpu_id;

	printk("Processors: %d\n", num_processors);
	/*
	 * Only use the first configuration found.
	 */
	return 1;
}

static int __init smp_scan_config(unsigned long base, unsigned long length)
{
	unsigned long *bp = phys_to_virt(base);
	struct intel_mp_floating *mpf;

	dprintk("Scan SMP from %p for %ld bytes.\n", bp,length);
	if (sizeof(*mpf) != 16)
		printk("Error: MPF size\n");

	while (length > 0) {
		mpf = (struct intel_mp_floating *)bp;
		if ((*bp == SMP_MAGIC_IDENT) &&
			(mpf->mpf_length == 1) &&
			!mpf_checksum((unsigned char *)bp, 16) &&
			((mpf->mpf_specification == 1)
				|| (mpf->mpf_specification == 4)) ) {

			printk("found SMP MP-table at %08ld\n",
						virt_to_phys(mpf));
			smp_get_mpf(mpf);
			return 1;
		}
		bp += 4;
		length -= 16;
	}
	return 0;
}

void __init init_intel_smp (void)
{
	unsigned int address;

	/*
	 * FIXME: Linux assumes you have 640K of base ram..
	 * this continues the error...
	 *
	 * 1) Scan the bottom 1K for a signature
	 * 2) Scan the top 1K of base RAM
	 * 3) Scan the 64K of bios
	 */
	if (smp_scan_config(0x0,0x400) ||
		smp_scan_config(639*0x400,0x400) ||
			smp_scan_config(0xF0000,0x10000))
		return;
	/*
	 * If it is an SMP machine we should know now, unless the
	 * configuration is in an EISA/MCA bus machine with an
	 * extended bios data area.
	 *
	 * there is a real-mode segmented pointer pointing to the
	 * 4K EBDA area at 0x40E, calculate and scan it here.
	 *
	 * NOTE! There are Linux loaders that will corrupt the EBDA
	 * area, and as such this kind of SMP config may be less
	 * trustworthy, simply because the SMP table may have been
	 * stomped on during early boot. These loaders are buggy and
	 * should be fixed.
	 */

	address = *(unsigned short *)phys_to_virt(0x40E);
	address <<= 4;
	smp_scan_config(address, 0x1000);
	if (smp_found_config)
		printk(KERN_WARNING "WARNING: MP table in the EBDA can be UNSAFE, contact linux-smp@vger.rutgers.edu if you experience SMP problems!\n");
}

#else

/*
 * The Visual Workstation is Intel MP compliant in the hardware
 * sense, but it doesnt have a BIOS(-configuration table).
 * No problem for Linux.
 */
void __init init_visws_smp(void)
{
	smp_found_config = 1;

	cpu_present_map |= 2; /* or in id 1 */
	apic_version[1] |= 0x10; /* integrated APIC */
	apic_version[0] |= 0x10;

	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;
}

#endif

/*
 * - Intel MP Configuration Table
 * - or SGI Visual Workstation configuration
 */
void __init init_smp_config (void)
{
#ifndef CONFIG_VISWS
	init_intel_smp();
#else
	init_visws_smp();
#endif
}



/*
 * Trampoline 80x86 program as an array.
 */

extern unsigned char trampoline_data [];
extern unsigned char trampoline_end  [];
static unsigned char *trampoline_base;

/*
 * Currently trivial. Write the real->protected mode
 * bootstrap into the page concerned. The caller
 * has made sure it's suitably aligned.
 */

static unsigned long __init setup_trampoline(void)
{
	memcpy(trampoline_base, trampoline_data, trampoline_end - trampoline_data);
	return virt_to_phys(trampoline_base);
}

/*
 * We are called very early to get the low memory for the
 * SMP bootup trampoline page.
 */
void __init smp_alloc_memory(void)
{
	trampoline_base = (void *) alloc_bootmem_low_pages(PAGE_SIZE);
	/*
	 * Has to be in very low memory so we can execute
	 * real-mode AP code.
	 */
	if (__pa(trampoline_base) >= 0x9F000)
		BUG();
}

/*
 * The bootstrap kernel entry code has set these up. Save them for
 * a given CPU
 */

void __init smp_store_cpu_info(int id)
{
	struct cpuinfo_x86 *c = cpu_data + id;

	*c = boot_cpu_data;
	c->pte_quick = 0;
	c->pmd_quick = 0;
	c->pgd_quick = 0;
	c->pgtable_cache_sz = 0;
	identify_cpu(c);
	/*
	 * Mask B, Pentium, but not Pentium MMX
	 */
	if (c->x86_vendor == X86_VENDOR_INTEL &&
	    c->x86 == 5 &&
	    c->x86_mask >= 1 && c->x86_mask <= 4 &&
	    c->x86_model <= 3)
		/*
		 * Remember we have B step Pentia with bugs
		 */
		smp_b_stepping = 1;
}

/*
 * Architecture specific routine called by the kernel just before init is
 * fired off. This allows the BP to have everything in order [we hope].
 * At the end of this all the APs will hit the system scheduling and off
 * we go. Each AP will load the system gdt's and jump through the kernel
 * init into idle(). At this point the scheduler will one day take over
 * and give them jobs to do. smp_callin is a standard routine
 * we use to track CPUs as they power up.
 */

static atomic_t smp_commenced = ATOMIC_INIT(0);

void __init smp_commence(void)
{
	/*
	 * Lets the callins below out of their loop.
	 */
	dprintk("Setting commenced=1, go go go\n");

	wmb();
	atomic_set(&smp_commenced,1);
}

extern void __error_in_io_apic_c(void);


int get_maxlvt(void)
{
	unsigned int v, ver, maxlvt;

	v = apic_read(APIC_LVR);
	ver = GET_APIC_VERSION(v);
	/* 82489DXs do not report # of LVT entries. */
	maxlvt = APIC_INTEGRATED(ver) ? GET_APIC_MAXLVT(v) : 2;
	return maxlvt;
}

void disable_local_APIC (void)
{
	unsigned long value;
        int maxlvt;

	/*
	 * Disable APIC
	 */
 	value = apic_read(APIC_SPIV);
 	value &= ~(1<<8);
 	apic_write(APIC_SPIV,value);

	/*
	 * Clean APIC state for other OSs:
	 */
 	value = apic_read(APIC_SPIV);
 	value &= ~(1<<8);
 	apic_write(APIC_SPIV,value);
	maxlvt = get_maxlvt();
	apic_write_around(APIC_LVTT, 0x00010000);
	apic_write_around(APIC_LVT0, 0x00010000);
	apic_write_around(APIC_LVT1, 0x00010000);
	if (maxlvt >= 3)
		apic_write_around(APIC_LVTERR, 0x00010000);
	if (maxlvt >= 4)
		apic_write_around(APIC_LVTPC, 0x00010000);
}

void __init setup_local_APIC (void)
{
	unsigned long value, ver, maxlvt;

	if ((SPURIOUS_APIC_VECTOR & 0x0f) != 0x0f)
		__error_in_io_apic_c();

 	value = apic_read(APIC_SPIV);
	/*
	 * Enable APIC
	 */
 	value |= (1<<8);

	/*
	 * Some unknown Intel IO/APIC (or APIC) errata is biting us with
	 * certain networking cards. If high frequency interrupts are
	 * happening on a particular IOAPIC pin, plus the IOAPIC routing
	 * entry is masked/unmasked at a high rate as well then sooner or
	 * later IOAPIC line gets 'stuck', no more interrupts are received
	 * from the device. If focus CPU is disabled then the hang goes
	 * away, oh well :-(
	 *
	 * [ This bug can be reproduced easily with a level-triggered
	 *   PCI Ne2000 networking cards and PII/PIII processors, dual
	 *   BX chipset. ]
	 */
#if 0
	/* Enable focus processor (bit==0) */
 	value &= ~(1<<9);
#else
	/* Disable focus processor (bit==1) */
	value |= (1<<9);
#endif
	/*
	 * Set spurious IRQ vector
	 */
	value |= SPURIOUS_APIC_VECTOR;
 	apic_write(APIC_SPIV,value);

	/*
	 * Set up LVT0, LVT1:
	 *
	 * set up through-local-APIC on the BP's LINT0. This is not
	 * strictly necessery in pure symmetric-IO mode, but sometimes
	 * we delegate interrupts to the 8259A.
	 */
	if (hard_smp_processor_id() == boot_cpu_id) {
		value = 0x00000700;
		printk("enabled ExtINT on CPU#%d\n", hard_smp_processor_id());
	} else {
		value = 0x00010700;
		printk("masked ExtINT on CPU#%d\n", hard_smp_processor_id());
	}
 	apic_write_around(APIC_LVT0,value);

	/*
	 * only the BP should see the LINT1 NMI signal, obviously.
	 */
	if (hard_smp_processor_id() == boot_cpu_id)
		value = 0x00000400;		// unmask NMI
	else
		value = 0x00010400;		// mask NMI
 	apic_write_around(APIC_LVT1,value);

	value = apic_read(APIC_LVR);
	ver = GET_APIC_VERSION(value);
	if (APIC_INTEGRATED(ver)) {		/* !82489DX */
		maxlvt = get_maxlvt();
		/*
		 * Due to the Pentium erratum 3AP.
		 */
		if (maxlvt > 3) {
			apic_readaround(APIC_SPIV); // not strictly necessery
			apic_write(APIC_ESR, 0);
		}
		value = apic_read(APIC_ESR);
		printk("ESR value before enabling vector: %08lx\n", value);

		value = apic_read(APIC_LVTERR);
		value = ERROR_APIC_VECTOR;      // enables sending errors
		apic_write(APIC_LVTERR,value);
		/*
		 * spec says clear errors after enabling vector.
		 */
		if (maxlvt != 3) {
			apic_readaround(APIC_SPIV);
			apic_write(APIC_ESR, 0);
		}
		value = apic_read(APIC_ESR);
		printk("ESR value after enabling vector: %08lx\n", value);
	} else
		printk("No ESR for 82489DX.\n");

	/*
	 * Set Task Priority to 'accept all'. We never change this
	 * later on.
	 */
 	value = apic_read(APIC_TASKPRI);
 	value &= ~APIC_TPRI_MASK;
 	apic_write(APIC_TASKPRI,value);

	/*
	 * Set up the logical destination ID and put the
	 * APIC into flat delivery mode.
	 */
 	value = apic_read(APIC_LDR);
	value &= ~APIC_LDR_MASK;
	value |= (1<<(smp_processor_id()+24));
 	apic_write(APIC_LDR,value);

 	value = apic_read(APIC_DFR);
	value |= SET_APIC_DFR(0xf);
 	apic_write(APIC_DFR, value);
}

void __init init_smp_mappings(void)
{
	unsigned long apic_phys;

	if (smp_found_config) {
		apic_phys = mp_lapic_addr;
	} else {
		/*
		 * set up a fake all zeroes page to simulate the
		 * local APIC and another one for the IO-APIC. We
		 * could use the real zero-page, but it's safer
		 * this way if some buggy code writes to this page ...
		 */
		apic_phys = (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
		apic_phys = __pa(apic_phys);
	}
	set_fixmap_nocache(FIX_APIC_BASE, apic_phys);
	dprintk("mapped APIC to %08lx (%08lx)\n", APIC_BASE, apic_phys);

#ifdef CONFIG_X86_IO_APIC
	{
		unsigned long ioapic_phys, idx = FIX_IO_APIC_BASE_0;
		int i;

		for (i = 0; i < nr_ioapics; i++) {
			if (smp_found_config) {
				ioapic_phys = mp_ioapics[i].mpc_apicaddr;
			} else {
				ioapic_phys = (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
				ioapic_phys = __pa(ioapic_phys);
			}
			set_fixmap_nocache(idx, ioapic_phys);
			dprintk("mapped IOAPIC to %08lx (%08lx)\n",
					__fix_to_virt(idx), ioapic_phys);
			idx++;
		}
	}
#endif
}

/*
 * TSC synchronization.
 *
 * We first check wether all CPUs have their TSC's synchronized,
 * then we print a warning if not, and always resync.
 */

static atomic_t tsc_start_flag = ATOMIC_INIT(0);
static atomic_t tsc_count_start = ATOMIC_INIT(0);
static atomic_t tsc_count_stop = ATOMIC_INIT(0);
static unsigned long long tsc_values[NR_CPUS] = { 0, };

#define NR_LOOPS 5

extern unsigned long fast_gettimeoffset_quotient;

/*
 * accurate 64-bit/32-bit division, expanded to 32-bit divisions and 64-bit
 * multiplication. Not terribly optimized but we need it at boot time only
 * anyway.
 *
 * result == a / b
 *	== (a1 + a2*(2^32)) / b
 *	== a1/b + a2*(2^32/b)
 *	== a1/b + a2*((2^32-1)/b) + a2/b + (a2*((2^32-1) % b))/b
 *		    ^---- (this multiplication can overflow)
 */

static unsigned long long div64 (unsigned long long a, unsigned long b0)
{
	unsigned int a1, a2;
	unsigned long long res;

	a1 = ((unsigned int*)&a)[0];
	a2 = ((unsigned int*)&a)[1];

	res = a1/b0 +
		(unsigned long long)a2 * (unsigned long long)(0xffffffff/b0) +
		a2 / b0 +
		(a2 * (0xffffffff % b0)) / b0;

	return res;
}

static void __init synchronize_tsc_bp (void)
{
	int i;
	unsigned long long t0;
	unsigned long long sum, avg;
	long long delta;
	unsigned long one_usec;
	int buggy = 0;

	printk("checking TSC synchronization across CPUs: ");

	one_usec = ((1<<30)/fast_gettimeoffset_quotient)*(1<<2);

	atomic_set(&tsc_start_flag, 1);
	wmb();

	/*
	 * We loop a few times to get a primed instruction cache,
	 * then the last pass is more or less synchronized and
	 * the BP and APs set their cycle counters to zero all at
	 * once. This reduces the chance of having random offsets
	 * between the processors, and guarantees that the maximum
	 * delay between the cycle counters is never bigger than
	 * the latency of information-passing (cachelines) between
	 * two CPUs.
	 */
	for (i = 0; i < NR_LOOPS; i++) {
		/*
		 * all APs synchronize but they loop on '== num_cpus'
		 */
		while (atomic_read(&tsc_count_start) != smp_num_cpus-1) mb();
		atomic_set(&tsc_count_stop, 0);
		wmb();
		/*
		 * this lets the APs save their current TSC:
		 */
		atomic_inc(&tsc_count_start);

		rdtscll(tsc_values[smp_processor_id()]);
		/*
		 * We clear the TSC in the last loop:
		 */
		if (i == NR_LOOPS-1)
			write_tsc(0, 0);

		/*
		 * Wait for all APs to leave the synchronization point:
		 */
		while (atomic_read(&tsc_count_stop) != smp_num_cpus-1) mb();
		atomic_set(&tsc_count_start, 0);
		wmb();
		atomic_inc(&tsc_count_stop);
	}

	sum = 0;
	for (i = 0; i < NR_CPUS; i++) {
		if (!(cpu_online_map & (1 << i)))
			continue;

		t0 = tsc_values[i];
		sum += t0;
	}
	avg = div64(sum, smp_num_cpus);

	sum = 0;
	for (i = 0; i < NR_CPUS; i++) {
		if (!(cpu_online_map & (1 << i)))
			continue;

		delta = tsc_values[i] - avg;
		if (delta < 0)
			delta = -delta;
		/*
		 * We report bigger than 2 microseconds clock differences.
		 */
		if (delta > 2*one_usec) {
			long realdelta;
			if (!buggy) {
				buggy = 1;
				printk("\n");
			}
			realdelta = div64(delta, one_usec);
			if (tsc_values[i] < avg)
				realdelta = -realdelta;

			printk("BIOS BUG: CPU#%d improperly initialized, has %ld usecs TSC skew! FIXED.\n",
				i, realdelta);
		}

		sum += delta;
	}
	if (!buggy)
		printk("passed.\n");
}

static void __init synchronize_tsc_ap (void)
{
	int i;

	/*
	 * smp_num_cpus is not necessarily known at the time
	 * this gets called, so we first wait for the BP to
	 * finish SMP initialization:
	 */
	while (!atomic_read(&tsc_start_flag)) mb();

	for (i = 0; i < NR_LOOPS; i++) {
		atomic_inc(&tsc_count_start);
		while (atomic_read(&tsc_count_start) != smp_num_cpus) mb();

		rdtscll(tsc_values[smp_processor_id()]);
		if (i == NR_LOOPS-1)
			write_tsc(0, 0);

		atomic_inc(&tsc_count_stop);
		while (atomic_read(&tsc_count_stop) != smp_num_cpus) mb();
	}
}
#undef NR_LOOPS

extern void calibrate_delay(void);

void __init smp_callin(void)
{
	int cpuid;
	unsigned long timeout;

	/*
	 * (This works even if the APIC is not enabled.)
	 */
	cpuid = GET_APIC_ID(apic_read(APIC_ID));

	dprintk("CPU#%d waiting for CALLOUT\n", cpuid);

	/*
	 * STARTUP IPIs are fragile beasts as they might sometimes
	 * trigger some glue motherboard logic. Complete APIC bus
	 * silence for 1 second, this overestimates the time the
	 * boot CPU is spending to send the up to 2 STARTUP IPIs
	 * by a factor of two. This should be enough.
	 */

	/*
	 * Waiting 2s total for startup (udelay is not yet working)
	 */
	timeout = jiffies + 2*HZ;
	while (time_before(jiffies, timeout)) {
		/*
		 * Has the boot CPU finished it's STARTUP sequence?
		 */
		if (test_bit(cpuid, &cpu_callout_map))
			break;
	}

	if (!time_before(jiffies, timeout)) {
		printk("BUG: CPU%d started up but did not get a callout!\n",
			cpuid);
		BUG();
	}

	/*
	 * the boot CPU has finished the init stage and is spinning
	 * on callin_map until we finish. We are free to set up this
	 * CPU, first the APIC. (this is probably redundant on most
	 * boards)
	 */

	dprintk("CALLIN, before setup_local_APIC().\n");
	setup_local_APIC();

	sti();

#ifdef CONFIG_MTRR
	/*
	 * Must be done before calibration delay is computed
	 */
	mtrr_init_secondary_cpu ();
#endif
	/*
	 * Get our bogomips.
	 */
	calibrate_delay();
	dprintk("Stack at about %p\n",&cpuid);

	/*
	 * Save our processor parameters
	 */
 	smp_store_cpu_info(cpuid);

	/*
	 * Allow the master to continue.
	 */
	set_bit(cpuid, &cpu_callin_map);

	/*
	 *      Synchronize the TSC with the BP
	 */
	if (cpu_has_tsc)
		synchronize_tsc_ap ();
}

int cpucount = 0;

extern int cpu_idle(void);

/*
 * Activate a secondary processor.
 */
int __init start_secondary(void *unused)
{
	/*
	 * Dont put anything before smp_callin(), SMP
	 * booting is too fragile that we want to limit the
	 * things done here to the most necessary things.
	 */
	cpu_init();
	smp_callin();
	while (!atomic_read(&smp_commenced))
		/* nothing */ ;
	/*
	 * low-memory mappings have been cleared, flush them from
	 * the local TLBs too.
	 */
	local_flush_tlb();

	return cpu_idle();
}

/*
 * Everything has been set up for the secondary
 * CPUs - they just need to reload everything
 * from the task structure
 * This function must not return.
 */
void __init initialize_secondary(void)
{
	/*
	 * We don't actually need to load the full TSS,
	 * basically just the stack pointer and the eip.
	 */

	asm volatile(
		"movl %0,%%esp\n\t"
		"jmp *%1"
		:
		:"r" (current->thread.esp),"r" (current->thread.eip));
}

extern struct {
	void * esp;
	unsigned short ss;
} stack_start;

static int __init fork_by_hand(void)
{
	struct pt_regs regs;
	/*
	 * don't care about the eip and regs settings since
	 * we'll never reschedule the forked task.
	 */
	return do_fork(CLONE_VM|CLONE_PID, 0, &regs);
}

static void __init do_boot_cpu(int i)
{
	unsigned long cfg;
	struct task_struct *idle;
	unsigned long send_status, accept_status;
	int timeout, num_starts, j;
	unsigned long start_eip;

	cpucount++;
	/*
	 * We can't use kernel_thread since we must avoid to
	 * reschedule the child.
	 */
	if (fork_by_hand() < 0)
		panic("failed fork for CPU %d", i);

	/*
	 * We remove it from the pidhash and the runqueue
	 * once we got the process:
	 */
	idle = init_task.prev_task;
	if (!idle)
		panic("No idle process for CPU %d", i);

	idle->processor = i;
	__cpu_logical_map[cpucount] = i;
	cpu_number_map[i] = cpucount;
	idle->has_cpu = 1; /* we schedule the first task manually */
	idle->thread.eip = (unsigned long) start_secondary;

	del_from_runqueue(idle);
	unhash_process(idle);
	init_tasks[cpucount] = idle;

	/* start_eip had better be page-aligned! */
	start_eip = setup_trampoline();

	/* So we see what's up   */
	printk("Booting processor %d eip %lx\n", i, start_eip);
	stack_start.esp = (void *) (1024 + PAGE_SIZE + (char *)idle);

	/*
	 * This grunge runs the startup process for
	 * the targeted processor.
	 */

	dprintk("Setting warm reset code and vector.\n");

	CMOS_WRITE(0xa, 0xf);
	local_flush_tlb();
	dprintk("1.\n");
	*((volatile unsigned short *) phys_to_virt(0x469)) = start_eip >> 4;
	dprintk("2.\n");
	*((volatile unsigned short *) phys_to_virt(0x467)) = start_eip & 0xf;
	dprintk("3.\n");

	/*
	 * Be paranoid about clearing APIC errors.
	 */

	if (APIC_INTEGRATED(apic_version[i])) {
		apic_readaround(APIC_SPIV);
		apic_write(APIC_ESR, 0);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
	}

	/*
	 * Status is now clean
	 */
	send_status = 	0;
	accept_status = 0;

	/*
	 * Starting actual IPI sequence...
	 */

	dprintk("Asserting INIT.\n");

	/*
	 * Turn INIT on
	 */
	cfg = apic_read(APIC_ICR2);
	cfg &= 0x00FFFFFF;

	/*
	 * Target chip
	 */
	apic_write(APIC_ICR2, cfg | SET_APIC_DEST_FIELD(i));

	/*
	 * Send IPI
	 */
	cfg = apic_read(APIC_ICR);
	cfg &= ~0xCDFFF;
	cfg |= (APIC_DEST_LEVELTRIG | APIC_DEST_ASSERT | APIC_DEST_DM_INIT);
	apic_write(APIC_ICR, cfg);

	udelay(200);
	dprintk("Deasserting INIT.\n");

	/* Target chip */
	cfg = apic_read(APIC_ICR2);
	cfg &= 0x00FFFFFF;
	apic_write(APIC_ICR2, cfg|SET_APIC_DEST_FIELD(i));

	/* Send IPI */
	cfg = apic_read(APIC_ICR);
	cfg &= ~0xCDFFF;
	cfg |= (APIC_DEST_LEVELTRIG | APIC_DEST_DM_INIT);
	apic_write(APIC_ICR, cfg);

	/*
	 * Should we send STARTUP IPIs ?
	 *
	 * Determine this based on the APIC version.
	 * If we don't have an integrated APIC, don't
	 * send the STARTUP IPIs.
	 */

	if (APIC_INTEGRATED(apic_version[i]))
		num_starts = 2;
	else
		num_starts = 0;

	/*
	 * Run STARTUP IPI loop.
	 */

	for (j = 1; j <= num_starts; j++) {
		dprintk("Sending STARTUP #%d.\n",j);
		apic_readaround(APIC_SPIV);
		apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
		dprintk("After apic_write.\n");

		/*
		 * STARTUP IPI
		 */

		/* Target chip */
		cfg = apic_read(APIC_ICR2);
		cfg &= 0x00FFFFFF;
		apic_write(APIC_ICR2, cfg|SET_APIC_DEST_FIELD(i));

		/* Boot on the stack */
		cfg = apic_read(APIC_ICR);
		cfg &= ~0xCDFFF;
		cfg |= (APIC_DEST_DM_STARTUP | (start_eip >> 12));

		/* Kick the second */
		apic_write(APIC_ICR, cfg);

		dprintk("Startup point 1.\n");

		dprintk("Waiting for send to finish...\n");
		timeout = 0;
		do {
			dprintk("+");
			udelay(100);
			send_status = apic_read(APIC_ICR) & 0x1000;
		} while (send_status && (timeout++ < 1000));

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(200);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
		if (send_status || accept_status)
			break;
	}
	dprintk("After Startup.\n");

	if (send_status)
		printk("APIC never delivered???\n");
	if (accept_status)
		printk("APIC delivery error (%lx).\n", accept_status);

	if (!send_status && !accept_status) {
		/*
		 * allow APs to start initializing.
		 */
		dprintk("Before Callout %d.\n", i);
		set_bit(i, &cpu_callout_map);
		dprintk("After Callout %d.\n", i);

		/*
		 * Wait 5s total for a response
		 */
		for (timeout = 0; timeout < 50000; timeout++) {
			if (test_bit(i, &cpu_callin_map))
				break;	/* It has booted */
			udelay(100);
		}

		if (test_bit(i, &cpu_callin_map)) {
			/* number CPUs logically, starting from 1 (BSP is 0) */
			printk("OK.\n");
			printk("CPU%d: ", i);
			print_cpu_info(&cpu_data[i]);
		} else {
			if (*((volatile unsigned char *)phys_to_virt(8192))
					== 0xA5) /* trampoline code not run */
				printk("Stuck ??\n");
			else
				printk("CPU booted but not responding.\n");
		}
		dprintk("CPU has booted.\n");
	} else {
		__cpu_logical_map[cpucount] = -1;
		cpu_number_map[i] = -1;
		cpucount--;
	}

	/* mark "stuck" area as not stuck */
	*((volatile unsigned long *)phys_to_virt(8192)) = 0;
}

cycles_t cacheflush_time;
extern unsigned long cpu_hz;

static void smp_tune_scheduling (void)
{
	unsigned long cachesize;
	/*
	 * Rough estimation for SMP scheduling, this is the number of
	 * cycles it takes for a fully memory-limited process to flush
	 * the SMP-local cache.
	 *
	 * (For a P5 this pretty much means we will choose another idle
	 *  CPU almost always at wakeup time (this is due to the small
	 *  L1 cache), on PIIs it's around 50-100 usecs, depending on
	 *  the cache size)
	 */

	if (!cpu_hz) {
		/*
		 * this basically disables processor-affinity
		 * scheduling on SMP without a TSC.
		 */
		cacheflush_time = 0;
		return;
	} else {
		cachesize = boot_cpu_data.x86_cache_size;
		if (cachesize == -1)
			cachesize = 8; /* Pentiums */

		cacheflush_time = cpu_hz/1024*cachesize/5000;
	}

	printk("per-CPU timeslice cutoff: %ld.%02ld usecs.\n",
		(long)cacheflush_time/(cpu_hz/1000000),
		((long)cacheflush_time*100/(cpu_hz/1000000)) % 100);
}

/*
 * Cycle through the processors sending APIC IPIs to boot each.
 */

extern int prof_multiplier[NR_CPUS];
extern int prof_old_multiplier[NR_CPUS];
extern int prof_counter[NR_CPUS];

void __init smp_boot_cpus(void)
{
	int i;

#ifdef CONFIG_MTRR
	/*  Must be done before other processors booted  */
	mtrr_init_boot_cpu ();
#endif
	/*
	 * Initialize the logical to physical CPU number mapping
	 * and the per-CPU profiling counter/multiplier
	 */

	for (i = 0; i < NR_CPUS; i++) {
		cpu_number_map[i] = -1;
		prof_counter[i] = 1;
		prof_old_multiplier[i] = 1;
		prof_multiplier[i] = 1;
	}

	/*
	 * Setup boot CPU information
	 */

	smp_store_cpu_info(boot_cpu_id); /* Final full version of the data */
	smp_tune_scheduling();
	printk("CPU%d: ", boot_cpu_id);
	print_cpu_info(&cpu_data[boot_cpu_id]);

	/*
	 * not necessary because the MP table should list the boot
	 * CPU too, but we do it for the sake of robustness anyway.
	 * (and for the case when a non-SMP board boots an SMP kernel)
	 */
	cpu_present_map |= (1 << hard_smp_processor_id());

	cpu_number_map[boot_cpu_id] = 0;

	init_idle();

	/*
	 * If we couldnt find an SMP configuration at boot time,
	 * get out of here now!
	 */

	if (!smp_found_config) {
		printk(KERN_NOTICE "SMP motherboard not detected. Using dummy APIC emulation.\n");
#ifndef CONFIG_VISWS
		io_apic_irqs = 0;
#endif
		cpu_online_map = cpu_present_map;
		smp_num_cpus = 1;
		goto smp_done;
	}

	/*
	 * If SMP should be disabled, then really disable it!
	 */

	if (!max_cpus) {
		smp_found_config = 0;
		printk(KERN_INFO "SMP mode deactivated, forcing use of dummy APIC emulation.\n");
	}

#ifdef SMP_DEBUG
	{
		int reg;

		/*
		 * This is to verify that we're looking at
		 * a real local APIC.  Check these against
		 * your board if the CPUs aren't getting
		 * started for no apparent reason.
		 */

		reg = apic_read(APIC_LVR);
		dprintk("Getting VERSION: %x\n", reg);

		apic_write(APIC_LVR, 0);
		reg = apic_read(APIC_LVR);
		dprintk("Getting VERSION: %x\n", reg);

		/*
		 * The two version reads above should print the same
		 * NON-ZERO!!! numbers.  If the second one is zero,
		 * there is a problem with the APIC write/read
		 * definitions.
		 *
		 * The next two are just to see if we have sane values.
		 * They're only really relevant if we're in Virtual Wire
		 * compatibility mode, but most boxes are anymore.
		 */


		reg = apic_read(APIC_LVT0);
		dprintk("Getting LVT0: %x\n", reg);

		reg = apic_read(APIC_LVT1);
		dprintk("Getting LVT1: %x\n", reg);
	}
#endif

	setup_local_APIC();

	if (GET_APIC_ID(apic_read(APIC_ID)) != boot_cpu_id)
		BUG();

	/*
	 * Now scan the CPU present map and fire up the other CPUs.
	 */

	/*
	 * Add all detected CPUs. (later on we can down individual
	 * CPUs which will change cpu_online_map but not necessarily
	 * cpu_present_map. We are pretty much ready for hot-swap CPUs.)
	 */
	cpu_online_map = cpu_present_map;
	mb();

	dprintk("CPU map: %lx\n", cpu_present_map);

	for (i = 0; i < NR_CPUS; i++) {
		/*
		 * Don't even attempt to start the boot CPU!
		 */
		if (i == boot_cpu_id)
			continue;

		if ((cpu_online_map & (1 << i))
		    && (max_cpus < 0 || max_cpus > cpucount+1)) {
			do_boot_cpu(i);
		}

		/*
		 * Make sure we unmap all failed CPUs
		 */
		if (cpu_number_map[i] == -1 && (cpu_online_map & (1 << i))) {
			printk("CPU #%d not responding - cannot use it.\n",i);
			cpu_online_map &= ~(1 << i);
		}
	}

	/*
	 * Cleanup possible dangling ends...
	 */

#ifndef CONFIG_VISWS
	{
		/*
		 * Install writable page 0 entry to set BIOS data area.
		 */
		local_flush_tlb();

		/*
		 * Paranoid:  Set warm reset code and vector here back
		 * to default values.
		 */
		CMOS_WRITE(0, 0xf);

		*((volatile long *) phys_to_virt(0x467)) = 0;
	}
#endif

	/*
	 * Allow the user to impress friends.
	 */

	dprintk("Before bogomips.\n");
	if (!cpucount) {
		printk(KERN_ERR "Error: only one processor found.\n");
		cpu_online_map = (1<<hard_smp_processor_id());
	} else {
		unsigned long bogosum = 0;
		for(i = 0; i < 32; i++)
			if (cpu_online_map&(1<<i))
				bogosum+=cpu_data[i].loops_per_sec;
		printk(KERN_INFO "Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
			cpucount+1,
			(bogosum+2500)/500000,
			((bogosum+2500)/5000)%100);
		dprintk("Before bogocount - setting activated=1.\n");
	}
	smp_num_cpus = cpucount + 1;

	if (smp_b_stepping)
		printk(KERN_WARNING "WARNING: SMP operation may be unreliable with B stepping processors.\n");
	dprintk("Boot done.\n");

	cache_APIC_registers();
#ifndef CONFIG_VISWS
	/*
	 * Here we can be sure that there is an IO-APIC in the system. Let's
	 * go and set it up:
	 */
	if (!skip_ioapic_setup)
		setup_IO_APIC();
#endif

smp_done:
	/*
	 * now we know the other CPUs have fired off and we know our
	 * APIC ID, so we can go init the TSS and stuff:
	 */
	cpu_init();

	/*
	 * Set up all local APIC timers in the system:
	 */
	setup_APIC_clocks();

	/*
	 * Synchronize the TSC with the AP
	 */
	if (cpu_has_tsc && cpucount)
		synchronize_tsc_bp();

	zap_low_mappings();
}

