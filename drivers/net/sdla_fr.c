/*****************************************************************************
* sdla_fr.c	WANPIPE(tm) Multiprotocol WAN Link Driver. Frame relay module.
*
* Author(s):	Gene Kozin	
*		Jaspreet Singh		<jaspreet@sangoma.com>
*
* Copyright:	(c) 1995-1997 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Nov 26, 1997	Jaspreet Singh	o Improved load sharing with multiple boards
*				o Added Cli() to protect enabling of interrupts
*				  while polling is called.
* Nov 24, 1997	Jaspreet Singh	o Added counters to avoid enabling of interrupts
*				  when they have been disabled by another
*				  interface or routine (eg. wpf_poll).
* Nov 06, 1997	Jaspreet Singh	o Added INTR_TEST_MODE to avoid polling	
*				  routine disable interrupts during interrupt
*				  testing.
* Oct 20, 1997  Jaspreet Singh  o Added hooks in for Router UP time.
* Oct 16, 1997  Jaspreet Singh  o The critical flag is used to maintain flow
*                                 control by avoiding RACE conditions.  The
*                                 cli() and restore_flags() are taken out.
*                                 The fr_channel structure is appended for 
*                                 Driver Statistics.
* Oct 15, 1997  Farhan Thawar    o updated if_send() and receive for IPX
* Aug 29, 1997  Farhan Thawar    o Removed most of the cli() and sti()
*                                o Abstracted the UDP management stuff
*                                o Now use tbusy and critical more intelligently
* Jul 21, 1997  Jaspreet Singh	 o Can configure T391, T392, N391, N392 & N393
*				   through router.conf.
*				 o Protected calls to sdla_peek() by adDing 
*				   save_flags(), cli() and restore_flags().
*				 o Added error message for Inactive DLCIs in
*				   fr_event() and update_chan_state().
*				 o Fixed freeing up of buffers using kfree() 
*			           when packets are received.
* Jul 07, 1997	Jaspreet Singh	 o Added configurable TTL for UDP packets 
*				 o Added ability to discard multicast and 
*				   broadcast source addressed packets
* Jun 27, 1997	Jaspreet Singh	 o Added FT1 monitor capabilities 
*				   New case (0x44) statement in if_send routine *				   Added a global variable rCount to keep track
*			 	   of FT1 status enabled on the board.
* May 29, 1997	Jaspreet Singh	 o Fixed major Flow Control Problem
*				   With multiple boards a problem was seen where
*				   the second board always stopped transmitting
*				   packet after running for a while. The code
*				   got into a stage where the interrupts were
*				   disabled and dev->tbusy was set to 1.
*                  		   This caused the If_send() routine to get into*                                  the if clause for it(0,dev->tbusy) 
*				   forever.
*				   The code got into this stage due to an 
*				   interrupt occurring within the if clause for 
*				   set_bit(0,dev->tbusy).  Since an interrupt 
*				   disables furhter transmit interrupt and 
* 				   makes dev->tbusy = 0, this effect was undone *                                  by making dev->tbusy = 1 in the if clause.
*				   The Fix checks to see if Transmit interrupts
*				   are disabled then do not make dev->tbusy = 1
* 	   			   Introduced a global variable: int_occur and
*				   added tx_int_enabled in the wan_device 
*				   structure.	
* May 21, 1997  Jaspreet Singh   o Fixed UDP Management for multiple
*                                  boards.
*
* Apr 25, 1997  Farhan Thawar    o added UDP Management stuff
*                                o fixed bug in if_send() and tx_intr() to
*                                  sleep and wakeup all devices
* Mar 11, 1997  Farhan Thawar   Version 3.1.1
*                                o fixed (+1) bug in fr508_rx_intr()
*                                o changed if_send() to return 0 if
*                                  wandev.critical() is true
*                                o free socket buffer in if_send() if
*                                  returning 0 
*                                o added tx_intr() routine
* Jan 30, 1997	Gene Kozin	Version 3.1.0
*				 o implemented exec() entry point
*				 o fixed a bug causing driver configured as
*				   a FR switch to be stuck in WAN_
*				   mode
* Jan 02, 1997	Gene Kozin	Initial version.
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
#include <asm/io.h>		/* for inb(), outb(), etc. */
#include <linux/time.h>		/* for do_gettimeofday */
#define	_GNUC_
#include <linux/sdla_fr.h>	/* frame relay firmware API definitions */
#include <asm/uaccess.h>

/****** Defines & Macros ****************************************************/

#define	MAX_CMD_RETRY	10	/* max number of firmware retries */
#define	FR_HEADER_LEN	8	/* max encapsulation header size */
#define	FR_CHANNEL_MTU	1500	/* unfragmented logical channel MTU */

/* Q.922 frame types */

#define	Q922_UI		0x03	/* Unnumbered Info frame */
#define	Q922_XID	0xAF	/* ??? */

/* DLCI configured or not */

#define DLCI_NOT_CONFIGURED	0x00
#define DLCI_CONFIG_PENDING	0x01
#define DLCI_CONFIGURED		0x02

/* CIR enabled or not */

#define CIR_ENABLED	0x00
#define CIR_DISABLED	0x01

/* Interrupt mode for DLCI = 0 */

#define BUFFER_INTR_MODE	0x00
#define DLCI_LIST_INTR_MODE	0x01

/* Transmit Interrupt Status */

#define DISABLED 		0x00
#define WAITING_TO_BE_ENABLED	0x01

/* For handle_IPXWAN() */

#define CVHexToAscii(b) (((unsigned char)(b) > (unsigned char)9) ? ((unsigned char)'A' + ((unsigned char)(b) - (unsigned char)10)) : ((unsigned char)'0' + (unsigned char)(b)))

/****** Data Structures *****************************************************/

/* This is an extention of the 'struct device' we create for each network
 * interface to keep the rest of channel-specific data.
 */
typedef struct fr_channel {
	char name[WAN_IFNAME_SZ + 1];	/* interface name, ASCIIZ */
	unsigned dlci_configured;	/* check whether configured or not */
	unsigned cir_status;	/* check whether CIR enabled or not */
	unsigned dlci;		/* logical channel number */
	unsigned cir;		/* committed information rate */
	unsigned bc;		/* committed burst size */
	unsigned be;		/* excess burst size */
	unsigned mc;		/* multicast support on or off */
	unsigned tx_int_status;	/* Transmit Interrupt Status */
	unsigned short pkt_length;	/* Packet Length */
	unsigned long router_start_time;	/* Router start time in seconds */
	unsigned long tick_counter;	/* counter for transmit time out */
	char dev_pending_devtint;	/* interface pending dev_tint() */
	char state;		/* channel state */
	void *dlci_int_interface;	/* pointer to the DLCI Interface */
	unsigned long IB_addr;	/* physical address of Interface Byte */
	unsigned long state_tick;	/* time of the last state change */
	sdla_t *card;		/* -> owner */
	struct net_device_stats ifstats;		/* interface statistics */
	unsigned long if_send_entry;
	unsigned long if_send_skb_null;
	unsigned long if_send_broadcast;
	unsigned long if_send_multicast;
	unsigned long if_send_critical_ISR;
	unsigned long if_send_critical_non_ISR;
	unsigned long if_send_busy;
	unsigned long if_send_busy_timeout;
	unsigned long if_send_FPIPE_request;
	unsigned long if_send_DRVSTATS_request;
	unsigned long if_send_wan_disconnected;
	unsigned long if_send_dlci_disconnected;
	unsigned long if_send_no_bfrs;
	unsigned long if_send_adptr_bfrs_full;
	unsigned long if_send_bfrs_passed_to_adptr;
	unsigned long rx_intr_no_socket;
	unsigned long rx_intr_dev_not_started;
	unsigned long rx_intr_FPIPE_request;
	unsigned long rx_intr_DRVSTATS_request;
	unsigned long rx_intr_bfr_not_passed_to_stack;
	unsigned long rx_intr_bfr_passed_to_stack;
	unsigned long UDP_FPIPE_mgmt_kmalloc_err;
	unsigned long UDP_FPIPE_mgmt_direction_err;
	unsigned long UDP_FPIPE_mgmt_adptr_type_err;
	unsigned long UDP_FPIPE_mgmt_adptr_cmnd_OK;
	unsigned long UDP_FPIPE_mgmt_adptr_cmnd_timeout;
	unsigned long UDP_FPIPE_mgmt_adptr_send_passed;
	unsigned long UDP_FPIPE_mgmt_adptr_send_failed;
	unsigned long UDP_FPIPE_mgmt_not_passed_to_stack;
	unsigned long UDP_FPIPE_mgmt_passed_to_stack;
	unsigned long UDP_FPIPE_mgmt_no_socket;
	unsigned long UDP_DRVSTATS_mgmt_kmalloc_err;
	unsigned long UDP_DRVSTATS_mgmt_adptr_cmnd_OK;
	unsigned long UDP_DRVSTATS_mgmt_adptr_cmnd_timeout;
	unsigned long UDP_DRVSTATS_mgmt_adptr_send_passed;
	unsigned long UDP_DRVSTATS_mgmt_adptr_send_failed;
	unsigned long UDP_DRVSTATS_mgmt_not_passed_to_stack;
	unsigned long UDP_DRVSTATS_mgmt_passed_to_stack;
	unsigned long UDP_DRVSTATS_mgmt_no_socket;
	unsigned long router_up_time;
} fr_channel_t;

typedef struct dlci_status {
	unsigned short dlci PACKED;
	unsigned char state PACKED;
} dlci_status_t;

typedef struct dlci_IB_mapping {
	unsigned short dlci PACKED;
	unsigned long addr_value PACKED;
} dlci_IB_mapping_t;

/* This structure is used for DLCI list Tx interrupt mode.  It is used to
   enable interrupt bit and set the packet length for transmission
 */

typedef struct fr_dlci_interface {
	unsigned char gen_interrupt PACKED;
	unsigned short packet_length PACKED;
	unsigned char reserved PACKED;
} fr_dlci_interface_t;

static unsigned short num_frames;
static unsigned long curr_trace_addr;
static unsigned long start_trace_addr;
static unsigned short available_buffer_space;
static char TracingEnabled; /* variable for keeping track of enabling/disabling FT1 monitor status */
static int rCount = 0;
extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

/* variable for keeping track of number of interrupts generated during 
 * interrupt test routine 
 */
static int Intr_test_counter;

/****** Function Prototypes *************************************************/

/* WAN link driver entry points. These are called by the WAN router module. */
static int update(wan_device_t * wandev);
static int new_if(wan_device_t * wandev, struct device *dev,
		  wanif_conf_t * conf);
static int del_if(wan_device_t * wandev, struct device *dev);
/* WANPIPE-specific entry points */
static int wpf_exec(struct sdla *card, void *u_cmd, void *u_data);
/* Network device interface */
static int if_init(struct device *dev);
static int if_open(struct device *dev);
static int if_close(struct device *dev);
static int if_header(struct sk_buff *skb, struct device *dev,
	    unsigned short type, void *daddr, void *saddr, unsigned len);
static int if_rebuild_hdr(struct sk_buff *skb);
static int if_send(struct sk_buff *skb, struct device *dev);
static struct net_device_stats *if_stats(struct device *dev);
/* Interrupt handlers */
static void fr502_isr(sdla_t * card);
static void fr508_isr(sdla_t * card);
static void fr502_rx_intr(sdla_t * card);
static void fr508_rx_intr(sdla_t * card);
static void tx_intr(sdla_t * card);
static void spur_intr(sdla_t * card);
/* Background polling routines */
static void wpf_poll(sdla_t * card);
/* Frame relay firmware interface functions */
static int fr_read_version(sdla_t * card, char *str);
static int fr_configure(sdla_t * card, fr_conf_t * conf);
static int fr_dlci_configure(sdla_t * card, fr_dlc_conf_t * conf, unsigned dlci);
static int fr_set_intr_mode(sdla_t * card, unsigned mode, unsigned mtu);
static int fr_comm_enable(sdla_t * card);
static int fr_comm_disable(sdla_t * card);
static int fr_get_err_stats(sdla_t * card);
static int fr_get_stats(sdla_t * card);
static int fr_add_dlci(sdla_t * card, int dlci, int num);
static int fr_activate_dlci(sdla_t * card, int dlci, int num);
static int fr_issue_isf(sdla_t * card, int isf);
static int fr502_send(sdla_t * card, int dlci, int attr, int len, void *buf);
static int fr508_send(sdla_t * card, int dlci, int attr, int len, void *buf);
/* Firmware asynchronous event handlers */
static int fr_event(sdla_t * card, int event, fr_mbox_t * mbox);
static int fr_modem_failure(sdla_t * card, fr_mbox_t * mbox);
static int fr_dlci_change(sdla_t * card, fr_mbox_t * mbox);
/* Miscellaneous functions */
static int update_chan_state(struct device *dev);
static void set_chan_state(struct device *dev, int state);
static struct device *find_channel(sdla_t * card, unsigned dlci);
static int is_tx_ready(sdla_t * card, fr_channel_t * chan);
static unsigned int dec_to_uint(unsigned char *str, int len);
static int reply_udp(unsigned char *data, unsigned int mbox_len);
static int intr_test(sdla_t * card);
static void init_chan_statistics(fr_channel_t * chan);
static void init_global_statistics(sdla_t * card);
static void read_DLCI_IB_mapping(sdla_t * card, fr_channel_t * chan);
/* Udp management functions */
static int process_udp_mgmt_pkt(char udp_pkt_src, sdla_t * card, struct sk_buff *skb, struct device *dev, int dlci, fr_channel_t * chan);
static int process_udp_driver_call(char udp_pkt_src, sdla_t * card, struct sk_buff *skb, struct device *dev, int dlci, fr_channel_t * chan);
static int udp_pkt_type(struct sk_buff *skb, sdla_t * card);
/* IPX functions */
static void switch_net_numbers(unsigned char *sendpacket, unsigned long network_number, unsigned char incoming);
static int handle_IPXWAN(unsigned char *sendpacket, char *devname, unsigned char enable_IPX, unsigned long network_number);

