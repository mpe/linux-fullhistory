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
 * Fixes:	Eric Schenk	: avoid multiple retransmissions in one
 *				: round trip timeout.
 */

#include <linux/config.h>
#include <net/tcp.h>
#include <linux/ip_fw.h>
#include <linux/firewall.h>
#include <linux/interrupt.h>


/*
 * RFC 1122 says:
 *
 * "the suggested [SWS] avoidance algorithm for the receiver is to keep
 *  RECV.NEXT + RCV.WIN fixed until:
 *  RCV.BUFF - RCV.USER - RCV.WINDOW >= min(1/2 RCV.BUFF, MSS)"
 *
 * Experiments against BSD and Solaris machines show that following
 * these rules results in the BSD and Solaris machines making very
 * bad guesses about how much data they can have in flight.
 *
 * Instead we follow the BSD lead and offer a window that gives
 * the size of the current free space, truncated to a multiple
 * of 1024 bytes. If the window is smaller than
 * 	min(sk->mss, MAX_WINDOW/2)
 * then we advertise the window as having size 0, unless this
 * would shrink the window we offered last time.
 * This results in as much as double the throughput as the original
 * implementation.
 *
 * We do BSD style SWS avoidance -- note that RFC1122 only says we
 * must do silly window avoidance, it does not require that we use
 * the suggested algorithm.
 *
 * The "rcvbuf" and "rmem_alloc" values are shifted by 1, because
 * they also contain buffer handling overhead etc, so the window
 * we actually use is essentially based on only half those values.
 */
int tcp_new_window(struct sock * sk)
{
	unsigned long window;
	unsigned long minwin, maxwin;

	/* Get minimum and maximum window values.. */
	minwin = sk->mss;
	if (!minwin)
		minwin = sk->mtu;
	maxwin = sk->window_clamp;
	if (!maxwin)
		maxwin = MAX_WINDOW;
	if (minwin > maxwin/2)
		minwin = maxwin/2;

	/* Get current rcvbuf size.. */
	window = sk->rcvbuf/2;
	if (window < minwin) {
		sk->rcvbuf = minwin*2;
		window = minwin;
	}

	/* Check rcvbuf against used and minimum window */
	window -= sk->rmem_alloc/2;
	if ((long)(window - minwin) < 0)		/* SWS avoidance */
		window = 0;

	if (window > 1023)
		window &= ~1023;
	if (window > maxwin)
		window = maxwin;
	return window;
}

/*
 *	Get rid of any delayed acks, we sent one already..
 */
static __inline__ void clear_delayed_acks(struct sock * sk)
{
	sk->ack_timed = 0;
	sk->ack_backlog = 0;
	sk->bytes_rcv = 0;
	del_timer(&sk->delack_timer);
}

/*
 *	This is the main buffer sending routine. We queue the buffer
 *	having checked it is sane seeming.
 */
 
