/*
 *	AX.25 release 035
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
 *	AX.25 028a	Jonathan(G4KLX)	New state machine based on SDL diagrams.
 *	AX.25 028b	Jonathan(G4KLX)	Extracted AX25 control block from the
 *					sock structure.
 *	AX.25 029	Alan(GW4PTS)	Switched to KA9Q constant names.
 *	AX.25 031	Joerg(DL1BKE)	Added DAMA support
 *	AX.25 032	Joerg(DL1BKE)	Fixed DAMA timeout bug
 *	AX.25 033	Jonathan(G4KLX)	Modularisation functions.
 *	AX.25 035	Frederic(F1OAT)	Support for pseudo-digipeating.
 */

#include <linux/config.h>
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
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

static void ax25_timer(unsigned long);

/*
 *	Linux set timer
 */
void ax25_set_timer(ax25_cb *ax25)
{
	unsigned long flags;	

	save_flags(flags); cli();
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
 *	This routine is called every 100ms. Decrement timer by this
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
				if (ax25->sk->rmem_alloc < (ax25->sk->rcvbuf / 2) && (ax25->condition & AX25_COND_OWN_RX_BUSY)) {
					ax25->condition &= ~AX25_COND_OWN_RX_BUSY;
					ax25->condition &= ~AX25_COND_ACK_PENDING;
					if (!ax25->dama_slave)
						ax25_send_control(ax25, AX25_RR, AX25_POLLOFF, AX25_RESPONSE);
					break;
				}
			}
			/*
			 * Check for frames to transmit.
			 */
			if (!ax25->dama_slave)
				ax25_kick(ax25);
			break;

		default:
			break;
	}

	if (ax25->t2timer > 0 && --ax25->t2timer == 0) {
		if (ax25->state == AX25_STATE_3 || ax25->state == AX25_STATE_4) {
			if (ax25->condition & AX25_COND_ACK_PENDING) {
				ax25->condition &= ~AX25_COND_ACK_PENDING;
				if (!ax25->dama_slave)
					ax25_timeout_response(ax25);
			}
		}
	}

	if (ax25->t3timer > 0 && --ax25->t3timer == 0) {
		/* dl1bke 960114: T3 expires and we are in DAMA mode:  */
		/*                send a DISC and abort the connection */
		if (ax25->dama_slave) {
			ax25_link_failed(&ax25->dest_addr, ax25->device);
			ax25_clear_queues(ax25);
			ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);

			ax25->state = AX25_STATE_0;
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
			ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
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
			ax25->sk->state     = TCP_CLOSE;
			ax25->sk->err       = 0;
			ax25->sk->shutdown |= SEND_SHUTDOWN;
			if (!ax25->sk->dead)
				ax25->sk->state_change(ax25->sk);
			ax25->sk->dead      = 1;
			ax25->sk->destroy   = 1;
		}
	}

	/* dl1bke 960114: DAMA T1 timeouts are handled in ax25_dama_slave_transmit */
	/* 		  nevertheless we have to re-enqueue the timer struct...   */
	if (ax25->t1timer == 0 || --ax25->t1timer > 0) {
		ax25_set_timer(ax25);
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
void ax25_t1_timeout(ax25_cb *ax25)
{	
	switch (ax25->state) {
		case AX25_STATE_1: 
			if (ax25->n2count == ax25->n2) {
				if (ax25->modulus == AX25_MODULUS) {
					ax25_link_failed(&ax25->dest_addr, ax25->device);
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
					ax25->window  = ax25_dev_get_value(ax25->device, AX25_VALUES_WINDOW);
					ax25->n2count = 0;
					ax25_send_control(ax25, AX25_SABM, ax25_dev_is_dama_slave(ax25->device) ? AX25_POLLOFF : AX25_POLLON, AX25_COMMAND);
				}
			} else {
				ax25->n2count++;
				if (ax25->modulus == AX25_MODULUS)
					ax25_send_control(ax25, AX25_SABM, ax25_dev_is_dama_slave(ax25->device) ? AX25_POLLOFF : AX25_POLLON, AX25_COMMAND);
				else
					ax25_send_control(ax25, AX25_SABME, ax25_dev_is_dama_slave(ax25->device) ? AX25_POLLOFF : AX25_POLLON, AX25_COMMAND);
			}
			break;

		case AX25_STATE_2:
			if (ax25->n2count == ax25->n2) {
				ax25_link_failed(&ax25->dest_addr, ax25->device);
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
				if (!ax25_dev_is_dama_slave(ax25->device))
					ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
			}
			break;

		case AX25_STATE_3: 
			ax25->n2count = 1;
			if (!ax25->dama_slave)
				ax25_transmit_enquiry(ax25);
			ax25->state   = AX25_STATE_4;
			break;

		case AX25_STATE_4:
			if (ax25->n2count == ax25->n2) {
				ax25_link_failed(&ax25->dest_addr, ax25->device);
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
				if (!ax25->dama_slave)
					ax25_transmit_enquiry(ax25);
			}
			break;
	}

	ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);

	ax25_set_timer(ax25);
}

/************************************************************************/
/*	Module support functions follow.				*/
/************************************************************************/

static struct protocol_struct {
	struct protocol_struct *next;
	unsigned int pid;
	int (*func)(struct sk_buff *, ax25_cb *);
} *protocol_list = NULL;

static struct linkfail_struct {
	struct linkfail_struct *next;
	void (*func)(ax25_address *, struct device *);
} *linkfail_list = NULL;

