/*****************************************************************************
* sdla_ppp.c	WANPIPE(tm) Multiprotocol WAN Link Driver. PPP module.
*
* Author: 	Jaspreet Singh 	<jaspreet@sangoma.com>
*
* Copyright:	(c) 1995-1997 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Mar 15, 1998  Alan Cox	o 2.1.8x basic port.
* Nov 27, 1997	Jaspreet Singh	o Added protection against enabling of irqs 
*				  while they have been disabled.
* Nov 24, 1997  Jaspreet Singh  o Fixed another RACE condition caused by
*                                 disabling and enabling of irqs.
*                               o Added new counters for stats on disable/enable*                                 IRQs.
* Nov 10, 1997	Jaspreet Singh	o Initialized 'skb->mac.raw' to 'skb->data'
*				  before every netif_rx().
*				o Free up the device structure in del_if().
* Nov 07, 1997	Jaspreet Singh	o Changed the delay to zero for Line tracing
*				  command.
* Oct 20, 1997 	Jaspreet Singh	o Added hooks in for Router UP time.
* Oct 16, 1997	Jaspreet Singh  o The critical flag is used to maintain flow
*				  control by avoiding RACE conditions.  The 
*				  cli() and restore_flags() are taken out.
*				  A new structure, "ppp_private_area", is added 
*				  to provide Driver Statistics.   
* Jul 21, 1997 	Jaspreet Singh	o Protected calls to sdla_peek() by adding 
*				  save_flags(), cli() and restore_flags().
* Jul 07, 1997	Jaspreet Singh  o Added configurable TTL for UDP packets
*				o Added ability to discard mulitcast and
*				  broacast source addressed packets.
* Jun 27, 1997 	Jaspreet Singh	o Added FT1 monitor capabilities
*				  New case (0x25) statement in if_send routine.
*				  Added a global variable rCount to keep track
*				  of FT1 status enabled on the board.
* May 22, 1997	Jaspreet Singh	o Added change in the PPP_SET_CONFIG command for
*				508 card to reflect changes in the new 
*				ppp508.sfm for supporting:continous transmission
*				of Configure-Request packets without receiving a
*				reply 				
*				OR-ed 0x300 to conf_flags 
*			        o Changed connect_tmout from 900 to 0
* May 21, 1997	Jaspreet Singh  o Fixed UDP Management for multiple boards
* Apr 25, 1997  Farhan Thawar    o added UDP Management stuff
* Mar 11, 1997  Farhan Thawar   Version 3.1.1
*                                o fixed (+1) bug in rx_intr()
*                                o changed if_send() to return 0 if
*                                  wandev.critical() is true
*                                o free socket buffer in if_send() if
*                                  returning 0 
* Jan 15, 1997	Gene Kozin	Version 3.1.0
*				 o implemented exec() entry point
* Jan 06, 1997	Gene Kozin	Initial version.
*****************************************************************************/

#include <linux/kernel.h>	/* printk(), and other useful stuff */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/string.h>	/* inline memset(), etc. */
#include <linux/malloc.h>	/* kmalloc(), kfree() */
#include <linux/wanrouter.h>	/* WAN router definitions */
#include <linux/wanpipe.h>	/* WANPIPE common user API definitions */
#include <linux/if_arp.h>	/* ARPHRD_* defines */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <asm/uaccess.h>	/* copyto/from user */
#define	_GNUC_
#include <linux/sdla_ppp.h>	/* PPP firmware API definitions */

/****** Defines & Macros ****************************************************/

#ifdef	_DEBUG_
#define	STATIC
#else
#define	STATIC		static
#endif
#define	PPP_DFLT_MTU	1500	/* default MTU */
#define	PPP_MAX_MTU	4000	/* maximum MTU */
#define PPP_HDR_LEN	1
#define	CONNECT_TIMEOUT	(90*HZ)	/* link connection timeout */
#define	HOLD_DOWN_TIME	(30*HZ)	/* link hold down time */

/* For handle_IPXWAN() */
#define CVHexToAscii(b) (((unsigned char)(b) > (unsigned char)9) ? ((unsigned char)'A' + ((unsigned char)(b) - (unsigned char)10)) : ((unsigned char)'0' + (unsigned char)(b)))

/******Data Structures*****************************************************/
/* This structure is placed in the private data area of the device structure.
 * The card structure used to occupy the private area but now the following 
 * structure will incorporate the card structure along with PPP specific data
 */

typedef struct ppp_private_area 
{
	sdla_t *card;
	unsigned long router_start_time;	/*router start time in sec */
	unsigned long tick_counter;	/*used for 5 second counter */
	unsigned mc;		/*multicast support on or off */
	/* PPP specific statistics */
	unsigned long if_send_entry;
	unsigned long if_send_skb_null;
	unsigned long if_send_broadcast;
	unsigned long if_send_multicast;
	unsigned long if_send_critical_ISR;
	unsigned long if_send_critical_non_ISR;
	unsigned long if_send_busy;
	unsigned long if_send_busy_timeout;
	unsigned long if_send_DRVSTATS_request;
	unsigned long if_send_PTPIPE_request;
	unsigned long if_send_wan_disconnected;
	unsigned long if_send_adptr_bfrs_full;
	unsigned long if_send_protocol_error;
	unsigned long if_send_tx_int_enabled;
	unsigned long if_send_bfr_passed_to_adptr;
	unsigned long rx_intr_no_socket;
	unsigned long rx_intr_DRVSTATS_request;
	unsigned long rx_intr_PTPIPE_request;
	unsigned long rx_intr_bfr_not_passed_to_stack;
	unsigned long rx_intr_bfr_passed_to_stack;
	unsigned long UDP_PTPIPE_mgmt_kmalloc_err;
	unsigned long UDP_PTPIPE_mgmt_adptr_type_err;
	unsigned long UDP_PTPIPE_mgmt_direction_err;
	unsigned long UDP_PTPIPE_mgmt_adptr_cmnd_timeout;
	unsigned long UDP_PTPIPE_mgmt_adptr_cmnd_OK;
	unsigned long UDP_PTPIPE_mgmt_passed_to_adptr;
	unsigned long UDP_PTPIPE_mgmt_passed_to_stack;
	unsigned long UDP_PTPIPE_mgmt_no_socket;
	unsigned long UDP_DRVSTATS_mgmt_kmalloc_err;
	unsigned long UDP_DRVSTATS_mgmt_adptr_type_err;
	unsigned long UDP_DRVSTATS_mgmt_direction_err;
	unsigned long UDP_DRVSTATS_mgmt_adptr_cmnd_timeout;
	unsigned long UDP_DRVSTATS_mgmt_adptr_cmnd_OK;
	unsigned long UDP_DRVSTATS_mgmt_passed_to_adptr;
	unsigned long UDP_DRVSTATS_mgmt_passed_to_stack;
	unsigned long UDP_DRVSTATS_mgmt_no_socket;
	unsigned long router_up_time;
} ppp_private_area_t;

/* variable for keeping track of enabling/disabling FT1 monitor status */

static int rCount = 0;
extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

/****** Function Prototypes *************************************************/

/* WAN link driver entry points. These are called by the WAN router module. */
static int update(wan_device_t * wandev);
static int new_if(wan_device_t * wandev, struct device *dev,
		  wanif_conf_t * conf);
static int del_if(wan_device_t * wandev, struct device *dev);
/* WANPIPE-specific entry points */
static int wpp_exec(struct sdla *card, void *u_cmd, void *u_data);
/* Network device interface */
static int if_init(struct device *dev);
static int if_open(struct device *dev);
static int if_close(struct device *dev);
static int if_header(struct sk_buff *skb, struct device *dev,
	    unsigned short type, void *daddr, void *saddr, unsigned len);
static int if_rebuild_hdr(struct sk_buff *skb);
static int if_send(struct sk_buff *skb, struct device *dev);
static struct enet_statistics *if_stats(struct device *dev);
/* PPP firmware interface functions */
static int ppp_read_version(sdla_t * card, char *str);
static int ppp_configure(sdla_t * card, void *data);
static int ppp_set_intr_mode(sdla_t * card, unsigned mode);
static int ppp_comm_enable(sdla_t * card);
static int ppp_comm_disable(sdla_t * card);
static int ppp_get_err_stats(sdla_t * card);
static int ppp_send(sdla_t * card, void *data, unsigned len, unsigned proto);
static int ppp_error(sdla_t * card, int err, ppp_mbox_t * mb);
/* Interrupt handlers */
STATIC void wpp_isr(sdla_t * card);
static void rx_intr(sdla_t * card);
static void tx_intr(sdla_t * card);
/* Background polling routines */
static void wpp_poll(sdla_t * card);
static void poll_active(sdla_t * card);
static void poll_connecting(sdla_t * card);
static void poll_disconnected(sdla_t * card);
/* Miscellaneous functions */
static int config502(sdla_t * card);
static int config508(sdla_t * card);
static void show_disc_cause(sdla_t * card, unsigned cause);
static unsigned char bps_to_speed_code(unsigned long bps);
static int reply_udp(unsigned char *data, unsigned int mbox_len);
static int process_udp_mgmt_pkt(char udp_pkt_src, sdla_t * card, struct sk_buff *skb, struct device *dev, ppp_private_area_t * ppp_priv_area);
static int process_udp_driver_call(char udp_pkt_src, sdla_t * card, struct sk_buff *skb, struct device *dev, ppp_private_area_t * ppp_priv_area);
static void init_ppp_tx_rx_buff(sdla_t * card);
static int intr_test(sdla_t * card);
static int udp_pkt_type(struct sk_buff *skb, sdla_t * card);
static void init_ppp_priv_struct(ppp_private_area_t * ppp_priv_area);
static void init_global_statistics(sdla_t * card);
static int Intr_test_counter;
static char TracingEnabled;
static unsigned long curr_trace_addr;
static unsigned long start_trace_addr;
static unsigned short available_buffer_space;
/* IPX functions */
static void switch_net_numbers(unsigned char *sendpacket, unsigned long network_number, unsigned char incoming);
static int handle_IPXWAN(unsigned char *sendpacket, char *devname, unsigned char enable_IPX, unsigned long network_number, unsigned short proto);

/****** Public Functions ****************************************************/

/*============================================================================
 * PPP protocol initialization routine.
 *
 * This routine is called by the main WANPIPE module during setup.  At this
 * point adapter is completely initialized and firmware is running.
 *  o read firmware version (to make sure it's alive)
 *  o configure adapter
 *  o initialize protocol-specific fields of the adapter data space.
 *
 * Return:	0	o.k.
 *		< 0	failure.
 */
