/*****************************************************************************/

/*
 *	sm.c  -- soundcard radio modem driver.
 *
 *	Copyright (C) 1996-1998  Thomas Sailer (sailer@ife.ee.ethz.ch)
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
 *  mode     mode string; eg. "wss:afsk1200"
 *  iobase   base address of the soundcard; common values are 0x220 for sbc,
 *           0x530 for wss
 *  irq      interrupt number; common values are 7 or 5 for sbc, 11 for wss
 *  dma      dma number; common values are 0 or 1
 *
 *
 *  History:
 *   0.1  21.09.96  Started
 *        18.10.96  Changed to new user space access routines (copy_{to,from}_user)
 *   0.4  21.01.97  Separately compileable soundcard/modem modules
 *   0.5  03.03.97  fixed LPT probing (check_lpt result was interpreted the wrong way round)
 *   0.6  16.04.97  init code/data tagged
 *   0.7  30.07.97  fixed halfduplex interrupt handlers/hotfix for CS423X
 *   0.8  14.04.98  cleanups
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/ioport.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/version.h>
#include "sm.h"

/* --------------------------------------------------------------------- */

/*static*/ const char sm_drvname[] = "soundmodem";
static const char sm_drvinfo[] = KERN_INFO "soundmodem: (C) 1996-1998 Thomas Sailer, HB9JNX/AE4WA\n"
KERN_INFO "soundmodem: version 0.8 compiled " __TIME__ " " __DATE__ "\n";

/* --------------------------------------------------------------------- */

/*static*/ const struct modem_tx_info *sm_modem_tx_table[] = {
#ifdef CONFIG_SOUNDMODEM_AFSK1200
	&sm_afsk1200_tx,
#endif /* CONFIG_SOUNDMODEM_AFSK1200 */
#ifdef CONFIG_SOUNDMODEM_AFSK2400_7
	&sm_afsk2400_7_tx,
#endif /* CONFIG_SOUNDMODEM_AFSK2400_7 */
#ifdef CONFIG_SOUNDMODEM_AFSK2400_8
	&sm_afsk2400_8_tx,
#endif /* CONFIG_SOUNDMODEM_AFSK2400_8 */
#ifdef CONFIG_SOUNDMODEM_AFSK2666
	&sm_afsk2666_tx,
#endif /* CONFIG_SOUNDMODEM_AFSK2666 */
#ifdef CONFIG_SOUNDMODEM_PSK4800
	&sm_psk4800_tx,
#endif /* CONFIG_SOUNDMODEM_PSK4800 */
#ifdef CONFIG_SOUNDMODEM_HAPN4800
	&sm_hapn4800_8_tx,
	&sm_hapn4800_10_tx,
	&sm_hapn4800_pm8_tx,
	&sm_hapn4800_pm10_tx,
#endif /* CONFIG_SOUNDMODEM_HAPN4800 */
#ifdef CONFIG_SOUNDMODEM_FSK9600
	&sm_fsk9600_4_tx,
	&sm_fsk9600_5_tx,
#endif /* CONFIG_SOUNDMODEM_FSK9600 */
	NULL
};

/*static*/ const struct modem_rx_info *sm_modem_rx_table[] = {
#ifdef CONFIG_SOUNDMODEM_AFSK1200
	&sm_afsk1200_rx,
#endif /* CONFIG_SOUNDMODEM_AFSK1200 */
#ifdef CONFIG_SOUNDMODEM_AFSK2400_7
	&sm_afsk2400_7_rx,
#endif /* CONFIG_SOUNDMODEM_AFSK2400_7 */
#ifdef CONFIG_SOUNDMODEM_AFSK2400_8
	&sm_afsk2400_8_rx,
#endif /* CONFIG_SOUNDMODEM_AFSK2400_8 */
#ifdef CONFIG_SOUNDMODEM_AFSK2666
	&sm_afsk2666_rx,
#endif /* CONFIG_SOUNDMODEM_AFSK2666 */
#ifdef CONFIG_SOUNDMODEM_PSK4800
	&sm_psk4800_rx,
#endif /* CONFIG_SOUNDMODEM_PSK4800 */
#ifdef CONFIG_SOUNDMODEM_HAPN4800
	&sm_hapn4800_8_rx,
	&sm_hapn4800_10_rx,
	&sm_hapn4800_pm8_rx,
	&sm_hapn4800_pm10_rx,
#endif /* CONFIG_SOUNDMODEM_HAPN4800 */
#ifdef CONFIG_SOUNDMODEM_FSK9600
	&sm_fsk9600_4_rx,
	&sm_fsk9600_5_rx,
#endif /* CONFIG_SOUNDMODEM_FSK9600 */
	NULL
};

