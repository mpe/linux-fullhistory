/* 8390.c: A general NS8390 ethernet driver core for linux. */
/*
	Written 1992-94 by Donald Becker.
  
	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771
  
  This is the chip-specific code for many 8390-based ethernet adaptors.
  This is not a complete driver, it must be combined with board-specific
  code such as ne.c, wd.c, 3c503.c, etc.

  Seeing how at least eight drivers use this code, (not counting the
  PCMCIA ones either) it is easy to break some card by what seems like
  a simple innocent change. Please contact me or Donald if you think
  you have found something that needs changing. -- PG


  Changelog:

  Paul Gortmaker	: remove set_bit lock, other cleanups.
  Paul Gortmaker	: add ei_get_8390_hdr() so we can pass skb's to 
			  ei_block_input() for eth_io_copy_and_sum().
  Paul Gortmaker	: exchange static int ei_pingpong for a #define,
			  also add better Tx error handling.
  Paul Gortmaker	: rewrite Rx overrun handling as per NS specs.
  Alexey Kuznetsov	: use the 8390's six bit hash multicast filter.
  Paul Gortmaker	: tweak ANK's above multicast changes a bit.
  Paul Gortmaker	: update packet statistics for v2.1.x
  Alan Cox		: support arbitary stupid port mappings on the
  			  68K Macintosh. Support >16bit I/O spaces
  Paul Gortmaker	: add kmod support for auto-loading of the 8390
			  module by all drivers that require it.


  Sources:
  The National Semiconductor LAN Databook, and the 3Com 3c503 databook.

  */

static const char *version =
    "8390.c:v1.10cvs 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#define NS8390_CORE
#include "8390.h"

/* These are the operational function interfaces to board-specific
   routines.
	void reset_8390(struct device *dev)
		Resets the board associated with DEV, including a hardware reset of
		the 8390.  This is only called when there is a transmit timeout, and
		it is always followed by 8390_init().
	void block_output(struct device *dev, int count, const unsigned char *buf,
					  int start_page)
		Write the COUNT bytes of BUF to the packet buffer at START_PAGE.  The
		"page" value uses the 8390's 256-byte pages.
	void get_8390_hdr(struct device *dev, struct e8390_hdr *hdr, int ring_page)
		Read the 4 byte, page aligned 8390 header. *If* there is a
		subsequent read, it will be of the rest of the packet.
	void block_input(struct device *dev, int count, struct sk_buff *skb, int ring_offset)
		Read COUNT bytes from the packet buffer into the skb data area. Start 
		reading from RING_OFFSET, the address as the 8390 sees it.  This will always
		follow the read of the 8390 header. 
*/
#define ei_reset_8390 (ei_local->reset_8390)
#define ei_block_output (ei_local->block_output)
#define ei_block_input (ei_local->block_input)
#define ei_get_8390_hdr (ei_local->get_8390_hdr)

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef ei_debug
int ei_debug = 1;
#endif

/* Index to functions. */
static void ei_tx_intr(struct device *dev);
static void ei_tx_err(struct device *dev);
static void ei_receive(struct device *dev);
static void ei_rx_overrun(struct device *dev);

/* Routines generic to NS8390-based boards. */
static void NS8390_trigger_send(struct device *dev, unsigned int length,
								int start_page);
static void set_multicast_list(struct device *dev);


/* Open/initialize the board.  This routine goes all-out, setting everything
   up anew at each open, even though many of these registers should only
   need to be set once at boot.
   */
int ei_open(struct device *dev)
{
	struct ei_device *ei_local = (struct ei_device *) dev->priv;

	/* This can't happen unless somebody forgot to call ethdev_init(). */
	if (ei_local == NULL) 
	{
		printk(KERN_EMERG "%s: ei_open passed a non-existent device!\n", dev->name);
		return -ENXIO;
	}
    
	NS8390_init(dev, 1);
	dev->start = 1;
	ei_local->irqlock = 0;
	return 0;
}

/* Opposite of above. Only used when "ifconfig <devname> down" is done. */
int ei_close(struct device *dev)
{
	NS8390_init(dev, 0);
	dev->start = 0;
	return 0;
}

