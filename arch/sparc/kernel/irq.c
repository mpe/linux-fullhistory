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

#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/vaddrs.h>
#include <asm/clock.h>
#include <asm/openprom.h>

#define DEBUG_IRQ

void disable_irq(unsigned int irq_nr)
{
  unsigned long flags;
  unsigned char *int_reg;
  
  save_flags(flags);
  cli();

  /* We have mapped the irq enable register in head.S and all we
   * have to do here is frob the bits.
   */

  int_reg = (unsigned char *) IRQ_ENA_ADR;

  switch(irq_nr)
    {
    case 1:
      *int_reg = ((*int_reg) & (~(0x02)));
      break;
    case 4:
      *int_reg = ((*int_reg) & (~(0x04)));
      break;
    case 6:
      *int_reg = ((*int_reg) & (~(0x08)));
      break;      
    case 8:
      *int_reg = ((*int_reg) & (~(0x10)));
      break;      
    case 10:
      *int_reg = ((*int_reg) & (~(0x20)));
      break;      
    case 14:
      *int_reg = ((*int_reg) & (~(0x80)));
      break;      
    default:
      printk("AIEEE, Illegal interrupt disable requested irq=%d\n", 
	     (int) irq_nr);
      break;
    };
  
  restore_flags(flags);
  return;
}

void enable_irq(unsigned int irq_nr)
{
  unsigned long flags;
  unsigned char *int_reg;
  
  save_flags(flags);
  cli();

  /* We have mapped the irq enable register in head.S and all we
   * have to do here is frob the bits.
   */

  int_reg = (unsigned char *) IRQ_ENA_ADR;
  
#ifdef DEBUG_IRQ
  printk(" --- Enabling IRQ level %d ---\n", irq_nr);
#endif

  switch(irq_nr)
    {
    case 1:
      *int_reg = ((*int_reg) | 0x02);
      break;
    case 4:
      *int_reg = ((*int_reg) | 0x04);
      break;
    case 6:
      *int_reg = ((*int_reg) | 0x08);
      break;      
    case 8:
      *int_reg = ((*int_reg) | 0x10);
      break;      
    case 10:
      *int_reg = ((*int_reg) | 0x20);
      break;      
    case 14:
      *int_reg = ((*int_reg) | 0x80);
      break;      
    default:
      printk("AIEEE, Illegal interrupt enable requested irq=%d\n", 
	     (int) irq_nr);
      break;
    };

  restore_flags(flags);

  return;
}

/*
 * Initial irq handlers.
 */
struct irqaction {
  void (*handler)(int, struct pt_regs *);
  unsigned long flags;
  unsigned long mask;
  const char *name;
};

static struct irqaction irq_action[16] = {
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL }
};


int get_irq_list(char *buf)
{
  int i, len = 0;
  struct irqaction * action = irq_action;
  
  for (i = 0 ; i < 16 ; i++, action++) {
    if (!action->handler)
      continue;
    len += sprintf(buf+len, "%2d: %8d %c %s\n",
		   i, kstat.interrupts[i],
		   (action->flags & SA_INTERRUPT) ? '+' : ' ',
		   action->name);
  }
  return len;
}

void free_irq(unsigned int irq)
{
        struct irqaction * action = irq + irq_action;
        unsigned long flags;

        if (irq > 14) {  /* 14 irq levels on the sparc */
                printk("Trying to free IRQ %d\n", irq);
                return;
        }
        if (!action->handler) {
                printk("Trying to free free IRQ%d\n", irq);
                return;
        }
        save_flags(flags);
        cli();
        disable_irq(irq);
        action->handler = NULL;
        action->flags = 0;
        action->mask = 0;
        action->name = NULL;
        restore_flags(flags);
}

#if 0
static void handle_nmi(struct pt_regs * regs)
{
  printk("NMI, probably due to bus-parity error.\n");
  printk("PC=%08lx, SP=%08lx\n", regs->pc, regs->sp);
}
#endif

void unexpected_irq(int irq, struct pt_regs * regs)
{
        int i;

        printk("IO device interrupt, irq = %d\n", irq);
        printk("PC = %08lx NPC = %08lx SP=%08lx\n", regs->pc, 
	       regs->npc, regs->sp);
        printk("Expecting: ");
        for (i = 0; i < 16; i++)
                if (irq_action[i].handler)
                        printk("[%s:%d] ", irq_action[i].name, i);
        printk("AIEEE\n");
}

static inline void handler_irq(int irq, struct pt_regs * regs)
{
  struct irqaction * action = irq + irq_action;

  if (!action->handler) {
    unexpected_irq(irq, regs);
    return;
  }
  action->handler(irq, regs);
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
  struct irqaction *action = irq + irq_action;

  kstat.interrupts[irq]++;
  action->handler(irq, regs);
  return;
}

/*
 * Since we need to special things to clear up the clock chip around
 * the do_timer() call we have a special version of do_IRQ for the
 * level 14 interrupt which does these things.
 */

asmlinkage void do_sparc_timer(int irq, struct pt_regs * regs)
{
  struct irqaction *action = irq + irq_action;
  register volatile int clear;

  kstat.interrupts[irq]++;

  /* I do the following already in the entry code, better safe than
   * sorry for now. Reading the limit register clears the interrupt.
   */
  clear = TIMER_STRUCT->timer_limit14;

  action->handler(irq, regs);
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
  printk("Got FAST_IRQ number %04lx\n", (long unsigned int) irq);
  return;
}

extern int first_descent;
extern void probe_clock(int);
		
int request_irq(unsigned int irq, void (*handler)(int, struct pt_regs *),
	unsigned long irqflags, const char * devname)
{
  struct irqaction *action;
  unsigned long flags;

  if(irq > 14)  /* Only levels 1-14 are valid on the Sparc. */
    return -EINVAL;

  if(irq == 0)  /* sched_init() requesting the timer IRQ */
    {
      irq = 14;
      probe_clock(first_descent);
    }

  action = irq + irq_action;

  if(action->handler)
    return -EBUSY;

  if(!handler)
    return -EINVAL;

  save_flags(flags);

  cli();

  action->handler = handler;
  action->flags = irqflags;
  action->mask = 0;
  action->name = devname;

  enable_irq(irq);

  restore_flags(flags);

  return 0;
}

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
  return;
}
