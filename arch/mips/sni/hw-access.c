/*
 * Low-level hardware access stuff for SNI RM200 PCI
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 by Ralf Baechle
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mc146818rtc.h>
#include <asm/pgtable.h>
#include <asm/vector.h>

extern int FLOPPY_IRQ;
extern int FLOPPY_DMA;

/*
 * How to access the FDC's registers.
 */
static unsigned char
fd_inb(unsigned int port)
{
	return inb_p(port);
}

static void
fd_outb(unsigned char value, unsigned int port)
{
	outb_p(value, port);
}

/*
 * How to access the floppy DMA functions.
 */
static void
fd_enable_dma(void)
{
	enable_dma(FLOPPY_DMA);
}

static void
fd_disable_dma(void)
{
	disable_dma(FLOPPY_DMA);
}

static int
fd_request_dma(void)
{
	return request_dma(FLOPPY_DMA, "floppy");
}

static void
fd_free_dma(void)
{
	free_dma(FLOPPY_DMA);
}

static void
fd_clear_dma_ff(void)
{
	clear_dma_ff(FLOPPY_DMA);
}

static void
fd_set_dma_mode(char mode)
{
	set_dma_mode(FLOPPY_DMA, mode);
}

static void
fd_set_dma_addr(unsigned int addr)
{
	set_dma_addr(FLOPPY_DMA, addr);
}

static void
fd_set_dma_count(unsigned int count)
{
	set_dma_count(FLOPPY_DMA, count);
}

static int
fd_get_dma_residue(void)
{
	return get_dma_residue(FLOPPY_DMA);
}

static void
fd_enable_irq(void)
{
	enable_irq(FLOPPY_IRQ);
}

static void
fd_disable_irq(void)
{
	disable_irq(FLOPPY_IRQ);
}

void
sni_fd_cacheflush(const void *addr, size_t size)
{
	flush_cache_all();
}

/*
 * RTC stuff (This is a guess on how the RM handles this ...)
 */
static unsigned char
rtc_read_data(unsigned long addr)
{
	outb_p(addr, RTC_PORT(0));
	return inb_p(RTC_PORT(1));
}

static void
rtc_write_data(unsigned char data, unsigned long addr)
{
	outb_p(addr, RTC_PORT(0));
	outb_p(data, RTC_PORT(1));
}

struct feature sni_rm200_pci_feature = {
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
