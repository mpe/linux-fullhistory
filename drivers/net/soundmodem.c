/*****************************************************************************/

/*
 *	soundmodem.c  -- soundcard radio modem driver.
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
 *
 *  Command line options (insmod command line)
 * 
 *  hardware hardware type; 0=sbc, 1=wss, any other value invalid
 *  mode     mode type; 0=1200 baud AFSK, 1=9600 baud FSK, any other
 *           value invalid
 *  iobase   base address of the soundcard; common values are 0x220 for sbc,
 *           0x530 for wss
 *  irq      interrupt number; common values are 7 or 5 for sbc, 11 for wss
 *  dma      dma number; common values are 0 or 1
 * 
 *
 *  History:
 *   0.1  21.09.96  Started
 *        18.10.96  Changed to new user space access routines (copy_{to,from}_user)
 */

/*****************************************************************************/

#include <linux/module.h>

#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <limits.h>
#include <linux/hdlcdrv.h>
#include <linux/soundmodem.h>

/* --------------------------------------------------------------------- */

#define NR_PORTS 4

#define SM_DEBUG

#define ENABLE_SBC
#define ENABLE_WSS
#undef  ENABLE_WSSFDX

#define ENABLE_AFSK1200
#define ENABLE_FSK9600

/* --------------------------------------------------------------------- */

#include "sm_tables.h"

/* --------------------------------------------------------------------- */

static struct device sm_device[NR_PORTS];

static struct {
	int hardware, mode, iobase, irq, dma, seriobase, pariobase, midiiobase;
} sm_ports[NR_PORTS] = { 
{ SM_HARDWARE_INVALID, SM_MODE_INVALID, -1, 0, 0, -1, -1, -1 }, 
};

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
#define SBC_HISPEED            0x48
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

#define DMA_MODE_AUTOINIT      0x10

/* --------------------------------------------------------------------- */

#define WSS_CONFIG(iobase)       (iobase+0)
#define WSS_STATUS(iobase)       (iobase+3)
#define WSS_CODEC_IA(iobase)     (iobase+4)
#define WSS_CODEC_ID(iobase)     (iobase+5)
#define WSS_CODEC_STATUS(iobase) (iobase+6)
#define WSS_CODEC_DATA(iobase)   (iobase+7)

#define WSS_EXTENT   8

/* --------------------------------------------------------------------- */

#define UART_RBR(iobase) (iobase+0)
#define UART_THR(iobase) (iobase+0)
#define UART_IER(iobase) (iobase+1)
#define UART_IIR(iobase) (iobase+2)
#define UART_FCR(iobase) (iobase+2)
#define UART_LCR(iobase) (iobase+3)
#define UART_MCR(iobase) (iobase+4)
#define UART_LSR(iobase) (iobase+5)
#define UART_MSR(iobase) (iobase+6)
#define UART_SCR(iobase) (iobase+7)
#define UART_DLL(iobase) (iobase+0)
#define UART_DLM(iobase) (iobase+1)

#define SER_EXTENT 8

#define LPT_DATA(iobase)    (iobase+0)
#define LPT_STATUS(iobase)  (iobase+1)
#define LPT_CONTROL(iobase) (iobase+2)
#define LPT_IRQ_ENABLE      0x10

#define LPT_EXTENT 3

#define MIDI_DATA(iobase)     (iobase)
#define MIDI_STATUS(iobase)   (iobase+1)
#define MIDI_READ_FULL 0x80   /* attention: negative logic!! */
#define MIDI_WRITE_EMPTY 0x40 /* attention: negative logic!! */

#define MIDI_EXTENT 2

/* ---------------------------------------------------------------------- */

#define PARAM_TXDELAY   1
#define PARAM_PERSIST   2
#define PARAM_SLOTTIME  3
#define PARAM_TXTAIL    4
#define PARAM_FULLDUP   5
#define PARAM_HARDWARE  6
#define PARAM_RETURN    255

#define SP_SER  1
#define SP_PAR  2
#define SP_MIDI 4

/* ---------------------------------------------------------------------- */
/*
 * Information that need to be kept for each board. 
 */

struct sm_state {
	struct hdlcdrv_state hdrv;

	struct config {
		int hardware;
		int mode;
	} config;

	struct modem_state {
		unsigned char revhi, revlo;
		
		unsigned int shreg;
		
		unsigned char last_sample;
		unsigned int bit_pll;
		unsigned int dcd_shreg;
		int dcd_sum0, dcd_sum1, dcd_sum2;
		unsigned int dcd_time;
		unsigned char last_rxbit;
		unsigned char tx_bit;
		
		signed char filt[9];
		
		unsigned long descram;
		unsigned long scram;
		
		unsigned char *dmabufr;
		unsigned char *dmabufw;
		unsigned char dmabufidx;
		unsigned char oldptt;
	} modem;
	
#define DIAGDATALEN 64
	struct diag_data {
		unsigned int mode;
		unsigned int flags;
		volatile int ptr;
		short data[DIAGDATALEN];
	} diag;
	
#ifdef SM_DEBUG
	struct debug_vals {
		unsigned long last_jiffies;
		unsigned cur_intcnt;
		unsigned last_intcnt;
		int cur_pllcorr;
		int last_pllcorr;
	} debug_vals;
#endif /* SM_DEBUG */
};

/* --------------------------------------------------------------------- */

struct modem_info {
	struct hdlcdrv_ops hops;
	unsigned int samplerate;
	unsigned char sbcmix;
	unsigned char sperbit;
	char *mode_name; /* used for request_{region,irq,dma} */
	/*
	 * low level chip informations
	 */
	unsigned char data_fmt;
	unsigned int dmabuflen;
	void (*modulator)(struct sm_state *, unsigned char *, int);
	void (*demodulator)(struct sm_state *, unsigned char *, int);
};

/* --------------------------------------------------------------------- */

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

/* --------------------------------------------------------------------- */

static void inline sm_int_freq(struct sm_state *sm)
{
#ifdef SM_DEBUG
	unsigned long cur_jiffies = jiffies;
	/* 
	 * measure the interrupt frequency
	 */
	sm->debug_vals.cur_intcnt++;
	if ((cur_jiffies - sm->debug_vals.last_jiffies) >= HZ) {
		sm->debug_vals.last_jiffies = cur_jiffies;
		sm->debug_vals.last_intcnt = sm->debug_vals.cur_intcnt;
		sm->debug_vals.cur_intcnt = 0;
	}
#endif /* SM_DEBUG */
}

/* --------------------------------------------------------------------- */
/*
 * ===================== port checking routines ========================
 */

static int inline check_lpt(unsigned int iobase)
{
	unsigned char b1,b2;
	int i;

	if (iobase <= 0 || iobase > 0x1000-LPT_EXTENT)
		return 0;
	if (check_region(iobase, LPT_EXTENT))
		return 0;
	b1 = inb(LPT_DATA(iobase));
	b2 = inb(LPT_CONTROL(iobase));
	outb(0xaa, LPT_DATA(iobase));
	i = inb(LPT_DATA(iobase)) == 0xaa;
	outb(0x55, LPT_DATA(iobase));
	i &= inb(LPT_DATA(iobase)) == 0x55;
	outb(0x0a, LPT_CONTROL(iobase));
	i &= (inb(LPT_CONTROL(iobase)) & 0xf) == 0x0a;
	outb(0x05, LPT_CONTROL(iobase));
	i &= (inb(LPT_CONTROL(iobase)) & 0xf) == 0x05;
	outb(b1, LPT_DATA(iobase));
	outb(b2, LPT_CONTROL(iobase));
	return !i;
}

/* --------------------------------------------------------------------- */

enum uart { c_uart_unknown, c_uart_8250,
	c_uart_16450, c_uart_16550, c_uart_16550A};
static const char *uart_str[] =
	{ "unknown", "8250", "16450", "16550", "16550A" };

static enum uart inline check_uart(unsigned int iobase)
{
	unsigned char b1,b2,b3;
	enum uart u;
	enum uart uart_tab[] =
		{ c_uart_16450, c_uart_unknown, c_uart_16550, c_uart_16550A };

	if (iobase <= 0 || iobase > 0x1000-SER_EXTENT)
		return c_uart_unknown;
	if (check_region(iobase, SER_EXTENT))
		return c_uart_unknown;
	b1 = inb(UART_MCR(iobase));
	outb(b1 | 0x10, UART_MCR(iobase));	/* loopback mode */
	b2 = inb(UART_MSR(iobase));
	outb(0x1a, UART_MCR(iobase));
	b3 = inb(UART_MSR(iobase)) & 0xf0;
	outb(b1, UART_MCR(iobase));	   /* restore old values */
	outb(b2, UART_MSR(iobase));
	if (b3 != 0x90) 
		return c_uart_unknown;
	inb(UART_RBR(iobase));
	inb(UART_RBR(iobase));
	outb(0x01, UART_FCR(iobase));		/* enable FIFOs */
	u = uart_tab[(inb(UART_IIR(iobase)) >> 6) & 3];
	if (u == c_uart_16450) {
		outb(0x5a, UART_SCR(iobase));
		b1 = inb(UART_SCR(iobase));
		outb(0xa5, UART_SCR(iobase));
		b2 = inb(UART_SCR(iobase));
		if ((b1 != 0x5a) || (b2 != 0xa5)) 
			u = c_uart_8250;
	}
	return u;
}

