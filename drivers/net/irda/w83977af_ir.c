/*********************************************************************
 *                
 * Filename:      w83977af_ir.c
 * Version:       0.8
 * Description:   FIR/MIR driver for the Winbond W83977AF Super I/O chip
 * Status:        Experimental.
 * Author:        Paul VanderSpek
 * Created at:    Wed Nov  4 11:46:16 1998
 * Modified at:   Mon Dec 14 21:51:53 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Corel Computer Corp.
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Paul VanderSpek nor Corel Computer Corp. admit liability
 *     nor provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 *     If you find bugs in this file, its very likely that the same bug
 *     will also be in w83977af_ir.c since the implementations is quite
 *     similar.
 *
 *     Notice that all functions that needs to access the chip in _any_
 *     way, must save BSR register on entry, and restore it on exit. 
 *     It is _very_ important to follow this policy!
 *
 *         __u8 bank;
 *     
 *         bank = inb( iobase+BSR);
 *  
 *         do_your_stuff_here();
 *
 *         outb( bank, iobase+BSR);
 *
 ********************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/wrapper.h>
#include <net/irda/irda_device.h>
#include <net/irda/w83977af.h>
#include <net/irda/w83977af_ir.h>

#define NETWINDER

static char *driver_name = "w83977af_ir";

#define CHIP_IO_EXTENT 8

static unsigned int io[] = { 0x400, ~0, ~0, ~0 };
static unsigned int irq[] = { 22, 0, 0, 0 };
static unsigned int dma[] = { 0, 0, 0, 0 };

static struct irda_device *dev_self[] = { NULL, NULL, NULL, NULL};

/* For storing entries in the status FIFO */
struct st_fifo_entry {
	int status;
	int len;
};

static struct st_fifo_entry prev;

/* Some prototypes */
static int  w83977af_open( int i, unsigned int iobase, unsigned int irq, 
			   unsigned int dma);
static int  w83977af_close( struct irda_device *idev);
static int  w83977af_probe( int iobase, int irq, int dma);
static int  w83977af_dma_receive(struct irda_device *idev); 
static int  w83977af_dma_receive_complete(struct irda_device *idev);
static int  w83977af_hard_xmit( struct sk_buff *skb, struct device *dev);
static int  w83977af_pio_write( int iobase, __u8 *buf, int len, int fifo_size);
static void w83977af_dma_write( struct irda_device *idev, int iobase);
static void w83977af_change_speed( struct irda_device *idev, int baud);
static void w83977af_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void w83977af_wait_until_sent( struct irda_device *idev);
static int w83977af_is_receiving( struct irda_device *idev);

static int w83977af_net_init( struct device *dev);
static int w83977af_net_open( struct device *dev);
static int w83977af_net_close( struct device *dev);

/*
 * Function w83977af_init ()
 *
 *    Initialize chip. Just try to find out how many chips we are dealing with
 *    and where they are
 */
__initfunc(int w83977af_init(void))
{
	int i;

	DEBUG( 0, __FUNCTION__ "()\n");

	prev.status = 0;

	for ( i=0; (io[i] < 2000) && (i < 4); i++) {
		int ioaddr = io[i];
		if (check_region(ioaddr, CHIP_IO_EXTENT))
			continue;
		if (w83977af_open( i, io[i], irq[i], dma[i]) == 0)
			return 0;
	}
	return -ENODEV;
}

/*
 * Function w83977af_cleanup ()
 *
 *    Close all configured chips
 *
 */
#ifdef MODULE
void w83977af_cleanup(void)
{
	int i;

        DEBUG( 4, __FUNCTION__ "()\n");

	for ( i=0; i < 4; i++) {
		if ( dev_self[i])
			w83977af_close( dev_self[i]);
	}
}
#endif /* MODULE */

/*
 * Function w83977af_open (iobase, irq)
 *
 *    Open driver instance
 *
 */
