/*********************************************************************
 *                
 * Filename:      irlap_event.c
 * Version:       0.1
 * Description:   IrLAP state machine implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Aug 16 00:59:29 1997
 * Modified at:   Tue Jan 19 22:58:45 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>,
 *                        Thomas Davis <ratbert@radiks.net>
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
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/skbuff.h>

#include <net/irda/irda.h>
#include <net/irda/irlap_event.h>

#include <net/irda/timer.h>
#include <net/irda/irlap.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/qos.h>

#include <net/irda/irda_device.h>

static int irlap_state_ndm    ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_query  ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_reply  ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_conn   ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_setup  ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_offline( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_xmit_p ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_pclose ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_nrm_p  ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_reset_wait(struct irlap_cb *self, IRLAP_EVENT event, 
				  struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_reset  ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_nrm_s  ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_xmit_s ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_sclose ( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_reset_check( struct irlap_cb *, IRLAP_EVENT event, 
				    struct sk_buff *, struct irlap_info *);

static char *irlap_event[] = {
	"DISCOVERY_REQUEST",
	"CONNECT_REQUEST",
	"CONNECT_RESPONSE",
	"DISCONNECT_REQUEST",
	"DATA_REQUEST",
	"RESET_REQUEST",
	"RESET_RESPONSE",
	"SEND_I_CMD",
	"RECV_DISCOVERY_XID_CMD",
	"RECV_DISCOVERY_XID_RSP",
	"RECV_SNRM_CMD",
	"RECV_TEST_CMD",
	"RECV_UA_RSP",
	"RECV_DM_RSP",
	"RECV_I_CMD",
	"RECV_I_RSP",
	"RECV_UI_FRAME",
	"RECV_FRMR_RSP",
	"RECV_RR_CMD",
	"RECV_RR_RSP",
	"RECV_RNR_FRAME",
	"RECV_DISC_FRAME",
	"SLOT_TIMER_EXPIRED",
	"QUERY_TIMER_EXPIRED",
	"FINAL_TIMER_EXPIRED",
	"POLL_TIMER_EXPIRED",
	"DISCOVERY_TIMER_EXPIRED",
	"WD_TIMER_EXPIRED",
	"BACKOFF_TIMER_EXPIRED",
};

char *irlap_state[] = {
	"LAP_NDM",
	"LAP_QUERY",
	"LAP_REPLY",
	"LAP_CONN",
	"LAP_SETUP",
	"LAP_OFFLINE",
	"LAP_XMIT_P",
	"LAP_PCLOSE",
	"LAP_NRM_P",
	"LAP_RESET_WAIT",
	"LAP_RESET",
	"LAP_NRM_S",
	"LAP_XMIT_S",
	"LAP_SCLOSE",
	"LAP_RESET_CHECK",
};

static int (*state[])( struct irlap_cb *self, IRLAP_EVENT event, 
		       struct sk_buff *skb, struct irlap_info *info) = 
{ 
	irlap_state_ndm,
	irlap_state_query,
	irlap_state_reply,
	irlap_state_conn,
	irlap_state_setup,
	irlap_state_offline,
	irlap_state_xmit_p,
	irlap_state_pclose,
	irlap_state_nrm_p,
	irlap_state_reset_wait,
	irlap_state_reset,
	irlap_state_nrm_s,
	irlap_state_xmit_s,
	irlap_state_sclose,
	irlap_state_reset_check,
};

/*
 * Function irda_poll_timer_expired (data)
 *
 *    
 *
 */
static void irlap_poll_timer_expired( unsigned long data)
{
	struct irlap_cb *self = (struct irlap_cb *) data;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	
	irlap_do_event( self, POLL_TIMER_EXPIRED, NULL, NULL);
}

void irlap_start_poll_timer( struct irlap_cb *self, int timeout)
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

#ifdef CONFIG_IRDA_FAST_RR
	if ( skb_queue_len( &self->tx_list) == 0) {
		if ( self->fast_RR == TRUE) {
			/*
			 *  Assert that the fast poll timer has not reached the
			 *  normal poll timer yet
			 */
			if ( self->fast_RR_timeout < timeout) {
				/*
				 *  FIXME: this should be a more configurable
				 *         function
				 */
				self->fast_RR_timeout += 15;

				/* Use this fast(er) timeout instead */
				timeout = self->fast_RR_timeout;
			}
		} else {
			self->fast_RR = TRUE;

			/* Start with just 1 ms */
			self->fast_RR_timeout = 1;
			timeout = 1;
		}
	} else
		self->fast_RR = FALSE;

	DEBUG( 4, __FUNCTION__ "(), Timeout=%d\n", timeout);
#endif
	irda_start_timer( &self->poll_timer, timeout, 
			  (unsigned long) self, irlap_poll_timer_expired);
}

/*
 * Function irlap_do_event (event, skb, info)
 *
 *    Rushes through the state machine without any delay. If state = XMIT
 *    then send queued data frames. 
 */
void irlap_do_event( struct irlap_cb *self, IRLAP_EVENT event, 
		     struct sk_buff *skb, struct irlap_info *info) 
{
	int ret;
	int iter = 0;
	
	if ( !self || self->magic != LAP_MAGIC) {
		DEBUG( 0, "irlap_do_event: bad pointer *self\n");
		return;
	}
		
  	DEBUG( 4, "irlap_do_event: event = %s, state = %s\n", 
	       irlap_event[ event], irlap_state[ self->state]); 

	/* 
	 *  Do event, this implementation does not deal with pending events. 
	 *  This is because I don't see the need for this. DB
	 */
	ret = (*state[ self->state]) ( self, event, skb, info);
	
	/* 
	 *  Check if we have switched to XMIT state? If so, send queued data 
	 *  frames if any, if -1 is returned it means that we are not allowed 
	 *  to send any more frames.  
	 */
	while (( self->state == LAP_XMIT_P) || ( self->state == LAP_XMIT_S)) { 
		if ( skb_queue_len( &self->tx_list) > 0) {
			
			struct sk_buff *skb = skb_dequeue( &self->tx_list); 
			ASSERT( skb != NULL, return;);
			
			DEBUG( 4, "** Sending queued data frames\n");
			ret = (*state[ self->state])( self, SEND_I_CMD, skb,
						      NULL);
			if ( ret == -EPROTO)
				return; /* Try again later! */
		} else
			return;

		/* Just in case :-) */
		if (iter++ > 100) {
			DEBUG( 0, __FUNCTION__ "(), *** breaking!! ***\n");
			return;
		}
	}
}

/*
 * Function irlap_next_state (self, state)
 *
 *    Switches state and provides debug information
 *
 */
