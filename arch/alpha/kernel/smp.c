/*
 *	linux/arch/alpha/kernel/smp.c
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/tasks.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/hwrpb.h>
#include <asm/ptrace.h>
#include <asm/atomic.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/spinlock.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>

#define __KERNEL_SYSCALLS__
#include <asm/unistd.h>

#include "proto.h"
#include "irq.h"


#define DEBUG_SMP 0
#if DEBUG_SMP
#define DBGS(args)	printk args
#else
#define DBGS(args)
#endif

/* A collection of per-processor data.  */
struct cpuinfo_alpha cpu_data[NR_CPUS];

/* A collection of single bit ipi messages.  */
static struct {
	unsigned long bits __cacheline_aligned;
} ipi_data[NR_CPUS];

enum ipi_message_type {
        IPI_RESCHEDULE,
        IPI_CALL_FUNC,
        IPI_CPU_STOP,
};

spinlock_t kernel_flag __cacheline_aligned = SPIN_LOCK_UNLOCKED;

/* Set to a secondary's cpuid when it comes online.  */
static unsigned long smp_secondary_alive;

unsigned long cpu_present_mask;	/* Which cpus ids came online.  */

static int max_cpus = -1;	/* Command-line limitation.  */
int smp_boot_cpuid;		/* Which processor we booted from.  */
int smp_num_probed;		/* Internal processor count */
int smp_num_cpus = 1;		/* Number that came online.  */
int smp_threads_ready;		/* True once the per process idle is forked. */
cycles_t cacheflush_time;

int cpu_number_map[NR_CPUS];
int __cpu_logical_map[NR_CPUS];

extern void calibrate_delay(void);
extern asmlinkage void entInt(void);


/*
 * Process bootcommand SMP options, like "nosmp" and "maxcpus=".
 */
void __init
smp_setup(char *str, int *ints)
{
	if (ints && ints[0] > 0)
		max_cpus = ints[1];
	else
		max_cpus = 0;
}

/*
 * Called by both boot and secondaries to move global data into
 *  per-processor storage.
 */
static inline void __init
smp_store_cpu_info(int cpuid)
{
	cpu_data[cpuid].loops_per_sec = loops_per_sec;
}

/*
 * Ideally sets up per-cpu profiling hooks.  Doesn't do much now...
 */
static inline void __init
smp_setup_percpu_timer(int cpuid)
{
	cpu_data[cpuid].prof_counter = 1;
	cpu_data[cpuid].prof_multiplier = 1;

#ifdef NOT_YET_PROFILING
	load_profile_irq(mid_xlate[cpu], lvl14_resolution);
	if (cpu == smp_boot_cpuid)
		enable_pil_irq(14);
#endif
}

/*
 * Where secondaries begin a life of C.
 */
void __init
smp_callin(void)
{
	int cpuid = hard_smp_processor_id();

	DBGS(("CALLIN %d state 0x%lx\n", cpuid, current->state));

	/* Turn on machine checks.  */
	wrmces(7);

	/* Set trap vectors.  */
	trap_init();

	/* Set interrupt vector.  */
	wrent(entInt, 0);

	/* Setup the scheduler for this processor.  */
	init_idle();

	/* Get our local ticker going. */
	smp_setup_percpu_timer(cpuid);

	/* Must have completely accurate bogos.  */
	__sti();
	calibrate_delay();
	smp_store_cpu_info(cpuid);

	/* Allow master to continue. */
	wmb();
	smp_secondary_alive = cpuid;

	/* Wait for the go code.  */
	while (!smp_threads_ready)
		barrier();

	printk(KERN_INFO "SMP: commencing CPU %d current %p\n",
	       cpuid, current);

	/* Do nothing.  */
	cpu_idle(NULL);
}


/*
 * Rough estimation for SMP scheduling, this is the number of cycles it
 * takes for a fully memory-limited process to flush the SMP-local cache.
 *
 * We are not told how much cache there is, so we have to guess.
 */
