/*
 * $Id: smp.c,v 1.39 1998/12/28 10:28:51 paulus Exp $
 *
 * Smp support for ppc.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) borrowing a great
 * deal of code from the sparc and intel versions.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tasks.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/spinlock.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/init.h>
#include <asm/io.h>

#include "time.h"

int smp_threads_ready = 0;
volatile int smp_commenced = 0;
int smp_num_cpus = 1;
struct cpuinfo_PPC cpu_data[NR_CPUS];
struct klock_info_struct klock_info = { KLOCK_CLEAR, 0 };
volatile unsigned char active_kernel_processor = NO_PROC_ID;	/* Processor holding kernel spinlock		*/
volatile unsigned long ipi_count;
spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;
unsigned int prof_multiplier[NR_CPUS];
unsigned int prof_counter[NR_CPUS];
int first_cpu_booted = 0;
cycles_t cacheflush_time;

/* all cpu mappings are 1-1 -- Cort */
int cpu_number_map[NR_CPUS] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,};
volatile unsigned long cpu_callin_map[NR_CPUS] = {0,};

int start_secondary(void *);
extern int cpu_idle(void *unused);

void smp_local_timer_interrupt(struct pt_regs * regs)
{
	int cpu = smp_processor_id();
	extern void update_one_process(struct task_struct *,unsigned long,
				       unsigned long,unsigned long,int);
	if (!--prof_counter[cpu]) {
		int user=0,system=0;
		struct task_struct * p = current;

		/*
		 * After doing the above, we need to make like
		 * a normal interrupt - otherwise timer interrupts
		 * ignore the global interrupt lock, which is the
		 * WrongThing (tm) to do.
		 */

		if (user_mode(regs))
			user=1;
		else
			system=1;

		if (p->pid) {
			update_one_process(p, 1, user, system, cpu);

			p->counter -= 1;
			if (p->counter < 0) {
				p->counter = 0;
				current->need_resched = 1;
			}
			if (p->priority < DEF_PRIORITY) {
				kstat.cpu_nice += user;
				kstat.per_cpu_nice[cpu] += user;
			} else {
				kstat.cpu_user += user;
				kstat.per_cpu_user[cpu] += user;
			}

			kstat.cpu_system += system;
			kstat.per_cpu_system[cpu] += system;

		}
		prof_counter[cpu]=prof_multiplier[cpu];
	}
}

/*
 * Dirty hack to get smp message passing working.
 * Right now it only works for stop cpu's but will be setup
 * later for more general message passing.
 *
 * As it is now, if we're sending two message at the same time
 * we have race conditions.  I avoided doing locks here since
 * all that works right now is the stop cpu message.
 *
 *  -- Cort
 */
int smp_message[NR_CPUS];
void smp_message_recv(void)
{
	int msg = smp_message[smp_processor_id()];
	
	/* clear interrupt */
	*(volatile unsigned long *)(0xf80000c0) = ~0L;
	eieio();
	
	/* make sure msg is for us */
	if ( msg == -1 ) return;

	ipi_count++;
	/*printk("SMP %d: smp_message_recv() msg %x\n", smp_processor_id(),msg);*/
	
	switch( msg )
	{
	case MSG_STOP_CPU:
		__cli();
		while (1) ;
		break;
	case MSG_RESCHEDULE: 
		current->need_resched = 1;
		break;
	case 0xf0f0: /* syncing time bases - just return */
		break;
	default:
		printk("SMP %d: smp_message_recv(): unknown msg %d\n",
		       smp_processor_id(), msg);
		break;
	}
	/* reset message */
	smp_message[smp_processor_id()] = -1;
}

void smp_send_reschedule(int cpu)
{
	/* This is only used if `cpu' is running an idle task,
	   so it will reschedule itself anyway... */
	/*smp_message_pass(cpu, MSG_RESCHEDULE, 0, 0);*/
}

void smp_send_stop(void)
{
	smp_message_pass(MSG_ALL_BUT_SELF, MSG_STOP_CPU, 0, 0);
}

spinlock_t mesg_pass_lock = SPIN_LOCK_UNLOCKED;
void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	if ( _machine != _MACH_Pmac )
		return;
printk("SMP %d: sending smp message %x\n", current->processor, msg);
if (smp_processor_id() ) printk("pass from cpu 1\n");
	spin_lock(&mesg_pass_lock);
#define OTHER (~smp_processor_id() & 1)
	
	switch( target )
	{
	case MSG_ALL:
		smp_message[smp_processor_id()] = msg;
		/* fall through */
	case MSG_ALL_BUT_SELF:
		smp_message[OTHER] = msg;
		break;
	default:
		smp_message[target] = msg;
		break;
	}
	/* interrupt secondary processor */
	*(volatile unsigned long *)(0xf80000c0) = ~0L;
	eieio();
	*(volatile unsigned long *)(0xf80000c0) = 0L;
	eieio();
	/* interrupt primary */
	/**(volatile unsigned long *)(0xf3019000);*/
	spin_unlock(&mesg_pass_lock);	
}

