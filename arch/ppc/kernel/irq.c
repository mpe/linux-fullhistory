/*
 *	linux/arch/ppc/kernel/irq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *      Adapted from arch/i386 by Gary Thomas
 *      Modified by Cort Dougan (cort@cs.nmt.edu) 
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

inline int get_irq_list(char *);
void check_irq(void);
void BeBox_CPU1(void);
void BeBox_state(void);
int BeBox_irq(void);
void show_BeBox_state(void);
void BeBox_enable_irq(int );
void BeBox_disable_irq(int );
void BeBox_init_IRQ(void);
void _do_bottom_half(void);
static _NOP(void);
static _delay(void);
void hard_disk_LED(int state);


#define SHOW_IRQ
#undef  SHOW_IRQ

/*
 * For the BeBox, interrupt numbers are 0..15 for 8259 PIC interrupts
 * and 16..31 for other BeBox motherboard type interrupts.
 */
 
unsigned long isBeBox[];
unsigned char *BeBox_IO_page;

static unsigned char cache_21 = 0xff;
static unsigned char cache_A1 = 0xff;

void disable_irq(unsigned int irq_nr)
{
	unsigned char mask;
	int s = _disable_interrupts();

	if (isBeBox[0] && (irq_nr >= 16))
	{
		BeBox_disable_irq(irq_nr);
	} else
	{
		mask = 1 << (irq_nr & 7);
		if (irq_nr < 8)
		{
			cache_21 |= mask;
			outb(cache_21,0x21);
		} else
		{
			cache_A1 |= mask;
			outb(cache_A1,0xA1);
		}
	}
	_enable_interrupts(s);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned char mask;
	int s = _disable_interrupts();

	if (isBeBox[0] && (irq_nr >= 16))
	{
		BeBox_enable_irq(irq_nr);
		_enable_interrupts(s);
		return;
	} else
	{
		mask = ~(1 << (irq_nr & 7));
		if (irq_nr < 8) {
			cache_21 &= mask;
			outb(cache_21,0x21);
		} else
		{
			cache_A1 &= mask;
			outb(cache_A1,0xA1);
		}
	}
	_enable_interrupts(s);
}

/*
 * Irq handlers.
 */
struct irq_action {
	void (*handler)(int, void *dev, struct pt_regs *);
	unsigned long flags;
	unsigned long mask;
	const char *name;
	int notified;
	void *dev_id;
};

static struct irq_action irq_action[32] = {
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL }
};


inline int get_irq_list(char *buf)
{
  int i, len = 0;
  struct irq_action * action = irq_action;
  
  for (i = 0;  i < 32;  i++, action++) {
    if (!action->handler)
      continue;
    len += sprintf(buf+len, "%2d: %8d %c %s\n",
		   i, kstat.interrupts[i],
		   (action->flags & SA_INTERRUPT) ? '+' : ' ',
		   action->name);
  }
  return len;
}

inline void
process_IRQ(int irq, int _irq, struct pt_regs *regs)
{
  struct irq_action *action;
  atomic_inc(&intr_count);
  if (irq < 16)
  {
    /* Mask interrupt */
    if (irq > 7)
    {
      cache_A1 |= (1<<(irq-8));
      outb(cache_A1, 0xA1);
    } else
    {
      cache_21 |= (1<<irq);
      outb(cache_21, 0x21);
    }
  }
  action = irq + irq_action;
  kstat.interrupts[irq]++;
  /* TEMP */
  /* On the Nobis, the keyboard interrupt "edge" gets lost - why? */
  if (irq == 0)
  {
    static int count;
    if (++count == 500)
    {
      if (inb(0x64) & 0x01)
      {
	struct irq_action *action;
	action = irq_action + 1;  /* Keyboard */
	printk("Reset KBD, KBSTAT = %x, ELCR = %x/%x, MASK = %x/%x\n",
	       inb(0x64), inb(0x4D0), inb(0x4D1), cache_21, cache_A1);
	action->handler(1, action->dev_id, regs);
      }
      count = 0;
    }
  }
  if (action->handler)
  {	
    action->handler(irq, action->dev_id, regs);
  } else
  {
    printk("Bogus interrupt %d/%x, pc %x regs %x\n",
	   irq, _irq,regs->nip,regs);
#if 0
    printk("BeBox[] = %x/%x\n", isBeBox[0], isBeBox[1]);
    show_BeBox_state();
    cnpause();
#endif
  }
  if (_disable_interrupts() && !action->notified)
  {
    action->notified = 1;
    printk("*** WARNING! %s handler [IRQ %d] turned interrupts on!\n",
	   action->name, irq);
  }
  if (irq < 16)
  {
    /* Issue EOI to interrupt controller */
    if (irq > 7)
    {
      outb(0xE0|(irq-8), 0xA0);
      outb(0xE2, 0x20);
    } else
    {
      outb(0xE0|irq, 0x20);
    }
    if (!(action->flags & SA_ONESHOT))
    {
      /* Re-enable interrupt */
      if (irq > 7)
      {
	cache_A1 &= ~(1<<(irq-8));
	outb(cache_A1, 0xA1);
      } else
      {
	cache_21 &= ~(1<<irq);
	outb(cache_21, 0x21);
      }
    }
  } else
  {
    BeBox_enable_irq(irq);
  }
  atomic_dec(&intr_count);
}

