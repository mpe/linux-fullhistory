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

#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/net.h>
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

static const char sm_drvname[] = "soundmodem";
static const char sm_drvinfo[] = KERN_INFO "soundmodem: (C) 1996 Thomas Sailer, HB9JNX/AE4WA\n"
KERN_INFO "soundmodem: version 0.3 compiled " __TIME__ " " __DATE__ "\n";           ;

/* --------------------------------------------------------------------- */

#define NR_PORTS 4

#define SM_DEBUG

/* --------------------------------------------------------------------- */

static struct device sm_device[NR_PORTS];

static struct {
	char *mode;
	int iobase, irq, dma, dma2, seriobase, pariobase, midiiobase;
} sm_ports[NR_PORTS] = {
	{ NULL, -1, 0, 0, 0, -1, -1, -1 },
};

/* --------------------------------------------------------------------- */

#define DMA_MODE_AUTOINIT      0x10

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

	const struct modem_tx_info *mode_tx;
	const struct modem_rx_info *mode_rx;

	const struct hardware_info *hwdrv;

	/*
	 * Hardware (soundcard) access routines state
	 */
	union {
		long hw[32/sizeof(long)];
	} hw;

	/*
	 * state of the modem code
	 */
	union {
		long m[32/sizeof(long)];
	} m;
	union {
		long d[256/sizeof(long)];
	} d;

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
		unsigned mod_cyc;
		unsigned demod_cyc;
		unsigned dma_residue;
	} debug_vals;
#endif /* SM_DEBUG */
};

/* ---------------------------------------------------------------------- */
/*
 * Mode definition structure
 */

struct modem_tx_info {
	const struct modem_tx_info *next;
	const char *name;
	unsigned int loc_storage;
	int srate;
	int bitrate;
	unsigned int dmabuflenmodulo;
	void (*modulator)(struct sm_state *, unsigned char *, int);
	void (*init)(struct sm_state *);
};
#define NEXT_TX_INFO NULL

extern const struct modem_tx_info *modem_tx_base;

struct modem_rx_info {
	const struct modem_rx_info *next;
	const char *name;
	unsigned int loc_storage;
	int srate;
	int bitrate;
	unsigned int dmabuflenmodulo;
	unsigned int sperbit;
	void (*demodulator)(struct sm_state *, unsigned char *, int);
	void (*init)(struct sm_state *);
};
#define NEXT_RX_INFO NULL

extern const struct modem_rx_info *modem_rx_base;

/* ---------------------------------------------------------------------- */
/*
 * Soundcard driver definition structure
 */

struct hardware_info {
	const struct hardware_info *next;
	char *hw_name; /* used for request_{region,irq,dma} */
	unsigned int loc_storage;
	/*
	 * mode specific open/close
	 */
	int (*open)(struct device *, struct sm_state *);
	int (*close)(struct device *, struct sm_state *);
	int (*ioctl)(struct device *, struct sm_state *, struct ifreq *,
		     struct hdlcdrv_ioctl *, int);
	int (*sethw)(struct device *, struct sm_state *, char *);
};
#define NEXT_HW_INFO NULL

extern const struct hardware_info *hardware_base;

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

static int check_lpt(unsigned int iobase)
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

static enum uart check_uart(unsigned int iobase)
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

static int check_midi(unsigned int iobase)
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

extern void inline output_status(struct sm_state *sm)
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

	printk(KERN_INFO "%s: ptt output:", sm_drvname);
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

extern inline void diag_trigger(struct sm_state *sm)
{
	if (sm->diag.ptr < 0)
		if (!(sm->diag.flags & SM_DIAGFLAG_DCDGATE) || sm->hdrv.hdlcrx.dcd)
			sm->diag.ptr = 0;
}

/* --------------------------------------------------------------------- */

extern inline void diag_add(struct sm_state *sm, int valinp, int valdemod)
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

extern inline void diag_add_one(struct sm_state *sm, int val)
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

static inline void diag_add_constellation(struct sm_state *sm, int vali, int valq)
{
	if ((sm->diag.mode != SM_DIAGMODE_CONSTELLATION) ||
	    sm->diag.ptr >= DIAGDATALEN-1 || sm->diag.ptr < 0)
		return;
	/* clip */
	if (vali > SHRT_MAX)
		vali = SHRT_MAX;
	if (vali < SHRT_MIN)
		vali = SHRT_MIN;
	if (valq > SHRT_MAX)
		valq = SHRT_MAX;
	if (valq < SHRT_MIN)
		valq = SHRT_MIN;
	sm->diag.data[sm->diag.ptr++] = vali;
	sm->diag.data[sm->diag.ptr++] = valq;
}

