/*
 * linux/include/asm-arm/io.h
 *
 * Copyright (C) 1996 Russell King
 *
 * Modifications:
 *  16-Sep-1996	RMK	Inlined the inx/outx functions & optimised for both
 *			constant addresses and variable addresses.
 *  04-Dec-1997	RMK	Moved a lot of this stuff to the new architecture
 *			specific IO header files.
 */
#ifndef __ASM_ARM_IO_H
#define __ASM_ARM_IO_H

#include <asm/hardware.h>
#include <asm/arch/mmu.h>
#include <asm/arch/io.h>
#include <asm/proc/io.h>

/* unsigned long virt_to_phys(void *x) */
#define virt_to_phys(x)		(__virt_to_phys((unsigned long)(x)))

/* void *phys_to_virt(unsigned long x) */
#define phys_to_virt(x)		((void *)(__phys_to_virt((unsigned long)(x))))

/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *              address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *              to an address that the kernel can use.
 */
#define virt_to_bus(x)	(__virt_to_bus((unsigned long)(x)))
#define bus_to_virt(x)	((void *)(__bus_to_virt((unsigned long)(x))))

/*
 * These macros actually build the multi-value IO function prototypes
 */
#define __OUTS(s,i,x)	extern void outs##s(unsigned int port, const void *from, int len);
#define __INS(s,i,x)	extern void ins##s(unsigned int port, void *to, int len);

#define __IO(s,i,x) \
  __OUTS(s,i,x) \
  __INS(s,i,x)

__IO(b,"b",char)
__IO(w,"h",short)
__IO(l,"",long)

/*
 * Note that due to the way __builtin_constant_t() works, you
 *  - can't use it inside an inline function (it will never be true)
 *  - you don't have to worry about side effects withing the __builtin..
 */
#ifdef __outbc
#define outb(val,port)	\
  (__builtin_constant_p((port)) ? __outbc((val),(port)) : __outb((val),(port)))
#else
#define outb(val,port) __outb((val),(port))
#endif

#ifdef __outwc
#define outw(val,port)	\
  (__builtin_constant_p((port)) ? __outwc((val),(port)) : __outw((val),(port)))
#else
#define outw(val,port) __outw((val),(port))
#endif

#ifdef __outlc
#define outl(val,port)	\
  (__builtin_constant_p((port)) ? __outlc((val),(port)) : __outl((val),(port)))
#else
#define outl(val,port) __outl((val),(port))
#endif

#ifdef __inbc
#define inb(port)	\
  (__builtin_constant_p((port)) ? __inbc((port)) : __inb((port)))
#else
#define inb(port) __inb((port))
#endif

#ifdef __inwc
#define inw(port)	\
  (__builtin_constant_p((port)) ? __inwc((port)) : __inw((port)))
#else
#define inw(port) __inw((port))
#endif

#ifdef __inlc
#define inl(port)	\
  (__builtin_constant_p((port)) ? __inlc((port)) : __inl((port)))
#else
#define inl(port) __inl((port))
#endif

/*
 * This macro will give you the translated IO address for this particular
 * architecture, which can be used with the out_t... functions.
 */
#define ioaddr(port)	\
  (__builtin_constant_p((port)) ? __ioaddrc((port)) : __ioaddr((port)))

#ifndef ARCH_IO_DELAY
/*
 * This architecture does not require any delayed IO.
 * It is handled in the hardware.
 */
#define outb_p(val,port)	outb((val),(port))
#define outw_p(val,port)	outw((val),(port))
#define outl_p(val,port)	outl((val),(port))
#define inb_p(port)		inb((port))
#define inw_p(port)		inw((port))
#define inl_p(port)		inl((port))
#define outsb_p(port,from,len)	outsb(port,from,len)
#define outsw_p(port,from,len)	outsw(port,from,len)
#define outsl_p(port,from,len)	outsl(port,from,len)
#define insb_p(port,to,len)	insb(port,to,len)
#define insw_p(port,to,len)	insw(port,to,len)
#define insl_p(port,to,len)	insl(port,to,len)

#else

/*
 * We have to delay the IO...
 */
#ifdef __outbc_p
#define outb_p(val,port)	\
  (__builtin_constant_p((port)) ? __outbc_p((val),(port)) : __outb_p((val),(port)))
#else
#define outb_p(val,port) __outb_p((val),(port))
#endif

#ifdef __outwc_p
#define outw_p(val,port)	\
  (__builtin_constant_p((port)) ? __outwc_p((val),(port)) : __outw_p((val),(port)))
#else
#define outw_p(val,port) __outw_p((val),(port))
#endif

#ifdef __outlc_p
#define outl_p(val,port)	\
  (__builtin_constant_p((port)) ? __outlc_p((val),(port)) : __outl_p((val),(port)))
#else
#define outl_p(val,port) __outl_p((val),(port))
#endif

#ifdef __inbc_p
#define inb_p(port)	\
  (__builtin_constant_p((port)) ? __inbc_p((port)) : __inb_p((port)))
#else
#define inb_p(port) __inb_p((port))
#endif

#ifdef __inwc_p
#define inw_p(port)	\
  (__builtin_constant_p((port)) ? __inwc_p((port)) : __inw_p((port)))
#else
#define inw_p(port) __inw_p((port))
#endif

#ifdef __inlc_p
#define inl_p(port)	\
  (__builtin_constant_p((port)) ? __inlc_p((port)) : __inl_p((port)))
#else
#define inl_p(port) __inl_p((port))
#endif

#endif

#undef ARCH_IO_DELAY
#undef ARCH_IO_CONSTANT

#ifdef __KERNEL__

extern void * __ioremap(unsigned long offset, unsigned long size, unsigned long flags);

/*
 * String version of IO memory access ops:
 */
extern void _memcpy_fromio(void *, unsigned long, unsigned long);
extern void _memcpy_toio(unsigned long, void *, unsigned long);
extern void _memset_io(unsigned long, int, unsigned long);

#define memcpy_fromio(to,from,len)	_memcpy_fromio((to),(unsigned long)(from),(len))
#define memcpy_toio(to,from,len)	_memcpy_toio((unsigned long)(to),(from),(len))
#define memset_io(addr,c,len)		_memset_io((unsigned long)(addr),(c),(len))

#endif

#endif

