/*********************************************************************
 *		  
 * Filename:	  irport.c
 * Version:	  0.8
 * Description:   Serial driver for IrDA. 
 * Status:	  Experimental.
 * Author:	  Dag Brattli <dagb@cs.uit.no>
 * Created at:	  Sun Aug  3 13:49:59 1997
 * Modified at:   Sat May 23 23:15:20 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:	  serial.c by Linus Torvalds 
 *		  serial_serial.c by Aage Kvalnes <aage@cs.uit.no>
 * 
 *     Copyright (c) 1997,1998 Dag Brattli <dagb@cs.uit.no>
 *     All Rights Reserved.
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
 *     NOTICE:
 *
 *     This driver is ment to be a small serial driver to be used for
 *     IR-chipsets that has a UART (16550) compatibility mode. If your
 *     chipset is is UART only, you should probably use IrTTY instead since
 *     the Linux serial driver is probably more robust and optimized.
 *
 *     The functions in this file may be used by FIR drivers, but this
 *     driver knows nothing about FIR drivers so don't ever insert such
 *     code into this file. Instead you should code your FIR driver in a
 *     separate file, and then call the functions in this file if
 *     necessary. This is becase it is difficult to use the Linux serial
 *     driver with a FIR driver becase they must share interrupts etc. Most
 *     FIR chipsets can function in advanced SIR mode, and you should
 *     probably use that mode instead of the UART compatibility mode (and
 *     then just forget about this file)
 *
 ********************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <linux/skbuff.h>
#include <linux/serial_reg.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/wrapper.h>
#include <net/irda/irport.h>

#define IO_EXTENT 8

static unsigned int io[]  = { 0x3e8, ~0, ~0, ~0 };
static unsigned int irq[] = { 11, 0, 0, 0 };

static void irport_write_wakeup( struct irda_device *idev);
static int  irport_write( int iobase, int fifo_size, __u8 *buf, int len);
static void irport_receive( struct irda_device *idev);

__initfunc(int irport_init(void))
{
/* 	int i; */

/* 	for ( i=0; (io[i] < 2000) && (i < 4); i++) { */
/* 		int ioaddr = io[i]; */
/* 		if (check_region(ioaddr, IO_EXTENT)) */
/* 			continue; */
/* 		if (irport_open( i, io[i], io2[i], irq[i], dma[i]) == 0) */
/* 			return 0; */
/* 	} */
/* 	return -ENODEV; */
	return 0;
}

/*
 * Function pc87108_cleanup ()
 *
 *    Close all configured chips
 *
 */
#ifdef MODULE
static void irport_cleanup(void)
{
	int i;

        DEBUG( 4, __FUNCTION__ "()\n");

	/* for ( i=0; i < 4; i++) { */
/* 		if ( dev_self[i]) */
/* 			irport_close( &(dev_self[i]->idev)); */
/* 	} */
}
#endif /* MODULE */

/*
 * Function irport_open (void)
 *
 *    Start IO port 
 *
 */
int irport_open( int iobase)
{
	DEBUG( 0, __FUNCTION__ "(), iobase=%#x\n", iobase);

	/* Initialize UART */
	outb( UART_LCR_WLEN8, iobase+UART_LCR);  /* Reset DLAB */
	outb(( UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2), iobase+UART_MCR);
	
	/* Turn on interrups */
	outb(( UART_IER_THRI |UART_IER_RLSI | UART_IER_RDI), iobase+UART_IER); 
	
	return 0;
}

/*
 * Function irport_cleanup ()
 *
 *    Stop IO port
 *
 */
void irport_close( int iobase) 
{
	DEBUG( 0, __FUNCTION__ "()\n");

	/* Reset UART */
	outb( 0, iobase+UART_MCR);

	/* Turn off interrupts */
	outb( 0, iobase+UART_IER); 
}

/*
 * Function irport_change_speed (idev, speed)
 *
 *    Set speed of port to specified baudrate
 *
 */
void irport_change_speed( int iobase, int speed) 
{
	int fcr;    /* FIFO control reg */
	int lcr;    /* Line control reg */
	int divisor;

	DEBUG( 0, __FUNCTION__ "(), Setting speed to: %d\n", speed);

	DEBUG( 0, __FUNCTION__ "(), iobase=%#x\n", iobase);

	/* Turn off interrupts */
	outb( 0, iobase+UART_IER); 

	divisor = SPEED_MAX/speed;
	
	fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_14;
        
	/* IrDA ports use 8N1 */
	lcr = UART_LCR_WLEN8;
	
	outb( UART_LCR_DLAB | lcr, iobase+UART_LCR); /* Set DLAB */
	outb( divisor & 0xff,      iobase+UART_DLL); /* Set speed */
	outb( divisor >> 8,	   iobase+UART_DLM);
	outb( lcr,		   iobase+UART_LCR); /* Set 8N1	*/
	outb( fcr,		   iobase+UART_FCR); /* Enable FIFO's */

	/* Turn on interrups */
	outb( UART_IER_THRI|UART_IER_RLSI|UART_IER_RDI, iobase+UART_IER); 
}

/*
 * Function irport_interrupt (irq, dev_id, regs)
 *
 *    
 */
void irport_interrupt( int irq, void *dev_id, struct pt_regs *regs) 
{
	struct irda_device *idev = (struct irda_device *) dev_id;

	int iobase, status;
	int iir;

	DEBUG( 4, __FUNCTION__ "(), irq %d\n", irq);

	if ( !idev) {
		printk( KERN_WARNING __FUNCTION__ 
			"() irq %d for unknown device.\n", irq);
		return;
	}

	idev->netdev.interrupt = 1;

	iobase = idev->io.iobase2;

	iir = inb(iobase + UART_IIR);
	do {
		status = inb( iobase+UART_LSR);
		
		if (status & UART_LSR_DR) {
	       		/* Receive interrupt */
			irport_receive(idev);
		}
		if (status & UART_LSR_THRE) {
	       		/* Transmitter ready for data */
			irport_write_wakeup(idev);
		}
	} while (!(inb(iobase+UART_IIR) & UART_IIR_NO_INT));

	idev->netdev.interrupt = 0;
}

/*
 * Function irport_write_wakeup (tty)
 *
 *    Called by the driver when there's room for more data.  If we have
 *    more packets to send, we send them here.
 *
 */
static void irport_write_wakeup( struct irda_device *idev)
{
	int actual = 0, count;
	
	DEBUG( 4, __FUNCTION__ "() <%ld>\n", jiffies);
	
	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);

	/* Finished with frame?  */
	if ( idev->tx_buff.offset == idev->tx_buff.len)  {

		/* 
		 *  Now serial buffer is almost free & we can start 
		 *  transmission of another packet 
		 */
		DEBUG( 4, __FUNCTION__ "(), finished with frame!\n");

		idev->netdev.tbusy = 0; /* Unlock */
		idev->stats.tx_packets++;

		/* Schedule network layer, so we can get some more frames */
		mark_bh( NET_BH);

		return;
	}

	/* Write data left in transmit buffer */
	count = idev->tx_buff.len - idev->tx_buff.offset;
	actual = irport_write( idev->io.iobase2, idev->io.fifo_size, 
			       idev->tx_buff.head, count);
	idev->tx_buff.offset += actual;
	idev->tx_buff.head += actual;
}

/*
 * Function irport_write (driver)
 *
 *    
 *
 */
static int irport_write( int iobase, int fifo_size, __u8 *buf, int len)
{
	int actual = 0;

	/* Tx FIFO should be empty! */
	if (!(inb( iobase+UART_LSR) & UART_LSR_THRE)) {
		DEBUG( 0, __FUNCTION__ "(), failed, fifo not empty!\n");
		return -1;
	}
        
	/* Fill FIFO with current frame */
	while (( fifo_size-- > 0) && (actual < len)) {
		/* Transmit next byte */
		outb( buf[actual], iobase+UART_TX);

		actual++;
	}
        
	DEBUG( 4, __FUNCTION__ "(), fifo_size %d ; %d sent of %d\n", 
	       fifo_size, actual, len);

	return actual;
}

/*
 * Function irport_xmit (void)
 *
 *    Transmits the current frame until FIFO is full, then
 *    waits until the next transmitt interrupt, and continues until the
 *    frame is transmited.
 */
int irport_hard_xmit( struct sk_buff *skb, struct device *dev)
{
	struct irda_device *idev;
	int xbofs;
	int actual;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( dev != NULL, return -1;);

	if ( dev->tbusy) {
		DEBUG( 4, __FUNCTION__ "(), tbusy==TRUE\n");
		
		return -EBUSY;
	}
	
	idev = (struct irda_device *) dev->priv;

	ASSERT( idev != NULL, return -1;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return -1;);

	/* Lock transmit buffer */
	if ( irda_lock( (void *) &dev->tbusy) == FALSE)
		return -EBUSY;
	
        /*  
	 *  Transfer skb to tx_buff while wrapping, stuffing and making CRC 
	 */
	idev->tx_buff.len = async_wrap_skb( skb, idev->tx_buff.data, 
					    idev->tx_buff.truesize);
	
	actual = irport_write( idev->io.iobase2, idev->io.fifo_size, 
			       idev->tx_buff.data, idev->tx_buff.len);
	
	idev->tx_buff.offset = actual;
	idev->tx_buff.head = idev->tx_buff.data + actual;
	
	dev_kfree_skb( skb);
	
	return 0;
}
        
/*
 * Function irport_receive (void)
 *
 *    Receive one frame from the infrared port
 *
 */
static void irport_receive( struct irda_device *idev) 
{
	int iobase;

	if ( !idev)
		return;

	DEBUG( 4, __FUNCTION__ "()\n");

	iobase = idev->io.iobase2;

	if ( idev->rx_buff.len == 0)
		idev->rx_buff.head = idev->rx_buff.data;
	
	/*  
	 * Receive all characters in Rx FIFO, unwrap and unstuff them. 
         * async_unwrap_char will deliver all found frames  
	 */
	do {
		async_unwrap_char( idev, inb( iobase+UART_RX));
		
	} while ( inb( iobase+UART_LSR) & UART_LSR_DR);	
}

#ifdef MODULE

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
	if (irport_init() < 0) {
		cleanup_module();
		return 1;
	}
	return(0);
}

#endif /* MODULE */

