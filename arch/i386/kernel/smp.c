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
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/smp.h>

/*
 *	Why isn't this somewhere standard ??
 */
 
extern __inline int max(int a,int b)
{
	if(a>b)
		return a;
	return b;
}


int smp_found_config=0;					/* Have we found an SMP box 				*/

unsigned long cpu_present_map = 0;			/* Bitmask of existing CPU's 				*/
int smp_num_cpus = 1;					/* Total count of live CPU's 				*/
int smp_threads_ready=0;				/* Set when the idlers are all forked 			*/
volatile int cpu_number_map[NR_CPUS];			/* which CPU maps to which logical number		*/
volatile int cpu_logical_map[NR_CPUS];			/* which logical number maps to which CPU		*/
volatile unsigned long cpu_callin_map[NR_CPUS] = {0,};	/* We always use 0 the rest is ready for parallel delivery */
volatile unsigned long smp_invalidate_needed;		/* Used for the invalidate map that's also checked in the spinlock */
struct cpuinfo_x86 cpu_data[NR_CPUS];			/* Per cpu bogomips and other parameters 		*/
static unsigned int num_processors = 1;			/* Internal processor count				*/
static unsigned long io_apic_addr = 0xFEC00000;		/* Address of the I/O apic (not yet used) 		*/
unsigned char boot_cpu_id = 0;				/* Processor that is doing the boot up 			*/
static unsigned char *kstack_base,*kstack_end;		/* Kernel stack list pointers 				*/
static int smp_activated = 0;				/* Tripped once we need to start cross invalidating 	*/
int apic_version[NR_CPUS];				/* APIC version number					*/
static volatile int smp_commenced=0;			/* Tripped when we start scheduling 		    	*/
unsigned long apic_addr=0xFEE00000;			/* Address of APIC (defaults to 0xFEE00000)		*/
unsigned long nlong = 0;				/* dummy used for apic_reg address + 0x20		*/
unsigned char *apic_reg=((unsigned char *)(&nlong))-0x20;/* Later set to the vremap() of the APIC 		*/
unsigned long apic_retval;				/* Just debugging the assembler.. 			*/
unsigned char *kernel_stacks[NR_CPUS];			/* Kernel stack pointers for CPU's (debugging)		*/

static volatile unsigned char smp_cpu_in_msg[NR_CPUS];	/* True if this processor is sending an IPI		*/
static volatile unsigned long smp_msg_data;		/* IPI data pointer					*/
static volatile int smp_src_cpu;			/* IPI sender processor					*/
static volatile int smp_msg_id;				/* Message being sent					*/

volatile unsigned long kernel_flag=0;			/* Kernel spinlock 					*/
volatile unsigned char active_kernel_processor = NO_PROC_ID;	/* Processor holding kernel spinlock		*/
volatile unsigned long kernel_counter=0;		/* Number of times the processor holds the lock		*/
volatile unsigned long syscall_count=0;			/* Number of times the processor holds the syscall lock	*/

volatile unsigned long ipi_count;			/* Number of IPI's delivered				*/
#ifdef __SMP_PROF__
volatile unsigned long smp_spins[NR_CPUS]={0};          /* Count interrupt spins 				*/
volatile unsigned long smp_spins_syscall[NR_CPUS]={0};  /* Count syscall spins                   		*/
volatile unsigned long smp_spins_syscall_cur[NR_CPUS]={0};/* Count spins for the actual syscall                 */
volatile unsigned long smp_spins_sys_idle[NR_CPUS]={0}; /* Count spins for sys_idle 				*/
volatile unsigned long smp_idle_count[1+NR_CPUS]={0,};	/* Count idle ticks					*/
#endif
#if defined (__SMP_PROF__)
volatile unsigned long smp_idle_map=0;			/* Map for idle processors 				*/
#endif

volatile unsigned long  smp_proc_in_lock[NR_CPUS] = {0,};/* for computing process time */
volatile unsigned long smp_process_available=0;

/*#define SMP_DEBUG*/

#ifdef SMP_DEBUG
#define SMP_PRINTK(x)	printk x
#else
#define SMP_PRINTK(x)
#endif


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

