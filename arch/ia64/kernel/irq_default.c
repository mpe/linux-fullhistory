#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/irq.h>
#include <asm/processor.h>
#include <asm/ptrace.h>


static int
irq_default_handle_irq (unsigned int irq, struct pt_regs *regs)
{
	printk("Unexpected irq vector 0x%x on CPU %u!\n", irq, smp_processor_id());
	return 0;		/* don't call do_bottom_half() for spurious interrupts */
}

static void
irq_default_noop (unsigned int irq)
{
	/* nuthing to do... */
}

struct hw_interrupt_type irq_type_default = {
	"default",
	(void (*)(unsigned long)) irq_default_noop,	/* init */
	irq_default_noop,				/* startup */
	irq_default_noop,				/* shutdown */
	irq_default_handle_irq,				/* handle */
	irq_default_noop,				/* enable */
	irq_default_noop				/* disable */
};
