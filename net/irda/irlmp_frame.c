/*********************************************************************
 *                
 * Filename:      irlmp_frame.c
 * Version:       0.8
 * Description:   IrLMP frame implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Aug 19 02:09:59 1997
 * Modified at:   Sat Jan 16 22:14:04 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>
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

#include <linux/config.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>

#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/irda/timer.h>
#include <net/irda/irlmp.h>
#include <net/irda/irlmp_frame.h>

static struct lsap_cb *irlmp_find_lsap( struct lap_cb *self, __u8 dlsap, 
					__u8 slsap, int status, hashbin_t *);

inline void irlmp_send_data_pdu( struct lap_cb *self, __u8 dlsap, __u8 slsap,
				 int expedited, struct sk_buff *skb)
{
	__u8 *frame;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	frame = skb->data;

	frame[0] = dlsap;
	frame[1] = slsap;

	if ( expedited) {
		DEBUG( 4, __FUNCTION__ "(), sending expedited data\n");
		irlap_data_request( self->irlap, skb, FALSE);
	} else {
		DEBUG( 4, __FUNCTION__ "(), sending reliable data\n");
		irlap_data_request( self->irlap, skb, TRUE);	
	}
}

/*
 * Function irlmp_send_lcf_pdu (dlsap, slsap, opcode,skb)
 *
 *    Send Link Control Frame to IrLAP
 */
void irlmp_send_lcf_pdu( struct lap_cb *self, __u8 dlsap, __u8 slsap,
			 __u8 opcode, struct sk_buff *skb) 
{
	__u8 *frame;
	
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	
	frame = skb->data;
	
	frame[0] = dlsap | CONTROL_BIT;
	frame[1] = slsap;

	frame[2] = opcode;

	if (opcode == DISCONNECT)
		frame[3] = 0x01; /* Service user request */
	else
		frame[3] = 0x00; /* rsvd */

	ASSERT( self->irlap != NULL, return;);
	irlap_data_request( self->irlap, skb, TRUE);
}

/*
 * Function irlmp_input (skb)
 *
 *    Used by IrLAP to pass received data frames to IrLMP layer
 *
 */
void irlmp_link_data_indication( struct lap_cb *self, int reliable, 
				 struct sk_buff *skb)
{
	__u8 *fp;
	__u8  slsap_sel;   /* Source (this) LSAP address */
	__u8  dlsap_sel;   /* Destination LSAP address */
	struct lsap_cb *lsap;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	ASSERT( skb->len > 2, return;);

	fp = skb->data;

	/*
	 *  The next statements may be confusing, but we do this so that 
	 *  destination LSAP of received frame is source LSAP in our view
	 */
	slsap_sel = fp[0] & LSAP_MASK; 
	dlsap_sel = fp[1];
	
	/*
	 *  Check if this is an incoming connection, since we must deal with
	 *  it in a different way than other established connections.
	 */
	if (( fp[0] & CONTROL_BIT) && ( fp[2] == CONNECT_CMD)) {
		DEBUG( 4,"Incoming connection, source LSAP=%d, dest LSAP=%d\n",
		       slsap_sel, dlsap_sel);
		
		/* Try to find LSAP among the unconnected LSAPs */
		lsap = irlmp_find_lsap( self, dlsap_sel, slsap_sel, 
					CONNECT_CMD, irlmp->unconnected_lsaps);
		
		/* Maybe LSAP was already connected, so try one more time */
		if ( !lsap)
		     lsap = irlmp_find_lsap( self, dlsap_sel, slsap_sel, 0,
					     self->lsaps);
	} else
		lsap = irlmp_find_lsap( self, dlsap_sel, slsap_sel, 0, 
					self->lsaps);
	
