/*
 *	linux/drivers/i2o/i2o_lan.c
 *
 * 	I2O LAN CLASS OSM 	January 7th 1999
 *
 *	(C) Copyright 1999 	University of Helsinki,
 *				Department of Computer Science
 *
 * 	This code is still under development / test.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Authors: 	Auvo Häkkinen <Auvo.Hakkinen@cs.Helsinki.FI>
 *			Juha Sievänen <Juha.Sievanen@cs.Helsinki.FI>
 *			Deepak Saxena <deepak@plexity.net>
 *
 *	Tested:		in FDDI environment (using SysKonnect's DDM)
 *			in Gigabit Eth environment (using SysKonnect's DDM)
 *			in Fast Ethernet environment (using Intel 82558 DDM)
 *
 *	TODO:		check error checking / timeouts
 *			code / test for other LAN classes
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/fddidevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/malloc.h>
#include <linux/trdevice.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/tqueue.h>
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

/* Module params */

static u32 bucketpost   = I2O_BUCKET_COUNT;
static u32 bucketthresh = I2O_BUCKET_THRESH;
static u32 rx_copybreak = 200;

#define MAX_LAN_CARDS 16
static struct net_device *i2o_landevs[MAX_LAN_CARDS+1];
static int unit = -1; 			/* device unit number */

struct i2o_lan_local {
	u8 unit;
	struct i2o_device *i2o_dev;
	struct fddi_statistics stats;   /* see also struct net_device_stats */
	unsigned short (*type_trans)(struct sk_buff *, struct net_device *);
	u32 bucket_count;  		/* nbr of buckets sent to DDM */
	u32 tx_count; 	  		/* packets in one TX message frame */
	u32 tx_max_out;	   		/* DDM's Tx queue len */
	u32 tx_out;			/* outstanding TXes */
	u32 sgl_max;			/* max SGLs in one message frame */
	u32 m;				/* IOP address of msg frame */

	struct tq_struct i2o_batch_send_task;
	struct sk_buff **i2o_fbl;	/* Free bucket list (to reuse skbs) */
	int i2o_fbl_tail;

	spinlock_t lock;
};

static void i2o_lan_reply(struct i2o_handler *h, struct i2o_controller *iop,
                          struct i2o_message *m);
static void i2o_lan_event_reply(struct net_device *dev, u32 *msg);
static int i2o_lan_receive_post(struct net_device *dev);
static int i2o_lan_receive_post_reply(struct net_device *dev, u32 *msg);
static void i2o_lan_release_buckets(struct net_device *dev, u32 *msg);

static struct i2o_handler i2o_lan_handler = {
	i2o_lan_reply,
	"I2O Lan OSM",
	0,              // context
	I2O_CLASS_LAN
};
static int lan_context;

static struct tq_struct i2o_post_buckets_task = {
	0, 0, (void (*)(void *))i2o_lan_receive_post, (void *) 0
};

/*
 * i2o_lan_reply(): The only callback function to handle incoming messages.
 */
static void i2o_lan_reply(struct i2o_handler *h, struct i2o_controller *iop,
			  struct i2o_message *m)
{
	u32 *msg = (u32 *)m;
	u8 unit  = (u8)(msg[2]>>16); // InitiatorContext
	struct net_device *dev = i2o_landevs[unit];

    	if (msg[0] & (1<<13)) { // Fail bit is set
 		printk(KERN_ERR "%s: IOP failed to process the msg:\n",dev->name);
		printk(KERN_ERR "  Cmd = 0x%02X, InitiatorTid = %d, TargetTid = %d\n",
			(msg[1] >> 24) & 0xFF, (msg[1] >> 12) & 0xFFF, msg[1] & 0xFFF);
		printk(KERN_ERR "  FailureCode = 0x%02X\n  Severity = 0x%02X\n  "
			"LowestVersion = 0x%02X\n  HighestVersion = 0x%02X\n",
			 msg[4] >> 24, (msg[4] >> 16) & 0xFF,
			(msg[4] >> 8) & 0xFF, msg[4] & 0xFF);
		printk(KERN_ERR "  FailingHostUnit = 0x%04X\n  FailingIOP = 0x%03X\n",
			msg[5] >> 16, msg[5] & 0xFFF);
		return;
	}	

#ifndef DRIVERDEBUG
	if (msg[4] >> 24)  	/* ReqStatus != SUCCESS */
#endif
		i2o_report_status(KERN_INFO, dev->name, msg);
	
	switch (msg[1] >> 24) {
	case LAN_RECEIVE_POST:
	{
		if (dev->start) {
			if (!(msg[4]>>24)) {
				i2o_lan_receive_post_reply(dev,msg);
				break;
			}

			// Something VERY wrong if this is happening
			printk( KERN_WARNING "%s: rejected bucket post.\n", dev->name);
		}

		// Shutting down, we are getting unused buckets back
		i2o_lan_release_buckets(dev,msg);	
	
		break;
	}

	case LAN_PACKET_SEND:
	case LAN_SDU_SEND:
	{
		struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;
		u8 trl_count  = msg[3] & 0x000000FF;

		while (trl_count) {
			// The HDM has handled the outgoing packet
			dev_kfree_skb((struct sk_buff *)msg[4 + trl_count]);
			dprintk(KERN_INFO "%s: Request skb freed (trl_count=%d).\n",
				dev->name,trl_count);
			priv->tx_out--;
			trl_count--;
		}

		if (dev->tbusy) {
			clear_bit(0,(void*)&dev->tbusy);
			mark_bh(NET_BH); /* inform upper layers */
		}
	
		break;	
	}

	case LAN_RESET: 	/* default reply without payload */
	case LAN_SUSPEND:
		break;

	case I2O_CMD_UTIL_EVT_REGISTER:
        case I2O_CMD_UTIL_EVT_ACK:
        	i2o_lan_event_reply(dev, msg);
        	break;

	default:
		printk(KERN_ERR "%s: No handler for the reply.\n", dev->name);
		i2o_report_status(KERN_INFO, dev->name, msg);		
	}
}

