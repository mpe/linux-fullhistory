/*********************************************************************
 *                
 * Filename:      nsc_fir.c
 * Version:       1.0
 * Description:   Driver for the NSC PC'108 and PC'338 IrDA chipsets
 * Status:        Stable.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Nov  7 21:43:15 1998
 * Modified at:   Wed Jan  5 13:59:21 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-2000 Dag Brattli <dagb@cs.uit.no>
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

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif

#include <net/irda/wrapper.h>
#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/irda_device.h>

#include <net/irda/nsc_fir.h>

#define CHIP_IO_EXTENT 8
#define BROKEN_DONGLE_ID

/* 
 * Define if you have multiple NSC IrDA controllers in your machine. Not
 * enabled by default since some single chips detects at multiple addresses
 */
#undef  CONFIG_NSC_FIR_MULTIPLE 

static char *driver_name = "nsc_fir";

/* Module parameters */
static int qos_mtt_bits = 0x07;  /* 1 ms or more */
static int dongle_id = 0;

static unsigned int io[]  = { 0x2f8, 0x2f8, 0x2f8, 0x2f8, 0x2f8 };
static unsigned int io2[] = { 0x150, 0x398, 0xea, 0x15c, 0x2e };
static unsigned int irq[] = { 3, 3, 3, 3, 3 };
static unsigned int dma[] = { 0, 0, 0, 0, 3 };

static struct nsc_fir_cb *dev_self[] = { NULL, NULL, NULL, NULL, NULL };

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
static int  nsc_fir_open(int i, unsigned int iobase, unsigned int board_addr, 
			 unsigned int irq, unsigned int dma);
#ifdef MODULE
static int  nsc_fir_close(struct nsc_fir_cb *self);
#endif /* MODULE */
static int  nsc_fir_probe(int iobase, int board_addr, int irq, int dma);
static void nsc_fir_pio_receive(struct nsc_fir_cb *self);
static int  nsc_fir_dma_receive(struct nsc_fir_cb *self); 
static int  nsc_fir_dma_receive_complete(struct nsc_fir_cb *self, int iobase);
static int  nsc_fir_hard_xmit_sir(struct sk_buff *skb, struct net_device *dev);
static int  nsc_fir_hard_xmit_fir(struct sk_buff *skb, struct net_device *dev);
static int  nsc_fir_pio_write(int iobase, __u8 *buf, int len, int fifo_size);
static void nsc_fir_dma_xmit(struct nsc_fir_cb *self, int iobase);
static void nsc_fir_change_speed(struct nsc_fir_cb *self, __u32 baud);
static void nsc_fir_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int  nsc_fir_is_receiving(struct nsc_fir_cb *self);
static int  nsc_fir_read_dongle_id (int iobase);
static void nsc_fir_init_dongle_interface (int iobase, int dongle_id);

static int  nsc_fir_net_init(struct net_device *dev);
static int  nsc_fir_net_open(struct net_device *dev);
static int  nsc_fir_net_close(struct net_device *dev);
static int  nsc_fir_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static struct net_device_stats *nsc_fir_net_get_stats(struct net_device *dev);
#ifdef CONFIG_APM
static int nsc_fir_apmproc(apm_event_t event);
#endif /* CONFIG_APM */
/*
 * Function nsc_fir_init ()
 *
 *    Initialize chip. Just try to find out how many chips we are dealing with
 *    and where they are
 */
int __init nsc_fir_init(void)
{
	int ret = -ENODEV;
	int ioaddr;
	int i;

#ifdef CONFIG_APM
	apm_register_callback(nsc_fir_apmproc);
#endif /* CONFIG_APM */

	for (i=0; (io[i] < 2000) && (i < 5); i++) {
		ioaddr = io[i];
		if (check_region(ioaddr, CHIP_IO_EXTENT) < 0)
			continue;
		if (nsc_fir_open(i, io[i], io2[i], irq[i], dma[i]) == 0)
		{
#ifdef CONFIG_NSC_FIR_MULTIPLE
			ret = 0;
#else
		        return 0;
#endif
		}
	}

	return ret;
}

/*
 * Function nsc_fir_cleanup ()
 *
 *    Close all configured chips
 *
 */
