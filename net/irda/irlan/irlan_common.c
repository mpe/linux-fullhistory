/*********************************************************************
 *                
 * Filename:      irlan_common.c
 * Version:       0.9
 * Description:   IrDA LAN Access Protocol Implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Wed Apr  7 17:03:21 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/irttp.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/timer.h>

#include <net/irda/irlan_common.h>
#include <net/irda/irlan_client.h>
#include <net/irda/irlan_provider.h> 
#include <net/irda/irlan_eth.h>
#include <net/irda/irlan_filter.h>

/* extern char sysctl_devname[]; */

/*
 *  Master structure
 */
hashbin_t *irlan = NULL;
static __u32 ckey, skey;

/* Module parameters */
static int eth = 0; /* Use "eth" or "irlan" name for devices */
static int access = ACCESS_PEER; /* PEER, DIRECT or HOSTED */
static int timeout = IRLAN_TIMEOUT;

static char *irlan_state[] = {
	"IRLAN_IDLE",
	"IRLAN_QUERY",
	"IRLAN_CONN", 
	"IRLAN_INFO",
	"IRLAN_MEDIA",
	"IRLAN_OPEN",
	"IRLAN_WAIT",
	"IRLAN_ARB", 
	"IRLAN_DATA",
	"IRLAN_CLOSE",
	"IRLAN_SYNC"
};

static char *irlan_access[] = {
	"UNKNOWN",
	"DIRECT",
	"PEER",
	"HOSTED"
};

static char *irlan_media[] = {
	"UNKNOWN",
	"802.3",
	"802.5"
};

static void __irlan_close(struct irlan_cb *self);
static int __irlan_insert_param(struct sk_buff *skb, char *param, int type, 
				__u8 value_byte, __u16 value_short, 
				__u8 *value_array, __u16 value_len);
static void irlan_close_tsaps(struct irlan_cb *self);

#ifdef CONFIG_PROC_FS
static int irlan_proc_read(char *buf, char **start, off_t offset, int len, 
			   int unused);

extern struct proc_dir_entry proc_irda;

struct proc_dir_entry proc_irlan = {
	0, 5, "irlan",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL,
	&irlan_proc_read,
};
#endif /* CONFIG_PROC_FS */

