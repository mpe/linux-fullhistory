/*
 * arch/arm/kernel/dma-rpc.c
 *
 * Copyright (C) 1998 Russell King
 *
 * DMA functions specific to RiscPC architecture
 */
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/fiq.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/uaccess.h>

#include "dma.h"

static struct fiq_handler fh = { NULL, "floppydma", NULL, NULL };

#if 0
typedef enum {
	dma_size_8	= 1,
	dma_size_16	= 2,
	dma_size_32	= 4,
	dma_size_128	= 16
} dma_size_t;

typedef struct {
	dma_size_t	transfersize;
} dma_t;
#endif

#define TRANSFER_SIZE	2

#define CURA	(0)
#define ENDA	((IOMD_IO0ENDA - IOMD_IO0CURA) << 2)
#define CURB	((IOMD_IO0CURB - IOMD_IO0CURA) << 2)
#define ENDB	((IOMD_IO0ENDB - IOMD_IO0CURA) << 2)
#define CR	((IOMD_IO0CR - IOMD_IO0CURA) << 2)
#define ST	((IOMD_IO0ST - IOMD_IO0CURA) << 2)

#define state_prog_a	0
#define state_wait_a	1
#define state_wait_b	2

static void arch_get_next_sg(dmasg_t *sg, dma_t *dma)
{
	unsigned long end, offset, flags = 0;

	if (dma->sg) {
		sg->address = dma->sg->address;
		offset = sg->address & ~PAGE_MASK;

		end = offset + dma->sg->length;

		if (end > PAGE_SIZE)
			end = PAGE_SIZE;

		if (offset + (int) TRANSFER_SIZE > end)
			flags |= DMA_END_L;

		sg->length = end - TRANSFER_SIZE;

		dma->sg->length -= end - offset;
		dma->sg->address += end - offset;

		if (dma->sg->length == 0) {
			if (dma->sgcount > 1) {
				dma->sg++;
				dma->sgcount--;
			} else {
				dma->sg = NULL;
				flags |= DMA_END_S;
			}
		}
	} else {
		flags = DMA_END_S | DMA_END_L;
		sg->address = 0;
		sg->length = 0;
	}

	sg->length |= flags;
}

static inline void arch_setup_dma_a(dmasg_t *sg, dma_t *dma)
{
	outl_t(sg->address, dma->dma_base + CURA);
	outl_t(sg->length, dma->dma_base + ENDA);
}

static inline void arch_setup_dma_b(dmasg_t *sg, dma_t *dma)
{
	outl_t(sg->address, dma->dma_base + CURB);
	outl_t(sg->length, dma->dma_base + ENDB);
}

static void arch_dma_handle(int irq, void *dev_id, struct pt_regs *regs)
{
	dma_t *dma = (dma_t *)dev_id;
	unsigned int status = 0, no_buffer = dma->sg == NULL;

	do {
		switch (dma->state) {
		case state_prog_a:
			arch_get_next_sg(&dma->cur_sg, dma);
			arch_setup_dma_a(&dma->cur_sg, dma);
			dma->state = state_wait_a;

		case state_wait_a:
			status = inb_t(dma->dma_base + ST);
			switch (status & (DMA_ST_OFL|DMA_ST_INT|DMA_ST_AB)) {
			case DMA_ST_OFL|DMA_ST_INT:
				arch_get_next_sg(&dma->cur_sg, dma);
				arch_setup_dma_a(&dma->cur_sg, dma);
				break;

			case DMA_ST_INT:
				arch_get_next_sg(&dma->cur_sg, dma);
				arch_setup_dma_b(&dma->cur_sg, dma);
				dma->state = state_wait_b;
				break;

			case DMA_ST_OFL|DMA_ST_INT|DMA_ST_AB:
				arch_setup_dma_b(&dma->cur_sg, dma);
				dma->state = state_wait_b;
				break;
			}
			break;

		case state_wait_b:
			status = inb_t(dma->dma_base + ST);
			switch (status & (DMA_ST_OFL|DMA_ST_INT|DMA_ST_AB)) {
			case DMA_ST_OFL|DMA_ST_INT|DMA_ST_AB:
				arch_get_next_sg(&dma->cur_sg, dma);
				arch_setup_dma_b(&dma->cur_sg, dma);
				break;

			case DMA_ST_INT|DMA_ST_AB:
				arch_get_next_sg(&dma->cur_sg, dma);
				arch_setup_dma_a(&dma->cur_sg, dma);
				dma->state = state_wait_a;
				break;

			case DMA_ST_OFL|DMA_ST_INT:
				arch_setup_dma_a(&dma->cur_sg, dma);
				dma->state = state_wait_a;
				break;
			}
			break;
		}
	} while (dma->sg && (status & DMA_ST_INT));

	if (no_buffer)
		disable_irq(irq);
}

