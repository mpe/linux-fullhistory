/*****************************************************************************
* sdla_ppp.c	WANPIPE(tm) Multiprotocol WAN Link Driver. PPP module.
*
* Author:	Gene Kozin	<genek@compuserve.com>
*
* Copyright:	(c) 1995-1997 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Jun 29, 1997  Alan Cox	o Dumped the idiot UDP management system.
*
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

#if	!defined(__KERNEL__) || !defined(MODULE)
#error	This code MUST be compiled as a kernel module!
#endif

#include <linux/kernel.h>	/* printk(), and other useful stuff */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/string.h>	/* inline memset(), etc. */
#include <linux/malloc.h>	/* kmalloc(), kfree() */
#include <linux/wanrouter.h>	/* WAN router definitions */
#include <linux/wanpipe.h>	/* WANPIPE common user API definitions */
#include <linux/if_arp.h>	/* ARPHRD_* defines */
#include <linux/init.h>		/* __initfunc et al. */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <asm/uaccess.h>

#define	_GNUC_
#include <linux/sdla_ppp.h>		/* PPP firmware API definitions */

/****** Defines & Macros ****************************************************/

#ifdef	_DEBUG_
#define	STATIC
#else
#define	STATIC		static
#endif

#define	CMD_OK		0		/* normal firmware return code */
#define	CMD_TIMEOUT	0xFF		/* firmware command timed out */

#define	PPP_DFLT_MTU	1500		/* default MTU */
#define	PPP_MAX_MTU	4000		/* maximum MTU */
#define PPP_HDR_LEN	1

#define	CONNECT_TIMEOUT	(90*HZ)		/* link connection timeout */
#define	HOLD_DOWN_TIME	(30*HZ)		/* link hold down time */

/****** Function Prototypes *************************************************/

/* WAN link driver entry points. These are called by the WAN router module. */
static int update (wan_device_t* wandev);
static int new_if (wan_device_t* wandev, struct device* dev,
	wanif_conf_t* conf);
static int del_if (wan_device_t* wandev, struct device* dev);

/* WANPIPE-specific entry points */
static int wpp_exec (struct sdla* card, void* u_cmd, void* u_data);

/* Network device interface */
static int if_init   (struct device* dev);
static int if_open   (struct device* dev);
static int if_close  (struct device* dev);
static int if_header (struct sk_buff* skb, struct device* dev,
	unsigned short type, void* daddr, void* saddr, unsigned len);
static int if_rebuild_hdr (struct sk_buff* skb);
static int if_send (struct sk_buff* skb, struct device* dev);
static struct enet_statistics* if_stats (struct device* dev);

/* PPP firmware interface functions */
static int ppp_read_version (sdla_t* card, char* str);
static int ppp_configure (sdla_t* card, void* data);
static int ppp_set_intr_mode (sdla_t* card, unsigned mode);
static int ppp_comm_enable (sdla_t* card);
static int ppp_comm_disable (sdla_t* card);
static int ppp_get_err_stats (sdla_t* card);
static int ppp_send (sdla_t* card, void* data, unsigned len, unsigned proto);
static int ppp_error (sdla_t *card, int err, ppp_mbox_t* mb);

/* Interrupt handlers */
STATIC void wpp_isr (sdla_t* card);
static void rx_intr (sdla_t* card);
static void tx_intr (sdla_t* card);

/* Background polling routines */
static void wpp_poll (sdla_t* card);
static void poll_active (sdla_t* card);
static void poll_connecting (sdla_t* card);
static void poll_disconnected (sdla_t* card);

/* Miscellaneous functions */
static int config502 (sdla_t* card);
static int config508 (sdla_t* card);
static void show_disc_cause (sdla_t* card, unsigned cause);
static unsigned char bps_to_speed_code (unsigned long bps);

