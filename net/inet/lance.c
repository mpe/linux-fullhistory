/* lance.c: An AMD LANCE ethernet driver for linux. */
/*
    Written 1993 by Donald Becker.

    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.  This software may be used and
    distributed according to the terms of the GNU Public License,
    incorporated herein by reference.

    This driver should work with the Allied Telesis 1500, and NE2100 clones.

    The author may be reached as becker@super.org or
    C/O Supercomputing Research Ctr., 17100 Science Dr., Bowie MD 20715
*/

static char *version = "lance.c:v0.08 8/12/93 becker@super.org\n";

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
/*#include <linux/interrupt.h>*/
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <asm/dma.h>
/*#include <asm/system.h>*/

#include "dev.h"
#include "iow.h"
#include "eth.h"
#include "skbuff.h"
#include "arp.h"

/* From auto_irq.c, should be in a *.h file. */
extern void autoirq_setup(int waittime);
extern int autoirq_report(int waittime);
extern struct device *irq2dev_map[16];

extern void printk_stats(struct enet_statistics *stats);

#ifdef LANCE_DEBUG
int lance_debug = LANCE_DEBUG;
#else
int lance_debug = 1;
#endif

#ifndef DEFAULT_DMA
#define DEFAULT_DMA	5
#endif

/* Bitfield in the high bits of init block ring buffer. */
#define RING_LEN_BITS	0x80000000
#define RING_MOD_MASK	0x0f
#define RING_SIZE	16


/* Offsets from base I/O address. */
#define LANCE_DATA 0x10
#define LANCE_ADDR 0x12
#define LANCE_RESET 0x14
#define LANCE_BUS_IF 0x16

/* The LANCE Rx and Tx ring descriptors. */
struct lance_rx_head {
    int	base;
    short buf_length;		/* This length is 2's complement (negative)! */
    short msg_length;		/* This length is "normal". */
};

struct lance_tx_head {
    int	  base;
    short length;		/* Length is 2's complement (negative)! */
    short misc;
};

/* The LANCE initialization block, described in databook. */
struct lance_init {
    unsigned short mode;	/* Pre-set mode (reg. 15) */
    unsigned char phys_addr[6];	/* Physical ethernet address */
    unsigned filter[2];		/* Multicast filter (unused). */
    /* Receive and transmit ring base, along with extra bits. */
    unsigned rx_ring;		/* Tx and Rx ring base pointers */
    unsigned tx_ring;
};

struct lance_private {
    /* These must aligned on 8-byte boundaries. */
    struct lance_rx_head rx_ring[RING_SIZE];
    struct lance_tx_head tx_ring[RING_SIZE];
    struct lance_init	init_block;
    int	cur_rx, cur_tx;		/* The next free ring entry */
    int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
    int dma;
    struct enet_statistics stats;
    int pad0, pad1;		/* Used for alignment */
};

/* This is a temporary solution to the lack of a ethercard low-memory
   allocation scheme.  We need it for bus-master or DMA ethercards if
   they are to work >16M memory systems.  */
#define PKT_BUF_SZ	1550
static char rx_buffs[PKT_BUF_SZ][RING_SIZE];

int at1500_init(int ioaddr, struct device *dev);
static int lance_open(struct device *dev);
static int lance_start_xmit(struct sk_buff *skb, struct device *dev);
static int lance_rx(struct device *dev);
static void lance_interrupt(int reg_ptr);
static int lance_close(struct device *dev);
static struct enet_statistics *lance_get_stats(struct device *dev);

#ifdef notdef
static struct sigaction lance_sigaction = { &lance_interrupt, 0, 0, NULL, };
#endif