void irlan_watchdog_timer_expired(unsigned long data)
{
	struct irmanager_event mgr_event;
	struct irlan_cb *self = (struct irlan_cb *) data;
	
	DEBUG(2, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Check if device still configured */
	if (self->dev.start) {
		mgr_event.event = EVENT_IRLAN_STOP;
		sprintf(mgr_event.devname, "%s", self->ifname);
		irmanager_notify(&mgr_event);

		/*
		 *  We set this to false, so that irlan_dev_close known that
		 *  notify_irmanager should actually be set to TRUE again 
		 *  instead of FALSE, since this close has not been initiated
		 *  by the user.
		 */
		self->notify_irmanager = FALSE;
	} else
		irlan_close(self);
}

void irlan_start_watchdog_timer(struct irlan_cb *self, int timeout)
{
	DEBUG(4, __FUNCTION__ "()\n");
	
	irda_start_timer(&self->watchdog_timer, timeout, (unsigned long) self,
			 irlan_watchdog_timer_expired);
}

/*
 * Function irlan_eth_open (dev)
 *
 *    Network device has been opened by user
 *
 */
static int irlan_eth_open(struct device *dev)
{
	struct irlan_cb *self;
	
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(dev != NULL, return -1;);

	self = (struct irlan_cb *) dev->priv;

	ASSERT(self != NULL, return -1;);

	/* Ready to play! */
/* 	dev->tbusy = 0; */ /* Wait until data link is ready */
	dev->interrupt = 0;
	dev->start = 1;

	self->notify_irmanager = TRUE;

	/* We are now open, so time to do some work */
	irlan_client_wakeup(self, self->saddr, self->daddr);

	MOD_INC_USE_COUNT;
	
	return 0;
}

/*
 * Function irlan_eth_close (dev)
 *
 *    Stop the ether network device, his function will usually be called by
 *    ifconfig down. We should now disconnect the link, We start the 
 *    close timer, so that the instance will be removed if we are unable
 *    to discover the remote device after the disconnect.
 */
static int irlan_eth_close(struct device *dev)
{
	struct irlan_cb *self = (struct irlan_cb *) dev->priv;

	DEBUG(4, __FUNCTION__ "()\n");
	
	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	MOD_DEC_USE_COUNT;

	irlan_close_data_channel(self);

	irlan_close_tsaps(self);

	irlan_do_client_event(self, IRLAN_LMP_DISCONNECT, NULL);
	irlan_do_provider_event(self, IRLAN_LMP_DISCONNECT, NULL);	
	
	irlan_start_watchdog_timer(self, IRLAN_TIMEOUT);

	/* Device closed by user! */
	if (self->notify_irmanager)
		self->notify_irmanager = FALSE;
	else
		self->notify_irmanager = TRUE;

	return 0;
}
/*
 * Function irlan_eth_init (dev)
 *
 *    The network device initialization function.
 *
 */
int irlan_eth_init(struct device *dev)
{
	struct irmanager_event mgr_event;
	struct irlan_cb *self;

	DEBUG(4, __FUNCTION__"()\n");

	ASSERT(dev != NULL, return -1;);
       
	self = (struct irlan_cb *) dev->priv;

	dev->open               = irlan_eth_open;
	dev->stop               = irlan_eth_close;
	dev->hard_start_xmit    = irlan_eth_xmit; 
	dev->get_stats	        = irlan_eth_get_stats;
	dev->set_multicast_list = irlan_eth_set_multicast_list;

	dev->tbusy = 1;
	
	ether_setup(dev);
	
	dev->tx_queue_len = TTP_MAX_QUEUE;

#if 0
	/*  
	 *  OK, since we are emulating an IrLAN sever we will have to give
	 *  ourself an ethernet address!
	 *  FIXME: this must be more dynamically
	 */
	dev->dev_addr[0] = 0x40;
	dev->dev_addr[1] = 0x00;
	dev->dev_addr[2] = 0x00;
	dev->dev_addr[3] = 0x00;
	dev->dev_addr[4] = 0x23;
	dev->dev_addr[5] = 0x45;
#endif
	/* 
	 * Network device has now been registered, so tell irmanager about
	 * it, so it can be configured with network parameters
	 */
	mgr_event.event = EVENT_IRLAN_START;
	sprintf(mgr_event.devname, "%s", self->ifname);
	irmanager_notify(&mgr_event);

	/* 
	 * We set this so that we only notify once, since if 
	 * configuration of the network device fails, the user
	 * will have to sort it out first anyway. No need to 
	 * try again.
	 */
	self->notify_irmanager = FALSE;

	return 0;
}

/*
 * Function irlan_init (void)
 *
 *    Initialize IrLAN layer
 *
 */
__initfunc(int irlan_init(void))
{
	struct irlan_cb *new;
	__u16 hints;

	DEBUG(4, __FUNCTION__"()\n");

	/* Allocate master array */
	irlan = hashbin_new(HB_LOCAL); 
	if (irlan == NULL) {
		printk(KERN_WARNING "IrLAN: Can't allocate hashbin!\n");
		return -ENOMEM;
	}
#ifdef CONFIG_PROC_FS
	proc_register(&proc_irda, &proc_irlan);
#endif /* CONFIG_PROC_FS */

	DEBUG(4, __FUNCTION__ "()\n");
	
	hints = irlmp_service_to_hint(S_LAN);

	/* Register with IrLMP as a client */
	ckey = irlmp_register_client(hints, irlan_client_discovery_indication,
				     NULL);
	
	/* Register with IrLMP as a service */
 	skey = irlmp_register_service(hints);

	/* Start the first IrLAN instance */
 	new = irlan_open(DEV_ADDR_ANY, DEV_ADDR_ANY, FALSE);

	/* Do some fast discovery! */
	irlmp_discovery_request(8);

	return 0;
}

void irlan_cleanup(void) 
{
	DEBUG(4, __FUNCTION__ "()\n");

	irlmp_unregister_client(ckey);

	irlmp_unregister_service(skey);

#ifdef CONFIG_PROC_FS
	proc_unregister(&proc_irda, proc_irlan.low_ino);
#endif /* CONFIG_PROC_FS */
	/*
	 *  Delete hashbin and close all irlan client instances in it
	 */
	hashbin_delete(irlan, (FREE_FUNC) __irlan_close);
}

/*
 * Function irlan_register_netdev (self)
 *
 *    Registers the network device to be used. We should don't register until
 *    we have been binded to a particular provider or client.
 */
int irlan_register_netdev(struct irlan_cb *self)
{
	int i=0;

	DEBUG(4, __FUNCTION__ "()\n");

	/* Check if we should call the device eth<x> or irlan<x> */
	if (!eth) {
		/* Get the first free irlan<x> name */
		do {
			sprintf(self->ifname, "%s%d", "irlan", i++);
		} while (dev_get(self->ifname) != NULL);
	}
	self->dev.name = self->ifname;
	
	if (register_netdev(&self->dev) != 0) {
		DEBUG(2, __FUNCTION__ "(), register_netdev() failed!\n");
		return -1;
	}
	self->netdev_registered = TRUE;
	
	return 0;
}

/*
 * Function irlan_open (void)
 *
 *    Open new instance of a client/provider, we should only register the 
 *    network device if this instance is ment for a particular client/provider
 */
struct irlan_cb *irlan_open(__u32 saddr, __u32 daddr, int netdev)
{
	struct irlan_cb *self;

	DEBUG(2, __FUNCTION__ "()\n");

	/* 
	 *  Initialize the irlan structure. 
	 */
	self = kmalloc(sizeof(struct irlan_cb), GFP_ATOMIC);
	if (self == NULL)
		return NULL;
	
	memset(self, 0, sizeof(struct irlan_cb));

	/*
	 *  Initialize local device structure
	 */
	self->magic = IRLAN_MAGIC;

	ASSERT(irlan != NULL, return NULL;);
	
	sprintf(self->ifname, "%s", "unknown");

	self->dev.priv = (void *) self;
	self->dev.next = NULL;
	self->dev.init = irlan_eth_init;
	
	self->saddr = saddr;
	self->daddr = daddr;

	/* Provider access can only be PEER, DIRECT, or HOSTED */
	self->access_type = access;
	self->media = MEDIA_802_3;

	self->notify_irmanager = TRUE;

	init_timer(&self->watchdog_timer);
	init_timer(&self->client.kick_timer);

	hashbin_insert(irlan, (QUEUE *) self, daddr, NULL);
		
	irlan_next_client_state(self, IRLAN_IDLE);
	irlan_next_provider_state(self, IRLAN_IDLE);

	irlan_open_data_tsap(self);
  	irlan_provider_open_ctrl_tsap(self);
	irlan_client_open_ctrl_tsap(self);

	/* Register network device now, or wait until some later time? */
	if (netdev)
		irlan_register_netdev(self);

	return self;
}
/*
 * Function irlan_close (self)
 *
 *    This function closes and deallocates the IrLAN client instances. Be 
 *    aware that other functions which calles client_close() must call 
 *    hashbin_remove() first!!!
 *
 */
static void __irlan_close(struct irlan_cb *self)
{
	DEBUG(2, __FUNCTION__ "()\n");
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	del_timer(&self->watchdog_timer);
	del_timer(&self->client.kick_timer);

	/* Close all open connections and remove TSAPs */
	irlan_close_tsaps(self);

	if (self->netdev_registered)
		unregister_netdev(&self->dev);
	
	self->magic = 0;
	kfree(self);
}

/*
 * Function irlan_close (self)
 *
 *    Close instance
 *
 */
void irlan_close(struct irlan_cb *self)
{
	struct irlan_cb *entry;
	
	DEBUG(2, __FUNCTION__ "()\n");

        ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Check if device is still configured */
	if (self->dev.start) {
		DEBUG(2, __FUNCTION__ 
		       "(), Device still configured, closing later!\n");
		return;
	}
	DEBUG(2, __FUNCTION__ "(), daddr=%08x\n", self->daddr);
	entry = hashbin_remove(irlan, self->daddr, NULL);

	ASSERT(entry == self, return;);
	
        __irlan_close(self);
}

void irlan_connect_indication(void *instance, void *sap, struct qos_info *qos,
			      __u32 max_sdu_size, struct sk_buff *skb)
{
	struct irlan_cb *self;
	struct tsap_cb *tsap;

	DEBUG(2, __FUNCTION__ "()\n");
	
	self = (struct irlan_cb *) instance;
	tsap = (struct tsap_cb *) sap;
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	ASSERT(tsap == self->tsap_data,return;);

	DEBUG(2, "IrLAN, We are now connected!\n");
	del_timer(&self->watchdog_timer);

	irlan_do_provider_event(self, IRLAN_DATA_CONNECT_INDICATION, skb);
	irlan_do_client_event(self, IRLAN_DATA_CONNECT_INDICATION, skb);

	if (self->access_type == ACCESS_PEER) {
		/* 
		 * Data channel is open, so we are now allowed to
		 * configure the remote filter 
		 */
		irlan_get_unicast_addr(self);
		irlan_open_unicast_addr(self);
	}
	/* Ready to transfer Ethernet frames */
	self->dev.tbusy = 0;
}

void irlan_connect_confirm(void *instance, void *sap, struct qos_info *qos, 
			   __u32 max_sdu_size, struct sk_buff *skb) 
{
	struct irlan_cb *self;

	DEBUG(2, __FUNCTION__ "()\n");

	self = (struct irlan_cb *) instance;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* TODO: we could set the MTU depending on the max_sdu_size */

	DEBUG(2, "IrLAN, We are now connected!\n");
	del_timer(&self->watchdog_timer);

	/* 
	 * Data channel is open, so we are now allowed to configure the remote
	 * filter 
	 */
	irlan_get_unicast_addr(self);
	irlan_open_unicast_addr(self);

	/* Ready to transfer Ethernet frames */
	self->dev.tbusy = 0;
}

/*
 * Function irlan_client_disconnect_indication (handle)
 *
 *    Callback function for the IrTTP layer. Indicates a disconnection of
 *    the specified connection (handle)
 */
void irlan_disconnect_indication(void *instance, void *sap, LM_REASON reason, 
				 struct sk_buff *userdata) 
{
	struct irlan_cb *self;
	struct tsap_cb *tsap;

	DEBUG(2, __FUNCTION__ "(), reason=%d\n", reason);
	
	self = (struct irlan_cb *) instance;
	tsap = (struct tsap_cb *) sap;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);	
	ASSERT(tsap != NULL, return;);
	ASSERT(tsap->magic == TTP_TSAP_MAGIC, return;);
	
	ASSERT(tsap == self->tsap_data, return;);

	DEBUG(2, "IrLAN, data channel disconnected by peer!\n");

	switch(reason) {
	case LM_USER_REQUEST: /* User request */
		irlan_close(self);
		break;
	case LM_LAP_DISCONNECT: /* Unexpected IrLAP disconnect */
		irlan_start_watchdog_timer(self, IRLAN_TIMEOUT);
		break;
	case LM_CONNECT_FAILURE: /* Failed to establish IrLAP connection */
		DEBUG(2, __FUNCTION__ "(), LM_CONNECT_FAILURE not impl\n");
		break;
	case LM_LAP_RESET:  /* IrLAP reset */
		DEBUG(2, __FUNCTION__ "(), LM_CONNECT_FAILURE not impl\n");
		break;
	case LM_INIT_DISCONNECT:
		DEBUG(2, __FUNCTION__ "(), LM_CONNECT_FAILURE not impl\n");
		break;
	default:
		break;
	}
	
	/* Stop IP from transmitting more packets */
	/* irlan_client_flow_indication(handle, FLOW_STOP, priv); */

	irlan_do_client_event(self, IRLAN_LMP_DISCONNECT, NULL);
	irlan_do_provider_event(self, IRLAN_LMP_DISCONNECT, NULL);
}

