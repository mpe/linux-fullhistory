#include <linux/kernel.h>

#include <asm/irq.h>
#include <asm/ptrace.h>

static int
sn1_startup_irq(unsigned int irq)
{
        return(0);
}

static void
sn1_shutdown_irq(unsigned int irq)
{
}

static void
sn1_disable_irq(unsigned int irq)
{
}

static void
sn1_enable_irq(unsigned int irq)
{
}

static int
sn1_handle_irq(unsigned int irq, struct pt_regs *regs)
{
       return(0);
}

struct hw_interrupt_type irq_type_sn1 = {
        "sn1_irq",
        sn1_startup_irq,
        sn1_shutdown_irq,
        sn1_handle_irq,
        sn1_enable_irq,
        sn1_disable_irq
};

void
sn1_irq_init (struct irq_desc desc[NR_IRQS])
{
	int i;

	for (i = IA64_MIN_VECTORED_IRQ; i <= IA64_MAX_VECTORED_IRQ; ++i) {
		irq_desc[i].handler = &irq_type_sn1;
	}
}