static int smp_read_mpc(struct mp_config_table *mpc)
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
	printk("OEM ID: %s ",str);
	memcpy(str,mpc->mpc_productid,12);
	str[12]=0;
	printk("Product ID: %s ",str);
	printk("APIC at: 0x%lX\n",mpc->mpc_lapic);

	/* set the local APIC address */
	apic_addr = mpc->mpc_lapic;
	
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
	                                io_apic_addr = m->mpc_apicaddr;
	                        }
                                mpt+=sizeof(*m);
                                count+=sizeof(*m); 
                                break;
			}
			case MP_INTSRC:
			{
				struct mpc_config_intsrc *m=
					(struct mpc_config_intsrc *)mpt;
				
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
 
int smp_scan_config(unsigned long base, unsigned long length)
{
	unsigned long *bp=(unsigned long *)base;
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
					pg0[0] = (apic_addr | 7);
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
					printk("Bus#0 is ");
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

				/*
				 *	Now that the boot CPU id is known,
				 *	set some other information about it.
				 */
				nlong = boot_cpu_id<<24;	/* Dummy 'self' for bootup */
				cpu_logical_map[0] = boot_cpu_id;

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

static unsigned char trampoline_data[]={ 
#include  "trampoline.hex"
};

/*
 *	Currently trivial. Write the real->protected mode
 *	bootstrap into the page concerned. The caller
 *	has made sure it's suitably aligned.
 */
 
static void install_trampoline(unsigned char *mp)
{
	memcpy(mp,trampoline_data,sizeof(trampoline_data));
}

/*
 *	We are called very early to get the low memory for the trampoline/kernel stacks
 *	This has to be done by mm/init.c to parcel us out nice low memory. We allocate
 *	the kernel stacks at 4K, 8K, 12K... currently (0-03FF is preserved for SMM and
 *	other things).
 */
 
unsigned long smp_alloc_memory(unsigned long mem_base)
{
	int size=(num_processors-1)*PAGE_SIZE;		/* Number of stacks needed */
	/*
	 *	Our stacks have to be below the 1Mb line, and mem_base on entry
	 *	is 4K aligned.
	 */
	 
	if(mem_base+size>=0x9F000)
		panic("smp_alloc_memory: Insufficient low memory for kernel stacks.\n");
	kstack_base=(void *)mem_base;
	mem_base+=size;
	kstack_end=(void *)mem_base;
	return mem_base;
}
	
/*
 *	Hand out stacks one at a time.
 */
 
static void *get_kernel_stack(void)
{
	void *stack=kstack_base;
	if(kstack_base>=kstack_end)
		return NULL;
	kstack_base+=PAGE_SIZE;
	return stack;
}


/*
 *	The bootstrap kernel entry code has set these up. Save them for
 *	a given CPU
 */
 
void smp_store_cpu_info(int id)
{
	struct cpuinfo_x86 *c=&cpu_data[id];
	c->hard_math=hard_math;			/* Always assumed same currently */
	c->x86=x86;
	c->x86_model=x86_model;
	c->x86_mask=x86_mask;
	c->x86_capability=x86_capability;
	c->fdiv_bug=fdiv_bug;
	c->wp_works_ok=wp_works_ok;		/* Always assumed the same currently */
	c->hlt_works_ok=hlt_works_ok;
	c->have_cpuid=have_cpuid;
	c->udelay_val=loops_per_sec;
	strcpy(c->x86_vendor_id, x86_vendor_id);
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

void smp_commence(void)
{
	/*
	 *	Lets the callin's below out of their loop.
	 */
	smp_commenced=1;
}
 
void smp_callin(void)
{
	extern void calibrate_delay(void);
	int cpuid=GET_APIC_ID(apic_read(APIC_ID));
	unsigned long l;
	
	/*
	 *	Activate our APIC
	 */
	 
	SMP_PRINTK(("CALLIN %d\n",smp_processor_id()));
 	l=apic_read(APIC_SPIV);
 	l|=(1<<8);		/* Enable */
 	apic_write(APIC_SPIV,l);
 	sti();
	/*
	 *	Get our bogomips.
	 */	
	calibrate_delay();
	/*
	 *	Save our processor parameters
	 */
 	smp_store_cpu_info(cpuid);
	/*
	 *	Allow the master to continue.
	 */	
	set_bit(cpuid, (unsigned long *)&cpu_callin_map[0]);
	/*
	 *	Until we are ready for SMP scheduling
	 */
	load_ldt(0);
/*	printk("Testing faulting...\n");
	*(long *)0=1;		 OOPS... */
	local_flush_tlb();
	while(!smp_commenced);
	if (cpu_number_map[cpuid] == -1)
		while(1);
	local_flush_tlb();
	SMP_PRINTK(("Commenced..\n"));
	
	load_TR(cpu_number_map[cpuid]);
/*	while(1);*/
}

/*
 *	Cycle through the processors sending pentium IPI's to boot each.
 */
 
void smp_boot_cpus(void)
{
	int i;
	int cpucount=0;
	unsigned long cfg;
	void *stack;
	extern unsigned long init_user_stack[];
	
	/*
	 *	Initialize the logical to physical cpu number mapping
	 */

	for (i = 0; i < NR_CPUS; i++)
		cpu_number_map[i] = -1;

	/*
	 *	Setup boot CPU information
	 */
 
	kernel_stacks[boot_cpu_id]=(void *)init_user_stack;	/* Set up for boot processor first */

	smp_store_cpu_info(boot_cpu_id);			/* Final full version of the data */

	cpu_present_map |= (1 << smp_processor_id());
	cpu_number_map[boot_cpu_id] = 0;
	active_kernel_processor=boot_cpu_id;

	/*
	 *	If we don't conform to the Intel MPS standard, get out
	 *	of here now!
	 */

	if (!smp_found_config)
		return;

	/*
	 *	Map the local APIC into kernel space
	 */

	apic_reg = vremap(apic_addr,4096);
	
	if(apic_reg == NULL)
		panic("Unable to map local apic.\n");
		
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
	
	/*
	 *	Enable the local APIC
	 */
 
	cfg=apic_read(APIC_SPIV);
	cfg|=(1<<8);		/* Enable APIC */
	apic_write(APIC_SPIV,cfg);

	udelay(10);
			
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
		
		if (cpu_present_map & (1 << i))
		{
			unsigned long send_status, accept_status;
			int timeout, num_starts, j;
			
			/*
			 *	We need a kernel stack for each processor.
			 */
			
			stack=get_kernel_stack();	/* We allocated these earlier */
			if(stack==NULL)
				panic("No memory for processor stacks.\n");
			kernel_stacks[i]=stack;
			install_trampoline(stack);

			printk("Booting processor %d stack %p: ",i,stack);			/* So we set what's up   */

			/*				
			 *	This grunge runs the startup process for
			 *	the targeted processor.
			 */

			SMP_PRINTK(("Setting warm reset code and vector.\n"));

			/*
			 *	Install a writable page 0 entry.
			 */
			 
			cfg=pg0[0];
			
			CMOS_WRITE(0xa, 0xf);
			pg0[0]=7;
			local_flush_tlb();
			*((volatile unsigned short *) 0x469) = ((unsigned long)stack)>>4;
			*((volatile unsigned short *) 0x467) = 0;
			
			/*
			 *	Protect it again
			 */
			 
			pg0[0]= cfg;
			local_flush_tlb();

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
					| (((int) stack) >> 12) );					/* Boot on the stack 	*/		
				apic_write(APIC_ICR, cfg);						/* Kick the second 	*/

				timeout = 0;
				do {
					udelay(10);
				} while ( (send_status = (apic_read(APIC_ICR) & 0x1000))
					  && (timeout++ < 1000));
				udelay(200);

				accept_status = (apic_read(APIC_ESR) & 0xEF);
			}

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
					cpucount++;
					/* number CPUs logically, starting from 1 (BSP is 0) */
					cpu_number_map[i] = cpucount;
					cpu_logical_map[cpucount] = i;
				}
				else
				{
					if(*((volatile unsigned char *)8192)==0xA5)
						printk("Stuck ??\n");
					else
						printk("Not responding.\n");
				}
			}

			/* mark "stuck" area as not stuck */
			*((volatile unsigned long *)8192) = 0;
		}
		
		/* 
		 *	Make sure we unmap all failed CPUs
		 */
		 
		if (cpu_number_map[i] == -1)
			cpu_present_map &= ~(1 << i);
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

	*((volatile long *) 0x467) = 0;

	/*
	 *	Restore old page 0 entry.
	 */

	pg0[0] = cfg;
	local_flush_tlb();

	/*
	 *	Allow the user to impress friends.
	 */
	
	if(cpucount==0)
	{
		printk("Error: only one processor found.\n");
		cpu_present_map=(1<<smp_processor_id());
	}
	else
	{
		unsigned long bogosum=0;
		for(i=0;i<32;i++)
		{
			if(cpu_present_map&(1<<i))
				bogosum+=cpu_data[i].udelay_val;
		}
		printk("Total of %d processors activated (%lu.%02lu BogoMIPS).\n", 
			cpucount+1, 
			(bogosum+2500)/500000,
			((bogosum+2500)/5000)%100);
		smp_activated=1;
		smp_num_cpus=cpucount+1;
	}
}


