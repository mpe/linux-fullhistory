/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_input.c,v 1.193 2000/04/20 14:41:16 davem Exp $
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

/*
 * Changes:
 *		Pedro Roque	:	Fast Retransmit/Recovery.
 *					Two receive queues.
 *					Retransmit queue handled by TCP.
 *					Better retransmit timer handling.
 *					New congestion avoidance.
 *					Header prediction.
 *					Variable renaming.
 *
 *		Eric		:	Fast Retransmit.
 *		Randy Scott	:	MSS option defines.
 *		Eric Schenk	:	Fixes to slow start algorithm.
 *		Eric Schenk	:	Yet another double ACK bug.
 *		Eric Schenk	:	Delayed ACK bug fixes.
 *		Eric Schenk	:	Floyd style fast retrans war avoidance.
 *		David S. Miller	:	Don't allow zero congestion window.
 *		Eric Schenk	:	Fix retransmitter so that it sends
 *					next packet on ack of previous packet.
 *		Andi Kleen	:	Moved open_request checking here
 *					and process RSTs for open_requests.
 *		Andi Kleen	:	Better prune_queue, and other fixes.
 *		Andrey Savochkin:	Fix RTT measurements in the presnce of
 *					timestamps.
 *		Andrey Savochkin:	Check sequence numbers correctly when
 *					removing SACKs due to in sequence incoming
 *					data segments.
 *		Andi Kleen:		Make sure we never ack data there is not
 *					enough room for. Also make this condition
 *					a fatal error if it might still happen.
 *		Andi Kleen:		Add tcp_measure_rcv_mss to make 
 *					connections with MSS<min(MTU,ann. MSS)
 *					work without delayed acks. 
 *		Andi Kleen:		Process packets with PSH set in the
 *					fast path.
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <net/tcp.h>
#include <net/inet_common.h>
#include <linux/ipsec.h>

#ifdef CONFIG_SYSCTL
#define SYNC_INIT 0 /* let the user enable it */
#else
#define SYNC_INIT 1
#endif

/* These are on by default so the code paths get tested.
 * For the final 2.2 this may be undone at our discretion. -DaveM
 */
int sysctl_tcp_timestamps = 1;
int sysctl_tcp_window_scaling = 1;
int sysctl_tcp_sack = 1;

int sysctl_tcp_syncookies = SYNC_INIT; 
int sysctl_tcp_stdurg;
int sysctl_tcp_rfc1337;
int sysctl_tcp_tw_recycle = 1;
int sysctl_tcp_abort_on_overflow = 0;
int sysctl_tcp_max_orphans = NR_FILE;
int sysctl_tcp_max_tw_buckets = NR_FILE*2;

static int prune_queue(struct sock *sk);

/* 
 * Adapt the MSS value used to make delayed ack decision to the 
 * real world.
 *
 * The constant 536 hasn't any good meaning.  In IPv4 world
 * MTU may be smaller, though it contradicts to RFC1122, which
 * states that MSS must be at least 536.
 * We use the constant to do not ACK each second
 * packet in a stream of tiny size packets.
 * It means that super-low mtu links will be aggressively delacked.
 * Seems, it is even good. If they have so low mtu, they are weirdly
 * slow.
 *
 * AK: BTW it may be useful to add an option to lock the rcv_mss.
 *     this way the beowulf people wouldn't need ugly patches to get the
 *     ack frequencies they want and it would be an elegant way to tune delack.
 */ 
static __inline__ void tcp_measure_rcv_mss(struct tcp_opt *tp, struct sk_buff *skb)
{
	unsigned int len, lss;

	lss = tp->ack.last_seg_size; 
	tp->ack.last_seg_size = 0; 

	/* skb->len may jitter because of SACKs, even if peer
	 * sends good full-sized frames.
	 */
	len = skb->len;
	if (len >= tp->ack.rcv_mss) {
		tp->ack.rcv_mss = len;
	} else {
		/* Otherwise, we make more careful check taking into account,
		 * that SACKs block is variable.
		 *
		 * "len" is invariant segment length, including TCP header.
		 */
		len = skb->tail - skb->h.raw;
		if (len >= TCP_MIN_RCVMSS + sizeof(struct tcphdr)) {
			/* Subtract also invariant (if peer is RFC compliant),
			 * tcp header plus fixed timestamp option length.
			 * Resulting "len" is MSS free of SACK jitter.
			 */
			len -= tp->tcp_header_len;
			if (len == lss)
				tp->ack.rcv_mss = len;
			tp->ack.last_seg_size = len;
		}
	}
}


static __inline__ void tcp_enter_quickack_mode(struct tcp_opt *tp)
{
	unsigned quickacks = tcp_receive_window(tp)/(2*tp->ack.rcv_mss);

	tp->ack.quick = max(min(quickacks, 127), 1);

	if (!tp->tstamp_ok && tp->ack.quick>2) {
		/* Quick ACKs are _dangerous_, if RTTM is not used.
		 * See comment in tcp_init_metrics(). We still help
		 * them to overcome the most difficult, initial
		 * phase of slow start.
		 */
		tp->ack.quick = 2;
	}
}

/* Send ACKs quickly, if "quick" count is not ehausted
 * and the session is not interactive.
 */

static __inline__ int tcp_in_quickack_mode(struct tcp_opt *tp)
{
	return (tp->ack.quick && !tp->ack.pingpong);
}

/* There is something which you must keep in mind when you analyze the
 * behavior of the tp->ato delayed ack timeout interval.  When a
 * connection starts up, we want to ack as quickly as possible.  The
 * problem is that "good" TCP's do slow start at the beginning of data
 * transmission.  The means that until we send the first few ACK's the
 * sender will sit on his end and only queue most of his data, because
 * he can only send snd_cwnd unacked packets at any given time.  For
 * each ACK we send, he increments snd_cwnd and transmits more of his
 * queue.  -DaveM
 */
static void tcp_event_data_recv(struct tcp_opt *tp, struct sk_buff *skb)
{
	u32 now;

	tcp_measure_rcv_mss(tp, skb);

	tp->ack.pending = 1;
	tp->ack.rcv_segs++;

	now = tcp_time_stamp;

	if (!tp->ack.ato) {
		/* The _first_ data packet received, initialize
		 * delayed ACK engine.
		 */

		/* Help sender leave slow start quickly. */
		tcp_enter_quickack_mode(tp);

		/* Pingpong is off, session is not interactive by default */
		tp->ack.pingpong = 0;

		/* ATO is minimal */
		tp->ack.ato = TCP_ATO_MIN;
	} else {
		int m = now - tp->ack.lrcvtime;

		if (m > TCP_ATO_MAX/2) {
			/* Do not touch ATO, if interval is out of bounds.
			 * It will be deflated by delack timer, if our peer
			 * really sends too rarely.
			 */
			if (m > tp->rto) {
				/* Too long gap. Apparently sender falled to
				 * restart window, so that we send ACKs quickly.
				 */
				tcp_enter_quickack_mode(tp);
			}
		} else {
			if (m <= 0)
				m = TCP_ATO_MIN/2;
			if (m <= tp->ack.ato)
				tp->ack.ato = (tp->ack.ato >> 1) + m;
		}
	}
	tp->ack.lrcvtime = now;
}

/* Called to compute a smoothed rtt estimate. The data fed to this
 * routine either comes from timestamps, or from segments that were
 * known _not_ to have been retransmitted [see Karn/Partridge
 * Proceedings SIGCOMM 87]. The algorithm is from the SIGCOMM 88
 * piece by Van Jacobson.
 * NOTE: the next three routines used to be one big routine.
 * To save cycles in the RFC 1323 implementation it was better to break
 * it up into three procedures. -- erics
 */

static __inline__ void tcp_rtt_estimator(struct tcp_opt *tp, __u32 mrtt)
{
	long m = mrtt; /* RTT */

	/*	The following amusing code comes from Jacobson's
	 *	article in SIGCOMM '88.  Note that rtt and mdev
	 *	are scaled versions of rtt and mean deviation.
	 *	This is designed to be as fast as possible 
	 *	m stands for "measurement".
	 *
	 *	On a 1990 paper the rto value is changed to:
	 *	RTO = rtt + 4 * mdev
	 */
	if(m == 0)
		m = 1;
	if (tp->srtt != 0) {
		m -= (tp->srtt >> 3);	/* m is now error in rtt est */
		tp->srtt += m;		/* rtt = 7/8 rtt + 1/8 new */
		if (m < 0)
			m = -m;		/* m is now abs(error) */
		m -= (tp->mdev >> 2);   /* similar update on mdev */
		tp->mdev += m;	    	/* mdev = 3/4 mdev + 1/4 new */
	} else {
		/* no previous measure. */
		tp->srtt = m<<3;	/* take the measured time to be rtt */
		tp->mdev = m<<2;	/* make sure rto = 3*rtt */
	}
}

/* Calculate rto without backoff.  This is the second half of Van Jacobson's
 * routine referred to above.
 */

static __inline__ void tcp_set_rto(struct tcp_opt *tp)
{
	tp->rto = (tp->srtt >> 3) + tp->mdev;
	/* I am not enough educated to understand this magic.
	 * However, it smells bad. snd_cwnd>31 is common case.
	 */
	tp->rto += (tp->rto >> 2) + (tp->rto >> (tp->snd_cwnd-1));
}
 

/* Keep the rto between HZ/5 and 120*HZ. 120*HZ is the upper bound
 * on packet lifetime in the internet. We need the HZ/5 lower
 * bound to behave correctly against BSD stacks with a fixed
 * delayed ack.
 * FIXME: It's not entirely clear this lower bound is the best
 * way to avoid the problem. Is it possible to drop the lower
 * bound and still avoid trouble with BSD stacks? Perhaps
 * some modification to the RTO calculation that takes delayed
 * ack bias into account? This needs serious thought. -- erics
 */
static __inline__ void tcp_bound_rto(struct tcp_opt *tp)
{
	if (tp->rto < TCP_RTO_MIN)
		tp->rto = TCP_RTO_MIN;
	else if (tp->rto > TCP_RTO_MAX)
		tp->rto = TCP_RTO_MAX;
}

/* Save metrics learned by this TCP session.
   This function is called only, when TCP finishes sucessfully
   i.e. when it enters TIME-WAIT or goes from LAST-ACK to CLOSE.
 */
static void tcp_update_metrics(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct dst_entry *dst = __sk_dst_get(sk);

	dst_confirm(dst);

	if (dst && (dst->flags&DST_HOST)) {
		int m;

		if (tp->backoff || !tp->srtt) {
			/* This session failed to estimate rtt. Why?
			 * Probably, no packets returned in time.
			 * Reset our results.
			 */
			if (!(dst->mxlock&(1<<RTAX_RTT)))
				dst->rtt = 0;
			return;
		}

		m = dst->rtt - tp->srtt;

		/* If newly calculated rtt larger than stored one,
		 * store new one. Otherwise, use EWMA. Remember,
		 * rtt overestimation is always better than underestimation.
		 */
		if (!(dst->mxlock&(1<<RTAX_RTT))) {
			if (m <= 0)
				dst->rtt = tp->srtt;
			else
				dst->rtt -= (m>>3);
		}

		if (!(dst->mxlock&(1<<RTAX_RTTVAR))) {
			if (m < 0)
				m = -m;

			/* Scale deviation to rttvar fixed point */
			m >>= 1;
			if (m < tp->mdev)
				m = tp->mdev;

			if (m >= dst->rttvar)
				dst->rttvar = m;
			else
				dst->rttvar -= (dst->rttvar - m)>>2;
		}

		if (tp->snd_ssthresh == 0x7FFFFFFF) {
			/* Slow start still did not finish. */
			if (dst->ssthresh &&
			    !(dst->mxlock&(1<<RTAX_SSTHRESH)) &&
			    tp->snd_cwnd > dst->ssthresh)
				dst->ssthresh = tp->snd_cwnd;
			if (!(dst->mxlock&(1<<RTAX_CWND)) &&
			    tp->snd_cwnd > dst->cwnd)
				dst->cwnd = tp->snd_cwnd;
		} else if (tp->snd_cwnd >= tp->snd_ssthresh && !tp->high_seq) {
			/* Cong. avoidance phase, cwnd is reliable. */
			if (!(dst->mxlock&(1<<RTAX_SSTHRESH)))
				dst->ssthresh = tp->snd_cwnd;
			if (!(dst->mxlock&(1<<RTAX_CWND)))
				dst->cwnd = (dst->cwnd + tp->snd_cwnd)>>1;
		} else {
			/* Else slow start did not finish, cwnd is non-sense,
			   ssthresh may be also invalid.
			 */
			if (!(dst->mxlock&(1<<RTAX_CWND)))
				dst->cwnd = (dst->cwnd + tp->snd_ssthresh)>>1;
			if (dst->ssthresh &&
			    !(dst->mxlock&(1<<RTAX_SSTHRESH)) &&
			    tp->snd_ssthresh > dst->ssthresh)
				dst->ssthresh = tp->snd_ssthresh;
		}
	}
}

/* Initialize metrics on socket. */

static void tcp_init_metrics(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct dst_entry *dst = __sk_dst_get(sk);

	if (dst == NULL)
		goto reset;

	dst_confirm(dst);

	if (dst->mxlock&(1<<RTAX_CWND))
		tp->snd_cwnd_clamp = dst->cwnd;
	if (dst->ssthresh) {
		tp->snd_ssthresh = dst->ssthresh;
		if (tp->snd_ssthresh > tp->snd_cwnd_clamp)
			tp->snd_ssthresh = tp->snd_cwnd_clamp;
	}

	if (dst->rtt == 0)
		goto reset;

	if (!tp->srtt && dst->rtt < (TCP_TIMEOUT_INIT<<3))
		goto reset;

	/* Initial rtt is determined from SYN,SYN-ACK.
	 * The segment is small and rtt may appear much
	 * less than real one. Use per-dst memory
	 * to make it more realistic.
	 *
	 * A bit of theory. RTT is time passed after "normal" sized packet
	 * is sent until it is ACKed. In normal curcumstances sending small
	 * packets force peer to delay ACKs and calculation is correct too.
	 * The algorithm is adaptive and, provided we follow specs, it
	 * NEVER underestimate RTT. BUT! If peer tries to make some clever
	 * tricks sort of "quick acks" for time long enough to decrease RTT
	 * to low value, and then abruptly stops to do it and starts to delay
	 * ACKs, wait for troubles.
	 */
	if (dst->rtt > tp->srtt)
		tp->srtt = dst->rtt;
	if (dst->rttvar > tp->mdev)
		tp->mdev = dst->rttvar;
	tcp_set_rto(tp);
	tcp_bound_rto(tp);
	if (tp->rto < TCP_TIMEOUT_INIT && !tp->saw_tstamp)
		goto reset;
	tp->snd_cwnd = tcp_init_cwnd(tp);
	return;


reset:
	/* Play conservative. If timestamps are not
	 * supported, TCP will fail to recalculate correct
	 * rtt, if initial rto is too small. FORGET ALL AND RESET!
	 */
	if (!tp->saw_tstamp && tp->srtt) {
		tp->srtt = 0;
		tp->mdev = TCP_TIMEOUT_INIT;
		tp->rto = TCP_TIMEOUT_INIT;
	}
}