int w83977af_open( int i, unsigned int iobase, unsigned int irq, 
		   unsigned int dma)
{
	struct irda_device *idev;
	int ret;

	DEBUG( 0, __FUNCTION__ "()\n");

	if ( w83977af_probe( iobase, irq, dma) == -1)
		return -1;

	/*
	 *  Allocate new instance of the driver
	 */
	idev = kmalloc( sizeof(struct irda_device), GFP_KERNEL);
	if ( idev == NULL) {
		printk( KERN_ERR "IrDA: Can't allocate memory for "
			"IrDA control block!\n");
		return -ENOMEM;
	}
	memset( idev, 0, sizeof(struct irda_device));
   
	/* Need to store self somewhere */
	dev_self[i] = idev;

	/* Initialize IO */
	idev->io.iobase    = iobase;
        idev->io.irq       = irq;
        idev->io.io_ext    = CHIP_IO_EXTENT;
        idev->io.dma       = dma;
        idev->io.fifo_size = 32;

	/* Lock the port that we need */
	ret = check_region( idev->io.iobase, idev->io.io_ext);
	if ( ret < 0) { 
		DEBUG( 0, __FUNCTION__ "(), can't get iobase of 0x%03x\n",
		       idev->io.iobase);
		/* w83977af_cleanup( self->idev);  */
		return -ENODEV;
	}
	request_region( idev->io.iobase, idev->io.io_ext, idev->name);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies( &idev->qos);
	
	/* The only value we must override it the baudrate */

	/* FIXME: The HP HDLS-1100 does not support 1152000! */
	idev->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200|IR_576000|IR_1152000|(IR_4000000 << 8);

	/* The HP HDLS-1100 needs 1 ms according to the specs */
	idev->qos.min_turn_time.bits = 0x03; /* 1ms and more */
	irda_qos_bits_to_value( &idev->qos);

	/* Specify which buffer allocation policy we need */
	idev->rx_buff.flags = GFP_KERNEL | GFP_DMA;
	idev->tx_buff.flags = GFP_KERNEL | GFP_DMA;

	/* Max DMA buffer size needed = (data_size + 6) * (window_size) + 6; */
	idev->rx_buff.truesize = 14384; 
	idev->tx_buff.truesize = 4000;
	
	/* Initialize callbacks */
	idev->hard_xmit       = w83977af_hard_xmit;
	idev->change_speed    = w83977af_change_speed;
	idev->wait_until_sent = w83977af_wait_until_sent;
        idev->is_receiving    = w83977af_is_receiving;

	/* Override the network functions we need to use */
	idev->netdev.init = w83977af_net_init;
	idev->netdev.hard_start_xmit = w83977af_hard_xmit;
	idev->netdev.open = w83977af_net_open;
	idev->netdev.stop = w83977af_net_close;

	/* Open the IrDA device */
	irda_device_open( idev, driver_name, NULL);
	
	return 0;
}

/*
 * Function w83977af_close (idev)
 *
 *    Close driver instance
 *
 */
static int w83977af_close( struct irda_device *idev)
{
	int iobase;

	DEBUG( 0, __FUNCTION__ "()\n");

	ASSERT( idev != NULL, return -1;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return -1;);

        iobase = idev->io.iobase;

	/* enter PnP configuration mode */
	w977_efm_enter();

	w977_select_device(W977_DEVICE_IR);

	/* Deactivate device */
	w977_write_reg(0x30, 0x00);

	w977_efm_exit();

	/* Release the PORT that this driver is using */
	DEBUG(0 , __FUNCTION__ "(), Releasing Region %03x\n", 
	      idev->io.iobase);
	release_region( idev->io.iobase, idev->io.io_ext);

	irda_device_close( idev);

	return 0;
}

/*
 * Function w83977af_probe (iobase, irq, dma)
 *
 *    Returns non-negative on success.
 *
 */