int wpp_init(sdla_t * card, wandev_conf_t * conf)
{
	union {
		char str[80];
	} u;
	/* Verify configuration ID */
	if (conf->config_id != WANCONFIG_PPP) {
		printk(KERN_INFO "%s: invalid configuration ID %u!\n",
		       card->devname, conf->config_id);
		return -EINVAL;
	}
	/* Initialize protocol-specific fields */
	switch (card->hw.fwid) {
	case SFID_PPP502:
		card->mbox = (void *) (card->hw.dpmbase + PPP502_MB_OFFS);
		card->flags = (void *) (card->hw.dpmbase + PPP502_FLG_OFFS);
		break;
	case SFID_PPP508:
		card->mbox = (void *) (card->hw.dpmbase + PPP508_MB_OFFS);
		card->flags = (void *) (card->hw.dpmbase + PPP508_FLG_OFFS);
		break;
	default:
		return -EINVAL;
	}
	/* Read firmware version.  Note that when adapter initializes, it
	 * clears the mailbox, so it may appear that the first command was
	 * executed successfully when in fact it was merely erased. To work
	 * around this, we execute the first command twice.
	 */
	if (ppp_read_version(card, NULL) || ppp_read_version(card, u.str))
		return -EIO;
	printk(KERN_INFO "%s: running PPP firmware v%s\n", card->devname, u.str);
	/* Adjust configuration and set defaults */
	card->wandev.mtu = (conf->mtu) ?
	    min(conf->mtu, PPP_MAX_MTU) : PPP_DFLT_MTU;
	card->wandev.bps = conf->bps;
	card->wandev.interface = conf->interface;
	card->wandev.clocking = conf->clocking;
	card->wandev.station = conf->station;
	card->isr = &wpp_isr;
	card->poll = &wpp_poll;
	card->exec = &wpp_exec;
	card->wandev.update = &update;
	card->wandev.new_if = &new_if;
	card->wandev.del_if = &del_if;
	card->wandev.state = WAN_DISCONNECTED;
	card->wandev.udp_port = conf->udp_port;
	card->wandev.ttl = conf->ttl;
	card->irq_dis_if_send_count = 0;
	card->irq_dis_poll_count = 0;
	TracingEnabled = 0;
	card->wandev.enable_IPX = conf->enable_IPX;
	if (conf->network_number)
		card->wandev.network_number = conf->network_number;
	else
		card->wandev.network_number = 0xDEADBEEF;
	/* initialize global statistics */
	init_global_statistics(card);
	return 0;
}

/******* WAN Device Driver Entry Points *************************************/

/*============================================================================
 * Update device status & statistics.
 */
static int update(wan_device_t * wandev)
{
	sdla_t *card;
	/* sanity checks */
	if ((wandev == NULL) || (wandev->private == NULL))
		return -EFAULT;
	if (wandev->state == WAN_UNCONFIGURED)
		return -ENODEV;
	if (test_and_set_bit(0, (void *) &wandev->critical))
		return -EAGAIN;
	card = wandev->private;
	ppp_get_err_stats(card);
	wandev->critical = 0;
	return 0;
}

/*============================================================================
 * Create new logical channel.
 * This routine is called by the router when ROUTER_IFNEW IOCTL is being
 * handled.
 * o parse media- and hardware-specific configuration
 * o make sure that a new channel can be created
 * o allocate resources, if necessary
 * o prepare network device structure for registaration.
 *
 * Return:	0	o.k.
 *		< 0	failure (channel will not be created)
 */

static int new_if(wan_device_t * wandev, struct device *dev, wanif_conf_t * conf)
{
	sdla_t *card = wandev->private;
	ppp_private_area_t *ppp_priv_area;
	if (wandev->ndev)
		return -EEXIST;
	if ((conf->name[0] == '\0') || (strlen(conf->name) > WAN_IFNAME_SZ)) {
		printk(KERN_INFO "%s: invalid interface name!\n",
		       card->devname);
		return -EINVAL;
	}
	/* allocate and initialize private data */
	ppp_priv_area = kmalloc(sizeof(ppp_private_area_t), GFP_KERNEL);
	if (ppp_priv_area == NULL)
		return -ENOMEM;
	memset(ppp_priv_area, 0, sizeof(ppp_private_area_t));
	ppp_priv_area->card = card;
	/* initialize data */
	strcpy(card->u.p.if_name, conf->name);
	/* initialize data in ppp_private_area structure */
	init_ppp_priv_struct(ppp_priv_area);
	ppp_priv_area->mc = conf->mc;
	/* prepare network device data space for registration */
	dev->name = card->u.p.if_name;
	dev->init = &if_init;
	dev->priv = ppp_priv_area;
	return 0;
}

/*============================================================================
 * Delete logical channel.
 */

static int del_if(wan_device_t * wandev, struct device *dev)
{
	if (dev->priv) {
		kfree(dev->priv);
		dev->priv = NULL;
	}
	return 0;
}

/****** WANPIPE-specific entry points ***************************************/

/*============================================================================
 * Execute adapter interface command.
 */

static int wpp_exec(struct sdla *card, void *u_cmd, void *u_data)
{
	ppp_mbox_t *mbox = card->mbox;
	int len;
	if(copy_from_user((void *) &mbox->cmd, u_cmd, sizeof(ppp_cmd_t)))
		return -EFAULT;
	len = mbox->cmd.length;
	if (len) {
		if(copy_from_user((void *) &mbox->data, u_data, len))
			return -EFAULT;
	}
	/* execute command */
	if (!sdla_exec(mbox))
		return -EIO;
	/* return result */
	if(copy_to_user(u_cmd, (void *) &mbox->cmd, sizeof(ppp_cmd_t)))
		return -EFAULT;
	len = mbox->cmd.length;
	if (len && u_data && copy_to_user(u_data, (void *) &mbox->data, len))
		return -EFAULT;
	return 0;
}

/****** Network Device Interface ********************************************/

/*============================================================================
 * Initialize Linux network interface.
 *
 * This routine is called only once for each interface, during Linux network
 * interface registration.  Returning anything but zero will fail interface
 * registration.
 */

static int if_init(struct device *dev)
{
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;
	wan_device_t *wandev = &card->wandev;

	/* Initialize device driver entry points */
	dev->open = &if_open;
	dev->stop = &if_close;
	dev->hard_header = &if_header;
	dev->rebuild_header = &if_rebuild_hdr;
	dev->hard_start_xmit = &if_send;
	dev->get_stats = &if_stats;
	/* Initialize media-specific parameters */
	dev->type = ARPHRD_PPP;	/* ARP h/w type */
	dev->mtu = wandev->mtu;
	dev->hard_header_len = PPP_HDR_LEN;	/* media header length */
	/* Initialize hardware parameters (just for reference) */
	dev->irq = wandev->irq;
	dev->dma = wandev->dma;
	dev->base_addr = wandev->ioport;
	dev->mem_start = (unsigned long)wandev->maddr;
	dev->mem_end = dev->mem_start + wandev->msize - 1;
	/* Set transmit buffer queue length */
	dev->tx_queue_len = 100;
	/* Initialize socket buffers */
	dev_init_buffers(dev);
	return 0;
}

/*============================================================================
 * Open network interface.
 * o enable communications and interrupts.
 * o prevent module from unloading by incrementing use count
 *
 * Return 0 if O.k. or errno.
 */

static int if_open(struct device *dev)
{
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;
	ppp_flags_t *flags = card->flags;
	struct timeval tv;
	int err = 0;
	if (dev->start)
		return -EBUSY;	/* only one open is allowed */
	if (test_and_set_bit(0, (void *) &card->wandev.critical))
		return -EAGAIN;
	if ((card->hw.fwid == SFID_PPP502) ? config502(card) : config508(card)) {
		err = -EIO;
		card->wandev.critical = 0;
		return err;
	}
	Intr_test_counter = 0;
	err = intr_test(card);
	if ((err) || (Intr_test_counter != (MAX_INTR_TEST_COUNTER + 1))) {
		printk(KERN_INFO "%s: Interrupt Test Failed, Counter: %i\n",
		       card->devname, Intr_test_counter);
		err = -EIO;
		card->wandev.critical = 0;
		return err;
	}
	printk(KERN_INFO "%s: Interrupt Test Passed, Counter: %i\n",
	       card->devname, Intr_test_counter);
	/* Initialize Rx/Tx buffer control fields */
	init_ppp_tx_rx_buff(card);
	if (ppp_set_intr_mode(card, 0x03)) {
		err = -EIO;
		card->wandev.critical = 0;
		return err;
	}
	flags->imask &= ~0x02;
	if (ppp_comm_enable(card)) {
		err = -EIO;
		card->wandev.critical = 0;
		return err;
	}
	wanpipe_set_state(card, WAN_CONNECTING);
	wanpipe_open(card);
	dev->mtu = min(dev->mtu, card->wandev.mtu);
	dev->interrupt = 0;
	dev->tbusy = 0;
	dev->start = 1;
	do_gettimeofday(&tv);
	ppp_priv_area->router_start_time = tv.tv_sec;
	card->wandev.critical = 0;
	return err;
}

/*============================================================================
 * Close network interface.
 * o if this is the last open, then disable communications and interrupts.
 * o reset flags.
 */

static int if_close(struct device *dev)
{
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;
	if (test_and_set_bit(0, (void *) &card->wandev.critical))
		return -EAGAIN;
	dev->start = 0;
	wanpipe_close(card);
	wanpipe_set_state(card, WAN_DISCONNECTED);
	ppp_set_intr_mode(card, 0);
	ppp_comm_disable(card);
	card->wandev.critical = 0;
	return 0;
}

/*============================================================================
 * Build media header.
 *
 * The trick here is to put packet type (Ethertype) into 'protocol' field of
 * the socket buffer, so that we don't forget it.  If packet type is not
 * supported, set skb->protocol to 0 and discard packet later.
 *
 * Return:	media header length.
 */

static int if_header(struct sk_buff *skb, struct device *dev,
	     unsigned short type, void *daddr, void *saddr, unsigned len)
{
	switch (type) 
	{
		case ETH_P_IP:
		case ETH_P_IPX:
			skb->protocol = type;
			break;
		default:
			skb->protocol = 0;
	}
	return PPP_HDR_LEN;
}

/*============================================================================
 * Re-build media header.
 *
 * Return:	1	physical address resolved.
 *		0	physical address not resolved
 */

static int if_rebuild_hdr(struct sk_buff *skb)
{
	struct device *dev=skb->dev;
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;
	printk(KERN_INFO "%s: rebuild_header() called for interface %s!\n",
	       card->devname, dev->name);
	return 1;
}

