#ifndef _PPC64_IO_H
#define _PPC64_IO_H

/* 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/page.h>
#include <asm/byteorder.h>
#ifdef CONFIG_PPC_ISERIES 
#include <asm/iSeries/iSeries_io.h>
#endif  
#include <asm/memory.h>
#include <asm/delay.h>

#define SIO_CONFIG_RA	0x398
#define SIO_CONFIG_RD	0x399

#define SLOW_DOWN_IO
/* Define this if you want to see virt_to_* messages */
#undef __IO_DEBUG

extern unsigned long isa_io_base;
extern unsigned long isa_mem_base;
extern unsigned long pci_io_base;
extern unsigned long pci_dram_offset;
extern int have_print;
#define _IO_BASE	isa_io_base
#define _ISA_MEM_BASE	isa_mem_base
#define PCI_DRAM_OFFSET	pci_dram_offset

#ifdef CONFIG_PPC_ISERIES
#define readb(addr)		iSeries_Read_Byte((void*)(addr))  
#define readw(addr)		iSeries_Read_Word((void*)(addr))  
#define readl(addr)		iSeries_Read_Long((void*)(addr))
#define writeb(data, addr)	iSeries_Write_Byte(data,((void*)(addr)))
#define writew(data, addr)	iSeries_Write_Word(data,((void*)(addr)))
#define writel(data, addr)	iSeries_Write_Long(data,((void*)(addr)))
#define memset_io(a,b,c)	iSeries_memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	iSeries_memcpy_fromio((void *)(a), (void *)(b), (c))
#define memcpy_toio(a,b,c)	iSeries_memcpy_toio((void *)(a), (void *)(b), (c))
#define inb(addr)		readb(((unsigned long)(addr)))  
#define inw(addr)		readw(((unsigned long)(addr)))  
#define inl(addr)		readl(((unsigned long)(addr)))
#define outb(data,addr)		writeb(data,((unsigned long)(addr)))  
#define outw(data,addr)		writew(data,((unsigned long)(addr)))  
#define outl(data,addr)		writel(data,((unsigned long)(addr)))
#else
#define IS_MAPPED_VADDR(port)	((unsigned long)(port) >> 60UL)
#ifdef CONFIG_PPC_EEH
#define readb(addr)		eeh_readb((void*)(addr))  
#define readw(addr)		eeh_readw((void*)(addr))  
#define readl(addr)		eeh_readl((void*)(addr))
#define writeb(data, addr)	eeh_writeb((data), ((void*)(addr)))
#define writew(data, addr)	eeh_writew((data), ((void*)(addr)))
#define writel(data, addr)	eeh_writel((data), ((void*)(addr)))
#define memset_io(a,b,c)	eeh_memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	eeh_memcpy_fromio((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	eeh_memcpy_toio((void *)(a),(b),(c))
#else
#define readb(addr)		in_8((volatile u8 *)(addr))
#define writeb(b,addr)		out_8((volatile u8 *)(addr), (b))
#define readw(addr)		in_le16((volatile u16 *)(addr))
#define readl(addr)		in_le32((volatile u32 *)(addr))
#define writew(b,addr)		out_le16((volatile u16 *)(addr),(b))
#define writel(b,addr)		out_le32((volatile u32 *)(addr),(b))
#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))
#endif
#define inb(port)		_inb((unsigned long)port)
#define outb(val, port)		_outb(val, (unsigned long)port)
#define inw(port)		_inw((unsigned long)port)
#define outw(val, port)		_outw(val, (unsigned long)port)
#define inl(port)		_inl((unsigned long)port)
#define outl(val, port)		_outl(val, (unsigned long)port)
#endif


/*
 * output pause versions need a delay at least for the
 * w83c105 ide controller in a p610.
 */
#define inb_p(port)             inb(port)
#define outb_p(val, port)       (udelay(1), outb((val), (port)))
#define inw_p(port)             inw(port)
#define outw_p(val, port)       (udelay(1), outw((val), (port)))
#define inl_p(port)             inl(port)
#define outl_p(val, port)       (udelay(1), outl((val, (port)))

/*
 * The insw/outsw/insl/outsl macros don't do byte-swapping.
 * They are only used in practice for transferring buffers which
 * are arrays of bytes, and byte-swapping is not appropriate in
 * that case.  - paulus */
