/*****************************************************************************/

/*
 *      main.c  --  Linux soundcard HF FSK driver.
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
 *  Command line options (insmod command line)
 *
 *  History:
 *   0.1  15.04.97  Adapted from baycom.c and made network driver interface
 *   0.2  05.07.97  All floating point stuff thrown out due to Linus' rantings :)
 *
 */

/*****************************************************************************/
      

#include <linux/config.h> /* for CONFIG_HFMODEM_WSS and CONFIG_HFMODEM_SBC */
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/hfmodem.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>

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

#if LINUX_VERSION_CODE >= 0x20123
#include <linux/init.h>
#else
#define __init
#define __initdata
#define __initfunc(x) x
#endif

/* --------------------------------------------------------------------- */

/*static*/ const char hfmodem_drvname[] = "hfmodem";
static const char hfmodem_drvinfo[] = KERN_INFO "hfmodem: (C) 1997 Thomas Sailer, HB9JNX/AE4WA\n"
KERN_INFO "hfmodem: version 0.2 compiled " __TIME__ " " __DATE__ "\n";

/* --------------------------------------------------------------------- */
/*
 * currently we support only one device
 */

struct hfmodem_state hfmodem_state[NR_DEVICE];

/* --------------------------------------------------------------------- */
/*
 * ===================== port checking routines ========================
 */


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

#define MIDI_DATA(iobase)     (iobase)
#define MIDI_STATUS(iobase)   (iobase+1)
#define MIDI_READ_FULL 0x80   /* attention: negative logic!! */
#define MIDI_WRITE_EMPTY 0x40 /* attention: negative logic!! */

#define MIDI_EXTENT 2

#define SP_SER  1
#define SP_PAR  2
#define SP_MIDI 4

/* --------------------------------------------------------------------- */

static void parptt_wakeup(void *handle)
{
        struct hfmodem_state *dev = (struct hfmodem_state *)handle;

	printk(KERN_DEBUG "%s: parptt: why am I being woken up?\n", hfmodem_drvname);
	if (!parport_claim(dev->ptt_out.pardev))
		printk(KERN_DEBUG "%s: parptt: I'm broken.\n", hfmodem_drvname);
}

/* --------------------------------------------------------------------- */
__initfunc(static int check_lpt(struct hfmodem_state *dev, unsigned int iobase))
{
	struct parport *pp = parport_enumerate();

	while (pp && pp->base != iobase)
		pp = pp->next;
	if (!pp)
		return 0;
	if (!(dev->ptt_out.pardev = parport_register_device(pp, hfmodem_drvname, NULL, parptt_wakeup, 
							    NULL, PARPORT_DEV_EXCL, dev)))
		return 0;
	return 1;
}

/* --------------------------------------------------------------------- */

enum uart { c_uart_unknown, c_uart_8250, c_uart_16450, c_uart_16550, c_uart_16550A };
static const char *uart_str[] __initdata = { "unknown", "8250", "16450", "16550", "16550A" };