int at1500_probe(struct device *dev)
{
    int *port, ports[] = {0x300, 0x320, 0x340, 0x360, 0};
    int ioaddr = dev->base_addr;

    if (ioaddr > 0x100)
	return ! at1500_init(ioaddr, dev);

    for (port = &ports[0]; *port; port++) {
	/* Probe for the Allied-Telesys vendor ID.  This will not detect
	   other NE2100-like ethercards, which must use a hard-wired ioaddr.
	   There must be a better way to detect a LANCE... */
	int ioaddr = *port;
	if (inb(ioaddr) != 0x00
	    || inb(ioaddr+1) != 0x00
	    || inb(ioaddr+2) != 0xF4)
	    continue;
	if (at1500_init(ioaddr, dev))
	    return 0;
    }
    return 1;			/* ENODEV would be more accurate. */
}

int
at1500_init(int ioaddr, struct device *dev)
{
    struct lance_private *lp;
    int i;

    dev->base_addr = ioaddr;
    printk("%s: LANCE at %#3x, address", dev->name, ioaddr);

    /* There is a 16 byte station address PROM at the base address.
       The first six bytes are the station address. */
    for (i = 0; i < 6; i++)
	printk(" %2.2x", dev->dev_addr[i] = inb(ioaddr + i));

    /* Reset the LANCE */
    inw(ioaddr+LANCE_RESET);

    /* Make up a LANCE-specific-data structure. */
    dev->priv = kmalloc(sizeof(struct lance_private), GFP_KERNEL);
    /* Align on 8-byte boundary. */
    dev->priv = (void *)(((int)dev->priv + 7) & ~0x07);

    if ((int)dev->priv & 0xff000000  ||  (int) rx_buffs & 0xff000000) {
	printk(" disabled (buff %#x > 16M).\n", (int)rx_buffs);
	return 0;
    }

    memset(dev->priv, 0, sizeof(struct lance_private));
    lp = (struct lance_private *)dev->priv;

    /* Un-Reset the LANCE, needed only for the NE2100. */
    outw(0, ioaddr+LANCE_RESET);

    lp->init_block.mode = 0x0003;	/* Disable Rx and Tx. */
    for (i = 0; i < 6; i++)
	lp->init_block.phys_addr[i] = dev->dev_addr[i];
    lp->init_block.filter[0] = 0x00000000;
    lp->init_block.filter[1] = 0x00000000;
    lp->init_block.rx_ring = (int)lp->rx_ring | RING_LEN_BITS;
    lp->init_block.tx_ring = (int)lp->tx_ring | RING_LEN_BITS;

    outw(0x0001, ioaddr+LANCE_ADDR);
    outw((short) &lp->init_block, ioaddr+LANCE_DATA);
    outw(0x0002, ioaddr+LANCE_ADDR);
    outw(((int)&lp->init_block) >> 16, ioaddr+LANCE_DATA);
    outw(0x0000, ioaddr+LANCE_ADDR);

    /* To auto-IRQ we enable the initialization-done and DMA err,
       interrupts. For now we will always get a DMA error. */
    if (dev->irq < 2) {
	autoirq_setup(0);

	/* Trigger an initialization just for the interrupt. */
	outw(0x0041, ioaddr+LANCE_DATA);

	dev->irq = autoirq_report(1);
	if (dev->irq)
	    printk(", using IRQ %d.\n", dev->irq);
	else {
	    printk(", failed to detect IRQ line.\n");
	    return 0;
	}
    } else
	printk(" assigned IRQ %d.\n", dev->irq);

#ifndef NE2100			/* The NE2100 might not understand */
    /* Turn on auto-select of media (10baseT or BNC) so that the user
       can watch the LEDs even if the board isn't opened. */
    outw(0x0002, ioaddr+LANCE_ADDR);
    outw(0x0002, ioaddr+LANCE_BUS_IF);
#endif

    if ((int)(lp->rx_ring) & 0x07)
	printk("%s: LANCE Rx and Tx rings not on even boundary.\n",
	       dev->name);

    if (lance_debug > 0)
	printk(version);

    /* The LANCE-specific entries in the device structure. */
    dev->open = &lance_open;
    dev->hard_start_xmit = &lance_start_xmit;
    dev->stop = &lance_close;
    dev->get_stats = &lance_get_stats;

    dev->mem_start = 0;
    dev->rmem_end = 0x00ffffff;		/* Bogus, needed for dev_rint(). */

    /* Fill in the generic field of the device structure. */
    for (i = 0; i < DEV_NUMBUFFS; i++)
	dev->buffs[i] = NULL;

    dev->hard_header	= eth_header;
    dev->add_arp	= eth_add_arp;
    dev->queue_xmit	= dev_queue_xmit;
    dev->rebuild_header	= eth_rebuild_header;
    dev->type_trans	= eth_type_trans;

    dev->type		= ARPHRD_ETHER;
    dev->hard_header_len = ETH_HLEN;
    dev->mtu		= 1500; /* eth_mtu */
    dev->addr_len	= ETH_ALEN;
    for (i = 0; i < dev->addr_len; i++) {
	dev->broadcast[i]=0xff;
    }

    /* New-style flags. */
    dev->flags		= IFF_BROADCAST;
    dev->family		= AF_INET;
    dev->pa_addr	= 0;
    dev->pa_brdaddr	= 0;
    dev->pa_mask	= 0;
    dev->pa_alen	= sizeof(unsigned long);

    return ioaddr;
}


