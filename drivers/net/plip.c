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
    "PLIP.010+ gniibe@mri.co.jp\n";

#include <linux/config.h>

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
#include <netinet/in.h>
#include <errno.h>
#include <linux/delay.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_plip.h>

#include <linux/timer.h>
#include <linux/ioport.h>
#include <asm/bitops.h>
#include <asm/irq.h>

#ifdef MODULE
#include <linux/module.h>
#include "../../tools/version.h"
#endif

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 3
#endif
static unsigned int net_debug = NET_DEBUG;

/* constants */
#define PLIP_MTU 1500

/* In micro second */
#define PLIP_DELAY_UNIT		   1

/* Connection time out = PLIP_TRIGGER_WAIT * PLIP_DELAY_UNIT usec */
#define PLIP_TRIGGER_WAIT	 500

/* Nibble time out = PLIP_NIBBLE_WAIT * PLIP_DELAY_UNIT usec */
#define PLIP_NIBBLE_WAIT	3000

#define PAR_DATA(dev)		(dev->base_addr+0)
#define PAR_STATUS(dev)		(dev->base_addr+1)
#define PAR_CONTROL(dev)	(dev->base_addr+2)

/* Index to functions, as function prototypes. */
static int plip_tx_packet(struct sk_buff *skb, struct device *dev);
static int plip_open(struct device *dev);
static int plip_close(struct device *dev);
static int plip_header(unsigned char *buff, struct device *dev,
		       unsigned short type, void *dest,
		       void *source, unsigned len, struct sk_buff *skb);
static struct enet_statistics *plip_get_stats(struct device *dev);
static int plip_rebuild_header(void *buff, struct device *dev,
			       unsigned long raddr, struct sk_buff *skb);

enum plip_state {
    PLIP_ST_DONE=0,
    PLIP_ST_TRANSMIT_BEGIN,
    PLIP_ST_TRIGGER,
    PLIP_ST_LENGTH_LSB,
    PLIP_ST_LENGTH_MSB,
    PLIP_ST_DATA,
    PLIP_ST_CHECKSUM,
    PLIP_ST_ERROR
};

enum plip_nibble_state {
    PLIP_NST_BEGIN,
    PLIP_NST_1,
    PLIP_NST_2,
    PLIP_NST_END
};

#define PLIP_STATE_STRING(x) \
    (((x) == PLIP_ST_DONE)?"0":\
     ((x) == PLIP_ST_TRANSMIT_BEGIN)?"b":\
     ((x) == PLIP_ST_TRIGGER)?"t":\
     ((x) == PLIP_ST_LENGTH_LSB)?"l":\
     ((x) == PLIP_ST_LENGTH_MSB)?"m":\
     ((x) == PLIP_ST_DATA)?"d":\
     ((x) == PLIP_ST_CHECKSUM)?"s":"B")

struct plip_local {
    enum plip_state state;
    enum plip_nibble_state nibble;
    unsigned short length;
    unsigned short count;
    unsigned short byte;
    unsigned char  checksum;
    unsigned char  data;
    struct sk_buff *skb;
};

struct net_local {
    struct enet_statistics e;
    struct timer_list tl;
    struct plip_local snd_data;
    struct plip_local rcv_data;
    unsigned long  trigger_us;
    unsigned long  nibble_us;
    unsigned long  unit_us;
};

/* Routines used internally. */
static void plip_device_clear(struct device *dev);
static void plip_error(struct device *dev);
static int plip_receive(struct device *dev, enum plip_nibble_state *ns_p,
			unsigned char *data_p);
static void plip_receive_packet(struct device *dev);
static void plip_interrupt(int reg_ptr);
static int plip_send(struct device *dev, enum plip_nibble_state *ns_p,
		     unsigned char data);
static void plip_send_packet(struct device *dev);
static int plip_ioctl(struct device *dev, struct ifreq *ifr);
static int plip_config(struct device *dev, struct ifmap *map);