void irlan_open_data_tsap(struct irlan_cb *self)
{
	struct notify_t notify;
	struct tsap_cb *tsap;

	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Check if already open */
	if (self->tsap_data)
		return;

	irda_notify_init(&notify);

	notify.data_indication       = irlan_eth_receive;
	notify.udata_indication      = irlan_eth_receive;
	notify.connect_indication    = irlan_connect_indication;
	notify.connect_confirm       = irlan_connect_confirm;
 	notify.flow_indication       = irlan_eth_flow_indication;
	notify.disconnect_indication = irlan_disconnect_indication;
	notify.instance              = self;
	strncpy(notify.name, "IrLAN data", NOTIFY_MAX_NAME);

	tsap = irttp_open_tsap(LSAP_ANY, DEFAULT_INITIAL_CREDIT, &notify);
	if (!tsap) {
		DEBUG(2, __FUNCTION__ "(), Got no tsap!\n");
		return;
	}
	self->tsap_data = tsap;

	/* 
	 *  This is the data TSAP selector which we will pass to the client
	 *  when the client ask for it.
	 */
	self->stsap_sel_data = self->tsap_data->stsap_sel;
}

static void irlan_close_tsaps(struct irlan_cb *self)
{
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* 
	 *  Disconnect and close all open TSAP connections
	 */
	if (self->tsap_data) {
		irttp_disconnect_request(self->tsap_data, NULL, P_NORMAL);
		irttp_close_tsap(self->tsap_data);
		self->tsap_data = NULL;
		
	}
	if (self->client.tsap_ctrl) {
		irttp_disconnect_request(self->client.tsap_ctrl, NULL, 
					 P_NORMAL);
		irttp_close_tsap(self->client.tsap_ctrl);
		self->client.tsap_ctrl = NULL;
	}
	if (self->provider.tsap_ctrl) {
		irttp_disconnect_request(self->provider.tsap_ctrl, NULL, 
					 P_NORMAL);
		irttp_close_tsap(self->provider.tsap_ctrl);
		self->provider.tsap_ctrl = NULL;
	}
}

