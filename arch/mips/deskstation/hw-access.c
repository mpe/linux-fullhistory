/*
 * Low-level hardware access stuff for Deskstation rPC44/Tyne
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997 by Ralf Baechle
 *
 * $Id: hw-access.c,v 1.3 1997/07/29 17:46:42 ralf Exp $
 */
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/kbdcntrlr.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/vector.h>

extern int FLOPPY_IRQ;
extern int FLOPPY_DMA;

asmlinkage extern void deskstation_handle_int(void);

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
	disable_dma(int channel);
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
deskstation_fd_cacheflush(const void *addr, size_t size)
{
	flush_cache_all();
}

/*
 * RTC stuff
 */
static unsigned char *
rtc_read_data()
{
	return 0;
}

static void
rtc_write_data(unsigned char data)
{
}

/*
 * KLUDGE
 */
static unsigned long
vdma_alloc(unsigned long paddr, unsigned long size)
{
	return 0;
}

#ifdef CONFIG_DESKSTATION_TYNE
struct feature deskstation_tyne_feature = {
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
#endif

#ifdef CONFIG_DESKSTATION_RPC44
struct feature deskstation_rpc44_feature = {
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
#endif

static unsigned char dtc_read_input(void)
{
	return inb(KBD_DATA_REG);
}

static void dtc_write_output(unsigned char val)
{
	outb(val, KBD_DATA_REG);
}

static void dtc_write_command(unsigned char val)
{
	outb(val, KBD_CNTL_REG);
}

static unsigned char dtc_read_status(void)
{
	return inb(KBD_STATUS_REG);
}

static void dtc_rm200_keyboard_setup(void)
{
	kbd_read_input = dtc_read_input;
	kbd_write_output = dtc_write_output;
	kbd_write_command = dtc_write_command;
	kbd_read_status = dtc_read_status;
	request_region(0x60, 16, "keyboard");
}
