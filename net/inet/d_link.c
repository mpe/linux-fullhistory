/*
 *	d_link.c
 *
 *	Portions (C) Copyright 1993 by Bjorn Ekwall
 *	The Author may be reached as bj0rn@blox.se
 *
 *	Linux driver for the D-Link Ethernet pocket adapter.
 *	Based on sources from linux 0.99pl5
 *	and on a sample network driver core for linux.
 *
 *	Sample driver written 1993 by Donald Becker <becker@super.org>
 *	C/O Supercomputing Research Ctr., 17100 Science Dr., Bowie MD 20715
 *
 *	compile-command:
 *	"gcc -DKERNEL -Wall -O6 -fomit-frame-pointer -c d_link.c"
 */
/*************************************************************
 * If you have trouble reading/writing to the adapter,
 * uncomment the following "#define":
#define REALLY_SLOW_IO
 */

/* $Id: d_link.c,v 0.21 1993/06/02 19:41:00 waltje Exp $ */
/* $Log: d_link.c,v $
 * Revision 0.21  1993/06/02  19:41:00 waltje
 * Applied multi-IRQ fix from Bjorn Ekwall and increaded version
 * number to 0.21.
 *
 * Revision 0.20  1993/03/26  11:43:53  root
 * Changed version number to indicate "alpha+" (almost beta :-)
 *
 * Revision 0.16  1993/03/26  11:26:46  root
 * Last ALPHA-minus version.
 * REALLY_SLOW_IO choice included (at line 20)
 * SLOW_DOWN_IO added anyway in convenience macros/functions
 * Test of D_LINK_FIFO included (not completely debugged)
 *
 * Revision 0.15  1993/03/24  14:00:49  root
 * Modified the interrupt handling considerably.
 * (The .asm source had me fooled in how it _really_ works :-)
 *
 * Revision 0.14  1993/03/21  01:57:25  root
 * Modified the interrupthandler for more robustness (hopefully :-)
 *
 * Revision 0.13  1993/03/19  11:45:09  root
 * Re-write of ALPHA release using Don Beckers skeleton (still works, kind of ...:-)
 *
 * Revision 0.12  1993/03/16  13:22:21  root
 * working ALPHA-release
 *
 */

static char *version =
	"d_link.c: $Revision: 0.21 $,  Bjorn Ekwall (bj0rn@blox.se)\n";

/*
 *	Based on adapter information gathered from DE600.ASM by D-Link Inc.,
 *	as included on disk C in the v.2.11 of PC/TCP from FTP Software.
 *
 *	For DE600.asm:
 *		Portions (C) Copyright 1990 D-Link, Inc.
 *		Copyright, 1988-1992, Russell Nelson, Crynwr Software
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2, or (at your option)
 *	any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <netinet/in.h>
#include <linux/ptrace.h>
#include <asm/system.h>
#include <errno.h>

#include "inet.h"
#include "dev.h"
#include "eth.h"
#include "ip.h"
#include "route.h"
#include "protocol.h"
#include "tcp.h"
#include "skbuff.h"
#include "sock.h"
#include "arp.h"


/* use 0 for production, 1 for verification, >2 for debug */
#ifndef D_LINK_DEBUG
#define D_LINK_DEBUG 0
#endif
static unsigned int d_link_debug = D_LINK_DEBUG;

#ifdef D_LINK_DEBUG
#define PRINTK(x) if (d_link_debug >= 2) printk x
#else
#define PRINTK(x) /**/
#endif

/**************************************************
 *                                                *
 * Definition of D-Link Ethernet Pocket adapter   *
 *                                                *
 **************************************************/
/*
 * D-Link Ethernet pocket adapter ports
 */
#define DATA_PORT	(dev->base_addr + 0)
#define STATUS_PORT	(dev->base_addr + 1)
#define COMMAND_PORT	(dev->base_addr + 2)

/*
 * D-Link COMMAND_PORT commands
 */
#define SELECT_NIC	0x04 /* select Network Interface Card */
#define SELECT_PRN	0x1c /* select Printer */
#define NML_PRN		0xec /* normal Printer situation */
#define IRQEN		0x10 /* enable IRQ line */