static void __init
smp_tune_scheduling (void)
{
	struct percpu_struct *cpu;
	unsigned long on_chip_cache;
	unsigned long freq;

	cpu = (struct percpu_struct*)((char*)hwrpb + hwrpb->processor_offset);
	switch (cpu->type)
	{
	case EV45_CPU:
		on_chip_cache = 16 + 16;
		break;

	case EV5_CPU:
	case EV56_CPU:
		on_chip_cache = 8 + 8 + 96;
		break;

	case PCA56_CPU:
		on_chip_cache = 16 + 8;
		break;

	case EV6_CPU:
		on_chip_cache = 64 + 64;
		break;

	default:
		on_chip_cache = 8 + 8;
		break;
	}

	freq = hwrpb->cycle_freq ? : est_cycle_freq;

	/* Magic estimation stolen from x86 port.  */
	cacheflush_time = freq / 1024 * on_chip_cache / 5000;
}

/*
 * Send a message to a secondary's console.  "START" is one such
 * interesting message.  ;-)
 */
static void
send_secondary_console_msg(char *str, int cpuid)
{
	struct percpu_struct *cpu;
	register char *cp1, *cp2;
	unsigned long cpumask;
	size_t len;
	long timeout;

	cpu = (struct percpu_struct *)
		((char*)hwrpb
		 + hwrpb->processor_offset
		 + cpuid * hwrpb->processor_size);

	cpumask = (1L << cpuid);
	if (hwrpb->txrdy & cpumask)
		goto delay1;
	ready1:

	cp2 = str;
	len = strlen(cp2);
	*(unsigned int *)&cpu->ipc_buffer[0] = len;
	cp1 = (char *) &cpu->ipc_buffer[1];
	memcpy(cp1, cp2, len);

	/* atomic test and set */
	wmb();
	set_bit(cpuid, &hwrpb->rxrdy);

	if (hwrpb->txrdy & cpumask)
		goto delay2;
	ready2:
	return;

delay1:
	/* Wait one second.  Note that jiffies aren't ticking yet.  */
	for (timeout = 100000; timeout > 0; --timeout) {
		if (!(hwrpb->txrdy & cpumask))
			goto ready1;
		udelay(10);
		barrier();
	}
	goto timeout;

delay2:
	/* Wait one second.  */
	for (timeout = 100000; timeout > 0; --timeout) {
		if (!(hwrpb->txrdy & cpumask))
			goto ready2;
		udelay(10);
		barrier();
	}
	goto timeout;

timeout:
	printk("Processor %x not ready\n", cpuid);
	return;
}

/*
 * A secondary console wants to send a message.  Receive it.
 */
static void
recv_secondary_console_msg(void)
{
	int mycpu, i, cnt;
	unsigned long txrdy = hwrpb->txrdy;
	char *cp1, *cp2, buf[80];
	struct percpu_struct *cpu;

	DBGS(("recv_secondary_console_msg: TXRDY 0x%lx.\n", txrdy));

	mycpu = hard_smp_processor_id();

	for (i = 0; i < NR_CPUS; i++) {
		if (!(txrdy & (1L << i)))
			continue;

		DBGS(("recv_secondary_console_msg: "
		      "TXRDY contains CPU %d.\n", i));

		cpu = (struct percpu_struct *)
		  ((char*)hwrpb
		   + hwrpb->processor_offset
		   + i * hwrpb->processor_size);

 		printk(KERN_INFO "recv_secondary_console_msg: on %d from %d"
		       " HALT_REASON 0x%lx FLAGS 0x%lx\n",
		       mycpu, i, cpu->halt_reason, cpu->flags);

		cnt = cpu->ipc_buffer[0] >> 32;
		if (cnt <= 0 || cnt >= 80)
			strcpy(buf, "<<< BOGUS MSG >>>");
		else {
			cp1 = (char *) &cpu->ipc_buffer[11];
			cp2 = buf;
			strcpy(cp2, cp1);
			
			while ((cp2 = strchr(cp2, '\r')) != 0) {
				*cp2 = ' ';
				if (cp2[1] == '\n')
					cp2[1] = ' ';
			}
		}

		printk(KERN_INFO "recv_secondary_console_msg: on %d "
		       "message is '%s'\n", mycpu, buf);
	}

	hwrpb->txrdy = 0;
}

