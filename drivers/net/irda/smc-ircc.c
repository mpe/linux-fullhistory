/*********************************************************************
 *                
 * Filename:      smc-ircc.c
 * Version:       0.1
 * Description:   Driver for the SMC Infrared Communications Controller (SMC)
 * Status:        Experimental.
 * Author:        Thomas Davis (tadavis@jps.net)
 * Created at:    
 * Modified at:   Fri Nov 12 21:08:09 1999
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
#include <linux/rtnetlink.h>

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

static unsigned int io[]  = { 0x2e8, 0x140, 0x118, ~0 }; 
static unsigned int io2[] = { 0x2f8, 0x3e8, 0x2e8, 0};

static struct ircc_cb *dev_self[] = { NULL, NULL, NULL, NULL};

/* Some prototypes */
static int  ircc_open(int i, unsigned int iobase, unsigned int board_addr);
#ifdef MODULE
static int  ircc_close(struct ircc_cb *self);
#endif /* MODULE */
static int  ircc_probe(int iobase, int board_addr);
static int  ircc_dma_receive(struct ircc_cb *self); 
static int  ircc_dma_receive_complete(struct ircc_cb *self, int iobase);
static int  ircc_hard_xmit(struct sk_buff *skb, struct net_device *dev);
static void ircc_dma_write(struct ircc_cb *self, int iobase);
static void ircc_change_speed(struct ircc_cb *self, __u32 speed);
static void ircc_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void ircc_wait_until_sent(struct ircc_cb *self);
static int  ircc_is_receiving(struct ircc_cb *self);

static int  ircc_net_init(struct net_device *dev);
static int  ircc_net_open(struct net_device *dev);
static int  ircc_net_close(struct net_device *dev);

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

	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");
	for (i=0; (io[i] < 2000) && (i < 4); i++) {
		int ioaddr = io[i];
		if (check_region(ioaddr, CHIP_IO_EXTENT))
			continue;
		if (ircc_open(i, io[i], io2[i]) == 0)
			return 0;
	}
	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");

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

	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	for (i=0; i < 4; i++) {
		if (dev_self[i])
			ircc_close(dev_self[i]);
	}
	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
}
#endif /* MODULE */

/*
 * Function ircc_open (iobase, irq)
 *
 *    Open driver instance
 *
 */
static int ircc_open(int i, unsigned int iobase, unsigned int iobase2)
{
        struct net_device *dev;
	struct ircc_cb *self;
	int config;
	int ret;
        int err;

	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	if ((config = ircc_probe(iobase, iobase2)) == -1) {
	        IRDA_DEBUG(ircc_debug, 
		      __FUNCTION__ ": addr 0x%04x - no device found!\n", iobase);
		return -1;
	}
	
	/*
	 *  Allocate new instance of the driver
	 */
	self = kmalloc(sizeof(struct ircc_cb), GFP_KERNEL);
	if (self == NULL) {
		printk(KERN_ERR "IrDA: Can't allocate memory for "
			"IrDA control block!\n");
		return -ENOMEM;
	}
	memset(self, 0, sizeof(struct ircc_cb));
   
	/* Need to store self somewhere */
	dev_self[i] = self;

	/* Initialize IO */
	self->io.iobase    = iobase;
        self->io.iobase2   = iobase2; /* Used by irport */
        self->io.irq       = config >> 4 & 0x0f;
	if (ircc_irq < 255) {
	        printk(KERN_INFO "smc: Overriding IRQ - chip says %d, using %d\n",
		      self->io.irq, ircc_irq);
		self->io.irq = ircc_irq;
	}
        self->io.io_ext    = CHIP_IO_EXTENT;
        self->io.io_ext2   = 8;       /* Used by irport */
        self->io.dma       = config & 0x0f;
	if (ircc_dma < 255) {
	        printk(KERN_INFO "smc: Overriding DMA - chip says %d, using %d\n",
		      self->io.dma, ircc_dma);
		self->io.dma = ircc_dma;
	}
        self->io.fifo_size = 16;

	/* Lock the port that we need */
	ret = check_region(self->io.iobase, self->io.io_ext);
	if (ret < 0) { 
		IRDA_DEBUG(0, __FUNCTION__ ": can't get iobase of 0x%03x\n",
		       self->io.iobase);
		/* ircc_cleanup(self->self);  */
		return -ENODEV;
	}
	ret = check_region(self->io.iobase2, self->io.io_ext2);
	if (ret < 0) { 
		IRDA_DEBUG(0, __FUNCTION__ ": can't get iobase of 0x%03x\n",
		       self->io.iobase2);
		/* ircc_cleanup(self->self);  */
		return -ENODEV;
	}
	request_region(self->io.iobase, self->io.io_ext, driver_name);
        request_region(self->io.iobase2, self->io.io_ext2, driver_name);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&self->qos);
	
