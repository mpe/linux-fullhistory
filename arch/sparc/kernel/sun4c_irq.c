/*  sun4c_irq.c
 *  arch/sparc/kernel/sun4c_irq.c:
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
#include <linux/interrupt.h>
#include <linux/malloc.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>

/* Pointer to the interrupt enable byte
 *
 * Dave Redman (djhr@tadpole.co.uk)
 * What you may not be aware of is that entry.S requires this variable.
 *
 *  --- linux_trap_nmi_sun4c --
 *
 * so don't go making it static, like I tried. sigh.
 */
unsigned char *interrupt_enable = 0;

static void sun4c_disable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char current_mask, new_mask;
    
	save_and_cli(flags);
	irq_nr &= NR_IRQS;
	current_mask = *interrupt_enable;
	switch(irq_nr) {
	case 1:
		new_mask = ((current_mask) & (~(SUN4C_INT_E1)));
		break;
	case 8:
		new_mask = ((current_mask) & (~(SUN4C_INT_E8)));
		break;
	case 10:
		new_mask = ((current_mask) & (~(SUN4C_INT_E10)));
		break;
	case 14:
		new_mask = ((current_mask) & (~(SUN4C_INT_E14)));
		break;
	default:
		restore_flags(flags);
		return;
	}
	*interrupt_enable = new_mask;
	restore_flags(flags);
}

static void sun4c_enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char current_mask, new_mask;
    
	save_and_cli(flags);
	irq_nr &= NR_IRQS;
	current_mask = *interrupt_enable;
	switch(irq_nr) {
	case 1:
		new_mask = ((current_mask) | SUN4C_INT_E1);
		break;
	case 8:
		new_mask = ((current_mask) | SUN4C_INT_E8);
		break;
	case 10:
		new_mask = ((current_mask) | SUN4C_INT_E10);
		break;
	case 14:
		new_mask = ((current_mask) | SUN4C_INT_E14);
		break;
	default:
		restore_flags(flags);
		return;
	}
	*interrupt_enable = new_mask;
	restore_flags(flags);
}

#define TIMER_IRQ  	10    /* Also at level 14, but we ignore that one. */
#define PROFILE_IRQ	14    /* Level14 ticker.. used by OBP for polling */

volatile struct sun4c_timer_info *sun4c_timers;

static void sun4c_clear_clock_irq(void)
{
	volatile unsigned int clear_intr;
	clear_intr = sun4c_timers->timer_limit10;
}

static void sun4c_clear_profile_irq(void)
{
	/* Errm.. not sure how to do this.. */
}

static void sun4c_load_profile_irq(unsigned int limit)
{
	/* Errm.. not sure how to do this.. */
}

static void sun4c_init_timers(void (*counter_fn)(int, void *, struct pt_regs *))
{
	int irq;

	/* Map the Timer chip, this is implemented in hardware inside
	 * the cache chip on the sun4c.
	 */
	sun4c_timers = sparc_alloc_io ((void *) SUN4C_TIMER_PHYSADDR, 0,
				       sizeof(struct sun4c_timer_info),
				       "timer", 0x0, 0x0);
    
	/* Have the level 10 timer tick at 100HZ.  We don't touch the
	 * level 14 timer limit since we are letting the prom handle
	 * them until we have a real console driver so L1-A works.
	 */
	sun4c_timers->timer_limit10 = (((1000000/HZ) + 1) << 10);
	master_l10_counter = &sun4c_timers->cur_count10;
	master_l10_limit = &sun4c_timers->timer_limit10;

	irq = request_irq(TIMER_IRQ,
			  counter_fn,
			  (SA_INTERRUPT | SA_STATIC_ALLOC),
			  "timer", NULL);
	if (irq) {
		prom_printf("time_init: unable to attach IRQ%d\n",TIMER_IRQ);
		prom_halt();
	}
    
	claim_ticker14(NULL, PROFILE_IRQ, 0);
}

#ifdef __SMP__
static void sun4c_nop(void) {}
#endif

void sun4c_init_IRQ(void)
{
	struct linux_prom_registers int_regs[2];
	int ie_node;
    
	ie_node = prom_searchsiblings (prom_getchild(prom_root_node),
				       "interrupt-enable");
	if(ie_node == 0)
		panic("Cannot find /interrupt-enable node");

	/* Depending on the "address" property is bad news... */
	prom_getproperty(ie_node, "reg", (char *) int_regs, sizeof(int_regs));
	interrupt_enable = (char *) sparc_alloc_io(int_regs[0].phys_addr, 0,
						   int_regs[0].reg_size,
						   "sun4c_interrupts",
						   int_regs[0].which_io, 0x0);
	enable_irq = sun4c_enable_irq;
	disable_irq = sun4c_disable_irq;
	clear_clock_irq = sun4c_clear_clock_irq;
	clear_profile_irq = sun4c_clear_profile_irq;
	load_profile_irq = sun4c_load_profile_irq;
	init_timers = sun4c_init_timers;
#ifdef __SMP__
	set_cpu_int = (void (*) (int, int))sun4c_nop;
	clear_cpu_int = (void (*) (int, int))sun4c_nop;
	set_irq_udt = (void (*) (int))sun4c_nop;
#endif
	*interrupt_enable = (SUN4C_INT_ENABLE);
	sti();
}