/*
 * Function irlan_ias_register (self, tsap_sel)
 *
 *    Register with LM-IAS
 *
 */
void irlan_ias_register(struct irlan_cb *self, __u8 tsap_sel)
{
	struct ias_object *obj;
	struct ias_value *new_value;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	
	/* 
	 * Check if object has already been registred by a previous provider.
	 * If that is the case, we just change the value of the attribute
	 */
	if (!irias_find_object("IrLAN")) {
		obj = irias_new_object("IrLAN", IAS_IRLAN_ID);
		irias_add_integer_attrib(obj, "IrDA:TinyTP:LsapSel", tsap_sel);
		irias_insert_object(obj);
	} else {
		new_value = irias_new_integer_value(tsap_sel);
		irias_object_change_attribute("IrLAN", "IrDA:TinyTP:LsapSel",
					      new_value);
	}
	
        /* Register PnP object only if not registred before */
        if (!irias_find_object("PnP")) {
		obj = irias_new_object("PnP", IAS_PNP_ID);
#if 0
		irias_add_string_attrib(obj, "Name", sysctl_devname);
#else
		irias_add_string_attrib(obj, "Name", "Linux");
#endif
		irias_add_string_attrib(obj, "DeviceID", "HWP19F0");
		irias_add_integer_attrib(obj, "CompCnt", 2);
		irias_add_string_attrib(obj, "Comp#01", "PNP8294");
		irias_add_string_attrib(obj, "Comp#02", "PNP8389");
		irias_add_string_attrib(obj, "Manufacturer", "Linux-IrDA Project");
		irias_insert_object(obj);
	}
}

