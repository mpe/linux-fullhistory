/*****************************************************************************/

/*
 *	sm_wss.h  -- soundcard radio modem driver, WSS (half duplex) driver
 *
 *	Copyright (C) 1996  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
 *
 */

/* --------------------------------------------------------------------- */

struct sc_state_wss {
	unsigned char revwss, revid, revv, revcid;
	unsigned char fmt[2];
	unsigned char crystal;
	unsigned int dmabuflen;
	unsigned char *dmabuf;
	unsigned char dmabufidx;
	unsigned char ptt;
	/* Full Duplex extensions */
	unsigned char *dmabuf2;
};

#define SCSTATE ((struct sc_state_wss *)(&sm->hw))

/* --------------------------------------------------------------------- */

#define WSS_CONFIG(iobase)       (iobase+0)
#define WSS_STATUS(iobase)       (iobase+3)
#define WSS_CODEC_IA(iobase)     (iobase+4)
#define WSS_CODEC_ID(iobase)     (iobase+5)
#define WSS_CODEC_STATUS(iobase) (iobase+6)
#define WSS_CODEC_DATA(iobase)   (iobase+7)

#define WSS_EXTENT   8

/* --------------------------------------------------------------------- */

static void write_codec(struct device *dev, unsigned char idx,
			unsigned char data)
{
	int timeout = 900000;

	/* wait until codec ready */
	while (timeout > 0 && inb(WSS_CODEC_IA(dev->base_addr)) & 0x80)
		timeout--;
	outb(idx, WSS_CODEC_IA(dev->base_addr));
	outb(data, WSS_CODEC_ID(dev->base_addr));
}


/* --------------------------------------------------------------------- */

static unsigned char read_codec(struct device *dev, unsigned char idx)
{
	int timeout = 900000;

	/* wait until codec ready */
	while (timeout > 0 && inb(WSS_CODEC_IA(dev->base_addr)) & 0x80)
		timeout--;
	outb(idx & 0x1f, WSS_CODEC_IA(dev->base_addr));
	return inb(WSS_CODEC_ID(dev->base_addr));
}

/* --------------------------------------------------------------------- */

extern void inline wss_ack_int(struct device *dev)
{
	outb(0, WSS_CODEC_STATUS(dev->base_addr));
}

/* --------------------------------------------------------------------- */

static int wss_srate_tab[16] = {
	8000, 5510, 16000, 11025, 27420, 18900, 32000, 22050,
	-1, 37800, -1, 44100, 48000, 33075, 9600, 6620
};

static int wss_srate_index(int srate)
{
	int i;

	for (i = 0; i < (sizeof(wss_srate_tab)/sizeof(wss_srate_tab[0])); i++)
		if (srate == wss_srate_tab[i] && wss_srate_tab[i] > 0)
			return i;
	return -1;
}

/* --------------------------------------------------------------------- */

static int wss_set_codec_fmt(struct device *dev, struct sm_state *sm, 
			     unsigned char fmt, char fdx, char fullcalib)
{
	unsigned long time;
	unsigned long flags;

	save_flags(flags);
	cli();
	/* Clock and data format register */
	write_codec(dev, 0x48, fmt);
	if (SCSTATE->crystal) {
		write_codec(dev, 0x5c, fmt & 0xf0);
		/* MCE and interface config reg */	
		write_codec(dev, 0x49, (fdx ? 0 : 0x4) | (fullcalib ? 0x18 : 0));
	} else 
		/* MCE and interface config reg */
		write_codec(dev, 0x49, fdx ? 0x8 : 0xc);
	outb(0xb, WSS_CODEC_IA(dev->base_addr)); /* leave MCE */
	/*
	 * wait for ACI start
	 */
	time = 1000;
	while (!(read_codec(dev, 0x0b) & 0x20))
		if (!(--time)) {
			printk(KERN_WARNING "%s: ad1848 auto calibration timed out (1)\n", 
			       sm_drvname);
			restore_flags(flags);
			return -1;
		}
	/*
	 * wait for ACI end
	 */
	sti();
	time = jiffies + HZ/4;
	while ((read_codec(dev, 0x0b) & 0x20) && ((signed)(jiffies - time) < 0));
	restore_flags(flags);
	if ((signed)(jiffies - time) >= 0) {
		printk(KERN_WARNING "%s: ad1848 auto calibration timed out (2)\n", 
		       sm_drvname);
		return -1;
	}
	return 0;
}

/* --------------------------------------------------------------------- */

