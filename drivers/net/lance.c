/* lance.c: An AMD LANCE ethernet driver for linux. */
/*
    Written 1993 by Donald Becker.

    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.  This software may be used and
    distributed according to the terms of the GNU Public License,
    incorporated herein by reference.

    This driver is for the Allied Telesis AT1500 and HP J2405A, and should work
    with most other LANCE-based bus-master (NE2100 clone) ethercards.

    The author may be reached as becker@super.org or
    C/O Supercomputing Research Ctr., 17100 Science Dr., Bowie MD 20715
*/

static char *version = "lance.c:v0.13f 10/18/93 becker@super.org\n";

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include "dev.h"
#include "iow.h"
#include "eth.h"
#include "skbuff.h"
#include "arp.h"

#ifdef LANCE_DEBUG
int lance_debug = LANCE_DEBUG;
#else
int lance_debug = 1;
#endif

#ifndef LANCE_DMA
#define LANCE_DMA	5
#endif

/* Set the number of Tx and Rx buffers. */
#ifndef LANCE_BUFFER_LOG_SZ
#define RING_SIZE	16
#define RING_MOD_MASK	0x0f
#define RING_LEN_BITS	0x80000000
#else
#define RING_SIZE	(1 << (LANCE_BUFFERS_LOG_SZ))
#define RING_MOD_MASK	(RING_SIZE - 1)
#define RING_LEN_BITS	(LANCE_BUFFER_LOG_SZ << 13)
#endif

#ifndef HAVE_ALLOC_SKB
#define alloc_skb(size, priority) (struct sk_buff *) kmalloc(size,priority)
#define kfree_skbmem(buff, size) kfree_s(buff,size)
#endif

/* Offsets from base I/O address. */
#define LANCE_DATA 0x10
#define LANCE_ADDR 0x12
#define LANCE_RESET 0x14
#define LANCE_BUS_IF 0x16
#define LANCE_TOTAL_SIZE 0x18

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
    char devname[8];
    /* These must aligned on 8-byte boundaries. */
    struct lance_rx_head rx_ring[RING_SIZE];
    struct lance_tx_head tx_ring[RING_SIZE];
    struct lance_init	init_block;
    long dma_buffs;		/* Address of Rx and Tx buffers. */
    int	cur_rx, cur_tx;		/* The next free ring entry */
    int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
    int dma;
    struct enet_statistics stats;
    char old_lance;
    int pad0, pad1;		/* Used for alignment */
};

/* We need a ethercard low-memory allocation scheme for for bus-master or
   DMA ethercards if they are to work >16M memory systems. This is a
   temporary solution to the lack of one, but it limits us to a single
   AT1500 and <16M. Bummer. */

#define PKT_BUF_SZ	1544

#ifndef MEM_START_ALLOC
static char rx_buffs[PKT_BUF_SZ][RING_SIZE];
#endif

static int lance_probe1(struct device *dev, short ioaddr);
static int lance_open(struct device *dev);
static void lance_init_ring(struct device *dev);
static int lance_start_xmit(struct sk_buff *skb, struct device *dev);
static int lance_rx(struct device *dev);
static void lance_interrupt(int reg_ptr);
static int lance_close(struct device *dev);
static struct enet_statistics *lance_get_stats(struct device *dev);
#ifdef HAVE_MULTICAST
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);
#endif



int at1500_probe(struct device *dev)
{
    int *port, ports[] = {0x300, 0x320, 0x340, 0x360, 0};
    int base_addr = dev->base_addr;

    if (base_addr > 0x1ff)		/* Check a single specified location. */
		return lance_probe1(dev, base_addr);
    else if (base_addr > 0)		/* Don't probe at all. */
		return ENXIO;

    /* First probe for the ethercard ID, 0x57, and then look for a LANCE
       chip. */
    
    for (port = &ports[0]; *port; port++) {
	int ioaddr = *port;

#ifdef HAVE_PORTRESERVE
	if (check_region(ioaddr, LANCE_TOTAL_SIZE))
	    continue;
#endif
	if (inb(ioaddr + 14) != 0x57
	    || inb(ioaddr + 15) != 0x57)
	    continue;

	if (lance_probe1(dev, ioaddr) == 0)
	    return 0;
    }

    dev->base_addr = base_addr;
    return ENODEV;			/* ENODEV would be more accurate. */
}

