/* lance32.c: An AMD PCnet32 ethernet driver for linux. */
/*
 *      Copyright 1996 Thomas Bogendoerfer
 * 
 * 	Derived from the lance driver written 1993,1994,1995 by Donald Becker.
 * 
 * 	Copyright 1993 United States Government as represented by the
 * 	Director, National Security Agency.
 * 
 * 	This software may be used and distributed according to the terms
 * 	of the GNU Public License, incorporated herein by reference.
 *
 * 	This driver is for PCnet32 and PCnetPCI based ethercards
 */

static const char *version = "lance32.c:v0.10 28.4.96 tsbogend@bigbug.franken.de\n";

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#ifdef LANCE32_DEBUG
int lance32_debug = LANCE32_DEBUG;
#else
int lance32_debug = 1;
#endif

/*
 * 				Theory of Operation
 * 
 * This driver uses the same software structure as the normal lance
 * driver. So look for a verbose description in lance.c. The differences
 * to the normal lance driver is the use of the 32bit mode of PCnet32
 * and PCnetPCI chips. Because these chips are 32bit chips, there is no
 * 16MB limitation and we don't need bounce buffers.
 */
 
/*
 * History:
 * v0.01:  Initial version
 *         only tested on Alpha Noname Board
 * v0.02:  changed IRQ handling for new interrupt scheme (dev_id)
 *         tested on a ASUS SP3G
 * v0.10:  fixed a odd problem with the 79C794 in a Compaq Deskpro XL
 *         looks like the 974 doesn't like stopping and restarting in a
 *         short period of time; now we do a reinit of the lance; the
 *         bug was triggered by doing ifconfig eth0 <ip> broadcast <addr>
 *         and hangs the machine (thanks to Klaus Liedl for debugging)
 */


/*
 * Set the number of Tx and Rx buffers, using Log_2(# buffers).
 * Reasonable default values are 4 Tx buffers, and 16 Rx buffers.
 * That translates to 2 (4 == 2^^2) and 4 (16 == 2^^4).
 */
#ifndef LANCE_LOG_TX_BUFFERS
#define LANCE_LOG_TX_BUFFERS 4
#define LANCE_LOG_RX_BUFFERS 4
#endif

#define TX_RING_SIZE			(1 << (LANCE_LOG_TX_BUFFERS))
#define TX_RING_MOD_MASK		(TX_RING_SIZE - 1)
#define TX_RING_LEN_BITS		((LANCE_LOG_TX_BUFFERS) << 12)

#define RX_RING_SIZE			(1 << (LANCE_LOG_RX_BUFFERS))
#define RX_RING_MOD_MASK		(RX_RING_SIZE - 1)
#define RX_RING_LEN_BITS		((LANCE_LOG_RX_BUFFERS) << 4)

#define PKT_BUF_SZ		1544

/* Offsets from base I/O address. */
#define LANCE_DATA 0x10
#define LANCE_ADDR 0x12
#define LANCE_RESET 0x14
#define LANCE_BUS_IF 0x16
#define LANCE_TOTAL_SIZE 0x18

/* The LANCE Rx and Tx ring descriptors. */
struct lance32_rx_head {
	u32 base;
	s16 buf_length;
        s16 status;    
	u32 msg_length;
	u32 reserved;
};
	
struct lance32_tx_head {
	u32 base;
	s16 length;
        s16 status;
	u32 misc;
	u32 reserved;
};


/* The LANCE 32-Bit initialization block, described in databook. */
struct lance32_init_block {
	u16 mode;
	u16 tlen_rlen;
	u8  phys_addr[6];
	u16 reserved;
	u32 filter[2];
	/* Receive and transmit ring base, along with extra bits. */    
	u32 rx_ring;
	u32 tx_ring;
};

struct lance32_private {
	/* The Tx and Rx ring entries must be aligned on 16-byte boundaries in 32bit mode. */
	struct lance32_rx_head   rx_ring[RX_RING_SIZE];
	struct lance32_tx_head   tx_ring[TX_RING_SIZE];
	struct lance32_init_block	init_block;
	const char *name;
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	unsigned long rx_buffs;			/* Address of Rx and Tx buffers. */
	int cur_rx, cur_tx;			/* The next free ring entry */
	int dirty_rx, dirty_tx;		        /* The ring entries to be free()ed. */
	int dma;
	struct enet_statistics stats;
	char tx_full;
	unsigned long lock;
};

