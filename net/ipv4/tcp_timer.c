/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_timer.c,v 1.32 1997/12/08 07:03:29 freitag Exp $
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

int sysctl_tcp_syn_retries = TCP_SYN_RETRIES; 
int sysctl_tcp_keepalive_time = TCP_KEEPALIVE_TIME;
int sysctl_tcp_keepalive_probes = TCP_KEEPALIVE_PROBES;
int sysctl_tcp_retries1 = TCP_RETR1;
int sysctl_tcp_retries2 = TCP_RETR2;

static void tcp_sltimer_handler(unsigned long);
static void tcp_syn_recv_timer(unsigned long);
static void tcp_keepalive(unsigned long data);

struct timer_list	tcp_slow_timer = {
	NULL, NULL,
	0, 0,
	tcp_sltimer_handler,
};


struct tcp_sl_timer tcp_slt_array[TCP_SLT_MAX] = {
	{ATOMIC_INIT(0), TCP_SYNACK_PERIOD, 0, tcp_syn_recv_timer},/* SYNACK	*/
	{ATOMIC_INIT(0), TCP_KEEPALIVE_PERIOD, 0, tcp_keepalive}   /* KEEPALIVE	*/
};

const char timer_bug_msg[] = KERN_DEBUG "tcpbug: unknown timer value\n";

/*
 * Using different timers for retransmit, delayed acks and probes
 * We may wish use just one timer maintaining a list of expire jiffies 
 * to optimize.
 */

void tcp_init_xmit_timers(struct sock *sk)
{
	init_timer(&sk->tp_pinfo.af_tcp.retransmit_timer);
	sk->tp_pinfo.af_tcp.retransmit_timer.function=&tcp_retransmit_timer;
	sk->tp_pinfo.af_tcp.retransmit_timer.data = (unsigned long) sk;
	
	init_timer(&sk->tp_pinfo.af_tcp.delack_timer);
	sk->tp_pinfo.af_tcp.delack_timer.function=&tcp_delack_timer;
	sk->tp_pinfo.af_tcp.delack_timer.data = (unsigned long) sk;

	init_timer(&sk->tp_pinfo.af_tcp.probe_timer);
	sk->tp_pinfo.af_tcp.probe_timer.function=&tcp_probe_timer;
	sk->tp_pinfo.af_tcp.probe_timer.data = (unsigned long) sk;
}

/*
 *	Reset the retransmission timer
 */
 
void tcp_reset_xmit_timer(struct sock *sk, int what, unsigned long when)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	if((long)when <= 0) {
		printk(KERN_DEBUG "xmit_timer <= 0 - timer:%d when:%lx\n", what, when);
		when=HZ/50;
	}

	switch (what) {
	case TIME_RETRANS:
		/* When seting the transmit timer the probe timer 
		 * should not be set.
		 * The delayed ack timer can be set if we are changing the
		 * retransmit timer when removing acked frames.
		 */
		del_timer(&tp->probe_timer);
		del_timer(&tp->retransmit_timer);
		tp->retransmit_timer.expires=jiffies+when;
		add_timer(&tp->retransmit_timer);
		break;

	case TIME_DACK:
		del_timer(&tp->delack_timer);
		tp->delack_timer.expires=jiffies+when;
		add_timer(&tp->delack_timer);
		break;

	case TIME_PROBE0:
		del_timer(&tp->probe_timer);
		tp->probe_timer.expires=jiffies+when;
		add_timer(&tp->probe_timer);
		break;	

	case TIME_WRITE:
		printk(KERN_DEBUG "bug: tcp_reset_xmit_timer TIME_WRITE\n");
		break;

	default:
		printk(KERN_DEBUG "bug: unknown timer value\n");
	};
}

void tcp_clear_xmit_timers(struct sock *sk)
{	
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	del_timer(&tp->retransmit_timer);
	del_timer(&tp->delack_timer);
	del_timer(&tp->probe_timer);
}