/* --------------------------------------------------------------------- */

static int inline check_midi(unsigned int iobase)
{
	unsigned long timeout;
	unsigned long flags;
	unsigned char b;

	if (iobase <= 0 || iobase > 0x1000-MIDI_EXTENT)
		return 0;
	if (check_region(iobase, MIDI_EXTENT))
		return 0;
	timeout = jiffies + (HZ / 100);
	while (inb(MIDI_STATUS(iobase)) & MIDI_WRITE_EMPTY)
		if ((signed)(jiffies - timeout) > 0)
			return 0;
	save_flags(flags);
	cli();
	outb(0xff, MIDI_DATA(iobase));
	b = inb(MIDI_STATUS(iobase));
	restore_flags(flags);
	if (!(b & MIDI_WRITE_EMPTY))
		return 0;
	while (inb(MIDI_STATUS(iobase)) & MIDI_WRITE_EMPTY)
		if ((signed)(jiffies - timeout) > 0)
			return 0;
	return 1;
}

/* --------------------------------------------------------------------- */

static void inline output_status(struct sm_state *sm)
{
	int invert_dcd = 0;
	int invert_ptt = 0;

	int ptt = hdlcdrv_ptt(&sm->hdrv) ^ invert_ptt;
	int dcd = (!!sm->hdrv.hdlcrx.dcd) ^ invert_dcd;

	if (sm->hdrv.ptt_out.flags & SP_SER) {
		outb(dcd | (ptt << 1), UART_MCR(sm->hdrv.ptt_out.seriobase));
		outb(0x40 & (-ptt), UART_LCR(sm->hdrv.ptt_out.seriobase));
	}
	if (sm->hdrv.ptt_out.flags & SP_PAR) {
		outb(ptt | (dcd << 1), LPT_DATA(sm->hdrv.ptt_out.pariobase));
	}
	if (sm->hdrv.ptt_out.flags & SP_MIDI && hdlcdrv_ptt(&sm->hdrv)) {
		outb(0, MIDI_DATA(sm->hdrv.ptt_out.midiiobase));
	}
}

/* --------------------------------------------------------------------- */

static void output_open(struct sm_state *sm)
{
	enum uart u = c_uart_unknown;

	sm->hdrv.ptt_out.flags = 0;
	if (sm->hdrv.ptt_out.seriobase > 0 && 
	    sm->hdrv.ptt_out.seriobase <= 0x1000-SER_EXTENT &&
	    ((u = check_uart(sm->hdrv.ptt_out.seriobase))) != c_uart_unknown) {
		sm->hdrv.ptt_out.flags |= SP_SER;
		request_region(sm->hdrv.ptt_out.seriobase, SER_EXTENT, "sm ser ptt");
		outb(0, UART_IER(sm->hdrv.ptt_out.seriobase));
		/* 5 bits, 1 stop, no parity, no break, Div latch access */
		outb(0x80, UART_LCR(sm->hdrv.ptt_out.seriobase)); 
		outb(0, UART_DLM(sm->hdrv.ptt_out.seriobase));
		outb(1, UART_DLL(sm->hdrv.ptt_out.seriobase)); /* as fast as possible */
		/* LCR and MCR set by output_status */
	}
	if (sm->hdrv.ptt_out.pariobase > 0 && 
	    sm->hdrv.ptt_out.pariobase <= 0x1000-LPT_EXTENT &&
	    check_lpt(sm->hdrv.ptt_out.pariobase)) {
		sm->hdrv.ptt_out.flags |= SP_PAR;
		request_region(sm->hdrv.ptt_out.pariobase, LPT_EXTENT, "sm par ptt");
	}
	if (sm->hdrv.ptt_out.midiiobase > 0 && 
	    sm->hdrv.ptt_out.midiiobase <= 0x1000-MIDI_EXTENT &&
	    check_midi(sm->hdrv.ptt_out.midiiobase)) {
		sm->hdrv.ptt_out.flags |= SP_MIDI;
		request_region(sm->hdrv.ptt_out.midiiobase, MIDI_EXTENT, 
			       "sm midi ptt");
	}
	output_status(sm);

	printk(KERN_INFO "sm: ptt output:");
	if (sm->hdrv.ptt_out.flags & SP_SER)
		printk(" serial interface at 0x%x, uart %s", sm->hdrv.ptt_out.seriobase,
		       uart_str[u]);
	if (sm->hdrv.ptt_out.flags & SP_PAR)
		printk(" parallel interface at 0x%x", sm->hdrv.ptt_out.pariobase);
	if (sm->hdrv.ptt_out.flags & SP_MIDI)
		printk(" mpu401 (midi) interface at 0x%x", sm->hdrv.ptt_out.midiiobase);
	if (!sm->hdrv.ptt_out.flags)
		printk(" none");
	printk("\n");
}

/* --------------------------------------------------------------------- */

static void output_close(struct sm_state *sm)
{
	/* release regions used for PTT output */
	sm->hdrv.hdlctx.ptt = sm->hdrv.hdlctx.calibrate = 0;
	output_status(sm);
	if (sm->hdrv.ptt_out.flags & SP_SER)
		release_region(sm->hdrv.ptt_out.seriobase, SER_EXTENT);
       	if (sm->hdrv.ptt_out.flags & SP_PAR) 
		release_region(sm->hdrv.ptt_out.pariobase, LPT_EXTENT);
       	if (sm->hdrv.ptt_out.flags & SP_MIDI) 
		release_region(sm->hdrv.ptt_out.midiiobase, MIDI_EXTENT);
	sm->hdrv.ptt_out.flags = 0;
}

/* --------------------------------------------------------------------- */
/*
 * ===================== diagnostics stuff ===============================
 */

static inline void diag_trigger(struct sm_state *sm)
{
	if (sm->diag.ptr < 0) 
		if (!(sm->diag.flags & SM_DIAGFLAG_DCDGATE) || sm->hdrv.hdlcrx.dcd)
			sm->diag.ptr = 0;
}

/* --------------------------------------------------------------------- */

static inline void diag_add(struct sm_state *sm, int valinp, int valdemod)
{
	int val;

	if ((sm->diag.mode != SM_DIAGMODE_INPUT && 
	     sm->diag.mode != SM_DIAGMODE_DEMOD) || 
	    sm->diag.ptr >= DIAGDATALEN || sm->diag.ptr < 0)
		return;
	val = (sm->diag.mode == SM_DIAGMODE_DEMOD) ? valdemod : valinp;
	/* clip */
	if (val > SHRT_MAX)
		val = SHRT_MAX;
	if (val < SHRT_MIN)
		val = SHRT_MIN;
	sm->diag.data[sm->diag.ptr++] = val;
}

/* --------------------------------------------------------------------- */

static inline void diag_add_one(struct sm_state *sm, int val)
{
	if ((sm->diag.mode != SM_DIAGMODE_INPUT && 
	     sm->diag.mode != SM_DIAGMODE_DEMOD) || 
	    sm->diag.ptr >= DIAGDATALEN || sm->diag.ptr < 0)
		return;
	/* clip */
	if (val > SHRT_MAX)
		val = SHRT_MAX;
	if (val < SHRT_MIN)
		val = SHRT_MIN;
	sm->diag.data[sm->diag.ptr++] = val;
}

/* --------------------------------------------------------------------- */
/*
 * ===================== modem routines 1200 baud =========================
 */

static inline unsigned int hweight32(unsigned int w)
	__attribute__ ((unused));
static inline unsigned int hweight16(unsigned short w)
	__attribute__ ((unused));
static inline unsigned int hweight8(unsigned char w)
        __attribute__ ((unused));

static inline unsigned int hweight32(unsigned int w) 
{
        unsigned int res = (w & 0x55555555) + ((w >> 1) & 0x55555555);
        res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
        res = (res & 0x0F0F0F0F) + ((res >> 4) & 0x0F0F0F0F);
        res = (res & 0x00FF00FF) + ((res >> 8) & 0x00FF00FF);
        return (res & 0x0000FFFF) + ((res >> 16) & 0x0000FFFF);
}

static inline unsigned int hweight16(unsigned short w)
{
        unsigned short res = (w & 0x5555) + ((w >> 1) & 0x5555);
        res = (res & 0x3333) + ((res >> 2) & 0x3333);
        res = (res & 0x0F0F) + ((res >> 4) & 0x0F0F);
        return (res & 0x00FF) + ((res >> 8) & 0x00FF);
}

static inline unsigned int hweight8(unsigned char w)
{
        unsigned short res = (w & 0x55) + ((w >> 1) & 0x55);
        res = (res & 0x33) + ((res >> 2) & 0x33);
        return (res & 0x0F) + ((res >> 4) & 0x0F);
}

/* --------------------------------------------------------------------- */

#ifdef ENABLE_AFSK1200