void irlap_next_state( struct irlap_cb *self, IRLAP_STATE state) 
{	
	if ( !self || self->magic != LAP_MAGIC) {
		DEBUG( 4, "irlap_next_state: I have lost myself!\n");
		return;
	}
	
	DEBUG( 4, "next LAP state = %s\n", irlap_state[ state]);

	self->state = state;

	/*
	 *  If we are swithing away from a XMIT state then we are allowed to 
	 *  transmit a maximum number of bytes again when we enter the XMIT 
	 *  state again. Since its possible to "switch" from XMIT to XMIT and
	 *  we cannot do this when swithing into the XMIT state :-)
	 */
	if (( state != LAP_XMIT_P) && ( state != LAP_XMIT_S))
		self->bytes_left = self->window_bytes;
}

/*
 * Function irlap_state_ndm (event, skb, frame)
 *
 *    NDM (Normal Disconnected Mode) state
 *
 */
static int irlap_state_ndm( struct irlap_cb *self, IRLAP_EVENT event, 
			    struct sk_buff *skb,  struct irlap_info *info) 
{
	DISCOVERY *discovery_rsp;
	int ret = 0;
	
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);

	switch( event) {
	case CONNECT_REQUEST:
		ASSERT( self->irdev != NULL, return -1;);

		if ( irda_device_is_media_busy( self->irdev)) {
			DEBUG( 0, __FUNCTION__
			      "(), CONNECT_REQUEST: media busy!\n");
			
			/* Always switch state before calling upper layers */
			irlap_next_state( self, LAP_NDM);
			
			irlap_disconnect_indication( self, LAP_MEDIA_BUSY);
		} else {
			irlap_send_snrm_frame( self, &self->qos_rx);
			
			/* Start Final-bit timer */
			irlap_start_final_timer( self, self->final_timeout);

			self->retry_count = 0;
			irlap_next_state( self, LAP_SETUP);
		}
		break;

	case RECV_SNRM_CMD:
		self->daddr = info->daddr;
		self->caddr = info->caddr;
		
		irlap_next_state( self, LAP_CONN);

		irlap_connect_indication( self, skb);
		break;

	case DISCOVERY_REQUEST:		
		ASSERT( info != NULL, return -1;);

	 	if ( irda_device_is_media_busy( self->irdev)) {
 			DEBUG(0, "irlap_discovery_request: media busy!\n"); 
			/* irlap->log.condition = MEDIA_BUSY; */
			
			/* Always switch state before calling upper layers */
			irlap_next_state( self, LAP_NDM); 
			
			/* This will make IrLMP try again */
 			irlap_discovery_confirm( self, NULL);
			return 0;
	 	} 
		
		self->S = info->S;
		self->s = info->s;
		irlap_send_discovery_xid_frame( self, info->S, info->s, TRUE,
						info->discovery);
		self->s++;

		irlap_start_slot_timer( self, SLOT_TIMEOUT);
		irlap_next_state( self, LAP_QUERY);
		break;

	case RECV_DISCOVERY_XID_CMD:
		ASSERT( info != NULL, return -1;);

		/* Assert that this is not the final slot */
		if ( info->s <= info->S) {
			self->daddr = info->daddr; 
			self->slot = irlap_generate_rand_time_slot( info->S,
								    info->s);
			DEBUG( 4, "XID_CMD: S=%d, s=%d, slot %d\n", info->S, 
			       info->s, self->slot);

			if ( self->slot == info->s) {
				discovery_rsp = irlmp_get_discovery_response();
				
				DEBUG( 4, "Sending XID rsp 1\n");
				irlap_send_discovery_xid_frame( self, info->S, 
								self->slot, 
								FALSE,
								discovery_rsp);
				self->frame_sent = TRUE;
			} else
				self->frame_sent = FALSE;
			
			irlap_start_query_timer( self, QUERY_TIMEOUT);
			irlap_next_state( self, LAP_REPLY);
		}

		dev_kfree_skb( skb);
		break;
		
	default:
		/* 	DEBUG( 0, "irlap_state_ndm: Unknown event"); */
		ret = -1;
		break;
	}	
	return ret;
}

/*
 * Function irlap_state_query (event, skb, info)
 *
 *    QUERY state
 *
 */
static int irlap_state_query( struct irlap_cb *self, IRLAP_EVENT event, 
			      struct sk_buff *skb, struct irlap_info *info) 
{
	int ret = 0;

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);

	switch( event) {
	case RECV_DISCOVERY_XID_RSP:
		ASSERT( info != NULL, return -1;);
		ASSERT( info->discovery != NULL, return -1;);

		DEBUG( 4, __FUNCTION__ "(), daddr=%08x\n", 
		       info->discovery->daddr);

		hashbin_insert( self->discovery_log, 
				(QUEUE *) info->discovery,
				info->discovery->daddr, NULL);

		dev_kfree_skb( skb);

		/* Keep state */
		irlap_next_state( self, LAP_QUERY); 
		break;
	case SLOT_TIMER_EXPIRED:
		if ( self->s < self->S) {
			irlap_send_discovery_xid_frame( self, self->S, 
							self->s, TRUE,
							self->discovery_cmd);
			self->s++;
			irlap_start_slot_timer( self, SLOT_TIMEOUT);
			
			/* Keep state */
			irlap_next_state( self, LAP_QUERY);
		} else {
			/* This is the final slot! */
			irlap_send_discovery_xid_frame( self, self->S, 0xff, 
							TRUE,
							self->discovery_cmd);

			/* Always switch state before calling upper layers */
			irlap_next_state( self, LAP_NDM);
	
			/*
			 *  We are now finished with the discovery procedure, 
			 *  so now we must return the results
			 */
			irlap_discovery_confirm( self, self->discovery_log);
		}
		break;
	default:
		DEBUG( 4, __FUNCTION__ "(), Unknown event %d, %s\n", event, 
		       irlap_event[event]);

		if ( skb != NULL) {
			dev_kfree_skb( skb);
		}
		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_reply (self, event, skb, info)
 *
 *    REPLY, we have received a XID discovery frame from a device and we
 *    are waiting for the right time slot to send a response XID frame
 * 
 */
static int irlap_state_reply( struct irlap_cb *self, IRLAP_EVENT event, 
			      struct sk_buff *skb, struct irlap_info *info) 
{
	DISCOVERY *discovery_rsp;
	int ret=0;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);

	switch( event) {
	case QUERY_TIMER_EXPIRED:
		DEBUG( 0, __FUNCTION__ "(), QUERY_TIMER_EXPIRED <%ld>\n",
		       jiffies);
		irlap_next_state( self, LAP_NDM);
		break;
	case RECV_DISCOVERY_XID_CMD:
		ASSERT( info != NULL, return -1;);
		/*
		 *  Last frame?
		 */
		if ( info->s == 0xff) {
			del_timer( &self->query_timer);
			
			/* info->log.condition = REMOTE; */

			/* Always switch state before calling upper layers */
			irlap_next_state( self, LAP_NDM);

			irlap_discovery_indication( self, info->discovery); 
		} else if (( info->s >= self->slot) && 
			   ( !self->frame_sent)) {
			DEBUG( 4, "Sending XID rsp 2, s=%d\n", info->s); 
			discovery_rsp = irlmp_get_discovery_response();

			irlap_send_discovery_xid_frame( self, info->S, 
							self->slot, FALSE,
							discovery_rsp);

			self->frame_sent = TRUE;
			irlap_next_state( self, LAP_REPLY);
		}
		dev_kfree_skb( skb);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %d, %s\n", event,
		       irlap_event[event]);

		if ( skb != NULL)
			dev_kfree_skb( skb);

		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_conn (event, skb, info)
 *
 *    CONN, we have received a SNRM command and is waiting for the upper
 *    layer to accept or refuse connection 
 *
 */
static int irlap_state_conn( struct irlap_cb *self, IRLAP_EVENT event, 
			     struct sk_buff *skb, struct irlap_info *info) 
{
	int ret = 0;

	DEBUG( 4, __FUNCTION__ "(), event=%s\n", irlap_event[ event]);

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);

	switch( event) {
	case CONNECT_RESPONSE:
		skb_pull( skb, 11);

		ASSERT( self->irdev != NULL, return -1;);
		irda_qos_negotiate( &self->qos_rx, &self->qos_tx, skb);

		irlap_initiate_connection_state( self);

		/*
		 * We are allowed to send two frames!
		 */
		irlap_send_ua_response_frame( self, &self->qos_rx);
		irlap_send_ua_response_frame( self, &self->qos_rx);
		
		irlap_apply_connection_parameters( self, &self->qos_tx);

		/*
		 *  The WD-timer could be set to the duration of the P-timer 
		 *  for this case, but it is recommomended to use twice the 
		 *  value (note 3 IrLAP p. 60). 
		 */
		irlap_start_wd_timer( self, self->wd_timeout);
		irlap_next_state( self, LAP_NRM_S);
		break;

	case RECV_SNRM_CMD:
		DEBUG( 3, __FUNCTION__ "(), event RECV_SNRM_CMD!\n");
#if 0
		irlap_next_state( self, LAP_NDM);
#endif
		break;

	case RECV_DISCOVERY_XID_CMD:
		DEBUG( 3, __FUNCTION__ "(), event RECV_DISCOVER_XID_CMD!\n");
		irlap_next_state( self, LAP_NDM);
		break;

	case DISCONNECT_REQUEST:
		irlap_send_dm_frame( self);
		irlap_next_state( self, LAP_CONN);
		break;

	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %d, %s\n", event, 
		       irlap_event[event]);
		ret = -1;
		break;
	}
	
	return ret;
}