__initfunc(static enum uart check_uart(unsigned int iobase))
{
        unsigned char b1,b2,b3;
        enum uart u;
        enum uart uart_tab[] = { c_uart_16450, c_uart_unknown, c_uart_16550, c_uart_16550A };

        if (iobase <= 0 || iobase > 0x1000-SER_EXTENT)
                return c_uart_unknown;
        if (check_region(iobase, SER_EXTENT))
                return c_uart_unknown;
        b1 = inb(UART_MCR(iobase));
        outb(b1 | 0x10, UART_MCR(iobase));      /* loopback mode */
        b2 = inb(UART_MSR(iobase));
        outb(0x1a, UART_MCR(iobase));
        b3 = inb(UART_MSR(iobase)) & 0xf0;
        outb(b1, UART_MCR(iobase));        /* restore old values */
        outb(b2, UART_MSR(iobase));
        if (b3 != 0x90)
                return c_uart_unknown;
        inb(UART_RBR(iobase));
        inb(UART_RBR(iobase));
        outb(0x01, UART_FCR(iobase));           /* enable FIFOs */
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

__initfunc(static int check_midi(unsigned int iobase))
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

static void output_status(struct hfmodem_state *dev, int ptt)
{
        int dcd = 0;

	ptt = !!ptt;
        if (dev->ptt_out.flags & SP_SER) {
                outb(dcd | (ptt << 1), UART_MCR(dev->ptt_out.seriobase));
                outb(0x40 & (-ptt), UART_LCR(dev->ptt_out.seriobase));
        }
        if (dev->ptt_out.flags & SP_PAR) {
                outb(ptt | (dcd << 1), LPT_DATA(dev->ptt_out.pariobase));
        }
        if (dev->ptt_out.flags & SP_MIDI && ptt) {
                outb(0, MIDI_DATA(dev->ptt_out.midiiobase));
        }
}

/* --------------------------------------------------------------------- */

__initfunc(static void output_check(struct hfmodem_state *dev))
{
        enum uart u = c_uart_unknown;

        if (((u = check_uart(dev->ptt_out.seriobase))) != c_uart_unknown)
		printk(KERN_INFO "%s: PTT output: uart found at address 0x%x type %s\n",
		       hfmodem_drvname, dev->ptt_out.seriobase, uart_str[u]);
	else {
		if (dev->ptt_out.seriobase > 0)
			printk(KERN_WARNING "%s: PTT output: no uart found at address 0x%x\n",
			       hfmodem_drvname, dev->ptt_out.seriobase);
		dev->ptt_out.seriobase = 0;
	}
        if (check_lpt(dev, dev->ptt_out.pariobase)) 
		printk(KERN_INFO "%s: PTT output: parallel port found at address 0x%x\n",
		       hfmodem_drvname, dev->ptt_out.pariobase);
	else {
		if (dev->ptt_out.pariobase > 0)
			printk(KERN_WARNING "%s: PTT output: no parallel port found at address 0x%x\n",
			       hfmodem_drvname, dev->ptt_out.pariobase);
		dev->ptt_out.pariobase = 0;
		dev->ptt_out.pardev = NULL;
	}
	if (dev->ptt_out.midiiobase > 0 && dev->ptt_out.midiiobase <= 0x1000-MIDI_EXTENT &&
            check_midi(dev->ptt_out.midiiobase))
		printk(KERN_INFO "%s: PTT output: midi port found at address 0x%x\n",
		       hfmodem_drvname, dev->ptt_out.midiiobase);
	else {
		if (dev->ptt_out.midiiobase > 0)
			printk(KERN_WARNING "%s: PTT output: no midi port found at address 0x%x\n",
			       hfmodem_drvname, dev->ptt_out.midiiobase);
		dev->ptt_out.midiiobase = 0;
	}
}

/* --------------------------------------------------------------------- */

static void output_open(struct hfmodem_state *dev)
{
        dev->ptt_out.flags = 0;
        if (dev->ptt_out.seriobase > 0) {
		if (!check_region(dev->ptt_out.seriobase, SER_EXTENT)) {
			request_region(dev->ptt_out.seriobase, SER_EXTENT, "hfmodem ser ptt");
			dev->ptt_out.flags |= SP_SER;
			outb(0, UART_IER(dev->ptt_out.seriobase));
			/* 5 bits, 1 stop, no parity, no break, Div latch access */
			outb(0x80, UART_LCR(dev->ptt_out.seriobase));
			outb(0, UART_DLM(dev->ptt_out.seriobase));
			outb(1, UART_DLL(dev->ptt_out.seriobase)); /* as fast as possible */
			/* LCR and MCR set by output_status */
		} else
			printk(KERN_WARNING "%s: PTT output: serial port at 0x%x busy\n",
			       hfmodem_drvname, dev->ptt_out.seriobase);
	}
        if (dev->ptt_out.pariobase > 0) {
		if (parport_claim(dev->ptt_out.pardev)) 
			printk(KERN_WARNING "%s: PTT output: parallel port at 0x%x busy\n",
			       hfmodem_drvname, dev->ptt_out.pariobase);
		else 
			dev->ptt_out.flags |= SP_PAR;
	}
        if (dev->ptt_out.midiiobase > 0) {
		if (!check_region(dev->ptt_out.midiiobase, MIDI_EXTENT)) {
			request_region(dev->ptt_out.midiiobase, MIDI_EXTENT, "hfmodem midi ptt");
			dev->ptt_out.flags |= SP_MIDI;
		} else
			printk(KERN_WARNING "%s: PTT output: midi port at 0x%x busy\n",
			       hfmodem_drvname, dev->ptt_out.midiiobase);
	}
	output_status(dev, 0);
        printk(KERN_INFO "%s: PTT output:", hfmodem_drvname);
        if (dev->ptt_out.flags & SP_SER)
                printk(" serial interface at 0x%x", dev->ptt_out.seriobase);
        if (dev->ptt_out.flags & SP_PAR)
                printk(" parallel interface at 0x%x", dev->ptt_out.pariobase);
        if (dev->ptt_out.flags & SP_MIDI)
                printk(" mpu401 (midi) interface at 0x%x", dev->ptt_out.midiiobase);
        if (!dev->ptt_out.flags)
                printk(" none");
        printk("\n");
}

/* --------------------------------------------------------------------- */

static void output_close(struct hfmodem_state *dev)
{
        /* release regions used for PTT output */
        output_status(dev, 0);
        if (dev->ptt_out.flags & SP_SER)
                release_region(dev->ptt_out.seriobase, SER_EXTENT);
        if (dev->ptt_out.flags & SP_PAR)
		parport_release(dev->ptt_out.pardev);
        if (dev->ptt_out.flags & SP_MIDI)
                release_region(dev->ptt_out.midiiobase, MIDI_EXTENT);
        dev->ptt_out.flags = 0;
}

/* --------------------------------------------------------------------- */

#define INC_SAMPLE   (1000000/HFMODEM_SRATE)
#define INC_FRAGMENT (HFMODEM_FRAGSAMPLES*1000000/HFMODEM_SRATE)
#define SIZE         (HFMODEM_FRAGSAMPLES*HFMODEM_NUMFRAGS)

static void hfmodem_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct hfmodem_state *dev = (struct hfmodem_state *)dev_id;
	unsigned int dmaptr;
	__s16 *s;
	unsigned int curfrag, nfrags;
	int i;
	hfmodem_time_t l1time;

	dmaptr = dev->scops->intack(dev);
	l1time = hfmodem_refclock_current(dev, ((SIZE+dmaptr-dev->dma.last_dmaptr) % SIZE) * 
					  INC_SAMPLE, 1);
	curfrag = (dev->dma.last_dmaptr = dmaptr) / HFMODEM_FRAGSAMPLES;
	l1time -= INC_SAMPLE * (SIZE+dmaptr-dev->dma.fragptr*HFMODEM_FRAGSAMPLES) % SIZE;
	sti();
	/*
	 * handle receiving
	 */
	if (dev->dma.ptt_frames <= 0) {
		while (dev->dma.fragptr != curfrag) {
			if (dev->dma.fragptr < HFMODEM_EXCESSFRAGS) {
				s = dev->dma.buf + SIZE + HFMODEM_FRAGSAMPLES * dev->dma.fragptr;
				memcpy(s, s - SIZE, HFMODEM_FRAGSIZE);
			} else
				s = dev->dma.buf + HFMODEM_FRAGSAMPLES * dev->dma.fragptr;
			if (dev->sbuf.kbuf && dev->sbuf.kptr && dev->sbuf.rem > 0) {
				i = HFMODEM_FRAGSAMPLES;
				if (i > dev->sbuf.rem)
					i = dev->sbuf.rem;
				memcpy(dev->sbuf.kptr, s, i * sizeof(s[0]));
				dev->sbuf.rem -= i;
				dev->sbuf.kptr += i;
			}
			hfmodem_input_samples(dev, l1time, INC_SAMPLE, s);
			l1time += INC_FRAGMENT;
			dev->dma.fragptr++;
			if (dev->dma.fragptr >= HFMODEM_NUMFRAGS)
				dev->dma.fragptr = 0;
		}
		/*
		 * check for output
		 */
		if (hfmodem_next_tx_event(dev, l1time) > (long)INC_FRAGMENT/2)
			goto int_return;
		/*
		 * start output
		 */
		output_status(dev, 1);
		dev->scops->prepare_output(dev);
		dev->dma.last_dmaptr = 0;
		/*
		 * clock adjust
		 */
		l1time = hfmodem_refclock_current(dev, 0, 0);
		/*
		 * fill first two fragments
		 */
		dev->dma.ptt_frames = 1;
		for (i = 0; i < 2 && i < HFMODEM_NUMFRAGS; i++)
			if (hfmodem_output_samples(dev, l1time+i*INC_FRAGMENT, INC_SAMPLE, 
						   dev->dma.buf+i*HFMODEM_FRAGSAMPLES))
				dev->dma.ptt_frames = i + 1;
		dev->dma.lastfrag = 0;
		dev->scops->trigger_output(dev);
		/*
		 * finish already pending rx requests
		 */
		hfmodem_finish_pending_rx_requests(dev);
		goto int_return;
	}
	/*
	 * handle transmitting
	 */
	nfrags = HFMODEM_NUMFRAGS + curfrag - dev->dma.lastfrag;
	dev->dma.lastfrag = curfrag;
	if (nfrags >= HFMODEM_NUMFRAGS)
		nfrags -= HFMODEM_NUMFRAGS;
	dev->dma.ptt_frames -= nfrags;
	if (dev->dma.ptt_frames < 0)
		dev->dma.ptt_frames = 0;
	while (dev->dma.ptt_frames < HFMODEM_NUMFRAGS && dev->dma.ptt_frames < 4 && 
	       hfmodem_output_samples(dev, l1time+dev->dma.ptt_frames*INC_FRAGMENT, 
				      INC_SAMPLE, dev->dma.buf + HFMODEM_FRAGSAMPLES * 
				      ((curfrag + dev->dma.ptt_frames) % HFMODEM_NUMFRAGS)))
		dev->dma.ptt_frames++;
	if (dev->dma.ptt_frames > 0)
		goto int_return;
	/* 
	 * start receiving
	 */
	output_status(dev, 0);
	dev->dma.last_dmaptr = 0;
	dev->dma.lastfrag = 0;
	dev->dma.fragptr = 0;
	dev->dma.ptt_frames = 0;
	dev->scops->prepare_input(dev);
	dev->scops->trigger_input(dev);
	hfmodem_refclock_current(dev, 0, 0);  /* needed to reset the time difference */
int_return:
	hfmodem_wakeup(dev);
}

