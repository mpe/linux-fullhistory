/*======================================================================
    fmvj18x_cs.c,v 1.9 1996/08/06 03:13:53 root Exp

    A fmvj18x (and its compatibles) PCMCIA client driver

    Contributed by Shingo Fujimoto, shingo@flab.fujitsu.co.jp

    TDK LAK-CD021 and CONTEC C-NET(PC)C support added by 
    Nobuhiro Katayama, kata-n@po.iijnet.or.jp

    The PCMCIA client code is based on code written by David Hinds.
    Network code is based on the "FMV-18x driver" by Yutaka TAMIYA
    but is actually largely Donald Becker's AT1700 driver, which
    carries the following attribution:

    Written 1993-94 by Donald Becker.

    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.
    
    This software may be used and distributed according to the terms
    of the GNU Public License, incorporated herein by reference.
    
    The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
    Center of Excellence in Space Data and Information Sciences
    Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771
    
======================================================================*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/system.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>

/*
   All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If
   you do not define PCMCIA_DEBUG at all, all the debug code will be
   left out.  If you compile with PCMCIA_DEBUG=0, the debug code will
   be present but disabled -- but it can then be enabled for specific
   modules at load time with a 'pc_debug=#' option to insmod.
*/
#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
MODULE_PARM(pc_debug, "i");
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
#else
#define DEBUG(n, args...)
#endif

/*
   For debugging this driver you may need more information.
   To enable printing registers or status, set 'fmvj18x_debug=#' option .
 */
#ifdef FMVJ18X_DEBUG
static int fmvj18x_debug = FMVJ18X_DEBUG;
#else
static int fmvj18x_debug = 2;
#endif /* FMVJ18X_DEBUG */

/* Bit map of interrupts to choose from */
/* This means pick from 15, 14, 12, 11, 10, 9, 7, 5, 4, and 3 */
static u_int irq_mask = 0xdeb8;
static int irq_list[4] = { -1 };

/* SRAM configuration */
/* 0:4KB*2 TX buffer   else:8KB*2 TX buffer */
static int sram_config = 0;

MODULE_PARM(irq_mask, "i");
MODULE_PARM(irq_list, "1-4i");
MODULE_PARM(sram_config, "i");

/*====================================================================*/
/* 
   driver version infomation 
 */
#ifdef PCMCIA_DEBUG
static char *version =
 "fmvj18x_cs.c,v 1.9 1996/08/06 03:13:53 root Exp";
#endif

/*====================================================================*/
/*
    PCMCIA event handlers
 */
static void fmvj18x_config(dev_link_t *link);
static void fmvj18x_release(u_long arg);
static int fmvj18x_event(event_t event, int priority,
			  event_callback_args_t *args);
static int fmvj18x_init(struct net_device *dev);
static dev_link_t *fmvj18x_attach(void);
static void fmvj18x_detach(dev_link_t *);

/*
    LAN controler(MBH86960A) specific routines
 */
static int fjn_config(struct net_device *dev, struct ifmap *map);
static int fjn_open(struct net_device *dev);
static int fjn_close(struct net_device *dev);
static int fjn_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void fjn_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void fjn_rx(struct net_device *dev);
static void fjn_reset(struct net_device *dev);
static struct net_device_stats *fjn_get_stats(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);

static dev_info_t dev_info = "fmvj18x_cs";
static dev_link_t *dev_list = NULL;

/*
    card type
 */
typedef enum { MBH10302, MBH10304, TDK, CONTEC, LA501 } cardtype_t;

/*
    driver specific data structure
*/
typedef struct local_info_t {
    dev_node_t node;
    struct net_device_stats stats;
    long open_time;
    uint tx_started:1;
    uint tx_queue;
    u_short tx_queue_len;
    cardtype_t cardtype;
    u_short sent;
    u_char mc_filter[8];
} local_info_t;

#define MC_FILTERBREAK 64

/*====================================================================*/
/* 
    ioport offset from the base address 
 */
#define TX_STATUS               0 /* transmit status register */
#define RX_STATUS               1 /* receive status register */
#define TX_INTR                 2 /* transmit interrupt mask register */
#define RX_INTR                 3 /* receive interrupt mask register */
#define TX_MODE                 4 /* transmit mode register */
#define RX_MODE                 5 /* receive mode register */
#define CONFIG_0                6 /* configuration register 0 */
#define CONFIG_1                7 /* configuration register 1 */

#define NODE_ID                 8 /* node ID register            (bank 0) */
#define MAR_ADR                 8 /* multicast address registers (bank 1) */

#define DATAPORT                8 /* buffer mem port registers   (bank 2) */
#define TX_START               10 /* transmit start register */
#define COL_CTRL               11 /* 16 collision control register */
#define BMPR12                 12 /* reserved */
#define BMPR13                 13 /* reserved */
#define RX_SKIP                14 /* skip received packet register */

#define LAN_CTRL               16 /* LAN card control register */

#define MAC_ID               0x1a /* hardware address */

/* 
    control bits 
 */
#define ENA_TMT_OK           0x80
#define ENA_TMT_REC          0x20
#define ENA_COL              0x04
#define ENA_16_COL           0x02
#define ENA_TBUS_ERR         0x01

