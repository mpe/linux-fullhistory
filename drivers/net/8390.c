/* 8390.c: A general NS8390 ethernet driver core for linux. */
/*
  Written 1992,1993 by Donald Becker.
  
  Copyright 1993 United States Government as represented by the
  Director, National Security Agency.	 This software may be used and
  distributed according to the terms of the GNU Public License,
  incorporated herein by reference.
  
  This is the chip-specific code for many 8390-based ethernet adaptors.
  
  The Author may be reached as becker@super.org or
  C/O Supercomputing Research Ctr., 17100 Science Dr., Bowie MD 20715
  */

static char *version =
    "8390.c:v0.99-13f 10/18/93 Donald Becker (becker@super.org)\n";
#include <linux/config.h>

/*
  Braindamage remaining:
  
  Ethernet devices should use a chr_drv device interface, with ioctl()s to
  configure the card, bring the interface up or down, allow access to
  statistics, and maybe read() and write() access to raw packets.
  This won't be done until after Linux 1.00.
  
  Sources:
  The National Semiconductor LAN Databook, and the 3Com 3c503 databook.
  The NE* programming info came from the Crynwr packet driver, and figuring
  out that the those boards are similar to the NatSemi evaluation board
  described in AN-729.	Thanks NS, no thanks to Novell/Eagle.
  */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/interrupt.h>

#include "dev.h"
#include "eth.h"
#include "ip.h"
#include "protocol.h"
#include "tcp.h"
#include "skbuff.h"
#include "sock.h"
#include "arp.h"

#include "8390.h"

#ifndef HAVE_ALLOC_SKB
#define alloc_skb(size, priority) (struct sk_buff *) kmalloc(size,priority)
#define kfree_skbmem(addr, size) kfree_s(addr,size)
#endif

#define ei_reset_8390 (ei_local->reset_8390)
#define ei_block_output (ei_local->block_output)
#define ei_block_input (ei_local->block_input)

/* use 0 for production, 1 for verification, >2 for debug */
#ifdef EI_DEBUG
int ei_debug = EI_DEBUG;
#else
int ei_debug = 1;
#endif

/* Max number of packets received at one Intr. */
/*static int high_water_mark = 0;*/

/* Index to functions. */
/* Put in the device structure. */
int ei_open(struct device *dev);
/* Dispatch from interrupts. */
void ei_interrupt(int reg_ptr);
static void ei_tx_intr(struct device *dev);
static void ei_receive(struct device *dev);
static void ei_rx_overrun(struct device *dev);

/* Routines generic to NS8390-based boards. */
void NS8390_init(struct device *dev, int startp);
static void NS8390_trigger_send(struct device *dev, unsigned int length,
								int start_page);
#ifdef HAVE_MULTICAST
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);
#endif

struct sigaction ei_sigaction = { ei_interrupt, 0, 0, NULL, };

/* Open/initialize the board.  This routine goes all-out, setting everything
   up anew at each open, even though many of these registers should only
   need to be set once at boot.
   */
int ei_open(struct device *dev)
{
    struct ei_device *ei_local = (struct ei_device *) dev->priv;
    
    if ( ! ei_local) {
		printk("%s: Opening a non-existent physical device\n", dev->name);
		return 1;		/* ENXIO would be more accurate. */
    }
    
    irq2dev_map[dev->irq] = dev;
    NS8390_init(dev, 1);
    ei_local->tx1 = ei_local->tx2 = 0;
    /* The old local flags... */
    ei_local->txing = 0;
    /* ... are now global. */
    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;
    ei_local->irqlock = 0;
    return 0;
}

