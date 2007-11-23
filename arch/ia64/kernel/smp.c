/*
 * SMP Support
 *
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * 
 * Lots of stuff stolen from arch/alpha/kernel/smp.c
 *
 *  00/03/31 Rohit Seth <rohit.seth@intel.com>	Fixes for Bootstrap Processor & cpu_online_map
 *			now gets done here (instead of setup.c)
 *  99/10/05 davidm	Update to bring it in sync with new command-line processing scheme.
 */
#define __KERNEL_SYSCALLS__

#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>

#include <asm/atomic.h>
#include <asm/bitops.h>
#include <asm/current.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/unistd.h>

extern int cpu_idle(void * unused);
extern void _start(void);

extern int cpu_now_booting;			     /* Used by head.S to find idle task */
extern volatile unsigned long cpu_online_map;	     /* Bitmap of available cpu's */
extern struct cpuinfo_ia64 cpu_data[NR_CPUS];	     /* Duh... */

spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;

struct smp_boot_data __initdata smp;
char __initdata no_int_routing = 0;

unsigned char smp_int_redirect;			/* are INT and IPI redirectable by the chipset? */
volatile int __cpu_number_map[NR_CPUS] = { -1, };    /* SAPIC ID -> Logical ID */
volatile int __cpu_logical_map[NR_CPUS] = { -1, };   /* logical ID -> SAPIC ID */
int smp_num_cpus = 1;		
int bootstrap_processor = -1;			     /* SAPIC ID of BSP */
int smp_threads_ready = 0;			     /* Set when the idlers are all forked */
cycles_t cacheflush_time = 0;
unsigned long ap_wakeup_vector = -1;		     /* External Int to use to wakeup AP's */
static int max_cpus = -1;			     /* Command line */
static unsigned long ipi_op[NR_CPUS];
struct smp_call_struct {
	void (*func) (void *info);
	void *info;
	long wait;
	atomic_t unstarted_count;
	atomic_t unfinished_count;
};
static struct smp_call_struct *smp_call_function_data;

#ifdef	CONFIG_ITANIUM_ASTEP_SPECIFIC
extern spinlock_t ivr_read_lock;
#endif

#define IPI_RESCHEDULE	        0
#define IPI_CALL_FUNC	        1
#define IPI_CPU_STOP	        2

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

static int __init nosmp(char *str)
{
	max_cpus = 0;
	return 1;
}

__setup("nosmp", nosmp);

static int __init maxcpus(char *str)
{
	get_option(&str, &max_cpus);
	return 1;
}

__setup("maxcpus=", maxcpus);

static int __init
nointroute(char *str)
{
	no_int_routing = 1;
	return 1;
}

__setup("nointroute", nointroute);

/*
 * Yoink this CPU from the runnable list... 
 */
void
halt_processor(void) 
{
        clear_bit(smp_processor_id(), &cpu_online_map);
	max_xtp();
	__cli();
        for (;;)
		;

}

void
handle_IPI(int irq, void *dev_id, struct pt_regs *regs) 
{
	int this_cpu = smp_processor_id();
	unsigned long *pending_ipis = &ipi_op[this_cpu];
	unsigned long ops;

	/* Count this now; we may make a call that never returns. */
	cpu_data[this_cpu].ipi_count++;

	mb();	/* Order interrupt and bit testing. */
	while ((ops = xchg(pending_ipis, 0)) != 0) {
	  mb();	/* Order bit clearing and data access. */
	  do {
		unsigned long which;

		which = ffz(~ops);
		ops &= ~(1 << which);
		
		switch (which) {
		case IPI_RESCHEDULE:
			/* 
			 * Reschedule callback.  Everything to be done is done by the 
			 * interrupt return path.  
			 */
			break;
			
		case IPI_CALL_FUNC: 
			{
				struct smp_call_struct *data;
				void (*func)(void *info);
				void *info;
				int wait;

				data = smp_call_function_data;
				func = data->func;
				info = data->info;
				wait = data->wait;

				mb();
				atomic_dec (&data->unstarted_count);

				/* At this point the structure may be gone unless wait is true.  */
				(*func)(info);

				/* Notify the sending CPU that the task is done.  */
				mb();
				if (wait) 
					atomic_dec (&data->unfinished_count);
			}
			break;

		case IPI_CPU_STOP:
			halt_processor();
			break;

		default:
			printk(KERN_CRIT "Unknown IPI on CPU %d: %lu\n", this_cpu, which);
			break;
		} /* Switch */
	  } while (ops);

	  mb();	/* Order data access and bit testing. */
	}
}

