/*********************************************************************
 *                
 * Filename:      irobex.c
 * Version:       0.3
 * Description:   Kernel side of the IrOBEX layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Jun 25 21:21:07 1998
 * Modified at:   Sat Jan 16 22:18:03 1999
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
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/ioctl.h>
#include <linux/init.h>

#include <asm/byteorder.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/poll.h>

#include <net/irda/irttp.h>
#include <net/irda/irias_object.h>
#include <net/irda/iriap.h>

#include <net/irda/irobex.h>

/*
 *  Master structure, only one instance for now!!
 */
struct irobex_cb *irobex;

char *irobex_state[] = {
	"OBEX_IDLE",
	"OBEX_DISCOVER",
	"OBEX_QUERY",
	"OBEX_CONN",
	"OBEX_DATA",
};

static int irobex_dev_open( struct inode * inode, struct file *file);
static int irobex_ioctl( struct inode *inode, struct file *filp, 
			 unsigned int cmd, unsigned long arg);

static int irobex_dev_close( struct inode *inode, struct file *file);
static ssize_t irobex_read( struct file *file, char *buffer, size_t count, 
			    loff_t *noidea);
static ssize_t irobex_write( struct file *file, const char *buffer,
			     size_t count, loff_t *noidea);
static loff_t irobex_seek( struct file *, loff_t, int);
static u_int irobex_poll( struct file *file, poll_table *wait);
static int irobex_fasync( int, struct file *, int);

static struct file_operations irobex_fops = {
	irobex_seek,	/* seek */
	irobex_read,
	irobex_write,
	NULL,		/* readdir */
	irobex_poll,    /* poll */
	irobex_ioctl,	/* ioctl */
	NULL,		/* mmap */
	irobex_dev_open,
	NULL,
	irobex_dev_close,
	NULL,
	irobex_fasync,
};

#ifdef CONFIG_PROC_FS
static int irobex_proc_read( char *buf, char **start, off_t offset, 
			     int len, int unused);

extern struct proc_dir_entry proc_irda;

struct proc_dir_entry proc_irobex = {
	0, 6, "irobex",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL,
	&irobex_proc_read,
};
#endif

/*
 * Function irobex_init (dev)
 *
 *   Initializes the irobex control structure, and registers as a misc
 *   device
 *
 */
__initfunc(int irobex_init(void))
{
	struct irobex_cb *self;

	self = kmalloc(sizeof(struct irobex_cb), GFP_ATOMIC);
	if ( self == NULL)
		return -ENOMEM;
	
	memset( self, 0, sizeof(struct irobex_cb));
 	sprintf( self->devname, "irobex%d", 0); /* Just one instance for now */
	
	self->magic = IROBEX_MAGIC;
	self->rx_flow = self->tx_flow = FLOW_START;
	
	self->dev.minor = MISC_DYNAMIC_MINOR;
	self->dev.name = "irobex";
	self->dev.fops = &irobex_fops;
	
	skb_queue_head_init( &self->rx_queue);
	init_timer( &self->watchdog_timer);

	irobex = self;
	
	misc_register( &self->dev);

#ifdef CONFIG_PROC_FS
	proc_register( &proc_irda, &proc_irobex);
#endif /* CONFIG_PROC_FS */
		
	irlmp_register_layer( S_OBEX, CLIENT | SERVER, TRUE, 
			      irobex_discovery_indication);
	
	return 0;
}

/*
 * Function irobex_cleanup (void)
 *
 *     Removes the IrOBEX layer
 *
 */
#ifdef MODULE
void irobex_cleanup(void)
{
	struct sk_buff *skb;
	struct irobex_cb *self;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	self = irobex;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);	

	/*
	 *  Deregister client and server
	 */
	irlmp_unregister_layer( S_OBEX, CLIENT | SERVER);

	if ( self->tsap) {
		irttp_close_tsap( self->tsap);
		self->tsap = NULL;
	}

	/* Stop timers */
	del_timer( &self->watchdog_timer);

	/*
	 *  Deallocate buffers
	 */
	while (( skb = skb_dequeue( &self->rx_queue)) != NULL)
		dev_kfree_skb( skb);

#ifdef CONFIG_PROC_FS
	proc_unregister( &proc_irda, proc_irobex.low_ino);