/*
 * Convince the console to have a secondary cpu begin execution.
 */
static int __init
secondary_cpu_start(int cpuid, struct task_struct *idle)
{
	struct percpu_struct *cpu;
	struct pcb_struct *hwpcb;
	long timeout;
	  
	cpu = (struct percpu_struct *)
		((char*)hwrpb
		 + hwrpb->processor_offset
		 + cpuid * hwrpb->processor_size);
	hwpcb = (struct pcb_struct *) cpu->hwpcb;

	/* Initialize the CPU's HWPCB to something just good enough for
	   us to get started.  Immediately after starting, we'll swpctx
	   to the target idle task's tss.  Reuse the stack in the mean
	   time.  Precalculate the target PCBB.  */
	hwpcb->ksp = (unsigned long) idle + sizeof(union task_union) - 16;
	hwpcb->usp = 0;
	hwpcb->ptbr = idle->tss.ptbr;
	hwpcb->pcc = 0;
	hwpcb->asn = 0;
	hwpcb->unique = virt_to_phys(&idle->tss);
	hwpcb->flags = idle->tss.pal_flags;
	hwpcb->res1 = hwpcb->res2 = 0;

	DBGS(("KSP 0x%lx PTBR 0x%lx VPTBR 0x%lx UNIQUE 0x%lx\n",
	      hwpcb->ksp, hwpcb->ptbr, hwrpb->vptb, hwcpb->unique));
	DBGS(("Starting secondary cpu %d: state 0x%lx pal_flags 0x%lx\n",
	      cpuid, idle->state, idle->tss.pal_flags));

	/* Setup HWRPB fields that SRM uses to activate secondary CPU */
	hwrpb->CPU_restart = __smp_callin;
	hwrpb->CPU_restart_data = (unsigned long) __smp_callin;

	/* Recalculate and update the HWRPB checksum */
	hwrpb_update_checksum(hwrpb);

	/*
	 * Send a "start" command to the specified processor.
	 */

	/* SRM III 3.4.1.3 */
	cpu->flags |= 0x22;	/* turn on Context Valid and Restart Capable */
	cpu->flags &= ~1;	/* turn off Bootstrap In Progress */
	wmb();

	send_secondary_console_msg("START\r\n", cpuid);

	/* Wait 1 second for an ACK from the console.  Note that jiffies 
	   aren't ticking yet.  */
	for (timeout = 100000; timeout > 0; timeout--) {
		if (cpu->flags & 1)
			goto started;
		udelay(10);
		barrier();
	}
	printk(KERN_ERR "SMP: Processor %d failed to start.\n", cpuid);
	return -1;

started:
	DBGS(("secondary_cpu_start: SUCCESS for CPU %d!!!\n", cpuid));
	return 0;
}

/*
 * Bring one cpu online.
 */