	if ( lsap == NULL) {
		DEBUG( 0, "IrLMP, Sorry, no LSAP for received frame!\n");
		DEBUG( 0, __FUNCTION__ 
		       "(), slsap_sel = %02x, dlsap_sel = %02x\n", slsap_sel, 
		       dlsap_sel);
		if ( fp[0] & CONTROL_BIT) {
			DEBUG( 0, __FUNCTION__ 
			       "(), received control frame %02x\n", fp[2]);
		} else {
			DEBUG( 0, __FUNCTION__ "(), received data frame\n");
		}
		dev_kfree_skb( skb);
		return;
	}

	/* 
	 *  Check if we received a control frame? 
	 */
	if ( fp[0] & CONTROL_BIT) {
		switch( fp[2]) {
		case CONNECT_CMD:
			lsap->lap = self;
			irlmp_do_lsap_event( lsap, LM_CONNECT_INDICATION, skb);
			break;
		case CONNECT_CNF:
			irlmp_do_lsap_event( lsap, LM_CONNECT_CONFIRM, skb);
			break;
		case DISCONNECT:
			DEBUG( 4, __FUNCTION__ "(), Disconnect indication!\n");
			irlmp_do_lsap_event( lsap, LM_DISCONNECT_INDICATION, 
					     skb);
			break;
		case ACCESSMODE_CMD:
			DEBUG( 0, "Access mode cmd not implemented!\n");
			break;
		case ACCESSMODE_CNF:
			DEBUG( 0, "Access mode cnf not implemented!\n");
			break;
		default:
			DEBUG( 0, __FUNCTION__ 
			       "(), Unknown control frame %02x\n", fp[2]);
			break;
		}
	} else if ( reliable == LAP_RELIABLE) {
		/* Must be pure data */
		irlmp_do_lsap_event( lsap, LM_DATA_INDICATION, skb);
	} else if ( reliable == LAP_UNRELIABLE) {
		irlmp_do_lsap_event( lsap, LM_UDATA_INDICATION, skb);
	}
}

/*
 * Function irlmp_link_disconnect_indication (reason, userdata)
 *
 *    IrLAP has disconnected 
 *
 */
void irlmp_link_disconnect_indication( struct lap_cb *lap, 
				       struct irlap_cb *irlap, 
				       LAP_REASON reason, 
				       struct sk_buff *userdata)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( lap != NULL, return;);
	ASSERT( lap->magic == LMP_LAP_MAGIC, return;);

	lap->reason = reason;

        /* FIXME: must do something with the userdata if any */

	/*
	 *  Inform station state machine
	 */
	irlmp_do_lap_event( lap, LM_LAP_DISCONNECT_INDICATION, NULL);
}

/*
 * Function irlmp_link_connect_indication (qos)
 *
 *    Incoming LAP connection!
 *
 */
void irlmp_link_connect_indication( struct lap_cb *self, struct qos_info *qos,
				    struct sk_buff *skb) 
{
	DEBUG( 4, __FUNCTION__ "()\n");

	/* Copy QoS settings for this session */
	self->qos = qos;

	irlmp_do_lap_event( self, LM_LAP_CONNECT_INDICATION, skb);
}

/*
 * Function irlmp_link_connect_confirm (qos)
 *
 *    LAP connection confirmed!
 *
 */
void irlmp_link_connect_confirm( struct lap_cb *self, struct qos_info *qos, 
				 struct sk_buff *userdata)
{
	DEBUG( 4, "irlmp_link_connect_confirm()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LAP_MAGIC, return;);
	ASSERT( qos != NULL, return;);

	/* Copy QoS settings for this session */
	self->qos = qos;

	irlmp_do_lap_event( self, LM_LAP_CONNECT_CONFIRM, NULL);
}

/*
 * Function irlmp_link_discovery_confirm (self, log)
 *
 *    Called by IrLAP with a list of discoveries after the discovery
 *    request has been carried out. A NULL log is received if IrLAP
 *    was unable to carry out the discovery request
 *
 */
void irlmp_link_discovery_confirm( struct lap_cb *self, hashbin_t *log)
{
/* 	DISCOVERY *discovery; */
	hashbin_t *old_log;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LAP_MAGIC, return;);

	ASSERT( self->cachelog != NULL, return;);