/* WARNING: this must not be called if tp->saw_tstamp was false. */
extern __inline__ void
tcp_replace_ts_recent(struct sock *sk, struct tcp_opt *tp, u32 seq)
{
	if (!after(seq, tp->rcv_wup)) {
		/* PAWS bug workaround wrt. ACK frames, the PAWS discard
		 * extra check below makes sure this can only happen
		 * for pure ACK frames.  -DaveM
		 *
		 * Not only, also it occurs for expired timestamps
		 * and RSTs with bad timestamp option. --ANK
		 */

		if((s32)(tp->rcv_tsval - tp->ts_recent) >= 0 ||
		   xtime.tv_sec >= tp->ts_recent_stamp + TCP_PAWS_24DAYS) {
			tp->ts_recent = tp->rcv_tsval;
			tp->ts_recent_stamp = xtime.tv_sec;
		}
	}
}

extern __inline__ int tcp_paws_discard(struct tcp_opt *tp, struct sk_buff *skb)
{
	return ((s32)(tp->rcv_tsval - tp->ts_recent) < 0 &&
		xtime.tv_sec < tp->ts_recent_stamp + TCP_PAWS_24DAYS

		 /* Sorry, PAWS as specified is broken wrt. pure-ACKs -DaveM

		    I cannot see quitely as all the idea behind PAWS
		    is destroyed 8)

		    The problem is only in reordering duplicate ACKs.
		    Hence, we can check this rare case more carefully.

		    1. Check that it is really duplicate ACK (ack==snd_una)
		    2. Give it some small "replay" window (~RTO)

		    We do not know units of foreign ts values, but make conservative
		    assumption that they are >=1ms. It solves problem
		    noted in Dave's mail to tcpimpl and does not harm PAWS. --ANK
		  */
		 && (TCP_SKB_CB(skb)->seq != TCP_SKB_CB(skb)->end_seq ||
		     TCP_SKB_CB(skb)->ack_seq != tp->snd_una ||
		     !skb->h.th->ack ||
		     (s32)(tp->ts_recent - tp->rcv_tsval) > (tp->rto*1024)/HZ));
}


static int __tcp_sequence(struct tcp_opt *tp, u32 seq, u32 end_seq)
{
	u32 end_window = tp->rcv_wup + tp->rcv_wnd;
#ifdef TCP_FORMAL_WINDOW
	u32 rcv_wnd = tcp_receive_window(tp);
#else
	u32 rcv_wnd = tp->rcv_wnd;
#endif

	if (rcv_wnd &&
	    after(end_seq, tp->rcv_nxt) &&
	    before(seq, end_window))
		return 1;
	if (seq != end_window)
		return 0;
	return (seq == end_seq);
}

/* This functions checks to see if the tcp header is actually acceptable. */
extern __inline__ int tcp_sequence(struct tcp_opt *tp, u32 seq, u32 end_seq)
{
#ifdef TCP_FORMAL_WINDOW
	u32 rcv_wnd = tcp_receive_window(tp);
#else
	u32 rcv_wnd = tp->rcv_wnd;
#endif
	if (seq == tp->rcv_nxt)
		return (rcv_wnd || (end_seq == seq));

	return __tcp_sequence(tp, seq, end_seq);
}

/* When we get a reset we do this. */
static void tcp_reset(struct sock *sk)
{
	/* We want the right error as BSD sees it (and indeed as we do). */
	switch (sk->state) {
		case TCP_SYN_SENT:
			sk->err = ECONNREFUSED;
			break;
		case TCP_CLOSE_WAIT:
			sk->err = EPIPE;
			break;
		case TCP_CLOSE:
			return;
		default:
			sk->err = ECONNRESET;
	}

	if (!sk->dead)
		sk->error_report(sk);

	tcp_done(sk);
}

/* This tags the retransmission queue when SACKs arrive. */
static void tcp_sacktag_write_queue(struct sock *sk, struct tcp_sack_block *sp, int nsacks)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int i = nsacks;

	while(i--) {
		struct sk_buff *skb = skb_peek(&sk->write_queue);
		__u32 start_seq = ntohl(sp->start_seq);
		__u32 end_seq = ntohl(sp->end_seq);
		int fack_count = 0;

		while((skb != NULL) &&
		      (skb != tp->send_head) &&
		      (skb != (struct sk_buff *)&sk->write_queue)) {
			/* The retransmission queue is always in order, so
			 * we can short-circuit the walk early.
			 */
			if(after(TCP_SKB_CB(skb)->seq, end_seq))
				break;

			/* We play conservative, we don't allow SACKS to partially
			 * tag a sequence space.
			 */
			fack_count++;
			if(!after(start_seq, TCP_SKB_CB(skb)->seq) &&
			   !before(end_seq, TCP_SKB_CB(skb)->end_seq)) {
				/* If this was a retransmitted frame, account for it. */
				if((TCP_SKB_CB(skb)->sacked & TCPCB_SACKED_RETRANS) &&
				   tp->retrans_out)
					tp->retrans_out--;
				TCP_SKB_CB(skb)->sacked |= TCPCB_SACKED_ACKED;

				/* RULE: All new SACKs will either decrease retrans_out
				 *       or advance fackets_out.
				 */
				if(fack_count > tp->fackets_out)
					tp->fackets_out = fack_count;
			}
			skb = skb->next;
		}
		sp++; /* Move on to the next SACK block. */
	}
}

/* Look for tcp options. Normally only called on SYN and SYNACK packets.
 * But, this can also be called on packets in the established flow when
 * the fast version below fails.
 */
void tcp_parse_options(struct sock *sk, struct tcphdr *th, struct tcp_opt *tp, int no_fancy)
{
	unsigned char *ptr;
	int length=(th->doff*4)-sizeof(struct tcphdr);

	ptr = (unsigned char *)(th + 1);
	tp->saw_tstamp = 0;

	while(length>0) {
	  	int opcode=*ptr++;
		int opsize;

		switch (opcode) {
			case TCPOPT_EOL:
				return;
			case TCPOPT_NOP:	/* Ref: RFC 793 section 3.1 */
				length--;
				continue;
			default:
				opsize=*ptr++;
				if (opsize < 2) /* "silly options" */
					return;
				if (opsize > length)
					break;	/* don't parse partial options */
	  			switch(opcode) {
				case TCPOPT_MSS:
					if(opsize==TCPOLEN_MSS && th->syn) {
						u16 in_mss = ntohs(*(__u16 *)ptr);
						if (in_mss) {
							if (tp->user_mss && tp->user_mss < in_mss)
								in_mss = tp->user_mss;
							tp->mss_clamp = in_mss;
						}
					}
					break;
				case TCPOPT_WINDOW:
					if(opsize==TCPOLEN_WINDOW && th->syn)
						if (!no_fancy && sysctl_tcp_window_scaling) {
							tp->wscale_ok = 1;
							tp->snd_wscale = *(__u8 *)ptr;
							if(tp->snd_wscale > 14) {
								if(net_ratelimit())
									printk("tcp_parse_options: Illegal window "
									       "scaling value %d >14 received.",
									       tp->snd_wscale);
								tp->snd_wscale = 14;
							}
						}
					break;
				case TCPOPT_TIMESTAMP:
					if(opsize==TCPOLEN_TIMESTAMP) {
						if (sysctl_tcp_timestamps && !no_fancy) {
							tp->tstamp_ok = 1;
							tp->saw_tstamp = 1;
							tp->rcv_tsval = ntohl(*(__u32 *)ptr);
							tp->rcv_tsecr = ntohl(*(__u32 *)(ptr+4));
						}
					}
					break;
				case TCPOPT_SACK_PERM:
					if(opsize==TCPOLEN_SACK_PERM && th->syn) {
						if (sysctl_tcp_sack && !no_fancy) {
							tp->sack_ok = 1;
							tp->num_sacks = 0;
						}
					}
					break;

				case TCPOPT_SACK:
					if((opsize >= (TCPOLEN_SACK_BASE + TCPOLEN_SACK_PERBLOCK)) &&
					   sysctl_tcp_sack && (sk != NULL) && !th->syn) {
						int sack_bytes = opsize - TCPOLEN_SACK_BASE;

						if(!(sack_bytes % TCPOLEN_SACK_PERBLOCK)) {
							int num_sacks = sack_bytes >> 3;
							struct tcp_sack_block *sackp;

							sackp = (struct tcp_sack_block *)ptr;
							tcp_sacktag_write_queue(sk, sackp, num_sacks);
						}
					}
	  			};
	  			ptr+=opsize-2;
	  			length-=opsize;
	  	};
	}
}

/* Fast parse options. This hopes to only see timestamps.
 * If it is wrong it falls back on tcp_parse_options().
 */
static __inline__ int tcp_fast_parse_options(struct sock *sk, struct tcphdr *th, struct tcp_opt *tp)
{
	/* If we didn't send out any options ignore them all. */
	if (tp->tcp_header_len == sizeof(struct tcphdr))
		return 0;
	if (th->doff == sizeof(struct tcphdr)>>2) {
		tp->saw_tstamp = 0;
		return 0;
	} else if (th->doff == (sizeof(struct tcphdr)>>2)+(TCPOLEN_TSTAMP_ALIGNED>>2)) {
		__u32 *ptr = (__u32 *)(th + 1);
		if (*ptr == __constant_ntohl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16)
					     | (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP)) {
			tp->saw_tstamp = 1;
			++ptr;
			tp->rcv_tsval = ntohl(*ptr);
			++ptr;
			tp->rcv_tsecr = ntohl(*ptr);
			return 1;
		}
	}
	tcp_parse_options(sk, th, tp, 0);
	return 1;
}

#define FLAG_DATA		0x01 /* Incoming frame contained data.		*/
#define FLAG_WIN_UPDATE		0x02 /* Incoming ACK was a window update.	*/
#define FLAG_DATA_ACKED		0x04 /* This ACK acknowledged new data.		*/
#define FLAG_RETRANS_DATA_ACKED	0x08 /* "" "" some of which was retransmitted.	*/
#define FLAG_SYN_ACKED		0x10 /* This ACK acknowledged new data.		*/

static __inline__ void clear_fast_retransmit(struct tcp_opt *tp)
{
	if (tp->dup_acks > 3)
		tp->snd_cwnd = (tp->snd_ssthresh);

	tp->dup_acks = 0;
}

/* NOTE: This code assumes that tp->dup_acks gets cleared when a
 * retransmit timer fires.
 */
static void tcp_fast_retrans(struct sock *sk, u32 ack, int not_dup)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Note: If not_dup is set this implies we got a
	 * data carrying packet or a window update.
	 * This carries no new information about possible
	 * lost packets, so we have to ignore it for the purposes
	 * of counting duplicate acks. Ideally this does not imply we
	 * should stop our fast retransmit phase, more acks may come
	 * later without data to help us. Unfortunately this would make
	 * the code below much more complex. For now if I see such
	 * a packet I clear the fast retransmit phase.
	 */
	if (ack == tp->snd_una && tp->packets_out && (not_dup == 0)) {
		/* This is the standard reno style fast retransmit branch. */

                /* 1. When the third duplicate ack is received, set ssthresh 
                 * to one half the current congestion window, but no less 
                 * than two segments. Retransmit the missing segment.
                 */
		if (tp->high_seq == 0 || after(ack, tp->high_seq)) {
			tp->dup_acks++;
			if ((tp->fackets_out > 3) || (tp->dup_acks == 3)) {
				__tcp_enter_cong_avoid(tp);
				/* ... and account for 3 ACKs, which are
				 * already received to this time.
				 */
                                tp->snd_cwnd += 3;

				if(!tp->fackets_out)
					tcp_retransmit_skb(sk,
							   skb_peek(&sk->write_queue));
				else
					tcp_fack_retransmit(sk);
                                tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, tp->rto);
			}
		} else if (++tp->dup_acks > 3) {
			/* 2. Each time another duplicate ACK arrives, increment 
			 * cwnd by the segment size. [...] Transmit a packet...
			 *
			 * Packet transmission will be done on normal flow processing
			 * since we're not in "retransmit mode".  We do not use
			 * duplicate ACKs to artificially inflate the congestion
			 * window when doing FACK.
			 */
			if(!tp->fackets_out) {
				tp->snd_cwnd++;
			} else {
				/* Fill any further holes which may have
				 * appeared.
				 *
				 * We may want to change this to run every
				 * further multiple-of-3 dup ack increments,
				 * to be more robust against out-of-order
				 * packet delivery.  -DaveM
				 */
				tcp_fack_retransmit(sk);
			}
		}
	} else if (tp->high_seq != 0) {
		/* In this branch we deal with clearing the Floyd style
		 * block on duplicate fast retransmits, and if requested
		 * we do Hoe style secondary fast retransmits.
		 */
		if (!before(ack, tp->high_seq) || (not_dup & FLAG_DATA) != 0) {
			/* Once we have acked all the packets up to high_seq
			 * we are done this fast retransmit phase.
			 * Alternatively data arrived. In this case we
			 * Have to abort the fast retransmit attempt.
			 * Note that we do want to accept a window
			 * update since this is expected with Hoe's algorithm.
			 */
			clear_fast_retransmit(tp);

			/* After we have cleared up to high_seq we can
			 * clear the Floyd style block.
			 */
			if (!before(ack, tp->high_seq)) {
				tp->high_seq = 0;
				tp->fackets_out = 0;
			}
		} else if (tp->dup_acks >= 3) {
			if (!tp->fackets_out) {
				/* Hoe Style. We didn't ack the whole
				 * window. Take this as a cue that
				 * another packet was lost and retransmit it.
				 * Don't muck with the congestion window here.
				 * Note that we have to be careful not to
				 * act if this was a window update and it
				 * didn't ack new data, since this does
				 * not indicate a packet left the system.
				 * We can test this by just checking
				 * if ack changed from snd_una, since
				 * the only way to get here without advancing
				 * from snd_una is if this was a window update.
				 */
				if (ack != tp->snd_una && before(ack, tp->high_seq)) {
                                	tcp_retransmit_skb(sk,
							   skb_peek(&sk->write_queue));
                                	tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, tp->rto);
				}
			} else {
				/* FACK style, fill any remaining holes in
				 * receiver's queue.
				 */
				tcp_fack_retransmit(sk);
			}
		}
	}
}

/* This is Jacobson's slow start and congestion avoidance. 
 * SIGCOMM '88, p. 328.
 */
static __inline__ void tcp_cong_avoid(struct tcp_opt *tp)
{
        if (tp->snd_cwnd <= tp->snd_ssthresh) {
                /* In "safe" area, increase. */
		if (tp->snd_cwnd < tp->snd_cwnd_clamp)
			tp->snd_cwnd++;
	} else {
                /* In dangerous area, increase slowly.
		 * In theory this is tp->snd_cwnd += 1 / tp->snd_cwnd
		 */
		if (tp->snd_cwnd_cnt >= tp->snd_cwnd) {
			if (tp->snd_cwnd < tp->snd_cwnd_clamp)
				tp->snd_cwnd++;
			tp->snd_cwnd_cnt=0;
		} else
			tp->snd_cwnd_cnt++;
        }
}

