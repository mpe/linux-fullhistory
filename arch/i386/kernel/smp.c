/*
 *	Intel MP v1.1/v1.4 specification support routines for multi-pentium
 *	hosts.
 *
 *	(c) 1995 Alan Cox, CymruNET Ltd  <alan@cymru.net>
 *	Supported by Caldera http://www.caldera.com.
 *	Much of the core SMP work is based on previous work by Thomas Radke, to
 *	whom a great many thanks are extended.
 *
 *	Thanks to Intel for making available several different Pentium and
 *	Pentium Pro MP machines.
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
 */

#include <linux/config.h>
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

#ifdef CONFIG_MTRR
#  include <asm/mtrr.h>
#endif

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#include "irq.h"

spinlock_t semaphore_wake_lock = SPIN_LOCK_UNLOCKED;

extern unsigned long start_kernel, _etext;
extern void update_one_process( struct task_struct *p,
				unsigned long ticks, unsigned long user,
				unsigned long system, int cpu);
/*
 *	Some notes on processor bugs:
 *
 *	Pentium and Pentium Pro (and all CPU's) have bugs. The Linux issues
 *	for SMP are handled as follows.
 *
 *	Pentium Pro
 *		Occasional delivery of 'spurious interrupt' as trap #16. This
 *	is very very rare. The kernel logs the event and recovers
 *
 *	Pentium
 *		There is a marginal case where REP MOVS on 100MHz SMP
 *	machines with B stepping processors can fail. XXX should provide
 *	an L1cache=Writethrough or L1cache=off option.
 *
 *		B stepping CPU's may hang. There are hardware work arounds
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


/*
 *	Why isn't this somewhere standard ??
 */

extern __inline int max(int a,int b)
{
	if(a>b)
		return a;
	return b;
}

static int smp_b_stepping = 0;				/* Set if we find a B stepping CPU			*/

static int max_cpus = -1;				/* Setup configured maximum number of CPUs to activate	*/
int smp_found_config=0;					/* Have we found an SMP box 				*/

unsigned long cpu_present_map = 0;			/* Bitmask of existing CPU's 				*/
int smp_num_cpus = 1;					/* Total count of live CPU's 				*/
int smp_threads_ready=0;				/* Set when the idlers are all forked 			*/
volatile int cpu_number_map[NR_CPUS];			/* which CPU maps to which logical number		*/
volatile int __cpu_logical_map[NR_CPUS];			/* which logical number maps to which CPU		*/
volatile unsigned long cpu_callin_map[NR_CPUS] = {0,};	/* We always use 0 the rest is ready for parallel delivery */
volatile unsigned long smp_invalidate_needed;		/* Used for the invalidate map that's also checked in the spinlock */
volatile unsigned long kstack_ptr;			/* Stack vector for booting CPU's			*/
struct cpuinfo_x86 cpu_data[NR_CPUS];			/* Per cpu bogomips and other parameters 		*/
static unsigned int num_processors = 1;			/* Internal processor count				*/
static unsigned long io_apic_addr = 0xFEC00000;		/* Address of the I/O apic (not yet used) 		*/
unsigned char boot_cpu_id = 0;				/* Processor that is doing the boot up 			*/
static int smp_activated = 0;				/* Tripped once we need to start cross invalidating 	*/
int apic_version[NR_CPUS];				/* APIC version number					*/
static volatile int smp_commenced=0;			/* Tripped when we start scheduling 		    	*/
unsigned long apic_retval;				/* Just debugging the assembler.. 			*/

static volatile unsigned char smp_cpu_in_msg[NR_CPUS];	/* True if this processor is sending an IPI		*/

volatile unsigned long kernel_flag=0;			/* Kernel spinlock 					*/
volatile unsigned char active_kernel_processor = NO_PROC_ID;	/* Processor holding kernel spinlock		*/
volatile unsigned long kernel_counter=0;		/* Number of times the processor holds the lock		*/
volatile unsigned long syscall_count=0;			/* Number of times the processor holds the syscall lock	*/