void __init smp_boot_cpus(void)
{
	extern struct task_struct *current_set[NR_CPUS];
	extern void __secondary_start(void);
	int i;
	struct task_struct *p;
	unsigned long a;

        printk("Entering SMP Mode...\n");
	
	first_cpu_booted = 1;
	/*dcbf(&first_cpu_booted);*/

	for (i = 0; i < NR_CPUS; i++) {
		prof_counter[i] = 1;
		prof_multiplier[i] = 1;
	}

	cpu_callin_map[0] = 1;
        smp_store_cpu_info(0);
        active_kernel_processor = 0;
	current->processor = 0;

	/*
	 * XXX very rough, assumes 20 bus cycles to read a cache line,
	 * timebase increments every 4 bus cycles, 32kB L1 data cache.
	 */
	cacheflush_time = 5 * 1024;

	if ( _machine != _MACH_Pmac )
	{
		printk("SMP not supported on this machine.\n");
		return;
	}
	
	/* create a process for second processor */
        kernel_thread(start_secondary, NULL, CLONE_PID);
	p = task[1];
	if ( !p )
		panic("No idle task for secondary processor\n");
	p->processor = 1;
	p->has_cpu = 1;
	current_set[1] = p;

	/* need to flush here since secondary bat's aren't setup */
	/* XXX ??? */
	for (a = KERNELBASE; a < KERNELBASE + 0x800000; a += 32)
		asm volatile("dcbf 0,%0" : : "r" (a) : "memory");
	asm volatile("sync");

	/*dcbf((void *)&current_set[1]);*/
	/* setup entry point of secondary processor */
	*(volatile unsigned long *)(0xf2800000) =
		(unsigned long)__secondary_start-KERNELBASE;
	eieio();
	/* interrupt secondary to begin executing code */
	*(volatile unsigned long *)(0xf80000c0) = ~0L;
	eieio();
	*(volatile unsigned long *)(0xf80000c0) = 0L;
	eieio();
	/*
	 * wait to see if the secondary made a callin (is actually up).
	 * udelay() isn't accurate here since we haven't yet called
	 * calibrate_delay() so use this value that I found through
	 * experimentation.  -- Cort
	 */
	for ( i = 1000; i && !cpu_callin_map[1] ; i-- )
		udelay(100);
	
	if(cpu_callin_map[1]) {
		printk("Processor %d found.\n", smp_num_cpus);
		smp_num_cpus++;
#if 1 /* this sync's the decr's, but we don't want this now -- Cort */
		set_dec(decrementer_count);
#endif
	} else {
		printk("Processor %d is stuck. \n", smp_num_cpus);
	}
	/* reset the entry point so if we get another intr we won't
	 * try to startup again */
	*(volatile unsigned long *)(0xf2800000) = 0x100;
	/* send interrupt to other processors to start decr's on all cpus */
	smp_message_pass(1,0xf0f0, 0, 0);
}

void __init smp_commence(void)
{
	printk("SMP %d: smp_commence()\n",current->processor);
	/*
	 *	Lets the callin's below out of their loop.
	 */
	smp_commenced = 1;
}

/* intel needs this */
void __init initialize_secondary(void)
{
}

/* Activate a secondary processor. */
asmlinkage int __init start_secondary(void *unused)
{
	printk("SMP %d: start_secondary()\n",current->processor);
	smp_callin();
	return cpu_idle(NULL);
}

void __init smp_callin(void)
{
	printk("SMP %d: smp_callin()\n",current->processor);
        smp_store_cpu_info(current->processor);
	set_dec(decrementer_count);
#if 0
	current->mm->mmap->vm_page_prot = PAGE_SHARED;
	current->mm->mmap->vm_start = PAGE_OFFSET;
	current->mm->mmap->vm_end = init_task.mm->mmap->vm_end;
#endif
	cpu_callin_map[current->processor] = 1;
	while(!smp_commenced)
		barrier();
	__sti();
	printk("SMP %d: smp_callin done\n", current->processor);
}

void __init smp_setup(char *str, int *ints)
{
	printk("SMP %d: smp_setup()\n",current->processor);
}

int __init setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

void __init smp_store_cpu_info(int id)
{
        struct cpuinfo_PPC *c = &cpu_data[id];

	/* assume bogomips are same for everything */
        c->loops_per_sec = loops_per_sec;
        c->pvr = _get_PVR();
}