static int wss_init_codec(struct device *dev, struct sm_state *sm, char fdx, 
			  unsigned char src_l, unsigned char src_r, 
			  int igain_l, int igain_r,
			  int ogain_l, int ogain_r)
{
	unsigned char tmp, reg0, reg1, reg6, reg7;
	static const signed char irqtab[16] = 
	{ -1, -1, 0x10, -1, -1, -1, -1, 0x08, -1, 0x10, 0x18, 0x20, -1, -1,
		  -1, -1 };
	static const signed char dmatab[4] = { 1, 2, -1, 3 };
	
	tmp = inb(WSS_STATUS(dev->base_addr));
	if ((tmp & 0x3f) != 0x04 && (tmp & 0x3f) != 0x00 && 
	    (tmp & 0x3f) != 0x0f) {
		printk(KERN_WARNING "sm: WSS card id register not found, "
		       "address 0x%lx, ID register 0x%02x\n", 
		       dev->base_addr, (int)tmp);
		/* return -1; */
		SCSTATE->revwss = 0;
	} else {
		if ((tmp & 0x80) && ((dev->dma == 0) || 
				     ((dev->irq >= 8) && (dev->irq != 9)))) {
			printk(KERN_ERR "%s: WSS: DMA0 and/or IRQ8..IRQ15 "
			       "(except IRQ9) cannot be used on an 8bit "
			       "card\n", sm_drvname);
			return -1;
		}		
		if (dev->irq > 15 || irqtab[dev->irq] == -1) {
			printk(KERN_ERR "%s: WSS: invalid interrupt %d\n", 
			       sm_drvname, (int)dev->irq);
			return -1;
		}
		if (dev->dma > 3 || dmatab[dev->dma] == -1) {
			printk(KERN_ERR "%s: WSS: invalid dma channel %d\n", 
			       sm_drvname, (int)dev->dma);
			return -1;
		}
		tmp = irqtab[dev->irq] | dmatab[dev->dma];
		/* irq probe */
		outb((tmp & 0x38) | 0x40, WSS_CONFIG(dev->base_addr));
		if (!(inb(WSS_STATUS(dev->base_addr)) & 0x40)) {
			outb(0, WSS_CONFIG(dev->base_addr));
			printk(KERN_ERR "%s: WSS: IRQ%d is not free!\n", 
			       sm_drvname, dev->irq);
		}
		outb(tmp, WSS_CONFIG(dev->base_addr));
		SCSTATE->revwss = inb(WSS_STATUS(dev->base_addr)) & 0x3f;
	}
	/*
	 * initialize the codec
	 */
	if (igain_l < 0)
		igain_l = 0;
	if (igain_r < 0)
		igain_r = 0;
	if (ogain_l > 0)
		ogain_l = 0;
	if (ogain_r > 0)
		ogain_r = 0;
	reg0 = (src_l << 6) & 0xc0;
	reg1 = (src_r << 6) & 0xc0;
	if (reg0 == 0x80 && igain_l >= 20) {
		reg0 |= 0x20;
		igain_l -= 20;
	}
	if (reg1 == 0x80 && igain_r >= 20) {
		reg1 |= 0x20;
		igain_r -= 20;
	}
	if (igain_l > 23)
		igain_l = 23;
	if (igain_r > 23)
		igain_r = 23;
	reg0 |= igain_l * 2 / 3;
	reg1 |= igain_r * 2 / 3;
	reg6 = (ogain_l < -95) ? 0x80 : (ogain_l * (-2) / 3);
	reg7 = (ogain_r < -95) ? 0x80 : (ogain_r * (-2) / 3);
	write_codec(dev, 9, 0);
	write_codec(dev, 0, 0x45);
	if (read_codec(dev, 0) != 0x45)
		goto codec_err;
	write_codec(dev, 0, 0xaa);
	if (read_codec(dev, 0) != 0xaa)
		goto codec_err;
	write_codec(dev, 12, 0x40); /* enable MODE2 */
	write_codec(dev, 16, 0);
	write_codec(dev, 0, 0x45);
	SCSTATE->crystal = (read_codec(dev, 16) != 0x45);
	write_codec(dev, 0, 0xaa);
	SCSTATE->crystal &= (read_codec(dev, 16) != 0xaa);
	if (SCSTATE->crystal) {
		SCSTATE->revcid = read_codec(dev, 0x19);
		SCSTATE->revv = (SCSTATE->revcid >> 5) & 7;
		SCSTATE->revcid &= 7;
		write_codec(dev, 0x10, 0x80); /* maximum output level */
		write_codec(dev, 0x11, 0x02); /* xtal enable and no HPF */
		write_codec(dev, 0x12, 0x80); /* left line input control */
		write_codec(dev, 0x13, 0x80); /* right line input control */
		write_codec(dev, 0x16, 0); /* disable alternative freq sel */
		write_codec(dev, 0x1a, 0xe0); /* mono IO disable */
		write_codec(dev, 0x1b, 0x00); /* left out no att */
		write_codec(dev, 0x1d, 0x00); /* right out no att */
	}

	if (wss_set_codec_fmt(dev, sm, SCSTATE->fmt[0], fdx, 1))
		goto codec_err;

        write_codec(dev, 0, reg0); /* left input control */
        write_codec(dev, 1, reg1); /* right input control */
        write_codec(dev, 2, 0x80); /* left aux#1 input control */
        write_codec(dev, 3, 0x80); /* right aux#1 input control */
        write_codec(dev, 4, 0x80); /* left aux#2 input control */
        write_codec(dev, 5, 0x80); /* right aux#2 input control */
        write_codec(dev, 6, reg6); /* left dac control */
        write_codec(dev, 7, reg7); /* right dac control */
        write_codec(dev, 0xa, 0x2); /* pin control register */
        write_codec(dev, 0xd, 0x0); /* digital mix control */
	SCSTATE->revid = read_codec(dev, 0xc) & 0xf;
	/*
	 * print revisions
	 */
	if (SCSTATE->crystal) 
		printk(KERN_INFO "%s: Crystal CODEC ID %d, Chip revision %d, "
		       " Chip ID %d\n", sm_drvname, (int)SCSTATE->revid,
		       (int)SCSTATE->revv, (int)SCSTATE->revcid);
	else
		printk(KERN_INFO "%s: WSS revision %d, CODEC revision %d\n", 
		       sm_drvname, (int)SCSTATE->revwss, 
		       (int)SCSTATE->revid);
	return 0;
 codec_err:
	outb(0, WSS_CONFIG(dev->base_addr));
	printk(KERN_ERR "%s: no WSS soundcard found at address 0x%lx\n", 
	       sm_drvname, dev->base_addr);
	return -1;
}

