/*
   pi2.c: Driver for the Ottawa Amateur Radio Club PI and PI2 interface.
   Copyright (c) 1994 David Perry

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2, as
   published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 675 Mass Ave, Cambridge MA 02139, USA.

   The file skeleton.c by Donald Becker was used as a starting point
   for this driver.

   Revision History

   April 6, 1994  (dp) Created
		       version 0.0 ALPHA
   April 10, 1994 (dp) Included cleanup, suggestions from J. P. Morrison.
                       version 0.1 ALPHA
   April 13, 1994 (dp) Included address probing from JPM, autoirq
		       version 0.2 ALPHA
   April 14, 1994 (ac) Sketched in the NET3 changes.
   April 17, 1994 (dp) Finished the NET3 changes. Used init_etherdev()
		       instead of kmalloc() to ensure that DMA buffers will
		       reside under the 16 meg line.
		       version 0.4 ALPHA
   April 18, 1994 (dp) Now using the kernel provided sk_buff handling functions.
		       Fixed a nasty problem with DMA.
		       version 0.5 ALPHA
   June 6, 1994 (ac)   Fixed to match the buffer locking changes. Added a hack to
   		       fix a funny I see (search for HACK) and fixed the calls in
   		       init() so it doesn't migrate module based ethernet cards up
   		       to eth2 Took out the old module ideas as they are no longer
   		       relevant to the PI driver.
   July 16, 1994 (dp)  Fixed the B channel rx overrun problem ac referred to
   		       above. Also added a bit of a hack to improve the maximum
   	               baud rate on the B channel (Search for STUFF2). Included
   		       ioctl stuff from John Paul Morrison. version 0.6 ALPHA
   Feb 9, 1995 (dp)    Updated for 1.1.90 kernel
                       version 0.7 ALPHA
   Apr 6, 1995 (ac)    Tweaks for NET3 pre snapshot 002 AX.25
   April 23, 1995 (dp) Fixed ioctl so it works properly with piconfig program
                       when changing the baud rate or clock mode.
                       version 0.8 ALPHA
   July 17, 1995 (ac)  Finally polishing of AX25.030+ support
   Oct  29, 1995 (ac)  A couple of minor fixes before this, and this release changes
   		       to the proper set_mac_address semantics which will break
   		       a few programs I suspect.
   Aug  18, 1996 (jsn) Converted to be used as a module.
   Dec  13, 1996 (jsn) Fixed to match Linux networking changes.
*/

/* The following #define invokes a hack that will improve performance (baud)
   for the B port. The up side is it makes 9600 baud work ok on the B port.
   It may do 38400, depending on the host. The down side is it burns up
   CPU cycles with ints locked for up to 1 character time, at the beginning
   of each transmitted packet. If this causes you to lose sleep, #undefine it.
*/

/*#define STUFF2 1*/

/* The default configuration */
#define PI_DMA 3

#define DEF_A_SPEED 0		/* 0 means external clock */
#define DEF_A_TXDELAY 15	/* 15 mS transmit delay */
#define DEF_A_PERSIST 128	/* 50% persistence */
#define DEF_A_SLOTIME 15	/* 15 mS slot time */
#define DEF_A_SQUELDELAY 1	/* 1 mS squelch delay - allows fcs and flag */
#define DEF_A_CLOCKMODE 0	/* clock mode - 0 is normal */

#define DEF_B_SPEED 1200	/* 1200 baud */
#define DEF_B_TXDELAY 40	/* 400 mS */
#define DEF_B_PERSIST 128	/* 50% */
#define DEF_B_SLOTIME 30	/* 300 mS */
#define DEF_B_SQUELDELAY 3	/* 30 mS */
#define DEF_B_CLOCKMODE 0	/* Normal clock mode */

/* The following #define is only really required for the PI card, not
   the PI2 - but it's safer to leave it in. */
#define REALLY_SLOW_IO 1

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
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
#include <linux/pi2.h>
#include <linux/init.h>
#include "z8530.h"
#include <net/ax25.h>

struct mbuf {
    struct mbuf *next;
    int cnt;
    char data[0];
};

/*
 *	The actual devices we will use
 */

/*
 *	PI device declarations.
 */

static int pi0_preprobe(struct device *dev){return 0;}	/* Dummy probe function */
static struct device pi0a = { "pi0a", 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, pi0_preprobe };
static struct device pi0b = { "pi0b", 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, pi0_preprobe };


/* The number of low I/O ports used by the card. */
#define PI_TOTAL_SIZE	8


/* Index to functions, as function prototypes. */

static int pi_probe(struct device *dev, int card_type);
static int pi_open(struct device *dev);
static int pi_send_packet(struct sk_buff *skb, struct device *dev);
static void pi_interrupt(int reg_ptr, void *dev_id, struct pt_regs *regs);
static int pi_close(struct device *dev);
static int pi_ioctl(struct device *dev, struct ifreq *ifr, int cmd);
static struct net_device_stats *pi_get_stats(struct device *dev);
static void rts(struct pi_local *lp, int x);
static void b_rxint(struct device *dev, struct pi_local *lp);
static void b_txint(struct pi_local *lp);
static void b_exint(struct pi_local *lp);
static void a_rxint(struct device *dev, struct pi_local *lp);
static void a_txint(struct pi_local *lp);
static void a_exint(struct pi_local *lp);
static char *get_dma_buffer(unsigned long *mem_ptr);
static int valid_dma_page(unsigned long addr, unsigned long dev_buffsize);

static char ax25_bcast[7] =
{'Q' << 1, 'S' << 1, 'T' << 1, ' ' << 1, ' ' << 1, ' ' << 1, '0' << 1};
static char ax25_test[7] =
{'L' << 1, 'I' << 1, 'N' << 1, 'U' << 1, 'X' << 1, ' ' << 1, '1' << 1};

static int ext2_secrm_seed = 152;	/* Random generator base */

extern inline unsigned char random(void)
{
	return (unsigned char) (ext2_secrm_seed = ext2_secrm_seed
			    * 69069l + 1);
}

extern inline void wrtscc(int cbase, int ctl, int sccreg, int val)
{
	/* assume caller disables interrupts! */
	outb_p(0, cbase + DMAEN);	/* Disable DMA while we touch the scc */
	outb_p(sccreg, ctl);		/* Select register */
	outb_p(val, ctl);		/* Output value */
	outb_p(1, cbase + DMAEN);	/* Enable DMA */
}

extern inline int rdscc(int cbase, int ctl, int sccreg)
{
	int retval;

	/* assume caller disables interrupts! */
	outb_p(0, cbase + DMAEN);	/* Disable DMA while we touch the scc */
	outb_p(sccreg, ctl);	/* Select register */
	retval = inb_p(ctl);
	outb_p(1, cbase + DMAEN);	/* Enable DMA */
	return retval;
}

static void switchbuffers(struct pi_local *lp)
{
	if (lp->rcvbuf == lp->rxdmabuf1)
		lp->rcvbuf = lp->rxdmabuf2;
	else
		lp->rcvbuf = lp->rxdmabuf1;
}

