/*
 * SMP boot-related support
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 01/05/16 Rohit Seth <rohit.seth@intel.com>	Moved SMP booting functions from smp.c to here.
 * 01/04/27 David Mosberger <davidm@hpl.hp.com>	Added ITC synching code.
 * 02/07/31 David Mosberger <davidm@hpl.hp.com>	Switch over to hotplug-CPU boot-sequence.
 *						smp_boot_cpus()/smp_commence() is replaced by
 *						smp_prepare_cpus()/__cpu_up()/smp_cpus_done().
 */


#define __KERNEL_SYSCALLS__

#include <linux/config.h>

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/efi.h>

#include <asm/atomic.h>
#include <asm/bitops.h>
#include <asm/cache.h>
#include <asm/current.h>
#include <asm/delay.h>
#include <asm/ia32.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/machvec.h>
#include <asm/mca.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/unistd.h>

#define SMP_DEBUG 0

#if SMP_DEBUG
#define Dprintk(x...)  printk(x)
#else
#define Dprintk(x...)
#endif


/*
 * ITC synchronization related stuff:
 */
#define MASTER	0
#define SLAVE	(SMP_CACHE_BYTES/8)

#define NUM_ROUNDS	64	/* magic value */
#define NUM_ITERS	5	/* likewise */

static spinlock_t itc_sync_lock = SPIN_LOCK_UNLOCKED;
static volatile unsigned long go[SLAVE + 1];

#define DEBUG_ITC_SYNC	0

extern void __init calibrate_delay (void);
extern void start_ap (void);
extern unsigned long ia64_iobase;

int cpucount;
task_t *task_for_booting_cpu;

/* Bitmask of currently online CPUs */
cpumask_t cpu_online_map;
EXPORT_SYMBOL(cpu_online_map);
cpumask_t phys_cpu_present_map;
EXPORT_SYMBOL(phys_cpu_present_map);

/* which logical CPU number maps to which CPU (physical APIC ID) */
volatile int ia64_cpu_to_sapicid[NR_CPUS];
EXPORT_SYMBOL(ia64_cpu_to_sapicid);

static volatile cpumask_t cpu_callin_map;

struct smp_boot_data smp_boot_data __initdata;

unsigned long ap_wakeup_vector = -1; /* External Int use to wakeup APs */

char __initdata no_int_routing;

unsigned char smp_int_redirect; /* are INT and IPI redirectable by the chipset? */

static int __init
nointroute (char *str)
{
	no_int_routing = 1;
	return 1;
}

__setup("nointroute", nointroute);

void
sync_master (void *arg)
{
	unsigned long flags, i;

	go[MASTER] = 0;

	local_irq_save(flags);
	{
		for (i = 0; i < NUM_ROUNDS*NUM_ITERS; ++i) {
			while (!go[MASTER]);
			go[MASTER] = 0;
			go[SLAVE] = ia64_get_itc();
		}
	}
	local_irq_restore(flags);
}

/*
 * Return the number of cycles by which our itc differs from the itc on the master
 * (time-keeper) CPU.  A positive number indicates our itc is ahead of the master,
 * negative that it is behind.
 */
static inline long
get_delta (long *rt, long *master)
{
	unsigned long best_t0 = 0, best_t1 = ~0UL, best_tm = 0;
	unsigned long tcenter, t0, t1, tm;
	long i;

	for (i = 0; i < NUM_ITERS; ++i) {
		t0 = ia64_get_itc();
		go[MASTER] = 1;
		while (!(tm = go[SLAVE]));
		go[SLAVE] = 0;
		t1 = ia64_get_itc();

		if (t1 - t0 < best_t1 - best_t0)
			best_t0 = t0, best_t1 = t1, best_tm = tm;
	}

	*rt = best_t1 - best_t0;
	*master = best_tm - best_t0;

	/* average best_t0 and best_t1 without overflow: */
	tcenter = (best_t0/2 + best_t1/2);
	if (best_t0 % 2 + best_t1 % 2 == 2)
		++tcenter;
	return tcenter - best_tm;
}

