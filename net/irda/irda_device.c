/*********************************************************************
 *                
 * Filename:      irda_device.c
 * Version:       0.3
 * Description:   Abstract device driver layer and helper functions
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Sep  2 20:22:08 1998
 * Modified at:   Mon Jan 18 11:05:59 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
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

#include <linux/config.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/init.h>

#include <net/pkt_sched.h>
#include <asm/dma.h>

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

hashbin_t *irda_device = NULL;

void irda_device_start_todo_timer( struct irda_device *self, int timeout);

/* Netdevice functions */
static int irda_device_net_rebuild_header(struct sk_buff *skb);
static int irda_device_net_hard_header(struct sk_buff *skb, 
				       struct device *dev,
				       unsigned short type, void *daddr, 
				       void *saddr, unsigned len);
static int irda_device_net_set_config( struct device *dev, struct ifmap *map);
static int irda_device_net_change_mtu( struct device *dev, int new_mtu);

#ifdef CONFIG_PROC_FS
int irda_device_proc_read( char *buf, char **start, off_t offset, int len, 
			   int unused);

#endif /* CONFIG_PROC_FS */

__initfunc(int irda_device_init( void))
{
	DEBUG( 4, __FUNCTION__ "()\n");

	/* Allocate master array */
	irda_device = hashbin_new( HB_LOCAL);
	if ( irda_device == NULL) {
		printk( KERN_WARNING "IrDA: Can't allocate irda_device hashbin!\n");
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
#ifdef CONFIG_ESI_DONGLE
	esi_init();
#endif
#ifdef CONFIG_TEKRAM_DONGLE
	tekram_init();
#endif
#ifdef CONFIG_ACTISYS_DONGLE
	actisys_init();
#endif

	return 0;
}

void irda_device_cleanup(void)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( irda_device != NULL, return;);

	hashbin_delete( irda_device, (FREE_FUNC) irda_device_close);
}

/*
 * Function irda_device_open (self)
 *
 *    Open a new IrDA port device
 *
 */
int irda_device_open( struct irda_device *self, char *name, void *priv)
{        
	int result;
	int i=0;
	
	/* Check that a minimum of allocation flags are specified */
	ASSERT(( self->rx_buff.flags & (GFP_KERNEL|GFP_ATOMIC)) != 0, 
	       return -1;);
	ASSERT(( self->tx_buff.flags & (GFP_KERNEL|GFP_ATOMIC)) != 0, 
	       return -1;);

	ASSERT( self->tx_buff.truesize > 0, return -1;);
	ASSERT( self->rx_buff.truesize > 0, return -1;);

	self->rx_buff.data = ( __u8 *) kmalloc( self->rx_buff.truesize, 
						self->rx_buff.flags);
	self->tx_buff.data = ( __u8 *) kmalloc( self->tx_buff.truesize, 
						self->tx_buff.flags);

	if ( self->rx_buff.data == NULL || self->tx_buff.data == NULL)   {
		DEBUG( 0, "IrDA Self: no space for buffers!\n");
		irda_device_close( self);
		return -ENOMEM;
	}

	memset( self->rx_buff.data, 0, self->rx_buff.truesize);
	memset( self->tx_buff.data, 0, self->tx_buff.truesize);
	
	self->magic = IRDA_DEVICE_MAGIC;

	self->rx_buff.in_frame = FALSE;
	self->rx_buff.state = OUTSIDE_FRAME;

	/* Initialize timers */
	init_timer( &self->media_busy_timer);	

	/* Open new IrLAP layer instance */
	self->irlap = irlap_open( self);

	/* A pointer to the low level implementation */
	self->priv = priv;

	/* Initialize IrDA net device */
	do {
		sprintf( self->name, "%s%d", "irda", i++);
	} while ( dev_get( self->name) != NULL);
	
	self->netdev.name = self->name;
	self->netdev.priv = (void *) self;
	self->netdev.next = NULL;

	if (( result = register_netdev( &self->netdev)) != 0) {
		DEBUG( 0, __FUNCTION__ "(), register_netdev() failed!\n");
		return -1;
	}

	/* 
	 * Make the description for the device. self->netdev.name will get
	 * a name like "irda0" and the self->descriptin will get a name
	 * like "irda0 <-> irtty0" 
	 */
	strncpy( self->description, self->name, 4);
	strcat( self->description, " <-> ");
	strncat( self->description, name, 23);

	hashbin_insert( irda_device, (QUEUE *) self, (int) self, NULL);

	/* Open network device */
	dev_open( &self->netdev);

	printk( "IrDA device %s registered.\n", self->name);

	irda_device_set_media_busy( self, FALSE);
        
	return 0;
}

/*
 * Function irda_device_close (self)
 *
 *    Close this instance of the irda_device, just deallocate buffers
 *
 */