static int
lance_open(struct device *dev)
{
    struct lance_private *lp = (struct lance_private *)dev->priv;
    int ioaddr = dev->base_addr;
    int i;

    if (request_irq(dev->irq, &lance_interrupt)) {
	return -EAGAIN;
    }

    if (lp->dma < 1)
	lp->dma = DEFAULT_DMA;

    if (request_dma(lp->dma)) {
	free_irq(dev->irq);
	return -EAGAIN;
    }
    irq2dev_map[dev->irq] = dev;

    /* Reset the LANCE */
    inw(ioaddr+LANCE_RESET);

    /* The DMA controller is used as a no-operation slave, "cascade mode". */
    enable_dma(lp->dma);
    set_dma_mode(lp->dma, DMA_MODE_CASCADE);

    /* Un-Reset the LANCE, needed only for the NE2100. */
    outw(0, ioaddr+LANCE_RESET);

#ifndef NE2100			/* The NE2100 might not understand */
    /* Turn on auto-select of media (10baseT or BNC). */
    outw(0x0002, ioaddr+LANCE_ADDR);
    outw(0x0002, ioaddr+LANCE_BUS_IF);
#endif

    if (lance_debug > 1)
	printk("%s: lance_open() irq %d dma %d tx/rx rings %#x/%#x init %#x.\n",
	       dev->name, dev->irq, lp->dma, lp->tx_ring, lp->rx_ring,
	       &lp->init_block);

    lp->cur_rx = lp->cur_tx = 0;
    lp->dirty_rx = lp->dirty_tx = 0;

    for (i = 0; i < RING_SIZE; i++) {
	lp->rx_ring[i].base = (int)rx_buffs | 0x80000000 + i*PKT_BUF_SZ;
	lp->rx_ring[i].buf_length = -PKT_BUF_SZ;
	lp->tx_ring[i].base = 0;
    }

    lp->init_block.mode = 0x0000;
    for (i = 0; i < 6; i++)
	lp->init_block.phys_addr[i] = dev->dev_addr[i];
    lp->init_block.filter[0] = 0x00000000;
    lp->init_block.filter[1] = 0x00000000;
    lp->init_block.rx_ring = (int)lp->rx_ring | RING_LEN_BITS;
    lp->init_block.tx_ring = (int)lp->tx_ring | RING_LEN_BITS;

    /* Re-initialize the LANCE, and start it when done. */
    outw(0x0001, ioaddr+LANCE_ADDR);
    outw((short) &lp->init_block, ioaddr+LANCE_DATA);
    outw(0x0002, ioaddr+LANCE_ADDR);
    outw(((int)&lp->init_block) >> 16, ioaddr+LANCE_DATA);

    outw(0x0004, ioaddr+LANCE_ADDR);
    outw(0x0d15, ioaddr+LANCE_DATA);

    outw(0x0000, ioaddr+LANCE_ADDR);
    outw(0x0001, ioaddr+LANCE_DATA);

    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;
    i = 0;
    while (i++ < 100)
	if (inw(ioaddr+LANCE_DATA) & 0x0100)
	    break;
    outw(0x0142, ioaddr+LANCE_DATA);

    if (lance_debug > 2)
	printk("%s: LANCE open after %d ticks, init block %#x csr0 %4.4x.\n",
	       dev->name, i, &lp->init_block, inw(ioaddr+LANCE_DATA));

    return 0;			/* Always succeed */
}

