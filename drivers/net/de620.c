/*
 *	de620.c $Revision: 1.31 $ BETA
 *
 *
 *	Linux driver for the D-Link DE-620 Ethernet pocket adapter.
 *
 *	Portions (C) Copyright 1993, 1994 by Bjorn Ekwall <bj0rn@blox.se>
 *
 *	Based on adapter information gathered from DOS packetdriver
 *	sources from D-Link Inc:  (Special thanks to Henry Ngai of D-Link.)
 *		Portions (C) Copyright D-Link SYSTEM Inc. 1991, 1992
 *		Copyright, 1988, Russell Nelson, Crynwr Software
 *
 *	Adapted to the sample network driver core for linux,
 *	written by: Donald Becker <becker@super.org>
 *		(Now at <becker@cesdis.gsfc.nasa.gov>
 *
 *	Valuable assistance from:
 *		J. Joshua Kopper <kopper@rtsg.mot.com>
 *		Olav Kvittem <Olav.Kvittem@uninett.no>
 *		Germano Caronni <caronni@nessie.cs.id.ethz.ch>
 *		Jeremy Fitzhardinge <jeremy@suite.sw.oz.au>
 *
 *****************************************************************************/
/*
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
 *****************************************************************************/
static char *version =
	"de620.c: $Revision: 1.31 $,  Bjorn Ekwall <bj0rn@blox.se>\n";

/***********************************************************************
 *
 * "Tuning" section.
 *
 * Compile-time options: (see below for descriptions)
 * -DDE620_IO=0x378	(lpt1)
 * -DDE620_IRQ=7	(lpt1)
 * -DDE602_DEBUG=...
 * -DSHUTDOWN_WHEN_LOST
 * -DCOUNT_LOOPS
 * -DLOWSPEED
 * -DREAD_DELAY
 * -DWRITE_DELAY
 */

/*
 * If the adapter has problems with high speeds, enable this #define
 * otherwise full printerport speed will be attempted.
 *
 * You can tune the READ_DELAY/WRITE_DELAY below if you enable LOWSPEED
 *
#define LOWSPEED
 */

#ifndef READ_DELAY
#define READ_DELAY 100	/* adapter internal read delay in 100ns units */
#endif

#ifndef WRITE_DELAY
#define WRITE_DELAY 100	/* adapter internal write delay in 100ns units */
#endif

/*
 * Enable this #define if you want the adapter to do a "ifconfig down" on
 * itself when we have detected that something is possibly wrong with it.
 * The default behaviour is to retry with "adapter_init()" until success.
 * This should be used for debugging purposes only.
 *
#define SHUTDOWN_WHEN_LOST
 */

/*
 * Enable debugging by "-DDE620_DEBUG=3" when compiling,
 * OR in "./CONFIG"
 * OR by enabling the following #define
 *
 * use 0 for production, 1 for verification, >2 for debug
 *
#define DE620_DEBUG 3
 */

#ifdef LOWSPEED
/*
 * Enable this #define if you want to see debugging output that show how long
 * we have to wait before the DE-620 is ready for the next read/write/command.
 *
#define COUNT_LOOPS
 */
#endif
static int bnc, utp;
/*
 * Force media with insmod:
 *	insmod de620.o bnc=1
 * or
 *	insmod de620.o utp=1
 */

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/in.h>
#include <linux/ptrace.h>
#include <asm/system.h>
#include <linux/errno.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

/* Constant definitions for the DE-620 registers, commands and bits */
#include "de620.h"

#define netstats enet_statistics
typedef unsigned char byte;

/*******************************************************
 *                                                     *
 * Definition of D-Link DE-620 Ethernet Pocket adapter *
 * See also "de620.h"                                  *
 *                                                     *
 *******************************************************/
#ifndef DE620_IO /* Compile-time configurable */
#define DE620_IO 0x378
#endif

#ifndef DE620_IRQ /* Compile-time configurable */
#define DE620_IRQ	7
#endif

#define DATA_PORT	(DE620_IO)
#define STATUS_PORT	(DE620_IO + 1)
#define COMMAND_PORT	(DE620_IO + 2)

#define RUNT 60		/* Too small Ethernet packet */
#define GIANT 1514	/* largest legal size packet, no fcs */

#ifdef DE620_DEBUG /* Compile-time configurable */
#define PRINTK(x) if (de620_debug >= 2) printk x
#else
#define DE620_DEBUG 0
#define PRINTK(x) /**/
#endif

