/*****************************************************************************/

/*
 *      sbc.c  --  Linux soundcard HF FSK driver, 
 *                 Soundblaster specific functions.
 *
 *      Copyright (C) 1997  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *        Swiss Federal Institute of Technology (ETH), Electronics Lab
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */

/*****************************************************************************/
     
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/hfmodem.h>

#include <asm/io.h>
#include <asm/dma.h>

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
#define DSP_INTACK_16BIT(iobase) (iobase+0xf)
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
#define ESS_EXTENDED_MODE      0xc6
#define SBC_SPEAKER_ON         0xd1
#define SBC_SPEAKER_OFF        0xd3
#define SBC_DMA_ON             0xd0
#define SBC_DMA_OFF            0xd4
#define SBC_SAMPLE_RATE        0x40
#define SBC_SAMPLE_RATE_OUT    0x41
#define SBC_SAMPLE_RATE_IN     0x42
#define SBC_MONO_8BIT          0xa0
#define SBC_MONO_16BIT         0xa4
#define SBC_STEREO_8BIT        0xa8
#define SBC_STEREO_16BIT       0xac

#define SBC4_OUT8_AI           0xc6
#define SBC4_IN8_AI            0xce
#define SBC4_MODE_UNS_MONO     0x00
#define SBC4_MODE_SIGN_MONO    0x10

#define SBC4_OUT16_AI          0xb6
#define SBC4_IN16_AI           0xbe
#define SBC4_OUT16_AI_NO_FIFO  0xb4
#define SBC4_IN16_AI_NO_FIFO   0xbc

/* --------------------------------------------------------------------- */

extern const struct hfmodem_scops sbc4_scops;
extern const struct hfmodem_scops ess_scops;

/* --------------------------------------------------------------------- */

static int reset_dsp(struct hfmodem_state *dev)
{
        int i;

        outb(1, DSP_RESET(dev->io.base_addr));
	udelay(3);
        outb(0, DSP_RESET(dev->io.base_addr));
        for (i = 0; i < 0xffff; i++)
                if (inb(DSP_DATA_AVAIL(dev->io.base_addr)) & 0x80)
                        if (inb(DSP_READ_DATA(dev->io.base_addr)) == 0xaa)
                                return 1;
        return 0;
}

/* --------------------------------------------------------------------- */

static void write_dsp(struct hfmodem_state *dev, unsigned char data)
{
        int i;
        
        for (i = 0; i < 0xffff; i++)
                if (!(inb(DSP_WRITE_STATUS(dev->io.base_addr)) & 0x80)) {
                        outb(data, DSP_WRITE_DATA(dev->io.base_addr));
                        return;
                }
}

/* --------------------------------------------------------------------- */

static int read_dsp(struct hfmodem_state *dev, unsigned char *data)
{
        int i;

        if (!data)
                return 0;
        for (i = 0; i < 0xffff; i++) 
                if (inb(DSP_DATA_AVAIL(dev->io.base_addr)) & 0x80) {
                        *data = inb(DSP_READ_DATA(dev->io.base_addr));
                        return 1;
                }
        return 0;
}

/* --------------------------------------------------------------------- */

static void write_ess(struct hfmodem_state *dev, unsigned char reg, unsigned char data)
{
	write_dsp(dev, reg);
	write_dsp(dev, data);
}

/* --------------------------------------------------------------------- */

static int read_ess(struct hfmodem_state *dev, unsigned char reg, unsigned char *data)
{
	write_dsp(dev, 0xc0);
	write_dsp(dev, reg);
	return read_dsp(dev, data);
}

/* --------------------------------------------------------------------- */

static int reset_ess(struct hfmodem_state *dev)
{
        int i;

        outb(3, DSP_RESET(dev->io.base_addr)); /* reset FIFOs too */
	udelay(3);
        outb(0, DSP_RESET(dev->io.base_addr));
        for (i = 0; i < 0xffff; i++)
                if (inb(DSP_DATA_AVAIL(dev->io.base_addr)) & 0x80)
                        if (inb(DSP_READ_DATA(dev->io.base_addr)) == 0xaa) {
				write_dsp(dev, ESS_EXTENDED_MODE);
                                return 1;
			}
        return 0;
}