/*
 * i2o_lan_event_reply(): Handle events.
 */
static void i2o_lan_event_reply(struct net_device *dev, u32 *msg)
{
        struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;
        struct i2o_device *i2o_dev = priv->i2o_dev;
        struct i2o_controller *iop = i2o_dev->controller;
        struct i2o_reply {
                u8  version_offset;
                u8  msg_flags;
                u16 msg_size;
                u32 tid:12;
                u32 initiator:12;
                u32 function:8;
                u32 initiator_context;
                u32 transaction_context;
                u32 evt_indicator;
                u32 evt_data[(iop->inbound_size - 20) / 4];     /* max */
        } *evt = (struct i2o_reply *)msg;

        int evt_data_len = (evt->msg_size - 5) * 4;             /* real */

        if (evt->function == I2O_CMD_UTIL_EVT_REGISTER) {
                printk(KERN_INFO "%s: I2O event - ", dev->name);

                switch (evt->evt_indicator) {
                        case I2O_EVT_IND_STATE_CHANGE:
                                printk("State chance 0x%08X.\n",
                                        evt->evt_data[0]);
                                break;
                        case I2O_EVT_IND_GENERAL_WARNING:
                                printk("General warning 0x%02X.\n",
                                        evt->evt_data[0]);
                                break;
                        case I2O_EVT_IND_CONFIGURATION_FLAG:
                                printk("Configuration requested.\n");
                                break;
                        case I2O_EVT_IND_LOCK_RELEASE:
                                printk("Lock released.\n");
                                break;
                        case I2O_EVT_IND_CAPABILITY_CHANGE:
                                printk("Capability change 0x%02X.\n",
                                        evt->evt_data[0]);
                                break;
                        case I2O_EVT_IND_DEVICE_RESET:
                                printk("Device reset.\n");
                                break;
                        case I2O_EVT_IND_EVT_MASK_MODIFIED:
                                printk("Event mask modified, 0x%08X.\n",
                                        evt->evt_data[0]);
                                break;
                        case I2O_EVT_IND_FIELD_MODIFIED: {
                                u16 *work16 = (u16 *)evt->evt_data;
                                printk("Group 0x%04X, field %d changed.\n",
                                        work16[0], work16[1]);
                                break;
                        }
                        case I2O_EVT_IND_VENDOR_EVT: {
                                int i;
                                printk("Vendor event:\n");
                                for (i = 0; i < evt_data_len / 4; i++)
                                        printk("   0x%08X\n", evt->evt_data[i]);
                                break;
                        }
                        case I2O_EVT_IND_DEVICE_STATE:
                                printk("Device state changed 0x%08X.\n",
                                        evt->evt_data[0]);
                                break;
                        case I2O_LAN_EVT_LINK_DOWN:
                                printk("Link to the physical device is lost.\n");
                                break;
                        case I2O_LAN_EVT_LINK_UP:
                                printk("Link to the physical device is (re)established.\n");
                                break;
                        case I2O_LAN_EVT_MEDIA_CHANGE:
                                printk("Media change.\n");
                                break;
                        default:
                                printk("Event Indicator = 0x%08X.\n",
                                        evt->evt_indicator);
                }

                /*
                 * EventAck necessary only for events that cause the device
                 * to syncronize with the user
                 *
                 *if (i2o_event_ack(iop, i2o_dev->lct_data->tid,
                 *               priv->unit << 16 | lan_context,
                 *               evt->evt_indicator,
                 *               evt->evt_data, evt_data_len) < 0)
                 *       printk("%s: Event Acknowledge timeout.\n", dev->name);
                 */
        }

        /* else evt->function == I2O_CMD_UTIL_EVT_ACK) */
        /* Do we need to do something here too? */
}

/*
 * i2o_lan_release_buckets(): Handle unused buckets.
 */
static void i2o_lan_release_buckets(struct net_device *dev, u32 *msg)
{
        struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;
	u8 trl_count  = (u8)(msg[3] & 0x000000FF);
	u32 *pskb = &msg[6];

	while (trl_count--) {
		dprintk("%s: Releasing unused sk_buff %p.\n",dev->name,
			(struct sk_buff*)(*pskb));
		dev_kfree_skb((struct sk_buff*)(*pskb));
		pskb++;
		priv->bucket_count--;
	}
}

/*
 * i2o_lan_receive_post_reply(): Process incoming packets.
 */
