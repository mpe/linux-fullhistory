/*****************************************************************************/

/*
 *	sm_sbc.h  -- soundcard radio modem driver soundblaster hardware driver
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

#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/ioport.h>
#include <linux/soundmodem.h>
#include "sm.h"

/* --------------------------------------------------------------------- */

/*
 * currently this module is supposed to support both module styles, i.e.
 * the old one present up to about 2.1.9, and the new one functioning
 * starting with 2.1.21. The reason is I have a kit allowing to compile
 * this module also under 2.0.x which was requested by several people.
 * This will go in 2.2
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE >= 0x20100
#include <asm/uaccess.h>
#else
#include <asm/segment.h>
#include <linux/mm.h>

#undef put_user
#undef get_user

#define put_user(x,ptr) ({ __put_user((unsigned long)(x),(ptr),sizeof(*(ptr))); 0; })
#define get_user(x,ptr) ({ x = ((__typeof__(*(ptr)))__get_user((ptr),sizeof(*(ptr)))); 0; })

extern inline int copy_from_user(void *to, const void *from, unsigned long n)
{
        int i = verify_area(VERIFY_READ, from, n);
        if (i)
                return i;
        memcpy_fromfs(to, from, n);
        return 0;
}

extern inline int copy_to_user(void *to, const void *from, unsigned long n)
{
        int i = verify_area(VERIFY_WRITE, to, n);
        if (i)
                return i;
        memcpy_tofs(to, from, n);
        return 0;
}
#endif

/* --------------------------------------------------------------------- */

struct sc_state_sbc {
	unsigned char revhi, revlo;
	unsigned char fmt[2];
	unsigned int dmabuflen;
	unsigned char *dmabuf;
	unsigned char dmabufidx;
	unsigned char ptt;
};

#define SCSTATE ((struct sc_state_sbc *)(&sm->hw))

/* --------------------------------------------------------------------- */
/* 
 * the sbc converter's registers 
 */
#define DSP_RESET(iobase)        (iobase+0x6)
#define DSP_READ_DATA(iobase)    (iobase+0xa)
#define DSP_WRITE_DATA(iobase)   (iobase+0xc)
#define DSP_WRITE_STATUS(iobase) (iobase+0xc)
#define DSP_DATA_AVAIL(iobase)   (iobase+0xe)
#define DSP_MIXER_ADDR(iobase)   (iobase+0x4)
#define DSP_MIXER_DATA(iobase)   (iobase+0x5)
#define SBC_EXTENT               16

/* --------------------------------------------------------------------- */
/*
 * SBC commands
 */
#define SBC_OUTPUT             0x14
#define SBC_INPUT              0x24
#define SBC_BLOCKSIZE          0x48
#define SBC_HI_OUTPUT          0x91 
#define SBC_HI_INPUT           0x99 
#define SBC_LO_OUTPUT_AUTOINIT 0x1c
#define SBC_LO_INPUT_AUTOINIT  0x2c
#define SBC_HI_OUTPUT_AUTOINIT 0x90 
#define SBC_HI_INPUT_AUTOINIT  0x98
#define SBC_IMMED_INT          0xf2
#define SBC_GET_REVISION       0xe1
#define ESS_GET_REVISION       0xe7
#define SBC_SPEAKER_ON         0xd1
#define SBC_SPEAKER_OFF        0xd3
#define SBC_DMA_ON             0xd0
#define SBC_DMA_OFF            0xd4
#define SBC_SAMPLE_RATE        0x40
#define SBC_MONO_8BIT          0xa0
#define SBC_MONO_16BIT         0xa4
#define SBC_STEREO_8BIT        0xa8
#define SBC_STEREO_16BIT       0xac

#define SBC4_OUT8_AI           0xc6
#define SBC4_IN8_AI            0xce
#define SBC4_MODE_UNS_MONO     0x00

/* --------------------------------------------------------------------- */

static int inline reset_dsp(struct device *dev)
{
	int i;

	outb(1, DSP_RESET(dev->base_addr));
	for (i = 0; i < 0x100; i++)
		SLOW_DOWN_IO;
	outb(0, DSP_RESET(dev->base_addr));
	for (i = 0; i < 0xffff; i++)
		if (inb(DSP_DATA_AVAIL(dev->base_addr)) & 0x80)
			if (inb(DSP_READ_DATA(dev->base_addr)) == 0xaa)
				return 1;
	return 0;
}

