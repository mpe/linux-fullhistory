/*********************************************************************
 *                
 * Filename:      smc-ircc.c
 * Version:       0.1
 * Description:   Driver for the SMC Infrared Communications Controller (SMC)
 * Status:        Experimental.
 * Author:        Thomas Davis (tadavis@jps.net)
 * Created at:    
 * Modified at:   Wed May 19 15:30:08 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Thomas Davis, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     I, Thomas Davis, admit no liability nor provide warranty for any
 *     of this software. This material is provided "AS-IS" and at no charge.
 *
 *     Applicable Models : Fujitsu Lifebook 635t
 *			   Sony PCG-505TX (gets DMA wrong.)
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

#include <net/irda/wrapper.h>
#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/irda_device.h>

#include <net/irda/smc-ircc.h>
#include <net/irda/irport.h>

static char *driver_name = "smc-ircc";

#define CHIP_IO_EXTENT 8

static unsigned int io[]  = { 0x2e8, 0x140, ~0, ~0 };
static unsigned int io2[] = { 0x2f8, 0x3e8, 0, 0};

static struct ircc_cb *dev_self[] = { NULL, NULL, NULL, NULL};

/* Some prototypes */
static int  ircc_open( int i, unsigned int iobase, unsigned int board_addr);
static int  ircc_close( struct irda_device *idev);
static int  ircc_probe( int iobase, int board_addr);
static int  ircc_dma_receive( struct irda_device *idev); 
static int  ircc_dma_receive_complete(struct irda_device *idev, int iobase);
static int  ircc_hard_xmit( struct sk_buff *skb, struct net_device *dev);
static void ircc_dma_write( struct irda_device *idev, int iobase);
static void ircc_change_speed( struct irda_device *idev, int baud);
static void ircc_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void ircc_wait_until_sent( struct irda_device *idev);
static int  ircc_is_receiving( struct irda_device *idev);

static int  ircc_net_init( struct net_device *dev);
static int  ircc_net_open( struct net_device *dev);
static int  ircc_net_close( struct net_device *dev);

static int ircc_debug=3;
static int ircc_irq=255;
static int ircc_dma=255;

static inline void register_bank(int port, int bank)
{
        outb(((inb(port+UART_MASTER) & 0xF0) | (bank & 0x07)),
             port+UART_MASTER);
}

static inline unsigned int serial_in(int port, int offset)
{
        return inb(port+offset);
}

static inline void serial_out(int port, int offset, int value)
{
        outb(value, port+offset);
}

/*
 * Function ircc_init ()
 *
 *    Initialize chip. Just try to find out how many chips we are dealing with
 *    and where they are
 */
int __init ircc_init(void)
{
	int i;

	DEBUG(ircc_debug, __FUNCTION__ " -->\n");
	for ( i=0; (io[i] < 2000) && (i < 4); i++) {
		int ioaddr = io[i];
		if (check_region(ioaddr, CHIP_IO_EXTENT))
			continue;
		if (ircc_open( i, io[i], io2[i]) == 0)
			return 0;
	}
	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");

	return -ENODEV;
}

/*
 * Function ircc_cleanup ()
 *
 *    Close all configured chips
 *
 */
#ifdef MODULE
static void ircc_cleanup(void)
{
	int i;

	DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	for ( i=0; i < 4; i++) {
		if ( dev_self[i])
			ircc_close( &(dev_self[i]->idev));
	}
	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
}
#endif /* MODULE */

/*
 * Function ircc_open (iobase, irq)
 *
 *    Open driver instance
 *
 */
