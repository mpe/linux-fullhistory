/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_timer.c,v 1.75 2000/04/08 07:21:25 davem Exp $
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
int sysctl_tcp_synack_retries = TCP_SYNACK_RETRIES; 
int sysctl_tcp_keepalive_time = TCP_KEEPALIVE_TIME;
int sysctl_tcp_keepalive_probes = TCP_KEEPALIVE_PROBES;
int sysctl_tcp_keepalive_intvl = TCP_KEEPALIVE_INTVL;
int sysctl_tcp_retries1 = TCP_RETR1;
int sysctl_tcp_retries2 = TCP_RETR2;
int sysctl_tcp_orphan_retries = TCP_ORPHAN_RETRIES;

static void tcp_retransmit_timer(unsigned long);
static void tcp_delack_timer(unsigned long);
static void tcp_probe_timer(unsigned long);
static void tcp_keepalive_timer (unsigned long data);
static void tcp_twkill(unsigned long);

const char timer_bug_msg[] = KERN_DEBUG "tcpbug: unknown timer value\n";

/*
 * Using different timers for retransmit, delayed acks and probes
 * We may wish use just one timer maintaining a list of expire jiffies 
 * to optimize.
 */

void tcp_init_xmit_timers(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	init_timer(&tp->retransmit_timer);
	tp->retransmit_timer.function=&tcp_retransmit_timer;
	tp->retransmit_timer.data = (unsigned long) sk;

	init_timer(&tp->delack_timer);
	tp->delack_timer.function=&tcp_delack_timer;
	tp->delack_timer.data = (unsigned long) sk;

	init_timer(&tp->probe_timer);
	tp->probe_timer.function=&tcp_probe_timer;
	tp->probe_timer.data = (unsigned long) sk;

	init_timer(&sk->timer);
	sk->timer.function=&tcp_keepalive_timer;
	sk->timer.data = (unsigned long) sk;
}

/*
 *	Reset the retransmission timer
 */
 
void tcp_reset_xmit_timer(struct sock *sk, int what, unsigned long when)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	switch (what) {
	case TCP_TIME_RETRANS:
		/* When seting the transmit timer the probe timer 
		 * should not be set.
		 * The delayed ack timer can be set if we are changing the
		 * retransmit timer when removing acked frames.
		 */
		if (timer_pending(&tp->probe_timer) && del_timer(&tp->probe_timer))
			__sock_put(sk);
		if (when > TCP_RTO_MAX) {
			printk(KERN_DEBUG "reset_xmit_timer sk=%p when=0x%lx, caller=%p\n", sk, when, NET_CALLER(sk));
			when = TCP_RTO_MAX;
		}
		if (!mod_timer(&tp->retransmit_timer, jiffies+when))
			sock_hold(sk);
		break;

	case TCP_TIME_DACK:
		if (!mod_timer(&tp->delack_timer, jiffies+when))
			sock_hold(sk);
		break;

	case TCP_TIME_PROBE0:
		if (!mod_timer(&tp->probe_timer, jiffies+when))
			sock_hold(sk);
		break;

	default:
		printk(KERN_DEBUG "bug: unknown timer value\n");
	};
}

void tcp_clear_xmit_timers(struct sock *sk)
{	
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	if(timer_pending(&tp->retransmit_timer) && del_timer(&tp->retransmit_timer))
		__sock_put(sk);
	if(timer_pending(&tp->delack_timer) && del_timer(&tp->delack_timer))
		__sock_put(sk);
	tp->ack.blocked = 0;
	if(timer_pending(&tp->probe_timer) && del_timer(&tp->probe_timer))
		__sock_put(sk);
	if(timer_pending(&sk->timer) && del_timer(&sk->timer))
		__sock_put(sk);
}

static void tcp_write_err(struct sock *sk)
{
	sk->err = sk->err_soft ? : ETIMEDOUT;
	sk->error_report(sk);

	tcp_done(sk);
}

