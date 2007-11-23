#ifndef _PPC_IO_H
#define _PPC_IO_H

#include <linux/config.h>
#include <asm/page.h>
#include <asm/byteorder.h>

#define SIO_CONFIG_RA	0x398
#define SIO_CONFIG_RD	0x399

#define SLOW_DOWN_IO

#define PMAC_ISA_MEM_BASE 	0
#define PMAC_PCI_DRAM_OFFSET 	0
#define CHRP_ISA_IO_BASE 	0xf8000000
#define CHRP_ISA_MEM_BASE 	0xf7000000
#define CHRP_PCI_DRAM_OFFSET 	0
#define PREP_ISA_IO_BASE 	0x80000000
#define PREP_ISA_MEM_BASE 	0xc0000000
#define PREP_PCI_DRAM_OFFSET 	0x80000000

#if defined(CONFIG_4xx)
#include <asm/board.h>
#elif defined(CONFIG_8xx)
#include <asm/mpc8xx.h>
#elif defined(CONFIG_8260)
#include <asm/mpc8260.h>
#else
#ifdef CONFIG_APUS
#define _IO_BASE 0
#define _ISA_MEM_BASE 0
#define PCI_DRAM_OFFSET 0
#else
extern unsigned long isa_io_base;
extern unsigned long isa_mem_base;
extern unsigned long pci_dram_offset;
#define _IO_BASE	isa_io_base
#define _ISA_MEM_BASE	isa_mem_base
#define PCI_DRAM_OFFSET	pci_dram_offset
#endif /* CONFIG_APUS */
#endif

#define readb(addr) in_8((volatile u8 *)(addr))
#define writeb(b,addr) out_8((volatile u8 *)(addr), (b))
#if defined(CONFIG_APUS)
#define readw(addr) (*(volatile u16 *) (addr))
#define readl(addr) (*(volatile u32 *) (addr))
#define writew(b,addr) ((*(volatile u16 *) (addr)) = (b))
#define writel(b,addr) ((*(volatile u32 *) (addr)) = (b))
#else
#define readw(addr) in_le16((volatile u16 *)(addr))
#define readl(addr) in_le32((volatile u32 *)(addr))
#define writew(b,addr) out_le16((volatile u16 *)(addr),(b))
#define writel(b,addr) out_le32((volatile u32 *)(addr),(b))
#endif

/*
 * The insw/outsw/insl/outsl macros don't do byte-swapping.
 * They are only used in practice for transferring buffers which
 * are arrays of bytes, and byte-swapping is not appropriate in
 * that case.  - paulus
 */
#define insb(port, buf, ns)	_insb((u8 *)((port)+_IO_BASE), (buf), (ns))
#define outsb(port, buf, ns)	_outsb((u8 *)((port)+_IO_BASE), (buf), (ns))
#define insw(port, buf, ns)	_insw_ns((u16 *)((port)+_IO_BASE), (buf), (ns))
#define outsw(port, buf, ns)	_outsw_ns((u16 *)((port)+_IO_BASE), (buf), (ns))
#define insl(port, buf, nl)	_insl_ns((u32 *)((port)+_IO_BASE), (buf), (nl))
#define outsl(port, buf, nl)	_outsl_ns((u32 *)((port)+_IO_BASE), (buf), (nl))

#define inb(port)		in_8((u8 *)((port)+_IO_BASE))
#define outb(val, port)		out_8((u8 *)((port)+_IO_BASE), (val))
#if defined(CONFIG_APUS)
#define inw(port)		in_be16((u16 *)((port)+_IO_BASE))
#define outw(val, port)		out_be16((u16 *)((port)+_IO_BASE), (val))
#define inl(port)		in_be32((u32 *)((port)+_IO_BASE))
#define outl(val, port)		out_be32((u32 *)((port)+_IO_BASE), (val))
#else
#define inw(port)		in_le16((u16 *)((port)+_IO_BASE))
#define outw(val, port)		out_le16((u16 *)((port)+_IO_BASE), (val))
#define inl(port)		in_le32((u32 *)((port)+_IO_BASE))
#define outl(val, port)		out_le32((u32 *)((port)+_IO_BASE), (val))
#endif

