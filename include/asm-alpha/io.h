#ifndef __ALPHA_IO_H
#define __ALPHA_IO_H

#include <linux/config.h>
#include <asm/system.h>

/* We don't use IO slowdowns on the Alpha, but.. */
#define __SLOW_DOWN_IO	do { } while (0)
#define SLOW_DOWN_IO	do { } while (0)

/*
 * Virtual -> physical identity mapping starts at this offset
 */
#ifdef USE_48_BIT_KSEG
#define IDENT_ADDR     0xffff800000000000
#else
#define IDENT_ADDR     0xfffffc0000000000
#endif

#ifdef __KERNEL__
#include <asm/machvec.h>

/*
 * We try to avoid hae updates (thus the cache), but when we
 * do need to update the hae, we need to do it atomically, so
 * that any interrupts wouldn't get confused with the hae
 * register not being up-to-date with respect to the hardware
 * value.
 */
static inline void __set_hae(unsigned long new_hae)
{
	unsigned long ipl = swpipl(7);

	alpha_mv.hae_cache = new_hae;
	*alpha_mv.hae_register = new_hae;
	mb();

	/* Re-read to make sure it was written.  */
	new_hae = *alpha_mv.hae_register;
	setipl(ipl);
}

static inline void set_hae(unsigned long new_hae)
{
	if (new_hae != alpha_mv.hae_cache)
		__set_hae(new_hae);
}

/*
 * Change virtual addresses to physical addresses and vv.
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	/* Conditionalize this on the CPU?  This here is 40 bits,
	   whereas EV4 only supports 34.  But KSEG is farther out
	   so it shouldn't _really_ matter.  */
	return 0xffffffffffUL & (unsigned long) address;
}

static inline void * phys_to_virt(unsigned long address)
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

#ifdef __KERNEL__
#ifdef CONFIG_ALPHA_GENERIC

/* In a generic kernel, we always go through the machine vector.  */

# define virt_to_bus(a)	alpha_mv.mv_virt_to_bus(a)
# define bus_to_virt(a)	alpha_mv.mv_bus_to_virt(a)

# define __inb		alpha_mv.mv_inb
# define __inw		alpha_mv.mv_inw
# define __inl		alpha_mv.mv_inl
# define __outb		alpha_mv.mv_outb
# define __outw		alpha_mv.mv_outw
# define __outl		alpha_mv.mv_outl

# define __readb(a)	alpha_mv.mv_readb((unsigned long)(a))
# define __readw(a)	alpha_mv.mv_readw((unsigned long)(a))
# define __readl(a)	alpha_mv.mv_readl((unsigned long)(a))
# define __readq(a)	alpha_mv.mv_readq((unsigned long)(a))
# define __writeb(v,a)	alpha_mv.mv_writeb((v),(unsigned long)(a))
# define __writew(v,a)	alpha_mv.mv_writew((v),(unsigned long)(a))
# define __writel(v,a)	alpha_mv.mv_writel((v),(unsigned long)(a))
# define __writeq(v,a)	alpha_mv.mv_writeq((v),(unsigned long)(a))

# define inb		__inb
# define inw		__inw
# define inl		__inl
# define outb		__outb
# define outw		__outw
# define outl		__outl

# define readb		__readb
# define readw		__readw
# define readl		__readl
# define readq		__readq
# define writeb		__writeb
# define writew		__writew
# define writel		__writel
# define writeq		__writeq

# define dense_mem(a)	alpha_mv.mv_dense_mem(a)

#else

/* Control how and what gets defined within the core logic headers.  */
#define __WANT_IO_DEF

#if defined(CONFIG_ALPHA_APECS)
# include <asm/core_apecs.h>
#elif defined(CONFIG_ALPHA_CIA)
# include <asm/core_cia.h>
#elif defined(CONFIG_ALPHA_LCA)
# include <asm/core_lca.h>
#elif defined(CONFIG_ALPHA_MCPCIA)
# include <asm/core_mcpcia.h>
#elif defined(CONFIG_ALPHA_PYXIS)
# include <asm/core_pyxis.h>
#elif defined(CONFIG_ALPHA_T2)
# include <asm/core_t2.h>
#elif defined(CONFIG_ALPHA_TSUNAMI)
# include <asm/core_tsunami.h>
#elif defined(CONFIG_ALPHA_JENSEN)
# include <asm/jensen.h>
#elif defined(CONFIG_ALPHA_RX164)
# include <asm/core_polaris.h>
#else
#error "What system is this?"
#endif

