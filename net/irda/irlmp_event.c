/*********************************************************************
 *                
 * Filename:      irlmp_event.c
 * Version:       0.1
 * Description:   An IrDA LMP event driver for Linux
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Sat Jan 16 22:22:29 1999
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

#include <linux/kernel.h>

#include <net/irda/irda.h>
#include <net/irda/timer.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>
#include <net/irda/irlmp_frame.h>
#include <net/irda/irlmp_event.h>

char *irlmp_state[] = {
	"LAP_STANDBY",
	"LAP_U_CONNECT",
	"LAP_ACTIVE",
};

char *irlsap_state[] = {
	"LSAP_DISCONNECTED",
	"LSAP_CONNECT",
	"LSAP_CONNECT_PEND",
	"LSAP_DATA_TRANSFER_READY",
	"LSAP_SETUP",
	"LSAP_SETUP_PEND",
};

static char *irlmp_event[] = {
	"LM_CONNECT_REQUEST",
 	"LM_CONNECT_CONFIRM",
	"LM_CONNECT_RESPONSE",
 	"LM_CONNECT_INDICATION", 	
	
	"LM_DISCONNECT_INDICATION",
	"LM_DISCONNECT_REQUEST",

 	"LM_DATA_REQUEST",
	"LM_UDATA_REQUEST",
 	"LM_DATA_INDICATION",
	"LM_UDATA_INDICATION",

	"LM_WATCHDOG_TIMEOUT",

	/* IrLAP events */
	"LM_LAP_CONNECT_REQUEST",
 	"LM_LAP_CONNECT_INDICATION", 
 	"LM_LAP_CONNECT_CONFIRM",
 	"LM_LAP_DISCONNECT_INDICATION", 
	"LM_LAP_DISCONNECT_REQUEST",
	"LM_LAP_DISCOVERY_REQUEST",
 	"LM_LAP_DISCOVERY_CONFIRM",
};

/* LAP Connection control proto declarations */
static void irlmp_state_standby  ( struct lap_cb *, IRLMP_EVENT, 
				   struct sk_buff *);
static void irlmp_state_u_connect( struct lap_cb *, IRLMP_EVENT, 
				   struct sk_buff *);
static void irlmp_state_active   ( struct lap_cb *, IRLMP_EVENT, 
				   struct sk_buff *);

/* LSAP Connection control proto declarations */
static void irlmp_state_disconnected( struct lsap_cb *, IRLMP_EVENT, 
				      struct sk_buff *);
static void irlmp_state_connect     ( struct lsap_cb *, IRLMP_EVENT, 
				      struct sk_buff *);
static void irlmp_state_connect_pend( struct lsap_cb *, IRLMP_EVENT,
				      struct sk_buff *);
static void irlmp_state_dtr         ( struct lsap_cb *, IRLMP_EVENT, 
				      struct sk_buff *);
static void irlmp_state_setup       ( struct lsap_cb *, IRLMP_EVENT, 
				      struct sk_buff *);
static void irlmp_state_setup_pend  ( struct lsap_cb *, IRLMP_EVENT, 
				      struct sk_buff *);

static void (*lap_state[]) ( struct lap_cb *, IRLMP_EVENT, struct sk_buff *) =
{
	irlmp_state_standby,
	irlmp_state_u_connect,
	irlmp_state_active,
};

static void (*lsap_state[])( struct lsap_cb *, IRLMP_EVENT, struct sk_buff *) =
{
	irlmp_state_disconnected,
	irlmp_state_connect,
	irlmp_state_connect_pend,
	irlmp_state_dtr,
	irlmp_state_setup,
	irlmp_state_setup_pend
};

/* Do connection control events */
void irlmp_do_lsap_event( struct lsap_cb *self, IRLMP_EVENT event, 
			  struct sk_buff *skb)
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);

	DEBUG( 4, __FUNCTION__ "(), EVENT = %s, STATE = %s\n",
	       irlmp_event[ event], irlmp_state[ self->lsap_state]);

	(*lsap_state[ self->lsap_state]) ( self, event, skb);
}