/* --------------------------------------------------------------------- */

static void setup_dma_wss(struct device *dev, struct sm_state *sm, int send)
{
        unsigned long flags;
        static const unsigned char codecmode[2] = { 0x0e, 0x0d };
        static const unsigned char dmamode[2] = { 
		DMA_MODE_READ | DMA_MODE_AUTOINIT, 
		DMA_MODE_WRITE  | DMA_MODE_AUTOINIT
	};
	unsigned char oldcodecmode;
	long abrt;
	unsigned long dmabufaddr = virt_to_bus(SCSTATE->dmabuf);

        if ((dmabufaddr & 0xffffu) + SCSTATE->dmabuflen > 0x10000)
                panic("%s: DMA buffer violates DMA boundary!", sm_drvname);
	send = !!send;
        save_flags(flags);
        cli();
	/*
	 * perform the final DMA sequence to disable the codec request
	 */
	oldcodecmode = read_codec(dev, 9);
        write_codec(dev, 9, 0xc); /* disable codec */
	wss_ack_int(dev);
	if (read_codec(dev, 11) & 0x10) {
		disable_dma(dev->dma);
		clear_dma_ff(dev->dma);
		set_dma_mode(dev->dma, dmamode[oldcodecmode & 1]);
		set_dma_addr(dev->dma, dmabufaddr);
		set_dma_count(dev->dma, SCSTATE->dmabuflen);
		enable_dma(dev->dma);
		abrt = 0;
		while ((read_codec(dev, 11) & 0x10) || ((++abrt) >= 0x10000));
	}
        disable_dma(dev->dma);
	if (read_codec(dev, 0x8) != SCSTATE->fmt[send])
		wss_set_codec_fmt(dev, sm, SCSTATE->fmt[send], 0, 0);
        clear_dma_ff(dev->dma);
        set_dma_mode(dev->dma, dmamode[send]);
        set_dma_addr(dev->dma, dmabufaddr);
        set_dma_count(dev->dma, SCSTATE->dmabuflen);
        enable_dma(dev->dma);
	write_codec(dev, 15, ((SCSTATE->dmabuflen >> 1) - 1) & 0xff);
	write_codec(dev, 14, ((SCSTATE->dmabuflen >> 1) - 1) >> 8);
	if (SCSTATE->crystal) {
		write_codec(dev, 31, ((SCSTATE->dmabuflen >> 1) - 1) & 0xff);
		write_codec(dev, 30, ((SCSTATE->dmabuflen >> 1) - 1) >> 8);
	}
	write_codec(dev, 9, codecmode[send]);
        restore_flags(flags);
	printk("sm: wss: %cx fmt: 0x%02x ifc: 0x%02x\n", "rt"[send],
	       read_codec(dev, 0x8), read_codec(dev, 0x9));
}

/* --------------------------------------------------------------------- */

static void wss_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct sm_state *sm = (struct sm_state *)dev->priv;
	unsigned char new_ptt;
	unsigned char *buf;
	int dmares;

	if (!dev || !sm || !sm->mode_rx || !sm->mode_tx || 
	    sm->hdrv.magic != HDLCDRV_MAGIC)
		return;
	new_ptt = hdlcdrv_ptt(&sm->hdrv);
	cli();
	wss_ack_int(dev);
	disable_dma(dev->dma);
	clear_dma_ff(dev->dma);
	dmares = get_dma_residue(dev->dma);
	if (dmares <= 0)
		dmares = SCSTATE->dmabuflen;
	buf = SCSTATE->dmabuf;
	if (dmares > SCSTATE->dmabuflen/2) {
		buf += SCSTATE->dmabuflen/2;
		dmares -= SCSTATE->dmabuflen/2;
	}