static void modulator_1200(struct sm_state *sm, unsigned char *buf, int buflen)
{
	static const int dds_inc[2] = { 8192, 15019 };
	int j, k;

	for (; buflen >= 8; buflen -= 8) {
		if (sm->modem.shreg <= 1)
			sm->modem.shreg = hdlcdrv_getbits(&sm->hdrv) | 0x10000;
		sm->modem.tx_bit = (sm->modem.tx_bit ^ 
				    (!(sm->modem.shreg & 1))) & 1;
		sm->modem.shreg >>= 1;
		k = dds_inc[sm->modem.tx_bit & 1];
		for (j = 0; j < 8; j++) {
			*buf++ = sinetab[(sm->modem.bit_pll >> 10) & 0x3f];
			sm->modem.bit_pll += k;
		}
	}
}

/* --------------------------------------------------------------------- */

static void demodulator_1200(struct sm_state *sm, unsigned char *buf, int buflen)
{
	static const int pll_corr[2] = { -0x1000, 0x1000 };
	int j;
	signed char *fp;
	const signed char *coeffp;
	int sum1, sum2;
	unsigned char newsample;

	for (; buflen > 0; buflen--, buf++) {
		sm->modem.filt[8] = (*buf - 128);
		for (sum1 = j = 0, fp = sm->modem.filt+1, coeffp = tx_lo_i; 
		     j < 8; j++, fp++, coeffp++) {
			sum1 += (*coeffp) * (*fp);
			fp[-1] = fp[0];
		}
		sum1 >>= 7;
		sum2 = sum1 * sum1;
		for (sum1 = j = 0, fp = sm->modem.filt, coeffp = tx_lo_q; 
		     j < 8; j++, fp++, coeffp++)
			sum1 += (*coeffp) * (*fp);
		sum1 >>= 7;
		sum2 += sum1 * sum1;
		for (sum1 = j = 0, fp = sm->modem.filt, coeffp = tx_hi_i; 
		     j < 8; j++, fp++, coeffp++)
			sum1 += (*coeffp) * (*fp);
		sum1 >>= 7;
		sum2 -= sum1 * sum1;
		for (sum1 = j = 0, fp = sm->modem.filt, coeffp = tx_hi_q; 
		     j < 8; j++, fp++, coeffp++)
			sum1 += (*coeffp) * (*fp);
		sum1 >>= 7;
		sum2 -= sum1 * sum1;
		sm->modem.dcd_shreg <<= 1;
		sm->modem.bit_pll += 0x2000;
		newsample = (sum2 > 0);
		if (sm->modem.last_sample ^ newsample) {
			sm->modem.last_sample = newsample;
			sm->modem.dcd_shreg |= 1;
			sm->modem.bit_pll += pll_corr
				[sm->modem.bit_pll < 0x9000];
			j = 4 * hweight8(sm->modem.dcd_shreg & 0x38)
				- hweight16(sm->modem.dcd_shreg & 0x7c0);
			sm->modem.dcd_sum0 += j;
		}
		hdlcdrv_channelbit(&sm->hdrv, sm->modem.last_sample);
		if ((--sm->modem.dcd_time) <= 0) {
			hdlcdrv_setdcd(&sm->hdrv, (sm->modem.dcd_sum0 + 
						   sm->modem.dcd_sum1 + 
						   sm->modem.dcd_sum2) < 0);
			sm->modem.dcd_sum2 = sm->modem.dcd_sum1;
			sm->modem.dcd_sum1 = sm->modem.dcd_sum0;
			sm->modem.dcd_sum0 = 2; /* slight bias */
			sm->modem.dcd_time = 120;
		}
		if (sm->modem.bit_pll >= 0x10000) {
			sm->modem.bit_pll &= 0xffff;
			sm->modem.shreg >>= 1;
			sm->modem.shreg |= (!(sm->modem.last_rxbit ^
					      sm->modem.last_sample)) << 16;
			sm->modem.last_rxbit = sm->modem.last_sample;
			diag_trigger(sm);
			if (sm->modem.shreg & 1) {
				hdlcdrv_putbits(&sm->hdrv, sm->modem.shreg >> 1);
				sm->modem.shreg = 0x10000;
			}
		}
		diag_add(sm, sm->modem.filt[7] << 8, sum2);
	}
}

#endif /* ENABLE_AFSK1200 */

/* --------------------------------------------------------------------- */
/*
 * ===================== modem routines 9600 baud =========================
 */

#ifdef ENABLE_FSK9600

#define DESCRAM_TAP1 0x20000
#define DESCRAM_TAP2 0x01000
#define DESCRAM_TAP3 0x00001

#define DESCRAM_TAPSH1 17
#define DESCRAM_TAPSH2 12
#define DESCRAM_TAPSH3 0

#define SCRAM_TAP1 0x20000 /* X^17 */
#define SCRAM_TAPN 0x00021 /* X^0+X^5 */

/* --------------------------------------------------------------------- */

#ifdef ENABLE_SBC

static void modulator_9600_4(struct sm_state *sm, unsigned char *buf, int buflen)
{
	int j;
	const unsigned char *cp;

	for (; buflen >= 4; buflen -= 4) {
		if (sm->modem.shreg <= 1)
			sm->modem.shreg = hdlcdrv_getbits(&sm->hdrv) | 0x10000;
		sm->modem.scram = ((sm->modem.scram << 1) |
				   (sm->modem.scram & 1));
		sm->modem.scram ^= (!(sm->modem.shreg & 1));
		sm->modem.shreg >>= 1;
		if (sm->modem.scram & (SCRAM_TAP1 << 1))
			sm->modem.scram ^= (SCRAM_TAPN << 1);
		sm->modem.tx_bit = (sm->modem.tx_bit << 1) | 
			(!!(sm->modem.scram & (SCRAM_TAP1 << 2)));
		cp = tx_filter_9k6_4 + (sm->modem.tx_bit & 0xff);
		for (j = 0; j < 4; j++) {
			*buf++ = *cp;
			cp += 0x100;
		}
	}
}

/* --------------------------------------------------------------------- */

static void demodulator_9600_4(struct sm_state *sm, unsigned char *buf, int buflen)
{
	static const int pll_corr[2] = { -0x1000, 0x1000 };
	unsigned char curbit;
	unsigned int descx;

	for (; buflen > 0; buflen--, buf++) {
		sm->modem.dcd_shreg <<= 1;
		sm->modem.bit_pll += 0x4000;
		curbit = (*buf >= 0x80);
		if (sm->modem.last_sample ^ curbit) {
			sm->modem.dcd_shreg |= 1;
			sm->modem.bit_pll += pll_corr
				[sm->modem.bit_pll < 0xa000];
			sm->modem.dcd_sum0 += 8 * 
				hweight8(sm->modem.dcd_shreg & 0x0c) - 
					!!(sm->modem.dcd_shreg & 0x10);
		}
		sm->modem.last_sample = curbit;
		hdlcdrv_channelbit(&sm->hdrv, sm->modem.last_sample);
		if ((--sm->modem.dcd_time) <= 0) {
			hdlcdrv_setdcd(&sm->hdrv, (sm->modem.dcd_sum0 + 
						   sm->modem.dcd_sum1 + 
						   sm->modem.dcd_sum2) < 0);
			sm->modem.dcd_sum2 = sm->modem.dcd_sum1;
			sm->modem.dcd_sum1 = sm->modem.dcd_sum0;
			sm->modem.dcd_sum0 = 2; /* slight bias */
			sm->modem.dcd_time = 240;
		}
		if (sm->modem.bit_pll >= 0x10000) {
			sm->modem.bit_pll &= 0xffff;
			sm->modem.descram = (sm->modem.descram << 1) | curbit;
			descx = sm->modem.descram ^ (sm->modem.descram >> 1);
			descx ^= ((descx >> DESCRAM_TAPSH1) ^
				  (descx >> DESCRAM_TAPSH2));
			sm->modem.shreg >>= 1;
			sm->modem.shreg |= (!(descx & 1)) << 16;
			if (sm->modem.shreg & 1) {
				hdlcdrv_putbits(&sm->hdrv, sm->modem.shreg >> 1);
				sm->modem.shreg = 0x10000;
			}
			diag_trigger(sm);
		}
		diag_add_one(sm, ((short)(*buf - 0x80)) << 8);
	}
}

#endif /* ENABLE_SBC */

/* --------------------------------------------------------------------- */

#if defined(ENABLE_WSS) || defined(ENABLE_WSSFDX)

static void modulator_9600_5(struct sm_state *sm, unsigned char *buf, int buflen)
{
	int j;
	const unsigned char *cp;

	for (; buflen >= 5; buflen -= 5) {
		if (sm->modem.shreg <= 1)
			sm->modem.shreg = hdlcdrv_getbits(&sm->hdrv) | 0x10000;
		sm->modem.scram = ((sm->modem.scram << 1) |
				   (sm->modem.scram & 1));
		sm->modem.scram ^= (!(sm->modem.shreg & 1));
		sm->modem.shreg >>= 1;
		if (sm->modem.scram & (SCRAM_TAP1 << 1))
			sm->modem.scram ^= (SCRAM_TAPN << 1);
		sm->modem.tx_bit = (sm->modem.tx_bit << 1) | 
			(!!(sm->modem.scram & (SCRAM_TAP1 << 2)));
		cp = tx_filter_9k6_5 + (sm->modem.tx_bit & 0xff);
		for (j = 0; j < 5; j++) {
			*buf++ = *cp;
			cp += 0x100;
		}
	}
}

/* --------------------------------------------------------------------- */

