#undef PT_DEBUG 1
/*
 * pt.c: Linux device driver for the Gracilis PackeTwin.
 * Copyright (c) 1995 Craig Small VK2XLZ (vk2xlz@vk2xlz.ampr.org.)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge MA 02139, USA.
 *
 * This driver is largely based upon the PI driver by David Perry.
 *
 * Revision History
 * 23/02/95 cs  Started again on driver, last one scrapped
 * 27/02/95 cs  Program works, we have chan A only.  Tx stays on
 * 28/02/95 cs  Fix Tx problem (& TxUIE instead of | )
 *		Fix Chan B Tx timer problem, used TMR2 instead of TMR1
 * 03/03/95 cs  Painfully found out (after 3 days) SERIAL_CFG is write only
 *              created image of it and DMA_CFG
 * 21/06/95 cs  Upgraded to suit PI driver 0.8 ALPHA
 * 22/08/95 cs	Changed it all around to make it like pi driver
 * 23/08/95 cs  It now works, got caught again by TMR2 and we must have
 *				auto-enables for daughter boards.
 * 07/10/95 cs  Fixed for 1.3.30 (hopefully)
 * 26/11/95 cs  Fixed for 1.3.43, ala 29/10 for pi2.c by ac
 * 21/12/95 cs  Got rid of those nasty warnings when compiling, for 1.3.48
 * 08/08/96 jsn Convert to use as a module. Removed send_kiss, empty_scc and
 *		pt_loopback functions - they were unused.
 * 13/12/96 jsn Fixed to match Linux networking changes.
 */

/*
 * default configuration of the PackeTwin,
 * ie What Craig uses his PT for.
 */
#define PT_DMA 3

#define DEF_A_SPEED	4800		/* 4800 baud */
#define DEF_A_TXDELAY	350		/* 350 mS */
#define DEF_A_PERSIST	64		/* 25% persistence */
#define DEF_A_SLOTIME	10		/* 10 mS */
#define DEF_A_SQUELDELAY 30		/* 30 mS */
#define DEF_A_CLOCKMODE	0		/* Normal clock mode */
#define DEF_A_NRZI		1		/* NRZI mode */

#define DEF_B_SPEED	0		/* 0 means external clock */
#define DEF_B_TXDELAY	250		/* 250 mS */
#define DEF_B_PERSIST	64		/* 25% */
#define DEF_B_SLOTIME	10		/* 10 mS */
#define DEF_B_SQUELDELAY 30		/* 30 mS */
#define DEF_B_CLOCKMODE 0 		/* Normal clock mode ?!? */
#define DEF_B_NRZI		1		/* NRZI mode */


#define	PARAM_TXDELAY	1
#define	PARAM_PERSIST	2
#define	PARAM_SLOTTIME	3
#define	PARAM_FULLDUP	5
#define	PARAM_HARDWARE	6
#define	PARAM_RETURN	255

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/if_arp.h>
#include <linux/pt.h>
#include <linux/init.h>
#include "z8530.h"
#include <net/ax25.h>

struct mbuf {
    struct mbuf *next;
    int cnt;
    char data[0];
};

/*
 * The actual PT devices we will use
 */
static int pt0_preprobe(struct device *dev) {return 0;} /* Dummy probe function */
static struct device pt0a = { "pt0a", 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, pt0_preprobe };
static struct device pt0b = { "pt0b", 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, pt0_preprobe };

/* Ok, they shouldn't be here, but both channels share them */
/* The Images of the Serial and DMA config registers */
static unsigned char pt_sercfg = 0;
static unsigned char pt_dmacfg = 0;

/* The number of IO ports used by the card */
#define PT_TOTAL_SIZE   16

/* Index to functions, as function prototypes. */

static int pt_probe(struct device *dev);
static int pt_open(struct device *dev);
static int pt_send_packet(struct sk_buff *skb, struct device *dev);
static void pt_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int pt_close(struct device *dev);
static int pt_ioctl(struct device *dev, struct ifreq *ifr, int cmd);
static struct net_device_stats *pt_get_stats(struct device *dev);
static void pt_rts(struct pt_local *lp, int x);
static void pt_rxisr(struct device *dev);
static void pt_txisr(struct pt_local *lp);
static void pt_exisr(struct pt_local *lp);
static void pt_tmrisr(struct pt_local *lp);
static char *get_dma_buffer(unsigned long *mem_ptr);
static int valid_dma_page(unsigned long addr, unsigned long dev_buffsize);
static int hw_probe(int ioaddr);
static void tdelay(struct pt_local *lp, int time);
static void chipset_init(struct device *dev);

static char ax25_bcast[7] =
{'Q' << 1, 'S' << 1, 'T' << 1, ' ' << 1, ' ' << 1, ' ' << 1, '0' << 1};
static char ax25_test[7] =
{'L' << 1, 'I' << 1, 'N' << 1, 'U' << 1, 'X' << 1, ' ' << 1, '1' << 1};



static int ext2_secrm_seed = 152;

static inline unsigned char random(void)
{
    return (unsigned char) (ext2_secrm_seed = ext2_secrm_seed * 60691 + 1);
}

static inline void wrtscc(int cbase, int ctl, int sccreg, unsigned char val)
{
    outb_p(sccreg, ctl);        /* Select register */
    outb_p(val, ctl);           /* Output value */
}

static inline unsigned char rdscc(int cbase, int ctl, int sccreg)
{
    unsigned char retval;

    outb_p(sccreg, ctl);        /* Select register */
    retval = inb_p(ctl);
    return retval;
}

static void switchbuffers(struct pt_local *lp)
{
    if (lp->rcvbuf == lp->rxdmabuf1)
	lp->rcvbuf = lp->rxdmabuf2;
    else
	lp->rcvbuf = lp->rxdmabuf1;
}

static void hardware_send_packet(struct pt_local *lp, struct sk_buff *skb)
{
	char kickflag;
	unsigned long flags;
	char *ptr;
	struct device *dev;

	/* First, let's see if this packet is actually a KISS packet */
	ptr = skb->data;
	if (ptr[0] != 0 && skb->len >= 2)
	{
#ifdef PT_DEBUG
		printk(KERN_DEBUG "PT: Rx KISS... Control = %d, value = %d.\n", ptr[0], (skb->len > 1? ptr[1] : -1));
#endif
		/* Kludge to get device */
		if ((struct pt_local*)(&pt0b.priv) == lp)
			dev = &pt0b;
		else
			dev = &pt0a;
		switch(ptr[0])
		{

			case PARAM_TXDELAY:
				/*TxDelay is in 10mS increments */
				lp->txdelay = ptr[1] * 10;
				break;
			case PARAM_PERSIST:
				lp->persist = ptr[1];
				break;
			case PARAM_SLOTTIME:
				lp->slotime = ptr[1];
				break;
			case PARAM_FULLDUP:
				/* Yeah right, you wish!  Fullduplex is a little while to
				 * go folks, but this is how you fire it up
				 */
				break;
			/* Perhaps we should have txtail here?? */
		} /*switch */
		return;
	}

	lp->stats.tx_packets++;
	lp->stats.tx_bytes+=skb->len;
	save_flags(flags);
	cli();
	kickflag = (skb_peek(&lp->sndq) == NULL) && (lp->sndbuf == NULL);
	restore_flags(flags);

#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: hardware_send_packet(): kickflag = %d (%d).\n", kickflag, lp->base & CHANA);
#endif
	skb_queue_tail(&lp->sndq, skb);
	if (kickflag) 
	{
        /* Simulate interrupt to transmit */
        	if (lp->dmachan)
			pt_txisr(lp);
		else 
		{
            		save_flags(flags);
	           	cli();
            		if (lp->tstate == IDLE)
                		pt_txisr(lp);
            		restore_flags(flags);
		}
	}
} /* hardware_send_packet() */

