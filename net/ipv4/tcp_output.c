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
 */

/*
 * Changes:	Pedro Roque	:	Retransmit queue handled by TCP.
 *				:	Fragmentation on mtu decrease
 *				:	Segment collapse on retransmit
 *				:	AF independence
 *
 *		Linus Torvalds	:	send_delayed_ack
 *
 */

#include <net/tcp.h>

/*
 *	Get rid of any delayed acks, we sent one already..
 */
static __inline__ void clear_delayed_acks(struct sock * sk)
{
	sk->delayed_acks = 0;
	sk->ack_backlog = 0;
	sk->bytes_rcv = 0;
	tcp_clear_xmit_timer(sk, TIME_DACK);
}

static __inline__ void update_send_head(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	
	tp->send_head = tp->send_head->next;

	if (tp->send_head == (struct sk_buff *) &sk->write_queue)
	{
		tp->send_head = NULL;
	}

}

static __inline__ int tcp_snd_test(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int nagle_check = 1;
	int len;

	/*
	 *	RFC 1122 - section 4.2.3.4
	 *
	 *	We must queue if
	 *
	 *	a) The right edge of this frame exceeds the window
	 *	b) There are packets in flight and we have a small segment
	 *	   [SWS avoidance and Nagle algorithm]
	 *	   (part of SWS is done on packetization)
	 *	c) We are retransmiting [Nagle]
	 *	d) We have too many packets 'in flight'
	 */
		
	len = skb->end_seq - skb->seq;

	if (!sk->nonagle && len < (sk->mss >> 1) && sk->packets_out)
	{
		nagle_check = 0;
	}

	return (nagle_check && sk->packets_out < sk->cong_window &&
		!after(skb->end_seq, tp->snd_una + tp->snd_wnd) &&
		sk->retransmits == 0);
}

/*
 *	This is the main buffer sending routine. We queue the buffer
 *	having checked it is sane seeming.
 */
 
int tcp_send_skb(struct sock *sk, struct sk_buff *skb)
{
	struct tcphdr * th = skb->h.th;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int size;

	/*
	 *	length of packet (not counting length of pre-tcp headers) 
	 */
	 
	size = skb->len - ((unsigned char *) th - skb->data);

	/*
	 *	Sanity check it.. 
	 */
	 
	if (size < sizeof(struct tcphdr) || size > skb->len) 
	{
		printk("tcp_send_skb: bad skb (skb = %p, data = %p, th = %p, len = %lu)\n",
			skb, skb->data, th, skb->len);
		kfree_skb(skb, FREE_WRITE);
		return 0;
	}

	/*
	 *	If we have queued a header size packet.. (these crash a few
	 *	tcp stacks if ack is not set)
	 */
	 
	if (size == sizeof(struct tcphdr)) 
	{
		/* 
                 * If it's got a syn or fin discard
                 */
		if(!th->syn && !th->fin) 
		{
			printk("tcp_send_skb: attempt to queue a bogon.\n");
			kfree_skb(skb,FREE_WRITE);
			return 0;
		}
	}


	/*
	 *	Actual processing.
	 */
	 
	tcp_statistics.TcpOutSegs++;  
	skb->seq = ntohl(th->seq);
	skb->end_seq = skb->seq + size - 4*th->doff;

	
	if (tp->send_head || !tcp_snd_test(sk, skb))
	{
		/* 
		 * Remember where we must start sending
		 */

		if (tp->send_head == NULL)
			tp->send_head = skb;

		skb_queue_tail(&sk->write_queue, skb);

		if (sk->packets_out == 0 && !tp->pending)
		{
			tp->pending = TIME_PROBE0;
			tcp_reset_xmit_timer(sk, TIME_PROBE0, tp->rto);
		}

	}
	else
	{
		struct sk_buff * buff;

		/*
		 *	This is going straight out
		 */

		skb_queue_tail(&sk->write_queue, skb);

		clear_delayed_acks(sk);
		 
		th->ack_seq = htonl(tp->rcv_nxt);
		th->window = htons(tcp_select_window(sk));

		tp->af_specific->send_check(sk, th, size, skb);

		tp->snd_nxt = skb->end_seq;
		
		atomic_inc(&sk->packets_out);

		skb->when = jiffies;
		
		buff = skb_clone(skb, GFP_ATOMIC);
		atomic_add(buff->truesize, &sk->wmem_alloc);

		tp->af_specific->queue_xmit(sk, skb->dev, buff, 1);

		if (!tcp_timer_is_set(sk, TIME_RETRANS))
			tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
	}

	return 0;
}

