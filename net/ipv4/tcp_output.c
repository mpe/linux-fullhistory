/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_output.c,v 1.120 2000/01/31 01:21:22 davem Exp $
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
 * Changes:	Pedro Roque	:	Retransmit queue handled by TCP.
 *				:	Fragmentation on mtu decrease
 *				:	Segment collapse on retransmit
 *				:	AF independence
 *
 *		Linus Torvalds	:	send_delayed_ack
 *		David S. Miller	:	Charge memory using the right skb
 *					during syn/ack processing.
 *		David S. Miller :	Output engine completely rewritten.
 *		Andrea Arcangeli:	SYNACK carry ts_recent in tsecr.
 *		Cacophonix Gaul :	draft-minshall-nagle-01
 *
 */

#include <net/tcp.h>

#include <linux/smp_lock.h>

/* People can turn this off for buggy TCP's found in printers etc. */
int sysctl_tcp_retrans_collapse = 1;

static __inline__ void update_send_head(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	tp->send_head = tp->send_head->next;
	if (tp->send_head == (struct sk_buff *) &sk->write_queue)
		tp->send_head = NULL;
}

/* Calculate mss to advertise in SYN segment.
 * RFC1122, RFC1063, draft-ietf-tcpimpl-pmtud-01 state that:
 *
 * 1. It is independent of path mtu.
 * 2. Ideally, it is maximal possible segment size i.e. 65535-40.
 * 3. For IPv4 it is reasonable to calculate it from maximal MTU of
 *    attached devices, because some buggy hosts are confused by
 *    large MSS.
 * 4. We do not make 3, we advertise MSS, calculated from first
 *    hop device mtu, but allow to raise it to ip_rt_min_advmss.
 *    This may be overriden via information stored in routing table.
 * 5. Value 65535 for MSS is valid in IPv6 and means "as large as possible,
 *    probably even Jumbo".
 */
static __u16 tcp_advertise_mss(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct dst_entry *dst = __sk_dst_get(sk);
	int mss = tp->advmss;

	if (dst && dst->advmss < mss) {
		mss = dst->advmss;
		tp->advmss = mss;
	}

	return (__u16)mss;
}

static __inline__ void tcp_event_data_sent(struct tcp_opt *tp, struct sk_buff *skb)
{
	/* If we had a reply for ato after last received
	 * packet, enter pingpong mode.
	 */
	if ((u32)(tp->lsndtime - tp->ack.lrcvtime) < tp->ack.ato)
		tp->ack.pingpong = 1;

	tp->lsndtime = tcp_time_stamp;
}

static __inline__ void tcp_event_ack_sent(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	tp->last_ack_sent = tp->rcv_nxt;
	tcp_dec_quickack_mode(tp);
	tp->ack.pending = 0;
	tcp_clear_xmit_timer(sk, TCP_TIME_DACK);
}

/* This routine actually transmits TCP packets queued in by
 * tcp_do_sendmsg().  This is used by both the initial
 * transmission and possible later retransmissions.
 * All SKB's seen here are completely headerless.  It is our
 * job to build the TCP header, and pass the packet down to
 * IP so it can do the same plus pass the packet off to the
 * device.
 *
 * We are working here with either a clone of the original
 * SKB, or a fresh unique copy made by the retransmit engine.
 */
int tcp_transmit_skb(struct sock *sk, struct sk_buff *skb)
{
	if(skb != NULL) {
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
		struct tcp_skb_cb *tcb = TCP_SKB_CB(skb);
		int tcp_header_size = tp->tcp_header_len;
		struct tcphdr *th;
		int sysctl_flags;
		int err;

#define SYSCTL_FLAG_TSTAMPS	0x1
#define SYSCTL_FLAG_WSCALE	0x2
#define SYSCTL_FLAG_SACK	0x4

		sysctl_flags = 0;
		if(tcb->flags & TCPCB_FLAG_SYN) {
			tcp_header_size = sizeof(struct tcphdr) + TCPOLEN_MSS;
			if(sysctl_tcp_timestamps) {
				tcp_header_size += TCPOLEN_TSTAMP_ALIGNED;
				sysctl_flags |= SYSCTL_FLAG_TSTAMPS;
			}
			if(sysctl_tcp_window_scaling) {
				tcp_header_size += TCPOLEN_WSCALE_ALIGNED;
				sysctl_flags |= SYSCTL_FLAG_WSCALE;
			}
			if(sysctl_tcp_sack) {
				sysctl_flags |= SYSCTL_FLAG_SACK;
				if(!(sysctl_flags & SYSCTL_FLAG_TSTAMPS))
					tcp_header_size += TCPOLEN_SACKPERM_ALIGNED;
			}
		} else if(tp->sack_ok && tp->num_sacks) {
			/* A SACK is 2 pad bytes, a 2 byte header, plus
			 * 2 32-bit sequence numbers for each SACK block.
			 */
			tcp_header_size += (TCPOLEN_SACK_BASE_ALIGNED +
					    (tp->num_sacks * TCPOLEN_SACK_PERBLOCK));
		}
		th = (struct tcphdr *) skb_push(skb, tcp_header_size);
		skb->h.th = th;
		skb_set_owner_w(skb, sk);

		/* Build TCP header and checksum it. */
		th->source		= sk->sport;
		th->dest		= sk->dport;
		th->seq			= htonl(TCP_SKB_CB(skb)->seq);
		th->ack_seq		= htonl(tp->rcv_nxt);
		th->doff		= (tcp_header_size >> 2);
		th->res1		= 0;
		*(((__u8 *)th) + 13)	= tcb->flags;
		th->check		= 0;
		th->urg_ptr		= ntohs(tcb->urg_ptr);
		if(tcb->flags & TCPCB_FLAG_SYN) {
			/* RFC1323: The window in SYN & SYN/ACK segments
			 * is never scaled.
			 */
			th->window	= htons(tp->rcv_wnd);
			tcp_syn_build_options((__u32 *)(th + 1),
					      tcp_advertise_mss(sk),
					      (sysctl_flags & SYSCTL_FLAG_TSTAMPS),
					      (sysctl_flags & SYSCTL_FLAG_SACK),
					      (sysctl_flags & SYSCTL_FLAG_WSCALE),
					      tp->rcv_wscale,
					      TCP_SKB_CB(skb)->when,
		      			      tp->ts_recent);
		} else {
			th->window	= htons(tcp_select_window(sk));
			tcp_build_and_update_options((__u32 *)(th + 1),
						     tp, TCP_SKB_CB(skb)->when);
		}
		tp->af_specific->send_check(sk, th, skb->len, skb);

		if (th->ack)
			tcp_event_ack_sent(sk);

		if (skb->len != tcp_header_size)
			tcp_event_data_sent(tp, skb);

		TCP_INC_STATS(TcpOutSegs);

		err = tp->af_specific->queue_xmit(skb);
		if (err <= 0)
			return err;

		tcp_enter_cong_avoid(tp);

		/* NET_XMIT_CN is special. It does not guarantee,
		 * that this packet is lost. It tells that device
		 * is about to start to drop packets or already
		 * drops some packets of the same priority and
		 * invokes us to send less aggressively.
		 */
		return err == NET_XMIT_CN ? 0 : err;
	}
	return -ENOBUFS;
#undef SYSCTL_FLAG_TSTAMPS
#undef SYSCTL_FLAG_WSCALE
#undef SYSCTL_FLAG_SACK
}