static int i2o_lan_receive_post_reply(struct net_device *dev, u32 *msg)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;
	struct i2o_bucket_descriptor *bucket = (struct i2o_bucket_descriptor *)&msg[6];
	struct i2o_packet_info *packet;
	u8 trl_count = msg[3] & 0x000000FF;
	struct sk_buff *skb, *old_skb;

	while (trl_count--) {
		skb = (struct sk_buff *)bucket->context;		
		packet = (struct i2o_packet_info *)bucket->packet_info;	
		priv->bucket_count--;

		if (packet->len < rx_copybreak) {
			old_skb = skb;
			skb = (struct sk_buff *)dev_alloc_skb(packet->len+2);	
			if (skb == NULL) {
				printk("%s: Can't allocate skb.\n", dev->name);
				return -ENOMEM;
			}				
			skb_reserve(skb,2);
			memcpy(skb_put(skb,packet->len), old_skb->data, packet->len);

			if (priv->i2o_fbl_tail < I2O_BUCKET_COUNT)
				priv->i2o_fbl[++priv->i2o_fbl_tail] = old_skb;	
			else
				dev_kfree_skb(old_skb);
		} else
			skb_put(skb,packet->len);	
	
		skb->dev = dev;
		skb->protocol = priv->type_trans(skb, dev);
		netif_rx(skb);

		dprintk(KERN_INFO "%s: Incoming packet (%d bytes) delivered "
			"to upper level.\n",dev->name,packet->len);

		bucket++; // to next Packet Descriptor Block
	}

#ifdef DRIVERDEBUG
	if (msg[5] == 0)
		printk(KERN_INFO "%s: DDM out of buckets (priv->count = %d)!\n",
			 dev->name, priv->bucket_count);
#endif

        if (priv->bucket_count <= bucketpost - bucketthresh) {
        	i2o_post_buckets_task.data = (void *)dev;
        	queue_task(&i2o_post_buckets_task, &tq_immediate);
 		mark_bh(IMMEDIATE_BH);
		/* Note: the task is queued only once */
	}
                        	
	return 0;
}


/*
 * i2o_lan_receive_post(): Post buckets to receive packets.
 */
static int i2o_lan_receive_post(struct net_device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;
	struct sk_buff *skb;
        u32 m; u32 *msg;
        u32 bucket_len = (dev->mtu + dev->hard_header_len);
        u32 total = bucketpost - priv->bucket_count;
        u32 bucket_count;
        u32 *sgl_elem;

        while (total) {
                m = I2O_POST_READ32(iop);
                if (m == 0xFFFFFFFF)
                        return -ETIMEDOUT;
                msg = (u32 *)(iop->mem_offset + m);

                bucket_count = (total >= priv->sgl_max) ? priv->sgl_max : total;
                total -= bucket_count;
                priv->bucket_count += bucket_count;

                dprintk(KERN_INFO "%s: Sending %d buckets (size %d) to LAN HDM.\n",
                        dev->name, bucket_count, bucket_len);

		__raw_writel(I2O_MESSAGE_SIZE(4 + 3 * bucket_count) | SGL_OFFSET_4, msg);
		__raw_writel(LAN_RECEIVE_POST<<24 | HOST_TID<<12 | i2o_dev->lct_data->tid, msg+1);
		__raw_writel(priv->unit << 16 | lan_context, msg+2);
		__raw_writel(bucket_count, msg+3);
                sgl_elem = &msg[4];

                while (bucket_count--) {
                        if (priv->i2o_fbl_tail >= 0)
                                skb = priv->i2o_fbl[priv->i2o_fbl_tail--];
                        else {
                                skb = dev_alloc_skb(bucket_len + 2);
                                if (skb == NULL)
                                        return -ENOMEM;
                                skb_reserve(skb, 2);
                        }
                        __raw_writel(0x51000000 | bucket_len, sgl_elem);
                        __raw_writel((u32)skb,		      sgl_elem+1);
                        __raw_writel(virt_to_bus(skb->data),  sgl_elem+2);
                        sgl_elem += 3;
                }

		/* set LE flag and post buckets */
		__raw_writel(__raw_readl(sgl_elem-3) | 0x80000000, (sgl_elem-3));
                i2o_post_message(iop,m);
        }

        return 0;
}

/*
 * i2o_lan_reset(): Reset the LAN adapter into the operational state and
 * 	restore it to full operation.
 */
static int i2o_lan_reset(struct net_device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;	
	u32 msg[5];

	dprintk(KERN_INFO "%s: LAN RESET MESSAGE.\n", dev->name);
	msg[0] = FIVE_WORD_MSG_SIZE | SGL_OFFSET_0;
	msg[1] = LAN_RESET<<24 | HOST_TID<<12 | i2o_dev->lct_data->tid;
	msg[2] = priv->unit << 16 | lan_context; // InitiatorContext
	msg[3] = 0; 				 // TransactionContext
	msg[4] = 1 << 16; 			 // return posted buckets

	if (i2o_post_this(iop, msg, sizeof(msg)) < 0)
		return -ETIMEDOUT;		

	return 0;
}

/*
 * i2o_lan_suspend(): Put LAN adapter into a safe, non-active state.
 * 	Reply to any LAN class message with status error_no_data_transfer
 *	/ suspended.
 */
