/*****************************************************************************/

/*
 *      es1371.c  --  Creative Ensoniq ES1371.
 *
 *      Copyright (C) 1998  Thomas Sailer (sailer@ife.ee.ethz.ch)
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
 * Special thanks to Ensoniq
 *
 *
 * Module command line parameters:
 *   joystick must be set to the base I/O-Port to be used for
 *   the gameport. Legal values are 0x200, 0x208, 0x210 and 0x218.         
 *   The gameport is mirrored eight times.
 *        
 *  Supported devices:
 *  /dev/dsp    standard /dev/dsp device, (mostly) OSS compatible
 *  /dev/mixer  standard /dev/mixer device, (mostly) OSS compatible
 *  /dev/dsp1   additional DAC, like /dev/dsp, but outputs to mixer "SYNTH" setting
 *  /dev/midi   simple MIDI UART interface, no ioctl
 *
 *  NOTE: the card does not have any FM/Wavetable synthesizer, it is supposed
 *  to be done in software. That is what /dev/dac is for. By now (Q2 1998)
 *  there are several MIDI to PCM (WAV) packages, one of them is timidity.
 *
 *  Revision history
 *    04.06.98   0.1   Initial release
 *                     Mixer stuff should be overhauled; especially optional AC97 mixer bits
 *                     should be detected. This results in strange behaviour of some mixer
 *                     settings, like master volume and mic.
 *    08.06.98   0.2   First release using Alan Cox' soundcore instead of miscdevice
 *    03.08.98   0.3   Do not include modversions.h
 *                     Now mixer behaviour can basically be selected between
 *                     "OSS documented" and "OSS actual" behaviour
 *    31.08.98   0.4   Fix realplayer problems - dac.count issues
 *    27.10.98   0.5   Fix joystick support
 *                     -- Oliver Neukum (c188@org.chemie.uni-muenchen.de)
 *    10.12.98   0.6   Fix drain_dac trying to wait on not yet initialized DMA
 *    23.12.98   0.7   Fix a few f_file & FMODE_ bugs
 *                     Don't wake up app until there are fragsize bytes to read/write
 *    06.01.99   0.8   remove the silly SA_INTERRUPT flag.
 *                     hopefully killed the egcs section type conflict
 *
 */

/*****************************************************************************/
      
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/malloc.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <asm/spinlock.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

/* --------------------------------------------------------------------- */

#undef OSS_DOCUMENTED_MIXER_SEMANTICS

/* --------------------------------------------------------------------- */

#ifndef PCI_VENDOR_ID_ENSONIQ
#define PCI_VENDOR_ID_ENSONIQ        0x1274    
#endif
#ifndef PCI_DEVICE_ID_ENSONIQ_ES1371
#define PCI_DEVICE_ID_ENSONIQ_ES1371 0x1371
#endif

#define ES1371_MAGIC  ((PCI_VENDOR_ID_ENSONIQ<<16)|PCI_DEVICE_ID_ENSONIQ_ES1371)

#define ES1371_EXTENT             0x40
#define JOY_EXTENT                8

#define ES1371_REG_CONTROL        0x00
#define ES1371_REG_STATUS         0x04
#define ES1371_REG_UART_DATA      0x08
#define ES1371_REG_UART_STATUS    0x09
#define ES1371_REG_UART_CONTROL   0x09
#define ES1371_REG_UART_TEST      0x0a
#define ES1371_REG_MEMPAGE        0x0c
#define ES1371_REG_SRCONV         0x10
#define ES1371_REG_CODEC          0x14
#define ES1371_REG_LEGACY         0x18
#define ES1371_REG_SERIAL_CONTROL 0x20
#define ES1371_REG_DAC1_SCOUNT    0x24
#define ES1371_REG_DAC2_SCOUNT    0x28
#define ES1371_REG_ADC_SCOUNT     0x2c

#define ES1371_REG_DAC1_FRAMEADR  0xc30
#define ES1371_REG_DAC1_FRAMECNT  0xc34
#define ES1371_REG_DAC2_FRAMEADR  0xc38
#define ES1371_REG_DAC2_FRAMECNT  0xc3c
#define ES1371_REG_ADC_FRAMEADR   0xd30
#define ES1371_REG_ADC_FRAMECNT   0xd34

#define ES1371_FMT_U8_MONO     0
#define ES1371_FMT_U8_STEREO   1
#define ES1371_FMT_S16_MONO    2
#define ES1371_FMT_S16_STEREO  3
#define ES1371_FMT_STEREO      1
#define ES1371_FMT_S16         2
#define ES1371_FMT_MASK        3

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

#define CTRL_JOY_SHIFT  24
#define CTRL_JOY_MASK   3
#define CTRL_JOY_200    0x00000000  /* joystick base address */
#define CTRL_JOY_208    0x01000000
#define CTRL_JOY_210    0x02000000
#define CTRL_JOY_218    0x03000000
#define CTRL_GPIO_IN0   0x00100000  /* general purpose inputs/outputs */
#define CTRL_GPIO_IN1   0x00200000
#define CTRL_GPIO_IN2   0x00400000
#define CTRL_GPIO_IN3   0x00800000
#define CTRL_GPIO_OUT0  0x00010000
#define CTRL_GPIO_OUT1  0x00020000
#define CTRL_GPIO_OUT2  0x00040000
#define CTRL_GPIO_OUT3  0x00080000
#define CTRL_MSFMTSEL   0x00008000  /* MPEG serial data fmt: 0 = Sony, 1 = I2S */
#define CTRL_SYNCRES    0x00004000  /* AC97 warm reset */
#define CTRL_ADCSTOP    0x00002000  /* stop ADC transfers */
#define CTRL_PWR_INTRM  0x00001000  /* 1 = power level ints enabled */
#define CTRL_M_CB       0x00000800  /* recording source: 0 = ADC, 1 = MPEG */
#define CTRL_CCB_INTRM  0x00000400  /* 1 = CCB "voice" ints enabled */
#define CTRL_PDLEV0     0x00000000  /* power down level */
#define CTRL_PDLEV1     0x00000100
#define CTRL_PDLEV2     0x00000200
#define CTRL_PDLEV3     0x00000300
#define CTRL_BREQ       0x00000080  /* 1 = test mode (internal mem test) */
#define CTRL_DAC1_EN    0x00000040  /* enable DAC1 */
#define CTRL_DAC2_EN    0x00000020  /* enable DAC2 */
#define CTRL_ADC_EN     0x00000010  /* enable ADC */
#define CTRL_UART_EN    0x00000008  /* enable MIDI uart */
#define CTRL_JYSTK_EN   0x00000004  /* enable Joystick port */
#define CTRL_XTALCLKDIS 0x00000002  /* 1 = disable crystal clock input */
#define CTRL_PCICLKDIS  0x00000001  /* 1 = disable PCI clock distribution */


#define STAT_INTR       0x80000000  /* wired or of all interrupt bits */
#define STAT_SYNC_ERR   0x00000100  /* 1 = codec sync error */
#define STAT_VC         0x000000c0  /* CCB int source, 0=DAC1, 1=DAC2, 2=ADC, 3=undef */
#define STAT_SH_VC      6
#define STAT_MPWR       0x00000020  /* power level interrupt */
#define STAT_MCCB       0x00000010  /* CCB int pending */
#define STAT_UART       0x00000008  /* UART int pending */
#define STAT_DAC1       0x00000004  /* DAC1 int pending */
#define STAT_DAC2       0x00000002  /* DAC2 int pending */
#define STAT_ADC        0x00000001  /* ADC int pending */

#define USTAT_RXINT     0x80        /* UART rx int pending */
#define USTAT_TXINT     0x04        /* UART tx int pending */
#define USTAT_TXRDY     0x02        /* UART tx ready */
#define USTAT_RXRDY     0x01        /* UART rx ready */

#define UCTRL_RXINTEN   0x80        /* 1 = enable RX ints */
#define UCTRL_TXINTEN   0x60        /* TX int enable field mask */
#define UCTRL_ENA_TXINT 0x20        /* enable TX int */
#define UCTRL_CNTRL     0x03        /* control field */
#define UCTRL_CNTRL_SWR 0x03        /* software reset command */

/* sample rate converter */
#define SRC_RAMADDR_MASK   0xfe000000
#define SRC_RAMADDR_SHIFT  25
#define SRC_WE             0x01000000  /* read/write control for SRC RAM */
#define SRC_BUSY           0x00800000  /* SRC busy */
#define SRC_DIS            0x00400000  /* 1 = disable SRC */
#define SRC_DDAC1          0x00200000  /* 1 = disable accum update for DAC1 */
#define SRC_DDAC2          0x00100000  /* 1 = disable accum update for DAC2 */
#define SRC_DADC           0x00080000  /* 1 = disable accum update for ADC2 */
#define SRC_RAMDATA_MASK   0x0000ffff
#define SRC_RAMDATA_SHIFT  0

#define SRCREG_ADC      0x78
#define SRCREG_DAC1     0x70
#define SRCREG_DAC2     0x74
#define SRCREG_VOL_ADC  0x6c
#define SRCREG_VOL_DAC1 0x7c
#define SRCREG_VOL_DAC2 0x7e

#define SRCREG_TRUNC_N     0x00
#define SRCREG_INT_REGS    0x01
#define SRCREG_ACCUM_FRAC  0x02
#define SRCREG_VFREQ_FRAC  0x03

#define CODEC_PIRD        0x00800000  /* 0 = write AC97 register */
#define CODEC_PIADD_MASK  0x007f0000
#define CODEC_PIADD_SHIFT 16
#define CODEC_PIDAT_MASK  0x0000ffff
#define CODEC_PIDAT_SHIFT 0

#define CODEC_RDY         0x80000000  /* AC97 read data valid */
#define CODEC_WIP         0x40000000  /* AC97 write in progress */
#define CODEC_PORD        0x00800000  /* 0 = write AC97 register */
#define CODEC_POADD_MASK  0x007f0000
#define CODEC_POADD_SHIFT 16
#define CODEC_PODAT_MASK  0x0000ffff
#define CODEC_PODAT_SHIFT 0


#define LEGACY_JFAST      0x80000000  /* fast joystick timing */
#define LEGACY_FIRQ       0x01000000  /* force IRQ */

#define SCTRL_DACTEST     0x00400000  /* 1 = DAC test, test vector generation purposes */
#define SCTRL_P2ENDINC    0x00380000  /*  */
#define SCTRL_SH_P2ENDINC 19
#define SCTRL_P2STINC     0x00070000  /*  */
#define SCTRL_SH_P2STINC  16
#define SCTRL_R1LOOPSEL   0x00008000  /* 0 = loop mode */
#define SCTRL_P2LOOPSEL   0x00004000  /* 0 = loop mode */
#define SCTRL_P1LOOPSEL   0x00002000  /* 0 = loop mode */
#define SCTRL_P2PAUSE     0x00001000  /* 1 = pause mode */
#define SCTRL_P1PAUSE     0x00000800  /* 1 = pause mode */
#define SCTRL_R1INTEN     0x00000400  /* enable interrupt */
#define SCTRL_P2INTEN     0x00000200  /* enable interrupt */
#define SCTRL_P1INTEN     0x00000100  /* enable interrupt */
#define SCTRL_P1SCTRLD    0x00000080  /* reload sample count register for DAC1 */
#define SCTRL_P2DACSEN    0x00000040  /* 1 = DAC2 play back last sample when disabled */
#define SCTRL_R1SEB       0x00000020  /* 1 = 16bit */
#define SCTRL_R1SMB       0x00000010  /* 1 = stereo */
#define SCTRL_R1FMT       0x00000030  /* format mask */
#define SCTRL_SH_R1FMT    4
#define SCTRL_P2SEB       0x00000008  /* 1 = 16bit */
#define SCTRL_P2SMB       0x00000004  /* 1 = stereo */
#define SCTRL_P2FMT       0x0000000c  /* format mask */
#define SCTRL_SH_P2FMT    2
#define SCTRL_P1SEB       0x00000002  /* 1 = 16bit */
#define SCTRL_P1SMB       0x00000001  /* 1 = stereo */
#define SCTRL_P1FMT       0x00000003  /* format mask */
#define SCTRL_SH_P1FMT    0