void tcp_send_skb(struct sock *sk, struct sk_buff *skb)
{
	int size;
	struct tcphdr * th = skb->h.th;

	/*
	 *	length of packet (not counting length of pre-tcp headers) 
	 */
	 
	size = skb->len - ((unsigned char *) th - skb->data);

	/*
	 *	Sanity check it.. 
	 */
	 
	if (size < sizeof(struct tcphdr) || size > skb->len) 
	{
		printk(KERN_ERR "tcp_send_skb: bad skb (skb = %p, data = %p, th = %p, len = %lu)\n",
			skb, skb->data, th, skb->len);
		kfree_skb(skb, FREE_WRITE);
		return;
	}

	/*
	 *	If we have queued a header size packet.. (these crash a few
	 *	tcp stacks if ack is not set)
	 */
	 
	if (size == sizeof(struct tcphdr)) 
	{
		/* If it's got a syn or fin it's notionally included in the size..*/
		if(!th->syn && !th->fin) 
		{
			printk(KERN_ERR "tcp_send_skb: attempt to queue a bogon.\n");
			kfree_skb(skb,FREE_WRITE);
			return;
		}
	}

	/*
	 * Jacobson recommends this in the appendix of his SIGCOMM'88 paper.
	 * The idea is to do a slow start again if we haven't been doing
	 * anything for a long time, in which case we have no reason to
	 * believe that our congestion window is still correct.
	 */
	if (sk->send_head == 0 && (jiffies - sk->idletime) > sk->rto)
		sk->cong_window = 1;

	/*
	 *	Actual processing.
	 */

	tcp_statistics.TcpOutSegs++;  
	skb->seq = ntohl(th->seq);
	skb->end_seq = skb->seq + size - 4*th->doff;

	/*
	 *	We must queue if
	 *
	 *	a) The right edge of this frame exceeds the window
	 *	b) We are retransmitting (Nagle's rule)
	 *	c) We have too many packets 'in flight'
	 */
	 
	if (after(skb->end_seq, sk->window_seq) ||
	    (sk->retransmits && sk->ip_xmit_timeout == TIME_WRITE) ||
	     sk->packets_out >= sk->cong_window) 
	{
		/* checksum will be supplied by tcp_write_xmit.  So
		 * we shouldn't need to set it at all.  I'm being paranoid */
		th->check = 0;
		if (skb->next != NULL) 
		{
			printk(KERN_ERR "tcp_send_partial: next != NULL\n");
			skb_unlink(skb);
		}
		skb_queue_tail(&sk->write_queue, skb);
		
		if (before(sk->window_seq, sk->write_queue.next->end_seq) &&
		    sk->send_head == NULL && sk->ack_backlog == 0)
			tcp_reset_xmit_timer(sk, TIME_PROBE0, sk->rto);
	}
	else 
	{
		/*
		 *	This is going straight out
		 */
		clear_delayed_acks(sk);		 
		th->ack_seq = htonl(sk->acked_seq);
		th->window = htons(tcp_select_window(sk));

		tcp_send_check(th, sk->saddr, sk->daddr, size, skb);

		sk->sent_seq = sk->write_seq;

		/*
		 *	This is mad. The tcp retransmit queue is put together
		 *	by the ip layer. This causes half the problems with
		 *	unroutable FIN's and other things.
		 */
		 
		sk->prot->queue_xmit(sk, skb->dev, skb, 0);
		
		/*
		 *	Set for next retransmit based on expected ACK time
		 *	of the first packet in the resend queue.
		 *	This is no longer a window behind.
		 */

		tcp_reset_xmit_timer(sk, TIME_WRITE, sk->rto);
	}
}

/*
 *	Locking problems lead us to a messy situation where we can have
 *	multiple partially complete buffers queued up. This is really bad
 *	as we don't want to be sending partial buffers. Fix this with
 *	a semaphore or similar to lock tcp_write per socket.
 *
 *	These routines are pretty self descriptive.
 */
 
struct sk_buff * tcp_dequeue_partial(struct sock * sk)
{
	struct sk_buff * skb;
	unsigned long flags;

	save_flags(flags);
	cli();
	skb = sk->partial;
	if (skb) {
		sk->partial = NULL;
		del_timer(&sk->partial_timer);
	}
	restore_flags(flags);
	return skb;
}

/*
 *	Empty the partial queue
 */
 
void tcp_send_partial(struct sock *sk)
{
	struct sk_buff *skb;

	if (sk == NULL)
		return;
	while ((skb = tcp_dequeue_partial(sk)) != NULL)
		tcp_send_skb(sk, skb);
}

/*
 *	Queue a partial frame
 */
 
void tcp_enqueue_partial(struct sk_buff * skb, struct sock * sk)
{
	struct sk_buff * tmp;
	unsigned long flags;

	save_flags(flags);
	cli();
	tmp = sk->partial;
	if (tmp)
		del_timer(&sk->partial_timer);
	sk->partial = skb;
	init_timer(&sk->partial_timer);
	/*
	 *	Wait up to 1 second for the buffer to fill.
	 */
	sk->partial_timer.expires = jiffies+HZ;
	sk->partial_timer.function = (void (*)(unsigned long)) tcp_send_partial;
	sk->partial_timer.data = (unsigned long) sk;
	add_timer(&sk->partial_timer);
	restore_flags(flags);
	if (tmp)
		tcp_send_skb(sk, tmp);
}

/*
 * 	This routine takes stuff off of the write queue,
 *	and puts it in the xmit queue. This happens as incoming acks
 *	open up the remote window for us.
 */
 
