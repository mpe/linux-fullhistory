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
	if ((long) addr < 0)
		*addr = val;
	else
		writew(val, (unsigned long) addr);
}

extern inline u16 scr_readw(u16 *addr)
{
	if ((long) addr < 0)
		return *addr;
	else
		return readw((unsigned long) addr);
}

extern inline void scr_memsetw(u16 *s, u16 c, unsigned int count)
{
	if ((long)s < 0)
		memsetw(s, c, count);
	else
		memsetw_io(s, c, count);
}

extern inline void scr_memcpyw_from(u16 *d, u16 *s, unsigned int count)
{
	memcpy_fromio(d, s, count);
}

extern inline void scr_memcpyw_to(u16 *d, u16 *s, unsigned int count)
{
	memcpy_toio(d, s, count);
}


#define vga_readb readb
#define vga_writeb writeb

#define VGA_MAP_MEM(x) (x)

#endif