/*
 * D-Link STATUS_PORT
 */
#define TX_INTR		0x88
#define RX_INTR		0x40

#define OTHER_INTR	0x00 /* dummy, always false */

/*
 * D-Link DATA_PORT commands
 * command in low 4 bits
 * data in high 4 bits
 * select current data nibble with HI_NIBBLE bit
 */
#define WRITE_DATA	0x00 /* write memory */
#define READ_DATA	0x01 /* read memory */
#define STATUS		0x02 /* read  status register */
#define COMMAND		0x03 /* write command register (see COMMAND below) */
#define NULL_COMMAND	0x04 /* null command */
#define RX_LEN		0x05 /* read  received packet length */
#define TX_ADDR		0x06 /* set adapter transmit memory address */
#define RW_ADDR		0x07 /* set adapter read/write memory address */
#define HI_NIBBLE	0x08 /* read/write the high nibble of data,
				or-ed with rest of command */

/*
 * command register, (I don't know all about these bits...)
 * accessed through DATA_PORT with low bits = COMMAND
 */
#define RX_ALL		0x01 /* bit 0,1 = 01 */
#define RX_BP		0x02 /* bit 0,1 = 10 */
#define RX_MBP		0x03 /* bit 0,1 = 11 */

#define TX_ENABLE	0x04 /* bit 2 */
#define RX_ENABLE	0x08 /* bit 3 */

#define RESET		0x80 /* set bit 7 high */
#define STOP_RESET	0x00 /* set bit 7 low */

/*
 * data to command register
 * (high 4 bits in write to DATA_PORT)
 */
#define RX_PAGE2_SELECT	0x10 /* bit 4, only 2 pages to select */
#define RX_BASE_PAGE	0x20 /* bit 5, always set when specifying RX_ADDR */
#define FLIP_IRQ	0x40 /* bit 6 */

/* Convenience definition, transmitter page 2 */
#define TX_PAGE2_SELECT	0x02

/*
 * D-Link adapter internal memory:
 *
 * 0-2K 1:st transmit page (send from pointer up to 2K)
 * 2-4K	2:nd transmit page (send from pointer up to 4K)
 *
 * 4-6K 1:st receive page (data from 4K upwards)
 * 6-8K 2:nd receive page (data from 6K upwards)
 *
 * 8K+	Adapter ROM (contains magic code and last 3 bytes of Ethernet address)
 */
#define MEM_2K		0x0800 /* 2048 */
#define MEM_4K		0x1000 /* 4096 */
#define NODE_ADDRESS	0x2000 /* 8192 */

#define RUNT 64 /*56*/ /* Too small Ethernet packet */

/**************************************************
 *                                                *
 *             End of definition                  *
 *                                                *
 **************************************************/

/* Common network statistics -- these will be in *.h someday. */
struct netstats {
	int	tx_packets;
	int	rx_packets;
	int	tx_errors;
	int	rx_errors;
	int	missed_packets;
	int	soft_tx_errors;
	int	soft_rx_errors;
	int	soft_trx_err_bits;
};
static struct netstats	*localstats;

/*
 * Index to functions, as function prototypes.
 */

/* Routines used internally. (See "convenience macros" also) */
static int		d_link_read_status(struct device *dev);
static unsigned	char	d_link_read_byte(int type, struct device *dev);

/* Put in the device structure. */
static int	d_link_open(struct device *dev);
static int	d_link_close(struct device *dev);
static int	d_link_start_xmit(struct sk_buff *skb, struct device *dev);

/* Dispatch from interrupts. */
static void	d_link_interrupt(int reg_ptr);
static void	d_link_tx_intr(struct device *dev);
static void	d_link_rx_intr(struct device *dev);

/* Initialization */
int		d_link_init(struct device *dev);
static void	adapter_init(struct device *dev, int startp);

/* Passed to sigaction() to register the interrupt handler. */
static struct sigaction d_link_sigaction = {
	&d_link_interrupt,
	0,
	0,
	NULL,
	};

/*
 * D-Link driver variables:
 */
