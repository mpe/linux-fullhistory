/*********************************************************************
 *                
 * Filename:      irlan_client.c
 * Version:       0.9
 * Description:   IrDA LAN Access Protocol (IrLAN) Client
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Thu Apr 22 23:03:55 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:       skeleton.c by Donald Becker <becker@CESDIS.gsfc.nasa.gov>
 *                slip.c by Laurence Culhane, <loz@holmes.demon.co.uk>
 *                          Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <net/arp.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/irttp.h>
#include <net/irda/irlmp.h>
#include <net/irda/irias_object.h>
#include <net/irda/iriap.h>
#include <net/irda/timer.h>

#include <net/irda/irlan_common.h>
#include <net/irda/irlan_event.h>
#include <net/irda/irlan_eth.h>
#include <net/irda/irlan_provider.h>
#include <net/irda/irlan_client.h>

#undef CONFIG_IRLAN_GRATUITOUS_ARP

static void irlan_client_ctrl_disconnect_indication(void *instance, void *sap, 
						    LM_REASON reason, 
						    struct sk_buff *);
static int irlan_client_ctrl_data_indication(void *instance, void *sap, 
					     struct sk_buff *skb);
static void irlan_client_ctrl_connect_confirm(void *instance, void *sap, 
					      struct qos_info *qos, 
					      __u32 max_sdu_size,
					      struct sk_buff *);
static void irlan_check_response_param(struct irlan_cb *self, char *param, 
				       char *value, int val_len);

static void irlan_client_kick_timer_expired(unsigned long data)
{
	struct irlan_cb *self = (struct irlan_cb *) data;
	
	DEBUG(2, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	
	/*  
	 * If we are in peer mode, the client may not have got the discovery
	 * indication it needs to make progress. If the client is still in 
	 * IDLE state, we must kick it to, but only if the provider is not IDLE
 	 */
	if ((self->access_type == ACCESS_PEER) && 
	    (self->client.state == IRLAN_IDLE) &&
	    (self->provider.state != IRLAN_IDLE)) {
		irlan_client_wakeup(self, self->saddr, self->daddr);
	}
}

void irlan_client_start_kick_timer(struct irlan_cb *self, int timeout)
{
	DEBUG(4, __FUNCTION__ "()\n");
	
	irda_start_timer(&self->client.kick_timer, timeout, 
			 (unsigned long) self, 
			 irlan_client_kick_timer_expired);
}

/*
 * Function irlan_client_wakeup (self, saddr, daddr)
 *
 *    Wake up client
 *
 */
