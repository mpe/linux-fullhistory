/*********************************************************************
 *                
 * Filename:      irda_device.c
 * Version:       0.5
 * Description:   Abstract device driver layer and helper functions
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Sep  2 20:22:08 1998
 * Modified at:   Tue Aug 24 14:31:13 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Modified at:   Fri May 28  3:11 CST 1999
 * Modified by:   Horst von Brand <vonbrand@sleipnir.valparaiso.cl>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
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
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/wireless.h>
#include <linux/spinlock.h>

#include <asm/ioctls.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <asm/dma.h>

#include <net/pkt_sched.h>

#include <net/irda/irda_device.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/timer.h>
#include <net/irda/wrapper.h>

extern int irtty_init(void);
extern int pc87108_init(void);
extern int w83977af_init(void);
extern int esi_init(void);
extern int tekram_init(void);
extern int actisys_init(void);
extern int girbil_init(void);

static hashbin_t *irda_device = NULL;
static hashbin_t *dongles = NULL;

/* Netdevice functions */
static int irda_device_net_rebuild_header(struct sk_buff *skb);
static int irda_device_net_hard_header(struct sk_buff *skb, 
				       struct net_device *dev,
				       unsigned short type, void *daddr, 
				       void *saddr, unsigned len);
static int irda_device_net_set_config(struct net_device *dev, struct ifmap *map);
static int irda_device_net_change_mtu(struct net_device *dev, int new_mtu);
static int irda_device_net_ioctl(struct net_device *dev, struct ifreq *rq,int cmd);
#ifdef CONFIG_PROC_FS
int irda_device_proc_read( char *buf, char **start, off_t offset, int len, 
			   int unused);

#endif /* CONFIG_PROC_FS */

int __init irda_device_init( void)
{
	/* Allocate master array */
	irda_device = hashbin_new( HB_LOCAL);
	if (irda_device == NULL) {
		WARNING("IrDA: Can't allocate irda_device hashbin!\n");
		return -ENOMEM;
	}

	dongles = hashbin_new(HB_LOCAL);
	if (dongles == NULL) {
		printk(KERN_WARNING 
		       "IrDA: Can't allocate dongles hashbin!\n");
		return -ENOMEM;
	}

	/* 
	 * Call the init function of the device drivers that has not been
	 * compiled as a module 
	 */
#ifdef CONFIG_IRTTY_SIR
	irtty_init();
#endif
#ifdef CONFIG_WINBOND_FIR
	w83977af_init();
#endif
#ifdef CONFIG_NSC_FIR
	pc87108_init();
#endif
#ifdef CONFIG_TOSHIBA_FIR
	toshoboe_init();
#endif
#ifdef CONFIG_SMC_IRCC_FIR
	ircc_init();
#endif
#ifdef CONFIG_ESI_DONGLE
	esi_init();
#endif
#ifdef CONFIG_TEKRAM_DONGLE
	tekram_init();
#endif
#ifdef CONFIG_ACTISYS_DONGLE
	actisys_init();
#endif
#ifdef CONFIG_GIRBIL_DONGLE
	girbil_init();
#endif
#ifdef CONFIG_LITELINK_DONGLE
	litelink_init();
#endif
#ifdef CONFIG_AIRPORT_DONGLE
 	airport_init();
#endif
	return 0;
}

void irda_device_cleanup(void)
{
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(irda_device != NULL, return;);

	hashbin_delete(dongles, NULL);
	hashbin_delete(irda_device, (FREE_FUNC) irda_device_close);
}

/*
 * Function irda_device_open (self)
 *
 *    Open a new IrDA port device
 *
 */
