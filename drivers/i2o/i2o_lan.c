/*
 * 	linux/drivers/i2o/i2o_lan.c
 *
 *    	I2O LAN CLASS OSM 	Prototyping, May 7th 1999
 *
 *	(C) Copyright 1999 	University of Helsinki,
 *				Department of Computer Science
 *
 *   	This code is still under development / test.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.    
 *
 * 	Author: 	Auvo Häkkinen <Auvo.Hakkinen@cs.Helsinki.FI>
 *
 *	Tested:		in FDDI environment (using SysKonnect's DDM)
 *			in ETH environment (using Intel 82558 DDM proto)
 *
 *	TODO:		batch mode networking
 *			- this one assumes that we always get one packet in a bucket
 *			- we've not been able to test batch replies and batch receives
 *			error checking / timeouts
 *			- code/test for other LAN classes
 */

#include <linux/module.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/fddidevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/malloc.h>
#include <linux/trdevice.h>
#include <asm/io.h>

#include <linux/errno.h>

#include <linux/i2o.h>
#include "i2o_lan.h"

//#define DRIVERDEBUG
#ifdef DRIVERDEBUG
#define dprintk(s, args...) printk(s, ## args) 
#else
#define dprintk(s, args...)
#endif

#define MAX_LAN_CARDS 4
static struct device *i2o_landevs[MAX_LAN_CARDS+1];
static int unit = -1; 			/* device unit number */

struct i2o_lan_local {
	u8 unit;
	struct i2o_device *i2o_dev;
	int reply_flag; 		// needed by scalar/table queries
	struct fddi_statistics stats;
/*	first fields are same as in struct net_device_stats stats; */ 
	unsigned short (*type_trans)(struct sk_buff *, struct device *);
};

/* function prototypes */
static int i2o_lan_receive_post(struct device *dev);
static int i2o_lan_receive_post_reply(struct device *dev, struct i2o_message *m);


static void i2o_lan_reply(struct i2o_handler *h, struct i2o_controller *iop, 
			  struct i2o_message *m)
{  
	u32 *msg = (u32 *)m;
	u8 unit  = (u8)(msg[2]>>16); // InitiatorContext
	struct device *dev = i2o_landevs[unit];

#ifdef DRIVERDEBUG
	i2o_report_status(KERN_INFO, "i2o_lan", msg[1]>>24, msg[4]>>24,
			  msg[4]&0xFFFF);
#endif
    	if (msg[0] & (1<<13)) // Fail bit is set
 	{
 		printk(KERN_INFO "IOP failed to process the msg\n");
		printk("From tid=%d to tid=%d",(msg[1]>>12)&0xFFF,msg[1]&0xFFF);
		return;
	}	

	switch (msg[1] >> 24) {
	case LAN_RECEIVE_POST: 
		if (dev->start) 
			i2o_lan_receive_post_reply(dev,m);
		else { 
			// we are getting unused buckets back
			u8 trl_count  = msg[3] & 0x000000FF; 	
			struct i2o_bucket_descriptor *bucket =
				(struct i2o_bucket_descriptor *)&msg[6];
			struct sk_buff *skb;
			do {	
				dprintk("Releasing unused bucket\n");
				skb = (struct sk_buff *)bucket->context;
				dev_kfree_skb(skb);
				bucket++;
			} while (--trl_count);
		}			
	break;

	case LAN_PACKET_SEND:
	case LAN_SDU_SEND: 
	{
		u8 trl_count  = msg[3] & 0x000000FF; 	
		
		if (msg[4] >> 24) 	// ReqStatus != SUCCESS
		{
			printk(KERN_WARNING "%s: ",dev->name); 
			report_common_status(msg[4]>>24);
			report_lan_dsc(msg[4]&0xFFFF);
		}

		do {	// The HDM has handled the outgoing packet
			dev_kfree_skb((struct sk_buff *)msg[4 + trl_count]);
			dprintk(KERN_INFO "%s: Request skb freed (trl_count=%d).\n",
				dev->name,trl_count);
		} while (--trl_count);
		
			dev->tbusy = 0;
			mark_bh(NET_BH); /* inform upper layers */
	}
	break;	

	default: 
		if (msg[2] & 0x80000000)  	// reply to a util get/set
		{	// flag for the i2o_post_wait
	       		int *flag = (int *)msg[3];
	       		// ReqStatus != I2O_REPLY_STATUS_SUCCESS
	      		 *flag = (msg[4] >> 24) ? I2O_POST_WAIT_TIMEOUT 
	       				        : I2O_POST_WAIT_OK ;
		}
	}
}