/* --------------------------------------------------------------------- */

static void inline write_dsp(struct device *dev, unsigned char data)
{
	int i;
	
	for (i = 0; i < 0xffff; i++)
		if (!(inb(DSP_WRITE_STATUS(dev->base_addr)) & 0x80)) {
			outb(data, DSP_WRITE_DATA(dev->base_addr));
			return;
		}
}

/* --------------------------------------------------------------------- */

static int inline read_dsp(struct device *dev, unsigned char *data)
{
	int i;

	if (!data)
		return 0;
	for (i = 0; i < 0xffff; i++) 
		if (inb(DSP_DATA_AVAIL(dev->base_addr)) & 0x80) {
			*data = inb(DSP_READ_DATA(dev->base_addr));
			return 1;
		}
	return 0;
}

/* --------------------------------------------------------------------- */

static void inline sbc_int_ack(struct device *dev)
{
	inb(DSP_DATA_AVAIL(dev->base_addr));
}

/* --------------------------------------------------------------------- */

static void setup_dma_dsp(struct device *dev, struct sm_state *sm, int send)
{
        unsigned long flags;
        static const unsigned char sbcmode[2][2] = {
		{ SBC_LO_INPUT_AUTOINIT, SBC_LO_OUTPUT_AUTOINIT }, 
		{ SBC_HI_INPUT_AUTOINIT, SBC_HI_OUTPUT_AUTOINIT }
	};
	static const unsigned char sbc4mode[2] = { SBC4_IN8_AI, SBC4_OUT8_AI };
        static const unsigned char dmamode[2] = { 
		DMA_MODE_READ | DMA_MODE_AUTOINIT, DMA_MODE_WRITE  | DMA_MODE_AUTOINIT
	};
	static const unsigned char sbcskr[2] = { SBC_SPEAKER_OFF, SBC_SPEAKER_ON };
	unsigned long dmabufaddr = virt_to_bus(SCSTATE->dmabuf);

	send = !!send;
        if (!reset_dsp(dev)) {
                printk(KERN_ERR "%s: sbc: cannot reset sb dsp\n", sm_drvname);
                return;
        }
        if ((dmabufaddr & 0xffff) + SCSTATE->dmabuflen > 0x10000)
                panic("sm: DMA buffer violates DMA boundary!");
        save_flags(flags);
        cli();
        sbc_int_ack(dev);
        write_dsp(dev, SBC_SAMPLE_RATE); /* set sampling rate */
        write_dsp(dev, SCSTATE->fmt[send]);
        write_dsp(dev, sbcskr[send]); 
        disable_dma(dev->dma);
        clear_dma_ff(dev->dma);
        set_dma_mode(dev->dma, dmamode[send]);
        set_dma_addr(dev->dma, dmabufaddr);
        set_dma_count(dev->dma, SCSTATE->dmabuflen);
        enable_dma(dev->dma);
        sbc_int_ack(dev);
	if (SCSTATE->revhi >= 4) {
		write_dsp(dev, sbc4mode[send]);
		write_dsp(dev, SBC4_MODE_UNS_MONO);
		write_dsp(dev, ((SCSTATE->dmabuflen >> 1) - 1) & 0xff);
		write_dsp(dev, ((SCSTATE->dmabuflen >> 1) - 1) >> 8);
	} else {
		write_dsp(dev, SBC_BLOCKSIZE);
		write_dsp(dev, ((SCSTATE->dmabuflen >> 1) - 1) & 0xff);
		write_dsp(dev, ((SCSTATE->dmabuflen >> 1) - 1) >> 8);
		write_dsp(dev, sbcmode[SCSTATE->fmt[send] >= 180][send]);
		/* hispeed mode if sample rate > 13kHz */
	}
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void sbc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct sm_state *sm = (struct sm_state *)dev->priv;
	unsigned char new_ptt;
	unsigned char *buf;

	if (!dev || !sm || sm->hdrv.magic != HDLCDRV_MAGIC)
		return;
	new_ptt = hdlcdrv_ptt(&sm->hdrv);
 	sbc_int_ack(dev);
 	buf = SCSTATE->dmabuf;
	if (SCSTATE->dmabufidx)
		buf += SCSTATE->dmabuflen/2;
	SCSTATE->dmabufidx = !SCSTATE->dmabufidx;
	sm_int_freq(sm);
	sti();
	if (new_ptt && !SCSTATE->ptt) {
		/* starting to transmit */
		disable_dma(dev->dma);
 		SCSTATE->dmabufidx = 0;
		time_exec(sm->debug_vals.demod_cyc, 
			  sm->mode_rx->demodulator(sm, buf, SCSTATE->dmabuflen/2));
		time_exec(sm->debug_vals.mod_cyc, 
			  sm->mode_tx->modulator(sm, SCSTATE->dmabuf, 
						 SCSTATE->dmabuflen/2));
 		setup_dma_dsp(dev, sm, 1);
		time_exec(sm->debug_vals.mod_cyc, 
			  sm->mode_tx->modulator(sm, SCSTATE->dmabuf + 
						 SCSTATE->dmabuflen/2,
						 SCSTATE->dmabuflen/2));
	} else if (SCSTATE->ptt == 1 && !new_ptt) {
		/* stopping transmission */
		disable_dma(dev->dma);
 		SCSTATE->dmabufidx = 0;
		setup_dma_dsp(dev, sm, 0);
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
	sm_output_status(sm);
	hdlcdrv_transmitter(dev, &sm->hdrv);
	hdlcdrv_receiver(dev, &sm->hdrv);
}

/* --------------------------------------------------------------------- */

static int sbc_open(struct device *dev, struct sm_state *sm) 
{
	if (sizeof(sm->m) < sizeof(struct sc_state_sbc)) {
		printk(KERN_ERR "sm sbc: sbc state too big: %d > %d\n", 
		       sizeof(struct sc_state_sbc), sizeof(sm->m));
		return -ENODEV;
	}
	if (!dev || !sm)
		return -ENXIO;
	if (dev->base_addr <= 0 || dev->base_addr > 0x1000-SBC_EXTENT || 
	    dev->irq < 2 || dev->irq > 15 || dev->dma > 3)
		return -ENXIO;
	if (check_region(dev->base_addr, SBC_EXTENT))
		return -EACCES;
	/*
	 * check if a card is available
	 */
	if (!reset_dsp(dev))
		return -ENODEV;
	write_dsp(dev, SBC_GET_REVISION);
	if (!read_dsp(dev, &SCSTATE->revhi) || 
	    !read_dsp(dev, &SCSTATE->revlo))
		return -ENODEV;
	if (SCSTATE->revhi < 2) {
		printk(KERN_ERR "%s: your card is an antiquity, at least DSP "
		       "rev 2.00 required\n", sm_drvname);
		return -ENODEV;
	}
	if (SCSTATE->revhi < 3 && 
	    (SCSTATE->fmt[0] >= 180 || SCSTATE->fmt[1] >= 180)) {
		printk(KERN_ERR "%s: sbc io 0x%lx: DSP rev %d.%02d too "
		       "old, at least 3.00 required\n", sm_drvname,
		       dev->base_addr, SCSTATE->revhi, SCSTATE->revlo);
		return -ENODEV;
	}
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
	if (request_irq(dev->irq, sbc_interrupt, SA_INTERRUPT, 
			sm->hwdrv->hw_name, dev)) {
		free_dma(dev->dma);
		kfree_s(SCSTATE->dmabuf, SCSTATE->dmabuflen);
		return -EBUSY;
	}
	request_region(dev->base_addr, SBC_EXTENT, sm->hwdrv->hw_name);
	setup_dma_dsp(dev, sm, 0);
	return 0;
}

/* --------------------------------------------------------------------- */

static int sbc_close(struct device *dev, struct sm_state *sm) 
{
	if (!dev || !sm)
		return -EINVAL;
	/*
	 * disable interrupts
	 */
	disable_dma(dev->dma);
	reset_dsp(dev);	
	free_irq(dev->irq, dev);	
	free_dma(dev->dma);	
	release_region(dev->base_addr, SBC_EXTENT);
	kfree_s(SCSTATE->dmabuf, SCSTATE->dmabuflen);
	return 0;
}

/* --------------------------------------------------------------------- */

static int sbc_sethw(struct device *dev, struct sm_state *sm, char *mode)
{
	char *cp = strchr(mode, '.');
	const struct modem_tx_info **mtp = sm_modem_tx_table;
	const struct modem_rx_info **mrp;
	int dv;

	if (!strcmp(mode, "off")) {
		sm->mode_tx = NULL;
		sm->mode_rx = NULL;
		return 0;
	}
	if (cp)
		*cp++ = '\0';
	else
		cp = mode;
	for (; *mtp; mtp++) {
		if ((*mtp)->loc_storage > sizeof(sm->m)) {
			printk(KERN_ERR "%s: insufficient storage for modulator %s (%d)\n",
			       sm_drvname, (*mtp)->name, (*mtp)->loc_storage);
			continue;
		}
		if (!(*mtp)->name || strcmp((*mtp)->name, mode))
			continue;
		if ((*mtp)->srate < 5000 || (*mtp)->srate > 44100)
			continue;
		for (mrp = sm_modem_rx_table; *mrp; mrp++) {
			if ((*mrp)->loc_storage > sizeof(sm->d)) {
				printk(KERN_ERR "%s: insufficient storage for demodulator %s (%d)\n",
				       sm_drvname, (*mrp)->name, (*mrp)->loc_storage);
				continue;
			}
			if ((*mrp)->name && !strcmp((*mrp)->name, cp) &&
			    (*mrp)->srate >= 5000 && (*mrp)->srate <= 44100) {
				sm->mode_tx = *mtp;
				sm->mode_rx = *mrp;
				SCSTATE->fmt[0] = 256-((1000000L+sm->mode_rx->srate/2)/
							 sm->mode_rx->srate);
				SCSTATE->fmt[1] = 256-((1000000L+sm->mode_tx->srate/2)/
							 sm->mode_tx->srate);
				dv = lcm(sm->mode_tx->dmabuflenmodulo, 
					 sm->mode_rx->dmabuflenmodulo);
				SCSTATE->dmabuflen = sm->mode_rx->srate/100+dv-1;
				SCSTATE->dmabuflen /= dv;
				SCSTATE->dmabuflen *= 2*dv; /* make sure DMA buf is even */
				return 0;
			}
		}
	}
	return -EINVAL;
}

/* --------------------------------------------------------------------- */

static int sbc_ioctl(struct device *dev, struct sm_state *sm, struct ifreq *ifr, 
		     struct hdlcdrv_ioctl *hi, int cmd)
{
	struct sm_ioctl bi;
	unsigned long flags;
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
		switch (SCSTATE->revhi) {
		case 2:
			bi.data.mix.mixer_type = SM_MIXER_CT1335;
			break;
		case 3:
			bi.data.mix.mixer_type = SM_MIXER_CT1345;
			break;
		case 4:
			bi.data.mix.mixer_type = SM_MIXER_CT1745;
			break;
		}
		if (bi.data.mix.mixer_type != SM_MIXER_INVALID &&
		    bi.data.mix.reg < 0x80) {
			save_flags(flags);
			cli();
			outb(bi.data.mix.reg, DSP_MIXER_ADDR(dev->base_addr));
			bi.data.mix.data = inb(DSP_MIXER_DATA(dev->base_addr));
			restore_flags(flags);
			i = 1;
		}
		if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
			return -EFAULT;
		return i;
		
	case SMCTL_SETMIXER:
		if (!suser())
			return -EACCES;
		switch (SCSTATE->revhi) {
		case 2:
			if (bi.data.mix.mixer_type != SM_MIXER_CT1335)
				return -EINVAL;
			break;
		case 3:
			if (bi.data.mix.mixer_type != SM_MIXER_CT1345)
				return -EINVAL;
			break;
		case 4:
			if (bi.data.mix.mixer_type != SM_MIXER_CT1745)
				return -EINVAL;
			break;
		default:
			return -ENODEV;
		}
		if (bi.data.mix.reg >= 0x80)
			return -EACCES;
		save_flags(flags);
		cli();
		outb(bi.data.mix.reg, DSP_MIXER_ADDR(dev->base_addr));
		outb(bi.data.mix.data, DSP_MIXER_DATA(dev->base_addr));
		restore_flags(flags);
		return 0;
		
	}
	if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
		return -EFAULT;
	return 0;

}

/* --------------------------------------------------------------------- */

const struct hardware_info sm_hw_sbc = {
	"sbc", sizeof(struct sc_state_sbc), 
	sbc_open, sbc_close, sbc_ioctl, sbc_sethw
};

/* --------------------------------------------------------------------- */
