/* Intel EtherExpress 16 device driver for Linux
 *
 * Written by John Sullivan, 1995
 *  based on original code by Donald Becker, with changes by
 *  Alan Cox and Pauline Middelink.
 *
 * Many modifications, and currently maintained, by
 *  Philip Blundell <Philip.Blundell@pobox.com>
 */

/* The EtherExpress 16 is a fairly simple card, based on a shared-memory
 * design using the i82586 Ethernet coprocessor.  It bears no relationship,
 * as far as I know, to the similarly-named "EtherExpress Pro" range.
 *
 * Historically, Linux support for these cards has been very bad.  However,
 * things seem to be getting better slowly. 
 */

/* It would be nice to seperate out all the 82586-specific code, so that it 
 * could be shared between drivers (as with 8390.c).  But this would be quite
 * a messy job.  The main motivation for doing this would be to bring 3c507
 * support back up to scratch.
 */

/* If your card is confused about what sort of interface it has (eg it
 * persistently reports "10baseT" when none is fitted), running 'SOFTSET /BART'
 * or 'SOFTSET /LISA' from DOS seems to help.
 */

/* Here's the scoop on memory mapping.
 *
 * There are three ways to access EtherExpress card memory: either using the
 * shared-memory mapping, or using PIO through the dataport, or using PIO
 * through the "shadow memory" ports.
 *
 * The shadow memory system works by having the card map some of its memory
 * as follows:
 *
 * (the low five bits of the SMPTR are ignored)
 * 
 *  base+0x4000..400f      memory at SMPTR+0..15
 *  base+0x8000..800f      memory at SMPTR+16..31
 *  base+0xc000..c007      dubious stuff (memory at SMPTR+16..23 apparently)
 *  base+0xc008..c00f      memory at 0x0008..0x000f
 *
 * This last set (the one at c008) is particularly handy because the SCB 
 * lives at 0x0008.  So that set of ports gives us easy random access to data 
 * in the SCB without having to mess around setting up pointers and the like.
 * We always use this method to access the SCB (via the scb_xx() functions).
 *
 * Dataport access works by aiming the appropriate (read or write) pointer
 * at the first address you're interested in, and then reading or writing from
 * the dataport.  The pointers auto-increment after each transfer.  We use
 * this for data transfer.
 *
 * We don't use the shared-memory system because it allegedly doesn't work on
 * all cards, and because it's a bit more prone to go wrong (it's one more
 * thing to configure...).
 */

/* Known bugs:
 *
 * - 8-bit mode is not supported, and makes things go wrong.
 * - Multicast and promiscuous modes are not supported.
 * - The card seems to want to give us two interrupts every time something
 *   happens, where just one would be better. 
 * - The statistics may not be getting reported properly.
 */

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
#include <linux/delay.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/malloc.h>

#ifndef NET_DEBUG
#define NET_DEBUG 4 
#endif

#include "eexpress.h"

#define EEXP_IO_EXTENT  16

/*
 * Private data declarations
 */

struct net_local 
{
	struct enet_statistics stats;
	unsigned long init_time;     /* jiffies when eexp_hw_init586 called */
	unsigned short rx_first;     /* first rx buf, same as RX_BUF_START */
	unsigned short rx_last;      /* last rx buf */
	unsigned short tx_head;      /* next free tx buf */
	unsigned short tx_reap;      /* first in-use tx buf */
	unsigned short tx_tail;      /* previous tx buf to tx_head */
	unsigned short tx_link;      /* last known-executing tx buf */
	unsigned short last_tx_restart;   /* set to tx_link when we
					     restart the CU */
	unsigned char started;
	unsigned char promisc;
	unsigned short rx_buf_start;
	unsigned short rx_buf_end;
	unsigned short num_tx_bufs;
	unsigned short num_rx_bufs;
};

/* This is the code and data that is downloaded to the EtherExpress card's
 * memory at boot time.
 */