static void demodulator_9600_5(struct sm_state *sm, unsigned char *buf, int buflen)
{
	static const int pll_corr[2] = { -0x1000, 0x1000 };
	unsigned char curbit;
	unsigned int descx;

	for (; buflen > 0; buflen--, buf++) {
		sm->modem.dcd_shreg <<= 1;
		sm->modem.bit_pll += 0x3333;
		curbit = (*buf >= 0x80);
		if (sm->modem.last_sample ^ curbit) {
			sm->modem.dcd_shreg |= 1;
			sm->modem.bit_pll += pll_corr
				[sm->modem.bit_pll < 0x9999];
			sm->modem.dcd_sum0 += 16 * 
				hweight8(sm->modem.dcd_shreg & 0x0c) - 
					hweight8(sm->modem.dcd_shreg & 0x70);
		}
		sm->modem.last_sample = curbit;
		hdlcdrv_channelbit(&sm->hdrv, sm->modem.last_sample);
		if ((--sm->modem.dcd_time) <= 0) {
			hdlcdrv_setdcd(&sm->hdrv, (sm->modem.dcd_sum0 + 
						   sm->modem.dcd_sum1 + 
						   sm->modem.dcd_sum2) < 0);
			sm->modem.dcd_sum2 = sm->modem.dcd_sum1;
			sm->modem.dcd_sum1 = sm->modem.dcd_sum0;
			sm->modem.dcd_sum0 = 2; /* slight bias */
			sm->modem.dcd_time = 240;
		}
		if (sm->modem.bit_pll >= 0x10000) {
			sm->modem.bit_pll &= 0xffff;
			sm->modem.descram = (sm->modem.descram << 1) | curbit;
			descx = sm->modem.descram ^ (sm->modem.descram >> 1);
			descx ^= ((descx >> DESCRAM_TAPSH1) ^
				  (descx >> DESCRAM_TAPSH2));
			sm->modem.shreg >>= 1;
			sm->modem.shreg |= (!(descx & 1)) << 16;
			if (sm->modem.shreg & 1) {
				hdlcdrv_putbits(&sm->hdrv, sm->modem.shreg >> 1);
				sm->modem.shreg = 0x10000;
			}
			diag_trigger(sm);
		}
		diag_add_one(sm, ((short)(*buf - 0x80)) << 8);
	}
}

#endif /* defined(ENABLE_WSS) || defined(ENABLE_WSSFDX) */
#endif /* ENABLE_FSK9600 */

/* --------------------------------------------------------------------- */
/*
 * ===================== soundblaster specific routines ===================
 */

#ifdef ENABLE_SBC

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

static void setup_dma_dsp(struct device *dev, int send)
{
	struct sm_state *sm = (struct sm_state *)dev->priv;
	struct modem_info *mi = (struct modem_info *)sm->hdrv.ops;
        unsigned long flags;
        static const unsigned char sbcmode[2][2] = {
        { SBC_LO_INPUT_AUTOINIT, SBC_LO_OUTPUT_AUTOINIT }, 
	{ SBC_HI_INPUT_AUTOINIT, SBC_HI_OUTPUT_AUTOINIT }};
        static const unsigned char dmamode[2] = 
        { DMA_MODE_READ | DMA_MODE_AUTOINIT, 
		  DMA_MODE_WRITE  | DMA_MODE_AUTOINIT};
	static const unsigned char sbcskr[2] = 
	{ SBC_SPEAKER_OFF, SBC_SPEAKER_ON };
	unsigned long dmabufaddr = virt_to_bus(sm->modem.dmabufr);

	send = !!send;
        if (!reset_dsp(dev)) {
                printk(KERN_ERR "sm: cannot reset sb dsp\n");
                return;
        }
        if ((dmabufaddr & 0xffff) + mi->dmabuflen > 0x10000)
                panic("sm: DMA buffer violates DMA boundary!");
        save_flags(flags);
        cli();
        sbc_int_ack(dev);
        write_dsp(dev, SBC_SAMPLE_RATE); /* set sampling rate */
        write_dsp(dev, mi->data_fmt);
        write_dsp(dev, sbcskr[send]); 
        disable_dma(dev->dma);
        clear_dma_ff(dev->dma);
        set_dma_mode(dev->dma, dmamode[send]);
        set_dma_addr(dev->dma, dmabufaddr);
        set_dma_count(dev->dma, mi->dmabuflen);
        enable_dma(dev->dma);
        sbc_int_ack(dev);
        write_dsp(dev, SBC_HISPEED);
        write_dsp(dev, ((mi->dmabuflen >> 1) - 1) & 0xff);
        write_dsp(dev, ((mi->dmabuflen >> 1) - 1) >> 8);
        write_dsp(dev, sbcmode[mi->samplerate >= 13000][send]);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

#if 0
static int probe_int(struct device *dev, struct sm_state *sm)
{
	unsigned long irqs;
	int irq;

	irqs = probe_irq_on();
	setup_dma_dsp(dev, virt_to_bus(sm->modem.dmabufr), 4, 256-77, 0);
	udelay(2000);
        irq = probe_irq_off(irqs);
	disable_dma(dev->dma);
	return irq;
}
#endif

/* --------------------------------------------------------------------- */

static void sbc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct sm_state *sm = (struct sm_state *)dev->priv;
	struct modem_info *mi = (struct modem_info *)sm->hdrv.ops;
	unsigned char new_ptt;
	unsigned char *buf;

	if (!dev || !sm || sm->hdrv.magic != HDLCDRV_MAGIC)
		return;
	new_ptt = hdlcdrv_ptt(&sm->hdrv);
 	sbc_int_ack(dev);
 	buf = sm->modem.dmabufr;
	if (sm->modem.dmabufidx)
		buf += mi->dmabuflen/2;
	sm->modem.dmabufidx = !sm->modem.dmabufidx;
	if (sm->modem.oldptt != new_ptt) {
		disable_dma(dev->dma);
		sti();
 		sm->modem.dmabufidx = 0;
		if (!new_ptt) {
			setup_dma_dsp(dev, 0);
			goto endint;
		}
		mi->demodulator(sm, buf, mi->dmabuflen/2);
		mi->modulator(sm, sm->modem.dmabufr, mi->dmabuflen/2);
 		setup_dma_dsp(dev, 1);
		mi->modulator(sm, sm->modem.dmabufr + mi->dmabuflen/2, 
			      mi->dmabuflen/2);
		goto endint;
 	}
	sm_int_freq(sm);
	sti();
	/*
	 * check if transmitter active
	 */
	if (new_ptt)
		mi->modulator(sm, buf, mi->dmabuflen/2);
	else {
		mi->demodulator(sm, buf, mi->dmabuflen/2);
		hdlcdrv_arbitrate(dev, &sm->hdrv);
	}
 endint:
	sm->modem.oldptt = new_ptt;
	output_status(sm);
	hdlcdrv_transmitter(dev, &sm->hdrv);
	hdlcdrv_receiver(dev, &sm->hdrv);
}

/* --------------------------------------------------------------------- */

static int sbc_open(struct device *dev) 
{
	struct sm_state *sm = (struct sm_state *)dev->priv;
	struct modem_info *mi = (struct modem_info *)sm->hdrv.ops;
	unsigned char revreq = (mi->samplerate >= 13000) ? 3 : 2;
	
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
	if (!read_dsp(dev, &sm->modem.revhi) || 
	    !read_dsp(dev, &sm->modem.revlo))
		return -ENODEV;
	if (sm->modem.revhi < revreq) {
		printk(KERN_ERR "sm: sbc io 0x%lx: DSP rev %d.%02d too "
		       "old, at least %d.00 required\n", dev->base_addr,
		       sm->modem.revhi, sm->modem.revlo, revreq);
		return -ENODEV;
	}
	/*
	 * initialize some variables
	 */
	if (!(sm->modem.dmabufr = kmalloc(mi->dmabuflen, GFP_KERNEL | GFP_DMA)))
		return -ENOMEM;
	sm->modem.shreg = sm->modem.last_sample = 0;
	sm->modem.bit_pll = sm->modem.dcd_shreg = sm->modem.dcd_sum1 = 0;
	sm->modem.dcd_sum2 = sm->modem.last_rxbit = sm->modem.tx_bit = 0;
	sm->modem.dmabufidx = sm->modem.oldptt = 0;
	sm->modem.dmabufw = NULL;
	sm->modem.dcd_time = 120;
	sm->modem.dcd_sum0 = 2;
#if 0
	if (!dev->irq) {
		int irq = probe_int(dev, sm);
		if (irq < 0) {
			printk(KERN_ERR "sm: irq autoprobe failed\n");
			kfree_s(sm->modem.dmabufr, mi->dmabuflen);
			return -EBUSY;
		}
		dev->irq = irq;
	}
#endif
	if (request_dma(dev->dma, mi->mode_name)) {
		kfree_s(sm->modem.dmabufr, mi->dmabuflen);
		return -EBUSY;
	}
	if (request_irq(dev->irq, sbc_interrupt, SA_INTERRUPT, 
			mi->mode_name, dev)) {
		free_dma(dev->dma);
		kfree_s(sm->modem.dmabufr, mi->dmabuflen);
		return -EBUSY;
	}
	request_region(dev->base_addr, SBC_EXTENT, mi->mode_name);
	setup_dma_dsp(dev, 0);
	output_open(sm);
	printk(KERN_INFO "sm: sbc at iobase 0x%lx irq %u dma "
	       "%u DSP revision %u.%02u\n", dev->base_addr, dev->irq, 
	       dev->dma, (unsigned int)sm->modem.revhi, 
	       (unsigned int)sm->modem.revlo);
	MOD_INC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */

static int sbc_close(struct device *dev) 
{
	struct sm_state *sm = (struct sm_state *)dev->priv;

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
	kfree(sm->modem.dmabufr);
	output_close(sm);
	printk(KERN_INFO "sm: close sbc at iobase 0x%lx irq %u dma %u\n", 
	       dev->base_addr, dev->irq, dev->dma);
	MOD_DEC_USE_COUNT;
	return 0;
}

#endif /* ENABLE_SBC */

/* --------------------------------------------------------------------- */
/*
 * ===================== Windows Sound System specific routines ==========
 */

#if defined(ENABLE_WSS) || defined(ENABLE_WSSFDX)

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
	outb(idx & 0xf, WSS_CODEC_IA(dev->base_addr));
	return inb(WSS_CODEC_ID(dev->base_addr));
}

