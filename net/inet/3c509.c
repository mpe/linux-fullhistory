/* el3.c: An 3c509 EtherLink3 ethernet driver for linux. */
/*
    Written 1993 by Donald Becker.

    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.  This software may be used and
    distributed according to the terms of the GNU Public License,
    incorporated herein by reference.
    
    This driver should work with the 3Com EtherLinkIII series.

    The author may be reached as becker@super.org or
    C/O Supercomputing Research Ctr., 17100 Science Dr., Bowie MD 20715
*/

static char *version = "el3.c: v0.02 8/13/93 becker@super.org\n";

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/in.h>
/*#include <asm/system.h>*/
#include <asm/io.h>
#ifndef port_read
#include "iow.h"
#endif

#include "dev.h"
#include "eth.h"
#include "skbuff.h"
#include "arp.h"

/* From auto_irq.c, should be in a *.h file. */
extern void autoirq_setup(int waittime);
extern int autoirq_report(int waittime);
extern struct device *irq2dev_map[16];

/* These should be in <asm/io.h>. */
#define port_read_l(port,buf,nr) \
__asm__("cld;rep;insl": :"d" (port),"D" (buf),"c" (nr):"cx","di")
#define port_write_l(port,buf,nr) \
__asm__("cld;rep;outsl": :"d" (port),"S" (buf),"c" (nr):"cx","si")


#ifdef EL3_DEBUG
int el3_debug = EL3_DEBUG;
#else
int el3_debug = 1;
#endif

/* Offsets from base I/O address. */
#define EL3_DATA 0x00
#define EL3_CMD 0x0e
#define EL3_STATUS 0x0e
#define ID_PORT 0x100
#define  EEPROM_READ 0x80

/* Register window 1 offsets, used in normal operation. */
#define TX_FREE 0x0C
#define TX_STATUS 0x0B
#define TX_FIFO 0x00
#define RX_STATUS 0x08
#define RX_FIFO 0x00

struct el3_private {
    struct enet_statistics stats;
};

static int el3_init(struct device *dev);
static int read_eeprom(int index);
static int el3_open(struct device *dev);
static int el3_start_xmit(struct sk_buff *skb, struct device *dev);
static void el3_interrupt(int reg_ptr);
static void update_stats(int addr, struct device *dev);
static struct enet_statistics *el3_get_stats(struct device *dev);
static int el3_rx(struct device *dev);
static int el3_close(struct device *dev);



int el3_probe(struct device *dev)
{
    short lrs_state = 0xff, i;
    unsigned short iobase = 0;

    /* Send the ID sequence to the ID_PORT. */
    outb(0x00, ID_PORT);
    outb(0x00, ID_PORT);
    for(i = 0; i < 255; i++) {
	outb(lrs_state, ID_PORT);
	lrs_state <<= 1;
	lrs_state = lrs_state & 0x100 ? lrs_state ^ 0xcf : lrs_state;
    }

    /* The current Space.c initialization makes it difficult to have more
       than one adaptor initialized.  Send me email if you have a need for
       multiple adaptors. */

    /* Read in EEPROM data.
       Only the highest address board will stay on-line. */

    {
	short *phys_addr = (short *)dev->dev_addr;
	phys_addr[0] = htons(read_eeprom(0));
	if (phys_addr[0] != 0x6000)
	    return 1;
	phys_addr[1] = htons(read_eeprom(1));
	phys_addr[2] = htons(read_eeprom(2));
    }

    iobase = read_eeprom(8);
    dev->irq = read_eeprom(9) >> 12;

    /* Activate the adaptor at the EEPROM location (if set), else 0x320. */

    if (iobase == 0x0000) {
	dev->base_addr = 0x320;
	outb(0xf2, ID_PORT);
    } else {
	dev->base_addr = 0x200 + ((iobase & 0x1f) << 4);
	outb(0xff, ID_PORT);
    }

    outw(0x0800, dev->base_addr + EL3_CMD);	 /* Window 0. */
    printk("%s: 3c509 at %#3.3x  key %4.4x iobase %4.4x.\n",
	   dev->name, dev->base_addr, inw(dev->base_addr), iobase);

    if (inw(dev->base_addr) == 0x6d50) {
	el3_init(dev);
	return 0;
    } else
	return -ENODEV;
}