static inline void
send_IPI_single(int dest_cpu, int op) 
{
	
	if (dest_cpu == -1) 
                return;
        
        ipi_op[dest_cpu] |= (1 << op);
	ipi_send(dest_cpu, IPI_IRQ, IA64_IPI_DM_INT, 0);
}

static inline void
send_IPI_allbutself(int op)
{
	int i;
	int cpu_id = 0;
	
	for (i = 0; i < smp_num_cpus; i++) {
		cpu_id = __cpu_logical_map[i];
		if (cpu_id != smp_processor_id())
			send_IPI_single(cpu_id, op);
	}
}

static inline void
send_IPI_all(int op)
{
	int i;

	for (i = 0; i < smp_num_cpus; i++)
		send_IPI_single(__cpu_logical_map[i], op);
}

static inline void
send_IPI_self(int op)
{
	send_IPI_single(smp_processor_id(), op);
}

void
smp_send_reschedule(int cpu)
{
	send_IPI_single(cpu, IPI_RESCHEDULE);
}

void
smp_send_stop(void)
{
	send_IPI_allbutself(IPI_CPU_STOP);
}

/*
 * Run a function on all other CPUs.
 *  <func>	The function to run. This must be fast and non-blocking.
 *  <info>	An arbitrary pointer to pass to the function.
 *  <retry>	If true, keep retrying until ready.
 *  <wait>	If true, wait until function has completed on other CPUs.
 *  [RETURNS]   0 on success, else a negative status code.
 *
 * Does not return until remote CPUs are nearly ready to execute <func>
 * or are or have executed.
 */

int
smp_call_function (void (*func) (void *info), void *info, int retry, int wait)
{
	struct smp_call_struct data;
	long timeout;
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	
	data.func = func;
	data.info = info;
	data.wait = wait;
	atomic_set(&data.unstarted_count, smp_num_cpus - 1);
	atomic_set(&data.unfinished_count, smp_num_cpus - 1);

	if (retry) {
		while (1) {
			if (smp_call_function_data) {
				schedule ();  /*  Give a mate a go  */
				continue;
			}
			spin_lock (&lock);
			if (smp_call_function_data) {
				spin_unlock (&lock);  /*  Bad luck  */
				continue;
			}
			/*  Mine, all mine!  */
			break;
		}
	}
	else {
		if (smp_call_function_data) 
			return -EBUSY;
		spin_lock (&lock);
		if (smp_call_function_data) {
			spin_unlock (&lock);
			return -EBUSY;
		}
	}

	smp_call_function_data = &data;
	spin_unlock (&lock);
	data.func = func;
	data.info = info;
	atomic_set (&data.unstarted_count, smp_num_cpus - 1);
	data.wait = wait;
	if (wait) 
		atomic_set (&data.unfinished_count, smp_num_cpus - 1);
	
	/*  Send a message to all other CPUs and wait for them to respond  */
	send_IPI_allbutself(IPI_CALL_FUNC);

	/*  Wait for response  */
	timeout = jiffies + HZ;
	while ( (atomic_read (&data.unstarted_count) > 0) &&
		time_before (jiffies, timeout) )
		barrier ();
	if (atomic_read (&data.unstarted_count) > 0) {
		smp_call_function_data = NULL;
		return -ETIMEDOUT;
	}
	if (wait)
		while (atomic_read (&data.unfinished_count) > 0)
			barrier ();
	smp_call_function_data = NULL;
	return 0;
}