/* --------------------------------------------------------------------- */

static void inline wss_ack_int(struct device *dev)
{
	outb(0, WSS_CODEC_STATUS(dev->base_addr));
}

/* --------------------------------------------------------------------- */

static int wss_init_codec(struct device *dev, unsigned char sdc, 
			  unsigned char src_l, unsigned char src_r, 
			  int igain_l, int igain_r,
			  int ogain_l, int ogain_r)
{
	struct sm_state *sm = (struct sm_state *)dev->priv;
	struct modem_info *mi = (struct modem_info *)sm->hdrv.ops;

	unsigned char tmp, reg0, reg1, reg6, reg7;
	static const signed char irqtab[16] = 
	{ -1, -1, 0x10, -1, -1, -1, -1, 0x08, -1, 0x10, 0x18, 0x20, -1, -1,
		  -1, -1 };
	static const signed char dmatab[4] = { 1, 2, -1, 3 };
	unsigned long time;
				
	tmp = inb(WSS_STATUS(dev->base_addr));
	if ((tmp & 0x3f) != 0x04 && (tmp & 0x3f) != 0x00 && 
	    (tmp & 0x3f) != 0x0f) {
		printk(KERN_ERR "sm: WSS card not found, address 0x%lx, ID "
		       "register 0x%02x\n", dev->base_addr, (int)tmp);
		return -1;
	}
	if ((tmp & 0x80) && ((dev->dma == 0) || ((dev->irq >= 8) && 
						 (dev->irq != 9)))) {
		printk(KERN_ERR "sm: WSS: DMA0 and/or IRQ8..IRQ15 (except "
		       "IRQ9) cannot be used on an 8bit card\n");
		return -1;
	}		
	if (dev->irq > 15 || irqtab[dev->irq] == -1) {
		printk(KERN_ERR "sm: WSS: invalid interrupt %d\n", 
		       (int)dev->irq);
		return -1;
	}
	if (dev->dma > 3 || dmatab[dev->dma] == -1) {
		printk(KERN_ERR "sm: WSS: invalid dma channel %d\n", 
		       (int)dev->dma);
		return -1;
	}
	tmp = irqtab[dev->irq] | dmatab[dev->dma];
	outb((tmp & 0x38) | 0x40, WSS_CONFIG(dev->base_addr)); /* irq probe */
	if (!(inb(WSS_STATUS(dev->base_addr)) & 0x40)) {
		outb(0, WSS_CONFIG(dev->base_addr));
		printk(KERN_ERR "sm: WSS: IRQ%d is not free!\n", dev->irq);
	}
	outb(tmp, WSS_CONFIG(dev->base_addr));
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
#if 1
	write_codec(dev, 9, 0);
	write_codec(dev, 15, 0xaa);
	write_codec(dev, 14, 0x55);
	if ((read_codec(dev, 15) != 0xaa) || (read_codec(dev, 14) != 0x55)) 
		goto codec_err;
#endif
	write_codec(dev, 0x48, mi->data_fmt);  /* Clock and data format register */
	write_codec(dev, 0x49, sdc ? 0xc : 0x8); /* MCE and interface config reg */
	/* single DMA channel, disable both DMA */
	/* clear MCE and wait until ACI set */
	time = jiffies + HZ/4;
	while (!(read_codec(dev, 0x0b) & 0x20) &&
	       ((signed)(jiffies - time) < 0));
	/* wait until ACI cleared */
	while ((read_codec(dev, 0x0b) & 0x20) &&
	       ((signed)(jiffies - time) < 0));
	if ((signed)(jiffies - time) >= 0) {
		printk(KERN_WARNING "sm: ad1848 auto calibration timed out\n");
		goto codec_err;
	}
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
	sm->modem.revhi = inb(WSS_STATUS(dev->base_addr)) & 0x3f;
	sm->modem.revlo = read_codec(dev, 0xc) & 0xf;
	/*
	 * print revisions
	 */
	printk(KERN_INFO "sm: WSS revision %d, CODEC revision %d\n", 
	       (int)sm->modem.revhi, (int)sm->modem.revlo);
	return 0;
 codec_err:
	outb(0, WSS_CONFIG(dev->base_addr));
	printk(KERN_ERR "sm: no WSS soundcard found at address 0x%lx\n",
	       dev->base_addr);
	return -1;
}

#endif /* defined(ENABLE_WSS) || defined(ENABLE_WSSFDX) */

/* --------------------------------------------------------------------- */

#ifdef ENABLE_WSS

static void setup_dma_wss(struct device *dev, int flg)
{
	struct sm_state *sm = (struct sm_state *)dev->priv;
	struct modem_info *mi = (struct modem_info *)sm->hdrv.ops;
        unsigned long flags;
        static const unsigned char codecmode[2] = { 0x0e, 0x0d };
        static const unsigned char dmamode[2] = 
        { DMA_MODE_READ | DMA_MODE_AUTOINIT, 
		  DMA_MODE_WRITE  | DMA_MODE_AUTOINIT};
	unsigned char oldcodecmode, codecdma;
	long abrt;
	unsigned long dmabufaddr = virt_to_bus(sm->modem.dmabufr);

        if ((dmabufaddr & 0xffff) + mi->dmabuflen > 0x10000)
                panic("sm: DMA buffer violates DMA boundary!");
	flg = !!flg;
        save_flags(flags);
        cli();
	/*
	 * perform the final DMA sequence to disable the codec request
	 */
	oldcodecmode = read_codec(dev, 9);
        write_codec(dev, 9, 0xc); /* disable codec */
	wss_ack_int(dev);
	if ((codecdma = read_codec(dev, 11)) & 0x10) {
		disable_dma(dev->dma);
		clear_dma_ff(dev->dma);
		set_dma_mode(dev->dma, dmamode[oldcodecmode & 1]);
		set_dma_addr(dev->dma, dmabufaddr);
		set_dma_count(dev->dma, mi->dmabuflen);
		enable_dma(dev->dma);
		abrt = 0;
		while (((codecdma = read_codec(dev, 11)) & 0x10) ||
		       ((++abrt) >= 0x10000));
	}
        disable_dma(dev->dma);
        clear_dma_ff(dev->dma);
        set_dma_mode(dev->dma, dmamode[flg]);
        set_dma_addr(dev->dma, dmabufaddr);
        set_dma_count(dev->dma, mi->dmabuflen);
        enable_dma(dev->dma);
	write_codec(dev, 15, ((mi->dmabuflen >> 1) - 1) & 0xff);
	write_codec(dev, 14, ((mi->dmabuflen >> 1) - 1) >> 8);
	write_codec(dev, 9, codecmode[flg]);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static void wss_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct sm_state *sm = (struct sm_state *)dev->priv;
	struct modem_info *mi = (struct modem_info *)sm->hdrv.ops;
	unsigned char new_ptt;
	unsigned char *buf;
	unsigned long flags;
	int dmares;

	if (!dev || !sm || sm->hdrv.magic != HDLCDRV_MAGIC)
		return;
	new_ptt = hdlcdrv_ptt(&sm->hdrv);
	save_flags(flags);
	cli();
	wss_ack_int(dev);
	disable_dma(dev->dma);
	clear_dma_ff(dev->dma);
	dmares = get_dma_residue(dev->dma);
	enable_dma(dev->dma);
	if (dmares <= 0)
		dmares = mi->dmabuflen;
	buf = sm->modem.dmabufr;
	if (dmares > mi->dmabuflen/2)
		buf += mi->dmabuflen/2;
	if (dmares > mi->dmabuflen/2)
		dmares -= mi->dmabuflen/2;
#ifdef SM_DEBUG
	if (!sm->debug_vals.last_pllcorr || 
	    dmares < sm->debug_vals.last_pllcorr)
		sm->debug_vals.last_pllcorr = dmares;
#endif /* SM_DEBUG */
	dmares--;
	write_codec(dev, 15, dmares & 0xff);
	write_codec(dev, 14, dmares >> 8);
	restore_flags(flags);
	if (sm->modem.oldptt != new_ptt) {
		disable_dma(dev->dma);
		sti();
 		sm->modem.dmabufidx = 0;
		if (!new_ptt) {
			setup_dma_wss(dev, 0);
			goto endint;
		}
		mi->demodulator(sm, buf, mi->dmabuflen/2);
		mi->modulator(sm, sm->modem.dmabufr, mi->dmabuflen/2);
 		setup_dma_wss(dev, 1);
		mi->modulator(sm, sm->modem.dmabufr + mi->dmabuflen/2,
			      mi->dmabuflen/2);
		goto endint;
 	}
	sm_int_freq(sm);
	sti();
	/*
	 * check if transmitter active
	 */
	if (new_ptt)
		mi->modulator(sm, buf, mi->dmabuflen/2);
	else {
		mi->demodulator(sm, buf, mi->dmabuflen/2);
		hdlcdrv_arbitrate(dev, &sm->hdrv);
	}
 endint:
	sm->modem.oldptt = new_ptt;
	output_status(sm);
	hdlcdrv_transmitter(dev, &sm->hdrv);
	hdlcdrv_receiver(dev, &sm->hdrv);
}

