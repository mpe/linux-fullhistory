/*********************************************************************
 *                
 * Filename:      irlap_frame.h
 * Version:       0.9
 * Description:   Build and transmit IrLAP frames
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Aug 19 10:27:26 1997
 * Modified at:   Fri Apr 23 09:33:55 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
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

#ifndef IRLAP_FRAME_H
#define IRLAP_FRAME_H

#include <linux/skbuff.h>

#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/irda/qos.h>

/* Frame types and templates */
#define INVALID   0xff

/* Unnumbered (U) commands */
#define SNRM_CMD  0x83 /* Set Normal Response Mode */
#define DISC_CMD  0x43 /* Disconnect */
#define XID_CMD   0x2f /* Exchange Station Identification */
#define TEST_CMD  0xe3 /* Test */

/* Unnumbered responses */
#define RNRM_RSP  0x83 /* Request Normal Response Mode */
#define UA_RSP    0x63 /* Unnumbered Acknowledgement */
#define FRMR_RSP  0x87 /* Frame Reject */
#define DM_RSP    0x0f /* Disconnect Mode */
#define RD_RSP    0x43 /* Request Disconnection */
#define XID_RSP   0xaf /* Exchange Station Identification */
#define TEST_RSP  0xe3 /* Test frame */

/* Supervisory (S) */
#define RR        0x01 /* Receive Ready */
#define REJ       0x09 /* Reject */
#define RNR       0x05 /* Receive Not Ready */
#define SREJ      0x0d /* Selective Reject */

/* Information (I) */
#define I_FRAME   0x00 /* Information Format */
#define UI_FRAME  0x03 /* Unnumbered Information */

#define CMD_FRAME 0x01
#define RSP_FRAME 0x00

#define PF_BIT    0x10 /* Poll/final bit */

struct xid_frame {
	__u8  caddr; /* Connection address */
	__u8  control;
	__u8  ident; /* Should always be XID_FORMAT */ 
	__u32 saddr; /* Source device address */
	__u32 daddr; /* Destination device address */
	__u8  flags; /* Discovery flags */
	__u8  slotnr;
	__u8  version;
	__u8  discovery_info[0];
} PACK;

struct test_frame {
	__u8 caddr;          /* Connection address */
	__u8 control;
	__u32 saddr;         /* Source device address */
	__u32 daddr;         /* Destination device address */
	__u8 info[0];        /* Information */
} PACK;

struct ua_frame {
	__u8 caddr;
	__u8 control;

	__u32 saddr; /* Source device address */
	__u32 daddr; /* Dest device address */
	__u8  params[0];
} PACK;
	
struct i_frame {
	__u8 caddr;
	__u8 control;
	__u8 data[0];
} PACK;

struct snrm_frame {
	__u8  caddr;
	__u8  control;
	__u32 saddr;
	__u32 daddr;
	__u8  ncaddr;
	__u8  params[0];
} PACK;

/* Per-packet information we need to hide inside sk_buff */
struct irlap_skb_cb {
	int magic; /* Be sure that we can trust the information */
	int mtt;   /* minimum turn around time */
	int xbofs; /* number of xbofs required */
	int vs;    /* next frame to send */
	int vr;    /* next frame to receive */
};

void irlap_send_discovery_xid_frame( struct irlap_cb *, int S, __u8 s, 
				     __u8 command, discovery_t *discovery);
void irlap_send_snrm_frame( struct irlap_cb *, struct qos_info *);
void irlap_send_test_frame(struct irlap_cb *self, __u32 daddr, 
			   struct sk_buff *cmd);
void irlap_send_ua_response_frame( struct irlap_cb *, struct qos_info *);
void irlap_send_ui_frame( struct irlap_cb *self, struct sk_buff *skb,
			  int command);
void irlap_send_dm_frame( struct irlap_cb *);
void irlap_send_disc_frame( struct irlap_cb *);
void irlap_send_rr_frame( struct irlap_cb *, int command);

void irlap_send_data_primary( struct irlap_cb *, struct sk_buff *);
void irlap_send_data_primary_poll( struct irlap_cb *, struct sk_buff *);
void irlap_send_data_secondary( struct irlap_cb *, struct sk_buff *);
void irlap_send_data_secondary_final( struct irlap_cb *, struct sk_buff *);
void irlap_resend_rejected_frames( struct irlap_cb *, int command);

void irlap_send_i_frame( struct irlap_cb *, struct sk_buff *, int command);
void irlap_send_ui_frame( struct irlap_cb *, struct sk_buff *, int command);

#endif
