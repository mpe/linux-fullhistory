/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_input.c,v 1.43 1997/04/16 09:18:47 davem Exp $
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
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <net/tcp.h>
#include <linux/ipsec.h>

typedef void			(*tcp_sys_cong_ctl_t)(struct sock *sk,
						      u32 seq, u32 ack,
						      u32 seq_rtt);

static void tcp_cong_avoid_vanj(struct sock *sk, u32 seq, u32 ack,
				u32 seq_rtt);
static void tcp_cong_avoid_vegas(struct sock *sk, u32 seq, u32 ack,
				 u32 seq_rtt);

int sysctl_tcp_cong_avoidance = 0;

static tcp_sys_cong_ctl_t tcp_sys_cong_ctl_f = &tcp_cong_avoid_vanj;

/*
 *	Called each time to estimate the delayed ack timeout. This is
 *	how it should be done so a fast link isnt impacted by ack delay.
 *
 *	I think we need a medium deviation here also...
 *	The estimated value is changing to fast
 */
 
static void tcp_delack_estimator(struct tcp_opt *tp)
{
	int m;

	/*
	 *	Delayed ACK time estimator.
	 */
	
	m = jiffies - tp->lrcvtime;

	tp->lrcvtime = jiffies;

	if (m < 0)
		return;

	/*
	 * if the mesured value is bigger than
	 * twice the round trip time ignore it.
	 */
	if ((m << 2) <= tp->srtt) 
	{
		m -= (tp->iat >> 3);
		tp->iat += m;

		if (m <0)
			m = -m;

		m -= (tp->iat_mdev >> 2);
		tp->iat_mdev += m;

		tp->ato = (tp->iat >> 3) + (tp->iat_mdev >> 2);

		if (tp->ato < HZ/50)
			tp->ato = HZ/50;
	}
	else
		tp->ato = 0;
}

/*
 *	Called on frames that were known _not_ to have been
 *	retransmitted [see Karn/Partridge Proceedings SIGCOMM 87]. 
 *	The algorithm is from the SIGCOMM 88 piece by Van Jacobson.
 */