#ifdef SM_DEBUG
	if (!sm->debug_vals.dma_residue || 
	    dmares < sm->debug_vals.dma_residue)
		sm->debug_vals.dma_residue = dmares;
#endif /* SM_DEBUG */
	dmares--;
	write_codec(dev, 15, dmares & 0xff);
	write_codec(dev, 14, dmares >> 8);
	if (SCSTATE->crystal) {
		write_codec(dev, 31, dmares & 0xff);
		write_codec(dev, 30, dmares >> 8);
	}
	enable_dma(dev->dma);
	sm_int_freq(sm);
	sti();
	if (new_ptt && !SCSTATE->ptt) {
		/* starting to transmit */
		disable_dma(dev->dma);
		sti();
 		SCSTATE->dmabufidx = 0;
		time_exec(sm->debug_vals.demod_cyc, 
			  sm->mode_rx->demodulator(sm, buf, SCSTATE->dmabuflen/2));
		time_exec(sm->debug_vals.mod_cyc, 
			  sm->mode_tx->modulator(sm, SCSTATE->dmabuf, 
						 SCSTATE->dmabuflen/2));
 		setup_dma_wss(dev, sm, 1);
		time_exec(sm->debug_vals.mod_cyc, 
			  sm->mode_tx->modulator(sm, SCSTATE->dmabuf + 
						 SCSTATE->dmabuflen/2,
						 SCSTATE->dmabuflen/2));
	} else if (SCSTATE->ptt == 1 && !new_ptt) {
		/* stopping transmission */
		disable_dma(dev->dma);
		sti();
 		SCSTATE->dmabufidx = 0;
		setup_dma_wss(dev, sm, 0);
		SCSTATE->ptt = 0;
	} else if (SCSTATE->ptt) {
                SCSTATE->ptt--;
		time_exec(sm->debug_vals.mod_cyc, 
			  sm->mode_tx->modulator(sm, buf, SCSTATE->dmabuflen/2));
        } else {
		time_exec(sm->debug_vals.demod_cyc, 
			  sm->mode_rx->demodulator(sm, buf, SCSTATE->dmabuflen/2));
		hdlcdrv_arbitrate(dev, &sm->hdrv);
        }
        if (new_ptt)
                SCSTATE->ptt = 2;
	output_status(sm);
	hdlcdrv_transmitter(dev, &sm->hdrv);
	hdlcdrv_receiver(dev, &sm->hdrv);
}

/* --------------------------------------------------------------------- */

static int wss_open(struct device *dev, struct sm_state *sm) 
{
	if (sizeof(sm->m) < sizeof(struct sc_state_wss)) {
		printk(KERN_ERR "sm wss: wss state too big: %d > %d\n", 
		       sizeof(struct sc_state_wss), sizeof(sm->m));
		return -ENODEV;
	}
	if (!dev || !sm || !sm->mode_rx || !sm->mode_tx)
		return -ENXIO;
	if (dev->base_addr <= 0 || dev->base_addr > 0x1000-WSS_EXTENT || 
	    dev->irq < 2 || dev->irq > 15 || dev->dma > 3)
		return -ENXIO;
	if (check_region(dev->base_addr, WSS_EXTENT))
		return -EACCES;
	/*
	 * check if a card is available
	 */
	if (wss_init_codec(dev, sm, 0, 1, 1, 0, 0, -45, -45))
		return -ENODEV;
	/*
	 * initialize some variables
	 */
	if (!(SCSTATE->dmabuf = kmalloc(SCSTATE->dmabuflen, GFP_KERNEL | GFP_DMA)))
		return -ENOMEM;
	SCSTATE->dmabufidx = SCSTATE->ptt = 0;

	memset(&sm->m, 0, sizeof(sm->m));
	memset(&sm->d, 0, sizeof(sm->d));
	if (sm->mode_tx->init)
		sm->mode_tx->init(sm);
	if (sm->mode_rx->init)
		sm->mode_rx->init(sm);

	if (request_dma(dev->dma, sm->hwdrv->hw_name)) {
		kfree_s(SCSTATE->dmabuf, SCSTATE->dmabuflen);
		return -EBUSY;
	}
	if (request_irq(dev->irq, wss_interrupt, SA_INTERRUPT, 
			sm->hwdrv->hw_name, dev)) {
		free_dma(dev->dma);
		kfree_s(SCSTATE->dmabuf, SCSTATE->dmabuflen);
		return -EBUSY;
	}
	request_region(dev->base_addr, WSS_EXTENT, sm->hwdrv->hw_name);
	setup_dma_wss(dev, sm, 0);
	return 0;
}