/* This is the main buffer sending routine. We queue the buffer
 * and decide whether to queue or transmit now.
 *
 * NOTE: probe0 timer is not checked, do not forget tcp_push_pending_frames,
 * otherwise socket can stall.
 */
void tcp_send_skb(struct sock *sk, struct sk_buff *skb, int force_queue, unsigned cur_mss)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Advance write_seq and place onto the write_queue. */
	tp->write_seq = TCP_SKB_CB(skb)->end_seq;
	__skb_queue_tail(&sk->write_queue, skb);

	if (!force_queue && tp->send_head == NULL && tcp_snd_test(tp, skb, cur_mss, 1)) {
		/* Send it out now. */
		TCP_SKB_CB(skb)->when = tcp_time_stamp;
		if (tcp_transmit_skb(sk, skb_clone(skb, GFP_KERNEL)) == 0) {
			tp->snd_nxt = TCP_SKB_CB(skb)->end_seq;
			tcp_minshall_update(tp, cur_mss, skb->len);
			tp->packets_out++;
			if(!tcp_timer_is_set(sk, TCP_TIME_RETRANS))
				tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, tp->rto);
			return;
		}
	}
	/* Queue it, remembering where we must start sending. */
	if (tp->send_head == NULL)
		tp->send_head = skb;
}

/* Function to create two new TCP segments.  Shrinks the given segment
 * to the specified size and appends a new segment with the rest of the
 * packet to the list.  This won't be called frequently, I hope. 
 * Remember, these are still headerless SKBs at this point.
 */
static int tcp_fragment(struct sock *sk, struct sk_buff *skb, u32 len)
{
	struct sk_buff *buff;
	int nsize = skb->len - len;
	u16 flags;

	/* Get a new skb... force flag on. */
	buff = sock_wmalloc(sk,
			    (nsize + MAX_TCP_HEADER + 15),
			    1, GFP_ATOMIC);
	if (buff == NULL)
		return -ENOMEM; /* We'll just try again later. */

	/* Reserve space for headers. */
	skb_reserve(buff, MAX_TCP_HEADER);
		
	/* Correct the sequence numbers. */
	TCP_SKB_CB(buff)->seq = TCP_SKB_CB(skb)->seq + len;
	TCP_SKB_CB(buff)->end_seq = TCP_SKB_CB(skb)->end_seq;
	
	/* PSH and FIN should only be set in the second packet. */
	flags = TCP_SKB_CB(skb)->flags;
	TCP_SKB_CB(skb)->flags = flags & ~(TCPCB_FLAG_FIN | TCPCB_FLAG_PSH);
	if(flags & TCPCB_FLAG_URG) {
		u16 old_urg_ptr = TCP_SKB_CB(skb)->urg_ptr;

		/* Urgent data is always a pain in the ass. */
		if(old_urg_ptr > len) {
			TCP_SKB_CB(skb)->flags &= ~(TCPCB_FLAG_URG);
			TCP_SKB_CB(skb)->urg_ptr = 0;
			TCP_SKB_CB(buff)->urg_ptr = old_urg_ptr - len;
		} else {
			flags &= ~(TCPCB_FLAG_URG);
		}
	}
	if(!(flags & TCPCB_FLAG_URG))
		TCP_SKB_CB(buff)->urg_ptr = 0;
	TCP_SKB_CB(buff)->flags = flags;
	TCP_SKB_CB(buff)->sacked = 0;

	/* Copy and checksum data tail into the new buffer. */
	buff->csum = csum_partial_copy_nocheck(skb->data + len, skb_put(buff, nsize),
					       nsize, 0);

	/* This takes care of the FIN sequence number too. */
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(buff)->seq;
	skb_trim(skb, len);

	/* Rechecksum original buffer. */
	skb->csum = csum_partial(skb->data, skb->len, 0);

	/* Looks stupid, but our code really uses when of
	 * skbs, which it never sent before. --ANK
	 *
	 * NOTE: several days after I added this, Dave repaired
	 * tcp_simple_retransmit() and it should not use ->when
	 * of never sent skbs more. I am not sure, so that
	 * this line remains until more careful investigation. --ANK
	 */
	TCP_SKB_CB(buff)->when = TCP_SKB_CB(skb)->when;

	/* Link BUFF into the send queue. */
	__skb_append(skb, buff);

	return 0;
}

