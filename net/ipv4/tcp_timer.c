/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_timer.c,v 1.66 1999/08/20 11:06:10 davem Exp $
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
static void tcp_twkill(unsigned long);

struct timer_list	tcp_slow_timer = {
	NULL, NULL,
	0, 0,
	tcp_sltimer_handler,
};


struct tcp_sl_timer tcp_slt_array[TCP_SLT_MAX] = {
	{ATOMIC_INIT(0), TCP_SYNACK_PERIOD, 0, tcp_syn_recv_timer},/* SYNACK	*/
	{ATOMIC_INIT(0), TCP_TWKILL_PERIOD, 0, tcp_twkill}         /* TWKILL	*/
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

	spin_lock_bh(&sk->timer_lock);
	switch (what) {
	case TIME_RETRANS:
		/* When seting the transmit timer the probe timer 
		 * should not be set.
		 * The delayed ack timer can be set if we are changing the
		 * retransmit timer when removing acked frames.
		 */
		if(tp->probe_timer.prev && del_timer(&tp->probe_timer))
			__sock_put(sk);
		if (!tp->retransmit_timer.prev || !del_timer(&tp->retransmit_timer))
			sock_hold(sk);
		if (when > 120*HZ) {
			printk(KERN_DEBUG "reset_xmit_timer sk=%p when=0x%lx, caller=%p\n", sk, when, NET_CALLER(sk));
			when = 120*HZ;
		}
		mod_timer(&tp->retransmit_timer, jiffies+when);
		break;

	case TIME_DACK:
		if (!tp->delack_timer.prev || !del_timer(&tp->delack_timer))
			sock_hold(sk);
		mod_timer(&tp->delack_timer, jiffies+when);
		break;

	case TIME_PROBE0:
		if (!tp->probe_timer.prev || !del_timer(&tp->probe_timer))
			sock_hold(sk);
		mod_timer(&tp->probe_timer, jiffies+when);
		break;	

	case TIME_WRITE:
		printk(KERN_DEBUG "bug: tcp_reset_xmit_timer TIME_WRITE\n");
		break;

	default:
		printk(KERN_DEBUG "bug: unknown timer value\n");
	};
	spin_unlock_bh(&sk->timer_lock);
}

void tcp_clear_xmit_timers(struct sock *sk)
{	
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	spin_lock_bh(&sk->timer_lock);
	if(tp->retransmit_timer.prev && del_timer(&tp->retransmit_timer))
		__sock_put(sk);
	if(tp->delack_timer.prev && del_timer(&tp->delack_timer))
		__sock_put(sk);
	if(tp->probe_timer.prev && del_timer(&tp->probe_timer))
		__sock_put(sk);
	if(sk->timer.prev && del_timer(&sk->timer))
		__sock_put(sk);
	spin_unlock_bh(&sk->timer_lock);
}

static void tcp_write_err(struct sock *sk, int force)
{
	sk->err = sk->err_soft ? sk->err_soft : ETIMEDOUT;
	sk->error_report(sk);

	tcp_clear_xmit_timers(sk);

	/* Do not time wait the socket. It is timed out and, hence,
	 * idle for 120*HZ. "force" argument is ignored, delete
	 * it eventually.
	 */

	/* Clean up time. */
	tcp_set_state(sk, TCP_CLOSE);
	tcp_done(sk);
}

/* A write timeout has occurred. Process the after effects. */
static void tcp_write_timeout(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Look for a 'soft' timeout. */
	if ((sk->state == TCP_ESTABLISHED &&
	     tp->retransmits && (tp->retransmits % TCP_QUICK_TRIES) == 0) ||
	    (sk->state != TCP_ESTABLISHED && tp->retransmits > sysctl_tcp_retries1)) {
		/* NOTE. draft-ietf-tcpimpl-pmtud-01.txt requires pmtu black
		   hole detection. :-(

		   It is place to make it. It is not made. I do not want
		   to make it. It is disguisting. It does not work in any
		   case. Let me to cite the same draft, which requires for
		   us to implement this:

   "The one security concern raised by this memo is that ICMP black holes
   are often caused by over-zealous security administrators who block
   all ICMP messages.  It is vitally important that those who design and
   deploy security systems understand the impact of strict filtering on
   upper-layer protocols.  The safest web site in the world is worthless
   if most TCP implementations cannot transfer data from it.  It would
   be far nicer to have all of the black holes fixed rather than fixing
   all of the TCP implementations."

                   Golden words :-).
		 */

		dst_negative_advice(&sk->dst_cache);
	}
	
	/* Have we tried to SYN too many times (repent repent 8)) */
	if(tp->retransmits > sysctl_tcp_syn_retries && sk->state==TCP_SYN_SENT) {
		tcp_write_err(sk, 1);
		/* Don't FIN, we got nothing back */
	} else if (tp->retransmits > sysctl_tcp_retries2) {
		/* Has it gone just too far? */
		tcp_write_err(sk, 0);
	}
}

