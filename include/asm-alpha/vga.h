/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */

#ifndef _LINUX_ASM_VGA_H_
#define _LINUX_ASM_VGA_H_

#include <asm/io.h>

#define VT_BUF_HAVE_RW
#define VT_BUF_HAVE_MEMSETW
#define VT_BUF_HAVE_MEMCPYF

extern inline void scr_writew(u16 val, u16 *addr)
{
	if (__is_ioaddr((unsigned long) addr))
		__raw_writew(val, (unsigned long) addr);
	else
		*addr = val;
}

extern inline u16 scr_readw(const u16 *addr)
{
	if (__is_ioaddr((unsigned long) addr))
		return __raw_readw((unsigned long) addr);
	else
		return *addr;
}

extern inline void scr_memsetw(u16 *s, u16 c, unsigned int count)
{
	if (__is_ioaddr((unsigned long) s))
		memsetw_io(s, c, count);
	else
		memsetw(s, c, count);
}

extern inline void scr_memcpyw_from(u16 *d, const u16 *s, unsigned int count)
{
	memcpy_fromio(d, s, count);
}

extern inline void scr_memcpyw_to(u16 *d, const u16 *s, unsigned int count)
{
	memcpy_toio(d, s, count);
}

/* ??? These are currently only used for downloading character sets.  As
   such, they don't need memory barriers.  Is this all they are intended
   to be used for?  */
#define vga_readb	readb
#define vga_writeb	writeb

#define VGA_MAP_MEM(x)	((unsigned long) ioremap((x), 0))

#endif
