/* smp.c: Sparc SMP support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/smp.h>

int smp_num_cpus;
int smp_threads_ready;
volatile unsigned long smp_msg_data;
volatile int smp_src_cpu;
volatile int smp_msg_id;

static volatile int smp_commenced = 0;

/* The only guaranteed locking primitive available on all Sparc
 * processors is 'ldstub [%addr_reg + imm], %dest_reg' which atomically
 * places the current byte at the effective address into dest_reg and
 * places 0xff there afterwards.  Pretty lame locking primitive
 * compared to the Alpha and the intel no?  Most Sparcs have 'swap'
 * instruction which is much better...
 */
klock_t kernel_lock;

void smp_commence(void)
{
	/*
	 *	Lets the callin's below out of their loop.
	 */
	smp_commenced = 1;
}

void smp_callin(void)
{
	int cpuid = smp_get_processor_id();

	/* XXX Clear the software interrupts _HERE_. */

	sti();
	calibrate_delay();
	smp_store_cpu_info(cpuid);
	set_bit(cpuid, (unsigned long *)&cpu_callin_map[0]);
	local_invalidate_all();
	while(!smp_commenced);
	if(cpu_number_map[cpuid] == -1)
		while(1);
	local_invalidate_all();
}

void smp_boot_cpus(void)
{
}

void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	struct sparc_ipimsg *msg = (struct sparc_ipimsg *) data;
	unsigned long target_map;
	int p = smp_processor_id();
	static volatile int message_cpu = NO_PROC_ID;

	if(!smp_activated || !smp_commenced)
		return;

	if(msg == MSG_RESCHEDULE) {
		if(smp_cpu_in_msg[p])
			return;
	}

	if(message_cpu != NO_PROC_ID && msg != MSG_STOP_CPU) {
		panic("CPU #%d: Message pass %d but pass in progress by %d of %d\n",
		      smp_processor_id(),msg,message_cpu, smp_msg_id);
	}
	message_cpu = smp_processor_id();
	smp_cpu_in_msg[p]++;
	if(msg != MSG_RESCHEDULE) {
		smp_src_cpu = p;
		smp_msg_id = msg;
		smp_msg_data = data;
	}

	if(target == MSG_ALL_BUT_SELF) {
		target_map = cpu_present_map;
		cpu_callin_map[0] = (1<<smp_src_cpu);
	} else if(target == MSG_ALL) {
		target_map = cpu_present_map;
		cpu_callin_map[0] = 0;
	} else {
		target_map = (1<<target);
		cpu_callin_map[0] = 0;
	}

	/* XXX Send lvl15 soft interrupt to cpus here XXX */

	switch(wait) {
	case 1:
		while(cpu_callin_map[0] != target_map);
		break;
	case 2:
		while(smp_invalidate_needed);
		break;
	}
	smp_cpu_in_msg[p]--;
	message_cpu = NO_PROC_ID;
}

inline void smp_invalidate(int type, unsigned long a, unsigned long b, unsigned long c)
{
	unsigned long flags;

	smp_invalidate_needed = cpu_present_map & ~(1<<smp_processor_id());
	save_flags(flags); cli();
	smp_message_pass(MSG_ALL_BUT_SELF, MSG_INVALIDATE_TLB, 0L, 2);
	local_invalidate();
	restore_flags(flags);
}

void smp_invalidate_all(void)
{
	smp_invalidate(0, 0, 0, 0);
}

void smp_invalidate_mm(struct mm_struct *mm)
{
	smp_invalidate(1, (unsigned long) mm, 0, 0);
}

void smp_invalidate_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	smp_invalidate(2, (unsigned long) mm, start, end);
}

void smp_invalidate_page(struct vm_area_struct *vmap, unsigned long page)
{
	smp_invalidate(3, (unsigned long)vmap->vm_mm, page, 0);
}