static struct i2o_handler i2o_lan_handler =
{
	i2o_lan_reply,
	"I2O Lan OSM",
	0		// context
};
static int lan_context;


static int i2o_lan_receive_post_reply(struct device *dev, struct i2o_message *m)
{
	u32 *msg = (u32 *)m;
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;
	struct i2o_bucket_descriptor *bucket = (struct i2o_bucket_descriptor *)&msg[6];
	struct i2o_packet_info *packet;
	
	u8 trl_count  = msg[3] & 0x000000FF; 	
	struct sk_buff *skb;

#ifdef 0
	dprintk(KERN_INFO "TrlFlags = 0x%02X, TrlElementSize = %d, TrlCount = %d\n"
		"msgsize = %d, buckets_remaining = %d\n", 
		msg[3]>>24, msg[3]&0x0000FF00, trl_count, msg[0]>>16, msg[5]);	
#endif

/* 
 * NOTE: here we assume that also in batch mode we will get only 
 * one packet per bucket. This can be ensured by setting the 
 * PacketOrphanLimit to MaxPacketSize, as well as the bucket size.
 */
	do {
		/* packet is not at all needed here */
		packet = (struct i2o_packet_info *)bucket->packet_info;	
#ifdef 0
		dprintk(KERN_INFO "flags = 0x%02X, offset = 0x%06X, status = 0x%02X, length = %d\n",
			packet->flags, packet->offset, packet->status, packet->len);
#endif
		skb = (struct sk_buff *)(bucket->context);
		skb_put(skb,packet->len);
		skb->dev  = dev;		
		skb->protocol = priv->type_trans(skb, dev);
		netif_rx(skb);

		dprintk(KERN_INFO "%s: Incoming packet (%d bytes) delivered "
			"to upper level.\n",dev->name,packet->len);
			
		bucket++; // to next Packet Descriptor Block

	} while (--trl_count);

	if (msg[5] <= I2O_BUCKET_THRESH)  	// BucketsRemaining
		i2o_lan_receive_post(dev);

	return 0;
}

/*  ====================================================
 *  Interface to i2o:  functions to send lan class request 
 */

/* 
 * i2o_lan_receive_post(): Post buckets to receive packets.
 */
static int i2o_lan_receive_post(struct device *dev)
{	
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;
	struct sk_buff *skb;
	u32 m; u32 *msg;
	
	u32 bucket_len = (dev->mtu + dev->hard_header_len);
	u32 bucket_count;
	int n_elems = (iop->inbound_size - 16 ) / 12; // msg header + SGLs
	u32 total = 0;
	int i;

	dprintk(KERN_INFO "%s: Allocating %d buckets (size %d).\n", 
		dev->name, I2O_BUCKET_COUNT, bucket_len);

	while (total < I2O_BUCKET_COUNT)
	{
		m = I2O_POST_READ32(iop);
		if (m == 0xFFFFFFFF)
			return -ETIMEDOUT;		
		msg = bus_to_virt(iop->mem_offset + m);
	
		bucket_count = (total + n_elems < I2O_BUCKET_COUNT)
			     ? n_elems
			     : I2O_BUCKET_COUNT - total;
			     
		msg[0] = I2O_MESSAGE_SIZE(4 + 3 *  bucket_count) | 1<<12 | SGL_OFFSET_4;
		msg[1] = LAN_RECEIVE_POST<<24 | HOST_TID<<12 | i2o_dev->id;
		msg[2] = priv->unit << 16 | lan_context; // InitiatorContext	
		msg[3] = bucket_count;			 // BucketCount

		for (i = 0; i < bucket_count; i++)
		{
			skb = dev_alloc_skb(bucket_len + 2);
			if (skb == NULL)
				return -ENOMEM;
			skb_reserve(skb, 2);
			msg[4 + 3*i] = 0x51000000 | bucket_len;
			msg[5 + 3*i] = (u32)skb;
			msg[6 + 3*i] = virt_to_bus(skb->data);
		}
		msg[4 + 3*i - 3] |= 0x80000000; // set LE flag
		i2o_post_message(iop,m);

		dprintk(KERN_INFO "%s: Sending %d buckets (size %d) to LAN HDM.\n",
			dev->name,bucket_count,bucket_len);

		total += bucket_count;
	}
	return 0;	
}	

