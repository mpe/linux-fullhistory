/*********************************************************************
 *                
 * Filename:      irttp.c
 * Version:       0.4
 * Description:   Tiny Transport Protocol (TTP) implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:31 1997
 * Modified at:   Tue Jan 19 23:56:58 1999
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

#include <linux/config.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>
#include <net/irda/irttp.h>

struct irttp_cb *irttp = NULL;

static void __irttp_close_tsap( struct tsap_cb *self);

static void irttp_data_indication( void *instance, void *sap, 
				   struct sk_buff *skb);
static void irttp_udata_indication( void *instance, void *sap, 
				    struct sk_buff *skb);
static void irttp_disconnect_indication( void *instance, void *sap,  
					 LM_REASON reason,
					 struct sk_buff *);
static void irttp_connect_indication( void *instance, void *sap, 
				      struct qos_info *qos, int max_sdu_size,
				      struct sk_buff *skb);

static void irttp_run_tx_queue( struct tsap_cb *self);
static void irttp_run_rx_queue( struct tsap_cb *self);

static void irttp_flush_queues( struct tsap_cb *self);
static void irttp_fragment_skb( struct tsap_cb *self, struct sk_buff *skb);
static struct sk_buff *irttp_reassemble_skb( struct tsap_cb *self);
static void irttp_start_todo_timer( struct tsap_cb *self, int timeout);

/*
 * Function irttp_init (void)
 *
 *    Initialize the IrTTP layer. Called by module initialization code
 *
 */
__initfunc(int irttp_init(void))
{
	DEBUG( 4, "--> irttp_init\n");

	/* Initialize the irttp structure. */
	if ( irttp == NULL) {
		irttp = kmalloc( sizeof(struct irttp_cb), GFP_KERNEL);
		if ( irttp == NULL)
			return -ENOMEM;
	}
	memset( irttp, 0, sizeof(struct irttp_cb));

	irttp->magic = TTP_MAGIC;

	irttp->tsaps = hashbin_new( HB_LOCAL);
	if ( !irttp->tsaps) {
		printk( KERN_WARNING "IrDA: Can't allocate IrTTP hashbin!\n");
		return -ENOMEM;
	}
	
	return 0;
}

/*
 * Function irttp_cleanup (void)
 *
 *    Called by module destruction/cleanup code
 *
 */
void irttp_cleanup(void) 
{
	DEBUG( 4, "irttp_cleanup\n");
	
	/* Check for main structure */
	ASSERT( irttp != NULL, return;);
	ASSERT( irttp->magic == TTP_MAGIC, return;);
	
	/*
	 *  Delete hashbin and close all TSAP instances in it
	 */
	hashbin_delete( irttp->tsaps, (FREE_FUNC) __irttp_close_tsap);

	irttp->magic = ~TTP_MAGIC;
	
	/* De-allocate main structure */
	kfree( irttp);

	irttp = NULL;
}

/*
 * Function irttp_open_tsap (stsap, notify)
 *
 *    Create TSAP connection endpoint,
 */
struct tsap_cb *irttp_open_tsap( __u8 stsap_sel, int credit, 
				 struct notify_t *notify) 
{
	struct notify_t ttp_notify;
	struct tsap_cb *self;
	struct lsap_cb *lsap;

	ASSERT( irttp != NULL, return NULL;);
	ASSERT( irttp->magic == TTP_MAGIC, return NULL;);

	self = kmalloc( sizeof(struct tsap_cb), GFP_ATOMIC);
	if ( self == NULL) {
		printk( KERN_ERR "IrTTP: Can't allocate memory for "
			"TSAP control block!\n");
		return NULL;
	}
	memset( self, 0, sizeof(struct tsap_cb));

	init_timer( &self->todo_timer);

	/* Initialize callbacks for IrLMP to use */

	irda_notify_init( &ttp_notify);
	ttp_notify.connect_confirm = irttp_connect_confirm;
	ttp_notify.connect_indication = irttp_connect_indication;
	ttp_notify.disconnect_indication = irttp_disconnect_indication;
	ttp_notify.data_indication = irttp_data_indication;
	ttp_notify.udata_indication = irttp_udata_indication;
	ttp_notify.instance = self;
	strncpy( ttp_notify.name, notify->name, NOTIFY_MAX_NAME);

	/*
	 *  Create LSAP at IrLMP layer
	 */
	lsap = irlmp_open_lsap( stsap_sel, &ttp_notify);
	if ( lsap == NULL) {
		printk( KERN_ERR "IrTTP, Unable to get LSAP!!\n");
		return NULL;
	}
	
	/*
	 *  If user specified LSAP_ANY as source TSAP selector, then IrLMP
	 *  will replace it with whatever source selector which is free, so
	 *  the stsap_sel we have might not be valid anymore
	 */
	self->stsap_sel = lsap->slsap_sel;
	DEBUG( 4, __FUNCTION__ "(), stsap_sel=%02x\n", self->stsap_sel);

	self->notify = *notify;
	self->lsap = lsap;
	self->magic = TTP_TSAP_MAGIC;

	skb_queue_head_init( &self->rx_queue);
	skb_queue_head_init( &self->tx_queue);
	skb_queue_head_init( &self->rx_fragments);