static void hardware_send_packet(struct pi_local *lp, struct sk_buff *skb)
{
	char kickflag;
	unsigned long flags;

	lp->stats.tx_packets++;

	save_flags(flags);
	cli();
	kickflag = (skb_peek(&lp->sndq) == NULL) && (lp->sndbuf == NULL);
	restore_flags(flags);

	skb_queue_tail(&lp->sndq, skb);
	if (kickflag) 
	{
		/* simulate interrupt to xmit */
		switch (lp->base & 2) 
		{
			case 2:
				a_txint(lp);	/* process interrupt */
				break;
			case 0:
				save_flags(flags);
				cli();
				if (lp->tstate == IDLE)
					b_txint(lp);
				restore_flags(flags);
				break;
		}
	}
}

static void setup_rx_dma(struct pi_local *lp)
{
    unsigned long flags;
    int cmd;
    unsigned long dma_abs;
    unsigned dmachan;

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

    /* Set DMA mode register to single transfers, incrementing address,
     *  auto init, writes
     */
    set_dma_mode(dmachan, DMA_MODE_READ | 0x10);
    set_dma_addr(dmachan, dma_abs);
    set_dma_count(dmachan, lp->bufsiz);
    enable_dma(dmachan);

    /* If a packet is already coming in, this line is supposed to
	   avoid receiving a partial packet.
     */
    wrtscc(lp->cardbase, cmd, R0, RES_Rx_CRC);

    /* Enable RX dma */
    wrtscc(lp->cardbase, cmd, R1,
      WT_RDY_ENAB | WT_FN_RDYFN | WT_RDY_RT | INT_ERR_Rx | EXT_INT_ENAB);

    restore_flags(flags);
}