/* codec constants */

#define CODEC_ID_DEDICATEDMIC    0x001
#define CODEC_ID_MODEMCODEC      0x002
#define CODEC_ID_BASSTREBLE      0x004
#define CODEC_ID_SIMULATEDSTEREO 0x008
#define CODEC_ID_HEADPHONEOUT    0x010
#define CODEC_ID_LOUDNESS        0x020
#define CODEC_ID_18BITDAC        0x040
#define CODEC_ID_20BITDAC        0x080
#define CODEC_ID_18BITADC        0x100
#define CODEC_ID_20BITADC        0x200

#define CODEC_ID_SESHIFT    10
#define CODEC_ID_SEMASK     0x1f


/* misc stuff */

#define FMODE_DAC         4           /* slight misuse of mode_t */

/* MIDI buffer sizes */

#define MIDIINBUF  256
#define MIDIOUTBUF 256

#define FMODE_MIDI_SHIFT 3
#define FMODE_MIDI_READ  (FMODE_READ << FMODE_MIDI_SHIFT)
#define FMODE_MIDI_WRITE (FMODE_WRITE << FMODE_MIDI_SHIFT)

#define SND_DEV_DSP16   5 

/* --------------------------------------------------------------------- */

static const char *stereo_enhancement[] __initdata = 
{
	"no 3D stereo enhancement",
	"Analog Devices Phat Stereo",
	"Creative Stereo Enhancement",
	"National Semiconductor 3D Stereo Enhancement",
	"YAMAHA Ymersion",
	"BBE 3D Stereo Enhancement",
	"Crystal Semiconductor 3D Stereo Enhancement",
	"Qsound QXpander",
	"Spatializer 3D Stereo Enhancement",
	"SRS 3D Stereo Enhancement",
	"Platform Technologies 3D Stereo Enhancement", 
	"AKM 3D Audio",
	"Aureal Stereo Enhancement",
	"AZTECH  3D Enhancement",
	"Binaura 3D Audio Enhancement",
	"ESS Technology Stereo Enhancement",
	"Harman International VMAx",
	"NVidea 3D Stereo Enhancement",
	"Philips Incredible Sound",
	"Texas Instruments 3D Stereo Enhancement",
	"VLSI Technology 3D Stereo Enhancement"
};

/* --------------------------------------------------------------------- */

struct es1371_state {
	/* magic */
	unsigned int magic;

	/* we keep sb cards in a linked list */
	struct es1371_state *next;

	/* soundcore stuff */
	int dev_audio;
	int dev_mixer;
	int dev_dac;
	int dev_midi;
	
	/* hardware resources */
	unsigned int io, irq;

	/* mixer registers; there is no HW readback */
	struct {
		unsigned short codec_id;
		unsigned int modcnt;
#ifndef OSS_DOCUMENTED_MIXER_SEMANTICS
		unsigned short vol[13];
#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */
	} mix;

	/* wave stuff */
	unsigned ctrl;
	unsigned sctrl;
	unsigned dac1rate, dac2rate, adcrate;

	spinlock_t lock;
	struct semaphore open_sem;
	mode_t open_mode;
	struct wait_queue *open_wait;

	struct dmabuf {
		void *rawbuf;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;
		unsigned hwptr, swptr;
		unsigned total_bytes;
		int count;
		unsigned error; /* over/underrun */
		struct wait_queue *wait;
		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;
		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned endcleared:1;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
	} dma_dac1, dma_dac2, dma_adc;

	/* midi stuff */
	struct {
		unsigned ird, iwr, icnt;
		unsigned ord, owr, ocnt;
		struct wait_queue *iwait;
		struct wait_queue *owait;
		unsigned char ibuf[MIDIINBUF];
		unsigned char obuf[MIDIOUTBUF];
	} midi;
};

/* --------------------------------------------------------------------- */

static struct es1371_state *devs = NULL;

/* --------------------------------------------------------------------- */

extern inline unsigned ld2(unsigned int x)
{
	unsigned r = 0;
	
	if (x >= 0x10000) {
		x >>= 16;
		r += 16;
	}
	if (x >= 0x100) {
		x >>= 8;
		r += 8;
	}
	if (x >= 0x10) {
		x >>= 4;
		r += 4;
	}
	if (x >= 4) {
		x >>= 2;
		r += 2;
	}
	if (x >= 2)
		r++;
	return r;
}

/* --------------------------------------------------------------------- */
/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#ifdef hweight32
#undef hweight32
#endif

extern __inline__ unsigned int hweight32(unsigned int w)
{
        unsigned int res = (w & 0x55555555) + ((w >> 1) & 0x55555555);
        res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
        res = (res & 0x0F0F0F0F) + ((res >> 4) & 0x0F0F0F0F);
        res = (res & 0x00FF00FF) + ((res >> 8) & 0x00FF00FF);
        return (res & 0x0000FFFF) + ((res >> 16) & 0x0000FFFF);
}

/* --------------------------------------------------------------------- */

static unsigned wait_src_ready(struct es1371_state *s)
{
	unsigned int t, r;

	for (t = 0; t < 1000; t++) {
		if (!((r = inl(s->io + ES1371_REG_SRCONV)) & SRC_BUSY))
			return r;
		udelay(1);
	}
	printk(KERN_DEBUG "es1371: sample rate converter timeout r = 0x%08x\n", r);
	return r;
}

static unsigned src_read(struct es1371_state *s, unsigned reg)
{
	unsigned int r;

	r = wait_src_ready(s) & (SRC_DIS | SRC_DDAC1 | SRC_DDAC2 | SRC_DADC);
	r |= (reg << SRC_RAMADDR_SHIFT) & SRC_RAMADDR_MASK;
	outl(r, s->io + ES1371_REG_SRCONV);
	return (wait_src_ready(s) & SRC_RAMDATA_MASK) >> SRC_RAMDATA_SHIFT;
}


static void src_write(struct es1371_state *s, unsigned reg, unsigned data)
{
	unsigned int r;

	r = wait_src_ready(s) & (SRC_DIS | SRC_DDAC1 | SRC_DDAC2 | SRC_DADC);
	r |= (reg << SRC_RAMADDR_SHIFT) & SRC_RAMADDR_MASK;
	r |= (data << SRC_RAMDATA_SHIFT) & SRC_RAMDATA_MASK;
	outl(r | SRC_WE, s->io + ES1371_REG_SRCONV);
}

/* --------------------------------------------------------------------- */

/* most of the following here is black magic */

static void set_adc_rate(struct es1371_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned int n, truncm, freq;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;
	n = rate / 3000;
	if ((1 << n) & ((1 << 15) | (1 << 13) | (1 << 11) | (1 << 9)))
		n--;
	truncm = (21 * n - 1) | 1;
        freq = ((48000UL << 15) / rate) * n;
	s->adcrate = (48000UL << 15) / (freq / n);
	spin_lock_irqsave(&s->lock, flags);
	if (rate >= 24000) {
		if (truncm > 239)
			truncm = 239;
		src_write(s, SRCREG_ADC+SRCREG_TRUNC_N, 
			  (((239 - truncm) >> 1) << 9) | (n << 4));
	} else {
		if (truncm > 119)
			truncm = 119;
		src_write(s, SRCREG_ADC+SRCREG_TRUNC_N, 
			  0x8000 | (((119 - truncm) >> 1) << 9) | (n << 4));
	}		
	src_write(s, SRCREG_ADC+SRCREG_INT_REGS, 
		  (src_read(s, SRCREG_ADC+SRCREG_INT_REGS) & 0x00ff) |
		  ((freq >> 5) & 0xfc00));
	src_write(s, SRCREG_ADC+SRCREG_VFREQ_FRAC, freq & 0x7fff);
	src_write(s, SRCREG_VOL_ADC, n << 8);
	src_write(s, SRCREG_VOL_ADC+1, n << 8);
	spin_unlock_irqrestore(&s->lock, flags);
}

static void set_dac1_rate(struct es1371_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned int freq, r;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;
        freq = (rate << 15) / 3000;
	s->dac1rate = (freq * 3000) >> 15;
	spin_lock_irqsave(&s->lock, flags);
	r = (wait_src_ready(s) & (SRC_DIS | SRC_DDAC2 | SRC_DADC)) | SRC_DDAC1;
	outl(r, s->io + ES1371_REG_SRCONV);
	src_write(s, SRCREG_DAC1+SRCREG_INT_REGS, 
		  (src_read(s, SRCREG_DAC1+SRCREG_INT_REGS) & 0x00ff) |
		  ((freq >> 5) & 0xfc00));
	src_write(s, SRCREG_DAC1+SRCREG_VFREQ_FRAC, freq & 0x7fff);
	r = (wait_src_ready(s) & (SRC_DIS | SRC_DDAC2 | SRC_DADC));
	outl(r, s->io + ES1371_REG_SRCONV);
	spin_unlock_irqrestore(&s->lock, flags);
}

static void set_dac2_rate(struct es1371_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned int freq, r;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;
        freq = (rate << 15) / 3000;
	s->dac2rate = (freq * 3000) >> 15;
	spin_lock_irqsave(&s->lock, flags);
	r = (wait_src_ready(s) & (SRC_DIS | SRC_DDAC1 | SRC_DADC)) | SRC_DDAC2;
	outl(r, s->io + ES1371_REG_SRCONV);
	src_write(s, SRCREG_DAC2+SRCREG_INT_REGS, 
		  (src_read(s, SRCREG_DAC2+SRCREG_INT_REGS) & 0x00ff) |
		  ((freq >> 5) & 0xfc00));
	src_write(s, SRCREG_DAC2+SRCREG_VFREQ_FRAC, freq & 0x7fff);
	r = (wait_src_ready(s) & (SRC_DIS | SRC_DDAC1 | SRC_DADC));
	outl(r, s->io + ES1371_REG_SRCONV);
	spin_unlock_irqrestore(&s->lock, flags);
}

/* --------------------------------------------------------------------- */

static void wrcodec(struct es1371_state *s, unsigned addr, unsigned data)
{
	unsigned long flags;
	unsigned t, x;

	for (t = 0; t < 0x1000; t++)
		if (!(inl(s->io+ES1371_REG_CODEC) & CODEC_WIP))
			break;
	spin_lock_irqsave(&s->lock, flags);
	/* save the current state for later */
	x = inl(s->io+ES1371_REG_SRCONV);
	/* enable SRC state data in SRC mux */
	outl((wait_src_ready(s) & (SRC_DIS | SRC_DDAC1 | SRC_DDAC2 | SRC_DADC)) | 0x00010000,
	     s->io+ES1371_REG_SRCONV);
	/* wait for a SAFE time to write addr/data and then do it, dammit */
	for (t = 0; t < 0x1000; t++)
		if ((inl(s->io+ES1371_REG_SRCONV) & 0x00070000) == 0x00010000)
			break;
	outl(((addr << CODEC_POADD_SHIFT) & CODEC_POADD_MASK) |
	     ((data << CODEC_PODAT_SHIFT) & CODEC_PODAT_MASK), s->io+ES1371_REG_CODEC);
	/* restore SRC reg */
	wait_src_ready(s);
	outl(x, s->io+ES1371_REG_SRCONV);
	spin_unlock_irqrestore(&s->lock, flags);
}

static unsigned rdcodec(struct es1371_state *s, unsigned addr)
{
	unsigned long flags;
	unsigned t, x;

	for (t = 0; t < 0x1000; t++)
		if (!(inl(s->io+ES1371_REG_CODEC) & CODEC_WIP))
			break;
	spin_lock_irqsave(&s->lock, flags);
	/* save the current state for later */
	x = inl(s->io+ES1371_REG_SRCONV);
	/* enable SRC state data in SRC mux */
	outl((wait_src_ready(s) & (SRC_DIS | SRC_DDAC1 | SRC_DDAC2 | SRC_DADC)) | 0x00010000,
	     s->io+ES1371_REG_SRCONV);
	/* wait for a SAFE time to write addr/data and then do it, dammit */
	for (t = 0; t < 0x1000; t++)
		if ((inl(s->io+ES1371_REG_SRCONV) & 0x00070000) == 0x00010000)
			break;
	outl(((addr << CODEC_POADD_SHIFT) & CODEC_POADD_MASK) | CODEC_PORD, s->io+ES1371_REG_CODEC);
	/* restore SRC reg */
	wait_src_ready(s);
	outl(x, s->io+ES1371_REG_SRCONV);
	spin_unlock_irqrestore(&s->lock, flags);
	/* now wait for the stinkin' data (RDY) */
	for (t = 0; t < 0x1000; t++)
		if ((x = inl(s->io+ES1371_REG_CODEC)) & CODEC_RDY)
			break;
	return ((x & CODEC_PIDAT_MASK) >> CODEC_PIDAT_SHIFT);
}

