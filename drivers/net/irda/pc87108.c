/*********************************************************************
 *                
 * Filename:      pc87108.c
 * Version:       0.8
 * Description:   FIR/MIR driver for the NS PC87108 chip
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Nov  7 21:43:15 1998
 * Modified at:   Thu Dec 16 00:54:27 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>
 *     Copyright (c) 1998 Lichen Wang, <lwang@actisys.com>
 *     Copyright (c) 1998 Actisys Corp., www.actisys.com
 *     All Rights Reserved
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
 *     Notice that all functions that needs to access the chip in _any_
 *     way, must save BSR register on entry, and restore it on exit. 
 *     It is _very_ important to follow this policy!
 *
 *         __u8 bank;
 *     
 *         bank = inb(iobase+BSR);
 *  
 *         do_your_stuff_here();
 *
 *         outb(bank, iobase+BSR);
 *
 *    If you find bugs in this file, its very likely that the same bug
 *    will also be in w83977af_ir.c since the implementations are quite
 *    similar.
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

#include <net/irda/pc87108.h>

#define BROKEN_DONGLE_ID

static char *driver_name = "pc87108";
static int qos_mtt_bits = 0x07;  /* 1 ms or more */

#define CHIP_IO_EXTENT 8

static unsigned int io[]  = { 0x2f8, ~0, ~0, ~0 };
static unsigned int io2[] = { 0x150, 0, 0, 0 };
static unsigned int irq[] = { 3, 0, 0, 0 };
static unsigned int dma[] = { 0, 0, 0, 0 };

static struct pc87108 *dev_self[] = { NULL, NULL, NULL, NULL};

static char *dongle_types[] = {
	"Differential serial interface",
	"Differential serial interface",
	"Reserved",
	"Reserved",
	"Sharp RY5HD01",
	"Reserved",
	"Single-ended serial interface",
	"Consumer-IR only",
	"HP HSDL-2300, HP HSDL-3600/HSDL-3610",
	"IBM31T1100 or Temic TFDS6000/TFDS6500",
	"Reserved",
	"Reserved",
	"HP HSDL-1100/HSDL-2100",
	"HP HSDL-1100/HSDL-2100"
	"Supports SIR Mode only",
	"No dongle connected",
};

/* Some prototypes */
static int  pc87108_open(int i, unsigned int iobase, unsigned int board_addr, 
			 unsigned int irq, unsigned int dma);
#ifdef MODULE
static int  pc87108_close(struct pc87108 *self);
#endif /* MODULE */
static int  pc87108_probe(int iobase, int board_addr, int irq, int dma);
static void pc87108_pio_receive(struct pc87108 *self);
static int  pc87108_dma_receive(struct pc87108 *self); 
static int  pc87108_dma_receive_complete(struct pc87108 *self, int iobase);
static int  pc87108_hard_xmit(struct sk_buff *skb, struct net_device *dev);
static int  pc87108_pio_write(int iobase, __u8 *buf, int len, int fifo_size);
static void pc87108_dma_write(struct pc87108 *self, int iobase);
static void pc87108_change_speed(struct pc87108 *self, __u32 baud);
static void pc87108_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void pc87108_wait_until_sent(struct pc87108 *self);
static int  pc87108_is_receiving(struct pc87108 *self);
static int  pc87108_read_dongle_id (int iobase);
static void pc87108_init_dongle_interface (int iobase, int dongle_id);

static int  pc87108_net_init(struct net_device *dev);
static int  pc87108_net_open(struct net_device *dev);
static int  pc87108_net_close(struct net_device *dev);
static int  pc87108_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

/*
 * Function pc87108_init ()
 *
 *    Initialize chip. Just try to find out how many chips we are dealing with
 *    and where they are
 */
int __init pc87108_init(void)
{
	int i;

	for (i=0; (io[i] < 2000) && (i < 4); i++) {
		int ioaddr = io[i];
		if (check_region(ioaddr, CHIP_IO_EXTENT) < 0)
			continue;
		if (pc87108_open(i, io[i], io2[i], irq[i], dma[i]) == 0)
			return 0;
	}
	return -ENODEV;
}

/*
 * Function pc87108_cleanup ()
 *
 *    Close all configured chips
 *
 */
#ifdef MODULE
static void pc87108_cleanup(void)
{
	int i;

        IRDA_DEBUG(4, __FUNCTION__ "()\n");

	for (i=0; i < 4; i++) {
		if (dev_self[i])
			pc87108_close(dev_self[i]);
	}
}
#endif /* MODULE */

/*
 * Function pc87108_open (iobase, irq)
 *
 *    Open driver instance
 *
 */