/***********************************************
 *                                             *
 * Index to functions, as function prototypes. *
 *                                             *
 ***********************************************/

/*
 * Routines used internally. (See also "convenience macros.. below")
 */

/* Put in the device structure. */
static int	de620_open(struct device *);
static int	de620_close(struct device *);
static struct netstats *get_stats(struct device *);
static void	de620_set_multicast_list(struct device *, int, void *);
static int	de620_start_xmit(struct sk_buff *, struct device *);

/* Dispatch from interrupts. */
static void	de620_interrupt(int, struct pt_regs *);
static int	de620_rx_intr(struct device *);

/* Initialization */
static int	adapter_init(struct device *);
int		de620_probe(struct device *);
static int	read_eeprom(void);


/*
 * D-Link driver variables:
 */
#define SCR_DEF NIBBLEMODE |INTON | SLEEP | AUTOTX
#define	TCR_DEF RXPB			/* not used: | TXSUCINT | T16INT */
#define DE620_RX_START_PAGE 12		/* 12 pages (=3k) reserved for tx */
#define DEF_NIC_CMD IRQEN | ICEN | DS1

unsigned int de620_debug = DE620_DEBUG;

static volatile byte	NIC_Cmd;
static volatile byte	next_rx_page;
static byte		first_rx_page;
static byte		last_rx_page;
static byte		EIPRegister;

static struct nic {
	byte	NodeID[6];
	byte	RAM_Size;
	byte	Model;
	byte	Media;
	byte	SCR;
} nic_data;

/**********************************************************
 *                                                        *
 * Convenience macros/functions for D-Link DE-620 adapter *
 *                                                        *
 **********************************************************/
#define de620_tx_buffs() (inb(STATUS_PORT) & (TXBF0 | TXBF1))
#define de620_flip_ds() NIC_Cmd ^= DS0 | DS1; outb(NIC_Cmd, COMMAND_PORT);

/* Check for ready-status, and return a nibble (high 4 bits) for data input */
#ifdef COUNT_LOOPS
static int tot_cnt;
#endif
static inline byte
de620_ready(void)
{
	byte value;
	register short int cnt = 0;

	while ((((value = inb(STATUS_PORT)) & READY) == 0) && (cnt <= 1000))
		++cnt;

#ifdef COUNT_LOOPS
	tot_cnt += cnt;
#endif
	return value & 0xf0; /* nibble */
}

static inline void
de620_send_command(byte cmd)
{
	de620_ready();
	if (cmd == W_DUMMY)
		outb(NIC_Cmd, COMMAND_PORT);

	outb(cmd, DATA_PORT);

	outb(NIC_Cmd ^ CS0, COMMAND_PORT);
	de620_ready();
	outb(NIC_Cmd, COMMAND_PORT);
}

static inline void
de620_put_byte(byte value)
{
	/* The de620_ready() makes 7 loops, on the average, on a DX2/66 */
	de620_ready();
	outb(value, DATA_PORT);
	de620_flip_ds();
}

static inline byte
de620_read_byte(void)
{
	byte value;

	/* The de620_ready() makes 7 loops, on the average, on a DX2/66 */
	value = de620_ready(); /* High nibble */
	de620_flip_ds();
	value |= de620_ready() >> 4; /* Low nibble */
	return value;
}

static inline void
de620_write_block(byte *buffer, int count)
{
#ifndef LOWSPEED
	byte uflip = NIC_Cmd ^ (DS0 | DS1);
	byte dflip = NIC_Cmd;
#else /* LOWSPEED */
#ifdef COUNT_LOOPS
	int bytes = count;
#endif /* COUNT_LOOPS */
#endif /* LOWSPEED */

#ifdef LOWSPEED
#ifdef COUNT_LOOPS
	tot_cnt = 0;
#endif /* COUNT_LOOPS */
	/* No further optimization useful, the limit is in the adapter. */
	for ( ; count > 0; --count, ++buffer) {
		de620_put_byte(*buffer);
	}
	de620_send_command(W_DUMMY);
#ifdef COUNT_LOOPS
	/* trial debug output: loops per byte in de620_ready() */
	printk("WRITE(%d)\n", tot_cnt/((bytes?bytes:1)));
#endif /* COUNT_LOOPS */
#else /* not LOWSPEED */
	for ( ; count > 0; count -=2) {
		outb(*buffer++, DATA_PORT);
		outb(uflip, COMMAND_PORT);
		outb(*buffer++, DATA_PORT);
		outb(dflip, COMMAND_PORT);
	}
	de620_send_command(W_DUMMY);
#endif /* LOWSPEED */
}