#endif
	
	misc_deregister( &self->dev);
	
	kfree( self);	
}
#endif /* MODULE */

/*
 * Function irobex_read (inode, file, buffer, count)
 *
 *    User process wants to read some data
 *
 */
static ssize_t irobex_read( struct file *file, char *buffer, size_t count, 
			    loff_t *noidea)
{
	int len=0;
	struct irobex_cb *self;
	struct sk_buff *skb = NULL;
	int ret;
	
	self = irobex;

	ASSERT( self != NULL, return -EIO;);
	ASSERT( self->magic == IROBEX_MAGIC, return -EIO;);
  
	DEBUG( 4, __FUNCTION__ ": count=%d, skb_len=%d, state=%s, eof=%d\n", 
	       count, skb_queue_len( &self->rx_queue), 
	       irobex_state[self->state], 
	       self->eof);

	if ( self->state != OBEX_DATA) {
		DEBUG( 0, __FUNCTION__ "(), link not connected yet!\n");
		return -EIO;
	}

	/*
	 *  If there is data to return, then we return it. If not, then we 
	 *  must check if we are still connected
	 */
	if ( skb_queue_len( &self->rx_queue) == 0) {

		/* Still connected?  */
		if ( self->state != OBEX_DATA) {
			switch ( self->eof) {
			case LM_USER_REQUEST:
				self->eof = FALSE;
				DEBUG(3, "read_irobex: returning 0\n");
				ret = 0;
				break;
			case LM_LAP_DISCONNECT:
				self->eof = FALSE;
				ret = -EIO;
				break;
			case LM_LAP_RESET:
				self->eof = FALSE;
				ret = -ECONNRESET;
				break;
			default:
				self->eof = FALSE;
				ret = -EIO;
				break;
			}
			return ret;
		}

		/* Return if user does not want to block */
		if ( file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		/* Go to sleep and wait for data!  */
		interruptible_sleep_on( &self->read_wait);

		/*
		 *  Ensure proper reaction to signals, and screen out 
		 *  blocked signals (page 112. linux device drivers)
		 */
		if ( signal_pending( current))
			return -ERESTARTSYS;
	}
	
	while ( count && skb_queue_len( &self->rx_queue)) {

		skb = skb_dequeue( &self->rx_queue);
		
		/*
		 *  Check if we have previously stopped IrTTP and we know
		 *  have more free space in our rx_queue. If so tell IrTTP
		 *  to start delivering frames again before our rx_queue gets
		 *  empty
		 */
		if ( self->rx_flow == FLOW_STOP) {
			if ( skb_queue_len( &self->rx_queue) < LOW_THRESHOLD) {
				DEBUG( 4, __FUNCTION__ "(), Starting IrTTP\n");
				self->rx_flow = FLOW_START;
				irttp_flow_request( self->tsap, FLOW_START);
			}
		}

		/*  
		 *  Is the request from the user less that the amount in the 
		 *  current packet?  
		 */
		if ( count <  skb->len) {
			copy_to_user( buffer+len, skb->data, count);
			len += count;
			
			/*
			 *  Remove copied data from skb and queue
			 *  it for next read
			 */
			skb_pull( skb, count);
			skb_queue_head( &self->rx_queue, skb);
			
			return len;
		} else {
			copy_to_user( buffer+len, skb->data, skb->len);
			count -= skb->len;
			len += skb->len;
			
			dev_kfree_skb( skb);
		}
	}
	return len;
}

/*
 * Function irobex_write (inode, file, buffer, count)
 *
 *    User process wants to write to device
 *
 */
static ssize_t irobex_write( struct file *file, const char *buffer, 
			     size_t count, loff_t *noidea)
{
	struct irobex_cb *self;
	struct sk_buff *skb;
	int data_len = 0;
	int len = 0;
	
	self = irobex;
	
	ASSERT( self != NULL, return -EIO;);
	ASSERT( self->magic == IROBEX_MAGIC, return -EIO;);

	DEBUG( 4, __FUNCTION__ ": count = %d\n", count);
	
	/*
	 *  If we are not connected then we just give up!
	 */
	if ( self->state != OBEX_DATA) {
		DEBUG( 0, __FUNCTION__ "(): Not connected!\n");
		
		return -ENOLINK;
	} 
	
	/* Check if IrTTP is wants us to slow down */
	if ( self->tx_flow == FLOW_STOP) {
		DEBUG( 4, __FUNCTION__ 
		       "(), IrTTP wants us to slow down, going to sleep\n");
		interruptible_sleep_on( &self->write_wait);
	}
	
	/* Send data to TTP layer possibly as muliple packets */
	while ( count) {
		
		/*
		 *  Check if request is larger than what fits inside a TTP
		 *  frame. In that case we must fragment the frame into 
		 *  multiple TTP frames. IrOBEX should not care about message
		 *  boundaries.
		 */
		if ( count < (self->irlap_data_size - IROBEX_MAX_HEADER))
			data_len = count;
		else 
		        data_len = self->irlap_data_size - IROBEX_MAX_HEADER;
		
		DEBUG( 4, __FUNCTION__ "(), data_len=%d, header_len = %d\n", 
		       data_len, IROBEX_MAX_HEADER);
		
		skb = dev_alloc_skb( data_len + IROBEX_MAX_HEADER);
		if ( skb == NULL) {
			DEBUG( 0, "irobex - couldn't allocate skbuff!\n");
			return 0;
		}
		
		skb_reserve( skb, IROBEX_MAX_HEADER);
		skb_put( skb, data_len);
		
		copy_from_user( skb->data, buffer+len, data_len);
		len += data_len;
		count -= data_len;
		
		DEBUG( 4, __FUNCTION__ "(), skb->len=%d\n", (int) skb->len);
		ASSERT( skb->len <= (self->irlap_data_size-IROBEX_MAX_HEADER),
			return len;);
		
		irttp_data_request( self->tsap, skb);
	}
	return (len);
}

/*
 * Function irobex_poll (file, wait)
 *
 *    
 *
 */
static u_int irobex_poll(struct file *file, poll_table *wait)
{
	DEBUG( 0, __FUNCTION__ "(), Sorry not implemented yet!\n");

	/* check out /usr/src/pcmcia/modules/ds.c for an example */
	return 0;
}

/*
 * Function irobex_fasync (inode, filp, mode)
 *
 *    Implementation for SIGIO
 *
 */
static int irobex_fasync( int fd, struct file *filp, int on)
{
	struct irobex_cb *self;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	self = irobex;

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IROBEX_MAGIC, return -1;);

	return fasync_helper( fd, filp, on, &self->async);
}

