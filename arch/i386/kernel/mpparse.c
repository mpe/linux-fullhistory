/*
 *	Intel Multiprocessor Specificiation 1.1 and 1.4
 *	compliant MP-table parsing routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998, 1999, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *	Fixes
 *		Erich Boleyn	:	MP v1.4 and additional changes.
 *		Alan Cox	:	Added EBDA scanning
 *		Ingo Molnar	:	various cleanups and rewrites
 *	Maciej W. Rozycki	:	Bits for genuine 82489DX timers
 */

#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/bootmem.h>
#include <linux/smp_lock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>

#include <asm/smp.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/pgalloc.h>

/* Have we found an MP table */
int smp_found_config = 0;

/*
 * Various Linux-internal data structures created from the
 * MP-table.
 */
int apic_version [NR_CPUS];
int mp_bus_id_to_type [MAX_MP_BUSSES] = { -1, };
int mp_bus_id_to_pci_bus [MAX_MP_BUSSES] = { -1, };
int mp_current_pci_id = 0;
int pic_mode;
unsigned long mp_lapic_addr = 0;

/* Processor that is doing the boot up */
unsigned int boot_cpu_id = 0;
/* Internal processor count */
static unsigned int num_processors = 1;

/* Bitmask of physically existing CPUs */
unsigned long phys_cpu_present_map = 0;

/*
 * IA s/w dev Vol 3, Section 7.4
 */
#define APIC_DEFAULT_PHYS_BASE 0xfee00000

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

	if (m->mpc_featureflag&(1<<0))
		Dprintk("    Floating point unit present.\n");
	if (m->mpc_featureflag&(1<<7))
		Dprintk("    Machine Exception supported.\n");
	if (m->mpc_featureflag&(1<<8))
		Dprintk("    64 bit compare & exchange supported.\n");
	if (m->mpc_featureflag&(1<<9))
		Dprintk("    Internal APIC present.\n");

	if (m->mpc_cpuflag & CPU_BOOTPROCESSOR) {
		Dprintk("    Bootup CPU\n");
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

	phys_cpu_present_map |= 1 << m->mpc_apicid;
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
	Dprintk("Bus #%d is %s\n", m->mpc_busid, str);

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
		phys_cpu_present_map = 3;
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

	Dprintk("Scan SMP from %p for %ld bytes.\n", bp,length);
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

	phys_cpu_present_map |= 2; /* or in id 1 */
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
#ifdef CONFIG_X86_IO_APIC
	init_intel_smp();
#endif
#ifdef CONFIG_VISWS
	init_visws_smp();
#endif
}