/* --------------------------------------------------------------------- */
/*
 * ===================== utility functions ===============================
 */

extern inline unsigned int hweight32(unsigned int w)
	__attribute__ ((unused));
extern inline unsigned int hweight16(unsigned short w)
	__attribute__ ((unused));
extern inline unsigned int hweight8(unsigned char w)
        __attribute__ ((unused));

extern inline unsigned int hweight32(unsigned int w)
{
        unsigned int res = (w & 0x55555555) + ((w >> 1) & 0x55555555);
        res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
        res = (res & 0x0F0F0F0F) + ((res >> 4) & 0x0F0F0F0F);
        res = (res & 0x00FF00FF) + ((res >> 8) & 0x00FF00FF);
        return (res & 0x0000FFFF) + ((res >> 16) & 0x0000FFFF);
}

extern inline unsigned int hweight16(unsigned short w)
{
        unsigned short res = (w & 0x5555) + ((w >> 1) & 0x5555);
        res = (res & 0x3333) + ((res >> 2) & 0x3333);
        res = (res & 0x0F0F) + ((res >> 4) & 0x0F0F);
        return (res & 0x00FF) + ((res >> 8) & 0x00FF);
}

extern inline unsigned int hweight8(unsigned char w)
{
        unsigned short res = (w & 0x55) + ((w >> 1) & 0x55);
        res = (res & 0x33) + ((res >> 2) & 0x33);
        return (res & 0x0F) + ((res >> 4) & 0x0F);
}

extern inline unsigned int gcd(unsigned int x, unsigned int y)
	__attribute__ ((unused));
extern inline unsigned int lcm(unsigned int x, unsigned int y)
	__attribute__ ((unused));

extern inline unsigned int gcd(unsigned int x, unsigned int y)
{
	for (;;) {
		if (!x)
			return y;
		if (!y)
			return x;
		if (x > y)
			x %= y;
		else
			y %= x;
	}
}

extern inline unsigned int lcm(unsigned int x, unsigned int y)
{
	return x * y / gcd(x, y);
}

/* --------------------------------------------------------------------- */
/*
 * ===================== profiling =======================================
 */


#if defined(SM_DEBUG) && (defined(CONFIG_M586) || defined(CONFIG_M686))

/*
 * only do 32bit cycle counter arithmetic; we hope we won't overflow :-)
 * in fact, overflowing modems would require over 2THz clock speeds :-)
 */

#define time_exec(var,cmd)                                      \
({                                                              \
	unsigned int cnt1, cnt2, cnt3;                          \
	__asm__(".byte 0x0f,0x31" : "=a" (cnt1), "=d" (cnt3));  \
	cmd;                                                    \
	__asm__(".byte 0x0f,0x31" : "=a" (cnt2), "=d" (cnt3));  \
	var = cnt2-cnt1;                                        \
})
#else /* defined(SM_DEBUG) && (defined(CONFIG_M586) || defined(CONFIG_M686)) */

#define time_exec(var,cmd) cmd

#endif /* defined(SM_DEBUG) && (defined(CONFIG_M586) || defined(CONFIG_M686)) */

/* --------------------------------------------------------------------- */

#ifdef CONFIG_SOUNDMODEM_WSS
#include "sm_wss.h"
#endif /* CONFIG_SOUNDMODEM_WSS */
#ifdef CONFIG_SOUNDMODEM_SBC
#include "sm_sbc.h"
#endif /* CONFIG_SOUNDMODEM_SBC */

#ifdef CONFIG_SOUNDMODEM_AFSK1200
#include "sm_afsk1200.h"
#endif /* CONFIG_SOUNDMODEM_AFSK1200 */
#ifdef CONFIG_SOUNDMODEM_FSK9600
#include "sm_fsk9600.h"
#endif /* CONFIG_SOUNDMODEM_FSK9600 */
#ifdef CONFIG_SOUNDMODEM_AFSK2666
#include "sm_afsk2666.h"
#endif /* CONFIG_SOUNDMODEM_AFSK2666 */
#ifdef CONFIG_SOUNDMODEM_PSK4800
#include "sm_psk4800.h"
#endif /* CONFIG_SOUNDMODEM_PSK4800 */