static int
lance_start_xmit(struct sk_buff *skb, struct device *dev)
{
    struct lance_private *lp = (struct lance_private *)dev->priv;
    int ioaddr = dev->base_addr;

    /* Transmitter timeout, serious problems. */
    if (dev->tbusy) {
	int tickssofar = jiffies - dev->trans_start;
	int entry = lp->cur_tx++;
	if (tickssofar < 5)
	    return 1;
	outw(0, ioaddr+LANCE_ADDR);
	printk("%s: transmit timed out, status %4.4x.\n", dev->name,
	       inw(ioaddr+LANCE_DATA));

	if (lp->tx_ring[(entry+1) & RING_MOD_MASK].base >= 0)
	    dev->tbusy=0;
	else
	    outw(0x00, ioaddr+LANCE_DATA),
	    outw(0x43, ioaddr+LANCE_DATA);;
	dev->trans_start = jiffies;

	return 0;
    }

    if (skb == NULL) {
	dev_tint(dev);
	return 0;
    }

    /* Fill in the ethernet header. */
    if (!skb->arp  &&  dev->rebuild_header(skb+1, dev)) {
	skb->dev = dev;
	arp_queue (skb);
	return 0;
    }

    if (skb->len <= 0)
	return 0;

    if (lance_debug > 3) {
	outw(0x0000, ioaddr+LANCE_ADDR);
	printk("%s: lance_start_xmit() called, csr0 %4.4x.\n", dev->name,
	       inw(ioaddr+LANCE_DATA));
	outw(0x0000, ioaddr+LANCE_DATA);
    }

    /* Avoid timer-based retransmission conflicts. */
    dev->tbusy=1;

    /* This code is broken for >16M RAM systems.
       There are two ways to fix it:
           Keep around several static low-memory buffers, and deal with
	   the book-keeping (bad for small systems).
	   Make static Tx buffers that are optionally used.
	   (Even worse.)
     */
    {				/* Fill in a Tx ring entry */
	int entry = lp->cur_tx++;

	entry &= RING_MOD_MASK;		/* Ring buffer. */
	/* Caution: the write order is important here. */
	lp->tx_ring[entry].length = -skb->len;

	/* This shouldn't be necessary... */
	lp->tx_ring[entry].length =
	    -(ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN);

	lp->tx_ring[entry].misc = 0x0000;
	lp->tx_ring[entry].base = (int)(skb+1) | 0x83000000;

	/* Trigger an immediate send poll. */
	outw(0x0000, ioaddr+LANCE_ADDR);
	outw(0x0048, ioaddr+LANCE_DATA);

	if (lance_debug > 4) {
	    unsigned char *pkt =
		(unsigned char *)(lp->tx_ring[entry].base & 0x00ffffff);

	    printk("%s: tx ring[%d], %#x, sk_buf %#x len %d.\n",
		   dev->name, entry, &lp->tx_ring[entry],
		   lp->tx_ring[entry].base, -lp->tx_ring[entry].length);
	    printk("%s:  Tx %2.2x %2.2x %2.2x ... %2.2x  %2.2x %2.2x %2.2x...%2.2x len %2.2x %2.2x  %2.2x %2.2x.\n",
		   dev->name, pkt[0], pkt[1], pkt[2], pkt[5], pkt[6],
		   pkt[7], pkt[8], pkt[11], pkt[12], pkt[13],
		   pkt[14], pkt[15]);
	}

	dev->trans_start = jiffies;

	if (lp->tx_ring[(entry+1) & RING_MOD_MASK].base >= 0)
	    dev->tbusy=0;
    }

    return 0;
}

