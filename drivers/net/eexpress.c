/* $Id: eexpress.c,v 1.13 1996/05/19 15:59:51 phil Exp $
 *
 * Intel EtherExpress device driver for Linux
 *
 * Original version written 1993 by Donald Becker
 * Modularized by Pauline Middelink <middelin@polyware.iaf.nl>
 * Changed to support io= irq= by Alan Cox <Alan.Cox@linux.org>
 * Reworked 1995 by John Sullivan <js10039@cam.ac.uk>
 * More fixes by Philip Blundell <pjb27@cam.ac.uk>
 */

/*
 * The original EtherExpress driver was just about usable, but
 * suffered from a long startup delay, a hard limit of 16k memory
 * usage on the card (EtherExpress 16s have either 32k or 64k),
 * and random locks under load. The last was particularly annoying
 * and made running eXceed/W preferable to Linux/XFree. After hacking
 * through the driver for a couple of days, I had fixed most of the
 * card handling errors, at the expense of turning the code into
 * a complete jungle, but still hadn't tracked down the lock-ups.
 * I had hoped these would be an IP bug, but failed to reproduce them
 * under other drivers, so decided to start from scratch and rewrite
 * the driver cleanly. And here it is.
 *
 * It's still not quite there, but self-corrects a lot more problems.
 * the 'CU wedged, resetting...' message shouldn't happen at all, but
 * at least we recover. It still locks occasionally, any ideas welcome.
 *
 * The original startup delay experienced by some people was due to the
 * first ARP request for the address of the default router getting lost.
 * (mostly the reply we were getting back was arriving before our
 * hardware address was set up, or before the configuration sequence
 * had told the card NOT to strip of the frame header). If you a long
 * startup delay, you may have lost this ARP request/reply, although
 * the original cause has been fixed. However, it is more likely that
 * you've just locked under this version.
 *
 * The main changes are in the 586 initialization procedure (which was
 * just broken before - the EExp is a strange beasty and needs careful
 * handling) the receive buffer handling (we now use a non-terminating
 * circular list of buffers, which stops the card giving us out-of-
 * resources errors), and the transmit code. The driver is also more
 * structured, and I have tried to keep the kernel interface separate
 * from the hardware interface (although some routines naturally want
 * to do both).
 *
 * John Sullivan
 *
 * 18/5/95:
 *
 * The lock-ups seem to happen when you access card memory after a 586
 * reset. This happens only 1 in 12 resets, on a random basis, and
 * completely locks the machine. As far as I can see there is no
 * workaround possible - the only thing to be done is make sure we
 * never reset the card *after* booting the kernel - once at probe time
 * must be sufficient, and we'll just have to put up with that failing
 * occasionally (or buy a new NIC). By the way, this looks like a 
 * definite card bug, since Intel's own driver for DOS does exactly the
 * same.
 *
 * This bug makes switching in and out of promiscuous mode a risky
 * business, since we must do a 586 reset each time.
 */

/*
 * Sources:
 *
 * The original eexpress.c by Donald Becker
 *   Sources: the Crynwr EtherExpress driver source.
 *            the Intel Microcommunications Databook Vol.1 1990
 *
 * wavelan.c and i82586.h
 *   This was invaluable for the complete '586 configuration details
 *   and command format.
 *
 * The Crynwr sources (again)
 *   Not as useful as the Wavelan driver, but then I had eexpress.c to
 *   go off.
 *
 * The Intel EtherExpress 16 ethernet card
 *   Provided the only reason I want to see a working etherexpress driver.
 *   A lot of fixes came from just observing how the card (mis)behaves when
 *   you prod it.
 *
 */

static char version[] = 
"eexpress.c: v0.10 04-May-95 John Sullivan <js10039@cam.ac.uk>\n"
"            v0.14 19-May-96 Philip Blundell <phil@tazenda.demon.co.uk>\n";

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/in.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/delay.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/malloc.h>

/*
 * Not actually used yet - may be implemented when the driver has
 * been debugged!
 *
 * Debug Level		Driver Status
 *	0		Final release
 *	1		Beta test
 *	2
 *	3
 * 	4		Report timeouts & 586 errors (normal debug level)
 *	5		Report all major events
 *	6		Dump sent/received packet contents
 *	7		Report function entry/exit
 */

#ifndef NET_DEBUG
#define NET_DEBUG 4
#endif
static unsigned int net_debug = NET_DEBUG;

#undef F_DEB

#include "eth82586.h"

#define PRIV(x)         ((struct net_local *)(x)->priv)
#define EEXP_IO_EXTENT  16

/*
 * Private data declarations
 */

struct net_local 
{
	struct enet_statistics stats;
	unsigned long init_time;        /* jiffies when eexp_hw_init586 called */
	unsigned short rx_first;        /* first rx buf, same as RX_BUF_START */
	unsigned short rx_last;         /* last rx buf */
	unsigned short tx_head;         /* next free tx buf */
	unsigned short tx_reap;         /* first in-use tx buf */
	unsigned short tx_tail;         /* previous tx buf to tx_head */
	unsigned short tx_link;         /* last known-executing tx buf */
	unsigned short last_tx_restart; /* set to tx_link when we restart the CU */
	unsigned char started;
	unsigned char promisc;
	unsigned short rx_buf_start;
	unsigned short rx_buf_end;
	unsigned short num_tx_bufs;
	unsigned short num_rx_bufs;
};

