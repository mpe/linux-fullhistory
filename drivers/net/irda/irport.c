/*********************************************************************
 * 
 * Filename:	  irport.c
 * Version:	  1.0
 * Description:   Half duplex serial port SIR driver for IrDA. 
 * Status:	  Experimental.
 * Author:	  Dag Brattli <dagb@cs.uit.no>
 * Created at:	  Sun Aug  3 13:49:59 1997
 * Modified at:   Wed Oct 20 00:07:42 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:	  serial.c by Linus Torvalds 
 * 
 *     Copyright (c) 1997, 1998, 1999 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *
 *     This driver is ment to be a small half duplex serial driver to be
 *     used for IR-chipsets that has a UART (16550) compatibility mode. 
 *     Eventually it will replace irtty, because of irtty has some 
 *     problems that is hard to get around when we don't have control
 *     over the serial driver. This driver may also be used by FIR 
 *     drivers to handle SIR mode for them.
 *
 ********************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/serial_reg.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/rtnetlink.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/wrapper.h>
#include <net/irda/irport.h>

#define IO_EXTENT 8

/* 
 * Currently you'll need to set these values using insmod like this:
 * insmod irport io=0x3e8 irq=11
 */
static unsigned int io[]  = { ~0, ~0, ~0, ~0 };
static unsigned int irq[] = { 0, 0, 0, 0 };

static unsigned int qos_mtt_bits = 0x03;

static struct irport_cb *dev_self[] = { NULL, NULL, NULL, NULL};
static char *driver_name = "irport";

static int irport_open(int i, unsigned int iobase, unsigned int irq);
static int irport_close(struct irport_cb *self);

static void irport_write_wakeup(struct irport_cb *self);
static int  irport_write(int iobase, int fifo_size, __u8 *buf, int len);
static void irport_receive(struct irport_cb *self);

static int  irport_net_init(struct net_device *dev);
static int  irport_net_open(struct net_device *dev);
static int  irport_net_close(struct net_device *dev);
static int  irport_is_receiving(struct irport_cb *self);
static void irport_set_dtr_rts(struct net_device *dev, int dtr, int rts);
static int  irport_raw_write(struct net_device *dev, __u8 *buf, int len);

int __init irport_init(void)
{
 	int i;

 	for (i=0; (io[i] < 2000) && (i < 4); i++) {
 		int ioaddr = io[i];
 		if (check_region(ioaddr, IO_EXTENT))
 			continue;
 		if (irport_open(i, io[i], irq[i]) == 0)
 			return 0;
 	}
	/* 
	 * Maybe something failed, but we can still be usable for FIR drivers 
	 */
 	return 0;
}

/*
 * Function irport_cleanup ()
 *
 *    Close all configured ports
 *
 */
#ifdef MODULE
static void irport_cleanup(void)
{
 	int i;

        IRDA_DEBUG( 4, __FUNCTION__ "()\n");

	for (i=0; i < 4; i++) {
 		if (dev_self[i])
 			irport_close(dev_self[i]);
 	}
}
#endif /* MODULE */

