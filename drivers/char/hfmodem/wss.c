/*****************************************************************************/

/*
 *      wss.c  --  Linux soundcard HF FSK driver, 
 *                 WindowsSoundSystem specific functions.
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

#include <asm/io.h>
#include <asm/dma.h>
 
#include <linux/hfmodem.h>

/* --------------------------------------------------------------------- */

#define WSS_CONFIG(iobase)       (iobase+0)
#define WSS_STATUS(iobase)       (iobase+3)
#define WSS_CODEC_IA(iobase)     (iobase+4)
#define WSS_CODEC_ID(iobase)     (iobase+5)
#define WSS_CODEC_STATUS(iobase) (iobase+6)
#define WSS_CODEC_DATA(iobase)   (iobase+7)

#define WSS_EXTENT   8

/* --------------------------------------------------------------------- */

extern const struct hfmodem_scops wss_scops;

/* --------------------------------------------------------------------- */

static void write_codec(struct hfmodem_state *dev, unsigned char idx,
                        unsigned char data)
{
        int timeout = 900000;

        /* wait until codec ready */
        while (timeout > 0 && inb(WSS_CODEC_IA(dev->io.base_addr)) & 0x80)
                timeout--;
        outb(idx, WSS_CODEC_IA(dev->io.base_addr));
        outb(data, WSS_CODEC_ID(dev->io.base_addr));
}

/* --------------------------------------------------------------------- */

static unsigned char read_codec(struct hfmodem_state *dev, unsigned char idx)
{
        int timeout = 900000;

        /* wait until codec ready */
        while (timeout > 0 && inb(WSS_CODEC_IA(dev->io.base_addr)) & 0x80)
                timeout--;
        outb(idx & 0x1f, WSS_CODEC_IA(dev->io.base_addr));
        return inb(WSS_CODEC_ID(dev->io.base_addr));
}

/* --------------------------------------------------------------------- */

extern __inline__ void wss_ack_int(struct hfmodem_state *dev)
{
        outb(0, WSS_CODEC_STATUS(dev->io.base_addr));
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

static int wss_set_codec_fmt(struct hfmodem_state *dev, unsigned char fmt)
{
        unsigned long time;
        unsigned long flags;

        save_flags(flags);
        cli();
        /* Clock and data format register */
        write_codec(dev, 0x48, fmt);
	/* MCE and interface config reg */
	write_codec(dev, 0x49, 0xc);
        outb(0xb, WSS_CODEC_IA(dev->io.base_addr)); /* leave MCE */
        /*
         * wait for ACI start
         */
        time = 1000;
        while (!(read_codec(dev, 0x0b) & 0x20))
                if (!(--time)) {
                        printk(KERN_WARNING "%s: ad1848 auto calibration timed out (1)\n", 
                               hfmodem_drvname);
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
                       hfmodem_drvname);
                return -1;
        }
        return 0;
}

/* --------------------------------------------------------------------- */

