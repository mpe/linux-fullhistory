/* smp.c: Sparc SMP support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/ptrace.h>
#include <asm/atomic.h>

#include <linux/kernel.h>
#include <linux/tasks.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>

#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/atops.h>
#include <asm/spinlock.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>

#define IRQ_RESCHEDULE		13
#define IRQ_STOP_CPU		14
#define IRQ_CROSS_CALL		15

extern ctxd_t *srmmu_ctx_table_phys;
extern int linux_num_cpus;

extern void calibrate_delay(void);

volatile int smp_processors_ready = 0;

unsigned long cpu_present_map = 0;
int smp_num_cpus = 1;
int smp_threads_ready=0;
unsigned char mid_xlate[NR_CPUS] = { 0, 0, 0, 0, };
volatile unsigned long cpu_callin_map[NR_CPUS] = {0,};
volatile unsigned long smp_spinning[NR_CPUS] = { 0, };
unsigned long smp_proc_in_lock[NR_CPUS] = { 0, };
struct cpuinfo_sparc cpu_data[NR_CPUS];
static unsigned char boot_cpu_id = 0;
static int smp_activated = 0;
volatile int cpu_number_map[NR_CPUS];
volatile int cpu_logical_map[NR_CPUS];

/* The only guaranteed locking primitive available on all Sparc
 * processors is 'ldstub [%reg + immediate], %dest_reg' which atomically
 * places the current byte at the effective address into dest_reg and
 * places 0xff there afterwards.  Pretty lame locking primitive
 * compared to the Alpha and the intel no?  Most Sparcs have 'swap'
 * instruction which is much better...
 */
struct klock_info klock_info = { KLOCK_CLEAR, 0 };

volatile unsigned long ipi_count;
#ifdef __SMP_PROF__
volatile unsigned long smp_spins[NR_CPUS]={0};
volatile unsigned long smp_spins_syscall[NR_CPUS]={0};
volatile unsigned long smp_spins_syscall_cur[NR_CPUS]={0};
volatile unsigned long smp_spins_sys_idle[NR_CPUS]={0};
volatile unsigned long smp_idle_count[1+NR_CPUS]={0,};
#endif
#if defined (__SMP_PROF__)
volatile unsigned long smp_idle_map=0;
#endif

volatile int smp_process_available=0;

/*#define SMP_DEBUG*/

#ifdef SMP_DEBUG
#define SMP_PRINTK(x)	printk x
#else
#define SMP_PRINTK(x)
#endif

static volatile int smp_commenced = 0;

static char smp_buf[512];

/* Not supported on Sparc yet. */
void smp_setup(char *str, int *ints)
{
}

char *smp_info(void)
{
	sprintf(smp_buf,
"        CPU0\t\tCPU1\t\tCPU2\t\tCPU3\n"
"State:  %s\t\t%s\t\t%s\t\t%s\n",
(cpu_present_map & 1) ? ((klock_info.akp == 0) ? "akp" : "online") : "offline",
(cpu_present_map & 2) ? ((klock_info.akp == 1) ? "akp" : "online") : "offline",
(cpu_present_map & 4) ? ((klock_info.akp == 2) ? "akp" : "online") : "offline",
(cpu_present_map & 8) ? ((klock_info.akp == 3) ? "akp" : "online") : "offline");
	return smp_buf;
}

static inline unsigned long swap(volatile unsigned long *ptr, unsigned long val)
{
	__asm__ __volatile__("swap [%1], %0\n\t" :
			     "=&r" (val), "=&r" (ptr) :
			     "0" (val), "1" (ptr));
	return val;
}

/*
 *	The bootstrap kernel entry code has set these up. Save them for
 *	a given CPU
 */

void smp_store_cpu_info(int id)
{
	cpu_data[id].udelay_val = loops_per_sec; /* this is it on sparc. */
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
	local_flush_cache_all();
	local_flush_tlb_all();
	smp_commenced = 1;
	local_flush_cache_all();
	local_flush_tlb_all();
}