/*
 * Function do_lap_event (event, skb, info)
 *
 *    Do IrLAP control events
 *
 */
void irlmp_do_lap_event( struct lap_cb *self, IRLMP_EVENT event, 
			 struct sk_buff *skb) 
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LAP_MAGIC, return;);
	
	DEBUG( 4, __FUNCTION__ "(), EVENT = %s, STATE = %s\n",
	       irlmp_event[event], 
	       irlmp_state[self->lap_state]);

	(*lap_state[ self->lap_state]) ( self, event, skb);
}

void irlmp_discovery_timer_expired( unsigned long data)
{
/* 	struct irlmp_cb *self = ( struct irlmp_cb *) data; */
	
	DEBUG( 4, "IrLMP, discovery timer expired!\n");
	
	irlmp_discovery_request( 8);

	/* Restart timer */
	irlmp_start_discovery_timer( irlmp, 300);
}

void irlmp_watchdog_timer_expired( unsigned long data)
{
	struct lsap_cb *self = ( struct lsap_cb *) data;
	
	DEBUG( 0, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);

	irlmp_do_lsap_event( self, LM_WATCHDOG_TIMEOUT, NULL);
}

/*********************************************************************
 *
 *    LAP connection control states
 *
 ********************************************************************/

/*
 * Function irlmp_state_standby (event, skb, info)
 *
 *    STANDBY, The IrLAP connection does not exist.
 *
 */
static void irlmp_state_standby( struct lap_cb *self, IRLMP_EVENT event, 
				 struct sk_buff *skb)
{	
	DEBUG( 4, __FUNCTION__ "()\n"); 
	ASSERT( self->irlap != NULL, return;);
	
	switch( event) {
	case LM_LAP_DISCOVERY_REQUEST:
		/* irlmp_next_station_state( LMP_DISCOVER); */
		
		irlap_discovery_request( self->irlap, &irlmp->discovery_cmd);
		break;
	case LM_LAP_DISCOVERY_CONFIRM:
 		/* irlmp_next_station_state( LMP_READY); */
		irlmp_discovery_confirm( self, self->cachelog);
 		break;
	case LM_LAP_CONNECT_INDICATION:
		/*  It's important to switch state first, to avoid IrLMP to 
		 *  think that the link is free since IrLMP may then start
		 *  discovery before the connection is properly set up. DB.
		 */
		irlmp_next_lap_state( self, LAP_ACTIVE);

		/* Just accept connection TODO, this should be fixed */
		irlap_connect_response( self->irlap, skb);
		break;
	case LM_LAP_CONNECT_REQUEST:
		DEBUG( 4, "irlmp_state_standby() LS_CONNECT_REQUEST\n");

		/* FIXME: need to set users requested QoS */
		irlap_connect_request( self->irlap, self->daddr, NULL, 0);

		irlmp_next_lap_state( self, LAP_U_CONNECT);
		break;
	case LM_LAP_DISCONNECT_INDICATION:
		DEBUG( 4, __FUNCTION__ 
		       "(), Error LM_LAP_DISCONNECT_INDICATION\n");
		
		irlmp_next_lap_state( self, LAP_STANDBY);
		break;
	default:
		DEBUG( 4, "irlmp_state_standby: Unknown event\n");
		break;
	}
}

/*
 * Function irlmp_state_u_connect (event, skb, info)
 *
 *    U_CONNECT, The layer above has tried to open an LSAP connection but
 *    since the IrLAP connection does not exist, we must first start an
 *    IrLAP connection. We are now waiting response from IrLAP.
 * */
static void irlmp_state_u_connect( struct lap_cb *self, IRLMP_EVENT event, 
				   struct sk_buff *skb)
{
	struct lsap_cb *lsap;
	struct lsap_cb *lsap_current;
	