static int i2o_lan_suspend(struct net_device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;	
	u32 msg[5];

	dprintk(KERN_INFO "%s: LAN SUSPEND MESSAGE.\n", dev->name);
	msg[0] = FIVE_WORD_MSG_SIZE | SGL_OFFSET_0;
	msg[1] = LAN_SUSPEND<<24 | HOST_TID<<12 | i2o_dev->lct_data->tid;
	msg[2] = priv->unit << 16 | lan_context; // InitiatorContext
	msg[3] = 0; 				 // TransactionContext
	msg[4] = 1 << 16; 			 // return posted buckets

	if (i2o_post_this(iop, msg, sizeof(msg)) < 0)
		return -ETIMEDOUT;

	return 0;
}

/*
 * i2o_set_batch_mode(): Set DDM into batch mode.
 */
static void i2o_set_batch_mode(struct net_device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;	
	struct i2o_controller *iop = i2o_dev->controller;	
	u32 val;

	/* set LAN_BATCH_CONTROL attributes */

	// enable batch mode, toggle automatically
	val = 0x00000000;
	if (i2o_set_scalar(iop, i2o_dev->lct_data->tid, 0x0003, 0, &val, sizeof(val)) <0)
		printk(KERN_WARNING "%s: Unable to enter I2O LAN batch mode.\n",
			dev->name);
	else
		dprintk(KERN_INFO "%s: I2O LAN batch mode enabled.\n",dev->name);

	/*
	 * When PacketOrphanlimit is same as the maximum packet length,
	 * the packets will never be split into two separate buckets
	 */

	/* set LAN_OPERATION attributes */

	val = dev->mtu + dev->hard_header_len; // PacketOrphanLimit
	if (i2o_set_scalar(iop, i2o_dev->lct_data->tid, 0x0004, 2, &val, sizeof(val)) < 0)
		printk(KERN_WARNING "%s: Unable to set PacketOrphanLimit.\n",
			dev->name);
	else
		dprintk(KERN_INFO "%s: PacketOrphanLimit set to %d.\n",
			dev->name,val);
	
	return;	
}

/*
 * i2o_lan_open(): Open the device to send/receive packets via
 * the network device.	
 */