/****** Public Functions ****************************************************/

/*============================================================================
 * Frame relay protocol initialization routine.
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
int wpf_init(sdla_t * card, wandev_conf_t * conf)
{
	union {
		char str[80];
		fr_conf_t cfg;
	} u;
	int i;
	/* Verify configuration ID */
	if (conf->config_id != WANCONFIG_FR) 
	{
		printk(KERN_INFO "%s: invalid configuration ID %u!\n",
		       card->devname, conf->config_id);
		return -EINVAL;
	}
	/* Initialize protocol-specific fields of adapter data space */
	switch (card->hw.fwid) 
	{
		case SFID_FR502:
			card->mbox = (void *) (card->hw.dpmbase + FR502_MBOX_OFFS);
			card->rxmb = (void *) (card->hw.dpmbase + FR502_RXMB_OFFS);
			card->flags = (void *) (card->hw.dpmbase + FR502_FLAG_OFFS);
			card->isr = &fr502_isr;
			break;
		case SFID_FR508:
			card->mbox = (void *) (card->hw.dpmbase + FR508_MBOX_OFFS);
			card->flags = (void *) (card->hw.dpmbase + FR508_FLAG_OFFS);
			card->isr = &fr508_isr;
			break;
		default:
			return -EINVAL;
	}
	/* Read firmware version.  Note that when adapter initializes, it
	 * clears the mailbox, so it may appear that the first command was
	 * executed successfully when in fact it was merely erased. To work
	 * around this, we execute the first command twice.
	 */
	if (fr_read_version(card, NULL) || fr_read_version(card, u.str))
		return -EIO;
	printk(KERN_INFO "%s: running frame relay firmware v%s\n",
	       card->devname, u.str);
	/* Adjust configuration */
	conf->mtu = max(min(conf->mtu, 4080), FR_CHANNEL_MTU + FR_HEADER_LEN);
	conf->bps = min(conf->bps, 2048000);
	/* Configure adapter firmware */
	memset(&u.cfg, 0, sizeof(u.cfg));
	u.cfg.mtu = conf->mtu;
	u.cfg.kbps = conf->bps / 1000;
	u.cfg.cir_fwd = u.cfg.cir_bwd = 16;
	u.cfg.bc_fwd = u.cfg.bc_bwd = 16;
	if (conf->station == WANOPT_CPE) 
	{
		u.cfg.options = 0x0080;
		printk(KERN_INFO "%s: Global CIR enabled by Default\n", card->devname);
	}
	else
	{
		u.cfg.options = 0x0081;
	}
	switch (conf->u.fr.signalling) 
	{
		case WANOPT_FR_Q933:
			u.cfg.options |= 0x0200;
			break;
		case WANOPT_FR_LMI:
			u.cfg.options |= 0x0400;
			break;
	}
	if (conf->station == WANOPT_CPE) 
	{
		u.cfg.options |= 0x8000;	/* auto config DLCI */
		card->u.f.dlci_num = 0;
	}
	else
	{
		u.cfg.station = 1;	/* switch emulation mode */
		/* For switch emulation we have to create a list of dlci(s)
		 * that will be sent to be global SET_DLCI_CONFIGURATION 
		 * command in fr_configure() routine. 
		 */
		card->u.f.dlci_num = min(max(conf->u.fr.dlci_num, 1), 100);
		for (i = 0; i < card->u.f.dlci_num; i++) 
		{
			card->u.f.node_dlci[i] = (unsigned short)
			    conf->u.fr.dlci[i] ? conf->u.fr.dlci[i] : 16;
		}
	}
	if (conf->clocking == WANOPT_INTERNAL)
		u.cfg.port |= 0x0001;
	if (conf->interface == WANOPT_RS232)
		u.cfg.port |= 0x0002;
	if (conf->u.fr.t391)
		u.cfg.t391 = min(conf->u.fr.t391, 30);
	else
		u.cfg.t391 = 5;
	if (conf->u.fr.t392)
		u.cfg.t392 = min(conf->u.fr.t392, 30);
	else
		u.cfg.t392 = 15;
	if (conf->u.fr.n391)
		u.cfg.n391 = min(conf->u.fr.n391, 255);
	else
		u.cfg.n391 = 2;
	if (conf->u.fr.n392)
		u.cfg.n392 = min(conf->u.fr.n392, 10);
	else
		u.cfg.n392 = 3;
	if (conf->u.fr.n393)
		u.cfg.n393 = min(conf->u.fr.n393, 10);
	else
		u.cfg.n393 = 4;
	if (fr_configure(card, &u.cfg))
		return -EIO;
	if (card->hw.fwid == SFID_FR508) 
	{
		fr_buf_info_t *buf_info =
		(void *) (card->hw.dpmbase + FR508_RXBC_OFFS);
		card->rxmb = (void *) (buf_info->rse_next - FR_MB_VECTOR + card->hw.dpmbase);
		card->u.f.rxmb_base = (void *) (buf_info->rse_base - FR_MB_VECTOR + card->hw.dpmbase);
		card->u.f.rxmb_last = (void *) (buf_info->rse_base + (buf_info->rse_num - 1) * 
				sizeof(fr_buf_ctl_t) - FR_MB_VECTOR + card->hw.dpmbase);
		card->u.f.rx_base = buf_info->buf_base;
		card->u.f.rx_top = buf_info->buf_top;
	}
	card->wandev.mtu = conf->mtu;
	card->wandev.bps = conf->bps;
	card->wandev.interface = conf->interface;
	card->wandev.clocking = conf->clocking;
	card->wandev.station = conf->station;
	card->poll = &wpf_poll;
	card->exec = &wpf_exec;
	card->wandev.update = &update;
	card->wandev.new_if = &new_if;
	card->wandev.del_if = &del_if;
	card->wandev.state = WAN_DISCONNECTED;
	card->wandev.ttl = conf->ttl;
	card->wandev.udp_port = conf->udp_port;
	card->wandev.enable_tx_int = 0;
	card->irq_dis_if_send_count = 0;
	card->irq_dis_poll_count = 0;
	card->wandev.enable_IPX = conf->enable_IPX;
	if (conf->network_number)
		card->wandev.network_number = conf->network_number;
	else
		card->wandev.network_number = 0xDEADBEEF;
	/* Intialize global statistics for a card */
	init_global_statistics(card);
	TracingEnabled = 0;
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
	fr_get_err_stats(card);
	fr_get_stats(card);
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
	fr_channel_t *chan;
	int err = 0;
	if ((conf->name[0] == '\0') || (strlen(conf->name) > WAN_IFNAME_SZ)) 
	{
		printk(KERN_INFO "%s: invalid interface name!\n",
		       card->devname);
		return -EINVAL;
	}
	/* allocate and initialize private data */
	chan = kmalloc(sizeof(fr_channel_t), GFP_KERNEL);
	if (chan == NULL)
		return -ENOMEM;
	memset(chan, 0, sizeof(fr_channel_t));
	strcpy(chan->name, conf->name);
	chan->card = card;
	/* verify media address */
	if (is_digit(conf->addr[0])) 
	{
		int dlci = dec_to_uint(conf->addr, 0);
		if (dlci && (dlci <= 4095)) 
		{
			chan->dlci = dlci;
		}
		else
		{
			printk(KERN_ERR "%s: invalid DLCI %u on interface %s!\n",
			       wandev->name, dlci, chan->name);
			err = -EINVAL;
		}
	} 
	else 
	{
		printk(KERN_ERR "%s: invalid media address on interface %s!\n",
		       wandev->name, chan->name);
		err = -EINVAL;
	}
	if (err) 
	{
		kfree(chan);
		return err;
	}
	/* place cir,be,bc and other channel specific information into the
	 * chan structure 
	 */
	if (conf->cir) 
	{
		chan->cir = max(1, min(conf->cir, 512));
		chan->cir_status = CIR_ENABLED;
		if (conf->bc)
			chan->bc = max(1, min(conf->bc, 512));
		if (conf->be)
			chan->be = max(0, min(conf->be, 511));
	}
	else
		chan->cir_status = CIR_DISABLED;
	chan->mc = conf->mc;
	chan->dlci_configured = DLCI_NOT_CONFIGURED;
	chan->tx_int_status = DISABLED;
	init_chan_statistics(chan);
	/* prepare network device data space for registration */
	dev->name = chan->name;
	dev->init = &if_init;
	dev->priv = chan;
	return 0;
}
/*============================================================================
 * Delete logical channel.
 */
static int del_if(wan_device_t * wandev, struct device *dev)
{
	if (dev->priv) 
	{
		kfree(dev->priv);
		dev->priv = NULL;
	}
	return 0;
}

/****** WANPIPE-specific entry points ***************************************/

/*============================================================================
 * Execute adapter interface command.
 */
static int wpf_exec(struct sdla *card, void *u_cmd, void *u_data)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err, len;
	fr_cmd_t cmd;
	if(copy_from_user((void *) &cmd, u_cmd, sizeof(cmd)))
		return -EFAULT;		
	/* execute command */
	do 
	{
		memcpy(&mbox->cmd, &cmd, sizeof(cmd));
		if (cmd.length)
		{
			if(copy_from_user((void *) &mbox->data, u_data, cmd.length))
				return -EFAULT;
		}
		if (sdla_exec(mbox))
			err = mbox->cmd.result;
		else
			return -EIO;
	} 
	while (err && retry-- && fr_event(card, err, mbox));

	/* return result */

	if(copy_to_user(u_cmd, (void *) &mbox->cmd, sizeof(fr_cmd_t)))
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
	fr_channel_t *chan = dev->priv;
	sdla_t *card = chan->card;
	wan_device_t *wandev = &card->wandev;

	/* Initialize device driver entry points */
	dev->open = &if_open;
	dev->stop = &if_close;
	dev->hard_header = &if_header;
	dev->rebuild_header = &if_rebuild_hdr;
	dev->hard_start_xmit = &if_send;
	dev->get_stats = &if_stats;
	/* Initialize media-specific parameters */
	dev->type = ARPHRD_DLCI;			/* ARP h/w type */
	dev->mtu = FR_CHANNEL_MTU;
	dev->hard_header_len = FR_HEADER_LEN;		/* media header length */
	dev->addr_len = 2;				/* hardware address length */
	*(unsigned short *) dev->dev_addr = htons(chan->dlci);
	/* Initialize hardware parameters (just for reference) */
	dev->irq = wandev->irq;
	dev->dma = wandev->dma;
	dev->base_addr = wandev->ioport;
	dev->mem_start = (unsigned long)wandev->maddr;
	dev->mem_end = dev->mem_start + wandev->msize - 1;
	/* Set transmit buffer queue length */
	dev->tx_queue_len = 10;
	/* Initialize socket buffers */
	dev_init_buffers(dev);
	set_chan_state(dev, WAN_DISCONNECTED);
	return 0;
}

/*============================================================================
 * Open network interface.
 * o if this is the first open, then enable communications and interrupts.
 * o prevent module from unloading by incrementing use count
 *
 * Return 0 if O.k. or errno.
 */

