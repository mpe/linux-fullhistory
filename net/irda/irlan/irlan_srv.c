/*********************************************************************
 *                
 * Filename:      irlan_srv.c
 * Version:       0.1
 * Description:   IrDA LAN Access Protocol Implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Mon Dec 14 19:10:49 1998
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

#include <linux/module.h> 

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
#include <net/irda/irlan_srv.h>

/*
 *  Private functions
 */
static void __irlan_server_close( struct irlan_cb *self);
static int irlan_server_dev_init( struct device *dev);
static int irlan_server_dev_open(struct device *dev);
static int irlan_server_dev_close(struct device *dev);
static void irlan_check_param( struct irlan_cb *self, char *param, char *value);

/*
 * Function irlan_server_init (dev)
 *
 *   Allocates the master array. Called by modprobe().
 */
__initfunc(int irlan_server_init( void))
{
	DEBUG( 4, "--> irlan_server_init\n");

	/* Register with IrLMP as a service */
	irlmp_register_layer( S_LAN, SERVER, FALSE, NULL);
	
 	irlan_server_register();
		
	DEBUG( 4, "irlan_server_init -->\n");
		
	return 0;
}

/*
 * Function irlan_server_cleanup (void)
 *
 *    Removes all instances of the IrLAN network device driver, and the
 *    master array. Called by rmmod().
 */
void irlan_server_cleanup(void) 
{
	DEBUG( 4, "--> irlan_server_cleanup\n");

	irlmp_unregister_layer( S_LAN, SERVER);

	/*
	 *  Delete hashbin and close all irlan client instances in it
	 */
	/* hashbin_delete( irlan, (FREE_FUNC) __irlan_server_close); */
	
	DEBUG( 4, "irlan_server_cleanup -->\n");
}

/*
 * Function irlan_server_open (void)
 *
 *    This function allocates and opens a new instance of the IrLAN network
 *    device driver.
 */
struct irlan_cb *irlan_server_open(void) 
{
	struct irlan_cb *self;
	int result;

	DEBUG( 4, __FUNCTION__ "()\n");

	/* 
	 *  Initialize the irlan_server structure. 
	 */

	self = kmalloc( sizeof(struct irlan_cb), GFP_ATOMIC);
	if ( self == NULL) {
		DEBUG( 0, __FUNCTION__ "(), Unable to kmalloc!\n");
		return NULL;
	}
	memset( self, 0, sizeof(struct irlan_cb));

	/*
	 *  Initialize local device structure
	 */
	self->magic = IRLAN_MAGIC;

	self->dev.name = self->ifname;
	self->dev.init = irlan_server_dev_init;
	self->dev.priv = (void *) self;
	self->dev.next = NULL;

	if (( result = register_netdev( &self->dev)) != 0) {
		DEBUG( 0, __FUNCTION__ "(), register_netdev() failed!\n");
		return NULL;
	}

	irlan_next_state( self, IRLAN_IDLE);

	hashbin_insert( irlan , (QUEUE *) self, (int) self, NULL);
	
	return self;
}

/*
 * Function irlan_server_dev_init (dev)
 *
 *    The network device initialization function. Called only once.
 *
 */
static int irlan_server_dev_init( struct device *dev)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( dev != NULL, return -1;);
       
	irlan_eth_init( dev);

	/* Overrride some functions */
	dev->open               = irlan_server_dev_open;
	dev->stop	        = irlan_server_dev_close;
	
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

	return 0;
}

/*
 * Function irlan_server_dev_open (dev)
 *
 *    Start the Servers ether network device, this function will be called by
 *    "ifconfig server0 up".  
 */
static int irlan_server_dev_open( struct device *dev)
{
	/* struct irlan_cb *self = (struct irlan_cb *) dev->priv; */
	
	DEBUG( 4, "irlan_server_dev_open()\n");

	ASSERT( dev != NULL, return -1;);

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	MOD_INC_USE_COUNT;
	
	return 0;
}