static int ei_start_xmit(struct sk_buff *skb, struct device *dev)
{
	int e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) dev->priv;
	int length, send_length, output_page;

	/*
	 *  We normally shouldn't be called if dev->tbusy is set, but the
	 *  existing code does anyway. If it has been too long since the
	 *  last Tx, we assume the board has died and kick it.
	 */
 
	if (dev->tbusy) 
	{	/* Do timeouts, just like the 8003 driver. */
		int txsr = inb(e8390_base+EN0_TSR), isr;
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < TX_TIMEOUT ||	(tickssofar < (TX_TIMEOUT+5) && ! (txsr & ENTSR_PTX))) 
			return 1;

		ei_local->stat.tx_errors++;
		isr = inb(e8390_base+EN0_ISR);
		if (dev->start == 0) 
		{
			printk(KERN_WARNING "%s: xmit on stopped card\n", dev->name);
			return 1;
		}

		/*
		 * Note that if the Tx posted a TX_ERR interrupt, then the
		 * error will have been handled from the interrupt handler
		 * and not here. Error statistics are handled there as well.
		 */

		printk(KERN_DEBUG "%s: Tx timed out, %s TSR=%#2x, ISR=%#2x, t=%d.\n",
			dev->name, (txsr & ENTSR_ABT) ? "excess collisions." :
			(isr) ? "lost interrupt?" : "cable problem?", txsr, isr, tickssofar);

		if (!isr && !ei_local->stat.tx_packets) 
		{
			/* The 8390 probably hasn't gotten on the cable yet. */
			ei_local->interface_num ^= 1;   /* Try a different xcvr.  */
		}

		/* Try to restart the card.  Perhaps the user has fixed something. */
		ei_reset_8390(dev);
		NS8390_init(dev, 1);
		dev->trans_start = jiffies;
	}
    
	length = skb->len;

	/* Mask interrupts from the ethercard. */
	outb_p(0x00, e8390_base + EN0_IMR);
	disable_irq(dev->irq);
	synchronize_irq();
	if (dev->interrupt) 
	{
		printk(KERN_WARNING "%s: Tx request while isr active.\n",dev->name);
		outb_p(ENISR_ALL, e8390_base + EN0_IMR);
		enable_irq(dev->irq);
		ei_local->stat.tx_errors++;
		dev_kfree_skb(skb);
		return 0;
	}
	ei_local->irqlock = 1;

	send_length = ETH_ZLEN < length ? length : ETH_ZLEN;
    
#ifdef EI_PINGPONG

	/*
	 * We have two Tx slots available for use. Find the first free
	 * slot, and then perform some sanity checks. With two Tx bufs,
	 * you get very close to transmitting back-to-back packets. With
	 * only one Tx buf, the transmitter sits idle while you reload the
	 * card, leaving a substantial gap between each transmitted packet.
	 */

	if (ei_local->tx1 == 0) 
	{
		output_page = ei_local->tx_start_page;
		ei_local->tx1 = send_length;
		if (ei_debug  &&  ei_local->tx2 > 0)
			printk(KERN_DEBUG "%s: idle transmitter tx2=%d, lasttx=%d, txing=%d.\n",
				dev->name, ei_local->tx2, ei_local->lasttx, ei_local->txing);
	}
	else if (ei_local->tx2 == 0) 
	{
		output_page = ei_local->tx_start_page + TX_1X_PAGES;
		ei_local->tx2 = send_length;
		if (ei_debug  &&  ei_local->tx1 > 0)
			printk(KERN_DEBUG "%s: idle transmitter, tx1=%d, lasttx=%d, txing=%d.\n",
				dev->name, ei_local->tx1, ei_local->lasttx, ei_local->txing);
	}
	else
	{	/* We should never get here. */
		if (ei_debug)
			printk(KERN_DEBUG "%s: No Tx buffers free! irq=%ld tx1=%d tx2=%d last=%d\n",
				dev->name, dev->interrupt, ei_local->tx1, ei_local->tx2, ei_local->lasttx);
		ei_local->irqlock = 0;
		dev->tbusy = 1;
		outb_p(ENISR_ALL, e8390_base + EN0_IMR);
		enable_irq(dev->irq);
		ei_local->stat.tx_errors++;
		return 1;
	}

	/*
	 * Okay, now upload the packet and trigger a send if the transmitter
	 * isn't already sending. If it is busy, the interrupt handler will
	 * trigger the send later, upon receiving a Tx done interrupt.
	 */

	ei_block_output(dev, length, skb->data, output_page);
	if (! ei_local->txing) 
	{
		ei_local->txing = 1;
		NS8390_trigger_send(dev, send_length, output_page);
		dev->trans_start = jiffies;
		if (output_page == ei_local->tx_start_page) 
		{
			ei_local->tx1 = -1;
			ei_local->lasttx = -1;
		}
		else 
		{
			ei_local->tx2 = -1;
			ei_local->lasttx = -2;
		}
	}
	else ei_local->txqueue++;

	dev->tbusy = (ei_local->tx1  &&  ei_local->tx2);