/* 
 * i2o_lan_reset(): Reset the LAN adapter into the operational state and 
 * 	restore it to full operation.
 */
static int i2o_lan_reset(struct device *dev) 
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;	
	u32 m; u32 *msg;

	m = I2O_POST_READ32(iop);
	if (m == 0xFFFFFFFF)
		return -ETIMEDOUT;
	msg = bus_to_virt(iop->mem_offset + m);

	msg[0] = FIVE_WORD_MSG_SIZE | SGL_OFFSET_0;
	msg[1] = LAN_RESET<<24 | HOST_TID<<12 | i2o_dev->id;
	msg[2] = priv->unit << 16 | lan_context; // InitiatorContext
	msg[3] = 0; 				 // TransactionContext
	msg[4] = 1 << 16; 			 // return posted buckets

	i2o_post_message(iop,m);

	return 0; 
}

/* 
 * i2o_lan_suspend(): Put LAN adapter into a safe, non-active state.
 * 	Reply to any LAN class message with status error_no_data_transfer 
 *	/ suspended.
 */
static int i2o_lan_suspend(struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;	
	u32 m; u32 *msg;

	m = I2O_POST_READ32(iop);
	if (m == 0xFFFFFFFF)
		return -ETIMEDOUT;
	msg = bus_to_virt(iop->mem_offset + m);

	msg[0] = FIVE_WORD_MSG_SIZE | SGL_OFFSET_0;
	msg[1] = LAN_SUSPEND<<24 | HOST_TID<<12 | i2o_dev->id;
	msg[2] = priv->unit << 16 | lan_context; // InitiatorContext
	msg[3] = 0; 				 // TransactionContext
	msg[4] = 1 << 16; 			 // return posted buckets

	i2o_post_message(iop,m);

	return 0;
}

/*
 * Set DDM into batch mode.
 */
static void i2o_set_batch_mode(struct device *dev)
{

/* 
 * NOTE: we have not been able to test batch mode
 * 	 since HDMs we have, don't implement it 
 */

	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;	
	struct i2o_controller *iop = i2o_dev->controller;	
	u32 val;

	/* set LAN_BATCH_CONTROL attributes */

	// enable batch mode, toggle automatically
	val = 0x00000000;
	if (i2o_params_set(iop, i2o_dev->id, lan_context, 0x0003, 0,
			&val, 4, &priv->reply_flag) <0)
		printk(KERN_WARNING "Unable to enter I2O LAN batch mode.\n");
	else
		dprintk(KERN_INFO "%s: I2O LAN batch mode enabled.\n",dev->name);

	/*
	 * When PacketOrphanlimit is same as the maximum packet length,
	 * the packets will never be split into two separate buckets
	 */

	/* set LAN_OPERATION attributes */

	val = dev->mtu + dev->hard_header_len; // PacketOrphanLimit
	if (i2o_params_set(iop, i2o_dev->id, lan_context, 0x0004, 2,
		&val, 4, &priv->reply_flag) < 0)
		printk(KERN_WARNING "i2o_lan: Unable to set PacketOrphanLimit.\n");
	else
		dprintk(KERN_INFO "PacketOrphanLimit set to %d\n",val);

#ifdef 0
/*
 * I2O spec 2.0: there should be proper default values for other attributes 
 * used in batch mode.
 */

	/* set LAN_RECEIVE_INFO attributes */
	
	val = 10; // RxMaxBucketsReply
	if (i2o_params_set(iop, i2o_dev->id, lan_context, 0x0008, 3,
		&val, 4, &priv->reply_flag) < 0)
		printk(KERN_WARNING "%s: Unable to set RxMaxBucketsReply.\n",
			dev->name);

	val = 10; // RxMaxPacketsBuckets
	if (i2o_params_set(iop, i2o_dev->id, lan_context, 0x0008, 4,
		&val, 4, &priv->reply_flag) < 0)
		printk(KERN_WARNING "%s: Unable to set RxMaxPacketsBucket.\n",
			dev->name);

	/* set LAN_BATCH_CONTROL attributes */

	val = 10; // MaxRxBatchCount
	if (i2o_params_set(iop, i2o_dev->id, lan_context, 0x0003, 5,
		&val, 4, &priv->reply_flag) < 0)
		printk(KERN_WARNING "%s: Unable to set MaxRxBatchCount.\n",
			dev->name);

	val = 10; // MaxTxBatchCount
	if (i2o_params_set(iop, i2o_dev->id, lan_context, 0x0003, 8,
		&val, 4, &priv->reply_flag) < 0)
		printk(KERN_WARNING "%s Unable to set MaxTxBatchCount.\n",
			dev->name);
#endif

	return;	
}