void tcp_delack_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;

	bh_lock_sock(sk);
	if (sk->lock.users) {
		/* Try again later. */
		tcp_reset_xmit_timer(sk, TIME_DACK, HZ/5);
		goto out_unlock;
	}

	if(!sk->zapped &&
	   sk->tp_pinfo.af_tcp.delayed_acks &&
	   sk->state != TCP_CLOSE)
		tcp_send_ack(sk);

out_unlock:
	bh_unlock_sock(sk);
	sock_put(sk);
}

void tcp_probe_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	if(sk->zapped)
		goto out;

	bh_lock_sock(sk);
	if (sk->lock.users) {
		/* Try again later. */
		tcp_reset_xmit_timer(sk, TIME_PROBE0, HZ/5);
		goto out_unlock;
	}

	/* *WARNING* RFC 1122 forbids this
	 *
	 * It doesn't AFAIK, because we kill the retransmit timer -AK
	 *
	 * FIXME: We ought not to do it, Solaris 2.5 actually has fixing
	 * this behaviour in Solaris down as a bug fix. [AC]
	 *
	 * Let me to explain. probes_out is zeroed by incoming ACKs
	 * even if they advertise zero window. Hence, connection is killed only
	 * if we received no ACKs for normal connection timeout. It is not killed
	 * only because window stays zero for some time, window may be zero
	 * until armageddon and even later. We are in full accordance
	 * with RFCs, only probe timer combines both retransmission timeout
	 * and probe timeout in one bottle.				--ANK
	 */
	if (tp->probes_out > sysctl_tcp_retries2) {
		tcp_write_err(sk, 0);
	} else {
		/* Only send another probe if we didn't close things up. */
		tcp_send_probe0(sk);
	}
out_unlock:
	bh_unlock_sock(sk);
out:
	sock_put(sk);
}