void irlan_client_wakeup(struct irlan_cb *self, __u32 saddr, __u32 daddr)
{
	struct irmanager_event mgr_event;

	DEBUG(0, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Check if we are already awake */
	if (self->client.state != IRLAN_IDLE)
		return;

	/* saddr may have changed! */
	self->saddr = saddr;
	
	/* Check if network device is up */
	if (self->dev.start) {
		/* Open TSAPs */
		irlan_client_open_ctrl_tsap(self);
 		irlan_provider_open_ctrl_tsap(self);
 		irlan_open_data_tsap(self);
		
		irlan_do_client_event(self, IRLAN_DISCOVERY_INDICATION, NULL);
	} else if (self->notify_irmanager) {
		/* 
		 * Tell irmanager that the device can now be 
		 * configured but only if the device was not taken
		 * down by the user
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
	}
	/* Restart watchdog timer */
	irlan_start_watchdog_timer(self, IRLAN_TIMEOUT);
	
	/* Start kick timer */
	irlan_client_start_kick_timer(self, 2*HZ);
}

/*
 * Function irlan_discovery_indication (daddr)
 *
 *    Remote device with IrLAN server support discovered
 *
 */
void irlan_client_discovery_indication(discovery_t *discovery) 
{
	struct irlan_cb *self, *entry;
	__u32 saddr, daddr;
	
	DEBUG(0, __FUNCTION__"()\n");

	ASSERT(irlan != NULL, return;);
	ASSERT(discovery != NULL, return;);

	saddr = discovery->saddr;
	daddr = discovery->daddr;

	/* 
	 *  Check if we already dealing with this provider.
	 */
	self = (struct irlan_cb *) hashbin_find(irlan, daddr, NULL);
      	if (self) {
		ASSERT(self->magic == IRLAN_MAGIC, return;);

		DEBUG(2, __FUNCTION__ "(), Found instance!\n");
		
		irlan_client_wakeup(self, saddr, daddr);

		return;
	}
	
	/* 
	 * We have no instance for daddr, so try and find an unused one
	 */
	self = hashbin_find(irlan, DEV_ADDR_ANY, NULL);
	if (self) {
		DEBUG(0, __FUNCTION__ "(), Found instance with DEV_ADDR_ANY!\n");
		/*
		 * Rehash instance, now we have a client (daddr) to serve.
		 */
		entry = hashbin_remove(irlan, self->daddr, NULL);
		ASSERT(entry == self, return;);

		self->daddr = daddr;
		self->saddr = saddr;

		DEBUG(0, __FUNCTION__ "(), daddr=%08x\n", self->daddr);
		hashbin_insert(irlan, (QUEUE*) self, self->daddr, NULL);
		
		/* Check if network device has been registered */
		if (!self->netdev_registered)
			irlan_register_netdev(self);
		
		/* Restart watchdog timer */
		irlan_start_watchdog_timer(self, IRLAN_TIMEOUT);
	}
}
	
/*
 * Function irlan_client_data_indication (handle, skb)
 *
 *    This function gets the data that is received on the control channel
 *
 */
static int irlan_client_ctrl_data_indication(void *instance, void *sap, 
					     struct sk_buff *skb)
{
	struct irlan_cb *self;
	
	DEBUG(4, __FUNCTION__ "()\n");
	
	self = (struct irlan_cb *) instance;
	
	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == IRLAN_MAGIC, return -1;);
	ASSERT(skb != NULL, return -1;);
	
	irlan_do_client_event(self, IRLAN_DATA_INDICATION, skb); 

	return 0;
}

static void irlan_client_ctrl_disconnect_indication(void *instance, void *sap, 
						    LM_REASON reason, 
						    struct sk_buff *userdata) 
{
	struct irlan_cb *self;
	struct tsap_cb *tsap;

	DEBUG(4, __FUNCTION__ "(), reason=%d\n", reason);
	
	self = (struct irlan_cb *) instance;
	tsap = (struct tsap_cb *) sap;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);	
	ASSERT(tsap != NULL, return;);
	ASSERT(tsap->magic == TTP_TSAP_MAGIC, return;);
	
	ASSERT(tsap == self->client.tsap_ctrl, return;);

	irlan_do_client_event(self, IRLAN_LMP_DISCONNECT, NULL);
}

/*
 * Function irlan_client_open_tsaps (self)
 *
 *    Initialize callbacks and open IrTTP TSAPs
 *
 */
void irlan_client_open_ctrl_tsap(struct irlan_cb *self)
{
	struct notify_t notify;
	struct tsap_cb *tsap;

	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Check if already open */
	if (self->client.tsap_ctrl)
		return;

	irda_notify_init(&notify);

	/* Set up callbacks */
	notify.data_indication       = irlan_client_ctrl_data_indication;
	notify.connect_confirm       = irlan_client_ctrl_connect_confirm;
	notify.disconnect_indication = irlan_client_ctrl_disconnect_indication;
	notify.instance = self;
	strncpy(notify.name, "IrLAN ctrl (c)", NOTIFY_MAX_NAME);
	
	tsap = irttp_open_tsap(LSAP_ANY, DEFAULT_INITIAL_CREDIT, &notify);
	if (!tsap) {
		DEBUG(2, __FUNCTION__ "(), Got no tsap!\n");
		return;
	}
	self->client.tsap_ctrl = tsap;
}

/*
 * Function irlan_client_connect_confirm (handle, skb)
 *
 *    Connection to peer IrLAN laye confirmed
 *
 */
static void irlan_client_ctrl_connect_confirm(void *instance, void *sap, 
					      struct qos_info *qos, 
					      __u32 max_sdu_size,
					      struct sk_buff *skb) 
{
	struct irlan_cb *self;

	DEBUG(4, __FUNCTION__ "()\n");

