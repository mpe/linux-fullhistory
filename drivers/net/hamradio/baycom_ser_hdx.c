/*****************************************************************************/

/*
 *	baycom_ser_hdx.c  -- baycom ser12 halfduplex radio modem driver.
 *
 *	Copyright (C) 1997  Thomas Sailer (sailer@ife.ee.ethz.ch)
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
 *  Supported modems
 *
 *  ser12:  This is a very simple 1200 baud AFSK modem. The modem consists only
 *          of a modulator/demodulator chip, usually a TI TCM3105. The computer
 *          is responsible for regenerating the receiver bit clock, as well as
 *          for handling the HDLC protocol. The modem connects to a serial port,
 *          hence the name. Since the serial port is not used as an async serial
 *          port, the kernel driver for serial ports cannot be used, and this
 *          driver only supports standard serial hardware (8250, 16450, 16550A)
 *
 *
 *  Command line options (insmod command line)
 *
 *  mode     * enables software DCD.
 *  iobase   base address of the port; common values are 0x3f8, 0x2f8, 0x3e8, 0x2e8
 *  irq      interrupt line of the port; common values are 4,3
 *
 *
 *  History:
 *   0.1  26.06.96  Adapted from baycom.c and made network driver interface
 *        18.10.96  Changed to new user space access routines (copy_{to,from}_user)
 *   0.3  26.04.97  init code/data tagged
 *   0.4  08.07.97  alternative ser12 decoding algorithm (uses delta CTS ints)
 *   0.5  11.11.97  ser12/par96 split into separate files
 *   0.6  14.04.98  cleanups
 */

/*****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/hdlcdrv.h>
#include <linux/baycom.h>
#include <linux/version.h>

/* --------------------------------------------------------------------- */

#define BAYCOM_DEBUG

/*
 * modem options; bit mask
 */
#define BAYCOM_OPTIONS_SOFTDCD  1

/* --------------------------------------------------------------------- */

static const char bc_drvname[] = "baycom_ser_hdx";
static const char bc_drvinfo[] = KERN_INFO "baycom_ser_hdx: (C) 1997-1998 Thomas Sailer, HB9JNX/AE4WA\n"
KERN_INFO "baycom_ser_hdx: version 0.6 compiled " __TIME__ " " __DATE__ "\n";

/* --------------------------------------------------------------------- */

#define NR_PORTS 4

static struct device baycom_device[NR_PORTS];

static struct {
	char *mode;
	int iobase, irq;
} baycom_ports[NR_PORTS] = { { NULL, 0, 0 }, };

/* --------------------------------------------------------------------- */

#define RBR(iobase) (iobase+0)
#define THR(iobase) (iobase+0)
#define IER(iobase) (iobase+1)
#define IIR(iobase) (iobase+2)
#define FCR(iobase) (iobase+2)
#define LCR(iobase) (iobase+3)
#define MCR(iobase) (iobase+4)
#define LSR(iobase) (iobase+5)
#define MSR(iobase) (iobase+6)
#define SCR(iobase) (iobase+7)
#define DLL(iobase) (iobase+0)
#define DLM(iobase) (iobase+1)

#define SER12_EXTENT 8

/* ---------------------------------------------------------------------- */
/*
 * Information that need to be kept for each board.
 */

struct baycom_state {
	struct hdlcdrv_state hdrv;

	unsigned int options;

	struct modem_state {
		short arb_divider;
		unsigned char flags;
		unsigned int shreg;
		struct modem_state_ser12 {
			unsigned char tx_bit;
			int dcd_sum0, dcd_sum1, dcd_sum2;
			unsigned char last_sample;
			unsigned char last_rxbit;
			unsigned int dcd_shreg;
			unsigned int dcd_time;
			unsigned int bit_pll;
			unsigned char interm_sample;
		} ser12;
	} modem;

#ifdef BAYCOM_DEBUG
	struct debug_vals {
		unsigned long last_jiffies;
		unsigned cur_intcnt;
		unsigned last_intcnt;
		int cur_pllcorr;
		int last_pllcorr;
	} debug_vals;
#endif /* BAYCOM_DEBUG */
};

/* --------------------------------------------------------------------- */

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

/* --------------------------------------------------------------------- */