#undef __WANT_IO_DEF

#endif /* GENERIC */
#endif /* __KERNEL__ */

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
extern unsigned long	_readq(unsigned long addr);
extern void		_writeb(unsigned char b, unsigned long addr);
extern void		_writew(unsigned short b, unsigned long addr);
extern void		_writel(unsigned int b, unsigned long addr);
extern void		_writeq(unsigned long b, unsigned long addr);

#ifdef __KERNEL__
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

#else 

/* Userspace declarations.  */

extern unsigned int	inb (unsigned long port);
extern unsigned int	inw (unsigned long port);
extern unsigned int	inl (unsigned long port);
extern void		outb (unsigned char b,unsigned long port);
extern void		outw (unsigned short w,unsigned long port);
extern void		outl (unsigned int l,unsigned long port);
extern unsigned long	readb(unsigned long addr);
extern unsigned long	readw(unsigned long addr);
extern unsigned long	readl(unsigned long addr);
extern void		writeb(unsigned char b, unsigned long addr);
extern void		writew(unsigned short b, unsigned long addr);
extern void		writel(unsigned int b, unsigned long addr);

#endif /* __KERNEL__ */

#ifdef __KERNEL__

/*
 * The "address" in IO memory space is not clearly either an integer or a
 * pointer. We will accept both, thus the casts.
 *
 * On the alpha, we have the whole physical address space mapped at all
 * times, so "ioremap()" and "iounmap()" do not need to do anything.
 */
static inline void * ioremap(unsigned long offset, unsigned long size)
{
	return (void *) offset;
} 

static inline void iounmap(void *addr)
{
}

#ifndef readb
# define readb(a)	_readb((unsigned long)(a))
#endif
#ifndef readw
# define readw(a)	_readw((unsigned long)(a))
#endif
#ifndef readl
# define readl(a)	_readl((unsigned long)(a))
#endif
#ifndef readq
# define readq(a)	_readq((unsigned long)(a))
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
#ifndef writeq
# define writeq(v,a)	_writeq((v),(unsigned long)(a))
#endif

/*
 * String version of IO memory access ops:
 */
extern void _memcpy_fromio(void *, unsigned long, long);
extern void _memcpy_toio(unsigned long, void *, long);
extern void _memset_c_io(unsigned long, unsigned long, long);

#define memcpy_fromio(to,from,len) \
  _memcpy_fromio((to),(unsigned long)(from),(len))
#define memcpy_toio(to,from,len) \
  _memcpy_toio((unsigned long)(to),(from),(len))
#define memset_io(addr,c,len) \
  _memset_c_io((unsigned long)(addr),0x0101010101010101UL*(u8)(c),(len))

#define __HAVE_ARCH_MEMSETW_IO
#define memsetw_io(addr,c,len) \
  _memset_c_io((unsigned long)(addr),0x0001000100010001UL*(u16)(c),(len))

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
 * only used by some shared memory 8390 Ethernet cards anyway.
 */

#define eth_io_copy_and_sum(skb,src,len,unused) \
  memcpy_fromio((skb)->data,(src),(len))

static inline int
check_signature(unsigned long io_addr, const unsigned char *signature,
		int length)
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
 * The Alpha Jensen hardware for some rather strange reason puts
 * the RTC clock at 0x170 instead of 0x70. Probably due to some
 * misguided idea about using 0x70 for NMI stuff.
 *
 * These defines will override the defaults when doing RTC queries
 */

#ifdef CONFIG_ALPHA_GENERIC
# define RTC_PORT(x)	((x) + alpha_mv.rtc_port)
#else
# ifdef CONFIG_ALPHA_JENSEN
#  define RTC_PORT(x)	(0x170+(x))
# else
#  define RTC_PORT(x)	(0x70 + (x))
# endif
#endif
#define RTC_ALWAYS_BCD	0

#endif /* __KERNEL__ */

#endif /* __ALPHA_IO_H */