static void __irlan_server_close( struct irlan_cb *self)
{
	DEBUG( 4, "--> irlan_server_close()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	/* 
	 *  Disconnect open TSAP connections
	 */
	if ( self->tsap_data) {
		irttp_disconnect_request( self->tsap_data, NULL, P_HIGH);
		
		/* FIXME: this will close the tsap before the disconenct
		 * frame has been sent 
		 */
		/* irttp_close_tsap( self->tsap_data); */
	}
	if ( self->tsap_ctrl) {
		irttp_disconnect_request( self->tsap_ctrl, NULL, P_HIGH);
		
		/* irttp_close_tsap( self->tsap_control); */
	}

	unregister_netdev( &self->dev);

	self->magic = ~IRLAN_MAGIC;
	
	kfree( self);

	DEBUG( 4, "irlan_server_close() -->\n");
}

/*
 * Function irlan_server_close (self)
 *
 *    This function closes and marks the IrLAN instance as not in use. 
 */
void irlan_server_close( struct irlan_cb *self)
{
	struct irlan_cb *entry;

	DEBUG( 4, "--> irlan_server_close()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	entry = hashbin_remove( irlan, (int) self, NULL);

	ASSERT( entry == self, return;);
	
	__irlan_server_close( self);

	DEBUG( 4, "irlan_server_close() -->\n");	
}

/*
 * Function irlan_server_dev_close (dev)
 *
 *    Stop the IrLAN ether network device, his function will be called by
 *    ifconfig down.
 */
static int irlan_server_dev_close( struct device *dev)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( dev != NULL, return -1;);
	
	dev->tbusy = 1;
	dev->start = 0;

	MOD_DEC_USE_COUNT;
	
	return 0;
}


/*
 * Function irlan_server_disconnect_indication (handle, reason, priv)
 *
 *    Callback function for the IrTTP layer. Indicates a disconnection of
 *    the specified connection (handle)
 *
 */
void irlan_server_disconnect_indication( void *instance, void *sap, 
					 LM_REASON reason, 
					 struct sk_buff *skb) 
{
	struct irlan_info info;
	struct irlan_cb *self;
	struct tsap_cb *tsap;
	
	DEBUG( 4, __FUNCTION__ "(), Reason=%d\n", reason);
	
	self = ( struct irlan_cb *) instance;
	tsap = ( struct tsap_cb *) sap;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);	

	info.daddr = self->daddr;
	
	if ( tsap == self->tsap_data) {
		DEBUG( 0, "IrLAN, data channel disconnected by peer!\n");
		self->connected = FALSE;
	} else if ( tsap == self->tsap_ctrl) {
		DEBUG( 0, "IrLAN, control channel disconnected by peer!\n");
	} else {
		DEBUG( 0, "Error, disconnect on unknown handle!\n");
	}
	
	/* Stop IP from transmitting more packets */
	/* irlan_flow_indication( handle, FLOW_STOP, priv); */

	irlan_do_server_event( self, IRLAN_LMP_DISCONNECT, NULL, NULL);
}
	
/*
 * Function irlan_server_control_data_indication (handle, skb)
 *
 *    This function gets the data that is received on the control channel
 *
 */
void irlan_server_control_data_indication( void *instance, void *sap, 
					   struct sk_buff *skb) 
{
	struct irlan_cb *self;
	__u8 code;
	
	DEBUG( 4, __FUNCTION__ "()\n");
	
