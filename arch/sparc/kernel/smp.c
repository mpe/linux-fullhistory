/* smp.c: Sparc SMP support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/ptrace.h>

#include <linux/kernel.h>
#include <linux/tasks.h>
#include <linux/smp.h>
#include <linux/interrupt.h>

#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>

extern ctxd_t *srmmu_ctx_table_phys;
extern int linux_num_cpus;

struct tlog {
	unsigned long pc;
	unsigned long psr;
};

struct tlog trap_log[4][256];
unsigned long trap_log_ent[4] = { 0, 0, 0, 0, };

extern void calibrate_delay(void);

volatile unsigned long stuck_pc = 0;
volatile int smp_processors_ready = 0;

int smp_found_config = 0;
unsigned long cpu_present_map = 0;
int smp_num_cpus = 1;
int smp_threads_ready=0;
unsigned char mid_xlate[NR_CPUS] = { 0, 0, 0, 0, };
volatile unsigned long cpu_callin_map[NR_CPUS] = {0,};
volatile unsigned long smp_invalidate_needed[NR_CPUS] = { 0, };
volatile unsigned long smp_spinning[NR_CPUS] = { 0, };
struct cpuinfo_sparc cpu_data[NR_CPUS];
unsigned char boot_cpu_id = 0;
static int smp_activated = 0;
static volatile unsigned char smp_cpu_in_msg[NR_CPUS];
static volatile unsigned long smp_msg_data;
static volatile int smp_src_cpu;
static volatile int smp_msg_id;
volatile int cpu_number_map[NR_CPUS];
volatile int cpu_logical_map[NR_CPUS];

/* The only guaranteed locking primitive available on all Sparc
 * processors is 'ldstub [%reg + immediate], %dest_reg' which atomically
 * places the current byte at the effective address into dest_reg and
 * places 0xff there afterwards.  Pretty lame locking primitive
 * compared to the Alpha and the intel no?  Most Sparcs have 'swap'
 * instruction which is much better...
 */
klock_t kernel_flag = KLOCK_CLEAR;
volatile unsigned char active_kernel_processor = NO_PROC_ID;
volatile unsigned long kernel_counter = 0;
volatile unsigned long syscall_count = 0;
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

volatile unsigned long smp_proc_in_lock[NR_CPUS] = {0,};
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
(cpu_present_map & 1) ? ((active_kernel_processor == 0) ? "akp" : "online") : "offline",
(cpu_present_map & 2) ? ((active_kernel_processor == 1) ? "akp" : "online") : "offline",
(cpu_present_map & 4) ? ((active_kernel_processor == 2) ? "akp" : "online") : "offline",
(cpu_present_map & 8) ? ((active_kernel_processor == 3) ? "akp" : "online") : "offline");
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

	sti();
	local_flush_cache_all();
	local_flush_tlb_all();
	calibrate_delay();
	smp_store_cpu_info(cpuid);
	local_flush_cache_all();
	local_flush_tlb_all();
	cli();

	/* Allow master to continue. */
	swap((unsigned long *)&cpu_callin_map[cpuid], 1);
	local_flush_cache_all();
	local_flush_tlb_all();
	while(!smp_commenced)
		barrier();
	local_flush_cache_all();
	local_flush_tlb_all();

	/* Fix idle thread fields. */
	__asm__ __volatile__("ld [%0], %%g6\n\t"
			     : : "r" (&current_set[smp_processor_id()])
			     : "memory" /* paranoid */);
	current->mm->mmap->vm_page_prot = PAGE_SHARED;
	current->mm->mmap->vm_start = PAGE_OFFSET;
	current->mm->mmap->vm_end = init_task.mm->mmap->vm_end;

	local_flush_cache_all();
	local_flush_tlb_all();

	sti();
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

	printk("Entering SMP Mode...\n");

	penguin_ctable.which_io = 0;
	penguin_ctable.phys_addr = (char *) srmmu_ctx_table_phys;
	penguin_ctable.reg_size = 0;

	sti();
	cpu_present_map |= (1 << smp_processor_id());
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
	active_kernel_processor = boot_cpu_id;
	smp_store_cpu_info(boot_cpu_id);
	set_irq_udt(0);
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
	smp_processors_ready = 1;
}