unsigned short start_code[] = {
	0x0000,                 /* SCP: set bus to 16 bits */
	0x0000,0x0000,          /* junk */
	0x0000,0x0000,          /* address of ISCP (lo,hi) */

	0x0001,                 /* ISCP: busy - cleared after reset */
	0x0008,0x0000,0x0000,   /* offset,address (lo,hi) of SCB */

	0x0000,0x0000,          /* SCB: status, commands */
	0x0000,0x0000,          /* links to first command block, first receive descriptor */
	0x0000,0x0000,          /* CRC error, alignment error counts */
	0x0000,0x0000,          /* out of resources, overrun error counts */

	0x0000,0x0000,          /* pad */
	0x0000,0x0000,

	0x0000,Cmd_Config,      /* startup configure sequence, at 0x0020 */
	0x0032,                 /* link to next command */
	0x080c,                 /* 12 bytes follow : fifo threshold=8 */
	0x2e40,                 /* don't rx bad frames : SRDY/ARDY => ext. sync. : preamble len=8
	                         * take addresses from data buffers : 6 bytes/address */
	0x6000,                 /* default backoff method & priority : interframe spacing = 0x60 */
	0xf200,                 /* slot time=0x200 : max collision retry = 0xf */
	0x0000,                 /* no HDLC : normal CRC : enable broadcast : disable promiscuous/multicast modes */
	0x003c,                 /* minimum frame length = 60 octets) */

	0x0000,Cmd_INT|Cmd_SetAddr,
	0x003e,                 /* link to next command */
	0x0000,0x0000,0x0000,   /* hardware address placed here, 0x0038 */
	0x0000,Cmd_END|Cmd_Nop, /* end of configure sequence */
	0x003e,

	0x0000

};

#define CONF_LINK 0x0020
#define CONF_HW_ADDR 0x0038

/* maps irq number to EtherExpress magic value */
static char irqrmap[] = { 0,0,1,2,3,4,0,0,0,1,5,6,0,0,0,0 };

/*
 * Prototypes for Linux interface
 */

extern int                  express_probe(struct device *dev);
static int                     eexp_open (struct device *dev);
static int                     eexp_close(struct device *dev);
static struct enet_statistics *eexp_stats(struct device *dev);
static int                     eexp_xmit (struct sk_buff *buf, struct device *dev);

static void                    eexp_irq  (int irq, void *dev_addr, struct pt_regs *regs);
static void                    eexp_set_multicast(struct device *dev);

/*
 * Prototypes for hardware access functions
 */

static void           eexp_hw_rx        (struct device *dev);
static void           eexp_hw_tx        (struct device *dev, unsigned short *buf, unsigned short len);
static int            eexp_hw_probe     (struct device *dev,unsigned short ioaddr);
static unsigned short eexp_hw_readeeprom(unsigned short ioaddr, unsigned char location);

static unsigned short eexp_hw_lasttxstat(struct device *dev);
static void           eexp_hw_txrestart (struct device *dev);

static void           eexp_hw_txinit    (struct device *dev);
static void           eexp_hw_rxinit    (struct device *dev);

static void           eexp_hw_init586   (struct device *dev);
static void           eexp_hw_ASICrst   (struct device *dev);

/*
 * Linux interface
 */

/*
 * checks for presence of EtherExpress card
 */

int express_probe(struct device *dev)
{
	unsigned short *port,ports[] = { 0x0300,0x0270,0x0320,0x0340,0 };
	unsigned short ioaddr = dev->base_addr;

	if (ioaddr&0xfe00)
		return eexp_hw_probe(dev,ioaddr);
	else if (ioaddr)
		return ENXIO;

	for ( port=&ports[0] ; *port ; port++ ) 
	{
		unsigned short sum = 0;
		int i;
		for ( i=0 ; i<4 ; i++ ) 
		{
			unsigned short t;
			t = inb(*port + ID_PORT);
			sum |= (t>>4) << ((t & 0x03)<<2);
		}
		if (sum==0xbaba && !eexp_hw_probe(dev,*port)) 
			return 0;
	}
	return ENODEV;
}

/*
 * open and initialize the adapter, ready for use
 */

static int eexp_open(struct device *dev)
{
	int irq = dev->irq;
	unsigned short ioaddr = dev->base_addr;

#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: eexp_open()\n", dev->name);
#endif

	if (!irq || !irqrmap[irq]) 
		return -ENXIO;

	if (irq2dev_map[irq] ||
	      /* more consistent, surely? */
	   ((irq2dev_map[irq]=dev),0) ||
	     request_irq(irq,&eexp_irq,0,"eexpress",NULL)) 
		return -EAGAIN;

	request_region(ioaddr, EEXP_IO_EXTENT, "eexpress");
	dev->tbusy = 0;
	dev->interrupt = 0;
	eexp_hw_init586(dev);
	dev->start = 1;
	MOD_INC_USE_COUNT;
#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: leaving eexp_open()\n", dev->name);
#endif
	return 0;
}

/*
 * close and disable the interface, leaving
 * the 586 in reset
 */