int w83977af_probe( int iobase, int irq, int dma)
{
	int version;
	
	DEBUG( 0, __FUNCTION__ "()\n");

	/* Enter PnP configuration mode */
	w977_efm_enter();

	w977_select_device(W977_DEVICE_IR);

	/* Configure PnP port, IRQ, and DMA channel */
	w977_write_reg(0x60, (iobase >> 8) & 0xff);
	w977_write_reg(0x61, (iobase) & 0xff);
	w977_write_reg(0x70, 0x06);
#ifdef NETWINDER
	w977_write_reg(0x74, dma+1); /* Netwinder uses 1 higher than Linux */
#else
	w977_write_reg(0x74, dma);   
#endif
	w977_write_reg(0x75, dma);   /* Disable Tx DMA */
	
	/* Set append hardware CRC, enable IR bank selection */	
	w977_write_reg(0xf0, APEDCRC|ENBNKSEL);

	/* Activate device */
	w977_write_reg(0x30, 0x01);

	w977_efm_exit();

 	/* Disable Advanced mode */
 	switch_bank( iobase, SET2);
 	outb(iobase+2, 0x00);  

	/* Turn on UART (global) interrupts */
	switch_bank( iobase, SET0);
	outb( HCR_EN_IRQ, iobase+HCR);
	
	/* Switch to advanced mode */
	switch_bank( iobase, SET2);
	outb( inb( iobase+ADCR1) | ADCR1_ADV_SL, iobase+ADCR1);

	/* Set default IR-mode */
	switch_bank( iobase, SET0);
	outb( HCR_SIR, iobase+HCR);

	/* Read the Advanced IR ID */
	switch_bank(iobase, SET3);
	version =  inb( iobase+AUID);
	
	/* Should be 0x1? */
	if (0x10 != (version & 0xf0)) {
		DEBUG( 0, __FUNCTION__ "(), Wrong chip version");	
		return -1;
	}
	
	/* Set FIFO size to 32 */
	switch_bank( iobase, SET2);
	outb( ADCR2_RXFS32|ADCR2_TXFS32, iobase+ADCR2);	
	
	/* Set FIFO threshold to TX17, RX16 */
	switch_bank(iobase, SET0);	
	outb(UFR_RXTL|UFR_TXTL|UFR_TXF_RST|UFR_RXF_RST|UFR_EN_FIFO,iobase+UFR);
/* 	outb( 0xa7, iobase+UFR); */

	/* Receiver frame length */
	switch_bank( iobase, SET4);
	outb( 2048 & 0xff, iobase+6);
	outb(( 2048 >> 8) & 0x1f, iobase+7);

	/* 
	 * Init HP HSDL-1100 transceiver. 
	 * 
	 * Set IRX_MSL since we have 2 * receive paths IRRX, and
	 * IRRXH. Clear IRSL0D since we want IRSL0 * to be a input pin used
	 * for IRRXH 
	 *
	 *   IRRX  pin 37 connected to receiver 
	 *   IRTX  pin 38 connected to transmitter
	 *   FIRRX pin 39 connected to receiver      (IRSL0) 
	 *   CIRRX pin 40 connected to pin 37
	 */
	switch_bank( iobase, SET7);
	outb( 0x40, iobase+7);
		
	DEBUG(0, "W83977AF (IR) driver loaded. Version: 0x%02x\n", version);
	
	return 0;
}

/*
 * Function w83977af_change_speed (idev, baud)
 *
 *    Change the speed of the device
 *
 */
void w83977af_change_speed( struct irda_device *idev, int speed)
{
	int ir_mode = HCR_SIR;
	int iobase; 
	__u8 set;

	DEBUG( 0, __FUNCTION__ "()\n");

	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);

	iobase = idev->io.iobase;

	/* Update accounting for new speed */
	idev->io.baudrate = speed;

	/* Save current bank */
	set = inb( iobase+SSR);

	/* Disable interrupts */
	switch_bank( iobase, SET0);
	outb( 0, iobase+ICR);

	/* Select Set 2 */
	switch_bank( iobase, SET2);

	outb( 0x00, iobase+ABHL);
	switch ( speed) {
	case 9600:   outb( 0x0c, iobase+ABLL); break;
	case 19200:  outb( 0x06, iobase+ABLL); break;
	case 37600:  outb( 0x03, iobase+ABLL); break;
	case 57600:  outb( 0x02, iobase+ABLL); break;
	case 115200: outb( 0x01, iobase+ABLL); break;
	case 576000:
		ir_mode = HCR_MIR_576;
		DEBUG(0, __FUNCTION__ "(), handling baud of 576000\n");
		break;
	case 1152000:
		ir_mode = HCR_MIR_1152;
		DEBUG(0, __FUNCTION__ "(), handling baud of 1152000\n");
		break;
	case 4000000:
		ir_mode = HCR_FIR;
		DEBUG(0, __FUNCTION__ "(), handling baud of 4000000\n");
		break;
	default:
		ir_mode = HCR_FIR;
		DEBUG( 0, __FUNCTION__ "(), unknown baud rate of %d\n", speed);
		break;
	}

	/* Set speed mode */
	switch_bank(iobase, SET0);
	outb( ir_mode, iobase+HCR);

	/* set FIFO size to 32 */
	switch_bank( iobase, SET2);
	outb( ADCR2_RXFS32|ADCR2_TXFS32, iobase+ADCR2);	
	
	/* set FIFO threshold to TX17, RX16 */
	switch_bank(iobase, SET0);
	outb( UFR_RXTL|UFR_TXTL|UFR_TXF_RST|UFR_RXF_RST|UFR_EN_FIFO, iobase+UFR);
	
	idev->netdev.tbusy = 0;
	
	/* Enable some interrupts so we can receive frames */
	switch_bank(iobase, SET0);
	if ( speed > 115200) {
		outb( ICR_EFSFI, iobase+ICR);
		w83977af_dma_receive( idev);
	} else
		outb( ICR_ERBRI, iobase+ICR);
    	
	/* Restore SSR */
	outb( set, iobase+SSR);
}

