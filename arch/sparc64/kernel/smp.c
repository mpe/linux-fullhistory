/* smp.c: Sparc64 SMP support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

#include <asm/head.h>
#include <asm/ptrace.h>
#include <asm/atomic.h>

#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/uaccess.h>
#include <asm/timer.h>
#include <asm/starfire.h>

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

extern int linux_num_cpus;
extern void calibrate_delay(void);
extern unsigned prom_cpu_nodes[];

struct cpuinfo_sparc cpu_data[NR_CPUS]  __attribute__ ((aligned (64)));

volatile int __cpu_number_map[NR_CPUS]  __attribute__ ((aligned (64)));
volatile int __cpu_logical_map[NR_CPUS] __attribute__ ((aligned (64)));

/* Please don't make this stuff initdata!!!  --DaveM */
static unsigned char boot_cpu_id = 0;
static int smp_activated = 0;

/* Kernel spinlock */
spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;

volatile int smp_processors_ready = 0;
unsigned long cpu_present_map = 0;
int smp_num_cpus = 1;
int smp_threads_ready = 0;

void __init smp_setup(char *str, int *ints)
{
	/* XXX implement me XXX */
}

static int max_cpus = NR_CPUS;
static int __init maxcpus(char *str)
{
	get_option(&str, &max_cpus);
	return 1;
}

__setup("maxcpus=", maxcpus);

void smp_info(struct seq_file *m)
{
	int i;
	
	seq_printf(m, "State:\n");
	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_present_map & (1UL << i))
			seq_printf(m,
				   "CPU%d:\t\tonline\n", i);
	}
}

void smp_bogo(struct seq_file *m)
{
	int i;
	
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_present_map & (1UL << i))
			seq_printf(m,
				   "Cpu%dBogo\t: %lu.%02lu\n"
				   "Cpu%dClkTck\t: %016lx\n",
				   i, cpu_data[i].udelay_val / (500000/HZ),
				   (cpu_data[i].udelay_val / (5000/HZ)) % 100,
				   i, cpu_data[i].clock_tick);
}

void __init smp_store_cpu_info(int id)
{
	int i, no;

	/* multiplier and counter set by
	   smp_setup_percpu_timer()  */
	cpu_data[id].udelay_val			= loops_per_jiffy;

	for (no = 0; no < linux_num_cpus; no++)
		if (linux_cpus[no].mid == id)
			break;

	cpu_data[id].clock_tick = prom_getintdefault(linux_cpus[no].prom_node,
						     "clock-frequency", 0);

	cpu_data[id].pgcache_size		= 0;
	cpu_data[id].pte_cache[0]		= NULL;
	cpu_data[id].pte_cache[1]		= NULL;
	cpu_data[id].pgdcache_size		= 0;
	cpu_data[id].pgd_cache			= NULL;
	cpu_data[id].idle_volume		= 1;

	for (i = 0; i < 16; i++)
		cpu_data[id].irq_worklists[i] = 0;
}

void __init smp_commence(void)
{
}

static void smp_setup_percpu_timer(void);

static volatile unsigned long callin_flag = 0;

extern void inherit_locked_prom_mappings(int save_p);
extern void cpu_probe(void);

void __init smp_callin(void)
{
	int cpuid = hard_smp_processor_id();
	unsigned long pstate;

	inherit_locked_prom_mappings(0);

	__flush_cache_all();
	__flush_tlb_all();

	cpu_probe();

	/* Guarentee that the following sequences execute
	 * uninterrupted.
	 */
	__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));

	/* Set things up so user can access tick register for profiling
	 * purposes.  Also workaround BB_ERRATA_1 by doing a dummy
	 * read back of %tick after writing it.
	 */
	__asm__ __volatile__("
	sethi	%%hi(0x80000000), %%g1
	ba,pt	%%xcc, 1f
	 sllx	%%g1, 32, %%g1
	.align	64
1:	rd	%%tick, %%g2
	add	%%g2, 6, %%g2
	andn	%%g2, %%g1, %%g2
	wrpr	%%g2, 0, %%tick
	rdpr	%%tick, %%g0"
	: /* no outputs */
	: /* no inputs */
	: "g1", "g2");

	if (SPARC64_USE_STICK) {
		/* Let the user get at STICK too. */
		__asm__ __volatile__("
			sethi	%%hi(0x80000000), %%g1
			sllx	%%g1, 32, %%g1
			rd	%%asr24, %%g2
			andn	%%g2, %%g1, %%g2
			wr	%%g2, 0, %%asr24"
		: /* no outputs */
		: /* no inputs */
		: "g1", "g2");
	}

	/* Restore PSTATE_IE. */
	__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
			     : /* no outputs */
			     : "r" (pstate));

	smp_setup_percpu_timer();

	__sti();

	calibrate_delay();
	smp_store_cpu_info(cpuid);
	callin_flag = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "flush  %%g6" : : : "memory");

	/* Clear this or we will die instantly when we
	 * schedule back to this idler...
	 */
	current->thread.flags &= ~(SPARC_FLAG_NEWCHILD);

	/* Attach to the address space of init_task. */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	while (!smp_processors_ready)
		membar("#LoadLoad");
}

