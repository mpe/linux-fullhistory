/*
 * Architecture specific parts of the Floppy driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997 Ralf Baechle
 *
 * $Id: floppy.h,v 1.3 1997/09/07 03:59:02 ralf Exp $
 */
#ifndef __ASM_MIPS_FLOPPY_H
#define __ASM_MIPS_FLOPPY_H

#include <linux/config.h>
#include <asm/bootinfo.h>
#include <asm/jazz.h>
#include <asm/jazzdma.h>
#include <asm/mipsconfig.h>
#include <asm/vector.h>

#define fd_inb(port)			feature->fd_inb(port)
#define fd_outb(value,port)		feature->fd_outb(value,port)

#define fd_enable_dma(channel)		feature->fd_enable_dma(channel)
#define fd_disable_dma(channel)		feature->fd_disable_dma(channel)
#define fd_request_dma(channel)		feature->fd_request_dma(channel)
#define fd_free_dma(channel)		feature->fd_free_dma(channel)
#define fd_clear_dma_ff(channel)	feature->fd_clear_dma_ff(channel)
#define fd_set_dma_mode(channel, mode)	feature->fd_set_dma_mode(channel, mode)
#define fd_set_dma_addr(channel, addr)	feature->fd_set_dma_addr(channel, \
					         virt_to_bus(addr))
#define fd_set_dma_count(channel,count)	feature->fd_set_dma_count(channel,count)
#define fd_get_dma_residue(channel)	feature->fd_get_dma_residue(channel)

#define fd_enable_irq(irq)		feature->fd_enable_irq(irq)
#define fd_disable_irq(irq)		feature->fd_disable_irq(irq)
#define fd_request_irq(irq)		request_irq(irq, floppy_interrupt, \
						    SA_INTERRUPT \
					            | SA_SAMPLE_RANDOM, \
				                    "floppy", NULL)
#define fd_free_irq(irq)		free_irq(irq, NULL);

#define MAX_BUFFER_SECTORS 24

/* Pure 2^n version of get_order */
extern inline int __get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

extern inline unsigned long mips_dma_mem_alloc(unsigned long size)
{
	int order = __get_order(size);
	unsigned long mem;

	mem = __get_dma_pages(GFP_KERNEL,order);
	if(!mem)
		return 0;
#ifdef CONFIG_MIPS_JAZZ
        if (mips_machgroup == MACH_GROUP_JAZZ)
		vdma_alloc(PHYSADDR(mem), size);
#endif
	return mem;
}

extern inline void mips_dma_mem_free(unsigned long addr, unsigned long size)
{       
#ifdef CONFIG_MIPS_JAZZ
        if (mips_machgroup == MACH_GROUP_JAZZ)
		vdma_free(PHYSADDR(addr));
#endif
	free_pages(addr, __get_order(size));	
}

#define fd_dma_mem_alloc(size) mips_dma_mem_alloc(size)
#define fd_dma_mem_free(mem,size) mips_dma_mem_free(mem,size)

/*
 * And on Mips's the CMOS info fails also ...
 *
 * FIXME: This information should come from the ARC configuration tree
 *        or whereever a particular machine has stored this ...
 */
#define FLOPPY0_TYPE 4		/* this is wrong for the Olli M700, but who cares... */
#define FLOPPY1_TYPE 0

#define FDC1			((mips_machgroup == MACH_GROUP_JAZZ) ? \
				JAZZ_FDC_BASE : 0x3f0)
static int FDC2=-1;

#define N_FDC 1			/* do you *really* want a second controller? */
#define N_DRIVE 8

#define FLOPPY_MOTOR_MASK 0xf0

/*
 * The DMA channel used by the floppy controller cannot access data at
 * addresses >= 16MB
 *
 * Went back to the 1MB limit, as some people had problems with the floppy
 * driver otherwise. It doesn't matter much for performance anyway, as most
 * floppy accesses go through the track buffer.
 *
 * On MIPSes using vdma, this actually means that *all* transfers go thru
 * the * track buffer since 0x1000000 is always smaller than KSEG0/1.
 * Actually this needs to be a bit more complicated since the so much different
 * hardware available with MIPS CPUs ...
 */
#define CROSS_64KB(a,s) ((unsigned long)(a)/K_64 != ((unsigned long)(a) + (s) - 1) / K_64)

#endif /* __ASM_MIPS_FLOPPY_H */
