/*
 * Deskstation Tyne specific C parts
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 by Ralf Baechle
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/io.h>

/*
 * How to access the FDC's registers.
 */
unsigned char deskstation_tyne_fd_inb(unsigned int port)
{
	return inb_p(port);
}

void deskstation_tyne_fd_outb(unsigned char value, unsigned int port)
{
	outb_p(value, port);
}

/*
 * How to access the floppy DMA functions.
 */
void deskstation_tyne_fd_enable_dma(void)
{
	enable_dma(FLOPPY_DMA);
}

void deskstation_tyne_fd_disable_dma(void)
{
	disable_dma(FLOPPY_DMA);
}

int deskstation_tyne_fd_request_dma(void)
{
	return request_dma(FLOPPY_DMA, "floppy");
}

void deskstation_tyne_fd_free_dma(void)
{
	free_dma(FLOPPY_DMA);
}

void deskstation_tyne_fd_clear_dma_ff(void)
{
	clear_dma_ff(FLOPPY_DMA);
}

int deskstation_tyne_fd_set_dma_mode(char mode)
{
	return set_dma_mode(FLOPPY_DMA, mode);
}

void deskstation_tyne_fd_set_dma_addr(unsigned int a)
{
	set_dma_addr(FLOPPY_DMA, addr);
}

void deskstation_tyne_fd_set_dma_count(unsigned int count)
{
	set_dma_count(FLOPPY_DMA, count);
}

int deskstation_tyne_fd_get_dma_residue(void)
{
	return get_dma_residue(FLOPPY_DMA);
}

void deskstation_tyne_fd_enable_irq(void)
{
	enable_irq(FLOPPY_IRQ);
}

void deskstation_tyne_fd_disable_irq(void)
{
	disable_irq(FLOPPY_IRQ);
}

void deskstation_tyne_fd_cacheflush(unsigned char *addr, unsigned int)
{
	sys_cacheflush((void *)addr, size, DCACHE);
}


/*
 * Tiny Tyne DMA buffer allocator
 *
 * Untested for a long time and changed again and again ...
 * Sorry, but no hardware to test ...
 */
static unsigned long allocated;

/*
 * Not very sophisticated, but should suffice for now...
 */
unsigned long deskstation_tyne_dma_alloc(size_t size)
{
	unsigned long ret = allocated;
	allocated += size;
	if (allocated > boot_info.dma_cache_size)
		ret = -1;
	return ret;
}

void deskstation_tyne_dma_init(void)
{
	if (boot_info.machtype != MACH_DESKSTATION_TYNE)
		return;
	allocated = 0;
	printk ("Deskstation Tyne DMA (%luk) buffer initialized.\n",
	        boot_info.dma_cache_size >> 10);
}