#if 1
	/* The only value we must override it the baudrate */
	self->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200|IR_576000|IR_1152000|(IR_4000000 << 8);
#else
	/* The only value we must override it the baudrate */
	self->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200;
#endif

	self->qos.min_turn_time.bits = 0x07;
	irda_qos_bits_to_value(&self->qos);

	self->flags = IFF_FIR|IFF_SIR|IFF_DMA|IFF_PIO;
	
	/* Max DMA buffer size needed = (data_size + 6) * (window_size) + 6; */
	self->rx_buff.truesize = 4000; 
	self->tx_buff.truesize = 4000;

	/* Allocate memory if needed */
	if (self->rx_buff.truesize > 0) {
		self->rx_buff.head = (__u8 *) kmalloc(self->rx_buff.truesize,
						      GFP_KERNEL|GFP_DMA);
		if (self->rx_buff.head == NULL)
			return -ENOMEM;
		memset(self->rx_buff.head, 0, self->rx_buff.truesize);
	}
	if (self->tx_buff.truesize > 0) {
		self->tx_buff.head = (__u8 *) kmalloc(self->tx_buff.truesize, 
						      GFP_KERNEL|GFP_DMA);
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
	
	if (!(dev = dev_alloc("irda%d", &err))) {
		ERROR(__FUNCTION__ "(), dev_alloc() failed!\n");
		return -ENOMEM;
	}

	dev->priv = (void *) self;
	self->netdev = dev;
	
	/* Override the network functions we need to use */
	dev->init            = ircc_net_init;
	dev->hard_start_xmit = ircc_hard_xmit;
	dev->open            = ircc_net_open;
	dev->stop            = ircc_net_close;

	rtnl_lock();
	err = register_netdevice(dev);
	rtnl_unlock();
	if (err) {
		ERROR(__FUNCTION__ "(), register_netdev() failed!\n");
		return -1;
	}

	MESSAGE("IrDA: Registered device %s\n", dev->name);

	irport_start(&self->irport, iobase2);

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}

/*
 * Function ircc_close (self)
 *
 *    Close driver instance
 *
 */
#ifdef MODULE
static int ircc_close(struct ircc_cb *self)
{
	int iobase;

	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	ASSERT(self != NULL, return -1;);

        iobase = self->io.iobase;

	irport_stop(&self->irport, self->io.iobase2);

	/* Remove netdevice */
	if (self->netdev) {
		rtnl_lock();
		unregister_netdev(self->netdev);
		rtnl_unlock();
	}

	register_bank(iobase, 0);
	serial_out(iobase, UART_IER, 0);
	serial_out(iobase, UART_MASTER, UART_MASTER_RESET);

	register_bank(iobase, 1);

        serial_out(iobase, UART_SCE_CFGA, 
		   UART_CFGA_IRDA_SIR_A | UART_CFGA_TX_POLARITY);
        serial_out(iobase, UART_SCE_CFGB, UART_CFGB_IR);
 
	/* Release the PORT that this driver is using */
	IRDA_DEBUG(ircc_debug, 
	       __FUNCTION__ ": releasing 0x%03x\n", self->io.iobase);

	release_region(self->io.iobase, self->io.io_ext);

	if (self->io.iobase2) {
		IRDA_DEBUG(ircc_debug, __FUNCTION__ ": releasing 0x%03x\n", 
		       self->io.iobase2);
		release_region(self->io.iobase2, self->io.io_ext2);
	}

	if (self->tx_buff.head)
		kfree(self->tx_buff.head);
	
	if (self->rx_buff.head)
		kfree(self->rx_buff.head);

	kfree(self);

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");

	return 0;
}
#endif /* MODULE */