static int irport_open(int i, unsigned int iobase, unsigned int irq)
{
	struct net_device *dev;
	struct irport_cb *self;
	int ret;
	int err;

	IRDA_DEBUG(0, __FUNCTION__ "()\n");

	/*
	 *  Allocate new instance of the driver
	 */
	self = kmalloc(sizeof(struct irport_cb), GFP_KERNEL);
	if (!self) {
		ERROR(__FUNCTION__ "(), can't allocate memory for "
		      "control block!\n");
		return -ENOMEM;
	}
	memset(self, 0, sizeof(struct irport_cb));
   
	spin_lock_init(&self->lock);

	/* Need to store self somewhere */
	dev_self[i] = self;

	/* Initialize IO */
	self->io.iobase2   = iobase;
        self->io.irq2      = irq;
        self->io.io_ext    = IO_EXTENT;
        self->io.fifo_size = 16;

	/* Lock the port that we need */
	ret = check_region(self->io.iobase2, self->io.io_ext);
	if (ret < 0) { 
		IRDA_DEBUG(0, __FUNCTION__ "(), can't get iobase of 0x%03x\n",
		      self->io.iobase2);
		/* irport_cleanup(self->self);  */
		return -ENODEV;
	}
	request_region(self->io.iobase2, self->io.io_ext, driver_name);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&self->qos);
	
	self->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200;

	self->qos.min_turn_time.bits = qos_mtt_bits;
	irda_qos_bits_to_value(&self->qos);
	
	self->flags = IFF_SIR|IFF_PIO;

	/* Specify how much memory we want */
	self->rx_buff.truesize = 4000; 
	self->tx_buff.truesize = 4000;
	
	/* Allocate memory if needed */
	if (self->rx_buff.truesize > 0) {
		self->rx_buff.head = (__u8 *) kmalloc(self->rx_buff.truesize,
						      GFP_KERNEL);
		if (self->rx_buff.head == NULL)
			return -ENOMEM;
		memset(self->rx_buff.head, 0, self->rx_buff.truesize);
	}
	if (self->tx_buff.truesize > 0) {
		self->tx_buff.head = (__u8 *) kmalloc(self->tx_buff.truesize, 
						      GFP_KERNEL);
		if (self->tx_buff.head == NULL) {
			kfree(self->rx_buff.head);
			return -ENOMEM;
		}
		memset(self->tx_buff.head, 0, self->tx_buff.truesize);
	}	
	self->rx_buff.in_frame = FALSE;
	self->rx_buff.state = OUTSIDE_FRAME;
	self->tx_buff.data = self->tx_buff.head;
	self->rx_buff.data = self->rx_buff.head;
	self->mode = IRDA_IRLAP;

	if (!(dev = dev_alloc("irda%d", &err))) {
		ERROR(__FUNCTION__ "(), dev_alloc() failed!\n");
		return -ENOMEM;
	}
	/* dev_alloc doesn't clear the struct, so lets do a little hack */
	memset(((__u8*)dev)+sizeof(char*),0,sizeof(struct net_device)-sizeof(char*));

 	dev->priv = (void *) self;
	self->netdev = dev;

	/* Override the network functions we need to use */
	dev->init            = irport_net_init;
	dev->hard_start_xmit = irport_hard_xmit;
	dev->open            = irport_net_open;
	dev->stop            = irport_net_close;

	/* Make ifconfig display some details */
	dev->base_addr = iobase;
	dev->irq = irq;

	rtnl_lock();
	err = register_netdevice(dev);
	rtnl_unlock();
	if (err) {
		ERROR(__FUNCTION__ "(), register_netdev() failed!\n");
		return -1;
	}
	MESSAGE("IrDA: Registered device %s\n", dev->name);

	return 0;
}

static int irport_close(struct irport_cb *self)
{
	ASSERT(self != NULL, return -1;);

	/* We are not using any dongle anymore! */
	if (self->dongle)
		irda_device_dongle_cleanup(self->dongle);
	self->dongle = NULL;
	
	/* Remove netdevice */
	if (self->netdev) {
		rtnl_lock();
		unregister_netdevice(self->netdev);
		rtnl_unlock();
	}

	/* Release the IO-port that this driver is using */
	IRDA_DEBUG(0 , __FUNCTION__ "(), Releasing Region %03x\n", 
	      self->io.iobase2);
	release_region(self->io.iobase2, self->io.io_ext);

	if (self->tx_buff.head)
		kfree(self->tx_buff.head);
	
	if (self->rx_buff.head)
		kfree(self->rx_buff.head);
	
	kfree(self);

	return 0;
}

void irport_start(struct irport_cb *self, int iobase)
{
	unsigned long flags;

	spin_lock_irqsave(&self->lock, flags);

	irport_stop(self, iobase);

	/* Initialize UART */
	outb(UART_LCR_WLEN8, iobase+UART_LCR);  /* Reset DLAB */
	outb((UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2), iobase+UART_MCR);
	
	/* Turn on interrups */
	outb(UART_IER_RLSI | UART_IER_RDI |UART_IER_THRI, iobase+UART_IER);

	spin_unlock_irqrestore(&self->lock, flags);
}

void irport_stop(struct irport_cb *self, int iobase)
{
	unsigned long flags;

	spin_lock_irqsave(&self->lock, flags);

	/* Reset UART */
	outb(0, iobase+UART_MCR);
	
	/* Turn off interrupts */
	outb(0, iobase+UART_IER);

	spin_unlock_irqrestore(&self->lock, flags);
}