/*
 * Function irlap_state_setup (event, skb, frame)
 *
 *    SETUP state, The local layer has transmitted a SNRM command frame to
 *    a remote peer layer and is awaiting a reply .
 *
 */
static int irlap_state_setup( struct irlap_cb *self, IRLAP_EVENT event, 
			      struct sk_buff *skb, struct irlap_info *info) 
{
	int ret = 0;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);

	switch( event) {
	case FINAL_TIMER_EXPIRED:
		if ( self->retry_count < self->N3) {
/* 
 *  Perform random backoff, Wait a random number of time units, minimum 
 *  duration half the time taken to transmitt a SNRM frame, maximum duration 
 *  1.5 times the time taken to transmit a SNRM frame. So this time should 
 *  between 15 msecs and 45 msecs.
 */
			irlap_start_backoff_timer( self, 2 + (jiffies % 3));
		} else {
			/* Always switch state before calling upper layers */
			irlap_next_state( self, LAP_NDM);

			irlap_disconnect_indication( self, LAP_FOUND_NONE);
		}
		break;
	case BACKOFF_TIMER_EXPIRED:
		irlap_send_snrm_frame( self, &self->qos_rx);
		irlap_start_final_timer( self, self->final_timeout);
		self->retry_count++;
		break;

	case RECV_SNRM_CMD:
		DEBUG( 4, __FUNCTION__ "(), SNRM battle!\n");

		ASSERT( skb != NULL, return 0;);
		ASSERT( info != NULL, return 0;);

		/*
		 *  The device with the largest device address wins the battle
		 *  (both have sent a SNRM command!)
		 */
		if ( info->daddr > self->saddr) {
			del_timer( &self->final_timer);
			irlap_initiate_connection_state( self);

			ASSERT( self->irdev != NULL, return -1;);
			irda_qos_negotiate( &self->qos_rx, &self->qos_tx, skb);
			
			irlap_send_ua_response_frame(self, &self->qos_rx);
			irlap_apply_connection_parameters( self, &self->qos_tx);
			irlap_connect_confirm( self, skb);
			
			/* 
			 *  The WD-timer could be set to the duration of the
			 *  P-timer for this case, but it is recommomended
			 *  to use twice the value (note 3 IrLAP p. 60).  
			 */
			irlap_start_wd_timer( self, self->wd_timeout);
			
			irlap_next_state( self, LAP_NRM_S);
		} else {
			/* We just ignore the other device! */
			irlap_next_state( self, LAP_SETUP);
		}
		break;
	case RECV_UA_RSP:
		/* Stop F-timer */
		del_timer( &self->final_timer);

		/* Initiate connection state */
		irlap_initiate_connection_state( self);

		/* Negotiate connection parameters */
		ASSERT( skb->len > 10, return -1;);
		skb_pull( skb, 10);

		ASSERT( self->irdev != NULL, return -1;);
		irda_qos_negotiate( &self->qos_rx, &self->qos_tx, skb);

		irlap_apply_connection_parameters( self, &self->qos_tx); 
		self->retry_count = 0;
		irlap_send_rr_frame( self, CMD_FRAME);

		irlap_start_final_timer( self, self->final_timeout/2);
		irlap_next_state( self, LAP_NRM_P);

		irlap_connect_confirm( self, skb);
		break;

	case RECV_DISC_FRAME:
		del_timer( &self->final_timer);
		irlap_next_state( self, LAP_NDM);

		irlap_disconnect_indication( self, LAP_DISC_INDICATION);
		break;

       /* DM handled in irlap_frame.c, irlap_input() */
		
	default:
		DEBUG( 4, "irlap_state_setup: Unknown event");
		ret = -1;
		break;
	}	
	return ret;
}

/*
 * Function irlap_state_offline (self, event, skb, info)
 *
 *    OFFLINE state, not used for now!
 *
 */
static int irlap_state_offline( struct irlap_cb *self, IRLAP_EVENT event, 
				struct sk_buff *skb, struct irlap_info *info) 
{
	DEBUG( 0, __FUNCTION__ "(), Unknown event\n");

	return -1;
}

/*
 * Function irlap_state_xmit_p (self, event, skb, info)
 * 
 *    XMIT, Only the primary station has right to transmit, and we therefor
 *    do not expect to receive any transmissions from other stations.  
 *
 */
static int irlap_state_xmit_p( struct irlap_cb *self, IRLAP_EVENT event, 
			       struct sk_buff *skb, struct irlap_info *info) 
{
	int ret = 0;
	