/* 
 * i2o_lan_open(): Open the device to send/receive packets via 
 * the network device.	
 */
static int i2o_lan_open(struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;	
	struct i2o_controller *iop = i2o_dev->controller;

	i2o_lan_reset(dev);
	
	if (i2o_issue_claim(iop, i2o_dev->id, lan_context, 1, 
		&priv->reply_flag) < 0)
	{
		printk(KERN_WARNING "%s: Unable to claim the I2O LAN device.\n", dev->name);
		return -EAGAIN;
	}
	dprintk(KERN_INFO "%s: I2O LAN device claimed (tid=%d).\n", dev->name, i2o_dev->id);

	dev->tbusy = 0;
	dev->start = 1;

	i2o_set_batch_mode(dev);
	i2o_lan_receive_post(dev);

	MOD_INC_USE_COUNT;
	
	return 0;
}

/*
 * i2o_lan_close(): End the transfering.
 */ 
static int i2o_lan_close(struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;	
	struct i2o_controller *iop = i2o_dev->controller;	

	dev->tbusy = 1;
	dev->start = 0;

	if (i2o_issue_claim(iop, i2o_dev->id, lan_context, 0, 
	    &priv->reply_flag) < 0)
	{
		printk(KERN_WARNING "%s: Unable to unclaim I2O LAN device (tid=%d)\n",
		       dev->name, i2o_dev->id);
	}

	i2o_lan_suspend(dev);

	MOD_DEC_USE_COUNT;

	return 0;
}

/* 
 * i2o_lan_sdu_send(): Send a packet, MAC header added by the HDM.
 * Must be supported by Fibre Channel, optional for Ethernet/802.3, 
 * Token Ring, FDDI
 */
static int i2o_lan_sdu_send(struct sk_buff *skb, struct device *dev)
{	
#ifdef 0
/* not yet tested */
        struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv; 
        struct i2o_device *i2o_dev = priv->i2o_dev;     
        struct i2o_controller *iop = i2o_dev->controller;
        u32 m; u32 *msg;

	dprintk(KERN_INFO "LanSDUSend called, skb->len = %d\n", skb->len);

        m = *iop->post_port;
        if (m == 0xFFFFFFFF)
	{
                dev_kfree_skb(skb);     
                return -1;
        }
        msg = bus_to_virt(iop->mem_offset + m);

        msg[0] = NINE_WORD_MSG_SIZE | SGL_OFFSET_4;
        msg[1] = LAN_SDU_SEND<<24 | HOST_TID<<12 | i2o_dev->id; 
        msg[2] = priv->unit << 16 | lan_context; // IntiatorContext
        msg[3] = 1<<4;		// TransmitControlWord: suppress CRC generation

        // create a simple SGL, see fig. 3-26
        // D7 = 1101 0111 = LE eob 0 1 LA dir bc1 bc0

        msg[4] = 0xD7000000 | (skb->len);  // no MAC hdr included     
        msg[5] = (u32)skb;		   // TransactionContext
        memcpy(&msg[6], skb->data, 8);	   // Destination MAC Addr ??
        msg[7] &= 0x0000FFFF;		   // followed by two bytes zeros
        msg[8] = virt_to_bus(skb->data);
        dev->trans_start = jiffies;
        i2o_post_message(iop,m);

	dprintk(KERN_INFO "%s: Packet (%d bytes) sent to network.\n",
		dev->name,skb->len);
#endif
        return 0;
}

