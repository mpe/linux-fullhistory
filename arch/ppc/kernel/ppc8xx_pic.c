
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <asm/irq.h>
#include <asm/8xx_immap.h>
#include <asm/mbx.h>
#include "ppc8xx_pic.h"


static void mbx_mask_irq(unsigned int irq_nr)
{
	if ( irq_nr == ISA_BRIDGE_INT ) return;
	if ( irq_nr >= ppc8xx_pic.irq_offset )
		irq_nr -= ppc8xx_pic.irq_offset;
	ppc_cached_irq_mask[0] &= ~(1 << (31-irq_nr));
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask =	ppc_cached_irq_mask[0];
}

static void mbx_unmask_irq(unsigned int irq_nr)
{
	if ( irq_nr >= ppc8xx_pic.irq_offset )
		irq_nr -= ppc8xx_pic.irq_offset;
	ppc_cached_irq_mask[0] |= (1 << (31-irq_nr));
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask =	ppc_cached_irq_mask[0];
}

static void mbx_mask_and_ack(unsigned int irq_nr)
{
	/* this shouldn't be masked, we mask the 8259 if we need to -- Cort */
	if ( irq_nr != ISA_BRIDGE_INT )
		mbx_mask_irq(irq_nr);
	if ( irq_nr >= ppc8xx_pic.irq_offset )
		irq_nr -= ppc8xx_pic.irq_offset;
	/* clear the pending bits */
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_sipend = 1 << (31-irq_nr);
}

struct hw_interrupt_type ppc8xx_pic = {
	" 8xx SIU  ",
	NULL,
	NULL,
	NULL,
	mbx_unmask_irq,
	mbx_mask_irq,
	mbx_mask_and_ack,
	0
};
