#ifndef _M68K_IO_H
#define _M68K_IO_H

#ifdef __KERNEL__

#include <asm/byteorder.h>

#ifdef CONFIG_ATARI
#include <asm/atarihw.h>

#define SLOW_DOWN_IO	do { if (MACH_IS_ATARI) MFPDELAY(); } while (0)
#endif

/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the m68k architecture, we just read/write the
 * memory location directly.
 */
/* ++roman: The assignments to temp. vars avoid that gcc sometimes generates
 * two accesses to memory, which may be undesireable for some devices.
 */
#define readb(addr) \
    ({ unsigned char __v = (*(volatile unsigned char *) (addr)); __v; })
#define readw(addr) \
    ({ unsigned short __v = (*(volatile unsigned short *) (addr)); __v; })
#define readl(addr) \
    ({ unsigned int __v = (*(volatile unsigned int *) (addr)); __v; })

#define writeb(b,addr) ((*(volatile unsigned char *) (addr)) = (b))
#define writew(b,addr) ((*(volatile unsigned short *) (addr)) = (b))
#define writel(b,addr) ((*(volatile unsigned int *) (addr)) = (b))

#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

#define inb_p(addr) readb(addr)
#define inb(addr) readb(addr)

#define outb(x,addr) ((void) writeb(x,addr))
#define outb_p(x,addr) outb(x,addr)

/*
 * Change virtual addresses to physical addresses and vv.
 */
extern unsigned long mm_vtop(unsigned long addr) __attribute__ ((const));
extern unsigned long mm_ptov(unsigned long addr) __attribute__ ((const));

extern inline unsigned long virt_to_phys(volatile void * address)
{
	return mm_vtop((unsigned long)address);
}

extern inline void * phys_to_virt(unsigned long address)
{
	return (void *) mm_ptov(address);
}

/*
 * IO bus memory addresses are 1:1 with the physical address,
 * except on the PCI bus of the Hades.
 */
#ifdef CONFIG_HADES
#define virt_to_bus(a) (virt_to_phys(a) + (MACH_IS_HADES ? 0x80000000 : 0))
#define bus_to_virt(a) (phys_to_virt((a) - (MACH_IS_HADES ? 0x80000000 : 0)))
#else
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt
#endif

#endif /* __KERNEL__ */

#endif /* _M68K_IO_H */