/*============================================================================
 * Send a packet on a network interface.
 * o set tbusy flag (marks start of the transmission) to block a timer-based
 *   transmit from overlapping.
 * o check link state. If link is not up, then drop the packet.
 * o execute adapter send command.
 * o free socket buffer
 *
 * Return:	0	complete (socket buffer must be freed)
 *		non-0	packet may be re-transmitted (tbusy must be set)
 *
 * Notes:
 * 1. This routine is called either by the protocol stack or by the "net
 *    bottom half" (with interrupts enabled).
 * 2. Setting tbusy flag will inhibit further transmit requests from the
 *    protocol stack and can be used for flow control with protocol layer.
 */

static int if_send(struct sk_buff *skb, struct device *dev)
{
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;
	unsigned char *sendpacket;
	unsigned long check_braddr, check_mcaddr;
	unsigned long host_cpu_flags;
	ppp_flags_t *flags = card->flags;
	int retry = 0;
	int err, udp_type;
	++ppp_priv_area->if_send_entry;
	if (skb == NULL) {
		/* If we get here, some higher layer thinks we've missed an
		 * tx-done interrupt.
		 */
		printk(KERN_INFO "%s: interface %s got kicked!\n",
		       card->devname, dev->name);
		++ppp_priv_area->if_send_skb_null;
		mark_bh(NET_BH);
		return 0;
	}
	if (dev->tbusy) {
		/* If our device stays busy for at least 5 seconds then we will
		 * kick start the device by making dev->tbusy = 0.  We expect 
		 * that our device never stays busy more than 5 seconds. So this
		 * is only used as a last resort. 
		 */
		++ppp_priv_area->if_send_busy;
		++card->wandev.stats.collisions;
		if ((jiffies - ppp_priv_area->tick_counter) < (5 * HZ)) {
			return 1;
		}
		printk(KERN_INFO "%s: Transmit times out\n", card->devname);
		++ppp_priv_area->if_send_busy_timeout;
		/* unbusy the card (because only one interface per card) */
		dev->tbusy = 0;
	}
	sendpacket = skb->data;
	udp_type = udp_pkt_type(skb, card);
	if (udp_type == UDP_DRVSTATS_TYPE) {
		++ppp_priv_area->if_send_DRVSTATS_request;
		process_udp_driver_call(UDP_PKT_FRM_STACK, card, skb, dev,
					ppp_priv_area);
		dev_kfree_skb(skb);
		return 0;
	} else if (udp_type == UDP_PTPIPE_TYPE)
		++ppp_priv_area->if_send_PTPIPE_request;
	/* retreive source address in two forms: broadcast & multicast */
	check_braddr = sendpacket[15];
	check_mcaddr = sendpacket[12];
	check_braddr = check_braddr << 8;
	check_mcaddr = check_mcaddr << 8;
	check_braddr |= sendpacket[14];
	check_mcaddr |= sendpacket[13];
	check_braddr = check_braddr << 8;
	check_mcaddr = check_mcaddr << 8;
	check_braddr |= sendpacket[13];
	check_mcaddr |= sendpacket[14];
	check_braddr = check_braddr << 8;
	check_mcaddr = check_mcaddr << 8;
	check_braddr |= sendpacket[12];
	check_mcaddr |= sendpacket[15];
	/* if the Source Address is a Multicast address */
	if ((ppp_priv_area->mc == WANOPT_NO) && (check_mcaddr >= 0xE0000001)
	    && (check_mcaddr <= 0xFFFFFFFE)) {
		printk(KERN_INFO "%s: Mutlicast Src. Addr. silently discarded\n"
		       ,card->devname);
		dev_kfree_skb(skb);
		++ppp_priv_area->if_send_multicast;
		++card->wandev.stats.tx_dropped;
		return 0;
	}
	disable_irq(card->hw.irq);
	++card->irq_dis_if_send_count;
	if (test_and_set_bit(0, (void *) &card->wandev.critical)) {
		if (card->wandev.critical == CRITICAL_IN_ISR) {
			/* If the critical flag is set due to an Interrupt
			 * then set enable transmit interrupt flag to enable
			 * transmit interrupt. (delay interrupt)
			 */
			card->wandev.enable_tx_int = 1;
			dev->tbusy = 1;
			/* set the counter to see if we get the interrupt in
			 * 5 seconds. 
			 */
			ppp_priv_area->tick_counter = jiffies;
			++ppp_priv_area->if_send_critical_ISR;
			save_flags(host_cpu_flags);
			cli();
			if ((!(--card->irq_dis_if_send_count)) &&
			    (!card->irq_dis_poll_count))
				enable_irq(card->hw.irq);
			restore_flags(host_cpu_flags);
			return 1;
		}
		dev_kfree_skb(skb);
		++ppp_priv_area->if_send_critical_non_ISR;
		save_flags(host_cpu_flags);
		cli();
		if ((!(--card->irq_dis_if_send_count)) &&
		    (!card->irq_dis_poll_count))
			enable_irq(card->hw.irq);
		restore_flags(host_cpu_flags);
		return 0;
	}
	if (udp_type == UDP_PTPIPE_TYPE) {
		err = process_udp_mgmt_pkt(UDP_PKT_FRM_STACK, card, skb,
					   dev, ppp_priv_area);
	} else if (card->wandev.state != WAN_CONNECTED) {
		++ppp_priv_area->if_send_wan_disconnected;
		++card->wandev.stats.tx_dropped;
	} else if (!skb->protocol) {
		++ppp_priv_area->if_send_protocol_error;
		++card->wandev.stats.tx_errors;
	} else {
		/*If it's IPX change the network numbers to 0 if they're ours. */
		if (skb->protocol == ETH_P_IPX) {
			if (card->wandev.enable_IPX) {
				switch_net_numbers(skb->data,
					 card->wandev.network_number, 0);
			} else {
				++card->wandev.stats.tx_dropped;
				goto tx_done;
			}
		}
		if (ppp_send(card, skb->data, skb->len, skb->protocol)) {
			retry = 1;
			dev->tbusy = 1;
			++ppp_priv_area->if_send_adptr_bfrs_full;
			++ppp_priv_area->if_send_tx_int_enabled;
			ppp_priv_area->tick_counter = jiffies;
			++card->wandev.stats.tx_errors;
			flags->imask |= 0x02;	/* unmask Tx interrupts */
		} else {
			++ppp_priv_area->if_send_bfr_passed_to_adptr;
			++card->wandev.stats.tx_packets;
			card->wandev.stats.tx_bytes += skb->len;
		}
	}
tx_done:
	if (!retry) {
		dev_kfree_skb(skb);
	}
	card->wandev.critical = 0;
	save_flags(host_cpu_flags);
	cli();
	if ((!(--card->irq_dis_if_send_count)) && (!card->irq_dis_poll_count))
		enable_irq(card->hw.irq);
	restore_flags(host_cpu_flags);
	return retry;
}

/*============================================================================
 * Reply to UDP Management system.
 * Return length of reply.
 */

static int reply_udp(unsigned char *data, unsigned int mbox_len)
{
	unsigned short len, udp_length, temp, i, ip_length;
	unsigned long sum;
	/* Set length of packet */
	len = mbox_len + 60;
	/* fill in UDP reply */
	data[36] = 0x02;
	/* fill in UDP length */
	udp_length = mbox_len + 40;
	/* put it on an even boundary */
	if (udp_length & 0x0001) {
		udp_length += 1;
		len += 1;
	}
	temp = (udp_length << 8) | (udp_length >> 8);
	memcpy(&data[24], &temp, 2);
	/* swap UDP ports */
	memcpy(&temp, &data[20], 2);
	memcpy(&data[20], &data[22], 2);
	memcpy(&data[22], &temp, 2);
	/* add UDP pseudo header */
	temp = 0x1100;
	memcpy(&data[udp_length + 20], &temp, 2);
	temp = (udp_length << 8) | (udp_length >> 8);
	memcpy(&data[udp_length + 22], &temp, 2);
	/* calculate UDP checksum */
	data[26] = data[27] = 0;
	sum = 0;
	for (i = 0; i < udp_length + 12; i += 2) {
		memcpy(&temp, &data[12 + i], 2);
		sum += (unsigned long) temp;
	}
	while (sum >> 16) {
		sum = (sum & 0xffffUL) + (sum >> 16);
	}
	temp = (unsigned short) sum;
	temp = ~temp;
	if (temp == 0)
		temp = 0xffff;
	memcpy(&data[26], &temp, 2);
	/* fill in IP length */
	ip_length = udp_length + 20;
	temp = (ip_length << 8) | (ip_length >> 8);
	memcpy(&data[2], &temp, 2);
	/* swap IP addresses */
	memcpy(&temp, &data[12], 2);
	memcpy(&data[12], &data[16], 2);
	memcpy(&data[16], &temp, 2);
	memcpy(&temp, &data[14], 2);
	memcpy(&data[14], &data[18], 2);
	memcpy(&data[18], &temp, 2);
	/* fill in IP checksum */
	data[10] = data[11] = 0;
	sum = 0;
	for (i = 0; i < 20; i += 2) {
		memcpy(&temp, &data[i], 2);
		sum += (unsigned long) temp;
	}
	while (sum >> 16) {
		sum = (sum & 0xffffUL) + (sum >> 16);
	}
	temp = (unsigned short) sum;
	temp = ~temp;
	if (temp == 0)
		temp = 0xffff;
	memcpy(&data[10], &temp, 2);
	return len;
}				/* reply_udp */

/*
   If incoming is 0 (outgoing)- if the net numbers is ours make it 0
   if incoming is 1 - if the net number is 0 make it ours 
 */

static void switch_net_numbers(unsigned char *sendpacket, unsigned long network_number, unsigned char incoming)
{
	unsigned long pnetwork_number;
	pnetwork_number = (unsigned long) ((sendpacket[6] << 24) +
			   (sendpacket[7] << 16) + (sendpacket[8] << 8) +
					   sendpacket[9]);
	if (!incoming) {
		/* If the destination network number is ours, make it 0 */
		if (pnetwork_number == network_number) {
			sendpacket[6] = sendpacket[7] = sendpacket[8] =
			    sendpacket[9] = 0x00;
		}
	} else {
		/* If the incoming network is 0, make it ours */
		if (pnetwork_number == 0) {
			sendpacket[6] = (unsigned char) (network_number >> 24);
			sendpacket[7] = (unsigned char) ((network_number &
						      0x00FF0000) >> 16);
			sendpacket[8] = (unsigned char) ((network_number &
						       0x0000FF00) >> 8);
			sendpacket[9] = (unsigned char) (network_number &
							 0x000000FF);
		}
	}
	pnetwork_number = (unsigned long) ((sendpacket[18] << 24) +
			 (sendpacket[19] << 16) + (sendpacket[20] << 8) +
					   sendpacket[21]);
	if (!incoming) {
		/* If the source network is ours, make it 0 */
		if (pnetwork_number == network_number) {
			sendpacket[18] = sendpacket[19] = sendpacket[20] =
			    sendpacket[21] = 0x00;
		}
	} else {
		/* If the source network is 0, make it ours */
		if (pnetwork_number == 0) {
			sendpacket[18] = (unsigned char) (network_number >> 24);
			sendpacket[19] = (unsigned char) ((network_number &
						      0x00FF0000) >> 16);
			sendpacket[20] = (unsigned char) ((network_number &
						       0x0000FF00) >> 8);
			sendpacket[21] = (unsigned char) (network_number &
							  0x000000FF);
		}
	}
}				/* switch_net_numbers */