/*
 * Function w83977af_hard_xmit (skb, dev)
 *
 *    Sets up a DMA transfer to send the current frame.
 *
 */
int w83977af_hard_xmit( struct sk_buff *skb, struct device *dev)
{
	struct irda_device *idev;
	int iobase;
	__u8 set;
	int mtt;
	
	idev = (struct irda_device *) dev->priv;

	ASSERT( idev != NULL, return 0;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return 0;);

	iobase = idev->io.iobase;

	DEBUG(4, __FUNCTION__ "(%ld), skb->len=%d\n", jiffies, (int) skb->len);

	if ( dev->tbusy) {
		DEBUG( 4, __FUNCTION__ "(), tbusy==TRUE\n");

		return -EBUSY;
	}
	
	/* Lock transmit buffer */
	if ( irda_lock( (void *) &dev->tbusy) == FALSE)
		return -EBUSY;

	/* Save current set */
	set = inb( iobase+SSR);
	
	/* Decide if we should use PIO or DMA transfer */
	if ( idev->io.baudrate > 115200) {
		memcpy( idev->tx_buff.data, skb->data, skb->len);
		idev->tx_buff.len = skb->len;
		idev->tx_buff.head = idev->tx_buff.data;
		idev->tx_buff.offset = 0;
		
		mtt = irda_get_mtt( skb);
	        if ( mtt > 50) {
			/* Adjust for timer resolution */
			mtt /= 1000+1;

			/* Setup timer */
			switch_bank( iobase, SET4);
			outb( mtt & 0xff, iobase+TMRL);
			outb(( mtt >> 8) & 0x0f, iobase+TMRH);
			
			/* Start timer */
			outb( IR_MSL_EN_TMR, iobase+IR_MSL);
			idev->io.direction = IO_XMIT;
			
			/* Enable timer interrupt */
			switch_bank( iobase, SET0);
			outb( ICR_ETMRI, iobase+ICR);
		} else {
			if ( mtt)
				udelay( mtt);

			/* Enable DMA interrupt */
			switch_bank( iobase, SET0);
	 		outb( ICR_EDMAI, iobase+ICR);
	     		w83977af_dma_write( idev, iobase);
		}
	} else {
		idev->tx_buff.len = async_wrap_skb( skb, idev->tx_buff.data, 
						    idev->tx_buff.truesize);
		
		idev->tx_buff.offset = 0;
		idev->tx_buff.head = idev->tx_buff.data;
		
		/* Add interrupt on tx low level (will fire immediately) */
		switch_bank( iobase, SET0);
		outb( ICR_ETXTHI, iobase+ICR);
	}
	dev_kfree_skb( skb);

	/* Restore set register */
	outb( set, iobase+SSR);

	return 0;
}


/*
 * Function w83977af_dma_write (idev, iobase)
 *
 *    
 *
 */
static void w83977af_dma_write( struct irda_device *idev, int iobase)
{
	__u8 set;

        DEBUG( 4, __FUNCTION__ "()\n");

	/* Save current set */
	set = inb( iobase+SSR);

	/* Disable DMA */
	switch_bank(iobase, SET0);
	outb( inb( iobase+HCR) & ~HCR_EN_DMA, iobase+HCR);
	
	setup_dma( idev->io.dma, idev->tx_buff.data, idev->tx_buff.len, 
		   DMA_MODE_WRITE);
	
	/* idev->media_busy = TRUE; */
	idev->io.direction = IO_XMIT;
	
	/* Choose transmit DMA channel  */ 
	switch_bank(iobase, SET2);
	outb( inb( iobase+ADCR1) | ADCR1_D_CHSW|ADCR1_DMA_F|ADCR1_ADV_SL, 
	      iobase+ADCR1);
	
	/* Enable DMA */
 	switch_bank( iobase, SET0);
	outb( inb( iobase+HCR) | HCR_EN_DMA, iobase+HCR);
	
	/* Restore set register */
	outb( set, iobase+SSR);
}

/*
 * Function w83977af_pio_write (iobase, buf, len, fifo_size)
 *
 *    
 *
 */