static int
read_eeprom(int index)
{
    int timer, bit, word = 0;
    
    /* Issue read command, and pause for at least 162 us. for it to complete.
       Assume extra-fast 16Mhz bus. */
    outb(EEPROM_READ + index, ID_PORT);

    for (timer = 0; timer < 162*4 + 400; timer++)
	SLOW_DOWN_IO;

    for (bit = 15; bit >= 0; bit--)
	word = (word << 1) + (inb(ID_PORT) & 0x01);
	
    if (el3_debug > 3)
	printk("  3c509 EEPROM word %d %#4.4x.\n", index, word);

    return word;
}

static int
el3_init(struct device *dev)
{
    struct el3_private *lp;
    int ioaddr = dev->base_addr;
    int i;

    printk("%s: EL3 at %#3x, address", dev->name, ioaddr);

    /* Read in the station address. */
    for (i = 0; i < 6; i++)
	printk(" %2.2x", dev->dev_addr[i]);
    printk(", IRQ %d.\n", dev->irq);

    /* Make up a EL3-specific-data structure. */
    dev->priv = kmalloc(sizeof(struct el3_private), GFP_KERNEL);
    memset(dev->priv, 0, sizeof(struct el3_private));
    lp = (struct el3_private *)dev->priv;

    if (el3_debug > 1)
	printk(version);

    /* The EL3-specific entries in the device structure. */
    dev->open = &el3_open;
    dev->hard_start_xmit = &el3_start_xmit;
    dev->stop = &el3_close;
    dev->get_stats = &el3_get_stats;

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
el3_open(struct device *dev)
{
    int ioaddr = dev->base_addr;
    int i;

    if (request_irq(dev->irq, &el3_interrupt)) {
	return -EAGAIN;
    }

    if (el3_debug > 3)
	printk("%s: Opening, IRQ %d  status@%x %4.4x reg4 %4.4x.\n",
	       dev->name, dev->irq, ioaddr + EL3_STATUS,
	       inw(ioaddr + EL3_STATUS), inw(ioaddr + 4));
    outw(0x0800, ioaddr + EL3_CMD); /* Make certain we are in window 0. */

    /* This is probably unnecessary. */
    outw(0x0001, ioaddr + 4);

    outw((dev->irq << 12) | 0x0f00, ioaddr + 8);

    irq2dev_map[dev->irq] = dev;

    /* Set the station address in window 2 each time opened. */
    outw(0x0802, ioaddr + EL3_CMD);

    for (i = 0; i < 6; i++)
	outb(dev->dev_addr[i], ioaddr + i);

    outw(0x1000, ioaddr + EL3_CMD); /* Start the thinnet transceiver. */

    outw(0x8005, ioaddr + EL3_CMD); /* Accept b-case and phys addr only. */
    outw(0xA800, ioaddr + EL3_CMD); /* Turn on statistics. */
    outw(0x2000, ioaddr + EL3_CMD); /* Enable the receiver. */
    outw(0x4800, ioaddr + EL3_CMD); /* Enable transmitter. */
    outw(0x78ff, ioaddr + EL3_CMD); /* Allow all status bits to be seen. */
    outw(0x7098, ioaddr + EL3_CMD); /* Set interrupt mask. */

    /* Switch to register set 1 for normal use. */
    outw(0x0801, ioaddr + EL3_CMD);

    if (el3_debug > 3)
	printk("%s: Opened 3c509  IRQ %d  status %4.4x.\n",
	       dev->name, dev->irq, inw(ioaddr + EL3_STATUS));

    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;

    return 0;			/* Always succeed */
}

static int
el3_start_xmit(struct sk_buff *skb, struct device *dev)
{
    struct el3_private *lp = (struct el3_private *)dev->priv;
    int ioaddr = dev->base_addr;

    /* Transmitter timeout, serious problems. */
    if (dev->tbusy) {
	int tickssofar = jiffies - dev->trans_start;
	if (tickssofar < 10)
	    return 1;
	printk("%s: transmit timed out, tx_status %4.4x status %4.4x.\n",
	       dev->name, inb(ioaddr + TX_STATUS), inw(ioaddr + EL3_STATUS));
	dev->trans_start = jiffies;
	/* Issue TX_RESET and TX_START commands. */
	outw(0x5800, ioaddr + EL3_CMD);	/* TX_RESET */
	outw(0x4800, ioaddr + EL3_CMD);	/* TX_START */
	dev->tbusy = 0;
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

    if (el3_debug > 4) {
	printk("%s: el3_start_xmit(lenght = %d) called, status %4.4x.\n",
	       dev->name, skb->len, inw(ioaddr + EL3_STATUS));
    }

    if (inw(ioaddr + EL3_STATUS) & 0x0001) { /* IRQ line active, missed one. */
      printk("%s: Missed interrupt, status %4.4x.\n", dev->name,
	     inw(ioaddr + EL3_STATUS));
      outw(0x7800, ioaddr + EL3_CMD); /* Fake interrupt trigger. */
      outw(0x6899, ioaddr + EL3_CMD); /* Ack IRQ */
      outw(0x78ff, ioaddr + EL3_CMD); /* Allow all status bits to be seen. */
    }

    /* Avoid timer-based retransmission conflicts. */
    dev->tbusy=1;

    /* Put out the doubleword header... */
    outw(skb->len, ioaddr + TX_FIFO);
    outw(0x00, ioaddr + TX_FIFO);
    /* ... and the packet rounded to a doubleword. */
    port_write(ioaddr + TX_FIFO, (void *)(skb+1),
	       ((skb->len + 3) >> 1) & ~0x1);
    
    dev->trans_start = jiffies;
    if (skb->free)
	kfree_skb (skb, FREE_WRITE);
    
    if (inw(ioaddr + TX_FREE) > 1536) {
	dev->tbusy=0;
    } else
	/* Interrupt us when the FIFO has room for max-sized packet. */
	outw(0x9000 + 1536, ioaddr + EL3_CMD);

    if (el3_debug > 4)
	printk("        Finished queueing packet, FIFO room remaining %d.\n",
	       inw(ioaddr + TX_FREE));
    /* Clear the Tx status stack. */
    {
	short tx_status;
	int i = 4;

	while (--i > 0  &&  (tx_status = inb(ioaddr + TX_STATUS)) > 0) {
	    if (el3_debug > 5)
		printk("        Tx status %4.4x.\n", tx_status);
	    if (tx_status & 0x38) lp->stats.tx_aborted_errors++;
	    if (tx_status & 0x30) outw(0x5800, ioaddr + EL3_CMD);
	    if (tx_status & 0x3C) outw(0x4800, ioaddr + EL3_CMD);
	    outb(0x00, ioaddr + TX_STATUS); /* Pop the status stack. */
	}
    }
    return 0;
}

/* The EL3 interrupt handler. */
static void
el3_interrupt(int reg_ptr)
{
    int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
    struct device *dev = irq2dev_map[irq];
    int ioaddr, status;

    if (dev == NULL) {
	printk ("el3_interrupt(): irq %d for unknown device.\n", irq);
	return;
    }

    if (dev->interrupt)
	printk("%s: Re-entering the interrupt handler.\n", dev->name);
    dev->interrupt = 1;

    ioaddr = dev->base_addr;
    status = inw(ioaddr + EL3_STATUS);

    if (el3_debug > 4)
	printk("%s: interrupt, status %4.4x.\n", dev->name, status);
    
    while ((status = inw(ioaddr + EL3_STATUS)) & 0x01) {

	if (status & 0x08) {
	    if (el3_debug > 5)
		printk("    TX room bit was handled.\n");
	    /* There's room in the FIFO for a full-sized packet. */
	    outw(0x6808, ioaddr + EL3_CMD); /* Ack IRQ */
	    dev->tbusy = 0;
	    mark_bh(INET_BH);
	}
	if (status & 0x80)		/* Statistics full. */
	    update_stats(ioaddr, dev);
	
	if (status & 0x10)
	    el3_rx(dev);

	/* Clear the interrupts we've handled. */
	outw(0x6899, ioaddr + EL3_CMD); /* Ack IRQ */
    }

    if (el3_debug > 4) {
	printk("%s: exiting interrupt, status %4.4x.\n", dev->name,
	       inw(ioaddr + EL3_STATUS));
    }
    
    if (inw(ioaddr + EL3_STATUS) & 0x01) {
	int i = 100000;
	printk("%s: exiting interrupt with status %4.4x.\n", dev->name,
	       inw(ioaddr + EL3_STATUS));
	while (i--)		/* Delay loop to see the message. */
	    inw(ioaddr + EL3_STATUS);
	while ((inw(ioaddr + EL3_STATUS) & 0x0010)  && i++ < 20)
	    outw(0x00, ioaddr + RX_STATUS);
    }

    dev->interrupt = 0;
    return;
}


static struct enet_statistics *
el3_get_stats(struct device *dev)
{
    struct el3_private *lp = (struct el3_private *)dev->priv;

    sti();
    update_stats(dev->base_addr, dev);
    cli();
    return &lp->stats;
}

/* Update statistics.  We change to register window 6, so this
   must be run single-threaded. */
static void update_stats(int ioaddr, struct device *dev)
{
    struct el3_private *lp = (struct el3_private *)dev->priv;

    if (el3_debug > 5)
	printk("   Updating the statistics.\n");
    /* Turn off statistics updates while reading. */
    outw(0xB000, ioaddr + EL3_CMD);
    /* Switch to the stats window, and read everything. */
    outw(0x0806, ioaddr + EL3_CMD);
    lp->stats.tx_carrier_errors	+= inb(ioaddr + 0);
    lp->stats.tx_heartbeat_errors	+= inb(ioaddr + 1);
    /* Multiple collisions. */	   inb(ioaddr + 2);
    lp->stats.collisions		+= inb(ioaddr + 3);
    lp->stats.tx_window_errors	+= inb(ioaddr + 4);
    lp->stats.rx_fifo_errors	+= inb(ioaddr + 5);
    lp->stats.tx_packets		+= inb(ioaddr + 6);
    lp->stats.rx_packets		+= inb(ioaddr + 7);
    /* Tx deferrals */		   inb(ioaddr + 8);
    inw(ioaddr + 10);	/* Total Rx and Tx octets. */
    inw(ioaddr + 12);

    /* Back to window 1, and turn statistics back on. */
    outw(0x0801, ioaddr + EL3_CMD);
    outw(0xA800, ioaddr + EL3_CMD);
    return;
}

/* Print statistics on the kernel error output. */
void printk_stats(struct enet_statistics *stats)
{

    printk("  Ethernet statistics:  Rx packets %6d  Tx packets %6d.\n",
	   stats->rx_packets, stats->tx_packets);
    printk("   Carrier errors:   %6d.\n", stats->tx_carrier_errors);
    printk("   Heartbeat errors: %6d.\n", stats->tx_heartbeat_errors);
    printk("   Collisions:       %6d.\n", stats->collisions);
    printk("   Rx FIFO problems: %6d.\n", stats->rx_fifo_errors);

    return;
}

static int
el3_rx(struct device *dev)
{
    struct el3_private *lp = (struct el3_private *)dev->priv;
    int ioaddr = dev->base_addr;
    short rx_status;

    if (el3_debug > 5)
	printk("       In rx_packet(), status %4.4x, rx_status %4.4x.\n",
	       inw(ioaddr+EL3_STATUS), inw(ioaddr+RX_STATUS));
    while ((rx_status = inw(ioaddr + RX_STATUS)) > 0) {
	if (rx_status & 0x4000) { /* Error, update stats. */
	    short error = rx_status & 0x3C00;
	    lp->stats.rx_errors++;
	    switch (error) {
	    case 0x2000:	lp->stats.rx_over_errors++; break;
	    case 0x2C00:	lp->stats.rx_length_errors++; break;
	    case 0x3400:	lp->stats.rx_crc_errors++; break;
	    case 0x2400:	lp->stats.rx_length_errors++; break;
	    case 0x3000:	lp->stats.rx_frame_errors++; break;
	    case 0x0800:	lp->stats.rx_frame_errors++; break;
	    }
	}
	if ( (! (rx_status & 0x4000))
	    || ! (rx_status & 0x2000)) { /* Dribble bits are OK. */
	    short length = rx_status & 0x3ff;
	    int sksize = sizeof(struct sk_buff) + length + 3;
	    struct sk_buff *skb;
	    skb = (struct sk_buff *) kmalloc(sksize, GFP_ATOMIC);

	    if (el3_debug > 4)
		printk("       Receiving packet size %d status %4.4x.\n",
		       length, rx_status);
	    if (skb != NULL) {
		skb->lock = 0;
		skb->mem_len = sksize;
		skb->mem_addr = skb;
		/* 'skb+1' points to the start of sk_buff data area. */
		port_read(ioaddr+RX_FIFO, (void *)(skb+1), ((length + 3) >> 2) << 1);
		if (dev_rint((unsigned char *)skb, length, IN_SKBUFF,dev)== 0){
		    if (el3_debug > 6)
			printk("     dev_rint() happy, status %4.4x.\n",
			inb(ioaddr + EL3_STATUS));
		    outw(0x4000, ioaddr + EL3_CMD); /* Rx discard */
		    while (inw(ioaddr + EL3_STATUS) & 0x1000)
		      printk("  Waiting for 3c509 to discard packet, status %x.\n",
			     inw(ioaddr + EL3_STATUS) );
		    if (el3_debug > 6)
			printk("     discarded packet, status %4.4x.\n",
			inb(ioaddr + EL3_STATUS));
		    continue;
		} else {
		    printk("%s: receive buffers full.\n", dev->name);
		    kfree_s(skb, sksize);
		}	    
	    } else if (el3_debug)
		printk("%s: Couldn't allocate a sk_buff of size %d.\n",
		       dev->name, sksize);
	}
	lp->stats.rx_dropped++;
	outw(0x4000, ioaddr + EL3_CMD); /* Rx discard */
	while (inw(ioaddr + EL3_STATUS) & 0x1000)
	  printk("  Waiting for 3c509 to discard packet, status %x.\n",
		 inw(ioaddr + EL3_STATUS) );
    }

    if (el3_debug > 5)
	printk("       Exiting rx_packet(), status %4.4x, rx_status %4.4x.\n",
	       inw(ioaddr+EL3_STATUS), inw(ioaddr+8));

    return 0;
}

static int
el3_close(struct device *dev)
{
    int ioaddr = dev->base_addr;

    if (el3_debug > 2)
	printk("%s: Shutting down ethercard.\n", dev->name);

    dev->tbusy = 1;
    dev->start = 0;

    /* Turn off statistics.  We update lp->stats below. */
    outw(0xB000, ioaddr + EL3_CMD);

    /* Disable the receiver and transmitter. */
    outw(0x1800, ioaddr + EL3_CMD);
    outw(0x5000, ioaddr + EL3_CMD);

    /* Turn off thinnet power. */
    outw(0xb800, ioaddr + EL3_CMD);

    if (el3_debug > 2) {
	struct el3_private *lp = (struct el3_private *)dev->priv;
	printk("%s: Status was %4.4x.\n", dev->name, inw(ioaddr + EL3_STATUS));
	printk_stats(&lp->stats);
    }

    /* Free the interrupt line. */
    free_irq(dev->irq);
    outw(0x1000, ioaddr + EL3_CMD);
    outw(0x0f00, ioaddr + 8);

    /* Switch back to register window 0. */
    outw(0x0800, ioaddr + EL3_CMD);

    irq2dev_map[dev->irq] = 0;

    update_stats(ioaddr, dev);
    return 0;
}

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -Wall -O6 -x c++ -c 3c509.c"
 * End:
 */
