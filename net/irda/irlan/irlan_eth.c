/*********************************************************************
 *                
 * Filename:      irlan_eth.c
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Oct 15 08:37:58 1998
 * Modified at:   Wed Dec  9 11:14:53 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:       skeleton.c by Donald Becker <becker@CESDIS.gsfc.nasa.gov>
 *                slip.c by Laurence Culhane,   <loz@holmes.demon.co.uk>
 *                          Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 * 
 *     Copyright (c) 1998 Dag Brattli, All Rights Reserved.
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

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <net/arp.h>

#include <net/irda/irda.h>
#include <net/irda/irlan_common.h>
#include <net/irda/irlan_eth.h>

/*
 * Function irlan_eth_init (dev)
 *
 *    The network device initialization function. Called only once.
 *
 */
int irlan_eth_init( struct device *dev)
{
	struct irlan_cb *self;

	DEBUG( 4, __FUNCTION__"()\n");

	ASSERT( dev != NULL, return -1;);
       
	self = (struct irlan_cb *) dev->priv;

/*  	dev->open               = irlan_eth_open;  */
/* 	dev->stop	        = irlan_eth_close; */

	dev->hard_start_xmit    = irlan_eth_tx; 
	dev->get_stats	        = irlan_eth_get_stats;
	dev->set_multicast_list = irlan_eth_set_multicast_list;

	dev->tbusy = 1;
	
	ether_setup( dev);
	
	dev->tx_queue_len = TTP_MAX_QUEUE;

	return 0;
}

/*
 * Function irlan_eth_open (dev)
 *
 *    Start the IrLAN ether network device, this function will be called by
 *    "ifconfig irlan0 up".
 *
 */
int irlan_eth_open( struct device *dev)
{
	/* struct irlan_cb *self = (struct irlan_cb *) dev->priv; */
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( dev != NULL, return -1;);

	/* Ready to play! */
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/* MOD_INC_USE_COUNT; */
	
	return 0;
}

/*
 * Function irlan_eth_close (dev)
 *
 *    Stop the Client ether network device, his function will be called by
 *    ifconfig down.
 */
int irlan_eth_close(struct device *dev)
{
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( dev != NULL, return -1;);
	
	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	/* MOD_DEC_USE_COUNT; */
	
	return 0;
}

/*
 * Function irlan_eth_tx (skb)
 *
 *    Transmits ethernet frames over IrDA link.
 *
 */
int irlan_eth_tx( struct sk_buff *skb, struct device *dev)
{
	struct irlan_cb *self;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	self = (struct irlan_cb *) dev->priv;

	ASSERT( self != NULL, return 0;);
	ASSERT( self->magic == IRLAN_MAGIC, return 0;);

	if ( dev->tbusy) {
		/*
		 * If we get here, some higher level has decided we are broken.
		 * There should really be a "kick me" function call instead.
		 */
		int tickssofar = jiffies - dev->trans_start; 
		DEBUG( 4, __FUNCTION__ "(), tbusy==TRUE\n");
		
		if ( tickssofar < 5) 
 			return -EBUSY;
		
 		dev->tbusy = 0;
 		dev->trans_start = jiffies;
	}
	/*
	 * If some higher layer thinks we've missed an tx-done interrupt
	 * we are passed NULL. Caution: dev_tint() handles the cli()/sti()
	 * itself.
	 */
	if ( skb == NULL) {
		DEBUG( 0, __FUNCTION__ "(), skb==NULL\n");

		return 0;
	}
	/*
	 *  Check that we are connected
         */
	if ( !self->connected) {
		DEBUG( 4, __FUNCTION__ "(), Not connected, dropping frame!\n");

		dev_kfree_skb( skb);
                ++self->stats.tx_dropped;
		
                return 0;
	}
	
	/*
	 * Block a timer-based transmit from overlapping. This could better be
	 * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	 */
	if ( test_and_set_bit(0, (void*) &dev->tbusy) != 0) {
 		printk( KERN_WARNING "%s: Transmitter access conflict.\n", 
			dev->name);
		return 0;
	}	
	DEBUG( 4, "Room left at head: %d\n", skb_headroom(skb));
	DEBUG( 4, "Room left at tail: %d\n", skb_tailroom(skb));
	DEBUG( 4, "Required room: %d\n", IRLAN_MAX_HEADER);
	
	/* Skb headroom large enough to contain IR-headers? */
	if (( skb_headroom( skb) < IRLAN_MAX_HEADER) || ( skb_shared( skb))) {
		struct sk_buff *new_skb = 
			skb_realloc_headroom(skb, IRLAN_MAX_HEADER);
		ASSERT( new_skb != NULL, return 0;);
		ASSERT( skb_headroom( new_skb) >= IRLAN_MAX_HEADER, return 0;);

		/*  Free original skb, and use the new one */
		dev_kfree_skb( skb);
		skb = new_skb;
	} 

	dev->trans_start = jiffies;
	self->stats.tx_packets++;
	self->stats.tx_bytes += skb->len; 

	/*
	 *  Now queue the packet in the transport layer
	 *  FIXME: clean up the code below! DB
	 */
	if ( self->use_udata) {
		irttp_udata_request( self->tsap_data, skb);
		dev->tbusy = 0;
	
		return 0;
	}

	if ( irttp_data_request( self->tsap_data, skb) == -1) {
		/*  
		 *  IrTTPs tx queue is full, so we just have to drop the
		 *  frame! You might think that we should just return -1
		 *  and don't deallocate the frame, but that is dangerous
		 *  since it's possible that we have replaced the original
		 *  skb with a new one with larger headroom, and that would
		 *  really confuse do_dev_queue_xmit() in dev.c! I have
		 *  tried :-) DB
		 */
		DEBUG( 4, __FUNCTION__ "(), Dropping frame\n");
		dev_kfree_skb( skb);
		++self->stats.tx_dropped;
		
		return 0;
	}
	dev->tbusy = 0;
	
	return 0;
}

