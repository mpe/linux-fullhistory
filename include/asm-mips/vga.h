/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */

#ifndef _LINUX_ASM_VGA_H_
#define _LINUX_ASM_VGA_H_

#include <asm/io.h>

#define VT_BUF_HAVE_RW

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

#define vga_readb readb
#define vga_writeb writeb

#define VGA_MAP_MEM(x) (x)

#endif