static int if_open(struct device *dev)
{
	fr_channel_t *chan = dev->priv;
	sdla_t *card = chan->card;
	struct device *dev2;
	int err = 0;
	fr508_flags_t *flags = card->flags;
	struct timeval tv;
	if (dev->start)
		return -EBUSY;	/* only one open is allowed */
	if (test_and_set_bit(0, (void *) &card->wandev.critical))
		return -EAGAIN;
	if (!card->open_cnt) 
	{
		Intr_test_counter = 0;
		card->intr_mode = INTR_TEST_MODE;
		err = intr_test(card);
		if ((err) || (Intr_test_counter != (MAX_INTR_TEST_COUNTER + 1))) {
			printk(KERN_INFO
			       "%s: Interrupt Test Failed, Counter: %i\n",
			       card->devname, Intr_test_counter);
			err = -EIO;
			card->wandev.critical = 0;
			return err;
		}
		printk(KERN_INFO "%s: Interrupt Test Passed, Counter: %i\n"
		       ,card->devname, Intr_test_counter);
		/* The following allocates and intializes a circular
		 * link list of interfaces per card.
		 */
		card->devs_struct = kmalloc(sizeof(load_sharing_t), GFP_KERNEL);
		if (card->devs_struct == NULL)
			return -ENOMEM;
		card->dev_to_devtint_next = card->devs_struct;
		for (dev2 = card->wandev.dev; dev2; dev2 = dev2->slave) {
			(card->devs_struct)->dev_ptr = dev2;
			if (dev2->slave == NULL)
				(card->devs_struct)->next = card->dev_to_devtint_next;
			else {
				(card->devs_struct)->next = kmalloc(
				     sizeof(load_sharing_t), GFP_KERNEL);
				if ((card->devs_struct)->next == NULL)
					return -ENOMEM;
				card->devs_struct = (card->devs_struct)->next;
			}
		}
		card->devs_struct = card->dev_to_devtint_next;
		card->intr_mode = BUFFER_INTR_MODE;
		/* 
		   check all the interfaces for the device to see if CIR has
		   been enabled for any DLCI(s). If so then use the DLCI list
		   Interrupt mode for fr_set_intr_mode(), otherwise use the                     default global interrupt mode
		 */
		for (dev2 = card->wandev.dev; dev2; dev2 = dev2->slave) {
			if (((fr_channel_t *) dev2->priv)->cir_status
			    == CIR_ENABLED) {
				card->intr_mode = DLCI_LIST_INTR_MODE;
				break;
			}
		}
		/* 
		   If you enable comms and then set ints, you get a Tx int as you
		   perform the SET_INT_TRIGGERS command. So, we only set int
		   triggers and then adjust the interrupt mask (to disable Tx ints)             before enabling comms. 
		 */
		if (card->intr_mode == BUFFER_INTR_MODE) {
			if (fr_set_intr_mode(card, 0x03, card->wandev.mtu)) {
				err = -EIO;
				card->wandev.critical = 0;
				return err;
			}
			printk(KERN_INFO
			       "%s: Global Buffering Tx Interrupt Mode\n"
			       ,card->devname);
		} else if (card->intr_mode == DLCI_LIST_INTR_MODE) {
			if (fr_set_intr_mode(card, 0x83, card->wandev.mtu)) {
				err = -EIO;
				card->wandev.critical = 0;
				return err;
			}
			printk(KERN_INFO
			       "%s: DLCI list Tx Interrupt Mode\n",
			       card->devname);
		}
		flags->imask &= ~0x02;
		if (fr_comm_enable(card)) {
			err = -EIO;
			card->wandev.critical = 0;
			return err;
		}
		wanpipe_set_state(card, WAN_CONNECTED);
		if (card->wandev.station == WANOPT_CPE) {
			/* CPE: issue full status enquiry */
			fr_issue_isf(card, FR_ISF_FSE);
		} else {	/* FR switch: activate DLCI(s) */
			/* For Switch emulation we have to ADD and ACTIVATE
			 * the DLCI(s) that were configured with the SET_DLCI_
			 * CONFIGURATION command. Add and Activate will fail if
			 * DLCI specified is not included in the list.
			 *
			 * Also If_open is called once for each interface. But
			 * it does not get in here for all the interface. So
			 * we have to pass the entire list of DLCI(s) to add 
			 * activate routines.  
			 */
			fr_add_dlci(card,
			     card->u.f.node_dlci[0], card->u.f.dlci_num);
			fr_activate_dlci(card,
			     card->u.f.node_dlci[0], card->u.f.dlci_num);
		}
	}
	dev->mtu = min(dev->mtu, card->wandev.mtu - FR_HEADER_LEN);
	dev->interrupt = 0;
	dev->tbusy = 0;
	dev->start = 1;
	wanpipe_open(card);
	update_chan_state(dev);
	do_gettimeofday(&tv);
	chan->router_start_time = tv.tv_sec;
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
	fr_channel_t *chan = dev->priv;
	sdla_t *card = chan->card;
	if (test_and_set_bit(0, (void *) &card->wandev.critical))
		return -EAGAIN;
	dev->start = 0;
	wanpipe_close(card);
	if (!card->open_cnt) 
	{
		wanpipe_set_state(card, WAN_DISCONNECTED);
		fr_set_intr_mode(card, 0, 0);
		fr_comm_disable(card);
	}
	card->wandev.critical = 0;
	return 0;
}

/*============================================================================
 * Build media header.
 * o encapsulate packet according to encapsulation type.
 *
 * The trick here is to put packet type (Ethertype) into 'protocol' field of
 * the socket buffer, so that we don't forget it.  If encapsulation fails,
 * set skb->protocol to 0 and discard packet later.
 *
 * Return:	media header length.
 */

static int if_header(struct sk_buff *skb, struct device *dev,
	     unsigned short type, void *daddr, void *saddr, unsigned len)
{
	int hdr_len = 0;
	skb->protocol = type;
	hdr_len = wanrouter_encapsulate(skb, dev);
	if (hdr_len < 0) 
	{
		hdr_len = 0;
		skb->protocol = 0;
	}
	skb_push(skb, 1);
	skb->data[0] = Q922_UI;
	++hdr_len;
	return hdr_len;
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
	fr_channel_t *chan = dev->priv;
	sdla_t *card = chan->card;
	printk(KERN_INFO "%s: rebuild_header() called for interface %s!\n",
	       card->devname, dev->name);
	return 1;
}

/*============================================================================
 * Send a packet on a network interface.
 * o set tbusy flag (marks start of the transmission) to block a timer-based
 *   transmit from overlapping.
 * o check link state. If link is not up, then drop the packet.
 * o check channel status. If it's down then initiate a call.
 * o pass a packet to corresponding WAN device.
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
	fr_channel_t *chan = dev->priv;
	sdla_t *card = chan->card;
	int retry = 0, err;
	unsigned char *sendpacket;
	struct device *dev2;
	unsigned long check_braddr, check_mcaddr;
	fr508_flags_t *adptr_flags = card->flags;
	int udp_type, send_data;
	fr_dlci_interface_t *dlci_interface = chan->dlci_int_interface;
	unsigned long host_cpu_flags;
	++chan->if_send_entry;

	if (dev->tbusy) 
	{
		/* If our device stays busy for at least 5 seconds then we will
		 * kick start the device by making dev->tbusy = 0.  We expect
		 * that our device never stays busy more than 5 seconds. So this                 * is only used as a last resort.
		 */
		++chan->if_send_busy;
		++chan->ifstats.collisions;
		if ((jiffies - chan->tick_counter) < (5 * HZ))
			return 1;

		printk(KERN_INFO "%s: Transmit timed out\n", chan->name);
		++chan->if_send_busy_timeout;
		/* unbusy all the interfaces on the card */
		for (dev2 = card->wandev.dev; dev2; dev2 = dev2->slave)
			dev2->tbusy = 0;
	}
	sendpacket = skb->data;
	udp_type = udp_pkt_type(skb, card);
	if (udp_type == UDP_DRVSTATS_TYPE) 
	{
		++chan->if_send_DRVSTATS_request;
		process_udp_driver_call(UDP_PKT_FRM_STACK, card, skb, dev, 0,
					chan);
		dev_kfree_skb(skb);
		return 0;
	}
	else if (udp_type == UDP_FPIPE_TYPE)
		++chan->if_send_FPIPE_request;
	/* retreive source address in two forms: broadcast & multicast */
	check_braddr = sendpacket[17];
	check_mcaddr = sendpacket[14];
	check_braddr = check_braddr << 8;
	check_mcaddr = check_mcaddr << 8;
	check_braddr |= sendpacket[16];
	check_mcaddr |= sendpacket[15];
	check_braddr = check_braddr << 8;
	check_mcaddr = check_mcaddr << 8;
	check_braddr |= sendpacket[15];
	check_mcaddr |= sendpacket[16];
	check_braddr = check_braddr << 8;
	check_mcaddr = check_mcaddr << 8;
	check_braddr |= sendpacket[14];
	check_mcaddr |= sendpacket[17];
	/* if the Source Address is a Multicast address */
	if ((chan->mc == WANOPT_NO) && (check_mcaddr >= 0xE0000001) &&
	    (check_mcaddr <= 0xFFFFFFFE)) 
	{
		printk(KERN_INFO "%s: Multicast Src. Addr. silently discarded\n"
		       ,card->devname);
		dev_kfree_skb(skb);
		++chan->ifstats.tx_dropped;
		++chan->if_send_multicast;
		return 0;
	}
	disable_irq(card->hw.irq);
	++card->irq_dis_if_send_count;
	if (test_and_set_bit(0, (void *) &card->wandev.critical)) 
	{
		if (card->wandev.critical == CRITICAL_IN_ISR) 
		{
			++chan->if_send_critical_ISR;
			if (card->intr_mode == DLCI_LIST_INTR_MODE) 
			{
				/* The enable_tx_int flag is set here so that if
				 * the critical flag is set due to an interrupt 
				 * then we want to enable transmit interrupts 
				 * again.
				 */
				card->wandev.enable_tx_int = 1;
				/* Setting this flag to WAITING_TO_BE_ENABLED 
				 * specifies that interrupt bit has to be 
				 * enabled for that particular interface. 
				 * (delayed interrupt)
				 */
				chan->tx_int_status = WAITING_TO_BE_ENABLED;
				/* This is used for enabling dynamic calculation
				 * of CIRs relative to the packet length.
				 */
				chan->pkt_length = skb->len;
				dev->tbusy = 1;
				chan->tick_counter = jiffies;
			}
			else
			{
				card->wandev.enable_tx_int = 1;
				dev->tbusy = 1;
				chan->tick_counter = jiffies;
			}
			save_flags(host_cpu_flags);
			cli();
			if ((!(--card->irq_dis_if_send_count)) &&
			    (!card->irq_dis_poll_count))
				enable_irq(card->hw.irq);
			restore_flags(host_cpu_flags);
			return 1;
		}
		++chan->if_send_critical_non_ISR;
		++chan->ifstats.tx_dropped;
		dev_kfree_skb(skb);
		save_flags(host_cpu_flags);
		cli();
		if ((!(--card->irq_dis_if_send_count)) &&
		    (!card->irq_dis_poll_count))
			enable_irq(card->hw.irq);
		restore_flags(host_cpu_flags);
		return 0;
	}
	card->wandev.critical = 0x21;
	if (udp_type == UDP_FPIPE_TYPE) 
	{
		err = process_udp_mgmt_pkt(UDP_PKT_FRM_STACK, card, skb,
					   dev, 0, chan);
	}
	else if (card->wandev.state != WAN_CONNECTED) 
	{
		++chan->if_send_wan_disconnected;
		++chan->ifstats.tx_dropped;
		++card->wandev.stats.tx_dropped;
	}
	else if (chan->state != WAN_CONNECTED) 
	{
		++chan->if_send_dlci_disconnected;
		update_chan_state(dev);
		++chan->ifstats.tx_dropped;
		++card->wandev.stats.tx_dropped;
	}
	else if (!is_tx_ready(card, chan)) 
	{
		if (card->intr_mode == DLCI_LIST_INTR_MODE) 
		{
			dlci_interface->gen_interrupt |= 0x40;
			dlci_interface->packet_length = skb->len;
		}
		dev->tbusy = 1;
		chan->tick_counter = jiffies;
		adptr_flags->imask |= 0x02;
		++chan->if_send_no_bfrs;
		retry = 1;
	}
	else
	{
		send_data = 1;
		/* If it's an IPX packet */
		if (sendpacket[1] == 0x00 &&
		    sendpacket[2] == 0x80 &&
		    sendpacket[6] == 0x81 &&
		    sendpacket[7] == 0x37) 
		{
			if (card->wandev.enable_IPX) 
			{
				switch_net_numbers(sendpacket,
					 card->wandev.network_number, 0);
			} 
			else 
			{
				/* increment some statistic here! */
				send_data = 0;
			}
		}
		if (send_data) 
		{
			err = (card->hw.fwid == SFID_FR508) ?
			    fr508_send(card, chan->dlci, 0, skb->len, skb->data) :
			    fr502_send(card, chan->dlci, 0, skb->len, skb->data);
			if (err) 
			{
				if (card->intr_mode == DLCI_LIST_INTR_MODE) 
				{
					dlci_interface->gen_interrupt |= 0x40;
					dlci_interface->packet_length = skb->len;
				}
				dev->tbusy = 1;
				chan->tick_counter = jiffies;
				adptr_flags->imask |= 0x02;
				retry = 1;
				++chan->if_send_adptr_bfrs_full;
				++chan->ifstats.tx_errors;
				++card->wandev.stats.tx_errors;
			}
			else 
			{
				++chan->if_send_bfrs_passed_to_adptr;
				++chan->ifstats.tx_packets;
				++card->wandev.stats.tx_packets;
				chan->ifstats.tx_bytes += skb->len;
				card->wandev.stats.tx_bytes += skb->len;
			}
		}
	}
	if (!retry)
		dev_kfree_skb(skb);

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
 * Return nothing.
 */