/* This function synchronize snd mss to current pmtu/exthdr set.

   tp->user_mss is mss set by user by TCP_MAXSEG. It does NOT counts
   for TCP options, but includes only bare TCP header.

   tp->mss_clamp is mss negotiated at connection setup.
   It is minumum of user_mss and mss received with SYN.
   It also does not include TCP options.

   tp->pmtu_cookie is last pmtu, seen by this function.

   tp->mss_cache is current effective sending mss, including
   all tcp options except for SACKs. It is evaluated,
   taking into account current pmtu, but never exceeds
   tp->mss_clamp.

   NOTE1. rfc1122 clearly states that advertised MSS
   DOES NOT include either tcp or ip options.

   NOTE2. tp->pmtu_cookie and tp->mss_cache are READ ONLY outside
   this function.			--ANK (980731)
 */

int tcp_sync_mss(struct sock *sk, u32 pmtu)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	int mss_now;

	/* Calculate base mss without TCP options:
	   It is MMS_S - sizeof(tcphdr) of rfc1122
	 */

	mss_now = pmtu - tp->af_specific->net_header_len - sizeof(struct tcphdr);

	/* Clamp it (mss_clamp does not include tcp options) */
	if (mss_now > tp->mss_clamp)
		mss_now = tp->mss_clamp;

	/* Now subtract optional transport overhead */
	mss_now -= tp->ext_header_len;

	/* Then reserve room for full set of TCP options and 8 bytes of data */
	if (mss_now < 48)
		mss_now = 48;

	/* Now subtract TCP options size, not including SACKs */
	mss_now -= tp->tcp_header_len - sizeof(struct tcphdr);

	/* Bound mss with half of window */
	if (tp->max_window && mss_now > (tp->max_window>>1))
		mss_now = max((tp->max_window>>1), 1);

	/* And store cached results */
	tp->pmtu_cookie = pmtu;
	tp->mss_cache = mss_now;
	return mss_now;
}


/* This routine writes packets to the network.  It advances the
 * send_head.  This happens as incoming acks open up the remote
 * window for us.
 *
 * Returns 1, if no segments are in flight and we have queued segments, but
 * cannot send anything now because of SWS or another problem.
 */
int tcp_write_xmit(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	unsigned int mss_now;

	/* If we are closed, the bytes will have to remain here.
	 * In time closedown will finish, we empty the write queue and all
	 * will be happy.
	 */
	if(sk->state != TCP_CLOSE) {
		struct sk_buff *skb;
		int sent_pkts = 0;

		/* Account for SACKS, we may need to fragment due to this.
		 * It is just like the real MSS changing on us midstream.
		 * We also handle things correctly when the user adds some
		 * IP options mid-stream.  Silly to do, but cover it.
		 */
		mss_now = tcp_current_mss(sk); 

		/* Anything on the transmit queue that fits the window can
		 * be added providing we are:
		 *
		 * a) following SWS avoidance [and Nagle algorithm]
		 * b) not exceeding our congestion window.
		 * c) not retransmitting [Nagle]
		 */
		while((skb = tp->send_head) &&
		      tcp_snd_test(tp, skb, mss_now, tcp_skb_is_last(sk, skb))) {
			if (skb->len > mss_now) {
				if (tcp_fragment(sk, skb, mss_now))
					break;
			}

			TCP_SKB_CB(skb)->when = tcp_time_stamp;
			if (tcp_transmit_skb(sk, skb_clone(skb, GFP_ATOMIC)))
				break;
			/* Advance the send_head.  This one is sent out. */
			update_send_head(sk);
			tp->snd_nxt = TCP_SKB_CB(skb)->end_seq;
			tcp_minshall_update(tp, mss_now, skb->len);
			tp->packets_out++;
			sent_pkts = 1;
		}

		/* If we sent anything, make sure the retransmit
		 * timer is active.
		 */
		if (sent_pkts) {
			if (!tcp_timer_is_set(sk, TCP_TIME_RETRANS))
				tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, tp->rto);
			return 0;
		}

		return !tp->packets_out && tp->send_head;
	}
	return 0;
}

/* This function returns the amount that we can raise the
 * usable window based on the following constraints
 *  
 * 1. The window can never be shrunk once it is offered (RFC 793)
 * 2. We limit memory per socket
 *
 * RFC 1122:
 * "the suggested [SWS] avoidance algorithm for the receiver is to keep
 *  RECV.NEXT + RCV.WIN fixed until:
 *  RCV.BUFF - RCV.USER - RCV.WINDOW >= min(1/2 RCV.BUFF, MSS)"
 *
 * i.e. don't raise the right edge of the window until you can raise
 * it at least MSS bytes.
 *
 * Unfortunately, the recommended algorithm breaks header prediction,
 * since header prediction assumes th->window stays fixed.
 *
 * Strictly speaking, keeping th->window fixed violates the receiver
 * side SWS prevention criteria. The problem is that under this rule
 * a stream of single byte packets will cause the right side of the
 * window to always advance by a single byte.
 * 
 * Of course, if the sender implements sender side SWS prevention
 * then this will not be a problem.
 * 
 * BSD seems to make the following compromise:
 * 
 *	If the free space is less than the 1/4 of the maximum
 *	space available and the free space is less than 1/2 mss,
 *	then set the window to 0.
 *	Otherwise, just prevent the window from shrinking
 *	and from being larger than the largest representable value.
 *
 * This prevents incremental opening of the window in the regime
 * where TCP is limited by the speed of the reader side taking
 * data out of the TCP receive queue. It does nothing about
 * those cases where the window is constrained on the sender side
 * because the pipeline is full.
 *
 * BSD also seems to "accidentally" limit itself to windows that are a
 * multiple of MSS, at least until the free space gets quite small.
 * This would appear to be a side effect of the mbuf implementation.
 * Combining these two algorithms results in the observed behavior
 * of having a fixed window size at almost all times.
 *
 * Below we obtain similar behavior by forcing the offered window to
 * a multiple of the mss when it is feasible to do so.
 *
 * Note, we don't "adjust" for TIMESTAMP or SACK option bytes.
 * Regular options like TIMESTAMP are taken into account.
 */