extern int cpu_idle(void);
extern void init_IRQ(void);

void initialize_secondary(void)
{
}

int start_secondary(void *unused)
{
	trap_init();
	init_IRQ();
	smp_callin();
	return cpu_idle();
}

void cpu_panic(void)
{
	printk("CPU[%d]: Returns from cpu_idle!\n", smp_processor_id());
	panic("SMP bolixed\n");
}

extern struct prom_cpuinfo linux_cpus[64];

extern unsigned long sparc64_cpu_startup;

/* The OBP cpu startup callback truncates the 3rd arg cookie to
 * 32-bits (I think) so to be safe we have it read the pointer
 * contained here so we work on >4GB machines. -DaveM
 */
static struct task_struct *cpu_new_task = NULL;

void __init smp_boot_cpus(void)
{
	int cpucount = 0, i;

	printk("Entering UltraSMPenguin Mode...\n");
	__sti();
	smp_store_cpu_info(boot_cpu_id);
	init_idle();

	if (linux_num_cpus == 1)
		return;

	for (i = 0; i < NR_CPUS; i++) {
		if (i == boot_cpu_id)
			continue;

		if ((cpucount + 1) == max_cpus)
			break;
		if (cpu_present_map & (1UL << i)) {
			unsigned long entry = (unsigned long)(&sparc64_cpu_startup);
			unsigned long cookie = (unsigned long)(&cpu_new_task);
			struct task_struct *p;
			int timeout;
			int no;

			prom_printf("Starting CPU %d... ", i);
			kernel_thread(start_secondary, NULL, CLONE_PID);
			cpucount++;

			p = init_task.prev_task;
			init_tasks[cpucount] = p;

			p->processor = i;
			p->cpus_runnable = 1 << i; /* we schedule the first task manually */

			del_from_runqueue(p);
			unhash_process(p);

			callin_flag = 0;
			for (no = 0; no < linux_num_cpus; no++)
				if (linux_cpus[no].mid == i)
					break;
			cpu_new_task = p;
			prom_startcpu(linux_cpus[no].prom_node,
				      entry, cookie);
			for (timeout = 0; timeout < 5000000; timeout++) {
				if (callin_flag)
					break;
				udelay(100);
			}
			if (callin_flag) {
				__cpu_number_map[i] = cpucount;
				__cpu_logical_map[cpucount] = i;
				prom_cpu_nodes[i] = linux_cpus[no].prom_node;
				prom_printf("OK\n");
			} else {
				cpucount--;
				printk("Processor %d is stuck.\n", i);
				prom_printf("FAILED\n");
			}
		}
		if (!callin_flag) {
			cpu_present_map &= ~(1UL << i);
			__cpu_number_map[i] = -1;
		}
	}
	cpu_new_task = NULL;
	if (cpucount == 0) {
		printk("Error: only one processor found.\n");
		cpu_present_map = (1UL << smp_processor_id());
	} else {
		unsigned long bogosum = 0;

		for (i = 0; i < NR_CPUS; i++) {
			if (cpu_present_map & (1UL << i))
				bogosum += cpu_data[i].udelay_val;
		}
		printk("Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
		       cpucount + 1,
		       bogosum/(500000/HZ),
		       (bogosum/(5000/HZ))%100);
		smp_activated = 1;
		smp_num_cpus = cpucount + 1;
	}
	smp_processors_ready = 1;
	membar("#StoreStore | #StoreLoad");
}

