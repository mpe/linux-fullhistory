/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	@(#)tcp_input.c	1.0.16	05/25/93
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
 *
 * FIXES
 *		Pedro Roque	:	Double ACK bug
 *		Eric Schenk	:	Fixes to slow start algorithm.
 *		Eric Schenk	:	Yet another double ACK bug.
 *		Eric Schenk	:	Delayed ACK bug fixes.
 *		Eric Schenk	:	Floyd style fast retrans war avoidance.
 *		Eric Schenk	: 	Skip fast retransmit on small windows.
 *		Eric schenk	:	Fixes to retransmission code to
 *				:	avoid extra retransmission.
 */

#include <linux/config.h>
#include <net/tcp.h>

/*
 *	Policy code extracted so it's now separate
 */

/*
 *	Called each time to estimate the delayed ack timeout. This is
 *	how it should be done so a fast link isn't impacted by ack delay.
 */
 
extern __inline__ void tcp_delack_estimator(struct sock *sk)
{
	/*
	 *	Delayed ACK time estimator.
	 */
	
	if (sk->lrcvtime == 0) 
	{
		sk->lrcvtime = jiffies;
		sk->ato = HZ/3;
	}
	else 
	{
		int m;
		
		m = jiffies - sk->lrcvtime;

		sk->lrcvtime = jiffies;

		if (m <= 0)
			m = 1;

		/* This used to test against sk->rtt.
		 * On a purely receiving link, there is no rtt measure.
		 * The result is that we lose delayed ACKs on one-way links.
		 * Therefore we test against sk->rto, which will always
		 * at least have a default value.
		 */
		if (m > sk->rto)
		{
			sk->ato = sk->rto;
			/*
			 * printk(KERN_DEBUG "ato: rtt %lu\n", sk->ato);
			 */
		}
		else 
		{
			/*
		 	 * Very fast acting estimator.
		 	 * May fluctuate too much. Probably we should be
			 * doing something like the rtt estimator here.
			 */
			sk->ato = (sk->ato >> 1) + m;
			/*
			 * printk(KERN_DEBUG "ato: m %lu\n", sk->ato);
			 */
		}
	}
}

/*
 *	Called on frames that were known _not_ to have been
 *	retransmitted [see Karn/Partridge Proceedings SIGCOMM 87]. 
 *	The algorithm is from the SIGCOMM 88 piece by Van Jacobson.
 */
 
extern __inline__ void tcp_rtt_estimator(struct sock *sk, struct sk_buff *oskb)
{
	long m;
	/*
	 *	The following amusing code comes from Jacobson's
	 *	article in SIGCOMM '88.  Note that rtt and mdev
	 *	are scaled versions of rtt and mean deviation.
	 *	This is designed to be as fast as possible 
	 *	m stands for "measurement".
	 */
	
	m = jiffies - oskb->when;  /* RTT */

	if (sk->rtt != 0) {
		if(m<=0)
			m=1;		/* IS THIS RIGHT FOR <0 ??? */
		m -= (sk->rtt >> 3);    /* m is now error in rtt est */
		sk->rtt += m;           /* rtt = 7/8 rtt + 1/8 new */
		if (m < 0)
			m = -m;		/* m is now abs(error) */
		m -= (sk->mdev >> 2);   /* similar update on mdev */
		sk->mdev += m;	    	/* mdev = 3/4 mdev + 1/4 new */
	} else {
		/* no previous measure. */
		sk->rtt = m<<3;		/* take the measured time to be rtt */
		sk->mdev = m<<1;	/* make sure rto = 3*rtt */
	}

	/*
	 *	Now update timeout.  Note that this removes any backoff.
	 */
			 
	/* Jacobson's algorithm calls for rto = R + 4V.
	 * We diverge from Jacobson's algorithm here. See the commentary
	 * in tcp_ack to understand why.
	 */
	sk->rto = (sk->rtt >> 3) + sk->mdev;
	sk->rto += (sk->rto>>2) + (sk->rto >> (sk->cong_window-1));
	if (sk->rto > 120*HZ)
		sk->rto = 120*HZ;
	if (sk->rto < HZ/5)	/* Was 1*HZ - keep .2 as minimum cos of the BSD delayed acks */
		sk->rto = HZ/5;
	sk->backoff = 0;
}

/*
 *	Cached last hit socket
 */
 
static volatile unsigned long 	th_cache_saddr, th_cache_daddr;
static volatile unsigned short  th_cache_dport, th_cache_sport;
static volatile struct sock *th_cache_sk;

void tcp_cache_zap(void)
{
	th_cache_sk=NULL;
}

/*
 *	Find the socket, using the last hit cache if applicable. The cache is not quite
 *	right...
 */

static inline struct sock * get_tcp_sock(u32 saddr, u16 sport, u32 daddr, u16 dport, u32 paddr, u16 pport)
{
	struct sock * sk;

	sk = (struct sock *) th_cache_sk;
	if (!sk || saddr != th_cache_saddr || daddr != th_cache_daddr ||
	    sport != th_cache_sport || dport != th_cache_dport) {
		sk = get_sock(&tcp_prot, dport, saddr, sport, daddr, paddr, pport);
		if (sk) {
			th_cache_saddr=saddr;
			th_cache_daddr=daddr;
  			th_cache_dport=dport;
			th_cache_sport=sport;
			th_cache_sk=sk;
		}
	}
	return sk;
}

/*
 * React to a out-of-window TCP sequence number in an incoming packet
 */
 
static void bad_tcp_sequence(struct sock *sk, struct tcphdr *th, u32 end_seq,
	      struct device *dev)
{
	if (th->rst)
		return;

	/*
	 *	Send a reset if we get something not ours and we are
	 *	unsynchronized. Note: We don't do anything to our end. We
	 *	are just killing the bogus remote connection then we will
	 *	connect again and it will work (with luck).
	 */
  	 
	if (sk->state==TCP_SYN_SENT || sk->state==TCP_SYN_RECV) 
	{
		tcp_send_reset(sk->saddr,sk->daddr,th,sk->prot,NULL,dev, sk->ip_tos,sk->ip_ttl);
		return;
	}

	/*
	 * 	This packet is old news. Usually this is just a resend
	 * 	from the far end, but sometimes it means the far end lost
	 *	an ACK we sent, so we better send an ACK.
	 */
	tcp_send_ack(sk);
}

/*
 *	This functions checks to see if the tcp header is actually acceptable. 
 */
 
extern __inline__ int tcp_sequence(struct sock *sk, u32 seq, u32 end_seq)
{
	u32 end_window = sk->lastwin_seq + sk->window;
	return	/* if start is at end of window, end must be too (zero window) */
		(seq == end_window && seq == end_seq) ||
		/* if start is before end of window, check for interest */
		(before(seq, end_window) && !before(end_seq, sk->acked_seq));
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
	sk->err = ECONNRESET;
	if (sk->state == TCP_SYN_SENT)
		sk->err = ECONNREFUSED;
	if (sk->state == TCP_CLOSE_WAIT)
		sk->err = EPIPE;
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
	kfree_skb(skb, FREE_READ);
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
 
static void tcp_options(struct sock *sk, struct tcphdr *th)
{
	unsigned char *ptr;
	int length=(th->doff*4)-sizeof(struct tcphdr);
	int mss_seen = 0;
    
	ptr = (unsigned char *)(th + 1);
  
	while(length>0)
	{
	  	int opcode=*ptr++;
	  	int opsize=*ptr++;
	  	switch(opcode)
	  	{
	  		case TCPOPT_EOL:
	  			return;
	  		case TCPOPT_NOP:	/* Ref: RFC 793 section 3.1 */
	  			length--;
	  			ptr--;		/* the opsize=*ptr++ above was a mistake */
	  			continue;
	  		
	  		default:
	  			if(opsize<=2)	/* Avoid silly options looping forever */
	  				return;
	  			switch(opcode)
	  			{
	  				case TCPOPT_MSS:
	  					if(opsize==4 && th->syn)
	  					{
	  						sk->mtu=min(sk->mtu,ntohs(*(unsigned short *)ptr));
							mss_seen = 1;
	  					}
	  					break;
		  				/* Add other options here as people feel the urge to implement stuff like large windows */
	  			}
	  			ptr+=opsize-2;
	  			length-=opsize;
	  	}
	}
	if (th->syn) 
	{
		if (! mss_seen)
		      sk->mtu=min(sk->mtu, 536);  /* default MSS if none sent */
	}
#ifdef CONFIG_INET_PCTCP
	sk->mss = min(sk->max_window >> 1, sk->mtu);
#else    
	sk->mss = min(sk->max_window, sk->mtu);
	sk->max_unacked = 2 * sk->mss;
#endif  
}


/*
 *	This routine handles a connection request.
 *	It should make sure we haven't already responded.
 *	Because of the way BSD works, we have to send a syn/ack now.
 *	This also means it will be harder to close a socket which is
 *	listening.
 */
 
static void tcp_conn_request(struct sock *sk, struct sk_buff *skb,
		 u32 daddr, u32 saddr, struct options *opt, struct device *dev, u32 seq)
{
	struct sock *newsk;
	struct tcphdr *th;
	struct rtable *rt;
  
	th = skb->h.th;

	/* If the socket is dead, don't accept the connection. */
	if (!sk->dead) 
	{
  		sk->data_ready(sk,0);
	}
	else 
	{
		if(sk->debug)
			printk("Reset on %p: Connect on dead socket.\n",sk);
		tcp_send_reset(daddr, saddr, th, sk->prot, opt, dev, sk->ip_tos,sk->ip_ttl);
		tcp_statistics.TcpAttemptFails++;
		kfree_skb(skb, FREE_READ);
		return;
	}

	/*
	 *	Make sure we can accept more.  This will prevent a
	 *	flurry of syns from eating up all our memory.
	 *
	 *	BSD does some funnies here and allows 3/2 times the
	 *	set backlog as a fudge factor. That's just too gross.
	 */

	if (sk->ack_backlog >= sk->max_ack_backlog) 
	{
		tcp_statistics.TcpAttemptFails++;
		kfree_skb(skb, FREE_READ);
		return;
	}

	/*
	 * We need to build a new sock struct.
	 * It is sort of bad to have a socket without an inode attached
	 * to it, but the wake_up's will just wake up the listening socket,
	 * and if the listening socket is destroyed before this is taken
	 * off of the queue, this will take care of it.
	 */

	newsk = (struct sock *) kmalloc(sizeof(struct sock), GFP_ATOMIC);
	if (newsk == NULL) 
	{
		/* just ignore the syn.  It will get retransmitted. */
		tcp_statistics.TcpAttemptFails++;
		kfree_skb(skb, FREE_READ);
		return;
	}

	memcpy(newsk, sk, sizeof(*newsk));
	newsk->opt = NULL;
	newsk->ip_route_cache  = NULL;
	if (opt && opt->optlen) 
	{
		sk->opt = (struct options*)kmalloc(sizeof(struct options)+opt->optlen, GFP_ATOMIC);
		if (!sk->opt) 
		{
	        	kfree_s(newsk, sizeof(struct sock));
			tcp_statistics.TcpAttemptFails++;
			kfree_skb(skb, FREE_READ);
			return;
		}
		if (ip_options_echo(sk->opt, opt, daddr, saddr, skb)) 
		{
			kfree_s(sk->opt, sizeof(struct options)+opt->optlen);
	        	kfree_s(newsk, sizeof(struct sock));
			tcp_statistics.TcpAttemptFails++;
			kfree_skb(skb, FREE_READ);
			return;
		}
	}
	skb_queue_head_init(&newsk->write_queue);
	skb_queue_head_init(&newsk->receive_queue);
	newsk->send_head = NULL;
	newsk->send_tail = NULL;
	newsk->send_next = NULL;
	skb_queue_head_init(&newsk->back_log);
	newsk->rtt = 0;
	newsk->rto = TCP_TIMEOUT_INIT;
	newsk->mdev = TCP_TIMEOUT_INIT;
	newsk->max_window = 0;
	/*
	 * See draft-stevens-tcpca-spec-01 for discussion of the
	 * initialization of these values.
	 */
	newsk->cong_window = 1;
	newsk->cong_count = 0;
	newsk->ssthresh = 0x7fffffff;

	newsk->lrcvtime = 0;
	newsk->idletime = 0;
	newsk->high_seq = 0;
	newsk->backoff = 0;
	newsk->blog = 0;
	newsk->intr = 0;
	newsk->proc = 0;
	newsk->done = 0;
	newsk->partial = NULL;
	newsk->pair = NULL;
	newsk->wmem_alloc = 0;
	newsk->rmem_alloc = 0;
	newsk->localroute = sk->localroute;

	newsk->max_unacked = MAX_WINDOW - TCP_WINDOW_DIFF;

	newsk->err = 0;
	newsk->shutdown = 0;
	newsk->ack_backlog = 0;
	newsk->acked_seq = skb->seq+1;
	newsk->lastwin_seq = skb->seq+1;
	newsk->delay_acks = 1;
	newsk->copied_seq = skb->seq+1;
	newsk->fin_seq = skb->seq;
	newsk->state = TCP_SYN_RECV;
	newsk->timeout = 0;
	newsk->ip_xmit_timeout = 0;
	newsk->write_seq = seq; 
	newsk->window_seq = newsk->write_seq;
	newsk->rcv_ack_seq = newsk->write_seq;
	newsk->urg_data = 0;
	newsk->retransmits = 0;
	newsk->linger=0;
	newsk->destroy = 0;
	init_timer(&newsk->timer);
	newsk->timer.data = (unsigned long)newsk;
	newsk->timer.function = &net_timer;
	init_timer(&newsk->delack_timer);
	newsk->delack_timer.data = (unsigned long)newsk;
	newsk->delack_timer.function = tcp_delack_timer;
	init_timer(&newsk->retransmit_timer);
	newsk->retransmit_timer.data = (unsigned long)newsk;
	newsk->retransmit_timer.function = tcp_retransmit_timer;
	newsk->dummy_th.source = skb->h.th->dest;
	newsk->dummy_th.dest = skb->h.th->source;
	
#ifdef CONFIG_IP_TRANSPARENT_PROXY
	/* 
	 *	Deal with possibly redirected traffic by setting num to
	 *	the intended destination port of the received packet.
	 */
	newsk->num = ntohs(skb->h.th->dest);

#endif
	/*
	 *	Swap these two, they are from our point of view. 
	 */
	 
	newsk->daddr = saddr;
	newsk->saddr = daddr;
	newsk->rcv_saddr = daddr;

	put_sock(newsk->num,newsk);
	newsk->acked_seq = skb->seq + 1;
	newsk->copied_seq = skb->seq + 1;
	newsk->socket = NULL;

	/*
	 *	Grab the ttl and tos values and use them 
	 */

	newsk->ip_ttl=sk->ip_ttl;
	newsk->ip_tos=skb->ip_hdr->tos;

	/*
	 *	Use 512 or whatever user asked for 
	 */

	/*
	 * 	Note use of sk->user_mss, since user has no direct access to newsk 
	 */

	rt = ip_rt_route(newsk->opt && newsk->opt->srr ? newsk->opt->faddr : saddr, 0);
	newsk->ip_route_cache = rt;
	
	if(rt!=NULL && (rt->rt_flags&RTF_WINDOW))
		newsk->window_clamp = rt->rt_window;
	else
		newsk->window_clamp = 0;
		
	if (sk->user_mss)
		newsk->mtu = sk->user_mss;
	else if (rt)
		newsk->mtu = rt->rt_mtu - sizeof(struct iphdr) - sizeof(struct tcphdr);
	else 
		newsk->mtu = 576 - sizeof(struct iphdr) - sizeof(struct tcphdr);

	/*
	 *	But not bigger than device MTU 
	 */

	newsk->mtu = min(newsk->mtu, dev->mtu - sizeof(struct iphdr) - sizeof(struct tcphdr));

#ifdef CONFIG_SKIP
	
	/*
	 *	SKIP devices set their MTU to 65535. This is so they can take packets
	 *	unfragmented to security process then fragment. They could lie to the
	 *	TCP layer about a suitable MTU, but it's easier to let skip sort it out
	 *	simply because the final package we want unfragmented is going to be
	 *
	 *	[IPHDR][IPSP][Security data][Modified TCP data][Security data]
	 */
	 
	if(skip_pick_mtu!=NULL)		/* If SKIP is loaded.. */
		sk->mtu=skip_pick_mtu(sk->mtu,dev);
#endif
	/*
	 *	This will min with what arrived in the packet 
	 */

	tcp_options(newsk,skb->h.th);
	
	tcp_cache_zap();
	tcp_send_synack(newsk, sk, skb);
}


/*
 * Handle a TCP window that shrunk on us. It shouldn't happen,
 * but..
 *
 * We may need to move packets from the send queue
 * to the write queue, if the window has been shrunk on us.
 * The RFC says you are not allowed to shrink your window
 * like this, but if the other end does, you must be able
 * to deal with it.
 */
void tcp_window_shrunk(struct sock * sk, u32 window_seq)
{
	struct sk_buff *skb;
	struct sk_buff *skb2;
	struct sk_buff *wskb = NULL;
 	
	skb2 = sk->send_head;
	sk->send_head = NULL;
	sk->send_tail = NULL;
	sk->send_next = NULL;

	/*
	 *	This is an artifact of a flawed concept. We want one
	 *	queue and a smarter send routine when we send all.
	 */
	cli();
	while (skb2 != NULL) 
	{
		skb = skb2;
		skb2 = skb->link3;
		skb->link3 = NULL;
		if (after(skb->end_seq, window_seq)) 
		{
			if (sk->packets_out > 0) 
				sk->packets_out--;
			/* We may need to remove this from the dev send list. */
			if (skb->next != NULL) 
			{
				skb_unlink(skb);				
			}
			/* Now add it to the write_queue. */
			if (wskb == NULL)
				skb_queue_head(&sk->write_queue,skb);
			else
				skb_append(wskb,skb);
			wskb = skb;
		} 
		else 
		{
			if (sk->send_head == NULL) 
			{
				sk->send_head = skb;
				sk->send_tail = skb;
				sk->send_next = skb;
			}
			else
			{
				sk->send_tail->link3 = skb;
				sk->send_tail = skb;
			}
			skb->link3 = NULL;
		}
	}
	sti();
}


/*
 *	This routine deals with incoming acks, but not outgoing ones.
 *
 *	This routine is totally _WRONG_. The list structuring is wrong,
 *	the algorithm is wrong, the code is wrong.
 */

static int tcp_ack(struct sock *sk, struct tcphdr *th, u32 ack, int len)
{
	int flag = 0;
	u32 window_seq;

	/* 
	 * 1 - there was data in packet as well as ack or new data is sent or 
	 *     in shutdown state
	 * 2 - data from retransmit queue was acked and removed
	 * 4 - window shrunk or data from retransmit queue was acked and removed
	 */

	if(sk->zapped)
		return(1);	/* Dead, can't ack any more so why bother */

	/*
	 *	We have dropped back to keepalive timeouts. Thus we have
	 *	no retransmits pending.
	 */
	 
	if (sk->ip_xmit_timeout == TIME_KEEPOPEN)
	  	sk->retransmits = 0;

	/*
	 *	If the ack is newer than sent or older than previous acks
	 *	then we can probably ignore it.
	 */
	 
	if (after(ack, sk->sent_seq) || before(ack, sk->rcv_ack_seq)) 
		goto uninteresting_ack;

	/*
	 *	Have we discovered a larger window
	 */
	window_seq = ntohs(th->window);
	if (window_seq > sk->max_window) 
	{
  		sk->max_window = window_seq;
#ifdef CONFIG_INET_PCTCP
		/* Hack because we don't send partial packets to non SWS
		   handling hosts */
		sk->mss = min(window_seq>>1, sk->mtu);
#else
		sk->mss = min(window_seq, sk->mtu);
#endif	
	}
	window_seq += ack;

	/*
	 *	See if our window has been shrunk. 
	 */
	if (after(sk->window_seq, window_seq))
		tcp_window_shrunk(sk, window_seq);

	/*
	 *	Pipe has emptied
	 */	 
	if (sk->send_tail == NULL || sk->send_head == NULL) 
	{
		sk->send_head = NULL;
		sk->send_tail = NULL;
		sk->send_next = NULL;
		sk->packets_out= 0;
	}

	/*
	 *	We don't want too many packets out there. 
	 */
	 
	if (sk->ip_xmit_timeout == TIME_WRITE && 
		sk->cong_window < 2048 && after(ack, sk->rcv_ack_seq)) 
	{
		
		/* 
		 * This is Jacobson's slow start and congestion avoidance. 
		 * SIGCOMM '88, p. 328.  Because we keep cong_window in integral
		 * mss's, we can't do cwnd += 1 / cwnd.  Instead, maintain a 
		 * counter and increment it once every cwnd times.  It's possible
		 * that this should be done only if sk->retransmits == 0.  I'm
		 * interpreting "new data is acked" as including data that has
		 * been retransmitted but is just now being acked.
		 */
		if (sk->cong_window <= sk->ssthresh)
			/* 
			 *	In "safe" area, increase
			 */
			sk->cong_window++;
		else 
		{
			/*
			 *	In dangerous area, increase slowly.  In theory this is
			 *  	sk->cong_window += 1 / sk->cong_window
			 */
			if (sk->cong_count >= sk->cong_window) 
			{
				sk->cong_window++;
				sk->cong_count = 0;
			}
			else 
				sk->cong_count++;
		}
	}

	/*
	 *	Remember the highest ack received and update the
	 *	right hand window edge of the host.
	 *	We do a bit of work here to track number of times we've
	 *	seen this ack without a change in the right edge of the
	 *	window and no data in the packet.
	 *	This will allow us to do fast retransmits.
	 */

	/* We are looking for duplicate ACKs here.
	 * An ACK is a duplicate if:
	 * (1) it has the same sequence number as the largest number we've seen,
	 * (2) it has the same window as the last ACK,
	 * (3) we have outstanding data that has not been ACKed
	 * (4) The packet was not carrying any data.
	 * (5) [From Floyd's paper on fast retransmit wars]
	 *     The packet acked data after high_seq;
	 * I've tried to order these in occurrence of most likely to fail
	 * to least likely to fail.
	 * [These are an extension of the rules BSD stacks use to
	 *  determine if an ACK is a duplicate.]
	 */

	if (sk->rcv_ack_seq == ack
		&& sk->window_seq == window_seq
		&& len != th->doff*4
		&& before(ack, sk->sent_seq)
		&& after(ack, sk->high_seq))
	{
		/* Prevent counting of duplicate ACKs if the congestion
		 * window is smaller than 3. Note that since we reduce
		 * the congestion window when we do a fast retransmit,
		 * we must be careful to keep counting if we were already
		 * counting. The idea behind this is to avoid doing
		 * fast retransmits if the congestion window is so small
		 * that we cannot get 3 ACKs due to the loss of a packet
		 * unless we are getting ACKs for retransmitted packets.
		 */
		if (sk->cong_window >= 3 || sk->rcv_ack_cnt > MAX_DUP_ACKS+1)
			sk->rcv_ack_cnt++;
		/* See draft-stevens-tcpca-spec-01 for explanation
		 * of what we are doing here.
		 */
		if (sk->rcv_ack_cnt == MAX_DUP_ACKS+1) {
			int tmp;

			/* We need to be a bit careful to preserve the
			 * count of packets that are out in the system here.
			 */
			sk->ssthresh = max(sk->cong_window >> 1, 2);
			sk->cong_window = sk->ssthresh+MAX_DUP_ACKS+1;
			tmp = sk->packets_out;
			tcp_do_retransmit(sk,0);
			sk->packets_out = tmp;
		} else if (sk->rcv_ack_cnt > MAX_DUP_ACKS+1) {
			sk->cong_window++;
			/*
			* At this point we are suppose to transmit a NEW
			* packet (not retransmit the missing packet,
			* this would only get us into a retransmit war.)
			* I think that having just adjusted cong_window
			* we will transmit the new packet below.
			*/
		}
	}
	else
	{
		if (sk->rcv_ack_cnt > MAX_DUP_ACKS) {
			sk->cong_window = sk->ssthresh;
		}
		sk->window_seq = window_seq;
		sk->rcv_ack_seq = ack;
		sk->rcv_ack_cnt = 1;
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

	if (sk->ip_xmit_timeout == TIME_PROBE0) 
	{
		sk->retransmits = 0;	/* Our probe was answered */
		
		/*
		 *	Was it a usable window open ?
		 */
		 
  		if (!skb_queue_empty(&sk->write_queue) &&   /* should always be true */
		    ! before (sk->window_seq, sk->write_queue.next->end_seq)) 
		{
			sk->backoff = 0;
			
			/*
			 *	Recompute rto from rtt.  this eliminates any backoff.
			 */

			/*
			 * Appendix C of Van Jacobson's final version of
			 * the SIGCOMM 88 paper states that although
			 * the original paper suggested that
			 *  RTO = R*2V
			 * was the correct calculation experience showed
			 * better results using
			 *  RTO = R*4V
			 * In particular this gives better performance over
			 * slow links, and should not effect fast links.
			 *
			 * Note: Jacobson's algorithm is fine on BSD which
			 * has a 1/2 second granularity clock, but with our
			 * 1/100 second granularity clock we become too
	 		 * sensitive to minor changes in the round trip time.
			 * We add in two compensating factors.
			 * First we multiply by 5/4. For large congestion
			 * windows this allows us to tolerate burst traffic
			 * delaying up to 1/4 of our packets.
			 * We also add in a rtt / cong_window term.
			 * For small congestion windows this allows
			 * a single packet delay, but has negligible effect
			 * on the compensation for large windows.
	 		 */
			sk->rto = (sk->rtt >> 3) + sk->mdev;
			sk->rto += (sk->rto>>2) + (sk->rto >> (sk->cong_window-1));
			if (sk->rto > 120*HZ)
				sk->rto = 120*HZ;
			if (sk->rto < HZ/5)	/* Was 1*HZ, then 1 - turns out we must allow about
						   .2 of a second because of BSD delayed acks - on a 100Mb/sec link
						   .2 of a second is going to need huge windows (SIGH) */
			sk->rto = HZ/5;
		}
	}

	/* 
	 *	See if we can take anything off of the retransmit queue.
	 */

	for (;;) {
		struct sk_buff * skb = sk->send_head;
		if (!skb)
			break;

		/* Check for a bug. */
		if (skb->link3 && after(skb->end_seq, skb->link3->end_seq)) 
			printk("INET: tcp.c: *** bug send_list out of order.\n");
			
		/*
		 *	If our packet is before the ack sequence we can
		 *	discard it as it's confirmed to have arrived the other end.
		 */
		 
		if (after(skb->end_seq, ack))
			break;

		if (sk->retransmits) 
		{
			/*
			 *	We were retransmitting.  don't count this in RTT est 
			 */
			flag |= 2;
		}

		if ((sk->send_head = skb->link3) == NULL)
		{
			sk->send_tail = NULL;
			sk->send_next = NULL;
			sk->retransmits = 0;
		}

		/*
		 * advance the send_next pointer if needed.
		 */
		if (sk->send_next == skb)
			sk->send_next = sk->send_head;

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

		/*
		 *	We have one less packet out there. 
		 */
			 
		if (sk->packets_out > 0) 
			sk->packets_out --;

		/* This is really only supposed to be called when we
		 * are actually ACKing new data, which should exclude
		 * the ACK handshake on an initial SYN packet as well.
		 * Rather than introducing a new test here for this
		 * special case, we just reset the initial values for
		 * rtt immediately after we move to the established state.
		 */
		if (!(flag&2)) 	/* Not retransmitting */
			tcp_rtt_estimator(sk,skb);
		IS_SKB(skb);

		/*
		 *	We may need to remove this from the dev send list. 
		 */
		cli();
		if (skb->next)
			skb_unlink(skb);
		sti();
		kfree_skb(skb, FREE_WRITE); /* write. */
		if (!sk->dead)
			sk->write_space(sk);
	}

	/*
	 * Maybe we can take some stuff off of the write queue,
	 * and put it onto the xmit queue.
	 * FIXME: (?) There is bizarre case being tested here, to check if
	 * the data at the head of the queue ends before the start of
	 * the sequence we already ACKed. This does not appear to be
	 * a case that can actually occur. Why are we testing it?
	 */

	if (!skb_queue_empty(&sk->write_queue) &&
	    	!before(sk->window_seq, sk->write_queue.next->end_seq) &&
		(sk->retransmits == 0 || 
		 sk->ip_xmit_timeout != TIME_WRITE ||
		 !after(sk->write_queue.next->end_seq, sk->rcv_ack_seq)) &&
		sk->packets_out < sk->cong_window)
	{
		/*
		 *	Add more data to the send queue.
		 */
		tcp_write_xmit(sk);
	}

	/*
	 * Reset timers to reflect the new state.
	 *
	 * from TIME_WAIT we stay in TIME_WAIT as long as we rx packets
	 * from TCP_CLOSE we don't do anything
	 *
	 * from anything else, if there is queued data (or fin) pending,
	 * we use a TIME_WRITE timeout, if there is data to write but
	 * no room in the window we use TIME_PROBE0, else if keepalive
	 * we reset to a KEEPALIVE timeout, else we delete the timer.
	 *
	 * We do not set flag for nominal write data, otherwise we may
	 * force a state where we start to write itsy bitsy tidbits
	 * of data.
	 */

	switch(sk->state) {
	case TCP_TIME_WAIT:
		/*
		 * keep us in TIME_WAIT until we stop getting packets,
		 * reset the timeout.
		 */
		tcp_reset_msl_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
		break;
	case TCP_CLOSE:
		/*
		 * don't touch the timer.
		 */
		break;
	default:
		/*
		 * 	Must check send_head and write_queue
		 * 	to determine which timeout to use.
		 */
		if (sk->send_head) {
			tcp_reset_xmit_timer(sk, TIME_WRITE, sk->rto);
		} else if (!skb_queue_empty(&sk->write_queue)
			&& sk->ack_backlog == 0)
		{
			/* 
			 * if the write queue is not empty when we get here
			 * then we failed to move any data to the retransmit
			 * queue above. (If we had send_head would be non-NULL).
			 * Furthermore, since the send_head is NULL here
			 * we must not be in retransmit mode at this point.
			 * This implies we have no packets in flight,
			 * hence sk->packets_out < sk->cong_window.
			 * Examining the conditions for the test to move
			 * data to the retransmission queue we find that
			 * we must therefore have a zero window.
			 * Hence, if the ack_backlog is 0 we should initiate
			 * a zero probe.
			 * We don't do a zero probe if we have a delayed
			 * ACK in hand since the other side may have a
			 * window opening, but they are waiting to hear
			 * from us before they tell us about it.
			 * (They are applying Nagle's rule).
			 * So, we don't set up the zero window probe
			 * just yet. We do have to clear the timer
			 * though in this case...
			 */
			tcp_reset_xmit_timer(sk, TIME_PROBE0, sk->rto);
		} else if (sk->keepopen) {
			tcp_reset_xmit_timer(sk, TIME_KEEPOPEN, TCP_TIMEOUT_LEN);
		} else {
			del_timer(&sk->retransmit_timer);
			sk->ip_xmit_timeout = 0;
		}
		break;
	}

	/*
	 *	We have nothing queued but space to send. Send any partial
	 *	packets immediately (end of Nagle rule application).
	 */
	 
	if (sk->packets_out == 0
	    && sk->partial != NULL
	    && skb_queue_empty(&sk->write_queue)
	    && sk->send_head == NULL) 
	{
		tcp_send_partial(sk);
	}

	/*
	 * In the LAST_ACK case, the other end FIN'd us.  We then FIN'd them, and
	 * we are now waiting for an acknowledge to our FIN.  The other end is
	 * already in TIME_WAIT.
	 *
	 * Move to TCP_CLOSE on success.
	 */

	if (sk->state == TCP_LAST_ACK) 
	{
		if (!sk->dead)
			sk->state_change(sk);
		if(sk->debug)
			printk("rcv_ack_seq: %X==%X, acked_seq: %X==%X\n",
				sk->rcv_ack_seq,sk->write_seq,sk->acked_seq,sk->fin_seq);
		if (sk->rcv_ack_seq == sk->write_seq /*&& sk->acked_seq == sk->fin_seq*/) 
		{
			sk->shutdown = SHUTDOWN_MASK;
			tcp_set_state(sk,TCP_CLOSE);
			return 1;
		}
	}

	/*
	 *	Incoming ACK to a FIN we sent in the case of our initiating the close.
	 *
	 *	Move to FIN_WAIT2 to await a FIN from the other end. Set
	 *	SEND_SHUTDOWN but not RCV_SHUTDOWN as data can still be coming in.
	 */

	if (sk->state == TCP_FIN_WAIT1) 
	{

		if (!sk->dead) 
			sk->state_change(sk);
		if (sk->rcv_ack_seq == sk->write_seq) 
		{
			sk->shutdown |= SEND_SHUTDOWN;
			tcp_set_state(sk, TCP_FIN_WAIT2);
			/* If the socket is dead, then there is no
			 * user process hanging around using it.
			 * We want to set up a FIN_WAIT2 timeout ala BSD.
			 */
			if (sk->dead)
				tcp_reset_msl_timer(sk, TIME_CLOSE, TCP_FIN_TIMEOUT);
		}
	}

	/*
	 *	Incoming ACK to a FIN we sent in the case of a simultaneous close.
	 *
	 *	Move to TIME_WAIT
	 */

	if (sk->state == TCP_CLOSING) 
	{

		if (!sk->dead) 
			sk->state_change(sk);
		if (sk->rcv_ack_seq == sk->write_seq) 
		{
			tcp_time_wait(sk);
		}
	}
	
	/*
	 *	Final ack of a three way shake 
	 */
	 
	if (sk->state==TCP_SYN_RECV)
	{
		tcp_set_state(sk, TCP_ESTABLISHED);
		tcp_options(sk,th);
		sk->dummy_th.dest=th->source;
		sk->copied_seq = sk->acked_seq;
		if(!sk->dead)
			sk->state_change(sk);
		if(sk->max_window==0)
		{
			sk->max_window=32;	/* Sanity check */
			sk->mss=min(sk->max_window,sk->mtu);
		}
		/* Reset the RTT estimator to the initial
		 * state rather than testing to avoid
		 * updating it on the ACK to the SYN packet.
		 */
		sk->rtt = 0;
		sk->rto = TCP_TIMEOUT_INIT;
		sk->mdev = TCP_TIMEOUT_INIT;
	}
	
	/*
	 * The following code has been greatly simplified from the
	 * old hacked up stuff. The wonders of properly setting the
	 * retransmission timeouts.
	 *
	 * If we are retransmitting, and we acked a packet on the retransmit
	 * queue, and there is still something in the retransmit queue,
	 * then we can output some retransmission packets.
	 */

	if (sk->send_head != NULL && (flag&2) && sk->retransmits)
	{
		tcp_do_retransmit(sk, 1);
	}

	return 1;

uninteresting_ack:
	if(sk->debug)
		printk("Ack ignored %u %u\n",ack,sk->sent_seq);
			
	/*
	 *	Keepalive processing.
	 */
		 
	if (after(ack, sk->sent_seq)) 
	{
		return 0;
	}
		
	/*
	 *	Restart the keepalive timer.
	 */
		 
	if (sk->keepopen) 
	{
		if(sk->ip_xmit_timeout==TIME_KEEPOPEN)
			tcp_reset_xmit_timer(sk, TIME_KEEPOPEN, TCP_TIMEOUT_LEN);
	}
	return 1;
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
			 * move to CLOSE_WAIT, tcp_data() already handled
			 * sending the ack.
			 */
			tcp_set_state(sk,TCP_CLOSE_WAIT);
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
			 * FIN lost hang). The TIME_WRITE code is already correct
			 * for handling this timeout.
			 */

			if (sk->ip_xmit_timeout != TIME_WRITE) {
				if (sk->send_head)
					tcp_reset_xmit_timer(sk, TIME_WRITE, sk->rto);
				else if (sk->ip_xmit_timeout != TIME_PROBE0
				|| skb_queue_empty(&sk->write_queue)) {
					/* BUG check case.
					 * We have a problem here if there
					 * is no timer running [leads to
					 * frozen socket] or no data in the
					 * write queue [means we sent a fin
					 * and lost it from the queue before
					 * changing the ack properly].
					 */
					printk(KERN_ERR "Lost timer or fin packet in tcp_fin.");
				}
			}
			tcp_set_state(sk,TCP_CLOSING);
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
 * Add a sk_buff to the TCP receive queue, calculating
 * the ACK sequence as we go..
 */
static inline void tcp_insert_skb(struct sk_buff * skb, struct sk_buff_head * list)
{
	struct sk_buff * prev, * next;
	u32 seq;

	/*
	 * Find where the new skb goes.. (This goes backwards,
	 * on the assumption that we get the packets in order)
	 */
	seq = skb->seq;
	prev = list->prev;
	next = (struct sk_buff *) list;
	for (;;) {
		if (prev == (struct sk_buff *) list || !after(prev->seq, seq))
			break;
		next = prev;
		prev = prev->prev;
	}
	__skb_insert(skb, prev, next, list);
}

/*
 * Called for each packet when we find a new ACK endpoint sequence in it
 */
static inline u32 tcp_queue_ack(struct sk_buff * skb, struct sock * sk)
{
	/*
	 *	When we ack the fin, we do the FIN 
	 *	processing.
	 */
	skb->acked = 1;
	if (skb->h.th->fin)
		tcp_fin(skb,sk,skb->h.th);
	return skb->end_seq;
}	

static void tcp_queue(struct sk_buff * skb, struct sock * sk, struct tcphdr *th)
{
	u32 ack_seq;

	tcp_insert_skb(skb, &sk->receive_queue);

	/*
	 * Did we get anything new to ack?
	 */
	ack_seq = sk->acked_seq;


	if (!after(skb->seq, ack_seq)) {
		if (after(skb->end_seq, ack_seq)) {
			/* the packet straddles our window end */
			struct sk_buff_head * list = &sk->receive_queue;
			struct sk_buff * next;
			ack_seq = tcp_queue_ack(skb, sk);

			/*
			 * Do we have any old packets to ack that the above
			 * made visible? (Go forward from skb)
			 */
			next = skb->next;
			while (next != (struct sk_buff *) list) {
				if (after(next->seq, ack_seq))
					break;
				if (after(next->end_seq, ack_seq))
					ack_seq = tcp_queue_ack(next, sk);
				next = next->next;
			}

			/*
			 * Ok, we found new data, update acked_seq as
			 * necessary (and possibly send the actual
			 * ACK packet).
			 */
			sk->acked_seq = ack_seq;

		} else {
			if (sk->debug)
				printk("Ack duplicate packet.\n");
			tcp_send_ack(sk);
			return;
		}


		/*
		 * Delay the ack if possible.  Send ack's to
		 * fin frames immediately as there shouldn't be
		 * anything more to come.
		 */
		if (!sk->delay_acks || th->fin) {
			tcp_send_ack(sk);
		} else {
			/*
			 * If psh is set we assume it's an
			 * interactive session that wants quick
			 * acks to avoid nagling too much. 
			 */
			int delay = HZ/2;
			if (th->psh)
				delay = HZ/50;
			tcp_send_delayed_ack(sk, delay, sk->ato);
		}

		/*
		 *	Tell the user we have some more data.
		 */

		if (!sk->dead)
			sk->data_ready(sk,0);

	}
	else
	{
	    /*
	     *	If we've missed a packet, send an ack.
	     *	Also start a timer to send another.
	     *
	     *	4.3reno machines look for these kind of acks so
	     *	they can do fast recovery. Three identical 'old'
	     *	acks lets it know that one frame has been lost
	     *      and should be resent. Because this is before the
	     *	whole window of data has timed out it can take
	     *	one lost frame per window without stalling.
	     *	[See Jacobson RFC1323, Stevens TCP/IP illus vol2]
	     *
	     *	We also should be spotting triple bad sequences.
	     *	[We now do this.]
	     *
	     */
	     
	    if (!skb->acked) 
	    {
		    if(sk->debug)
			    printk("Ack past end of seq packet.\n");
		    tcp_send_ack(sk);
		    /*
		     * We need to be very careful here. We must
		     * not violate Jacobsons packet conservation condition.
		     * This means we should only send an ACK when a packet
		     * leaves the network. We can say a packet left the
		     * network when we see a packet leave the network, or
		     * when an rto measure expires.
		     */
		    tcp_send_delayed_ack(sk,sk->rto,sk->rto);
	    }
	}
}


/*
 *	This routine handles the data.  If there is room in the buffer,
 *	it will be have already been moved into it.  If there is no
 *	room, then we will just have to discard the packet.
 */

static int tcp_data(struct sk_buff *skb, struct sock *sk, 
	 unsigned long saddr, unsigned int len)
{
	struct tcphdr *th;
	u32 new_seq, shut_seq;

	th = skb->h.th;
	skb_pull(skb,th->doff*4);
	skb_trim(skb,len-(th->doff*4));

	/*
	 *	The bytes in the receive read/assembly queue has increased. Needed for the
	 *	low memory discard algorithm 
	 */
	   
	sk->bytes_rcv += skb->len;
	
	if (skb->len == 0 && !th->fin) 
	{
		/* 
		 *	Don't want to keep passing ack's back and forth. 
		 *	(someone sent us dataless, boring frame)
		 */
		if (!th->ack)
			tcp_send_ack(sk);
		kfree_skb(skb, FREE_READ);
		return(0);
	}


	/*
	 *	We no longer have anyone receiving data on this connection.
	 */

#ifndef TCP_DONT_RST_SHUTDOWN		 

	if(sk->shutdown & RCV_SHUTDOWN)
	{
		/*
		 *	FIXME: BSD has some magic to avoid sending resets to
		 *	broken 4.2 BSD keepalives. Much to my surprise a few non
		 *	BSD stacks still have broken keepalives so we want to
		 *	cope with it.
		 */

		if(skb->len)	/* We don't care if it's just an ack or
				   a keepalive/window probe */
		{
			new_seq = skb->seq + skb->len + th->syn;	/* Right edge of _data_ part of frame */
			
			/* Do this the way 4.4BSD treats it. Not what I'd
			   regard as the meaning of the spec but it's what BSD
			   does and clearly they know everything 8) */

			/*
			 *	This is valid because of two things
			 *
			 *	a) The way tcp_data behaves at the bottom.
			 *	b) A fin takes effect when read not when received.
			 */
			 
			shut_seq = sk->acked_seq+1;	/* Last byte */
			
			if(after(new_seq,shut_seq))
			{
				if(sk->debug)
					printk("Data arrived on %p after close [Data right edge %X, Socket shut on %X] %d\n",
						sk, new_seq, shut_seq, sk->blog);
				if(sk->dead)
				{
					sk->acked_seq = new_seq + th->fin;
					tcp_send_reset(sk->saddr, sk->daddr, skb->h.th,
						sk->prot, NULL, skb->dev, sk->ip_tos, sk->ip_ttl);
					tcp_statistics.TcpEstabResets++;
					sk->err = EPIPE;
					sk->error_report(sk);
					sk->shutdown = SHUTDOWN_MASK;
					tcp_set_state(sk,TCP_CLOSE);
					kfree_skb(skb, FREE_READ);
					return 0;
				}
			}
		}
	}

#endif

	/*
  	 * We should only call this if there is data in the frame.
 	 */
	tcp_delack_estimator(sk);

	tcp_queue(skb, sk, th);

	return(0);
}


/*
 *	This routine is only called when we have urgent data
 *	signalled. Its the 'slow' part of tcp_urg. It could be
 *	moved inline now as tcp_urg is only called from one
 *	place. We handle URGent data wrong. We have to - as
 *	BSD still doesn't use the correction from RFC961.
 */
 
static void tcp_check_urg(struct sock * sk, struct tcphdr * th)
{
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
	sk->urg_data = URG_NOTYET;
	sk->urg_seq = ptr;
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

/*
 * This should be a bit smarter and remove partially
 * overlapping stuff too, but this should be good
 * enough for any even remotely normal case (and the
 * worst that can happen is that we have a few
 * unnecessary packets in the receive queue).
 *
 * This function is never called with an empty list..
 */
static inline void tcp_remove_dups(struct sk_buff_head * list)
{
	struct sk_buff * next = list->next;

	for (;;) {
		struct sk_buff * skb = next;
		next = next->next;
		if (next == (struct sk_buff *) list)
			break;
		if (before(next->end_seq, skb->end_seq)) {
			__skb_unlink(next, list);
			kfree_skb(next, FREE_READ);
			next = skb;
			continue;
		}
		if (next->seq != skb->seq)
			continue;
		__skb_unlink(skb, list);
		kfree_skb(skb, FREE_READ);
	}
}

/*
 * Throw out all unnecessary packets: we've gone over the
 * receive queue limit. This shouldn't happen in a normal
 * TCP connection, but we might have gotten duplicates etc.
 */
static void prune_queue(struct sk_buff_head * list)
{
	for (;;) {
		struct sk_buff * skb = list->prev;

		/* gone through it all? */
		if (skb == (struct sk_buff *) list)
			break;
		if (!skb->acked) {
			__skb_unlink(skb, list);
			kfree_skb(skb, FREE_READ);
			continue;
		}
		tcp_remove_dups(list);
		break;
	}
}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
/*
 *	Check whether a received TCP packet might be for one of our
 *	connections.
 */

int tcp_chkaddr(struct sk_buff *skb)
{
	struct iphdr *iph = skb->h.iph;
	struct tcphdr *th = (struct tcphdr *)(skb->h.raw + iph->ihl*4);
	struct sock *sk;

	sk = get_sock(&tcp_prot, th->dest, iph->saddr, th->source, iph->daddr, 0, 0);

	if (!sk) return 0;
	/* 0 means accept all LOCAL addresses here, not all the world... */
	if (sk->rcv_saddr == 0) return 0;
	return 1;
}
#endif

/*
 *	A TCP packet has arrived.
 *		skb->h.raw is the TCP header.
 */
 
int tcp_rcv(struct sk_buff *skb, struct device *dev, struct options *opt,
	__u32 daddr, unsigned short len,
	__u32 saddr, int redo, struct inet_protocol * protocol)
{
	struct tcphdr *th;
	struct sock *sk;
	int syn_ok=0;
#ifdef CONFIG_IP_TRANSPARENT_PROXY
	int r;
#endif

	/*
	 * "redo" is 1 if we have already seen this skb but couldn't
	 * use it at that time (the socket was locked).  In that case
	 * we have already done a lot of the work (looked up the socket
	 * etc).
	 */
	th = skb->h.th;
	sk = skb->sk;
	if (!redo) {
		tcp_statistics.TcpInSegs++;
		if (skb->pkt_type!=PACKET_HOST)
			goto discard_it;

		/*
		 *	Pull up the IP header.
		 */
	
		skb_pull(skb, skb->h.raw-skb->data);

		/*
		 *	Try to use the device checksum if provided.
		 */
		switch (skb->ip_summed) 
		{
			case CHECKSUM_NONE:
				skb->csum = csum_partial((char *)th, len, 0);
			case CHECKSUM_HW:
				if (tcp_check(th, len, saddr, daddr, skb->csum))
					goto discard_it;
			default:
				/* CHECKSUM_UNNECESSARY */
		}
		sk = get_tcp_sock(saddr, th->source, daddr, th->dest, dev->pa_addr, skb->redirport);
		if (!sk)
			goto no_tcp_socket;
		skb->sk = sk;
		skb->seq = ntohl(th->seq);
		skb->end_seq = skb->seq + th->syn + th->fin + len - th->doff*4;
		skb->ack_seq = ntohl(th->ack_seq);

		skb->acked = 0;
		skb->used = 0;
		skb->free = 1;
		skb->saddr = daddr;
		skb->daddr = saddr;

		/*
		 * We may need to add it to the backlog here. 
		 */
		if (sk->users) 
		{
			__skb_queue_tail(&sk->back_log, skb);
			return(0);
		}
	}

	/*
	 *	If this socket has got a reset it's to all intents and purposes 
	 *	really dead. Count closed sockets as dead.
	 *
	 *	Note: BSD appears to have a bug here. A 'closed' TCP in BSD
	 *	simply drops data. This seems incorrect as a 'closed' TCP doesn't
	 *	exist so should cause resets as if the port was unreachable.
	 */

	if (sk->zapped || sk->state==TCP_CLOSE)
		goto no_tcp_socket;

	if (!sk->prot) 
	{
		printk(KERN_CRIT "IMPOSSIBLE 3\n");
		return(0);
	}


	/*
	 *	Charge the memory to the socket. 
	 */
	 
	skb->sk=sk;
	atomic_add(skb->truesize, &sk->rmem_alloc);

	/*
	 * Mark the time of the last received packet.
	 */
	sk->idletime = jiffies;
	
	/*
	 *	We should now do header prediction.
	 */
	 
	/*
	 *	This basically follows the flow suggested by RFC793, with the corrections in RFC1122. We
	 *	don't implement precedence and we process URG incorrectly (deliberately so) for BSD bug
	 *	compatibility. We also set up variables more thoroughly [Karn notes in the
	 *	KA9Q code the RFC793 incoming segment rules don't initialise the variables for all paths].
	 */

	if(sk->state!=TCP_ESTABLISHED)		/* Skip this lot for normal flow */
	{
	
		/*
		 *	Now deal with unusual cases.
		 */
	 
		if(sk->state==TCP_LISTEN)
		{
			if(th->ack)	/* These use the socket TOS.. might want to be the received TOS */
				tcp_send_reset(daddr,saddr,th,sk->prot,opt,dev,sk->ip_tos, sk->ip_ttl);

			/*
			 *	We don't care for RST, and non SYN are absorbed (old segments)
			 *	Broadcast/multicast SYN isn't allowed. Note - bug if you change the
			 *	netmask on a running connection it can go broadcast. Even Sun's have
			 *	this problem so I'm ignoring it 
			 */
			   
#ifdef CONFIG_IP_TRANSPARENT_PROXY
			/*
			 * We may get non-local addresses and still want to
			 * handle them locally, due to transparent proxying.
			 * Thus, narrow down the test to what is really meant.
			 */
			if(th->rst || !th->syn || th->ack || (r = ip_chk_addr(daddr) == IS_BROADCAST || r == IS_MULTICAST))
#else
			if(th->rst || !th->syn || th->ack || ip_chk_addr(daddr)!=IS_MYADDR)
#endif
			{
				kfree_skb(skb, FREE_READ);
				return 0;
			}
		
			/*	
			 *	Guess we need to make a new socket up 
			 */
		
			tcp_conn_request(sk, skb, daddr, saddr, opt, dev, tcp_init_seq());
		
			/*
			 *	Now we have several options: In theory there is nothing else
			 *	in the frame. KA9Q has an option to send data with the syn,
			 *	BSD accepts data with the syn up to the [to be] advertised window
			 *	and Solaris 2.1 gives you a protocol error. For now we just ignore
			 *	it, that fits the spec precisely and avoids incompatibilities. It
			 *	would be nice in future to drop through and process the data.
			 *
			 *	Now TTCP is starting to use we ought to queue this data.
			 */
			 
			return 0;
		}
	
		/* 
		 *	Retransmitted SYN for our socket. This is uninteresting. If sk->state==TCP_LISTEN
		 *	then it's a new connection
		 */
		 
		if (sk->state == TCP_SYN_RECV && th->syn && skb->seq+1 == sk->acked_seq)
		{
			kfree_skb(skb, FREE_READ);
			return 0;
		}
		
		/*
		 *	SYN sent means we have to look for a suitable ack and either reset
		 *	for bad matches or go to connected. The SYN_SENT case is unusual and should
		 *	not be in line code. [AC]
		 */
	   
		if(sk->state==TCP_SYN_SENT)
		{
			/* Crossed SYN or previous junk segment */
			if(th->ack)
			{
				/* We got an ack, but it's not a good ack.
				 * We used to test this with a call to tcp_ack,
				 * but this loses, because it takes the SYN
				 * packet out of the send queue, even if
				 * the ACK doesn't have the SYN bit sent, and
				 * therefore isn't the one we are waiting for.
				 */
				if (after(skb->ack_seq, sk->sent_seq) || before(skb->ack_seq, sk->rcv_ack_seq))
				{
					/* Reset the ack - it's an ack from a 
					   different connection  [ th->rst is checked in tcp_send_reset()] */
					tcp_statistics.TcpAttemptFails++;
					tcp_send_reset(daddr, saddr, th,
						sk->prot, opt,dev,sk->ip_tos,sk->ip_ttl);
					kfree_skb(skb, FREE_READ);
					return(0);
				}
				if(th->rst)
					return tcp_reset(sk,skb);
				if(!th->syn)
				{
					/* A valid ack from a different connection
					   start. Shouldn't happen but cover it */
	         			tcp_statistics.TcpAttemptFails++;
	                                tcp_send_reset(daddr, saddr, th,
	                                        sk->prot, opt,dev,sk->ip_tos,sk->ip_ttl);
					kfree_skb(skb, FREE_READ);
					return 0;
				}

				/* process the ACK, get the SYN packet out
				 * of the send queue, do other initial
				 * processing stuff. [We know it's good, and
				 * we know it's the SYN,ACK we want.]
				 */
				tcp_ack(sk,th,skb->ack_seq,len);


				/*
				 *	Ok.. it's good. Set up sequence numbers and
				 *	move to established.
				 */
				syn_ok=1;	/* Don't reset this connection for the syn */
				sk->acked_seq = skb->seq+1;
				sk->lastwin_seq = skb->seq+1;
				sk->fin_seq = skb->seq;
				tcp_send_ack(sk);
				tcp_set_state(sk, TCP_ESTABLISHED);
				tcp_options(sk,th);
				sk->dummy_th.dest=th->source;
				sk->copied_seq = sk->acked_seq;
				if(!sk->dead)
				{
					sk->state_change(sk);
					sock_wake_async(sk->socket, 0);
				}
				if(sk->max_window==0)
				{
					sk->max_window = 32;
					sk->mss = min(sk->max_window, sk->mtu);
				}
				/* Reset the RTT estimator to the initial
				 * state rather than testing to avoid
				 * updating it on the ACK to the SYN packet.
				 */
				sk->rtt = 0;
				sk->rto = TCP_TIMEOUT_INIT;
				sk->mdev = TCP_TIMEOUT_INIT;
			}
			else
			{
				/* See if SYN's cross. Drop if boring */
				if(th->syn && !th->rst)
				{
					/* Crossed SYN's are fine - but talking to
					   yourself is right out... */
					if(sk->saddr==saddr && sk->daddr==daddr &&
						sk->dummy_th.source==th->source &&
						sk->dummy_th.dest==th->dest)
					{
						tcp_statistics.TcpAttemptFails++;
						return tcp_reset(sk,skb);
					}
					tcp_set_state(sk,TCP_SYN_RECV);
					
					/*
					 *	FIXME:
					 *	Must send SYN|ACK here
					 */
				}		
				/* Discard junk segment */
				kfree_skb(skb, FREE_READ);
				return 0;
			}
			/*
			 *	SYN_RECV with data maybe.. drop through
			 */
			goto rfc_step6;
		}

	/*
	 *	BSD has a funny hack with TIME_WAIT and fast reuse of a port. There is
	 *	a more complex suggestion for fixing these reuse issues in RFC1644
	 *	but not yet ready for general use. Also see RFC1379.
	 *
	 *	Note the funny way we go back to the top of this function for
	 *	this case ("goto try_next_socket").  That also takes care of
	 *	checking "sk->users" for the new socket as well as doing all
	 *	the normal tests on the packet.
	 */
	
#define BSD_TIME_WAIT
#ifdef BSD_TIME_WAIT
		if (sk->state == TCP_TIME_WAIT && th->syn && sk->dead && 
			after(skb->seq, sk->acked_seq) && !th->rst)
		{
			u32 seq = sk->write_seq;
			if(sk->debug)
				printk("Doing a BSD time wait\n");
			tcp_statistics.TcpEstabResets++;	   
			atomic_sub(skb->truesize, &sk->rmem_alloc);
			skb->sk = NULL;
			sk->err=ECONNRESET;
			tcp_set_state(sk, TCP_CLOSE);
			sk->shutdown = SHUTDOWN_MASK;
			sk=get_sock(&tcp_prot, th->dest, saddr, th->source, daddr, dev->pa_addr, skb->redirport);
			/* this is not really correct: we should check sk->users */
			if (sk && sk->state==TCP_LISTEN)
			{
				skb->sk = sk;
				atomic_add(skb->truesize, &sk->rmem_alloc);
				tcp_conn_request(sk, skb, daddr, saddr,opt, dev,seq+128000);
				return 0;
			}
			kfree_skb(skb, FREE_READ);
			return 0;
		}
#endif	
	}

	/*
	 *	We are now in normal data flow (see the step list in the RFC)
	 *	Note most of these are inline now. I'll inline the lot when
	 *	I have time to test it hard and look at what gcc outputs 
	 */

	if (!tcp_sequence(sk, skb->seq, skb->end_seq-th->syn))
	{
		bad_tcp_sequence(sk, th, skb->end_seq-th->syn, dev);
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	if(th->rst)
		return tcp_reset(sk,skb);
	
	/*
	 *	!syn_ok is effectively the state test in RFC793.
	 */
	 
	if(th->syn && !syn_ok)
	{
		tcp_send_reset(daddr,saddr,th, &tcp_prot, opt, dev, skb->ip_hdr->tos, 255);
		return tcp_reset(sk,skb);	
	}

	/*
	 *	Process the ACK
	 */
	 

	if(th->ack && !tcp_ack(sk,th,skb->ack_seq,len))
	{
		/*
		 *	Our three way handshake failed.
		 */
		 
		if(sk->state==TCP_SYN_RECV)
		{
			tcp_send_reset(daddr, saddr, th,sk->prot, opt, dev,sk->ip_tos,sk->ip_ttl);
		}
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	
rfc_step6:		/* I'll clean this up later */

	/*
	 *	If the accepted buffer put us over our queue size we
	 *	now drop it (we must process the ack first to avoid
	 *	deadlock cases).
	 */

	/*
	 *	Process urgent data
	 */
	 	
	tcp_urg(sk, th, len);
	
	/*
	 *	Process the encapsulated data
	 */
	
	if(tcp_data(skb,sk, saddr, len))
		kfree_skb(skb, FREE_READ);

	/*
	 *	If our receive queue has grown past its limits,
	 *	try to prune away duplicates etc..
	 */
	if (sk->rmem_alloc > sk->rcvbuf)
		prune_queue(&sk->receive_queue);

	/*
	 *	And done
	 */	
	
	return 0;

no_tcp_socket:
	/*
	 *	No such TCB. If th->rst is 0 send a reset (checked in tcp_send_reset)
	 */
	tcp_send_reset(daddr, saddr, th, &tcp_prot, opt,dev,skb->ip_hdr->tos,255);

discard_it:
	/*
	 *	Discard frame
	 */
	skb->sk = NULL;
	kfree_skb(skb, FREE_READ);
	return 0;
}