/*
 * Function ircc_probe (iobase, board_addr, irq, dma)
 *
 *    Returns non-negative on success.
 *
 */
static int ircc_probe(int iobase, int iobase2) 
{
	int version = 1;
	int low, high, chip, config, dma, irq;
	
	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	register_bank(iobase, 3);
	high = serial_in(iobase, UART_ID_HIGH);
	low = serial_in(iobase, UART_ID_LOW);
	chip = serial_in(iobase, UART_CHIP_ID);
	version = serial_in(iobase, UART_VERSION);
	config = serial_in(iobase, UART_INTERFACE);
	irq = config >> 4 & 0x0f;
	dma = config & 0x0f;

        if (high == 0x10 && low == 0xb8 && (chip == 0xf1 || chip == 0xf2)) { 
                IRDA_DEBUG(0, "SMC IrDA Controller found; IrCC version %d.%d, "
		      "port 0x%04x, dma %d, interrupt %d\n",
                      chip & 0x0f, version, iobase, dma, irq);
	} else {
		return -1;
	}

	serial_out(iobase, UART_MASTER, 0);

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");

	return config;
}

/*
 * Function ircc_change_speed (self, baud)
 *
 *    Change the speed of the device
 *
 */
static void ircc_change_speed(struct ircc_cb *self, __u32 speed)
{
	int iobase, ir_mode, select, fast; 

	IRDA_DEBUG(ircc_debug+1, __FUNCTION__ " -->\n");

	ASSERT(self != NULL, return;);

	iobase = self->io.iobase;

	/* Update accounting for new speed */
	self->io.speed = speed;

	switch (speed) {
	case 9600:
	case 19200:
	case 37600:
	case 57600:
	case 115200:
	        IRDA_DEBUG(ircc_debug+1, 
		      __FUNCTION__ ": using irport to change speed to %d\n",
		      speed);
		register_bank(iobase, 0);
		serial_out(iobase, UART_IER, 0);
		serial_out(iobase, UART_MASTER, UART_MASTER_RESET);
		serial_out(iobase, UART_MASTER, UART_MASTER_INT_EN);
		irport_start(&self->irport, self->io.iobase2);
		__irport_change_speed(&self->irport, speed);
		return;
		break;

	case 576000:		
		ir_mode = UART_CFGA_IRDA_HDLC;
		select = 0;
		fast = 0;
		IRDA_DEBUG(ircc_debug, __FUNCTION__ ": handling baud of 576000\n");
		break;
	case 1152000:
		ir_mode = UART_CFGA_IRDA_HDLC;
		select = UART_1152;
		fast = 0;
		IRDA_DEBUG(ircc_debug, __FUNCTION__ ": handling baud of 1152000\n");
		break;
	case 4000000:
		ir_mode = UART_CFGA_IRDA_4PPM;
		select = 0;
		fast = UART_LCR_A_FAST;
		IRDA_DEBUG(ircc_debug, __FUNCTION__ ": handling baud of 4000000\n");
		break;
	default:
		IRDA_DEBUG(0, __FUNCTION__ ": unknown baud rate of %d\n", speed);
		return;
	}

#if 0
	serial_out(self->io.iobase2, 4, 0x08);
#endif

	serial_out(iobase, UART_MASTER, UART_MASTER_RESET);

	register_bank(iobase, 0);
	serial_out(iobase, UART_IER, 0);
	
       	irport_stop(&self->irport, self->io.iobase2);

	self->netdev->tbusy = 0;
	
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

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
}