static unsigned short start_code[] = {
/* 0xfff6 */
	0x0000,                 /* set bus to 16 bits */
	0x0000,0x0000,         
	0x0000,0x0000,          /* address of ISCP (lo,hi) */

/* 0x0000 */
	0x0001,                 /* ISCP: busy - cleared after reset */
	0x0008,0x0000,0x0000,   /* offset,address (lo,hi) of SCB */

	0x0000,0x0000,          /* SCB: status, commands */
	0x0000,0x0000,          /* links to first command block, 
				   first receive descriptor */
	0x0000,0x0000,          /* CRC error, alignment error counts */
	0x0000,0x0000,          /* out of resources, overrun error counts */

	0x0000,0x0000,          /* pad */
	0x0000,0x0000,

/* 0x0020 -- start of 82586 CU program */
#define CONF_LINK 0x0020
	0x0000,Cmd_Config,      
	0x0032,                 /* link to next command */
	0x080c,                 /* 12 bytes follow : fifo threshold=8 */
	0x2e40,                 /* don't rx bad frames
				 * SRDY/ARDY => ext. sync. : preamble len=8
	                         * take addresses from data buffers
				 * 6 bytes/address
				 */
	0x6000,                 /* default backoff method & priority
				 * interframe spacing = 0x60 */
	0xf200,                 /* slot time=0x200 
				 * max collision retry = 0xf */
	0x0000,                 /* no HDLC : normal CRC : enable broadcast 
				 * disable promiscuous/multicast modes */
	0x003c,                 /* minimum frame length = 60 octets) */

	0x0000,Cmd_INT|Cmd_SetAddr,
	0x003e,                 /* link to next command */
	0x0000,0x0000,0x0000,   /* hardware address placed here */

	0x0000,Cmd_TDR,0x0048,
/* 0x0044 -- TDR result placed here */
	0x0000, 0x0000,

/* Eventually, a set-multicast will go in here */

	0x0000,Cmd_END|Cmd_Nop, /* end of configure sequence */
	0x0048,

	0x0000

};

/* maps irq number to EtherExpress magic value */
static char irqrmap[] = { 0,0,1,2,3,4,0,0,0,1,5,6,0,0,0,0 };

/*
 * Prototypes for Linux interface
 */

extern int express_probe(struct device *dev);
static int eexp_open(struct device *dev);
static int eexp_close(struct device *dev);
static struct enet_statistics *eexp_stats(struct device *dev);
static int eexp_xmit(struct sk_buff *buf, struct device *dev);

static void eexp_irq(int irq, void *dev_addr, struct pt_regs *regs);
static void eexp_set_multicast(struct device *dev);

/*
 * Prototypes for hardware access functions
 */

static void eexp_hw_rx_pio(struct device *dev);
static void eexp_hw_tx_pio(struct device *dev, unsigned short *buf,
		       unsigned short len);
static int eexp_hw_probe(struct device *dev,unsigned short ioaddr);
static unsigned short eexp_hw_readeeprom(unsigned short ioaddr,
					 unsigned char location);

static unsigned short eexp_hw_lasttxstat(struct device *dev);
static void eexp_hw_txrestart(struct device *dev);

static void eexp_hw_txinit    (struct device *dev);
static void eexp_hw_rxinit    (struct device *dev);

static void eexp_hw_init586   (struct device *dev);

/*
 * Primitive hardware access functions.
 */

static inline unsigned short scb_status(struct device *dev)
{
	return inw(dev->base_addr + 0xc008);
}

static inline unsigned short scb_rdcmd(struct device *dev)
{
	return inw(dev->base_addr + 0xc00a);
}

static inline void scb_command(struct device *dev, unsigned short cmd)
{
	outw(cmd, dev->base_addr + 0xc00a);
}

static inline void scb_wrcbl(struct device *dev, unsigned short val)
{
	outw(val, dev->base_addr + 0xc00c);
}

static inline void scb_wrrfa(struct device *dev, unsigned short val)
{
	outw(val, dev->base_addr + 0xc00e);
}

static inline void set_loopback(struct device *dev)
{
	outb(inb(dev->base_addr + Config) | 2, dev->base_addr + Config); 
}

static inline void clear_loopback(struct device *dev)
{
	outb(inb(dev->base_addr + Config) & ~2, dev->base_addr + Config); 
}