#define _IOMAP_VADDR(port) (IS_MAPPED_VADDR(port) ? (port) : (port)+_IO_BASE)
#define insb(port, buf, ns)	_insb((u8 *)(_IOMAP_VADDR(port)), (buf), (ns))
#define outsb(port, buf, ns)	_outsb((u8 *)(_IOMAP_VADDR(port)), (buf), (ns))
#define insw(port, buf, ns)	_insw_ns((u16 *)(_IOMAP_VADDR(port)), (buf), (ns))
#define outsw(port, buf, ns)	_outsw_ns((u16 *)(_IOMAP_VADDR(port)), (buf), (ns))
#define insl(port, buf, nl)	_insl_ns((u32 *)(_IOMAP_VADDR(port)), (buf), (nl))
#define outsl(port, buf, nl)	_outsl_ns((u32 *)(_IOMAP_VADDR(port)), (buf), (nl))

extern void _insb(volatile u8 *port, void *buf, int ns);
extern void _outsb(volatile u8 *port, const void *buf, int ns);
extern void _insw(volatile u16 *port, void *buf, int ns);
extern void _outsw(volatile u16 *port, const void *buf, int ns);
extern void _insl(volatile u32 *port, void *buf, int nl);
extern void _outsl(volatile u32 *port, const void *buf, int nl);
extern void _insw_ns(volatile u16 *port, void *buf, int ns);
extern void _outsw_ns(volatile u16 *port, const void *buf, int ns);
extern void _insl_ns(volatile u32 *port, void *buf, int nl);
extern void _outsl_ns(volatile u32 *port, const void *buf, int nl);

/*
 * The *_ns versions below don't do byte-swapping.
 * Neither do the standard versions now, these are just here
 * for older code.
 */
#define insw_ns(port, buf, ns)	_insw_ns((u16 *)(_IOMAP_VADDR(port)), (buf), (ns))
#define outsw_ns(port, buf, ns)	_outsw_ns((u16 *)(_IOMAP_VADDR(port)), (buf), (ns))
#define insl_ns(port, buf, nl)	_insl_ns((u32 *)(_IOMAP_VADDR(port)), (buf), (nl))
#define outsl_ns(port, buf, nl)	_outsl_ns((u32 *)(_IOMAP_VADDR(port)), (buf), (nl))


#define IO_SPACE_LIMIT ~(0UL)
#define MEM_SPACE_LIMIT ~(0UL)


#ifdef __KERNEL__
/*
 * Map in an area of physical address space, for accessing
 * I/O devices etc.
 */
extern void *__ioremap(unsigned long address, unsigned long size,
		       unsigned long flags);
extern void *ioremap(unsigned long address, unsigned long size);
#define ioremap_nocache(addr, size)	ioremap((addr), (size))
extern void iounmap(void *addr);

/*
 * Change virtual addresses to physical addresses and vv, for
 * addresses in the area where the kernel has the RAM mapped.
 */
extern inline unsigned long virt_to_phys(volatile void * address)
{
#ifdef __IO_DEBUG
	printk("virt_to_phys: 0x%08lx -> 0x%08lx\n", 
			(unsigned long) address,
			__pa((unsigned long)address));
#endif
	return __pa((unsigned long)address);
}

extern inline void * phys_to_virt(unsigned long address)
{
#ifdef __IO_DEBUG
	printk("phys_to_virt: 0x%08lx -> 0x%08lx\n", address, __va(address));
#endif
	return (void *) __va(address);
}

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)	((page - mem_map) << PAGE_SHIFT)

#if 0
#define BIO_VMERGE_BOUNDARY	4096
#endif

#endif /* __KERNEL__ */

extern inline void iosync(void)
{
        __asm__ __volatile__ ("sync" : : : "memory");
}

/* Enforce in-order execution of data I/O. 
 * No distinction between read/write on PPC; use eieio for all three.
 */
#define iobarrier_rw() eieio()
#define iobarrier_r()  eieio()
#define iobarrier_w()  eieio()

/*
 * 8, 16 and 32 bit, big and little endian I/O operations, with barrier.
 */