static inline void
de620_read_block(byte *data, int count)
{
#ifndef LOWSPEED
	byte value;
	byte uflip = NIC_Cmd ^ (DS0 | DS1);
	byte dflip = NIC_Cmd;
#else /* LOWSPEED */
#ifdef COUNT_LOOPS
	int bytes = count;

	tot_cnt = 0;
#endif /* COUNT_LOOPS */
#endif /* LOWSPEED */

#ifdef LOWSPEED
	/* No further optimization useful, the limit is in the adapter. */
	while (count-- > 0) {
		*data++ = de620_read_byte();
		de620_flip_ds();
	}
#ifdef COUNT_LOOPS
	/* trial debug output: loops per byte in de620_ready() */
	printk("READ(%d)\n", tot_cnt/(2*(bytes?bytes:1)));
#endif /* COUNT_LOOPS */
#else /* not LOWSPEED */
	while (count-- > 0) {
		value = inb(STATUS_PORT) & 0xf0; /* High nibble */
		outb(uflip, COMMAND_PORT);
		*data++ = value | inb(STATUS_PORT) >> 4; /* Low nibble */
		outb(dflip , COMMAND_PORT);
	}
#endif /* LOWSPEED */
}

static inline void
de620_set_delay(void)
{
	de620_ready();
	outb(W_DFR, DATA_PORT);
	outb(NIC_Cmd ^ CS0, COMMAND_PORT);

	de620_ready();
#ifdef LOWSPEED
	outb(WRITE_DELAY, DATA_PORT);
#else
	outb(0, DATA_PORT);
#endif
	de620_flip_ds();

	de620_ready();
#ifdef LOWSPEED
	outb(READ_DELAY, DATA_PORT);
#else
	outb(0, DATA_PORT);
#endif
	de620_flip_ds();
}

static inline void
de620_set_register(byte reg, byte value)
{
	de620_ready();
	outb(reg, DATA_PORT);
	outb(NIC_Cmd ^ CS0, COMMAND_PORT);

	de620_put_byte(value);
}

static inline byte
de620_get_register(byte reg)
{
	byte value;

	de620_send_command(reg);
	value = de620_read_byte();
	de620_send_command(W_DUMMY);

	return value;
}

/*********************************************************************
 *
 * Open/initialize the board.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is a non-reboot way to recover if something goes wrong.
 *
 */
static int
de620_open(struct device *dev)
{
	if (request_irq(DE620_IRQ, de620_interrupt, 0, "de620")) {
		printk ("%s: unable to get IRQ %d\n", dev->name, DE620_IRQ);
		return 1;
	}
	irq2dev_map[DE620_IRQ] = dev;

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
	if (adapter_init(dev)) {
		return 1;
	}
	dev->start = 1;
	return 0;
}

/************************************************
 *
 * The inverse routine to de620_open().
 *
 */
static int
de620_close(struct device *dev)
{
	/* disable recv */
	de620_set_register(W_TCR, RXOFF);

	free_irq(DE620_IRQ);
	irq2dev_map[DE620_IRQ] = NULL;

	dev->start = 0;
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}

/*********************************************
 *
 * Return current statistics
 *
 */
static struct netstats *
get_stats(struct device *dev)
{
	return (struct netstats *)(dev->priv);
}

/*********************************************
 *
 * Set or clear the multicast filter for this adaptor.
 * (no real multicast implemented for the DE-620, but she can be promiscuous...)
 *
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets, and do
 *			best-effort filtering.
 */
static void
de620_set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
	if (num_addrs) { /* Enable promiscuous mode */
		de620_set_register(W_TCR, (TCR_DEF & ~RXPBM) | RXALL);
	}
	else { /* Disable promiscuous mode, use normal mode */
		de620_set_register(W_TCR, TCR_DEF);
	}
}

/*******************************************************
 *
 * Copy a buffer to the adapter transmit page memory.
 * Start sending.
 */
