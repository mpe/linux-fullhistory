/*********************************************************************
 *                
 * Filename:      irlan_provider.c
 * Version:       0.9
 * Description:   IrDA LAN Access Protocol Implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Thu Feb  4 16:08:33 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:       skeleton.c by Donald Becker <becker@CESDIS.gsfc.nasa.gov>
 *                slip.c by Laurence Culhane,   <loz@holmes.demon.co.uk>
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
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>

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
#include <net/irda/irlan_eth.h>
#include <net/irda/irlan_event.h>
#include <net/irda/irlan_provider.h>
#include <net/irda/irlan_filter.h>
#include <net/irda/irlan_client.h>

/*
 * Function irlan_provider_control_data_indication (handle, skb)
 *
 *    This function gets the data that is received on the control channel
 *
 */
void irlan_provider_data_indication(void *instance, void *sap, 
				    struct sk_buff *skb) 
{
	struct irlan_cb *self;
	__u8 code;
	
	DEBUG(4, __FUNCTION__ "()\n");
	
	self = (struct irlan_cb *) instance;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	ASSERT(skb != NULL, return;);

	code = skb->data[0];
	switch(code) {
	case CMD_GET_PROVIDER_INFO:
		DEBUG(4, "Got GET_PROVIDER_INFO command!\n");
		irlan_do_provider_event(self, IRLAN_GET_INFO_CMD, skb); 
		break;

	case CMD_GET_MEDIA_CHAR:
		DEBUG(4, "Got GET_MEDIA_CHAR command!\n");
		irlan_do_provider_event(self, IRLAN_GET_MEDIA_CMD, skb); 
		break;
	case CMD_OPEN_DATA_CHANNEL:
		DEBUG(4, "Got OPEN_DATA_CHANNEL command!\n");
		irlan_do_provider_event(self, IRLAN_OPEN_DATA_CMD, skb); 
		break;
	case CMD_FILTER_OPERATION:
		DEBUG(4, "Got FILTER_OPERATION command!\n");
		irlan_do_provider_event(self, IRLAN_FILTER_CONFIG_CMD, skb);
		break;
	case CMD_RECONNECT_DATA_CHAN:
		DEBUG(2, __FUNCTION__"(), Got RECONNECT_DATA_CHAN command\n");
		DEBUG(2, __FUNCTION__"(), NOT IMPLEMENTED\n");
		break;
	case CMD_CLOSE_DATA_CHAN:
		DEBUG(2, "Got CLOSE_DATA_CHAN command!\n");
		DEBUG(2, __FUNCTION__"(), NOT IMPLEMENTED\n");
		break;
	default:
		DEBUG(2, __FUNCTION__ "(), Unknown command!\n");
		break;
	}
}

/*
 * Function irlan_provider_connect_indication (handle, skb, priv)
 *
 *    Got connection from peer IrLAN layer
 *
 */
void irlan_provider_connect_indication(void *instance, void *sap, 
				       struct qos_info *qos, int max_sdu_size,
				       struct sk_buff *skb)
{
	struct irlan_cb *self, *entry, *new;
	struct tsap_cb *tsap;

	DEBUG(2, __FUNCTION__ "()\n");
	
	self = (struct irlan_cb *) instance;
	tsap = (struct tsap_cb *) sap;
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	
	ASSERT(tsap == self->provider.tsap_ctrl,return;);
	ASSERT(self->provider.state == IRLAN_IDLE, return;);

	/* Check if this provider is unused */
	if (self->daddr == DEV_ADDR_ANY) {
		/*
		 * Rehash instance, now we have a client (daddr) to serve.
		 */
		entry = hashbin_remove(irlan, self->daddr, NULL);
		ASSERT( entry == self, return;);
		
		self->daddr = irttp_get_daddr(tsap);
		DEBUG(2, __FUNCTION__ "(), daddr=%08x\n", self->daddr);
		hashbin_insert(irlan, (QUEUE*) self, self->daddr, NULL);
	} else {
		/*
		 * If we already have the daddr set, this means that the
		 * client must already have started (peer mode). We must
		 * make sure that this connection attempt is from the same
		 * device as the client is dealing with!  
		 */
		ASSERT(self->daddr == irttp_get_daddr(tsap), return;);
	}
	
