/*********************************************************************
 *                
 * Filename:      irvtd.h
 * Version:       0.1
 * Sources:       irlpt.h
 * 
 *     Copyright (c) 1998, Takahide Higuchi <thiguchi@pluto.dti.ne.jp>,
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

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/tqueue.h>

#include <net/irda/irmod.h>
#include <net/irda/qos.h>
#include <net/irda/ircomm_common.h>


#define IRVTD_MAGIC 0xff545943  /* random */
#define COMM_MAX_TTY 1
#define IRVTD_RX_QUEUE_HIGH 10
#define IRVTD_RX_QUEUE_LOW  2


/*
 * Serial input interrupt line counters -- external structure
 * Four lines can interrupt: CTS, DSR, RI, DCD
 *
 * this structure must be compatible with serial_icounter_struct defined in
 * <linux/serial.h>.
 */
struct icounter_struct {
        int cts, dsr, rng, dcd;
        int reserved[16];
};

struct irvtd_cb {

        int magic;          /* magic used to detect corruption of the struct */

	/* if daddr is NULL, remote device have not been discovered yet */

	int rx_disable;
	struct sk_buff *txbuff;     /* buffer queue */
	struct sk_buff_head rxbuff;     /* buffer queue */
	struct ircomm_cb *comm;     /* ircomm instance */

	/* 
	 * These members are used for compatibility with usual serial device.
	 * See linux/serial.h
	 */

	int baud_base;
	int flags;
	struct tty_struct *tty;

	int line;
	int count;                /* open count */
	int blocked_open;
	struct wait_queue       *open_wait;
	struct wait_queue       *close_wait;
	struct wait_queue       *delta_msr_wait;
	struct wait_queue       *tx_wait;

	struct tq_struct        rx_tqueue;

	long pgrp;
	long session;  
	struct termios normal_termios;
	struct termios callout_termios;
	unsigned short  closing_wait;     /* time to wait before closing */
	unsigned short  close_delay;
	
	int mcr;
	int msr;
	int cts_stoptx;
	int ttp_stoptx;
	int ttp_stoprx;
	struct icounter_struct icount;
	int read_status_mask;
	int ignore_status_mask;
};


/* Debug function */

/* #define CHECK_SKB(skb) check_skb((skb),  __LINE__,__FILE__) */



#endif
