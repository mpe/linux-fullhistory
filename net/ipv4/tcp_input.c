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
 */

#include <linux/config.h>
#include <net/tcp.h>

/*
 *	Policy code extracted so its now seperate
 */

/*
 *	Called each time to estimate the delayed ack timeout. This is
 *	how it should be done so a fast link isnt impacted by ack delay.
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

		if (m > (sk->rtt >> 3)) 
		{
			sk->ato = sk->rtt >> 3;
			/*
			 * printk(KERN_DEBUG "ato: rtt %lu\n", sk->ato);
			 */
		}
		else 
		{
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
	if(m<=0)
		m=1;		/* IS THIS RIGHT FOR <0 ??? */
	m -= (sk->rtt >> 3);    /* m is now error in rtt est */
	sk->rtt += m;           /* rtt = 7/8 rtt + 1/8 new */
	if (m < 0)
		m = -m;		/* m is now abs(error) */
	m -= (sk->mdev >> 2);   /* similar update on mdev */
	sk->mdev += m;	    	/* mdev = 3/4 mdev + 1/4 new */

	/*
	 *	Now update timeout.  Note that this removes any backoff.
	 */
			 
	sk->rto = ((sk->rtt >> 2) + sk->mdev) >> 1;
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

static inline struct sock * get_tcp_sock(u32 saddr, u16 sport, u32 daddr, u16 dport)
{
	struct sock * sk;

	sk = (struct sock *) th_cache_sk;
	if (!sk || saddr != th_cache_saddr || daddr != th_cache_daddr ||
	    sport != th_cache_sport || dport != th_cache_dport) {
		sk = get_sock(&tcp_prot, dport, saddr, sport, daddr);
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
 
static void bad_tcp_sequence(struct sock *sk, struct tcphdr *th, short len,
	     struct options *opt, unsigned long saddr, struct device *dev)
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
	 *	4.3reno machines look for these kind of acks so they can do fast
	 *	recovery. Three identical 'old' acks lets it know that one frame has
	 *	been lost and should be resent. Because this is before the whole window
	 *	of data has timed out it can take one lost frame per window without
	 *	stalling. [See Jacobson RFC1323, Stevens TCP/IP illus vol2]
	 *
	 *	We also should be spotting triple bad sequences.
	 */
	tcp_send_ack(sk->sent_seq, sk->acked_seq, sk, th, saddr);
	return;
}

/*
 *	This functions checks to see if the tcp header is actually acceptable. 
 */
 
extern __inline__ int tcp_sequence(struct sock *sk, u32 seq, u32 end_seq)
{
	u32 end_window = sk->acked_seq + sk->window;
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
	 *	set backlog as a fudge factor. Thats just too gross.
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
	skb_queue_head_init(&newsk->back_log);
	newsk->rtt = 0;		/*TCP_CONNECT_TIME<<3*/
	newsk->rto = TCP_TIMEOUT_INIT;
	newsk->mdev = 0;
	newsk->max_window = 0;
	newsk->cong_window = 1;
	newsk->cong_count = 0;
	newsk->ssthresh = 0;
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
	init_timer(&newsk->retransmit_timer);
	newsk->retransmit_timer.data = (unsigned long)newsk;
	newsk->retransmit_timer.function=&tcp_retransmit_timer;
	newsk->dummy_th.source = skb->h.th->dest;
	newsk->dummy_th.dest = skb->h.th->source;
	
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
	 *	TCP layer about a suitable MTU, but its easier to let skip sort it out
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
		return(1);	/* Dead, cant ack any more so why bother */

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
	 *	If there is data set flag 1
	 */
	 
	if (len != th->doff*4) 
		flag |= 1;

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
	if (after(sk->window_seq, window_seq)) {
		flag |= 4;
		tcp_window_shrunk(sk, window_seq);
	}

	/*
	 *	Update the right hand window edge of the host
	 */
	sk->window_seq = window_seq;

	/*
	 *	Pipe has emptied
	 */	 
	if (sk->send_tail == NULL || sk->send_head == NULL) 
	{
		sk->send_head = NULL;
		sk->send_tail = NULL;
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
		if (sk->cong_window < sk->ssthresh)  
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
	 *	Remember the highest ack received.
	 */
	 
	sk->rcv_ack_seq = ack;
	
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
		 
  		if (skb_peek(&sk->write_queue) != NULL &&   /* should always be non-null */
		    ! before (sk->window_seq, sk->write_queue.next->end_seq)) 
		{
			sk->backoff = 0;
			
			/*
			 *	Recompute rto from rtt.  this eliminates any backoff.
			 */

			sk->rto = ((sk->rtt >> 2) + sk->mdev) >> 1;
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
   
	while(sk->send_head != NULL) 
	{
		/* Check for a bug. */
		if (sk->send_head->link3 &&
		    after(sk->send_head->end_seq, sk->send_head->link3->end_seq)) 
			printk("INET: tcp.c: *** bug send_list out of order.\n");
			
		/*
		 *	If our packet is before the ack sequence we can
		 *	discard it as it's confirmed to have arrived the other end.
		 */
		 
		if (before(sk->send_head->end_seq, ack+1)) 
		{
			struct sk_buff *oskb;	
			if (sk->retransmits) 
			{	
				/*
				 *	We were retransmitting.  don't count this in RTT est 
				 */
				flag |= 2;

				/*
				 * even though we've gotten an ack, we're still
				 * retransmitting as long as we're sending from
				 * the retransmit queue.  Keeping retransmits non-zero
				 * prevents us from getting new data interspersed with
				 * retransmissions.
				 */

				if (sk->send_head->link3)	/* Any more queued retransmits? */
					sk->retransmits = 1;
				else
					sk->retransmits = 0;
			}
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

			oskb = sk->send_head;

			if (!(flag&2)) 	/* Not retransmitting */
				tcp_rtt_estimator(sk,oskb);
			flag |= (2|4);	/* 2 is really more like 'don't adjust the rtt 
					   In this case as we just set it up */
			cli();
			oskb = sk->send_head;
			IS_SKB(oskb);
			sk->send_head = oskb->link3;
			if (sk->send_head == NULL) 
			{
				sk->send_tail = NULL;
			}

		/*
		 *	We may need to remove this from the dev send list. 
		 */

			if (oskb->next)
				skb_unlink(oskb);
			sti();
			kfree_skb(oskb, FREE_WRITE); /* write. */
			if (!sk->dead)
				sk->write_space(sk);
		}
		else
		{
			break;
		}
	}

	/*
	 * XXX someone ought to look at this too.. at the moment, if skb_peek()
	 * returns non-NULL, we complete ignore the timer stuff in the else
	 * clause.  We ought to organize the code so that else clause can
	 * (should) be executed regardless, possibly moving the PROBE timer
	 * reset over.  The skb_peek() thing should only move stuff to the
	 * write queue, NOT also manage the timer functions.
	 */

	/*
	 * Maybe we can take some stuff off of the write queue,
	 * and put it onto the xmit queue.
	 */
	if (skb_peek(&sk->write_queue) != NULL) 
	{
		if (after (sk->window_seq+1, sk->write_queue.next->end_seq) &&
			(sk->retransmits == 0 || 
			 sk->ip_xmit_timeout != TIME_WRITE ||
			 before(sk->write_queue.next->end_seq, sk->rcv_ack_seq + 1))
			&& sk->packets_out < sk->cong_window) 
		{
			/*
			 *	Add more data to the send queue.
			 */
			flag |= 1;
			tcp_write_xmit(sk);
		}
		else if (before(sk->window_seq, sk->write_queue.next->end_seq) &&
 			sk->send_head == NULL &&
 			sk->ack_backlog == 0 &&
 			sk->state != TCP_TIME_WAIT) 
 		{
 			/*
 			 *	Data to queue but no room.
 			 */
 			tcp_reset_xmit_timer(sk, TIME_PROBE0, sk->rto);
 		}		
	}
	else
	{
		/*
		 * from TIME_WAIT we stay in TIME_WAIT as long as we rx packets
		 * from TCP_CLOSE we don't do anything
		 *
		 * from anything else, if there is write data (or fin) pending,
		 * we use a TIME_WRITE timeout, else if keepalive we reset to
		 * a KEEPALIVE timeout, else we delete the timer.
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
			 * 	Must check send_head, write_queue, and ack_backlog
			 * 	to determine which timeout to use.
			 */
			if (sk->send_head || skb_peek(&sk->write_queue) != NULL || sk->ack_backlog) {
				tcp_reset_xmit_timer(sk, TIME_WRITE, sk->rto);
			} else if (sk->keepopen) {
				tcp_reset_xmit_timer(sk, TIME_KEEPOPEN, TCP_TIMEOUT_LEN);
			} else {
				del_timer(&sk->retransmit_timer);
				sk->ip_xmit_timeout = 0;
			}
			break;
		}
	}

	/*
	 *	We have nothing queued but space to send. Send any partial
	 *	packets immediately (end of Nagle rule application).
	 */
	 
	if (sk->packets_out == 0 && sk->partial != NULL &&
		skb_peek(&sk->write_queue) == NULL && sk->send_head == NULL) 
	{
		flag |= 1;
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
			flag |= 1;
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
			flag |= 1;
			sk->shutdown |= SEND_SHUTDOWN;
			tcp_set_state(sk, TCP_FIN_WAIT2);
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
			flag |= 1;
			tcp_time_wait(sk);
		}
	}
	
	/*
	 *	Final ack of a three way shake 
	 */
	 
	if(sk->state==TCP_SYN_RECV)
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
	}
	
	/*
	 * I make no guarantees about the first clause in the following
	 * test, i.e. "(!flag) || (flag&4)".  I'm not entirely sure under
	 * what conditions "!flag" would be true.  However I think the rest
	 * of the conditions would prevent that from causing any
	 * unnecessary retransmission. 
	 *   Clearly if the first packet has expired it should be 
	 * retransmitted.  The other alternative, "flag&2 && retransmits", is
	 * harder to explain:  You have to look carefully at how and when the
	 * timer is set and with what timeout.  The most recent transmission always
	 * sets the timer.  So in general if the most recent thing has timed
	 * out, everything before it has as well.  So we want to go ahead and
	 * retransmit some more.  If we didn't explicitly test for this
	 * condition with "flag&2 && retransmits", chances are "when + rto < jiffies"
	 * would not be true.  If you look at the pattern of timing, you can
	 * show that rto is increased fast enough that the next packet would
	 * almost never be retransmitted immediately.  Then you'd end up
	 * waiting for a timeout to send each packet on the retransmission
	 * queue.  With my implementation of the Karn sampling algorithm,
	 * the timeout would double each time.  The net result is that it would
	 * take a hideous amount of time to recover from a single dropped packet.
	 * It's possible that there should also be a test for TIME_WRITE, but
	 * I think as long as "send_head != NULL" and "retransmit" is on, we've
	 * got to be in real retransmission mode.
	 *   Note that tcp_do_retransmit is called with all==1.  Setting cong_window
	 * back to 1 at the timeout will cause us to send 1, then 2, etc. packets.
	 * As long as no further losses occur, this seems reasonable.
	 */
	
	if (((!flag) || (flag&4)) && sk->send_head != NULL &&
	       (((flag&2) && sk->retransmits) ||
	       (sk->send_head->when + sk->rto < jiffies))) 
	{
		if(sk->send_head->when + sk->rto < jiffies)
			tcp_retransmit(sk,0);	
		else
		{
			tcp_do_retransmit(sk, 1);
			tcp_reset_xmit_timer(sk, TIME_WRITE, sk->rto);
		}
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

			if(sk->ip_xmit_timeout != TIME_WRITE)
				tcp_reset_xmit_timer(sk, TIME_WRITE, sk->rto);
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
 *	This routine handles the data.  If there is room in the buffer,
 *	it will be have already been moved into it.  If there is no
 *	room, then we will just have to discard the packet.
 */

static int tcp_data(struct sk_buff *skb, struct sock *sk, 
	 unsigned long saddr, unsigned short len)
{
	struct sk_buff *skb1, *skb2;
	struct tcphdr *th;
	int dup_dumped=0;
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
			tcp_send_ack(sk->sent_seq, sk->acked_seq,sk, th, saddr);
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
	 * 	Now we have to walk the chain, and figure out where this one
	 * 	goes into it.  This is set up so that the last packet we received
	 * 	will be the first one we look at, that way if everything comes
	 * 	in order, there will be no performance loss, and if they come
	 * 	out of order we will be able to fit things in nicely.
	 *
	 *	[AC: This is wrong. We should assume in order first and then walk
	 *	 forwards from the first hole based upon real traffic patterns.]
	 *	
	 */

	if (skb_peek(&sk->receive_queue) == NULL) 	/* Empty queue is easy case */
	{
		skb_queue_head(&sk->receive_queue,skb);
		skb1= NULL;
	} 
	else
	{
		for(skb1=sk->receive_queue.prev; ; skb1 = skb1->prev) 
		{
			if(sk->debug)
			{
				printk("skb1=%p :", skb1);
				printk("skb1->seq = %d: ", skb1->seq);
				printk("skb->seq = %d\n",skb->seq);
				printk("copied_seq = %d acked_seq = %d\n", sk->copied_seq,
						sk->acked_seq);
			}
			
			/*
			 *	Optimisation: Duplicate frame or extension of previous frame from
			 *	same sequence point (lost ack case).
			 *	The frame contains duplicate data or replaces a previous frame
			 *	discard the previous frame (safe as sk->users is set) and put
			 *	the new one in its place.
			 */
			 
			if (skb->seq==skb1->seq && skb->len>=skb1->len)
			{
				skb_append(skb1,skb);
				skb_unlink(skb1);
				kfree_skb(skb1,FREE_READ);
				dup_dumped=1;
				skb1=NULL;
				break;
			}
			
			/*
			 *	Found where it fits
			 */
			 
			if (after(skb->seq+1, skb1->seq))
			{
				skb_append(skb1,skb);
				break;
			}
			
			/*
			 *	See if we've hit the start. If so insert.
			 */
			if (skb1 == skb_peek(&sk->receive_queue))
			{
				skb_queue_head(&sk->receive_queue, skb);
				break;
			}
		}
  	}

	/*
	 *	Figure out what the ack value for this frame is
	 */
	 
	if (before(sk->acked_seq, sk->copied_seq)) 
	{
		printk("*** tcp.c:tcp_data bug acked < copied\n");
		sk->acked_seq = sk->copied_seq;
	}

	/*
	 *	Now figure out if we can ack anything. This is very messy because we really want two
	 *	receive queues, a completed and an assembly queue. We also want only one transmit
	 *	queue.
	 */

	if ((!dup_dumped && (skb1 == NULL || skb1->acked)) || before(skb->seq, sk->acked_seq+1)) 
	{
		if (before(skb->seq, sk->acked_seq+1)) 
		{

			if (after(skb->end_seq, sk->acked_seq)) 
				sk->acked_seq = skb->end_seq;

			skb->acked = 1;

			/*
			 *	When we ack the fin, we do the FIN 
			 *	processing.
			 */

			if (skb->h.th->fin) 
			{
				tcp_fin(skb,sk,skb->h.th);
			}
	  
			for(skb2 = skb->next;
			    skb2 != (struct sk_buff *)&sk->receive_queue;
			    skb2 = skb2->next) 
			{
				if (before(skb2->seq, sk->acked_seq+1)) 
				{
					if (after(skb2->end_seq, sk->acked_seq))
						sk->acked_seq = skb2->end_seq;

					skb2->acked = 1;
					/*
					 * 	When we ack the fin, we do
					 * 	the fin handling.
					 */
					if (skb2->h.th->fin) 
					{
						tcp_fin(skb,sk,skb->h.th);
					}

					/*
					 *	Force an immediate ack.
					 */
					 
					sk->ack_backlog = sk->max_ack_backlog;
				}
				else
				{
					break;
				}
			}

			/*
			 *	This also takes care of updating the window.
			 *	This if statement needs to be simplified.
			 *
			 *      rules for delaying an ack:
			 *      - delay time <= 0.5 HZ
			 *      - we don't have a window update to send
			 *      - must send at least every 2 full sized packets
			 */
			if (!sk->delay_acks ||
			    /* sk->ack_backlog >= sk->max_ack_backlog || */
			    sk->bytes_rcv > sk->max_unacked || th->fin ||
			    sk->ato > HZ/2 ||
			    tcp_raise_window(sk)) {
				tcp_send_ack(sk->sent_seq, sk->acked_seq,sk,th, saddr);
			}
			else 
			{	
				sk->ack_backlog++;
			
				if(sk->debug)				
					printk("Ack queued.\n");
				
				tcp_reset_xmit_timer(sk, TIME_WRITE, sk->ato);
				
			}
		}
	}

	/*
	 *	If we've missed a packet, send an ack.
	 *	Also start a timer to send another.
	 */
	 
	if (!skb->acked) 
	{
	
	/*
	 *	This is important.  If we don't have much room left,
	 *	we need to throw out a few packets so we have a good
	 *	window.  Note that mtu is used, not mss, because mss is really
	 *	for the send side.  He could be sending us stuff as large as mtu.
	 */
		 
		while (sock_rspace(sk) < sk->mtu) 
		{
			skb1 = skb_peek(&sk->receive_queue);
			if (skb1 == NULL) 
			{
				printk("INET: tcp.c:tcp_data memory leak detected.\n");
				break;
			}

			/*
			 *	Don't throw out something that has been acked. 
			 */
		 
			if (skb1->acked) 
			{
				break;
			}
		
			skb_unlink(skb1);
			kfree_skb(skb1, FREE_READ);
		}
		tcp_send_ack(sk->sent_seq, sk->acked_seq, sk, th, saddr);
		sk->ack_backlog++;
		tcp_reset_xmit_timer(sk, TIME_WRITE, min(sk->ato, HZ/2));
	}

	/*
	 *	Now tell the user we may have some data. 
	 */
	 
	if (!sk->dead) 
	{
        	if(sk->debug)
        		printk("Data wakeup.\n");
		sk->data_ready(sk,0);
	} 
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
		sk = get_tcp_sock(saddr, th->source, daddr, th->dest);
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
		printk("IMPOSSIBLE 3\n");
		return(0);
	}


	/*
	 *	Charge the memory to the socket. 
	 */
	 
	skb->sk=sk;
	sk->rmem_alloc += skb->truesize;
	
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
			   
			if(th->rst || !th->syn || th->ack || ip_chk_addr(daddr)!=IS_MYADDR)
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
		 *	then its a new connection
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
				/* We got an ack, but it's not a good ack */
				if(!tcp_ack(sk,th,skb->ack_seq,len))
				{
					/* Reset the ack - its an ack from a 
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
				/*
				 *	Ok.. it's good. Set up sequence numbers and
				 *	move to established.
				 */
				syn_ok=1;	/* Don't reset this connection for the syn */
				sk->acked_seq = skb->seq+1;
				sk->lastwin_seq = skb->seq+1;
				sk->fin_seq = skb->seq;
				tcp_send_ack(sk->sent_seq,sk->acked_seq,sk,th,sk->daddr);
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
			sk->rmem_alloc -= skb->truesize;
			skb->sk = NULL;
			sk->err=ECONNRESET;
			tcp_set_state(sk, TCP_CLOSE);
			sk->shutdown = SHUTDOWN_MASK;
			sk=get_sock(&tcp_prot, th->dest, saddr, th->source, daddr);
			/* this is not really correct: we should check sk->users */
			if (sk && sk->state==TCP_LISTEN)
			{
				skb->sk = sk;
				sk->rmem_alloc += skb->truesize;
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
		bad_tcp_sequence(sk, th, len, opt, saddr, dev);
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

	tcp_delack_estimator(sk);
	
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
	 
	if (sk->rmem_alloc  >= sk->rcvbuf) 
	{
		kfree_skb(skb, FREE_READ);
		return(0);
	}


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