/*
 *	A non wait message cannot pass data or cpu source info. This current setup
 *	is only safe because the kernel lock owner is the only person who can send a message.
 *
 *	Wrapping this whole block in a spinlock is not the safe answer either. A processor may
 *	get stuck with irq's off waiting to send a message and thus not replying to the person
 *	spinning for a reply....
 *
 *	In the end flush tlb ought to be the NMI and a very very short function (to avoid the old
 *	IDE disk problems), and other messages sent with IRQ's enabled in a civilised fashion. That
 *	will also boost performance.
 */
 
void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	unsigned long cfg;
	unsigned long target_map;
	int p=smp_processor_id();
	int irq=0x2d;								/* IRQ 13 */
	int ct=0;
	static volatile int message_cpu = NO_PROC_ID;

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
	
	if(msg==MSG_RESCHEDULE)							/* Reschedules we do via trap 0x30 */
	{
		irq=0x30;
		if(smp_cpu_in_msg[p])
			return;
	}

	/*
	 *	Sanity check we don't re-enter this across CPU's. Only the kernel
	 *	lock holder may send messages. For a STOP_CPU we are bringing the
	 *	entire box to the fastest halt we can.. A reschedule carries
	 *	no data and can occur during a flush.. guess what panic
	 *	I got to notice this bug...
	 */
	 
	if(message_cpu!=NO_PROC_ID && msg!=MSG_STOP_CPU && msg!=MSG_RESCHEDULE)
	{
		panic("CPU #%d: Message pass %d but pass in progress by %d of %d\n",
			smp_processor_id(),msg,message_cpu, smp_msg_id);
	}
	message_cpu=smp_processor_id();
	

	/*
	 *	We are busy
	 */
	 	
	smp_cpu_in_msg[p]++;
	
	/*
	 *	Reschedule is currently special
	 */
	 
	if(msg!=MSG_RESCHEDULE)
	{
		smp_src_cpu=p;
		smp_msg_id=msg;
		smp_msg_data=data;
	}
	
