/*
 *	AX.25 release 036
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	AX.25 036	Jonathan(G4KLX)	Cloned from ax25_timer.c.
 *			Joerg(DL1BKE)	Added DAMA Slave Timeout timer
 */

#include <linux/config.h>
#if defined(CONFIG_AX25_DAMA_SLAVE)
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
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

static void ax25_ds_timeout(unsigned long);

/*
 *	Add DAMA slave timeout timer to timer list.
 *	Unlike the connection based timers the timeout function gets 
 *	triggered every second. Please note that NET_AX25_DAMA_SLAVE_TIMEOUT
 *	(aka /proc/sys/net/ax25/{dev}/dama_slave_timeout) is still in
 *	1/10th of a second.
 */

static void ax25_ds_add_timer(ax25_dev *ax25_dev)
{
	struct timer_list *t = &ax25_dev->dama.slave_timer;
	t->data		= (unsigned long) ax25_dev;
	t->function	= &ax25_ds_timeout;
	t->expires	= jiffies + HZ;
	add_timer(t);
}

void ax25_ds_del_timer(ax25_dev *ax25_dev)
{
	if (ax25_dev) del_timer(&ax25_dev->dama.slave_timer);
}

void ax25_ds_set_timer(ax25_dev *ax25_dev)
{
	if (ax25_dev == NULL)		/* paranoia */
		return;

	del_timer(&ax25_dev->dama.slave_timer);
	ax25_dev->dama.slave_timeout = ax25_dev->values[AX25_VALUES_DS_TIMEOUT] / 10;
	ax25_ds_add_timer(ax25_dev);
}

/*
 *	DAMA Slave Timeout
 *	Silently discard all (slave) connections in case our master forgot us...
 */

static void ax25_ds_timeout(unsigned long arg)
{
	ax25_dev *ax25_dev = (struct ax25_dev *) arg;
	ax25_cb *ax25;
	
	if (ax25_dev == NULL || !ax25_dev->dama.slave)
		return;			/* Yikes! */
	
	if (!ax25_dev->dama.slave_timeout || --ax25_dev->dama.slave_timeout)
	{
		ax25_ds_set_timer(ax25_dev);
		return;
	}
	
	for (ax25=ax25_list; ax25 != NULL; ax25 = ax25->next)
	{
		if (ax25->ax25_dev != ax25_dev || !ax25->dama_slave)
			continue;

		ax25_link_failed(&ax25->dest_addr, ax25_dev->dev);
		ax25_clear_queues(ax25);
		ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
		ax25->state = AX25_STATE_0;

		if (ax25->sk != NULL)
		{
			SOCK_DEBUG(ax25->sk, "AX.25 DAMA Slave Timeout\n");
			ax25->sk->state     = TCP_CLOSE;
			ax25->sk->err       = ETIMEDOUT;
			ax25->sk->shutdown |= SEND_SHUTDOWN;
			if (!ax25->sk->dead)
				ax25->sk->state_change(ax25->sk);
			ax25->sk->dead      = 1;
		}
		
		ax25_set_timer(ax25);	/* notify socket... */
	}
	
	ax25_dev_dama_off(ax25_dev);
}


/*
 *	AX.25 TIMER 
 *
 *	This routine is called every 100ms. Decrement timer by this
 *	amount - if expired then process the event.
 */
