/*********************************************************************
 *                
 * Filename:      ircomm_tty.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Jun  6 23:24:22 1999
 * Modified at:   Fri Aug 13 07:31:35 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *     
 ********************************************************************/

#ifndef IRCOMM_TTY_H
#define IRCOMM_TTY_H

#include <linux/serial.h>

#include <net/irda/irias_object.h>
#include <net/irda/ircomm_core.h>
#include <net/irda/ircomm_param.h>

#define IRCOMM_TTY_PORTS 32
#define IRCOMM_TTY_MAGIC 0x3432
#define IRCOMM_TTY_MAJOR 161
#define IRCOMM_TTY_MINOR 0

/*
 * IrCOMM TTY driver state
 */
struct ircomm_tty_cb {
	QUEUE   queue;
	magic_t magic;

	int state;

	struct tty_struct *tty;
	struct ircomm_cb *ircomm;

	struct sk_buff_head tx_queue;  /* Frames to be transmitted */
	struct sk_buff *tx_skb;
	struct sk_buff *ctrl_skb;

	/* Parameters */
	struct ircomm_params session;

	__u8 service_type;

	int line;
	int flags;

	__u8 dlsap_sel;
	__u8 slsap_sel;

	__u32 saddr;
	__u32 daddr;

	__u32 max_data_size;
	__u32 max_header_size;

	struct ias_object* obj;
	int skey;
	int ckey;

	struct termios	  normal_termios;
	struct termios	  callout_termios;

	wait_queue_head_t open_wait;
	struct timer_list watchdog_timer;
	struct tq_struct  tqueue;

	long pgrp;		/* pgrp of opening process */
	int  open_count;
	int  blocked_open;	/* # of blocked opens */
};

void ircomm_tty_start(struct tty_struct *tty);
void ircomm_tty_stop(struct tty_struct *tty);
void ircomm_tty_check_modem_status(struct ircomm_tty_cb *self);

extern void ircomm_tty_change_speed(struct ircomm_tty_cb *self);
extern int ircomm_tty_ioctl(struct tty_struct *tty, struct file *file, 
			    unsigned int cmd, unsigned long arg);
extern void ircomm_tty_set_termios(struct tty_struct *tty, 
				   struct termios *old_termios);
extern hashbin_t *ircomm_tty;

#endif