#else	/* EI_PINGPONG */

	/*
	 * Only one Tx buffer in use. You need two Tx bufs to come close to
	 * back-to-back transmits. Expect a 20 -> 25% performance hit on
	 * reasonable hardware if you only use one Tx buffer.
	 */

	ei_block_output(dev, length, skb->data, ei_local->tx_start_page);
	ei_local->txing = 1;
	NS8390_trigger_send(dev, send_length, ei_local->tx_start_page);
	dev->trans_start = jiffies;
	dev->tbusy = 1;

#endif	/* EI_PINGPONG */

	/* Turn 8390 interrupts back on. */
	ei_local->irqlock = 0;
	outb_p(ENISR_ALL, e8390_base + EN0_IMR);
	enable_irq(dev->irq);

	dev_kfree_skb (skb);
	ei_local->stat.tx_bytes += send_length;
    
	return 0;
}

/* The typical workload of the driver:
   Handle the ether interface interrupts. */

void ei_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct device *dev = dev_id;
	int e8390_base;
	int interrupts, nr_serviced = 0;
	struct ei_device *ei_local;
    
	if (dev == NULL) 
	{
		printk ("net_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}
    
	e8390_base = dev->base_addr;
	ei_local = (struct ei_device *) dev->priv;
	if (dev->interrupt || ei_local->irqlock) 
	{
#if 1 /* This might just be an interrupt for a PCI device sharing this line */
		/* The "irqlock" check is only for testing. */
		printk(ei_local->irqlock
			   ? "%s: Interrupted while interrupts are masked! isr=%#2x imr=%#2x.\n"
			   : "%s: Reentering the interrupt handler! isr=%#2x imr=%#2x.\n",
			   dev->name, inb_p(e8390_base + EN0_ISR),
			   inb_p(e8390_base + EN0_IMR));
#endif
		return;
	}
    
	dev->interrupt = 1;
    
	/* Change to page 0 and read the intr status reg. */
	outb_p(E8390_NODMA+E8390_PAGE0, e8390_base + E8390_CMD);
	if (ei_debug > 3)
		printk("%s: interrupt(isr=%#2.2x).\n", dev->name,
			   inb_p(e8390_base + EN0_ISR));
    
	/* !!Assumption!! -- we stay in page 0.	 Don't break this. */
	while ((interrupts = inb_p(e8390_base + EN0_ISR)) != 0
		   && ++nr_serviced < MAX_SERVICE) 
	{
		if (dev->start == 0) 
		{
			printk(KERN_WARNING "%s: interrupt from stopped card\n", dev->name);
			interrupts = 0;
			break;
		}
		if (interrupts & ENISR_OVER) 
			ei_rx_overrun(dev);
		else if (interrupts & (ENISR_RX+ENISR_RX_ERR)) 
		{
			/* Got a good (?) packet. */
			ei_receive(dev);
		}
		/* Push the next to-transmit packet through. */
		if (interrupts & ENISR_TX)
			ei_tx_intr(dev);
		else if (interrupts & ENISR_TX_ERR)
			ei_tx_err(dev);

		if (interrupts & ENISR_COUNTERS) 
		{
			ei_local->stat.rx_frame_errors += inb_p(e8390_base + EN0_COUNTER0);
			ei_local->stat.rx_crc_errors   += inb_p(e8390_base + EN0_COUNTER1);
			ei_local->stat.rx_missed_errors+= inb_p(e8390_base + EN0_COUNTER2);
			outb_p(ENISR_COUNTERS, e8390_base + EN0_ISR); /* Ack intr. */
		}
		
		/* Ignore any RDC interrupts that make it back to here. */
		if (interrupts & ENISR_RDC) 
		{
			outb_p(ENISR_RDC, e8390_base + EN0_ISR);
		}

		outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base + E8390_CMD);
	}
    
	if (interrupts && ei_debug) 
	{
		outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base + E8390_CMD);
		if (nr_serviced >= MAX_SERVICE) 
		{
			printk("%s: Too much work at interrupt, status %#2.2x\n",
				   dev->name, interrupts);
			outb_p(ENISR_ALL, e8390_base + EN0_ISR); /* Ack. most intrs. */
		} else {
			printk("%s: unknown interrupt %#2x\n", dev->name, interrupts);
			outb_p(0xff, e8390_base + EN0_ISR); /* Ack. all intrs. */
		}
	}
	dev->interrupt = 0;
	return;
}