int irda_device_open(struct irda_device *self, char *name, void *priv)
{        
	int result;
	int i=0;

	/* Allocate memory if needed */
	if (self->rx_buff.truesize > 0) {
		self->rx_buff.head = ( __u8 *) kmalloc(self->rx_buff.truesize,
						       self->rx_buff.flags);
		if (self->rx_buff.head == NULL)
			return -ENOMEM;

		memset(self->rx_buff.head, 0, self->rx_buff.truesize);
	}
	if (self->tx_buff.truesize > 0) {
		self->tx_buff.head = ( __u8 *) kmalloc(self->tx_buff.truesize, 
						       self->tx_buff.flags);
		if (self->tx_buff.head == NULL) {
			kfree(self->rx_buff.head);
			return -ENOMEM;
		}

		memset(self->tx_buff.head, 0, self->tx_buff.truesize);
	}
	
      	self->magic = IRDA_DEVICE_MAGIC;

	self->rx_buff.in_frame = FALSE;
	self->rx_buff.state = OUTSIDE_FRAME;
	self->tx_buff.data = self->tx_buff.head;
	self->rx_buff.data = self->rx_buff.head;

	/* Initialize timers */
	init_timer(&self->media_busy_timer);	

	self->lock = SPIN_LOCK_UNLOCKED;

	/* A pointer to the low level implementation */
	self->priv = priv;

	/* Initialize IrDA net device */
	do {
		sprintf(self->name, "%s%d", "irda", i++);
	} while (dev_get(self->name) != NULL);
	
	self->netdev.name = self->name;
	self->netdev.priv = (void *) self;
	self->netdev.next = NULL;

	if ((result = register_netdev(&self->netdev)) != 0) {
		DEBUG(0, __FUNCTION__ "(), register_netdev() failed!\n");
		return -1;
	}
	
	/* 
	 * Make the description for the device. self->netdev.name will get
	 * a name like "irda0" and the self->descriptin will get a name
	 * like "irda0 <-> irtty0" 
	 */
	strncpy(self->description, self->name, 5);
	strcat(self->description, " <-> ");
	strncat(self->description, name, 23);
	
	hashbin_insert(irda_device, (QUEUE *) self, (int) self, NULL);
	
	/* Open network device */
	dev_open(&self->netdev);

	MESSAGE("IrDA: Registered device %s\n", self->name);

	irda_device_set_media_busy(self, FALSE);

	return 0;
}

/*
 * Function irda_device_close (self)
 *
 *    Close this instance of the irda_device, just deallocate buffers
 *
 */
void __irda_device_close(struct irda_device *self)
{
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return;);

	/* We do this test to know if the device has been registered at all */
	if (self->netdev.type == ARPHRD_IRDA) {
		dev_close(&self->netdev);
		
		/* Remove netdevice */
		unregister_netdev(&self->netdev);
	}

	/* Stop timers */
	del_timer(&self->media_busy_timer);

	if (self->tx_buff.head)
		kfree(self->tx_buff.head);

	if (self->rx_buff.head)
		kfree(self->rx_buff.head);

	self->magic = 0;
}

/*
 * Function irda_device_close (self)
 *
 *    Close the device
 *
 */
void irda_device_close(struct irda_device *self)
{
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return;);

	/* We are not using any dongle anymore! */
	if (self->dongle)
		self->dongle->close(self);

	hashbin_remove(irda_device, (int) self, NULL);

	__irda_device_close(self);
}

/*
 * Function irda_device_set_media_busy (self, status)
 *
 *    Called when we have detected that another station is transmiting
 *    in contention mode.
 */
void irda_device_set_media_busy(struct irda_device *self, int status) 
{
	DEBUG(4, __FUNCTION__ "(%s)\n", status ? "TRUE" : "FALSE");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return;);

	if (status) {
		self->media_busy = TRUE;
		irda_device_start_mbusy_timer(self);
		DEBUG( 4, "Media busy!\n");
	} else {
		self->media_busy = FALSE;
		del_timer(&self->media_busy_timer);
	}
}

/*
 * Function __irda_device_change_speed (self, speed)
 *
 *    When this function is called, we will have a process context so its
 *    possible for us to sleep, wait or whatever :-)
 */