u32 __tcp_select_window(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	/* MSS for the peer's data.  Previous verions used mss_clamp
	 * here.  I don't know if the value based on our guesses
	 * of peer's MSS is better for the performance.  It's more correct
	 * but may be worse for the performance because of rcv_mss
	 * fluctuations.  --SAW  1998/11/1
	 */
	unsigned int mss = tp->ack.rcv_mss;
	int free_space;
	u32 window;

	/* Sometimes free_space can be < 0. */
	free_space = tcp_space(sk); 
	if (free_space > ((int) tp->window_clamp))
		free_space = tp->window_clamp;
	if (tp->window_clamp < mss)
		mss = tp->window_clamp; 

	if ((free_space < (min((int)tp->window_clamp, tcp_full_space(sk)) / 2)) && 
		(free_space < ((int) (mss/2)))) {
		window = 0;

		/* THIS IS _VERY_ GOOD PLACE to play window clamp.
		 * if free_space becomes suspiciously low
		 * verify ratio rmem_alloc/(rcv_nxt - copied_seq),
		 * and if we predict that when free_space will be lower mss,
		 * rmem_alloc will run out of rcvbuf*2, shrink window_clamp.
		 * It will eliminate most of prune events! Very simple,
		 * it is the next thing to do.			--ANK
		 */
	} else {
		/* Get the largest window that is a nice multiple of mss.
		 * Window clamp already applied above.
		 * If our current window offering is within 1 mss of the
		 * free space we just keep it. This prevents the divide
		 * and multiply from happening most of the time.
		 * We also don't do any window rounding when the free space
		 * is too small.
		 */
		window = tp->rcv_wnd;
		if ((((int) window) <= (free_space - ((int) mss))) ||
				(((int) window) > free_space))
			window = (((unsigned int) free_space)/mss)*mss;
	}
	return window;
}

/* Attempt to collapse two adjacent SKB's during retransmission. */
static void tcp_retrans_try_collapse(struct sock *sk, struct sk_buff *skb, int mss_now)
{
	struct sk_buff *next_skb = skb->next;

	/* The first test we must make is that neither of these two
	 * SKB's are still referenced by someone else.
	 */
	if(!skb_cloned(skb) && !skb_cloned(next_skb)) {
		int skb_size = skb->len, next_skb_size = next_skb->len;
		u16 flags = TCP_SKB_CB(skb)->flags;

		/* Punt if the first SKB has URG set. */
		if(flags & TCPCB_FLAG_URG)
			return;
	
		/* Also punt if next skb has been SACK'd. */
		if(TCP_SKB_CB(next_skb)->sacked & TCPCB_SACKED_ACKED)
			return;

		/* Punt if not enough space exists in the first SKB for
		 * the data in the second, or the total combined payload
		 * would exceed the MSS.
		 */
		if ((next_skb_size > skb_tailroom(skb)) ||
		    ((skb_size + next_skb_size) > mss_now))
			return;

		/* Ok.  We will be able to collapse the packet. */
		__skb_unlink(next_skb, next_skb->list);

		if(skb->len % 4) {
			/* Must copy and rechecksum all data. */
			memcpy(skb_put(skb, next_skb_size), next_skb->data, next_skb_size);
			skb->csum = csum_partial(skb->data, skb->len, 0);
		} else {
			/* Optimize, actually we could also combine next_skb->csum
			 * to skb->csum using a single add w/carry operation too.
			 */
			skb->csum = csum_partial_copy_nocheck(next_skb->data,
							      skb_put(skb, next_skb_size),
							      next_skb_size, skb->csum);
		}
	
		/* Update sequence range on original skb. */
		TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(next_skb)->end_seq;

		/* Merge over control information. */
		flags |= TCP_SKB_CB(next_skb)->flags; /* This moves PSH/FIN etc. over */
		if(flags & TCPCB_FLAG_URG) {
			u16 urgptr = TCP_SKB_CB(next_skb)->urg_ptr;
			TCP_SKB_CB(skb)->urg_ptr = urgptr + skb_size;
		}
		TCP_SKB_CB(skb)->flags = flags;

		/* All done, get rid of second SKB and account for it so
		 * packet counting does not break.
		 */
		kfree_skb(next_skb);
		sk->tp_pinfo.af_tcp.packets_out--;
	}
}

/* Do a simple retransmit without using the backoff mechanisms in
 * tcp_timer. This is used for path mtu discovery. 
 * The socket is already locked here.
 */ 
void tcp_simple_retransmit(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb, *old_next_skb;
	unsigned int mss = tcp_current_mss(sk);

 	/* Don't muck with the congestion window here. */
 	tp->dup_acks = 0;
 	tp->high_seq = tp->snd_nxt;
 	tp->retrans_head = NULL;

 	/* Input control flow will see that this was retransmitted
	 * and not use it for RTT calculation in the absence of
	 * the timestamp option.
	 */
	for (old_next_skb = skb = skb_peek(&sk->write_queue);
	     ((skb != tp->send_head) &&
	      (skb != (struct sk_buff *)&sk->write_queue));
	     skb = skb->next) {
		int resend_skb = 0;

		/* Our goal is to push out the packets which we
		 * sent already, but are being chopped up now to
		 * account for the PMTU information we have.
		 *
		 * As we resend the queue, packets are fragmented
		 * into two pieces, and when we try to send the
		 * second piece it may be collapsed together with
		 * a subsequent packet, and so on.  -DaveM
		 */
		if (old_next_skb != skb || skb->len > mss)
			resend_skb = 1;
		old_next_skb = skb->next;
		if (resend_skb != 0) {
			if (tcp_retransmit_skb(sk, skb))
				break;
		}
	}
}

static __inline__ void update_retrans_head(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	
	tp->retrans_head = tp->retrans_head->next;
	if((tp->retrans_head == tp->send_head) ||
	   (tp->retrans_head == (struct sk_buff *) &sk->write_queue)) {
		tp->retrans_head = NULL;
		tp->rexmt_done = 1;
	}
}

/* This retransmits one SKB.  Policy decisions and retransmit queue
 * state updates are done by the caller.  Returns non-zero if an
 * error occurred which prevented the send.
 */
int tcp_retransmit_skb(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	unsigned int cur_mss = tcp_current_mss(sk);

#ifdef TCP_DEBUG
	/* It was possible this summer, that retransmit timer
	 * raced with its deletion and hit socket with packets_out==0.
	 * I fixed it, but preserved the check in the place,
	 * where the fault occured. --ANK
	 */
	if (skb == NULL) {
		printk("tcp_retransmit_skb: bug, skb==NULL, caller=%p\n", NET_CALLER(sk));
		return -EFAULT;
	}
#endif

	if(skb->len > cur_mss) {
		if(tcp_fragment(sk, skb, cur_mss))
			return -ENOMEM; /* We'll try again later. */

		/* New SKB created, account for it. */
		tp->packets_out++;
	}

	/* Collapse two adjacent packets if worthwhile and we can. */
	if(!(TCP_SKB_CB(skb)->flags & TCPCB_FLAG_SYN) &&
	   (skb->len < (cur_mss >> 1)) &&
	   (skb->next != tp->send_head) &&
	   (skb->next != (struct sk_buff *)&sk->write_queue) &&
	   (sysctl_tcp_retrans_collapse != 0))
		tcp_retrans_try_collapse(sk, skb, cur_mss);

	if(tp->af_specific->rebuild_header(sk))
		return -EHOSTUNREACH; /* Routing failure or similar. */

	/* Some Solaris stacks overoptimize and ignore the FIN on a
	 * retransmit when old data is attached.  So strip it off
	 * since it is cheap to do so and saves bytes on the network.
	 */
	if(skb->len > 0 &&
	   (TCP_SKB_CB(skb)->flags & TCPCB_FLAG_FIN) &&
	   tp->snd_una == (TCP_SKB_CB(skb)->end_seq - 1)) {
		TCP_SKB_CB(skb)->seq = TCP_SKB_CB(skb)->end_seq - 1;
		skb_trim(skb, 0);
		skb->csum = 0;
	}

	/* Ok, we're gonna send it out, update state. */
	TCP_SKB_CB(skb)->sacked |= TCPCB_SACKED_RETRANS;
	tp->retrans_out++;

	/* Make a copy, if the first transmission SKB clone we made
	 * is still in somebody's hands, else make a clone.
	 */
	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	if(skb_cloned(skb))
		skb = skb_copy(skb, GFP_ATOMIC);
	else
		skb = skb_clone(skb, GFP_ATOMIC);

	/* Update global TCP statistics and return success. */
	TCP_INC_STATS(TcpRetransSegs);

	return tcp_transmit_skb(sk, skb);
}

/* This gets called after a retransmit timeout, and the initially
 * retransmitted data is acknowledged.  It tries to continue
 * resending the rest of the retransmit queue, until either
 * we've sent it all or the congestion window limit is reached.
 * If doing SACK, the first ACK which comes back for a timeout
 * based retransmit packet might feed us FACK information again.
 * If so, we use it to avoid unnecessarily retransmissions.
 */
void tcp_xmit_retransmit_queue(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb;

	if (tp->retrans_head == NULL &&
	    tp->rexmt_done == 0)
		tp->retrans_head = skb_peek(&sk->write_queue);
	if (tp->retrans_head == tp->send_head)
		tp->retrans_head = NULL;

	/* Each time, advance the retrans_head if we got
	 * a packet out or we skipped one because it was
	 * SACK'd.  -DaveM
	 */
	while ((skb = tp->retrans_head) != NULL) {
		/* If it has been ack'd by a SACK block, we don't
		 * retransmit it.
		 */
		if(!(TCP_SKB_CB(skb)->sacked & TCPCB_SACKED_ACKED)) {
			/* Send it out, punt if error occurred. */
			if(tcp_retransmit_skb(sk, skb))
				break;

			update_retrans_head(sk);
		
			/* Stop retransmitting if we've hit the congestion
			 * window limit.
			 */
			if (tp->retrans_out >= tp->snd_cwnd)
				break;
		} else {
			update_retrans_head(sk);
		}
	}
}

/* Using FACK information, retransmit all missing frames at the receiver
 * up to the forward most SACK'd packet (tp->fackets_out) if the packet
 * has not been retransmitted already.
 */
void tcp_fack_retransmit(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb = skb_peek(&sk->write_queue);
	int packet_cnt = 0;

	while((skb != NULL) &&
	      (skb != tp->send_head) &&
	      (skb != (struct sk_buff *)&sk->write_queue)) {
		__u8 sacked = TCP_SKB_CB(skb)->sacked;

		if(sacked & (TCPCB_SACKED_ACKED | TCPCB_SACKED_RETRANS))
			goto next_packet;

		/* Ok, retransmit it. */
		if(tcp_retransmit_skb(sk, skb))
			break;

		if(tcp_packets_in_flight(tp) >= tp->snd_cwnd)
			break;
next_packet:
		packet_cnt++;
		if(packet_cnt >= tp->fackets_out)
			break;
		skb = skb->next;
	}
}

/* Send a fin.  The caller locks the socket for us.  This cannot be
 * allowed to fail queueing a FIN frame under any circumstances.
 */
