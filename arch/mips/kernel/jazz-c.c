/*
 * Jazz specific C parts
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Ralf Baechle
 */
#include <linux/delay.h>

#include <asm/cachectl.h>
#include <asm/jazz.h>
#include <asm/jazzdma.h>
#include <asm/segment.h>

unsigned char jazz_fd_inb(unsigned int port)
{
	unsigned char c;

	c = *(volatile unsigned char *) port;
	udelay(1);

	return c;
}

void jazz_fd_outb(unsigned char value, unsigned int port)
{
	*(volatile unsigned char *) port = value;
}

/*
 * How to access the floppy DMA functions.
 */
void jazz_fd_enable_dma(void)
{
	vdma_enable(JAZZ_FLOPPY_DMA);
}

void jazz_fd_disable_dma(void)
{
	vdma_disable(JAZZ_FLOPPY_DMA);
}

int jazz_fd_request_dma(void)
{
	return 0;
}

void jazz_fd_free_dma(void)
{
}

void jazz_fd_clear_dma_ff(void)
{
}

void jazz_fd_set_dma_mode(char mode)
{
	vdma_set_mode(JAZZ_FLOPPY_DMA, mode);
}

void jazz_fd_set_dma_addr(unsigned int a)
{
	vdma_set_addr(JAZZ_FLOPPY_DMA, vdma_phys2log(PHYSADDR(a)));
}

void jazz_fd_set_dma_count(unsigned int count)
{
	vdma_set_count(JAZZ_FLOPPY_DMA, count);
}

int jazz_fd_get_dma_residue(void)
{
	return vdma_get_residue(JAZZ_FLOPPY_DMA);
}

void jazz_fd_enable_irq(void)
{
}

void jazz_fd_disable_irq(void)
{
}

void jazz_fd_cacheflush(unsigned char *addr, unsigned int size)
{
	sys_cacheflush((void *)addr, size, DCACHE);
}

unsigned char jazz_rtc_read_data(void)
{
	return *(char *)JAZZ_RTC_BASE;
}

void jazz_rtc_write_data(unsigned char data)
{
	*(char *)JAZZ_RTC_BASE = data;
}
