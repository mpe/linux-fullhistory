#ifndef _M68K_IO_H
#define _M68K_IO_H

#ifdef __KERNEL__

#include <linux/config.h>

#ifdef CONFIG_ATARI
#include <asm/atarihw.h>

#define SLOW_DOWN_IO	do { if (MACH_IS_ATARI) MFPDELAY(); } while (0)
#endif

#include <asm/virtconvert.h>

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

#define writeb(b,addr) (void)((*(volatile unsigned char *) (addr)) = (b))
#define writew(b,addr) (void)((*(volatile unsigned short *) (addr)) = (b))
#define writel(b,addr) (void)((*(volatile unsigned int *) (addr)) = (b))

#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

#define inb_p(addr) readb(addr)
#define inb(addr) readb(addr)

#define outb(x,addr) ((void) writeb(x,addr))
#define outb_p(x,addr) outb(x,addr)

#endif /* __KERNEL__ */

#endif /* _M68K_IO_H */
