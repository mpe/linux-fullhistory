#ifndef _PPC_KERNEL_PMAC_PIC_H
#define _PPC_KERNEL_PMAC_PIC_H

#include "local_irq.h"

extern struct hw_interrupt_type pmac_pic;

void pmac_pic_init(void);
void pmac_do_IRQ(struct pt_regs *regs,
                 int            cpu,
                 int            isfake);


#endif /* _PPC_KERNEL_PMAC_PIC_H */

