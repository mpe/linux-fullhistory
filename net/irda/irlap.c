/*********************************************************************
 *                
 * Filename:      irlap.c
 * Version:       0.8
 * Description:   An IrDA LAP driver for Linux
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Sat Jan 16 22:19:27 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
 *     
 *     This program is free software; you can redistribute iyt and/or 
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
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>
#include <net/irda/irqueue.h>
#include <net/irda/irlmp.h>
#include <net/irda/irlmp_frame.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/irlap.h>
#include <net/irda/timer.h>
#include <net/irda/qos.h>
#include <net/irda/irlap_comp.h>

hashbin_t *irlap = NULL;

static void __irlap_close( struct irlap_cb *self);

#ifdef CONFIG_PROC_FS
int irlap_proc_read( char *buf, char **start, off_t offset, int len, 
		     int unused);

#endif /* CONFIG_PROC_FS */

__initfunc(int irlap_init( void))
{
	/* Allocate master array */
	irlap = hashbin_new( HB_LOCAL);
	if ( irlap == NULL) {
		printk( KERN_WARNING "IrLAP: Can't allocate irlap hashbin!\n");
		return -ENOMEM;
	}

#ifdef CONFIG_IRDA_COMPRESSION
	irlap_compressors = hashbin_new( HB_LOCAL);
	if ( irlap_compressors == NULL) {
		printk( KERN_WARNING "IrLAP: Can't allocate compressors hashbin!\n");
		return -ENOMEM;
	}
#endif

	return 0;
}

void irlap_cleanup(void)
{
	ASSERT( irlap != NULL, return;);

	hashbin_delete( irlap, (FREE_FUNC) __irlap_close);

#ifdef CONFIG_IRDA_COMPRESSION
	hashbin_delete( irlap_compressors, (FREE_FUNC) kfree);
#endif
}

/*
 * Function irlap_open (driver)
 *
 *    Initialize IrLAP layer
 *
 */
struct irlap_cb *irlap_open( struct irda_device *irdev)
{
	struct irlap_cb *self;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( irdev != NULL, return NULL;);
	ASSERT( irdev->magic == IRDA_DEVICE_MAGIC, return NULL;);

	/* Initialize the irlap structure. */
	self = kmalloc( sizeof( struct irlap_cb), GFP_KERNEL);
	if ( self == NULL)
		return NULL;
	
	memset( self, 0, sizeof(struct irlap_cb));
	self->magic = LAP_MAGIC;

	/* Make a binding between the layers */
	self->irdev = irdev;
	self->netdev = &irdev->netdev;

	irlap_next_state( self, LAP_OFFLINE);

	/* Initialize transmitt queue */
	skb_queue_head_init( &self->tx_list);
	skb_queue_head_init( &self->wx_list);

	/* My unique IrLAP device address! :-) */
	self->saddr = jiffies;

	/*  Generate random connection address for this session */
	self->caddr = jiffies & 0xfe;

	init_timer( &self->slot_timer);
	init_timer( &self->query_timer);
	init_timer( &self->discovery_timer);
	init_timer( &self->final_timer);		
	init_timer( &self->poll_timer);
	init_timer( &self->wd_timer);
	init_timer( &self->backoff_timer);

	irlap_apply_default_connection_parameters( self);
	
	irlap_next_state( self, LAP_NDM);

	hashbin_insert( irlap, (QUEUE *) self, self->saddr, NULL);

	irlmp_register_irlap( self, self->saddr, &self->notify);
	
	DEBUG( 4, "irlap_open -->\n");

	return self;
}

/*
 * Function __irlap_close (self)
 *
 *    Remove IrLAP and all allocated memory. Stop any pending timers.
 *
 */
static void __irlap_close( struct irlap_cb *self)
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	/* Stop timers */
	del_timer( &self->slot_timer);
	del_timer( &self->query_timer);
	del_timer( &self->discovery_timer);
	del_timer( &self->final_timer);		
	del_timer( &self->poll_timer);
	del_timer( &self->wd_timer);
	del_timer( &self->backoff_timer);

	irlap_flush_all_queues( self);
       
	self->irdev = NULL;
	self->magic = ~LAP_MAGIC;
	
	kfree( self);
}