	/*
	 *  Insert ourself into the hashbin
	 */
	hashbin_insert( irttp->tsaps, (QUEUE *) self, self->stsap_sel, NULL);

	if ( credit > TTP_MAX_QUEUE)
		self->initial_credit = TTP_MAX_QUEUE;
	else
		self->initial_credit = credit;
	
	return self;
}

/*
 * Function irttp_close (handle)
 *
 *    Remove an instance of a TSAP. This function should only deal with the
 *    deallocation of the TSAP, and resetting of the TSAPs values;
 *
 */
static void __irttp_close_tsap( struct tsap_cb *self)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	/* First make sure we're connected. */
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);

	irttp_flush_queues( self);

	del_timer( &self->todo_timer);

	self->connected = FALSE;
	self->magic = ~TTP_TSAP_MAGIC;

	/*
	 *  Deallocate structure
	 */
	kfree( self);

	DEBUG( 4, "irttp_close_tsap() -->\n");
}

/*
 * Function irttp_close (self)
 *
 *    Remove TSAP from list of all TSAPs and then deallocate all resources
 *    associated with this TSAP
 *
 */
void irttp_close_tsap( struct tsap_cb *self)
{
	struct tsap_cb *tsap;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);

	tsap = hashbin_remove( irttp->tsaps, self->stsap_sel, NULL);

	ASSERT( tsap == self, return;);

	/* Close corresponding LSAP */
	if ( self->lsap) {
		irlmp_close_lsap( self->lsap);
		self->lsap = NULL;
	}

	__irttp_close_tsap( self);
}

/*
 * Function irttp_udata_request (self, skb)
 *
 *    Send unreliable data on this TSAP
 *
 */
int irttp_udata_request( struct tsap_cb *self, struct sk_buff *skb) 
{
	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return -1;);
	ASSERT( skb != NULL, return -1;);

	DEBUG( 4, __FUNCTION__ "()\n");

	/* Check that nothing bad happens */
	if (( skb->len == 0) || ( !self->connected)) {
		DEBUG( 0, __FUNCTION__ "(), No data, or not connected\n");
		return -1;
	}
	
	if ( skb->len > self->max_seg_size) {
		DEBUG( 0, __FUNCTION__ "(), UData is to large for IrLAP!\n");
		return -1;
	}
		    
	irlmp_udata_request( self->lsap, skb);
	self->stats.tx_packets++;

	return 0;
}

/*
 * Function irttp_data_request (handle, skb)
 *
 *    Queue frame for transmission. If SAR is enabled, fragement the frame 
 *    and queue the fragments for transmission
 */
int irttp_data_request( struct tsap_cb *self, struct sk_buff *skb) 
{
	__u8 *frame;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return -1;);
	ASSERT( skb != NULL, return -1;);

	/* Check that nothing bad happens */
	if (( skb->len == 0) || ( !self->connected)) {
		DEBUG( 4, __FUNCTION__ "(), No data, or not connected\n");
		return -1;
	}

	/*  
	 *  Check if SAR is disabled, and the frame is larger than what fits
	 *  inside an IrLAP frame
	 */
	if (( self->tx_max_sdu_size == 0) && 
	    ( skb->len > self->max_seg_size)) 
	{
		DEBUG( 0, __FUNCTION__ 
		       "(), SAR disabled, and data is to large for IrLAP!\n");
		return -1;
	}

	/* 
	 *  Check if SAR is enabled, and the frame is larger than the 
	 *  TxMaxSduSize 
	 */
	if (( self->tx_max_sdu_size != 0) && 
	    (skb->len > self->tx_max_sdu_size))
	{
		DEBUG( 0, __FUNCTION__ "(), SAR enabled, "
		       "but data is larger than TxMaxSduSize!\n");
		return -1;
	}
	/* 
	 *  Check if transmit queue is full
	 */
	if ( skb_queue_len( &self->tx_queue) >= TTP_MAX_QUEUE) {
		/*
		 *  Give it a chance to empty itself
		 */
		irttp_run_tx_queue( self);
		
		return -1;
	}
       
	/* Queue frame, or queue frame segments */
	if (( self->tx_max_sdu_size == 0) || 
	    ( skb->len < self->max_seg_size)) {
		/* Queue frame */
		frame = skb_push( skb, TTP_HEADER);
		frame[0] = 0x00; /* Clear more bit */
		
		DEBUG( 4, __FUNCTION__ "(), queueing original skb\n");
		skb_queue_tail( &self->tx_queue, skb);
	} else {
		/*
		 *  Fragment the frame, this function will also queue the
		 *  fragments, we don't care about the fact the the transmit
		 *  queue may be overfilled by all the segments for a little
		 *  while
		 */
		irttp_fragment_skb( self, skb);
	}

	/* Check if we can accept more data from client */
	if (( !self->tx_sdu_busy) && 
	    ( skb_queue_len( &self->tx_queue) > HIGH_THRESHOLD)) {
		
		/* Tx queue filling up, so stop client */
		self->tx_sdu_busy = TRUE;
		
	 	if ( self->notify.flow_indication) {
 			self->notify.flow_indication( self->notify.instance, 
						      self, 
						      FLOW_STOP);
		}
 	}
	
	/* Try to make some progress */
	irttp_run_tx_queue( self);
	
	return 0;
}