/*
 * A transmitter error has happened. Most likely excess collisions (which
 * is a fairly normal condition). If the error is one where the Tx will
 * have been aborted, we try and send another one right away, instead of
 * letting the failed packet sit and collect dust in the Tx buffer. This
 * is a much better solution as it avoids kernel based Tx timeouts, and
 * an unnecessary card reset.
 */

static void ei_tx_err(struct device *dev)
{
	int e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) dev->priv;
	unsigned char txsr = inb_p(e8390_base+EN0_TSR);
	unsigned char tx_was_aborted = txsr & (ENTSR_ABT+ENTSR_FU);

#ifdef VERBOSE_ERROR_DUMP
	printk(KERN_DEBUG "%s: transmitter error (%#2x): ", dev->name, txsr);
	if (txsr & ENTSR_ABT)
		printk("excess-collisions ");
	if (txsr & ENTSR_ND)
		printk("non-deferral ");
	if (txsr & ENTSR_CRS)
		printk("lost-carrier ");
	if (txsr & ENTSR_FU)
		printk("FIFO-underrun ");
	if (txsr & ENTSR_CDH)
		printk("lost-heartbeat ");
	printk("\n");
#endif

	outb_p(ENISR_TX_ERR, e8390_base + EN0_ISR); /* Ack intr. */

	if (tx_was_aborted)
		ei_tx_intr(dev);
	else 
	{
		ei_local->stat.tx_errors++;
		if (txsr & ENTSR_CRS) ei_local->stat.tx_carrier_errors++;
		if (txsr & ENTSR_CDH) ei_local->stat.tx_heartbeat_errors++;
		if (txsr & ENTSR_OWC) ei_local->stat.tx_window_errors++;
	}
}

/* We have finished a transmit: check for errors and then trigger the next
   packet to be sent. */

static void ei_tx_intr(struct device *dev)
{
	int e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) dev->priv;
	int status = inb(e8390_base + EN0_TSR);
    
	outb_p(ENISR_TX, e8390_base + EN0_ISR); /* Ack intr. */

#ifdef EI_PINGPONG

	/*
	 * There are two Tx buffers, see which one finished, and trigger
	 * the send of another one if it exists.
	 */
	ei_local->txqueue--;

	if (ei_local->tx1 < 0) 
	{
		if (ei_local->lasttx != 1 && ei_local->lasttx != -1)
			printk(KERN_ERR "%s: bogus last_tx_buffer %d, tx1=%d.\n",
				ei_local->name, ei_local->lasttx, ei_local->tx1);
		ei_local->tx1 = 0;
		dev->tbusy = 0;
		if (ei_local->tx2 > 0) 
		{
			ei_local->txing = 1;
			NS8390_trigger_send(dev, ei_local->tx2, ei_local->tx_start_page + 6);
			dev->trans_start = jiffies;
			ei_local->tx2 = -1,
			ei_local->lasttx = 2;
		}
		else ei_local->lasttx = 20, ei_local->txing = 0;	
	}
	else if (ei_local->tx2 < 0) 
	{
		if (ei_local->lasttx != 2  &&  ei_local->lasttx != -2)
			printk("%s: bogus last_tx_buffer %d, tx2=%d.\n",
				ei_local->name, ei_local->lasttx, ei_local->tx2);
		ei_local->tx2 = 0;
		dev->tbusy = 0;
		if (ei_local->tx1 > 0) 
		{
			ei_local->txing = 1;
			NS8390_trigger_send(dev, ei_local->tx1, ei_local->tx_start_page);
			dev->trans_start = jiffies;
			ei_local->tx1 = -1;
			ei_local->lasttx = 1;
		}
		else
			ei_local->lasttx = 10, ei_local->txing = 0;
	}
	else printk(KERN_WARNING "%s: unexpected TX-done interrupt, lasttx=%d.\n",
			dev->name, ei_local->lasttx);

#else	/* EI_PINGPONG */
	/*
	 *  Single Tx buffer: mark it free so another packet can be loaded.
	 */
	ei_local->txing = 0;
	dev->tbusy = 0;