/*
 * Synchronize ar.itc of the current (slave) CPU with the ar.itc of the MASTER CPU
 * (normally the time-keeper CPU).  We use a closed loop to eliminate the possibility of
 * unaccounted-for errors (such as getting a machine check in the middle of a calibration
 * step).  The basic idea is for the slave to ask the master what itc value it has and to
 * read its own itc before and after the master responds.  Each iteration gives us three
 * timestamps:
 *
 *	slave		master
 *
 *	t0 ---\
 *             ---\
 *		   --->
 *			tm
 *		   /---
 *	       /---
 *	t1 <---
 *
 *
 * The goal is to adjust the slave's ar.itc such that tm falls exactly half-way between t0
 * and t1.  If we achieve this, the clocks are synchronized provided the interconnect
 * between the slave and the master is symmetric.  Even if the interconnect were
 * asymmetric, we would still know that the synchronization error is smaller than the
 * roundtrip latency (t0 - t1).
 *
 * When the interconnect is quiet and symmetric, this lets us synchronize the itc to
 * within one or two cycles.  However, we can only *guarantee* that the synchronization is
 * accurate to within a round-trip time, which is typically in the range of several
 * hundred cycles (e.g., ~500 cycles).  In practice, this means that the itc's are usually
 * almost perfectly synchronized, but we shouldn't assume that the accuracy is much better
 * than half a micro second or so.
 */
void
ia64_sync_itc (unsigned int master)
{
	long i, delta, adj, adjust_latency = 0, done = 0;
	unsigned long flags, rt, master_time_stamp, bound;
	extern void ia64_cpu_local_tick (void);
#if DEBUG_ITC_SYNC
	struct {
		long rt;	/* roundtrip time */
		long master;	/* master's timestamp */
		long diff;	/* difference between midpoint and master's timestamp */
		long lat;	/* estimate of itc adjustment latency */
	} t[NUM_ROUNDS];
#endif

	go[MASTER] = 1;

	if (smp_call_function_single(master, sync_master, NULL, 1, 0) < 0) {
		printk(KERN_ERR "sync_itc: failed to get attention of CPU %u!\n", master);
		return;
	}

	while (go[MASTER]);	/* wait for master to be ready */

	spin_lock_irqsave(&itc_sync_lock, flags);
	{
		for (i = 0; i < NUM_ROUNDS; ++i) {
			delta = get_delta(&rt, &master_time_stamp);
			if (delta == 0) {
				done = 1;	/* let's lock on to this... */
				bound = rt;
			}

			if (!done) {
				if (i > 0) {
					adjust_latency += -delta;
					adj = -delta + adjust_latency/4;
				} else
					adj = -delta;

				ia64_set_itc(ia64_get_itc() + adj);
			}
#if DEBUG_ITC_SYNC
			t[i].rt = rt;
			t[i].master = master_time_stamp;
			t[i].diff = delta;
			t[i].lat = adjust_latency/4;
#endif
		}
	}
	spin_unlock_irqrestore(&itc_sync_lock, flags);

#if DEBUG_ITC_SYNC
	for (i = 0; i < NUM_ROUNDS; ++i)
		printk("rt=%5ld master=%5ld diff=%5ld adjlat=%5ld\n",
		       t[i].rt, t[i].master, t[i].diff, t[i].lat);
#endif

	printk(KERN_INFO "CPU %d: synchronized ITC with CPU %u (last diff %ld cycles, "
	       "maxerr %lu cycles)\n", smp_processor_id(), master, delta, rt);

	/*
	 * Check whether we sync'd the itc ahead of the next timer interrupt.  If so, just
	 * reset it.
	 */
	if (time_after(ia64_get_itc(), local_cpu_data->itm_next)) {
		Dprintk("CPU %d: oops, jumped a timer tick; resetting timer.\n",
			smp_processor_id());
		ia64_cpu_local_tick();
	}
}

/*
 * Ideally sets up per-cpu profiling hooks.  Doesn't do much now...
 */
static inline void __init
smp_setup_percpu_timer (void)
{
	local_cpu_data->prof_counter = 1;
	local_cpu_data->prof_multiplier = 1;
}

