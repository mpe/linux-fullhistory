/* $Id: parport.h,v 1.7 2000/01/28 13:43:14 jj Exp $
 * parport.h: sparc64 specific parport initialization and dma.
 *
 * Copyright (C) 1999  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _ASM_SPARC64_PARPORT_H
#define _ASM_SPARC64_PARPORT_H 1

#include <linux/config.h>
#include <asm/ebus.h>
#include <asm/ns87303.h>

#ifdef CONFIG_PARPORT_PC_PCMCIA
#define __maybe_init
#define __maybe_initdata
#else
#define __maybe_init __init
#define __maybe_initdata __initdata
#endif

#define PARPORT_PC_MAX_PORTS	PARPORT_MAX

static struct linux_ebus_dma *sparc_ebus_dmas[PARPORT_PC_MAX_PORTS];

static __inline__ void
reset_dma(unsigned int dmanr)
{
	unsigned int dcsr;

	writel(EBUS_DCSR_RESET, &sparc_ebus_dmas[dmanr]->dcsr);
	udelay(1);
	dcsr = EBUS_DCSR_BURST_SZ_16 | EBUS_DCSR_TCI_DIS |
	       EBUS_DCSR_EN_CNT | EBUS_DCSR_INT_EN;
	writel(dcsr, &sparc_ebus_dmas[dmanr]->dcsr);
}

static __inline__ void
enable_dma(unsigned int dmanr)
{
	unsigned int dcsr;

	dcsr = readl(&sparc_ebus_dmas[dmanr]->dcsr);
	dcsr |= EBUS_DCSR_EN_DMA;
	writel(dcsr, &sparc_ebus_dmas[dmanr]->dcsr);
}

static __inline__ void
disable_dma(unsigned int dmanr)
{
	unsigned int dcsr;

	dcsr = readl(&sparc_ebus_dmas[dmanr]->dcsr);
	if (dcsr & EBUS_DCSR_EN_DMA) {
		while (dcsr & EBUS_DCSR_DRAIN) {
			udelay(1);
			dcsr = readl(&sparc_ebus_dmas[dmanr]->dcsr);
		}
		dcsr &= ~(EBUS_DCSR_EN_DMA);
		writel(dcsr, &sparc_ebus_dmas[dmanr]->dcsr);

		dcsr = readl(&sparc_ebus_dmas[dmanr]->dcsr);
		if (dcsr & EBUS_DCSR_ERR_PEND)
			reset_dma(dmanr);
	}
}

static __inline__ void
clear_dma_ff(unsigned int dmanr)
{
	/* nothing */
}

static __inline__ void
set_dma_mode(unsigned int dmanr, char mode)
{
	unsigned int dcsr;

	dcsr = readl(&sparc_ebus_dmas[dmanr]->dcsr);
	dcsr |= EBUS_DCSR_EN_CNT | EBUS_DCSR_TC;
	if (mode == DMA_MODE_WRITE)
		dcsr &= ~(EBUS_DCSR_WRITE);
	else
		dcsr |= EBUS_DCSR_WRITE;
	writel(dcsr, &sparc_ebus_dmas[dmanr]->dcsr);
}

static __inline__ void
set_dma_addr(unsigned int dmanr, unsigned int addr)
{
	writel(addr, &sparc_ebus_dmas[dmanr]->dacr);
}

static __inline__ void
set_dma_count(unsigned int dmanr, unsigned int count)
{
	writel(count, &sparc_ebus_dmas[dmanr]->dbcr);
}

static __inline__ int
get_dma_residue(unsigned int dmanr)
{
	int res;

	res = readl(&sparc_ebus_dmas[dmanr]->dbcr);
	if (res != 0)
		reset_dma(dmanr);
	return res;
}

static int __maybe_init parport_pc_init_pci(int irq, int dma);

static int user_specified __initdata = 0;

int __init
parport_pc_init(int *io, int *io_hi, int *irq, int *dma)
{
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;
	int count = 0;

	if (!pci_present())
		return 0;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, "ecpp")) {
				unsigned long base = edev->resource[0].start;
				unsigned long config = edev->resource[1].start;

				sparc_ebus_dmas[count] =
						(struct linux_ebus_dma *)
							edev->resource[2].start;
				reset_dma(count);

				/* Configure IRQ to Push Pull, Level Low */
				/* Enable ECP, set bit 2 of the CTR first */
				outb(0x04, base + 0x02);
				ns87303_modify(config, PCR,
					       PCR_EPP_ENABLE |
					       PCR_IRQ_ODRAIN,
					       PCR_ECP_ENABLE |
					       PCR_ECP_CLK_ENA |
					       PCR_IRQ_POLAR);

				/* CTR bit 5 controls direction of port */
				ns87303_modify(config, PTR,
					       0, PTR_LPT_REG_DIR);

				if (parport_pc_probe_port(base, base + 0x400,
							  edev->irqs[0],
							  count, ebus->self))
					count++;
			}
		}
	}

	count += parport_pc_init_pci(PARPORT_IRQ_AUTO, PARPORT_DMA_NONE);
	return count;
}

#endif /* !(_ASM_SPARC64_PARPORT_H */