/* Do not allow orphaned sockets to eat all our resources.
 * This is direct violation of TCP specs, but it is required
 * to prevent DoS attacks. It is called when a retransmission timeout
 * or zero probe timeout occurs on orphaned socket.
 *
 * Criterium is still not confirmed experimentally and may change.
 * We kill the socket, if:
 * 1. If number of orphaned sockets exceeds an administratively configured
 *    limit.
 * 2. Under pessimistic assumption that all the orphans eat memory not
 *    less than this one, total consumed memory exceeds all
 *    the available memory.
 */
static int tcp_out_of_resources(struct sock *sk, int do_reset)
{
	int orphans = atomic_read(&tcp_orphan_count);

	if (orphans >= sysctl_tcp_max_orphans ||
	    ((orphans*atomic_read(&sk->wmem_alloc))>>PAGE_SHIFT) >= num_physpages) {
		if (net_ratelimit())
			printk(KERN_INFO "Out of socket memory\n");
		if (do_reset)
			tcp_send_active_reset(sk, GFP_ATOMIC);
		tcp_done(sk);
		return 1;
	}
	return 0;
}

/* A write timeout has occurred. Process the after effects. */
static int tcp_write_timeout(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int retry_until;

	if ((1<<sk->state)&(TCPF_SYN_SENT|TCPF_SYN_RECV)) {
		if (tp->retransmits)
			dst_negative_advice(&sk->dst_cache);
		retry_until = tp->syn_retries ? : sysctl_tcp_syn_retries;
	} else {
		if (tp->retransmits >= sysctl_tcp_retries1) {
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

		retry_until = sysctl_tcp_retries2;
		if (sk->dead) {
			if (tcp_out_of_resources(sk, tp->retransmits < retry_until))
				return 1;

			retry_until = sysctl_tcp_orphan_retries;
		}
	}

	if (tp->retransmits >= retry_until) {
		/* Has it gone just too far? */
		tcp_write_err(sk);
		return 1;
	}
	return 0;
}

static void tcp_delack_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	bh_lock_sock(sk);
	if (sk->lock.users) {
		/* Try again later. */
		tp->ack.blocked = 1;
		NET_INC_STATS_BH(DelayedACKLocked);
		tcp_reset_xmit_timer(sk, TCP_TIME_DACK, TCP_DELACK_MIN);
		goto out_unlock;
	}

	if (tp->ack.pending) {
		if (!tp->ack.pingpong) {
			/* Delayed ACK missed: inflate ATO. */
			tp->ack.ato = min(tp->ack.ato<<1, TCP_ATO_MAX);
		} else {
			/* Delayed ACK missed: leave pingpong mode and
			 * deflate ATO.
			 */
			tp->ack.pingpong = 0;
			tp->ack.ato = TCP_ATO_MIN;
		}
		tcp_send_ack(sk);
		NET_INC_STATS_BH(DelayedACKs);
	}
	TCP_CHECK_TIMER(sk);

out_unlock:
	timer_exit(&tp->delack_timer);
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void tcp_probe_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	int max_probes;

	bh_lock_sock(sk);
	if (sk->lock.users) {
		/* Try again later. */
		tcp_reset_xmit_timer(sk, TCP_TIME_PROBE0, HZ/5);
		goto out_unlock;
	}

	if (sk->state == TCP_CLOSE)
		goto out_unlock;

	if (tp->packets_out || !tp->send_head) {
		tp->probes_out = 0;
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
	max_probes = sysctl_tcp_retries2;

	if (sk->dead) {
		if (tcp_out_of_resources(sk, tp->probes_out <= max_probes))
			goto out_unlock;

		max_probes = sysctl_tcp_orphan_retries;
	}

	if (tp->probes_out > max_probes) {
		tcp_write_err(sk);
	} else {
		/* Only send another probe if we didn't close things up. */
		tcp_send_probe0(sk);
		TCP_CHECK_TIMER(sk);
	}
out_unlock:
	timer_exit(&tp->probe_timer);
	bh_unlock_sock(sk);
	sock_put(sk);
}


/* Kill off TIME_WAIT sockets once their lifetime has expired. */
static int tcp_tw_death_row_slot = 0;
int tcp_tw_count = 0;

static struct tcp_tw_bucket *tcp_tw_death_row[TCP_TWKILL_SLOTS];
static spinlock_t tw_death_lock = SPIN_LOCK_UNLOCKED;
static struct timer_list tcp_tw_timer = { function: tcp_twkill };

static void SMP_TIMER_NAME(tcp_twkill)(unsigned long dummy)
{
	struct tcp_tw_bucket *tw;
	int killed = 0;

	/* NOTE: compare this to previous version where lock
	 * was released after detaching chain. It was racy,
	 * because tw buckets are scheduled in not serialized context
	 * in 2.3 (with netfilter), and with softnet it is common, because
	 * soft irqs are not sequenced.
	 */
	spin_lock(&tw_death_lock);

	if (tcp_tw_count == 0)
		goto out;

	while((tw = tcp_tw_death_row[tcp_tw_death_row_slot]) != NULL) {
		tcp_tw_death_row[tcp_tw_death_row_slot] = tw->next_death;
		tw->pprev_death = NULL;
		spin_unlock(&tw_death_lock);

		tcp_timewait_kill(tw);
		tcp_tw_put(tw);

		killed++;

		spin_lock(&tw_death_lock);
	}
	tcp_tw_death_row_slot =
		((tcp_tw_death_row_slot + 1) & (TCP_TWKILL_SLOTS - 1));

	if ((tcp_tw_count -= killed) != 0)
		mod_timer(&tcp_tw_timer, jiffies+TCP_TWKILL_PERIOD);
	net_statistics[smp_processor_id()*2].TimeWaited += killed;
out:
	spin_unlock(&tw_death_lock);
}

SMP_TIMER_DEFINE(tcp_twkill, tcp_twkill_task);

/* These are always called from BH context.  See callers in
 * tcp_input.c to verify this.
 */

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
		if (--tcp_tw_count == 0)
			del_timer(&tcp_tw_timer);
	}
	spin_unlock(&tw_death_lock);
}

