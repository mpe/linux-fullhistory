/* smp.c: Sparc64 SMP support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

extern int linux_num_cpus;
extern void calibrate_delay(void);

volatile int smp_processors_ready = 0;
unsigned long cpu_present_map = 0;
int smp_num_cpus = 1;
int smp_threads_ready = 0;

struct cpuinfo_sparc64 cpu_data[NR_CPUS];
static unsigned char boot_cpu_id = 0;
static int smp_activated = 0;

volatile int cpu_number_map[NR_CPUS];
volatile int cpu_logical_map[NR_CPUS];

struct klock_info klock_info = { KLOCK_CLEAR, 0 };

static volatile int smp_commenced = 0;

void smp_setup(char *str, int *ints)
{
	/* XXX implement me XXX */
}

static char smp_buf[512];

char *smp_info(void)
{
	/* XXX not SMP safe and need to support up to 64 penguins */
	sprintf(smp_buf,
"        CPU0\t\tCPU1\t\tCPU2\t\tCPU3\n"
"State:  %s\t\t%s\t\t%s\t\t%s\n",
(cpu_present_map & 1) ? ((klock_info.akp == 0) ? "akp" : "online") : "offline",
(cpu_present_map & 2) ? ((klock_info.akp == 1) ? "akp" : "online") : "offline",
(cpu_present_map & 4) ? ((klock_info.akp == 2) ? "akp" : "online") : "offline",
(cpu_present_map & 8) ? ((klock_info.akp == 3) ? "akp" : "online") : "offline");
	return smp_buf;
}

void smp_store_cpu_info(int id)
{
	cpu_data[id].udelay_val = loops_per_sec;
}

void smp_commence(void)
{
	local_flush_cache_all();
	local_flush_tlb_all();
	smp_commenced = 1;
	local_flush_cache_all();
	local_flush_tlb_all();
}

static void smp_setup_percpu_timer(void);

static volatile unsigned long callin_flag = 0;

void smp_callin(void)
{
	int cpuid = hard_smp_processor_id();

	local_flush_cache_all();
	local_flush_tlb_all();

	smp_setup_percpu_timer();

	calibrate_delay();
	smp_store_cpu_info(cpuid);
	callin_flag = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "flush  %g6" : : : "memory");

	while(!task[cpuid])
		barrier();
	current = task[cpuid];

	while(!smp_commenced)
		barrier();

	__sti();
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

extern struct prom_cpuinfo linux_cpus[NR_CPUS];