/*
 * Function irttp_xmit (self)
 *
 *    If possible, transmit a frame queued for transmission.
 *
 */
static void irttp_run_tx_queue( struct tsap_cb *self) 
{
	struct sk_buff *skb = NULL;
	unsigned long flags;
	__u8 *frame;
	int n;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);

	if ( irda_lock( &self->tx_queue_lock) == FALSE)
		return;

	while (( self->send_credit > 0) && !skb_queue_empty( &self->tx_queue)){

		skb = skb_dequeue( &self->tx_queue);
		ASSERT( skb != NULL, return;);
		
		/* Make room for TTP header */
		ASSERT( skb_headroom( skb) >= TTP_HEADER, return;);
				
		/*
		 *  Since we can transmit and receive frames concurrently, 
		 *  the code below is a critical region and we must assure that
		 *  nobody messes with the credits while we update them.
		 */
		save_flags( flags); 
		cli();

		n = self->avail_credit;
		self->avail_credit = 0;
		
		/* Only space for 127 credits in frame */
		if ( n > 127) {
			self->avail_credit = n-127;
			n = 127;
		}
		self->remote_credit += n;
		self->send_credit--;

		restore_flags(flags);

		DEBUG( 4, "irttp_xmit: Giving away %d credits\n", n);
		
		/* 
		 *  More bit must be set by the data_request() or fragment() 
		 *  functions
		 */
		frame = skb->data;

		DEBUG( 4, __FUNCTION__ "(), More=%s\n", frame[0] & 0x80 ? 
		       "TRUE" : "FALSE" );

		frame[0] |= (__u8) (n & 0x7f);
		
		irlmp_data_request( self->lsap, skb);
		self->stats.tx_packets++;

		/* Check if we can accept more frames from client */
		if (( self->tx_sdu_busy) && 
		    ( skb_queue_len( &self->tx_queue) < LOW_THRESHOLD)) { 
			self->tx_sdu_busy = FALSE;
			
			if ( self->notify.flow_indication)
				self->notify.flow_indication( self->notify.instance, 
							      self, 
							      FLOW_START);
		}
	}
	
	/* Reset lock */
	self->tx_queue_lock = 0;

	/* Check if there is any disconnect request pending */
	if ( self->disconnect_pend) {
		if ( self->disconnect_skb) {
			irttp_disconnect_request( self, self->disconnect_skb,
						  P_NORMAL);
			self->disconnect_skb = NULL;
		} else
			irttp_disconnect_request( self, NULL, P_NORMAL);
	}
}

/*
 * Function irttp_give_credit (self)
 *
 *    Send a dataless flowdata TTP-PDU and give available credit to peer
 *    TSAP
 */