	DEBUG( 4, __FUNCTION__ "()\n"); 

	switch( event) {
	case LM_LAP_CONNECT_CONFIRM:
		/* For all lsap_ce E Associated do LS_Connect_confirm */
		irlmp_next_lap_state( self, LAP_ACTIVE);

		lsap = ( struct lsap_cb *) hashbin_get_first( self->lsaps);
		while ( lsap != NULL) {
			irlmp_do_lsap_event(lsap, LM_LAP_CONNECT_CONFIRM, skb);
			lsap = (struct lsap_cb*) hashbin_get_next(self->lsaps);
		}		
		break;
	case LM_LAP_DISCONNECT_INDICATION:
		DEBUG( 4, __FUNCTION__ "(), IRLAP_DISCONNECT_INDICATION\n");
	
		irlmp_next_lap_state( self, LAP_STANDBY);

		/* Send disconnect event to all LSAPs using this link */
		
		lsap = ( struct lsap_cb *) hashbin_get_first( self->lsaps);
		while ( lsap != NULL ) {
			ASSERT( lsap->magic == LMP_LSAP_MAGIC, return;);
			
			lsap_current = lsap;

			/* Be sure to stay one item ahead */
			lsap = ( struct lsap_cb *) hashbin_get_next( self->lsaps);
			irlmp_do_lsap_event( lsap_current, 
					     LM_LAP_DISCONNECT_INDICATION,
					     NULL);
		}
		break;
	case LM_LAP_DISCONNECT_REQUEST:
		DEBUG( 4, __FUNCTION__ "(), LM_LAP_DISCONNECT_REQUEST\n");

		irlmp_next_lap_state( self, LAP_STANDBY);

		/* FIXME */
/* 		irlap_disconnect_request( self->irlap); */
		break;
	default:
		DEBUG( 4, __FUNCTION__ "(), Unknown event\n");
		break;
	}	
}

/*
 * Function irlmp_state_active (event, skb, info)
 *
 *    ACTIVE, IrLAP connection is active
 *
 */
static void irlmp_state_active( struct lap_cb *self, IRLMP_EVENT event, 
				struct sk_buff *skb)
{
	struct lsap_cb *lsap;
	struct lsap_cb *lsap_current;

	DEBUG( 4, __FUNCTION__ "()\n"); 

 	switch( event) {
	case LM_LAP_CONNECT_REQUEST:
		DEBUG( 4, __FUNCTION__ "(), LS_CONNECT_REQUEST\n");

		/*
		 *  LAP connection allready active, just bounce back! Since we 
		 *  don't know which LSAP that tried to do this, we have to 
		 *  notify all LSAPs using this LAP, but that should be safe to
		 *  do anyway.
		 */
		lsap = ( struct lsap_cb *) hashbin_get_first( self->lsaps);
		while ( lsap != NULL) {
			irlmp_do_lsap_event( lsap, LM_LAP_CONNECT_CONFIRM, 
					     skb); 
			lsap = (struct lsap_cb*) hashbin_get_next(self->lsaps);
		}
		
		/* Keep state */
		break;
	case LM_LAP_DISCONNECT_REQUEST:
		DEBUG( 4, __FUNCTION__ "(), LM_LAP_DISCONNECT_REQUEST\n");

		/*
		 *  Need to find out if we should close IrLAP or not
		 */
		if ( hashbin_get_size( self->lsaps) == 0) {
			DEBUG( 0, __FUNCTION__ 
			       "(), no more LSAPs so time to disconnect IrLAP\n");
			irlmp_next_lap_state( self, LAP_STANDBY);
		
			irlap_disconnect_request( self->irlap);
		}
		break;
	case LM_LAP_DISCONNECT_INDICATION:
		DEBUG( 4, __FUNCTION__ "(), IRLAP_DISCONNECT_INDICATION\n");
	
		irlmp_next_lap_state( self, LAP_STANDBY);		
		
		/* 
		 *  Inform all connected LSAP's using this link
		 */
		lsap = ( struct lsap_cb *) hashbin_get_first( self->lsaps);
		while ( lsap != NULL ) {
			ASSERT( lsap->magic == LMP_LSAP_MAGIC, return;);
			
			lsap_current = lsap;

			/* Be sure to stay one item ahead */
			lsap = ( struct lsap_cb *) hashbin_get_next( self->lsaps);
			irlmp_do_lsap_event( lsap_current, 
					     LM_LAP_DISCONNECT_INDICATION,
					     NULL);
		}
		break;
	default:
		DEBUG( 4, __FUNCTION__ "(), Unknown event %d\n", event);
		break;
	}	
}