static void spitfire_xcall_helper(u64 data0, u64 data1, u64 data2, u64 pstate, unsigned long cpu)
{
	u64 result, target;
	int stuck, tmp;

	if (this_is_starfire) {
		/* map to real upaid */
		cpu = (((cpu & 0x3c) << 1) |
			((cpu & 0x40) >> 4) |
			(cpu & 0x3));
	}

	target = (cpu << 14) | 0x70;
again:
	/* Ok, this is the real Spitfire Errata #54.
	 * One must read back from a UDB internal register
	 * after writes to the UDB interrupt dispatch, but
	 * before the membar Sync for that write.
	 * So we use the high UDB control register (ASI 0x7f,
	 * ADDR 0x20) for the dummy read. -DaveM
	 */
	tmp = 0x40;
	__asm__ __volatile__("
	wrpr	%1, %2, %%pstate
	stxa	%4, [%0] %3
	stxa	%5, [%0+%8] %3
	add	%0, %8, %0
	stxa	%6, [%0+%8] %3
	membar	#Sync
	stxa	%%g0, [%7] %3
	membar	#Sync
	mov	0x20, %%g1
	ldxa	[%%g1] 0x7f, %%g0
	membar	#Sync"
	: "=r" (tmp)
	: "r" (pstate), "i" (PSTATE_IE), "i" (ASI_INTR_W),
	  "r" (data0), "r" (data1), "r" (data2), "r" (target), "r" (0x10), "0" (tmp)
       : "g1");

	/* NOTE: PSTATE_IE is still clear. */
	stuck = 100000;
	do {
		__asm__ __volatile__("ldxa [%%g0] %1, %0"
			: "=r" (result)
			: "i" (ASI_INTR_DISPATCH_STAT));
		if (result == 0) {
			__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
					     : : "r" (pstate));
			return;
		}
		stuck -= 1;
		if (stuck == 0)
			break;
	} while (result & 0x1);
	__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
			     : : "r" (pstate));
	if (stuck == 0) {
		printk("CPU[%d]: mondo stuckage result[%016lx]\n",
		       smp_processor_id(), result);
	} else {
		udelay(2);
		goto again;
	}
}

static __inline__ void spitfire_xcall_deliver(u64 data0, u64 data1, u64 data2, unsigned long mask)
{
	int ncpus = smp_num_cpus - 1;
	int i;
	u64 pstate;

	__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));
	for (i = 0; (i < NR_CPUS) && ncpus; i++) {
		if (mask & (1UL << i)) {
			spitfire_xcall_helper(data0, data1, data2, pstate, i);
			ncpus--;
		}
	}
}

/* Cheetah now allows to send the whole 64-bytes of data in the interrupt
 * packet, but we have no use for that.  However we do take advantage of
 * the new pipelining feature (ie. dispatch to multiple cpus simultaneously).
 */
#if NR_CPUS > 32
#error Fixup cheetah_xcall_deliver Dave...
#endif
static void cheetah_xcall_deliver(u64 data0, u64 data1, u64 data2, unsigned long mask)
{
	u64 pstate;
	int nack_busy_id;

	if (!mask)
		return;

	__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));