static int ircc_open( int i, unsigned int iobase, unsigned int iobase2)
{
	struct ircc_cb *self;
	struct irda_device *idev;
	int ret;
	int config;

	DEBUG( ircc_debug, __FUNCTION__ " -->\n");

	if ((config = ircc_probe( iobase, iobase2)) == -1) {
	        DEBUG(ircc_debug, 
		      __FUNCTION__ ": addr 0x%04x - no device found!\n", iobase);
		return -1;
	}
	
	/*
	 *  Allocate new instance of the driver
	 */
	self = kmalloc( sizeof(struct ircc_cb), GFP_KERNEL);
	if ( self == NULL) {
		printk( KERN_ERR "IrDA: Can't allocate memory for "
			"IrDA control block!\n");
		return -ENOMEM;
	}
	memset(self, 0, sizeof(struct ircc_cb));
   
	/* Need to store self somewhere */
	dev_self[i] = self;

	idev = &self->idev;

	/* Initialize IO */
	idev->io.iobase    = iobase;
        idev->io.iobase2   = iobase2; /* Used by irport */
        idev->io.irq       = config >> 4 & 0x0f;
	if (ircc_irq < 255) {
	        printk(KERN_INFO "smc: Overriding IRQ - chip says %d, using %d\n",
		      idev->io.irq, ircc_irq);
		idev->io.irq = ircc_irq;
	}
        idev->io.io_ext    = CHIP_IO_EXTENT;
        idev->io.io_ext2   = 8;       /* Used by irport */
        idev->io.dma       = config & 0x0f;
	if (ircc_dma < 255) {
	        printk(KERN_INFO "smc: Overriding DMA - chip says %d, using %d\n",
		      idev->io.dma, ircc_dma);
		idev->io.dma = ircc_dma;
	}
        idev->io.fifo_size = 16;

	/* Lock the port that we need */
	ret = check_region( idev->io.iobase, idev->io.io_ext);
	if ( ret < 0) { 
		DEBUG( 0, __FUNCTION__ ": can't get iobase of 0x%03x\n",
		       idev->io.iobase);
		/* ircc_cleanup( self->idev);  */
		return -ENODEV;
	}
	ret = check_region( idev->io.iobase2, idev->io.io_ext2);
	if ( ret < 0) { 
		DEBUG( 0, __FUNCTION__ ": can't get iobase of 0x%03x\n",
		       idev->io.iobase2);
		/* ircc_cleanup( self->idev);  */
		return -ENODEV;
	}
	request_region( idev->io.iobase, idev->io.io_ext, idev->name);
        request_region( idev->io.iobase2, idev->io.io_ext2, idev->name);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies( &idev->qos);
	
#if 1
	/* The only value we must override it the baudrate */
	idev->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200|IR_576000|IR_1152000|(IR_4000000 << 8);
#else
	/* The only value we must override it the baudrate */
	idev->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200;
#endif

	idev->qos.min_turn_time.bits = 0x07;
	irda_qos_bits_to_value( &idev->qos);

	idev->flags = IFF_FIR|IFF_SIR|IFF_DMA|IFF_PIO;
	
	/* Specify which buffer allocation policy we need */
	idev->rx_buff.flags = GFP_KERNEL | GFP_DMA;
	idev->tx_buff.flags = GFP_KERNEL | GFP_DMA;

	/* Max DMA buffer size needed = (data_size + 6) * (window_size) + 6; */
	idev->rx_buff.truesize = 4000; 
	idev->tx_buff.truesize = 4000;
	
	/* Initialize callbacks */
	idev->change_speed    = ircc_change_speed;
	idev->wait_until_sent = ircc_wait_until_sent;
	idev->is_receiving    = ircc_is_receiving;
     
	/* Override the network functions we need to use */
	idev->netdev.init            = ircc_net_init;
	idev->netdev.hard_start_xmit = ircc_hard_xmit;
	idev->netdev.open            = ircc_net_open;
	idev->netdev.stop            = ircc_net_close;

	irport_start(idev, iobase2);

	/* Open the IrDA device */
	irda_device_open( idev, driver_name, self);
	
	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}

/*
 * Function ircc_close (idev)
 *
 *    Close driver instance
 *
 */
static int ircc_close( struct irda_device *idev)
{
	int iobase;

	DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	ASSERT( idev != NULL, return -1;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return -1;);

        iobase = idev->io.iobase;

	irport_stop(idev, idev->io.iobase2);

	register_bank(iobase, 0);
	serial_out(iobase, UART_IER, 0);
	serial_out(iobase, UART_MASTER, UART_MASTER_RESET);

	register_bank(iobase, 1);

        serial_out(iobase, UART_SCE_CFGA, 
		   UART_CFGA_IRDA_SIR_A | UART_CFGA_TX_POLARITY);
        serial_out(iobase, UART_SCE_CFGB, UART_CFGB_IR);
 
	/* Release the PORT that this driver is using */
	DEBUG( ircc_debug, 
	       __FUNCTION__ ": releasing 0x%03x\n", idev->io.iobase);

	release_region( idev->io.iobase, idev->io.io_ext);

	if ( idev->io.iobase2) {
		DEBUG( ircc_debug, __FUNCTION__ ": releasing 0x%03x\n", 
		       idev->io.iobase2);
		release_region( idev->io.iobase2, idev->io.io_ext2);
	}

	irda_device_close( idev);

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}

