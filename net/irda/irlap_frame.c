/*********************************************************************
 *                
 * Filename:      irlap_frame.c
 * Version:       0.3
 * Description:   Build and transmit IrLAP frames
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Aug 19 10:27:26 1997
 * Modified at:   Tue Jan 19 22:58:13 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>, All Rights Resrved.
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
#include <net/sock.h>

#include <linux/skbuff.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <net/pkt_sched.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>
#include <net/irda/irlap.h>
#include <net/irda/wrapper.h>
#include <net/irda/timer.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/qos.h>

extern __u8 *irlmp_hint_to_service( __u8 *hint);

/*
 * Function irlap_insert_mtt (self, skb)
 *
 *    Insert minimum turnaround time relevant information into the skb. We 
 *    need to do this since it's per packet relevant information.
 *
 */
__inline__ void irlap_insert_mtt( struct irlap_cb *self, struct sk_buff *skb)
{
	struct irlap_skb_cb *cb;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	cb = (struct irlap_skb_cb *) skb->cb;
	
	cb->mtt = self->mtt_required;
	
	/* Reset */
	self->mtt_required = 0;
	
	/* 
	 * Delay equals negotiated BOFs count plus the number of BOFs to 
	 * force the negotiated minimum turnaround time 
	 */
	cb->xbofs = self->bofs_count+self->xbofs_delay;
	
	/* Reset XBOF's delay (used only for getting min turn time) */
	self->xbofs_delay = 0;

	DEBUG( 4, __FUNCTION__ "(), using %d xbofs\n", cb->xbofs);
}

/*
 * Function irlap_send_snrm_cmd (void)
 *
 *    Transmits a connect SNRM command frame
 */