/*
 * Function irlap_close ()
 *
 *    
 *
 */
void irlap_close( struct irlap_cb *self) 
{
	struct irlap_cb *lap;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	irlap_disconnect_indication( self, LAP_DISC_INDICATION);

	irlmp_unregister_irlap( self->saddr);
	self->notify.instance = NULL;

	/* Be sure that we manage to remove ourself from the hash */
	lap = hashbin_remove( irlap, self->saddr, NULL);
	if ( !lap) {
		DEBUG( 0, __FUNCTION__ "(), Didn't find myself!\n");
		return;
	}
	__irlap_close( lap);
}

/*
 * Function irlap_connect_indication ()
 *
 *    Another device is attempting to make a connection
 *
 */
void irlap_connect_indication( struct irlap_cb *self, struct sk_buff *skb) 
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	irlap_init_qos_capabilities( self, NULL); /* No user QoS! */
	
	irlmp_link_connect_indication( self->notify.instance, &self->qos_tx, 
				       skb);
}

/*
 * Function irlap_connect_response (void)
 *
 *    Service user has accepted incomming connection
 *
 */
void irlap_connect_response( struct irlap_cb *self, struct sk_buff *skb) 
{
	DEBUG( 4, __FUNCTION__ "()\n");
	
	irlap_do_event( self, CONNECT_RESPONSE, skb, NULL);
}

/*
 * Function irlap_connect_request (daddr, qos, sniff)
 *
 *    Request connection with another device, sniffing is not implemented 
 *    yet.
 */
void irlap_connect_request( struct irlap_cb *self, __u32 daddr, 
			    struct qos_info *qos_user, int sniff) 
{
	DEBUG( 4, __FUNCTION__ "()\n"); 

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

 	self->daddr = daddr;

	/*
	 *  If the service user specifies QoS values for this connection, 
	 *  then use them
	 */
	irlap_init_qos_capabilities( self, qos_user);
	
	if ( self->state == LAP_NDM) {
		irlap_do_event( self, CONNECT_REQUEST, NULL, NULL);
	} else {
		DEBUG( 0, __FUNCTION__ "() Wrong state!\n");
		
		irlap_disconnect_indication( self, LAP_MEDIA_BUSY);
	}
	       
}

/*
 * Function irlap_connect_confirm (void)
 *
 *    Connection request is accepted
 *
 */
void irlap_connect_confirm( struct irlap_cb *self, struct sk_buff *skb)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	irlmp_link_connect_confirm( self->notify.instance, &self->qos_tx, skb);
}

/*
 * Function irlap_data_indication (skb)
 *
 *    Received data frames from IR-port, so we just pass them up to 
 *    IrLMP for further processing
 *
 */
inline void irlap_data_indication( struct irlap_cb *self, struct sk_buff *skb) 
{
	DEBUG( 4, __FUNCTION__ "()\n"); 

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* Hide LAP header from IrLMP layer */
	skb_pull( skb, LAP_ADDR_HEADER+LAP_CTRL_HEADER);

#ifdef CONFIG_IRDA_COMPRESSION
	if ( self->qos_tx.compression.value) {
		skb = irlap_decompress_frame( self, skb);
		if ( !skb) {
			DEBUG( 0, __FUNCTION__ "(), Decompress error!\n");
			return;
		}
	}
#endif

	irlmp_link_data_indication( self->notify.instance, LAP_RELIABLE, skb);
}

/*
 * Function irlap_unit_data_indication (self, skb)
 *
 *    Received some data that was sent unreliable
 *
 */
void irlap_unit_data_indication( struct irlap_cb *self, struct sk_buff *skb)
{
	DEBUG( 0, __FUNCTION__ "()\n"); 

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* Hide LAP header from IrLMP layer */
	skb_pull( skb, LAP_ADDR_HEADER+LAP_CTRL_HEADER);

#ifdef CONFIG_IRDA_COMPRESSION
	if ( self->qos_tx.compression.value) {
		
		skb = irlap_decompress_frame( self, skb);
		if ( !skb) {
			DEBUG( 0, __FUNCTION__ "(), Decompress error!\n");
			return;
		}
	}
#endif
	irlmp_link_data_indication(self->notify.instance, LAP_UNRELIABLE, skb);
}

