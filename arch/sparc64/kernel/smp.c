/* smp.c: Sparc64 SMP support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/tasks.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/head.h>
#include <asm/ptrace.h>
#include <asm/atomic.h>

#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/spinlock.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/uaccess.h>
#include <asm/timer.h>

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

extern int linux_num_cpus;
extern void calibrate_delay(void);
extern unsigned prom_cpu_nodes[];

struct cpuinfo_sparc cpu_data[NR_CPUS]  __attribute__ ((aligned (64)));

volatile int cpu_number_map[NR_CPUS]    __attribute__ ((aligned (64)));
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

__initfunc(void smp_setup(char *str, int *ints))
{
	/* XXX implement me XXX */
}

int smp_info(char *buf)
{
	int len = 7, i;
	
	strcpy(buf, "State:\n");
	for (i = 0; i < NR_CPUS; i++)
		if(cpu_present_map & (1UL << i))
			len += sprintf(buf + len,
					"CPU%d:\t\tonline\n", i);
	return len;
}

int smp_bogo(char *buf)
{
	int len = 0, i;
	
	for (i = 0; i < NR_CPUS; i++)
		if(cpu_present_map & (1UL << i))
			len += sprintf(buf + len,
				       "Cpu%dBogo\t: %lu.%02lu\n",
				       i, cpu_data[i].udelay_val / 500000,
				       (cpu_data[i].udelay_val / 5000) % 100);
	return len;
}

__initfunc(void smp_store_cpu_info(int id))
{
	int i;

	cpu_data[id].irq_count			= 0;
	cpu_data[id].bh_count			= 0;
	/* multiplier and counter set by
	   smp_setup_percpu_timer()  */
	cpu_data[id].udelay_val			= loops_per_sec;

	cpu_data[id].pgcache_size		= 0;
	cpu_data[id].pte_cache			= NULL;
	cpu_data[id].pgdcache_size		= 0;
	cpu_data[id].pgd_cache			= NULL;
	cpu_data[id].idle_volume		= 0;

	for(i = 0; i < 16; i++)
		cpu_data[id].irq_worklists[i] = 0;
}

__initfunc(void smp_commence(void))
{
}

static void smp_setup_percpu_timer(void);
static void smp_tune_scheduling(void);

static volatile unsigned long callin_flag = 0;

extern void inherit_locked_prom_mappings(int save_p);
extern void cpu_probe(void);

__initfunc(void smp_callin(void))
{
	int cpuid = hard_smp_processor_id();

	inherit_locked_prom_mappings(0);

	__flush_cache_all();
	__flush_tlb_all();

	cpu_probe();

	/* Master did this already, now is the time for us to do it. */
	__asm__ __volatile__("
	sethi	%%hi(0x80000000), %%g1
	sllx	%%g1, 32, %%g1
	rd	%%tick, %%g2
	add	%%g2, 6, %%g2
	andn	%%g2, %%g1, %%g2
	wrpr	%%g2, 0, %%tick
"	: /* no outputs */
	: /* no inputs */
	: "g1", "g2");

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
	current->tss.flags &= ~(SPARC_FLAG_NEWCHILD);

	while(!smp_processors_ready)
		membar("#LoadLoad");
}

extern int cpu_idle(void *unused);
extern void init_IRQ(void);

void initialize_secondary(void)
{
}

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

extern struct prom_cpuinfo linux_cpus[64];

extern unsigned long smp_trampoline;

/* The OBP cpu startup callback truncates the 3rd arg cookie to
 * 32-bits (I think) so to be safe we have it read the pointer
 * contained here so we work on >4GB machines. -DaveM
 */
static struct task_struct *cpu_new_task = NULL;

