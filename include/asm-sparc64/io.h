/* $Id: io.h,v 1.24 1999/09/06 01:17:54 davem Exp $ */
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


#define PCI_DVMA_HASHSZ	256

extern unsigned long pci_dvma_v2p_hash[PCI_DVMA_HASHSZ];
extern unsigned long pci_dvma_p2v_hash[PCI_DVMA_HASHSZ];

#define pci_dvma_ahashfn(addr)	(((addr) >> 24) & 0xff)

extern __inline__ unsigned long virt_to_phys(volatile void *addr)
{
	unsigned long vaddr = (unsigned long)addr;
	unsigned long off;

	/* Handle kernel variable pointers... */
	if (vaddr < PAGE_OFFSET)
		vaddr += PAGE_OFFSET - (unsigned long)&empty_zero_page;

	off = pci_dvma_v2p_hash[pci_dvma_ahashfn(vaddr - PAGE_OFFSET)];
	return vaddr + off;
}

extern __inline__ void *phys_to_virt(unsigned long addr)
{
	unsigned long paddr = addr & 0xffffffffUL;
	unsigned long off;

	off = pci_dvma_p2v_hash[pci_dvma_ahashfn(paddr)];
	return (void *)(paddr + off);
}

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/* Different PCI controllers we support have their PCI MEM space
 * mapped to an either 2GB (Psycho) or 4GB (Sabre) aligned area,
 * so need to chop off the top 33 or 32 bits.
 */
extern unsigned long pci_memspace_mask;

#define bus_dvma_to_mem(__vaddr) ((__vaddr) & pci_memspace_mask)

extern __inline__ unsigned int inb(unsigned long addr)
{
	unsigned int ret;

	__asm__ __volatile__("lduba [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));

	return ret;
}

extern __inline__ unsigned int inw(unsigned long addr)
{
	unsigned int ret;

	__asm__ __volatile__("lduha [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));

	return ret;
}

extern __inline__ unsigned int inl(unsigned long addr)
{
	unsigned int ret;

	__asm__ __volatile__("lduwa [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));

	return ret;
}

extern __inline__ void outb(unsigned char b, unsigned long addr)
{
	__asm__ __volatile__("stba %0, [%1] %2"
			     : /* no outputs */
			     : "r" (b), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));
}

extern __inline__ void outw(unsigned short w, unsigned long addr)
{
	__asm__ __volatile__("stha %0, [%1] %2"
			     : /* no outputs */
			     : "r" (w), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));
}

extern __inline__ void outl(unsigned int l, unsigned long addr)
{
	__asm__ __volatile__("stwa %0, [%1] %2"
			     : /* no outputs */
			     : "r" (l), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));
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
extern __inline__ unsigned int _readb(unsigned long addr)
{
	unsigned int ret;

	__asm__ __volatile__("lduba [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));

	return ret;
}

extern __inline__ unsigned int _readw(unsigned long addr)
{
	unsigned int ret;

	__asm__ __volatile__("lduha [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));

	return ret;
}

extern __inline__ unsigned int _readl(unsigned long addr)
{
	unsigned int ret;

	__asm__ __volatile__("lduwa [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));

	return ret;
}

extern __inline__ void _writeb(unsigned char b, unsigned long addr)
{
	__asm__ __volatile__("stba %0, [%1] %2"
			     : /* no outputs */
			     : "r" (b), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));
}

extern __inline__ void _writew(unsigned short w, unsigned long addr)
{
	__asm__ __volatile__("stha %0, [%1] %2"
			     : /* no outputs */
			     : "r" (w), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));
}

extern __inline__ void _writel(unsigned int l, unsigned long addr)
{
	__asm__ __volatile__("stwa %0, [%1] %2"
			     : /* no outputs */
			     : "r" (l), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L));
}

#define readb(__addr)		(_readb((unsigned long)(__addr)))
#define readw(__addr)		(_readw((unsigned long)(__addr)))
#define readl(__addr)		(_readl((unsigned long)(__addr)))
#define writeb(__b, __addr)	(_writeb((__b), (unsigned long)(__addr)))
#define writew(__w, __addr)	(_writew((__w), (unsigned long)(__addr)))
#define writel(__l, __addr)	(_writel((__l), (unsigned long)(__addr)))

#define IO_SPACE_LIMIT 0xffffffff

/*
 * Memcpy to/from I/O space is just a regular memory operation on
 * Ultra as well.
 */

/*
 * FIXME: Write faster routines using ASL_*L for this.
 */
static inline void *
memset_io(void *dst, int c, __kernel_size_t n)
{
	char *d = dst;

	while (n--)
		*d++ = c;

	return dst;
}

static inline void *
memcpy_fromio(void *dst, const void *src, __kernel_size_t n)
{
	const char *s = src;
	char *d = dst;

	while (n--)
		*d++ = *s++;

	return dst;
}

static inline void *
memcpy_toio(void *dst, const void *src, __kernel_size_t n)
{
	const char *s = src;
	char *d = dst;

	while (n--)
		*d++ = *s++;

	return dst;
}

#if 0 /* XXX Not exactly, we need to use ASI_*L from/to the I/O end,
       * XXX so these are disabled until we code that stuff.
       */
#define eth_io_copy_and_sum(a,b,c,d) eth_copy_and_sum((a),((char *)(b)),(c),(d))
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

/* On sparc64 we have the whole physical IO address space accessible
 * using physically addressed loads and stores, so this does nothing.
 */
#define ioremap(__offset, __size)	((void *)(__offset))
#define iounmap(__addr)			do { } while(0)

extern void sparc_ultra_mapioaddr(unsigned long physaddr,
				  unsigned long virt_addr,
				  int bus, int rdonly);
extern void sparc_ultra_unmapioaddr(unsigned long virt_addr);

extern __inline__ void mapioaddr(unsigned long physaddr,
				 unsigned long virt_addr,
				 int bus, int rdonly)
{
	sparc_ultra_mapioaddr(physaddr, virt_addr, bus, rdonly);
}

extern __inline__ void unmapioaddr(unsigned long virt_addr)
{
	sparc_ultra_unmapioaddr(virt_addr);
}

extern void *sparc_alloc_io(u32 pa, void *va, int sz, char *name,
			    u32 io, int rdonly);
extern void sparc_free_io (void *va, int sz);
extern void *sparc_dvma_malloc (int sz, char *name, __u32 *dvma_addr);

/* Nothing to do */

#define dma_cache_inv(_start,_size)		do { } while (0)
#define dma_cache_wback(_start,_size)		do { } while (0)
#define dma_cache_wback_inv(_start,_size)	do { } while (0)

#endif /* !(__SPARC64_IO_H) */