static void setup_tx_dma(struct pi_local *lp, int length)
{
    unsigned long dma_abs;
    unsigned long flags;
    unsigned long dmachan;

    save_flags(flags);
    cli();

    dmachan = lp->dmachan;
    dma_abs = (unsigned long) (lp->txdmabuf);

    if(!valid_dma_page(dma_abs, DMA_BUFF_SIZE + sizeof(struct mbuf)))
	panic("PI: TX buffer violates DMA boundary!");

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

static void tdelay(struct pi_local *lp, int time)
{
    int port;
    unsigned int t1;
    unsigned char sc;

    if (lp->base & 2) {		/* If A channel */
	sc = SC1;
	t1 = time;
	port = lp->cardbase + TMR1;
    } else {
	sc = SC2;
	t1 = 10 * time;		/* 10s of milliseconds for the B channel */
	port = lp->cardbase + TMR2;
	wrtscc(lp->cardbase, lp->base + CTL, R1, INT_ALL_Rx | EXT_INT_ENAB);
    }

    /* Setup timer sc */
    outb_p(sc | LSB_MSB | MODE0, lp->cardbase + TMRCMD);

    /* times 2 to make millisecs */
    outb_p((t1 << 1) & 0xFF, port);
    outb_p((t1 >> 7) & 0xFF, port);

    /* Enable correct int for timeout */
    wrtscc(lp->cardbase, lp->base + CTL, R15, CTSIE);
    wrtscc(lp->cardbase, lp->base + CTL, R0, RES_EXT_INT);
}

static void a_txint(struct pi_local *lp)
{
    int cmd;
    unsigned long flags;

    save_flags(flags);
    cli();

    cmd = CTL + lp->base;

    switch (lp->tstate) {
    case IDLE:
	/* Transmitter idle. Find a frame for transmission */
	if ((lp->sndbuf = skb_dequeue(&lp->sndq)) == NULL) {
	    rts(lp, OFF);
	    restore_flags(flags);
	    return;
	}
	/* If a buffer to send, we drop thru here */
    case DEFER:
	/* we may have deferred prev xmit attempt */
	/* Check DCD - debounce it
         * See Intel Microcommunications Handbook, p2-308
         */
	wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	if ((rdscc(lp->cardbase, cmd, R0) & DCD) != 0) {
	    lp->tstate = DEFER;
	    tdelay(lp, 100);
	    /* defer until DCD transition or timeout */
	    wrtscc(lp->cardbase, cmd, R15, CTSIE | DCDIE);
	    restore_flags(flags);
	    return;
	}
	if (random() > lp->persist) {
	    lp->tstate = DEFER;
	    tdelay(lp, lp->slotime);
	    restore_flags(flags);
	    return;
	}
	/* Assert RTS early minimize collision window */
	wrtscc(lp->cardbase, cmd, R5, TxCRC_ENAB | RTS | Tx8);
	rts(lp, ON);		/* Transmitter on */
	lp->tstate = ST_TXDELAY;
	tdelay(lp, lp->txdelay);
	restore_flags(flags);
	return;
    default:
	break;
    }				/* end switch(lp->state) */

    restore_flags(flags);
}				/*a_txint */

static void a_exint(struct pi_local *lp)
{
    unsigned long flags;
    int cmd;
    char st;
    int length;

    save_flags(flags);
    cli();			/* disable interrupts */

    st = rdscc(lp->cardbase, lp->base + CTL, R0);	/* Fetch status */

    /* reset external status latch */
    wrtscc(lp->cardbase, CTL + lp->base, R0, RES_EXT_INT);
    cmd = lp->base + CTL;

    if ((lp->rstate >= ACTIVE) && (st & BRK_ABRT)) {
	setup_rx_dma(lp);
	lp->rstate = ACTIVE;
    }
    switch (lp->tstate) {
    case ACTIVE:
	kfree_skb(lp->sndbuf);
	lp->sndbuf = NULL;
	lp->tstate = FLAGOUT;
	tdelay(lp, lp->squeldelay);
	break;
    case FLAGOUT:
	if ((lp->sndbuf = skb_dequeue(&lp->sndq)) == NULL) {
	    /* Nothing to send - return to receive mode */
	    lp->tstate = IDLE;
	    rts(lp, OFF);
	    restore_flags(flags);
	    return;
	}
	/* NOTE - fall through if more to send */
    case ST_TXDELAY:
	/* Disable DMA chan */
	disable_dma(lp->dmachan);

	/* Set up for TX dma */
	wrtscc(lp->cardbase, cmd, R1, WT_FN_RDYFN | EXT_INT_ENAB);


	/* Get all chars */
	/* Strip KISS control byte */
	length = lp->sndbuf->len - 1;
	memcpy(lp->txdmabuf, &lp->sndbuf->data[1], length);


	/* Setup DMA controller for tx */
	setup_tx_dma(lp, length);

	/* select transmit interrupts to enable */
	/* Allow DMA on chan */
	enable_dma(lp->dmachan);

	/* reset CRC, Txint pend*/
	wrtscc(lp->cardbase, cmd, R0, RES_Tx_CRC | RES_Tx_P);

	/* allow Underrun int only */
	wrtscc(lp->cardbase, cmd, R15, TxUIE);

	/* Enable TX DMA */
	wrtscc(lp->cardbase, cmd, R1, WT_RDY_ENAB | WT_FN_RDYFN | EXT_INT_ENAB);

	/* Send CRC on underrun */
	wrtscc(lp->cardbase, cmd, R0, RES_EOM_L);


	/* packet going out now */
	lp->tstate = ACTIVE;
	break;
    case DEFER:
	/* we have deferred prev xmit attempt
         * See Intel Microcommunications Handbook, p2-308
         */
	wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	if ((rdscc(lp->cardbase, cmd, R0) & DCD) != 0) {
	    lp->tstate = DEFER;
	    tdelay(lp, 100);
	    /* Defer until dcd transition or 100mS timeout */
	    wrtscc(lp->cardbase, CTL + lp->base, R15, CTSIE | DCDIE);
	    restore_flags(flags);
	    return;
	}
	if (random() > lp->persist) {
	    lp->tstate = DEFER;
	    tdelay(lp, lp->slotime);
	    restore_flags(flags);
	    return;
	}
	/* Assert RTS early minimize collision window */
	wrtscc(lp->cardbase, cmd, R5, TxCRC_ENAB | RTS | Tx8);
	rts(lp, ON);		/* Transmitter on */
	lp->tstate = ST_TXDELAY;
	tdelay(lp, lp->txdelay);
	restore_flags(flags);
	return;
    }				/* switch(lp->tstate) */

    restore_flags(flags);
}				/* a_exint() */

/* Receive interrupt handler for the A channel
 */
static void a_rxint(struct device *dev, struct pi_local *lp)
{
    unsigned long flags;
    int cmd;
    int bytecount;
    char rse;
    struct sk_buff *skb;
    int sksize, pkt_len;
    struct mbuf *cur_buf;
    unsigned char *cfix;

    save_flags(flags);
    cli();			/* disable interrupts */
    cmd = lp->base + CTL;

    rse = rdscc(lp->cardbase, cmd, R1);	/* Get special condition bits from R1 */
    if (rse & Rx_OVR)
	lp->rstate = RXERROR;

    if (rse & END_FR) {
	/* If end of frame */
	/* figure length of frame from 8237 */
	clear_dma_ff(lp->dmachan);
	bytecount = lp->bufsiz - get_dma_residue(lp->dmachan);

	if ((rse & CRC_ERR) || (lp->rstate > ACTIVE) || (bytecount < 10)) {
	    if ((bytecount >= 10) && (rse & CRC_ERR)) {
		lp->stats.rx_crc_errors++;
	    }
	    if (lp->rstate == RXERROR) {
		lp->stats.rx_errors++;
		lp->stats.rx_over_errors++;
	    }
	    /* Reset buffer pointers */
	    lp->rstate = ACTIVE;
	    setup_rx_dma(lp);
	} else {
	    /* Here we have a valid frame */
	    /* Toss 2 crc bytes , add one for KISS */
	    pkt_len = lp->rcvbuf->cnt = bytecount - 2 + 1;

	    /* Get buffer for next frame */
	    cur_buf = lp->rcvbuf;
	    switchbuffers(lp);
	    setup_rx_dma(lp);


	    /* Malloc up new buffer. */
	    sksize = pkt_len;

	    skb = dev_alloc_skb(sksize);
	    if (skb == NULL) {
		printk(KERN_ERR "PI: %s: Memory squeeze, dropping packet.\n", dev->name);
		lp->stats.rx_dropped++;
		restore_flags(flags);
		return;
	    }
	    skb->dev = dev;

	    /* KISS kludge -  prefix with a 0 byte */
	    cfix=skb_put(skb,pkt_len);
	    *cfix++=0;
	    /* 'skb->data' points to the start of sk_buff data area. */
	    memcpy(cfix, (char *) cur_buf->data,
		   pkt_len - 1);
	    skb->protocol=htons(ETH_P_AX25);
	    skb->mac.raw=skb->data;
	    netif_rx(skb);
	    lp->stats.rx_packets++;
	}			/* end good frame */
    }				/* end EOF check */
    wrtscc(lp->cardbase, lp->base + CTL, R0, ERR_RES);	/* error reset */
    restore_flags(flags);
}

static void b_rxint(struct device *dev, struct pi_local *lp)
{
    unsigned long flags;
    int cmd;
    char rse;
    struct sk_buff *skb;
    int sksize;
    int pkt_len;
    unsigned char *cfix;

    save_flags(flags);
    cli();			/* disable interrupts */
    cmd = CTL + lp->base;

    rse = rdscc(lp->cardbase, cmd, R1);	/* get status byte from R1 */

    if ((rdscc(lp->cardbase, cmd, R0)) & Rx_CH_AV) {
	/* there is a char to be stored
         * read special condition bits before reading the data char
         */
	if (rse & Rx_OVR) {
	    /* Rx overrun - toss buffer */
	    /* reset buffer pointers */
	    lp->rcp = lp->rcvbuf->data;
	    lp->rcvbuf->cnt = 0;

	    lp->rstate = RXERROR;	/* set error flag */
	    lp->stats.rx_errors++;
	    lp->stats.rx_over_errors++;
	} else if (lp->rcvbuf->cnt >= lp->bufsiz) {
	    /* Too large -- toss buffer */
	    /* reset buffer pointers */
	    lp->rcp = lp->rcvbuf->data;
	    lp->rcvbuf->cnt = 0;
	    lp->rstate = TOOBIG;/* when set, chars are not stored */
	}
	/* ok, we can store the received character now */
	if (lp->rstate == ACTIVE) {	/* If no errors... */
	    *lp->rcp++ = rdscc(lp->cardbase, cmd, R8);	/* char to rcv buff */
	    lp->rcvbuf->cnt++;	/* bump count */
	} else {
	    /* got to empty FIFO */
	    (void) rdscc(lp->cardbase, cmd, R8);
	    wrtscc(lp->cardbase, cmd, R0, ERR_RES);	/* reset err latch */
	    lp->rstate = ACTIVE;
	}
    }
    if (rse & END_FR) {
	/* END OF FRAME -- Make sure Rx was active */
	if (lp->rcvbuf->cnt > 0) {
	    if ((rse & CRC_ERR) || (lp->rstate > ACTIVE) || (lp->rcvbuf->cnt < 10)) {
		if ((lp->rcvbuf->cnt >= 10) && (rse & CRC_ERR)) {
		    lp->stats.rx_crc_errors++;
		}
		lp->rcp = lp->rcvbuf->data;
		lp->rcvbuf->cnt = 0;
	    } else {
		/* Here we have a valid frame */
		pkt_len = lp->rcvbuf->cnt -= 2;	/* Toss 2 crc bytes */
		pkt_len += 1;	/* Make room for KISS control byte */

		/* Malloc up new buffer. */
		sksize = pkt_len;
		skb = dev_alloc_skb(sksize);
		if (skb == NULL) {
		    printk(KERN_ERR "PI: %s: Memory squeeze, dropping packet.\n", dev->name);
		    lp->stats.rx_dropped++;
		    restore_flags(flags);
		    return;
		}
		skb->dev = dev;

		/* KISS kludge -  prefix with a 0 byte */
		cfix=skb_put(skb,pkt_len);
		*cfix++=0;
		/* 'skb->data' points to the start of sk_buff data area. */
		memcpy(cfix, lp->rcvbuf->data, pkt_len - 1);
		skb->protocol=ntohs(ETH_P_AX25);
		skb->mac.raw=skb->data;
		netif_rx(skb);
		lp->stats.rx_packets++;
		/* packet queued - initialize buffer for next frame */
		lp->rcp = lp->rcvbuf->data;
		lp->rcvbuf->cnt = 0;

	    }			/* end good frame queued */
	}			/* end check for active receive upon EOF */
	lp->rstate = ACTIVE;	/* and clear error status */
    }				/* end EOF check */
    restore_flags(flags);
}


static void b_txint(struct pi_local *lp)
{
    unsigned long flags;
    int cmd;
    unsigned char c;

    save_flags(flags);
    cli();
    cmd = CTL + lp->base;

    switch (lp->tstate) {
    case CRCOUT:
	lp->tstate = FLAGOUT;
	tdelay(lp, lp->squeldelay);
	restore_flags(flags);
	return;
    case IDLE:
	/* Transmitter idle. Find a frame for transmission */
	if ((lp->sndbuf = skb_dequeue(&lp->sndq)) == NULL) {
	    /* Nothing to send - return to receive mode
             * Tx OFF now - flag should have gone
             */
	    rts(lp, OFF);

	    restore_flags(flags);
	    return;
	}
	lp->txptr = lp->sndbuf->data;
	lp->txptr++;		/* Ignore KISS control byte */
	lp->txcnt = (int) lp->sndbuf->len - 1;
	/* If a buffer to send, we drop thru here */
    case DEFER:		/* we may have deferred prev xmit attempt */
	/* Check DCD - debounce it */
	/* See Intel Microcommunications Handbook, p2-308 */
	wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	if ((rdscc(lp->cardbase, cmd, R0) & DCD) != 0) {
	    lp->tstate = DEFER;
	    tdelay(lp, 100);
	    /* defer until DCD transition or timeout */
	    wrtscc(lp->cardbase, cmd, R15, CTSIE | DCDIE);
	    restore_flags(flags);
	    return;
	}
	if (random() > lp->persist) {
	    lp->tstate = DEFER;
	    tdelay(lp, lp->slotime);
	    restore_flags(flags);
	    return;
	}
	rts(lp, ON);		/* Transmitter on */
	lp->tstate = ST_TXDELAY;
	tdelay(lp, lp->txdelay);
	restore_flags(flags);
	return;

    case ACTIVE:
	/* Here we are actively sending a frame */
	if (lp->txcnt--) {
	    c = *lp->txptr++;
	    /* next char is gone */
	    wrtscc(lp->cardbase, cmd, R8, c);
	    /* stuffing a char satisfies Interrupt condition */
	} else {
	    /* No more to send */
	    kfree_skb(lp->sndbuf);
	    lp->sndbuf = NULL;
	    if ((rdscc(lp->cardbase, cmd, R0) & 0x40)) {
		/* Did we underrun? */
		/* unexpected underrun */
		lp->stats.tx_errors++;
		lp->stats.tx_fifo_errors++;
		wrtscc(lp->cardbase, cmd, R0, SEND_ABORT);
		lp->tstate = FLAGOUT;
		tdelay(lp, lp->squeldelay);
		restore_flags(flags);
		return;
	    }
	    lp->tstate = UNDERRUN;	/* Now we expect to underrun */
	    /* Send flags on underrun */
	    if (lp->speed) {	/* If internally clocked */
		wrtscc(lp->cardbase, cmd, R10, CRCPS | NRZI);
	    } else {
		wrtscc(lp->cardbase, cmd, R10, CRCPS);
	    }
	    wrtscc(lp->cardbase, cmd, R0, RES_Tx_P);	/* reset Tx Int Pend */
	}
	restore_flags(flags);
	return;			/* back to wait for interrupt */
    }				/* end switch */
    restore_flags(flags);
}

/* Pi SIO External/Status interrupts (for the B channel)
 * This can be caused by a receiver abort, or a Tx UNDERRUN/EOM.
 * Receiver automatically goes to Hunt on an abort.
 *
 * If the Tx Underrun interrupt hits, change state and
 * issue a reset command for it, and return.
 */
static void b_exint(struct pi_local *lp)
{
    unsigned long flags;
    char st;
    int cmd;
    char c;

    cmd = CTL + lp->base;
    save_flags(flags);
    cli();			/* disable interrupts */
    st = rdscc(lp->cardbase, cmd, R0);	/* Fetch status */
    /* reset external status latch */
    wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);


    switch (lp->tstate) {
    case ACTIVE:		/* Unexpected underrun */
	kfree_skb(lp->sndbuf);
	lp->sndbuf = NULL;
	wrtscc(lp->cardbase, cmd, R0, SEND_ABORT);
	lp->tstate = FLAGOUT;
	lp->stats.tx_errors++;
	lp->stats.tx_fifo_errors++;
	tdelay(lp, lp->squeldelay);
	restore_flags(flags);
	return;
    case UNDERRUN:
	lp->tstate = CRCOUT;
	restore_flags(flags);
	return;
    case FLAGOUT:
	/* Find a frame for transmission */
	if ((lp->sndbuf = skb_dequeue(&lp->sndq)) == NULL) {
	    /* Nothing to send - return to receive mode
             * Tx OFF now - flag should have gone
             */
	    rts(lp, OFF);
	    lp->tstate = IDLE;
	    restore_flags(flags);
	    return;
	}
	lp->txptr = lp->sndbuf->data;
	lp->txptr++;		/* Ignore KISS control byte */
	lp->txcnt = (int) lp->sndbuf->len - 1;
	/* Get first char to send */
	lp->txcnt--;
	c = *lp->txptr++;
	wrtscc(lp->cardbase, cmd, R0, RES_Tx_CRC);	/* reset for next frame */

	/* Send abort on underrun */
	if (lp->speed) {	/* If internally clocked */
	    wrtscc(lp->cardbase, cmd, R10, CRCPS | NRZI | ABUNDER);
	} else {
	    wrtscc(lp->cardbase, cmd, R10, CRCPS | ABUNDER);
	}

	wrtscc(lp->cardbase, cmd, R8, c);	/* First char out now */
	wrtscc(lp->cardbase, cmd, R0, RES_EOM_L);	/* Reset end of message latch */

#ifdef STUFF2
        /* stuff an extra one if we can */
	if (lp->txcnt) {
	    lp->txcnt--;
	    c = *lp->txptr++;
	    /* Wait for tx buffer empty */
	    while((rdscc(lp->cardbase, cmd, R0) & 0x04) == 0)
		;
	    wrtscc(lp->cardbase, cmd, R8, c);
	}
#endif

	/* select transmit interrupts to enable */

	wrtscc(lp->cardbase, cmd, R15, TxUIE);	/* allow Underrun int only */
	wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	wrtscc(lp->cardbase, cmd, R1, TxINT_ENAB | EXT_INT_ENAB);	/* Tx/Ext ints */

	lp->tstate = ACTIVE;	/* char going out now */
	restore_flags(flags);
	return;

    case DEFER:
	/* Check DCD - debounce it
         * See Intel Microcommunications Handbook, p2-308
         */
	wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	if ((rdscc(lp->cardbase, cmd, R0) & DCD) != 0) {
	    lp->tstate = DEFER;
	    tdelay(lp, 100);
	    /* defer until DCD transition or timeout */
	    wrtscc(lp->cardbase, cmd, R15, CTSIE | DCDIE);
	    restore_flags(flags);
	    return;
	}
	if (random() > lp->persist) {
	    lp->tstate = DEFER;
	    tdelay(lp, lp->slotime);
	    restore_flags(flags);
	    return;
	}
	rts(lp, ON);		/* Transmitter on */
	lp->tstate = ST_TXDELAY;
	tdelay(lp, lp->txdelay);
	restore_flags(flags);
	return;

    case ST_TXDELAY:

	/* Get first char to send */
	lp->txcnt--;
	c = *lp->txptr++;
	wrtscc(lp->cardbase, cmd, R0, RES_Tx_CRC);	/* reset for next frame */

	/* Send abort on underrun */
	if (lp->speed) {	/* If internally clocked */
	    wrtscc(lp->cardbase, cmd, R10, CRCPS | NRZI | ABUNDER);
	} else {
	    wrtscc(lp->cardbase, cmd, R10, CRCPS | ABUNDER);
	}

	wrtscc(lp->cardbase, cmd, R8, c);	/* First char out now */
	wrtscc(lp->cardbase, cmd, R0, RES_EOM_L);	/* Reset end of message latch */

#ifdef STUFF2
        /* stuff an extra one if we can */
	if (lp->txcnt) {
	    lp->txcnt--;
	    c = *lp->txptr++;
	    /* Wait for tx buffer empty */
	    while((rdscc(lp->cardbase, cmd, R0) & 0x04) == 0)
		;
	    wrtscc(lp->cardbase, cmd, R8, c);
	}
#endif

	/* select transmit interrupts to enable */

	wrtscc(lp->cardbase, cmd, R15, TxUIE);	/* allow Underrun int only */
	wrtscc(lp->cardbase, cmd, R0, RES_EXT_INT);
	/* Tx/Extern ints on */
	wrtscc(lp->cardbase, cmd, R1, TxINT_ENAB | EXT_INT_ENAB);

	lp->tstate = ACTIVE;	/* char going out now */
	restore_flags(flags);
	return;
    }

    /* Receive Mode only
     * This triggers when hunt mode is entered, & since an ABORT
     * automatically enters hunt mode, we use that to clean up
     * any waiting garbage
     */
    if ((lp->rstate == ACTIVE) && (st & BRK_ABRT)) {
	(void) rdscc(lp->cardbase, cmd, R8);
	(void) rdscc(lp->cardbase, cmd, R8);
	(void) rdscc(lp->cardbase, cmd, R8);
	lp->rcp = lp->rcvbuf->data;
	lp->rcvbuf->cnt = 0;	/* rewind on DCD transition */
    }
    restore_flags(flags);
}