static inline short int SHADOW(short int addr)
{
	addr &= 0x1f;
	if (addr > 0xf) addr += 0x3ff0;
	return addr + 0x4000;
}

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

	for (port=&ports[0] ; *port ; port++ ) 
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
	   ((irq2dev_map[irq]=dev),0) ||
	     request_irq(irq,&eexp_irq,0,"EtherExpress",NULL)) 
		return -EAGAIN;

	request_region(ioaddr, EEXP_IO_EXTENT, "EtherExpress");
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
 * close and disable the interface, leaving the 586 in reset.
 */
static int eexp_close(struct device *dev)
{
	unsigned short ioaddr = dev->base_addr;
	struct net_local *lp = dev->priv;

	int irq = dev->irq;

	dev->tbusy = 1; 
	dev->start = 0;
  
	outb(SIRQ_dis|irqrmap[irq],ioaddr+SET_IRQ);
	lp->started = 0;
	scb_command(dev, SCB_CUsuspend|SCB_RUsuspend);
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

	return &lp->stats;
}

/* 
 * This gets called when a higher level thinks we are broken.  Check that
 * nothing has become jammed in the CU.
 */

static void unstick_cu(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;

	if (lp->started) 
	{
		if ((jiffies - dev->trans_start)>50) 
		{
			if (lp->tx_link==lp->last_tx_restart) 
			{
				unsigned short boguscount=200,rsst;
				printk(KERN_WARNING "%s: Retransmit timed out, status %04x, resetting...\n",
				       dev->name, scb_status(dev));
				eexp_hw_txinit(dev);
				lp->last_tx_restart = 0;
				scb_wrcbl(dev, lp->tx_link);
				scb_command(dev, SCB_CUstart);
				outb(0,ioaddr+SIGNAL_CA);
				while (!SCB_complete(rsst=scb_status(dev))) 
				{
					if (!--boguscount) 
					{
						boguscount=200;
						printk(KERN_WARNING "%s: Reset timed out status %04x, retrying...\n",
						       dev->name,rsst);
						scb_wrcbl(dev, lp->tx_link);
						scb_command(dev, SCB_CUstart);
						outb(0,ioaddr+SIGNAL_CA);
					}
				}
				dev->tbusy = 0;
				mark_bh(NET_BH);
			}
			else
			{
				unsigned short status = scb_status(dev);
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
					else
					{
						printk(KERN_WARNING "%s: transmit timed out\n", dev->name);
					}
				}
			}
		}
	}
	else
	{
		if ((jiffies-lp->init_time)>10)
		{
			unsigned short status = scb_status(dev);
			printk(KERN_WARNING "%s: i82586 startup timed out, status %04x, resetting...\n",
			       dev->name, status);
			eexp_hw_init586(dev);
			dev->tbusy = 0;
			mark_bh(NET_BH);
		}
	}
}

/*
 * Called to transmit a packet, or to allow us to right ourselves
 * if the kernel thinks we've died.
 */