/* --------------------------------------------------------------------- */

static int wss_close(struct device *dev, struct sm_state *sm) 
{
	if (!dev || !sm)
		return -EINVAL;
	/*
	 * disable interrupts
	 */
	disable_dma(dev->dma);
        write_codec(dev, 9, 0xc); /* disable codec */
	free_irq(dev->irq, dev);	
	free_dma(dev->dma);	
	release_region(dev->base_addr, WSS_EXTENT);
	kfree_s(SCSTATE->dmabuf, SCSTATE->dmabuflen);
	return 0;
}

/* --------------------------------------------------------------------- */

static int wss_sethw(struct device *dev, struct sm_state *sm, char *mode)
{
	char *cp = strchr(mode, '.');
	const struct modem_tx_info *mtp = modem_tx_base;
	const struct modem_rx_info *mrp;
	int i, j, dv;

	if (!strcmp(mode, "off")) {
		sm->mode_tx = NULL;
		sm->mode_rx = NULL;
		return 0;
	}
	if (cp)
		*cp++ = '\0';
	else
		cp = mode;
	for (; mtp; mtp = mtp->next) {
		if (mtp->loc_storage > sizeof(sm->m)) {
			printk(KERN_ERR "%s: insufficient storage for modulator %s (%d)\n",
			       sm_drvname, mtp->name, mtp->loc_storage);
			continue;
		}
		if (!mtp->name || strcmp(mtp->name, mode))
			continue;
		if ((i = wss_srate_index(mtp->srate)) < 0) 
			continue;
		for (mrp = modem_rx_base; mrp; mrp = mrp->next) {
			if (mrp->loc_storage > sizeof(sm->d)) {
				printk(KERN_ERR "%s: insufficient storage for demodulator %s (%d)\n",
				       sm_drvname, mrp->name, mrp->loc_storage);
				continue;
			}
			if (mrp->name && !strcmp(mrp->name, cp) &&
			    ((j = wss_srate_index(mrp->srate)) >= 0)) {
				sm->mode_tx = mtp;
				sm->mode_rx = mrp;
				SCSTATE->fmt[0] = j;
				SCSTATE->fmt[1] = i;
				dv = lcm(sm->mode_tx->dmabuflenmodulo, 
					 sm->mode_rx->dmabuflenmodulo);
				SCSTATE->dmabuflen = sm->mode_rx->srate/100+dv-1;
				SCSTATE->dmabuflen /= dv;
				SCSTATE->dmabuflen *= 2*dv; /* make sure DMA buf is even */
	    printk(KERN_DEBUG "sm wss: modtx %u modrx %u srt %u buflen %u\n",
		   sm->mode_tx->dmabuflenmodulo, sm->mode_rx->dmabuflenmodulo,
		   sm->mode_rx->srate, SCSTATE->dmabuflen);
				return 0;
			}
		}
	}
	return -EINVAL;
}

/* --------------------------------------------------------------------- */

static int wss_ioctl(struct device *dev, struct sm_state *sm, struct ifreq *ifr, 
		     struct hdlcdrv_ioctl *hi, int cmd)
{
	struct sm_ioctl bi;
	int i;
	
	if (cmd != SIOCDEVPRIVATE)
		return -ENOIOCTLCMD;

	if (hi->cmd == HDLCDRVCTL_MODEMPARMASK)
		return HDLCDRV_PARMASK_IOBASE | HDLCDRV_PARMASK_IRQ | 
			HDLCDRV_PARMASK_DMA | HDLCDRV_PARMASK_SERIOBASE | 
			HDLCDRV_PARMASK_PARIOBASE | HDLCDRV_PARMASK_MIDIIOBASE;

	if (copy_from_user(&bi, ifr->ifr_data, sizeof(bi)))
		return -EFAULT;

	switch (bi.cmd) {
	default:
		return -ENOIOCTLCMD;

	case SMCTL_GETMIXER:
		i = 0;
		bi.data.mix.sample_rate = sm->mode_rx->srate;
		bi.data.mix.bit_rate = sm->hdrv.par.bitrate;
		bi.data.mix.mixer_type = SCSTATE->crystal ? 
			SM_MIXER_CRYSTAL : SM_MIXER_AD1848;
		if (((SCSTATE->crystal ? 0x2c0c20fflu: 0x20fflu) 
		     >> bi.data.mix.reg) & 1) {
			bi.data.mix.data = read_codec(dev, bi.data.mix.reg);
			i = 1;
		}
		if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
			return -EFAULT;
		return i;

	case SMCTL_SETMIXER:
		if (!suser())
			return -EACCES;
		if ((bi.data.mix.mixer_type != SM_MIXER_CRYSTAL || 
		     !SCSTATE->crystal) &&
		    (bi.data.mix.mixer_type != SM_MIXER_AD1848 ||
		     bi.data.mix.reg >= 0x10))
			return -EINVAL;
		if (!((0x2c0c20fflu >> bi.data.mix.reg) & 1))
			return -EACCES;
		write_codec(dev, bi.data.mix.reg, bi.data.mix.data);
		return 0;
		
	}
	if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
		return -EFAULT;
	return 0;

}

