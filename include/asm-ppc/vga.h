/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */

#ifndef _LINUX_ASM_VGA_H_
#define _LINUX_ASM_VGA_H_

#include <asm/io.h>
#include <asm/processor.h>

#include <linux/config.h>
#include <linux/console.h>

#define VT_BUF_HAVE_RW

extern inline void scr_writew(u16 val, u16 *addr)
{
	/* If using vgacon (not fbcon) byteswap the writes.
	 * If non-vgacon assume fbcon and don't byteswap
	 * just like include/linux/vt_buffer.h.
	 * XXX: this is a performance loss so get rid of it
	 *      as soon as fbcon works on prep.
	 * -- Cort
	 */
#ifdef CONFIG_FB
	if ( conswitchp != &vga_con )
		(*(addr) = (val));
	else
#endif /* CONFIG_FB */
		st_le16(addr, val);
}

extern inline u16 scr_readw(const u16 *addr)
{
#ifdef CONFIG_FB
	if ( conswitchp != &vga_con )
		return (*(addr));
	else
#endif /* CONFIG_FB */
		return ld_le16((unsigned short *)addr);
}

#define VT_BUF_HAVE_MEMCPYF
#define scr_memcpyw_from memcpy
#define scr_memcpyw_to memcpy

extern unsigned long vgacon_remap_base;
#define VGA_MAP_MEM(x) (x + vgacon_remap_base)
#define vga_readb(x) (*(x))
#define vga_writeb(x,y) (*(y) = (x))

#endif
