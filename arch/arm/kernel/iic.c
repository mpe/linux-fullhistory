/*
 * linux/arch/arm/kernel/iic.c
 *
 * Copyright (C) 1995, 1996 Russell King
 *
 * IIC is used to get the current time from the CMOS rtc.
 */

#include <linux/delay.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/ioc.h>

#define FORCE_ONES	0xdc

/*
 * if delay loop has been calibrated then us that,
 * else use IOC timer 1.
 */
static void iic_delay(void)
{
	extern unsigned long loops_per_sec;
	if (loops_per_sec != (1 << 12)) {
		udelay(100); /* was 10 */
		return;
	} else {
		unsigned long flags;
		save_flags_cli(flags);

		outb(254,  IOC_T1LTCHL);
		outb(255,  IOC_T1LTCHH);
		outb(0,    IOC_T1GO);
		outb(1<<6, IOC_IRQCLRA);			/* clear T1 irq */
		outb(10,   IOC_T1LTCHL); /* was 4 */
		outb(0,    IOC_T1LTCHH);
		outb(0,    IOC_T1GO);
		while ((inb(IOC_IRQSTATA) & (1<<6)) == 0);
		restore_flags(flags);
	}
}

#define IIC_INIT()		dat = (inb(IOC_CONTROL) | FORCE_ONES) & ~3
#define IIC_SET_DAT		outb(dat|=1, IOC_CONTROL);
#define IIC_CLR_DAT		outb(dat&=~1, IOC_CONTROL);
#define IIC_SET_CLK		outb(dat|=2, IOC_CONTROL);
#define IIC_CLR_CLK		outb(dat&=~2, IOC_CONTROL);
#define IIC_DELAY		iic_delay();
#define IIC_READ_DATA()		(inb(IOC_CONTROL) & 1)

static inline void iic_set_lines(int clk, int dat)
{
	int old;

	old = inb(IOC_CONTROL) | FORCE_ONES;

	old &= ~3;

	if (clk)
		old |= 2;
	if (dat)
		old |= 1;

	outb(old, IOC_CONTROL);

	iic_delay();
}

static inline unsigned int iic_read_data(void)
{
	return inb(IOC_CONTROL) & 1;
}

/*
 * C: ==~~_
 * D: =~~__
 */
static inline void iic_start(void)
{
	unsigned int dat;

	IIC_INIT();

	IIC_SET_DAT
	IIC_DELAY
	IIC_SET_CLK
	IIC_DELAY

	IIC_CLR_DAT
	IIC_DELAY
	IIC_CLR_CLK
	IIC_DELAY
}

/*
 * C: __~~
 * D: =__~
 */
static inline void iic_stop(void)
{
	unsigned int dat;

	IIC_INIT();

	IIC_CLR_DAT
	IIC_DELAY
	IIC_SET_CLK
	IIC_DELAY
	IIC_SET_DAT
	IIC_DELAY
}

/*
 * C: __~_
 * D: =___
 */
static inline void iic_acknowledge(void)
{
	unsigned int dat;

	IIC_INIT();

	IIC_CLR_DAT
	IIC_DELAY
	IIC_SET_CLK
	IIC_DELAY
	IIC_CLR_CLK
	IIC_DELAY
}

/*
 * C: __~_
 * D: =~H~
 */
static inline int iic_is_acknowledged(void)
{
	unsigned int dat, ack_bit;

	IIC_INIT();

	IIC_SET_DAT
	IIC_DELAY
	IIC_SET_CLK
	IIC_DELAY

	ack_bit = IIC_READ_DATA();

	IIC_CLR_CLK
	IIC_DELAY

	return ack_bit == 0;
}

/*
 * C: _~__~__~__~__~__~__~__~_
 * D: =DDXDDXDDXDDXDDXDDXDDXDD
 */
static void iic_sendbyte(unsigned int b)
{
	unsigned int dat, i;

	IIC_INIT();

	for (i = 0; i < 8; i++) {
		if (b & 128)
			IIC_SET_DAT
		else
			IIC_CLR_DAT
		IIC_DELAY

		IIC_SET_CLK
		IIC_DELAY
		IIC_CLR_CLK
		IIC_DELAY

		b <<= 1;
	}
}

/*
 * C: __~_~_~_~_~_~_~_~_
 * D: =~HHHHHHHHHHHHHHHH
 */
static unsigned char iic_recvbyte(void)
{
	unsigned int dat, i, in;

	IIC_INIT();

	IIC_SET_DAT
	IIC_DELAY

	in = 0;
	for (i = 0; i < 8; i++) {
		IIC_SET_CLK
		IIC_DELAY

		in = (in << 1) | IIC_READ_DATA();

		IIC_CLR_CLK
		IIC_DELAY
	}

	return in;
}

int iic_control (unsigned char addr, unsigned char loc, unsigned char *buf, int len)
{
	int i, err = -EIO;

	iic_start();
	iic_sendbyte(addr & 0xfe);
	if (!iic_is_acknowledged())
		goto error;

	iic_sendbyte(loc);
	if (!iic_is_acknowledged())
		goto error;

	if (addr & 1) {
		iic_stop();
		iic_start();
		iic_sendbyte(addr|1);
		if (!iic_is_acknowledged())
			goto error;

		for (i = 0; i < len - 1; i++) {
			buf[i] = iic_recvbyte();
			iic_acknowledge();
		}
		buf[i] = iic_recvbyte();
	} else {
		for (i = 0; i < len; i++) {
			iic_sendbyte(buf[i]);

			if (!iic_is_acknowledged())
				goto error;
		}
	}

	err = 0;
error:
	iic_stop();

	return err;
}