static int reply_udp(unsigned char *data, unsigned int mbox_len)
{
	unsigned short len, udp_length, temp, i, ip_length;
	unsigned long sum;
	/* Set length of packet */
	len = mbox_len + 62;
	/* fill in UDP reply */
	data[38] = 0x02;
	/* fill in UDP length */
	udp_length = mbox_len + 40;
	/* put it on an even boundary */
	if (udp_length & 0x0001) 
	{
		udp_length += 1;
		len += 1;
	}
	temp = (udp_length << 8) | (udp_length >> 8);
	memcpy(&data[26], &temp, 2);
	/* swap UDP ports */
	memcpy(&temp, &data[22], 2);
	memcpy(&data[22], &data[24], 2);
	memcpy(&data[24], &temp, 2);
	/* add UDP pseudo header */
	temp = 0x1100;
	memcpy(&data[udp_length + 22], &temp, 2);
	temp = (udp_length << 8) | (udp_length >> 8);
	memcpy(&data[udp_length + 24], &temp, 2);
	/* calculate UDP checksum */
	data[28] = data[29] = 0;
	sum = 0;
	for (i = 0; i < udp_length + 12; i += 2) 
	{
		memcpy(&temp, &data[14 + i], 2);
		sum += (unsigned long) temp;
	}
	while (sum >> 16)
		sum = (sum & 0xffffUL) + (sum >> 16);

	temp = (unsigned short) sum;
	temp = ~temp;
	if (temp == 0)
		temp = 0xffff;
	memcpy(&data[28], &temp, 2);
	/* fill in IP length */
	ip_length = udp_length + 20;
	temp = (ip_length << 8) | (ip_length >> 8);
	memcpy(&data[4], &temp, 2);
	/* swap IP addresses */
	memcpy(&temp, &data[14], 2);
	memcpy(&data[14], &data[18], 2);
	memcpy(&data[18], &temp, 2);
	memcpy(&temp, &data[16], 2);
	memcpy(&data[16], &data[20], 2);
	memcpy(&data[20], &temp, 2);
	/* fill in IP checksum */
	data[12] = data[13] = 0;
	sum = 0;
	for (i = 0; i < 20; i += 2) 
	{
		memcpy(&temp, &data[2 + i], 2);
		sum += (unsigned long) temp;
	}
	while (sum >> 16)
		sum = (sum & 0xffffUL) + (sum >> 16);
	temp = (unsigned short) sum;
	temp = ~temp;
	if (temp == 0)
		temp = 0xffff;
	memcpy(&data[12], &temp, 2);
	return len;
}				/* reply_udp */
/*
   If incoming is 0 (outgoing)- if the net numbers is ours make it 0
   if incoming is 1 - if the net number is 0 make it ours 
 */

static void switch_net_numbers(unsigned char *sendpacket, unsigned long network_number, unsigned char incoming)
{
	unsigned long pnetwork_number;
	pnetwork_number = (unsigned long) ((sendpacket[14] << 24) +
			 (sendpacket[15] << 16) + (sendpacket[16] << 8) +
					   sendpacket[17]);
	if (!incoming) {
		/* If the destination network number is ours, make it 0 */
		if (pnetwork_number == network_number) {
			sendpacket[14] = sendpacket[15] = sendpacket[16] =
			    sendpacket[17] = 0x00;
		}
	} else {
		/* If the incoming network is 0, make it ours */
		if (pnetwork_number == 0) 
		{
			sendpacket[14] = (unsigned char) (network_number >> 24);
			sendpacket[15] = (unsigned char) ((network_number &
						      0x00FF0000) >> 16);
			sendpacket[16] = (unsigned char) ((network_number &
						       0x0000FF00) >> 8);
			sendpacket[17] = (unsigned char) (network_number &
							  0x000000FF);
		}
	}
	pnetwork_number = (unsigned long) ((sendpacket[26] << 24) +
			 (sendpacket[27] << 16) + (sendpacket[28] << 8) +
					   sendpacket[29]);
	if (!incoming) {
		/* If the source network is ours, make it 0 */
		if (pnetwork_number == network_number) 
		{
			sendpacket[26] = sendpacket[27] = sendpacket[28] =
			    sendpacket[29] = 0x00;
		}
	} else {
		/* If the source network is 0, make it ours */
		if (pnetwork_number == 0) {
			sendpacket[26] = (unsigned char) (network_number >> 24);
			sendpacket[27] = (unsigned char) ((network_number &
						      0x00FF0000) >> 16);
			sendpacket[28] = (unsigned char) ((network_number &
						       0x0000FF00) >> 8);
			sendpacket[29] = (unsigned char) (network_number &
							  0x000000FF);
		}
	}
}				/* switch_net_numbers */

/*============================================================================
 * Get Ethernet-style interface statistics.
 * Return a pointer to struct net_device_stats.
 */

static struct net_device_stats *if_stats(struct device *dev)
{
	fr_channel_t *chan = dev->priv;
	if(chan==NULL)
		return NULL;
		
	return &chan->ifstats;
}

/****** Interrupt Handlers **************************************************/
/*============================================================================
 * S502 frame relay interrupt service routine.
 */

static void fr502_isr(sdla_t * card)
{
	fr502_flags_t *flags = card->flags;
	switch (flags->iflag) 
	{
		case 0x01:		/* receive interrupt */
			fr502_rx_intr(card);
			break;
		case 0x02:		/* transmit interrupt */
			flags->imask &= ~0x02;
			tx_intr(card);
			break;
		default:
			spur_intr(card);
	}
	flags->iflag = 0;
}
/*============================================================================
 * S508 frame relay interrupt service routine.
 */

static void fr508_isr(sdla_t * card)
{
	fr508_flags_t *flags = card->flags;
	fr_buf_ctl_t *bctl;
	char *ptr = &flags->iflag;
	struct device *dev = card->wandev.dev;
	struct device *dev2;
	int i;
	unsigned long host_cpu_flags;
	unsigned disable_tx_intr = 1;
	fr_channel_t *chan;
	fr_dlci_interface_t *dlci_interface;
	/* This flag prevents nesting of interrupts.  See sdla_isr() routine
	 * in sdlamain.c. 
	 */
	card->in_isr = 1;
	++card->statistics.isr_entry;
	if (test_and_set_bit(0, (void *) &card->wandev.critical)) 
	{
		printk(KERN_INFO "fr508_isr: %s, wandev.critical set to 0x%02X, int type = 0x%02X\n", card->devname, card->wandev.critical, flags->iflag);
		++card->statistics.isr_already_critical;
		card->in_isr = 0;
		return;
	}
	/* For all interrupts set the critical flag to CRITICAL_RX_INTR.
	 * If the if_send routine is called with this flag set it will set
	 * the enable transmit flag to 1. (for a delayed interrupt)
	 */
	card->wandev.critical = CRITICAL_IN_ISR;
	card->dlci_int_mode_unbusy = 0;
	card->buff_int_mode_unbusy = 0;
	switch (flags->iflag) 
	{
		case 0x01:		/* receive interrupt */
			++card->statistics.isr_rx;
			fr508_rx_intr(card);
			break;
		case 0x02:		/* transmit interrupt */
			++card->statistics.isr_tx;
			bctl = (void *) (flags->tse_offs - FR_MB_VECTOR +
				 card->hw.dpmbase);
			bctl->flag = 0xA0;
			if (card->intr_mode == DLCI_LIST_INTR_MODE) 
			{
				/* Find the structure and make it unbusy */
				dev = find_channel(card, flags->dlci);
				dev->tbusy = 0;
				/* This is used to perform devtint at the
				 * end of the isr 
				 */
				card->dlci_int_mode_unbusy = 1;
				/* check to see if any other interfaces are
				 * busy. If so then do not disable the tx
				 * interrupts 
				 */
				for (dev2 = card->wandev.dev; dev2;
					dev2 = dev2->slave) 
				{
					if (dev2->tbusy == 1) 
					{
						disable_tx_intr = 0;
						break;
					}
				}
				if (disable_tx_intr)
					flags->imask &= ~0x02;
			} 
			else if (card->intr_mode == BUFFER_INTR_MODE) 
			{
				for (dev2 = card->wandev.dev; dev2;
					dev2 = dev2->slave) 
				{
					if (!dev2 || !dev2->start) 
					{
						++card->statistics.tx_intr_dev_not_started;
						continue;
					}
					if (dev2->tbusy) 
					{
						card->buff_int_mode_unbusy = 1;
						((fr_channel_t *) dev2->priv)->dev_pending_devtint = 1;
						dev2->tbusy = 0;
					} 
					else
						((fr_channel_t *) dev2->priv)->dev_pending_devtint = 0;
				}
				flags->imask &= ~0x02;
			}
			break;
		case 0x08:
			Intr_test_counter++;
			++card->statistics.isr_intr_test;
			break;
		default:
			++card->statistics.isr_spurious;
			spur_intr(card);
			printk(KERN_INFO "%s: Interrupt Type 0x%02X!\n",
			       card->devname, flags->iflag);
			printk(KERN_INFO "%s: ID Bytes = ", card->devname);
			for (i = 0; i < 8; i++)
				printk(KERN_INFO "0x%02X ", *(ptr + 0x28 + i));
			printk(KERN_INFO "\n");
			break;
	}
	card->wandev.critical = CRITICAL_INTR_HANDLED;
	if (card->wandev.enable_tx_int) 
	{
		if (card->intr_mode == DLCI_LIST_INTR_MODE) 
		{
			for (dev2 = card->wandev.dev; dev2; dev2 = dev2->slave) 
			{
				chan = dev2->priv;
				if (chan->tx_int_status == WAITING_TO_BE_ENABLED) 
				{
					dlci_interface = chan->dlci_int_interface;
					dlci_interface->gen_interrupt |= 0x40;
					dlci_interface->packet_length = chan->pkt_length;
					chan->tx_int_status = DISABLED;
				}
			}
		}
		card->wandev.enable_tx_int = 0;
		flags->imask |= 0x02;
		++card->statistics.isr_enable_tx_int;
	}
	save_flags(host_cpu_flags);
	cli();
	card->in_isr = 0;
	card->wandev.critical = 0xD1;
	flags->iflag = 0;
	card->wandev.critical = 0;
	restore_flags(host_cpu_flags);
	/* Device is now ready to send. The instant this is executed the If_Send
	   routine is called. That is why this is put at the bottom of the ISR
	   to prevent a endless loop condition caused by repeated Interrupts and
	   enable_tx_int flag.
	 */
	if (card->dlci_int_mode_unbusy)
		mark_bh(NET_BH);
	if (card->buff_int_mode_unbusy) 
	{
		for (;;) 
		{
			if (((fr_channel_t *) ((card->devs_struct)->dev_ptr)->priv)->dev_pending_devtint == 1) 
			{
				((fr_channel_t *) ((card->devs_struct)->dev_ptr)->priv)->dev_pending_devtint = 0;
				mark_bh(NET_BH);
			}
			if ((card->devs_struct)->next == card->dev_to_devtint_next)
				break;
			card->devs_struct = (card->devs_struct)->next;
		}
		card->devs_struct = (card->dev_to_devtint_next)->next;
		card->dev_to_devtint_next = card->devs_struct;
	}
}
/*============================================================================
 * Receive interrupt handler.
 */

static void fr502_rx_intr(sdla_t * card)
{
	fr_mbox_t *mbox = card->rxmb;
	struct sk_buff *skb;
	struct device *dev;
	fr_channel_t *chan;
	unsigned dlci, len;
	void *buf;
	unsigned char *sendpacket;
	unsigned char buf2[3];
	int udp_type;
	sdla_mapmem(&card->hw, FR502_RX_VECTOR);
	dlci = mbox->cmd.dlci;
	len = mbox->cmd.length;
	/* Find network interface for this packet */
	dev = find_channel(card, dlci);
	if (dev == NULL) 
	{
		/* Invalid channel, discard packet */
		printk(KERN_INFO "%s: receiving on orphaned DLCI %d!\n",
		       card->devname, dlci);
		sdla_mapmem(&card->hw, FR_MB_VECTOR);
	}
	chan = dev->priv;
	if (!dev->start) 
	{
		++chan->ifstats.rx_dropped;
		sdla_mapmem(&card->hw, FR_MB_VECTOR);
	}
	/* Allocate socket buffer */
	skb = dev_alloc_skb(len);
	if (skb == NULL) 
	{
		printk(KERN_INFO "%s: no socket buffers available!\n",
		       card->devname);
		++chan->ifstats.rx_dropped;
		sdla_mapmem(&card->hw, FR_MB_VECTOR);
	}
	/* Copy data to the socket buffer */
	buf = skb_put(skb, len);
	memcpy(buf, mbox->data, len);
	sdla_mapmem(&card->hw, FR_MB_VECTOR);
	/* Check if it's a UDP management packet */
	sendpacket = skb->data;
	memcpy(&buf2, &card->wandev.udp_port, 2);
	udp_type = udp_pkt_type(skb, card);
	if ((udp_type == UDP_FPIPE_TYPE) || (udp_type == UDP_DRVSTATS_TYPE)) 
	{
		if (udp_type == UDP_DRVSTATS_TYPE) 
		{
			++chan->rx_intr_DRVSTATS_request;
			process_udp_driver_call(UDP_PKT_FRM_NETWORK, card, skb,
						dev, dlci, chan);
		}
		else
		{
			++chan->rx_intr_FPIPE_request;
			process_udp_mgmt_pkt(UDP_PKT_FRM_NETWORK, card, skb,
					     dev, dlci, chan);
		}
	}
	else
	{
		/* Decapsulate packet and pass it up the protocol stack */
		skb->dev = dev;
		buf = skb_pull(skb, 1);		/* remove hardware header */
		if (!wanrouter_type_trans(skb, dev)) 
		{
			/* can't decapsulate packet */
			dev_kfree_skb(skb);
			++chan->ifstats.rx_errors;
			++card->wandev.stats.rx_errors;
		}
		else 
		{
			netif_rx(skb);
			++chan->ifstats.rx_packets;
			++card->wandev.stats.rx_packets;
			chan->ifstats.rx_bytes += skb->len;
			card->wandev.stats.rx_bytes += skb->len;
		}
	}
	sdla_mapmem(&card->hw, FR_MB_VECTOR);
}
/*============================================================================
 * Receive interrupt handler.
 */