/* --------------------------------------------------------------------- */

static int hfmodem_close(struct inode *inode, struct file *file)
{
	struct hfmodem_state *dev = &hfmodem_state[0];

	if (!dev->active)
		return -EPERM;
	dev->active = 0;
	dev->scops->stop(dev);
        free_irq(dev->io.irq, dev);        
	disable_dma(dev->io.dma);
	free_dma(dev->io.dma);
        release_region(dev->io.base_addr, dev->scops->extent);
	kfree_s(dev->dma.buf, HFMODEM_FRAGSIZE * (HFMODEM_NUMFRAGS+HFMODEM_EXCESSFRAGS));
	hfmodem_clear_rq(dev);
	if (dev->sbuf.kbuf) {
		kfree_s(dev->sbuf.kbuf, dev->sbuf.size);
		dev->sbuf.kbuf = dev->sbuf.kptr = NULL;
		dev->sbuf.size = dev->sbuf.rem = 0;
	}
	output_close(dev);
	MOD_DEC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */

static int hfmodem_open(struct inode *inode, struct file *file)
{
	struct hfmodem_state *dev = &hfmodem_state[0];

	if (dev->active)
		return -EBUSY;
	if (!dev->scops)
		return -EPERM;
	/*
	 * clear vars
	 */
	memset(&dev->l1, 0, sizeof(dev->l1));
	dev->dma.last_dmaptr = 0;
	dev->dma.lastfrag = 0;
	dev->dma.fragptr = 0;
	dev->dma.ptt_frames = 0;
	/*
	 * allocate memory
	 */
	if (!(dev->dma.buf = kmalloc(HFMODEM_FRAGSIZE * (HFMODEM_NUMFRAGS+HFMODEM_EXCESSFRAGS), GFP_KERNEL | GFP_DMA)))
		return -ENOMEM;
	/*
	 * allocate resources
	 */
        if (request_dma(dev->io.dma, hfmodem_drvname)) {
                kfree_s(dev->dma.buf, HFMODEM_FRAGSIZE * (HFMODEM_NUMFRAGS+HFMODEM_EXCESSFRAGS));
                return -EBUSY;
        }
        if (request_irq(dev->io.irq, hfmodem_interrupt, SA_INTERRUPT, hfmodem_drvname, dev)) {
                free_dma(dev->io.dma);
                kfree_s(dev->dma.buf, HFMODEM_FRAGSIZE * (HFMODEM_NUMFRAGS+HFMODEM_EXCESSFRAGS));
                return -EBUSY;
        }
        request_region(dev->io.base_addr, dev->scops->extent, hfmodem_drvname);
	
	/* clear requests */
      	dev->active++;
	MOD_INC_USE_COUNT;
	hfmodem_refclock_init(dev);
	output_open(dev);
	dev->scops->init(dev);
	dev->scops->prepare_input(dev);
	dev->scops->trigger_input(dev);
	return 0;
}

/* --------------------------------------------------------------------- */

static struct file_operations hfmodem_fops = {
	NULL,		    /* hfmodem_seek */
	NULL,               /* hfmodem_read */
	NULL,               /* hfmodem_write */
	NULL, 		    /* hfmodem_readdir */
#if LINUX_VERSION_CODE >= 0x20100
	hfmodem_poll,       /* hfmodem_poll */
#else 
	hfmodem_select,     /* hfmodem_select */
#endif
	hfmodem_ioctl,      /* hfmodem_ioctl */
	NULL,		    /* hfmodem_mmap */
	hfmodem_open,       /* hfmodem_open */
	NULL,		    /* flush */
	hfmodem_close,      /* hfmodem_close */
	NULL,               /* hfmodem_fsync */
	NULL,               /* hfmodem_fasync */
	NULL,               /* hfmodem_check_media_change */
	NULL                /* hfmodem_revalidate */
};

/* --------------------------------------------------------------------- */

static struct miscdevice hfmodem_device = {
	HFMODEM_MINOR, hfmodem_drvname, &hfmodem_fops
};

/* --------------------------------------------------------------------- */

#ifdef MODULE

/*
 * Command line parameters
 */

static int hw = 0;
static unsigned int iobase = 0x220;
static unsigned int irq = 7;
static unsigned int dma = 1;

static unsigned int serio = 0;
static unsigned int pario = 0;
static unsigned int midiio = 0;

#if LINUX_VERSION_CODE >= 0x20115

MODULE_PARM(hw, "i");
MODULE_PARM_DESC(hw, "hardware type: 0=SBC, 1=WSS");
MODULE_PARM(iobase, "i");
MODULE_PARM_DESC(iobase, "io base address");
MODULE_PARM(irq, "i");
MODULE_PARM_DESC(irq, "interrupt number");
MODULE_PARM(dma, "i");
MODULE_PARM_DESC(dma, "dma number (>=4 for SB16/32/64/etc, <=3 for the rest)");
MODULE_PARM(serio, "i");
MODULE_PARM_DESC(serio, "address of serial port to output PTT");
MODULE_PARM(pario, "i");
MODULE_PARM_DESC(pario, "address of parallel port to output PTT");
MODULE_PARM(midiio, "i");
MODULE_PARM_DESC(midiio, "address of midi (MPU401) port to output PTT");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("HF FSK modem code");

/* these are the module parameters from refclock.c */

MODULE_PARM(scale_tvusec, "i");
MODULE_PARM_DESC(scale_tvusec, "Scaling value for the tv_usec field (can be obta
ined by refclock)");

#ifdef __i386__
MODULE_PARM(scale_rdtsc, "i");
MODULE_PARM_DESC(scale_rdtsc, "Scaling value for the rdtsc counter (can be obtai
ned by refclock)");
MODULE_PARM(rdtsc_ok, "i");
MODULE_PARM_DESC(rdtsc_ok, "Set to 0 to disable the use of the rdtsc instruction
");
#endif /* __i386__ */

#endif

__initfunc(int init_module(void))
{
	int i;

	printk(hfmodem_drvinfo);
	memset(hfmodem_state, 0, sizeof(hfmodem_state));
        memset(hfmodem_correlator_cache, 0, sizeof(hfmodem_correlator_cache));
	hfmodem_state[0].io.base_addr = iobase;
	hfmodem_state[0].io.irq = irq;
	hfmodem_state[0].io.dma = dma;
	hfmodem_state[0].ptt_out.seriobase = serio;
	hfmodem_state[0].ptt_out.pariobase = pario;
	hfmodem_state[0].ptt_out.midiiobase = midiio;
	hfmodem_refclock_probe();
	output_check(&hfmodem_state[0]);
#if defined(CONFIG_HFMODEM_WSS) && defined(CONFIG_HFMODEM_SBC)
        if (hw) 
                i = hfmodem_wssprobe(&hfmodem_state[0]);
        else
                i = hfmodem_sbcprobe(&hfmodem_state[0]);
#else
        i = -EINVAL;
#ifdef CONFIG_HFMODEM_WSS
        i = hfmodem_wssprobe(&hfmodem_state[0]);
#endif
#ifdef CONFIG_HFMODEM_SBC
        i = hfmodem_sbcprobe(&hfmodem_state[0]);
#endif
#endif
	if (i)
		return i;
	if ((i =  misc_register(&hfmodem_device))) {
		printk(KERN_ERR "%s: cannot register misc device\n", hfmodem_drvname);
		return i;
	}
	return 0;
}

void cleanup_module(void)
{
	struct hfmodem_state *dev = &hfmodem_state[0];

	if (dev->ptt_out.pariobase > 0)
		parport_unregister_device(dev->ptt_out.pardev);
	misc_deregister(&hfmodem_device);
}

#else /* MODULE */
/* --------------------------------------------------------------------- */

static int hw = 0;

__initfunc(void hfmodem_setup(char *str, int *ints))
{
        if (ints[0] < 7) {
                printk(KERN_WARNING "%s: setup: too few parameters\n", hfmodem_drvname);
                return;
        }       
        memset(hfmodem_state, 0, sizeof(hfmodem_state));
        memset(hfmodem_correlator_cache, 0, sizeof(hfmodem_correlator_cache));
        hw = ints[1];
        hfmodem_state[0].io.base_addr = ints[2];
        hfmodem_state[0].io.irq = ints[3];
        hfmodem_state[0].io.dma = ints[4];
        if (ints[0] >= 8)
                hfmodem_state[0].ptt_out.seriobase = ints[5];
        if (ints[0] >= 9)
                hfmodem_state[0].ptt_out.pariobase = ints[6];
        if (ints[0] >= 10)
                hfmodem_state[0].ptt_out.midiiobase = ints[7];
        hfmodem_refclock_setscale(ints[ints[0]-2], ints[ints[0]-1], ints[ints[0]]);
}

__initfunc(void hfmodem_init(void))
{
        int i;

	printk(hfmodem_drvinfo);
        hfmodem_refclock_probe();
        output_check(&hfmodem_state[0]);
#if defined(CONFIG_HFMODEM_WSS) && defined(CONFIG_HFMODEM_SBC)
        if (hw) 
                i = hfmodem_wssprobe(&hfmodem_state[0]);
        else
                i = hfmodem_sbcprobe(&hfmodem_state[0]);
#else
        i = -EINVAL;
#ifdef CONFIG_HFMODEM_WSS
        i = hfmodem_wssprobe(&hfmodem_state[0]);
#endif
#ifdef CONFIG_HFMODEM_SBC
        i = hfmodem_sbcprobe(&hfmodem_state[0]);
#endif
#endif
        if (i) {
                printk(KERN_ERR "%s: soundcard probe failed\n", hfmodem_drvname);
                return;
        }
        if ((i =  misc_register(&hfmodem_device))) {
                printk(KERN_ERR "%s: cannot register misc device\n", hfmodem_drvname);
                return;
        }
}

/* --------------------------------------------------------------------- */
#endif /* MODULE */