void tcp_write_xmit(struct sock *sk)
{
	struct sk_buff *skb;

	/*
	 *	The bytes will have to remain here. In time closedown will
	 *	empty the write queue and all will be happy 
	 */

	if(sk->zapped)
		return;

	/*
	 *	Anything on the transmit queue that fits the window can
	 *	be added providing we are not
	 *
	 *	a) retransmitting (Nagle's rule)
	 *	b) exceeding our congestion window.
	 */
	 
	while((skb = skb_peek(&sk->write_queue)) != NULL &&
		!after(skb->end_seq, sk->window_seq) &&
		(sk->retransmits == 0 ||
		 sk->ip_xmit_timeout != TIME_WRITE ||
		 !after(skb->end_seq, sk->rcv_ack_seq))
		&& sk->packets_out < sk->cong_window) 
	{
		IS_SKB(skb);
		skb_unlink(skb);
		
		/*
		 *	See if we really need to send the packet. 
		 */
		 
		if (before(skb->end_seq, sk->rcv_ack_seq +1)) 
		{
			/*
			 *	This is acked data. We can discard it. This 
			 *	cannot currently occur.
			 */
			 
			sk->retransmits = 0;
			kfree_skb(skb, FREE_WRITE);
			if (!sk->dead) 
				sk->write_space(sk);
		} 
		else
		{
			struct tcphdr *th;
			struct iphdr *iph;
			int size;
/*
 * put in the ack seq and window at this point rather than earlier,
 * in order to keep them monotonic.  We really want to avoid taking
 * back window allocations.  That's legal, but RFC1122 says it's frowned on.
 * Ack and window will in general have changed since this packet was put
 * on the write queue.
 */
			iph = skb->ip_hdr;
			th = (struct tcphdr *)(((char *)iph) +(iph->ihl << 2));
			size = skb->len - (((unsigned char *) th) - skb->data);
#ifndef CONFIG_NO_PATH_MTU_DISCOVERY
			if (size > sk->mtu - sizeof(struct iphdr))
			{
				iph->frag_off &= ~htons(IP_DF);
				ip_send_check(iph);
			}
#endif
			
			th->ack_seq = htonl(sk->acked_seq);
			th->window = htons(tcp_select_window(sk));

			tcp_send_check(th, sk->saddr, sk->daddr, size, skb);

			sk->sent_seq = skb->end_seq;
			
			/*
			 *	IP manages our queue for some crazy reason
			 */
			 
			sk->prot->queue_xmit(sk, skb->dev, skb, skb->free);

			clear_delayed_acks(sk);

			tcp_reset_xmit_timer(sk, TIME_WRITE, sk->rto);
		}
	}
}


/*
 *	A socket has timed out on its send queue and wants to do a
 *	little retransmitting. Currently this means TCP.
 */

