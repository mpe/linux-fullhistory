/*
 * linux/include/asm-arm/arch-nexuspci/time.h
 *
 * Copyright (c) 1997 Phil Blundell.
 *
 * Nexus PCI card has no real-time clock.  We get timer ticks from the
 * SCC chip.
 */

#define UART_BASE		0xfff00000
#define INTCONT			0xffe00000

static void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	static int count = 50;

	writeb(0x90, UART_BASE + 8);

	if (--count == 0) {
		static int state = 1;
		state ^= 1;
		writeb(0x1a + state, INTCONT);
		count = 50;
	}

	readb(UART_BASE + 0x14);
	readb(UART_BASE + 0x14);
	readb(UART_BASE + 0x14);
	readb(UART_BASE + 0x14);
	readb(UART_BASE + 0x14);
	readb(UART_BASE + 0x14);

	do_timer(regs);	
}

extern __inline__ void setup_timer(void)
{
	int tick = 3686400 / 16 / 2 / 100;

	writeb(tick & 0xff, UART_BASE + 0x1c);
	writeb(tick >> 8, UART_BASE + 0x18);
	writeb(0x80, UART_BASE + 8);
	writeb(0x10, UART_BASE + 0x14);

	timer_irq.handler = timer_interrupt;

	setup_arm_irq(IRQ_TIMER, &timer_irq);
}