/* The LANCE interrupt handler. */
static void
lance_interrupt(int reg_ptr)
{
    int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
    struct device *dev = irq2dev_map[irq];
    struct lance_private *lp;
    int csr0, ioaddr;

    if (dev == NULL) {
	printk ("lance_interrupt(): irq %d for unknown device.\n", irq);
	return;
    }

    ioaddr = dev->base_addr;
    lp = (struct lance_private *)dev->priv;
    if (dev->interrupt)
	printk("%s: Re-entering the interrupt handler.\n", dev->name);

    dev->interrupt = 1;

    outw(0x00, dev->base_addr + LANCE_ADDR);
    csr0 = inw(dev->base_addr + LANCE_DATA);

    /* Acknowledge all of the current interrupt sources ASAP. */
    outw(csr0 & ~0x004f, dev->base_addr + LANCE_DATA);

    if (lance_debug > 5)
	printk("%s: interrupt  csr0=%#2.2x new csr=%#2.2x.\n",
	       dev->name, csr0, inw(dev->base_addr + LANCE_DATA));

    if (csr0 & 0x0400)		/* Rx interrupt */
	lance_rx(dev);

    if (csr0 & 0x0200) {	/* Tx-done interrupt */
	int dirty_tx = lp->dirty_tx & RING_MOD_MASK;

	if (lance_debug > 5)
	    printk("%s: Cleaning tx ring, dirty %d clean %d.\n",
		   dev->name, dirty_tx, (lp->cur_tx & RING_MOD_MASK));

	/* This code is broken for >16M RAM systems. */
	while (dirty_tx != (lp->cur_tx & RING_MOD_MASK)
	       && lp->tx_ring[dirty_tx].base > 0) {
	    sk_buff *skb =
		(sk_buff *)(lp->tx_ring[dirty_tx].base & 0x00ffffff);
	    unsigned short *tmdp = (unsigned short *)(&lp->tx_ring[dirty_tx]);
	    int status = lp->tx_ring[dirty_tx].base >> 24;

	    if (status & 0x40) { /* There was an major error, log it. */
		int err_status = lp->tx_ring[dirty_tx].misc;
		lp->stats.tx_errors++;
		if (err_status & 0x0400) lp->stats.tx_aborted_errors++;
		if (err_status & 0x0800) lp->stats.tx_carrier_errors++;
		if (err_status & 0x1000) lp->stats.tx_window_errors++;
		if (err_status & 0x4000) lp->stats.tx_fifo_errors++;
		/* We should re-init() after the FIFO error. */
	    } else if (status & 0x18)
		lp->stats.collisions++;
	    else
		lp->stats.tx_packets++;
	    if (lance_debug > 5)
		printk("%s: Tx done entry %d, %4.4x %4.4x %4.4x %4.4x.\n",
		       dev->name, dirty_tx,
		       tmdp[0], tmdp[1], tmdp[2], tmdp[3]);
	    if ((skb-1)->free)
		kfree_skb (skb-1, FREE_WRITE);
	    dirty_tx = ++lp->dirty_tx & RING_MOD_MASK;
	}
    }

    /* Clear the interrupts we've handled. */
    outw(0x0000, dev->base_addr + LANCE_ADDR);
    outw(0x7f40, dev->base_addr + LANCE_DATA);

    if (lance_debug > 4)
	printk("%s: exiting interrupt, csr%d=%#4.4x.\n",
	       dev->name, inw(ioaddr + LANCE_ADDR),
	       inw(dev->base_addr + LANCE_DATA));

    dev->interrupt = 0;
    return;
}