static void inline baycom_int_freq(struct baycom_state *bc)
{
#ifdef BAYCOM_DEBUG
	unsigned long cur_jiffies = jiffies;
	/*
	 * measure the interrupt frequency
	 */
	bc->debug_vals.cur_intcnt++;
	if ((cur_jiffies - bc->debug_vals.last_jiffies) >= HZ) {
		bc->debug_vals.last_jiffies = cur_jiffies;
		bc->debug_vals.last_intcnt = bc->debug_vals.cur_intcnt;
		bc->debug_vals.cur_intcnt = 0;
		bc->debug_vals.last_pllcorr = bc->debug_vals.cur_pllcorr;
		bc->debug_vals.cur_pllcorr = 0;
	}
#endif /* BAYCOM_DEBUG */
}

/* --------------------------------------------------------------------- */
/*
 * ===================== SER12 specific routines =========================
 */

static void inline ser12_set_divisor(struct device *dev,
				     unsigned char divisor)
{
	outb(0x81, LCR(dev->base_addr));	/* DLAB = 1 */
	outb(divisor, DLL(dev->base_addr));
	outb(0, DLM(dev->base_addr));
	outb(0x01, LCR(dev->base_addr));	/* word length = 6 */
	/*
	 * make sure the next interrupt is generated;
	 * 0 must be used to power the modem; the modem draws its
	 * power from the TxD line
	 */
	outb(0x00, THR(dev->base_addr));
	/*
	 * it is important not to set the divider while transmitting;
	 * this reportedly makes some UARTs generating interrupts
	 * in the hundredthousands per second region
	 * Reported by: Ignacio.Arenaza@studi.epfl.ch (Ignacio Arenaza Nuno)
	 */
}

/* --------------------------------------------------------------------- */

/*
 * must call the TX arbitrator every 10ms
 */
#define SER12_ARB_DIVIDER(bc) ((bc->options & BAYCOM_OPTIONS_SOFTDCD) ? \
			       36 : 24)
#define SER12_DCD_INTERVAL(bc) ((bc->options & BAYCOM_OPTIONS_SOFTDCD) ? \
				240 : 12)

static inline void ser12_tx(struct device *dev, struct baycom_state *bc)
{
	/* one interrupt per channel bit */
	ser12_set_divisor(dev, 12);
	/*
	 * first output the last bit (!) then call HDLC transmitter,
	 * since this may take quite long
	 */
	outb(0x0e | (!!bc->modem.ser12.tx_bit), MCR(dev->base_addr));
	if (bc->modem.shreg <= 1)
		bc->modem.shreg = 0x10000 | hdlcdrv_getbits(&bc->hdrv);
	bc->modem.ser12.tx_bit = !(bc->modem.ser12.tx_bit ^
				   (bc->modem.shreg & 1));
	bc->modem.shreg >>= 1;
}

/* --------------------------------------------------------------------- */