static int
de620_start_xmit(struct sk_buff *skb, struct device *dev)
{
	unsigned long flags;
	int len;
	int tickssofar;
	byte *buffer = skb->data;
	byte using_txbuf;

	/*
	 * If some higher layer thinks we've missed a
	 * tx-done interrupt we are passed NULL.
	 * Caution: dev_tint() handles the cli()/sti() itself.
	 */

	if (skb == NULL) {
		dev_tint(dev);
		return 0;
	}

	using_txbuf = de620_tx_buffs(); /* Peek at the adapter */
	dev->tbusy = (using_txbuf == (TXBF0 | TXBF1)); /* Boolean! */

	if (dev->tbusy) {	/* Do timeouts, to avoid hangs. */
		tickssofar = jiffies - dev->trans_start;

		if (tickssofar < 5)
			return 1;

		/* else */
		printk("%s: transmit timed out (%d), %s?\n",
			dev->name,
			tickssofar,
			"network cable problem"
			);
		/* Restart the adapter. */
		if (adapter_init(dev)) /* maybe close it */
			return 1;
	}

	if ((len = skb->len) < RUNT)
		len = RUNT;
	if (len & 1) /* send an even number of bytes */
		++len;

	/* Start real output */
	save_flags(flags);
	cli();

	PRINTK(("de620_start_xmit: len=%d, bufs 0x%02x\n",
		(int)skb->len, using_txbuf));

	/* select a free tx buffer. if there is one... */
	switch (using_txbuf) {
	default: /* both are free: use TXBF0 */
	case TXBF1: /* use TXBF0 */
		de620_send_command(W_CR | RW0);
		using_txbuf |= TXBF0;
		break;

	case TXBF0: /* use TXBF1 */
		de620_send_command(W_CR | RW1);
		using_txbuf |= TXBF1;
		break;

	case (TXBF0 | TXBF1): /* NONE!!! */
		printk("de620: Ouch! No tx-buffer available!\n");
		restore_flags(flags);
		return 1;
		break;
	}
	de620_write_block(buffer, len);

	dev->trans_start = jiffies;
	dev->tbusy = (using_txbuf == (TXBF0 | TXBF1)); /* Boolean! */

	((struct netstats *)(dev->priv))->tx_packets++;
	
	restore_flags(flags); /* interrupts maybe back on */
	
	dev_kfree_skb (skb, FREE_WRITE);

	return 0;
}

/*****************************************************
 *
 * Handle the network interface interrupts.
 *
 */
static void
de620_interrupt(int irq, struct pt_regs *regs)
{
	struct device *dev = irq2dev_map[irq];
	byte irq_status;
	int bogus_count = 0;
	int again = 0;

	/* This might be deleted now, no crummy drivers present :-) Or..? */
	if ((dev == NULL) || (DE620_IRQ != irq)) {
		printk("%s: bogus interrupt %d\n", dev?dev->name:"DE620", irq);
		return;
	}

	cli();
	dev->interrupt = 1;

	/* Read the status register (_not_ the status port) */
	irq_status = de620_get_register(R_STS);

	PRINTK(("de620_interrupt (%2.2X)\n", irq_status));

	if (irq_status & RXGOOD) {
		do {
			again = de620_rx_intr(dev);
			PRINTK(("again=%d\n", again));
		}
		while (again && (++bogus_count < 100));
	}

	dev->tbusy = (de620_tx_buffs() == (TXBF0 | TXBF1)); /* Boolean! */

	dev->interrupt = 0;
	sti();
	return;
}

/**************************************
 *
 * Get a packet from the adapter
 *
 * Send it "upstairs"
 *
 */
