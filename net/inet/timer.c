/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		TIMER - implementation of software timers.
 *
 * Version:	@(#)timer.c	1.0.7	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Fred Baumgarten, <dc6iq@insu1.etec.uni-karlsruhe.de>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <linux/interrupt.h>
#include "inet.h"
#include "timer.h"
#include "dev.h"
#include "ip.h"
#include "protocol.h"
#include "tcp.h"
#include "skbuff.h"
#include "sock.h"
#include "arp.h"


struct timer *timer_base = NULL;
unsigned long seq_offset;


void
delete_timer(struct timer *t)
{
  struct timer *tm;

  DPRINTF((DBG_TMR, "delete_timer(t=%X)\n", t));
  cli();
  if (timer_base == NULL || t == NULL) {
	t->running = 9;
	sti();
	return;
  }
  if (t == timer_base) {
	timer_base = t->next;
	if (timer_base != NULL) {
		timer_table[NET_TIMER].expires = timer_base->when;
		timer_active |= (1 << NET_TIMER);
	} else {
		timer_active &= ~(1 << NET_TIMER);
	}
	t->running = 0;
	sti();
	return;
  }
  tm = timer_base;
  while (tm != NULL) {
	if (tm->next == t) {
		tm->next = t->next;
		t->running = 0;
		sti();
		return;
	}
	tm = tm->next;
  }
  sti();
  t->running = 9;
}


void
reset_timer(struct timer *t)
{
  struct timer *tm;

  DPRINTF((DBG_TMR, "reset_timer(t=%X) when = %d jiffies = %d\n",
						t, t->when, jiffies));
  if (t == NULL) {
	printk("*** reset timer NULL timer\n");
	__asm__ ("\t int $3\n"::);
  }
  if (t->running) {
        DPRINTF((DBG_TMR, "t->running has value of %d, len %d\n",
						t->running, t->len));
  }
  t->running = 1;
  delete_timer(t);		/* here is another race condition ! */
  cli();			/* what about a new reset_timer while being\ */
  if (!((t->running == 0) || (t->running == 9))) {    /* about to install on old one ? -FB */
        printk("reset_timer(): t->running after delete_timer: %d !\n",
								t->running);
	sti();
	return;
  }
  t->running = 2;
  if ((int) t->len < 0)		/* prevent close to infinite timers. THEY _DO_ */
        t->len = 3;		/* happen (negative values ?) - don't ask me why ! -FB */
  
  delete_timer(t);
  t->when = timer_seq + t->len;

  /* First see if it goes at the beginning. */

  if (timer_base == NULL) {
	t->next = NULL;
	timer_base = t;
	timer_table[NET_TIMER].expires = timer_base->when;
	timer_active |= (1 << NET_TIMER);
	t->running = 3;
	sti();
	return;
  }
  if (before(t->when, timer_base->when)) {
	t->next = timer_base;
	timer_base = t;
	timer_table[NET_TIMER].expires = timer_base->when;
  	timer_active |= (1 << NET_TIMER);
	t->running = 4;

	sti();
	return;
  }
  tm = timer_base;
  while (tm != NULL) {
	if (tm->next == NULL || t->when < tm->next->when) {
		t->next = tm->next;
		tm->next = t;
		timer_table[NET_TIMER].expires = timer_base->when;
		timer_active |= (1 << NET_TIMER);
		t->running = 5;
		sti();
		return;
	}
	tm = tm->next;
  }
  sti();
}


/*
 * Now we will only be called whenever we need to do
 * something, but we must be sure to process all of the
 * sockets that need it.
 */
void
net_timer(void)
{
  struct sock *sk;
  struct timer *tm;

  cli();			/* a timer might expire and a new one with */
				/* earlier expiration could be inserted before -FB */
  tm = timer_base;
  while ((tm != NULL) && (jiffies >= tm->when)) {
	int why;

	sk = tm->sk;
	if (sk->inuse) {
		break;
	}
	sk->inuse = 1;
	sti();
	why = sk->timeout;

	DPRINTF((DBG_TMR, "net_timer: found sk=%X why = %d\n", sk, why));
	if (sk->keepopen) {
		sk->time_wait.len = TCP_TIMEOUT_LEN;
		sk->timeout = TIME_KEEPOPEN;
		reset_timer(tm);
	} else {
		sk->timeout = 0;
		delete_timer(tm);
	}
	
	/* Always see if we need to send an ack. */
	if (sk->ack_backlog) {
		sk->prot->read_wakeup(sk);
		if (!sk->dead) wake_up(sk->sleep);
	}
	
	/* Now we need to figure out why the socket was on the timer. */
	switch (why) {
		case TIME_DONE:
			if (!sk->dead || sk->state != TCP_CLOSE) {
				printk("non dead socket in time_done\n");
				release_sock(sk);
				break;
			}
			destroy_sock(sk);
			break;
		case TIME_DESTROY:
			/*
			 * We've waited for a while for all the memory
			 * assosiated with the socket to be freed.  We
			 * need to print an error message.
			 */
			DPRINTF((DBG_TMR, "possible memory leak.  sk = %X\n", sk));
			destroy_sock(sk);
			sk->inuse = 0;
			break;
		case TIME_CLOSE:
			/* We've waited long enough, close the socket. */
			sk->state = TCP_CLOSE;
			delete_timer(&sk->time_wait);

			/*
			 * Kill the ARP entry in case the hardware
			 * has changed.
			 */
			arp_destroy(sk->daddr);
			if (!sk->dead) wake_up(sk->sleep);
			sk->shutdown = SHUTDOWN_MASK;
			sk->time_wait.len = TCP_DONE_TIME;
			sk->timeout = TIME_DESTROY;
			reset_timer (&sk->time_wait);
			release_sock(sk);
			break;
		case TIME_WRITE: /* try to retransmit. */
			/*
			 * It could be we got here because we
			 * needed to send an ack.  So we need
			 * to check for that.
			 */
			if (sk->send_head != NULL) {
				if (before(jiffies, sk->send_head->when +
							sk->rtt)) {
					sk->time_wait.len = sk->rtt;
					sk->timeout = TIME_WRITE;
					reset_timer(&sk->time_wait);
					release_sock(sk);
					break;
				}
				DPRINTF((DBG_TMR, "retransmitting.\n"));
				sk->prot->retransmit(sk, 0);

				if (sk->retransmits > TCP_RETR1) {
					DPRINTF((DBG_TMR, "timer.c TIME_WRITE time-out 1\n"));
					arp_destroy(sk->daddr);
					ip_route_check(sk->daddr);
				}

				if (sk->retransmits > TCP_RETR2) {
					DPRINTF((DBG_TMR, "timer.c TIME_WRITE time-out 2\n"));
					sk->err = ETIMEDOUT;
					if (sk->state == TCP_FIN_WAIT1 ||
					    sk->state == TCP_FIN_WAIT2 ||
					    sk->state == TCP_LAST_ACK) {
						sk->state = TCP_TIME_WAIT;
						sk->timeout = TIME_CLOSE;
						sk->time_wait.len =
							TCP_TIMEWAIT_LEN;
						reset_timer(&sk->time_wait);
					} else {
						sk->prot->close(sk,1);
						break;
					}
				}
			}
			release_sock(sk);
			break;
		case TIME_KEEPOPEN:
			/* Send something to keep the connection open. */
			if (sk->prot->write_wakeup != NULL)
					sk->prot->write_wakeup(sk);
			sk->retransmits ++;
			if (sk->shutdown == SHUTDOWN_MASK) {
				sk->prot->close(sk,1);
				sk->state = TCP_CLOSE;
			}

			if (sk->retransmits > TCP_RETR1) {
				DPRINTF((DBG_TMR, "timer.c TIME_KEEPOPEN time-out 1\n"));
				arp_destroy(sk->daddr);
				ip_route_check(sk->daddr);
				release_sock(sk);
				break;
			}
			if (sk->retransmits > TCP_RETR2) {
				DPRINTF((DBG_TMR, "timer.c TIME_KEEPOPEN time-out 2\n"));
				arp_destroy (sk->daddr);
				sk->err = ETIMEDOUT;
				if (sk->state == TCP_FIN_WAIT1 ||
				    sk->state == TCP_FIN_WAIT2) {
					sk->state = TCP_TIME_WAIT;
					if (!sk->dead) wake_up(sk->sleep);
					release_sock(sk);
				} else {
					sk->prot->close (sk, 1);
				}
				break;
			}
			release_sock(sk);
			break;
		default:
			printk("net timer expired - reason unknown, sk=%08X\n",
								(int) sk);
			release_sock(sk);
			break;
	}
	cli();
	tm = timer_base;
  }

  /* Now we need to reset the timer. */

  if (timer_base != NULL) {
	timer_table[NET_TIMER].expires = timer_base->when;
	timer_active |= 1 << NET_TIMER;
  }
  sti();
}