extern __inline__ void tcp_rtt_estimator(struct tcp_opt *tp, __u32 mrtt)
{
	long m;
	/*
	 *	The following amusing code comes from Jacobson's
	 *	article in SIGCOMM '88.  Note that rtt and mdev
	 *	are scaled versions of rtt and mean deviation.
	 *	This is designed to be as fast as possible 
	 *	m stands for "measurement".
	 */
	/*
	 *	On a 1990 paper the rto value is changed to:
	 *	RTO = rtt + 4 * mdev
	 */

	m = mrtt;  /* RTT */

	if (tp->srtt != 0) {
		if(m<=0)
			m=1;		/* IS THIS RIGHT FOR <0 ??? */
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


	/*
	 *	Now update timeout.  Note that this removes any backoff.
	 */
			 
	tp->rto = (tp->srtt >> 3) + tp->mdev;
	tp->rto += (tp->rto >> 2) + (tp->rto >> (tp->snd_cwnd-1));


	if (tp->rto > 120*HZ)
		tp->rto = 120*HZ;

	/* Was 1*HZ - keep .2 as minimum cos of the BSD delayed acks 
	 * FIXME: It's not entirely clear this lower bound is the best
	 * way to avoid the problem. Is it possible to drop the lower
	 * bound and still avoid trouble with BSD stacks? Perhaps
	 * some modification to the RTO calculation that takes delayed
	 * ack bais into account? This needs serious thought. -- erics
	 */
	if (tp->rto < HZ/5)
		tp->rto = HZ/5;

	tp->backoff = 0;
}

static int __tcp_sequence(struct tcp_opt *tp, u32 seq, u32 end_seq)
{
	u32 end_window;

	end_window = tp->rcv_wup + tp->rcv_wnd;

	if (tp->rcv_wnd)
	{
		if (!before(seq, tp->rcv_nxt) && before(seq, end_window))
			return 1;

		if ((end_seq - seq) && after(end_seq, tp->rcv_nxt) &&
		    !after(end_seq, end_window))
			return 1;
	}

	return 0;
}

/*
 *	This functions checks to see if the tcp header is actually acceptable. 
 */
 
extern __inline__ int tcp_sequence(struct tcp_opt *tp, u32 seq, u32 end_seq)
{
	if (seq == tp->rcv_nxt)
	{
		return (tp->rcv_wnd || (end_seq == seq));
	}
	return __tcp_sequence(tp, seq, end_seq);
}

/*
 *	When we get a reset we do this. This probably is a tcp_output routine
 *	really.
 */

static int tcp_reset(struct sock *sk, struct sk_buff *skb)
{
	sk->zapped = 1;
	/*
	 *	We want the right error as BSD sees it (and indeed as we do).
	 */
	switch (sk->state) {
		case TCP_TIME_WAIT:
			break;
		case TCP_SYN_SENT:
			sk->err = ECONNREFUSED;
			break;
		case TCP_CLOSE_WAIT:
			sk->err = EPIPE;
			break;
		default:
			sk->err = ECONNRESET;
	}
#ifdef CONFIG_TCP_RFC1337
	/*
	 *	Time wait assassination protection [RFC1337]
	 *
	 *	This is a good idea, but causes more sockets to take time to close.
	 *
	 *	Ian Heavens has since shown this is an inadequate fix for the protocol
	 *	bug in question.
	 */
	if(sk->state!=TCP_TIME_WAIT)
	{	
		tcp_set_state(sk,TCP_CLOSE);
		sk->shutdown = SHUTDOWN_MASK;
	}
#else	
	tcp_set_state(sk,TCP_CLOSE);
	sk->shutdown = SHUTDOWN_MASK;
#endif	
	if (!sk->dead) 
		sk->state_change(sk);

	return(0);
}


/*
 *	Look for tcp options. Parses everything but only knows about MSS.
 *	This routine is always called with the packet containing the SYN.
 *	However it may also be called with the ack to the SYN.  So you
 *	can't assume this is always the SYN.  It's always called after
 *	we have set up sk->mtu to our own MTU.
 *
 *	We need at minimum to add PAWS support here. Possibly large windows
 *	as Linux gets deployed on 100Mb/sec networks.
 */
 
int tcp_parse_options(struct tcphdr *th)
{
	unsigned char *ptr;
	int length=(th->doff*4)-sizeof(struct tcphdr);
	int mss = 0;

	ptr = (unsigned char *)(th + 1);

	while(length>0)
	{
	  	int opcode=*ptr++;
	  	int opsize=*ptr++;
	  	switch(opcode)
	  	{
	  		case TCPOPT_EOL:
	  			return 0;
	  		case TCPOPT_NOP:	/* Ref: RFC 793 section 3.1 */
	  			length--;
	  			ptr--;		/* the opsize=*ptr++ above was a mistake */
	  			continue;
	  		
	  		default:
	  			if(opsize<=2)	/* Avoid silly options looping forever */
	  				return 0;
	  			switch(opcode)
	  			{
	  				case TCPOPT_MSS:
	  					if(opsize==TCPOLEN_MSS && th->syn)
	  					{
							mss = ntohs(*(unsigned short *)ptr);
	  					}
	  					break;
		  				/* Add other options here as people feel the urge to implement stuff like large windows */
	  			}
	  			ptr+=opsize-2;
	  			length-=opsize;
	  	}
	}

	return mss;
}


/* 
 *  See draft-stevens-tcpca-spec-01 for documentation.
 */

static void tcp_fast_retrans(struct sock *sk, u32 ack, int not_dup)
{
	struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);

	/* FIXME: if we are already retransmitting should this code
	 * be skipped? [Floyd high_seq check sort of does this]
	 * The case I'm worried about is falling into a fast
	 * retransmit on a link with a congestion window of 1 or 2.
	 * There was some evidence in 2.0.x that this was problem
	 * on really slow links (1200 or 2400 baud). I need to
	 * try this situation again and see what happens.
	 */

	/*
	 * An ACK is a duplicate if:
	 * (1) it has the same sequence number as the largest number we've 
	 *     seen,
	 * (2) it has the same window as the last ACK,
	 * (3) we have outstanding data that has not been ACKed
	 * (4) The packet was not carrying any data.
	 * (5) [From Floyds paper on fast retransmit wars]
	 *     The packet acked data after high_seq;
	 */

	if (ack == tp->snd_una && sk->packets_out && (not_dup == 0))
	{
		/*
		 * 1. When the third duplicate ack is received, set ssthresh 
		 * to one half the current congestion window, but no less 
		 * than two segments. Retransmit the missing segment.
		 */

		if (tp->high_seq == 0 || after(ack, tp->high_seq))
		{
			sk->dup_acks++;

			if (sk->dup_acks == 3)
			{
				sk->ssthresh = max(tp->snd_cwnd >> 1, 2);
				tp->snd_cwnd = sk->ssthresh + 3;
				tcp_do_retransmit(sk, 0);
				/* careful not to timeout just after fast
				 * retransmit!
				 */
				tcp_reset_xmit_timer(sk, TIME_RETRANS,
						     tp->rto);
			}
		}

		/*
		 * 2. Each time another duplicate ACK arrives, increment 
		 * cwnd by the segment size. [...] Transmit a packet...
		 *
		 * Packet transmission will be done on normal flow processing
		 * since we're not in "retransmit mode"
		 */

		if (sk->dup_acks >= 3)
		{
			sk->dup_acks++;
			tp->snd_cwnd++;
		}
	}
	else
	{
		/*
		 * 3. When the next ACK arrives that acknowledges new data,
		 *    set cwnd to ssthresh
		 */

		if (sk->dup_acks >= 3)
		{
			tp->retrans_head = NULL;
			tp->snd_cwnd = max(sk->ssthresh, 1);
			atomic_set(&sk->retransmits, 0);
		}
		sk->dup_acks = 0;
		tp->high_seq = 0;
	}
}

/*
 *      TCP slow start and congestion avoidance in two flavors:
 *      RFC 1122 and TCP Vegas.
 *
 *      This is a /proc/sys configurable option. 
 */

#define SHIFT_FACTOR 16

static void tcp_cong_avoid_vegas(struct sock *sk, u32 seq, u32 ack,
				 u32 seq_rtt)
{
	struct tcp_opt * tp;
	unsigned int actual, expected;
	unsigned int inv_rtt, inv_basertt, inv_basebd;
	u32 snt_bytes;

	/*
	 *	From:
	 *      TCP Vegas: New Techniques for Congestion 
	 *	Detection and Avoidance.
	 *
	 *
	 *	Warning: This code is a scratch implementation taken
	 *	from the paper only. The code they distribute seams
	 *	to have improved several things over the initial spec.
	 */

	tp = &(sk->tp_pinfo.af_tcp);

	if (!seq_rtt)
		seq_rtt = 1;

	if (tp->basertt)
		tp->basertt = min(seq_rtt, tp->basertt);
	else
		tp->basertt = seq_rtt;

	/*
	 * 
	 *	actual	 = throughput for this segment.
	 *	expected = number_of_bytes in transit / BaseRTT
	 * 
	 */

	snt_bytes = ack - seq;

	inv_rtt = (1 << SHIFT_FACTOR) / seq_rtt;
	inv_basertt = (1 << SHIFT_FACTOR) / tp->basertt;

	actual =  snt_bytes * inv_rtt;

	expected = (tp->snd_nxt - tp->snd_una) * inv_basertt;

	inv_basebd = sk->mss * inv_basertt;

	/*
	 *      Slow Start
	 */
	
	if (tp->snd_cwnd < sk->ssthresh &&
	    (seq == tp->snd_nxt ||
	     (expected - actual <= TCP_VEGAS_GAMMA * inv_basebd)))
	{
		/*
		 * "Vegas allows exponential growth only every other
		 *  RTT"
		 */
			
		if (sk->cong_count++)
		{
			tp->snd_cwnd++;
			sk->cong_count = 0;
		}
	}
	else 
	{
		/*
		 *      Congestion Avoidance
		 */

		if (expected - actual <= TCP_VEGAS_ALPHA * inv_basebd)
		{
			/* Increase Linearly */
				
			if (sk->cong_count++ >= tp->snd_cwnd)
			{
				tp->snd_cwnd++;
				sk->cong_count = 0;
			}
		}

		if (expected - actual >= TCP_VEGAS_BETA * inv_basebd)
		{
			/* Decrease Linearly */

			if (sk->cong_count++ >= tp->snd_cwnd)
			{
				tp->snd_cwnd--;
				sk->cong_count = 0;
			}

			/* Never less than 2 segments */
			if (tp->snd_cwnd < 2)
				tp->snd_cwnd = 2;
		}
	}
}