static inline void ser12_rx(struct device *dev, struct baycom_state *bc)
{
	unsigned char cur_s;
	/*
	 * do demodulator
	 */
	cur_s = inb(MSR(dev->base_addr)) & 0x10;	/* the CTS line */
	hdlcdrv_channelbit(&bc->hdrv, cur_s);
	bc->modem.ser12.dcd_shreg = (bc->modem.ser12.dcd_shreg << 1) |
		(cur_s != bc->modem.ser12.last_sample);
	bc->modem.ser12.last_sample = cur_s;
	if(bc->modem.ser12.dcd_shreg & 1) {
		if (bc->options & BAYCOM_OPTIONS_SOFTDCD) {
			unsigned int dcdspos, dcdsneg;

			dcdspos = dcdsneg = 0;
			dcdspos += ((bc->modem.ser12.dcd_shreg >> 1) & 1);
			if (!(bc->modem.ser12.dcd_shreg & 0x7ffffffe))
				dcdspos += 2;
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 2) & 1);
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 3) & 1);
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 4) & 1);

			bc->modem.ser12.dcd_sum0 += 16*dcdspos - dcdsneg;
		} else
			bc->modem.ser12.dcd_sum0--;
	}
	if(!bc->modem.ser12.dcd_time) {
		hdlcdrv_setdcd(&bc->hdrv, (bc->modem.ser12.dcd_sum0 +
					   bc->modem.ser12.dcd_sum1 +
					   bc->modem.ser12.dcd_sum2) < 0);
		bc->modem.ser12.dcd_sum2 = bc->modem.ser12.dcd_sum1;
		bc->modem.ser12.dcd_sum1 = bc->modem.ser12.dcd_sum0;
		/* offset to ensure DCD off on silent input */
		bc->modem.ser12.dcd_sum0 = 2;
		bc->modem.ser12.dcd_time = SER12_DCD_INTERVAL(bc);
	}
	bc->modem.ser12.dcd_time--;
	if (bc->options & BAYCOM_OPTIONS_SOFTDCD) {
		/*
		 * PLL code for the improved software DCD algorithm
		 */
		if (bc->modem.ser12.interm_sample) {
			/*
			 * intermediate sample; set timing correction to normal
			 */
			ser12_set_divisor(dev, 4);
		} else {
			/*
			 * do PLL correction and call HDLC receiver
			 */
			switch (bc->modem.ser12.dcd_shreg & 7) {
			case 1: /* transition too late */
				ser12_set_divisor(dev, 5);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr++;
#endif /* BAYCOM_DEBUG */
				break;
			case 4:	/* transition too early */
				ser12_set_divisor(dev, 3);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr--;
#endif /* BAYCOM_DEBUG */
				break;
			default:
				ser12_set_divisor(dev, 4);
				break;
			}
			bc->modem.shreg >>= 1;
			if (bc->modem.ser12.last_sample ==
			    bc->modem.ser12.last_rxbit)
				bc->modem.shreg |= 0x10000;
			bc->modem.ser12.last_rxbit =
				bc->modem.ser12.last_sample;
		}
		if (++bc->modem.ser12.interm_sample >= 3)
			bc->modem.ser12.interm_sample = 0;
		/*
		 * DCD stuff
		 */
		if (bc->modem.ser12.dcd_shreg & 1) {
			unsigned int dcdspos, dcdsneg;

			dcdspos = dcdsneg = 0;
			dcdspos += ((bc->modem.ser12.dcd_shreg >> 1) & 1);
			dcdspos += (!(bc->modem.ser12.dcd_shreg & 0x7ffffffe))
				<< 1;
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 2) & 1);
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 3) & 1);
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 4) & 1);

			bc->modem.ser12.dcd_sum0 += 16*dcdspos - dcdsneg;
		}
	} else {
		/*
		 * PLL algorithm for the hardware squelch DCD algorithm
		 */
		if (bc->modem.ser12.interm_sample) {
			/*
			 * intermediate sample; set timing correction to normal
			 */
			ser12_set_divisor(dev, 6);
		} else {
			/*
			 * do PLL correction and call HDLC receiver
			 */
			switch (bc->modem.ser12.dcd_shreg & 3) {
			case 1: /* transition too late */
				ser12_set_divisor(dev, 7);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr++;
#endif /* BAYCOM_DEBUG */
				break;
			case 2:	/* transition too early */
				ser12_set_divisor(dev, 5);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr--;
#endif /* BAYCOM_DEBUG */
				break;
			default:
				ser12_set_divisor(dev, 6);
				break;
			}
			bc->modem.shreg >>= 1;
			if (bc->modem.ser12.last_sample ==
			    bc->modem.ser12.last_rxbit)
				bc->modem.shreg |= 0x10000;
			bc->modem.ser12.last_rxbit =
				bc->modem.ser12.last_sample;
		}
		bc->modem.ser12.interm_sample = !bc->modem.ser12.interm_sample;
		/*
		 * DCD stuff
		 */
		bc->modem.ser12.dcd_sum0 -= (bc->modem.ser12.dcd_shreg & 1);
	}
	outb(0x0d, MCR(dev->base_addr));		/* transmitter off */
	if (bc->modem.shreg & 1) {
		hdlcdrv_putbits(&bc->hdrv, bc->modem.shreg >> 1);
		bc->modem.shreg = 0x10000;
	}
	if(!bc->modem.ser12.dcd_time) {
		hdlcdrv_setdcd(&bc->hdrv, (bc->modem.ser12.dcd_sum0 +
					   bc->modem.ser12.dcd_sum1 +
					   bc->modem.ser12.dcd_sum2) < 0);
		bc->modem.ser12.dcd_sum2 = bc->modem.ser12.dcd_sum1;
		bc->modem.ser12.dcd_sum1 = bc->modem.ser12.dcd_sum0;
		/* offset to ensure DCD off on silent input */
		bc->modem.ser12.dcd_sum0 = 2;
		bc->modem.ser12.dcd_time = SER12_DCD_INTERVAL(bc);
	}
	bc->modem.ser12.dcd_time--;
}