static int pc87108_open(int i, unsigned int iobase, unsigned int board_addr, 
			unsigned int irq, unsigned int dma)
{
	struct net_device *dev;
	struct pc87108 *self;
	int dongle_id;
	int ret;
	int err;

	IRDA_DEBUG(0, __FUNCTION__ "()\n");

	if ((dongle_id = pc87108_probe(iobase, board_addr, irq, dma)) == -1)
		return -1;

	/*
	 *  Allocate new instance of the driver
	 */
	self = kmalloc(sizeof(struct pc87108), GFP_KERNEL);
	if (self == NULL) {
		printk(KERN_ERR "IrDA: Can't allocate memory for "
		       "IrDA control block!\n");
		return -ENOMEM;
	}
	memset(self, 0, sizeof(struct pc87108));
   
	/* Need to store self somewhere */
	dev_self[i] = self;

	/* Initialize IO */
	self->io.iobase    = iobase;
        self->io.irq       = irq;
        self->io.io_ext    = CHIP_IO_EXTENT;
        self->io.dma       = dma;
        self->io.fifo_size = 32;

	/* Lock the port that we need */
	ret = check_region(self->io.iobase, self->io.io_ext);
	if (ret < 0) { 
		IRDA_DEBUG(0, __FUNCTION__ "(), can't get iobase of 0x%03x\n",
		      self->io.iobase);
		/* pc87108_cleanup(self->self);  */
		return -ENODEV;
	}
	request_region(self->io.iobase, self->io.io_ext, driver_name);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&self->qos);
	
	/* The only value we must override it the baudrate */
	self->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200|IR_576000|IR_1152000|(IR_4000000 << 8);
	
	self->qos.min_turn_time.bits = qos_mtt_bits;
	irda_qos_bits_to_value(&self->qos);
	
	self->flags = IFF_FIR|IFF_MIR|IFF_SIR|IFF_DMA|IFF_PIO|IFF_DONGLE;

	/* Max DMA buffer size needed = (data_size + 6) * (window_size) + 6; */
	self->rx_buff.truesize = 14384; 
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
	/* dev_alloc doesn't clear the struct, so lets do a little hack */
	memset(((__u8*)dev)+sizeof(char*),0,sizeof(struct net_device)-sizeof(char*));

	dev->priv = (void *) self;
	self->netdev = dev;

	/* Override the network functions we need to use */
	dev->init            = pc87108_net_init;
	dev->hard_start_xmit = pc87108_hard_xmit;
	dev->open            = pc87108_net_open;
	dev->stop            = pc87108_net_close;
	dev->do_ioctl        = pc87108_net_ioctl;

	rtnl_lock();
	err = register_netdevice(dev);
	rtnl_unlock();
	if (err) {
		ERROR(__FUNCTION__ "(), register_netdev() failed!\n");
		return -1;
	}

	MESSAGE("IrDA: Registered device %s\n", dev->name);
	
	self->io.dongle_id = dongle_id;
	pc87108_init_dongle_interface(iobase, dongle_id);

	return 0;
}

#ifdef MODULE
/*
 * Function pc87108_close (self)
 *
 *    Close driver instance
 *
 */
static int pc87108_close(struct pc87108 *self)
{
	int iobase;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return -1;);

        iobase = self->io.iobase;

	/* Remove netdevice */
	if (self->netdev) {
		rtnl_lock();
		unregister_netdevice(self->netdev);
		rtnl_unlock();
		/* Must free the old-style 2.2.x device */
		kfree(self->netdev);
	}

	/* Release the PORT that this driver is using */
	IRDA_DEBUG(4, __FUNCTION__ "(), Releasing Region %03x\n", 
		   self->io.iobase);
	release_region(self->io.iobase, self->io.io_ext);

	if (self->tx_buff.head)
		kfree(self->tx_buff.head);
	
	if (self->rx_buff.head)
		kfree(self->rx_buff.head);

	kfree(self);

	return 0;
}
#endif /* MODULE */

/*
 * Function pc87108_probe (iobase, board_addr, irq, dma)
 *
 *    Returns non-negative on success.
 *
 */
static int pc87108_probe(int iobase, int board_addr, int irq, int dma) 
{
	int version;
	__u8 temp=0;
	int dongle_id;
	
	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	/* Base Address and Interrupt Control Register BAIC */
	outb(0, board_addr);
	switch (iobase) {
	case 0x3E8: outb(0x14, board_addr+1); break;
	case 0x2E8: outb(0x15, board_addr+1); break;
	case 0x3F8: outb(0x16, board_addr+1); break;
	case 0x2F8: outb(0x17, board_addr+1); break;
	default:    ERROR(__FUNCTION__ "(), invalid base_address");
	}
	
	/* Control Signal Routing Register CSRT */
	switch (irq) {
	case 3:  temp = 0x01; break;
	case 4:  temp = 0x02; break;
	case 5:  temp = 0x03; break;
	case 7:  temp = 0x04; break;
	case 9:  temp = 0x05; break;
	case 11: temp = 0x06; break;
	case 15: temp = 0x07; break;
	default: ERROR(__FUNCTION__ "(), invalid irq");
	}
	outb(1, board_addr);
	
	switch (dma) {	
	case 0: outb(0x08+temp, board_addr+1); break;
	case 1: outb(0x10+temp, board_addr+1); break;
	case 3: outb(0x18+temp, board_addr+1); break;
	default: IRDA_DEBUG(0, __FUNCTION__ "(), invalid dma");
	}

	/* Mode Control Register MCTL */
	outb(2, board_addr);
	outb(0x03, board_addr+1);
		
	/* read the Module ID */
	switch_bank(iobase, BANK3);
	version = inb(iobase+MID);
	
	/* should be 0x2? */
	if (0x20 != (version & 0xf0)) {
		ERROR(__FUNCTION__ "(), Wrong chip version %02x\n", version);
		return -1;
	}
	
	/* Switch to advanced mode */
	switch_bank(iobase, BANK2);
	outb(ECR1_EXT_SL, iobase+ECR1);
	switch_bank(iobase, BANK0);

	dongle_id = pc87108_read_dongle_id(iobase);
	IRDA_DEBUG(0, __FUNCTION__ "(), Found dongle: %s\n", 
		   dongle_types[dongle_id]);
	
	/* Set FIFO threshold to TX17, RX16, reset and enable FIFO's */
	switch_bank(iobase, BANK0);	
	outb(FCR_RXTH|FCR_TXTH|FCR_TXSR|FCR_RXSR|FCR_FIFO_EN, iobase+FCR);
	
	/* Set FIFO size to 32 */
	switch_bank(iobase, BANK2);
	outb(EXCR2_RFSIZ|EXCR2_TFSIZ, iobase+EXCR2);	

	/* IRCR2: FEND_MD is set */
	switch_bank(iobase, BANK5);
	outb(0x2a, iobase+4);

	/* Make sure that some defaults are OK */
	switch_bank(iobase, BANK6);
	outb(0x20, iobase+0); /* Set 32 bits FIR CRC */
	outb(0x0a, iobase+1); /* Set MIR pulse width */
	outb(0x0d, iobase+2); /* Set SIR pulse width */
	outb(0x2a, iobase+4); /* Set beginning frag, and preamble length */

	/* Receiver frame length */
	switch_bank(iobase, BANK4);
	outb(2048 & 0xff, iobase+6);
	outb((2048 >> 8) & 0x1f, iobase+7);

	/* Transmitter frame length */
	outb(2048 & 0xff, iobase+4);
	outb((2048 >> 8) & 0x1f, iobase+5);
	
	IRDA_DEBUG(0, "PC87108 driver loaded. Version: 0x%02x\n", version);

	/* Enable receive interrupts */
	switch_bank(iobase, BANK0);
	outb(IER_RXHDL_IE, iobase+IER);

	return dongle_id;
}

/*
 * Function pc87108_read_dongle_id (void)
 *
 * Try to read dongle indentification. This procedure needs to be executed
 * once after power-on/reset. It also needs to be used whenever you suspect
 * that the user may have plugged/unplugged the IrDA Dongle.
 * 
 */
static int pc87108_read_dongle_id (int iobase)
{
	int dongle_id;
	__u8 bank;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	bank = inb(iobase+BSR);

	/* Select Bank 7 */
	switch_bank(iobase, BANK7);
	
	/* IRCFG4: IRSL0_DS and IRSL21_DS are cleared */
	outb(0x00, iobase+7);
	
	/* ID0, 1, and 2 are pulled up/down very slowly */
	udelay(50);
	
	/* IRCFG1: read the ID bits */
	dongle_id = inb(iobase+4) & 0x0f;

#ifdef BROKEN_DONGLE_ID
	if (dongle_id == 0x0a)
		dongle_id = 0x09;
#endif
	
	/* Go back to  bank 0 before returning */
	switch_bank(iobase, BANK0);

	IRDA_DEBUG(0, __FUNCTION__ "(), Dongle = %#x\n", dongle_id);

	outb(bank, iobase+BSR);

	return dongle_id;
}

/*
 * Function pc87108_init_dongle_interface (iobase, dongle_id)
 *
 *     This function initializes the dongle for the transceiver that is
 *     used. This procedure needs to be executed once after
 *     power-on/reset. It also needs to be used whenever you suspect that
 *     the dongle is changed. 
 */
static void pc87108_init_dongle_interface (int iobase, int dongle_id)
{
	int bank;

	/* Save current bank */
	bank = inb(iobase+BSR);

	/* Select Bank 7 */
	switch_bank(iobase, BANK7);
	
	/* IRCFG4: set according to dongle_id */
	switch (dongle_id) {
	case 0x00: /* same as */
	case 0x01: /* Differential serial interface */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not defined by irda yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x02: /* same as */
	case 0x03: /* Reserved */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not defined by irda yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x04: /* Sharp RY5HD01 */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not supported yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x05: /* Reserved */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not defined by irda yet",
		       dongle_types[dongle_id]); 
		break;
	case 0x06: /* Single-ended serial interface */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not defined by irda yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x07: /* Consumer-IR only */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s is not for IrDA mode\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x08: /* HP HSDL-2300, HP HSDL-3600/HSDL-3610 */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not supported yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x09: /* IBM31T1100 or Temic TFDS6000/TFDS6500 */
		outb_p(0x28, iobase+7); /* Set irsl[0-2] as output */
		break;
	case 0x0A: /* same as */
	case 0x0B: /* Reserved */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not defined by irda yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x0C: /* same as */
	case 0x0D: /* HP HSDL-1100/HSDL-2100 */
		/* 
		 * Set irsl0 as input, irsl[1-2] as output, and separate 
		 * inputs are used for SIR and MIR/FIR 
		 */
		outb(0x48, iobase+7); 
		break;
	case 0x0E: /* Supports SIR Mode only */
		outb(0x28, iobase+7); /* Set irsl[0-2] as output */
		break;
	case 0x0F: /* No dongle connected */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s\n",
		       dongle_types[dongle_id]); 
		IRDA_DEBUG(0, "***\n");

		switch_bank(iobase, BANK0);
		outb(0x62, iobase+MCR);
		break;
	default: 
		IRDA_DEBUG(0, __FUNCTION__ "(), invalid dongle_id %#x", 
			   dongle_id);
	}
	
	/* IRCFG1: IRSL1 and 2 are set to IrDA mode */
	outb(0x00, iobase+4);

	/* Restore bank register */
	outb(bank, iobase+BSR);
	
} /* set_up_dongle_interface */

/*
 * Function pc87108_change_dongle_speed (iobase, speed, dongle_id)
 *
 *    Change speed of the attach dongle
 *
 */
static void pc87108_change_dongle_speed(int iobase, int speed, int dongle_id)
{
	unsigned long flags;
	__u8 bank;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	/* Save current bank */
	bank = inb(iobase+BSR);

	/* Select Bank 7 */
	switch_bank(iobase, BANK7);
	
	/* IRCFG1: set according to dongle_id */
	switch (dongle_id) {
	case 0x00: /* same as */
	case 0x01: /* Differential serial interface */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not defined by irda yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x02: /* same as */
	case 0x03: /* Reserved */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not defined by irda yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x04: /* Sharp RY5HD01 */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not supported yet\n",
		       dongle_types[dongle_id]); 
	case 0x05: /* Reserved */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not defined by irda yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x06: /* Single-ended serial interface */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not defined by irda yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x07: /* Consumer-IR only */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s is not for IrDA mode\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x08: /* HP HSDL-2300, HP HSDL-3600/HSDL-3610 */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not supported yet\n",
		       dongle_types[dongle_id]); 
	case 0x09: /* IBM31T1100 or Temic TFDS6000/TFDS6500 */
		switch_bank(iobase, BANK7);
		outb_p(0x01, iobase+4);

		if (speed == 4000000) {
			save_flags(flags);
			cli();
			outb(0x81, iobase+4);
			outb(0x80, iobase+4);
			restore_flags(flags);
		}
		else
			outb_p(0x00, iobase+4);
		break;
	case 0x0A: /* same as */
	case 0x0B: /* Reserved */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s not defined by irda yet\n",
		       dongle_types[dongle_id]); 
		break;
	case 0x0C: /* same as */
	case 0x0D: /* HP HSDL-1100/HSDL-2100 */
		break;
	case 0x0E: /* Supports SIR Mode only */
		break;
	case 0x0F: /* No dongle connected */
		IRDA_DEBUG(0, __FUNCTION__ "(), %s is not for IrDA mode\n",
		       dongle_types[dongle_id]);

		switch_bank(iobase, BANK0); 
		outb(0x62, iobase+MCR);
		break;
	default: 
		IRDA_DEBUG(0, __FUNCTION__ "(), invalid data_rate\n");
	}
	/* Restore bank register */
	outb(bank, iobase+BSR);
}

/*
 * Function pc87108_change_speed (self, baud)
 *
 *    Change the speed of the device
 *
 */
static void pc87108_change_speed(struct pc87108 *self, __u32 speed)
{
	__u8 mcr = MCR_SIR;
	__u8 bank;
	int iobase; 

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);

	iobase = self->io.iobase;

	/* Update accounting for new speed */
	self->io.speed = speed;

	/* Save current bank */
	bank = inb(iobase+BSR);

	/* Disable interrupts */
	switch_bank(iobase, BANK0);
	outb(0, iobase+IER);

	  /* Select Bank 2 */
	switch_bank(iobase, BANK2);

	outb(0x00, iobase+BGDH);
	switch (speed) {
	case 9600:   outb(0x0c, iobase+BGDL); break;
	case 19200:  outb(0x06, iobase+BGDL); break;
	case 37600:  outb(0x03, iobase+BGDL); break;
	case 57600:  outb(0x02, iobase+BGDL); break;
	case 115200: outb(0x01, iobase+BGDL); break;
	case 576000:
		switch_bank(iobase, BANK5);
		
		/* IRCR2: MDRS is set */
		outb(inb(iobase+4) | 0x04, iobase+4);
	       
		mcr = MCR_MIR;
		IRDA_DEBUG(0, __FUNCTION__ "(), handling baud of 576000\n");
		break;
	case 1152000:
		mcr = MCR_MIR;
		IRDA_DEBUG(0, __FUNCTION__ "(), handling baud of 1152000\n");
		break;
	case 4000000:
		mcr = MCR_FIR;
		IRDA_DEBUG(0, __FUNCTION__ "(), handling baud of 4000000\n");
		break;
	default:
		mcr = MCR_FIR;
		IRDA_DEBUG(0, __FUNCTION__ "(), unknown baud rate of %d\n", 
			   speed);
		break;
	}

	/* Set appropriate speed mode */
	switch_bank(iobase, BANK0);
	outb(mcr | MCR_TX_DFR, iobase+MCR);

	/* Give some hits to the transceiver */
	pc87108_change_dongle_speed(iobase, speed, self->io.dongle_id);

	/* Set FIFO threshold to TX17, RX16 */
	switch_bank(iobase, BANK0);
	outb(FCR_RXTH|     /* Set Rx FIFO threshold */
	      FCR_TXTH|     /* Set Tx FIFO threshold */
	      FCR_TXSR|     /* Reset Tx FIFO */
	      FCR_RXSR|     /* Reset Rx FIFO */
	      FCR_FIFO_EN,  /* Enable FIFOs */
	      iobase+FCR);
	/* outb(0xa7, iobase+FCR); */
	
	/* Set FIFO size to 32 */
	switch_bank(iobase, BANK2);
	outb(EXCR2_RFSIZ|EXCR2_TFSIZ, iobase+EXCR2);	
	
	self->netdev->tbusy = 0;
	
	/* Enable some interrupts so we can receive frames */
	switch_bank(iobase, BANK0); 
	if (speed > 115200) {
		outb(IER_SFIF_IE, iobase+IER);
		pc87108_dma_receive(self);
	} else
		outb(IER_RXHDL_IE, iobase+IER);
    	
	/* Restore BSR */
	outb(bank, iobase+BSR);
}

/*
 * Function pc87108_hard_xmit (skb, dev)
 *
 *    Transmit the frame!
 *
 */
static int pc87108_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct pc87108 *self;
	int iobase;
	__u32 speed;
	__u8 bank;
	int mtt;
	
	self = (struct pc87108 *) dev->priv;

	ASSERT(self != NULL, return 0;);

	iobase = self->io.iobase;

	IRDA_DEBUG(4, __FUNCTION__ "(%ld), skb->len=%d\n", jiffies, 
		   (int) skb->len);
	
	/* Lock transmit buffer */
	if (irda_lock((void *) &dev->tbusy) == FALSE)
		return -EBUSY;

	/* Check if we need to change the speed */
	if ((speed = irda_get_speed(skb)) != self->io.speed)
		self->new_speed = speed;

	/* Save current bank */
	bank = inb(iobase+BSR);

	/* Decide if we should use PIO or DMA transfer */
	if (self->io.speed > 115200) {
		self->tx_buff.data = self->tx_buff.head;
		memcpy(self->tx_buff.data, skb->data, skb->len);
		self->tx_buff.len = skb->len;

		mtt = irda_get_mtt(skb);
	        if (mtt > 50) {
			/* Adjust for timer resolution */
			mtt = mtt / 125 + 1;

			/* Setup timer */
			switch_bank(iobase, BANK4);
			outb(mtt & 0xff, iobase+TMRL);
			outb((mtt >> 8) & 0x0f, iobase+TMRH);
			
			/* Start timer */
			outb(IRCR1_TMR_EN, iobase+IRCR1);
			self->io.direction = IO_XMIT;
			
			/* Enable timer interrupt */
			switch_bank(iobase, BANK0);
			outb(IER_TMR_IE, iobase+IER);
		} else {
			/* Use udelay for delays less than 50 us. */
			if (mtt)
				udelay(mtt);

			/* Enable DMA interrupt */
			switch_bank(iobase, BANK0);
	 		outb(IER_DMA_IE, iobase+IER);
	     		pc87108_dma_write(self, iobase);
		}
        } else {
	        self->tx_buff.len = async_wrap_skb(skb, self->tx_buff.data, 
						   self->tx_buff.truesize);
		
		self->tx_buff.data = self->tx_buff.head;
		
		/* Add interrupt on tx low level (will fire immediately) */
		switch_bank(iobase, BANK0);
		outb(IER_TXLDL_IE, iobase+IER);
	}
	dev_kfree_skb(skb);

	/* Restore bank register */
	outb(bank, iobase+BSR);

	return 0;
}

/*
 * Function pc87108_dma_xmit (self, iobase)
 *
 *    Transmit data using DMA
 *
 */
static void pc87108_dma_write(struct pc87108 *self, int iobase)
{
	int bsr;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	/* Save current bank */
	bsr = inb(iobase+BSR);

	/* Disable DMA */
	switch_bank(iobase, BANK0);
	outb(inb(iobase+MCR) & ~MCR_DMA_EN, iobase+MCR);

	setup_dma(self->io.dma, self->tx_buff.data, self->tx_buff.len, 
		  DMA_MODE_WRITE);
	
	self->io.direction = IO_XMIT;
	
	/* Choose transmit DMA channel  */ 
	switch_bank(iobase, BANK2);
	outb(inb(iobase+ECR1) | ECR1_DMASWP|ECR1_DMANF|ECR1_EXT_SL, 
	     iobase+ECR1);
	
	/* Enable DMA */
 	switch_bank(iobase, BANK0);	
	outb(inb(iobase+MCR)|MCR_DMA_EN, iobase+MCR);

	/* Restore bank register */
	outb(bsr, iobase+BSR);
}

/*
 * Function pc87108_pio_xmit (self, iobase)
 *
 *    Transmit data using PIO. Returns the number of bytes that actually
 *    got transfered
 *
 */
static int pc87108_pio_write(int iobase, __u8 *buf, int len, int fifo_size)
{
	int actual = 0;
	__u8 bank;
	
	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	/* Save current bank */
	bank = inb(iobase+BSR);

	switch_bank(iobase, BANK0);
	if (!(inb_p(iobase+LSR) & LSR_TXEMP)) {
		IRDA_DEBUG(4, __FUNCTION__ 
			   "(), warning, FIFO not empty yet!\n");

		fifo_size -= 17;
		IRDA_DEBUG(4, __FUNCTION__ "(), %d bytes left in tx fifo\n", 
			   fifo_size);
	}

	/* Fill FIFO with current frame */
	while ((fifo_size-- > 0) && (actual < len)) {
		/* Transmit next byte */
		outb(buf[actual++], iobase+TXD);
	}
        
	IRDA_DEBUG(4, __FUNCTION__ "(), fifo_size %d ; %d sent of %d\n", 
		   fifo_size, actual, len);
	
	/* Restore bank */
	outb(bank, iobase+BSR);

	return actual;
}

/*
 * Function pc87108_dma_xmit_complete (self)
 *
 *    The transfer of a frame in finished. This function will only be called 
 *    by the interrupt handler
 *
 */
static void pc87108_dma_xmit_complete(struct pc87108 *self)
{
	int iobase;
	__u8 bank;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);

	iobase = self->io.iobase;

	/* Save current bank */
	bank = inb(iobase+BSR);

	/* Disable DMA */
	switch_bank(iobase, BANK0);
	outb(inb(iobase+MCR) & ~MCR_DMA_EN, iobase+MCR);
	
	/* Check for underrrun! */
	if (inb(iobase+ASCR) & ASCR_TXUR) {
		self->stats.tx_errors++;
		self->stats.tx_fifo_errors++;
		
		/* Clear bit, by writing 1 into it */
		outb(ASCR_TXUR, iobase+ASCR);
	} else {
		self->stats.tx_packets++;
		self->stats.tx_bytes +=  self->tx_buff.len;
	}
	
	if (self->new_speed) {
		pc87108_change_speed(self, self->new_speed);
		self->new_speed = 0;
	}

	/* Unlock tx_buff and request another frame */
	self->netdev->tbusy = 0; /* Unlock */
	
	/* Tell the network layer, that we can accept more frames */
	mark_bh(NET_BH);

	/* Restore bank */
	outb(bank, iobase+BSR);
}

/*
 * Function pc87108_dma_receive (self)
 *
 *    Get ready for receiving a frame. The device will initiate a DMA
 *    if it starts to receive a frame.
 *
 */
static int pc87108_dma_receive(struct pc87108 *self) 
{
	int iobase;
	__u8 bsr;

	ASSERT(self != NULL, return -1;);

	IRDA_DEBUG(4, __FUNCTION__ "\n");

	iobase = self->io.iobase;

	/* Save current bank */
	bsr = inb(iobase+BSR);

	/* Disable DMA */
	switch_bank(iobase, BANK0);
	outb(inb(iobase+MCR) & ~MCR_DMA_EN, iobase+MCR);

	setup_dma(self->io.dma, self->rx_buff.data, 
		  self->rx_buff.truesize, DMA_MODE_READ);
	
	/* driver->media_busy = FALSE; */
	self->io.direction = IO_RECV;
	self->rx_buff.data = self->rx_buff.head;

	/* Reset Rx FIFO. This will also flush the ST_FIFO */
	outb(FCR_RXTH|FCR_TXTH|FCR_RXSR|FCR_FIFO_EN, iobase+FCR);
	self->st_fifo.len = self->st_fifo.tail = self->st_fifo.head = 0;

	/* Choose DMA Rx, DMA Fairness, and Advanced mode */
	switch_bank(iobase, BANK2);
	outb((inb(iobase+ECR1) & ~ECR1_DMASWP)|ECR1_DMANF|ECR1_EXT_SL, 
	     iobase+ECR1);
	
	/* enable DMA */
	switch_bank(iobase, BANK0);
	outb(inb(iobase+MCR)|MCR_DMA_EN, iobase+MCR);

	/* Restore bank register */
	outb(bsr, iobase+BSR);
	
	IRDA_DEBUG(4, __FUNCTION__ "(), done!\n");	
	
	return 0;
}

/*
 * Function pc87108_dma_receive_complete (self)
 *
 *    Finished with receiving frames
 *
 *    
 */
static int pc87108_dma_receive_complete(struct pc87108 *self, int iobase)
{
	struct sk_buff *skb;
	struct st_fifo *st_fifo;
	__u8 bank;
	__u8 status;
	int len;

	st_fifo = &self->st_fifo;

	/* Save current bank */
	bank = inb(iobase+BSR);
	
	/* Read status FIFO */
	switch_bank(iobase, BANK5);
	while ((status = inb(iobase+FRM_ST)) & FRM_ST_VLD) {
		st_fifo->entries[st_fifo->tail].status = status;

		st_fifo->entries[st_fifo->tail].len  = inb(iobase+RFLFL);
		st_fifo->entries[st_fifo->tail].len |= inb(iobase+RFLFH) << 8;
		
		st_fifo->tail++;
		st_fifo->len++;
	}
	
	/* Try to process all entries in status FIFO */
	switch_bank(iobase, BANK0);
	while (st_fifo->len) {
      
		/* Get first entry */
		status = st_fifo->entries[st_fifo->head].status;
		len    = st_fifo->entries[st_fifo->head].len;
		st_fifo->head++;
		st_fifo->len--;

		/* Check for errors */
		if (status & FRM_ST_ERR_MSK) {
			if (status & FRM_ST_LOST_FR) {
				/* Add number of lost frames to stats */
				self->stats.rx_errors += len;	
			} else {
				/* Skip frame */
				self->stats.rx_errors++;
				
				self->rx_buff.data += len;
			
				if (status & FRM_ST_MAX_LEN)
					self->stats.rx_length_errors++;
				
				if (status & FRM_ST_PHY_ERR) 
					self->stats.rx_frame_errors++;
				
				if (status & FRM_ST_BAD_CRC) 
					self->stats.rx_crc_errors++;
			}
			/* The errors below can be reported in both cases */
			if (status & FRM_ST_OVR1)
				self->stats.rx_fifo_errors++;
			
			if (status & FRM_ST_OVR2)
				self->stats.rx_fifo_errors++;
			
		} else {
			/* Check if we have transfered all data to memory */
			if (inb(iobase+LSR) & LSR_RXDA) {
				/* Put this entry back in fifo */
				st_fifo->head--;
				st_fifo->len++;
				st_fifo->entries[st_fifo->head].status = status;
				st_fifo->entries[st_fifo->head].len = len;

				/* Restore bank register */
				outb(bank, iobase+BSR);
			
				return FALSE; 	/* I'll be back! */
			}

			/* Should be OK then */			
			skb = dev_alloc_skb(len+1);
			if (skb == NULL)  {
				printk(KERN_INFO __FUNCTION__ 
					"(), memory squeeze, dropping frame.\n");
				/* Restore bank register */
				outb(bank, iobase+BSR);

				return FALSE;
			}
			
			/* Make sure IP header gets aligned */
			skb_reserve(skb, 1); 

			/* Copy frame without CRC */
			if (self->io.speed < 4000000) {
				skb_put(skb, len-2);
				memcpy(skb->data, self->rx_buff.data, len-2);
			} else {
				skb_put(skb, len-4);
				memcpy(skb->data, self->rx_buff.data, len-4);
			}

			/* Move to next frame */
			self->rx_buff.data += len;
			self->stats.rx_packets++;

			skb->dev = self->netdev;
			skb->mac.raw  = skb->data;
			skb->protocol = htons(ETH_P_IRDA);
			netif_rx(skb);
		}
	}
	/* Restore bank register */
	outb(bank, iobase+BSR);

	return TRUE;
}