void smp_callin(void)
{
	int cpuid = smp_processor_id();

	local_flush_cache_all();
	local_flush_tlb_all();
	set_irq_udt(mid_xlate[boot_cpu_id]);
	calibrate_delay();
	smp_store_cpu_info(cpuid);
	local_flush_cache_all();
	local_flush_tlb_all();

	/* Allow master to continue. */
	swap((unsigned long *)&cpu_callin_map[cpuid], 1);
	local_flush_cache_all();
	local_flush_tlb_all();

	while(!task[cpuid] || current_set[cpuid] != task[cpuid])
		barrier();

	/* Fix idle thread fields. */
	__asm__ __volatile__("ld [%0], %%g6\n\t"
			     : : "r" (&current_set[cpuid])
			     : "memory" /* paranoid */);
	current->mm->mmap->vm_page_prot = PAGE_SHARED;
	current->mm->mmap->vm_start = PAGE_OFFSET;
	current->mm->mmap->vm_end = init_task.mm->mmap->vm_end;
	
	while(!smp_commenced)
		barrier();

	local_flush_cache_all();
	local_flush_tlb_all();

	__sti();
}

void cpu_panic(void)
{
	printk("CPU[%d]: Returns from cpu_idle!\n", smp_processor_id());
	panic("SMP bolixed\n");
}

/*
 *	Cycle through the processors asking the PROM to start each one.
 */
 
extern struct prom_cpuinfo linux_cpus[NCPUS];
static struct linux_prom_registers penguin_ctable;

void smp_boot_cpus(void)
{
	int cpucount = 0;
	int i = 0;
	int first, prev;

	printk("Entering SMP Mode...\n");

	penguin_ctable.which_io = 0;
	penguin_ctable.phys_addr = (unsigned int) srmmu_ctx_table_phys;
	penguin_ctable.reg_size = 0;

	__sti();
	cpu_present_map = 0;
	for(i=0; i < linux_num_cpus; i++)
		cpu_present_map |= (1<<i);
	for(i=0; i < NR_CPUS; i++)
		cpu_number_map[i] = -1;
	for(i=0; i < NR_CPUS; i++)
		cpu_logical_map[i] = -1;
	mid_xlate[boot_cpu_id] = (linux_cpus[boot_cpu_id].mid & ~8);
	cpu_number_map[boot_cpu_id] = 0;
	cpu_logical_map[0] = boot_cpu_id;
	klock_info.akp = boot_cpu_id;
	smp_store_cpu_info(boot_cpu_id);
	set_irq_udt(mid_xlate[boot_cpu_id]);
	local_flush_cache_all();
	if(linux_num_cpus == 1)
		return;  /* Not an MP box. */
	for(i = 0; i < NR_CPUS; i++) {
		if(i == boot_cpu_id)
			continue;

		if(cpu_present_map & (1 << i)) {
			extern unsigned long sparc_cpu_startup;
			unsigned long *entry = &sparc_cpu_startup;
			int timeout;

			/* See trampoline.S for details... */
			entry += ((i-1) * 6);

			/* whirrr, whirrr, whirrrrrrrrr... */
			printk("Starting CPU %d at %p\n", i, entry);
			mid_xlate[i] = (linux_cpus[i].mid & ~8);
			local_flush_cache_all();
			prom_startcpu(linux_cpus[i].prom_node,
				      &penguin_ctable, 0, (char *)entry);

			/* wheee... it's going... */
			for(timeout = 0; timeout < 5000000; timeout++) {
				if(cpu_callin_map[i])
					break;
				udelay(100);
			}
			if(cpu_callin_map[i]) {
				/* Another "Red Snapper". */
				cpucount++;
				cpu_number_map[i] = i;
				cpu_logical_map[i] = i;
			} else {
				printk("Processor %d is stuck.\n", i);
			}
		}
		if(!(cpu_callin_map[i])) {
			cpu_present_map &= ~(1 << i);
			cpu_number_map[i] = -1;
		}
	}
	local_flush_cache_all();
	if(cpucount == 0) {
		printk("Error: only one Processor found.\n");
		cpu_present_map = (1 << smp_processor_id());
	} else {
		unsigned long bogosum = 0;
		for(i = 0; i < NR_CPUS; i++) {
			if(cpu_present_map & (1 << i))
				bogosum += cpu_data[i].udelay_val;
		}
		printk("Total of %d Processors activated (%lu.%02lu BogoMIPS).\n",
		       cpucount + 1,
		       (bogosum + 2500)/500000,
		       ((bogosum + 2500)/5000)%100);
		smp_activated = 1;
		smp_num_cpus = cpucount + 1;
	}

	/* Setup CPU list for IRQ distribution scheme. */
	first = prev = -1;
	for(i = 0; i < NR_CPUS; i++) {
		if(cpu_present_map & (1 << i)) {
			if(first == -1)
				first = i;
			if(prev != -1)
				cpu_data[i].next = i;
			cpu_data[i].mid = mid_xlate[i];
			prev = i;
		}
	}
	cpu_data[prev].next = first;

	/* Ok, they are spinning and ready to go. */
	smp_processors_ready = 1;
}