/*********************************************************************
 *
 *    LSAP connection control states
 *
 ********************************************************************/

/*
 * Function irlmp_state_disconnected (event, skb, info)
 *
 *    DISCONNECTED
 *
 */
static void irlmp_state_disconnected( struct lsap_cb *self, IRLMP_EVENT event,
				      struct sk_buff *skb) 
{
	struct lsap_cb *lsap;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);

	switch( event) {
	case LM_CONNECT_REQUEST:
		DEBUG( 4, __FUNCTION__ "(), LM_CONNECT_REQUEST\n");
		irlmp_next_lsap_state( self, LSAP_SETUP_PEND);

		irlmp_do_lap_event( self->lap, LM_LAP_CONNECT_REQUEST, NULL);

		/* Start watchdog timer ( 5 secs for now) */
		irlmp_start_watchdog_timer( self, 500);
		break;
	case LM_CONNECT_INDICATION:
		irlmp_next_lsap_state( self, LSAP_CONNECT_PEND);


		/* 
		 *  Bind this LSAP to the IrLAP link where the connect was
		 *  received 
		 *  FIXME: this should be done in the LAP state machine
		 */
		lsap = hashbin_remove( irlmp->unconnected_lsaps, 
				       self->slsap_sel, NULL);

		ASSERT( lsap == self, return;);
		
		ASSERT( self->lap != NULL, return;);
		ASSERT( self->lap->lsaps != NULL, return;);
		
		hashbin_insert( self->lap->lsaps, (QUEUE *) self, 
				self->slsap_sel, NULL);

		irlmp_do_lap_event( self->lap, LM_LAP_CONNECT_REQUEST, skb);
		break;
	default:
		/* DEBUG( 4, "irlmp_state_disconnected: Unknown event %d\n",
		   event); */
		break;
	}
}

/*
 * Function irlmp_state_connect (self, event, skb)
 *
 *    CONNECT
 *
 */
static void irlmp_state_connect( struct lsap_cb *self, IRLMP_EVENT event, 
				 struct sk_buff *skb) 
{

	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);

	switch( event) {
	case LM_CONNECT_RESPONSE:
		ASSERT( skb != NULL, return;);

		irlmp_send_lcf_pdu( self->lap, self->dlsap_sel, 
				    self->slsap_sel, CONNECT_CNF, skb);

		del_timer( &self->watchdog_timer);

		irlmp_next_lsap_state( self, LSAP_DATA_TRANSFER_READY);
		break;
	default:
		DEBUG( 4, "irlmp_state_connect: Unknown event\n");
		break;
	}
}

/*
 * Function irlmp_state_connect_pend (event, skb, info)
 *
 *    CONNECT_PEND
 *
 */