retry:
	__asm__ __volatile__("wrpr %0, %1, %%pstate\n\t"
			     : : "r" (pstate), "i" (PSTATE_IE));

	/* Setup the dispatch data registers. */
	__asm__ __volatile__("stxa	%0, [%3] %6\n\t"
			     "stxa	%1, [%4] %6\n\t"
			     "stxa	%2, [%5] %6\n\t"
			     "membar	#Sync\n\t"
			     : /* no outputs */
			     : "r" (data0), "r" (data1), "r" (data2),
			       "r" (0x40), "r" (0x50), "r" (0x60),
			       "i" (ASI_INTR_W));

	nack_busy_id = 0;
	{
		int i, ncpus = smp_num_cpus - 1;

		for (i = 0; (i < NR_CPUS) && ncpus; i++) {
			if (mask & (1UL << i)) {
				u64 target = (i << 14) | 0x70;

				target |= (nack_busy_id++ << 24);
				__asm__ __volatile__("stxa	%%g0, [%0] %1\n\t"
						     "membar	#Sync\n\t"
						     : /* no outputs */
						     : "r" (target), "i" (ASI_INTR_W));
				ncpus--;
			}
		}
	}

	/* Now, poll for completion. */
	{
		u64 dispatch_stat;
		long stuck;

		stuck = 100000 * nack_busy_id;
		do {
			__asm__ __volatile__("ldxa	[%%g0] %1, %0"
					     : "=r" (dispatch_stat)
					     : "i" (ASI_INTR_DISPATCH_STAT));
			if (dispatch_stat == 0UL) {
				__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
						     : : "r" (pstate));
				return;
			}
			if (!--stuck)
				break;
		} while (dispatch_stat & 0x5555555555555555UL);

		__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
				     : : "r" (pstate));

		if ((stuck & ~(0x5555555555555555UL)) == 0) {
			/* Busy bits will not clear, continue instead
			 * of freezing up on this cpu.
			 */
			printk("CPU[%d]: mondo stuckage result[%016lx]\n",
			       smp_processor_id(), dispatch_stat);
		} else {
			int i, this_busy_nack = 0;

			/* Delay some random time with interrupts enabled
			 * to prevent deadlock.
			 */
			udelay(2 * nack_busy_id);

			/* Clear out the mask bits for cpus which did not
			 * NACK us.
			 */
			for (i = 0; i < NR_CPUS; i++) {
				if (mask & (1UL << i)) {
					if ((dispatch_stat & (0x2 << this_busy_nack)) == 0)
						mask &= ~(1UL << i);
					this_busy_nack += 2;
				}
			}

			goto retry;
		}
	}
}

/* Send cross call to all processors mentioned in MASK
 * except self.
 */
static void smp_cross_call_masked(unsigned long *func, u32 ctx, u64 data1, u64 data2, unsigned long mask)
{
	if (smp_processors_ready) {
		u64 data0 = (((u64)ctx)<<32 | (((u64)func) & 0xffffffff));

		mask &= ~(1UL<<smp_processor_id());

		if (tlb_type == spitfire)
			spitfire_xcall_deliver(data0, data1, data2, mask);
		else
			cheetah_xcall_deliver(data0, data1, data2, mask);

		/* NOTE: Caller runs local copy on master. */
	}
}

/* Send cross call to all processors except self. */
#define smp_cross_call(func, ctx, data1, data2) \
	smp_cross_call_masked(func, ctx, data1, data2, cpu_present_map)

struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t finished;
	int wait;
};

extern unsigned long xcall_call_function;

int smp_call_function(void (*func)(void *info), void *info,
		      int nonatomic, int wait)
{
	struct call_data_struct data;
	int cpus = smp_num_cpus - 1;

	if (!cpus)
		return 0;

	data.func = func;
	data.info = info;
	atomic_set(&data.finished, 0);
	data.wait = wait;

	smp_cross_call(&xcall_call_function,
		       0, (u64) &data, 0);
	/* 
	 * Wait for other cpus to complete function or at
	 * least snap the call data.
	 */
	while (atomic_read(&data.finished) != cpus)
		barrier();

	return 0;
}

void smp_call_function_client(struct call_data_struct *call_data)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;

	if (call_data->wait) {
		/* let initiator proceed only after completion */
		func(info);
		atomic_inc(&call_data->finished);
	} else {
		/* let initiator proceed after getting data */
		atomic_inc(&call_data->finished);
		func(info);
	}
}

extern unsigned long xcall_flush_tlb_page;
extern unsigned long xcall_flush_tlb_mm;
extern unsigned long xcall_flush_tlb_range;
extern unsigned long xcall_flush_tlb_all;
extern unsigned long xcall_tlbcachesync;
extern unsigned long xcall_flush_cache_all;
extern unsigned long xcall_report_regs;
extern unsigned long xcall_receive_signal;
extern unsigned long xcall_flush_dcache_page_cheetah;
extern unsigned long xcall_flush_dcache_page_spitfire;

#ifdef DCFLUSH_DEBUG
extern atomic_t dcpage_flushes;
extern atomic_t dcpage_flushes_xcall;
#endif

