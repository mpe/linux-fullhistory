/*
 * linux/arch/arm/drivers/net/ether1.c
 *
 * (C) Copyright 1996,1997,1998 Russell King
 *
 * Acorn ether1 driver (82586 chip)
 *  for Acorn machines
 */

/*
 * We basically keep two queues in the cards memory - one for transmit
 * and one for receive.  Each has a head and a tail.  The head is where
 * we/the chip adds packets to be transmitted/received, and the tail
 * is where the transmitter has got to/where the receiver will stop.
 * Both of these queues are circular, and since the chip is running
 * all the time, we have to be careful when we modify the pointers etc
 * so that the buffer memory contents is valid all the time.
 */

/*
 * Change log:
 * 1.00	RMK			Released
 * 1.01	RMK	19/03/1996	Transfers the last odd byte onto/off of the card now.
 * 1.02	RMK	25/05/1997	Added code to restart RU if it goes not ready
 * 1.03	RMK	14/09/1997	Cleaned up the handling of a reset during the TX interrupt.
 *				Should prevent lockup.
 * 1.04 RMK	17/09/1997	Added more info when initialsation of chip goes wrong.
 *				TDR now only reports failure when chip reports non-zero
 *				TDR time-distance.
 * 1.05	RMK	31/12/1997	Removed calls to dev_tint for 2.1
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/ecard.h>

#define __ETHER1_C
#include "ether1.h"

static unsigned int net_debug = NET_DEBUG;

#define BUFFER_SIZE	0x10000
#define TX_AREA_START	0x00100
#define TX_AREA_END	0x05000
#define RX_AREA_START	0x05000
#define RX_AREA_END	0x0fc00

#define tx_done(dev) 0
/* ------------------------------------------------------------------------- */
static char *version = "ether1 ethernet driver (c) 1995 Russell King v1.05\n";

#define BUS_16 16
#define BUS_8  8

static const card_ids ether1_cids[] = {
	{ MANU_ACORN, PROD_ACORN_ETHER1 },
	{ 0xffff, 0xffff }
};

/* ------------------------------------------------------------------------- */

#define DISABLEIRQS 1
#define NORMALIRQS  0

#define ether1_inw(dev, addr, type, offset, svflgs) ether1_inw_p (dev, addr + (int)(&((type *)0)->offset), svflgs)
#define ether1_outw(dev, val, addr, type, offset, svflgs) ether1_outw_p (dev, val, addr + (int)(&((type *)0)->offset), svflgs)

static inline unsigned short
ether1_inw_p (struct device *dev, int addr, int svflgs)
{
	unsigned long flags;
	unsigned short ret;

	if (svflgs) {
		save_flags_cli (flags);
	}
	outb (addr >> 12, REG_PAGE);
	ret = inw (ETHER1_RAM + ((addr & 4095) >> 1));
	if (svflgs)
		restore_flags (flags);
	return ret;
}

static inline void
ether1_outw_p (struct device *dev, unsigned short val, int addr, int svflgs)
{
	unsigned long flags;

	if (svflgs) {
		save_flags_cli (flags);
	}
	outb (addr >> 12, REG_PAGE);
	outw (val, ETHER1_RAM + ((addr & 4095) >> 1));
	if (svflgs)
		restore_flags (flags);
}

/*
 * Some inline assembler to allow fast transfers on to/off of the card.
 * Since this driver depends on some features presented by the ARM
 * specific architecture, and that you can't configure this driver
 * without specifiing ARM mode, this is not a problem.
 *
 * This routine is essentially an optimised memcpy from the card's
 * onboard RAM to kernel memory.
 */
static inline void *
ether1_inswb (unsigned int addr, void *data, unsigned int len)
{
	int used;

	addr = IO_BASE + (addr << 2);

	__asm__ __volatile__(
		"subs	%3, %3, #2
		bmi	2f
1:		ldr	%0, [%1], #4
		strb	%0, [%2], #1
		mov	%0, %0, lsr #8
		strb	%0, [%2], #1
		subs	%3, %3, #2
		bmi	2f
		ldr	%0, [%1], #4
		strb	%0, [%2], #1
		mov	%0, %0, lsr #8
		strb	%0, [%2], #1
		subs	%3, %3, #2
		bmi	2f
		ldr	%0, [%1], #4
		strb	%0, [%2], #1
		mov	%0, %0, lsr #8
		strb	%0, [%2], #1
		subs	%3, %3, #2
		bmi	2f
		ldr	%0, [%1], #4
		strb	%0, [%2], #1
		mov	%0, %0, lsr #8
		strb	%0, [%2], #1
		subs	%3, %3, #2
		bpl	1b
2:		adds	%3, %3, #1
		ldreqb	%0, [%1]
		streqb	%0, [%2]"
	: "=&r" (used), "=&r" (addr), "=&r" (data), "=&r" (len)
	:                "1"  (addr), "2"   (data), "3"   (len));

	return data;
}