static void tcp_cong_avoid_vanj(struct sock *sk, u32 seq, u32 ack, u32 seq_rtt)
{
	struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);
	
        /* 
         * This is Jacobson's slow start and congestion avoidance. 
         * SIGCOMM '88, p. 328.  Because we keep cong_window in 
         * integral mss's, we can't do cwnd += 1 / cwnd.  
         * Instead, maintain a counter and increment it once every 
         * cwnd times.  
	 * FIXME: Check to be sure the mathematics works out right
	 * on this trick when we have to reduce the congestion window.
	 * The cong_count has to be reset properly when reduction events
	 * happen.
	 * FIXME: What happens when the congestion window gets larger
	 * than the maximum receiver window by some large factor
	 * Suppose the pipeline never looses packets for a long
	 * period of time, then traffic increases causing packet loss.
	 * The congestion window should be reduced, but what it should
	 * be reduced to is not clear, since 1/2 the old window may
	 * still be larger than the maximum sending rate we ever achieved.
         */

        if (tp->snd_cwnd <= sk->ssthresh)  
	{
                /* 
                 *	In "safe" area, increase
                 */

                tp->snd_cwnd++;
	}
        else 
	{
                /*
                 *	In dangerous area, increase slowly.  
                 *      In theory this is
                 *  	tp->snd_cwnd += 1 / tp->snd_cwnd
                 */

                if (sk->cong_count >= tp->snd_cwnd) {
			
                        tp->snd_cwnd++;
                        sk->cong_count = 0;
                }
                else 
                        sk->cong_count++;
        }       
}


#define FLAG_DATA		0x01
#define FLAG_WIN_UPDATE		0x02
#define FLAG_DATA_ACKED		0x04

static int tcp_clean_rtx_queue(struct sock *sk, __u32 ack, __u32 *seq,
			       __u32 *seq_rtt)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb;
	unsigned long now = jiffies;
	int acked = 0;

	while((skb=skb_peek(&sk->write_queue)) && (skb != tp->send_head))
	{

#ifdef TCP_DEBUG
		/* Check for a bug. */

		if (skb->next != (struct sk_buff*) &sk->write_queue &&
		    after(skb->end_seq, skb->next->seq))
		{
			printk(KERN_DEBUG "INET: tcp_input.c: *** "
			       "bug send_list out of order.\n");
		}
#endif								
		/*
		 *	If our packet is before the ack sequence we can
		 *	discard it as it's confirmed to have arrived the 
		 *	other end.
		 */
		 
		if (after(skb->end_seq, ack))
			break;

		SOCK_DEBUG(sk, "removing seg %x-%x from retransmit queue\n",
			   skb->seq, skb->end_seq);

		acked = FLAG_DATA_ACKED;
		
		/* FIXME: packet counting may break if we have to
		 * do packet "repackaging" for stacks that don't
		 * like overlapping packets.
		 */
		sk->packets_out--;

		*seq = skb->seq;
		*seq_rtt = now - skb->when;

		skb_unlink(skb);
		
		kfree_skb(skb, FREE_WRITE);
	}

	if (acked)
	{
		tp->retrans_head = NULL;
		if (!sk->dead)
			sk->write_space(sk);
	}

	return acked;
}

static void tcp_ack_probe(struct sock *sk, __u32 ack)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	
	/*
	 *	Our probe was answered
	 */
	tp->probes_out = 0;
	
	/*
	 *	Was it a usable window open ?
	 */
	
	/* should always be non-null */
	if (tp->send_head != NULL &&
	    !before (ack + tp->snd_wnd, tp->send_head->end_seq))
	{
		tp->backoff = 0;
		tp->pending = 0;
		
		tcp_clear_xmit_timer(sk, TIME_PROBE0);
		
	}
	else
	{
		tcp_reset_xmit_timer(sk, TIME_PROBE0,
				     min(tp->rto << tp->backoff, 120*HZ));
	}
}
 
/*
 *	This routine deals with incoming acks, but not outgoing ones.
 */