static int ei_start_xmit(struct sk_buff *skb, struct device *dev)
{
    int e8390_base = dev->base_addr;
    struct ei_device *ei_local = (struct ei_device *) dev->priv;
    int length, send_length;
    int tmp_tbusy;	  /* we must lock dev_tint in dev.c with dev->t_busy =1 */
    /* because on a slow pc a quasi endless loop can appear */
    
    if (dev->tbusy) {	/* Do timeouts, just like the 8003 driver. */
		int txsr = inb(e8390_base+EN0_TSR), isr;
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 5	||	(tickssofar < 15 && ! (txsr & ENTSR_PTX))) {
			return 1;
		}
		isr = inb(e8390_base+EN0_ISR);
		printk("%s: transmit timed out, TX status %#2x, ISR %#2x.\n",
			   dev->name, txsr, isr);
		/* It's possible to check for an IRQ conflict here.
		   I may have to do that someday. */
		if (isr)
			printk("%s: Possible IRQ conflict on IRQ%d?", dev->name, dev->irq);
		else
			printk("%s: Possible network cable problem?\n", dev->name);
		/* It futile, but try to restart it anyway. */
		ei_reset_8390(dev);
		NS8390_init(dev, 1);
		printk("\n");
    }
    
    /* This is new: it means some higher layer thinks we've missed an
       tx-done interrupt. Caution: dev_tint() handles the cli()/sti()
       itself. */
    if (skb == NULL) {
		dev_tint(dev);
		return 0;
    }
    /* Fill in the ethernet header. */
    if (!skb->arp  &&  dev->rebuild_header(skb->data, dev)) {
		skb->dev = dev;
		arp_queue (skb);
		return 0;
    }
    skb->arp=1;
    
    if (skb->len <= 0)
		return 0;
    length = skb->len;
    send_length = ETH_ZLEN < length ? length : ETH_ZLEN;
    /* Turn off interrupts so that we can put the packet out safely. */
    cli();
    if (dev->interrupt || ei_local->irqlock) {
		/* We should never get here during an interrupt after 0.99.4. */
		sti();
		if (ei_debug > 2)
			printk("%s: Attempt to reenter critical zone%s.\n",
				   dev->name, ei_local->irqlock ? " during interrupt" : "");
		return 1;
    }
    /* Mask interrupts from the ethercard. */
    outb(0x00,	e8390_base + EN0_IMR);
	
    /* Atomically lock out dev.c:dev_tint(). */
    tmp_tbusy = set_bit(0, (void*)&dev->tbusy);
	
    ei_local->irqlock = 1;
    sti();
    if (ei_local->pingpong) {
		int output_page;
		if (ei_local->tx1 == 0) {
			output_page = ei_local->tx_start_page;
			ei_local->tx1 = send_length;
			if (ei_debug  &&  ei_local->tx2 > 0)
				printk("%s: idle transmitter tx2=%d, lasttx=%d, txing=%d.\n",
					   dev->name, ei_local->tx2, ei_local->lasttx,
					   ei_local->txing);
		} else if (ei_local->tx2 == 0) {
			output_page = ei_local->tx_start_page + 6;
			ei_local->tx2 = send_length;
			if (ei_debug  &&  ei_local->tx1 > 0)
				printk("%s: idle transmitter, tx1=%d, lasttx=%d, txing=%d.\n",
					   dev->name, ei_local->tx1, ei_local->lasttx,
					   ei_local->txing);
		} else {
			/* We can get to here if we get an rx interrupt and queued
			   a tx packet just before masking 8390 irqs above. */
			if (ei_debug > 2)
				printk("%s: No packet buffer space for ping-pong use.\n",
					   dev->name);
			cli();
			ei_local->irqlock = 0;
			dev->tbusy = tmp_tbusy;
			outb_p(ENISR_ALL,  e8390_base + EN0_IMR);
			sti();
			return 1;
		}
		dev->trans_start = jiffies;
		ei_block_output(dev, length, skb->data, output_page);
		if (! ei_local->txing) {
			NS8390_trigger_send(dev, send_length, output_page);
			if (output_page == ei_local->tx_start_page)
				ei_local->tx1 = -1, ei_local->lasttx = -1;
			else
				ei_local->tx2 = -1, ei_local->lasttx = -2;
			ei_local->txing = 1;
		} else
			ei_local->txqueue++;
		if (ei_local->tx1  &&  ei_local->tx2)
			tmp_tbusy = 1;
    } else {
		dev->trans_start = jiffies;
		ei_block_output(dev, length, skb->data,
						ei_local->tx_start_page);
		NS8390_trigger_send(dev, send_length, ei_local->tx_start_page);
		tmp_tbusy = 1;
    }  /* PINGPONG */
    
    if (skb->free)
		kfree_skb (skb, FREE_WRITE);
    
    /* Turn 8390 interrupts back on. */
    cli();
    outb_p(ENISR_ALL,  e8390_base + EN0_IMR);
    ei_local->irqlock = 0;
    dev->tbusy=tmp_tbusy;
    sti();
    return 0;
}