/* Remove acknowledged frames from the retransmission queue. */
static int tcp_clean_rtx_queue(struct sock *sk, __u32 ack,
			       __u32 *seq, __u32 *seq_rtt)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb;
	__u32 now = tcp_time_stamp;
	int acked = 0;

	/* If we are retransmitting, and this ACK clears up to
	 * the retransmit head, or further, then clear our state.
	 */
	if (tp->retrans_head != NULL &&
	    !before(ack, TCP_SKB_CB(tp->retrans_head)->end_seq))
		tp->retrans_head = NULL;

	while((skb=skb_peek(&sk->write_queue)) && (skb != tp->send_head)) {
		struct tcp_skb_cb *scb = TCP_SKB_CB(skb); 
		__u8 sacked = scb->sacked;
		
		/* If our packet is before the ack sequence we can
		 * discard it as it's confirmed to have arrived at
		 * the other end.
		 */
		if (after(scb->end_seq, ack))
			break;

		/* Initial outgoing SYN's get put onto the write_queue
		 * just like anything else we transmit.  It is not
		 * true data, and if we misinform our callers that
		 * this ACK acks real data, we will erroneously exit
		 * connection startup slow start one packet too
		 * quickly.  This is severely frowned upon behavior.
		 */
		if((sacked & TCPCB_SACKED_RETRANS) && tp->retrans_out)
			tp->retrans_out--;
		if(!(scb->flags & TCPCB_FLAG_SYN)) {
			acked |= FLAG_DATA_ACKED;
			if(sacked & TCPCB_SACKED_RETRANS)
				acked |= FLAG_RETRANS_DATA_ACKED;
			if(tp->fackets_out)
				tp->fackets_out--;
		} else {
			acked |= FLAG_SYN_ACKED;
			/* This is pure paranoia. */
			tp->retrans_head = NULL;
		}
		tp->packets_out--;
		*seq = scb->seq;
		*seq_rtt = now - scb->when;
		__skb_unlink(skb, skb->list);
		kfree_skb(skb);
	}
	return acked;
}

static void tcp_ack_probe(struct sock *sk, __u32 ack)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	
	/* Was it a usable window open? */

	if (tp->send_head != NULL) {
		if (!after(TCP_SKB_CB(tp->send_head)->end_seq, ack + tp->snd_wnd)) {
			tp->backoff = 0;
			tcp_clear_xmit_timer(sk, TCP_TIME_PROBE0);
			/* If packets_out==0, socket must be waked up by
			 * subsequent tcp_data_snd_check(). This function is
			 * not for random using!
			 */
		} else if (!tp->packets_out) {
			tcp_reset_xmit_timer(sk, TCP_TIME_PROBE0,
					     min(tp->rto << tp->backoff, TCP_RTO_MAX));
		}
	}
}

/* Should we open up the congestion window? */
static __inline__ int should_advance_cwnd(struct tcp_opt *tp, int flag)
{
	/* Data must have been acked. */
	if ((flag & FLAG_DATA_ACKED) == 0)
		return 0;

	/* Some of the data acked was retransmitted somehow? */
	if ((flag & FLAG_RETRANS_DATA_ACKED) != 0) {
		/* We advance in all cases except during
		 * non-FACK fast retransmit/recovery.
		 */
		if (tp->fackets_out != 0 ||
		    tp->retransmits != 0)
			return 1;

		/* Non-FACK fast retransmit does it's own
		 * congestion window management, don't get
		 * in the way.
		 */
		return 0;
	}

	/* New non-retransmitted data acked, always advance.  */
	return 1;
}

/* Read draft-ietf-tcplw-high-performance before mucking
 * with this code. (Superceeds RFC1323)
 */
static void tcp_ack_saw_tstamp(struct sock *sk, struct tcp_opt *tp,
			       u32 seq, u32 ack, int flag)
{
	__u32 seq_rtt;

	/* RTTM Rule: A TSecr value received in a segment is used to
	 * update the averaged RTT measurement only if the segment
	 * acknowledges some new data, i.e., only if it advances the
	 * left edge of the send window.
	 *
	 * See draft-ietf-tcplw-high-performance-00, section 3.3.
	 * 1998/04/10 Andrey V. Savochkin <saw@msu.ru>
	 */
	if (!(flag & (FLAG_DATA_ACKED|FLAG_SYN_ACKED)))
		return;

	seq_rtt = tcp_time_stamp - tp->rcv_tsecr;
	tcp_rtt_estimator(tp, seq_rtt);
	if (tp->retransmits) {
		if (tp->packets_out == 0) {
			tp->retransmits = 0;
			tp->fackets_out = 0;
			tp->retrans_out = 0;
			tp->backoff = 0;
			tcp_set_rto(tp);
		} else {
			/* Still retransmitting, use backoff */
			tcp_set_rto(tp);
			tp->rto = tp->rto << tp->backoff;
		}
	} else {
		tcp_set_rto(tp);
	}

	tcp_bound_rto(tp);
}

static __inline__ void tcp_ack_packets_out(struct sock *sk, struct tcp_opt *tp)
{
	struct sk_buff *skb = skb_peek(&sk->write_queue);

#ifdef TCP_DEBUG
	/* It occured in 2.3, because of racy timers. Namely,
	 * retransmit timer did not check packets_out and retransmitted
	 * send_head sometimes and, hence, messed all the write_queue.
	 * Now it is impossible, I bet. --ANK
	 */
	if (skb == NULL) {
		printk("Sucks! packets_out=%d, sk=%p, %d\n", tp->packets_out, sk, sk->state);
		return;
	}
#endif

	/* Some data was ACK'd, if still retransmitting (due to a
	 * timeout), resend more of the retransmit queue.  The
	 * congestion window is handled properly by that code.
	 */
	if (tp->retransmits) {
		tcp_xmit_retransmit_queue(sk);
		tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, tp->rto);
	} else {
		__u32 when = tp->rto - (tcp_time_stamp - TCP_SKB_CB(skb)->when);
		if ((__s32)when < 0)
			when = 1;
		tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, when);
	}
}

/* This routine deals with incoming acks, but not outgoing ones. */
static int tcp_ack(struct sock *sk, struct tcphdr *th, 
		   u32 ack_seq, u32 ack, int len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int flag = 0;
	u32 seq = 0;
	u32 seq_rtt = 0;

	if(sk->state == TCP_CLOSE)
		return 1;	/* Dead, can't ack any more so why bother */

	/* If the ack is newer than sent or older than previous acks
	 * then we can probably ignore it.
	 */
	if (after(ack, tp->snd_nxt) || before(ack, tp->snd_una))
		goto uninteresting_ack;

	/* If there is data set flag 1 */
	if (len != th->doff*4)
		flag |= FLAG_DATA;

	/* Update our send window. */

	/* This is the window update code as per RFC 793
	 * snd_wl{1,2} are used to prevent unordered
	 * segments from shrinking the window 
	 */
	if (before(tp->snd_wl1, ack_seq) ||
	    (tp->snd_wl1 == ack_seq && !after(tp->snd_wl2, ack))) {
		u32 nwin = ntohs(th->window) << tp->snd_wscale;

		if ((tp->snd_wl2 != ack) || (nwin > tp->snd_wnd)) {
			flag |= FLAG_WIN_UPDATE;
			if (tp->snd_wnd != nwin) {
				tp->snd_wnd = nwin;

				/* Note, it is the only place, where
				 * fast path is recovered for sending TCP.
				 */
				if (skb_queue_len(&tp->out_of_order_queue) == 0 &&
#ifdef TCP_FORMAL_WINDOW
				    tcp_receive_window(tp) &&
#endif
				    !tp->urg_data)
					tcp_fast_path_on(tp);

				if (nwin > tp->max_window) {
					tp->max_window = nwin;
					tcp_sync_mss(sk, tp->pmtu_cookie);
				}
			}

			tp->snd_wl1 = ack_seq;
			tp->snd_wl2 = ack;
		}
	}

	/* BEWARE! From this place and until return from this function
	 * snd_nxt and snd_wnd are out of sync. All the routines, called
	 * from here must get "ack" as argument or they should not depend
	 * on right edge of window. It is _UGLY_. It cries to be fixed. --ANK
	 */

	/* We passed data and got it acked, remove any soft error
	 * log. Something worked...
	 */
	sk->err_soft = 0;
	tp->probes_out = 0;
	tp->rcv_tstamp = tcp_time_stamp;

	/* See if we can take anything off of the retransmit queue. */
	flag |= tcp_clean_rtx_queue(sk, ack, &seq, &seq_rtt);

	/* If this ack opens up a zero window, clear backoff.  It was
	 * being used to time the probes, and is probably far higher than
	 * it needs to be for normal retransmission.
	 */
	if (tcp_timer_is_set(sk, TCP_TIME_PROBE0))
		tcp_ack_probe(sk, ack);

	/* We must do this here, before code below clears out important
	 * state contained in tp->fackets_out and tp->retransmits.  -DaveM
	 */
	if (should_advance_cwnd(tp, flag))
		tcp_cong_avoid(tp);

	/* If we have a timestamp, we always do rtt estimates. */
	if (tp->saw_tstamp) {
		tcp_ack_saw_tstamp(sk, tp, seq, ack, flag);
	} else {
		/* If we were retransmiting don't count rtt estimate. */
		if (tp->retransmits) {
			if (tp->packets_out == 0) {
				tp->retransmits = 0;
				tp->fackets_out = 0;
				tp->retrans_out = 0;
			}
		} else {
			/* We don't have a timestamp. Can only use
			 * packets that are not retransmitted to determine
			 * rtt estimates. Also, we must not reset the
			 * backoff for rto until we get a non-retransmitted
			 * packet. This allows us to deal with a situation
			 * where the network delay has increased suddenly.
			 * I.e. Karn's algorithm. (SIGCOMM '87, p5.)
			 */
			if (flag & (FLAG_DATA_ACKED|FLAG_SYN_ACKED)) {
				if(!(flag & FLAG_RETRANS_DATA_ACKED)) {
					tp->backoff = 0;
					tcp_rtt_estimator(tp, seq_rtt);
					tcp_set_rto(tp);
					tcp_bound_rto(tp);
				}
			}
		}
	}

	if (tp->packets_out) {
		if (flag & FLAG_DATA_ACKED)
			tcp_ack_packets_out(sk, tp);
	} else {
		tcp_clear_xmit_timer(sk, TCP_TIME_RETRANS);
	}

	flag &= (FLAG_DATA | FLAG_WIN_UPDATE);
	if ((ack == tp->snd_una	&& tp->packets_out && flag == 0) ||
	    (tp->high_seq != 0)) {
		tcp_fast_retrans(sk, ack, flag);
	} else {
		/* Clear any aborted fast retransmit starts. */
		tp->dup_acks = 0;
	}
	/* It is not a brain fart, I thought a bit now. 8)
	 *
	 * Forward progress is indicated, if:
	 *   1. the ack acknowledges new data.
	 *   2. or the ack is duplicate, but it is caused by new segment
	 *      arrival. This case is filtered by:
	 *      - it contains no data, syn or fin.
	 *      - it does not update window.
	 *   3. or new SACK. It is difficult to check, so that we ignore it.
	 *
	 * Forward progress is also indicated by arrival new data,
	 * which was caused by window open from our side. This case is more
	 * difficult and it is made (alas, incorrectly) in tcp_data_queue().
	 *                                              --ANK (990513)
	 */
	if (ack != tp->snd_una || (flag == 0 && !th->fin))
		dst_confirm(sk->dst_cache);

	if (ack != tp->snd_una)
		tp->sorry = 1;

	/* Remember the highest ack received. */
	tp->snd_una = ack;
	return 1;

uninteresting_ack:
	SOCK_DEBUG(sk, "Ack ignored %u %u\n", ack, tp->snd_nxt);
	return 0;
}

int tcp_paws_check(struct tcp_opt *tp, int rst)
{
	if ((s32)(tp->rcv_tsval - tp->ts_recent) >= 0)
		return 0;
	if (xtime.tv_sec >= tp->ts_recent_stamp + TCP_PAWS_24DAYS)
		return 0;

	/* RST segments are not recommended to carry timestamp,
	   and, if they do, it is recommended to ignore PAWS because
	   "their cleanup function should take precedence over timestamps."
	   Certainly, it is mistake. It is necessary to understand the reasons
	   of this constraint to relax it: if peer reboots, clock may go
	   out-of-sync and half-open connections will not be reset.
	   Actually, the problem would be not existing if all
	   the implementations followed draft about maintaining clock
	   via reboots. Linux-2.2 DOES NOT!

	   However, we can relax time bounds for RST segments to MSL.
	 */
	if (rst && xtime.tv_sec >= tp->ts_recent_stamp + TCP_PAWS_MSL)
		return 0;
	return 1;
}

static __inline__ int tcp_in_window(u32 seq, u32 end_seq, u32 s_win, u32 e_win)
{
	if (seq == s_win)
		return 1;
	if (after(end_seq, s_win) && before(seq, e_win))
		return 1;
	return (seq == e_win && seq == end_seq);
}

/* New-style handling of TIME_WAIT sockets. */

/* Must be called with locally disabled BHs. */
void tcp_timewait_kill(struct tcp_tw_bucket *tw)
{
	struct tcp_ehash_bucket *ehead;
	struct tcp_bind_hashbucket *bhead;
	struct tcp_bind_bucket *tb;

	/* Unlink from established hashes. */
	ehead = &tcp_ehash[tw->hashent];
	write_lock(&ehead->lock);
	if (!tw->pprev) {
		write_unlock(&ehead->lock);
		return;
	}
	if(tw->next)
		tw->next->pprev = tw->pprev;
	*(tw->pprev) = tw->next;
	tw->pprev = NULL;
	write_unlock(&ehead->lock);

	/* Disassociate with bind bucket. */
	bhead = &tcp_bhash[tcp_bhashfn(tw->num)];
	spin_lock(&bhead->lock);
	if ((tb = tw->tb) != NULL) {
		if(tw->bind_next)
			tw->bind_next->bind_pprev = tw->bind_pprev;
		*(tw->bind_pprev) = tw->bind_next;
		tw->tb = NULL;
		if (tb->owners == NULL) {
			if (tb->next)
				tb->next->pprev = tb->pprev;
			*(tb->pprev) = tb->next;
			kmem_cache_free(tcp_bucket_cachep, tb);
		}
	}
	spin_unlock(&bhead->lock);

#ifdef INET_REFCNT_DEBUG
	if (atomic_read(&tw->refcnt) != 1) {
		printk(KERN_DEBUG "tw_bucket %p refcnt=%d\n", tw, atomic_read(&tw->refcnt));
	}
#endif
	tcp_tw_put(tw);
}

/* 
 * * Main purpose of TIME-WAIT state is to close connection gracefully,
 *   when one of ends sits in LAST-ACK or CLOSING retransmitting FIN
 *   (and, probably, tail of data) and one or more our ACKs are lost.
 * * What is TIME-WAIT timeout? It is associated with maximal packet
 *   lifetime in the internet, which results in wrong conclusion, that
 *   it is set to catch "old duplicate segments" wandering out of their path.
 *   It is not quite correct. This timeout is calculated so that it exceeds
 *   maximal retransmision timeout enough to allow to lose one (or more)
 *   segments sent by peer and our ACKs. This time may be calculated from RTO.
 * * When TIME-WAIT socket receives RST, it means that another end
 *   finally closed and we are allowed to kill TIME-WAIT too.
 * * Second purpose of TIME-WAIT is catching old duplicate segments.
 *   Well, certainly it is pure paranoia, but if we load TIME-WAIT
 *   with this semantics, we MUST NOT kill TIME-WAIT state with RSTs.
 * * If we invented some more clever way to catch duplicates
 *   (f.e. based on PAWS), we could truncate TIME-WAIT to several RTOs.
 *
 * The algorithm below is based on FORMAL INTERPRETATION of RFCs.
 * When you compare it to RFCs, please, read section SEGMENT ARRIVES
 * from the very beginning.
 *
 * NOTE. With recycling (and later with fin-wait-2) TW bucket
 * is _not_ stateless. It means, that strictly speaking we must
 * spinlock it. I do not want! Well, probability of misbehaviour
 * is ridiculously low and, seems, we could use some mb() tricks
 * to avoid misread sequence numbers, states etc.  --ANK
 */
enum tcp_tw_status
tcp_timewait_state_process(struct tcp_tw_bucket *tw, struct sk_buff *skb,
			   struct tcphdr *th, unsigned len)
{
	struct tcp_opt tp;
	int paws_reject = 0;

