/*********************************************************************
 *
 * Filename:      irlpt.c
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Thomas Davis, <ratbert@radiks.net>
 * Created at:    Sat Feb 21 18:54:38 1998
 * Modified at:   Sun Mar  8 23:44:19 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:	  irlan.c
 *
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>,
 *			   Dag Brattli,  <dagb@cs.uit.no>
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     I, Thomas Davis, provide no warranty for any of this software.
 *     This material is provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#ifndef IRLPT_COMMON_H
#define IRLPT_COMMON_H

#include <net/irda/qos.h>
#include <net/irda/irmod.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/miscdevice.h>

#include <linux/poll.h>

extern char *irlpt_service_type[];
extern char *irlpt_port_type[];
extern char *irlpt_connected[];
extern char *irlpt_reasons[];
extern char *irlpt_client_fsm_state[];
extern char *irlpt_server_fsm_state[];
extern char *irlpt_fsm_event[];

extern struct wait_queue *lpt_wait;

extern struct irlpt_cb *irlpt_find_handle(unsigned int minor);
extern void irlpt_flow_control(struct sk_buff *skb);

extern ssize_t irlpt_read( struct file *file, char *buffer, 
			   size_t count, loff_t *noidea);
extern ssize_t irlpt_write(struct file *file, const char *buffer, 
			   size_t count, loff_t *noidea);
extern loff_t irlpt_seek(struct file *, loff_t, int);
extern int irlpt_open(struct inode * inode, struct file *file);
extern int irlpt_close(struct inode *inode, struct file *file);
extern u_int irlpt_poll(struct file *file, poll_table *wait);

/* FSM definitions */

typedef enum {
        IRLPT_CLIENT_IDLE,
	IRLPT_CLIENT_QUERY,
	IRLPT_CLIENT_READY,
	IRLPT_CLIENT_WAITI,
	IRLPT_CLIENT_WAITR,
	IRLPT_CLIENT_CONN,
} IRLPT_CLIENT_STATE;

typedef enum {
	IRLPT_SERVER_IDLE,
	IRLPT_SERVER_CONN,
} IRLPT_SERVER_STATE;
    
/* IrLPT Events */

typedef enum {
	QUERY_REMOTE_IAS,
	IAS_PROVIDER_AVAIL,
	IAS_PROVIDER_NOT_AVAIL,
	LAP_DISCONNECT,
	LMP_CONNECT,
	LMP_DISCONNECT,
	LMP_CONNECT_INDICATION,
	LMP_DISCONNECT_INDICATION,
#if 0
	TTP_CONNECT_INDICATION,
	TTP_DISCONNECT_INDICATION,
#endif
        IRLPT_DISCOVERY_INDICATION,
	IRLPT_CONNECT_REQUEST,
	IRLPT_DISCONNECT_REQUEST,
	CLIENT_DATA_INDICATION,
} IRLPT_EVENT;

struct irlpt_info {
	struct lsap_cb *lsap;
	__u8 dlsap_sel;
	__u32 daddr;
};

/* Command packet types */

#define IRLPT_MAX_PACKET 1024
#define IRLPT_MAX_HEADER LMP_MAX_HEADER
#define IRLPT_MAX_DEVICES 3
#define IRLPT_MAGIC 0x0755

typedef enum {
	IRLPT_DISCONNECTED,
	IRLPT_WAITING,
	IRLPT_CONNECTED,
	IRLPT_FLUSHED,
} IRLPT_SERVER_STATUS;

#define IRLPT_LSAP      0x09

#define PI_SERVICE_TYPE	0x00

#define IRLPT_UNKNOWN         0x00            /* not defined yet. */
#define IRLPT_THREE_WIRE_RAW  0x01		/* bit 0 */
#define IRLPT_THREE_WIRE      0x02		/* bit 1 */
#define IRLPT_NINE_WIRE	      0x04		/* bit 2 */
#define IRLPT_CENTRONICS      0x08		/* bit 3 */
#define IRLPT_SERVER_MODE     0xFF            /* our own flag */

#define PI_PORT_TYPE	0x01

#define IRLPT_SERIAL	0x01		/* bit 0 */
#define IRLPT_PARALLEL	0x02		/* bit 1 */

#define PI_PORT_NAME	0x02

#define PI_CRITICAL	0x80

struct irlpt_cb {
	QUEUE queue;		/* must be first. */

	int magic;		/* magic used to detect corruption of 
				   the struct */
	__u32 daddr;		/* my local address. */

	struct timer_list retry_timer;
	
	int volatile state;	/* Current state of IrCOMM layer */
	int open_retries;
        int in_use;		/* flag to prevent re-use */
        char ifname[16];		/* name of the allocated instance, 
				   and registered device. */
	struct lsap_cb *lsap;	/* lmp handle */

	__u8 dlsap_sel;		/* remote LSAP selector address */
	__u8 slsap_sel;		/* local LSAP selectoraddress */
	__u8 servicetype;	/* Type of remote service, ie THREE_WIRE_RAW */
	__u8 porttype;		/* type of remote port. */

	struct miscdevice ir_dev; /* used to register the misc device. */

	int count;                /* open count */
	int irlap_data_size;	/* max frame size we can send */
	int pkt_count;		/* how many packets are queued up */

	struct wait_queue *read_wait;	/* wait queues */
	struct wait_queue *write_wait;
	struct wait_queue *ex_wait;

	/* this is used by the server side of the system */

        IRLPT_SERVER_STATE connected;

	int eof;
	int service_LSAP;

	struct sk_buff_head rx_queue; /* read buffer queue */
};

/* Debug function */
void irlpt_dump_buffer(struct sk_buff *);

#endif