static int tcp_ack(struct sock *sk, struct tcphdr *th, 
		   u32 ack_seq, u32 ack, int len)
{
	int flag = 0;
	u32 seq = 0;
	u32 seq_rtt = 0;
	struct sk_buff *skb;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);


	if(sk->zapped)
		return(1);	/* Dead, can't ack any more so why bother */

	if (tp->pending == TIME_KEEPOPEN)
	{
	  	tp->probes_out = 0;
	}

	tp->rcv_tstamp = jiffies;

	/*
	 *	If the ack is newer than sent or older than previous acks
	 *	then we can probably ignore it.
	 */

	if (after(ack, tp->snd_nxt) || before(ack, tp->snd_una))
		goto uninteresting_ack;

	/*
	 *	If there is data set flag 1
	 */
	 
	if (len != th->doff*4) 
	{
		flag |= FLAG_DATA;
		tcp_delack_estimator(tp);
	}

	/*
	 *	Update our send window
	 */

	/*
	 *	This is the window update code as per RFC 793
	 *	snd_wl{1,2} are used to prevent unordered
	 *	segments from shrinking the window 
	 */

	if (before(tp->snd_wl1, ack_seq) ||
	    (tp->snd_wl1 == ack_seq && !after(tp->snd_wl2, ack)))
	{
		unsigned long nwin;

		nwin = ntohs(th->window);
		if ((tp->snd_wl2 != ack) || (nwin > tp->snd_wnd))
		{
			flag |= FLAG_WIN_UPDATE;
			tp->snd_wnd = nwin;

			tp->snd_wl1 = ack_seq;
			tp->snd_wl2 = ack;

			if (nwin > sk->max_window)
				sk->max_window = nwin;
		}
	}

	/*
	 *	We passed data and got it acked, remove any soft error
	 *	log. Something worked...
	 */

	sk->err_soft = 0;

	/*
	 *	If this ack opens up a zero window, clear backoff.  It was
	 *	being used to time the probes, and is probably far higher than
	 *	it needs to be for normal retransmission.
	 */

	if (tp->pending == TIME_PROBE0)
	{
		tcp_ack_probe(sk, ack);
	}

	/* 
	 *	See if we can take anything off of the retransmit queue.
	 */

	if (tcp_clean_rtx_queue(sk, ack, &seq, &seq_rtt))
		flag |= FLAG_DATA_ACKED;


	/*
	 *	if we where retransmiting don't count rtt estimate
	 */

	if (atomic_read(&sk->retransmits))
	{
		if (sk->packets_out == 0)
			atomic_set(&sk->retransmits, 0);
	}
	else
	{
		/*
		 * Note that we only reset backoff and rto in the
		 * rtt recomputation code.  And that doesn't happen
		 * if there were retransmissions in effect.  So the
		 * first new packet after the retransmissions is
		 * sent with the backoff still in effect.  Not until
		 * we get an ack from a non-retransmitted packet do
		 * we reset the backoff and rto.  This allows us to deal
		 * with a situation where the network delay has increased
		 * suddenly.  I.e. Karn's algorithm. (SIGCOMM '87, p5.)
		 */

		if (flag & FLAG_DATA_ACKED)
		{
			tcp_rtt_estimator(tp, seq_rtt);

			(*tcp_sys_cong_ctl_f)(sk, seq, ack, seq_rtt);
		}
	}

	if (sk->packets_out)
	{
		if (flag & FLAG_DATA_ACKED)
		{
			long when;

			skb = skb_peek(&sk->write_queue);
			when = tp->rto - (jiffies - skb->when);

			/*
			 * FIXME: This assumes that when we are retransmitting
			 * we should only ever respond with one packet.
			 * This means congestion windows should not grow
			 * during recovery. In 2.0.X we allow the congestion
			 * window to grow. It is not clear to me which
			 * decision is correct. The RFCs should be double
			 * checked as should the behavior of other stacks.
			 * Also note that if we do want to allow the
			 * congestion window to grow during retransmits
			 * we have to fix the call to congestion window
			 * updates so that it works during retransmission.
			 */

			if (atomic_read(&sk->retransmits))
			{
				tp->retrans_head = NULL;
				/* 
				 * This is tricky. We are retransmiting a 
				 * segment of a window when congestion occured.
				 */
				tcp_do_retransmit(sk, 0);
				tcp_reset_xmit_timer(sk, TIME_RETRANS,
						     tp->rto);
			}
			else
				tcp_reset_xmit_timer(sk, TIME_RETRANS, when);
		}
	}
	else
		tcp_clear_xmit_timer(sk, TIME_RETRANS);


	/* FIXME: danger, if we just did a timeout and got the third
	 * ack on this packet, then this is going to send it again!
	 * [No. Floyd retransmit war check keeps this from happening. -- erics]
	 */
	tcp_fast_retrans(sk, ack, (flag & (FLAG_DATA|FLAG_WIN_UPDATE)));

	/*
	 *	Remember the highest ack received.
	 */

	tp->snd_una = ack;

	return 1;

uninteresting_ack:

	SOCK_DEBUG(sk, "Ack ignored %u %u\n", ack, tp->snd_nxt);

	return 0;
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
 *
 */
 
static int tcp_fin(struct sk_buff *skb, struct sock *sk, struct tcphdr *th)
{
	sk->fin_seq = skb->end_seq;

	tcp_send_ack(sk);

	if (!sk->dead) 
	{
		sk->state_change(sk);
		sock_wake_async(sk->socket, 1);
	}

	switch(sk->state) 
	{
		case TCP_SYN_RECV:
		case TCP_SYN_SENT:
		case TCP_ESTABLISHED:
			/*
			 * move to CLOSE_WAIT
			 */

			tcp_set_state(sk, TCP_CLOSE_WAIT);
			
			if (th->rst)
				sk->shutdown = SHUTDOWN_MASK;
			break;

		case TCP_CLOSE_WAIT:
		case TCP_CLOSING:
			/*
			 * received a retransmission of the FIN, do
			 * nothing.
			 */
			break;
		case TCP_TIME_WAIT:
			/*
			 * received a retransmission of the FIN,
			 * restart the TIME_WAIT timer.
			 */
			tcp_reset_msl_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
			return(0);
		case TCP_FIN_WAIT1:
			/*
			 * This case occurs when a simultaneous close
			 * happens, we must ack the received FIN and
			 * enter the CLOSING state.
			 *
			 * This causes a WRITE timeout, which will either
			 * move on to TIME_WAIT when we timeout, or resend
			 * the FIN properly (maybe we get rid of that annoying
			 * FIN lost hang). The TIME_WRITE code is already 
			 * correct for handling this timeout.
			 */

			tcp_set_state(sk, TCP_CLOSING);
			break;
		case TCP_FIN_WAIT2:
			/*
			 * received a FIN -- send ACK and enter TIME_WAIT
			 */
			tcp_reset_msl_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
			sk->shutdown|=SHUTDOWN_MASK;
			tcp_set_state(sk,TCP_TIME_WAIT);
			break;
		case TCP_CLOSE:
			/*
			 * already in CLOSE
			 */
			break;
		default:
			tcp_set_state(sk,TCP_LAST_ACK);
	
			/* Start the timers. */
			tcp_reset_msl_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
			return(0);
	}

	return(0);
}



	/*
	 * This one checks to see if we can put data from the
	 * out_of_order queue into the receive_queue
	 */

