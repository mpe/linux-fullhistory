/*********************************************************************
 *                
 * Filename:      irlan_cli.c
 * Version:       0.8
 * Description:   IrDA LAN Access Protocol Client
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Mon Jan 18 13:24:26 1999
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

#include <linux/module.h>

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
#include <net/irda/irlan_cli.h>

/*
 *  Private functions
 */
static struct irlan_cb *irlan_client_open( __u32 saddr, __u32 daddr);
static void irlan_client_close( struct irlan_cb *self);


static int irlan_client_eth_open( struct device *dev)
{
	/* struct irlan_cb *self = (struct irlan_cb *) dev->priv; */
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( dev != NULL, return -1;);

	/* Ready to play! */
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	MOD_INC_USE_COUNT;
	
	return 0;
}

/*
 * Function irlan_eth_close (dev)
 *
 *    Stop the Client ether network device, his function will be called by
 *    ifconfig down.
 */
static int irlan_client_eth_close(struct device *dev)
{
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( dev != NULL, return -1;);
	
	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	MOD_DEC_USE_COUNT;
	
	return 0;
}

/*
 * Function irlan_client_eth_init (dev)
 *
 *    
 *
 */
int irlan_client_eth_init( struct device *dev) 
{
	irlan_eth_init( dev);

	/* Overrride some functions */
	dev->open  = irlan_client_eth_open;
	dev->stop = irlan_client_eth_close;

	return 0;
}

/*
 * Function irlan_client_init (dev)
 *
 *   Allocates the master array. Called by modprobe().
 */
__initfunc(int irlan_client_init( void))
{
	DEBUG( 4, __FUNCTION__ "()\n");
	
	/* Register with IrLMP as a service user */
	irlmp_register_layer( S_LAN, CLIENT, TRUE, 
			      irlan_discovery_indication);

	/* Do some fast discovery! */
	irlmp_discovery_request( 8);

	return 0;
}

/*
 * Function irlan_client_cleanup (void)
 *
 *    Removes all instances of the IrLAN network device driver, and the
 *    master array. Called by rmmod().
 *
 */
void irlan_client_cleanup(void) 
{
	DEBUG( 4, __FUNCTION__ "()\n");

	irlmp_unregister_layer( S_LAN, CLIENT);
}

/*
 * Function irlan_client_open (void)
 *
 *    This function allocates and opens a new instance of the IrLAN network
 *    device driver.
 *
 */
static struct irlan_cb *irlan_client_open( __u32 saddr, __u32 daddr) 
{
	struct irlan_cb *self;
	int result;

	DEBUG( 4, "IrLAN: irlan_client_open()\n");

	ASSERT( irlan != NULL, return NULL;);
	
	self = irlan_open();
	if ( self == NULL)
		return NULL;

	/* Use default name instead! */
	/* sprintf( self->ifname, "irlan%d", ); */
 	self->dev.name = self->ifname;
	self->dev.priv = (void *) self;
	self->dev.next = NULL;
	self->dev.init = irlan_client_eth_init;
	
	self->saddr = saddr;
	self->daddr = daddr;

	/*
	 *  Insert ourself into the hashbin
	 */
	hashbin_insert( irlan, (QUEUE *) self, saddr, NULL);

	if (( result = register_netdev( &self->dev)) != 0) {
		DEBUG( 0, "IrLAN, Register_netdev() failed!\n");
		return NULL;
	}

	irlan_next_state( self, IRLAN_IDLE);

	irlan_client_open_tsaps( self);
	
	return self;
}

/*
 * Function irlan_client_close (self)
 *
 *    
 *
 */