/*
 * Function irobex_seek (inode, file, buffer, count)
 *
 *    Not implemented yet!
 *
 */
static loff_t irobex_seek( struct file *file, loff_t off, int whence)
{
	DEBUG( 0, __FUNCTION__ "(), Not implemented yet!\n");

	return -ESPIPE;
}

/*
 * Function irobex_ioctl (inode, filp, cmd, arg)
 *
 *    Drivers IOCTL handler, used for connecting and disconnecting
 *    irobex connections
 *
 */
static int irobex_ioctl( struct inode *inode, struct file *filp, 
			 unsigned int cmd, unsigned long arg)
{
	struct irobex_cb *self;
	int err = 0;
	int size = _IOC_SIZE(cmd);
	
	DEBUG( 4, __FUNCTION__ "()\n");

	self = irobex;
	
	ASSERT( self != NULL, return -ENOTTY;);
	ASSERT( self->magic = IROBEX_MAGIC, return -ENOTTY;);
	
	if ( _IOC_TYPE(cmd) != IROBEX_IOC_MAGIC) 
		return -EINVAL;
	if ( _IOC_NR(cmd) > IROBEX_IOC_MAXNR)
		return -EINVAL;

	if ( _IOC_DIR(cmd) & _IOC_READ)
		err = verify_area( VERIFY_WRITE, (void *) arg, size);
	else if ( _IOC_DIR(cmd) & _IOC_WRITE)
		err = verify_area( VERIFY_READ, (void *) arg, size);
	if ( err)
		return err;

	switch ( cmd) {
	case IROBEX_IOCSCONNECT:
		DEBUG( 4, __FUNCTION__ "(): IROBEX_IOCSCONNECT!\n");
		
		/* Already connected? */
		if ( self->state == OBEX_DATA) {
			DEBUG( 0, __FUNCTION__ "(), already connected!\n");
			return 0;
		}
		
		/* Timeout after 15 secs. */
		irobex_start_watchdog_timer( self, 1000);

		/*
		 * If we have discovered a remote device we
		 * check if the discovery is still fresh. If not, we don't
		 * trust the address.
		 */
		if ( self->daddr && ((jiffies - self->time_discovered) > 500))
			self->daddr = 0;

		/* 
		 * Try to discover remote remote device if it has not been 
		 * discovered yet. 
		 */
		if ( !self->daddr) {
			self->state = OBEX_DISCOVER;
			
			irlmp_discovery_request( 8);
			
			/* Wait for discovery to complete */
			interruptible_sleep_on( &self->write_wait);
			del_timer( &self->watchdog_timer);
		}
		
		/* Give up if we are unable to discover any remote devices */
		if ( !self->daddr) {
			DEBUG( 0, __FUNCTION__ 
			       "(), Unable to discover any devices!\n");
			return -ENOTTY;
		}
		
		/* Need to find remote destination TSAP selector? */
		if ( !self->dtsap_sel) {
			DEBUG( 0, __FUNCTION__ "() : Quering remote IAS!\n");
			
			self->state = OBEX_QUERY;
			
			/* Timeout after 5 secs. */
			irobex_start_watchdog_timer( self, 500);
			iriap_getvaluebyclass_request( 
				self->daddr,
				"OBEX", 
				"IrDA:TinyTP:LsapSel",
				irobex_get_value_confirm,
				self);
			
			interruptible_sleep_on( &self->write_wait);
			del_timer( &self->watchdog_timer);
		}	
		
		if ( !self->dtsap_sel) {
			DEBUG( 0, __FUNCTION__ 
			       "(), Unable to query remote LM-IAS!\n");
			return -ENOTTY;
		}

		self->state = OBEX_CONN;
		
		/* Timeout after 5 secs. */
		irobex_start_watchdog_timer( self, 500);

		irttp_connect_request( self->tsap, self->dtsap_sel, 
				       self->daddr, NULL, SAR_DISABLE, 
				       NULL);
		
		/* Go to sleep and wait for connection!  */
		interruptible_sleep_on( &self->write_wait);
		del_timer( &self->watchdog_timer);
		
		if ( self->state != OBEX_DATA) {
			DEBUG( 0, __FUNCTION__ 
			       "(), Unable to connect to remote device!\n");
			return -ENOTTY;
		}
		
		break;
	case IROBEX_IOCSDISCONNECT:
		DEBUG( 4, __FUNCTION__ "(): IROBEX_IOCSDISCONNECT!\n");

		if ( self->state != OBEX_DATA)
			return 0;

		irttp_disconnect_request( self->tsap, NULL, P_NORMAL);

		/* Reset values for this instance */
		self->state = OBEX_IDLE;
		self->eof = LM_USER_REQUEST;
		self->daddr = 0;
		self->dtsap_sel = 0;
		self->rx_flow = FLOW_START;
		self->tx_flow = FLOW_START;
		
		wake_up_interruptible( &self->read_wait);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Function irobex_dev_open (inode, file)
 *
 *    Device opened by user process
 *
 */
static int irobex_dev_open( struct inode * inode, struct file *file)
{
	struct irobex_cb *self;
	
	DEBUG( 4, __FUNCTION__ "()\n");
	
	self = irobex;

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IROBEX_MAGIC, return -1;);

	if ( self->count++) {
		DEBUG( 3, "open_irobex: count not zero; actual = %d\n",
		       self->count);
		self->count--;
		return -EBUSY;
	}

	irobex_register_server( self);

	/* Reset values for this instance */
	self->state = OBEX_IDLE;
	self->eof = FALSE;
	self->daddr = 0;
	self->dtsap_sel = 0;
	self->rx_flow = FLOW_START;
	self->tx_flow = FLOW_START;

	MOD_INC_USE_COUNT;
	
	return 0;
}

static int irobex_dev_close( struct inode *inode, struct file *file)
{
	struct irobex_cb *self;
	struct sk_buff *skb;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	self = irobex;

	ASSERT( self != NULL, return -ENODEV;);
	ASSERT( self->magic == IROBEX_MAGIC, return -EBADR;);

	/* Deallocate buffers */
	while (( skb = skb_dequeue( &self->rx_queue)) != NULL) {
		DEBUG( 3, "irobex_close: freeing SKB\n");
		dev_kfree_skb( skb);
	}

	/* Close TSAP is its still there */
	if ( self->tsap) {
		irttp_close_tsap( self->tsap);
		self->tsap = NULL;
	}
	self->state = OBEX_IDLE;
	self->eof = FALSE;
	self->daddr = 0;
	self->dtsap_sel = 0;
	self->rx_flow = FLOW_START;
	self->tx_flow = FLOW_START;

	/* Remove this filp from the asynchronously notified filp's */
	irobex_fasync( -1, file, 0);

	self->count--;
	
	MOD_DEC_USE_COUNT;

	return 0;
}

/*
 * Function irobex_discovery_inication (daddr)
 *
 *    Remote device discovered, try query the remote IAS to see which
 *    device it is, and which services it has.
 *
 */
void irobex_discovery_indication( DISCOVERY *discovery) 
{
 	struct irobex_cb *self;
	
	DEBUG( 4, __FUNCTION__ "()\n");
	
	self = irobex;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);

	/* Remember address and time if was discovered */
	self->daddr = discovery->daddr;
	self->time_discovered = jiffies;
	
	/* Wake up process if its waiting for device to be discovered */
	if ( self->state == OBEX_DISCOVER)
		wake_up_interruptible( &self->write_wait);
}