/* --------------------------------------------------------------------- */

struct hardware_info hw_wss = {
	NEXT_HW_INFO, "wss", sizeof(struct sc_state_wss), 
	wss_open, wss_close, wss_ioctl, wss_sethw
};
#undef NEXT_HW_INFO
#define NEXT_HW_INFO (&hw_wss)

/* --------------------------------------------------------------------- */

static void setup_fdx_dma_wss(struct device *dev, struct sm_state *sm)
{
        unsigned long flags;
	unsigned char oldcodecmode, codecdma;
	long abrt;
	unsigned long dmabufaddr1 = virt_to_bus(SCSTATE->dmabuf);
	unsigned long dmabufaddr2 = virt_to_bus(SCSTATE->dmabuf2);

        if (((dmabufaddr1 & 0xffffu) + SCSTATE->dmabuflen > 0x10000) ||
	    ((dmabufaddr2 & 0xffffu) + SCSTATE->dmabuflen > 0x10000))
                panic("%s: DMA buffer violates DMA boundary!", sm_drvname);
        save_flags(flags);
        cli();
	/*
	 * perform the final DMA sequence to disable the codec request
	 */
	oldcodecmode = read_codec(dev, 9);
        write_codec(dev, 9, 0); /* disable codec DMA */
	wss_ack_int(dev);
	if ((codecdma = read_codec(dev, 11)) & 0x10) {
		disable_dma(dev->dma);
		disable_dma(sm->hdrv.ptt_out.dma2);
		clear_dma_ff(dev->dma);
		set_dma_mode(dev->dma, DMA_MODE_WRITE  | DMA_MODE_AUTOINIT);
		set_dma_addr(dev->dma, dmabufaddr1);
		set_dma_count(dev->dma, SCSTATE->dmabuflen);
		clear_dma_ff(sm->hdrv.ptt_out.dma2);
		set_dma_mode(sm->hdrv.ptt_out.dma2, DMA_MODE_READ  | DMA_MODE_AUTOINIT);
		set_dma_addr(sm->hdrv.ptt_out.dma2, dmabufaddr2);
		set_dma_count(sm->hdrv.ptt_out.dma2, SCSTATE->dmabuflen);
		enable_dma(dev->dma);
		enable_dma(sm->hdrv.ptt_out.dma2);
		abrt = 0;
		while (((codecdma = read_codec(dev, 11)) & 0x10) ||
		       ((++abrt) >= 0x10000));
	}
        disable_dma(dev->dma);
        disable_dma(sm->hdrv.ptt_out.dma2);
	clear_dma_ff(dev->dma);
	set_dma_mode(dev->dma, DMA_MODE_WRITE  | DMA_MODE_AUTOINIT);
	set_dma_addr(dev->dma, dmabufaddr1);
	set_dma_count(dev->dma, SCSTATE->dmabuflen);
	clear_dma_ff(sm->hdrv.ptt_out.dma2);
	set_dma_mode(sm->hdrv.ptt_out.dma2, DMA_MODE_READ  | DMA_MODE_AUTOINIT);
	set_dma_addr(sm->hdrv.ptt_out.dma2, dmabufaddr2);
	set_dma_count(sm->hdrv.ptt_out.dma2, SCSTATE->dmabuflen);
        enable_dma(dev->dma);
        enable_dma(sm->hdrv.ptt_out.dma2);
	write_codec(dev, 15, ((SCSTATE->dmabuflen >> 1) - 1) & 0xff);
	write_codec(dev, 14, ((SCSTATE->dmabuflen >> 1) - 1) >> 8);
	if (SCSTATE->crystal) {
		write_codec(dev, 31, ((SCSTATE->dmabuflen >> 1) - 1) & 0xff);
		write_codec(dev, 30, ((SCSTATE->dmabuflen >> 1) - 1) >> 8);
	}
	write_codec(dev, 9, 3);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void wssfdx_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct sm_state *sm = (struct sm_state *)dev->priv;
	unsigned char *buf1;
	unsigned char *buf2;
	unsigned long flags;
	int dmares1, dmares2;

	if (!dev || !sm || !sm->mode_rx || !sm->mode_tx || 
	    sm->hdrv.magic != HDLCDRV_MAGIC)
		return;
	save_flags(flags);
	cli();
	if (SCSTATE->crystal && (!(read_codec(dev, 0x18) & 0x20))) {
		/* only regard Crystal Playback interrupts! */
		wss_ack_int(dev);
		return;
	}
	wss_ack_int(dev);
	disable_dma(dev->dma);
	disable_dma(sm->hdrv.ptt_out.dma2);
	clear_dma_ff(dev->dma);
	dmares1 = get_dma_residue(dev->dma);
	clear_dma_ff(sm->hdrv.ptt_out.dma2);
	dmares2 = get_dma_residue(sm->hdrv.ptt_out.dma2);
	if (dmares1 <= 0)
		dmares1 = SCSTATE->dmabuflen;
	buf1 = SCSTATE->dmabuf;
	if (dmares1 > SCSTATE->dmabuflen/2) {
		buf1 += SCSTATE->dmabuflen/2;
		dmares1 -= SCSTATE->dmabuflen/2;
	}
	if (dmares2 <= 0)
		dmares2 = SCSTATE->dmabuflen;
	buf2 = SCSTATE->dmabuf2;
	if (dmares2 > SCSTATE->dmabuflen/2) {
		buf2 += SCSTATE->dmabuflen/2;
		dmares2 -= SCSTATE->dmabuflen/2;
	}
#ifdef SM_DEBUG
	if (!sm->debug_vals.dma_residue || 
	    dmares1 < sm->debug_vals.dma_residue)
		sm->debug_vals.dma_residue = dmares1;
#endif /* SM_DEBUG */
	dmares1--;
	dmares2--;
	write_codec(dev, 15, dmares1 & 0xff);
	write_codec(dev, 14, dmares1 >> 8);
	if (SCSTATE->crystal) {
		write_codec(dev, 31, dmares2 & 0xff);
		write_codec(dev, 30, dmares2 >> 8);
	}
	enable_dma(dev->dma);
	enable_dma(sm->hdrv.ptt_out.dma2);
	restore_flags(flags);
	sm_int_freq(sm);
	sti();
	if ((SCSTATE->ptt = hdlcdrv_ptt(&sm->hdrv))) 
		time_exec(sm->debug_vals.mod_cyc, 
			  sm->mode_tx->modulator(sm, buf1, SCSTATE->dmabuflen/2));
	else
		time_exec(sm->debug_vals.mod_cyc, 
			  memset(buf1, 0x80, SCSTATE->dmabuflen/2));
	time_exec(sm->debug_vals.demod_cyc, 
		  sm->mode_rx->demodulator(sm, buf2, SCSTATE->dmabuflen/2));
	hdlcdrv_arbitrate(dev, &sm->hdrv);
	output_status(sm);
	hdlcdrv_transmitter(dev, &sm->hdrv);
	hdlcdrv_receiver(dev, &sm->hdrv);
}

