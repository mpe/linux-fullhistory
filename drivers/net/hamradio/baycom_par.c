/*****************************************************************************/

/*
 *	baycom_par.c  -- baycom par96 and picpar radio modem driver.
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
 *  par96:  This is a modem for 9600 baud FSK compatible to the G3RUH standard.
 *          The modem does all the filtering and regenerates the receiver clock.
 *          Data is transferred from and to the PC via a shift register.
 *          The shift register is filled with 16 bits and an interrupt is
 *          signalled. The PC then empties the shift register in a burst. This
 *          modem connects to the parallel port, hence the name. The modem
 *          leaves the implementation of the HDLC protocol and the scrambler
 *          polynomial to the PC. This modem is no longer available (at least
 *          from Baycom) and has been replaced by the PICPAR modem (see below).
 *          You may however still build one from the schematics published in
 *          cq-DL :-).
 *
 *  picpar: This is a redesign of the par96 modem by Henning Rech, DF9IC. The
 *          modem is protocol compatible to par96, but uses only three low
 *          power ICs and can therefore be fed from the parallel port and
 *          does not require an additional power supply. It features
 *          built in DCD circuitry. The driver should therefore be configured
 *          for hardware DCD.
 *
 *
 *  Command line options (insmod command line)
 *
 *  mode     driver mode string. Valid choices are par96 and picpar.
 *  iobase   base address of the port; common values are 0x378, 0x278, 0x3bc
 *
 *
 *  History:
 *   0.1  26.06.96  Adapted from baycom.c and made network driver interface
 *        18.10.96  Changed to new user space access routines (copy_{to,from}_user)
 *   0.3  26.04.97  init code/data tagged
 *   0.4  08.07.97  alternative ser12 decoding algorithm (uses delta CTS ints)
 *   0.5  11.11.97  split into separate files for ser12/par96
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
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/hdlcdrv.h>
#include <linux/baycom.h>
#include <linux/parport.h>

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

extern __inline__ int copy_from_user(void *to, const void *from, unsigned long n)
{
        int i = verify_area(VERIFY_READ, from, n);
        if (i)
                return i;
        memcpy_fromfs(to, from, n);
        return 0;
}

extern __inline__ int copy_to_user(void *to, const void *from, unsigned long n)
{
        int i = verify_area(VERIFY_WRITE, to, n);
        if (i)
                return i;
        memcpy_tofs(to, from, n);
        return 0;
}
#endif

#if LINUX_VERSION_CODE >= 0x20123
#include <linux/init.h>
#else
#define __init
#define __initdata
#define __initfunc(x) x
#endif

/* --------------------------------------------------------------------- */

#define BAYCOM_DEBUG

/*
 * modem options; bit mask
 */
#define BAYCOM_OPTIONS_SOFTDCD  1

/* --------------------------------------------------------------------- */

static const char bc_drvname[] = "baycom_par";
static const char bc_drvinfo[] = KERN_INFO "baycom_par: (C) 1997 Thomas Sailer, HB9JNX/AE4WA\n"
KERN_INFO "baycom_par: version 0.5 compiled " __TIME__ " " __DATE__ "\n";

/* --------------------------------------------------------------------- */

#define NR_PORTS 4

static struct device baycom_device[NR_PORTS];

static struct {
	const char *mode;
	int iobase;
} baycom_ports[NR_PORTS] = { { NULL, 0 }, };

/* --------------------------------------------------------------------- */

#define SER12_EXTENT 8

#define LPT_DATA(dev)    ((dev)->base_addr+0)
#define LPT_STATUS(dev)  ((dev)->base_addr+1)
#define LPT_CONTROL(dev) ((dev)->base_addr+2)
#define LPT_IRQ_ENABLE      0x10

#define PAR96_BURSTBITS 16
#define PAR96_BURST     4
#define PAR96_PTT       2
#define PAR96_TXBIT     1
#define PAR96_ACK       0x40
#define PAR96_RXBIT     0x20
#define PAR96_DCD       0x10
#define PAR97_POWER     0xf8

/* ---------------------------------------------------------------------- */
/*
 * Information that need to be kept for each board.
 */