static inline void *
ether1_outswb (unsigned int addr, void *data, unsigned int len)
{
	int used;

	addr = IO_BASE + (addr << 2);

	__asm__ __volatile__(
		"subs	%3, %3, #2
		bmi	2f
1:		ldr	%0, [%2], #2
		mov	%0, %0, lsl #16
		orr	%0, %0, %0, lsr #16
		str	%0, [%1], #4
		subs	%3, %3, #2
		bmi	2f
		ldr	%0, [%2], #2
		mov	%0, %0, lsl #16
		orr	%0, %0, %0, lsr #16
		str	%0, [%1], #4
		subs	%3, %3, #2
		bmi	2f
		ldr	%0, [%2], #2
		mov	%0, %0, lsl #16
		orr	%0, %0, %0, lsr #16
		str	%0, [%1], #4
		subs	%3, %3, #2
		bmi	2f
		ldr	%0, [%2], #2
		mov	%0, %0, lsl #16
		orr	%0, %0, %0, lsr #16
		str	%0, [%1], #4
		subs	%3, %3, #2
		bpl	1b
2:		adds	%3, %3, #1
		ldreqb	%0, [%2]
		streqb	%0, [%1]"
	: "=&r" (used), "=&r" (addr), "=&r" (data), "=&r" (len)
	:                "1"  (addr), "2"   (data), "3"   (len));

	return data;
}


static void
ether1_writebuffer (struct device *dev, void *data, unsigned int start, unsigned int length)
{
	unsigned int page, thislen, offset;

	offset = start & 4095;

	for (page = start >> 12; length; page++) {
		outb (page, REG_PAGE);
		if (offset + length > 4096) {
			length -= 4096 - offset;
			thislen = 4096 - offset;
		} else {
			thislen = length;
			length = 0;
		}

		data = ether1_outswb (ETHER1_RAM + (offset >> 1), data, thislen);
		offset = 0;
	}
}

static void
ether1_readbuffer (struct device *dev, void *data, unsigned int start, unsigned int length)
{
	unsigned int page, thislen, offset;

	offset = start & 4095;

	for (page = start >> 12; length; page++) {
		outb (page, REG_PAGE);
		if (offset + length > 4096) {
			length -= 4096 - offset;
			thislen = 4096 - offset;
		} else {
			thislen = length;
			length = 0;
		}

		data = ether1_inswb (ETHER1_RAM + (offset >> 1), data, thislen);
		offset = 0;
	}
}

__initfunc(static int
ether1_ramtest (struct device *dev, unsigned char byte))
{
	unsigned char *buffer = kmalloc (BUFFER_SIZE, GFP_KERNEL);
	int i, ret = BUFFER_SIZE;
	int max_errors = 15;
	int bad = -1;
	int bad_start = 0;

	if (!buffer)
		return 1;

	memset (buffer, byte, BUFFER_SIZE);
	ether1_writebuffer (dev, buffer, 0, BUFFER_SIZE);
	memset (buffer, byte ^ 0xff, BUFFER_SIZE);
	ether1_readbuffer (dev, buffer, 0, BUFFER_SIZE);

	for (i = 0; i < BUFFER_SIZE; i++) {
		if (buffer[i] != byte) {
			if (max_errors >= 0 && bad != buffer[i]) {
				if (bad != -1)
					printk ("\n");
				printk (KERN_CRIT "%s: RAM failed with (%02X instead of %02X) at 0x%04X",
					dev->name, buffer[i], byte, i);
				ret = -ENODEV;
				max_errors --;
				bad = buffer[i];
				bad_start = i;
			}
		} else {
			if (bad != -1) {
			    	if (bad_start == i - 1)
					printk ("\n");
				else
					printk (" - 0x%04X\n", i - 1);
				bad = -1;
			}
		}
	}

	if (bad != -1)
		printk (" - 0x%04X\n", BUFFER_SIZE);
	kfree (buffer);

	return ret;
}

static int
ether1_reset (struct device *dev)
{
	outb (CTRL_RST|CTRL_ACK, REG_CONTROL);
	return BUS_16;
}

__initfunc(static int
ether1_init_2 (struct device *dev))
{
	int i;
	dev->mem_start = 0;

	i = ether1_ramtest (dev, 0x5a);

	if (i > 0)
		i = ether1_ramtest (dev, 0x1e);

	if (i <= 0)
	    	return -ENODEV;

	dev->mem_end = i;
	return 0;
}

/*
 * These are the structures that are loaded into the ether RAM card to
 * initialise the 82586
 */

/* at 0x0100 */
#define NOP_ADDR	(TX_AREA_START)
#define NOP_SIZE	(0x06)
static nop_t  init_nop  = {
	0,
	CMD_NOP,
	NOP_ADDR
};