/* Short-time timewait calendar */

static int tcp_twcal_hand = -1;
static int tcp_twcal_jiffie;
static void tcp_twcal_tick(unsigned long);
static struct timer_list tcp_twcal_timer = {NULL, NULL, 0, 0, tcp_twcal_tick,};
static struct tcp_tw_bucket *tcp_twcal_row[TCP_TW_RECYCLE_SLOTS];

void tcp_tw_schedule(struct tcp_tw_bucket *tw, int timeo)
{
	struct tcp_tw_bucket **tpp;
	int slot;

	/* timeout := RTO * 3.5
	 *
	 * 3.5 = 1+2+0.5 to wait for two retransmits.
	 *
	 * RATIONALE: if FIN arrived and we entered TIME-WAIT state,
	 * our ACK acking that FIN can be lost. If N subsequent retransmitted
	 * FINs (or previous seqments) are lost (probability of such event
	 * is p^(N+1), where p is probability to lose single packet and
	 * time to detect the loss is about RTO*(2^N - 1) with exponential
	 * backoff). Normal timewait length is calculated so, that we
	 * waited at least for one retransmitted FIN (maximal RTO is 120sec).
	 * [ BTW Linux. following BSD, violates this requirement waiting
	 *   only for 60sec, we should wait at least for 240 secs.
	 *   Well, 240 consumes too much of resources 8)
	 * ]
	 * This interval is not reduced to catch old duplicate and
	 * responces to our wandering segments living for two MSLs.
	 * However, if we use PAWS to detect
	 * old duplicates, we can reduce the interval to bounds required
	 * by RTO, rather than MSL. So, if peer understands PAWS, we
	 * kill tw bucket after 3.5*RTO (it is important that this number
	 * is greater than TS tick!) and detect old duplicates with help
	 * of PAWS.
	 */
	slot = (timeo + (1<<TCP_TW_RECYCLE_TICK) - 1) >> TCP_TW_RECYCLE_TICK;

	spin_lock(&tw_death_lock);

	/* Unlink it, if it was scheduled */
	if (tw->pprev_death) {
		if(tw->next_death)
			tw->next_death->pprev_death = tw->pprev_death;
		*tw->pprev_death = tw->next_death;
		tw->pprev_death = NULL;
		tcp_tw_count--;
	} else
		atomic_inc(&tw->refcnt);

	if (slot >= TCP_TW_RECYCLE_SLOTS) {
		/* Schedule to slow timer */
		if (timeo >= TCP_TIMEWAIT_LEN) {
			slot = TCP_TWKILL_SLOTS-1;
		} else {
			slot = (timeo + TCP_TWKILL_PERIOD-1) / TCP_TWKILL_PERIOD;
			if (slot >= TCP_TWKILL_SLOTS)
				slot = TCP_TWKILL_SLOTS-1;
		}
		tw->ttd = jiffies + timeo;
		slot = (tcp_tw_death_row_slot + slot) & (TCP_TWKILL_SLOTS - 1);
		tpp = &tcp_tw_death_row[slot];
	} else {
		tw->ttd = jiffies + (slot<<TCP_TW_RECYCLE_TICK);

		if (tcp_twcal_hand < 0) {
			tcp_twcal_hand = 0;
			tcp_twcal_jiffie = jiffies;
			tcp_twcal_timer.expires = tcp_twcal_jiffie + (slot<<TCP_TW_RECYCLE_TICK);
			add_timer(&tcp_twcal_timer);
		} else {
			if ((long)(tcp_twcal_timer.expires - jiffies) > (slot<<TCP_TW_RECYCLE_TICK))
				mod_timer(&tcp_twcal_timer, jiffies + (slot<<TCP_TW_RECYCLE_TICK));
			slot = (tcp_twcal_hand + slot)&(TCP_TW_RECYCLE_SLOTS-1);
		}
		tpp = &tcp_twcal_row[slot];
	}

	if((tw->next_death = *tpp) != NULL)
		(*tpp)->pprev_death = &tw->next_death;
	*tpp = tw;
	tw->pprev_death = tpp;

	if (tcp_tw_count++ == 0)
		mod_timer(&tcp_tw_timer, jiffies+TCP_TWKILL_PERIOD);
	spin_unlock(&tw_death_lock);
}