/*
 *	Function to create two new tcp segments.
 *	Shrinks the given segment to the specified size and appends a new
 *	segment with the rest of the packet to the list.
 *	This won't be called frenquently, I hope... 
 */

static int tcp_fragment(struct sock *sk, struct sk_buff *skb, u32 len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *buff;
	struct tcphdr *th, *nth;	
	int nsize;
	int tmp;

	th = skb->h.th;

	/* size of new segment */
	nsize = skb->tail - ((unsigned char *) (th + 1)) - len;

	if (nsize <= 0)
	{
		printk(KERN_DEBUG "tcp_fragment: bug size <= 0\n");
		return -1;
	}

	/*
	 *	Get a new skb... force flag on
	 */
	buff = sock_wmalloc(sk, nsize + 128 + sk->prot->max_header + 15, 1, 
			    GFP_ATOMIC);

	if (buff == NULL)
		return -1;

	buff->sk = sk;
	buff->localroute = sk->localroute;
	    	
	/*
	 *	Put headers on the new packet
	 */

	tmp = tp->af_specific->build_net_header(sk, buff);

	if (tmp < 0)
	{
		sock_wfree(sk, buff);
		return -1;
	}
		
	/*
	 *	Move the TCP header over
	 */
	
	nth = (struct tcphdr *) skb_put(buff, sizeof(*th));

	buff->h.th = nth;
	
	memcpy(nth, th, sizeof(*th));
	
	/*
	 *	Correct the new header
	 */
	
	buff->seq = skb->seq + len;
	buff->end_seq = skb->end_seq;
	nth->seq = htonl(buff->seq);
	nth->check = 0;
	nth->doff  = 5; 
	
	/* urg data is always an headache */
	if (th->urg)
	{
		if (th->urg_ptr > len)
		{
			th->urg = 0;
			nth->urg_ptr -= len;
		}
		else
		{
			nth->urg = 0;
		}
	}

	/*
	 *	Copy TCP options and data start to our new buffer
	 */
	
	buff->csum = csum_partial_copy(((u8 *)(th + 1)) + len,
				       skb_put(buff, nsize),
				       nsize, 0);
       

	skb->end_seq -= nsize;

	skb_trim(skb, skb->len - nsize);

	/* remember to checksum this packet afterwards */
	th->check = 0;
	skb->csum = csum_partial((u8*) (th + 1), skb->tail - ((u8 *) (th + 1)),
				 0);

	skb_append(skb, buff);

	return 0;
}

static void tcp_wrxmit_prob(struct sock *sk, struct sk_buff *skb)
{
	/*
	 *	This is acked data. We can discard it. This 
	 *	cannot currently occur.
	 */

	sk->retransmits = 0;

	printk(KERN_DEBUG "tcp_write_xmit: bug skb in write queue\n");

	update_send_head(sk);

	skb_unlink(skb);	
	skb->sk = NULL;
	skb->free = 1;
	kfree_skb(skb, FREE_WRITE);

	if (!sk->dead)
		sk->write_space(sk);
}

static int tcp_wrxmit_frag(struct sock *sk, struct sk_buff *skb, int size)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	
	printk(KERN_DEBUG "tcp_write_xmit: frag needed size=%d mss=%d\n", 
	       size, sk->mss);
				
	if (tcp_fragment(sk, skb, sk->mss))
	{
		/* !tcp_frament Failed! */
		tp->send_head = skb;
		atomic_dec(&sk->packets_out);
		return -1;
	}
	else
	{
		/* 
		 * If tcp_fragment succeded then
		 * the send head is the resulting
		 * fragment
		 */
		tp->send_head = skb->next;
	}
	return 0;
}