/* At each hardware IRQ, we get this called to forward IRQ reception
 * to the next processor.  The caller must disable the IRQ level being
 * serviced globally so that there are no double interrupts received.
 */
void smp_irq_rotate(int cpu)
{
	if(smp_processors_ready)
		set_irq_udt(cpu_data[cpu_data[cpu].next].mid);
}

/* Cross calls, in order to work efficiently and atomically do all
 * the message passing work themselves, only stopcpu and reschedule
 * messages come through here.
 */
void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	static unsigned long smp_cpu_in_msg[NR_CPUS];
	unsigned long mask;
	int me = smp_processor_id();
	int irq, i;

	if(msg == MSG_RESCHEDULE) {
		irq = IRQ_RESCHEDULE;

		if(smp_cpu_in_msg[me])
			return;
	} else if(msg == MSG_STOP_CPU) {
		irq = IRQ_STOP_CPU;
	} else {
		goto barf;
	}

	smp_cpu_in_msg[me]++;
	if(target == MSG_ALL_BUT_SELF || target == MSG_ALL) {
		mask = cpu_present_map;
		if(target == MSG_ALL_BUT_SELF)
			mask &= ~(1 << me);
		for(i = 0; i < 4; i++) {
			if(mask & (1 << i))
				set_cpu_int(mid_xlate[i], irq);
		}
	} else {
		set_cpu_int(mid_xlate[target], irq);
	}
	smp_cpu_in_msg[me]--;

	return;
barf:
	printk("Yeeee, trying to send SMP msg(%d) on cpu %d\n", msg, me);
	panic("Bogon SMP message pass.");
}

struct smp_funcall {
	smpfunc_t func;
	unsigned long arg1;
	unsigned long arg2;
	unsigned long arg3;
	unsigned long arg4;
	unsigned long arg5;
	unsigned long processors_in[NR_CPUS];  /* Set when ipi entered. */
	unsigned long processors_out[NR_CPUS]; /* Set when ipi exited. */
} ccall_info;

/* Returns failure code if for example any of the cpu's failed to respond
 * within a certain timeout period.
 */

#define CCALL_TIMEOUT   5000000 /* enough for initial testing */

static spinlock_t cross_call_lock = SPIN_LOCK_UNLOCKED;