/* --------------------------------------------------------------------- */

static int sm_open(struct device *dev);
static int sm_close(struct device *dev);
static int sm_ioctl(struct device *dev, struct ifreq *ifr,
		    struct hdlcdrv_ioctl *hi, int cmd);

/* --------------------------------------------------------------------- */

const struct modem_tx_info *modem_tx_base = NEXT_TX_INFO;
const struct modem_rx_info *modem_rx_base = NEXT_RX_INFO;
const struct hardware_info *hardware_base = NEXT_HW_INFO;

static const struct hdlcdrv_ops sm_ops = {
	sm_drvname, sm_drvinfo, sm_open, sm_close, sm_ioctl
};

/* --------------------------------------------------------------------- */

static int sm_open(struct device *dev)
{
	struct sm_state *sm;
	int err;

	if (!dev || !dev->priv ||
	    ((struct sm_state *)dev->priv)->hdrv.magic != HDLCDRV_MAGIC) {
		printk(KERN_ERR "sm_open: invalid device struct\n");
		return -EINVAL;
	}
	sm = (struct sm_state *)dev->priv;

	if (!sm->mode_tx || !sm->mode_rx || !sm->hwdrv || !sm->hwdrv->open)
		return -ENODEV;
	sm->hdrv.par.bitrate = sm->mode_rx->bitrate;
	err = sm->hwdrv->open(dev, sm);
	if (err)
		return err;
	output_open(sm);
	MOD_INC_USE_COUNT;
	printk(KERN_INFO "%s: %s mode %s.%s at iobase 0x%lx irq %u dma %u\n",
	       sm_drvname, sm->hwdrv->hw_name, sm->mode_tx->name,
	       sm->mode_rx->name, dev->base_addr, dev->irq, dev->dma);
	return 0;
}

/* --------------------------------------------------------------------- */

static int sm_close(struct device *dev)
{
	struct sm_state *sm;
	int err = -ENODEV;

	if (!dev || !dev->priv ||
	    ((struct sm_state *)dev->priv)->hdrv.magic != HDLCDRV_MAGIC) {
		printk(KERN_ERR "sm_close: invalid device struct\n");
		return -EINVAL;
	}
	sm = (struct sm_state *)dev->priv;


	if (sm->hwdrv && sm->hwdrv->close)
		err = sm->hwdrv && sm->hwdrv->close(dev, sm);
	output_close(sm);
	MOD_DEC_USE_COUNT;
	printk(KERN_INFO "%s: close %s at iobase 0x%lx irq %u dma %u\n",
	       sm_drvname, sm->hwdrv->hw_name, dev->base_addr, dev->irq, dev->dma);
	return err;
}

/* --------------------------------------------------------------------- */

static int sethw(struct device *dev, struct sm_state *sm, char *mode)
{
	char *cp = strchr(mode, ':');
	const struct hardware_info *hwp = hardware_base;

	if (!cp)
		cp = mode;
	else {
		*cp++ = '\0';
		while (hwp && hwp->hw_name && strcmp(hwp->hw_name, mode))
			hwp = hwp->next;
		if (!hwp || !hwp->hw_name)
			return -EINVAL;
		if (hwp->loc_storage > sizeof(sm->hw)) {
			printk(KERN_ERR "%s: insufficient storage for hw driver %s (%d)\n",
			       sm_drvname, hwp->hw_name, hwp->loc_storage);
			return -EINVAL;
		}
		sm->hwdrv = hwp;
	}
	if (!*cp)
		return 0;
	if (sm->hwdrv && sm->hwdrv->sethw)
		return sm->hwdrv->sethw(dev, sm, cp);
	return -EINVAL;
}

/* --------------------------------------------------------------------- */

static int sm_ioctl(struct device *dev, struct ifreq *ifr,
		    struct hdlcdrv_ioctl *hi, int cmd)
{
	struct sm_state *sm;
	struct sm_ioctl bi;
	unsigned long flags;
	unsigned int newdiagmode;
	unsigned int newdiagflags;
	char *cp;
	const struct modem_tx_info *mtp = modem_tx_base;
	const struct modem_rx_info *mrp = modem_rx_base;
	const struct hardware_info *hwp = hardware_base;