	/*
	 *  If log is missing this means that IrLAP was unable to perform the
	 *  discovery, so restart discovery again with just the half timeout
	 *  of the normal one.
	 */
	if ( !log) {
		irlmp_start_discovery_timer( irlmp, 150);
		return;
	}

#if 0
	discovery = hashbin_remove_first( log);
	while ( discovery) {
		DEBUG( 0, __FUNCTION__ "(), found %s\n", discovery->info);

		/* Remove any old discovery of this device */
		hashbin_remove( self->cachelog, discovery->daddr, NULL);

		/* Insert the new one */
		hashbin_insert( self->cachelog, (QUEUE *) discovery, 
				discovery->daddr, NULL);

		discovery = hashbin_remove_first( log);
	}
#endif
	old_log = self->cachelog;
	self->cachelog = log;
	hashbin_delete( old_log, (FREE_FUNC) kfree);
      
	irlmp_do_lap_event( self, LM_LAP_DISCOVERY_CONFIRM, NULL);

	DEBUG( 4, __FUNCTION__ "() -->\n");
}

#ifdef CONFIG_IRDA_CACHE_LAST_LSAP
__inline__ void irlmp_update_cache( struct lsap_cb *self)
{
	/* Update cache entry */
	irlmp->cache.dlsap_sel = self->dlsap_sel;
	irlmp->cache.slsap_sel = self->slsap_sel;
	irlmp->cache.lsap = self;
	irlmp->cache.valid = TRUE;
}
#endif

/*
 * Function irlmp_find_handle (self, dlsap_sel, slsap_sel, status, queue)
 *
 *    Find handle assosiated with destination and source LSAP
 *
 */
static struct lsap_cb *irlmp_find_lsap( struct lap_cb *self, __u8 dlsap_sel,
					__u8 slsap_sel, int status,
					hashbin_t *queue) 
{
	struct lsap_cb *lsap;
	
	ASSERT( self != NULL, return NULL;);
	ASSERT( self->magic == LMP_LAP_MAGIC, return NULL;);

	/* 
	 *  Optimize for the common case. We assume that the last frame
	 *  received is in the same connection as the last one, so check in
	 *  cache first to avoid the linear search
	 */
#ifdef CONFIG_IRDA_CACHE_LAST_LSAP
	ASSERT( irlmp != NULL, return NULL;);

	if (( irlmp->cache.valid) && 
	    ( irlmp->cache.slsap_sel == slsap_sel) && 
	    ( irlmp->cache.dlsap_sel == dlsap_sel)) 
	{
		DEBUG( 4, __FUNCTION__ "(), Using cached LSAP\n");
		return ( irlmp->cache.lsap);
	}	
#endif
	lsap = ( struct lsap_cb *) hashbin_get_first( queue);
	while ( lsap != NULL) {
		/* 
		 *  If this is an incomming connection, then the destination 
		 *  LSAP selector may have been specified as LM_ANY so that 
		 *  any client can connect. In that case we only need to check
		 *  if the source LSAP (in our view!) match!
		 */
		if (( status == CONNECT_CMD) && 
		    ( lsap->slsap_sel == slsap_sel) &&      
		    ( lsap->dlsap_sel == LSAP_ANY)) 
		{
			DEBUG( 4,"Incoming connection: Setting dlsap_sel=%d\n",
			       dlsap_sel);
			lsap->dlsap_sel = dlsap_sel;
			
#ifdef CONFIG_IRDA_CACHE_LAST_LSAP
			irlmp_update_cache( lsap);
#endif
			return lsap;
		}
		/*
		 *  Check if source LSAP and dest LSAP selectors match.
		 */
		if (( lsap->slsap_sel == slsap_sel) && 
		    ( lsap->dlsap_sel == dlsap_sel)) 
		{
#ifdef CONFIG_IRDA_CACHE_LAST_LSAP
			irlmp_update_cache( lsap);
#endif
			return lsap;
		}
		lsap = ( struct lsap_cb *) hashbin_get_next( queue);
	}

	/* Sorry not found! */
	return NULL;
}