/*	printk("SMP message pass #%d to %d of %d\n",
		p, msg, target);*/
	
	/*
	 *	Wait for the APIC to become ready - this should never occur. Its
	 *	a debugging check really.
	 */
	 
	while(ct<1000)
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
		printk("CPU #%d: previous IPI still not cleared after 10mS", smp_processor_id());
		
	/*
	 *	Program the APIC to deliver the IPI
	 */
	 
	cfg=apic_read(APIC_ICR2);
	cfg&=0x00FFFFFF;
	apic_write(APIC_ICR2, cfg|SET_APIC_DEST_FIELD(target));			/* Target chip     		*/
	cfg=apic_read(APIC_ICR);
	cfg&=~0xFDFFF;								/* Clear bits 			*/
	cfg|=APIC_DEST_FIELD|APIC_DEST_DM_FIXED|irq;				/* Send an IRQ 13		*/		

	/*
	 *	Set the target requirement
	 */
	 
	if(target==MSG_ALL_BUT_SELF)
	{
		cfg|=APIC_DEST_ALLBUT;
		target_map=cpu_present_map;
		cpu_callin_map[0]=(1<<smp_src_cpu);
	}
	else if(target==MSG_ALL)
	{
		cfg|=APIC_DEST_ALLINC;
		target_map=cpu_present_map;
		cpu_callin_map[0]=0;
	}
	else
	{
		target_map=(1<<target);
		cpu_callin_map[0]=0;
	}
		
	/*
	 *	Send the IPI. The write to APIC_ICR fires this off.
	 */
	 
	apic_write(APIC_ICR, cfg);	
	
	/*
	 *	Spin waiting for completion
	 */
	 
	switch(wait)
	{
		case 1:
			while(cpu_callin_map[0]!=target_map);		/* Spin on the pass		*/
			break;
		case 2:
			while(smp_invalidate_needed);			/* Wait for invalidate map to clear */
			break;
	}
	
	/*
	 *	Record our completion
	 */
	 
	smp_cpu_in_msg[p]--;
	message_cpu=NO_PROC_ID;
}