static int tcp_write_err(struct sock *sk, int force)
{
	sk->err = sk->err_soft ? sk->err_soft : ETIMEDOUT;
	sk->error_report(sk);
	
	tcp_clear_xmit_timers(sk);
	
	/* Time wait the socket. */
	if (!force && (1<<sk->state) & (TCPF_FIN_WAIT1|TCPF_FIN_WAIT2|TCPF_CLOSING)) {
		tcp_set_state(sk,TCP_TIME_WAIT);
		tcp_reset_msl_timer (sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
	} else {
		/* Clean up time. */
		tcp_set_state(sk, TCP_CLOSE);
		return 0;
	}
	return 1;
}

/*
 *	A write timeout has occurred. Process the after effects. BROKEN (badly)
 */

static int tcp_write_timeout(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/*
	 *	Look for a 'soft' timeout.
	 */
	if ((sk->state == TCP_ESTABLISHED &&
	     tp->retransmits && (tp->retransmits % TCP_QUICK_TRIES) == 0) ||
	    (sk->state != TCP_ESTABLISHED && tp->retransmits > sysctl_tcp_retries1)) {
		/*	Attempt to recover if arp has changed (unlikely!) or
		 *	a route has shifted (not supported prior to 1.3).
		 */
		ip_rt_advice((struct rtable**)&sk->dst_cache, 0);
	}
	
	/* Have we tried to SYN too many times (repent repent 8)) */
	if(tp->retransmits > sysctl_tcp_syn_retries && sk->state==TCP_SYN_SENT) {
		tcp_write_err(sk, 1);
		/* Don't FIN, we got nothing back */
		return 0;
	}

	/* Has it gone just too far? */
	if (tp->retransmits > sysctl_tcp_retries2) 
		return tcp_write_err(sk, 0);

	return 1;
}


void tcp_delack_timer(unsigned long data) {

	struct sock *sk = (struct sock*)data;

	if(sk->zapped)
		return;
	
	if (sk->tp_pinfo.af_tcp.delayed_acks)
		tcp_read_wakeup(sk); 		
}

void tcp_probe_timer(unsigned long data) {

	struct sock *sk = (struct sock*)data;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	if(sk->zapped) 
		return;
	
	if (sk->sock_readers) {
		/* Try again in second. */
		tcp_reset_xmit_timer(sk, TIME_PROBE0, HZ);
		return;
	}

	/*
	 *	*WARNING* RFC 1122 forbids this 
	 * 	It doesn't AFAIK, because we kill the retransmit timer -AK
	 *	FIXME: We ought not to do it, Solaris 2.5 actually has fixing
	 *	this behaviour in Solaris down as a bug fix. [AC]
	 */
	if (tp->probes_out > sysctl_tcp_retries2) {
		if(sk->err_soft)
			sk->err = sk->err_soft;
		else
			sk->err = ETIMEDOUT;
		sk->error_report(sk);

		/* Time wait the socket. */
		if ((1<<sk->state) & (TCPF_FIN_WAIT1|TCPF_FIN_WAIT2|TCPF_CLOSING)) {
			tcp_set_state(sk, TCP_TIME_WAIT);
			tcp_reset_msl_timer (sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
		} else {
			/* Clean up time. */
			tcp_set_state(sk, TCP_CLOSE);
		}
	}
	
	tcp_send_probe0(sk);
}

static __inline__ int tcp_keepopen_proc(struct sock *sk)
{
	int res = 0;

	if ((1<<sk->state) & (TCPF_ESTABLISHED|TCPF_CLOSE_WAIT|TCPF_FIN_WAIT2)) {
		struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
		__u32 elapsed = jiffies - tp->rcv_tstamp;

		if (elapsed >= sysctl_tcp_keepalive_time) {
			if (tp->probes_out > sysctl_tcp_keepalive_probes) {
				if(sk->err_soft)
					sk->err = sk->err_soft;
				else
					sk->err = ETIMEDOUT;

				tcp_set_state(sk, TCP_CLOSE);
			} else {
				tp->probes_out++;
				tp->pending = TIME_KEEPOPEN;
				tcp_write_wakeup(sk);
				res = 1;
			}
		}
	}
	return res;
}

/*
 *	Check all sockets for keepalive timer
 *	Called every 75 seconds
 *	This timer is started by af_inet init routine and is constantly
 *	running.
 *
 *	It might be better to maintain a count of sockets that need it using
 *	setsockopt/tcp_destroy_sk and only set the timer when needed.
 */

/*
 *	don't send over 5 keepopens at a time to avoid burstiness 
 *	on big servers [AC]
 */
#define MAX_KA_PROBES	5

int sysctl_tcp_max_ka_probes = MAX_KA_PROBES;

/* Keepopen's are only valid for "established" TCP's, nicely our listener
 * hash gets rid of most of the useless testing, so we run through a couple
 * of the established hash chains each clock tick.  -DaveM
 *
 * And now, even more magic... TIME_WAIT TCP's cannot have keepalive probes
 * going off for them, so we only need check the first half of the established
 * hash table, even less testing under heavy load.
 *
 * I _really_ would rather do this by adding a new timer_struct to struct sock,
 * and this way only those who set the keepalive option will get the overhead.
 * The idea is you set it for 2 hours when the sock is first connected, when it
 * does fire off (if at all, most sockets die earlier) you check for the keepalive
 * option and also if the sock has been idle long enough to start probing.
 */
static void tcp_keepalive(unsigned long data)
{
	static int chain_start = 0;
	int count = 0;
	int i;
	
	for(i = chain_start; i < (chain_start + ((TCP_HTABLE_SIZE/2) >> 2)); i++) {
		struct sock *sk = tcp_established_hash[i];
		while(sk) {
			if(sk->keepopen) {
				count += tcp_keepopen_proc(sk);
				if(count == sysctl_tcp_max_ka_probes)
					goto out;
			}
			sk = sk->next;
		}
	}
out:
	chain_start = ((chain_start + ((TCP_HTABLE_SIZE/2)>>2)) &
		       ((TCP_HTABLE_SIZE/2) - 1));
}

/*
 *	The TCP retransmit timer. This lacks a few small details.
 *
 *	1. 	An initial rtt timeout on the probe0 should cause what we can
 *		of the first write queue buffer to be split and sent.
 *	2.	On a 'major timeout' as defined by RFC1122 we shouldn't report
 *		ETIMEDOUT if we know an additional 'soft' error caused this.
 *		tcp_err should save a 'soft error' for us.
 *	[Unless someone has broken it then it does, except for one 2.0 
 *	broken case of a send when the route/device is directly unreachable,
 *	and we error but should retry! - FIXME] [AC]
 */

void tcp_retransmit_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	/* We are reset. We will send no more retransmits. */
	if(sk->zapped) {
		tcp_clear_xmit_timer(sk, TIME_RETRANS);
		return;
	}

	if (sk->sock_readers) {
		/* Try again in a second. */
		tcp_reset_xmit_timer(sk, TIME_RETRANS, HZ);
		return;
	}
	lock_sock(sk);

	/* Clear delay ack timer. */
	tcp_clear_xmit_timer(sk, TIME_DACK);

	/* Retransmission. */
	tp->retrans_head = NULL;
	if (tp->retransmits == 0) {
		/* remember window where we lost
		 * "one half of the current window but at least 2 segments"
		 */
		tp->snd_ssthresh = max(tp->snd_cwnd >> 1, 2);
		tp->snd_cwnd_cnt = 0;
		tp->snd_cwnd = 1;
	}

	tp->retransmits++;

	tp->dup_acks = 0;
	tp->high_seq = tp->snd_nxt;
	tcp_do_retransmit(sk, 0);

	/* Increase the timeout each time we retransmit.  Note that
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
	tp->backoff++;	/* FIXME: always same as retransmits? -- erics */
	tp->rto = min(tp->rto << 1, 120*HZ);
	tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);

	tcp_write_timeout(sk);

	release_sock(sk);
}

