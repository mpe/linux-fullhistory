/*********************************************************************
 *                
 * Filename:      irlap.h
 * Version:       0.3
 * Description:   An IrDA LAP driver for Linux
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Sat Dec 12 12:25:33 1998
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

#ifndef IRLAP_H
#define IRLAP_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ppp_defs.h>
#include <linux/ppp-comp.h>

#include <net/irda/irlap_event.h>

#define LAP_RELIABLE   1
#define LAP_UNRELIABLE 0

#define LAP_ADDR_HEADER 1  /* IrLAP Address Header */
#define LAP_CTRL_HEADER 1  /* IrLAP Control Header */
#define LAP_COMP_HEADER 1  /* IrLAP Compression Header */

#ifdef CONFIG_IRDA_COMPRESSION
#  define LAP_HEADER  (LAP_ADDR_HEADER + LAP_CTRL_HEADER + LAP_COMP_HEADER)
#  define IRDA_COMPRESSED 1
#  define IRDA_NORMAL     0
#else
#define LAP_HEADER (LAP_ADDR_HEADER + LAP_CTRL_HEADER)
#endif

#define BROADCAST  0xffffffff /* Broadcast device address */
#define CBROADCAST 0xfe       /* Connection broadcast address */
#define XID_FORMAT 0x01       /* Discovery XID format */

#define LAP_WINDOW_SIZE 8
#define MAX_CONNECTIONS 1

#define NR_EXPECTED     1
#define NR_UNEXPECTED   0
#define NR_INVALID     -1

#define NS_EXPECTED     1
#define NS_UNEXPECTED   0
#define NS_INVALID     -1

#ifdef CONFIG_IRDA_COMPRESSION
/*  
 *  Just some shortcuts (may give you strange compiler errors if you change 
 *  them :-)
 */
#define irda_compress    (*self->compessor.cp->compress)
#define irda_comp_free   (*self->compressor.cp->comp_free)
#define irda_decompress  (*self->decompressor.cp->decompress)
#define irda_decomp_free (*self->decompressor.cp->decomp_free)
#define irda_incomp      (*self->decompressor.cp->incomp)

struct irda_compressor {
	QUEUE queue;

	struct compressor *cp;
	void *state; /* Not used by IrDA */
};
#endif

/* Main structure of IrLAP */
struct irlap_cb {
	QUEUE q; /* Must be first */

	int magic;

	struct irda_device *irdev;
	struct device      *netdev;

	/* Connection state */
	volatile IRLAP_STATE state;       /* Current state */

	/* Timers used by IrLAP */
	struct timer_list query_timer;
	struct timer_list slot_timer;
	struct timer_list discovery_timer;
	struct timer_list final_timer;
	struct timer_list poll_timer;
	struct timer_list wd_timer;
	struct timer_list backoff_timer;

	/* Timeouts which will be different with different turn time */
	int poll_timeout;
	int final_timeout;
	int wd_timeout;

	struct sk_buff_head tx_list;  /* Frames to be transmitted */

 	__u8    caddr;        /* Connection address */
	__u32   saddr;        /* Source device address */
	__u32   daddr;        /* Destination device address */

	int     retry_count;  /* Times tried to establish connection */
	int     add_wait;     /* True if we are waiting for frame */

#ifdef CONFIG_IRDA_FAST_RR
	/* 
	 *  To send a faster RR if tx queue empty 
	 */
	int     fast_RR_timeout;
	int     fast_RR;      
#endif

	int N1; /* N1 * F-timer = Negitiated link disconnect warning threshold */
	int N2; /* N2 * F-timer = Negitiated link disconnect time */
	int N3; /* Connection retry count */

	int     local_busy;
	int     remote_busy;
	int     xmitflag;

	__u8    vs;           /* Next frame to be sent */
	__u8    vr;           /* Next frame to be received */
	int     tmp;
	__u8    va;           /* Last frame acked */
 	int     window;       /* Nr of I-frames allowed to send */
	int     window_size;  /* Current negotiated window size */
	int     window_bytes; /* Number of bytes allowed to send */
	int     bytes_left;   /* Number of bytes allowed to transmit */

	struct sk_buff_head wx_list;

	__u8    ack_required;
	
	/* XID parameters */
 	__u8    S;           /* Number of slots */
	__u8    slot;        /* Random chosen slot */
 	__u8    s;           /* Current slot */
	int     frame_sent;  /* Have we sent reply? */

	int discovery_count;
	hashbin_t *discovery_log;
 	DISCOVERY *discovery_cmd;

	struct qos_info qos_tx;    /* QoS requested by peer */
	struct qos_info qos_rx;    /* QoS requested by self */

	struct notify_t notify; /* Callbacks to IrLMP */

	int     mtt_required;  /* Minumum turnaround time required */
	int     xbofs_delay;   /* Nr of XBOF's used to MTT */
	int     bofs_count;    /* Negotiated extra BOFs */

 	struct irda_statistics stats;

#ifdef CONFIG_IRDA_RECYCLE_RR
	struct sk_buff *recycle_rr_skb;
#endif

#ifdef CONFIG_IRDA_COMPRESSION
	struct irda_compressor compressor;
        struct irda_compressor decompressor;
#endif
};

extern hashbin_t *irlap;

/* 
 *  Function prototypes 
 */

int irlap_init( void);
void irlap_cleanup( void);

struct irlap_cb *irlap_open( struct irda_device *dev);
void irlap_close( struct irlap_cb *self);

void irlap_connect_request( struct irlap_cb *self, __u32 daddr, 
			    struct qos_info *qos, int sniff);
void irlap_connect_response( struct irlap_cb *self, struct sk_buff *skb);
void irlap_connect_indication( struct irlap_cb *self, struct sk_buff *skb);
void irlap_connect_confirm( struct irlap_cb *, struct sk_buff *skb);

inline void irlap_data_indication( struct irlap_cb *, struct sk_buff *);
inline void irlap_unit_data_indication( struct irlap_cb *, struct sk_buff *);
inline void irlap_data_request( struct irlap_cb *, struct sk_buff *, 
				int reliable);

void irlap_disconnect_request( struct irlap_cb *);
void irlap_disconnect_indication( struct irlap_cb *, LAP_REASON reason);

void irlap_status_indication( int quality_of_link);

void irlap_test_request( __u8 *info, int len);

void irlap_discovery_request( struct irlap_cb *, DISCOVERY *discovery);
void irlap_discovery_confirm( struct irlap_cb *, hashbin_t *discovery_log);
void irlap_discovery_indication( struct irlap_cb *, DISCOVERY *discovery);

void irlap_reset_indication( struct irlap_cb *self);
void irlap_reset_confirm(void);

void irlap_update_nr_received( struct irlap_cb *, int nr);
int irlap_validate_nr_received( struct irlap_cb *, int nr);
int irlap_validate_ns_received( struct irlap_cb *, int ns);

int  irlap_generate_rand_time_slot( int S, int s);
void irlap_initiate_connection_state( struct irlap_cb *);
void irlap_flush_all_queues( struct irlap_cb *);
void irlap_change_speed( struct irlap_cb *, int);
void irlap_wait_min_turn_around( struct irlap_cb *, struct qos_info *);

void irlap_init_qos_capabilities( struct irlap_cb *, struct qos_info *);
void irlap_apply_default_connection_parameters( struct irlap_cb *self);
void irlap_apply_connection_parameters( struct irlap_cb *, struct qos_info *);

#endif