static int eexp_close(struct device *dev)
{
	unsigned short ioaddr = dev->base_addr;
	int irq = dev->irq;

	dev->tbusy = 1; 
	dev->start = 0;
  
	outb(SIRQ_dis|irqrmap[irq],ioaddr+SET_IRQ);
	PRIV(dev)->started = 0;
	outw(SCB_CUsuspend|SCB_RUsuspend,ioaddr+SCB_CMD);
	outb(0,ioaddr+SIGNAL_CA);
	free_irq(irq,NULL);
	irq2dev_map[irq] = NULL;
	outb(i586_RST,ioaddr+EEPROM_Ctrl);
	release_region(ioaddr,16);
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * Return interface stats
 */

static struct enet_statistics *eexp_stats(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	/* 
	 * Hmmm, this looks a little too easy... The card maintains
	 * some stats in the SCB, and I'm not convinced we're
	 * incrementing the most sensible statistics when the card
	 * returns an error (esp. slow DMA, out-of-resources)
	 */
	return &lp->stats;
}

/*
 * Called to transmit a packet, or to allow us to right ourselves
 * if the kernel thinks we've died.
 */

static int eexp_xmit(struct sk_buff *buf, struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;

#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: eexp_xmit()\n", dev->name);
#endif

	outb(SIRQ_dis|irqrmap[dev->irq],ioaddr+SET_IRQ);
	if (dev->tbusy) 
	{
		/* This will happen, but hopefully not as often as when
		 * tbusy==0. If it happens too much, we probably ought
		 * to think about unwedging ourselves...
		 */
		if (test_bit(0,(void *)&PRIV(dev)->started)) 
		{
			if ((jiffies - dev->trans_start)>5) 
			{
				if (lp->tx_link==lp->last_tx_restart) 
				{
					unsigned short boguscount=200,rsst;
					printk(KERN_WARNING "%s: Retransmit timed out, status %04x, resetting...\n",
						dev->name,inw(ioaddr+SCB_STATUS));
					eexp_hw_txinit(dev);
					lp->last_tx_restart = 0;
					outw(lp->tx_link,ioaddr+SCB_CBL);
					outw(0,ioaddr+SCB_STATUS);
					outw(SCB_CUstart,ioaddr+SCB_CMD);
					outb(0,ioaddr+SIGNAL_CA);
					while (!SCB_complete(rsst=inw(ioaddr+SCB_STATUS))) 
					{
						if (!--boguscount) 
						{
							boguscount=200;
							printk(KERN_WARNING "%s: Reset timed out status %04x, retrying...\n",
								dev->name,rsst);
							outw(lp->tx_link,ioaddr+SCB_CBL);
							outw(0,ioaddr+SCB_STATUS);
							outw(SCB_CUstart,ioaddr+SCB_CMD);
							outb(0,ioaddr+SIGNAL_CA);
						}
					}
					dev->tbusy = 0;
					mark_bh(NET_BH);
				}
				else
				{
					unsigned short status = inw(ioaddr+SCB_STATUS);
					if (SCB_CUdead(status)) 
					{
						unsigned short txstatus = eexp_hw_lasttxstat(dev);
						printk(KERN_WARNING "%s: Transmit timed out, CU not active status %04x %04x, restarting...\n",
							dev->name, status, txstatus);
						eexp_hw_txrestart(dev);
					}
					else
					{
						unsigned short txstatus = eexp_hw_lasttxstat(dev);
						if (dev->tbusy && !txstatus) 
						{
							printk(KERN_WARNING "%s: CU wedged, status %04x %04x, resetting...\n",
								dev->name,status,txstatus);
							eexp_hw_init586(dev); 
							dev->tbusy = 0;
							mark_bh(NET_BH);
						}
					}
				}
			}
		}
		else
		{
			if ((jiffies-lp->init_time)>10)
			{
				unsigned short status = inw(ioaddr+SCB_STATUS);
				printk(KERN_WARNING "%s: i82586 startup timed out, status %04x, resetting...\n",
					dev->name, status);
				eexp_hw_init586(dev);
				dev->tbusy = 0;
				mark_bh(NET_BH);
			}
		}
	}

	if (buf==NULL) 
	{
		unsigned short status = inw(ioaddr+SCB_STATUS);
		unsigned short txstatus = eexp_hw_lasttxstat(dev);
		if (SCB_CUdead(status)) 
		{
			printk(KERN_WARNING "%s: CU has died! status %04x %04x, attempting to restart...\n",
				dev->name, status, txstatus);
			lp->stats.tx_errors++;
			eexp_hw_txrestart(dev);
		}
		dev_tint(dev);
		outb(SIRQ_en|irqrmap[dev->irq],ioaddr+SET_IRQ);
		return 0;
	}

	if (set_bit(0,(void *)&dev->tbusy)) 
	{
		lp->stats.tx_dropped++;
	}
	else
	{
		unsigned short length = (ETH_ZLEN < buf->len) ? buf->len : ETH_ZLEN;
		unsigned short *data = (unsigned short *)buf->data;

		outb(SIRQ_dis|irqrmap[dev->irq],ioaddr+SET_IRQ);
		eexp_hw_tx(dev,data,length);
		outb(SIRQ_en|irqrmap[dev->irq],ioaddr+SET_IRQ);
	}
	dev_kfree_skb(buf, FREE_WRITE);
	outb(SIRQ_en|irqrmap[dev->irq],ioaddr+SET_IRQ);
	return 0;
}

/*
 * Handle an EtherExpress interrupt
 * If we've finished initializing, start the RU and CU up.
 * If we've already started, reap tx buffers, handle any received packets,
 * check to make sure we've not become wedged.
 */

static void eexp_irq(int irq, void *dev_info, struct pt_regs *regs)
{
	struct device *dev = irq2dev_map[irq];
	struct net_local *lp;
	unsigned short ioaddr,status,ack_cmd;
	unsigned short old_rp,old_wp;

	if (dev==NULL) 
	{
		printk(KERN_WARNING "net_interrupt(): irq %d for unknown device caught by EExpress\n",irq);
		return;
	}

#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: interrupt\n", dev->name);
#endif

	dev->interrupt = 1; /* should this be reset on exit? */
  
	lp = (struct net_local *)dev->priv;
	ioaddr = dev->base_addr;

	outb(SIRQ_dis|irqrmap[irq],ioaddr+SET_IRQ);
	old_rp = inw(ioaddr+READ_PTR);
	old_wp = inw(ioaddr+WRITE_PTR);
	status = inw(ioaddr+SCB_STATUS);
	ack_cmd = SCB_ack(status);

	if (PRIV(dev)->started==0 && SCB_complete(status)) 
	{
#if NET_DEBUG > 4
		printk(KERN_DEBUG "%s: SCBcomplete event received\n", dev->name);
#endif
		while (SCB_CUstat(status)==2)
			status = inw_p(ioaddr+SCB_STATUS);
#if NET_DEBUG > 4
                printk(KERN_DEBUG "%s: CU went non-active (status = %08x)\n", dev->name, status);
#endif
		PRIV(dev)->started=1;
		outw_p(lp->tx_link,ioaddr+SCB_CBL);
		outw_p(PRIV(dev)->rx_buf_start,ioaddr+SCB_RFA);
		ack_cmd |= SCB_CUstart | SCB_RUstart;
	}
	else if (PRIV(dev)->started) 
	{
		unsigned short txstatus;
		txstatus = eexp_hw_lasttxstat(dev);
	}
  
	if (SCB_rxdframe(status)) 
	{
		eexp_hw_rx(dev);
	}

	if ((PRIV(dev)->started&2)!=0 && SCB_RUstat(status)!=4) 
	{
		printk(KERN_WARNING "%s: RU stopped status %04x, restarting...\n",
			dev->name,status);
		lp->stats.rx_errors++;
		eexp_hw_rxinit(dev);
		outw(PRIV(dev)->rx_buf_start,ioaddr+SCB_RFA);
		ack_cmd |= SCB_RUstart;
	} 
	else if (PRIV(dev)->started==1 && SCB_RUstat(status)==4) 
		PRIV(dev)->started|=2;

	outw(ack_cmd,ioaddr+SCB_CMD);
	outb(0,ioaddr+SIGNAL_CA);
	outw(old_rp,ioaddr+READ_PTR);
	outw(old_wp,ioaddr+WRITE_PTR);
	outb(SIRQ_en|irqrmap[irq],ioaddr+SET_IRQ);
	dev->interrupt = 0;
#if NET_DEBUG > 6
        printk(KERN_DEBUG "%s: leaving eexp_irq()\n", dev->name);
#endif
	return;
}

/*
 * Hardware access functions
 */

/*
 * Check all the receive buffers, and hand any received packets
 * to the upper levels. Basic sanity check on each frame
 * descriptor
 */
 
static void eexp_hw_rx(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;
	unsigned short old_wp = inw(ioaddr+WRITE_PTR);
	unsigned short old_rp = inw(ioaddr+READ_PTR);
	unsigned short rx_block = lp->rx_first;
	unsigned short boguscount = lp->num_rx_bufs;

#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: eexp_hw_rx()\n", dev->name);
#endif

	while (outw(rx_block,ioaddr+READ_PTR),boguscount--) 
	{
		unsigned short status = inw(ioaddr);
		unsigned short rfd_cmd = inw(ioaddr);
		unsigned short rx_next = inw(ioaddr);
		unsigned short pbuf = inw(ioaddr);
		unsigned short pkt_len;

		if (FD_Done(status)) 
		{
			outw(pbuf,ioaddr+READ_PTR);
			pkt_len = inw(ioaddr);

			if (rfd_cmd!=0x0000 || pbuf!=rx_block+0x16
				|| (pkt_len & 0xc000)!=0xc000) 
			{
				printk(KERN_WARNING "%s: Rx frame at %04x corrupted, status %04x, cmd %04x, "
					"next %04x, pbuf %04x, len %04x\n",dev->name,rx_block,
					status,rfd_cmd,rx_next,pbuf,pkt_len);
				boguscount++;
				continue;
			}
			else if (!FD_OK(status)) 
			{
				lp->stats.rx_errors++;
				if (FD_CRC(status)) 
					lp->stats.rx_crc_errors++;
				if (FD_Align(status))
					lp->stats.rx_frame_errors++;
				if (FD_Resrc(status))
					lp->stats.rx_fifo_errors++;
				if (FD_DMA(status))
					lp->stats.rx_over_errors++;
				if (FD_Short(status))
					lp->stats.rx_length_errors++;
			}
			else
			{
				struct sk_buff *skb;
				pkt_len &= 0x3fff;
				skb = dev_alloc_skb(pkt_len+16);
				if (skb == NULL) 
				{
					printk(KERN_WARNING "%s: Memory squeeze, dropping packet\n",dev->name);
					lp->stats.rx_dropped++;
					break;
				}
				skb->dev = dev;
				skb_reserve(skb, 2);
				outw(pbuf+10,ioaddr+READ_PTR);
				insw(ioaddr,skb_put(skb,pkt_len),(pkt_len+1)>>1);
				skb->protocol = eth_type_trans(skb,dev);
				netif_rx(skb);
				lp->stats.rx_packets++;
			}
			outw(rx_block,ioaddr+WRITE_PTR);
			outw(0x0000,ioaddr);
			outw(0x0000,ioaddr);
		}
		rx_block = rx_next;
	}
	outw(old_rp,ioaddr+READ_PTR);
	outw(old_wp,ioaddr+WRITE_PTR);
}

/*
 * Hand a packet to the card for transmission
 * If we get here, we MUST have already checked
 * to make sure there is room in the transmit
 * buffer region
 */

static void eexp_hw_tx(struct device *dev, unsigned short *buf, unsigned short len)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;
	unsigned short old_wp = inw(ioaddr+WRITE_PTR);

	outw(lp->tx_head,ioaddr+WRITE_PTR);
	outw(0x0000,ioaddr);
	outw(Cmd_INT|Cmd_Xmit,ioaddr);
	outw(lp->tx_head+0x08,ioaddr);
	outw(lp->tx_head+0x0e,ioaddr);
	outw(0x0000,ioaddr);
	outw(0x0000,ioaddr);
	outw(lp->tx_head+0x08,ioaddr);
	outw(0x8000|len,ioaddr);
	outw(-1,ioaddr);
	outw(lp->tx_head+0x16,ioaddr);
	outw(0,ioaddr);
	outsw(ioaddr,buf,(len+1)>>1);
	outw(lp->tx_tail+0x0c,ioaddr+WRITE_PTR);
	outw(lp->tx_head,ioaddr);
	dev->trans_start = jiffies;
	lp->tx_tail = lp->tx_head;
	if (lp->tx_head==TX_BUF_START+((lp->num_tx_bufs-1)*TX_BUF_SIZE)) 
		lp->tx_head = TX_BUF_START;
	else 
		lp->tx_head += TX_BUF_SIZE;
	if (lp->tx_head != lp->tx_reap) 
		dev->tbusy = 0;
	outw(old_wp,ioaddr+WRITE_PTR);
}