void __irda_device_close( struct irda_device *self)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRDA_DEVICE_MAGIC, return;);

	dev_close( &self->netdev);

	/* Remove netdevice */
	unregister_netdev( &self->netdev);

	/* Stop timers */
	del_timer( &self->todo_timer);
	del_timer( &self->media_busy_timer);

	if ( self->tx_buff.data) {
		kfree( self->tx_buff.data);
	}

	if ( self->rx_buff.data) {
		kfree( self->rx_buff.data);
	}

	self->magic = 0;
}

/*
 * Function irda_device_close (self)
 *
 *    
 *
 */
void irda_device_close( struct irda_device *self)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRDA_DEVICE_MAGIC, return;);

	/* Stop IrLAP */
	irlap_close( self->irlap);
	self->irlap = NULL;

	hashbin_remove( irda_device, (int) self, NULL);

	__irda_device_close( self);
}

/*
 * Function irda_device_set_media_busy (self, status)
 *
 *    Called when we have detected that another station is transmiting
 *    in contention mode.
 */
void irda_device_set_media_busy( struct irda_device *self, int status) 
{
	DEBUG( 4, __FUNCTION__ "(%s)\n", status ? "TRUE" : "FALSE");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRDA_DEVICE_MAGIC, return;);

	if ( status) {
		self->media_busy = TRUE;
		irda_device_start_mbusy_timer( self);
		DEBUG( 4, "Media busy!\n");
	} else {
		self->media_busy = FALSE;
		del_timer( &self->media_busy_timer);
	}
}

/*
 * Function __irda_device_change_speed (self, speed)
 *
 *    When this function is called, we will have a process context so its
 *    possible for us to sleep, wait or whatever :-)
 */
static void __irda_device_change_speed( struct irda_device *self, int speed)
{
	ASSERT( self != NULL, return;);

	if ( self->magic != IRDA_DEVICE_MAGIC) {
		DEBUG( 0, __FUNCTION__ 
		       "(), irda device is gone! Maybe you need to update "
		       "your irmanager and/or irattach!");
		       
		       return;
	}

	/*
	 *  Is is possible to change speed yet? Wait until the last byte 
	 *  has been transmitted.
	 */
	if ( self->wait_until_sent) {
		self->wait_until_sent( self);
		
		if ( self->change_speed) {
			self->change_speed( self, speed);
			
			/* Update the QoS value only */
			self->qos.baud_rate.value = speed;
		}
	} else {
		DEBUG( 0, __FUNCTION__ "(), Warning, wait_until_sent() "
		       "is not implemented by the irda_device!\n");
     
	}
}

/*
 * Function irda_device_change_speed (self, speed)
 *
 *    Change the speed of the currently used irda_device
 *
 */
inline void irda_device_change_speed( struct irda_device *self, int speed)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRDA_DEVICE_MAGIC, return;);

	irda_execute_as_process( self, 
				 (TODO_CALLBACK) __irda_device_change_speed, 
				 speed);
}

inline int irda_device_is_media_busy( struct irda_device *self)
{
	ASSERT( self != NULL, return FALSE;);
	ASSERT( self->magic == IRDA_DEVICE_MAGIC, return FALSE;);
	
	return self->media_busy;
}

inline int irda_device_is_receiving( struct irda_device *self)
{
	ASSERT( self != NULL, return FALSE;);
	ASSERT( self->magic == IRDA_DEVICE_MAGIC, return FALSE;);

	if ( self->is_receiving)
		return self->is_receiving( self);
	else
		return FALSE;
}

inline struct qos_info *irda_device_get_qos( struct irda_device *self)
{
	ASSERT( self != NULL, return NULL;);
	ASSERT( self->magic == IRDA_DEVICE_MAGIC, return NULL;);

	return &self->qos;
}

void irda_device_todo_expired( unsigned long data)
{
	struct irda_device *self = ( struct irda_device *) data;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	/* Check that we still exist */
	if ( !self || self->magic != IRDA_DEVICE_MAGIC) {
		return;
	}
	__irda_device_change_speed( self, self->new_speed);
}

/*
 * Function irda_device_start_todo_timer (self, timeout)
 *
 *    Start todo timer. This function is used to delay execution of certain
 *    functions. Its implemented using timers since delaying a timer or a
 *    bottom halves function can be very difficult othervise.
 *
 */
void irda_device_start_todo_timer( struct irda_device *self, int timeout)
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRDA_DEVICE_MAGIC, return;);

	del_timer( &self->todo_timer);
	
	self->todo_timer.data     = (unsigned long) self;
	self->todo_timer.function = &irda_device_todo_expired;
	self->todo_timer.expires  = jiffies + timeout;
	
	add_timer( &self->todo_timer);
}