static struct listen_struct {
	struct listen_struct *next;
	ax25_address  callsign;
	struct device *dev;
} *listen_list = NULL;

int ax25_protocol_register(unsigned int pid, int (*func)(struct sk_buff *, ax25_cb *))
{
	struct protocol_struct *protocol;
	unsigned long flags;

	if (pid == AX25_P_TEXT || pid == AX25_P_SEGMENT)
		return 0;
#ifdef CONFIG_INET
	if (pid == AX25_P_IP || pid == AX25_P_ARP)
		return 0;
#endif
	if ((protocol = (struct protocol_struct *)kmalloc(sizeof(*protocol), GFP_ATOMIC)) == NULL)
		return 0;

	protocol->pid  = pid;
	protocol->func = func;

	save_flags(flags);
	cli();

	protocol->next = protocol_list;
	protocol_list  = protocol;

	restore_flags(flags);

	return 1;
}

void ax25_protocol_release(unsigned int pid)
{
	struct protocol_struct *s, *protocol = protocol_list;
	unsigned long flags;

	if (protocol == NULL)
		return;

	save_flags(flags);
	cli();

	if (protocol->pid == pid) {
		protocol_list = protocol->next;
		restore_flags(flags);
		kfree_s(protocol, sizeof(struct protocol_struct));
		return;
	}

	while (protocol != NULL && protocol->next != NULL) {
		if (protocol->next->pid == pid) {
			s = protocol->next;
			protocol->next = protocol->next->next;
			restore_flags(flags);
			kfree_s(s, sizeof(struct protocol_struct));
			return;
		}

		protocol = protocol->next;
	}

	restore_flags(flags);
}

int ax25_linkfail_register(void (*func)(ax25_address *, struct device *))
{
	struct linkfail_struct *linkfail;
	unsigned long flags;

	if ((linkfail = (struct linkfail_struct *)kmalloc(sizeof(*linkfail), GFP_ATOMIC)) == NULL)
		return 0;

	linkfail->func = func;

	save_flags(flags);
	cli();

	linkfail->next = linkfail_list;
	linkfail_list  = linkfail;

	restore_flags(flags);

	return 1;
}

void ax25_linkfail_release(void (*func)(ax25_address *, struct device *))
{
	struct linkfail_struct *s, *linkfail = linkfail_list;
	unsigned long flags;

	if (linkfail == NULL)
		return;

	save_flags(flags);
	cli();

	if (linkfail->func == func) {
		linkfail_list = linkfail->next;
		restore_flags(flags);
		kfree_s(linkfail, sizeof(struct linkfail_struct));
		return;
	}

	while (linkfail != NULL && linkfail->next != NULL) {
		if (linkfail->next->func == func) {
			s = linkfail->next;
			linkfail->next = linkfail->next->next;
			restore_flags(flags);
			kfree_s(s, sizeof(struct linkfail_struct));
			return;
		}

		linkfail = linkfail->next;
	}

	restore_flags(flags);
}

int ax25_listen_register(ax25_address *callsign, struct device *dev)
{
	struct listen_struct *listen;
	unsigned long flags;

	if (ax25_listen_mine(callsign, dev))
		return 0;

	if ((listen = (struct listen_struct *)kmalloc(sizeof(*listen), GFP_ATOMIC)) == NULL)
		return 0;

	listen->callsign = *callsign;
	listen->dev      = dev;

	save_flags(flags);
	cli();

	listen->next = listen_list;
	listen_list  = listen;

	restore_flags(flags);

	return 1;
}

void ax25_listen_release(ax25_address *callsign, struct device *dev)
{
	struct listen_struct *s, *listen = listen_list;
	unsigned long flags;

	if (listen == NULL)
		return;

	save_flags(flags);
	cli();

	if (ax25cmp(&listen->callsign, callsign) == 0 && listen->dev == dev) {
		listen_list = listen->next;
		restore_flags(flags);
		kfree_s(listen, sizeof(struct listen_struct));
		return;
	}

	while (listen != NULL && listen->next != NULL) {
		if (ax25cmp(&listen->next->callsign, callsign) == 0 && listen->next->dev == dev) {
			s = listen->next;
			listen->next = listen->next->next;
			restore_flags(flags);
			kfree_s(s, sizeof(struct listen_struct));
			return;
		}

		listen = listen->next;
	}

	restore_flags(flags);
}

int (*ax25_protocol_function(unsigned int pid))(struct sk_buff *, ax25_cb *)
{
	struct protocol_struct *protocol;

	for (protocol = protocol_list; protocol != NULL; protocol = protocol->next)
		if (protocol->pid == pid)
			return protocol->func;

	return NULL;
}

int ax25_listen_mine(ax25_address *callsign, struct device *dev)
{
	struct listen_struct *listen;

	for (listen = listen_list; listen != NULL; listen = listen->next)
		if (ax25cmp(&listen->callsign, callsign) == 0 && (listen->dev == dev || listen->dev == NULL))
			return 1;

	return 0;
}

void ax25_link_failed(ax25_address *callsign, struct device *dev)
{
	struct linkfail_struct *linkfail;

	for (linkfail = linkfail_list; linkfail != NULL; linkfail = linkfail->next)
		(linkfail->func)(callsign, dev);
}

int ax25_protocol_is_registered(unsigned int pid)
{
	struct protocol_struct *protocol;

	for (protocol = protocol_list; protocol != NULL; protocol = protocol->next)
		if (protocol->pid == pid)
			return 1;

	return 0;
}

#endif