/*
 * 	This routine writes packets to the network.
 *	It advances the send_head.
 *	This happens as incoming acks open up the remote window for us.
 */
 
void tcp_write_xmit(struct sock *sk)
{
	struct sk_buff *skb;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	/*
	 *	The bytes will have to remain here. In time closedown will
	 *	empty the write queue and all will be happy 
	 */

	if(sk->zapped)
		return;

	/*
	 *	Anything on the transmit queue that fits the window can
	 *	be added providing we are:
	 *
	 *	a) following SWS avoidance [and Nagle algorithm]
	 *	b) not exceeding our congestion window.
	 *	c) not retransmiting [Nagle]
	 */

	start_bh_atomic();

	while((skb = tp->send_head) && tcp_snd_test(sk, skb))
	{
		IS_SKB(skb);
				
		/*
		 *	See if we really need to send the packet. 
		 */
		 
		if (!after(skb->end_seq, tp->snd_una)) 
		{
			tcp_wrxmit_prob(sk, skb);
		} 
		else
		{
			struct tcphdr *th;
			struct sk_buff *buff;
			int size;

			/* 
			 * Advance the send_head
			 * This one is going out.
			 */

			update_send_head(sk);

			atomic_inc(&sk->packets_out);


/*
 * put in the ack seq and window at this point rather than earlier,
 * in order to keep them monotonic.  We really want to avoid taking
 * back window allocations.  That's legal, but RFC1122 says it's frowned on.
 * Ack and window will in general have changed since this packet was put
 * on the write queue.
 */

			th = skb->h.th;
			size = skb->len - (((unsigned char *) th) - skb->data);

			if (size - (th->doff << 2) > sk->mss)
			{
				if (tcp_wrxmit_frag(sk, skb, size))
					break;
			}
			
			th->ack_seq = htonl(tp->rcv_nxt);
			th->window = htons(tcp_select_window(sk));

			tp->af_specific->send_check(sk, th, size, skb);

			if (before(skb->end_seq, tp->snd_nxt)) 
				printk(KERN_DEBUG "tcp_write_xmit:"
				       " sending already sent seq\n");
			else
				tp->snd_nxt = skb->end_seq;
			
			clear_delayed_acks(sk);
			
			skb->when = jiffies;

			buff = skb_clone(skb, GFP_ATOMIC);
			atomic_add(buff->truesize, &sk->wmem_alloc);

			tp->af_specific->queue_xmit(sk, skb->dev, buff, 1);
			
			if (!tcp_timer_is_set(sk, TIME_RETRANS))
			{
				tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
			}
		}
	}

	end_bh_atomic();
}

static int tcp_retrans_try_collapse(struct sock *sk, struct sk_buff *skb)
{
	struct tcphdr *th1, *th2;
	int size1, size2, avail;
	struct sk_buff *buff = skb->next;

	th1 = skb->h.th;

	if (th1->urg)
		return -1;

	avail = skb_tailroom(skb);

	/*
	 *  size of tcp payload
	 */

	size1 = skb->tail - (u8 *) (th1 + 1);
	
	th2 = buff->h.th;

	size2 = buff->tail - (u8 *) (th2 + 1); 

	if (size2 > avail || size1 + size2 > sk->mss )
		return -1;

	/*
	 *  ok. we will be able to collapse the packet
	 */

	skb_unlink(buff);

	memcpy(skb_put(skb, size2), ((char *) th2) + (th2->doff << 2), size2);
	
	/*
	 * update sizes on original skb. both TCP and IP
	 */
 
	skb->end_seq += size2;

	if (th2->urg)
	{
		th1->urg = 1;
		th1->urg_ptr = th2->urg_ptr + size1;
	}

	/*
	 * ... and off you go.
	 */

	buff->free = 1;
	kfree_skb(buff, FREE_WRITE);
	atomic_dec(&sk->packets_out);

	/* 
	 *	Header checksum will be set by the retransmit procedure
	 *	after calling rebuild header
	 */

	th1->check = 0;
	skb->csum = csum_partial((u8*) (th1+1), size1 + size2, 0);

	return 0;
}


