/* $Id: plip.c,v 1.7 1994/12/16 06:20:02 gniibe Exp $ */
/* plip.c: A parallel port "network" driver for linux. */
/* This driver is for parallel port with 5-bit cable (LapLink (R) cable). */
/*
 * Authors:	Donald Becker,  <becker@super.org>
 *		Tommy Thorn, <thorn@daimi.aau.dk>
 *		Tanabe Hiroyasu, <hiro@sanpo.t.u-tokyo.ac.jp>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Peter Bauer, <100136.3530@compuserve.com>
 *		Niibe Yutaka, <gniibe@mri.co.jp>
 *
 *		This is the all improved state based PLIP that Niibe Yutaka has contributed.
 *
 *		Modularization by Alan Cox. I also added the plipconfig program to tune the timeouts
 *		and ifmap support for funny serial port settings or setting odd values using the 
 *		modular plip. I also took the panic() calls out. I don't like panic - especially when
 *		it can be avoided.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

/*
 * Original version and the name 'PLIP' from Donald Becker <becker@super.org>
 * inspired by Russ Nelson's parallel port packet driver.
 */

static char *version =
    "NET3 "
#ifdef MODULE
    "MODULAR "    
#endif    
    "PLIP $Revision: 1.7 $ gniibe@mri.co.jp\n";

/*
  Sources:
	Ideas and protocols came from Russ Nelson's <nelson@crynwr.com>
	"parallel.asm" parallel port packet driver.

  The "Crynwr" parallel port standard specifies the following protocol:
   send header nibble '8'
   count-low octet
   count-high octet
   ... data octets
   checksum octet
  Each octet is sent as <wait for rx. '0x1?'> <send 0x10+(octet&0x0F)>
			<wait for rx. '0x0?'> <send 0x00+((octet>>4)&0x0F)>

The cable used is a de facto standard parallel null cable -- sold as
a "LapLink" cable by various places.  You'll need a 10-conductor cable to
make one yourself.  The wiring is:
    SLCTIN	17 - 17
    GROUND	25 - 25
    D0->ERROR	2 - 15		15 - 2
    D1->SLCT	3 - 13		13 - 3
    D2->PAPOUT	4 - 12		12 - 4
    D3->ACK	5 - 10		10 - 5
    D4->BUSY	6 - 11		11 - 6
  Do not connect the other pins.  They are
    D5,D6,D7 are 7,8,9
    STROBE is 1, FEED is 14, INIT is 16
    extra grounds are 18,19,20,21,22,23,24
*/

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/if_ether.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/lp.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_plip.h>

#include <linux/tqueue.h>
#include <linux/ioport.h>
#include <asm/bitops.h>
#include <asm/irq.h>

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 3
#endif
static unsigned int net_debug = NET_DEBUG;

/* In micro second */
#define PLIP_DELAY_UNIT		   1

/* Connection time out = PLIP_TRIGGER_WAIT * PLIP_DELAY_UNIT usec */
#define PLIP_TRIGGER_WAIT	 500

/* Nibble time out = PLIP_NIBBLE_WAIT * PLIP_DELAY_UNIT usec */
#define PLIP_NIBBLE_WAIT        5000

#define PAR_DATA(dev)		((dev)->base_addr+0)
#define PAR_STATUS(dev)		((dev)->base_addr+1)
#define PAR_CONTROL(dev)	((dev)->base_addr+2)

/* Index to functions, as function prototypes. */
static int plip_tx_packet(struct sk_buff *skb, struct device *dev);
static int plip_open(struct device *dev);
static int plip_close(struct device *dev);
static struct enet_statistics *plip_get_stats(struct device *dev);
static int plip_rebuild_header(void *buff, struct device *dev,
			       unsigned long raddr, struct sk_buff *skb);
static void plip_kick_bh(struct device *dev);
static void plip_bh(struct device *dev);

enum plip_connection_state {
    PLIP_CN_NONE=0,
    PLIP_CN_RECEIVE,
    PLIP_CN_SEND,
    PLIP_CN_CLOSING,
    PLIP_CN_ERROR
};

