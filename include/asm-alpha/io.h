#ifndef __ALPHA_IO_H
#define __ALPHA_IO_H

#include <linux/config.h>

#include <asm/system.h>

/* We don't use IO slowdowns on the alpha, but.. */
#define __SLOW_DOWN_IO	do { } while (0)
#define SLOW_DOWN_IO	do { } while (0)

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
 * accommodate things like XFree or svgalib that like to define their
 * own versions of inb etc.
 */
extern void __sethae (unsigned long addr);	/* syscall */
extern void _sethae (unsigned long addr);	/* cached version */

#endif /* !__KERNEL__ */

/*
 * There are different chipsets to interface the Alpha CPUs to the world.
 */
#if defined(CONFIG_ALPHA_LCA)
# include <asm/lca.h>		/* get chip-specific definitions */
#elif defined(CONFIG_ALPHA_APECS)
# include <asm/apecs.h>		/* get chip-specific definitions */
#elif defined(CONFIG_ALPHA_CIA)
# include <asm/cia.h>		/* get chip-specific definitions */
#else
# include <asm/jensen.h>
#endif

/*
 * The convention used for inb/outb etc. is that names starting with
 * two underscores are the inline versions, names starting with a
 * single underscore are proper functions, and names starting with a
 * letter are macros that map in some way to inline or proper function
 * versions.  Not all that pretty, but before you change it, be sure
 * to convince yourself that it won't break anything (in particular
 * module support).
 */
extern unsigned int	_inb (unsigned long port);
extern unsigned int	_inw (unsigned long port);
extern unsigned int	_inl (unsigned long port);
extern void		_outb (unsigned char b,unsigned long port);
extern void		_outw (unsigned short w,unsigned long port);
extern void		_outl (unsigned int l,unsigned long port);
extern unsigned long	_readb(unsigned long addr);
extern unsigned long	_readw(unsigned long addr);
extern unsigned long	_readl(unsigned long addr);
extern void		_writeb(unsigned char b, unsigned long addr);
extern void		_writew(unsigned short b, unsigned long addr);
extern void		_writel(unsigned int b, unsigned long addr);

/*
 * The platform header files may define some of these macros to use
 * the inlined versions where appropriate.  These macros may also be
 * redefined by userlevel programs.
 */
#ifndef inb
# define inb(p)		_inb((p))
#endif
#ifndef inw
# define inw(p)		_inw((p))
#endif
#ifndef inl
# define inl(p)		_inl((p))
#endif
#ifndef outb
# define outb(b,p)	_outb((b),(p))
#endif
#ifndef outw
# define outw(w,p)	_outw((w),(p))
#endif
#ifndef outl
# define outl(l,p)	_outl((l),(p))
#endif

#ifndef inb_p
# define inb_p		inb
#endif
#ifndef inw_p
# define inw_p		inw
#endif
#ifndef inl_p
# define inl_p		inl
#endif

#ifndef outb_p
# define outb_p		outb
#endif
#ifndef outw_p
# define outw_p		outw
#endif
#ifndef outl_p
# define outl_p		outl
#endif

/*
 * The "address" in IO memory space is not clearly either a integer or a
 * pointer. We will accept both, thus the casts.
 */
#ifndef readb
# define readb(a)	_readb((unsigned long)(a))
#endif
#ifndef readw
# define readw(a)	_readw((unsigned long)(a))
#endif
#ifndef readl
# define readl(a)	_readl((unsigned long)(a))
#endif
#ifndef writeb
# define writeb(v,a)	_writeb((v),(unsigned long)(a))
#endif
#ifndef writew
# define writew(v,a)	_writew((v),(unsigned long)(a))
#endif
#ifndef writel
# define writel(v,a)	_writel((v),(unsigned long)(a))
#endif

#ifdef __KERNEL__

/*
 * String version of IO memory access ops:
 */
extern void _memcpy_fromio(void *, unsigned long, unsigned long);
extern void _memcpy_toio(unsigned long, void *, unsigned long);
extern void _memset_io(unsigned long, int, unsigned long);

#define memcpy_fromio(to,from,len)	_memcpy_fromio((to),(unsigned long)(from),(len))
#define memcpy_toio(to,from,len)	_memcpy_toio((unsigned long)(to),(from),(len))
#define memset_io(addr,c,len)		_memset_io((unsigned long)(addr),(c),(len))

/*
 * String versions of in/out ops:
 */
extern void insb (unsigned long port, void *dst, unsigned long count);
extern void insw (unsigned long port, void *dst, unsigned long count);
extern void insl (unsigned long port, void *dst, unsigned long count);
extern void outsb (unsigned long port, const void *src, unsigned long count);
extern void outsw (unsigned long port, const void *src, unsigned long count);
extern void outsl (unsigned long port, const void *src, unsigned long count);

/*
 * XXX - We don't have csum_partial_copy_fromio() yet, so we cheat here and 
 * just copy it. The net code will then do the checksum later. Presently 
 * only used by some shared memory 8390 ethernet cards anyway.
 */

#define eth_io_copy_and_sum(skb,src,len,unused)	memcpy_fromio((skb)->data,(src),(len))

#endif /* __KERNEL__ */

#endif