	tp.saw_tstamp = 0;
	if (th->doff > (sizeof(struct tcphdr)>>2) && tw->ts_recent_stamp) {
		tcp_parse_options(NULL, th, &tp, 0);

		if (tp.saw_tstamp) {
			tp.ts_recent = tw->ts_recent;
			tp.ts_recent_stamp = tw->ts_recent_stamp;
			paws_reject = tcp_paws_check(&tp, th->rst);
		}
	}

	if (tw->substate == TCP_FIN_WAIT2) {
		/* Just repeat all the checks of tcp_rcv_state_process() */

		/* Out of window, send ACK */
		if (paws_reject ||
		    !tcp_in_window(TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq,
				   tw->rcv_nxt, tw->rcv_nxt + tw->rcv_wnd))
			return TCP_TW_ACK;

		if (th->rst)
			goto kill;

		if (th->syn && TCP_SKB_CB(skb)->seq != tw->syn_seq)
			goto kill_with_rst;

		/* Dup ACK? */
		if (!after(TCP_SKB_CB(skb)->end_seq, tw->rcv_nxt) ||
		    TCP_SKB_CB(skb)->end_seq == TCP_SKB_CB(skb)->seq) {
			tcp_tw_put(tw);
			return TCP_TW_SUCCESS;
		}

		/* New data or FIN. If new data arrive after half-duplex close,
		 * reset.
		 */
		if (!th->fin || TCP_SKB_CB(skb)->end_seq != tw->rcv_nxt+1) {
kill_with_rst:
			tcp_tw_deschedule(tw);
			tcp_timewait_kill(tw);
			tcp_tw_put(tw);
			return TCP_TW_RST;
		}

		/* FIN arrived, enter true time-wait state. */
		tw->substate = TCP_TIME_WAIT;
		tw->rcv_nxt = TCP_SKB_CB(skb)->end_seq;
		if (tp.saw_tstamp) {
			tw->ts_recent_stamp = xtime.tv_sec;
			tw->ts_recent = tp.rcv_tsval;
		}

		/* I am shamed, but failed to make it more elegant.
		 * Yes, it is direct reference to IP, which is impossible
		 * to generalize to IPv6. Taking into account that IPv6
		 * do not undertsnad recycling in any case, it not
		 * a big problem in practice. --ANK */
		if (tw->family == AF_INET &&
		    sysctl_tcp_tw_recycle && tw->ts_recent_stamp &&
		    tcp_v4_tw_remember_stamp(tw))
			tcp_tw_schedule(tw, tw->timeout);
		else
			tcp_tw_schedule(tw, TCP_TIMEWAIT_LEN);
		return TCP_TW_ACK;
	}

	/*
	 *	Now real TIME-WAIT state.
	 *
	 *	RFC 1122:
	 *	"When a connection is [...] on TIME-WAIT state [...]
	 *	[a TCP] MAY accept a new SYN from the remote TCP to
	 *	reopen the connection directly, if it:
	 *	
	 *	(1)  assigns its initial sequence number for the new
	 *	connection to be larger than the largest sequence
	 *	number it used on the previous connection incarnation,
	 *	and
	 *
	 *	(2)  returns to TIME-WAIT state if the SYN turns out 
	 *	to be an old duplicate".
	 */

	if (!paws_reject &&
	    (TCP_SKB_CB(skb)->seq == TCP_SKB_CB(skb)->end_seq &&
	     TCP_SKB_CB(skb)->seq == tw->rcv_nxt)) {
		/* In window segment, it may be only reset or bare ack. */

		if (th->rst) {
			/* This is TIME_WAIT assasination, in two flavors.
			 * Oh well... nobody has a sufficient solution to this
			 * protocol bug yet.
			 */
			if (sysctl_tcp_rfc1337 == 0) {
kill:
				tcp_tw_deschedule(tw);
				tcp_timewait_kill(tw);
				tcp_tw_put(tw);
				return TCP_TW_SUCCESS;
			}
		}
		tcp_tw_schedule(tw, TCP_TIMEWAIT_LEN);

		if (tp.saw_tstamp) {
			tw->ts_recent = tp.rcv_tsval;
			tw->ts_recent_stamp = xtime.tv_sec;
		}

		tcp_tw_put(tw);
		return TCP_TW_SUCCESS;
	}

	/* Out of window segment.

	   All the segments are ACKed immediately.

	   The only exception is new SYN. We accept it, if it is
	   not old duplicate and we are not in danger to be killed
	   by delayed old duplicates. RFC check is that it has
	   newer sequence number works at rates <40Mbit/sec.
	   However, if paws works, it is reliable AND even more,
	   we even may relax silly seq space cutoff.

	   RED-PEN: we violate main RFC requirement, if this SYN will appear
	   old duplicate (i.e. we receive RST in reply to SYN-ACK),
	   we must return socket to time-wait state. It is not good,
	   but not fatal yet.
	 */

	if (th->syn && !th->rst && !th->ack && !paws_reject &&
	    (after(TCP_SKB_CB(skb)->seq, tw->rcv_nxt) ||
	     (tp.saw_tstamp && (s32)(tw->ts_recent - tp.rcv_tsval) < 0))) {
		u32 isn = tw->snd_nxt + 2;
		if (isn == 0)
			isn++;
		TCP_SKB_CB(skb)->when = isn;
		return TCP_TW_SYN;
	}

	if (paws_reject)
		NET_INC_STATS_BH(PAWSEstabRejected);

	if(!th->rst) {
		/* In this case we must reset the TIMEWAIT timer.
		 *
		 * If it is ACKless SYN it may be both old duplicate
		 * and new good SYN with random sequence number <rcv_nxt.
		 * Do not reschedule in the last case.
		 */
		if (paws_reject || th->ack)
			tcp_tw_schedule(tw, TCP_TIMEWAIT_LEN);

		/* Send ACK. Note, we do not put the bucket,
		 * it will be released by caller.
		 */
		return TCP_TW_ACK;
	}
	tcp_tw_put(tw);
	return TCP_TW_SUCCESS;
}

/* Enter the time wait state.  This is called with locally disabled BH.
 * Essentially we whip up a timewait bucket, copy the
 * relevant info into it from the SK, and mess with hash chains
 * and list linkage.
 */
static void __tcp_tw_hashdance(struct sock *sk, struct tcp_tw_bucket *tw)
{
	struct tcp_ehash_bucket *ehead = &tcp_ehash[sk->hashent];
	struct tcp_bind_hashbucket *bhead;
	struct sock **head, *sktw;

	write_lock(&ehead->lock);

	/* Step 1: Remove SK from established hash. */
	if (sk->pprev) {
		if(sk->next)
			sk->next->pprev = sk->pprev;
		*sk->pprev = sk->next;
		sk->pprev = NULL;
		sock_prot_dec_use(sk->prot);
	}

	/* Step 2: Hash TW into TIMEWAIT half of established hash table. */
	head = &(ehead + tcp_ehash_size)->chain;
	sktw = (struct sock *)tw;
	if((sktw->next = *head) != NULL)
		(*head)->pprev = &sktw->next;
	*head = sktw;
	sktw->pprev = head;
	atomic_inc(&tw->refcnt);

	write_unlock(&ehead->lock);

	/* Step 3: Put TW into bind hash. Original socket stays there too.
	   Note, that any socket with sk->num!=0 MUST be bound in binding
	   cache, even if it is closed.
	 */
	bhead = &tcp_bhash[tcp_bhashfn(sk->num)];
	spin_lock(&bhead->lock);
	tw->tb = (struct tcp_bind_bucket *)sk->prev;
	BUG_TRAP(sk->prev!=NULL);
	if ((tw->bind_next = tw->tb->owners) != NULL)
		tw->tb->owners->bind_pprev = &tw->bind_next;
	tw->tb->owners = (struct sock*)tw;
	tw->bind_pprev = &tw->tb->owners;
	spin_unlock(&bhead->lock);
}

/* 
 * Move a socket to time-wait or dead fin-wait-2 state.
 */ 
void tcp_time_wait(struct sock *sk, int state, int timeo)
{
	struct tcp_tw_bucket *tw = NULL;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int recycle_ok = 0;

	if (sysctl_tcp_tw_recycle && tp->ts_recent_stamp)
		recycle_ok = tp->af_specific->remember_stamp(sk);

	if (tcp_tw_count < sysctl_tcp_max_tw_buckets)
		tw = kmem_cache_alloc(tcp_timewait_cachep, SLAB_ATOMIC);

	if(tw != NULL) {
		int rto = (tp->rto<<2) - (tp->rto>>1);

		/* Give us an identity. */
		tw->daddr	= sk->daddr;
		tw->rcv_saddr	= sk->rcv_saddr;
		tw->bound_dev_if= sk->bound_dev_if;
		tw->num		= sk->num;
		tw->state	= TCP_TIME_WAIT;
		tw->substate	= state;
		tw->sport	= sk->sport;
		tw->dport	= sk->dport;
		tw->family	= sk->family;
		tw->reuse	= sk->reuse;
		tw->rcv_wscale	= tp->rcv_wscale;
		atomic_set(&tw->refcnt, 0);

		tw->hashent	= sk->hashent;
		tw->rcv_nxt	= tp->rcv_nxt;
		tw->snd_nxt	= tp->snd_nxt;
		tw->rcv_wnd	= tcp_receive_window(tp);
		tw->syn_seq	= tp->syn_seq;
		tw->ts_recent	= tp->ts_recent;
		tw->ts_recent_stamp= tp->ts_recent_stamp;
		tw->pprev_death = NULL;

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		if(tw->family == PF_INET6) {
			memcpy(&tw->v6_daddr,
			       &sk->net_pinfo.af_inet6.daddr,
			       sizeof(struct in6_addr));
			memcpy(&tw->v6_rcv_saddr,
			       &sk->net_pinfo.af_inet6.rcv_saddr,
			       sizeof(struct in6_addr));
		}
#endif
		/* Linkage updates. */
		__tcp_tw_hashdance(sk, tw);

		/* Get the TIME_WAIT timeout firing. */
		if (timeo < rto)
			timeo = rto;

		if (recycle_ok) {
			tw->timeout = rto;
		} else {
			tw->timeout = TCP_TIMEWAIT_LEN;
			if (state == TCP_TIME_WAIT)
				timeo = TCP_TIMEWAIT_LEN;
		}

		tcp_tw_schedule(tw, timeo);
	} else {
		/* Sorry, if we're out of memory, just CLOSE this
		 * socket up.  We've got bigger problems than
		 * non-graceful socket closings.
		 */
		if (net_ratelimit())
			printk(KERN_INFO "TCP: time wait bucket table overflow\n");
	}

	tcp_update_metrics(sk);
	tcp_done(sk);
}

/*
 * 	Process the FIN bit. This now behaves as it is supposed to work
 *	and the FIN takes effect when it is validly part of sequence
 *	space. Not before when we get holes.
 *
 *	If we are ESTABLISHED, a received fin moves us to CLOSE-WAIT
 *	(and thence onto LAST-ACK and finally, CLOSE, we never enter
 *	TIME-WAIT)
 *
 *	If we are in FINWAIT-1, a received FIN indicates simultaneous
 *	close and we go into CLOSING (and later onto TIME-WAIT)
 *
 *	If we are in FINWAIT-2, a received FIN moves us to TIME-WAIT.
 */
 
static void tcp_fin(struct sk_buff *skb, struct sock *sk, struct tcphdr *th)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	tp->fin_seq = TCP_SKB_CB(skb)->end_seq;
	tp->ack.pending = 1;
	tp->ack.quick = 0;

	sk->shutdown |= RCV_SHUTDOWN;

	switch(sk->state) {
		case TCP_SYN_RECV:
		case TCP_ESTABLISHED:
			/* Move to CLOSE_WAIT */
			tcp_set_state(sk, TCP_CLOSE_WAIT);
			break;

		case TCP_CLOSE_WAIT:
		case TCP_CLOSING:
			/* Received a retransmission of the FIN, do
			 * nothing.
			 */
			break;
		case TCP_LAST_ACK:
			/* RFC793: Remain in the LAST-ACK state. */
			break;

		case TCP_FIN_WAIT1:
			/* This case occurs when a simultaneous close
			 * happens, we must ack the received FIN and
			 * enter the CLOSING state.
			 */
			tcp_set_state(sk, TCP_CLOSING);
			break;
		case TCP_FIN_WAIT2:
			/* Received a FIN -- send ACK and enter TIME_WAIT. */
			tcp_send_ack(sk);
			tcp_time_wait(sk, TCP_TIME_WAIT, 0);
			break;
		default:
			/* Only TCP_LISTEN and TCP_CLOSE are left, in these
			 * cases we should never reach this piece of code.
			 */
			printk("tcp_fin: Impossible, sk->state=%d\n", sk->state);
			break;
	};

	/* It _is_ possible, that we have something out-of-order _after_ FIN.
	 * Probably, we should reset in this case. For now drop them.
	 */
	__skb_queue_purge(&tp->out_of_order_queue);
	if (tp->sack_ok)
		tp->num_sacks = 0;

	if (!sk->dead) {
		sk->state_change(sk);

		/* Do not send POLL_HUP for half duplex close. */
		if (sk->shutdown == SHUTDOWN_MASK || sk->state == TCP_CLOSE)
			sk_wake_async(sk, 1, POLL_HUP);
		else
			sk_wake_async(sk, 1, POLL_IN);
	}
}

/* These routines update the SACK block as out-of-order packets arrive or
 * in-order packets close up the sequence space.
 */
static void tcp_sack_maybe_coalesce(struct tcp_opt *tp, struct tcp_sack_block *sp)
{
	int this_sack, num_sacks = tp->num_sacks;
	struct tcp_sack_block *swalk = &tp->selective_acks[0];

	/* If more than one SACK block, see if the recent change to SP eats into
	 * or hits the sequence space of other SACK blocks, if so coalesce.
	 */
	if(num_sacks != 1) {
		for(this_sack = 0; this_sack < num_sacks; this_sack++, swalk++) {
			if(swalk == sp)
				continue;

			/* First case, bottom of SP moves into top of the
			 * sequence space of SWALK.
			 */
			if(between(sp->start_seq, swalk->start_seq, swalk->end_seq)) {
				sp->start_seq = swalk->start_seq;
				goto coalesce;
			}
			/* Second case, top of SP moves into bottom of the
			 * sequence space of SWALK.
			 */
			if(between(sp->end_seq, swalk->start_seq, swalk->end_seq)) {
				sp->end_seq = swalk->end_seq;
				goto coalesce;
			}
		}
	}
	/* SP is the only SACK, or no coalescing cases found. */
	return;

coalesce:
	/* Zap SWALK, by moving every further SACK up by one slot.
	 * Decrease num_sacks.
	 */
	for(; this_sack < num_sacks-1; this_sack++, swalk++) {
		struct tcp_sack_block *next = (swalk + 1);
		swalk->start_seq = next->start_seq;
		swalk->end_seq = next->end_seq;
	}
	tp->num_sacks--;
}

static __inline__ void tcp_sack_swap(struct tcp_sack_block *sack1, struct tcp_sack_block *sack2)
{
	__u32 tmp;

	tmp = sack1->start_seq;
	sack1->start_seq = sack2->start_seq;
	sack2->start_seq = tmp;

	tmp = sack1->end_seq;
	sack1->end_seq = sack2->end_seq;
	sack2->end_seq = tmp;
}