/*============================================================================
 * Get Ethernet-style interface statistics.
 * Return a pointer to struct enet_statistics.
 */

static struct enet_statistics *if_stats(struct device *dev)
{
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card;
	
	/*
	 *	Device is down:No statistics
	 */
	 
	if(ppp_priv_area==NULL)
		return NULL;
	
	card = ppp_priv_area->card;
	return &card->wandev.stats;
}

/****** PPP Firmware Interface Functions ************************************/

/*============================================================================
 * Read firmware code version.
 *	Put code version as ASCII string in str. 
 */

static int ppp_read_version(sdla_t * card, char *str)
{
	ppp_mbox_t *mb = card->mbox;
	int err;
	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_READ_CODE_VERSION;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK)
		ppp_error(card, err, mb);
	else if (str) {
		int len = mb->cmd.length;
		memcpy(str, mb->data, len);
		str[len] = '\0';
	}
	return err;
}

/*============================================================================
 * Configure PPP firmware.
 */

static int ppp_configure(sdla_t * card, void *data)
{
	ppp_mbox_t *mb = card->mbox;
	int data_len = (card->hw.fwid == SFID_PPP502) ?
	sizeof(ppp502_conf_t) : sizeof(ppp508_conf_t);
	int err;
	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	memcpy(mb->data, data, data_len);
	mb->cmd.length = data_len;
	mb->cmd.command = PPP_SET_CONFIG;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK)
		ppp_error(card, err, mb);
	return err;
}

/*============================================================================
 * Set interrupt mode.
 */

static int ppp_set_intr_mode(sdla_t * card, unsigned mode)
{
	ppp_mbox_t *mb = card->mbox;
	int err;
	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->data[0] = mode;
	switch (card->hw.fwid) {
	case SFID_PPP502:
		mb->cmd.length = 1;
		break;
	case SFID_PPP508:
	default:
		mb->data[1] = card->hw.irq;
		mb->cmd.length = 2;
	}
	mb->cmd.command = PPP_SET_INTR_FLAGS;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK)
		ppp_error(card, err, mb);
	return err;
}

/*============================================================================
 * Enable communications.
 */

static int ppp_comm_enable(sdla_t * card)
{
	ppp_mbox_t *mb = card->mbox;
	int err;
	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_COMM_ENABLE;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK)
		ppp_error(card, err, mb);
	return err;
}

/*============================================================================
 * Disable communications.
 */

static int ppp_comm_disable(sdla_t * card)
{
	ppp_mbox_t *mb = card->mbox;
	int err;
	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_COMM_DISABLE;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK)
		ppp_error(card, err, mb);
	return err;
}

/*============================================================================
 * Get communications error statistics.
 */

static int ppp_get_err_stats(sdla_t * card)
{
	ppp_mbox_t *mb = card->mbox;
	int err;
	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_READ_ERROR_STATS;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err == CMD_OK) {
		ppp_err_stats_t *stats = (void *) mb->data;
		card->wandev.stats.rx_over_errors = stats->rx_overrun;
		card->wandev.stats.rx_crc_errors = stats->rx_bad_crc;
		card->wandev.stats.rx_missed_errors = stats->rx_abort;
		card->wandev.stats.rx_length_errors = stats->rx_lost;
		card->wandev.stats.tx_aborted_errors = stats->tx_abort;
	} else
		ppp_error(card, err, mb);
	return err;
}

/*============================================================================
 * Send packet.
 *	Return:	0 - o.k.
 *		1 - no transmit buffers available
 */

static int ppp_send(sdla_t * card, void *data, unsigned len, unsigned proto)
{
	ppp_buf_ctl_t *txbuf = card->u.p.txbuf;
	unsigned long addr;
	if (txbuf->flag)
		return 1
		    ;
	if (card->hw.fwid == SFID_PPP502)
		addr = (txbuf->buf.o_p[1] << 8) + txbuf->buf.o_p[0];
	else
		addr = txbuf->buf.ptr;
	sdla_poke(&card->hw, addr, data, len);
	txbuf->length = len;	/* frame length */
	if (proto == ETH_P_IPX)
		txbuf->proto = 0x01;	/* protocol ID */
	txbuf->flag = 1;	/* start transmission */
	/* Update transmit buffer control fields */
	card->u.p.txbuf = ++txbuf;
	if ((void *) txbuf > card->u.p.txbuf_last)
		card->u.p.txbuf = card->u.p.txbuf_base;
	return 0;
}

/****** Firmware Error Handler **********************************************/

/*============================================================================
 * Firmware error handler.
 *	This routine is called whenever firmware command returns non-zero
 *	return code.
 *
 * Return zero if previous command has to be cancelled.
 */

static int ppp_error(sdla_t * card, int err, ppp_mbox_t * mb)
{
	unsigned cmd = mb->cmd.command;
	switch (err) {
	case CMD_TIMEOUT:
		printk(KERN_ERR "%s: command 0x%02X timed out!\n",
		       card->devname, cmd);
		break;
	default:
		printk(KERN_INFO "%s: command 0x%02X returned 0x%02X!\n"
		       ,card->devname, cmd, err);
	}
	return 0;
}

/****** Interrupt Handlers **************************************************/

/*============================================================================
 * PPP interrupt service routine.
 */

STATIC void wpp_isr(sdla_t * card)
{
	ppp_flags_t *flags = card->flags;
	char *ptr = &flags->iflag;
	unsigned long host_cpu_flags;
	struct device *dev = card->wandev.dev;
	int i;
	card->in_isr = 1;
	++card->statistics.isr_entry;
	if (test_and_set_bit(0, (void *) &card->wandev.critical)) {
		++card->statistics.isr_already_critical;
		printk(KERN_INFO "%s: Critical while in ISR!\n", card->devname);
		card->in_isr = 0;
		return;
	}
	/* For all interrupts set the critical flag to CRITICAL_IN_ISR. 
	 * If the if_send routine is called with this flag set it will set 
	 * the enable transmit flag to 1. (for a delayed interrupt) 
	 */
	card->wandev.critical = CRITICAL_IN_ISR;
	card->buff_int_mode_unbusy = 0;
	switch (flags->iflag) {
	case 0x01:		/* receive interrupt */
		++card->statistics.isr_rx;
		rx_intr(card);
		break;
	case 0x02:		/* transmit interrupt */
		++card->statistics.isr_tx;
		flags->imask &= ~0x02;
		dev->tbusy = 0;
		card->buff_int_mode_unbusy = 1;
		break;
	case 0x08:
		++Intr_test_counter;
		++card->statistics.isr_intr_test;
		break;
	default:		/* unexpected interrupt */
		++card->statistics.isr_spurious;
		printk(KERN_INFO "%s: spurious interrupt 0x%02X!\n",
		       card->devname, flags->iflag);
		printk(KERN_INFO "%s: ID Bytes = ", card->devname);
		for (i = 0; i < 8; i++)
			printk(KERN_INFO "0x%02X ", *(ptr + 0x28 + i));
		printk(KERN_INFO "\n");
	}
	/* The critical flag is set to CRITICAL_INTR_HANDLED to let the
	 * if_send call know that the interrupt is handled so that 
	 * transmit interrupts are not enabled again.
	 */
	card->wandev.critical = CRITICAL_INTR_HANDLED;
	/* If the enable transmit interrupt flag is set then enable transmit 
	 * interrupt on the board. This only goes through if if_send is called 
	 * and the critical flag is set due to an Interrupt. 
	 */
	if (card->wandev.enable_tx_int) {
		flags->imask |= 0x02;
		card->wandev.enable_tx_int = 0;
		++card->statistics.isr_enable_tx_int;
	}
	save_flags(host_cpu_flags);
	cli();
	card->in_isr = 0;
	flags->iflag = 0;
	card->wandev.critical = 0;
	restore_flags(host_cpu_flags);
	if (card->buff_int_mode_unbusy)
		mark_bh(NET_BH);
}

/*============================================================================
 * Receive interrupt handler.
 */

static void rx_intr(sdla_t * card)
{
	ppp_buf_ctl_t *rxbuf = card->rxmb;
	struct device *dev = card->wandev.dev;
	ppp_private_area_t *ppp_priv_area;
	struct sk_buff *skb;
	unsigned len;
	void *buf;
	int i, err;
	ppp_flags_t *flags = card->flags;
	char *ptr = &flags->iflag;
	int udp_type;
	if (rxbuf->flag != 0x01) {
		printk(KERN_INFO
		       "%s: corrupted Rx buffer @ 0x%X, flag = 0x%02X!\n",
		       card->devname, (unsigned) rxbuf, rxbuf->flag);
		printk(KERN_INFO "%s: ID Bytes = ", card->devname);
		for (i = 0; i < 8; i++)
			printk(KERN_INFO "0x%02X ", *(ptr + 0x28 + i));
		printk(KERN_INFO "\n");
		++card->statistics.rx_intr_corrupt_rx_bfr;
		return;
	}
	if (dev && dev->start) {
		len = rxbuf->length;
		ppp_priv_area = dev->priv;
		/* Allocate socket buffer */
		skb = dev_alloc_skb(len);
		if (skb != NULL) {
			/* Copy data to the socket buffer */
			if (card->hw.fwid == SFID_PPP502) {
				unsigned addr = (rxbuf->buf.o_p[1] << 8) +
				rxbuf->buf.o_p[0];
				buf = skb_put(skb, len);
				sdla_peek(&card->hw, addr, buf, len);
			} else {
				unsigned addr = rxbuf->buf.ptr;
				if ((addr + len) > card->u.p.rx_top + 1) {
					unsigned tmp = card->u.p.rx_top - addr
					+ 1;
					buf = skb_put(skb, tmp);
					sdla_peek(&card->hw, addr, buf, tmp);
					addr = card->u.p.rx_base;
					len -= tmp;
				}
				buf = skb_put(skb, len);
				sdla_peek(&card->hw, addr, buf, len);
			}
			/* Decapsulate packet */
			switch (rxbuf->proto) {
			case 0x00:
				skb->protocol = htons(ETH_P_IP);
				break;
			case 0x01:
				skb->protocol = htons(ETH_P_IPX);
				break;
			}
			udp_type = udp_pkt_type(skb, card);
			if (udp_type == UDP_DRVSTATS_TYPE) {
				++ppp_priv_area->rx_intr_DRVSTATS_request;
				process_udp_driver_call(
					  UDP_PKT_FRM_NETWORK, card, skb,
						     dev, ppp_priv_area);
				dev_kfree_skb(skb);
			} else if (udp_type == UDP_PTPIPE_TYPE) {
				++ppp_priv_area->rx_intr_PTPIPE_request;
				err = process_udp_mgmt_pkt(
					       UDP_PKT_FRM_NETWORK, card,
						skb, dev, ppp_priv_area);
				dev_kfree_skb(skb);
			} else if (handle_IPXWAN(skb->data, card->devname, card->wandev.enable_IPX, card->wandev.network_number, skb->protocol)) {
				if (card->wandev.enable_IPX) {
					ppp_send(card, skb->data, skb->len, ETH_P_IPX);
					dev_kfree_skb(skb);
				} else {
					++card->wandev.stats.rx_dropped;
				}
			} else {
				/* Pass it up the protocol stack */
				skb->dev = dev;
				skb->mac.raw = skb->data;
				netif_rx(skb);
				++card->wandev.stats.rx_packets;
 				card->wandev.stats.rx_bytes += skb->len;
				++ppp_priv_area->rx_intr_bfr_passed_to_stack;
			}
		} else {
			printk(KERN_INFO "%s: no socket buffers available!\n",
			       card->devname);
			++card->wandev.stats.rx_dropped;
			++ppp_priv_area->rx_intr_no_socket;
		}
	} else
		++card->statistics.rx_intr_dev_not_started;
	/* Release buffer element and calculate a pointer to the next one */
	rxbuf->flag = (card->hw.fwid == SFID_PPP502) ? 0xFF : 0x00;
	card->rxmb = ++rxbuf;
	if ((void *) rxbuf > card->u.p.rxbuf_last)
		card->rxmb = card->u.p.rxbuf_base;
}