/* Cross calls must be serialized, at least currently. */
void smp_cross_call(smpfunc_t func, unsigned long arg1, unsigned long arg2,
		    unsigned long arg3, unsigned long arg4, unsigned long arg5)
{
	unsigned long me = smp_processor_id();
	unsigned long flags, mask;
	int i, timeout;

	if(smp_processors_ready) {
		__save_flags(flags);
		__cli();
		spin_lock(&cross_call_lock);

		/* Init function glue. */
		ccall_info.func = func;
		ccall_info.arg1 = arg1;
		ccall_info.arg2 = arg2;
		ccall_info.arg3 = arg3;
		ccall_info.arg4 = arg4;
		ccall_info.arg5 = arg5;

		/* Init receive/complete mapping. */
		for(i = 0; i < smp_num_cpus; i++) {
			ccall_info.processors_in[i] = 0;
			ccall_info.processors_out[i] = 0;
		}
		ccall_info.processors_in[me] = 1;
		ccall_info.processors_out[me] = 1;

		/* Fire it off. */
		mask = (cpu_present_map & ~(1 << me));
		for(i = 0; i < 4; i++) {
			if(mask & (1 << i))
				set_cpu_int(mid_xlate[i], IRQ_CROSS_CALL);
		}

		/* For debugging purposes right now we can timeout
		 * on both callin and callexit.
		 */
		timeout = CCALL_TIMEOUT;
		for(i = 0; i < smp_num_cpus; i++) {
			while(!ccall_info.processors_in[i] && timeout-- > 0)
				barrier();
			if(!ccall_info.processors_in[i])
				goto procs_time_out;
		}

		/* Run local copy. */
		func(arg1, arg2, arg3, arg4, arg5);

		/* Spin on proc dispersion. */
		timeout = CCALL_TIMEOUT;
		for(i = 0; i < smp_num_cpus; i++) {
			while(!ccall_info.processors_out[i] && timeout-- > 0)
				barrier();
			if(!ccall_info.processors_out[i])
				goto procs_time_out;
		}
		spin_unlock(&cross_call_lock);
		__restore_flags(flags);
		return; /* made it... */

procs_time_out:
		printk("smp: Wheee, penguin drops off the bus\n");
		spin_unlock(&cross_call_lock);
		__restore_flags(flags);
		return; /* why me... why me... */
	}

	/* Just need to run local copy. */
	func(arg1, arg2, arg3, arg4, arg5);
}

void smp_flush_cache_all(void)
{ xc0((smpfunc_t) local_flush_cache_all); }

void smp_flush_tlb_all(void)
{ xc0((smpfunc_t) local_flush_tlb_all); }

void smp_flush_cache_mm(struct mm_struct *mm)
{ 
	if(mm->context != NO_CONTEXT)
		xc1((smpfunc_t) local_flush_cache_mm, (unsigned long) mm);
}

void smp_flush_tlb_mm(struct mm_struct *mm)
{
	if(mm->context != NO_CONTEXT)
		xc1((smpfunc_t) local_flush_tlb_mm, (unsigned long) mm);
}

void smp_flush_cache_range(struct mm_struct *mm, unsigned long start,
			   unsigned long end)
{
	if(mm->context != NO_CONTEXT)
		xc3((smpfunc_t) local_flush_cache_range, (unsigned long) mm,
		    start, end);
}

void smp_flush_tlb_range(struct mm_struct *mm, unsigned long start,
			 unsigned long end)
{
	if(mm->context != NO_CONTEXT)
		xc3((smpfunc_t) local_flush_tlb_range, (unsigned long) mm,
		    start, end);
}

void smp_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{ xc2((smpfunc_t) local_flush_cache_page, (unsigned long) vma, page); }

void smp_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{ xc2((smpfunc_t) local_flush_tlb_page, (unsigned long) vma, page); }

void smp_flush_page_to_ram(unsigned long page)
{ xc1((smpfunc_t) local_flush_page_to_ram, page); }

void smp_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{ xc2((smpfunc_t) local_flush_sig_insns, (unsigned long) mm, insn_addr); }

/* Reschedule call back. */
void smp_reschedule_irq(void)
{
	need_resched=1;
}

/* Running cross calls. */
void smp_cross_call_irq(void)
{
	int i = smp_processor_id();

	ccall_info.processors_in[i] = 1;
	ccall_info.func(ccall_info.arg1, ccall_info.arg2, ccall_info.arg3,
			ccall_info.arg4, ccall_info.arg5);
	ccall_info.processors_out[i] = 1;
}

/* Stopping processors. */
void smp_stop_cpu_irq(void)
{
	__sti();
	while(1)
		barrier();
}