/* --------------------------------------------------------------------- */

extern inline void stop_adc(struct es1371_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ctrl &= ~CTRL_ADC_EN;
	outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
}	

extern inline void stop_dac1(struct es1371_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ctrl &= ~CTRL_DAC1_EN;
	outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
}	

extern inline void stop_dac2(struct es1371_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ctrl &= ~CTRL_DAC2_EN;
	outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac1(struct es1371_state *s)
{
	unsigned long flags;
	unsigned fragremain, fshift;

	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ctrl & CTRL_DAC1_EN) && (s->dma_dac1.mapped || s->dma_dac1.count > 0)
	    && s->dma_dac1.ready) {
		s->ctrl |= CTRL_DAC1_EN;
		s->sctrl = (s->sctrl & ~(SCTRL_P1LOOPSEL | SCTRL_P1PAUSE | SCTRL_P1SCTRLD)) | SCTRL_P1INTEN;
		outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
		fragremain = ((- s->dma_dac1.hwptr) & (s->dma_dac1.fragsize-1));
		fshift = sample_shift[(s->sctrl & SCTRL_P1FMT) >> SCTRL_SH_P1FMT];
		if (fragremain < 2*fshift)
			fragremain = s->dma_dac1.fragsize;
		outl((fragremain >> fshift) - 1, s->io+ES1371_REG_DAC1_SCOUNT);
		outl(s->ctrl, s->io+ES1371_REG_CONTROL);
		outl((s->dma_dac1.fragsize >> fshift) - 1, s->io+ES1371_REG_DAC1_SCOUNT);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac2(struct es1371_state *s)
{
	unsigned long flags;
	unsigned fragremain, fshift;

	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ctrl & CTRL_DAC2_EN) && (s->dma_dac2.mapped || s->dma_dac2.count > 0)
	    && s->dma_dac2.ready) {
		s->ctrl |= CTRL_DAC2_EN;
		s->sctrl = (s->sctrl & ~(SCTRL_P2LOOPSEL | SCTRL_P2PAUSE | SCTRL_P2DACSEN | 
					 SCTRL_P2ENDINC | SCTRL_P2STINC)) | SCTRL_P2INTEN |
			(((s->sctrl & SCTRL_P2FMT) ? 2 : 1) << SCTRL_SH_P2ENDINC) | 
			(0 << SCTRL_SH_P2STINC);
		outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
		fragremain = ((- s->dma_dac2.hwptr) & (s->dma_dac2.fragsize-1));
		fshift = sample_shift[(s->sctrl & SCTRL_P2FMT) >> SCTRL_SH_P2FMT];
		if (fragremain < 2*fshift)
			fragremain = s->dma_dac2.fragsize;
		outl((fragremain >> fshift) - 1, s->io+ES1371_REG_DAC2_SCOUNT);
		outl(s->ctrl, s->io+ES1371_REG_CONTROL);
		outl((s->dma_dac2.fragsize >> fshift) - 1, s->io+ES1371_REG_DAC2_SCOUNT);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_adc(struct es1371_state *s)
{
	unsigned long flags;
	unsigned fragremain, fshift;

	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ctrl & CTRL_ADC_EN) && (s->dma_adc.mapped || s->dma_adc.count < (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize))
	    && s->dma_adc.ready) {
		s->ctrl |= CTRL_ADC_EN;
		s->sctrl = (s->sctrl & ~SCTRL_R1LOOPSEL) | SCTRL_R1INTEN;
		outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
		fragremain = ((- s->dma_adc.hwptr) & (s->dma_adc.fragsize-1));
		fshift = sample_shift[(s->sctrl & SCTRL_R1FMT) >> SCTRL_SH_R1FMT];
		if (fragremain < 2*fshift)
			fragremain = s->dma_adc.fragsize;
		outl((fragremain >> fshift) - 1, s->io+ES1371_REG_ADC_SCOUNT);
		outl(s->ctrl, s->io+ES1371_REG_CONTROL);
		outl((s->dma_adc.fragsize >> fshift) - 1, s->io+ES1371_REG_ADC_SCOUNT);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

/* --------------------------------------------------------------------- */

#define DMABUF_DEFAULTORDER (17-PAGE_SHIFT)
#define DMABUF_MINORDER 1


extern inline void dealloc_dmabuf(struct dmabuf *db)
{
	unsigned long map, mapend;

	if (db->rawbuf) {
		/* undo marking the pages as reserved */
		mapend = MAP_NR(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
		for (map = MAP_NR(db->rawbuf); map <= mapend; map++)
			clear_bit(PG_reserved, &mem_map[map].flags);	
		free_pages((unsigned long)db->rawbuf, db->buforder);
	}
	db->rawbuf = NULL;
	db->mapped = db->ready = 0;
}

static int prog_dmabuf(struct es1371_state *s, struct dmabuf *db, unsigned rate, unsigned fmt, unsigned reg)
{
	int order;
	unsigned bytepersec;
	unsigned bufs;
	unsigned long map, mapend;

	db->hwptr = db->swptr = db->total_bytes = db->count = db->error = db->endcleared = 0;
	if (!db->rawbuf) {
		db->ready = db->mapped = 0;
		for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER && !db->rawbuf; order--)
			db->rawbuf = (void *)__get_free_pages(GFP_KERNEL, order);
		if (!db->rawbuf)
			return -ENOMEM;
		db->buforder = order;
		/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
		mapend = MAP_NR(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
		for (map = MAP_NR(db->rawbuf); map <= mapend; map++)
			set_bit(PG_reserved, &mem_map[map].flags);
	}
	fmt &= ES1371_FMT_MASK;
	bytepersec = rate << sample_shift[fmt];
	bufs = PAGE_SIZE << db->buforder;
	if (db->ossfragshift) {
		if ((1000 << db->ossfragshift) < bytepersec)
			db->fragshift = ld2(bytepersec/1000);
		else
			db->fragshift = db->ossfragshift;
	} else {
		db->fragshift = ld2(bytepersec/100/(db->subdivision ? db->subdivision : 1));
		if (db->fragshift < 3)
			db->fragshift = 3;
	}
	db->numfrag = bufs >> db->fragshift;
	while (db->numfrag < 4 && db->fragshift > 3) {
		db->fragshift--;
		db->numfrag = bufs >> db->fragshift;
	}
	db->fragsize = 1 << db->fragshift;
	if (db->ossmaxfrags >= 4 && db->ossmaxfrags < db->numfrag)
		db->numfrag = db->ossmaxfrags;
	db->fragsamples = db->fragsize >> sample_shift[fmt];
	db->dmasize = db->numfrag << db->fragshift;
	memset(db->rawbuf, (fmt & ES1371_FMT_S16) ? 0 : 0x80, db->dmasize);
	outl((reg >> 8) & 15, s->io+ES1371_REG_MEMPAGE);
	outl(virt_to_bus(db->rawbuf), s->io+(reg & 0xff));
	outl((db->dmasize >> 2)-1, s->io+((reg + 4) & 0xff));
	db->ready = 1;
	return 0;
}

extern inline int prog_dmabuf_adc(struct es1371_state *s)
{
	stop_adc(s);
	return prog_dmabuf(s, &s->dma_adc, s->adcrate, (s->sctrl >> SCTRL_SH_R1FMT) & ES1371_FMT_MASK, 
			   ES1371_REG_ADC_FRAMEADR);
}

extern inline int prog_dmabuf_dac2(struct es1371_state *s)
{
	stop_dac2(s);
	return prog_dmabuf(s, &s->dma_dac2, s->dac2rate, (s->sctrl >> SCTRL_SH_P2FMT) & ES1371_FMT_MASK, 
			   ES1371_REG_DAC2_FRAMEADR);
}

extern inline int prog_dmabuf_dac1(struct es1371_state *s)
{
	stop_dac1(s);
	return prog_dmabuf(s, &s->dma_dac1, s->dac1rate, (s->sctrl >> SCTRL_SH_P1FMT) & ES1371_FMT_MASK,
			   ES1371_REG_DAC1_FRAMEADR);
}

extern inline unsigned get_hwptr(struct es1371_state *s, struct dmabuf *db, unsigned reg)
{
	unsigned hwptr, diff;

	outl((reg >> 8) & 15, s->io+ES1371_REG_MEMPAGE);
	hwptr = (inl(s->io+(reg & 0xff)) >> 14) & 0x3fffc;
	diff = (db->dmasize + hwptr - db->hwptr) % db->dmasize;
	db->hwptr = hwptr;
	return diff;
}

extern inline void clear_advance(void *buf, unsigned bsize, unsigned bptr, unsigned len, unsigned char c)
{
	if (bptr + len > bsize) {
		unsigned x = bsize - bptr;
		memset(((char *)buf) + bptr, c, x);
		bptr = 0;
		len -= x;
	}
	memset(((char *)buf) + bptr, c, len);
}

/* call with spinlock held! */
static void es1371_update_ptr(struct es1371_state *s)
{
	int diff;

	/* update ADC pointer */
	if (s->ctrl & CTRL_ADC_EN) {
		diff = get_hwptr(s, &s->dma_adc, ES1371_REG_ADC_FRAMECNT);
		s->dma_adc.total_bytes += diff;
		s->dma_adc.count += diff;
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize) 
			wake_up(&s->dma_adc.wait);
		if (!s->dma_adc.mapped) {
			if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
				s->ctrl &= ~CTRL_ADC_EN;
				outl(s->ctrl, s->io+ES1371_REG_CONTROL);
				s->dma_adc.error++;
			}
		}
	}
	/* update DAC1 pointer */
	if (s->ctrl & CTRL_DAC1_EN) {
		diff = get_hwptr(s, &s->dma_dac1, ES1371_REG_DAC1_FRAMECNT);
		s->dma_dac1.total_bytes += diff;
		if (s->dma_dac1.mapped) {
			s->dma_dac1.count += diff;
			if (s->dma_dac1.count >= (signed)s->dma_dac1.fragsize)
				wake_up(&s->dma_dac1.wait);
		} else {
			s->dma_dac1.count -= diff;
			if (s->dma_dac1.count <= 0) {
				s->ctrl &= ~CTRL_DAC1_EN;
				outl(s->ctrl, s->io+ES1371_REG_CONTROL);
				s->dma_dac1.error++;
			} else if (s->dma_dac1.count <= (signed)s->dma_dac1.fragsize && !s->dma_dac1.endcleared) {
				clear_advance(s->dma_dac1.rawbuf, s->dma_dac1.dmasize, s->dma_dac1.swptr, 
					      s->dma_dac1.fragsize, (s->sctrl & SCTRL_P1SEB) ? 0 : 0x80);
				s->dma_dac1.endcleared = 1;
			}
			if (s->dma_dac1.count + (signed)s->dma_dac1.fragsize <= (signed)s->dma_dac1.dmasize)
				wake_up(&s->dma_dac1.wait);
		}
	}
	/* update DAC2 pointer */
	if (s->ctrl & CTRL_DAC2_EN) {
		diff = get_hwptr(s, &s->dma_dac2, ES1371_REG_DAC2_FRAMECNT);
		s->dma_dac2.total_bytes += diff;
		if (s->dma_dac2.mapped) {
			s->dma_dac2.count += diff;
			if (s->dma_dac2.count >= (signed)s->dma_dac2.fragsize)
				wake_up(&s->dma_dac2.wait);
		} else {
			s->dma_dac2.count -= diff;
			if (s->dma_dac2.count <= 0) {
				s->ctrl &= ~CTRL_DAC2_EN;
				outl(s->ctrl, s->io+ES1371_REG_CONTROL);
				s->dma_dac2.error++;
			} else if (s->dma_dac2.count <= (signed)s->dma_dac2.fragsize && !s->dma_dac2.endcleared) {
				clear_advance(s->dma_dac2.rawbuf, s->dma_dac2.dmasize, s->dma_dac2.swptr, 
					      s->dma_dac2.fragsize, (s->sctrl & SCTRL_P2SEB) ? 0 : 0x80);
				s->dma_dac2.endcleared = 1;
			}
			if (s->dma_dac2.count + (signed)s->dma_dac2.fragsize <= (signed)s->dma_dac2.dmasize)
				wake_up(&s->dma_dac2.wait);
		}
	}
}