#endif

	/* Minimize Tx latency: update the statistics after we restart TXing. */
	if (status & ENTSR_COL)
		ei_local->stat.collisions++;
	if (status & ENTSR_PTX)
		ei_local->stat.tx_packets++;
	else 
	{
		ei_local->stat.tx_errors++;
		if (status & ENTSR_ABT) 
		{
			ei_local->stat.tx_aborted_errors++;
			ei_local->stat.collisions += 16;
		}
		if (status & ENTSR_CRS) 
			ei_local->stat.tx_carrier_errors++;
		if (status & ENTSR_FU) 
			ei_local->stat.tx_fifo_errors++;
		if (status & ENTSR_CDH)
			ei_local->stat.tx_heartbeat_errors++;
		if (status & ENTSR_OWC)
			ei_local->stat.tx_window_errors++;
	}
	mark_bh (NET_BH);
}

/* We have a good packet(s), get it/them out of the buffers. */

static void ei_receive(struct device *dev)
{
	int e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) dev->priv;
	unsigned char rxing_page, this_frame, next_frame;
	unsigned short current_offset;
	int rx_pkt_count = 0;
	struct e8390_pkt_hdr rx_frame;
	int num_rx_pages = ei_local->stop_page-ei_local->rx_start_page;
    
	while (++rx_pkt_count < 10) 
	{
		int pkt_len, pkt_stat;
		
		/* Get the rx page (incoming packet pointer). */
		outb_p(E8390_NODMA+E8390_PAGE1, e8390_base + E8390_CMD);
		rxing_page = inb_p(e8390_base + EN1_CURPAG);
		outb_p(E8390_NODMA+E8390_PAGE0, e8390_base + E8390_CMD);
		
		/* Remove one frame from the ring.  Boundary is always a page behind. */
		this_frame = inb_p(e8390_base + EN0_BOUNDARY) + 1;
		if (this_frame >= ei_local->stop_page)
			this_frame = ei_local->rx_start_page;
		
		/* Someday we'll omit the previous, iff we never get this message.
		   (There is at least one clone claimed to have a problem.)  */
		if (ei_debug > 0  &&  this_frame != ei_local->current_page)
			printk(KERN_ERR "%s: mismatched read page pointers %2x vs %2x.\n",
				   dev->name, this_frame, ei_local->current_page);
		
		if (this_frame == rxing_page)	/* Read all the frames? */
			break;				/* Done for now */
		
		current_offset = this_frame << 8;
		ei_get_8390_hdr(dev, &rx_frame, this_frame);
		
		pkt_len = rx_frame.count - sizeof(struct e8390_pkt_hdr);
		pkt_stat = rx_frame.status;
		
		next_frame = this_frame + 1 + ((pkt_len+4)>>8);
		
		/* Check for bogosity warned by 3c503 book: the status byte is never
		   written.  This happened a lot during testing! This code should be
		   cleaned up someday. */
		if (rx_frame.next != next_frame
			&& rx_frame.next != next_frame + 1
			&& rx_frame.next != next_frame - num_rx_pages
			&& rx_frame.next != next_frame + 1 - num_rx_pages) {
			ei_local->current_page = rxing_page;
			outb(ei_local->current_page-1, e8390_base+EN0_BOUNDARY);
			ei_local->stat.rx_errors++;
			continue;
		}

		if (pkt_len < 60  ||  pkt_len > 1518) 
		{
			if (ei_debug)
				printk(KERN_DEBUG "%s: bogus packet size: %d, status=%#2x nxpg=%#2x.\n",
					   dev->name, rx_frame.count, rx_frame.status,
					   rx_frame.next);
			ei_local->stat.rx_errors++;
			ei_local->stat.rx_length_errors++;
		}
		 else if ((pkt_stat & 0x0F) == ENRSR_RXOK) 
		{
			struct sk_buff *skb;
			
			skb = dev_alloc_skb(pkt_len+2);
			if (skb == NULL) 
			{
				if (ei_debug > 1)
					printk(KERN_DEBUG "%s: Couldn't allocate a sk_buff of size %d.\n",
						   dev->name, pkt_len);
				ei_local->stat.rx_dropped++;
				break;
			}
			else
			{
				skb_reserve(skb,2);	/* IP headers on 16 byte boundaries */
				skb->dev = dev;
				skb_put(skb, pkt_len);	/* Make room */
				ei_block_input(dev, pkt_len, skb, current_offset + sizeof(rx_frame));
				skb->protocol=eth_type_trans(skb,dev);
				netif_rx(skb);
				ei_local->stat.rx_packets++;
				ei_local->stat.rx_bytes += pkt_len;
				if (pkt_stat & ENRSR_PHY)
					ei_local->stat.multicast++;
			}
		} 
		else 
		{
			if (ei_debug)
				printk(KERN_DEBUG "%s: bogus packet: status=%#2x nxpg=%#2x size=%d\n",
					   dev->name, rx_frame.status, rx_frame.next,
					   rx_frame.count);
			ei_local->stat.rx_errors++;
			/* NB: The NIC counts CRC, frame and missed errors. */
			if (pkt_stat & ENRSR_FO)
				ei_local->stat.rx_fifo_errors++;
		}
		next_frame = rx_frame.next;
		
		/* This _should_ never happen: it's here for avoiding bad clones. */
		if (next_frame >= ei_local->stop_page) {
			printk("%s: next frame inconsistency, %#2x\n", dev->name,
				   next_frame);
			next_frame = ei_local->rx_start_page;
		}
		ei_local->current_page = next_frame;
		outb_p(next_frame-1, e8390_base+EN0_BOUNDARY);
	}

	/* We used to also ack ENISR_OVER here, but that would sometimes mask
	   a real overrun, leaving the 8390 in a stopped state with rec'vr off. */
	outb_p(ENISR_RX+ENISR_RX_ERR, e8390_base+EN0_ISR);
	return;
}

