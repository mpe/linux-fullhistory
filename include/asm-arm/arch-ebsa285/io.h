/*
 * linux/include/asm-arm/arch-ebsa285/io.h
 *
 * Copyright (C) 1997-1999 Russell King
 *
 * Modifications:
 *  06-12-1997	RMK	Created.
 *  07-04-1999	RMK	Major cleanup
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#include <asm/dec21285.h>

/*
 * This architecture does not require any delayed IO, and
 * has the constant-optimised IO
 */
#undef	ARCH_IO_DELAY
#define ARCH_READWRITE

#define __pci_io_addr(x)	(PCIO_BASE + (unsigned int)(x))

#define __inb(p)		(*(volatile unsigned char *)__pci_io_addr(p))
#define __inl(p)		(*(volatile unsigned long *)__pci_io_addr(p))

extern __inline__ unsigned int __inw(unsigned int port)
{
	unsigned int value;
	__asm__ __volatile__(
	"ldr%?h	%0, [%1, %2]	@ inw"
	: "=&r" (value)
	: "r" (PCIO_BASE), "r" (port));
	return value;
}


#define __outb(v,p)		(*(volatile unsigned char *)__pci_io_addr(p) = (v))
#define __outl(v,p)		(*(volatile unsigned long *)__pci_io_addr(p) = (v))

extern __inline__ void __outw(unsigned int value, unsigned int port)
{
	__asm__ __volatile__(
	"str%?h	%0, [%1, %2]	@ outw"
	: : "r" (value), "r" (PCIO_BASE), "r" (port));
}

#define __ioaddr(p)	__pci_io_addr(p)

/*
 * ioremap support - validate a PCI memory address,
 * and convert a PCI memory address to a physical
 * address for the page tables.
 */
#define valid_ioaddr(iomem,size) ((iomem) < 0x80000000 && (iomem) + (size) <= 0x80000000)
#define io_to_phys(iomem)	 ((iomem) + DC21285_PCI_MEM)

/*
 * Fudge up IO addresses by this much.  Once we're confident that nobody
 * is using read*() and so on with addresses they didn't get from ioremap
 * this can go away.
 */
#define IO_FUDGE_FACTOR		PCIMEM_BASE

#define __pci_mem_addr(x)	((void *)(IO_FUDGE_FACTOR + (unsigned long)(x)))

/*
 * ioremap takes a PCI memory address, as specified in
 * linux/Documentation/IO-mapping.txt
 */
#define ioremap(iomem_addr,size)					\
({									\
	unsigned long _addr = (iomem_addr), _size = (size);		\
	void *_ret = NULL;						\
	if (valid_ioaddr(_addr, _size)) {				\
		_addr = io_to_phys(_addr);				\
		_ret = __ioremap(_addr, _size, 0) - IO_FUDGE_FACTOR;	\
	}								\
	_ret; })

#define ioremap_nocache(iomem_addr,size) ioremap((iomem_addr),(size))

#define iounmap(_addr)	do { __iounmap(__pci_mem_addr((_addr))); } while (0)

#define readb(addr)	(*(volatile unsigned char *)__pci_mem_addr(addr))
#define readw(addr)	(*(volatile unsigned short *)__pci_mem_addr(addr))
#define readl(addr)	(*(volatile unsigned long *)__pci_mem_addr(addr))

#define writeb(b,addr)	(*(volatile unsigned char *)__pci_mem_addr(addr) = (b))
#define writew(b,addr)	(*(volatile unsigned short *)__pci_mem_addr(addr) = (b))
#define writel(b,addr)	(*(volatile unsigned long *)__pci_mem_addr(addr) = (b))

#define memset_io(a,b,c)	memset(__pci_mem_addr(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),__pci_mem_addr(b),(c))
#define memcpy_toio(a,b,c)	memcpy(__pci_mem_addr(a),(b),(c))

#define eth_io_copy_and_sum(a,b,c,d) eth_copy_and_sum((a),__pci_mem_addr(b),(c),(d))

#endif