enum plip_packet_state {
    PLIP_PK_DONE=0,
    PLIP_PK_TRIGGER,
    PLIP_PK_LENGTH_LSB,
    PLIP_PK_LENGTH_MSB,
    PLIP_PK_DATA,
    PLIP_PK_CHECKSUM
};

enum plip_nibble_state {
    PLIP_NB_BEGIN,
    PLIP_NB_1,
    PLIP_NB_2,
};

#define PLIP_STATE_STRING(x) \
    (((x) == PLIP_PK_DONE)?"0":\
     ((x) == PLIP_PK_TRIGGER)?"t":\
     ((x) == PLIP_PK_LENGTH_LSB)?"l":\
     ((x) == PLIP_PK_LENGTH_MSB)?"m":\
     ((x) == PLIP_PK_DATA)?"d":\
     ((x) == PLIP_PK_CHECKSUM)?"s":"B")

struct plip_local {
    enum plip_packet_state state;
    enum plip_nibble_state nibble;
    unsigned short length;
    unsigned short byte;
    unsigned char  checksum;
    unsigned char  data;
    struct sk_buff *skb;
};

struct net_local {
    struct enet_statistics e;
    struct tq_struct immediate;
    struct tq_struct deferred;
    struct plip_local snd_data;
    struct plip_local rcv_data;
    unsigned long  trigger_us;
    unsigned long  nibble_us;
    unsigned long  unit_us;
    enum plip_connection_state connection;
    unsigned short timeout_count;
};

/* Routines used internally. */
static void plip_device_clear(struct device *dev);
static void plip_interrupt(int reg_ptr);

static int plip_error(struct device *dev);
static int plip_receive_packet(struct device *dev);
static int plip_send_packet(struct device *dev);
static int plip_ioctl(struct device *dev, struct ifreq *ifr, int cmd);
static int plip_config(struct device *dev, struct ifmap *map);

int
plip_init(struct device *dev)
{
    struct net_local *nl;

    /* Check that there is something at base_addr. */
    outb(LP_PINITP, PAR_CONTROL(dev));
    outb(0x00, PAR_DATA(dev));
    if (inb(PAR_DATA(dev)) != 0x00)
	return -ENODEV;

    /* Alpha testers must have the version number to report bugs. */
    if (net_debug)
	printk(version);

    if (dev->irq) {
	printk("%s: configured for parallel port at %#3x, IRQ %d.\n",
	       dev->name, dev->base_addr, dev->irq);
    } else {
	printk("%s: configured for parallel port at %#3x",
	       dev->name, dev->base_addr);
#ifdef MODULE
	/* autoirq doesn't work :(, but we can set it by ifconfig */
#else
	autoirq_setup(0);
	outb(LP_PINITP|LP_PSELECP, PAR_CONTROL(dev));
	outb(LP_PINITP|LP_PSELECP|LP_PINTEN, PAR_CONTROL(dev));
	outb(LP_PINITP|LP_PSELECP, PAR_CONTROL(dev));
	dev->irq = autoirq_report(1);
#endif
	if (dev->irq)
	    printk(", probed IRQ %d.\n", dev->irq);
	else {
	    printk(", failed to detect IRQ line.\n");
	    return -ENODEV;
	}
    }

    register_iomem(PAR_DATA(dev), 3,"plip");

    /* Fill in the generic fields of the device structure. */
    ether_setup(dev);

    /* And, override parts of it */
    dev->rebuild_header 	= plip_rebuild_header;
    dev->hard_start_xmit	= plip_tx_packet;
    dev->open			= plip_open;
    dev->stop			= plip_close;
    dev->get_stats 		= plip_get_stats;
    dev->set_config		= plip_config;
    dev->do_ioctl		= plip_ioctl;
    dev->flags			= IFF_POINTOPOINT;

    /* Set private structure */
    dev->priv = kmalloc(sizeof (struct net_local), GFP_KERNEL);
    memset(dev->priv, 0, sizeof(struct net_local));
    nl = (struct net_local *) dev->priv;
    
    /* initialize constants */
    nl->trigger_us	= PLIP_TRIGGER_WAIT;
    nl->nibble_us	= PLIP_NIBBLE_WAIT;
    nl->unit_us		= PLIP_DELAY_UNIT;

    /* initialize task queue structures */
    nl->immediate.next = &tq_last;
    nl->immediate.sync = 0;
    nl->immediate.routine = (void *)(void *)plip_bh;
    nl->immediate.data = dev;

    nl->deferred.next = &tq_last;
    nl->deferred.sync = 0;
    nl->deferred.routine = (void *)(void *)plip_kick_bh;
    nl->deferred.data = dev;

    return 0;
}

