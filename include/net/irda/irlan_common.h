/*********************************************************************
 *                
 * Filename:      irlan.h
 * Version:       0.1
 * Description:   IrDA LAN access layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Thu Oct 29 13:23:11 1998
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

#ifndef IRLAN_H
#define IRLAN_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include "irqueue.h"
#include "irttp.h"

#define IRLAN_MTU 1518

/* Command packet types */
#define CMD_GET_PROVIDER_INFO   0
#define CMD_GET_MEDIA_CHAR      1
#define CMD_OPEN_DATA_CHANNEL   2
#define CMD_CLOSE_DATA_CHAN     3
#define CMD_RECONNECT_DATA_CHAN 4
#define CMD_FILTER_OPERATION    5

/* Some responses */
#define RSP_SUCCESS                 0
#define RSP_INSUFFICIENT_RESOURCES  1
#define RSP_INVALID_COMMAND_FORMAT  2
#define RSP_COMMAND_NOT_SUPPORTED   3
#define RSP_PARAM_NOT_SUPPORTED     4
#define RSP_VALUE_NOT_SUPPORTED     5
#define RSP_NOT_OPEN                6
#define RSP_AUTHENTICATION_REQUIRED 7
#define RSP_INVALID_PASSWORD        8
#define RSP_PROTOCOL_ERROR          9
#define RSP_ASYNCHRONOUS_ERROR    255

/* Media types */
#define MEDIA_802_3 1
#define MEDIA_802_5 2

/* Filter parameters */
#define DATA_CHAN 1
#define FILTER_TYPE 2
#define FILTER_MODE 3

/* Filter types */
#define IR_DIRECTED   1
#define IR_FUNCTIONAL 2
#define IR_GROUP      3
#define IR_MAC_FRAME  4
#define IR_MULTICAST  5
#define IR_BROADCAST  6
#define IR_IPX_SOCKET 7

/* Filter modes */
#define ALL    1
#define FILTER 2
#define NONE   3

/* Filter operations */
#define GET     1
#define CLEAR   2
#define ADD     3
#define REMOVE  4
#define DYNAMIC 5

/* Access types */
#define DIRECT 1
#define PEER   2
#define HOSTED 3

#define IRLAN_MAX_HEADER (TTP_HEADER+LMP_HEADER+LAP_HEADER)

/*
 *  IrLAN client subclass
 */
struct irlan_client_cb {
	/*
	 *  Client fields
	 */
	int open_retries;
	
	__u8 reconnect_key[255];
	__u8 key_len;
	
	int unicast_open;
	int broadcast_open;
};

/*
 * IrLAN servers subclass
 */
struct irlan_server_cb {
	
	/*
	 *  Store some values here which are used by the irlan_server to parse
	 *  FILTER_OPERATIONs
	 */
	int data_chan;
	int filter_type;
	int filter_mode;
	int filter_operation;
	int filter_entry;

	__u8 mac_address[6]; /* Generated MAC address for peer device */
};

/*
 *  IrLAN super class
 */
struct irlan_cb {
	QUEUE queue; /* Must be first */

	int    magic;
	char   ifname[9];
	struct device dev;  /* Ethernet device structure*/
	struct enet_statistics stats;

	__u32 saddr;        /* Source devcie address */
	__u32 daddr;        /* Destination device address */
	int   connected;    /* TTP layer ready to exchange ether frames */
	
	int state;  /* Current state of IrLAN layer */

	int media;	
	
	struct tsap_cb *tsap_ctrl;
	struct tsap_cb *tsap_data;

	int  use_udata;  /* Use Unit Data transfers */

	__u8 dtsap_sel_data; /* Destination data TSAP selector */
	__u8 stsap_sel_data; /* Source data TSAP selector */
	__u8 dtsap_sel_ctrl; /* Destination ctrl TSAP selector */

	int client; /* Client or server */
	union {
		struct irlan_client_cb client;
		struct irlan_server_cb server;
	} t;

	/* void (*irlan_dev_init)(struct irlan_cb *); */

	/* 
         *  Used by extract_params, placed here for now to avoid placing 
         *  them on the stack. FIXME: remove these!
         */
        char name[255];
        char value[1016];
};

struct irlan_cb *irlan_open(void);

void irlan_get_provider_info( struct irlan_cb *self);
void irlan_get_unicast_addr( struct irlan_cb *self);
void irlan_get_media_char( struct irlan_cb *self);
void irlan_open_data_channel( struct irlan_cb *self);
void irlan_set_multicast_filter( struct irlan_cb *self, int status);
void irlan_set_broadcast_filter( struct irlan_cb *self, int status);
void irlan_open_unicast_addr( struct irlan_cb *self);

int insert_byte_param( struct sk_buff *skb, char *param, __u8 value);
int insert_string_param( struct sk_buff *skb, char *param, char *value);
int insert_array_param( struct sk_buff *skb, char *name, __u8 *value, 
			__u16 value_len);

int insert_param( struct sk_buff *skb, char *param, int type, char *value_char,
		  __u8 value_byte, __u16 value_short);

int irlan_get_response_param( __u8 *buf, char *name, char *value, int *len);
void print_ret_code( __u8 code);

extern hashbin_t *irlan;

#endif