void SMP_TIMER_NAME(tcp_twcal_tick)(unsigned long dummy)
{
	int n, slot;
	unsigned long j;
	unsigned long now = jiffies;
	int killed = 0;
	int adv = 0;

	spin_lock(&tw_death_lock);
	if (tcp_twcal_hand < 0)
		goto out;

	slot = tcp_twcal_hand;
	j = tcp_twcal_jiffie;

	for (n=0; n<TCP_TW_RECYCLE_SLOTS; n++) {
		if ((long)(j - now) <= 0) {
			struct tcp_tw_bucket *tw;

			while((tw = tcp_twcal_row[slot]) != NULL) {
				tcp_twcal_row[slot] = tw->next_death;
				tw->pprev_death = NULL;

				tcp_timewait_kill(tw);
				tcp_tw_put(tw);
				killed++;
			}
		} else {
			if (!adv) {
				adv = 1;
				tcp_twcal_jiffie = j;
				tcp_twcal_hand = slot;
			}

			if (tcp_twcal_row[slot] != NULL) {
				mod_timer(&tcp_twcal_timer, j);
				goto out;
			}
		}
		j += (1<<TCP_TW_RECYCLE_TICK);
		slot = (slot+1)&(TCP_TW_RECYCLE_SLOTS-1);
	}
	tcp_twcal_hand = -1;

out:
	if ((tcp_tw_count -= killed) == 0)
		del_timer(&tcp_tw_timer);
	net_statistics[smp_processor_id()*2].TimeWaitKilled += killed;
	spin_unlock(&tw_death_lock);
}

