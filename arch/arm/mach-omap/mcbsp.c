/*
 * linux/arch/arm/omap/mcbsp.c
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Samuel Ortiz <samuel.ortiz@nokia.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Multichannel mode not supported.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/interrupt.h>

#include <asm/delay.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/arch/dma.h>
#include <asm/arch/mux.h>
#include <asm/arch/irqs.h>
#include <asm/arch/mcbsp.h>

#ifdef CONFIG_MCBSP_DEBUG
#define DBG(x...)	printk(x)
#else
#define DBG(x...)	do { } while (0)
#endif

struct omap_mcbsp {
	u32                          io_base;
	u8                           id;
	u8                           free;
	omap_mcbsp_word_length       rx_word_length;
	omap_mcbsp_word_length       tx_word_length;

	/* IRQ based TX/RX */
	int                          rx_irq;
	int                          tx_irq;

	/* DMA stuff */
	u8                           dma_rx_sync;
	short                        dma_rx_lch;
	u8                           dma_tx_sync;
	short                        dma_tx_lch;

	/* Completion queues */
	struct completion            tx_irq_completion;
	struct completion            rx_irq_completion;
	struct completion            tx_dma_completion;
	struct completion            rx_dma_completion;

	spinlock_t                   lock;
};

static struct omap_mcbsp mcbsp[OMAP_MAX_MCBSP_COUNT];