volatile unsigned long ipi_count;			/* Number of IPI's delivered				*/

volatile unsigned long  smp_proc_in_lock[NR_CPUS] = {0,};/* for computing process time */
volatile int smp_process_available=0;

const char lk_lockmsg[] = "lock from interrupt context at %p\n"; 

int mp_bus_id_to_type [MAX_MP_BUSSES] = { -1, };
extern int mp_irq_entries;
extern struct mpc_config_intsrc mp_irqs [MAX_IRQ_SOURCES];
extern int mpc_default_type;
int mp_bus_id_to_pci_bus [MAX_MP_BUSSES] = { -1, };
int mp_current_pci_id = 0;
unsigned long mp_lapic_addr = 0;

/* #define SMP_DEBUG */

#ifdef SMP_DEBUG
#define SMP_PRINTK(x)	printk x
#else
#define SMP_PRINTK(x)
#endif

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

__initfunc(void smp_setup(char *str, int *ints))
{
	if (ints && ints[0] > 0)
		max_cpus = ints[1];
	else
		max_cpus = 0;
}

void ack_APIC_irq (void)
{
	/* Clear the IPI */

	/* Dummy read */
	apic_read(APIC_SPIV);

	/* Docs say use 0 for future compatibility */
	apic_write(APIC_EOI, 0);
}

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
	if(family==0x6)
		return("Pentium(tm) Pro");
	if(family==0x5)
		return("Pentium(tm)");
	if(family==0x0F && model==0x0F)
		return("Special controller");
	if(family==0x04 && model<9)
		return model_defs[model];
	sprintf(n,"Unknown CPU [%d:%d]",family, model);
	return n;
}

/*
 *	Read the MPC
 */