static int lance32_open(struct device *dev);
static void lance32_init_ring(struct device *dev);
static int lance32_start_xmit(struct sk_buff *skb, struct device *dev);
static int lance32_rx(struct device *dev);
static void lance32_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int lance32_close(struct device *dev);
static struct enet_statistics *lance32_get_stats(struct device *dev);
static void lance32_set_multicast_list(struct device *dev);



/* lance32_probe1 */
void lance32_probe1(struct device *dev, char *chipname, int pci_irq_line)
{
	struct lance32_private *lp;
        int ioaddr = dev->base_addr;
	short dma_channels;					/* Mark spuriously-busy DMA channels */    
        int i;
    
	/* Make certain the data structures used by the LANCE are 16byte aligned and DMAble. */
	lp = (struct lance32_private *) (((unsigned long)kmalloc(sizeof(*lp)+15, GFP_DMA | GFP_KERNEL)+15) & ~15);
      
	memset(lp, 0, sizeof(*lp));
	dev->priv = lp;
	lp->name = chipname;
	lp->rx_buffs = (unsigned long) kmalloc(PKT_BUF_SZ*RX_RING_SIZE, GFP_DMA | GFP_KERNEL);

	lp->init_block.mode = 0x0003;		/* Disable Rx and Tx. */
        lp->init_block.tlen_rlen = TX_RING_LEN_BITS | RX_RING_LEN_BITS;    
	for (i = 0; i < 6; i++)
		lp->init_block.phys_addr[i] = dev->dev_addr[i];
	lp->init_block.filter[0] = 0x00000000;
	lp->init_block.filter[1] = 0x00000000;
	lp->init_block.rx_ring = (u32)virt_to_bus(lp->rx_ring);
	lp->init_block.tx_ring = (u32)virt_to_bus(lp->tx_ring);
    
	/* switch lance to 32bit mode */
	outw(0x0014, ioaddr+LANCE_ADDR);
	outw(0x0002, ioaddr+LANCE_BUS_IF);

	outw(0x0001, ioaddr+LANCE_ADDR);
	inw(ioaddr+LANCE_ADDR);
	outw((short) (u32) virt_to_bus(&lp->init_block), ioaddr+LANCE_DATA);
	outw(0x0002, ioaddr+LANCE_ADDR);
	inw(ioaddr+LANCE_ADDR);
	outw(((u32)virt_to_bus(&lp->init_block)) >> 16, ioaddr+LANCE_DATA);
	outw(0x0000, ioaddr+LANCE_ADDR);
	inw(ioaddr+LANCE_ADDR);
    
	if (pci_irq_line) {
		dev->dma = 4;			/* Native bus-master, no DMA channel needed. */
		dev->irq = pci_irq_line;
	} else {
		/* The DMA channel may be passed in PARAM1. */
		if (dev->mem_start & 0x07)
			dev->dma = dev->mem_start & 0x07;
	}

	if (dev->dma == 0) {
		/* Read the DMA channel status register, so that we can avoid
		   stuck DMA channels in the DMA detection below. */
		dma_channels = ((inb(DMA1_STAT_REG) >> 4) & 0x0f) |
			(inb(DMA2_STAT_REG) & 0xf0);
	}
	if (dev->irq >= 2)
		printk(" assigned IRQ %d", dev->irq);
	else {
		/* To auto-IRQ we enable the initialization-done and DMA error
		   interrupts. For ISA boards we get a DMA error, but VLB and PCI
		   boards will work. */
		autoirq_setup(0);

		/* Trigger an initialization just for the interrupt. */
		outw(0x0041, ioaddr+LANCE_DATA);

		dev->irq = autoirq_report(1);
		if (dev->irq)
			printk(", probed IRQ %d", dev->irq);
		else {
			printk(", failed to detect IRQ line.\n");
			return;
		}

		/* Check for the initialization done bit, 0x0100, which means
		   that we don't need a DMA channel. */
		if (inw(ioaddr+LANCE_DATA) & 0x0100)
			dev->dma = 4;
	}

	if (dev->dma == 4) {
		printk(", no DMA needed.\n");
	} else if (dev->dma) {
		if (request_dma(dev->dma, chipname)) {
			printk("DMA %d allocation failed.\n", dev->dma);
			return;
		} else
			printk(", assigned DMA %d.\n", dev->dma);
	} else {			/* OK, we have to auto-DMA. */
		for (i = 0; i < 4; i++) {
			static const char dmas[] = { 5, 6, 7, 3 };
			int dma = dmas[i];
			int boguscnt;

			/* Don't enable a permanently busy DMA channel, or the machine
			   will hang. */
			if (test_bit(dma, &dma_channels))
				continue;
			outw(0x7f04, ioaddr+LANCE_DATA); /* Clear the memory error bits. */
			if (request_dma(dma, chipname))
				continue;
			set_dma_mode(dma, DMA_MODE_CASCADE);
			enable_dma(dma);

			/* Trigger an initialization. */
			outw(0x0001, ioaddr+LANCE_DATA);
			for (boguscnt = 100; boguscnt > 0; --boguscnt)
				if (inw(ioaddr+LANCE_DATA) & 0x0900)
					break;
			if (inw(ioaddr+LANCE_DATA) & 0x0100) {
				dev->dma = dma;
				printk(", DMA %d.\n", dev->dma);
				break;
			} else {
				disable_dma(dma);
				free_dma(dma);
			}
		}
		if (i == 4) {			/* Failure: bail. */
			printk("DMA detection failed.\n");
			return;
		}
	}
    
        outw(0x0002, ioaddr+LANCE_ADDR);
	outw(0x0002, ioaddr+LANCE_BUS_IF);

	if (lance32_debug > 0)
		printk(version);

	/* The LANCE-specific entries in the device structure. */
	dev->open = &lance32_open;
	dev->hard_start_xmit = &lance32_start_xmit;
	dev->stop = &lance32_close;
	dev->get_stats = &lance32_get_stats;
	dev->set_multicast_list = &lance32_set_multicast_list;

	return;
}