/* at 0x003a */
#define TDR_ADDR	(0x003a)
#define TDR_SIZE	(0x08)
static tdr_t  init_tdr	= {
	0,
	CMD_TDR | CMD_INTR,
	NOP_ADDR,
	0
};

/* at 0x002e */
#define MC_ADDR		(0x002e)
#define MC_SIZE		(0x0c)
static mc_t   init_mc   = {
	0,
	CMD_SETMULTICAST,
	TDR_ADDR,
	0,
	{ { 0, } }
};

/* at 0x0022 */
#define SA_ADDR		(0x0022)
#define SA_SIZE		(0x0c)
static sa_t   init_sa   = {
	0,
	CMD_SETADDRESS,
	MC_ADDR,
	{ 0, }
};

/* at 0x0010 */
#define CFG_ADDR	(0x0010)
#define CFG_SIZE	(0x12)
static cfg_t  init_cfg  = {
	0,
	CMD_CONFIG,
	SA_ADDR,
	8,
	8,
	CFG8_SRDY,
	CFG9_PREAMB8 | CFG9_ADDRLENBUF | CFG9_ADDRLEN(6),
	0,
	0x60,
	0,
	CFG13_RETRY(15) | CFG13_SLOTH(2),
	0,
};

/* at 0x0000 */
#define SCB_ADDR	(0x0000)
#define SCB_SIZE	(0x10)
static scb_t  init_scb  = {
	0,
	SCB_CMDACKRNR | SCB_CMDACKCNA | SCB_CMDACKFR | SCB_CMDACKCX,
	CFG_ADDR,
	RX_AREA_START,
	0,
	0,
	0,
	0
};

/* at 0xffee */
#define ISCP_ADDR	(0xffee)
#define ISCP_SIZE	(0x08)
static iscp_t init_iscp = {
	1,
	SCB_ADDR,
	0x0000,
	0x0000
};

/* at 0xfff6 */
#define SCP_ADDR	(0xfff6)
#define SCP_SIZE	(0x0a)
static scp_t  init_scp  = {
	SCP_SY_16BBUS,
	{ 0, 0 },
	ISCP_ADDR,
	0
};

#define RFD_SIZE	(0x16)
static rfd_t  init_rfd	= {
	0,
	0,
	0,
	0,
	{ 0, },
	{ 0, },
	0
};

#define RBD_SIZE	(0x0a)
static rbd_t  init_rbd	= {
	0,
	0,
	0,
	0,
	ETH_FRAME_LEN + 8
};

#define TX_SIZE		(0x08)
#define TBD_SIZE	(0x08)

