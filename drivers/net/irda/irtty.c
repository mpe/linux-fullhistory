/*********************************************************************
 *                
 * Filename:      irtty.c
 * Version:       1.0
 * Description:   IrDA line discipline implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Dec  9 21:18:38 1997
 * Modified at:   Mon Jan 18 15:32:03 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:       slip.c by Laurence Culhane,   <loz@holmes.demon.co.uk>
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

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irtty.h>
#include <net/irda/wrapper.h>
#include <net/irda/irlap.h>
#include <net/irda/timer.h>
#include <net/irda/irda_device.h>
#include <linux/kmod.h>

static hashbin_t *irtty = NULL;
static hashbin_t *dongles = NULL;

static struct tty_ldisc irda_ldisc;

static int irtty_hard_xmit( struct sk_buff *skb, struct device *dev);
static void irtty_wait_until_sent( struct irda_device *driver);
static int irtty_is_receiving( struct irda_device *idev);
static int irtty_net_init( struct device *dev);
static int irtty_net_open(struct device *dev);
static int irtty_net_close(struct device *dev);

static int  irtty_open( struct tty_struct *tty);
static void irtty_close( struct tty_struct *tty);
static int  irtty_ioctl( struct tty_struct *, void *, int, void *);
static int  irtty_receive_room( struct tty_struct *tty);
static void irtty_change_speed( struct irda_device *dev, int baud);
static void irtty_write_wakeup( struct tty_struct *tty);

static void irtty_receive_buf( struct tty_struct *, const unsigned char *, 
			       char *, int);
char *driver_name = "irtty";

__initfunc(int irtty_init(void))
{
	int status;
	
	irtty = hashbin_new( HB_LOCAL);
	if ( irtty == NULL) {
		printk( KERN_WARNING "IrDA: Can't allocate irtty hashbin!\n");
		return -ENOMEM;
	}

	dongles = hashbin_new( HB_LOCAL);
	if ( dongles == NULL) {
		printk( KERN_WARNING 
			"IrDA: Can't allocate dongles hashbin!\n");
		return -ENOMEM;
	}

	/* Fill in our line protocol discipline, and register it */
	memset( &irda_ldisc, 0, sizeof( irda_ldisc));

	irda_ldisc.magic = TTY_LDISC_MAGIC;
 	irda_ldisc.name  = "irda";
	irda_ldisc.flags = 0;
	irda_ldisc.open  = irtty_open;
	irda_ldisc.close = irtty_close;
	irda_ldisc.read  = NULL;
	irda_ldisc.write = NULL;
	irda_ldisc.ioctl = (int (*)(struct tty_struct *, struct file *,
				    unsigned int, unsigned long)) irtty_ioctl;
 	irda_ldisc.poll  = NULL;
	irda_ldisc.receive_buf  = irtty_receive_buf;
	irda_ldisc.receive_room = irtty_receive_room;
	irda_ldisc.write_wakeup = irtty_write_wakeup;
	
	if (( status = tty_register_ldisc( N_IRDA, &irda_ldisc)) != 0)  {
		printk( KERN_ERR 
			"IrDA: can't register line discipline (err = %d)\n", 
			status);
	}
	
	return status;
}

/* 
 *  Function irtty_cleanup ( )
 *
 *    Called when the irda module is removed. Here we remove all instances
 *    of the driver, and the master array.
 */
#ifdef MODULE
static void irtty_cleanup(void) 
{
	int ret;
	
	/*
	 *  Unregister tty line-discipline
	 */
	if (( ret = tty_register_ldisc( N_IRDA, NULL))) {
		printk( KERN_ERR 
			"IrTTY: can't unregister line discipline (err = %d)\n",
 			ret);
	}

	/*
	 *  The TTY should care of deallocating the instances by using the
	 *  callback to irtty_close(), therefore we do give any deallocation
	 *  function to hashbin_destroy().
	 */
	hashbin_delete( irtty, NULL);
	hashbin_delete( dongles, NULL);
}
#endif /* MODULE */