#ifdef MODULE
static void nsc_fir_cleanup(void)
{
	int i;

        IRDA_DEBUG(4, __FUNCTION__ "()\n");

#ifdef CONFIG_APM
	apm_unregister_callback(nsc_fir_apmproc);
#endif /* CONFIG_APM */


	for (i=0; i < 5; i++) {
		if (dev_self[i])
			nsc_fir_close(dev_self[i]);
	}
}
#endif /* MODULE */

/*
 * Function nsc_fir_open (iobase, irq)
 *
 *    Open driver instance
 *
 */
static int 
nsc_fir_open(int i, unsigned int iobase, unsigned int board_addr, 
	     unsigned int irq, unsigned int dma)
{
	struct net_device *dev;
	struct nsc_fir_cb *self;
	int dongle_id;
	int ret;
	int err;

	IRDA_DEBUG(2, __FUNCTION__ "()\n");

	if ((dongle_id = nsc_fir_probe(iobase, board_addr, irq, dma)) == -1)
		return -1;

	/*
	 *  Allocate new instance of the driver
	 */
	self = kmalloc(sizeof(struct nsc_fir_cb), GFP_KERNEL);
	if (self == NULL) {
		ERROR(__FUNCTION__ "(), can't allocate memory for "
		       "control block!\n");
		return -ENOMEM;
	}
	memset(self, 0, sizeof(struct nsc_fir_cb));
	spin_lock_init(&self->lock);
   
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
		/* nsc_fir_cleanup(self->self);  */
		return -ENODEV;
	}
	request_region(self->io.iobase, self->io.io_ext, driver_name);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&self->qos);
	
	/* The only value we must override it the baudrate */
	self->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200|IR_576000|IR_1152000 |(IR_4000000 << 8);
	
	self->qos.min_turn_time.bits = qos_mtt_bits;
	irda_qos_bits_to_value(&self->qos);
	
	self->flags = IFF_FIR|IFF_MIR|IFF_SIR|IFF_DMA|IFF_PIO|IFF_DONGLE;

	/* Max DMA buffer size needed = (data_size + 6) * (window_size) + 6; */
	self->rx_buff.truesize = 14384; 
	self->tx_buff.truesize = 14384;

	/* Allocate memory if needed */
	self->rx_buff.head = (__u8 *) kmalloc(self->rx_buff.truesize,
					      GFP_KERNEL|GFP_DMA);
	if (self->rx_buff.head == NULL)
		return -ENOMEM;
	memset(self->rx_buff.head, 0, self->rx_buff.truesize);
	
	self->tx_buff.head = (__u8 *) kmalloc(self->tx_buff.truesize, 
					      GFP_KERNEL|GFP_DMA);
	if (self->tx_buff.head == NULL) {
		kfree(self->rx_buff.head);
		return -ENOMEM;
	}
	memset(self->tx_buff.head, 0, self->tx_buff.truesize);

	self->rx_buff.in_frame = FALSE;
	self->rx_buff.state = OUTSIDE_FRAME;
	self->tx_buff.data = self->tx_buff.head;
	self->rx_buff.data = self->rx_buff.head;
	
	/* Reset Tx queue info */
	self->tx_fifo.len = self->tx_fifo.ptr = self->tx_fifo.free = 0;
	self->tx_fifo.tail = self->tx_buff.head;

	if (!(dev = dev_alloc("irda%d", &err))) {
		ERROR(__FUNCTION__ "(), dev_alloc() failed!\n");
		return -ENOMEM;
	}

	dev->priv = (void *) self;
	self->netdev = dev;

	/* Override the network functions we need to use */
	dev->init            = nsc_fir_net_init;
	dev->hard_start_xmit = nsc_fir_hard_xmit_sir;
	dev->open            = nsc_fir_net_open;
	dev->stop            = nsc_fir_net_close;
	dev->do_ioctl        = nsc_fir_net_ioctl;
	dev->get_stats	     = nsc_fir_net_get_stats;

	rtnl_lock();
	err = register_netdevice(dev);
	rtnl_unlock();
	if (err) {
		ERROR(__FUNCTION__ "(), register_netdev() failed!\n");
		return -1;
	}

	MESSAGE("IrDA: Registered device %s\n", dev->name);
	
	self->io.dongle_id = dongle_id;
	nsc_fir_init_dongle_interface(iobase, dongle_id);

	return 0;
}

#ifdef MODULE
/*
 * Function nsc_fir_close (self)
 *
 *    Close driver instance
 *
 */
static int nsc_fir_close(struct nsc_fir_cb *self)
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
 * Function nsc_fir_init_807 (iobase, board_addr, irq, dma)
 *
 *    Initialize the NSC '108 chip
 *
 */
static void nsc_fir_init_807(int iobase, int board_addr, int irq, int dma)
{
	__u8 temp=0;

	outb(2, board_addr);      /* Mode Control Register (MCTL) */
	outb(0x00, board_addr+1); /* Disable device */
	
	/* Base Address and Interrupt Control Register (BAIC) */
	outb(0, board_addr);
	switch (iobase) {
	case 0x3e8: outb(0x14, board_addr+1); break;
	case 0x2e8: outb(0x15, board_addr+1); break;
		case 0x3f8: outb(0x16, board_addr+1); break;
	case 0x2f8: outb(0x17, board_addr+1); break;
	default: ERROR(__FUNCTION__ "(), invalid base_address");
	}
	
	/* Control Signal Routing Register (CSRT) */
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
	default: ERROR(__FUNCTION__ "(), invalid dma");
	}
	
	outb(2, board_addr);      /* Mode Control Register (MCTL) */
	outb(0x03, board_addr+1); /* Enable device */
}

/*
 * Function nsc_fir_init_338 (iobase, board_addr, irq, dma)
 *
 *    Initialize the NSC '338 chip. Remember that the 87338 needs two 
 *    consecutive writes to the data registers while CPU interrupts are
 *    disabled. The 97338 does not require this, but shouldn't be any
 *    harm if we do it anyway.
 */
static void nsc_fir_init_338(int iobase, int board_addr, int irq, int dma)
{
	/* No init yet */
}

static int nsc_fir_find_chip(int board_addr)
{
	__u8 index, id;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	/* Read index register */
	index = inb(board_addr);
	if (index == 0xff) {
		IRDA_DEBUG(0, __FUNCTION__ "(), no chip at 0x%03x\n", 
			   board_addr);
		return -1;
	}

	/* Read chip identification register (SID) for the PC97338 */
	outb(8, board_addr);
	id = inb(board_addr+1);
	if ((id & 0xf0) == PC97338) {
		MESSAGE("%s, Found NSC PC97338 chip, revision=%d\n", 
			driver_name, id & 0x0f);
		return PC97338;		
	} 

	/* Read device identification (DID) for the PC87108 */
	outb(5, board_addr);
	id = inb(board_addr+1);
	if ((id & 0xf0) == PC87108) {
		MESSAGE("%s, Found NSC PC87108 chip, revision=%d\n", 
			driver_name, id & 0x0f);
		return PC87108;
	}

	return -1;
}

/*
 * Function nsc_fir_probe (iobase, board_addr, irq, dma)
 *
 *    Returns non-negative on success.
 *
 */
static int nsc_fir_probe(int iobase, int board_addr, int irq, int dma)
{
	int version;
	__u8 chip;

	chip = nsc_fir_find_chip(board_addr);
	switch (chip) {
	case PC87108:
		nsc_fir_init_807(iobase, board_addr, irq, dma);
		break;
	case PC97338:
		nsc_fir_init_338(iobase, board_addr, irq, dma);
		break;
	default:
		/* Found no chip */
		return -1;
	}

	/* Read the Module ID */
	switch_bank(iobase, BANK3);
	version = inb(iobase+MID);

	/* Should be 0x2? */
	if (0x20 != (version & 0xf0)) {
		ERROR("%s, Wrong chip version %02x\n", driver_name, version);
		return -1;
	}
	MESSAGE("%s, Found chip at base=0x%04x\n", driver_name, board_addr);

	/* Switch to advanced mode */
	switch_bank(iobase, BANK2);
	outb(ECR1_EXT_SL, iobase+ECR1);
	switch_bank(iobase, BANK0);

	/* Check if user has supplied the dongle id or not */
	if (!dongle_id) {
		dongle_id = nsc_fir_read_dongle_id(iobase);
		
		MESSAGE("%s, Found dongle: %s\n", driver_name,
			dongle_types[dongle_id]);
	} else {
		MESSAGE("%s, Using dongle: %s\n", driver_name,
			dongle_types[dongle_id]);
	}
	
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

	MESSAGE("%s, driver loaded (Dag Brattli)\n", driver_name);

	/* Enable receive interrupts */
	switch_bank(iobase, BANK0);
	outb(IER_RXHDL_IE, iobase+IER);

	return dongle_id;
}

/*
 * Function nsc_fir_read_dongle_id (void)
 *
 * Try to read dongle indentification. This procedure needs to be executed
 * once after power-on/reset. It also needs to be used whenever you suspect
 * that the user may have plugged/unplugged the IrDA Dongle.
 * 
 */
static int nsc_fir_read_dongle_id (int iobase)
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

	outb(bank, iobase+BSR);

	return dongle_id;
}

/*
 * Function nsc_fir_init_dongle_interface (iobase, dongle_id)
 *
 *     This function initializes the dongle for the transceiver that is
 *     used. This procedure needs to be executed once after
 *     power-on/reset. It also needs to be used whenever you suspect that
 *     the dongle is changed. 
 */
static void nsc_fir_init_dongle_interface (int iobase, int dongle_id)
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
	case 0x05: /* Reserved, but this is what the Thinkpad reports */
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
 * Function nsc_fir_change_dongle_speed (iobase, speed, dongle_id)
 *
 *    Change speed of the attach dongle
 *
 */
static void nsc_fir_change_dongle_speed(int iobase, int speed, int dongle_id)
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
		} else
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
 * Function nsc_fir_change_speed (self, baud)
 *
 *    Change the speed of the device
 *
 */
static void nsc_fir_change_speed(struct nsc_fir_cb *self, __u32 speed)
{
	struct net_device *dev = self->netdev;
	__u8 mcr = MCR_SIR;
	int iobase; 
	__u8 bank;

	IRDA_DEBUG(2, __FUNCTION__ "(), speed=%d\n", speed);

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
	case 38400:  outb(0x03, iobase+BGDL); break;
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
	nsc_fir_change_dongle_speed(iobase, speed, self->io.dongle_id);

	/* Set FIFO threshold to TX17, RX16 */
	switch_bank(iobase, BANK0);
	outb(0x00, iobase+FCR);
	outb(FCR_FIFO_EN, iobase+FCR);
	outb(FCR_RXTH|     /* Set Rx FIFO threshold */
	     FCR_TXTH|     /* Set Tx FIFO threshold */
	     FCR_TXSR|     /* Reset Tx FIFO */
	     FCR_RXSR|     /* Reset Rx FIFO */
	     FCR_FIFO_EN,  /* Enable FIFOs */
	     iobase+FCR);
	
	/* Set FIFO size to 32 */
	switch_bank(iobase, BANK2);
	outb(EXCR2_RFSIZ|EXCR2_TFSIZ, iobase+EXCR2);
	
	self->netdev->tbusy = 0;
	
	/* Enable some interrupts so we can receive frames */
	switch_bank(iobase, BANK0); 
	if (speed > 115200) {
		/* Install FIR xmit handler */
		dev->hard_start_xmit = nsc_fir_hard_xmit_fir;
		outb(IER_SFIF_IE, iobase+IER);
		nsc_fir_dma_receive(self);
	} else {
		/* Install SIR xmit handler */
		dev->hard_start_xmit = nsc_fir_hard_xmit_sir;
		outb(IER_RXHDL_IE, iobase+IER);
	}
    	
	/* Restore BSR */
	outb(bank, iobase+BSR);
}

/*
 * Function nsc_fir_hard_xmit (skb, dev)
 *
 *    Transmit the frame!
 *
 */
static int nsc_fir_hard_xmit_sir(struct sk_buff *skb, struct net_device *dev)
{
	struct nsc_fir_cb *self;
	unsigned long flags;
	int iobase;
	__u32 speed;
	__u8 bank;
	
	self = (struct nsc_fir_cb *) dev->priv;

	ASSERT(self != NULL, return 0;);

	iobase = self->io.iobase;

	/* Lock transmit buffer */
	if (irda_lock((void *) &dev->tbusy) == FALSE)
		return -EBUSY;
	
	/* Check if we need to change the speed */
	if ((speed = irda_get_speed(skb)) != self->io.speed)
		self->new_speed = speed;
	
	spin_lock_irqsave(&self->lock, flags);
	
	/* Save current bank */
	bank = inb(iobase+BSR);
	
	self->tx_buff.data = self->tx_buff.head;
	
	self->tx_buff.len = async_wrap_skb(skb, self->tx_buff.data, 
					   self->tx_buff.truesize);
	
	/* Add interrupt on tx low level (will fire immediately) */
	switch_bank(iobase, BANK0);
	outb(IER_TXLDL_IE, iobase+IER);
	
	/* Restore bank register */
	outb(bank, iobase+BSR);

	spin_unlock_irqrestore(&self->lock, flags);

	dev_kfree_skb(skb);

	return 0;
}

static int nsc_fir_hard_xmit_fir(struct sk_buff *skb, struct net_device *dev)
{
	struct nsc_fir_cb *self;
	unsigned long flags;
	int iobase;
	__u32 speed;
	__u8 bank;
	int mtt, diff;
	
	self = (struct nsc_fir_cb *) dev->priv;

	ASSERT(self != NULL, return 0;);

	iobase = self->io.iobase;

	/* Lock transmit buffer */
	if (irda_lock((void *) &dev->tbusy) == FALSE)
		return -EBUSY;

	/* Check if we need to change the speed */
	if ((speed = irda_get_speed(skb)) != self->io.speed)
		self->new_speed = speed;

	spin_lock_irqsave(&self->lock, flags);

	/* Save current bank */
	bank = inb(iobase+BSR);

	/* Register and copy this frame to DMA memory */
	self->tx_fifo.queue[self->tx_fifo.free].start = self->tx_fifo.tail;
	self->tx_fifo.queue[self->tx_fifo.free].len = skb->len;
	self->tx_fifo.tail += skb->len;

	memcpy(self->tx_fifo.queue[self->tx_fifo.free].start, skb->data, 
	       skb->len);
	
	self->tx_fifo.len++;
	self->tx_fifo.free++;

	/* Start transmit only if there is currently no transmit going on */
	if (self->tx_fifo.len == 1) {
		mtt = irda_get_mtt(skb);
		if (mtt) {
			/* Check how much time we have used already */
			do_gettimeofday(&self->now);
			diff = self->now.tv_usec - self->stamp.tv_usec;
			if (diff < 0) 
				diff += 1000000;
			
			/* Check if the mtt is larger than the time we have
			 * already used by all the protocol processing
			 */
			if (mtt > diff) {
				mtt -= diff;
				if (mtt > 125) {
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
					
					/* Timer will take care of the rest */
					goto out; 
				} else
					udelay(mtt);
			}
		}
		
		/* Enable DMA interrupt */
		switch_bank(iobase, BANK0);
		outb(IER_DMA_IE, iobase+IER);
		nsc_fir_dma_xmit(self, iobase);
	}
	/* Not busy transmitting anymore if window is not full */
	if (self->tx_fifo.len < MAX_WINDOW)
		dev->tbusy = 0;
 out:
	/* Restore bank register */
	outb(bank, iobase+BSR);

	spin_unlock_irqrestore(&self->lock, flags);

	dev_kfree_skb(skb);

	return 0;
}

/*
 * Function nsc_fir_dma_xmit (self, iobase)
 *
 *    Transmit data using DMA
 *
 */
static void nsc_fir_dma_xmit(struct nsc_fir_cb *self, int iobase)
{
	int bsr;

	/* Save current bank */
	bsr = inb(iobase+BSR);

	/* Disable DMA */
	switch_bank(iobase, BANK0);
	outb(inb(iobase+MCR) & ~MCR_DMA_EN, iobase+MCR);
	
	self->io.direction = IO_XMIT;
	
	/* Choose transmit DMA channel  */ 
	switch_bank(iobase, BANK2);
	outb(ECR1_DMASWP|ECR1_DMANF|ECR1_EXT_SL, iobase+ECR1);
	
	setup_dma(self->io.dma, 
		  self->tx_fifo.queue[self->tx_fifo.ptr].start, 
		  self->tx_fifo.queue[self->tx_fifo.ptr].len, 
		  DMA_TX_MODE);

	/* Enable DMA and SIR interaction pulse */
 	switch_bank(iobase, BANK0);	
	outb(inb(iobase+MCR)|MCR_TX_DFR|MCR_DMA_EN|MCR_IR_PLS, iobase+MCR);

	/* Restore bank register */
	outb(bsr, iobase+BSR);
}

/*
 * Function nsc_fir_pio_xmit (self, iobase)
 *
 *    Transmit data using PIO. Returns the number of bytes that actually
 *    got transfered
 *
 */
static int nsc_fir_pio_write(int iobase, __u8 *buf, int len, int fifo_size)
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
 * Function nsc_fir_dma_xmit_complete (self)
 *
 *    The transfer of a frame in finished. This function will only be called 
 *    by the interrupt handler
 *
 */
static int nsc_fir_dma_xmit_complete(struct nsc_fir_cb *self)
{
	int iobase;
	__u8 bank;
	int ret = TRUE;

	IRDA_DEBUG(2, __FUNCTION__ "()\n");

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
		self->stats.tx_bytes += self->tx_buff.len;
	}
	
	if (self->new_speed) {
		nsc_fir_change_speed(self, self->new_speed);
		self->new_speed = 0;
	}

	/* Finished with this frame, so prepare for next */
	self->tx_fifo.ptr++;
	self->tx_fifo.len--;

	/* Any frames to be sent back-to-back? */
	if (self->tx_fifo.len) {
		nsc_fir_dma_xmit(self, iobase);
		
		/* Not finished yet! */
		ret = FALSE;
	}

	/* Not busy transmitting anymore */
	self->netdev->tbusy = 0;

	/* Tell the network layer, that we can accept more frames */
	mark_bh(NET_BH);

	/* Restore bank */
	outb(bank, iobase+BSR);
	
	return ret;
}

/*
 * Function nsc_fir_dma_receive (self)
 *
 *    Get ready for receiving a frame. The device will initiate a DMA
 *    if it starts to receive a frame.
 *
 */
static int nsc_fir_dma_receive(struct nsc_fir_cb *self) 
{
	int iobase;
	__u8 bsr;

	ASSERT(self != NULL, return -1;);

	iobase = self->io.iobase;

	/* Reset Tx FIFO info */
	self->tx_fifo.len = self->tx_fifo.ptr = self->tx_fifo.free = 0;
	self->tx_fifo.tail = self->tx_buff.head;

	/* Save current bank */
	bsr = inb(iobase+BSR);

	/* Disable DMA */
	switch_bank(iobase, BANK0);
	outb(inb(iobase+MCR) & ~MCR_DMA_EN, iobase+MCR);

	/* Choose DMA Rx, DMA Fairness, and Advanced mode */
	switch_bank(iobase, BANK2);
	outb(ECR1_DMANF|ECR1_EXT_SL, iobase+ECR1);

	self->io.direction = IO_RECV;
	self->rx_buff.data = self->rx_buff.head;
	
	/* Reset Rx FIFO. This will also flush the ST_FIFO */
	switch_bank(iobase, BANK0);
	outb(FCR_RXSR|FCR_FIFO_EN, iobase+FCR);
	self->st_fifo.len = self->st_fifo.tail = self->st_fifo.head = 0;
	
	setup_dma(self->io.dma, self->rx_buff.data, self->rx_buff.truesize, 
		  DMA_RX_MODE);

	/* Enable DMA */
	switch_bank(iobase, BANK0);
	outb(inb(iobase+MCR)|MCR_DMA_EN, iobase+MCR);

	/* Restore bank register */
	outb(bsr, iobase+BSR);
	
	return 0;
}

/*
 * Function nsc_fir_dma_receive_complete (self)
 *
 *    Finished with receiving frames
 *
 *    
 */
static int nsc_fir_dma_receive_complete(struct nsc_fir_cb *self, int iobase)
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
			switch_bank(iobase, BANK0);
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

			/* 
			 * Remember when we received this frame, so we can
			 * reduce the min turn time a bit since we will know
			 * how much time we have used for protocol processing
			 */
			do_gettimeofday(&self->stamp);

			skb = dev_alloc_skb(len+1);
			if (skb == NULL)  {
				WARNING(__FUNCTION__ "(), memory squeeze, "
					"dropping frame.\n");
				
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
 * Function nsc_fir_pio_receive (self)
 *
 *    Receive all data in receiver FIFO
 *
 */
static void nsc_fir_pio_receive(struct nsc_fir_cb *self) 
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
 * Function nsc_fir_sir_interrupt (self, eir)
 *
 *    Handle SIR interrupt
 *
 */
static __u8 nsc_fir_sir_interrupt(struct nsc_fir_cb *self, int eir)
{
	int actual;
	__u8 new_ier = 0;

	/* Check if transmit FIFO is low on data */
	if (eir & EIR_TXLDL_EV) {
		/* Write data left in transmit buffer */
		actual = nsc_fir_pio_write(self->io.iobase, 
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
			
		        mark_bh(NET_BH);	

			new_ier |= IER_TXEMP_IE;
		}
			
	}
	/* Check if transmission has completed */
	if (eir & EIR_TXEMP_EV) {
		/* Check if we need to change the speed? */
		if (self->new_speed) {
			IRDA_DEBUG(2, __FUNCTION__ "(), Changing speed!\n");
			nsc_fir_change_speed(self, self->new_speed);
			self->new_speed = 0;
		}

		/* Turn around and get ready to receive some data */
		self->io.direction = IO_RECV;
		new_ier |= IER_RXHDL_IE;
	}

	/* Rx FIFO threshold or timeout */
	if (eir & EIR_RXHDL_EV) {
		nsc_fir_pio_receive(self);

		/* Keep receiving */
		new_ier |= IER_RXHDL_IE;
	}
	return new_ier;
}

/*
 * Function nsc_fir_fir_interrupt (self, eir)
 *
 *    Handle MIR/FIR interrupt
 *
 */
static __u8 nsc_fir_fir_interrupt(struct nsc_fir_cb *self, int iobase, int eir)
{
	__u8 new_ier = 0;
	__u8 bank;

	bank = inb(iobase+BSR);
	
	/* Status event, or end of frame detected in FIFO */
	if (eir & (EIR_SFIF_EV|EIR_LS_EV)) {
		if (nsc_fir_dma_receive_complete(self, iobase)) {

			/* Wait for next status FIFO interrupt */
			new_ier |= IER_SFIF_IE;
		} else {
			/* DMA not finished yet */

			/* Set timer value, resolution 125 us */
			switch_bank(iobase, BANK4);
			outb(0x0f, iobase+TMRL); /* 125 us * 15 */
			outb(0x00, iobase+TMRH);

			/* Start timer */
			outb(IRCR1_TMR_EN, iobase+IRCR1);

			new_ier |= IER_TMR_IE;
		}
	} else if (eir & EIR_TMR_EV) { /* Timer finished */
		/* Disable timer */
		switch_bank(iobase, BANK4);
		outb(0, iobase+IRCR1);

		/* Clear timer event */
		switch_bank(iobase, BANK0);
		outb(ASCR_CTE, iobase+ASCR);

		/* Check if this is a TX timer interrupt */
		if (self->io.direction == IO_XMIT) {
			nsc_fir_dma_xmit(self, iobase);

			/*  Interrupt on DMA */
			new_ier |= IER_DMA_IE;
		} else {
			/* Check if DMA has now finished */
			nsc_fir_dma_receive_complete(self, iobase);

			new_ier |= IER_SFIF_IE;
		}
	} else if (eir & EIR_DMA_EV) { /* Finished with transmission */
		if (nsc_fir_dma_xmit_complete(self)) {		
			/* Check if there are more frames to be transmitted */
			if (irda_device_txqueue_empty(self->netdev)) {
				/* Prepare for receive */
				nsc_fir_dma_receive(self);
			
				new_ier = IER_LS_IE|IER_SFIF_IE;
			}
		} else {
			/*  Not finished yet, so interrupt on DMA again */
			new_ier |= IER_DMA_IE;
		}
	}
	outb(bank, iobase+BSR);

	return new_ier;
}

/*
 * Function nsc_fir_interrupt (irq, dev_id, regs)
 *
 *    An interrupt from the chip has arrived. Time to do some work
 *
 */
static void nsc_fir_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct nsc_fir_cb *self;
	__u8 bsr, eir, ier;
	int iobase;

	if (!dev) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n", 
		       driver_name, irq);
		return;
	}
	self = (struct nsc_fir_cb *) dev->priv;

	spin_lock(&self->lock);	
	dev->interrupt = 1;

	iobase = self->io.iobase;

	bsr = inb(iobase+BSR); 	/* Save current bank */

	switch_bank(iobase, BANK0);	
	ier = inb(iobase+IER); 
	eir = inb(iobase+EIR) & ier; /* Mask out the interesting ones */ 

	outb(0, iobase+IER); /* Disable interrupts */
	
	if (eir) {
		/* Dispatch interrupt handler for the current speed */
		if (self->io.speed > 115200)
			ier = nsc_fir_fir_interrupt(self, iobase, eir);
		else
			ier = nsc_fir_sir_interrupt(self, eir);
	}

	outb(ier, iobase+IER);   /* Restore interrupts */
	outb(bsr, iobase+BSR);   /* Restore bank register */

	dev->interrupt = 0;
	spin_unlock(&self->lock);
}

/*
 * Function nsc_fir_is_receiving (self)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int nsc_fir_is_receiving(struct nsc_fir_cb *self)
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
 * Function nsc_fir_net_init (dev)
 *
 *    Initialize network device
 *
 */
static int nsc_fir_net_init(struct net_device *dev)
{
	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	/* Setup to be a normal IrDA network device driver */
	irda_device_setup(dev);

	/* Insert overrides below this line! */

	return 0;
}

/*
 * Function nsc_fir_net_open (dev)
 *
 *    Start the device
 *
 */
static int nsc_fir_net_open(struct net_device *dev)
{
	struct nsc_fir_cb *self;
	int iobase;
	__u8 bank;
	
	IRDA_DEBUG(4, __FUNCTION__ "()\n");
	
	ASSERT(dev != NULL, return -1;);
	self = (struct nsc_fir_cb *) dev->priv;
	
	ASSERT(self != NULL, return 0;);
	
	iobase = self->io.iobase;

	if (request_irq(self->io.irq, nsc_fir_interrupt, 0, dev->name, 
			(void *) dev)) 
	{
		return -EAGAIN;
	}
	/*
	 * Always allocate the DMA channel after the IRQ, and clean up on 
	 * failure.
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
 * Function nsc_fir_net_close (dev)
 *
 *    Stop the device
 *
 */
static int nsc_fir_net_close(struct net_device *dev)
{
	struct nsc_fir_cb *self;
	int iobase;
	__u8 bank;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");
	
	ASSERT(dev != NULL, return -1;);
	self = (struct nsc_fir_cb *) dev->priv;
	
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
 * Function nsc_fir_net_ioctl (dev, rq, cmd)
 *
 *    Process IOCTL commands for this device
 *
 */
static int nsc_fir_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct nsc_fir_cb *self;
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
		nsc_fir_change_speed(self, irq->ifr_baudrate);
		break;
	case SIOCSMEDIABUSY: /* Set media busy */
		irda_device_set_media_busy(self->netdev, TRUE);
		break;
	case SIOCGRECEIVING: /* Check if we are receiving right now */
		irq->ifr_receiving = nsc_fir_is_receiving(self);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	
	restore_flags(flags);
	
	return ret;
}

static struct net_device_stats *nsc_fir_net_get_stats(struct net_device *dev)
{
	struct nsc_fir_cb *self = (struct nsc_fir_cb *) dev->priv;
	
	return &self->stats;
}

#ifdef CONFIG_APM
static void nsc_fir_suspend(struct nsc_fir_cb *self)
{
	int i = 10;

	MESSAGE("%s, Suspending\n", driver_name);

	if (self->suspend)
		return;

	self->suspend = 1;
}


static void nsc_fir_wakeup(struct nsc_fir_cb *self)
{
	struct net_device *dev = self->netdev;
	unsigned long flags;

	if (!self->suspend)
		return;

	save_flags(flags);
	cli();

	restore_flags(flags);
	MESSAGE("%s, Waking up\n", driver_name);
}

static int nsc_fir_apmproc(apm_event_t event)
{
	static int down = 0;          /* Filter out double events */
	int i;

	switch (event) {
	case APM_SYS_SUSPEND:
	case APM_USER_SUSPEND:
		if (!down) {			
			for (i = 0; i < 4; i++) {
				if (dev_self[i])
					nsc_fir_suspend(dev_self[i]);
			}
		}
		down = 1;
		break;
	case APM_NORMAL_RESUME:
	case APM_CRITICAL_RESUME:
		if (down) {
			for (i = 0; i < 4; i++) {
				if (dev_self[i])
					nsc_fir_wakeup(dev_self[i]);
			}
		}
		down = 0;
		break;
	}
	return 0;
}
#endif /* CONFIG_APM */

#ifdef MODULE
MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("NSC FIR IrDA Device Driver");

MODULE_PARM(qos_mtt_bits, "i");
MODULE_PARM(io, "1-4i");
MODULE_PARM(io2, "1-4i");
MODULE_PARM(irq, "1-4i");
MODULE_PARM(dongle_id, "i");

int init_module(void)
{
	return nsc_fir_init();
}

void cleanup_module(void)
{
	nsc_fir_cleanup();
}
#endif /* MODULE */