static void irlan_client_close( struct irlan_cb *self)
{
	struct irlan_cb *entry;

	DEBUG( 0, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	entry = hashbin_remove( irlan, self->daddr, NULL);

	ASSERT( entry == self, return;);

	/* __irlan_close( self); */
}

/*
 * Function irlan_discovery_indication (daddr)
 *
 *    Remote device with IrLAN server support discovered
 *
 */
void irlan_discovery_indication( DISCOVERY *discovery) 
{
	struct irlan_cb *self;
	__u32 saddr, daddr;
	
	ASSERT( irlan != NULL, return;);
	ASSERT( discovery != NULL, return;);

	saddr = discovery->saddr;
	daddr = discovery->daddr;

	/* 
	 *  Check if an instance is already dealing with this device
	 *  (saddr) 
	 */
	self = (struct irlan_cb *) hashbin_find( irlan, saddr, NULL);
      	if ( self != NULL) {
		ASSERT( self->magic == IRLAN_MAGIC, return;);

		DEBUG( 4, __FUNCTION__ "(), Found instance!\n");
		if ( self->state == IRLAN_IDLE) {
			/* daddr may have changed! */
			self->daddr = daddr;

			irlan_do_client_event( self, 
					       IRLAN_DISCOVERY_INDICATION, 
					       NULL);
		} else {
			DEBUG( 0, __FUNCTION__ "(), state=%s\n", 
			       irlan_state[ self->state]);
			/*  
			 *  If we get here, it's obvious that the last 
			 *  connection attempt has failed, so its best 
			 *  to go back to idle!
			 */
			irlan_do_client_event( self, IRLAN_LMP_DISCONNECT, 
					       NULL);
		}
		return;
	}
	
	/* 
	 * We have no instance for daddr, so time to start a new instance.
	 */
	DEBUG( 0, __FUNCTION__ "(), Opening new instance for saddr=%#x\n",
	       saddr);

	if (( self = irlan_client_open( saddr, daddr)) == NULL) {
		DEBUG( 0, "irlan_client_open failed!\n");
		return;
	}	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	DEBUG( 4, "Setting irlan_client state!\n");
	if ( self->state == IRLAN_IDLE) {
		irlan_do_client_event( self, IRLAN_DISCOVERY_INDICATION, NULL);
	} else {
		DEBUG( 0, __FUNCTION__ "(), Hmm, got here too!\n");
	}
}

/*
 * Function irlan_client_disconnect_indication (handle)
 *
 *    Callback function for the IrTTP layer. Indicates a disconnection of
 *    the specified connection (handle)
 */
void irlan_client_disconnect_indication( void *instance, void *sap, 
					 LM_REASON reason, 
					 struct sk_buff *userdata) 
{
	struct irlan_cb *self;
	struct tsap_cb *tsap;

	self = ( struct irlan_cb *) instance;
	tsap = ( struct tsap_cb *) sap;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);	
	ASSERT( tsap != NULL, return;);
	ASSERT( tsap->magic == TTP_TSAP_MAGIC, return;);
	
	DEBUG( 4, __FUNCTION__ "(), reason=%d\n", reason);
	
	if ( tsap == self->tsap_data) {
		DEBUG( 4, "IrLAN, data channel disconnected by peer!\n");
		self->connected = FALSE;
	} else if ( tsap == self->tsap_ctrl) {
		DEBUG( 4, "IrLAN, control channel disconnected by peer!\n");
	} else {
		DEBUG( 0, "Error, disconnect on unknown handle!\n");
	}
	
	/* Stop IP from transmitting more packets */
	/* irlan_client_flow_indication( handle, FLOW_STOP, priv); */

	irlan_do_client_event( self, IRLAN_LMP_DISCONNECT, NULL);
}
	
/*
 * Function irlan_control_data_indication (handle, skb)
 *
 *    This function gets the data that is received on the control channel
 *
 */