/* 
 *  Function irtty_open(tty)
 *
 *    This function is called by the TTY module when the IrDA line
 *    discipline is called for.  Because we are sure the tty line exists,
 *    we only have to link it to a free IrDA channel.  
 */
static int irtty_open( struct tty_struct *tty) 
{
	struct irtty_cb *self;
	char name[16];
	
	ASSERT( tty != NULL, return -EEXIST;);

	/* First make sure we're not already connected. */
	self = (struct irtty_cb *) tty->disc_data;
	if ( self != NULL && self->magic == IRTTY_MAGIC)
		return -EEXIST;
	
	/*
	 *  Allocate new instance of the driver
	 */
	self = kmalloc( sizeof(struct irtty_cb), GFP_KERNEL);
	if ( self == NULL) {
		printk( KERN_ERR "IrDA: Can't allocate memory for "
			"IrDA control block!\n");
		return -ENOMEM;
	}
	memset( self, 0, sizeof(struct irtty_cb));
	
	self->tty = tty;
	tty->disc_data = self;

	/* Give self a name */
	sprintf( name, "%s%d", tty->driver.name,
		 MINOR(tty->device) - tty->driver.minor_start +
		 tty->driver.name_base);

	/* hashbin_insert( irtty, (QUEUE*) self, 0, self->name); */
	hashbin_insert( irtty, (QUEUE*) self, (int) self, NULL);

	if (tty->driver.flush_buffer) {
		tty->driver.flush_buffer(tty);
	}

	if (tty->ldisc.flush_buffer) {
		tty->ldisc.flush_buffer(tty);
	}
	
	self->magic = IRTTY_MAGIC;

	/*
	 *  Initialize driver
	 */
	/* self->idev.flags |= SIR_MODE | IO_PIO; */
	self->idev.rx_buff.state = OUTSIDE_FRAME;

	/* 
	 *  Initialize QoS capabilities, we fill in all the stuff that
	 *  we support. Be careful not to place any restrictions on values
	 *  that are not device dependent (such as link disconnect time) so
	 *  this parameter can be set by IrLAP (or the user) instead. DB
	 */
	irda_init_max_qos_capabilies( &self->idev.qos);

	/* The only value we must override it the baudrate */
	self->idev.qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200;
	self->idev.qos.min_turn_time.bits = 0x03;
	irda_qos_bits_to_value( &self->idev.qos);

	/* Specify which buffer allocation policy we need */
	self->idev.rx_buff.flags = GFP_KERNEL;
	self->idev.tx_buff.flags = GFP_KERNEL;

	/* Specify how much memory we want */
	self->idev.rx_buff.truesize = 4000; 
	self->idev.tx_buff.truesize = 4000;

	/* Initialize callbacks */
	self->idev.change_speed    = irtty_change_speed;
 	self->idev.is_receiving    = irtty_is_receiving;
	/* self->idev.is_tbusy        = irtty_is_tbusy; */
	self->idev.wait_until_sent = irtty_wait_until_sent;

	/* Override the network functions we need to use */
	self->idev.netdev.init            = irtty_net_init;
	self->idev.netdev.hard_start_xmit = irtty_hard_xmit;
	self->idev.netdev.open            = irtty_net_open;
	self->idev.netdev.stop            = irtty_net_close;

	/* Open the IrDA device */
	irda_device_open( &self->idev, name, self);

	MOD_INC_USE_COUNT;

	return 0;
}

/* 
 *  Function irtty_close ( tty)
 *
 *    Close down a IrDA channel. This means flushing out any pending queues,
 *    and then restoring the TTY line discipline to what it was before it got
 *    hooked to IrDA (which usually is TTY again).  
 */