/*
 * Function ircc_hard_xmit (skb, dev)
 *
 *    Transmit the frame!
 *
 */
static int ircc_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ircc_cb *self;
	int iobase;
	int mtt;
	__u32 speed;

	IRDA_DEBUG(ircc_debug+1, __FUNCTION__ " -->\n");
	self = (struct ircc_cb *) dev->priv;

	ASSERT(self != NULL, return 0;);

	iobase = self->io.iobase;

	IRDA_DEBUG(ircc_debug+1, __FUNCTION__ "(%ld), skb->len=%d\n", jiffies,
		   (int) skb->len);

	/* Check if we need to change the speed */
	if ((speed = irda_get_speed(skb)) != self->io.speed)
		ircc_change_speed(self, speed);

	/* Use irport for SIR speeds */
	if (self->io.speed <= 115200) {
		IRDA_DEBUG(ircc_debug+1, __FUNCTION__ 
			   ": calling irport_hard_xmit\n");
		return irport_hard_xmit(skb, dev);
	}
	
	IRDA_DEBUG(ircc_debug, __FUNCTION__ ": using dma; len=%d\n", skb->len);

	/* Lock transmit buffer */
	if (irda_lock((void *) &dev->tbusy) == FALSE)
		return -EBUSY;

	memcpy(self->tx_buff.head, skb->data, skb->len);

	/* Make sure that the length is a multiple of 16 bits */
	if (skb->len & 0x01)
		skb->len++;

	self->tx_buff.len = skb->len;
	self->tx_buff.data = self->tx_buff.head;
#if 0
	self->tx_buff.offset = 0;
#endif
	
	mtt = irda_get_mtt(skb);
	
	/* Use udelay for delays less than 50 us. */
	if (mtt)
		udelay(mtt);
	
	ircc_dma_write(self, iobase);
	
	dev_kfree_skb(skb);

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}

/*
 * Function ircc_dma_xmit (self, iobase)
 *
 *    Transmit data using DMA
 *
 */
static void ircc_dma_write(struct ircc_cb *self, int iobase)
{
	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	ASSERT(self != NULL, return;);

	iobase = self->io.iobase;

	setup_dma(self->io.dma, self->tx_buff.data, self->tx_buff.len, 
		   DMA_MODE_WRITE);
	
	self->io.direction = IO_XMIT;

	serial_out(self->io.iobase2, 4, 0x08);

	register_bank(iobase, 4);
	serial_out(iobase, UART_CONTROL, 
		   (serial_in(iobase, UART_CONTROL) & 0xF0));

	serial_out(iobase, UART_BOF_COUNT_LO, 2);
	serial_out(iobase, UART_BRICKWALL_CNT_LO, 0);
#if 1
	serial_out(iobase, UART_BRICKWALL_TX_CNT_HI, self->tx_buff.len >> 8);
	serial_out(iobase, UART_TX_SIZE_LO, self->tx_buff.len & 0xff);
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

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
}

/*
 * Function ircc_dma_xmit_complete (self)
 *
 *    The transfer of a frame in finished. This function will only be called 
 *    by the interrupt handler
 *
 */
static void ircc_dma_xmit_complete(struct ircc_cb *self, int underrun)
{
	int iobase, d;

	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	ASSERT(self != NULL, return;);

	register_bank(self->io.iobase, 1);

	serial_out(self->io.iobase, UART_SCE_CFGB,
		   serial_in(self->io.iobase, UART_SCE_CFGB) &
		   ~UART_CFGB_DMA_ENABLE);

	d = get_dma_residue(self->io.dma);

	IRDA_DEBUG(ircc_debug, __FUNCTION__ ": dma residue = %d, len=%d, sent=%d\n", 
	      d, self->tx_buff.len, self->tx_buff.len - d);

	iobase = self->io.iobase;

	/* Check for underrrun! */
	if (underrun) {
		self->stats.tx_errors++;
		self->stats.tx_fifo_errors++;		
	} else {
		self->stats.tx_packets++;
		self->stats.tx_bytes +=  self->tx_buff.len;
	}

	/* Unlock tx_buff and request another frame */
	self->netdev->tbusy = 0; /* Unlock */
	
	/* Tell the network layer, that we can accept more frames */
	mark_bh(NET_BH);

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
}

/*
 * Function ircc_dma_receive (self)
 *
 *    Get ready for receiving a frame. The device will initiate a DMA
 *    if it starts to receive a frame.
 *
 */
static int ircc_dma_receive(struct ircc_cb *self) 
{
	int iobase;

	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	ASSERT(self != NULL, return -1;);

	iobase= self->io.iobase;

	setup_dma(self->io.dma, self->rx_buff.data, self->rx_buff.truesize, 
		   DMA_MODE_READ);
	
	/* driver->media_busy = FALSE; */
	self->io.direction = IO_RECV;
	self->rx_buff.data = self->rx_buff.head;
#if 0
	self->rx_buff.offset = 0;
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

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}

/*
 * Function ircc_dma_receive_complete (self)
 *
 *    Finished with receiving frames
 *
 *    
 */
static int ircc_dma_receive_complete(struct ircc_cb *self, int iobase)
{
	struct sk_buff *skb;
	int len, msgcnt;

	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	msgcnt = serial_in(self->io.iobase, UART_LCR_B) & 0x08;

	IRDA_DEBUG(ircc_debug, __FUNCTION__ ": dma count = %d\n",
	      get_dma_residue(self->io.dma));

	len = self->rx_buff.truesize - get_dma_residue(self->io.dma) - 4;

	IRDA_DEBUG(ircc_debug, __FUNCTION__ ": msgcnt = %d, len=%d\n", msgcnt, len);

	skb = dev_alloc_skb(len+1);

	if (skb == NULL)  {
		printk(KERN_INFO __FUNCTION__ 
			": memory squeeze, dropping frame.\n");
		return FALSE;
	}
			
	/* Make sure IP header gets aligned */
	skb_reserve(skb, 1); 
	skb_put(skb, len);

	memcpy(skb->data, self->rx_buff.data, len);
	self->stats.rx_packets++;

	skb->dev = self->netdev;
	skb->mac.raw  = skb->data;
	skb->protocol = htons(ETH_P_IRDA);
	netif_rx(skb);

	register_bank(self->io.iobase, 1);
	serial_out(self->io.iobase, UART_SCE_CFGB,
		   serial_in(self->io.iobase, UART_SCE_CFGB) &
		   ~UART_CFGB_DMA_ENABLE);

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
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
	struct net_device *dev = (struct net_device *) dev_id;
	struct ircc_cb *self;

	IRDA_DEBUG(ircc_debug+1, __FUNCTION__ " -->\n");

	if (dev == NULL) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n", 
			driver_name, irq);
		return;
	}
	
	self = (struct ircc_cb *) dev->priv;

	if (self->io.speed <= 115200) {
		IRDA_DEBUG(ircc_debug+1, __FUNCTION__ 
		      ": routing interrupt to irport_interrupt\n");
		return irport_interrupt(irq, dev_id, regs);
	}

	iobase = self->io.iobase;

	dev->interrupt = 1;

	serial_out(iobase, UART_MASTER, 0);

	register_bank(iobase, 0);

	iir = serial_in(iobase, UART_IIR);

	serial_out(iobase, UART_IER, 0);

	IRDA_DEBUG(ircc_debug, __FUNCTION__ ": iir = 0x%02x\n", iir);

	if (iir & UART_IIR_EOM) {
	        IRDA_DEBUG(ircc_debug, __FUNCTION__ ": UART_IIR_EOM\n");
		if (self->io.direction == IO_RECV) {
			ircc_dma_receive_complete(self, iobase);
		} else {
			ircc_dma_xmit_complete(self, iobase);
		}
		ircc_dma_receive(self);
	}

	if (iir & UART_IIR_ACTIVE_FRAME) {
	        IRDA_DEBUG(ircc_debug, __FUNCTION__ ": UART_IIR_ACTIVE_FRAME\n");
		self->rx_buff.state = INSIDE_FRAME;
#if 0
		ircc_dma_receive(self);
#endif
	}

	if (iir & UART_IIR_RAW_MODE) {
		IRDA_DEBUG(ircc_debug, __FUNCTION__ ": IIR RAW mode interrupt.\n");
	}

	dev->interrupt = 0;

	register_bank(iobase, 0);
	serial_out(iobase, UART_IER, UART_IER_ACTIVE_FRAME|UART_IER_EOM);
	serial_out(iobase, UART_MASTER, UART_MASTER_INT_EN);

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
}

