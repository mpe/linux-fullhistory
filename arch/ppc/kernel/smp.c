/*
 * $Id: smp.c,v 1.68 1999/09/17 19:38:05 cort Exp $
 *
 * Smp support for ppc.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) borrowing a great
 * deal of code from the sparc and intel versions.
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 *
 * Support for PReP (Motorola MTX/MVME) SMP by Troy Benjegerdes
 * (troy@microux.com, hozer@drgw.net)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/init.h>
#include <linux/openpic.h>
#include <linux/spinlock.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/init.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/gemini.h>

#include "time.h"
#include "open_pic.h"
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
			if (p->counter <= 0) {
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

void smp_message_recv(int msg)
{
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
	case MSG_INVALIDATE_TLB:
		_tlbia();
	case 0xf0f0: /* pmac syncing time bases - just return */
		break;
	default:
		printk("SMP %d: smp_message_recv(): unknown msg %d\n",
		       smp_processor_id(), msg);
		break;
	}
}

/*
 * As it is now, if we're sending two message at the same time
 * we have race conditions on Pmac.  The PowerSurge doesn't easily
 * allow us to send IPI messages so we put the messages in
 * smp_message[].
 *
 * This is because don't have several IPI's on the PowerSurge even though
 * we do on the chrp.  It would be nice to use actual IPI's such as with openpic
 * rather than this.
 *  -- Cort
 */
int pmac_smp_message[NR_CPUS];
void pmac_smp_message_recv(void)
{
	int msg = pmac_smp_message[smp_processor_id()];
	
	/* clear interrupt */
	out_be32(PSURGE_INTR, ~0);
	
	/* make sure msg is for us */
	if ( msg == -1 ) return;

	smp_message_recv(msg);
	
 	/* reset message */
	pmac_smp_message[smp_processor_id()] = -1;
}

/*
 * 750's don't broadcast tlb invalidates so
 * we have to emulate that behavior.
 *   -- Cort
 */
void smp_send_tlb_invalidate(int cpu)
{
	if ( (_get_PVR()>>16) == 8 )
		smp_message_pass(MSG_ALL_BUT_SELF, MSG_INVALIDATE_TLB, 0, 0);
}

void smp_send_reschedule(int cpu)
{
	/*
	 * This is only used if `cpu' is running an idle task,
	 * so it will reschedule itself anyway...
	 *
	 * This isn't the case anymore since the other CPU could be
	 * sleeping and won't reschedule until the next interrupt (such
	 * as the timer).
	 *  -- Cort
	 */
	/* This is only used if `cpu' is running an idle task,
	   so it will reschedule itself anyway... */
	smp_message_pass(cpu, MSG_RESCHEDULE, 0, 0);
}

void smp_send_stop(void)
{
	smp_message_pass(MSG_ALL_BUT_SELF, MSG_STOP_CPU, 0, 0);
}

void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	int i;
	
	if ( !(_machine & (_MACH_Pmac|_MACH_chrp|_MACH_prep|_MACH_gemini)) )
		return;

	switch (_machine) {
	case _MACH_Pmac:
		/*
		 * IPI's on the Pmac are a hack but without reasonable
		 * IPI hardware SMP on Pmac is a hack.
		 *
		 * We assume here that the msg is not -1.  If it is,
		 * the recipient won't know the message was destined
		 * for it. -- Cort
		 */
		for ( i = 0; i <= smp_num_cpus ; i++ )
			pmac_smp_message[i] = -1;
		switch( target )
		{
		case MSG_ALL:
			pmac_smp_message[smp_processor_id()] = msg;
			/* fall through */
		case MSG_ALL_BUT_SELF:
			for ( i = 0 ; i < smp_num_cpus ; i++ )
				if ( i != smp_processor_id () )
					pmac_smp_message[i] = msg;
			break;
		default:
			pmac_smp_message[target] = msg;
			break;
		}
		/* interrupt secondary processor */
		out_be32(PSURGE_INTR, ~0);
		out_be32(PSURGE_INTR, 0);
		/*
		 * Assume for now that the secondary doesn't send
		 * IPI's -- Cort
		 */
		/* interrupt primary */
		/**(volatile unsigned long *)(0xf3019000);*/
		break;
	case _MACH_chrp:
	case _MACH_prep:
	case _MACH_gemini:
		/* make sure we're sending something that translates to an IPI */
		if ( msg > 0x3 )
			break;
		switch ( target )
		{
		case MSG_ALL:
			openpic_cause_IPI(smp_processor_id(), msg, 0xffffffff);
			break;
		case MSG_ALL_BUT_SELF:
			openpic_cause_IPI(smp_processor_id(), msg,
					  0xffffffff & ~(1 << smp_processor_id()));
			break;
		default:
			openpic_cause_IPI(smp_processor_id(), msg, 1<<target);
			break;
		}
		break;
	}
}