static void irtty_close( struct tty_struct *tty) 
{
	struct irtty_cb *self = (struct irtty_cb *) tty->disc_data;
	
	/* First make sure we're connected. */
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);

	/* We are not using any dongle anymore! */
	if ( self->dongle_q)
		self->dongle_q->dongle->close( &self->idev);

	/* Remove driver */
	irda_device_close( &self->idev);

	/* Stop tty */
	tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
	tty->disc_data = 0;

	self->tty = NULL;
	self->magic = 0;
	
	/* hashbin_remove( irtty, 0, self->name); */
	self = hashbin_remove( irtty, (int) self, NULL);
	
	if ( self != NULL)
		kfree( self);

 	MOD_DEC_USE_COUNT;

	DEBUG( 4, "IrTTY: close() -->\n");
}

/* 
 *  Function irtty_change_speed ( self, baud)
 *
 *    Change the speed of the serial port. The driver layer must check that
 *    all transmission has finished using the irtty_wait_until_sent() 
 *    function.
 */
static void irtty_change_speed( struct irda_device *idev, int baud) 
{
        struct termios old_termios;
	struct irtty_cb *self;
	int cflag;

	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);

	self = (struct irtty_cb *) idev->priv;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);

	old_termios = *(self->tty->termios);
	cflag = self->tty->termios->c_cflag;

	cflag &= ~CBAUD;

	DEBUG( 4, __FUNCTION__ "(), Setting speed to %d\n", baud);

	switch( baud) {
	case 1200:
		cflag |= B1200;
		break;
	case 2400:
		cflag |= B2400;
		break;
	case 4800:
		cflag |= B4800;
		break;
	case 19200:
		cflag |= B19200;
		break;
	case 38400:
		cflag |= B38400;
		break;
	case 57600:
		cflag |= B57600;
		break;
	case 115200:
		cflag |= B115200;
		break;
	case 9600:
	default:
		cflag |= B9600;
		break;
	}	

	self->tty->termios->c_cflag = cflag;
	self->tty->driver.set_termios( self->tty, &old_termios);
}

/*
 * Function irtty_init_dongle (self, type)
 *
 *    Initialize attached dongle. Warning, must be called with a process
 *    context!
 */
static void irtty_init_dongle( struct irtty_cb *self, int type)
{
	struct dongle_q *node;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);

#ifdef CONFIG_KMOD
	/* Try to load the module needed */
	switch( type) {
	case ESI_DONGLE:
		DEBUG( 0, __FUNCTION__ "(), ESI dongle!\n");
		request_module( "esi");
		break;
	case TEKRAM_DONGLE:
		DEBUG( 0, __FUNCTION__ "(), Tekram dongle!\n");
		request_module( "tekram");
		break;
	case ACTISYS_DONGLE:
		DEBUG( 0, __FUNCTION__ "(), ACTiSYS dongle!\n");
		request_module( "actisys");
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown dongle type!\n");
		return;
		break;
	}
#endif /* CONFIG_KMOD */

	node = hashbin_find( dongles, type, NULL);
	if ( !node) {
		DEBUG( 0, __FUNCTION__ 
		       "(), Unable to find requested dongle\n");
		return;
	}
	self->dongle_q = node;

	/* Use this change speed function instead of the default */
	self->idev.change_speed = node->dongle->change_speed;

	/*
	 * Now initialize the dongle!
	 */
	node->dongle->open( &self->idev, type);
	node->dongle->qos_init( &self->idev, &self->idev.qos);
	
	/* Reset dongle */
	node->dongle->reset( &self->idev, 0);

	/* Set to default baudrate */
	node->dongle->change_speed( &self->idev, 9600);
}

/*
 * Function irtty_ioctl (tty, file, cmd, arg)
 *
 *     The Swiss army knife of system calls :-)
 *
 */