	/* Update saddr, since client may have moved to a new link */
	self->saddr = irttp_get_saddr(tsap);
	DEBUG(2, __FUNCTION__ "(), saddr=%08x\n", self->saddr);
	
	/* Check if network device has been registered */
	if (!self->netdev_registered)
		irlan_register_netdev(self);
	
	irlan_do_provider_event(self, IRLAN_CONNECT_INDICATION, NULL);

	/*  
	 * If we are in peer mode, the client may not have got the discovery
	 * indication it needs to make progress. If the client is still in 
	 * IDLE state, we must kick it to 
	 */
	if ((self->access_type == ACCESS_PEER) && 
	    (self->client.state == IRLAN_IDLE))
		irlan_client_wakeup(self, self->saddr, self->daddr);

	/* 
	 * This provider is now in use, so start a new provider instance to
         * serve other clients. This will also change the LM-IAS entry so that
	 * other clients don't try to connect to us, now that we are busy.
	 */
	new = irlan_open(DEV_ADDR_ANY, DEV_ADDR_ANY, FALSE);
	self->client.start_new_provider = FALSE;
}

/*
 * Function irlan_provider_connect_response (handle)
 *
 *    Accept incomming connection
 *
 */
void irlan_provider_connect_response(struct irlan_cb *self,
				     struct tsap_cb *tsap)
{
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Just accept */
	irttp_connect_response(tsap, IRLAN_MTU, NULL);

	/* Check if network device has been registered */
	if (!self->netdev_registered)
		irlan_register_netdev(self);
		
}

void irlan_provider_disconnect_indication(void *instance, void *sap, 
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
	
	ASSERT(tsap == self->provider.tsap_ctrl, return;);
	
	irlan_do_provider_event(self, IRLAN_LMP_DISCONNECT, NULL);
}

/*
 * Function irlan_parse_open_data_cmd (self, skb)
 *
 *    
 *
 */
int irlan_parse_open_data_cmd(struct irlan_cb *self, struct sk_buff *skb)
{
	int ret;
	
	ret = irlan_provider_extract_params(self, CMD_OPEN_DATA_CHANNEL, skb);

	return ret;
}

/*
 * Function extract_params (skb)
 *
 *    Extract all parameters from received buffer, then feed them to 
 *    check_params for parsing
 *
 */
int irlan_provider_extract_params(struct irlan_cb *self, int cmd,
				  struct sk_buff *skb) 
{
	__u8 *frame;
	__u8 *ptr;
	int count;
	__u8 name_len;
	__u16 val_len;
	int i;
	char *name;
        char *value;
	int ret = RSP_SUCCESS;
	
	ASSERT(skb != NULL, return -RSP_PROTOCOL_ERROR;);
	
	DEBUG(4, __FUNCTION__ "(), skb->len=%d\n", (int)skb->len);

	ASSERT(self != NULL, return -RSP_PROTOCOL_ERROR;);
	ASSERT(self->magic == IRLAN_MAGIC, return -RSP_PROTOCOL_ERROR;);
	
	if (!skb)
		return -RSP_PROTOCOL_ERROR;

	frame = skb->data;

	name = kmalloc(255, GFP_ATOMIC);
	if (!name)
		return -RSP_INSUFFICIENT_RESOURCES;
	value = kmalloc(1016, GFP_ATOMIC);
	if (!value) {
		kfree(name);
		return -RSP_INSUFFICIENT_RESOURCES;
	}

	/* How many parameters? */
	count = frame[1];

	DEBUG(4, "Got %d parameters\n", count);
	
	ptr = frame+2;
	
	/* For all parameters */
 	for (i=0; i<count;i++) {
		ret = irlan_get_param(ptr, name, value, &val_len);
		if (ret < 0) {
			DEBUG(2, __FUNCTION__ "(), IrLAN, Error!\n");
			break;
		}
		ptr+=ret;
		ret = RSP_SUCCESS;
		irlan_check_command_param(self, name, value);
	}
	/* Cleanup */
	kfree(name);
	kfree(value);

