/*
 * Low-level hardware access stuff for Jazz family machines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996 by Ralf Baechle
 */
#include <linux/delay.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <asm/addrspace.h>
#include <asm/vector.h>
#include <asm/jazz.h>
#include <asm/jazzdma.h>
#include <asm/pgtable.h>
#include <asm/mc146818rtc.h>

static unsigned char
fd_inb(unsigned int port)
{
	unsigned char c;

	c = *(volatile unsigned char *) port;
	udelay(1);

	return c;
}

static void
fd_outb(unsigned char value, unsigned int port)
{
	*(volatile unsigned char *) port = value;
}

/*
 * How to access the floppy DMA functions.
 */
static void
fd_enable_dma(void)
{
	vdma_enable(JAZZ_FLOPPY_DMA);
}

static void
fd_disable_dma(void)
{
	vdma_disable(JAZZ_FLOPPY_DMA);
}

static int
fd_request_dma(void)
{
	return 0;
}

static void
fd_free_dma(void)
{
}

static void
fd_clear_dma_ff(void)
{
}

static void
fd_set_dma_mode(char mode)
{
	vdma_set_mode(JAZZ_FLOPPY_DMA, mode);
}

static void
fd_set_dma_addr(unsigned int a)
{
	vdma_set_addr(JAZZ_FLOPPY_DMA, vdma_phys2log(PHYSADDR(a)));
}

static void
fd_set_dma_count(unsigned int count)
{
	vdma_set_count(JAZZ_FLOPPY_DMA, count);
}

static int
fd_get_dma_residue(void)
{
	return vdma_get_residue(JAZZ_FLOPPY_DMA);
}

static void
fd_enable_irq(void)
{
}

static void
fd_disable_irq(void)
{
}

void
jazz_fd_cacheflush(const void *addr, size_t size)
{
	flush_cache_all();
}

static unsigned char
rtc_read_data(unsigned long addr)
{
	outb_p(addr, RTC_PORT(0));
	return *(char *)JAZZ_RTC_BASE;
}

static void
rtc_write_data(unsigned char data, unsigned long addr)
{
	outb_p(addr, RTC_PORT(0));
	*(char *)JAZZ_RTC_BASE = data;
}

struct feature jazz_feature = {
	/*
	 * How to access the floppy controller's ports
	 */
	fd_inb,
	fd_outb,
	/*
	 * How to access the floppy DMA functions.
	 */
	fd_enable_dma,
	fd_disable_dma,
	fd_request_dma,
	fd_free_dma,
	fd_clear_dma_ff,
	fd_set_dma_mode,
	fd_set_dma_addr,
	fd_set_dma_count,
	fd_get_dma_residue,
	fd_enable_irq,
	fd_disable_irq,
	/*
	 * How to access the RTC functions.
	 */
	rtc_read_data,
	rtc_write_data
};