/* Kill off TIME_WAIT sockets once their lifetime has expired. */
int tcp_tw_death_row_slot = 0;
static struct tcp_tw_bucket *tcp_tw_death_row[TCP_TWKILL_SLOTS] =
	{ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
static spinlock_t tw_death_lock = SPIN_LOCK_UNLOCKED;


static void tcp_twkill(unsigned long data)
{
	struct tcp_tw_bucket *tw;
	int killed = 0;

	/* The death-row tw chains are only ever touched
	 * in BH context so no BH disabling (for now) is needed.
	 */
	spin_lock(&tw_death_lock);
	tw = tcp_tw_death_row[tcp_tw_death_row_slot];
	tcp_tw_death_row[tcp_tw_death_row_slot] = NULL;
	tcp_tw_death_row_slot =
	  ((tcp_tw_death_row_slot + 1) & (TCP_TWKILL_SLOTS - 1));
	spin_unlock(&tw_death_lock);

	while(tw != NULL) {
		struct tcp_tw_bucket *next = tw->next_death;

		tcp_timewait_kill(tw);
		tcp_tw_put(tw);
		killed++;
		tw = next;
	}
	if(killed != 0) {
		struct tcp_sl_timer *slt = (struct tcp_sl_timer *)data;
		atomic_sub(killed, &slt->count);
	}
}

/* These are always called from BH context.  See callers in
 * tcp_input.c to verify this.
 */
void tcp_tw_schedule(struct tcp_tw_bucket *tw)
{
	struct tcp_tw_bucket **tpp;
	int slot;

	spin_lock(&tw_death_lock);
	slot = (tcp_tw_death_row_slot - 1) & (TCP_TWKILL_SLOTS - 1);
	tpp = &tcp_tw_death_row[slot];
	if((tw->next_death = *tpp) != NULL)
		(*tpp)->pprev_death = &tw->next_death;
	*tpp = tw;
	tw->pprev_death = tpp;

	tw->death_slot = slot;
	atomic_inc(&tw->refcnt);
	spin_unlock(&tw_death_lock);

	tcp_inc_slow_timer(TCP_SLT_TWKILL);
}

/* Happens rarely if at all, no care about scalability here. */
void tcp_tw_reschedule(struct tcp_tw_bucket *tw)
{
	struct tcp_tw_bucket **tpp;
	int slot;

	spin_lock(&tw_death_lock);
	if (tw->pprev_death) {
		if(tw->next_death)
			tw->next_death->pprev_death = tw->pprev_death;
		*tw->pprev_death = tw->next_death;
		tw->pprev_death = NULL;
	} else
		atomic_inc(&tw->refcnt);

	slot = (tcp_tw_death_row_slot - 1) & (TCP_TWKILL_SLOTS - 1);
	tpp = &tcp_tw_death_row[slot];
	if((tw->next_death = *tpp) != NULL)
		(*tpp)->pprev_death = &tw->next_death;
	*tpp = tw;
	tw->pprev_death = tpp;

	tw->death_slot = slot;
	spin_unlock(&tw_death_lock);

	/* Timer was incremented when we first entered the table. */
}

/* This is for handling early-kills of TIME_WAIT sockets. */
void tcp_tw_deschedule(struct tcp_tw_bucket *tw)
{
	spin_lock(&tw_death_lock);
	if (tw->pprev_death) {
		if(tw->next_death)
			tw->next_death->pprev_death = tw->pprev_death;
		*tw->pprev_death = tw->next_death;
		tw->pprev_death = NULL;
		tcp_tw_put(tw);
	}
	spin_unlock(&tw_death_lock);

	tcp_dec_slow_timer(TCP_SLT_TWKILL);
}


/*
 *	The TCP retransmit timer.
 *
 *	1. 	An initial rtt timeout on the probe0 should cause what we can
 *		of the first write queue buffer to be split and sent.
 *	2.	On a 'major timeout' as defined by RFC1122 we do not report
 *		ETIMEDOUT if we know an additional 'soft' error caused this.
 *		tcp_err saves a 'soft error' for us.
 */

void tcp_retransmit_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	/* We are reset. We will send no more retransmits. */
	if(sk->zapped)
		goto out;

	bh_lock_sock(sk);
	if (sk->lock.users) {
		/* Try again later */  
		tcp_reset_xmit_timer(sk, TIME_RETRANS, HZ/20);
		goto out_unlock;
	}

	/* Clear delay ack timer. */
	tcp_clear_xmit_timer(sk, TIME_DACK);

	/* RFC 2018, clear all 'sacked' flags in retransmission queue,
	 * the sender may have dropped out of order frames and we must
	 * send them out should this timer fire on us.
	 */
	if(tp->sack_ok) {
		struct sk_buff *skb = skb_peek(&sk->write_queue);

		while((skb != NULL) &&
		      (skb != tp->send_head) &&
		      (skb != (struct sk_buff *)&sk->write_queue)) {
			TCP_SKB_CB(skb)->sacked &=
				~(TCPCB_SACKED_ACKED | TCPCB_SACKED_RETRANS);
			skb = skb->next;
		}
	}

	/* Retransmission. */
	tp->retrans_head = NULL;
	tp->rexmt_done = 0;
	tp->fackets_out = 0;
	tp->retrans_out = 0;
	if (tp->retransmits == 0) {
		/* Remember window where we lost:
		 * "one half of the current window but at least 2 segments"
		 *
		 * Here "current window" means the effective one, which
		 * means it must be an accurate representation of our current
		 * sending rate _and_ the snd_wnd.
		 */
		tp->snd_ssthresh = tcp_recalc_ssthresh(tp);
		tp->snd_cwnd_cnt = 0;
		tp->snd_cwnd = 1;
	}

	tp->retransmits++;

	tp->dup_acks = 0;
	tp->high_seq = tp->snd_nxt;
	tcp_retransmit_skb(sk, skb_peek(&sk->write_queue));

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
	tp->backoff++;
	tp->rto = min(tp->rto << 1, 120*HZ);
	tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);

	tcp_write_timeout(sk);

out_unlock:
	bh_unlock_sock(sk);
out:
	sock_put(sk);
}

/*
 *	Slow timer for SYN-RECV sockets
 */

static void tcp_do_syn_queue(struct sock *sk, struct tcp_opt *tp, unsigned long now)
{
	struct open_request *prev, *req;

	prev = (struct open_request *) &tp->syn_wait_queue;
	for(req = tp->syn_wait_queue; req; ) {
		struct open_request *next = req->dl_next;

		if (!req->sk && (long)(now - req->expires) >= 0) {
			tcp_synq_unlink(tp, req, prev);
			if(req->retrans >= sysctl_tcp_retries1) {
				(*req->class->destructor)(req);
				tcp_dec_slow_timer(TCP_SLT_SYNACK);
				tp->syn_backlog--;
				tcp_openreq_free(req);
				if (! tp->syn_wait_queue)
					break;
			} else {
				unsigned long timeo;
				struct open_request *rp;

				(*req->class->rtx_syn_ack)(sk, req);
				req->retrans++;
				timeo = min((TCP_TIMEOUT_INIT << req->retrans),
					    (120 * HZ));
				req->expires = now + timeo;
				rp = prev->dl_next;
				tcp_synq_queue(tp, req);
				if(rp != prev->dl_next)
					prev = prev->dl_next;
			}
		} else
			prev = req;
		req = next;
	}
}