void tcp_do_retransmit(struct sock *sk, int all)
{
	struct sk_buff * skb;
	struct proto *prot;
	struct device *dev;
	struct rtable *rt;

	prot = sk->prot;
	if (!all) {
		/*
		 * If we are just retransmitting one packet reset
		 * to the start of the queue.
		 */
		sk->send_next = sk->send_head;
		sk->packets_out = 0;
	}
	skb = sk->send_next;

	while (skb != NULL)
	{
		struct tcphdr *th;
		struct iphdr *iph;
		int size;

		dev = skb->dev;
		IS_SKB(skb);
		skb->when = jiffies;

		/* dl1bke 960201 - @%$$! Hope this cures strange race conditions    */
		/*		   with AX.25 mode VC. (esp. DAMA)		    */
		/*		   if the buffer is locked we should not retransmit */
		/*		   anyway, so we don't need all the fuss to prepare */
		/*		   the buffer in this case. 			    */
		/*		   (the skb_pull() changes skb->data while we may   */
		/*		   actually try to send the data. Ouch. A side	    */
		/*		   effect is that we'll send some unnecessary data, */
		/*		   but the alternative is disastrous...	    */
		
		if (skb_device_locked(skb))
			break;

		/*
		 *	Discard the surplus MAC header
		 */
		 
		skb_pull(skb,((unsigned char *)skb->ip_hdr)-skb->data);

		/*
		 * In general it's OK just to use the old packet.  However we
		 * need to use the current ack and window fields.  Urg and
		 * urg_ptr could possibly stand to be updated as well, but we
		 * don't keep the necessary data.  That shouldn't be a problem,
		 * if the other end is doing the right thing.  Since we're
		 * changing the packet, we have to issue a new IP identifier.
		 */

		iph = (struct iphdr *)skb->data;
		th = (struct tcphdr *)(((char *)iph) + (iph->ihl << 2));
		size = ntohs(iph->tot_len) - (iph->ihl<<2);
		
		/*
		 *	Note: We ought to check for window limits here but
		 *	currently this is done (less efficiently) elsewhere.
		 */

		/*
		 *	Put a MAC header back on (may cause ARPing)
		 */
		 
	        {
			/* ANK: UGLY, but the bug, that was here, should be fixed.
			 */
			struct options *  opt = (struct options*)skb->proto_priv;
			rt = ip_check_route(&sk->ip_route_cache, opt->srr?opt->faddr:iph->daddr, skb->localroute);
	        }

		iph->id = htons(ip_id_count++);
#ifndef CONFIG_NO_PATH_MTU_DISCOVERY
		if (rt && ntohs(iph->tot_len) > rt->rt_mtu)
			iph->frag_off &= ~htons(IP_DF);
#endif
		ip_send_check(iph);
			
		if (rt==NULL)	/* Deep poo */
		{
			if(skb->sk)
			{
				skb->sk->err_soft=ENETUNREACH;
				skb->sk->error_report(skb->sk);
			}
			/* Can't transmit this packet, no reason
			 * to transmit the later ones, even if
			 * the congestion window allows.
			 */
			break;
		}
		else
		{
			dev=rt->rt_dev;
			skb->raddr=rt->rt_gateway;
			skb->dev=dev;
			skb->arp=1;
#ifdef CONFIG_FIREWALL
        		if (call_out_firewall(PF_INET, skb->dev, iph, NULL) < FW_ACCEPT) {
				/* The firewall wants us to dump the packet.
			 	* We have to check this here, because
			 	* the drop in ip_queue_xmit only catches the
			 	* first time we send it. We must drop on
				* every resend as well.
			 	*/
				break;
        		}
#endif 
			if (rt->rt_hh)
			{
				memcpy(skb_push(skb,dev->hard_header_len),rt->rt_hh->hh_data,dev->hard_header_len);
				if (!rt->rt_hh->hh_uptodate)
				{
					skb->arp = 0;
#if RT_CACHE_DEBUG >= 2
					printk("tcp_do_retransmit: hh miss %08x via %08x\n", iph->daddr, rt->rt_gateway);
#endif
				}
			}
			else if (dev->hard_header)
			{
				if(dev->hard_header(skb, dev, ETH_P_IP, NULL, NULL, skb->len)<0)
					skb->arp=0;
			}
		
			/*
			 *	This is not the right way to handle this. We have to
			 *	issue an up to date window and ack report with this 
			 *	retransmit to keep the odd buggy tcp that relies on 
			 *	the fact BSD does this happy. 
			 *	We don't however need to recalculate the entire 
			 *	checksum, so someone wanting a small problem to play
			 *	with might like to implement RFC1141/RFC1624 and speed
			 *	this up by avoiding a full checksum.
			 */
		 
			th->ack_seq = htonl(sk->acked_seq);
			clear_delayed_acks(sk);
			th->window = ntohs(tcp_select_window(sk));
			tcp_send_check(th, sk->saddr, sk->daddr, size, skb);
		
			/*
			 *	If the interface is (still) up and running, kick it.
			 */
	
			if (dev->flags & IFF_UP)
			{
				/*
				 *	If the packet is still being sent by the device/protocol
				 *	below then don't retransmit. This is both needed, and good -
				 *	especially with connected mode AX.25 where it stops resends
				 *	occurring of an as yet unsent anyway frame!
				 *	We still add up the counts as the round trip time wants
				 *	adjusting.
				 */
				if (sk && !skb_device_locked(skb))
				{
					/* Remove it from any existing driver queue first! */
					skb_unlink(skb);
					/* Now queue it */
					ip_statistics.IpOutRequests++;
					dev_queue_xmit(skb, dev, sk->priority);
					sk->packets_out++;
				}
			}
		}

		/*
		 *	Count retransmissions
		 */
		 
		sk->prot->retransmits++;
		tcp_statistics.TcpRetransSegs++;

		/*
		 * Record the high sequence number to help avoid doing
		 * to much fast retransmission.
		 */
		if (sk->retransmits)
			sk->high_seq = sk->sent_seq;
		
		/*
	         * Advance the send_next pointer so we don't keep
		 * retransmitting the same stuff every time we get an ACK.
		 */
		sk->send_next = skb->link3;

		/*
		 *	Only one retransmit requested.
		 */
	
		if (!all)
			break;

		/*
		 *	This should cut it off before we send too many packets.
		 */

		if (sk->packets_out >= sk->cong_window)
			break;

		skb = skb->link3;
	}
}