/* 
 * We have a receiver overrun: we have to kick the 8390 to get it started
 * again. Problem is that you have to kick it exactly as NS prescribes in
 * the updated datasheets, or "the NIC may act in an unpredictable manner."
 * This includes causing "the NIC to defer indefinitely when it is stopped
 * on a busy network."  Ugh.
 */

static void ei_rx_overrun(struct device *dev)
{
	int e8390_base = dev->base_addr;
	unsigned char was_txing, must_resend = 0;
	struct ei_device *ei_local = (struct ei_device *) dev->priv;
    
	/*
	 * Record whether a Tx was in progress and then issue the
	 * stop command.
	 */
	was_txing = inb_p(e8390_base+E8390_CMD) & E8390_TRANS;
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base+E8390_CMD);
    
	if (ei_debug > 1)
		printk(KERN_DEBUG "%s: Receiver overrun.\n", dev->name);
	ei_local->stat.rx_over_errors++;
    
	/* 
	 * Wait a full Tx time (1.2ms) + some guard time, NS says 1.6ms total.
	 * Early datasheets said to poll the reset bit, but now they say that
	 * it "is not a reliable indicator and subsequently should be ignored."
	 * We wait at least 10ms.
	 */

	udelay(10*1000);

	/*
	 * Reset RBCR[01] back to zero as per magic incantation.
	 */
	outb_p(0x00, e8390_base+EN0_RCNTLO);
	outb_p(0x00, e8390_base+EN0_RCNTHI);

	/*
	 * See if any Tx was interrupted or not. According to NS, this
	 * step is vital, and skipping it will cause no end of havoc.
	 */

	if (was_txing)
	{ 
		unsigned char tx_completed = inb_p(e8390_base+EN0_ISR) & (ENISR_TX+ENISR_TX_ERR);
		if (!tx_completed)
			must_resend = 1;
	}

	/*
	 * Have to enter loopback mode and then restart the NIC before
	 * you are allowed to slurp packets up off the ring.
	 */
	outb_p(E8390_TXOFF, e8390_base + EN0_TXCR);
	outb_p(E8390_NODMA + E8390_PAGE0 + E8390_START, e8390_base + E8390_CMD);

	/*
	 * Clear the Rx ring of all the debris, and ack the interrupt.
	 */
	ei_receive(dev);
	outb_p(ENISR_OVER, e8390_base+EN0_ISR);

	/*
	 * Leave loopback mode, and resend any packet that got stopped.
	 */
	outb_p(E8390_TXCONFIG, e8390_base + EN0_TXCR); 
	if (must_resend)
    		outb_p(E8390_NODMA + E8390_PAGE0 + E8390_START + E8390_TRANS, e8390_base + E8390_CMD);
}

static struct net_device_stats *get_stats(struct device *dev)
{
	int ioaddr = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) dev->priv;
    
	/* If the card is stopped, just return the present stats. */
	if (dev->start == 0) 
		return &ei_local->stat;

	/* Read the counter registers, assuming we are in page 0. */
	ei_local->stat.rx_frame_errors += inb_p(ioaddr + EN0_COUNTER0);
	ei_local->stat.rx_crc_errors   += inb_p(ioaddr + EN0_COUNTER1);
	ei_local->stat.rx_missed_errors+= inb_p(ioaddr + EN0_COUNTER2);
    
	return &ei_local->stat;
}

/*
 * Update the given Autodin II CRC value with another data byte.
 */

