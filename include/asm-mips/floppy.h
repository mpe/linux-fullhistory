/*
 * Architecture specific parts of the Floppy driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995
 */
#ifndef __ASM_MIPS_FLOPPY_H
#define __ASM_MIPS_FLOPPY_H

#include <asm/bootinfo.h>
#include <asm/jazz.h>
#include <asm/jazzdma.h>
#include <asm/mipsconfig.h>
#include <asm/vector.h>

#define fd_inb(port)			feature->fd_inb(port)
#define fd_outb(value,port)		feature->fd_outb(value,port)

#define fd_enable_dma()			feature->fd_enable_dma()
#define fd_disable_dma()		feature->fd_disable_dma()
#define fd_request_dma()		feature->fd_request_dma()
#define fd_free_dma()			feature->fd_free_dma()
#define fd_clear_dma_ff()		feature->fd_clear_dma_ff()
#define fd_set_dma_mode(mode)		feature->fd_set_dma_mode(mode)
#define fd_set_dma_addr(addr)		feature->fd_set_dma_addr(addr)
#define fd_set_dma_count(count)		feature->fd_set_dma_count(count)
#define fd_get_dma_residue()		feature->fd_get_dma_residue()
#define fd_enable_irq()			feature->fd_enable_irq()
#define fd_disable_irq()		feature->fd_disable_irq()
#define fd_cacheflush(addr, size)	feature->fd_cacheflush((void *)addr, size)
#define fd_request_irq()        request_irq(FLOPPY_IRQ, floppy_interrupt, \
					    SA_INTERRUPT|SA_SAMPLE_RANDOM, \
				            "floppy", NULL)
#define fd_free_irq()           free_irq(FLOPPY_IRQ, NULL);

#define MAX_BUFFER_SECTORS 24

static unsigned long mips_dma_mem_alloc(unsigned long size)
{
	int order = __get_order(size);
	unsigned long mem;

	mem = __get_dma_pages(GFP_KERNEL,order);
	if(!mem)
		return 0;
        if (boot_info.machtype == MACH_ACER_PICA_61 ||
            boot_info.machtype == MACH_MIPS_MAGNUM_4000 ||
            boot_info.machtype == MACH_OLIVETTI_M700)
		vdma_alloc(PHYSADDR(mem), size);
	return mem;
}

static void mips_dma_mem_free(unsigned long addr, unsigned long size)
{       
        if (boot_info.machtype == MACH_ACER_PICA_61 ||
            boot_info.machtype == MACH_MIPS_MAGNUM_4000 ||
            boot_info.machtype == MACH_OLIVETTI_M700)
		vdma_free(PHYSADDR(addr));
	free_pages(addr, __get_order(size));	
}

#define fd_dma_mem_alloc(mem,size) mips_dma_mem_alloc(mem,size)
#define fd_dma_mem_free(mem) mips_dma_mem_free(mem)

/*
 * And on Mips's the CMOS info fails also ...
 *
 * FIXME: This information should come from the ARC configuration tree
 *        or wherever a particular machine has stored this ...
 */
#define FLOPPY0_TYPE 4		/* this is wrong for the Olli M700, but who cares... */
#define FLOPPY1_TYPE 0

#define FDC1			((boot_info.machtype == MACH_ACER_PICA_61 || \
				boot_info.machtype == MACH_MIPS_MAGNUM_4000 || \
				boot_info.machtype == MACH_OLIVETTI_M700) ? \
				0xe0003000 : 0x3f0)
static int FDC2=-1;

#define N_FDC 1			/* do you *really* want a second controller? */
#define N_DRIVE 8

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