static int
lance32_open(struct device *dev)
{
	struct lance32_private *lp = (struct lance32_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int i;

	if (dev->irq == 0 ||
		request_irq(dev->irq, &lance32_interrupt, 0, lp->name, (void *)dev)) {
		return -EAGAIN;
	}

	irq2dev_map[dev->irq] = dev;

	/* Reset the LANCE */
	inw(ioaddr+LANCE_RESET);

	/* switch lance to 32bit mode */
	outw(0x0014, ioaddr+LANCE_ADDR);
	outw(0x0002, ioaddr+LANCE_BUS_IF);

	/* The DMA controller is used as a no-operation slave, "cascade mode". */
	if (dev->dma != 4) {
		enable_dma(dev->dma);
		set_dma_mode(dev->dma, DMA_MODE_CASCADE);
	}

	/* Turn on auto-select of media (AUI, BNC). */
	outw(0x0002, ioaddr+LANCE_ADDR);
	outw(0x0002, ioaddr+LANCE_BUS_IF);

	if (lance32_debug > 1)
		printk("%s: lance32_open() irq %d dma %d tx/rx rings %#x/%#x init %#x.\n",
			   dev->name, dev->irq, dev->dma,
		           (u32) virt_to_bus(lp->tx_ring),
		           (u32) virt_to_bus(lp->rx_ring),
			   (u32) virt_to_bus(&lp->init_block));

	lp->init_block.mode = 0x0000;
	lp->init_block.filter[0] = 0x00000000;
	lp->init_block.filter[1] = 0x00000000;
	lance32_init_ring(dev);
    
	/* Re-initialize the LANCE, and start it when done. */
	outw(0x0001, ioaddr+LANCE_ADDR);
	outw((short) (u32) virt_to_bus(&lp->init_block), ioaddr+LANCE_DATA);
	outw(0x0002, ioaddr+LANCE_ADDR);
	outw(((u32)virt_to_bus(&lp->init_block)) >> 16, ioaddr+LANCE_DATA);

	outw(0x0004, ioaddr+LANCE_ADDR);
	outw(0x0915, ioaddr+LANCE_DATA);

	outw(0x0000, ioaddr+LANCE_ADDR);
	outw(0x0001, ioaddr+LANCE_DATA);

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;
	i = 0;
	while (i++ < 100)
		if (inw(ioaddr+LANCE_DATA) & 0x0100)
			break;
	/* 
	 * We used to clear the InitDone bit, 0x0100, here but Mark Stockton
	 * reports that doing so triggers a bug in the '974.
	 */
 	outw(0x0042, ioaddr+LANCE_DATA);

	if (lance32_debug > 2)
		printk("%s: LANCE32 open after %d ticks, init block %#x csr0 %4.4x.\n",
			   dev->name, i, (u32) virt_to_bus(&lp->init_block), inw(ioaddr+LANCE_DATA));

	return 0;					/* Always succeed */
}

/*
 * The LANCE has been halted for one reason or another (busmaster memory
 * arbitration error, Tx FIFO underflow, driver stopped it to reconfigure,
 * etc.).  Modern LANCE variants always reload their ring-buffer
 * configuration when restarted, so we must reinitialize our ring
 * context before restarting.  As part of this reinitialization,
 * find all packets still on the Tx ring and pretend that they had been
 * sent (in effect, drop the packets on the floor) - the higher-level
 * protocols will time out and retransmit.  It'd be better to shuffle
 * these skbs to a temp list and then actually re-Tx them after
 * restarting the chip, but I'm too lazy to do so right now.  dplatt@3do.com
 */

static void 
lance32_purge_tx_ring(struct device *dev)
{
	struct lance32_private *lp = (struct lance32_private *)dev->priv;
	int i;

	for (i = 0; i < TX_RING_SIZE; i++) {
		if (lp->tx_skbuff[i]) {
			dev_kfree_skb(lp->tx_skbuff[i],FREE_WRITE);
			lp->tx_skbuff[i] = NULL;
		}
	}
}


/* Initialize the LANCE Rx and Tx rings. */
static void
lance32_init_ring(struct device *dev)
{
	struct lance32_private *lp = (struct lance32_private *)dev->priv;
	int i;

	lp->lock = 0, lp->tx_full = 0;
	lp->cur_rx = lp->cur_tx = 0;
	lp->dirty_rx = lp->dirty_tx = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		lp->rx_ring[i].base = (u32)virt_to_bus((char *)lp->rx_buffs + i*PKT_BUF_SZ);
		lp->rx_ring[i].buf_length = -PKT_BUF_SZ;
	        lp->rx_ring[i].status = 0x8000;	    
	}
	/* The Tx buffer address is filled in as needed, but we do need to clear
	   the upper ownership bit. */
	for (i = 0; i < TX_RING_SIZE; i++) {
	        lp->tx_ring[i].base = 0;
	        lp->tx_ring[i].status = 0;
	}

        lp->init_block.tlen_rlen = TX_RING_LEN_BITS | RX_RING_LEN_BITS;
	for (i = 0; i < 6; i++)
		lp->init_block.phys_addr[i] = dev->dev_addr[i];
	lp->init_block.rx_ring = (u32)virt_to_bus(lp->rx_ring);
	lp->init_block.tx_ring = (u32)virt_to_bus(lp->tx_ring);
}