/* --------------------------------------------------------------------- */

static int wss_open(struct device *dev) 
{
	struct sm_state *sm = (struct sm_state *)dev->priv;
	struct modem_info *mi = (struct modem_info *)sm->hdrv.ops;

	if (!dev || !sm)
		return -ENXIO;
	if (dev->base_addr <= 0 || dev->base_addr > 0x1000-WSS_EXTENT || 
	    dev->irq < 2 || dev->irq > 15 || dev->dma > 3)
		return -ENXIO;
	if (check_region(dev->base_addr, WSS_EXTENT))
		return -EACCES;
	/*
	 * check if a card is available
	 */
	if (wss_init_codec(dev, 1, 1, 1, 0, 0, -45, -45))
		return -ENODEV;
	/*
	 * initialize some variables
	 */
	if (!(sm->modem.dmabufr = kmalloc(mi->dmabuflen, GFP_KERNEL | GFP_DMA)))
		return -ENOMEM;
	sm->modem.shreg = sm->modem.last_sample = 0;
	sm->modem.bit_pll = sm->modem.dcd_shreg = sm->modem.dcd_sum1 = 0;
	sm->modem.dcd_sum2 = sm->modem.last_rxbit = sm->modem.tx_bit = 0;
	sm->modem.dmabufidx = sm->modem.oldptt = 0;
	sm->modem.dmabufw = NULL;
	sm->modem.dcd_time = 120;
	sm->modem.dcd_sum0 = 2;
	if (request_dma(dev->dma, mi->mode_name)) {
		kfree_s(sm->modem.dmabufr, mi->dmabuflen);
		return -EBUSY;
	}
	if (request_irq(dev->irq, wss_interrupt, SA_INTERRUPT, 
			mi->mode_name, dev)) {
		free_dma(dev->dma);
		kfree_s(sm->modem.dmabufr, mi->dmabuflen);
		return -EBUSY;
	}
	request_region(dev->base_addr, WSS_EXTENT, mi->mode_name);
	setup_dma_wss(dev, 0);
	output_open(sm);
	printk(KERN_INFO "sm: wss at iobase 0x%lx irq %u dma "
	       "%u\n", dev->base_addr, dev->irq, dev->dma);
	MOD_INC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */

static int wss_close(struct device *dev) 
{
	struct sm_state *sm = (struct sm_state *)dev->priv;

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
	kfree_s(sm->modem.dmabufr, mi->dmabuflen);
	output_close(sm);
	printk(KERN_INFO "sm: close wss at iobase 0x%lx irq %u"
	       " dma %u\n", dev->base_addr, dev->irq, dev->dma);
	MOD_DEC_USE_COUNT;
	return 0;
}

#endif /* ENABLE_WSS */

/* --------------------------------------------------------------------- */
/*
 * =========== Windows Sound System Fullduplex specific routines ==========
 */

/*
 * This does _not_ work on my hardware
 */

#ifdef ENABLE_WSSFDX

static void setup_dma_wssfdx(struct device *dev)
{
	struct sm_state *sm = (struct sm_state *)dev->priv;
	struct modem_info *mi = (struct modem_info *)sm->hdrv.ops;
        unsigned long flags;
	unsigned char codecdma;
	long abrt;
	unsigned long dmabufraddr = virt_to_bus(sm->modem.dmabufr);
	unsigned long dmabufwaddr = virt_to_bus(sm->modem.dmabufw);

        if (((dmabufraddr & 0xffff) + mi->dmabuflen > 0x10000) ||
	    ((dmabufwaddr & 0xffff) + mi->dmabuflen > 0x10000))
                panic("sm: DMA buffer violates DMA boundary!");
        save_flags(flags);
        cli();
	/*
	 * perform the final DMA sequence to disable the codec request
	 */
        write_codec(dev, 9, 0x8); /* disable codec */
	wss_ack_int(dev);
	if ((codecdma = read_codec(dev, 11)) & 0x10) {
		disable_dma(dev->dma);
		disable_dma(!dev->dma);
		clear_dma_ff(dev->dma);
		set_dma_mode(dev->dma, DMA_MODE_WRITE | DMA_MODE_AUTOINIT);
		set_dma_addr(dev->dma, dmabufwaddr);
		set_dma_count(dev->dma, mi->dmabuflen);
		set_dma_mode(!dev->dma,  DMA_MODE_READ | DMA_MODE_AUTOINIT);
		set_dma_addr(!dev->dma, dmabufraddr);
		set_dma_count(!dev->dma, mi->dmabuflen);
		enable_dma(dev->dma);
		enable_dma(!dev->dma);
		abrt = 0;
		while (((codecdma = read_codec(dev, 11)) & 0x10) ||
		       ((++abrt) >= 0x10000));
	}
	disable_dma(dev->dma);
	disable_dma(!dev->dma);
	clear_dma_ff(dev->dma);
	set_dma_mode(dev->dma, DMA_MODE_WRITE | DMA_MODE_AUTOINIT);
	set_dma_addr(dev->dma, dmabufwaddr);
	set_dma_count(dev->dma, mi->dmabuflen);
	set_dma_mode(!dev->dma,  DMA_MODE_READ | DMA_MODE_AUTOINIT);
	set_dma_addr(!dev->dma, dmabufraddr);
	set_dma_count(!dev->dma, mi->dmabuflen);
	enable_dma(dev->dma);
	enable_dma(!dev->dma);
	write_codec(dev, 15, ((mi->dmabuflen >> 1) - 1) & 0xff);
	write_codec(dev, 14, ((mi->dmabuflen >> 1) - 1) >> 8);
	write_codec(dev, 9, 0x0b);
        restore_flags(flags);
}

/* --------------------------------------------------------------------- */

static int msgcnt = 10;

static void wssfdx_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct sm_state *sm = (struct sm_state *)dev->priv;
	struct modem_info *mi = (struct modem_info *)sm->hdrv.ops;
	unsigned char new_ptt;
	unsigned char *bufr;
	unsigned char *bufw;
	unsigned long flags;
	int dmares, dmares2, i;

	if (!dev || !sm || sm->hdrv.magic != HDLCDRV_MAGIC)
		return;
	new_ptt = hdlcdrv_ptt(&sm->hdrv);
	save_flags(flags);
	cli();
	wss_ack_int(dev);
	disable_dma(dev->dma);
	disable_dma(!dev->dma);
	clear_dma_ff(dev->dma);
	dmares = get_dma_residue(dev->dma);
	dmares2 = get_dma_residue(!dev->dma);
	enable_dma(dev->dma);
	enable_dma(!dev->dma);
	if (dmares <= 0)
		dmares = mi->dmabuflen;
	if (dmares2 <= 0)
		dmares2 = mi->dmabuflen;
	bufw = sm->modem.dmabufw;
	if (dmares > mi->dmabuflen/2)
		bufw += mi->dmabuflen/2;
	bufr = sm->modem.dmabufr;
	if (dmares2 > mi->dmabuflen/2)
		bufr += mi->dmabuflen/2;
	if ((i = dmares) > mi->dmabuflen/2)
		i -= mi->dmabuflen/2;
#ifdef SM_DEBUG
	if (!sm->debug_vals.last_pllcorr || 
	    i < sm->debug_vals.last_pllcorr)
		sm->debug_vals.last_pllcorr = i;
#endif /* SM_DEBUG */
	i--;
	write_codec(dev, 15, i & 0xff);
	write_codec(dev, 14, i >> 8);
	restore_flags(flags);
	sm_int_freq(sm);
	sti();

	if (msgcnt > 0) {
		msgcnt--;
		printk(KERN_DEBUG "sm: DMA residue: playback: %d "
		       "capture: %d\n", dmares, dmares2);
	}

	/*
	 * check if transmitter active
	 */
	if (new_ptt)
		mi->modulator(sm, bufw, mi->dmabuflen/2);
	else 
		memset(bufw, 0x80, mi->dmabuflen/2);
	mi->demodulator(sm, bufr, mi->dmabuflen/2);
	hdlcdrv_arbitrate(dev, &sm->hdrv);
	sm->modem.oldptt = new_ptt;
	output_status(sm);
	hdlcdrv_transmitter(dev, &sm->hdrv);
	hdlcdrv_receiver(dev, &sm->hdrv);
}

