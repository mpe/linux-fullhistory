/* $Id: io.h,v 1.12 1997/08/19 03:11:52 davem Exp $ */
#ifndef __SPARC64_IO_H
#define __SPARC64_IO_H

#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/page.h>      /* IO address mapping routines need this */
#include <asm/system.h>
#include <asm/asi.h>

/* PC crapola... */
#define __SLOW_DOWN_IO	do { } while (0)
#define SLOW_DOWN_IO	do { } while (0)

extern __inline__ unsigned long virt_to_phys(volatile void *addr)
{
	return ((((unsigned long)addr) - PAGE_OFFSET) | 0x80000000UL);
}

extern __inline__ void *phys_to_virt(unsigned long addr)
{
	return ((void *)((addr & ~0x80000000) + PAGE_OFFSET));
}

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

extern __inline__ unsigned int inb(unsigned long addr)
{
	unsigned int ret;

	__asm__ __volatile__("lduba [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PL));

	return ret;
}

extern __inline__ unsigned int inw(unsigned long addr)
{
	unsigned int ret;

	__asm__ __volatile__("lduha [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PL));

	return ret;
}

extern __inline__ unsigned int inl(unsigned long addr)
{
	unsigned int ret;

	__asm__ __volatile__("lduwa [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PL));

	return ret;
}

extern __inline__ void outb(unsigned char b, unsigned long addr)
{
	__asm__ __volatile__("stba %0, [%1] %2"
			     : /* no outputs */
			     : "r" (b), "r" (addr), "i" (ASI_PL));
}

extern __inline__ void outw(unsigned short w, unsigned long addr)
{
	__asm__ __volatile__("stha %0, [%1] %2"
			     : /* no outputs */
			     : "r" (w), "r" (addr), "i" (ASI_PL));
}

extern __inline__ void outl(unsigned int l, unsigned long addr)
{
	__asm__ __volatile__("stwa %0, [%1] %2"
			     : /* no outputs */
			     : "r" (l), "r" (addr), "i" (ASI_PL));
}

#define inb_p inb
#define outb_p outb

extern void outsb(unsigned long addr, const void *src, unsigned long count);
extern void outsw(unsigned long addr, const void *src, unsigned long count);
extern void outsl(unsigned long addr, const void *src, unsigned long count);
extern void insb(unsigned long addr, void *dst, unsigned long count);
extern void insw(unsigned long addr, void *dst, unsigned long count);
extern void insl(unsigned long addr, void *dst, unsigned long count);

/* Memory functions, same as I/O accesses on Ultra. */
#define readb(addr)		inb(addr)
#define readw(addr)		inw(addr)
#define readl(addr)		inl(addr)
#define writeb(b, addr)		outb((b), (addr))
#define writew(w, addr)		outw((w), (addr))
#define writel(l, addr)		outl((l), (addr))

/* Memcpy to/from I/O space is just a regular memory operation on Ultra as well. */

#if 0 /* XXX Not exactly, we need to use ASI_*L from/to the I/O end,
       * XXX so these are disabled until we code that stuff.
       */
#define memset_io(a,b,c)		memset(((char *)(a)),(b),(c))
#define memcpy_fromio(a,b,c)		memcpy((a),((char *)(b)),(c))
#define memcpy_toio(a,b,c)		memcpy(((char *)(a)),(b),(c))
#define eth_io_copy_and_sum(a,b,c,d)	eth_copy_and_sum((a),((char *)(b)),(c),(d))
#endif

static inline int check_signature(unsigned long io_addr,
				  const unsigned char *signature,
				  int length)
{
	int retval = 0;
	do {
		if (readb(io_addr++) != *signature++)
			goto out;
	} while (--length);
	retval = 1;
out:
	return retval;
}

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

#endif /* !(__SPARC64_IO_H) */