/*
 * Sanity check the suspected EtherExpress card
 * Read hardware address, reset card, size memory and
 * initialize buffer memory pointers. These should
 * probably be held in dev->priv, in case someone has 2
 * differently configured cards in their box (Arghhh!)
 */

static int eexp_hw_probe(struct device *dev, unsigned short ioaddr)
{
	unsigned short hw_addr[3];
	int i;
	unsigned char *chw_addr = (unsigned char *)hw_addr;

	printk("%s: EtherExpress at %#x, ",dev->name,ioaddr);

	hw_addr[0] = eexp_hw_readeeprom(ioaddr,2);
	hw_addr[1] = eexp_hw_readeeprom(ioaddr,3);
	hw_addr[2] = eexp_hw_readeeprom(ioaddr,4);

	if (hw_addr[2]!=0x00aa || ((hw_addr[1] & 0xff00)!=0x0000)) 
	{
		printk("rejected: invalid address %04x%04x%04x\n",
			hw_addr[2],hw_addr[1],hw_addr[0]);
		return ENODEV;
	}

	dev->base_addr = ioaddr;
	for ( i=0 ; i<6 ; i++ ) 
		dev->dev_addr[i] = chw_addr[5-i];

	{
		char irqmap[]={0, 9, 3, 4, 5, 10, 11, 0};
		char *ifmap[]={"AUI", "BNC", "10baseT"};
		enum iftype {AUI=0, BNC=1, TP=2};
		unsigned short setupval = eexp_hw_readeeprom(ioaddr,0);

		dev->irq = irqmap[setupval>>13];
		dev->if_port = !(setupval & 0x1000) ? AUI :
			eexp_hw_readeeprom(ioaddr,5) & 0x1 ? TP : BNC;

		printk("IRQ %d, Interface %s, ",dev->irq,ifmap[dev->if_port]);

		outb(SIRQ_dis|irqrmap[dev->irq],ioaddr+SET_IRQ);
		outb(0,ioaddr+SET_IRQ);
	}

	dev->priv = kmalloc(sizeof(struct net_local), GFP_KERNEL);
	if (!dev->priv) 
		return -ENOMEM;

	memset(dev->priv, 0, sizeof(struct net_local));

	eexp_hw_ASICrst(dev);

	{
		unsigned short i586mso = 0x023e;
		unsigned short old_wp,old_rp,old_a0,old_a1;
		unsigned short a0_0,a1_0,a0_1,a1_1;

		old_wp = inw(ioaddr+WRITE_PTR);
		old_rp = inw(ioaddr+READ_PTR);
		outw(0x8000+i586mso,ioaddr+READ_PTR);
		old_a1 = inw(ioaddr);
		outw(i586mso,ioaddr+READ_PTR);
		old_a0 = inw(ioaddr);
		outw(i586mso,ioaddr+WRITE_PTR);
		outw(0x55aa,ioaddr);
		outw(i586mso,ioaddr+READ_PTR);
		a0_0 = inw(ioaddr);
		outw(0x8000+i586mso,ioaddr+WRITE_PTR);
		outw(0x5a5a,ioaddr);
		outw(0x8000+i586mso,ioaddr+READ_PTR);
		a1_0 = inw(ioaddr);
		outw(i586mso,ioaddr+READ_PTR);
		a0_1 = inw(ioaddr);
		outw(i586mso,ioaddr+WRITE_PTR);
		outw(0x1234,ioaddr);
		outw(0x8000+i586mso,ioaddr+READ_PTR);
		a1_1 = inw(ioaddr);

		if ((a0_0 != a0_1) || (a1_0 != a1_1) ||
			(a1_0 != 0x5a5a) || (a0_0 != 0x55aa)) 
		{
			printk("32k\n");
			PRIV(dev)->rx_buf_end = 0x7ff6;
			PRIV(dev)->num_tx_bufs = 4;
		}
		else
		{
			printk("64k\n");
			PRIV(dev)->num_tx_bufs = 8;
			PRIV(dev)->rx_buf_start = TX_BUF_START + (PRIV(dev)->num_tx_bufs*TX_BUF_SIZE);
			PRIV(dev)->rx_buf_end = 0xfff6;
		}

		outw(0x8000+i586mso,ioaddr+WRITE_PTR);
		outw(old_a1,ioaddr);
		outw(i586mso,ioaddr+WRITE_PTR);
		outw(old_a0,ioaddr);
		outw(old_wp,ioaddr+WRITE_PTR);
		outw(old_rp,ioaddr+READ_PTR);
	}
  
	if (net_debug) 
		printk(version);
	dev->open = eexp_open;
	dev->stop = eexp_close;
	dev->hard_start_xmit = eexp_xmit;
	dev->get_stats = eexp_stats;
	dev->set_multicast_list = &eexp_set_multicast;
	ether_setup(dev);
	return 0;
}

