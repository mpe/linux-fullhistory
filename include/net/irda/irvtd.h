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

#ifndef IRVTD_H
#define IRVTD_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/tqueue.h>
#include <linux/serial.h>

#include <net/irda/irmod.h>
#include <net/irda/qos.h>
#include <net/irda/ircomm_common.h>


#define IRVTD_MAGIC 0xff545943  /* random */
#define COMM_MAX_TTY 1
#define IRVTD_RX_QUEUE_HIGH 10
#define IRVTD_RX_QUEUE_LOW  2

#define IRCOMM_MAJOR  60;  /* Zero means automatic allocation
                              60,61,62,and 63 is reserved for experiment */
#define IRVTD_MINOR 64



struct irvtd_cb {

        int magic;          /* magic used to detect corruption of the struct */

	/* if daddr is NULL, remote device have not been discovered yet */

	int tx_disable;
	int rx_disable;
	struct sk_buff *txbuff;     
	struct sk_buff_head rxbuff; 
	struct ircomm_cb *comm;     /* ircomm instance */

	/* 
	 * These members are used for compatibility with usual serial device.
	 * See linux/serial.h
	 */

	int flags;
	struct tty_struct *tty;

	int line;
	int count;                /* open count */
	int blocked_open;
	struct wait_queue       *open_wait;
	struct wait_queue       *close_wait;
	struct wait_queue       *delta_msr_wait;
	struct wait_queue       *tx_wait;

	struct timer_list       timer;

	long pgrp;
	long session;  
	unsigned short  closing_wait;     /* time to wait before closing */
	unsigned short  close_delay;
	
	int custom_divisor;
	int mcr;
	int msr;
	int cts_stoptx;
	int ttp_stoptx;
	int ttp_stoprx;
	int disconnect_pend;
	struct serial_icounter_struct icount;
	int read_status_mask;
	int ignore_status_mask;
};


#endif