static int
lance_rx(struct device *dev)
{
    struct lance_private *lp = (struct lance_private *)dev->priv;
    int entry = lp->cur_rx & RING_MOD_MASK;
	
    /* Check to see if we own this entry. */
    while (lp->rx_ring[entry].base >= 0) {
	int status = lp->rx_ring[entry].base >> 24;
	if (lance_debug > 5) {
	    unsigned char *pkt =
		(unsigned char *)(lp->rx_ring[entry].base & 0x00ffffff);
	    printk("%s: Rx packet at ring entry %d, len %d status %2.2x.\n",
		   dev->name, entry, lp->rx_ring[entry].msg_length,
		   lp->rx_ring[entry].base >> 24);
	    printk("%s:  Rx %2.2x %2.2x %2.2x ... %2.2x  %2.2x %2.2x %2.2x...%2.2x len %2.2x %2.2x  %2.2x %2.2x.\n",
		   dev->name, pkt[0], pkt[1], pkt[2], pkt[5], pkt[6],
		   pkt[7], pkt[8], pkt[11], pkt[12], pkt[13],
		   pkt[14], pkt[15]);
	}
	/* If so, copy it to the upper layers. */
	if (status & 0x40) {	/* There was an error. */
	    lp->stats.rx_errors++;
	    if (status & 0x20) lp->stats.rx_frame_errors++;
	    if (status & 0x10) lp->stats.rx_over_errors++;
	    if (status & 0x08) lp->stats.rx_crc_errors++;
	    if (status & 0x04) lp->stats.rx_fifo_errors++;
	} else {
	    if (dev_rint((unsigned char *)(lp->rx_ring[entry].base
					   & 0x00ffffff),
			 lp->rx_ring[entry].msg_length, 0, dev)) {
		lp->stats.rx_dropped++;
		break;
	    }
	    lp->stats.rx_packets++;
	}

	lp->rx_ring[entry].base |= 0x80000000;
	entry = (entry+1) & RING_MOD_MASK;
    }
    lp->cur_rx = entry;

    return 0;
}

static int
lance_close(struct device *dev)
{
    int ioaddr = dev->base_addr;
    struct lance_private *lp = (struct lance_private *)dev->priv;

    dev->start = 0;
    dev->tbusy = 1;

    outw(112, ioaddr+LANCE_ADDR);
    lp->stats.rx_missed_errors = inw(ioaddr+LANCE_DATA);

    outw(0, ioaddr+LANCE_ADDR);

    if (lance_debug > 1)
	printk("%s: Shutting down ethercard, status was %2.2x.\n",
	       dev->name, inw(ioaddr+LANCE_DATA));
#ifdef PRINTK_STATS
    if (lance_debug > 2)
	printk_stats(&lp->stats);
#endif

    /* We stop the LANCE here -- it occasionally polls
       memory if we don't. */
    outw(0x0004, ioaddr+LANCE_DATA);

    disable_dma(lp->dma);

    free_irq(dev->irq);
    free_dma(lp->dma);

    irq2dev_map[dev->irq] = 0;

    return 0;
}

static struct enet_statistics *
lance_get_stats(struct device *dev)
{
    struct lance_private *lp = (struct lance_private *)dev->priv;
    short ioaddr = dev->base_addr;
    short saved_addr;

    cli();
    saved_addr = inw(ioaddr+LANCE_ADDR);
    outw(112, ioaddr+LANCE_ADDR);
    lp->stats.rx_missed_errors = inw(ioaddr+LANCE_DATA);
    outw(saved_addr, ioaddr+LANCE_ADDR);
    sti();

    return &lp->stats;
}

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -Wall -O6 -x c++ -c lance.c"
 * End:
 */
