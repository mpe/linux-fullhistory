/*********************************************************************
 *                
 * Filename:      irlan_eth.c
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Oct 15 08:37:58 1998
 * Modified at:   Thu Apr 22 14:26:39 1999
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
 *     Neither Dag Brattli nor University of Troms� admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/if_arp.h>
#include <net/arp.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irlan_common.h>
#include <net/irda/irlan_client.h>
#include <net/irda/irlan_event.h>
#include <net/irda/irlan_eth.h>

/*
 * Function irlan_eth_init (dev)
 *
 *    The network device initialization function.
 *
 */
int irlan_eth_init(struct device *dev)
{
	struct irmanager_event mgr_event;
	struct irlan_cb *self;

	DEBUG(0, __FUNCTION__"()\n");

	ASSERT(dev != NULL, return -1;);
       
	self = (struct irlan_cb *) dev->priv;

	dev->open               = irlan_eth_open;
	dev->stop               = irlan_eth_close;
	dev->hard_start_xmit    = irlan_eth_xmit; 
	dev->get_stats	        = irlan_eth_get_stats;
	dev->set_multicast_list = irlan_eth_set_multicast_list;

	dev->tbusy = 1;
	
	ether_setup(dev);
	
	dev->tx_queue_len = TTP_MAX_QUEUE;

#if 0
	/*  
	 *  OK, since we are emulating an IrLAN sever we will have to give
	 *  ourself an ethernet address!
	 *  FIXME: this must be more dynamically
	 */
	dev->dev_addr[0] = 0x40;
	dev->dev_addr[1] = 0x00;
	dev->dev_addr[2] = 0x00;
	dev->dev_addr[3] = 0x00;
	dev->dev_addr[4] = 0x23;
	dev->dev_addr[5] = 0x45;
#endif
	/* 
	 * Network device has now been registered, so tell irmanager about
	 * it, so it can be configured with network parameters
	 */
	mgr_event.event = EVENT_IRLAN_START;
	sprintf(mgr_event.devname, "%s", self->ifname);
	irmanager_notify(&mgr_event);

	/* 
	 * We set this so that we only notify once, since if 
	 * configuration of the network device fails, the user
	 * will have to sort it out first anyway. No need to 
	 * try again.
	 */
	self->notify_irmanager = FALSE;

	return 0;
}

/*
 * Function irlan_eth_open (dev)
 *
 *    Network device has been opened by user
 *
 */
int irlan_eth_open(struct device *dev)
{
	struct irlan_cb *self;
	
	DEBUG(0, __FUNCTION__ "()\n");

	ASSERT(dev != NULL, return -1;);

	self = (struct irlan_cb *) dev->priv;

	ASSERT(self != NULL, return -1;);

	/* Ready to play! */
/* 	dev->tbusy = 0; */ /* Wait until data link is ready */
	dev->interrupt = 0;
	dev->start = 1;

	self->notify_irmanager = TRUE;

	/* We are now open, so time to do some work */
	irlan_client_wakeup(self, self->saddr, self->daddr);

	irlan_mod_inc_use_count();
	
	return 0;
}

/*
 * Function irlan_eth_close (dev)
 *
 *    Stop the ether network device, his function will usually be called by
 *    ifconfig down. We should now disconnect the link, We start the 
 *    close timer, so that the instance will be removed if we are unable
 *    to discover the remote device after the disconnect.
 */
int irlan_eth_close(struct device *dev)
{
	struct irlan_cb *self = (struct irlan_cb *) dev->priv;

	DEBUG(0, __FUNCTION__ "()\n");
	
	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	irlan_mod_dec_use_count();

	irlan_close_data_channel(self);

	irlan_close_tsaps(self);

	irlan_do_client_event(self, IRLAN_LMP_DISCONNECT, NULL);
	irlan_do_provider_event(self, IRLAN_LMP_DISCONNECT, NULL);	
	
	irlan_start_watchdog_timer(self, IRLAN_TIMEOUT);

	/* Device closed by user! */
	if (self->notify_irmanager)
		self->notify_irmanager = FALSE;
	else
		self->notify_irmanager = TRUE;

	return 0;
}

/*
 * Function irlan_eth_tx (skb)
 *
 *    Transmits ethernet frames over IrDA link.
 *
 */
