/* interrupt.h */
#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <linux/kernel.h>
#include <asm/bitops.h>

struct irqaction {
	void (*handler)(int, void *, struct pt_regs *);
	unsigned long flags;
	unsigned long mask;
	const char *name;
	void *dev_id;
	struct irqaction *next;
};

extern unsigned long intr_count;

extern int bh_mask_count[32];
extern unsigned long bh_active;
extern unsigned long bh_mask;
extern void (*bh_base[32])(void);

asmlinkage void do_bottom_half(void);

/* Who gets which entry in bh_base.  Things which will occur most often
   should come first - in which case NET should be up the top with SERIAL/TQUEUE! */
   
enum {
	TIMER_BH = 0,
	CONSOLE_BH,
	TQUEUE_BH,
	DIGI_BH,
	SERIAL_BH,
	RISCOM8_BH,
	BAYCOM_BH,
	NET_BH,
	IMMEDIATE_BH,
	KEYBOARD_BH,
	CYCLADES_BH,
	CM206_BH
};

extern inline void init_bh(int nr, void (*routine)(void))
{
	bh_base[nr] = routine;
	bh_mask_count[nr] = 0;
	bh_mask |= 1 << nr;
}

extern inline void mark_bh(int nr)
{
	set_bit(nr, &bh_active);
}

/*
 * These use a mask count to correctly handle
 * nested disable/enable calls
 */
extern inline void disable_bh(int nr)
{
	bh_mask &= ~(1 << nr);
	bh_mask_count[nr]++;
}

extern inline void enable_bh(int nr)
{
	if (!--bh_mask_count[nr])
		bh_mask |= 1 << nr;
}

/*
 * start_bh_atomic/end_bh_atomic also nest
 * naturally by using a counter
 */
extern inline void start_bh_atomic(void)
{
	intr_count++;
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	intr_count--;
}

/*
 * Autoprobing for irqs:
 *
 * probe_irq_on() and probe_irq_off() provide robust primitives
 * for accurate IRQ probing during kernel initialization.  They are
 * reasonably simple to use, are not "fooled" by spurious interrupts,
 * and, unlike other attempts at IRQ probing, they do not get hung on
 * stuck interrupts (such as unused PS2 mouse interfaces on ASUS boards).
 *
 * For reasonably foolproof probing, use them as follows:
 *
 * 1. clear and/or mask the device's internal interrupt.
 * 2. sti();
 * 3. irqs = probe_irq_on();      // "take over" all unassigned idle IRQs
 * 4. enable the device and cause it to trigger an interrupt.
 * 5. wait for the device to interrupt, using non-intrusive polling or a delay.
 * 6. irq = probe_irq_off(irqs);  // get IRQ number, 0=none, negative=multiple
 * 7. service the device to clear its pending interrupt.
 * 8. loop again if paranoia is required.
 *
 * probe_irq_on() returns a mask of allocated irq's.
 *
 * probe_irq_off() takes the mask as a parameter,
 * and returns the irq number which occurred,
 * or zero if none occurred, or a negative irq number
 * if more than one irq occurred.
 */
extern unsigned long probe_irq_on(void);	/* returns 0 on failure */
extern int probe_irq_off(unsigned long);	/* returns 0 or negative on failure */

#endif