static int wss_init_codec(struct hfmodem_state *dev)
{
        unsigned char tmp, revwss, revid;
        static const signed char irqtab[16] = { 
		-1, -1, 0x10, -1, -1, -1, -1, 0x08, -1, 0x10, 0x18, 0x20, -1, -1, -1, -1 
	};
        static const signed char dmatab[4] = { 1, 2, -1, 3 };
	int fmt;
        
	if ((fmt = wss_srate_index(HFMODEM_SRATE)) < 0) {
		printk(KERN_ERR "%s: WSS: sampling rate not supported\n", hfmodem_drvname);
		return -1;
	}
	fmt &= 0x0f;
#ifdef __BIG_ENDIAN
	fmt |= 0xc0;
#else /* __BIG_ENDIAN */
	fmt |= 0x40;
#endif /* __BIG_ENDIAN */
        tmp = inb(WSS_STATUS(dev->io.base_addr));
        if ((tmp & 0x3f) != 0x04 && (tmp & 0x3f) != 0x00 && 
            (tmp & 0x3f) != 0x0f) {
                printk(KERN_WARNING "%s: WSS card id register not found, "
                       "address 0x%x, ID register 0x%02x\n", hfmodem_drvname,
                       dev->io.base_addr, (int)tmp);
                /* return -1; */
                revwss = 0;
        } else {
                if ((tmp & 0x80) && ((dev->io.dma == 0) || ((dev->io.irq >= 8) && (dev->io.irq != 9)))) {
                        printk(KERN_ERR "%s: WSS: DMA0 and/or IRQ8..IRQ15 "
                               "(except IRQ9) cannot be used on an 8bit "
                               "card\n", hfmodem_drvname);
                        return -1;
                }               
                if (dev->io.irq > 15 || irqtab[dev->io.irq] == -1) {
                        printk(KERN_ERR "%s: WSS: invalid interrupt %d\n", 
                               hfmodem_drvname, (int)dev->io.irq);
                        return -1;
                }
                if (dev->io.dma > 3 || dmatab[dev->io.dma] == -1) {
                        printk(KERN_ERR "%s: WSS: invalid dma channel %d\n", 
                               hfmodem_drvname, (int)dev->io.dma);
                        return -1;
                }
                tmp = irqtab[dev->io.irq] | dmatab[dev->io.dma];
                /* irq probe */
                outb((tmp & 0x38) | 0x40, WSS_CONFIG(dev->io.base_addr));
                if (!(inb(WSS_STATUS(dev->io.base_addr)) & 0x40)) {
                        outb(0, WSS_CONFIG(dev->io.base_addr));
                        printk(KERN_ERR "%s: WSS: IRQ%d is not free!\n", 
                               hfmodem_drvname, dev->io.irq);
                }
                outb(tmp, WSS_CONFIG(dev->io.base_addr));
                revwss = inb(WSS_STATUS(dev->io.base_addr)) & 0x3f;
        }
        /*
         * initialize the codec
         */
        write_codec(dev, 9, 0);
	write_codec(dev, 12, 0);
        write_codec(dev, 0, 0x45);
        if (read_codec(dev, 0) != 0x45)
                goto codec_err;
        write_codec(dev, 0, 0xaa);
        if (read_codec(dev, 0) != 0xaa)
                goto codec_err;
        if (wss_set_codec_fmt(dev, fmt))
                goto codec_err;
        write_codec(dev, 0, 0x40); /* left input control */
        write_codec(dev, 1, 0x40); /* right input control */
        write_codec(dev, 2, 0x80); /* left aux#1 input control */
        write_codec(dev, 3, 0x80); /* right aux#1 input control */
        write_codec(dev, 4, 0x80); /* left aux#2 input control */
        write_codec(dev, 5, 0x80); /* right aux#2 input control */
        write_codec(dev, 6, 0x80); /* left dac control */
        write_codec(dev, 7, 0x80); /* right dac control */
        write_codec(dev, 0xa, 0x2); /* pin control register */
        write_codec(dev, 0xd, 0x0); /* digital mix control */
        revid = read_codec(dev, 0xc) & 0xf;
        /*
         * print revisions
         */        
	printk(KERN_INFO "%s: WSS revision %d, CODEC revision %d\n", 
	       hfmodem_drvname, (int)revwss, (int)revid);
        return 0;
 codec_err:
        outb(0, WSS_CONFIG(dev->io.base_addr));
        printk(KERN_ERR "%s: no WSS soundcard found at address 0x%x\n", 
               hfmodem_drvname, dev->io.base_addr);
        return -1;
}

/* --------------------------------------------------------------------- */

int hfmodem_wssprobe(struct hfmodem_state *dev)
{
	if (dev->io.base_addr <= 0 || dev->io.base_addr > 0x1000-WSS_EXTENT || 
            dev->io.irq < 2 || dev->io.irq > 15 || dev->io.dma > 3 || dev->io.dma == 2)
                return -ENXIO;
        if (check_region(dev->io.base_addr, WSS_EXTENT))
                return -EACCES;
        /*
         * check if a card is available
         */
        if (wss_init_codec(dev)) {
                printk(KERN_ERR "%s: sbc: no card at io address 0x%x\n", 
		       hfmodem_drvname, dev->io.base_addr);
                return -ENODEV;
        }
	dev->scops = &wss_scops;
	return 0;
}

/* --------------------------------------------------------------------- */

static void wss_init(struct hfmodem_state *dev)
{
	wss_init_codec(dev);
}

/* --------------------------------------------------------------------- */