/*
 * Function irobex_disconnect_indication (handle, reason, priv)
 *
 *    Link has been disconnected
 *
 */
void irobex_disconnect_indication( void *instance, void *sap, 
				   LM_REASON reason, struct sk_buff *userdata)
{
	struct irobex_cb *self;
	
	DEBUG( 4, __FUNCTION__ "(), reason=%d\n", reason);
	
	self = ( struct irobex_cb *) instance;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);
	
	self->state = OBEX_IDLE;
	self->eof = reason;
	self->daddr = 0;
	self->dtsap_sel = 0;
	self->rx_flow = self->tx_flow = FLOW_START;

	wake_up_interruptible( &self->read_wait);
	wake_up_interruptible( &self->write_wait);
	
	DEBUG( 4, __FUNCTION__ "(), skb_queue_len=%d\n",
	       skb_queue_len( &irobex->rx_queue));
	
	if ( userdata)
	        dev_kfree_skb( userdata);
}

/*
 * Function irobex_connect_confirm (instance, sap, qos, userdata)
 *
 *    Connection to peer IrOBEX layer established
 *
 */
void irobex_connect_confirm( void *instance, void *sap, struct qos_info *qos,
			     int max_sdu_size, struct sk_buff *userdata)
{
	struct irobex_cb *self;
	
	DEBUG( 4, __FUNCTION__ "()\n");
	
	self = ( struct irobex_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);
	ASSERT( qos != NULL, return;);

	DEBUG( 4, __FUNCTION__ "(), IrLAP data size=%d\n", 
	       qos->data_size.value);

	self->irlap_data_size = qos->data_size.value;

	/*
	 *  Wake up any blocked process wanting to write. Finally this process
	 *  can start writing since the connection is now open :-)
	 */
	if (self->state == OBEX_CONN) {
		self->state = OBEX_DATA;
		wake_up_interruptible( &self->write_wait);
	}
	
	if ( userdata) {
	        dev_kfree_skb( userdata);

	}
}