struct baycom_state {
	struct hdlcdrv_state hdrv;

	struct pardevice *pdev;
	unsigned int options;

	struct modem_state {
		short arb_divider;
		unsigned char flags;
		unsigned int shreg;
		struct modem_state_par96 {
			int dcd_count;
			unsigned int dcd_shreg;
			unsigned long descram;
			unsigned long scram;
		} par96;
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

static void __inline__ baycom_int_freq(struct baycom_state *bc)
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
 * ===================== PAR96 specific routines =========================
 */

#define PAR96_DESCRAM_TAP1 0x20000
#define PAR96_DESCRAM_TAP2 0x01000
#define PAR96_DESCRAM_TAP3 0x00001

#define PAR96_DESCRAM_TAPSH1 17
#define PAR96_DESCRAM_TAPSH2 12
#define PAR96_DESCRAM_TAPSH3 0

#define PAR96_SCRAM_TAP1 0x20000 /* X^17 */
#define PAR96_SCRAM_TAPN 0x00021 /* X^0+X^5 */

/* --------------------------------------------------------------------- */

static __inline__ void par96_tx(struct device *dev, struct baycom_state *bc)
{
	int i;
	unsigned int data = hdlcdrv_getbits(&bc->hdrv);

	for(i = 0; i < PAR96_BURSTBITS; i++, data >>= 1) {
		unsigned char val = PAR97_POWER;
		bc->modem.par96.scram = ((bc->modem.par96.scram << 1) |
					 (bc->modem.par96.scram & 1));
		if (!(data & 1))
			bc->modem.par96.scram ^= 1;
		if (bc->modem.par96.scram & (PAR96_SCRAM_TAP1 << 1))
			bc->modem.par96.scram ^=
				(PAR96_SCRAM_TAPN << 1);
		if (bc->modem.par96.scram & (PAR96_SCRAM_TAP1 << 2))
			val |= PAR96_TXBIT;
		outb(val, LPT_DATA(dev));
		outb(val | PAR96_BURST, LPT_DATA(dev));
	}
}

/* --------------------------------------------------------------------- */

static __inline__ void par96_rx(struct device *dev, struct baycom_state *bc)
{
	int i;
	unsigned int data, mask, mask2, descx;

	/*
	 * do receiver; differential decode and descramble on the fly
	 */
	for(data = i = 0; i < PAR96_BURSTBITS; i++) {
		bc->modem.par96.descram = (bc->modem.par96.descram << 1);
		if (inb(LPT_STATUS(dev)) & PAR96_RXBIT)
			bc->modem.par96.descram |= 1;
		descx = bc->modem.par96.descram ^
			(bc->modem.par96.descram >> 1);
		/* now the diff decoded data is inverted in descram */
		outb(PAR97_POWER | PAR96_PTT, LPT_DATA(dev));
		descx ^= ((descx >> PAR96_DESCRAM_TAPSH1) ^
			  (descx >> PAR96_DESCRAM_TAPSH2));
		data >>= 1;
		if (!(descx & 1))
			data |= 0x8000;
		outb(PAR97_POWER | PAR96_PTT | PAR96_BURST, LPT_DATA(dev));
	}
	hdlcdrv_putbits(&bc->hdrv, data);
	/*
	 * do DCD algorithm
	 */
	if (bc->options & BAYCOM_OPTIONS_SOFTDCD) {
		bc->modem.par96.dcd_shreg = (bc->modem.par96.dcd_shreg >> 16)
			| (data << 16);
		/* search for flags and set the dcd counter appropriately */
		for(mask = 0x1fe00, mask2 = 0xfc00, i = 0;
		    i < PAR96_BURSTBITS; i++, mask <<= 1, mask2 <<= 1)
			if ((bc->modem.par96.dcd_shreg & mask) == mask2)
				bc->modem.par96.dcd_count = HDLCDRV_MAXFLEN+4;
		/* check for abort/noise sequences */
		for(mask = 0x1fe00, mask2 = 0x1fe00, i = 0;
		    i < PAR96_BURSTBITS; i++, mask <<= 1, mask2 <<= 1)
			if (((bc->modem.par96.dcd_shreg & mask) == mask2) &&
			    (bc->modem.par96.dcd_count >= 0))
				bc->modem.par96.dcd_count -= HDLCDRV_MAXFLEN-10;
		/* decrement and set the dcd variable */
		if (bc->modem.par96.dcd_count >= 0)
			bc->modem.par96.dcd_count -= 2;
		hdlcdrv_setdcd(&bc->hdrv, bc->modem.par96.dcd_count > 0);
	} else {
		hdlcdrv_setdcd(&bc->hdrv, !!(inb(LPT_STATUS(dev)) & PAR96_DCD));
	}
}

/* --------------------------------------------------------------------- */

static void par96_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct baycom_state *bc = (struct baycom_state *)dev->priv;