static int eexp_xmit(struct sk_buff *buf, struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: eexp_xmit()\n", dev->name);
#endif

	outb(SIRQ_dis|irqrmap[dev->irq],dev->base_addr+SET_IRQ);
	
	/* If dev->tbusy is set, all our tx buffers are full but the kernel
	 * is calling us anyway.  Check that nothing bad is happening.
	 */
	if (dev->tbusy) 
		unstick_cu(dev);

	if (buf==NULL) 
	{
		/* Some higher layer thinks we might have missed a
		 * tx-done interrupt.  Does this ever actually happen?
		 */
		unsigned short status = scb_status(dev);
		unsigned short txstatus = eexp_hw_lasttxstat(dev);
		if (SCB_CUdead(status)) 
		{
			printk(KERN_WARNING "%s: CU has died! status %04x %04x, attempting to restart...\n",
				dev->name, status, txstatus);
			lp->stats.tx_errors++;
			eexp_hw_txrestart(dev);
		}
		dev_tint(dev);
		outb(SIRQ_en|irqrmap[dev->irq],dev->base_addr+SET_IRQ);
		return 0;
	}

	if (set_bit(0,(void *)&dev->tbusy)) 
	{
		lp->stats.tx_dropped++;
	}
	else
	{
		unsigned short length = (ETH_ZLEN < buf->len) ? buf->len :
			ETH_ZLEN;
		unsigned short *data = (unsigned short *)buf->data;

	        eexp_hw_tx_pio(dev,data,length);
	}
	dev_kfree_skb(buf, FREE_WRITE);
	outb(SIRQ_en|irqrmap[dev->irq],dev->base_addr+SET_IRQ);
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

	if (dev==NULL) 
	{
		printk(KERN_WARNING "eexpress: irq %d for unknown device\n",
		       irq);
		return;
	}

	lp = (struct net_local *)dev->priv;
	ioaddr = dev->base_addr;

	outb(SIRQ_dis|irqrmap[irq],ioaddr+SET_IRQ); 

	dev->interrupt = 1; 
  
	status = scb_status(dev);

#if NET_DEBUG > 4
	printk(KERN_DEBUG "%s: interrupt (status %x)\n", dev->name, status);
#endif

	ack_cmd = SCB_ack(status);

	if (lp->started==0 && SCB_complete(status)) 
	{
		while (SCB_CUstat(status)==2)
				status = scb_status(dev);
#if NET_DEBUG > 4
		printk(KERN_DEBUG "%s: CU went non-active (status = %08x)\n",
		       dev->name, status);
#endif

		/* now get the TDR status */
		{
			short tdr_status;
			outw(0x40, dev->base_addr + SM_PTR);
			tdr_status = inw(dev->base_addr + 0x8004);
			if (tdr_status & TDR_SHORT) {
					printk(KERN_WARNING "%s: TDR reports cable short at %d tick%s\n", dev->name, tdr_status & TDR_TIME, ((tdr_status & TDR_TIME) != 1) ? "s" : "");
			} 
			else if (tdr_status & TDR_OPEN) {
					printk(KERN_WARNING "%s: TDR reports cable broken at %d tick%s\n", dev->name, tdr_status & TDR_TIME, ((tdr_status & TDR_TIME) != 1) ? "s" : "");
			}
			else if (tdr_status & TDR_XCVRPROBLEM) {
					printk(KERN_WARNING "%s: TDR reports transceiver problem\n", dev->name);
			}
#if NET_DEBUG > 4
			else if (tdr_status & TDR_LINKOK) {
					printk(KERN_DEBUG "%s: TDR reports link OK\n", dev->name);
			}
#endif
		}

		lp->started=1;
		scb_wrcbl(dev, lp->tx_link);
		scb_wrrfa(dev, lp->rx_buf_start);
		ack_cmd |= SCB_CUstart | SCB_RUstart | 0x2000;
	}
	else if (lp->started) 
	{
		unsigned short txstatus;
		txstatus = eexp_hw_lasttxstat(dev);
	}

	if (SCB_rxdframe(status)) 
	{
		eexp_hw_rx_pio(dev);
	}

	if ((lp->started&2)!=0 && SCB_RUstat(status)!=4) 
	{
		printk(KERN_WARNING "%s: RU stopped: status %04x\n",
			dev->name,status);
		lp->stats.rx_errors++;
		eexp_hw_rxinit(dev);
		scb_wrrfa(dev, lp->rx_buf_start);
		ack_cmd |= SCB_RUstart;
	} 
	else if (lp->started==1 && SCB_RUstat(status)==4) 
		lp->started|=2;

	scb_command(dev, ack_cmd);
	outb(0,ioaddr+SIGNAL_CA);

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
 * descriptor, though we don't bother trying to fix broken ones.
 */
 
static void eexp_hw_rx_pio(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short rx_block = lp->rx_first;
	unsigned short boguscount = lp->num_rx_bufs;
	unsigned short ioaddr = dev->base_addr;

#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: eexp_hw_rx()\n", dev->name);
#endif

	while (boguscount--) 
	{
		unsigned short status, rfd_cmd, rx_next, pbuf, pkt_len;

		outw(rx_block, ioaddr + READ_PTR);
		status = inw(ioaddr + DATAPORT);
		rfd_cmd = inw(ioaddr + DATAPORT);
		rx_next = inw(ioaddr + DATAPORT);
		pbuf = inw(ioaddr + DATAPORT);

		if (FD_Done(status)) 
		{
			outw(pbuf, ioaddr + READ_PTR);
			pkt_len = inw(ioaddr + DATAPORT);

			if (rfd_cmd!=0x0000 || pbuf!=rx_block+0x16
				|| (pkt_len & 0xc000)!=0xc000) 
			{
				/* This should never happen.  If it does,
				 * we almost certainly have a driver bug.
				 */
				printk(KERN_WARNING "%s: Rx frame at %04x corrupted, status %04x, cmd %04x, "
					"next %04x, pbuf %04x, len %04x\n",dev->name,rx_block,
					status,rfd_cmd,rx_next,pbuf,pkt_len);
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
				outw(pbuf+10, ioaddr+READ_PTR);
			        insw(ioaddr+DATAPORT, skb_put(skb,pkt_len),(pkt_len+1)>>1);
				skb->protocol = eth_type_trans(skb,dev);
				netif_rx(skb);
				lp->stats.rx_packets++;
			}
			outw(rx_block, ioaddr+WRITE_PTR);
			outw(0, ioaddr+DATAPORT);
			outw(0, ioaddr+DATAPORT);
		}
		rx_block = rx_next;
	}
}

/*
 * Hand a packet to the card for transmission
 * If we get here, we MUST have already checked
 * to make sure there is room in the transmit
 * buffer region.
 */

static void eexp_hw_tx_pio(struct device *dev, unsigned short *buf,
		       unsigned short len)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;

	outw(lp->tx_head, ioaddr + WRITE_PTR);

	outw(0x0000, ioaddr + DATAPORT);
        outw(Cmd_INT|Cmd_Xmit, ioaddr + DATAPORT);
	outw(lp->tx_head+0x08, ioaddr + DATAPORT);
	outw(lp->tx_head+0x0e, ioaddr + DATAPORT);

	outw(0x0000, ioaddr + DATAPORT);
	outw(0x0000, ioaddr + DATAPORT);
	outw(lp->tx_head+0x08, ioaddr + DATAPORT);

	outw(0x8000|len, ioaddr + DATAPORT);
	outw(-1, ioaddr + DATAPORT);
	outw(lp->tx_head+0x16, ioaddr + DATAPORT);
	outw(0, ioaddr + DATAPORT);

        outsw(ioaddr + DATAPORT, buf, (len+1)>>1);

	outw(lp->tx_tail+0xc, ioaddr + WRITE_PTR);
	outw(lp->tx_head, ioaddr + DATAPORT);

	dev->trans_start = jiffies;
	lp->tx_tail = lp->tx_head;
	if (lp->tx_head==TX_BUF_START+((lp->num_tx_bufs-1)*TX_BUF_SIZE)) 
		lp->tx_head = TX_BUF_START;
	else 
		lp->tx_head += TX_BUF_SIZE;
	if (lp->tx_head != lp->tx_reap) 
		dev->tbusy = 0;
}