/*
 * Function irlap_data_request (self, skb)
 *
 *    Queue data for transmission, must wait until XMIT state
 *
 */
inline void irlap_data_request( struct irlap_cb *self, struct sk_buff *skb,
				int reliable)
{
	DEBUG( 4, __FUNCTION__ "()\n");
       
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	DEBUG( 4, "irlap_data_request: tx_list=%d\n", 
		   skb_queue_len( &self->tx_list));

#ifdef CONFIG_IRDA_COMPRESSION
	if ( self->qos_tx.compression.value) {
		skb = irlap_compress_frame( self, skb);
		if ( !skb) {
			DEBUG( 0, __FUNCTION__ "(), Compress error!\n");
			return;
		}
	}
#endif
	
	ASSERT( skb_headroom( skb) >= (LAP_ADDR_HEADER+LAP_CTRL_HEADER), 
		return;);
	skb_push( skb, LAP_ADDR_HEADER+LAP_CTRL_HEADER);

	/*  
	 *  Must set frame format now so that the rest of the code knows 
	 *  if its dealing with an I or an UI frame
	 */
	if ( reliable)
		skb->data[1] = I_FRAME;
	else {
		DEBUG( 4, __FUNCTION__ "(), queueing unreliable frame\n");
		skb->data[1] = UI_FRAME;
	}

	/* 
	 *  Send event if this frame only if we are in the right state 
	 *  FIXME: udata should be sent first! (skb_queue_head?)
	 */
  	if (( self->state == LAP_XMIT_P) || (self->state == LAP_XMIT_S)) {
		/*
		 *  Check if the transmit queue contains some unsent frames,
		 *  and if so, make sure they are sent first
		 */
		if ( !skb_queue_empty( &self->tx_list)) {
			skb_queue_tail( &self->tx_list, skb);
			skb = skb_dequeue( &self->tx_list);
			
			ASSERT( skb != NULL, return;);
		}
		irlap_do_event( self, SEND_I_CMD, skb, NULL);
	} else
		skb_queue_tail( &self->tx_list, skb);
	
}

/*
 * Function irlap_disconnect_request (void)
 *
 *    Request to disconnect connection by service user
 */
void irlap_disconnect_request( struct irlap_cb *self) 
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	irlap_do_event( self, DISCONNECT_REQUEST, NULL, NULL);
}

/*
 * Function irlap_disconnect_indication (void)
 *
 *    Disconnect request from other device
 *
 */
void irlap_disconnect_indication( struct irlap_cb *self, LAP_REASON reason) 
{
	DEBUG( 4, __FUNCTION__ "()\n"); 

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

#ifdef CONFIG_IRDA_COMPRESSION
	irda_free_compression( self);
#endif

	/* Flush queues */
	irlap_flush_all_queues( self);
	
	switch( reason) {
	case LAP_RESET_INDICATION:
		DEBUG( 0, "Sending reset request!\n");
		irlap_do_event( self, RESET_REQUEST, NULL, NULL);
		break;
	case LAP_NO_RESPONSE:		
	case LAP_DISC_INDICATION:
	case LAP_FOUND_NONE:
	case LAP_MEDIA_BUSY:
		irlmp_link_disconnect_indication( self->notify.instance, 
						  self, reason, NULL);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Reason %d not implemented!\n", 
		       reason);
	}
}

/*
 * Function irlap_discovery_request (gen_addr_bit)
 *
 *    Start one single discovery operation.
 *
 */
void irlap_discovery_request( struct irlap_cb *self, DISCOVERY *discovery) 
{
	struct irlap_info info;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( discovery != NULL, return;);

  	/*
	 *  Discovery is only possible in NDM mode
	 */ 
	if ( self->state == LAP_NDM) {
		ASSERT( self->discovery_log == NULL, return;);
		self->discovery_log= hashbin_new( HB_LOCAL);

		info.S = 6; /* Number of slots */
		info.s = 0; /* Current slot */

		self->discovery_cmd = discovery;
		info.discovery = discovery;
		
		irlap_do_event( self, DISCOVERY_REQUEST, NULL, &info);
	} else { 
 		DEBUG( 4, __FUNCTION__ 
 			"(), discovery only possible in NDM mode\n");
		irlap_discovery_confirm( self, NULL);
 	} 
}