/*
 * Function ircc_probe (iobase, board_addr, irq, dma)
 *
 *    Returns non-negative on success.
 *
 */
static int ircc_probe( int iobase, int iobase2) 
{
	int version = 1;
	int low, high, chip, config, dma, irq;
	
	DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	register_bank(iobase, 3);
	high = serial_in(iobase, UART_ID_HIGH);
	low = serial_in(iobase, UART_ID_LOW);
	chip = serial_in(iobase, UART_CHIP_ID);
	version = serial_in(iobase, UART_VERSION);
	config = serial_in(iobase, UART_INTERFACE);
	irq = config >> 4 & 0x0f;
	dma = config & 0x0f;

	if (high == 0x10 && low == 0xb8 && chip == 0xf1) {
		DEBUG(0, "SMC IrDA Controller found; version = %d, "
		      "port 0x%04x, dma %d, interrupt %d\n",
		      version, iobase, dma, irq);
	} else {
		return -1;
	}

	serial_out(iobase, UART_MASTER, 0);

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");

	return config;
}

/*
 * Function ircc_change_speed (idev, baud)
 *
 *    Change the speed of the device
 *
 */
static void ircc_change_speed( struct irda_device *idev, int speed)
{
	struct ircc_cb *self;
	int iobase, ir_mode, select, fast; 

	DEBUG(ircc_debug+1, __FUNCTION__ " -->\n");

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);

	self = idev->priv;
	iobase = idev->io.iobase;

	/* Update accounting for new speed */
	idev->io.baudrate = speed;

	switch ( speed) {
	case 9600:
	case 19200:
	case 37600:
	case 57600:
	case 115200:
	        DEBUG(ircc_debug+1, 
		      __FUNCTION__ ": using irport to change speed to %d\n",
		      speed);
		register_bank(iobase, 0);
		serial_out(iobase, UART_IER, 0);
		serial_out(iobase, UART_MASTER, UART_MASTER_RESET);
		serial_out(iobase, UART_MASTER, UART_MASTER_INT_EN);
		irport_start(idev, idev->io.iobase2);
		irport_change_speed( idev, speed);
		return;
		break;

	case 576000:		
		ir_mode = UART_CFGA_IRDA_HDLC;
		select = 0;
		fast = 0;
		DEBUG( ircc_debug, __FUNCTION__ ": handling baud of 576000\n");
		break;
	case 1152000:
		ir_mode = UART_CFGA_IRDA_HDLC;
		select = UART_1152;
		fast = 0;
		DEBUG(ircc_debug, __FUNCTION__ ": handling baud of 1152000\n");
		break;
	case 4000000:
		ir_mode = UART_CFGA_IRDA_4PPM;
		select = 0;
		fast = UART_LCR_A_FAST;
		DEBUG(ircc_debug, __FUNCTION__ ": handling baud of 4000000\n");
		break;
	default:
		DEBUG( 0, __FUNCTION__ ": unknown baud rate of %d\n", speed);
		return;
	}

#if 0
	serial_out(idev->io.iobase2, 4, 0x08);
#endif

	serial_out(iobase, UART_MASTER, UART_MASTER_RESET);

	register_bank(iobase, 0);
	serial_out(iobase, UART_IER, 0);
	
       	irport_stop(idev, idev->io.iobase2);

	idev->netdev.tbusy = 0;
	
	register_bank(iobase, 1);

	serial_out(iobase, UART_SCE_CFGA, 
		   ((serial_in(iobase, UART_SCE_CFGA) & 0x87) | ir_mode));

	serial_out(iobase, UART_SCE_CFGB,
		   ((serial_in(iobase, UART_SCE_CFGB) & 0x3f) | UART_CFGB_IR));

	(void) serial_in(iobase, UART_FIFO_THRESHOLD);
	serial_out(iobase, UART_FIFO_THRESHOLD, 64);

	register_bank(iobase, 4);

	serial_out(iobase, UART_CONTROL,
		   (serial_in(iobase, UART_CONTROL) & 0x30) 
		   | select | UART_CRC );

	register_bank(iobase, 0);

	serial_out(iobase, UART_LCR_A, fast);

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
}

/*
 * Function ircc_hard_xmit (skb, dev)
 *
 *    Transmit the frame!
 *
 */