	self = ( struct irlan_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	ASSERT( skb != NULL, return;);

	code = skb->data[0];
	switch( code) {
	case CMD_GET_PROVIDER_INFO:
		DEBUG( 4, "Got GET_PROVIDER_INFO command!\n");
		irlan_do_server_event( self, IRLAN_GET_INFO_CMD, skb, NULL); 
		break;

	case CMD_GET_MEDIA_CHAR:
		DEBUG( 4, "Got GET_MEDIA_CHAR command!\n");
		irlan_do_server_event( self, IRLAN_GET_MEDIA_CMD, skb, NULL); 
		break;
	case CMD_OPEN_DATA_CHANNEL:
		DEBUG( 4, "Got OPEN_DATA_CHANNEL command!\n");
		irlan_do_server_event( self, IRLAN_OPEN_DATA_CMD, skb, NULL); 
		break;
	case CMD_FILTER_OPERATION:
		DEBUG( 4, "Got FILTER_OPERATION command!\n");
		irlan_do_server_event( self, IRLAN_FILTER_CONFIG_CMD, skb, 
				       NULL);
		break;
	case CMD_RECONNECT_DATA_CHAN:
		DEBUG( 0, __FUNCTION__"(), Got RECONNECT_DATA_CHAN command\n");
		DEBUG( 0, __FUNCTION__"(), NOT IMPLEMENTED\n");
		break;
	case CMD_CLOSE_DATA_CHAN:
		DEBUG( 0, "Got CLOSE_DATA_CHAN command!\n");
		DEBUG( 0, __FUNCTION__"(), NOT IMPLEMENTED\n");
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown command!\n");
		break;
	}
}

/*
 * Function irlan_server_connect_indication (handle, skb, priv)
 *
 *    Got connection from peer IrLAN layer
 *
 */
void irlan_server_connect_indication( void *instance, void *sap, 
				      struct qos_info *qos, int max_sdu_size,
				      struct sk_buff *skb)
{
	struct irlan_cb *self;
	struct irlan_info info;
	struct tsap_cb *tsap;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	self = ( struct irlan_cb *) instance;
	tsap = ( struct tsap_cb *) sap;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	info.tsap = tsap;
	
	if ( tsap == self->tsap_data)
		irlan_do_server_event( self, IRLAN_DATA_CONNECT_INDICATION, 
				       NULL, &info);
	else
		irlan_do_server_event( self, IRLAN_CONNECT_INDICATION, NULL, 
				       &info);
}

/*
 * Function irlan_server_connect_response (handle)
 *
 *    Accept incomming connection
 *
 */
void irlan_server_connect_response( struct irlan_cb *self, 
				    struct tsap_cb *tsap)
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	/* FIXME: define this value */
	irttp_connect_response( tsap, 1518, NULL);
}

/*
 * Function irlan_server_get_provider_info (self)
 *
 *    Send Get Provider Information command to peer IrLAN layer
 *
 */
void irlan_server_get_provider_info( struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	skb =  dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0,"irlan_server_get_provider_info: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put( skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_GET_PROVIDER_INFO;
	frame[1] = 0x00;                 /* Zero parameters */
	
	irttp_data_request( self->tsap_ctrl, skb);
}

/*
 * Function irlan_parse_open_data_cmd (self, skb)
 *
 *    
 *
 */
int irlan_parse_open_data_cmd( struct irlan_cb *self, struct sk_buff *skb)
{
	int ret = RSP_SUCCESS;
	
	irlan_server_extract_params( self, CMD_OPEN_DATA_CHANNEL, skb);

	return ret;
}

/*
 * Function extract_params (skb)
 *
 *    Extract all parameters from received buffer, then feed them to 
 *    check_params for parsing
 *
 */
int irlan_server_extract_params( struct irlan_cb *self, int cmd,
				struct sk_buff *skb) 
{
	__u8 *frame;
	__u8 *ptr;
	int count;
	__u8 name_len;
	__u16 val_len;
	int i;
	
	ASSERT( skb != NULL, return -RSP_PROTOCOL_ERROR;);
	
	DEBUG( 4, __FUNCTION__ "(), skb->len=%d\n", (int)skb->len);

	ASSERT( self != NULL, return -RSP_PROTOCOL_ERROR;);
	ASSERT( self->magic == IRLAN_MAGIC, return -RSP_PROTOCOL_ERROR;);
	
	if ( skb == NULL) {
		DEBUG( 0, "extract_params: Got NULL skb!\n");
		return -RSP_PROTOCOL_ERROR;
	}
	frame = skb->data;

	/* How many parameters? */
	count = frame[1];

	DEBUG( 4, "Got %d parameters\n", count);
	
	ptr = frame+2;