void smp_reschedule_irq(int cpl, struct pt_regs *regs)
{
	if(smp_processor_id() != active_kernel_processor)
		panic("SMP Reschedule on CPU #%d, but #%d is active.\n",
		      smp_processor_id(), active_kernel_processor);
	if(user_mode(regs)) {
		current->utime++;
		if (current->pid) {
			if (current->priority < 15)
				kstat.cpu_nice++;
			else
				kstat.cpu_user++;
		}
		/* Update ITIMER_VIRT for current task if not in a system call */
		if (current->it_virt_value && !(--current->it_virt_value)) {
			current->it_virt_value = current->it_virt_incr;
			send_sig(SIGVTALRM,current,1);
		}
	} else {
		current->stime++;
		if(current->pid)
			kstat.cpu_system++;
#ifdef CONFIG_PROFILE
		if (prof_buffer && current->pid) {
			extern int _stext;
			unsigned long eip = regs->eip - (unsigned long) &_stext;
			eip >>= CONFIG_PROFILE_SHIFT;
			if (eip < prof_len)
				prof_buffer[eip]++;
		}
#endif
	}

	/*
	 * check the cpu time limit on the process.
	 */
	if ((current->rlim[RLIMIT_CPU].rlim_max != RLIM_INFINITY) &&
	    (((current->stime + current->utime) / HZ) >= current->rlim[RLIMIT_CPU].rlim_max))
		send_sig(SIGKILL, current, 1);
	if ((current->rlim[RLIMIT_CPU].rlim_cur != RLIM_INFINITY) &&
	    (((current->stime + current->utime) % HZ) == 0)) {
		unsigned long psecs = (current->stime + current->utime) / HZ;
		/* send when equal */
		if (psecs == current->rlim[RLIMIT_CPU].rlim_cur)
			send_sig(SIGXCPU, current, 1);
		/* and every five seconds thereafter. */
		else if ((psecs > current->rlim[RLIMIT_CPU].rlim_cur) &&
			 ((psecs - current->rlim[RLIMIT_CPU].rlim_cur) % 5) == 0)
			send_sig(SIGXCPU, current, 1);
	}

	/* Update ITIMER_PROF for the current task */
	if (current->it_prof_value && !(--current->it_prof_value)) {
		current->it_prof_value = current->it_prof_incr;
		send_sig(SIGPROF,current,1);
	}

	if(0 > --current->counter || current->pid == 0) {
		current->counter = 0;
		need_resched = 1;
	}
}

void smp_message_irq(int cpl, struct pt_regs *regs)
{
	int i=smp_processor_id();
/*	static int n=0;
	if(n++<NR_CPUS)
		printk("IPI %d->%d(%d,%ld)\n",smp_src_cpu,i,smp_msg_id,smp_msg_data);*/
	switch(smp_msg_id)
	{
		case 0:	/* IRQ 13 testing - boring */
			return;
			
		/*
		 *	A TLB flush is needed.
		 */
		 
		case MSG_INVALIDATE_TLB:
			if(clear_bit(i,(unsigned long *)&smp_invalidate_needed))
				local_invalidate();
			set_bit(i, (unsigned long *)&cpu_callin_map[0]);
			cpu_callin_map[0]|=1<<smp_processor_id();
			break;
			
		/*
		 *	Halt other CPU's for a panic or reboot
		 */
		case MSG_STOP_CPU:
			while(1)
			{
				if(cpu_data[smp_processor_id()].hlt_works_ok)
					__asm__("hlt");
			}
		default:
			printk("CPU #%d sent invalid cross CPU message to CPU #%d: %X(%lX).\n",
				smp_src_cpu,smp_processor_id(),smp_msg_id,smp_msg_data);
			break;
	}
	/*
	 *	Clear the IPI, so we can receive future IPI's
	 */
	 
	apic_read(APIC_SPIV);		/* Dummy read */
	apic_write(APIC_EOI, 0);	/* Docs say use 0 for future compatibility */
}
