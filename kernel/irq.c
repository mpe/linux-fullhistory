/*
 *	linux/kernel/irq.c
 *
 *	(C) 1992 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * The same sigaction struct is used, and with similar semantics (ie there
 * is a SA_INTERRUPT flag etc). Naturally it's not a 1:1 relation, but there
 * are similarities.
 *
 * sa_handler(int irq_NR) is the default function called.
 * sa_mask is 0 if nothing uses this IRQ
 * sa_flags contains various info: SA_INTERRUPT etc
 * sa_restorer is the unused
 */

#include <signal.h>
#include <errno.h>

#include <sys/ptrace.h>

#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>

struct sigaction irq_sigaction[16] = {
	{ NULL, 0, 0, NULL },
};

void irq13(void);

/*
 * This builds up the IRQ handler stubs using some ugly macros in irq.h
 *
 * These macros create the low-level assembly IRQ routines that do all
 * the operations that are needed to keep the AT interrupt-controller
 * happy. They are also written to be fast - and to disable interrupts
 * as little as humanly possible.
 */
BUILD_IRQ(FIRST,0,0x01)
BUILD_IRQ(FIRST,1,0x02)
BUILD_IRQ(FIRST,2,0x04)
BUILD_IRQ(FIRST,3,0x08)
BUILD_IRQ(FIRST,4,0x10)
BUILD_IRQ(FIRST,5,0x20)
BUILD_IRQ(FIRST,6,0x40)
BUILD_IRQ(FIRST,7,0x80)
BUILD_IRQ(SECOND,8,0x01)
BUILD_IRQ(SECOND,9,0x02)
BUILD_IRQ(SECOND,10,0x04)
BUILD_IRQ(SECOND,11,0x08)
BUILD_IRQ(SECOND,12,0x10)
BUILD_IRQ(SECOND,13,0x20)
BUILD_IRQ(SECOND,14,0x40)
BUILD_IRQ(SECOND,15,0x80)

/*
 * This routine gets called at every IRQ request. Interrupts
 * are enabled, the interrupt has been accnowledged and this
 * particular interrupt is disabled when this is called.
 *
 * The routine has to call the appropriate handler (disabling
 * interrupts if needed first). If no handler exists, we return
 * an error value, telling the low-level IRQ routines not to
 * re-enable this IRQ line.
 *
 * Note similarities on a very low level between this and the
 * do_signal() function. Naturally this is simplified, but they
 * get similar arguments, use them similarly etc... Note that
 * unlike the signal-handlers, the IRQ-handlers don't get the IRQ
 * (signal) number as argument, but the cpl value at the time of
 * the interrupt.
 */
int do_IRQ(int irq, struct pt_regs * regs)
{
	struct sigaction * sa = irq + irq_sigaction;
	void (*handler)(int);
	unsigned int esp;

	if (!(handler = sa->sa_handler))
		return -1;	/* the irq isn't re-enabled */
	__asm__ __volatile__("movl %%esp,%0":"=r" (esp));
	if (esp < 200+(unsigned long)(current+1)) {
		printk("Stack overflow on IRQ%d: shutting down\n",irq);
		return -1;
	}
	if (sa->sa_flags & SA_INTERRUPT)
		cli();
	handler(regs->cs & 3);
	sti();
	return 0;		/* re-enable the irq when returning */
}

int irqaction(unsigned int irq, struct sigaction * new)
{
	struct sigaction * sa;
	unsigned long flags;

	if (irq > 15)
		return -EINVAL;
	if (irq == 2)
		irq = 9;
	sa = irq + irq_sigaction;
	if (sa->sa_mask)
		return -EBUSY;
	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
	*sa = *new;
	sa->sa_mask = 1;
	if (irq < 8)
		outb(inb_p(0x21) & ~(1<<irq),0x21);
	else
		outb(inb_p(0xA1) & ~(1<<(irq-8)),0xA1);
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
	return 0;
}
		
int request_irq(unsigned int irq, void (*handler)(int))
{
	struct sigaction sa;

	sa.sa_handler = handler;
	sa.sa_flags = 0;
	sa.sa_mask = 0;
	sa.sa_restorer = NULL;
	return irqaction(irq,&sa);
}

void free_irq(unsigned int irq)
{
	struct sigaction * sa = irq + irq_sigaction;
	unsigned long flags;

	if (irq > 15) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if (!sa->sa_mask) {
		printk("Trying to free free IRQ%d\n",irq);
		return;
	}
	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
	if (irq < 8)
		outb(inb_p(0x21) | (1<<irq),0x21);
	else
		outb(inb_p(0xA1) | (1<<(irq-8)),0xA1);
	sa->sa_handler = NULL;
	sa->sa_flags = 0;
	sa->sa_mask = 0;
	sa->sa_restorer = NULL;
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}

extern void math_error(void);

static void math_error_irq(int cpl)
{
	outb(0,0xF0);
	math_error();
}

void init_IRQ(void)
{
	set_trap_gate(0x20,IRQ0_interrupt);
	set_trap_gate(0x21,IRQ1_interrupt);
	set_trap_gate(0x22,IRQ2_interrupt);
	set_trap_gate(0x23,IRQ3_interrupt);
	set_trap_gate(0x24,IRQ4_interrupt);
	set_trap_gate(0x25,IRQ5_interrupt);
	set_trap_gate(0x26,IRQ6_interrupt);
	set_trap_gate(0x27,IRQ7_interrupt);
	set_trap_gate(0x28,IRQ8_interrupt);
	set_trap_gate(0x29,IRQ9_interrupt);
	set_trap_gate(0x2a,IRQ10_interrupt);
	set_trap_gate(0x2b,IRQ11_interrupt);
	set_trap_gate(0x2c,IRQ12_interrupt);
	set_trap_gate(0x2d,IRQ13_interrupt);
	set_trap_gate(0x2e,IRQ14_interrupt);
	set_trap_gate(0x2f,IRQ15_interrupt);
	if (request_irq(13,math_error_irq))
		printk("Unable to get IRQ13 for math-error handler\n");
}