static int ircc_hard_xmit( struct sk_buff *skb, struct net_device *dev)
{
	struct irda_device *idev;
	int iobase;
	int mtt;

	DEBUG(ircc_debug+1, __FUNCTION__ " -->\n");
	idev = (struct irda_device *) dev->priv;

	ASSERT( idev != NULL, return 0;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return 0;);

	iobase = idev->io.iobase;

	DEBUG(ircc_debug+1, __FUNCTION__ "(%ld), skb->len=%d\n", jiffies, (int) skb->len);

	/* Use irport for SIR speeds */
	if (idev->io.baudrate <= 115200) {
		DEBUG(ircc_debug+1, __FUNCTION__ ": calling irport_hard_xmit\n");
		return irport_hard_xmit(skb, dev);
	}
	
	DEBUG(ircc_debug, __FUNCTION__ ": using dma; len=%d\n", skb->len);

	/* Lock transmit buffer */
	if (irda_lock((void *) &dev->tbusy) == FALSE)
		return -EBUSY;

	memcpy( idev->tx_buff.head, skb->data, skb->len);

	/* Make sure that the length is a multiple of 16 bits */
	if ( skb->len & 0x01)
		skb->len++;

	idev->tx_buff.len = skb->len;
	idev->tx_buff.data = idev->tx_buff.head;
#if 0
	idev->tx_buff.offset = 0;
#endif
	
	mtt = irda_get_mtt( skb);
	
	/* Use udelay for delays less than 50 us. */
	if (mtt)
		udelay( mtt);
	
	ircc_dma_write( idev, iobase);
	
	dev_kfree_skb( skb);

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}

/*
 * Function ircc_dma_xmit (idev, iobase)
 *
 *    Transmit data using DMA
 *
 */
static void ircc_dma_write( struct irda_device *idev, int iobase)
{
	struct ircc_cb *self;

	DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);

	self = idev->priv;
	iobase = idev->io.iobase;

	setup_dma( idev->io.dma, idev->tx_buff.data, idev->tx_buff.len, 
		   DMA_MODE_WRITE);
	
	idev->io.direction = IO_XMIT;

	serial_out(idev->io.iobase2, 4, 0x08);

	register_bank(iobase, 4);
	serial_out(iobase, UART_CONTROL, 
		   (serial_in(iobase, UART_CONTROL) & 0xF0));

	serial_out(iobase, UART_BOF_COUNT_LO, 2);
	serial_out(iobase, UART_BRICKWALL_CNT_LO, 0);
#if 1
	serial_out(iobase, UART_BRICKWALL_TX_CNT_HI, idev->tx_buff.len >> 8);
	serial_out(iobase, UART_TX_SIZE_LO, idev->tx_buff.len & 0xff);
#else
	serial_out(iobase, UART_BRICKWALL_TX_CNT_HI, 0);
	serial_out(iobase, UART_TX_SIZE_LO, 0);
#endif

	register_bank(iobase, 1);
	serial_out(iobase, UART_SCE_CFGB,
		   serial_in(iobase, UART_SCE_CFGB) | UART_CFGB_DMA_ENABLE);

	register_bank(iobase, 0);

	serial_out(iobase, UART_IER, UART_IER_ACTIVE_FRAME | UART_IER_EOM);
	serial_out(iobase, UART_LCR_B,
		   UART_LCR_B_SCE_TRANSMIT|UART_LCR_B_SIP_ENABLE);

	serial_out(iobase, UART_MASTER, UART_MASTER_INT_EN);

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
}

/*
 * Function ircc_dma_xmit_complete (idev)
 *
 *    The transfer of a frame in finished. This function will only be called 
 *    by the interrupt handler
 *
 */
static void ircc_dma_xmit_complete( struct irda_device *idev, int underrun)
{
	struct ircc_cb *self;
	int iobase, d;

	DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);

	register_bank(idev->io.iobase, 1);

	serial_out(idev->io.iobase, UART_SCE_CFGB,
		   serial_in(idev->io.iobase, UART_SCE_CFGB) &
		   ~UART_CFGB_DMA_ENABLE);

	d = get_dma_residue(idev->io.dma);

	DEBUG(ircc_debug, __FUNCTION__ ": dma residue = %d, len=%d, sent=%d\n", 
	      d, idev->tx_buff.len, idev->tx_buff.len - d);

	self = idev->priv;

	iobase = idev->io.iobase;

	/* Check for underrrun! */
	if ( underrun) {
		idev->stats.tx_errors++;
		idev->stats.tx_fifo_errors++;		
	} else {
		idev->stats.tx_packets++;
		idev->stats.tx_bytes +=  idev->tx_buff.len;
	}

	/* Unlock tx_buff and request another frame */
	idev->netdev.tbusy = 0; /* Unlock */
	idev->media_busy = FALSE;
	
	/* Tell the network layer, that we can accept more frames */
	mark_bh( NET_BH);

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
}

/*
 * Function ircc_dma_receive (idev)
 *
 *    Get ready for receiving a frame. The device will initiate a DMA
 *    if it starts to receive a frame.
 *
 */
static int ircc_dma_receive( struct irda_device *idev) 
{
	struct ircc_cb *self;
	int iobase;

	DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	ASSERT( idev != NULL, return -1;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return -1;);

	self = idev->priv;
	iobase= idev->io.iobase;

	setup_dma( idev->io.dma, idev->rx_buff.data, idev->rx_buff.truesize, 
		   DMA_MODE_READ);
	
	/* driver->media_busy = FALSE; */
	idev->io.direction = IO_RECV;
	idev->rx_buff.data = idev->rx_buff.head;
#if 0
	idev->rx_buff.offset = 0;
#endif

	register_bank(iobase, 4);
	serial_out(iobase, UART_CONTROL, 
		   (serial_in(iobase, UART_CONTROL) &0xF0));
	serial_out(iobase, UART_BOF_COUNT_LO, 2);
	serial_out(iobase, UART_BRICKWALL_CNT_LO, 0);
	serial_out(iobase, UART_BRICKWALL_TX_CNT_HI, 0);
	serial_out(iobase, UART_TX_SIZE_LO, 0);
	serial_out(iobase, UART_RX_SIZE_HI, 0);
	serial_out(iobase, UART_RX_SIZE_LO, 0);

	register_bank(iobase, 0);
	serial_out(iobase, 
		   UART_LCR_B, UART_LCR_B_SCE_RECEIVE | UART_LCR_B_SIP_ENABLE);
	
	register_bank(iobase, 1);
	serial_out(iobase, UART_SCE_CFGB,
		   serial_in(iobase, UART_SCE_CFGB) | 
		   UART_CFGB_DMA_ENABLE | UART_CFGB_DMA_BURST);

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}

/*
 * Function ircc_dma_receive_complete (idev)
 *
 *    Finished with receiving frames
 *
 *    
 */
static int ircc_dma_receive_complete( struct irda_device *idev, int iobase)
{
	struct sk_buff *skb;
	struct ircc_cb *self;
	int len, msgcnt;

	DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	self = idev->priv;

	msgcnt = serial_in(idev->io.iobase, UART_LCR_B) & 0x08;

	DEBUG(ircc_debug, __FUNCTION__ ": dma count = %d\n",
	      get_dma_residue(idev->io.dma));

	len = idev->rx_buff.truesize - get_dma_residue(idev->io.dma) - 4;

	DEBUG(ircc_debug, __FUNCTION__ ": msgcnt = %d, len=%d\n", msgcnt, len);

	skb = dev_alloc_skb( len+1);

	if (skb == NULL)  {
		printk( KERN_INFO __FUNCTION__ 
			": memory squeeze, dropping frame.\n");
		return FALSE;
	}
			
	/* Make sure IP header gets aligned */
	skb_reserve( skb, 1); 
	skb_put( skb, len);

	memcpy(skb->data, idev->rx_buff.data, len);
	idev->stats.rx_packets++;

	skb->dev = &idev->netdev;
	skb->mac.raw  = skb->data;
	skb->protocol = htons(ETH_P_IRDA);
	netif_rx( skb);

	register_bank(idev->io.iobase, 1);
	serial_out(idev->io.iobase, UART_SCE_CFGB,
		   serial_in(idev->io.iobase, UART_SCE_CFGB) &
		   ~UART_CFGB_DMA_ENABLE);

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
	return TRUE;
}

/*
 * Function ircc_interrupt (irq, dev_id, regs)
 *
 *    An interrupt from the chip has arrived. Time to do some work
 *
 */
static void ircc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int iobase, iir;

	struct irda_device *idev = (struct irda_device *) dev_id;

	DEBUG(ircc_debug+1, __FUNCTION__ " -->\n");

	if (idev == NULL) {
		printk( KERN_WARNING "%s: irq %d for unknown device.\n", 
			driver_name, irq);
		return;
	}
	
	if (idev->io.baudrate <= 115200) {
		DEBUG(ircc_debug+1, __FUNCTION__ 
		      ": routing interrupt to irport_interrupt\n");
		return irport_interrupt( irq, dev_id, regs);
	}

	iobase = idev->io.iobase;

	idev->netdev.interrupt = 1;

	serial_out(iobase, UART_MASTER, 0);

	register_bank(iobase, 0);

	iir = serial_in(iobase, UART_IIR);

	serial_out(iobase, UART_IER, 0);

	DEBUG(ircc_debug, __FUNCTION__ ": iir = 0x%02x\n", iir);

	if (iir & UART_IIR_EOM) {
	        DEBUG(ircc_debug, __FUNCTION__ ": UART_IIR_EOM\n");
		if (idev->io.direction == IO_RECV) {
			ircc_dma_receive_complete(idev, iobase);
		} else {
			ircc_dma_xmit_complete(idev, iobase);
		}
		ircc_dma_receive(idev);
	}

	if (iir & UART_IIR_ACTIVE_FRAME) {
	        DEBUG(ircc_debug, __FUNCTION__ ": UART_IIR_ACTIVE_FRAME\n");
		idev->rx_buff.state = INSIDE_FRAME;
#if 0
		ircc_dma_receive(idev);
#endif
	}

	if (iir & UART_IIR_RAW_MODE) {
		DEBUG(ircc_debug, __FUNCTION__ ": IIR RAW mode interrupt.\n");
	}

	idev->netdev.interrupt = 0;

	register_bank(iobase, 0);
	serial_out(iobase, UART_IER, UART_IER_ACTIVE_FRAME|UART_IER_EOM);
	serial_out(iobase, UART_MASTER, UART_MASTER_INT_EN);

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
}

/*
 * Function ircc_wait_until_sent (idev)
 *
 *    This function should put the current thread to sleep until all data 
 *    have been sent, so it is safe to change the speed.
 */
static void ircc_wait_until_sent( struct irda_device *idev)
{
	DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	/* Just delay 60 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(6);

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
}

/*
 * Function ircc_is_receiving (idev)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int ircc_is_receiving( struct irda_device *idev)
{
	int status = FALSE;
	/* int iobase; */

	DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	ASSERT( idev != NULL, return FALSE;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return FALSE;);

	DEBUG(ircc_debug, __FUNCTION__ ": dma count = %d\n",
	      get_dma_residue(idev->io.dma));

	status = ( idev->rx_buff.state != OUTSIDE_FRAME);
	
	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");

	return status;
}

/*
 * Function ircc_net_init (dev)
 *
 *    Initialize network device
 *
 */
static int ircc_net_init( struct net_device *dev)
{
	DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	/* Setup to be a normal IrDA network device driver */
	irda_device_setup( dev);

	/* Insert overrides below this line! */

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}


/*
 * Function ircc_net_open (dev)
 *
 *    Start the device
 *
 */
static int ircc_net_open( struct net_device *dev)
{
	struct irda_device *idev;
	int iobase;
	
	DEBUG(ircc_debug, __FUNCTION__ " -->\n");
	
	ASSERT( dev != NULL, return -1;);
	idev = (struct irda_device *) dev->priv;
	
	ASSERT( idev != NULL, return 0;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return 0;);
	
	iobase = idev->io.iobase;

	if (request_irq( idev->io.irq, ircc_interrupt, 0, idev->name, 
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

	/* turn on interrupts */
	
	MOD_INC_USE_COUNT;

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}

/*
 * Function ircc_net_close (dev)
 *
 *    Stop the device
 *
 */
static int ircc_net_close(struct net_device *dev)
{
	struct irda_device *idev;
	int iobase;

	DEBUG(ircc_debug, __FUNCTION__ " -->\n");
	
	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	ASSERT( dev != NULL, return -1;);
	idev = (struct irda_device *) dev->priv;
	
	ASSERT( idev != NULL, return 0;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return 0;);
	
	iobase = idev->io.iobase;

	disable_dma( idev->io.dma);

	/* Disable interrupts */
       
	free_irq( idev->io.irq, idev);
	free_dma( idev->io.dma);

	MOD_DEC_USE_COUNT;

	DEBUG( ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}

#ifdef MODULE

MODULE_AUTHOR("Thomas Davis <tadavis@jps.net>");
MODULE_DESCRIPTION("SMC IrCC controller driver");
MODULE_PARM(ircc_debug,"1i");
MODULE_PARM(ircc_dma, "1i");
MODULE_PARM(ircc_irq, "1i");

/*
 * Function init_module (void)
 *
 *    
 *
 */
int init_module(void)
{
	return ircc_init();
}

/*
 * Function cleanup_module (void)
 *
 *    
 *
 */
void cleanup_module(void)
{
	ircc_cleanup();
}

#endif
