#ifndef _PPC_KERNEL_OPEN_PIC_H
#define _PPC_KERNEL_OPEN_PIC_H

extern struct hw_interrupt_type open_pic;

void openpic_ipi_action(int cpl, void *dev_id, struct pt_regs *regs);

#endif /* _PPC_KERNEL_OPEN_PIC_H */