#define ENA_PKT_RDY          0x80
#define ENA_BUS_ERR          0x40
#define ENA_LEN_ERR          0x08
#define ENA_ALG_ERR          0x04
#define ENA_CRC_ERR          0x02
#define ENA_OVR_FLO          0x01

/* flags */
#define F_TMT_RDY            0x80 /* can accept new packet */
#define F_NET_BSY            0x40 /* carrier is detected */
#define F_TMT_OK             0x20 /* send packet successfully */
#define F_SRT_PKT            0x10 /* short packet error */
#define F_COL_ERR            0x04 /* collision error */
#define F_16_COL             0x02 /* 16 collision error */
#define F_TBUS_ERR           0x01 /* bus read error */

#define F_PKT_RDY            0x80 /* packet(s) in buffer */
#define F_BUS_ERR            0x40 /* bus read error */
#define F_LEN_ERR            0x08 /* short packet */
#define F_ALG_ERR            0x04 /* frame error */
#define F_CRC_ERR            0x02 /* CRC error */
#define F_OVR_FLO            0x01 /* overflow error */

#define F_BUF_EMP            0x40 /* receive buffer is empty */

#define F_SKP_PKT            0x05 /* drop packet in buffer */

/* default bitmaps */
#define D_TX_INTR  ( ENA_TMT_OK )
#define D_RX_INTR  ( ENA_PKT_RDY | ENA_LEN_ERR \
		   | ENA_ALG_ERR | ENA_CRC_ERR | ENA_OVR_FLO )
#define TX_STAT_M  ( F_TMT_RDY )
#define RX_STAT_M  ( F_PKT_RDY | F_LEN_ERR \
                   | F_ALG_ERR | F_CRC_ERR | F_OVR_FLO )

/* commands */
#define D_TX_MODE            0x06 /* no tests, detect carrier */
#define ID_MATCHED           0x02 /* (RX_MODE) */
#define RECV_ALL             0x03 /* (RX_MODE) */
#define CONFIG0_DFL          0x5a /* 16bit bus, 4K x 2 Tx queues */
#define CONFIG0_DFL_1        0x5e /* 16bit bus, 8K x 2 Tx queues */
#define CONFIG0_RST          0xda /* Data Link Controler off (CONFIG_0) */
#define CONFIG0_RST_1        0xde /* Data Link Controler off (CONFIG_0) */
#define BANK_0               0xa0 /* bank 0 (CONFIG_1) */
#define BANK_1               0xa4 /* bank 1 (CONFIG_1) */
#define BANK_2               0xa8 /* bank 2 (CONFIG_1) */
#define CHIP_OFF             0x80 /* contrl chip power off (CONFIG_1) */
#define DO_TX                0x80 /* do transmit packet */
#define SEND_PKT             0x81 /* send a packet */
#define AUTO_MODE            0x07 /* Auto skip packet on 16 col detected */
#define MANU_MODE            0x03 /* Stop and skip packet on 16 col */
#define TDK_AUTO_MODE        0x47 /* Auto skip packet on 16 col detected */
#define TDK_MANU_MODE        0x43 /* Stop and skip packet on 16 col */
#define INTR_OFF             0x0d /* LAN controler ignores interrupts */
#define INTR_ON              0x1d /* LAN controler will catch interrupts */

/*======================================================================

    This bit of code is used to avoid unregistering network devices
    at inappropriate times.  2.2 and later kernels are fairly picky
    about when this can happen.
    
======================================================================*/

static void flush_stale_links(void)
{
    dev_link_t *link, *next;
    for (link = dev_list; link; link = next) {
	next = link->next;
	if (link->state & DEV_STALE_LINK)
	    fmvj18x_detach(link);
    }
}

/*====================================================================*/

static void cs_error(client_handle_t handle, int func, int ret)
{
    error_info_t err = { func, ret };
    CardServices(ReportError, handle, &err);
}

/*====================================================================*/