/*
 * Function ircc_wait_until_sent (self)
 *
 *    This function should put the current thread to sleep until all data 
 *    have been sent, so it is safe to change the speed.
 */
static void ircc_wait_until_sent(struct ircc_cb *self)
{
	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	/* Just delay 60 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(60));

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
}

/*
 * Function ircc_is_receiving (self)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int ircc_is_receiving(struct ircc_cb *self)
{
	int status = FALSE;
	/* int iobase; */

	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	ASSERT(self != NULL, return FALSE;);

	IRDA_DEBUG(ircc_debug, __FUNCTION__ ": dma count = %d\n",
	      get_dma_residue(self->io.dma));

	status = (self->rx_buff.state != OUTSIDE_FRAME);
	
	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");

	return status;
}

/*
 * Function ircc_net_init (dev)
 *
 *    Initialize network device
 *
 */
static int ircc_net_init(struct net_device *dev)
{
	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");

	/* Setup to be a normal IrDA network device driver */
	irda_device_setup(dev);

	/* Insert overrides below this line! */

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}


/*
 * Function ircc_net_open (dev)
 *
 *    Start the device
 *
 */
static int ircc_net_open(struct net_device *dev)
{
	struct ircc_cb *self;
	int iobase;
	
	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");
	
	ASSERT(dev != NULL, return -1;);
	self = (struct ircc_cb *) dev->priv;
	
	ASSERT(self != NULL, return 0;);
	
	iobase = self->io.iobase;

	if (request_irq(self->io.irq, ircc_interrupt, 0, dev->name, 
			 (void *) dev)) {
		return -EAGAIN;
	}
	/*
	 * Always allocate the DMA channel after the IRQ,
	 * and clean up on failure.
	 */
	if (request_dma(self->io.dma, dev->name)) {
		free_irq(self->io.irq, dev);
		return -EAGAIN;
	}
		
	/* Ready to play! */
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/* 
	 * Open new IrLAP layer instance, now that everything should be
	 * initialized properly 
	 */
	self->irlap = irlap_open(dev, &self->qos);
	
	MOD_INC_USE_COUNT;

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
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
	struct ircc_cb *self;
	int iobase;

	IRDA_DEBUG(ircc_debug, __FUNCTION__ " -->\n");
	
	ASSERT(dev != NULL, return -1;);
	self = (struct ircc_cb *) dev->priv;
	
	ASSERT(self != NULL, return 0;);
	
	iobase = self->io.iobase;

	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	/* Stop and remove instance of IrLAP */
	if (self->irlap)
		irlap_close(self->irlap);
	self->irlap = NULL;

	disable_dma(self->io.dma);

	/* Disable interrupts */
	free_irq(self->io.irq, dev);
	free_dma(self->io.dma);

	MOD_DEC_USE_COUNT;

	IRDA_DEBUG(ircc_debug, "--> " __FUNCTION__ "\n");
	return 0;
}

#ifdef MODULE

MODULE_AUTHOR("Thomas Davis <tadavis@jps.net>");
MODULE_DESCRIPTION("SMC IrCC controller driver");
MODULE_PARM(ircc_debug,"1i");
MODULE_PARM(ircc_dma, "1i");
MODULE_PARM(ircc_irq, "1i");

int init_module(void)
{
	return ircc_init();
}

void cleanup_module(void)
{
	ircc_cleanup();
}

#endif