void __init smp_boot_cpus(void)
{
	extern struct task_struct *current_set[NR_CPUS];
	extern unsigned long smp_chrp_cpu_nr;
	extern void __secondary_start_psurge(void);
	extern void __secondary_start_chrp(void);
	int i, cpu_nr;
	struct task_struct *p;
	unsigned long a;

	printk("Entering SMP Mode...\n");
	smp_num_cpus = 1;
        smp_store_cpu_info(0);

	/*
	 * assume for now that the first cpu booted is
	 * cpu 0, the master -- Cort
	 */
	cpu_callin_map[0] = 1;
        active_kernel_processor = 0;
	current->processor = 0;

	init_idle();

	for (i = 0; i < NR_CPUS; i++) {
		prof_counter[i] = 1;
		prof_multiplier[i] = 1;
	}

	/*
	 * XXX very rough, assumes 20 bus cycles to read a cache line,
	 * timebase increments every 4 bus cycles, 32kB L1 data cache.
	 */
	cacheflush_time = 5 * 1024;

	if ( !(_machine & (_MACH_Pmac|_MACH_chrp|_MACH_gemini)) )
	{
		printk("SMP not supported on this machine.\n");
		return;
	}
	
	switch ( _machine )
	{
	case _MACH_Pmac:
		/* assume powersurge board - 2 processors -- Cort */
		cpu_nr = 2; 
		break;
	case _MACH_chrp:
		for ( i = 0; i < 4 ; i++ )
			openpic_enable_IPI(i);
		cpu_nr = smp_chrp_cpu_nr;
		break;
	case _MACH_gemini:
		for ( i = 0; i < 4 ; i++ )
			openpic_enable_IPI(i);
                cpu_nr = (readb(GEMINI_CPUSTAT) & GEMINI_CPU_COUNT_MASK)>>2;
                cpu_nr = (cpu_nr == 0) ? 4 : cpu_nr;
		break;
	}

	/*
	 * only check for cpus we know exist.  We keep the callin map
	 * with cpus at the bottom -- Cort
	 */
	for ( i = 1 ; i < cpu_nr; i++ )
	{
		int c;
		struct pt_regs regs;
		
		/* create a process for the processor */
		/* we don't care about the values in regs since we'll
		   never reschedule the forked task. */
		if (do_fork(CLONE_VM|CLONE_PID, 0, &regs) < 0)
			panic("failed fork for CPU %d", i);
		p = init_task.prev_task;
		if (!p)
			panic("No idle task for CPU %d", i);
		del_from_runqueue(p);
		unhash_process(p);
		init_tasks[i] = p;

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
			udelay(1);
			out_be32(PSURGE_INTR, 0);
			break;
		case _MACH_chrp:
			*(unsigned long *)KERNELBASE = i;
			asm volatile("dcbf 0,%0"::"r"(KERNELBASE):"memory");
			break;
		case _MACH_gemini:
			openpic_init_processor( 1<<i );
			openpic_init_processor( 0 );
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
			smp_num_cpus++;
		} else {
			printk("Processor %d is stuck.\n", i);
		}
	}
	
	if ( _machine & (_MACH_gemini|_MACH_chrp|_MACH_prep) )
		do_openpic_setup_cpu();
	
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
	/*
	 *	Lets the callin's below out of their loop.
	 */
	wmb();
	smp_commenced = 1;
}

/* intel needs this */
void __init initialize_secondary(void)
{
}

/* Activate a secondary processor. */
int __init start_secondary(void *unused)
{
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	smp_callin();
	return cpu_idle(NULL);
}

void __init smp_callin(void)
{
        smp_store_cpu_info(current->processor);
	set_dec(decrementer_count);
	
	init_idle();
#if 0
	current->mm->mmap->vm_page_prot = PAGE_SHARED;
	current->mm->mmap->vm_start = PAGE_OFFSET;
	current->mm->mmap->vm_end = init_mm.mmap->vm_end;
#endif
	cpu_callin_map[current->processor] = 1;
	/*
	 * Each processor has to do this and this is the best
	 * place to stick it for now.
	 *  -- Cort
	 */
	if ( _machine & (_MACH_gemini|_MACH_chrp|_MACH_prep) )
		do_openpic_setup_cpu();
#ifdef CONFIG_GEMINI	
	if ( _machine == _MACH_gemini )
		gemini_init_l2();
#endif
	while(!smp_commenced)
		barrier();
	__sti();
}

void __init smp_setup(char *str, int *ints)
{
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
