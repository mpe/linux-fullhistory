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

/* #define DEBUG(n, args...) printk( KERN_DEBUG args) */ /* enable all debug message */

#include <linux/types.h>
#include <net/irda/irmod.h> 

typedef enum {
	COMM_DISCOVERY,
        COMM_IDLE,
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
} IRCOMM_EVENT;


#define IRCOMM_MAGIC            0x434f4d4d
#define COMM_INIT_CTRL_PARAM    3          /* length of initial control parameters */
#define COMM_CTRL_MIN           1          /* length of clen field */
#define COMM_HEADER_SIZE        (LAP_HEADER+LMP_HEADER+TTP_HEADER+COMM_CTRL_MIN)
#define COMM_DEFAULT_DATA_SIZE  64
#define IRCOMM_MAX_CONNECTION   1          /* Don't change */

#define IAS_PARAM               1
#define CONTROL_CHANNEL         2



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
#define PORT_TYPE 0x02
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

#define TX_READY 0xFE  /* FIXME: this is not defined in IrCOMM spec */
#define TX_BUSY  0XFF  /*         so we should find another way */


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
	int maxsdusize;
 	__u32 daddr;        /* Device address of the peer device */ 

	void (*d_handler)(struct ircomm_cb *self);
	struct notify_t notify;     /* container of callbacks */

	struct sk_buff *ctrl_skb;   /* queue of control channel */

	struct tsap_cb *tsap;          /* IrTTP/LMP handle */
	struct qos_info *qos;         /* Quality of Service */
	
	int reason;          /* I don't know about reason: 
				see Irlmp.c or somewhere :p)*/ 

	__u8 dlsap;          /* IrLMP dlsap */
	__u8 lsap;           /* sap of local device */ 

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
	char port_name[60];

};



void ircomm_connect_request(struct ircomm_cb *self, int maxsdusize);
void ircomm_connect_response(struct ircomm_cb *self, struct sk_buff *userdata,
			     int maxsdusize);
void ircomm_disconnect_request(struct ircomm_cb *self,
				      struct sk_buff *userdata);
void ircomm_data_request(struct ircomm_cb *self,
			 struct sk_buff *userdata);
void ircomm_control_request(struct ircomm_cb *self);
void ircomm_append_ctrl(struct ircomm_cb *self, __u8 instruction);
struct ircomm_cb *ircomm_attach_cable( __u8 servicetype, struct notify_t notify, 
			 void *handler);
int ircomm_detach_cable(struct ircomm_cb *self);


void ircomm_accept_data_indication(void *instance, void *sap, struct sk_buff *skb);
void ircomm_accept_connect_confirm(void *instance, void *sap, struct qos_info *qos, 
				   int maxsdusize, struct sk_buff *skb);
void ircomm_accept_connect_indication(void *instance, void *sap,
				      struct qos_info *qos, 
				      int maxsdusize, struct sk_buff *skb);
void ircomm_accept_disconnect_indication(void *instance, void *sap, LM_REASON reason,
					 struct sk_buff *skb);
void ircomm_accept_flow_indication(void *instance, void *sap, LOCAL_FLOW flow);
void ircomm_next_state( struct ircomm_cb *self, IRCOMM_STATE state);