static dev_link_t *fmvj18x_attach(void)
{
    client_reg_t client_reg;
    dev_link_t *link;
    struct net_device *dev;
    int i, ret;
    
    DEBUG(0, "fmvj18x_attach()\n");
    flush_stale_links();

    /* Initialize the dev_link_t structure */
    link = kmalloc(sizeof(struct dev_link_t), GFP_KERNEL);
    memset(link, 0, sizeof(struct dev_link_t));

    link->release.function = &fmvj18x_release;
    link->release.data = (u_long)link;

    /* The io structure describes IO port mapping */
    link->io.NumPorts1 = 32;
    link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
    link->io.IOAddrLines = 5;

    /* Interrupt setup */
    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
    link->irq.IRQInfo1 = IRQ_INFO2_VALID|IRQ_LEVEL_ID;
    if (irq_list[0] == -1)
	link->irq.IRQInfo2 = irq_mask;
    else
	for (i = 0; i < 4; i++)
	    link->irq.IRQInfo2 |= 1 << irq_list[i];
    link->irq.Handler = &fjn_interrupt;
    
    /* General socket configuration */
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.Vcc = 50;
    link->conf.IntType = INT_MEMORY_AND_IO;

    /* Make up a FMVJ18x specific data structure */
    dev = kmalloc(sizeof(struct net_device), GFP_KERNEL);
    memset(dev, 0, sizeof(struct net_device));
    dev->priv = kmalloc(sizeof(local_info_t), GFP_KERNEL);
    memset(dev->priv, 0, sizeof(local_info_t));

    /* The FMVJ18x specific entries in the device structure. */
    dev->hard_start_xmit = &fjn_start_xmit;
    dev->set_config = &fjn_config;
    dev->get_stats = &fjn_get_stats;
    dev->set_multicast_list = &set_rx_mode;
    ether_setup(dev);
    dev->name = ((local_info_t *)dev->priv)->node.dev_name;
    dev->init = &fmvj18x_init;
    dev->open = &fjn_open;
    dev->stop = &fjn_close;
    dev->tbusy = 0xFF;
    link->priv = link->irq.Instance = dev;
    
    /* Register with Card Services */
    link->next = dev_list;
    dev_list = link;
    client_reg.dev_info = &dev_info;
    client_reg.Attributes = INFO_IO_CLIENT | INFO_CARD_SHARE;
    client_reg.EventMask =
	CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
	CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
	CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
    client_reg.event_handler = &fmvj18x_event;
    client_reg.Version = 0x0210;
    client_reg.event_callback_args.client_data = link;
    ret = CardServices(RegisterClient, &link->handle, &client_reg);
    if (ret != 0) {
	cs_error(link->handle, RegisterClient, ret);
	fmvj18x_detach(link);
	return NULL;
    }

    return link;
} /* fmvj18x_attach */

/*====================================================================*/

static void fmvj18x_detach(dev_link_t *link)
{
    dev_link_t **linkp;
    long flags;
    
    DEBUG(0, "fmvj18x_detach(0x%p)\n", link);
    
    /* Locate device structure */
    for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
	if (*linkp == link) break;
    if (*linkp == NULL)
	return;

    save_flags(flags);
    cli();
    if (link->state & DEV_RELEASE_PENDING) {
	del_timer(&link->release);
	link->state &= ~DEV_RELEASE_PENDING;
    }
    restore_flags(flags);
    
    if (link->state & DEV_CONFIG) {
	fmvj18x_release((u_long)link);
	if (link->state & DEV_STALE_CONFIG) {
	    link->state |= DEV_STALE_LINK;
	    return;
	}
    }

    /* Break the link with Card Services */
    if (link->handle)
	CardServices(DeregisterClient, link->handle);
    
    /* Unlink device structure, free pieces */
    *linkp = link->next;
    if (link->priv) {
	struct net_device *dev = link->priv;
	if (link->dev != NULL)
	    unregister_netdev(dev);
	if (dev->priv) 
	    kfree(dev->priv);
	kfree(dev);
    }
    kfree(link);
    
} /* fmvj18x_detach */

/*====================================================================*/

#define CS_CHECK(fn, args...) \
while ((last_ret=CardServices(last_fn=(fn), args))!=0) goto cs_failed

