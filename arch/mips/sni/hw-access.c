/* $Id: hw-access.c,v 1.5 1998/05/07 00:39:56 ralf Exp $
 *
 * Low-level hardware access stuff for SNI RM200 PCI
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kbdcntrlr.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/keyboard.h>
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
fd_enable_dma(int channel)
{
	enable_dma(channel);
}

static void
fd_disable_dma(int channel)
{
	disable_dma(channel);
}

static int
fd_request_dma(int channel)
{
	return request_dma(channel, "floppy");
}

static void
fd_free_dma(int channel)
{
	free_dma(channel);
}

static void
fd_clear_dma_ff(int channel)
{
	clear_dma_ff(channel);
}

static void
fd_set_dma_mode(int channel, char mode)
{
	set_dma_mode(channel, mode);
}

static void
fd_set_dma_addr(int channel, unsigned int addr)
{
	set_dma_addr(channel, addr);
}

static void
fd_set_dma_count(int channel, unsigned int count)
{
	set_dma_count(channel, count);
}

static int
fd_get_dma_residue(int channel)
{
	return get_dma_residue(channel);
}

static void
fd_enable_irq(int irq)
{
	enable_irq(irq);
}

static void
fd_disable_irq(int irq)
{
	disable_irq(irq);
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

#define KBD_STAT_IBF		0x02	/* Keyboard input buffer full */

static unsigned char sni_read_input(void)
{
	return inb(KBD_DATA_REG);
}

static void sni_write_output(unsigned char val)
{
	int status;

	do {
		status = inb(KBD_CNTL_REG);
	} while (status & KBD_STAT_IBF);
	outb(val, KBD_DATA_REG);
}

static void sni_write_command(unsigned char val)
{
	int status;

	do {
		status = inb(KBD_CNTL_REG);
	} while (status & KBD_STAT_IBF);
	outb(val, KBD_CNTL_REG);
}

static unsigned char sni_read_status(void)
{
	return inb(KBD_STATUS_REG);
}

__initfunc(void sni_rm200_keyboard_setup(void))
{
	kbd_read_input = sni_read_input;
	kbd_write_output = sni_write_output;
	kbd_write_command = sni_write_command;
	kbd_read_status = sni_read_status;
	request_region(0x60, 16, "keyboard");
}