static int
de620_rx_intr(struct device *dev)
{
	struct header_buf {
		byte		status;
		byte		Rx_NextPage;
		unsigned short	Rx_ByteCount;
	} header_buf;
	struct sk_buff *skb;
	int size;
	byte *buffer;
	byte pagelink;
	byte curr_page;

	PRINTK(("de620_rx_intr: next_rx_page = %d\n", next_rx_page));

	/* Tell the adapter that we are going to read data, and from where */
	de620_send_command(W_CR | RRN);
	de620_set_register(W_RSA1, next_rx_page);
	de620_set_register(W_RSA0, 0);

	/* Deep breath, and away we goooooo */
	de620_read_block((byte *)&header_buf, sizeof(struct header_buf));
	PRINTK(("page status=0x%02x, nextpage=%d, packetsize=%d\n",
	header_buf.status, header_buf.Rx_NextPage, header_buf.Rx_ByteCount));

	/* Plausible page header? */
	pagelink = header_buf.Rx_NextPage;
	if ((pagelink < first_rx_page) || (last_rx_page < pagelink)) {
		/* Ouch... Forget it! Skip all and start afresh... */
		printk("%s: Ring overrun? Restoring...\n", dev->name);
		/* You win some, you loose some. And sometimes plenty... */
		adapter_init(dev);
		((struct netstats *)(dev->priv))->rx_over_errors++;
		return 0;
	}

	/* OK, this look good, so far. Let's see if it's consistent... */
	/* Let's compute the start of the next packet, based on where we are */
	pagelink = next_rx_page +
		((header_buf.Rx_ByteCount + (4 - 1 + 0x100)) >> 8);

	/* Are we going to wrap around the page counter? */
	if (pagelink > last_rx_page)
		pagelink -= (last_rx_page - first_rx_page + 1);

	/* Is the _computed_ next page number equal to what the adapter says? */
	if (pagelink != header_buf.Rx_NextPage) {
		/* Naah, we'll skip this packet. Probably bogus data as well */
		printk("%s: Page link out of sync! Restoring...\n", dev->name);
		next_rx_page = header_buf.Rx_NextPage; /* at least a try... */
		de620_send_command(W_DUMMY);
		de620_set_register(W_NPRF, next_rx_page);
		((struct netstats *)(dev->priv))->rx_over_errors++;
		return 0;
	}
	next_rx_page = pagelink;

	size = header_buf.Rx_ByteCount - 4;
	if ((size < RUNT) || (GIANT < size)) {
		printk("%s: Illegal packet size: %d!\n", dev->name, size);
	}
	else { /* Good packet? */
		skb = alloc_skb(size, GFP_ATOMIC);
		if (skb == NULL) { /* Yeah, but no place to put it... */
			printk("%s: Couldn't allocate a sk_buff of size %d.\n",
				dev->name, size);
			((struct netstats *)(dev->priv))->rx_dropped++;
		}
		else { /* Yep! Go get it! */
			skb->len = size; skb->dev = dev; skb->free = 1;
			/* skb->data points to the start of sk_buff data area */
			buffer = skb->data;
			/* copy the packet into the buffer */
			de620_read_block(buffer, size);
			PRINTK(("Read %d bytes\n", size));
			netif_rx(skb); /* deliver it "upstairs" */
			/* count all receives */
			((struct netstats *)(dev->priv))->rx_packets++;
		}
	}

	/* Let's peek ahead to see if we have read the last current packet */
	/* NOTE! We're _not_ checking the 'EMPTY'-flag! This seems better... */
	curr_page = de620_get_register(R_CPR);
	de620_set_register(W_NPRF, next_rx_page);
	PRINTK(("next_rx_page=%d CPR=%d\n", next_rx_page, curr_page));

	return (next_rx_page != curr_page); /* That was slightly tricky... */
}

/*********************************************
 *
 * Reset the adapter to a known state
 *
 */