void irlan_client_ctrl_data_indication( void *instance, void *sap, 
					struct sk_buff *skb)
{
	struct irlan_cb *self;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	self = ( struct irlan_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	
	DEBUG( 4, "Got IRLAN_DATA_INDICATION!\n");
	irlan_do_client_event( self, IRLAN_DATA_INDICATION, skb); 
}

/*
 * Function irlan_client_open_tsaps (self)
 *
 *    Initialize callbacks and open IrTTP TSAPs
 *
 */
void irlan_client_open_tsaps( struct irlan_cb *self) 
{
	/* struct irlan_frame frame; */
	struct notify_t notify_ctrl;
	struct notify_t notify_data;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	irda_notify_init( &notify_ctrl);
	irda_notify_init( &notify_data);

	/* Set up callbacks */
	notify_ctrl.data_indication       = irlan_client_ctrl_data_indication;
	notify_ctrl.connect_confirm       = irlan_client_connect_confirm;
	notify_ctrl.disconnect_indication = irlan_client_disconnect_indication;
	notify_ctrl.instance              = self;
	strncpy( notify_ctrl.name, "IrLAN ctrl", NOTIFY_MAX_NAME);
	
	notify_data.data_indication       = irlan_eth_rx;
	notify_data.udata_indication      = irlan_eth_rx;
	notify_data.connect_confirm       = irlan_client_connect_confirm;
 	notify_data.flow_indication       = irlan_eth_flow_indication;
	notify_data.disconnect_indication = irlan_client_disconnect_indication;
	notify_data.instance              = self;
	strncpy( notify_data.name, "IrLAN data", NOTIFY_MAX_NAME);

	/* Create TSAP's */
	self->tsap_ctrl = irttp_open_tsap( LSAP_ANY, 
					   DEFAULT_INITIAL_CREDIT, 
					   &notify_ctrl);
	self->tsap_data = irttp_open_tsap( LSAP_ANY, 
					   DEFAULT_INITIAL_CREDIT,
					   &notify_data);
}

/*
 * Function irlan_client_connect_confirm (handle, skb)
 *
 *    Connection to peer IrLAN laye confirmed
 *
 */
void irlan_client_connect_confirm( void *instance, void *sap, 
				   struct qos_info *qos, int max_sdu_size,
				   struct sk_buff *skb) 
{
	struct irlan_cb *self;

	DEBUG( 4, __FUNCTION__ "()\n");