static inline u32 update_crc(u8 byte, u32 current_crc)
{
	int bit;
	u8 ah = 0;
	for (bit=0; bit<8; bit++) 
	{
		u8 carry = (current_crc>>31);
		current_crc <<= 1;
		ah = ((ah<<1) | carry) ^ byte;
		if (ah&1)
			current_crc ^= 0x04C11DB7;	/* CRC polynomial */
		ah >>= 1;
		byte >>= 1;
	}
	return current_crc;
}

/*
 * Form the 64 bit 8390 multicast table from the linked list of addresses
 * associated with this dev structure.
 */
 
static inline void make_mc_bits(u8 *bits, struct device *dev)
{
	struct dev_mc_list *dmi;

	for (dmi=dev->mc_list; dmi; dmi=dmi->next) 
	{
		int i;
		u32 crc;
		if (dmi->dmi_addrlen != ETH_ALEN) 
		{
			printk(KERN_INFO "%s: invalid multicast address length given.\n", dev->name);
			continue;
		}
		crc = 0xffffffff;	/* initial CRC value */
		for (i=0; i<ETH_ALEN; i++)
			crc = update_crc(dmi->dmi_addr[i], crc);
		/* 
		 * The 8390 uses the 6 most significant bits of the
		 * CRC to index the multicast table.
		 */
		bits[crc>>29] |= (1<<((crc>>26)&7));
	}
}

/*
 *	Set or clear the multicast filter for this adaptor.
 */
 
static void set_multicast_list(struct device *dev)
{
	int e8390_base = dev->base_addr;
	int i;
	unsigned long flags;
	struct ei_device *ei_local = (struct ei_device*)dev->priv;

	if (!(dev->flags&(IFF_PROMISC|IFF_ALLMULTI))) 
	{
		memset(ei_local->mcfilter, 0, 8);
		if (dev->mc_list)
			make_mc_bits(ei_local->mcfilter, dev);
	}
	else
		memset(ei_local->mcfilter, 0xFF, 8);	/* mcast set to accept-all */

	/* 
	 * DP8390 manuals don't specify any magic sequence for altering
	 * the multicast regs on an already running card. To be safe, we
	 * ensure multicast mode is off prior to loading up the new hash
	 * table. If this proves to be not enough, we can always resort
	 * to stopping the NIC, loading the table and then restarting.
	 *
	 * Bug Alert!  The MC regs on the SMC 83C690 (SMC Elite and SMC 
	 * Elite16) appear to be write-only. The NS 8390 data sheet lists
	 * them as r/w so this is a bug.  The SMC 83C790 (SMC Ultra and
	 * Ultra32 EISA) appears to have this bug fixed.
	 */
	if (dev->start)
		outb_p(E8390_RXCONFIG, e8390_base + EN0_RXCR);
	save_flags(flags);
	cli();
	outb_p(E8390_NODMA + E8390_PAGE1, e8390_base + E8390_CMD);
	for(i = 0; i < 8; i++) 
	{
		outb_p(ei_local->mcfilter[i], e8390_base + EN1_MULT_SHIFT(i));
#ifdef NOT_83C690
		if(inb_p(e8390_base + EN1_MULT_SHIFT(i))!=ei_local->mcfilter[i])
			printk(KERN_ERR "Multicast filter read/write mismap %d\n",i);
#endif
	}
	outb_p(E8390_NODMA + E8390_PAGE0, e8390_base + E8390_CMD);
	restore_flags(flags);

  	if(dev->flags&IFF_PROMISC)
  		outb_p(E8390_RXCONFIG | 0x18, e8390_base + EN0_RXCR);
	else if(dev->flags&IFF_ALLMULTI || dev->mc_list)
  		outb_p(E8390_RXCONFIG | 0x08, e8390_base + EN0_RXCR);
  	else
  		outb_p(E8390_RXCONFIG, e8390_base + EN0_RXCR);
}

/*
 * Initialize the rest of the 8390 device structure.  Do NOT __initfunc
 * this, as it is used by 8390 based modular drivers too.
 */
int ethdev_init(struct device *dev)
{
	if (ei_debug > 1)
		printk(version);
    
	if (dev->priv == NULL) 
	{
		struct ei_device *ei_local;
		
		dev->priv = kmalloc(sizeof(struct ei_device), GFP_KERNEL);
		if (dev->priv == NULL)
			return -ENOMEM;
		memset(dev->priv, 0, sizeof(struct ei_device));
		ei_local = (struct ei_device *)dev->priv;
	}
    
	dev->hard_start_xmit = &ei_start_xmit;
	dev->get_stats	= get_stats;
	dev->set_multicast_list = &set_multicast_list;

	ether_setup(dev);
        
	return 0;
}