void irttp_give_credit( struct tsap_cb *self) 
{
	struct sk_buff *tx_skb = NULL;
	unsigned long flags;
	int n;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);	

	DEBUG( 4, "irttp_give_credit() send=%d,avail=%d,remote=%d\n", 
	       self->send_credit, self->avail_credit, self->remote_credit);
	
	/* Give credit to peer */
	tx_skb = dev_alloc_skb( 64);
	if ( tx_skb == NULL) {
		DEBUG( 0, "irttp_give_credit: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	/* Reserve space for LMP, and LAP header */
	skb_reserve( tx_skb, LMP_HEADER+LAP_HEADER);

	/*
	 *  Since we can transmit and receive frames concurrently, 
	 *  the code below is a critical region and we must assure that
	 *  nobody messes with the credits while we update them.
	 */
	save_flags( flags);
	cli();

	n = self->avail_credit;
	self->avail_credit = 0;
	
	/* Only space for 127 credits in frame */
	if ( n > 127) {
		self->avail_credit = n - 127;
		n = 127;
	}
	self->remote_credit += n;

	restore_flags(flags);

	skb_put( tx_skb, 1);
	tx_skb->data[0] = (__u8) ( n & 0x7f);
	
	irlmp_data_request( self->lsap, tx_skb);
	self->stats.tx_packets++;
}

/*
 * Function irttp_udata_indication (instance, sap, skb)
 *
 *    
 *
 */
void irttp_udata_indication( void *instance, void *sap, struct sk_buff *skb) 
{
	struct tsap_cb *self;

	DEBUG( 4, __FUNCTION__ "()\n");

	self = (struct tsap_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* Just pass data to layer above */
	if ( self->notify.udata_indication) {
		self->notify.udata_indication( self->notify.instance, self, 
					       skb);
	}
	self->stats.rx_packets++;
}

/*
 * Function irttp_data_indication (handle, skb)
 *
 *    Receive segment from IrLMP. 
 *
 */
void irttp_data_indication( void *instance, void *sap, struct sk_buff *skb)
{
	struct tsap_cb *self;
	int more;
	int n;
	__u8 *frame;
	
	self = (struct tsap_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	frame = skb->data;
	
	n = frame[0] & 0x7f;     /* Extract the credits */
	more = frame[0] & 0x80;

 	DEBUG( 4, __FUNCTION__"(), got %d credits, TSAP sel=%02x\n", 
	       n, self->stsap_sel);

	self->stats.rx_packets++;

	/* 
	 *  Data or dataless frame? Dataless frames only contain the 
	 *  TTP_HEADER
	 */
	if ( skb->len == 1) {
		/* Dataless flowdata TTP-PDU */
		self->send_credit += n;
	} else {
		/* Deal with inbound credit */
		self->send_credit += n;
		self->remote_credit--;
		
		/* 
		 *  We don't remove the TTP header, since we must preserve the
		 *  more bit, so the defragment routing knows what to do
		 */
		skb_queue_tail( &self->rx_queue, skb);
	} 

	irttp_run_rx_queue( self);

	/* 
	 *  Give avay some credits to peer? 
	 */
	if (( skb_queue_empty( &self->tx_queue)) && 
	    ( self->remote_credit < LOW_THRESHOLD) && 
	    ( self->avail_credit > 0)) 
	{
		/* Schedule to start immediately after this thread */
		irttp_start_todo_timer( self, 0);
	}

	/* If peer has given us some credites and we didn't have anyone
         * from before, the we need to shedule the tx queue? 
	 */
	if ( self->send_credit == n)
		irttp_start_todo_timer( self, 0);
}

/*
 * Function irttp_flow_request (self, command)
 *
 *    This funtion could be used by the upper layers to tell IrTTP to stop
 *    delivering frames if the receive queues are starting to get full, or 
 *    to tell IrTTP to start delivering frames again.
 */
void irttp_flow_request( struct tsap_cb *self, LOCAL_FLOW flow)
{
	DEBUG( 0, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);

	switch ( flow) {
	case FLOW_STOP:
		DEBUG( 0, __FUNCTION__ "(), flow stop\n");
		self->rx_sdu_busy = TRUE;
		break;
	case FLOW_START:
		DEBUG( 0, __FUNCTION__ "(), flow start\n");
		self->rx_sdu_busy = FALSE;
		
		irttp_run_rx_queue( self);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown flow command!\n");
	}
}
	
/*
 * Function irttp_connect_request (self, dtsap_sel, daddr, qos)
 *
 *    Try to connect to remote destination TSAP selector
 *
 */
void irttp_connect_request( struct tsap_cb *self, __u8 dtsap_sel, __u32 daddr,
			    struct qos_info *qos, int max_sdu_size, 
			    struct sk_buff *userdata) 
{
	struct sk_buff *skb;
	__u8 *frame;
	__u8 n;
	
	DEBUG( 4, __FUNCTION__ "(), max_sdu_size=%d\n", max_sdu_size); 
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);

	/* Any userdata supplied? */
	if ( userdata == NULL) {
		skb = dev_alloc_skb( 64);
		if (skb == NULL) {
			DEBUG( 0, __FUNCTION__ "Could not allocate an "
			       "sk_buff of length %d\n", 64);
			return;
		}
		
		/* Reserve space for MUX_CONTROL and LAP header */
		skb_reserve( skb, (TTP_HEADER+LMP_CONTROL_HEADER+LAP_HEADER));
	} else {
		skb = userdata;
		/*  
		 *  Check that the client has reserved enough space for 
		 *  headers
		 */
		ASSERT( skb_headroom( userdata) >= 
			(TTP_HEADER+LMP_CONTROL_HEADER+LAP_HEADER), return;);
	}

	/* Initialize connection parameters */
	self->connected = FALSE;
	self->avail_credit = 0;
	self->rx_max_sdu_size = max_sdu_size;
	self->rx_sdu_size = 0;
	self->rx_sdu_busy = FALSE;
	self->dtsap_sel = dtsap_sel;

	n = self->initial_credit;

	self->remote_credit = 0;
	self->send_credit = 0;
	
	/*
	 *  Give away max 127 credits for now
	 */
	if ( n > 127) {
		self->avail_credit=n-127;
		n = 127;
	}

	self->remote_credit = n;

	/* SAR enabled? */
	if ( max_sdu_size > 0) {
		ASSERT( skb_headroom( skb) >= 
			(TTP_HEADER_WITH_SAR+LMP_CONTROL_HEADER+LAP_HEADER), 
			return;);

		/* Insert SAR parameters */
		frame = skb_push( skb, TTP_HEADER_WITH_SAR);
		
		frame[0] = TTP_PARAMETERS | n; 
		frame[1] = 0x04; /* Length */
		frame[2] = 0x01; /* MaxSduSize */
		frame[3] = 0x02; /* Value length */
		*((__u16 *) (frame+4))= htons( max_sdu_size); /* Big endian! */
	} else {
		/* Insert plain TTP header */
		frame = skb_push( skb, TTP_HEADER);
		
		/* Insert initial credit in frame */
		frame[0] = n & 0x7f;
	}

	/* Connect with IrLMP. No QoS parameters for now */
	irlmp_connect_request( self->lsap, dtsap_sel, daddr, qos, skb);
}

/*
 * Function irttp_connect_confirm (handle, qos, skb)
 *
 *    Sevice user confirms TSAP connection with peer. 
 *
 */
void irttp_connect_confirm( void *instance, void *sap, struct qos_info *qos,
			    int max_seg_size, struct sk_buff *skb) 
{
	struct tsap_cb *self;
	__u8 *frame;
	__u8 n;
	int parameters;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	self = (struct tsap_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* FIXME: just remove this when we know its working */
	ASSERT( max_seg_size == qos->data_size.value, return;);

	self->max_seg_size = max_seg_size-LMP_HEADER-LAP_HEADER;

	/*
	 *  Check if we have got some QoS parameters back! This should be the
	 *  negotiated QoS for the link.
	 */
	if ( qos) {
		DEBUG( 4, "IrTTP, Negotiated BAUD_RATE: %02x\n", 
		       qos->baud_rate.bits);			
		DEBUG( 4, "IrTTP, Negotiated BAUD_RATE: %d bps.\n", 
		       qos->baud_rate.value);
	}

	frame = skb->data;
	n = frame[0] & 0x7f;
	
	DEBUG( 4, __FUNCTION__ "(), Initial send_credit=%d\n", n);
	
	self->send_credit = n;
	self->tx_max_sdu_size = 0;
	self->connected = TRUE;

	parameters = frame[0] & 0x80;	
	if ( parameters) {
		DEBUG( 4, __FUNCTION__ "(), Contains parameters!\n");
		
		self->tx_max_sdu_size = ntohs(*(__u16 *)(frame+4));
		DEBUG( 4, __FUNCTION__ "(), RxMaxSduSize=%d\n", 
		       self->tx_max_sdu_size);
	}
	
	DEBUG( 4, "irttp_connect_confirm() send=%d,avail=%d,remote=%d\n", 
	       self->send_credit, self->avail_credit, self->remote_credit);

	skb_pull( skb, TTP_HEADER);

	if ( self->notify.connect_confirm) {
		self->notify.connect_confirm( self->notify.instance, self, 
					      qos, self->tx_max_sdu_size, 
					      skb);
	}
}

/*
 * Function irttp_connect_indication (handle, skb)
 *
 *    Some other device is connecting to this TSAP
 *
 */
void irttp_connect_indication( void *instance, void *sap, 
			       struct qos_info *qos, int max_seg_size, 
			       struct sk_buff *skb) 
{
	struct tsap_cb *self;
	__u8 *frame;
	int parameters;
	int n;

	self = (struct tsap_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);
 	ASSERT( skb != NULL, return;);

	/* FIXME: just remove this when we know its working */
	ASSERT( max_seg_size == qos->data_size.value, return;);

	self->max_seg_size = max_seg_size-LMP_HEADER-LAP_HEADER;

	DEBUG( 4, "irttp_connect_indication(), TSAP sel=%02x\n", 
	       self->stsap_sel);

	/* FIXME: Need to update dtsap_sel if its equal to LSAP_ANY */
/* 	if ( self->dtsap_sel == LSAP_ANY) */
/* 		self->dtsap_sel = lsap->dlsap_sel; */

	frame = skb->data;
	n = frame[0] & 0x7f;

	self->send_credit = n;
	self->tx_max_sdu_size = 0;
	
	parameters = frame[0] & 0x80;	
	if ( parameters) {
		DEBUG( 4, __FUNCTION__ "(), Contains parameters!\n");
		
		self->tx_max_sdu_size = ntohs(*(__u16 *)(frame+4));
		DEBUG( 4, __FUNCTION__ "(), MaxSduSize=%d\n", 
		       self->tx_max_sdu_size);
	}

	DEBUG( 4, "irttp_connect_indication: initial send_credit=%d\n", n);

	skb_pull( skb, 1);

	if ( self->notify.connect_indication) {
		self->notify.connect_indication( self->notify.instance, self, 
						 qos, self->rx_max_sdu_size, 
						 skb);
	}
}

/*
 * Function irttp_connect_response (handle, userdata)
 *
 *    Service user is accepting the connection, just pass it down to
 *    IrLMP!
 * 
 */
void irttp_connect_response( struct tsap_cb *self, int max_sdu_size, 
			     struct sk_buff *userdata)
{
	struct sk_buff *skb;
	__u8 *frame;
	__u8 n;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);

	DEBUG( 4, __FUNCTION__ "(), Source TSAP selector=%02x\n", 
	       self->stsap_sel);
	
	/* Any userdata supplied? */
	if ( userdata == NULL) {
		skb = dev_alloc_skb( 64);
		if (skb == NULL) {
			DEBUG( 0, __FUNCTION__ "Could not allocate an "
			       "sk_buff of length %d\n", 64);
			return;
		}

		/* Reserve space for MUX_CONTROL and LAP header */
		skb_reserve( skb, (TTP_HEADER+LMP_CONTROL_HEADER+LAP_HEADER));
	} else {
		skb = userdata;
		/*  
		 *  Check that the client has reserved enough space for 
		 *  headers
		 */
		ASSERT( skb_headroom( skb) >= 
			(TTP_HEADER+LMP_CONTROL_HEADER+LAP_HEADER), return;);
	}
	
	self->avail_credit = 0;
	self->remote_credit = 0;
	self->rx_max_sdu_size = max_sdu_size;
	self->rx_sdu_size = 0;
	self->rx_sdu_busy = FALSE;

	n = self->initial_credit;

	/* Frame has only space for max 127 credits (7 bits) */
	if ( n > 127) {
		self->avail_credit = n - 127;
		n = 127;
	}

	self->remote_credit = n;
	self->connected = TRUE;

	/* SAR enabled? */
	if ( max_sdu_size > 0) {
		ASSERT( skb_headroom( skb) >= 
			(TTP_HEADER_WITH_SAR+LMP_CONTROL_HEADER+LAP_HEADER), 
			return;);
		
		/* Insert TTP header with SAR parameters */
		frame = skb_push( skb, TTP_HEADER_WITH_SAR);
		
		frame[0] = TTP_PARAMETERS | n;
		frame[1] = 0x04; /* Length */
		frame[2] = 0x01; /* MaxSduSize */
		frame[3] = 0x02; /* Value length */
		*((__u16 *) (frame+4))= htons( max_sdu_size);
	} else {
		/* Insert TTP header */
		frame = skb_push( skb, TTP_HEADER);
		
		frame[0] = n & 0x7f;
	}
	 
	irlmp_connect_response( self->lsap, skb);
}

/*
 * Function irttp_disconnect_request ( self)
 *
 *    Close this connection please! If priority is high, the queued data 
 *    segments, if any, will be deallocated first
 *
 */
void irttp_disconnect_request( struct tsap_cb *self, struct sk_buff *userdata, 
			       int priority)
{
	struct sk_buff *skb;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);

	/* Already disconnected? */
	if ( !self->connected) {
		DEBUG( 4, __FUNCTION__ "(), already disconnected!\n");
		return;
	}

	/* Disconnect already pending? */
	if ( self->disconnect_pend) {
		DEBUG( 0, __FUNCTION__ "(), disconnect already pending\n");
		if ( userdata) {
			dev_kfree_skb( userdata);
		}

		/* Try to make some progress */
		irttp_run_rx_queue( self);
		return;
	}

	/*
	 *  Check if there is still data segments in the transmit queue
	 */
	if ( skb_queue_len( &self->tx_queue) > 0) {
		if ( priority == P_HIGH) {
			DEBUG( 0, __FUNCTION__  "High priority!!()\n" );
			
			/* 
			 *  No need to send the queued data, if we are 
			 *  disconnecting right now since the data will
			 *  not have any usable connection to be sent on
			 */
			irttp_flush_queues( self);
		} else if ( priority == P_NORMAL) {
			/* 
			 *  Must delay disconnect til after all data segments
			 *  have been sent an the tx_queue is empty
			 */
			if ( userdata)
				self->disconnect_skb = userdata;
			else
				self->disconnect_skb = NULL;

			self->disconnect_pend = TRUE;

			irttp_run_tx_queue( self);
			/*  
			 *  irttp_xmit will call us again when the tx_queue
			 *  is empty
			 */
			return;
		}
	}
	DEBUG( 0, __FUNCTION__ "(), Disconnecting ...\n");

	self->connected = FALSE;
	
	if ( !userdata) {
		skb = dev_alloc_skb( 64);
		if (skb == NULL) {
			DEBUG( 0, __FUNCTION__ "(), Could not allocate an "
			       "sk_buff of length %d\n", 64);
			return;
		}

		/* 
		 *  Reserve space for MUX and LAP header 
		 */
		skb_reserve( skb, LMP_CONTROL_HEADER+LAP_HEADER);

		userdata = skb;
	}
	irlmp_disconnect_request( self->lsap, userdata);
}

/*
 * Function irttp_disconnect_indication (self, reason)
 *
 *    Disconnect indication, TSAP disconnected by peer?
 *
 */
void irttp_disconnect_indication( void *instance, void *sap, LM_REASON reason, 
				  struct sk_buff *userdata) 
{
	struct tsap_cb *self;

	DEBUG( 4, "irttp_disconnect_indication()\n");

	self = ( struct tsap_cb *) instance;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);
	
	self->connected = FALSE;
	
	/* 
	 *  Use callback to notify layer above 
	 */
	if ( self->notify.disconnect_indication)
		self->notify.disconnect_indication( self->notify.instance, 
						    self, reason, userdata);
}