/*
 * Function irlan_eth_rx (handle, skb)
 *
 *    This function gets the data that is received on the data channel
 *
 */
void irlan_eth_rx( void *instance, void *sap, struct sk_buff *skb)
{
	struct irlan_cb *self;

	self = ( struct irlan_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	if (skb == NULL) {
		++self->stats.rx_dropped; 
		return;
	}
	IS_SKB( skb, return;);
	ASSERT( skb->len > 1, return;);
		
	DEBUG( 4, "Got some ether data: length=%d\n", (int)skb->len); 
	
	/* 
	 * Adopt this frame! Important to set all these fields since they 
	 * might have been previously set by the low level IrDA network
	 * device driver 
	 */
	skb->dev = &self->dev;
	skb->protocol=eth_type_trans( skb, skb->dev); /* Remove eth header */
	
	netif_rx( skb);   /* Eat it! */
	
	self->stats.rx_packets++;
	self->stats.rx_bytes += skb->len; 
}

/*
 * Function irlan_eth_flow (status)
 *
 *    Do flow control between IP/Ethernet and IrLAN/IrTTP. This is done by 
 *    controlling the dev->tbusy variable.
 */
void irlan_eth_flow_indication( void *instance, void *sap, LOCAL_FLOW flow)
{
	struct irlan_cb *self;
	struct device *dev;

	DEBUG( 4, __FUNCTION__ "()\n");

	self = (struct irlan_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	dev = &self->dev;

	ASSERT( dev != NULL, return;);
	
	switch ( flow) {
	case FLOW_STOP:
		DEBUG( 4, "IrLAN, stopping Ethernet layer\n");

		dev->tbusy = 1;
		break;
	case FLOW_START:
		/* 
		 *  Tell upper layers that its time to transmit frames again
		 */
		DEBUG( 4, "IrLAN, starting Ethernet layer\n");

		dev->tbusy = 0;

		/* 
		 *  Ready to receive more frames, so schedule the network
		 *  layer
		 */
		mark_bh( NET_BH);		
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown flow command!\n");
	}
}

/*
 * Function irlan_eth_rebuild_header (buff, dev, dest, skb)
 *
 *    If we don't want to use ARP. Currently not used!!
 *
 */
void irlan_eth_rebuild_header( void *buff, struct device *dev, 
			       unsigned long dest, struct sk_buff *skb)
{
	struct ethhdr *eth = ( struct ethhdr *) buff;

	memcpy( eth->h_source, dev->dev_addr, dev->addr_len);
	memcpy( eth->h_dest, dev->dev_addr, dev->addr_len);

	/* return 0; */
}

/*
 * Function set_multicast_list (dev)
 *
 *    Configure the filtering of the device
 *
 */
#define HW_MAX_ADDRS 4 /* Must query to get it! */
void irlan_eth_set_multicast_list( struct device *dev) 
{
 	struct irlan_cb *self;

 	self = dev->priv; 

	DEBUG( 4, __FUNCTION__ "()\n");

 	ASSERT( self != NULL, return;); 
 	ASSERT( self->magic == IRLAN_MAGIC, return;);

	if (dev->flags&IFF_PROMISC) {
		/* Enable promiscuous mode */
		DEBUG( 0, "Promiscous mode not implemented\n");
		/* outw(MULTICAST|PROMISC, ioaddr); */
	}
	else if ((dev->flags&IFF_ALLMULTI) || dev->mc_count > HW_MAX_ADDRS) {
		/* Disable promiscuous mode, use normal mode. */
		DEBUG( 4, __FUNCTION__ "(), Setting multicast filter\n");
		/* hardware_set_filter(NULL); */

		irlan_set_multicast_filter( self, TRUE);
	}
	else if (dev->mc_count) {
		DEBUG( 4, __FUNCTION__ "(), Setting multicast filter\n");
		/* Walk the address list, and load the filter */
		/* hardware_set_filter(dev->mc_list); */

		irlan_set_multicast_filter( self, TRUE);
	}
	else {
		DEBUG( 4, __FUNCTION__ "(), Clearing multicast filter\n");
		irlan_set_multicast_filter( self, FALSE);
	}

	if ( dev->flags & IFF_BROADCAST) {
		DEBUG( 4, __FUNCTION__ "(), Setting broadcast filter\n");
		irlan_set_broadcast_filter( self, TRUE);
	} else {
		DEBUG( 4, __FUNCTION__ "(), Clearing broadcast filter\n");
		irlan_set_broadcast_filter( self, FALSE);
	}
}

/*
 * Function irlan_get_stats (dev)
 *
 *    Get the current statistics for this device
 *
 */
struct enet_statistics *irlan_eth_get_stats( struct device *dev) 
{
	struct irlan_cb *self = (struct irlan_cb *) dev->priv;

	ASSERT( self != NULL, return NULL;);
	ASSERT( self->magic == IRLAN_MAGIC, return NULL;);

	return &self->stats;
}