	/* For all parameters */
 	for ( i=0; i<count;i++) {
		/* get length of parameter name ( 1 byte) */
		name_len = *ptr++;

		if (name_len > 255) {
			DEBUG( 0, "extract_params, name_len > 255\n");
			return -RSP_PROTOCOL_ERROR;
		}

		/* get parameter name */
		memcpy( self->name, ptr, name_len);
		self->name[ name_len] = '\0';
		ptr+=name_len;
		
		/*  
		 *  Get length of parameter value ( 2 bytes in little endian 
		 *  format) 
		 */
		val_len = *ptr++ & 0xff;	
		val_len |= *ptr++ << 8;
		
		if (val_len > 1016) {
			DEBUG( 0, 
			       "extract_params, parameter length to long\n");
			return -RSP_PROTOCOL_ERROR;
		}
		
		/* get parameter value */
 		memcpy( self->value, ptr, val_len);
 		self->value[ val_len] = '\0';
 		ptr+=val_len;
		
 		DEBUG( 4, "Parameter: %s ", self->name); 
 		DEBUG( 4, "Value: %s\n", self->value); 
		
		irlan_check_param( self, self->name, self->value);
	}
	return RSP_SUCCESS;
}

/*
 * Function handle_filter_request (self, skb)
 *
 *    Handle filter request from client peer device
 *
 */
void handle_filter_request( struct irlan_cb *self, struct sk_buff *skb)
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	if (( self->t.server.filter_type == IR_DIRECTED) && 
	    ( self->t.server.filter_operation == DYNAMIC))
	{
		DEBUG( 0, "Giving peer a dynamic Ethernet address\n");

		self->t.server.mac_address[0] = 0x40;
		self->t.server.mac_address[1] = 0x00;
		self->t.server.mac_address[2] = 0x00;
		self->t.server.mac_address[3] = 0x00;
		self->t.server.mac_address[4] = 0x12;
		self->t.server.mac_address[5] = 0x34;

		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x03;
		insert_param( skb, "FILTER_MODE", 1, "NONE", 0, 0);
		insert_param( skb, "MAX_ENTRY", 3, NULL, 0, 0x0001);
		insert_array_param( skb, "FILTER_ENTRY", self->t.server.mac_address, 6);
		return;
	}
	
	if (( self->t.server.filter_type == IR_DIRECTED) && 
	    ( self->t.server.filter_mode == FILTER))
	{
		DEBUG( 0, "Directed filter on\n");
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x00;
		return;
	}
	if (( self->t.server.filter_type == IR_DIRECTED) && 
	    ( self->t.server.filter_mode == NONE))
	{
		DEBUG( 0, "Directed filter off\n");
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x00;
		return;
	}

	if (( self->t.server.filter_type == IR_BROADCAST) && 
	    ( self->t.server.filter_mode == FILTER))
	{
		DEBUG( 0, "Broadcast filter on\n");
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x00;
		return;
	}
	if (( self->t.server.filter_type == IR_BROADCAST) && 
	    ( self->t.server.filter_mode == NONE))
	{
		DEBUG( 0, "Broadcast filter off\n");
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x00;
		return;
	}
	if (( self->t.server.filter_type == IR_MULTICAST) && 
	    ( self->t.server.filter_mode == FILTER))
	{
		DEBUG( 0, "Multicast filter on\n");
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x00;
		return;
	}
	if (( self->t.server.filter_type == IR_MULTICAST) && 
	    ( self->t.server.filter_mode == NONE))
	{
		DEBUG( 0, "Multicast filter off\n");
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x00;
		return;
	}
	if (( self->t.server.filter_type == IR_MULTICAST) && 
	    ( self->t.server.filter_operation == GET))
	{
		DEBUG( 0, "Multicast filter get\n");
		skb->data[0] = 0x00; /* Success? */
		skb->data[1] = 0x02;
		insert_param( skb, "FILTER_MODE", 1, "NONE", 0, 0);
		insert_param( skb, "MAX_ENTRY", 3, NULL, 0, 16);
		return;
	}
	skb->data[0] = 0x00; /* Command not supported */
	skb->data[1] = 0x00;

	DEBUG( 0, "Not implemented!\n");
}