/* --------------------------------------------------------------------- */

static int wssfdx_open(struct device *dev, struct sm_state *sm) 
{
	if (!dev || !sm || !sm->mode_rx || !sm->mode_tx)
		return -ENXIO;
	if (dev->base_addr <= 0 || dev->base_addr > 0x1000-WSS_EXTENT || 
	    dev->irq < 2 || dev->irq > 15 || dev->dma > 3)
		return -ENXIO;
	if (check_region(dev->base_addr, WSS_EXTENT))
		return -EACCES;
	/*
	 * check if a card is available
	 */
	if (wss_init_codec(dev, sm, 1, 1, 1, 0, 0, -45, -45))
		return -ENODEV;
	/*
	 * initialize some variables
	 */
	if (!(SCSTATE->dmabuf = kmalloc(SCSTATE->dmabuflen, GFP_KERNEL | GFP_DMA)))
		return -ENOMEM;
	if (!(SCSTATE->dmabuf2 = kmalloc(SCSTATE->dmabuflen, GFP_KERNEL | GFP_DMA))) {
		kfree_s(SCSTATE->dmabuf, SCSTATE->dmabuflen);
		return -ENOMEM;
	}
	SCSTATE->dmabufidx = SCSTATE->ptt = 0;

	memset(&sm->m, 0, sizeof(sm->m));
	memset(&sm->d, 0, sizeof(sm->d));
	if (sm->mode_tx->init)
		sm->mode_tx->init(sm);
	if (sm->mode_rx->init)
		sm->mode_rx->init(sm);

	if (request_dma(dev->dma, sm->hwdrv->hw_name)) {
		kfree_s(SCSTATE->dmabuf, SCSTATE->dmabuflen);
		kfree_s(SCSTATE->dmabuf2, SCSTATE->dmabuflen);
		return -EBUSY;
	}
	if (request_dma(sm->hdrv.ptt_out.dma2, sm->hwdrv->hw_name)) {
		kfree_s(SCSTATE->dmabuf, SCSTATE->dmabuflen);
		kfree_s(SCSTATE->dmabuf2, SCSTATE->dmabuflen);
		free_dma(dev->dma);
		return -EBUSY;
	}
	if (request_irq(dev->irq, wssfdx_interrupt, SA_INTERRUPT, 
			sm->hwdrv->hw_name, dev)) {
		kfree_s(SCSTATE->dmabuf, SCSTATE->dmabuflen);
		kfree_s(SCSTATE->dmabuf2, SCSTATE->dmabuflen);
		free_dma(dev->dma);
		free_dma(sm->hdrv.ptt_out.dma2);
		return -EBUSY;
	}
	request_region(dev->base_addr, WSS_EXTENT, sm->hwdrv->hw_name);
	setup_fdx_dma_wss(dev, sm);
	return 0;
}