void ax25_ds_timer(ax25_cb *ax25)
{
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
				if (ax25->sk->rmem_alloc < (ax25->sk->rcvbuf / 2) && (ax25->condition & AX25_COND_OWN_RX_BUSY)) {
					ax25->condition &= ~AX25_COND_OWN_RX_BUSY;
					break;
				}
			}
			break;

		default:
			break;
	}
	
	/* dl1bke 960114: T3 works much like the IDLE timeout, but
	 *                gets reloaded with every frame for this
	 *		  connection.
	 */

	if (ax25->t3timer > 0 && --ax25->t3timer == 0) {
		ax25_link_failed(&ax25->dest_addr, ax25->ax25_dev->dev);
		ax25_clear_queues(ax25);
		ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);

		ax25->state = AX25_STATE_0;
		ax25_dama_off(ax25);

		if (ax25->sk != NULL) {
			SOCK_DEBUG(ax25->sk, "AX.25 T3 Timeout\n");
			ax25->sk->state     = TCP_CLOSE;
			ax25->sk->err       = ETIMEDOUT;
			ax25->sk->shutdown |= SEND_SHUTDOWN;
			if (!ax25->sk->dead)
				ax25->sk->state_change(ax25->sk);
			ax25->sk->dead      = 1;
		}

		ax25_set_timer(ax25);

		return;
	}

	/* dl1bke 960228: close the connection when IDLE expires.
	 *		  unlike T3 this timer gets reloaded only on
	 *		  I frames.
	 */

	if (ax25->idletimer > 0 && --ax25->idletimer == 0) {
		ax25_clear_queues(ax25);

		ax25->n2count = 0;
		ax25->t3timer = ax25->t3;

		/* state 1 or 2 should not happen, but... */

		if (ax25->state == AX25_STATE_1 || ax25->state == AX25_STATE_2)
			ax25->state = AX25_STATE_0;
		else
			ax25->state = AX25_STATE_2;

		ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);

		if (ax25->sk != NULL) {
			ax25->sk->state     = TCP_CLOSE;
			ax25->sk->err       = 0;
			ax25->sk->shutdown |= SEND_SHUTDOWN;
			if (!ax25->sk->dead)
				ax25->sk->state_change(ax25->sk);
			ax25->sk->dead      = 1;
			ax25->sk->destroy   = 1;
		}
	}

	ax25_set_timer(ax25);
}

/* dl1bke 960114: The DAMA protocol requires to send data and SABM/DISC
 *                within the poll of any connected channel. Remember 
 *                that we are not allowed to send anything unless we
 *                get polled by the Master.
 *
 *                Thus we'll have to do parts of our T1 handling in
 *                ax25_enquiry_response().
 */
void ax25_ds_t1_timeout(ax25_cb *ax25)
{	
	switch (ax25->state) {
		case AX25_STATE_1: 
			if (ax25->n2count == ax25->n2) {
				if (ax25->modulus == AX25_MODULUS) {
					ax25_link_failed(&ax25->dest_addr, ax25->ax25_dev->dev);
					ax25_clear_queues(ax25);
					ax25->state = AX25_STATE_0;
					if (ax25->sk != NULL) {
						ax25->sk->state     = TCP_CLOSE;
						ax25->sk->err       = ETIMEDOUT;
						ax25->sk->shutdown |= SEND_SHUTDOWN;
						if (!ax25->sk->dead)
							ax25->sk->state_change(ax25->sk);
						ax25->sk->dead      = 1;
					}
				} else {
					ax25->modulus = AX25_MODULUS;
					ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
					ax25->n2count = 0;
					ax25_send_control(ax25, AX25_SABM, AX25_POLLOFF, AX25_COMMAND);
				}
			} else {
				ax25->n2count++;
				if (ax25->modulus == AX25_MODULUS)
					ax25_send_control(ax25, AX25_SABM, AX25_POLLOFF, AX25_COMMAND);
				else
					ax25_send_control(ax25, AX25_SABME, AX25_POLLOFF, AX25_COMMAND);
			}
			break;

		case AX25_STATE_2:
			if (ax25->n2count == ax25->n2) {
				ax25_link_failed(&ax25->dest_addr, ax25->ax25_dev->dev);
				ax25_clear_queues(ax25);
				ax25->state = AX25_STATE_0;
				ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);

				if (ax25->sk != NULL) {
					ax25->sk->state     = TCP_CLOSE;
					ax25->sk->err       = ETIMEDOUT;
					ax25->sk->shutdown |= SEND_SHUTDOWN;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead      = 1;
				}
			} else {
				ax25->n2count++;
			}
			break;

		case AX25_STATE_3: 
			ax25->n2count = 1;
			ax25->state   = AX25_STATE_4;
			break;

		case AX25_STATE_4:
			if (ax25->n2count == ax25->n2) {
				ax25_link_failed(&ax25->dest_addr, ax25->ax25_dev->dev);
				ax25_clear_queues(ax25);
				ax25_send_control(ax25, AX25_DM, AX25_POLLON, AX25_RESPONSE);
				ax25->state = AX25_STATE_0;
				if (ax25->sk != NULL) {
					SOCK_DEBUG(ax25->sk, "AX.25 link Failure\n");
					ax25->sk->state     = TCP_CLOSE;
					ax25->sk->err       = ETIMEDOUT;
					ax25->sk->shutdown |= SEND_SHUTDOWN;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead      = 1;
				}
			} else {
				ax25->n2count++;
			}
			break;
	}

	ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);

	ax25_set_timer(ax25);
}

#endif