static volatile int		rx_page		= 0;
static struct device		*realdev;
#ifdef D_LINK_FIFO
static volatile int		free_tx_page = 0x03;	/* 2 pages = 0000 0011 */
static volatile unsigned int	busy_tx_page = 0x00;	/* 2 pages = 0000 0000 */
static volatile int		transmit_next_from;
#endif

/*
 * Convenience macros/functions for D-Link adapter
 *
 * If you are having trouble reading/writing correctly,
 * try to uncomment the line "#define REALLY_SLOW_IO" (near line 20)
 */

#define select_prn() \
	outb_p(SELECT_PRN, COMMAND_PORT)

#define select_nic() \
	outb_p(SELECT_NIC, COMMAND_PORT)

#define d_link_put_byte(data) \
	outb_p(((data) << 4)   | WRITE_DATA            , DATA_PORT); \
	outb_p(((data) & 0xf0) | WRITE_DATA | HI_NIBBLE, DATA_PORT)

/*
 * The first two outb_p()'s below could perhaps be deleted if there
 * would be more delay in the last two. Not certain about it yet...
 */
#define d_link_put_command(cmd) \
	outb_p(( rx_page        << 4)   | COMMAND            , DATA_PORT); \
	outb_p(( rx_page        & 0xf0) | COMMAND | HI_NIBBLE, DATA_PORT); \
	outb_p(((rx_page | cmd) << 4)   | COMMAND            , DATA_PORT); \
	outb_p(((rx_page | cmd) & 0xf0) | COMMAND | HI_NIBBLE, DATA_PORT)

#define d_link_setup_address(addr,type) \
	outb_p((((addr) << 4) & 0xf0) | type            , DATA_PORT); \
	outb_p(( (addr)       & 0xf0) | type | HI_NIBBLE, DATA_PORT); \
	outb_p((((addr) >> 4) & 0xf0) | type            , DATA_PORT); \
	outb_p((((addr) >> 8) & 0xf0) | type | HI_NIBBLE, DATA_PORT)

static int
d_link_read_status(struct device *dev)
{
	int	status;

	select_nic();
	outb_p(STATUS, DATA_PORT);
	SLOW_DOWN_IO; /* See comment line 20 */
	status = inb_p(STATUS_PORT);
	outb_p(NULL_COMMAND, DATA_PORT);
	outb_p(NULL_COMMAND | HI_NIBBLE, DATA_PORT);

	return status;
}

static unsigned char
d_link_read_byte(int type, struct device *dev) { /* dev used by macros */
	unsigned char	lo;

	outb_p((type), DATA_PORT);
	SLOW_DOWN_IO; /* See comment line 20 */
	lo = inb_p(STATUS_PORT);
	outb_p((type) | HI_NIBBLE, DATA_PORT);
	SLOW_DOWN_IO; /* See comment line 20 */
	return ((lo & 0xf0) >> 4) | (inb_p(STATUS_PORT) & 0xf0);
}

#ifdef D_LINK_FIFO
	/* Handle a "fifo" in an int (= busy_tx_page) */
# define AT_FIFO_OUTPUT (busy_tx_page & 0x0f)
# define ANY_QUEUED_IN_FIFO (busy_tx_page & 0xf0)

# define PULL_FROM_FIFO	{ busy_tx_page >>= 4;}
# define PUSH_INTO_FIFO(page) { \
	if (busy_tx_page)	/* there already is a transmit in progress */ \
		busy_tx_page |= (page << 4); \
	else \
		busy_tx_page = page; \
	}
#endif

/*
 * Open/initialize the board.  This is called (in the current kernel)
 * sometime after booting when the 'config <dev->name>' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is a non-reboot way to recover if something goes wrong.
 */
static int
d_link_open(struct device *dev)
{
	adapter_init(dev, 1);
	dev->tbusy = 0;		/* Transmit busy...  */
	dev->interrupt = 0;
	dev->start = 1;
	return 0;
}

/*
 * The inverse routine to d_link_open().
 */
static int
d_link_close(struct device *dev)
{
	dev->start = 0;

	adapter_init(dev, 0);

	irqaction(dev->irq, NULL);

	return 0;
}

/*
 * Copy a buffer to the adapter transmit page memory.
 * Start sending.
 */