static int irtty_ioctl( struct tty_struct *tty, void *file, int cmd, 
			void *arg) 
{
	struct irtty_cb *self;
	int err = 0;
	int size = _IOC_SIZE(cmd);

	self = (struct irtty_cb *) tty->disc_data;

	ASSERT( self != NULL, return -ENODEV;);
	ASSERT( self->magic == IRTTY_MAGIC, return -EBADR;);

	if ( _IOC_DIR(cmd) & _IOC_READ)
		err = verify_area( VERIFY_WRITE, (void *) arg, size);
	else if ( _IOC_DIR(cmd) & _IOC_WRITE)
		err = verify_area( VERIFY_READ, (void *) arg, size);
	if ( err)
		return err;
	
	switch(cmd) {
	case TCGETS:
	case TCGETA:
		return n_tty_ioctl( tty, (struct file *) file, cmd, 
				    (unsigned long) arg);
		break;
	case IRTTY_IOCTDONGLE:
		/* Initialize dongle */
		irtty_init_dongle( self, (int) arg);
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

/* 
 *  Function irtty_receive_buf( tty, cp, count)
 *
 *    Handle the 'receiver data ready' interrupt.  This function is called
 *    by the 'tty_io' module in the kernel when a block of IrDA data has
 *    been received, which can now be decapsulated and delivered for
 *    further processing 
 */
static void irtty_receive_buf( struct tty_struct *tty, const unsigned 
			       char *cp, char *fp, int count) 
{
	struct irtty_cb *self = (struct irtty_cb *) tty->disc_data;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);
	
	/* Read the characters out of the buffer */
 	while (count--) {
		/* 
		 *  Characters received with a parity error, etc?
		 */
 		if (fp && *fp++) { 
			DEBUG( 0, "Framing or parity error!\n");
			irda_device_set_media_busy( &self->idev, TRUE);
			/* sl->rx_errors++; */
 			cp++;
 			continue;
 		}
		/*
		 *  Unwrap and destuff one byte
		 */
		async_unwrap_char( &self->idev, *cp++);
		/* self->rx_over_errors++; */
	}
}

/*
 * Function irtty_hard_xmit (skb, dev)
 *
 *    Transmit skb
 *
 */
static int irtty_hard_xmit( struct sk_buff *skb, struct device *dev)
{
	struct irtty_cb *self;
	struct irda_device *idev;
	int actual = 0;

	ASSERT( dev != NULL, return 0;);
	ASSERT( skb != NULL, return 0;);

	if ( dev->tbusy) {
		DEBUG( 4, __FUNCTION__ "(), tbusy==TRUE\n");
		
		return -EBUSY;
	}

	idev = (struct irda_device *) dev->priv;

	ASSERT( idev != NULL, return 0;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return -1;);
	
	self = (struct irtty_cb *) idev->priv;

	ASSERT( self != NULL, return 0;);
	ASSERT( self->magic == IRTTY_MAGIC, return 0;);

	/* Lock transmit buffer */
	if ( irda_lock( (void *) &dev->tbusy) == FALSE)
		return 0;

        /*  
	 *  Transfer skb to tx_buff while wrapping, stuffing and making CRC 
	 */
        idev->tx_buff.len = async_wrap_skb( skb, idev->tx_buff.data, 
					    idev->tx_buff.truesize); 

	self->tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);

	dev->trans_start = jiffies;

	if ( self->tty->driver.write)
		actual = self->tty->driver.write( self->tty, 0, 
						  idev->tx_buff.data, 
						  idev->tx_buff.len);

	idev->tx_buff.offset = actual;
	idev->tx_buff.head = idev->tx_buff.data + actual;
#if 0
	/* 
	 *  Did we transmit the whole frame? Commented out for now since
	 *  I must check if this optimalization really works. DB.
	 */
 	if (( idev->tx.count - idev->tx.ptr) <= 0) {
 		DEBUG( 4, "irtty_xmit_buf: finished with frame!\n");
 		self->tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
 		irda_unlock( &self->tbusy);
 	}
#endif

	dev_kfree_skb( skb);

	return 0;
}

/*
 * Function irtty_receive_room (tty)
 *
 *    Used by the TTY to find out how much data we can receive at a time
 * 
*/
static int irtty_receive_room( struct tty_struct *tty) 
{
	return 65536;  /* We can handle an infinite amount of data. :-) */
}

/*
 * Function irtty_write_wakeup (tty)
 *
 *    Called by the driver when there's room for more data.  If we have
 *    more packets to send, we send them here.
 *
 */
