/*
 * $Id: smp.c,v 1.24 1998/04/27 09:02:37 cort Exp $
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
unsigned long cpu_present_map = 0;
volatile int cpu_number_map[NR_CPUS];
volatile unsigned long cpu_callin_map[NR_CPUS] = {0,};
volatile int __cpu_logical_map[NR_CPUS];
static unsigned char boot_cpu_id = 0;
struct cpuinfo_PPC cpu_data[NR_CPUS];
struct klock_info_struct klock_info = { KLOCK_CLEAR, 0 };
volatile unsigned char active_kernel_processor = NO_PROC_ID;	/* Processor holding kernel spinlock		*/

volatile unsigned long ipi_count;

unsigned int prof_multiplier[NR_CPUS];
unsigned int prof_counter[NR_CPUS];

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
				need_resched = 1;
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
 * As it is now, if we're sending two message as the same time
 * we have race conditions.  I avoided doing locks here since
 * all that works right now is the stop cpu message.
 *
 *  -- Cort
 */
int smp_message[NR_CPUS];
void smp_message_recv(void)
{
	int msg = smp_message[smp_processor_id()];
	
	printk("SMP %d: smp_message_recv() msg %x\n", smp_processor_id(),msg);
	
	/* make sure msg is for us */
	if ( msg == -1 ) return;
printk("recv after msg check\n");	
	switch( msg )
	{
	case MSG_STOP_CPU:
		__cli();
		while (1) ;
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

spinlock_t mesg_pass_lock = SPIN_LOCK_UNLOCKED;
void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	printk("SMP %d: sending smp message\n", current->processor);

	spin_lock(&mesg_pass_lock);
	if ( _machine != _MACH_Pmac )
		return;

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
	/**(volatile unsigned long *)(0xf80000c0) = 0xffffffff;
	eieio();*/
	*(volatile unsigned long *)(0xf80000c0) = 0;
	/* interrupt primary */
	/**(volatile unsigned long *)(0xf3019000);*/
	spin_unlock(&mesg_pass_lock);	
}

__initfunc(void smp_boot_cpus(void))
{
	extern unsigned long secondary_entry[];
	extern struct task_struct *current_set[NR_CPUS];
	int i, timeout;
	struct task_struct *p;
	
        printk("Entering SMP Mode...\n");

	for (i = 0; i < NR_CPUS; i++) {
		cpu_number_map[i] = -1;
		prof_counter[i] = 1;
		prof_multiplier[i] = 1;
	}
	
	cpu_present_map = 0;
	for(i=0; i < NR_CPUS; i++)
		__cpu_logical_map[i] = -1;

        smp_store_cpu_info(boot_cpu_id);
        active_kernel_processor = boot_cpu_id;
	current->processor = boot_cpu_id;
        cpu_present_map |= 1;
        cpu_number_map[boot_cpu_id] = 0;
	__cpu_logical_map[0] = boot_cpu_id;
	
	if ( _machine != _MACH_Pmac )
	{
		printk("SMP not supported on this machine.\n");
		return;
	}
	
        /* assume a 2nd processor for now */	
        cpu_present_map |= (1 << 1); 
	smp_num_cpus = 2;
	
	/* create a process for second processor */
        kernel_thread(start_secondary, NULL, CLONE_PID);
	p = task[1];
	if ( !p )
		panic("No idle task for secondary processor\n");
	p->processor = 1;
	current_set[1] = p;
	/* need to flush here since secondary bat's aren't setup */
	dcbf((volatile unsigned long *)&current_set[1]);
	
	/* setup entry point of secondary processor */
	*(volatile unsigned long *)(0xf2800000)
		= (unsigned long)secondary_entry-KERNELBASE;
	eieio();
	/* interrupt secondary to begin executing code */
	*(volatile unsigned long *)(0xf80000c0) = 0L;
	eieio();
	/* wait to see if the secondary made a callin (is actually up) */
	for ( timeout = 0; timeout < 15000 ; timeout++ )
	{
		if(cpu_callin_map[1])
			break;
		udelay(100);
	}
	if(cpu_callin_map[1]) {
		cpu_number_map[1] = 1;
		__cpu_logical_map[i] = 1;
		printk("Processor 1 found.\n");

#if 0 /* this sync's the decr's, but we don't want this now -- Cort */
		set_dec(decrementer_count);
#endif
		/* interrupt secondary to start decr's again */
		smp_message_pass(1,0xf0f0, 0, 0);
		/* interrupt secondary to begin executing code */
		/**(volatile unsigned long *)(0xf80000c0) = 0L;
		eieio();*/
	} else {
		smp_num_cpus--;
		printk("Processor %d is stuck.\n", 1);
	}
}

__initfunc(void smp_commence(void))
{
	printk("SMP %d: smp_commence()\n",current->processor);
	/*
	 *	Lets the callin's below out of their loop.
	 */
	local_flush_tlb_all();
	smp_commenced = 1;
	local_flush_tlb_all();
}

/* intel needs this */
__initfunc(void initialize_secondary(void))
{
}

/* Activate a secondary processor. */
__initfunc(int start_secondary(void *unused))
{
	printk("SMP %d: start_secondary()\n",current->processor);
	smp_callin();
	return cpu_idle(NULL);
}

__initfunc(void smp_callin(void))
{
	printk("SMP %d: smp_callin()\n",current->processor);
        smp_store_cpu_info(1);
	set_dec(decrementer_count);
	
	current->mm->mmap->vm_page_prot = PAGE_SHARED;
	current->mm->mmap->vm_start = PAGE_OFFSET;
	current->mm->mmap->vm_end = init_task.mm->mmap->vm_end;
	
	/* assume we're just the secondary processor for now */
	cpu_callin_map[1] = 1;

	while(!smp_commenced)
		barrier();
	__sti();
}

__initfunc(void smp_setup(char *str, int *ints))
{
	printk("SMP %d: smp_setup()\n",current->processor);
}

__initfunc(int setup_profiling_timer(unsigned int multiplier))
{
	return 0;
}

__initfunc(void smp_store_cpu_info(int id))
{
        struct cpuinfo_PPC *c = &cpu_data[id];

        c->loops_per_sec = loops_per_sec;
        c->pvr = _get_PVR();
}