static int w83977af_pio_write( int iobase, __u8 *buf, int len, int fifo_size)
{
	int actual = 0;
	__u8 set;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	/* Save current bank */
	set = inb( iobase+SSR);

	switch_bank( iobase, SET0);
	if (!( inb_p( iobase+USR) & USR_TSRE)) {
		DEBUG( 4, __FUNCTION__ "(), warning, FIFO not empty yet!\n");

		fifo_size -= 17;
		DEBUG( 4, __FUNCTION__ "%d bytes left in tx fifo\n", fifo_size);
	}

	/* Fill FIFO with current frame */
	while (( fifo_size-- > 0) && (actual < len)) {
		/* Transmit next byte */
		outb( buf[actual++], iobase+TBR);
	}
        
	DEBUG( 4, __FUNCTION__ "(), fifo_size %d ; %d sent of %d\n", 
	       fifo_size, actual, len);

	/* Restore bank */
	outb( set, iobase+SSR);

	return actual;
}

/*
 * Function w83977af_dma_xmit_complete (idev)
 *
 *    The transfer of a frame in finished. So do the necessary things
 *
 *    
 */
void w83977af_dma_xmit_complete( struct irda_device *idev)
{
	int iobase;
	__u8 set;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);

	iobase = idev->io.iobase;

	/* Save current set */
	set = inb( iobase+SSR);

	/* Disable DMA */
	switch_bank(iobase, SET0);
	outb( inb(iobase+HCR) & ~HCR_EN_DMA, iobase+HCR);
	
	/* Check for underrrun! */
	if ( inb( iobase+AUDR) & AUDR_UNDR) {
		DEBUG( 0, __FUNCTION__ "(), Transmit underrun!\n");
		
		idev->stats.tx_errors++;
		idev->stats.tx_fifo_errors++;

		/* Clear bit, by writing 1 to it */
		outb( AUDR_UNDR, iobase+AUDR);
	} else
		idev->stats.tx_packets++;

	/* Unlock tx_buff and request another frame */
	idev->netdev.tbusy = 0; /* Unlock */
	idev->media_busy = FALSE;
	
	/* Tell the network layer, that we want more frames */
	mark_bh( NET_BH);

	/* Restore set */
	outb( set, iobase+SSR);
}

/*
 * Function w83977af_dma_receive (idev)
 *
 *    Get ready for receiving a frame. The device will initiate a DMA
 *    if it starts to receive a frame.
 *
 */
int w83977af_dma_receive( struct irda_device *idev) 
{
	int iobase;
	__u8 set;
#ifdef NETWINDER
	unsigned long flags;
	__u8 hcr;
#endif

	ASSERT( idev != NULL, return -1;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return -1;);

	DEBUG( 0, __FUNCTION__ "\n");

	iobase= idev->io.iobase;

	/* Save current set */
	set = inb( iobase+SSR);

	/* Disable DMA */
	switch_bank( iobase, SET0);
	outb( inb( iobase+HCR) & ~HCR_EN_DMA, iobase+HCR);

#ifdef NETWINDER
	save_flags(flags);
	cli();

	disable_dma( idev->io.dma);
	clear_dma_ff( idev->io.dma);
	set_dma_mode( idev->io.dma, DMA_MODE_READ);
	set_dma_addr( idev->io.dma, virt_to_bus(idev->rx_buff.data));
	set_dma_count( idev->io.dma, idev->rx_buff.truesize);
#else
	setup_dma( idev->io.dma, idev->rx_buff.data, idev->rx_buff.truesize, 
		   DMA_MODE_READ);
#endif
	/* driver->media_busy = FALSE; */
	idev->io.direction = IO_RECV;
	idev->rx_buff.head = idev->rx_buff.data;
	idev->rx_buff.offset = 0;

	/* 
	 * Reset Rx FIFO. This will also flush the ST_FIFO, it's very 
	 * important that we don't reset the Tx FIFO since it might not
	 * be finished transmitting yet
	 */
	outb( UFR_RXTL|UFR_TXTL|UFR_RXF_RST|UFR_EN_FIFO, iobase+UFR);
	prev.status = 0;

	/* Choose DMA Rx, DMA Fairness, and Advanced mode */
	switch_bank(iobase, SET2);
	outb(( inb( iobase+ADCR1) & ~ADCR1_D_CHSW)|ADCR1_DMA_F|ADCR1_ADV_SL,
	     iobase+ADCR1);
	
	/* Enable DMA */
	switch_bank(iobase, SET0);
#ifdef NETWINDER
	hcr = inb( iobase+HCR);
	enable_dma( idev->io.dma);
	outb( hcr | HCR_EN_DMA, iobase+HCR);
	restore_flags(flags);
#else	
	outb( inb( iobase+HCR) | HCR_EN_DMA, iobase+HCR);
#endif
	
	/* Restore set */
	outb( set, iobase+SSR);

	DEBUG( 4, __FUNCTION__ "(), done!\n");	

	return 0;
}

/*
 * Function w83977af_receive_complete (idev)
 *
 *    Finished with receiving a frame
 *
 */
int w83977af_dma_receive_complete(struct irda_device *idev)
{
	struct sk_buff *skb;
	int len;
	int iobase;
	__u8 set;
	__u8 status;

	DEBUG( 0, __FUNCTION__ "\n");

	iobase = idev->io.iobase;

	/* Save current set */
	set = inb( iobase+SSR);
	
	iobase = idev->io.iobase;

	switch_bank(iobase, SET5);
	if ( prev.status & FS_FO_FSFDR) {
		status = prev.status;
		len = prev.len;

		prev.status = 0;
	} else {
		status = inb( iobase+FS_FO);
		len = inb( iobase+RFLFL);
		len |= inb( iobase+RFLFH) << 8;
	}

	while ( status & FS_FO_FSFDR) {
		/* Check for errors */
		if ( status & FS_FO_ERR_MSK) {
			if ( status & FS_FO_LST_FR) {
				/* Add number of lost frames to stats */
				idev->stats.rx_errors += len;	
			} else {
				/* Skip frame */
				idev->stats.rx_errors++;
				
				idev->rx_buff.offset += len;
				idev->rx_buff.head   += len;
				
				if ( status & FS_FO_MX_LEX)
					idev->stats.rx_length_errors++;
				
				if ( status & FS_FO_PHY_ERR) 
					idev->stats.rx_frame_errors++;
				
				if ( status & FS_FO_CRC_ERR) 
					idev->stats.rx_crc_errors++;
			}
			/* The errors below can be reported in both cases */
			if ( status & FS_FO_RX_OV)
				idev->stats.rx_fifo_errors++;
			
			if ( status & FS_FO_FSF_OV)
				idev->stats.rx_fifo_errors++;
			
		} else {
			/* Check if we have transfered all data to memory */
			switch_bank(iobase, SET0);
			if ( inb( iobase+USR) & USR_RDR) {
				/* Put this entry back in fifo */
				prev.status = status;
				prev.len = len;

				/* Restore set register */
				outb( set, iobase+SSR);
			
				return FALSE; 	/* I'll be back! */
			}
						
			skb = dev_alloc_skb( len+1);
			if (skb == NULL)  {
				printk( KERN_INFO __FUNCTION__ 
					"(), memory squeeze, dropping frame.\n");
				/* Restore set register */
				outb( set, iobase+SSR);

				return FALSE;
			}
			
			/*  Align to 20 bytes */
			skb_reserve( skb, 1); 
			
			/* Copy frame without CRC */
			if ( idev->io.baudrate < 4000000) {
				skb_put( skb, len-2);
				memcpy( skb->data, idev->rx_buff.head, len-2);
			} else {
				skb_put( skb, len-4);
				memcpy( skb->data, idev->rx_buff.head, len-4);
			}

			/* Move to next frame */
			idev->rx_buff.offset += len;
			idev->rx_buff.head += len;
			
			skb->dev = &idev->netdev;
			skb->mac.raw  = skb->data;
			skb->protocol = htons(ETH_P_IRDA);
			netif_rx( skb);
			idev->stats.rx_packets++;
		}
		/* Read next entry in ST_FIFO */
		switch_bank(iobase, SET5);
		status = inb( iobase+FS_FO);
		len = inb( iobase+RFLFL);
		len |= inb( iobase+RFLFH) << 8;
	}
	/* Restore set register */
	outb( set, iobase+SSR);

	return TRUE;
}

/*
 * Function pc87108_pio_receive (idev)
 *
 *    Receive all data in receiver FIFO
 *
 */
static void w83977af_pio_receive( struct irda_device *idev) 
{
	__u8 byte = 0x00;
	int iobase;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	iobase = idev->io.iobase;
	
	if ( idev->rx_buff.len == 0) {
		idev->rx_buff.head = idev->rx_buff.data;
	}

	/*  Receive all characters in Rx FIFO */
	do {
		byte = inb( iobase+RBR);
		async_unwrap_char( idev, byte);

	} while ( inb( iobase+USR) & USR_RDR); /* Data available */	
}

/*
 * Function w83977af_sir_interrupt (idev, eir)
 *
 *    Handle SIR interrupt
 *
 */
static __u8 w83977af_sir_interrupt( struct irda_device *idev, int isr)
{
	int len;
	int actual;
	__u8 new_icr = 0;

	DEBUG( 4, __FUNCTION__ "(), isr=%#x\n", isr);
	
	/* Transmit FIFO low on data */
	if ( isr & ISR_TXTH_I) {
		/* Write data left in transmit buffer */
		len = idev->tx_buff.len - idev->tx_buff.offset;

		ASSERT( len > 0, return 0;);
		actual = w83977af_pio_write( idev->io.iobase, 
					     idev->tx_buff.head, 
					     len, idev->io.fifo_size);
		idev->tx_buff.offset += actual;
		idev->tx_buff.head += actual;
		
		idev->io.direction = IO_XMIT;
		ASSERT( actual <= len, return 0;);
		/* Check if finished */
		if ( actual == len) { 
			DEBUG( 4, __FUNCTION__ "(), finished with frame!\n");
			idev->netdev.tbusy = 0; /* Unlock */
			idev->stats.tx_packets++;

			/* Schedule network layer */
		        mark_bh(NET_BH);	

			new_icr |= ICR_ETBREI;
		} else
			new_icr |= ICR_ETXTHI;
	}
	/* Check if transmission has completed */
	if ( isr & ISR_TXEMP_I) {
		
		/* Turn around and get ready to receive some data */
		idev->io.direction = IO_RECV;
		new_icr |= ICR_ERBRI;
	}

	/* Rx FIFO threshold or timeout */
	if ( isr & ISR_RXTH_I) {
		w83977af_pio_receive( idev);

		/* Keep receiving */
		new_icr |= ICR_ERBRI;
	}
	return new_icr;
}

/*
 * Function pc87108_fir_interrupt (idev, eir)
 *
 *    Handle MIR/FIR interrupt
 *
 */
static __u8 w83977af_fir_interrupt( struct irda_device *idev, int isr)
{
	__u8 new_icr = 0;
	__u8 set;
	int iobase;

	DEBUG( 4, __FUNCTION__ "(), isr=%#x\n", isr);

	iobase = idev->io.iobase;

	set = inb( iobase+SSR);
	
	/* End of frame detected in FIFO */
	if ( isr & (ISR_FEND_I|ISR_FSF_I)) {
		if ( w83977af_dma_receive_complete( idev)) {
			
			new_icr |= ICR_EFSFI;
		} else {
			/* DMA not finished yet */

			/* Set timer value, resolution 1 ms */
			switch_bank( iobase, SET4);
			outb( 0x01, iobase+TMRL); /* 1 ms */
			outb( 0x00, iobase+TMRH);

			/* Start timer */
			outb( IR_MSL_EN_TMR, iobase+IR_MSL);

			new_icr |= ICR_ETMRI;
		}
	}
	/* Timer finished */
	if ( isr & ISR_TMR_I) {
		/* Disable timer */
		switch_bank( iobase, SET4);
		outb( 0, iobase+IR_MSL);

		/* Clear timer event */
		/* switch_bank(iobase, SET0); */
/* 		outb( ASCR_CTE, iobase+ASCR); */

		/* Check if this is a TX timer interrupt */
		if ( idev->io.direction == IO_XMIT) {
			w83977af_dma_write( idev, iobase);

			new_icr |= ICR_EDMAI;
		} else {
			/* Check if DMA has now finished */
			w83977af_dma_receive_complete( idev);

			new_icr |= ICR_EFSFI;
		}
	}	
	/* Finished with DMA */
	if ( isr & ISR_DMA_I) {
		w83977af_dma_xmit_complete( idev);

		/* Check if there are more frames to be transmitted */
		if ( irda_device_txqueue_empty( idev)) {
		
			/* Prepare for receive */
			w83977af_dma_receive( idev);
			new_icr = ICR_EFSFI;
		}
	}
	
	/* Restore set */
	outb( set, iobase+SSR);

	return new_icr;
}

/*
 * Function pc87108_interrupt (irq, dev_id, regs)
 *
 *    An interrupt from the chip has arrived. Time to do some work
 *
 */
static void w83977af_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	__u8 set, icr, isr;
	int iobase;

	struct irda_device *idev = (struct irda_device *) dev_id;

	if ( idev == NULL) {
		printk( KERN_WARNING "%s: irq %d for unknown device.\n", 
			driver_name, irq);
		return;
	}

	idev->netdev.interrupt = 1;

	iobase = idev->io.iobase;

	/* Save current bank */
	set = inb( iobase+SSR);
	switch_bank( iobase, SET0);
	
	icr = inb( iobase+ICR); 
	isr = inb( iobase+ISR) & icr; /* Mask out the interesting ones */ 

	outb( 0, iobase+ICR); /* Disable interrupts */
	
	if ( isr) {
		/* Dispatch interrupt handler for the current speed */
		if ( idev->io.baudrate > 115200)
			icr = w83977af_fir_interrupt( idev, isr);
		else
			icr = w83977af_sir_interrupt( idev, isr);
	}

	outb( icr, iobase+ICR);    /* Restore (new) interrupts */
	outb( set, iobase+SSR);    /* Restore bank register */

	idev->netdev.interrupt = 0;
}

/*
 * Function w83977af_wait_until_sent (idev)
 *
 *    This function should put the current thread to sleep until all data 
 *    have been sent, so it is safe to f.eks. change the speed.
 */
static void w83977af_wait_until_sent( struct irda_device *idev)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(6);
}

/*
 * Function w83977af_is_receiving (idev)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int w83977af_is_receiving( struct irda_device *idev)
{
	int status = FALSE;
	int iobase;
	__u8 set;

	ASSERT( idev != NULL, return FALSE;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return FALSE;);

	if ( idev->io.baudrate > 115200) {
		iobase = idev->io.iobase;

		/* Check if rx FIFO is not empty */
		set = inb( iobase+SSR);
		switch_bank( iobase, SET2);
		if (( inb( iobase+RXFDTH) & 0x3f) != 0) {
			/* We are receiving something */
			status =  TRUE;
		}
		outb( set, iobase+SSR);
	} else 
		status = ( idev->rx_buff.state != OUTSIDE_FRAME);
	
	return status;
}

/*
 * Function w83977af_net_init (dev)
 *
 *    
 *
 */
static int w83977af_net_init( struct device *dev)
{
	DEBUG( 0, __FUNCTION__ "()\n");

	/* Set up to be a normal IrDA network device driver */
	irda_device_setup( dev);

	/* Insert overrides below this line! */

	return 0;
}


/*
 * Function w83977af_net_open (dev)
 *
 *    Start the device
 *
 */
static int w83977af_net_open( struct device *dev)
{
	struct irda_device *idev;
	int iobase;
	__u8 set;
	
	DEBUG( 0, __FUNCTION__ "()\n");
	
	ASSERT( dev != NULL, return -1;);
	idev = (struct irda_device *) dev->priv;
	
	ASSERT( idev != NULL, return 0;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return 0;);
	
	iobase = idev->io.iobase;

	if (request_irq( idev->io.irq, w83977af_interrupt, 0, idev->name, 
			 (void *) idev)) {
		return -EAGAIN;
	}
	/*
	 * Always allocate the DMA channel after the IRQ,
	 * and clean up on failure.
	 */
	if (request_dma(idev->io.dma, idev->name)) {
		free_irq( idev->io.irq, idev);
		return -EAGAIN;
	}
		
	/* Ready to play! */
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/* Save current set */
	set = inb( iobase+SSR);

 	/* Enable some interrupts so we can receive frames again */
 	switch_bank(iobase, SET0);
 	if ( idev->io.baudrate > 115200) {
 		outb( ICR_EFSFI, iobase+ICR);
 		w83977af_dma_receive( idev);
 	} else
 		outb( ICR_ERBRI, iobase+ICR);

	/* Restore bank register */
	outb( set, iobase+SSR);

	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * Function w83977af_net_close (dev)
 *
 *    Stop the device
 *
 */
static int w83977af_net_close(struct device *dev)
{
	struct irda_device *idev;
	int iobase;
	__u8 set;

	DEBUG( 0, __FUNCTION__ "()\n");
	
	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	ASSERT( dev != NULL, return -1;);
	idev = (struct irda_device *) dev->priv;
	
	ASSERT( idev != NULL, return 0;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return 0;);
	
	iobase = idev->io.iobase;

	disable_dma( idev->io.dma);

	/* Save current set */
	set = inb( iobase+SSR);
	
	/* Disable interrupts */
	switch_bank( iobase, SET0);
	outb( 0, iobase+ICR); 

	free_irq( idev->io.irq, idev);
	free_dma( idev->io.dma);

	/* Restore bank register */
	outb( set, iobase+SSR);

	MOD_DEC_USE_COUNT;

	return 0;
}

#ifdef MODULE

/*
 * Function init_module (void)
 *
 *    
 *
 */
int init_module(void)
{
	w83977af_init();

	return(0);
}

/*
 * Function cleanup_module (void)
 *
 *    
 *
 */
void cleanup_module(void)
{
	w83977af_cleanup();
}

#endif