void tcp_send_fin(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);	
	struct sk_buff *skb = skb_peek_tail(&sk->write_queue);
	unsigned int mss_now;
	
	/* Optimization, tack on the FIN if we have a queue of
	 * unsent frames.  But be careful about outgoing SACKS
	 * and IP options.
	 */
	mss_now = tcp_current_mss(sk); 

	/* Please, find seven differences of 2.3.33 and loook
	 * what I broke here. 8) --ANK
	 */

	if(tp->send_head != NULL) {
		/* tcp_write_xmit() takes care of the rest. */
		TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_FIN;
		TCP_SKB_CB(skb)->end_seq++;
		tp->write_seq++;

		/* Special case to avoid Nagle bogosity.  If this
		 * segment is the last segment, and it was queued
		 * due to Nagle/SWS-avoidance, send it out now.
		 *
		 * Hmm... actually it overrides also congestion
		 * avoidance (OK for FIN) and retransmit phase
		 * (not OK? Added.).
		 */
		if(tp->send_head == skb &&
		   !after(tp->write_seq, tp->snd_una + tp->snd_wnd) &&
		   !tp->retransmits) {
			TCP_SKB_CB(skb)->when = tcp_time_stamp;
			if (!tcp_transmit_skb(sk, skb_clone(skb, GFP_ATOMIC))) {
				update_send_head(sk);
				tp->snd_nxt = TCP_SKB_CB(skb)->end_seq;
				tp->packets_out++;
				if(!tcp_timer_is_set(sk, TCP_TIME_RETRANS))
					tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, tp->rto);
			} else
				tcp_check_probe_timer(sk, tp);
		}
	} else {
		/* Socket is locked, keep trying until memory is available. */
		do {
			skb = sock_wmalloc(sk,
					   MAX_TCP_HEADER + 15,
					   1, GFP_KERNEL);
		} while (skb == NULL);

		/* Reserve space for headers and prepare control bits. */
		skb_reserve(skb, MAX_TCP_HEADER);
		skb->csum = 0;
		TCP_SKB_CB(skb)->flags = (TCPCB_FLAG_ACK | TCPCB_FLAG_FIN);
		TCP_SKB_CB(skb)->sacked = 0;
		TCP_SKB_CB(skb)->urg_ptr = 0;

		/* FIN eats a sequence byte, write_seq advanced by tcp_send_skb(). */
		TCP_SKB_CB(skb)->seq = tp->write_seq;
		TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq + 1;
		tcp_send_skb(sk, skb, 0, mss_now);
		__tcp_push_pending_frames(sk, tp, mss_now);
	}
}

/* We get here when a process closes a file descriptor (either due to
 * an explicit close() or as a byproduct of exit()'ing) and there
 * was unread data in the receive queue.  This behavior is recommended
 * by draft-ietf-tcpimpl-prob-03.txt section 3.10.  -DaveM
 */
void tcp_send_active_reset(struct sock *sk, int priority)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb;

	/* NOTE: No TCP options attached and we never retransmit this. */
	skb = alloc_skb(MAX_TCP_HEADER + 15, priority);
	if (!skb)
		return;

	/* Reserve space for headers and prepare control bits. */
	skb_reserve(skb, MAX_TCP_HEADER);
	skb->csum = 0;
	TCP_SKB_CB(skb)->flags = (TCPCB_FLAG_ACK | TCPCB_FLAG_RST);
	TCP_SKB_CB(skb)->sacked = 0;
	TCP_SKB_CB(skb)->urg_ptr = 0;

	/* Send it off. */
	TCP_SKB_CB(skb)->seq = tp->snd_nxt;
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq;
	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	tcp_transmit_skb(sk, skb);
}

/* WARNING: This routine must only be called when we have already sent
 * a SYN packet that crossed the incoming SYN that caused this routine
 * to get called. If this assumption fails then the initial rcv_wnd
 * and rcv_wscale values will not be correct.
 */
int tcp_send_synack(struct sock *sk)
{
	struct tcp_opt* tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff* skb;	

	skb = sock_wmalloc(sk, MAX_TCP_HEADER + 15,
			   1, GFP_ATOMIC);
	if (skb == NULL) 
		return -ENOMEM;

	/* Reserve space for headers and prepare control bits. */
	skb_reserve(skb, MAX_TCP_HEADER);
	skb->csum = 0;
	TCP_SKB_CB(skb)->flags = (TCPCB_FLAG_ACK | TCPCB_FLAG_SYN);
	TCP_SKB_CB(skb)->sacked = 0;
	TCP_SKB_CB(skb)->urg_ptr = 0;

	/* SYN eats a sequence byte. */
	TCP_SKB_CB(skb)->seq = tp->snd_una;
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq + 1;
	__skb_queue_tail(&sk->write_queue, skb);
	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	tp->packets_out++;
	return tcp_transmit_skb(sk, skb_clone(skb, GFP_ATOMIC));
}

/*
 * Prepare a SYN-ACK.
 */