/* The typical workload of the driver:
   Handle the ether interface interrupts. */
void ei_interrupt(int reg_ptr)
{
    int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
    struct device *dev = (struct device *)(irq2dev_map[irq]);
    int e8390_base;
    int interrupts, boguscount = 0;
    struct ei_device *ei_local;
    
    if (dev == NULL) {
		printk ("net_interrupt(): irq %d for unknown device.\n", irq);
		return;
    }
    e8390_base = dev->base_addr;
    ei_local = (struct ei_device *) dev->priv;
    if (dev->interrupt || ei_local->irqlock) {
		/* The "irqlock" check is only for testing. */
		sti();
		printk(ei_local->irqlock
			   ? "%s: Interrupted while interrupts are masked! isr=%#2x imr=%#2x.\n"
			   : "%s: Reentering the interrupt handler! isr=%#2x imr=%#2x.\n",
			   dev->name, inb_p(e8390_base + EN0_ISR),
			   inb_p(e8390_base + EN0_IMR));
		return;
    }
    
    dev->interrupt = 1;
    sti(); /* Allow other interrupts. */
    
    /* Change to page 0 and read the intr status reg. */
    outb_p(E8390_NODMA+E8390_PAGE0, e8390_base + E8390_CMD);
    if (ei_debug > 3)
		printk("%s: interrupt(isr=%#2.2x).\n", dev->name,
			   inb_p(e8390_base + EN0_ISR));
    
    /* !!Assumption!! -- we stay in page 0.	 Don't break this. */
    while ((interrupts = inb_p(e8390_base + EN0_ISR)) != 0
		   && ++boguscount < 20) {
		if (interrupts & ENISR_RDC) {
			/* Ack meaningless DMA complete. */
			outb_p(ENISR_RDC, e8390_base + EN0_ISR);
		}
		if (interrupts & ENISR_OVER) {
			ei_rx_overrun(dev);
		} else if (interrupts & (ENISR_RX+ENISR_RX_ERR)) {
			/* Got a good (?) packet. */
			ei_receive(dev);
		}
		/* Push the next to-transmit packet through. */
		if (interrupts & ENISR_TX) {
			ei_tx_intr(dev);
		} else if (interrupts & ENISR_COUNTERS) {
			struct ei_device *ei_local = (struct ei_device *) dev->priv;
			ei_local->stat.rx_frame_errors += inb_p(e8390_base + EN0_COUNTER0);
			ei_local->stat.rx_crc_errors   += inb_p(e8390_base + EN0_COUNTER1);
			ei_local->stat.rx_missed_errors+= inb_p(e8390_base + EN0_COUNTER2);
			outb_p(ENISR_COUNTERS, e8390_base + EN0_ISR); /* Ack intr. */
		}
		
		/* Ignore the transmit errs and reset intr for now. */
		if (interrupts & ENISR_TX_ERR) {
			outb_p(ENISR_TX_ERR, e8390_base + EN0_ISR); /* Ack intr. */
		}
		outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base + E8390_CMD);
    }
    
    if (interrupts && ei_debug) {
		printk("%s: unknown interrupt %#2x\n", dev->name, interrupts);
		outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base + E8390_CMD);
		outb_p(0xff, e8390_base + EN0_ISR); /* Ack. all intrs. */
    }
    dev->interrupt = 0;
    return;
}

/* We have finished a transmit: check for errors and then trigger the next
   packet to be sent. */
