/*
 *  linux/arch/m68k/hp300/ints.c
 *
 *  Copyright (C) 1998 Philip Blundell <philb@gnu.org>
 *
 *  This file contains the HP300-specific interrupt handling.  There
 *  isn't much here -- we only use the autovector interrupts and at the
 *  moment everything difficult is handled by the generic code.
 */

#include <linux/config.h>
#include <asm/ptrace.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/traps.h>
#include "ints.h"

static void hp300_nmi_handler(int irq, void *dev_id, struct pt_regs *fp)
{
  extern void hp300_reset(void);
  printk("RESET pressed - self destruct sequence initiated.\n");
  hp300_reset();
}

int hp300_request_irq(unsigned int irq,
		void (*handler) (int, void *, struct pt_regs *),
		unsigned long flags, const char *devname, void *dev_id)
{
  return sys_request_irq(irq, handler, flags, devname, dev_id);
}

void hp300_free_irq(unsigned int irq, void *dev_id)
{
  sys_free_irq(irq, dev_id);
}

__initfunc(void hp300_init_IRQ(void))
{
  /* IPL6 - NMI (keyboard reset) */
  sys_request_irq(7, hp300_nmi_handler, IRQ_FLG_STD, "NMI", hp300_nmi_handler);
}