/*
 * Flush all other CPU's tlb and then mine.  Do this with smp_call_function() as we
 * want to ensure all TLB's flushed before proceeding.
 *
 * XXX: Is it OK to use the same ptc.e info on all cpus?
 */
void
smp_flush_tlb_all(void)
{
	smp_call_function((void (*)(void *))__flush_tlb_all, NULL, 1, 1);
	__flush_tlb_all();
}

/*
 * Ideally sets up per-cpu profiling hooks.  Doesn't do much now...
 */
static inline void __init
smp_setup_percpu_timer(int cpuid)
{
        cpu_data[cpuid].prof_counter = 1;
        cpu_data[cpuid].prof_multiplier = 1;
}

void 
smp_do_timer(struct pt_regs *regs)
{
        int cpu = smp_processor_id();
        int user = user_mode(regs);
	struct cpuinfo_ia64 *data = &cpu_data[cpu];

	extern void update_one_process(struct task_struct *, unsigned long, unsigned long, 
				       unsigned long, int);
        if (!--data->prof_counter) {
		irq_enter(cpu, TIMER_IRQ);

		update_one_process(current, 1, user, !user, cpu);
		if (current->pid) { 
			if (--current->counter < 0) {
				current->counter = 0;
				current->need_resched = 1;
			}

			if (user) {
				if (current->priority < DEF_PRIORITY) {
					kstat.cpu_nice++;
					kstat.per_cpu_nice[cpu]++;
				} else {
					kstat.cpu_user++;
					kstat.per_cpu_user[cpu]++;
				}
			} else {
				kstat.cpu_system++;
				kstat.per_cpu_system[cpu]++;
			}
		} 
		
		data->prof_counter = data->prof_multiplier;
		irq_exit(cpu, TIMER_IRQ);
	}
}


/*
 * Called by both boot and secondaries to move global data into
 * per-processor storage.
 */
static inline void __init
smp_store_cpu_info(int cpuid)
{
	struct cpuinfo_ia64 *c = &cpu_data[cpuid];

	identify_cpu(c);
}

/* 
 * SAL shoves the AP's here when we start them.  Physical mode, no kernel TR, 
 * no RRs set, better than even chance that psr is bogus.  Fix all that and 
 * call _start.  In effect, pretend to be lilo.
 *
 * Stolen from lilo_start.c.  Thanks David! 
 */
void
start_ap(void)
{
	unsigned long flags;

	/*
	 * Install a translation register that identity maps the
	 * kernel's 256MB page(s).
	 */
	ia64_clear_ic(flags);
	ia64_set_rr(          0, (0x1000 << 8) | (_PAGE_SIZE_1M << 2));
	ia64_set_rr(PAGE_OFFSET, (ia64_rid(0, PAGE_OFFSET) << 8) | (_PAGE_SIZE_256M << 2));
	ia64_srlz_d();
	ia64_itr(0x3, 1, PAGE_OFFSET,
		 pte_val(mk_pte_phys(0, __pgprot(__DIRTY_BITS|_PAGE_PL_0|_PAGE_AR_RWX))),
		 _PAGE_SIZE_256M);
	ia64_srlz_i();

	flags = (IA64_PSR_IT | IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_RT | IA64_PSR_DFH | 
		 IA64_PSR_BN);
	
	asm volatile ("movl r8 = 1f\n"
		      ";;\n"
		      "mov cr.ipsr=%0\n"
		      "mov cr.iip=r8\n" 
		      "mov cr.ifs=r0\n"
		      ";;\n"
		      "rfi;;"
		      "1:\n"
		      "movl r1 = __gp" :: "r"(flags) : "r8");
	_start();
}


/*
 * AP's start using C here.
 */