	if (!dev || !dev->priv ||
	    ((struct sm_state *)dev->priv)->hdrv.magic != HDLCDRV_MAGIC) {
		printk(KERN_ERR "sm_ioctl: invalid device struct\n");
		return -EINVAL;
	}
	sm = (struct sm_state *)dev->priv;

	if (cmd != SIOCDEVPRIVATE) {
		if (!sm->hwdrv || !sm->hwdrv->ioctl)
			return sm->hwdrv->ioctl(dev, sm, ifr, hi, cmd);
		return -ENOIOCTLCMD;
	}
	switch (hi->cmd) {
	default:
		if (sm->hwdrv && sm->hwdrv->ioctl)
			return sm->hwdrv->ioctl(dev, sm, ifr, hi, cmd);
		return -ENOIOCTLCMD;

	case HDLCDRVCTL_GETMODE:
		cp = hi->data.modename;
		if (sm->hwdrv && sm->hwdrv->hw_name)
			cp += sprintf(cp, "%s:", sm->hwdrv->hw_name);
		else
			cp += sprintf(cp, "<unspec>:");
		if (sm->mode_tx && sm->mode_tx->name)
			cp += sprintf(cp, "%s", sm->mode_tx->name);
		else
			cp += sprintf(cp, "<unspec>");
		if (!sm->mode_rx || !sm->mode_rx ||
		    strcmp(sm->mode_rx->name, sm->mode_tx->name)) {
			if (sm->mode_rx && sm->mode_rx->name)
				cp += sprintf(cp, ",%s", sm->mode_rx->name);
			else
				cp += sprintf(cp, ",<unspec>");
		}
		if (copy_to_user(ifr->ifr_data, hi, sizeof(*hi)))
			return -EFAULT;
		return 0;

	case HDLCDRVCTL_SETMODE:
		if (!suser() || dev->start)
			return -EACCES;
		hi->data.modename[sizeof(hi->data.modename)-1] = '\0';
		return sethw(dev, sm, hi->data.modename);

	case HDLCDRVCTL_MODELIST:
		cp = hi->data.modename;
		while (hwp) {
			if (hwp->hw_name)
				cp += sprintf("%s:,", hwp->hw_name);
			hwp = hwp->next;
		}
		while (mtp) {
			if (mtp->name)
				cp += sprintf(">%s,", mtp->name);
			mtp = mtp->next;
		}
		while (mrp) {
			if (mrp->name)
				cp += sprintf("<%s,", mrp->name);
			mrp = mrp->next;
		}
		cp[-1] = '\0';
		if (copy_to_user(ifr->ifr_data, hi, sizeof(*hi)))
			return -EFAULT;
		return 0;

#ifdef SM_DEBUG
	case SMCTL_GETDEBUG:
		if (copy_from_user(&bi, ifr->ifr_data, sizeof(bi)))
			return -EFAULT;
		bi.data.dbg.int_rate = sm->debug_vals.last_intcnt;
		bi.data.dbg.mod_cycles = sm->debug_vals.mod_cyc;
		bi.data.dbg.demod_cycles = sm->debug_vals.demod_cyc;
		bi.data.dbg.dma_residue = sm->debug_vals.dma_residue;
		sm->debug_vals.mod_cyc = sm->debug_vals.demod_cyc =
			sm->debug_vals.dma_residue = 0;
		if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
			return -EFAULT;
		return 0;
#endif /* SM_DEBUG */

	case SMCTL_DIAGNOSE:
		if (copy_from_user(&bi, ifr->ifr_data, sizeof(bi)))
			return -EFAULT;
		newdiagmode = bi.data.diag.mode;
		newdiagflags = bi.data.diag.flags;
		if (newdiagmode > SM_DIAGMODE_CONSTELLATION)
			return -EINVAL;
		bi.data.diag.mode = sm->diag.mode;
		bi.data.diag.flags = sm->diag.flags;
		bi.data.diag.samplesperbit = sm->mode_rx->sperbit;
		if (sm->diag.mode != newdiagmode) {
			save_flags(flags);
			cli();
			sm->diag.ptr = -1;
			sm->diag.flags = newdiagflags & ~SM_DIAGFLAG_VALID;
			sm->diag.mode = newdiagmode;
			restore_flags(flags);
			if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
				return -EFAULT;
			return 0;
		}
		if (sm->diag.ptr < 0 || sm->diag.mode == SM_DIAGMODE_OFF) {
			if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
				return -EFAULT;
			return 0;
		}
		if (bi.data.diag.datalen > DIAGDATALEN)
			bi.data.diag.datalen = DIAGDATALEN;
		if (sm->diag.ptr < bi.data.diag.datalen) {
			if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
				return -EFAULT;
			return 0;
		}
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
		if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
			return -EFAULT;
		return 0;
	}
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

