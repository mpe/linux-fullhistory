/* $Id: parport.h,v 1.4 1999/08/08 01:38:18 davem Exp $
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

#undef HAVE_SLOW_DEVICES

#define PARPORT_PC_MAX_PORTS	PARPORT_MAX

static struct linux_ebus_dma *sparc_ebus_dmas[PARPORT_PC_MAX_PORTS];

static __inline__ void
reset_dma(unsigned int dmanr)
{
	unsigned int dcsr;

	writel(EBUS_DCSR_RESET, &sparc_ebus_dmas[dmanr]->dcsr);
	udelay(1);
	dcsr = EBUS_DCSR_BURST_SZ_16 | EBUS_DCSR_TCI_DIS |
	       EBUS_DCSR_EN_CNT;
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
		if (dcsr & EBUS_DCSR_ERR_PEND) {
			reset_dma(dmanr);
			dcsr &= ~(EBUS_DCSR_ERR_PEND);
		}
		writel(dcsr, &sparc_ebus_dmas[dmanr]->dcsr);
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
	unsigned int dcsr;
	int res;

	res = readl(&sparc_ebus_dmas[dmanr]->dbcr);
	if (res != 0) {
		dcsr = readl(&sparc_ebus_dmas[dmanr]->dcsr);
		reset_dma(dmanr);
		writel(dcsr, &sparc_ebus_dmas[dmanr]->dcsr);
	}
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
				unsigned long base = edev->base_address[0];
				unsigned long config = edev->base_address[1];
				unsigned char cfg;

				sparc_ebus_dmas[count] =
						(struct linux_ebus_dma *)
							edev->base_address[2];
				reset_dma(count);

				/* Enable ECP, set bit 2 of the CTR first */
				outb(0x04, base + 0x02);
				cfg = ns87303_readb(config, PCR);
				cfg |= (PCR_ECP_ENABLE | PCR_ECP_CLK_ENA);
				ns87303_writeb(config, PCR, cfg);

				/* CTR bit 5 controls direction of port */
				cfg = ns87303_readb(config, PTR);
				cfg |= PTR_LPT_REG_DIR;
				ns87303_writeb(config, PTR, cfg);

				/* Configure IRQ to Push Pull, Level Low */
				cfg = ns87303_readb(config, PCR);
				cfg &= ~(PCR_IRQ_ODRAIN);
				cfg |= PCR_IRQ_POLAR;
				ns87303_writeb(config, PCR, cfg);

#ifndef HAVE_SLOW_DEVICES
				/* Enable Zero Wait State for ECP */
				cfg = ns87303_readb(config, FCR);
				cfg |= FCR_ZWS_ENA;
				ns87303_writeb(config, FCR, cfg);
#endif

				if (parport_pc_probe_port(base, base + 0x400,
							  edev->irqs[0],
							  count))
					count++;
			}
		}
	}

	count += parport_pc_init_pci(PARPORT_IRQ_AUTO, PARPORT_DMA_NONE);
	return count;
}

#endif /* !(_ASM_SPARC64_PARPORT_H */
