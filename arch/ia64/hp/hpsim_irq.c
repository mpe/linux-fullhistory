/*
 * Platform dependent support for HP simulator.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Vijay Chander <vijay@engr.sgi.com>
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/console.h>

#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/pal.h>
#include <asm/machvec.h>
#include <asm/pgtable.h>
#include <asm/sal.h>


static int
irq_hp_sim_handle_irq (unsigned int irq, struct pt_regs *regs)
{
	struct irqaction *action = 0;
	struct irq_desc *id = irq_desc + irq;
	unsigned int status;
	int retval;

	spin_lock(&irq_controller_lock);
	{
		status = id->status;
		if ((status & IRQ_INPROGRESS) == 0 && (status & IRQ_ENABLED) != 0) {
			action = id->action;
			status |= IRQ_INPROGRESS;
		}
		id->status = status & ~(IRQ_REPLAY | IRQ_WAITING);
	}
	spin_unlock(&irq_controller_lock);

	if (!action) {
		if (!(id->status & IRQ_AUTODETECT))
			printk("irq_hpsim_handle_irq: unexpected interrupt %u\n", irq);
		return 0;
	}

	retval = invoke_irq_handlers(irq, regs, action);

	spin_lock(&irq_controller_lock);
	{
		id->status &= ~IRQ_INPROGRESS;
	}
	spin_unlock(&irq_controller_lock);

	return retval;
}

static void
irq_hp_sim_noop (unsigned int irq)
{
}

static struct hw_interrupt_type irq_type_hp_sim = {
	"hp_sim",
	(void (*)(unsigned long)) irq_hp_sim_noop,	/* init */
	irq_hp_sim_noop,				/* startup */
	irq_hp_sim_noop,				/* shutdown */
	irq_hp_sim_handle_irq,				/* handle */
	irq_hp_sim_noop,				/* enable */
	irq_hp_sim_noop,				/* disable */
};

void
hpsim_irq_init (struct irq_desc desc[NR_IRQS])
{
	int i;

	for (i = IA64_MIN_VECTORED_IRQ; i <= IA64_MAX_VECTORED_IRQ; ++i) {
		irq_desc[i].handler = &irq_type_hp_sim;
	}
}
