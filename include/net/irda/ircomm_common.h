/*********************************************************************
 *                
 * Filename:      ircomm_common.h
 * Version:       
 * Description:   An implementation of IrCOMM service interface and state machine 
 * Status:        Experimental.
 * Author:        Takahide Higuchi <thiguchi@pluto.dti.ne.jp>
 *
 *     Copyright (c) 1998, Takahide Higuchi, <thiguchi@pluto.dti.ne.jp>,
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     I, Takahide Higuchi, provide no warranty for any of this software.
 *     This material is provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#ifndef IRCOMM_H
#define IRCOMM_H

/* #define DEBUG(n, args...) printk( KERN_DEBUG args) */ /* enable all debug message */

#include <linux/types.h>
#include <net/irda/irmod.h> 

typedef enum {
        COMM_IDLE,

	COMM_DISCOVERY_WAIT,
	COMM_QUERYPARAM_WAIT,
	COMM_QUERYLSAP_WAIT,

	COMM_WAITI,
	COMM_WAITR,
	COMM_CONN,
} IRCOMM_STATE;

/* IrCOMM Events */
typedef enum {
	IRCOMM_CONNECT_REQUEST,
	TTP_CONNECT_INDICATION,
	LMP_CONNECT_INDICATION,

	TTP_CONNECT_CONFIRM,
	TTP_DISCONNECT_INDICATION,
	LMP_CONNECT_CONFIRM,
	LMP_DISCONNECT_INDICATION,

	IRCOMM_CONNECT_RESPONSE,
	IRCOMM_DISCONNECT_REQUEST,

	TTP_DATA_INDICATION,
	IRCOMM_DATA_REQUEST,
	LMP_DATA_INDICATION,
	IRCOMM_CONTROL_REQUEST,
	
	DISCOVERY_INDICATION,
	GOT_PARAMETERS,
	GOT_LSAPSEL,
	QUERYIAS_ERROR,

} IRCOMM_EVENT;

typedef enum {
	TX_READY,
	TX_BUSY,

	IAS_PARAM,
	CONTROL_CHANNEL,
} IRCOMM_CMD;



#define IRCOMM_MAGIC            0x434f4d4d
#define COMM_INIT_CTRL_PARAM    3          /* length of initial control parameters */
#define COMM_HEADER             1          /* length of clen field */
#define COMM_HEADER_SIZE        (LAP_HEADER+LMP_HEADER+TTP_HEADER+COMM_HEADER)
#define COMM_DEFAULT_DATA_SIZE  64
#define IRCOMM_MAX_CONNECTION   1          /* Don't change for now */




#define UNKNOWN         0x00            /* not defined yet. */
/* we use 9wire if servicetype=DEFAULT, but is it good? */
#define DEFAULT         0x0a            /* private number */
#define THREE_WIRE_RAW	0x01		/* bit 0 */
#define THREE_WIRE	0x02		/* bit 1 */
#define NINE_WIRE	0x04		/* bit 2 */
#define CENTRONICS	0x08		/* bit 3 */

#define SERIAL		0x01		/* bit 0 */
#define PARALLEL	0x02		/* bit 1 */


#define SERVICETYPE 0x00
#define PORT_TYPE 0x01
#define PORT_NAME 0x02
#define FIXED_PORT_NAME 0x82

#define DATA_RATE 0x10
#define DATA_FORMAT 0x11
#define FLOW_CONTROL 0x12
#define XON_XOFF_CHAR 0x13
#define ENQ_ACK_CHAR 0x14
#define LINESTATUS 0x15
#define BREAK_SIGNAL 0x16

#define DTELINE_STATE 0x20
#define DCELINE_STATE 0x21
#define POLL_FOR_LINE_SETTINGS 0x22

#define STATUS_QUERY 0x30
#define SET_BUSY_TIMEOUT 0x31
#define IEEE1284_MODE_SUPPORT 0x32
#define IEEE1284_DEVICEID 0x33
#define IEEE1284_MODE 0x34
#define IEEE1284_ECP_EPP_DATA_TRANSFER 0x35

/*  parameters of FLOW_CONTROL  */

#define USE_RTS 0x08  /* use RTS on output */
#define USE_CTS 0x04  /* use CTS on input */
#define USE_DTR 0x20  /* use DTR on output */

/*  parameters of DTELINE_STATE  */

#define DELTA_DTR 0x01
#define DELTA_RTS 0x02
#define MCR_DTR 0x04
#define MCR_RTS 0x08

/*  parameters of DCELINE_STATE  */

#define DELTA_CTS 0x01
#define DELTA_DSR 0x02
#define DELTA_RI  0x04 
#define DELTA_DCD 0x08
#define MSR_CTS   0x10
#define MSR_DSR   0x20
#define MSR_RI    0x40 
#define MSR_DCD   0x80

/*  parameters of DATA_FORMAT */

#define IRCOMM_WLEN5   0x00       /* word length is 5bit */
#define IRCOMM_WLEN6   0x01       /* word length is 6bit */
#define IRCOMM_WLEN7   0x02       /* word length is 7bit */
#define IRCOMM_WLEN8   0x03       /* word length is 8bit */

#define IRCOMM_STOP2   0x04       /* 2 stop bits mode */
#define IRCOMM_PARENB  0x08       /* parity enable */
#define IRCOMM_PARODD  0x00       /*  odd parity */
#define IRCOMM_PAREVEN 0x10       /*  even parity */
#define IRCOMM_PARMARK 0x20
#define IRCOMM_PARSPC  0x30

/*  parameters of LINE_STATUS */

#define LSR_OE     0x02    /* Overrun error indicator */
#define LSR_PE     0x04    /* Parity error indicator */
#define LSR_FE     0x08    /* Frame error indicator */
#define LSR_BI     0x01    /* Break interrupt indicator */


struct ircomm_cb{
	int magic;
	int state;          /* Current state of IrCOMM layer: 
			     *  DISCOVERY,COMM_IDLE, COMM_WAITR,
			     *  COMM_WAITI, COMM_CONN
			     */
	int in_use;
	int null_modem_mode;     /* switch for null modem emulation */
	int ttp_stop;

	int max_txbuff_size;          
	__u32 maxsdusize;

 	__u32 daddr;        /* Device address of the peer device */ 
	__u32 saddr;
	__u32 skey;
	__u32 ckey;
	int                 queryias_lock;
	int                 ias_type;
	int disconnect_priority; /* P_NORMAL or P_HIGH. see irttp.h */
	struct notify_t notify;     /* container of callbacks */
	void (*d_handler)(struct ircomm_cb *self);

	int control_ch_pending;
	struct sk_buff *ctrl_skb;   /* queue of control channel */

	__u8 dlsap;          /* IrLMP dlsap */
	__u8 lsap;           /* sap of local device */ 
	struct tsap_cb *tsap;          /* IrTTP/LMP handle */
	struct qos_info *qos;         /* Quality of Service */
	int reason;          /* I don't know about reason: 
				see Irlmp.c or somewhere :p)*/ 
	int peer_cap;        /* capability of peer device */

	wait_queue_head_t   discovery_wait;
	wait_queue_head_t   ias_wait;

	/* statistics */
	int                 tx_packets;
	int                 rx_packets;
	int                 tx_controls;
	int                 pending_control_tuples;
	int                 ignored_control_tuples;



	__u8 pi ;            /* instruction of control channel*/ 

	__u8 port_type;
	__u8 peer_port_type;

	__u8 servicetype;    
	__u8 peer_servicetype;    
	__u8 data_format;   
	__u8 peer_data_format;   
	__u8 flow_ctrl;
	__u8 peer_flow_ctrl;
	__u8 line_status;
	__u8 peer_line_status;
	__u8 break_signal;
	__u8 peer_break_signal;
	__u8 dte;
	__u8 peer_dte;
	__u8 dce;
	__u8 peer_dce;
	__u8 xon_char;
	__u8 xoff_char;
	__u8 peer_xon_char;
	__u8 peer_xoff_char;
	__u8 enq_char;
	__u8 ack_char;
	__u8 peer_enq_char;
	__u8 peer_ack_char;
	__u8 busy_timeout;
	__u8 peer_busy_timeout;
	__u8 ecp_epp_mode;
	__u8 peer_ecp_epp_mode;
	__u8 channel_or_addr;
	__u8 peer_channel_or_addr;
	
	__u32 data_rate;
	__u32 peer_data_rate;
	char port_name[33];
	int port_name_critical;
};



void ircomm_connect_request(struct ircomm_cb *self, __u8 servicetype);
void ircomm_connect_response(struct ircomm_cb *self, struct sk_buff *userdata,
			     __u32 maxsdusize);
void ircomm_disconnect_request(struct ircomm_cb *self,
			       struct sk_buff *userdata, int priority);
int ircomm_data_request(struct ircomm_cb *self,
			struct sk_buff *userdata);
void ircomm_control_request(struct ircomm_cb *self, __u8 instruction);

void ircomm_parse_tuples(struct ircomm_cb *self, struct sk_buff *skb, int type);

struct ircomm_cb *ircomm_open_instance(struct notify_t notify);
int ircomm_close_instance(struct ircomm_cb *self);


#endif