/*
 * Function irport_probe (void)
 *
 *    Start IO port 
 *
 */
int irport_probe(int iobase)
{
	IRDA_DEBUG(4, __FUNCTION__ "(), iobase=%#x\n", iobase);

	return 0;
}

/*
 * Function irport_change_speed (self, speed)
 *
 *    Set speed of IrDA port to specified baudrate
 *
 */
void irport_change_speed(struct irport_cb *self, __u32 speed)
{
	unsigned long flags;
	int iobase; 
	int fcr;    /* FIFO control reg */
	int lcr;    /* Line control reg */
	int divisor;

	IRDA_DEBUG(2, __FUNCTION__ "(), Setting speed to: %d\n", speed);

	ASSERT(self != NULL, return;);

	iobase = self->io.iobase2;
	
	/* Update accounting for new speed */
	self->io.speed = speed;

	spin_lock_irqsave(&self->lock, flags);

	/* Turn off interrupts */
	outb(0, iobase+UART_IER); 

	divisor = SPEED_MAX/speed;
	
	fcr = UART_FCR_ENABLE_FIFO;

	/* 
	 * Use trigger level 1 to avoid 3 ms. timeout delay at 9600 bps, and
	 * almost 1,7 ms at 19200 bps. At speeds above that we can just forget
	 * about this timeout since it will always be fast enough. 
	 */
	if (self->io.speed < 38400)
		fcr |= UART_FCR_TRIGGER_1;
	else 
		fcr |= UART_FCR_TRIGGER_14;
        
	/* IrDA ports use 8N1 */
	lcr = UART_LCR_WLEN8;
	
	outb(UART_LCR_DLAB | lcr, iobase+UART_LCR); /* Set DLAB */
	outb(divisor & 0xff,      iobase+UART_DLL); /* Set speed */
	outb(divisor >> 8,	  iobase+UART_DLM);
	outb(lcr,		  iobase+UART_LCR); /* Set 8N1	*/
	outb(fcr,		  iobase+UART_FCR); /* Enable FIFO's */

	/* Turn on interrups */
	outb(/*UART_IER_RLSI|*/UART_IER_RDI/*|UART_IER_THRI*/, iobase+UART_IER);

	spin_unlock_irqrestore(&self->lock, flags);
}

/*
 * Function irport_write_wakeup (tty)
 *
 *    Called by the driver when there's room for more data.  If we have
 *    more packets to send, we send them here.
 *
 */
static void irport_write_wakeup(struct irport_cb *self)
{
	int actual = 0;
	int iobase;
	int fcr;

	ASSERT(self != NULL, return;);

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	/* Finished with frame?  */
	if (self->tx_buff.len > 0)  {
		/* Write data left in transmit buffer */
		actual = irport_write(self->io.iobase2, self->io.fifo_size, 
				      self->tx_buff.data, self->tx_buff.len);
		self->tx_buff.data += actual;
		self->tx_buff.len  -= actual;
	} else {
		iobase = self->io.iobase2;
		/* 
		 *  Now serial buffer is almost free & we can start 
		 *  transmission of another packet 
		 */
		self->netdev->tbusy = 0; /* Unlock */
		self->stats.tx_packets++;

		/* Schedule network layer, so we can get some more frames */
		mark_bh(NET_BH);

		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR;

		if (self->io.speed < 38400)
			fcr |= UART_FCR_TRIGGER_1;
		else 
			fcr |= UART_FCR_TRIGGER_14;

		/* 
		 * Reset Rx FIFO to make sure that all reflected transmit data
		 * will be discarded
		 */
		outb(fcr, iobase+UART_FCR);

		/* Turn on receive interrupts */
		outb(/* UART_IER_RLSI| */UART_IER_RDI, iobase+UART_IER);
	}
}

/*
 * Function irport_write (driver)
 *
 *    Fill Tx FIFO with transmit data
 *
 */
static int irport_write(int iobase, int fifo_size, __u8 *buf, int len)
{
	int actual = 0;

	/* Tx FIFO should be empty! */
	if (!(inb(iobase+UART_LSR) & UART_LSR_THRE)) {
		IRDA_DEBUG(0, __FUNCTION__ "(), failed, fifo not empty!\n");
		return -1;
	}
        
	/* Fill FIFO with current frame */
	while ((fifo_size-- > 0) && (actual < len)) {
		/* Transmit next byte */
		outb(buf[actual], iobase+UART_TX);

		actual++;
	}
        
	return actual;
}