/*
 * Function irobex_connect_response (handle)
 *
 *    Accept incomming connection
 *
 */
void irobex_connect_response( struct irobex_cb *self)
{
	struct sk_buff *skb;
/* 	__u8 *frame; */

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);	

	self->state = OBEX_DATA;

	skb = dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0, __FUNCTION__ "() Could not allocate sk_buff!\n");
		return;
	}

	/* Reserve space for MUX_CONTROL and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_CONTROL_HEADER+LAP_HEADER);

	irttp_connect_response( self->tsap, SAR_DISABLE, skb);
}

/*
 * Function irobex_connect_indication (handle, skb, priv)
 *
 *    Connection request from a remote device
 *
 */
void irobex_connect_indication( void *instance, void *sap, 
				struct qos_info *qos, int max_sdu_size,
				struct sk_buff *userdata)
{
	struct irmanager_event mgr_event;
	struct irobex_cb *self;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	self = ( struct irobex_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);	
	ASSERT( userdata != NULL, return;);

	self->eof = FALSE;

	DEBUG( 4, __FUNCTION__ "(), skb_len = %d\n", 
	       (int) userdata->len);

	DEBUG( 4, __FUNCTION__ "(), IrLAP data size=%d\n", 
	       qos->data_size.value);

	ASSERT( qos->data_size.value >= 64, return;);