/* hold spinlock for the following! */
static void es1371_handle_midi(struct es1371_state *s)
{
	unsigned char ch;
	int wake;

	if (!(s->ctrl & CTRL_UART_EN))
		return;
	wake = 0;
	while (inb(s->io+ES1371_REG_UART_STATUS) & USTAT_RXRDY) {
		ch = inb(s->io+ES1371_REG_UART_DATA);
		if (s->midi.icnt < MIDIINBUF) {
			s->midi.ibuf[s->midi.iwr] = ch;
			s->midi.iwr = (s->midi.iwr + 1) % MIDIINBUF;
			s->midi.icnt++;
		}
		wake = 1;
	}
	if (wake)
		wake_up(&s->midi.iwait);
	wake = 0;
	while ((inb(s->io+ES1371_REG_UART_STATUS) & USTAT_TXRDY) && s->midi.ocnt > 0) {
		outb(s->midi.obuf[s->midi.ord], s->io+ES1371_REG_UART_DATA);
		s->midi.ord = (s->midi.ord + 1) % MIDIOUTBUF;
		s->midi.ocnt--;
		if (s->midi.ocnt < MIDIOUTBUF-16)
			wake = 1;
	}
	if (wake)
		wake_up(&s->midi.owait);
	outb((s->midi.ocnt > 0) ? UCTRL_RXINTEN | UCTRL_ENA_TXINT : UCTRL_RXINTEN, s->io+ES1371_REG_UART_CONTROL);
}

static void es1371_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        struct es1371_state *s = (struct es1371_state *)dev_id;
	unsigned int intsrc, sctl;
	
	/* fastpath out, to ease interrupt sharing */
	intsrc = inl(s->io+ES1371_REG_STATUS);
	if (!(intsrc & 0x80000000))
		return;
	spin_lock(&s->lock);
	/* clear audio interrupts first */
	sctl = s->sctrl;
	if (intsrc & STAT_ADC)
		sctl &= ~SCTRL_R1INTEN;
	if (intsrc & STAT_DAC1)
		sctl &= ~SCTRL_P1INTEN;
	if (intsrc & STAT_DAC2)
		sctl &= ~SCTRL_P2INTEN;
	outl(sctl, s->io+ES1371_REG_SERIAL_CONTROL);
	outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
	es1371_update_ptr(s);
	es1371_handle_midi(s);
	spin_unlock(&s->lock);
}

/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT "es1371: invalid magic value\n";

#define VALIDATE_STATE(s)                         \
({                                                \
	if (!(s) || (s)->magic != ES1371_MAGIC) { \
		printk(invalid_magic);            \
		return -ENXIO;                    \
	}                                         \
})

/* --------------------------------------------------------------------- */

#define AC97_PESSIMISTIC

/*
 * this define causes the driver to assume that all optional
 * AC97 bits are missing. This is what Ensoniq does too in their
 * Windows driver. Maybe we should one day autoprobe for these
 * bits. But anyway I have to see an AC97 codec that implements
 * one of those optional (volume) bits.
 */

static const unsigned int recsrc[8] = 
{
	SOUND_MASK_MIC,
	SOUND_MASK_CD,
	SOUND_MASK_VIDEO,
	SOUND_MASK_LINE1,
	SOUND_MASK_LINE,
	SOUND_MASK_VOLUME,
	SOUND_MASK_PHONEOUT,
	SOUND_MASK_PHONEIN
};

static const unsigned char volreg[SOUND_MIXER_NRDEVICES] = 
{
	/* 5 bit stereo */
	[SOUND_MIXER_LINE] = 0x10,
	[SOUND_MIXER_CD] = 0x12,
	[SOUND_MIXER_VIDEO] = 0x14,
	[SOUND_MIXER_LINE1] = 0x16,
	[SOUND_MIXER_PCM] = 0x18,
	/* 6 bit stereo */
	[SOUND_MIXER_VOLUME] = 0x02,
	[SOUND_MIXER_PHONEOUT] = 0x04,
	/* 6 bit mono */
	[SOUND_MIXER_OGAIN] = 0x06,
	[SOUND_MIXER_PHONEIN] = 0x0c,
	/* 4 bit mono but shifted by 1 */
	[SOUND_MIXER_SPEAKER] = 0x08,
	/* 6 bit mono + preamp */
	[SOUND_MIXER_MIC] = 0x0e,
	/* 4 bit stereo */
	[SOUND_MIXER_RECLEV] = 0x1c,
	/* 4 bit mono */
	[SOUND_MIXER_IGAIN] = 0x1e
};

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS

#define swab(x) ((((x) >> 8) & 0xff) | (((x) << 8) & 0xff00))

static int mixer_rdch(struct es1371_state *s, unsigned int ch, int *arg)
{
	int j;

	switch (ch) {
	case SOUND_MIXER_MIC:
		j = rdcodec(s, 0x0e);
		if (j & 0x8000)
			return put_user(0, (int *)arg);
#ifdef AC97_PESSIMISTIC
		return put_user(0x4949 - 0x202 * (j & 0x1f) + ((j & 0x40) ? 0x1b1b : 0), (int *)arg);
#else /* AC97_PESSIMISTIC */
		return put_user(0x5757 - 0x101 * ((j & 0x3f) * 5 / 4) + ((j & 0x40) ? 0x0d0d : 0), (int *)arg);
#endif /* AC97_PESSIMISTIC */

	case SOUND_MIXER_OGAIN:
	case SOUND_MIXER_PHONEIN:
		j = rdcodec(s, volreg[ch]);
		if (j & 0x8000)
			return put_user(0, (int *)arg);
#ifdef AC97_PESSIMISTIC
		return put_user(0x6464 - 0x303 * (j & 0x1f), (int *)arg);
#else /* AC97_PESSIMISTIC */
		return put_user((0x6464 - 0x303 * (j & 0x3f) / 2) & 0x7f7f, (int *)arg);
#endif /* AC97_PESSIMISTIC */

	case SOUND_MIXER_PHONEOUT:
		if (!(s->mix.codec_id & CODEC_ID_HEADPHONEOUT))
			return -EINVAL;
		/* fall through */
	case SOUND_MIXER_VOLUME:
		j = rdcodec(s, volreg[ch]);
		if (j & 0x8000)
			return put_user(0, (int *)arg);
#ifdef AC97_PESSIMISTIC
		return put_user(0x6464 - (swab(j) & 0x1f1f) * 3, (int *)arg);
#else /* AC97_PESSIMISTIC */
		return put_user((0x6464 - (swab(j) & 0x3f3f) * 3 / 2) & 0x7f7f, (int *)arg);
#endif /* AC97_PESSIMISTIC */
		
	case SOUND_MIXER_SPEAKER:
		j = rdcodec(s, 0x0a);
		if (j & 0x8000)
			return put_user(0, (int *)arg);
		return put_user(0x6464 - ((j >> 1) & 0xf) * 0x606, (int *)arg);
		
	case SOUND_MIXER_LINE:
	case SOUND_MIXER_CD:
	case SOUND_MIXER_VIDEO:
	case SOUND_MIXER_LINE1:
	case SOUND_MIXER_PCM:
		j = rdcodec(s, volreg[ch]);
		if (j & 0x8000)
			return put_user(0, (int *)arg);
		return put_user(0x6464 - (swab(j) & 0x1f1f) * 3, (int *)arg);

	case SOUND_MIXER_BASS:
	case SOUND_MIXER_TREBLE:
		if (!(s->mix.codec_id & CODEC_ID_BASSTREBLE))
			return -EINVAL;
		j = rdcodec(s, 0x08);
		if (ch == SOUND_MIXER_BASS)
			j >>= 8;
		return put_user((((j & 15) * 100) / 15) * 0x101, (int *)arg);
	
		/* SOUND_MIXER_RECLEV and SOUND_MIXER_IGAIN specify gain */
	case SOUND_MIXER_RECLEV:
		j = rdcodec(s, 0x1c);
		if (j & 0x8000)
			return put_user(0, (int *)arg);
		return put_user((swab(j)  & 0xf0f) * 6 + 0xa0a, (int *)arg);
		
	case SOUND_MIXER_IGAIN:
		if (!(s->mix.codec_id & CODEC_ID_DEDICATEDMIC))
			return -EINVAL;
		j = rdcodec(s, 0x1e);
		if (j & 0x8000)
			return put_user(0, (int *)arg);
		return put_user((j & 0xf) * 0x606 + 0xa0a, (int *)arg);
	
	default:
		return -EINVAL;
	}
}

#else /* OSS_DOCUMENTED_MIXER_SEMANTICS */

static const unsigned char volidx[SOUND_MIXER_NRDEVICES] = 
{
	/* 5 bit stereo */
	[SOUND_MIXER_LINE] = 1,
	[SOUND_MIXER_CD] = 2,
	[SOUND_MIXER_VIDEO] = 3,
	[SOUND_MIXER_LINE1] = 4,
	[SOUND_MIXER_PCM] = 5,
	/* 6 bit stereo */
	[SOUND_MIXER_VOLUME] = 6,
	[SOUND_MIXER_PHONEOUT] = 7,
	/* 6 bit mono */
	[SOUND_MIXER_OGAIN] = 8,
	[SOUND_MIXER_PHONEIN] = 9,
	/* 4 bit mono but shifted by 1 */
	[SOUND_MIXER_SPEAKER] = 10,
	/* 6 bit mono + preamp */
	[SOUND_MIXER_MIC] = 11,
	/* 4 bit stereo */
	[SOUND_MIXER_RECLEV] = 12,
	/* 4 bit mono */
	[SOUND_MIXER_IGAIN] = 13
};

#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */

static int mixer_wrch(struct es1371_state *s, unsigned int ch, int val)
{
	int i;
	unsigned l1, r1;

	l1 = val & 0xff;
	r1 = (val >> 8) & 0xff;
	if (l1 > 100)
		l1 = 100;
	if (r1 > 100)
		r1 = 100;
	switch (ch) {
	case SOUND_MIXER_LINE:
	case SOUND_MIXER_CD:
	case SOUND_MIXER_VIDEO:
	case SOUND_MIXER_LINE1:
	case SOUND_MIXER_PCM:
		if (l1 < 7 && r1 < 7) {
			wrcodec(s, volreg[ch], 0x8000);
			return 0;
		}
		if (l1 < 7)
			l1 = 7;
		if (r1 < 7)
			r1 = 7;
		wrcodec(s, volreg[ch], (((100 - l1) / 3) << 8) | ((100 - r1) / 3));
		return 0;

	case SOUND_MIXER_PHONEOUT:
		if (!(s->mix.codec_id & CODEC_ID_HEADPHONEOUT))
			return -EINVAL;
		/* fall through */
	case SOUND_MIXER_VOLUME:
#ifdef AC97_PESSIMISTIC
		if (l1 < 7 && r1 < 7) {
			wrcodec(s, volreg[ch], 0x8000);
			return 0;
		}
		if (l1 < 7)
			l1 = 7;
		if (r1 < 7)
			r1 = 7;
		wrcodec(s, volreg[ch], (((100 - l1) / 3) << 8) | ((100 - r1) / 3));
		return 0;
#else /* AC97_PESSIMISTIC */
		if (l1 < 4 && r1 < 4) {
			wrcodec(s, volreg[ch], 0x8000);
			return 0;
		}
		if (l1 < 4)
			l1 = 4;
		if (r1 < 4)
			r1 = 4;
		wrcodec(s, volreg[ch], ((2 * (100 - l1) / 3) << 8) | (2 * (100 - r1) / 3));
		return 0;
#endif /* AC97_PESSIMISTIC */

	case SOUND_MIXER_OGAIN:
	case SOUND_MIXER_PHONEIN:
#ifdef AC97_PESSIMISTIC
		wrcodec(s, volreg[ch], (l1 < 7) ? 0x8000 : (100 - l1) / 3);
		return 0;
#else /* AC97_PESSIMISTIC */
		wrcodec(s, volreg[ch], (l1 < 4) ? 0x8000 : (2 * (100 - l1) / 3));
		return 0;
#endif /* AC97_PESSIMISTIC */
			
	case SOUND_MIXER_SPEAKER:
		wrcodec(s, 0x0a, (l1 < 10) ? 0x8000 : ((100 - l1) / 6) << 1);
		return 0;

	case SOUND_MIXER_MIC:
#ifdef AC97_PESSIMISTIC
		if (l1 < 11) {
			wrcodec(s, 0x0e, 0x8000);
			return 0;
		}
		i = 0;
		if (l1 >= 27) {
			l1 -= 27;
			i = 0x40;
		}
		if (l1 < 11) 
			l1 = 11;
		wrcodec(s, 0x0e, ((73 - l1) / 2) | i);
		return 0;
#else /* AC97_PESSIMISTIC */
		if (l1 < 9) {
			wrcodec(s, 0x0e, 0x8000);
			return 0;
		}
		i = 0;
		if (l1 >= 13) {
			l1 -= 13;
			i = 0x40;
		}
		if (l1 < 9) 
			l1 = 9;
		wrcodec(s, 0x0e, (((87 - l1) * 4) / 5) | i);
		return 0;
#endif /* AC97_PESSIMISTIC */
		
	case SOUND_MIXER_BASS:
		val = ((l1 * 15) / 100) & 0xf;
		wrcodec(s, 0x08, (rdcodec(s, 0x08) & 0x00ff) | (val << 8));
		return 0;

	case SOUND_MIXER_TREBLE:
		val = ((l1 * 15) / 100) & 0xf;
		wrcodec(s, 0x08, (rdcodec(s, 0x08) & 0xff00) | val);
		return 0;
		
		/* SOUND_MIXER_RECLEV and SOUND_MIXER_IGAIN specify gain */
	case SOUND_MIXER_RECLEV:
		if (l1 < 10 || r1 < 10) {
			wrcodec(s, 0x1c, 0x8000);
			return 0;
		}
		if (l1 < 10)
			l1 = 10;
		if (r1 < 10)
			r1 = 10;
		wrcodec(s, 0x1c, (((l1 - 10) / 6) << 8) | ((r1 - 10) / 6));
		return 0;

	case SOUND_MIXER_IGAIN:
		if (!(s->mix.codec_id & CODEC_ID_DEDICATEDMIC))
			return -EINVAL;
		wrcodec(s, 0x1e, (l1 < 10) ? 0x8000 : ((l1 - 10) / 6) & 0xf);
		return 0;
		
	default:
		return -EINVAL;
	}
}

static int mixer_ioctl(struct es1371_state *s, unsigned int cmd, unsigned long arg)
{
	int i, val;

	VALIDATE_STATE(s);
	if (cmd == SOUND_MIXER_PRIVATE1) {
		if (!(s->mix.codec_id & (CODEC_ID_SEMASK << CODEC_ID_SESHIFT)))
			return -EINVAL;
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val & 1)
			wrcodec(s, 0x22, ((val << 3) & 0xf00) | ((val >> 1) & 0xf));
		val = rdcodec(s, 0x22);
		return put_user(((val & 0xf) << 1) | ((val & 0xf00) >> 3), (int *)arg);
	}
        if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		strncpy(info.id, "ES1371", sizeof(info.id));
		strncpy(info.name, "Ensoniq ES1371", sizeof(info.name));
		info.modify_counter = s->mix.modcnt;
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		strncpy(info.id, "ES1371", sizeof(info.id));
		strncpy(info.name, "Ensoniq ES1371", sizeof(info.name));
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, (int *)arg);
	if (_IOC_TYPE(cmd) != 'M' || _IOC_SIZE(cmd) != sizeof(int))
                return -EINVAL;
        if (_IOC_DIR(cmd) == _IOC_READ) {
                switch (_IOC_NR(cmd)) {
                case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
			return put_user(recsrc[rdcodec(s, 0x1a) & 7], (int *)arg);
			
                case SOUND_MIXER_DEVMASK: /* Arg contains a bit for each supported device */
			return put_user(SOUND_MASK_LINE | SOUND_MASK_CD | SOUND_MASK_VIDEO |
					SOUND_MASK_LINE1 | SOUND_MASK_PCM | SOUND_MASK_VOLUME |
					SOUND_MASK_OGAIN | SOUND_MASK_PHONEIN | SOUND_MASK_SPEAKER |
					SOUND_MASK_MIC | SOUND_MASK_RECLEV |
					((s->mix.codec_id & CODEC_ID_BASSTREBLE) ? (SOUND_MASK_BASS | SOUND_MASK_TREBLE) : 0) |
					((s->mix.codec_id & CODEC_ID_HEADPHONEOUT) ? SOUND_MASK_PHONEOUT : 0) |
					((s->mix.codec_id & CODEC_ID_DEDICATEDMIC) ? SOUND_MASK_IGAIN : 0), (int *)arg);

		case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			return put_user(SOUND_MASK_MIC | SOUND_MASK_CD | SOUND_MASK_VIDEO | SOUND_MASK_LINE1 |
					SOUND_MASK_LINE | SOUND_MASK_VOLUME | SOUND_MASK_PHONEOUT |
					SOUND_MASK_PHONEIN, (int *)arg);

                case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
			return put_user(SOUND_MASK_LINE | SOUND_MASK_CD | SOUND_MASK_VIDEO |
					SOUND_MASK_LINE1 | SOUND_MASK_PCM | SOUND_MASK_VOLUME |
					SOUND_MASK_PHONEOUT | SOUND_MASK_RECLEV, (int *)arg);

                case SOUND_MIXER_CAPS:
			return put_user(SOUND_CAP_EXCL_INPUT, (int *)arg);

		default:
			i = _IOC_NR(cmd);
                        if (i >= SOUND_MIXER_NRDEVICES)
                                return -EINVAL;
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
			return mixer_rdch(s, i, (int *)arg);
#else /* OSS_DOCUMENTED_MIXER_SEMANTICS */
			if (!volidx[i])
				return -EINVAL;
			return put_user(s->mix.vol[volidx[i]-1], (int *)arg);
#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */
		}
	}
        if (_IOC_DIR(cmd) != (_IOC_READ|_IOC_WRITE)) 
		return -EINVAL;
	s->mix.modcnt++;
	switch (_IOC_NR(cmd)) {
	case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
                get_user_ret(val, (int *)arg, -EFAULT);
                i = hweight32(val);
                if (i == 0)
                        return 0; /*val = mixer_recmask(s);*/
                else if (i > 1) 
                        val &= ~recsrc[rdcodec(s, 0x1a) & 7];
                for (i = 0; i < 8; i++) {
			if (val & recsrc[i]) {
				wrcodec(s, 0x1a, 0x101 * i);
				return 0;
			}
		}
                return 0;

	default:
		i = _IOC_NR(cmd);
		if (i >= SOUND_MIXER_NRDEVICES)
			return -EINVAL;
		get_user_ret(val, (int *)arg, -EFAULT);
		if (mixer_wrch(s, i, val))
			return -EINVAL;
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		return mixer_rdch(s, i, (int *)arg);
#else /* OSS_DOCUMENTED_MIXER_SEMANTICS */
		if (!volidx[i])
			return -EINVAL;
		s->mix.vol[volidx[i]-1] = val;
		return put_user(s->mix.vol[volidx[i]-1], (int *)arg);
#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */
	}
}

/* --------------------------------------------------------------------- */

static loff_t es1371_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

/* --------------------------------------------------------------------- */

static int es1371_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct es1371_state *s = devs;

	while (s && s->dev_mixer != minor)
		s = s->next;
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	MOD_INC_USE_COUNT;
	return 0;
}

static int es1371_release_mixdev(struct inode *inode, struct file *file)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	
	VALIDATE_STATE(s);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int es1371_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	return mixer_ioctl((struct es1371_state *)file->private_data, cmd, arg);
}

static /*const*/ struct file_operations es1371_mixer_fops = {
	&es1371_llseek,
	NULL,  /* read */
	NULL,  /* write */
	NULL,  /* readdir */
	NULL,  /* poll */
	&es1371_ioctl_mixdev,
	NULL,  /* mmap */
	&es1371_open_mixdev,
	NULL,	/* flush */
	&es1371_release_mixdev,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL,  /* lock */
};

/* --------------------------------------------------------------------- */

static int drain_dac1(struct es1371_state *s, int nonblock)
{
        struct wait_queue wait = { current, NULL };
	unsigned long flags;
	int count, tmo;
	
	if (s->dma_dac1.mapped || !s->dma_dac1.ready)
		return 0;
        current->state = TASK_INTERRUPTIBLE;
        add_wait_queue(&s->dma_dac1.wait, &wait);
        for (;;) {
                spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac1.count;
                spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
                        break;
                if (nonblock) {
                        remove_wait_queue(&s->dma_dac1.wait, &wait);
                        current->state = TASK_RUNNING;
                        return -EBUSY;
                }
		tmo = (count * HZ) / s->dac1rate;
		tmo >>= sample_shift[(s->sctrl & SCTRL_P1FMT) >> SCTRL_SH_P1FMT];
		if (!schedule_timeout(tmo ? : 1) && tmo)
			printk(KERN_DEBUG "es1371: dma timed out??\n");
        }
        remove_wait_queue(&s->dma_dac1.wait, &wait);
        current->state = TASK_RUNNING;
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

static int drain_dac2(struct es1371_state *s, int nonblock)
{
        struct wait_queue wait = { current, NULL };
	unsigned long flags;
	int count, tmo;

	if (s->dma_dac2.mapped || !s->dma_dac2.ready)
		return 0;
        current->state = TASK_INTERRUPTIBLE;
        add_wait_queue(&s->dma_dac2.wait, &wait);
        for (;;) {
                spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac2.count;
                spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
                        break;
                if (nonblock) {
                        remove_wait_queue(&s->dma_dac2.wait, &wait);
                        current->state = TASK_RUNNING;
                        return -EBUSY;
                }
		tmo = (count * HZ) / s->dac2rate;
		tmo >>= sample_shift[(s->sctrl & SCTRL_P2FMT) >> SCTRL_SH_P2FMT];
		if (!schedule_timeout(tmo ? : 1) && tmo)
			printk(KERN_DEBUG "es1371: dma timed out??\n");
        }
        remove_wait_queue(&s->dma_dac2.wait, &wait);
        current->state = TASK_RUNNING;
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

/* --------------------------------------------------------------------- */

static ssize_t es1371_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_adc.mapped)
		return -ENXIO;
	if (!s->dma_adc.ready && (ret = prog_dmabuf_adc(s)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		swptr = s->dma_adc.swptr;
		cnt = s->dma_adc.dmasize-swptr;
		if (s->dma_adc.count < cnt)
			cnt = s->dma_adc.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			start_adc(s);
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EBUSY;
			interruptible_sleep_on(&s->dma_adc.wait);
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_to_user(buffer, s->dma_adc.rawbuf + swptr, cnt))
			return ret ? ret : -EFAULT;
		swptr = (swptr + cnt) % s->dma_adc.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_adc.swptr = swptr;
		s->dma_adc.count -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_adc(s);
	}
	return ret;
}