/*
 * Function irttp_run_rx_queue (self)
 *
 *     Check if we have any frames to be transmitted, or if we have any
 *     available credit to give away.
 */
void irttp_run_rx_queue( struct tsap_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
	int more = 0;
	void *instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);

	instance = self->notify.instance;
	ASSERT( instance != NULL, return;);

	DEBUG( 4, "irttp_do_events() send=%d,avail=%d,remote=%d\n", 
	       self->send_credit, self->avail_credit, self->remote_credit);

	if ( irda_lock( &self->rx_queue_lock) == FALSE)
		return;
	
	/*
	 *  Process receive queue
	 */
	while (( !skb_queue_empty( &self->rx_queue)) &&  !self->rx_sdu_busy) {

		skb = skb_dequeue( &self->rx_queue);
		if ( !skb)
			break; /* Should not happend, but ...  */
		
		self->avail_credit++;

		frame = skb->data;
		more = frame[0] & 0x80;
		DEBUG( 4, __FUNCTION__ "(), More=%s\n", more ? "TRUE" : 
		       "FALSE");

		/* Remove TTP header */
		skb_pull( skb, TTP_HEADER);

		/* Add the length of the remaining data */
		self->rx_sdu_size += skb->len;

		/*  
		 * If SAR is disabled, or user has requested no reassembly
		 * of received fragements then we just deliver them
		 * immediately. This can be requested by clients that
		 * implements byte streams without any message boundaries
		 */
		if ((self->no_defrag) || (self->rx_max_sdu_size == 0)) {
			self->notify.data_indication( instance, self, skb);
			self->rx_sdu_size = 0;

			continue;
		}

		/* Check if this is a fragment, and not the last fragment */
		if ( more) {
			/*  
			 *  Queue the fragment if we still are within the 
			 *  limits of the maximum size of the rx_sdu
			 */
			if ( self->rx_sdu_size <= self->rx_max_sdu_size) {
				DEBUG( 4, __FUNCTION__ 
				       "(), queueing fragment\n");

				skb_queue_tail( &self->rx_fragments, skb);
			} else {
				DEBUG( 0, __FUNCTION__ "(), Error!\n");
			}
		} else {
			/*
			 *  This is the last fragment, so time to reassemble!
			 */
			if ( self->rx_sdu_size <= self->rx_max_sdu_size) {

				/* A little optimizing. Only queue the 
				 * fragment if there is other fragments. Since
				 * if this is the last and only fragment, 
				 * there is no need to reassemble 
				 */
				if ( !skb_queue_empty( &self->rx_fragments)) {

					DEBUG( 4, __FUNCTION__ 
					       "(), queueing fragment\n");
					skb_queue_tail( &self->rx_fragments, 
							skb);

					skb = irttp_reassemble_skb( self);
				}
				self->notify.data_indication( instance, self, 
							      skb);
			} else {
				DEBUG( 0, __FUNCTION__ 
				       "(), Truncated frame\n");
				self->notify.data_indication( 
					self->notify.instance, self, skb);
			}
			self->rx_sdu_size = 0;
		}
	}
	/* Reset lock */
	self->rx_queue_lock = 0;
}