static void wss_stop(struct hfmodem_state *dev)
{
	unsigned long flags;
        unsigned char oldcodecmode;
        long abrt;

        save_flags(flags);
        cli();
        /*
         * perform the final DMA sequence to disable the codec request
         */
        oldcodecmode = read_codec(dev, 9);
        write_codec(dev, 9, 0xc); /* disable codec */
        wss_ack_int(dev);
        if (read_codec(dev, 11) & 0x10) {
                disable_dma(dev->io.dma);
                clear_dma_ff(dev->io.dma);
                set_dma_mode(dev->io.dma, (oldcodecmode & 1) ? 
			     (DMA_MODE_WRITE | DMA_MODE_AUTOINIT) : (DMA_MODE_READ | DMA_MODE_AUTOINIT));
                set_dma_addr(dev->io.dma, virt_to_bus(dev->dma.buf));
                set_dma_count(dev->io.dma, HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE);
                enable_dma(dev->io.dma);
                abrt = 0;
                while ((read_codec(dev, 11) & 0x10) || ((++abrt) >= 0x10000));
        }
        disable_dma(dev->io.dma);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void wss_prepare_input(struct hfmodem_state *dev)
{
	unsigned long flags;

	wss_stop(dev);
	save_flags(flags);
        cli();
        disable_dma(dev->io.dma);
        clear_dma_ff(dev->io.dma);
        set_dma_mode(dev->io.dma, DMA_MODE_READ | DMA_MODE_AUTOINIT);
        set_dma_addr(dev->io.dma, virt_to_bus(dev->dma.buf));
        set_dma_count(dev->io.dma, HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE);
        enable_dma(dev->io.dma);
        write_codec(dev, 15, (HFMODEM_FRAGSAMPLES-1) & 0xff);
        write_codec(dev, 14, (HFMODEM_FRAGSAMPLES-1) >> 8);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void wss_trigger_input(struct hfmodem_state *dev)
{
	unsigned long flags;

	save_flags(flags);
        cli();
        write_codec(dev, 9, 0x0e);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void wss_prepare_output(struct hfmodem_state *dev)
{
	unsigned long flags;

	wss_stop(dev);
        save_flags(flags);
        cli();
        disable_dma(dev->io.dma);
        clear_dma_ff(dev->io.dma);
        set_dma_mode(dev->io.dma, DMA_MODE_WRITE | DMA_MODE_AUTOINIT);
        set_dma_addr(dev->io.dma, virt_to_bus(dev->dma.buf));
        set_dma_count(dev->io.dma, HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE);
        enable_dma(dev->io.dma);
        write_codec(dev, 15, (HFMODEM_FRAGSAMPLES-1) & 0xff);
        write_codec(dev, 14, (HFMODEM_FRAGSAMPLES-1) >> 8);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void wss_trigger_output(struct hfmodem_state *dev)
{
	unsigned long flags;

        save_flags(flags);
        cli();
        write_codec(dev, 9, 0x0d);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static unsigned int wss_intack(struct hfmodem_state *dev)
{
	unsigned int dmaptr, nums;
	unsigned long flags;

	save_flags(flags);
        cli();
	wss_ack_int(dev);
        disable_dma(dev->io.dma);
        clear_dma_ff(dev->io.dma);
        dmaptr = get_dma_residue(dev->io.dma);
	if (dmaptr == 0 || dmaptr > HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE)
		dmaptr = HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE;
	nums = (((dmaptr - 1) % HFMODEM_FRAGSIZE) - 1) / 2;
        write_codec(dev, 15, nums  & 0xff);
        write_codec(dev, 14, nums >> 8);
        enable_dma(dev->io.dma);
	restore_flags(flags);
	return (HFMODEM_NUMFRAGS * HFMODEM_FRAGSIZE - dmaptr) / 2;
}

/* --------------------------------------------------------------------- */

static void wss_mixer(struct hfmodem_state *dev, int src, int igain, int ogain)
{
	unsigned long flags;
	static const unsigned char srctoreg[3] = { 1, 2, 0 };
	static const unsigned char regtosrc[4] = { 2, 0, 1, 0 };
	unsigned char tmp;

	save_flags(flags);
	cli();
	tmp = read_codec(dev, 0x00);
	if (src < 0 || src > 2)
		src = regtosrc[(tmp >> 6) & 3];
	if (igain < 0 || igain > 255) {
		if (src == 1)
			igain = ((tmp & 0xf) + ((tmp & 0x20) ? 13 : 0)) << 3;
		else
			igain = (tmp & 0xf) << 4;
	}
	if (src == 1) {
		if (igain > (28<<3))
			tmp = 0x2f;
		else if (igain >= (13<<3))
			tmp = 0x20 + (((igain >> 3) - 13) & 0xf);
		else 
			tmp = (igain >> 3) & 0xf;
	} else 
		tmp = (igain >> 4) & 0xf;
	tmp |= srctoreg[src] << 6;
	write_codec(dev, 0, tmp);
	write_codec(dev, 1, tmp);
	if (ogain > 0 && ogain <= 255) {
		tmp = 63 - (ogain >> 2);
		write_codec(dev, 6, tmp);
		write_codec(dev, 7, tmp);
	} else if (ogain == 0) {
		write_codec(dev, 6, 0x80);
		write_codec(dev, 7, 0x80);
	}
	restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static const struct hfmodem_scops wss_scops = {
	WSS_EXTENT, wss_init, wss_prepare_input, wss_trigger_input, 
	wss_prepare_output, wss_trigger_output, wss_stop, wss_intack, wss_mixer
};

/* --------------------------------------------------------------------- */
