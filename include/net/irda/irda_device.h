/*********************************************************************
 *                
 * Filename:      irda_device.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Haris Zukanovic <haris@stud.cs.uit.no>
 * Created at:    Tue Apr 14 12:41:42 1998
 * Modified at:   Mon Jan 18 10:52:10 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Haris Zukanovic, <haris@stud.cs.uit.no>
 *     Copyright (c) 1998 Dag Brattli, <dagb@cs.uit.no>
 *     Copyright (c) 1998 Thomas Davis, <ratbert@radiks.net>,
 *     All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Haris Zukanovic nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#ifndef IRDA_DEVICE_H
#define IRDA_DEVICE_H

#include <linux/tty.h>
#include <linux/netdevice.h>

#include <net/irda/irda.h>
#include <net/irda/qos.h>
#include <net/irda/irqueue.h>

#define SIR_MODE 	0x01
#define MIR_MODE 	0x02
#define FIR_MODE 	0x04
#define IO_PIO   	0x08
#define IO_DMA		0x10

#define IO_XMIT 0x01
#define IO_RECV 0x02

/* Chip specific info */
struct chipio_t {
        int iobase, iobase2;  /* IO base */
        int io_ext, io_ext2;  /* Length of iobase */
        int irq, irq2;        /* Interrupts used */
        int fifo_size;        /* FIFO size */

        int dma, dma2;        /* DMA channel used */
        int irqflags;         /* interrupt flags (ie, SA_SHIRQ|SA_INTERRUPT) */
	int direction;        /* Used by some FIR drivers */

	int baudrate;         /* Currently used baudrate */
	int dongle_id;        /* Dongle or transceiver currently used */
};

/* Buffer specific info */
struct iobuff_t {
	int state;            /* Receiving state (transmit state not used) */
	int in_frame;         /* True if receiving frame */

	__u8 *data;	      /* the buffer */
	__u8 *head;	      /* start of data in buffer */
	__u8 *tail;           /* end of data in buffer */

	int offset;	      /* Usually data + offset = head */
	int len;	      /* currently used bytes in buffer */
	int truesize;	      /* total size of the data area */
	__u16 fcs;

	int flags;            /* Allocation flags (GFP_KERNEL | GFP_DMA ) */
};

/* 
 * This structure contains data that _we_ would have liked to be in the device
 * structure, but we don't want to mess it up more than it is already. Better 
 * to keep the data in separate structures! This structure abstracts common 
 * stuff from IrDA port implementations.
 */
struct irda_device {
	QUEUE q; /* Must be first */

        int  magic;	       /* Our magic bullet */
	char name[16];         /* Name of device "irda0" */
	char description[32];  /* Something like "irda0 <-> ttyS0" */

	struct irlap_cb *irlap; /* The link layer we are connected to  */
	struct device netdev;   /* Yes! we are some kind of netdevice */
	struct enet_statistics stats;

/* 	int flags; */

 	void *priv;           /* Pointer to low level implementation */

	struct qos_info qos;  /* QoS capabilities for this device */

	struct chipio_t io;
	struct iobuff_t tx_buff;
	struct iobuff_t rx_buff;

	int media_busy;
	
	/* Media busy stuff */
	struct timer_list media_busy_timer;
	struct timer_list todo_timer;

	int  (*hard_xmit)( struct sk_buff *skb, struct device *dev);
        void (*change_speed)( struct irda_device *driver, int baud);

 	int (*is_receiving)(struct irda_device *);     /* receiving? */
	/* int (*is_tbusy)(struct irda_device *); */   /* transmitting? */
	void (*wait_until_sent)(struct irda_device *);

	int new_speed; /* Will be removed in future */
};

extern hashbin_t *irda_device;

/* Function prototypes */
int  irda_device_init( void);
void irda_device_cleanup( void);

int  irda_device_open( struct irda_device *, char *name, void *priv);
void irda_device_close( struct irda_device *);

/* Interface to be uses by IrLAP */
inline void irda_device_set_media_busy( struct irda_device *, int status);
inline int  irda_device_is_media_busy( struct irda_device *);
inline int  irda_device_is_receiving( struct irda_device *);
inline void irda_device_change_speed( struct irda_device *, int);

inline struct qos_info *irda_device_get_qos( struct irda_device *self);
int irda_device_txqueue_empty( struct irda_device *self);

int irda_device_setup( struct device *dev);

__inline__ int irda_get_mtt( struct sk_buff *skb);

void setup_dma( int channel, char *buffer, int count, int mode);

#endif







