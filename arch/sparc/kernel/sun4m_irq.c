/*  sun4m_irq.c
 *  arch/sparc/kernel/sun4m_irq.c:
 *
 *  djhr: Hacked out of irq.c into a CPU dependent version.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995 Pete A. Zaitcev (zaitcev@ipmce.su)
 *  Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/io.h>

static unsigned long dummy;

extern int linux_num_cpus;
struct sun4m_intregs *sun4m_interrupts;
unsigned long *irq_rcvreg = &dummy;

/* These tables only apply for interrupts greater than 15..
 * 
 * any intr value below 0x10 is considered to be a soft-int
 * this may be useful or it may not.. but that's how I've done it.
 * and it won't clash with what OBP is telling us about devices.
 *
 * take an encoded intr value and lookup if it's valid
 * then get the mask bits that match from irq_mask
 */
static unsigned char irq_xlate[32] = {
    /*  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  a,  b,  c,  d,  e,  f */
	0,  0,  0,  0,  1,  0,  2,  0,  3,  0,  4,  5,  6,  0,  0,  7,
	0,  0,  8,  9,  0, 10,  0, 11,  0, 12,  0, 13,  0, 14,  0,  0
};

static unsigned long irq_mask[] = {
	0,						  /* illegal index */
	SUN4M_INT_SCSI,				  	  /*  1 irq 4 */
	SUN4M_INT_ETHERNET,				  /*  2 irq 6 */
	SUN4M_INT_VIDEO,				  /*  3 irq 8 */
	SUN4M_INT_REALTIME,				  /*  4 irq 10 */
	SUN4M_INT_FLOPPY,				  /*  5 irq 11 */
	(SUN4M_INT_SERIAL | SUN4M_INT_KBDMS),	  	  /*  6 irq 12 */
	SUN4M_INT_MODULE_ERR,			  	  /*  7 irq 15 */
	SUN4M_INT_SBUS(0),				  /*  8 irq 2 */
	SUN4M_INT_SBUS(1),				  /*  9 irq 3 */
	SUN4M_INT_SBUS(2),				  /* 10 irq 5 */
	SUN4M_INT_SBUS(3),				  /* 11 irq 7 */
	SUN4M_INT_SBUS(4),				  /* 12 irq 9 */
	SUN4M_INT_SBUS(5),				  /* 13 irq 11 */
	SUN4M_INT_SBUS(6)				  /* 14 irq 13 */
};

inline unsigned long sun4m_get_irqmask(unsigned int irq)
{
	unsigned long mask;
    
	if (irq > 0x20) {
		/* OBIO/SBUS interrupts */
		irq &= 0x1f;
		mask = irq_mask[irq_xlate[irq]];
		if (!mask)
			printk("sun4m_get_irqmask: IRQ%d has no valid mask!\n",irq);
	} else {
		/* Soft Interrupts will come here
		 * Currently there is no way to trigger them but I'm sure something
		 * could be cooked up.
		 */
		irq &= 0xf;
		mask = SUN4M_SOFT_INT(irq);
	}
	return mask;
}

static void sun4m_disable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;
	int cpu = smp_processor_id();

	mask = sun4m_get_irqmask(irq_nr);
	save_and_cli(flags);
	if (irq_nr > 15)
		sun4m_interrupts->set = mask;
	else
		sun4m_interrupts->cpu_intregs[cpu].set = mask;
	restore_flags(flags);    
}

static void sun4m_enable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;
	int cpu = smp_processor_id();

	/* Dreadful floppy hack. When we use 0x2b instead of
         * 0x0b the system blows (it starts to whistle!).
         * So we continue to use 0x0b. Fixme ASAP. --P3
         */
        if (irq_nr != 0x0b) {
		mask = sun4m_get_irqmask(irq_nr);
		save_and_cli(flags);
		if (irq_nr > 15)
			sun4m_interrupts->clear = mask;
		else
			sun4m_interrupts->cpu_intregs[cpu].clear = mask;
		restore_flags(flags);    
	} else {
		save_and_cli(flags);
		sun4m_interrupts->clear = SUN4M_INT_FLOPPY;
		restore_flags(flags);
	}
}

void sun4m_send_ipi(int cpu, int level)
{
	unsigned long mask;

	mask = sun4m_get_irqmask(level);
	sun4m_interrupts->cpu_intregs[cpu].set = mask;
}

