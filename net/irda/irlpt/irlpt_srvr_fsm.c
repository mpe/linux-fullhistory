/*********************************************************************
 *                
 * Filename:      irlpt_srvr_fsm.c
 * Version:       0.1
 * Sources:       irlan_event.c
 * 
 *     Copyright (c) 1997, Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>, All Rights Reserved.
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
#include <net/irda/irlpt_server.h>
#include <net/irda/irlpt_server_fsm.h>
#include <net/irda/irda.h>

static int irlpt_server_state_idle  ( struct irlpt_cb *self, 
				      IRLPT_EVENT event,
				      struct sk_buff *skb, 
				      struct irlpt_info *info);
static int irlpt_server_state_conn  ( struct irlpt_cb *self, 
				      IRLPT_EVENT event, 
				      struct sk_buff *skb, 
				      struct irlpt_info *info);

#if 0
static char *rcsid = "$Id: irlpt_server_fsm.c,v 1.4 1998/10/05 05:46:45 ratbert Exp $";
#endif

int irlpt_server_fsm_debug = 3;

static int (*irlpt_server_state[])( struct irlpt_cb *self, IRLPT_EVENT event, 
				    struct sk_buff *skb,
				    struct irlpt_info *info) = 
{ 
	irlpt_server_state_idle,
	irlpt_server_state_conn,
};

void irlpt_server_do_event( struct irlpt_cb *self, IRLPT_EVENT event, 
			    struct sk_buff *skb, struct irlpt_info *info) 
{
	DEBUG( irlpt_server_fsm_debug, __FUNCTION__ ": STATE = %s, EVENT = %s\n", 
	       irlpt_server_fsm_state[self->state], irlpt_fsm_event[event]);

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	(*irlpt_server_state[ self->state]) ( self, event, skb, info);

	DEBUG( irlpt_server_fsm_debug, __FUNCTION__ " -->\n");
}

void irlpt_server_next_state( struct irlpt_cb *self, IRLPT_CLIENT_STATE state) 
{

	DEBUG( irlpt_server_fsm_debug, __FUNCTION__ ": NEXT STATE = %s\n", 
	       irlpt_server_fsm_state[state]);

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	self->state = state;

	DEBUG( irlpt_server_fsm_debug, __FUNCTION__ " -->\n");
}

/*
 * Function server_state_idle (event, skb, info)
 *
 *    IDLE, We are waiting for an indication that there is a provider
 *    available.
 */
static int irlpt_server_state_idle( struct irlpt_cb *self, IRLPT_EVENT event, 
				    struct sk_buff *skb, 
				    struct irlpt_info *info) 
{
	struct sk_buff *r_skb;

	DEBUG( irlpt_server_fsm_debug, "--> " __FUNCTION__ ":\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

	switch( event) {
	case LMP_CONNECT:
		DEBUG( irlpt_server_fsm_debug, __FUNCTION__ 
		       ": LM_CONNECT, remote lsap=%d\n", 
		       info->dlsap_sel);

		self->dlsap_sel = info->dlsap_sel;

		r_skb = dev_alloc_skb(64);
		if (r_skb == NULL) { 
			printk( KERN_INFO __FUNCTION__ 
				": can't allocate sk_buff of length 64\n");
			return 0;
		}
		ALLOC_SKB_MAGIC(r_skb);
		skb_reserve( r_skb, LMP_MAX_HEADER);
		skb->len = 0;
		irlmp_connect_response( self->lsap, r_skb);
		irlpt_server_next_state( self, IRLPT_SERVER_CONN);

		break;

	default:
		DEBUG( irlpt_server_fsm_debug, __FUNCTION__ 
		       ": Unknown event %d (%s)\n", 
		       event, irlpt_fsm_event[event]);
		break;
	}

	if (skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_server_fsm_debug, __FUNCTION__ " -->\n");

	return 0;
}

/*
 * Function server_state_conn (event, skb, info)
 *
 *    CONN, We have connected to a provider but has not issued any
 *    commands yet.
 *
 */
static int irlpt_server_state_conn( struct irlpt_cb *self, 
				    IRLPT_EVENT event, 
				    struct sk_buff *skb, 
				    struct irlpt_info *info) 
{
	DEBUG( irlpt_server_fsm_debug, "--> " __FUNCTION__ ":\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

	switch( event) {
	case LMP_DISCONNECT:
	case LAP_DISCONNECT:
		DEBUG( irlpt_server_fsm_debug, __FUNCTION__ 
		       ": LMP_DISCONNECT/LAP_DISCONNECT\n");
		irlpt_server_next_state( self, IRLPT_SERVER_IDLE);
		break;

	default:
		DEBUG( irlpt_server_fsm_debug, __FUNCTION__ 
		       ": Unknown event %d (%s)\n", 
		       event, irlpt_fsm_event[event]);
		break;
	}

	if ( skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_server_fsm_debug, __FUNCTION__ " -->\n");

	return 0;
}

#if 0
static void irlpt_server_print_event( IRLPT_EVENT event) 
{
	DEBUG( 0, "IRLPT_EVENT = %s\n", irlpt_fsm_event[event]);
}
#endif