static void fmvj18x_config(dev_link_t *link)
{
    client_handle_t handle;
    tuple_t tuple;
    cisparse_t parse;
    struct net_device *dev;
    u_short buf[32];
    int i, last_fn, last_ret;
    ioaddr_t ioaddr;
    cardtype_t cardtype;
    char *card_name = "unknown";
    u_char *node_id;
    
    handle = link->handle;
    dev =link->priv;

    DEBUG(0, "fmvj18x_config(0x%p)\n", link);

    /*
       This reads the card's CONFIG tuple to find its configuration
       registers.
    */
    tuple.DesiredTuple = CISTPL_CONFIG;
    CS_CHECK(GetFirstTuple, handle, &tuple);
    tuple.TupleData = (u_char *)buf;
    tuple.TupleDataMax = 64;
    tuple.TupleOffset = 0;
    CS_CHECK(GetTupleData, handle, &tuple);
    CS_CHECK(ParseTuple, handle, &tuple, &parse);
    
    /* Configure card */
    link->state |= DEV_CONFIG;

    link->conf.ConfigBase = parse.config.base; 
    link->conf.Present = parse.config.rmask[0];

    tuple.DesiredTuple = CISTPL_FUNCE;
    tuple.TupleOffset = 0;
    if (CardServices(GetFirstTuple, handle, &tuple) == CS_SUCCESS) {
	/* Yes, I have CISTPL_FUNCE. Let's check CISTPL_MANFID */
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	CS_CHECK(GetFirstTuple, handle, &tuple);
	CS_CHECK(GetTupleData, handle, &tuple);
	CS_CHECK(ParseTuple, handle, &tuple, &parse);
	link->conf.ConfigIndex = parse.cftable_entry.index;
	tuple.DesiredTuple = CISTPL_MANFID;
	CS_CHECK(GetFirstTuple, handle, &tuple);
	CS_CHECK(GetTupleData, handle, &tuple);

	switch (le16_to_cpu(buf[0])) {
	case MANFID_TDK:
	    cardtype = TDK;
	    break;
	case MANFID_CONTEC:
	    cardtype = CONTEC;
	    break;
	case MANFID_FUJITSU:
	    if (le16_to_cpu(buf[1]) == PRODID_FUJITSU_MBH10302)
		cardtype = MBH10302;
	    else if (le16_to_cpu(buf[1]) == PRODID_FUJITSU_MBH10304)
		cardtype = MBH10304;
	    else
		cardtype = LA501;
	    break;
	default:
	    cardtype = MBH10304;
	}
    } else {
	/* old type card */
	cardtype = MBH10302;
	link->conf.ConfigIndex = 1;
    }
    CS_CHECK(RequestIO, link->handle, &link->io);
    CS_CHECK(RequestIRQ, link->handle, &link->irq);
    CS_CHECK(RequestConfiguration, link->handle, &link->conf);
    dev->irq = link->irq.AssignedIRQ;
    dev->base_addr = link->io.BasePort1;
    dev->tbusy = 0;
    if (register_netdev(dev) != 0) {
	printk(KERN_NOTICE "fmvj18x_cs: register_netdev() failed\n");
	goto failed;
    }

    ioaddr = dev->base_addr;

    /* Power On chip and select bank 0 */
    outb(BANK_0, ioaddr + CONFIG_1);
    /* Reset controler */
    if( sram_config == 0 ) 
	outb(CONFIG0_RST, ioaddr + CONFIG_0);
    else
	outb(CONFIG0_RST_1, ioaddr + CONFIG_0);
    
    /* Set hardware address */
    switch (cardtype) {
    case MBH10304:
    case TDK:
    case LA501:
    case CONTEC:
	tuple.DesiredTuple = CISTPL_FUNCE;
	tuple.TupleOffset = 0;
	CS_CHECK(GetFirstTuple, handle, &tuple);
	tuple.TupleOffset = 0;
	CS_CHECK(GetTupleData, handle, &tuple);
	if (cardtype == MBH10304) {
	    /* MBH10304's CIS_FUNCE is corrupted */
	    node_id = &(tuple.TupleData[5]);
	    card_name = "FMV-J182";
	} else {
	    while (tuple.TupleData[0] != CISTPL_FUNCE_LAN_NODE_ID ) {
		CS_CHECK(GetNextTuple, handle, &tuple) ;
		CS_CHECK(GetTupleData, handle, &tuple) ;
	    }
	    node_id = &(tuple.TupleData[2]);
	    if( cardtype == TDK ) {
		card_name = "TDK LAK-CD021";
	    } else if( cardtype == LA501 ) {
		card_name = "LA501";
	    } else {
		card_name = "C-NET(PC)C";
	    }
	}
	/* Read MACID from CIS */
	for (i = 0; i < 6; i++)
	    dev->dev_addr[i] = node_id[i];
	break;
    case MBH10302:
    default:
	/* Read MACID from register */
	for (i = 0; i < 6; i++) 
	    dev->dev_addr[i] = inb(ioaddr + MAC_ID + i);
	card_name = "FMV-J181";
	break;
    }

    link->dev = &((local_info_t *)dev->priv)->node;
    link->state &= ~DEV_CONFIG_PENDING;

    ((struct local_info_t *)dev->priv)->cardtype = cardtype ;
    /* print current configuration */
    printk(KERN_INFO "%s: %s, sram %s, port %#3lx, irq %d, hw_addr ", 
	   dev->name, card_name, sram_config == 0 ? "4K TX*2" : "8K TX*2", 
	   dev->base_addr, dev->irq);
    for (i = 0; i < 6; i++)
	printk("%02X%s", dev->dev_addr[i], ((i<5) ? ":" : "\n"));

    return;
    
cs_failed:
    /* All Card Services errors end up here */
    cs_error(link->handle, last_fn, last_ret);
failed:
    fmvj18x_release((u_long)link);

} /* fmvj18x_config */
 
/*====================================================================*/

static void fmvj18x_release(u_long arg)
{
    dev_link_t *link = (dev_link_t *)arg;

    DEBUG(0, "fmvj18x_release(0x%p)\n", link);

    /*
       If the device is currently in use, we won't release until it
       is actually closed.
    */
    if (link->open) {
	DEBUG(1, "fmvj18x_cs: release postponed, '%s' "
	      "still open\n", link->dev->dev_name);
	link->state |= DEV_STALE_CONFIG;
	return;
    }

    /* Don't bother checking to see if these succeed or not */
    CardServices(ReleaseWindow, link->win);
    CardServices(ReleaseConfiguration, link->handle);
    CardServices(ReleaseIO, link->handle, &link->io);
    CardServices(ReleaseIRQ, link->handle, &link->irq);
    
    link->state &= ~(DEV_CONFIG | DEV_RELEASE_PENDING);
    
} /* fmvj18x_release */

/*====================================================================*/

static int fmvj18x_event(event_t event, int priority,
			  event_callback_args_t *args)
{
    dev_link_t *link = args->client_data;
    struct net_device *dev = link->priv;

    DEBUG(1, "fmvj18x_event(0x%06x)\n", event);
    
    switch (event) {
    case CS_EVENT_CARD_REMOVAL:
	link->state &= ~DEV_PRESENT;
	if (link->state & DEV_CONFIG) {
	    dev->tbusy = 0xFF; 
	    dev->start = 0;
	    link->release.expires = jiffies + HZ/20;
	    add_timer(&link->release);
	}
	break;
    case CS_EVENT_CARD_INSERTION:
	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	fmvj18x_config(link);
	break;
    case CS_EVENT_PM_SUSPEND:
	link->state |= DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_RESET_PHYSICAL:
	if (link->state & DEV_CONFIG) {
	    if (link->open) {
		dev->tbusy = 0xFF; 
		dev->start = 0;
	    }
	    CardServices(ReleaseConfiguration, link->handle);
	}
	break;
    case CS_EVENT_PM_RESUME:
	link->state &= ~DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_CARD_RESET:
	if (link->state & DEV_CONFIG) {
	    CardServices(RequestConfiguration, link->handle, &link->conf);
	    if (link->open) {
		dev->tbusy = 0;
		dev->start = 1;
		fjn_reset(dev);
	    }
	}
	break;
    }
    return 0;
} /* fmvj18x_event */

