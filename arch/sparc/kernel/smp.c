/* smp.c: Sparc SMP support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tasks.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>

#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/atops.h>
#include <asm/spinlock.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#define IRQ_RESCHEDULE		13
#define IRQ_STOP_CPU		14
#define IRQ_CROSS_CALL		15

extern ctxd_t *srmmu_ctx_table_phys;
extern int linux_num_cpus;

extern void calibrate_delay(void);

/* XXX Let's get rid of this thing if we can... */
extern struct task_struct *current_set[NR_CPUS];

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

volatile int smp_process_available=0;

/*#define SMP_DEBUG*/

#ifdef SMP_DEBUG
#define SMP_PRINTK(x)	printk x
#else
#define SMP_PRINTK(x)
#endif

volatile int smp_commenced = 0;

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

static void smp_setup_percpu_timer(void);

void smp_callin(void)
{
	int cpuid = hard_smp_processor_id();

	local_flush_cache_all();
	local_flush_tlb_all();
	set_irq_udt(mid_xlate[boot_cpu_id]);

	/* Get our local ticker going. */
	smp_setup_percpu_timer();

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

extern int cpu_idle(void *unused);
extern void init_IRQ(void);

/* Only broken Intel needs this, thus it should not even be referenced
 * globally...
 */
void initialize_secondary(void)
{
}

/* Activate a secondary processor. */
int start_secondary(void *unused)
{
	trap_init();
	init_IRQ();
	smp_callin();
	return cpu_idle(NULL);
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
	current->processor = boot_cpu_id;
	smp_store_cpu_info(boot_cpu_id);
	set_irq_udt(mid_xlate[boot_cpu_id]);
	smp_setup_percpu_timer();
	local_flush_cache_all();
	if(linux_num_cpus == 1)
		return;  /* Not an MP box. */
	for(i = 0; i < NR_CPUS; i++) {
		if(i == boot_cpu_id)
			continue;

		if(cpu_present_map & (1 << i)) {
			extern unsigned long sparc_cpu_startup;
			unsigned long *entry = &sparc_cpu_startup;
			struct task_struct *p;
			int timeout;

			/* Cook up an idler for this guy. */
			kernel_thread(start_secondary, NULL, CLONE_PID);

			p = task[++cpucount];

			p->processor = i;
			current_set[i] = p;

			/* See trampoline.S for details... */
			entry += ((i-1) * 3);

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
				cpu_number_map[i] = i;
				cpu_logical_map[i] = i;
			} else {
				cpucount--;
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

static spinlock_t cross_call_lock = SPIN_LOCK_UNLOCKED;

/* Cross calls must be serialized, at least currently. */
void smp_cross_call(smpfunc_t func, unsigned long arg1, unsigned long arg2,
		    unsigned long arg3, unsigned long arg4, unsigned long arg5)
{
	if(smp_processors_ready) {
		register int ncpus = smp_num_cpus;
		unsigned long flags;

		spin_lock_irqsave(&cross_call_lock, flags);

		/* Init function glue. */
		ccall_info.func = func;
		ccall_info.arg1 = arg1;
		ccall_info.arg2 = arg2;
		ccall_info.arg3 = arg3;
		ccall_info.arg4 = arg4;
		ccall_info.arg5 = arg5;

		/* Init receive/complete mapping, plus fire the IPI's off. */
		{
			register void (*send_ipi)(int,int) = set_cpu_int;
			register unsigned long mask;
			register int i;

			mask = (cpu_present_map & ~(1 << smp_processor_id()));
			for(i = 0; i < ncpus; i++) {
				if(mask & (1 << i)) {
					ccall_info.processors_in[i] = 0;
					ccall_info.processors_out[i] = 0;
					send_ipi(mid_xlate[i], IRQ_CROSS_CALL);
				} else {
					ccall_info.processors_in[i] = 1;
					ccall_info.processors_out[i] = 1;
				}
			}
		}

		/* First, run local copy. */
		func(arg1, arg2, arg3, arg4, arg5);

		{
			register int i;

			i = 0;
			do {
				while(!ccall_info.processors_in[i])
					barrier();
			} while(++i < ncpus);

			i = 0;
			do {
				while(!ccall_info.processors_out[i])
					barrier();
			} while(++i < ncpus);
		}

		spin_unlock_irqrestore(&cross_call_lock, flags);
	} else
		func(arg1, arg2, arg3, arg4, arg5); /* Just need to run local copy. */
}

void smp_flush_cache_all(void)
{ xc0((smpfunc_t) local_flush_cache_all); }

void smp_flush_tlb_all(void)
{ xc0((smpfunc_t) local_flush_tlb_all); }

void smp_flush_cache_mm(struct mm_struct *mm)
{ 
	if(mm->context != NO_CONTEXT) {
		if(mm->cpu_vm_mask == (1 << smp_processor_id()))
			local_flush_cache_mm(mm);
		else
			xc1((smpfunc_t) local_flush_cache_mm, (unsigned long) mm);
	}
}

void smp_flush_tlb_mm(struct mm_struct *mm)
{
	if(mm->context != NO_CONTEXT) {
		if(mm->cpu_vm_mask == (1 << smp_processor_id())) {
			local_flush_tlb_mm(mm);
		} else {
			xc1((smpfunc_t) local_flush_tlb_mm, (unsigned long) mm);
			if(mm->count == 1 && current->mm == mm)
				mm->cpu_vm_mask = (1 << smp_processor_id());
		}
	}
}

void smp_flush_cache_range(struct mm_struct *mm, unsigned long start,
			   unsigned long end)
{
	if(mm->context != NO_CONTEXT) {
		if(mm->cpu_vm_mask == (1 << smp_processor_id()))
			local_flush_cache_range(mm, start, end);
		else
			xc3((smpfunc_t) local_flush_cache_range, (unsigned long) mm,
			    start, end);
	}
}

void smp_flush_tlb_range(struct mm_struct *mm, unsigned long start,
			 unsigned long end)
{
	if(mm->context != NO_CONTEXT) {
		if(mm->cpu_vm_mask == (1 << smp_processor_id()))
			local_flush_tlb_range(mm, start, end);
		else
			xc3((smpfunc_t) local_flush_tlb_range, (unsigned long) mm,
			    start, end);
	}
}

void smp_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if(mm->context != NO_CONTEXT) {
		if(mm->cpu_vm_mask == (1 << smp_processor_id()))
			local_flush_cache_page(vma, page);
		else
			xc2((smpfunc_t) local_flush_cache_page,
			    (unsigned long) vma, page);
	}
}

void smp_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if(mm->context != NO_CONTEXT) {
		if(mm->cpu_vm_mask == (1 << smp_processor_id()))
			local_flush_tlb_page(vma, page);
		else
			xc2((smpfunc_t) local_flush_tlb_page, (unsigned long) vma, page);
	}
}

void smp_flush_page_to_ram(unsigned long page)
{
	/* Current theory is that those who call this are the one's
	 * who have just dirtied their cache with the pages contents
	 * in kernel space, therefore we only run this on local cpu.
	 *
	 * XXX This experiment failed, research further... -DaveM
	 */
#if 1
	xc1((smpfunc_t) local_flush_page_to_ram, page);
#else
	local_flush_page_to_ram(page);
#endif
}

void smp_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
	if(mm->cpu_vm_mask == (1 << smp_processor_id()))
		local_flush_sig_insns(mm, insn_addr);
	else
		xc2((smpfunc_t) local_flush_sig_insns, (unsigned long) mm, insn_addr);
}

/* Reschedule call back. */
void smp_reschedule_irq(void)
{
	need_resched = 1;
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

/* Protects counters touched during level14 ticker */
spinlock_t ticker_lock = SPIN_LOCK_UNLOCKED;

#ifdef CONFIG_PROFILE

/* 32-bit Sparc specific profiling function. */
static inline void sparc_do_profile(unsigned long pc)
{
	if(prof_buffer && current->pid) {
		extern int _stext;

		pc -= (unsigned long) &_stext;
		pc >>= prof_shift;

		spin_lock(&ticker_lock);
		if(pc < prof_len)
			prof_buffer[pc]++;
		else
			prof_buffer[prof_len - 1]++;
		spin_unlock(&ticker_lock);
	}
}

#endif

unsigned int prof_multiplier[NR_CPUS];
unsigned int prof_counter[NR_CPUS];

extern void update_one_process(struct task_struct *p, unsigned long ticks,
			       unsigned long user, unsigned long system);

void smp_percpu_timer_interrupt(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	clear_profile_irq(mid_xlate[cpu]);
#ifdef CONFIG_PROFILE
	if(!user_mode(regs))
		sparc_do_profile(regs->pc);
#endif
	if(!--prof_counter[cpu]) {
		int user = user_mode(regs);
		if(current->pid) {
			update_one_process(current, 1, user, !user);

			if(--current->counter < 0) {
				current->counter = 0;
				need_resched = 1;
			}

			spin_lock(&ticker_lock);
			if(user) {
				if(current->priority < DEF_PRIORITY)
					kstat.cpu_nice++;
				else
					kstat.cpu_user++;
			} else {
				kstat.cpu_system++;
			}
			spin_unlock(&ticker_lock);
		}
		prof_counter[cpu] = prof_multiplier[cpu];
	}
}

extern unsigned int lvl14_resolution;

static void smp_setup_percpu_timer(void)
{
	int cpu = smp_processor_id();

	prof_counter[cpu] = prof_multiplier[cpu] = 1;
	load_profile_irq(mid_xlate[cpu], lvl14_resolution);

	if(cpu == boot_cpu_id)
		enable_pil_irq(14);
}

int setup_profiling_timer(unsigned int multiplier)
{
	int i;
	unsigned long flags;

	/* Prevent level14 ticker IRQ flooding. */
	if((!multiplier) || (lvl14_resolution / multiplier) < 500)
		return -EINVAL;

	save_and_cli(flags);
	for(i = 0; i < NR_CPUS; i++) {
		if(cpu_present_map & (1 << i)) {
			load_profile_irq(mid_xlate[i], lvl14_resolution / multiplier);
			prof_multiplier[i] = multiplier;
		}
	}
	restore_flags(flags);

	return 0;
}