asmlinkage inline void handle_IRQ(struct pt_regs *regs)
{
  int irq, _irq, s;
  struct irq_action *action;
  static int _ints;
  
  if (!isBeBox[0] || ((irq = BeBox_irq()) < 16))
  {
    /* Figure out IRQ#, etc. */
    outb(0x0C, 0x20);  /* Poll interrupt controller */
    irq = _irq = inb(0x20);
    irq &= 0x07;  /* Caution! */
    if (irq == 2)
    { /* Cascaded interrupt -> IRQ8..IRQ15 */
      outb(0x0C, 0xA0);
      irq = (_irq = inb(0xA0)) & 0x07;
      irq += 8;
    }
  }
  process_IRQ(irq, _irq, regs);
  
  /* Sometimes, the cascaded IRQ controller get's "stuck" */
  if ((irq == 0) && (_ints++ == 100))
  {
    _ints = 0;
    outb(0x0A, 0xA0);  _irq = inb(0xA0);
    if (_irq & ~cache_A1)
    {  /* Figure out which IRQs are present */
      _irq &= ~cache_A1;
      for (irq = 0;  irq < 7;  irq++)
      {
	if (_irq & (1<<irq))
	{
#if 0
	  printk("Dropped IRQ #%d\n", irq+8);
#endif
	  process_IRQ(irq+8, _irq, regs);
	}
      }
    }
  }
}

/*
 * Display current IRQ state
 */

void
show_irq_state(void)
{
  unsigned char state_21, state_A1;
  outb(0x0A, 0x20);  state_21 = inb(0x20);
  outb(0x0A, 0xA0);  state_A1 = inb(0xA0);
  printk("IRQ State = %x/%x, Edge = %x/%x, Processor = %d\n", state_21, state_A1, inb(0x4D0), inb(0x4D1), _Processor);
}

/*
 * Initialize interrupt controllers to a well-known state.
 */

static void
reset_int_controllers(void)
{
	/* Initialize interrupt controllers */
	outb(0x11, 0x20); /* Start init sequence */
	outb(0x40, 0x21); /* Vector base */
	outb(0x04, 0x21); /* Cascade (slave) on IRQ2 */
	outb(0x01, 0x21); /* Select 8086 mode */
	outb(0xFF, 0x21); /* Mask all */
	outb(0x11, 0xA0); /* Start init sequence */
	outb(0x48, 0xA1); /* Vector base */
	outb(0x02, 0xA1); /* Cascade (slave) on IRQ2 */
	outb(0x01, 0xA1); /* Select 8086 mode */
	outb(0xFF, 0xA1); /* Mask all */
#if 0
	outb(0x00, 0x4D0); /* All edge triggered */
	outb(0xCF, 0x4D1); /* Trigger mode */
#endif
	outb(cache_A1, 0xA1);
	outb(cache_21, 0x21);
	enable_irq(2);  /* Enable cascade interrupt */
}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irq_action * action;
	unsigned long flags;

#ifdef SHOW_IRQ
if (irq) printk("Request IRQ #%d, Handler: %x\n", irq, handler);
#endif
	if (irq > 15)
	{
		if (!isBeBox[0] || (irq > 31))
			return -EINVAL;
	}
	action = irq + irq_action;
	if (action->handler)
		return -EBUSY;
	if (!handler)
		return -EINVAL;
	save_flags(flags);
	cli();
	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->dev_id = dev_id;
	enable_irq(irq);
	restore_flags(flags);
	return 0;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irq_action * action = irq + irq_action;
	unsigned long flags;

	if (irq > 31) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if (!action->handler) {
		printk("Trying to free free IRQ%d\n",irq);
		return;
	}
	disable_irq(irq);
	save_flags(flags);
	cli();
	action->handler = NULL;
	action->flags = 0;
	action->mask = 0;
	action->name = NULL;
	action->dev_id = NULL;
	restore_flags(flags);
}