static void __init
smp_callin (void)
{
	int cpuid, phys_id;
	extern void ia64_init_itm(void);

#ifdef CONFIG_PERFMON
	extern void pfm_init_percpu(void);
#endif

	cpuid = smp_processor_id();
	phys_id = hard_smp_processor_id();

	if (cpu_test_and_set(cpuid, cpu_online_map)) {
		printk(KERN_ERR "huh, phys CPU#0x%x, CPU#0x%x already present??\n",
		       phys_id, cpuid);
		BUG();
	}

	smp_setup_percpu_timer();

	/*
	 * Get our bogomips.
	 */
	ia64_init_itm();

	/*
	 * Set I/O port base per CPU
	 */
	ia64_set_kr(IA64_KR_IO_BASE, __pa(ia64_iobase));

#ifdef CONFIG_IA64_MCA
	ia64_mca_cmc_vector_setup();	/* Setup vector on AP & enable */
	ia64_mca_check_errors();	/* For post-failure MCA error logging */
#endif

#ifdef CONFIG_PERFMON
	pfm_init_percpu();
#endif

	local_irq_enable();
	calibrate_delay();
	local_cpu_data->loops_per_jiffy = loops_per_jiffy;
#ifdef CONFIG_IA32_SUPPORT
	ia32_gdt_init();
#endif

	if (!(sal_platform_features & IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT)) {
		/*
		 * Synchronize the ITC with the BP.  Need to do this after irqs are
		 * enabled because ia64_sync_itc() calls smp_call_function_single(), which
		 * calls spin_unlock_bh(), which calls spin_unlock_bh(), which calls
		 * local_bh_enable(), which bugs out if irqs are not enabled...
		 */
		Dprintk("Going to syncup ITC with BP.\n");
		ia64_sync_itc(0);
	}

	/*
	 * Allow the master to continue.
	 */
	cpu_set(cpuid, cpu_callin_map);
	Dprintk("Stack on CPU %d at about %p\n",cpuid, &cpuid);
}


/*
 * Activate a secondary processor.  head.S calls this.
 */
int __init
start_secondary (void *unused)
{
	extern int cpu_idle (void);

	Dprintk("start_secondary: starting CPU 0x%x\n", hard_smp_processor_id());
	efi_map_pal_code();
	cpu_init();
	smp_callin();

	return cpu_idle();
}

static struct task_struct * __init
fork_by_hand (void)
{
	/*
	 * Don't care about the IP and regs settings since we'll never reschedule the
	 * forked task.
	 */
	return copy_process(CLONE_VM|CLONE_IDLETASK, 0, 0, 0, NULL, NULL);
}

static int __init
do_boot_cpu (int sapicid, int cpu)
{
	struct task_struct *idle;
	int timeout;

	/*
	 * We can't use kernel_thread since we must avoid to reschedule the child.
	 */
	idle = fork_by_hand();
	if (IS_ERR(idle))
		panic("failed fork for CPU %d", cpu);
	wake_up_forked_process(idle);

	/*
	 * We remove it from the pidhash and the runqueue
	 * once we got the process:
	 */
	init_idle(idle, cpu);

	unhash_process(idle);

	task_for_booting_cpu = idle;

	Dprintk("Sending wakeup vector %lu to AP 0x%x/0x%x.\n", ap_wakeup_vector, cpu, sapicid);

	platform_send_ipi(cpu, ap_wakeup_vector, IA64_IPI_DM_INT, 0);

	/*
	 * Wait 10s total for the AP to start
	 */
	Dprintk("Waiting on callin_map ...");
	for (timeout = 0; timeout < 100000; timeout++) {
		if (cpu_isset(cpu, cpu_callin_map))
			break;  /* It has booted */
		udelay(100);
	}
	Dprintk("\n");

	if (!cpu_isset(cpu, cpu_callin_map)) {
		printk(KERN_ERR "Processor 0x%x/0x%x is stuck.\n", cpu, sapicid);
		ia64_cpu_to_sapicid[cpu] = -1;
		cpu_clear(cpu, cpu_online_map);  /* was set in smp_callin() */
		return -EINVAL;
	}
	return 0;
}

static int __init
decay (char *str)
{
	int ticks;
	get_option (&str, &ticks);
	cache_decay_ticks = ticks;
	return 1;
}

__setup("decay=", decay);

/*
 * # of ticks an idle task is considered cache-hot.  Highly application-dependent.  There
 * are apps out there which are known to suffer significantly with values >= 4.
 */
unsigned long cache_decay_ticks = 10;	/* equal to MIN_TIMESLICE */

static void
smp_tune_scheduling (void)
{
	printk(KERN_INFO "task migration cache decay timeout: %ld msecs.\n",
	       (cache_decay_ticks + 1) * 1000 / HZ);
}

/*
 * Initialize the logical CPU number to SAPICID mapping
 */
void __init
smp_build_cpu_map (void)
{
	int sapicid, cpu, i;
	int boot_cpu_id = hard_smp_processor_id();

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		ia64_cpu_to_sapicid[cpu] = -1;

	ia64_cpu_to_sapicid[0] = boot_cpu_id;
	cpus_clear(phys_cpu_present_map);
	cpu_set(0, phys_cpu_present_map);

	for (cpu = 1, i = 0; i < smp_boot_data.cpu_count; i++) {
		sapicid = smp_boot_data.cpu_phys_id[i];
		if (sapicid == boot_cpu_id)
			continue;
		cpu_set(cpu, phys_cpu_present_map);
		ia64_cpu_to_sapicid[cpu] = sapicid;
		cpu++;
	}
}