/* Probe for a PI card. */
/* This routine also initializes the timer chip */

__initfunc(static int hw_probe(int ioaddr))
{
    int time = 1000;		/* Number of milliseconds for test */
    unsigned long start_time, end_time;

    int base, tmr0, tmr1, tmrcmd;
    int a = 1;
    int b = 1;

    base = ioaddr & 0x3f0;
    tmr0 = TMR0 + base;
    tmr1 = TMR1 + base;
    tmrcmd = TMRCMD + base;

    /* Set up counter chip timer 0 for 500 uS period square wave */
    /* assuming a 3.68 mhz clock for now */
    outb_p(SC0 | LSB_MSB | MODE3, tmrcmd);
    outb_p(922 & 0xFF, tmr0);
    outb_p(922 >> 8, tmr0);

    /* Setup timer control word for timer 1*/
    outb_p(SC1 | LSB_MSB | MODE0, tmrcmd);
    outb_p((time << 1) & 0xFF, tmr1);
    outb_p((time >> 7) & 0XFF, tmr1);

    /* wait until counter reg is loaded */
    do {
	/* Latch count for reading */
	outb_p(SC1, tmrcmd);
	a = inb_p(tmr1);
	b = inb_p(tmr1);
    } while (b == 0);
    start_time = jiffies;
    while (b != 0) {
	/* Latch count for reading */
	outb_p(SC1, tmrcmd);
	a = inb_p(tmr1);
	b = inb_p(tmr1);
	end_time = jiffies;
	/* Don't wait forever - there may be no card here */
	if ((end_time - start_time) > 200)
	    return 0;		/* No card found */
    }
    end_time = jiffies;
    /* 87 jiffies, for a 3.68 mhz clock, half that for a double speed clock */
    if ((end_time - start_time) > 65) {
	return (1);		/* PI card found */
    } else {
	/* Faster crystal - tmr0 needs adjusting */
	/* Set up counter chip */
	/* 500 uS square wave */
	outb_p(SC0 | LSB_MSB | MODE3, tmrcmd);
	outb_p(1844 & 0xFF, tmr0);
	outb_p(1844 >> 8, tmr0);
	return (2);		/* PI2 card found */
    }
}