/*
 * Function irport_xmit (void)
 *
 *    Transmits the current frame until FIFO is full, then
 *    waits until the next transmitt interrupt, and continues until the
 *    frame is transmited.
 */
int irport_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct irport_cb *self;
	unsigned long flags;
	int actual = 0;
	int iobase;
	__u32 speed;

	ASSERT(dev != NULL, return 0;);
	
	self = (struct irport_cb *) dev->priv;

	ASSERT(self != NULL, return 0;);

	iobase = self->io.iobase2;

	/* Lock transmit buffer */
	if (irda_lock((void *) &dev->tbusy) == FALSE) {
		int tickssofar = jiffies - dev->trans_start;
		if ((tickssofar < 5) || !dev->start)
			return -EBUSY;

		WARNING("%s: transmit timed out\n", dev->name);
		irport_start(self, iobase);
		irport_change_speed(self, self->io.speed );

		dev->trans_start = jiffies;
	}

	/* Check if we need to change the speed */
	if ((speed = irda_get_speed(skb)) != self->io.speed)
		irport_change_speed(self, speed);

	spin_lock_irqsave(&self->lock, flags);

	/* Init tx buffer */
	self->tx_buff.data = self->tx_buff.head;

        /* Copy skb to tx_buff while wrapping, stuffing and making CRC */
	self->tx_buff.len = async_wrap_skb(skb, self->tx_buff.data, 
					   self->tx_buff.truesize);
	
	self->tx_buff.data += actual;
	self->tx_buff.len  -= actual;

	/* Turn on transmit finished interrupt. Will fire immediately!  */
	outb(UART_IER_THRI, iobase+UART_IER); 

	spin_unlock_irqrestore(&self->lock, flags);

	dev_kfree_skb(skb);
	
	return 0;
}
        
/*
 * Function irport_receive (self)
 *
 *    Receive one frame from the infrared port
 *
 */
static void irport_receive(struct irport_cb *self) 
{
	int boguscount = 0;
	int iobase;

	ASSERT(self != NULL, return;);

	iobase = self->io.iobase2;

	/*  
	 * Receive all characters in Rx FIFO, unwrap and unstuff them. 
         * async_unwrap_char will deliver all found frames  
	 */
	do {
		async_unwrap_char(self->netdev, &self->rx_buff, 
				  inb(iobase+UART_RX));

		/* Make sure we don't stay here to long */
		if (boguscount++ > 32) {
			IRDA_DEBUG(2,__FUNCTION__ "(), breaking!\n");
			break;
		}
	} while (inb(iobase+UART_LSR) & UART_LSR_DR);	
}

/*
 * Function irport_interrupt (irq, dev_id, regs)
 *
 *    Interrupt handler
 */
void irport_interrupt(int irq, void *dev_id, struct pt_regs *regs) 
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct irport_cb *self;
	int boguscount = 0;
	int iobase;
	int iir, lsr;

	if (!dev) {
		WARNING(__FUNCTION__ "() irq %d for unknown device.\n", irq);
		return;
	}
	self = (struct irport_cb *) dev->priv;

	spin_lock(&self->lock);

	dev->interrupt = 1;

	iobase = self->io.iobase2;

	iir = inb(iobase+UART_IIR) & UART_IIR_ID;
	while (iir) {
		/* Clear interrupt */
		lsr = inb(iobase+UART_LSR);

		IRDA_DEBUG(4, __FUNCTION__ "(), iir=%02x, lsr=%02x, iobase=%#x\n", 
		      iir, lsr, iobase);

		switch (iir) {
		case UART_IIR_RLSI:
			IRDA_DEBUG(2, __FUNCTION__ "(), RLSI\n");
			break;
		case UART_IIR_RDI:
			/* Receive interrupt */
			irport_receive(self);
			break;
		case UART_IIR_THRI:
			if (lsr & UART_LSR_THRE)
				/* Transmitter ready for data */
				irport_write_wakeup(self);
			break;
		default:
			IRDA_DEBUG(0, __FUNCTION__ "(), unhandled IIR=%#x\n", iir);
			break;
		} 
		
		/* Make sure we don't stay here to long */
		if (boguscount++ > 100)
			break;

 	        iir = inb(iobase + UART_IIR) & UART_IIR_ID;
	}
	dev->interrupt = 0;

	spin_unlock(&self->lock);
}