static void __irda_device_change_speed(struct irda_device *self, int speed)
{
	int n = 0;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return;);
	
	/*
	 *  Is is possible to change speed yet? Wait until the last byte 
	 *  has been transmitted.
	 */
	if (!self->wait_until_sent) {
		ERROR("IrDA: wait_until_sent() "
		      "has not implemented by the IrDA device driver!\n");
		return;
	}

	/* Make sure all transmitted data has actually been sent */
	self->wait_until_sent(self);

	/* Make sure nobody tries to transmit during the speed change */
	while (irda_lock((void *) &self->netdev.tbusy) == FALSE) {
		WARNING(__FUNCTION__ "(), device locked!\n");
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(MSECS_TO_JIFFIES(10));

		if (n++ > 10) {
			WARNING(__FUNCTION__ "(), breaking loop!\n");
			return;
		}
	}
	
	/* Change speed of dongle */
	if (self->dongle)
		self->dongle->change_speed(self, speed);
	
	/* Change speed of IrDA port */
	if (self->change_speed) {
		self->change_speed(self, speed);
		
		/* Update the QoS value only */
		self->qos.baud_rate.value = speed;
	}
	self->netdev.tbusy = FALSE;
}

/*
 * Function irda_device_change_speed (self, speed)
 *
 *    Change the speed of the currently used irda_device
 *
 */
inline void irda_device_change_speed(struct irda_device *self, int speed)
{
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return;);

	irda_execute_as_process(self, 
				(TODO_CALLBACK) __irda_device_change_speed, 
				speed);
}

inline int irda_device_is_media_busy( struct irda_device *self)
{
	ASSERT(self != NULL, return FALSE;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return FALSE;);
	
	return self->media_busy;
}

inline int irda_device_is_receiving( struct irda_device *self)
{
	ASSERT(self != NULL, return FALSE;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return FALSE;);

	if (self->is_receiving)
		return self->is_receiving(self);
	else
		return FALSE;
}

inline struct qos_info *irda_device_get_qos(struct irda_device *self)
{
	ASSERT(self != NULL, return NULL;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return NULL;);

	return &self->qos;
}

static struct enet_statistics *irda_device_get_stats( struct net_device *dev)
{
	struct irda_device *priv = (struct irda_device *) dev->priv;

	return &priv->stats;
}

/*
 * Function irda_device_setup (dev)
 *
 *    This function should be used by low level device drivers in a similar way
 *    as ether_setup() is used by normal network device drivers
 */
int irda_device_setup(struct net_device *dev) 
{
	struct irda_device *self;

	ASSERT(dev != NULL, return -1;);

	self = (struct irda_device *) dev->priv;

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return -1;);	

	dev->get_stats	     = irda_device_get_stats;
	dev->rebuild_header  = irda_device_net_rebuild_header;
	dev->set_config      = irda_device_net_set_config;
	dev->change_mtu      = irda_device_net_change_mtu;
/*  	dev->hard_header     = irda_device_net_hard_header; */
	dev->do_ioctl        = irda_device_net_ioctl;
        dev->hard_header_len = 0;
        dev->addr_len        = 0;

        dev->type            = ARPHRD_IRDA;
        dev->tx_queue_len    = 8; /* Window size + 1 s-frame */
 
	memset(dev->broadcast, 0xff, 4);

	dev->mtu = 2048;
	dev->tbusy = 1;
	
	dev_init_buffers(dev);

	dev->flags = IFF_NOARP;
	
	return 0;
}

int irda_device_net_open(struct net_device *dev)
{
	struct irda_device *self;

	ASSERT(dev != NULL, return -1;);

	self = dev->priv;

	ASSERT(self != NULL, return 0;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return -1;);

	/* Ready to play! */
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/* 
	 * Open new IrLAP layer instance, now that everything should be
	 * initialized properly 
	 */
	self->irlap = irlap_open(self);
        
	/* It's now safe to initilize the saddr */
	memcpy(self->netdev.dev_addr, &self->irlap->saddr, 4);

	return 0;
}

int irda_device_net_close(struct net_device *dev)
{
	struct irda_device *self;

	ASSERT(dev != NULL, return -1;);

	self = dev->priv;

	ASSERT(self != NULL, return 0;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return -1;);
	
	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	/* Stop and remove instance of IrLAP */
	if (self->irlap)
		irlap_close(self->irlap);
	self->irlap = NULL;

	return 0;
}