/*
 * Function irlap_discovery_confirm (log)
 *
 *    A device has been discovered in front of this station, we
 *    report directly to LMP.
 */
void irlap_discovery_confirm( struct irlap_cb *self, hashbin_t *discovery_log) 
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	
	ASSERT( self->notify.instance != NULL, return;);
	
	/* Inform IrLMP */
	irlmp_link_discovery_confirm( self->notify.instance, discovery_log);
	
	/* 
	 *  IrLMP has now the responsibilities for the discovery_log 
	 */
	self->discovery_log = NULL;
}

/*
 * Function irlap_discovery_indication (log)
 *
 *    Somebody is trying to discover us!
 *
 */
void irlap_discovery_indication( struct irlap_cb *self, DISCOVERY *discovery) 
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( discovery != NULL, return;);

	ASSERT( self->notify.instance != NULL, return;);
	
	irlmp_discovery_indication( self->notify.instance, discovery);
}

/*
 * Function irlap_status_indication (quality_of_link)
 *
 *    
 *
 */
void irlap_status_indication( int quality_of_link) 
{
	switch( quality_of_link) {
	case STATUS_NO_ACTIVITY:
		printk( KERN_INFO "IrLAP, no activity on link!\n");
		break;
	case STATUS_NOISY:
		printk( KERN_INFO "IrLAP, noisy link!\n");
		break;
	default:
		break;
	}
	/* TODO: layering violation! */
	irlmp_status_indication( quality_of_link, NO_CHANGE);
}

/*
 * Function irlap_reset_indication (void)
 *
 *    
 *
 */