void smp_boot_cpus(void)
{
	int cpucount = 0, i, first, prev;

	printk("Entering UltraSMPenguin Mode...\n");
	__sti();
	cpu_present_map = 0;
	for(i = 0; i < linux_num_cpus; i++)
		cpu_present_map |= (1 << i);
	for(i = 0; i < NR_CPUS; i++) {
		cpu_number_map[i] = -1;
		cpu_logical_map[i] = -1;
	}
	cpu_number_map[boot_cpu_id] = 0;
	cpu_logical_map[0] = boot_cpu_id;
	klock_info.akp = boot_cpu_id;
	current->processor = boot_cpu_id;
	smp_store_cpu_info(boot_cpu_id);
	smp_setup_percpu_timer();

	if(linux_num_cpus == 1)
		return;

	for(i = 0; i < NR_CPUS; i++) {
		if(i == boot_cpu_id)
			continue;

		if(cpu_present_map & (1 << i)) {
			extern unsigned long sparc64_cpu_startup;
			unsigned long entry = (unsigned long)&sparc_cpu_startup;
			struct task_struct *p;
			int timeout;

			kernel_thread(start_secondary, NULL, CLONE_PID);
			p = task[++cpucount];
			p->processor = i;
			prom_startcpu(linux_cpus[i].prom_node, entry, i);
			for(timeout = 0; timeout < 5000000; timeout++) {
				if(cpu_callin_map[i])
					break;
				udelay(100);
			}
			if(cpu_callin_map[i]) {
				/* XXX fix this */
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
	if(cpucount == 0) {
		printk("Error: only one processor found.\n");
		cpu_present_map = (1 << smp_processor_id());
	} else {
		unsigned long bogosum = 0;

		for(i = 0; i < NR_CPUS; i++) {
			if(cpu_present_map & (1 << i))
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
}

/* XXX deprecated interface... */
void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	printk("smp_message_pass() called, this is bad, spinning.\n");
	__sti();
	while(1)
		barrier();
}

/* XXX Make it fast later. */
void smp_cross_call(unsigned long *func, u32 ctx, u64 data1, u64 data2)
{
	if(smp_processors_ready) {
		unsigned long mask;
		u64 data0 = (((unsigned long)ctx)<<32 |
			     (((unsigned long)func) & 0xffffffff));
		u64 pstate;
		int i, ncpus = smp_num_cpus;

		__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));
		mask = (cpu_present_map & ~(1 << smp_processor_id()));
		for(i = 0; i < ncpus; i++) {
			if(mask & (1 << i)) {
				u64 target = mid<<14 | 0x70;
				u64 result;

				__asm__ __volatile__("
				wrpr	%0, %1, %%pstate
				wrpr	%%g0, %2, %%asi
				stxa	%3, [0x40] %%asi
				stxa	%4, [0x50] %%asi
				stxa	%5, [0x60] %%asi
				stxa	%%g0, [%6] %7
				membar	#Sync"
				: /* No outputs */
				: "r" (pstate), "i" (PSTATE_IE), "i" (ASI_UDB_INTR_W),
				  "r" (data0), "r" (data1), "r" (data2),
				  "r" (target), "i" (ASI_UDB_INTR_W));

				/* NOTE: PSTATE_IE is still clear. */
				do {
					__asm__ __volatile__("ldxa [%%g0] %1, %0",
						: "=r" (result)
						: "i" (ASI_INTR_DISPATCH_STAT));
				} while(result & 0x1);
				__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
						     : : "r" (pstate));
				if(result & 0x2)
					panic("Penguin NACK's master!");
			}
		}

		/* NOTE: Caller runs local copy on master. */
	}
}

extern unsigned long xcall_flush_tlb_page;
extern unsigned long xcall_flush_tlb_mm;
extern unsigned long xcall_flush_tlb_range;
extern unsigned long xcall_flush_tlb_all;
extern unsigned long xcall_flush_cache_all;

void smp_flush_cache_all(void)
{
	smp_cross_call(&xcall_flush_cache_all, 0, 0, 0);
}

void smp_flush_tlb_all(void)
{
	smp_cross_call(&xcall_flush_tlb_all, 0, 0, 0);
}

void smp_flush_tlb_mm(struct mm_struct *mm)
{
	u32 ctx = mm->context & 0x1fff;
	if(mm->cpu_vm_mask != (1 << smp_processor_id()))
		smp_cross_call(&xcall_flush_tlb_mm, ctx, 0, 0);
	__flush_tlb_mm(ctx);
}

void smp_flush_tlb_range(struct mm_struct *mm, unsigned long start,
			 unsigned long end)
{
	u32 ctx = mm->context & 0x1fff;
	if(mm->cpu_vm_mask != (1 << smp_processor_id()))
		smp_cross_call(&xcall_flush_tlb_range, ctx, start, end);
	__flush_tlb_range(ctx, start, end);
}

void smp_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	u32 ctx = mm->context & 0x1fff;

	if(mm->cpu_vm_mask != (1 << smp_processor_id()))
		smp_cross_call(&xcall_flush_tlb_page, ctx, page, 0);
	__flush_tlb_page(ctx, page);
}

static spinlock_t ticker_lock = SPIN_LOCK_UNLOCKED;

static inline void sparc64_do_profile(unsigned long pc)
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

unsigned int prof_multiplier[NR_CPUS];
unsigned int prof_counter[NR_CPUS];

extern void update_one_process(struct task_struct *p, unsigned long ticks,
			       unsigned long user, unsigned long system);

void smp_percpu_timer_interrupt(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	clear_profile_irq(cpu);
	if(!user_mode(regs))
		sparc_do_profile(regs->pc);
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

static void smp_setup_percpu_timer(void)
{
	/* XXX implement me */
}

int setup_profiling_timer(unsigned int multiplier)
{
	/* XXX implement me */
}