void irlap_send_snrm_frame( struct irlap_cb *self, struct qos_info *qos) 
{
	struct sk_buff *skb;
	__u8 *frame;
	int len;
	int n;

	DEBUG( 4, "irlap_send_snrm_cmd()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	n = 0;

	/* Allocate frame */
	skb = dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0,"irlap_send_snrm_cmd: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	skb_put( skb, 2); 
	frame = skb->data;

	/* Insert address field */
	frame[n] = CMD_FRAME;
	frame[n++] |= (qos) ? CBROADCAST : self->caddr;

	/* Insert control field */
 	frame[n++] = SNRM_CMD | PF_BIT;
	
	/*
	 *  If we are establishing a connection then insert QoS paramerters 
	 */
	if (qos) {
		skb_put( skb, 9); /* 21 left */
		memcpy( frame+n, &self->saddr, 4); n += 4;
		memcpy( frame+n, &self->daddr, 4); n += 4;

		frame[n++] = self->caddr;
				
		/* skb_put( skb, 21); */
		len = irda_insert_qos_negotiation_params( qos, frame+n);
		/* Should not be dangerous to do this afterwards */
		skb_put( skb, len);
	}
	skb->dev = self->netdev;
	skb->h.raw = skb->data;
	irlap_insert_mtt( self, skb);

	dev_queue_xmit( skb);
	self->stats.tx_packets++;
}

/*
 * Function irlap_recv_snrm_cmd (skb, info)
 *
 *    Received SNRM (Set Normal Response Mode) command frame
 *
 */
static void irlap_recv_snrm_cmd( struct irlap_cb *self, struct sk_buff *skb, 
				 struct irlap_info *info) 
{
	struct snrm_frame *frame;

	DEBUG( 4, __FUNCTION__ "() <%ld>\n", jiffies);

	ASSERT( skb != NULL, return;);
	ASSERT( info != NULL, return;);

	frame = ( struct snrm_frame *) skb->data;

	/* Copy peer device address */
	memcpy( &info->daddr, &frame->saddr, 4);

	/* Copy connection address */
	info->caddr = frame->ncaddr;

	irlap_do_event( self, RECV_SNRM_CMD, skb, info);
}

/*
 * Function irlap_send_ua_response_frame (qos)
 *
 *    Send UA (Unnumbered Acknowledgement) frame
 *
 */
void irlap_send_ua_response_frame( struct irlap_cb *self, 
				   struct qos_info *qos)
{
	struct sk_buff *skb;
	__u8 *frame;
	int n;
	int len;
	
	DEBUG( 4, __FUNCTION__ "() <%ld>\n", jiffies);
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	skb = NULL;
	n = 0;

	/* Allocate frame */
	skb =  dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0, __FUNCTION__
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	skb_put( skb, 10);
	frame = skb->data;
	
	/* Build UA response */
	frame[n++] = self->caddr;
 	frame[n++] = UA_RSP | PF_BIT;
	memcpy( frame+n, &self->saddr, 4); n += 4;
	memcpy( frame+n, &self->daddr, 4); n += 4;
	
	/* Should we send QoS negotiation parameters? */
	if ( qos) {
		len = irda_insert_qos_negotiation_params( qos, frame+n);
		skb_put( skb, len);
	}

	skb->dev = self->netdev;
	skb->h.raw = skb->data;
	irlap_insert_mtt( self, skb);

	dev_queue_xmit( skb);
	self->stats.tx_packets++;
}


/*
 * Function irlap_send_dm_frame (void)
 *
 *    Send disconnected mode (DM) frame
 *
 */
void irlap_send_dm_frame( struct irlap_cb *self)
{
	struct sk_buff *skb = NULL;
	__u8 *frame;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	skb =  dev_alloc_skb( 32);
	if (skb == NULL) {
		DEBUG( 0,"irlap_send_disc_frame: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	skb_put( skb, 2);
	frame = skb->data;
	
	if ( self->state == LAP_NDM)
		frame[0] = CBROADCAST;
	else
		frame[0] = self->caddr;

	frame[1] = DM_RSP | PF_BIT;
	
	skb->dev = self->netdev;
	skb->h.raw = skb->data;
	irlap_insert_mtt( self, skb);

	dev_queue_xmit( skb);
	self->stats.tx_packets++;
}

/*
 * Function irlap_send_disc_frame (void)
 *
 *    Send disconnect (DISC) frame
 *
 */
void irlap_send_disc_frame( struct irlap_cb *self) 
{
	struct sk_buff *skb = NULL;
	__u8 *frame;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	skb =  dev_alloc_skb( 32);
	if (skb == NULL) {
		DEBUG( 0,"irlap_send_disc_frame: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	skb_put( skb, 2);
	frame = skb->data;
	
	frame[0] = self->caddr | CMD_FRAME;
	frame[1] = DISC_CMD | PF_BIT;
	
	skb->dev = self->netdev;
	skb->h.raw = skb->data;
	irlap_insert_mtt( self, skb);

	dev_queue_xmit( skb);
	self->stats.tx_packets++;
}

/*
 * Function irlap_send_discovery_xid_frame (S, s, command)
 *
 *    Build and transmit a XID (eXchange station IDentifier) discovery
 *    frame. 
 */
void irlap_send_discovery_xid_frame( struct irlap_cb *self, int S, __u8 s, 
				     __u8 command, DISCOVERY *discovery) 
{
	struct sk_buff *skb = NULL;
	struct xid_frame *frame;
	__u32 bcast = BROADCAST;

 	DEBUG( 4, __FUNCTION__ "(), s=%d, S=%d, command=%d\n", s, S, command);

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( discovery != NULL, return;);

	skb =  dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0,"irlap_send_discovery_xid_frame: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	skb_put( skb, 14);
	frame = ( struct xid_frame *) skb->data;

	if ( command) {
		frame->caddr = CBROADCAST | CMD_FRAME;
		frame->control =  XID_CMD | PF_BIT;
	} else {
		frame->caddr = CBROADCAST;
		frame->control =  XID_RSP | PF_BIT;
	}
	frame->ident = XID_FORMAT;

	memcpy( &frame->saddr, &self->saddr, 4);

	if ( command)
		memcpy( &frame->daddr, &bcast, 4);
	else
		memcpy( &frame->daddr, &self->daddr, 4);	
	
	switch( S) {
	case 1:
		frame->flags = 0x00;
		break;
	case 6:
		frame->flags = 0x01;
		break;
	case 8:
		frame->flags = 0x02;
		break;
	case 16:
		frame->flags = 0x03;
		break;
	default:
		frame->flags = 0x02;
		break;
	}

	frame->slotnr = s; 
	frame->version = 0x00;

	/*  
	 *  Provide info for final slot only in commands, and for all
	 *  responses. Send the second byte of the hint only if the
	 *  EXTENSION bit is set in the first byte.
	 */
	if ( !command || ( frame->slotnr == 0xff)) {
		int i;

		if( discovery->hint[0] & HINT_EXTENSION)
			skb_put( skb, 3+discovery->info_len);
		else
			skb_put( skb, 2+discovery->info_len);
		
		i = 0;
		frame->discovery_info[i++] = discovery->hint[0];
		if( discovery->hint[0] & HINT_EXTENSION)
			frame->discovery_info[i++] = discovery->hint[1];
		
		frame->discovery_info[i++] = discovery->charset;

		ASSERT( discovery->info_len < 30, return;);

		memcpy( &frame->discovery_info[i++], discovery->info, 
			discovery->info_len);

	} 

	ASSERT( self->netdev != NULL, return;);

	skb->dev = self->netdev;
	skb->h.raw = skb->data;
	irlap_insert_mtt( self, skb);

	dev_queue_xmit( skb);
	self->stats.tx_packets++;
}

/*
 * Function irlap_recv_discovery_xid_rsp (skb, info)
 *
 *    Received a XID discovery response
 *
 */
static void irlap_recv_discovery_xid_rsp( struct irlap_cb *self, 
					  struct sk_buff *skb, 
					  struct irlap_info *info) 
{
	struct xid_frame *xid;
	DISCOVERY *discovery = NULL;
	char *text;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	ASSERT( info != NULL, return;);

	if (( discovery = kmalloc( sizeof( DISCOVERY), GFP_ATOMIC)) == NULL) {
		DEBUG( 0, __FUNCTION__ "(), kmalloc failed!\n");
		return;
	}
	memset( discovery, 0, sizeof( DISCOVERY));

	xid = (struct xid_frame *) skb->data;

	/* 
	 *  Copy peer device address and set the source address
	 */
	memcpy( &info->daddr, &xid->saddr, 4);
	discovery->daddr = info->daddr;
	discovery->saddr = self->saddr;

	DEBUG( 4, __FUNCTION__ "(), daddr=%08x\n", discovery->daddr);

	/* Get info returned from peer */
	discovery->hint[0] = xid->discovery_info[0];
	if ( xid->discovery_info[0] & HINT_EXTENSION) {
		DEBUG( 4, "EXTENSION\n");
		discovery->hint[1] = xid->discovery_info[1];
		discovery->charset = xid->discovery_info[2];
		text = (char *) &xid->discovery_info[3];
	} else {
		discovery->hint[1] = 0;
		discovery->charset = xid->discovery_info[1];
		text = (char *) &xid->discovery_info[2];
	}
	/* 
	 *  Terminate string, should be safe since this is where the 
	 *  FCS bytes resides.
	 */
	skb->data[skb->len] = '\0'; 
	strcpy( discovery->info, text);

	info->discovery = discovery;

	irlap_do_event( self, RECV_DISCOVERY_XID_RSP, skb, info);
}

/*
 * Function irlap_recv_discovery_xid_cmd (skb, info)
 *
 *    Received a XID discovery command
 *
 */
static void irlap_recv_discovery_xid_cmd( struct irlap_cb *self, 
					  struct sk_buff *skb, 
					  struct irlap_info *info) 
{
	struct xid_frame *xid;
	DISCOVERY *discovery = NULL;
	char *text;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	ASSERT( info != NULL, return;);

	xid = (struct xid_frame *) skb->data;
	
	/* Copy peer device address */
	memcpy( &info->daddr, &xid->saddr, 4);

	switch ( xid->flags & 0x03) {
	case 0x00:
		info->S = 1;
		break;
	case 0x01:
		info->S = 6;
		break;
	case 0x02:
		info->S = 8;
		break;
	case 0x03:
		info->S = 16;
		break;
	default:
		/* Error!! */
		return;
	}
	info->s = xid->slotnr;
	
	/* 
	 *  Check if last frame 
	 */
	if ( info->s == 0xff) {
		/*
		 *  We now have some discovery info to deliver!
		 */
		discovery = kmalloc( sizeof( DISCOVERY), GFP_ATOMIC);
		if ( !discovery) {
			DEBUG( 0, __FUNCTION__ "(), kmalloc failed!\n");
			return;
		}
		discovery->daddr = info->daddr;
		discovery->saddr = self->saddr;

		DEBUG( 4, __FUNCTION__ "(), daddr=%08x\n", 
		       discovery->daddr);

		discovery->hint[0] = xid->discovery_info[0];
		if ( xid->discovery_info[0] & HINT_EXTENSION) {
			DEBUG( 4, "EXTENSION\n");
			discovery->hint[1] = xid->discovery_info[1];
			discovery->charset = xid->discovery_info[2];
			text = (char *) &xid->discovery_info[3];
		} else {
			discovery->hint[1] = 0;
			discovery->charset = xid->discovery_info[1];
			text = (char *) &xid->discovery_info[2];
		}
		/* 
		 *  Terminate string, should be safe since this is where the 
		 *  FCS bytes resides.
		 */
		skb->data[skb->len] = '\0'; 
		strcpy( discovery->info, text);

		info->discovery = discovery;
	} else
		info->discovery = NULL;
	
	DEBUG( 4, __FUNCTION__"(), s=%d, S=%d <%ld>\n", 
	       info->s, info->S, jiffies); 

	irlap_do_event( self, RECV_DISCOVERY_XID_CMD, skb, info);
}

/*
 * Function irlap_send_rr_frame (self, command)
 *
 *    Build and transmit RR (Receive Ready) frame. Notice that it is currently
 *    only possible to send RR frames with the poll bit set.
 */
void irlap_send_rr_frame( struct irlap_cb *self, int command) 
{
	struct sk_buff *skb = NULL;
	__u8 *frame;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

#ifdef CONFIG_IRDA_RECYCLE_RR
	if ( self->recycle_rr_skb) {
		DEBUG( 4, __FUNCTION__ "(), recycling skb!\n");
		skb = self->recycle_rr_skb;
		self->recycle_rr_skb = NULL;
	}
#endif      
	if ( !skb) {
		skb = dev_alloc_skb( 32);
		if (skb == NULL) {
			printk( KERN_WARNING "irlap_send_rr_frame: "
				"Could not allocate an skb of length %d\n", 32);
			return;
		}
		skb_put( skb, 2);
	}
	ASSERT( skb->len == 2, return;);

	frame = skb->data;
	
	frame[0] = self->caddr;
	frame[0] |= (command) ? CMD_FRAME : 0;

	frame[1] = RR | PF_BIT | (self->vr << 5);

   	DEBUG( 4, __FUNCTION__ "(), vr=%d, %ld\n", self->vr, jiffies); 

	skb->dev = self->netdev;
	skb->h.raw = skb->data;
	irlap_insert_mtt( self, skb);

	dev_queue_xmit( skb);
	self->stats.tx_packets++;
}

/*
 * Function irlap_recv_rr_frame (skb, info)
 *
 *    Received RR (Receive Ready) frame from peer station
 *
 */
static void irlap_recv_rr_frame( struct irlap_cb *self, struct sk_buff *skb, 
				 struct irlap_info *info, int command)
{
	__u8 *frame;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	frame = skb->data;
	info->nr = frame[1] >> 5;

	DEBUG( 4, __FUNCTION__ "(), nr=%d, %ld\n", info->nr, jiffies);

	/*
	 *  Make sure the state-machine is in the right state for receiving, 
	 *  if not, then we just discard the received frame for now!
	 *  TODO: check if we should queue this frame, or make tty tell that
	 *  it is receiving frames until the frame is delivered instead of
	 *  until it is outside a frame.
	 */
#if 0
	if (( self->state != LAP_NRM_P) && ( self->state != LAP_NRM_S)) {
		DEBUG( 0, __FUNCTION__ "(), Wrong state, dropping frame!\n");
		dev_kfree_skb( skb);
		return;
	}
#endif

#ifdef CONFIG_IRDA_RECYCLE_RR
	/* Only recycle one RR frame */
	if ( self->recycle_rr_skb == NULL) {

		/* Keep this skb, so it can be reused */
		self->recycle_rr_skb = skb;

		/*  
		 *  Set skb to NULL, so that the state machine will not 
		 *  try to deallocate it.
		 */
		skb = NULL;  
	}
#endif
	/* Check if this is a command or a response frame */
	if ( command)
		irlap_do_event( self, RECV_RR_CMD, skb, info);
	else
		irlap_do_event( self, RECV_RR_RSP, skb, info);
}

void irlap_send_frmr_frame( struct irlap_cb *self, int command)
{
	struct sk_buff *skb = NULL;
	__u8 *frame;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	skb = dev_alloc_skb( 32);
	if (skb == NULL) {
		printk( KERN_WARNING "irlap_send_frmr_frame: "
			"Could not allocate an sk_buff of length %d\n", 32);
		return;
	}

	skb_put( skb, 2);
	frame = skb->data;
	
	frame[0] = self->caddr;
	frame[0] |= (command) ? CMD_FRAME : 0;

	frame[1]  = (self->vs << 1);
	frame[1] |= PF_BIT;
	frame[1] |= (self->vr << 5);

	frame[2] = 0;

   	DEBUG( 4, __FUNCTION__ "(), vr=%d, %ld\n",self->vr, jiffies); 

	skb->dev = self->netdev;
	skb->h.raw = skb->data;
	irlap_insert_mtt( self, skb);

	dev_queue_xmit( skb);
	self->stats.tx_packets++;
}

/*
 * Function irlap_recv_rnr_frame (self, skb, info)
 *
 *    Received RNR (Receive Not Ready) frame from peer station
 *
 */
static void irlap_recv_rnr_frame( struct irlap_cb *self, struct sk_buff *skb, 
				  struct irlap_info *info) 
{
	__u8 *frame;

	ASSERT( skb != NULL, return;);
	ASSERT( info != NULL, return;);

	frame = skb->data;
	info->nr = frame[1] >> 5;

	DEBUG( 4, __FUNCTION__ "(), nr=%d, %ld\n", info->nr, jiffies);

	irlap_do_event( self, RECV_RNR_FRAME, skb, info);
}

/*
 * Function irlap_recv_ua_frame (skb, frame)
 *
 *    Received UA (Unnumbered Acknowledgement) frame
 *
 */
static void irlap_recv_ua_frame( struct irlap_cb *self, struct sk_buff *skb, 
				 struct irlap_info *info) 
{
 	DEBUG( 4, __FUNCTION__ "()\n"); 

	ASSERT( skb != NULL, return;);
	ASSERT( info != NULL, return;);

	irlap_do_event( self, RECV_UA_RSP, skb, info);
}

/*
 * Function irlap_send_data_primary(self, skb)
 *
 *    
 *
 */
void irlap_send_data_primary( struct irlap_cb *self, struct sk_buff *skb)
{
	struct sk_buff *tx_skb;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* Initialize variables */
	tx_skb = NULL;

	if ( skb->data[1] == I_FRAME) {

		/*  
		 *  Insert frame sequence number (Vs) in control field before
		 *  inserting into transmit window queue.
		 */
		skb->data[1] = I_FRAME | (self->vs << 1);
		
		/* * Copy buffer */
		tx_skb = skb_clone( skb, GFP_ATOMIC);
		/* tx_skb = skb_copy( skb, GFP_ATOMIC);  */
		if ( tx_skb == NULL) {
			dev_kfree_skb( skb);
			return;
		}
		
		/*
		 *  make sure the skb->sk accounting of memory usage is sane
		 */
		if (skb->sk != NULL)
			skb_set_owner_w( tx_skb, skb->sk);
		
		/* 
		 *  Insert frame in store, in case of retransmissions 
		 */
		skb_queue_tail( &self->wx_list, skb);
		
		self->vs = (self->vs + 1) % 8;
		self->ack_required = FALSE;		
		self->window -= 1;

		irlap_send_i_frame( self, tx_skb, CMD_FRAME);
	} else {
		DEBUG( 4, __FUNCTION__ "(), sending unreliable frame\n");
		irlap_send_ui_frame( self, skb, CMD_FRAME);
		self->window -= 1;
	}
}
/*
 * Function irlap_send_data_primary_poll ( self, skb)
 *
 *    Send I(nformation) frame as primary with poll bit set
 */
void irlap_send_data_primary_poll( struct irlap_cb *self, struct sk_buff *skb) 
{
	struct sk_buff *tx_skb;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* Initialize variables */
	tx_skb = NULL;

	/* Is this reliable or unreliable data? */
	if ( skb->data[1] == I_FRAME) {
		
		/*  
		 *  Insert frame sequence number (Vs) in control field before
		 *  inserting into transmit window queue.
		 */
		skb->data[1] = I_FRAME | (self->vs << 1);
		
		/* Copy buffer */
		tx_skb = skb_clone( skb, GFP_ATOMIC);
		/* tx_skb = skb_copy( skb, GFP_ATOMIC);  */
		if ( tx_skb == NULL) {
			dev_kfree_skb( skb);
			return;
		}
		
		/*
		 *  make sure the skb->sk accounting of memory usage is sane
		 */
		if (skb->sk != NULL)
			skb_set_owner_w( tx_skb, skb->sk);
		
		/* 
		 *  Insert frame in store, in case of retransmissions 
		 */
		skb_queue_tail( &self->wx_list, skb);
		
		/*  
		 *  Set poll bit if necessary. We do this to the copied
		 *  skb, since retransmitted need to set or clear the poll
		 *  bit depending on when * they are sent.  
		 */
		/* Stop P timer */
		del_timer( &self->poll_timer);
		
		tx_skb->data[1] |= PF_BIT;
		
		self->vs = (self->vs + 1) % 8;
		self->ack_required = FALSE;
		self->window = self->window_size;

		irlap_start_final_timer( self, self->final_timeout);

		irlap_send_i_frame( self, tx_skb, CMD_FRAME);
	} else {
		DEBUG( 4, __FUNCTION__ "(), sending unreliable frame\n");

		del_timer( &self->poll_timer);

		if ( self->ack_required) {
			irlap_send_ui_frame( self, skb, CMD_FRAME);
			irlap_send_rr_frame( self, CMD_FRAME);
			self->ack_required = FALSE;
		} else {
			skb->data[1] |= PF_BIT;
			irlap_send_ui_frame( self, skb, CMD_FRAME);
		}
		self->window = self->window_size;
		irlap_start_final_timer( self, self->final_timeout);
	}
}

/*
 * Function irlap_send_data_secondary_final (self, skb)
 *
 *    Send I(nformation) frame as secondary with final bit set
 *
 */
void irlap_send_data_secondary_final( struct irlap_cb *self, 
				      struct sk_buff *skb) 
{
	struct sk_buff *tx_skb = NULL;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* Is this reliable or unreliable data? */
	if ( skb->data[1] == I_FRAME) {

		/*  
		 *  Insert frame sequence number (Vs) in control field before
		 *  inserting into transmit window queue.
		 */
		skb->data[1] = I_FRAME | (self->vs << 1);
		
		tx_skb = skb_clone( skb, GFP_ATOMIC);
		/* tx_skb = skb_copy( skb, GFP_ATOMIC); */
		if ( tx_skb == NULL) {
			dev_kfree_skb( skb);
			return;
		}		

		if (skb->sk != NULL)
			skb_set_owner_w( tx_skb, skb->sk);
		
		/* Insert frame in store */
		skb_queue_tail( &self->wx_list, skb);
		
		tx_skb->data[1] |= PF_BIT;
		
		self->vs = (self->vs + 1) % 8; 
		self->window = self->window_size;
		self->ack_required = FALSE;
		
		irlap_start_wd_timer( self, self->wd_timeout);

		irlap_send_i_frame( self, tx_skb, RSP_FRAME); 
	} else {
		if ( self->ack_required) {
			irlap_send_ui_frame( self, skb, RSP_FRAME);
			irlap_send_rr_frame( self, RSP_FRAME);
			self->ack_required = FALSE;
		} else {
			skb->data[1] |= PF_BIT;
			irlap_send_ui_frame( self, skb, RSP_FRAME);
		}
		self->window = self->window_size;

		irlap_start_wd_timer( self, self->wd_timeout);
	}
}

/*
 * Function irlap_send_data_secondary (self, skb)
 *
 *    Send I(nformation) frame as secondary without final bit set
 *
 */
void irlap_send_data_secondary( struct irlap_cb *self, struct sk_buff *skb) 
{
	struct sk_buff *tx_skb = NULL;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* Is this reliable or unreliable data? */
	if ( skb->data[1] == I_FRAME) {
		
		/*  
		 *  Insert frame sequence number (Vs) in control field before
		 *  inserting into transmit window queue.
		 */
		skb->data[1] = I_FRAME | (self->vs << 1);
		
		tx_skb = skb_clone( skb, GFP_ATOMIC);
		/* tx_skb = skb_copy( skb, GFP_ATOMIC); */
		if ( tx_skb == NULL) {
			dev_kfree_skb( skb);
			return;
		}		
		
		if (skb->sk != NULL)
			skb_set_owner_w( tx_skb, skb->sk);
		
		/* Insert frame in store */
		skb_queue_tail( &self->wx_list, skb);
		
		self->vs = (self->vs + 1) % 8;
		self->ack_required = FALSE;		
		self->window -= 1;

		irlap_send_i_frame( self, tx_skb, RSP_FRAME); 
	} else {
		irlap_send_ui_frame( self, skb, RSP_FRAME);
		self->window -= 1;
	}
}

/*
 * Function irlap_resend_rejected_frames (nr)
 *
 *    Resend frames which has not been acknowledged. TODO: check that the 
 *    traversal of the list is atomic, i.e that no-one tries to insert or
 *    remove frames from the list while we travers it!
 * 
 *    FIXME: It is not safe to traverse a this list without locking it!
 */
void irlap_resend_rejected_frames( struct irlap_cb *self, int command) 
{
	struct sk_buff *tx_skb;
	struct sk_buff *skb;
	int count;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	DEBUG( 4, __FUNCTION__ "(), retry_count=%d\n",
	       self->retry_count);

	/* Initialize variables */
	skb = tx_skb = NULL;

	/* 
	 *  Resend all unacknowledged frames 
	 */
	count = skb_queue_len( &self->wx_list);
	skb = skb_peek( &self->wx_list);
	while ( skb != NULL) {
		irlap_wait_min_turn_around( self, &self->qos_tx);

		tx_skb = skb_clone( skb, GFP_ATOMIC);
		/* tx_skb = skb_copy( skb, GFP_ATOMIC); */
		if ( tx_skb == NULL) {
			/* Unlink tx_skb from list */
			tx_skb->next = tx_skb->prev = NULL;
			tx_skb->list = NULL;
		
			dev_kfree_skb( skb);
			return;	
		}
		/* Unlink tx_skb from list */
		tx_skb->next = tx_skb->prev = NULL;
		tx_skb->list = NULL;

		/*
		 *  make sure the skb->sk accounting of memory usage is sane
		 */
		if ( skb->sk != NULL)
			skb_set_owner_w( tx_skb, skb->sk);

		/* Clear old Nr field + poll bit */
		tx_skb->data[1] &= 0x0f;

		/* 
		 *  Set poll bit on the last frame retransmitted
		 */
	 	if ( count-- == 1) 
	 		tx_skb->data[1] |= PF_BIT; /* Set p/f bit */
		else
			tx_skb->data[1] &= ~PF_BIT; /* Clear p/f bit */
	      	
		irlap_send_i_frame( self, tx_skb, command);

		/* 
		 *  If our skb is the last buffer in the list, then
		 *  we are finished, if not, move to the next sk-buffer
		 */
		if ( skb == skb_peek_tail( &self->wx_list))
			skb = NULL;
		else
			skb = skb->next;
	}
	/* 
	 *  We can now fill the window with additinal data frames
	 */
	return; /* Skip this for now, DB */

	while ( skb_queue_len( &self->tx_list) > 0) {
		
		DEBUG( 0, __FUNCTION__ "(), sending additional frames!\n");
		if (( skb_queue_len( &self->tx_list) > 0) && 
		    ( self->window > 0)) {
			skb = skb_dequeue( &self->tx_list); 
			ASSERT( skb != NULL, return;);

			/*
			 *  If send window > 1 then send frame with pf 
			 *  bit cleared
			 */ 
			if (( self->window > 1) && 
			    skb_queue_len( &self->tx_list) > 0) 
			{
				irlap_send_data_primary( self, skb);
			} else {
				irlap_send_data_primary_poll( self, skb);
			}
		}
	}
}

/*
 * Function irlap_send_ui_frame (self, skb, command)
 *
 *    Contruct and transmit an Unnumbered Information (UI) frame
 *
 */
void irlap_send_ui_frame( struct irlap_cb *self, struct sk_buff *skb, 
			  int command) 
{
	__u8  *frame;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	
	frame = skb->data;
	
	/* Insert connection address */
	frame[0] = self->caddr;
	frame[0] |= (command) ? CMD_FRAME : 0;

	skb->dev = self->netdev;
	skb->h.raw = skb->data;
	irlap_insert_mtt( self, skb);

	dev_queue_xmit( skb);
	self->stats.tx_packets++;
}

/*
 * Function irlap_send_i_frame (skb)
 *
 *    Contruct and transmit Information (I) frame
 */
void irlap_send_i_frame( struct irlap_cb *self, struct sk_buff *skb, 
			 int command) 
{
	__u8  *frame;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	frame = skb->data;
	
	/* Insert connection address */
	frame[0] = self->caddr;
	frame[0] |= (command) ? CMD_FRAME : 0;
	
	/* Insert next to receive (Vr) */
	frame[1] |= (self->vr << 5);  /* insert nr */
#if 0
	{
		int vr, vs, pf;
		
		/* Chech contents of various fields */
		vr = frame[1] >> 5;
		vs = (frame[1] >> 1) & 0x07;
		pf = (frame[1] >> 4) & 0x01;
		
		DEBUG( 0, __FUNCTION__ "(), vs=%d, vr=%d, p=%d, %ld\n", 
		       vs, vr, pf, jiffies);
	}
#endif	
	skb->dev = self->netdev;
	skb->h.raw = skb->data;
	irlap_insert_mtt( self, skb);

	dev_queue_xmit( skb);
	self->stats.tx_packets++;
}

/*
 * Function irlap_recv_i_frame (skb, frame)
 *
 *    Receive and parse an I (Information) frame
 * 
 */
static void irlap_recv_i_frame( struct irlap_cb *self, struct sk_buff *skb, 
				struct irlap_info *info, int command) 
{
	__u8 *frame;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	frame = skb->data;

	info->nr = frame[1] >> 5;          /* Next to receive */
	info->pf = frame[1] & PF_BIT;      /* Final bit */
	info->ns = (frame[1] >> 1) & 0x07; /* Next to send */

 	DEBUG( 4, __FUNCTION__"(), ns=%d, nr=%d, pf=%d, %ld\n", 
	       info->ns, info->nr, info->pf>>4, jiffies); 

	/*
	 *  Make sure the state-machine is in the right state for receiving, 
	 *  if not, then we just discard the received frame for now!
	 *  TODO: check if we should queue this frame, or make tty tell that
	 *  it is receiving frames until the frame is delivered instead of
	 *  until it is outside a frame.
	 */
	if (( self->state != LAP_NRM_P) && ( self->state != LAP_NRM_S)) {
		DEBUG( 0, __FUNCTION__ "(), Wrong state, dropping frame!\n");
		dev_kfree_skb( skb);
		return;
	}

	/* Check if this is a command or a response frame */
	if ( command)
		irlap_do_event( self, RECV_I_CMD, skb, info);
	else
		irlap_do_event( self, RECV_I_RSP, skb, info);
}

/*
 * Function irlap_recv_ui_frame (self, skb, info)
 *
 *    Receive and parse an Unnumbered Information (UI) frame
 *
 */
static void irlap_recv_ui_frame( struct irlap_cb *self, struct sk_buff *skb, 
				 struct irlap_info *info)
{
	__u8 *frame;

	DEBUG( 4, __FUNCTION__ "()\n");

	frame = skb->data;

	info->pf = frame[1] & PF_BIT;      /* Final bit */

	irlap_do_event( self, RECV_UI_FRAME, skb, info);
}

/*
 * Function irlap_recv_frmr_frame (skb, frame)
 *
 *    Received Frame Reject response.
 *
 */
static void irlap_recv_frmr_frame( struct irlap_cb *self, struct sk_buff *skb, 
				   struct irlap_info *info) 
{
	__u8 *frame;
	int w, x, y, z;

	DEBUG( 0, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	ASSERT( info != NULL, return;);
	
	frame = skb->data;

	info->nr = frame[2] >> 5;          /* Next to receive */
	info->pf = frame[2] & PF_BIT;        /* Final bit */
	info->ns = (frame[2] >> 1) & 0x07; /* Next to send */

	w = frame[3] & 0x01;
	x = frame[3] & 0x02;
	y = frame[3] & 0x04;
	z = frame[3] & 0x08;
	
	if ( w) {
		DEBUG( 0, "Rejected control field is undefined or not "
		       "implemented.\n");
	} 
	if ( x) {
		DEBUG( 0, "Rejected control field was invalid because it "
		       "contained a non permitted I field.\n");
	}
	if ( y) {
		DEBUG( 0, "Received I field exceeded the maximum negotiated "
		       "for the existing connection or exceeded the maximum "
		       "this station supports if no connection exists.\n");
	}
	if ( z) {
		DEBUG( 0, "Rejected control field control field contained an "
		       "invalid Nr count.\n");
	}
	irlap_do_event( self, RECV_FRMR_RSP, skb, info);
}

/*
 * Function irlap_input (skb)
 *
 *  Called when a frame is received. Dispatches the right receive function 
 *  for processing of the frame.
 */
int irlap_input( struct sk_buff *skb, struct device *netdev, 
		 struct packet_type *ptype)
{
	struct irlap_info info;
	struct irlap_cb *self;
	struct irda_device *idev;
	__u8 *frame;
	int i, command;
	__u8 control;
	
	idev = ( struct irda_device *) netdev->priv;

	ASSERT( idev != NULL, return -1;);
	self = idev->irlap;

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LAP_MAGIC, return -1;);
	ASSERT(( skb != NULL) && (skb->len > 1), return -1;);
	
	frame = skb->data;

	command    = frame[0] & CMD_FRAME;
	info.caddr = frame[0] & CBROADCAST;
	
	info.pf      = frame[1] &  PF_BIT;
	info.control = frame[1] & ~PF_BIT; /* Mask away poll/final bit */

	control = info.control;

	/* 
	 *  First check if this frame addressed to us 
	 */
	if (( info.caddr != self->caddr) && ( info.caddr != CBROADCAST)) {

		DEBUG( 0, __FUNCTION__ "(), Received frame is not for us!\n");
		for(i=0; i<(skb->len < 15?skb->len:15);i++) {
 			printk( "%02x ", frame[i]);
		}
 		printk("\n"); 
		
		dev_kfree_skb( skb);

		return 0;
	}
	/*  
	 *  Optimize for the common case and check if the frame is an
	 *  I(nformation) frame. Only I-frames have bit 0 set to 0
	 */
	if( ~control & 0x01) {
		irlap_recv_i_frame( self, skb, &info, command);
		self->stats.rx_packets++;
		return 0;
	}
	/*
	 *  We now check is the frame is an S(upervisory) frame. Only 
	 *  S-frames have bit 0 set to 1 and bit 1 set to 0
	 */
	if ( ~control & 0x02) {
		/* 
		 *  Received S(upervisory) frame, check which frame type it is
		 *  only the first nibble is of interest
		 */
		switch( control & 0x0f) {
		case RR:
			irlap_recv_rr_frame( self, skb, &info, command);
			self->stats.rx_packets++;
			break;
		case RNR:
			DEBUG( 4, "*** RNR frame received! pf = %d ***\n", 
			       info.pf >> 4);
			irlap_recv_rnr_frame( self, skb, &info);
			self->stats.rx_packets++;
			break;
		case REJ:
			DEBUG( 0, "*** REJ frame received! ***\n");
			break;
		case SREJ:
			DEBUG( 0, "*** SREJ frame received! ***\n");
			break;
		default:
			DEBUG( 0, "Unknown S frame %02x received!\n", 
			       info.control);
			break;
		}
		return 0;
	}
	/* 
	 *  This must be a C(ontrol) frame 
	 */
	switch( control) {
	case XID_RSP:
		DEBUG( 4, "XID rsp frame received!\n");
		irlap_recv_discovery_xid_rsp( self, skb, &info);
		break;
	case XID_CMD:
		DEBUG( 4, "XID cmd frame received!\n");
		irlap_recv_discovery_xid_cmd( self, skb, &info);
		break;
	case SNRM_CMD:
		DEBUG( 4, "SNRM frame received!\n"); 
		irlap_recv_snrm_cmd( self, skb, &info);
		break;
	case DM_RSP:
		DEBUG( 0, "DM rsp frame received!\n");
		irlap_next_state( self, LAP_NDM);
		break;
	case DISC_CMD:
		DEBUG( 0, "DISC cmd frame received!\n");
		irlap_do_event( self, RECV_DISC_FRAME, skb, &info);
		break;
	case TEST_CMD:
		DEBUG( 0, "Test frame received!\n");
		dev_kfree_skb( skb);
		break;
	case UA_RSP:
		DEBUG( 4, "UA rsp frame received!\n");
		irlap_recv_ua_frame( self, skb, &info);
		break;
	case FRMR_RSP:
		DEBUG( 4, "FRMR_RSP recevied!\n");
		irlap_recv_frmr_frame( self, skb, &info);
		break;
	case UI_FRAME:
		DEBUG( 4, "UI-frame received!\n");
		irlap_recv_ui_frame( self, skb, &info);
		break;
	default:
		DEBUG( 0, "Unknown frame %02x received!\n", info.control);
		dev_kfree_skb( skb); 
		break;
	}
	self->stats.rx_packets++;

	return 0;
}