static int fmvj18x_init(struct net_device *dev)
{
    return 0;
} /* fmvj18x_init */

/*====================================================================*/

static int __init init_fmvj18x_cs(void)
{
    servinfo_t serv;
    DEBUG(0, "%s\n", version);
    CardServices(GetCardServicesInfo, &serv);
    if (serv.Revision != CS_RELEASE_CODE) {
	printk(KERN_NOTICE "fmvj18x: Card Services release "
	       "does not match!\n");
	return -1;
    }
    register_pccard_driver(&dev_info, &fmvj18x_attach, &fmvj18x_detach);
    return 0;
}

static void __exit exit_fmvj18x_cs(void)
{
    DEBUG(0, "fmvj18x_cs: unloading\n");
    unregister_pccard_driver(&dev_info);
    while (dev_list != NULL)
	fmvj18x_detach(dev_list);
}

module_init(init_fmvj18x_cs);
module_exit(exit_fmvj18x_cs);

/*====================================================================*/

static void fjn_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    struct net_device *dev = (struct net_device *)dev_id;
    ioaddr_t ioaddr;
    local_info_t *lp;
    unsigned short tx_stat, rx_stat;

    if (dev == NULL) {
        printk(KERN_NOTICE "fjn_interrupt(): irq %d for "
	       "unknown device.\n", irq);
        return;
    }
    if (dev->interrupt) {
        printk(KERN_NOTICE "%s: re-entering the interrupt handler.\n",
	       dev->name);
        return;
    }
    dev->interrupt = 1;
    lp = (struct local_info_t *)dev->priv;
    ioaddr = dev->base_addr;

    /* avoid multiple interrupts */
    outw(0x0000, ioaddr + TX_INTR);

    /* wait for a while */
    udelay(1);

    /* get status */
    tx_stat = inb(ioaddr + TX_STATUS);
    rx_stat = inb(ioaddr + RX_STATUS);

    /* clear status */
    outb(tx_stat, ioaddr + TX_STATUS);
    outb(rx_stat, ioaddr + RX_STATUS);
    
    if (fmvj18x_debug > 4) {
        printk(KERN_DEBUG "%s: interrupt, rx_status %02x.\n",
	       dev->name, rx_stat);
        printk(KERN_DEBUG "               tx_status %02x.\n",
	       tx_stat);
    }
    
    if (rx_stat || (inb(ioaddr + RX_MODE) & F_BUF_EMP) == 0) {
	/* there is packet(s) in rx buffer */
	fjn_rx(dev);
    }
    if (tx_stat & F_TMT_RDY) {
	lp->stats.tx_packets += lp->sent ;
        lp->sent = 0 ;
	if (lp->tx_queue) {
	    outb(DO_TX | lp->tx_queue, ioaddr + TX_START);
	    lp->sent = lp->tx_queue ;
	    lp->tx_queue = 0;
	    lp->tx_queue_len = 0;
	    dev->trans_start = jiffies;
	    dev->tbusy = 0;
	    mark_bh(NET_BH);	/* Inform upper layers. */
	} else {
	    lp->tx_started = 0;
	    dev->tbusy = 0;
	    mark_bh(NET_BH);	/* Inform upper layers. */
	}
    }
    if (fmvj18x_debug > 4) {
        printk(KERN_DEBUG "%s: exiting interrupt,\n", dev->name);
        printk(KERN_DEBUG "    tx_status %02x, rx_status %02x.\n",
	       tx_stat, rx_stat);
    }

    dev->interrupt = 0;
    outb(D_TX_INTR, ioaddr + TX_INTR);
    outb(D_RX_INTR, ioaddr + RX_INTR);

    return;
} /* fjn_interrupt */

/*====================================================================*/