#define SA_PROBE SA_ONESHOT

static void no_action(int irq, void *dev, struct pt_regs * regs)
{
#ifdef DEBUG
	printk("Probe got IRQ: %d\n", irq);
#endif
}

unsigned long probe_irq_on (void)
{
	unsigned int i, irqs = 0, irqmask;
	unsigned long delay;

	/* first, snaffle up any unassigned irqs */
	for (i = 15; i > 0; i--) {
		if (!request_irq(i, no_action, SA_PROBE, "probe", NULL)) {
			enable_irq(i);
			irqs |= (1 << i);
		}
	}

	/* wait for spurious interrupts to mask themselves out again */
	for (delay = jiffies + 2; delay > jiffies; );	/* min 10ms delay */

	/* now filter out any obviously spurious interrupts */
	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	for (i = 15; i > 0; i--) {
		if (irqs & (1 << i) & irqmask) {
			irqs ^= (1 << i);
			free_irq(i, NULL);
		}
	}
#ifdef DEBUG
	printk("probe_irq_on:  irqs=0x%04x irqmask=0x%04x\n", irqs, irqmask);
#endif
	return irqs;
}

int probe_irq_off (unsigned long irqs)
{
	unsigned int i, irqmask;

	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	for (i = 15; i > 0; i--) {
		if (irqs & (1 << i)) {
			free_irq(i, NULL);
		}
	}
#ifdef DEBUG
	printk("probe_irq_off: irqs=0x%04x irqmask=0x%04x\n", irqs, irqmask);
#endif
	irqs &= irqmask;
	if (!irqs)
		return 0;
	i = ffz(~irqs);
	if (irqs != (irqs & (1 << i)))
		i = -i;
	return i;
}
 
void init_IRQ(void)
{
  int i;

  if ((_get_PVR()>>16) == 1)  /* PPC 601 */
  { /* Nobis? */
    reset_int_controllers();
  }
#define TIMER0_COUNT 0x40
#define TIMER_CONTROL 0x43
  /* set the clock to 100 Hz */
  outb_p(0x34,TIMER_CONTROL);		/* binary, mode 2, LSB/MSB, ch 0 */
  outb_p(LATCH & 0xff , TIMER0_COUNT);	/* LSB */
  outb(LATCH >> 8 , TIMER0_COUNT);	/* MSB */
  if (request_irq(2, no_action, SA_INTERRUPT, "cascade", NULL))
    printk("Unable to get IRQ2 for cascade\n");
  request_region(0x20,0x20,"pic1");
  request_region(0xa0,0x20,"pic2");
  
  /* Make sure IRQ2 (cascade) interrupt is "level" based */
  outb(inb(0x4D0)|0x04, 0x4D0); /* IRQ2 level based */
  
  /* Set up PCI interrupts */
  route_PCI_interrupts();
  
  if (isBeBox[0])
  {
    BeBox_init_IRQ();
  }
}

/*
 * Wrapper for "bottom 1/2" of interrupt processing.  This routine
 * is called whenever an interrupt needs non-interrupt-time service.
 */

void _do_bottom_half(void)
{
  _enable_interrupts(1);
  do_bottom_half();
  _disable_interrupts();
}

void hard_disk_LED(int state)
{
  if (_Processor == _PROC_IBM) {
    outb(state, IBM_HDD_LED);
  }
}
 

/*
 * Support for interrupts on the BeBox
 */

#define CPU0_INT_MASK	(volatile unsigned long *)(BeBox_IO_page+0x0F0)
#define CPU1_INT_MASK	(volatile unsigned long *)(BeBox_IO_page+0x1F0)
#define INT_SOURCE	(volatile unsigned long *)(BeBox_IO_page+0x2F0)
#define CPU_RESET	(volatile unsigned long *)(BeBox_IO_page+0x4F0)

#define _CPU0_INT_MASK	(volatile unsigned long *)(0xA0000000+0x0F0)
#define _CPU1_INT_MASK	(volatile unsigned long *)(0xA0000000+0x1F0)
#define _INT_SOURCE	(volatile unsigned long *)(0xA0000000+0x2F0)
#define _CPU_RESET	(volatile unsigned long *)(0xA0000000+0x4F0)

#define CPU_HRESET	0x20000000
#define CPU_SRESET	0x40000000

#define SCSI_IRQ	16

#define INT_SCSI	(1<<21)
#define INT_8259	(1<<5)

/*
 * Map of pseudo IRQs to actual bits
 * Note: We give out IRQ #16..31 for all interrupt sources which are
 * not found in the 8259 PIC.
 */
 