/* 
 * i2o_lan_packet_send(): Send a packet as is, including the MAC header.
 * 
 * Must be supported by Ethernet/802.3, Token Ring, FDDI, optional for 
 * Fibre Channel
 */ 
static int i2o_lan_packet_send(struct sk_buff *skb, struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;	
	struct i2o_controller *iop = i2o_dev->controller;
	u32 m; u32 *msg;

	m = *iop->post_port;
	if (m == 0xFFFFFFFF) {
		dev_kfree_skb(skb);
		return -1;
	}

	msg = bus_to_virt(iop->mem_offset + m);

	msg[0] = SEVEN_WORD_MSG_SIZE | 1<<12 | SGL_OFFSET_4;
	msg[1] = LAN_PACKET_SEND<<24 | HOST_TID<<12 | i2o_dev->id;	
	msg[2] = priv->unit << 16 | lan_context; // IntiatorContext
	msg[3] = 1 << 4; 			 // TransmitControlWord
	
	// create a simple SGL, see fig. 3-26
	// D5 = 1101 0101 = LE eob 0 1 LA dir bc1 bc0

	msg[4] = 0xD5000000 | skb->len;		// MAC hdr included
	msg[5] = (u32)skb; 			// TransactionContext
	msg[6] = virt_to_bus(skb->data);

	i2o_post_message(iop,m);

	dprintk(KERN_INFO "%s: Packet (%d bytes) sent to network.\n",
		dev->name, skb->len);

	return 0;
}

/*
 * net_device_stats(): Return statistical information.
 */
static struct net_device_stats *i2o_lan_get_stats(struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;
	u64 val[16];

	/* query LAN_HISTORICAL_STATS scalar parameter group 0x0100 */

        i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0100, -1, 
        		 &val, 16*8, &priv->reply_flag);
        priv->stats.tx_packets = val[0];
        priv->stats.tx_bytes   = val[1];
        priv->stats.rx_packets = val[2];
        priv->stats.rx_bytes   = val[3];
        priv->stats.tx_errors  = val[4];
        priv->stats.rx_errors  = val[5];
	priv->stats.rx_dropped = val[6];

	// other net_device_stats and FDDI class specific fields follow ...

	return (struct net_device_stats *)&priv->stats;
}
 
/*
 * i2o_lan_set_multicast_list(): Enable a network device to receive packets
 *	not send to the protocol address.
 */