static int fjn_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct local_info_t *lp = (struct local_info_t *)dev->priv;
    ioaddr_t ioaddr = dev->base_addr;

    if (dev->tbusy) {
	/* If we get here, some higher level has decided we are broken.
	   There should really be a "kick me" function call instead. */
	int tickssofar = jiffies - dev->trans_start;
	if (tickssofar < 10)
	    return 1;
	printk(KERN_NOTICE "%s: transmit timed out with status %04x, %s?\n",
	       dev->name, htons(inw(ioaddr + TX_STATUS)),
	       inb(ioaddr + TX_STATUS) & F_TMT_RDY
	       ? "IRQ conflict" : "network cable problem");
	printk(KERN_NOTICE "%s: timeout registers: %04x %04x %04x "
	       "%04x %04x %04x %04x %04x.\n",
	       dev->name, htons(inw(ioaddr + 0)),
	       htons(inw(ioaddr + 2)), htons(inw(ioaddr + 4)),
	       htons(inw(ioaddr + 6)), htons(inw(ioaddr + 8)),
	       htons(inw(ioaddr +10)), htons(inw(ioaddr +12)),
	       htons(inw(ioaddr +14)));
	lp->stats.tx_errors++;
	/* ToDo: We should try to restart the adaptor... */
	cli();

	fjn_reset(dev);

	lp->tx_started = 0;
	lp->tx_queue = 0;
	lp->tx_queue_len = 0;
	lp->sent = 0;
	lp->open_time = jiffies;
	dev->interrupt = 0;
	dev->tbusy = 0;
	dev->start = 1;
    
	sti();
    }

    /* Block a timer-based transmit from overlapping.  This could better be
       done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
    if (test_and_set_bit(0, (void*)&dev->tbusy) != 0)
	printk(KERN_NOTICE "%s: Transmitter access conflict.\n", dev->name);
    else {
	short length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	unsigned char *buf = skb->data;

	if (length > ETH_FRAME_LEN) {
	    if (fmvj18x_debug)
		printk(KERN_NOTICE "%s: Attempting to send a large packet"
		       " (%d bytes).\n", dev->name, length);
	    return 1;
	}

	if (fmvj18x_debug > 4)
	    printk(KERN_DEBUG "%s: Transmitting a packet of length %lu.\n",
		   dev->name, (unsigned long)skb->len);
	lp->stats.tx_bytes += skb->len;

	/* Disable both interrupts. */
	outw(0x0000, ioaddr + TX_INTR);

	/* wait for a while */
	udelay(1);

	outw(length, ioaddr + DATAPORT);
	outsw(ioaddr + DATAPORT, buf, (length + 1) >> 1);

	lp->tx_queue++;
	lp->tx_queue_len += ((length+3) & ~1);

	if (lp->tx_started == 0) {
	    /* If the Tx is idle, always trigger a transmit. */
	    outb(DO_TX | lp->tx_queue, ioaddr + TX_START);
	    lp->sent = lp->tx_queue ;
	    lp->tx_queue = 0;
	    lp->tx_queue_len = 0;
	    dev->trans_start = jiffies;
	    lp->tx_started = 1;
	    dev->tbusy = 0;
	} else {
	    if( sram_config == 0 ) {
		if (lp->tx_queue_len < (4096 - (ETH_FRAME_LEN +2)) )
		    /* Yes, there is room for one more packet. */
		    dev->tbusy = 0;
	    } else {
		if (lp->tx_queue_len < (8192 - (ETH_FRAME_LEN +2)) && 
						lp->tx_queue < 127 )
		    /* Yes, there is room for one more packet. */
		    dev->tbusy = 0;
	    }
	}

	/* Re-enable interrupts */
	outb(D_TX_INTR, ioaddr + TX_INTR);
	outb(D_RX_INTR, ioaddr + RX_INTR);
    }
    dev_kfree_skb (skb);

    return 0;
} /* fjn_start_xmit */

/*====================================================================*/

static void fjn_reset(struct net_device *dev)
{
    struct local_info_t *lp = (struct local_info_t *)dev->priv;
    ioaddr_t ioaddr = dev->base_addr;
    int i;

    if (fmvj18x_debug > 4) {
	printk(KERN_DEBUG "fjn_reset(%s) called.\n",dev->name);
    }

    /* Power On chip and select bank 0 */
    outb(BANK_0, ioaddr + CONFIG_1);
    /* Reset buffers */
    if( sram_config == 0 ) 
	outb(CONFIG0_RST, ioaddr + CONFIG_0);
    else
	outb(CONFIG0_RST_1, ioaddr + CONFIG_0);

    /* Set Tx modes */
    outb(D_TX_MODE, ioaddr + TX_MODE);
    /* set Rx modes */
    outb(ID_MATCHED, ioaddr + RX_MODE);

    /* Set hardware address */
    for (i = 0; i < 6; i++) 
        outb(dev->dev_addr[i], ioaddr + NODE_ID + i);

    if (fmvj18x_debug > 4) {
	printk(KERN_DEBUG "node id: ");
	for (i = 0; i < 6; i++) 
	    printk("%02X ",inb(ioaddr + NODE_ID + i));
	printk("\n");
    }
    /* Switch to bank 1 */
    outb(BANK_1, ioaddr + CONFIG_1);

    /* set the multicast table to accept none. */
    for (i = 0; i < 6; i++) 
        outb(0x00, ioaddr + MAR_ADR + i);

    /* Switch to bank 2 (runtime mode) */
    outb(BANK_2, ioaddr + CONFIG_1);

    /* set 16col ctrl bits */
    if( lp->cardtype == TDK ) 
        outb(TDK_AUTO_MODE, ioaddr + COL_CTRL);
    else
        outb(AUTO_MODE, ioaddr + COL_CTRL);

    /* clear Reserved Regs */
    outb(0x00, ioaddr + BMPR12);
    outb(0x00, ioaddr + BMPR13);

    /* reset Skip packet reg. */
    outb(0x01, ioaddr + RX_SKIP);

    /* Enable Tx and Rx */
    if( sram_config == 0 )
	outb(CONFIG0_DFL, ioaddr + CONFIG_0);
    else
	outb(CONFIG0_DFL_1, ioaddr + CONFIG_0);

    /* Init receive pointer ? */
    inw(ioaddr + DATAPORT);
    inw(ioaddr + DATAPORT);

    /* Clear all status */
    outb(0xff, ioaddr + TX_STATUS);
    outb(0xff, ioaddr + RX_STATUS);

    if( lp->cardtype != TDK ) 
    		outb(INTR_OFF, ioaddr + LAN_CTRL);

    /* Turn on Rx interrupts */
    outb(D_TX_INTR, ioaddr + TX_INTR);
    outb(D_RX_INTR, ioaddr + RX_INTR);

    /* Turn on interrupts from LAN card controler */
    if( lp->cardtype != TDK ) 
		outb(INTR_ON, ioaddr + LAN_CTRL);
} /* fjn_reset */