static void fr508_rx_intr(sdla_t * card)
{
	fr_buf_ctl_t *frbuf = card->rxmb;
	struct sk_buff *skb;
	struct device *dev;
	fr_channel_t *chan;
	unsigned dlci, len, offs;
	void *buf;
	unsigned rx_count = 0;
	fr508_flags_t *flags = card->flags;
	char *ptr = &flags->iflag;
	int i, err, udp_type;
	if (frbuf->flag != 0x01) 
	{
		printk(KERN_INFO
		       "%s: corrupted Rx buffer @ 0x%X, flag = 0x%02X!\n",
		       card->devname, (unsigned) frbuf, frbuf->flag);
		printk(KERN_INFO "%s: ID Bytes = ", card->devname);
		for (i = 0; i < 8; i++)
			printk(KERN_INFO "0x%02X ", *(ptr + 0x28 + i));
		printk(KERN_INFO "\n");
		++card->statistics.rx_intr_corrupt_rx_bfr;
		return;
	}
	
	do 
	{
		len = frbuf->length;
		dlci = frbuf->dlci;
		offs = frbuf->offset;
		/* Find network interface for this packet */
		dev = find_channel(card, dlci);
		chan = dev->priv;
		if (dev == NULL) 
		{
			/* Invalid channel, discard packet */
			printk(KERN_INFO "%s: receiving on orphaned DLCI %d!\n"
			       ,card->devname, dlci);
			++card->statistics.rx_intr_on_orphaned_DLCI;
		}
		else
		{
			skb = dev_alloc_skb(len);
			if (!dev->start || (skb == NULL)) 
			{
				++chan->ifstats.rx_dropped;
				if (dev->start) 
				{
					printk(KERN_INFO
					       "%s: no socket buffers available!\n",
					       card->devname);
					++chan->rx_intr_no_socket;
				} else
					++chan->rx_intr_dev_not_started;
			}
			else
			{
				/* Copy data to the socket buffer */
				if ((offs + len) > card->u.f.rx_top + 1) 
				{
					unsigned tmp = card->u.f.rx_top - offs + 1;
					buf = skb_put(skb, tmp);
					sdla_peek(&card->hw, offs, buf, tmp);
					offs = card->u.f.rx_base;
					len -= tmp;
				}
				buf = skb_put(skb, len);
				sdla_peek(&card->hw, offs, buf, len);
				udp_type = udp_pkt_type(skb, card);
				if (udp_type == UDP_DRVSTATS_TYPE) 
				{
					++chan->rx_intr_DRVSTATS_request;
					process_udp_driver_call(
					  UDP_PKT_FRM_NETWORK, card, skb,
							dev, dlci, chan);
				}
				else if (udp_type == UDP_FPIPE_TYPE) 
				{
					++chan->rx_intr_FPIPE_request;
					err = process_udp_mgmt_pkt(
					       UDP_PKT_FRM_NETWORK, card,
						   skb, dev, dlci, chan);
				}
				else if (handle_IPXWAN(skb->data, card->devname, card->wandev.enable_IPX, card->wandev.network_number)) 
				{
					if (card->wandev.enable_IPX) 
						fr508_send(card, dlci, 0, skb->len, skb->data);
				} 
				else
				{
					/* Decapsulate packet and pass it up the
					   protocol stack */
					skb->dev = dev;
					/* remove hardware header */
					buf = skb_pull(skb, 1);
					if (!wanrouter_type_trans(skb, dev)) 
					{
						/* can't decapsulate packet */
						dev_kfree_skb(skb);
						++chan->
						    rx_intr_bfr_not_passed_to_stack;
						++chan->
						    ifstats.rx_errors;
						++card->
						    wandev.stats.rx_errors;
					}
					else
					{
						netif_rx(skb);
						++chan->rx_intr_bfr_passed_to_stack;
						++chan->ifstats.rx_packets;
						++card->wandev.stats.rx_packets;
						chan->ifstats.rx_bytes += skb->len;
						card->wandev.stats.rx_bytes += skb->len;
					}
				}
			}
		}
		/* Release buffer element and calculate a pointer to the next 
		   one */
		frbuf->flag = 0;
		card->rxmb = ++frbuf;
		if ((void *) frbuf > card->u.f.rxmb_last)
			card->rxmb = card->u.f.rxmb_base;
		/* The loop put in is temporary, that is why the break is
		 * placed here. (?????)
		 */
		break;
		frbuf = card->rxmb;
	}
	while (frbuf->flag && ((++rx_count) < 4));
}
/*============================================================================
 * Transmit interrupt handler.
 * o print a warning
 * o 
 * If number of spurious interrupts exceeded some limit, then ???
 */
static void tx_intr(sdla_t * card)
{
	struct device *dev = card->wandev.dev;
	if (card->intr_mode == BUFFER_INTR_MODE) 
	{
		for (; dev; dev = dev->slave) 
		{
			if (!dev || !dev->start) 
			{
				++card->statistics.tx_intr_dev_not_started;
				continue;
			}
			dev->tbusy = 0;
			mark_bh(NET_BH);
		}
	}
	else
	{
		dev->tbusy = 0;
		mark_bh(NET_BH);
	}
}

/*============================================================================
 * Spurious interrupt handler.
 * o print a warning
 * o 
 * If number of spurious interrupts exceeded some limit, then ???
 */

static void spur_intr(sdla_t * card)
{
	printk(KERN_INFO "%s: spurious interrupt!\n", card->devname);
}

/*
   Return 0 for non-IPXWAN packet
   1 for IPXWAN packet or IPX is not enabled!
 */

static int handle_IPXWAN(unsigned char *sendpacket, char *devname, unsigned char enable_IPX, unsigned long network_number)
{
	int i;
	if (sendpacket[1] == 0x00 &&
	    sendpacket[2] == 0x80 &&
	    sendpacket[6] == 0x81 &&
	    sendpacket[7] == 0x37) 
	{
		/* It's an IPX packet */
		if (!enable_IPX) {
			/* Return 1 so we don't pass it up the stack. */
			return 1;
		}
	}
	else
	{
		/* It's not IPX so return and pass it up the stack. */
		return 0;
	}
	if (sendpacket[24] == 0x90 &&
	    sendpacket[25] == 0x04) 
	{
		/* It's IPXWAN */
		if (sendpacket[10] == 0x02 &&
		    sendpacket[42] == 0x00) 
		{
			/* It's a timer request packet */
			printk(KERN_INFO "%s: Received IPXWAN Timer Request packet\n", devname);
			/* Go through the routing options and answer no to every */
			/* option except Unnumbered RIP/SAP */
			for (i = 49; sendpacket[i] == 0x00; i += 5) 
			{
				/* 0x02 is the option for Unnumbered RIP/SAP */
				if (sendpacket[i + 4] != 0x02) 
				{
					sendpacket[i + 1] = 0;
				}
			}
			/* Skip over the extended Node ID option */
			if (sendpacket[i] == 0x04) 
				i += 8;
			/* We also want to turn off all header compression opt. */
			for (; sendpacket[i] == 0x80;) 
			{
				sendpacket[i + 1] = 0;
				i += (sendpacket[i + 2] << 8) + (sendpacket[i + 3]) + 4;
			}
			/* Set the packet type to timer response */
			sendpacket[42] = 0x01;
			printk(KERN_INFO "%s: Sending IPXWAN Timer Response\n", devname);
		}
		else if (sendpacket[42] == 0x02) 
		{
			/* This is an information request packet */
			printk(KERN_INFO "%s: Received IPXWAN Information Request packet\n", devname);
			/* Set the packet type to information response */
			sendpacket[42] = 0x03;
			/* Set the router name */
			sendpacket[59] = 'F';
			sendpacket[60] = 'P';
			sendpacket[61] = 'I';
			sendpacket[62] = 'P';
			sendpacket[63] = 'E';
			sendpacket[64] = '-';
			sendpacket[65] = CVHexToAscii(network_number >> 28);
			sendpacket[66] = CVHexToAscii((network_number & 0x0F000000) >> 24);
			sendpacket[67] = CVHexToAscii((network_number & 0x00F00000) >> 20);
			sendpacket[68] = CVHexToAscii((network_number & 0x000F0000) >> 16);
			sendpacket[69] = CVHexToAscii((network_number & 0x0000F000) >> 12);
			sendpacket[70] = CVHexToAscii((network_number & 0x00000F00) >> 8);
			sendpacket[71] = CVHexToAscii((network_number & 0x000000F0) >> 4);
			sendpacket[72] = CVHexToAscii(network_number & 0x0000000F);
			for (i = 73; i < 107; i += 1) 
				sendpacket[i] = 0;
			printk(KERN_INFO "%s: Sending IPXWAN Information Response packet\n", devname);
		}
		else
		{
			printk(KERN_INFO "%s: Unknown IPXWAN packet!\n", devname);
			return 0;
		}
		/* Set the WNodeID to our network address */
		sendpacket[43] = (unsigned char) (network_number >> 24);
		sendpacket[44] = (unsigned char) ((network_number & 0x00FF0000) >> 16);
		sendpacket[45] = (unsigned char) ((network_number & 0x0000FF00) >> 8);
		sendpacket[46] = (unsigned char) (network_number & 0x000000FF);
		return 1;
	}
	/* If we get here, its an IPX-data packet so it'll get passed up the stack. */
	/* switch the network numbers */
	switch_net_numbers(sendpacket, network_number, 1);
	return 0;
}

/****** Background Polling Routines  ****************************************/

/*============================================================================
 * Main polling routine.
 * This routine is repeatedly called by the WANPIPE 'thead' to allow for
 * time-dependent housekeeping work.
 *
 * o fetch asynchronous network events.
 *
 * Notes:
 * 1. This routine may be called on interrupt context with all interrupts
 *    enabled. Beware!
 */

static void wpf_poll(sdla_t * card)
{
/*      struct device* dev = card->wandev.dev;  */
	fr508_flags_t *flags = card->flags;
	unsigned long host_cpu_flags;
	++card->statistics.poll_entry;
	if (((jiffies - card->state_tick) < HZ) ||
	    (card->intr_mode == INTR_TEST_MODE))
		return;
	disable_irq(card->hw.irq);
	++card->irq_dis_poll_count;
	if (test_and_set_bit(0, (void *) &card->wandev.critical)) 
	{
		++card->statistics.poll_already_critical;
		save_flags(host_cpu_flags);
		cli();
		if ((!card->irq_dis_if_send_count) &&
		    (!(--card->irq_dis_poll_count)))
			enable_irq(card->hw.irq);
		restore_flags(host_cpu_flags);
		return;
	}
	card->wandev.critical = 0x11;
	++card->statistics.poll_processed;
	/* This is to be changed later ??? */
	/*
	   if( dev && dev->tbusy && !(flags->imask & 0x02) ) {
	   printk(KERN_INFO "%s: Wpf_Poll: tbusy = 0x01, imask = 0x%02X\n",             card->devname, flags->imask);
	   }
	 */
	if (flags->event) 
	{
		fr_mbox_t *mbox = card->mbox;
		int err;
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		mbox->cmd.command = FR_READ_STATUS;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
		if (err)
			fr_event(card, err, mbox);
	}
	card->wandev.critical = 0;
	save_flags(host_cpu_flags);
	cli();
	if ((!card->irq_dis_if_send_count) && (!(--card->irq_dis_poll_count)))
		enable_irq(card->hw.irq);
	restore_flags(host_cpu_flags);
	card->state_tick = jiffies;
}

/****** Frame Relay Firmware-Specific Functions *****************************/

/*============================================================================
 * Read firmware code version.
 * o fill string str with firmware version info. 
 */

static int fr_read_version(sdla_t * card, char *str)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	do 
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		mbox->cmd.command = FR_READ_CODE_VERSION;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));
	
	if (!err && str) 
	{
		int len = mbox->cmd.length;
		memcpy(str, mbox->data, len);
		str[len] = '\0';
	}
	return err;
}
/*============================================================================
 * Set global configuration.
 */
 
static int fr_configure(sdla_t * card, fr_conf_t * conf)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int dlci_num = card->u.f.dlci_num;
	int err, i;
	do 
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		memcpy(mbox->data, conf, sizeof(fr_conf_t));
		if (dlci_num)
			for (i = 0; i < dlci_num; ++i)
				((fr_conf_t *) mbox->data)->dlci[i] =
				    card->u.f.node_dlci[i];
		mbox->cmd.command = FR_SET_CONFIG;
		mbox->cmd.length =
		    sizeof(fr_conf_t) + dlci_num * sizeof(short);
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}
/*============================================================================
 * Set DLCI configuration.
 */
static int fr_dlci_configure(sdla_t * card, fr_dlc_conf_t * conf, unsigned dlci)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	do
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		memcpy(mbox->data, conf, sizeof(fr_dlc_conf_t));
		mbox->cmd.dlci = (unsigned short) dlci;
		mbox->cmd.command = FR_SET_CONFIG;
		mbox->cmd.length = 0x0E;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry--);
	return err;
}
/*============================================================================
 * Set interrupt mode.
 */