void sun4m_clear_ipi(int cpu, int level)
{
	unsigned long mask;

	mask = sun4m_get_irqmask(level);
	sun4m_interrupts->cpu_intregs[cpu].clear = mask;
}

void sun4m_set_udt(int cpu)
{
	sun4m_interrupts->undirected_target = cpu;
}

#define OBIO_INTR	0x20
#define TIMER_IRQ  	(OBIO_INTR | 10)
#define PROFILE_IRQ	(OBIO_INTR | 14)

struct sun4m_timer_regs *sun4m_timers;
unsigned int lvl14_resolution = (((1000000/HZ) + 1) << 10);

static void sun4m_clear_clock_irq(void)
{
	volatile unsigned int clear_intr;
	clear_intr = sun4m_timers->l10_timer_limit;
}

static void sun4m_clear_profile_irq(void)
{
	volatile unsigned int clear;
    
	clear = sun4m_timers->cpu_timers[0].l14_timer_limit;
}

static void sun4m_load_profile_irq(unsigned int limit)
{
	sun4m_timers->cpu_timers[0].l14_timer_limit = limit;
}

#if HANDLE_LVL14_IRQ
static void sun4m_lvl14_handler(int irq, void *dev_id, struct pt_regs * regs)
{
	volatile unsigned int clear;
    
	printk("CPU[%d]: TOOK A LEVEL14!\n", smp_processor_id());
	/* we do nothing with this at present
	 * this is purely to prevent OBP getting its mucky paws
	 * in linux.
	 */
	clear = sun4m_timers->cpu_timers[0].l14_timer_limit; /* clear interrupt */
    
	/* reload with value, this allows on the fly retuning of the level14
	 * timer
	 */
	sun4m_timers->cpu_timers[0].l14_timer_limit = lvl14_resolution;
}
#endif /* HANDLE_LVL14_IRQ */

__initfunc(static void sun4m_init_timers(void (*counter_fn)(int, void *, struct pt_regs *)))
{
	int reg_count, irq, cpu;
	struct linux_prom_registers cnt_regs[PROMREG_MAX];
	int obio_node, cnt_node;

	cnt_node = 0;
	if((obio_node =
	    prom_searchsiblings (prom_getchild(prom_root_node), "obio")) == 0 ||
	   (obio_node = prom_getchild (obio_node)) == 0 ||
	   (cnt_node = prom_searchsiblings (obio_node, "counter")) == 0) {
		prom_printf("Cannot find /obio/counter node\n");
		prom_halt();
	}
	reg_count = prom_getproperty(cnt_node, "reg",
				     (void *) cnt_regs, sizeof(cnt_regs));
	reg_count = (reg_count/sizeof(struct linux_prom_registers));
    
	/* Apply the obio ranges to the timer registers. */
	prom_apply_obio_ranges(cnt_regs, reg_count);
    
	cnt_regs[4].phys_addr = cnt_regs[reg_count-1].phys_addr;
	cnt_regs[4].reg_size = cnt_regs[reg_count-1].reg_size;
	cnt_regs[4].which_io = cnt_regs[reg_count-1].which_io;
	for(obio_node = 1; obio_node < 4; obio_node++) {
		cnt_regs[obio_node].phys_addr =
			cnt_regs[obio_node-1].phys_addr + PAGE_SIZE;
		cnt_regs[obio_node].reg_size = cnt_regs[obio_node-1].reg_size;
		cnt_regs[obio_node].which_io = cnt_regs[obio_node-1].which_io;
	}
    
	/* Map the per-cpu Counter registers. */
	sun4m_timers = sparc_alloc_io(cnt_regs[0].phys_addr, 0,
				      PAGE_SIZE*NCPUS, "counters_percpu",
				      cnt_regs[0].which_io, 0x0);
    
	/* Map the system Counter register. */
	sparc_alloc_io(cnt_regs[4].phys_addr, 0,
		       cnt_regs[4].reg_size,
		       "counters_system",
		       cnt_regs[4].which_io, 0x0);
    
	sun4m_timers->l10_timer_limit =  (((1000000/HZ) + 1) << 10);
	master_l10_counter = &sun4m_timers->l10_cur_count;
	master_l10_limit = &sun4m_timers->l10_timer_limit;

	irq = request_irq(TIMER_IRQ,
			  counter_fn,
			  (SA_INTERRUPT | SA_STATIC_ALLOC),
			  "timer", NULL);
	if (irq) {
		prom_printf("time_init: unable to attach IRQ%d\n",TIMER_IRQ);
		prom_halt();
	}
    
	/* Can't cope with multiple CPUS yet so no level14 tick events */
#if HANDLE_LVL14_IRQ
	if (linux_num_cpus > 1)
		claim_ticker14(NULL, PROFILE_IRQ, 0);
	else
		claim_ticker14(sun4m_lvl14_handler, PROFILE_IRQ, lvl14_resolution);
#endif /* HANDLE_LVL14_IRQ */
	if(linux_num_cpus > 1) {
		for(cpu = 0; cpu < 4; cpu++)
			sun4m_timers->cpu_timers[cpu].l14_timer_limit = 0;
		sun4m_interrupts->set = SUN4M_INT_E14;
	} else {
		sun4m_timers->cpu_timers[0].l14_timer_limit = 0;
	}
}

