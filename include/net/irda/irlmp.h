/*********************************************************************
 *                
 * Filename:      irlmp.h
 * Version:       0.3
 * Description:   IrDA Link Management Protocol (LMP) layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 17 20:54:32 1997
 * Modified at:   Mon Dec  7 21:11:32 1998
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

#ifndef IRLMP_H
#define IRLMP_H

#include <linux/config.h>
#include <linux/types.h>

#include "irmod.h"
#include "qos.h"
#include "irlap.h"
#include "irlmp_event.h"
#include "irqueue.h"

/* LSAP-SEL's */
#define LSAP_MASK     0x7f
#define LSAP_IAS      0x00
#define LSAP_ANY      0xff

/* Predefined LSAPs used by the various servers */
#define TSAP_IRLAN    0x05
#define LSAP_IRLPT    0x06
#define TSAP_IROBEX   0x07
#define TSAP_IRCOMM   0x08

#define LMP_HEADER          2    /* Dest LSAP + Source LSAP */
#define LMP_CONTROL_HEADER  4
#define LMP_MAX_HEADER      (LAP_HEADER+LMP_HEADER)

#define LM_MAX_CONNECTIONS  10

/* Hint bit positions for first hint byte */
#define HINT_PNP         0x01
#define HINT_PDA         0x02
#define HINT_COMPUTER    0x04
#define HINT_PRINTER     0x08
#define HINT_MODEM       0x10
#define HINT_FAX         0x20
#define HINT_LAN         0x40
#define HINT_EXTENSION   0x80

/* Hint bit positions for second hint byte (first extension byte) */
#define HINT_TELEPHONY   0x01
#define HINT_FILE_SERVER 0x02
#define HINT_COMM        0x04
#define HINT_MESSAGE     0x08
#define HINT_HTTP        0x10
#define HINT_OBEX        0x20

typedef enum {
	S_PNP,
	S_PDA,
	S_COMPUTER,
	S_PRINTER,
	S_MODEM,
	S_FAX,
	S_LAN,
	S_TELEPHONY,
	S_COMM,
	S_OBEX,
} SERVICE;

#define S_END 0xff

#define CLIENT 1
#define SERVER 2

typedef void (*DISCOVERY_CALLBACK) ( DISCOVERY*);

struct irlmp_registration {
	QUEUE queue; /* Must be first */

	int service; /* LAN, OBEX, COMM etc. */
	int type;    /* Client or server or both */

	DISCOVERY_CALLBACK discovery_callback;
};

struct lap_cb; /* Forward decl. */

/*
 *  Information about each logical LSAP connection
 */
struct lsap_cb {
	QUEUE queue;      /* Must be first */

	int   magic;

	int   connected;
	int   persistent;

	struct irda_statistics stats;

	__u8  slsap_sel;   /* Source (this) LSAP address */
	__u8  dlsap_sel;   /* Destination LSAP address (if connected) */

	struct sk_buff *tmp_skb; /* Store skb here while connecting */

	struct timer_list watchdog_timer;

	IRLMP_STATE     lsap_state;  /* Connection state */
	struct notify_t notify;      /* Indication/Confirm entry points */
	struct qos_info qos;         /* QoS for this connection */

	struct lap_cb *lap; /* Pointer to LAP connection structure */
};

/*
 *  Information about each registred IrLAP layer
 */
struct lap_cb {
	QUEUE queue;      /* Must be first */

	int magic;
	int reason;    /* LAP disconnect reason */

	IRLMP_STATE lap_state;

	struct irlap_cb *irlap; /* Instance of IrLAP layer */

	hashbin_t *lsaps;         /* LSAP associated with this link */

	__u8  caddr;            /* Connection address */

 	__u32 saddr;  /* Source device address */
 	__u32 daddr;  /* Destination device address */
	
	hashbin_t *cachelog;    /* Discovered devices for this link */

	struct qos_info *qos;  /* LAP QoS for this session */
};

/*
 *  Used for caching the last slsap->dlsap->handle mapping
 */
typedef struct {
	int valid;

	__u8 slsap_sel;
	__u8 dlsap_sel;
	struct lsap_cb *lsap;
} CACHE_ENTRY;

/*
 *  Main structure for IrLMP
 */
struct irlmp_cb {
	int magic;

	__u8 conflict_flag;
	
	/* int discovery; */

	DISCOVERY  discovery_rsp; /* Discovery response to use by IrLAP */
	DISCOVERY  discovery_cmd; /* Discovery command to use by IrLAP */

	int free_lsap_sel;

#ifdef CONFIG_IRDA_CACHE_LAST_LSAP
	CACHE_ENTRY cache;  /* Caching last slsap->dlsap->handle mapping */
#endif
	struct timer_list discovery_timer;

 	hashbin_t *links;         /* IrLAP connection table */
	hashbin_t *unconnected_lsaps;
 	hashbin_t *registry;

	__u8 hint[2]; /* Hint bits */
};

/* Prototype declarations */
int  irlmp_init(void);
void irlmp_cleanup(void);

struct lsap_cb *irlmp_open_lsap( __u8 slsap, struct notify_t *notify);
void irlmp_close_lsap( struct lsap_cb *self);

void irlmp_register_layer( int service, int type, int do_discovery, 
			      DISCOVERY_CALLBACK);
void irlmp_unregister_layer( int service, int type);

void irlmp_register_irlap( struct irlap_cb *self, __u32 saddr, 
			   struct notify_t *);
void irlmp_unregister_irlap( __u32 saddr);

void irlmp_connect_request( struct lsap_cb *, __u8 dlsap_sel, __u32 daddr, 
			    struct qos_info *, struct sk_buff *);
void irlmp_connect_indication( struct lsap_cb *self, struct sk_buff *skb);
void irlmp_connect_response( struct lsap_cb *, struct sk_buff *);
void irlmp_connect_confirm( struct lsap_cb *, struct sk_buff *);


void irlmp_disconnect_indication( struct lsap_cb *self, LM_REASON reason, 
				  struct sk_buff *userdata);
void irlmp_disconnect_request( struct lsap_cb *, struct sk_buff *userdata);

void irlmp_discovery_confirm( struct lap_cb *, hashbin_t *discovery_log);
void irlmp_discovery_indication( struct lap_cb *, DISCOVERY *discovery);
void irlmp_discovery_request( int nslots);
DISCOVERY *irlmp_get_discovery_response(void);

void irlmp_data_request( struct lsap_cb *, struct sk_buff *);
void irlmp_udata_request( struct lsap_cb *, struct sk_buff *);
void irlmp_data_indication( struct lsap_cb *, struct sk_buff *);
void irlmp_udata_indication( struct lsap_cb *, struct sk_buff *);

void irlmp_status_request(void);
void irlmp_status_indication( LINK_STATUS link, LOCK_STATUS lock);

int  irlmp_slsap_inuse( __u8 slsap);
__u8 irlmp_find_free_slsap(void);

LM_REASON irlmp_convert_lap_reason( LAP_REASON);

extern struct irlmp_cb *irlmp;

#endif
