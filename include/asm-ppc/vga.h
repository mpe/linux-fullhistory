/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */

#ifndef _LINUX_ASM_VGA_H_
#define _LINUX_ASM_VGA_H_

#include <asm/io.h>

#include <linux/config.h>

#if defined(CONFIG_VGA_CONSOLE) || defined(CONFIG_MDA_CONSOLE)

#define VT_BUF_HAVE_RW
/*
 *  These are only needed for supporting VGA or MDA text mode, which use little
 *  endian byte ordering.
 *  In other cases, we can optimize by using native byte ordering and
 *  <linux/vt_buffer.h> has already done the right job for us.
 */

extern inline void scr_writew(u16 val, u16 *addr)
{
    writew(val, (unsigned long)addr);
}

extern inline u16 scr_readw(const u16 *addr)
{
    return readw((unsigned long)addr);
}

#define VT_BUF_HAVE_MEMCPYW
#define scr_memcpyw	memcpy

#endif /* !CONFIG_VGA_CONSOLE && !CONFIG_MDA_CONSOLE */

extern unsigned long vgacon_remap_base;
#define VGA_MAP_MEM(x) (x + vgacon_remap_base)
#define vga_readb(x) (*(x))
#define vga_writeb(x,y) (*(y) = (x))

#endif
