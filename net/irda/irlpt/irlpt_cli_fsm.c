/*********************************************************************
 *                
 * Filename:      irlpt_cli_fsm.c
 * Version:       0.1
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Jan 12 11:06:00 1999
 * Modified at:   Tue Jan 12 11:14:22 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>
 *     Copyright (c) 1998, Dag Brattli, <dagb@cs.uit.no>
 *     All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     I, Thomas Davis, provide no warranty for any of this software. This 
 *     material is provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <net/irda/iriap.h>
#include <net/irda/irlmp.h>
#include <net/irda/irttp.h>
#include <net/irda/irlpt_common.h>
#include <net/irda/irlpt_cli.h>
#include <net/irda/irlpt_cli_fsm.h>
#include <net/irda/irda.h>

#if 0
static char *rcsid = "$Id: irlpt_client_fsm.c,v 1.3 1998/10/05 05:46:44 ratbert Exp $";
#endif

static int irlpt_client_state_idle  ( struct irlpt_cb *self, IRLPT_EVENT event,
				      struct sk_buff *skb, 
				      struct irlpt_info *info);
static int irlpt_client_state_query ( struct irlpt_cb *self, IRLPT_EVENT event,
				      struct sk_buff *skb, 
				      struct irlpt_info *info);
static int irlpt_client_state_ready  ( struct irlpt_cb *self, 
				       IRLPT_EVENT event, 
				       struct sk_buff *skb, 
				       struct irlpt_info *info);
static int irlpt_client_state_waiti ( struct irlpt_cb *self, IRLPT_EVENT event,
				      struct sk_buff *skb, 
				      struct irlpt_info *info);
static int irlpt_client_state_waitr ( struct irlpt_cb *self, IRLPT_EVENT event,
				      struct sk_buff *skb, 
				      struct irlpt_info *info);
static int irlpt_client_state_conn  ( struct irlpt_cb *self, IRLPT_EVENT event,
				      struct sk_buff *skb, 
				      struct irlpt_info *info);

int irlpt_client_fsm_debug = 3;

int (*irlpt_client_state[])( struct irlpt_cb *self, IRLPT_EVENT event, 
				    struct sk_buff *skb, 
				    struct irlpt_info *info) = 
{ 
	irlpt_client_state_idle,
	irlpt_client_state_query,
	irlpt_client_state_ready,
	irlpt_client_state_waiti,
	irlpt_client_state_waitr,
	irlpt_client_state_conn,
};

void irlpt_client_do_event( struct irlpt_cb *self, IRLPT_EVENT event, 
				   struct sk_buff *skb, 
				   struct irlpt_info *info) 
{
	DEBUG( irlpt_client_fsm_debug,"--> "  __FUNCTION__ "\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ ": STATE = %s, EVENT = %s\n", 
	       irlpt_server_fsm_state[self->state], irlpt_fsm_event[event]);

	(*irlpt_client_state[ self->state]) ( self, event, skb, info);

	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ " -->\n");
}

void irlpt_client_next_state( struct irlpt_cb *self, IRLPT_CLIENT_STATE state) 
{
	DEBUG( irlpt_client_fsm_debug,"--> "  __FUNCTION__ ":\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ ": NEXT STATE = %s\n", 
	       irlpt_client_fsm_state[state]);

	self->state = state;

	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ " -->\n");
}

/*
 * Function client_state_idle (event, skb, info)
 *
 *    IDLE, We are waiting for an indication that there is a provider
 *    available.
 */
static int irlpt_client_state_idle( struct irlpt_cb *self, IRLPT_EVENT event, 
				    struct sk_buff *skb, 
				    struct irlpt_info *info) 
{
	DEBUG( irlpt_client_fsm_debug,"--> "  __FUNCTION__ ":\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

	switch( event) {
	case IRLPT_DISCOVERY_INDICATION:
		/* Get some values from peer IAS */
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
		       ": IRLPT_DISCOVERY_INDICATION, sending getvaluebyclass command..\n");
		iriap_getvaluebyclass_request( info->daddr, 
					"IrLPT", "IrDA:IrLMP:LsapSel",
					irlpt_client_get_value_confirm,
					(void *) self);
		irlpt_client_next_state( self, IRLPT_CLIENT_QUERY);
		break;

	default:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
		       ": Unknown event %d (%s)\n",
		       event, irlpt_fsm_event[event]);
		break;
	}

	if (skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ " -->\n");

	return 0;
}

/*
 * Function client_state_query
 *
 *    QUERY, We have queryed the remote IAS and is ready to connect
 *    to provider, just waiting for the confirm.
 *
 */
static int irlpt_client_state_query( struct irlpt_cb *self, IRLPT_EVENT event, 
				     struct sk_buff *skb, 
				     struct irlpt_info *info) 
{
	DEBUG( irlpt_client_fsm_debug,"--> "  __FUNCTION__ ":\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

	switch( event) {
	case IAS_PROVIDER_AVAIL:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ ": IAS_PROVIDER_AVAIL\n");
		self->open_retries = 0;
		irlpt_client_next_state( self, IRLPT_CLIENT_READY);
		irlpt_client_do_event( self, IRLPT_CONNECT_REQUEST, NULL, NULL);
		break;

	case IAS_PROVIDER_NOT_AVAIL:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ ": IAS_PROVIDER_NOT_AVAIL\n");
		irlpt_client_next_state( self, IRLPT_CLIENT_IDLE);
		break;