/*============================================================================
 * Transmit interrupt handler.
 */

static void tx_intr(sdla_t * card)
{
	struct device *dev = card->wandev.dev;
	if (!dev || !dev->start) {
		++card->statistics.tx_intr_dev_not_started;
		return;
	}
	dev->tbusy = 0;
	mark_bh(NET_BH);
}

static int handle_IPXWAN(unsigned char *sendpacket, char *devname, unsigned char enable_IPX, unsigned long network_number, unsigned short proto)
{
	int i;
	if (proto == htons(ETH_P_IPX)) {
		/* It's an IPX packet */
		if (!enable_IPX) {
			/* Return 1 so we don't pass it up the stack. */
			return 1;
		}
	} else {
		/* It's not IPX so pass it up the stack. */
		return 0;
	}
	if (sendpacket[16] == 0x90 &&
	    sendpacket[17] == 0x04) {
		/* It's IPXWAN */
		if (sendpacket[2] == 0x02 &&
		    sendpacket[34] == 0x00) {
			/* It's a timer request packet */
			printk(KERN_INFO "%s: Received IPXWAN Timer Request packet\n", devname);
			/* Go through the routing options and answer no to every */
			/* option except Unnumbered RIP/SAP */
			for (i = 41; sendpacket[i] == 0x00; i += 5) {
				/* 0x02 is the option for Unnumbered RIP/SAP */
				if (sendpacket[i + 4] != 0x02) {
					sendpacket[i + 1] = 0;
				}
			}
			/* Skip over the extended Node ID option */
			if (sendpacket[i] == 0x04) {
				i += 8;
			}
			/* We also want to turn off all header compression opt. */
			for (; sendpacket[i] == 0x80;) {
				sendpacket[i + 1] = 0;
				i += (sendpacket[i + 2] << 8) + (sendpacket[i + 3]) + 4;
			}
			/* Set the packet type to timer response */
			sendpacket[34] = 0x01;
			printk(KERN_INFO "%s: Sending IPXWAN Timer Response\n", devname);
		} else if (sendpacket[34] == 0x02) {
			/* This is an information request packet */
			printk(KERN_INFO "%s: Received IPXWAN Information Request packet\n", devname);
			/* Set the packet type to information response */
			sendpacket[34] = 0x03;
			/* Set the router name */
			sendpacket[51] = 'P';
			sendpacket[52] = 'T';
			sendpacket[53] = 'P';
			sendpacket[54] = 'I';
			sendpacket[55] = 'P';
			sendpacket[56] = 'E';
			sendpacket[57] = '-';
			sendpacket[58] = CVHexToAscii(network_number >> 28);
			sendpacket[59] = CVHexToAscii((network_number & 0x0F000000) >> 24);
			sendpacket[60] = CVHexToAscii((network_number & 0x00F00000) >> 20);
			sendpacket[61] = CVHexToAscii((network_number & 0x000F0000) >> 16);
			sendpacket[62] = CVHexToAscii((network_number & 0x0000F000) >> 12);
			sendpacket[63] = CVHexToAscii((network_number & 0x00000F00) >> 8);
			sendpacket[64] = CVHexToAscii((network_number & 0x000000F0) >> 4);
			sendpacket[65] = CVHexToAscii(network_number & 0x0000000F);
			for (i = 66; i < 99; i += 1)
				sendpacket[i] = 0;
			printk(KERN_INFO "%s: Sending IPXWAN Information Response packet\n", devname);
		} else {
			printk(KERN_INFO "%s: Unknown IPXWAN packet!\n", devname);
			return 0;
		}
		/* Set the WNodeID to our network address */
		sendpacket[35] = (unsigned char) (network_number >> 24);
		sendpacket[36] = (unsigned char) ((network_number & 0x00FF0000) >> 16);
		sendpacket[37] = (unsigned char) ((network_number & 0x0000FF00) >> 8);
		sendpacket[38] = (unsigned char) (network_number & 0x000000FF);
		return 1;
	} else {
		/* If we get here's its an IPX-data packet, so it'll get passed up the stack. */
		/* switch the network numbers */
		switch_net_numbers(sendpacket, network_number, 1);
		return 0;
	}
}

/****** Background Polling Routines  ****************************************/

/*============================================================================
 * Main polling routine.
 * This routine is repeatedly called by the WANPIPE 'thread' to allow for
 * time-dependent housekeeping work.
 *
 * Notes:
 * 1. This routine may be called on interrupt context with all interrupts
 *    enabled. Beware!
 */

static void wpp_poll(sdla_t * card)
{
	struct device *dev = card->wandev.dev;
	ppp_flags_t *adptr_flags = card->flags;
	unsigned long host_cpu_flags;
	++card->statistics.poll_entry;
	/* The wpp_poll is called continously by the WANPIPE thread to allow
	 * for line state housekeeping. However if we are in a connected state
	 * then we do not need to go through all the checks everytime. When in
	 * connected state execute wpp_poll once every second.
	 */
	if (card->wandev.state == WAN_CONNECTED) {
		if ((jiffies - card->state_tick) < HZ)
			return;
	}
	disable_irq(card->hw.irq);
	++card->irq_dis_poll_count;
	if (test_and_set_bit(0, (void *) &card->wandev.critical)) {
		++card->statistics.poll_already_critical;
		printk(KERN_INFO "%s: critical inside wpp_poll\n",
		       card->devname);
		save_flags(host_cpu_flags);
		cli();
		if ((!card->irq_dis_if_send_count) &&
		    (!(--card->irq_dis_poll_count)))
			enable_irq(card->hw.irq);
		restore_flags(host_cpu_flags);
		return;
	}
	++card->statistics.poll_processed;
	if (dev && dev->tbusy && !(adptr_flags->imask & 0x02)) {
		++card->statistics.poll_tbusy_bad_status;
		printk(KERN_INFO "%s: Wpp_Poll: tbusy = 0x01, imask = 0x%02X\n"
		       ,card->devname, adptr_flags->imask);
	}
	switch (card->wandev.state) {
	case WAN_CONNECTED:
		card->state_tick = jiffies;
		poll_active(card);
		break;
	case WAN_CONNECTING:
		poll_connecting(card);
		break;
	case WAN_DISCONNECTED:
		poll_disconnected(card);
		break;
	default:
		printk(KERN_INFO "%s: Unknown Poll State 0x%02X\n",
		       card->devname, card->wandev.state);
		break;
	}
	card->wandev.critical = 0;
	save_flags(host_cpu_flags);
	cli();
	if ((!card->irq_dis_if_send_count) && (!(--card->irq_dis_poll_count)))
		enable_irq(card->hw.irq);
	restore_flags(host_cpu_flags);
}

/*============================================================================
 * Monitor active link phase.
 */

static void poll_active(sdla_t * card)
{
	ppp_flags_t *flags = card->flags;
	/* We check the lcp_state to see if we are in DISCONNECTED state.
	 * We are considered to be connected for lcp states 0x06, 0x07, 0x08
	 * and 0x09.
	 */
	if ((flags->lcp_state <= 0x05) || (flags->disc_cause & 0x03)) {
		wanpipe_set_state(card, WAN_DISCONNECTED);
		show_disc_cause(card, flags->disc_cause);
	}
}

/*============================================================================
 * Monitor link establishment phase.
 * o if connection timed out, disconnect the link.
 */

static void poll_connecting(sdla_t * card)
{
	ppp_flags_t *flags = card->flags;
	if (flags->lcp_state == 0x09) {
		wanpipe_set_state(card, WAN_CONNECTED);
	} else if (flags->disc_cause & 0x03) {
		wanpipe_set_state(card, WAN_DISCONNECTED);
		show_disc_cause(card, flags->disc_cause);
	}
}

/*============================================================================
 * Monitor physical link disconnected phase.
 *  o if interface is up and the hold-down timeout has expired, then retry
 *    connection.
 */

static void poll_disconnected(sdla_t * card)
{
	struct device *dev = card->wandev.dev;
	if (dev && dev->start &&
	    ((jiffies - card->state_tick) > HOLD_DOWN_TIME)) {
		wanpipe_set_state(card, WAN_CONNECTING);
		if (ppp_comm_enable(card) == CMD_OK)
			init_ppp_tx_rx_buff(card);
	}
}

/****** Miscellaneous Functions *********************************************/

/*============================================================================
 * Configure S502 adapter.
 */

