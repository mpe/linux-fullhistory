/*********************************************************************
 *                
 * Filename:      irlan_cli_event.c
 * Version:       0.1
 * Description:   IrLAN Client FSM (Finite State Machine)
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Wed Dec  9 02:36:49 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
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

#include <linux/skbuff.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/iriap.h>
#include <net/irda/irlmp.h>
#include <net/irda/irttp.h>

#include <net/irda/irlan_common.h>
#include <net/irda/irlan_cli.h>
#include <net/irda/irlan_event.h>

static int irlan_client_state_idle ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);
static int irlan_client_state_query( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);
static int irlan_client_state_conn ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);
static int irlan_client_state_info ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);
static int irlan_client_state_media( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);
static int irlan_client_state_open ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);
static int irlan_client_state_wait ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);
static int irlan_client_state_arb  ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);
static int irlan_client_state_data ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);
static int irlan_client_state_close( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);
static int irlan_client_state_sync ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb);

static int (*state[])( struct irlan_cb *, IRLAN_EVENT event, 
		       struct sk_buff *) = 
{ 
	irlan_client_state_idle,
	irlan_client_state_query,
	irlan_client_state_conn,
	irlan_client_state_info,
	irlan_client_state_media,
	irlan_client_state_open,
	irlan_client_state_wait,
	irlan_client_state_arb,
	irlan_client_state_data,
	irlan_client_state_close,
	irlan_client_state_sync
};

void irlan_do_client_event( struct irlan_cb *self, 
			    IRLAN_EVENT event, 
			    struct sk_buff *skb) 
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	(*state[ self->state]) ( self, event, skb);
}

/*
 * Function irlan_client_state_idle (event, skb, info)
 *
 *    IDLE, We are waiting for an indication that there is a provider
 *    available.
 */
static int irlan_client_state_idle( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb) 
{
	DEBUG( 4, "irlan_client_state_idle()\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLAN_MAGIC, return -1;);
	
	switch( event) {
	case IRLAN_DISCOVERY_INDICATION:
		/* Get some values from peer IAS */
#if 0
		iriap_getvaluebyclass_request( self->daddr,
					       /*  "PnP", "DeviceID",  */
					       "Device", "DeviceName",
					       irlan_client_get_value_confirm,
					       self);
#endif
		iriap_getvaluebyclass_request( self->daddr,
					       "IrLAN", "IrDA:TinyTP:LsapSel",
					       irlan_client_get_value_confirm,
					       self);

		irlan_next_state( self, IRLAN_QUERY);
		break;
	default:
		DEBUG( 4, __FUNCTION__ "(), Unknown event %d\n", event);
		break;
	}
	if ( skb) {
		dev_kfree_skb( skb);
	}
	return 0;
}

/*
 * Function irlan_client_state_query (event, skb, info)
 *
 *    QUERY, We have queryed the remote IAS and is ready to connect
 *    to provider, just waiting for the confirm.
 *
 */
static int irlan_client_state_query( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb) 
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLAN_MAGIC, return -1;);
	
	switch( event) {
	case IRLAN_IAS_PROVIDER_AVAIL:
		ASSERT( self->dtsap_sel_ctrl != 0, return -1;);

		self->t.client.open_retries = 0;
		
		irttp_connect_request( self->tsap_ctrl, self->dtsap_sel_ctrl, 
				       self->daddr, NULL, IRLAN_MTU, NULL);
		irlan_next_state( self, IRLAN_CONN);
		break;
	case IRLAN_IAS_PROVIDER_NOT_AVAIL:
		DEBUG( 0, __FUNCTION__ "(), IAS_PROVIDER_NOT_AVAIL\n");
		irlan_next_state( self, IRLAN_IDLE);
		break;
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_state( self, IRLAN_IDLE);
		break;
	default:
		DEBUG( 0, __FUNCTION__"(), Unknown event %d\n", event);
		break;
	}
	if ( skb) {
		dev_kfree_skb( skb);
	}
	
	return 0;
}

/*
 * Function irlan_client_state_conn (event, skb, info)
 *
 *    CONN, We have connected to a provider but has not issued any
 *    commands yet.
 *
 */
static int irlan_client_state_conn( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb) 
{
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return -1;);
	
	switch( event) {
	case IRLAN_CONNECT_COMPLETE:
		/* Send getinfo cmd */
		irlan_get_provider_info( self);
		irlan_next_state( self, IRLAN_INFO);
		break;
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_state( self, IRLAN_IDLE);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %d\n", event);
		break;
	}
	if ( skb) {
		dev_kfree_skb( skb);
	}
	
	return 0;
}

/*
 * Function irlan_client_state_info (self, event, skb, info)
 *
 *    INFO, We have issued a GetInfo command and is awaiting a reply.
 */
static int irlan_client_state_info( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb) 
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return -1;);
	
	switch( event) {
	case IRLAN_DATA_INDICATION:
		ASSERT( skb != NULL, return -1;);
	
		irlan_client_extract_params( self, skb);
		
		irlan_next_state( self, IRLAN_MEDIA);
		
		irlan_get_media_char( self);
		break;
		
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_state( self, IRLAN_IDLE);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %d\n", event);
		break;
	}
	if ( skb) {
		dev_kfree_skb( skb);
	}
	
	return 0;
}

