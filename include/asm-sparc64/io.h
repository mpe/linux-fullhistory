/* $Id: io.h,v 1.3 1997/03/03 16:51:53 jj Exp $ */
#ifndef __SPARC64_IO_H
#define __SPARC64_IO_H

#include <linux/kernel.h>

#include <asm/page.h>      /* IO address mapping routines need this */
#include <asm/system.h>

extern void sparc_ultra_mapioaddr   (unsigned long physaddr, unsigned long virt_addr, int bus, int rdonly);
extern void sparc_ultra_unmapioaddr (unsigned long virt_addr);

extern __inline__ void mapioaddr (unsigned long physaddr, unsigned long virt_addr, int bus, int rdonly)
{
	sparc_ultra_mapioaddr (physaddr, virt_addr, bus, rdonly);
	return;
}

extern __inline__ void unmapioaddr(unsigned long virt_addr)
{
	sparc_ultra_unmapioaddr (virt_addr);
	return;
}

extern void *sparc_alloc_io (void *, void *, int, char *, int, int);
extern void sparc_free_io (void *, int);
extern void *sparc_dvma_malloc (int, char *);

#define virt_to_phys(x) __pa((unsigned long)(x))
#define phys_to_virt(x) __va((unsigned long)(x))

#endif /* !(__SPARC64_IO_H) */