static __inline__ void __local_flush_dcache_page(struct page *page)
{
#if (L1DCACHE_SIZE > PAGE_SIZE)
	__flush_dcache_page(page->virtual,
			    ((tlb_type == spitfire) &&
			     page->mapping != NULL));
#else
	if (page->mapping != NULL &&
	    tlb_type == spitfire)
		__flush_icache_page(__pa(page->virtual));
#endif
}

void smp_flush_dcache_page_impl(struct page *page, int cpu)
{
	if (smp_processors_ready) {
		unsigned long mask = 1UL << cpu;

#ifdef DCFLUSH_DEBUG
		atomic_inc(&dcpage_flushes);
#endif
		if (cpu == smp_processor_id()) {
			__local_flush_dcache_page(page);
		} else if ((cpu_present_map & mask) != 0) {
			u64 data0;

			if (tlb_type == spitfire) {
				data0 = ((u64)&xcall_flush_dcache_page_spitfire);
				if (page->mapping != NULL)
					data0 |= ((u64)1 << 32);
				spitfire_xcall_deliver(data0,
						       __pa(page->virtual),
						       (u64) page->virtual,
						       mask);
			} else {
				data0 = ((u64)&xcall_flush_dcache_page_cheetah);
				cheetah_xcall_deliver(data0,
						      __pa(page->virtual),
						      0, mask);
			}
#ifdef DCFLUSH_DEBUG
			atomic_inc(&dcpage_flushes_xcall);
#endif
		}
	}
}

void smp_receive_signal(int cpu)
{
	if (smp_processors_ready) {
		unsigned long mask = 1UL << cpu;

		if ((cpu_present_map & mask) != 0) {
			u64 data0 = (((u64)&xcall_receive_signal) & 0xffffffff);

			if (tlb_type == spitfire)
				spitfire_xcall_deliver(data0, 0, 0, mask);
			else
				cheetah_xcall_deliver(data0, 0, 0, mask);
		}
	}
}

void smp_report_regs(void)
{
	smp_cross_call(&xcall_report_regs, 0, 0, 0);
}

void smp_flush_cache_all(void)
{
	smp_cross_call(&xcall_flush_cache_all, 0, 0, 0);
	__flush_cache_all();
}

void smp_flush_tlb_all(void)
{
	smp_cross_call(&xcall_flush_tlb_all, 0, 0, 0);
	__flush_tlb_all();
}

/* We know that the window frames of the user have been flushed
 * to the stack before we get here because all callers of us
 * are flush_tlb_*() routines, and these run after flush_cache_*()
 * which performs the flushw.
 *
 * The SMP TLB coherency scheme we use works as follows:
 *
 * 1) mm->cpu_vm_mask is a bit mask of which cpus an address
 *    space has (potentially) executed on, this is the heuristic
 *    we use to avoid doing cross calls.
 *
 *    Also, for flushing from kswapd and also for clones, we
 *    use cpu_vm_mask as the list of cpus to make run the TLB.
 *
 * 2) TLB context numbers are shared globally across all processors
 *    in the system, this allows us to play several games to avoid
 *    cross calls.
 *
 *    One invariant is that when a cpu switches to a process, and
 *    that processes tsk->active_mm->cpu_vm_mask does not have the
 *    current cpu's bit set, that tlb context is flushed locally.
 *
 *    If the address space is non-shared (ie. mm->count == 1) we avoid
 *    cross calls when we want to flush the currently running process's
 *    tlb state.  This is done by clearing all cpu bits except the current
 *    processor's in current->active_mm->cpu_vm_mask and performing the
 *    flush locally only.  This will force any subsequent cpus which run
 *    this task to flush the context from the local tlb if the process
 *    migrates to another cpu (again).
 *
 * 3) For shared address spaces (threads) and swapping we bite the
 *    bullet for most cases and perform the cross call (but only to
 *    the cpus listed in cpu_vm_mask).
 *
 *    The performance gain from "optimizing" away the cross call for threads is
 *    questionable (in theory the big win for threads is the massive sharing of
 *    address space state across processors).
 */