SMP_TIMER_DEFINE(tcp_twcal_tick, tcp_twcal_tasklet);

/*
 *	The TCP retransmit timer.
 */

static void tcp_retransmit_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	bh_lock_sock(sk);
	if (sk->lock.users) {
		/* Try again later */  
		tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, HZ/20);
		goto out_unlock;
	}

	if (sk->state == TCP_CLOSE || tp->packets_out == 0)
		goto out_unlock;

	BUG_TRAP(!skb_queue_empty(&sk->write_queue));

	if (tcp_write_timeout(sk))
		goto out_unlock;

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

	tp->dup_acks = 0;
	tp->high_seq = tp->snd_nxt;
	if (tcp_retransmit_skb(sk, skb_peek(&sk->write_queue)) > 0) {
		/* Retransmission failed because of local congestion,
		 * do not backoff.
		 */
		if (!tp->retransmits)
			tp->retransmits=1;
		tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS,
				     min(tp->rto, TCP_RESOURCE_PROBE_INTERVAL));
		TCP_CHECK_TIMER(sk);
		goto out_unlock;
	}

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
	tp->retransmits++;
	tp->rto = min(tp->rto << 1, TCP_RTO_MAX);
	tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, tp->rto);
	if (tp->retransmits > sysctl_tcp_retries1)
		__sk_dst_reset(sk);
	TCP_CHECK_TIMER(sk);

out_unlock:
	timer_exit(&tp->retransmit_timer);
	bh_unlock_sock(sk);
	sock_put(sk);
}

/*
 *	Timer for listening sockets
 */

static void tcp_synack_timer(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct tcp_listen_opt *lopt = tp->listen_opt;
	int max_retries = tp->syn_retries ? : sysctl_tcp_synack_retries;
	int thresh = max_retries;
	unsigned long now = jiffies;
	struct open_request **reqp, *req;
	int i, budget;

	if (lopt == NULL || lopt->qlen == 0)
		return;

	/* Normally all the openreqs are young and become mature
	 * (i.e. converted to established socket) for first timeout.
	 * If synack was not acknowledged for 3 seconds, it means
	 * one of the following things: synack was lost, ack was lost,
	 * rtt is high or nobody planned to ack (i.e. synflood).
	 * When server is a bit loaded, queue is populated with old
	 * open requests, reducing effective size of queue.
	 * When server is well loaded, queue size reduces to zero
	 * after several minutes of work. It is not synflood,
	 * it is normal operation. The solution is pruning
	 * too old entries overriding normal timeout, when
	 * situation becomes dangerous.
	 *
	 * Essentially, we reserve half of room for young
	 * embrions; and abort old ones without pity, if old
	 * ones are about to clog our table.
	 */
	if (lopt->qlen>>(lopt->max_qlen_log-1)) {
		int young = (lopt->qlen_young<<1);

		while (thresh > 2) {
			if (lopt->qlen < young)
				break;
			thresh--;
			young <<= 1;
		}
	}

	if (tp->defer_accept)
		max_retries = tp->defer_accept;

	budget = 2*(TCP_SYNQ_HSIZE/(TCP_TIMEOUT_INIT/TCP_SYNQ_INTERVAL));
	i = lopt->clock_hand;

	do {
		reqp=&lopt->syn_table[i];
		while ((req = *reqp) != NULL) {
			if ((long)(now - req->expires) >= 0) {
				if ((req->retrans < thresh ||
				     (req->acked && req->retrans < max_retries))
				    && !req->class->rtx_syn_ack(sk, req, NULL)) {
					unsigned long timeo;

					if (req->retrans++ == 0)
						lopt->qlen_young--;
					timeo = min((TCP_TIMEOUT_INIT << req->retrans),
						    TCP_RTO_MAX);
					req->expires = now + timeo;
					reqp = &req->dl_next;
					continue;
				}

				/* Drop this request */
				write_lock(&tp->syn_wait_lock);
				*reqp = req->dl_next;
				write_unlock(&tp->syn_wait_lock);
				lopt->qlen--;
				if (req->retrans == 0)
					lopt->qlen_young--;
				tcp_openreq_free(req);
				continue;
			}
			reqp = &req->dl_next;
		}

		i = (i+1)&(TCP_SYNQ_HSIZE-1);

	} while (--budget > 0);

	lopt->clock_hand = i;

	if (lopt->qlen)
		tcp_reset_keepalive_timer(sk, TCP_SYNQ_INTERVAL);
}