/* --------------------------------------------------------------------- */

static int config_resources(struct hfmodem_state *dev)
{
        unsigned char irqreg = 0, dmareg = 0, realirq, realdma;
        unsigned long flags;

        switch (dev->io.irq) {
        case 2:
        case 9:
                irqreg |= 0x01;
                break;

        case 5:
                irqreg |= 0x02;
                break;

        case 7:
                irqreg |= 0x04;
                break;

        case 10:
                irqreg |= 0x08;
                break;
                
        default:
                return -ENODEV;
        }

        switch (dev->io.dma) {
        case 0:
                dmareg |= 0x01;
                break;

        case 1:
                dmareg |= 0x02;
                break;

        case 3:
                dmareg |= 0x08;
                break;

	case 5:
		dmareg |= 0x20;
		break;
		
	case 6:
		dmareg |= 0x40;
		break;
		
	case 7:
		dmareg |= 0x80;
		break;
		
         default:
                return -ENODEV;
        }
        save_flags(flags);
        cli();
        outb(0x80, DSP_MIXER_ADDR(dev->io.base_addr));
        outb(irqreg, DSP_MIXER_DATA(dev->io.base_addr));
        realirq = inb(DSP_MIXER_DATA(dev->io.base_addr));
        outb(0x81, DSP_MIXER_ADDR(dev->io.base_addr));
        outb(dmareg, DSP_MIXER_DATA(dev->io.base_addr));
        realdma = inb(DSP_MIXER_DATA(dev->io.base_addr));
        restore_flags(flags);
        if ((~realirq) & irqreg || (~realdma) & dmareg) {
                printk(KERN_ERR "%s: sbc resource registers cannot be set; PnP device "
                       "and IRQ/DMA specified wrongly?\n", hfmodem_drvname);
                return -EINVAL;
        }
        return 0;
}

/* --------------------------------------------------------------------- */

extern __inline__ void sbc_int_ack_8bit(struct hfmodem_state *dev)
{
        inb(DSP_DATA_AVAIL(dev->io.base_addr));
}

/* --------------------------------------------------------------------- */

extern __inline__ void sbc_int_ack_16bit(struct hfmodem_state *dev)
{
        inb(DSP_INTACK_16BIT(dev->io.base_addr));
}

/* --------------------------------------------------------------------- */

static void set_mixer(struct hfmodem_state *dev, unsigned char reg, unsigned char data)
{
	outb(reg, DSP_MIXER_ADDR(dev->io.base_addr));
	outb(data, DSP_MIXER_DATA(dev->io.base_addr));
}	

/* --------------------------------------------------------------------- */

