/* $Id: io.h,v 1.10 1997/04/10 05:13:29 davem Exp $ */
#ifndef __SPARC64_IO_H
#define __SPARC64_IO_H

#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/page.h>      /* IO address mapping routines need this */
#include <asm/system.h>

extern __inline__ unsigned long inb_local(unsigned long addr)
{
	return 0;
}

extern __inline__ void outb_local(unsigned char b, unsigned long addr)
{
	return;
}

extern __inline__ unsigned long inb(unsigned long addr)
{
	return 0;
}

extern __inline__ unsigned long inw(unsigned long addr)
{
	return 0;
}

extern __inline__ unsigned long inl(unsigned long addr)
{
	return 0;
}

extern __inline__ void outb(unsigned char b, unsigned long addr)
{
	return;
}

extern __inline__ void outw(unsigned short b, unsigned long addr)
{
	return;
}

extern __inline__ void outl(unsigned int b, unsigned long addr)
{
	return;
}

/*
 * Memory functions
 */
extern __inline__ unsigned long readb(unsigned long addr)
{
	return 0;
}

extern __inline__ unsigned long readw(unsigned long addr)
{
	return 0;
}

extern __inline__ unsigned long readl(unsigned long addr)
{
	return 0;
}

extern __inline__ void writeb(unsigned short b, unsigned long addr)
{
	return;
}

extern __inline__ void writew(unsigned short b, unsigned long addr)
{
	return;
}

extern __inline__ void writel(unsigned int b, unsigned long addr)
{
	return;
}

#define inb_p inb
#define outb_p outb

extern void sparc_ultra_mapioaddr   (unsigned long physaddr, unsigned long virt_addr,
				     int bus, int rdonly);
extern void sparc_ultra_unmapioaddr (unsigned long virt_addr);

extern __inline__ void mapioaddr (unsigned long physaddr, unsigned long virt_addr,
				  int bus, int rdonly)
{
	sparc_ultra_mapioaddr (physaddr, virt_addr, bus, rdonly);
}

extern __inline__ void unmapioaddr(unsigned long virt_addr)
{
	sparc_ultra_unmapioaddr (virt_addr);
}

extern void *sparc_alloc_io (u32 pa, void *va, int sz, char *name, u32 io, int rdonly);
extern void sparc_free_io (void *va, int sz);
extern void *sparc_dvma_malloc (int sz, char *name, __u32 *dvma_addr);

#define virt_to_phys(x) __pa((unsigned long)(x))
#define phys_to_virt(x) __va((unsigned long)(x))

#endif /* !(__SPARC64_IO_H) */