static void  tcp_ofo_queue(struct sock *sk)
{
	struct sk_buff * skb;
	struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);

	while ((skb = skb_peek(&sk->out_of_order_queue))) {
		
		if (after(skb->seq, tp->rcv_nxt))
			break;

		if (!after(skb->end_seq, tp->rcv_nxt)) {
			SOCK_DEBUG(sk, "ofo packet was allready received \n");
			skb_unlink(skb);
			kfree_skb(skb, FREE_READ);

			continue;
		}
		SOCK_DEBUG(sk, "ofo requeuing : rcv_next %X seq %X - %X\n",
			   tp->rcv_nxt, skb->seq, skb->end_seq);

		skb_unlink(skb);
		 
		skb_queue_tail(&sk->receive_queue, skb);

		tp->rcv_nxt = skb->end_seq;
	}
}

static void tcp_data_queue(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff * skb1;
	struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);

	/*
	 *  Queue data for delivery to the user
	 *  Packets in sequence go to the receive queue
	 *  Out of sequence packets to out_of_order_queue
	 */


	if (skb->seq == tp->rcv_nxt) {

		/*
		 * Ok. In sequence.
		 */
		
 
		skb_queue_tail(&sk->receive_queue, skb);


		tp->rcv_nxt = skb->end_seq;

		tcp_ofo_queue(sk);
		
		if (skb_queue_len(&sk->out_of_order_queue) == 0)
			tp->pred_flags = htonl((0x5010 << 16) | tp->snd_wnd);

		return;
	}
	
	/*
	 *  Not in sequence
	 *  either a retransmit or some packet got lost
	 */

	if (!after(skb->end_seq, tp->rcv_nxt)) {
		
		/* 
		 * A retransmit.
		 * 2nd most common case.
		 * force an imediate ack
		 */
		SOCK_DEBUG(sk, "retransmit received: seq %X\n", skb->seq);

		sk->delayed_acks = MAX_DELAY_ACK;
		kfree_skb(skb, FREE_READ);

		return;
	}


	if (before(skb->seq, tp->rcv_nxt)) {

		/*
		 * Partial packet
		 * seq < rcv_next < end_seq
		 */
		SOCK_DEBUG(sk, "partial packet: rcv_next %X seq %X - %X\n",
			   tp->rcv_nxt, skb->seq, skb->end_seq);

		skb_queue_tail(&sk->receive_queue, skb);

		tp->rcv_nxt = skb->end_seq;

		tcp_ofo_queue(sk);

		if (skb_queue_len(&sk->out_of_order_queue) == 0)
			tp->pred_flags = htonl((0x5010 << 16) | tp->snd_wnd);

		return;
	}

	/*
	 *	Ok. This is an out_of_order segment
	 */

	/* Force an ack */

	sk->delayed_acks = MAX_DELAY_ACK;

	/*
	 *	disable header predition
	 */

	tp->pred_flags = 0;

	SOCK_DEBUG(sk, "out of order segment: rcv_next %X seq %X - %X\n",
		   tp->rcv_nxt, skb->seq, skb->end_seq);

	if (skb_peek(&sk->out_of_order_queue) == NULL) {
		skb_queue_head(&sk->out_of_order_queue,skb);
	}
	else 
		for(skb1=sk->out_of_order_queue.prev; ; skb1 = skb1->prev) {

			/* allready there */
			if (skb->seq==skb1->seq && skb->len>=skb1->len)
			{
 				skb_append(skb1,skb);
 				skb_unlink(skb1);
 				kfree_skb(skb1,FREE_READ);
				break;
			}
			
			if (after(skb->seq, skb1->seq))
			{
				skb_append(skb1,skb);
				break;
			}

                        /*
			 *	See if we've hit the start. If so insert.
			 */
			if (skb1 == skb_peek(&sk->out_of_order_queue)) {
				skb_queue_head(&sk->out_of_order_queue,skb);
				break;
			}
		}
}


/*
 *	This routine handles the data.  If there is room in the buffer,
 *	it will be have already been moved into it.  If there is no
 *	room, then we will just have to discard the packet.
 */

static int tcp_data(struct sk_buff *skb, struct sock *sk, unsigned int len)
{
	struct tcphdr *th;
	struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);

	th = skb->h.th;
	skb_pull(skb,th->doff*4);
	skb_trim(skb,len-(th->doff*4));

        if (skb->len == 0 && !th->fin)
        {
		return(0);
        }

	/*
	 *	FIXME: don't accept data after the receved fin
	 */

	/*
	 *	The bytes in the receive read/assembly queue has increased. 
	 *	Needed for the low memory discard algorithm 
	 */

	sk->bytes_rcv += skb->len;

	/*
	 *	We no longer have anyone receiving data on this connection.
	 */

	tcp_data_queue(sk, skb);

	if (before(tp->rcv_nxt, sk->copied_seq)) 
	{
		printk(KERN_DEBUG "*** tcp.c:tcp_data bug acked < copied\n");
		tp->rcv_nxt = sk->copied_seq;
	}

	sk->delayed_acks++;

	/*
	 *	Now tell the user we may have some data. 
	 */

	if (!sk->dead)
	{
		SOCK_DEBUG(sk, "Data wakeup.\n");
		sk->data_ready(sk,0);
	}
	return(1);
}