static int __init
smp_boot_one_cpu(int cpuid, int cpunum)
{
	struct task_struct *idle;
	long timeout;

	/* Cook up an idler for this guy.  Note that the address we give
	   to kernel_thread is irrelevant -- it's going to start where
	   HWRPB.CPU_restart says to start.  But this gets all the other
	   task-y sort of data structures set up like we wish.  */
	kernel_thread((void *)__smp_callin, NULL, CLONE_PID|CLONE_VM);
	idle = task[cpunum];
	if (!idle)
		panic("No idle process for CPU %d", cpuid);
	idle->processor = cpuid;

	/* Schedule the first task manually.  */
	/* ??? Ingo, what is this?  */
	idle->has_cpu = 1;

	DBGS(("smp_boot_one_cpu: CPU %d state 0x%lx flags 0x%lx\n",
	      cpuid, idle->state, idle->flags));

	/* The secondary will change this once it is happy.  Note that
	   secondary_cpu_start contains the necessary memory barrier.  */
	smp_secondary_alive = -1;

	/* Whirrr, whirrr, whirrrrrrrrr... */
	if (secondary_cpu_start(cpuid, idle))
		return -1;

	/* We've been acked by the console; wait one second for the task
	   to start up for real.  Note that jiffies aren't ticking yet.  */
	for (timeout = 0; timeout < 100000; timeout++) {
		if (smp_secondary_alive != -1)
			goto alive;
		udelay(10);
		barrier();
	}

	printk(KERN_ERR "SMP: Processor %d is stuck.\n", cpuid);
	return -1;

alive:
	/* Another "Red Snapper". */
	cpu_number_map[cpuid] = cpunum;
	__cpu_logical_map[cpunum] = cpuid;
	return 0;
}

/*
 * Called from setup_arch.  Detect an SMP system and which processors
 * are present.
 */
void __init
setup_smp(void)
{
	struct percpu_struct *cpubase, *cpu;
	int i;

	smp_boot_cpuid = hard_smp_processor_id();
	if (smp_boot_cpuid != 0) {
		printk(KERN_WARNING "SMP: Booting off cpu %d instead of 0?\n",
		       smp_boot_cpuid);
	}

	if (hwrpb->nr_processors > 1) {
		int boot_cpu_palrev;

		DBGS(("setup_smp: nr_processors %ld\n",
		      hwrpb->nr_processors));

		cpubase = (struct percpu_struct *)
			((char*)hwrpb + hwrpb->processor_offset);
		boot_cpu_palrev = cpubase->pal_revision;

		for (i = 0; i < hwrpb->nr_processors; i++ ) {
			cpu = (struct percpu_struct *)
				((char *)cpubase + i*hwrpb->processor_size);
			if ((cpu->flags & 0x1cc) == 0x1cc) {
				smp_num_probed++;
				/* Assume here that "whami" == index */
				cpu_present_mask |= (1L << i);
				cpu->pal_revision = boot_cpu_palrev;
			}

			DBGS(("setup_smp: CPU %d: flags 0x%lx type 0x%lx\n",
			      i, cpu->flags, cpu->type));
			DBGS(("setup_smp: CPU %d: PAL rev 0x%lx\n",
			      i, cpu->pal_revision));
		}
	} else {
		smp_num_probed = 1;
		cpu_present_mask = (1L << smp_boot_cpuid);
	}

	printk(KERN_INFO "SMP: %d CPUs probed -- cpu_present_mask = %lx\n",
	       smp_num_probed, cpu_present_mask);
}

/*
 * Called by smp_init bring all the secondaries online and hold them.
 */
void __init
smp_boot_cpus(void)
{
	int cpu_count, i;
	unsigned long bogosum;

	/* Take care of some initial bookkeeping.  */
	memset(cpu_number_map, -1, sizeof(cpu_number_map));
	memset(__cpu_logical_map, -1, sizeof(__cpu_logical_map));
	memset(ipi_data, 0, sizeof(ipi_data));

	cpu_number_map[smp_boot_cpuid] = 0;
	__cpu_logical_map[0] = smp_boot_cpuid;
	current->processor = smp_boot_cpuid;

	smp_store_cpu_info(smp_boot_cpuid);
	smp_tune_scheduling();
	smp_setup_percpu_timer(smp_boot_cpuid);

	init_idle();

	/* Nothing to do on a UP box, or when told not to.  */
	if (smp_num_probed == 1 || max_cpus == 0) {
	        printk(KERN_INFO "SMP mode deactivated.\n");
		return;
	}

	printk(KERN_INFO "SMP starting up secondaries.\n");

	cpu_count = 1;
	for (i = 0; i < NR_CPUS; i++) {
		if (i == smp_boot_cpuid)
			continue;

	        if (((cpu_present_mask >> i) & 1) == 0)
			continue;

		if (smp_boot_one_cpu(i, cpu_count))
			continue;

		cpu_count++;
	}

	if (cpu_count == 1) {
		printk(KERN_ERR "SMP: Only one lonely processor alive.\n");
		return;
	}

	bogosum = 0;
        for (i = 0; i < NR_CPUS; i++) {
		if (cpu_present_mask & (1L << i))
			bogosum += cpu_data[i].loops_per_sec;
        }
	printk(KERN_INFO "SMP: Total of %d processors activated "
	       "(%lu.%02lu BogoMIPS).\n",
	       cpu_count, (bogosum + 2500) / 500000,
	       ((bogosum + 2500) / 5000) % 100);

	smp_num_cpus = cpu_count;
}