static void setup_rx_dma(struct pt_local *lp)
{
	unsigned long flags;
	int cmd;
	unsigned long dma_abs;
	unsigned char dmachan;

	save_flags(flags);
	cli();

	dma_abs = (unsigned long) (lp->rcvbuf->data);
	dmachan = lp->dmachan;
	cmd = lp->base + CTL;

	if(!valid_dma_page(dma_abs, DMA_BUFF_SIZE + sizeof(struct mbuf)))
		panic("PI: RX buffer violates DMA boundary!");

	/* Get ready for RX DMA */
	wrtscc(lp->cardbase, cmd, R1, WT_FN_RDYFN | WT_RDY_RT | INT_ERR_Rx | EXT_INT_ENAB);

	disable_dma(dmachan);
	clear_dma_ff(dmachan);

	/*
	 *	Set DMA mode register to single transfers, incrementing address,
	 *	auto init, writes
	 */

	set_dma_mode(dmachan, DMA_MODE_READ | 0x10);
	set_dma_addr(dmachan, dma_abs);
	set_dma_count(dmachan, lp->bufsiz);
	enable_dma(dmachan);

	/*
	 *	If a packet is already coming in, this line is supposed to
	 *	avoid receiving a partial packet.
	 */

	wrtscc(lp->cardbase, cmd, R0, RES_Rx_CRC);

	/* Enable RX dma */
	wrtscc(lp->cardbase, cmd, R1,
		WT_RDY_ENAB | WT_FN_RDYFN | WT_RDY_RT | INT_ERR_Rx | EXT_INT_ENAB);

	restore_flags(flags);
}

static void setup_tx_dma(struct pt_local *lp, int length)
{
    unsigned long dma_abs;
    unsigned long flags;
    unsigned long dmachan;

    save_flags(flags);
    cli();

    dmachan = lp->dmachan;
    dma_abs = (unsigned long) (lp->txdmabuf);

    if(!valid_dma_page(dma_abs, DMA_BUFF_SIZE + sizeof(struct mbuf)))
	panic("PT: TX buffer violates DMA boundary!");

    disable_dma(dmachan);
    /* Set DMA mode register to single transfers, incrementing address,
     *  no auto init, reads
     */
    set_dma_mode(dmachan, DMA_MODE_WRITE);
    clear_dma_ff(dmachan);
    set_dma_addr(dmachan, dma_abs);
    /* output byte count */
    set_dma_count(dmachan, length);

    restore_flags(flags);
}

/*
 * This sets up all the registers in the SCC for the given channel
 * based upon tsync_hwint()
 */
static void scc_init(struct device *dev)
{
	unsigned long flags;
	struct pt_local *lp = (struct pt_local*) dev->priv;
	register int cmd = lp->base + CTL;
	int tc, br;

#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: scc_init(): (%d).\n", lp->base & CHANA);
#endif
	save_flags(flags);
	cli();

	/* We may put something here to enable_escc */

	if (cmd & CHANA)
	{
	        wrtscc(lp->cardbase, cmd, R9, CHRA);	/* Reset channel A */
	        wrtscc(lp->cardbase, cmd, R2, 0xff);	/* Initialise interrupt vector */
	}
	else
	    	wrtscc(lp->cardbase, cmd, R9, CHRB);	/* Reset channel B */

	/* Deselect all Rx and Tx interrupts */
	wrtscc(lp->cardbase, cmd, R1, 0);

	/* Turn off external interrupts (like CTS/CD) */
	wrtscc(lp->cardbase, cmd, R15, 0);

	/* X1 clock, SDLC mode */
	wrtscc(lp->cardbase, cmd, R4, SDLC | X1CLK);

	/* Preset CRC and set mode */
	if (lp->nrzi)
	    	/* Preset Tx CRC, put into NRZI mode */
		wrtscc(lp->cardbase, cmd, R10, CRCPS | NRZI);
	else
		/* Preset Tx CRC, put into NRZ mode */
		wrtscc(lp->cardbase, cmd, R10, CRCPS);

	/* Tx/Rx parameters */
	if (lp->speed)		/* Use internal clocking */
	       /* Tx Clk from BRG. Rx Clk form DPLL, TRxC pin outputs DPLL */
	       wrtscc(lp->cardbase, cmd, R11, TCBR | RCDPLL | TRxCDP | TRxCOI);
	else	/* Use external clocking */
	{
		/* Tx Clk from TRxCL. Rx Clk from RTxCL, TRxC pin if input */
		wrtscc(lp->cardbase, cmd, R11, TCTRxCP | RCRTxCP | TRxCBR);
        	wrtscc(lp->cardbase,cmd, R14, 0);	/* wiz1 */
	}

	/* Null out SDLC start address */
	wrtscc(lp->cardbase, cmd, R6, 0);

	/* SDLC flag */
	wrtscc(lp->cardbase, cmd, R7, FLAG);

	/* Setup Tx but don't enable it */
	wrtscc(lp->cardbase, cmd, R5, Tx8 | DTR);

	/* Setup Rx */
	wrtscc(lp->cardbase, cmd, R3, AUTO_ENAB | Rx8);

	/* Setup the BRG, turn it off first */
	wrtscc(lp->cardbase, cmd, R14, BRSRC);

	/* set the 32x time constant for the BRG in Rx mode */
	if (lp->speed)
	{
		br = lp->speed;
		tc = ((lp->xtal / 32) / (br * 2)) - 2;
		wrtscc(lp->cardbase, cmd, R12, tc & 0xff);		/* lower byte */
   		wrtscc(lp->cardbase, cmd, R13, (tc >> 8) & 0xff);	/* upper byte */
	}

	/* Turn transmitter off, to setup stuff */
   	pt_rts(lp, OFF);

	/* External clocking */
	if (lp->speed)
	{
		/* DPLL frm BRG, BRG src PCLK */
		wrtscc(lp->cardbase, cmd, R14, BRSRC | SSBR);
		wrtscc(lp->cardbase, cmd, R14, BRSRC | SEARCH);	/* SEARCH mode, keep BRG src */
		wrtscc(lp->cardbase, cmd, R14, BRSRC | BRENABL);	/* Enable the BRG */

	    /* Turn off external clock port */
		if (lp->base & CHANA)
			outb_p( (pt_sercfg &= ~PT_EXTCLKA), (lp->cardbase + SERIAL_CFG) );
		else
			outb_p( (pt_sercfg &= ~PT_EXTCLKB), (lp->cardbase + SERIAL_CFG) );
	}
	else
	{
		/* DPLL frm rtxc,BRG src PCLK */
		/* Turn on external clock port */
		if (lp->base & CHANA)
			outb_p( (pt_sercfg |= PT_EXTCLKA), (lp->cardbase + SERIAL_CFG) );
		else
			outb_p( (pt_sercfg |= PT_EXTCLKB), (lp->cardbase + SERIAL_CFG) );
	}

	if (!lp->dmachan)
		wrtscc(lp->cardbase, cmd, R1, (INT_ALL_Rx | EXT_INT_ENAB));

	wrtscc(lp->cardbase, cmd, R15, BRKIE);	/* ABORT int */

	/* Turn on the DTR to tell modem we're alive */
	if (lp->base & CHANA)
		outb_p( (pt_sercfg |= PT_DTRA_ON), (lp->cardbase + SERIAL_CFG) );
	else
	    	outb_p( (pt_sercfg |= PT_DTRB_ON), (lp->cardbase + SERIAL_CFG) );

	/* Now, turn on the receiver and hunt for a flag */
	wrtscc(lp->cardbase, cmd, R3, RxENABLE | RxCRC_ENAB | AUTO_ENAB | Rx8 );

	restore_flags(flags);

} /* scc_init() */