static void
lance32_restart(struct device *dev, unsigned int csr0_bits, int must_reinit)
{
        int i;
	int ioaddr = dev->base_addr;
    
	lance32_purge_tx_ring(dev);
	lance32_init_ring(dev);
    
	outw(0x0000, ioaddr + LANCE_ADDR);
        /* ReInit Ring */
        outw(0x0001, ioaddr + LANCE_DATA);
	i = 0;
	while (i++ < 100)
		if (inw(ioaddr+LANCE_DATA) & 0x0100)
			break;

	outw(csr0_bits, ioaddr + LANCE_DATA);
}

static int
lance32_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct lance32_private *lp = (struct lance32_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int entry;
	unsigned long flags;

	/* Transmitter timeout, serious problems. */
	if (dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 20)
			return 1;
		outw(0, ioaddr+LANCE_ADDR);
		printk("%s: transmit timed out, status %4.4x, resetting.\n",
			   dev->name, inw(ioaddr+LANCE_DATA));
		outw(0x0004, ioaddr+LANCE_DATA);
		lp->stats.tx_errors++;
#ifndef final_version
		{
			int i;
			printk(" Ring data dump: dirty_tx %d cur_tx %d%s cur_rx %d.",
				   lp->dirty_tx, lp->cur_tx, lp->tx_full ? " (full)" : "",
				   lp->cur_rx);
			for (i = 0 ; i < RX_RING_SIZE; i++)
				printk("%s %08x %04x %04x", i & 0x3 ? "" : "\n ",
					   lp->rx_ring[i].base, -lp->rx_ring[i].buf_length,
					   lp->rx_ring[i].msg_length);
			for (i = 0 ; i < TX_RING_SIZE; i++)
				printk("%s %08x %04x %04x", i & 0x3 ? "" : "\n ",
					   lp->tx_ring[i].base, -lp->tx_ring[i].length,
					   lp->tx_ring[i].misc);
			printk("\n");
		}
#endif
		lance32_restart(dev, 0x0042, 1);

		dev->tbusy=0;
		dev->trans_start = jiffies;

		return 0;
	}

	if (skb == NULL) {
		dev_tint(dev);
		return 0;
	}

	if (skb->len <= 0)
		return 0;

	if (lance32_debug > 3) {
		outw(0x0000, ioaddr+LANCE_ADDR);
		printk("%s: lance32_start_xmit() called, csr0 %4.4x.\n", dev->name,
			   inw(ioaddr+LANCE_DATA));
		outw(0x0000, ioaddr+LANCE_DATA);
	}

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (set_bit(0, (void*)&dev->tbusy) != 0) {
		printk("%s: Transmitter access conflict.\n", dev->name);
		return 1;
	}

	if (set_bit(0, (void*)&lp->lock) != 0) {
		if (lance32_debug > 0)
			printk("%s: tx queue lock!.\n", dev->name);
		/* don't clear dev->tbusy flag. */
		return 1;
	}

	/* Fill in a Tx ring entry */

	/* Mask to ring buffer boundary. */
	entry = lp->cur_tx & TX_RING_MOD_MASK;

	/* Caution: the write order is important here, set the base address
	   with the "ownership" bits last. */

	lp->tx_ring[entry].length = -skb->len;

	lp->tx_ring[entry].misc = 0x00000000;

	lp->tx_skbuff[entry] = skb;
	lp->tx_ring[entry].base = (u32)virt_to_bus(skb->data);
        lp->tx_ring[entry].status = 0x8300;

	lp->cur_tx++;

	/* Trigger an immediate send poll. */
	outw(0x0000, ioaddr+LANCE_ADDR);
	outw(0x0048, ioaddr+LANCE_DATA);

	dev->trans_start = jiffies;

	save_flags(flags);
	cli();
	lp->lock = 0;
	if (lp->tx_ring[(entry+1) & TX_RING_MOD_MASK].base == 0)
		dev->tbusy=0;
	else
		lp->tx_full = 1;
	restore_flags(flags);

	return 0;
}

