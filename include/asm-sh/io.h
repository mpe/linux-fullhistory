#ifndef __ASM_SH_IO_H
#define __ASM_SH_IO_H
/* XXXXXXXXXXXXXXXXX */

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

extern __inline__ unsigned long readb(unsigned long addr)
{
       return *(volatile unsigned char*)addr;
}

extern __inline__ unsigned long readw(unsigned long addr)
{
       return *(volatile unsigned short*)addr;
}

extern __inline__ unsigned long readl(unsigned long addr)
{
       return *(volatile unsigned long*)addr;
}

extern __inline__ void writeb(unsigned short b, unsigned long addr)
{
       *(volatile unsigned char*)addr = b;
}

extern __inline__ void writew(unsigned short b, unsigned long addr)
{
       *(volatile unsigned short*)addr = b;
}

extern __inline__ void writel(unsigned int b, unsigned long addr)
{
        *(volatile unsigned long*)addr = b;
}

extern __inline__ unsigned long inb_local(unsigned long addr)
{
       return readb(addr);
}

extern __inline__ void outb_local(unsigned char b, unsigned long addr)
{
       return writeb(b,addr);
}

extern __inline__ unsigned long inb(unsigned long addr)
{
       return readb(addr);
}

extern __inline__ unsigned long inw(unsigned long addr)
{
       return readw(addr);
}

extern __inline__ unsigned long inl(unsigned long addr)
{
       return readl(addr);
}

extern __inline__ void outb(unsigned char b, unsigned long addr)
{
       return writeb(b,addr);
}

extern __inline__ void outw(unsigned short b, unsigned long addr)
{
       return writew(b,addr);
}

extern __inline__ void outl(unsigned int b, unsigned long addr)
{
       return writel(b,addr);
}

#define inb_p inb
#define outb_p outb

#ifdef __KERNEL__

#include <asm/addrspace.h>

/*
 * Change virtual addresses to physical addresses and vv.
 * These are trivial on the 1:1 Linux/SuperH mapping
 */
extern __inline__ unsigned long virt_to_phys(volatile void * address)
{
	return PHYSADDR(address);
}

extern __inline__ void * phys_to_virt(unsigned long address)
{
	return (void *)KSEG0ADDR(address);
}

extern void * ioremap(unsigned long phys_addr, unsigned long size);
extern void iounmap(void *addr);

/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the x86 architecture, we just read/write the
 * memory location directly.
 *
 * On SH, we have the whole physical address space mapped at all times
 * (as MIPS does), so "ioremap()" and "iounmap()" do not need to do
 * anything.  (This isn't true for all machines but we still handle
 * these cases with wired TLB entries anyway ...)
 *
 * We cheat a bit and always return uncachable areas until we've fixed
 * the drivers to handle caching properly.  
 */
extern __inline__ void * ioremap(unsigned long offset, unsigned long size)
{
	return (void *) KSEG1ADDR(offset);
}

/*
 * This one maps high address device memory and turns off caching for that area.
 * it's useful if some control registers are in such an area and write combining
 * or read caching is not desirable:
 */
extern __inline__ void * ioremap_nocache (unsigned long offset, unsigned long size)
{
	return (void *) KSEG1ADDR(offset);
}

extern __inline__ void iounmap(void *addr)
{
}

static __inline__ int check_signature(unsigned long io_addr,
			const unsigned char *signature, int length)
{
	int retval = 0;
	do {
		if (readb(io_addr) != *signature)
			goto out;
		io_addr++;
		signature++;
		length--;
	} while (length);
	retval = 1;
out:
	return retval;
}

/* Nothing to do */

#define dma_cache_inv(_start,_size)		do { } while (0)
#define dma_cache_wback(_start,_size)		do { } while (0)
#define dma_cache_wback_inv(_start,_size)	do { } while (0)

#endif /* __KERNEL__ */
#endif /* __ASM_SH_IO_H */