static void omap_mcbsp_dump_reg(u8 id)
{
	DBG("**** MCBSP%d regs ****\n", mcbsp[id].id);
	DBG("DRR2:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, DRR2));
	DBG("DRR1:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, DRR1));
	DBG("DXR2:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, DXR2));
	DBG("DXR1:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, DXR1));
	DBG("SPCR2: 0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, SPCR2));
	DBG("SPCR1: 0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, SPCR1));
	DBG("RCR2:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, RCR2));
	DBG("RCR1:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, RCR1));
	DBG("XCR2:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, XCR2));
	DBG("XCR1:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, XCR1));
	DBG("SRGR2: 0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, SRGR2));
	DBG("SRGR1: 0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, SRGR1));
	DBG("PCR0:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, PCR0));
	DBG("***********************\n");
}


static irqreturn_t omap_mcbsp_tx_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct omap_mcbsp * mcbsp_tx = (struct omap_mcbsp *)(dev_id);

	DBG("TX IRQ callback : 0x%x\n", OMAP_MCBSP_READ(mcbsp_tx->io_base, SPCR2));

	complete(&mcbsp_tx->tx_irq_completion);
	return IRQ_HANDLED;
}

static irqreturn_t omap_mcbsp_rx_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct omap_mcbsp * mcbsp_rx = (struct omap_mcbsp *)(dev_id);

	DBG("RX IRQ callback : 0x%x\n", OMAP_MCBSP_READ(mcbsp_rx->io_base, SPCR2));

	complete(&mcbsp_rx->rx_irq_completion);
	return IRQ_HANDLED;
}


static void omap_mcbsp_tx_dma_callback(int lch, u16 ch_status, void *data)
{
	struct omap_mcbsp * mcbsp_dma_tx = (struct omap_mcbsp *)(data);

	DBG("TX DMA callback : 0x%x\n", OMAP_MCBSP_READ(mcbsp_dma_tx->io_base, SPCR2));

	/* We can free the channels */
	omap_free_dma(mcbsp_dma_tx->dma_tx_lch);
	mcbsp_dma_tx->dma_tx_lch = -1;

	complete(&mcbsp_dma_tx->tx_dma_completion);
}

static void omap_mcbsp_rx_dma_callback(int lch, u16 ch_status, void *data)
{
	struct omap_mcbsp * mcbsp_dma_rx = (struct omap_mcbsp *)(data);

	DBG("RX DMA callback : 0x%x\n", OMAP_MCBSP_READ(mcbsp_dma_rx->io_base, SPCR2));

	/* We can free the channels */
	omap_free_dma(mcbsp_dma_rx->dma_rx_lch);
	mcbsp_dma_rx->dma_rx_lch = -1;

	complete(&mcbsp_dma_rx->rx_dma_completion);
}


/*
 * omap_mcbsp_config simply write a config to the
 * appropriate McBSP.
 * You either call this function or set the McBSP registers
 * by yourself before calling omap_mcbsp_start().
 */

void omap_mcbsp_config(unsigned int id, const struct omap_mcbsp_reg_cfg * config)
{
	u32 io_base = mcbsp[id].io_base;

	DBG("OMAP-McBSP: McBSP%d  io_base: 0x%8x\n", id+1, io_base);

	/* We write the given config */
	OMAP_MCBSP_WRITE(io_base, SPCR2, config->spcr2);
	OMAP_MCBSP_WRITE(io_base, SPCR1, config->spcr1);
	OMAP_MCBSP_WRITE(io_base, RCR2, config->rcr2);
	OMAP_MCBSP_WRITE(io_base, RCR1, config->rcr1);
	OMAP_MCBSP_WRITE(io_base, XCR2, config->xcr2);
	OMAP_MCBSP_WRITE(io_base, XCR1, config->xcr1);
	OMAP_MCBSP_WRITE(io_base, SRGR2, config->srgr2);
	OMAP_MCBSP_WRITE(io_base, SRGR1, config->srgr1);
	OMAP_MCBSP_WRITE(io_base, SRGR2, config->mcr2);
	OMAP_MCBSP_WRITE(io_base, SRGR1, config->mcr1);
	OMAP_MCBSP_WRITE(io_base, PCR0, config->pcr0);
}



static int omap_mcbsp_check(unsigned int id)
{
	if (cpu_is_omap730()) {
		if (id > OMAP_MAX_MCBSP_COUNT - 1) {
		       printk(KERN_ERR "OMAP-McBSP: McBSP%d doesn't exist\n", id + 1);
		       return -1;
		}
		return 0;
	}

	if (cpu_is_omap1510() || cpu_is_omap1610() || cpu_is_omap1710()) {
		if (id > OMAP_MAX_MCBSP_COUNT) {
			printk(KERN_ERR "OMAP-McBSP: McBSP%d doesn't exist\n", id + 1);
			return -1;
		}
		return 0;
	}

	return -1;
}

#define DSP_RSTCT2              0xe1008014

static void omap_mcbsp_dsp_request(void)
{
	if (cpu_is_omap1510() || cpu_is_omap1610() || cpu_is_omap1710()) {
		omap_writew((omap_readw(ARM_RSTCT1) | (1 << 1) | (1 << 2)),
			    ARM_RSTCT1);
		omap_writew((omap_readw(ARM_CKCTL) | 1 << EN_DSPCK),
			    ARM_CKCTL);
		omap_writew((omap_readw(ARM_IDLECT2) | (1 << EN_APICK)),
			    ARM_IDLECT2);

		/* enable 12MHz clock to mcbsp 1 & 3 */
		__raw_writew(__raw_readw(DSP_IDLECT2) | (1 << EN_XORPCK),
			     DSP_IDLECT2);
		__raw_writew(__raw_readw(DSP_RSTCT2) | 1 | 1 << 1,
			     DSP_RSTCT2);
	}
}

static void omap_mcbsp_dsp_free(void)
{
	/* Useless for now */
}


int omap_mcbsp_request(unsigned int id)
{
	int err;

	if (omap_mcbsp_check(id) < 0)
		return -EINVAL;

	/*
	 * On 1510, 1610 and 1710, McBSP1 and McBSP3
	 * are DSP public peripherals.
	 */
	if (id == OMAP_MCBSP1 || id == OMAP_MCBSP3)
		omap_mcbsp_dsp_request();

	spin_lock(&mcbsp[id].lock);
	if (!mcbsp[id].free) {
		printk (KERN_ERR "OMAP-McBSP: McBSP%d is currently in use\n", id + 1);
		spin_unlock(&mcbsp[id].lock);
		return -1;
	}

	mcbsp[id].free = 0;
	spin_unlock(&mcbsp[id].lock);

	/* We need to get IRQs here */
	err = request_irq(mcbsp[id].tx_irq, omap_mcbsp_tx_irq_handler, 0,
			  "McBSP",
			  (void *) (&mcbsp[id]));
	if (err != 0) {
		printk(KERN_ERR "OMAP-McBSP: Unable to request TX IRQ %d for McBSP%d\n",
		       mcbsp[id].tx_irq, mcbsp[id].id);
		return err;
	}

	init_completion(&(mcbsp[id].tx_irq_completion));


	err = request_irq(mcbsp[id].rx_irq, omap_mcbsp_rx_irq_handler, 0,
			  "McBSP",
			  (void *) (&mcbsp[id]));
	if (err != 0) {
		printk(KERN_ERR "OMAP-McBSP: Unable to request RX IRQ %d for McBSP%d\n",
		       mcbsp[id].rx_irq, mcbsp[id].id);
		free_irq(mcbsp[id].tx_irq, (void *) (&mcbsp[id]));
		return err;
	}

	init_completion(&(mcbsp[id].rx_irq_completion));
	return 0;

}

void omap_mcbsp_free(unsigned int id)
{
	if (omap_mcbsp_check(id) < 0)
		return;

	if (id == OMAP_MCBSP1 || id == OMAP_MCBSP3)
		omap_mcbsp_dsp_free();

	spin_lock(&mcbsp[id].lock);
	if (mcbsp[id].free) {
		printk (KERN_ERR "OMAP-McBSP: McBSP%d was not reserved\n", id + 1);
		spin_unlock(&mcbsp[id].lock);
		return;
	}

	mcbsp[id].free = 1;
	spin_unlock(&mcbsp[id].lock);

	/* Free IRQs */
	free_irq(mcbsp[id].rx_irq, (void *) (&mcbsp[id]));
	free_irq(mcbsp[id].tx_irq, (void *) (&mcbsp[id]));
}

/*
 * Here we start the McBSP, by enabling the sample
 * generator, both transmitter and receivers,
 * and the frame sync.
 */
void omap_mcbsp_start(unsigned int id)
{
	u32 io_base;
	u16 w;

	if (omap_mcbsp_check(id) < 0)
		return;

	io_base = mcbsp[id].io_base;

	mcbsp[id].rx_word_length = ((OMAP_MCBSP_READ(io_base, RCR1) >> 5) & 0x7);
	mcbsp[id].tx_word_length = ((OMAP_MCBSP_READ(io_base, XCR1) >> 5) & 0x7);

	/* Start the sample generator */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w | (1 << 6));

	/* Enable transmitter and receiver */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w | 1);

	w = OMAP_MCBSP_READ(io_base, SPCR1);
	OMAP_MCBSP_WRITE(io_base, SPCR1, w | 1);

	udelay(100);

	/* Start frame sync */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w | (1 << 7));

	/* Dump McBSP Regs */
	omap_mcbsp_dump_reg(id);

}

void omap_mcbsp_stop(unsigned int id)
{
	u32 io_base;
	u16 w;

	if (omap_mcbsp_check(id) < 0)
		return;

	io_base = mcbsp[id].io_base;

        /* Reset transmitter */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w & ~(1));

	/* Reset receiver */
	w = OMAP_MCBSP_READ(io_base, SPCR1);
	OMAP_MCBSP_WRITE(io_base, SPCR1, w & ~(1));

	/* Reset the sample rate generator */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w & ~(1 << 6));
}


/*
 * IRQ based word transmission.
 */
void omap_mcbsp_xmit_word(unsigned int id, u32 word)
{
	u32 io_base;
	omap_mcbsp_word_length word_length = mcbsp[id].tx_word_length;

	if (omap_mcbsp_check(id) < 0)
		return;

	io_base = mcbsp[id].io_base;

	wait_for_completion(&(mcbsp[id].tx_irq_completion));

	if (word_length > OMAP_MCBSP_WORD_16)
		OMAP_MCBSP_WRITE(io_base, DXR2, word >> 16);
	OMAP_MCBSP_WRITE(io_base, DXR1, word & 0xffff);
}

u32 omap_mcbsp_recv_word(unsigned int id)
{
	u32 io_base;
	u16 word_lsb, word_msb = 0;
	omap_mcbsp_word_length word_length = mcbsp[id].rx_word_length;

	if (omap_mcbsp_check(id) < 0)
		return -EINVAL;

	io_base = mcbsp[id].io_base;

	wait_for_completion(&(mcbsp[id].rx_irq_completion));

	if (word_length > OMAP_MCBSP_WORD_16)
		word_msb = OMAP_MCBSP_READ(io_base, DRR2);
	word_lsb = OMAP_MCBSP_READ(io_base, DRR1);

	return (word_lsb | (word_msb << 16));
}


/*
 * Simple DMA based buffer rx/tx routines.
 * Nothing fancy, just a single buffer tx/rx through DMA.
 * The DMA resources are released once the transfer is done.
 * For anything fancier, you should use your own customized DMA
 * routines and callbacks.
 */
int omap_mcbsp_xmit_buffer(unsigned int id, dma_addr_t buffer, unsigned int length)
{
	int dma_tx_ch;

	if (omap_mcbsp_check(id) < 0)
		return -EINVAL;

	if (omap_request_dma(mcbsp[id].dma_tx_sync, "McBSP TX", omap_mcbsp_tx_dma_callback,
			     &mcbsp[id],
			     &dma_tx_ch)) {
		printk("OMAP-McBSP: Unable to request DMA channel for McBSP%d TX. Trying IRQ based TX\n", id+1);
		return -EAGAIN;
	}
	mcbsp[id].dma_tx_lch = dma_tx_ch;

	DBG("TX DMA on channel %d\n", dma_tx_ch);

	init_completion(&(mcbsp[id].tx_dma_completion));

	omap_set_dma_transfer_params(mcbsp[id].dma_tx_lch,
				     OMAP_DMA_DATA_TYPE_S16,
				     length >> 1, 1,
				     OMAP_DMA_SYNC_ELEMENT);

	omap_set_dma_dest_params(mcbsp[id].dma_tx_lch,
				 OMAP_DMA_PORT_TIPB,
				 OMAP_DMA_AMODE_CONSTANT,
				 mcbsp[id].io_base + OMAP_MCBSP_REG_DXR1);

	omap_set_dma_src_params(mcbsp[id].dma_tx_lch,
				OMAP_DMA_PORT_EMIFF,
				OMAP_DMA_AMODE_POST_INC,
				buffer);

	omap_start_dma(mcbsp[id].dma_tx_lch);
	wait_for_completion(&(mcbsp[id].tx_dma_completion));
	return 0;
}


int omap_mcbsp_recv_buffer(unsigned int id, dma_addr_t buffer, unsigned int length)
{
	int dma_rx_ch;

	if (omap_mcbsp_check(id) < 0)
		return -EINVAL;

	if (omap_request_dma(mcbsp[id].dma_rx_sync, "McBSP RX", omap_mcbsp_rx_dma_callback,
			     &mcbsp[id],
			     &dma_rx_ch)) {
		printk("Unable to request DMA channel for McBSP%d RX. Trying IRQ based RX\n", id+1);
		return -EAGAIN;
	}
	mcbsp[id].dma_rx_lch = dma_rx_ch;

	DBG("RX DMA on channel %d\n", dma_rx_ch);

	init_completion(&(mcbsp[id].rx_dma_completion));

	omap_set_dma_transfer_params(mcbsp[id].dma_rx_lch,
				     OMAP_DMA_DATA_TYPE_S16,
				     length >> 1, 1,
				     OMAP_DMA_SYNC_ELEMENT);

	omap_set_dma_src_params(mcbsp[id].dma_rx_lch,
				OMAP_DMA_PORT_TIPB,
				OMAP_DMA_AMODE_CONSTANT,
				mcbsp[id].io_base + OMAP_MCBSP_REG_DRR1);

	omap_set_dma_dest_params(mcbsp[id].dma_rx_lch,
				 OMAP_DMA_PORT_EMIFF,
				 OMAP_DMA_AMODE_POST_INC,
				 buffer);

	omap_start_dma(mcbsp[id].dma_rx_lch);
	wait_for_completion(&(mcbsp[id].rx_dma_completion));
	return 0;
}


/*
 * SPI wrapper.
 * Since SPI setup is much simpler than the generic McBSP one,
 * this wrapper just need an omap_mcbsp_spi_cfg structure as an input.
 * Once this is done, you can call omap_mcbsp_start().
 */
void omap_mcbsp_set_spi_mode(unsigned int id, const struct omap_mcbsp_spi_cfg * spi_cfg)
{
	struct omap_mcbsp_reg_cfg mcbsp_cfg;

	if (omap_mcbsp_check(id) < 0)
		return;

	memset(&mcbsp_cfg, 0, sizeof(struct omap_mcbsp_reg_cfg));

	/* SPI has only one frame */
	mcbsp_cfg.rcr1 |= (RWDLEN1(spi_cfg->word_length) | RFRLEN1(0));
	mcbsp_cfg.xcr1 |= (XWDLEN1(spi_cfg->word_length) | XFRLEN1(0));

        /* Clock stop mode */
	if (spi_cfg->clk_stp_mode == OMAP_MCBSP_CLK_STP_MODE_NO_DELAY)
		mcbsp_cfg.spcr1 |= (1 << 12);
	else
		mcbsp_cfg.spcr1 |= (3 << 11);

	/* Set clock parities */
	if (spi_cfg->rx_clock_polarity == OMAP_MCBSP_CLK_RISING)
		mcbsp_cfg.pcr0 |= CLKRP;
	else
		mcbsp_cfg.pcr0 &= ~CLKRP;

	if (spi_cfg->tx_clock_polarity == OMAP_MCBSP_CLK_RISING)
		mcbsp_cfg.pcr0 &= ~CLKXP;
	else
		mcbsp_cfg.pcr0 |= CLKXP;

	/* Set SCLKME to 0 and CLKSM to 1 */
	mcbsp_cfg.pcr0 &= ~SCLKME;
	mcbsp_cfg.srgr2 |= CLKSM;

	/* Set FSXP */
	if (spi_cfg->fsx_polarity == OMAP_MCBSP_FS_ACTIVE_HIGH)
		mcbsp_cfg.pcr0 &= ~FSXP;
	else
		mcbsp_cfg.pcr0 |= FSXP;

	if (spi_cfg->spi_mode == OMAP_MCBSP_SPI_MASTER) {
		mcbsp_cfg.pcr0 |= CLKXM;
		mcbsp_cfg.srgr1 |= CLKGDV(spi_cfg->clk_div -1);
		mcbsp_cfg.pcr0 |= FSXM;
		mcbsp_cfg.srgr2 &= ~FSGM;
		mcbsp_cfg.xcr2 |= XDATDLY(1);
		mcbsp_cfg.rcr2 |= RDATDLY(1);
	}
	else {
		mcbsp_cfg.pcr0 &= ~CLKXM;
		mcbsp_cfg.srgr1 |= CLKGDV(1);
		mcbsp_cfg.pcr0 &= ~FSXM;
		mcbsp_cfg.xcr2 &= ~XDATDLY(3);
		mcbsp_cfg.rcr2 &= ~RDATDLY(3);
	}

	mcbsp_cfg.xcr2 &= ~XPHASE;
	mcbsp_cfg.rcr2 &= ~RPHASE;

	omap_mcbsp_config(id, &mcbsp_cfg);
}


/*
 * McBSP1 and McBSP3 are directly mapped on 1610 and 1510.
 * 730 has only 2 McBSP, and both of them are MPU peripherals.
 */
struct omap_mcbsp_info {
	u32 virt_base;
	u8 dma_rx_sync, dma_tx_sync;
	u16 rx_irq, tx_irq;
};

#ifdef CONFIG_ARCH_OMAP730
static const struct omap_mcbsp_info mcbsp_730[] = {
	[0] = { .virt_base = io_p2v(OMAP730_MCBSP1_BASE),
		.dma_rx_sync = OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP1_TX,
		.rx_irq = INT_730_McBSP1RX,
		.tx_irq = INT_730_McBSP1TX },
	[1] = { .virt_base = io_p2v(OMAP730_MCBSP2_BASE),
		.dma_rx_sync = OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP3_TX,
		.rx_irq = INT_730_McBSP2RX,
		.tx_irq = INT_730_McBSP2TX },
};
#endif

#ifdef CONFIG_ARCH_OMAP1510
static const struct omap_mcbsp_info mcbsp_1510[] = {
	[0] = { .virt_base = OMAP1510_MCBSP1_BASE,
		.dma_rx_sync = OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP1_TX,
		.rx_irq = INT_McBSP1RX,
		.tx_irq = INT_McBSP1TX },
	[1] = { .virt_base = io_p2v(OMAP1510_MCBSP2_BASE),
		.dma_rx_sync = OMAP_DMA_MCBSP2_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP2_TX,
		.rx_irq = INT_1510_SPI_RX,
		.tx_irq = INT_1510_SPI_TX },
	[2] = { .virt_base = OMAP1510_MCBSP3_BASE,
		.dma_rx_sync = OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP3_TX,
		.rx_irq = INT_McBSP3RX,
		.tx_irq = INT_McBSP3TX },
};
#endif

#if defined(CONFIG_ARCH_OMAP1610) || defined(CONFIG_ARCH_OMAP1710)
static const struct omap_mcbsp_info mcbsp_1610[] = {
	[0] = { .virt_base = OMAP1610_MCBSP1_BASE,
		.dma_rx_sync = OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP1_TX,
		.rx_irq = INT_McBSP1RX,
		.tx_irq = INT_McBSP1TX },
	[1] = { .virt_base = io_p2v(OMAP1610_MCBSP2_BASE),
		.dma_rx_sync = OMAP_DMA_MCBSP2_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP2_TX,
		.rx_irq = INT_1610_McBSP2_RX,
		.tx_irq = INT_1610_McBSP2_TX },
	[2] = { .virt_base = OMAP1610_MCBSP3_BASE,
		.dma_rx_sync = OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP3_TX,
		.rx_irq = INT_McBSP3RX,
		.tx_irq = INT_McBSP3TX },
};
#endif

static int __init omap_mcbsp_init(void)
{
	int mcbsp_count = 0, i;
	static const struct omap_mcbsp_info *mcbsp_info;

	printk("Initializing OMAP McBSP system\n");
#ifdef CONFIG_ARCH_OMAP730
	if (cpu_is_omap730()) {
		mcbsp_info = mcbsp_730;
		mcbsp_count = ARRAY_SIZE(mcbsp_730);
	}
#endif
#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		mcbsp_info = mcbsp_1510;
		mcbsp_count = ARRAY_SIZE(mcbsp_1510);
	}
#endif
#if defined(CONFIG_ARCH_OMAP1610) || defined(CONFIG_ARCH_OMAP1710)
	if (cpu_is_omap1610() || cpu_is_omap1710()) {
		mcbsp_info = mcbsp_1610;
		mcbsp_count = ARRAY_SIZE(mcbsp_1610);
	}
#endif
	for (i = 0; i < OMAP_MAX_MCBSP_COUNT ; i++) {
		if (i >= mcbsp_count) {
			mcbsp[i].io_base = 0;
			mcbsp[i].free = 0;
                        continue;
		}
		mcbsp[i].id = i + 1;
		mcbsp[i].free = 1;
		mcbsp[i].dma_tx_lch = -1;
		mcbsp[i].dma_rx_lch = -1;

		mcbsp[i].io_base = mcbsp_info[i].virt_base;
		mcbsp[i].tx_irq = mcbsp_info[i].tx_irq;
		mcbsp[i].rx_irq = mcbsp_info[i].rx_irq;
		mcbsp[i].dma_rx_sync = mcbsp_info[i].dma_rx_sync;
		mcbsp[i].dma_tx_sync = mcbsp_info[i].dma_tx_sync;
		spin_lock_init(&mcbsp[i].lock);
	}

	return 0;
}


arch_initcall(omap_mcbsp_init);

EXPORT_SYMBOL(omap_mcbsp_config);
EXPORT_SYMBOL(omap_mcbsp_request);
EXPORT_SYMBOL(omap_mcbsp_free);
EXPORT_SYMBOL(omap_mcbsp_start);
EXPORT_SYMBOL(omap_mcbsp_stop);
EXPORT_SYMBOL(omap_mcbsp_xmit_word);
EXPORT_SYMBOL(omap_mcbsp_recv_word);
EXPORT_SYMBOL(omap_mcbsp_xmit_buffer);
EXPORT_SYMBOL(omap_mcbsp_recv_buffer);
EXPORT_SYMBOL(omap_mcbsp_set_spi_mode);