/*
 *	This routine will send an RST to the other tcp. 
 */
 
void tcp_send_reset(unsigned long saddr, unsigned long daddr, struct tcphdr *th,
	  struct proto *prot, struct options *opt, struct device *dev, int tos, int ttl)
{
	struct sk_buff *buff;
	struct tcphdr *t1;
	int tmp;
	struct device *ndev=NULL;

	/*
	 *	Cannot reset a reset (Think about it).
	 */
	 
	if(th->rst)
		return;
  
	/*
	 * We need to grab some memory, and put together an RST,
	 * and then put it into the queue to be sent.
	 */

	buff = sock_wmalloc(NULL, MAX_RESET_SIZE, 1, GFP_ATOMIC);
	if (buff == NULL) 
	  	return;

	buff->sk = NULL;
	buff->dev = dev;
	buff->localroute = 0;
	buff->csum = 0;

	/*
	 *	Put in the IP header and routing stuff. 
	 */

	tmp = prot->build_header(buff, saddr, daddr, &ndev, IPPROTO_TCP, opt,
			   sizeof(struct tcphdr),tos,ttl,NULL);
	if (tmp < 0) 
	{
  		buff->free = 1;
		sock_wfree(NULL, buff);
		return;
	}

	t1 =(struct tcphdr *)skb_put(buff,sizeof(struct tcphdr));
	memset(t1, 0, sizeof(*t1));

	/*
	 *	Swap the send and the receive. 
	 */

	t1->dest = th->source;
	t1->source = th->dest;
	t1->doff = sizeof(*t1)/4;
	t1->rst = 1;
  
	if(th->ack)
	{
	  	t1->seq = th->ack_seq;
	}
	else
	{
		t1->ack = 1;
	  	if(!th->syn)
			t1->ack_seq = th->seq;
		else
			t1->ack_seq = htonl(ntohl(th->seq)+1);
	}

	tcp_send_check(t1, saddr, daddr, sizeof(*t1), buff);
	prot->queue_xmit(NULL, ndev, buff, 1);
	tcp_statistics.TcpOutSegs++;
}

/*
 *	Send a fin.
 */

void tcp_send_fin(struct sock *sk)
{
	struct proto *prot =(struct proto *)sk->prot;
	struct tcphdr *th =(struct tcphdr *)&sk->dummy_th;
	struct tcphdr *t1;
	struct sk_buff *buff;
	struct device *dev=NULL;
	int tmp;
		
	buff = sock_wmalloc(sk, MAX_RESET_SIZE,1 , GFP_KERNEL);

	if (buff == NULL)
	{
		/* This is a disaster if it occurs */
		printk(KERN_CRIT "tcp_send_fin: Impossible malloc failure");
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

	tmp = prot->build_header(buff,sk->saddr, sk->daddr, &dev,
			   IPPROTO_TCP, sk->opt,
			   sizeof(struct tcphdr),sk->ip_tos,sk->ip_ttl,&sk->ip_route_cache);
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
	buff->dev = dev;
	memcpy(t1, th, sizeof(*t1));
	buff->seq = sk->write_seq;
	sk->write_seq++;
	buff->end_seq = sk->write_seq;
	t1->seq = htonl(buff->seq);
	t1->ack_seq = htonl(sk->acked_seq);
	t1->window = htons(tcp_select_window(sk));
	t1->fin = 1;
	tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), buff);

	/*
	 * If there is data in the write queue, the fin must be appended to
	 * the write queue.
 	 */
 	
 	if (skb_peek(&sk->write_queue) != NULL) 
 	{
  		buff->free = 0;
		if (buff->next != NULL) 
		{
			printk(KERN_ERR "tcp_send_fin: next != NULL\n");
			skb_unlink(buff);
		}
		skb_queue_tail(&sk->write_queue, buff);
  	} 
  	else 
  	{
        	sk->sent_seq = sk->write_seq;
		sk->prot->queue_xmit(sk, dev, buff, 0);
		tcp_reset_xmit_timer(sk, TIME_WRITE, sk->rto);
	}
}