static const struct hardware_info *sm_hardware_table[] = {
#ifdef CONFIG_SOUNDMODEM_SBC
	&sm_hw_sbc,
	&sm_hw_sbcfdx,
#endif /* CONFIG_SOUNDMODEM_SBC */
#ifdef CONFIG_SOUNDMODEM_WSS
	&sm_hw_wss,
	&sm_hw_wssfdx,
#endif /* CONFIG_SOUNDMODEM_WSS */
	NULL
};

/* --------------------------------------------------------------------- */

#define NR_PORTS 4

/* --------------------------------------------------------------------- */

static struct device sm_device[NR_PORTS];

static struct {
	char *mode;
	int iobase, irq, dma, dma2, seriobase, pariobase, midiiobase;
} sm_ports[NR_PORTS] = {
	{ NULL, -1, 0, 0, 0, -1, -1, -1 },
};

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

/* --------------------------------------------------------------------- */
/*
 * ===================== port checking routines ========================
 */

/*
 * returns 0 if ok and != 0 on error;
 * the same behaviour as par96_check_lpt in baycom.c
 */

/*
 * returns 0 if ok and != 0 on error;
 * the same behaviour as par96_check_lpt in baycom.c
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

void sm_output_status(struct sm_state *sm)
{
	int invert_dcd = 0;
	int invert_ptt = 0;

	int ptt = /*hdlcdrv_ptt(&sm->hdrv)*/(sm->dma.ptt_cnt > 0) ^ invert_ptt;
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

static void sm_output_open(struct sm_state *sm)
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
	    !check_lpt(sm->hdrv.ptt_out.pariobase)) {
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
	sm_output_status(sm);

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

static void sm_output_close(struct sm_state *sm)
{
	/* release regions used for PTT output */
	sm->hdrv.hdlctx.ptt = sm->hdrv.hdlctx.calibrate = 0;
	sm_output_status(sm);
	if (sm->hdrv.ptt_out.flags & SP_SER)
		release_region(sm->hdrv.ptt_out.seriobase, SER_EXTENT);
       	if (sm->hdrv.ptt_out.flags & SP_PAR)
		release_region(sm->hdrv.ptt_out.pariobase, LPT_EXTENT);
       	if (sm->hdrv.ptt_out.flags & SP_MIDI)
		release_region(sm->hdrv.ptt_out.midiiobase, MIDI_EXTENT);
	sm->hdrv.ptt_out.flags = 0;
}

/* --------------------------------------------------------------------- */

static int sm_open(struct device *dev);
static int sm_close(struct device *dev);
static int sm_ioctl(struct device *dev, struct ifreq *ifr,
		    struct hdlcdrv_ioctl *hi, int cmd);

/* --------------------------------------------------------------------- */

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
	sm_output_open(sm);
	MOD_INC_USE_COUNT;
	printk(KERN_INFO "%s: %s mode %s.%s at iobase 0x%lx irq %u dma %u dma2 %u\n",
	       sm_drvname, sm->hwdrv->hw_name, sm->mode_tx->name,
	       sm->mode_rx->name, dev->base_addr, dev->irq, dev->dma, sm->hdrv.ptt_out.dma2);
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
	sm_output_close(sm);
	MOD_DEC_USE_COUNT;
	printk(KERN_INFO "%s: close %s at iobase 0x%lx irq %u dma %u\n",
	       sm_drvname, sm->hwdrv->hw_name, dev->base_addr, dev->irq, dev->dma);
	return err;
}

/* --------------------------------------------------------------------- */