#define inb_p(port)		in_8((u8 *)((port)+_IO_BASE))
#define outb_p(val, port)	out_8((u8 *)((port)+_IO_BASE), (val))
#define inw_p(port)		in_le16((u16 *)((port)+_IO_BASE))
#define outw_p(val, port)	out_le16((u16 *)((port)+_IO_BASE), (val))
#define inl_p(port)		in_le32((u32 *)((port)+_IO_BASE))
#define outl_p(val, port)	out_le32((u32 *)((port)+_IO_BASE), (val))

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
#define insw_ns(port, buf, ns)	_insw_ns((u16 *)((port)+_IO_BASE), (buf), (ns))
#define outsw_ns(port, buf, ns)	_outsw_ns((u16 *)((port)+_IO_BASE), (buf), (ns))
#define insl_ns(port, buf, nl)	_insl_ns((u32 *)((port)+_IO_BASE), (buf), (nl))
#define outsl_ns(port, buf, nl)	_outsl_ns((u32 *)((port)+_IO_BASE), (buf), (nl))


#define IO_SPACE_LIMIT ~0

#define memset_io(a,b,c)       memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)   memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

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
extern unsigned long iopa(unsigned long addr);
#ifdef CONFIG_APUS
extern unsigned long mm_ptov(unsigned long addr) __attribute__ ((const));
#endif

/*
 * The PCI bus is inherently Little-Endian.  The PowerPC is being
 * run Big-Endian.  Thus all values which cross the [PCI] barrier
 * must be endian-adjusted.  Also, the local DRAM has a different
 * address from the PCI point of view, thus buffer addresses also
 * have to be modified [mapped] appropriately.
 */
extern inline unsigned long virt_to_bus(volatile void * address)
{
#ifndef CONFIG_APUS
        if (address == (void *)0)
		return 0;
        return (unsigned long)address - KERNELBASE + PCI_DRAM_OFFSET;
#else
	return iopa ((unsigned long) address);
#endif
}

extern inline void * bus_to_virt(unsigned long address)
{
#ifndef CONFIG_APUS
        if (address == 0)
		return 0;
        return (void *)(address - PCI_DRAM_OFFSET + KERNELBASE);
#else
	return (void*) mm_ptov (address);
#endif
}

/*
 * Change virtual addresses to physical addresses and vv, for
 * addresses in the area where the kernel has the RAM mapped.
 */
extern inline unsigned long virt_to_phys(volatile void * address)
{
#ifndef CONFIG_APUS
	return (unsigned long) address - KERNELBASE;
#else
	return iopa ((unsigned long) address);
#endif
}

extern inline void * phys_to_virt(unsigned long address)
{
#ifndef CONFIG_APUS
	return (void *) (address + KERNELBASE);
#else
	return (void*) mm_ptov (address);
#endif
}

#endif /* __KERNEL__ */

/*
 * Enforce In-order Execution of I/O:
 * Acts as a barrier to ensure all previous I/O accesses have
 * completed before any further ones are issued.
 */
extern inline void eieio(void)
{
	__asm__ __volatile__ ("eieio" : : : "memory");
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

	__asm__ __volatile__("lbz%U1%X1 %0,%1; eieio" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_8(volatile unsigned char *addr, int val)
{
	__asm__ __volatile__("stb%U0%X0 %1,%0; eieio" : "=m" (*addr) : "r" (val));
}

extern inline int in_le16(volatile unsigned short *addr)
{
	int ret;

	__asm__ __volatile__("lhbrx %0,0,%1; eieio" : "=r" (ret) :
			      "r" (addr), "m" (*addr));
	return ret;
}

extern inline int in_be16(volatile unsigned short *addr)
{
	int ret;

	__asm__ __volatile__("lhz%U1%X1 %0,%1; eieio" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_le16(volatile unsigned short *addr, int val)
{
	__asm__ __volatile__("sthbrx %1,0,%2; eieio" : "=m" (*addr) :
			      "r" (val), "r" (addr));
}

extern inline void out_be16(volatile unsigned short *addr, int val)
{
	__asm__ __volatile__("sth%U0%X0 %1,%0; eieio" : "=m" (*addr) : "r" (val));
}

extern inline unsigned in_le32(volatile unsigned *addr)
{
	unsigned ret;

	__asm__ __volatile__("lwbrx %0,0,%1; eieio" : "=r" (ret) :
			     "r" (addr), "m" (*addr));
	return ret;
}

extern inline unsigned in_be32(volatile unsigned *addr)
{
	unsigned ret;

	__asm__ __volatile__("lwz%U1%X1 %0,%1; eieio" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_le32(volatile unsigned *addr, int val)
{
	__asm__ __volatile__("stwbrx %1,0,%2; eieio" : "=m" (*addr) :
			     "r" (val), "r" (addr));
}

extern inline void out_be32(volatile unsigned *addr, int val)
{
	__asm__ __volatile__("stw%U0%X0 %1,%0; eieio" : "=m" (*addr) : "r" (val));
}

#ifdef __KERNEL__
static inline int check_signature(unsigned long io_addr,
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

#endif