static void
plip_kick_bh(struct device *dev)
{
    struct net_local *nl = (struct net_local *)dev->priv;

    if (nl->connection == PLIP_CN_NONE)
	return;
    queue_task(&nl->immediate, &tq_immediate);
    mark_bh(IMMEDIATE_BH);
    return;
}

static void
plip_bh(struct device *dev)
{
    struct net_local *nl = (struct net_local *)dev->priv;
    struct enet_statistics *stats = (struct enet_statistics *) dev->priv;
    struct plip_local *rcv = &nl->rcv_data;
    struct plip_local *snd = &nl->snd_data;
    int result, timeout=0;
    unsigned char *s;
    unsigned char c0;
    struct sk_buff *skb;

    while (!timeout) {
	cli();
	switch (nl->connection) {
	case PLIP_CN_NONE:
	    sti();
	    return;

	case PLIP_CN_RECEIVE:
	    sti();
	    disable_irq(dev->irq);
	    outb(LP_PINITP|LP_PSELECP, PAR_CONTROL(dev));
	    outb(0x01, PAR_DATA(dev));   /* send ACK */
	    dev->interrupt = 0;
	    result = plip_receive_packet(dev);
	    if (result == 0) { /* success */
		outb (0x00, PAR_DATA(dev));
		skb = rcv->skb;
		rcv->skb = NULL;
		stats->rx_packets++;
		netif_rx(skb);
		if (snd->state != PLIP_PK_DONE) {
		    nl->connection = PLIP_CN_SEND;
		    outb(LP_PINITP|LP_PSELECP|LP_PINTEN, PAR_CONTROL(dev));
		    enable_irq(dev->irq);
		} else {
		    nl->connection = PLIP_CN_NONE;
		    outb(LP_PINITP|LP_PSELECP|LP_PINTEN, PAR_CONTROL(dev));
		    enable_irq(dev->irq);
		    return;
		}
	    } else if (result == -1) { /* failure */
		outb(0x00, PAR_DATA(dev));
		if (rcv->skb)
		    dev_kfree_skb(rcv->skb, FREE_WRITE);
		rcv->state = PLIP_PK_DONE;
		rcv->skb = NULL;
		if (snd->skb)
		    dev_kfree_skb(snd->skb, FREE_WRITE);
		snd->state = PLIP_PK_DONE;
		snd->skb = NULL;
		dev->tbusy = 1;
		nl->connection = PLIP_CN_ERROR;
	    } else
		timeout = 1;
	    break;

	case PLIP_CN_SEND:
	    sti();
	    result = plip_send_packet(dev);
	    if (result == -1) /* interrupted */
		break;
	    if (result == 0) { /* success */
		outb (0x00, PAR_DATA(dev));
		snd->state = PLIP_PK_DONE;
		snd->skb = NULL;
		nl->connection = PLIP_CN_CLOSING;
		queue_task(&nl->deferred, &tq_timer);
		outb(LP_PINITP|LP_PSELECP|LP_PINTEN, PAR_CONTROL(dev));
		enable_irq(dev->irq);
		return;
	    } else
		timeout = 1;
	    break;

	case PLIP_CN_CLOSING:
	    sti();
	    nl->connection = PLIP_CN_NONE;
	    mark_bh(NET_BH);
	    dev->tbusy = 0;
	    return;

	case PLIP_CN_ERROR:
	    sti();
	    result = plip_error(dev);
	    if (result == 0) {
		nl->connection = PLIP_CN_NONE;
		dev->tbusy = 0;
		outb(LP_PINITP|LP_PSELECP|LP_PINTEN, PAR_CONTROL(dev));
		enable_irq(dev->irq);
		return;
	    } else {
		queue_task(&nl->deferred, &tq_timer);
		return;
	    }
	    break;
	}
    }

    /* timeout */
    if (++nl->timeout_count > 3) { /* cable problem? */
	c0 = inb(PAR_STATUS(dev));

	if (nl->connection == PLIP_CN_SEND) {
	    stats->tx_errors++;
	    stats->tx_aborted_errors++;
	    s =  PLIP_STATE_STRING(snd->state);
	    if (net_debug > 1)
		printk("%s: transmit timeout(%s,%02x)... reset interface.\n",
		       dev->name, s, (unsigned int)c0);
	    if (snd->skb)
		dev_kfree_skb(snd->skb, FREE_WRITE);
	} else if (nl->connection == PLIP_CN_RECEIVE) {
	    stats->rx_dropped++;
	    s =  PLIP_STATE_STRING(rcv->state);
	    if (net_debug > 1)
		printk("%s: receive timeout(%s,%02x)... reset interface.\n",
		       dev->name, s, (unsigned int)c0);
	    if (rcv->skb)
		dev_kfree_skb(rcv->skb, FREE_WRITE);
	}
	disable_irq(dev->irq);
	outb(LP_PINITP|LP_PSELECP, PAR_CONTROL(dev));
	dev->tbusy = 1;
	nl->connection = PLIP_CN_ERROR;
	outb(0x00, PAR_DATA(dev));
    }

    queue_task(&nl->deferred, &tq_timer);
    return;
}