static int
adapter_init(struct device *dev)
{
	int i;
	static int was_down = 0;

	if ((nic_data.Model == 3) || (nic_data.Model == 0)) { /* CT */
		EIPRegister = NCTL0;
		if (nic_data.Media != 1)
			EIPRegister |= NIS0;	/* not BNC */
	}
	else if (nic_data.Model == 2) { /* UTP */
		EIPRegister = NCTL0 | NIS0;
	}

	if (utp)
		EIPRegister = NCTL0 | NIS0;
	if (bnc)
		EIPRegister = NCTL0;

	de620_send_command(W_CR | RNOP | CLEAR);
	de620_send_command(W_CR | RNOP);

	de620_set_register(W_SCR, SCR_DEF);
	/* disable recv to wait init */
	de620_set_register(W_TCR, RXOFF);

	/* Set the node ID in the adapter */
	for (i = 0; i < 6; ++i) { /* W_PARn = 0xaa + n */
		de620_set_register(W_PAR0 + i, dev->dev_addr[i]);
	}

	de620_set_register(W_EIP, EIPRegister);

	next_rx_page = first_rx_page = DE620_RX_START_PAGE;
	if (nic_data.RAM_Size)
		last_rx_page = nic_data.RAM_Size - 1;
	else /* 64k RAM */
		last_rx_page = 255;

	de620_set_register(W_SPR, first_rx_page); /* Start Page Register */
	de620_set_register(W_EPR, last_rx_page);  /* End Page Register */
	de620_set_register(W_CPR, first_rx_page); /* Current Page Register */
	de620_send_command(W_NPR | first_rx_page); /* Next Page Register */
	de620_send_command(W_DUMMY);
	de620_set_delay();

	/* Final sanity check: Anybody out there? */
	/* Let's hope some bits from the statusregister make a good check */
#define CHECK_MASK (  0 | TXSUC |  T16  |  0  | RXCRC | RXSHORT |  0  |  0  )
#define CHECK_OK   (  0 |   0   |  0    |  0  |   0   |   0     |  0  |  0  )
        /* success:   X     0      0       X      0       0        X     X  */
        /* ignore:   EEDI                RXGOOD                   COLS  LNKS*/

	if (((i = de620_get_register(R_STS)) & CHECK_MASK) != CHECK_OK) {
		printk("Something has happened to the DE-620!  Please check it"
#ifdef SHUTDOWN_WHEN_LOST
			" and do a new ifconfig"
#endif
			"! (%02x)\n", i);
#ifdef SHUTDOWN_WHEN_LOST
		/* Goodbye, cruel world... */
		dev->flags &= ~IFF_UP;
		de620_close(dev);
#endif
		was_down = 1;
		return 1; /* failed */
	}
	if (was_down) {
		printk("Thanks, I feel much better now!\n");
		was_down = 0;
	}

	/* All OK, go ahead... */
	de620_set_register(W_TCR, TCR_DEF);

	return 0; /* all ok */
}

/******************************************************************************
 *
 * Only start-up code below
 *
 */
/****************************************
 *
 * Check if there is a DE-620 connected
 */
int
de620_probe(struct device *dev)
{
	static struct netstats de620_netstats;
	int i;
	byte checkbyte = 0xa5;

	if (de620_debug)
		printk(version);

	printk("D-Link DE-620 pocket adapter");

	/* Initially, configure basic nibble mode, so we can read the EEPROM */
	NIC_Cmd = DEF_NIC_CMD;
	de620_set_register(W_EIP, EIPRegister);

	/* Anybody out there? */
	de620_set_register(W_CPR, checkbyte);
	checkbyte = de620_get_register(R_CPR);

	if ((checkbyte != 0xa5) || (read_eeprom() != 0)) {
		printk(" not identified in the printer port\n");
		return ENODEV;
	}

#if 0 /* Not yet */
	if (check_region(DE620_IO, 3)) {
		printk(", port 0x%x busy\n", DE620_IO);
		return EBUSY;
	}
#endif
	request_region(DE620_IO, 3, "de620");

	/* else, got it! */
	printk(", Ethernet Address: %2.2X",
		dev->dev_addr[0] = nic_data.NodeID[0]);
	for (i = 1; i < ETH_ALEN; i++) {
		printk(":%2.2X", dev->dev_addr[i] = nic_data.NodeID[i]);
		dev->broadcast[i] = 0xff;
	}

	printk(" (%dk RAM,",
		(nic_data.RAM_Size) ? (nic_data.RAM_Size >> 2) : 64);

	if (nic_data.Media == 1)
		printk(" BNC)\n");
	else
		printk(" UTP)\n");

	/* Initialize the device structure. */
	/*dev->priv = kmalloc(sizeof(struct netstats), GFP_KERNEL);*/
	dev->priv = &de620_netstats;

	memset(dev->priv, 0, sizeof(struct netstats));
	dev->get_stats = get_stats;
	dev->open = de620_open;
	dev->stop = de620_close;
	dev->hard_start_xmit = &de620_start_xmit;
	dev->set_multicast_list = &de620_set_multicast_list;
	dev->base_addr = DE620_IO;
	dev->irq = DE620_IRQ;

	ether_setup(dev);
	
	/* dump eeprom */
	if (de620_debug) {
		printk("\nEEPROM contents:\n");
		printk("RAM_Size = 0x%02X\n", nic_data.RAM_Size);
		printk("NodeID = %02X:%02X:%02X:%02X:%02X:%02X\n",
			nic_data.NodeID[0], nic_data.NodeID[1],
			nic_data.NodeID[2], nic_data.NodeID[3],
			nic_data.NodeID[4], nic_data.NodeID[5]);
		printk("Model = %d\n", nic_data.Model);
		printk("Media = %d\n", nic_data.Media);
		printk("SCR = 0x%02x\n", nic_data.SCR);
	}

	return 0;
}

