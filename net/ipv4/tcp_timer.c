/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	@(#)tcp.c	1.0.16	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Charles Hedrick, <hedrick@klinzhai.rutgers.edu>
 *		Linus Torvalds, <torvalds@cs.helsinki.fi>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Matthew Dillon, <dillon@apollo.west.oic.com>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 */

#include <net/tcp.h>

void tcp_delack_timer(unsigned long data)
{
	tcp_send_ack((struct sock *) data);
}

/*
 *	Reset the retransmission timer
 *
 *	We currently ignore the why/when parameters, and decide on
 *	our own how long we should wait on our own..
 */
 
void tcp_reset_xmit_timer(struct sock *sk, int why, unsigned long when)
{
	unsigned long now = jiffies;

	when = ~0UL;
	why = -1;

	/*
	 * Was an old timer event active?
	 */
	if (del_timer(&sk->retransmit_timer)) {
		why = sk->ip_xmit_timeout;
		when = sk->retransmit_timer.expires;
	}

	/* 
	 * Keepopen processing?
	 */
	if (sk->keepopen) {
		unsigned long new_when = now + TCP_TIMEOUT_LEN;
		if (new_when < when) {
			when = new_when;
			why = TIME_KEEPOPEN;
		}
	}

	/*
	 * Retransmission?
	 */
	if (sk->send_head) {
		struct sk_buff * skb = sk->send_head;
		unsigned long new_when = skb->when + sk->rto;
		if (new_when < when) {
			when = new_when;
			why = TIME_WRITE;
		}
	} else if (!skb_queue_empty(&sk->write_queue)) {
		/*
		 * Zero window probe?
		 */
		struct sk_buff * skb = sk->write_queue.next;
		if (before(sk->window_seq, skb->end_seq)) {
			unsigned long new_when = now + TIME_PROBE0;
			if (new_when < when) {
				when = new_when;
				why = TIME_PROBE0;
			}
		}
	}

	if (why >= 0) {
		sk->ip_xmit_timeout = why;
		sk->retransmit_timer.expires = when;
		add_timer(&sk->retransmit_timer);
	}
}

/*
 *	POLICY:
 *
 * 	This is the normal code called for timeouts.  It does the retransmission
 * 	and then does backoff.  tcp_do_retransmit is separated out because
 * 	tcp_ack needs to send stuff from the retransmit queue without
 * 	initiating a backoff.
 */


static void tcp_retransmit_time(struct sock *sk, int all)
{
	tcp_do_retransmit(sk, all);

	/*
	 * Increase the timeout each time we retransmit.  Note that
	 * we do not increase the rtt estimate.  rto is initialized
	 * from rtt, but increases here.  Jacobson (SIGCOMM 88) suggests
	 * that doubling rto each time is the least we can get away with.
	 * In KA9Q, Karn uses this for the first few times, and then
	 * goes to quadratic.  netBSD doubles, but only goes up to *64,
	 * and clamps at 1 to 64 sec afterwards.  Note that 120 sec is
	 * defined in the protocol as the maximum possible RTT.  I guess
	 * we'll have to use something other than TCP to talk to the
	 * University of Mars.
	 *
	 * PAWS allows us longer timeouts and large windows, so once
	 * implemented ftp to mars will work nicely. We will have to fix
	 * the 120 second clamps though!
	 */

	sk->backoff++;
	sk->rto = min(sk->rto << 1, 120*HZ);
	tcp_reset_xmit_timer(sk, TIME_WRITE, sk->rto);
}

/*
 *	POLICY:
 *		Congestion control.
 *
 *	A timer event has trigger a tcp retransmit timeout. The
 *	socket xmit queue is ready and set up to send. Because
 *	the ack receive code keeps the queue straight we do
 *	nothing clever here.
 */

void tcp_retransmit(struct sock *sk, int all)
{
	if (all) 
	{
		tcp_retransmit_time(sk, all);
		return;
	}

	sk->ssthresh = sk->cong_window >> 1; /* remember window where we lost */
	/* sk->ssthresh in theory can be zero.  I guess that's OK */
	sk->cong_count = 0;

	sk->cong_window = 1;

	/* Do the actual retransmit. */
	tcp_retransmit_time(sk, all);
}

/*
 *	A write timeout has occurred. Process the after effects. BROKEN (badly)
 */

static int tcp_write_timeout(struct sock *sk)
{
	/*
	 *	Look for a 'soft' timeout.
	 */
	if ((sk->state == TCP_ESTABLISHED && sk->retransmits && !(sk->retransmits & 7))
		|| (sk->state != TCP_ESTABLISHED && sk->retransmits > TCP_RETR1)) 
	{
		/*
		 *	Attempt to recover if arp has changed (unlikely!) or
		 *	a route has shifted (not supported prior to 1.3).
		 */
		ip_rt_advice(&sk->ip_route_cache, 0);
	}
	
	/*
	 *	Have we tried to SYN too many times (repent repent 8))
	 */
	 
	if(sk->retransmits > TCP_SYN_RETRIES && sk->state==TCP_SYN_SENT)
	{
		if(sk->err_soft)
			sk->err=sk->err_soft;
		else
			sk->err=ETIMEDOUT;
		sk->error_report(sk);
		del_timer(&sk->retransmit_timer);
		tcp_statistics.TcpAttemptFails++;	/* Is this right ??? - FIXME - */
		tcp_set_state(sk,TCP_CLOSE);
		/* Don't FIN, we got nothing back */
		return 0;
	}
	/*
	 *	Has it gone just too far ?
	 */
	if (sk->retransmits > TCP_RETR2) 
	{
		if(sk->err_soft)
			sk->err = sk->err_soft;
		else
			sk->err = ETIMEDOUT;
		sk->error_report(sk);
		del_timer(&sk->retransmit_timer);
		/*
		 *	Time wait the socket 
		 */
		if (sk->state == TCP_FIN_WAIT1 || sk->state == TCP_FIN_WAIT2 || sk->state == TCP_CLOSING ) 
		{
			tcp_set_state(sk,TCP_TIME_WAIT);
			tcp_reset_msl_timer (sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
		}
		else
		{
			/*
			 *	Clean up time.
			 */
			tcp_set_state(sk, TCP_CLOSE);
			return 0;
		}
	}
	return 1;
}


/*
 *	The TCP retransmit timer. This lacks a few small details.
 *
 *	1. 	An initial rtt timeout on the probe0 should cause what we can
 *		of the first write queue buffer to be split and sent.
 *	2.	On a 'major timeout' as defined by RFC1122 we shouldn't report
 *		ETIMEDOUT if we know an additional 'soft' error caused this.
 *		tcp_err should save a 'soft error' for us.
 */

void tcp_retransmit_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	int why = sk->ip_xmit_timeout;

	/*
	 *	We are reset. We will send no more retransmits.
	 */
	 
	if(sk->zapped)
		return;
		
	/* 
	 *	Only process if socket is not in use
	 */

	if (sk->users) 
	{
		/* Try again in 1 second */
		sk->retransmit_timer.expires = jiffies+HZ;
		add_timer(&sk->retransmit_timer);
		return;
	}

	if (sk->ack_backlog && !sk->dead) 
		sk->data_ready(sk,0);

	/* Now we need to figure out why the socket was on the timer. */

	switch (why) 
	{
	/* Window probing */
	case TIME_PROBE0:
		tcp_send_probe0(sk);
		tcp_reset_xmit_timer(sk, 0, 0);		/* get us going again */
		break;

	/* Retransmitting */
	case TIME_WRITE:
		sk->prot->retransmit (sk, 0);
		tcp_write_timeout(sk);
		tcp_reset_xmit_timer(sk, 0, 0);		/* get us going again */		
		break;

	/* Sending Keepalives */
	case TIME_KEEPOPEN:
		/* Send something to keep the connection open. */
		if (sk->prot->write_wakeup)
			  sk->prot->write_wakeup (sk);
		sk->retransmits++;
		sk->prot->retransmits++;
		tcp_write_timeout(sk);
		tcp_reset_xmit_timer (sk, 0, 0);
		break;

	default:
		printk ("rexmit_timer: timer expired - reason unknown\n");
		break;
	}
}
