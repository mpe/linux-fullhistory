/*
 *	$Id: io_hd64461.c,v 1.1 2000/06/10 21:45:18 yaegashi Exp $
 *	Copyright (C) 2000 YAEGASHI Takeshi
 *	Typical I/O routines for HD64461 system.
 */

#include <linux/config.h>
#include <asm/io.h>
#include <asm/hd64461.h>

static __inline__ unsigned long PORT2ADDR(unsigned long port)
{
	/* HD64461 internal devices (0xb0000000) */
	if (port < 0x10000) return CONFIG_HD64461_IOBASE + port;

	/* PCMCIA channel 0, I/O (0xba000000) */
	if (port < 0x20000) return 0xba000000 + port - 0x10000;

	/* PCMCIA channel 1, memory (0xb5000000)
	   SH7709 cannot support I/O card attached to Area 5 */
	if (port < 0x30000) return 0xb5000000 + port - 0x20000;

	/* Whole physical address space (0xa0000000) */
	return 0xa0000000 + (port & 0x1fffffff);
}

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
