/*
 * $Id: smp.c,v 1.8 1998/01/06 06:44:57 cort Exp $
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

int smp_threads_ready = 0;
volatile int smp_commenced = 0;
int smp_num_cpus = 1;
unsigned long cpu_present_map = 0;
volatile int cpu_number_map[NR_CPUS];
volatile unsigned long cpu_callin_map[NR_CPUS] = {0,};
static unsigned char boot_cpu_id = 0;
struct cpuinfo_PPC cpu_data[NR_CPUS];
struct klock_info klock_info = { KLOCK_CLEAR, 0 };
volatile unsigned char active_kernel_processor = NO_PROC_ID;	/* Processor holding kernel spinlock		*/

volatile unsigned long hash_table_lock;

int start_secondary(void *);

extern void init_IRQ(void);
extern int cpu_idle(void *unused);

void smp_boot_cpus(void)
{
	extern unsigned long secondary_entry[];
	extern struct task_struct *current_set[NR_CPUS];
	int i, timeout;
	struct task_struct *p;
	
        printk("Entering SMP Mode...\n");
	cpu_present_map = 0;
	for(i=0; i < NR_CPUS; i++)
		cpu_number_map[i] = -1;

        smp_store_cpu_info(boot_cpu_id);
        active_kernel_processor = boot_cpu_id;
	current->processor = boot_cpu_id;
	
        cpu_present_map |= 1;
        /* assume a 2nd processor for now */	
        cpu_present_map |= (1 << 1); 
	smp_num_cpus = 2;
        cpu_number_map[boot_cpu_id] = 0;
	
	/* create a process for second processor */
        kernel_thread(start_secondary, NULL, CLONE_PID);
        cpu_number_map[1] = 1;
	p = task[1];
	if ( !p )
		panic("No idle task for secondary processor\n");
	p->processor = 1;
	current_set[1] = p;
	/* setup entry point of secondary processor */
	*(volatile unsigned long *)(0xf2800000)
		= (unsigned long)secondary_entry-KERNELBASE;
	/* interrupt secondary to begin executing code */
	eieio();
	*(volatile unsigned long *)(0xf80000c0) = 0;
	eieio();
	/* wait to see if the secondary made a callin (is actually up) */
	for ( timeout = 0; timeout < 1500 ; timeout++ )
		udelay(100);
	if(cpu_callin_map[1]) {
		cpu_number_map[1] = 1;
		printk("Processor 1 found.\n");
	} else {
		smp_num_cpus--;
		printk("Processor %d is stuck.\n", 1);
	}
{
  extern unsigned long amhere;
  printk("amhere: %x\n", amhere);
}
}

void smp_commence(void)
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
void initialize_secondary(void)
{
}

/* Activate a secondary processor. */
int start_secondary(void *unused)
{
	printk("SMP %d: start_secondary()\n",current->processor);
	smp_callin();
	return cpu_idle(NULL);
}

void smp_callin(void)
{
	printk("SMP %d: smp_callin()\n",current->processor);
	/*calibrate_delay();*/
        smp_store_cpu_info(1);
	
	/* assume we're just the secondary processor for now */
	cpu_callin_map[1] = 1;
	dcbf(&cpu_callin_map[1]);
	
	current->mm->mmap->vm_page_prot = PAGE_SHARED;
	current->mm->mmap->vm_start = PAGE_OFFSET;
	current->mm->mmap->vm_end = init_task.mm->mmap->vm_end;

	while(!smp_commenced)
		barrier();
	__sti();
}

void smp_setup(char *str, int *ints)
{
	printk("SMP %d: smp_setup()\n",current->processor);
}

void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	printk("SMP %d: sending smp message\n",current->processor);
#if 0	
	if ( smp_processor_id() == 0 )
	{
		/* interrupt secondary */
		*(volatile unsigned long *)(0xf80000c0) = 0;
	}
	else
	{
		/* interrupt primary */
		*(volatile unsigned long *)(0xf3019000);
	}
#endif	
}

int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

__initfunc(void smp_store_cpu_info(int id))
{
        struct cpuinfo_PPC *c = &cpu_data[id];

        c->loops_per_sec = loops_per_sec;
        c->pvr = _get_PVR();
}