static int
ether1_init_for_open (struct device *dev)
{
	struct ether1_priv *priv = (struct ether1_priv *)dev->priv;
	int i, status, addr, next, next2;
	int failures = 0;

	outb (CTRL_RST|CTRL_ACK, REG_CONTROL);

	for (i = 0; i < 6; i++)
		init_sa.sa_addr[i] = dev->dev_addr[i];

	/* load data structures into ether1 RAM */
	ether1_writebuffer (dev, &init_scp,  SCP_ADDR,  SCP_SIZE);
	ether1_writebuffer (dev, &init_iscp, ISCP_ADDR, ISCP_SIZE);
	ether1_writebuffer (dev, &init_scb,  SCB_ADDR,  SCB_SIZE);
	ether1_writebuffer (dev, &init_cfg,  CFG_ADDR,  CFG_SIZE);
	ether1_writebuffer (dev, &init_sa,   SA_ADDR,   SA_SIZE);
	ether1_writebuffer (dev, &init_mc,   MC_ADDR,   MC_SIZE);
	ether1_writebuffer (dev, &init_tdr,  TDR_ADDR,  TDR_SIZE);
	ether1_writebuffer (dev, &init_nop,  NOP_ADDR,  NOP_SIZE);

	if (ether1_inw (dev, CFG_ADDR, cfg_t, cfg_command, NORMALIRQS) != CMD_CONFIG) {
		printk (KERN_ERR "%s: detected either RAM fault or compiler bug\n",
			dev->name);
		return 1;
	}

	/*
	 * setup circularly linked list of { rfd, rbd, buffer }, with
	 * all rfds circularly linked, rbds circularly linked.
	 * First rfd is linked to scp, first rbd is linked to first
	 * rfd.  Last rbd has a suspend command.
	 */
	addr = RX_AREA_START;
	do {
		next = addr + RFD_SIZE + RBD_SIZE + ETH_FRAME_LEN + 10;
		next2 = next + RFD_SIZE + RBD_SIZE + ETH_FRAME_LEN + 10;

		if (next2 >= RX_AREA_END) {
			next = RX_AREA_START;
			init_rfd.rfd_command = RFD_CMDEL | RFD_CMDSUSPEND;
			priv->rx_tail = addr;
		} else
			init_rfd.rfd_command = 0;
		if (addr == RX_AREA_START)
			init_rfd.rfd_rbdoffset = addr + RFD_SIZE;
		else
			init_rfd.rfd_rbdoffset = 0;
		init_rfd.rfd_link = next;
		init_rbd.rbd_link = next + RFD_SIZE;
		init_rbd.rbd_bufl = addr + RFD_SIZE + RBD_SIZE;

		ether1_writebuffer (dev, &init_rfd, addr, RFD_SIZE);
		ether1_writebuffer (dev, &init_rbd, addr + RFD_SIZE, RBD_SIZE);
		addr = next;
	} while (next2 < RX_AREA_END);

	priv->tx_link = NOP_ADDR;
	priv->tx_head = NOP_ADDR + NOP_SIZE;
	priv->tx_tail = TDR_ADDR;
	priv->rx_head = RX_AREA_START;

	/* release reset & give 586 a prod */
	priv->resetting = 1;
	priv->initialising = 1;
	outb (CTRL_RST, REG_CONTROL);
	outb (0, REG_CONTROL);
	outb (CTRL_CA, REG_CONTROL);

	/* 586 should now unset iscp.busy */
	i = jiffies + HZ/2;
	while (ether1_inw (dev, ISCP_ADDR, iscp_t, iscp_busy, DISABLEIRQS) == 1) {
		if (time_after(jiffies, i)) {
			printk (KERN_WARNING "%s: can't initialise 82586: iscp is busy\n", dev->name);
			return 1;
		}
	}

	/* check status of commands that we issued */
	i += HZ/10;
	while (((status = ether1_inw (dev, CFG_ADDR, cfg_t, cfg_status, DISABLEIRQS))
			& STAT_COMPLETE) == 0) {
		if (time_after(jiffies, i))
			break;
	}

	if ((status & (STAT_COMPLETE | STAT_OK)) != (STAT_COMPLETE | STAT_OK)) {
		printk (KERN_WARNING "%s: can't initialise 82586: config status %04X\n", dev->name, status);
		printk (KERN_DEBUG "%s: SCB=[STS=%04X CMD=%04X CBL=%04X RFA=%04X]\n", dev->name,
			ether1_inw (dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_command, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_rfa_offset, NORMALIRQS));
		failures += 1;
	}

	i += HZ/10;
	while (((status = ether1_inw (dev, SA_ADDR, sa_t, sa_status, DISABLEIRQS))
			& STAT_COMPLETE) == 0) {
		if (time_after(jiffies, i))
			break;
	}

	if ((status & (STAT_COMPLETE | STAT_OK)) != (STAT_COMPLETE | STAT_OK)) {
		printk (KERN_WARNING "%s: can't initialise 82586: set address status %04X\n", dev->name, status);
		printk (KERN_DEBUG "%s: SCB=[STS=%04X CMD=%04X CBL=%04X RFA=%04X]\n", dev->name,
			ether1_inw (dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_command, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_rfa_offset, NORMALIRQS));
		failures += 1;
	}

	i += HZ/10;
	while (((status = ether1_inw (dev, MC_ADDR, mc_t, mc_status, DISABLEIRQS))
			& STAT_COMPLETE) == 0) {
		if (time_after(jiffies, i))
			break;
	}

	if ((status & (STAT_COMPLETE | STAT_OK)) != (STAT_COMPLETE | STAT_OK)) {
		printk (KERN_WARNING "%s: can't initialise 82586: set multicast status %04X\n", dev->name, status);
		printk (KERN_DEBUG "%s: SCB=[STS=%04X CMD=%04X CBL=%04X RFA=%04X]\n", dev->name,
			ether1_inw (dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_command, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_rfa_offset, NORMALIRQS));
		failures += 1;
	}

	i += HZ;
	while (((status = ether1_inw (dev, TDR_ADDR, tdr_t, tdr_status, DISABLEIRQS))
			& STAT_COMPLETE) == 0) {
		if (time_after(jiffies, i))
			break;
	}

	if ((status & (STAT_COMPLETE | STAT_OK)) != (STAT_COMPLETE | STAT_OK)) {
		printk (KERN_WARNING "%s: can't tdr (ignored)\n", dev->name);
		printk (KERN_DEBUG "%s: SCB=[STS=%04X CMD=%04X CBL=%04X RFA=%04X]\n", dev->name,
			ether1_inw (dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_command, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS),
			ether1_inw (dev, SCB_ADDR, scb_t, scb_rfa_offset, NORMALIRQS));
	} else {
		status = ether1_inw (dev, TDR_ADDR, tdr_t, tdr_result, DISABLEIRQS);
		if (status & TDR_XCVRPROB)
			printk (KERN_WARNING "%s: i/f failed tdr: transceiver problem\n", dev->name);
		else if ((status & (TDR_SHORT|TDR_OPEN)) && (status & TDR_TIME)) {
#ifdef FANCY
			printk (KERN_WARNING "%s: i/f failed tdr: cable %s %d.%d us away\n", dev->name,
				status & TDR_SHORT ? "short" : "open", (status & TDR_TIME) / 10,
				(status & TDR_TIME) % 10);
#else
			printk (KERN_WARNING "%s: i/f failed tdr: cable %s %d clks away\n", dev->name,
				status & TDR_SHORT ? "short" : "open", (status & TDR_TIME));
#endif
		}
	}

	if (failures)
		ether1_reset (dev);
	return failures ? 1 : 0;
}