/*====================================================================*/

static void fjn_rx(struct net_device *dev)
{
    struct local_info_t *lp = (struct local_info_t *)dev->priv;
    ioaddr_t ioaddr = dev->base_addr;
    int boguscount = 10;	/* 5 -> 10: by agy 19940922 */

    if (fmvj18x_debug > 4)
        printk(KERN_DEBUG "%s: in rx_packet(), rx_status %02x.\n",
               dev->name, inb(ioaddr + RX_STATUS));

    while ((inb(ioaddr + RX_MODE) & F_BUF_EMP) == 0) {
	u_short status = inw(ioaddr + DATAPORT);

	if (fmvj18x_debug > 4)
	    printk(KERN_DEBUG "%s: Rxing packet mode %02x status %04x.\n",
		   dev->name, inb(ioaddr + RX_MODE), status);
#ifndef final_version
	if (status == 0) {
	    outb(F_SKP_PKT, ioaddr + RX_SKIP);
	    break;
	}
#endif
	if ((status & 0xF0) != 0x20) {	/* There was an error. */
	    lp->stats.rx_errors++;
	    if (status & F_LEN_ERR) lp->stats.rx_length_errors++;
	    if (status & F_ALG_ERR) lp->stats.rx_frame_errors++;
	    if (status & F_CRC_ERR) lp->stats.rx_crc_errors++;
	    if (status & F_OVR_FLO) lp->stats.rx_over_errors++;
	} else {
	    u_short pkt_len = inw(ioaddr + DATAPORT);
	    /* Malloc up new buffer. */
	    struct sk_buff *skb;

	    if (pkt_len > 1550) {
		printk(KERN_NOTICE "%s: The FMV-18x claimed a very "
		       "large packet, size %d.\n", dev->name, pkt_len);
		outb(F_SKP_PKT, ioaddr + RX_SKIP);
		lp->stats.rx_errors++;
		break;
	    }
	    skb = dev_alloc_skb(pkt_len+2);
	    if (skb == NULL) {
		printk(KERN_NOTICE "%s: Memory squeeze, dropping "
		       "packet (len %d).\n", dev->name, pkt_len);
		outb(F_SKP_PKT, ioaddr + RX_SKIP);
		lp->stats.rx_dropped++;
		break;
	    }
	    skb->dev = dev;

	    skb_reserve(skb, 2);
	    insw(ioaddr + DATAPORT, skb_put(skb, pkt_len),
		    (pkt_len + 1) >> 1);
	    skb->protocol = eth_type_trans(skb, dev);

	    if (fmvj18x_debug > 5) {
		int i;
		printk(KERN_DEBUG "%s: Rxed packet of length %d: ",
		       dev->name, pkt_len);
		for (i = 0; i < 14; i++)
		    printk(" %02x", skb->data[i]);
		printk(".\n");
	    }

	    netif_rx(skb);
	    lp->stats.rx_packets++;
	    lp->stats.rx_bytes += skb->len;
	}
	if (--boguscount <= 0)
	    break;
    }

    /* If any worth-while packets have been received, dev_rint()
	   has done a mark_bh(NET_BH) for us and will work on them
	   when we get to the bottom-half routine. */
/*
    if( lp->cardtype != TDK ) {
	int i;
	for (i = 0; i < 20; i++) {
	    if ((inb(ioaddr + RX_MODE) & F_BUF_EMP) == F_BUF_EMP)
		break;
	    (void)inw(ioaddr + DATAPORT);  /+ dummy status read +/
	    outb(F_SKP_PKT, ioaddr + RX_SKIP);
	}

	if (fmvj18x_debug > 5 && i > 0)
	    printk(KERN_DEBUG "%s: Exint Rx packet with mode %02x after"
		   " %d ticks.\n", dev->name, inb(ioaddr + RX_MODE), i);
    }
*/

    return;
} /* fjn_rx */

/*====================================================================*/

static int fjn_config(struct net_device *dev, struct ifmap *map){
    return 0;
} /* fjn_config */