	default:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ ": Unknown event %d (%s)\n",
		       event, irlpt_fsm_event[event]);
		break;
	}

	if (skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ " -->\n");

	return 0;
}

/*
 * Function client_state_info 
 *
 *    INFO, We have issued a GetInfo command and is awaiting a reply.
 */
static int irlpt_client_state_ready( struct irlpt_cb *self, IRLPT_EVENT event,
				     struct sk_buff *skb, 
				     struct irlpt_info *info) 
{
	DEBUG( irlpt_client_fsm_debug,"--> "  __FUNCTION__ ":\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

	switch( event) {
	case IRLPT_CONNECT_REQUEST:
	        DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
		       ": IRLPT_CONNECT_REQUEST\n");
		irlpt_client_connect_request(self);
		irlpt_client_next_state( self, IRLPT_CLIENT_WAITI);
		break;
	case LMP_DISCONNECT:
	case LAP_DISCONNECT:
	        DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
		       ": LMP_DISCONNECT or LAP_DISCONNECT\n");
		irlpt_client_next_state( self, IRLPT_CLIENT_IDLE);
		break;
	default:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
		       ": Unknown event %d (%s)\n", 
		       event, irlpt_fsm_event[event]);
		break;
	}

	if ( skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ " -->\n");

	return 0;
}


/*
 * Function client_state_waiti 
 *
 *
 */
static int irlpt_client_state_waiti( struct irlpt_cb *self, IRLPT_EVENT event, 
				     struct sk_buff *skb, 
				     struct irlpt_info *info) 
{
	DEBUG( irlpt_client_fsm_debug,"--> "  __FUNCTION__ ":\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

	switch( event) {
	case LMP_CONNECT:
	        DEBUG( irlpt_client_fsm_debug, __FUNCTION__ ": LMP_CONNECT\n");
	        irlpt_client_next_state(self, IRLPT_CLIENT_CONN);
	        break;
	case LAP_DISCONNECT:
	case LMP_DISCONNECT:
	        DEBUG( irlpt_client_fsm_debug, __FUNCTION__ ": LMP_DISCONNECT\n");
	        irlpt_client_next_state( self, IRLPT_CLIENT_IDLE);
	        break;

	default:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
		       ": Unknown event %d (%s)\n", 
		       event, irlpt_fsm_event[event]);
		break;
	}

	if ( skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ " -->\n");

	return 0;
}

/*
 * Function client_state_waitr 
 *
 *
 */
static int irlpt_client_state_waitr( struct irlpt_cb *self, IRLPT_EVENT event,
				     struct sk_buff *skb, 
				     struct irlpt_info *info) 
{
	DEBUG( irlpt_client_fsm_debug,"--> "  __FUNCTION__ ":\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

	switch( event) {
	case LMP_CONNECT:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ ": LMP_CONNECT\n");
		irlpt_client_next_state(self, IRLPT_CLIENT_CONN);
		break;

	case LMP_DISCONNECT:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ ": LMP_DISCONNECT\n");
		irlpt_client_next_state( self, IRLPT_CLIENT_IDLE);
		break;

	case LAP_DISCONNECT:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ ": LAP_DISCONNECT\n");
		irlpt_client_next_state( self, IRLPT_CLIENT_IDLE);
		break;

	default:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
		       ": Unknown event %d, (%s)\n",
		       event, irlpt_fsm_event[event]);
		break;
	}

	if ( skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ " -->\n");

	return 0;
}

/*
 * Function client_state_conn (event, skb, info)
 *
 *    CONN, We have connected to a provider but has not issued any
 *    commands yet.
 *
 */
static int irlpt_client_state_conn( struct irlpt_cb *self, IRLPT_EVENT event, 
				    struct sk_buff *skb, 
				    struct irlpt_info *info) 
{
	DEBUG( irlpt_client_fsm_debug,"--> "  __FUNCTION__ ":\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

	switch( event) {
	case CLIENT_DATA_INDICATION:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
		       ": CLIENT_DATA_INDICATION\n");
		irlpt_client_next_state( self, IRLPT_CLIENT_CONN);
		break;

	case LMP_DISCONNECT:
	case LAP_DISCONNECT:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
		       ": LMP_DISCONNECT/LAP_DISCONNECT\n");
		irlpt_client_next_state( self, IRLPT_CLIENT_IDLE);
		break;

	default:
		DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
		       ": Unknown event %d (%s)\n",
		       event, irlpt_fsm_event[event]);
		break;
	}

	if ( skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ " -->\n");

	return 0;
}

void irlpt_client_print_event( IRLPT_EVENT event) 
{
	DEBUG( irlpt_client_fsm_debug, __FUNCTION__ 
	       ": IRLPT_EVENT = %s\n", irlpt_fsm_event[event]);
}