/*
 * Called by smp_init to release the blocking online cpus once they 
 * are all started.
 */
void __init
smp_commence(void)
{
	/* smp_init sets smp_threads_ready -- that's enough.  */
	mb();
}

/*
 * Only broken Intel needs this, thus it should not even be
 * referenced globally.
 */

void __init
initialize_secondary(void)
{
}


extern void update_one_process(struct task_struct *p, unsigned long ticks,
	                       unsigned long user, unsigned long system,
			       int cpu);

void
smp_percpu_timer_interrupt(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	int user = user_mode(regs);
	struct cpuinfo_alpha *data = &cpu_data[cpu];

#ifdef NOT_YET_PROFILING
	clear_profile_irq(mid_xlate[cpu]);
	if (!user)
		alpha_do_profile(regs->pc);
#endif

	if (!--data->prof_counter) {
		/* We need to make like a normal interrupt -- otherwise
		   timer interrupts ignore the global interrupt lock,
		   which would be a Bad Thing.  */
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

int __init
setup_profiling_timer(unsigned int multiplier)
{
#ifdef NOT_YET_PROFILING
	int i;
	unsigned long flags;

	/* Prevent level14 ticker IRQ flooding. */
	if((!multiplier) || (lvl14_resolution / multiplier) < 500)
	        return -EINVAL;

	save_and_cli(flags);
	for (i = 0; i < NR_CPUS; i++) {
	        if (cpu_present_mask & (1L << i)) {
	                load_profile_irq(mid_xlate[i],
					 lvl14_resolution / multiplier);
	                prof_multiplier[i] = multiplier;
	        }
	}
	restore_flags(flags);

	return 0;
#else
	return -EINVAL;
#endif
}


static void
send_ipi_message(unsigned long to_whom, enum ipi_message_type operation)
{
	long i, j;

	/* Reduce the number of memory barriers by doing two loops,
	   one to set the bits, one to invoke the interrupts.  */

	mb();	/* Order out-of-band data and bit setting. */

	for (i = 0, j = 1; i < NR_CPUS; ++i, j <<= 1) {
		if (to_whom & j)
			set_bit(operation, &ipi_data[i].bits);
	}

	mb();	/* Order bit setting and interrupt. */

	for (i = 0, j = 1; i < NR_CPUS; ++i, j <<= 1) {
		if (to_whom & j)
			wripir(i);
	}
}

/* Structure and data for smp_call_function.  This is designed to 
   minimize static memory requirements.  Plus it looks cleaner.  */

struct smp_call_struct {
	void (*func) (void *info);
	void *info;
	long wait;
	atomic_t unstarted_count;
	atomic_t unfinished_count;
};

static struct smp_call_struct *smp_call_function_data;

/* Atomicly drop data into a shared pointer.  The pointer is free if
   it is initially locked.  If retry, spin until free.  */

static inline int
pointer_lock (void *lock, void *data, int retry)
{
	void *old, *tmp;

	mb();
again:
	/* Compare and swap with zero.  */
	asm volatile (
	"1:	ldq_l	%0,%1\n"
	"	mov	%3,%2\n"
	"	bne	%0,2f\n"
	"	stq_c	%2,%1\n"
	"	beq	%2,1b\n"
	"2:"
	: "=&r"(old), "=m"(*(void **)lock), "=&r"(tmp)
	: "r"(data)
	: "memory");

	if (old == 0)
		return 0;
	if (! retry)
		return -EBUSY;

	while (*(void **)lock)
		schedule();
	goto again;
}

void
handle_ipi(struct pt_regs *regs)
{
	int this_cpu = smp_processor_id();
	unsigned long *pending_ipis = &ipi_data[this_cpu].bits;
	unsigned long ops;

	DBGS(("handle_ipi: on CPU %d ops 0x%x PC 0x%lx\n",
	      this_cpu, *pending_ipis, regs->pc));

	mb();	/* Order interrupt and bit testing. */
	while ((ops = xchg(pending_ipis, 0)) != 0) {
	  mb();	/* Order bit clearing and data access. */
	  do {
		unsigned long which;

		which = ops & -ops;
		ops &= ~which;
		which = ffz(~which);

		if (which == IPI_RESCHEDULE) {
			/* Reschedule callback.  Everything to be done
			   is done by the interrupt return path.  */
		}
		else if (which == IPI_CALL_FUNC) {
			struct smp_call_struct *data;
			void (*func)(void *info);
			void *info;
			int wait;

			data = smp_call_function_data;
			func = data->func;
			info = data->info;
			wait = data->wait;

			/* Notify the sending CPU that the data has been
			   received, and execution is about to begin.  */
			mb();
			atomic_dec (&data->unstarted_count);

			/* At this point the structure may be gone unless
			   wait is true.  */
			(*func)(info);

			/* Notify the sending CPU that the task is done.  */
			mb();
			if (wait) atomic_dec (&data->unfinished_count);
		}
		else if (which == IPI_CPU_STOP) {
			halt();
		}
		else {
			printk(KERN_CRIT "Unknown IPI on CPU %d: %lu\n",
			       this_cpu, which);
		}
	  } while (ops);

	  mb();	/* Order data access and bit testing. */
	}

	cpu_data[this_cpu].ipi_count++;

	if (hwrpb->txrdy)
		recv_secondary_console_msg();
}

void
smp_send_reschedule(int cpu)
{
	send_ipi_message(1L << cpu, IPI_RESCHEDULE);
}

void
smp_send_stop(void)
{
	unsigned long to_whom = cpu_present_mask ^ (1L << smp_processor_id());
	send_ipi_message(to_whom, IPI_CPU_STOP);
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
	unsigned long to_whom = cpu_present_mask ^ (1L << smp_processor_id());
	struct smp_call_struct data;
	long timeout;
	
	data.func = func;
	data.info = info;
	data.wait = wait;
	atomic_set(&data.unstarted_count, smp_num_cpus - 1);
	atomic_set(&data.unfinished_count, smp_num_cpus - 1);

	/* Aquire the smp_call_function_data mutex.  */
	if (pointer_lock(&smp_call_function_data, &data, retry))
		return -EBUSY;

	/* Send a message to all other CPUs.  */
	send_ipi_message(to_whom, IPI_CALL_FUNC);

	/* Wait for a minimal response.  */
	timeout = jiffies + HZ;
	while (atomic_read (&data.unstarted_count) > 0
	       && time_before (jiffies, timeout))
		barrier();

	/* We either got one or timed out -- clear the lock.  */
	mb();
	smp_call_function_data = 0;
	if (atomic_read (&data.unstarted_count) > 0)
		return -ETIMEDOUT;

	/* Wait for a complete response, if needed.  */
	if (wait) {
		while (atomic_read (&data.unfinished_count) > 0)
			barrier();
	}

	return 0;
}

static void
ipi_flush_tlb_all(void *ignored)
{
	tbia();
}

void
flush_tlb_all(void)
{
	tbia();

	/* Although we don't have any data to pass, we do want to
	   synchronize with the other processors.  */
	if (smp_call_function(ipi_flush_tlb_all, NULL, 1, 1)) {
		printk(KERN_CRIT "flush_tlb_all: timed out\n");
	}
}

static void
ipi_flush_tlb_mm(void *x)
{
	struct mm_struct *mm = (struct mm_struct *) x;
	if (mm == current->mm)
		flush_tlb_current(mm);
}

void
flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->mm)
		flush_tlb_current(mm);
	else
		flush_tlb_other(mm);

	if (smp_call_function(ipi_flush_tlb_mm, mm, 1, 1)) {
		printk(KERN_CRIT "flush_tlb_mm: timed out\n");
	}
}