static void ei_tx_intr(struct device *dev)
{
    int e8390_base = dev->base_addr;
    int status = inb(e8390_base + EN0_TSR);
    struct ei_device *ei_local = (struct ei_device *) dev->priv;
    
    outb_p(ENISR_TX, e8390_base + EN0_ISR); /* Ack intr. */
    
    if (ei_local->pingpong) {
		ei_local->txqueue--;
		if (ei_local->tx1 < 0) {
			if (ei_local->lasttx != 1 && ei_local->lasttx != -1)
				printk("%s: bogus last_tx_buffer %d, tx1=%d.\n",
					   ei_local->name, ei_local->lasttx, ei_local->tx1);
			ei_local->tx1 = 0;
			dev->tbusy = 0;
			if (ei_local->tx2 > 0) {
				NS8390_trigger_send(dev, ei_local->tx2, ei_local->tx_start_page + 6);
				dev->trans_start = jiffies;
				ei_local->txing = 1;
				ei_local->tx2 = -1,
				ei_local->lasttx = 2;
			} else
				ei_local->lasttx = 20, ei_local->txing = 0;
		} else if (ei_local->tx2 < 0) {
			if (ei_local->lasttx != 2  &&  ei_local->lasttx != -2)
				printk("%s: bogus last_tx_buffer %d, tx2=%d.\n",
					   ei_local->name, ei_local->lasttx, ei_local->tx2);
			ei_local->tx2 = 0;
			dev->tbusy = 0;
			if (ei_local->tx1 > 0) {
				NS8390_trigger_send(dev, ei_local->tx1, ei_local->tx_start_page);
				dev->trans_start = jiffies;
				ei_local->txing = 1;
				ei_local->tx1 = -1;
				ei_local->lasttx = 1;
			} else
				ei_local->lasttx = 10, ei_local->txing = 0;
		} else
			printk("%s: unexpected TX-done interrupt, lasttx=%d.\n",
				   dev->name, ei_local->lasttx);
    } else {
		ei_local->txing = 0;
		dev->tbusy = 0;
    }
    
    /* Do the statistics _after_ we start the next TX. */
    if (status & ENTSR_PTX)
		ei_local->stat.tx_packets++;
    else
		ei_local->stat.tx_errors++;
    if (status & ENTSR_COL)
		ei_local->stat.collisions++;
    if (status & ENTSR_ABT)
		ei_local->stat.tx_aborted_errors++;
    if (status & ENTSR_CRS)
		ei_local->stat.tx_carrier_errors++;
    if (status & ENTSR_FU)
		ei_local->stat.tx_fifo_errors++;
    if (status & ENTSR_CDH)
		ei_local->stat.tx_heartbeat_errors++;
    if (status & ENTSR_OWC)
		ei_local->stat.tx_window_errors++;
    
    mark_bh (INET_BH);
}

/* We have a good packet(s), get it/them out of the buffers. */