/*
 * Function irttp_flush_queues (self)
 *
 *     Flushes (removes all frames) in transitt-buffer (tx_list)
 */
void irttp_flush_queues( struct tsap_cb *self)
{
	struct sk_buff* skb;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);
	
	/* Deallocate frames waiting to be sent */
	while (( skb = skb_dequeue( &self->tx_queue)) != NULL) {
		dev_kfree_skb( skb);
	}
	/* Deallocate received frames */
	while (( skb = skb_dequeue( &self->rx_queue)) != NULL) {
		dev_kfree_skb( skb);
	}
	/* Deallocate received fragments */
	while (( skb = skb_dequeue( &self->rx_fragments)) != NULL) {
		dev_kfree_skb( skb);
	}
	
}

/*
 * Function irttp_reasseble (self)
 *
 *    Makes a new (continuous) skb of all the fragments in the fragment
 *    queue
 *
 */
static struct sk_buff *irttp_reassemble_skb( struct tsap_cb *self)
{
	struct sk_buff *skb, *frag;
	int n = 0;  /* Fragment index */

      	ASSERT( self != NULL, return NULL;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return NULL;);

	DEBUG( 4, __FUNCTION__ "(), self->rx_sdu_size=%d\n", 
	       self->rx_sdu_size);

	skb = dev_alloc_skb( self->rx_sdu_size);
	if ( !skb) {
		DEBUG( 0, __FUNCTION__ "(), unable to allocate skb\n");
		return NULL;
	}