__initfunc(static int
ether1_probe1 (struct device *dev))
{
	static unsigned int version_printed = 0;
	struct ether1_priv *priv;
	int i;

	if (!dev->priv)
		dev->priv = kmalloc (sizeof (struct ether1_priv), GFP_KERNEL);

	if (!dev->priv)
	    	return 1;

	priv = (struct ether1_priv *)dev->priv;
	memset (priv, 0, sizeof (struct ether1_priv));

	if ((priv->bus_type = ether1_reset (dev)) == 0) {
		kfree (dev->priv);
		return 1;
	}

	if (net_debug && version_printed++ == 0)
		printk (KERN_INFO "%s", version);

	printk (KERN_INFO "%s: ether1 found [%d, %04lx, %d]", dev->name, priv->bus_type,
		dev->base_addr, dev->irq);

	request_region (dev->base_addr, 16, "ether1");
	request_region (dev->base_addr + 0x800, 4096, "ether1(ram)");

	for (i = 0; i < 6; i++)
		printk (i==0?" %02x":i==5?":%02x\n":":%02x", dev->dev_addr[i]);

	if (ether1_init_2 (dev)) {
		kfree (dev->priv);
		return 1;
	}

	dev->open		    = ether1_open;
	dev->stop		    = ether1_close;
	dev->hard_start_xmit    = ether1_sendpacket;
	dev->get_stats	    = ether1_getstats;
	dev->set_multicast_list = ether1_setmulticastlist;

	/* Fill in the fields of the device structure with ethernet values */
	ether_setup (dev);

#ifndef CLAIM_IRQ_AT_OPEN
	if (request_irq (dev->irq, ether1_interrupt, 0, "ether1", dev)) {
		kfree (dev->priv);
		return -EAGAIN;
	}
#endif
	return 0;
}	
    
/* ------------------------------------------------------------------------- */

__initfunc(static void
ether1_addr (struct device *dev))
{
	int i;
    
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = inb (IDPROM_ADDRESS + i);
}

__initfunc(int
ether1_probe (struct device *dev))
{
#ifndef MODULE
	struct expansion_card *ec;

	if (!dev)
		return ENODEV;

	ecard_startfind ();
	if ((ec = ecard_find (0, ether1_cids)) == NULL)
		return ENODEV;

	dev->base_addr = ecard_address (ec, ECARD_IOC, ECARD_FAST);
	dev->irq       = ec->irq;

	ecard_claim (ec);

#endif
	ether1_addr (dev);

	if (ether1_probe1 (dev) == 0)
		return 0;
	return ENODEV;
}

/* ------------------------------------------------------------------------- */

static int
ether1_txalloc (struct device *dev, int size)
{
	struct ether1_priv *priv = (struct ether1_priv *)dev->priv;
	int start, tail;

	size = (size + 1) & ~1;
	tail = priv->tx_tail;

	if (priv->tx_head + size > TX_AREA_END) {
		if (tail > priv->tx_head)
			return -1;
		start = TX_AREA_START;
		if (start + size > tail)
			return -1;
		priv->tx_head = start + size;
	} else {
		if (priv->tx_head < tail && (priv->tx_head + size) > tail)
			return -1;
		start = priv->tx_head;
		priv->tx_head += size;
	}

	return start;
}

static void
ether1_restart (struct device *dev, char *reason)
{
	struct ether1_priv *priv = (struct ether1_priv *)dev->priv;
	priv->stats.tx_errors ++;

	if (reason)
		printk (KERN_WARNING "%s: %s - resetting device\n", dev->name, reason);
	else
		printk (" - resetting device\n");

	ether1_reset (dev);

	dev->start = 0;
	dev->tbusy = 0;

	if (ether1_init_for_open (dev))
		printk (KERN_ERR "%s: unable to restart interface\n", dev->name);

	dev->start = 1;
}

static int
ether1_open (struct device *dev)
{
	struct ether1_priv *priv = (struct ether1_priv *)dev->priv;
#ifdef CLAIM_IRQ_AT_OPEN
	if (request_irq (dev->irq, ether1_interrupt, 0, "ether1", dev))
		return -EAGAIN;
#endif
	MOD_INC_USE_COUNT;

	memset (&priv->stats, 0, sizeof (struct enet_statistics));

	if (ether1_init_for_open (dev)) {
#ifdef CLAIM_IRQ_AT_OPEN
		free_irq (dev->irq, dev);
#endif
		MOD_DEC_USE_COUNT;
		return -EAGAIN;
	}

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	return 0;
}