void irlap_reset_indication( struct irlap_cb *self)
{
	DEBUG( 0, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	
	if ( self->state == LAP_RESET_WAIT)
		irlap_do_event( self, RESET_REQUEST, NULL, NULL);
	else
		irlap_do_event( self, RESET_RESPONSE, NULL, NULL);
}

/*
 * Function irlap_reset_confirm (void)
 *
 *    
 *
 */
void irlap_reset_confirm(void)
{
	DEBUG( 0, __FUNCTION__ "()\n");
}

/*
 * Function irlap_generate_rand_time_slot (S, s)
 *
 *    Generate a random time slot between s and S-1 where
 *    S = Number of slots (0 -> S-1)
 *    s = Current slot
 */
int irlap_generate_rand_time_slot( int S, int s) 
{
	int slot;
	
	ASSERT(( S - s) > 0, return 0;);

	slot = s + jiffies % (S-s);
	
	ASSERT(( slot >= s) || ( slot < S), return 0;);
	
	return slot;
}

/*
 * Function irlap_update_nr_received (nr)
 *
 *    Remove all acknowledged frames in current window queue. This code is 
 *    not intuitive and you should not try to change it. If you think it
 *    contains bugs, please mail a patch to the author instead.
 */
void irlap_update_nr_received( struct irlap_cb *self, int nr) 
{
	struct sk_buff *skb = NULL;
	int count = 0;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	/*
         * Remove all the ack-ed frames from the window queue.
         */

	DEBUG( 4, "--> wx_list=%d, va=%d, nr=%d\n", 
	       skb_queue_len( &self->wx_list), self->va, nr);

	/* 
	 *  Optimize for the common case. It is most likely that the receiver
	 *  will acknowledge all the frames we have sent! So in that case we
	 *  delete all frames stored in window.
	 */
	if ( nr == self->vs) {
		while (( skb = skb_dequeue( &self->wx_list)) != NULL) {
		     dev_kfree_skb(skb);
		}
		/* The last acked frame is the next to send minus one */
		self->va = nr - 1;
	} else {
		/* Remove all acknowledged frames in current window */
		while (( skb_peek( &self->wx_list) != NULL) && 
		       ((( self->va+1) % 8) != nr)) 
		{
			skb = skb_dequeue( &self->wx_list);
			dev_kfree_skb(skb);
			
			self->va = (self->va + 1) % 8;
			count++;
		}
		
		DEBUG( 4, "irlap_update_nr_received(), removed %d\n", count);
		DEBUG( 4, "wx_list=%d, va=%d, nr=%d -->\n", 
		       skb_queue_len( &self->wx_list), self->va, nr);
	}
	
	/* Advance window */
	self->window = self->window_size - skb_queue_len( &self->wx_list);
}

/*
 * Function irlap_validate_ns_received (ns)
 *
 *    Validate the next to send (ns) field from received frame.
 */
int irlap_validate_ns_received( struct irlap_cb *self, int ns) 
{
	ASSERT( self != NULL, return -ENODEV;);
	ASSERT( self->magic == LAP_MAGIC, return -EBADR;);

	/*  ns as expected?  */
	if ( ns == self->vr) {
		DEBUG( 4, "*** irlap_validate_ns_received: expected!\n");
		return NS_EXPECTED;
	}
	/*
	 *  Stations are allowed to treat invalid NS as unexpected NS
	 *  IrLAP, Recv ... with-invalid-Ns. p. 84
	 */
	return NS_UNEXPECTED;

	/* return NR_INVALID; */
}
/*
 * Function irlap_validate_nr_received (nr)
 *
 *    Validate the next to receive (nr) field from received frame.
 *
 */
int irlap_validate_nr_received( struct irlap_cb *self, int nr) 
{
	ASSERT( self != NULL, return -ENODEV;);
	ASSERT( self->magic == LAP_MAGIC, return -EBADR;);

	/*  nr as expected?  */
	if ( nr == self->vs) {
		DEBUG( 4, "*** irlap_validate_nr_received: expected!\n");
		return NR_EXPECTED;
	}

	/*
	 *  unexpected nr? (but within current window), first we check if the 
	 *  ns numbers of the frames in the current window wrap.
	 */
	if ( self->va < self->vs) {
		if (( nr >= self->va) && ( nr <= self->vs))
			return NR_UNEXPECTED;
	} else {
		if (( nr >= self->va) || ( nr <= self->vs)) 
			return NR_UNEXPECTED;
	}
	
	/* Invalid nr!  */
	return NR_INVALID;
}

/*
 * Function irlap_initiate_connection_state ()
 *
 *    Initialize the connection state parameters
 *
 */
void irlap_initiate_connection_state( struct irlap_cb *self) 
{
	DEBUG( 4, "irlap_initiate_connection_state()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	/* Next to send and next to receive */
	self->vs = self->vr = 0;

	/* Last frame which got acked (0 - 1) % 8 */
	self->va = 7;

	self->window = 1;

	self->remote_busy = FALSE;
	self->retry_count = 0;
}

/*
 * Function irlap_wait_min_turn_around (self, qos)
 *
 *    Wait negotiated minimum turn around time, this function actually sets
 *    the number of BOS's that must be sent before the next transmitted
 *    frame in order to delay for the specified amount of time. This is
 *    done to avoid using timers, and the forbidden udelay!
 */
void irlap_wait_min_turn_around( struct irlap_cb *self, struct qos_info *qos) 
{
	int usecs;
	int speed;
	int bytes = 0;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( qos != NULL, return;);

	/* Get QoS values.  */
	speed = qos->baud_rate.value;
	usecs = qos->min_turn_time.value;

	/* No need to calculate XBOFs for speeds over 115200 bps */
	if ( speed > 115200) {
		self->mtt_required = usecs;
		return;
	}
	
	DEBUG( 4, __FUNCTION__ "(), delay=%d usecs\n", usecs); 
	
	/*  
	 *  Send additional BOF's for the next frame for the requested
	 *  min turn time, so now we must calculate how many chars (XBOF's) we 
	 *  must send for the requested time period (min turn time)
	 */
	bytes = speed * usecs / 10000000;

	DEBUG( 4, __FUNCTION__ "(), xbofs delay = %d\n", bytes);
	
	self->xbofs_delay = bytes;
}

/*
 * Function irlap_flush_all_queues (void)
 *
 *    Flush all queues
 *
 */
void irlap_flush_all_queues( struct irlap_cb *self) 
{
	struct sk_buff* skb;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	/* Free transmission queue */
	while (( skb = skb_dequeue( &self->tx_list)) != NULL)
		dev_kfree_skb( skb);
	
	/* Free sliding window buffered packets */
	while (( skb = skb_dequeue( &self->wx_list)) != NULL)
		dev_kfree_skb( skb);

#ifdef CONFIG_IRDA_RECYCLE_RR
	if ( self->recycle_rr_skb) { 
 		dev_kfree_skb( self->recycle_rr_skb);
 		self->recycle_rr_skb = NULL;
 	}
#endif
}

/*
 * Function irlap_setspeed (self, speed)
 *
 *    Change the speed of the IrDA port
 *
 */
void irlap_change_speed( struct irlap_cb *self, int speed)
{
	DEBUG( 4, __FUNCTION__ "(), setting speed to %d\n", speed);

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	if ( !self->irdev) {
		DEBUG( 0, __FUNCTION__ "(), driver missing!\n");
		return;
	}

	irda_device_change_speed( self->irdev, speed);

	self->qos_rx.baud_rate.value = speed;
	self->qos_tx.baud_rate.value = speed;
}

#ifdef CONFIG_IRDA_COMPRESSION
void irlap_init_comp_qos_capabilities( struct irlap_cb *self)
{
	struct irda_compressor *comp;
	__u8 mask; /* Current bit tested */
	int i;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	/* 
	 *  Find out which compressors we support. We do this be checking that
	 *  the corresponding compressor for each bit set in the QoS bits has 
	 *  actually been loaded. Ths is sort of hairy code but that is what 
	 *  you get when you do a little bit flicking :-)
	 */
	DEBUG( 4, __FUNCTION__ "(), comp bits 0x%02x\n", 
	       self->qos_rx.compression.bits); 
	mask = 0x80; /* Start with testing MSB */
	for ( i=0;i<8;i++) {
		DEBUG( 4, __FUNCTION__ "(), testing bit %d\n", 8-i);
		if ( self->qos_rx.compression.bits & mask) {
			DEBUG( 4, __FUNCTION__ "(), bit %d is set by defalt\n",
			       8-i);
			comp = hashbin_find( irlap_compressors, 
					     compression[ msb_index(mask)], 
					     NULL);
			if ( !comp) {
				/* Protocol not supported, so clear the bit */
				DEBUG( 4, __FUNCTION__ "(), Compression "
				       "protocol %d has not been loaded!\n", 
				       compression[msb_index(mask)]);
				self->qos_rx.compression.bits &= ~mask;
				DEBUG( 4, __FUNCTION__ 
				       "(), comp bits 0x%02x\n", 
				       self->qos_rx.compression.bits); 
			}
		}
		/* Try the next bit */
		mask >>= 1;
	}
}
#endif	

/*
 * Function irlap_init_qos_capabilities (self, qos)
 *
 *    Initialize QoS for this IrLAP session, What we do is to compute the
 *    intersection of the QoS capabilities for the user, driver and for
 *    IrLAP itself. Normally, IrLAP will not specify any values, but it can
 *    be used to restrict certain values.
 */
void irlap_init_qos_capabilities( struct irlap_cb *self, 
				  struct qos_info *qos_user)
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	ASSERT( self->irdev != NULL, return;);

	/* Start out with the maximum QoS support possible */
	irda_init_max_qos_capabilies( &self->qos_rx);

#ifdef CONFIG_IRDA_COMPRESSION
	irlap_init_comp_qos_capabilities( self);
#endif

	/* Apply drivers QoS capabilities */
	irda_qos_compute_intersection( &self->qos_rx, 
				       irda_device_get_qos( self->irdev));

	/*
	 *  Check for user supplied QoS parameters. The service user is only 
	 *  allowed to supply these values. We check each parameter since the
	 *  user may not have set all of them.
	 */
	if ( qos_user != NULL) {
		DEBUG( 0, __FUNCTION__ "(), Found user specified QoS!\n");

		if ( qos_user->baud_rate.bits)
			self->qos_rx.baud_rate.bits &= qos_user->baud_rate.bits;

		if ( qos_user->max_turn_time.bits)
			self->qos_rx.max_turn_time.bits &= qos_user->max_turn_time.bits;
		if ( qos_user->data_size.bits)
			self->qos_rx.data_size.bits &= qos_user->data_size.bits;

		if ( qos_user->link_disc_time.bits)
			self->qos_rx.link_disc_time.bits &= qos_user->link_disc_time.bits;
#ifdef CONFIG_IRDA_COMPRESSION
		self->qos_rx.compression.bits &= qos_user->compression.bits;
#endif
	}

	/* 
	 *  Make the intersection between IrLAP and drivers QoS
	 *  capabilities 
	 */

	/* Use 500ms in IrLAP for now */
	self->qos_rx.max_turn_time.bits &= 0x03;
	self->qos_rx.max_turn_time.bits &= 0x01;

	/* Set data size */
	/* self->qos_rx.data_size.bits &= 0x03; */

	/* Set disconnect time */
	self->qos_rx.link_disc_time.bits &= 0x07;

	irda_qos_bits_to_value( &self->qos_rx);
}

/*
 * Function irlap_apply_default_connection_parameters (void)
 *
 *    Use the default connection and transmission parameters
 * 
 */
void irlap_apply_default_connection_parameters( struct irlap_cb *self)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	irlap_change_speed( self, 9600);

	/* Default value in NDM */
	self->bofs_count = 11;

	/* Use these until connection has been made */
	self->final_timeout = FINAL_TIMEOUT;
	self->poll_timeout = POLL_TIMEOUT;
	self->wd_timeout = WD_TIMEOUT;

	self->qos_tx.data_size.value = 64;
	self->qos_tx.additional_bofs.value = 11;

	irlap_flush_all_queues( self);
}

