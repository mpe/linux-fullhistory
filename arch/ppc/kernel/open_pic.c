#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/openpic.h>
#include <asm/irq.h>
#include "open_pic.h"
#include "i8259.h"

#ifdef __SMP__
void openpic_ipi_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	smp_message_recv();
}
#endif /* __SMP__ */

void __openfirmware chrp_mask_and_ack_irq(unsigned int irq_nr)
{
	if (is_8259_irq(irq_nr))
	    i8259_pic.mask_and_ack(irq_nr);
}

static void __openfirmware chrp_mask_irq(unsigned int irq_nr)
{
	if (is_8259_irq(irq_nr))
		i8259_pic.disable(irq_nr);
	else
		openpic_disable_irq(irq_to_openpic(irq_nr));
}

static void __openfirmware chrp_unmask_irq(unsigned int irq_nr)
{
	if (is_8259_irq(irq_nr))
		i8259_pic.enable(irq_nr);
	else
		openpic_enable_irq(irq_to_openpic(irq_nr));
}

struct hw_interrupt_type open_pic = {
	" OpenPIC  ",
	NULL,
	NULL,
	NULL,
	chrp_unmask_irq,
	chrp_mask_irq,
	chrp_mask_and_ack_irq,
	0
};