static int fr_set_intr_mode(sdla_t * card, unsigned mode, unsigned mtu)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	do
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		if (card->hw.fwid == SFID_FR502) 
		{
			fr502_intr_ctl_t *ictl = (void *) mbox->data;
			memset(ictl, 0, sizeof(fr502_intr_ctl_t));
			ictl->mode = mode;
			ictl->tx_len = mtu;
			mbox->cmd.length = sizeof(fr502_intr_ctl_t);
		}
		else
		{
			fr508_intr_ctl_t *ictl = (void *) mbox->data;
			memset(ictl, 0, sizeof(fr508_intr_ctl_t));
			ictl->mode = mode;
			ictl->tx_len = mtu;
			ictl->irq = card->hw.irq;
			mbox->cmd.length = sizeof(fr508_intr_ctl_t);
		}
		mbox->cmd.command = FR_SET_INTR_MODE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}
/*============================================================================
 * Enable communications.
 */
static int fr_comm_enable(sdla_t * card)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	do 
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		mbox->cmd.command = FR_COMM_ENABLE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}
/*============================================================================
 * Disable communications. 
 */
static int fr_comm_disable(sdla_t * card)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	do
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		mbox->cmd.command = FR_COMM_DISABLE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}
/*============================================================================
 * Get communications error statistics. 
 */
static int fr_get_err_stats(sdla_t * card)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	
	do
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		mbox->cmd.command = FR_READ_ERROR_STATS;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	if (!err) 
	{
		fr_comm_stat_t *stats = (void *) mbox->data;
		card->wandev.stats.rx_over_errors = stats->rx_overruns;
		card->wandev.stats.rx_crc_errors = stats->rx_bad_crc;
		card->wandev.stats.rx_missed_errors = stats->rx_aborts;
		card->wandev.stats.rx_length_errors = stats->rx_too_long;
		card->wandev.stats.tx_aborted_errors = stats->tx_aborts;
	}
	return err;
}
/*============================================================================
 * Get statistics. 
 */
static int fr_get_stats(sdla_t * card)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	do 
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		mbox->cmd.command = FR_READ_STATISTICS;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	if (!err) 
	{
		fr_link_stat_t *stats = (void *) mbox->data;
		card->wandev.stats.rx_frame_errors = stats->rx_bad_format;
		card->wandev.stats.rx_dropped = stats->rx_dropped + stats->rx_dropped2;
	}
	return err;
}
/*============================================================================
 * Add DLCI(s) (Access Node only!).
 * This routine will perform the ADD_DLCIs command for the specified DLCI.
 */
static int fr_add_dlci(sdla_t * card, int dlci, int num)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err, i;
	do 
	{
		unsigned short *dlci_list = (void *) mbox->data;
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		for (i = 0; i < num; ++i)
			dlci_list[i] = card->u.f.node_dlci[i];
		mbox->cmd.length = num * sizeof(short);
		mbox->cmd.command = FR_ADD_DLCI;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}
/*============================================================================
 * Activate DLCI(s) (Access Node only!). 
 * This routine will perform the ACTIVATE_DLCIs command with a list of DLCIs. 
 */
static int fr_activate_dlci(sdla_t * card, int dlci, int num)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err, i;
	do
	{
		unsigned short *dlci_list = (void *) mbox->data;
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		for (i = 0; i < num; ++i)
			dlci_list[i] = card->u.f.node_dlci[i];
		mbox->cmd.length = num * sizeof(short);
		mbox->cmd.command = FR_ACTIVATE_DLCI;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}
/*============================================================================
 * Issue in-channel signalling frame. 
 */
static int fr_issue_isf(sdla_t * card, int isf)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	do
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		mbox->data[0] = isf;
		mbox->cmd.length = 1;
		mbox->cmd.command = FR_ISSUE_IS_FRAME;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}
/*============================================================================
 * Send a frame (S502 version). 
 */
static int fr502_send(sdla_t * card, int dlci, int attr, int len, void *buf)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	
	do 
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		memcpy(mbox->data, buf, len);
		mbox->cmd.dlci = dlci;
		mbox->cmd.attr = attr;
		mbox->cmd.length = len;
		mbox->cmd.command = FR_WRITE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}
/*============================================================================
 * Send a frame (S508 version). 
 */
static int fr508_send(sdla_t * card, int dlci, int attr, int len, void *buf)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	
	do
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		mbox->cmd.dlci = dlci;
		mbox->cmd.attr = attr;
		mbox->cmd.length = len;
		mbox->cmd.command = FR_WRITE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	if (!err) 
	{
		fr_buf_ctl_t *frbuf = (void *) (*(unsigned long *) mbox->data -
					FR_MB_VECTOR + card->hw.dpmbase);
		sdla_poke(&card->hw, frbuf->offset, buf, len);
		frbuf->flag = 0x01;
	}
	return err;
}

/****** Firmware Asynchronous Event Handlers ********************************/

/*============================================================================
 * Main asynchronous event/error handler.
 *	This routine is called whenever firmware command returns non-zero
 *	return code.
 *
 * Return zero if previous command has to be cancelled.
 */

static int fr_event(sdla_t * card, int event, fr_mbox_t * mbox)
{
	fr508_flags_t *flags = card->flags;
	char *ptr = &flags->iflag;
	int i;
	switch (event) 
	{
		case FRRES_MODEM_FAILURE:
			return fr_modem_failure(card, mbox);
		case FRRES_CHANNEL_DOWN:
			wanpipe_set_state(card, WAN_DISCONNECTED);
			return 1;
		case FRRES_CHANNEL_UP:
			wanpipe_set_state(card, WAN_CONNECTED);
			return 1;
		case FRRES_DLCI_CHANGE:
			return fr_dlci_change(card, mbox);
		case FRRES_DLCI_MISMATCH:
			printk(KERN_INFO "%s: DLCI list mismatch!\n",
			       card->devname);
			return 1;
		case CMD_TIMEOUT:
			printk(KERN_ERR "%s: command 0x%02X timed out!\n",
			       card->devname, mbox->cmd.command);
			printk(KERN_INFO "%s: ID Bytes = ", card->devname);
			for (i = 0; i < 8; i++)
				printk(KERN_INFO "0x%02X ", *(ptr + 0x28 + i));
			printk(KERN_INFO "\n");
			break;
		case FRRES_DLCI_INACTIVE:
			printk(KERN_ERR "%s: DLCI %u is inactive!\n",
			       card->devname, mbox->cmd.dlci);
			break;
		case FRRES_CIR_OVERFLOW:
			break;
		case FRRES_BUFFER_OVERFLOW:
			break;
		default:
			printk(KERN_INFO "%s: command 0x%02X returned 0x%02X!\n"
			       ,card->devname, mbox->cmd.command, event);
	}
	return 0;
}

/*============================================================================
 * Handle modem error.
 *
 * Return zero if previous command has to be cancelled.
 */
static int fr_modem_failure(sdla_t * card, fr_mbox_t * mbox)
{
	printk(KERN_INFO "%s: physical link down! (modem error 0x%02X)\n",
	       card->devname, mbox->data[0]);
	switch (mbox->cmd.command) 
	{
		case FR_WRITE:
		case FR_READ:
			return 0;
	}
	return 1;
}
/*============================================================================
 * Handle DLCI status change.
 *
 * Return zero if previous command has to be cancelled.
 */
static int fr_dlci_change(sdla_t * card, fr_mbox_t * mbox)
{
	dlci_status_t *status = (void *) mbox->data;
	int cnt = mbox->cmd.length / sizeof(dlci_status_t);
	fr_dlc_conf_t cfg;
	fr_channel_t *chan;
	struct device *dev2;
	for (; cnt; --cnt, ++status) 
	{
		unsigned short dlci = status->dlci;
		struct device *dev = find_channel(card, dlci);
		if (dev == NULL) 
		{
			printk(KERN_INFO
			       "%s: CPE contains unconfigured DLCI= %d\n",
			       card->devname, dlci);
		}
		else 
		{
			if (status->state & 0x01) 
			{
				printk(KERN_INFO
				       "%s: DLCI %u has been deleted!\n",
				       card->devname, dlci);
				if (dev && dev->start)
					set_chan_state(dev, WAN_DISCONNECTED);
			}
			else if (status->state & 0x02) 
			{
				printk(KERN_INFO
				       "%s: DLCI %u becomes active!\n",
				       card->devname, dlci);
				chan = dev->priv;
				/* This flag is used for configuring specific 
				   DLCI(s) when they become active.
				 */
				chan->dlci_configured = DLCI_CONFIG_PENDING;
				if (dev && dev->start)
					set_chan_state(dev, WAN_CONNECTED);
			}
		}
	}
	for (dev2 = card->wandev.dev; dev2; dev2 = dev2->slave) 
	{
		chan = dev2->priv;
		if (chan->dlci_configured == DLCI_CONFIG_PENDING) 
		{
			memset(&cfg, 0, sizeof(cfg));
			if (chan->cir_status == CIR_DISABLED) 
			{
				cfg.cir_fwd = cfg.cir_bwd = 16;
				cfg.bc_fwd = cfg.bc_bwd = 16;
				cfg.conf_flags = 0x0001;
				printk(KERN_INFO "%s: CIR Disabled for %s\n",
				       card->devname, chan->name);
			} else if (chan->cir_status == CIR_ENABLED) {
				cfg.cir_fwd = cfg.cir_bwd = chan->cir;
				cfg.bc_fwd = cfg.bc_bwd = chan->bc;
				cfg.be_fwd = cfg.be_bwd = chan->be;
				cfg.conf_flags = 0x0000;
				printk(KERN_INFO "%s: CIR Enabled for %s\n",
				       card->devname, chan->name);
			}
			if (fr_dlci_configure(card, &cfg, chan->dlci)) 
			{
				printk(KERN_INFO
				    "%s: DLCI Configure failed for %d\n",
				       card->devname, chan->dlci);
				return 1;
			}
			chan->dlci_configured = DLCI_CONFIGURED;
			/* Read the interface byte mapping into the channel 
			   structure.
			 */
			if (card->intr_mode == DLCI_LIST_INTR_MODE)
				read_DLCI_IB_mapping(card, chan);
		}
	}
	return 1;
}
/******* Miscellaneous ******************************************************/

/*============================================================================
 * Update channel state. 
 */
static int update_chan_state(struct device *dev)
{
	fr_channel_t *chan = dev->priv;
	sdla_t *card = chan->card;
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;
	int dlci_found = 0;

	do 
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		mbox->cmd.command = FR_LIST_ACTIVE_DLCI;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));

	if (!err) 
	{
		unsigned short *list = (void *) mbox->data;
		int cnt = mbox->cmd.length / sizeof(short);
		for (; cnt; --cnt, ++list) 
		{
			if (*list == chan->dlci) 
			{
				dlci_found = 1;
				set_chan_state(dev, WAN_CONNECTED);
				break;
			}
		}
		if (!dlci_found)
			printk(KERN_INFO "%s: DLCI %u is inactive\n",
			       card->devname, chan->dlci);
	}
	
	return err;
}
/*============================================================================
 * Set channel state.
 */
static void set_chan_state(struct device *dev, int state)
{
	fr_channel_t *chan = dev->priv;
	sdla_t *card = chan->card;
	unsigned long flags;
	
	save_flags(flags);
	cli();
	
	if (chan->state != state) 
	{
		switch (state) 
		{
			case WAN_CONNECTED:
				printk(KERN_INFO "%s: interface %s connected!\n"
				       ,card->devname, dev->name);
				break;
			case WAN_CONNECTING:
				printk(KERN_INFO
				       "%s: interface %s connecting...\n",
				       card->devname, dev->name);
				break;
			case WAN_DISCONNECTED:
				printk(KERN_INFO
				       "%s: interface %s disconnected!\n",
				       card->devname, dev->name);
				break;
		}
		chan->state = state;
	}
	chan->state_tick = jiffies;
	restore_flags(flags);
}

/*============================================================================
 * Find network device by its channel number.
 */
static struct device *find_channel(sdla_t * card, unsigned dlci)
{
	struct device *dev;
	for (dev = card->wandev.dev; dev; dev = dev->slave)
		if (((fr_channel_t *) dev->priv)->dlci == dlci)
			break;
	return dev;
}
/*============================================================================
 * Check to see if a frame can be sent. If no transmit buffers available,
 * enable transmit interrupts.
 *
 * Return:	1 - Tx buffer(s) available
 *		0 - no buffers available
 */

static int is_tx_ready(sdla_t * card, fr_channel_t * chan)
{
	if (card->hw.fwid == SFID_FR508) 
	{
		unsigned char sb = inb(card->hw.port);
		if (sb & 0x02)
			return 1;
	}
	else 
	{
		fr502_flags_t *flags = card->flags;
		if (flags->tx_ready)
			return 1;
		flags->imask |= 0x02;
	}
	return 0;
}

/*============================================================================
 * Convert decimal string to unsigned integer.
 * If len != 0 then only 'len' characters of the string are converted.
 */
static unsigned int dec_to_uint(unsigned char *str, int len)
{
	unsigned val;
	if (!len)
		len = strlen(str);
	for (val = 0; len && is_digit(*str); ++str, --len)
		val = (val * 10) + (*str - (unsigned) '0');
	return val;
}

/*==============================================================================
 * Process UDP call of type FPIPE8ND
 */