int
plip_init(struct device *dev)
{
    int i;
    struct net_local *pl;

    /* Check that there is something at base_addr. */
    outb(0x00, PAR_CONTROL(dev));
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
	autoirq_setup(0);
	outb(0x00, PAR_CONTROL(dev));
	outb(0x10, PAR_CONTROL(dev));
	outb(0x00, PAR_CONTROL(dev));
	dev->irq = autoirq_report(1);
	if (dev->irq)
	    printk(", probed IRQ %d.\n", dev->irq);
	else {
	    printk(", failed to detect IRQ line.\n");
	    return -ENODEV;
	}
    }

    /* Initialize the device structure. */
    dev->rmem_end	= (unsigned long) NULL;
    dev->rmem_start	= (unsigned long) NULL;
    dev->mem_end	= (unsigned long) NULL;
    dev->mem_start	= (unsigned long) NULL;

    dev->priv = kmalloc(sizeof (struct net_local), GFP_KERNEL);
    memset(dev->priv, 0, sizeof(struct net_local));
    pl=dev->priv;
    
    pl->trigger_us	=	PLIP_TRIGGER_WAIT;
    pl->nibble_us	=	PLIP_NIBBLE_WAIT;

    dev->mtu			= PLIP_MTU;
    dev->hard_start_xmit	= plip_tx_packet;
    dev->open			= plip_open;
    dev->stop			= plip_close;
    dev->hard_header		= plip_header;
    dev->type_trans		= eth_type_trans;
    dev->get_stats 		= plip_get_stats;
    dev->set_config		= plip_config;
    dev->do_ioctl		= plip_ioctl;
    

    dev->hard_header_len	= ETH_HLEN;
    dev->addr_len		= ETH_ALEN;
    dev->type			= ARPHRD_ETHER;
    dev->rebuild_header 	= plip_rebuild_header;

    for (i = 0; i < DEV_NUMBUFFS; i++)
	skb_queue_head_init(&dev->buffs[i]);

    for (i = 0; i < dev->addr_len; i++) {
	dev->broadcast[i]=0xff;
	dev->dev_addr[i] = 0;
    }

    /* New-style flags. */
    dev->flags		= 0;
    dev->family		= AF_INET;
    dev->pa_addr	= 0;
    dev->pa_brdaddr	= 0;
    dev->pa_dstaddr	= 0;
    dev->pa_mask	= 0;
    dev->pa_alen	= sizeof(unsigned long);

    return 0;
}