void tcp_send_synack(struct sock * newsk, struct sock * sk, struct sk_buff * skb)
{
	struct tcphdr *t1;
	unsigned char *ptr;
	struct sk_buff * buff;
	struct device *ndev=NULL;
	int tmp;

	buff = sock_wmalloc(newsk, MAX_SYN_SIZE, 1, GFP_ATOMIC);
	if (buff == NULL) 
	{
		sk->err = ENOMEM;
		destroy_sock(newsk);
		kfree_skb(skb, FREE_READ);
		tcp_statistics.TcpAttemptFails++;
		return;
	}
  
	buff->sk = newsk;
	buff->localroute = newsk->localroute;

	/*
	 *	Put in the IP header and routing stuff. 
	 */

	tmp = sk->prot->build_header(buff, newsk->saddr, newsk->daddr, &ndev,
			       IPPROTO_TCP, newsk->opt, MAX_SYN_SIZE,sk->ip_tos,sk->ip_ttl,&newsk->ip_route_cache);

	/*
	 *	Something went wrong. 
	 */

	if (tmp < 0) 
	{
		sk->err = tmp;
		buff->free = 1;
		kfree_skb(buff,FREE_WRITE);
		destroy_sock(newsk);
		skb->sk = sk;
		kfree_skb(skb, FREE_READ);
		tcp_statistics.TcpAttemptFails++;
		return;
	}

	t1 =(struct tcphdr *)skb_put(buff,sizeof(struct tcphdr));
  
	memcpy(t1, skb->h.th, sizeof(*t1));
	buff->seq = newsk->write_seq++;
	buff->end_seq = newsk->write_seq;
	/*
	 *	Swap the send and the receive. 
	 */
	t1->dest = skb->h.th->source;
	t1->source = newsk->dummy_th.source;
	t1->seq = ntohl(buff->seq);
	newsk->sent_seq = newsk->write_seq;
	t1->window = ntohs(tcp_select_window(newsk));
	t1->syn = 1;
	t1->ack = 1;
	t1->urg = 0;
	t1->rst = 0;
	t1->psh = 0;
	t1->ack_seq = htonl(newsk->acked_seq);
	t1->doff = sizeof(*t1)/4+1;
	ptr = skb_put(buff,4);
	ptr[0] = 2;
	ptr[1] = 4;
	ptr[2] = ((newsk->mtu) >> 8) & 0xff;
	ptr[3] =(newsk->mtu) & 0xff;
	buff->csum = csum_partial(ptr, 4, 0);
	tcp_send_check(t1, newsk->saddr, newsk->daddr, sizeof(*t1)+4, buff);
	newsk->prot->queue_xmit(newsk, ndev, buff, 0);
	tcp_reset_xmit_timer(newsk, TIME_WRITE , TCP_TIMEOUT_INIT);
	skb->sk = newsk;

	/*
	 *	Charge the sock_buff to newsk. 
	 */
	 
	atomic_sub(skb->truesize, &sk->rmem_alloc);
	atomic_add(skb->truesize, &newsk->rmem_alloc);
	
	skb_queue_tail(&sk->receive_queue,skb);
	sk->ack_backlog++;
	tcp_statistics.TcpOutSegs++;
}

/*
 *	Set up the timers for sending a delayed ack..
 *
 *      rules for delaying an ack:
 *      - delay time <= 0.5 HZ
 *      - must send at least every 2 full sized packets
 *      - we don't have a window update to send
 *
 * 	additional thoughts:
 *	- we should not delay sending an ACK if we have ato > 0.5 HZ.
 *	  My thinking about this is that in this case we will just be
 *	  systematically skewing the RTT calculation. (The rule about
 *	  sending every two full sized packets will never need to be
 *	  invoked, the delayed ack will be sent before the ATO timeout
 *	  every time. Of course, the relies on our having a good estimate
 *	  for packet interarrival times.)
 */
void tcp_send_delayed_ack(struct sock * sk, int max_timeout, unsigned long timeout)
{
	unsigned long now;
	static int delack_guard=0;

	if(delack_guard)
		return;
	
	delack_guard++;
	
	/* Calculate new timeout */
	now = jiffies;
	if (timeout > max_timeout)
		timeout = max_timeout;
	timeout += now;
	if (sk->bytes_rcv >= sk->max_unacked) {
		tcp_send_ack(sk);
		delack_guard--;
		return;
	}

	/* Use new timeout only if there wasn't a older one earlier  */
	if (!del_timer(&sk->delack_timer) || timeout < sk->delack_timer.expires)
		sk->delack_timer.expires = timeout;

	sk->ack_backlog++;
	add_timer(&sk->delack_timer);
	delack_guard--;
}



/*
 *	This routine sends an ack and also updates the window. 
 */
 
void tcp_send_ack(struct sock *sk)
{
	struct sk_buff *buff;
	struct tcphdr *t1;
	struct device *dev = NULL;
	int tmp;

	if(sk->zapped)
		return;		/* We have been reset, we may not send again */
		
	/*
	 *	If we have nothing queued for transmit and the transmit timer
	 *	is on we are just doing an ACK timeout and need to switch
	 *	to a keepalive.
	 */

	clear_delayed_acks(sk);

	if (sk->send_head == NULL
	    && skb_queue_empty(&sk->write_queue)
	    && sk->ip_xmit_timeout == TIME_WRITE)
	{
		if (sk->keepopen)
			tcp_reset_xmit_timer(sk,TIME_KEEPOPEN,TCP_TIMEOUT_LEN);
		else
			del_timer(&sk->retransmit_timer);
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

		tcp_send_delayed_ack(sk, HZ/2, HZ/2);
		return;
	}

	/*
	 *	Assemble a suitable TCP frame
	 */
	 
	buff->sk = sk;
	buff->localroute = sk->localroute;
	buff->csum = 0;

	/* 
	 *	Put in the IP header and routing stuff. 
	 */
	 
	tmp = sk->prot->build_header(buff, sk->saddr, sk->daddr, &dev,
				IPPROTO_TCP, sk->opt, MAX_ACK_SIZE,sk->ip_tos,sk->ip_ttl,&sk->ip_route_cache);
	if (tmp < 0) 
	{
  		buff->free = 1;
		sock_wfree(sk, buff);
		return;
	}
	t1 =(struct tcphdr *)skb_put(buff,sizeof(struct tcphdr));

  	/*
  	 *	Fill in the packet and send it
  	 */
  	 
	memcpy(t1, &sk->dummy_th, sizeof(*t1));
	t1->seq     = htonl(sk->sent_seq);
  	t1->ack_seq = htonl(sk->acked_seq);
	t1->window  = htons(tcp_select_window(sk));

  	tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), buff);
  	if (sk->debug)
  		 printk(KERN_ERR "\rtcp_ack: seq %x ack %x\n", sk->sent_seq, sk->acked_seq);
  	sk->prot->queue_xmit(sk, dev, buff, 1);
  	tcp_statistics.TcpOutSegs++;
}

/*
 *	This routine sends a packet with an out of date sequence
 *	number. It assumes the other end will try to ack it.
 */

