/*
 *	Intel MP v1.1/v1.4 specification support routines for multi-pentium
 *	hosts.
 *
 *	(c) 1995 Alan Cox, CymruNET Ltd  <alan@cymru.net>
 *	(c) 1998 Ingo Molnar
 *
 *	Supported by Caldera http://www.caldera.com.
 *	Much of the core SMP work is based on previous work by Thomas Radke, to
 *	whom a great many thanks are extended.
 *
 *	Thanks to Intel for making available several different Pentium,
 *	Pentium Pro and Pentium-II/Xeon MP machines.
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
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <asm/mtrr.h>

#include "irq.h"

extern unsigned long start_kernel, _etext;
extern void update_one_process( struct task_struct *p,
				unsigned long ticks, unsigned long user,
				unsigned long system, int cpu);
/*
 *	Some notes on processor bugs:
 *
 *	Pentium and Pentium Pro (and all CPUs) have bugs. The Linux issues
 *	for SMP are handled as follows.
 *
 *	Pentium Pro
 *		Occasional delivery of 'spurious interrupt' as trap #16. This
 *	is very rare. The kernel logs the event and recovers
 *
 *	Pentium
 *		There is a marginal case where REP MOVS on 100MHz SMP
 *	machines with B stepping processors can fail. XXX should provide
 *	an L1cache=Writethrough or L1cache=off option.
 *
 *		B stepping CPUs may hang. There are hardware work arounds
 *	for this. We warn about it in case your board doesnt have the work
 *	arounds. Basically thats so I can tell anyone with a B stepping
 *	CPU and SMP problems "tough".
 *
 *	Specific items [From Pentium Processor Specification Update]
 *
 *	1AP.	Linux doesn't use remote read
 *	2AP.	Linux doesn't trust APIC errors
 *	3AP.	We work around this
 *	4AP.	Linux never generated 3 interrupts of the same priority
 *		to cause a lost local interrupt.
 *	5AP.	Remote read is never used
 *	9AP.	XXX NEED TO CHECK WE HANDLE THIS XXX
 *	10AP.	XXX NEED TO CHECK WE HANDLE THIS XXX
 *	11AP.	Linux reads the APIC between writes to avoid this, as per
 *		the documentation. Make sure you preserve this as it affects
 *		the C stepping chips too.
 *
 *	If this sounds worrying believe me these bugs are ___RARE___ and
 *	there's about nothing of note with C stepping upwards.
 */


/* Kernel spinlock */
spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;

/*
 * function prototypes:
 */
static void cache_APIC_registers (void);
static void stop_this_cpu (void);

static int smp_b_stepping = 0;				/* Set if we find a B stepping CPU			*/

static int max_cpus = -1;				/* Setup configured maximum number of CPUs to activate	*/
int smp_found_config=0;					/* Have we found an SMP box 				*/

unsigned long cpu_present_map = 0;			/* Bitmask of physically existing CPUs 				*/
unsigned long cpu_online_map = 0;			/* Bitmask of currently online CPUs 				*/
int smp_num_cpus = 1;					/* Total count of live CPUs 				*/
int smp_threads_ready=0;				/* Set when the idlers are all forked 			*/
volatile int cpu_number_map[NR_CPUS];			/* which CPU maps to which logical number		*/
volatile int __cpu_logical_map[NR_CPUS];			/* which logical number maps to which CPU		*/
static volatile unsigned long cpu_callin_map[NR_CPUS] = {0,};	/* We always use 0 the rest is ready for parallel delivery */
static volatile unsigned long cpu_callout_map[NR_CPUS] = {0,};	/* We always use 0 the rest is ready for parallel delivery */
volatile unsigned long smp_invalidate_needed;		/* Used for the invalidate map that's also checked in the spinlock */
volatile unsigned long kstack_ptr;			/* Stack vector for booting CPUs			*/
struct cpuinfo_x86 cpu_data[NR_CPUS];			/* Per CPU bogomips and other parameters 		*/
static unsigned int num_processors = 1;			/* Internal processor count				*/
unsigned long mp_ioapic_addr = 0xFEC00000;		/* Address of the I/O apic (not yet used) 		*/
unsigned char boot_cpu_id = 0;				/* Processor that is doing the boot up 			*/
static int smp_activated = 0;				/* Tripped once we need to start cross invalidating 	*/
int apic_version[NR_CPUS];				/* APIC version number					*/
unsigned long apic_retval;				/* Just debugging the assembler.. 			*/

volatile unsigned long kernel_counter=0;		/* Number of times the processor holds the lock		*/
volatile unsigned long syscall_count=0;			/* Number of times the processor holds the syscall lock	*/

volatile unsigned long ipi_count;			/* Number of IPIs delivered				*/

const char lk_lockmsg[] = "lock from interrupt context at %p\n"; 

int mp_bus_id_to_type [MAX_MP_BUSSES] = { -1, };
extern int mp_irq_entries;
extern struct mpc_config_intsrc mp_irqs [MAX_IRQ_SOURCES];
extern int mpc_default_type;
int mp_bus_id_to_pci_bus [MAX_MP_BUSSES] = { -1, };
int mp_current_pci_id = 0;
unsigned long mp_lapic_addr = 0;
int skip_ioapic_setup = 0;				/* 1 if "noapic" boot option passed */

/* #define SMP_DEBUG */

#ifdef SMP_DEBUG
#define SMP_PRINTK(x)	printk x
#else
#define SMP_PRINTK(x)
#endif

/*
 * IA s/w dev Vol 3, Section 7.4
 */
#define APIC_DEFAULT_PHYS_BASE 0xfee00000

/*
 *	Setup routine for controlling SMP activation
 *
 *	Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 *      activation entirely (the MPS table probe still happens, though).
 *
 *	Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 *	greater than 0, limits the maximum number of CPUs activated in
 *	SMP mode to <NUM>.
 */

void __init smp_setup(char *str, int *ints)
{
	if (ints && ints[0] > 0)
		max_cpus = ints[1];
	else
		max_cpus = 0;
}

void ack_APIC_irq(void)
{
	/* Clear the IPI */

	/* Dummy read */
	apic_read(APIC_SPIV);

	/* Docs say use 0 for future compatibility */
	apic_write(APIC_EOI, 0);
}

/*
 * Intel MP BIOS table parsing routines:
 */

#ifndef CONFIG_X86_VISWS_APIC
/*
 *	Checksum an MP configuration block.
 */

static int mpf_checksum(unsigned char *mp, int len)
{
	int sum=0;
	while(len--)
		sum+=*mp++;
	return sum&0xFF;
}

/*
 *	Processor encoding in an MP configuration block
 */

