#ifndef __ALPHA_IO_H
#define __ALPHA_IO_H

#include <linux/config.h>

#include <asm/system.h>

/*
 * The hae (hardware address extension) register is used to
 * access high IO addresses. To avoid doing an external cycle
 * every time we need to set the hae, we have a hae cache in
 * memory. The kernel entry code makes sure that the hae is
 * preserved across interrupts, so it is safe to set the hae
 * once and then depend on it staying the same in kernel code.
 */
extern struct hae {
	unsigned long	cache;
	unsigned long	*reg;
} hae;

/*
 * Virtual -> physical identity mapping starts at this offset
 */
#define IDENT_ADDR	(0xfffffc0000000000UL)

#ifdef __KERNEL__

/*
 * We try to avoid hae updates (thus the cache), but when we
 * do need to update the hae, we need to do it atomically, so
 * that any interrupts wouldn't get confused with the hae
 * register not being up-to-date with respect to the hardware
 * value.
 */
extern inline void set_hae(unsigned long new_hae)
{
	unsigned long ipl = swpipl(7);
	hae.cache = new_hae;
	*hae.reg = new_hae;
	mb();
	setipl(ipl);
}

/*
 * Change virtual addresses to physical addresses and vv.
 */
extern inline unsigned long virt_to_phys(volatile void * address)
{
	return 0xffffffffUL & (unsigned long) address;
}

extern inline void * phys_to_virt(unsigned long address)
{
	return (void *) (address + IDENT_ADDR);
}

#else /* !__KERNEL__ */

/*
 * Define actual functions in private name-space so it's easier to
 * accomodate things like XFree or svgalib that like to define their
 * own versions of inb etc.
 */
extern unsigned int _inb (unsigned long port);
extern unsigned int _inw (unsigned long port);
extern unsigned int _inl (unsigned long port);
extern void _outb (unsigned char b,unsigned long port);
extern void _outw (unsigned short w,unsigned long port);
extern void _outl (unsigned int l,unsigned long port);

#ifndef inb
# define inb(p) _inb((p))
# define inw(p) _inw((p))
# define inl(p) _inl((p))
# define outb(b,p) _outb((b),(p))
# define outw(w,p) _outw((w),(p))
# define outl(l,p) _outl((l),(p))
#endif

#endif /* __KERNEL__ */

/*
 * There are different version of the alpha motherboards: the
 * "interesting" (read: slightly braindead) Jensen type hardware
 * and the PCI version
 */
#ifdef CONFIG_PCI
#include <asm/lca.h>		/* get chip-specific definitions */
#else
#include <asm/jensen.h>
#endif

/*
 * String version of IO memory access ops:
 */
extern void memcpy_fromio(void *, unsigned long, unsigned long);
extern void memcpy_toio(unsigned long, void *, unsigned long);
extern void memset_io(unsigned long, int, unsigned long);

/*
 * String versions of in/out ops:
 */
extern void insb (unsigned long port, void *src, unsigned long count);
extern void insw (unsigned long port, void *src, unsigned long count);
extern void insl (unsigned long port, void *src, unsigned long count);
extern void outsb (unsigned long port, void *dst, unsigned long count);
extern void outsw (unsigned long port, void *dst, unsigned long count);
extern void outsl (unsigned long port, void *dst, unsigned long count);

#endif