	self->irlap_data_size = qos->data_size.value;

	/* We just accept the connection */
	irobex_connect_response( self);
#if 1
	mgr_event.event = EVENT_IROBEX_START;
	sprintf( mgr_event.devname, "%s", self->devname);
	irmanager_notify( &mgr_event);
#endif
	wake_up_interruptible( &self->read_wait);

	if ( userdata) {
	        dev_kfree_skb( userdata);
	}
}

/*
 * Function irobex_data_indication (instance, sap, skb)
 *
 *    This function gets the data that is received on the data channel
 *
 */
void irobex_data_indication( void *instance, void *sap, struct sk_buff *skb) 
{

	struct irobex_cb *self;
	
	self = ( struct irobex_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	
	DEBUG( 4, __FUNCTION__ "(), len=%d\n", (int) skb->len);
	
	skb_queue_tail( &self->rx_queue, skb);
	
	/*
	 *  Check if queues are beginning to get filled, and inform 
	 *  IrTTP to slow down if that is the case
	 */
	if ( skb_queue_len( &self->rx_queue) > HIGH_THRESHOLD) {
		DEBUG( 0, __FUNCTION__ 
		       "(), rx_queue is full, telling IrTTP to slow down\n");
		self->rx_flow = FLOW_STOP;
		irttp_flow_request( self->tsap, FLOW_STOP);
	}

	/*
	 *  Wake up process blocked on read or select
	 */
	wake_up_interruptible( &self->read_wait);

	/* Send signal to asynchronous readers */
	if ( self->async)
		kill_fasync( self->async, SIGIO);
}

/*
 * Function irobex_flow_indication (instance, sap, cmd)
 *
 *    
 *
 */
void irobex_flow_indication( void *instance, void *sap, LOCAL_FLOW flow) 
{
	struct irobex_cb *self;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	self = ( struct irobex_cb *) instance;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);
	
	switch ( flow) {
	case FLOW_STOP:
		DEBUG( 0, __FUNCTION__ "(), IrTTP wants us to slow down\n");
		self->tx_flow = flow;
		break;
	case FLOW_START:
		self->tx_flow = flow;
		DEBUG( 0, __FUNCTION__ "(), IrTTP wants us to start again\n");
		wake_up_interruptible( &self->write_wait);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown flow command!\n");
	}
}

/*
 * Function irobex_get_value_confirm (obj_id, value)
 *
 *    Got results from previous GetValueByClass request
 *
 */
void irobex_get_value_confirm( __u16 obj_id, struct ias_value *value, 
			       void *priv)
{
	struct irobex_cb *self;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( priv != NULL, return;);
	self = ( struct irobex_cb *) priv;
	
	if ( !self || self->magic != IROBEX_MAGIC) {
		DEBUG( 0, "irobex_get_value_confirm: bad magic!\n");
		return;
	}

	switch ( value->type) {
	case IAS_INTEGER:
		DEBUG( 4, __FUNCTION__ "() int=%d\n", value->t.integer);
		
		if ( value->t.integer != -1) {
			self->dtsap_sel = value->t.integer;

			/*
			 *  Got the remote TSAP, so wake up any processes
			 *  blocking on write. We don't do the connect 
			 *  ourselves since we must make sure there is a 
			 *  process that wants to make a connection, so we
			 *  just let that process do the connect itself
			 */
			if ( self->state == OBEX_QUERY)
				wake_up_interruptible( &self->write_wait);
		} else 
			self->dtsap_sel = 0;
		break;
	case IAS_STRING:
		DEBUG( 0, __FUNCTION__ "(), got string %s\n", value->t.string);
		break;
	case IAS_OCT_SEQ:
		DEBUG( 0, __FUNCTION__ "(), OCT_SEQ not implemented\n");
		break;
	case IAS_MISSING:
		DEBUG( 0, __FUNCTION__ "(), MISSING not implemented\n");
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), unknown type!\n");
		break;
	}
}