static int
ether1_sendpacket (struct sk_buff *skb, struct device *dev)
{
	struct ether1_priv *priv = (struct ether1_priv *)dev->priv;

	if (priv->restart)
		ether1_restart (dev, NULL);

	if (dev->tbusy) {
		/*
		 * If we get here, some higher level has decided that we are broken.
		 * There should really be a "kick me" function call instead.
		 */
		int tickssofar = jiffies - dev->trans_start;

		if (tickssofar < 5)
			return 1;

		/* Try to restart the adapter. */
		ether1_restart (dev, "transmit timeout, network cable problem?");
		dev->trans_start = jiffies;
	}

	/*
	 * Block a timer-based transmit from overlapping.  This could better be
	 * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	 */
	if (test_and_set_bit (0, (void *)&dev->tbusy) != 0)
		printk (KERN_WARNING "%s: transmitter access conflict.\n", dev->name);
	else {
		int len = (ETH_ZLEN < skb->len) ? skb->len : ETH_ZLEN;
		int tmp, tst, nopaddr, txaddr, tbdaddr, dataddr;
		unsigned long flags;
		tx_t tx;
		tbd_t tbd;
		nop_t nop;

		/*
		 * insert packet followed by a nop
		 */
		txaddr = ether1_txalloc (dev, TX_SIZE);
		tbdaddr = ether1_txalloc (dev, TBD_SIZE);
		dataddr = ether1_txalloc (dev, len);
		nopaddr = ether1_txalloc (dev, NOP_SIZE);

		tx.tx_status = 0;
		tx.tx_command = CMD_TX | CMD_INTR;
		tx.tx_link = nopaddr;
		tx.tx_tbdoffset = tbdaddr;
		tbd.tbd_opts = TBD_EOL | len;
		tbd.tbd_link = I82586_NULL;
		tbd.tbd_bufl = dataddr;
		tbd.tbd_bufh = 0;
		nop.nop_status = 0;
		nop.nop_command = CMD_NOP;
		nop.nop_link = nopaddr;

		save_flags_cli (flags);
		ether1_writebuffer (dev, &tx, txaddr, TX_SIZE);
		ether1_writebuffer (dev, &tbd, tbdaddr, TBD_SIZE);
		ether1_writebuffer (dev, skb->data, dataddr, len);
		ether1_writebuffer (dev, &nop, nopaddr, NOP_SIZE);
		tmp = priv->tx_link;
		priv->tx_link = nopaddr;

		/* now reset the previous nop pointer */
		ether1_outw (dev, txaddr, tmp, nop_t, nop_link, NORMALIRQS);

		restore_flags (flags);

		/* handle transmit */
		dev->trans_start = jiffies;

		/* check to see if we have room for a full sized ether frame */
		tmp = priv->tx_head;
		tst = ether1_txalloc (dev, TX_SIZE + TBD_SIZE + NOP_SIZE + ETH_FRAME_LEN);
		priv->tx_head = tmp;
		if (tst != -1)
			dev->tbusy = 0;
	}
	dev_kfree_skb (skb);

	return 0;
}