int hfmodem_sbcprobe(struct hfmodem_state *dev)
{
	unsigned char revhi, revlo, essrevhi, essrevlo, tmp;
	int ret;

	if (dev->io.base_addr <= 0 || dev->io.base_addr > 0x1000-SBC_EXTENT || 
            dev->io.irq < 2 || dev->io.irq > 15 || dev->io.dma > 7 || dev->io.dma == 2)
                return -ENXIO;
        if (check_region(dev->io.base_addr, SBC_EXTENT))
                return -EACCES;
        /*
         * check if a card is available
         */
        if (!reset_dsp(dev)) {
                printk(KERN_ERR "%s: sbc: no card at io address 0x%x\n", 
		       hfmodem_drvname, dev->io.base_addr);
                return -ENODEV;
        }
	set_mixer(dev, 0, 0); /* reset mixer */
        write_dsp(dev, SBC_GET_REVISION);
        if (!read_dsp(dev, &revhi) || !read_dsp(dev, &revlo))
                return -ENODEV;
        printk(KERN_INFO "%s: SoundBlaster DSP revision %d.%02d\n", hfmodem_drvname, revhi, revlo);
	if (revhi == 3 && revlo == 1) {
		write_dsp(dev, ESS_GET_REVISION);
		if (!read_dsp(dev, &essrevhi) || !read_dsp(dev, &essrevlo))
			return -ENODEV;
		if (essrevhi == 0x48 && (essrevlo & 0xf0) == 0x80) {
			printk(KERN_INFO "%s: ESS ES488 AudioDrive (rev %d): unsupported.\n",
			       hfmodem_drvname, essrevlo & 0x0f);
			return -ENODEV;
		}
		if (essrevhi == 0x68 && (essrevlo & 0xf0) == 0x80) {
			printk(KERN_INFO "%s: ESS ES%s688 AudioDrive (rev %d)\n",
			       hfmodem_drvname, ((essrevlo & 0x0f) >= 8) ? "1" : "", essrevlo & 0x0f);
			if (dev->io.dma > 3) {
				printk(KERN_INFO "%s: DMA number out of range\n", hfmodem_drvname);
				return -ENXIO;
			}
			printk(KERN_INFO "%s: ess: irq: ", hfmodem_drvname);
			read_ess(dev, 0xb1, &tmp);
			switch (tmp & 0xf) {
			case 0:
				printk("2, 9, \"all others\"");
				break;
			       
			case 5:
				printk("5");
				break;

			case 10:
				printk("7");
				break;

			case 15:
				printk("10");
				break;

			default:
				printk("unknown (%d)", tmp & 0xf);
				break;
			}
			printk(" dma: ");
			read_ess(dev, 0xb2, &tmp);
			switch (tmp & 0xf) {
			case 0:
				printk("\"all others\"");
				break;
			       
			case 5:
				printk("0");
				break;

			case 10:
				printk("1");
				break;

			case 15:
				printk("3");
				break;

			default:
				printk("unknown (%d)", tmp & 0xf);
				break;
			}
			printk("\n");
			dev->scops = &ess_scops;
			return 0;
		}
	}
	if (revhi < 4) {
		printk(KERN_INFO "%s: at least SB16 required\n", hfmodem_drvname);
		return -ENODEV;
	}
	if (dev->io.dma < 4) {
		printk(KERN_INFO "%s: DMA number out of range\n", hfmodem_drvname);
		return -ENXIO;
	}
	if ((ret = config_resources(dev)))
		return ret;
	dev->scops = &sbc4_scops;
	return 0;
}

/* --------------------------------------------------------------------- */

static void sbc4_init(struct hfmodem_state *dev)
{
}

/* --------------------------------------------------------------------- */