static void rts(struct pi_local *lp, int x)
{
    int tc;
    long br;
    int cmd;
    int dummy;

    /* assumes interrupts are off */
    cmd = CTL + lp->base;

    /* Reprogram BRG and turn on transmitter to send flags */
    if (x == ON) {		/* Turn Tx ON and Receive OFF */
	/* Exints off first to avoid abort int */
	wrtscc(lp->cardbase, cmd, R15, 0);
	wrtscc(lp->cardbase, cmd, R3, Rx8);	/* Rx off */
	lp->rstate = IDLE;
	if (cmd & 2) {		/* if channel a */
	    /* Set up for TX dma */
	    wrtscc(lp->cardbase, cmd, R1, WT_FN_RDYFN | EXT_INT_ENAB);
	} else {
	    wrtscc(lp->cardbase, cmd, R1, 0);	/* No interrupts */
	}

	if (!lp->clockmode) {
	    if (lp->speed) {	/* if internally clocked */
		br = lp->speed;	/* get desired speed */
		tc = (lp->xtal / br) - 2;	/* calc 1X BRG divisor */
		wrtscc(lp->cardbase, cmd, R12, tc & 0xFF);	/* lower byte */
		wrtscc(lp->cardbase, cmd, R13, (tc >> 8) & 0xFF);	/* upper byte */
	    }
	}
	wrtscc(lp->cardbase, cmd, R5, TxCRC_ENAB | RTS | TxENAB | Tx8 | DTR);
	/* Transmitter now on */
    } else {			/* Tx OFF and Rx ON */
	lp->tstate = IDLE;
	wrtscc(lp->cardbase, cmd, R5, Tx8 | DTR);	/*  TX off */

	if (!lp->clockmode) {
	    if (lp->speed) {	/* if internally clocked */
		/* Reprogram BRG for 32x clock for receive DPLL */
		/* BRG off, keep Pclk source */
		wrtscc(lp->cardbase, cmd, R14, BRSRC);
		br = lp->speed;	/* get desired speed */
		/* calc 32X BRG divisor */
		tc = ((lp->xtal / 32) / br) - 2;
		wrtscc(lp->cardbase, cmd, R12, tc & 0xFF);	/* lower byte */
		wrtscc(lp->cardbase, cmd, R13, (tc >> 8) & 0xFF);	/* upper byte */
		/* SEARCH mode, BRG source */
		wrtscc(lp->cardbase, cmd, R14, BRSRC | SEARCH);
		/* Enable the BRG */
		wrtscc(lp->cardbase, cmd, R14, BRSRC | BRENABL);
	    }
	}
	/* Flush rx fifo */
	wrtscc(lp->cardbase, cmd, R3, Rx8);	/* Make sure rx is off */
	wrtscc(lp->cardbase, cmd, R0, ERR_RES);	/* reset err latch */
	dummy = rdscc(lp->cardbase, cmd, R1);	/* get status byte from R1 */
	(void) rdscc(lp->cardbase, cmd, R8);
	(void) rdscc(lp->cardbase, cmd, R8);

	(void) rdscc(lp->cardbase, cmd, R8);

	/* Now, turn on the receiver and hunt for a flag */
	wrtscc(lp->cardbase, cmd, R3, RxENABLE | Rx8);
	lp->rstate = ACTIVE;	/* Normal state */

	if (cmd & 2) {		/* if channel a */
	    setup_rx_dma(lp);
	} else {
	    /* reset buffer pointers */
	    lp->rcp = lp->rcvbuf->data;
	    lp->rcvbuf->cnt = 0;
	    wrtscc(lp->cardbase, cmd, R1, (INT_ALL_Rx | EXT_INT_ENAB));
	}
	wrtscc(lp->cardbase, cmd, R15, BRKIE);	/* allow ABORT int */
    }
}