/*
 * Function irlan_client_state_media (self, event, skb, info)
 *
 *    MEDIA, The irlan_client has issued a GetMedia command and is awaiting a
 *    reply.
 *
 */
static int irlan_client_state_media( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb) 
{
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return -1;);

	switch( event) {
	case IRLAN_DATA_INDICATION:
		irlan_client_extract_params( self, skb);
		irlan_open_data_channel( self);
		irlan_next_state( self, IRLAN_OPEN);
		break;
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_state( self, IRLAN_IDLE);
		break;
	default:
		DEBUG( 0, "irlan_client_state_media, Unknown event %d\n", 
		       event);
		break;
	}
	if ( skb) {
		dev_kfree_skb( skb);
	}
	
	return 0;
}

/*
 * Function irlan_client_state_open (self, event, skb, info)
 *
 *    OPEN, The irlan_client has issued a OpenData command and is awaiting a
 *    reply
 *
 */
static int irlan_client_state_open( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb) 
{
	struct qos_info qos;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return -1;);

	switch( event) {
	case IRLAN_DATA_INDICATION:
		irlan_client_extract_params( self, skb);
		
		/*
		 *  Check if we have got the remote TSAP for data 
		 *  communications
		 */
	  	ASSERT( self->dtsap_sel_data != 0, return -1;);

		qos.link_disc_time.bits = 0x01; /* 3 secs */

		irttp_connect_request( self->tsap_data, 
				       self->dtsap_sel_data, self->daddr,
				       NULL, IRLAN_MTU, NULL);
		
  		irlan_next_state( self, IRLAN_DATA);
		break;

	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_state( self, IRLAN_IDLE);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %d\n", 
		       event);
		break;
	}
	
	if ( skb) {
		dev_kfree_skb( skb);
	}

	return 0;
}

/*
 * Function irlan_client_state_wait (self, event, skb, info)
 *
 *    WAIT, The irlan_client is waiting for the local provider to enter the
 *    provider OPEN state.
 *
 */
static int irlan_client_state_wait( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb) 
{
	DEBUG( 4, "irlan_client_state_wait()\n");
	
	ASSERT( self != NULL, return -1;);
	
	switch( event) {
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_state( self, IRLAN_IDLE);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %d\n", 
		       event);
		break;
	}
	if ( skb) {
		dev_kfree_skb( skb);
	}
	
	return 0;
}

static int irlan_client_state_arb( struct irlan_cb *self, 
				   IRLAN_EVENT event, 
				   struct sk_buff *skb) 
{
	DEBUG( 0, __FUNCTION__ "(), not implemented!\n");
	
	if ( skb) {
		dev_kfree_skb( skb);
	}
	return 0;
}

/*
 * Function irlan_client_state_data (self, event, skb, info)
 *
 *    DATA, The data channel is connected, allowing data transfers between
 *    the local and remote machines.
 *
 */
static int irlan_client_state_data( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb) 
{
	struct irmanager_event mgr_event;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLAN_MAGIC, return -1;);

	switch( event) {
	case IRLAN_CONNECT_COMPLETE:
		irlan_get_unicast_addr( self);
		irlan_open_unicast_addr( self);
		/* irlan_set_broadcast_filter( self, TRUE);  */

		DEBUG( 4, "IrLAN, We are now connected!\n");

		/* irlan_next_state( LAN_DATA); */
		break;
	case IRLAN_DATA_INDICATION:
		irlan_client_extract_params( self, skb);

		/* irlan_client_flow_indication( self->data_tsap, FLOW_START,  */
/* 					self); */

		/* Make sure the code below only runs once */
		if ( !self->connected) {
			mgr_event.event = EVENT_IRLAN_START;
			sprintf( mgr_event.devname, "%s", self->ifname);
			irmanager_notify( &mgr_event);

			self->connected = TRUE;
		}
		break;
		
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		mgr_event.event = EVENT_IRLAN_STOP;
		sprintf( mgr_event.devname, "%s", self->ifname);
		irmanager_notify( &mgr_event);

		irlan_next_state( self, IRLAN_IDLE);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %d\n", event);
		break;
	}
	if ( skb) {
		dev_kfree_skb( skb);
	}
	
	return 0;
}

/*
 * Function irlan_client_state_close (self, event, skb, info)
 *
 *    
 *
 */
static int irlan_client_state_close( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb) 
{
	DEBUG( 0, __FUNCTION__ "()\n");

	if ( skb) {
		dev_kfree_skb( skb);
	}

	return 0;
}

/*
 * Function irlan_client_state_sync (self, event, skb, info)
 *
 *    
 *
 */
static int irlan_client_state_sync( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb) 
{
	DEBUG( 0, __FUNCTION__ "()\n");
	
	if ( skb) {
		dev_kfree_skb( skb);
	}
	
	return 0;
}