static void i2o_lan_set_multicast_list(struct device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;
	u32 filter_mask;

	dprintk(KERN_INFO "Entered i2o_lan_set_multicast_list().\n");

return;

/*
 * FIXME: For some reason this kills interrupt handler in i2o_post_wait :-(
 * 
 */
	dprintk(KERN_INFO "dev->flags = 0x%08X, dev->mc_count = 0x%08X\n",
		dev->flags,dev->mc_count);

	if (i2o_query_scalar(iop, i2o_dev->id, lan_context, 0x0001, 3, 
			     &filter_mask, 4, &priv->reply_flag) < 0 )
		printk(KERN_WARNING "i2o_lan: Unable to query filter mask.\n");	

	dprintk(KERN_INFO "filter_mask = 0x%08X\n",filter_mask);

	if (dev->flags & IFF_PROMISC)
	{
        	// Enable promiscuous mode

		filter_mask |= 0x00000002; 
		if (i2o_params_set(iop, i2o_dev->id, lan_context, 0x0001, 3,
				&filter_mask, 4, &priv->reply_flag) <0)
			printk(KERN_WARNING "i2o_lan: Unable to enable promiscuous multicast mode.\n");
		else
			dprintk(KERN_INFO "i2o_lan: Promiscuous multicast mode enabled.\n");

		return;
        }
        
// 	if ((dev->flags & IFF_ALLMULTI) || dev->mc_count > HW_MAX_ADDRS)
// 	{
//        	// Disable promiscuous mode, use normal mode.
// 		hardware_set_filter(NULL);
//
//		dprintk(KERN_INFO "i2o_lan: Disabled promiscuous mode, uses normal mode\n"); 
//
//		filter_mask = 0x00000000; 
//		i2o_params_set(iop, i2o_dev->id, lan_context, 0x0001, 3,
//				&filter_mask, 4, &priv->reply_flag);
//		
//		return;
//      }

        if (dev->mc_count)
	{
        	// Walk the address list, and load the filter
//              hardware_set_filter(dev->mc_list);

                filter_mask = 0x00000004; 
		if (i2o_params_set(iop, i2o_dev->id, lan_context, 0x0001, 3,
				   &filter_mask, 4, &priv->reply_flag) <0)
			printk(KERN_WARNING "i2o_lan: Unable to enable Promiscuous multicast mode.\n");
		else
			dprintk(KERN_INFO "i2o_lan: Promiscuous multicast mode enabled.\n");			
        
       		return;
	}
        
        // Unicast
        
        filter_mask |= 0x00000300; // Broadcast, Multicast disabled
	if (i2o_params_set(iop, i2o_dev->id, lan_context, 0x0001, 3,
			&filter_mask, 4, &priv->reply_flag) <0)
		printk(KERN_WARNING "i2o_lan: Unable to enable unicast mode.\n");
	else
		dprintk(KERN_INFO "i2o_lan: Unicast mode enabled.\n");	

	return;
}

struct device *i2o_lan_register_device(struct i2o_device *i2o_dev)
{
	struct device *dev = NULL;
	struct i2o_lan_local *priv = NULL;
	u8 hw_addr[8];
	unsigned short (*type_trans)(struct sk_buff *, struct device *);

	switch (i2o_dev->subclass)
	{
	case I2O_LAN_ETHERNET:
		/* Note: init_etherdev calls 
			 ether_setup() and register_netdevice() 
			 and allocates the priv structure	 */

        	dev = init_etherdev(NULL, sizeof(struct i2o_lan_local));
		if (dev == NULL)
			return NULL;
		type_trans = eth_type_trans;
		break;

/*
#ifdef CONFIG_ANYLAN
	case I2O_LAN_100VG:
		printk(KERN_WARNING "i2o_lan: 100base VG not yet supported\n");
		break;
#endif
*/

#ifdef CONFIG_TR
	case I2O_LAN_TR:
		dev = init_trdev(NULL, sizeof(struct i2o_lan_local));
		if(dev==NULL)
			return NULL;
		type_trans = tr_type_trans;
		break;
#endif

#ifdef CONFIG_FDDI
	case I2O_LAN_FDDI:
	{
		int size = sizeof(struct device) + sizeof(struct i2o_lan_local)
			   + sizeof("fddi%d ");

        	dev = (struct device *) kmalloc(size, GFP_KERNEL);
        	memset((char *)dev, 0, size);
            	dev->priv = (void *)(dev + 1);
                dev->name = (char *)(dev + 1) + sizeof(struct i2o_lan_local);

		if (dev_alloc_name(dev,"fddi%d") < 0)
		{
			printk(KERN_WARNING "i2o_lan: Too many FDDI devices.\n");
			kfree(dev);
			return NULL;
		}
		type_trans = fddi_type_trans;

		fddi_setup(dev);
		register_netdev(dev);
      	}
	break;
#endif

/*
#ifdef CONFIG_FIBRE_CHANNEL
	case I2O_LAN_FIBRE_CHANNEL:
		printk(KERN_WARNING "i2o_lan: Fibre Channel not yet supported\n");
	break;
#endif
*/
	case I2O_LAN_UNKNOWN:
	default:
		printk(KERN_WARNING "i2o_lan: LAN type 0x%08X not supported\n",
		       i2o_dev->subclass);
		return NULL;
	}