static void tcp_sack_new_ofo_skb(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct tcp_sack_block *sp = &tp->selective_acks[0];
	int cur_sacks = tp->num_sacks;

	if (!cur_sacks)
		goto new_sack;

	/* Optimize for the common case, new ofo frames arrive
	 * "in order". ;-)  This also satisfies the requirements
	 * of RFC2018 about ordering of SACKs.
	 */
	if(sp->end_seq == TCP_SKB_CB(skb)->seq) {
		sp->end_seq = TCP_SKB_CB(skb)->end_seq;
		tcp_sack_maybe_coalesce(tp, sp);
	} else if(sp->start_seq == TCP_SKB_CB(skb)->end_seq) {
		/* Re-ordered arrival, in this case, can be optimized
		 * as well.
		 */
		sp->start_seq = TCP_SKB_CB(skb)->seq;
		tcp_sack_maybe_coalesce(tp, sp);
	} else {
		struct tcp_sack_block *swap = sp + 1;
		int this_sack, max_sacks = (tp->tstamp_ok ? 3 : 4);

		/* Oh well, we have to move things around.
		 * Try to find a SACK we can tack this onto.
		 */

		for(this_sack = 1; this_sack < cur_sacks; this_sack++, swap++) {
			if((swap->end_seq == TCP_SKB_CB(skb)->seq) ||
			   (swap->start_seq == TCP_SKB_CB(skb)->end_seq)) {
				if(swap->end_seq == TCP_SKB_CB(skb)->seq)
					swap->end_seq = TCP_SKB_CB(skb)->end_seq;
				else
					swap->start_seq = TCP_SKB_CB(skb)->seq;
				tcp_sack_swap(sp, swap);
				tcp_sack_maybe_coalesce(tp, sp);
				return;
			}
		}

		/* Could not find an adjacent existing SACK, build a new one,
		 * put it at the front, and shift everyone else down.  We
		 * always know there is at least one SACK present already here.
		 *
		 * If the sack array is full, forget about the last one.
		 */
		if (cur_sacks >= max_sacks) {
			cur_sacks--;
			tp->num_sacks--;
		}
		while(cur_sacks >= 1) {
			struct tcp_sack_block *this = &tp->selective_acks[cur_sacks];
			struct tcp_sack_block *prev = (this - 1);
			this->start_seq = prev->start_seq;
			this->end_seq = prev->end_seq;
			cur_sacks--;
		}

	new_sack:
		/* Build the new head SACK, and we're done. */
		sp->start_seq = TCP_SKB_CB(skb)->seq;
		sp->end_seq = TCP_SKB_CB(skb)->end_seq;
		tp->num_sacks++;
	}
}

static void tcp_sack_remove_skb(struct tcp_opt *tp, struct sk_buff *skb)
{
	struct tcp_sack_block *sp = &tp->selective_acks[0];
	int num_sacks = tp->num_sacks;
	int this_sack;

	/* This is an in order data segment _or_ an out-of-order SKB being
	 * moved to the receive queue, so we know this removed SKB will eat
	 * from the front of a SACK.
	 */
	for(this_sack = 0; this_sack < num_sacks; this_sack++, sp++) {
		/* Check if the start of the sack is covered by skb. */
		if(!before(sp->start_seq, TCP_SKB_CB(skb)->seq) &&
		   before(sp->start_seq, TCP_SKB_CB(skb)->end_seq))
			break;
	}

	/* This should only happen if so many SACKs get built that some get
	 * pushed out before we get here, or we eat some in sequence packets
	 * which are before the first SACK block.
	 */
	if(this_sack >= num_sacks)
		return;

	sp->start_seq = TCP_SKB_CB(skb)->end_seq;
	if(!before(sp->start_seq, sp->end_seq)) {
		/* Zap this SACK, by moving forward any other SACKS. */
		for(this_sack += 1; this_sack < num_sacks; this_sack++, sp++) {
			struct tcp_sack_block *next = (sp + 1);
			sp->start_seq = next->start_seq;
			sp->end_seq = next->end_seq;
		}
		tp->num_sacks--;
	}
}

static void tcp_sack_extend(struct tcp_opt *tp, struct sk_buff *old_skb, struct sk_buff *new_skb)
{
	struct tcp_sack_block *sp = &tp->selective_acks[0];
	int num_sacks = tp->num_sacks;
	int this_sack;

	for(this_sack = 0; this_sack < num_sacks; this_sack++, sp++) {
		if(sp->end_seq == TCP_SKB_CB(old_skb)->end_seq)
			break;
	}
	if(this_sack >= num_sacks)
		return;
	sp->end_seq = TCP_SKB_CB(new_skb)->end_seq;
}


/* This one checks to see if we can put data from the
 * out_of_order queue into the receive_queue.
 */
static void tcp_ofo_queue(struct sock *sk)
{
	struct sk_buff *skb;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	while ((skb = skb_peek(&tp->out_of_order_queue))) {
		if (after(TCP_SKB_CB(skb)->seq, tp->rcv_nxt))
			break;

		if (!after(TCP_SKB_CB(skb)->end_seq, tp->rcv_nxt)) {
			SOCK_DEBUG(sk, "ofo packet was already received \n");
			__skb_unlink(skb, skb->list);
			kfree_skb(skb);
			continue;
		}
		SOCK_DEBUG(sk, "ofo requeuing : rcv_next %X seq %X - %X\n",
			   tp->rcv_nxt, TCP_SKB_CB(skb)->seq,
			   TCP_SKB_CB(skb)->end_seq);

		if(tp->sack_ok)
			tcp_sack_remove_skb(tp, skb);
		__skb_unlink(skb, skb->list);
		__skb_queue_tail(&sk->receive_queue, skb);
		tp->rcv_nxt = TCP_SKB_CB(skb)->end_seq;
		if(skb->h.th->fin)
			tcp_fin(skb, sk, skb->h.th);
	}
}

static void tcp_data_queue(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *skb1;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int eaten = 0;

	/*  Queue data for delivery to the user.
	 *  Packets in sequence go to the receive queue.
	 *  Out of sequence packets to the out_of_order_queue.
	 */
	if (TCP_SKB_CB(skb)->seq == tp->rcv_nxt) {
		/* Ok. In sequence. */
		if (tp->ucopy.task == current &&
		    tp->copied_seq == tp->rcv_nxt &&
		    tp->ucopy.len &&
		    sk->lock.users &&
		    !tp->urg_data) {
			int chunk = min(skb->len, tp->ucopy.len);

			__set_current_state(TASK_RUNNING);

			local_bh_enable();
			if (memcpy_toiovec(tp->ucopy.iov, skb->data, chunk)) {
				sk->err = EFAULT;
				sk->error_report(sk);
			}
			local_bh_disable();
			tp->ucopy.len -= chunk;
			tp->copied_seq += chunk;
			eaten = (chunk == skb->len && !skb->h.th->fin);
		}

		if (!eaten) {
queue_and_out:
			skb_set_owner_r(skb, sk);
			__skb_queue_tail(&sk->receive_queue, skb);
		}
		dst_confirm(sk->dst_cache);
		tp->rcv_nxt = TCP_SKB_CB(skb)->end_seq;
		if(skb->len)
			tcp_event_data_recv(tp, skb); 
		if(skb->h.th->fin)
			tcp_fin(skb, sk, skb->h.th);

		/* This may have eaten into a SACK block. */
		if(tp->sack_ok && tp->num_sacks)
			tcp_sack_remove_skb(tp, skb);
		tcp_ofo_queue(sk);

		/* Turn on fast path. */ 
		if (skb_queue_len(&tp->out_of_order_queue) == 0 &&
#ifdef TCP_FORMAL_WINDOW
		    tcp_receive_window(tp) &&
#endif
		    !tp->urg_data)
			tcp_fast_path_on(tp);

		if (eaten) {
			kfree_skb(skb);
		} else if (!sk->dead)
			sk->data_ready(sk, 0);
		return;
	}

	/* An old packet, either a retransmit or some packet got lost. */
	if (!after(TCP_SKB_CB(skb)->end_seq, tp->rcv_nxt)) {
		/* A retransmit, 2nd most common case.  Force an imediate ack.
		 * 
		 * It is impossible, seq is checked by top level.
		 */
		NETDEBUG(printk("retransmit in tcp_data_queue: seq %X\n", TCP_SKB_CB(skb)->seq));
		tcp_enter_quickack_mode(tp);
		tp->ack.pending = 1;
		kfree_skb(skb);
		return;
	}

	if (before(TCP_SKB_CB(skb)->seq, tp->rcv_nxt)) {
		/* Partial packet, seq < rcv_next < end_seq */
		SOCK_DEBUG(sk, "partial packet: rcv_next %X seq %X - %X\n",
			   tp->rcv_nxt, TCP_SKB_CB(skb)->seq,
			   TCP_SKB_CB(skb)->end_seq);

		goto queue_and_out;
	}

	/* Ok. This is an out_of_order segment, force an ack. */
	tp->ack.pending = 1;

	/* Disable header prediction. */
	tp->pred_flags = 0;


	SOCK_DEBUG(sk, "out of order segment: rcv_next %X seq %X - %X\n",
		   tp->rcv_nxt, TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq);

	skb_set_owner_r(skb, sk);

	if (skb_peek(&tp->out_of_order_queue) == NULL) {
		/* Initial out of order segment, build 1 SACK. */
		if(tp->sack_ok) {
			tp->num_sacks = 1;
			tp->selective_acks[0].start_seq = TCP_SKB_CB(skb)->seq;
			tp->selective_acks[0].end_seq = TCP_SKB_CB(skb)->end_seq;
		}
		__skb_queue_head(&tp->out_of_order_queue,skb);
	} else {
		for(skb1=tp->out_of_order_queue.prev; ; skb1 = skb1->prev) {
			/* Already there. */
			if (TCP_SKB_CB(skb)->seq == TCP_SKB_CB(skb1)->seq) {
				if (skb->len >= skb1->len) {
					if(tp->sack_ok)
						tcp_sack_extend(tp, skb1, skb);
					__skb_append(skb1, skb);
					__skb_unlink(skb1, skb1->list);
					kfree_skb(skb1);
				} else {
					/* A duplicate, smaller than what is in the
					 * out-of-order queue right now, toss it.
					 */
					kfree_skb(skb);
				}
				break;
			}
			
			if (after(TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb1)->seq)) {
				__skb_append(skb1, skb);
				if(tp->sack_ok)
					tcp_sack_new_ofo_skb(sk, skb);
				break;
			}

                        /* See if we've hit the start. If so insert. */
			if (skb1 == skb_peek(&tp->out_of_order_queue)) {
				__skb_queue_head(&tp->out_of_order_queue,skb);
				if(tp->sack_ok)
					tcp_sack_new_ofo_skb(sk, skb);
				break;
			}
		}
	}
	return;
}


/*
 *	This routine handles the data.  If there is room in the buffer,
 *	it will be have already been moved into it.  If there is no
 *	room, then we will just have to discard the packet.
 */

static void tcp_data(struct sk_buff *skb, struct sock *sk, unsigned int len)
{
	struct tcphdr *th;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	th = skb->h.th;
	skb_pull(skb, th->doff*4);
	skb_trim(skb, len - (th->doff*4));

        if (skb->len == 0 && !th->fin)
		goto drop;

	/* 
	 *	If our receive queue has grown past its limits shrink it.
	 *	Make sure to do this before moving rcv_nxt, otherwise
	 *	data might be acked for that we don't have enough room.
	 */
	if (atomic_read(&sk->rmem_alloc) > sk->rcvbuf) { 
		if (prune_queue(sk) < 0) { 
			/* Still not enough room. That can happen when
			 * skb->true_size differs significantly from skb->len.
			 */
			goto drop;
		}
	}

	tcp_data_queue(sk, skb);

	if (before(tp->rcv_nxt, tp->copied_seq)) {
		printk(KERN_DEBUG "*** tcp.c:tcp_data bug acked < copied\n");
		tp->rcv_nxt = tp->copied_seq;
	}
	return;

drop:
	kfree_skb(skb);
}

/* When incoming ACK allowed to free some skb from write_queue,
 * we remember this in flag tp->sorry and wake up socket on the exit
 * from tcp input handler. Probably, handler has already eat this space
 * sending ACK and cloned frames from tcp_write_xmit().
 */
static __inline__ void tcp_new_space(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct socket *sock;

	tp->sorry = 0;

	if (sock_wspace(sk) >= tcp_min_write_space(sk) &&
	    (sock = sk->socket) != NULL) {
		clear_bit(SOCK_NOSPACE, &sock->flags);

		if (sk->sleep && waitqueue_active(sk->sleep))
			wake_up_interruptible(sk->sleep);

		if (sock->fasync_list)
			sock_wake_async(sock, 2, POLL_OUT);
	}
}

static void __tcp_data_snd_check(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	if (after(TCP_SKB_CB(skb)->end_seq, tp->snd_una + tp->snd_wnd) ||
	    tcp_packets_in_flight(tp) >= tp->snd_cwnd ||
	    tcp_write_xmit(sk))
		tcp_check_probe_timer(sk, tp);
}

static __inline__ void tcp_data_snd_check(struct sock *sk)
{
	struct sk_buff *skb = sk->tp_pinfo.af_tcp.send_head;

	if (skb != NULL)
		__tcp_data_snd_check(sk, skb); 
}

/*
 * Check if sending an ack is needed.
 */
static __inline__ void __tcp_ack_snd_check(struct sock *sk, int ofo_possible)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* This also takes care of updating the window.
	 * This if statement needs to be simplified.
	 *
	 * Rules for delaying an ack:
	 *      - delay time <= 0.5 HZ
	 *      - we don't have a window update to send
	 *      - must send at least every 2 full sized packets
	 *	- must send an ACK if we have any out of order data
	 *
	 * With an extra heuristic to handle loss of packet
	 * situations and also helping the sender leave slow
	 * start in an expediant manner.
	 */

	    /* More than one full frame received or... */
	if (((tp->rcv_nxt - tp->rcv_wup) > tp->ack.rcv_mss
#ifdef TCP_MORE_COARSE_ACKS
	     /* Avoid to send immediate ACK from input path, if it
	      * does not advance window far enough. tcp_recvmsg() will do this.
	      */
	     && (!sysctl_tcp_retrans_collapse || __tcp_select_window(sk) >= tp->rcv_wnd)
#endif
	     ) ||
	    /* We ACK each frame or... */
	    tcp_in_quickack_mode(tp) ||
	    /* We have out of order data or */
	    (ofo_possible &&
	     skb_peek(&tp->out_of_order_queue) != NULL)) {
		/* Then ack it now */
		tcp_send_ack(sk);
	} else {
		/* Else, send delayed ack. */
		tcp_send_delayed_ack(sk);
	}
}

static __inline__ void tcp_ack_snd_check(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	if (tp->ack.pending == 0) {
		/* We sent a data segment already. */
		return;
	}
	__tcp_ack_snd_check(sk, 1);
}


/*
 *	This routine is only called when we have urgent data
 *	signalled. Its the 'slow' part of tcp_urg. It could be
 *	moved inline now as tcp_urg is only called from one
 *	place. We handle URGent data wrong. We have to - as
 *	BSD still doesn't use the correction from RFC961.
 *	For 1003.1g we should support a new option TCP_STDURG to permit
 *	either form (or just set the sysctl tcp_stdurg).
 */
 