	ASSERT( self != NULL, return -ENODEV;);
	ASSERT( self->magic == LAP_MAGIC, return -EBADR;);

	DEBUG( 4, __FUNCTION__ "(), event=%s, vs=%d, vr=%d", 
	       irlap_event[ event], self->vs, self->vr); 
		
	switch( event) {
	case SEND_I_CMD:
		ASSERT( skb != NULL, return -1;);
		DEBUG( 4, __FUNCTION__ "(), Window=%d\n", self->window);
		
		/*
		 *  Only send frame if send-window > 0.
		 */ 
		if (( self->window > 0) && ( !self->remote_busy)) {

			/*
			 *  Test if we have transmitted more bytes over the 
			 *  link than its possible to do with the current 
			 *  speed and turn-around-time.
			 */
			if (( skb->len+self->bofs_count) > self->bytes_left) {
				DEBUG( 4, __FUNCTION__ "(), Not allowed to "
				       "transmit more bytes!\n");
				skb_queue_head( &self->tx_list, skb);

				/*
				 *  We should switch state to LAP_NRM_P, but
				 *  that is not possible since we must be sure
				 *  that we poll the other side. Since we have
				 *  used up our time, the poll timer should
				 *  trigger anyway now,so we just wait for it
				 *  DB
				 */
				return -EPROTO;
			}
			self->bytes_left -= ( skb->len + self->bofs_count);

			/*
			 *  Send data with poll bit cleared only if window > 1
			 *  and there is more frames after this one to be sent
			 */
			if (( self->window > 1) && 
			    skb_queue_len( &self->tx_list) > 0) 
			{   
				DEBUG( 4, __FUNCTION__ "(), window > 1\n");
				irlap_send_data_primary( self, skb);
				irlap_next_state( self, LAP_XMIT_P);
			} else {
				DEBUG( 4, __FUNCTION__ "(), window <= 1\n");
				irlap_send_data_primary_poll( self, skb);
				irlap_next_state( self, LAP_NRM_P);
			}
#ifdef CONFIG_IRDA_FAST_RR
			/* Peer may want to reply immediately */
			self->fast_RR = FALSE;
#endif
		} else {
			DEBUG( 4, __FUNCTION__ 
			       "(), Unable to send! remote busy?\n");
			skb_queue_head( &self->tx_list, skb);

			/*
			 *  The next ret is important, because it tells 
			 *  irlap_next_state _not_ to deliver more frames
			 */
			ret = -EPROTO;
		}
		break;
	case DISCONNECT_REQUEST:
		del_timer( &self->poll_timer);
		irlap_wait_min_turn_around( self, &self->qos_tx);
		irlap_send_disc_frame( self);
		irlap_flush_all_queues( self);
		irlap_start_final_timer( self, self->final_timeout);
		self->retry_count = 0;
		irlap_next_state( self, LAP_PCLOSE);
		break;
	case POLL_TIMER_EXPIRED:
		irlap_send_rr_frame( self, CMD_FRAME);
		irlap_start_final_timer( self, self->final_timeout);
		irlap_next_state( self, LAP_NRM_P);
		break;
	default:
		/* DEBUG( 0, "irlap_state_xmit: Unknown event"); */
		ret = -EINVAL;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_pclose (event, skb, info)
 *
 *    PCLOSE state
 */
static int irlap_state_pclose( struct irlap_cb *self, IRLAP_EVENT event, 
			       struct sk_buff *skb, struct irlap_info *info) 
{
	int ret = 0;

	DEBUG( 0, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);	

	switch( event) {
	case RECV_UA_RSP:
		del_timer( &self->final_timer);
		
		irlap_apply_default_connection_parameters( self);

		/* Always switch state before calling upper layers */
		irlap_next_state( self, LAP_NDM);

		irlap_disconnect_indication( self, LAP_DISC_INDICATION);

		break;
	case FINAL_TIMER_EXPIRED:
		if ( self->retry_count < self->N3) {
			irlap_wait_min_turn_around( self, &self->qos_tx);
			irlap_send_disc_frame( self);
			irlap_start_final_timer( self, self->final_timeout);
			self->retry_count++;
			/* Keep state */
		} else {
			irlap_apply_default_connection_parameters( self);

			/* 
			 *  Always switch state before calling upper layers 
			 */
			irlap_next_state( self, LAP_NDM);

			irlap_disconnect_indication( self, LAP_NO_RESPONSE);
		}
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %d\n", event);
		ret = -1;
		break;	
	}
	return ret;
}

/*
 * Function irlap_state_nrm_p (self, event, skb, info)
 *
 *   NRM_P (Normal Response Mode as Primary), The primary station has given
 *   permissions to a secondary station to transmit IrLAP resonse frames
 *   (by sending a frame with the P bit set). The primary station will not
 *   transmit any frames and is expecting to receive frames only from the
 *   secondary to which transmission permissions has been given.
 */
static int irlap_state_nrm_p( struct irlap_cb *self, IRLAP_EVENT event, 
			      struct sk_buff *skb, struct irlap_info *info) 
{
	int ret = 0;
	int ns_status;
	int nr_status;

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);

	switch( event) {
	case RECV_RR_RSP:
		DEBUG( 4, __FUNCTION__ "(), RECV_RR_FRAME: "
		       "Retrans:%d, nr=%d, va=%d, vs=%d, vr=%d\n",
		       self->retry_count, info->nr, self->va, self->vs, 
		       self->vr);

		ASSERT( info != NULL, return -1;);

		/*  
		 *  If you get a RR, the remote isn't busy anymore, 
		 *  no matter what the NR 
		 */
		self->remote_busy = FALSE;

		/* 
		 *  Nr as expected? 
		 */
		ret = irlap_validate_nr_received( self, info->nr);
		if ( ret == NR_EXPECTED) {	
			/* Stop final timer */
			del_timer( &self->final_timer);
			
			/* Update Nr received */
			irlap_update_nr_received( self, info->nr);
			
			/*
			 *  Got expected NR, so reset the retry_count. This 
			 *  is not done by the IrLAP standard , which is 
			 *  strange! DB.
			 */
			self->retry_count = 0;			
			irlap_wait_min_turn_around( self, &self->qos_tx);

			/* Start poll timer */
			irlap_start_poll_timer( self, self->poll_timeout);

			irlap_next_state( self, LAP_XMIT_P);
		} else if ( ret == NR_UNEXPECTED) {
			ASSERT( info != NULL, return -1;);	
			/* 
			 *  Unexpected nr! 
			 */
			
			/* Update Nr received */
			irlap_update_nr_received( self, info->nr);

			DEBUG( 4, "RECV_RR_FRAME: Retrans:%d, nr=%d, va=%d, "
			       "vs=%d, vr=%d\n",
			       self->retry_count, info->nr, self->va, 
			       self->vs, self->vr);
			
			/* Resend rejected frames */
			irlap_resend_rejected_frames( self, CMD_FRAME);
			
			/*
			 *  Start only if not running, DB
			 *  TODO: Should this one be here?
			 */
			/* if ( !self->final_timer.prev) */
/* 				irda_start_timer( FINAL_TIMER, self->final_timeout);  */

			/* Keep state */
			irlap_next_state( self, LAP_NRM_P);
		} else if ( ret == NR_INVALID) {
			DEBUG( 0, "irlap_state_nrm_p: received RR with "
			       "invalid nr !\n");
			del_timer( &self->final_timer);

			irlap_next_state( self, LAP_RESET_WAIT);

			irlap_disconnect_indication( self, 
						     LAP_RESET_INDICATION);
			self->xmitflag = TRUE;
		}
		if (skb)
			dev_kfree_skb( skb);
		break;
	case RECV_RNR_FRAME:
		DEBUG( 4, "irlap_state_nrm_p: RECV_RNR_FRAME: Retrans:%d, "
		       "nr=%d, va=%d, vs=%d, vr=%d\n",
		       self->retry_count, info->nr, self->va, self->vs, 
		       self->vr);

		ASSERT( info != NULL, return -1;);

		/* Stop final timer */
		del_timer( &self->final_timer);
		self->remote_busy = TRUE;

		/* Update Nr received */
		irlap_update_nr_received( self, info->nr);
			
		/* Start poll timer */
		irlap_start_poll_timer( self, self->poll_timeout);

		irlap_next_state( self, LAP_XMIT_P);

		dev_kfree_skb( skb);
		break;
	case RECV_I_RSP:
		/* FIXME: must check for remote_busy below */
#ifdef CONFIG_IRDA_FAST_RR
		/* 
		 *  Reset the fast_RR so we can use the fast RR code with
		 *  full speed the next time since peer may have more frames
		 *  to transmitt
		 */
		self->fast_RR = FALSE;
#endif

		ASSERT( info != NULL, return -1;);

		ns_status = irlap_validate_ns_received( self, info->ns);
		nr_status = irlap_validate_nr_received( self, info->nr);

		/* 
		 *  Check for expected I(nformation) frame
		 */
		if ((ns_status == NS_EXPECTED) && (nr_status == NR_EXPECTED)) {
			/* 
			 *  poll bit cleared?
			 */
			if ( !info->pf) {
				self->vr = (self->vr + 1) % 8;
			
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
				
				self->ack_required = TRUE;
				
				/* Keep state, do not move this line */
				irlap_next_state( self, LAP_NRM_P);
				
				irlap_data_indication( self, skb);
			} else {
				del_timer( &self->final_timer);

				self->vr = (self->vr + 1) % 8;
			
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
		
				/*  
				 *  Got expected NR, so reset the
				 *  retry_count. This is not done by IrLAP,
				 *  which is strange!  
				 */
				self->retry_count = 0;
				self->ack_required = TRUE;
			
				/* This is the last frame */
				irlap_start_poll_timer( self, self->poll_timeout);
				irlap_wait_min_turn_around( self, &self->qos_tx);
				/* Do not move this line */
				irlap_next_state( self, LAP_XMIT_P);
			
				irlap_data_indication( self, skb);
			}
			break;
			
		}
		/*
		 *  Unexpected next to send (Ns)
		 */
		if (( ns_status == NS_UNEXPECTED) && 
		    ( nr_status == NR_EXPECTED)) 
		{
			if ( !info->pf) {
				irlap_update_nr_received( self, info->nr);
				
				/*
				 *  Wait until the last frame before doing 
				 *  anything
				 */

				/* Keep state */
				irlap_next_state( self, LAP_NRM_P);
			} else {
				DEBUG( 4, __FUNCTION__
				       "(), missing or duplicate frame!\n");
				
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
				
				irlap_wait_min_turn_around( self, &self->qos_tx);
				irlap_send_rr_frame( self, CMD_FRAME);
				
				self->ack_required = FALSE;
			
				irlap_start_final_timer( self, self->final_timeout);
				irlap_next_state( self, LAP_NRM_P);
			}
			dev_kfree_skb( skb);
			break;
		}
		/* 
		 *  Unexpected next to receive (Nr) 
		 */
		if (( ns_status == NS_EXPECTED) && 
		    ( nr_status == NR_UNEXPECTED))
		{
			if ( info->pf) {
				self->vr = (self->vr + 1) % 8;
			
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
			
				/* Resend rejected frames */
				irlap_resend_rejected_frames( self, CMD_FRAME);
				
				self->ack_required = FALSE;
				irlap_start_final_timer( self, self->final_timeout);
				
				/* Keep state, do not move this line */
				irlap_next_state( self, LAP_NRM_P);
				
				irlap_data_indication( self, skb);
			} else {
				/* 
				 *  Do not resend frames until the last
				 *  frame has arrived from the other
				 *  device. This is not documented in
				 *  IrLAP!!  
				 */
				self->vr = (self->vr + 1) % 8;

				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
				
				self->ack_required = FALSE;

				/* Keep state, do not move this line!*/
				irlap_next_state( self, LAP_NRM_P); 
				
				irlap_data_indication( self, skb);
			}
			break;
		}
		/*
		 *  Unexpected next to send (Ns) and next to receive (Nr)
		 *  Not documented by IrLAP!
		 */
		if (( ns_status == NS_UNEXPECTED) && 
		    ( nr_status == NR_UNEXPECTED)) 
		{
			DEBUG( 4, "IrLAP: unexpected nr and ns!\n");
			if ( info->pf) {
				/* Resend rejected frames */
				irlap_resend_rejected_frames( self, CMD_FRAME);
				
				/* Give peer some time to retransmit! */
				irlap_start_final_timer( self, self->final_timeout);

				/* Keep state, do not move this line */
				irlap_next_state( self, LAP_NRM_P);
			} else {
				/* Update Nr received */
				/* irlap_update_nr_received( info->nr); */
				
				self->ack_required = FALSE;
			}
			break;
		}

		/*
		 *  Invalid NR or NS
		 */
		if (( nr_status == NR_INVALID) || ( ns_status == NS_INVALID)) {
			if ( info->pf) {
				del_timer( &self->final_timer);
				
				irlap_next_state( self, LAP_RESET_WAIT);

				irlap_disconnect_indication( self, LAP_RESET_INDICATION);
				self->xmitflag = TRUE;
			} else {
				del_timer( &self->final_timer);
				
				irlap_disconnect_indication( self, LAP_RESET_INDICATION);
				
				self->xmitflag = FALSE;
			}
			break;
		}
		DEBUG( 0, "irlap_state_nrm_p: Not implemented!\n");
		DEBUG( 0, "event=%s, ns_status=%d, nr_status=%d\n", 
		       irlap_event[ event], ns_status, nr_status);
		break;
	case RECV_UI_FRAME:
		/*  poll bit cleared?  */
		if ( !info->pf) {
			irlap_unit_data_indication( self, skb);
			irlap_next_state( self, LAP_NRM_P);
		} else {
			del_timer( &self->final_timer);
			irlap_unit_data_indication( self, skb);
			irlap_start_poll_timer( self, self->poll_timeout);
		}
		break;
	case RECV_FRMR_RSP:
		del_timer( &self->final_timer);
		self->xmitflag = TRUE;
		irlap_next_state( self, LAP_RESET_WAIT);
		irlap_reset_indication( self);
		break;
	case FINAL_TIMER_EXPIRED:
		/* 
		 *  We are allowed to wait for additional 300 ms if
		 *  final timer expires when we are in the middle
		 *  of receiving a frame (page 45, IrLAP). Check that
		 *  we only do this once for each frame.
		 */
		if ( irda_device_is_receiving( self->irdev) && 
		     !self->add_wait) {
			DEBUG( 4, "FINAL_TIMER_EXPIRED when receiving a "
			       "frame! Waiting a little bit more!\n");
			irlap_start_final_timer( self, 30);

			/*
			 *  Don't allow this to happen one more time in a row, 
			 *  or else we can get a pretty tight loop here if 
			 *  if we only receive half a frame. DB.
			 */
			self->add_wait = TRUE;
			break;
		}
		self->add_wait = FALSE;

		if (( self->retry_count < self->N2) && 
		    ( self->retry_count != self->N1)) {
			
			irlap_wait_min_turn_around( self, &self->qos_tx);
			irlap_send_rr_frame( self, CMD_FRAME);
			
			irlap_start_final_timer( self, self->final_timeout);
		 	self->retry_count++;

			DEBUG( 4, "irlap_state_nrm_p: FINAL_TIMER_EXPIRED:"
			       " retry_count=%d\n", self->retry_count);
			/* Keep state */
		} else if ( self->retry_count == self->N1) {
			irlap_status_indication( STATUS_NO_ACTIVITY);
			irlap_wait_min_turn_around( self, &self->qos_tx);
			irlap_send_rr_frame( self, CMD_FRAME);
			
			irlap_start_final_timer( self, self->final_timeout);
			self->retry_count++;

			DEBUG( 4, "retry count = N1; retry_count=%d\n", 
			       self->retry_count);
			/* Keep state */
		} else if ( self->retry_count >= self->N2) {
			irlap_apply_default_connection_parameters( self);

			/* Always switch state before calling upper layers */
			irlap_next_state( self, LAP_NDM);
			irlap_disconnect_indication( self, LAP_NO_RESPONSE);
		}
		break;
	case RECV_DISC_FRAME: /* FIXME: Check how this is in the standard! */
		DEBUG( 0, __FUNCTION__ "(), RECV_DISC_FRAME()\n");

		/* Always switch state before calling upper layers */
		irlap_next_state( self, LAP_NDM);
		
		irlap_wait_min_turn_around( self, &self->qos_tx);
		irlap_send_ua_response_frame( self, NULL);

 		del_timer( &self->final_timer);
		/* del_timer( &self->poll_timer); */

		irlap_flush_all_queues( self);
		irlap_apply_default_connection_parameters( self);

		irlap_disconnect_indication( self, LAP_DISC_INDICATION);
		if (skb)
			dev_kfree_skb( skb);
		
		break;
	default:
		/* DEBUG( 0, "irlap_state_nrm_p: Unknown event"); */
		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_reset_wait (event, skb, info)
 *
 *    We have informed the service user of a reset condition, and is
 *    awaiting reset of disconnect request.
 *
 */
int irlap_state_reset_wait( struct irlap_cb *self, IRLAP_EVENT event, 
			    struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;
	
	DEBUG( 3, __FUNCTION__ "(), event = %s\n", irlap_event[event]);
	
	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);
	
	switch( event) {
	case RESET_REQUEST:
		if ( self->xmitflag) {
			irlap_wait_min_turn_around( self, &self->qos_tx);
			irlap_send_snrm_frame( self, NULL);
			irlap_start_final_timer( self, self->final_timeout);
			irlap_next_state( self, LAP_RESET);
		} else {
			irlap_start_final_timer( self, self->final_timeout);
			irlap_next_state( self, LAP_RESET);
		}
		break;
	case DISCONNECT_REQUEST:
		irlap_wait_min_turn_around( self, &self->qos_tx);
		irlap_send_disc_frame( self);
		irlap_flush_all_queues( self);
		irlap_start_final_timer( self, self->final_timeout);
		self->retry_count = 0;
		irlap_next_state( self, LAP_PCLOSE);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %s\n", 
		       irlap_event[event]);
		ret = -1;
		break;	
	}
	return ret;
}

/*
 * Function irlap_state_reset (self, event, skb, info)
 *
 *    We have sent a SNRM reset command to the peer layer, and is awaiting
 *    reply.
 *
 */
int irlap_state_reset( struct irlap_cb *self, IRLAP_EVENT event, 
		       struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;
	
	DEBUG( 3, __FUNCTION__ "(), event = %s\n", irlap_event[event]);
	
	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);
	