/*
 * Function pc87108_pio_receive (self)
 *
 *    Receive all data in receiver FIFO
 *
 */
static void pc87108_pio_receive(struct pc87108 *self) 
{
	__u8 byte = 0x00;
	int iobase;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	
	iobase = self->io.iobase;
	
	/*  Receive all characters in Rx FIFO */
	do {
		byte = inb(iobase+RXD);
		async_unwrap_char(self->netdev, &self->stats, &self->rx_buff, 
				  byte);

	} while (inb(iobase+LSR) & LSR_RXDA); /* Data available */	
}

/*
 * Function pc87108_sir_interrupt (self, eir)
 *
 *    Handle SIR interrupt
 *
 */
static __u8 pc87108_sir_interrupt(struct pc87108 *self, int eir)
{
	int actual;
	__u8 new_ier = 0;

	/* Transmit FIFO low on data */
	if (eir & EIR_TXLDL_EV) {
		/* Write data left in transmit buffer */
		actual = pc87108_pio_write(self->io.iobase, 
					   self->tx_buff.data, 
					   self->tx_buff.len, 
					   self->io.fifo_size);
		self->tx_buff.data += actual;
		self->tx_buff.len  -= actual;
		
		self->io.direction = IO_XMIT;

		/* Check if finished */
		if (self->tx_buff.len > 0)
			new_ier |= IER_TXLDL_IE;
		else { 
			self->netdev->tbusy = 0; /* Unlock */
			self->stats.tx_packets++;

			/* Check if we need to change the speed? */
			if (self->new_speed) {
				IRDA_DEBUG(2, __FUNCTION__ 
					   "(), Changing speed!\n");
				pc87108_change_speed(self, self->new_speed);
				self->new_speed = 0;
			}
			
		        mark_bh(NET_BH);	

			new_ier |= IER_TXEMP_IE;
		}
			
	}
	/* Check if transmission has completed */
	if (eir & EIR_TXEMP_EV) {
		
		/* Turn around and get ready to receive some data */
		self->io.direction = IO_RECV;
		new_ier |= IER_RXHDL_IE;
	}

	/* Rx FIFO threshold or timeout */
	if (eir & EIR_RXHDL_EV) {
		pc87108_pio_receive(self);

		/* Keep receiving */
		new_ier |= IER_RXHDL_IE;
	}
	return new_ier;
}

/*
 * Function pc87108_fir_interrupt (self, eir)
 *
 *    Handle MIR/FIR interrupt
 *
 */
static __u8 pc87108_fir_interrupt(struct pc87108 *self, int iobase,
				  int eir)
{
	__u8 new_ier = 0;
	__u8 bank;

	bank = inb(iobase+BSR);
	
	/* Status event, or end of frame detected in FIFO */
	if (eir & (EIR_SFIF_EV|EIR_LS_EV)) {
		if (pc87108_dma_receive_complete(self, iobase)) {

			/* Wait for next status FIFO interrupt */
			new_ier |= IER_SFIF_IE;
		} else {
			/* DMA not finished yet */

			/* Set timer value, resolution 125 us */
			switch_bank(iobase, BANK4);
			outb(0x0f, iobase+TMRL); /* 125 us */
			outb(0x00, iobase+TMRH);

			/* Start timer */
			outb(IRCR1_TMR_EN, iobase+IRCR1);

			new_ier |= IER_TMR_IE;
		}
	}
	/* Timer finished */
	if (eir & EIR_TMR_EV) {
		/* Disable timer */
		switch_bank(iobase, BANK4);
		outb(0, iobase+IRCR1);

		/* Clear timer event */
		switch_bank(iobase, BANK0);
		outb(ASCR_CTE, iobase+ASCR);

		/* Check if this is a TX timer interrupt */
		if (self->io.direction == IO_XMIT) {
			pc87108_dma_write(self, iobase);

			/*  Interrupt on DMA */
			new_ier |= IER_DMA_IE;
		} else {
			/* Check if DMA has now finished */
			pc87108_dma_receive_complete(self, iobase);

			new_ier |= IER_SFIF_IE;
		}
	}	
	/* Finished with transmission */
	if (eir & EIR_DMA_EV) {
		pc87108_dma_xmit_complete(self);
		
		/* Check if there are more frames to be transmitted */
		if (irda_device_txqueue_empty(self->netdev)) {
			/* Prepare for receive */
			pc87108_dma_receive(self);
			
			new_ier = IER_LS_IE|IER_SFIF_IE;
		}
	}
	outb(bank, iobase+BSR);

	return new_ier;
}

/*
 * Function pc87108_interrupt (irq, dev_id, regs)
 *
 *    An interrupt from the chip has arrived. Time to do some work
 *
 */
static void pc87108_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct pc87108 *self;
	__u8 bsr, eir, ier;
	int iobase;

	if (!dev) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n", 
			driver_name, irq);
		return;
	}
	self = (struct pc87108 *) dev->priv;

	dev->interrupt = 1;

	iobase = self->io.iobase;

	/* Save current bank */
	bsr = inb(iobase+BSR);

	switch_bank(iobase, BANK0);	
	ier = inb(iobase+IER); 
	eir = inb(iobase+EIR) & ier; /* Mask out the interesting ones */ 

	outb(0, iobase+IER); /* Disable interrupts */
	
	if (eir) {
		/* Dispatch interrupt handler for the current speed */
		if (self->io.speed > 115200)
			ier = pc87108_fir_interrupt(self, iobase, eir);
		else
			ier = pc87108_sir_interrupt(self, eir);
	}

	outb(ier, iobase+IER);   /* Restore interrupts */
	outb(bsr, iobase+BSR);   /* Restore bank register */

	dev->interrupt = 0;
}