/*
 *	Slow timer for SYN-RECV sockets
 */

/* This now scales very nicely. -DaveM */
static void tcp_syn_recv_timer(unsigned long data)
{
	struct sock *sk;
	unsigned long now = jiffies;
	int i;

	for(i = 0; i < TCP_LHTABLE_SIZE; i++) {
		sk = tcp_listening_hash[i];

		while(sk) {
			struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
			
			/* TCP_LISTEN is implied. */
			if (!sk->sock_readers && tp->syn_wait_queue) {
				struct open_request *prev = (struct open_request *)(&tp->syn_wait_queue);
				struct open_request *req = tp->syn_wait_queue;
				do {
					struct open_request *conn;
				  
					conn = req;
					req = req->dl_next;

					if (conn->sk) {
						prev = conn; 
						continue; 
					}

					if ((long)(now - conn->expires) <= 0)
						break;

					tcp_synq_unlink(tp, conn, prev);
					if (conn->retrans >= sysctl_tcp_retries1) {
#ifdef TCP_DEBUG
						printk(KERN_DEBUG "syn_recv: "
						       "too many retransmits\n");
#endif
						(*conn->class->destructor)(conn);
						tcp_dec_slow_timer(TCP_SLT_SYNACK);
						sk->ack_backlog--;
						tcp_openreq_free(conn);

						if (!tp->syn_wait_queue)
							break;
					} else {
						__u32 timeo;
						struct open_request *op; 

						(*conn->class->rtx_syn_ack)(sk, conn);

						conn->retrans++;
#ifdef TCP_DEBUG
						printk(KERN_DEBUG "syn_ack rtx %d\n",
						       conn->retrans);
#endif
						timeo = min((TCP_TIMEOUT_INIT 
							     << conn->retrans),
							    120*HZ);
						conn->expires = now + timeo;
						op = prev->dl_next; 
						tcp_synq_queue(tp, conn);
						if (op != prev->dl_next)
							prev = prev->dl_next;
					}
					/* old prev still valid here */
				} while (req);
			}
			sk = sk->next;
		}
	}
}

void tcp_sltimer_handler(unsigned long data)
{
	struct tcp_sl_timer *slt = tcp_slt_array;
	unsigned long next = ~0UL;
	unsigned long now = jiffies;
	int i;

	for (i=0; i < TCP_SLT_MAX; i++, slt++) {
		if (atomic_read(&slt->count)) {
			long trigger;

			trigger = slt->period - ((long)(now - slt->last));

			if (trigger <= 0) {
				(*slt->handler)((unsigned long) slt);
				slt->last = now;
				trigger = slt->period;
			}
			next = min(next, trigger);
		}
	}

	if (next != ~0UL) {
		tcp_slow_timer.expires = now + next;
		add_timer(&tcp_slow_timer);
	}
}

void __tcp_inc_slow_timer(struct tcp_sl_timer *slt)
{
	unsigned long now = jiffies;
	unsigned long next = 0;
	unsigned long when;

	slt->last = now;
		
	when = now + slt->period;
	if (del_timer(&tcp_slow_timer))
		next = tcp_slow_timer.expires;

	if (next && ((long)(next - when) < 0))
		when = next;
		
	tcp_slow_timer.expires = when;
	add_timer(&tcp_slow_timer);
}
