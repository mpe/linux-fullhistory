/*********************************************************************
 *                
 * Filename:      irmod.h
 * Version:       0.3
 * Description:   IrDA module and utilities functions
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Dec 15 13:58:52 1997
 * Modified at:   Tue Jan 12 14:56:11 1999
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
 *     provided "AS-IS" and at no charg.
 *     
 ********************************************************************/

#ifndef IRMOD_H
#define IRMOD_H

#include <linux/skbuff.h>
#include <linux/miscdevice.h>

#include <net/irda/irqueue.h>
#include <net/irda/qos.h>

#define IRMGR_IOC_MAGIC 'm'
#define IRMGR_IOCTNPC     _IO(IRMGR_IOC_MAGIC, 1)
#define IRMGR_IOC_MAXNR   1 

/*
 *  Events that we pass to the user space manager
 */
typedef enum {
	EVENT_DEVICE_DISCOVERED = 0,
	EVENT_REQUEST_MODULE,
	EVENT_IRLAN_START,
	EVENT_IRLAN_STOP,
	EVENT_IRLPT_START,
	EVENT_IRLPT_STOP,
	EVENT_IROBEX_START,
	EVENT_IROBEX_STOP,
	EVENT_IRDA_STOP,
	EVENT_NEED_PROCESS_CONTEXT,
} IRMGR_EVENT;

/*
 *  Event information passed to the IrManager daemon process
 */
struct irmanager_event {
	IRMGR_EVENT event;
	char devname[10];
	char info[32];
	int service;
	__u32 saddr;
	__u32 daddr;
};

typedef void (*TODO_CALLBACK)( void *self, __u32 param);

/*
 *  Same as irmanager_event but this one can be queued and inclueds some
 *  addtional information
 */
struct irda_event {
	QUEUE q; /* Must be first */
	
	struct irmanager_event event;
};

/*
 *  Funtions with needs to be called with a process context
 */
struct irda_todo {
	QUEUE q; /* Must be first */

	void *self;
	TODO_CALLBACK callback;
	__u32 param;
};

/*
 *  Main structure for the IrDA device (not much here :-)
 */
struct irda_cb {
	struct miscdevice dev;	
	struct wait_queue *wait_queue;

	int in_use;

	QUEUE *event_queue; /* Events queued for the irmanager */
	QUEUE *todo_queue;  /* Todo list */
};

typedef struct {
        char irda_call[7];  /* 6 call + SSID (shifted ascii!) */
} irda_address;

struct sockaddr_irda {
	short sirda_family;
	irda_address sirda_call;
        int sirda_ndigis;
};

/*
 *  This type is used by the protocols that transmit 16 bits words in 
 *  little endian format. A little endian machine stores MSB of word in
 *  byte[1] and LSB in byte[0]. A big endian machine stores MSB in byte[0] 
 *  and LSB in byte[1].
 */
typedef union {
	__u16 word;
	__u8  byte[2];
} __u16_host_order;

/*
 *  Information monitored by some layers
 */
struct irda_statistics
{
        int     rx_packets;             /* total packets received       */
        int     tx_packets;             /* total packets transmitted    */
        int     rx_errors;              /* bad packets received         */
        int     tx_errors;              /* packet transmit problems     */
        int     rx_dropped;             /* no space in linux buffers    */
        int     tx_dropped;             /* no space available in linux  */
	int     rx_compressed;
	int     tx_compressed;
	int     rx_bytes;               /* total bytes received         */
	int     tx_bytes;               /* total bytes transmitted      */

        int     multicast;              /* multicast packets received   */
        int     collisions;
	
        /* detailed rx_errors: */
        int     rx_length_errors;
        int     rx_over_errors;         /* receiver ring buff overflow  */
        int     rx_crc_errors;          /* recved pkt with crc error    */
        int     rx_frame_errors;        /* recv'd frame alignment error */
        int     rx_fifo_errors;         /* recv'r fifo overrun          */
        int     rx_missed_errors;       /* receiver missed packet       */

        /* detailed tx_errors */
        int     tx_aborted_errors;
        int     tx_carrier_errors;
        int     tx_fifo_errors;
        int     tx_heartbeat_errors;
        int     tx_window_errors;
};

typedef enum {
	NO_CHANGE,
	LOCKED,
	UNLOCKED,
} LOCK_STATUS;

/* Misc status information */
typedef enum {
	STATUS_OK,
	STATUS_ABORTED,
	STATUS_NO_ACTIVITY,
	STATUS_NOISY,
	STATUS_REMOTE,
} LINK_STATUS;

typedef enum { /* FIXME check the two first reason codes */
	LAP_DISC_INDICATION=1, /* Received a disconnect request from peer */
	LAP_NO_RESPONSE,       /* To many retransmits without response */
	LAP_RESET_INDICATION,  /* To many retransmits, or invalid nr/ns */
	LAP_FOUND_NONE,        /* No devices were discovered */
	LAP_MEDIA_BUSY,
	LAP_PRIMARY_CONFLICT,
} LAP_REASON;

/*  
 *  IrLMP disconnect reasons. The order is very important, since they 
 *  correspond to disconnect reasons sent in IrLMP disconnect frames, so
 *  please do not touch :-)
 */
typedef enum {
	LM_USER_REQUEST = 1,  /* User request */
	LM_LAP_DISCONNECT,    /* Unexpected IrLAP disconnect */
	LM_CONNECT_FAILURE,   /* Failed to establish IrLAP connection */
	LM_LAP_RESET,         /* IrLAP reset */
	LM_INIT_DISCONNECT,   /* Link Management initiated disconnect */
} LM_REASON; /* FIXME: Just for now */

/*
 *  IrLMP character code values
 */
#define CS_ASCII       0x00
#define	CS_ISO_8859_1  0x01
#define	CS_ISO_8859_2  0x02
#define	CS_ISO_8859_3  0x03
#define	CS_ISO_8859_4  0x04
#define	CS_ISO_8859_5  0x05
#define	CS_ISO_8859_6  0x06
#define	CS_ISO_8859_7  0x07
#define	CS_ISO_8859_8  0x08
#define	CS_ISO_8859_9  0x09
#define CS_UNICODE     0xff
	
/*
 * The DISCOVERY structure is used for both discovery requests and responses
 */
#define DISCOVERY struct discovery_t
struct discovery_t {
	QUEUE queue;              /* Must be first! */

	__u32       saddr;        /* Which link the device was discovered */
	__u32       daddr;        /* Remote device address */
	LAP_REASON  condition;    /* More info about the discovery */

	__u8        hint[2];      /* Discovery hint bits */
	__u8        charset;
	char        info[32];     /* Usually the name of the device */
	__u8        info_len;     /* Length of device info field */

	int         gen_addr_bit; /* Need to generate a new device address? */
};

typedef enum { FLOW_STOP, FLOW_START } LOCAL_FLOW;

/*
 *  Notify structure used between transport and link management layers
 */
struct notify_t {
	void (*data_indication)( void *instance, void *sap, 
				 struct sk_buff *skb);
	void (*udata_indication)( void *instance, void *sap, 
				  struct sk_buff *skb);
	void (*connect_confirm)( void *instance, void *sap, 
				 struct qos_info *qos, int max_sdu_size,
				 struct sk_buff *skb);
	void (*connect_indication)( void *instance, void *sap, 
				    struct qos_info *qos, int max_sdu_size,
				    struct sk_buff *skb);
	void (*disconnect_indication)( void *instance, void *sap, 
				       LM_REASON reason, struct sk_buff *);
	void (*flow_indication)( void *instance, void *sap, LOCAL_FLOW flow);
	void *instance; /* Layer instance pointer */
	char name[16];  /* Name of layer */
};

#define NOTIFY_MAX_NAME 16

int irmod_init_module(void);
void irmod_cleanup_module(void);

inline int irda_lock( int *lock);
inline int irda_unlock( int *lock);

void irda_notify_init( struct notify_t *notify);

void irda_execute_as_process( void *self, TODO_CALLBACK callback, __u32 param);
void irmanager_notify( struct irmanager_event *event);

#endif