/* --------------------------------------------------------------------- */

static void ser12_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct baycom_state *bc = (struct baycom_state *)dev->priv;
	unsigned char iir;

	if (!dev || !bc || bc->hdrv.magic != HDLCDRV_MAGIC)
		return;
	/* fast way out */
	if ((iir = inb(IIR(dev->base_addr))) & 1)
		return;
	baycom_int_freq(bc);
	do {
		switch (iir & 6) {
		case 6:
			inb(LSR(dev->base_addr));
			break;
			
		case 4:
			inb(RBR(dev->base_addr));
			break;
			
		case 2:
			/*
			 * check if transmitter active
			 */
			if (hdlcdrv_ptt(&bc->hdrv))
				ser12_tx(dev, bc);
			else {
				ser12_rx(dev, bc);
				bc->modem.arb_divider--;
			}
			outb(0x00, THR(dev->base_addr));
			break;
			
		default:
			inb(MSR(dev->base_addr));
			break;
		}
		iir = inb(IIR(dev->base_addr));
	} while (!(iir & 1));
	if (bc->modem.arb_divider <= 0) {
		bc->modem.arb_divider = SER12_ARB_DIVIDER(bc);
		__sti();
		hdlcdrv_arbitrate(dev, &bc->hdrv);
	}
	__sti();
	hdlcdrv_transmitter(dev, &bc->hdrv);
	hdlcdrv_receiver(dev, &bc->hdrv);
	__cli();
}

/* --------------------------------------------------------------------- */

enum uart { c_uart_unknown, c_uart_8250,
	    c_uart_16450, c_uart_16550, c_uart_16550A};
static const char *uart_str[] = { 
	"unknown", "8250", "16450", "16550", "16550A" 
};

static enum uart ser12_check_uart(unsigned int iobase)
{
	unsigned char b1,b2,b3;
	enum uart u;
	enum uart uart_tab[] =
		{ c_uart_16450, c_uart_unknown, c_uart_16550, c_uart_16550A };

	b1 = inb(MCR(iobase));
	outb(b1 | 0x10, MCR(iobase));	/* loopback mode */
	b2 = inb(MSR(iobase));
	outb(0x1a, MCR(iobase));
	b3 = inb(MSR(iobase)) & 0xf0;
	outb(b1, MCR(iobase));			/* restore old values */
	outb(b2, MSR(iobase));
	if (b3 != 0x90)
		return c_uart_unknown;
	inb(RBR(iobase));
	inb(RBR(iobase));
	outb(0x01, FCR(iobase));		/* enable FIFOs */
	u = uart_tab[(inb(IIR(iobase)) >> 6) & 3];
	if (u == c_uart_16450) {
		outb(0x5a, SCR(iobase));
		b1 = inb(SCR(iobase));
		outb(0xa5, SCR(iobase));
		b2 = inb(SCR(iobase));
		if ((b1 != 0x5a) || (b2 != 0xa5))
			u = c_uart_8250;
	}
	return u;
}

/* --------------------------------------------------------------------- */