static int sethw(struct device *dev, struct sm_state *sm, char *mode)
{
	char *cp = strchr(mode, ':');
	const struct hardware_info **hwp = sm_hardware_table;

	if (!cp)
		cp = mode;
	else {
		*cp++ = '\0';
		while (hwp && (*hwp) && (*hwp)->hw_name && strcmp((*hwp)->hw_name, mode))
			hwp++;
		if (!hwp || !*hwp || !(*hwp)->hw_name)
			return -EINVAL;
		if ((*hwp)->loc_storage > sizeof(sm->hw)) {
			printk(KERN_ERR "%s: insufficient storage for hw driver %s (%d)\n",
			       sm_drvname, (*hwp)->hw_name, (*hwp)->loc_storage);
			return -EINVAL;
		}
		sm->hwdrv = *hwp;
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
	const struct modem_tx_info **mtp = sm_modem_tx_table;
	const struct modem_rx_info **mrp = sm_modem_rx_table;
	const struct hardware_info **hwp = sm_hardware_table;

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
		if (dev->start || !suser())
			return -EACCES;
		hi->data.modename[sizeof(hi->data.modename)-1] = '\0';
		return sethw(dev, sm, hi->data.modename);

	case HDLCDRVCTL_MODELIST:
		cp = hi->data.modename;
		while (*hwp) {
			if ((*hwp)->hw_name)
				cp += sprintf(cp, "%s:,", (*hwp)->hw_name);
			hwp++;
		}
		while (*mtp) {
			if ((*mtp)->name)
				cp += sprintf(cp, ">%s,", (*mtp)->name);
			mtp++;
		}
		while (*mrp) {
			if ((*mrp)->name)
				cp += sprintf(cp, "<%s,", (*mrp)->name);
			mrp++;
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
__initfunc(static int sm_init(void))
#else /* MODULE */
__initfunc(int sm_init(void))
#endif /* MODULE */
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
static char *mode = NULL;
static int iobase = -1;
static int irq = -1;
static int dma = -1;
static int dma2 = -1;
static int serio = 0;
static int pario = 0;
static int midiio = 0;

#if LINUX_VERSION_CODE >= 0x20115

MODULE_PARM(mode, "s");
MODULE_PARM_DESC(mode, "soundmodem operating mode; eg. sbc:afsk1200 or wss:fsk9600");
MODULE_PARM(iobase, "i");
MODULE_PARM_DESC(iobase, "soundmodem base address");
MODULE_PARM(irq, "i");
MODULE_PARM_DESC(irq, "soundmodem interrupt");
MODULE_PARM(dma, "i");
MODULE_PARM_DESC(dma, "soundmodem dma channel");
MODULE_PARM(dma2, "i");
MODULE_PARM_DESC(dma2, "soundmodem 2nd dma channel; full duplex only");
MODULE_PARM(serio, "i");
MODULE_PARM_DESC(serio, "soundmodem PTT output on serial port");
MODULE_PARM(pario, "i");
MODULE_PARM_DESC(pario, "soundmodem PTT output on parallel port");
MODULE_PARM(midiio, "i");
MODULE_PARM_DESC(midiio, "soundmodem PTT output on midi port");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("Soundcard amateur radio modem driver");

#endif

__initfunc(int init_module(void))
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

__initfunc(void sm_setup(char *str, int *ints))
{
	int i;

	for (i = 0; (i < NR_PORTS) && (sm_ports[i].mode); i++);
	if ((i >= NR_PORTS) || (ints[0] < 3)) {
		printk(KERN_INFO "%s: too many or invalid interface "
		       "specifications\n", sm_drvname);
		return;
	}
	sm_ports[i].mode = str;
	sm_ports[i].iobase = ints[1];
	sm_ports[i].irq = ints[2];
	sm_ports[i].dma = ints[3];
	sm_ports[i].dma2 = (ints[0] >= 4) ? ints[4] : 0;
	sm_ports[i].seriobase = (ints[0] >= 5) ? ints[5] : 0;
	sm_ports[i].pariobase = (ints[0] >= 6) ? ints[6] : 0;
	sm_ports[i].midiiobase = (ints[0] >= 7) ? ints[7] : 0;
	if (i < NR_PORTS-1)
		sm_ports[i+1].mode = NULL;
}

#endif /* MODULE */
/* --------------------------------------------------------------------- */