/* This page of functions should be 8390 generic */
/* Follow National Semi's recommendations for initializing the "NIC". */

void NS8390_init(struct device *dev, int startp)
{
	int e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) dev->priv;
	int i;
	int endcfg = ei_local->word16 ? (0x48 | ENDCFG_WTS) : 0x48;
	unsigned long flags;
    
	if(sizeof(struct e8390_pkt_hdr)!=4)
    		panic("8390.c: header struct mispacked\n");    
	/* Follow National Semi's recommendations for initing the DP83902. */
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base+E8390_CMD); /* 0x21 */
	outb_p(endcfg, e8390_base + EN0_DCFG);	/* 0x48 or 0x49 */
	/* Clear the remote byte count registers. */
	outb_p(0x00,  e8390_base + EN0_RCNTLO);
	outb_p(0x00,  e8390_base + EN0_RCNTHI);
	/* Set to monitor and loopback mode -- this is vital!. */
	outb_p(E8390_RXOFF, e8390_base + EN0_RXCR); /* 0x20 */
	outb_p(E8390_TXOFF, e8390_base + EN0_TXCR); /* 0x02 */
	/* Set the transmit page and receive ring. */
	outb_p(ei_local->tx_start_page, e8390_base + EN0_TPSR);
	ei_local->tx1 = ei_local->tx2 = 0;
	outb_p(ei_local->rx_start_page, e8390_base + EN0_STARTPG);
	outb_p(ei_local->stop_page-1, e8390_base + EN0_BOUNDARY);	/* 3c503 says 0x3f,NS0x26*/
	ei_local->current_page = ei_local->rx_start_page;		/* assert boundary+1 */
	outb_p(ei_local->stop_page, e8390_base + EN0_STOPPG);
	/* Clear the pending interrupts and mask. */
	outb_p(0xFF, e8390_base + EN0_ISR);
	outb_p(0x00,  e8390_base + EN0_IMR);
    
	/* Copy the station address into the DS8390 registers. */
	save_flags(flags);
	cli();
	outb_p(E8390_NODMA + E8390_PAGE1 + E8390_STOP, e8390_base+E8390_CMD); /* 0x61 */
	for(i = 0; i < 6; i++) 
	{
		outb_p(dev->dev_addr[i], e8390_base + EN1_PHYS_SHIFT(i));
		if(inb_p(e8390_base + EN1_PHYS_SHIFT(i))!=dev->dev_addr[i])
			printk(KERN_ERR "Hw. address read/write mismap %d\n",i);
	}

	outb_p(ei_local->rx_start_page, e8390_base + EN1_CURPAG);
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base+E8390_CMD);
	restore_flags(flags);
	dev->tbusy = 0;
	dev->interrupt = 0;
	ei_local->tx1 = ei_local->tx2 = 0;
	ei_local->txing = 0;

	if (startp) 
	{
		outb_p(0xff,  e8390_base + EN0_ISR);
		outb_p(ENISR_ALL,  e8390_base + EN0_IMR);
		outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base+E8390_CMD);
		outb_p(E8390_TXCONFIG, e8390_base + EN0_TXCR); /* xmit on. */
		/* 3c503 TechMan says rxconfig only after the NIC is started. */
		outb_p(E8390_RXCONFIG, e8390_base + EN0_RXCR); /* rx on,  */
		set_multicast_list(dev);	/* (re)load the mcast table */
	}
	return;
}

/* Trigger a transmit start, assuming the length is valid. */
static void NS8390_trigger_send(struct device *dev, unsigned int length,
								int start_page)
{
	int e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) dev->priv;

	outb_p(E8390_NODMA+E8390_PAGE0, e8390_base+E8390_CMD);
    
	if (inb_p(e8390_base) & E8390_TRANS) 
	{
		printk(KERN_WARNING "%s: trigger_send() called with the transmitter busy.\n",
			dev->name);
		return;
	}
	outb_p(length & 0xff, e8390_base + EN0_TCNTLO);
	outb_p(length >> 8, e8390_base + EN0_TCNTHI);
	outb_p(start_page, e8390_base + EN0_TPSR);
	outb_p(E8390_NODMA+E8390_TRANS+E8390_START, e8390_base+E8390_CMD);
}

#ifdef MODULE

struct module *NS8390_module = NULL;

int init_module(void)
{
	NS8390_module = &__this_module;
	return 0;
}

void cleanup_module(void)
{
	NS8390_module = NULL;
}

#endif /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c 8390.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */
