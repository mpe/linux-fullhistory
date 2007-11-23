/*********************************************************************
 *                
 * Filename:      irport.h
 * Version:       0.1
 * Description:   Serial driver for IrDA
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug  3 13:49:59 1997
 * Modified at:   Sat Oct 30 19:54:07 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997, 1998-1999 Dag Brattli <dagb@cs.uit.no>
 *     All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Troms� admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#ifndef IRPORT_H
#define IRPORT_H

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/spinlock.h>

#include <net/irda/irda_device.h>

#define SPEED_DEFAULT 9600
#define SPEED_MAX     115200

/*
 * These are the supported serial types.
 */
#define PORT_UNKNOWN    0
#define PORT_8250       1
#define PORT_16450      2
#define PORT_16550      3
#define PORT_16550A     4
#define PORT_CIRRUS     5
#define PORT_16650      6
#define PORT_MAX        6  

#define FRAME_MAX_SIZE 2048

struct irport_cb {
	struct net_device *netdev; /* Yes! we are some kind of netdevice */
	struct net_device_stats stats;

	struct irlap_cb    *irlap; /* The link layer we are binded to */

	struct chipio_t io;        /* IrDA controller information */
	struct iobuff_t tx_buff;   /* Transmit buffer */
	struct iobuff_t rx_buff;   /* Receive buffer */

	struct qos_info qos;       /* QoS capabilities for this device */
	dongle_t *dongle;          /* Dongle driver */

 	__u32 flags;               /* Interface flags */

	spinlock_t lock;           /* For serializing operations */

	int mode;
};

void irport_start(struct irport_cb *self, int iobase);
void irport_stop(struct irport_cb *self, int iobase);
int  irport_probe(int iobase);

void __irport_change_speed(struct irport_cb *self, __u32 speed);
void irport_interrupt(int irq, void *dev_id, struct pt_regs *regs);
int  irport_hard_xmit(struct sk_buff *skb, struct net_device *dev);

#endif