static inline void send_ipi(unsigned long target_map, int irq)
{
	int i;

	for(i = 0; i < 4; i++) {
		if((1<<i) & target_map)
			set_cpu_int(mid_xlate[i], irq);
	}
}

/*
 * A non wait message cannot pass data or cpu source info. This current
 * setup is only safe because the kernel lock owner is the only person
 * who can send a message.
 *
 * Wrapping this whole block in a spinlock is not the safe answer either.
 * A processor may get stuck with irq's off waiting to send a message and
 * thus not replying to the person spinning for a reply....
 *
 * On the Sparc we use NMI's for all messages except reschedule.
 */

static volatile int message_cpu = NO_PROC_ID;

void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	unsigned long target_map;
	int p = smp_processor_id();
	int irq = 15;
	int i;

	/* Before processors have been placed into their initial
	 * patterns do not send messages.
	 */
	if(!smp_processors_ready)
		return;

	/* Skip the reschedule if we are waiting to clear a
	 * message at this time. The reschedule cannot wait
	 * but is not critical.
	 */
	if(msg == MSG_RESCHEDULE) {
		irq = 13;
		if(smp_cpu_in_msg[p])
			return;
	}

	/* Sanity check we don't re-enter this across CPU's. Only the kernel
	 * lock holder may send messages. For a STOP_CPU we are bringing the
	 * entire box to the fastest halt we can.. A reschedule carries
	 * no data and can occur during a flush.. guess what panic
	 * I got to notice this bug...
	 */
	if(message_cpu != NO_PROC_ID && msg != MSG_STOP_CPU && msg != MSG_RESCHEDULE) {
		printk("CPU #%d: Message pass %d but pass in progress by %d of %d\n",
		      smp_processor_id(),msg,message_cpu, smp_msg_id);

		/* I don't know how to gracefully die so that debugging
		 * this doesn't completely eat up my filesystems...
		 * let's try this...
		 */
		smp_cpu_in_msg[p] = 0; /* In case we come back here... */
		intr_count = 0;        /* and so panic don't barf... */
		smp_swap(&message_cpu, NO_PROC_ID); /* push the store buffer */
		sti();
		printk("spinning, please L1-A, type ctrace and send output to davem\n");
		while(1)
			barrier();
	}
	smp_swap(&message_cpu, smp_processor_id()); /* store buffers... */

	/* We are busy. */
	smp_cpu_in_msg[p]++;

	/* Reschedule is currently special. */
	if(msg != MSG_RESCHEDULE) {
		smp_src_cpu = p;
		smp_msg_id = msg;
		smp_msg_data = data;
	}

#if 0
	printk("SMP message pass from cpu %d to cpu %d msg %d\n", p, target, msg);
#endif

	/* Set the target requirement. */
	for(i = 0; i < smp_num_cpus; i++)
		swap((unsigned long *) &cpu_callin_map[i], 0);
	if(target == MSG_ALL_BUT_SELF) {
		target_map = (cpu_present_map & ~(1<<p));
		swap((unsigned long *) &cpu_callin_map[p], 1);
	} else if(target == MSG_ALL) {
		target_map = cpu_present_map;
	} else {
		for(i = 0; i < smp_num_cpus; i++)
			if(i != target)
				swap((unsigned long *) &cpu_callin_map[i], 1);
		target_map = (1<<target);
	}

	/* Fire it off. */
	send_ipi(target_map, irq);

	switch(wait) {
	case 1:
		for(i = 0; i < smp_num_cpus; i++)
			while(!cpu_callin_map[i])
				barrier();
		break;
	case 2:
		for(i = 0; i < smp_num_cpus; i++)
			while(smp_invalidate_needed[i])
				barrier();
		break;
	case 3:
		/* For cross calls we hold message_cpu and smp_cpu_in_msg[]
		 * until all processors disperse.  Else we have _big_ problems.
		 */
		return;
	}
	smp_cpu_in_msg[p]--;
	message_cpu = NO_PROC_ID;
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

/* #define DEBUG_CCALL */