static int ser12_open(struct device *dev)
{
	struct baycom_state *bc = (struct baycom_state *)dev->priv;
	enum uart u;

	if (!dev || !bc)
		return -ENXIO;
	if (!dev->base_addr || dev->base_addr > 0x1000-SER12_EXTENT ||
	    dev->irq < 2 || dev->irq > 15)
		return -ENXIO;
	if (check_region(dev->base_addr, SER12_EXTENT))
		return -EACCES;
	memset(&bc->modem, 0, sizeof(bc->modem));
	bc->hdrv.par.bitrate = 1200;
	if ((u = ser12_check_uart(dev->base_addr)) == c_uart_unknown)
		return -EIO;
	outb(0, FCR(dev->base_addr));  /* disable FIFOs */
	outb(0x0d, MCR(dev->base_addr));
	outb(0, IER(dev->base_addr));
	if (request_irq(dev->irq, ser12_interrupt, SA_INTERRUPT | SA_SHIRQ,
			"baycom_ser12", dev))
		return -EBUSY;
	request_region(dev->base_addr, SER12_EXTENT, "baycom_ser12");
	/*
	 * enable transmitter empty interrupt
	 */
	outb(2, IER(dev->base_addr));
	/*
	 * set the SIO to 6 Bits/character and 19200 or 28800 baud, so that
	 * we get exactly (hopefully) 2 or 3 interrupts per radio symbol,
	 * depending on the usage of the software DCD routine
	 */
	ser12_set_divisor(dev, (bc->options & BAYCOM_OPTIONS_SOFTDCD) ? 4 : 6);
	printk(KERN_INFO "%s: ser12 at iobase 0x%lx irq %u options "
	       "0x%x uart %s\n", bc_drvname, dev->base_addr, dev->irq,
	       bc->options, uart_str[u]);
	MOD_INC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */

static int ser12_close(struct device *dev)
{
	struct baycom_state *bc = (struct baycom_state *)dev->priv;

	if (!dev || !bc)
		return -EINVAL;
	/*
	 * disable interrupts
	 */
	outb(0, IER(dev->base_addr));
	outb(1, MCR(dev->base_addr));
	free_irq(dev->irq, dev);
	release_region(dev->base_addr, SER12_EXTENT);
	printk(KERN_INFO "%s: close ser12 at iobase 0x%lx irq %u\n",
	       bc_drvname, dev->base_addr, dev->irq);
	MOD_DEC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */
/*
 * ===================== hdlcdrv driver interface =========================
 */

/* --------------------------------------------------------------------- */

static int baycom_ioctl(struct device *dev, struct ifreq *ifr,
			struct hdlcdrv_ioctl *hi, int cmd);

/* --------------------------------------------------------------------- */

static struct hdlcdrv_ops ser12_ops = {
	bc_drvname,
	bc_drvinfo,
	ser12_open,
	ser12_close,
	baycom_ioctl
};

/* --------------------------------------------------------------------- */

static int baycom_setmode(struct baycom_state *bc, const char *modestr)
{
	bc->options = !!strchr(modestr, '*');
	return 0;
}

/* --------------------------------------------------------------------- */

static int baycom_ioctl(struct device *dev, struct ifreq *ifr,
			struct hdlcdrv_ioctl *hi, int cmd)
{
	struct baycom_state *bc;
	struct baycom_ioctl bi;
	int cmd2;

	if (!dev || !dev->priv ||
	    ((struct baycom_state *)dev->priv)->hdrv.magic != HDLCDRV_MAGIC) {
		printk(KERN_ERR "bc_ioctl: invalid device struct\n");
		return -EINVAL;
	}
	bc = (struct baycom_state *)dev->priv;

	if (cmd != SIOCDEVPRIVATE)
		return -ENOIOCTLCMD;
	if (get_user(cmd2, (int *)ifr->ifr_data))
		return -EFAULT;
	switch (hi->cmd) {
	default:
		break;

	case HDLCDRVCTL_GETMODE:
		strcpy(hi->data.modename, "ser12");
		if (bc->options & 1)
			strcat(hi->data.modename, "*");
		if (copy_to_user(ifr->ifr_data, hi, sizeof(struct hdlcdrv_ioctl)))
			return -EFAULT;
		return 0;

	case HDLCDRVCTL_SETMODE:
		if (dev->start || !suser())
			return -EACCES;
		hi->data.modename[sizeof(hi->data.modename)-1] = '\0';
		return baycom_setmode(bc, hi->data.modename);

	case HDLCDRVCTL_MODELIST:
		strcpy(hi->data.modename, "ser12");
		if (copy_to_user(ifr->ifr_data, hi, sizeof(struct hdlcdrv_ioctl)))
			return -EFAULT;
		return 0;

	case HDLCDRVCTL_MODEMPARMASK:
		return HDLCDRV_PARMASK_IOBASE | HDLCDRV_PARMASK_IRQ;

	}

	if (copy_from_user(&bi, ifr->ifr_data, sizeof(bi)))
		return -EFAULT;
	switch (bi.cmd) {
	default:
		return -ENOIOCTLCMD;

#ifdef BAYCOM_DEBUG
	case BAYCOMCTL_GETDEBUG:
		bi.data.dbg.debug1 = bc->hdrv.ptt_keyed;
		bi.data.dbg.debug2 = bc->debug_vals.last_intcnt;
		bi.data.dbg.debug3 = bc->debug_vals.last_pllcorr;
		break;
#endif /* BAYCOM_DEBUG */

	}
	if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
		return -EFAULT;
	return 0;

}

/* --------------------------------------------------------------------- */

__initfunc(int baycom_ser_hdx_init(void))
{
	int i, j, found = 0;
	char set_hw = 1;
	struct baycom_state *bc;
	char ifname[HDLCDRV_IFNAMELEN];


	printk(bc_drvinfo);
	/*
	 * register net devices
	 */
	for (i = 0; i < NR_PORTS; i++) {
		struct device *dev = baycom_device+i;
		sprintf(ifname, "bcsh%d", i);

		if (!baycom_ports[i].mode)
			set_hw = 0;
		if (!set_hw)
			baycom_ports[i].iobase = baycom_ports[i].irq = 0;
		j = hdlcdrv_register_hdlcdrv(dev, &ser12_ops,
					     sizeof(struct baycom_state),
					     ifname, baycom_ports[i].iobase,
					     baycom_ports[i].irq, 0);
		if (!j) {
			bc = (struct baycom_state *)dev->priv;
			if (set_hw && baycom_setmode(bc, baycom_ports[i].mode))
				set_hw = 0;
			found++;
		} else {
			printk(KERN_WARNING "%s: cannot register net device\n",
			       bc_drvname);
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
static char *mode[NR_PORTS] = { "ser12*", };
static int iobase[NR_PORTS] = { 0x3f8, };
static int irq[NR_PORTS] = { 4, };

#if LINUX_VERSION_CODE >= 0x20115

MODULE_PARM(mode, "1-" __MODULE_STRING(NR_PORTS) "s");
MODULE_PARM_DESC(mode, "baycom operating mode; * for software DCD");
MODULE_PARM(iobase, "1-" __MODULE_STRING(NR_PORTS) "i");
MODULE_PARM_DESC(iobase, "baycom io base address");
MODULE_PARM(irq, "1-" __MODULE_STRING(NR_PORTS) "i");
MODULE_PARM_DESC(irq, "baycom irq number");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("Baycom ser12 half duplex amateur radio modem driver");

#endif

__initfunc(int init_module(void))
{
	int i;

	for (i = 0; (i < NR_PORTS) && (mode[i]); i++) {
		baycom_ports[i].mode = mode[i];
		baycom_ports[i].iobase = iobase[i];
		baycom_ports[i].irq = irq[i];
	}
	if (i < NR_PORTS-1)
		baycom_ports[i+1].mode = NULL;
	return baycom_ser_hdx_init();
}

/* --------------------------------------------------------------------- */

void cleanup_module(void)
{
	int i;

	for(i = 0; i < NR_PORTS; i++) {
		struct device *dev = baycom_device+i;
		struct baycom_state *bc = (struct baycom_state *)dev->priv;

		if (bc) {
			if (bc->hdrv.magic != HDLCDRV_MAGIC)
				printk(KERN_ERR "baycom: invalid magic in "
				       "cleanup_module\n");
			else
				hdlcdrv_unregister_hdlcdrv(dev);
		}
	}
}

#else /* MODULE */
/* --------------------------------------------------------------------- */
/*
 * format: baycom_ser_=io,irq,mode
 * mode: [*]
 * * indicates sofware DCD
 */

__initfunc(void baycom_ser_hdx_setup(char *str, int *ints))
{
	int i;

	for (i = 0; (i < NR_PORTS) && (baycom_ports[i].mode); i++);
	if ((i >= NR_PORTS) || (ints[0] < 2)) {
		printk(KERN_INFO "%s: too many or invalid interface "
		       "specifications\n", bc_drvname);
		return;
	}
	baycom_ports[i].mode = str;
	baycom_ports[i].iobase = ints[1];
	baycom_ports[i].irq = ints[2];
	if (i < NR_PORTS-1)
		baycom_ports[i+1].mode = NULL;
}

#endif /* MODULE */
/* --------------------------------------------------------------------- */