static void tcp_data_snd_check(struct sock *sk)
{
	struct sk_buff *skb;
	struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);

	if ((skb = tp->send_head))
	{
		if (!after(skb->end_seq, tp->snd_una + tp->snd_wnd) &&
		    sk->packets_out < tp->snd_cwnd )
		{
			/*
			 *	Add more data to the send queue.
			 */
			/* FIXME: the congestion window is checked
			 * again in tcp_write_xmit anyway?!
			 */

			tcp_write_xmit(sk);
			if(!sk->dead)
				sk->write_space(sk);
		}
		else if (sk->packets_out == 0 && !tp->pending)
 		{
 			/*
 			 *	Data to queue but no room.
 			 */
			/* FIXME: Is it right to do a zero window probe into
			 * a congestion window limited window???
			 */
 			tcp_reset_xmit_timer(sk, TIME_PROBE0, tp->rto);
 		}
	}
}

static __inline__ void tcp_ack_snd_check(struct sock *sk)
{
	/*
	 *	This also takes care of updating the window.
	 *	This if statement needs to be simplified.
	 *
	 *      rules for delaying an ack:
	 *      - delay time <= 0.5 HZ
	 *      - we don't have a window update to send
	 *      - must send at least every 2 full sized packets
	 */

	if (sk->delayed_acks == 0)
	{
		/*
		 *	We sent a data segment already
		 */
		return;
	}

	if (sk->delayed_acks >= MAX_DELAY_ACK || tcp_raise_window(sk))
	{
		tcp_send_ack(sk);
	}
	else
	{
		tcp_send_delayed_ack(sk, HZ/2);
	}
}

/*
 *	This routine is only called when we have urgent data
 *	signalled. Its the 'slow' part of tcp_urg. It could be
 *	moved inline now as tcp_urg is only called from one
 *	place. We handle URGent data wrong. We have to - as
 *	BSD still doesn't use the correction from RFC961.
 *	For 1003.1g we should support a new option TCP_STDURG to permit
 *	either form.
 */
 
static void tcp_check_urg(struct sock * sk, struct tcphdr * th)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	u32 ptr = ntohs(th->urg_ptr);

	if (ptr)
		ptr--;
	ptr += ntohl(th->seq);

	/* ignore urgent data that we've already seen and read */
	if (after(sk->copied_seq, ptr))
		return;

	/* do we already have a newer (or duplicate) urgent pointer? */
	if (sk->urg_data && !after(ptr, sk->urg_seq))
		return;

	/* tell the world about our new urgent pointer */
	if (sk->proc != 0) {
		if (sk->proc > 0) {
			kill_proc(sk->proc, SIGURG, 1);
		} else {
			kill_pg(-sk->proc, SIGURG, 1);
		}
	}
	/*
	 *	We may be adding urgent data when the last byte read was
	 *	urgent. To do this requires some care. We cannot just ignore
	 *	sk->copied_seq since we would read the last urgent byte again
	 *	as data, nor can we alter copied_seq until this data arrives
	 *	or we break the sematics of SIOCATMARK (and thus sockatmark())
	 */
	if (sk->urg_seq == sk->copied_seq)
		sk->copied_seq++;	/* Move the copied sequence on correctly */
	sk->urg_data = URG_NOTYET;
	sk->urg_seq = ptr;

	/* disable header prediction */
	tp->pred_flags = 0;
}

/*
 *	This is the 'fast' part of urgent handling.
 */
 
static inline void tcp_urg(struct sock *sk, struct tcphdr *th, unsigned long len)
{
	/*
	 *	Check if we get a new urgent pointer - normally not 
	 */
	 
	if (th->urg)
		tcp_check_urg(sk,th);

	/*
	 *	Do we wait for any urgent data? - normally not
	 */
	 
	if (sk->urg_data == URG_NOTYET) {
		u32 ptr;

		/*
		 *	Is the urgent pointer pointing into this packet? 
		 */	 
		ptr = sk->urg_seq - ntohl(th->seq) + th->doff*4;
		if (ptr < len) {
			sk->urg_data = URG_VALID | *(ptr + (unsigned char *) th);
			if (!sk->dead)
				sk->data_ready(sk,0);
		}
	}
}


static void prune_queue(struct sock *sk)
{
	struct sk_buff * skb;

	/*
	 *	clean the out_of_order queue
	 */

	while ((skb = skb_dequeue(&sk->out_of_order_queue))) 
	{
		kfree_skb(skb, FREE_READ);
	}
}


int tcp_rcv_established(struct sock *sk, struct sk_buff *skb,
			struct tcphdr *th, __u16 len)
{
	struct tcp_opt *tp;
	int queued = 0;
	u32 flg;
	
	/*
	 *	Header prediction.
	 *	The code follows the one in the famous 
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

	tp = &(sk->tp_pinfo.af_tcp); 
	flg = *(((u32 *)th) + 3);
		
	/*
	 *	pred_flags is 0x5?10 << 16 + snd_wnd
	 *	if header_predition is to be made
	 *	? will be 0 else it will be !0
	 *	(when there are holes in the receive 
	 *	 space for instance)
	 */