static int
lance_probe1(struct device *dev, short ioaddr)
{
    struct lance_private *lp;
    int hpJ2405A = 0;
    int i, reset_val;

    hpJ2405A = (inb(ioaddr) == 0x08 && inb(ioaddr+1) == 0x00
		&& inb(ioaddr+2) == 0x09);

    /* Reset the LANCE.  */
    reset_val = inw(ioaddr+LANCE_RESET); /* Reset the LANCE */

    /* The Un-Reset needed is only needed for the real NE2100, and will
       confuse the HP board. */
    if (!hpJ2405A)
	outw(reset_val, ioaddr+LANCE_RESET);

    outw(0x0000, ioaddr+LANCE_ADDR); /* Switch to window 0 */
    if (inw(ioaddr+LANCE_DATA) != 0x0004)
	return ENXIO;

    printk("%s: LANCE at %#3x,", dev->name, ioaddr);

    /* There is a 16 byte station address PROM at the base address.
       The first six bytes are the station address. */
    for (i = 0; i < 6; i++)
	printk(" %2.2x", dev->dev_addr[i] = inb(ioaddr + i));

    dev->base_addr = ioaddr;
#ifdef HAVE_PORTRESERVE
    snarf_region(ioaddr, LANCE_TOTAL_SIZE);
#endif

    /* Make up a LANCE-specific-data structure. */
#ifdef MEM_START_ALLOC
    dev->priv = (void *)((mem_start + 7) & ~7);
    mem_start += (long)dev->priv + sizeof(struct lance_private);
    lp = (struct lance_private *)dev->priv;
    lp->dma_buffs = mem_start;
    mem_start += PKT_BUF_SZ * RING_SIZE;
#else    
    dev->priv = kmalloc(sizeof(struct lance_private), GFP_KERNEL);
    /* Align on 8-byte boundary. */
    dev->priv = (void *)(((int)dev->priv + 7) & ~0x07);

    if ((int)dev->priv & 0xff000000  ||  (int) rx_buffs & 0xff000000) {
	printk(" disabled (buff %#x > 16M).\n", (int)rx_buffs);
	return -ENOMEM;
    }
    memset(dev->priv, 0, sizeof(struct lance_private));
    lp = (struct lance_private *)dev->priv;
    lp->dma_buffs = (long)rx_buffs;
#endif

#ifndef final_version
    /* This should never happen. */
    if ((int)(lp->rx_ring) & 0x07) {
	printk(" **ERROR** LANCE Rx and Tx rings not on even boundary.\n");
	return -ENXIO;
    }
#endif

    outw(88, ioaddr+LANCE_ADDR);
    lp->old_lance = (inw(ioaddr+LANCE_DATA) != 0x3003);

#ifdef notdef
    printk(lp->old_lance ? " original LANCE (%04x)" : " PCnet-ISA LANCE (%04x)",
	   inw(ioaddr+LANCE_DATA));
#endif

    lp->init_block.mode = 0x0003;	/* Disable Rx and Tx. */
    for (i = 0; i < 6; i++)
	lp->init_block.phys_addr[i] = dev->dev_addr[i];
    lp->init_block.filter[0] = 0x00000000;
    lp->init_block.filter[1] = 0x00000000;
    lp->init_block.rx_ring = (int)lp->rx_ring | RING_LEN_BITS;
    lp->init_block.tx_ring = (int)lp->tx_ring | RING_LEN_BITS;

    outw(0x0001, ioaddr+LANCE_ADDR);
    outw((short) (int) &lp->init_block, ioaddr+LANCE_DATA);
    outw(0x0002, ioaddr+LANCE_ADDR);
    outw(((int)&lp->init_block) >> 16, ioaddr+LANCE_DATA);
    outw(0x0000, ioaddr+LANCE_ADDR);

    if (hpJ2405A) {
	char dma_tbl[4] = {3, 5, 6, 7};
	char irq_tbl[8] = {3, 4, 5, 9, 10, 11, 12, 15};
	short reset_val = inw(ioaddr+LANCE_RESET);
	dev->dma = dma_tbl[(reset_val >> 2) & 3];
	dev->irq = irq_tbl[(reset_val >> 4) & 7];
	printk(" HP J2405A IRQ %d DMA %d.\n", dev->irq, dev->dma);
    } else {
	/* The DMA channel may be passed in on this parameter. */
	dev->dma = dev->mem_start & 0x07;

	/* To auto-IRQ we enable the initialization-done and DMA err,
	   interrupts. For now we will always get a DMA error. */
	if (dev->irq < 2) {
	    autoirq_setup(0);

	    /* Trigger an initialization just for the interrupt. */
	    outw(0x0041, ioaddr+LANCE_DATA);

	    dev->irq = autoirq_report(1);
	    if (dev->irq)
		printk(", probed IRQ %d, fixed at DMA %d.\n", dev->irq, dev->dma);
	    else {
		printk(", failed to detect IRQ line.\n");
		return -EAGAIN;
	    }
	} else
	    printk(" assigned IRQ %d DMA %d.\n", dev->irq, dev->dma);
    }

    if (! lp->old_lance) {
	/* Turn on auto-select of media (10baseT or BNC) so that the user
	   can watch the LEDs even if the board isn't opened. */
	outw(0x0002, ioaddr+LANCE_ADDR);
	outw(0x0002, ioaddr+LANCE_BUS_IF);
    }

    if (lance_debug > 0)
	printk(version);

    /* The LANCE-specific entries in the device structure. */
    dev->open = &lance_open;
    dev->hard_start_xmit = &lance_start_xmit;
    dev->stop = &lance_close;
    dev->get_stats = &lance_get_stats;
#ifdef HAVE_MULTICAST
    dev->set_multicast_list = &set_multicast_list;
#endif

    dev->mem_start = 0;

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

    return 0;
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

    lp->dma = dev->dma ? dev->dma : LANCE_DMA;

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
    if (lp->old_lance)
	outw(0, ioaddr+LANCE_RESET);

    if (! lp->old_lance) {
	/* This is 79C960-specific: Turn on auto-select of media (AUI, BNC). */
	outw(0x0002, ioaddr+LANCE_ADDR);
	outw(0x0002, ioaddr+LANCE_BUS_IF);
    }

    if (lance_debug > 1)
	printk("%s: lance_open() irq %d dma %d tx/rx rings %#x/%#x init %#x.\n",
	       dev->name, dev->irq, lp->dma, (int) lp->tx_ring, (int) lp->rx_ring,
	       (int) &lp->init_block);

    lance_init_ring(dev);
    /* Re-initialize the LANCE, and start it when done. */
    outw(0x0001, ioaddr+LANCE_ADDR);
    outw((short) (int) &lp->init_block, ioaddr+LANCE_DATA);
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
	       dev->name, i, (int) &lp->init_block, inw(ioaddr+LANCE_DATA));

    return 0;			/* Always succeed */
}