static int i2o_lan_open(struct net_device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
#if 0
	struct i2o_controller *iop = i2o_dev->controller;
	u32 evt_mask =  0xFFC00007; // All generic events, all lan events
#endif
	if (i2o_claim_device(i2o_dev, &i2o_lan_handler, I2O_CLAIM_PRIMARY)) {
		printk(KERN_WARNING "%s: Unable to claim the I2O LAN device.\n", dev->name);
		return -EAGAIN;
	}
	dprintk(KERN_INFO "%s: I2O LAN device claimed (tid=%d).\n",
		dev->name, i2o_dev->lct_data->tid);
#if 0
	if (i2o_event_register(iop, i2o_dev->lct_data->tid,
				priv->unit << 16 | lan_context, evt_mask) < 0)
		printk(KERN_WARNING "%s: Unable to set the event mask.\n", dev->name);
#endif
	i2o_lan_reset(dev);
	
	priv->i2o_fbl = kmalloc(bucketpost * sizeof(struct sk_buff *),GFP_KERNEL);
	if (priv->i2o_fbl == NULL)
		return -ENOMEM;
	priv->i2o_fbl_tail = -1;

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
static int i2o_lan_close(struct net_device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;	
#if 0
	struct i2o_controller *iop = i2o_dev->controller;

	if (i2o_event_register(iop, i2o_dev->lct_data->tid,
				priv->unit << 16 | lan_context, 0) < 0)
		printk(KERN_WARNING "%s: Unable to clear the event mask.\n",
#endif				dev->name);

	dev->tbusy = 1;
	dev->start = 0;
	i2o_lan_suspend(dev);

	if (i2o_release_device(i2o_dev, &i2o_lan_handler, I2O_CLAIM_PRIMARY))
		printk(KERN_WARNING "%s: Unable to unclaim I2O LAN device "
		       "(tid=%d).\n", dev->name, i2o_dev->lct_data->tid);

	while (priv->i2o_fbl_tail >= 0)
		dev_kfree_skb(priv->i2o_fbl[priv->i2o_fbl_tail--]);
	kfree(priv->i2o_fbl);

	MOD_DEC_USE_COUNT;

	return 0;
}

#if 0
/*
 * i2o_lan_sdu_send(): Send a packet, MAC header added by the HDM.
 * Must be supported by Fibre Channel, optional for Ethernet/802.3,
 * Token Ring, FDDI
 */
static int i2o_lan_sdu_send(struct sk_buff *skb, struct net_device *dev)
{	
        return -EINVAL;
}
#endif

static void i2o_lan_batch_send(struct net_device *dev)
{	
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;
	struct i2o_controller *iop = priv->i2o_dev->controller;

	if (priv->tx_count != 0) {
		i2o_post_message(iop, priv->m);
 		dprintk("%s: %d packets sent.\n", dev->name, priv->tx_count);	
		priv->tx_count = 0;
	}
}

/*
 * i2o_lan_packet_send(): Send a packet as is, including the MAC header.
 *
 * Must be supported by Ethernet/802.3, Token Ring, FDDI, optional for
 * Fibre Channel
 */
static int i2o_lan_packet_send(struct sk_buff *skb, struct net_device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;
	u32 m, *msg;
	u32 *sgl_elem;

	/*
	 * Keep interrupt from changing dev->tbusy from underneath us
	 * (Do we really need to do this?)
	 */

	if (test_and_set_bit(0,(void*)&dev->tbusy) != 0) {
        	return 1;
        }

	priv->tx_count++;
	priv->tx_out++;

	if (priv->tx_count == 1) {
		dprintk("%s: New message frame\n", dev->name);

		m = I2O_POST_READ32(iop);
		if (m == 0xFFFFFFFF) {
			dev_kfree_skb(skb);
			return -ETIMEDOUT;
		}
		msg = (u32 *)(iop->mem_offset + m);
		priv->m = m;

		__raw_writel(SEVEN_WORD_MSG_SIZE | 1<<12 | SGL_OFFSET_4, msg);
		__raw_writel(LAN_PACKET_SEND<<24 | HOST_TID<<12 | i2o_dev->lct_data->tid, msg+1);
		__raw_writel(priv->unit << 16 | lan_context, msg+2); // InitiatorContext
		__raw_writel(1 << 4, msg+3);  			     // TransmitControlWord
		__raw_writel(0xD5000000 | skb->len, msg+4);  	     // MAC hdr included
		__raw_writel((u32)skb, msg+5);  		     // TransactionContext
		__raw_writel(virt_to_bus(skb->data), msg+6);

        	queue_task(&priv->i2o_batch_send_task, &tq_scheduler);

	} else {  /* Add new SGL element to the previous message frame */
	
		dprintk("%s: Adding packet %d to msg frame\n", 
			dev->name, priv->tx_count);

		msg = (u32 *)(iop->mem_offset + priv->m);
		sgl_elem = &msg[priv->tx_count * 3 + 1];

		__raw_writel(I2O_MESSAGE_SIZE((__raw_readl(msg)>>16) + 3) | 1<<12 | SGL_OFFSET_4, msg);
		__raw_writel(__raw_readl(sgl_elem-3) & 0x7FFFFFFF, sgl_elem-3); /* clear LE flag */
		__raw_writel(0xD5000000 | skb->len, sgl_elem);
		__raw_writel((u32)skb, sgl_elem+1);
		__raw_writel(virt_to_bus(skb->data), sgl_elem+2);

		if (priv->tx_count == priv->sgl_max) { 	/* frame full, send now */
			i2o_post_message(iop, priv->m);
 			dprintk("%s: %d packets sent.\n", dev->name, priv->tx_count);	
			priv->tx_count = 0;
		}
	}
	
	/* If HDMs TxMaxPktOut reached, stay busy (don't clean tbusy) */

	if (priv->tx_out < priv->tx_max_out)
		clear_bit(0, (void *)&dev->tbusy);
	
	return 0;
}

/*
 * i2o_lan_get_stats(): Fill in the statistics.
 */
static struct net_device_stats *i2o_lan_get_stats(struct net_device *dev)
{
	struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
	struct i2o_device *i2o_dev = priv->i2o_dev;
	struct i2o_controller *iop = i2o_dev->controller;
	u64 val64[16];
	u64 supported_group[4] = { 0, 0, 0, 0 };

        if (i2o_query_scalar(iop, i2o_dev->lct_data->tid, 0x0100, -1, val64,
        		     sizeof(val64)) < 0)
        	printk("%s: Unable to query LAN_HISTORICAL_STATS.\n",dev->name);
	else {
        	dprintk("%s: LAN_HISTORICAL_STATS queried.\n",dev->name);
        	priv->stats.tx_packets = val64[0];
        	priv->stats.tx_bytes   = val64[1];
        	priv->stats.rx_packets = val64[2];
        	priv->stats.rx_bytes   = val64[3];
        	priv->stats.tx_errors  = val64[4];
        	priv->stats.rx_errors  = val64[5];
		priv->stats.rx_dropped = val64[6];
	}

        if (i2o_query_scalar(iop, i2o_dev->lct_data->tid, 0x0180, -1,
        		&supported_group, sizeof(supported_group)) < 0)
        	printk("%s: Unable to query LAN_SUPPORTED_OPTIONAL_HISTORICAL_STATS.\n",dev->name);

	if (supported_group[2]) {
        	if (i2o_query_scalar(iop, i2o_dev->lct_data->tid, 0x0183, -1,
        	 	val64, sizeof(val64)) < 0)
        		printk("%s: Unable to query LAN_OPTIONAL_RX_HISTORICAL_STATS.\n",dev->name);
		else {
        		dprintk("%s: LAN_OPTIONAL_RX_HISTORICAL_STATS queried.\n",dev->name);
			priv->stats.multicast        = val64[4];
			priv->stats.rx_length_errors = val64[10];
			priv->stats.rx_crc_errors    = val64[0];
		}
	}

	if (i2o_dev->lct_data->sub_class == I2O_LAN_ETHERNET) {
		u64 supported_stats = 0;		

        	if (i2o_query_scalar(iop, i2o_dev->lct_data->tid,  0x0200, -1,
        			 val64, sizeof(val64)) < 0)
        		printk("%s: Unable to query LAN_802_3_HISTORICAL_STATS.\n",dev->name);
		else {
        		dprintk("%s: LAN_802_3_HISTORICAL_STATS queried.\n",dev->name);
	 		priv->stats.transmit_collision = val64[1] + val64[2];
			priv->stats.rx_frame_errors    = val64[0];		
			priv->stats.tx_carrier_errors  = val64[6];
		}

        	if (i2o_query_scalar(iop, i2o_dev->lct_data->tid,  0x0280, -1,
        			 &supported_stats, sizeof(supported_stats)) < 0)
        		printk("%s: Unable to query LAN_SUPPORTED_802_3_HISTORICAL_STATS.\n", dev->name);

        	if (supported_stats != 0) {
        		if (i2o_query_scalar(iop, i2o_dev->lct_data->tid,  0x0281, -1,
        				 val64, sizeof(val64)) < 0)
        			printk("%s: Unable to query LAN_OPTIONAL_802_3_HISTORICAL_STATS.\n",dev->name);
			else {
        			dprintk("%s: LAN_OPTIONAL_802_3_HISTORICAL_STATS queried.\n",dev->name);
				if (supported_stats & 0x1)
					priv->stats.rx_over_errors = val64[0];
				if (supported_stats & 0x4)
					priv->stats.tx_heartbeat_errors = val64[2];
			}
		}
	}

#ifdef CONFIG_TR
	if (i2o_dev->lct_data->sub_class == I2O_LAN_TR) {
        	if (i2o_query_scalar(iop, i2o_dev->lct_data->tid,  0x0300, -1,
        			 val64, sizeof(val64)) < 0)
        		printk("%s: Unable to query LAN_802_5_HISTORICAL_STATS.\n",dev->name);
		else {
			struct tr_statistics *stats =
					(struct tr_statistics *)&priv->stats;
			dprintk("%s: LAN_802_5_HISTORICAL_STATS queried.\n",dev->name);

			stats->line_errors		= val64[0];
			stats->internal_errors		= val64[7];
			stats->burst_errors		= val64[4];
			stats->A_C_errors		= val64[2];
			stats->abort_delimiters		= val64[3];
			stats->lost_frames		= val64[1];
			/* stats->recv_congest_count	= ?;  FIXME ??*/
			stats->frame_copied_errors	= val64[5];
			stats->frequency_errors		= val64[6];
			stats->token_errors		= val64[9];
		}
		/* Token Ring optional stats not yet defined */
	}
#endif

#ifdef CONFIG_FDDI
	if (i2o_dev->lct_data->sub_class == I2O_LAN_FDDI) {
        	if (i2o_query_scalar(iop, i2o_dev->lct_data->tid,  0x0400, -1,
        			 val64, sizeof(val64)) < 0)
        		printk("%s: Unable to query LAN_FDDI_HISTORICAL_STATS.\n",dev->name);
		else {
			dprintk("%s: LAN_FDDI_HISTORICAL_STATS queried.\n",dev->name);
			priv->stats.smt_cf_state = val64[0];
			memcpy(priv->stats.mac_upstream_nbr, &val64[1], FDDI_K_ALEN);
			memcpy(priv->stats.mac_downstream_nbr, &val64[2], FDDI_K_ALEN);			
			priv->stats.mac_error_cts = val64[3];
			priv->stats.mac_lost_cts  = val64[4];
			priv->stats.mac_rmt_state = val64[5];
			memcpy(priv->stats.port_lct_fail_cts, &val64[6], 8);
			memcpy(priv->stats.port_lem_reject_cts, &val64[7], 8);	
			memcpy(priv->stats.port_lem_cts, &val64[8], 8);
			memcpy(priv->stats.port_pcm_state, &val64[9], 8);
		}
		/* FDDI optional stats not yet defined */
	}		
#endif

	return (struct net_device_stats *)&priv->stats;
}

/*
 * i2o_lan_set_mc_list(): Enable a network device to receive packets
 *      not send to the protocol address.
 */

static void i2o_lan_set_mc_list(struct net_device *dev)
{
        struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;
        struct i2o_device *i2o_dev = priv->i2o_dev;
        struct i2o_controller *iop = i2o_dev->controller;
        u32 filter_mask;
        u32 max_size_mc_table;
        u32 mc_addr_group[64];

        if (i2o_query_scalar(iop, i2o_dev->lct_data->tid,  0x0001, -1,
                             &mc_addr_group, sizeof(mc_addr_group)) < 0 ) {
		printk(KERN_WARNING "%s: Unable to query LAN_MAC_ADDRESS group.\n", dev->name);
                return;
        }

        max_size_mc_table = mc_addr_group[8];

        if (dev->flags & IFF_PROMISC) {
                filter_mask = 0x00000002;
                dprintk(KERN_INFO "%s: Enabling promiscuous mode...\n", dev->name);
        }

        else if ((dev->flags & IFF_ALLMULTI) || dev->mc_count > max_size_mc_table) {
                filter_mask = 0x00000004;
                dprintk(KERN_INFO "%s: Enabling all multicast mode...\n", dev->name);
        }

        else if (dev->mc_count) {
                struct dev_mc_list *mc;
		u8 mc_table[2 + 8 * dev->mc_count]; // RowCount, Addresses
		u64 *work64 = (u64 *)(mc_table + 2);

                filter_mask = 0x00000000;
                dprintk(KERN_INFO "%s: Enabling multicast mode...\n", dev->name);

                /* Fill multicast addr table */

		memset(mc_table, 0, sizeof(mc_table));
                memcpy(mc_table, &dev->mc_count, 2);
                for (mc = dev->mc_list; mc ; mc = mc->next, work64++ )
                        memcpy(work64, mc->dmi_addr, mc->dmi_addrlen);
		
		/* Clear old mc table, copy new table to <iop,tid> */

                if (i2o_clear_table(iop, i2o_dev->lct_data->tid,  0x0002) < 0)
                        printk("%s: Unable to clear LAN_MULTICAST_MAC_ADDRESS table.\n",dev->name);

                if ((i2o_row_add_table(iop, i2o_dev->lct_data->tid,  0x0002, -1,
                        mc_table, sizeof(mc_table))) < 0)
                        printk("%s: Unable to set LAN_MULTICAST_MAC_ADDRESS table.\n",dev->name);
        }

        else {	
                filter_mask = 0x00000300; // Broadcast, Multicast disabled
                printk(KERN_INFO "%s: Enabling unicast mode...\n",dev->name);
        }

	/* Finally copy new FilterMask to <iop,tid> */

        if (i2o_set_scalar(iop, i2o_dev->lct_data->tid,  0x0001, 3,
                        &filter_mask, sizeof(filter_mask)) <0)
                printk(KERN_WARNING "%s: Unable to set MAC FilterMask.\n",dev->name);

        return;
}

/*
 * i2o_lan_set_multicast_list():
 *       Queue routine i2o_lan_set_mc_list() to be called later.
 *       Needs to be async.
 */

static void i2o_lan_set_multicast_list(struct net_device *dev)
{
        struct tq_struct *task;

        task = (struct tq_struct *)kmalloc(sizeof(struct tq_struct), GFP_KERNEL);
        if (task == NULL)
                return;

        task->next = NULL;
        task->sync = 0;
        task->routine = (void *)i2o_lan_set_mc_list;
        task->data = (void *)dev;
        queue_task(task, &tq_scheduler);
}

/*
 * i2o_lan_change_mtu(): Change maximum transfer unit size.
 */
static int i2o_lan_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > 9000))
		return -EINVAL;
		
	dev->mtu = new_mtu;
	return 0;
}