	self = ( struct irlan_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	/* TODO: we could set the MTU depending on the max_sdu_size */

	irlan_do_client_event( self, IRLAN_CONNECT_COMPLETE, NULL);
}

/*
 * Function irlan_client_reconnect_data_channel (self)
 *
 *    Try to reconnect data channel (currently not used)
 *
 */
void irlan_client_reconnect_data_channel( struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
		
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	skb =  dev_alloc_skb( 128);
	if (skb == NULL) {
		DEBUG( 0, __FUNCTION__
		       "(), Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put( skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_RECONNECT_DATA_CHAN;
	frame[1] = 0x01;
 	insert_array_param( skb, "RECONNECT_KEY", 
			    self->t.client.reconnect_key, 
			    self->t.client.key_len);
	
	irttp_data_request( self->tsap_ctrl, skb);	
}

/*
 * Function irlan_client_extract_params (skb)
 *
 *    Extract all parameters from received buffer, then feed them to 
 *    check_params for parsing
 *
 */
void irlan_client_extract_params( struct irlan_cb *self, 
				  struct sk_buff *skb)
{
	__u8 *frame;
	__u8 *ptr;
	int count;
	int ret;
	int val_len;
	int i;

	ASSERT( skb != NULL, return;);	
	
	DEBUG( 4, __FUNCTION__ "() skb->len=%d\n", (int) skb->len);
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	if ( skb == NULL) {
		DEBUG( 0, __FUNCTION__ "(), Got NULL skb!\n");
		return;
	}
	frame = skb->data;

	/* 
	 *  Check return code and print it if not success 
	 */
	if ( frame[0])
		print_ret_code( frame[0]);
	
	/* How many parameters? */
	count = frame[1];

	DEBUG( 4, "Got %d parameters\n", count);
	
	ptr = frame+2;

	/* For all parameters */
 	for ( i=0; i<count;i++) {
		ret = irlan_get_response_param( ptr, self->name, self->value, 
						&val_len);
		if ( ret == -1) {
			DEBUG( 0, __FUNCTION__ "(), IrLAN, Error!\n");
			return;
		}
		ptr+=ret;
		check_response_param( self, self->name, self->value, val_len);
 	}
}

/*
 * Function check_param (param, value)
 *
 *    Check which parameter is received and update local variables
 *
 */
void check_response_param( struct irlan_cb *self, char *param, 
			   char *value, int val_len) 
{
	int i;
	__u8 *bytes;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	/*
	 *  Media type
	 */
	if ( strcmp( param, "MEDIA") == 0) {
		if ( strcmp( value, "802.3") == 0)
			self->media = MEDIA_802_3;
		else
			self->media = MEDIA_802_5;
		return;
	}
	/*
	 *  IRLAN version
	 */
	if ( strcmp( param, "IRLAN_VER") == 0) {
		DEBUG( 4, "IrLAN version %d.%d\n", 
			(__u8) value[0], (__u8) value[1]);
		return;
	}
	/*
	 *  Which remote TSAP to use for data channel
	 */
	if ( strcmp( param, "DATA_CHAN") == 0) {
		self->dtsap_sel_data = value[0];
		DEBUG( 4, "Data TSAP = %02x\n", self->dtsap_sel_data);
		return;
	}
	/*
	 *  RECONNECT_KEY, in case the link goes down!
	 */
	if ( strcmp( param, "RECONNECT_KEY") == 0) {
		DEBUG( 4, "Got reconnect key: ");
		/* for (i = 0; i < val_len; i++) */
/* 			printk( "%02x", value[i]); */
		memcpy( self->t.client.reconnect_key, value, val_len);
		self->t.client.key_len = val_len;
		DEBUG( 4, "\n");
	}
	/*
	 *  FILTER_ENTRY, have we got the ethernet address?
	 */
	if ( strcmp( param, "FILTER_ENTRY") == 0) {
		bytes = value;
		DEBUG( 4, "Ethernet address = %02x:%02x:%02x:%02x:%02x:%02x\n",
		       bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], 
		       bytes[5]);
		for (i = 0; i < 6; i++) 
			self->dev.dev_addr[i] = bytes[i];
#if 0
	/*
	 * When we get a new MAC address do a gratuitous ARP. This is useful
	 * if we have changed access points on the same subnet.
	 */
		DEBUG( 4, "IrLAN: Sending gratuitous ARP\n");
		arp_send( ARPOP_REQUEST, ETH_P_ARP, self->dev.pa_addr,
			&self->dev, self->dev.pa_addr, NULL,
			self->dev.dev_addr, NULL);
#endif
	}
}

/*
 * Function irlan_client_get_value_confirm (obj_id, value)
 *
 *    Got results from previous GetValueByClass request
 *
 */
void irlan_client_get_value_confirm( __u16 obj_id, struct ias_value *value,
				     void *priv) 
{
	struct irlan_cb *self;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( priv != NULL, return;);

	self = ( struct irlan_cb *) priv;

	ASSERT( self->magic == IRLAN_MAGIC, return;);

	switch ( value->type) {
	case IAS_INTEGER:
		self->dtsap_sel_ctrl = value->t.integer;

		if ( value->t.integer != -1) {
			irlan_do_client_event( self, IRLAN_IAS_PROVIDER_AVAIL,
					       NULL);
			return;
		}
		break;
	case IAS_STRING:
		DEBUG( 0, __FUNCTION__ "(), got string %s\n", value->t.string);
		break;
	case IAS_OCT_SEQ:
		DEBUG( 0, __FUNCTION__ "(), OCT_SEQ not implemented\n");
		break;
	case IAS_MISSING:
		DEBUG( 0, __FUNCTION__ "(), MISSING not implemented\n");
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), unknown type!\n");
		break;
	}
	irlan_do_client_event( self, IRLAN_IAS_PROVIDER_NOT_AVAIL, NULL);
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("The Linux IrDA LAN protocol"); 

/*
 * Function init_module (void)
 *
 *    Initialize the IrLAN module, this function is called by the
 *    modprobe(1) program.
 */
int init_module(void) 
{
	DEBUG( 4, __FUNCTION__ "(), irlan_client.c\n");

	irlan_client_init();

	return 0;
}

/*
 * Function cleanup_module (void)
 *
 *    Remove the IrLAN module, this function is called by the rmmod(1)
 *    program
 */
void cleanup_module(void) 
{
	DEBUG( 4, "--> irlan, cleanup_module\n");
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */

	/* Free some memory */
 	irlan_client_cleanup();

	DEBUG( 4, "irlan, cleanup_module -->\n");
}

#endif /* MODULE */









