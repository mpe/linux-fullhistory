/*********************************************************************
 *                
 * Filename:      irttp.h
 * Version:       0.3
 * Description:   Tiny Transport Protocol (TTP) definitions
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:31 1997
 * Modified at:   Sat Dec  5 13:48:12 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
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

#ifndef IRTTP_H
#define IRTTP_H

#include <linux/types.h>
#include <linux/skbuff.h>

#include <net/irda/irmod.h>
#include <net/irda/irlmp.h>
#include <net/irda/qos.h>
#include <net/irda/irqueue.h>

#define TTP_MAX_CONNECTIONS   LM_MAX_CONNECTIONS
#define TTP_HEADER            1
#define TTP_HEADER_WITH_SAR   6
#define TTP_PARAMETERS        0x80
#define TTP_MORE              0x80

#define DEFAULT_INITIAL_CREDIT 22

#define LOW_THRESHOLD  4
#define HIGH_THRESHOLD 8
#define TTP_MAX_QUEUE 22

/* Some priorities for disconnect requests */
#define P_NORMAL 0
#define P_HIGH 1

#define SAR_DISABLE 0

/*
 *  This structure contains all data assosiated with one instance of a TTP 
 *  connection.
 */
struct tsap_cb {
	QUEUE queue;         /* For linking it into the hashbin */
	int  magic;          /* Just in case */

	int max_seg_size;    /* Max data that fit into an IrLAP frame */

	__u8 stsap_sel;      /* Source TSAP */
	__u8 dtsap_sel;      /* Destination TSAP */

	struct lsap_cb *lsap; /* Corresponding LSAP to this TSAP */

	__u8 connected;       /* TSAP connected */
	 
	__u8 initial_credit;  /* Initial credit to give peer */

        int avail_credit;    /* Available credit to return to peer */
	int remote_credit;   /* Credit held by peer TTP entity */
	int send_credit;     /* Credit held by local TTP entity */
	
	struct sk_buff_head tx_queue; /* Frames to be transmitted */
	struct sk_buff_head rx_queue; /* Received frames */
	struct sk_buff_head rx_fragments;
	int tx_queue_lock;
	int rx_queue_lock;

	struct notify_t notify;       /* Callbacks to client layer */

	struct irda_statistics stats;
	struct timer_list todo_timer; 
	
	int rx_sdu_busy;   /* RxSdu.busy */
	int rx_sdu_size;   /* The current size of a partially received frame */
	int rx_max_sdu_size; /* Max receive user data size */

	int tx_sdu_busy;     /* TxSdu.busy */
	int tx_max_sdu_size; /* Max transmit user data size */

        int no_defrag;       /* Don't reassemble received fragments */

	int disconnect_pend;   /* Disconnect, but still data to send */
	struct sk_buff *disconnect_skb;
};

struct irttp_cb {
	int magic;
	
	hashbin_t *tsaps;
};

int  irttp_init(void);
void irttp_cleanup(void);

struct tsap_cb *irttp_open_tsap( __u8 stsap, int credit, 
				 struct notify_t *notify);
void irttp_close_tsap( struct tsap_cb *self);

int  irttp_data_request( struct tsap_cb *self, struct sk_buff *skb);
int  irttp_udata_request( struct tsap_cb *self, struct sk_buff *skb);

void irttp_connect_request( struct tsap_cb *self, __u8 dtsap_sel, __u32 daddr,
			    struct qos_info *qos, int max_sdu_size, 
			    struct sk_buff *userdata);
void irttp_connect_confirm( void *instance, void *sap, struct qos_info *qos, 
			    int max_sdu_size, struct sk_buff *skb);
void irttp_connect_response( struct tsap_cb *self, int max_sdu_size, 
			     struct sk_buff *userdata);

void irttp_disconnect_request( struct tsap_cb *self, struct sk_buff *skb,
			       int priority);
void irttp_flow_request( struct tsap_cb *self, LOCAL_FLOW flow);

static __inline__ void irttp_no_reassemble( struct tsap_cb *self)
{
	self->no_defrag = TRUE;
}

extern struct irttp_cb *irttp;

#endif /* IRTTP_H */
