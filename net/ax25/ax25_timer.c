/*
 *	AX.25 release 031
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 1.2.1 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	AX.25 028a	Jonathan(G4KLX)	New state machine based on SDL diagrams.
 *	AX.25 028b	Jonathan(G4KLX)	Extracted AX25 control block from the
 *					sock structure.
 *	AX.25 029	Alan(GW4PTS)	Switched to KA9Q constant names.
 *	AX.25 031	Joerg(DL1BKE)	Added DAMA support
 *	AX.25 032	Joerg(DL1BKE)	Fixed DAMA timeout bug
 */

#include <linux/config.h>
#ifdef CONFIG_AX25
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#ifdef CONFIG_NETROM
#include <net/netrom.h>
#endif

static void ax25_timer(unsigned long);

/*
 *	Linux set/reset timer routines
 */
void ax25_set_timer(ax25_cb *ax25)
{
	unsigned long flags;	

	save_flags(flags);
	cli();
	del_timer(&ax25->timer);
	restore_flags(flags);

	ax25->timer.next     = ax25->timer.prev = NULL;	
	ax25->timer.data     = (unsigned long)ax25;
	ax25->timer.function = &ax25_timer;

	ax25->timer.expires = jiffies + 10;
	add_timer(&ax25->timer);
}

static void ax25_reset_timer(ax25_cb *ax25)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
	del_timer(&ax25->timer);
	restore_flags(flags);

	ax25->timer.data     = (unsigned long)ax25;
	ax25->timer.function = &ax25_timer;
	ax25->timer.expires  = jiffies + 10;
	add_timer(&ax25->timer);
}

/*
 *	AX.25 TIMER 
 *
 *	This routine is called every 500ms. Decrement timer by this
 *	amount - if expired then process the event.
 */
static void ax25_timer(unsigned long param)
{
	ax25_cb *ax25 = (ax25_cb *)param;

	switch (ax25->state) {
		case AX25_STATE_0:
			/* Magic here: If we listen() and a new link dies before it
			   is accepted() it isn't 'dead' so doesn't get removed. */
			if (ax25->sk == NULL || ax25->sk->destroy || (ax25->sk->state == TCP_LISTEN && ax25->sk->dead)) {
				del_timer(&ax25->timer);
				ax25_destroy_socket(ax25);
				return;
			}
			break;

		case AX25_STATE_3:
		case AX25_STATE_4:
			/*
			 * Check the state of the receive buffer.
			 */
			if (ax25->sk != NULL) {
				if (ax25->sk->rmem_alloc < (ax25->sk->rcvbuf / 2) && (ax25->condition & OWN_RX_BUSY_CONDITION)) {
					ax25->condition &= ~OWN_RX_BUSY_CONDITION;
					if (!ax25->dama_slave) /* dl1bke */
						ax25_send_control(ax25, RR, POLLOFF, C_RESPONSE);
					ax25->condition &= ~ACK_PENDING_CONDITION;
					break;
				}
			}
			/*
			 * Check for frames to transmit.
			 */
			if (!ax25->dama_slave)
				ax25_kick(ax25);	/* dl1bke 960114 */
			break;

		default:
			break;
	}

	if (ax25->t2timer > 0 && --ax25->t2timer == 0) {
		if (ax25->state == AX25_STATE_3 || ax25->state == AX25_STATE_4) {
			if (ax25->condition & ACK_PENDING_CONDITION) {
				ax25->condition &= ~ACK_PENDING_CONDITION;
				if (!ax25->dama_slave)			/* dl1bke 960114 */
					ax25_timeout_response(ax25);
			}
		}
	}

	if (ax25->t3timer > 0 && --ax25->t3timer == 0) {
		/* dl1bke 960114: T3 expires and we are in DAMA mode:  */
		/*                send a DISC and abort the connection */
		if (ax25->dama_slave) {
#ifdef CONFIG_NETROM
			nr_link_failed(&ax25->dest_addr, ax25->device);
#endif
			ax25_clear_queues(ax25);
			ax25_send_control(ax25, DISC, POLLON, C_COMMAND);
				
			ax25->state = AX25_STATE_0;
			if (ax25->sk != NULL) {
				if (ax25->sk->debug)
					printk("T3 Timeout\n");
				ax25->sk->state = TCP_CLOSE;
				ax25->sk->err   = ETIMEDOUT;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead  = 1;
			}

			ax25_reset_timer(ax25);
			return;
		}
		
		if (ax25->state == AX25_STATE_3) {
			ax25->n2count = 0;
			ax25_transmit_enquiry(ax25);
			ax25->state   = AX25_STATE_4;
		}
		ax25->t3timer = ax25->t3;
	}
	
	if (ax25->idletimer > 0 && --ax25->idletimer == 0) {
		/* dl1bke 960228: close the connection when IDLE expires */
		/* 		  similar to DAMA T3 timeout but with    */
		/* 		  a "clean" disconnect of the connection */

		ax25_clear_queues(ax25);

		ax25->n2count = 0;
		if (!ax25->dama_slave) {
			ax25->t3timer = 0;
			ax25_send_control(ax25, DISC, POLLON, C_COMMAND);
		} else {
			ax25->t3timer = ax25->t3;
		}
		
		/* state 1 or 2 should not happen, but... */
		
		if (ax25->state == AX25_STATE_1 || ax25->state == AX25_STATE_2)
			ax25->state = AX25_STATE_0;
		else
			ax25->state = AX25_STATE_2;

		ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);

		if (ax25->sk != NULL) {
			ax25->sk->state = TCP_CLOSE;
			ax25->sk->err = 0;
			if (!ax25->sk->dead)
				ax25->sk->state_change(ax25->sk);
			ax25->sk->dead = 1;
			ax25->sk->destroy = 1;
		}
	}
		                                                                                                                                                                                                                                                                                                                                        
	/* dl1bke 960114: DAMA T1 timeouts are handled in ax25_dama_slave_transmit */
	/* 		  nevertheless we have to re-enqueue the timer struct...   */
	
	if (ax25->t1timer == 0 || --ax25->t1timer > 0) {
		ax25_reset_timer(ax25);
		return;
	}

	if (!ax25_dev_is_dama_slave(ax25->device)) {
		if (ax25->dama_slave)
			ax25->dama_slave = 0;
		ax25_t1_timeout(ax25);
	}
}


