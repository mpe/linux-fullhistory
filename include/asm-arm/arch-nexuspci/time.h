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

#define update_rtc()

extern __inline__ unsigned long gettimeoffset (void)
{
	return 0;
}

extern __inline__ int reset_timer (void)
{
	static int count = 50;
	writeb(0x90, UART_BASE + 8);
	if (--count == 0)
	{
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
	return 1;
}

extern __inline__ unsigned long setup_timer (void)
{
	int tick = 3686400 / 16 / 2 / 100;
	writeb(tick & 0xff, UART_BASE + 0x1c);
	writeb(tick >> 8, UART_BASE + 0x18);
	writeb(0x80, UART_BASE + 8);
	writeb(0x10, UART_BASE + 0x14);
	/*
	 * Default the date to 1 Jan 1970 0:0:0
	 * You will have to run a time daemon to set the
	 * clock correctly at bootup
	 */
	return mktime(1970, 1, 1, 0, 0, 0);
}