/**********************************
 *
 * Read info from on-board EEPROM
 *
 * Note: Bitwise serial I/O to/from the EEPROM vi the status _register_!
 */
#define sendit(data) de620_set_register(W_EIP, data | EIPRegister);

static unsigned short
ReadAWord(int from)
{
	unsigned short data;
	int nbits;

	/* cs   [__~~] SET SEND STATE */
	/* di   [____]                */
	/* sck  [_~~_]                */
	sendit(0); sendit(1); sendit(5); sendit(4);

	/* Send the 9-bit address from where we want to read the 16-bit word */
	for (nbits = 9; nbits > 0; --nbits, from <<= 1) {
		if (from & 0x0100) { /* bit set? */
			/* cs    [~~~~] SEND 1 */
			/* di    [~~~~]        */
			/* sck   [_~~_]        */
			sendit(6); sendit(7); sendit(7); sendit(6);
		}
		else {
			/* cs    [~~~~] SEND 0 */
			/* di    [____]        */
			/* sck   [_~~_]        */
			sendit(4); sendit(5); sendit(5); sendit(4);
		}
	}

	/* Shift in the 16-bit word. The bits appear serially in EEDI (=0x80) */
	for (data = 0, nbits = 16; nbits > 0; --nbits) {
		/* cs    [~~~~] SEND 0 */
		/* di    [____]        */
		/* sck   [_~~_]        */
		sendit(4); sendit(5); sendit(5); sendit(4);
		data = (data << 1) | ((de620_get_register(R_STS) & EEDI) >> 7);
	}
	/* cs    [____] RESET SEND STATE */
	/* di    [____]                  */
	/* sck   [_~~_]                  */
	sendit(0); sendit(1); sendit(1); sendit(0);

	return data;
}

static int
read_eeprom(void)
{
	unsigned short wrd;

	/* D-Link Ethernet addresses are in the series  00:80:c8:7X:XX:XX:XX */
	wrd = ReadAWord(0x1aa);	/* bytes 0 + 1 of NodeID */
	if (wrd != htons(0x0080)) /* Valid D-Link ether sequence? */
		return -1; /* Nope, not a DE-620 */
	nic_data.NodeID[0] = wrd & 0xff;
	nic_data.NodeID[1] = wrd >> 8;

	wrd = ReadAWord(0x1ab);	/* bytes 2 + 3 of NodeID */
	if ((wrd & 0xff) != 0xc8) /* Valid D-Link ether sequence? */
		return -1; /* Nope, not a DE-620 */
	nic_data.NodeID[2] = wrd & 0xff;
	nic_data.NodeID[3] = wrd >> 8;

	wrd = ReadAWord(0x1ac);	/* bytes 4 + 5 of NodeID */
	nic_data.NodeID[4] = wrd & 0xff;
	nic_data.NodeID[5] = wrd >> 8;

	wrd = ReadAWord(0x1ad);	/* RAM size in pages (256 bytes). 0 = 64k */
	nic_data.RAM_Size = (wrd >> 8);

	wrd = ReadAWord(0x1ae);	/* hardware model (CT = 3) */
	nic_data.Model = (wrd & 0xff);

	wrd = ReadAWord(0x1af); /* media (indicates BNC/UTP) */
	nic_data.Media = (wrd & 0xff);

	wrd = ReadAWord(0x1a8); /* System Configuration Register */
	nic_data.SCR = (wrd >> 8);

	return 0; /* no errors */
}

/******************************************************************************
 *
 * Loadable module skeleton
 *
 */
#ifdef MODULE
char kernel_version[] = UTS_RELEASE;
static char nullname[8];
static struct device de620_dev = {
	nullname, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, de620_probe };

int
init_module(void)
{
	if (register_netdev(&de620_dev) != 0)
		return -EIO;
	return 0;
}

void
cleanup_module(void)
{
	unregister_netdev(&de620_dev);
	release_region(DE620_IO, 3);
}
#endif /* MODULE */

/*
 * (add '-DMODULE' when compiling as loadable module)
 *
 * compile-command:
 *	gcc -D__KERNEL__ -Wall -Wstrict-prototypes -O2 \
 *	 -fomit-frame-pointer -m486 \
 *	-I/usr/src/linux/include -I../../net/inet -c de620.c
 */