/*
 *	Read a word from eeprom location (0-63?)
 */
static unsigned short eexp_hw_readeeprom(unsigned short ioaddr, unsigned char location)
{
	unsigned short cmd = 0x180|(location&0x7f);
	unsigned short rval = 0,wval = EC_CS|i586_RST;
	int i;
 
	outb(EC_CS|i586_RST,ioaddr+EEPROM_Ctrl);
	for ( i=0x100 ; i ; i>>=1 ) 
	{
		if (cmd&i) 
			wval |= EC_Wr;
		else 
			wval &= ~EC_Wr;

		outb(wval,ioaddr+EEPROM_Ctrl);
		outb(wval|EC_Clk,ioaddr+EEPROM_Ctrl);
		eeprom_delay();
		outb(wval,ioaddr+EEPROM_Ctrl);
		eeprom_delay();
	}	
	wval &= ~EC_Wr;
	outb(wval,ioaddr+EEPROM_Ctrl);
	for ( i=0x8000 ; i ; i>>=1 ) 
	{
		outb(wval|EC_Clk,ioaddr+EEPROM_Ctrl);
		eeprom_delay();
		if (inb(ioaddr+EEPROM_Ctrl)&EC_Rd) 
			rval |= i;
		outb(wval,ioaddr+EEPROM_Ctrl);
		eeprom_delay();
	}
	wval &= ~EC_CS;
	outb(wval|EC_Clk,ioaddr+EEPROM_Ctrl);
	eeprom_delay();
	outb(wval,ioaddr+EEPROM_Ctrl);
	eeprom_delay();
	return rval;
}