__initfunc(void smp_boot_cpus(void))
{
	int cpucount = 0, i;

	printk("Entering UltraSMPenguin Mode...\n");
	__sti();
	smp_store_cpu_info(boot_cpu_id);
	smp_tune_scheduling();

	if(linux_num_cpus == 1)
		return;

	for(i = 0; i < NR_CPUS; i++) {
		if(i == boot_cpu_id)
			continue;

		if(cpu_present_map & (1UL << i)) {
			unsigned long entry = (unsigned long)(&smp_trampoline);
			unsigned long cookie = (unsigned long)(&cpu_new_task);
			struct task_struct *p;
			int timeout;
			int no;
			extern unsigned long phys_base;

			entry += phys_base - KERNBASE;
			cookie += phys_base - KERNBASE;
			kernel_thread(start_secondary, NULL, CLONE_PID);
			p = task[++cpucount];
			p->processor = i;
			callin_flag = 0;
			for (no = 0; no < linux_num_cpus; no++)
				if (linux_cpus[no].mid == i)
					break;
			cpu_new_task = p;
			prom_startcpu(linux_cpus[no].prom_node,
				      entry, cookie);
			for(timeout = 0; timeout < 5000000; timeout++) {
				if(callin_flag)
					break;
				udelay(100);
			}
			if(callin_flag) {
				cpu_number_map[i] = cpucount;
				__cpu_logical_map[cpucount] = i;
				prom_cpu_nodes[i] = linux_cpus[no].prom_node;
			} else {
				cpucount--;
				printk("Processor %d is stuck.\n", i);
			}
		}
		if(!callin_flag) {
			cpu_present_map &= ~(1UL << i);
			cpu_number_map[i] = -1;
		}
	}
	cpu_new_task = NULL;
	if(cpucount == 0) {
		printk("Error: only one processor found.\n");
		cpu_present_map = (1UL << smp_processor_id());
	} else {
		unsigned long bogosum = 0;

		for(i = 0; i < NR_CPUS; i++) {
			if(cpu_present_map & (1UL << i))
				bogosum += cpu_data[i].udelay_val;
		}
		printk("Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
		       cpucount + 1,
		       (bogosum + 2500)/500000,
		       ((bogosum + 2500)/5000)%100);
		smp_activated = 1;
		smp_num_cpus = cpucount + 1;
	}
	smp_processors_ready = 1;
	membar("#StoreStore | #StoreLoad");
}

/* #define XCALL_DEBUG */

static inline void xcall_deliver(u64 data0, u64 data1, u64 data2, u64 pstate, unsigned long cpu)
{
	u64 result, target = (cpu << 14) | 0x70;
	int stuck;

#ifdef XCALL_DEBUG
	printk("CPU[%d]: xcall(data[%016lx:%016lx:%016lx],tgt[%016lx])\n",
	       smp_processor_id(), data0, data1, data2, target);
#endif
again:
	__asm__ __volatile__("
	wrpr	%0, %1, %%pstate
	wr	%%g0, %2, %%asi
	stxa	%3, [0x40] %%asi
	stxa	%4, [0x50] %%asi
	stxa	%5, [0x60] %%asi
	membar	#Sync
	stxa	%%g0, [%6] %%asi
	membar	#Sync"
	: /* No outputs */
	: "r" (pstate), "i" (PSTATE_IE), "i" (ASI_UDB_INTR_W),
	  "r" (data0), "r" (data1), "r" (data2), "r" (target));

	/* NOTE: PSTATE_IE is still clear. */
	stuck = 100000;
	do {
		__asm__ __volatile__("ldxa [%%g0] %1, %0"
			: "=r" (result)
			: "i" (ASI_INTR_DISPATCH_STAT));
		if(result == 0) {
			__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
					     : : "r" (pstate));
			return;
		}
		stuck -= 1;
		if(stuck == 0)
			break;
	} while(result & 0x1);
	__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
			     : : "r" (pstate));
	if(stuck == 0) {
#ifdef XCALL_DEBUG
		printk("CPU[%d]: mondo stuckage result[%016lx]\n",
		       smp_processor_id(), result);
#endif
	} else {
#ifdef XCALL_DEBUG
		printk("CPU[%d]: Penguin %d NACK's master.\n", smp_processor_id(), cpu);
#endif
		udelay(2);
		goto again;
	}
}

void smp_cross_call(unsigned long *func, u32 ctx, u64 data1, u64 data2)
{
	if(smp_processors_ready) {
		unsigned long mask = (cpu_present_map & ~(1UL<<smp_processor_id()));
		u64 pstate, data0 = (((u64)ctx)<<32 | (((u64)func) & 0xffffffff));
		int i, ncpus = smp_num_cpus - 1;

		__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));
		for(i = 0; i < NR_CPUS; i++) {
			if(mask & (1UL << i)) {
				xcall_deliver(data0, data1, data2, pstate, i);
				ncpus--;
			}
			if (!ncpus) break;
		}
		/* NOTE: Caller runs local copy on master. */
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