/* Some nice day when we really thread the kernel I'd like to synchronize
 * this with either a broadcast conditional variable, a resource adaptive
 * generic mutex, or a convoy semaphore scheme of some sort.  No reason
 * we can't let multiple processors in here if the appropriate locking
 * is done.  Note that such a scheme assumes we will have a
 * prioritized ipi scheme using different software level irq's.
 */
void smp_cross_call(smpfunc_t func, unsigned long arg1, unsigned long arg2,
		    unsigned long arg3, unsigned long arg4, unsigned long arg5)
{
	unsigned long me = smp_processor_id();
	unsigned long flags;
	int i, timeout;

#ifdef DEBUG_CCALL
	printk("xc%d<", me);
#endif
	if(smp_processors_ready) {
		save_and_cli(flags);
		if(me != active_kernel_processor)
			goto cross_call_not_master;

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
		smp_message_pass(MSG_ALL_BUT_SELF, MSG_CROSS_CALL, 0, 3);

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
#ifdef DEBUG_CCALL
		printk("I");
#endif

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
#ifdef DEBUG_CCALL
		printk("O>");
#endif
		/* See wait case 3 in smp_message_pass()... */
		smp_cpu_in_msg[me]--;
		message_cpu = NO_PROC_ID;
		restore_flags(flags);
		return; /* made it... */

procs_time_out:
		printk("smp: Wheee, penguin drops off the bus\n");
		smp_cpu_in_msg[me]--;
		message_cpu = NO_PROC_ID;
		restore_flags(flags);
		return; /* why me... why me... */
	}

	/* Just need to run local copy. */
	func(arg1, arg2, arg3, arg4, arg5);
	return;

cross_call_not_master:
	printk("Cross call initiated by non master cpu\n");
	printk("akp=%x me=%08lx\n", active_kernel_processor, me);
	restore_flags(flags);
	panic("penguin cross call");
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
	if(smp_processor_id() != active_kernel_processor)
		panic("SMP Reschedule on CPU #%d, but #%d is active.\n",
		      smp_processor_id(), active_kernel_processor);

	need_resched=1;
}

/* XXX FIXME: this still doesn't work right... XXX */

/* #define DEBUG_CAPTURE */

static volatile unsigned long release = 1;
static volatile int capture_level = 0;

void smp_capture(void)
{
	unsigned long flags;

	if(!smp_activated || !smp_commenced)
		return;
#ifdef DEBUG_CAPTURE
	printk("C<%d>", smp_processor_id());
#endif
	save_and_cli(flags);
	if(!capture_level) {
		release = 0;
		smp_message_pass(MSG_ALL_BUT_SELF, MSG_CAPTURE, 0, 1);
	}
	capture_level++;
	restore_flags(flags);
}

void smp_release(void)
{
	unsigned long flags;
	int i;

	if(!smp_activated || !smp_commenced)
		return;
#ifdef DEBUG_CAPTURE
	printk("R<%d>", smp_processor_id());
#endif
	save_and_cli(flags);
	if(!(capture_level - 1)) {
		release = 1;
		for(i = 0; i < smp_num_cpus; i++)
			while(cpu_callin_map[i])
				barrier();
	}
	capture_level -= 1;
	restore_flags(flags);
}

/* Park a processor, we must watch for more IPI's to invalidate
 * our cache's and TLB's. And also note we can only wait for
 * "lock-less" IPI's and process those, as a result of such IPI's
 * being non-maskable traps being on is enough to receive them.
 */

/* Message call back. */
void smp_message_irq(void)
{
	int i=smp_processor_id();

	switch(smp_msg_id) {
	case MSG_CROSS_CALL:
		/* Do it to it. */
		ccall_info.processors_in[i] = 1;
		ccall_info.func(ccall_info.arg1, ccall_info.arg2, ccall_info.arg3,
				ccall_info.arg4, ccall_info.arg5);
		ccall_info.processors_out[i] = 1;
		break;

		/*
		 *	Halt other CPU's for a panic or reboot
		 */
	case MSG_STOP_CPU:
		sti();
		while(1)
			barrier();

	default:
		printk("CPU #%d sent invalid cross CPU message to CPU #%d: %X(%lX).\n",
		       smp_src_cpu,smp_processor_id(),smp_msg_id,smp_msg_data);
		break;
	}
}