/* dl1bke 960114: The DAMA protocol requires to send data and SABM/DISC
 *                within the poll of any connected channel. Remember 
 *                that we are not allowed to send anything unless we
 *                get polled by the Master.
 *                
 *                Thus we'll have to do parts of our T1 handling in
 *                ax25_enquiry_response().
 */
void ax25_t1_timeout(ax25_cb * ax25)
{	
	switch (ax25->state) {
		case AX25_STATE_1: 
			if (ax25->n2count == ax25->n2) {
				if (ax25->modulus == MODULUS) {
#ifdef CONFIG_NETROM
					nr_link_failed(&ax25->dest_addr, ax25->device);
#endif
					ax25_clear_queues(ax25);
					ax25->state = AX25_STATE_0;
					if (ax25->sk != NULL) {
						ax25->sk->state = TCP_CLOSE;
						ax25->sk->err   = ETIMEDOUT;
						if (!ax25->sk->dead)
							ax25->sk->state_change(ax25->sk);
						ax25->sk->dead  = 1;
					}
				} else {
					ax25->modulus = MODULUS;
					ax25->window  = ax25_dev_get_value(ax25->device, AX25_VALUES_WINDOW);
					ax25->n2count = 0;
					ax25_send_control(ax25, SABM, ax25_dev_is_dama_slave(ax25->device)? POLLOFF : POLLON, C_COMMAND);
				}
			} else {
				ax25->n2count++;
				if (ax25->modulus == MODULUS) {
					ax25_send_control(ax25, SABM, ax25_dev_is_dama_slave(ax25->device)? POLLOFF : POLLON, C_COMMAND);
				} else {
					ax25_send_control(ax25, SABME, ax25_dev_is_dama_slave(ax25->device)? POLLOFF : POLLON, C_COMMAND);
				}
			}
			break;

		case AX25_STATE_2:
			if (ax25->n2count == ax25->n2) {
#ifdef CONFIG_NETROM
				nr_link_failed(&ax25->dest_addr, ax25->device);
#endif
				ax25_clear_queues(ax25);
				ax25->state = AX25_STATE_0;
				ax25_send_control(ax25, DISC, POLLON, C_COMMAND); /* dl1bke */
				
				if (ax25->sk != NULL) {
					ax25->sk->state = TCP_CLOSE;
					ax25->sk->err   = ETIMEDOUT;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead  = 1;
				}
			} else {
				ax25->n2count++;
				if (!ax25_dev_is_dama_slave(ax25->device))	/* dl1bke */
					ax25_send_control(ax25, DISC, POLLON, C_COMMAND);
			}
			break;

		case AX25_STATE_3: 
			ax25->n2count = 1;
			if (!ax25->dama_slave)			/* dl1bke 960114 */
				ax25_transmit_enquiry(ax25);
			ax25->state   = AX25_STATE_4;
			break;

		case AX25_STATE_4:
			if (ax25->n2count == ax25->n2) {
#ifdef CONFIG_NETROM
				nr_link_failed(&ax25->dest_addr, ax25->device);
#endif
				ax25_clear_queues(ax25);
				ax25_send_control(ax25, DM, POLLON, C_RESPONSE);
				ax25->state = AX25_STATE_0;
				if (ax25->sk != NULL) {
					if (ax25->sk->debug)
						printk("Link Failure\n");
					ax25->sk->state = TCP_CLOSE;
					ax25->sk->err   = ETIMEDOUT;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead  = 1;
				}
			} else {
				ax25->n2count++;
				if (!ax25->dama_slave)		/* dl1bke 960114 */
					ax25_transmit_enquiry(ax25);
			}
			break;
	}

	ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);

	ax25_set_timer(ax25);
}

#endif