void tcp_delete_keepalive_timer (struct sock *sk)
{
	if (timer_pending(&sk->timer) && del_timer (&sk->timer))
		__sock_put(sk);
}

void tcp_reset_keepalive_timer (struct sock *sk, unsigned long len)
{
	if (!mod_timer(&sk->timer, jiffies+len))
		sock_hold(sk);
}

void tcp_set_keepalive(struct sock *sk, int val)
{
	if ((1<<sk->state)&(TCPF_CLOSE|TCPF_LISTEN))
		return;

	if (val && !sk->keepopen)
		tcp_reset_keepalive_timer(sk, keepalive_time_when(&sk->tp_pinfo.af_tcp));
	else if (!val)
		tcp_delete_keepalive_timer(sk);
}


static void tcp_keepalive_timer (unsigned long data)
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

	if (sk->state == TCP_LISTEN) {
		tcp_synack_timer(sk);
		goto out;
	}

	if (sk->state == TCP_FIN_WAIT2 && sk->dead) {
		if (tp->linger2 >= 0) {
			int tmo = tcp_fin_time(tp) - TCP_TIMEWAIT_LEN;

			if (tmo > 0) {
				tcp_time_wait(sk, TCP_FIN_WAIT2, tmo);
				goto out;
			}
		}
		tcp_send_active_reset(sk, GFP_ATOMIC);
		goto death;
	}

	if (!sk->keepopen || sk->state == TCP_CLOSE)
		goto out;

	elapsed = keepalive_time_when(tp);

	/* It is alive without keepalive 8) */
	if (tp->packets_out || tp->send_head)
		goto resched;

	elapsed = tcp_time_stamp - tp->rcv_tstamp;

	if (elapsed >= keepalive_time_when(tp)) {
		if ((!tp->keepalive_probes && tp->probes_out >= sysctl_tcp_keepalive_probes) ||
		     (tp->keepalive_probes && tp->probes_out >= tp->keepalive_probes)) {
			tcp_send_active_reset(sk, GFP_ATOMIC);
			tcp_write_err(sk);
			goto out;
		}
		if (tcp_write_wakeup(sk) <= 0) {
			tp->probes_out++;
			elapsed = keepalive_intvl_when(tp);
		} else {
			/* If keepalive was lost due to local congestion,
			 * try harder.
			 */
			elapsed = TCP_RESOURCE_PROBE_INTERVAL;
		}
	} else {
		/* It is tp->rcv_tstamp + keepalive_time_when(tp) */
		elapsed = keepalive_time_when(tp) - elapsed;
	}

	TCP_CHECK_TIMER(sk);

resched:
	tcp_reset_keepalive_timer (sk, elapsed);
	goto out;

death:	
	tcp_done(sk);

out:
	timer_exit(&sk->timer);
	bh_unlock_sock(sk);
	sock_put(sk);
}