static void scc_init(struct device *dev)
{
    unsigned long flags;
    struct pi_local *lp = (struct pi_local *) dev->priv;

    int tc;
    long br;
    register int cmd;

    /* Initialize 8530 channel for SDLC operation */

    cmd = CTL + lp->base;
    save_flags(flags);
    cli();

    switch (cmd & CHANA) {
    case CHANA:
	wrtscc(lp->cardbase, cmd, R9, CHRA);	/* Reset channel A */
	wrtscc(lp->cardbase, cmd, R2, 0xff);	/* Initialize interrupt vector */
	break;
    default:
	wrtscc(lp->cardbase, cmd, R9, CHRB);	/* Reset channel B */
	break;
    }

    /* Deselect all Rx and Tx interrupts */
    wrtscc(lp->cardbase, cmd, R1, 0);

    /* Turn off external interrupts (like CTS/CD) */
    wrtscc(lp->cardbase, cmd, R15, 0);

    /* X1 clock, SDLC mode */
    wrtscc(lp->cardbase, cmd, R4, SDLC | X1CLK);

    /* Tx/Rx parameters */
    if (lp->speed) {		/* Use internal clocking */
	wrtscc(lp->cardbase, cmd, R10, CRCPS | NRZI);
	if (!lp->clockmode)
	    /* Tx Clk from BRG. Rcv Clk from DPLL, TRxC pin outputs DPLL */
	    wrtscc(lp->cardbase, cmd, R11, TCBR | RCDPLL | TRxCDP | TRxCOI);
	else
	    /* Tx Clk from DPLL, Rcv Clk from DPLL, TRxC Outputs BRG */
	    wrtscc(lp->cardbase, cmd, R11, TCDPLL | RCDPLL | TRxCBR | TRxCOI);
    } else {			/* Use external clocking */
	wrtscc(lp->cardbase, cmd, R10, CRCPS);
	/* Tx Clk from Trxcl. Rcv Clk from Rtxcl, TRxC pin is input */
	wrtscc(lp->cardbase, cmd, R11, TCTRxCP);
    }

    /* Null out SDLC start address */
    wrtscc(lp->cardbase, cmd, R6, 0);

    /* SDLC flag */
    wrtscc(lp->cardbase, cmd, R7, FLAG);

    /* Set up the Transmitter but don't enable it
     *  DTR, 8 bit TX chars only
     */
    wrtscc(lp->cardbase, cmd, R5, Tx8 | DTR);

    /* Receiver initial setup */
    wrtscc(lp->cardbase, cmd, R3, Rx8);	/* 8 bits/char */

    /* Setting up BRG now - turn it off first */
    wrtscc(lp->cardbase, cmd, R14, BRSRC);	/* BRG off, keep Pclk source */

    /* set the 32x time constant for the BRG in Receive mode */

    if (lp->speed) {
	br = lp->speed;		/* get desired speed */
	tc = ((lp->xtal / 32) / br) - 2;	/* calc 32X BRG divisor */
    } else {
	tc = 14;
    }

    wrtscc(lp->cardbase, cmd, R12, tc & 0xFF);	/* lower byte */
    wrtscc(lp->cardbase, cmd, R13, (tc >> 8) & 0xFF);	/* upper byte */

    /* Following subroutine sets up and ENABLES the receiver */
    rts(lp, OFF);		/* TX OFF and RX ON */

    if (lp->speed) {
	/* DPLL frm BRG, BRG src PCLK */
	wrtscc(lp->cardbase, cmd, R14, BRSRC | SSBR);
    } else {
	/* DPLL frm rtxc,BRG src PCLK */
	wrtscc(lp->cardbase, cmd, R14, BRSRC | SSRTxC);
    }
    wrtscc(lp->cardbase, cmd, R14, BRSRC | SEARCH);	/* SEARCH mode, keep BRG src */
    wrtscc(lp->cardbase, cmd, R14, BRSRC | BRENABL);	/* Enable the BRG */

    if (!(cmd & 2))		/* if channel b */
	wrtscc(lp->cardbase, cmd, R1, (INT_ALL_Rx | EXT_INT_ENAB));

    wrtscc(lp->cardbase, cmd, R15, BRKIE);	/* ABORT int */

    /* Now, turn on the receiver and hunt for a flag */
    wrtscc(lp->cardbase, cmd, R3, RxENABLE | RxCRC_ENAB | Rx8);

    restore_flags(flags);
}

static void chipset_init(struct device *dev)
{
    int cardbase;
    unsigned long flags;

    cardbase = dev->base_addr & 0x3f0;

    save_flags(flags);
    cli();
    wrtscc(cardbase, dev->base_addr + CTL, R9, FHWRES);	/* Hardware reset */
    /* Disable interrupts with master interrupt ctrl reg */
    wrtscc(cardbase, dev->base_addr + CTL, R9, 0);
    restore_flags(flags);

}