static void
ether1_xmit_done (struct device *dev)
{
	struct ether1_priv *priv = (struct ether1_priv *)dev->priv;
	nop_t nop;
	int caddr, tst;

	caddr = priv->tx_tail;

again:
	ether1_readbuffer (dev, &nop, caddr, NOP_SIZE);

	switch (nop.nop_command & CMD_MASK) {
	case CMD_TDR:
		/* special case */
		if (ether1_inw (dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS)
				!= (unsigned short)I82586_NULL) {
			ether1_outw(dev, SCB_CMDCUCSTART | SCB_CMDRXSTART, SCB_ADDR, scb_t,
				    scb_command, NORMALIRQS);
			outb (CTRL_CA, REG_CONTROL);
		}
		priv->tx_tail = NOP_ADDR;
		return;

	case CMD_NOP:
		if (nop.nop_link == caddr) {
			if (priv->initialising == 0)
				printk (KERN_WARNING "%s: strange command complete with no tx command!\n", dev->name);
			else
			        priv->initialising = 0;
			return;
		}
		if (caddr == nop.nop_link)
			return;
		caddr = nop.nop_link;
		goto again;

	case CMD_TX:
		if (nop.nop_status & STAT_COMPLETE)
			break;
		printk (KERN_ERR "%s: strange command complete without completed command\n", dev->name);
		priv->restart = 1;
		return;

	default:
		printk (KERN_WARNING "%s: strange command %d complete! (offset %04X)", dev->name,
			nop.nop_command & CMD_MASK, caddr);
		priv->restart = 1;
		return;
	}

	while (nop.nop_status & STAT_COMPLETE) {
		if (nop.nop_status & STAT_OK) {
			priv->stats.tx_packets ++;
			priv->stats.collisions += (nop.nop_status & STAT_COLLISIONS);
		} else {
			priv->stats.tx_errors ++;

			if (nop.nop_status & STAT_COLLAFTERTX)
				priv->stats.collisions ++;
			if (nop.nop_status & STAT_NOCARRIER)
				priv->stats.tx_carrier_errors ++;
			if (nop.nop_status & STAT_TXLOSTCTS)
				printk (KERN_WARNING "%s: cts lost\n", dev->name);
			if (nop.nop_status & STAT_TXSLOWDMA)
				priv->stats.tx_fifo_errors ++;
			if (nop.nop_status & STAT_COLLEXCESSIVE)
				priv->stats.collisions += 16;
		}

		if (nop.nop_link == caddr) {
			printk (KERN_ERR "%s: tx buffer chaining error: tx command points to itself\n", dev->name);
			break;
		}

		caddr = nop.nop_link;
		ether1_readbuffer (dev, &nop, caddr, NOP_SIZE);
		if ((nop.nop_command & CMD_MASK) != CMD_NOP) {
			printk (KERN_ERR "%s: tx buffer chaining error: no nop after tx command\n", dev->name);
			break;
		}

		if (caddr == nop.nop_link)
			break;

		caddr = nop.nop_link;
		ether1_readbuffer (dev, &nop, caddr, NOP_SIZE);
		if ((nop.nop_command & CMD_MASK) != CMD_TX) {
			printk (KERN_ERR "%s: tx buffer chaining error: no tx command after nop\n", dev->name);
			break;
		}
	}
	priv->tx_tail = caddr;

	caddr = priv->tx_head;
	tst = ether1_txalloc (dev, TX_SIZE + TBD_SIZE + NOP_SIZE + ETH_FRAME_LEN);
	priv->tx_head = caddr;
	if (tst != -1)
		dev->tbusy = 0;
    
	mark_bh (NET_BH);
}

static void
ether1_recv_done (struct device *dev)
{
	struct ether1_priv *priv = (struct ether1_priv *)dev->priv;
	int status;
	int nexttail, rbdaddr;
	rbd_t rbd;

	do {
		status = ether1_inw (dev, priv->rx_head, rfd_t, rfd_status, NORMALIRQS);
		if ((status & RFD_COMPLETE) == 0)
			break;

		rbdaddr = ether1_inw (dev, priv->rx_head, rfd_t, rfd_rbdoffset, NORMALIRQS);
		ether1_readbuffer (dev, &rbd, rbdaddr, RBD_SIZE);

		if ((rbd.rbd_status & (RBD_EOF | RBD_ACNTVALID)) == (RBD_EOF | RBD_ACNTVALID)) {
			int length = rbd.rbd_status & RBD_ACNT;
			struct sk_buff *skb;

			length = (length + 1) & ~1;
			skb = dev_alloc_skb (length + 2);

			if (skb) {
				skb->dev = dev;
				skb_reserve (skb, 2);

				ether1_readbuffer (dev, skb_put (skb, length), rbd.rbd_bufl, length);

				skb->protocol = eth_type_trans (skb, dev);
				netif_rx (skb);
				priv->stats.rx_packets ++;
			} else
				priv->stats.rx_dropped ++;
		} else {
			printk(KERN_WARNING "%s: %s\n", dev->name,
				(rbd.rbd_status & RBD_EOF) ? "oversized packet" : "acnt not valid");
			priv->stats.rx_dropped ++;
		}

		nexttail = ether1_inw (dev, priv->rx_tail, rfd_t, rfd_link, NORMALIRQS);
		/* nexttail should be rx_head */
		if (nexttail != priv->rx_head)
			printk(KERN_ERR "%s: receiver buffer chaining error (%04X != %04X)\n",
				dev->name, nexttail, priv->rx_head);
		ether1_outw (dev, RFD_CMDEL | RFD_CMDSUSPEND, nexttail, rfd_t, rfd_command, NORMALIRQS);
		ether1_outw (dev, 0, priv->rx_tail, rfd_t, rfd_command, NORMALIRQS);
		ether1_outw (dev, 0, priv->rx_tail, rfd_t, rfd_status, NORMALIRQS);
		ether1_outw (dev, 0, priv->rx_tail, rfd_t, rfd_rbdoffset, NORMALIRQS);
	
		priv->rx_tail = nexttail;
		priv->rx_head = ether1_inw (dev, priv->rx_head, rfd_t, rfd_link, NORMALIRQS);
	} while (1);
}