/*
 *	This is fraught with deadlocks. Linus does a flush tlb at a whim
 *	even with IRQ's off. We have to avoid a pair of crossing flushes
 *	or we are doomed.  See the notes about smp_message_pass.
 */
 
void smp_flush_tlb(void)
{
	unsigned long flags;
	if(smp_activated && smp_processor_id()!=active_kernel_processor)
		panic("CPU #%d:Attempted flush tlb IPI when not AKP(=%d)\n",smp_processor_id(),active_kernel_processor);
/*	printk("SMI-");*/
	
	/*
	 *	The assignment is safe because it's volatile so the compiler cannot reorder it,
	 *	because the i586 has strict memory ordering and because only the kernel lock holder
	 *	may issue a tlb flush. If you break any one of those three change this to an atomic
	 *	bus locked or.
	 */
	
	smp_invalidate_needed=cpu_present_map&~(1<<smp_processor_id());
	
	/*
	 *	Processors spinning on the lock will see this IRQ late. The smp_invalidate_needed map will
	 *	ensure they don't do a spurious flush tlb or miss one.
	 */
	 
	save_flags(flags);
	cli();
	smp_message_pass(MSG_ALL_BUT_SELF, MSG_INVALIDATE_TLB, 0L, 2);
	
	/*
	 *	Flush the local TLB
	 */
	 
	local_flush_tlb();
	
	restore_flags(flags);
	
	/*
	 *	Completed.
	 */
	 
/*	printk("SMID\n");*/
}

/*	
 *	Reschedule call back
 */

void smp_reschedule_irq(int cpl, struct pt_regs *regs)
{
#ifdef DEBUGGING_SMP_RESCHED
	static int ct=0;
	if(ct==0)
	{
		printk("Beginning scheduling on CPU#%d\n",smp_processor_id());
		ct=1;
	}
#endif	
	if(smp_processor_id()!=active_kernel_processor)
		panic("SMP Reschedule on CPU #%d, but #%d is active.\n",
			smp_processor_id(), active_kernel_processor);

	need_resched=1;

	/*
	 *	Clear the IPI
	 */
	apic_read(APIC_SPIV);		/* Dummy read */
	apic_write(APIC_EOI, 0);	/* Docs say use 0 for future compatibility */
}	

/*
 *	Message call back.
 */
 
void smp_message_irq(int cpl, void *dev_id, struct pt_regs *regs)
{
	int i=smp_processor_id();
/*	static int n=0;
	if(n++<NR_CPUS)
		printk("IPI %d->%d(%d,%ld)\n",smp_src_cpu,i,smp_msg_id,smp_msg_data);*/
	switch(smp_msg_id)
	{
		case 0:	/* IRQ 13 testing - boring */
			return;
			
		/*
		 *	A TLB flush is needed.
		 */
		 
		case MSG_INVALIDATE_TLB:
			if(clear_bit(i,(unsigned long *)&smp_invalidate_needed))
				local_flush_tlb();
			set_bit(i, (unsigned long *)&cpu_callin_map[0]);
		/*	cpu_callin_map[0]|=1<<smp_processor_id();*/
			break;
			
		/*
		 *	Halt other CPU's for a panic or reboot
		 */
		case MSG_STOP_CPU:
			while(1)
			{
				if(cpu_data[smp_processor_id()].hlt_works_ok)
					__asm__("hlt");
			}
		default:
			printk("CPU #%d sent invalid cross CPU message to CPU #%d: %X(%lX).\n",
				smp_src_cpu,smp_processor_id(),smp_msg_id,smp_msg_data);
			break;
	}
	/*
	 *	Clear the IPI, so we can receive future IPI's
	 */
	 
	apic_read(APIC_SPIV);		/* Dummy read */
	apic_write(APIC_EOI, 0);	/* Docs say use 0 for future compatibility */
}