	skb_put( skb, self->rx_sdu_size);

	/*
	 *  Copy all fragments to a new buffer
	 */
	while (( frag = skb_dequeue( &self->rx_fragments)) != NULL) {
		memcpy( skb->data+n, frag->data, frag->len);
		n += frag->len;
		
		dev_kfree_skb( frag);
	}
	DEBUG( 4, __FUNCTION__ "(), frame len=%d\n", n);
	/* Set the new length */

	DEBUG( 4, __FUNCTION__ "(), rx_sdu_size=%d\n", self->rx_sdu_size);
	ASSERT( n <= self->rx_sdu_size, return NULL;);
	skb_trim( skb, n);

	self->rx_sdu_size = 0;

	return skb;
}

/*
 * Function irttp_fragment_skb (skb)
 *
 *    Fragments a frame and queues all the fragments for transmission
 *
 */
static void irttp_fragment_skb( struct tsap_cb *self, struct sk_buff *skb)
{
	struct sk_buff *frag;
	__u8 *frame;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/*
	 *  Split frame into a number of segments
	 */
	while ( skb->len > 0) {
		/*
		 *  Instead of making the last segment, we just
		 *  queue what is left of the original skb
		 */
		if ( skb->len < self->max_seg_size) {
			DEBUG( 4, __FUNCTION__ 
			       "(), queuing last segment\n");

			frame = skb_push( skb, TTP_HEADER);
			frame[0] = 0x00; /* Clear more bit */
			skb_queue_tail( &self->tx_queue, skb);
			
			return;
		}
		
		/* Make new segment */
		frag = dev_alloc_skb( self->max_seg_size+
					 TTP_HEADER+LMP_HEADER+
					 LAP_HEADER);
		if ( frag == NULL) {
			DEBUG( 0, __FUNCTION__ 
			       "(), Couldn't allocate skbuff!\n");
			return;
		}

		skb_reserve( frag, LMP_HEADER+LAP_HEADER);

		/*
		 *  Copy data from the original skb into this fragment. We
		 *  first insert the TTP header with the more bit set
		 */
		frame = skb_put( frag, self->max_seg_size+TTP_HEADER);
		frame[0] = TTP_MORE;
		memcpy( frag->data+1, skb->data, self->max_seg_size);
		
		/* Hide the copied data from the original skb */
		skb_pull( skb, self->max_seg_size);
		
		skb_queue_tail( &self->tx_queue, frag);
	}
}