/*
 *	A socket has timed out on its send queue and wants to do a
 *	little retransmitting.
 *	retransmit_head can be different from the head of the write_queue
 *	if we are doing fast retransmit.
 */

void tcp_do_retransmit(struct sock *sk, int all)
{
	struct sk_buff * skb;
	int ct=0;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	start_bh_atomic();

	if (tp->retrans_head == NULL)
		tp->retrans_head = skb_peek(&sk->write_queue);

	if (tp->retrans_head == tp->send_head)
		tp->retrans_head = NULL;
	
	while ((skb = tp->retrans_head) != NULL)
	{
		struct tcphdr *th;
		u32 tcp_size;

		IS_SKB(skb);
		
		/*
		 * In general it's OK just to use the old packet.  However we
		 * need to use the current ack and window fields.  Urg and
		 * urg_ptr could possibly stand to be updated as well, but we
		 * don't keep the necessary data.  That shouldn't be a problem,
		 * if the other end is doing the right thing.  Since we're
		 * changing the packet, we have to issue a new IP identifier.
		 */

		th = skb->h.th;

		tcp_size = skb->tail - ((unsigned char *) (th + 1));

		if (tcp_size > sk->mss)
		{
			if (tcp_fragment(sk, skb, sk->mss))
			{
				printk(KERN_DEBUG "tcp_fragment failed\n");
				return;
			}
			atomic_inc(&sk->packets_out);
		}

		if (!th->syn &&
		    tcp_size < (sk->mss >> 1) &&
		    skb->next != tp->send_head &&
		    skb->next != (struct sk_buff *)&sk->write_queue)
		{
			tcp_retrans_try_collapse(sk, skb);
		}	       		

		if (tp->af_specific->rebuild_header(sk, skb) == 0) 
		{
			struct sk_buff *buff;
			int size;

			if (sk->debug)
				printk("retransmit sending\n");

			/*
			 *	update ack and window
			 */
			th->ack_seq = htonl(tp->rcv_nxt);
			th->window = ntohs(tcp_select_window(sk));

			size = skb->tail - (unsigned char *) th;
			tp->af_specific->send_check(sk, th, size, skb);

			skb->when = jiffies;
			buff = skb_clone(skb, GFP_ATOMIC);
			atomic_add(buff->truesize, &sk->wmem_alloc);

			clear_delayed_acks(sk);

			tp->af_specific->queue_xmit(sk, skb->dev, buff, 1);
		}
		else
		{
			printk(KERN_DEBUG "tcp_do_rebuild_header failed\n");
			break;
		}

		/*
		 *	Count retransmissions
		 */
		 
		ct++;
		sk->prot->retransmits ++;
		tcp_statistics.TcpRetransSegs++;

		/*
		 * Record the high sequence number to help avoid doing
		 * to much fast retransmission.
		 */

		if (sk->retransmits)
		       tp->high_seq = tp->snd_nxt;
		
		/*
		 *	Only one retransmit requested.
		 */
	
		if (!all)
			break;

		/*
		 *	This should cut it off before we send too many packets.
		 */

		if (ct >= sk->cong_window)
			break;

		/*
		 *	Advance the pointer
		 */
		
		tp->retrans_head = skb->next;
		if ((tp->retrans_head == tp->send_head) ||
		    (tp->retrans_head == (struct sk_buff *) &sk->write_queue))
		{
			tp->retrans_head = NULL;
		}
	}

	end_bh_atomic();
}

/*
 *	Send a fin.
 */

