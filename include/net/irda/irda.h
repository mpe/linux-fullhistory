/*********************************************************************
 *                
 * Filename:      irda.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Dec  9 21:13:12 1997
 * Modified at:   Mon May 10 09:51:13 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
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

#ifndef NET_IRDA_H
#define NET_IRDA_H

#include <linux/config.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>

#include <net/irda/qos.h>
#include <net/irda/irqueue.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE 
#define FALSE 0
#endif

#define ALIGN __attribute__((aligned))
#define PACK __attribute__((packed))


#ifdef CONFIG_IRDA_DEBUG

extern __u32 irda_debug;

/* use 0 for production, 1 for verification, >2 for debug */
#define IRDA_DEBUG_LEVEL 0

#define DEBUG(n, args...) if (irda_debug >= (n)) printk(KERN_DEBUG args)
#define ASSERT(expr, func) \
if(!(expr)) { \
        printk( "Assertion failed! %s,%s,%s,line=%d\n",\
        #expr,__FILE__,__FUNCTION__,__LINE__); \
        ##func}
#else
#define DEBUG(n, args...)
#define ASSERT(expr, func)
#endif /* CONFIG_IRDA_DEBUG */

#define WARNING(args...) printk(KERN_WARNING args)
#define MESSAGE(args...) printk(KERN_INFO args)
#define ERROR(args...)   printk(KERN_ERR args)

#define MSECS_TO_JIFFIES(ms) (ms*HZ/1000)

/*
 *  Magic numbers used by Linux/IR. Random numbers which must be unique to 
 *  give the best protection
 */
#define IRTTY_MAGIC        0x2357
#define LAP_MAGIC          0x1357
#define LMP_MAGIC          0x4321
#define LMP_LSAP_MAGIC     0x69333
#define LMP_LAP_MAGIC      0x3432
#define IRDA_DEVICE_MAGIC  0x63454
#define IAS_MAGIC          0x007
#define TTP_MAGIC          0x241169
#define TTP_TSAP_MAGIC     0x4345
#define IROBEX_MAGIC       0x341324
#define HB_MAGIC           0x64534
#define IRLAN_MAGIC        0x754
#define IAS_OBJECT_MAGIC   0x34234
#define IAS_ATTRIB_MAGIC   0x45232

#define IAS_DEVICE_ID 0x5342 
#define IAS_PNP_ID    0xd342
#define IAS_OBEX_ID   0x34323
#define IAS_IRLAN_ID  0x34234
#define IAS_IRCOMM_ID 0x2343
#define IAS_IRLPT_ID  0x9876

typedef enum { FLOW_STOP, FLOW_START } LOCAL_FLOW;

/* IrDA Socket */
struct tsap_cb;
struct irda_sock {
	__u32 saddr;          /* my local address */
	__u32 daddr;          /* peer address */

	struct ias_object *ias_obj;
	struct tsap_cb *tsap; /* TSAP used by this connection */
	__u8 dtsap_sel;       /* remote TSAP address */
	__u8 stsap_sel;       /* local TSAP address */
	
	__u32 max_sdu_size_rx;
	__u32 max_sdu_size_tx;
	__u32 max_data_size;
	__u8  max_header_size;
	struct qos_info qos_tx;

	__u16 mask;           /* Hint bits mask */
	__u16 hints;          /* Hint bits */

	__u32 ckey;           /* IrLMP client handle */
	__u32 skey;           /* IrLMP service handle */

	int nslots;           /* Number of slots to use for discovery */

	int errno;

	struct sock *sk;
	wait_queue_head_t ias_wait;       /* Wait for LM-IAS answer */

	LOCAL_FLOW tx_flow;
	LOCAL_FLOW rx_flow;
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

/* Misc status information */
typedef enum {
	STATUS_OK,
	STATUS_ABORTED,
	STATUS_NO_ACTIVITY,
	STATUS_NOISY,
	STATUS_REMOTE,
} LINK_STATUS;

typedef enum {
	LOCK_NO_CHANGE,
	LOCK_LOCKED,
	LOCK_UNLOCKED,
} LOCK_STATUS;

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
	LM_LSAP_NOTCONN,      /* Data delivered on unconnected LSAP */
	LM_NON_RESP_CLIENT,   /* Non responsive LM-MUX client */
	LM_NO_AVAIL_CLIENT,   /* No available LM-MUX client */
	LM_CONN_HALF_OPEN,    /* Connection is half open */
	LM_BAD_SOURCE_ADDR,   /* Illegal source address (i.e 0x00) */
} LM_REASON;
#define LM_UNKNOWN 0xff       /* Unspecified disconnect reason */

/*
 *  Notify structure used between transport and link management layers
 */
struct notify_t {
	int (*data_indication)(void *priv, void *sap, struct sk_buff *skb);
	int (*udata_indication)(void *priv, void *sap, struct sk_buff *skb);
	void (*connect_confirm)(void *instance, void *sap, 
				struct qos_info *qos, __u32 max_sdu_size,
				__u8 max_header_size, struct sk_buff *skb);
	void (*connect_indication)(void *instance, void *sap, 
				   struct qos_info *qos, __u32 max_sdu_size,
				   __u8 max_header_size, struct sk_buff *skb);
	void (*disconnect_indication)(void *instance, void *sap, 
				      LM_REASON reason, struct sk_buff *);
	void (*flow_indication)(void *instance, void *sap, LOCAL_FLOW flow);
	void *instance; /* Layer instance pointer */
	char name[16];  /* Name of layer */
};

#define NOTIFY_MAX_NAME 16

#endif /* NET_IRDA_H */