struct flush_tlb_page_struct {
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long addr;
};

static void
ipi_flush_tlb_page(void *x)
{
	struct flush_tlb_page_struct *data = (struct flush_tlb_page_struct *)x;
	if (data->mm == current->mm)
		flush_tlb_current_page(data->mm, data->vma, data->addr);
}

void
flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	struct flush_tlb_page_struct data;
	struct mm_struct *mm = vma->vm_mm;

	data.vma = vma;
	data.mm = mm;
	data.addr = addr;

	if (mm == current->mm)
		flush_tlb_current_page(mm, vma, addr);
	else
		flush_tlb_other(mm);
	
	if (smp_call_function(ipi_flush_tlb_page, &data, 1, 1)) {
		printk(KERN_CRIT "flush_tlb_page: timed out\n");
	}
}

void
flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	/* On the Alpha we always flush the whole user tlb.  */
	flush_tlb_mm(mm);
}


int
smp_info(char *buffer)
{
	long i;
	unsigned long sum = 0;
	for (i = 0; i < NR_CPUS; i++)
		sum += cpu_data[i].ipi_count;

	return sprintf(buffer, "CPUs probed %d active %d map 0x%lx IPIs %ld\n",
		       smp_num_probed, smp_num_cpus, cpu_present_mask, sum);
}