static int config502(sdla_t * card)
{
	ppp502_conf_t cfg;
	/* Prepare PPP configuration structure */
	memset(&cfg, 0, sizeof(ppp502_conf_t));
	if (card->wandev.clocking)
		cfg.line_speed = bps_to_speed_code(card->wandev.bps);
	cfg.txbuf_num = 4;
	cfg.mtu_local = card->wandev.mtu;
	cfg.mtu_remote = card->wandev.mtu;
	cfg.restart_tmr = 30;
	cfg.auth_rsrt_tmr = 30;
	cfg.auth_wait_tmr = 300;
	cfg.mdm_fail_tmr = 5;
	cfg.dtr_drop_tmr = 1;
	cfg.connect_tmout = 0;	/* changed it from 900 */
	cfg.conf_retry = 10;
	cfg.term_retry = 2;
	cfg.fail_retry = 5;
	cfg.auth_retry = 10;
	cfg.ip_options = 0x80;
	cfg.ipx_options = 0xA0;
	cfg.conf_flags |= 0x0E;
/*
   cfg.ip_local         = dev->pa_addr;
   cfg.ip_remote                = dev->pa_dstaddr;
 */
	return ppp_configure(card, &cfg);
}

/*============================================================================
 * Configure S508 adapter.
 */

static int config508(sdla_t * card)
{
	ppp508_conf_t cfg;
	/* Prepare PPP configuration structure */
	memset(&cfg, 0, sizeof(ppp508_conf_t));
	if (card->wandev.clocking)
		cfg.line_speed = card->wandev.bps;
	if (card->wandev.interface == WANOPT_RS232)
		cfg.conf_flags |= 0x0020;
	cfg.conf_flags |= 0x300;	/*send Configure-Request packets forever */
	cfg.txbuf_percent = 60;	/* % of Tx bufs */
	cfg.mtu_local = card->wandev.mtu;
	cfg.mtu_remote = card->wandev.mtu;
	cfg.restart_tmr = 30;
	cfg.auth_rsrt_tmr = 30;
	cfg.auth_wait_tmr = 300;
	cfg.mdm_fail_tmr = 100;
	cfg.dtr_drop_tmr = 1;
	cfg.connect_tmout = 0;	/* changed it from 900 */
	cfg.conf_retry = 10;
	cfg.term_retry = 2;
	cfg.fail_retry = 5;
	cfg.auth_retry = 10;
	cfg.ip_options = 0x80;
	cfg.ipx_options = 0xA0;
/*
   cfg.ip_local         = dev->pa_addr;
   cfg.ip_remote                = dev->pa_dstaddr;
 */
	return ppp_configure(card, &cfg);
}

/*============================================================================
 * Show disconnection cause.
 */

static void show_disc_cause(sdla_t * card, unsigned cause)
{
	if (cause & 0x0002)
		printk(KERN_INFO "%s: link terminated by peer\n",
		       card->devname);
	else if (cause & 0x0004)
		printk(KERN_INFO "%s: link terminated by user\n",
		       card->devname);
	else if (cause & 0x0008)
		printk(KERN_INFO "%s: authentication failed\n", card->devname);
	else if (cause & 0x0010)
		printk(KERN_INFO
		       "%s: authentication protocol negotiation failed\n",
		       card->devname);
	else if (cause & 0x0020)
		printk(KERN_INFO
		       "%s: peer's request for authentication rejected\n",
		       card->devname);
	else if (cause & 0x0040)
		printk(KERN_INFO "%s: MRU option rejected by peer\n",
		       card->devname);
	else if (cause & 0x0080)
		printk(KERN_INFO "%s: peer's MRU was too small\n",
		       card->devname);
	else if (cause & 0x0100)
		printk(KERN_INFO "%s: failed to negotiate peer's LCP options\n",
		       card->devname);
	else if (cause & 0x0200)
		printk(KERN_INFO "%s: failed to negotiate peer's IPCP options\n"
		       ,card->devname);
	else if (cause & 0x0400)
		printk(KERN_INFO
		       "%s: failed to negotiate peer's IPXCP options\n",
		       card->devname);
}

/*============================================================================
 * Convert line speed in bps to a number used by S502 code.
 */

static unsigned char bps_to_speed_code(unsigned long bps)
{
	unsigned char number;
	if (bps <= 1200)
		number = 0x01;
	else if (bps <= 2400)
		number = 0x02;
	else if (bps <= 4800)
		number = 0x03;
	else if (bps <= 9600)
		number = 0x04;
	else if (bps <= 19200)
		number = 0x05;
	else if (bps <= 38400)
		number = 0x06;
	else if (bps <= 45000)
		number = 0x07;
	else if (bps <= 56000)
		number = 0x08;
	else if (bps <= 64000)
		number = 0x09;
	else if (bps <= 74000)
		number = 0x0A;
	else if (bps <= 112000)
		number = 0x0B;
	else if (bps <= 128000)
		number = 0x0C;
	else
		number = 0x0D;
	return number;
}

/*============================================================================
 * Process UDP call of type DRVSTATS.  
 */

static int process_udp_driver_call(char udp_pkt_src, sdla_t * card, struct sk_buff *skb, struct device *dev, ppp_private_area_t * ppp_priv_area)
{
	unsigned char *sendpacket;
	unsigned char buf2[5];
	unsigned char *data;
	unsigned char *buf;
	unsigned int len;
	ppp_mbox_t *mbox = card->mbox;
	struct sk_buff *new_skb;
	int err;
	sendpacket = skb->data;
	memcpy(&buf2, &card->wandev.udp_port, 2);
	if ((data = kmalloc(2000, GFP_ATOMIC)) == NULL) {
		printk(KERN_INFO
		       "%s: Error allocating memory for UDP DRIVER STATS cmnd0x%02X"
		       ,card->devname, data[45]);
		++ppp_priv_area->UDP_DRVSTATS_mgmt_kmalloc_err;
		return 1;
	}
	memcpy(data, sendpacket, skb->len);
	switch (data[45]) {
		/* PPIPE_DRIVER_STATISTICS */
	case 0x26:
		*(unsigned long *) &data[60] =
		    ppp_priv_area->if_send_entry;
		*(unsigned long *) &data[64] =
		    ppp_priv_area->if_send_skb_null;
		*(unsigned long *) &data[68] =
		    ppp_priv_area->if_send_broadcast;
		*(unsigned long *) &data[72] =
		    ppp_priv_area->if_send_multicast;
		*(unsigned long *) &data[76] =
		    ppp_priv_area->if_send_critical_ISR;
		*(unsigned long *) &data[80] =
		    ppp_priv_area->if_send_critical_non_ISR;
		*(unsigned long *) &data[84] =
		    ppp_priv_area->if_send_busy;
		*(unsigned long *) &data[88] =
		    ppp_priv_area->if_send_busy_timeout;
		*(unsigned long *) &data[92] =
		    ppp_priv_area->if_send_DRVSTATS_request;
		*(unsigned long *) &data[96] =
		    ppp_priv_area->if_send_PTPIPE_request;
		*(unsigned long *) &data[100] =
		    ppp_priv_area->if_send_wan_disconnected;
		*(unsigned long *) &data[104] =
		    ppp_priv_area->if_send_adptr_bfrs_full;
		*(unsigned long *) &data[108] =
		    ppp_priv_area->if_send_protocol_error;
		*(unsigned long *) &data[112] =
		    ppp_priv_area->if_send_tx_int_enabled;
		*(unsigned long *) &data[116] =
		    ppp_priv_area->if_send_bfr_passed_to_adptr;
		*(unsigned long *) &data[118] =
		    card->irq_dis_if_send_count;
		mbox->cmd.length = 62;
		break;
	case 0x27:
		*(unsigned long *) &data[60] = card->statistics.isr_entry;
		*(unsigned long *) &data[64] =
		    card->statistics.isr_already_critical;
		*(unsigned long *) &data[68] = card->statistics.isr_rx;
		*(unsigned long *) &data[72] = card->statistics.isr_tx;
		*(unsigned long *) &data[76] =
		    card->statistics.isr_intr_test;
		*(unsigned long *) &data[80] =
		    card->statistics.isr_spurious;
		*(unsigned long *) &data[84] =
		    card->statistics.isr_enable_tx_int;
		*(unsigned long *) &data[88] =
		    card->statistics.rx_intr_corrupt_rx_bfr;
		*(unsigned long *) &data[92] =
		    ppp_priv_area->rx_intr_no_socket;
		*(unsigned long *) &data[96] =
		    ppp_priv_area->rx_intr_DRVSTATS_request;
		*(unsigned long *) &data[100] =
		    ppp_priv_area->rx_intr_PTPIPE_request;
		*(unsigned long *) &data[104] =
		    ppp_priv_area->rx_intr_bfr_passed_to_stack;
		*(unsigned long *) &data[108] =
		    card->statistics.rx_intr_dev_not_started;
		*(unsigned long *) &data[112] =
		    card->statistics.tx_intr_dev_not_started;
		mbox->cmd.length = 56;
		break;
	case 0x28:
		*(unsigned long *) &data[60] =
		    ppp_priv_area->UDP_PTPIPE_mgmt_kmalloc_err;
		*(unsigned long *) &data[64] =
		    ppp_priv_area->UDP_PTPIPE_mgmt_adptr_type_err;
		*(unsigned long *) &data[68] =
		    ppp_priv_area->UDP_PTPIPE_mgmt_direction_err;
		*(unsigned long *) &data[72] =
		    ppp_priv_area->
		    UDP_PTPIPE_mgmt_adptr_cmnd_timeout;
		*(unsigned long *) &data[76] =
		    ppp_priv_area->UDP_PTPIPE_mgmt_adptr_cmnd_OK;
		*(unsigned long *) &data[80] =
		    ppp_priv_area->UDP_PTPIPE_mgmt_passed_to_adptr;
		*(unsigned long *) &data[84] =
		    ppp_priv_area->UDP_PTPIPE_mgmt_passed_to_stack;
		*(unsigned long *) &data[88] =
		    ppp_priv_area->UDP_PTPIPE_mgmt_no_socket;
		*(unsigned long *) &data[92] =
		    ppp_priv_area->UDP_DRVSTATS_mgmt_kmalloc_err;
		*(unsigned long *) &data[96] =
		    ppp_priv_area->
		    UDP_DRVSTATS_mgmt_adptr_cmnd_timeout;
		*(unsigned long *) &data[100] =
		    ppp_priv_area->UDP_DRVSTATS_mgmt_adptr_cmnd_OK;
		*(unsigned long *) &data[104] =
		    ppp_priv_area->
		    UDP_DRVSTATS_mgmt_passed_to_adptr;
		*(unsigned long *) &data[108] =
		    ppp_priv_area->
		    UDP_DRVSTATS_mgmt_passed_to_stack;
		*(unsigned long *) &data[112] =
		    ppp_priv_area->UDP_DRVSTATS_mgmt_no_socket;
		*(unsigned long *) &data[116] =
		    card->statistics.poll_entry;
		*(unsigned long *) &data[120] =
		    card->statistics.poll_already_critical;
		*(unsigned long *) &data[124] =
		    card->statistics.poll_processed;
		*(unsigned long *) &data[126] =
		    card->irq_dis_poll_count;
		mbox->cmd.length = 70;
		break;
	default:
		/* it's a board command */
		memcpy(&mbox->cmd, &sendpacket[45], sizeof(ppp_cmd_t));
		if (mbox->cmd.length) {
			memcpy(&mbox->data, &sendpacket[60],
			       mbox->cmd.length);
		}
		/* run the command on the board */
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
		if (err != CMD_OK) {
			ppp_error(card, err, mbox);
			++ppp_priv_area->
			    UDP_DRVSTATS_mgmt_adptr_cmnd_timeout;
			break;
		}
		++ppp_priv_area->UDP_DRVSTATS_mgmt_adptr_cmnd_OK;
		/* copy the result back to our buffer */
		memcpy(data, sendpacket, skb->len);
		memcpy(&data[45], &mbox->cmd, sizeof(ppp_cmd_t));
		if (mbox->cmd.length) {
			memcpy(&data[60], &mbox->data, mbox->cmd.length);
		}
	}
	/* Fill UDP TTL */
	data[8] = card->wandev.ttl;
	len = reply_udp(data, mbox->cmd.length);
	if (udp_pkt_src == UDP_PKT_FRM_NETWORK) {
		++ppp_priv_area->UDP_DRVSTATS_mgmt_passed_to_adptr;
		ppp_send(card, data, len, skb->protocol);
	} else {
		/* Pass it up the stack
		   Allocate socket buffer */
		if ((new_skb = dev_alloc_skb(len)) != NULL) {
			/* copy data into new_skb */
			buf = skb_put(new_skb, len);
			memcpy(buf, data, len);
			++ppp_priv_area->UDP_DRVSTATS_mgmt_passed_to_stack;
			/* Decapsulate packet and pass it up the protocol 
			   stack */
			new_skb->protocol = htons(ETH_P_IP);
			new_skb->dev = dev;
			new_skb->mac.raw = new_skb->data;
			netif_rx(new_skb);
		} else {
			++ppp_priv_area->UDP_DRVSTATS_mgmt_no_socket;
			printk(KERN_INFO "no socket buffers available!\n");
		}
	}
	kfree(data);
	return 0;
}