/*
 * Function irobex_provider_confirm (dlsap)
 *
 *    IrOBEX provider is discovered. We can now establish connections
 *    TODO: This function is currently not used!
 */
void irobex_provider_confirm( struct irobex_cb *self, __u8 dlsap) 
{
	/* struct irobex_cb *self = irobex; */
	struct notify_t notify;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);

	notify.data_indication = irobex_data_indication;
	notify.connect_confirm = irobex_connect_confirm;
	notify.connect_indication = irobex_connect_indication;
	notify.flow_indication = irobex_flow_indication;
	notify.disconnect_indication = irobex_disconnect_indication;
	notify.instance = self;
	
	/* Create TSAP's */
	self->tsap = irttp_open_tsap( LSAP_ANY, DEFAULT_INITIAL_CREDIT,
				      &notify);
	
/* 	DEBUG( 0, "OBEX allocated TSAP%d for data\n", self->handle); */
	
	/* irlan_do_event( IAS_PROVIDER_AVAIL, NULL, &frame); */
}

/*
 * Function irobex_register_server(void)
 *
 *    Register server support so we can accept incomming connections. We
 *    must register both a TSAP for control and data
 * 
 */
void irobex_register_server( struct irobex_cb *self)
{
	struct notify_t notify;
	struct ias_object *obj;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);

	irda_notify_init( &notify);

	notify.connect_confirm       = irobex_connect_confirm;
	notify.connect_indication    = irobex_connect_indication;
	notify.disconnect_indication = irobex_disconnect_indication;
	notify.data_indication       = irobex_data_indication;
	notify.flow_indication       = irobex_flow_indication;
	notify.instance = self;
	strcpy( notify.name, "IrOBEX");

	self->tsap = irttp_open_tsap( TSAP_IROBEX, DEFAULT_INITIAL_CREDIT,
				      &notify);	
	if ( self->tsap == NULL) {
		DEBUG( 0, __FUNCTION__ "(), Unable to allocate TSAP!\n");
		return;
	}

	/* 
	 *  Register with LM-IAS
	 */
	obj = irias_new_object( "OBEX", 0x42343);
	irias_add_integer_attrib( obj, "IrDA:TinyTP:LsapSel", TSAP_IROBEX);
	irias_insert_object( obj);
}

void irobex_watchdog_timer_expired( unsigned long data)
{
	struct irobex_cb *self = ( struct irobex_cb *) data;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IROBEX_MAGIC, return;);

	switch (self->state) {
	case OBEX_CONN:      /* FALLTROUGH */
	case OBEX_DISCOVER:  /* FALLTROUGH */
	case OBEX_QUERY:     /* FALLTROUGH */
		wake_up_interruptible( &self->write_wait);
		break;
	default:
		break;
	}
}

#ifdef CONFIG_PROC_FS
/*
 * Function irobex_proc_read (buf, start, offset, len, unused)
 *
 *    Give some info to the /proc file system
 */
static int irobex_proc_read( char *buf, char **start, off_t offset, 
			     int len, int unused)
{
 	struct irobex_cb *self;

	self = irobex;

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IROBEX_MAGIC, return -1;);

	len = 0;
	
	len += sprintf( buf+len, "ifname: %s ",self->devname);
	len += sprintf( buf+len, "state: %s ", irobex_state[ self->state]);
	len += sprintf( buf+len, "EOF: %s\n", self->eof ? "TRUE": "FALSE");
	
	return len;
}

#endif /* CONFIG_PROC_FS */

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("The Linux IrOBEX module"); 

/*
 * Function init_module (void)
 *
 *    Initialize the IrOBEX module, this function is called by the
 *    modprobe(1) program.
 */
int init_module(void) 
{
	irobex_init();
	
	return 0;
}

/*
 * Function cleanup_module (void)
 *
 *    Remove the IrOBEX module, this function is called by the rmmod(1)
 *    program
 */
void cleanup_module(void) 
{
	/* 
	 *  No need to check MOD_IN_USE, as sys_delete_module() checks. 
	 */
	
	/* Free some memory */
	irobex_cleanup();	
}

#endif /* MODULE */