/*
 * Function irda_device_net_rebuild_header (buff, dev, dst, skb)
 *
 *    
 *
 */
static int irda_device_net_rebuild_header( struct sk_buff *skb)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	return 0;
}

static int irda_device_net_hard_header(struct sk_buff *skb, struct net_device *dev,
				       unsigned short type, void *daddr, 
				       void *saddr, unsigned len)
{
	DEBUG( 0, __FUNCTION__ "()\n");

	skb->mac.raw = skb->data;
        /* skb_push(skb,PPP_HARD_HDR_LEN); */
        /* return PPP_HARD_HDR_LEN; */
	
	return 0;
}

static int irda_device_net_set_config( struct net_device *dev, struct ifmap *map)
{
	DEBUG( 0, __FUNCTION__ "()\n");

	return 0;
}

static int irda_device_net_change_mtu( struct net_device *dev, int new_mtu)
{
     DEBUG( 0, __FUNCTION__ "()\n");
     
     return 0;  
}


#define SIOCSDONGLE     SIOCDEVPRIVATE
static int irda_device_net_ioctl(struct net_device *dev, /* ioctl device */
				 struct ifreq *rq,   /* Data passed */
				 int	cmd)	     /* Ioctl number */
{
	unsigned long flags;
	int			ret = 0;
#ifdef WIRELESS_EXT
	struct iwreq *wrq = (struct iwreq *) rq;
#endif
	struct irda_device *self;

	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(dev != NULL, return -1;);

	self = (struct irda_device *) dev->priv;

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return -1;);

	DEBUG(0, "%s: ->irda_device_net_ioctl(cmd=0x%X)\n", dev->name, cmd);
	
	/* Disable interrupts & save flags */
	save_flags(flags);
	cli();
	
	/* Look what is the request */
	switch (cmd) {
#ifdef WIRELESS_EXT
	case SIOCGIWNAME:
		/* Get name */
		strcpy(wrq->u.name, self->name);
		break;
	case SIOCSIWNWID:
		/* Set domain */
		if (wrq->u.nwid.on) {
			
		} break;
	case SIOCGIWNWID:
		/* Read domain*/
/* 		wrq->u.nwid.nwid = domain; */
/* 		wrq->u.nwid.on = 1; */
		break;
	case SIOCGIWENCODE:
		/* Get scramble key */
		/* 	wrq->u.encoding.code = scramble_key; */
/* 		wrq->u.encoding.method = 1; */
		break;
	case SIOCSIWENCODE:
		/* Set  scramble key */
		/* scramble_key = wrq->u.encoding.code; */
		break;
	case SIOCGIWRANGE:
		/* Basic checking... */
		if(wrq->u.data.pointer != (caddr_t) 0) {
			struct iw_range	range;
			
			/* Verify the user buffer */
			ret = verify_area(VERIFY_WRITE, wrq->u.data.pointer,
					  sizeof(struct iw_range));
			if(ret)
				break;
			
			/* Set the length (useless : its constant...) */
			wrq->u.data.length = sizeof(struct iw_range);
			
			/* Set information in the range struct */
			range.throughput = 1.6 * 1024 * 1024;	/* don't argue on this ! */
			range.min_nwid = 0x0000;
			range.max_nwid = 0x01FF;
			
			range.num_channels = range.num_frequency = 0;
			
			range.sensitivity = 0x3F;
			range.max_qual.qual = 255;
			range.max_qual.level = 255;
			range.max_qual.noise = 0;
			
			/* Copy structure to the user buffer */
			copy_to_user(wrq->u.data.pointer, &range,
				     sizeof(struct iw_range));
		}
		break;
	case SIOCGIWPRIV:
		/* Basic checking... */
#if 0
		if (wrq->u.data.pointer != (caddr_t) 0) {
			struct iw_priv_args	priv[] =
			{	/* cmd,		set_args,	get_args,	name */
				{ SIOCGIPSNAP, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 0, 
				  sizeof(struct site_survey), 
				  "getsitesurvey" },
			};
			
			/* Verify the user buffer */
			ret = verify_area(VERIFY_WRITE, wrq->u.data.pointer,
					  sizeof(priv));
			if (ret)
				break;
			
			/* Set the number of ioctl available */
			wrq->u.data.length = 1;
			
			/* Copy structure to the user buffer */
			copy_to_user(wrq->u.data.pointer, (u_char *) priv,
				     sizeof(priv));
		}
#endif 
		break;
#endif
	case SIOCSDONGLE: /* Set dongle */
		/* Initialize dongle */
		irda_device_init_dongle(self, (int) rq->ifr_data);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	
	restore_flags(flags);
	
	return ret;
}