static void
ether1_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct ether1_priv *priv = (struct ether1_priv *)dev->priv;
	int status;

	dev->interrupt = 1;

	status = ether1_inw (dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS);

	if (status) {
		ether1_outw(dev, status & (SCB_STRNR | SCB_STCNA | SCB_STFR | SCB_STCX),
			    SCB_ADDR, scb_t, scb_command, NORMALIRQS);
		outb (CTRL_CA | CTRL_ACK, REG_CONTROL);
		if (status & SCB_STCX) {
			ether1_xmit_done (dev);
		}
		if (status & SCB_STCNA) {
			if (priv->resetting == 0)
				printk (KERN_WARNING "%s: CU went not ready ???\n", dev->name);
			else
				priv->resetting += 1;
			if (ether1_inw (dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS)
					!= (unsigned short)I82586_NULL) {
				ether1_outw (dev, SCB_CMDCUCSTART, SCB_ADDR, scb_t, scb_command, NORMALIRQS);
				outb (CTRL_CA, REG_CONTROL);
			}
			if (priv->resetting == 2)
				priv->resetting = 0;
		}
		if (status & SCB_STFR) {
			ether1_recv_done (dev);
		}
		if (status & SCB_STRNR) {
			if (ether1_inw (dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS) & SCB_STRXSUSP) {
				printk (KERN_WARNING "%s: RU went not ready: RU suspended\n", dev->name);
				ether1_outw (dev, SCB_CMDRXRESUME, SCB_ADDR, scb_t, scb_command, NORMALIRQS);
				outb (CTRL_CA, REG_CONTROL);
				priv->stats.rx_dropped ++;	/* we suspended due to lack of buffer space */
			} else
				printk(KERN_WARNING "%s: RU went not ready: %04X\n", dev->name,
					ether1_inw (dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS));
			printk (KERN_WARNING "RU ptr = %04X\n", ether1_inw (dev, SCB_ADDR, scb_t, scb_rfa_offset,
						NORMALIRQS));
		}
	} else
	        outb (CTRL_ACK, REG_CONTROL);

	dev->interrupt = 0;
}

static int
ether1_close (struct device *dev)
{
#ifdef CLAIM_IRQ_AT_OPEN
	free_irq (dev->irq, dev);
#endif

	ether1_reset (dev);

	dev->start = 0;
	dev->tbusy = 0;

	MOD_DEC_USE_COUNT;

	return 0;
}

static struct enet_statistics *
ether1_getstats (struct device *dev)
{
	struct ether1_priv *priv = (struct ether1_priv *)dev->priv;
	return &priv->stats;
}

/*
 * Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets.
 * num_addrs == 0	Normal mode, clear multicast list.
 * num_addrs > 0	Multicast mode, receive normal and MC packets, and do
 *			best-effort filtering.
 */
static void
ether1_setmulticastlist (struct device *dev)
{
}

/* ------------------------------------------------------------------------- */

#ifdef MODULE

static char ethernames[MAX_ECARDS][9];
static struct device *my_ethers[MAX_ECARDS];
static struct expansion_card *ec[MAX_ECARDS];

int
init_module (void)
{
	int i;

	for (i = 0; i < MAX_ECARDS; i++) {
		my_ethers[i] = NULL;
		ec[i] = NULL;
		strcpy (ethernames[i], "        ");
	}

	i = 0;

	ecard_startfind ();

	do {
		if ((ec[i] = ecard_find(0, ether1_cids)) == NULL)
			break;

		my_ethers[i] = (struct device *)kmalloc (sizeof (struct device), GFP_KERNEL);
		memset (my_ethers[i], 0, sizeof (struct device));

		my_ethers[i]->irq = ec[i]->irq;
		my_ethers[i]->base_addr = ecard_address (ec[i], ECARD_IOC, ECARD_FAST);
		my_ethers[i]->init = ether1_probe;
		my_ethers[i]->name = ethernames[i];

		ecard_claim (ec[i]);

		if (register_netdev (my_ethers[i]) != 0) {
			for (i = 0; i < 4; i++) {
				if (my_ethers[i]) {
					kfree (my_ethers[i]);
					my_ethers[i] = NULL;
				}
				if (ec[i]) {
					ecard_release (ec[i]);
					ec[i] = NULL;
				}
			}
			return -EIO;
		}
		i++;
	} while (i < MAX_ECARDS);

	return i != 0 ? 0 : -ENODEV;
}

void
cleanup_module (void)
{
	int i;

	for (i = 0; i < MAX_ECARDS; i++) {
		if (my_ethers[i]) {
			unregister_netdev (my_ethers[i]);
			release_region (my_ethers[i]->base_addr, 16);
			release_region (my_ethers[i]->base_addr + 0x800, 4096);
#ifndef CLAIM_IRQ_AT_OPEN
			free_irq (my_ethers[i]->irq, my_ethers[i]);
#endif
			my_ethers[i] = NULL;
		}
		if (ec[i]) {
			ecard_release (ec[i]);
			ec[i] = NULL;
		}
	}
}
#endif /* MODULE */