	self = (struct irlan_cb *) instance;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* TODO: we could set the MTU depending on the max_sdu_size */

	irlan_do_client_event(self, IRLAN_CONNECT_COMPLETE, NULL);
}

/*
 * Function irlan_client_reconnect_data_channel (self)
 *
 *    Try to reconnect data channel (currently not used)
 *
 */
void irlan_client_reconnect_data_channel(struct irlan_cb *self) 
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
	
 	frame[0] = CMD_RECONNECT_DATA_CHAN;
	frame[1] = 0x01;
 	irlan_insert_array_param(skb, "RECONNECT_KEY", 
				 self->client.reconnect_key,
				 self->client.key_len);
	
	irttp_data_request(self->client.tsap_ctrl, skb);	
}

/*
 * Function irlan_client_parse_response (self, skb)
 *
 *    Extract all parameters from received buffer, then feed them to 
 *    check_params for parsing
 */
void irlan_client_parse_response(struct irlan_cb *self, struct sk_buff *skb)
{
	__u8 *frame;
	__u8 *ptr;
	int count;
	int ret;
	__u16 val_len;
	int i;
        char *name;
        char *value;

	ASSERT(skb != NULL, return;);	
	
	DEBUG(4, __FUNCTION__ "() skb->len=%d\n", (int) skb->len);
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	
	if (!skb) {
		ERROR( __FUNCTION__ "(), Got NULL skb!\n");
		return;
	}
	frame = skb->data;
	
	/* 
	 *  Check return code and print it if not success 
	 */
	if (frame[0]) {
		print_ret_code(frame[0]);
		return;
	}
	
	name = kmalloc(255, GFP_ATOMIC);
	if (!name)
		return;
	value = kmalloc(1016, GFP_ATOMIC);
	if (!value) {
		kfree(name);
		return;
	}

	/* How many parameters? */
	count = frame[1];

	DEBUG(4, __FUNCTION__ "(), got %d parameters\n", count);
	
	ptr = frame+2;

	/* For all parameters */
 	for (i=0; i<count;i++) {
		ret = irlan_extract_param(ptr, name, value, &val_len);
		if (ret == -1) {
			DEBUG(2, __FUNCTION__ "(), IrLAN, Error!\n");
			break;
		}
		ptr+=ret;
		irlan_check_response_param(self, name, value, val_len);
 	}
	/* Cleanup */
	kfree(name);
	kfree(value);
}

/*
 * Function check_param (param, value)
 *
 *    Check which parameter is received and update local variables
 *
 */