void tcp_write_wakeup(struct sock *sk)
{
	struct sk_buff *buff,*skb;
	struct tcphdr *t1;
	struct device *dev=NULL;
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
	if ( before(sk->sent_seq, sk->window_seq) && 
	    (skb=skb_peek(&sk->write_queue)))
	{
		/*
	    	 * We are probing the opening of a window
	    	 * but the window size is != 0
	    	 * must have been a result SWS avoidance ( sender )
	    	 */
	    
	    	struct iphdr *iph;
	    	struct tcphdr *th;
	    	struct tcphdr *nth;
	    	unsigned long win_size;
#if 0
		unsigned long ow_size;
#endif
	
		/*
		 *	How many bytes can we send ?
		 */
		 
		win_size = sk->window_seq - sk->sent_seq;

		/*
		 *	Recover the buffer pointers
		 */
		 
	    	iph = (struct iphdr *)skb->ip_hdr;
	    	th = (struct tcphdr *)(((char *)iph) +(iph->ihl << 2));

		/*
		 *	Grab the data for a temporary frame
		 */
		 
	    	buff = sock_wmalloc(sk, win_size + th->doff * 4 + 
				     (iph->ihl << 2) +
				     sk->prot->max_header + 15, 
				     1, GFP_ATOMIC);
	    	if ( buff == NULL )
	    		return;

	 	/* 
	 	 *	If we strip the packet on the write queue we must
	 	 *	be ready to retransmit this one 
	 	 */
	    
	    	buff->free = /*0*/1;

	    	buff->sk = sk;
	    	buff->localroute = sk->localroute;
	    	
	    	/*
	    	 *	Put headers on the new packet
	    	 */

	    	tmp = sk->prot->build_header(buff, sk->saddr, sk->daddr, &dev,
					 IPPROTO_TCP, sk->opt, buff->truesize,
					 sk->ip_tos,sk->ip_ttl,&sk->ip_route_cache);
	    	if (tmp < 0) 
	    	{
			sock_wfree(sk, buff);
			return;
		}
		
		/*
		 *	Move the TCP header over
		 */

		buff->dev = dev;

		nth = (struct tcphdr *) skb_put(buff,sizeof(*th));

		memcpy(nth, th, sizeof(*th));
		
		/*
		 *	Correct the new header
		 */
		 
		nth->ack = 1; 
		nth->ack_seq = htonl(sk->acked_seq);
		nth->window = htons(tcp_select_window(sk));
		nth->check = 0;

		/*
		 *	Copy TCP options and data start to our new buffer
		 */
		 
		buff->csum = csum_partial_copy((void *)(th + 1), skb_put(buff,win_size),
				win_size + th->doff*4 - sizeof(*th), 0);
		
		/*
		 *	Remember our right edge sequence number.
		 */
		 
	    	buff->end_seq = sk->sent_seq + win_size;
	    	sk->sent_seq = buff->end_seq;		/* Hack */
		if(th->urg && ntohs(th->urg_ptr) < win_size)
			nth->urg = 0;

		/*
		 *	Checksum the split buffer
		 */
		 
	    	tcp_send_check(nth, sk->saddr, sk->daddr, 
			   nth->doff * 4 + win_size , buff);
	}
	else
	{	
		buff = sock_wmalloc(sk,MAX_ACK_SIZE,1, GFP_ATOMIC);
		if (buff == NULL) 
			return;

		buff->free = 1;
		buff->sk = sk;
		buff->localroute = sk->localroute;
		buff->csum = 0;

		/*
		 *	Put in the IP header and routing stuff. 
		 */
		 
		tmp = sk->prot->build_header(buff, sk->saddr, sk->daddr, &dev,
				IPPROTO_TCP, sk->opt, MAX_ACK_SIZE,sk->ip_tos,sk->ip_ttl,&sk->ip_route_cache);
		if (tmp < 0) 
		{
			sock_wfree(sk, buff);
			return;
		}

		t1 = (struct tcphdr *)skb_put(buff,sizeof(struct tcphdr));
		memcpy(t1,(void *) &sk->dummy_th, sizeof(*t1));

		/*
		 *	Use a previous sequence.
		 *	This should cause the other end to send an ack.
		 */
	 
		t1->seq = htonl(sk->sent_seq-1);
/*		t1->fin = 0;	-- We are sending a 'previous' sequence, and 0 bytes of data - thus no FIN bit */
		t1->ack_seq = htonl(sk->acked_seq);
		t1->window = htons(tcp_select_window(sk));
		tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), buff);

	}		

	/*
	 *	Send it.
	 */
	
	sk->prot->queue_xmit(sk, dev, buff, 1);
	tcp_statistics.TcpOutSegs++;
}

/*
 *	A window probe timeout has occurred.
 */

void tcp_send_probe0(struct sock *sk)
{
	if (sk->zapped)
		return;		/* After a valid reset we can send no more */

	tcp_write_wakeup(sk);

	sk->backoff++;
	sk->rto = min(sk->rto << 1, 120*HZ);
	sk->retransmits++;
	sk->prot->retransmits ++;
	tcp_reset_xmit_timer (sk, TIME_PROBE0, sk->rto);
}