/*
 * Function check_request_param (self, param, value)
 *
 *    Check parameters in request from peer device
 *
 */
static void irlan_check_param( struct irlan_cb *self, char *param, char *value)
{
	__u8 *bytes;

	DEBUG( 4, __FUNCTION__ "()\n");

	bytes = value;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	DEBUG( 4, "%s, %s\n", param, value);

	/*
	 *  This is experimental!! DB.
	 */
	 if ( strcmp( param, "MODE") == 0) {
		DEBUG( 0, __FUNCTION__ "()\n");
		self->use_udata = TRUE;
		return;
	}

	/*
	 *  FILTER_TYPE
	 */
	if ( strcmp( param, "FILTER_TYPE") == 0) {
		if ( strcmp( value, "DIRECTED") == 0) {
			self->t.server.filter_type = IR_DIRECTED;
			return;
		}
		if ( strcmp( value, "MULTICAST") == 0) {
			self->t.server.filter_type = IR_MULTICAST;
			return;
		}
		if ( strcmp( value, "BROADCAST") == 0) {
			self->t.server.filter_type = IR_BROADCAST;
			return;
		}
	}
	/*
	 *  FILTER_MODE
	 */
	if ( strcmp( param, "FILTER_MODE") == 0) {
		if ( strcmp( value, "ALL") == 0) {
			self->t.server.filter_mode = ALL;
			return;
		}
		if ( strcmp( value, "FILTER") == 0) {
			self->t.server.filter_mode = FILTER;
			return;
		}
		if ( strcmp( value, "NONE") == 0) {
			self->t.server.filter_mode = FILTER;
			return;
		}
	}
	/*
	 *  FILTER_OPERATION
	 */
	if ( strcmp( param, "FILTER_OPERATION") == 0) {
		if ( strcmp( value, "DYNAMIC") == 0) {
			self->t.server.filter_operation = DYNAMIC;
			return;
		}
		if ( strcmp( value, "GET") == 0) {
			self->t.server.filter_operation = GET;
			return;
		}
	}
}

/*
 * Function irlan_server_send_reply (self, info)
 *
 *    Send reply to query to peer IrLAN layer
 *
 */