int irlan_eth_xmit(struct sk_buff *skb, struct device *dev)
{
	struct irlan_cb *self;

	DEBUG(4, __FUNCTION__ "()\n");
	
	self = (struct irlan_cb *) dev->priv;

	ASSERT(self != NULL, return 0;);
	ASSERT(self->magic == IRLAN_MAGIC, return 0;);

	/* Lock transmit buffer */
	if (irda_lock((void *) &dev->tbusy) == FALSE) {
		/*
		 * If we get here, some higher level has decided we are broken.
		 * There should really be a "kick me" function call instead.
		 */
		int tickssofar = jiffies - dev->trans_start; 
		
		if (tickssofar < 5) 
 			return -EBUSY;
		
 		dev->tbusy = 0;
 		dev->trans_start = jiffies;
	}
	
	DEBUG(4, "Room left at head: %d\n", skb_headroom(skb));
	DEBUG(4, "Room left at tail: %d\n", skb_tailroom(skb));
	DEBUG(4, "Required room: %d\n", IRLAN_MAX_HEADER);
	
	/* skb headroom large enough to contain IR-headers? */
	if ((skb_headroom(skb) < IRLAN_MAX_HEADER) || (skb_shared(skb))) {
		struct sk_buff *new_skb = 
			skb_realloc_headroom(skb, IRLAN_MAX_HEADER);
		ASSERT(new_skb != NULL, return 0;);
		ASSERT(skb_headroom(new_skb) >= IRLAN_MAX_HEADER, return 0;);

		/*  Free original skb, and use the new one */
		dev_kfree_skb(skb);
		skb = new_skb;
	} 

	dev->trans_start = jiffies;
	self->stats.tx_packets++;
	self->stats.tx_bytes += skb->len; 

	/*
	 *  Now queue the packet in the transport layer
	 *  FIXME: clean up the code below! DB
	 */
	if (self->use_udata) {
		irttp_udata_request(self->tsap_data, skb);
		dev->tbusy = 0;
	
		return 0;
	}

	if (irttp_data_request(self->tsap_data, skb) == -1) {
		/*  
		 *  IrTTPs tx queue is full, so we just have to drop the
		 *  frame! You might think that we should just return -1
		 *  and don't deallocate the frame, but that is dangerous
		 *  since it's possible that we have replaced the original
		 *  skb with a new one with larger headroom, and that would
		 *  really confuse do_dev_queue_xmit() in dev.c! I have
		 *  tried :-) DB
		 */
		dev_kfree_skb(skb);
		++self->stats.tx_dropped;
		
		return 0;
	}
	dev->tbusy = 0; /* Finished! */
	
	return 0;
}

/*
 * Function irlan_eth_receive (handle, skb)
 *
 *    This function gets the data that is received on the data channel
 *
 */
int irlan_eth_receive(void *instance, void *sap, struct sk_buff *skb)
{
	struct irlan_cb *self;

	self = (struct irlan_cb *) instance;

	ASSERT(self != NULL, return 0;);
	ASSERT(self->magic == IRLAN_MAGIC, return 0;);

	if (skb == NULL) {
		++self->stats.rx_dropped; 
		return 0;
	}
	ASSERT(skb->len > 1, return 0;);
		
	/* 
	 * Adopt this frame! Important to set all these fields since they 
	 * might have been previously set by the low level IrDA network
	 * device driver 
	 */
	skb->dev = &self->dev;
	skb->protocol=eth_type_trans(skb, skb->dev); /* Remove eth header */
	
	netif_rx(skb);   /* Eat it! */
	
	self->stats.rx_packets++;
	self->stats.rx_bytes += skb->len; 

	return 0;
}

/*
 * Function irlan_eth_flow (status)
 *
 *    Do flow control between IP/Ethernet and IrLAN/IrTTP. This is done by 
 *    controlling the dev->tbusy variable.
 */