static void irlmp_state_connect_pend( struct lsap_cb *self, IRLMP_EVENT event,
				      struct sk_buff *skb) 
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);

	switch( event) {
	case LM_CONNECT_REQUEST:
		/* Keep state */
		break;
	case LM_CONNECT_RESPONSE:
		printk( KERN_WARNING 
			"IrLMP CONNECT-PEND, No indication issued yet\n");
		/* Keep state */
		break;
	case LM_DISCONNECT_REQUEST:
		printk( KERN_WARNING
			"IrLMP CONNECT-PEND, "
			"Not yet bound to IrLAP connection\n");
		/* Keep state */
		break;
	case LM_LAP_CONNECT_CONFIRM:
		DEBUG( 4, "irlmp_state_connect_pend: LS_CONNECT_CONFIRM\n");
		irlmp_next_lsap_state( self, LSAP_CONNECT);
		irlmp_connect_indication( self, skb);
		break;
		
	default:
		DEBUG( 4, "irlmp_state_connect_pend: Unknown event %d\n", 
		       event);
		break;	
	}	
}

/*
 * Function irlmp_state_dtr (self, event, skb)
 *
 *    DATA_TRANSFER_READY
 *
 */
static void irlmp_state_dtr( struct lsap_cb *self, IRLMP_EVENT event, 
			     struct sk_buff *skb) 
{
	LM_REASON reason;

 	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
	ASSERT( self->lap != NULL, return;);

	switch( event) {
	case LM_CONNECT_REQUEST:
		printk( KERN_WARNING 
			"IrLMP DTR:  Error, LSAP allready connected\n");
		/* Keep state */
		break;
	case LM_CONNECT_RESPONSE:
		printk( KERN_WARNING 
			"IrLMP DTR:  Error, LSAP allready connected\n");
		/* Keep state */
		break;
	case LM_DISCONNECT_REQUEST:
		ASSERT( skb != NULL, return;);

		irlmp_send_lcf_pdu( self->lap, self->dlsap_sel, 
				    self->slsap_sel, DISCONNECT, skb);
		irlmp_next_lsap_state( self, LSAP_DISCONNECTED);
		
		/* Try to close the LAP connection if its still there */
		if ( self->lap) {
			DEBUG( 4, __FUNCTION__ "(), trying to close IrLAP\n");
			irlmp_do_lap_event( self->lap, 
					    LM_LAP_DISCONNECT_REQUEST, 
					    NULL);
		}

		break;
	case LM_DATA_REQUEST:
		ASSERT( skb != NULL, return;);
		irlmp_send_data_pdu( self->lap, self->dlsap_sel, 
				     self->slsap_sel, FALSE, skb);
		/* irlmp_next_lsap_state( DATA_TRANSFER_READY, info->handle);*/
		break;
	case LM_UDATA_REQUEST:
		ASSERT( skb != NULL, return;);
		irlmp_send_data_pdu( self->lap, self->dlsap_sel, 
				     self->slsap_sel, TRUE, skb);
		break;
	case LM_DATA_INDICATION:
		irlmp_data_indication( self, skb); 
		/* irlmp_next_lsap_state( DATA_TRANSFER_READY, info->handle);*/
		break;
	case LM_UDATA_INDICATION:
		irlmp_udata_indication( self, skb); 
		/* irlmp_next_lsap_state( DATA_TRANSFER_READY, info->handle);*/
		break;
	case LM_LAP_DISCONNECT_INDICATION:
		irlmp_next_lsap_state( self, LSAP_DISCONNECTED);

		reason = irlmp_convert_lap_reason( self->lap->reason);

		irlmp_disconnect_indication( self, reason, NULL);
		break;
	case LM_DISCONNECT_INDICATION:
		irlmp_next_lsap_state( self, LSAP_DISCONNECTED);
			
		ASSERT( self->lap != NULL, return;);
		ASSERT( self->lap->magic == LMP_LAP_MAGIC, return;);
	
		reason = irlmp_convert_lap_reason( self->lap->reason);

		 /* Try to close the LAP connection */
		DEBUG( 4, __FUNCTION__ "(), trying to close IrLAP\n");
		irlmp_do_lap_event( self->lap, LM_LAP_DISCONNECT_REQUEST, 
				    NULL);

		irlmp_disconnect_indication( self, reason, skb);

		break;
	default:
		DEBUG( 4, __FUNCTION__ "(), Unknown event %d\n", event);
		break;	
	}	
}