/* The LANCE32 interrupt handler. */
static void
lance32_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct device *dev = (struct device *)dev_id;
	struct lance32_private *lp;
	int csr0, ioaddr, boguscnt=10;
	int must_restart;

	if (dev == NULL) {
		printk ("lance32_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}

	ioaddr = dev->base_addr;
	lp = (struct lance32_private *)dev->priv;
	if (dev->interrupt)
		printk("%s: Re-entering the interrupt handler.\n", dev->name);

	dev->interrupt = 1;

	outw(0x00, dev->base_addr + LANCE_ADDR);
	while ((csr0 = inw(dev->base_addr + LANCE_DATA)) & 0x8600
		   && --boguscnt >= 0) {
		/* Acknowledge all of the current interrupt sources ASAP. */
		outw(csr0 & ~0x004f, dev->base_addr + LANCE_DATA);

		must_restart = 0;

		if (lance32_debug > 5)
			printk("%s: interrupt  csr0=%#2.2x new csr=%#2.2x.\n",
				   dev->name, csr0, inw(dev->base_addr + LANCE_DATA));

		if (csr0 & 0x0400)			/* Rx interrupt */
			lance32_rx(dev);

		if (csr0 & 0x0200) {		/* Tx-done interrupt */
			int dirty_tx = lp->dirty_tx;

			while (dirty_tx < lp->cur_tx) {
				int entry = dirty_tx & TX_RING_MOD_MASK;
				int status = lp->tx_ring[entry].status;
			
				if (status < 0)
					break;			/* It still hasn't been Txed */

				lp->tx_ring[entry].base = 0;

				if (status & 0x4000) {
					/* There was an major error, log it. */
					int err_status = lp->tx_ring[entry].misc;
					lp->stats.tx_errors++;
					if (err_status & 0x04000000) lp->stats.tx_aborted_errors++;
					if (err_status & 0x08000000) lp->stats.tx_carrier_errors++;
					if (err_status & 0x10000000) lp->stats.tx_window_errors++;
					if (err_status & 0x40000000) {
						/* Ackk!  On FIFO errors the Tx unit is turned off! */
						lp->stats.tx_fifo_errors++;
						/* Remove this verbosity later! */
						printk("%s: Tx FIFO error! Status %4.4x.\n",
							   dev->name, csr0);
						/* Restart the chip. */
						must_restart = 1;
					}
				} else {
					if (status & 0x1800)
						lp->stats.collisions++;
					lp->stats.tx_packets++;
				}

				/* We must free the original skb */
				if (lp->tx_skbuff[entry]) {
					dev_kfree_skb(lp->tx_skbuff[entry],FREE_WRITE);
					lp->tx_skbuff[entry] = 0;
				}
				dirty_tx++;
			}

#ifndef final_version
			if (lp->cur_tx - dirty_tx >= TX_RING_SIZE) {
				printk("out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
					   dirty_tx, lp->cur_tx, lp->tx_full);
				dirty_tx += TX_RING_SIZE;
			}
#endif

			if (lp->tx_full && dev->tbusy
				&& dirty_tx > lp->cur_tx - TX_RING_SIZE + 2) {
				/* The ring is no longer full, clear tbusy. */
				lp->tx_full = 0;
				dev->tbusy = 0;
				mark_bh(NET_BH);
			}

			lp->dirty_tx = dirty_tx;
		}

		/* Log misc errors. */
		if (csr0 & 0x4000) lp->stats.tx_errors++; /* Tx babble. */
		if (csr0 & 0x1000) lp->stats.rx_errors++; /* Missed a Rx frame. */
		if (csr0 & 0x0800) {
			printk("%s: Bus master arbitration failure, status %4.4x.\n",
				   dev->name, csr0);
			/* Restart the chip. */
			must_restart = 1;
		}

		if (must_restart) {
			/* stop the chip to clear the error condition, then restart */
			outw(0x0000, dev->base_addr + LANCE_ADDR);
			outw(0x0004, dev->base_addr + LANCE_DATA);
			lance32_restart(dev, 0x0002, 0);
		}
	}

    /* Clear any other interrupt, and set interrupt enable. */
    outw(0x0000, dev->base_addr + LANCE_ADDR);
    outw(0x7940, dev->base_addr + LANCE_DATA);

	if (lance32_debug > 4)
		printk("%s: exiting interrupt, csr%d=%#4.4x.\n",
			   dev->name, inw(ioaddr + LANCE_ADDR),
			   inw(dev->base_addr + LANCE_DATA));

	dev->interrupt = 0;
	return;
}

static int
lance32_rx(struct device *dev)
{
	struct lance32_private *lp = (struct lance32_private *)dev->priv;
	int entry = lp->cur_rx & RX_RING_MOD_MASK;
	int i;
		
	/* If we own the next entry, it's a new packet. Send it up. */
	while (lp->rx_ring[entry].status >= 0) {
		int status = lp->rx_ring[entry].status >> 8;

		if (status != 0x03) {			/* There was an error. */
			/* There is a tricky error noted by John Murphy,
			   <murf@perftech.com> to Russ Nelson: Even with full-sized
			   buffers it's possible for a jabber packet to use two
			   buffers, with only the last correctly noting the error. */
			if (status & 0x01)	/* Only count a general error at the */
				lp->stats.rx_errors++; /* end of a packet.*/
			if (status & 0x20) lp->stats.rx_frame_errors++;
			if (status & 0x10) lp->stats.rx_over_errors++;
			if (status & 0x08) lp->stats.rx_crc_errors++;
			if (status & 0x04) lp->stats.rx_fifo_errors++;
			lp->rx_ring[entry].status &= 0x03ff;
		}
		else 
		{
			/* Malloc up new buffer, compatible with net-2e. */
			short pkt_len = (lp->rx_ring[entry].msg_length & 0xfff)-4;
			struct sk_buff *skb;
			
			if(pkt_len<60)
			{
				printk("%s: Runt packet!\n",dev->name);
				lp->stats.rx_errors++;
			}
			else
			{
				skb = dev_alloc_skb(pkt_len+2);
				if (skb == NULL) 
				{
					printk("%s: Memory squeeze, deferring packet.\n", dev->name);
					for (i=0; i < RX_RING_SIZE; i++)
						if (lp->rx_ring[(entry+i) & RX_RING_MOD_MASK].status < 0)
							break;

					if (i > RX_RING_SIZE -2) 
					{
						lp->stats.rx_dropped++;
						lp->rx_ring[entry].status |= 0x8000;
						lp->cur_rx++;
					}
					break;
				}
				skb->dev = dev;
				skb_reserve(skb,2);	/* 16 byte align */
				skb_put(skb,pkt_len);	/* Make room */
				eth_copy_and_sum(skb,
					(unsigned char *)bus_to_virt(lp->rx_ring[entry].base),
					pkt_len,0);
				skb->protocol=eth_type_trans(skb,dev);
				netif_rx(skb);
				lp->stats.rx_packets++;
			}
		}
		/* The docs say that the buffer length isn't touched, but Andrew Boyd
		   of QNX reports that some revs of the 79C965 clear it. */
		lp->rx_ring[entry].buf_length = -PKT_BUF_SZ;
		lp->rx_ring[entry].status |= 0x8000;
		entry = (++lp->cur_rx) & RX_RING_MOD_MASK;
	}

	/* We should check that at least two ring entries are free.	 If not,
	   we should free one and mark stats->rx_dropped++. */

	return 0;
}

static int
lance32_close(struct device *dev)
{
	int ioaddr = dev->base_addr;
	struct lance32_private *lp = (struct lance32_private *)dev->priv;

	dev->start = 0;
	dev->tbusy = 1;

	outw(112, ioaddr+LANCE_ADDR);
	lp->stats.rx_missed_errors = inw(ioaddr+LANCE_DATA);

	outw(0, ioaddr+LANCE_ADDR);

	if (lance32_debug > 1)
		printk("%s: Shutting down ethercard, status was %2.2x.\n",
			   dev->name, inw(ioaddr+LANCE_DATA));

	/* We stop the LANCE here -- it occasionally polls
	   memory if we don't. */
	outw(0x0004, ioaddr+LANCE_DATA);

	if (dev->dma != 4)
		disable_dma(dev->dma);

	free_irq(dev->irq, dev);

	irq2dev_map[dev->irq] = 0;

	return 0;
}

static struct enet_statistics *
lance32_get_stats(struct device *dev)
{
	struct lance32_private *lp = (struct lance32_private *)dev->priv;
	short ioaddr = dev->base_addr;
	short saved_addr;
	unsigned long flags;

	save_flags(flags);
	cli();
	saved_addr = inw(ioaddr+LANCE_ADDR);
	outw(112, ioaddr+LANCE_ADDR);
	lp->stats.rx_missed_errors = inw(ioaddr+LANCE_DATA);
	outw(saved_addr, ioaddr+LANCE_ADDR);
	restore_flags(flags);

	return &lp->stats;
}

/* Set or clear the multicast filter for this adaptor.
 */

static void lance32_set_multicast_list(struct device *dev)
{
	short ioaddr = dev->base_addr;
	struct lance32_private *lp = (struct lance32_private *)dev->priv;    

	if (dev->flags&IFF_PROMISC) {
		/* Log any net taps. */
		printk("%s: Promiscuous mode enabled.\n", dev->name);
	        lp->init_block.mode = 0x8000;
	} else {
		int num_addrs=dev->mc_count;
		if(dev->flags&IFF_ALLMULTI)
			num_addrs=1;
		/* FIXIT: We don't use the multicast table, but rely on upper-layer filtering. */
		memset(lp->init_block.filter , (num_addrs == 0) ? 0 : -1, sizeof(lp->init_block.filter));
	        lp->init_block.mode = 0x0000;	        
	}
    
	outw(0, ioaddr+LANCE_ADDR);
	outw(0x0004, ioaddr+LANCE_DATA); /* Temporarily stop the lance.	 */

	lance32_restart(dev, 0x0042, 0); /*  Resume normal operation */

}


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c lance32.c"
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */
