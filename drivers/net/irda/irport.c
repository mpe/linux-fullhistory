/*********************************************************************
 * 
 * Filename:	  irport.c
 * Version:	  1.0
 * Description:   Half duplex serial port SIR driver for IrDA. 
 * Status:	  Experimental.
 * Author:	  Dag Brattli <dagb@cs.uit.no>
 * Created at:	  Sun Aug  3 13:49:59 1997
 * Modified at:   Wed Aug 11 09:24:46 1999
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

static struct irda_device *dev_self[] = { NULL, NULL, NULL, NULL};
static char *driver_name = "irport";

static int irport_open(int i, unsigned int iobase, unsigned int irq);
static int irport_close(struct irda_device *idev);

static void irport_write_wakeup(struct irda_device *idev);
static int  irport_write(int iobase, int fifo_size, __u8 *buf, int len);
static void irport_receive(struct irda_device *idev);

static int  irport_net_init(struct net_device *dev);
static int  irport_net_open(struct net_device *dev);
static int  irport_net_close(struct net_device *dev);
static int  irport_is_receiving(struct irda_device *idev);
static void irport_set_dtr_rts(struct irda_device *idev, int dtr, int rts);
static int  irport_raw_write(struct irda_device *idev, __u8 *buf, int len);

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

        DEBUG( 4, __FUNCTION__ "()\n");

	for (i=0; i < 4; i++) {
 		if (dev_self[i])
 			irport_close(dev_self[i]);
 	}
}
#endif /* MODULE */

static int irport_open(int i, unsigned int iobase, unsigned int irq)
{
	struct irda_device *idev;
	int ret;

	DEBUG( 0, __FUNCTION__ "()\n");

/* 	if (irport_probe(iobase, irq) == -1) */
/* 		return -1; */

	/*
	 *  Allocate new instance of the driver
	 */
	idev = kmalloc(sizeof(struct irda_device), GFP_KERNEL);
	if (idev == NULL) {
		printk( KERN_ERR "IrDA: Can't allocate memory for "
			"IrDA control block!\n");
		return -ENOMEM;
	}
	memset(idev, 0, sizeof(struct irda_device));
   
	/* Need to store self somewhere */
	dev_self[i] = idev;

	/* Initialize IO */
	idev->io.iobase2   = iobase;
        idev->io.irq2      = irq;
        idev->io.io_ext    = IO_EXTENT;
        idev->io.fifo_size = 16;

	idev->netdev.base_addr = iobase;
	idev->netdev.irq = irq;

	/* Lock the port that we need */
	ret = check_region(idev->io.iobase2, idev->io.io_ext);
	if (ret < 0) { 
		DEBUG(0, __FUNCTION__ "(), can't get iobase of 0x%03x\n",
		      idev->io.iobase2);
		/* irport_cleanup(self->idev);  */
		return -ENODEV;
	}
	request_region(idev->io.iobase2, idev->io.io_ext, idev->name);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&idev->qos);
	
	idev->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200;

	idev->qos.min_turn_time.bits = qos_mtt_bits;
	irda_qos_bits_to_value(&idev->qos);
	
	idev->flags = IFF_SIR|IFF_PIO;

	/* Specify which buffer allocation policy we need */
	idev->rx_buff.flags = GFP_KERNEL;
	idev->tx_buff.flags = GFP_KERNEL;

	idev->rx_buff.truesize = 4000; 
	idev->tx_buff.truesize = 4000;
	
	/* Initialize callbacks */
	idev->change_speed    = irport_change_speed;
	idev->wait_until_sent = irport_wait_until_sent;
        idev->is_receiving    = irport_is_receiving;
	idev->set_dtr_rts     = irport_set_dtr_rts;
	idev->raw_write       = irport_raw_write;

	/* Override the network functions we need to use */
	idev->netdev.init            = irport_net_init;
	idev->netdev.hard_start_xmit = irport_hard_xmit;
	idev->netdev.open            = irport_net_open;
	idev->netdev.stop            = irport_net_close;

	/* Open the IrDA device */
	irda_device_open(idev, driver_name, NULL);
	
	return 0;
}

static int irport_close(struct irda_device *idev)
{
	ASSERT(idev != NULL, return -1;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return -1;);

	/* Release the IO-port that this driver is using */
	DEBUG(0 , __FUNCTION__ "(), Releasing Region %03x\n", 
	      idev->io.iobase2);
	release_region(idev->io.iobase2, idev->io.io_ext);

	irda_device_close(idev);

	kfree(idev);

	return 0;
}

void irport_start(struct irda_device *idev, int iobase)
{
	unsigned long flags;

	spin_lock_irqsave(&idev->lock, flags);

	irport_stop(idev, iobase);

	/* Initialize UART */
	outb(UART_LCR_WLEN8, iobase+UART_LCR);  /* Reset DLAB */
	outb((UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2), iobase+UART_MCR);
	
	/* Turn on interrups */
	outb(UART_IER_RLSI | UART_IER_RDI |UART_IER_THRI, iobase+UART_IER);

	spin_unlock_irqrestore(&idev->lock, flags);
}

void irport_stop(struct irda_device *idev, int iobase)
{
	unsigned long flags;

	spin_lock_irqsave(&idev->lock, flags);

	/* Reset UART */
	outb(0, iobase+UART_MCR);
	
	/* Turn off interrupts */
	outb(0, iobase+UART_IER);

	spin_unlock_irqrestore(&idev->lock, flags);
}

/*
 * Function irport_probe (void)
 *
 *    Start IO port 
 *
 */
int irport_probe(int iobase)
{
	DEBUG(4, __FUNCTION__ "(), iobase=%#x\n", iobase);

	return 0;
}

/*
 * Function irport_change_speed (idev, speed)
 *
 *    Set speed of IrDA port to specified baudrate
 *
 */
void irport_change_speed(struct irda_device *idev, __u32 speed)
{
	unsigned long flags;
	int iobase; 
	int fcr;    /* FIFO control reg */
	int lcr;    /* Line control reg */
	int divisor;

	DEBUG(0, __FUNCTION__ "(), Setting speed to: %d\n", speed);

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);

	iobase = idev->io.iobase2;
	
	/* Update accounting for new speed */
	idev->io.baudrate = speed;

	spin_lock_irqsave(&idev->lock, flags);

	/* Turn off interrupts */
	outb(0, iobase+UART_IER); 

	divisor = SPEED_MAX/speed;
	
	fcr = UART_FCR_ENABLE_FIFO;

	/* 
	 * Use trigger level 1 to avoid 3 ms. timeout delay at 9600 bps, and
	 * almost 1,7 ms at 19200 bps. At speeds above that we can just forget
	 * about this timeout since it will always be fast enough. 
	 */
	if (idev->io.baudrate < 38400)
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

	spin_unlock_irqrestore(&idev->lock, flags);
}

/*
 * Function irport_write_wakeup (tty)
 *
 *    Called by the driver when there's room for more data.  If we have
 *    more packets to send, we send them here.
 *
 */
static void irport_write_wakeup(struct irda_device *idev)
{
	int actual = 0;
	int iobase;
	int fcr;

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);

	DEBUG(4, __FUNCTION__ "()\n");

	/* Finished with frame?  */
	if (idev->tx_buff.len > 0)  {
		/* Write data left in transmit buffer */
		actual = irport_write(idev->io.iobase2, idev->io.fifo_size, 
				      idev->tx_buff.data, idev->tx_buff.len);
		idev->tx_buff.data += actual;
		idev->tx_buff.len  -= actual;
	} else {
		iobase = idev->io.iobase2;
		/* 
		 *  Now serial buffer is almost free & we can start 
		 *  transmission of another packet 
		 */
		idev->netdev.tbusy = 0; /* Unlock */
		idev->stats.tx_packets++;

		/* Schedule network layer, so we can get some more frames */
		mark_bh(NET_BH);

		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR;

		if (idev->io.baudrate < 38400)
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
		DEBUG(0, __FUNCTION__ "(), failed, fifo not empty!\n");
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
	struct irda_device *idev;
	unsigned long flags;
	int actual = 0;
	int iobase;

	ASSERT(dev != NULL, return 0;);
	
	idev = (struct irda_device *) dev->priv;

	ASSERT(idev != NULL, return 0;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return 0;);

	iobase = idev->io.iobase2;

	/* Lock transmit buffer */
	if (irda_lock((void *) &dev->tbusy) == FALSE) {
		int tickssofar = jiffies - dev->trans_start;
		if ((tickssofar < 5) || !dev->start)
			return -EBUSY;

		WARNING("%s: transmit timed out\n", dev->name);
		irport_start(idev, iobase);
		irport_change_speed(idev, idev->io.baudrate);

		dev->trans_start = jiffies;
	}

	spin_lock_irqsave(&idev->lock, flags);

	/* Init tx buffer */
	idev->tx_buff.data = idev->tx_buff.head;

        /* Copy skb to tx_buff while wrapping, stuffing and making CRC */
	idev->tx_buff.len = async_wrap_skb(skb, idev->tx_buff.data, 
					   idev->tx_buff.truesize);
	
	idev->tx_buff.data += actual;
	idev->tx_buff.len  -= actual;

	/* Turn on transmit finished interrupt. Will fire immediately!  */
	outb(UART_IER_THRI, iobase+UART_IER); 

	spin_unlock_irqrestore(&idev->lock, flags);

	dev_kfree_skb(skb);
	
	return 0;
}
        
/*
 * Function irport_receive (idev)
 *
 *    Receive one frame from the infrared port
 *
 */
static void irport_receive(struct irda_device *idev) 
{
	int iobase;
	int boguscount = 0;

	ASSERT(idev != NULL, return;);

	iobase = idev->io.iobase2;

	/*  
	 * Receive all characters in Rx FIFO, unwrap and unstuff them. 
         * async_unwrap_char will deliver all found frames  
	 */
	do {
		async_unwrap_char(idev, inb(iobase+UART_RX));

		/* Make sure we don't stay here to long */
		if (boguscount++ > 32) {
			DEBUG(0,__FUNCTION__ "(), breaking!\n");
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
	struct irda_device *idev = (struct irda_device *) dev_id;
	int iobase;
	int iir, lsr;
	int boguscount = 0;

	if (!idev) {
		WARNING(__FUNCTION__ "() irq %d for unknown device.\n", irq);
		return;
	}

	spin_lock(&idev->lock);

	idev->netdev.interrupt = 1;

	iobase = idev->io.iobase2;

	iir = inb(iobase+UART_IIR) & UART_IIR_ID;
	while (iir) {
		/* Clear interrupt */
		lsr = inb(iobase+UART_LSR);

		DEBUG(4, __FUNCTION__ "(), iir=%02x, lsr=%02x, iobase=%#x\n", 
		      iir, lsr, iobase);

		switch (iir) {
		case UART_IIR_RLSI:
			DEBUG(2, __FUNCTION__ "(), RLSI\n");
			break;
		case UART_IIR_RDI:
			//if (lsr & UART_LSR_DR)
				/* Receive interrupt */
				irport_receive(idev);
			break;
		case UART_IIR_THRI:
			if (lsr & UART_LSR_THRE)
				/* Transmitter ready for data */
				irport_write_wakeup(idev);
			break;
		default:
			DEBUG(0, __FUNCTION__ "(), unhandled IIR=%#x\n", iir);
			break;
		} 
		
		/* Make sure we don't stay here to long */
		if (boguscount++ > 100)
			break;

 	        iir = inb(iobase + UART_IIR) & UART_IIR_ID;
	}
	idev->netdev.interrupt = 0;

	spin_unlock(&idev->lock);
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
	struct irda_device *idev;
	int iobase;

	ASSERT(dev != NULL, return -1;);
	idev = (struct irda_device *) dev->priv;

	iobase = idev->io.iobase2;

	if (request_irq(idev->io.irq2, irport_interrupt, 0, idev->name, 
			(void *) idev))
		return -EAGAIN;

	irport_start(idev, iobase);
	irda_device_net_open(dev);

	/* Change speed to make sure dongles follow us again */
	if (idev->change_speed)
		idev->change_speed(idev, 9600);

	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * Function irport_net_close (idev)
 *
 *    
 *
 */
static int irport_net_close(struct net_device *dev)
{
	struct irda_device *idev;
	int iobase;

	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(dev != NULL, return -1;);
	idev = (struct irda_device *) dev->priv;

	ASSERT(idev != NULL, return -1;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return -1;)

	iobase = idev->io.iobase2;

	irda_device_net_close(dev);
	irport_stop(idev, iobase);

	free_irq(idev->io.irq2, idev);

	MOD_DEC_USE_COUNT;

	return 0;
}

/*
 * Function irport_wait_until_sent (idev)
 *
 *    Delay exectution until finished transmitting
 *
 */
void irport_wait_until_sent(struct irda_device *idev)
{
	int iobase;

	iobase = idev->io.iobase2;

	/* Wait until Tx FIFO is empty */
	while (!(inb(iobase+UART_LSR) & UART_LSR_THRE)) {
		DEBUG(2, __FUNCTION__ "(), waiting!\n");
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(MSECS_TO_JIFFIES(60));
	}
}

/*
 * Function irport_is_receiving (idev)
 *
 *    Returns true is we are currently receiving data
 *
 */
static int irport_is_receiving(struct irda_device *idev)
{
	return (idev->rx_buff.state != OUTSIDE_FRAME);
}

/*
 * Function irport_set_dtr_rts (tty, dtr, rts)
 *
 *    This function can be used by dongles etc. to set or reset the status
 *    of the dtr and rts lines
 */
static void irport_set_dtr_rts(struct irda_device *idev, int dtr, int rts)
{
	int iobase;

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);

	iobase = idev->io.iobase2;

	if (dtr)
		dtr = UART_MCR_DTR;
	if (rts)
		rts = UART_MCR_RTS;

	outb(dtr|rts|UART_MCR_OUT2, iobase+UART_MCR);
}

static int irport_raw_write(struct irda_device *idev, __u8 *buf, int len)
{
	int iobase;
	int actual = 0;

	ASSERT(idev != NULL, return -1;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return -1;);

	iobase = idev->io.iobase2;

	/* Tx FIFO should be empty! */
	if (!(inb(iobase+UART_LSR) & UART_LSR_THRE)) {
		DEBUG( 0, __FUNCTION__ "(), failed, fifo not empty!\n");
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

/*
 * Function cleanup_module (void)
 *
 *    
 *
 */
void cleanup_module(void)
{
	irport_cleanup();
}

/*
 * Function init_module (void)
 *
 *    
 */
int init_module(void)
{
	return irport_init();
}

#endif /* MODULE */

