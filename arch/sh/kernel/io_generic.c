/* $Id: io_generic.c,v 1.3 2000/05/07 23:31:58 gniibe Exp $
 *
 * linux/arch/sh/kernel/io_generic.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 *
 * Generic I/O routine.
 *
 */

#include <linux/config.h>
#include <asm/io.h>

#define PORT2ADDR(x) (CONFIG_IOPORT_START+(x))

static inline void delay(void)
{
	ctrl_inw(0xa0000000);
}

unsigned long inb(unsigned int port)
{
	return *(volatile unsigned char*)PORT2ADDR(port);
}

unsigned long inb_p(unsigned int port)
{
	unsigned long v = *(volatile unsigned char*)PORT2ADDR(port);

	delay();
	return v;
}

unsigned long inw(unsigned int port)
{
	return *(volatile unsigned short*)PORT2ADDR(port);
}

unsigned long inl(unsigned int port)
{
	return *(volatile unsigned long*)PORT2ADDR(port);
}

void insb(unsigned int port, void *buffer, unsigned long count)
{
	unsigned char *buf=buffer;
	while(count--) *buf++=inb(port);
}

void insw(unsigned int port, void *buffer, unsigned long count)
{
	unsigned short *buf=buffer;
	while(count--) *buf++=inw(port);
}

void insl(unsigned int port, void *buffer, unsigned long count)
{
	unsigned long *buf=buffer;
	while(count--) *buf++=inl(port);
}

void outb(unsigned long b, unsigned int port)
{
	*(volatile unsigned char*)PORT2ADDR(port) = b;
}

void outb_p(unsigned long b, unsigned int port)
{
	*(volatile unsigned char*)PORT2ADDR(port) = b;
	delay();
}

void outw(unsigned long b, unsigned int port)
{
	*(volatile unsigned short*)PORT2ADDR(port) = b;
}

void outl(unsigned long b, unsigned int port)
{
        *(volatile unsigned long*)PORT2ADDR(port) = b;
}

void outsb(unsigned int port, const void *buffer, unsigned long count)
{
	const unsigned char *buf=buffer;
	while(count--) outb(*buf++, port);
}

void outsw(unsigned int port, const void *buffer, unsigned long count)
{
	const unsigned short *buf=buffer;
	while(count--) outw(*buf++, port);
}

void outsl(unsigned int port, const void *buffer, unsigned long count)
{
	const unsigned long *buf=buffer;
	while(count--) outl(*buf++, port);
}