static char TracingEnabled;
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
__initfunc(int wpp_init (sdla_t* card, wandev_conf_t* conf))
{
	union
	{
		char str[80];
	} u;

	/* Verify configuration ID */
	if (conf->config_id != WANCONFIG_PPP)
	{
		printk(KERN_INFO "%s: invalid configuration ID %u!\n",
			card->devname, conf->config_id)
		;
		return -EINVAL;
	}

	/* Initialize protocol-specific fields */
	switch (card->hw.fwid)
	{
	case SFID_PPP502:
		card->mbox  = (void*)(card->hw.dpmbase + PPP502_MB_OFFS);
		card->flags = (void*)(card->hw.dpmbase + PPP502_FLG_OFFS);
		break;

	case SFID_PPP508:
		card->mbox  = (void*)(card->hw.dpmbase + PPP508_MB_OFFS);
		card->flags = (void*)(card->hw.dpmbase + PPP508_FLG_OFFS);
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
		return -EIO
	;
	printk(KERN_INFO "%s: running PPP firmware v%s\n",
		card->devname, u.str)
	;

	/* Adjust configuration and set defaults */
	card->wandev.mtu = (conf->mtu) ?
		min(conf->mtu, PPP_MAX_MTU) : PPP_DFLT_MTU
	;
	card->wandev.bps	= conf->bps;
	card->wandev.interface	= conf->interface;
	card->wandev.clocking	= conf->clocking;
	card->wandev.station	= conf->station;
	card->isr		= &wpp_isr;
	card->poll		= &wpp_poll;
	card->exec		= &wpp_exec;
	card->wandev.update	= &update;
	card->wandev.new_if	= &new_if;
	card->wandev.del_if	= &del_if;
	card->wandev.state	= WAN_DISCONNECTED;
        card->wandev.udp_port   = conf->udp_port;
        TracingEnabled          = '0';
	return 0;
}

/******* WAN Device Driver Entry Points *************************************/

/*============================================================================
 * Update device status & statistics.
 */
static int update (wan_device_t* wandev)
{
	sdla_t* card;

	/* sanity checks */
	if ((wandev == NULL) || (wandev->private == NULL))
		return -EFAULT
	;
	if (wandev->state == WAN_UNCONFIGURED)
		return -ENODEV
	;
	if (test_and_set_bit(0, (void*)&wandev->critical))
		return -EAGAIN
	;
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
static int new_if (wan_device_t* wandev, struct device* dev, wanif_conf_t* conf)
{
	sdla_t* card = wandev->private;

	if (wandev->ndev)
		return -EEXIST
	;
	if ((conf->name[0] == '\0') || (strlen(conf->name) > WAN_IFNAME_SZ))
	{
		printk(KERN_INFO "%s: invalid interface name!\n",
			card->devname)
		;
		return -EINVAL;
	}

	/* initialize data */
	strcpy(card->u.p.if_name, conf->name);

	/* prepare network device data space for registration */
	dev->name = card->u.p.if_name;
	dev->init = &if_init;
	dev->priv = card;
	return 0;
}

/*============================================================================
 * Delete logical channel.
 */
static int del_if (wan_device_t* wandev, struct device* dev)
{
	return 0;
}

/****** WANPIPE-specific entry points ***************************************/

/*============================================================================
 * Execute adapter interface command.
 */
static int wpp_exec (struct sdla* card, void* u_cmd, void* u_data)
{
	ppp_mbox_t* mbox = card->mbox;
	int len;

	if(copy_from_user((void*)&mbox->cmd, u_cmd, sizeof(ppp_cmd_t)))
		return -EFAULT;
	len = mbox->cmd.length;
	if (len)
	{
		if(copy_from_user((void*)&mbox->data, u_data, len))
			return -EFAULT;
	}

	/* execute command */
	if (!sdla_exec(mbox))
		return -EIO;

	/* return result */
	if(copy_to_user(u_cmd, (void*)&mbox->cmd, sizeof(ppp_cmd_t)))
		return -EFAULT;
	len = mbox->cmd.length;
	if (len && u_data && copy_to_user(u_data, (void*)&mbox->data, len))
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
static int if_init (struct device* dev)
{
	sdla_t* card = dev->priv;
	wan_device_t* wandev = &card->wandev;
	int i;

	/* Initialize device driver entry points */
	dev->open		= &if_open;
	dev->stop		= &if_close;
	dev->hard_header	= &if_header;
	dev->rebuild_header	= &if_rebuild_hdr;
	dev->hard_start_xmit	= &if_send;
	dev->get_stats		= &if_stats;

	/* Initialize media-specific parameters */
	dev->family		= AF_INET;	/* address family */
	dev->type		= ARPHRD_PPP;	/* ARP h/w type */
	dev->mtu		= wandev->mtu;
	dev->hard_header_len	= PPP_HDR_LEN;	/* media header length */

	/* Initialize hardware parameters (just for reference) */
	dev->irq		= wandev->irq;
	dev->dma		= wandev->dma;
	dev->base_addr		= wandev->ioport;
	dev->mem_start		= wandev->maddr;
	dev->mem_end		= wandev->maddr + wandev->msize - 1;

        /* Set transmit buffer queue length */
        dev->tx_queue_len = 30;
   
	/* Initialize socket buffers */
	for (i = 0; i < DEV_NUMBUFFS; ++i)
		skb_queue_head_init(&dev->buffs[i])
	;
	return 0;
}

/*============================================================================
 * Open network interface.
 * o enable communications and interrupts.
 * o prevent module from unloading by incrementing use count
 *
 * Return 0 if O.k. or errno.
 */
static int if_open (struct device* dev)
{
	sdla_t* card = dev->priv;
	int err = 0;

	if (dev->start)
		return -EBUSY		/* only one open is allowed */
	;
	if (test_and_set_bit(0, (void*)&card->wandev.critical))
		return -EAGAIN;
	;
	if ((card->hw.fwid == SFID_PPP502) ? config502(card) : config508(card))
	{
		err = -EIO;
		goto split;
	}

	/* Initialize Rx/Tx buffer control fields */
	if (card->hw.fwid == SFID_PPP502)
	{
		ppp502_buf_info_t* info =
			(void*)(card->hw.dpmbase + PPP502_BUF_OFFS)
		;

		card->u.p.txbuf_base = (void*)(card->hw.dpmbase +
			info->txb_offs)
		;
		card->u.p.txbuf_last = (ppp_buf_ctl_t*)card->u.p.txbuf_base +
			(info->txb_num - 1)
		;
		card->u.p.rxbuf_base = (void*)(card->hw.dpmbase +
			info->rxb_offs)
		;
		card->u.p.rxbuf_last = (ppp_buf_ctl_t*)card->u.p.rxbuf_base +
			(info->rxb_num - 1)
		;
	}
	else
	{
		ppp508_buf_info_t* info =
			(void*)(card->hw.dpmbase + PPP508_BUF_OFFS)
		;

		card->u.p.txbuf_base = (void*)(card->hw.dpmbase +
			(info->txb_ptr - PPP508_MB_VECT))
		;
		card->u.p.txbuf_last = (ppp_buf_ctl_t*)card->u.p.txbuf_base +
			(info->txb_num - 1)
		;
		card->u.p.rxbuf_base = (void*)(card->hw.dpmbase +
			(info->rxb_ptr - PPP508_MB_VECT))
		;
		card->u.p.rxbuf_last = (ppp_buf_ctl_t*)card->u.p.rxbuf_base +
			(info->rxb_num - 1)
		;
		card->u.p.rx_base = info->rxb_base;
		card->u.p.rx_top  = info->rxb_end;
	}
	card->u.p.txbuf = card->u.p.txbuf_base;
	card->rxmb = card->u.p.rxbuf_base;

	if (ppp_set_intr_mode(card, 0x03) || ppp_comm_enable(card))
	{
		err = -EIO;
		goto split;
	}
	wanpipe_set_state(card, WAN_CONNECTING);
	wanpipe_open(card);
	dev->mtu = min(dev->mtu, card->wandev.mtu);
	dev->interrupt = 0;
	dev->tbusy = 0;
	dev->start = 1;

split:
	card->wandev.critical = 0;
	return err;
}

/*============================================================================
 * Close network interface.
 * o if this is the last open, then disable communications and interrupts.
 * o reset flags.
 */
static int if_close (struct device* dev)
{
	sdla_t* card = dev->priv;

	if (test_and_set_bit(0, (void*)&card->wandev.critical))
		return -EAGAIN;
	;
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
static int if_header (struct sk_buff* skb, struct device* dev,
	unsigned short type, void* daddr, void* saddr, unsigned len)
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
static int if_rebuild_hdr (struct sk_buff* skb)
{
	sdla_t* card = skb->dev->priv;

	printk(KERN_INFO "%s: rebuild_header() called for interface %s!\n",
		card->devname, skb->dev->name)
	;
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
static int if_send (struct sk_buff* skb, struct device* dev)
{
	sdla_t* card = dev->priv;
	int retry = 0;

	if (test_and_set_bit(0, (void*)&card->wandev.critical))
	{
#ifdef _DEBUG_
		printk(KERN_INFO "%s: if_send() hit critical section!\n",
			card->devname)
		;
#endif
		return 1;
	}

	if (test_and_set_bit(0, (void*)&dev->tbusy))
	{
#ifdef _DEBUG_
		printk(KERN_INFO "%s: Tx collision on interface %s!\n",
			card->devname, dev->name)
		;
#endif
		++card->wandev.stats.collisions;
		retry = 1;
	} else if (card->wandev.state != WAN_CONNECTED)
		++card->wandev.stats.tx_dropped
	;
	else if (!skb->protocol)
		++card->wandev.stats.tx_errors
	;
	else if (ppp_send(card, skb->data, skb->len, skb->protocol))
	{
		ppp_flags_t* flags = card->flags;

		flags->imask |= 0x02;	/* unmask Tx interrupts */
		retry = 1;
	}
	else ++card->wandev.stats.tx_packets;

	if (!retry)
	{
		dev_kfree_skb(skb, FREE_WRITE);
		dev->tbusy = 0;
	}
	card->wandev.critical = 0;
	return retry;
}

/*============================================================================
 * Get ethernet-style interface statistics.
 * Return a pointer to struct enet_statistics.
 */

static struct enet_statistics* if_stats (struct device* dev)
{
	sdla_t* card = dev->priv;

	return &card->wandev.stats;
}

/****** PPP Firmware Interface Functions ************************************/

/*============================================================================
 * Read firmware code version.
 *	Put code version as ASCII string in str. 
 */
static int ppp_read_version (sdla_t* card, char* str)
{
	ppp_mbox_t* mb = card->mbox;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_READ_CODE_VERSION;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK) ppp_error(card, err, mb);
	else if (str)
	{
		int len = mb->cmd.length;

		memcpy(str, mb->data, len);
		str[len] = '\0';
	}
	return err;
}

/*============================================================================
 * Configure PPP firmware.
 */
static int ppp_configure (sdla_t* card, void* data)
{
	ppp_mbox_t* mb = card->mbox;
	int data_len = (card->hw.fwid == SFID_PPP502) ?
		sizeof(ppp502_conf_t) : sizeof(ppp508_conf_t)
	;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	memcpy(mb->data, data, data_len);
	mb->cmd.length  = data_len;
	mb->cmd.command = PPP_SET_CONFIG;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK) ppp_error(card, err, mb);
	return err;
}

/*============================================================================
 * Set interrupt mode.
 */
static int ppp_set_intr_mode (sdla_t* card, unsigned mode)
{
	ppp_mbox_t* mb = card->mbox;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->data[0] = mode;
	switch (card->hw.fwid)
	{
	case SFID_PPP502:
		mb->cmd.length  = 1;
		break;

	case SFID_PPP508:
	default:
		mb->data[1] = card->hw.irq;
		mb->cmd.length = 2;
	}
	mb->cmd.command = PPP_SET_INTR_FLAGS;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK) ppp_error(card, err, mb);
	return err;
}

/*============================================================================
 * Enable communications.
 */
static int ppp_comm_enable (sdla_t* card)
{
	ppp_mbox_t* mb = card->mbox;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_COMM_ENABLE;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK) ppp_error(card, err, mb);
	return err;
}

/*============================================================================
 * Disable communications.
 */
static int ppp_comm_disable (sdla_t* card)
{
	ppp_mbox_t* mb = card->mbox;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_COMM_DISABLE;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK) ppp_error(card, err, mb);
	return err;
}

