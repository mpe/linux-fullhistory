/*
 * linux/include/asm-arm/arch-ebsa285/io.h
 *
 * Copyright (C) 1997,1998 Russell King
 *
 * Modifications:
 *  06-Dec-1997	RMK	Created.
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

/*
 * Dynamic IO functions - let the compiler
 * optimize the expressions
 */
#define DECLARE_DYN_OUT(fnsuffix,instr,typ)				\
extern __inline__ void							\
__out##fnsuffix (unsigned int value, unsigned int port)			\
{									\
	__asm__ __volatile__(						\
	"str%?" ##instr## "	%0, [%1, %2]	@ out"###fnsuffix	\
	: 								\
	: "r" (value), "r" (PCIO_BASE), typ (port));			\
}

#define DECLARE_DYN_IN(sz,fnsuffix,instr,typ)				\
extern __inline__ unsigned sz						\
__in##fnsuffix (unsigned int port)					\
{									\
	unsigned long value;						\
	__asm__ __volatile__(						\
	"ldr%?" ##instr## "	%0, [%1, %2]	@ in"###fnsuffix	\
	: "=&r" (value)							\
	: "r" (PCIO_BASE), typ (port));					\
	return (unsigned sz)value;					\
}

extern __inline__ unsigned int __ioaddr (unsigned int port)		\
{									\
	return (unsigned int)(PCIO_BASE + port);			\
}

#define DECLARE_IO(sz,fnsuffix,instr,typ)	\
	DECLARE_DYN_OUT(fnsuffix,instr,typ)	\
	DECLARE_DYN_IN(sz,fnsuffix,instr,typ)

DECLARE_IO(char,b,"b","Jr")
DECLARE_IO(short,w,"h","r")
DECLARE_IO(long,l,"","Jr")

#undef DECLARE_IO
#undef DECLARE_DYN_OUT
#undef DECLARE_DYN_IN

/*
 * Constant address IO functions
 *
 * These have to be macros for the 'J' constraint to work -
 * +/-4096 immediate operand.
 */
#define __outbc(value,port)						\
({									\
	__asm__ __volatile__(						\
	"str%?b	%0, [%1, %2]				@ outbc"	\
	:								\
	: "r" (value), "r" (PCIO_BASE), "Jr" (port));			\
})

#define __inbc(port)							\
({									\
	unsigned char result;						\
	__asm__ __volatile__(						\
	"ldr%?b	%0, [%1, %2]				@ inbc"		\
	: "=r" (result)							\
	: "r" (PCIO_BASE), "Jr" (port));				\
	result;								\
})

#define __outwc(value,port)						\
({									\
	__asm__ __volatile__(						\
	"str%?h	%0, [%1, %2]				@ outwc"	\
	:								\
	: "r" (value), "r" (PCIO_BASE), "r" (port));			\
})

#define __inwc(port)							\
({									\
	unsigned short result;						\
	__asm__ __volatile__(						\
	"ldr%?h	%0, [%1, %2]				@ inwc"		\
	: "=r" (result)							\
	: "r" (PCIO_BASE), "r" (port));					\
	result & 0xffff;						\
})

#define __outlc(value,port)						\
({									\
	__asm__ __volatile__(						\
	"str%?	%0, [%1, %2]				@ outlc"	\
	:								\
	: "r" (value), "r" (PCIO_BASE), "Jr" (port));			\
})

#define __inlc(port)							\
({									\
	unsigned long result;						\
	__asm__ __volatile__(						\
	"ldr%?	%0, [%1, %2]				@ inlc"		\
	: "=r" (result)							\
	: "r" (PCIO_BASE), "Jr" (port));				\
	result;								\
})

#define __ioaddrc(port)							\
({									\
	unsigned long addr;						\
	addr = PCIO_BASE + port;					\
	addr;								\
})

/*
 * Translated address IO functions
 *
 * IO address has already been translated to a virtual address
 */
#define outb_t(v,p)							\
	(*(volatile unsigned char *)(p) = (v))

#define inb_t(p)							\
	(*(volatile unsigned char *)(p))

#define outl_t(v,p)							\
	(*(volatile unsigned long *)(p) = (v))

#define inl_t(p)							\
	(*(volatile unsigned long *)(p))

/*
 * ioremap support - validate a PCI memory address,
 * and convert a PCI memory address to a physical
 * address for the page tables.
 */
#define valid_ioaddr(iomem,size) ((iomem) < 0x80000000 && (iomem) + (size) <= 0x80000000)
#define io_to_phys(iomem)	((iomem) + DC21285_PCI_MEM)

/*
 * Fudge up IO addresses by this much.  Once we're confident that nobody
 * is using read*() and so on with addresses they didn't get from ioremap
 * this can go away.
 */
#define IO_FUDGE_FACTOR		PCIMEM_BASE

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

extern void iounmap(void *addr);

#define DECLARE_PCI_WRITE(typ,fnsuffix)					\
static inline void write##fnsuffix(unsigned typ val, unsigned int addr)	\
{									\
	*(volatile unsigned typ *)(IO_FUDGE_FACTOR + addr) = val;	\
}

#define DECLARE_PCI_READ(typ,fnsuffix)					\
static inline unsigned typ read##fnsuffix (unsigned int addr)		\
{									\
	return *(volatile unsigned typ *)(IO_FUDGE_FACTOR + addr);	\
}

#define DECLARE_PCI(typ,fnsuffix)					\
	DECLARE_PCI_WRITE(typ,fnsuffix)					\
	DECLARE_PCI_READ(typ,fnsuffix)

DECLARE_PCI(char,b)
DECLARE_PCI(short,w)
DECLARE_PCI(long,l)

#undef DECLARE_PCI
#undef DECLARE_PCI_READ
#undef DECLARE_PCI_WRITE

#endif