static struct enet_statistics *irda_device_get_stats( struct device *dev)
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
int irda_device_setup( struct device *dev) 
{
	struct irda_device *self;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( dev != NULL, return -1;);

	self = (struct irda_device *) dev->priv;

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRDA_DEVICE_MAGIC, return -1;);	

	dev->get_stats	     = irda_device_get_stats;
	dev->rebuild_header  = irda_device_net_rebuild_header;
	dev->set_config      = irda_device_net_set_config;
	dev->change_mtu      = irda_device_net_change_mtu;
	dev->hard_header     = irda_device_net_hard_header;
        dev->hard_header_len = 0;
        dev->addr_len        = 0;

        dev->type            = ARPHRD_IRDA;
        dev->tx_queue_len    = 10;  /* Short queues in IrDA */
 
	memcpy( dev->dev_addr, &self->irlap->saddr, 4);
	memset( dev->broadcast, 0xff, 4);

	dev->mtu = 2048;
	dev->tbusy = 1;
	
	dev_init_buffers( dev);

	dev->flags = 0; /* IFF_NOARP | IFF_POINTOPOINT; */
	
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

static int irda_device_net_hard_header (struct sk_buff *skb, 
					struct device *dev,
					unsigned short type, void *daddr, 
					void *saddr, unsigned len)
{
	DEBUG( 0, __FUNCTION__ "()\n");

	skb->mac.raw = skb->data;
        /* skb_push(skb,PPP_HARD_HDR_LEN); */
/*         return PPP_HARD_HDR_LEN; */
	
	return 0;
}

static int irda_device_net_set_config( struct device *dev, struct ifmap *map)
{
	DEBUG( 0, __FUNCTION__ "()\n");

	return 0;
}

static int irda_device_net_change_mtu( struct device *dev, int new_mtu)
{
     DEBUG( 0, __FUNCTION__ "()\n");
     
     return 0;  
}

/*
 * Function irda_device_transmit_finished (void)
 *
 *    Check if there is still some frames in the transmit queue for this
 *    device
 *
 */
int irda_device_txqueue_empty( struct irda_device *self)
{
	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRDA_DEVICE_MAGIC, return -1;);	

	/* FIXME: check if this is the right way of doing it? */
	if ( skb_queue_len( &self->netdev.qdisc->q))
		return FALSE;

	return TRUE;
}

/*
 * Function irda_get_mtt (skb)
 *
 *    Utility function for getting the 
 *
 */
__inline__ int irda_get_mtt( struct sk_buff *skb)
{
        return ((struct irlap_skb_cb *)(skb->cb))->mtt;
}

/*
 * Function setup_dma (idev, buffer, count, mode)
 *
 *    Setup the DMA channel
 *
 */
void setup_dma( int channel, char *buffer, int count, int mode)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	disable_dma( channel);
	clear_dma_ff( channel);
	set_dma_mode( channel, mode);
	set_dma_addr( channel, virt_to_bus(buffer));
	set_dma_count( channel, count);
	enable_dma( channel);

	restore_flags(flags);
}

#ifdef CONFIG_PROC_FS
/*
 * Function irlap_proc_read (buf, start, offset, len, unused)
 *
 *    Give some info to the /proc file system
 *
 */
int irda_device_proc_read( char *buf, char **start, off_t offset, int len, 
			   int unused)
{
	struct irda_device *self;
	unsigned long flags;
     
	save_flags(flags);
	cli();

	len = 0;

	self = (struct irda_device *) hashbin_get_first( irda_device);
	while ( self != NULL) {
		len += sprintf( buf+len, "device name: %s\n", self->name);
		len += sprintf( buf+len, "description: %s\n", 
				self->description);
		len += sprintf( buf+len, "  tbusy=%s\n", self->netdev.tbusy ? 
				"TRUE" : "FALSE");
		len += sprintf( buf+len, "  bps\tmaxtt\tdsize\twinsize\taddbofs\tmintt\tldisc\n");
		
		len += sprintf( buf+len, "  %d\t", 
				self->qos.baud_rate.value);
		len += sprintf( buf+len, "%d\t", 
				self->qos.max_turn_time.value);
		len += sprintf( buf+len, "%d\t",
				self->qos.data_size.value);
		len += sprintf( buf+len, "%d\t",
				self->qos.window_size.value);
		len += sprintf( buf+len, "%d\t",
				self->qos.additional_bofs.value);
		len += sprintf( buf+len, "%d\t", 
				self->qos.min_turn_time.value);
		len += sprintf( buf+len, "%d", 
				self->qos.link_disc_time.value);
		len += sprintf( buf+len, "\n");
	       
		self = (struct irda_device *) hashbin_get_next( irda_device);
	}
	restore_flags(flags);

	return len;
}

#endif /* CONFIG_PROC_FS */