void __init
smp_callin(void) 
{
	extern void ia64_rid_init(void);
	extern void ia64_init_itm(void);
	extern void ia64_cpu_local_tick(void);

	ia64_set_dcr(IA64_DCR_DR | IA64_DCR_DK | IA64_DCR_DX | IA64_DCR_PP);
	ia64_set_fpu_owner(0);	       
	ia64_rid_init();		/* initialize region ids */

	cpu_init();
	__flush_tlb_all();

	smp_store_cpu_info(smp_processor_id());
	smp_setup_percpu_timer(smp_processor_id());

	if (test_and_set_bit(smp_processor_id(), &cpu_online_map)) {
		printk("CPU#%d already initialized!\n", smp_processor_id());
		machine_halt();
	}  
	while (!smp_threads_ready) 
		mb();

	normal_xtp();

	/* setup the CPU local timer tick */
	ia64_cpu_local_tick();

	/* Disable all local interrupts */
	ia64_set_lrr0(0, 1);	
	ia64_set_lrr1(0, 1);	

	__sti();		/* Interrupts have been off till now. */

	printk("SMP: CPU %d starting idle loop\n", smp_processor_id());

	cpu_idle(NULL);
}

/*
 * Create the idle task for a new AP.  DO NOT use kernel_thread() because
 * that could end up calling schedule() in the ia64_leave_kernel exit
 * path in which case the new idle task could get scheduled before we
 * had a chance to remove it from the run-queue...
 */
static int __init 
fork_by_hand(void)
{
	/*
	 * Don't care about the usp and regs settings since we'll never
	 * reschedule the forked task.
	 */
	return do_fork(CLONE_VM|CLONE_PID, 0, 0);
}

/*
 * Bring one cpu online.
 *
 * NB: cpuid is the CPU BUS-LOCAL ID, not the entire SAPIC ID.  See asm/smp.h.
 */
static int __init
smp_boot_one_cpu(int cpuid, int cpunum)
{
	struct task_struct *idle;
	long timeout;

	/* 
	 * Create an idle task for this CPU.  Note that the address we
	 * give to kernel_thread is irrelevant -- it's going to start
	 * where OS_BOOT_RENDEVZ vector in SAL says to start.  But
	 * this gets all the other task-y sort of data structures set
	 * up like we wish.   We need to pull the just created idle task 
	 * off the run queue and stuff it into the init_tasks[] array.  
	 * Sheesh . . .
	 */
	if (fork_by_hand() < 0) 
		panic("failed fork for CPU %d", cpuid);
	/*
	 * We remove it from the pidhash and the runqueue
	 * once we got the process:
	 */
	idle = init_task.prev_task;
	if (!idle)
		panic("No idle process for CPU %d", cpuid);
	init_tasks[cpunum] = idle;
	del_from_runqueue(idle);
        unhash_process(idle);

	/* Schedule the first task manually.  */
	idle->processor = cpuid;
	idle->has_cpu = 1;

	/* Let _start know what logical CPU we're booting (offset into init_tasks[] */
	cpu_now_booting = cpunum;
	
	/* Kick the AP in the butt */
	ipi_send(cpuid, ap_wakeup_vector, IA64_IPI_DM_INT, 0);
	ia64_srlz_i();
	mb();

	/* 
	 * OK, wait a bit for that CPU to finish staggering about.  smp_callin() will
	 * call cpu_init() which will set a bit for this AP.  When that bit flips, the AP
	 * is waiting for smp_threads_ready to be 1 and we can move on.
	 */
	for (timeout = 0; timeout < 100000; timeout++) {
		if (test_bit(cpuid, &cpu_online_map))
			goto alive;
		udelay(100);
		barrier();
	}

	printk(KERN_ERR "SMP: Processor %d is stuck.\n", cpuid);
	return 0;

alive:
	/* Remember the AP data */
	__cpu_number_map[cpuid] = cpunum;
	__cpu_logical_map[cpunum] = cpuid;
	return 1;
}



/*
 * Called by smp_init bring all the secondaries online and hold them.  
 * XXX: this is ACPI specific; it uses "magic" variables exported from acpi.c 
 *      to 'discover' the AP's.  Blech.
 */