/*
 * Sanity check the suspected EtherExpress card
 * Read hardware address, reset card, size memory and initialize buffer
 * memory pointers. These are held in dev->priv, in case someone has more
 * than one card in a machine.
 */

static int eexp_hw_probe(struct device *dev, unsigned short ioaddr)
{
	unsigned short hw_addr[3];
	unsigned int memory_size;
	static char *ifmap[]={"AUI", "BNC", "RJ45"};
	enum iftype {AUI=0, BNC=1, TP=2};
	int i;
	unsigned short xsum = 0;
	struct net_local *lp;

	printk("%s: EtherExpress 16 at %#x",dev->name,ioaddr);

	outb(ASIC_RST, ioaddr+EEPROM_Ctrl);
	outb(0, ioaddr+EEPROM_Ctrl);
	udelay(500);
	outb(i586_RST, ioaddr+EEPROM_Ctrl);

	hw_addr[0] = eexp_hw_readeeprom(ioaddr,2);
	hw_addr[1] = eexp_hw_readeeprom(ioaddr,3);
	hw_addr[2] = eexp_hw_readeeprom(ioaddr,4);

	if (hw_addr[2]!=0x00aa || ((hw_addr[1] & 0xff00)!=0x0000)) 
	{
		printk(" rejected: invalid address %04x%04x%04x\n",
			hw_addr[2],hw_addr[1],hw_addr[0]);
		return ENODEV;
	}

	/* Calculate the EEPROM checksum.  Carry on anyway if it's bad,
	 * though. 
	 */
	for (i = 0; i < 64; i++)
		xsum += eexp_hw_readeeprom(ioaddr, i);
	if (xsum != 0xbaba) 
		printk(" (bad EEPROM xsum 0x%02x)", xsum);

	dev->base_addr = ioaddr;
	for ( i=0 ; i<6 ; i++ ) 
		dev->dev_addr[i] = ((unsigned char *)hw_addr)[5-i];

	{
		char irqmap[]={0, 9, 3, 4, 5, 10, 11, 0};
		unsigned short setupval = eexp_hw_readeeprom(ioaddr,0);

		/* Use the IRQ from EEPROM if none was given */
		if (!dev->irq)
			dev->irq = irqmap[setupval>>13];

		dev->if_port = !(setupval & 0x1000) ? AUI :
			eexp_hw_readeeprom(ioaddr,5) & 0x1 ? TP : BNC;
	}

	dev->priv = lp = kmalloc(sizeof(struct net_local), GFP_KERNEL);
	if (!dev->priv) 
		return ENOMEM;

	memset(dev->priv, 0, sizeof(struct net_local));
	
	printk("; using IRQ %d, %s connector", dev->irq,ifmap[dev->if_port]);

	/* Find out how much RAM we have on the card */
	outw(0, dev->base_addr + WRITE_PTR);
	for (i = 0; i < 32768; i++)
		outw(0, dev->base_addr + DATAPORT);

        for (memory_size = 0; memory_size < 64; memory_size++)
	{
		outw(memory_size<<10, dev->base_addr + WRITE_PTR);
		outw(memory_size<<10, dev->base_addr + READ_PTR);
		if (inw(dev->base_addr+DATAPORT)) 
			break;
		outw(memory_size | 0x5000, dev->base_addr+DATAPORT);
		outw(memory_size<<10, dev->base_addr + READ_PTR);
		if (inw(dev->base_addr+DATAPORT) != (memory_size | 0x5000))
			break;
	}

	/* Sort out the number of buffers.  We may have 16, 32, 48 or 64k
	 * of RAM to play with.
	 */
	lp->num_tx_bufs = 4;
	lp->rx_buf_end = 0x3ff6;
	switch (memory_size)
	{
	case 64:
		lp->rx_buf_end += 0x4000;
	case 48:
		lp->num_tx_bufs += 4;
		lp->rx_buf_end += 0x4000;
	case 32:
		lp->rx_buf_end += 0x4000;
	case 16:
		printk(", %dk RAM.\n", memory_size);
		break;
	default:
		printk("; bad memory size (%dk).\n", memory_size);
		kfree(dev->priv);
		return ENODEV;
		break;
	}

	lp->rx_buf_start = TX_BUF_START + (lp->num_tx_bufs*TX_BUF_SIZE);

	dev->open = eexp_open;
	dev->stop = eexp_close;
	dev->hard_start_xmit = eexp_xmit;
	dev->get_stats = eexp_stats;
	dev->set_multicast_list = &eexp_set_multicast;
	ether_setup(dev);
	return 0;
}

