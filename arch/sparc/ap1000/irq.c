  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
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
#include <asm/ap1000/apreg.h>

extern void ap_clear_clock_irq(void);
extern void ap_init_timers(void);

static void ap_enable_irq(unsigned int irq_nr)
{
  /* printk("ENABLE IRQ %d IGNORED\n",irq_nr); */
}

static void ap_disable_irq(unsigned int irq_nr)
{
  printk("DISABLE IRQ %d IGNORED\n",irq_nr);
}

static void ap_clear_profile_irq(void)
{
  MC_OUT(MC_INTR,AP_CLR_INTR_REQ << MC_INTR_ITIM0_SH);
}

static void ap_load_profile_irq(unsigned limit)
{
  MC_OUT(MC_ITIMER0,limit); 
}

void ap_init_IRQ(void)
{
  enable_irq = ap_enable_irq;
  disable_irq = ap_disable_irq;
  clear_clock_irq = ap_clear_clock_irq;
  clear_profile_irq = ap_clear_profile_irq;
  load_profile_irq = ap_load_profile_irq;
  init_timers = ap_init_timers;

  sti(); /* the sun4m code does this, so we do too */
}