/*
 * Function irda_device_txqueue_empty (irda_device)
 *
 *    Check if there is still some frames in the transmit queue for this
 *    device. Maybe we should use: q->q.qlen == 0.
 *
 */
int irda_device_txqueue_empty(struct irda_device *self)
{
	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return -1;);	

	if (skb_queue_len(&self->netdev.qdisc->q))
		return FALSE;

	return TRUE;
}

/*
 * Function irda_device_init_dongle (self, type)
 *
 *    Initialize attached dongle. Warning, must be called with a process
 *    context!
 */
void irda_device_init_dongle(struct irda_device *self, int type)
{
	struct dongle_q *node;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return;);

#ifdef CONFIG_KMOD
	/* Try to load the module needed */
	switch (type) {
	case ESI_DONGLE:
		MESSAGE("IrDA: Initializing ESI dongle!\n");
		request_module("esi");
		break;
	case TEKRAM_DONGLE:
		MESSAGE("IrDA: Initializing Tekram dongle!\n");
		request_module("tekram");
		break;
	case ACTISYS_DONGLE:     /* FALLTHROUGH */
	case ACTISYS_PLUS_DONGLE:
		MESSAGE("IrDA: Initializing ACTiSYS dongle!\n");
		request_module("actisys");
		break;
	case GIRBIL_DONGLE:
		MESSAGE("IrDA: Initializing GIrBIL dongle!\n");
		request_module("girbil");
		break;
	case LITELINK_DONGLE:
		MESSAGE("IrDA: Initializing Litelink dongle!\n");
		request_module("litelink");
		break;
	case AIRPORT_DONGLE:
		MESSAGE("IrDA: Initializing Airport dongle!\n");
		request_module("airport");
		break;
	default:
		ERROR("Unknown dongle type!\n");
		return;
	}
#endif /* CONFIG_KMOD */

	node = hashbin_find(dongles, type, NULL);
	if (!node) {
		ERROR("IrDA: Unable to find requested dongle\n");
		return;
	}
	
	/* Check if we're already using a dongle */
	if (self->dongle) {
		self->dongle->close(self);
	}

	/* Set the dongle to be used by this driver */
	self->dongle = node->dongle;

	/* Now initialize the dongle!  */
	node->dongle->open(self, type);
	node->dongle->qos_init(self, &self->qos);
	
	/* Reset dongle */
	node->dongle->reset(self);

	/* Set to default baudrate */
	irda_device_change_speed(self, 9600);
}

/*
 * Function irda_device_register_dongle (dongle)
 *
 *    
 *
 */
int irda_device_register_dongle(struct dongle *dongle)
{
	struct dongle_q *new;
	
	/* Check if this dongle has been registred before */
	if (hashbin_find(dongles, dongle->type, NULL)) {
		MESSAGE(__FUNCTION__ "(), Dongle already registered\n");
                return 0;
        }
	
	/* Make new IrDA dongle */
        new = (struct dongle_q *) kmalloc(sizeof(struct dongle_q), GFP_KERNEL);
        if (new == NULL)
                return -1;
		
	memset(new, 0, sizeof( struct dongle_q));
        new->dongle = dongle;

	/* Insert IrDA dongle into hashbin */
	hashbin_insert(dongles, (QUEUE *) new, dongle->type, NULL);
	
        return 0;
}

/*
 * Function irda_device_unregister_dongle (dongle)
 *
 *    
 *
 */