/*
 * i2o_lan_register_device(): Register LAN class device to kernel.
 */
struct net_device *i2o_lan_register_device(struct i2o_device *i2o_dev)
{
	struct net_device *dev = NULL;
	struct i2o_lan_local *priv = NULL;
	u8 hw_addr[8];
	u32 tx_max_out = 0;
	unsigned short (*type_trans)(struct sk_buff *, struct net_device *);
	void (*unregister_dev)(struct net_device *dev);

	switch (i2o_dev->lct_data->sub_class) {
	case I2O_LAN_ETHERNET:
        	dev = init_etherdev(NULL, sizeof(struct i2o_lan_local));
		if (dev == NULL)
			return NULL;
		type_trans = eth_type_trans;
		unregister_dev = unregister_netdev;
		break;

#ifdef CONFIG_ANYLAN
	case I2O_LAN_100VG:
		printk(KERN_ERR "i2o_lan: 100base VG not yet supported.\n");
		break;
#endif

#ifdef CONFIG_TR
	case I2O_LAN_TR:
		dev = init_trdev(NULL, sizeof(struct i2o_lan_local));
		if (dev==NULL)
			return NULL;
		type_trans = tr_type_trans;
		unregister_dev = unregister_trdev;		
		break;
#endif

#ifdef CONFIG_FDDI
	case I2O_LAN_FDDI:
	{
		int size = sizeof(struct net_device) + sizeof(struct i2o_lan_local)
			   + sizeof("fddi%d ");

        	dev = (struct net_device *) kmalloc(size, GFP_KERNEL);
        	memset((char *)dev, 0, size);
            	dev->priv = (void *)(dev + 1);
                dev->name = (char *)(dev + 1) + sizeof(struct i2o_lan_local);

		if (dev_alloc_name(dev,"fddi%d") < 0) {
			printk(KERN_WARNING "i2o_lan: Too many FDDI devices.\n");
			kfree(dev);
			return NULL;
		}
		type_trans = fddi_type_trans;
		unregister_dev = (void *)unregister_netdevice;
		
		fddi_setup(dev);
		register_netdev(dev);
      	}
	break;
#endif

#ifdef CONFIG_FIBRE_CHANNEL
	case I2O_LAN_FIBRE_CHANNEL:
		printk(KERN_INFO "i2o_lan: Fibre Channel not yet supported.\n");
	break;
#endif

	case I2O_LAN_UNKNOWN:
	default:
		printk(KERN_ERR "i2o_lan: LAN type 0x%08X not supported.\n",
		       i2o_dev->lct_data->sub_class);
		return NULL;
	}