	switch( event) {
	case RECV_DISC_FRAME:
		del_timer( &self->final_timer);

		irlap_apply_default_connection_parameters( self);

		/* Always switch state before calling upper layers */
		irlap_next_state( self, LAP_NDM);

		irlap_disconnect_indication( self, LAP_NO_RESPONSE);
		break;
	case RECV_UA_RSP:
		del_timer( &self->final_timer);
		
		/* Initiate connection state */
		irlap_initiate_connection_state( self);
		
		irlap_reset_confirm();
		
		self->remote_busy = FALSE;
		irlap_start_poll_timer( self, self->poll_timeout);
		irlap_next_state( self, LAP_XMIT_P);
		break;
	case FINAL_TIMER_EXPIRED:
		if ( self->retry_count < 3) {
			irlap_wait_min_turn_around( self, &self->qos_tx);

			ASSERT( self->irdev != NULL, return -1;);
			irlap_send_snrm_frame(self, 
					      irda_device_get_qos( self->irdev));

			self->retry_count++; /* Experimental!! */

			irlap_start_final_timer( self, self->final_timeout);
			irlap_next_state( self, LAP_RESET);
		} else if ( self->retry_count >= self->N3) {
			irlap_apply_default_connection_parameters( self);
			
			/* Always switch state before calling upper layers */
			irlap_next_state( self, LAP_NDM);
			
			irlap_disconnect_indication( self, LAP_NO_RESPONSE);
		}
		break;

	case RECV_SNRM_CMD:
		DEBUG(3, "lap_reset: RECV_SNRM_CMD\n");
		irlap_initiate_connection_state( self);
		irlap_wait_min_turn_around( self, &self->qos_tx);
		irlap_send_ua_response_frame( self, &self->qos_rx);
		irlap_reset_confirm();
		irlap_start_wd_timer( self, self->wd_timeout);
		irlap_next_state( self, LAP_NDM);
		break;

	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %s\n", 
		       irlap_event[ event]);
		ret = -1;
		break;	
	}
	return ret;
}