static int fjn_open(struct net_device *dev)
{
    struct local_info_t *lp = (struct local_info_t *)dev->priv;
    dev_link_t *link;

    if (fmvj18x_debug > 4)
        printk(KERN_DEBUG "fjn_open('%s').\n", dev->name);

    for (link = dev_list; link; link = link->next)
	if (link->priv == dev) break;
    if (!DEV_OK(link))
	return -ENODEV;
    
    link->open++;
    
    fjn_reset(dev);
    
    lp->tx_started = 0;
    lp->tx_queue = 0;
    lp->tx_queue_len = 0;
    lp->open_time = jiffies;
    dev->interrupt = 0;
    dev->tbusy = 0;
    dev->start = 1;
    
    MOD_INC_USE_COUNT;

    return 0;
} /* fjn_open */

/*====================================================================*/

static int fjn_close(struct net_device *dev)
{
    ioaddr_t ioaddr = dev->base_addr;
    struct local_info_t *lp = (struct local_info_t *)dev->priv;
    dev_link_t *link;

    if (fmvj18x_debug > 4)
        printk(KERN_DEBUG "fjn_open('%s').\n", dev->name);

    for (link = dev_list; link; link = link->next)
	if (link->priv == dev) break;
    if (link == NULL)
	return -ENODEV;
    
    if (fmvj18x_debug > 2)
	printk(KERN_DEBUG "%s: shutting down ethercard.\n", dev->name);

    ((struct local_info_t *)dev->priv)->open_time = 0;

    dev->tbusy = 1;
    dev->start = 0;

    /* Set configuration register 0 to disable Tx and Rx. */
    if( sram_config == 0 ) 
	outb(CONFIG0_RST ,ioaddr + CONFIG_0);
    else
	outb(CONFIG0_RST_1 ,ioaddr + CONFIG_0);

    /* Update the statistics -- ToDo. */

    /* Power-down the chip.  Green, green, green! */
    outb(CHIP_OFF ,ioaddr + CONFIG_1);

    /* Set the ethernet adaptor disable IRQ */
    if( lp->cardtype != TDK ) 
    		outb(INTR_OFF, ioaddr + LAN_CTRL);

    link->open--;
    dev->start = 0;
    if (link->state & DEV_STALE_CONFIG) {
	link->release.expires = jiffies + HZ/20;
	link->state |= DEV_RELEASE_PENDING;
	add_timer(&link->release);
    }
    MOD_DEC_USE_COUNT;

    return 0;
} /* fjn_close */

/*====================================================================*/

static struct net_device_stats *fjn_get_stats(struct net_device *dev)
{
    local_info_t *lp = (local_info_t *)dev->priv;
    return &lp->stats;
} /* fjn_get_stats */

/*====================================================================*/

/*
  Set the multicast/promiscuous mode for this adaptor.
*/

/* The little-endian AUTODIN II ethernet CRC calculation.
   N.B. Do not use for bulk data, use a table-based routine instead.
   This is common code and should be moved to net/core/crc.c */
static unsigned const ethernet_polynomial_le = 0xedb88320U;
static inline unsigned ether_crc_le(int length, unsigned char *data)
{
    unsigned int crc = 0xffffffff;	/* Initial value. */
    while(--length >= 0) {
	unsigned char current_octet = *data++;
	int bit;
	for (bit = 8; --bit >= 0; current_octet >>= 1) {
	    if ((crc ^ current_octet) & 1) {
		crc >>= 1;
		crc ^= ethernet_polynomial_le;
	    } else
		crc >>= 1;
	}
    }
    return crc;
}

static void set_rx_mode(struct net_device *dev)
{
    ioaddr_t ioaddr = dev->base_addr;
    struct local_info_t *lp = (struct local_info_t *)dev->priv;
    unsigned char mc_filter[8];		 /* Multicast hash filter */
    long flags;
    int i;
    
    if (dev->flags & IFF_PROMISC) {
	/* Unconditionally log net taps. */
	printk("%s: Promiscuous mode enabled.\n", dev->name);
	memset(mc_filter, 0xff, sizeof(mc_filter));
	outb(3, ioaddr + RX_MODE);	/* Enable promiscuous mode */
    } else if (dev->mc_count > MC_FILTERBREAK
	       ||  (dev->flags & IFF_ALLMULTI)) {
	/* Too many to filter perfectly -- accept all multicasts. */
	memset(mc_filter, 0xff, sizeof(mc_filter));
	outb(2, ioaddr + RX_MODE);	/* Use normal mode. */
    } else if (dev->mc_count == 0) {
	memset(mc_filter, 0x00, sizeof(mc_filter));
	outb(1, ioaddr + RX_MODE);	/* Ignore almost all multicasts. */
    } else {
	struct dev_mc_list *mclist;
	int i;
	
	memset(mc_filter, 0, sizeof(mc_filter));
	for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
	     i++, mclist = mclist->next)
	    set_bit(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x3f,
		    mc_filter);
    }
    
    save_flags(flags);
    cli();
    if (memcmp(mc_filter, lp->mc_filter, sizeof(mc_filter))) {
	int saved_bank = inb(ioaddr + CONFIG_1);
	/* Switch to bank 1 and set the multicast table. */
	outb(0xe4, ioaddr + CONFIG_1);
	for (i = 0; i < 8; i++)
	    outb(mc_filter[i], ioaddr + 8 + i);
	memcpy(lp->mc_filter, mc_filter, sizeof(mc_filter));
	outb(saved_bank, ioaddr + CONFIG_1);
    }
    restore_flags(flags);
}