void smp_flush_tlb_mm(struct mm_struct *mm)
{
        /*
         * This code is called from two places, dup_mmap and exit_mmap. In the
         * former case, we really need a flush. In the later case, the callers
         * are single threaded exec_mmap (really need a flush), multithreaded
         * exec_mmap case (do not need to flush, since the caller gets a new
         * context via activate_mm), and all other callers of mmput() whence
         * the flush can be optimized since the associated threads are dead and
         * the mm is being torn down (__exit_mm and other mmput callers) or the
         * owning thread is dissociating itself from the mm. The
         * (atomic_read(&mm->mm_users) == 0) check ensures real work is done
         * for single thread exec and dup_mmap cases. An alternate check might
         * have been (current->mm != mm).
         *                                              Kanoj Sarcar
         */
        if (atomic_read(&mm->mm_users) == 0)
                return;

	{
		u32 ctx = CTX_HWBITS(mm->context);
		int cpu = smp_processor_id();

		if (atomic_read(&mm->mm_users) == 1) {
			/* See smp_flush_tlb_page for info about this. */
			mm->cpu_vm_mask = (1UL << cpu);
			goto local_flush_and_out;
		}

		smp_cross_call_masked(&xcall_flush_tlb_mm,
				      ctx, 0, 0,
				      mm->cpu_vm_mask);

	local_flush_and_out:
		__flush_tlb_mm(ctx, SECONDARY_CONTEXT);
	}
}

void smp_flush_tlb_range(struct mm_struct *mm, unsigned long start,
			 unsigned long end)
{
	{
		u32 ctx = CTX_HWBITS(mm->context);
		int cpu = smp_processor_id();

		start &= PAGE_MASK;
		end    = PAGE_ALIGN(end);

		if (mm == current->active_mm && atomic_read(&mm->mm_users) == 1) {
			mm->cpu_vm_mask = (1UL << cpu);
			goto local_flush_and_out;
		}

		smp_cross_call_masked(&xcall_flush_tlb_range,
				      ctx, start, end,
				      mm->cpu_vm_mask);

	local_flush_and_out:
		__flush_tlb_range(ctx, start, SECONDARY_CONTEXT, end, PAGE_SIZE, (end-start));
	}
}

void smp_flush_tlb_page(struct mm_struct *mm, unsigned long page)
{
	{
		u32 ctx = CTX_HWBITS(mm->context);
		int cpu = smp_processor_id();

		page &= PAGE_MASK;
		if (mm == current->active_mm && atomic_read(&mm->mm_users) == 1) {
			/* By virtue of being the current address space, and
			 * having the only reference to it, the following operation
			 * is safe.
			 *
			 * It would not be a win to perform the xcall tlb flush in
			 * this case, because even if we switch back to one of the
			 * other processors in cpu_vm_mask it is almost certain that
			 * all TLB entries for this context will be replaced by the
			 * time that happens.
			 */
			mm->cpu_vm_mask = (1UL << cpu);
			goto local_flush_and_out;
		} else {
			/* By virtue of running under the mm->page_table_lock,
			 * and mmu_context.h:switch_mm doing the same, the following
			 * operation is safe.
			 */
			if (mm->cpu_vm_mask == (1UL << cpu))
				goto local_flush_and_out;
		}

		/* OK, we have to actually perform the cross call.  Most likely
		 * this is a cloned mm or kswapd is kicking out pages for a task
		 * which has run recently on another cpu.
		 */
		smp_cross_call_masked(&xcall_flush_tlb_page,
				      ctx, page, 0,
				      mm->cpu_vm_mask);
		if (!(mm->cpu_vm_mask & (1UL << cpu)))
			return;

	local_flush_and_out:
		__flush_tlb_page(ctx, page, SECONDARY_CONTEXT);
	}
}

/* CPU capture. */
/* #define CAPTURE_DEBUG */
extern unsigned long xcall_capture;

static atomic_t smp_capture_depth = ATOMIC_INIT(0);
static atomic_t smp_capture_registry = ATOMIC_INIT(0);
static unsigned long penguins_are_doing_time = 0;

void smp_capture(void)
{
	if (smp_processors_ready) {
		int result = __atomic_add(1, &smp_capture_depth);

		membar("#StoreStore | #LoadStore");
		if (result == 1) {
			int ncpus = smp_num_cpus;

#ifdef CAPTURE_DEBUG
			printk("CPU[%d]: Sending penguins to jail...",
			       smp_processor_id());
#endif
			penguins_are_doing_time = 1;
			membar("#StoreStore | #LoadStore");
			atomic_inc(&smp_capture_registry);
			smp_cross_call(&xcall_capture, 0, 0, 0);
			while (atomic_read(&smp_capture_registry) != ncpus)
				membar("#LoadLoad");
#ifdef CAPTURE_DEBUG
			printk("done\n");
#endif
		}
	}
}