void irlan_server_send_reply( struct irlan_cb *self, int command, int ret_code)
{
	struct sk_buff *skb;

	DEBUG( 4, "irlan_server_send_reply()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	skb =  dev_alloc_skb( 128);
	if (skb == NULL) {
		DEBUG( 0,"irlan_server_send_reply: "
		       "Could not allocate an sk_buff of length %d\n", 128);
		return;
	}

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put( skb, 2);
       
	switch ( command) {
	case CMD_GET_PROVIDER_INFO:
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x02; /* 2 parameters */
		insert_param( skb, "MEDIA", 1, "802.3", 0, 0);
		insert_param( skb, "IRLAN_VER", 3, NULL, 0, 0x0101);
		break;
	case CMD_GET_MEDIA_CHAR:
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x05; /* 5 parameters */
		insert_param( skb, "FILTER_TYPE", 1, "DIRECTED", 0, 0);
		insert_param( skb, "FILTER_TYPE", 1, "BROADCAST", 0, 0);
		insert_param( skb, "FILTER_TYPE", 1, "MULTICAST", 0, 0);
		insert_param( skb, "ACCESS_TYPE", 1, "DIRECTED", 0, 0);
		insert_param( skb, "MAX_FRAME", 3, NULL, 0, 0x05ee);
		break;
	case CMD_OPEN_DATA_CHANNEL:
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x02; /* 2 parameters */
		insert_param( skb, "DATA_CHAN", 2, NULL, self->stsap_sel_data, 0);
		insert_param( skb, "RECONNECT_KEY", 1, "LINUX RULES!", 0, 0);
		break;
	case CMD_FILTER_OPERATION:
		handle_filter_request( self, skb);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown command!\n");
		break;
	}

	irttp_data_request( self->tsap_ctrl, skb);
}

/*
 * Function irlan_server_register(void)
 *
 *    Register server support so we can accept incomming connections. We
 *    must register both a TSAP for control and data
 * 
 */
void irlan_server_register(void)
{
	struct notify_t notify;
	struct irlan_cb *self;
	struct ias_object *obj;
	struct tsap_cb *tsap;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	/*
	 *  Open irlan_server instance
	 */
	self = irlan_server_open();
	if ( !self || self->magic != IRLAN_MAGIC) {
		DEBUG( 0, __FUNCTION__"(), Unable to open server!\n");
		return;
	}
	/*
	 *  First register well known control TSAP
	 */
	irda_notify_init( &notify);
	notify.data_indication       = irlan_server_control_data_indication;
	notify.connect_indication    = irlan_server_connect_indication;
	notify.disconnect_indication = irlan_server_disconnect_indication;
	notify.instance = self;
	strncpy( notify.name, "IrLAN srv. ctrl", 16);

	/* FIXME: should not use a static value here! */
	tsap = irttp_open_tsap( TSAP_IRLAN, 1, &notify);
	if ( tsap == NULL) {
		DEBUG( 0, __FUNCTION__ "(), Got no handle!!\n");
		return;
	}
	self->tsap_ctrl = tsap;

	/*
	 *  Now register data TSAP
	 */
	irda_notify_init( &notify);
	notify.data_indication       = irlan_eth_rx;
	notify.udata_indication      = irlan_eth_rx;
	notify.connect_indication    = irlan_server_connect_indication;
	notify.disconnect_indication = irlan_server_disconnect_indication;
	notify.instance = self;
	strncpy( notify.name, "IrLAN srv. data", 16);

	/*
	 *  Register well known address with IrTTP
	 */ 
	tsap = irttp_open_tsap( LSAP_ANY, DEFAULT_INITIAL_CREDIT, &notify);
	if ( tsap == NULL) {
		DEBUG( 0, __FUNCTION__ "(), Got no handle!\n");
		return;
	}
	self->tsap_data = tsap;

	/* 
	 *  This is the data TSAP selector which we will pass to the client
	 *  when the client ask for it.
	 */
	self->stsap_sel_data = tsap->stsap_sel;
	ASSERT( self->stsap_sel_data > 0, return;);

	DEBUG( 0, "irlan_server_register(), Using Source TSAP selector=%02x\n",
	       self->stsap_sel_data);
	
	/* 
	 *  Register with LM-IAS
	 */
	obj = irias_new_object( "IrLAN", IAS_IRLAN_ID);
	irias_add_integer_attrib( obj, "IrDA:TinyTP:LsapSel", TSAP_IRLAN);
	irias_insert_object( obj);

	obj = irias_new_object( "PnP", IAS_PNP_ID);
	irias_add_string_attrib( obj, "Name", "Linux");
	irias_add_string_attrib( obj, "DeviceID", "HWP19F0");
	irias_add_integer_attrib( obj, "CompCnt", 2);
	irias_add_string_attrib( obj, "Comp#01", "PNP8294");
	irias_add_string_attrib( obj, "Comp#01", "PNP8389");
	irias_add_string_attrib( obj, "Manufacturer", "Linux/IR Project");
	irias_insert_object( obj);
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("The Linux IrDA LAN Server protocol"); 

/*
 * Function init_module (void)
 *
 *    Initialize the IrLAN module, this function is called by the
 *    modprobe(1) program.
 */
int init_module(void) 
{
/* 	int result; */

	DEBUG( 4, "--> IrLAN irlan_server: init_module\n");

	irlan_server_init();

	DEBUG( 4, "IrLAN irlan_server: init_module -->\n");	

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
	DEBUG( 4, "--> irlan_server, cleanup_module\n");
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */

	/* Free some memory */
 	irlan_server_cleanup();

	DEBUG( 4, "irlan_server, cleanup_module -->\n");
}

#endif /* MODULE */