#ifdef CONFIG_NUMA

/* on which node is each logical CPU (one cacheline even for 64 CPUs) */
u8 cpu_to_node_map[NR_CPUS] __cacheline_aligned;
EXPORT_SYMBOL(cpu_to_node_map);
/* which logical CPUs are on which nodes */
cpumask_t node_to_cpu_mask[MAX_NUMNODES] __cacheline_aligned;

/*
 * Build cpu to node mapping and initialize the per node cpu masks.
 */
void __init
build_cpu_to_node_map (void)
{
	int cpu, i, node;

	for(node=0; node<MAX_NUMNODES; node++)
		cpus_clear(node_to_cpu_mask[node]);
	for(cpu = 0; cpu < NR_CPUS; ++cpu) {
		/*
		 * All Itanium NUMA platforms I know use ACPI, so maybe we
		 * can drop this ifdef completely.                    [EF]
		 */
#ifdef CONFIG_ACPI_NUMA
		node = -1;
		for (i = 0; i < NR_CPUS; ++i)
			if (cpu_physical_id(cpu) == node_cpuid[i].phys_id) {
				node = node_cpuid[i].nid;
				break;
			}
#else
#		error Fixme: Dunno how to build CPU-to-node map.
#endif
		cpu_to_node_map[cpu] = node;
		if (node >= 0)
			cpu_set(cpu, node_to_cpu_mask[node]);
	}
}

#endif /* CONFIG_NUMA */

/*
 * Cycle through the APs sending Wakeup IPIs to boot each.
 */
void __init
smp_prepare_cpus (unsigned int max_cpus)
{
	int boot_cpu_id = hard_smp_processor_id();

	/*
	 * Initialize the per-CPU profiling counter/multiplier
	 */

	smp_setup_percpu_timer();

	/*
	 * We have the boot CPU online for sure.
	 */
	cpu_set(0, cpu_online_map);
	cpu_set(0, cpu_callin_map);

	local_cpu_data->loops_per_jiffy = loops_per_jiffy;
	ia64_cpu_to_sapicid[0] = boot_cpu_id;

	printk(KERN_INFO "Boot processor id 0x%x/0x%x\n", 0, boot_cpu_id);

	current_thread_info()->cpu = 0;
	smp_tune_scheduling();

	/*
	 * If SMP should be disabled, then really disable it!
	 */
	if (!max_cpus) {
		printk(KERN_INFO "SMP mode deactivated.\n");
		cpus_clear(cpu_online_map);
		cpus_clear(phys_cpu_present_map);
		cpu_set(0, cpu_online_map);
		cpu_set(0, phys_cpu_present_map);
		return;
	}
}

void __devinit smp_prepare_boot_cpu(void)
{
	cpu_set(smp_processor_id(), cpu_online_map);
	cpu_set(smp_processor_id(), cpu_callin_map);
}

void
smp_cpus_done (unsigned int dummy)
{
	int cpu;
	unsigned long bogosum = 0;

	/*
	 * Allow the user to impress friends.
	 */

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		if (cpu_online(cpu))
			bogosum += cpu_data(cpu)->loops_per_jiffy;

	printk(KERN_INFO "Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
	       (int)num_online_cpus(), bogosum/(500000/HZ), (bogosum/(5000/HZ))%100);
}

int __devinit
__cpu_up (unsigned int cpu)
{
	int ret;
	int sapicid;

	sapicid = ia64_cpu_to_sapicid[cpu];
	if (sapicid == -1)
		return -EINVAL;

	/* Processor goes to start_secondary(), sets online flag */
	ret = do_boot_cpu(sapicid, cpu);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * Assume that CPU's have been discovered by some platform-dependent interface.  For
 * SoftSDV/Lion, that would be ACPI.
 *
 * Setup of the IPI irq handler is done in irq.c:init_IRQ_SMP().
 */
void __init
init_smp_config(void)
{
	struct fptr {
		unsigned long fp;
		unsigned long gp;
	} *ap_startup;
	long sal_ret;

	/* Tell SAL where to drop the AP's.  */
	ap_startup = (struct fptr *) start_ap;
	sal_ret = ia64_sal_set_vectors(SAL_VECTOR_OS_BOOT_RENDEZ,
				       ia64_tpa(ap_startup->fp), ia64_tpa(ap_startup->gp), 0, 0, 0, 0);
	if (sal_ret < 0)
		printk(KERN_ERR "SMP: Can't set SAL AP Boot Rendezvous: %s\n",
		       ia64_sal_strerror(sal_ret));
}