/*
 * Function irlap_state_xmit_s (event, skb, info)
 * 
 *   XMIT_S, The secondary station has been given the right to transmit,
 *   and we therefor do not expect to receive any transmissions from other
 *   stations.  
 */
static int irlap_state_xmit_s( struct irlap_cb *self, IRLAP_EVENT event, 
			       struct sk_buff *skb, struct irlap_info *info) 
{
	int ret = 0;
	
	DEBUG( 4, __FUNCTION__ "(), event=%s\n", irlap_event[ event]); 

	ASSERT( self != NULL, return -ENODEV;);
	ASSERT( self->magic == LAP_MAGIC, return -EBADR;);
	
	switch( event) {
	case SEND_I_CMD:
		/*
		 *  Send frame only if send window > 1
		 */ 
		if (( self->window > 0) && ( !self->remote_busy)) {
			/*
			 *  Test if we have transmitted more bytes over the 
			 *  link than its possible to do with the current 
			 *  speed and turn-around-time.
			 */
			if (( skb->len+self->bofs_count) > self->bytes_left) {
				DEBUG( 4, "IrDA: Not allowed to transmit more bytes!\n");
				skb_queue_head( &self->tx_list, skb);
				/*
				 *  Switch to NRM_S, this is only possible
				 *  when we are in secondary mode, since we 
				 *  must be sure that we don't miss any RR
				 *  frames
				 */
				irlap_next_state( self, LAP_NRM_S);

				return -EPROTO; /* Try again later */
			}
			self->bytes_left -= ( skb->len + self->bofs_count);

			/*
			 *  Send data with final bit cleared only if window > 1
			 *  and there is more frames to be sent
			 */
			if (( self->window > 1) && 
			    skb_queue_len( &self->tx_list) > 0) 
			{   
				DEBUG( 4, __FUNCTION__ "(), window > 1\n");
				irlap_send_data_secondary( self, skb);
				irlap_next_state( self, LAP_XMIT_S);
			} else {
				DEBUG( 4, "(), window <= 1\n");
				irlap_send_data_secondary_final( self, skb);
				irlap_next_state( self, LAP_NRM_S);
			}
		} else {
			DEBUG( 0, __FUNCTION__ "(), Unable to send!\n");
			skb_queue_head( &self->tx_list, skb);
			ret = -EPROTO;
		}
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %s\n", 
		       irlap_event[ event]);
		ret = -EINVAL;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_nrm_s (event, skb, info)
 *
 *    NRM_S (Normal Response Mode as Secondary) state, in this state we are 
 *    expecting to receive frames from the primary station
 *
 */
static int irlap_state_nrm_s( struct irlap_cb *self, IRLAP_EVENT event, 
			      struct sk_buff *skb, struct irlap_info *info) 
{
	int ret = 0;
	int ns_status;
	int nr_status;