void smp_receive_signal(int cpu)
{
	if(smp_processors_ready &&
	   (cpu_present_map & (1UL<<cpu)) != 0) {
		u64 pstate, data0 = (((u64)&xcall_receive_signal) & 0xffffffff);
		__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));
		xcall_deliver(data0, 0, 0, pstate, cpu);
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
 * 2) TLB context numbers are shared globally across all processors
 *    in the system, this allows us to play several games to avoid
 *    cross calls.
 *
 *    One invariant is that when a cpu switches to a process, and
 *    that processes tsk->mm->cpu_vm_mask does not have the current
 *    cpu's bit set, that tlb context is flushed locally.
 *
 *    If the address space is non-shared (ie. mm->count == 1) we avoid
 *    cross calls when we want to flush the currently running process's
 *    tlb state.  This is done by clearing all cpu bits except the current
 *    processor's in current->mm->cpu_vm_mask and performing the flush
 *    locally only.  This will force any subsequent cpus which run this
 *    task to flush the context from the local tlb if the process migrates
 *    to another cpu (again).
 *
 * 3) For shared address spaces (threads) and swapping we bite the
 *    bullet for most cases and perform the cross call.
 *
 *    The performance gain from "optimizing" away the cross call for threads is
 *    questionable (in theory the big win for threads is the massive sharing of
 *    address space state across processors).
 *
 *    For the swapping case the locking is difficult to get right, we'd have to
 *    enforce strict ordered access to mm->cpu_vm_mask via a spinlock for example.
 *    Then again one could argue that when you are swapping, the cost of a cross
 *    call won't even show up on the performance radar.  But in any case we do get
 *    rid of the cross-call when the task has a dead context or the task has only
 *    ever run on the local cpu.
 */
void smp_flush_tlb_mm(struct mm_struct *mm)
{
	u32 ctx = mm->context & 0x3ff;

	if(mm == current->mm && atomic_read(&mm->count) == 1) {
		if(mm->cpu_vm_mask != (1UL << smp_processor_id()))
			mm->cpu_vm_mask = (1UL << smp_processor_id());
		goto local_flush_and_out;
	}
	smp_cross_call(&xcall_flush_tlb_mm, ctx, 0, 0);

local_flush_and_out:
	__flush_tlb_mm(ctx, SECONDARY_CONTEXT);
}

void smp_flush_tlb_range(struct mm_struct *mm, unsigned long start,
			 unsigned long end)
{
	u32 ctx = mm->context & 0x3ff;

	start &= PAGE_MASK;
	end   &= PAGE_MASK;
	if(mm == current->mm && atomic_read(&mm->count) == 1) {
		if(mm->cpu_vm_mask != (1UL << smp_processor_id()))
			mm->cpu_vm_mask = (1UL << smp_processor_id());
		goto local_flush_and_out;
	}
	smp_cross_call(&xcall_flush_tlb_range, ctx, start, end);

local_flush_and_out:
	__flush_tlb_range(ctx, start, SECONDARY_CONTEXT, end, PAGE_SIZE, (end-start));
}

void smp_flush_tlb_page(struct mm_struct *mm, unsigned long page)
{
	u32 ctx = mm->context & 0x3ff;

	page &= PAGE_MASK;
	if(mm == current->mm && atomic_read(&mm->count) == 1) {
		if(mm->cpu_vm_mask != (1UL << smp_processor_id()))
			mm->cpu_vm_mask = (1UL << smp_processor_id());
		goto local_flush_and_out;
	} else {
		spin_lock(&scheduler_lock);

		/* Try to handle two special cases to avoid cross calls
		 * in common scenerios where we are swapping process
		 * pages out.
		 */
		if(((mm->context ^ tlb_context_cache) & CTX_VERSION_MASK) ||
		   (mm->cpu_vm_mask == 0)) {
			/* A dead context cannot ever become "alive" until
			 * a task switch is done to it.
			 */
			spin_unlock(&scheduler_lock);
			return; /* It's dead, nothing to do. */
		}
		if(mm->cpu_vm_mask == (1UL << smp_processor_id())) {
			spin_unlock(&scheduler_lock);
			__flush_tlb_page(ctx, page, SECONDARY_CONTEXT);
			return; /* Only local flush is necessary. */
		}

		spin_unlock(&scheduler_lock);
	}
	smp_cross_call(&xcall_flush_tlb_page, ctx, page, 0);

local_flush_and_out:
	__flush_tlb_page(ctx, page, SECONDARY_CONTEXT);
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
		int result = atomic_add_return(1, &smp_capture_depth);

		membar("#StoreStore | #LoadStore");
		if(result == 1) {
			int ncpus = smp_num_cpus;

#ifdef CAPTURE_DEBUG
			printk("CPU[%d]: Sending penguins to jail...",
			       smp_processor_id());
#endif
			penguins_are_doing_time = 1;
			membar("#StoreStore | #LoadStore");
			atomic_inc(&smp_capture_registry);
			smp_cross_call(&xcall_capture, 0, 0, 0);
			while(atomic_read(&smp_capture_registry) != ncpus)
				membar("#LoadLoad");
#ifdef CAPTURE_DEBUG
			printk("done\n");
#endif
		}
	}
}