__initfunc(int pi_init(void))
{
    int *port;
    int ioaddr = 0;
    int card_type = 0;
    int ports[] = {0x380, 0x300, 0x320, 0x340, 0x360, 0x3a0, 0};

    printk(KERN_INFO "PI: V0.8 ALPHA April 23 1995 David Perry (dp@hydra.carleton.ca)\n");

    /* Only one card supported for now */
    for (port = &ports[0]; *port && !card_type; port++) {
	ioaddr = *port;

	if (check_region(ioaddr, PI_TOTAL_SIZE) == 0) {
	    printk(KERN_INFO "PI: Probing for card at address %#3x\n",ioaddr);
	    card_type = hw_probe(ioaddr);
	}
    }

    switch (card_type) {
    case 1:
	printk(KERN_INFO "PI: Found a PI card at address %#3x\n", ioaddr);
	break;
    case 2:
	printk(KERN_INFO "PI: Found a PI2 card at address %#3x\n", ioaddr);
	break;
    default:
	printk(KERN_ERR "PI: ERROR: No card found\n");
	return -EIO;
    }

    /* Link a couple of device structures into the chain */
    /* For the A port */
    /* Allocate space for 4 buffers even though we only need 3,
       because one of them may cross a DMA page boundary and
       be rejected by get_dma_buffer().
    */
    register_netdev(&pi0a);

    pi0a.priv = kmalloc(sizeof(struct pi_local) + (DMA_BUFF_SIZE + sizeof(struct mbuf)) * 4, GFP_KERNEL | GFP_DMA);

    pi0a.dma = PI_DMA;
    pi0a.base_addr = ioaddr + 2;
    pi0a.irq = 0;

    /* And the B port */
    register_netdev(&pi0b);
    pi0b.base_addr = ioaddr;
    pi0b.irq = 0;

    pi0b.priv = kmalloc(sizeof(struct pi_local) + (DMA_BUFF_SIZE + sizeof(struct mbuf)) * 4, GFP_KERNEL | GFP_DMA);

    /* Now initialize them */
    pi_probe(&pi0a, card_type);
    pi_probe(&pi0b, card_type);

    pi0b.irq = pi0a.irq;	/* IRQ is shared */

    return 0;
}

static int valid_dma_page(unsigned long addr, unsigned long dev_buffsize)
{
    if (((addr & 0xffff) + dev_buffsize) <= 0x10000)
	return 1;
    else
	return 0;
}

static int pi_set_mac_address(struct device *dev, void *addr)
{
    struct sockaddr *sa = (struct sockaddr *)addr;
    memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);	/* addr is an AX.25 shifted ASCII */
    return 0;						/* mac address */
}

/* Allocate a buffer which does not cross a DMA page boundary */
static char *
get_dma_buffer(unsigned long *mem_ptr)
{
    char *ret;

    ret = (char *)*mem_ptr;

    if(!valid_dma_page(*mem_ptr, DMA_BUFF_SIZE + sizeof(struct mbuf))){
	*mem_ptr += (DMA_BUFF_SIZE + sizeof(struct mbuf));
	ret = (char *)*mem_ptr;
    }
    *mem_ptr += (DMA_BUFF_SIZE + sizeof(struct mbuf));
    return (ret);
}