static void irlan_check_response_param(struct irlan_cb *self, char *param, 
				       char *value, int val_len) 
{
	__u16 tmp_cpu; /* Temporary value in host order */
	__u8 *bytes;
	int i;

	DEBUG(4, __FUNCTION__ "(), parm=%s\n", param);

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/*
	 *  Media type
	 */
	if (strcmp(param, "MEDIA") == 0) {
		if (strcmp(value, "802.3") == 0)
			self->media = MEDIA_802_3;
		else
			self->media = MEDIA_802_5;
		return;
	}
	if (strcmp(param, "FILTER_TYPE") == 0) {
		if (strcmp(value, "DIRECTED") == 0)
			self->client.filter_type |= IRLAN_DIRECTED;
		else if (strcmp(value, "FUNCTIONAL") == 0)
			self->client.filter_type |= IRLAN_FUNCTIONAL;
		else if (strcmp(value, "GROUP") == 0)
			self->client.filter_type |= IRLAN_GROUP;
		else if (strcmp(value, "MAC_FRAME") == 0)
			self->client.filter_type |= IRLAN_MAC_FRAME;
		else if (strcmp(value, "MULTICAST") == 0)
			self->client.filter_type |= IRLAN_MULTICAST;
		else if (strcmp(value, "BROADCAST") == 0)
			self->client.filter_type |= IRLAN_BROADCAST;
		else if (strcmp(value, "IPX_SOCKET") == 0)
			self->client.filter_type |= IRLAN_IPX_SOCKET;
		
	}
	if (strcmp(param, "ACCESS_TYPE") == 0) {
		if (strcmp(value, "DIRECT") == 0)
			self->access_type = ACCESS_DIRECT;
		else if (strcmp(value, "PEER") == 0)
			self->access_type = ACCESS_PEER;
		else if (strcmp(value, "HOSTED") == 0)
			self->access_type = ACCESS_HOSTED;
		else {
			DEBUG(2, __FUNCTION__ "(), unknown access type!\n");
		}
	}
	/*
	 *  IRLAN version
	 */
	if (strcmp(param, "IRLAN_VER") == 0) {
		DEBUG(4, "IrLAN version %d.%d\n", (__u8) value[0], 
		      (__u8) value[1]);

		self->version[0] = value[0];
		self->version[1] = value[1];
		return;
	}
	/*
	 *  Which remote TSAP to use for data channel
	 */
	if (strcmp(param, "DATA_CHAN") == 0) {
		self->dtsap_sel_data = value[0];
		DEBUG(4, "Data TSAP = %02x\n", self->dtsap_sel_data);
		return;
	}
	if (strcmp(param, "CON_ARB") == 0) {
		memcpy(&tmp_cpu, value, 2); /* Align value */
		le16_to_cpus(&tmp_cpu);     /* Convert to host order */
		self->client.recv_arb_val = tmp_cpu;
		DEBUG(2, __FUNCTION__ "(), receive arb val=%d\n", 
		      self->client.recv_arb_val);
	}
	if (strcmp(param, "MAX_FRAME") == 0) {
		memcpy(&tmp_cpu, value, 2); /* Align value */
		le16_to_cpus(&tmp_cpu);     /* Convert to host order */
		self->client.max_frame = tmp_cpu;
		DEBUG(4, __FUNCTION__ "(), max frame=%d\n", 
		      self->client.max_frame);
	}
	 
	/*
	 *  RECONNECT_KEY, in case the link goes down!
	 */
	if (strcmp(param, "RECONNECT_KEY") == 0) {
		DEBUG(4, "Got reconnect key: ");
		/* for (i = 0; i < val_len; i++) */
/* 			printk("%02x", value[i]); */
		memcpy(self->client.reconnect_key, value, val_len);
		self->client.key_len = val_len;
		DEBUG(4, "\n");
	}
	/*
	 *  FILTER_ENTRY, have we got an ethernet address?
	 */
	if (strcmp(param, "FILTER_ENTRY") == 0) {
		bytes = value;
		DEBUG(4, "Ethernet address = %02x:%02x:%02x:%02x:%02x:%02x\n",
		      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], 
		      bytes[5]);
		for (i = 0; i < 6; i++) 
			self->dev.dev_addr[i] = bytes[i];
	}
}

/*
 * Function irlan_client_get_value_confirm (obj_id, value)
 *
 *    Got results from remote LM-IAS
 *
 */
void irlan_client_get_value_confirm(int result, __u16 obj_id, 
				    struct ias_value *value, void *priv) 
{
	struct irlan_cb *self;
	
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(priv != NULL, return;);

	self = (struct irlan_cb *) priv;
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Check if request succeeded */
	if (result != IAS_SUCCESS) {
		DEBUG(2, __FUNCTION__ "(), got NULL value!\n");
		irlan_do_client_event(self, IRLAN_IAS_PROVIDER_NOT_AVAIL, 
				      NULL);
		return;
	}

	switch (value->type) {
	case IAS_INTEGER:
		self->dtsap_sel_ctrl = value->t.integer;

		if (value->t.integer != -1) {
			irlan_do_client_event(self, IRLAN_IAS_PROVIDER_AVAIL,
					      NULL);
			return;
		}
		break;
	case IAS_STRING:
		DEBUG(2, __FUNCTION__ "(), got string %s\n", value->t.string);
		break;
	case IAS_OCT_SEQ:
		DEBUG(2, __FUNCTION__ "(), OCT_SEQ not implemented\n");
		break;
	case IAS_MISSING:
		DEBUG(2, __FUNCTION__ "(), MISSING not implemented\n");
		break;
	default:
		DEBUG(2, __FUNCTION__ "(), unknown type!\n");
		break;
	}
	irlan_do_client_event(self, IRLAN_IAS_PROVIDER_NOT_AVAIL, NULL);
}
