/* $Id: pcicons.h,v 1.2 1997/08/24 12:13:11 ecd Exp $
 * pcicons.h: Stuff which is generic across all PCI console drivers.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef PCICONS_H
#define PCICONS_H

#include <linux/pci.h>
#include <asm/asi.h>
#include <asm/io.h>

extern unsigned long pcivga_iobase;
extern unsigned long pcivga_membase;

extern unsigned char vga_font[8192];

extern __inline__ unsigned int pcivga_inb(unsigned long off)
{
	return inb(pcivga_iobase + off);
}

extern __inline__ unsigned int pcivga_inw(unsigned long off)
{
	return inw(pcivga_iobase + off);
}

extern __inline__ unsigned int pcivga_inl(unsigned long off)
{
	return inl(pcivga_iobase + off);
}

extern __inline__ void pcivga_outb(unsigned char val, unsigned long off)
{
	outb(val, pcivga_iobase + off);
}

extern __inline__ void pcivga_outw(unsigned short val, unsigned long off)
{
	outw(val, pcivga_iobase + off);
}

extern __inline__ void pcivga_outl(unsigned int val, unsigned long off)
{
	outl(val, pcivga_iobase + off);
}

extern __inline__ unsigned int pcivga_readb(unsigned long off)
{
	return readb(pcivga_membase + off);
}

extern __inline__ unsigned int pcivga_readw(unsigned long off)
{
	return readw(pcivga_membase + off);
}

extern __inline__ unsigned int pcivga_readl(unsigned long off)
{
	return readl(pcivga_membase + off);
}

extern __inline__ void pcivga_writeb(unsigned char val, unsigned long off)
{
	writeb(val, pcivga_membase + off);
}

extern __inline__ void pcivga_writew(unsigned short val, unsigned long off)
{
	writew(val, pcivga_membase + off);
}

extern __inline__ void pcivga_writel(unsigned int val, unsigned long off)
{
	writel(val, pcivga_membase + off);
}

#endif /* PCICONS_H */