struct sk_buff * tcp_make_synack(struct sock *sk, struct dst_entry *dst,
				 struct open_request *req)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct tcphdr *th;
	int tcp_header_size;
	struct sk_buff *skb;

	skb = sock_wmalloc(sk, MAX_TCP_HEADER + 15, 1, GFP_ATOMIC);
	if (skb == NULL)
		return NULL;

	/* Reserve space for headers. */
	skb_reserve(skb, MAX_TCP_HEADER);

	skb->dst = dst_clone(dst);

	tcp_header_size = (sizeof(struct tcphdr) + TCPOLEN_MSS +
			   (req->tstamp_ok ? TCPOLEN_TSTAMP_ALIGNED : 0) +
			   (req->wscale_ok ? TCPOLEN_WSCALE_ALIGNED : 0) +
			   /* SACK_PERM is in the place of NOP NOP of TS */
			   ((req->sack_ok && !req->tstamp_ok) ? TCPOLEN_SACKPERM_ALIGNED : 0));
	skb->h.th = th = (struct tcphdr *) skb_push(skb, tcp_header_size);

	memset(th, 0, sizeof(struct tcphdr));
	th->syn = 1;
	th->ack = 1;
	th->source = sk->sport;
	th->dest = req->rmt_port;
	TCP_SKB_CB(skb)->seq = req->snt_isn;
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq + 1;
	th->seq = htonl(TCP_SKB_CB(skb)->seq);
	th->ack_seq = htonl(req->rcv_isn + 1);
	if (req->rcv_wnd == 0) { /* ignored for retransmitted syns */
		__u8 rcv_wscale; 
		/* Set this up on the first call only */
		req->window_clamp = tp->window_clamp ? : skb->dst->window;
		/* tcp_full_space because it is guaranteed to be the first packet */
		tcp_select_initial_window(tcp_full_space(sk), 
			dst->advmss - (req->tstamp_ok ? TCPOLEN_TSTAMP_ALIGNED : 0),
			&req->rcv_wnd,
			&req->window_clamp,
			req->wscale_ok,
			&rcv_wscale);
		req->rcv_wscale = rcv_wscale; 
	}

	/* RFC1323: The window in SYN & SYN/ACK segments is never scaled. */
	th->window = htons(req->rcv_wnd);

	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	tcp_syn_build_options((__u32 *)(th + 1), dst->advmss, req->tstamp_ok,
			      req->sack_ok, req->wscale_ok, req->rcv_wscale,
			      TCP_SKB_CB(skb)->when,
			      req->ts_recent);

	skb->csum = 0;
	th->doff = (tcp_header_size >> 2);
	TCP_INC_STATS(TcpOutSegs);
	return skb;
}

int tcp_connect(struct sock *sk, struct sk_buff *buff)
{
	struct dst_entry *dst = __sk_dst_get(sk);
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Reserve space for headers. */
	skb_reserve(buff, MAX_TCP_HEADER);

	/* We'll fix this up when we get a response from the other end.
	 * See tcp_input.c:tcp_rcv_state_process case TCP_SYN_SENT.
	 */
	tp->tcp_header_len = sizeof(struct tcphdr) +
		(sysctl_tcp_timestamps ? TCPOLEN_TSTAMP_ALIGNED : 0);

	/* If user gave his TCP_MAXSEG, record it to clamp */
	if (tp->user_mss)
		tp->mss_clamp = tp->user_mss;
	tp->max_window = 0;
	tcp_sync_mss(sk, dst->pmtu);
	tcp_initialize_rcv_mss(sk);

	if (!tp->window_clamp)
		tp->window_clamp = dst->window;
	tp->advmss = dst->advmss;

	tcp_select_initial_window(tcp_full_space(sk),
		tp->advmss - (tp->tcp_header_len - sizeof(struct tcphdr)),
		&tp->rcv_wnd,
		&tp->window_clamp,
		sysctl_tcp_window_scaling,
		&tp->rcv_wscale);

	/* Socket identity change complete, no longer
	 * in TCP_CLOSE, so enter ourselves into the
	 * hash tables.
	 */
	tcp_set_state(sk,TCP_SYN_SENT);
	if (tp->af_specific->hash_connecting(sk))
		goto err_out;

	sk->err = 0;
	sk->done = 0;
	tp->snd_wnd = 0;
	tp->snd_wl1 = 0;
	tp->snd_wl2 = tp->write_seq;
	tp->snd_una = tp->write_seq;
	tp->snd_sml = tp->write_seq;
	tp->rcv_nxt = 0;
	tp->rcv_wup = 0;
	tp->copied_seq = 0;

	tp->rto = TCP_TIMEOUT_INIT;
	tcp_init_xmit_timers(sk);
	tp->retransmits = 0;
	tp->fackets_out = 0;
	tp->retrans_out = 0;

	TCP_SKB_CB(buff)->flags = TCPCB_FLAG_SYN;
	TCP_SKB_CB(buff)->sacked = 0;
	TCP_SKB_CB(buff)->urg_ptr = 0;
	buff->csum = 0;
	TCP_SKB_CB(buff)->seq = tp->write_seq++;
	TCP_SKB_CB(buff)->end_seq = tp->write_seq;
	tp->snd_nxt = tp->write_seq;

	/* Send it off. */
	TCP_SKB_CB(buff)->when = tcp_time_stamp;
	tp->syn_stamp = TCP_SKB_CB(buff)->when;
	__skb_queue_tail(&sk->write_queue, buff);
	tp->packets_out++;
	tcp_transmit_skb(sk, skb_clone(buff, GFP_KERNEL));
	TCP_INC_STATS(TcpActiveOpens);

	/* Timer for repeating the SYN until an answer. */
	tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, tp->rto);
	return 0;

err_out:
	tcp_set_state(sk,TCP_CLOSE);
	kfree_skb(buff);
	return -EADDRNOTAVAIL;
}

/* Send out a delayed ack, the caller does the policy checking
 * to see if we should even be here.  See tcp_input.c:tcp_ack_snd_check()
 * for details.
 */