/* Initialize the LANCE Rx and Tx rings. */
static void
lance_init_ring(struct device *dev)
{
    struct lance_private *lp = (struct lance_private *)dev->priv;
    int i;

    lp->cur_rx = lp->cur_tx = 0;
    lp->dirty_rx = lp->dirty_tx = 0;

    for (i = 0; i < RING_SIZE; i++) {
	lp->rx_ring[i].base = (lp->dma_buffs + i*PKT_BUF_SZ) | 0x80000000;
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
}

static int
lance_start_xmit(struct sk_buff *skb, struct device *dev)
{
    struct lance_private *lp = (struct lance_private *)dev->priv;
    int ioaddr = dev->base_addr;

    /* Transmitter timeout, serious problems. */
    if (dev->tbusy) {
	int tickssofar = jiffies - dev->trans_start;
	if (tickssofar < 5)
	    return 1;
	outw(0, ioaddr+LANCE_ADDR);
	printk("%s: transmit timed out, status %4.4x, resetting.\n",
	       dev->name, inw(ioaddr+LANCE_DATA));

	lance_init_ring(dev);
	outw(0x43, ioaddr+LANCE_DATA);

	dev->tbusy=0;
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
    skb->arp=1;

    if (skb->len <= 0)
	return 0;

    if (lance_debug > 3) {
	outw(0x0000, ioaddr+LANCE_ADDR);
	printk("%s: lance_start_xmit() called, csr0 %4.4x.\n", dev->name,
	       inw(ioaddr+LANCE_DATA));
	outw(0x0000, ioaddr+LANCE_DATA);
    }

    /* Block a timer-based transmit from overlapping.  This could better be
       done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
    if (set_bit(0, (void*)&dev->tbusy) != 0)
	printk("%s: Transmitter access conflict.\n", dev->name);

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
		   dev->name, entry, (int) &lp->tx_ring[entry],
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
    struct device *dev = (struct device *)(irq2dev_map[irq]);
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
	    struct sk_buff *skb =
		(struct sk_buff *)(lp->tx_ring[dirty_tx].base & 0x00ffffff);
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
	/* mark_bh(INET_BH); */
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
	
    /* If we own the next entry, it's a new packet. Send it up. */
    while (lp->rx_ring[entry].base >= 0) {
	int status = lp->rx_ring[entry].base >> 24;

	if (status & 0x40) {	/* There was an error. */
	    lp->stats.rx_errors++;
	    if (status & 0x20) lp->stats.rx_frame_errors++;
	    if (status & 0x10) lp->stats.rx_over_errors++;
	    if (status & 0x08) lp->stats.rx_crc_errors++;
	    if (status & 0x04) lp->stats.rx_fifo_errors++;
	} else {
	    /* Malloc up new buffer, compatible with net-2e. */
	    short pkt_len = lp->rx_ring[entry].msg_length;
	    int sksize = sizeof(struct sk_buff) + pkt_len;
	    struct sk_buff *skb;

	    skb = alloc_skb(sksize, GFP_ATOMIC);
	    if (skb == NULL) {
		printk("%s: Memory squeeze, deferring packet.\n", dev->name);
		lp->stats.rx_dropped++;	/* Really, deferred. */
		break;
	    }
	    skb->mem_len = sksize;
	    skb->mem_addr = skb;
	    skb->len = pkt_len;
	    skb->dev = dev;
	    memcpy((unsigned char *) (skb + 1),
		   (unsigned char *)(lp->rx_ring[entry].base & 0x00ffffff),
		   pkt_len);
#ifdef HAVE_NETIF_RX
	    netif_rx(skb);
#else
	    skb->lock = 0;
	    if (dev_rint((unsigned char*)skb, pkt_len, IN_SKBUFF, dev) != 0) {
		kfree_skbmem(skb, sksize);
		lp->stats.rx_dropped++;
		break;
	    }
#endif
	    lp->stats.rx_packets++;
	}

	lp->rx_ring[entry].base |= 0x80000000;
	entry = (entry+1) & RING_MOD_MASK;
    }

    /* We should check that at least two ring entries are free.  If not,
       we should free one and mark stats->rx_dropped++. */

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

#ifdef HAVE_MULTICAST
/* Set or clear the multicast filter for this adaptor.
   num_addrs == -1	Promiscuous mode, receive all packets
   num_addrs == 0	Normal mode, clear multicast list
   num_addrs > 0	Multicast mode, receive normal and MC packets, and do
   			best-effort filtering.
 */
static void
set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
    short ioaddr = dev->base_addr;

    /* We take the simple way out and always enable promiscuous mode. */
    outw(0, ioaddr+LANCE_ADDR);
    outw(0x0004, ioaddr+LANCE_DATA); /* Temporarily stop the lance.  */

    outw(15, ioaddr+LANCE_ADDR);
    if (num_addrs >= 0) {
	short multicast_table[4];
	int i;
	/* We don't use the multicast table, but rely on upper-layer filtering. */
	memset(multicast_table, (num_addrs == 0) ? 0 : -1, sizeof(multicast_table));
	for (i = 0; i < 4; i++) {
	    outw(8 + i, ioaddr+LANCE_ADDR);
	    outw(multicast_table[i], ioaddr+LANCE_DATA);
	}
	outw(0x0000, ioaddr+LANCE_DATA); /* Unset promiscuous mode */
    } else {
	outw(0x8000, ioaddr+LANCE_DATA); /* Set promiscuous mode */
    }

    outw(0, ioaddr+LANCE_ADDR);
    outw(0x0142, ioaddr+LANCE_DATA); /* Resume normal operation. */
}
#endif

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c lance.c"
 * End:
 */