static int process_udp_mgmt_pkt(char udp_pkt_src, sdla_t * card, struct sk_buff *skb, struct device *dev, int dlci, fr_channel_t * chan)
{
	int c_retry = MAX_CMD_RETRY;
	unsigned char *data;
	unsigned char *buf;
	unsigned char buf2[5];
	unsigned int loops, frames, len;
	unsigned long data_ptr;
	unsigned short real_len, buffer_length;
	struct sk_buff *new_skb;
	unsigned char *sendpacket;
	fr_mbox_t *mbox = card->mbox;
	int err;
	struct timeval tv;
	int udp_mgmt_req_valid = 1;
	sendpacket = skb->data;
	memcpy(&buf2, &card->wandev.udp_port, 2);
	if ((data = kmalloc(2000, GFP_ATOMIC)) == NULL) 
	{
		printk(KERN_INFO
		       "%s: Error allocating memory for UDP management cmnd 0x%02X",
		       card->devname, data[47]);
		++chan->UDP_FPIPE_mgmt_kmalloc_err;
		return 1;
	}
	memcpy(data, sendpacket, skb->len);
	switch (data[47]) 
	{
		/* FPIPE_ENABLE_TRACE */
		case 0x41:
		/* FPIPE_DISABLE_TRACE */
		case 0x42:
		/* FPIPE_GET_TRACE_INFO */
		case 0x43:
		/* SET FT1 MODE */
		case 0x81:
			if (udp_pkt_src == UDP_PKT_FRM_NETWORK) 
			{
				++chan->UDP_FPIPE_mgmt_direction_err;
				udp_mgmt_req_valid = 0;
				break;
			}
		/* FPIPE_FT1_READ_STATUS */
		case 0x44:
		/* FT1 MONITOR STATUS */
		case 0x80:
			if (card->hw.fwid != SFID_FR508) 
			{
				++chan->UDP_FPIPE_mgmt_adptr_type_err;
				udp_mgmt_req_valid = 0;
			}
			break;
		default:
			break;
	}
	if (!udp_mgmt_req_valid) 
	{
		/* set length to 0 */
		data[48] = data[49] = 0;
		/* set return code */
		data[50] = (card->hw.fwid != SFID_FR508) ? 0x1F : 0xCD;
	}
	else
	{
		switch (data[47]) 
		{
			/* FPIPE_ENABLE_TRACE */
			case 0x41:
				if (!TracingEnabled) 
				{
					do 
					{
						/* SET_TRACE_CONFIGURATION */
						mbox->cmd.command = 0x60;
						mbox->cmd.length = 1;
						mbox->cmd.dlci = 0x00;
						mbox->data[0] = 0x37;
						err = sdla_exec(mbox) ?
							mbox->cmd.result : CMD_TIMEOUT;
					}
					while (err && c_retry-- && fr_event(card, err, mbox));
	
					if (err) 
					{
						TracingEnabled = 0;
						/* set the return code */
						data[50] = mbox->cmd.result;
						mbox->cmd.length = 0;
						break;
					}
					/* get num_frames */
					sdla_peek(&card->hw, 0x9000, &num_frames, 2);
					sdla_peek(&card->hw, 0x9002, &curr_trace_addr,4);
					start_trace_addr = curr_trace_addr;
					/* MAX_SEND_BUFFER_SIZE - 
					 * sizeof(UDP_MGMT_PACKET) - 41 */
					available_buffer_space = 1926;
					/* set return code */
					data[50] = 0;
				} 
				else
				{
					/* set return code to line trace already 
					   enabled */
					data[50] = 1;
				}
				mbox->cmd.length = 0;
				TracingEnabled = 1;
				break;
			/* FPIPE_DISABLE_TRACE */
			case 0x42:
				if (TracingEnabled) 
				{
					do 
					{
						/* SET_TRACE_CONFIGURATION */
						mbox->cmd.command = 0x60;
						mbox->cmd.length = 1;
						mbox->cmd.dlci = 0x00;
						mbox->data[0] = 0x36;
						err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
					}
					while (err && c_retry-- && fr_event(card, err, mbox));
				}
				/* set return code */
				data[50] = 0;
				mbox->cmd.length = 0;
				TracingEnabled = 0;
				break;
			/* FPIPE_GET_TRACE_INFO */
			case 0x43:
				/* Line trace cannot be performed on the 502 */
				if (!TracingEnabled) 
				{
					/* set return code */
					data[50] = 1;
					mbox->cmd.length = 0;
					break;
				}
				buffer_length = 0;
				loops = (num_frames < 20) ? num_frames : 20;
				for (frames = 0; frames < loops; frames += 1) 
				{
					sdla_peek(&card->hw, curr_trace_addr, &buf2, 1);
					/* no data on board so exit */
					if (buf2[0] == 0x00)
						break;
					/* 1+sizeof(FRAME_DATA) = 9 */
					if ((available_buffer_space - buffer_length) < 9) 
					{
						/* indicate we have more frames on board
						   and exit */
						data[62] |= 0x02;
						break;
					}
					/* get frame status */
					sdla_peek(&card->hw, curr_trace_addr + 0x05, &data[62 + buffer_length], 1);
					/* get time stamp */
					sdla_peek(&card->hw, curr_trace_addr + 0x06, &data[66 + buffer_length], 2);
					/* get frame length */
					sdla_peek(&card->hw, curr_trace_addr + 0x01, &data[64 + buffer_length], 2);
					/* get pointer to real data */
					sdla_peek(&card->hw, curr_trace_addr + 0x0C,&data_ptr, 4);
					/* see if we can fit the frame into the user buffer */
					memcpy(&real_len, &data[64 + buffer_length], 2);
					if (data_ptr == 0 || real_len + 8 > available_buffer_space) 
					{
						data[63 + buffer_length] = 0x00;
					}
					else
					{
						/* we can take it next time */
						if (available_buffer_space - buffer_length < real_len + 8) 
						{
							data[62] |= 0x02;
							break;
						}
						/* ok, get the frame */
						data[63 + buffer_length] = 0x01;
						/* get the data */
						sdla_peek(&card->hw, data_ptr, &data[68 + buffer_length], real_len);
						/* zero the opp flag to show we got the frame */
						buf2[0] = 0x00;
						sdla_poke(&card->hw, curr_trace_addr, &buf2, 1);
						/* now move onto the next frame */
						curr_trace_addr += 16;
						/* check if we passed the last address */
						if (curr_trace_addr >= (start_trace_addr + num_frames * 16)) 
							curr_trace_addr = start_trace_addr;
						/* update buffer length and make sure 
						   its even */
						if (data[63 + buffer_length] == 0x01) 
							buffer_length += real_len - 1;
						/* for the header */
						buffer_length += 8;
						if (buffer_length & 0x0001)
							buffer_length += 1;
					}
				}
				/* ok now set the total number of frames passed in the 
				   high 5 bits */
				data[62] = (frames << 3) | data[62];
				/* set the data length */
				mbox->cmd.length = buffer_length;
				memcpy(&data[48], &buffer_length, 2);
				data[50] = 0;
				break;
			/* FPIPE_FT1_READ_STATUS */
			case 0x44:
				sdla_peek(&card->hw, 0xF020, &data[62], 2);
				data[48] = 2;
				data[49] = 0;
				data[50] = 0;
				mbox->cmd.length = 2;
				break;
			/* FPIPE_FLUSH_DRIVER_STATS */
			case 0x48:
				init_chan_statistics(chan);
				init_global_statistics(card);
				mbox->cmd.length = 0;
				break;
			case 0x49:
				do_gettimeofday(&tv);
				chan->router_up_time = tv.tv_sec - chan->router_start_time;
				*(unsigned long *) &data[62] = chan->router_up_time;
				mbox->cmd.length = 4;
				break;
			/* FPIPE_KILL_BOARD */
			case 0x50:
				break;
			/* FT1 MONITOR STATUS */
			case 0x80:
				if (data[62] == 1) 
				{
					if (rCount++ != 0) 
					{
						data[50] = 0;
						mbox->cmd.length = 1;
						break;
					}
				}
				/* Disable FT1 MONITOR STATUS */
				if (data[62] == 0) 
				{
					if (--rCount != 0) 
					{
						data[50] = 0;
						mbox->cmd.length = 1;
						break;
					}
				}
			default:
				do 
				{
					memcpy(&mbox->cmd, &sendpacket[47], sizeof(fr_cmd_t));
					if (mbox->cmd.length) 
						memcpy(&mbox->data, &sendpacket[62],mbox->cmd.length);
					err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
				}
				while (err && c_retry-- && fr_event(card, err, mbox));
			
				if (!err) 
				{
					++chan->UDP_FPIPE_mgmt_adptr_cmnd_OK;
					memcpy(data, sendpacket, skb->len);
					memcpy(&data[47], &mbox->cmd, sizeof(fr_cmd_t));
					if (mbox->cmd.length) 
					{
						memcpy(&data[62], &mbox->data,mbox->cmd.length);
					}
				}
				else
				{
					++chan->UDP_FPIPE_mgmt_adptr_cmnd_timeout;
				}
			}
	}
	/* Fill UDP TTL */
	data[10] = card->wandev.ttl;
	len = reply_udp(data, mbox->cmd.length);
	if (udp_pkt_src == UDP_PKT_FRM_NETWORK) 
	{
		err = fr508_send(card, dlci, 0, len, data);
		if (err)
			++chan->UDP_FPIPE_mgmt_adptr_send_passed;
		else
			++chan->UDP_FPIPE_mgmt_adptr_send_failed;
		dev_kfree_skb(skb);
	}
	else 
	{
		/* Allocate socket buffer */
		if ((new_skb = dev_alloc_skb(len)) != NULL) 
		{
			/* copy data into new_skb */
			buf = skb_put(new_skb, len);
			memcpy(buf, data, len);
			/* Decapsulate packet and pass it up the protocol 
			   stack */
			new_skb->dev = dev;
			buf = skb_pull(new_skb, 1);	/* remove hardware header */
			if (!wanrouter_type_trans(new_skb, dev)) 
			{
				++chan->UDP_FPIPE_mgmt_not_passed_to_stack;
				/* can't decapsulate packet */
				dev_kfree_skb(new_skb);
			}
			else 
			{
				++chan->UDP_FPIPE_mgmt_passed_to_stack;
				netif_rx(new_skb);
			}
		}
		else 
		{
			++chan->UDP_FPIPE_mgmt_no_socket;
			printk(KERN_INFO
			       "%s: UDP mgmt cmnd, no socket buffers available!\n",
			       card->devname);
		}
	}
	kfree(data);
	return 0;
}
/*==============================================================================
 * Perform the Interrupt Test by running the READ_CODE_VERSION command MAX_INTR_
 * TEST_COUNTER times.
 */
 
static int intr_test(sdla_t * card)
{
	fr_mbox_t *mb = card->mbox;
	int err, i;
	/* The critical flag is unset here because we want to get into the
	   ISR without the flag already set. The If_open sets the flag.
	 */
	card->wandev.critical = 0;
	err = fr_set_intr_mode(card, 0x08, card->wandev.mtu);
	if (err == CMD_OK) 
	{
		for (i = 0; i < MAX_INTR_TEST_COUNTER; i++) 
		{
			/* Run command READ_CODE_VERSION */
			memset(&mb->cmd, 0, sizeof(fr_cmd_t));
			mb->cmd.length = 0;
			mb->cmd.command = 0x40;
			err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
			if (err != CMD_OK)
				fr_event(card, err, mb);
		}
	} 
	else 
	{
		return err;
	}
	err = fr_set_intr_mode(card, 0, card->wandev.mtu);
	if (err != CMD_OK)
		return err;
	card->wandev.critical = 1;
	return 0;
}
/*============================================================================
 * Process UDP call of type DRVSTATS.  
 */