extern inline int in_8(volatile unsigned char *addr)
{
	int ret;

	__asm__ __volatile__("eieio; lbz%U1%X1 %0,%1" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_8(volatile unsigned char *addr, int val)
{
	__asm__ __volatile__("stb%U0%X0 %1,%0" : "=m" (*addr) : "r" (val));
}

extern inline int in_le16(volatile unsigned short *addr)
{
	int ret;

	__asm__ __volatile__("eieio; lhbrx %0,0,%1" : "=r" (ret) :
			      "r" (addr), "m" (*addr));
	return ret;
}

extern inline int in_be16(volatile unsigned short *addr)
{
	int ret;

	__asm__ __volatile__("eieio; lhz%U1%X1 %0,%1" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_le16(volatile unsigned short *addr, int val)
{
	__asm__ __volatile__("sthbrx %1,0,%2" : "=m" (*addr) :
			      "r" (val), "r" (addr));
}

extern inline void out_be16(volatile unsigned short *addr, int val)
{
	__asm__ __volatile__("sth%U0%X0 %1,%0" : "=m" (*addr) : "r" (val));
}

extern inline unsigned in_le32(volatile unsigned *addr)
{
	unsigned ret;

	__asm__ __volatile__("eieio; lwbrx %0,0,%1" : "=r" (ret) :
			     "r" (addr), "m" (*addr));
	return ret;
}

extern inline unsigned in_be32(volatile unsigned *addr)
{
	unsigned ret;

	__asm__ __volatile__("eieio; lwz%U1%X1 %0,%1" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_le32(volatile unsigned *addr, int val)
{
	__asm__ __volatile__("stwbrx %1,0,%2" : "=m" (*addr) :
			     "r" (val), "r" (addr));
}

extern inline void out_be32(volatile unsigned *addr, int val)
{
	__asm__ __volatile__("stw%U0%X0 %1,%0" : "=m" (*addr) : "r" (val));
}

#ifdef CONFIG_PPC_EEH
#include <asm/eeh.h>
#endif

#ifndef CONFIG_PPC_ISERIES 
static inline u8 _inb(unsigned long port) {
	if (IS_MAPPED_VADDR(port))
		return readb((void *)port);
	else if (_IO_BASE)
		return in_8((u8 *)((port)+_IO_BASE));
	else
		return 0xff;
}
static inline void _outb(u8 val, unsigned long port) {
	if (IS_MAPPED_VADDR(port))
		return writeb(val, (void *)port);
	else if (_IO_BASE)
		out_8((u8 *)((port)+_IO_BASE), val);
}
static inline u16 _inw(unsigned long port) {
	if (IS_MAPPED_VADDR(port))
		return readw((void *)port);
	else if (_IO_BASE)
		return in_le16((u16 *)((port)+_IO_BASE));
	else
		return 0xffff;
}
static inline void _outw(u16 val, unsigned long port) {
	if (IS_MAPPED_VADDR(port))
		return writew(val, (void *)port);
	else if (_IO_BASE)
		out_le16((u16 *)((port)+_IO_BASE), val);
}
static inline u32 _inl(unsigned long port) {
	if (IS_MAPPED_VADDR(port))
		return readl((void *)port);
	else if (_IO_BASE)
		return in_le32((u32 *)((port)+_IO_BASE));
	else
		return 0xffffffff;
}
static inline void _outl(u32 val, unsigned long port) {
	if (IS_MAPPED_VADDR(port))
		return writel(val, (void *)port);
	else if (_IO_BASE)
		out_le32((u32 *)((port)+_IO_BASE), val);
}
#endif

#ifdef __KERNEL__
static inline int check_signature(unsigned long io_addr,
	const unsigned char *signature, int length)
{
	int retval = 0;
#ifndef CONFIG_PPC_ISERIES 
	do {
		if (readb(io_addr) != *signature)
			goto out;
		io_addr++;
		signature++;
		length--;
	} while (length);
	retval = 1;
out:
#endif
	return retval;
}

/* Nothing to do */

#define dma_cache_inv(_start,_size)		do { } while (0)
#define dma_cache_wback(_start,_size)		do { } while (0)
#define dma_cache_wback_inv(_start,_size)	do { } while (0)

#endif /* __KERNEL__ */

#endif /* _PPC64_IO_H */