	DEBUG( 4, __FUNCTION__ "(), event=%s\n", irlap_event[ event]);

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);

	switch( event) {
	case RECV_RR_CMD:
		self->retry_count = 0;

		/* 
		 *  Nr as expected? 
		 */
		nr_status = irlap_validate_nr_received( self, info->nr);
		if ( nr_status == NR_EXPECTED) {
			if (( skb_queue_len( &self->tx_list) > 0) && 
			    ( self->window > 0)) {
				self->remote_busy = FALSE;
				
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
				del_timer( &self->wd_timer);
				
				irlap_wait_min_turn_around( self, &self->qos_tx);
				irlap_next_state( self, LAP_XMIT_S);
			} else {			
				self->remote_busy = FALSE;
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
				irlap_wait_min_turn_around( self, &self->qos_tx);
				
				irlap_send_rr_frame( self, RSP_FRAME);
				
				irlap_start_wd_timer( self, self->wd_timeout);
				irlap_next_state( self, LAP_NRM_S);
			}
		} else if ( nr_status == NR_UNEXPECTED) {
			self->remote_busy = FALSE;
			irlap_update_nr_received( self, info->nr);
			irlap_resend_rejected_frames( self, RSP_FRAME);

			irlap_start_wd_timer( self, self->wd_timeout);

			/* Keep state */
			irlap_next_state( self, LAP_NRM_S); 
		} else {
			DEBUG( 0, __FUNCTION__ "(), "
			       "invalid nr not implemented!\n");
		} 
		if ( skb)
			dev_kfree_skb( skb);

		break;
	case RECV_I_CMD:
		/* FIXME: must check for remote_busy below */
		DEBUG( 4, __FUNCTION__ "(), event=%s nr=%d, vs=%d, ns=%d, "
		       "vr=%d, pf=%d\n", irlap_event[event], info->nr, 
		       self->vs, info->ns, self->vr, info->pf);

		self->retry_count = 0;

		ns_status = irlap_validate_ns_received( self, info->ns);
		nr_status = irlap_validate_nr_received( self, info->nr);
		/* 
		 *  Check for expected I(nformation) frame
		 */
		if ((ns_status == NS_EXPECTED) && (nr_status == NR_EXPECTED)) {
			/* 
			 *  poll bit cleared?
			 */
			if ( !info->pf) {
				self->vr = (self->vr + 1) % 8;
				
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
				
				self->ack_required = TRUE;
				
				/*
				 *  Starting WD-timer here is optional, but
				 *  not recommended. Note 6 IrLAP p. 83
				 */
				/* irda_start_timer( WD_TIMER, self->wd_timeout); */

				/* Keep state, do not move this line */
				irlap_next_state( self, LAP_NRM_S);
				
				irlap_data_indication( self, skb);
				break;
			} else {
				self->vr = (self->vr + 1) % 8;
				
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
				
				/* 
				 *  We should wait before sending RR, and
				 *  also before changing to XMIT_S
				 *  state. (note 1, IrLAP p. 82) 
				 */
				irlap_wait_min_turn_around( self, &self->qos_tx);
				/*
				 *  Any pending data requests?
				 */
				if (( skb_queue_len( &self->tx_list) > 0) && 
				    ( self->window > 0)) 
				{
					self->ack_required = TRUE;
					
					del_timer( &self->wd_timer);
					
					irlap_next_state( self, LAP_XMIT_S);
				} else {
					irlap_send_rr_frame( self, RSP_FRAME);
					irlap_start_wd_timer( self, self->wd_timeout);

					/* Keep the state */
					irlap_next_state( self, LAP_NRM_S);
				}
				irlap_data_indication( self, skb);

				break;
			}
		}
		/*
		 *  Check for Unexpected next to send (Ns)
		 */
		if (( ns_status == NS_UNEXPECTED) && 
		    ( nr_status == NR_EXPECTED)) 
		{
			/* Unexpected next to send, with final bit cleared */
			if ( !info->pf) {
				irlap_update_nr_received( self, info->nr);
				
				irlap_start_wd_timer( self, self->wd_timeout);
			} else {
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
			
				irlap_wait_min_turn_around( self, &self->qos_tx);
				irlap_send_rr_frame( self, CMD_FRAME);
			
				irlap_start_wd_timer( self, self->wd_timeout);
			}
			dev_kfree_skb( skb);
			break;
		}

		/* 
		 *  Unexpected Next to Receive(NR) ?
		 */
		if (( ns_status == NS_EXPECTED) && 
		    ( nr_status == NR_UNEXPECTED))
		{
			if ( info->pf) {
				DEBUG( 4, "RECV_I_RSP: frame(s) lost\n");
				
				self->vr = (self->vr + 1) % 8;
				
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
				
				/* Resend rejected frames */
				irlap_resend_rejected_frames( self, RSP_FRAME);

				/* Keep state, do not move this line */
				irlap_next_state( self, LAP_NRM_S);

				irlap_data_indication( self, skb);
				irlap_start_wd_timer( self, self->wd_timeout);

				break;
			}
			/*
			 *  This is not documented in IrLAP!! Unexpected NR
			 *  with poll bit cleared
			 */
			if ( !info->pf) {
				self->vr = (self->vr + 1) % 8;
				
				/* Update Nr received */
				irlap_update_nr_received( self, info->nr);
				
				/* Keep state, do not move this line */
				irlap_next_state( self, LAP_NRM_S);
				
				irlap_data_indication( self, skb);
				irlap_start_wd_timer( self, self->wd_timeout);
			}
		}
		
		if ( ret == NR_INVALID) {
			DEBUG( 0, "NRM_S, NR_INVALID not implemented!\n");
		}
		if ( ret == NS_INVALID) {
			DEBUG( 0, "NRM_S, NS_INVALID not implemented!\n");
		}
		break;
	case RECV_UI_FRAME:
		/* 
		 *  poll bit cleared?
		 */
		if ( !info->pf) {
			irlap_unit_data_indication( self, skb);
			irlap_next_state( self, LAP_NRM_S); /* Keep state */
		} else {
			/*
			 *  Any pending data requests?
			 */
			if (( skb_queue_len( &self->tx_list) > 0) && 
			    ( self->window > 0) && !self->remote_busy) 
			{
				irlap_unit_data_indication( self, skb);
				
				del_timer( &self->wd_timer);

				irlap_next_state( self, LAP_XMIT_S);
			} else {
				irlap_unit_data_indication( self, skb);

				irlap_wait_min_turn_around( self, &self->qos_tx);

				irlap_send_rr_frame( self, RSP_FRAME);
				self->ack_required = FALSE;
				
				irlap_start_wd_timer( self, self->wd_timeout);

				/* Keep the state */
				irlap_next_state( self, LAP_NRM_S);
			}
		}
		break;
	case RECV_SNRM_CMD:
		del_timer( &self->wd_timer);
		DEBUG( 0, "irlap_state_nrm_s: received SNRM cmd\n");
		irlap_next_state( self, LAP_RESET_CHECK);

		irlap_reset_indication( self);
		break;
	case WD_TIMER_EXPIRED:
		DEBUG( 4, "WD_TIMER_EXPIRED: %ld\n", jiffies);
	
		/*
		 *  Wait until retry_count * n matches negotiated threshold/
		 *  disconnect time (note 2 in IrLAP p. 82)
		 */
		DEBUG( 0, "retry_count = %d\n", self->retry_count);

		if (( self->retry_count < (self->N2/2))  && 
		    ( self->retry_count != self->N1/2)) {
			
			irlap_start_wd_timer( self, self->wd_timeout);
			self->retry_count++;		
		} else if ( self->retry_count == (self->N1/2)) {
			irlap_status_indication( STATUS_NO_ACTIVITY);
			irlap_start_wd_timer( self, self->wd_timeout);
			self->retry_count++;
		} else if ( self->retry_count >= self->N2/2) {
			irlap_apply_default_connection_parameters( self);
			
			/* Always switch state before calling upper layers */
			irlap_next_state( self, LAP_NDM);

			irlap_disconnect_indication( self, LAP_NO_RESPONSE);
		}
		break;

	case RECV_DISC_FRAME:
		/* Always switch state before calling upper layers */
		irlap_next_state( self, LAP_NDM);

		irlap_wait_min_turn_around( self, &self->qos_tx);
		irlap_send_ua_response_frame( self, NULL);
		del_timer( &self->wd_timer);
		irlap_flush_all_queues( self);
		irlap_apply_default_connection_parameters( self);

		irlap_disconnect_indication( self, LAP_DISC_INDICATION);
		if (skb)
			dev_kfree_skb( skb);
		
		break;

	case RECV_DISCOVERY_XID_CMD:
		DEBUG( 3, "irlap_state_nrm_s: got event RECV_DISCOVER_XID_CMD!\n");
		del_timer( &self->final_timer);
		
		irlap_apply_default_connection_parameters( self);

		/* Always switch state before calling upper layers */
		irlap_next_state( self, LAP_NDM);

		irlap_disconnect_indication( self, LAP_DISC_INDICATION);
		
#if 0
		irlap_wait_min_turn_around( self, &self->qos_session);
		irlap_send_rr_frame( RSP_FRAME);
		irda_start_timer( WD_TIMER, self->wd_timeout);
		irlap_next_state( self, LAP_NRM_S);
#endif
		break;

	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %d, (%s)\n", 
		       event, irlap_event[event]);
		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_sclose (self, event, skb, info)
 *
 *    
 *
 */
static int irlap_state_sclose( struct irlap_cb *self, IRLAP_EVENT event, 
			       struct sk_buff *skb, struct irlap_info *info) 
{
	DEBUG( 0, __FUNCTION__ "(), Not implemented!\n");

	return -1;
}

static int irlap_state_reset_check( struct irlap_cb *self, IRLAP_EVENT event, 
				   struct sk_buff *skb, 
				   struct irlap_info *info) 
{
	int ret = 0;

	DEBUG( 0, __FUNCTION__ "(), event=%s\n", irlap_event[ event]); 

	ASSERT( self != NULL, return -ENODEV;);
	ASSERT( self->magic == LAP_MAGIC, return -EBADR;);
	
	switch( event) {
	case RESET_RESPONSE:
		irlap_send_ua_response_frame( self, &self->qos_rx);
		irlap_initiate_connection_state( self);
		irlap_start_wd_timer( self, WD_TIMEOUT);
		irlap_flush_all_queues( self);
		
		irlap_next_state( self, LAP_NRM_S);
		break;
	case DISCONNECT_REQUEST:
		irlap_wait_min_turn_around( self, &self->qos_tx);
		/* irlap_send_rd_frame(self); */
		irlap_start_wd_timer( self, WD_TIMEOUT);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown event %d, (%s)\n", 
		       event, irlap_event[event]);
		ret = -1;
		break;
	}

	return ret;
}