void smp_release(void)
{
	if (smp_processors_ready) {
		if (atomic_dec_and_test(&smp_capture_depth)) {
#ifdef CAPTURE_DEBUG
			printk("CPU[%d]: Giving pardon to imprisoned penguins\n",
			       smp_processor_id());
#endif
			penguins_are_doing_time = 0;
			membar("#StoreStore | #StoreLoad");
			atomic_dec(&smp_capture_registry);
		}
	}
}

/* Imprisoned penguins run with %pil == 15, but PSTATE_IE set, so they
 * can service tlb flush xcalls...
 */
extern void prom_world(int);
extern void save_alternate_globals(unsigned long *);
extern void restore_alternate_globals(unsigned long *);
void smp_penguin_jailcell(void)
{
	unsigned long global_save[24];

	__asm__ __volatile__("flushw");
	save_alternate_globals(global_save);
	prom_world(1);
	atomic_inc(&smp_capture_registry);
	membar("#StoreLoad | #StoreStore");
	while (penguins_are_doing_time)
		membar("#LoadLoad");
	restore_alternate_globals(global_save);
	atomic_dec(&smp_capture_registry);
	prom_world(0);
}

extern unsigned long xcall_promstop;

void smp_promstop_others(void)
{
	if (smp_processors_ready)
		smp_cross_call(&xcall_promstop, 0, 0, 0);
}

extern void sparc64_do_profile(unsigned long pc, unsigned long o7);

static unsigned long current_tick_offset;

#define prof_multiplier(__cpu)		cpu_data[(__cpu)].multiplier
#define prof_counter(__cpu)		cpu_data[(__cpu)].counter

void smp_percpu_timer_interrupt(struct pt_regs *regs)
{
	unsigned long compare, tick, pstate;
	int cpu = smp_processor_id();
	int user = user_mode(regs);

	/*
	 * Check for level 14 softint.
	 */
	{
		unsigned long tick_mask;

		if (SPARC64_USE_STICK)
			tick_mask = (1UL << 16);
		else
			tick_mask = (1UL << 0);

		if (!(get_softint() & tick_mask)) {
			extern void handler_irq(int, struct pt_regs *);

			handler_irq(14, regs);
			return;
		}
		clear_softint(tick_mask);
	}

	do {
		if (!user)
			sparc64_do_profile(regs->tpc, regs->u_regs[UREG_RETPC]);
		if (!--prof_counter(cpu)) {
			if (cpu == boot_cpu_id) {
				irq_enter(cpu, 0);

				kstat.irqs[cpu][0]++;
				timer_tick_interrupt(regs);

				irq_exit(cpu, 0);
			}

			update_process_times(user);

			prof_counter(cpu) = prof_multiplier(cpu);
		}

		/* Guarentee that the following sequences execute
		 * uninterrupted.
		 */
		__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
				     "wrpr	%0, %1, %%pstate"
				     : "=r" (pstate)
				     : "i" (PSTATE_IE));

		/* Workaround for Spitfire Errata (#54 I think??), I discovered
		 * this via Sun BugID 4008234, mentioned in Solaris-2.5.1 patch
		 * number 103640.
		 *
		 * On Blackbird writes to %tick_cmpr can fail, the
		 * workaround seems to be to execute the wr instruction
		 * at the start of an I-cache line, and perform a dummy
		 * read back from %tick_cmpr right after writing to it. -DaveM
		 *
		 * Just to be anal we add a workaround for Spitfire
		 * Errata 50 by preventing pipeline bypasses on the
		 * final read of the %tick register into a compare
		 * instruction.  The Errata 50 description states
		 * that %tick is not prone to this bug, but I am not
		 * taking any chances.
		 */
		if (!SPARC64_USE_STICK) {
		__asm__ __volatile__("rd	%%tick_cmpr, %0\n\t"
				     "ba,pt	%%xcc, 1f\n\t"
				     " add	%0, %2, %0\n\t"
				     ".align	64\n"
				  "1: wr	%0, 0x0, %%tick_cmpr\n\t"
				     "rd	%%tick_cmpr, %%g0\n\t"
				     "rd	%%tick, %1\n\t"
				     "mov	%1, %1"
				     : "=&r" (compare), "=r" (tick)
				     : "r" (current_tick_offset));
		} else {
		__asm__ __volatile__("rd	%%asr25, %0\n\t"
				     "add	%0, %2, %0\n\t"
				     "wr	%0, 0x0, %%asr25\n\t"
				     "rd	%%asr24, %1\n\t"
				     : "=&r" (compare), "=r" (tick)
				     : "r" (current_tick_offset));
		}

		/* Restore PSTATE_IE. */
		__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
				     : /* no outputs */
				     : "r" (pstate));
	} while (tick >= compare);
}