/* Resets the given channel and whole SCC if both channels off */
static void chipset_init(struct device *dev)
{

	struct pt_local *lp = (struct pt_local*) dev->priv;
#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: chipset_init(): pt0a tstate = %d.\n", ((struct pt_local*)pt0a.priv)->tstate);
	printk(KERN_DEBUG "PT: chipset_init(): pt0b tstate = %d.\n", ((struct pt_local*)pt0b.priv)->tstate);
#endif
	/* Reset SCC if both channels are to be canned */
	if ( ((lp->base & CHANA) && !(pt_sercfg & PT_DTRB_ON)) ||
			(!(lp->base & CHANA) && !(pt_sercfg & PT_DTRA_ON)) )
	{
		wrtscc(lp->cardbase, lp->base + CTL, R9, FHWRES);
        	/* Reset int and dma registers */
	        outb_p((pt_sercfg = 0), lp->cardbase + SERIAL_CFG);
	        outb_p((pt_dmacfg = 0), lp->cardbase + DMA_CFG);
#ifdef PT_DEBUG
		printk(KERN_DEBUG "PT: chipset_init() Resetting SCC, called by ch (%d).\n", lp->base & CHANA);
#endif
	}
	/* Reset individual channel */
    	if (lp->base & CHANA) {
        	wrtscc(lp->cardbase, lp->base + CTL, R9, MIE | DLC | NV | CHRA);
        	outb_p( (pt_sercfg &= ~PT_DTRA_ON), lp->cardbase + SERIAL_CFG);
    	} else {
        	wrtscc(lp->cardbase, lp->base + CTL, R9, MIE | DLC | NV | CHRB);
			outb_p( (pt_sercfg &= ~PT_DTRB_ON), lp->cardbase + SERIAL_CFG);
	}
} /* chipset_init() */



__initfunc(int pt_init(void))
{
    int *port;
    int ioaddr = 0;
    int card_type = 0;
    int ports[] =
    { 0x230, 0x240, 0x250, 0x260, 0x270, 0x280, 0x290, 0x2a0,
      0x2b0, 0x300, 0x330, 0x3f0,  0};

    printk(KERN_INFO "PT: 0.41 ALPHA 07 October 1995 Craig Small (csmall@small.dropbear.id.au)\n");

    for (port = &ports[0]; *port && !card_type; port++) {
        ioaddr = *port;

        if (check_region(ioaddr, PT_TOTAL_SIZE) == 0) {
            printk(KERN_INFO "PT: Probing for card at address %#3x\n", ioaddr);
            card_type = hw_probe(ioaddr);
        }
    }
    if (card_type) {
        printk(KERN_INFO "PT: Found a PT at address %#3x\n",ioaddr);
    } else {
        printk(KERN_ERR "PT: ERROR: No card found.\n");
        return -EIO;
    }

    /*
     * Link a couple of device structures into the chain
     *
     * For the A port
     * Allocate space for 4 buffers even though we only need 3,
     * because one of them may cross a DMA page boundary and
     * be rejected by get_dma_buffer().
     */
    register_netdev(&pt0a);

    pt0a.priv= kmalloc(sizeof(struct pt_local) + (DMA_BUFF_SIZE + sizeof(struct mbuf)) * 4, GFP_KERNEL | GFP_DMA);

    pt0a.dma = 0;	/* wizzer - no dma yet */
    pt0a.base_addr = ioaddr + CHANA;
    pt0a.irq = 0;

    /* And B port */
    register_netdev(&pt0b);

    pt0b.priv= kmalloc(sizeof(struct pt_local) + (DMA_BUFF_SIZE + sizeof(struct mbuf)) * 4, GFP_KERNEL | GFP_DMA);

    pt0b.base_addr = ioaddr + CHANB;
    pt0b.irq = 0;

    /* Now initialise them */
    pt_probe(&pt0a);
    pt_probe(&pt0b);

    pt0b.irq = pt0a.irq;	/* IRQ is shared */

    return 0;
} /* pt_init() */

/*
 * Probe for PT card.  Also initialises the timers
 */
__initfunc(static int hw_probe(int ioaddr))
{
    int time = 1000;		/* Number of milliseconds to test */
    int a = 1;
    int b = 1;
    unsigned long start_time, end_time;

    inb_p(ioaddr + TMR1CLR);
    inb_p(ioaddr + TMR2CLR);

    /* Timer counter channel 0, 1mS period */
    outb_p(SC0 | LSB_MSB | MODE3, ioaddr + TMRCMD);
    outb_p(0x00, ioaddr + TMR0);
    outb_p(0x18, ioaddr + TMR0);

    /* Setup timer control word for timer 1 */
    outb_p(SC1 | LSB_MSB | MODE0, ioaddr + TMRCMD);
    outb_p((time << 1) & 0xff, ioaddr + TMR1);
    outb_p((time >> 7) & 0xff, ioaddr + TMR1);

    /* wait until counter reg is loaded */
    do {
        /* Latch count for reading */
        outb_p(SC1, ioaddr + TMRCMD);
        a = inb_p(ioaddr + TMR1);
        b = inb_p(ioaddr + TMR1);
    } while (b == 0);
    start_time = jiffies;
    while(b != 0)
    {
        /* Latch count for reading */
        outb_p(SC1, ioaddr + TMRCMD);
        a = inb_p(ioaddr + TMR1);
        b = inb_p(ioaddr + TMR1);
        end_time = jiffies;
        /* Don't wait forever - there may be no card here */
        if ((end_time - start_time) > 200)
        {
        	inb_p(ioaddr + TMR1CLR);
            return 0;
        }
    }

    /* Now fix the timers up for general operation */

    /* Clear the timers */
    inb_p(ioaddr + TMR1CLR);
    inb_p(ioaddr + TMR2CLR);

    outb_p(SC1 | LSB_MSB | MODE0, ioaddr + TMRCMD);
    inb_p(ioaddr + TMR1CLR);

    outb_p(SC2 | LSB_MSB | MODE0, ioaddr + TMRCMD);
    /* Should this be tmr1 or tmr2? wiz3*/
    inb_p(ioaddr + TMR1CLR);

    return 1;
} /* hw_probe() */