static int process_udp_driver_call(char udp_pkt_src, sdla_t * card, struct sk_buff *skb, struct device *dev, int dlci, fr_channel_t * chan)
{
	int c_retry = MAX_CMD_RETRY;
	unsigned char *sendpacket;
	unsigned char buf2[5];
	unsigned char *data;
	unsigned char *buf;
	unsigned int len;
	fr_mbox_t *mbox = card->mbox;
	struct sk_buff *new_skb;
	int err;
	sendpacket = skb->data;
	memcpy(&buf2, &card->wandev.udp_port, 2);
	if ((data = kmalloc(2000, GFP_ATOMIC)) == NULL) 
	{
		printk(KERN_INFO
		       "%s: Error allocating memory for UDP DRIVER STATS cmnd0x%02X"
		       ,card->devname, data[45]);
		++chan->UDP_DRVSTATS_mgmt_kmalloc_err;
		return 1;
	}
	memcpy(data, sendpacket, skb->len);
	switch (data[47]) 
	{
		case 0x45:
			*(unsigned long *) &data[62] = chan->if_send_entry;
			*(unsigned long *) &data[66] = chan->if_send_skb_null;
			*(unsigned long *) &data[70] = chan->if_send_broadcast;
			*(unsigned long *) &data[74] = chan->if_send_multicast;
			*(unsigned long *) &data[78] = chan->if_send_critical_ISR;
			*(unsigned long *) &data[82] = chan->if_send_critical_non_ISR;
			*(unsigned long *) &data[86] = chan->if_send_busy;
			*(unsigned long *) &data[90] = chan->if_send_busy_timeout;
			*(unsigned long *) &data[94] = chan->if_send_DRVSTATS_request;
			*(unsigned long *) &data[98] = chan->if_send_FPIPE_request;
			*(unsigned long *) &data[102] = chan->if_send_wan_disconnected;
			*(unsigned long *) &data[106] = chan->if_send_dlci_disconnected;
			*(unsigned long *) &data[110] = chan->if_send_no_bfrs;
			*(unsigned long *) &data[114] = chan->if_send_adptr_bfrs_full;
			*(unsigned long *) &data[118] = chan->if_send_bfrs_passed_to_adptr;
			*(unsigned long *) &data[120] = card->irq_dis_if_send_count;
			mbox->cmd.length = 62;
			break;
		case 0x46:
			*(unsigned long *) &data[62] = card->statistics.isr_entry;
			*(unsigned long *) &data[66] = card->statistics.isr_already_critical;
			*(unsigned long *) &data[70] = card->statistics.isr_rx;
			*(unsigned long *) &data[74] = card->statistics.isr_tx;
			*(unsigned long *) &data[78] = card->statistics.isr_intr_test;
			*(unsigned long *) &data[82] = card->statistics.isr_spurious;
			*(unsigned long *) &data[86] = card->statistics.isr_enable_tx_int;
			*(unsigned long *) &data[90] = card->statistics.tx_intr_dev_not_started;
			*(unsigned long *) &data[94] = card->statistics.rx_intr_corrupt_rx_bfr;
			*(unsigned long *) &data[98] = card->statistics.rx_intr_on_orphaned_DLCI;
			*(unsigned long *) &data[102] = chan->rx_intr_no_socket;
			*(unsigned long *) &data[106] = chan->rx_intr_dev_not_started;
			*(unsigned long *) &data[110] = chan->rx_intr_DRVSTATS_request;
			*(unsigned long *) &data[114] = chan->rx_intr_FPIPE_request;
			*(unsigned long *) &data[118] = chan->rx_intr_bfr_not_passed_to_stack;
			*(unsigned long *) &data[122] = chan->rx_intr_bfr_passed_to_stack;
			mbox->cmd.length = 64;
			break;
		case 0x47:
			*(unsigned long *) &data[62] = chan->UDP_FPIPE_mgmt_kmalloc_err;
			*(unsigned long *) &data[66] = chan->UDP_FPIPE_mgmt_adptr_type_err;
			*(unsigned long *) &data[70] = chan->UDP_FPIPE_mgmt_direction_err;
			*(unsigned long *) &data[74] = chan->UDP_FPIPE_mgmt_adptr_cmnd_timeout;
			*(unsigned long *) &data[78] = chan->UDP_FPIPE_mgmt_adptr_cmnd_OK;
			*(unsigned long *) &data[82] = chan->UDP_FPIPE_mgmt_adptr_send_passed;
			*(unsigned long *) &data[86] = chan->UDP_FPIPE_mgmt_adptr_send_failed;
			*(unsigned long *) &data[90] = chan->UDP_FPIPE_mgmt_no_socket;
			*(unsigned long *) &data[94] = chan->UDP_FPIPE_mgmt_not_passed_to_stack;
			*(unsigned long *) &data[98] = chan->UDP_FPIPE_mgmt_passed_to_stack;
			*(unsigned long *) &data[102] = chan->UDP_DRVSTATS_mgmt_kmalloc_err;
			*(unsigned long *) &data[106] = chan->UDP_DRVSTATS_mgmt_adptr_cmnd_timeout;
			*(unsigned long *) &data[110] = chan->UDP_DRVSTATS_mgmt_adptr_cmnd_OK;
			*(unsigned long *) &data[114] = chan->UDP_DRVSTATS_mgmt_adptr_send_passed;
			*(unsigned long *) &data[118] = chan->UDP_DRVSTATS_mgmt_adptr_send_failed;
			*(unsigned long *) &data[122] = chan->UDP_DRVSTATS_mgmt_no_socket;
			*(unsigned long *) &data[126] = chan->UDP_DRVSTATS_mgmt_not_passed_to_stack;
			*(unsigned long *) &data[130] = chan->UDP_DRVSTATS_mgmt_passed_to_stack;
			*(unsigned long *) &data[134] = card->statistics.poll_entry;
			*(unsigned long *) &data[138] = card->statistics.poll_already_critical;
			*(unsigned long *) &data[142] = card->statistics.poll_processed;
			*(unsigned long *) &data[144] = card->irq_dis_poll_count;
			mbox->cmd.length = 86;
			break;
		default:
			do 
			{
				memcpy(&mbox->cmd, &sendpacket[47], sizeof(fr_cmd_t));
				if (mbox->cmd.length) 
					memcpy(&mbox->data, &sendpacket[62], mbox->cmd.length);
				err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
			} 
			while (err && c_retry-- && fr_event(card, err, mbox));
			
			if (!err) 
			{
				++chan->UDP_DRVSTATS_mgmt_adptr_cmnd_OK;
				memcpy(data, sendpacket, skb->len);
				memcpy(&data[47], &mbox->cmd, sizeof(fr_cmd_t));
				if (mbox->cmd.length) 
					memcpy(&data[62], &mbox->data, mbox->cmd.length);
			} 
			else 
			{
				++chan->UDP_DRVSTATS_mgmt_adptr_cmnd_timeout;
			}
	}
	/* Fill UDP TTL */
	data[10] = card->wandev.ttl;
	len = reply_udp(data, mbox->cmd.length);
	if (udp_pkt_src == UDP_PKT_FRM_NETWORK) 
	{
		err = fr508_send(card, dlci, 0, len, data);
		if (err)
			++chan->UDP_DRVSTATS_mgmt_adptr_send_failed;
		else
			++chan->UDP_DRVSTATS_mgmt_adptr_send_passed;
		dev_kfree_skb(skb);
	}
	else
	{
		/* Allocate socket buffer */
		if ((new_skb = dev_alloc_skb(len)) != NULL) 
		{
			/* copy data into new_skb */
			buf = skb_put(new_skb, len);
			memcpy(buf, data, len);
			/* Decapsulate packet and pass it up the 
			   protocol stack */
			new_skb->dev = dev;
			/* remove hardware header */
			buf = skb_pull(new_skb, 1);
			if (!wanrouter_type_trans(new_skb, dev)) 
			{
				/* can't decapsulate packet */
				++chan->UDP_DRVSTATS_mgmt_not_passed_to_stack;
				dev_kfree_skb(new_skb);
			}
			else
			{
				++chan->UDP_DRVSTATS_mgmt_passed_to_stack;
				netif_rx(new_skb);
			}
		}
		else
		{
			++chan->UDP_DRVSTATS_mgmt_no_socket;
			printk(KERN_INFO "%s: UDP mgmt cmnd, no socket buffers available!\n", card->devname);
		}
	}
	kfree(data);
	return 0;
}

/*==============================================================================
 * Determine what type of UDP call it is. DRVSTATS or FPIPE8ND ?
 */

static int udp_pkt_type(struct sk_buff *skb, sdla_t * card)
{
	unsigned char *sendpacket;
	unsigned char buf2[5];
	sendpacket = skb->data;
	memcpy(&buf2, &card->wandev.udp_port, 2);
	if (sendpacket[2] == 0x45 &&	/* IP packet */
	    sendpacket[11] == 0x11 &&	/* UDP packet */
	    sendpacket[24] == buf2[1] &&	/* UDP Port */
	    sendpacket[25] == buf2[0] &&
	    sendpacket[38] == 0x01) 
	{
		if (sendpacket[30] == 0x46 &&	/* FPIPE8ND: Signature */
		    sendpacket[31] == 0x50 &&
		    sendpacket[32] == 0x49 &&
		    sendpacket[33] == 0x50 &&
		    sendpacket[34] == 0x45 &&
		    sendpacket[35] == 0x38 &&
		    sendpacket[36] == 0x4E &&
		    sendpacket[37] == 0x44) 
		{
			return UDP_FPIPE_TYPE;
		} else if (sendpacket[30] == 0x44 &&	/* DRVSTATS: Signature */
			   sendpacket[31] == 0x52 &&
			   sendpacket[32] == 0x56 &&
			   sendpacket[33] == 0x53 &&
			   sendpacket[34] == 0x54 &&
			   sendpacket[35] == 0x41 &&
			   sendpacket[36] == 0x54 &&
			   sendpacket[37] == 0x53) 
		{
			return UDP_DRVSTATS_TYPE;
		}
		else
			return UDP_INVALID_TYPE;
	}
	else
		return UDP_INVALID_TYPE;
}
/*==============================================================================
 * Initializes the Statistics values in the fr_channel structure.
 */
 
void init_chan_statistics(fr_channel_t * chan)
{
	chan->if_send_entry = 0;
	chan->if_send_skb_null = 0;
	chan->if_send_broadcast = 0;
	chan->if_send_multicast = 0;
	chan->if_send_critical_ISR = 0;
	chan->if_send_critical_non_ISR = 0;
	chan->if_send_busy = 0;
	chan->if_send_busy_timeout = 0;
	chan->if_send_FPIPE_request = 0;
	chan->if_send_DRVSTATS_request = 0;
	chan->if_send_wan_disconnected = 0;
	chan->if_send_dlci_disconnected = 0;
	chan->if_send_no_bfrs = 0;
	chan->if_send_adptr_bfrs_full = 0;
	chan->if_send_bfrs_passed_to_adptr = 0;
	chan->rx_intr_no_socket = 0;
	chan->rx_intr_dev_not_started = 0;
	chan->rx_intr_FPIPE_request = 0;
	chan->rx_intr_DRVSTATS_request = 0;
	chan->rx_intr_bfr_not_passed_to_stack = 0;
	chan->rx_intr_bfr_passed_to_stack = 0;
	chan->UDP_FPIPE_mgmt_kmalloc_err = 0;
	chan->UDP_FPIPE_mgmt_direction_err = 0;
	chan->UDP_FPIPE_mgmt_adptr_type_err = 0;
	chan->UDP_FPIPE_mgmt_adptr_cmnd_OK = 0;
	chan->UDP_FPIPE_mgmt_adptr_cmnd_timeout = 0;
	chan->UDP_FPIPE_mgmt_adptr_send_passed = 0;
	chan->UDP_FPIPE_mgmt_adptr_send_failed = 0;
	chan->UDP_FPIPE_mgmt_not_passed_to_stack = 0;
	chan->UDP_FPIPE_mgmt_passed_to_stack = 0;
	chan->UDP_FPIPE_mgmt_no_socket = 0;
	chan->UDP_DRVSTATS_mgmt_kmalloc_err = 0;
	chan->UDP_DRVSTATS_mgmt_adptr_cmnd_OK = 0;
	chan->UDP_DRVSTATS_mgmt_adptr_cmnd_timeout = 0;
	chan->UDP_DRVSTATS_mgmt_adptr_send_passed = 0;
	chan->UDP_DRVSTATS_mgmt_adptr_send_failed = 0;
	chan->UDP_DRVSTATS_mgmt_not_passed_to_stack = 0;
	chan->UDP_DRVSTATS_mgmt_passed_to_stack = 0;
	chan->UDP_DRVSTATS_mgmt_no_socket = 0;
}
/*==============================================================================
 * Initializes the Statistics values in the Sdla_t structure.
 */

void init_global_statistics(sdla_t * card)
{
	/* Intialize global statistics for a card */
	card->statistics.isr_entry = 0;
	card->statistics.isr_already_critical = 0;
	card->statistics.isr_rx = 0;
	card->statistics.isr_tx = 0;
	card->statistics.isr_intr_test = 0;
	card->statistics.isr_spurious = 0;
	card->statistics.isr_enable_tx_int = 0;
	card->statistics.rx_intr_corrupt_rx_bfr = 0;
	card->statistics.rx_intr_on_orphaned_DLCI = 0;
	card->statistics.tx_intr_dev_not_started = 0;
	card->statistics.poll_entry = 0;
	card->statistics.poll_already_critical = 0;
	card->statistics.poll_processed = 0;
}

static void read_DLCI_IB_mapping(sdla_t * card, fr_channel_t * chan)
{
	fr_mbox_t *mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	dlci_IB_mapping_t *result;
	int err, counter, found;
	do 
	{
		memset(&mbox->cmd, 0, sizeof(fr_cmd_t));
		mbox->cmd.command = FR_READ_DLCI_IB_MAPPING;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	}
	while (err && retry-- && fr_event(card, err, mbox));
	
	if (mbox->cmd.result != 0)
		printk(KERN_INFO "%s: Read DLCI IB Mapping failed\n", chan->name);

	counter = mbox->cmd.length / sizeof(dlci_IB_mapping_t);
	result = (void *) mbox->data;
	found = 0;
	for (; counter; --counter, ++result) 
	{
		if (result->dlci == chan->dlci) 
		{
			printk(KERN_INFO "%s: DLCI= %d, IB addr = %lx for %s\n"
			 ,card->devname, result->dlci, result->addr_value ,chan->name);
			chan->IB_addr = result->addr_value;
			chan->dlci_int_interface = (void *) (card->hw.dpmbase +
					   (chan->IB_addr & 0x00001FFF));
			found = 1;
			break;
		}
	}
	if (!found)
		printk(KERN_INFO "%s: DLCI %d not found by IB MAPPING cmd\n",
		       card->devname, chan->dlci);
}

/****** End *****************************************************************/