static int
plip_tx_packet (struct sk_buff *skb, struct device *dev)
{
    struct net_local *lp = (struct net_local *)dev->priv;
    struct plip_local *snd = &lp->snd_data;

    if (dev->tbusy) {
	/* it is sending a packet now */
	int tickssofar = jiffies - dev->trans_start;
	if (tickssofar < 100)	/* please try later, again */
	    return 1;

	/* something wrong... force to reset */
	printk("%s: transmit timed out, cable problem??\n", dev->name);
	plip_device_clear(dev);
    }

    /* If some higher layer thinks we've missed an tx-done interrupt
       we are passed NULL. Caution: dev_tint() handles the cli()/sti()
       itself. */
    if (skb == NULL) {
	dev_tint(dev);
	return 0;
    }

    cli();
    if (set_bit(0, (void *)&dev->tbusy) != 0) {
	sti();
	printk("%s: Transmitter access conflict.\n", dev->name);
	return 1;
    }
    if (dev->interrupt) {
	sti();
	return 1;
    }
    snd->state = PLIP_ST_TRANSMIT_BEGIN;
    sti();

    dev->trans_start = jiffies;
    if (net_debug > 4)
	printk("Ss");

    if (skb->len > dev->mtu) {
	printk("%s: packet too big, %d.\n", dev->name, (int)skb->len);
	return 0;
    }

    snd->skb = skb;
    snd->length = skb->len;
    snd->count = 0;

    cli();
    if (dev->interrupt == 0) {
	/* set timer */
	lp->tl.expires = 0;
	lp->tl.data = (unsigned long)dev;
	lp->tl.function = (void (*)(unsigned long))plip_send_packet;
	add_timer(&lp->tl);
	mark_bh(TIMER_BH);
    }
    snd->state = PLIP_ST_TRIGGER;
    sti();

    return 0;
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is
   run.

   This routine gets exclusive access to the parallel port by allocating
   its IRQ line.
   */
static int
plip_open(struct device *dev)
{
    struct net_local *lp = (struct net_local *)dev->priv;
    struct plip_local *rcv = &lp->rcv_data;

    rcv->skb = alloc_skb(dev->mtu, GFP_KERNEL);
    if (rcv->skb == NULL) {
	printk("%s: couldn't get memory for receiving packet.\n", dev->name);
	return -EAGAIN;
    }
    rcv->skb->len = dev->mtu;
    rcv->skb->dev = dev;
    cli();
    if (request_irq(dev->irq , plip_interrupt) != 0) {
	sti();
	printk("%s: couldn't get IRQ %d.\n", dev->name, dev->irq);
	return -EAGAIN;
    }
    irq2dev_map[dev->irq] = dev;
    sti();
    /* enable rx interrupt. */
    outb(0x10, PAR_CONTROL(dev));
    plip_device_clear(dev);
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
    struct net_local *lp = (struct net_local *)dev->priv;

    dev->tbusy = 1;
    dev->start = 0;
    cli();
    free_irq(dev->irq);
    irq2dev_map[dev->irq] = NULL;
    sti();
    outb(0x00, PAR_DATA(dev));
    /* make sure that we don't register the timer */
    del_timer(&lp->tl);
    /* release the interrupt. */
    outb(0x00, PAR_CONTROL(dev));
#ifdef MODULE
    MOD_DEC_USE_COUNT;
#endif        
    return 0;
}

/* Fill in the MAC-level header. */
static int
plip_header(unsigned char *buff, struct device *dev,
	    unsigned short type, void *daddr,
	    void *saddr, unsigned len, struct sk_buff *skb)
{
    int i;

    if (dev->dev_addr[0] == 0) {
	for (i=0; i < ETH_ALEN - sizeof(unsigned long); i++)
	    dev->dev_addr[i] = 0xfc;
	memcpy(&(dev->dev_addr[i]), &dev->pa_addr, sizeof(unsigned long));
    }

    return eth_header(buff, dev, type, daddr, saddr, len, skb);
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
	printk("plip_rebuild_header: Don't know how to resolve type %d addreses?\n",(int)eth->h_proto);
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
    struct net_local *lp = (struct net_local *)dev->priv;

    outb (0x00, PAR_DATA(dev));
    lp->snd_data.state = PLIP_ST_DONE;
    lp->rcv_data.state = PLIP_ST_DONE;
    cli();
    dev->tbusy = 0;
    dev->interrupt = 0;
    /* make sure that we don't register the timer */
    del_timer(&lp->tl);
    sti();
    enable_irq(dev->irq);
}

static void
plip_error(struct device *dev)
{
    struct net_local *lp = (struct net_local *)dev->priv;
    struct plip_local *snd = &((struct net_local *)dev->priv)->snd_data;
    struct plip_local *rcv = &lp->rcv_data;
    unsigned char status;

    outb(0x00, PAR_DATA(dev));
    cli();
    del_timer(&lp->tl);
    snd->state = PLIP_ST_ERROR;
    sti();
    if (rcv->skb == NULL) {
	rcv->skb = alloc_skb(dev->mtu, GFP_ATOMIC);
	if (rcv->skb == NULL) {
	    printk("%s: couldn't get memory.\n", dev->name);
	    goto again;
	}
	rcv->skb->len = dev->mtu;
	rcv->skb->dev = dev;
    }

    status = inb(PAR_STATUS(dev));
    if ((status & 0xf8) == 0x80) {
	plip_device_clear(dev);
	mark_bh(NET_BH);
    } else {
    again:
	lp->tl.expires = 1;
	lp->tl.data = (unsigned long)dev;
	lp->tl.function = (void (*)(unsigned long))plip_error;
	add_timer(&lp->tl);
    }
}

/* PLIP_RECEIVE --- receive a byte(two nibbles)
   Return 0 on success, return 1 on failure */
static int
plip_receive(struct device *dev, enum plip_nibble_state *ns_p,
	     unsigned char *data_p)
{
    unsigned char c0, c1;
    unsigned int cx;
    struct net_local *nl=(struct net_local *)dev->priv;

    while (1)
	switch (*ns_p) {
	case PLIP_NST_BEGIN:
	    cx = nl->nibble_us;
	    while (1) {
		c0 = inb(PAR_STATUS(dev));
		udelay(PLIP_DELAY_UNIT);
		if ((c0 & 0x80) == 0) {
		    c1 = inb(PAR_STATUS(dev));
		    if (c0 == c1)
			break;
		}
		if (--cx == 0)
		    return 1;
	    }
	    *data_p = (c0 >> 3) & 0x0f;
	    outb(0x10, PAR_DATA(dev)); /* send ACK */
	    *ns_p = PLIP_NST_1;
	    break;

	case PLIP_NST_1:
	    cx = nl->nibble_us;
	    while (1) {
		c0 = inb(PAR_STATUS(dev));
		udelay(PLIP_DELAY_UNIT);
		if (c0 & 0x80) {
		    c1 = inb(PAR_STATUS(dev));
		    if (c0 == c1)
			break;
		}
		if (--cx == 0)
		    return 1;
	    }
	    *data_p |= (c0 << 1) & 0xf0;
	    outb(0x00, PAR_DATA(dev)); /* send ACK */
	    *ns_p = PLIP_NST_2;
	    return 0;
	    break;

	default:
	    printk("plip:receive state error\n");
	    *ns_p = PLIP_NST_2;	    
	    return 1;
	    break;
	}
}

static void
plip_receive_packet(struct device *dev)
{
    struct net_local *lp = (struct net_local *)dev->priv;
    struct enet_statistics *stats = (struct enet_statistics *) dev->priv;
    struct plip_local *snd = &lp->snd_data;
    struct plip_local *rcv = &lp->rcv_data;
    unsigned char *lbuf = rcv->skb->data;
    unsigned char c0;
    unsigned char *s =  PLIP_STATE_STRING(rcv->state);

    if (net_debug > 4)
	printk("R%s",s);

    while (1) {
	switch (rcv->state) {
	case PLIP_ST_TRIGGER:
	    disable_irq(dev->irq);
	    rcv->state = PLIP_ST_LENGTH_LSB;
	    rcv->nibble = PLIP_NST_BEGIN;
	    break;

	case PLIP_ST_LENGTH_LSB:
	    if (plip_receive(dev, &rcv->nibble, (unsigned char *)&rcv->length))
		goto try_again;

	    rcv->state = PLIP_ST_LENGTH_MSB;
	    rcv->nibble = PLIP_NST_BEGIN;
	    break;

	case PLIP_ST_LENGTH_MSB:
	    if (plip_receive(dev, &rcv->nibble,
			     (unsigned char *)&rcv->length+1))
		goto try_again;

	    if (rcv->length > rcv->skb->len || rcv->length < 8) {
		printk("%s: bogus packet size %d.\n", dev->name, rcv->length);
		plip_error(dev);
		return;
	    }
	    rcv->skb->len = rcv->length;
	    rcv->state = PLIP_ST_DATA;
	    rcv->nibble = PLIP_NST_BEGIN;
	    rcv->byte = 0;
	    rcv->checksum = 0;
	    break;

	case PLIP_ST_DATA:
	    if (plip_receive(dev, &rcv->nibble, &lbuf[rcv->byte]))
		goto try_again;

	    rcv->checksum += lbuf[rcv->byte];
	    rcv->byte++;
	    rcv->nibble = PLIP_NST_BEGIN;
	    if (rcv->byte == rcv->length)
		rcv->state = PLIP_ST_CHECKSUM;
	    break;

	case PLIP_ST_CHECKSUM:
	    if (plip_receive(dev, &rcv->nibble, &rcv->data))
		goto try_again;
	    if (rcv->data != rcv->checksum) {
		stats->rx_crc_errors++;
		if (net_debug)
		    printk("%s: checksum error\n", dev->name);
		plip_error(dev);
		return;
	    }

	    rcv->state = PLIP_ST_DONE;
	    netif_rx(rcv->skb);

	    /* Malloc up new buffer. */
	    rcv->skb = alloc_skb(dev->mtu, GFP_ATOMIC);
	    if (rcv->skb == NULL) {
		printk("%s: Memory squeeze.\n", dev->name);
		plip_error(dev);
		return;
	    }
	    rcv->skb->len = dev->mtu;
	    rcv->skb->dev = dev;
	    stats->rx_packets++;
	    if (net_debug > 4)
		printk("R(%4.4d)", rcv->length);

	    if (snd->state == PLIP_ST_TRANSMIT_BEGIN) {
		dev->interrupt = 0;
		enable_irq(dev->irq);
	    } else if (snd->state == PLIP_ST_TRIGGER) {
		cli();
		dev->interrupt = 0;
		if (net_debug > 3)
		    printk("%%");
		lp->tl.expires = 0;
		lp->tl.data = (unsigned long)dev;
		lp->tl.function
		    = (void (*)(unsigned long))plip_send_packet;
		add_timer(&lp->tl);
		mark_bh(TIMER_BH);
		enable_irq(dev->irq);
		sti();
	    } else
		plip_device_clear(dev);
	    return;

	default:
	    printk("plip: bad STATE?? %04d", rcv->state);
	    plip_device_clear(dev);
	    return;
	}
    }

 try_again:
    if (++rcv->count > 2) { /* timeout */
	s = PLIP_STATE_STRING(rcv->state);
	c0 = inb(PAR_STATUS(dev));
	stats->rx_dropped++;
	if (net_debug > 1)
	    printk("%s: receive timeout(%s,%02x)... reset interface.\n",
		   dev->name, s, (unsigned int)c0);
	plip_error(dev);
    } else {
	s =  PLIP_STATE_STRING(rcv->state);
	if (net_debug > 3)
	    printk("r%s",s);

	/* set timer */
	lp->tl.expires = 1;
	lp->tl.data = (unsigned long)dev;
	lp->tl.function = (void (*)(unsigned long))plip_receive_packet;
	add_timer(&lp->tl);
    }
}

/* Handle the parallel port interrupts. */
static void
plip_interrupt(int reg_ptr)
{
    int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
    struct device *dev = irq2dev_map[irq];
    struct net_local *lp = (struct net_local *)dev->priv;
    struct plip_local *rcv = &lp->rcv_data;
    struct plip_local *snd = &lp->snd_data;
    unsigned char c0;

    if (dev == NULL) {
	if (net_debug)
	    printk ("plip_interrupt: irq %d for unknown device.\n", irq);
	return;
    }

    if (dev->interrupt) {
	if (net_debug > 3)
	    printk("2");
	return;
    }

    if (dev->tbusy) {
	if (snd->state > PLIP_ST_TRIGGER) {
	    printk("%s: rx interrupt in transmission\n", dev->name);
	    return;
	}
	if (net_debug > 3)
	    printk("3");
    }

    if (snd->state == PLIP_ST_ERROR)
	return;

    c0 = inb(PAR_STATUS(dev));
    if ((c0 & 0xf8) != 0xc0) {
	if (net_debug > 3)
	    printk("?");
	return;
    }

    dev->interrupt = 1;

    if (net_debug > 3)
	printk("!");

    dev->last_rx = jiffies;
    outb(0x01, PAR_DATA(dev));   /* send ACK */
    rcv->state = PLIP_ST_TRIGGER;
    rcv->count = 0;

    /* set timer */
    del_timer(&lp->tl);
    lp->tl.expires = 0;
    lp->tl.data = (unsigned long)dev;
    lp->tl.function = (void (*)(unsigned long))plip_receive_packet;
    add_timer(&lp->tl);
    mark_bh (TIMER_BH);
}

/* PLIP_SEND --- send a byte (two nibbles) 
   Return 0 on success, return 1 on failure */
static int
plip_send(struct device *dev, enum plip_nibble_state *ns_p, unsigned char data)
{
    unsigned char c0;
    unsigned int cx;
    struct net_local *nl= (struct net_local *)dev->priv;

    while (1)
	switch (*ns_p) {
	case PLIP_NST_BEGIN:
	    outb((data & 0x0f), PAR_DATA(dev));
	    *ns_p = PLIP_NST_1;
	    break;

	case PLIP_NST_1:
	    outb(0x10 | (data & 0x0f), PAR_DATA(dev));
	    cx = nl->nibble_us;
	    while (1) {
		c0 = inb(PAR_STATUS(dev));
		if ((c0 & 0x80) == 0) 
		    break;
		if (--cx == 0) /* time out */
		    return 1;
	    }
	    outb(0x10 | (data >> 4), PAR_DATA(dev));
	    *ns_p = PLIP_NST_2;
	    break;

	case PLIP_NST_2:
	    outb((data >> 4), PAR_DATA(dev));
	    cx = nl->nibble_us;
	    while (1) {
		c0 = inb(PAR_STATUS(dev));
		if (c0 & 0x80)
		    break;
		if (--cx == 0) /* time out */
		    return 1;
	    }
	    return 0;

	default:
	    printk("plip:send state error\n");
	    return 1;
	}
}

static void
plip_send_packet(struct device *dev)
{
    struct enet_statistics *stats = (struct enet_statistics *) dev->priv;
    struct net_local *lp = (struct net_local *)dev->priv;
    struct plip_local *snd = &lp->snd_data;
    unsigned char *lbuf = snd->skb->data;
    unsigned char c0;
    unsigned int cx;
    unsigned char *s =  PLIP_STATE_STRING(snd->state);

    if (net_debug > 4)
	printk("S%s",s);

    while (1) {
	switch (snd->state) {
	case PLIP_ST_TRIGGER:
	    /* Trigger remote rx interrupt. */
	    outb(0x08, PAR_DATA(dev));
	    cx = lp->trigger_us;
	    while (1) {
		if (dev->interrupt) {
		    stats->collisions++;
		    if (net_debug > 3)
			printk("$");
		    mark_bh(TIMER_BH);
		    return;
		}
		cli();
		c0 = inb(PAR_STATUS(dev));
		if (c0 & 0x08) {
		    disable_irq(dev->irq);
		    if (net_debug > 3)
			printk("+");
		    /* OK, connection established! */
		    snd->state = PLIP_ST_LENGTH_LSB;
		    snd->nibble = PLIP_NST_BEGIN;
		    snd->count = 0;
		    sti();
		    break;
		}
		sti();
		udelay(PLIP_DELAY_UNIT);
		if (--cx == 0) {
		    outb(0x00, PAR_DATA(dev));
		    goto try_again;
		}
	    }
	    break;

	case PLIP_ST_LENGTH_LSB:
	    if (plip_send(dev, &snd->nibble, snd->length & 0xff)) /* timeout */
		goto try_again;

	    snd->state = PLIP_ST_LENGTH_MSB;
	    snd->nibble = PLIP_NST_BEGIN;
	    break;

	case PLIP_ST_LENGTH_MSB:
	    if (plip_send(dev, &snd->nibble, snd->length >> 8)) /* timeout */
		goto try_again;

	    snd->state = PLIP_ST_DATA;
	    snd->nibble = PLIP_NST_BEGIN;
	    snd->byte = 0;
	    snd->checksum = 0;
	    break;

	case PLIP_ST_DATA:
	    if (plip_send(dev, &snd->nibble, lbuf[snd->byte])) /* timeout */
		goto try_again;

	    snd->nibble = PLIP_NST_BEGIN;
	    snd->checksum += lbuf[snd->byte];
	    snd->byte++;
	    if (snd->byte == snd->length)
		snd->state = PLIP_ST_CHECKSUM;
	    break;

	case PLIP_ST_CHECKSUM:
	    if (plip_send(dev, &snd->nibble, snd->checksum)) /* timeout */
		goto try_again;

	    mark_bh(NET_BH);
	    plip_device_clear(dev);
	    if (net_debug > 4)
		printk("S(%4.4d)", snd->length);
	    dev_kfree_skb(snd->skb, FREE_WRITE);
	    stats->tx_packets++;
	    return;

	default:
	    printk("plip: BAD STATE?? %04d", snd->state);
	    plip_device_clear(dev);
	    return;
	}
    }

 try_again:
    if (++snd->count > 3) {
	/* timeout */
	s =  PLIP_STATE_STRING(snd->state);
	c0 = inb(PAR_STATUS(dev));
	stats->tx_errors++;
	stats->tx_aborted_errors++;
	if (net_debug > 1)
	    printk("%s: transmit timeout(%s,%02x)... reset interface.\n",
		   dev->name, s, (unsigned int)c0);
	dev_kfree_skb(snd->skb,FREE_WRITE);
	plip_error(dev);
    } else {
	s =  PLIP_STATE_STRING(snd->state);
	if (net_debug > 3)
	    printk("s%s",s);

	cli();
	if (dev->interrupt == 0) {
	    /* set timer */
	    lp->tl.expires = 1;
	    lp->tl.data = (unsigned long)dev;
	    lp->tl.function = (void (*)(unsigned long))plip_send_packet;
	    add_timer(&lp->tl);
	}
	sti();
    }
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

static int plip_ioctl(struct device *dev, struct ifreq *rq)
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
	int err;

	if ( ((err=register_netdev(&dev_plip0)) == 0) &&
	     ((err=register_netdev(&dev_plip1)) == 0) &&
	     ((err=register_netdev(&dev_plip2)) == 0)
	   )
	{
		if(err==-EEXIST)
			printk("plip devices already present. Module not loaded.\n");
		return err;
	}
	return 0;
}

void
cleanup_module(void)
{
	if (MOD_IN_USE)
		printk("plip: device busy, remove delayed\n");
	else
	{
		unregister_netdev(&dev_plip0);
		if(dev_plip0.priv)
		{
			kfree_s(dev_plip0.priv,sizeof(struct net_local));
			dev_plip0.priv=NULL;
		}
		unregister_netdev(&dev_plip1);
		if(dev_plip1.priv)
		{
			kfree_s(dev_plip1.priv,sizeof(struct net_local));
			dev_plip0.priv=NULL;
		}
		unregister_netdev(&dev_plip2);
		if(dev_plip2.priv)
		{
			kfree_s(dev_plip2.priv,sizeof(struct net_local));
			dev_plip2.priv=NULL;
		}
	}
}
#endif /* MODULE */

/*
 * Local variables:
 * compile-command: "gcc -D__KERNEL__ -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -c plip.c"
 * c-indent-level: 4
 * c-continued-statement-offset: 4
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * version-control: t
 * kept-new-versions: 10
 * End:
 */
