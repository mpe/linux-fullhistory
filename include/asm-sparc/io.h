/* $Id: io.h,v 1.8 1995/11/25 02:31:50 davem Exp $ */
#ifndef __SPARC_IO_H
#define __SPARC_IO_H

#include <linux/kernel.h>

#include <asm/page.h>      /* IO address mapping routines need this */
#include <asm/system.h>

/*
 * Defines for io operations on the Sparc. Whether a memory access is going
 * to i/o sparc is encoded in the pte. The type bits determine whether this
 * is i/o sparc, on board memory, or VME space for VME cards. I think VME
 * space only works on sun4's
 */

extern inline unsigned long inb_local(unsigned long addr)
{
	return 0;
}

extern inline void outb_local(unsigned char b, unsigned long addr)
{
	return;
}

extern inline unsigned long inb(unsigned long addr)
{
	return 0;
}

extern inline unsigned long inw(unsigned long addr)
{
	return 0;
}

extern inline unsigned long inl(unsigned long addr)
{
	return 0;
}

extern inline void outb(unsigned char b, unsigned long addr)
{
	return;
}

extern inline void outw(unsigned short b, unsigned long addr)
{
	return;
}

extern inline void outl(unsigned int b, unsigned long addr)
{
	return;
}

/*
 * Memory functions
 */
extern inline unsigned long readb(unsigned long addr)
{
	return 0;
}

extern inline unsigned long readw(unsigned long addr)
{
	return 0;
}

extern inline unsigned long readl(unsigned long addr)
{
	return 0;
}

extern inline void writeb(unsigned short b, unsigned long addr)
{
	return;
}

extern inline void writew(unsigned short b, unsigned long addr)
{
	return;
}

extern inline void writel(unsigned int b, unsigned long addr)
{
	return;
}

#define inb_p inb
#define outb_p outb

extern void sun4c_mapioaddr(unsigned long, unsigned long, int bus_type, int rdonly);
extern void srmmu_mapioaddr(unsigned long, unsigned long, int bus_type, int rdonly);

extern inline void mapioaddr(unsigned long physaddr, unsigned long virt_addr,
			     int bus, int rdonly)
{
	switch(sparc_cpu_model) {
	case sun4c:
		sun4c_mapioaddr(physaddr, virt_addr, bus, rdonly);
		break;
	case sun4m:
	case sun4d:
	case sun4e:
		srmmu_mapioaddr(physaddr, virt_addr, bus, rdonly);
		break;
	default:
		printk("mapioaddr: Trying to map IO space for unsupported machine.\n");
		printk("mapioaddr: sparc_cpu_model = %d\n", sparc_cpu_model);
		printk("mapioaddr: Halting...\n");
		halt();
	};
	return;
}

extern void *sparc_alloc_io (void *, void *, int, char *, int, int);
extern void *sparc_dvma_malloc (int, char *);

#endif /* !(__SPARC_IO_H) */