static ssize_t es1371_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_dac2.mapped)
		return -ENXIO;
	if (!s->dma_dac2.ready && (ret = prog_dmabuf_dac2(s)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		if (s->dma_dac2.count < 0) {
			s->dma_dac2.count = 0;
			s->dma_dac2.swptr = s->dma_dac2.hwptr;
		}
		swptr = s->dma_dac2.swptr;
		cnt = s->dma_dac2.dmasize-swptr;
		if (s->dma_dac2.count + cnt > s->dma_dac2.dmasize)
			cnt = s->dma_dac2.dmasize - s->dma_dac2.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			start_dac2(s);
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EBUSY;
			interruptible_sleep_on(&s->dma_dac2.wait);
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_from_user(s->dma_dac2.rawbuf + swptr, buffer, cnt))
			return ret ? ret : -EFAULT;
		swptr = (swptr + cnt) % s->dma_dac2.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_dac2.swptr = swptr;
		s->dma_dac2.count += cnt;
		s->dma_dac2.endcleared = 0;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac2(s);
	}
	return ret;
}

static unsigned int es1371_poll(struct file *file, struct poll_table_struct *wait)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &s->dma_dac2.wait, wait);
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &s->dma_adc.wait, wait);
	spin_lock_irqsave(&s->lock, flags);
	es1371_update_ptr(s);
	if (file->f_mode & FMODE_READ) {
			if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
				mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac2.mapped) {
			if (s->dma_dac2.count >= (signed)s->dma_dac2.fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)s->dma_dac2.dmasize >= s->dma_dac2.count + (signed)s->dma_dac2.fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int es1371_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	struct dmabuf *db;
	int ret;
	unsigned long size;

	VALIDATE_STATE(s);
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf_dac2(s)) != 0)
			return ret;
		db = &s->dma_dac2;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf_adc(s)) != 0)
			return ret;
		db = &s->dma_adc;
	} else 
		return -EINVAL;
	if (vma->vm_offset != 0)
		return -EINVAL;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << db->buforder))
		return -EINVAL;
	if (remap_page_range(vma->vm_start, virt_to_phys(db->rawbuf), size, vma->vm_page_prot))
		return -EAGAIN;
	db->mapped = 1;
	vma->vm_file = file;
	file->f_count++;
	return 0;
}

static int es1371_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int val, mapped, ret;

	VALIDATE_STATE(s);
        mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac2.mapped) ||
		((file->f_mode & FMODE_READ) && s->dma_adc.mapped);
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac2(s, 0/*file->f_flags & O_NONBLOCK*/);
		return 0;
		
	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP, (int *)arg);
		
        case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			stop_dac2(s);
			synchronize_irq();
			s->dma_dac2.swptr = s->dma_dac2.hwptr = s->dma_dac2.count = s->dma_dac2.total_bytes = 0;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			synchronize_irq();
			s->dma_adc.swptr = s->dma_adc.hwptr = s->dma_adc.count = s->dma_adc.total_bytes = 0;
		}
		return 0;

        case SNDCTL_DSP_SPEED:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val >= 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				set_adc_rate(s, val);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac2(s);
				s->dma_dac2.ready = 0;
				set_dac2_rate(s, val);
			}
		}
		return put_user((file->f_mode & FMODE_READ) ? s->adcrate : s->dac2rate, (int *)arg);

        case SNDCTL_DSP_STEREO:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.ready = 0;
			spin_lock_irqsave(&s->lock, flags);
			if (val)
				s->sctrl |= SCTRL_R1SMB;
			else
				s->sctrl &= ~SCTRL_R1SMB;
			outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac2(s);
			s->dma_dac2.ready = 0;
			spin_lock_irqsave(&s->lock, flags);
			if (val)
				s->sctrl |= SCTRL_P2SMB;
			else
				s->sctrl &= ~SCTRL_P2SMB;
			outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
                }
		return 0;

        case SNDCTL_DSP_CHANNELS:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				spin_lock_irqsave(&s->lock, flags);
				if (val >= 2)
					s->sctrl |= SCTRL_R1SMB;
				else
					s->sctrl &= ~SCTRL_R1SMB;
				outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
				spin_unlock_irqrestore(&s->lock, flags);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac2(s);
				s->dma_dac2.ready = 0;
				spin_lock_irqsave(&s->lock, flags);
				if (val >= 2)
					s->sctrl |= SCTRL_P2SMB;
				else
					s->sctrl &= ~SCTRL_P2SMB;
				outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
				spin_unlock_irqrestore(&s->lock, flags);
			}
		}
		return put_user((s->sctrl & ((file->f_mode & FMODE_READ) ? SCTRL_R1SMB : SCTRL_P2SMB)) ? 2 : 1, (int *)arg);
		
	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(AFMT_S16_LE|AFMT_U8, (int *)arg);
		
	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val != AFMT_QUERY) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				spin_lock_irqsave(&s->lock, flags);
				if (val == AFMT_S16_LE)
					s->sctrl |= SCTRL_R1SEB;
				else
					s->sctrl &= ~SCTRL_R1SEB;
				outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
				spin_unlock_irqrestore(&s->lock, flags);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac2(s);
				s->dma_dac2.ready = 0;
				spin_lock_irqsave(&s->lock, flags);
				if (val == AFMT_S16_LE)
					s->sctrl |= SCTRL_P2SEB;
				else
					s->sctrl &= ~SCTRL_P2SEB;
				outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
				spin_unlock_irqrestore(&s->lock, flags);
			}
		}
		return put_user((s->sctrl & ((file->f_mode & FMODE_READ) ? SCTRL_R1SEB : SCTRL_P2SEB)) ? 
				AFMT_S16_LE : AFMT_U8, (int *)arg);
		
	case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if (file->f_mode & FMODE_READ && s->ctrl & CTRL_ADC_EN) 
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && s->ctrl & CTRL_DAC2_EN) 
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, (int *)arg);
		
	case SNDCTL_DSP_SETTRIGGER:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!s->dma_adc.ready && (ret = prog_dmabuf_adc(s)))
					return ret;
				start_adc(s);
			} else
				stop_adc(s);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!s->dma_dac2.ready && (ret = prog_dmabuf_dac2(s)))
					return ret;
				start_dac2(s);
			} else
				stop_dac2(s);
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!(s->ctrl & CTRL_DAC2_EN) && (val = prog_dmabuf_dac2(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1371_update_ptr(s);
		abinfo.fragsize = s->dma_dac2.fragsize;
                abinfo.bytes = s->dma_dac2.dmasize - s->dma_dac2.count;
                abinfo.fragstotal = s->dma_dac2.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_dac2.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!(s->ctrl & CTRL_ADC_EN) && (val = prog_dmabuf_adc(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1371_update_ptr(s);
		abinfo.fragsize = s->dma_adc.fragsize;
                abinfo.bytes = s->dma_adc.count;
                abinfo.fragstotal = s->dma_adc.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_adc.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
		
        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		es1371_update_ptr(s);
                val = s->dma_dac2.count;
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(val, (int *)arg);

        case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		es1371_update_ptr(s);
                cinfo.bytes = s->dma_adc.total_bytes;
                cinfo.blocks = s->dma_adc.total_bytes >> s->dma_adc.fragshift;
                cinfo.ptr = s->dma_adc.hwptr;
		if (s->dma_adc.mapped)
			s->dma_adc.count &= s->dma_adc.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

        case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		es1371_update_ptr(s);
                cinfo.bytes = s->dma_dac2.total_bytes;
                cinfo.blocks = s->dma_dac2.total_bytes >> s->dma_dac2.fragshift;
                cinfo.ptr = s->dma_dac2.hwptr;
		if (s->dma_dac2.mapped)
			s->dma_dac2.count &= s->dma_dac2.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

        case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf_dac2(s)))
				return val;
			return put_user(s->dma_dac2.fragsize, (int *)arg);
		}
		if ((val = prog_dmabuf_adc(s)))
			return val;
		return put_user(s->dma_adc.fragsize, (int *)arg);

        case SNDCTL_DSP_SETFRAGMENT:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (file->f_mode & FMODE_READ) {
			s->dma_adc.ossfragshift = val & 0xffff;
			s->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_adc.ossfragshift < 4)
				s->dma_adc.ossfragshift = 4;
			if (s->dma_adc.ossfragshift > 15)
				s->dma_adc.ossfragshift = 15;
			if (s->dma_adc.ossmaxfrags < 4)
				s->dma_adc.ossmaxfrags = 4;
		}
		if (file->f_mode & FMODE_WRITE) {
			s->dma_dac2.ossfragshift = val & 0xffff;
			s->dma_dac2.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_dac2.ossfragshift < 4)
				s->dma_dac2.ossfragshift = 4;
			if (s->dma_dac2.ossfragshift > 15)
				s->dma_dac2.ossfragshift = 15;
			if (s->dma_dac2.ossmaxfrags < 4)
				s->dma_dac2.ossmaxfrags = 4;
		}
		return 0;

        case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) ||
		    (file->f_mode & FMODE_WRITE && s->dma_dac2.subdivision))
			return -EINVAL;
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ)
			s->dma_adc.subdivision = val;
		if (file->f_mode & FMODE_WRITE)
			s->dma_dac2.subdivision = val;
		return 0;

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_RATE:
        case SOUND_PCM_READ_CHANNELS:
        case SOUND_PCM_READ_BITS:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}
	return mixer_ioctl(s, cmd, arg);
}

static int es1371_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct es1371_state *s = devs;
	unsigned long flags;

	while (s && ((s->dev_audio ^ minor) & ~0xf))
		s = s->next;
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & file->f_mode) {
		if (file->f_flags & O_NONBLOCK) {
			up(&s->open_sem);
			return -EBUSY;
		}
		up(&s->open_sem);
		interruptible_sleep_on(&s->open_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}
	if (file->f_mode & FMODE_READ) {
		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
		set_adc_rate(s, 8000);
	}
	if (file->f_mode & FMODE_WRITE) {
		s->dma_dac2.ossfragshift = s->dma_dac2.ossmaxfrags = s->dma_dac2.subdivision = 0;
		set_dac2_rate(s, 8000);
	}
	spin_lock_irqsave(&s->lock, flags);
	if (file->f_mode & FMODE_READ) {
		s->sctrl &= ~SCTRL_R1FMT;
		if ((minor & 0xf) == SND_DEV_DSP16)
			s->sctrl |= ES1371_FMT_S16_MONO << SCTRL_SH_R1FMT;
		else
			s->sctrl |= ES1371_FMT_U8_MONO << SCTRL_SH_R1FMT;
	}
	if (file->f_mode & FMODE_WRITE) {
		s->sctrl &= ~SCTRL_P2FMT;
		if ((minor & 0xf) == SND_DEV_DSP16)
			s->sctrl |= ES1371_FMT_S16_MONO << SCTRL_SH_P2FMT;
		else
			s->sctrl |= ES1371_FMT_U8_MONO << SCTRL_SH_P2FMT;
	}
	outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	up(&s->open_sem);
	MOD_INC_USE_COUNT;
	return 0;
}

static int es1371_release(struct inode *inode, struct file *file)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		drain_dac2(s, file->f_flags & O_NONBLOCK);
	down(&s->open_sem);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac2(s);
		dealloc_dmabuf(&s->dma_dac2);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
		dealloc_dmabuf(&s->dma_adc);
	}
	s->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);
	up(&s->open_sem);
	wake_up(&s->open_wait);
	MOD_DEC_USE_COUNT;
	return 0;
}

static /*const*/ struct file_operations es1371_audio_fops = {
	&es1371_llseek,
	&es1371_read,
	&es1371_write,
	NULL,  /* readdir */
	&es1371_poll,
	&es1371_ioctl,
	&es1371_mmap,
	&es1371_open,
	NULL,	/* flush */
	&es1371_release,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL,  /* lock */
};

/* --------------------------------------------------------------------- */

static ssize_t es1371_write_dac(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	ssize_t ret = 0;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_dac1.mapped)
		return -ENXIO;
	if (!s->dma_dac1.ready && (ret = prog_dmabuf_dac1(s)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		if (s->dma_dac1.count < 0) {
			s->dma_dac1.count = 0;
			s->dma_dac1.swptr = s->dma_dac1.hwptr;
		}
		swptr = s->dma_dac1.swptr;
		cnt = s->dma_dac1.dmasize-swptr;
		if (s->dma_dac1.count + cnt > s->dma_dac1.dmasize)
			cnt = s->dma_dac1.dmasize - s->dma_dac1.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			start_dac1(s);
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EBUSY;
			interruptible_sleep_on(&s->dma_dac1.wait);
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_from_user(s->dma_dac1.rawbuf + swptr, buffer, cnt))
			return ret ? ret : -EFAULT;
		swptr = (swptr + cnt) % s->dma_dac1.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_dac1.swptr = swptr;
		s->dma_dac1.count += cnt;
		s->dma_dac1.endcleared = 0;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac1(s);
	}
	return ret;
}