/*
 * Function irlan_get_provider_info (self)
 *
 *    Send Get Provider Information command to peer IrLAN layer
 *
 */
void irlan_get_provider_info(struct irlan_cb *self)
{
	struct sk_buff *skb;
	__u8 *frame;

	DEBUG(4, __FUNCTION__ "()\n");
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = dev_alloc_skb(64);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put(skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_GET_PROVIDER_INFO;
	frame[1] = 0x00;                 /* Zero parameters */
	
	irttp_data_request(self->client.tsap_ctrl, skb);
}

/*
 * Function irlan_open_data_channel (self)
 *
 *    Send an Open Data Command to provider
 *
 */
void irlan_open_data_channel(struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	
	skb = dev_alloc_skb(64);
	if (!skb)
		return;

	skb_reserve(skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put(skb, 2);
	
	frame = skb->data;
	
	/* Build frame */
 	frame[0] = CMD_OPEN_DATA_CHANNEL;
	frame[1] = 0x02; /* Two parameters */

	irlan_insert_string_param(skb, "MEDIA", "802.3");
	irlan_insert_string_param(skb, "ACCESS_TYPE", "DIRECT");
	/* irlan_insert_string_param(skb, "MODE", "UNRELIABLE"); */

/* 	self->use_udata = TRUE; */

	irttp_data_request(self->client.tsap_ctrl, skb);
}

void irlan_close_data_channel(struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = dev_alloc_skb(64);
	if (!skb)
		return;

	skb_reserve(skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put(skb, 2);
	
	frame = skb->data;
	
	/* Build frame */
 	frame[0] = CMD_CLOSE_DATA_CHAN;
	frame[1] = 0x01; /* Two parameters */

	irlan_insert_byte_param(skb, "DATA_CHAN", self->dtsap_sel_data);

	irttp_data_request(self->client.tsap_ctrl, skb);
}

/*
 * Function irlan_open_unicast_addr (self)
 *
 *    Make IrLAN provider accept ethernet frames addressed to the unicast 
 *    address.
 *
 */
void irlan_open_unicast_addr(struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);	
	
	skb = dev_alloc_skb(128);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put(skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
 	irlan_insert_byte_param(skb, "DATA_CHAN" , self->dtsap_sel_data);
 	irlan_insert_string_param(skb, "FILTER_TYPE", "DIRECTED");
 	irlan_insert_string_param(skb, "FILTER_MODE", "FILTER"); 
	
	irttp_data_request(self->client.tsap_ctrl, skb);
}

/*
 * Function irlan_set_broadcast_filter (self, status)
 *
 *    Make IrLAN provider accept ethernet frames addressed to the broadcast
 *    address. Be careful with the use of this one, since there may be a lot
 *    of broadcast traffic out there. We can still function without this
 *    one but then _we_ have to initiate all communication with other
 *    hosts, since ARP request for this host will not be answered.
 */
void irlan_set_broadcast_filter(struct irlan_cb *self, int status) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG(2, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	
 	skb = dev_alloc_skb(128);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put(skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
 	irlan_insert_byte_param(skb, "DATA_CHAN", self->dtsap_sel_data);
 	irlan_insert_string_param(skb, "FILTER_TYPE", "BROADCAST");
	if (status)
		irlan_insert_string_param(skb, "FILTER_MODE", "FILTER"); 
	else
		irlan_insert_string_param(skb, "FILTER_MODE", "NONE"); 
	
	irttp_data_request(self->client.tsap_ctrl, skb);
}

/*
 * Function irlan_set_multicast_filter (self, status)
 *
 *    Make IrLAN provider accept ethernet frames addressed to the multicast
 *    address. 
 *
 */
void irlan_set_multicast_filter(struct irlan_cb *self, int status) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG(2, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

 	skb = dev_alloc_skb(128);
	if (!skb)
		return;
	
	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put(skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
 	irlan_insert_byte_param(skb, "DATA_CHAN", self->dtsap_sel_data);
 	irlan_insert_string_param(skb, "FILTER_TYPE", "MULTICAST");
	if (status)
		irlan_insert_string_param(skb, "FILTER_MODE", "ALL"); 
	else
		irlan_insert_string_param(skb, "FILTER_MODE", "NONE"); 
	
	irttp_data_request(self->client.tsap_ctrl, skb);
}

/*
 * Function irlan_get_unicast_addr (self)
 *
 *    Retrives the unicast address from the IrLAN provider. This address
 *    will be inserted into the devices structure, so the ethernet layer
 *    can construct its packets.
 *
 */
void irlan_get_unicast_addr(struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
		
	DEBUG(2, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	
	skb = dev_alloc_skb(128);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put(skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
 	irlan_insert_byte_param(skb, "DATA_CHAN", self->dtsap_sel_data);
 	irlan_insert_string_param(skb, "FILTER_TYPE", "DIRECTED");
 	irlan_insert_string_param(skb, "FILTER_OPERATION", "DYNAMIC"); 
	
	irttp_data_request(self->client.tsap_ctrl, skb);
}

/*
 * Function irlan_get_media_char (self)
 *
 *    
 *
 */
void irlan_get_media_char(struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	
	skb = dev_alloc_skb(64);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put(skb, 2);
	
	frame = skb->data;
	
	/* Build frame */
 	frame[0] = CMD_GET_MEDIA_CHAR;
	frame[1] = 0x01; /* One parameter */
	
	irlan_insert_string_param(skb, "MEDIA", "802.3");
	
	irttp_data_request(self->client.tsap_ctrl, skb);
}

/*
 * Function insert_byte_param (skb, param, value)
 *
 *    Insert byte parameter into frame
 *
 */
int irlan_insert_byte_param(struct sk_buff *skb, char *param, __u8 value)
{
	return __irlan_insert_param(skb, param, IRLAN_BYTE, value, 0, NULL, 0);
}

int irlan_insert_short_param(struct sk_buff *skb, char *param, __u16 value)
{
	return __irlan_insert_param(skb, param, IRLAN_SHORT, 0, value, NULL, 0);
}

/*
 * Function insert_string (skb, param, value)
 *
 *    Insert string parameter into frame
 *
 */
int irlan_insert_string_param(struct sk_buff *skb, char *param, char *string)
{
	int string_len = strlen(string);

	return __irlan_insert_param(skb, param, IRLAN_ARRAY, 0, 0, string, 
				    string_len);
}

/*
 * Function insert_array_param(skb, param, value, len_value)
 *
 *    Insert array parameter into frame
 *
 */
int irlan_insert_array_param(struct sk_buff *skb, char *name, __u8 *array,
			     __u16 array_len)
{
	return __irlan_insert_param(skb, name, IRLAN_ARRAY, 0, 0, array, 
				    array_len);
}

/*
 * Function insert_param (skb, param, value, byte)
 *
 *    Insert parameter at end of buffer, structure of a parameter is:
 *
 *    -----------------------------------------------------------------------
 *    | Name Length[1] | Param Name[1..255] | Val Length[2] | Value[0..1016]|
 *    -----------------------------------------------------------------------
 */
static int __irlan_insert_param(struct sk_buff *skb, char *param, int type, 
				__u8 value_byte, __u16 value_short, 
				__u8 *value_array, __u16 value_len)
{
	__u8 *frame;
	__u8 param_len;
	__u16 tmp_le; /* Temporary value in little endian format */
	int n=0;
	
	if (skb == NULL) {
		DEBUG(2, __FUNCTION__ "(), Got NULL skb\n");
		return 0;
	}	

	param_len = strlen(param);
	switch (type) {
	case IRLAN_BYTE:
		value_len = 1;
		break;
	case IRLAN_SHORT:
		value_len = 2;
		break;
	case IRLAN_ARRAY:
		ASSERT(value_array != NULL, return 0;);
		ASSERT(value_len > 0, return 0;);
		break;
	default:
		DEBUG(2, __FUNCTION__ "(), Unknown parameter type!\n");
		return 0;
		break;
	}
	
	/* Insert at end of sk-buffer */
	frame = skb->tail;

	/* Make space for data */
	if (skb_tailroom(skb) < (param_len+value_len+3)) {
		DEBUG(2, __FUNCTION__ "(), No more space at end of skb\n");
		return 0;
	}	
	skb_put(skb, param_len+value_len+3);
	
	/* Insert parameter length */
	frame[n++] = param_len;
	
	/* Insert parameter */
	memcpy(frame+n, param, param_len); n += param_len;
	
	/* Insert value length (2 byte little endian format, LSB first) */
	tmp_le = cpu_to_le16(value_len);
	memcpy(frame+n, &tmp_le, 2); n += 2; /* To avoid alignment problems */

	/* Insert value */
	switch (type) {
	case IRLAN_BYTE:
		frame[n++] = value_byte;
		break;
	case IRLAN_SHORT:
		tmp_le = cpu_to_le16(value_short);
		memcpy(frame+n, &tmp_le, 2); n += 2;
		break;
	case IRLAN_ARRAY:
		memcpy(frame+n, value_array, value_len); n+=value_len;
		break;
	default:
		break;
	}
	ASSERT(n == (param_len+value_len+3), return 0;);

	return param_len+value_len+3;
}

/*
 * Function irlan_get_response_param (buf, param, value)
 *
 *    Extracts a single parameter name/value pair from buffer and updates
 *    the buffer pointer to point to the next name/value pair. 
 */
int irlan_get_param(__u8 *buf, char *name, char *value, __u16 *len)
{
	__u8 name_len;
	__u16 val_len;
	int n=0;
	
	DEBUG(4, __FUNCTION__ "()\n");
	
	/* get length of parameter name (1 byte) */
	name_len = buf[n++];
	
	if (name_len > 254) {
		DEBUG(2, __FUNCTION__ "(), name_len > 254\n");
		return -RSP_INVALID_COMMAND_FORMAT;
	}
	
	/* get parameter name */
	memcpy(name, buf+n, name_len);
	name[ name_len] = '\0';
	n+=name_len;
	
	/*  
	 *  Get length of parameter value (2 bytes in little endian 
	 *  format) 
	 */
	memcpy(&val_len, buf+n, 2); /* To avoid alignment problems */
	le16_to_cpus(&val_len); n+=2;
	
	if (val_len > 1016) {
		DEBUG(2, __FUNCTION__ "(), parameter length to long\n");
		return -RSP_INVALID_COMMAND_FORMAT;
	}
	*len = val_len;

	/* get parameter value */
	memcpy(value, buf+n, val_len);
	value[ val_len] = '\0';
	n+=val_len;
	
	DEBUG(4, "Parameter: %s ", name); 
	DEBUG(4, "Value: %s\n", value); 

	return n;
}

#ifdef CONFIG_PROC_FS
/*
 * Function irlan_client_proc_read (buf, start, offset, len, unused)
 *
 *    Give some info to the /proc file system
 */
static int irlan_proc_read(char *buf, char **start, off_t offset, int len, 
			   int unused)
{
 	struct irlan_cb *self;
	unsigned long flags;
     
	save_flags(flags);
	cli();

	ASSERT(irlan != NULL, return 0;);
	
	len = 0;
	
	len += sprintf(buf+len, "IrLAN instances:\n");
	
	self = (struct irlan_cb *) hashbin_get_first(irlan);
	while (self != NULL) {
		ASSERT(self->magic == IRLAN_MAGIC, return len;);
		
		len += sprintf(buf+len, "ifname: %s,\n",
			       self->ifname);
		len += sprintf(buf+len, "client state: %s, ",
			       irlan_state[ self->client.state]);
		len += sprintf(buf+len, "provider state: %s,\n",
			       irlan_state[ self->provider.state]);
		len += sprintf(buf+len, "saddr: %#08x, ",
			       self->saddr);
		len += sprintf(buf+len, "daddr: %#08x\n",
			       self->daddr);
		len += sprintf(buf+len, "version: %d.%d,\n",
			       self->version[1], self->version[0]);
		len += sprintf(buf+len, "access type: %s\n", 
			       irlan_access[ self->access_type]);
		len += sprintf(buf+len, "media: %s\n", 
			       irlan_media[ self->media]);

		len += sprintf(buf+len, "local filter:\n");
		len += sprintf(buf+len, "remote filter: ");
		len += irlan_print_filter(self->client.filter_type, buf+len);

		len += sprintf(buf+len, "tx busy: %s\n", self->dev.tbusy ? 
			       "TRUE" : "FALSE");

		len += sprintf(buf+len, "\n");

		self = (struct irlan_cb *) hashbin_get_next(irlan);
 	} 
	restore_flags(flags);

	return len;
}
#endif

/*
 * Function print_ret_code (code)
 *
 *    Print return code of request to peer IrLAN layer.
 *
 */
void print_ret_code(__u8 code) 
{
	switch(code) {
	case 0:
		printk(KERN_INFO "Success\n");
		break;
	case 1:
		printk(KERN_WARNING "Insufficient resources\n");
		break;
	case 2:
		printk(KERN_WARNING "Invalid command format\n");
		break;
	case 3:
		printk(KERN_WARNING "Command not supported\n");
		break;
	case 4:
		printk(KERN_WARNING "Parameter not supported\n");
		break;
	case 5:
		printk(KERN_WARNING "Value not supported\n");
		break;
	case 6:
		printk(KERN_WARNING "Not open\n");
		break;
	case 7:
		printk(KERN_WARNING "Authentication required\n");
		break;
	case 8:
		printk(KERN_WARNING "Invalid password\n");
		break;
	case 9:
		printk(KERN_WARNING "Protocol error\n");
		break;
	case 255:
		printk(KERN_WARNING "Asynchronous status\n");
		break;
	}
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("The Linux IrDA LAN protocol"); 

MODULE_PARM(eth, "i");
MODULE_PARM(access, "i");
MODULE_PARM(timeout, "i");

/*
 * Function init_module (void)
 *
 *    Initialize the IrLAN module, this function is called by the
 *    modprobe(1) program.
 */
int init_module(void) 
{
	return irlan_init();
}

/*
 * Function cleanup_module (void)
 *
 *    Remove the IrLAN module, this function is called by the rmmod(1)
 *    program
 */
void cleanup_module(void) 
{
	/* Free some memory */
 	irlan_cleanup();
}

#endif /* MODULE */