/*=============================================================================
 * Process UDP call of type PTPIPEAB.
 */

static int process_udp_mgmt_pkt(char udp_pkt_src, sdla_t * card,
				struct sk_buff *skb, struct device *dev,
				ppp_private_area_t * ppp_priv_area)
{
	unsigned char *sendpacket;
	unsigned char buf2[5];
	unsigned char *data;
	unsigned char *buf;
	unsigned int frames, len;
	struct sk_buff *new_skb;
	unsigned short buffer_length, real_len;
	unsigned long data_ptr;
	int udp_mgmt_req_valid = 1;
	ppp_mbox_t *mbox = card->mbox;
	struct timeval tv;
	int err;
	sendpacket = skb->data;
	memcpy(&buf2, &card->wandev.udp_port, 2);
	if ((data = kmalloc(2000, GFP_ATOMIC)) == NULL) {
		printk(KERN_INFO
		       "%s: Error allocating memory for UDP management cmnd0x%02X"
		       ,card->devname, data[45]);
		++ppp_priv_area->UDP_PTPIPE_mgmt_kmalloc_err;
		return 1;
	}
	memcpy(data, sendpacket, skb->len);
	switch (data[45]) {
		/* FT1 MONITOR STATUS */
	case 0x80:
		if (card->hw.fwid != SFID_PPP508) {
			++ppp_priv_area->UDP_PTPIPE_mgmt_adptr_type_err;
			udp_mgmt_req_valid = 0;
			break;
		}
		/* PPIPE_ENABLE_TRACING */
	case 0x20:
		/* PPIPE_DISABLE_TRACING */
	case 0x21:
		/* PPIPE_GET_TRACE_INFO */
	case 0x22:
		/* SET FT1 MODE */
	case 0x81:
		if (udp_pkt_src == UDP_PKT_FRM_NETWORK) {
			++ppp_priv_area->UDP_PTPIPE_mgmt_direction_err;
			udp_mgmt_req_valid = 0;
		}
		break;
	default:
		break;
	}
	if (!udp_mgmt_req_valid) {
		/* set length to 0 */
		data[46] = data[47] = 0;
		/* set return code */
		data[48] = 0xCD;
	} else {
		switch (data[45]) {
			/* PPIPE_ENABLE_TRACING */
		case 0x20:
			if (!TracingEnabled) {
				/* OPERATE_DATALINE_MONITOR */
				mbox->cmd.command = 0x33;
				mbox->cmd.length = 1;
				mbox->data[0] = 0x03;
				err = sdla_exec(mbox) ?
				    mbox->cmd.result : CMD_TIMEOUT;
				if (err != CMD_OK) {
					ppp_error(card, err, mbox);
					TracingEnabled = 0;
					/* set the return code */
					data[48] = mbox->cmd.result;
					mbox->cmd.length = 0;
					break;
				}
				if (card->hw.fwid == SFID_PPP502) {
					sdla_peek(&card->hw, 0x9000, &buf2, 2);
				} else {
					sdla_peek(&card->hw, 0xC000, &buf2, 2);
				}
				curr_trace_addr = 0;
				memcpy(&curr_trace_addr, &buf2, 2);
				start_trace_addr = curr_trace_addr;
				/* MAX_SEND_BUFFER_SIZE -sizeof(UDP_MGMT_PACKET)
				   - 41 */
				available_buffer_space = 1926;
			}
			data[48] = 0;
			mbox->cmd.length = 0;
			TracingEnabled = 1;
			break;
			/* PPIPE_DISABLE_TRACING */
		case 0x21:
			if (TracingEnabled) {
				/* OPERATE_DATALINE_MONITOR */
				mbox->cmd.command = 0x3;
				mbox->cmd.length = 1;
				mbox->data[0] = 0x00;
				err = sdla_exec(mbox) ?
				    mbox->cmd.result : CMD_TIMEOUT;
			}
			/*set return code */
			data[48] = 0;
			mbox->cmd.length = 0;
			TracingEnabled = 0;
			break;
			/* PPIPE_GET_TRACE_INFO */
		case 0x22:
			if (TracingEnabled) {
				buffer_length = 0;
				/* frames < NUM_TRACE_FRAMES */
				for (frames = 0; frames < 62; frames += 1) {
					sdla_peek(&card->hw, curr_trace_addr,
						  &buf2, 1);
					/* no data on board so exit */
					if (buf2[0] == 0x00)
						break;
					/*1+sizeof(FRAME_DATA) = 9 */
					if ((available_buffer_space -
					     buffer_length) < 9) {
						/*indicate we have more frames 
						   on board and exit */
						data[60] |= 0x02;
						break;
					}
					/* get frame status */
					sdla_peek(&card->hw, curr_trace_addr +
						  0x01, &data[60 + buffer_length], 1);
					/* get time stamp */
					sdla_peek(&card->hw, curr_trace_addr +
						  0x06, &data[64 + buffer_length], 2);
					/* get frame length */
					sdla_peek(&card->hw, curr_trace_addr +
						  0x02, &data[62 + buffer_length], 2);
					/* get pointer to real data */
					sdla_peek(&card->hw, curr_trace_addr +
						  0x04, &buf2, 2);
					data_ptr = 0;
					memcpy(&data_ptr, &buf2, 2);
					/* see if we can fit the frame into the 
					   user buffer */
					memcpy(&real_len,
					   &data[62 + buffer_length], 2);
					if ((data_ptr == 0) ||
					    ((real_len + 8) >
					     available_buffer_space)) {
						data[61 + buffer_length] = 0x00;
					} else {
						/* we can take it next time */
						if ((available_buffer_space -
						     buffer_length) <
						    (real_len + 8)) {
							data[60] |= 0x02;
							break;
						}
						/* ok, get the frame */
						data[61 + buffer_length] = 0x01;
						/* get the data */
						sdla_peek(&card->hw, data_ptr,
						&data[66 + buffer_length],
							  real_len);
						/* zero the opp flag to 
						   show we got the frame */
						buf2[0] = 0x00;
						sdla_poke(&card->hw,
							  curr_trace_addr, &buf2, 1);
						/* now move onto the next 
						   frame */
						curr_trace_addr += 8;
						/* check if we passed the last 
						   address */
						if (curr_trace_addr >=
						    start_trace_addr + 0x1F0) {
							curr_trace_addr =
							    start_trace_addr;
						}
						/* update buffer length and make                                                   sure its even */
						if (data[61 + buffer_length]
						    == 0x01) {
							buffer_length +=
							    real_len - 1;
						}
						/* for the header */
						buffer_length += 8;
						if (buffer_length & 0x0001)
							buffer_length += 1;
					}
				}
				/* ok now set the total number of frames passed
				   in the high 5 bits */
				data[60] = (frames << 2) | data[60];
				/* set the data length */
				mbox->cmd.length = buffer_length;
				memcpy(&data[46], &buffer_length, 2);
				/* set return code */
				data[48] = 0;
			} else {
				/* set return code */
				data[48] = 1;
				mbox->cmd.length = 0;
			}
			break;
			/* PPIPE_GET_IBA_DATA */
		case 0x23:
			mbox->cmd.length = 0x09;
			if (card->hw.fwid == SFID_PPP502) {
				sdla_peek(&card->hw, 0xA003, &data[60],
					  mbox->cmd.length);
			} else {
				sdla_peek(&card->hw, 0xF003, &data[60],
					  mbox->cmd.length);
			}
			/* set the length of the data */
			data[46] = 0x09;
			/* set return code */
			data[48] = 0x00;
			break;
			/* PPIPE_KILL_BOARD */
		case 0x24:
			break;
			/* PPIPE_FT1_READ_STATUS */
		case 0x25:
			sdla_peek(&card->hw, 0xF020, &data[60], 2);
			data[46] = 2;
			data[47] = 0;
			data[48] = 0;
			mbox->cmd.length = 2;
			break;
		case 0x29:
			init_ppp_priv_struct(ppp_priv_area);
			init_global_statistics(card);
			mbox->cmd.length = 0;
			break;
		case 0x30:
			do_gettimeofday(&tv);
			ppp_priv_area->router_up_time = tv.tv_sec -
			    ppp_priv_area->router_start_time;
			*(unsigned long *) &data[60] =
			    ppp_priv_area->router_up_time;
			mbox->cmd.length = 4;
			break;
			/* FT1 MONITOR STATUS */
		case 0x80:
			/* Enable FT1 MONITOR STATUS */
			if (data[60] == 1) {
				if (rCount++ != 0) {
					data[48] = 0;
					mbox->cmd.length = 1;
					break;
				}
			}
			/* Disable FT1 MONITOR STATUS */
			if (data[60] == 0) {
				if (--rCount != 0) {
					data[48] = 0;
					mbox->cmd.length = 1;
					break;
				}
			}
		default:
			/* it's a board command */
			memcpy(&mbox->cmd, &sendpacket[45], sizeof(ppp_cmd_t));
			if (mbox->cmd.length) {
				memcpy(&mbox->data, &sendpacket[60],
				       mbox->cmd.length);
			}
			/* run the command on the board */
			err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
			if (err != CMD_OK) {
				ppp_error(card, err, mbox);
				++ppp_priv_area->
				    UDP_PTPIPE_mgmt_adptr_cmnd_timeout;
				break;
			}
			++ppp_priv_area->UDP_PTPIPE_mgmt_adptr_cmnd_OK;
			/* copy the result back to our buffer */
			memcpy(data, sendpacket, skb->len);
			memcpy(&data[45], &mbox->cmd, sizeof(ppp_cmd_t));
			if (mbox->cmd.length) {
				memcpy(&data[60], &mbox->data, mbox->cmd.length);
			}
		}		/* end of switch */
	}			/* end of else */
	/* Fill UDP TTL */
	data[8] = card->wandev.ttl;
	len = reply_udp(data, mbox->cmd.length);
	if (udp_pkt_src == UDP_PKT_FRM_NETWORK) {
		++ppp_priv_area->UDP_PTPIPE_mgmt_passed_to_adptr;
		ppp_send(card, data, len, skb->protocol);
	} else {
		/* Pass it up the stack
		   Allocate socket buffer */
		if ((new_skb = dev_alloc_skb(len)) != NULL) {
			/* copy data into new_skb */
			buf = skb_put(new_skb, len);
			memcpy(buf, data, len);
			++ppp_priv_area->UDP_PTPIPE_mgmt_passed_to_stack;
			/* Decapsulate packet and pass it up the protocol 
			   stack */
			new_skb->protocol = htons(ETH_P_IP);
			new_skb->dev = dev;
			new_skb->mac.raw = new_skb->data;
			netif_rx(new_skb);
		} else {
			++ppp_priv_area->UDP_PTPIPE_mgmt_no_socket;
			printk(KERN_INFO "no socket buffers available!\n");
		}
	}
	kfree(data);
	return 0;
}