	priv = (struct i2o_lan_local *)dev->priv;
	priv->i2o_dev = i2o_dev;
	priv->type_trans = type_trans;
	priv->bucket_count = 0;
	priv->sgl_max = (i2o_dev->controller->inbound_size - 16) / 12;

	unit++;
	i2o_landevs[unit] = dev;
	priv->unit = unit;

	if (i2o_query_scalar(i2o_dev->controller, i2o_dev->lct_data->tid,
			     0x0001, 0, &hw_addr, sizeof(hw_addr)) < 0) {
     		printk(KERN_ERR "%s: Unable to query hardware address.\n", dev->name);
		unit--;
		unregister_dev(dev);
		kfree(dev);
      		return NULL;
	}

	dprintk("%s: hwaddr = %02X:%02X:%02X:%02X:%02X:%02X\n",
      		dev->name,hw_addr[0], hw_addr[1], hw_addr[2], hw_addr[3],
      		hw_addr[4], hw_addr[5]);

	dev->addr_len = 6;
	memcpy(dev->dev_addr, hw_addr, 6);

	if (i2o_query_scalar(i2o_dev->controller, i2o_dev->lct_data->tid,
		0x0007, 2, &tx_max_out, sizeof(tx_max_out)) < 0)
	{
      		printk(KERN_ERR "%s: Unable to query max TX queue.\n", dev->name);
      		unit--;
      		unregister_dev(dev);
      		kfree(dev);
         	return NULL;
	}
   	dprintk(KERN_INFO "%s: Max TX Outstanding = %d.\n", dev->name, tx_max_out);
   	priv->tx_max_out = tx_max_out;
   	priv->tx_out	= 0;
   	priv->tx_count	= 0;
	priv->lock	= SPIN_LOCK_UNLOCKED;