	printk(sm_drvinfo);
	/*
	 * register net devices
	 */
	for (i = 0; i < NR_PORTS; i++) {
		struct device *dev = sm_device+i;
		sprintf(ifname, "sm%d", i);

		if (!sm_ports[i].mode)
			set_hw = 0;
		if (!set_hw)
			sm_ports[i].iobase = sm_ports[i].irq = 0;
		j = hdlcdrv_register_hdlcdrv(dev, &sm_ops, sizeof(struct sm_state),
					     ifname, sm_ports[i].iobase,
					     sm_ports[i].irq, sm_ports[i].dma);
		if (!j) {
			sm = (struct sm_state *)dev->priv;
			sm->hdrv.ptt_out.dma2 = sm_ports[i].dma2;
			sm->hdrv.ptt_out.seriobase = sm_ports[i].seriobase;
			sm->hdrv.ptt_out.pariobase = sm_ports[i].pariobase;
			sm->hdrv.ptt_out.midiiobase = sm_ports[i].midiiobase;
			if (set_hw && sethw(dev, sm, sm_ports[i].mode))
				set_hw = 0;
			found++;
		} else {
			printk(KERN_WARNING "%s: cannot register net device\n",
			       sm_drvname);
		}
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
char *mode = NULL;
int iobase = -1;
int irq = -1;
int dma = -1;
int dma2 = -1;
int serio = 0;
int pario = 0;
int midiio = 0;

MODULE_PARM(mode, "s");
MODULE_PARM(iobase, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(dma, "i");
MODULE_PARM(dma2, "i");
MODULE_PARM(serio, "i");
MODULE_PARM(pario, "i");
MODULE_PARM(midiio, "i");

int init_module(void)
{
	if (mode) {
		if (iobase == -1)
			iobase = (!strncmp(mode, "sbc", 3)) ? 0x220 : 0x530;
		if (irq == -1)
			irq = (!strncmp(mode, "sbc", 3)) ? 5 : 11;
		if (dma == -1)
			dma = 1;
	}
	sm_ports[0].mode = mode;
	sm_ports[0].iobase = iobase;
	sm_ports[0].irq = irq;
	sm_ports[0].dma = dma;
	sm_ports[0].dma2 = dma2;
	sm_ports[0].seriobase = serio;
	sm_ports[0].pariobase = pario;
	sm_ports[0].midiiobase = midiio;
	sm_ports[1].mode = NULL;

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
 * format: sm=io,irq,dma[,dma2[,serio[,pario]]],mode
 * mode: hw:modem
 * hw: sbc, wss, wssfdx
 * modem: afsk1200, fsk9600
 */

void sm_setup(char *str, int *ints)
{
	int i;

	for (i = 0; (i < NR_PORTS) && (sm_ports[i].mode); i++);
	if ((i >= NR_PORTS) || (ints[0] < 4)) {
		printk(KERN_INFO "%s: too many or invalid interface "
		       "specifications\n", sm_drvname);
		return;
	}
	sm_ports[i].mode = str;
	sm_ports[i].iobase = ints[1];
	sm_ports[i].irq = ints[2];
	sm_ports[i].dma = ints[3];
	sm_ports[i].dma2 = (ints[0] >= 5) ? ints[4] : 0;
	sm_ports[i].seriobase = (ints[0] >= 6) ? ints[5] : 0;
	sm_ports[i].pariobase = (ints[0] >= 7) ? ints[6] : 0;
	sm_ports[i].midiiobase = (ints[0] >= 8) ? ints[7] : 0;
	if (i < NR_PORTS-1)
		sm_ports[i+1].mode = NULL;
}

#endif /* MODULE */
/* --------------------------------------------------------------------- */