/*
 * Reap tx buffers and return last transmit status.
 * if ==0 then either:
 *    a) we're not transmitting anything, so why are we here?
 *    b) we've died.
 * otherwise, Stat_Busy(return) means we've still got some packets
 * to transmit, Stat_Done(return) means our buffers should be empty
 * again
 */

static unsigned short eexp_hw_lasttxstat(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;
	unsigned short old_rp = inw(ioaddr+READ_PTR);
	unsigned short old_wp = inw(ioaddr+WRITE_PTR);
	unsigned short tx_block = lp->tx_reap;
	unsigned short status;
  
	if (!test_bit(0,(void *)&dev->tbusy) && lp->tx_head==lp->tx_reap) 
		return 0x0000;

	do
	{
		outw(tx_block,ioaddr+READ_PTR);
		status = inw(ioaddr);
		if (!Stat_Done(status)) 
		{
			lp->tx_link = tx_block;
			outw(old_rp,ioaddr+READ_PTR);
			outw(old_wp,ioaddr+WRITE_PTR);
			return status;
		}
		else 
		{
			lp->last_tx_restart = 0;
			lp->stats.collisions += Stat_NoColl(status);
			if (!Stat_OK(status)) 
			{
				if (Stat_Abort(status)) 
					lp->stats.tx_aborted_errors++;
				if (Stat_TNoCar(status) || Stat_TNoCTS(status)) 
					lp->stats.tx_carrier_errors++;
				if (Stat_TNoDMA(status)) 
					lp->stats.tx_fifo_errors++;
			}
			else
				lp->stats.tx_packets++;
		}
		if (tx_block == TX_BUF_START+((lp->num_tx_bufs-1)*TX_BUF_SIZE)) 
			lp->tx_reap = tx_block = TX_BUF_START;
		else
			lp->tx_reap = tx_block += TX_BUF_SIZE;
		dev->tbusy = 0;
		mark_bh(NET_BH);
	}
	while (lp->tx_reap != lp->tx_head);

	lp->tx_link = lp->tx_tail + 0x08;
	outw(old_rp,ioaddr+READ_PTR);
	outw(old_wp,ioaddr+WRITE_PTR);

	return status;
}

/* 
 * This should never happen. It is called when some higher
 * routine detects the CU has stopped, to try to restart
 * it from the last packet we knew we were working on,
 * or the idle loop if we had finished for the time.
 */

static void eexp_hw_txrestart(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;
  
	lp->last_tx_restart = lp->tx_link;
	outw(lp->tx_link,ioaddr+SCB_CBL);
	outw(SCB_CUstart,ioaddr+SCB_CMD);
	outw(0,ioaddr+SCB_STATUS);
	outb(0,ioaddr+SIGNAL_CA);

	{
		unsigned short boguscount=50,failcount=5;
		while (!inw(ioaddr+SCB_STATUS)) 
		{
			if (!--boguscount) 
			{
				if (--failcount) 
				{
					printk(KERN_WARNING "%s: CU start timed out, status %04x, cmd %04x\n",
						dev->name, inw(ioaddr+SCB_STATUS), inw(ioaddr+SCB_CMD));
					outw(lp->tx_link,ioaddr+SCB_CBL);
					outw(0,ioaddr+SCB_STATUS);
					outw(SCB_CUstart,ioaddr+SCB_CMD);
					outb(0,ioaddr+SIGNAL_CA);
					boguscount = 100;
				}
				else
				{
					printk(KERN_WARNING "%s: Failed to restart CU, resetting board...\n",dev->name);
					eexp_hw_init586(dev);
					dev->tbusy = 0;
					mark_bh(NET_BH);
					return;
				}
			}
		}
	}
}