	if (flg == tp->pred_flags && skb->seq == tp->rcv_nxt)
	{
		if (len <= sizeof(struct tcphdr))
		{
			if (len == sizeof(struct tcphdr))
			{
				tcp_ack(sk, th, skb->seq, skb->ack_seq, len);
				tcp_data_snd_check(sk);
			}

			kfree_skb(skb, FREE_READ);
			return 0;
		}
		else if (skb->ack_seq == tp->snd_una)
		{
			/* 
			 * Bulk data transfer: receiver 
			 */
			
			skb_pull(skb,sizeof(struct tcphdr));
			
			skb_queue_tail(&sk->receive_queue, skb);
			tp->rcv_nxt = skb->end_seq;
			sk->bytes_rcv += len - sizeof(struct tcphdr);
			
			sk->data_ready(sk, 0);
			tcp_delack_estimator(tp);

			if (sk->delayed_acks++ == 0)
			{
				tcp_send_delayed_ack(sk, HZ/2);
			}
			else
			{
				tcp_send_ack(sk);
			}

			return 0;
		}
	}

	if (!tcp_sequence(tp, skb->seq, skb->end_seq))
	{
		if (!th->rst)
		{
			if (after(skb->seq, tp->rcv_nxt))
			{
				SOCK_DEBUG(sk, "seq:%d end:%d wup:%d wnd:%d\n",
					   skb->seq, skb->end_seq,
					   tp->rcv_wup, tp->rcv_wnd);
			}
			tcp_send_ack(sk);
			kfree_skb(skb, FREE_READ);
			return 0;
		}
	}

	if(th->syn && skb->seq != sk->syn_seq)
	{
		printk(KERN_DEBUG "syn in established state\n");
		tcp_reset(sk, skb);
		kfree_skb(skb, FREE_READ);
		return 1;
	}
	