/*
 * Function irlmp_state_setup (event, skb, info)
 *
 *    SETUP, Station Control has set up the underlying IrLAP connection.
 *    An LSAP connection request has been transmitted to the peer
 *    LSAP-Connection Control FSM and we are awaiting reply.
 */
static void irlmp_state_setup( struct lsap_cb *self, IRLMP_EVENT event, 
			       struct sk_buff *skb) 
{
	LM_REASON reason;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);

	DEBUG( 4, "irlmp_state_setup()\n");

	switch( event) {
	case LM_CONNECT_CONFIRM:
		ASSERT( skb != NULL, return;);

		irlmp_next_lsap_state( self, LSAP_DATA_TRANSFER_READY);

		del_timer( &self->watchdog_timer);
		
		irlmp_connect_confirm( self, skb);
		break;
	case LM_DISCONNECT_INDICATION:
		irlmp_next_lsap_state( self, LSAP_DISCONNECTED);

		del_timer( &self->watchdog_timer);

		ASSERT( self->lap != NULL, return;);
		ASSERT( self->lap->magic == LMP_LAP_MAGIC, return;);
		
		reason = irlmp_convert_lap_reason( self->lap->reason);
		
		irlmp_disconnect_indication( self, reason, skb);
		break;
	default:
		DEBUG( 4, __FUNCTION__ "(), Unknown event %d\n", event);
		break;	
	}
}

/*
 * Function irlmp_state_setup_pend (event, skb, info)
 *
 *    SETUP_PEND, An LM_CONNECT_REQUEST has been received from the service
 *    user to set up an LSAP connection. A request has been sent to the
 *    LAP FSM to set up the underlying IrLAP connection, and we
 *    are awaiting confirm.
 */
static void irlmp_state_setup_pend( struct lsap_cb *self, IRLMP_EVENT event, 
				    struct sk_buff *skb) 
{

	DEBUG( 4, __FUNCTION__ "()\n"); 

	ASSERT( self != NULL, return;);
	ASSERT( irlmp != NULL, return;);

	switch( event) {
	case LM_LAP_CONNECT_CONFIRM:
		irlmp_send_lcf_pdu( self->lap, self->dlsap_sel, 
				    self->slsap_sel, CONNECT_CMD, 
				    self->tmp_skb);
		irlmp_next_lsap_state( self, LSAP_SETUP);
		break;
	case LM_DISCONNECT_INDICATION:
		del_timer( &self->watchdog_timer);

		irlmp_next_lsap_state( self, LSAP_DISCONNECTED);
		break;
	case LM_WATCHDOG_TIMEOUT:
		DEBUG( 0, __FUNCTION__ "() WATCHDOG_TIMEOUT!\n");

		/* FIXME: should we do a disconnect_indication? */
		ASSERT( self->lap != NULL, return;);
		irlmp_do_lap_event( self->lap, LM_LAP_DISCONNECT_REQUEST, NULL);
		irlmp_next_lsap_state( self, LSAP_DISCONNECTED);
		break;
	default:
		DEBUG( 4, __FUNCTION__ "(), Unknown event %d\n", event);
		break;	
	}
}

void irlmp_next_lap_state( struct lap_cb *self, IRLMP_STATE state) 
{
	DEBUG( 4, __FUNCTION__ "(), LMP LAP = %s\n", irlmp_state[state]);
	self->lap_state = state;
}

void irlmp_next_lsap_state( struct lsap_cb *self, LSAP_STATE state) 
{
	ASSERT( self != NULL, return;);

	DEBUG( 4, __FUNCTION__ "(), LMP LSAP = %s\n", irlsap_state[state]);
	self->lsap_state = state;
}
