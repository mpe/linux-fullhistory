#ifndef ASMARM_VGA_H
#define ASMARM_VGA_H

#include <asm/hardware.h>
#include <asm/io.h>

#define VGA_MAP_MEM(x)	(PCIMEM_BASE + (x))

#define vga_readb(x)	(*(x))
#define vga_writeb(x,y)	(*(y) = (x))

#endif