void tcp_send_delayed_ack(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	unsigned long timeout;

	/* Stay within the limit we were given */
	timeout = tp->ack.ato;
	timeout += jiffies + (timeout>>2);

	/* Use new timeout only if there wasn't a older one earlier. */
	spin_lock_bh(&sk->timer_lock);
	if (!tp->delack_timer.prev || !del_timer(&tp->delack_timer)) {
		sock_hold(sk);
		tp->delack_timer.expires = timeout;
	} else {
		/* If delack timer was blocked or is about to expire,
		 * send ACK now.
		 */
		if (tp->ack.blocked || time_before_eq(tp->delack_timer.expires, jiffies+(tp->ack.ato>>2))) {
			spin_unlock_bh(&sk->timer_lock);

			tcp_send_ack(sk);
			__sock_put(sk);
			return;
		}

		if (time_before(timeout, tp->delack_timer.expires))
			tp->delack_timer.expires = timeout;
	}
	add_timer(&tp->delack_timer);
	spin_unlock_bh(&sk->timer_lock);

#ifdef TCP_FORMAL_WINDOW
	/* Explanation. Header prediction path does not handle
	 * case of zero window. If we send ACK immediately, pred_flags
	 * are reset when sending ACK. If rcv_nxt is advanced and
	 * ack is not sent, than delayed ack is scheduled.
	 * Hence, it is the best place to check for zero window.
	 */
	if (tp->pred_flags) {
		if (tcp_receive_window(tp) == 0)
			tp->pred_flags = 0;
	} else {
		if (skb_queue_len(&tp->out_of_order_queue) == 0 &&
		    !tp->urg_data)
			tcp_fast_path_on(tp);
	}
#endif
}

/* This routine sends an ack and also updates the window. */
void tcp_send_ack(struct sock *sk)
{
	/* If we have been reset, we may not send again. */
	if(sk->state != TCP_CLOSE) {
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
		struct sk_buff *buff;

		/* We are not putting this on the write queue, so
		 * tcp_transmit_skb() will set the ownership to this
		 * sock.
		 */
		buff = alloc_skb(MAX_TCP_HEADER + 15, GFP_ATOMIC);
		if (buff == NULL) {
			tp->ack.pending = 1;
			tcp_reset_xmit_timer(sk, TCP_TIME_DACK, TCP_DELACK_MAX);
			return;
		}

		/* Reserve space for headers and prepare control bits. */
		skb_reserve(buff, MAX_TCP_HEADER);
		buff->csum = 0;
		TCP_SKB_CB(buff)->flags = TCPCB_FLAG_ACK;
		TCP_SKB_CB(buff)->sacked = 0;
		TCP_SKB_CB(buff)->urg_ptr = 0;

		/* Send it off, this clears delayed acks for us. */
		TCP_SKB_CB(buff)->seq = TCP_SKB_CB(buff)->end_seq = tp->snd_nxt;
		TCP_SKB_CB(buff)->when = tcp_time_stamp;
		tcp_transmit_skb(sk, buff);
	}
}

/* This routine sends a packet with an out of date sequence
 * number. It assumes the other end will try to ack it.
 */
int tcp_write_wakeup(struct sock *sk)
{
	if (sk->state != TCP_CLOSE) {
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
		struct sk_buff *skb;

		/* Now this function is never called, while
		 * we have something not ACKed in queue.
		 */
		BUG_TRAP(tp->snd_una == tp->snd_nxt);

		if (tp->snd_wnd > (tp->snd_nxt-tp->snd_una)
		    && ((skb = tp->send_head) != NULL)) {
			int err;
			unsigned long win_size;

			/* We are probing the opening of a window
			 * but the window size is != 0
			 * must have been a result SWS avoidance ( sender )
			 */
			win_size = tp->snd_wnd - (tp->snd_nxt - tp->snd_una);
			if (win_size < TCP_SKB_CB(skb)->end_seq - TCP_SKB_CB(skb)->seq) {
				if (tcp_fragment(sk, skb, win_size))
					return -1;
			}
			TCP_SKB_CB(skb)->when = tcp_time_stamp;
			err = tcp_transmit_skb(sk, skb_clone(skb, GFP_ATOMIC));
			if (!err) {
				update_send_head(sk);
				tp->snd_nxt = TCP_SKB_CB(skb)->end_seq;
				tp->packets_out++;
				if (!tcp_timer_is_set(sk, TCP_TIME_RETRANS))
					tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, tp->rto);
			}
			return err;
		} else {
			/* We don't queue it, tcp_transmit_skb() sets ownership. */
			skb = alloc_skb(MAX_TCP_HEADER + 15, GFP_ATOMIC);
			if (skb == NULL) 
				return -1;

			/* Reserve space for headers and set control bits. */
			skb_reserve(skb, MAX_TCP_HEADER);
			skb->csum = 0;
			TCP_SKB_CB(skb)->flags = TCPCB_FLAG_ACK;
			TCP_SKB_CB(skb)->sacked = 0;
			TCP_SKB_CB(skb)->urg_ptr = 0;

			/* Use a previous sequence.  This should cause the other
			 * end to send an ack.  Don't queue or clone SKB, just
			 * send it.
			 *
			 * RED-PEN: logically it should be snd_una-1.
			 * snd_nxt-1 will not be acked. snd_una==snd_nxt
			 * in this place however. Right?
			 */
			TCP_SKB_CB(skb)->seq = tp->snd_una - 1;
			TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq;
			TCP_SKB_CB(skb)->when = tcp_time_stamp;
			return tcp_transmit_skb(sk, skb);
		}
	}
	return -1;
}

/* A window probe timeout has occurred.  If window is not closed send
 * a partial packet else a zero probe.
 */
void tcp_send_probe0(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int err;

	err = tcp_write_wakeup(sk);

	if (tp->packets_out || !tp->send_head) {
		/* Cancel probe timer, if it is not required. */
		tp->probes_out = 0;
		tp->backoff = 0;
		return;
	}

	if (err <= 0) {
		tp->backoff++;
		tp->probes_out++;
		tcp_reset_xmit_timer (sk, TCP_TIME_PROBE0, 
				      min(tp->rto << tp->backoff, TCP_RTO_MAX));
	} else {
		/* If packet was not sent due to local congestion,
		 * do not backoff and do not remember probes_out.
		 * Let local senders to fight for local resources.
		 *
		 * Use accumulated backoff yet.
		 */
		if (!tp->probes_out)
			tp->probes_out=1;
		tcp_reset_xmit_timer (sk, TCP_TIME_PROBE0, 
				      min(tp->rto << tp->backoff, TCP_RESOURCE_PROBE_INTERVAL));
	}
}
