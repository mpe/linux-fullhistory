/*********************************************************************
 *                
 * Filename:      irkbd.h
 * Version:       0.2
 * Description:   IrDA Keyboard/Mouse driver (Tekram IR-660)
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Mar  1 00:24:19 1999
 * Modified at:   Thu Mar 11 14:54:00 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
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

#ifndef IRKBD_H
#define IRKBD_H

/* Some commands */
#define IRKBD_CMD_INIT_KBD   0xfe
#define IRKBD_CMD_INIT_MOUSE 0xff
#define IRKBD_CMD_ENABLE     0x41
#define IRKBD_CMD_LED        0x31
#define IRKBD_CMD_KDB_SPEED  0x33

/* Some responses */
#define IRKBD_RSP_KBDOK      0x11
#define IRKBD_RSP_KBDERR     0x12
#define IRKBD_RSP_MSOK       0x21
#define IRKBD_RSP_MSERR      0x22
#define IRKBD_RSP_LEDOK      0x31
#define IRKBD_RSP_KBDSPEEDOK 0x33
#define IRKBD_RSP_RSPN41     0x41

#define IRKBD_RATE       2 /* Polling rate, should be 15 ms */
#define IRKBD_TIMEOUT  100 /* 1000 ms */

#define SUBFRAME_MASK     0xc0
#define SUBFRAME_MOUSE    0x80
#define SUBFRAME_KEYBOARD 0x40
#define SUBFRAME_RESPONSE 0x00

#define IRKBD_MAX_HEADER (TTP_HEADER+LMP_HEADER+LAP_HEADER)

#define IRKBD_BUF_SIZE 4096 /* Must be power of 2! */

enum {
	IRKBD_IDLE,       /* Not connected */
	IRKBD_INIT_KBD,   /* Initializing keyboard */
	IRKBD_INIT_MOUSE, /* Initializing mouse */
	IRKBD_POLLING,    /* Polling device */
};

/* Main structure */
struct irkbd_cb {
	struct miscdevice dev;
	char devname[9];    /* name of the registered device */
	int state;

	int count;          /* Open count */

	__u32 saddr;        /* my local address */
	__u32 daddr;        /* peer address */

	struct tsap_cb *tsap;		
	__u8 dtsap_sel;     /* remote TSAP address */
	__u8 stsap_sel;     /* local TSAP address */

	struct timer_list watchdog_timer;

	LOCAL_FLOW tx_flow;
	LOCAL_FLOW rx_flow;

	__u8 scancodes[IRKBD_BUF_SIZE]; /* Buffer for mouse events */
	int head;
	int tail;

	struct wait_queue *read_wait;
	struct fasync_struct *async;
};

#endif /* IRKBD_H */