static void ei_receive(struct device *dev)
{
    int e8390_base = dev->base_addr;
    struct ei_device *ei_local = (struct ei_device *) dev->priv;
    int rxing_page, this_frame, next_frame, current_offset;
    int boguscount = 0;
    struct e8390_pkt_hdr rx_frame;
    int num_rx_pages = ei_local->stop_page-ei_local->rx_start_page;
    
    while (++boguscount < 10) {
		int pkt_len;
		
		/* Get the rx page (incoming packet pointer). */
		outb_p(E8390_NODMA+E8390_PAGE1, e8390_base + E8390_CMD);
		rxing_page = inb_p(e8390_base + EN1_CURPAG);
		outb_p(E8390_NODMA+E8390_PAGE0, e8390_base + E8390_CMD);
		
		/* Remove one frame from the ring.  Boundary is alway a page behind. */
		this_frame = inb_p(e8390_base + EN0_BOUNDARY) + 1;
		if (this_frame >= ei_local->stop_page)
			this_frame = ei_local->rx_start_page;
		
		/* Someday we'll omit the previous step, iff we never get this message.*/
		if (ei_debug > 0	&&	this_frame != ei_local->current_page)
			printk("%s: mismatched read page pointers %2x vs %2x.\n",
				   dev->name, this_frame, ei_local->current_page);
		
		if (this_frame == rxing_page)	/* Read all the frames? */
			break;				/* Done for now */
		
		current_offset = this_frame << 8;
		ei_block_input(dev, sizeof(rx_frame), (char *)&rx_frame,
					   current_offset);
		
		pkt_len = rx_frame.count - sizeof(rx_frame);
		
		next_frame = this_frame + 1 + ((pkt_len+4)>>8);
		
		/* Check for bogosity warned by 3c503 book: the status byte is never
		   written.  This happened a lot during testing! This code should be
		   cleaned up someday. */
		if (	 rx_frame.next != next_frame
			&& rx_frame.next != next_frame + 1
			&& rx_frame.next != next_frame - num_rx_pages
			&& rx_frame.next != next_frame + 1 - num_rx_pages) {
#ifndef EI_DEBUG
			ei_local->current_page = rxing_page;
			outb(ei_local->current_page-1, e8390_base+EN0_BOUNDARY);
			continue;
#else
			static int last_rx_bogosity = -1;
			printk("%s: bogus packet header, status=%#2x nxpg=%#2x sz=%#x (at %#4x)\n",
				   dev->name, rx_frame.status, rx_frame.next, rx_frame.count,
				   current_offset);
			
			if (ei_local->stat.rx_packets != last_rx_bogosity) {
				/* Maybe we can avoid resetting the chip... empty the packet ring. */
				ei_local->current_page = rxing_page;
				printk("%s:	setting next frame to %#2x (nxt=%#2x, rx_frm.nx=%#2x rx_frm.stat=%#2x).\n",
					   dev->name, ei_local->current_page, next_frame,
					   rx_frame.next, rx_frame.status);
				last_rx_bogosity = ei_local->stat.rx_packets;
				outb(ei_local->current_page-1, e8390_base+EN0_BOUNDARY);
				continue;
			} else {
				/* Oh no Mr Bill! Last ditch error recovery. */
				printk("%s: recovery failed, resetting at packet #%d..",
					   dev->name, ei_local->stat.rx_packets);
				sti();
				ei_reset_8390(dev);
				NS8390_init(dev, 1);
				printk("restarting.\n");
				return;
			}
#endif	/* EI8390_NOCHECK */
		}
		
		if ((pkt_len < 46  ||  pkt_len > 1535) && ei_debug)
			printk("%s: bogus packet size, status=%#2x nxpg=%#2x size=%#x\n",
				   dev->name, rx_frame.status, rx_frame.next, rx_frame.count);
		if ((rx_frame.status & 0x0F) == ENRSR_RXOK) {
			int sksize = sizeof(struct sk_buff) + pkt_len;
			struct sk_buff *skb;
			
			skb = alloc_skb(sksize, GFP_ATOMIC);
			if (skb == NULL) {
				if (ei_debug)
					printk("%s: Couldn't allocate a sk_buff of size %d.\n",
						   dev->name, sksize);
				ei_local->stat.rx_dropped++;
				break;
			} else {
				skb->mem_len = sksize;
				skb->mem_addr = skb;
				skb->len = pkt_len;
				skb->dev = dev;
				
				/* 'skb->data' points to the start of sk_buff data area. */
				ei_block_input(dev, pkt_len, (char *) skb->data,
							   current_offset + sizeof(rx_frame));
#ifdef HAVE_NETIF_RX
				netif_rx(skb);
#else
				skb->lock = 0;
				if (dev_rint((unsigned char*)skb, pkt_len, IN_SKBUFF, dev)) {
					kfree_skbmem(skb, sksize);
					lp->stats.rx_dropped++;
					break;
				}
#endif
				ei_local->stat.rx_packets++;
			}
		} else {
			int errs = rx_frame.status;
			if (ei_debug)
				printk("%s: bogus packet, status=%#2x nxpg=%#2x size=%d\n",
					   dev->name, rx_frame.status, rx_frame.next, rx_frame.count);
			if (errs & ENRSR_FO)
				ei_local->stat.rx_fifo_errors++;
		}
		next_frame = rx_frame.next;
		
		/* This should never happen, it's here for debugging. */
		if (next_frame >= ei_local->stop_page) {
			printk("%s: next frame inconsistency, %#2x..", dev->name, next_frame);
			next_frame = ei_local->rx_start_page;
		}
		ei_local->current_page += 1 + ((pkt_len+4)>>8);
		ei_local->current_page = next_frame;
		outb(next_frame-1, e8390_base+EN0_BOUNDARY);
    }
    /* If any worth-while packets have been received, dev_rint()
       has done a mark_bh(INET_BH) for us and will work on them
       when we get to the bottom-half routine. */
    
    /* Bug alert!  Reset ENISR_OVER to avoid spurious overruns! */
    outb_p(ENISR_RX+ENISR_RX_ERR+ENISR_OVER, e8390_base+EN0_ISR);
    return;
}

/* We have a receiver overrun: we have to kick the 8390 to get it started
   again.*/
