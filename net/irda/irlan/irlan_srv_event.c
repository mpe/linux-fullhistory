/*********************************************************************
 *                
 * Filename:      irlan_srv_event.c
 * Version:       0.1
 * Description:   IrLAN Server FSM (Finite State Machine)
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Wed Dec  9 02:39:05 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
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

#include <net/irda/irda.h>
#include <net/irda/iriap.h>
#include <net/irda/irlmp.h>
#include <net/irda/irttp.h>

#include <net/irda/irlan_srv.h>
#include <net/irda/irlan_event.h>

static int irlan_server_state_idle ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb, 
				     struct irlan_info *info);
static int irlan_server_state_info ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb, 
				     struct irlan_info *info);
static int irlan_server_state_open ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb, 
				     struct irlan_info *info);
static int irlan_server_state_data ( struct irlan_cb *self, 
				     IRLAN_EVENT event, 
				     struct sk_buff *skb, 
				     struct irlan_info *info);

static int (*state[])( struct irlan_cb *self, IRLAN_EVENT event, 
		       struct sk_buff *skb, struct irlan_info *info) = 
{ 
	irlan_server_state_idle,
	NULL, /* Query */
	NULL, /* Info */
	irlan_server_state_info,
	NULL, /* Media */
	irlan_server_state_open,
	NULL, /* Wait */
	NULL, /* Arb */
	irlan_server_state_data,
	NULL, /* Close */
	NULL, /* Sync */
};

void irlan_do_server_event( struct irlan_cb *self, 
			    IRLAN_EVENT event, 
			    struct sk_buff *skb, 
			    struct irlan_info *info) 
{
	(*state[ self->state]) ( self, event, skb, info);
}

/*
 * Function irlan_server_state_idle (event, skb, info)
 *
 *    IDLE, We are waiting for an indication that there is a provider
 *    available.
 */
static int irlan_server_state_idle( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb, 
				    struct irlan_info *info) 
{
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return -1;);
	
	switch( event) {
	case IRLAN_CONNECT_INDICATION:
	     ASSERT( info != NULL, return 0;);
	     irlan_server_connect_response( self, info->tsap);
	     irlan_next_state( self, IRLAN_INFO);
	     break;
	default:
		DEBUG( 4, __FUNCTION__ "(), Unknown event %d\n", 
		       event);
		break;
	}
	if ( skb) {
		dev_kfree_skb( skb);
	}
	return 0;
}

/*
 * Function irlan_server_state_info (self, event, skb, info)
 *
 *    INFO, We have issued a GetInfo command and is awaiting a reply.
 */
static int irlan_server_state_info( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb, 
				    struct irlan_info *info) 
{
	int ret;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return -1;);

	switch( event) {
	case IRLAN_GET_INFO_CMD: 
		irlan_server_send_reply( self, CMD_GET_PROVIDER_INFO, 
					 RSP_SUCCESS);
		/* Keep state */
		break;

	case IRLAN_GET_MEDIA_CMD: 
		irlan_server_send_reply( self, CMD_GET_MEDIA_CHAR, 
					 RSP_SUCCESS);
		/* Keep state */
		break;
		
	case IRLAN_OPEN_DATA_CMD:
		ret = irlan_parse_open_data_cmd( self, skb);
		irlan_server_send_reply( self, CMD_OPEN_DATA_CHANNEL, ret);

		if ( ret == RSP_SUCCESS)
			irlan_next_state( self, IRLAN_OPEN);
		break;
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_state( self, IRLAN_IDLE);
		break;
	default:
		DEBUG( 0, "irlan_server_state_info, Unknown event %d\n", 
		       event);
		break;
	}
	if ( skb) {
		dev_kfree_skb( skb);
	}
	
	return 0;
}

/*
 * Function irlan_server_state_open (self, event, skb, info)
 *
 *    OPEN, The client has issued a OpenData command and is awaiting a
 *    reply
 *
 */
static int irlan_server_state_open( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb, 
				    struct irlan_info *info) 
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return -1;);

	switch( event) {
	case IRLAN_FILTER_CONFIG_CMD:
		irlan_server_extract_params( self, CMD_FILTER_OPERATION, skb);
		irlan_server_send_reply( self, CMD_FILTER_OPERATION, 
					 RSP_SUCCESS);
		/* Keep state */
		break;
		
	case IRLAN_DATA_CONNECT_INDICATION: 
		DEBUG( 4, "DATA_CONNECT_INDICATION\n");
		irlan_next_state( self, IRLAN_DATA);
		irlan_server_connect_response( self, info->tsap);
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
 * Function irlan_server_state_data (self, event, skb, info)
 *
 *    DATA, The data channel is connected, allowing data transfers between
 *    the local and remote machines.
 *
 */
static int irlan_server_state_data( struct irlan_cb *self, 
				    IRLAN_EVENT event, 
				    struct sk_buff *skb, 
				    struct irlan_info *info) 
{
	struct irmanager_event mgr_event;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLAN_MAGIC, return -1;);

	switch( event) {
	case IRLAN_FILTER_CONFIG_CMD:
		irlan_server_extract_params( self, CMD_FILTER_OPERATION, skb);
		irlan_server_send_reply( self, CMD_FILTER_OPERATION, 
					 RSP_SUCCESS);

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
		DEBUG( 0, "irlan_server_state_data, Unknown event %d\n", 
		       event);
		break;
	}
	if ( skb) {
		dev_kfree_skb( skb);
	}
	
	return 0;
}