	return ret;
}

/*
 * Function irlan_provider_send_reply (self, info)
 *
 *    Send reply to query to peer IrLAN layer
 *
 */
void irlan_provider_send_reply(struct irlan_cb *self, int command, 
			       int ret_code)
{
	struct sk_buff *skb;

	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = dev_alloc_skb(128);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put(skb, 2);
       
	switch (command) {
	case CMD_GET_PROVIDER_INFO:
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x02; /* 2 parameters */
		switch (self->media) {
		case MEDIA_802_3:
			irlan_insert_string_param(skb, "MEDIA", "802.3");
			break;
		case MEDIA_802_5:
			irlan_insert_string_param(skb, "MEDIA", "802.5");
			break;
		default:
			DEBUG(2, __FUNCTION__ "(), unknown media type!\n");
			break;
		}
		irlan_insert_short_param(skb, "IRLAN_VER", 0x0101);
		break;
	case CMD_GET_MEDIA_CHAR:
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x05; /* 5 parameters */
		irlan_insert_string_param(skb, "FILTER_TYPE", "DIRECTED");
		irlan_insert_string_param(skb, "FILTER_TYPE", "BROADCAST");
		irlan_insert_string_param(skb, "FILTER_TYPE", "MULTICAST");

		switch(self->access_type) {
		case ACCESS_DIRECT:
			irlan_insert_string_param(skb, "ACCESS_TYPE", "DIRECT");
			break;
		case ACCESS_PEER:
			irlan_insert_string_param(skb, "ACCESS_TYPE", "PEER");
			break;
		case ACCESS_HOSTED:
			irlan_insert_string_param(skb, "ACCESS_TYPE", "HOSTED");
			break;
		default:
			DEBUG(2, __FUNCTION__ "(), Unknown access type\n");
			break;
		}
		irlan_insert_short_param(skb, "MAX_FRAME", 0x05ee);
		break;
	case CMD_OPEN_DATA_CHANNEL:
		skb->data[0] = 0x00; /* Success */
		if (self->provider.send_arb_val) {
			skb->data[1] = 0x03; /* 3 parameters */
			irlan_insert_short_param(skb, "CON_ARB", 
						 self->provider.send_arb_val);
		} else
			skb->data[1] = 0x02; /* 2 parameters */
		irlan_insert_byte_param(skb, "DATA_CHAN", self->stsap_sel_data);
		irlan_insert_array_param(skb, "RECONNECT_KEY", "LINUX RULES!",
					 12);
		break;
	case CMD_FILTER_OPERATION:
		handle_filter_request(self, skb);
		break;
	default:
		DEBUG(2, __FUNCTION__ "(), Unknown command!\n");
		break;
	}

	irttp_data_request(self->provider.tsap_ctrl, skb);
}

/*
 * Function irlan_provider_register(void)
 *
 *    Register provider support so we can accept incomming connections.
 * 
 */
void irlan_provider_open_ctrl_tsap(struct irlan_cb *self)
{
	struct notify_t notify;
	struct tsap_cb *tsap;
	
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Check if already open */
	if (self->provider.tsap_ctrl)
		return;
	
	/*
	 *  First register well known control TSAP
	 */
	irda_notify_init(&notify);
	notify.data_indication       = irlan_provider_data_indication;
	notify.connect_indication    = irlan_provider_connect_indication;
	notify.disconnect_indication = irlan_provider_disconnect_indication;
	notify.instance = self;
	strncpy(notify.name, "IrLAN ctrl (p)", 16);

	tsap = irttp_open_tsap(LSAP_ANY, 1, &notify);
	if (!tsap) {
		DEBUG(2, __FUNCTION__ "(), Got no tsap!\n");
		return;
	}
	self->provider.tsap_ctrl = tsap;

	/* Register with LM-IAS */
	irlan_ias_register(self,tsap->stsap_sel);
}