static unsigned int es1371_poll_dac(struct file *file, struct poll_table_struct *wait)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	poll_wait(file, &s->dma_dac1.wait, wait);
	spin_lock_irqsave(&s->lock, flags);
	es1371_update_ptr(s);
	if (s->dma_dac1.mapped) {
		if (s->dma_dac1.count >= (signed)s->dma_dac1.fragsize)
			mask |= POLLOUT | POLLWRNORM;
	} else {
		if ((signed)s->dma_dac1.dmasize >= s->dma_dac1.count + (signed)s->dma_dac1.fragsize)
			mask |= POLLOUT | POLLWRNORM;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int es1371_mmap_dac(struct file *file, struct vm_area_struct *vma)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	int ret;
	unsigned long size;

	VALIDATE_STATE(s);
	if (!(vma->vm_flags & VM_WRITE))
		return -EINVAL;
	if ((ret = prog_dmabuf_dac1(s)) != 0)
		return ret;
	if (vma->vm_offset != 0)
		return -EINVAL;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << s->dma_dac1.buforder))
		return -EINVAL;
	if (remap_page_range(vma->vm_start, virt_to_phys(s->dma_dac1.rawbuf), size, vma->vm_page_prot))
		return -EAGAIN;
	s->dma_dac1.mapped = 1;
	vma->vm_file = file;
	file->f_count++;
	return 0;
}

static int es1371_ioctl_dac(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int val, ret;

	VALIDATE_STATE(s);
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_SYNC:
		return drain_dac1(s, 0/*file->f_flags & O_NONBLOCK*/);
		
	case SNDCTL_DSP_SETDUPLEX:
		return -EINVAL;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP, (int *)arg);
		
        case SNDCTL_DSP_RESET:
		stop_dac1(s);
		synchronize_irq();
		s->dma_dac1.swptr = s->dma_dac1.hwptr = s->dma_dac1.count = s->dma_dac1.total_bytes = 0;
		return 0;

        case SNDCTL_DSP_SPEED:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val >= 0) {
			stop_dac1(s);
			s->dma_dac1.ready = 0;
			set_dac1_rate(s, val);
		}
		return put_user(s->dac1rate, (int *)arg);

        case SNDCTL_DSP_STEREO:
		get_user_ret(val, (int *)arg, -EFAULT);
		stop_dac1(s);
		s->dma_dac1.ready = 0;
		spin_lock_irqsave(&s->lock, flags);
		if (val)
			s->sctrl |= SCTRL_P1SMB;
		else
			s->sctrl &= ~SCTRL_P1SMB;
		outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
		spin_unlock_irqrestore(&s->lock, flags);
		return 0;

        case SNDCTL_DSP_CHANNELS:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 0) {
			stop_dac1(s);
			s->dma_dac1.ready = 0;
			spin_lock_irqsave(&s->lock, flags);
			if (val >= 2)
				s->sctrl |= SCTRL_P1SMB;
			else
				s->sctrl &= ~SCTRL_P1SMB;
			outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
		}
		return put_user((s->sctrl & SCTRL_P1SMB) ? 2 : 1, (int *)arg);
		
        case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(AFMT_S16_LE|AFMT_U8, (int *)arg);
		
        case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val != AFMT_QUERY) {
			stop_dac1(s);
			s->dma_dac1.ready = 0;
			spin_lock_irqsave(&s->lock, flags);
			if (val == AFMT_S16_LE)
				s->sctrl |= SCTRL_P1SEB;
			else
				s->sctrl &= ~SCTRL_P1SEB;
			outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
		}
		return put_user((s->sctrl & SCTRL_P1SEB) ? AFMT_S16_LE : AFMT_U8, (int *)arg);

        case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
		return put_user((s->ctrl & CTRL_DAC1_EN) ? PCM_ENABLE_OUTPUT : 0, (int *)arg);
						
	case SNDCTL_DSP_SETTRIGGER:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val & PCM_ENABLE_OUTPUT) {
			if (!s->dma_dac1.ready && (ret = prog_dmabuf_dac1(s)))
				return ret;
			start_dac1(s);
		} else
			stop_dac1(s);
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(s->ctrl & CTRL_DAC2_EN) && (val = prog_dmabuf_dac1(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1371_update_ptr(s);
		abinfo.fragsize = s->dma_dac1.fragsize;
                abinfo.bytes = s->dma_dac1.dmasize - s->dma_dac1.count;
                abinfo.fragstotal = s->dma_dac1.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_dac1.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_GETODELAY:
		spin_lock_irqsave(&s->lock, flags);
		es1371_update_ptr(s);
                val = s->dma_dac1.count;
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(val, (int *)arg);

        case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		es1371_update_ptr(s);
                cinfo.bytes = s->dma_dac1.total_bytes;
                cinfo.blocks = s->dma_dac1.total_bytes >> s->dma_dac1.fragshift;
                cinfo.ptr = s->dma_dac1.hwptr;
		if (s->dma_dac1.mapped)
			s->dma_dac1.count &= s->dma_dac1.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

        case SNDCTL_DSP_GETBLKSIZE:
		if ((val = prog_dmabuf_dac1(s)))
			return val;
                return put_user(s->dma_dac1.fragsize, (int *)arg);

        case SNDCTL_DSP_SETFRAGMENT:
                get_user_ret(val, (int *)arg, -EFAULT);
		s->dma_dac1.ossfragshift = val & 0xffff;
		s->dma_dac1.ossmaxfrags = (val >> 16) & 0xffff;
		if (s->dma_dac1.ossfragshift < 4)
			s->dma_dac1.ossfragshift = 4;
		if (s->dma_dac1.ossfragshift > 15)
			s->dma_dac1.ossfragshift = 15;
		if (s->dma_dac1.ossmaxfrags < 4)
			s->dma_dac1.ossmaxfrags = 4;
		return 0;

        case SNDCTL_DSP_SUBDIVIDE:
		if (s->dma_dac1.subdivision)
			return -EINVAL;
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		s->dma_dac1.subdivision = val;
		return 0;

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_RATE:
        case SOUND_PCM_READ_CHANNELS:
        case SOUND_PCM_READ_BITS:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}
	return mixer_ioctl(s, cmd, arg);
}

static int es1371_open_dac(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct es1371_state *s = devs;
	unsigned long flags;

	while (s && ((s->dev_dac ^ minor) & ~0xf))
		s = s->next;
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
       	/* we allow opening with O_RDWR, most programs do it although they will only write */
#if 0
	if (file->f_mode & FMODE_READ)
		return -EPERM;
#endif
	if (!(file->f_mode & FMODE_WRITE))
		return -EINVAL;
       	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & FMODE_DAC) {
		if (file->f_flags & O_NONBLOCK) {
			up(&s->open_sem);
			return -EBUSY;
		}
		up(&s->open_sem);
		interruptible_sleep_on(&s->open_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}
	s->dma_dac1.ossfragshift = s->dma_dac1.ossmaxfrags = s->dma_dac1.subdivision = 0;
	set_dac1_rate(s, 8000);
	spin_lock_irqsave(&s->lock, flags);
	s->sctrl &= ~SCTRL_P1FMT;
	if ((minor & 0xf) == SND_DEV_DSP16)
		s->sctrl |= ES1371_FMT_S16_MONO << SCTRL_SH_P1FMT;
	else
		s->sctrl |= ES1371_FMT_U8_MONO << SCTRL_SH_P1FMT;
	outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= FMODE_DAC;
	up(&s->open_sem);
	MOD_INC_USE_COUNT;
	return 0;
}

static int es1371_release_dac(struct inode *inode, struct file *file)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;

	VALIDATE_STATE(s);
	drain_dac1(s, file->f_flags & O_NONBLOCK);
	down(&s->open_sem);
	stop_dac1(s);
	dealloc_dmabuf(&s->dma_dac1);
	s->open_mode &= ~FMODE_DAC;
	up(&s->open_sem);
	wake_up(&s->open_wait);
	MOD_DEC_USE_COUNT;
	return 0;
}

static /*const*/ struct file_operations es1371_dac_fops = {
	&es1371_llseek,
	NULL,  /* read */
	&es1371_write_dac,
	NULL,  /* readdir */
	&es1371_poll_dac,
	&es1371_ioctl_dac,
	&es1371_mmap_dac,
	&es1371_open_dac,
	NULL,	/* flush */
	&es1371_release_dac,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL,  /* lock */
};

/* --------------------------------------------------------------------- */

static ssize_t es1371_midi_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned ptr;
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		ptr = s->midi.ird;
		cnt = MIDIINBUF - ptr;
		if (s->midi.icnt < cnt)
			cnt = s->midi.icnt;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EBUSY;
			interruptible_sleep_on(&s->midi.iwait);
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_to_user(buffer, s->midi.ibuf + ptr, cnt))
			return ret ? ret : -EFAULT;
		ptr = (ptr + cnt) % MIDIINBUF;
		spin_lock_irqsave(&s->lock, flags);
		s->midi.ird = ptr;
		s->midi.icnt -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
	}
	return ret;
}

static ssize_t es1371_midi_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned ptr;
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		ptr = s->midi.owr;
		cnt = MIDIOUTBUF - ptr;
		if (s->midi.ocnt + cnt > MIDIOUTBUF)
			cnt = MIDIOUTBUF - s->midi.ocnt;
		if (cnt <= 0)
			es1371_handle_midi(s);
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EBUSY;
			interruptible_sleep_on(&s->midi.owait);
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_from_user(s->midi.obuf + ptr, buffer, cnt))
			return ret ? ret : -EFAULT;
		ptr = (ptr + cnt) % MIDIOUTBUF;
		spin_lock_irqsave(&s->lock, flags);
		s->midi.owr = ptr;
		s->midi.ocnt += cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		spin_lock_irqsave(&s->lock, flags);
		es1371_handle_midi(s);
		spin_unlock_irqrestore(&s->lock, flags);
	}
	return ret;
}

static unsigned int es1371_midi_poll(struct file *file, struct poll_table_struct *wait)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &s->midi.owait, wait);
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &s->midi.iwait, wait);
	spin_lock_irqsave(&s->lock, flags);
	if (file->f_mode & FMODE_READ) {
		if (s->midi.icnt > 0)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->midi.ocnt < MIDIOUTBUF)
			mask |= POLLOUT | POLLWRNORM;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int es1371_midi_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct es1371_state *s = devs;
	unsigned long flags;

	while (s && s->dev_midi != minor)
		s = s->next;
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & (file->f_mode << FMODE_MIDI_SHIFT)) {
		if (file->f_flags & O_NONBLOCK) {
			up(&s->open_sem);
			return -EBUSY;
		}
		up(&s->open_sem);
		interruptible_sleep_on(&s->open_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
		outb(UCTRL_CNTRL_SWR, s->io+ES1371_REG_UART_CONTROL);
		outb(0, s->io+ES1371_REG_UART_CONTROL);
		outb(0, s->io+ES1371_REG_UART_TEST);
	}
	if (file->f_mode & FMODE_READ) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
	}
	if (file->f_mode & FMODE_WRITE) {
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
	}
	s->ctrl |= CTRL_UART_EN;
	outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	es1371_handle_midi(s);
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= (file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ | FMODE_MIDI_WRITE);
	up(&s->open_sem);
	MOD_INC_USE_COUNT;
	return 0;
}