/* --------------------------------------------------------------------- */

static int wssfdx_open(struct device *dev) 
{
	struct sm_state *sm = (struct sm_state *)dev->priv;
	struct modem_info *mi = (struct modem_info *)sm->hdrv.ops;

	if (!dev || !sm)
		return -ENXIO;
	if (dev->base_addr <= 0 || dev->base_addr > 0x1000-WSS_EXTENT || 
	    dev->irq < 2 || dev->irq > 15 || dev->dma > 3)
		return -ENXIO;
	if (check_region(dev->base_addr, WSS_EXTENT))
		return -EACCES;
	/*
	 * check if a card is available
	 */
	if (wss_init_codec(dev, 0, 1, 1, 0, 0, -45, -45))
		return -ENODEV;
	/*
	 * initialize some variables
	 */
	if (!(sm->modem.dmabufr = kmalloc(mi->dmabuflen, GFP_KERNEL | GFP_DMA)))
		return -ENOMEM;
	if (!(sm->modem.dmabufw = kmalloc(mi->dmabuflen, GFP_KERNEL | GFP_DMA))) {
		kfree_s(sm->modem.dmabufr, mi->dmabuflen);
		return -ENOMEM;
	}
	sm->modem.shreg = sm->modem.last_sample = 0;
	sm->modem.bit_pll = sm->modem.dcd_shreg = sm->modem.dcd_sum1 = 0;
	sm->modem.dcd_sum2 = sm->modem.last_rxbit = sm->modem.tx_bit = 0;
	sm->modem.dmabufidx = sm->modem.oldptt = 0;
	sm->modem.dcd_time = 120;
	sm->modem.dcd_sum0 = 2;
	if (request_dma(dev->dma, mi->mode_name)) {
		kfree_s(sm->modem.dmabufr, mi->dmabuflen);
		kfree_s(sm->modem.dmabufw, mi->dmabuflen);
		return -EBUSY;
	}
	if (request_dma(!dev->dma, mi->mode_name)) {
		free_dma(dev->dma);
		kfree_s(sm->modem.dmabufr, mi->dmabuflen);
		kfree_s(sm->modem.dmabufw, mi->dmabuflen);
		return -EBUSY;
	}
	if (request_irq(dev->irq, wssfdx_interrupt, SA_INTERRUPT, 
			mi->mode_name, dev)) {
		free_dma(dev->dma);
		free_dma(!dev->dma);
		kfree_s(sm->modem.dmabufr, mi->dmabuflen);
		kfree_s(sm->modem.dmabufw, mi->dmabuflen);
		return -EBUSY;
	}
	request_region(dev->base_addr, WSS_EXTENT, mi->mode_name);
	setup_dma_wssfdx(dev);
	output_open(sm);
	printk(KERN_INFO "sm: wss fdx at iobase 0x%lx irq %u dma1 "
	       "%u dma2 %u\n", dev->base_addr, dev->irq, dev->dma, !dev->dma);
	MOD_INC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */

static int wssfdx_close(struct device *dev) 
{
	struct sm_state *sm = (struct sm_state *)dev->priv;

	if (!dev || !sm)
		return -EINVAL;
	/*
	 * disable interrupts
	 */
	disable_dma(dev->dma);
	disable_dma(!dev->dma);
        write_codec(dev, 9, 0x8); /* disable codec */
	free_irq(dev->irq, dev);	
	free_dma(dev->dma);	
	free_dma(!dev->dma);	
	release_region(dev->base_addr, WSS_EXTENT);
	kfree(sm->modem.dmabufr);
	kfree(sm->modem.dmabufw);
	output_close(sm);
	printk(KERN_INFO "sm: close wss fdx at iobase 0x%lx irq %u"
	       " dma %u\n", dev->base_addr, dev->irq, dev->dma);
	MOD_DEC_USE_COUNT;
	return 0;
}

#endif /* ENABLE_WSSFDX */

/* --------------------------------------------------------------------- */
/*
 * ===================== hdlcdrv driver interface =========================
 */

static int sm_ioctl(struct device *dev, struct ifreq *ifr, int cmd);

#define SBC1200_SRATE (256-104) /* the SBC sampling rate, 256-(1E6/srate) */
#define SBC1200_DMABUFLEN 192   /* DMA buffer duration exactly 20ms */

#define SBC9600_SRATE (256-26) /* the SBC sampling rate, 256-(1E6/srate) */
#define SBC9600_DMABUFLEN 768    /* DMA buffer duration exactly 20ms */

#define WSS1200_DATAFMT 0x0e   /* 8bit unsigned PCM, Mono, XTAL1, 9.6kHz */
#define WSS1200_DMABUFLEN 192        /* DMA buffer duration exactly 20ms */

#define WSS9600_DATAFMT 0x0c    /* 8bit unsigned PCM, Mono, XTAL1, 48kHz */
#define WSS9600_DMABUFLEN 960        /* DMA buffer duration exactly 20ms */

/* --------------------------------------------------------------------- */

#ifdef ENABLE_SBC

static const struct modem_info sbc1200_ops = { 
{ 1200, sbc_open, sbc_close, sm_ioctl }, 9600, 1, 8, "sbc_1200",
SBC1200_SRATE, SBC1200_DMABUFLEN,
modulator_1200, demodulator_1200
};
/* --------------------------------------------------------------------- */

static const struct modem_info sbc9600_ops = {
{ 9600, sbc_open, sbc_close, sm_ioctl }, 38400, 1, 4, "sbc_9600",
SBC9600_SRATE, SBC9600_DMABUFLEN,
modulator_9600_4, demodulator_9600_4
};

#endif /* ENABLE_SBC */

/* --------------------------------------------------------------------- */

#ifdef ENABLE_WSS

static const struct modem_info wss1200_ops = {
{ 1200, wss_open, wss_close, sm_ioctl }, 9600, 0, 8, "wss_1200",
WSS1200_DATAFMT, WSS1200_DMABUFLEN,
modulator_1200, demodulator_1200
};

/* --------------------------------------------------------------------- */

static const struct modem_info wss9600_ops = {
{ 9600, wss_open, wss_close, sm_ioctl }, 48000, 0, 5, "wss_9600",
WSS9600_DATAFMT, WSS9600_DMABUFLEN,
modulator_9600_5, demodulator_9600_5
};

#endif /* ENABLE_WSS */

/* --------------------------------------------------------------------- */

#ifdef ENABLE_WSSFDX

static const struct modem_info wss1200fdx_ops = {
{ 1200, wssfdx_open, wssfdx_close, sm_ioctl }, 9600, 0, 8, "wss_fdx_1200",
WSS1200_DATAFMT, WSS1200_DMABUFLEN,
modulator_1200, demodulator_1200
};

/* --------------------------------------------------------------------- */

static const struct modem_info wss9600fdx_ops = {
{ 9600, wssfdx_open, wssfdx_close, sm_ioctl }, 48000, 0, 5, "wss_fdx_9600",
WSS9600_DATAFMT, WSS9600_DMABUFLEN,
modulator_9600_5, demodulator_9600_5
};

#endif /* ENABLE_WSSFDX */

/* --------------------------------------------------------------------- */

static const struct modem_info dummy_ops = {
{ 0, NULL, NULL, sm_ioctl }, 0, 0, 0, "none",
0, 0,
NULL, NULL
};

/* --------------------------------------------------------------------- */

static const struct modem_info *ops_tab[3][2] = {
#ifdef ENABLE_SBC
{ &sbc1200_ops, &sbc9600_ops }, 
#else /* ENABLE_SBC */
{ &dummy_ops, &dummy_ops }, 
#endif /* ENABLE_SBC */
#ifdef ENABLE_WSS
{ &wss1200_ops, &wss9600_ops },
#else /* ENABLE_WSS */
{ &dummy_ops, &dummy_ops }, 
#endif /* ENABLE_WSS */
#ifdef ENABLE_WSSFDX
{ &wss1200fdx_ops, &wss9600fdx_ops }
#else /* ENABLE_WSSFDX */
{ &dummy_ops, &dummy_ops }
#endif /* ENABLE_WSSFDX */
};

/* --------------------------------------------------------------------- */

static int sm_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
	int i;
	struct sm_state *sm;
	struct sm_ioctl bi;
	unsigned long flags;
	unsigned int newdiagmode;
	unsigned int newdiagflags;
	
	if (!dev || !dev->priv || 
	    ((struct sm_state *)dev->priv)->hdrv.magic != HDLCDRV_MAGIC) {
		printk(KERN_ERR "sm_ioctl: invalid device struct\n");
		return -EINVAL;
	}
	sm = (struct sm_state *)dev->priv;

	if (cmd != SIOCDEVPRIVATE)
		return -ENOIOCTLCMD;
	if (copy_from_user(&bi, ifr->ifr_data, sizeof(bi)))
		return -EFAULT;

	switch (bi.cmd) {
	default:
		return -ENOIOCTLCMD;

	case SMCTL_GETMODEMTYPE:
		bi.data.cfg.hardware = sm->config.hardware;
		bi.data.cfg.mode = sm->config.mode;
		break;

	case SMCTL_SETMODEMTYPE:
		if (!suser() || dev->start)
			return -EACCES;
		if (bi.data.cfg.hardware < SM_HARDWARE_INVALID ||
		    bi.data.cfg.hardware > SM_HARDWARE_WSSFDX ||
		    bi.data.cfg.mode < SM_MODE_INVALID ||
		    bi.data.cfg.mode > SM_MODE_FSK9600)
			return -EINVAL;
		sm->config.hardware = bi.data.cfg.hardware;
		sm->config.mode = bi.data.cfg.mode;
		if (bi.data.cfg.hardware == SM_HARDWARE_INVALID ||
		    bi.data.cfg.mode == SM_MODE_INVALID)
			sm->hdrv.ops = &dummy_ops.hops;
		else {
			sm->hdrv.ops = &ops_tab[sm->config.hardware][sm->config.mode]->hops;
			if (!((struct modem_info *)sm->hdrv.ops)->samplerate)
				return -ENODEV;
		}
		return 0;

#ifdef SM_DEBUG
	case SMCTL_GETDEBUG:
		bi.data.dbg.debug1 = sm->hdrv.ptt_keyed;
		bi.data.dbg.debug2 = sm->debug_vals.last_intcnt;
		bi.data.dbg.debug3 = sm->debug_vals.last_pllcorr;
		break;
#endif /* SM_DEBUG */

	case SMCTL_GETMIXER:
		i = verify_area(VERIFY_WRITE, ifr->ifr_data, sizeof(bi));
		if (i)
			return i;
		i = 0;
		bi.data.mix.sample_rate = ((struct modem_info *)sm->hdrv.ops)->samplerate;
		bi.data.mix.bit_rate = sm->hdrv.ops->bitrate;
		if (((struct modem_info *)sm->hdrv.ops)->sbcmix) {
			switch (sm->modem.revhi) {
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
		} else {
			bi.data.mix.mixer_type = SM_MIXER_AD1848;
			if ((0x20ff >> bi.data.mix.reg) & 1) {
				bi.data.mix.data = read_codec(dev, bi.data.mix.reg);
				i = 1;
			}
		}
		if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
			return -EFAULT;
		return i;

	case SMCTL_SETMIXER:
		if (!suser())
			return -EACCES;
		if (((struct modem_info *)sm->hdrv.ops)->sbcmix) {
			switch (sm->modem.revhi) {
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
		} else {
			if (bi.data.mix.mixer_type != SM_MIXER_AD1848)
				return -EINVAL;
			if (!((0x20ff >> bi.data.mix.reg) & 1))
				return -EACCES;
			write_codec(dev, bi.data.mix.reg, bi.data.mix.data);
			return 0;
		}

	case SMCTL_DIAGNOSE:
		newdiagmode = bi.data.diag.mode;
		newdiagflags = bi.data.diag.flags;
		if (newdiagmode > SM_DIAGMODE_DEMOD)
			return -EINVAL;
		bi.data.diag.mode = sm->diag.mode;
		bi.data.diag.flags = sm->diag.flags;
		bi.data.diag.samplesperbit = ((struct modem_info *)sm->hdrv.ops)->sperbit;
		if (sm->diag.mode != newdiagmode) {
			save_flags(flags);
			cli();
			sm->diag.ptr = -1;
			sm->diag.flags = newdiagflags & ~SM_DIAGFLAG_VALID;
			sm->diag.mode = newdiagmode;
			restore_flags(flags);
			break;
		}
		if (sm->diag.ptr < 0 || sm->diag.mode == SM_DIAGMODE_OFF)
			break;
		if (bi.data.diag.datalen > DIAGDATALEN)
			bi.data.diag.datalen = DIAGDATALEN;
		if (sm->diag.ptr < bi.data.diag.datalen)
			break;
		i = verify_area(VERIFY_WRITE, bi.data.diag.data,
				bi.data.diag.datalen * 
				sizeof(short));
		if (i)
			return i;
		if (copy_to_user(bi.data.diag.data, sm->diag.data, 
				 bi.data.diag.datalen * sizeof(short)))
			return -EFAULT;
		bi.data.diag.flags |= SM_DIAGFLAG_VALID;
		save_flags(flags);
		cli();
		sm->diag.ptr = -1;
		sm->diag.flags = newdiagflags & ~SM_DIAGFLAG_VALID;
		sm->diag.mode = newdiagmode;
		restore_flags(flags);
		break;

	}
	if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
		return -EFAULT;
	return 0;

}

/* --------------------------------------------------------------------- */

#ifdef MODULE
static
#endif /* MODULE */
int sm_init(void)
{
	int i, j, found = 0;
	char set_hw = 1;
	struct sm_state *sm;
	char ifname[HDLCDRV_IFNAMELEN];
	
	printk(KERN_INFO "sm: compiled %s %s\n", __TIME__, __DATE__);
	/*
	 * register net devices
	 */
	for (i = 0; i < NR_PORTS; i++) {
		struct device *dev = sm_device+i;
		sprintf(ifname, "sm%d", i);

		if (sm_ports[i].hardware < SM_HARDWARE_INVALID ||
		    sm_ports[i].hardware > SM_HARDWARE_WSSFDX ||
		    sm_ports[i].mode < SM_MODE_INVALID ||
		    sm_ports[i].mode > SM_MODE_FSK9600)
			set_hw = 0;
		if (set_hw) {
			j = hdlcdrv_register_hdlcdrv(dev, &ops_tab[sm_ports[i].hardware]
						     [sm_ports[i].mode]->hops, 
						     sizeof(struct sm_state), ifname, 
						     sm_ports[i].iobase,
						     sm_ports[i].irq,
						     sm_ports[i].dma);
			if (!j) {
				sm = (struct sm_state *)dev->priv;
				sm->hdrv.ptt_out.seriobase = sm_ports[i].seriobase;
				sm->hdrv.ptt_out.pariobase = sm_ports[i].pariobase;
				sm->hdrv.ptt_out.midiiobase = sm_ports[i].midiiobase;
			}
		} else 
			j = hdlcdrv_register_hdlcdrv(dev, &dummy_ops.hops, 
						     sizeof(struct sm_state), 
						     ifname, 0, 0, 0);
		if (j) {
			printk(KERN_WARNING "sm: cannot register net "
			       "device\n");
		} else
			found++;
	}
	if (!found)
		return -ENXIO;
	return 0;
}

/* --------------------------------------------------------------------- */

#ifdef MODULE

/*
 * command line settable parameters
 */
int hardware = SM_HARDWARE_INVALID;
int mode = SM_MODE_INVALID;
int iobase = -1;
int irq = -1;
int dma = -1;
int seriobase = 0;
int pariobase = 0;
int midiiobase = 0;

int init_module(void)
{
	printk(KERN_INFO "sm: v0.1 (C) 1996 Thomas Sailer HB9JNX/AE4WA\n");

	if (hardware != SM_HARDWARE_INVALID) {
		if (iobase == -1)
			iobase = (hardware == SM_HARDWARE_SBC) ? 0x220 : 0x530;
		if (irq == -1)
			irq = (hardware == SM_HARDWARE_SBC) ? 5 : 11;
		if (dma == -1)
			dma = 1;
	}
	sm_ports[0].hardware = hardware;
	sm_ports[0].mode = mode;
	sm_ports[0].iobase = iobase;
	sm_ports[0].irq = irq;
	sm_ports[0].dma = dma;
	sm_ports[0].seriobase = seriobase;
	sm_ports[0].pariobase = pariobase;
	sm_ports[0].midiiobase = midiiobase;
	sm_ports[1].hardware = SM_HARDWARE_INVALID;

	return sm_init();
}

/* --------------------------------------------------------------------- */

void cleanup_module(void)
{
	int i;

	printk(KERN_INFO "sm: cleanup_module called\n");

	for(i = 0; i < NR_PORTS; i++) {
		struct device *dev = sm_device+i;
		struct sm_state *sm = (struct sm_state *)dev->priv;

		if (sm) {
			if (sm->hdrv.magic != HDLCDRV_MAGIC)
				printk(KERN_ERR "sm: invalid magic in "
				       "cleanup_module\n");
			else 
				hdlcdrv_unregister_hdlcdrv(dev);
		}
	}
}

#else /* MODULE */
/* --------------------------------------------------------------------- */
/*
 * format: sm=hw,mode,io,irq,dma,serio,pario[,hw,mode,io,irq,dma,serio,pario]
 * hw=0: SBC, hw=1: WSS; mode=0: AFSK1200, mode=1: FSK9600
 */

void sm_setup(char *str, int *ints)
{
	int i;

	for (i = 0; i < NR_PORTS; i++) 
		if (ints[0] >= 8*(i+1)) {
			sm_ports[i].hardware = ints[8*i+1];
			sm_ports[i].mode = ints[8*i+2];
			sm_ports[i].iobase = ints[8*i+3];
			sm_ports[i].irq = ints[8*i+4];
			sm_ports[i].dma = ints[8*i+5];
			sm_ports[i].seriobase = ints[8*i+6];
			sm_ports[i].pariobase = ints[8*i+7];
			sm_ports[i].midiiobase = ints[8*i+8];
		} else
			sm_ports[i].hardware = SM_HARDWARE_INVALID;

}

#endif /* MODULE */
/* --------------------------------------------------------------------- */