unsigned long BeBox_IRQ_map[] =
   {
   	INT_SCSI,	/* 16 - SCSI */
   	0x00000000,	/* 17 - Unused */
   	0x00000000,	/* 18 - Unused */
   	0x00000000,	/* 19 - Unused */
   	0x00000000,	/* 20 - Unused */
   	0x00000000,	/* 21 - Unused */
   	0x00000000,	/* 22 - Unused */
   	0x00000000,	/* 23 - Unused */
   	0x00000000,	/* 24 - Unused */
   	0x00000000,	/* 25 - Unused */
   	0x00000000,	/* 26 - Unused */
   	0x00000000,	/* 27 - Unused */
   	0x00000000,	/* 28 - Unused */
   	0x00000000,	/* 29 - Unused */
   	0x00000000,	/* 30 - Unused */
   	0x00000000,	/* 31 - Unused */
   };

volatile int CPU1_alive;
volatile int CPU1_trace;

static
_NOP(void)
{
}

static
_delay(void)
{
	int i;
	for (i = 0;  i < 100;  i++) _NOP();
}

void
BeBox_init_IRQ(void)
{
	int tmr;
	volatile extern long BeBox_CPU1_vector;
	*CPU0_INT_MASK = 0x0FFFFFFC;  /* Clear all bits? */	
	*CPU0_INT_MASK = 0x80000003 | INT_8259;
	*CPU1_INT_MASK = 0x0FFFFFFC;  
printk("Start CPU #1 - CPU Status: %x\n", *CPU_RESET);
	BeBox_CPU1_vector = 0x0100;  /* Reset */
	tmr = 0;
	while (CPU1_alive == 0)
	{
		if (++tmr == 1000)
		{
printk("CPU #1 not there? - CPU Status: %x, Trace: %x\n", *CPU_RESET, CPU1_trace);
			break;
		}
		_delay();
	}
printk("CPU #1 running!\n");
}

void
BeBox_disable_irq(int irq)
{
	/* Note: this clears the particular bit */
	*CPU0_INT_MASK = BeBox_IRQ_map[irq-16];
}

void
BeBox_enable_irq(int irq)
{
	int s = _disable_interrupts();
	/* Sets a single bit */
#if 0	
printk("BeBox IRQ Mask = %x", *CPU0_INT_MASK);
#endif
	*CPU0_INT_MASK = 0x80000000 | BeBox_IRQ_map[irq-16];
#if 0
printk("/%x\n", *CPU0_INT_MASK);
#endif	
	_enable_interrupts(s);	
}

void
show_BeBox_state(void)
{
	unsigned long cpu0_int_mask;
	unsigned long int_state;
	cpu0_int_mask = (*CPU0_INT_MASK & 0x0FFFFFFC) & ~INT_8259;
	int_state = cpu0_int_mask & *INT_SOURCE;
	printk("Ints[%x] = %x, Mask[%x] = %x/%x, State = %x\n", INT_SOURCE, *INT_SOURCE, CPU0_INT_MASK, *CPU0_INT_MASK, cpu0_int_mask, int_state);
}

int
BeBox_irq(void)
{
	int i;
	unsigned long cpu0_int_mask;
	unsigned long int_state;
	cpu0_int_mask = (*CPU0_INT_MASK & 0x0FFFFFFC) & ~INT_8259;
	int_state = cpu0_int_mask & *INT_SOURCE;
	if (int_state)
	{ /* Determine the pseudo-interrupt # */
#if 0	
		printk("Ints[%x] = %x, Mask[%x] = %x/%x, State = %x\n", INT_SOURCE, *INT_SOURCE, CPU0_INT_MASK, *CPU0_INT_MASK, cpu0_int_mask, int_state);
#endif		
		for (i = 0;  i < 16;  i++)
		{
			if (BeBox_IRQ_map[i] & int_state)
			{
				return (i+16);
			}
		}
printk("Ints[%x] = %x, Mask[%x] = %x/%x, State = %x\n", INT_SOURCE, *INT_SOURCE, CPU0_INT_MASK, *CPU0_INT_MASK, cpu0_int_mask, int_state);
printk("Can't find BeBox IRQ!\n");
	}
	return (0);
}

void BeBox_state(void)
{
	printk("Int state = %x, CPU0 mask = %x, CPU1 mask = %x\n", *INT_SOURCE, *CPU0_INT_MASK, *CPU1_INT_MASK);
}

void BeBox_CPU1(void)
{
	CPU1_alive++;
	while (1) ;
}