	if (!dev || !bc || bc->hdrv.magic != HDLCDRV_MAGIC)
		return;

	baycom_int_freq(bc);
	/*
	 * check if transmitter active
	 */
	if (hdlcdrv_ptt(&bc->hdrv))
		par96_tx(dev, bc);
	else {
		par96_rx(dev, bc);
		if (--bc->modem.arb_divider <= 0) {
			bc->modem.arb_divider = 6;
			__sti();
			hdlcdrv_arbitrate(dev, &bc->hdrv);
		}
	}
	__sti();
	hdlcdrv_transmitter(dev, &bc->hdrv);
	hdlcdrv_receiver(dev, &bc->hdrv);
        __cli();
}

/* --------------------------------------------------------------------- */

static void par96_wakeup(void *handle)
{
        struct device *dev = (struct device *)handle;
	struct baycom_state *bc = (struct baycom_state *)dev->priv;

	printk(KERN_DEBUG "baycom_par: %s: why am I being woken up?\n", dev->name);
	if (!parport_claim(bc->pdev))
		printk(KERN_DEBUG "baycom_par: %s: I'm broken.\n", dev->name);
}

/* --------------------------------------------------------------------- */

static int par96_open(struct device *dev)
{
	struct baycom_state *bc = (struct baycom_state *)dev->priv;
	struct parport *pp = parport_enumerate();

	if (!dev || !bc)
		return -ENXIO;
	while (pp && pp->base != dev->base_addr) 
		pp = pp->next;
	if (!pp) {
		printk(KERN_ERR "baycom_par: parport at 0x%lx unknown\n", dev->base_addr);
		return -ENXIO;
	}
	if (pp->irq < 0) {
		printk(KERN_ERR "baycom_par: parport at 0x%lx has no irq\n", pp->base);
		return -ENXIO;
	}
	memset(&bc->modem, 0, sizeof(bc->modem));
	bc->hdrv.par.bitrate = 9600;
	if (!(bc->pdev = parport_register_device(pp, dev->name, NULL, par96_wakeup, 
						 par96_interrupt, PARPORT_DEV_EXCL, dev))) {
		printk(KERN_ERR "baycom_par: cannot register parport at 0x%lx\n", pp->base);
		return -ENXIO;
	}
	if (parport_claim(bc->pdev)) {
		printk(KERN_ERR "baycom_par: parport at 0x%lx busy\n", pp->base);
		parport_unregister_device(bc->pdev);
		return -EBUSY;
	}
	dev->irq = pp->irq;
	/* bc->pdev->port->ops->change_mode(bc->pdev->port, PARPORT_MODE_PCSPP);  not yet implemented */
        bc->hdrv.par.bitrate = 9600;
	/* switch off PTT */
	outb(PAR96_PTT | PAR97_POWER, LPT_DATA(dev));
	/*bc->pdev->port->ops->enable_irq(bc->pdev->port);  not yet implemented */
        outb(LPT_IRQ_ENABLE, LPT_CONTROL(dev));	
	printk(KERN_INFO "%s: par96 at iobase 0x%lx irq %u options 0x%x\n",
	       bc_drvname, dev->base_addr, dev->irq, bc->options);
	MOD_INC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */

static int par96_close(struct device *dev)
{
	struct baycom_state *bc = (struct baycom_state *)dev->priv;

	if (!dev || !bc)
		return -EINVAL;
	/* disable interrupt */
        outb(0, LPT_CONTROL(dev));
	/*bc->pdev->port->ops->disable_irq(bc->pdev->port);  not yet implemented */
	/* switch off PTT */
	outb(PAR96_PTT | PAR97_POWER, LPT_DATA(dev));
	parport_release(bc->pdev);
	parport_unregister_device(bc->pdev);
	printk(KERN_INFO "%s: close par96 at iobase 0x%lx irq %u\n",
	       bc_drvname, dev->base_addr, dev->irq);
	MOD_DEC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */
/*
 * ===================== hdlcdrv driver interface =========================
 */

static int baycom_ioctl(struct device *dev, struct ifreq *ifr,
			struct hdlcdrv_ioctl *hi, int cmd);

/* --------------------------------------------------------------------- */

static struct hdlcdrv_ops par96_ops = {
	bc_drvname,
	bc_drvinfo,
	par96_open,
	par96_close,
	baycom_ioctl
};

/* --------------------------------------------------------------------- */

static int baycom_setmode(struct baycom_state *bc, const char *modestr)
{
	if (!strncmp(modestr, "picpar", 6))
		bc->options = 0;
	else if (!strncmp(modestr, "par96", 5))
		bc->options = BAYCOM_OPTIONS_SOFTDCD;
	else
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
		strcpy(hi->data.modename, bc->options ? "par96" : "picpar");
		if (copy_to_user(ifr->ifr_data, hi, sizeof(struct hdlcdrv_ioctl)))
			return -EFAULT;
		return 0;

	case HDLCDRVCTL_SETMODE:
		if (dev->start || !suser())
			return -EACCES;
		hi->data.modename[sizeof(hi->data.modename)-1] = '\0';
		return baycom_setmode(bc, hi->data.modename);

	case HDLCDRVCTL_MODELIST:
		strcpy(hi->data.modename, "par96,picpar");
		if (copy_to_user(ifr->ifr_data, hi, sizeof(struct hdlcdrv_ioctl)))
			return -EFAULT;
		return 0;

	case HDLCDRVCTL_MODEMPARMASK:
		return HDLCDRV_PARMASK_IOBASE;

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

__initfunc(int baycom_par_init(void))
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
		sprintf(ifname, "bcp%d", i);

		if (!baycom_ports[i].mode)
			set_hw = 0;
		if (!set_hw)
			baycom_ports[i].iobase = 0;
		j = hdlcdrv_register_hdlcdrv(dev, &par96_ops,
					     sizeof(struct baycom_state),
					     ifname, baycom_ports[i].iobase, 0, 0);
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
static const char *mode[NR_PORTS] = { "picpar", };
static int iobase[NR_PORTS] = { 0x378, };

#if LINUX_VERSION_CODE >= 0x20115

MODULE_PARM(mode, "1-" __MODULE_STRING(NR_PORTS) "s");
MODULE_PARM_DESC(mode, "baycom operating mode; eg. par96 or picpar");
MODULE_PARM(iobase, "1-" __MODULE_STRING(NR_PORTS) "i");
MODULE_PARM_DESC(iobase, "baycom io base address");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("Baycom par96 and picpar amateur radio modem driver");

#endif

__initfunc(int init_module(void))
{
	int i;

	for (i = 0; (i < NR_PORTS) && (mode[i]); i++) {
		baycom_ports[i].mode = mode[i];
		baycom_ports[i].iobase = iobase[i];
	}
	if (i < NR_PORTS-1)
		baycom_ports[i+1].mode = NULL;
	return baycom_par_init();
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
 * format: baycom_par=io,mode
 * mode: par96,picpar
 */

__initfunc(void baycom_par_setup(char *str, int *ints))
{
	int i;

	for (i = 0; (i < NR_PORTS) && (baycom_ports[i].mode); i++);
	if ((i >= NR_PORTS) || (ints[0] < 1)) {
		printk(KERN_INFO "%s: too many or invalid interface "
		       "specifications\n", bc_drvname);
		return;
	}
	baycom_ports[i].mode = str;
	baycom_ports[i].iobase = ints[1];
	if (i < NR_PORTS-1)
		baycom_ports[i+1].mode = NULL;
}

#endif /* MODULE */
/* --------------------------------------------------------------------- */