static int irport_net_init(struct net_device *dev)
{
	/* Set up to be a normal IrDA network device driver */
	irda_device_setup(dev);

	/* Insert overrides below this line! */

	return 0;
}

/*
 * Function irport_net_open (dev)
 *
 *    
 *
 */
static int irport_net_open(struct net_device *dev)
{
	struct irport_cb *self;
	int iobase;

	ASSERT(dev != NULL, return -1;);
	self = (struct irport_cb *) dev->priv;

	iobase = self->io.iobase2;

	if (request_irq(self->io.irq2, irport_interrupt, 0, dev->name, 
			(void *) dev))
		return -EAGAIN;

	irport_start(self, iobase);

	/* Ready to play! */
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/* 
	 * Open new IrLAP layer instance, now that everything should be
	 * initialized properly 
	 */
	self->irlap = irlap_open(dev, &self->qos);

	/* FIXME: change speed of dongle */

	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * Function irport_net_close (self)
 *
 *    
 *
 */
static int irport_net_close(struct net_device *dev)
{
	struct irport_cb *self;
	int iobase;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(dev != NULL, return -1;);
	self = (struct irport_cb *) dev->priv;

	ASSERT(self != NULL, return -1;);

	iobase = self->io.iobase2;

	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	/* Stop and remove instance of IrLAP */
	if (self->irlap)
		irlap_close(self->irlap);
	self->irlap = NULL;

	irport_stop(self, iobase);

	free_irq(self->io.irq2, dev);

	MOD_DEC_USE_COUNT;

	return 0;
}

/*
 * Function irport_wait_until_sent (self)
 *
 *    Delay exectution until finished transmitting
 *
 */
#if 0
void irport_wait_until_sent(struct irport_cb *self)
{
	int iobase;

	iobase = self->io.iobase2;

	/* Wait until Tx FIFO is empty */
	while (!(inb(iobase+UART_LSR) & UART_LSR_THRE)) {
		IRDA_DEBUG(2, __FUNCTION__ "(), waiting!\n");
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(MSECS_TO_JIFFIES(60));
	}
}
#endif

/*
 * Function irport_is_receiving (self)
 *
 *    Returns true is we are currently receiving data
 *
 */
static int irport_is_receiving(struct irport_cb *self)
{
	return (self->rx_buff.state != OUTSIDE_FRAME);
}

/*
 * Function irport_set_dtr_rts (tty, dtr, rts)
 *
 *    This function can be used by dongles etc. to set or reset the status
 *    of the dtr and rts lines
 */
static void irport_set_dtr_rts(struct net_device *dev, int dtr, int rts)
{
	struct irport_cb *self = dev->priv;
	int iobase;

	ASSERT(self != NULL, return;);

	iobase = self->io.iobase2;

	if (dtr)
		dtr = UART_MCR_DTR;
	if (rts)
		rts = UART_MCR_RTS;

	outb(dtr|rts|UART_MCR_OUT2, iobase+UART_MCR);
}

static int irport_raw_write(struct net_device *dev, __u8 *buf, int len)
{
	struct irport_cb *self = (struct irport_cb *) dev->priv;
	int actual = 0;
	int iobase;

	ASSERT(self != NULL, return -1;);

	iobase = self->io.iobase2;

	/* Tx FIFO should be empty! */
	if (!(inb(iobase+UART_LSR) & UART_LSR_THRE)) {
		IRDA_DEBUG( 0, __FUNCTION__ "(), failed, fifo not empty!\n");
		return -1;
	}
        
	/* Fill FIFO with current frame */
	while (actual < len) {
		/* Transmit next byte */
		outb(buf[actual], iobase+UART_TX);
		actual++;
	}

	return actual;
}

#ifdef MODULE
MODULE_PARM(io, "1-4i");
MODULE_PARM(irq, "1-4i");

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Half duplex serial driver for IrDA SIR mode");

void cleanup_module(void)
{
	irport_cleanup();
}

int init_module(void)
{
	return irport_init();
}
#endif /* MODULE */

