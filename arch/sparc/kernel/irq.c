/*  arch/sparc/kernel/irq.c:  Interrupt request handling routines. On the
 *                            Sparc the IRQ's are basically 'cast in stone'
 *                            and you are supposed to probe the prom's device
 *                            node trees to find out who's got which IRQ.
 *
 *  Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 *
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * The same sigaction struct is used, and with similar semantics (ie there
 * is a SA_INTERRUPT flag etc). Naturally it's not a 1:1 relation, but there
 * are similarities.
 *
 * sa_handler(int irq_NR) is the default function called (0 if no).
 * sa_mask is horribly ugly (I won't even mention it)
 * sa_flags contains various info: SA_INTERRUPT etc
 * sa_restorer is the unused
 */

#include <asm/ptrace.h>
#include <asm/system.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>

void disable_irq(unsigned int irq_nr)
{
  unsigned long flags;

  save_flags(flags);
  restore_flags(flags);
  return;
}

void enable_irq(unsigned int irq_nr)
{
  unsigned long flags;

  save_flags(flags);
  restore_flags(flags);
  return;
}

int get_irq_list(char *buf)
{
  int len = 0;

  return len;
}

/*
 * do_IRQ handles IRQ's that have been installed without the
 * SA_INTERRUPT flag: it uses the full signal-handling return
 * and runs with other interrupts enabled. All relatively slow
 * IRQ's should use this format: notably the keyboard/timer
 * routines.
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
  kstat.interrupts[irq]++;
  return;
}

/*
 * do_fast_IRQ handles IRQ's that don't need the fancy interrupt return
 * stuff - the handler is also running with interrupts disabled unless
 * it explicitly enables them later.
 */
asmlinkage void do_fast_IRQ(int irq)
{
  kstat.interrupts[irq]++;
  return;
}

#define SA_PROBE SA_ONESHOT

/*
 * Using "struct sigaction" is slightly silly, but there
 * are historical reasons and it works well, so..
 */
static int irqaction(unsigned int irq, struct sigaction * new_sa)
{
	unsigned long flags;

	save_flags(flags);
	restore_flags(flags);
	return 0;
}
		
int request_irq(unsigned int irq, void (*handler)(int),
	unsigned long flags, const char * devname)
{
	return irqaction(irq, (struct sigaction *) 0);
}

void free_irq(unsigned int irq)
{
  unsigned long flags;

  save_flags(flags);
  restore_flags(flags);
  return;
}

static void math_error_irq(int cpl)
{
  return;
}

static void no_action(int cpl) { }

unsigned int probe_irq_on (void)
{
  unsigned int irqs = 0;

  return irqs;
}

int probe_irq_off (unsigned int irqs)
{
  unsigned int i = 0;

  return i;
}

void init_IRQ(void)
{
  int i;

  for (i = 0; i < 16 ; i++)
    set_intr_gate(0x20+i,bad_interrupt[i]);

  return;
}