static int pi_probe(struct device *dev, int card_type)
{
    short ioaddr;
    struct pi_local *lp;
    unsigned long flags;
    unsigned long mem_ptr;

    ioaddr = dev->base_addr;

    /* Initialize the device structure. */
    /* Must be done before chipset_init */
    /* Make certain the data structures used by the PI2 are aligned. */
    dev->priv = (void *) (((int) dev->priv + 7) & ~7);
    lp = (struct pi_local *) dev->priv;

    memset(dev->priv, 0, sizeof(struct pi_local));

    /* Allocate some buffers which do not cross DMA page boundaries */
    mem_ptr = (unsigned long) dev->priv + sizeof(struct pi_local);
    lp->txdmabuf = get_dma_buffer(&mem_ptr);
    lp->rxdmabuf1 = (struct mbuf *) get_dma_buffer(&mem_ptr);
    lp->rxdmabuf2 = (struct mbuf *) get_dma_buffer(&mem_ptr);

    /* Initialize rx buffer */
    lp->rcvbuf = lp->rxdmabuf1;
    lp->rcp = lp->rcvbuf->data;
    lp->rcvbuf->cnt = 0;

    /* Initialize the transmit queue head structure */
    skb_queue_head_init(&lp->sndq);

    /* These need to be initialized before scc_init is called. */
    if (card_type == 1)
	lp->xtal = (unsigned long) SINGLE / 2;
    else
	lp->xtal = (unsigned long) DOUBLE / 2;
    lp->base = dev->base_addr;
    lp->cardbase = dev->base_addr & 0x3f0;
    if (dev->base_addr & CHANA) {
	lp->speed = DEF_A_SPEED;
	/* default channel access Params */
	lp->txdelay = DEF_A_TXDELAY;
	lp->persist = DEF_A_PERSIST;
	lp->slotime = DEF_A_SLOTIME;
	lp->squeldelay = DEF_A_SQUELDELAY;
	lp->clockmode = DEF_A_CLOCKMODE;

    } else {
	lp->speed = DEF_B_SPEED;
	/* default channel access Params */
	lp->txdelay = DEF_B_TXDELAY;
	lp->persist = DEF_B_PERSIST;
	lp->slotime = DEF_B_SLOTIME;
	lp->squeldelay = DEF_B_SQUELDELAY;
	lp->clockmode = DEF_B_CLOCKMODE;
    }
    lp->bufsiz = DMA_BUFF_SIZE;
    lp->tstate = IDLE;

    chipset_init(dev);

    if (dev->base_addr & CHANA) {	/* Do these things only for the A port */
	/* Note that a single IRQ services 2 devices (A and B channels) */

	lp->dmachan = dev->dma;
	if (lp->dmachan < 1 || lp->dmachan > 3)
	    printk(KERN_ERR "PI: DMA channel %d out of range\n", lp->dmachan);

	/* chipset_init() was already called */

	if (dev->irq < 2) {
	    autoirq_setup(0);
	    save_flags(flags);
	    cli();
	    wrtscc(lp->cardbase, CTL + lp->base, R1, EXT_INT_ENAB);
	    /* enable PI card interrupts */
	    wrtscc(lp->cardbase, CTL + lp->base, R9, MIE | NV);
	    restore_flags(flags);
	    /* request a timer interrupt for 1 mS hence */
	    tdelay(lp, 1);
	    /* 20 "jiffies" should be plenty of time... */
	    dev->irq = autoirq_report(20);
	    if (!dev->irq) {
		printk(KERN_ERR "PI: Failed to detect IRQ line.\n");
	    }
	    save_flags(flags);
	    cli();
	    wrtscc(lp->cardbase, dev->base_addr + CTL, R9, FHWRES);	/* Hardware reset */
	    /* Disable interrupts with master interrupt ctrl reg */
	    wrtscc(lp->cardbase, dev->base_addr + CTL, R9, 0);
	    restore_flags(flags);
	}

	printk(KERN_INFO "PI: Autodetected IRQ %d, assuming DMA %d.\n",
	       dev->irq, dev->dma);

	/* This board has jumpered interrupts. Snarf the interrupt vector
		   now.  There is no point in waiting since no other device can use
		   the interrupt, and this marks the 'irqaction' as busy. */
	{
	    int irqval = request_irq(dev->irq, &pi_interrupt,0, "pi2", dev);
	    if (irqval) {
		printk(KERN_ERR "PI: unable to get IRQ %d (irqval=%d).\n",
		       dev->irq, irqval);
		return EAGAIN;
	    }
	}

	/* Grab the region */
	request_region(ioaddr & 0x3f0, PI_TOTAL_SIZE, "pi2" );


    }				/* Only for A port */
    dev->open = pi_open;
    dev->stop = pi_close;
    dev->do_ioctl = pi_ioctl;
    dev->hard_start_xmit = pi_send_packet;
    dev->get_stats = pi_get_stats;

    /* Fill in the fields of the device structure */

    dev_init_buffers(dev);
    
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
    dev->hard_header    = ax25_encapsulate;
    dev->rebuild_header = ax25_rebuild_header;
#endif

    dev->set_mac_address = pi_set_mac_address;

    dev->type = ARPHRD_AX25;			/* AF_AX25 device */
    dev->hard_header_len = 73;			/* We do digipeaters now */
    dev->mtu = 1500;				/* eth_mtu is the default */
    dev->addr_len = 7;				/* sizeof an ax.25 address */
    memcpy(dev->broadcast, ax25_bcast, 7);
    memcpy(dev->dev_addr, ax25_test, 7);

    /* New-style flags. */
    dev->flags = 0;
    return 0;
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine should set everything up anew at each open, even
   registers that "should" only need to be set once at boot, so that
   there is non-reboot way to recover if something goes wrong.
   */
static int pi_open(struct device *dev)
{
    unsigned long flags;
    static first_time = 1;

    struct pi_local *lp = (struct pi_local *) dev->priv;

    if (dev->base_addr & 2) {	/* if A channel */
	if (first_time) {
	    if (request_dma(dev->dma,"pi2")) {
		free_irq(dev->irq, dev);
		return -EAGAIN;
	    }
	}
	/* Reset the hardware here. */
	chipset_init(dev);
    }
    lp->tstate = IDLE;

    if (dev->base_addr & 2) {	/* if A channel */
	scc_init(dev);		/* Called once for each channel */
	scc_init(dev->next);
    }
    /* master interrupt enable */
    save_flags(flags);
    cli();
    wrtscc(lp->cardbase, CTL + lp->base, R9, MIE | NV);
    restore_flags(flags);

    lp->open_time = jiffies;

    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;
    first_time = 0;

    MOD_INC_USE_COUNT;

    return 0;
}

static int pi_send_packet(struct sk_buff *skb, struct device *dev)
{
    struct pi_local *lp = (struct pi_local *) dev->priv;

    hardware_send_packet(lp, skb);
    dev->trans_start = jiffies;

    return 0;
}

/* The typical workload of the driver:
   Handle the network interface interrupts. */
static void pi_interrupt(int reg_ptr, void *dev_id, struct pt_regs *regs)
{
/*    int irq = -(((struct pt_regs *) reg_ptr)->orig_eax + 2);*/
    struct pi_local *lp;
    int st;
    unsigned long flags;

/*    dev_b = dev_a->next;	 Relies on the order defined in Space.c */

#if 0
    if (dev_a == NULL) {
	printk(KERN_ERR "PI: pi_interrupt(): irq %d for unknown device.\n", irq);
	return;
    }
#endif
    /* Read interrupt status register (only valid from channel A)
     * Process all pending interrupts in while loop
     */
    lp = (struct pi_local *) pi0a.priv;	/* Assume channel A */
    while ((st = rdscc(lp->cardbase, pi0a.base_addr | CHANA | CTL, R3)) != 0) {
	if (st & CHBTxIP) {
	    /* Channel B Transmit Int Pending */
	    lp = (struct pi_local *) pi0b.priv;
	    b_txint(lp);
	} else if (st & CHARxIP) {
	    /* Channel A Rcv Interrupt Pending */
	    lp = (struct pi_local *) pi0a.priv;
	    a_rxint(&pi0a, lp);
	} else if (st & CHATxIP) {
	    /* Channel A Transmit Int Pending */
	    lp = (struct pi_local *) pi0a.priv;
	    a_txint(lp);
	} else if (st & CHAEXT) {
	    /* Channel A External Status Int */
	    lp = (struct pi_local *) pi0a.priv;
	    a_exint(lp);
	} else if (st & CHBRxIP) {
	    /* Channel B Rcv Interrupt Pending */
	    lp = (struct pi_local *) pi0b.priv;
	    b_rxint(&pi0b, lp);
	} else if (st & CHBEXT) {
	    /* Channel B External Status Int */
	    lp = (struct pi_local *) pi0b.priv;
	    b_exint(lp);
	}
	/* Reset highest interrupt under service */
	save_flags(flags);
	cli();
	wrtscc(lp->cardbase, lp->base + CTL, R0, RES_H_IUS);
	restore_flags(flags);
    }				/* End of while loop on int processing */
    return;
}

/* The inverse routine to pi_open(). */
static int pi_close(struct device *dev)
{
    unsigned long flags;
    struct pi_local *lp;
    struct sk_buff *ptr;

    save_flags(flags);
    cli();

    lp = (struct pi_local *) dev->priv;
    ptr = NULL;

    chipset_init(dev);		/* reset the scc */
    disable_dma(lp->dmachan);

    lp->open_time = 0;

    dev->tbusy = 1;
    dev->start = 0;

    /* Free any buffers left in the hardware transmit queue */
    while ((ptr = skb_dequeue(&lp->sndq)) != NULL)
	kfree_skb(ptr);

    restore_flags(flags);

    MOD_DEC_USE_COUNT;

    return 0;
}

static int pi_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
    unsigned long flags;
    struct pi_req rq;
    struct pi_local *lp = (struct pi_local *) dev->priv;

    int ret = verify_area(VERIFY_WRITE, ifr->ifr_data, sizeof(struct pi_req));
    if (ret)
	return ret;

    if(cmd!=SIOCDEVPRIVATE)
    	return -EINVAL;

    copy_from_user(&rq, ifr->ifr_data, sizeof(struct pi_req));

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
	pi_open(&pi0a); /* both channels get reset %%% */
	restore_flags(flags);
	ret = 0;
	break;

    case SIOCSPIDMA:

	if (!suser())
	    return -EPERM;
	ret = 0;
	if (dev->base_addr & 2) {   /* if A channel */
	   if (rq.dmachan < 1 || rq.dmachan > 3)
		return -EINVAL;
	   save_flags(flags);
	   cli();
	   pi_close(dev);
	   free_dma(lp->dmachan);
	   dev->dma = lp->dmachan = rq.dmachan;
	   if (request_dma(lp->dmachan,"pi2"))
		ret = -EAGAIN;
	   pi_open(dev);
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
	copy_to_user(ifr->ifr_data, &rq, sizeof(struct pi_req));
	ret = 0;
	break;

    default:
	ret = -EINVAL;
    }
    return ret;
}

/* Get the current statistics.	This may be called with the card open or
   closed. */
static struct net_device_stats *pi_get_stats(struct device *dev)
{
	struct pi_local *lp = (struct pi_local *) dev->priv;

	return &lp->stats;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("David Perry <dp@hydra.carleton.ca>");
MODULE_DESCRIPTION("AX.25 driver for the Ottawa PI and PI/2 HDLC cards");

int init_module(void)
{
    return pi_init();
}

void cleanup_module(void)
{
    free_irq(pi0a.irq, &pi0a);	/* IRQs and IO Ports are shared */
    release_region(pi0a.base_addr & 0x3f0, PI_TOTAL_SIZE);

    kfree(pi0a.priv);
    pi0a.priv = NULL;
    unregister_netdev(&pi0a);

    kfree(pi0b.priv);
    pi0b.priv = NULL;
    unregister_netdev(&pi0b);
}
#endif