static void ei_rx_overrun(struct device *dev)
{
    int e8390_base = dev->base_addr;
    int reset_start_time = jiffies;
    struct ei_device *ei_local = (struct ei_device *) dev->priv;
    
    /* We should already be stopped and in page0.  Remove after testing. */
    outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base+E8390_CMD);
    
    if (ei_debug)
		printk("%s: Receiver overrun.\n", dev->name);
    ei_local->stat.rx_over_errors++;
    
    /* The we.c driver does dummy = inb_p( RBCR[01] ); at this point.
       It might mean something -- magic to speed up a reset?  A 8390 bug?*/
    
    /* Wait for reset in case the NIC is doing a tx or rx.	This could take up to
       1.5msec, but we have no way of timing something in that range.  The 'jiffies'
       are just a sanity check. */
    while ((inb_p(e8390_base+EN0_ISR) & ENISR_RESET) == 0)
		if (jiffies - reset_start_time > 1) {
			printk("%s: reset did not complete at ei_rx_overrun.\n",
				   dev->name);
			NS8390_init(dev, 1);
			return;
		};
    
    /* Remove packets right away. */
    ei_receive(dev);
    
    outb_p(0xff, e8390_base+EN0_ISR);
    /* Generic 8390 insns to start up again, same as in open_8390(). */
    outb_p(E8390_NODMA + E8390_PAGE0 + E8390_START, e8390_base + E8390_CMD);
    outb_p(E8390_TXCONFIG, e8390_base + EN0_TXCR); /* xmit on. */
}

static struct enet_statistics *get_stats(struct device *dev)
{
    short ioaddr = dev->base_addr;
    struct ei_device *ei_local = (struct ei_device *) dev->priv;
    
    /* Read the counter registers, assuming we are in page 0. */
    ei_local->stat.rx_frame_errors += inb_p(ioaddr + EN0_COUNTER0);
    ei_local->stat.rx_crc_errors   += inb_p(ioaddr + EN0_COUNTER1);
    ei_local->stat.rx_missed_errors+= inb_p(ioaddr + EN0_COUNTER2);
    
    return &ei_local->stat;
}

#ifdef HAVE_MULTICAST
/* Set or clear the multicast filter for this adaptor.
   num_addrs == -1	Promiscuous mode, receive all packets
   num_addrs == 0	Normal mode, clear multicast list
   num_addrs > 0	Multicast mode, receive normal and MC packets, and do
   .   				best-effort filtering.
   */
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
    short ioaddr = dev->base_addr;
    
    if (num_addrs > 0) {
		/* The multicast-accept list is initialized to accept-all, and we
		   rely on higher-level filtering for now. */
		outb_p(E8390_RXCONFIG | 0x08, ioaddr + EN0_RXCR);
    } else if (num_addrs < 0)
		outb_p(E8390_RXCONFIG | 0x10, ioaddr + EN0_RXCR);
    else
		outb_p(E8390_RXCONFIG, ioaddr + EN0_RXCR);
}
#endif