	priv = (struct i2o_lan_local *)dev->priv;
	priv->i2o_dev = i2o_dev;
	priv->type_trans = type_trans;

        if (i2o_query_scalar(i2o_dev->controller, i2o_dev->id, lan_context,
			     0x0001, 0, &hw_addr, 8, &priv->reply_flag) < 0)
	{
        	printk("%s: Unable to query hardware address.\n",
		       dev->name);
      		return NULL;  
        }
        
	dprintk("%s hwaddr = %02X:%02X:%02X:%02X:%02X:%02X\n",
      		dev->name,hw_addr[0], hw_addr[1], hw_addr[2], hw_addr[3],
      		hw_addr[4], hw_addr[5]);

	dev->addr_len = 6;
	memcpy(dev->dev_addr, hw_addr, 6);

	dev->open               = i2o_lan_open;
	dev->stop               = i2o_lan_close;
	dev->hard_start_xmit    = i2o_lan_packet_send;
	dev->get_stats          = i2o_lan_get_stats;
	dev->set_multicast_list = i2o_lan_set_multicast_list;

	return dev;
}

#ifdef MODULE

int init_module(void)
{
	struct device *dev;
	struct i2o_lan_local *priv;
        int i;

        if (i2o_install_handler(&i2o_lan_handler) < 0)
	{
 		printk(KERN_ERR "Unable to register I2O LAN OSM.\n");
		return -EINVAL;
        }   

	lan_context = i2o_lan_handler.context;

        for (i=0; i < MAX_I2O_CONTROLLERS; i++)
	{
                struct i2o_controller *iop = i2o_find_controller(i);
                struct i2o_device *i2o_dev;

                if (iop==NULL)
			continue;

                for (i2o_dev=iop->devices;i2o_dev != NULL;i2o_dev=i2o_dev->next)
		{
                        int class = i2o_dev->class;

                        if (class != 0x020) /* not I2O_CLASS_LAN device*/ 
                                continue;

			if (unit == MAX_LAN_CARDS)
			{
				printk(KERN_WARNING "Too many I2O LAN devices.\n");
				return -EINVAL;
			}

			dev = i2o_lan_register_device(i2o_dev);
 			if (dev == NULL)
			{
				printk(KERN_WARNING "Unable to register I2O LAN device\n");
				continue; // try next one
			}
			priv = (struct i2o_lan_local *)dev->priv;	

			unit++;
			i2o_landevs[unit] = dev;
			priv->unit = unit;

			printk(KERN_INFO "%s: I2O LAN device registered, tid = %d,"
				" subclass = 0x%08X, unit = %d.\n",
				dev->name, i2o_dev->id, i2o_dev->subclass, 
				priv->unit);
                }
        }

	dprintk(KERN_INFO "%d I2O LAN devices found and registered.\n", unit+1);

	return 0;
}

void cleanup_module(void)
{
	int i;

	for (i = 0; i <= unit; i++)
	{
		struct device *dev = i2o_landevs[i];
		struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
		struct i2o_device *i2o_dev = priv->i2o_dev;	

		switch (i2o_dev->subclass)
		{
		case I2O_LAN_ETHERNET:
			unregister_netdev(dev);
			kfree(dev);
			break;
#ifdef CONFIG_FDDI
		case I2O_LAN_FDDI:
			unregister_netdevice(dev);
			kfree(dev);
			break;
#endif
#ifdef CONFIG_TR
		case I2O_LAN_TR:
			unregister_netdev(dev);
			kfree(dev);
			break;
#endif
		default:
			printk(KERN_WARNING "i2o_lan: Spurious I2O LAN subclass 0x%08X.\n",
			       i2o_dev->subclass);
		}

		dprintk(KERN_INFO "%s: I2O LAN device unregistered.\n", 
			dev->name);
	}

        i2o_remove_handler(&i2o_lan_handler);
}

EXPORT_NO_SYMBOLS;
MODULE_AUTHOR("Univ of Helsinki, CS Department");
MODULE_DESCRIPTION("I2O Lan OSM");

#endif    
