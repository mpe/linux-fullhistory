/*
 * $Id: smp.c,v 1.48 1999/03/16 10:40:32 cort Exp $
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
#include <linux/openpic.h>

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
#include <asm/prom.h>

#include "time.h"
int first_cpu_booted = 0;
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
cycles_t cacheflush_time;

/* all cpu mappings are 1-1 -- Cort */
int cpu_number_map[NR_CPUS] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,};
volatile unsigned long cpu_callin_map[NR_CPUS] = {0,};

int start_secondary(void *);
extern int cpu_idle(void *unused);
u_int openpic_read(volatile u_int *addr);

/* register for interrupting the secondary processor on the powersurge */
#define PSURGE_INTR	((volatile unsigned *)0xf80000c0)

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
 *
 * As it is now, if we're sending two message at the same time
 * we have race conditions.  The PowerSurge doesn't easily
 * allow us to send IPI messages so we put the messages in
 * smp_message[].
 *
 * This is because don't have several IPI's on the PowerSurge even though
 * we do on the chrp.  It would be nice to use the actual IPI's on the chrp
 * rather than this but having two methods of doing IPI isn't a good idea
 * right now.
 *  -- Cort
 */
int smp_message[NR_CPUS];
void smp_message_recv(void)
{
	int msg = smp_message[smp_processor_id()];

	if ( _machine == _MACH_Pmac )
	{
		/* clear interrupt */
		out_be32(PSURGE_INTR, ~0);
	}
	
	/* make sure msg is for us */
	if ( msg == -1 ) return;

	ipi_count++;
	
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
	int i;
	if ( !(_machine & (_MACH_Pmac|_MACH_chrp)) )
		return;

	spin_lock(&mesg_pass_lock);

	/*
	 * We assume here that the msg is not -1.  If it is,
	 * the recipient won't know the message was destined
	 * for it. -- Cort
	 */
	
	switch( target )
	{
	case MSG_ALL:
		smp_message[smp_processor_id()] = msg;
		/* fall through */
	case MSG_ALL_BUT_SELF:
		for ( i = 0 ; i < smp_num_cpus ; i++ )
			if ( i != smp_processor_id () )
				smp_message[i] = msg;
		break;
	default:
		smp_message[target] = msg;
		break;
	}
	
	if ( _machine == _MACH_Pmac )
	{
		/* interrupt secondary processor */
		out_be32(PSURGE_INTR, ~0);
		out_be32(PSURGE_INTR, 0);
		/*
		 * Assume for now that the secondary doesn't send
		 * IPI's -- Cort
		 */
		/* interrupt primary */
		/**(volatile unsigned long *)(0xf3019000);*/
	}
	
	if ( _machine == _MACH_chrp )
	{
		/*
		 * There has to be some way of doing this better -
		 * perhaps a sent-to-all or send-to-all-but-self
		 * in the openpic.  This gets us going for now, though.
		 * -- Cort
		 */
		switch ( target )
		{
		case MSG_ALL:
			for ( i = 0 ; i < smp_num_cpus ; i++ )
				openpic_cause_IPI(i, 0, 0xffffffff );
			break;
		case MSG_ALL_BUT_SELF:
			for ( i = 0 ; i < smp_num_cpus ; i++ )
				if ( i != smp_processor_id () )
					openpic_cause_IPI(i, 0,
			  0xffffffff & ~(1 << smp_processor_id()));
			break;
		default:
			openpic_cause_IPI(target, 0, 1U << target);
			break;
		}
	}
	
	spin_unlock(&mesg_pass_lock);
}

void __init smp_boot_cpus(void)
{
	extern struct task_struct *current_set[NR_CPUS];
	extern void __secondary_start_psurge(void);
	int i;
	struct task_struct *p;
	unsigned long a;

        printk("Entering SMP Mode...\n");
	/* let other processors know to not do certain initialization */
	first_cpu_booted = 1;
	
	/*
	 * assume for now that the first cpu booted is
	 * cpu 0, the master -- Cort
	 */
	cpu_callin_map[0] = 1;
        smp_store_cpu_info(0);
        active_kernel_processor = 0;
	current->processor = 0;

	for (i = 0; i < NR_CPUS; i++) {
		prof_counter[i] = 1;
		prof_multiplier[i] = 1;
	}

	/*
	 * XXX very rough, assumes 20 bus cycles to read a cache line,
	 * timebase increments every 4 bus cycles, 32kB L1 data cache.
	 */
	cacheflush_time = 5 * 1024;

	if ( !(_machine & (_MACH_Pmac|_MACH_chrp)) )
	{
		printk("SMP not supported on this machine.\n");
		return;
	}
	
	switch ( _machine )
	{
	case _MACH_Pmac:
		/* assume powersurge board - 2 processors -- Cort */
		smp_num_cpus = 2; 
		break;
	case _MACH_chrp:
		smp_num_cpus = ((openpic_read(&OpenPIC->Global.Feature_Reporting0)
				 & OPENPIC_FEATURE_LAST_PROCESSOR_MASK) >>
				OPENPIC_FEATURE_LAST_PROCESSOR_SHIFT)+1;
		/* get our processor # - we may not be cpu 0 */
		printk("SMP %d processors, boot CPU is %d (should be 0)\n",
		       smp_num_cpus,
		       10/*openpic_read(&OpenPIC->Processor[0]._Who_Am_I)*/);
		break;
	}

	/*
	 * only check for cpus we know exist.  We keep the callin map
	 * with cpus at the bottom -- Cort
	 */
	for ( i = 1 ; i < smp_num_cpus; i++ )
	{
		int c;
		
		/* create a process for the processor */
		kernel_thread(start_secondary, NULL, CLONE_PID);
		p = task[i];
		if ( !p )
			panic("No idle task for secondary processor\n");
		p->processor = i;
		p->has_cpu = 1;
		current_set[i] = p;

		/* need to flush here since secondary bats aren't setup */
		for (a = KERNELBASE; a < KERNELBASE + 0x800000; a += 32)
			asm volatile("dcbf 0,%0" : : "r" (a) : "memory");
		asm volatile("sync");

		/* wake up cpus */
		switch ( _machine )
		{
		case _MACH_Pmac:
			/* setup entry point of secondary processor */
			*(volatile unsigned long *)(0xf2800000) =
				(unsigned long)__secondary_start_psurge-KERNELBASE;
			eieio();
			/* interrupt secondary to begin executing code */
			out_be32(PSURGE_INTR, ~0);
			out_be32(PSURGE_INTR, 0);
			break;
		case _MACH_chrp:
			*(unsigned long *)KERNELBASE = i;
			asm volatile("dcbf 0,%0"::"r"(KERNELBASE):"memory");
			break;
		}
		
		/*
		 * wait to see if the cpu made a callin (is actually up).
		 * use this value that I found through experimentation.
		 * -- Cort
		 */
		for ( c = 1000; c && !cpu_callin_map[i] ; c-- )
			udelay(100);
		
		if ( cpu_callin_map[i] )
		{
			printk("Processor %d found.\n", i);
			/* this sync's the decr's -- Cort */
			if ( _machine == _MACH_Pmac )
				set_dec(decrementer_count);
		} else {
			printk("Processor %d is stuck.\n", i);
		}
	}
	
	if ( _machine == _MACH_Pmac )
	{
		/* reset the entry point so if we get another intr we won't
		 * try to startup again */
		*(volatile unsigned long *)(0xf2800000) = 0x100;
		/* send interrupt to other processors to start decr's on all cpus */
		smp_message_pass(1,0xf0f0, 0, 0);
	}
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