static int es1371_midi_release(struct inode *inode, struct file *file)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
        struct wait_queue wait = { current, NULL };
	unsigned long flags;
	unsigned count, tmo;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE) {
		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue(&s->midi.owait, &wait);
		for (;;) {
			spin_lock_irqsave(&s->lock, flags);
			count = s->midi.ocnt;
			spin_unlock_irqrestore(&s->lock, flags);
			if (count <= 0)
				break;
			if (signal_pending(current))
				break;
			if (file->f_flags & O_NONBLOCK) {
				remove_wait_queue(&s->midi.owait, &wait);
				current->state = TASK_RUNNING;
				return -EBUSY;
			}
			tmo = (count * HZ) / 3100;
			if (!schedule_timeout(tmo ? : 1) && tmo)
				printk(KERN_DEBUG "es1371: midi timed out??\n");
		}
		remove_wait_queue(&s->midi.owait, &wait);
		current->state = TASK_RUNNING;
	}
	down(&s->open_sem);
	s->open_mode &= (~(file->f_mode << FMODE_MIDI_SHIFT)) & (FMODE_MIDI_READ|FMODE_MIDI_WRITE);
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		s->ctrl &= ~CTRL_UART_EN;
		outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	}
	spin_unlock_irqrestore(&s->lock, flags);
	up(&s->open_sem);
	wake_up(&s->open_wait);
	MOD_DEC_USE_COUNT;
	return 0;
}

static /*const*/ struct file_operations es1371_midi_fops = {
	&es1371_llseek,
	&es1371_midi_read,
	&es1371_midi_write,
	NULL,  /* readdir */
	&es1371_midi_poll,
	NULL,  /* ioctl */
	NULL,  /* mmap */
	&es1371_midi_open,
	NULL,	/* flush */
	&es1371_midi_release,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL,  /* lock */
};

/* --------------------------------------------------------------------- */

/* maximum number of devices */
#define NR_DEVICE 5

#if CONFIG_SOUND_ES1371_JOYPORT_BOOT
static int joystick[NR_DEVICE] = { 
CONFIG_SOUND_ES1371_GAMEPORT
, 0, };
#else
static int joystick[NR_DEVICE] = { 0, };
#endif

/* --------------------------------------------------------------------- */

static struct initvol {
	int mixch;
	int vol;
} initvol[] __initdata = {
	{ SOUND_MIXER_WRITE_LINE, 0x4040 },
	{ SOUND_MIXER_WRITE_CD, 0x4040 },
	{ MIXER_WRITE(SOUND_MIXER_VIDEO), 0x4040 },
	{ SOUND_MIXER_WRITE_LINE1, 0x4040 },
	{ SOUND_MIXER_WRITE_PCM, 0x4040 },
	{ SOUND_MIXER_WRITE_VOLUME, 0x4040 },
	{ MIXER_WRITE(SOUND_MIXER_PHONEOUT), 0x4040 },
	{ SOUND_MIXER_WRITE_OGAIN, 0x4040 },
	{ MIXER_WRITE(SOUND_MIXER_PHONEIN), 0x4040 },
	{ SOUND_MIXER_WRITE_SPEAKER, 0x4040 },
	{ SOUND_MIXER_WRITE_MIC, 0x4040 },
	{ SOUND_MIXER_WRITE_RECLEV, 0x4040 },
	{ SOUND_MIXER_WRITE_IGAIN, 0x4040 }
};

#ifdef MODULE
__initfunc(int init_module(void))
#else
__initfunc(int init_es1371(void))
#endif
{
	struct es1371_state *s;
	struct pci_dev *pcidev = NULL;
	mm_segment_t fs;
	int i, val, val2, index = 0;

	if (!pci_present())   /* No PCI bus in this machine! */
		return -ENODEV;
	printk(KERN_INFO "es1371: version v0.8 time " __TIME__ " " __DATE__ "\n");
	while (index < NR_DEVICE && 
	       (pcidev = pci_find_device(PCI_VENDOR_ID_ENSONIQ, PCI_DEVICE_ID_ENSONIQ_ES1371, pcidev))) {
		if (pcidev->base_address[0] == 0 || 
		    (pcidev->base_address[0] & PCI_BASE_ADDRESS_SPACE) != PCI_BASE_ADDRESS_SPACE_IO)
			continue;
		if (pcidev->irq == 0) 
			continue;
		if (!(s = kmalloc(sizeof(struct es1371_state), GFP_KERNEL))) {
			printk(KERN_WARNING "es1371: out of memory\n");
			continue;
		}
		memset(s, 0, sizeof(struct es1371_state));
		init_waitqueue(&s->dma_adc.wait);
		init_waitqueue(&s->dma_dac1.wait);
		init_waitqueue(&s->dma_dac2.wait);
		init_waitqueue(&s->open_wait);
		init_waitqueue(&s->midi.iwait);
		init_waitqueue(&s->midi.owait);
		s->open_sem = MUTEX;
		s->magic = ES1371_MAGIC;
		s->io = pcidev->base_address[0] & PCI_BASE_ADDRESS_IO_MASK;
		s->irq = pcidev->irq;
		if (check_region(s->io, ES1371_EXTENT)) {
			printk(KERN_ERR "es1371: io ports %#x-%#x in use\n", s->io, s->io+ES1371_EXTENT-1);
			goto err_region;
		}
		request_region(s->io, ES1371_EXTENT, "es1371");
		if (request_irq(s->irq, es1371_interrupt, SA_SHIRQ, "es1371", s)) {
			printk(KERN_ERR "es1371: irq %u in use\n", s->irq);
			goto err_irq;
		}
		printk(KERN_INFO "es1371: found adapter at io %#06x irq %u\n"
		       KERN_INFO "es1371: features: joystick 0x%x\n", s->io, s->irq, joystick[index]);
		/* register devices */
		if ((s->dev_audio = register_sound_dsp(&es1371_audio_fops, -1)) < 0)
			goto err_dev1;
		if ((s->dev_mixer = register_sound_mixer(&es1371_mixer_fops, -1)) < 0)
			goto err_dev2;
		if ((s->dev_dac = register_sound_dsp(&es1371_dac_fops, -1)) < 0)
			goto err_dev3;
		if ((s->dev_midi = register_sound_midi(&es1371_midi_fops, -1)) < 0)
			goto err_dev4;
		/* initialize codec registers */
		s->ctrl = 0;
		if ((joystick[index] & ~0x18) == 0x200) {
			if (check_region(joystick[index], JOY_EXTENT))
				printk(KERN_ERR "es1371: joystick address 0x%x already in use\n", joystick[index]);
			else {
				s->ctrl |= CTRL_JYSTK_EN | (((joystick[index] >> 3) & CTRL_JOY_MASK) << CTRL_JOY_SHIFT);
			}
		}
		s->sctrl = 0;
		/* initialize the chips */
		outl(s->ctrl, s->io+ES1371_REG_CONTROL);
		outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
		outl(0, s->io+ES1371_REG_LEGACY);
		/* AC97 warm reset to start the bitclk */
		outl(s->ctrl | CTRL_SYNCRES, s->io+ES1371_REG_CONTROL);
		udelay(2);
		outl(s->ctrl, s->io+ES1371_REG_CONTROL);
		/* init the sample rate converter */
		outl(SRC_DIS, s->io + ES1371_REG_SRCONV);
		for (val = 0; val < 0x80; val++)
			src_write(s, val, 0);
		src_write(s, SRCREG_DAC1+SRCREG_TRUNC_N, 16 << 4);
		src_write(s, SRCREG_DAC1+SRCREG_INT_REGS, 16 << 10);
		src_write(s, SRCREG_DAC2+SRCREG_TRUNC_N, 16 << 4);
		src_write(s, SRCREG_DAC2+SRCREG_INT_REGS, 16 << 10);
		src_write(s, SRCREG_VOL_ADC, 1 << 12);
		src_write(s, SRCREG_VOL_ADC+1, 1 << 12);
		src_write(s, SRCREG_VOL_DAC1, 1 << 12);
		src_write(s, SRCREG_VOL_DAC1+1, 1 << 12);
		src_write(s, SRCREG_VOL_DAC2, 1 << 12);
		src_write(s, SRCREG_VOL_DAC2+1, 1 << 12);
		set_adc_rate(s, 22050);
		set_dac1_rate(s, 22050);
		set_dac2_rate(s, 22050);
		/* WARNING:
		 * enabling the sample rate converter without properly programming
		 * its parameters causes the chip to lock up (the SRC busy bit will
		 * be stuck high, and I've found no way to rectify this other than
		 * power cycle)
		 */
		outl(0, s->io+ES1371_REG_SRCONV);
		/* codec init */
		wrcodec(s, 0x00, 0); /* reset codec */
		s->mix.codec_id = rdcodec(s, 0x00);  /* get codec ID */
		val = rdcodec(s, 0x7c);
		val2 = rdcodec(s, 0x7e);
		printk(KERN_INFO "es1371: codec vendor %c%c%c revision %d\n", 
		       (val >> 8) & 0xff, val & 0xff, (val2 >> 8) & 0xff, val2 & 0xff);
		printk(KERN_INFO "es1371: codec features");
		if (s->mix.codec_id & CODEC_ID_DEDICATEDMIC)
			printk(" dedicated MIC PCM in");
		if (s->mix.codec_id & CODEC_ID_MODEMCODEC)
			printk(" Modem Line Codec");
		if (s->mix.codec_id & CODEC_ID_BASSTREBLE)
			printk(" Bass & Treble");
		if (s->mix.codec_id & CODEC_ID_SIMULATEDSTEREO)
			printk(" Simulated Stereo");
		if (s->mix.codec_id & CODEC_ID_HEADPHONEOUT)
			printk(" Headphone out");
		if (s->mix.codec_id & CODEC_ID_LOUDNESS)
			printk(" Loudness");
		if (s->mix.codec_id & CODEC_ID_18BITDAC)
			printk(" 18bit DAC");
		if (s->mix.codec_id & CODEC_ID_20BITDAC)
			printk(" 20bit DAC");
		if (s->mix.codec_id & CODEC_ID_18BITADC)
			printk(" 18bit ADC");
		if (s->mix.codec_id & CODEC_ID_20BITADC)
			printk(" 20bit ADC");
		printk("%s\n", (s->mix.codec_id & 0x3ff) ? "" : " none");
		val = (s->mix.codec_id >> CODEC_ID_SESHIFT) & CODEC_ID_SEMASK;
		printk(KERN_INFO "es1371: stereo enhancement: %s\n", (val <= 20) ? stereo_enhancement[val] : "unknown");
		fs = get_fs();
		set_fs(KERNEL_DS);
		val = SOUND_MASK_LINE;
		mixer_ioctl(s, SOUND_MIXER_WRITE_RECSRC, (unsigned long)&val);
		for (i = 0; i < sizeof(initvol)/sizeof(initvol[0]); i++) {
			val = initvol[i].vol;
			mixer_ioctl(s, initvol[i].mixch, (unsigned long)&val);
		}
		set_fs(fs);
		/* queue it for later freeing */
		s->next = devs;
		devs = s;
		index++;
		continue;

	err_dev4:
		unregister_sound_dsp(s->dev_dac);
	err_dev3:
		unregister_sound_mixer(s->dev_mixer);
	err_dev2:
		unregister_sound_dsp(s->dev_audio);
	err_dev1:
		printk(KERN_ERR "es1371: cannot register misc device\n");
		free_irq(s->irq, s);
	err_irq:
		release_region(s->io, ES1371_EXTENT);
	err_region:
		kfree_s(s, sizeof(struct es1371_state));
	}
	if (!devs)
		return -ENODEV;
	return 0;
}

/* --------------------------------------------------------------------- */

#ifdef MODULE

MODULE_PARM(joystick, "1-" __MODULE_STRING(NR_DEVICE) "i");
MODULE_PARM_DESC(joystick, "sets address and enables joystick interface (still need separate driver)");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("ES1371 AudioPCI97 Driver");

void cleanup_module(void)
{
	struct es1371_state *s;

	while ((s = devs)) {
		devs = devs->next;
		outl(0, s->io+ES1371_REG_CONTROL); /* switch everything off */
		outl(0, s->io+ES1371_REG_SERIAL_CONTROL); /* clear serial interrupts */
		synchronize_irq();
		free_irq(s->irq, s);
		release_region(s->io, ES1371_EXTENT);
		unregister_sound_dsp(s->dev_audio);
		unregister_sound_mixer(s->dev_mixer);
		unregister_sound_dsp(s->dev_dac);
		unregister_sound_midi(s->dev_midi);
		kfree_s(s, sizeof(struct es1371_state));
	}
	printk(KERN_INFO "es1371: unloading\n");
}

#endif /* MODULE */
