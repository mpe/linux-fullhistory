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
	st_le16(addr, val);
}

extern inline u16 scr_readw(u16 *addr)
{
	return ld_le16(addr);
}

#define VT_BUF_HAVE_MEMCPYF
#define scr_memcpyw_from memcpy
#define scr_memcpyw_to memcpy

#define VGA_MAP_MEM(x) (x + _ISA_MEM_BASE)
#define vga_readb(x) (*(x))
#define vga_writeb(x,y) (*(y) = (x))

#endif