static void pt_rts(struct pt_local *lp, int x)
{
	int tc;
	long br;
	int cmd = lp->base + CTL;
#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: pt_rts(): Transmitter status will be %d (%d).\n", x, lp->base & CHANA);
#endif
	if (x == ON) {
	    /* Ex ints off to avoid int */
	    wrtscc(lp->cardbase, cmd, R15, 0);
	    wrtscc(lp->cardbase, cmd, R3, AUTO_ENAB | Rx8);	/* Rx off */
	    lp->rstate = IDLE;

	    if(lp->dmachan)
	    {
	        /* Setup for Tx DMA */
	        wrtscc(lp->cardbase, cmd, R1, WT_FN_RDYFN | EXT_INT_ENAB);
	    } else {
	        /* No interrupts */
	        wrtscc(lp->cardbase, cmd, R1, 0);
	    }

            if (!lp->clockmode)
            {
                if (lp->speed)
                {
                    br = lp->speed;
                    tc = (lp->xtal / (br * 2)) - 2;
                    wrtscc(lp->cardbase, cmd, R12, tc & 0xff);
                    wrtscc(lp->cardbase, cmd, R13, (tc >> 8) & 0xff);
                }
            }
            /* Turn on Tx by raising RTS */
            wrtscc(lp->cardbase, cmd, R5, TxCRC_ENAB | RTS | TxENAB | Tx8 | DTR);
            /* Transmitter on now */
        } else {		/* turning off Tx */
            lp->tstate = IDLE;

            /* Turn off Tx by dropping RTS */
            wrtscc(lp->cardbase, cmd, R5, Tx8 | DTR);
            if (!lp->clockmode)
            {
                if (lp->speed)		/* internally clocked */
                {
                    /* Reprogram BRG from 32x clock for Rx DPLL */
                    /* BRG off, keep PClk source */
                    wrtscc(lp->cardbase, cmd, R14, BRSRC);
                    br = lp->speed;
                    tc = ((lp->xtal / 32) / (br * 2)) - 2;
                    wrtscc(lp->cardbase, cmd, R12, tc & 0xff);
                    wrtscc(lp->cardbase, cmd, R13, (tc >> 8) & 0xff);

                    /* SEARCH mode, BRG source */
                    wrtscc(lp->cardbase, cmd, R14, BRSRC | SEARCH);
                    /* Enable the BRG */
                    wrtscc(lp->cardbase, cmd, R14, BRSRC | BRENABL);
                }
            }
            /* Flush Rx fifo */
            /* Turn Rx off */
            wrtscc(lp->cardbase, cmd, R3, AUTO_ENAB | Rx8);

            /* Reset error latch */
            wrtscc(lp->cardbase, cmd, R0, ERR_RES);

            /* get status byte from R1 */
            (void) rdscc(lp->cardbase, cmd, R1);

            /* Read and dump data in queue */
            (void) rdscc(lp->cardbase, cmd, R8);
            (void) rdscc(lp->cardbase, cmd, R8);
            (void) rdscc(lp->cardbase, cmd, R8);

            /* Now, turn on Rx and hunt for a flag */
              wrtscc(lp->cardbase, cmd, R3, RxENABLE | AUTO_ENAB | Rx8 );

            lp->rstate = ACTIVE;

            if (lp->dmachan)
            {
                setup_rx_dma(lp);
            } else {
                /* Reset buffer pointers */
                lp->rcp = lp->rcvbuf->data;
                lp->rcvbuf->cnt = 0;
                /* Allow aborts to interrupt us */
                wrtscc(lp->cardbase, cmd, R1, INT_ALL_Rx | EXT_INT_ENAB);

	}
	wrtscc(lp->cardbase, cmd, R15, BRKIE );
    }
} /* pt_rts() */


static int valid_dma_page(unsigned long addr, unsigned long dev_bufsize)
{
    if (((addr & 0xffff) + dev_bufsize) <= 0x10000)
        return 1;
    else
        return 0;
}

static int pt_set_mac_address(struct device *dev, void *addr)
{
	struct sockaddr *sa = (struct sockaddr *)addr;
	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);		/* addr is an AX.25 shifted ASCII */
	return 0;		/* mac address */
}


/* Allocate a buffer which does not cross a DMA page boundary */
static char * get_dma_buffer(unsigned long *mem_ptr)
{
    char *ret;

    ret = (char *) *mem_ptr;

    if (!valid_dma_page(*mem_ptr, DMA_BUFF_SIZE + sizeof(struct mbuf))) {
        *mem_ptr += (DMA_BUFF_SIZE + sizeof(struct mbuf));
        ret = (char *) *mem_ptr;
    }
    *mem_ptr += (DMA_BUFF_SIZE + sizeof(struct mbuf));
    return (ret);
} /* get_dma_buffer() */


/*
 * Sets up all the structures for the PT device
 */
static int pt_probe(struct device *dev)
{
    short ioaddr;
    struct pt_local *lp;
    unsigned long flags;
    unsigned long mem_ptr;

    ioaddr = dev->base_addr;

    /*
     * Initialise the device structure.
     * Must be done before chipset_init()
     * Make sure data structures used by  the PT are aligned
     */
    dev->priv = (void *) (((int) dev->priv + 7) & ~7);
    lp = (struct pt_local*) dev->priv;

    memset(dev->priv, 0, sizeof(struct pt_local));

    /* Allocate some buffers which do not cross DMA boundaries */
    mem_ptr = (unsigned long) dev->priv + sizeof(struct pt_local);
    lp->txdmabuf = get_dma_buffer(&mem_ptr);
    lp->rxdmabuf1 = (struct mbuf *) get_dma_buffer(&mem_ptr);
    lp->rxdmabuf2 = (struct mbuf *) get_dma_buffer(&mem_ptr);

    /* Initialise the Rx buffer */
    lp->rcvbuf = lp->rxdmabuf1;
    lp->rcp = lp->rcvbuf->data;
    lp->rcvbuf->cnt = 0;

    /* Initialise the transmit queue head structure */
    skb_queue_head_init(&lp->sndq);

    lp->base = dev->base_addr;
    lp->cardbase = dev->base_addr & 0x3f0;

    /* These need to be initialised before scc_init() is called.
     */
    lp->xtal = XTAL;

    if (dev->base_addr & CHANA) {
        lp->speed = DEF_A_SPEED;
        lp->txdelay = DEF_A_TXDELAY;
        lp->persist = DEF_A_PERSIST;
        lp->slotime = DEF_A_SLOTIME;
        lp->squeldelay = DEF_A_SQUELDELAY;
        lp->clockmode = DEF_A_CLOCKMODE;
        lp->nrzi = DEF_A_NRZI;
    } else {
        lp->speed = DEF_B_SPEED;
        lp->txdelay = DEF_B_TXDELAY;
        lp->persist = DEF_B_PERSIST;
        lp->slotime = DEF_B_SLOTIME;
        lp->squeldelay = DEF_B_SQUELDELAY;
        lp->clockmode = DEF_B_CLOCKMODE;
        lp->nrzi = DEF_B_NRZI;
    }
    lp->bufsiz = DMA_BUFF_SIZE;
    lp->tstate = IDLE;

    chipset_init(dev);

    if (dev->base_addr & CHANA) {
        /* Note that a single IRQ services 2 devices (A and B channels)
        */

	/*
	 * We disable the dma for a while, we have to get ints working
	 * properly first!!
	 */
	lp->dmachan = 0;

        if (dev->irq < 2) {
            autoirq_setup(0);

            /* Turn on PT interrupts */
            save_flags(flags);
            cli();
            outb_p( pt_sercfg |= PT_EI, lp->cardbase + INT_CFG);
            restore_flags(flags);

            /* Set a timer interrupt */
            tdelay(lp, 1);
            dev->irq = autoirq_report(20);

	    /* Turn off PT interrupts */
	    save_flags(flags);
	    cli();
            outb_p( (pt_sercfg  &= ~ PT_EI), lp->cardbase + INT_CFG);
            restore_flags(flags);

            if (!dev->irq) {
                printk(KERN_ERR "PT: ERROR: Failed to detect IRQ line, assuming IRQ7.\n");
            }
        }

        printk(KERN_INFO "PT: Autodetected IRQ %d, assuming DMA %d\n", dev->irq, dev->dma);

        /* This board has jumpered interrupts. Snarf the interrupt vector
         * now.  There is no point in waiting since no other device can use
         * the interrupt, and this marks the 'irqaction' as busy.
         */
        {
            int irqval = request_irq(dev->irq, &pt_interrupt,0, "pt", dev);
            if (irqval) {
                printk(KERN_ERR "PT: ERROR: Unable to get IRQ %d (irqval = %d).\n",
                    dev->irq, irqval);
                return EAGAIN;
            }
        }

        /* Grab the region */
        request_region(ioaddr & 0x3f0, PT_TOTAL_SIZE, "pt" );
    } /* A port */
    dev->open = pt_open;
    dev->stop = pt_close;
    dev->do_ioctl = pt_ioctl;
    dev->hard_start_xmit = pt_send_packet;
    dev->get_stats = pt_get_stats;

    /* Fill in the fields of the device structure */
    dev_init_buffers(dev);

#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
    dev->hard_header    = ax25_encapsulate;
    dev->rebuild_header = ax25_rebuild_header;
#endif

    dev->set_mac_address = pt_set_mac_address;

    dev->type = ARPHRD_AX25;            /* AF_AX25 device */
    dev->hard_header_len = 73;      /* We do digipeaters now */
    dev->mtu = 1500;                /* eth_mtu is default */
    dev->addr_len = 7;               /* sizeof an ax.25 address */
    memcpy(dev->broadcast, ax25_bcast, 7);
    memcpy(dev->dev_addr, ax25_test, 7);

    /* New style flags */
    dev->flags = 0;

    return 0;
} /* pt_probe() */