#if DEBUG_SPINLOCK

#ifdef MANAGE_SPINLOCK_IPL

static inline long 
spinlock_raise_ipl(spinlock_t * lock)
{
 	long min_ipl = lock->target_ipl;
	long last_ipl = swpipl(7);
	if (last_ipl < 7 && min_ipl < 7)
		setipl(min_ipl < last_ipl ? last_ipl : min_ipl);
	return last_ipl;
}

static inline void
spinlock_restore_ipl(long prev)
{
	setipl(prev);
}

#else

#define spinlock_raise_ipl(LOCK)	((void)(LOCK), 0)
#define spinlock_restore_ipl(PREV)	((void)(PREV))

#endif /* MANAGE_SPINLOCK_IPL */

void
spin_unlock(spinlock_t * lock)
{
	long old_ipl = lock->saved_ipl;
	mb();
	lock->lock = 0;
	spinlock_restore_ipl(old_ipl);
}

void
spin_lock(spinlock_t * lock)
{
	long tmp;
	long stuck;
	void *inline_pc = __builtin_return_address(0);
	unsigned long started = jiffies;
	int printed = 0;
	int cpu = smp_processor_id();
	long old_ipl = spinlock_raise_ipl(lock);

	stuck = 1L << 28;
 try_again:

	/* Use sub-sections to put the actual loop at the end
	   of this object file's text section so as to perfect
	   branch prediction.  */
	__asm__ __volatile__(
	"1:	ldl_l	%0,%1\n"
	"	subq	%2,1,%2\n"
	"	blbs	%0,2f\n"
	"	or	%0,1,%0\n"
	"	stl_c	%0,%1\n"
	"	beq	%0,3f\n"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"2:	ldl	%0,%1\n"
	"	subq	%2,1,%2\n"
	"3:	blt	%2,4b\n"
	"	blbs	%0,2b\n"
	"	br	1b\n"
	".previous"
	: "=r" (tmp), "=m" (__dummy_lock(lock)), "=r" (stuck)
	: "1" (__dummy_lock(lock)), "2" (stuck));

	if (stuck < 0) {
		printk(KERN_WARNING
		       "spinlock stuck at %p(%d) owner %s at %p(%d) st %ld\n",
		       inline_pc, cpu, lock->task->comm, lock->previous,
		       lock->task->processor, lock->task->state);
		stuck = 1L << 36;
		printed = 1;
		goto try_again;
	}

	/* Exiting.  Got the lock.  */
	lock->saved_ipl = old_ipl;
	lock->on_cpu = cpu;
	lock->previous = inline_pc;
	lock->task = current;

	if (printed) {
		printk(KERN_WARNING "spinlock grabbed at %p(%d) %ld ticks\n",
		       inline_pc, cpu, jiffies - started);
	}
}