void tcp_send_fin(struct sock *sk)
{
	struct tcphdr *th =(struct tcphdr *)&sk->dummy_th;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);	
	struct tcphdr *t1;
	struct sk_buff *buff;
	int tmp;
	
		
	buff = sock_wmalloc(sk, MAX_RESET_SIZE, 1, GFP_KERNEL);

	if (buff == NULL)
	{
		/* This is a disaster if it occurs */
		printk("tcp_send_fin: Impossible malloc failure");
		return;
	}

	/*
	 *	Administrivia
	 */
	 
	buff->sk = sk;
	buff->localroute = sk->localroute;
	buff->csum = 0;

	/*
	 *	Put in the IP header and routing stuff. 
	 */

	tmp = tp->af_specific->build_net_header(sk, buff);

	if (tmp < 0) 
	{
		int t;
  		/*
  		 *	Finish anyway, treat this as a send that got lost. 
  		 *	(Not good).
  		 */
  		 
	  	buff->free = 1;
		sock_wfree(sk,buff);
		sk->write_seq++;
		t=del_timer(&sk->timer);
		if(t)
			add_timer(&sk->timer);
		else
			tcp_reset_msl_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
		return;
	}
	
	/*
	 *	We ought to check if the end of the queue is a buffer and
	 *	if so simply add the fin to that buffer, not send it ahead.
	 */

	t1 =(struct tcphdr *)skb_put(buff,sizeof(struct tcphdr));
	buff->h.th =  t1;

	memcpy(t1, th, sizeof(*t1));
	buff->seq = sk->write_seq;
	sk->write_seq++;
	buff->end_seq = sk->write_seq;
	t1->seq = htonl(buff->seq);
	t1->ack_seq = htonl(tp->rcv_nxt);
	t1->window = htons(tcp_select_window(sk));
	t1->fin = 1;

	tp->af_specific->send_check(sk, t1, sizeof(*t1), buff);

	/*
	 * The fin can only be transmited after the data.
 	 */
 	
	skb_queue_tail(&sk->write_queue, buff);

 	if (tp->send_head == NULL)
	{
		struct sk_buff *skb1;

		atomic_inc(&sk->packets_out);
		tp->snd_nxt = sk->write_seq;
		buff->when = jiffies;

		skb1 = skb_clone(buff, GFP_KERNEL);
		atomic_add(skb1->truesize, &sk->wmem_alloc);

		tp->af_specific->queue_xmit(sk, skb1->dev, skb1, 1);

                if (!tcp_timer_is_set(sk, TIME_RETRANS))
			tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
	}
}

int tcp_send_synack(struct sock *sk)
{
	struct tcp_opt * tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff * skb;	
	struct sk_buff * buff;
	struct tcphdr *th;
	unsigned char *ptr;
	int tmp;
	
	skb = sock_wmalloc(sk, MAX_SYN_SIZE, 1, GFP_ATOMIC);

	if (skb == NULL) 
	{
		return -ENOMEM;
	}

	skb->sk = sk;
	skb->localroute = sk->localroute;

	tmp = tp->af_specific->build_net_header(sk, skb);
	
	if (tmp < 0)
	{
		skb->free = 1;
		kfree_skb(skb, FREE_WRITE);
		return tmp;
	}

	th =(struct tcphdr *) skb_put(skb, sizeof(struct tcphdr));
	skb->h.th = th;
	memset(th, 0, sizeof(struct tcphdr));

	th->syn = 1;
	th->ack = 1;

	th->source = sk->dummy_th.source;
	th->dest = sk->dummy_th.dest;
	       
	skb->seq = tp->snd_una;
	skb->end_seq = skb->seq + 1 /* th->syn */ ;
	th->seq = ntohl(skb->seq);

	th->window = ntohs(tp->rcv_wnd);

	th->ack_seq = htonl(tp->rcv_nxt);
	th->doff = sizeof(*th)/4 + 1;

	ptr = skb_put(skb, TCPOLEN_MSS);
	ptr[0] = TCPOPT_MSS;
	ptr[1] = TCPOLEN_MSS;
	ptr[2] = ((sk->mss) >> 8) & 0xff;
	ptr[3] = (sk->mss) & 0xff;
	skb->csum = csum_partial(ptr, TCPOLEN_MSS, 0);

	tp->af_specific->send_check(sk, th, sizeof(*th)+4, skb);

	skb_queue_tail(&sk->write_queue, skb);

	atomic_inc(&sk->packets_out);
	
	skb->when = jiffies;
	buff = skb_clone(skb, GFP_ATOMIC);

	atomic_add(skb->truesize, &sk->wmem_alloc);

	tp->af_specific->queue_xmit(sk, skb->dev, buff, 1);

	tcp_reset_xmit_timer(sk, TIME_RETRANS, TCP_TIMEOUT_INIT);

	tcp_statistics.TcpOutSegs++;

	return 0;
}