/*============================================================================
 * Get communications error statistics.
 */
static int ppp_get_err_stats (sdla_t* card)
{
	ppp_mbox_t* mb = card->mbox;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_READ_ERROR_STATS;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err == CMD_OK)
	{
		ppp_err_stats_t* stats = (void*)mb->data;

		card->wandev.stats.rx_over_errors    = stats->rx_overrun;
		card->wandev.stats.rx_crc_errors     = stats->rx_bad_crc;
		card->wandev.stats.rx_missed_errors  = stats->rx_abort;
		card->wandev.stats.rx_length_errors  = stats->rx_lost;
		card->wandev.stats.tx_aborted_errors = stats->tx_abort;
	}
	else ppp_error(card, err, mb);
	return err;
}

/*============================================================================
 * Send packet.
 *	Return:	0 - o.k.
 *		1 - no transmit buffers available
 */
static int ppp_send (sdla_t* card, void* data, unsigned len, unsigned proto)
{
	ppp_buf_ctl_t* txbuf = card->u.p.txbuf;
	unsigned long addr, cpu_flags;

	if (txbuf->flag)
		return 1
	;
	if (card->hw.fwid == SFID_PPP502)
		addr = (txbuf->buf.o_p[1] << 8) + txbuf->buf.o_p[0]
	; 
	else addr = txbuf->buf.ptr;

	save_flags(cpu_flags);
	cli();
	sdla_poke(&card->hw, addr, data, len);
	restore_flags(cpu_flags);
	txbuf->length = len;		/* frame length */
	if (proto == ETH_P_IPX)
		txbuf->proto = 0x01	/* protocol ID */
	;
	txbuf->flag = 1;		/* start transmission */

	/* Update transmit buffer control fields */
	card->u.p.txbuf = ++txbuf;
	if ((void*)txbuf > card->u.p.txbuf_last)
		card->u.p.txbuf = card->u.p.txbuf_base
	;
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
static int ppp_error (sdla_t *card, int err, ppp_mbox_t* mb)
{
	unsigned cmd = mb->cmd.command;

	switch (err)
	{
	case CMD_TIMEOUT:
		printk(KERN_ERR "%s: command 0x%02X timed out!\n",
			card->devname, cmd)
		;
		break;

	default:
		printk(KERN_INFO "%s: command 0x%02X returned 0x%02X!\n",
			card->devname, cmd, err)
		;
	}
	return 0;
}

/****** Interrupt Handlers **************************************************/

/*============================================================================
 * PPP interrupt service routine.
 */
STATIC void wpp_isr (sdla_t* card)
{
	ppp_flags_t* flags = card->flags;

	switch (flags->iflag)
	{
	case 0x01:	/* receive interrupt */
		rx_intr(card);
		break;

	case 0x02:	/* transmit interrupt */
		flags->imask &= ~0x02;
		tx_intr(card);
		break;

	default:	/* unexpected interrupt */
		printk(KERN_INFO "%s: spurious interrupt 0x%02X!\n",
			card->devname, flags->iflag)
		;
	}
	flags->iflag = 0;
}

/*============================================================================
 * Receive interrupt handler.
 */
static void rx_intr (sdla_t* card)
{
	ppp_buf_ctl_t* rxbuf = card->rxmb;
	struct device* dev = card->wandev.dev;
	struct sk_buff* skb;
	unsigned len;
	void* buf;
   
	if (rxbuf->flag != 0x01)
	{
		printk(KERN_INFO "%s: corrupted Rx buffer @ 0x%X!\n",
			card->devname, (unsigned)rxbuf)
		;
		return;
	}

	if (!dev || !dev->start)
		goto rx_done
	;
	len  = rxbuf->length;

	/* Allocate socket buffer */
	skb = dev_alloc_skb(len);
	if (skb == NULL)
	{
		printk(KERN_INFO "%s: no socket buffers available!\n",
			card->devname)
		;
		++card->wandev.stats.rx_dropped;
		goto rx_done;
	}

	/* Copy data to the socket buffer */
	if (card->hw.fwid == SFID_PPP502)
	{
		unsigned addr = (rxbuf->buf.o_p[1] << 8) + rxbuf->buf.o_p[0]; 

		buf = skb_put(skb, len);
		sdla_peek(&card->hw, addr, buf, len);
	}
	else
	{
		unsigned addr = rxbuf->buf.ptr;

		if ((addr + len) > card->u.p.rx_top + 1)
		{
			unsigned tmp = card->u.p.rx_top - addr + 1;

			buf = skb_put(skb, tmp);
			sdla_peek(&card->hw, addr, buf, tmp);
			addr = card->u.p.rx_base;
			len -= tmp;
		}
		buf = skb_put(skb, len);
		sdla_peek(&card->hw, addr, buf, len);
	}

	/* Decapsulate packet */
        switch (rxbuf->proto)
	{
	case 0x00:
		skb->protocol = htons(ETH_P_IP);
		break;

	case 0x01:
		skb->protocol = htons(ETH_P_IPX);
		break;
	}

	/* Pass it up the protocol stack */
	skb->dev = dev;
	netif_rx(skb);
	++card->wandev.stats.rx_packets;
rx_done:
	/* Release buffer element and calculate a pointer to the next one */
	rxbuf->flag = (card->hw.fwid == SFID_PPP502) ? 0xFF : 0x00;
	card->rxmb = ++rxbuf;
	if ((void*)rxbuf > card->u.p.rxbuf_last)
		card->rxmb = card->u.p.rxbuf_base
	;
}

/*============================================================================
 * Transmit interrupt handler.
 */
static void tx_intr (sdla_t* card)
{
	struct device* dev = card->wandev.dev;

	if (!dev || !dev->start)
		return
	;
	dev->tbusy = 0;
	dev_tint(dev);
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
static void wpp_poll (sdla_t* card)
{
	switch(card->wandev.state)
	{
	case WAN_CONNECTED:
		poll_active(card);
		break;

	case WAN_CONNECTING:
		poll_connecting(card);
		break;

	case WAN_DISCONNECTED:
		poll_disconnected(card);
		break;
	}
}

/*============================================================================
 * Monitor active link phase.
 */
static void poll_active (sdla_t* card)
{
	ppp_flags_t* flags = card->flags;

	if (flags->disc_cause & 0x03)
	{
		wanpipe_set_state(card, WAN_DISCONNECTED);
		show_disc_cause(card, flags->disc_cause);
	}
}

/*============================================================================
 * Monitor link establishment phase.
 * o if connection timed out, disconnect the link.
 */
static void poll_connecting (sdla_t* card)
{
	ppp_flags_t* flags = card->flags;

	if (flags->lcp_state == 0x09)
	{
		wanpipe_set_state(card, WAN_CONNECTED);
	}
	else if (flags->disc_cause & 0x03)
	{
		wanpipe_set_state(card, WAN_DISCONNECTED);
		show_disc_cause(card, flags->disc_cause);
	}
}

/*============================================================================
 * Monitor physical link disconnected phase.
 *  o if interface is up and the hold-down timeout has expired, then retry
 *    connection.
 */
static void poll_disconnected (sdla_t* card)
{
	struct device* dev = card->wandev.dev;

	if (dev && dev->start &&
	    ((jiffies - card->state_tick) > HOLD_DOWN_TIME))
	{
		wanpipe_set_state(card, WAN_CONNECTING);
		ppp_comm_enable(card);
	}
}

/****** Miscellaneous Functions *********************************************/

/*============================================================================
 * Configure S502 adapter.
 */
static int config502 (sdla_t* card)
{
	ppp502_conf_t cfg;

	/* Prepare PPP configuration structure */
	memset(&cfg, 0, sizeof(ppp502_conf_t));

	if (card->wandev.clocking)
		cfg.line_speed = bps_to_speed_code(card->wandev.bps)
	;
	cfg.txbuf_num		= 4;
	cfg.mtu_local		= card->wandev.mtu;
	cfg.mtu_remote		= card->wandev.mtu;
	cfg.restart_tmr		= 30;
	cfg.auth_rsrt_tmr	= 30;
	cfg.auth_wait_tmr	= 300;
	cfg.mdm_fail_tmr	= 5;
	cfg.dtr_drop_tmr	= 1;
	cfg.connect_tmout	= 0;	/* changed it from 900 */
	cfg.conf_retry		= 10;
	cfg.term_retry		= 2;
	cfg.fail_retry		= 5;
	cfg.auth_retry		= 10;
	cfg.ip_options		= 0x80;
	cfg.ipx_options		= 0xA0;
        cfg.conf_flags         |= 0x0E;
/*
	cfg.ip_local		= dev->pa_addr;
	cfg.ip_remote		= dev->pa_dstaddr;
*/
	return ppp_configure(card, &cfg);
}

/*============================================================================
 * Configure S508 adapter.
 */
static int config508 (sdla_t* card)
{
	ppp508_conf_t cfg;

	/* Prepare PPP configuration structure */
	memset(&cfg, 0, sizeof(ppp508_conf_t));

	if (card->wandev.clocking)
		cfg.line_speed = card->wandev.bps
	;
	if (card->wandev.interface == WANOPT_RS232)
		cfg.conf_flags |= 0x0020;
	;
        cfg.conf_flags 	|= 0x300; /*send Configure-Request packets forever*/
	cfg.txbuf_percent	= 60;	/* % of Tx bufs */
	cfg.mtu_local		= card->wandev.mtu;
	cfg.mtu_remote		= card->wandev.mtu;
	cfg.restart_tmr		= 30;
	cfg.auth_rsrt_tmr	= 30;
	cfg.auth_wait_tmr	= 300;
	cfg.mdm_fail_tmr	= 5;
	cfg.dtr_drop_tmr	= 1;
	cfg.connect_tmout	= 0; 	/* changed it from 900 */
	cfg.conf_retry		= 10;
	cfg.term_retry		= 2;
	cfg.fail_retry		= 5;
	cfg.auth_retry		= 10;
	cfg.ip_options		= 0x80;
	cfg.ipx_options		= 0xA0;
/*
	cfg.ip_local		= dev->pa_addr;
	cfg.ip_remote		= dev->pa_dstaddr;
*/
	return ppp_configure(card, &cfg);
}

/*============================================================================
 * Show disconnection cause.
 */
static void show_disc_cause (sdla_t* card, unsigned cause)
{
	if (cause & 0x0002) printk(KERN_INFO
		"%s: link terminated by peer\n", card->devname)
	;
	else if (cause & 0x0004) printk(KERN_INFO
		"%s: link terminated by user\n", card->devname)
	;
	else if (cause & 0x0008) printk(KERN_INFO
		"%s: authentication failed\n", card->devname)
	;
	else if (cause & 0x0010) printk(KERN_INFO
		"%s: authentication protocol negotiation failed\n",
		card->devname)
	;
	else if (cause & 0x0020) printk(KERN_INFO
		"%s: peer's request for authentication rejected\n",
		card->devname)
	;
	else if (cause & 0x0040) printk(KERN_INFO
		"%s: MRU option rejected by peer\n", card->devname)
	;
	else if (cause & 0x0080) printk(KERN_INFO
		"%s: peer's MRU was too small\n", card->devname)
	;
	else if (cause & 0x0100) printk(KERN_INFO
		"%s: failed to negotiate peer's LCP options\n",
		card->devname)
	;
	else if (cause & 0x0200) printk(KERN_INFO
		"%s: failed to negotiate peer's IPCP options\n",
		card->devname)
	;
	else if (cause & 0x0400) printk(KERN_INFO
		"%s: failed to negotiate peer's IPXCP options\n",
		card->devname)
	;
}

/*============================================================================
 * Convert line speed in bps to a number used by S502 code.
 */
static unsigned char bps_to_speed_code (unsigned long bps)
{
	unsigned char	number;

	if (bps <= 1200)        number = 0x01 ;
	else if (bps <= 2400)   number = 0x02;
	else if (bps <= 4800)   number = 0x03;
	else if (bps <= 9600)   number = 0x04;
	else if (bps <= 19200)  number = 0x05;
	else if (bps <= 38400)  number = 0x06;
	else if (bps <= 45000)  number = 0x07;
	else if (bps <= 56000)  number = 0x08;
	else if (bps <= 64000)  number = 0x09;
	else if (bps <= 74000)  number = 0x0A;
	else if (bps <= 112000) number = 0x0B;
	else if (bps <= 128000) number = 0x0C;
	else number = 0x0D;

	return number;
}

/****** End *****************************************************************/