void __init
smp_boot_cpus(void)
{
	int i, cpu_count = 1;
	unsigned long bogosum;

	/* Take care of some initial bookkeeping.  */
	memset(&__cpu_number_map, -1, sizeof(__cpu_number_map));
	memset(&__cpu_logical_map, -1, sizeof(__cpu_logical_map));
	memset(&ipi_op, 0, sizeof(ipi_op));

	/* Setup BSP mappings */
	__cpu_number_map[bootstrap_processor] = 0;
	__cpu_logical_map[0] = bootstrap_processor;
	current->processor = bootstrap_processor;

	/* Mark BSP booted and get active_mm context */
	cpu_init();

	/* reset XTP for interrupt routing */
	normal_xtp();

	/* And generate an entry in cpu_data */
	smp_store_cpu_info(bootstrap_processor);
#if 0
	smp_tune_scheduling();
#endif
 	smp_setup_percpu_timer(bootstrap_processor);

	if (test_and_set_bit(bootstrap_processor, &cpu_online_map)) {
		printk("CPU#%d already initialized!\n", smp_processor_id());
		machine_halt();
	}  
	init_idle();

	/* Nothing to do when told not to.  */
	if (max_cpus == 0) {
	        printk(KERN_INFO "SMP mode deactivated.\n");
		return;
	}

	if (max_cpus != -1) 
		printk("Limiting CPUs to %d\n", max_cpus);

	if (smp.cpu_count > 1) {
		printk(KERN_INFO "SMP: starting up secondaries.\n");

		for (i = 0; i < NR_CPUS; i++) {
			if (smp.cpu_map[i] == -1 || 
			    smp.cpu_map[i] == bootstrap_processor)
				continue;

			if (smp_boot_one_cpu(smp.cpu_map[i], cpu_count) == 0)
				continue;

			cpu_count++; /* Count good CPUs only... */
			/* 
			 * Bail if we've started as many CPUS as we've been told to.
			 */
			if (cpu_count == max_cpus)
				break;
		}
	}

	if (cpu_count == 1) {
		printk(KERN_ERR "SMP: Bootstrap processor only.\n");
	}

	bogosum = 0;
        for (i = 0; i < NR_CPUS; i++) {
		if (cpu_online_map & (1L << i))
			bogosum += cpu_data[i].loops_per_sec;
        }

	printk(KERN_INFO "SMP: Total of %d processors activated "
	       "(%lu.%02lu BogoMIPS).\n",
	       cpu_count, (bogosum + 2500) / 500000,
	       ((bogosum + 2500) / 5000) % 100);

	smp_num_cpus = cpu_count;
}

/* 
 * Called from main.c by each AP.
 */
void __init 
smp_commence(void)
{
	mb();
}

/*
 * Not used; part of the i386 bringup
 */
void __init 
initialize_secondary(void)
{
}

int __init
setup_profiling_timer(unsigned int multiplier)
{
        return -EINVAL;
}

/*
 * Assume that CPU's have been discovered by some platform-dependant
 * interface.  For SoftSDV/Lion, that would be ACPI.
 *
 * Setup of the IPI irq handler is done in irq.c:init_IRQ_SMP().
 *
 * So this just gets the BSP SAPIC ID and print's it out.  Dull, huh?
 *
 * Not anymore.  This also registers the AP OS_MC_REDVEZ address with SAL.
 */
void __init
init_smp_config(void)
{
	struct fptr {
		unsigned long fp;
		unsigned long gp;
	} *ap_startup;
	long sal_ret;

	/* Grab the BSP ID */
	bootstrap_processor = hard_smp_processor_id();

	/* Tell SAL where to drop the AP's.  */
	ap_startup = (struct fptr *) start_ap;
	sal_ret = ia64_sal_set_vectors(SAL_VECTOR_OS_BOOT_RENDEZ,
				       __pa(ap_startup->fp), __pa(ap_startup->gp), 0, 
				       0, 0, 0);
	if (sal_ret < 0) {
		printk("SMP: Can't set SAL AP Boot Rendezvous: %s\n", ia64_sal_strerror(sal_ret));
		printk("     Forcing UP mode\n");
		max_cpus = 0;
		smp_num_cpus = 1; 
	}

}