__initfunc(static int smp_read_mpc(struct mp_config_table *mpc))
{
	char str[16];
	int count=sizeof(*mpc);
	int apics=0;
	unsigned char *mpt=((unsigned char *)mpc)+count;

	if(memcmp(mpc->mpc_signature,MPC_SIGNATURE,4))
	{
		printk("Bad signature [%c%c%c%c].\n",
			mpc->mpc_signature[0],
			mpc->mpc_signature[1],
			mpc->mpc_signature[2],
			mpc->mpc_signature[3]);
		return 1;
	}
	if(mpf_checksum((unsigned char *)mpc,mpc->mpc_length))
	{
		printk("Checksum error.\n");
		return 1;
	}
	if(mpc->mpc_spec!=0x01 && mpc->mpc_spec!=0x04)
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
				if(m->mpc_cpuflag&CPU_ENABLED)
				{
					printk("Processor #%d %s APIC version %d\n",
						m->mpc_apicid,
						mpc_family((m->mpc_cpufeature&
							CPU_FAMILY_MASK)>>8,
							(m->mpc_cpufeature&
								CPU_MODEL_MASK)>>4),
						m->mpc_apicver);
#ifdef SMP_DEBUG
					if(m->mpc_featureflag&(1<<0))
						printk("    Floating point unit present.\n");
					if(m->mpc_featureflag&(1<<7))
						printk("    Machine Exception supported.\n");
					if(m->mpc_featureflag&(1<<8))
						printk("    64 bit compare & exchange supported.\n");
					if(m->mpc_featureflag&(1<<9))
						printk("    Internal APIC present.\n");
#endif
					if(m->mpc_cpuflag&CPU_BOOTPROCESSOR)
					{
						SMP_PRINTK(("    Bootup CPU\n"));
						boot_cpu_id=m->mpc_apicid;
					}
					else	/* Boot CPU already counted */
						num_processors++;

					if(m->mpc_apicid>NR_CPUS)
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
				if(m->mpc_flags&MPC_APIC_USABLE)
				{
					apics++;
					printk("I/O APIC #%d Version %d at 0x%lX.\n",
						m->mpc_apicid,m->mpc_apicver,
						m->mpc_apicaddr);
					io_apic_addr = (unsigned long)phys_to_virt(m->mpc_apicaddr);
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
	if(apics>1)
		printk("Warning: Multiple APIC's not supported.\n");
	return num_processors;
}

/*
 *	Scan the memory blocks for an SMP configuration block.
 */

__initfunc(int smp_scan_config(unsigned long base, unsigned long length))
{
	unsigned long *bp=phys_to_virt(base);
	struct intel_mp_floating *mpf;

	SMP_PRINTK(("Scan SMP from %p for %ld bytes.\n",
		bp,length));
	if(sizeof(*mpf)!=16)
		printk("Error: MPF size\n");

	while(length>0)
	{
		if(*bp==SMP_MAGIC_IDENT)
		{
			mpf=(struct intel_mp_floating *)bp;
			if(mpf->mpf_length==1 &&
				!mpf_checksum((unsigned char *)bp,16) &&
				(mpf->mpf_specification == 1
				 || mpf->mpf_specification == 4) )
			{
				printk("Intel MultiProcessor Specification v1.%d\n", mpf->mpf_specification);
				if(mpf->mpf_feature2&(1<<7))
					printk("    IMCR and PIC compatibility mode.\n");
				else
					printk("    Virtual Wire compatibility mode.\n");
				smp_found_config=1;
				/*
				 *	Now see if we need to read further.
				 */
				if(mpf->mpf_feature1!=0)
				{
					unsigned long cfg;

					/*
					 *	We need to know what the local
					 *	APIC id of the boot CPU is!
					 */

/*
 *
 *	HACK HACK HACK HACK HACK HACK HACK HACK HACK HACK HACK HACK HACK
 *
 *	It's not just a crazy hack...  ;-)
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
				if(mpf->mpf_feature1>4)
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
				if(mpf->mpf_physptr)
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

__initfunc(static unsigned long setup_trampoline(void))
{
	memcpy(trampoline_base, trampoline_data, trampoline_end - trampoline_data);
	return virt_to_phys(trampoline_base);
}

/*
 *	We are called very early to get the low memory for the
 *	SMP bootup trampoline page.
 */
__initfunc(unsigned long smp_alloc_memory(unsigned long mem_base))
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

__initfunc(void smp_store_cpu_info(int id))
{
	struct cpuinfo_x86 *c=&cpu_data[id];

	*c = boot_cpu_data;
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
 *	At the end of this all the AP's will hit the system scheduling and off
 *	we go. Each AP will load the system gdt's and jump through the kernel
 *	init into idle(). At this point the scheduler will one day take over
 * 	and give them jobs to do. smp_callin is a standard routine
 *	we use to track CPU's as they power up.
 */

__initfunc(void smp_commence(void))
{
	/*
	 *	Lets the callin's below out of their loop.
	 */
	SMP_PRINTK(("Setting commenced=1, go go go\n"));
	smp_commenced=1;
}

__initfunc(void enable_local_APIC(void))
{
	unsigned long value;

 	value = apic_read(APIC_SPIV);
 	value |= (1<<8);		/* Enable APIC (bit==1) */
 	value &= ~(1<<9);		/* Enable focus processor (bit==0) */
 	apic_write(APIC_SPIV,value);

	udelay(100);			/* B safe */
}

__initfunc(void smp_callin(void))
{
	extern void calibrate_delay(void);
	int cpuid=GET_APIC_ID(apic_read(APIC_ID));

	/*
	 *	Activate our APIC
	 */
	
	SMP_PRINTK(("CALLIN %d %d\n",hard_smp_processor_id(), smp_processor_id()));
	enable_local_APIC();

	/*
	 * Set up our APIC timer.
	 */
	setup_APIC_clock();

 	sti();
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

static int cpucount = 0;

extern int cpu_idle(void * unused);

/*
 *	Activate a secondary processor.
 */
__initfunc(int start_secondary(void *unused))
{
#ifdef CONFIG_MTRR
	/*  Must be done before calibration delay is computed  */
	mtrr_init_secondary_cpu ();
#endif
	smp_callin();
	while (!smp_commenced)
		barrier();
	return cpu_idle(NULL);
}

/*
 * Everything has been set up for the secondary
 * CPU's - they just need to reload everything
 * from the task structure
 */
__initfunc(void initialize_secondary(void))
{
	struct thread_struct * p = &current->tss;

	/*
	 * We don't actually need to load the full TSS,
	 * basically just the stack pointer and the eip.
	 */
	asm volatile("lldt %%ax": :"a" (p->ldt));
	asm volatile("ltr %%ax": :"a" (p->tr));
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

__initfunc(static void do_boot_cpu(int i))
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
	cfg |= (APIC_DEST_FIELD | APIC_DEST_LEVELTRIG
		| APIC_DEST_ASSERT | APIC_DEST_DM_INIT);
	apic_write(APIC_ICR, cfg);						/* Send IPI */

	udelay(200);
	SMP_PRINTK(("Deasserting INIT.\n"));

	cfg=apic_read(APIC_ICR2);
	cfg&=0x00FFFFFF;
	apic_write(APIC_ICR2, cfg|SET_APIC_DEST_FIELD(i));			/* Target chip     	*/
	cfg=apic_read(APIC_ICR);
	cfg&=~0xCDFFF;								/* Clear bits 		*/
	cfg |= (APIC_DEST_FIELD | APIC_DEST_LEVELTRIG
				| APIC_DEST_DM_INIT);
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
		cfg |= (APIC_DEST_FIELD
			| APIC_DEST_DM_STARTUP
			| (start_eip >> 12));						/* Boot on the stack 	*/
		SMP_PRINTK(("Before start apic_write.\n"));
		apic_write(APIC_ICR, cfg);						/* Kick the second 	*/

		SMP_PRINTK(("Startup point 1.\n"));
		timeout = 0;
		do {
			SMP_PRINTK(("Sleeping.\n")); udelay(1000000);
			udelay(10);
		} while ( (send_status = (apic_read(APIC_ICR) & 0x1000))
			  && (timeout++ < 1000));
		udelay(200);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
	}
	SMP_PRINTK(("After Startup.\n"));

	if (send_status)		/* APIC never delivered?? */
		printk("APIC never delivered???\n");
	if (accept_status)		/* Send accept error */
		printk("APIC delivery error (%lx).\n", accept_status);

	if( !(send_status || accept_status) )
	{
		for(timeout=0;timeout<50000;timeout++)
		{
			if(cpu_callin_map[0]&(1<<i))
				break;				/* It has booted */
			udelay(100);				/* Wait 5s total for a response */
		}
		if(cpu_callin_map[0]&(1<<i))
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
			if(*((volatile unsigned char *)phys_to_virt(8192))==0xA5)
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

unsigned int prof_multiplier[NR_CPUS];
unsigned int prof_counter[NR_CPUS];

/*
 *	Cycle through the processors sending APIC IPI's to boot each.
 */

__initfunc(void smp_boot_cpus(void))
{
	int i;
	unsigned long cfg;

#ifdef CONFIG_MTRR
	/*  Must be done before other processors booted  */
	mtrr_init_boot_cpu ();
#endif
	/*
	 *	Initialize the logical to physical cpu number mapping
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
	printk("CPU%d: ", boot_cpu_id);
	print_cpu_info(&cpu_data[boot_cpu_id]);

	cpu_present_map |= (1 << hard_smp_processor_id());
	cpu_number_map[boot_cpu_id] = 0;
	active_kernel_processor=boot_cpu_id;

	/*
	 *	If we don't conform to the Intel MPS standard, get out
	 *	of here now!
	 */

	if (!smp_found_config)
	{
		printk(KERN_NOTICE "SMP motherboard not detected. Using dummy APIC emulation.\n");
		io_apic_irqs = 0;
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
	 *	Now scan the cpu present map and fire up the other CPUs.
	 */

	SMP_PRINTK(("CPU map: %lx\n", cpu_present_map));

	for(i=0;i<NR_CPUS;i++)
	{
		/*
		 *	Don't even attempt to start the boot CPU!
		 */
		if (i == boot_cpu_id)
			continue;

		if ((cpu_present_map & (1 << i))
		    && (max_cpus < 0 || max_cpus > cpucount+1))
		{
			do_boot_cpu(i);
		}

		/*
		 *	Make sure we unmap all failed CPUs
		 */
		
		if (cpu_number_map[i] == -1 && (cpu_present_map & (1 << i))) {
			printk("CPU #%d not responding. Removing from cpu_present_map.\n",i);
			cpu_present_map &= ~(1 << i);
                }
        }

	/*
	 *	Cleanup possible dangling ends...
	 */

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

	/*
	 *	Allow the user to impress friends.
	 */

	SMP_PRINTK(("Before bogomips.\n"));
	if(cpucount==0)
	{
		printk(KERN_ERR "Error: only one processor found.\n");
		cpu_present_map=(1<<hard_smp_processor_id());
	}
	else
	{
		unsigned long bogosum=0;
		for(i=0;i<32;i++)
		{
			if(cpu_present_map&(1<<i))
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
	if(smp_b_stepping)
		printk(KERN_WARNING "WARNING: SMP operation may be unreliable with B stepping processors.\n");
	SMP_PRINTK(("Boot done.\n"));

	/*
	 * Here we can be sure that there is an IO-APIC in the system, lets
	 * go and set it up:
	 */
	setup_IO_APIC();

smp_done:
#ifdef CONFIG_MTRR
	/*  Must be done after other processors booted  */
	mtrr_init ();
#endif
}


void send_IPI (int dest, int vector)
{
	unsigned long cfg;
	unsigned long flags;

	__save_flags(flags);
	__cli();

	/*
	 * prepare target chip field
	 */

	cfg = apic_read(APIC_ICR2) & 0x00FFFFFF;
	apic_write(APIC_ICR2, cfg|SET_APIC_DEST_FIELD(dest));

	cfg = apic_read(APIC_ICR);
	cfg &= ~0xFDFFF;
	cfg |= APIC_DEST_FIELD|APIC_DEST_DM_FIXED|vector;
	cfg |= dest;
	
	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	
	apic_write(APIC_ICR, cfg);
	__restore_flags(flags);
}

void funny (void)
{
	send_IPI(APIC_DEST_ALLBUT,0x30 /*IO_APIC_VECTOR(11)*/);
	for(;;)__cli();
}

/*
 * A non wait message cannot pass data or cpu source info. This current setup
 * is only safe because the kernel lock owner is the only person who can send
 * a message.
 *
 * Wrapping this whole block in a spinlock is not the safe answer either. A
 * processor may get stuck with irq's off waiting to send a message and thus
 * not replying to the person spinning for a reply....
 *
 * In the end flush tlb ought to be the NMI and a very very short function
 * (to avoid the old IDE disk problems), and other messages sent with IRQ's
 * enabled in a civilised fashion. That will also boost performance.
 */

void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	unsigned long cfg;
	unsigned long dest = 0;
	unsigned long target_map;
	int p=smp_processor_id();
	int irq;
	int ct=0;

	/*
	 *	During boot up send no messages
	 */
	
	if(!smp_activated || !smp_commenced)
		return;


	/*
	 *	Skip the reschedule if we are waiting to clear a
	 *	message at this time. The reschedule cannot wait
	 *	but is not critical.
	 */

	switch (msg) {
		case MSG_RESCHEDULE:
			irq = 0x30;
			if (smp_cpu_in_msg[p])
				return;
			break;

		case MSG_INVALIDATE_TLB:
			/* make this a NMI some day */
			irq = 0x31;
			break;

		case MSG_STOP_CPU:
			irq = 0x40;
			break;

		case MSG_MTRR_CHANGE:
			irq = 0x50;
			break;

		default:
			printk("Unknown SMP message %d\n", msg);
			return;
	}

	/*
	 * Sanity check we don't re-enter this across CPU's. Only the kernel
	 * lock holder may send messages. For a STOP_CPU we are bringing the
	 * entire box to the fastest halt we can.. A reschedule carries
	 * no data and can occur during a flush.. guess what panic
	 * I got to notice this bug...
	 */
	
	/*
	 *	We are busy
	 */
	
	smp_cpu_in_msg[p]++;

/*	printk("SMP message pass #%d to %d of %d\n",
		p, msg, target);*/

	/*
	 * Wait for the APIC to become ready - this should never occur. Its
	 * a debugging check really.
	 */
	
	while (ct<1000)
	{
		cfg=apic_read(APIC_ICR);
		if(!(cfg&(1<<12)))
			break;
		ct++;
		udelay(10);
	}

	/*
	 *	Just pray... there is nothing more we can do
	 */
	
	if(ct==1000)
		printk("CPU #%d: previous IPI still not cleared after 10mS\n", p);

	/*
	 *	Set the target requirement
	 */
	
	if(target==MSG_ALL_BUT_SELF)
	{
		dest=APIC_DEST_ALLBUT;
		target_map=cpu_present_map;
		cpu_callin_map[0]=(1<<p);
	}
	else if(target==MSG_ALL)
	{
		dest=APIC_DEST_ALLINC;
		target_map=cpu_present_map;
		cpu_callin_map[0]=0;
	}
	else
		panic("huh?");

	/*
	 * Program the APIC to deliver the IPI
	 */

	send_IPI(dest,irq);

	/*
	 * Spin waiting for completion
	 */
	
	switch(wait)
	{
		int stuck;
		case 1:
			stuck = 50000000;
			while(cpu_callin_map[0]!=target_map) {
				--stuck;
				if (!stuck) {
					printk("stuck on target_map IPI wait\n");
					break;
				}
			}
			break;
		case 2:
			stuck = 50000000;
			/* Wait for invalidate map to clear */
			while (smp_invalidate_needed) {
				/* Take care of "crossing" invalidates */
				if (test_bit(p, &smp_invalidate_needed))
					clear_bit(p, &smp_invalidate_needed);
				--stuck;
				if (!stuck) {
					printk("stuck on smp_invalidate_needed IPI wait (CPU#%d)\n",p);
					break;
				}
			}
			break;
	}

	/*
	 *	Record our completion
	 */
	
	smp_cpu_in_msg[p]--;
}

/*
 *	This is fraught with deadlocks. Linus does a flush tlb at a whim
 *	even with IRQ's off. We have to avoid a pair of crossing flushes
 *	or we are doomed.  See the notes about smp_message_pass.
 */

void smp_flush_tlb(void)
{
	unsigned long flags;

#if 0
	if(smp_activated && smp_processor_id()!=active_kernel_processor) {
		printk("CPU #%d:Attempted flush tlb IPI when not AKP(=%d)\n",smp_processor_id(),active_kernel_processor);
		*(char *)0=0;
	}
#endif
/*	printk("SMI-");*/

	/*
	 *	The assignment is safe because it's volatile so the compiler cannot reorder it,
	 *	because the i586 has strict memory ordering and because only the kernel lock holder
	 *	may issue a tlb flush. If you break any one of those three change this to an atomic
	 *	bus locked or.
	 */

	smp_invalidate_needed=cpu_present_map;

	/*
	 *	Processors spinning on the lock will see this IRQ late. The smp_invalidate_needed map will
	 *	ensure they don't do a spurious flush tlb or miss one.
	 */
	
	__save_flags(flags);
	__cli();
	smp_message_pass(MSG_ALL_BUT_SELF, MSG_INVALIDATE_TLB, 0L, 2);

	/*
	 *	Flush the local TLB
	 */
	
	local_flush_tlb();

	__restore_flags(flags);

	/*
	 *	Completed.
	 */
	
/*	printk("SMID\n");*/
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
		x86_do_profile (regs->eip);

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
				need_resched = 1;
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
	 * Currently this isnt too much of an issue (performance wise),
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
	spin_lock(&irq_controller_lock);
	ack_APIC_irq ();
	spin_unlock(&irq_controller_lock);

	smp_local_timer_interrupt(regs);
}

/*
 *	Reschedule call back
 */
asmlinkage void smp_reschedule_interrupt(void)
{
	int cpu = smp_processor_id();

	ack_APIC_irq();
	for (;;) __cli();
	/*
	 * This looks silly, but we actually do need to wait
	 * for the global interrupt lock.
	 */
	printk("huh, this is used, where???\n");
	irq_enter(cpu, 0);
	need_resched = 1;
	irq_exit(cpu, 0);
}

/*
 * Invalidate call-back
 */
asmlinkage void smp_invalidate_interrupt(void)
{
	if (test_and_clear_bit(smp_processor_id(), &smp_invalidate_needed))
		local_flush_tlb();

	ack_APIC_irq ();
}

/*
 *	CPU halt call-back
 */
asmlinkage void smp_stop_cpu_interrupt(void)
{
	if (cpu_data[smp_processor_id()].hlt_works_ok)
		for(;;) __asm__("hlt");
	for  (;;) ;
}

void (*mtrr_hook) (void) = NULL;

asmlinkage void smp_mtrr_interrupt(void)
{
	ack_APIC_irq ();
	if (mtrr_hook) (*mtrr_hook) ();
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
__initfunc(static unsigned int get_8254_timer_count (void))
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

#define APIC_DIVISOR 1

void setup_APIC_timer (unsigned int clocks)
{
	unsigned long lvtt1_value;
	unsigned int tmp_value;

	/*
	 * Unfortunately the local APIC timer cannot be set up into NMI
	 * mode. With the IO APIC we can re-route the external timer
	 * interrupt and broadcast it as an NMI to all CPUs, so no pain.
	 *
	 * NOTE: this trap vector (0x41) and the gate in
	 * BUILD_SMP_TIMER_INTERRUPT should be the same ;)
	 */
	tmp_value = apic_read(APIC_LVTT);
	lvtt1_value = APIC_LVT_TIMER_PERIODIC | 0x41;
	apic_write(APIC_LVTT , lvtt1_value);

	/*
	 * Divide PICLK by 16
	 */
	tmp_value = apic_read(APIC_TDCR);
	apic_write(APIC_TDCR , (tmp_value & ~APIC_TDR_DIV_1 )
				 | APIC_TDR_DIV_1);

	tmp_value = apic_read(APIC_TMICT);
	apic_write(APIC_TMICT, clocks/APIC_DIVISOR);
}

__initfunc(void wait_8254_wraparound (void))
{
	unsigned int curr_count, prev_count=~0;
	int delta;

	curr_count = get_8254_timer_count();

	do {
		prev_count = curr_count;
		curr_count = get_8254_timer_count();
		delta = curr_count-prev_count;

	/*
	 * This limit for delta seems arbitrary, but it isnt, it's
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

__initfunc(int calibrate_APIC_clock (void))
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
	 * The timer chip counts down to zero. Lets wait
	 * for a wraparound to start exact measurement:
	 * (the current tick might have been already half done)
	 */

	wait_8254_wraparound ();

	/*
	 * We wrapped around just now, lets start:
	 */
	RDTSC(t1);
	tt1=apic_read(APIC_TMCCT);

#define LOOPS (HZ/10)
	/*
	 * lets wait LOOPS wraprounds:
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

	printk("..... APIC bus clock speed is %ld.%04ld MHz.\n",
		calibration_result/(1000000/HZ),
		calibration_result%(1000000/HZ)  );
#undef LOOPS

	return calibration_result;
}

static unsigned int calibration_result;

__initfunc(void setup_APIC_clock (void))
{
	unsigned long flags;

	static volatile int calibration_lock;

	save_flags(flags);
	cli();

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


	restore_flags(flags);
}

/*
 * the frequency of the profiling timer can be changed
 * by writing a multiplier value into /proc/profile.
 *
 * usually you want to run this on all CPUs ;)
 */
int setup_profiling_timer (unsigned int multiplier)
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
	setup_APIC_timer (calibration_result/multiplier);
	prof_multiplier[cpu]=multiplier;
	restore_flags(flags);

	return 0;
}

#undef APIC_DIVISOR
