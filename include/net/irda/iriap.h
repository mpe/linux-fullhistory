/*********************************************************************
 *                
 * Filename:      iriap.h
 * Version:       
 * Description:   Information Access Protocol (IAP)
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Aug 21 00:02:07 1997
 * Modified at:   Sat Dec  5 13:45:37 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
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

#ifndef IRIAP_H
#define IRIAP_H

#include <linux/types.h>
#include <linux/skbuff.h>

#include <net/irda/qos.h>
#include <net/irda/iriap_event.h>
#include <net/irda/irias_object.h>
#include <net/irda/irqueue.h>
#include <net/irda/timer.h>

#define LST 0x80
#define ACK 0x40

#define IAS_SERVER 0
#define IAS_CLIENT 1

/* IrIAP Op-codes */
#define GET_INFO_BASE      0x01
#define GET_OBJECTS        0x02
#define GET_VALUE          0x03
#define GET_VALUE_BY_CLASS 0x04
#define GET_OBJECT_INFO    0x05
#define GET_ATTRIB_NAMES   0x06

#define IAS_SUCCESS        0
#define IAS_CLASS_UNKNOWN  1
#define IAS_ATTRIB_UNKNOWN 2

typedef void (*CONFIRM_CALLBACK)( __u16 obj_id, struct ias_value *value,
				  void *priv);

struct iap_value {
	char *full;
        char *name;
        char *attr;
        __u16   obj_id;
        __u8    ret_code;
        __u8    type;
        int     len;
        int value_int;
        char *value_char;
};

struct iriap_cb {
	QUEUE queue; /* Must be first */
	
	int          magic;  /* Magic cookie */
	int          mode;   /* Client or server */
	__u32        daddr;
	__u8         operation;

	struct sk_buff *skb;
	struct lsap_cb *lsap;
	__u8 slsap_sel;

	/* Client states */
	IRIAP_STATE client_state;
	IRIAP_STATE call_state;
	
	/* Server states */
	IRIAP_STATE server_state;
	IRIAP_STATE r_connect_state;
	
	CONFIRM_CALLBACK confirm;
	void *priv;

	struct timer_list watchdog_timer;
};

int  iriap_init(void);
void iriap_cleanup(void);
void iriap_getvaluebyclass_request( __u32 addr, char *name, char *attr, 
				    CONFIRM_CALLBACK callback, void *priv);
void iriap_getvaluebyclass_confirm( struct iriap_cb *self, 
				    struct sk_buff *skb);

void iriap_send_ack( struct iriap_cb *self);
void iriap_data_indication( void *instance, void *sap, struct sk_buff *skb);
void iriap_connect_confirm( void *instance, void *sap, struct qos_info *qos, 
			    int max_sdu_size, struct sk_buff *skb);
void iriap_connect_indication( void *instance, void *sap, 
			       struct qos_info *qos, int max_sdu_size,
			       struct sk_buff *skb);
void iriap_call_indication( struct iriap_cb *self, struct sk_buff *skb);

void iriap_register_server(void);

void iriap_watchdog_timer_expired( unsigned long data);

static inline void iriap_start_watchdog_timer( struct iriap_cb *self, 
					       int timeout) 
{
	irda_start_timer( &self->watchdog_timer, timeout, 
			  (unsigned long) self, iriap_watchdog_timer_expired);
}

#endif