static int
plip_tx_packet(struct sk_buff *skb, struct device *dev)
{
    struct net_local *nl = (struct net_local *)dev->priv;
    struct plip_local *snd = &nl->snd_data;

    if (dev->tbusy)
	return 1;

    /* If some higher layer thinks we've missed an tx-done interrupt
       we are passed NULL. Caution: dev_tint() handles the cli()/sti()
       itself. */
    if (skb == NULL) {
	dev_tint(dev);
	return 0;
    }

    if (set_bit(0, (void*)&dev->tbusy) != 0) {
	printk("%s: Transmitter access conflict.\n", dev->name);
	return 1;
    }

    if (skb->len > dev->mtu) {
	printk("%s: packet too big, %d.\n", dev->name, (int)skb->len);
	dev->tbusy = 0;
	return 0;
    }

    snd->state = PLIP_PK_TRIGGER;
    dev->trans_start = jiffies;

    snd->skb = skb;
    snd->length = skb->len;

    cli();
    if (nl->connection == PLIP_CN_NONE) {
	nl->connection = PLIP_CN_SEND;
	nl->timeout_count = 0;
    }
    sti();
    queue_task(&nl->immediate, &tq_immediate);
    mark_bh(IMMEDIATE_BH);

    return 0;
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine gets exclusive access to the parallel port by allocating
   its IRQ line.
 */
static int
plip_open(struct device *dev)
{
    int i;

    cli();
    if (request_irq(dev->irq , plip_interrupt, 0, "plip") != 0) {
	sti();
	printk("%s: couldn't get IRQ %d.\n", dev->name, dev->irq);
	return -EAGAIN;
    }
    irq2dev_map[dev->irq] = dev;
    sti();
    /* enable rx interrupt. */
    outb(LP_PINITP|LP_PSELECP|LP_PINTEN, PAR_CONTROL(dev));
    plip_device_clear(dev);

    /* Fill in the MAC-level header. */
    for (i=0; i < ETH_ALEN - sizeof(unsigned long); i++)
	dev->dev_addr[i] = 0xfc;
    memcpy(&(dev->dev_addr[i]), &dev->pa_addr, sizeof(unsigned long));

    dev->start = 1;
#ifdef MODULE
    MOD_INC_USE_COUNT;
#endif        
    return 0;
}

/* The inverse routine to plip_open (). */
static int
plip_close(struct device *dev)
{
    dev->tbusy = 1;
    dev->start = 0;
    cli();
    free_irq(dev->irq);
    irq2dev_map[dev->irq] = NULL;
    sti();
    outb(0x00, PAR_DATA(dev));
    /* release the interrupt. */
    outb(LP_PINITP|LP_PSELECP, PAR_CONTROL(dev));
#ifdef MODULE
    MOD_DEC_USE_COUNT;
#endif        
    return 0;
}

static struct enet_statistics *
plip_get_stats(struct device *dev)
{
    struct enet_statistics *localstats = (struct enet_statistics*)dev->priv;
    return localstats;
}

/* We don't need to send arp, for plip is point-to-point. */
static int
plip_rebuild_header(void *buff, struct device *dev, unsigned long dst,
		    struct sk_buff *skb)
{
    struct ethhdr *eth = (struct ethhdr *)buff;
    int i;

    if (eth->h_proto != htons(ETH_P_IP)) {
	printk("plip_rebuild_header: Don't know how to resolve type %d addresses?\n", (int)eth->h_proto);
	memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
	return 0;
    }

    for (i=0; i < ETH_ALEN - sizeof(unsigned long); i++)
	eth->h_dest[i] = 0xfc;
    memcpy(&(eth->h_dest[i]), &dst, sizeof(unsigned long));
    return 0;
}

static void
plip_device_clear(struct device *dev)
{
    struct net_local *nl = (struct net_local *)dev->priv;

    outb (0x00, PAR_DATA(dev));
    nl->rcv_data.state = PLIP_PK_DONE;
    nl->snd_data.state = PLIP_PK_DONE;
    nl->rcv_data.skb = NULL;
    nl->snd_data.skb = NULL;
    nl->connection = PLIP_CN_NONE;
    cli();
    dev->tbusy = 0;
    sti();
    outb(LP_PINITP|LP_PSELECP|LP_PINTEN, PAR_CONTROL(dev));
    enable_irq(dev->irq);
}

/* PLIP_ERROR --- wait till other end settled */
static int
plip_error(struct device *dev)
{
    unsigned char status;

    status = inb(PAR_STATUS(dev));
    if ((status & 0xf8) == 0x80)
	return 0;
    return 1;
}

/* PLIP_RECEIVE --- receive a byte(two nibbles)
   Returns 0 on success, 1 on failure          */
inline static int
plip_receive(unsigned short nibble_timeout, unsigned short unit_us,
	     unsigned short status_addr, unsigned short data_addr,
	     enum plip_nibble_state *ns_p, unsigned char *data_p)
{
    unsigned char c0, c1;
    unsigned int cx;

    switch (*ns_p) {
    case PLIP_NB_BEGIN:
	cx = nibble_timeout;
	while (1) {
	    c0 = inb(status_addr);
	    udelay(unit_us);
	    if ((c0 & 0x80) == 0) {
		c1 = inb(status_addr);
		if (c0 == c1)
		    break;
	    }
	    if (--cx == 0)
		return 1;
	}
	*data_p = (c0 >> 3) & 0x0f;
	outb(0x10, data_addr); /* send ACK */
	*ns_p = PLIP_NB_1;

    case PLIP_NB_1:
	cx = nibble_timeout;
	while (1) {
	    c0 = inb(status_addr);
	    udelay(unit_us);
	    if (c0 & 0x80) {
		c1 = inb(status_addr);
		if (c0 == c1)
		    break;
	    }
	    if (--cx == 0)
		return 1;
	}
	*data_p |= (c0 << 1) & 0xf0;
	outb(0x00, data_addr); /* send ACK */
	*ns_p = PLIP_NB_BEGIN;
	return 0;

    case PLIP_NB_2:
    }
}

/* PLIP_RECEIVE_PACKET --- receive a packet
   Returns 0 on success, 1 when timeout, -1 on failure */
static int
plip_receive_packet(struct device *dev)
{
    unsigned short data_addr = PAR_DATA(dev), status_addr = PAR_STATUS(dev);
    struct net_local *nl = (struct net_local *)dev->priv;
    unsigned short nibble_timeout = nl->nibble_us, unit_us = nl->unit_us;
    struct plip_local *rcv = &nl->rcv_data;
    unsigned char *lbuf;
    struct enet_statistics *stats = (struct enet_statistics *) dev->priv;

    switch (rcv->state) {
    case PLIP_PK_TRIGGER:
	rcv->state = PLIP_PK_LENGTH_LSB;
	rcv->nibble = PLIP_NB_BEGIN;

    case PLIP_PK_LENGTH_LSB:
	if (plip_receive(nibble_timeout, unit_us, status_addr, data_addr,
			 &rcv->nibble, (unsigned char *)&rcv->length))
	    return 1;
	rcv->state = PLIP_PK_LENGTH_MSB;

    case PLIP_PK_LENGTH_MSB:
	if (plip_receive(nibble_timeout, unit_us, status_addr, data_addr,
			 &rcv->nibble, (unsigned char *)&rcv->length+1))
	    return 1;
	if (rcv->length > dev->mtu || rcv->length < 8) {
	    printk("%s: bogus packet size %d.\n", dev->name, rcv->length);
	    return -1;
	}
	/* Malloc up new buffer. */
	rcv->skb = alloc_skb(rcv->length, GFP_ATOMIC);
	if (rcv->skb == NULL) {
	    printk("%s: Memory squeeze.\n", dev->name);
	    return -1;
	}
	rcv->skb->len = rcv->length;
	rcv->skb->dev = dev;
	rcv->state = PLIP_PK_DATA;
	rcv->byte = 0;
	rcv->checksum = 0;

    case PLIP_PK_DATA:
	lbuf = rcv->skb->data;
	do {
	    if (plip_receive(nibble_timeout, unit_us, status_addr, data_addr,
			     &rcv->nibble, &lbuf[rcv->byte]))
		return 1;
	    rcv->checksum += lbuf[rcv->byte];
	} while (++rcv->byte < rcv->length);
	rcv->state = PLIP_PK_CHECKSUM;

    case PLIP_PK_CHECKSUM:
	if (plip_receive(nibble_timeout, unit_us, status_addr, data_addr,
			 &rcv->nibble, &rcv->data))
	    return 1;
	if (rcv->data != rcv->checksum) {
	    stats->rx_crc_errors++;
	    if (net_debug)
		printk("%s: checksum error\n", dev->name);
	    return -1;
	}
	rcv->state = PLIP_PK_DONE;

    case PLIP_PK_DONE:
    }
    return 0;
}

/* Handle the parallel port interrupts. */
static void
plip_interrupt(int reg_ptr)
{
    int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
    struct device *dev = (struct device *) irq2dev_map[irq];
    struct net_local *nl = (struct net_local *)dev->priv;
    struct plip_local *rcv = &nl->rcv_data;
    unsigned char c0;

    if (dev == NULL) {
	if (net_debug)
	    printk ("plip_interrupt: irq %d for unknown device.\n", irq);
	return;
    }

    if (dev->interrupt)
	return;

    c0 = inb(PAR_STATUS(dev));
    if ((c0 & 0xf8) != 0xc0) {
	if (net_debug > 3)
	    printk("plip: spurious interrupt\n");
	return;
    }
    dev->interrupt = 1;
    if (net_debug > 3)
	printk("%s: interrupt.\n", dev->name);

    cli();
    switch (nl->connection) {
    case PLIP_CN_CLOSING:
	dev->tbusy = 0;
    case PLIP_CN_NONE:
    case PLIP_CN_SEND:
	sti();
	dev->last_rx = jiffies;
	rcv->state = PLIP_PK_TRIGGER;
	nl->connection = PLIP_CN_RECEIVE;
	nl->timeout_count = 0;
	queue_task(&nl->immediate, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
	break;

    case PLIP_CN_RECEIVE:
	sti();
	printk("%s: receive interrupt when receiving packet\n", dev->name);
	break;

    case PLIP_CN_ERROR:
	sti();
	printk("%s: receive interrupt in error state\n", dev->name);
	break;
    }
}

/* PLIP_SEND --- send a byte (two nibbles) 
   Returns 0 on success, 1 on failure        */
inline static int
plip_send(unsigned short nibble_timeout, unsigned short unit_us,
	  unsigned short status_addr, unsigned short data_addr,
	  enum plip_nibble_state *ns_p, unsigned char data)
{
    unsigned char c0;
    unsigned int cx;

    switch (*ns_p) {
    case PLIP_NB_BEGIN:
	outb((data & 0x0f), data_addr);
	*ns_p = PLIP_NB_1;

    case PLIP_NB_1:
	outb(0x10 | (data & 0x0f), data_addr);
	cx = nibble_timeout;
	while (1) {
	    c0 = inb(status_addr);
	    if ((c0 & 0x80) == 0) 
		break;
	    if (--cx == 0) /* time out */
		return 1;
	    udelay(unit_us);
	}
	outb(0x10 | (data >> 4), data_addr);
	*ns_p = PLIP_NB_2;

    case PLIP_NB_2:
	outb((data >> 4), data_addr);
	cx = nibble_timeout;
	while (1) {
	    c0 = inb(status_addr);
	    if (c0 & 0x80)
		break;
	    if (--cx == 0) /* time out */
		return 1;
	    udelay(unit_us);
	}
	*ns_p = PLIP_NB_BEGIN;
	return 0;
    }
}

/* PLIP_SEND_PACKET --- send a packet
   Returns 0 on success, 1 when timeout, -1 when interrupted  */
static int
plip_send_packet(struct device *dev)
{
    unsigned short data_addr = PAR_DATA(dev), status_addr = PAR_STATUS(dev);
    struct net_local *nl = (struct net_local *)dev->priv;
    unsigned short nibble_timeout = nl->nibble_us, unit_us = nl->unit_us;
    struct plip_local *snd = &nl->snd_data;
    unsigned char *lbuf = snd->skb->data;
    unsigned char c0;
    unsigned int cx;
    struct enet_statistics *stats = (struct enet_statistics *) dev->priv;

    switch (snd->state) {
    case PLIP_PK_TRIGGER:
	/* Trigger remote rx interrupt. */
	outb(0x08, PAR_DATA(dev));
	cx = nl->trigger_us;
	while (1) {
	    if (nl->connection == PLIP_CN_RECEIVE) { /* interrupted */
		stats->collisions++;
		if (net_debug > 3)
		    printk("%s: collision.\n", dev->name);
		return -1;
	    }
	    cli();
	    c0 = inb(PAR_STATUS(dev));
	    if (c0 & 0x08) {
		disable_irq(dev->irq);
		outb(LP_PINITP|LP_PSELECP, PAR_CONTROL(dev));
		if (net_debug > 3)
		    printk("+");
		/* OK, connection established! */
		snd->state = PLIP_PK_LENGTH_LSB;
		snd->nibble = PLIP_NB_BEGIN;
		nl->timeout_count = 0;
		sti();
		break;
	    }
	    sti();
	    udelay(nl->unit_us);
	    if (--cx == 0) {
		outb(0x00, PAR_DATA(dev));
		return 1;
	    }
	}

    case PLIP_PK_LENGTH_LSB:
	if (plip_send(nibble_timeout, unit_us, status_addr, data_addr,
		      &snd->nibble, snd->length & 0xff)) /* timeout */
	    return 1;
	snd->state = PLIP_PK_LENGTH_MSB;

    case PLIP_PK_LENGTH_MSB:
	if (plip_send(nibble_timeout, unit_us, status_addr, data_addr,
		      &snd->nibble, snd->length >> 8)) /* timeout */
	    return 1;
	snd->state = PLIP_PK_DATA;
	snd->byte = 0;
	snd->checksum = 0;

    case PLIP_PK_DATA:
	do {
	    if (plip_send(nibble_timeout, unit_us, status_addr, data_addr,
			  &snd->nibble, lbuf[snd->byte])) /* timeout */
		return 1;
	    snd->checksum += lbuf[snd->byte];
	} while (++snd->byte < snd->length);
	snd->state = PLIP_PK_CHECKSUM;

    case PLIP_PK_CHECKSUM:
	if (plip_send(nibble_timeout, unit_us, status_addr, data_addr,
		      &snd->nibble, snd->checksum)) /* timeout */
	    return 1;

	dev_kfree_skb(snd->skb, FREE_WRITE);
	stats->tx_packets++;

    case PLIP_PK_DONE:
    }
    return 0;
}

static int plip_config(struct device *dev, struct ifmap *map)
{
	if(dev->flags&IFF_UP)
		return -EBUSY;
/*
 *	We could probe this for verification, but since they told us
 *	to do it then they can suffer.
 */
	if(map->base_addr!= (unsigned short)-1)
		dev->base_addr=map->base_addr;
	if(map->irq!= (unsigned char)-1)
		dev->irq= map->irq;
	return 0;
}

static int plip_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
	struct net_local *nl = (struct net_local *) dev->priv;
	struct plipconf *pc = (struct plipconf *) &rq->ifr_data;
	
	switch(pc->pcmd)
	{
		case PLIP_GET_TIMEOUT:
			pc->trigger=nl->trigger_us;
			pc->nibble=nl->nibble_us;
			pc->unit=nl->unit_us;
			break;
		case PLIP_SET_TIMEOUT:
			nl->trigger_us=pc->trigger;
			nl->nibble_us=pc->nibble;
			nl->unit_us=pc->unit;
			break;
		default:
			return -EOPNOTSUPP;
	}
	return 0;
}


#ifdef MODULE
char kernel_version[] = UTS_RELEASE;

static struct device dev_plip0 = 
{
	"plip0" /*"plip"*/,
	0, 0, 0, 0,		/* memory */
	0x3BC, 5,		/* base, irq */
	0, 0, 0, NULL, plip_init 
};

static struct device dev_plip1 = 
{
	"plip1" /*"plip"*/,
	0, 0, 0, 0,		/* memory */
	0x378, 7,		/* base, irq */
	0, 0, 0, NULL, plip_init 
};

static struct device dev_plip2 = 
{
	"plip2" /*"plip"*/,
	0, 0, 0, 0,		/* memory */
	0x278, 2,		/* base, irq */
	0, 0, 0, NULL, plip_init 
};

int
init_module(void)
{
    int devices=0;

    if (register_netdev(&dev_plip0) != 0)
	devices++;
    if (register_netdev(&dev_plip1) != 0)
	devices++;
    if (register_netdev(&dev_plip2) != 0)
	devices++;
    if (devices == 0)
	return -EIO;
    return 0;
}

void
cleanup_module(void)
{
    if (MOD_IN_USE)
	printk("plip: device busy, remove delayed\n");
    else {
	if (dev_plip0.priv) {
	    unregister_netdev(&dev_plip0);
	    release_region(PAR_DATA(&dev_plip0), 3);
	    kfree_s(dev_plip0.priv, sizeof(struct net_local));
	    dev_plip0.priv = NULL;
	}
	if (dev_plip1.priv) {
	    unregister_netdev(&dev_plip1);
	    release_region(PAR_DATA(&dev_plip1), 3);
	    kfree_s(dev_plip1.priv, sizeof(struct net_local));
	    dev_plip1.priv = NULL;
	}
	if (dev_plip2.priv) {
	    unregister_netdev(&dev_plip2);
	    release_region(PAR_DATA(&dev_plip2), 3);
	    kfree_s(dev_plip2.priv, sizeof(struct net_local));
	    dev_plip2.priv = NULL;
	}
    }
}
#endif /* MODULE */

/*
 * Local variables:
 * compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -pipe -m486 -I../../net/inet -c plip.c"
 * c-indent-level: 4
 * c-continued-statement-offset: 4
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * version-control: t
 * kept-new-versions: 10
 * End:
 */