	priv->i2o_batch_send_task.next    = NULL;
	priv->i2o_batch_send_task.sync    = 0;
	priv->i2o_batch_send_task.routine = (void *)i2o_lan_batch_send;
	priv->i2o_batch_send_task.data    = (void *)dev;

	dev->open               = i2o_lan_open;
	dev->stop               = i2o_lan_close;
	dev->hard_start_xmit    = i2o_lan_packet_send;
	dev->get_stats          = i2o_lan_get_stats;
	dev->set_multicast_list = i2o_lan_set_multicast_list;
	dev->change_mtu		= i2o_lan_change_mtu;

	return dev;
}

#ifdef MODULE
#define i2o_lan_init	init_module
#endif

int __init i2o_lan_init(void)
{
	struct net_device *dev;
	int i;

	printk(KERN_INFO "Linux I2O LAN OSM (c) 1999 University of Helsinki.\n");

	if (bucketpost > I2O_BUCKET_COUNT)
		bucketpost = I2O_BUCKET_COUNT;
	if (bucketthresh > bucketpost)
		bucketthresh = bucketpost;

	if (i2o_install_handler(&i2o_lan_handler) < 0) {
 		printk(KERN_ERR "i2o_lan: Unable to register I2O LAN OSM.\n");
		return -EINVAL;
	}
	lan_context = i2o_lan_handler.context;
	
	for(i=0; i <= MAX_LAN_CARDS; i++)
		i2o_landevs[i] = NULL;

	for (i=0; i < MAX_I2O_CONTROLLERS; i++) {
		struct i2o_controller *iop = i2o_find_controller(i);
		struct i2o_device *i2o_dev;

		if (iop==NULL) continue;

		for (i2o_dev=iop->devices;i2o_dev != NULL;i2o_dev=i2o_dev->next) {

			if (i2o_dev->lct_data->class_id != I2O_CLASS_LAN)
				continue;

			/* Make sure device not already claimed by an ISM */
			if (i2o_dev->lct_data->user_tid != 0xFFF)
				continue;

			if (unit == MAX_LAN_CARDS) {
				i2o_unlock_controller(iop);
				printk(KERN_WARNING "i2o_lan: Too many I2O LAN devices.\n");
				return -EINVAL;
			}

			dev = i2o_lan_register_device(i2o_dev);
 			if (dev == NULL) {
				printk(KERN_ERR "i2o_lan: Unable to register I2O LAN device.\n");
				continue; // try next one
			}

			printk(KERN_INFO "%s: I2O LAN device registered, tid = %d,"
				" subclass = 0x%08X, unit = %d.\n",
				dev->name, i2o_dev->lct_data->tid, i2o_dev->lct_data->sub_class,
				((struct i2o_lan_local *)dev->priv)->unit);
		}

		i2o_unlock_controller(iop);
	}

	dprintk(KERN_INFO "%d I2O LAN devices found and registered.\n", unit+1);

	return 0;
}

#ifdef MODULE

void cleanup_module(void)
{
	int i;

	for (i = 0; i <= unit; i++) {
		struct net_device *dev = i2o_landevs[i];
		struct i2o_lan_local *priv = (struct i2o_lan_local *)dev->priv;	
		struct i2o_device *i2o_dev = priv->i2o_dev;	

		switch (i2o_dev->lct_data->sub_class) {
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
			unregister_trdev(dev);
			kfree(dev);
			break;
#endif
		default:
			printk(KERN_WARNING "i2o_lan: Spurious I2O LAN subclass 0x%08X.\n",
			       i2o_dev->lct_data->sub_class);
		}

		dprintk(KERN_INFO "%s: I2O LAN device unregistered.\n",
			dev->name);
	}

	i2o_remove_handler(&i2o_lan_handler);
}

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Univ of Helsinki, CS Department");
MODULE_DESCRIPTION("I2O Lan OSM");

MODULE_PARM(bucketpost, "i");   // Total number of buckets to post
MODULE_PARM(bucketthresh, "i"); // Bucket post threshold
MODULE_PARM(rx_copybreak, "i");

#endif