static void irtty_write_wakeup( struct tty_struct *tty) 
{
	int actual = 0, count;
	struct irtty_cb *self = (struct irtty_cb *) tty->disc_data;
	struct irda_device *idev;
	
	/* 
	 *  First make sure we're connected. 
	 */
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);

	idev = &self->idev;

	/*
	 *  Finished with frame?
	 */
	if ( idev->tx_buff.offset == idev->tx_buff.len)  {

		/* 
		 *  Now serial buffer is almost free & we can start 
		 *  transmission of another packet 
		 */
		DEBUG( 4, __FUNCTION__ "(), finished with frame!\n");

		tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);

		idev->netdev.tbusy = 0; /* Unlock */
		idev->stats.tx_packets++;
		idev->stats.tx_bytes += idev->tx_buff.len;

		/* Tell network layer that we want more frames */
		mark_bh( NET_BH);

		return;
	}
	/*
	 *  Write data left in transmit buffer
	 */
	count = idev->tx_buff.len - idev->tx_buff.offset;
	actual = tty->driver.write( tty, 0, idev->tx_buff.head, count);
	idev->tx_buff.offset += actual;
	idev->tx_buff.head += actual;

	DEBUG( 4, "actual=%d, sent %d\n", actual, count);
}

/*
 * Function irtty_is_receiving (idev)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int irtty_is_receiving( struct irda_device *idev)
{
	return ( idev->rx_buff.state != OUTSIDE_FRAME);
}

/*
 * Function irtty_change_speed_ready (idev)
 *
 *    Are we completely finished with transmitting frames so its possible
 *    to change the speed of the serial port. Warning this function must
 *    be called with a process context!
 */
static void irtty_wait_until_sent( struct irda_device *idev)
{
	struct irtty_cb *self = (struct irtty_cb *) idev->priv;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);
	
	DEBUG( 4, "Chars in buffer %d\n", 
	       self->tty->driver.chars_in_buffer( self->tty));
	
	tty_wait_until_sent( self->tty, 0);
}

int irtty_register_dongle( struct dongle *dongle)
{
	struct dongle_q *new;
	
	/* Check if this compressor has been registred before */
	if ( hashbin_find ( dongles, dongle->type, NULL)) {
		DEBUG( 0, __FUNCTION__ "(), Dongle already registered\n");
                return 0;
        }
	
	/* Make new IrDA dongle */
        new = (struct dongle_q *) kmalloc (sizeof (struct dongle_q), 
					      GFP_KERNEL);
        if (new == NULL) {
                return 1;
		
        }
	memset( new, 0, sizeof( struct dongle_q));
        new->dongle = dongle;

	/* Insert IrDA compressor into hashbin */
	hashbin_insert( dongles, (QUEUE *) new, dongle->type, NULL);
	
        return 0;
}

void irtty_unregister_dongle( struct dongle *dongle)
{
	struct dongle_q *node;

	node = hashbin_remove( dongles, dongle->type, NULL);
	if ( !node) {
		DEBUG( 0, __FUNCTION__ "(), dongle not found!\n");
		return;
	}
	kfree( node);
}

static int irtty_net_init( struct device *dev)
{
	/* Set up to be a normal IrDA network device driver */
	irda_device_setup( dev);

	/* Insert overrides below this line! */

	return 0;
}


static int irtty_net_open( struct device *dev)
{
	ASSERT( dev != NULL, return -1;);

	/* Ready to play! */
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	MOD_INC_USE_COUNT;

	return 0;
}

static int irtty_net_close(struct device *dev)
{
	ASSERT( dev != NULL, return -1;);
	
	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	MOD_DEC_USE_COUNT;

	return 0;
}

#ifdef MODULE

/*
 * Function init_module (void)
 *
 *    Initialize IrTTY module
 *
 */
int init_module(void)
{
	irtty_init();
	return(0);
}

/*
 * Function cleanup_module (void)
 *
 *    Cleanup IrTTY module
 *
 */
void cleanup_module(void)
{
	irtty_cleanup();
}

#endif /* MODULE */