/* Initialize the rest of the 8390 device structure. */
int ethdev_init(struct device *dev)
{
    int i;
    
    if (ei_debug > 1)
		printk(version);
    
    if (dev->priv == NULL) {
		struct ei_device *ei_local;
		
		dev->priv = kmalloc(sizeof(struct ei_device), GFP_KERNEL);
		memset(dev->priv, 0, sizeof(struct ei_device));
		ei_local = (struct ei_device *)dev->priv;
#ifndef NO_PINGPONG
		ei_local->pingpong = 1;
#endif
    }
    
    /* The open call may be overridden by the card-specific code. */
    if (dev->open == NULL)
		dev->open = &ei_open;
    /* We should have a dev->stop entry also. */
    dev->hard_start_xmit = &ei_start_xmit;
    dev->get_stats	= get_stats;
#ifdef HAVE_MULTICAST
    dev->set_multicast_list = &set_multicast_list;
#endif
    
    for (i = 0; i < DEV_NUMBUFFS; i++)
		dev->buffs[i] = NULL;
    
    dev->hard_header	= eth_header;
    dev->add_arp		= eth_add_arp;
    dev->queue_xmit		= dev_queue_xmit;
    dev->rebuild_header	= eth_rebuild_header;
    dev->type_trans		= eth_type_trans;
    
    dev->type		= ARPHRD_ETHER;
    dev->hard_header_len = ETH_HLEN;
    dev->mtu		= 1500; /* eth_mtu */
    dev->addr_len	= ETH_ALEN;
    for (i = 0; i < ETH_ALEN; i++) {
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


/* This page of functions should be 8390 generic */
/* Follow National Semi's recommendations for initializing the "NIC". */
void NS8390_init(struct device *dev, int startp)
{
    int e8390_base = dev->base_addr;
    struct ei_device *ei_local = (struct ei_device *) dev->priv;
    int i;
    int endcfg = ei_local->word16 ? (0x48 | ENDCFG_WTS) : 0x48;
    
    /* Follow National Semi's recommendations for initing the DP83902. */
    outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base); /* 0x21 */
    outb_p(endcfg, e8390_base + EN0_DCFG);	/* 0x48 or 0x49 */
    /* Clear the remote byte count registers. */
    outb_p(0x00,  e8390_base + EN0_RCNTLO);
    outb_p(0x00,  e8390_base + EN0_RCNTHI);
    /* Set to monitor and loopback mode -- this is vital!. */
    outb_p(E8390_RXOFF, e8390_base + EN0_RXCR); /* 0x20 */
    outb_p(E8390_TXOFF, e8390_base + EN0_TXCR); /* 0x02 */
    /* Set the transmit page and receive ring. */
    outb_p(ei_local->tx_start_page,	 e8390_base + EN0_TPSR);
    ei_local->tx1 = ei_local->tx2 = 0;
    outb_p(ei_local->rx_start_page,	 e8390_base + EN0_STARTPG);
    outb_p(ei_local->stop_page-1, e8390_base + EN0_BOUNDARY); /* 3c503 says 0x3f,NS0x26*/
    ei_local->current_page = ei_local->rx_start_page;		/* assert boundary+1 */
    outb_p(ei_local->stop_page,	  e8390_base + EN0_STOPPG);
    /* Clear the pending interrupts and mask. */
    outb_p(0xFF, e8390_base + EN0_ISR);
    outb_p(0x00,  e8390_base + EN0_IMR);
    
    /* Copy the station address into the DS8390 registers,
       and set the multicast hash bitmap to receive all multicasts. */
    cli();
    outb_p(E8390_NODMA + E8390_PAGE1 + E8390_STOP, e8390_base); /* 0x61 */
    for(i = 0; i < 6; i++) {
		outb_p(dev->dev_addr[i], e8390_base + EN1_PHYS + i);
    }
    /* Initialize the multicast list to accept-all.  If we enable multicast
       the higher levels can do the filtering. */
    for(i = 0; i < 8; i++)
		outb_p(0xff, e8390_base + EN1_MULT + i);
    
    outb_p(ei_local->rx_start_page,	 e8390_base + EN1_CURPAG);
    outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base);
    sti();
    if (startp) {
		outb_p(0xff,  e8390_base + EN0_ISR);
		outb_p(ENISR_ALL,  e8390_base + EN0_IMR);
		outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base);
		outb_p(E8390_TXCONFIG, e8390_base + EN0_TXCR); /* xmit on. */
		/* 3c503 TechMan says rxconfig only after the NIC is started. */
		outb_p(E8390_RXCONFIG,	e8390_base + EN0_RXCR); /* rx on,  */
    }
    return;
}

/* Trigger a transmit start, assuming the length is valid. */
static void NS8390_trigger_send(struct device *dev, unsigned int length,
								int start_page)
{
    int e8390_base = dev->base_addr;
    
    ei_status.txing = 1;
    outb_p(E8390_NODMA+E8390_PAGE0, e8390_base);
    
    if (inb_p(e8390_base) & E8390_TRANS) {
		printk("%s: trigger_send() called with the transmitter busy.\n",
			   dev->name);
		return;
    }
    outb_p(length & 0xff, e8390_base + EN0_TCNTLO);
    outb_p(length >> 8, e8390_base + EN0_TCNTHI);
    outb_p(start_page, e8390_base + EN0_TPSR);
    outb_p(E8390_NODMA+E8390_TRANS+E8390_START, e8390_base);
    return;
}


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c 8390.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 4
 * End:
 */