int
spin_trylock(spinlock_t * lock)
{
	long old_ipl = spinlock_raise_ipl(lock);
	int ret;
	if ((ret = !test_and_set_bit(0, lock))) {
		mb();
		lock->saved_ipl = old_ipl;
		lock->on_cpu = smp_processor_id();
		lock->previous = __builtin_return_address(0);
		lock->task = current;
	} else {
		spinlock_restore_ipl(old_ipl);
	}
	return ret;
}
#endif /* DEBUG_SPINLOCK */

#if DEBUG_RWLOCK
void write_lock(rwlock_t * lock)
{
	long regx, regy;
	int stuck_lock, stuck_reader;
	void *inline_pc = __builtin_return_address(0);

 try_again:

	stuck_lock = 1<<26;
	stuck_reader = 1<<26;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0\n"
	"	blbs	%1,6f\n"
	"	blt	%1,8f\n"
	"	mov	1,%1\n"
	"	stl_c	%1,%0\n"
	"	beq	%1,6f\n"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"6:	blt	%3,4b	# debug\n"
	"	subl	%3,1,%3	# debug\n"
	"	ldl	%1,%0\n"
	"	blbs	%1,6b\n"
	"8:	blt	%4,4b	# debug\n"
	"	subl	%4,1,%4	# debug\n"
	"	ldl	%1,%0\n"
	"	blt	%1,8b\n"
	"	br	1b\n"
	".previous"
	: "=m" (__dummy_lock(lock)), "=&r" (regx), "=&r" (regy),
	  "=&r" (stuck_lock), "=&r" (stuck_reader)
	: "0" (__dummy_lock(lock)), "3" (stuck_lock), "4" (stuck_reader));

	if (stuck_lock < 0) {
		printk(KERN_WARNING "write_lock stuck at %p\n", inline_pc);
		goto try_again;
	}
	if (stuck_reader < 0) {
		printk(KERN_WARNING "write_lock stuck on readers at %p\n",
		       inline_pc);
		goto try_again;
	}
}

void read_lock(rwlock_t * lock)
{
	long regx;
	int stuck_lock;
	void *inline_pc = __builtin_return_address(0);

 try_again:

	stuck_lock = 1<<26;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0;"
	"	blbs	%1,6f;"
	"	subl	%1,2,%1;"
	"	stl_c	%1,%0;"
	"	beq	%1,6f;"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"6:	ldl	%1,%0;"
	"	blt	%2,4b	# debug\n"
	"	subl	%2,1,%2	# debug\n"
	"	blbs	%1,6b;"
	"	br	1b\n"
	".previous"
	: "=m" (__dummy_lock(lock)), "=&r" (regx), "=&r" (stuck_lock)
	: "0" (__dummy_lock(lock)), "2" (stuck_lock));

	if (stuck_lock < 0) {
		printk(KERN_WARNING "read_lock stuck at %p\n", inline_pc);
		goto try_again;
	}
}
#endif /* DEBUG_RWLOCK */
