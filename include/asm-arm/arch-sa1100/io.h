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

/*
 * This architecture does not require any delayed IO
 */
#undef	ARCH_IO_DELAY

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

#endif