void smp_release(void)
{
	if(smp_processors_ready) {
		if(atomic_dec_and_test(&smp_capture_depth)) {
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
void smp_penguin_jailcell(void)
{
	flushw_user();
	atomic_inc(&smp_capture_registry);
	membar("#StoreLoad | #StoreStore");
	while(penguins_are_doing_time)
		membar("#LoadLoad");
	atomic_dec(&smp_capture_registry);
}

static inline void sparc64_do_profile(unsigned long pc)
{
	if (prof_buffer && current->pid) {
		extern int _stext;

		pc -= (unsigned long) &_stext;
		pc >>= prof_shift;

		if(pc >= prof_len)
			pc = prof_len - 1;
		atomic_inc((atomic_t *)&prof_buffer[pc]);
	}
}

static unsigned long current_tick_offset;

#define prof_multiplier(__cpu)		cpu_data[(__cpu)].multiplier
#define prof_counter(__cpu)		cpu_data[(__cpu)].counter

extern void update_one_process(struct task_struct *p, unsigned long ticks,
			       unsigned long user, unsigned long system,
			       int cpu);

void smp_percpu_timer_interrupt(struct pt_regs *regs)
{
	unsigned long compare, tick;
	int cpu = smp_processor_id();
	int user = user_mode(regs);

	/*
	 * Check for level 14 softint.
	 */
	if (!(get_softint() & (1UL << 0))) {
		extern void handler_irq(int, struct pt_regs *);

		handler_irq(14, regs);
		return;
	}

	clear_softint((1UL << 0));
	do {
		if(!user)
			sparc64_do_profile(regs->tpc);
		if(!--prof_counter(cpu))
		{
			if (cpu == boot_cpu_id) {
/* XXX Keep this in sync with irq.c --DaveM */
#define irq_enter(cpu, irq)			\
do {	hardirq_enter(cpu);			\
	spin_unlock_wait(&global_irq_lock);	\
} while(0)
#define irq_exit(cpu, irq)	hardirq_exit(cpu)

				irq_enter(cpu, 0);
				kstat.irqs[cpu][0]++;

				timer_tick_interrupt(regs);

				irq_exit(cpu, 0);

#undef irq_enter
#undef irq_exit
			}

			if(current->pid) {
				unsigned int *inc, *inc2;

				update_one_process(current, 1, user, !user, cpu);
				if(--current->counter < 0) {
					current->counter = 0;
					current->need_resched = 1;
				}

				if(user) {
					if(current->priority < DEF_PRIORITY) {
						inc = &kstat.cpu_nice;
						inc2 = &kstat.per_cpu_nice[cpu];
					} else {
						inc = &kstat.cpu_user;
						inc2 = &kstat.per_cpu_user[cpu];
					}
				} else {
					inc = &kstat.cpu_system;
					inc2 = &kstat.per_cpu_system[cpu];
				}
				atomic_inc((atomic_t *)inc);
				atomic_inc((atomic_t *)inc2);
			}
			prof_counter(cpu) = prof_multiplier(cpu);
		}

		__asm__ __volatile__("rd	%%tick_cmpr, %0\n\t"
				     "add	%0, %2, %0\n\t"
				     "wr	%0, 0x0, %%tick_cmpr\n\t"
				     "rd	%%tick, %1"
				     : "=&r" (compare), "=r" (tick)
				     : "r" (current_tick_offset));
	} while (tick >= compare);
}

__initfunc(static void smp_setup_percpu_timer(void))
{
	int cpu = smp_processor_id();

	prof_counter(cpu) = prof_multiplier(cpu) = 1;

	__asm__ __volatile__("rd	%%tick, %%g1\n\t"
			     "add	%%g1, %0, %%g1\n\t"
			     "wr	%%g1, 0x0, %%tick_cmpr"
			     : /* no outputs */
			     : "r" (current_tick_offset)
			     : "g1");
}

__initfunc(void smp_tick_init(void))
{
	int i;
	
	boot_cpu_id = hard_smp_processor_id();
	current_tick_offset = timer_tick_offset;
	cpu_present_map = 0;
	for(i = 0; i < linux_num_cpus; i++)
		cpu_present_map |= (1UL << linux_cpus[i].mid);
	for(i = 0; i < NR_CPUS; i++) {
		cpu_number_map[i] = -1;
		__cpu_logical_map[i] = -1;
	}
	cpu_number_map[boot_cpu_id] = 0;
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
	base = page_address(p);
	while(found != 0) {
		/* Failure. */
		if(p >= (mem_map + max_mapnr))
			return 0UL;
		if(PageSkip(p)) {
			p = p->next_hash;
			base = page_address(p);
			found = size;
		} else {
			found -= PAGE_SIZE;
			p++;
		}
	}
	return base;
}

cycles_t cacheflush_time;

__initfunc(static void smp_tune_scheduling (void))
{
	unsigned long flush_base, flags, *p;
	unsigned int ecache_size;
	cycles_t tick1, tick2, raw;

	/* Approximate heuristic for SMP scheduling.  It is an
	 * estimation of the time it takes to flush the L2 cache
	 * on the local processor.
	 *
	 * The ia32 chooses to use the L1 cache flush time instead,
	 * and I consider this complete nonsense.  The Ultra can service
	 * a miss to the L1 with a hit to the L2 in 7 or 8 cycles, and
	 * L2 misses are what create extra bus traffic (ie. the "cost"
	 * of moving a process from one cpu to another).
	 */
	printk("SMP: Calibrating ecache flush... ");
	ecache_size = prom_getintdefault(linux_cpus[0].prom_node,
					 "ecache-size", (512 *1024));
	flush_base = find_flush_base(ecache_size << 1);

	if(flush_base != 0UL) {
		__save_and_cli(flags);

		/* Scan twice the size once just to get the TLB entries
		 * loaded and make sure the second scan measures pure misses.
		 */
		for(p = (unsigned long *)flush_base;
		    ((unsigned long)p) < (flush_base + (ecache_size<<1));
		    p += (64 / sizeof(unsigned long)))
			*((volatile unsigned long *)p);

		/* Now the real measurement. */
		__asm__ __volatile__("
		b,pt	%%xcc, 1f
		 rd	%%tick, %0

		.align	64
1:		ldx	[%2 + 0x000], %%g1
		ldx	[%2 + 0x040], %%g2
		ldx	[%2 + 0x080], %%g3
		ldx	[%2 + 0x0c0], %%g5
		add	%2, 0x100, %2
		cmp	%2, %4
		bne,pt	%%xcc, 1b
		 nop
	
		rd	%%tick, %1"
		: "=&r" (tick1), "=&r" (tick2), "=&r" (flush_base)
		: "2" (flush_base), "r" (flush_base + ecache_size)
		: "g1", "g2", "g3", "g5");

		__restore_flags(flags);

		raw = (tick2 - tick1);

		/* Dampen it a little, considering two processes
		 * sharing the cache and fitting.
		 */
		cacheflush_time = (raw - (raw >> 2));
	} else
		cacheflush_time = ((ecache_size << 2) +
				   (ecache_size << 1));

	printk("Using heuristic of %d cycles.\n",
	       (int) cacheflush_time);
}

int __init setup_profiling_timer(unsigned int multiplier)
{
	unsigned long flags;
	int i;

	if((!multiplier) || (timer_tick_offset / multiplier) < 1000)
		return -EINVAL;

	save_and_cli(flags);
	for(i = 0; i < NR_CPUS; i++) {
		if(cpu_present_map & (1UL << i))
			prof_multiplier(i) = multiplier;
	}
	current_tick_offset = (timer_tick_offset / multiplier);
	restore_flags(flags);

	return 0;
}