static void __init smp_setup_percpu_timer(void)
{
	int cpu = smp_processor_id();
	unsigned long pstate;

	prof_counter(cpu) = prof_multiplier(cpu) = 1;

	/* Guarentee that the following sequences execute
	 * uninterrupted.
	 */
	__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));

	/* Workaround for Spitfire Errata (#54 I think??), I discovered
	 * this via Sun BugID 4008234, mentioned in Solaris-2.5.1 patch
	 * number 103640.
	 *
	 * On Blackbird writes to %tick_cmpr can fail, the
	 * workaround seems to be to execute the wr instruction
	 * at the start of an I-cache line, and perform a dummy
	 * read back from %tick_cmpr right after writing to it. -DaveM
	 */
	if (!SPARC64_USE_STICK) {
	__asm__ __volatile__("
		rd	%%tick, %%g1
		ba,pt	%%xcc, 1f
		 add	%%g1, %0, %%g1
		.align	64
	1:	wr	%%g1, 0x0, %%tick_cmpr
		rd	%%tick_cmpr, %%g0"
	: /* no outputs */
	: "r" (current_tick_offset)
	: "g1");
	} else {
	__asm__ __volatile__("
		rd	%%asr24, %%g1
		add	%%g1, %0, %%g1
		wr	%%g1, 0x0, %%asr25"
	: /* no outputs */
	: "r" (current_tick_offset)
	: "g1");
	}

	/* Restore PSTATE_IE. */
	__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
			     : /* no outputs */
			     : "r" (pstate));
}

void __init smp_tick_init(void)
{
	int i;
	
	boot_cpu_id = hard_smp_processor_id();
	current_tick_offset = timer_tick_offset;
	cpu_present_map = 0;
	for (i = 0; i < linux_num_cpus; i++)
		cpu_present_map |= (1UL << linux_cpus[i].mid);
	for (i = 0; i < NR_CPUS; i++) {
		__cpu_number_map[i] = -1;
		__cpu_logical_map[i] = -1;
	}
	__cpu_number_map[boot_cpu_id] = 0;
	prom_cpu_nodes[boot_cpu_id] = linux_cpus[0].prom_node;
	__cpu_logical_map[0] = boot_cpu_id;
	current->processor = boot_cpu_id;
	prof_counter(boot_cpu_id) = prof_multiplier(boot_cpu_id) = 1;
}

static inline unsigned long find_flush_base(unsigned long size)
{
	struct page *p = mem_map;
	unsigned long found, base;

	size = PAGE_ALIGN(size);
	found = size;
	base = (unsigned long) page_address(p);
	while (found != 0) {
		/* Failure. */
		if (p >= (mem_map + max_mapnr))
			return 0UL;
		if (PageReserved(p)) {
			found = size;
			base = (unsigned long) page_address(p);
		} else {
			found -= PAGE_SIZE;
		}
		p++;
	}
	return base;
}

/* /proc/profile writes can call this, don't __init it please. */
int setup_profiling_timer(unsigned int multiplier)
{
	unsigned long flags;
	int i;

	if ((!multiplier) || (timer_tick_offset / multiplier) < 1000)
		return -EINVAL;

	save_and_cli(flags);
	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_present_map & (1UL << i))
			prof_multiplier(i) = multiplier;
	}
	current_tick_offset = (timer_tick_offset / multiplier);
	restore_flags(flags);

	return 0;
}