/* --------------------------------------------------------------------- */

static int wssfdx_close(struct device *dev, struct sm_state *sm) 
{
	if (!dev || !sm)
		return -EINVAL;
	/*
	 * disable interrupts
	 */
	disable_dma(dev->dma);
        write_codec(dev, 9, 0xc); /* disable codec */
	free_irq(dev->irq, dev);	
	free_dma(dev->dma);	
	free_dma(sm->hdrv.ptt_out.dma2);	
	release_region(dev->base_addr, WSS_EXTENT);
	kfree_s(SCSTATE->dmabuf, SCSTATE->dmabuflen);
	kfree_s(SCSTATE->dmabuf2, SCSTATE->dmabuflen);
	return 0;
}

/* --------------------------------------------------------------------- */

static int wssfdx_sethw(struct device *dev, struct sm_state *sm, char *mode)
{
	char *cp = strchr(mode, '.');
	const struct modem_tx_info *mtp = modem_tx_base;
	const struct modem_rx_info *mrp;
	int i, dv;

	if (!strcmp(mode, "off")) {
		sm->mode_tx = NULL;
		sm->mode_rx = NULL;
		return 0;
	}
	if (cp)
		*cp++ = '\0';
	else
		cp = mode;
	for (; mtp; mtp = mtp->next) {
		if (mtp->loc_storage > sizeof(sm->m)) {
			printk(KERN_ERR "%s: insufficient storage for modulator %s (%d)\n",
			       sm_drvname, mtp->name, mtp->loc_storage);
			continue;
		}
		if (!mtp->name || strcmp(mtp->name, mode))
			continue;
		if ((i = wss_srate_index(mtp->srate)) < 0) 
			continue;
		for (mrp = modem_rx_base; mrp; mrp = mrp->next) {
			if (mrp->loc_storage > sizeof(sm->d)) {
				printk(KERN_ERR "%s: insufficient storage for demodulator %s (%d)\n",
				       sm_drvname, mrp->name, mrp->loc_storage);
				continue;
			}
			if (mrp->name && !strcmp(mrp->name, cp) &&
			    mtp->srate == mrp->srate) {
				sm->mode_tx = mtp;
				sm->mode_rx = mrp;
				SCSTATE->fmt[0] = SCSTATE->fmt[1] = i;
				dv = lcm(sm->mode_tx->dmabuflenmodulo, 
					 sm->mode_rx->dmabuflenmodulo);
				SCSTATE->dmabuflen = sm->mode_rx->srate/100+dv-1;
				SCSTATE->dmabuflen /= dv;
				SCSTATE->dmabuflen *= 2*dv; /* make sure DMA buf is even */
	    printk(KERN_DEBUG "sm wssfdx: modtx %u modrx %u srt %u buflen %u\n",
		   sm->mode_tx->dmabuflenmodulo, sm->mode_rx->dmabuflenmodulo,
		   sm->mode_rx->srate, SCSTATE->dmabuflen);
				return 0;
			}
		}
	}
	return -EINVAL;
}

/* --------------------------------------------------------------------- */

static int wssfdx_ioctl(struct device *dev, struct sm_state *sm, struct ifreq *ifr, 
			struct hdlcdrv_ioctl *hi, int cmd)
{
	if (cmd != SIOCDEVPRIVATE)
		return -ENOIOCTLCMD;

	if (hi->cmd == HDLCDRVCTL_MODEMPARMASK)
		return HDLCDRV_PARMASK_IOBASE | HDLCDRV_PARMASK_IRQ |
			HDLCDRV_PARMASK_DMA | HDLCDRV_PARMASK_DMA2 |
			HDLCDRV_PARMASK_SERIOBASE | HDLCDRV_PARMASK_PARIOBASE |
			HDLCDRV_PARMASK_MIDIIOBASE;

	return wss_ioctl(dev, sm, ifr, hi, cmd);
}

/* --------------------------------------------------------------------- */

struct hardware_info hw_wssfdx = {
	NEXT_HW_INFO, "wssfdx", sizeof(struct sc_state_wss), 
	wssfdx_open, wssfdx_close, wssfdx_ioctl, wssfdx_sethw
};
#undef NEXT_HW_INFO
#define NEXT_HW_INFO (&hw_wssfdx)

/* --------------------------------------------------------------------- */

#undef SCSTATE