int arch_request_dma(dmach_t channel, dma_t *dma, const char *dev_name)
{
	unsigned long flags;
	int ret;

	switch (channel) {
	case DMA_0:
	case DMA_1:
	case DMA_2:
	case DMA_3:
	case DMA_S0:
	case DMA_S1:
		save_flags_cli(flags);
		ret = request_irq(dma->dma_irq, arch_dma_handle,
				  SA_INTERRUPT, dev_name, dma);
		if (!ret)
			disable_irq(dma->dma_irq);
		restore_flags(flags);
		break;

	case DMA_VIRTUAL_FLOPPY:
	case DMA_VIRTUAL_SOUND:
		ret = 0;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

void arch_free_dma(dmach_t channel, dma_t *dma)
{
	switch (channel) {
	case DMA_0:
	case DMA_1:
	case DMA_2:
	case DMA_3:
	case DMA_S0:
	case DMA_S1:
		free_irq(dma->dma_irq, dma);
		break;

	default:
		break;
	}
}

int arch_get_dma_residue(dmach_t channel, dma_t *dma)
{
	int residue = 0;

	switch (channel) {
	case DMA_0:	/* Physical DMA channels */
	case DMA_1:
	case DMA_2:
	case DMA_3:
	case DMA_S0:
	case DMA_S1:
		break;

	case DMA_VIRTUAL_FLOPPY: {
		extern int floppy_fiqresidual(void);
		residue = floppy_fiqresidual();
		}
		break;
	}
	return residue;
}

void arch_enable_dma(dmach_t channel, dma_t *dma)
{
	unsigned long dma_base = dma->dma_base;
	unsigned int ctrl;

	switch (channel) {
	case DMA_0:	/* Physical DMA channels */
	case DMA_1:
	case DMA_2:
	case DMA_3:
	case DMA_S0:
	case DMA_S1:
		ctrl = TRANSFER_SIZE | DMA_CR_E;

		if (dma->invalid) {
			dma->invalid = 0;

			outb_t(DMA_CR_C, dma_base + CR);
			dma->state = state_prog_a;
		}
		
		if (dma->dma_mode == DMA_MODE_READ)
			ctrl |= DMA_CR_D;

		outb_t(ctrl, dma_base + CR);
		enable_irq(dma->dma_irq);
		break;

	case DMA_VIRTUAL_FLOPPY: {
		void *fiqhandler_start;
		unsigned int fiqhandler_length;
		struct pt_regs regs;

		if (dma->dma_mode == DMA_MODE_READ) {
			extern unsigned char floppy_fiqin_start, floppy_fiqin_end;
			fiqhandler_start = &floppy_fiqin_start;
			fiqhandler_length = &floppy_fiqin_end - &floppy_fiqin_start;
		} else {
			extern unsigned char floppy_fiqout_start, floppy_fiqout_end;
			fiqhandler_start = &floppy_fiqout_start;
			fiqhandler_length = &floppy_fiqout_end - &floppy_fiqout_start;
		}

		regs.ARM_r9  = dma->buf.length;
		regs.ARM_r10 = __bus_to_virt(dma->buf.address);
		regs.ARM_fp  = (int)PCIO_FLOPPYDMABASE;

		if (claim_fiq(&fh)) {
			printk("floppydma: couldn't claim FIQ.\n");
			return;
		}

		set_fiq_handler(fiqhandler_start, fiqhandler_length);
		set_fiq_regs(&regs);
		enable_irq(dma->dma_irq);

		}
		break;

	default:
		break;
	}
}

void arch_disable_dma(dmach_t channel, dma_t *dma)
{
	unsigned long dma_base = dma->dma_base;
	unsigned int ctrl;

	switch (channel) {
	case DMA_0:	/* Physical DMA channels */
	case DMA_1:
	case DMA_2:
	case DMA_3:
	case DMA_S0:
	case DMA_S1:
		disable_irq(dma->dma_irq);
		ctrl = inb_t(dma_base + CR);
		outb_t(ctrl & ~DMA_CR_E, dma_base + CR);
		break;

	case DMA_VIRTUAL_FLOPPY:
		disable_irq(dma->dma_irq);
		release_fiq(&fh);
		break;
	}
}

__initfunc(void arch_dma_init(dma_t *dma))
{
	outb(0, IOMD_IO0CR);
	outb(0, IOMD_IO1CR);
	outb(0, IOMD_IO2CR);
	outb(0, IOMD_IO3CR);

//	outb(0xf0, IOMD_DMATCR);

	dma[0].dma_base = ioaddr(IOMD_IO0CURA);
	dma[0].dma_irq  = IRQ_DMA0;
	dma[1].dma_base = ioaddr(IOMD_IO1CURA);
	dma[1].dma_irq  = IRQ_DMA1;
	dma[2].dma_base = ioaddr(IOMD_IO2CURA);
	dma[2].dma_irq  = IRQ_DMA2;
	dma[3].dma_base = ioaddr(IOMD_IO3CURA);
	dma[3].dma_irq  = IRQ_DMA3;
	dma[4].dma_base = ioaddr(IOMD_SD0CURA);
	dma[4].dma_irq  = IRQ_DMAS0;
	dma[5].dma_base = ioaddr(IOMD_SD1CURA);
	dma[5].dma_irq  = IRQ_DMAS1;
	dma[6].dma_irq  = 64;

	/* Setup DMA channels 2,3 to be for podules
	 * and channels 0,1 for internal devices
	 */
	outb(DMA_EXT_IO3|DMA_EXT_IO2, IOMD_DMAEXT);
}
