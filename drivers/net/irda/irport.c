/*********************************************************************
 *		  
 * Filename:	  irport.c
 * Version:	  0.1
 * Description:   Serial driver for IrDA. The functions in this file
 *                may be used by FIR drivers, but this file knows
 *                nothing about FIR drivers!!!
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
 ********************************************************************/

/* #include <linux/module.h> */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <linux/errno.h>

#include <linux/skbuff.h>
#include <linux/serial_reg.h>

#include "irda.h"
#include "ircompat.h"
#include "irport.h"
#include "timer.h"
#include "crc.h"
#include "wrapper.h"
#include "irlap_frame.h"

#define IO_EXTENT	8

static void irport_write_wakeup( struct irda_device *idev);
static int irport_write( int iobase, int fifo_size, __u8 *buf, int len);
static void irport_receive( struct irda_device *idev);

/*
 * Function irport_open (void)
 *
 *    Start IO port 
 *
 */
int irport_open( int iobase)
{
	/* Initialize UART */
	outb_p( UART_LCR_WLEN8, iobase+UART_LCR);  /* Reset DLAB */
	outb_p(( UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2), 
	       iobase+UART_MCR);
	
	/* Turn on interrups */
	outb_p(( UART_IER_THRI |UART_IER_RLSI | UART_IER_RDI), 
	       iobase+UART_IER); 
	
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
	outb_p( 0, iobase+UART_MCR);

	/* Turn off interrupts */
	outb_p( 0, iobase+UART_IER); 
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

	/* Turn off interrupts */
	outb_p( 0, iobase+UART_IER); 

	divisor = SPEED_MAX/speed;
	
	fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_14;
        
	/* IrDA ports use 8N1 */
	lcr = UART_LCR_WLEN8;
	
	outb_p( UART_LCR_DLAB | lcr, iobase+UART_LCR); /* Set DLAB */
	outb_p( divisor & 0xff,      iobase+UART_DLL); /* Set speed	*/
	outb_p( divisor >> 8,	     iobase+UART_DLM);
	outb_p( lcr,		     iobase+UART_LCR); /* Set 8N1	*/
	outb_p( fcr,		     iobase+UART_FCR); /* Enable FIFO's */

	/* Turn on interrups */
	outb_p(( UART_IER_THRI |UART_IER_RLSI | UART_IER_RDI), 
	       iobase+UART_IER); 
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

	iobase = idev->io.iobase;
	iir    = inb( iobase + UART_IIR);

	do {
		status = inb( iobase+UART_LSR);
		
		if ( status & UART_LSR_DR) {
	       		/* Receive interrupt */
			irport_receive( idev);
		}
		if ( status & UART_LSR_THRE) {
	       		/* Transmitter ready for data */
			irport_write_wakeup( idev);
		}
	} while ( !(inb( iobase+UART_IIR) & UART_IIR_NO_INT));
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
	
	/* 
	 *  First make sure we're connected. 
	 */
	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);

	/*
	 *  Finished with frame?
	 */
	if ( idev->tx.ptr == idev->tx.len)  {

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
	/*
	 *  Write data left in transmit buffer
	 */
	count = idev->tx.len - idev->tx.ptr;
	actual = irport_write( idev->io.iobase, idev->io.fifo_size, 
			       idev->tx.head, count);
	idev->tx.ptr += actual;
	idev->tx.head += actual;
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

	if (!(inb_p( iobase+UART_LSR) & UART_LSR_THRE)) {
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
	
	idev = (struct irda_device *)  dev->priv;

	ASSERT( idev != NULL, return -1;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return -1;);

	if ( skb == NULL) {
		DEBUG( 0, __FUNCTION__ "(), skb==NULL\n");
#if LINUX_VERSION_CODE < LinuxVersionCode(2,1,0)
		dev_tint(dev); 
#endif
		return 0;
	}

	/* Lock transmit buffer */
	if ( irda_lock( (void *) &dev->tbusy) == FALSE)
		return -EBUSY;
	
        /*  
	 *  Transfer skb to tx_buff while wrapping, stuffing and making CRC 
	 */
	idev->tx.len = async_wrap_skb( skb, idev->tx.buff, idev->tx.buffsize);

	actual = irport_write( idev->io.iobase, idev->io.fifo_size, 
			       idev->tx.buff, idev->tx.len);

	idev->tx.ptr = actual;
	idev->tx.head = idev->tx.buff + actual;

	IS_SKB( skb, return 0;);
	FREE_SKB_MAGIC( skb);
	DEV_KFREE_SKB( skb, FREE_WRITE);

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
	__u8 byte = 0x00;
	int iobase;

	if ( !idev)
		return;

	DEBUG( 0, __FUNCTION__ "()\n");

	iobase = idev->io.iobase;

	if ( idev->rx.len == 0) {
		idev->rx.head = idev->rx.buff;
	}

	/* 
	 *  Receive all characters in FIFO 
	 */
	do {
		byte = inb_p( iobase+UART_RX);
		async_unwrap_char( idev, byte);
		
	} while ( inb_p( iobase+UART_LSR) & UART_LSR_DR);	
}

/*
 * Function cleanup_module (void)
 *
 *    
 *
 */
/* void cleanup_module(void) */
/* { */
/* 	DEBUG( 3, "IrPORT: cleanup_module!\n"); */
/* 	irport_cleanup(irport_drv); */
/* } */

/*
 * Function init_module (void)
 *
 *    
 *
 */
/* int init_module(void) */
/* { */
/* 	if (irport_init() < 0) { */
/* 		cleanup_module(); */
/* 		return 1; */
/* 	} */
/* 	return(0); */
/* } */