/*
 * Writes down the list of transmit buffers into card
 * memory. Initial separate, repeated transmits link
 * them into a circular list, such that the CU can
 * be constantly active, and unlink them as we reap
 * transmitted packet buffers, so the CU doesn't loop
 * and endlessly transmit packets. (Try hacking the driver
 * to send continuous broadcast messages, say ARP requests
 * on a subnet with Windows boxes running on Novell and
 * LAN Workplace with EMM386. Amusing to watch them all die
 * horribly leaving the Linux boxes up!)
 */

static void eexp_hw_txinit(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;
	unsigned short old_wp = inw(ioaddr+WRITE_PTR);
	unsigned short tx_block = TX_BUF_START;
	unsigned short curtbuf;

	for ( curtbuf=0 ; curtbuf<lp->num_tx_bufs ; curtbuf++ ) 
	{
		outw(tx_block,ioaddr+WRITE_PTR);
		outw(0x0000,ioaddr);
		outw(Cmd_INT|Cmd_Xmit,ioaddr);
		outw(tx_block+0x08,ioaddr);
		outw(tx_block+0x0e,ioaddr);
		outw(0x0000,ioaddr);
		outw(0x0000,ioaddr);
		outw(tx_block+0x08,ioaddr);
		outw(0x8000,ioaddr);
		outw(-1,ioaddr);
		outw(tx_block+0x16,ioaddr);
		outw(0x0000,ioaddr);
		tx_block += TX_BUF_SIZE;
	}
	lp->tx_head = TX_BUF_START;
	lp->tx_reap = TX_BUF_START;
	lp->tx_tail = tx_block - TX_BUF_SIZE;
	lp->tx_link = lp->tx_tail + 0x08;
	lp->rx_buf_start = tx_block;
	outw(old_wp,ioaddr+WRITE_PTR);
}

/* is this a standard test pattern, or dbecker randomness? */

unsigned short rx_words[] = 
{
	0xfeed,0xf00d,0xf001,0x0505,0x2424,0x6565,0xdeaf
};

/*
 * Write the circular list of receive buffer descriptors to
 * card memory. Note, we no longer mark the end of the list,
 * so if all the buffers fill up, the 82586 will loop until
 * we free one. This may sound dodgy, but it works, and
 * it makes the error detection in the interrupt handler
 * a lot simpler.
 */

static void eexp_hw_rxinit(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;
	unsigned short old_wp = inw(ioaddr+WRITE_PTR);
	unsigned short rx_block = lp->rx_buf_start;

	lp->num_rx_bufs = 0;
	lp->rx_first = rx_block;
	do 
	{
		lp->num_rx_bufs++;
		outw(rx_block,ioaddr+WRITE_PTR);
		outw(0x0000,ioaddr);
		outw(0x0000,ioaddr);
		outw(rx_block+RX_BUF_SIZE,ioaddr);
		outw(rx_block+0x16,ioaddr);
		outsw(ioaddr, rx_words, sizeof(rx_words)>>1);
		outw(0x8000,ioaddr);
		outw(-1,ioaddr);
		outw(rx_block+0x20,ioaddr);
		outw(0x0000,ioaddr);
		outw(0x8000|(RX_BUF_SIZE-0x20),ioaddr);
		lp->rx_last = rx_block;
		rx_block += RX_BUF_SIZE;
	} while (rx_block <= lp->rx_buf_end-RX_BUF_SIZE);

	outw(lp->rx_last+4,ioaddr+WRITE_PTR);
	outw(lp->rx_first,ioaddr);

	outw(old_wp,ioaddr+WRITE_PTR);
}

/*
 * Reset the 586, fill memory (including calls to
 * eexp_hw_[(rx)(tx)]init()) unreset, and start
 * the configuration sequence. We don't wait for this
 * to finish, but allow the interrupt handler to start
 * the CU and RU for us. We can't start the receive/
 * transmission system up before we know that the
 * hardware is configured correctly
 */
static void eexp_hw_init586(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;

#if NET_DEBUG > 6
        printk("%s: eexp_hw_init586()\n", dev->name);
#endif

	lp->started = 0;
	set_loopback;

	outb(SIRQ_dis|irqrmap[dev->irq],ioaddr+SET_IRQ);
	outb_p(i586_RST,ioaddr+EEPROM_Ctrl);
	udelay(2000);  /* delay 20ms */
        {
		unsigned long ofs;
		for (ofs = 0; ofs < lp->rx_buf_end; ofs += 32) {
			unsigned long i;
			outw_p(ofs, ioaddr+SM_PTR);
			for (i = 0; i < 16; i++) {
				outw_p(0, ioaddr+SM_ADDR(i<<1));
			}
		}
	}

	outw_p(lp->rx_buf_end,ioaddr+WRITE_PTR);
	start_code[28] = (dev->flags & IFF_PROMISC)?(start_code[28] | 1):(start_code[28] & ~1);
	lp->promisc = dev->flags & IFF_PROMISC;
	/* We may die here */
	outsw(ioaddr, start_code, sizeof(start_code)>>1);
	outw(CONF_HW_ADDR,ioaddr+WRITE_PTR);
	outsw(ioaddr,dev->dev_addr,3);
	eexp_hw_txinit(dev);
	eexp_hw_rxinit(dev);
	outw(0,ioaddr+WRITE_PTR);
	outw(1,ioaddr);
	outb(0,ioaddr+EEPROM_Ctrl);
	outw(0,ioaddr+SCB_CMD);
	outb(0,ioaddr+SIGNAL_CA);
	{
		unsigned short rboguscount=50,rfailcount=5;
		while (outw(0,ioaddr+READ_PTR),inw(ioaddr)) 
		{
			if (!--rboguscount) 
			{
				printk(KERN_WARNING "%s: i82586 reset timed out, kicking...\n",
					dev->name);
				outw(0,ioaddr+SCB_CMD);
				outb(0,ioaddr+SIGNAL_CA);
				rboguscount = 100;
				if (!--rfailcount) 
				{
					printk(KERN_WARNING "%s: i82586 not responding, giving up.\n",
						dev->name);
					return;
				}
			}
		}
	}

	outw(CONF_LINK,ioaddr+SCB_CBL);
	outw(0,ioaddr+SCB_STATUS);
	outw(0xf000|SCB_CUstart,ioaddr+SCB_CMD);
	outb(0,ioaddr+SIGNAL_CA);
	{
		unsigned short iboguscount=50,ifailcount=5;
		while (!inw(ioaddr+SCB_STATUS)) 
		{
			if (!--iboguscount) 
			{
				if (--ifailcount) 
				{
					printk(KERN_WARNING "%s: i82586 initialization timed out, status %04x, cmd %04x\n",
						dev->name, inw(ioaddr+SCB_STATUS), inw(ioaddr+SCB_CMD));
					outw(CONF_LINK,ioaddr+SCB_CBL);
					outw(0,ioaddr+SCB_STATUS);
					outw(0xf000|SCB_CUstart,ioaddr+SCB_CMD);
					outb(0,ioaddr+SIGNAL_CA);
					iboguscount = 100;
				}
				else 
				{
					printk(KERN_WARNING "%s: Failed to initialize i82586, giving up.\n",dev->name);
					return;
				}
			}
		}
	}
  
	outb(SIRQ_en|irqrmap[dev->irq],ioaddr+SET_IRQ);
	clear_loopback;
	lp->init_time = jiffies;
#if NET_DEBUG > 6
        printk("%s: leaving eexp_hw_init586()\n", dev->name);
#endif
	return;
}