static void sbc4_prepare_input(struct hfmodem_state *dev)
{
	unsigned long flags;

        if (!reset_dsp(dev)) {
                printk(KERN_ERR "%s: sbc: cannot reset sb dsp\n", hfmodem_drvname);
                return;
        }
        save_flags(flags);
        cli();
        disable_dma(dev->io.dma);
        clear_dma_ff(dev->io.dma);
        set_dma_mode(dev->io.dma, DMA_MODE_READ | DMA_MODE_AUTOINIT);
        set_dma_addr(dev->io.dma, virt_to_bus(dev->dma.buf));
        set_dma_count(dev->io.dma, HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE);
        enable_dma(dev->io.dma);
        sbc_int_ack_16bit(dev);
        write_dsp(dev, SBC_SAMPLE_RATE_IN); /* set sampling rate */
        write_dsp(dev, HFMODEM_SRATE >> 8);
        write_dsp(dev, HFMODEM_SRATE & 0xff);
        write_dsp(dev, SBC_SPEAKER_OFF); 
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void sbc4_trigger_input(struct hfmodem_state *dev)
{
	unsigned long flags;

        save_flags(flags);
        cli();
	write_dsp(dev, SBC4_IN16_AI_NO_FIFO);
	write_dsp(dev, SBC4_MODE_UNS_MONO);
	write_dsp(dev, (HFMODEM_FRAGSAMPLES-1) & 0xff);
	write_dsp(dev, (HFMODEM_FRAGSAMPLES-1) >> 8);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void sbc4_prepare_output(struct hfmodem_state *dev)
{
	unsigned long flags;

        if (!reset_dsp(dev)) {
                printk(KERN_ERR "%s: sbc: cannot reset sb dsp\n", hfmodem_drvname);
                return;
        }
        save_flags(flags);
        cli();
        disable_dma(dev->io.dma);
        clear_dma_ff(dev->io.dma);
        set_dma_mode(dev->io.dma, DMA_MODE_WRITE | DMA_MODE_AUTOINIT);
        set_dma_addr(dev->io.dma, virt_to_bus(dev->dma.buf));
        set_dma_count(dev->io.dma, HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE);
        enable_dma(dev->io.dma);
        sbc_int_ack_16bit(dev);
        write_dsp(dev, SBC_SAMPLE_RATE_OUT); /* set sampling rate */
        write_dsp(dev, HFMODEM_SRATE >> 8);
        write_dsp(dev, HFMODEM_SRATE & 0xff);
        write_dsp(dev, SBC_SPEAKER_ON); 
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void sbc4_trigger_output(struct hfmodem_state *dev)
{
	unsigned long flags;

        save_flags(flags);
        cli();
	write_dsp(dev, SBC4_OUT16_AI_NO_FIFO);
	write_dsp(dev, SBC4_MODE_UNS_MONO);
	write_dsp(dev, (HFMODEM_FRAGSAMPLES-1) & 0xff);
	write_dsp(dev, (HFMODEM_FRAGSAMPLES-1) >> 8);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void sbc4_stop(struct hfmodem_state *dev)
{
	reset_dsp(dev);
}

/* --------------------------------------------------------------------- */

static unsigned int sbc4_intack(struct hfmodem_state *dev)
{
	unsigned int dmaptr;
	unsigned long flags;
	unsigned char intsrc;

	save_flags(flags);
        cli();
        outb(0x82, DSP_MIXER_ADDR(dev->io.base_addr));
        intsrc = inb(DSP_MIXER_DATA(dev->io.base_addr));
        if (intsrc & 0x01) 
                sbc_int_ack_8bit(dev);
        if (intsrc & 0x02)
		sbc_int_ack_16bit(dev);
        disable_dma(dev->io.dma);
        clear_dma_ff(dev->io.dma);
        dmaptr = get_dma_residue(dev->io.dma);
        enable_dma(dev->io.dma);
	restore_flags(flags);
	if (dmaptr == 0 || dmaptr > HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE)
		dmaptr = HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE;
	return (HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE - dmaptr) / 2;
}

/* --------------------------------------------------------------------- */

static void sbc4_mixer(struct hfmodem_state *dev, int src, int igain, int ogain)
{
	unsigned long flags;
	static const unsigned char srcbits[3] = { 0x18, 0x01, 0x06 };

	save_flags(flags);
	cli();
	if (src >= 0 && src <= 2) {
		set_mixer(dev, 0x3d, srcbits[src]);
		set_mixer(dev, 0x3e, srcbits[src]);
	}
	if (ogain >= 0 && ogain <= 255) {
		set_mixer(dev, 0x30, ogain);
		set_mixer(dev, 0x31, ogain);
	}
	if (igain >= 0 && igain <= 255) {
		set_mixer(dev, 0x36, igain);
		set_mixer(dev, 0x37, igain);
		set_mixer(dev, 0x38, igain);
		set_mixer(dev, 0x39, igain);
		set_mixer(dev, 0x3a, igain);
	}
	set_mixer(dev, 0x32, 0xff);
	set_mixer(dev, 0x33, 0xff);
	set_mixer(dev, 0x34, 0);
	set_mixer(dev, 0x35, 0);
	set_mixer(dev, 0x3b, 0); /* pc spkr vol */
	set_mixer(dev, 0x3c, 0); /* output src */
	set_mixer(dev, 0x3f, 0); /* inp gain */
	set_mixer(dev, 0x40, 0);
	set_mixer(dev, 0x41, 0); /* outp gain */
	set_mixer(dev, 0x42, 0);
	set_mixer(dev, 0x43, 1); /* mic agc off */
	set_mixer(dev, 0x44, 8<<4); /* treble */
	set_mixer(dev, 0x45, 8<<4); 
	set_mixer(dev, 0x46, 8<<4); /* bass */
	set_mixer(dev, 0x47, 8<<4); 	
	restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void ess_prepare_input(struct hfmodem_state *dev)
{
	unsigned long flags;
	unsigned char tmp;

        if (!reset_ess(dev)) {
                printk(KERN_ERR "%s: sbc: cannot reset ess dsp\n", hfmodem_drvname);
                return;
        }
        save_flags(flags);
        cli();
        disable_dma(dev->io.dma);
        clear_dma_ff(dev->io.dma);
        set_dma_mode(dev->io.dma, DMA_MODE_READ | DMA_MODE_AUTOINIT);
        set_dma_addr(dev->io.dma, virt_to_bus(dev->dma.buf));
        set_dma_count(dev->io.dma, HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE);
        enable_dma(dev->io.dma);
        sbc_int_ack_8bit(dev);
	write_ess(dev, 0xa1, 128 - (397700 + HFMODEM_SRATE/2) / HFMODEM_SRATE);
	/*
	 * Set filter divider register
	 * Rolloff at 90% of the half sampling rate
	 */
	write_ess(dev, 0xa2, 256-(7160000 / (82 * (HFMODEM_SRATE * 9 / 20))));
        write_dsp(dev, SBC_SPEAKER_OFF); 
	write_ess(dev, 0xb8, 0x0e); /* Auto init DMA mode */
	read_ess(dev, 0xa8, &tmp);
	write_ess(dev, 0xa8, (tmp & ~0x03) | 2);     /* Mono */
	write_ess(dev, 0xb9, 2);    /* Demand mode (4 bytes/DMA request) */
	/* 16 bit mono */
	write_ess(dev, 0xb7, 0x71);
	write_ess(dev, 0xb7, 0xf4);

	read_ess(dev, 0xb1, &tmp);
	write_ess(dev, 0xb1, (tmp & 0x0f) | 0x50);
	read_ess(dev, 0xb2, &tmp);
	write_ess(dev, 0xb2, (tmp & 0x0f) | 0x50);
 
	write_ess(dev, 0xa4, (unsigned char) ((-HFMODEM_FRAGSIZE) & 0xff));
	write_ess(dev, 0xa5, (unsigned char) (((-HFMODEM_FRAGSIZE) >> 8) & 0xff));
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void ess_trigger_input(struct hfmodem_state *dev)
{
	unsigned long flags;
	unsigned char tmp;

        save_flags(flags);
        cli();
	read_ess(dev, 0xb8, &tmp);
	write_ess(dev, 0xb8, tmp | 0x0f);         /* Go */
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

void ess_prepare_output(struct hfmodem_state *dev)
{
	unsigned long flags;
	unsigned char tmp;

        if (!reset_ess(dev)) {
                printk(KERN_ERR "%s: sbc: cannot reset ess dsp\n", hfmodem_drvname);
                return;
        }
        save_flags(flags);
        cli();
        disable_dma(dev->io.dma);
        clear_dma_ff(dev->io.dma);
        set_dma_mode(dev->io.dma, DMA_MODE_WRITE | DMA_MODE_AUTOINIT);
        set_dma_addr(dev->io.dma, virt_to_bus(dev->dma.buf));
        set_dma_count(dev->io.dma, HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE);
        enable_dma(dev->io.dma);
        sbc_int_ack_8bit(dev);
	write_ess(dev, 0xa1, 128 - (397700 + HFMODEM_SRATE/2) / HFMODEM_SRATE);
	/*
	 * Set filter divider register
	 * Rolloff at 90% of the half sampling rate
	 */
	write_ess(dev, 0xa2, 256-(7160000 / (82 * (HFMODEM_SRATE * 9 / 20))));
	write_ess(dev, 0xb8, 0x04); /* Auto init DMA mode */
	read_ess(dev, 0xa8, &tmp);
	write_ess(dev, 0xa8, (tmp & ~0x03) | 2);     /* Mono */
	write_ess(dev, 0xb9, 2);    /* Demand mode (4 bytes/DMA request) */
	/* 16 bit mono */
	write_ess(dev, 0xb6, 0x00);
	write_ess(dev, 0xb7, 0x71);
	write_ess(dev, 0xb7, 0xf4);

	read_ess(dev, 0xb1, &tmp);
	write_ess(dev, 0xb1, (tmp & 0x0f) | 0x50);
	read_ess(dev, 0xb2, &tmp);
	write_ess(dev, 0xb2, (tmp & 0x0f) | 0x50);

	write_ess(dev, 0xa4, (unsigned char) ((-HFMODEM_FRAGSIZE) & 0xff));
	write_ess(dev, 0xa5, (unsigned char) (((-HFMODEM_FRAGSIZE) >> 8) & 0xff));

        write_dsp(dev, SBC_SPEAKER_ON); 
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

void ess_trigger_output(struct hfmodem_state *dev)
{
	unsigned long flags;
	unsigned char tmp;

        save_flags(flags);
        cli();
	read_ess(dev, 0xb8, &tmp);
	write_ess(dev, 0xb8, tmp | 0x05);         /* Go */
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

unsigned int ess_intack(struct hfmodem_state *dev)
{
	unsigned int dmaptr;
	unsigned long flags;
	unsigned char st;
#if 0
	static unsigned int cnt = 0;
#endif

	save_flags(flags);
        cli();
	st = inb(DSP_WRITE_STATUS(dev->io.base_addr));
	sbc_int_ack_8bit(dev);
        disable_dma(dev->io.dma);
        clear_dma_ff(dev->io.dma);
        dmaptr = get_dma_residue(dev->io.dma);
        enable_dma(dev->io.dma);
	restore_flags(flags);
#if 0
	cnt = (cnt + 1) & 0x3f;
	if (!cnt)
		printk(KERN_DEBUG "%s: ess: FIFO: full:%c empty:%c half empty:%c  IRQ: cpu:%c half empty:%c DMA:%c\n",
		       hfmodem_drvname, '1'-!(st&0x20), '1'-!(st&0x10), '1'-!(st&0x8), 
		       '1'-!(st&0x4), '1'-!(st&0x2), '1'-!(st&0x1));
#endif
	if (st & 0x20) /* FIFO full, 256 bytes */
		dmaptr += 256;
	else if (!(st & 0x10)) /* FIFO not empty, assume half full 128 bytes */
		dmaptr += 128;
	if (dmaptr > HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE)
		dmaptr -= HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE;
	if (dmaptr == 0 || dmaptr > HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE)
		dmaptr = HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE;
	return (HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE - dmaptr) / 2;
}

/* --------------------------------------------------------------------- */

static void ess_mixer(struct hfmodem_state *dev, int src, int igain, int ogain)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	if (src >= 0 && src <= 2) 
		set_mixer(dev, 0x0c, ((src+3) & 3) << 1);
	if (ogain >= 0 && ogain <= 255)
		set_mixer(dev, 0x22, (ogain & 0xf0) | ((ogain >> 4) & 0xf));
	if (igain >= 0 && igain <= 255) {
		set_mixer(dev, 0x36, igain);
		set_mixer(dev, 0x37, igain);
		set_mixer(dev, 0x38, igain);
		set_mixer(dev, 0x39, igain);
		set_mixer(dev, 0x3a, igain);
	}
	set_mixer(dev, 0x4, 0xff);
	set_mixer(dev, 0xe, 0x0);
	set_mixer(dev, 0x26, 0);
	set_mixer(dev, 0x28, 0);
	set_mixer(dev, 0x2e, 0);
	restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static const struct hfmodem_scops sbc4_scops = {
	SBC_EXTENT, sbc4_init, sbc4_prepare_input, sbc4_trigger_input, 
	sbc4_prepare_output, sbc4_trigger_output, sbc4_stop, sbc4_intack, sbc4_mixer
};

static const struct hfmodem_scops ess_scops = {
	SBC_EXTENT, sbc4_init, ess_prepare_input, ess_trigger_input, 
	ess_prepare_output, ess_trigger_output, sbc4_stop, ess_intack, ess_mixer
};

/* --------------------------------------------------------------------- */