/* Open/initialise the board.  This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that 'should' only be set once at boot, so that there is
 * a non-reboot way to recover if something goes wrong.
 * derived from last half of tsync_attach()
 */
static int pt_open(struct device *dev)
{
    unsigned long flags;
    struct pt_local *lp = dev->priv;
    static first_time = 1;

    if (dev->base_addr & CHANA)
    {
        if (first_time)
        {
            if (request_dma(dev->dma, "pt"))
            {
                free_irq(dev->irq, dev);
                return -EAGAIN;
            }
        }

         /* Reset hardware */
         chipset_init(dev);
     }
     lp->tstate = IDLE;

     if (dev->base_addr & CHANA)
     {
         scc_init(dev);
         scc_init(dev->next);
     }
     /* Save a copy of register RR0 for comparing with later on */
     /* We always put 0 in zero count */
     lp->saved_RR0 = rdscc(lp->cardbase, lp->base + CTL, R0) & ~ZCOUNT;

    /* master interrupt enable */
    save_flags(flags);
    cli();
    wrtscc(lp->cardbase, lp->base + CTL, R9, MIE | NV);
    outb_p( pt_sercfg |= PT_EI, lp->cardbase + INT_CFG);
    restore_flags(flags);

    lp->open_time = jiffies;

    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;
    first_time = 0;

    MOD_INC_USE_COUNT;

    return 0;
} /* pt_open() */

static int pt_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct pt_local *lp = (struct pt_local *) dev->priv;

#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: pt_send_packet(): (%d)\n", lp->base & CHANA);
#endif
	hardware_send_packet(lp, skb);
	dev->trans_start = jiffies;

	return 0;
}



/* The inverse routine to pt_open() */
static int pt_close(struct device *dev)
{
	unsigned long flags;
	struct pt_local *lp = dev->priv;
	struct sk_buff *ptr = NULL;
	int cmd;

	cmd = lp->base + CTL;

	save_flags(flags);
	cli();

	/* Reset SCC or channel */
	chipset_init(dev);
	disable_dma(lp->dmachan);

	lp->open_time = 0;
	dev->tbusy = 1;
	dev->start = 0;

	/* Free any buffers left in the hardware transmit queue */
	while ((ptr = skb_dequeue(&lp->sndq)) != NULL)
		kfree_skb(ptr);

	restore_flags(flags);

#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: pt_close(): Closing down channel (%d).\n", lp->base & CHANA);
#endif

	MOD_DEC_USE_COUNT;

	return 0;
} /* pt_close() */


static int pt_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
    unsigned long flags;
    struct pt_req rq;
    struct pt_local *lp = (struct pt_local *) dev->priv;

    int ret = verify_area(VERIFY_WRITE, ifr->ifr_data, sizeof(struct pt_req));
    if (ret)
	return ret;

    if (cmd != SIOCDEVPRIVATE)
        return -EINVAL;

    copy_from_user(&rq, ifr->ifr_data, sizeof(struct pt_req));

    switch (rq.cmd) {
    case SIOCSPIPARAM:

	if (!suser())
	    return -EPERM;
	save_flags(flags);
	cli();
	lp->txdelay = rq.txdelay;
	lp->persist = rq.persist;
	lp->slotime = rq.slotime;
	lp->squeldelay = rq.squeldelay;
	lp->clockmode = rq.clockmode;
	lp->speed = rq.speed;
	pt_open(&pt0a);
	restore_flags(flags);
	ret = 0;
	break;

    case SIOCSPIDMA:

	if (!suser())
	    return -EPERM;
	ret = 0;
	if (dev->base_addr & CHANA) {   /* if A channel */
	   if (rq.dmachan < 1 || rq.dmachan > 3)
		return -EINVAL;
	   save_flags(flags);
	   cli();
	   pt_close(dev);
	   free_dma(lp->dmachan);
	   dev->dma = lp->dmachan = rq.dmachan;
	   if (request_dma(lp->dmachan,"pt"))
		ret = -EAGAIN;
	   pt_open(dev);
	   restore_flags(flags);
	}
	break;

    case SIOCSPIIRQ:
	ret = -EINVAL;      /* add this later */
	break;

    case SIOCGPIPARAM:
    case SIOCGPIDMA:
    case SIOCGPIIRQ:

	rq.speed = lp->speed;
	rq.txdelay = lp->txdelay;
	rq.persist = lp->persist;
	rq.slotime = lp->slotime;
	rq.squeldelay = lp->squeldelay;
	rq.clockmode = lp->clockmode;
	rq.dmachan = lp->dmachan;
	rq.irq = dev->irq;
	copy_to_user(ifr->ifr_data, &rq, sizeof(struct pt_req));
	ret = 0;
	break;

    default:
	ret = -EINVAL;
    }
    return ret;
}

/*
 *	Get the current statistics.
 *	This may be called with the card open or closed. 
 */
 
static struct net_device_stats *pt_get_stats(struct device *dev)
{
	struct pt_local *lp = (struct pt_local *) dev->priv;
	return &lp->stats;
}


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c skeleton.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 4
 * End:
 */


static void tdelay(struct pt_local *lp, int time)
{
	/* For some reason, we turn off the Tx interrupts here! */
	if (!lp->dmachan)
		wrtscc(lp->cardbase, lp->base + CTL, R1, INT_ALL_Rx | EXT_INT_ENAB);

	if (lp->base & CHANA) 
	{
		outb_p(time & 0xff, lp->cardbase + TMR1);
		outb_p((time >> 8)&0xff, lp->cardbase + TMR1);
	}
	else
	{
		outb_p(time & 0xff, lp->cardbase + TMR2);
		outb_p((time >> 8)&0xff, lp->cardbase + TMR2);
	}
} /* tdelay */