/*
 * Function irlap_apply_connection_parameters (qos)
 *
 *    Initialize IrLAP with the negotiated QoS values
 *
 */
void irlap_apply_connection_parameters( struct irlap_cb *self, 
					struct qos_info *qos) 
{
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);

	irlap_change_speed( self, qos->baud_rate.value);

	self->window_size = qos->window_size.value;
	self->window      = qos->window_size.value;
	self->bofs_count  = qos->additional_bofs.value;

	/*
	 *  Calculate how many bytes it is possible to transmit before the
	 *  link must be turned around wb = baud * mtt/1000 * 1/2
	 */
	self->window_bytes = qos->baud_rate.value 
		* qos->max_turn_time.value / 10000;
	DEBUG( 4, "Setting window_bytes = %d\n", self->window_bytes);

	/*
	 *  Set N1 to 0 if Link Disconnect/Threshold Time = 3 and set it to 
	 *  3 seconds otherwise. See page 71 in IrLAP for more details.
	 *  TODO: these values should be calculated from the final timer
         *  as well
	 */
	if ( qos->link_disc_time.value == 3)
		self->N1 = 0;
	else
		/* self->N1 = 6; */
		self->N1 = 3000 / qos->max_turn_time.value;
	
	DEBUG( 4, "Setting N1 = %d\n", self->N1);
	
	/* self->N2 = qos->link_disc_time.value * 2; */
	self->N2 = qos->link_disc_time.value * 1000 / qos->max_turn_time.value;
	DEBUG( 4, "Setting N2 = %d\n", self->N2);

	/* 
	 *  Initialize timeout values, some of the rules are listed on 
	 *  page 92 in IrLAP. Divide by 10 since the kernel timers has a
	 *  resolution of 10 ms.
	 */
	self->poll_timeout = qos->max_turn_time.value / 10;
	self->final_timeout = qos->max_turn_time.value / 10;
	self->wd_timeout = self->poll_timeout * 2;