/*
 * Function irttp_todo_expired (data)
 *
 *    Todo timer has expired!
 *
 */
static void irttp_todo_expired( unsigned long data)
{
	struct tsap_cb *self = ( struct tsap_cb *) data;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	/* Check that we still exist */
	if ( !self || self->magic != TTP_TSAP_MAGIC) {
		return;
	}

	irttp_run_rx_queue( self);
	irttp_run_tx_queue( self);

	/*  Give avay some credits to peer?  */
	if (( skb_queue_empty( &self->tx_queue)) && 
	    ( self->remote_credit < LOW_THRESHOLD) && 
	    ( self->avail_credit > 0)) 
	{
		DEBUG( 4, "irttp_do_events: sending credit!\n");
		irttp_give_credit( self);
	}
	
	/* Rearm! */
	/* irttp_start_todo_timer( self, 50); */
}

/*
 * Function irttp_start_todo_timer (self, timeout)
 *
 *    Start todo timer. 
 *
 */
static void irttp_start_todo_timer( struct tsap_cb *self, int timeout)
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == TTP_TSAP_MAGIC, return;);

	del_timer( &self->todo_timer);
	
	self->todo_timer.data     = (unsigned long) self;
	self->todo_timer.function = &irttp_todo_expired;
	self->todo_timer.expires  = jiffies + timeout;
	
	add_timer( &self->todo_timer);
}

#ifdef CONFIG_PROC_FS
/*
 * Function irttp_proc_read (buf, start, offset, len, unused)
 *
 *    Give some info to the /proc file system
 */
int irttp_proc_read( char *buf, char **start, off_t offset, int len, 
		     int unused)
{
	struct tsap_cb *self;
	unsigned long flags;
	int i = 0;
	
	ASSERT( irttp != NULL, return 0;);
	
	len = 0;
	
	save_flags(flags);
	cli();

	self = ( struct tsap_cb *) hashbin_get_first( irttp->tsaps);
	while ( self != NULL) {
		if ( !self || self->magic != TTP_TSAP_MAGIC) {
			DEBUG( 0, "irttp_proc_read: bad ptr self\n");
			return len;
		}

		len += sprintf( buf+len, "TSAP %d, ", i++);
		len += sprintf( buf+len, "stsap_sel: %02x, ", 
				self->stsap_sel);
		len += sprintf( buf+len, "dtsap_sel: %02x\n", 
				self->dtsap_sel);
		len += sprintf( buf+len, "  connected: %s, ",
				self->connected? "TRUE":"FALSE");
		len += sprintf( buf+len, "avail credit: %d, ",
				self->avail_credit);
		len += sprintf( buf+len, "remote credit: %d, ",
				self->remote_credit);
		len += sprintf( buf+len, "send credit: %d\n",
				self->send_credit);
		len += sprintf( buf+len, "  tx packets: %d, ",
				self->stats.tx_packets);
		len += sprintf( buf+len, "rx packets: %d, ",
				self->stats.rx_packets);
		len += sprintf( buf+len, "tx_queue len: %d ", 
				skb_queue_len( &self->tx_queue));
		len += sprintf( buf+len, "rx_queue len: %d\n", 
				skb_queue_len( &self->rx_queue));
		len += sprintf( buf+len, "  tx_sdu_busy: %s, ",
				self->tx_sdu_busy? "TRUE":"FALSE");
		len += sprintf( buf+len, "rx_sdu_busy: %s\n",
				self->rx_sdu_busy? "TRUE":"FALSE");
		len += sprintf( buf+len, "  max_seg_size: %d, ",
				self->max_seg_size);
		len += sprintf( buf+len, "tx_max_sdu_size: %d, ",
				self->tx_max_sdu_size);
		len += sprintf( buf+len, "rx_max_sdu_size: %d\n",
				self->rx_max_sdu_size);

		len += sprintf( buf+len, "  Used by (%s)\n", 
				self->notify.name);

		len += sprintf( buf+len, "\n");
		
		self = ( struct tsap_cb *) hashbin_get_next( irttp->tsaps);
	}
	restore_flags(flags);

	return len;
}

#endif /* PROC_FS */