static void pt_txisr(struct pt_local *lp)
{
	unsigned long flags;
	int cmd;
	unsigned char c;

	save_flags(flags);
	cli();
	cmd = lp->base + CTL;

#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: pt_txisr(): tstate = %d (%d).\n", lp->tstate, lp->base & CHANA);
#endif

	switch (lp->tstate)
	{
	case CRCOUT:
	    lp->tstate = FLAGOUT;
	    tdelay(lp, lp->squeldelay);
	    restore_flags(flags);
	    return;

	case IDLE:
	    /* Transmitter idle. Find a frame for transmission */
	    if ((lp->sndbuf = skb_dequeue(&lp->sndq)) == NULL)
	    {
	        /* Nothing to send - return to receive mode
	         * Tx off now - flag should have gone
	         */
	        pt_rts(lp, OFF);

	        restore_flags(flags);
	        return;
	    }
	    if (!lp->dmachan)
	    {
		    lp->txptr = lp->sndbuf->data;
		    lp->txptr++;		/* Ignore KISS control byte */
		    lp->txcnt = (int) lp->sndbuf->len - 1;
		}
	    /* If a buffer to send, drop though here */

	case DEFER:
	    /* Check DCD - debounce it */
	    /* See Intel Microcommunications Handbook p2-308 */
	    wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	    wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	    if ((rdscc(lp->cardbase, cmd, R0) & DCD) != 0)
	    {
	        lp->tstate = DEFER;
	        tdelay(lp, 100);
	        /* DEFER until DCD transition or timeout */
	        wrtscc(lp->cardbase, cmd, R15, DCDIE);
	        restore_flags(flags);
	        return;
	    }
	    if (random() > lp->persist)
	    {
	        lp->tstate = DEFER;
	        tdelay(lp, lp->slotime);
	        restore_flags(flags);
	        return;
	    }
	    pt_rts(lp, ON);		/* Tx on */
	    if (lp->dmachan)
	    	wrtscc(lp->cardbase, cmd, R5, TxCRC_ENAB | RTS | Tx8);
	    lp->tstate = ST_TXDELAY;
	    tdelay(lp, lp->txdelay);
	    restore_flags(flags);
	    return;

	case ACTIVE:
	    /* Here we are actively sending a frame */
	    if (lp->txcnt--)
	    {
	        /* XLZ - checkout Gracilis PT code to see if the while
	         * loop is better or not.
	         */
	        c = *lp->txptr++;
	        /* next char is gone */
	        wrtscc(lp->cardbase, cmd, R8, c);
	        /* stuffing a char satisfies interrupt condition */
	    } else {
	        /* No more to send */
	        kfree_skb(lp->sndbuf);
	        lp->sndbuf = NULL;
	        if ((rdscc(lp->cardbase, cmd, R0) & TxEOM))
	        {
	            /* Did we underrun */
	            lp->stats.tx_errors++;
	            lp->stats.tx_fifo_errors++;
	            wrtscc(lp->cardbase, cmd, R0, SEND_ABORT);
	            lp->tstate = FLAGOUT;
	            tdelay(lp, lp->squeldelay);
	            restore_flags(flags);
	            return;
	        }
	        lp->tstate = UNDERRUN;
	        /* Send flags on underrun */
	       if (lp->nrzi)
	       {
	           wrtscc(lp->cardbase, cmd, R10, CRCPS | NRZI);
	       } else {
	           wrtscc(lp->cardbase, cmd, R10, CRCPS | NRZ);
	       }
	       /* Reset Tx interrupt pending */
	       wrtscc(lp->cardbase, cmd, R0, RES_Tx_P);
	   }
	   restore_flags(flags);
	   return;
	default:
		printk(KERN_ERR "PT: pt_txisr(): Invalid tstate (%d) for chan %s.\n", lp->tstate, (cmd & CHANA? "A": "B") );
		pt_rts(lp, OFF);
		lp->tstate = IDLE;
		break;
    } 				/*switch */
    restore_flags(flags);
}

static void pt_rxisr(struct device *dev)
{
    struct pt_local *lp = (struct pt_local*) dev->priv;
    int cmd = lp->base + CTL;
    int bytecount;
    unsigned long flags;
    char rse;
    struct sk_buff *skb;
    int sksize, pkt_len;
    struct mbuf *cur_buf = NULL;
    unsigned char *cfix;

    save_flags(flags);
    cli();

    /* Get status byte from R1 */
    rse = rdscc(lp->cardbase, cmd, R1);

#ifdef PT_DEBUG
    printk(KERN_DEBUG "PT: pt_rxisr(): R1 = %#3x. (%d)\n", rse, lp->base & CHANA);
#endif

	if (lp->dmachan && (rse & Rx_OVR))
		lp->rstate = RXERROR;

    if (rdscc(lp->cardbase, cmd, R0) & Rx_CH_AV && !lp->dmachan)
    {
        /* There is a char to be stored
         * Read special condition bits before reading the data char
         */
        if (rse & Rx_OVR)
        {
             /* Rx overrun - toss buffer */
             /* wind back the pointers */
             lp->rcp = lp->rcvbuf->data;
             lp->rcvbuf->cnt = 0;
             lp->rstate = RXERROR;
             lp->stats.rx_errors++;
             lp->stats.rx_fifo_errors++;
         } else if (lp->rcvbuf->cnt >= lp->bufsiz)
             {
                 /* Too large packet
                  * wind back Rx buffer pointers
                  */
                 lp->rcp = lp->rcvbuf->data;
                 lp->rcvbuf->cnt = 0;
                 lp->rstate = TOOBIG;
             }
         /* ok, we can store the Rx char if no errors */
         if (lp->rstate == ACTIVE)
         {
             *lp->rcp++ = rdscc(lp->cardbase, cmd, R8);
             lp->rcvbuf->cnt++;
         } else {
             /* we got an error, dump the FIFO */
             (void) rdscc(lp->cardbase, cmd, R8);
             (void) rdscc(lp->cardbase, cmd, R8);
             (void) rdscc(lp->cardbase, cmd, R8);

             /* Reset error latch */
             wrtscc(lp->cardbase, cmd, R0, ERR_RES);
             lp->rstate = ACTIVE;

             /* Resync the SCC */
             wrtscc(lp->cardbase, cmd, R3, RxENABLE | ENT_HM | AUTO_ENAB | Rx8);

         }
     }

     if (rse & END_FR)
     {
#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: pt_rxisr() Got end of a %u byte frame.\n", lp->rcvbuf->cnt);
#endif
		if (lp->dmachan)
		{
			clear_dma_ff(lp->dmachan);
			bytecount = lp->bufsiz - get_dma_residue(lp->dmachan);
		} else {
			bytecount = lp->rcvbuf->cnt;
		}

         /* END OF FRAME - Make sure Rx was active */
         if (lp->rcvbuf->cnt > 0 || lp->dmachan)
         {
             if ((rse & CRC_ERR) || (lp->rstate > ACTIVE) || (bytecount < 10))
             {
                 if ((bytecount >= 10) && (rse & CRC_ERR))
                 {
                     lp->stats.rx_crc_errors++;
                 }
                 if (lp->dmachan)
                 {
                 	if (lp->rstate == RXERROR)
                 	{
                 		lp->stats.rx_errors++;
                 		lp->stats.rx_over_errors++;
                 	}
                 	lp->rstate = ACTIVE;
                 	setup_rx_dma(lp);
                 } else {
	                 /* wind back Rx buffer pointers */
    	             lp->rcp = lp->rcvbuf->data;
        	         lp->rcvbuf->cnt = 0;

					/* Re-sync the SCC */
					wrtscc(lp->cardbase, cmd, R3, RxENABLE | ENT_HM | AUTO_ENAB | Rx8);

        	     }
#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: pt_rxisr() %s error.\n", (rse & CRC_ERR)? "CRC" : "state");
#endif
             } else {
                 /* We have a valid frame */
                 if (lp->dmachan)
                 {
                 	pkt_len = lp->rcvbuf->cnt = bytecount - 2 +1;
					/* Get buffer for next frame */
                 	cur_buf = lp->rcvbuf;
                 	switchbuffers(lp);
                 	setup_rx_dma(lp);
                 } else {
	                 pkt_len = lp->rcvbuf->cnt -= 2;  /* Toss 2 CRC bytes */
    	             pkt_len += 1;	/* make room for KISS control byte */
        		}

                 /* Malloc up new buffer */
                 sksize = pkt_len;
                 skb = dev_alloc_skb(sksize);
                 if (skb == NULL)
                 {
                     printk(KERN_ERR "PT: %s: Memory squeeze, dropping packet.\n", dev->name);
                     lp->stats.rx_dropped++;
                     restore_flags(flags);
                     return;
                 }
                 skb->dev = dev;

                 /* KISS kludge = prefix with a 0 byte */
                 cfix=skb_put(skb,pkt_len);
                 *cfix++=0;
                 /* skb->data points to the start of sk_buff area */
                 if (lp->dmachan)
                 	memcpy(cfix, (char*)cur_buf->data, pkt_len - 1);
                 else
                    memcpy(cfix, lp->rcvbuf->data, pkt_len - 1);
                 skb->protocol = ntohs(ETH_P_AX25);
                 skb->mac.raw=skb->data;
                 lp->stats.rx_bytes+=skb->len;
                 netif_rx(skb);
                 lp->stats.rx_packets++;
                 if (!lp->dmachan)
                 {
	                 /* packet queued - wind back buffer for next frame */
    	             lp->rcp = lp->rcvbuf->data;
	                 lp->rcvbuf->cnt = 0;
	             }
             } /* good frame */
         } /* check active Rx */
         /* Clear error status */
         lp->rstate = ACTIVE;
         /* Reset error latch */
     } /* end EOF check */
     wrtscc(lp->cardbase, cmd, R0, ERR_RES);
     restore_flags(flags);
} /* pt_rxisr() */