#ifdef CONFIG_IRDA_COMPRESSION
	if ( qos->compression.value) {
		DEBUG( 0, __FUNCTION__ "(), Initializing compression\n");
		irda_set_compression( self, qos->compression.value);

		irlap_compressor_init( self, 0);
	}
#endif
}

#ifdef CONFIG_PROC_FS
/*
 * Function irlap_proc_read (buf, start, offset, len, unused)
 *
 *    Give some info to the /proc file system
 *
 */
int irlap_proc_read( char *buf, char **start, off_t offset, int len, 
		     int unused)
{
	struct irlap_cb *self;
	unsigned long flags;
	int i = 0;
     
	save_flags(flags);
	cli();

	len = 0;

	self = (struct irlap_cb *) hashbin_get_first( irlap);
	while ( self != NULL) {
		ASSERT( self != NULL, return -ENODEV;);
		ASSERT( self->magic == LAP_MAGIC, return -EBADR;);

		len += sprintf( buf+len, "IrLAP[%d] <-> %s ",
				i++, self->irdev->name);
		len += sprintf( buf+len, "state: %s\n", 
				irlap_state[ self->state]);
		
		len += sprintf( buf+len, "  caddr: %#02x, ", self->caddr);
		len += sprintf( buf+len, "saddr: %#08x, ", self->saddr);
		len += sprintf( buf+len, "daddr: %#08x\n", self->daddr);
		
		len += sprintf( buf+len, "  win size: %d, ", 
				self->window_size);
		len += sprintf( buf+len, "win: %d, ", self->window);
		len += sprintf( buf+len, "win bytes: %d, ", self->window_bytes);
		len += sprintf( buf+len, "bytes left: %d\n", self->bytes_left);

		len += sprintf( buf+len, "  tx queue len: %d ", 
				skb_queue_len( &self->tx_list));
		len += sprintf( buf+len, "win queue len: %d ", 
				skb_queue_len( &self->wx_list));
		len += sprintf( buf+len, "rbusy: %s\n", self->remote_busy ? 
				"TRUE" : "FALSE");
		
		len += sprintf( buf+len, "  retrans: %d ", self->retry_count);
		len += sprintf( buf+len, "vs: %d ", self->vs);
		len += sprintf( buf+len, "vr: %d ", self->vr);
		len += sprintf( buf+len, "va: %d\n", self->va);
		
		len += sprintf( buf+len, "  qos\tbps\tmaxtt\tdsize\twinsize\taddbofs\tmintt\tldisc\tcomp\n");
		
		len += sprintf( buf+len, "  tx\t%d\t", 
				self->qos_tx.baud_rate.value);
		len += sprintf( buf+len, "%d\t", 
				self->qos_tx.max_turn_time.value);
		len += sprintf( buf+len, "%d\t",
				self->qos_tx.data_size.value);
		len += sprintf( buf+len, "%d\t",
				self->qos_tx.window_size.value);
		len += sprintf( buf+len, "%d\t",
				self->qos_tx.additional_bofs.value);
		len += sprintf( buf+len, "%d\t", 
				self->qos_tx.min_turn_time.value);
		len += sprintf( buf+len, "%d\t", 
				self->qos_tx.link_disc_time.value);
#ifdef CONFIG_IRDA_COMPRESSION
		len += sprintf( buf+len, "%d",
				self->qos_tx.compression.value);
#endif
		len += sprintf( buf+len, "\n");

		len += sprintf( buf+len, "  rx\t%d\t", 
				self->qos_rx.baud_rate.value);
		len += sprintf( buf+len, "%d\t", 
				self->qos_rx.max_turn_time.value);
		len += sprintf( buf+len, "%d\t",
				self->qos_rx.data_size.value);
		len += sprintf( buf+len, "%d\t",
				self->qos_rx.window_size.value);
		len += sprintf( buf+len, "%d\t",
				self->qos_rx.additional_bofs.value);
		len += sprintf( buf+len, "%d\t", 
				self->qos_rx.min_turn_time.value);
		len += sprintf( buf+len, "%d\t", 
				self->qos_rx.link_disc_time.value);
#ifdef CONFIG_IRDA_COMPRESSION
		len += sprintf( buf+len, "%d",
				self->qos_rx.compression.value);
#endif
		len += sprintf( buf+len, "\n");
		
		self = (struct irlap_cb *) hashbin_get_next( irlap);
	}
	restore_flags(flags);

	return len;
}

#endif /* CONFIG_PROC_FS */