__initfunc(void sun4m_init_IRQ(void))
{
	int ie_node,i;
	struct linux_prom_registers int_regs[PROMREG_MAX];
	int num_regs;
    
	cli();
	if((ie_node = prom_searchsiblings(prom_getchild(prom_root_node), "obio")) == 0 ||
	   (ie_node = prom_getchild (ie_node)) == 0 ||
	   (ie_node = prom_searchsiblings (ie_node, "interrupt")) == 0) {
		prom_printf("Cannot find /obio/interrupt node\n");
		prom_halt();
	}
	num_regs = prom_getproperty(ie_node, "reg", (char *) int_regs,
				    sizeof(int_regs));
	num_regs = (num_regs/sizeof(struct linux_prom_registers));
    
	/* Apply the obio ranges to these registers. */
	prom_apply_obio_ranges(int_regs, num_regs);
    
	int_regs[4].phys_addr = int_regs[num_regs-1].phys_addr;
	int_regs[4].reg_size = int_regs[num_regs-1].reg_size;
	int_regs[4].which_io = int_regs[num_regs-1].which_io;
	for(ie_node = 1; ie_node < 4; ie_node++) {
		int_regs[ie_node].phys_addr = int_regs[ie_node-1].phys_addr + PAGE_SIZE;
		int_regs[ie_node].reg_size = int_regs[ie_node-1].reg_size;
		int_regs[ie_node].which_io = int_regs[ie_node-1].which_io;
	}

	/* Map the interrupt registers for all possible cpus. */
	sun4m_interrupts = sparc_alloc_io(int_regs[0].phys_addr, 0,
					  PAGE_SIZE*NCPUS, "interrupts_percpu",
					  int_regs[0].which_io, 0x0);
    
	/* Map the system interrupt control registers. */
	sparc_alloc_io(int_regs[4].phys_addr, 0,
		       int_regs[4].reg_size, "interrupts_system",
		       int_regs[4].which_io, 0x0);
    
	sun4m_interrupts->set = ~SUN4M_INT_MASKALL;
	for (i=0; i<linux_num_cpus; i++)
		sun4m_interrupts->cpu_intregs[i].clear = ~0x17fff;
    
	if (linux_num_cpus > 1) {
		/* system wide interrupts go to cpu 0, this should always
		 * be safe because it is guaranteed to be fitted or OBP doesn't
		 * come up
		 *
		 * Not sure, but writing here on SLAVIO systems may puke
		 * so I don't do it unless there is more than 1 cpu.
		 */
#if 0
		printk("Warning:"
		       "sun4m multiple CPU interrupt code requires work\n");
#endif
		irq_rcvreg = (unsigned long *)
				&sun4m_interrupts->undirected_target;
		sun4m_interrupts->undirected_target = 0;
	}
	enable_irq = sun4m_enable_irq;
	disable_irq = sun4m_disable_irq;
	clear_clock_irq = sun4m_clear_clock_irq;
	clear_profile_irq = sun4m_clear_profile_irq;
	load_profile_irq = sun4m_load_profile_irq;
	init_timers = sun4m_init_timers;
#ifdef __SMP__
	set_cpu_int = (void (*) (int, int))sun4m_send_ipi;
	clear_cpu_int = (void (*) (int, int))sun4m_clear_ipi;
	set_irq_udt = (void (*) (int))sun4m_set_udt;
#endif
	sti();
}