/*
 * Read a word from the EtherExpress on-board serial EEPROM.
 * The EEPROM contains 64 words of 16 bits.
 */
static unsigned short eexp_hw_readeeprom(unsigned short ioaddr, 
					 unsigned char location)
{
	unsigned short cmd = 0x180|(location&0x7f);
	unsigned short rval = 0,wval = EC_CS|i586_RST;
	int i;
 
	outb(EC_CS|i586_RST,ioaddr+EEPROM_Ctrl);
	for (i=0x100 ; i ; i>>=1 ) 
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
	for (i=0x8000 ; i ; i>>=1 ) 
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
	unsigned short tx_block = lp->tx_reap;
	unsigned short status;
  
	if ((!dev->tbusy) && lp->tx_head==lp->tx_reap) 
		return 0x0000;

	do
	{
		outw(tx_block, dev->base_addr + SM_PTR);
		status = inw(SHADOW(tx_block));
		if (!Stat_Done(status)) 
		{
			lp->tx_link = tx_block;
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

	return status;
}

/* 
 * This should never happen. It is called when some higher routine detects
 * that the CU has stopped, to try to restart it from the last packet we knew
 * we were working on, or the idle loop if we had finished for the time.
 */

static void eexp_hw_txrestart(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;
  
	lp->last_tx_restart = lp->tx_link;
	scb_wrcbl(dev, lp->tx_link);
	scb_command(dev, SCB_CUstart);
	outb(0,ioaddr+SIGNAL_CA);

	{
		unsigned short boguscount=50,failcount=5;
		while (!scb_status(dev)) 
		{
			if (!--boguscount) 
			{
				if (--failcount) 
				{
					printk(KERN_WARNING "%s: CU start timed out, status %04x, cmd %04x\n", dev->name, scb_status(dev), scb_rdcmd(dev));
				        scb_wrcbl(dev, lp->tx_link);
					scb_command(dev, SCB_CUstart);
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
 * Writes down the list of transmit buffers into card memory.  Each
 * entry consists of an 82586 transmit command, followed by a jump
 * pointing to itself.  When we want to transmit a packet, we write 
 * the data into the appropriate transmit buffer and then modify the
 * preceding jump to point at the new transmit command.  This means that
 * the 586 command unit is continuously active. 
 */

static void eexp_hw_txinit(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short tx_block = TX_BUF_START;
	unsigned short curtbuf;
	unsigned short ioaddr = dev->base_addr;

	for ( curtbuf=0 ; curtbuf<lp->num_tx_bufs ; curtbuf++ ) 
	{
		outw(tx_block, ioaddr + WRITE_PTR);

	        outw(0x0000, ioaddr + DATAPORT);
		outw(Cmd_INT|Cmd_Xmit, ioaddr + DATAPORT);
		outw(tx_block+0x08, ioaddr + DATAPORT);
		outw(tx_block+0x0e, ioaddr + DATAPORT);

		outw(0x0000, ioaddr + DATAPORT);
		outw(0x0000, ioaddr + DATAPORT);
		outw(tx_block+0x08, ioaddr + DATAPORT);

		outw(0x8000, ioaddr + DATAPORT);
		outw(-1, ioaddr + DATAPORT);
		outw(tx_block+0x16, ioaddr + DATAPORT);
		outw(0x0000, ioaddr + DATAPORT);

		tx_block += TX_BUF_SIZE;
	}
	lp->tx_head = TX_BUF_START;
	lp->tx_reap = TX_BUF_START;
	lp->tx_tail = tx_block - TX_BUF_SIZE;
	lp->tx_link = lp->tx_tail + 0x08;
	lp->rx_buf_start = tx_block;

}

/*
 * Write the circular list of receive buffer descriptors to card memory.
 * The end of the list isn't marked, which means that the 82586 receive
 * unit will loop until buffers become available (this avoids it giving us
 * "out of resources" messages). 
 */

static void eexp_hw_rxinit(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short rx_block = lp->rx_buf_start;
	unsigned short ioaddr = dev->base_addr; 

	lp->num_rx_bufs = 0;
	lp->rx_first = rx_block;
	do 
	{
		lp->num_rx_bufs++;

		outw(rx_block, ioaddr + WRITE_PTR);
		
		outw(0, ioaddr + DATAPORT);  outw(0, ioaddr+DATAPORT);
		outw(rx_block + RX_BUF_SIZE, ioaddr+DATAPORT);
		outw(rx_block + 0x16, ioaddr+DATAPORT);

		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		
		outw(0x8000, ioaddr+DATAPORT); 
		outw(0xffff, ioaddr+DATAPORT);
		outw(rx_block + 0x20, ioaddr+DATAPORT);
		outw(0, ioaddr+DATAPORT);
		outw(0x8000 | (RX_BUF_SIZE-0x20), ioaddr+DATAPORT);

		lp->rx_last = rx_block;
		rx_block += RX_BUF_SIZE;
	} while (rx_block <= lp->rx_buf_end-RX_BUF_SIZE);

	outw(lp->rx_last + 4, ioaddr+WRITE_PTR);
	outw(lp->rx_first, ioaddr+DATAPORT);

}

/*
 * Un-reset the 586, and start the configuration sequence. We don't wait for
 * this to finish, but allow the interrupt handler to start the CU and RU for
 * us.  We can't start the receive/transmission system up before we know that
 * the hardware is configured correctly.
 */
static void eexp_hw_init586(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned short ioaddr = dev->base_addr;
	int i;

#if NET_DEBUG > 6
	printk("%s: eexp_hw_init586()\n", dev->name);
#endif

	lp->started = 0;

	set_loopback(dev);  

	/* Bash the startup code a bit */
	start_code[28] = (dev->flags & IFF_PROMISC)?(start_code[28] | 1):
		(start_code[28] & ~1);
	lp->promisc = dev->flags & IFF_PROMISC;
	memcpy(&start_code[33], &dev->dev_addr[0], 6);

	outb(SIRQ_dis|irqrmap[dev->irq],ioaddr+SET_IRQ); 

	/* Download the startup code */
	outw(lp->rx_buf_end & ~31, ioaddr + SM_PTR);
	outw(start_code[0], ioaddr + 0x8006);
	outw(start_code[1], ioaddr + 0x8008);
	outw(start_code[2], ioaddr + 0x800a);
	outw(start_code[3], ioaddr + 0x800c);
	outw(start_code[4], ioaddr + 0x800e);
	for (i = 10; i < (sizeof(start_code)); i+=32) {
		int j;
		outw(i-10, ioaddr + SM_PTR);
		for (j = 0; j < 16; j+=2)
			outw(start_code[(i+j)/2],
			     ioaddr+0x4000+j);
		for (j = 0; j < 16; j+=2)
			outw(start_code[(i+j+16)/2],
			     ioaddr+0x8000+j);
	}

	eexp_hw_txinit(dev);
	eexp_hw_rxinit(dev);

	outb(0,ioaddr+EEPROM_Ctrl);
	udelay(5000);

	scb_command(dev, 0xf000);
	outb(0,ioaddr+SIGNAL_CA);

	outw(0, ioaddr+SM_PTR);

	{
		unsigned short rboguscount=50,rfailcount=5;
		while (inw(ioaddr+0x4000)) 
		{
			if (!--rboguscount) 
			{
				printk(KERN_WARNING "%s: i82586 reset timed out, kicking...\n",
					dev->name);
				scb_command(dev, 0);
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

        scb_wrcbl(dev, CONF_LINK);
	scb_command(dev, 0xf000|SCB_CUstart);
	outb(0,ioaddr+SIGNAL_CA);

	{
		unsigned short iboguscount=50,ifailcount=5;
		while (!scb_status(dev)) 
		{
			if (!--iboguscount) 
			{
				if (--ifailcount) 
				{
					printk(KERN_WARNING "%s: i82586 initialization timed out, status %04x, cmd %04x\n",
						dev->name, scb_status(dev), scb_rdcmd(dev));
					scb_wrcbl(dev, CONF_LINK);
				        scb_command(dev, 0xf000|SCB_CUstart);
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
  
	clear_loopback(dev); 
	outb(SIRQ_en|irqrmap[dev->irq],ioaddr+SET_IRQ); 

	lp->init_time = jiffies;
#if NET_DEBUG > 6
        printk("%s: leaving eexp_hw_init586()\n", dev->name);
#endif
	return;
}


/*
 * Set or clear the multicast filter for this adaptor.
 */
static void
eexp_set_multicast(struct device *dev)
{
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

static int irq[EEXP_MAX_CARDS] = {0, };
static int io[EEXP_MAX_CARDS] = {0, };

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
 * End:
 */