/*
 * Function pc87108_wait_until_sent (self)
 *
 *    This function should put the current thread to sleep until all data 
 *    have been sent, so it is safe to f.eks. change the speed.
 */
static void pc87108_wait_until_sent(struct pc87108 *self)
{
	/* Just delay 60 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(60));
}

/*
 * Function pc87108_is_receiving (self)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int pc87108_is_receiving(struct pc87108 *self)
{
	int status = FALSE;
	int iobase;
	__u8 bank;

	ASSERT(self != NULL, return FALSE;);

	if (self->io.speed > 115200) {
		iobase = self->io.iobase;

		/* Check if rx FIFO is not empty */
		bank = inb(iobase+BSR);
		switch_bank(iobase, BANK2);
		if ((inb(iobase+RXFLV) & 0x3f) != 0) {
			/* We are receiving something */
			status =  TRUE;
		}
		outb(bank, iobase+BSR);
	} else 
		status = (self->rx_buff.state != OUTSIDE_FRAME);
	
	return status;
}

/*
 * Function pc87108_net_init (dev)
 *
 *    Initialize network device
 *
 */
static int pc87108_net_init(struct net_device *dev)
{
	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	/* Setup to be a normal IrDA network device driver */
	irda_device_setup(dev);

	/* Insert overrides below this line! */

	return 0;
}


/*
 * Function pc87108_net_open (dev)
 *
 *    Start the device
 *
 */
static int pc87108_net_open(struct net_device *dev)
{
	struct pc87108 *self;
	int iobase;
	__u8 bank;
	
	IRDA_DEBUG(4, __FUNCTION__ "()\n");
	
	ASSERT(dev != NULL, return -1;);
	self = (struct pc87108 *) dev->priv;
	
	ASSERT(self != NULL, return 0;);
	
	iobase = self->io.iobase;

	if (request_irq(self->io.irq, pc87108_interrupt, 0, dev->name, 
			(void *) dev)) 
	{
		return -EAGAIN;
	}
	/*
	 * Always allocate the DMA channel after the IRQ,
	 * and clean up on failure.
	 */
	if (request_dma(self->io.dma, dev->name)) {
		free_irq(self->io.irq, self);
		return -EAGAIN;
	}
	
	/* Save current bank */
	bank = inb(iobase+BSR);
	
	/* turn on interrupts */
	switch_bank(iobase, BANK0);
	outb(IER_LS_IE | IER_RXHDL_IE, iobase+IER);

	/* Restore bank register */
	outb(bank, iobase+BSR);

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

	return 0;
}

/*
 * Function pc87108_net_close (dev)
 *
 *    Stop the device
 *
 */
static int pc87108_net_close(struct net_device *dev)
{
	struct pc87108 *self;
	int iobase;
	__u8 bank;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");
	
	ASSERT(dev != NULL, return -1;);
	self = (struct pc87108 *) dev->priv;
	
	ASSERT(self != NULL, return 0;);

	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	/* Stop and remove instance of IrLAP */
	if (self->irlap)
		irlap_close(self->irlap);
	self->irlap = NULL;
	
	iobase = self->io.iobase;

	disable_dma(self->io.dma);

	/* Save current bank */
	bank = inb(iobase+BSR);

	/* Disable interrupts */
	switch_bank(iobase, BANK0);
	outb(0, iobase+IER); 
       
	free_irq(self->io.irq, dev);
	free_dma(self->io.dma);

	/* Restore bank register */
	outb(bank, iobase+BSR);

	MOD_DEC_USE_COUNT;

	return 0;
}

/*
 * Function pc87108_net_ioctl (dev, rq, cmd)
 *
 *    Process IOCTL commands for this device
 *
 */
static int pc87108_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct pc87108 *self;
	unsigned long flags;
	int ret = 0;

	ASSERT(dev != NULL, return -1;);

	self = dev->priv;

	ASSERT(self != NULL, return -1;);

	IRDA_DEBUG(2, __FUNCTION__ "(), %s, (cmd=0x%X)\n", dev->name, cmd);
	
	/* Disable interrupts & save flags */
	save_flags(flags);
	cli();
	
	switch (cmd) {
	case SIOCSBANDWIDTH: /* Set bandwidth */
		pc87108_change_speed(self, irq->ifr_baudrate);
		break;
	case SIOCSMEDIABUSY: /* Set media busy */
		irda_device_set_media_busy(self->netdev, TRUE);
		break;
	case SIOCGRECEIVING: /* Check if we are receiving right now */
		irq->ifr_receiving = pc87108_is_receiving(self);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	
	restore_flags(flags);
	
	return ret;
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("NSC PC87108 IrDA Device Driver");

MODULE_PARM(qos_mtt_bits, "i");
MODULE_PARM(io, "1-4i");
MODULE_PARM(io2, "1-4i");
MODULE_PARM(irq, "1-4i");

/*
 * Function init_module (void)
 *
 *    
 *
 */
int init_module(void)
{
	return pc87108_init();
}

/*
 * Function cleanup_module (void)
 *
 *    
 *
 */
void cleanup_module(void)
{
	pc87108_cleanup();
}
#endif /* MODULE */