static void tcp_check_urg(struct sock * sk, struct tcphdr * th)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	u32 ptr = ntohs(th->urg_ptr);

	if (ptr && !sysctl_tcp_stdurg)
		ptr--;
	ptr += ntohl(th->seq);

	/* Ignore urgent data that we've already seen and read. */
	if (after(tp->copied_seq, ptr))
		return;

	/* Do we already have a newer (or duplicate) urgent pointer? */
	if (tp->urg_data && !after(ptr, tp->urg_seq))
		return;

	/* Tell the world about our new urgent pointer. */
	if (sk->proc != 0) {
		if (sk->proc > 0)
			kill_proc(sk->proc, SIGURG, 1);
		else
			kill_pg(-sk->proc, SIGURG, 1);
		sk_wake_async(sk, 3, POLL_PRI);
	}

	/* We may be adding urgent data when the last byte read was
	 * urgent. To do this requires some care. We cannot just ignore
	 * tp->copied_seq since we would read the last urgent byte again
	 * as data, nor can we alter copied_seq until this data arrives
	 * or we break the sematics of SIOCATMARK (and thus sockatmark())
	 */
	if (tp->urg_seq == tp->copied_seq)
		tp->copied_seq++;	/* Move the copied sequence on correctly */
	tp->urg_data = TCP_URG_NOTYET;
	tp->urg_seq = ptr;

	/* Disable header prediction. */
	tp->pred_flags = 0;
}

/* This is the 'fast' part of urgent handling. */
static inline void tcp_urg(struct sock *sk, struct tcphdr *th, unsigned long len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Check if we get a new urgent pointer - normally not. */
	if (th->urg)
		tcp_check_urg(sk,th);

	/* Do we wait for any urgent data? - normally not... */
	if (tp->urg_data == TCP_URG_NOTYET) {
		u32 ptr = tp->urg_seq - ntohl(th->seq) + (th->doff*4);

		/* Is the urgent pointer pointing into this packet? */	 
		if (ptr < len) {
			tp->urg_data = TCP_URG_VALID | *(ptr + (unsigned char *) th);
			if (!sk->dead)
				sk->data_ready(sk,0);
		}
	}
}

/* Clean the out_of_order queue if we can, trying to get
 * the socket within its memory limits again.
 *
 * Return less than zero if we should start dropping frames
 * until the socket owning process reads some of the data
 * to stabilize the situation.
 */
static int prune_queue(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp; 
	struct sk_buff *skb;
	int pruned = 0;

	SOCK_DEBUG(sk, "prune_queue: c=%x\n", tp->copied_seq);

	NET_INC_STATS_BH(PruneCalled);

	/* First, purge the out_of_order queue. */
	skb = __skb_dequeue_tail(&tp->out_of_order_queue);
	if(skb != NULL) {
		/* Free it all. */
		do {
			pruned += skb->len;
			net_statistics[smp_processor_id()*2].OfoPruned += skb->len; 
			kfree_skb(skb);
			skb = __skb_dequeue_tail(&tp->out_of_order_queue);
		} while(skb != NULL);

		/* Reset SACK state.  A conforming SACK implementation will
		 * do the same at a timeout based retransmit.  When a connection
		 * is in a sad state like this, we care only about integrity
		 * of the connection not performance.
		 */
		if(tp->sack_ok)
			tp->num_sacks = 0;
	}
	
	/* If we are really being abused, tell the caller to silently
	 * drop receive data on the floor.  It will get retransmitted
	 * and hopefully then we'll have sufficient space.
	 *
	 * We used to try to purge the in-order packets too, but that
	 * turns out to be deadly and fraught with races.  Consider:
	 *
	 * 1) If we acked the data, we absolutely cannot drop the
	 *    packet.  This data would then never be retransmitted.
	 * 2) It is possible, with a proper sequence of events involving
	 *    delayed acks and backlog queue handling, to have the user
	 *    read the data before it gets acked.  The previous code
	 *    here got this wrong, and it lead to data corruption.
	 * 3) Too much state changes happen when the FIN arrives, so once
	 *    we've seen that we can't remove any in-order data safely.
	 *
	 * The net result is that removing in-order receive data is too
	 * complex for anyones sanity.  So we don't do it anymore.  But
	 * if we are really having our buffer space abused we stop accepting
	 * new receive data.
	 *
	 * 8) The arguments are interesting, but I even cannot imagine
	 * what kind of arguments could force us to drop NICE, ALREADY
	 * RECEIVED DATA only to get one more packet? --ANK
	 *
	 * FIXME: it should recompute SACK state and only remove enough
	 *        buffers to get into bounds again. The current scheme loses
	 *        badly sometimes on links with large RTT, especially when 
	 *        the driver has high overhead per skb.
	 *        (increasing the rcvbuf is not enough because it inflates the
	 *         the window too, disabling flow control effectively) -AK
	 *
	 *	  Mmm... Why not to scale it seprately then? Just replace
	 *	  / WINDOW_ADVERTISE_DIVISOR with >> sk->window_advertise_scale
	 *	  and adjust it dynamically, when TCP window flow control
	 *	  fails?						-ANK
	 */

	tp->ack.quick = 0;

	if(atomic_read(&sk->rmem_alloc) < (sk->rcvbuf << 1))
		return 0;

	NET_INC_STATS_BH(RcvPruned);

	/* Massive buffer overcommit. */
	return -1;
}

static int tcp_copy_to_iovec(struct sock *sk, struct sk_buff *skb, int hlen)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int chunk = skb->len - hlen;
	int err;

	local_bh_enable();
	if (skb->ip_summed==CHECKSUM_UNNECESSARY)
		err = memcpy_toiovec(tp->ucopy.iov, skb->h.raw + hlen, chunk);
	else
		err = copy_and_csum_toiovec(tp->ucopy.iov, skb, hlen);

	if (!err) {
update:
		tp->ucopy.len -= chunk;
		tp->copied_seq += chunk;
		local_bh_disable();
		return 0;
	}

	if (err == -EFAULT) {
		sk->err = EFAULT;
		sk->error_report(sk);
		goto update;
	}

	local_bh_disable();
	return err;
}

static int __tcp_checksum_complete_user(struct sock *sk, struct sk_buff *skb)
{
	int result;

	if (sk->lock.users) {
		local_bh_enable();
		result = __tcp_checksum_complete(skb);
		local_bh_disable();
	} else {
		result = __tcp_checksum_complete(skb);
	}
	return result;
}

static __inline__ int
tcp_checksum_complete_user(struct sock *sk, struct sk_buff *skb)
{
	return skb->ip_summed != CHECKSUM_UNNECESSARY &&
		__tcp_checksum_complete_user(sk, skb);
}

/*
 *	TCP receive function for the ESTABLISHED state. 
 *
 *	It is split into a fast path and a slow path. The fast path is 
 * 	disabled when:
 *	- A zero window was announced from us - zero window probing
 *        is only handled properly in the slow path. 
 *	  [ NOTE: actually, it was made incorrectly and nobody ever noticed
 *	    this! Reason is clear: 1. Correct senders do not send
 *	    to zero window. 2. Even if a sender sends to zero window,
 *	    nothing terrible occurs.
 *
 *	    For now I cleaned this and fast path is really always disabled,
 *	    when window is zero, but I would be more happy to remove these
 *	    checks. Code will be only cleaner and _faster_.    --ANK
 *	
 *	    Later note. I've just found that slow path also accepts
 *	    out of window segments, look at tcp_sequence(). So...
 *	    it is the last argument: I repair all and comment out
 *	    repaired code by TCP_FORMAL_WINDOW.
 *	    [ I remember one rhyme from a chidren's book. (I apologize,
 *	      the trasnlation is not rhymed 8)): people in one (jewish) village
 *	      decided to build sauna, but divided to two parties.
 *	      The first one insisted that battens should not be dubbed,
 *	      another objected that foots will suffer of splinters,
 *	      the first fended that dubbed wet battens are too slippy
 *	      and people will fall and it is much more serious!
 *	      Certaiinly, all they went to rabbi.
 *	      After some thinking, he judged: "Do not be lazy!
 *	      Certainly, dub the battens! But put them by dubbed surface down."
 *          ]
 *        ]
 *
 *	- Out of order segments arrived.
 *	- Urgent data is expected.
 *	- There is no buffer space left
 *	- Unexpected TCP flags/window values/header lengths are received
 *	  (detected by checking the TCP header against pred_flags) 
 *	- Data is sent in both directions. Fast path only supports pure senders
 *	  or pure receivers (this means either the sequence number or the ack
 *	  value must stay constant)
 *	- Unexpected TCP option.
 *
 *	When these conditions are not satisfied it drops into a standard 
 *	receive procedure patterned after RFC793 to handle all cases.
 *	The first three cases are guaranteed by proper pred_flags setting,
 *	the rest is checked inline. Fast processing is turned on in 
 *	tcp_data_queue when everything is OK.
 */
int tcp_rcv_established(struct sock *sk, struct sk_buff *skb,
			struct tcphdr *th, unsigned len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/*
	 *	Header prediction.
	 *	The code losely follows the one in the famous 
	 *	"30 instruction TCP receive" Van Jacobson mail.
	 *	
	 *	Van's trick is to deposit buffers into socket queue 
	 *	on a device interrupt, to call tcp_recv function
	 *	on the receive process context and checksum and copy
	 *	the buffer to user space. smart...
	 *
	 *	Our current scheme is not silly either but we take the 
	 *	extra cost of the net_bh soft interrupt processing...
	 *	We do checksum and copy also but from device to kernel.
	 */

	/* RED-PEN. Using static variables to pass function arguments
	 * cannot be good idea...
	 */
	tp->saw_tstamp = 0;

	/*	pred_flags is 0xS?10 << 16 + snd_wnd
	 *	if header_predition is to be made
	 *	'S' will always be tp->tcp_header_len >> 2
	 *	'?' will be 0 for the fast path, otherwise pred_flags is 0 to
	 *  turn it off	(when there are holes in the receive 
	 *	 space for instance)
	 *	PSH flag is ignored.
	 */

	if ((tcp_flag_word(th) & ~(TCP_RESERVED_BITS|TCP_FLAG_PSH)) == tp->pred_flags &&
		TCP_SKB_CB(skb)->seq == tp->rcv_nxt) {
		int tcp_header_len = tp->tcp_header_len;

		/* Timestamp header prediction: tcp_header_len
		 * is automatically equal to th->doff*4 due to pred_flags
		 * match.
		 */

		/* Check timestamp */
		if (tcp_header_len == sizeof(struct tcphdr) + TCPOLEN_TSTAMP_ALIGNED) {
			__u32 *ptr = (__u32 *)(th + 1);

			/* No? Slow path! */
			if (*ptr != __constant_ntohl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16)
						     | (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP))
				goto slow_path;

			tp->saw_tstamp = 1;
			++ptr; 
			tp->rcv_tsval = ntohl(*ptr);
			++ptr;
			tp->rcv_tsecr = ntohl(*ptr);

			/* If PAWS failed, check it more carefully in slow path */
			if ((s32)(tp->rcv_tsval - tp->ts_recent) < 0)
				goto slow_path;

			/* Predicted packet is in window by definition.
			 * seq == rcv_nxt and rcv_wup <= rcv_nxt.
			 * Hence, check seq<=rcv_wup reduces to:
			 */
			if (tp->rcv_nxt == tp->rcv_wup) {
				tp->ts_recent = tp->rcv_tsval;
				tp->ts_recent_stamp = xtime.tv_sec;
			}
		}

		if (len <= tcp_header_len) {
			/* Bulk data transfer: sender */
			if (len == tcp_header_len) {
				/* We know that such packets are checksummed
				 * on entry.
				 */
				tcp_ack(sk, th, TCP_SKB_CB(skb)->seq,
					TCP_SKB_CB(skb)->ack_seq, len); 
				kfree_skb(skb); 
				tcp_data_snd_check(sk);
				if (tp->sorry)
					tcp_new_space(sk);
				return 0;
			} else { /* Header too small */
				TCP_INC_STATS_BH(TcpInErrs);
				goto discard;
			}
		} else if (TCP_SKB_CB(skb)->ack_seq == tp->snd_una) {
			int eaten = 0;

			if (tp->ucopy.task == current &&
			    tp->copied_seq == tp->rcv_nxt &&
			    len - tcp_header_len <= tp->ucopy.len &&
			    sk->lock.users) {
				eaten = 1;

				NET_INC_STATS_BH(TCPHPHitsToUser);

				__set_current_state(TASK_RUNNING);

				if (tcp_copy_to_iovec(sk, skb, tcp_header_len))
					goto csum_error;

				__skb_pull(skb,tcp_header_len);

				tp->rcv_nxt = TCP_SKB_CB(skb)->end_seq;
			} else {
				if (tcp_checksum_complete_user(sk, skb))
					goto csum_error;

				if (atomic_read(&sk->rmem_alloc) > sk->rcvbuf)
					goto step5;

				NET_INC_STATS_BH(TCPHPHits);

				/* Bulk data transfer: receiver */
				__skb_pull(skb,tcp_header_len);

				/* DO NOT notify forward progress here.
				 * It saves dozen of CPU instructions in fast path. --ANK
				 * And where is it signaled then ? -AK
				 * Nowhere. 8) --ANK
				 */
				__skb_queue_tail(&sk->receive_queue, skb);
				skb_set_owner_r(skb, sk);

				tp->rcv_nxt = TCP_SKB_CB(skb)->end_seq;

				/* FIN bit check is not done since if FIN is set in
				 * this frame, the pred_flags won't match up. -DaveM
				 */
				sk->data_ready(sk, 0);
			}

			tcp_event_data_recv(tp, skb);

#ifdef TCP_MORE_COARSE_ACKS
			if (eaten) {
				if (tcp_in_quickack_mode(tp)) {
					tcp_send_ack(sk);
				} else {
					tcp_send_delayed_ack(sk);
				}
			} else
#endif
			__tcp_ack_snd_check(sk, 0);

			if (eaten)
				kfree_skb(skb);
			return 0;
		}
		/* Packet is in sequence, flags are trivial;
		 * only ACK is strange. Jump to step 5.
		 */
		if (tcp_checksum_complete_user(sk, skb))
			goto csum_error;
		goto step5;
	}

slow_path:
	if (tcp_checksum_complete_user(sk, skb))
		goto csum_error;

	/*
	 * RFC1323: H1. Apply PAWS check first.
	 */
	if (tcp_fast_parse_options(sk, th, tp) && tp->saw_tstamp &&
	    tcp_paws_discard(tp, skb)) {
		if (!th->rst) {
			NET_INC_STATS_BH(PAWSEstabRejected);
			tcp_send_ack(sk);
			goto discard;
		}
		/* Resets are accepted even if PAWS failed.

		   ts_recent update must be made after we are sure
		   that the packet is in window.
		 */
	}

	/*
	 *	Standard slow path.
	 */

	if (!tcp_sequence(tp, TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq)) {
		/* RFC793, page 37: "In all states except SYN-SENT, all reset
		 * (RST) segments are validated by checking their SEQ-fields."
		 * And page 69: "If an incoming segment is not acceptable,
		 * an acknowledgment should be sent in reply (unless the RST bit
		 * is set, if so drop the segment and return)".
		 */
		if (th->rst)
			goto discard;
		if (after(TCP_SKB_CB(skb)->seq, tp->rcv_nxt)) {
			SOCK_DEBUG(sk, "seq:%d end:%d wup:%d wnd:%d\n",
				   TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq,
				   tp->rcv_wup, tp->rcv_wnd);
		}
		tcp_enter_quickack_mode(tp);
		tcp_send_ack(sk);
		NET_INC_STATS_BH(DelayedACKLost);
		goto discard;
	}

	if(th->rst) {
		tcp_reset(sk);
		goto discard;
	}

	if (tp->saw_tstamp) {
		tcp_replace_ts_recent(sk, tp,
				      TCP_SKB_CB(skb)->seq);
	}

	if(th->syn && TCP_SKB_CB(skb)->seq != tp->syn_seq) {
		SOCK_DEBUG(sk, "syn in established state\n");
		TCP_INC_STATS_BH(TcpInErrs);
		tcp_reset(sk);
		return 1;
	}