static char *mpc_family(int family,int model)
{
	static char n[32];
	static char *model_defs[]=
	{
		"80486DX","80486DX",
		"80486SX","80486DX/2 or 80487",
		"80486SL","Intel5X2(tm)",
		"Unknown","Unknown",
		"80486DX/4"
	};
	if (family==0x6)
		return("Pentium(tm) Pro");
	if (family==0x5)
		return("Pentium(tm)");
	if (family==0x0F && model==0x0F)
		return("Special controller");
	if (family==0x04 && model<9)
		return model_defs[model];
	sprintf(n,"Unknown CPU [%d:%d]",family, model);
	return n;
}

/*
 *	Read the MPC
 */

static int __init smp_read_mpc(struct mp_config_table *mpc)
{
	char str[16];
	int count=sizeof(*mpc);
	int ioapics = 0;
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
	memcpy(ioapic_OEM_ID,str,9);
	printk("OEM ID: %s ",str);
	
	memcpy(str,mpc->mpc_productid,12);
	str[12]=0;
	memcpy(ioapic_Product_ID,str,13);
	printk("Product ID: %s ",str);

	printk("APIC at: 0x%lX\n",mpc->mpc_lapic);

	/* save the local APIC address, it might be non-default */
	mp_lapic_addr = mpc->mpc_lapic;

	/*
	 *	Now process the configuration blocks.
	 */
	
	while(count<mpc->mpc_length)
	{
		switch(*mpt)
		{
			case MP_PROCESSOR:
			{
				struct mpc_config_processor *m=
					(struct mpc_config_processor *)mpt;
				if (m->mpc_cpuflag&CPU_ENABLED)
				{
					printk("Processor #%d %s APIC version %d\n",
						m->mpc_apicid,
						mpc_family((m->mpc_cpufeature&
							CPU_FAMILY_MASK)>>8,
							(m->mpc_cpufeature&
								CPU_MODEL_MASK)>>4),
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
					if (m->mpc_cpuflag&CPU_BOOTPROCESSOR)
					{
						SMP_PRINTK(("    Bootup CPU\n"));
						boot_cpu_id=m->mpc_apicid;
					}
					else	/* Boot CPU already counted */
						num_processors++;

					if (m->mpc_apicid>NR_CPUS)
						printk("Processor #%d unused. (Max %d processors).\n",m->mpc_apicid, NR_CPUS);
					else
					{
						cpu_present_map|=(1<<m->mpc_apicid);
						apic_version[m->mpc_apicid]=m->mpc_apicver;
					}
				}
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			case MP_BUS:
			{
				struct mpc_config_bus *m=
					(struct mpc_config_bus *)mpt;
				memcpy(str,m->mpc_bustype,6);
				str[6]=0;
				SMP_PRINTK(("Bus #%d is %s\n",
					m->mpc_busid,
					str));
				if ((strncmp(m->mpc_bustype,"ISA",3) == 0) ||
					(strncmp(m->mpc_bustype,"EISA",4) == 0))
					mp_bus_id_to_type[m->mpc_busid] =
						MP_BUS_ISA;
				else
				if (strncmp(m->mpc_bustype,"PCI",3) == 0) {
					mp_bus_id_to_type[m->mpc_busid] =
						MP_BUS_PCI;
					mp_bus_id_to_pci_bus[m->mpc_busid] =
						mp_current_pci_id;
					mp_current_pci_id++;
				}
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			case MP_IOAPIC:
			{
				struct mpc_config_ioapic *m=
					(struct mpc_config_ioapic *)mpt;
				if (m->mpc_flags&MPC_APIC_USABLE)
				{
					ioapics++;
					printk("I/O APIC #%d Version %d at 0x%lX.\n",
						m->mpc_apicid,m->mpc_apicver,
						m->mpc_apicaddr);
					/*
					 * we use the first one only currently
					 */
					if (ioapics == 1)
						mp_ioapic_addr = m->mpc_apicaddr;
				}
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			case MP_INTSRC:
			{
				struct mpc_config_intsrc *m=
					(struct mpc_config_intsrc *)mpt;

				mp_irqs [mp_irq_entries] = *m;
				if (++mp_irq_entries == MAX_IRQ_SOURCES) {
					printk("Max irq sources exceeded!!\n");
					printk("Skipping remaining sources.\n");
					--mp_irq_entries;
				}

				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			case MP_LINTSRC:
			{
				struct mpc_config_intlocal *m=
					(struct mpc_config_intlocal *)mpt;
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
		}
	}
	if (ioapics > 1)
	{
		printk("Warning: Multiple IO-APICs not yet supported.\n");
		printk("Warning: switching to non APIC mode.\n");
		skip_ioapic_setup=1;
	}
	return num_processors;
}

/*
 *	Scan the memory blocks for an SMP configuration block.
 */

static int __init smp_scan_config(unsigned long base, unsigned long length)
{
	unsigned long *bp=phys_to_virt(base);
	struct intel_mp_floating *mpf;

	SMP_PRINTK(("Scan SMP from %p for %ld bytes.\n",
		bp,length));
	if (sizeof(*mpf)!=16)
		printk("Error: MPF size\n");

	while (length>0)
	{
		if (*bp==SMP_MAGIC_IDENT)
		{
			mpf=(struct intel_mp_floating *)bp;
			if (mpf->mpf_length==1 &&
				!mpf_checksum((unsigned char *)bp,16) &&
				(mpf->mpf_specification == 1
				 || mpf->mpf_specification == 4) )
			{
				printk("Intel MultiProcessor Specification v1.%d\n", mpf->mpf_specification);
				if (mpf->mpf_feature2&(1<<7))
					printk("    IMCR and PIC compatibility mode.\n");
				else
					printk("    Virtual Wire compatibility mode.\n");
				smp_found_config=1;
				/*
				 *	Now see if we need to read further.
				 */
				if (mpf->mpf_feature1!=0)
				{
					unsigned long cfg;

					/* local APIC has default address */
					mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;
					/*
					 *	We need to know what the local
					 *	APIC id of the boot CPU is!
					 */

/*
 *
 *	HACK HACK HACK HACK HACK HACK HACK HACK HACK HACK HACK HACK HACK
 *
 *	It's not just a crazy hack.  ;-)
 */
					/*
					 *	Standard page mapping
					 *	functions don't work yet.
					 *	We know that page 0 is not
					 *	used.  Steal it for now!
					 */
			
					cfg=pg0[0];
					pg0[0] = (mp_lapic_addr | 7);
					local_flush_tlb();

					boot_cpu_id = GET_APIC_ID(*((volatile unsigned long *) APIC_ID));

					/*
					 *	Give it back
					 */

					pg0[0]= cfg;
					local_flush_tlb();

/*
 *
 *	END OF HACK   END OF HACK   END OF HACK   END OF HACK   END OF HACK
 *
 */
					/*
					 *	2 CPUs, numbered 0 & 1.
					 */
					cpu_present_map=3;
					num_processors=2;
					printk("I/O APIC at 0xFEC00000.\n");

					/*
					 * Save the default type number, we
					 * need it later to set the IO-APIC
					 * up properly:
					 */
					mpc_default_type = mpf->mpf_feature1;

					printk("Bus #0 is ");
				}
				switch(mpf->mpf_feature1)
				{
					case 1:
					case 5:
						printk("ISA\n");
						break;
					case 2:
						printk("EISA with no IRQ8 chaining\n");
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
						break;
					default:
						printk("???\nUnknown standard configuration %d\n",
							mpf->mpf_feature1);
						return 1;
				}
				if (mpf->mpf_feature1>4)
				{
					printk("Bus #1 is PCI\n");

					/*
					 *	Set local APIC version to
					 *	the integrated form.
					 *	It's initialized to zero
					 *	otherwise, representing
					 *	a discrete 82489DX.
					 */
					apic_version[0] = 0x10;
					apic_version[1] = 0x10;
				}
				/*
				 *	Read the physical hardware table.
				 *	Anything here will override the
				 *	defaults.
				 */
				if (mpf->mpf_physptr)
					smp_read_mpc((void *)mpf->mpf_physptr);

				__cpu_logical_map[0] = boot_cpu_id;
				global_irq_holder = boot_cpu_id;
				current->processor = boot_cpu_id;

				printk("Processors: %d\n", num_processors);
				/*
				 *	Only use the first configuration found.
				 */
				return 1;
			}
		}
		bp+=4;
		length-=16;
	}

	return 0;
}

void __init init_intel_smp (void)
{
	/*
	 * FIXME: Linux assumes you have 640K of base ram..
	 * this continues the error...
	 *
	 * 1) Scan the bottom 1K for a signature
	 * 2) Scan the top 1K of base RAM
	 * 3) Scan the 64K of bios
	 */
	if (!smp_scan_config(0x0,0x400) &&
	    !smp_scan_config(639*0x400,0x400) &&
	    !smp_scan_config(0xF0000,0x10000)) {
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
		unsigned int address;

		address = *(unsigned short *)phys_to_virt(0x40E);
		address<<=4;
		smp_scan_config(address, 0x1000);
		if (smp_found_config)
			printk(KERN_WARNING "WARNING: MP table in the EBDA can be UNSAFE, contact linux-smp@vger.rutgers.edu if you experience SMP problems!\n");
	}
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
 *	Trampoline 80x86 program as an array.
 */

extern unsigned char trampoline_data [];
extern unsigned char trampoline_end  [];
static unsigned char *trampoline_base;

/*
 *	Currently trivial. Write the real->protected mode
 *	bootstrap into the page concerned. The caller
 *	has made sure it's suitably aligned.
 */

static unsigned long __init setup_trampoline(void)
{
	memcpy(trampoline_base, trampoline_data, trampoline_end - trampoline_data);
	return virt_to_phys(trampoline_base);
}

/*
 *	We are called very early to get the low memory for the
 *	SMP bootup trampoline page.
 */
unsigned long __init smp_alloc_memory(unsigned long mem_base)
{
	if (virt_to_phys((void *)mem_base) >= 0x9F000)
		panic("smp_alloc_memory: Insufficient low memory for kernel trampoline 0x%lx.", mem_base);
	trampoline_base = (void *)mem_base;
	return mem_base + PAGE_SIZE;
}

/*
 *	The bootstrap kernel entry code has set these up. Save them for
 *	a given CPU
 */

void __init smp_store_cpu_info(int id)
{
	struct cpuinfo_x86 *c=&cpu_data[id];

	*c = boot_cpu_data;
	c->pte_quick = 0;
	c->pgd_quick = 0;
	c->pgtable_cache_sz = 0;
	identify_cpu(c);
	/*
	 *	Mask B, Pentium, but not Pentium MMX
	 */
	if (c->x86_vendor == X86_VENDOR_INTEL &&
	    c->x86 == 5 &&
	    c->x86_mask >= 1 && c->x86_mask <= 4 &&
	    c->x86_model <= 3)
		smp_b_stepping=1;		/* Remember we have B step Pentia with bugs */
}

/*
 *	Architecture specific routine called by the kernel just before init is
 *	fired off. This allows the BP to have everything in order [we hope].
 *	At the end of this all the APs will hit the system scheduling and off
 *	we go. Each AP will load the system gdt's and jump through the kernel
 *	init into idle(). At this point the scheduler will one day take over
 * 	and give them jobs to do. smp_callin is a standard routine
 *	we use to track CPUs as they power up.
 */

static atomic_t smp_commenced = ATOMIC_INIT(0);

void __init smp_commence(void)
{
	/*
	 *	Lets the callins below out of their loop.
	 */
	SMP_PRINTK(("Setting commenced=1, go go go\n"));

	wmb();
	atomic_set(&smp_commenced,1);
}

void __init enable_local_APIC(void)
{
	unsigned long value;

 	value = apic_read(APIC_SPIV);
 	value |= (1<<8);		/* Enable APIC (bit==1) */
 	value &= ~(1<<9);		/* Enable focus processor (bit==0) */
	value |= 0xff;			/* Set spurious IRQ vector to 0xff */
 	apic_write(APIC_SPIV,value);

 	value = apic_read(APIC_TASKPRI);
 	value &= ~APIC_TPRI_MASK;	/* Set Task Priority to 'accept all' */
 	apic_write(APIC_TASKPRI,value);

	/*
	 * Set arbitrarion priority to 0
	 */
 	value = apic_read(APIC_ARBPRI);
 	value &= ~APIC_ARBPRI_MASK;
 	apic_write(APIC_ARBPRI, value);

	/*
	 * Set the logical destination ID to 'all', just to be safe.
	 * also, put the APIC into flat delivery mode.
	 */
 	value = apic_read(APIC_LDR);
	value &= ~APIC_LDR_MASK;
	value |= SET_APIC_LOGICAL_ID(0xff);
 	apic_write(APIC_LDR,value);

 	value = apic_read(APIC_DFR);
	value |= SET_APIC_DFR(0xf);
 	apic_write(APIC_DFR, value);

	udelay(100);			/* B safe */
	ack_APIC_irq();
	udelay(100);
}

unsigned long __init init_smp_mappings(unsigned long memory_start)
{
	unsigned long apic_phys;

	memory_start = PAGE_ALIGN(memory_start);
	if (smp_found_config) {
		apic_phys = mp_lapic_addr;
	} else {
		/*
		 * set up a fake all zeroes page to simulate the
		 * local APIC and another one for the IO-APIC. We
		 * could use the real zero-page, but it's safer
		 * this way if some buggy code writes to this page ...
		 */
		apic_phys = __pa(memory_start);
		memset((void *)memory_start, 0, PAGE_SIZE);
		memory_start += PAGE_SIZE;
	}
	set_fixmap(FIX_APIC_BASE,apic_phys);
	printk("mapped APIC to %08lx (%08lx)\n", APIC_BASE, apic_phys);

#ifdef CONFIG_X86_IO_APIC
	{
		unsigned long ioapic_phys;

		if (smp_found_config) {
			ioapic_phys = mp_ioapic_addr;
		} else {
			ioapic_phys = __pa(memory_start);
			memset((void *)memory_start, 0, PAGE_SIZE);
			memory_start += PAGE_SIZE;
		}
		set_fixmap(FIX_IO_APIC_BASE,ioapic_phys);
		printk("mapped IOAPIC to %08lx (%08lx)\n",
				fix_to_virt(FIX_IO_APIC_BASE), ioapic_phys);
	}
#endif

	return memory_start;
}

extern void calibrate_delay(void);

void __init smp_callin(void)
{
	int cpuid;
	unsigned long timeout;

	/*
	 * (This works even if the APIC is not enabled.)
	 */
	cpuid = GET_APIC_ID(apic_read(APIC_ID));

	SMP_PRINTK(("CPU#%d waiting for CALLOUT\n", cpuid));

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
	while (time_before(jiffies,timeout))
	{
		/*
		 * Has the boot CPU finished it's STARTUP sequence?
		 */
		if (test_bit(cpuid, (unsigned long *)&cpu_callout_map[0]))
			break;
	}

	while (!time_before(jiffies,timeout)) {
		printk("BUG: CPU%d started up but did not get a callout!\n",
			cpuid);
		stop_this_cpu();
	}

	/*
	 * the boot CPU has finished the init stage and is spinning
	 * on callin_map until we finish. We are free to set up this
	 * CPU, first the APIC. (this is probably redundant on most
	 * boards)
	 */
	
	SMP_PRINTK(("CALLIN, before enable_local_APIC().\n"));
	enable_local_APIC();

	/*
	 * Set up our APIC timer.
	 */
	setup_APIC_clock();

 	__sti();

#ifdef CONFIG_MTRR
	/*  Must be done before calibration delay is computed  */
	mtrr_init_secondary_cpu ();
#endif
	/*
	 *	Get our bogomips.
	 */
	calibrate_delay();
	SMP_PRINTK(("Stack at about %p\n",&cpuid));

	/*
	 *	Save our processor parameters
	 */
 	smp_store_cpu_info(cpuid);

	/*
	 *	Allow the master to continue.
	 */
	set_bit(cpuid, (unsigned long *)&cpu_callin_map[0]);
}

int cpucount = 0;

extern int cpu_idle(void * unused);

/*
 *	Activate a secondary processor.
 */
int __init start_secondary(void *unused)
{
	/*
	 * Dont put anything before smp_callin(), SMP
	 * booting is too fragile that we want to limit the
	 * things done here to the most necessary things.
	 */
	smp_callin();
	while (!atomic_read(&smp_commenced))
		/* nothing */ ;
	return cpu_idle(NULL);
}

/*
 * Everything has been set up for the secondary
 * CPUs - they just need to reload everything
 * from the task structure
 */
void __init initialize_secondary(void)
{
	struct thread_struct * p = &current->tss;

	/*
	 * Load up the LDT and the task register.
	 */
	asm volatile("lldt %%ax": :"a" (p->ldt));
	asm volatile("ltr %%ax": :"a" (p->tr));
	stts();

	/*
	 * We don't actually need to load the full TSS,
	 * basically just the stack pointer and the eip.
	 */

	asm volatile(
		"movl %0,%%esp\n\t"
		"jmp *%1"
		:
		:"r" (p->esp),"r" (p->eip));
}

extern struct {
	void * esp;
	unsigned short ss;
} stack_start;

static void __init do_boot_cpu(int i)
{
	unsigned long cfg;
	pgd_t maincfg;
	struct task_struct *idle;
	unsigned long send_status, accept_status;
	int timeout, num_starts, j;
	unsigned long start_eip;

	/*
	 *	We need an idle process for each processor.
	 */

	kernel_thread(start_secondary, NULL, CLONE_PID);
	cpucount++;

	idle = task[cpucount];
	if (!idle)
		panic("No idle process for CPU %d", i);

	idle->processor = i;
	__cpu_logical_map[cpucount] = i;
	cpu_number_map[i] = cpucount;

	/* start_eip had better be page-aligned! */
	start_eip = setup_trampoline();

	printk("Booting processor %d eip %lx\n", i, start_eip);	/* So we see what's up   */
	stack_start.esp = (void *) (1024 + PAGE_SIZE + (char *)idle);

	/*
	 *	This grunge runs the startup process for
	 *	the targeted processor.
	 */

	SMP_PRINTK(("Setting warm reset code and vector.\n"));

	CMOS_WRITE(0xa, 0xf);
	local_flush_tlb();
	SMP_PRINTK(("1.\n"));
	*((volatile unsigned short *) phys_to_virt(0x469)) = start_eip >> 4;
	SMP_PRINTK(("2.\n"));
	*((volatile unsigned short *) phys_to_virt(0x467)) = start_eip & 0xf;
	SMP_PRINTK(("3.\n"));

	maincfg=swapper_pg_dir[0];
	((unsigned long *)swapper_pg_dir)[0]=0x102007;

	/*
	 *	Be paranoid about clearing APIC errors.
	 */

	if ( apic_version[i] & 0xF0 )
	{
		apic_write(APIC_ESR, 0);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
	}

	/*
	 *	Status is now clean
	 */
	
	send_status = 	0;
	accept_status = 0;

	/*
	 *	Starting actual IPI sequence...
	 */

	SMP_PRINTK(("Asserting INIT.\n"));

	/*
	 *	Turn INIT on
	 */
			
	cfg=apic_read(APIC_ICR2);
	cfg&=0x00FFFFFF;
	apic_write(APIC_ICR2, cfg|SET_APIC_DEST_FIELD(i)); 			/* Target chip     	*/
	cfg=apic_read(APIC_ICR);
	cfg&=~0xCDFFF;								/* Clear bits 		*/
	cfg |= (APIC_DEST_LEVELTRIG | APIC_DEST_ASSERT | APIC_DEST_DM_INIT);
	apic_write(APIC_ICR, cfg);						/* Send IPI */

	udelay(200);
	SMP_PRINTK(("Deasserting INIT.\n"));

	cfg=apic_read(APIC_ICR2);
	cfg&=0x00FFFFFF;
	apic_write(APIC_ICR2, cfg|SET_APIC_DEST_FIELD(i));			/* Target chip     	*/
	cfg=apic_read(APIC_ICR);
	cfg&=~0xCDFFF;								/* Clear bits 		*/
	cfg |= (APIC_DEST_LEVELTRIG | APIC_DEST_DM_INIT);
	apic_write(APIC_ICR, cfg);						/* Send IPI */

	/*
	 *	Should we send STARTUP IPIs ?
	 *
	 *	Determine this based on the APIC version.
	 *	If we don't have an integrated APIC, don't
	 *	send the STARTUP IPIs.
	 */

	if ( apic_version[i] & 0xF0 )
		num_starts = 2;
	else
		num_starts = 0;

	/*
	 *	Run STARTUP IPI loop.
	 */

	for (j = 1; !(send_status || accept_status)
		    && (j <= num_starts) ; j++)
	{
		SMP_PRINTK(("Sending STARTUP #%d.\n",j));
		apic_write(APIC_ESR, 0);
		SMP_PRINTK(("After apic_write.\n"));

		/*
		 *	STARTUP IPI
		 */

		cfg=apic_read(APIC_ICR2);
		cfg&=0x00FFFFFF;
		apic_write(APIC_ICR2, cfg|SET_APIC_DEST_FIELD(i));			/* Target chip     	*/
		cfg=apic_read(APIC_ICR);
		cfg&=~0xCDFFF;								/* Clear bits 		*/
		cfg |= (APIC_DEST_DM_STARTUP | (start_eip >> 12));						/* Boot on the stack 	*/
		SMP_PRINTK(("Before start apic_write.\n"));
		apic_write(APIC_ICR, cfg);						/* Kick the second 	*/

		SMP_PRINTK(("Startup point 1.\n"));

		timeout = 0;
		SMP_PRINTK(("Waiting for send to finish...\n"));
		do {
			SMP_PRINTK(("+"));
			udelay(100);
			send_status = apic_read(APIC_ICR) & 0x1000;
		} while (send_status && (timeout++ < 1000));

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(200);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
	}
	SMP_PRINTK(("After Startup.\n"));

	if (send_status)		/* APIC never delivered?? */
		printk("APIC never delivered???\n");
	if (accept_status)		/* Send accept error */
		printk("APIC delivery error (%lx).\n", accept_status);

	if ( !(send_status || accept_status) )
	{
		/*
		 * allow APs to start initializing.
		 */
		SMP_PRINTK(("Before Callout %d.\n", i));
		set_bit(i, (unsigned long *)&cpu_callout_map[0]);
		SMP_PRINTK(("After Callout %d.\n", i));

		for(timeout=0;timeout<50000;timeout++)
		{
			if (cpu_callin_map[0]&(1<<i))
				break;				/* It has booted */
			udelay(100);				/* Wait 5s total for a response */
		}
		if (cpu_callin_map[0]&(1<<i))
		{
			/* number CPUs logically, starting from 1 (BSP is 0) */
#if 0
			cpu_number_map[i] = cpucount;
			__cpu_logical_map[cpucount] = i;
#endif
			printk("OK.\n");
			printk("CPU%d: ", i);
			print_cpu_info(&cpu_data[i]);
		}
		else
		{
			if (*((volatile unsigned char *)phys_to_virt(8192))==0xA5)
				printk("Stuck ??\n");
			else
				printk("Not responding.\n");
		}
	SMP_PRINTK(("CPU has booted.\n"));
	}
	else
	{
		__cpu_logical_map[cpucount] = -1;
		cpu_number_map[i] = -1;
		cpucount--;
	}

	swapper_pg_dir[0]=maincfg;
	local_flush_tlb();

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

unsigned int prof_multiplier[NR_CPUS];
unsigned int prof_counter[NR_CPUS];

/*
 *	Cycle through the processors sending APIC IPIs to boot each.
 */

void __init smp_boot_cpus(void)
{
	int i;

#ifdef CONFIG_MTRR
	/*  Must be done before other processors booted  */
	mtrr_init_boot_cpu ();
#endif
	/*
	 *	Initialize the logical to physical CPU number mapping
	 *	and the per-CPU profiling counter/multiplier
	 */

	for (i = 0; i < NR_CPUS; i++) {
		cpu_number_map[i] = -1;
		prof_counter[i] = 1;
		prof_multiplier[i] = 1;
	}

	/*
	 *	Setup boot CPU information
	 */

	smp_store_cpu_info(boot_cpu_id);			/* Final full version of the data */
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

	/*
	 * If we couldnt find an SMP configuration at boot time,
	 * get out of here now!
	 */

	if (!smp_found_config)
	{
		printk(KERN_NOTICE "SMP motherboard not detected. Using dummy APIC emulation.\n");
#ifndef CONFIG_VISWS
		io_apic_irqs = 0;
#endif
		cpu_online_map = cpu_present_map;
		goto smp_done;
	}

	/*
	 *	If SMP should be disabled, then really disable it!
	 */

	if (!max_cpus)
	{
		smp_found_config = 0;
		printk(KERN_INFO "SMP mode deactivated, forcing use of dummy APIC emulation.\n");
	}

#ifdef SMP_DEBUG
	{
		int reg;

		/*
		 *	This is to verify that we're looking at
		 *	a real local APIC.  Check these against
		 *	your board if the CPUs aren't getting
		 *	started for no apparent reason.
		 */

		reg = apic_read(APIC_VERSION);
		SMP_PRINTK(("Getting VERSION: %x\n", reg));

		apic_write(APIC_VERSION, 0);
		reg = apic_read(APIC_VERSION);
		SMP_PRINTK(("Getting VERSION: %x\n", reg));

		/*
		 *	The two version reads above should print the same
		 *	NON-ZERO!!! numbers.  If the second one is zero,
		 *	there is a problem with the APIC write/read
		 *	definitions.
		 *
		 *	The next two are just to see if we have sane values.
		 *	They're only really relevant if we're in Virtual Wire
		 *	compatibility mode, but most boxes are anymore.
		 */


		reg = apic_read(APIC_LVT0);
		SMP_PRINTK(("Getting LVT0: %x\n", reg));

		reg = apic_read(APIC_LVT1);
		SMP_PRINTK(("Getting LVT1: %x\n", reg));
	}
#endif

	enable_local_APIC();

	/*
	 * Set up our local APIC timer:
	 */
	setup_APIC_clock ();

	/*
	 *	Now scan the CPU present map and fire up the other CPUs.
	 */

	/*
	 * Add all detected CPUs. (later on we can down individual
	 * CPUs which will change cpu_online_map but not necessarily
	 * cpu_present_map. We are pretty much ready for hot-swap CPUs.)
	 */
	cpu_online_map = cpu_present_map;
	mb();

	SMP_PRINTK(("CPU map: %lx\n", cpu_present_map));

	for(i=0;i<NR_CPUS;i++)
	{
		/*
		 *	Don't even attempt to start the boot CPU!
		 */
		if (i == boot_cpu_id)
			continue;

		if ((cpu_online_map & (1 << i))
		    && (max_cpus < 0 || max_cpus > cpucount+1))
		{
			do_boot_cpu(i);
		}

		/*
		 *	Make sure we unmap all failed CPUs
		 */
		
		if (cpu_number_map[i] == -1 && (cpu_online_map & (1 << i))) {
			printk("CPU #%d not responding. Removing from cpu_online_map.\n",i);
			cpu_online_map &= ~(1 << i);
                }
        }

	/*
	 *	Cleanup possible dangling ends...
	 */

#ifndef CONFIG_VISWS
	{
		unsigned long cfg;

		/*
		 *	Install writable page 0 entry.
		 */
		cfg = pg0[0];
		pg0[0] = 3;	/* writeable, present, addr 0 */
		local_flush_tlb();
	
		/*
		 *	Paranoid:  Set warm reset code and vector here back
		 *	to default values.
		 */

		CMOS_WRITE(0, 0xf);

		*((volatile long *) phys_to_virt(0x467)) = 0;

		/*
		 *	Restore old page 0 entry.
		 */

		pg0[0] = cfg;
		local_flush_tlb();
	}
#endif

	/*
	 *	Allow the user to impress friends.
	 */

	SMP_PRINTK(("Before bogomips.\n"));
	if (cpucount==0)
	{
		printk(KERN_ERR "Error: only one processor found.\n");
		cpu_online_map = (1<<hard_smp_processor_id());
	}
	else
	{
		unsigned long bogosum=0;
		for(i=0;i<32;i++)
		{
			if (cpu_online_map&(1<<i))
				bogosum+=cpu_data[i].loops_per_sec;
		}
		printk(KERN_INFO "Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
			cpucount+1,
			(bogosum+2500)/500000,
			((bogosum+2500)/5000)%100);
		SMP_PRINTK(("Before bogocount - setting activated=1.\n"));
		smp_activated=1;
		smp_num_cpus=cpucount+1;
	}
	if (smp_b_stepping)
		printk(KERN_WARNING "WARNING: SMP operation may be unreliable with B stepping processors.\n");
	SMP_PRINTK(("Boot done.\n"));

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
}


/*
 * the following functions deal with sending IPIs between CPUs.
 *
 * We use 'broadcast', CPU->CPU IPIs and self-IPIs too.
 */


/*
 * Silly serialization to work around CPU bug in P5s.
 * We can safely turn it off on a 686.
 */
#ifdef CONFIG_X86_GOOD_APIC
# define FORCE_APIC_SERIALIZATION 0
#else
# define FORCE_APIC_SERIALIZATION 1
#endif

static unsigned int cached_APIC_ICR;
static unsigned int cached_APIC_ICR2;

/*
 * Caches reserved bits, APIC reads are (mildly) expensive
 * and force otherwise unnecessary CPU synchronization.
 *
 * (We could cache other APIC registers too, but these are the
 * main ones used in RL.)
 */
#define slow_ICR (apic_read(APIC_ICR) & ~0xFDFFF)
#define slow_ICR2 (apic_read(APIC_ICR2) & 0x00FFFFFF)

void cache_APIC_registers (void)
{
	cached_APIC_ICR = slow_ICR;
	cached_APIC_ICR2 = slow_ICR2;
	mb();
}

static inline unsigned int __get_ICR (void)
{
#if FORCE_APIC_SERIALIZATION
	/*
	 * Wait for the APIC to become ready - this should never occur. It's
	 * a debugging check really.
	 */
	int count = 0;
	unsigned int cfg;

	while (count < 1000)
	{
		cfg = slow_ICR;
		if (!(cfg&(1<<12))) {
			if (count)
				atomic_add(count, (atomic_t*)&ipi_count);
			return cfg;
		}
		count++;
		udelay(10);
	}
	printk("CPU #%d: previous IPI still not cleared after 10mS\n",
			smp_processor_id());
	return cfg;
#else
	return cached_APIC_ICR;
#endif
}

static inline unsigned int __get_ICR2 (void)
{
#if FORCE_APIC_SERIALIZATION
	return slow_ICR2;
#else
	return cached_APIC_ICR2;
#endif
}

static inline int __prepare_ICR (unsigned int shortcut, int vector)
{
	unsigned int cfg;

	cfg = __get_ICR();
	cfg |= APIC_DEST_DM_FIXED|shortcut|vector;

	return cfg;
}

static inline int __prepare_ICR2 (unsigned int dest)
{
	unsigned int cfg;

	cfg = __get_ICR2();
	cfg |= SET_APIC_DEST_FIELD(dest);

	return cfg;
}

static inline void __send_IPI_shortcut(unsigned int shortcut, int vector)
{
	unsigned int cfg;
/*
 * Subtle. In the case of the 'never do double writes' workaround we
 * have to lock out interrupts to be safe. Otherwise it's just one
 * single atomic write to the APIC, no need for cli/sti.
 */
#if FORCE_APIC_SERIALIZATION
	unsigned long flags;

	__save_flags(flags);
	__cli();
#endif

	/*
	 * No need to touch the target chip field
	 */

	cfg = __prepare_ICR(shortcut, vector);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write(APIC_ICR, cfg);
#if FORCE_APIC_SERIALIZATION
	__restore_flags(flags);
#endif
}

static inline void send_IPI_allbutself(int vector)
{
	__send_IPI_shortcut(APIC_DEST_ALLBUT, vector);
}

static inline void send_IPI_all(int vector)
{
	__send_IPI_shortcut(APIC_DEST_ALLINC, vector);
}

void send_IPI_self(int vector)
{
	__send_IPI_shortcut(APIC_DEST_SELF, vector);
}

static inline void send_IPI_single(int dest, int vector)
{
	unsigned long cfg;
#if FORCE_APIC_SERIALIZATION
	unsigned long flags;

	__save_flags(flags);
	__cli();
#endif

	/*
	 * prepare target chip field
	 */

	cfg = __prepare_ICR2(dest);
	apic_write(APIC_ICR2, cfg);

	/*
	 * program the ICR 
	 */
	cfg = __prepare_ICR(0, vector);
	
	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write(APIC_ICR, cfg);
#if FORCE_APIC_SERIALIZATION
	__restore_flags(flags);
#endif
}

/*
 * This is fraught with deadlocks. Probably the situation is not that
 * bad as in the early days of SMP, so we might ease some of the
 * paranoia here.
 */

void smp_flush_tlb(void)
{
	int cpu = smp_processor_id();
	int stuck;
	unsigned long flags;

	/*
	 * it's important that we do not generate any APIC traffic
	 * until the AP CPUs have booted up!
	 */
	if (cpu_online_map) {
		/*
		 * The assignment is safe because it's volatile so the
		 * compiler cannot reorder it, because the i586 has
		 * strict memory ordering and because only the kernel
		 * lock holder may issue a tlb flush. If you break any
		 * one of those three change this to an atomic bus
		 * locked or.
		 */

		smp_invalidate_needed = cpu_online_map;

		/*
		 * Processors spinning on some lock with IRQs disabled
		 * will see this IRQ late. The smp_invalidate_needed
		 * map will ensure they don't do a spurious flush tlb
		 * or miss one.
		 */
	
		__save_flags(flags);
		__cli();

		send_IPI_allbutself(INVALIDATE_TLB_VECTOR);

		/*
		 * Spin waiting for completion
		 */

		stuck = 50000000;
		while (smp_invalidate_needed) {
			/*
			 * Take care of "crossing" invalidates
			 */
			if (test_bit(cpu, &smp_invalidate_needed))
			clear_bit(cpu, &smp_invalidate_needed);
			--stuck;
			if (!stuck) {
				printk("stuck on TLB IPI wait (CPU#%d)\n",cpu);
				break;
			}
		}
		__restore_flags(flags);
	}

	/*
	 *	Flush the local TLB
	 */
	local_flush_tlb();

}


/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */

void smp_send_reschedule(int cpu)
{
	send_IPI_single(cpu, RESCHEDULE_VECTOR);
}

/*
 * this function sends a 'stop' IPI to all other CPUs in the system.
 * it goes straight through.
 */

void smp_send_stop(void)
{
	send_IPI_allbutself(STOP_CPU_VECTOR);
}

/*
 * this function sends an 'reload MTRR state' IPI to all other CPUs
 * in the system. it goes straight through, completion processing
 * is done on the mttr.c level.
 */

void smp_send_mtrr(void)
{
	send_IPI_allbutself(MTRR_CHANGE_VECTOR);
}

/*
 * Local timer interrupt handler. It does both profiling and
 * process statistics/rescheduling.
 *
 * We do profiling in every local tick, statistics/rescheduling
 * happen only every 'profiling multiplier' ticks. The default
 * multiplier is 1 and it can be changed by writing the new multiplier
 * value into /proc/profile.
 */

void smp_local_timer_interrupt(struct pt_regs * regs)
{
	int cpu = smp_processor_id();

	/*
	 * The profiling function is SMP safe. (nothing can mess
	 * around with "current", and the profiling counters are
	 * updated with atomic operations). This is especially
	 * useful with a profiling multiplier != 1
	 */
	if (!user_mode(regs))
		x86_do_profile(regs->eip);

	if (!--prof_counter[cpu]) {
		int user=0,system=0;
		struct task_struct * p = current;

		/*
		 * After doing the above, we need to make like
		 * a normal interrupt - otherwise timer interrupts
		 * ignore the global interrupt lock, which is the
		 * WrongThing (tm) to do.
		 */

		if (user_mode(regs))
			user=1;
		else
			system=1;

 		irq_enter(cpu, 0);
		if (p->pid) {
			update_one_process(p, 1, user, system, cpu);

			p->counter -= 1;
			if (p->counter < 0) {
				p->counter = 0;
				p->need_resched = 1;
			}
			if (p->priority < DEF_PRIORITY) {
				kstat.cpu_nice += user;
				kstat.per_cpu_nice[cpu] += user;
			} else {
				kstat.cpu_user += user;
				kstat.per_cpu_user[cpu] += user;
			}

			kstat.cpu_system += system;
			kstat.per_cpu_system[cpu] += system;

		}
		prof_counter[cpu]=prof_multiplier[cpu];
		irq_exit(cpu, 0);
	}

	/*
	 * We take the 'long' return path, and there every subsystem
	 * grabs the apropriate locks (kernel lock/ irq lock).
	 *
	 * we might want to decouple profiling from the 'long path',
	 * and do the profiling totally in assembly.
	 *
	 * Currently this isn't too much of an issue (performance wise),
	 * we can take more than 100K local irqs per second on a 100 MHz P5.
	 */
}

/*
 * Local APIC timer interrupt. This is the most natural way for doing
 * local interrupts, but local timer interrupts can be emulated by
 * broadcast interrupts too. [in case the hw doesnt support APIC timers]
 *
 * [ if a single-CPU system runs an SMP kernel then we call the local
 *   interrupt as well. Thus we cannot inline the local irq ... ]
 */
void smp_apic_timer_interrupt(struct pt_regs * regs)
{
	/*
	 * NOTE! We'd better ACK the irq immediately,
	 * because timer handling can be slow, and we
	 * want to be able to accept NMI tlb invalidates
	 * during this time.
	 */
	ack_APIC_irq();
	smp_local_timer_interrupt(regs);
}

/*
 * Reschedule call back. Nothing to do,
 * all the work is done automatically when
 * we return from the interrupt.
 */
asmlinkage void smp_reschedule_interrupt(void)
{
	ack_APIC_irq();
}

/*
 * Invalidate call-back
 */
asmlinkage void smp_invalidate_interrupt(void)
{
	if (test_and_clear_bit(smp_processor_id(), &smp_invalidate_needed))
		local_flush_tlb();

	ack_APIC_irq();
}

static void stop_this_cpu (void)
{
	/*
	 * Remove this CPU:
	 */
	clear_bit(smp_processor_id(), &cpu_online_map);

	if (cpu_data[smp_processor_id()].hlt_works_ok)
		for(;;) __asm__("hlt");
	for (;;);
}

/*
 *	CPU halt call-back
 */
asmlinkage void smp_stop_cpu_interrupt(void)
{
	stop_this_cpu();
}

void (*mtrr_hook) (void) = NULL;

asmlinkage void smp_mtrr_interrupt(void)
{
	ack_APIC_irq();
	if (mtrr_hook) (*mtrr_hook)();
}

/*
 * This interrupt should _never_ happen with our APIC/SMP architecture
 */
asmlinkage void smp_spurious_interrupt(void)
{
	/* ack_APIC_irq();   see sw-dev-man vol 3, chapter 7.4.13.5 */
	printk("spurious APIC interrupt, ayiee, should never happen.\n");
}

/*
 * This part sets up the APIC 32 bit clock in LVTT1, with HZ interrupts
 * per second. We assume that the caller has already set up the local
 * APIC.
 *
 * The APIC timer is not exactly sync with the external timer chip, it
 * closely follows bus clocks.
 */

#define RDTSC(x)	__asm__ __volatile__ (  "rdtsc" \
				:"=a" (((unsigned long*)&x)[0]),  \
				 "=d" (((unsigned long*)&x)[1]))

/*
 * The timer chip is already set up at HZ interrupts per second here,
 * but we do not accept timer interrupts yet. We only allow the BP
 * to calibrate.
 */
static unsigned int __init get_8254_timer_count(void)
{
	unsigned int count;

	outb_p(0x00, 0x43);
	count = inb_p(0x40);
	count |= inb_p(0x40) << 8;

	return count;
}

/*
 * This function sets up the local APIC timer, with a timeout of
 * 'clocks' APIC bus clock. During calibration we actually call
 * this function twice, once with a bogus timeout value, second
 * time for real. The other (noncalibrating) CPUs call this
 * function only once, with the real value.
 *
 * We are strictly in irqs off mode here, as we do not want to
 * get an APIC interrupt go off accidentally.
 *
 * We do reads before writes even if unnecessary, to get around the
 * APIC double write bug.
 */

#define APIC_DIVISOR 16

void setup_APIC_timer(unsigned int clocks)
{
	unsigned long lvtt1_value;
	unsigned int tmp_value;

	/*
	 * Unfortunately the local APIC timer cannot be set up into NMI
	 * mode. With the IO APIC we can re-route the external timer
	 * interrupt and broadcast it as an NMI to all CPUs, so no pain.
	 */
	tmp_value = apic_read(APIC_LVTT);
	lvtt1_value = APIC_LVT_TIMER_PERIODIC | LOCAL_TIMER_VECTOR;
	apic_write(APIC_LVTT , lvtt1_value);

	/*
	 * Divide PICLK by 16
	 */
	tmp_value = apic_read(APIC_TDCR);
	apic_write(APIC_TDCR , (tmp_value & ~APIC_TDR_DIV_1 )
				 | APIC_TDR_DIV_16);

	tmp_value = apic_read(APIC_TMICT);
	apic_write(APIC_TMICT, clocks/APIC_DIVISOR);
}

void __init wait_8254_wraparound(void)
{
	unsigned int curr_count, prev_count=~0;
	int delta;

	curr_count = get_8254_timer_count();

	do {
		prev_count = curr_count;
		curr_count = get_8254_timer_count();
		delta = curr_count-prev_count;

	/*
	 * This limit for delta seems arbitrary, but it isn't, it's
	 * slightly above the level of error a buggy Mercury/Neptune
	 * chipset timer can cause.
	 */

	} while (delta<300);
}

/*
 * In this function we calibrate APIC bus clocks to the external
 * timer. Unfortunately we cannot use jiffies and the timer irq
 * to calibrate, since some later bootup code depends on getting
 * the first irq? Ugh.
 *
 * We want to do the calibration only once since we
 * want to have local timer irqs syncron. CPUs connected
 * by the same APIC bus have the very same bus frequency.
 * And we want to have irqs off anyways, no accidental
 * APIC irq that way.
 */

int __init calibrate_APIC_clock(void)
{
	unsigned long long t1,t2;
	long tt1,tt2;
	long calibration_result;
	int i;

	printk("calibrating APIC timer ... ");

	/*
	 * Put whatever arbitrary (but long enough) timeout
	 * value into the APIC clock, we just want to get the
	 * counter running for calibration.
	 */
	setup_APIC_timer(1000000000);

	/*
	 * The timer chip counts down to zero. Let's wait
	 * for a wraparound to start exact measurement:
	 * (the current tick might have been already half done)
	 */

	wait_8254_wraparound ();

	/*
	 * We wrapped around just now. Let's start:
	 */
	RDTSC(t1);
	tt1=apic_read(APIC_TMCCT);

#define LOOPS (HZ/10)
	/*
	 * Let's wait LOOPS wraprounds:
	 */
	for (i=0; i<LOOPS; i++)
		wait_8254_wraparound ();

	tt2=apic_read(APIC_TMCCT);
	RDTSC(t2);

	/*
	 * The APIC bus clock counter is 32 bits only, it
	 * might have overflown, but note that we use signed
	 * longs, thus no extra care needed.
	 *
	 * underflown to be exact, as the timer counts down ;)
	 */

	calibration_result = (tt1-tt2)*APIC_DIVISOR/LOOPS;

	SMP_PRINTK(("\n..... %ld CPU clocks in 1 timer chip tick.",
			 (unsigned long)(t2-t1)/LOOPS));

	SMP_PRINTK(("\n..... %ld APIC bus clocks in 1 timer chip tick.",
			 calibration_result));


	printk("\n..... CPU clock speed is %ld.%04ld MHz.\n",
		((long)(t2-t1)/LOOPS)/(1000000/HZ),
		((long)(t2-t1)/LOOPS)%(1000000/HZ)  );

	printk("..... system bus clock speed is %ld.%04ld MHz.\n",
		calibration_result/(1000000/HZ),
		calibration_result%(1000000/HZ)  );
#undef LOOPS

	return calibration_result;
}

static unsigned int calibration_result;

void __init setup_APIC_clock(void)
{
	unsigned long flags;

	static volatile int calibration_lock;

	__save_flags(flags);
	__cli();

	SMP_PRINTK(("setup_APIC_clock() called.\n"));

	/*
	 * [ setup_APIC_clock() is called from all CPUs, but we want
	 *   to do this part of the setup only once ... and it fits
	 *   here best ]
	 */
	if (!test_and_set_bit(0,&calibration_lock)) {

		calibration_result=calibrate_APIC_clock();
		/*
	 	 * Signal completion to the other CPU[s]:
	 	 */
		calibration_lock = 3;

	} else {
		/*
		 * Other CPU is calibrating, wait for finish:
		 */
		SMP_PRINTK(("waiting for other CPU calibrating APIC ... "));
		while (calibration_lock == 1);
		SMP_PRINTK(("done, continuing.\n"));
	}

/*
 * Now set up the timer for real.
 */

	setup_APIC_timer (calibration_result);

	/*
	 * We ACK the APIC, just in case there is something pending.
	 */

	ack_APIC_irq ();

	__restore_flags(flags);
}

/*
 * the frequency of the profiling timer can be changed
 * by writing a multiplier value into /proc/profile.
 *
 * usually you want to run this on all CPUs ;)
 */
int setup_profiling_timer(unsigned int multiplier)
{
	int cpu = smp_processor_id();
	unsigned long flags;

	/*
	 * Sanity check. [at least 500 APIC cycles should be
	 * between APIC interrupts as a rule of thumb, to avoid
	 * irqs flooding us]
	 */
	if ( (!multiplier) || (calibration_result/multiplier < 500))
		return -EINVAL;

	save_flags(flags);
	cli();
	setup_APIC_timer(calibration_result/multiplier);
	prof_multiplier[cpu]=multiplier;
	restore_flags(flags);

	return 0;
}

#undef APIC_DIVISOR
