/*
 * linux/arch/arm/kernel/iic.c
 *
 * Copyright (C) 1995, 1996 Russell King
 *
 * IIC is used to get the current time from the CMOS rtc.
 */

#include <linux/delay.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/hardware.h>

/*
 * if delay loop has been calibrated then us that,
 * else use IOC timer 1.
 */
static void iic_delay (void)
{
	extern unsigned long loops_per_sec;
	if (loops_per_sec != (1 << 12)) {
		udelay(10);
		return;
	} else {
		unsigned long flags;
		save_flags_cli(flags);

		outb(254,  IOC_T1LTCHL);
		outb(255,  IOC_T1LTCHH);
		outb(0,    IOC_T1GO);
		outb(1<<6, IOC_IRQCLRA);			/* clear T1 irq */
		outb(4,    IOC_T1LTCHL);
		outb(0,    IOC_T1LTCHH);
		outb(0,    IOC_T1GO);
		while ((inb(IOC_IRQSTATA) & (1<<6)) == 0);
		restore_flags(flags);
	}
}

static inline void iic_start (void)
{
	unsigned char out;

	out = inb(IOC_CONTROL) | 0xc2;

	outb(out, IOC_CONTROL);
	iic_delay();

	outb(out ^ 1, IOC_CONTROL);
	iic_delay();
}

static inline void iic_stop (void)
{
	unsigned char out;

	out = inb(IOC_CONTROL) | 0xc3;

	iic_delay();
	outb(out ^ 1, IOC_CONTROL);

	iic_delay();
	outb(out, IOC_CONTROL);
}

static int iic_sendbyte (unsigned char b)
{
	unsigned char out, in;
	int i;

	out = (inb(IOC_CONTROL) & 0xfc) | 0xc0;

	outb(out, IOC_CONTROL);
	for (i = 7; i >= 0; i--) {
		unsigned char c;
		c = out | ((b & (1 << i)) ? 1 : 0);

		outb(c, IOC_CONTROL);
		iic_delay();

		outb(c | 2, IOC_CONTROL);
		iic_delay();

		outb(c, IOC_CONTROL);
	}
	outb(out | 1, IOC_CONTROL);
	iic_delay();

	outb(out | 3, IOC_CONTROL);
	iic_delay();

	in = inb(IOC_CONTROL) & 1;

	outb(out | 1, IOC_CONTROL);
	iic_delay();

	outb(out, IOC_CONTROL);
	iic_delay();

	if(in) {
		printk("No acknowledge from RTC\n");
		return 1;
	} else
		return 0;
}

static unsigned char iic_recvbyte (void)
{
	unsigned char out, in;
	int i;

	out = (inb(IOC_CONTROL) & 0xfc) | 0xc0;

	outb(out, IOC_CONTROL);
	in = 0;
	for (i = 7; i >= 0; i--) {
		outb(out | 1, IOC_CONTROL);
		iic_delay();
		outb(out | 3, IOC_CONTROL);
		iic_delay();
		in = (in << 1) | (inb(IOC_CONTROL) & 1);
		outb(out | 1, IOC_CONTROL);
		iic_delay();
	}
	outb(out, IOC_CONTROL);
	iic_delay();
	outb(out | 2, IOC_CONTROL);
	iic_delay();

	return in;
}

void iic_control (unsigned char addr, unsigned char loc, unsigned char *buf, int len)
{
	iic_start();

	if (iic_sendbyte(addr & 0xfe))
		goto error;

	if (iic_sendbyte(loc))
		goto error;

	if (addr & 1) {
		int i;

		for (i = 0; i < len; i++)
			if (iic_sendbyte (buf[i]))
				break;
	} else {
		int i;

		iic_stop();
		iic_start();
		iic_sendbyte(addr|1);
		for (i = 0; i < len; i++)
			buf[i] = iic_recvbyte ();
	}
error:
	iic_stop();
}