step5:
	if(th->ack)
		tcp_ack(sk, th, TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->ack_seq, len);
	
	/* Process urgent data. */
	tcp_urg(sk, th, len);

	/* step 7: process the segment text */
	tcp_data(skb, sk, len);

	/* Be careful, tcp_data() may have put this into TIME_WAIT. */
	if(sk->state != TCP_CLOSE) {
		tcp_data_snd_check(sk);
		tcp_ack_snd_check(sk);
		if (tp->sorry)
			tcp_new_space(sk);
	}

	return 0;

csum_error:
	TCP_INC_STATS_BH(TcpInErrs);

discard:
	kfree_skb(skb);
	return 0;
}


/* This is not only more efficient than what we used to do, it eliminates
 * a lot of code duplication between IPv4/IPv6 SYN recv processing. -DaveM
 *
 * Actually, we could lots of memory writes here. tp of listening
 * socket contains all necessary default parameters.
 */
struct sock *tcp_create_openreq_child(struct sock *sk, struct open_request *req, struct sk_buff *skb)
{
	struct sock *newsk = sk_alloc(PF_INET, GFP_ATOMIC, 0);

	if(newsk != NULL) {
		struct tcp_opt *newtp;
#ifdef CONFIG_FILTER
		struct sk_filter *filter;
#endif

		memcpy(newsk, sk, sizeof(*newsk));
		newsk->state = TCP_SYN_RECV;

		/* SANITY */
		newsk->pprev = NULL;
		newsk->prev = NULL;

		/* Clone the TCP header template */
		newsk->dport = req->rmt_port;

		sock_lock_init(newsk);
		bh_lock_sock(newsk);

		atomic_set(&newsk->rmem_alloc, 0);
		skb_queue_head_init(&newsk->receive_queue);
		atomic_set(&newsk->wmem_alloc, 0);
		skb_queue_head_init(&newsk->write_queue);
		atomic_set(&newsk->omem_alloc, 0);

		newsk->done = 0;
		newsk->proc = 0;
		newsk->backlog.head = newsk->backlog.tail = NULL;
		skb_queue_head_init(&newsk->error_queue);
		newsk->write_space = tcp_write_space;
#ifdef CONFIG_FILTER
		if ((filter = newsk->filter) != NULL)
			sk_filter_charge(newsk, filter);
#endif

		/* Now setup tcp_opt */
		newtp = &(newsk->tp_pinfo.af_tcp);
		newtp->pred_flags = 0;
		newtp->rcv_nxt = req->rcv_isn + 1;
		newtp->snd_nxt = req->snt_isn + 1;
		newtp->snd_una = req->snt_isn + 1;
		newtp->snd_sml = req->snt_isn + 1;

		tcp_delack_init(newtp);
		if (skb->len >= 536)
			newtp->ack.last_seg_size = skb->len;

		tcp_prequeue_init(newtp);

		newtp->snd_wl1 = req->rcv_isn;
		newtp->snd_wl2 = req->snt_isn;

		newtp->retransmits = 0;
		newtp->backoff = 0;
		newtp->srtt = 0;
		newtp->mdev = TCP_TIMEOUT_INIT;
		newtp->rto = TCP_TIMEOUT_INIT;

		newtp->packets_out = 0;
		newtp->fackets_out = 0;
		newtp->retrans_out = 0;
		newtp->snd_ssthresh = 0x7fffffff;

		/* So many TCP implementations out there (incorrectly) count the
		 * initial SYN frame in their delayed-ACK and congestion control
		 * algorithms that we must have the following bandaid to talk
		 * efficiently to them.  -DaveM
		 */
		newtp->snd_cwnd = 2;
		newtp->snd_cwnd_cnt = 0;
		newtp->high_seq = 0;

		newtp->dup_acks = 0;
		tcp_init_xmit_timers(newsk);
		skb_queue_head_init(&newtp->out_of_order_queue);
		newtp->send_head = newtp->retrans_head = NULL;
		newtp->rcv_wup = req->rcv_isn + 1;
		newtp->write_seq = req->snt_isn + 1;
		newtp->copied_seq = req->rcv_isn + 1;

		newtp->saw_tstamp = 0;

		newtp->probes_out = 0;
		newtp->num_sacks = 0;
		newtp->syn_seq = req->rcv_isn;
		newtp->fin_seq = req->rcv_isn;
		newtp->urg_data = 0;
		newtp->listen_opt = NULL;
		newtp->accept_queue = newtp->accept_queue_tail = NULL;
		/* Deinitialize syn_wait_lock to trap illegal accesses. */
		memset(&newtp->syn_wait_lock, 0, sizeof(newtp->syn_wait_lock));

		/* Back to base struct sock members. */
		newsk->err = 0;
		newsk->priority = 0;
		atomic_set(&newsk->refcnt, 1);
#ifdef INET_REFCNT_DEBUG
		atomic_inc(&inet_sock_nr);
#endif

		if (newsk->keepopen)
			tcp_reset_keepalive_timer(newsk, keepalive_time_when(newtp));
		newsk->socket = NULL;
		newsk->sleep = NULL;

		newtp->tstamp_ok = req->tstamp_ok;
		if((newtp->sack_ok = req->sack_ok) != 0)
			newtp->num_sacks = 0;
		newtp->window_clamp = req->window_clamp;
		newtp->rcv_wnd = req->rcv_wnd;
		newtp->wscale_ok = req->wscale_ok;
		if (newtp->wscale_ok) {
			newtp->snd_wscale = req->snd_wscale;
			newtp->rcv_wscale = req->rcv_wscale;
		} else {
			newtp->snd_wscale = newtp->rcv_wscale = 0;
			newtp->window_clamp = min(newtp->window_clamp,65535);
		}
		newtp->snd_wnd = ntohs(skb->h.th->window) << newtp->snd_wscale;
		newtp->max_window = newtp->snd_wnd;

		if (newtp->tstamp_ok) {
			newtp->ts_recent = req->ts_recent;
			newtp->ts_recent_stamp = xtime.tv_sec;
			newtp->tcp_header_len = sizeof(struct tcphdr) + TCPOLEN_TSTAMP_ALIGNED;
		} else {
			newtp->ts_recent_stamp = 0;
			newtp->tcp_header_len = sizeof(struct tcphdr);
		}
		newtp->mss_clamp = req->mss;
	}
	return newsk;
}

/* 
 *	Process an incoming packet for SYN_RECV sockets represented
 *	as an open_request.
 */

struct sock *tcp_check_req(struct sock *sk,struct sk_buff *skb,
			   struct open_request *req,
			   struct open_request **prev)
{
	struct tcphdr *th = skb->h.th;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	u32 flg = tcp_flag_word(th) & (TCP_FLAG_RST|TCP_FLAG_SYN|TCP_FLAG_ACK);
	int paws_reject = 0;
	struct tcp_opt ttp;
	struct sock *child;

	ttp.saw_tstamp = 0;
	if (th->doff > (sizeof(struct tcphdr)>>2)) {
		tcp_parse_options(NULL, th, &ttp, 0);

		if (ttp.saw_tstamp) {
			ttp.ts_recent = req->ts_recent;
			/* We do not store true stamp, but it is not required,
			 * it can be estimated (approximately)
			 * from another data.
			 */
			ttp.ts_recent_stamp = xtime.tv_sec - ((TCP_TIMEOUT_INIT/HZ)<<req->retrans);
			paws_reject = tcp_paws_check(&ttp, th->rst);
		}
	}

	/* Check for pure retransmited SYN. */
	if (TCP_SKB_CB(skb)->seq == req->rcv_isn &&
	    flg == TCP_FLAG_SYN &&
	    !paws_reject) {
		/*
		 * RFC793 draws (Incorrectly! It was fixed in RFC1122)
		 * this case on figure 6 and figure 8, but formal
		 * protocol description says NOTHING.
		 * To be more exact, it says that we should send ACK,
		 * because this segment (at least, if it has no data)
		 * is out of window.
		 *
		 *  CONCLUSION: RFC793 (even with RFC1122) DOES NOT
		 *  describe SYN-RECV state. All the description
		 *  is wrong, we cannot believe to it and should
		 *  rely only on common sense and implementation
		 *  experience.
		 *
		 * Enforce "SYN-ACK" according to figure 8, figure 6
		 * of RFC793, fixed by RFC1122.
		 */
		req->class->rtx_syn_ack(sk, req, NULL);
		return NULL;
	}

	/* Further reproduces section "SEGMENT ARRIVES"
	   for state SYN-RECEIVED of RFC793.
	   It is broken, however, it does not work only
	   when SYNs are crossed, which is impossible in our
	   case.

	   But generally, we should (RFC lies!) to accept ACK
	   from SYNACK both here and in tcp_rcv_state_process().
	   tcp_rcv_state_process() does not, hence, we do not too.

	   Note that the case is absolutely generic:
	   we cannot optimize anything here without
	   violating protocol. All the checks must be made
	   before attempt to create socket.
	 */

	/* RFC793: "first check sequence number". */

	if (paws_reject || !tcp_in_window(TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq,
					  req->rcv_isn+1, req->rcv_isn+1+req->rcv_wnd)) {
		/* Out of window: send ACK and drop. */
		if (!(flg & TCP_FLAG_RST))
			req->class->send_ack(skb, req);
		if (paws_reject)
			NET_INC_STATS_BH(PAWSEstabRejected);
		return NULL;
	}

	/* In sequence, PAWS is OK. */

	if (ttp.saw_tstamp && !after(TCP_SKB_CB(skb)->seq, req->rcv_isn+1))
		req->ts_recent = ttp.rcv_tsval;

	if (TCP_SKB_CB(skb)->seq == req->rcv_isn) {
		/* Truncate SYN, it is out of window starting
		   at req->rcv_isn+1. */
		flg &= ~TCP_FLAG_SYN;
	}

	/* RFC793: "second check the RST bit" and
	 *	   "fourth, check the SYN bit"
	 */
	if (flg & (TCP_FLAG_RST|TCP_FLAG_SYN))
		goto embryonic_reset;

	/* RFC793: "fifth check the ACK field" */

	if (!(flg & TCP_FLAG_ACK))
		return NULL;

	/* Invalid ACK: reset will be sent by listening socket */
	if (TCP_SKB_CB(skb)->ack_seq != req->snt_isn+1)
		return sk;
	/* Also, it would be not so bad idea to check rcv_tsecr, which
	 * is essentially ACK extension and too early or too late values
	 * should cause reset in unsynchronized states.
	 */

	/* If TCP_DEFER_ACCEPT is set, drop bare ACK. */
	if (tp->defer_accept && TCP_SKB_CB(skb)->end_seq == req->rcv_isn+1) {
		req->acked = 1;
		return NULL;
	}

	/* OK, ACK is valid, create big socket and
	 * feed this segment to it. It will repeat all
	 * the tests. THIS SEGMENT MUST MOVE SOCKET TO
	 * ESTABLISHED STATE. If it will be dropped after
	 * socket is created, wait for troubles.
	 */
	child = tp->af_specific->syn_recv_sock(sk, skb, req, NULL);
	if (child == NULL)
		goto listen_overflow;

	tcp_synq_unlink(tp, req, prev);
	tcp_synq_removed(sk, req);

	tcp_acceptq_queue(sk, req, child);
	return child;

listen_overflow:
	if (!sysctl_tcp_abort_on_overflow) {
		req->acked = 1;
		return NULL;
	}

embryonic_reset:
	NET_INC_STATS_BH(EmbryonicRsts);
	if (!(flg & TCP_FLAG_RST))
		req->class->send_reset(skb);

	tcp_synq_drop(sk, req, prev);
	return NULL;
}

/*
 * Queue segment on the new socket if the new socket is active,
 * otherwise we just shortcircuit this and continue with
 * the new socket.
 */

int tcp_child_process(struct sock *parent, struct sock *child,
		      struct sk_buff *skb)
{
	int ret = 0;
	int state = child->state;

	if (child->lock.users == 0) {
		ret = tcp_rcv_state_process(child, skb, skb->h.th, skb->len);

		/* Wakeup parent, send SIGIO */
		if (state == TCP_SYN_RECV && child->state != state)
			parent->data_ready(parent, 0);
	} else {
		/* Alas, it is possible again, because we do lookup
		 * in main socket hash table and lock on listening
		 * socket does not protect us more.
		 */
		sk_add_backlog(child, skb);
	}

	bh_unlock_sock(child);
	return ret;
}