/*=============================================================================
 * Initial the ppp_private_area structure.
 */

static void init_ppp_priv_struct(ppp_private_area_t * ppp_priv_area)
{
	ppp_priv_area->if_send_entry = 0;
	ppp_priv_area->if_send_skb_null = 0;
	ppp_priv_area->if_send_broadcast = 0;
	ppp_priv_area->if_send_multicast = 0;
	ppp_priv_area->if_send_critical_ISR = 0;
	ppp_priv_area->if_send_critical_non_ISR = 0;
	ppp_priv_area->if_send_busy = 0;
	ppp_priv_area->if_send_busy_timeout = 0;
	ppp_priv_area->if_send_DRVSTATS_request = 0;
	ppp_priv_area->if_send_PTPIPE_request = 0;
	ppp_priv_area->if_send_wan_disconnected = 0;
	ppp_priv_area->if_send_adptr_bfrs_full = 0;
	ppp_priv_area->if_send_bfr_passed_to_adptr = 0;
	ppp_priv_area->rx_intr_no_socket = 0;
	ppp_priv_area->rx_intr_DRVSTATS_request = 0;
	ppp_priv_area->rx_intr_PTPIPE_request = 0;
	ppp_priv_area->rx_intr_bfr_not_passed_to_stack = 0;
	ppp_priv_area->rx_intr_bfr_passed_to_stack = 0;
	ppp_priv_area->UDP_PTPIPE_mgmt_kmalloc_err = 0;
	ppp_priv_area->UDP_PTPIPE_mgmt_adptr_type_err = 0;
	ppp_priv_area->UDP_PTPIPE_mgmt_direction_err = 0;
	ppp_priv_area->UDP_PTPIPE_mgmt_adptr_cmnd_timeout = 0;
	ppp_priv_area->UDP_PTPIPE_mgmt_adptr_cmnd_OK = 0;
	ppp_priv_area->UDP_PTPIPE_mgmt_passed_to_adptr = 0;
	ppp_priv_area->UDP_PTPIPE_mgmt_passed_to_stack = 0;
	ppp_priv_area->UDP_PTPIPE_mgmt_no_socket = 0;
	ppp_priv_area->UDP_DRVSTATS_mgmt_kmalloc_err = 0;
	ppp_priv_area->UDP_DRVSTATS_mgmt_adptr_type_err = 0;
	ppp_priv_area->UDP_DRVSTATS_mgmt_direction_err = 0;
	ppp_priv_area->UDP_DRVSTATS_mgmt_adptr_cmnd_timeout = 0;
	ppp_priv_area->UDP_DRVSTATS_mgmt_adptr_cmnd_OK = 0;
	ppp_priv_area->UDP_DRVSTATS_mgmt_passed_to_adptr = 0;
	ppp_priv_area->UDP_DRVSTATS_mgmt_passed_to_stack = 0;
	ppp_priv_area->UDP_DRVSTATS_mgmt_no_socket = 0;
}

/*============================================================================
 * Initialize Global Statistics
 */

static void init_global_statistics(sdla_t * card)
{
	card->statistics.isr_entry = 0;
	card->statistics.isr_already_critical = 0;
	card->statistics.isr_tx = 0;
	card->statistics.isr_rx = 0;
	card->statistics.isr_intr_test = 0;
	card->statistics.isr_spurious = 0;
	card->statistics.isr_enable_tx_int = 0;
	card->statistics.rx_intr_corrupt_rx_bfr = 0;
	card->statistics.rx_intr_dev_not_started = 0;
	card->statistics.tx_intr_dev_not_started = 0;
	card->statistics.poll_entry = 0;
	card->statistics.poll_already_critical = 0;
	card->statistics.poll_processed = 0;
	card->statistics.poll_tbusy_bad_status = 0;
}

/*============================================================================
 * Initialize Receive and Transmit Buffers.
 */

static void init_ppp_tx_rx_buff(sdla_t * card)
{
	if (card->hw.fwid == SFID_PPP502) {
		ppp502_buf_info_t *info =
		(void *) (card->hw.dpmbase + PPP502_BUF_OFFS);
		card->u.p.txbuf_base =
		    (void *) (card->hw.dpmbase + info->txb_offs);
		card->u.p.txbuf_last = (ppp_buf_ctl_t *) card->u.p.txbuf_base +
		    (info->txb_num - 1);
		card->u.p.rxbuf_base =
		    (void *) (card->hw.dpmbase + info->rxb_offs);
		card->u.p.rxbuf_last = (ppp_buf_ctl_t *) card->u.p.rxbuf_base +
		    (info->rxb_num - 1);
	} else {
		ppp508_buf_info_t *info =
		(void *) (card->hw.dpmbase + PPP508_BUF_OFFS);
		card->u.p.txbuf_base = (void *) (card->hw.dpmbase +
				       (info->txb_ptr - PPP508_MB_VECT));
		card->u.p.txbuf_last = (ppp_buf_ctl_t *) card->u.p.txbuf_base +
		    (info->txb_num - 1);
		card->u.p.rxbuf_base = (void *) (card->hw.dpmbase +
				       (info->rxb_ptr - PPP508_MB_VECT));
		card->u.p.rxbuf_last = (ppp_buf_ctl_t *) card->u.p.rxbuf_base +
		    (info->rxb_num - 1);
		card->u.p.rx_base = info->rxb_base;
		card->u.p.rx_top = info->rxb_end;
	}
	card->u.p.txbuf = card->u.p.txbuf_base;
	card->rxmb = card->u.p.rxbuf_base;
}

/*=============================================================================
 * Perform the Interrupt Test by running the READ_CODE_VERSION command MAX_INTR
 * _TEST_COUNTER times.
 */

static int intr_test(sdla_t * card)
{
	ppp_mbox_t *mb = card->mbox;
	int err, i;
	/* The critical flag is unset because during initialization (if_open) 
	 * we want the interrupts to be enabled so that when the wpp_isr is
	 * called it does not exit due to critical flag set.
	 */
	card->wandev.critical = 0;
	err = ppp_set_intr_mode(card, 0x08);
	if (err == CMD_OK) {
		for (i = 0; i < MAX_INTR_TEST_COUNTER; i++) {
			/* Run command READ_CODE_VERSION */
			memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
			mb->cmd.length = 0;
			mb->cmd.command = 0x10;
			err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
			if (err != CMD_OK)
				ppp_error(card, err, mb);
		}
	} else
		return err;
	err = ppp_set_intr_mode(card, 0);
	if (err != CMD_OK)
		return err;
	card->wandev.critical = 1;
	return 0;
}

/*==============================================================================
 * Determine what type of UDP call it is. DRVSTATS or PTPIPEAB ?
 */

static int udp_pkt_type(struct sk_buff *skb, sdla_t * card)
{
	unsigned char *sendpacket;
	unsigned char buf2[5];
	sendpacket = skb->data;
	memcpy(&buf2, &card->wandev.udp_port, 2);
	if (sendpacket[0] == 0x45 &&	/* IP packet */
	    sendpacket[9] == 0x11 &&	/* UDP packet */
	    sendpacket[22] == buf2[1] &&	/* UDP Port */
	    sendpacket[23] == buf2[0] &&
	    sendpacket[36] == 0x01) {
		if (sendpacket[28] == 0x50 &&	/* PTPIPEAB: Signature */
		    sendpacket[29] == 0x54 &&
		    sendpacket[30] == 0x50 &&
		    sendpacket[31] == 0x49 &&
		    sendpacket[32] == 0x50 &&
		    sendpacket[33] == 0x45 &&
		    sendpacket[34] == 0x41 &&
		    sendpacket[35] == 0x42) {
			return UDP_PTPIPE_TYPE;
		} else if (sendpacket[28] == 0x44 &&	/* DRVSTATS: Signature */
			   sendpacket[29] == 0x52 &&
			   sendpacket[30] == 0x56 &&
			   sendpacket[31] == 0x53 &&
			   sendpacket[32] == 0x54 &&
			   sendpacket[33] == 0x41 &&
			   sendpacket[34] == 0x54 &&
			   sendpacket[35] == 0x53) {
			return UDP_DRVSTATS_TYPE;
		} else
			return UDP_INVALID_TYPE;
	} else
		return UDP_INVALID_TYPE;
}

/****** End *****************************************************************/