/* This now scales very nicely. -DaveM */
static void tcp_syn_recv_timer(unsigned long data)
{
	struct sock *sk;
	unsigned long now = jiffies;
	int i;

	read_lock(&tcp_lhash_lock);
	for(i = 0; i < TCP_LHTABLE_SIZE; i++) {
		sk = tcp_listening_hash[i];
		while(sk) {
			struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
			
			/* TCP_LISTEN is implied. */
			bh_lock_sock(sk);
			if (!sk->lock.users && tp->syn_wait_queue)
				tcp_do_syn_queue(sk, tp, now);
			bh_unlock_sock(sk);
			sk = sk->next;
		}
	}
	read_unlock(&tcp_lhash_lock);
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

			/* Only reschedule if some events remain. */
			if (atomic_read(&slt->count))
				next = min(next, trigger);
		}
	}
	if (next != ~0UL)
		mod_timer(&tcp_slow_timer, (now + next));
}

void __tcp_inc_slow_timer(struct tcp_sl_timer *slt)
{
	unsigned long now = jiffies;
	unsigned long when;

	slt->last = now;

	when = now + slt->period;

	if (tcp_slow_timer.prev) {
		if ((long)(tcp_slow_timer.expires - when) >= 0)
			mod_timer(&tcp_slow_timer, when);
	} else {
		tcp_slow_timer.expires = when;
		add_timer(&tcp_slow_timer);
	}
}

void tcp_delete_keepalive_timer (struct sock *sk)
{
	spin_lock_bh(&sk->timer_lock);
	if (sk->timer.prev && del_timer (&sk->timer))
		__sock_put(sk);
	spin_unlock_bh(&sk->timer_lock);
}

void tcp_reset_keepalive_timer (struct sock *sk, unsigned long len)
{
	spin_lock_bh(&sk->timer_lock);
	if(!sk->timer.prev || !del_timer(&sk->timer))
		sock_hold(sk);
	mod_timer(&sk->timer, jiffies+len);
	spin_unlock_bh(&sk->timer_lock);
}

void tcp_set_keepalive(struct sock *sk, int val)
{
	if (val && !sk->keepopen)
		tcp_reset_keepalive_timer(sk, sysctl_tcp_keepalive_time);
	else if (!val)
		tcp_delete_keepalive_timer(sk);
}


void tcp_keepalive_timer (unsigned long data)
{
	struct sock *sk = (struct sock *) data;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	__u32 elapsed;

	/* Only process if socket is not in use. */
	bh_lock_sock(sk);
	if (sk->lock.users) {
		/* Try again later. */ 
		tcp_reset_keepalive_timer (sk, HZ/20);
		goto out;
	}

	if (sk->state == TCP_FIN_WAIT2 && sk->dead)
		goto death;

	if (!sk->keepopen)
		goto out;

	elapsed = sysctl_tcp_keepalive_time;
	if (!((1<<sk->state) & (TCPF_ESTABLISHED|TCPF_CLOSE_WAIT|TCPF_FIN_WAIT2)))
		goto resched;

	elapsed = tcp_time_stamp - tp->rcv_tstamp;

	if (elapsed >= sysctl_tcp_keepalive_time) {
		if (tp->probes_out > sysctl_tcp_keepalive_probes) {
			tcp_write_err(sk, 1);
			goto out;
		}
		tp->probes_out++;
		tp->pending = TIME_KEEPOPEN;
		tcp_write_wakeup(sk);
		/* Randomize to avoid synchronization */
		elapsed = (TCP_KEEPALIVE_PERIOD>>1) + (net_random()%TCP_KEEPALIVE_PERIOD);
	} else {
		/* It is tp->rcv_tstamp + sysctl_tcp_keepalive_time */
		elapsed = sysctl_tcp_keepalive_time - elapsed;
	}

resched:
	tcp_reset_keepalive_timer (sk, elapsed);
	goto out;

death:	
	tcp_set_state(sk, TCP_CLOSE);
	tcp_clear_xmit_timers(sk);
	tcp_done(sk);

out:
	bh_unlock_sock(sk);
	sock_put(sk);
}