static int tcp_rcv_synsent_state_process(struct sock *sk, struct sk_buff *skb,
					 struct tcphdr *th, unsigned len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	tcp_parse_options(sk, th, tp, 0);

	if (th->ack) {
		/* rfc793:
		 * "If the state is SYN-SENT then
		 *    first check the ACK bit
		 *      If the ACK bit is set
		 *	  If SEG.ACK =< ISS, or SEG.ACK > SND.NXT, send
		 *        a reset (unless the RST bit is set, if so drop
		 *        the segment and return)"
		 *
		 *  I cite this place to emphasize one essential
		 *  detail, this check is different of one
		 *  in established state: SND.UNA <= SEG.ACK <= SND.NXT.
		 *  SEG_ACK == SND.UNA == ISS is invalid in SYN-SENT,
		 *  because we have no previous data sent before SYN.
		 *                                        --ANK(990513)
		 *
		 *  We do not send data with SYN, so that RFC-correct
		 *  test reduces to:
		 */
		if (TCP_SKB_CB(skb)->ack_seq != tp->snd_nxt)
			return 1;

		/* Check not from any RFC, but it is evident consequence
		 * of combining PAWS and usual SYN-SENT logic: ACK _is_
		 * checked in SYN-SENT unlike another states, hence
		 * echoed tstamp must be checked too.
		 */
		if (tp->saw_tstamp) {
			if (tp->rcv_tsecr == 0) {
				/* Workaround for bug in linux-2.1 and early
				 * 2.2 kernels. Let's pretend that we did not
				 * see such timestamp to avoid bogus rtt value,
				 * calculated by tcp_ack().
				 */
				tp->saw_tstamp = 0;

				/* But do not forget to store peer's timestamp! */
				if (th->syn) {
					tp->ts_recent = tp->rcv_tsval;
					tp->ts_recent_stamp = xtime.tv_sec;
				}
			} else if ((__s32)(tp->rcv_tsecr - tcp_time_stamp) > 0 ||
				   (__s32)(tp->rcv_tsecr - tp->syn_stamp) < 0) {
				NETDEBUG(if (net_ratelimit()) printk(KERN_DEBUG "TCP: synsent reject.\n"));
				NET_INC_STATS_BH(PAWSActiveRejected);
				return 1;
			}
		}

		/* Now ACK is acceptable.
		 *
		 * "If the RST bit is set
		 *    If the ACK was acceptable then signal the user "error:
		 *    connection reset", drop the segment, enter CLOSED state,
		 *    delete TCB, and return."
		 */

		if (th->rst) {
			tcp_reset(sk);
			goto discard;
		}

		/* rfc793:
		 *   "fifth, if neither of the SYN or RST bits is set then
		 *    drop the segment and return."
		 *
		 *    See note below!
		 *                                        --ANK(990513)
		 */
		if (!th->syn)
			goto discard;

		/* rfc793:
		 *   "If the SYN bit is on ...
		 *    are acceptable then ...
		 *    (our SYN has been ACKed), change the connection
		 *    state to ESTABLISHED..."
		 *
		 * Do you see? SYN-less ACKs in SYN-SENT state are
		 * completely ignored.
		 *
		 * The bug causing stalled SYN-SENT sockets
		 * was here: tcp_ack advanced snd_una and canceled
		 * retransmit timer, so that bare ACK received
		 * in SYN-SENT state (even with invalid ack==ISS,
		 * because tcp_ack check is too weak for SYN-SENT)
		 * causes moving socket to invalid semi-SYN-SENT,
		 * semi-ESTABLISHED state and connection hangs.
		 *                                     --ANK (990514)
		 *
		 * Bare ACK is valid, however.
		 * Actually, RFC793 requires to send such ACK
		 * in reply to any out of window packet.
		 * It is wrong, but Linux also send such
		 * useless ACKs sometimes.
		 *                                     --ANK (990724)
		 */

		tp->snd_wl1 = TCP_SKB_CB(skb)->seq;
		tcp_ack(sk,th, TCP_SKB_CB(skb)->seq,
			TCP_SKB_CB(skb)->ack_seq, len);

		/* Ok.. it's good. Set up sequence numbers and
		 * move to established.
		 */
		tp->rcv_nxt = TCP_SKB_CB(skb)->seq+1;
		tp->rcv_wup = TCP_SKB_CB(skb)->seq+1;

		/* RFC1323: The window in SYN & SYN/ACK segments is
		 * never scaled.
		 */
		tp->snd_wnd = ntohs(th->window);
		tp->snd_wl1 = TCP_SKB_CB(skb)->seq;
		tp->snd_wl2 = TCP_SKB_CB(skb)->ack_seq;
		tp->fin_seq = TCP_SKB_CB(skb)->seq;

		tcp_set_state(sk, TCP_ESTABLISHED);

		if (tp->wscale_ok == 0) {
			tp->snd_wscale = tp->rcv_wscale = 0;
			tp->window_clamp = min(tp->window_clamp,65535);
		}

		if (tp->tstamp_ok) {
			tp->tcp_header_len =
				sizeof(struct tcphdr) + TCPOLEN_TSTAMP_ALIGNED;
		} else
			tp->tcp_header_len = sizeof(struct tcphdr);
		if (tp->saw_tstamp) {
			tp->ts_recent = tp->rcv_tsval;
			tp->ts_recent_stamp = xtime.tv_sec;
		}
		tcp_sync_mss(sk, tp->pmtu_cookie);
		tcp_initialize_rcv_mss(sk);
		tcp_init_metrics(sk);
		tcp_init_buffer_space(sk);

		if (sk->keepopen)
			tcp_reset_keepalive_timer(sk, keepalive_time_when(tp));

		tp->copied_seq = tp->rcv_nxt;
		__tcp_fast_path_on(tp, tp->snd_wnd);

		if(!sk->dead) {
			sk->state_change(sk);
			sk_wake_async(sk, 0, POLL_OUT);
		}

		if (tp->write_pending) {
			/* Save one ACK. Data will be ready after
			 * several ticks, if write_pending is set.
			 *
			 * It may be deleted, but with this feature tcpdumps
			 * look so _wonderfully_ clever, that I was not able
			 * to stand against the temptation 8)     --ANK
			 */
			tp->ack.pending = 1;
			tp->ack.lrcvtime = tcp_time_stamp;
			tcp_enter_quickack_mode(tp);
			tp->ack.ato = TCP_ATO_MIN;
			tcp_reset_xmit_timer(sk, TCP_TIME_DACK, TCP_DELACK_MIN);
			goto discard;
		} else {
			tcp_send_ack(sk);
		}
		return -1;
	}

	/* No ACK in the segment */

	if (th->rst) {
		/* rfc793:
		 * "If the RST bit is set
		 *
		 *      Otherwise (no ACK) drop the segment and return."
		 */

		goto discard;
	}

	/* PAWS check. */
	if (tp->ts_recent_stamp && tp->saw_tstamp && tcp_paws_check(tp, 0))
		goto discard;

	if (th->syn) {
		/* We see SYN without ACK. It is attempt of
		 *  simultaneous connect with crossed SYNs.
		 *
		 * The previous version of the code
		 * checked for "connecting to self"
		 * here. that check is done now in
		 * tcp_connect.
		 *
		 * RED-PEN: BTW, it does not. 8)
		 */
		tcp_set_state(sk, TCP_SYN_RECV);
		if (tp->saw_tstamp) {
			tp->ts_recent = tp->rcv_tsval;
			tp->ts_recent_stamp = xtime.tv_sec;
		}

		tp->rcv_nxt = TCP_SKB_CB(skb)->seq + 1;
		tp->rcv_wup = TCP_SKB_CB(skb)->seq + 1;

		/* RFC1323: The window in SYN & SYN/ACK segments is
		 * never scaled.
		 */
		tp->snd_wnd = ntohs(th->window);
		tp->snd_wl1 = TCP_SKB_CB(skb)->seq;
		tp->max_window = tp->snd_wnd;

		tcp_sync_mss(sk, tp->pmtu_cookie);
		tcp_initialize_rcv_mss(sk);

		tcp_send_synack(sk);
#if 0
		/* Note, we could accept data and URG from this segment.
		 * There are no obstacles to make this.
		 *
		 * However, if we ignore data in ACKless segments sometimes,
		 * we have no reasons to accept it sometimes.
		 * Also, seems the code doing it in step6 of tcp_rcv_state_process
		 * is not flawless. So, discard packet for sanity.
		 * Uncomment this return to process the data.
		 */
		return -1;
#endif
	}
	/* "fifth, if neither of the SYN or RST bits is set then
	 * drop the segment and return."
	 */

discard:
	kfree_skb(skb);
	return 0;
}


/*
 *	This function implements the receiving procedure of RFC 793 for
 *	all states except ESTABLISHED and TIME_WAIT. 
 *	It's called from both tcp_v4_rcv and tcp_v6_rcv and should be
 *	address independent.
 */
	
int tcp_rcv_state_process(struct sock *sk, struct sk_buff *skb,
			  struct tcphdr *th, unsigned len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int queued = 0;

	tp->saw_tstamp = 0;

	switch (sk->state) {
	case TCP_CLOSE:
		/* When state == CLOSED, hash lookup always fails.
		 *
		 * But, there is a back door, the backlog queue.
		 * If we have a sequence of packets in the backlog
		 * during __release_sock() which have a sequence such
		 * that:
		 *	packet X	causes entry to TCP_CLOSE state
		 *	...
		 *	packet X + N	has FIN bit set
		 *
		 * We report a (luckily) harmless error in this case.
		 * The issue is that backlog queue processing bypasses
		 * any hash lookups (we know which socket packets are for).
		 * The correct behavior here is what 2.0.x did, since
		 * a TCP_CLOSE socket does not exist.  Drop the frame
		 * and send a RST back to the other end.
		 */

		/* 1. The socket may be moved to TIME-WAIT state.
		   2. While this socket was locked, another socket
		      with the same identity could be created.
		   3. To continue?

		   CONCLUSION: discard and only discard!

		   Alternative would be relookup and recurse into tcp_v?_rcv
		   (not *_do_rcv) to work with timewait and listen states
		   correctly.
		 */
		goto discard;

	case TCP_LISTEN:
		if(th->ack)
			return 1;

		if(th->syn) {
			if(tp->af_specific->conn_request(sk, skb) < 0)
				return 1;

			/* Now we have several options: In theory there is 
			 * nothing else in the frame. KA9Q has an option to 
			 * send data with the syn, BSD accepts data with the
			 * syn up to the [to be] advertised window and 
			 * Solaris 2.1 gives you a protocol error. For now 
			 * we just ignore it, that fits the spec precisely 
			 * and avoids incompatibilities. It would be nice in
			 * future to drop through and process the data.
			 *
			 * Now that TTCP is starting to be used we ought to 
			 * queue this data.
			 * But, this leaves one open to an easy denial of
		 	 * service attack, and SYN cookies can't defend
			 * against this problem. So, we drop the data
			 * in the interest of security over speed.
			 */
			goto discard;
		}
		goto discard;

	case TCP_SYN_SENT:
		queued = tcp_rcv_synsent_state_process(sk, skb, th, len);
		if (queued >= 0)
			return queued;
		queued = 0;
		goto step6;
	}

	/*   Parse the tcp_options present on this header.
	 *   By this point we really only expect timestamps.
	 *   Note that this really has to be here and not later for PAWS
	 *   (RFC1323) to work.
	 */
	if (tcp_fast_parse_options(sk, th, tp) && tp->saw_tstamp &&
	    tcp_paws_discard(tp, skb)) {
		if (!th->rst) {
			tcp_send_ack(sk);
			goto discard;
		}
		/* Reset is accepted even if it did not pass PAWS. */
	}

	/* The silly FIN test here is necessary to see an advancing ACK in
	 * retransmitted FIN frames properly.  Consider the following sequence:
	 *
	 *	host1 --> host2		FIN XSEQ:XSEQ(0) ack YSEQ
	 *	host2 --> host1		FIN YSEQ:YSEQ(0) ack XSEQ
	 *	host1 --> host2		XSEQ:XSEQ(0) ack YSEQ+1
	 *	host2 --> host1		FIN YSEQ:YSEQ(0) ack XSEQ+1	(fails tcp_sequence test)
	 *
	 * At this point the connection will deadlock with host1 believing
	 * that his FIN is never ACK'd, and thus it will retransmit it's FIN
	 * forever.  The following fix is from Taral (taral@taral.net).
	 *
	 * RED-PEN. Seems, the above is not true.
	 * If at least one end is RFC compliant, it will send ACK to
	 * out of window FIN and, hence, move peer to TIME-WAIT.
	 * I comment out this line. --ANK
	 *
	 * RED-PEN. DANGER! tcp_sequence check rejects also SYN-ACKs
	 * received in SYN-RECV. The problem is that description of
	 * segment processing in SYN-RECV state in RFC792 is WRONG.
	 * Correct check would accept ACK from this SYN-ACK, see
	 * figures 6 and 8 (fixed by RFC1122). Compare this
	 * to problem with FIN, they smell similarly. --ANK
	 */

	/* step 1: check sequence number */
	if (!tcp_sequence(tp, TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq)
#if 0
	    && !(th->fin && TCP_SKB_CB(skb)->end_seq == tp->rcv_nxt)
#endif
	    ) {
		if (!th->rst) {
			NET_INC_STATS_BH(DelayedACKLost);
			tcp_enter_quickack_mode(tp);
			tcp_send_ack(sk);
		}
		goto discard;
	}

	/* step 2: check RST bit */
	if(th->rst) {
		tcp_reset(sk);
		goto discard;
	}

	if (tp->saw_tstamp) {
		tcp_replace_ts_recent(sk, tp,
				      TCP_SKB_CB(skb)->seq);
	}

	/* step 3: check security and precedence [ignored] */

	/*	step 4:
	 *
	 *	Check for a SYN, and ensure it matches the SYN we were
	 *	first sent. We have to handle the rather unusual (but valid)
	 *	sequence that KA9Q derived products may generate of
	 *
	 *	SYN
	 *				SYN|ACK Data
	 *	ACK	(lost)
	 *				SYN|ACK Data + More Data
	 *	.. we must ACK not RST...
	 *
	 *	We keep syn_seq as the sequence space occupied by the 
	 *	original syn. 
	 */

	if (th->syn && TCP_SKB_CB(skb)->seq != tp->syn_seq) {
		tcp_reset(sk);
		return 1;
	}

	/* step 5: check the ACK field */
	if (th->ack) {
		int acceptable = tcp_ack(sk, th, TCP_SKB_CB(skb)->seq,
					 TCP_SKB_CB(skb)->ack_seq, len);

		switch(sk->state) {
		case TCP_SYN_RECV:
			if (acceptable) {
				tcp_set_state(sk, TCP_ESTABLISHED);
				tp->copied_seq = tp->rcv_nxt;

				/* Note, that this wakeup is only for marginal
				 * crossed SYN case. Passively open sockets
				 * are not waked up, because sk->sleep == NULL
				 * and sk->socket == NULL.
				 */
				if (!sk->dead) {
					sk->state_change(sk);
					sk_wake_async(sk,0,POLL_OUT);
				}

				tp->snd_una = TCP_SKB_CB(skb)->ack_seq;
				tp->snd_wnd = ntohs(th->window) << tp->snd_wscale;
				tp->snd_wl1 = TCP_SKB_CB(skb)->seq;
				tp->snd_wl2 = TCP_SKB_CB(skb)->ack_seq;

				/* tcp_ack considers this ACK as duplicate
				 * and does not calculate rtt.
				 * Fix it at least with timestamps.
				 */
				if (tp->saw_tstamp && !tp->srtt)
					tcp_ack_saw_tstamp(sk, tp, 0, 0, FLAG_SYN_ACKED);

				tcp_init_metrics(sk);
				tcp_fast_path_on(tp);
			} else {
				SOCK_DEBUG(sk, "bad ack\n");
				return 1;
			}
			break;

		case TCP_FIN_WAIT1:
			if (tp->snd_una == tp->write_seq) {
				tcp_set_state(sk, TCP_FIN_WAIT2);
				sk->shutdown |= SEND_SHUTDOWN;
				dst_confirm(sk->dst_cache);

				if (!sk->dead) {
					/* Wake up lingering close() */
					sk->state_change(sk);
				} else {
					int tmo;

					if (tp->linger2 < 0 ||
					    (TCP_SKB_CB(skb)->end_seq != TCP_SKB_CB(skb)->seq &&
					     after(TCP_SKB_CB(skb)->end_seq - th->fin, tp->rcv_nxt))) {
						tcp_done(sk);
						return 1;
					}

					tmo = tcp_fin_time(tp);
					if (tmo > TCP_TIMEWAIT_LEN) {
						tcp_reset_keepalive_timer(sk, tmo - TCP_TIMEWAIT_LEN);
					} else if (th->fin || sk->lock.users) {
						/* Bad case. We could lose such FIN otherwise.
						 * It is not a big problem, but it looks confusing
						 * and not so rare event. We still can lose it now,
						 * if it spins in bh_lock_sock(), but it is really
						 * marginal case.
						 */
						tcp_reset_keepalive_timer(sk, tmo);
					} else {
						tcp_time_wait(sk, TCP_FIN_WAIT2, tmo);
						goto discard;
					}
				}
			}
			break;

		case TCP_CLOSING:
			if (tp->snd_una == tp->write_seq) {
				tcp_time_wait(sk, TCP_TIME_WAIT, 0);
				goto discard;
			}
			break;

		case TCP_LAST_ACK:
			if (tp->snd_una == tp->write_seq) {
				tcp_update_metrics(sk);
				tcp_done(sk);
				goto discard;
			}
			break;
		}
	} else
		goto discard;

step6:
	/* step 6: check the URG bit */
	tcp_urg(sk, th, len);

	/* step 7: process the segment text */
	switch (sk->state) {
	case TCP_CLOSE_WAIT:
	case TCP_CLOSING:
		if (!before(TCP_SKB_CB(skb)->seq, tp->fin_seq))
			break;
	case TCP_FIN_WAIT1:
	case TCP_FIN_WAIT2:
		/* RFC 793 says to queue data in these states,
		 * RFC 1122 says we MUST send a reset. 
		 * BSD 4.4 also does reset.
		 */
		if (sk->shutdown & RCV_SHUTDOWN) {
			if (TCP_SKB_CB(skb)->end_seq != TCP_SKB_CB(skb)->seq &&
			    after(TCP_SKB_CB(skb)->end_seq - th->fin, tp->rcv_nxt)) {
				tcp_reset(sk);
				return 1;
			}
		}
		/* Fall through */
	case TCP_ESTABLISHED: 
		tcp_data(skb, sk, len);
		queued = 1;
		break;
	}

	/* tcp_data could move socket to TIME-WAIT */
	if (sk->state != TCP_CLOSE) {
		tcp_data_snd_check(sk);
		tcp_ack_snd_check(sk);
		if (tp->sorry)
			tcp_new_space(sk);
	}

	if (!queued) { 
discard:
		kfree_skb(skb);
	}
	return 0;
}