/*
 * This handles the two timer interrupts.
 * This is a real bugger, cause you have to rip it out of the pi's
 * external status code.  They use the CTS line or something.
 */
static void pt_tmrisr(struct pt_local *lp)
{
    unsigned long flags;

#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: pt_tmrisr(): tstate = %d (%d).\n", lp->tstate, lp->base & CHANA);
#endif

    save_flags(flags);
    cli();


    switch (lp->tstate)
    {
    /* Most of this stuff is in pt_exisr() */
    case FLAGOUT:
    case ST_TXDELAY:
    case DEFER:
/*    case ACTIVE:
    case UNDERRUN:*/
        pt_exisr(lp);
        break;

    default:
	if (lp->base & CHANA)
 	    printk(KERN_ERR "PT: pt_tmrisr(): Invalid tstate %d for Channel A\n", lp->tstate);
	else
	    printk(KERN_ERR "PT: pt_tmrisr(): Invalid tstate %d for Channel B\n", lp->tstate);
	break;
    } /* end switch */
    restore_flags(flags);
} /* pt_tmrisr() */


/*
 * This routine is called by the kernel when there is an interrupt for the
 * PT.
 */
static void pt_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    /* It's a tad dodgy here, but we assume pt0a until proven otherwise */
    struct device *dev = &pt0a;
    struct pt_local *lp = dev->priv;
    unsigned char intreg;
    unsigned char st;
    register int cbase = dev->base_addr & 0x3f0;
    unsigned long flags;

    /* Read the PT's interrupt register, this is not the SCC one! */
    intreg = inb_p(cbase + INT_REG);
    while(( intreg & 0x07) != 0x07) {
        /* Read interrupt register pending from Channel A */
        while ((st = rdscc(cbase, cbase + CHANA + CTL, R3)) != 0)
        {
        	/* Read interrupt vector from R2, channel B */
#ifdef PT_DEBUG
		printk(KERN_DEBUG "PT: pt_interrupt(): R3 = %#3x", st);
#endif
/*        	st = rdscc(lp->cardbase, cbase + CHANB + CTL, R2) & 0x0e;*/
#ifdef PT_DEBUG
		printk(KERN_DEBUG "PI: R2 = %#3x.\n", st);
#endif
			if (st & CHARxIP) {
			    /* Channel A Rx */
	            lp = (struct pt_local*)pt0a.priv;
	            pt_rxisr(&pt0a);
	        } else if (st & CHATxIP) {
	            /* Channel A Tx */
	            lp = (struct pt_local*)pt0a.priv;
	            pt_txisr(lp);
	        } else if (st & CHAEXT) {
	        	/* Channel A External Status */
	        	lp = (struct pt_local*)pt0a.priv;
	        	pt_exisr(lp);
	        } else if (st & CHBRxIP) {
	        	/* Channel B Rx */
	            lp= (struct pt_local*)pt0b.priv;
	            pt_rxisr(&pt0b);
	        } else if (st & CHBTxIP) {
            	/* Channel B Tx */
	            lp = (struct pt_local*)pt0b.priv;
	            pt_txisr(lp);
			} else if (st & CHBEXT) {
	        	/* Channel B External Status */
	        	lp = (struct pt_local*)pt0b.priv;
	        	pt_exisr(lp);
	        }
            /* Reset highest interrupt under service */
            save_flags(flags);
            cli();
            wrtscc(lp->cardbase, lp->base + CTL, R0, RES_H_IUS);
            restore_flags(flags);
        }  /* end of SCC ints */

        if (!(intreg & PT_TMR1_MSK))
        {
            /* Clear timer 1 */
            inb_p(cbase + TMR1CLR);

            pt_tmrisr( (struct pt_local*)pt0a.priv);
        }

        if (!(intreg & PT_TMR2_MSK))
        {
            /* Clear timer 2 */
            inb_p(cbase + TMR2CLR);

            pt_tmrisr( (struct pt_local*)pt0b.priv);
        }

        /* Get the next PT interrupt vector */
        intreg = inb_p(cbase + INT_REG);
    } /* while (intreg) */
} /* pt_interrupt() */