/*
 *	Set up the timers for sending a delayed ack..
 *
 *      rules for delaying an ack:
 *      - delay time <= 0.5 HZ
 *      - must send at least every 2 full sized packets
 *      - we don't have a window update to send
 */

void tcp_send_delayed_ack(struct sock * sk, int max_timeout)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	unsigned long timeout, now;

	/* Calculate new timeout */
	now = jiffies;
	timeout = tp->ato;

	if (timeout > max_timeout || sk->bytes_rcv > (sk->mss << 2))
	{
		timeout = now;
	}
	else
		timeout += now;

	/* Use new timeout only if there wasn't a older one earlier  */
	if (!del_timer(&tp->delack_timer) || timeout < tp->delack_timer.expires)
	{
		tp->delack_timer.expires = timeout;
	}

	add_timer(&tp->delack_timer);
}



/*
 *	This routine sends an ack and also updates the window. 
 */
 
void tcp_send_ack(struct sock *sk)
{
	struct sk_buff *buff;
	struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);
	struct tcphdr *th;
	int tmp;

	
	if(sk->zapped)
	{
		/* We have been reset, we may not send again */
		return;		
	}

	/*
	 * We need to grab some memory, and put together an ack,
	 * and then put it into the queue to be sent.
	 */

	buff = sock_wmalloc(sk, MAX_ACK_SIZE, 1, GFP_ATOMIC);
	if (buff == NULL) 
	{
		/* 
		 *	Force it to send an ack. We don't have to do this
		 *	(ACK is unreliable) but it's much better use of 
		 *	bandwidth on slow links to send a spare ack than
		 *	resend packets. 
		 */
		 
		tcp_send_delayed_ack(sk, HZ/2);
		return;
	}

	clear_delayed_acks(sk);

	/*
	 *	Assemble a suitable TCP frame
	 */
	 
	buff->sk = sk;
	buff->localroute = sk->localroute;
	buff->csum = 0;

	/* 
	 *	Put in the IP header and routing stuff. 
	 */
	 
	tmp = tp->af_specific->build_net_header(sk, buff);

	if (tmp < 0) 
	{
  		buff->free = 1;
		sock_wfree(sk, buff);
		return;
	}

	th =(struct tcphdr *)skb_put(buff,sizeof(struct tcphdr));

	memcpy(th, &sk->dummy_th, sizeof(struct tcphdr));

	/*
	 *	Swap the send and the receive. 
	 */
	 
	th->window	= ntohs(tcp_select_window(sk));
	th->seq		= ntohl(tp->snd_nxt);
	th->ack_seq	= ntohl(tp->rcv_nxt);

  	/*
  	 *	Fill in the packet and send it
  	 */

	tp->af_specific->send_check(sk, th, sizeof(struct tcphdr), buff);

  	if (sk->debug)
  		 printk("\rtcp_send_ack: seq %x ack %x\n", 
			tp->snd_nxt, tp->rcv_nxt);

	tp->af_specific->queue_xmit(sk, buff->dev, buff, 1);

  	tcp_statistics.TcpOutSegs++;
}