static int
d_link_start_xmit(struct sk_buff *skb, struct device *dev)
{
	static int	tx_page = 0;
	int		transmit_from;
	int		len;
	int		tickssofar;
	unsigned char	*buffer = (unsigned char *)(skb + 1);

	/*
	 * If some higher layer thinks we've missed a
	 * tx-done interrupt * we are passed NULL.
	 * Caution: dev_tint() handles the cli()/sti() itself.
	 */

	if (skb == NULL) {
		dev_tint(dev);
		return 0;
	}

	/* For ethernet, fill in the header (hardware addresses) with an arp. */
	if (!skb->arp  &&  dev->rebuild_header(skb + 1, dev)) {
		skb->dev = dev;
		arp_queue (skb);
		return 0;
	}

#ifdef D_LINK_FIFO
	if (free_tx_page == 0) {	/* Do timeouts, to avoid hangs. */
#else
	if (dev->tbusy) {	/* Do timeouts, to avoid hangs. */
#endif
		tickssofar = jiffies - dev->trans_start;

		if (tickssofar < 5) {
			return 1;
		}

		/* else */
		printk("%s: transmit timed out (%d), %s?\n",
			dev->name,
			tickssofar,
			"network cable problem"
			);
		/* Try to restart the adapter. */
		/* Maybe in next release... :-)
		adapter_init(dev, 1);
		*/
	}

	/* Start real output */
	PRINTK(("d_link_start_xmit:len=%d\n", skb->len));
	cli();
	select_nic();

#ifdef D_LINK_FIFO
	/* magic code selects the least significant bit in free_tx_page */
	tx_page = free_tx_page & (-free_tx_page);

	free_tx_page &= ~tx_page;
	dev->tbusy = !free_tx_page; /* any more free pages? */

	PUSH_INTO_FIFO(tx_page);
#else
	tx_page ^= TX_PAGE2_SELECT;	/* Flip page, only 2 pages */
#endif

	if ((len = skb->len) < RUNT) /*&& Hmm...? */
		len = RUNT;

	if (tx_page & TX_PAGE2_SELECT)
		transmit_from = MEM_4K - len;
	else
		transmit_from = MEM_2K - len;

	d_link_setup_address(transmit_from, RW_ADDR);

	for ( ; len > 0; --len, ++buffer) {
		d_link_put_byte(*buffer); /* macro! watch out for side effects! */
	}

#ifdef D_LINK_FIFO
	if (ANY_QUEUED_IN_FIFO == 0)	{ /* there is no transmit in progress */
		d_link_setup_address(transmit_from, TX_ADDR);
		d_link_put_command(TX_ENABLE);
		dev->trans_start = jiffies;
	} else {
		transmit_next_from = transmit_from;
	}
#else
	d_link_setup_address(transmit_from, TX_ADDR);
	d_link_put_command(TX_ENABLE);
	dev->trans_start = jiffies;
#endif

	sti(); /* interrupts back on */
	
	if (skb->free) {
		kfree_skb (skb, FREE_WRITE);
	}

	return 0;
}

/*
 * The typical workload of the driver:
 * Handle the network interface interrupts.
 */
static void
d_link_interrupt(int reg_ptr)
{
	int		irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
	struct device	*dev = realdev;
	int		interrupts;

	/* Get corresponding device */
/*
	for (dev = dev_base; dev != NULL; dev = dev->next) {
		if (dev->irq == irq)
			break;
	}
*/
	if (dev == NULL) {
		printk ("d_link_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}

	if (dev->start == 0) {
		return; /*&& bogus interrupt at boot!?!?!? */
	}

	cli();
	dev->interrupt = 1;
	sti();	/* Allow other interrupts. */

	localstats = (struct netstats*) dev->priv;

	interrupts = d_link_read_status(dev);

	PRINTK(("d_link_interrupt (%2.2X)\n", interrupts));

	/*
	 * Interrupts have been observed to be:
	 *
	 * Value	My interpretation
	 * -----	-----------------
	 * 0x47		Normal receive interrupt
	 * 0x4F		Receive AND transmit interrupt simultaneously
	 * 0x87		Normal transmit interrupt (? I treat it as such...)
	 * 0x8F		Normal transmit interrupt
	 */

	/*
	 * Take care of TX interrupts first, in case there is an extra
         * page to transmit (keep the adapter busy while we work).
	 */
	if (interrupts & TX_INTR) { /* 1xxx 1xxx */
		d_link_tx_intr(dev);
	}

	if (interrupts & RX_INTR) { /* x1xx xxxx */
		d_link_rx_intr(dev);
	}

	/* I'm not sure if there are any other interrupts from D-Link... */
	if (d_link_debug && (interrupts & OTHER_INTR)) {
		printk("%s: unknown interrupt %#2x\n", dev->name, interrupts);
	}

	/* Check comment near line 20! */
	if (	(interrupts != 0x47) &&
		(interrupts != 0x87) &&
		(interrupts != 0x4F) &&
		(interrupts != 0x8F)
		)
		printk("Strange d_link_interrupt: <%2.2X>\n", interrupts);

	cli();
	dev->interrupt = 0;

	/* Enable adapter interrupts */
	select_prn();
	return;
}

/*
 * Do internal handshake: Transmitter done (of this page).
 * Also handle the case of a pending transmit page.
 */
static void
d_link_tx_intr(struct device *dev)
{
	localstats->tx_packets++;

	cli();
	dev->tbusy = 0;

#ifdef D_LINK_FIFO
	free_tx_page |= AT_FIFO_OUTPUT;
	PULL_FROM_FIFO;

	if (AT_FIFO_OUTPUT != 0) { /* more in queue! */
		d_link_setup_address(transmit_next_from, TX_ADDR);
		d_link_put_command(TX_ENABLE);
		dev->trans_start = jiffies;
	}
#endif

	mark_bh(INET_BH);
	sti();
}

/*
 * We have a good packet(s), get it/them out of the buffers.
 */
static void
d_link_rx_intr(struct device *dev)
{
	struct sk_buff	*skb;
	int		i;
	int		size;
	int		sksize;
	unsigned char	*buffer;

	/* Get size of received packet */
	/* Ignore trailing 4 CRC-bytes */
	size = d_link_read_byte(RX_LEN, dev); /* low byte */
	size = size + (d_link_read_byte(RX_LEN, dev) << 8) - 4;

	/* Tell adapter where to store next incoming packet, enable receiver */
	rx_page ^= RX_PAGE2_SELECT;	/* Flip bit, only 2 pages */
	d_link_put_command(RX_ENABLE);

	if (size == 0)		/* Read all the frames? */
		return;			/* Done for now */

	if ((size < 32  ||  size > 1535) && d_link_debug)
		printk("%s: Bogus packet size %d.\n", dev->name, size);

	sksize = sizeof(struct sk_buff) + size;
	if ((skb = (struct sk_buff *) kmalloc(sksize, GFP_ATOMIC)) == NULL) {
		if (d_link_debug) {
				printk("%s: Couldn't allocate a sk_buff of size %d.\n",
					dev->name, sksize);
		}
		return;
	}
	/* else */

	skb->lock = 0;
	skb->mem_len = sksize;
	skb->mem_addr = skb;
	/* 'skb + 1' points to the start of sk_buff data area. */
	buffer = (unsigned char *)(skb + 1);

	/* Get packet */

	/* Tell adapter from where we want to read this packet */
	if (rx_page & RX_PAGE2_SELECT) {
		d_link_setup_address(MEM_4K, RW_ADDR);
	} else {
		d_link_setup_address(MEM_4K + MEM_2K, RW_ADDR);
	}

	/* copy the packet into the buffer */
	for (i = size; i > 0; --i) {
		*buffer++ = d_link_read_byte(READ_DATA, dev);
	}
	
	localstats->rx_packets++; /* count all receives */

	if(dev_rint((unsigned char *)skb, size, IN_SKBUFF, dev)) {
		printk("%s: receive buffers full.\n", dev->name);
		return;
	}

	/*
	 * If any worth-while packets have been received, dev_rint()
	 * has done a mark_bh(INET_BH) for us and will work on them
	 * when we get to the bottom-half routine.
	 */
	return;
}

int
d_link_init(struct device *dev)
{
	int	i;

	printk("%s: D-Link pocket adapter", dev->name);
	/* Alpha testers must have the version number to report bugs. */
	if (d_link_debug > 1)
		printk(version);

	/* probe for adapter */
	rx_page = 0;
	(void)d_link_read_status(dev);
	d_link_put_command(RESET);
	d_link_put_command(STOP_RESET);
	if (d_link_read_status(dev) & 0xf0) {
		printk(": probe failed at %#3x.\n", dev->base_addr);
		return ENODEV;
	}

	/*
	 * Maybe we found one,
	 * have to check if it is a D-Link adapter...
	 */

	/* Get the adapter ethernet address from the ROM */
	d_link_setup_address(NODE_ADDRESS, RW_ADDR);
	for (i = 0; i < ETH_ALEN; i++) {
		dev->dev_addr[i] = d_link_read_byte(READ_DATA, dev);
		dev->broadcast[i] = 0xff;
	}

	/* Check magic code */
	if ((dev->dev_addr[1] == 0xde) && (dev->dev_addr[2] == 0x15)) {
		/* OK, install real address */
		dev->dev_addr[0] = 0x00;
		dev->dev_addr[1] = 0x80;
		dev->dev_addr[2] = 0xc8;
		dev->dev_addr[3] &= 0x0f;
		dev->dev_addr[3] |= 0x70;
	} else {
		printk(", not found in printer port!\n");
		return ENODEV;
	}

	/* Initialize the device structure. */
	dev->priv = kmalloc(sizeof(struct netstats), GFP_KERNEL);
	memset(dev->priv, 0, sizeof(struct netstats));

	for (i = 0; i < DEV_NUMBUFFS; i++)
		dev->buffs[i] = NULL;

	dev->hard_header = eth_header;
	dev->add_arp = eth_add_arp;
	dev->queue_xmit = dev_queue_xmit;
	dev->rebuild_header = eth_rebuild_header;
	dev->type_trans = eth_type_trans;

	dev->open = &d_link_open;
	dev->stop = &d_link_close;
	dev->hard_start_xmit = &d_link_start_xmit;

	/* These are ethernet specific. */
	dev->type = ARPHRD_ETHER;
	dev->hard_header_len = ETH_HLEN;
	dev->mtu = 1500; /* eth_mtu */
	dev->addr_len	= ETH_ALEN;

	/* New-style flags. */
	dev->flags = IFF_BROADCAST;
	dev->family = AF_INET;
	dev->pa_addr = 0;
	dev->pa_brdaddr = 0;
	dev->pa_mask = 0;
	dev->pa_alen = sizeof(unsigned long);

	if (irqaction (dev->irq, &d_link_sigaction)) {
		printk (": unable to get IRQ %d\n", dev->irq);
		return 0;
	}

	printk(", Ethernet Address: %2.2X", dev->dev_addr[0]);
	for (i = 1; i < ETH_ALEN; i++)
		printk(":%2.2X",dev->dev_addr[i]);
	printk("\n");

	return 0;
}

static void
adapter_init(struct device *dev, int startp)
{
	int	i;

	cli(); /* no interrupts yet, please */

	select_nic();
	rx_page = 0;
	d_link_put_command(RESET);
	d_link_put_command(STOP_RESET);

	if (startp) {
		irqaction (dev->irq, &d_link_sigaction);
		realdev = dev;
#ifdef D_LINK_FIFO
		free_tx_page = 0x03;	/* 2 pages = 0000 0011 */
		busy_tx_page = 0x00;	/* 2 pages = 0000 0000 */
#endif
		/* set the ether address. */
		d_link_setup_address(NODE_ADDRESS, RW_ADDR);
		for (i = 0; i < ETH_ALEN; i++) {
			d_link_put_byte(dev->dev_addr[i]);
		}

		/* where to start saving incoming packets */
		rx_page = 0 | RX_BP | RX_BASE_PAGE;
		d_link_setup_address(MEM_4K, RW_ADDR);
		/* Enable receiver */
		d_link_put_command(RX_ENABLE);
	}
	else
		d_link_put_command(0);

	select_prn();

	sti();
}