void irlan_eth_flow_indication(void *instance, void *sap, LOCAL_FLOW flow)
{
	struct irlan_cb *self;
	struct device *dev;

	DEBUG(4, __FUNCTION__ "()\n");

	self = (struct irlan_cb *) instance;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	
	dev = &self->dev;

	ASSERT(dev != NULL, return;);
	
	switch (flow) {
	case FLOW_STOP:
		DEBUG(4, "IrLAN, stopping Ethernet layer\n");

		dev->tbusy = 1;
		break;
	case FLOW_START:
		/* 
		 *  Tell upper layers that its time to transmit frames again
		 */
		DEBUG(4, "IrLAN, starting Ethernet layer\n");

		dev->tbusy = 0;

		/* 
		 *  Ready to receive more frames, so schedule the network
		 *  layer
		 */
		mark_bh(NET_BH);		
		break;
	default:
		DEBUG(0, __FUNCTION__ "(), Unknown flow command!\n");
	}
}

/*
 * Function irlan_eth_rebuild_header (buff, dev, dest, skb)
 *
 *    If we don't want to use ARP. Currently not used!!
 *
 */
void irlan_eth_rebuild_header(void *buff, struct device *dev, 
			      unsigned long dest, struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *) buff;

	memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
	memcpy(eth->h_dest, dev->dev_addr, dev->addr_len);

	/* return 0; */
}

/*
 * Function irlan_etc_send_gratuitous_arp (dev)
 *
 *    Send gratuitous ARP to announce that we have changed
 *    hardware address, so that all peers updates their ARP tables
 */
void irlan_etc_send_gratuitous_arp(struct device *dev) 
{
	struct in_device *in_dev;

	/* 
	 * When we get a new MAC address do a gratuitous ARP. This
	 * is useful if we have changed access points on the same
	 * subnet.  
	 */
	DEBUG(4, "IrLAN: Sending gratuitous ARP\n");
	in_dev = dev->ip_ptr;
	arp_send(ARPOP_REQUEST, ETH_P_ARP, 
		 in_dev->ifa_list->ifa_address,
		 &dev, 
		 in_dev->ifa_list->ifa_address,
		 NULL, dev->dev_addr, NULL);
}

/*
 * Function set_multicast_list (dev)
 *
 *    Configure the filtering of the device
 *
 */
#define HW_MAX_ADDRS 4 /* Must query to get it! */
void irlan_eth_set_multicast_list(struct device *dev) 
{
 	struct irlan_cb *self;

 	self = dev->priv; 

	DEBUG(0, __FUNCTION__ "()\n");
	return;
 	ASSERT(self != NULL, return;); 
 	ASSERT(self->magic == IRLAN_MAGIC, return;);

	if (dev->flags&IFF_PROMISC) {
		/* Enable promiscuous mode */
		DEBUG(0, "Promiscous mode not implemented\n");
		/* outw(MULTICAST|PROMISC, ioaddr); */
	}
	else if ((dev->flags & IFF_ALLMULTI) || dev->mc_count > HW_MAX_ADDRS) {
		/* Disable promiscuous mode, use normal mode. */
		DEBUG(4, __FUNCTION__ "(), Setting multicast filter\n");
		/* hardware_set_filter(NULL); */

		irlan_set_multicast_filter(self, TRUE);
	}
	else if (dev->mc_count) {
		DEBUG(4, __FUNCTION__ "(), Setting multicast filter\n");
		/* Walk the address list, and load the filter */
		/* hardware_set_filter(dev->mc_list); */

		irlan_set_multicast_filter(self, TRUE);
	}
	else {
		DEBUG(4, __FUNCTION__ "(), Clearing multicast filter\n");
		irlan_set_multicast_filter(self, FALSE);
	}

	if (dev->flags & IFF_BROADCAST) {
		DEBUG(4, __FUNCTION__ "(), Setting broadcast filter\n");
		irlan_set_broadcast_filter(self, TRUE);
	} else {
		DEBUG(4, __FUNCTION__ "(), Clearing broadcast filter\n");
		irlan_set_broadcast_filter(self, FALSE);
	}
}

/*
 * Function irlan_get_stats (dev)
 *
 *    Get the current statistics for this device
 *
 */
struct enet_statistics *irlan_eth_get_stats(struct device *dev) 
{
	struct irlan_cb *self = (struct irlan_cb *) dev->priv;

	ASSERT(self != NULL, return NULL;);
	ASSERT(self->magic == IRLAN_MAGIC, return NULL;);

	return &self->stats;
}