/* 
 * completely reset the EtherExpress hardware. We will most likely get
 * an interrupt during this whether we want one or not. It is best,
 * therefore, to call this while we don't have a request_irq() on.
 */

static void eexp_hw_ASICrst(struct device *dev)
{
	unsigned short ioaddr = dev->base_addr;
	unsigned short wrval = 0x0001,succount=0,boguscount=500;

	outb(SIRQ_dis|irqrmap[dev->irq],ioaddr+SET_IRQ);

	PRIV(dev)->started = 0;
	outb(ASIC_RST|i586_RST,ioaddr+EEPROM_Ctrl);
	while (succount<20) 
	{
		if (wrval == 0xffff) 
			wrval = 0x0001;
		outw(0,ioaddr+WRITE_PTR);
		outw(wrval,ioaddr);
		outw(0,ioaddr+READ_PTR);
		if (wrval++ == inw(ioaddr)) 
			succount++;
		else 
		{
			succount = 0;
			if (!boguscount--) 
			{
				boguscount = 500;
				printk("%s: Having problems resetting EtherExpress ASIC, continuing...\n",
					dev->name);
				wrval = 0x0001;
				outb(ASIC_RST|i586_RST,ioaddr+EEPROM_Ctrl);
			}
		}
	}
	outb(i586_RST,ioaddr+EEPROM_Ctrl);
}


/*
 * Set or clear the multicast filter for this adaptor.
 * We have to do a complete 586 restart for this to take effect.
 * At the moment only promiscuous mode is supported.
 */
static void
eexp_set_multicast(struct device *dev)
{
	if ((dev->flags & IFF_PROMISC) != PRIV(dev)->promisc)
		eexp_hw_init586(dev);
}


/*
 * MODULE stuff
 */
#ifdef MODULE

#define EEXP_MAX_CARDS     4    /* max number of cards to support */
#define NAMELEN            8    /* max length of dev->name (inc null) */

static char namelist[NAMELEN * EEXP_MAX_CARDS] = { 0, };

static struct device dev_eexp[EEXP_MAX_CARDS] = 
{
        { NULL,         /* will allocate dynamically */
	  0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, express_probe },  
};

int irq[EEXP_MAX_CARDS] = {0, };
int io[EEXP_MAX_CARDS] = {0, };

/* Ideally the user would give us io=, irq= for every card.  If any parameters
 * are specified, we verify and then use them.  If no parameters are given, we
 * autoprobe for one card only.
 */
int init_module(void)
{
	int this_dev, found = 0;

	for (this_dev = 0; this_dev < EEXP_MAX_CARDS; this_dev++) {
		struct device *dev = &dev_eexp[this_dev];
		dev->name = namelist + (NAMELEN*this_dev);
		dev->irq = irq[this_dev];
		dev->base_addr = io[this_dev];
		if (io[this_dev] == 0) {
			if (this_dev) break;
			printk(KERN_NOTICE "eexpress.c: Module autoprobe not recommended, give io=xx.\n");
		}
		if (register_netdev(dev) != 0) {
			printk(KERN_WARNING "eexpress.c: Failed to register card at 0x%x.\n", io[this_dev]);
			if (found != 0) return 0;
			return -ENXIO;
		}
		found++;
	}
	return 0;
}

void cleanup_module(void)
{
	int this_dev;
        
	for (this_dev = 0; this_dev < EEXP_MAX_CARDS; this_dev++) {
		struct device *dev = &dev_eexp[this_dev];
		if (dev->priv != NULL) {
			kfree(dev->priv);
			dev->priv = NULL;
			release_region(dev->base_addr, EEXP_IO_EXTENT);
			unregister_netdev(dev);
		}
	}
}
#endif

/*
 * Local Variables:
 *  c-file-style: "linux"
 *  tab-width: 8
 *  compile-command: "gcc -D__KERNEL__ -I/discs/bibble/src/linux-1.3.69/include  -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce -pipe -m486 -DCPU=486 -DMODULE  -c 3c505.c"
 * End:
 */