/*
 *	This routine sends a packet with an out of date sequence
 *	number. It assumes the other end will try to ack it.
 */

void tcp_write_wakeup(struct sock *sk)
{
	struct sk_buff *buff, *skb;
	struct tcphdr *t1;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int tmp;

	if (sk->zapped)
		return;	/* After a valid reset we can send no more */

	/*
	 *	Write data can still be transmitted/retransmitted in the
	 *	following states.  If any other state is encountered, return.
	 *	[listen/close will never occur here anyway]
	 */

	if (sk->state != TCP_ESTABLISHED && 
	    sk->state != TCP_CLOSE_WAIT &&
	    sk->state != TCP_FIN_WAIT1 && 
	    sk->state != TCP_LAST_ACK &&
	    sk->state != TCP_CLOSING
	) 
	{
		return;
	}

	if (before(tp->snd_nxt, tp->snd_una + tp->snd_wnd) && 
	    (skb=tp->send_head))
	{
		/*
	    	 * We are probing the opening of a window
	    	 * but the window size is != 0
	    	 * must have been a result SWS avoidance ( sender )
	    	 */

		struct tcphdr *th;
		unsigned long win_size;

		win_size = tp->snd_wnd - (tp->snd_nxt - tp->snd_una);

		if (win_size < skb->end_seq - skb->seq)
		{
			if (tcp_fragment(sk, skb, win_size))
			{
				printk(KERN_DEBUG "tcp_write_wakeup: "
				       "fragment failed\n");
				return;
			}
		}

			    	
		th = skb->h.th;
		
		tp->af_specific->send_check(sk, th, th->doff * 4 + win_size, 
					    skb);

		buff = skb_clone(skb, GFP_ATOMIC);

		atomic_add(buff->truesize, &sk->wmem_alloc);
		atomic_inc(&sk->packets_out);

		clear_delayed_acks(sk);

		if (!tcp_timer_is_set(sk, TIME_RETRANS))
			tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);

		skb->when = jiffies;

		update_send_head(sk);

		tp->snd_nxt = skb->end_seq;
	}
	else
	{	
		buff = sock_wmalloc(sk,MAX_ACK_SIZE, 1, GFP_ATOMIC);
		if (buff == NULL) 
			return;

		buff->free = 1;
		buff->sk = sk;
		buff->localroute = sk->localroute;
		buff->csum = 0;

		/*
		 *	Put in the IP header and routing stuff. 
		 */
		 
		tmp = tp->af_specific->build_net_header(sk, buff);

		if (tmp < 0) 
		{
			sock_wfree(sk, buff);
			return;
		}

		t1 = (struct tcphdr *) skb_put(buff, sizeof(struct tcphdr));
		memcpy(t1,(void *) &sk->dummy_th, sizeof(*t1));

		/*
		 *	Use a previous sequence.
		 *	This should cause the other end to send an ack.
		 */
	 
		t1->seq = htonl(tp->snd_nxt-1);
/*		t1->fin = 0;	-- We are sending a 'previous' sequence, and 0 bytes of data - thus no FIN bit */
		t1->ack_seq = htonl(tp->rcv_nxt);
		t1->window = htons(tcp_select_window(sk));
		
		tp->af_specific->send_check(sk, t1, sizeof(*t1), buff);
	}		

	/*
	 *	Send it.
	 */

	tp->af_specific->queue_xmit(sk, buff->dev, buff, 1);
	tcp_statistics.TcpOutSegs++;
}

/*
 *	A window probe timeout has occurred.
 *	If window is not closed send a partial packet
 *	else a zero probe.
 */

void tcp_send_probe0(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	if (sk->zapped)
		return;		/* After a valid reset we can send no more */


	tcp_write_wakeup(sk);

	tp->pending = TIME_PROBE0;

	tp->backoff++;
	tp->probes_out++;

	tcp_reset_xmit_timer (sk, TIME_PROBE0, 
			      min(tp->rto << tp->backoff, 120*HZ));
}
