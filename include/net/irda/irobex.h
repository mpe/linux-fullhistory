/*********************************************************************
 *                
 * Filename:      irobex.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Jul  4 22:43:57 1998
 * Modified at:   Wed Jan 13 15:55:28 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli, All Rights Reserved.
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

#ifndef IROBEX_H
#define IROBEX_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
/* #include <linux/netdevice.h> */
#include <linux/miscdevice.h>

#include <net/irda/timer.h>
#include <net/irda/qos.h>
#include <net/irda/irmod.h>

#define LOW_THRESHOLD      4
#define HIGH_THRESHOLD     8
#define IROBEX_MAX_QUEUE  12

/* Small structure to be used by the IOCTL call */
struct irobex_ioc_t {
     __u32 daddr;
};

#define IROBEX_IOC_MAGIC 'k'

#define IROBEX_IOCSCONNECT    _IOW(IROBEX_IOC_MAGIC, 1, 4)
#define IROBEX_IOCSDISCONNECT _IOW(IROBEX_IOC_MAGIC, 2, 4)
#define IROBEX_IOC_MAXNR 2

#define IROBEX_MAX_HEADER (TTP_HEADER+LMP_HEADER+LAP_HEADER)

typedef enum {
	OBEX_IDLE,       /* Doing nothing */
	OBEX_DISCOVER,   /* Trying to discovery remote device */
	OBEX_QUERY,      /* Querying remote LM-IAS */
	OBEX_CONN,       /* Trying to connect to remote device */
	OBEX_DATA,       /* Data transfer ready */
} OBEX_STATE;

struct irobex_cb {
	QUEUE queue;        /* Must be first! */

        int magic;          /* magic used to detect corruption of the struct */

	OBEX_STATE state;   /* Current state */

	__u32 saddr;        /* my local address */
	__u32 daddr;        /* peer address */
	unsigned long time_discovered;

        char devname[9];    /* name of the registered device */
	struct tsap_cb *tsap;
	int eof;

	__u8 dtsap_sel;         /* remote TSAP address */
	__u8 stsap_sel;         /* local TSAP address */

	int irlap_data_size;

	struct miscdevice dev;

	int count;                /* open count */

	struct sk_buff_head rx_queue; /* Receive queue */

	struct wait_queue *read_wait;
	struct wait_queue *write_wait;

	struct fasync_struct *async;

	struct timer_list watchdog_timer;

	LOCAL_FLOW tx_flow;
	LOCAL_FLOW rx_flow;
};

int    irobex_init(void);
void   irobex_cleanup(void);
struct irobex_cb *irobex_open(void);
void   irobex_close( struct irobex_cb *self);

void irobex_discovery_indication( DISCOVERY *);

void irobex_data_request( int handle, struct sk_buff *skb);
void irobex_data_indication( void *instance, void *sap, struct sk_buff *skb);
void irobex_control_data_indication( void *instance, void *sap, 
				     struct sk_buff *skb);

void irobex_connect_request( struct irobex_cb *self);
void irobex_connect(struct irobex_cb *self, struct sk_buff *skb);
void irobex_connect_confirm( void *instance, void *sap, struct qos_info *qos,
			     int max_sdu_size, struct sk_buff *skb);
void irobex_disconnect_indication( void *instance, void *sap, LM_REASON reason,
				   struct sk_buff *skb);
void irobex_flow_indication( void *instance, void *sap, LOCAL_FLOW flow);

void irobex_extract_params( struct sk_buff *skb);
void irobex_get_value_confirm(__u16 obj_id, struct ias_value *value, 
			      void *priv);
void irobex_register_server( struct irobex_cb *self);

void irobex_watchdog_timer_expired( unsigned long data);

inline void irobex_start_watchdog_timer( struct irobex_cb *self, int timeout) 
{
	irda_start_timer( &self->watchdog_timer, timeout, (unsigned long) self,
			  irobex_watchdog_timer_expired);
}

extern struct irobex_cb *irobex;

#endif
