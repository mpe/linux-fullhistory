#ifndef __ASM_SH_IO_H
#define __ASM_SH_IO_H

/*
 * Convention:
 *    read{b,w,l}/write{b,w,l} are for PCI,
 *    while in{b,w,l}/out{b,w,l} are for ISA
 * These may (will) be platform specific function.
 *
 * In addition, we have 
 *   ctrl_in{b,w,l}/ctrl_out{b,w,l} for SuperH specific I/O.
 *   which are processor specific.
 */

#include <asm/cache.h>

#define inw_p	inw
#define outw_p	outw

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

extern __inline__ void writeb(unsigned char b, unsigned long addr)
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

extern unsigned long inb(unsigned int port);
extern unsigned long inb_p(unsigned int port);
extern unsigned long inw(unsigned int port);
extern unsigned long inl(unsigned int port);
extern void insb(unsigned int port, void *addr, unsigned long count);
extern void insw(unsigned int port, void *addr, unsigned long count);
extern void insl(unsigned int port, void *addr, unsigned long count);

extern void outb(unsigned long value, unsigned int port);
extern void outb_p(unsigned long value, unsigned int port);
extern void outw(unsigned long value, unsigned int port);
extern void outl(unsigned long value, unsigned int port);
extern void outsb(unsigned int port, const void *addr, unsigned long count);
extern void outsw(unsigned int port, const void *addr, unsigned long count);
extern void outsl(unsigned int port, const void *addr, unsigned long count);

/*
 * If the platform has PC-like I/O, this function gives us the address
 * from the offset.
 */
extern unsigned long sh_isa_slot(unsigned long offset);

#define isa_readb(a) readb(sh_isa_slot(a))
#define isa_readw(a) readw(sh_isa_slot(a))
#define isa_readl(a) readl(sh_isa_slot(a))
#define isa_writeb(b,a) writeb(b,sh_isa_slot(a))
#define isa_writew(w,a) writew(w,sh_isa_slot(a))
#define isa_writel(l,a) writel(l,sh_isa_slot(a))
#define isa_memset_io(a,b,c) \
  memset((void *)(sh_isa_slot((unsigned long)a)),(b),(c))
#define isa_memcpy_fromio(a,b,c) \
  memcpy((a),(void *)(sh_isa_slot((unsigned long)(b))),(c))
#define isa_memcpy_toio(a,b,c) \
  memcpy((void *)(sh_isa_slot((unsigned long)(a))),(b),(c))

extern __inline__ unsigned long ctrl_inb(unsigned long addr)
{
	return *(volatile unsigned char*)addr;
}

extern __inline__ unsigned long ctrl_inw(unsigned long addr)
{
	return *(volatile unsigned short*)addr;
}

extern __inline__ unsigned long ctrl_inl(unsigned long addr)
{
	return *(volatile unsigned long*)addr;
}

extern __inline__ void ctrl_outb(unsigned char b, unsigned long addr)
{
	*(volatile unsigned char*)addr = b;
}

extern __inline__ void ctrl_outw(unsigned short b, unsigned long addr)
{
	*(volatile unsigned short*)addr = b;
}

extern __inline__ void ctrl_outl(unsigned int b, unsigned long addr)
{
        *(volatile unsigned long*)addr = b;
}

#ifdef __KERNEL__

#define IO_SPACE_LIMIT 0xffffffff

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
	return (void *)P1SEGADDR(address);
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
	return (void *) P2SEGADDR(offset);
}

/*
 * This one maps high address device memory and turns off caching for that area.
 * it's useful if some control registers are in such an area and write combining
 * or read caching is not desirable:
 */
extern __inline__ void * ioremap_nocache (unsigned long offset, unsigned long size)
{
	return (void *) P2SEGADDR(offset);
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

/*
 * The caches on some architectures aren't dma-coherent and have need to
 * handle this in software.  There are three types of operations that
 * can be applied to dma buffers.
 *
 *  - dma_cache_wback_inv(start, size) makes caches and RAM coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_inv(start, size) invalidates the affected parts of the
 *    caches.  Dirty lines of the caches may be written back or simply
 *    be discarded.  This operation is necessary before dma operations
 *    to the memory.
 *  - dma_cache_wback(start, size) writes back any dirty lines but does
 *    not invalidate the cache.  This can be used before DMA reads from
 *    memory,
 */

#define dma_cache_wback_inv(_start,_size) \
    cache_flush_area((unsigned long)(_start),((unsigned long)(_start)+(_size)))
#define dma_cache_inv(_start,_size) \
    cache_purge_area((unsigned long)(_start),((unsigned long)(_start)+(_size)))
#define dma_cache_wback(_start,_size) \
    cache_wback_area((unsigned long)(_start),((unsigned long)(_start)+(_size)))

#endif /* __KERNEL__ */
#endif /* __ASM_SH_IO_H */