void irda_device_unregister_dongle(struct dongle *dongle)
{
	struct dongle_q *node;

	node = hashbin_remove(dongles, dongle->type, NULL);
	if (!node) {
		ERROR(__FUNCTION__ "(), dongle not found!\n");
		return;
	}
	kfree(node);
}

/*
 * Function irda_device_set_raw_mode (self, status)
 *
 *    
 *
 */
int irda_device_set_raw_mode(struct irda_device* self, int status)
{	
	DEBUG(2, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return -1;);
	
	if (self->set_raw_mode == NULL) {
		ERROR(__FUNCTION__ "(), set_raw_mode not impl. by "
		      "device driver\n");
		return -1;
	}
	
	self->raw_mode = status;
	self->set_raw_mode(self, status);
	
	return 0;
}

/*
 * Function setup_dma (idev, buffer, count, mode)
 *
 *    Setup the DMA channel
 *
 */
void setup_dma(int channel, char *buffer, int count, int mode)
{
	unsigned long flags;
	
	flags = claim_dma_lock();
	
	disable_dma(channel);
	clear_dma_ff(channel);
	set_dma_mode(channel, mode);
	set_dma_addr(channel, virt_to_bus(buffer));
	set_dma_count(channel, count);
	enable_dma(channel);

	release_dma_lock(flags);
}

#ifdef CONFIG_PROC_FS

int irda_device_print_flags(struct irda_device *idev, char *buf)
{
	int len=0;

	len += sprintf( buf+len, "\t");

	if (idev->netdev.flags & IFF_UP)
		len += sprintf( buf+len, "UP ");
	if (!idev->netdev.tbusy)
		len += sprintf( buf+len, "RUNNING ");

	if (idev->flags & IFF_SIR)
		len += sprintf( buf+len, "SIR ");
	if (idev->flags & IFF_MIR)
		len += sprintf( buf+len, "MIR ");
	if (idev->flags & IFF_FIR)
		len += sprintf( buf+len, "FIR ");
	if (idev->flags & IFF_PIO)
		len += sprintf( buf+len, "PIO ");
	if (idev->flags & IFF_DMA)
		len += sprintf( buf+len, "DMA ");
	if (idev->flags & IFF_SHM)
		len += sprintf( buf+len, "SHM ");
	if (idev->flags & IFF_DONGLE)
		len += sprintf( buf+len, "DONGLE ");

	len += sprintf( buf+len, "\n");

	return len;
}

/*
 * Function irda_device_proc_read (buf, start, offset, len, unused)
 *
 *    Give some info to the /proc file system
 *
 */
int irda_device_proc_read(char *buf, char **start, off_t offset, int len, 
			  int unused)
{
	struct irda_device *self;
	unsigned long flags;
     
	save_flags(flags);
	cli();

	len = 0;

	self = (struct irda_device *) hashbin_get_first(irda_device);
	while ( self != NULL) {
		len += sprintf(buf+len, "\n%s,", self->name);
		len += sprintf(buf+len, "\tbinding: %s\n", 
			       self->description);
		
		len += irda_device_print_flags(self, buf+len);
		len += sprintf(buf+len, "\tbps\tmaxtt\tdsize\twinsize\taddbofs\tmintt\tldisc\n");
		
		len += sprintf(buf+len, "\t%d\t", 
			       self->qos.baud_rate.value);
		len += sprintf(buf+len, "%d\t", 
			       self->qos.max_turn_time.value);
		len += sprintf(buf+len, "%d\t",
			       self->qos.data_size.value);
		len += sprintf(buf+len, "%d\t",
			       self->qos.window_size.value);
		len += sprintf(buf+len, "%d\t",
			       self->qos.additional_bofs.value);
		len += sprintf(buf+len, "%d\t", 
			       self->qos.min_turn_time.value);
		len += sprintf(buf+len, "%d", 
			       self->qos.link_disc_time.value);
		len += sprintf(buf+len, "\n");
		
		self = (struct irda_device *) hashbin_get_next(irda_device);
	}
	restore_flags(flags);

	return len;
}

#endif /* CONFIG_PROC_FS */