	if(th->rst)
	{
		tcp_reset(sk,skb);
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	
	if(th->ack)
	{
		tcp_ack(sk, th, skb->seq, skb->ack_seq, len);
	}

	
	/*
	 *	Process urgent data
	 */

	tcp_urg(sk, th, len);

	/*
	 *	step 7: process the segment text
	 */


	queued = tcp_data(skb, sk, len);

	/*
	 *	step 8: check the FIN bit
	 */

	if (th->fin)
	{
		tcp_fin(skb, sk, th);
	}

	tcp_data_snd_check(sk);
	tcp_ack_snd_check(sk);

	/*
	 *	If our receive queue has grown past its limits,
	 *	try to prune away duplicates etc..
	 */
	if (atomic_read(&sk->rmem_alloc) > sk->rcvbuf)
		prune_queue(sk);

	/*
	 *	And done
	 */	
	
	if (!queued)
		kfree_skb(skb, FREE_READ);
	return 0;
}
		

/*
 *	This function implements the receiving procedure of RFC 793.
 *	It's called from both tcp_v4_rcv and tcp_v6_rcv and should be
 *	address independent.
 */
	
int tcp_rcv_state_process(struct sock *sk, struct sk_buff *skb,
			  struct tcphdr *th, void *opt, __u16 len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int queued = 0;
	int rcv_mss;

	/*
	 *	state == CLOSED
	 *	Hash lookup always fails, so no worries. -DaveM 
	 */

	switch (sk->state) {


	case TCP_LISTEN:
		
		if (th->rst)			
			goto discard;

		/* 
		 * These use the socket TOS.. 
		 * might want to be the received TOS 
		 */

		if(th->ack)
		{	
			/*
			 *  send reset
			 */

			return 1;
		}
		
		
		if(th->syn)
		{
			int err;
			__u32 isn;

			isn = tp->af_specific->init_sequence(sk, skb);
			err = tp->af_specific->conn_request(sk, skb, opt, isn);

			if (err < 0)
				return 1;

			/*
			 *  Now we have several options: In theory there is 
			 *  nothing else in the frame. KA9Q has an option to 
			 *  send data with the syn, BSD accepts data with the
			 *  syn up to the [to be] advertised window and 
			 *  Solaris 2.1 gives you a protocol error. For now 
			 *  we just ignore it, that fits the spec precisely 
			 *  and avoids incompatibilities. It would be nice in
			 *  future to drop through and process the data.
			 *
			 *  Now that TTCP is starting to be used we ought to 
			 *  queue this data.
			 */

			return 0;
		}
		
		goto discard;
		break;

	case TCP_SYN_SENT:
		
		/*
		 *	SYN sent means we have to look for a suitable ack and 
		 *	either reset for bad matches or go to connected. 
		 *	The SYN_SENT case is unusual and should
		 *	not be in line code. [AC]
		 */
	   
		if(th->ack)
		{
			tp->snd_wl1 = skb->seq;

			/* We got an ack, but it's not a good ack */
			if(!tcp_ack(sk,th, skb->seq, skb->ack_seq, len))
			{
				tcp_statistics.TcpAttemptFails++;
				return 1;
			}

			if(th->rst)
			{
				tcp_reset(sk,skb);
				goto discard;
			}

			if(!th->syn)
			{
				/* 
				 *  A valid ack from a different connection
				 *  start. Shouldn't happen but cover it 
				 */
				tcp_statistics.TcpAttemptFails++;
				return 1;
			}

			/*
			 *	Ok.. it's good. Set up sequence 
			 *	numbers and
			 *	move to established.
			 */

			tp->rcv_nxt = skb->seq+1;
			tp->rcv_wnd = 0;
			tp->rcv_wup = skb->seq+1;

			tp->snd_wnd = htons(th->window);
			tp->snd_wl1 = skb->seq;
			tp->snd_wl2 = skb->ack_seq;

			sk->fin_seq = skb->seq;
			tcp_send_ack(sk);

			tcp_set_state(sk, TCP_ESTABLISHED);
			rcv_mss = tcp_parse_options(th);
			
			if (rcv_mss)
				sk->mss = min(sk->mss, rcv_mss);
			
			sk->dummy_th.dest = th->source;
			sk->copied_seq = tp->rcv_nxt;

			if(!sk->dead)
			{
				sk->state_change(sk);
				sock_wake_async(sk->socket, 0);
			}

			/* Drop through step 6 */
			goto step6;
		}
		else
		{
			if(th->syn && !th->rst)
			{
				/* 
				 * the previous version of the code
				 * checked for "connecting to self"
				 * here. that check is done now in
				 * tcp_connect
				 */

				tcp_set_state(sk, TCP_SYN_RECV);
				
				tp->rcv_nxt = skb->seq + 1;
				tp->rcv_wup = skb->seq + 1;

				tp->snd_wnd = htons(th->window);
				tp->snd_wl1 = skb->seq;
				
				tcp_send_synack(sk);
				goto discard;
			}		

		}
		break;

	case TCP_TIME_WAIT:
	        /*
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

		if (th->syn && !th->rst && after(skb->seq, tp->rcv_nxt))
		{
			__u32 isn;
			int err;

			skb_orphan(skb);
                        sk->err = ECONNRESET;
                        tcp_set_state(sk, TCP_CLOSE);
                        sk->shutdown = SHUTDOWN_MASK;

			isn = tp->rcv_nxt + 128000;

			sk = tp->af_specific->get_sock(skb, th);

			if (sk == NULL || !ipsec_sk_policy(sk,skb))
				goto discard;

			skb_set_owner_r(skb, sk);
			tp = &sk->tp_pinfo.af_tcp;
			
			err = tp->af_specific->conn_request(sk, skb, opt, isn);

			if (err < 0)
				return 1;

			return 0;
		}

		break;

	}

	/*
	 *	step 1: check sequence number
	 */

	if (!tcp_sequence(tp, skb->seq, skb->end_seq))
	{
		if (!th->rst)
		{
			tcp_send_ack(sk);
			goto discard;
		}
	}


	/*
	 *	step 2: check RST bit
	 */

	if(th->rst)
	{
		tcp_reset(sk,skb);
		goto discard;
	}

	/*
	 *	step 3: check security and precedence 
	 *	[ignored]
	 */

	/*
	 *	step 4:
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

	if (th->syn && skb->seq!=sk->syn_seq)
	{
		tcp_reset(sk, skb);
		return 1;
	}

	/*
	 *	step 5: check the ACK field
	 */

	if (th->ack) 
	{
		int acceptable = tcp_ack(sk,th,skb->seq, skb->ack_seq,len);
		
		switch(sk->state) {
		case TCP_SYN_RECV:
			if (acceptable)
			{
				tcp_set_state(sk, TCP_ESTABLISHED);
				sk->dummy_th.dest=th->source;
				sk->copied_seq = tp->rcv_nxt;

				if(!sk->dead)
					sk->state_change(sk);		

				tp->snd_una = skb->ack_seq;
				tp->snd_wnd = htons(th->window);
				tp->snd_wl1 = skb->seq;
				tp->snd_wl2 = skb->ack_seq;

			}
			else
				return 1;
			break;

		case TCP_FIN_WAIT1:

			if (tp->snd_una == sk->write_seq) 
			{
				sk->shutdown |= SEND_SHUTDOWN;
				tcp_set_state(sk, TCP_FIN_WAIT2);
				if (!sk->dead)
					sk->state_change(sk);
			}
			break;

		case TCP_CLOSING:	

			if (tp->snd_una == sk->write_seq)
			{
				tcp_time_wait(sk);
			}
			break;

		case TCP_LAST_ACK:

			if (tp->snd_una == sk->write_seq)
			{
				sk->shutdown = SHUTDOWN_MASK;
				tcp_set_state(sk,TCP_CLOSE);
				if (!sk->dead)
					sk->state_change(sk);
				goto discard;
			}
			break;

		case TCP_TIME_WAIT:
			/*
			 * keep us in TIME_WAIT until we stop getting 
			 * packets, reset the timeout.
			 */
			tcp_reset_msl_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
			break;

		}
	}
	else
		goto discard;

  step6:

	/*
	 *	step 6: check the URG bit
	 */

	tcp_urg(sk, th, len);

	/*
	 *	step 7: process the segment text
	 */

	switch (sk->state) {
	case TCP_CLOSE_WAIT:
	case TCP_CLOSING:
		if (!before(skb->seq, sk->fin_seq))
			break;
	
	case TCP_FIN_WAIT1:
	case TCP_FIN_WAIT2:

		/*
		 *	RFC 793 says to queue data in this states,
		 *	RFC 1122 says we MUST send a reset. 
		 *	BSD 4.4 also does reset.
		 */

		if ((sk->shutdown & RCV_SHUTDOWN) && sk->dead)
		{
			if (after(skb->end_seq - th->fin, tp->rcv_nxt))
			{
				tcp_reset(sk, skb);
				return 1;
			}
		}
		
	case TCP_ESTABLISHED:
		queued = tcp_data(skb, sk, len);
		break;
	}

	/*
	 *	step 8: check the FIN bit
	 */

	if (th->fin)
	{
		tcp_fin(skb, sk, th);
	}

	tcp_data_snd_check(sk);
	tcp_ack_snd_check(sk);

	if (queued)
		return 0;
  discard:

	kfree_skb(skb, FREE_READ);
	return 0;
}

int tcp_sysctl_congavoid(ctl_table *ctl, int write, struct file * filp,
			 void *buffer, size_t *lenp)
{
	int val = sysctl_tcp_cong_avoidance;
	int retv;

	retv = proc_dointvec(ctl, write, filp, buffer, lenp);

	if (write)
	{
		switch (sysctl_tcp_cong_avoidance) {
			case 0:
				tcp_sys_cong_ctl_f = &tcp_cong_avoid_vanj;
				break;
			case 1:
				tcp_sys_cong_ctl_f = &tcp_cong_avoid_vegas;
				break;
			default:
				retv = -EINVAL;
				sysctl_tcp_cong_avoidance = val;
		}
	}

	return retv;
}