static void pt_exisr(struct pt_local *lp)
{
    unsigned long flags;
    int cmd = lp->base + CTL;
    unsigned char st;
    char c;
    int length;

    save_flags(flags);
    cli();

    /* Get external status */
    st = rdscc(lp->cardbase, cmd, R0);

#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: exisr(): R0 = %#3x tstate = %d (%d).\n", st, lp->tstate, lp->base & CHANA);
#endif
    /* Reset external status latch */
    wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);

    if ((lp->rstate >= ACTIVE) && (st & BRK_ABRT) && lp->dmachan)
    {
    	setup_rx_dma(lp);
    	lp->rstate = ACTIVE;
    }

    switch (lp->tstate)
    {
    case ACTIVE:		/* Unexpected underrun */
#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: exisr(): unexpected underrun detected.\n");
#endif
        kfree_skb(lp->sndbuf);
        lp->sndbuf = NULL;
        if (!lp->dmachan)
        {
	        wrtscc(lp->cardbase, cmd, R0, SEND_ABORT);
	        lp->stats.tx_errors++;
	        lp->stats.tx_fifo_errors++;
	    }
        lp->tstate = FLAGOUT;
        tdelay(lp, lp->squeldelay);
        restore_flags(flags);
        return;
    case UNDERRUN:
        lp->tstate = CRCOUT;
        restore_flags(flags);
        return;
    case FLAGOUT:
        /* squeldelay has timed out */
        /* Find a frame for transmission */
        if ((lp->sndbuf = skb_dequeue(&lp->sndq)) == NULL)
        {
            /* Nothing to send - return to Rx mode */
            pt_rts(lp, OFF);
            lp->tstate = IDLE;
            restore_flags(flags);
            return;
        }
        if (!lp->dmachan)
        {
	        lp->txptr = lp->sndbuf->data;
    	    lp->txptr++;		/* Ignore KISS control byte */
	        lp->txcnt = (int) lp->sndbuf->len - 1;
	    }
        /* Fall through if we have a packet */

    case ST_TXDELAY:
    	if (lp->dmachan)
    	{
    		/* Disable DMA chan */
    		disable_dma(lp->dmachan);

    		/* Set up for TX dma */
    		wrtscc(lp->cardbase, cmd, R1, WT_FN_RDYFN | EXT_INT_ENAB);

    		length = lp->sndbuf->len - 1;
    		memcpy(lp->txdmabuf, &lp->sndbuf->data[1], length);

    		/* Setup DMA controller for Tx */
    		setup_tx_dma(lp, length);

    		enable_dma(lp->dmachan);

    		/* Reset CRC, Txint pending */
    		wrtscc(lp->cardbase, cmd, R0, RES_Tx_CRC | RES_Tx_P);

    		/* Allow underrun only */
    		wrtscc(lp->cardbase, cmd, R15, TxUIE);

    		/* Enable TX DMA */
    		wrtscc(lp->cardbase, cmd, R1, WT_RDY_ENAB | WT_FN_RDYFN | EXT_INT_ENAB);

    		/* Send CRC on underrun */
    		wrtscc(lp->cardbase, cmd, R0, RES_EOM_L);

    		lp->tstate = ACTIVE;
    		break;
    	}
        /* Get first char to send */
        lp->txcnt--;
        c = *lp->txptr++;
        /* Reset CRC for next frame */
        wrtscc(lp->cardbase, cmd, R0, RES_Tx_CRC);

        /* send abort on underrun */
        if (lp->nrzi)
        {
            wrtscc(lp->cardbase, cmd, R10, CRCPS | NRZI | ABUNDER);
        } else {
            wrtscc(lp->cardbase, cmd, R10, CRCPS | NRZ | ABUNDER);
        }
        /* send first char */
        wrtscc(lp->cardbase, cmd, R8, c);

        /* Reset end of message latch */
        wrtscc(lp->cardbase, cmd, R0, RES_EOM_L);

        /* stuff an extra one in */
/*        while ((rdscc(lp->cardbase, cmd, R0) & Tx_BUF_EMP) && lp->txcnt)
        {
            lp->txcnt--;
            c = *lp->txptr++;
            wrtscc(lp->cardbase, cmd, R8, c);
        }*/

        /* select Tx interrupts to enable */
        /* Allow underrun int only */
        wrtscc(lp->cardbase, cmd, R15, TxUIE);

        /* Reset external interrupts */
        wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);

        /* Tx and Rx ints enabled */
        wrtscc(lp->cardbase, cmd, R1, TxINT_ENAB | EXT_INT_ENAB);

        lp->tstate = ACTIVE;
        restore_flags(flags);
        return;

        /* slotime has timed out */
    case DEFER:
        /* Check DCD - debounce it
         * see Intel Microcommunications Handbook, p2-308
         */
        wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
        wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
        if ((rdscc(lp->cardbase, cmd, R0) & DCD) != 0)
        {
            lp->tstate = DEFER;
            tdelay(lp, 100);
            /* DEFER until DCD transition or timeout */
            wrtscc(lp->cardbase, cmd, R15, DCDIE);
            restore_flags(flags);
            return;
        }
        if (random() > lp->persist)
        {
            lp->tstate = DEFER;
            tdelay(lp, lp->slotime);
            restore_flags(flags);
            return;
        }
        if (lp->dmachan)
        	wrtscc(lp->cardbase, cmd, R5, TxCRC_ENAB | RTS | Tx8);
        pt_rts(lp, ON);			/* Tx on */
        lp->tstate = ST_TXDELAY;
        tdelay(lp, lp->txdelay);
        restore_flags(flags);
        return;

 	/* Only for int driven parts */
 	if (lp->dmachan)
 	{
 		restore_flags(flags);
 		return;
 	}

    } /* end switch */
    /*
     * Rx mode only
     * This triggers when hunt mode is entered, & since an ABORT
     * automatically enters hunt mode, we use that to clean up
     * any waiting garbage
     */
    if ((lp->rstate == ACTIVE) && (st & BRK_ABRT) )
    {
#ifdef PT_DEBUG
	printk(KERN_DEBUG "PT: exisr(): abort detected.\n");
#endif
  		/* read and dump all of SCC Rx FIFO */
        (void) rdscc(lp->cardbase, cmd, R8);
        (void) rdscc(lp->cardbase, cmd, R8);
        (void) rdscc(lp->cardbase, cmd, R8);

        lp->rcp = lp->rcvbuf->data;
        lp->rcvbuf->cnt = 0;

		/* Re-sync the SCC */
		wrtscc(lp->cardbase, cmd, R3, RxENABLE | ENT_HM | AUTO_ENAB | Rx8);

    }

    /* Check for DCD transitions */
    if ( (st & DCD) != (lp->saved_RR0 & DCD))
    {
#ifdef PT_DEBUG
        printk(KERN_DEBUG "PT: pt_exisr(): DCD is now %s.\n", (st & DCD)? "ON" : "OFF" );
#endif
		if (st & DCD)
		{
			/* Check that we don't already have some data */
			if (lp->rcvbuf->cnt > 0)
			{
#ifdef PT_DEBUG
				printk(KERN_DEBUG "PT: pt_exisr() dumping %u bytes from buffer.\n", lp->rcvbuf->cnt);
#endif
				/* wind back buffers */
				lp->rcp = lp->rcvbuf->data;
				lp->rcvbuf->cnt = 0;
			}
		} else {  /* DCD off */

			/* read and dump al SCC FIFO */
			(void)rdscc(lp->cardbase, cmd, R8);
			(void)rdscc(lp->cardbase, cmd, R8);
			(void)rdscc(lp->cardbase, cmd, R8);

			/* wind back buffers */
			lp->rcp = lp->rcvbuf->data;
			lp->rcvbuf->cnt = 0;

			/* Re-sync the SCC */
			wrtscc(lp->cardbase, cmd, R3, RxENABLE | ENT_HM | AUTO_ENAB | Rx8);
		}

    }
    /* Update the saved version of register RR) */
    lp->saved_RR0 = st &~ ZCOUNT;
    restore_flags(flags);

} /* pt_exisr() */

#ifdef MODULE
EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Craig Small VK2XLZ <vk2xlz@vk2xlz.ampr.org>");
MODULE_DESCRIPTION("AX.25 driver for the Gracillis PacketTwin HDLC card");

int init_module(void)
{
	return pt_init();
}

void cleanup_module(void)
{
	free_irq(pt0a.irq, &pt0a);	/* IRQs and IO Ports are shared */
	release_region(pt0a.base_addr & 0x3f0, PT_TOTAL_SIZE);

	kfree(pt0a.priv);
	pt0a.priv = NULL;
	unregister_netdev(&pt0a);

	kfree(pt0b.priv);
	pt0b.priv = NULL;
	unregister_netdev(&pt0b);
}
#endif
