/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_output.c,v 1.65 1998/03/15 12:07:03 davem Exp $
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
 *
 */

#include <net/tcp.h>

extern int sysctl_tcp_timestamps;
extern int sysctl_tcp_window_scaling;

/* Get rid of any delayed acks, we sent one already.. */
static __inline__ void clear_delayed_acks(struct sock * sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	tp->delayed_acks = 0;
	if(tcp_in_quickack_mode(tp))
		tp->ato = ((HZ/100)*2);
	tcp_clear_xmit_timer(sk, TIME_DACK);
}

static __inline__ void update_send_head(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	
	tp->send_head = tp->send_head->next;
	if (tp->send_head == (struct sk_buff *) &sk->write_queue)
		tp->send_head = NULL;
}

/*
 *	This is the main buffer sending routine. We queue the buffer
 *	having checked it is sane seeming.
 */
 
void tcp_send_skb(struct sock *sk, struct sk_buff *skb, int force_queue)
{
	struct tcphdr *th = skb->h.th;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int size;

	/* Length of packet (not counting length of pre-tcp headers). */
	size = skb->len - ((unsigned char *) th - skb->data);

	/* If there is a FIN or a SYN we add it onto the size. */
	if (th->fin || th->syn) {
		if(th->syn)
			size++;
		if(th->fin)
			size++;
	}

	/* Actual processing. */
	skb->seq = ntohl(th->seq);
	skb->end_seq = skb->seq + size - 4*th->doff;

	skb_queue_tail(&sk->write_queue, skb);

	if (!force_queue && tp->send_head == NULL && tcp_snd_test(sk, skb)) {
		struct sk_buff * buff;

		/* This is going straight out. */
		tp->last_ack_sent = tp->rcv_nxt;
		th->ack_seq = htonl(tp->rcv_nxt);
		th->window = htons(tcp_select_window(sk));
		tcp_update_options((__u32 *)(th + 1),tp);

		tp->af_specific->send_check(sk, th, size, skb);

		buff = skb_clone(skb, GFP_KERNEL);
		if (buff == NULL)
			goto queue;
		
		clear_delayed_acks(sk);
		skb_set_owner_w(buff, sk);

		tp->snd_nxt = skb->end_seq;
		tp->packets_out++;

		skb->when = jiffies;

		tcp_statistics.TcpOutSegs++;
		tp->af_specific->queue_xmit(buff);

		if (!tcp_timer_is_set(sk, TIME_RETRANS))
			tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);

		return;
	}

queue:
	/* Remember where we must start sending. */
	if (tp->send_head == NULL)
		tp->send_head = skb;
	if (!force_queue && tp->packets_out == 0 && !tp->pending) {
		tp->pending = TIME_PROBE0;
		tcp_reset_xmit_timer(sk, TIME_PROBE0, tp->rto);
	}
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

	/* Size of new segment. */
	nsize = skb->tail - ((unsigned char *)(th)+tp->tcp_header_len) - len;
	if (nsize <= 0) {
		printk(KERN_DEBUG "tcp_fragment: bug size <= 0\n");
		return -1;
	}

	/* Get a new skb... force flag on. */
	buff = sock_wmalloc(sk, nsize + 128 + sk->prot->max_header + 15, 1, 
			    GFP_ATOMIC);
	if (buff == NULL)
		return -1;

	/* Put headers on the new packet. */
	tmp = tp->af_specific->build_net_header(sk, buff);
	if (tmp < 0) {
		kfree_skb(buff);
		return -1;
	}
		
	/* Move the TCP header over. */
	nth = (struct tcphdr *) skb_put(buff, tp->tcp_header_len);
	buff->h.th = nth;
	memcpy(nth, th, tp->tcp_header_len);

	/* Correct the new header. */
	buff->seq = skb->seq + len;
	buff->end_seq = skb->end_seq;
	nth->seq = htonl(buff->seq);
	nth->check = 0;
	nth->doff  = th->doff;
	
	/* urg data is always an headache */
	if (th->urg) {
		if (th->urg_ptr > len) {
			th->urg = 0;
			nth->urg_ptr -= len;
		} else {
			nth->urg = 0;
		}
	}

	/* Copy data tail to our new buffer. */
	buff->csum = csum_partial_copy(((u8 *)(th)+tp->tcp_header_len) + len,
				       skb_put(buff, nsize),
				       nsize, 0);

	skb->end_seq -= nsize;
	skb_trim(skb, skb->len - nsize);

	/* Remember to checksum this packet afterwards. */
	th->check = 0;
	skb->csum = csum_partial((u8*)(th) + tp->tcp_header_len, skb->tail - ((u8 *) (th)+tp->tcp_header_len),
				 0);

	skb_append(skb, buff);

	return 0;
}

static void tcp_wrxmit_prob(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* This is acked data. We can discard it. This cannot currently occur. */
	tp->retransmits = 0;

	printk(KERN_DEBUG "tcp_write_xmit: bug skb in write queue\n");

	update_send_head(sk);

	skb_unlink(skb);	
	kfree_skb(skb);

	if (!sk->dead)
		sk->write_space(sk);
}

static int tcp_wrxmit_frag(struct sock *sk, struct sk_buff *skb, int size)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	
	SOCK_DEBUG(sk, "tcp_write_xmit: frag needed size=%d mss=%d\n",
		   size, sk->mss);

	if (tcp_fragment(sk, skb, sk->mss)) {
		/* !tcp_frament Failed! */
		tp->send_head = skb;
		tp->packets_out--;
		return -1;
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
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	u16 rcv_wnd;
	int sent_pkts = 0;

	/* The bytes will have to remain here. In time closedown will
	 * empty the write queue and all will be happy.
	 */
	if(sk->zapped)
		return;

	/*	Anything on the transmit queue that fits the window can
	 *	be added providing we are:
	 *
	 *	a) following SWS avoidance [and Nagle algorithm]
	 *	b) not exceeding our congestion window.
	 *	c) not retransmiting [Nagle]
	 */
	rcv_wnd = htons(tcp_select_window(sk));
	while((skb = tp->send_head) && tcp_snd_test(sk, skb)) {
		struct tcphdr *th;
		struct sk_buff *buff;
		int size;

		/* See if we really need to send the packet. (debugging code) */
		if (!after(skb->end_seq, tp->snd_una)) {
			tcp_wrxmit_prob(sk, skb);
			continue;
		}

		/*	Put in the ack seq and window at this point rather
		 *	than earlier, in order to keep them monotonic.
		 *	We really want to avoid taking back window allocations.
		 *	That's legal, but RFC1122 says it's frowned on.
		 *	Ack and window will in general have changed since
		 *	this packet was put on the write queue.
		 */
		th = skb->h.th;
		size = skb->len - (((unsigned char *) th) - skb->data);
		if (size - (th->doff << 2) > sk->mss) {
			if (tcp_wrxmit_frag(sk, skb, size))
				break;
			size = skb->len - (((unsigned char*)th) - skb->data);
		}

		tp->last_ack_sent = tp->rcv_nxt;
		th->ack_seq = htonl(tp->rcv_nxt);
		th->window = rcv_wnd;
		tcp_update_options((__u32 *)(th + 1),tp);

		tp->af_specific->send_check(sk, th, size, skb);

#ifdef TCP_DEBUG
		if (before(skb->end_seq, tp->snd_nxt))
			printk(KERN_DEBUG "tcp_write_xmit:"
			       " sending already sent seq\n");
#endif

		buff = skb_clone(skb, GFP_ATOMIC);
		if (buff == NULL)
			break;

		/* Advance the send_head.  This one is going out. */
		update_send_head(sk);
		clear_delayed_acks(sk);

		tp->packets_out++;
		skb_set_owner_w(buff, sk);

		tp->snd_nxt = skb->end_seq;

		skb->when = jiffies;

		sent_pkts = 1;
		tp->af_specific->queue_xmit(buff);
	}

	if (sent_pkts && !tcp_timer_is_set(sk, TIME_RETRANS))
		tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
}



/* This function returns the amount that we can raise the
 * usable window based on the following constraints
 *  
 * 1. The window can never be shrunk once it is offered (RFC 793)
 * 2. We limit memory per socket
 *
 * RFC 1122:
 * "the suggested [SWS] avoidance algoritm for the receiver is to keep
 *  RECV.NEXT + RCV.WIN fixed until:
 *  RCV.BUFF - RCV.USER - RCV.WINDOW >= min(1/2 RCV.BUFF, MSS)"
 *
 * i.e. don't raise the right edge of the window until you can raise
 * it at least MSS bytes.
 *
 * Unfortunately, the recomended algorithm breaks header prediction,
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
 * FIXME: In our current implementation the value returned by sock_rpsace(sk)
 * is the total space we have allocated to the socket to store skbuf's.
 * The current design assumes that up to half of that space will be
 * taken by headers, and the remaining space will be available for TCP data.
 * This should be accounted for correctly instead.
 */
u32 __tcp_select_window(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	unsigned int mss = sk->mss;
	unsigned int free_space;
	u32 window, cur_win;

	free_space = (sk->rcvbuf - atomic_read(&sk->rmem_alloc)) / 2;
	if (tp->window_clamp) {
		free_space = min(tp->window_clamp, free_space);
		mss = min(tp->window_clamp, mss);
	} else {
		printk("tcp_select_window: tp->window_clamp == 0.\n");
	}

	if (mss < 1) {
		mss = 1;
		printk("tcp_select_window: sk->mss fell to 0.\n");
	}
	
	cur_win = tcp_receive_window(tp);
	if (free_space < sk->rcvbuf/4 && free_space < mss/2) {
		window = 0;
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
		if ((window <= (free_space - mss)) || (window > free_space))
			window = (free_space/mss)*mss;
	}
	return window;
}

static int tcp_retrans_try_collapse(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct tcphdr *th1, *th2;
	int size1, size2, avail;
	struct sk_buff *buff = skb->next;

	th1 = skb->h.th;

	if (th1->urg)
		return -1;

	avail = skb_tailroom(skb);

	/* Size of TCP payload. */
	size1 = skb->tail - ((u8 *) (th1)+(th1->doff<<2));
	
	th2 = buff->h.th;
	size2 = buff->tail - ((u8 *) (th2)+(th2->doff<<2)); 

	if (size2 > avail || size1 + size2 > sk->mss )
		return -1;

	/* Ok.  We will be able to collapse the packet. */
	skb_unlink(buff);
	memcpy(skb_put(skb, size2), ((char *) th2) + (th2->doff << 2), size2);
	
	/* Update sizes on original skb, both TCP and IP. */
	skb->end_seq += buff->end_seq - buff->seq;
	if (th2->urg) {
		th1->urg = 1;
		th1->urg_ptr = th2->urg_ptr + size1;
	}
	if (th2->fin)
		th1->fin = 1;

	/* ... and off you go. */
	kfree_skb(buff);
	tp->packets_out--;

	/* Header checksum will be set by the retransmit procedure
	 * after calling rebuild header.
	 */
	th1->check = 0;
	skb->csum = csum_partial((u8*)(th1)+(th1->doff<<2), size1 + size2, 0);
	return 0;
}

/* Do a simple retransmit without using the backoff mechanisms in
 * tcp_timer. This is used to speed up path mtu recovery. Note that
 * these simple retransmit aren't counted in the usual tcp retransmit
 * backoff counters. 
 * The socket is already locked here.
 */ 
void tcp_simple_retransmit(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Clear delay ack timer. */
 	tcp_clear_xmit_timer(sk, TIME_DACK);
 
 	tp->retrans_head = NULL; 
 	/* Don't muck with the congestion window here. */
 	tp->dup_acks = 0;
 	tp->high_seq = tp->snd_nxt;
 	/* FIXME: make the current rtt sample invalid */
 	tcp_do_retransmit(sk, 0); 
}

/*
 *	A socket has timed out on its send queue and wants to do a
 *	little retransmitting.
 *	retrans_head can be different from the head of the write_queue
 *	if we are doing fast retransmit.
 */

void tcp_do_retransmit(struct sock *sk, int all)
{
	struct sk_buff * skb;
	int ct=0;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	if (tp->retrans_head == NULL)
		tp->retrans_head = skb_peek(&sk->write_queue);

	if (tp->retrans_head == tp->send_head)
		tp->retrans_head = NULL;
	
	while ((skb = tp->retrans_head) != NULL) {
		struct sk_buff *buff;
		struct tcphdr *th;
		int tcp_size;
		int size;

		/* In general it's OK just to use the old packet.  However we
		 * need to use the current ack and window fields.  Urg and
		 * urg_ptr could possibly stand to be updated as well, but we
		 * don't keep the necessary data.  That shouldn't be a problem,
		 * if the other end is doing the right thing.  Since we're
		 * changing the packet, we have to issue a new IP identifier.
		 */

		th = skb->h.th;

		tcp_size = skb->tail - ((unsigned char *)(th)+tp->tcp_header_len);

		if (tcp_size > sk->mss) {
			if (tcp_fragment(sk, skb, sk->mss)) {
				printk(KERN_DEBUG "tcp_fragment failed\n");
				return;
			}
			tp->packets_out++;
		}

		if (!th->syn &&
		    tcp_size < (sk->mss >> 1) &&
		    skb->next != tp->send_head &&
		    skb->next != (struct sk_buff *)&sk->write_queue)
			tcp_retrans_try_collapse(sk, skb);

		if (tp->af_specific->rebuild_header(sk, skb)) {
#ifdef TCP_DEBUG
			printk(KERN_DEBUG "tcp_do_rebuild_header failed\n");
#endif
			break;
		}

		SOCK_DEBUG(sk, "retransmit sending seq=%x\n", skb->seq);

		/* Update ack and window. */
		tp->last_ack_sent = th->ack_seq = htonl(tp->rcv_nxt);
		th->window = ntohs(tcp_select_window(sk));
		tcp_update_options((__u32 *)(th+1),tp);

		size = skb->tail - (unsigned char *) th;
		tp->af_specific->send_check(sk, th, size, skb);

		skb->when = jiffies;

		buff = skb_clone(skb, GFP_ATOMIC);
		if (buff == NULL)
			break;

		skb_set_owner_w(buff, sk);

		clear_delayed_acks(sk);
		tp->af_specific->queue_xmit(buff);
		
		/* Count retransmissions. */
		ct++;
		sk->prot->retransmits++;
		tcp_statistics.TcpRetransSegs++;

		/* Only one retransmit requested. */
		if (!all)
			break;

		/* This should cut it off before we send too many packets. */
		if (ct >= tp->snd_cwnd)
			break;

		/* Advance the pointer. */
		tp->retrans_head = skb->next;
		if ((tp->retrans_head == tp->send_head) ||
		    (tp->retrans_head == (struct sk_buff *) &sk->write_queue))
			tp->retrans_head = NULL;
	}
}

/* Send a fin.  The caller locks the socket for us.  This cannot be
 * allowed to fail queueing a FIN frame under any circumstances.
 */
void tcp_send_fin(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);	
	
	/* Optimization, tack on the FIN if we have a queue of
	 * unsent frames.
	 */
	if(tp->send_head != NULL) {
		struct sk_buff *tail = skb_peek_tail(&sk->write_queue);
		struct tcphdr *th = tail->h.th;
		int data_len;

		/* Unfortunately tcp_write_xmit won't check for going over
		 * the MSS due to the FIN sequence number, so we have to
		 * watch out for it here.
		 */
		data_len = (tail->tail - (((unsigned char *)th)+tp->tcp_header_len));
		if(data_len >= sk->mss)
			goto build_new_frame; /* ho hum... */

		/* tcp_write_xmit() will checksum the header etc. for us. */
		th->fin = 1;
		tail->end_seq++;
	} else {
		struct sk_buff *buff;
		struct tcphdr *th;

build_new_frame:
		buff = sock_wmalloc(sk,
				    (BASE_ACK_SIZE + tp->tcp_header_len +
				     sizeof(struct sk_buff)),
				    1, GFP_KERNEL);
		if (buff == NULL) {
			/* We can only fail due to low memory situations, not
			 * due to going over our sndbuf limits (due to the
			 * force flag passed to sock_wmalloc).  So just keep
			 * trying.  We cannot allow this fail.  The socket is
			 * still locked, so we need not check if the connection
			 * was reset in the meantime etc.
			 */
			goto build_new_frame;
		}

		/* Administrivia. */
		buff->csum = 0;

		/* Put in the IP header and routing stuff.
		 *
		 * FIXME:
		 * We can fail if the interface for the route
		 * this socket takes goes down right before
		 * we get here.  ANK is there a way to point
		 * this into a "black hole" route in such a
		 * case?  Ideally, we should still be able to
		 * queue this and let the retransmit timer
		 * keep trying until the destination becomes
		 * reachable once more.  -DaveM
		 */
		if(tp->af_specific->build_net_header(sk, buff) < 0) {
			kfree_skb(buff);
			goto update_write_seq;
		}
		th = (struct tcphdr *) skb_put(buff, tp->tcp_header_len);
		buff->h.th = th;

		memcpy(th, (void *) &(sk->dummy_th), sizeof(*th));
		th->seq = htonl(tp->write_seq);
		th->fin = 1;
		tcp_build_options((__u32 *)(th + 1), tp);

		/* This makes sure we do things like abide by the congestion
		 * window and other constraints which prevent us from sending.
		 */
		tcp_send_skb(sk, buff, 0);
	}
update_write_seq:
	/* So that we recognize the ACK coming back for
	 * this FIN as being legitimate.
	 */
	tp->write_seq++;
}

/* We get here when a process closes a file descriptor (either due to
 * an explicit close() or as a byproduct of exit()'ing) and there
 * was unread data in the receive queue.  This behavior is recommended
 * by draft-ietf-tcpimpl-prob-03.txt section 3.10.  -DaveM
 */
void tcp_send_active_reset(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb;
	struct tcphdr *th;

again:
	/* NOTE: No TCP options attached and we never retransmit this. */
	skb = sock_wmalloc(sk, (BASE_ACK_SIZE + sizeof(*th)), 1, GFP_KERNEL);
	if(skb == NULL)
		goto again;
	skb->csum = 0;
	if(tp->af_specific->build_net_header(sk, skb) < 0) {
		kfree_skb(skb);
	} else {
		th = (struct tcphdr *) skb_put(skb, sizeof(*th));
		memcpy(th, &(sk->dummy_th), sizeof(*th));
		th->seq = htonl(tp->write_seq);
		th->rst = 1;
		th->doff = sizeof(*th) / 4;
		tp->last_ack_sent = tp->rcv_nxt;
		th->ack_seq = htonl(tp->rcv_nxt);
		th->window = htons(tcp_select_window(sk));
		tp->af_specific->send_check(sk, th, sizeof(*th), skb);
		tp->af_specific->queue_xmit(skb);
		tcp_statistics.TcpOutSegs++;
		tcp_statistics.TcpOutRsts++;
	}
}

/* WARNING: This routine must only be called when we have already sent
 * a SYN packet that crossed the incoming SYN that caused this routine
 * to get called. If this assumption fails then the initial rcv_wnd
 * and rcv_wscale values will not be correct.
 *
 * XXX When you have time Dave, redo this to use tcp_send_skb() just
 * XXX like tcp_send_fin() above now does.... -DaveM
 */
int tcp_send_synack(struct sock *sk)
{
	struct tcp_opt * tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff * skb;	
	struct sk_buff * buff;
	struct tcphdr *th;
	int tmp;
	
	skb = sock_wmalloc(sk, MAX_SYN_SIZE + sizeof(struct sk_buff), 1, GFP_ATOMIC);
	if (skb == NULL) 
		return -ENOMEM;

	tmp = tp->af_specific->build_net_header(sk, skb);
	if (tmp < 0) {
		kfree_skb(skb);
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

	/* This is a resend of a previous SYN, now with an ACK.
	 * we must reuse the previously offered window.
	 */
	th->window = htons(tp->rcv_wnd);

	tp->last_ack_sent = th->ack_seq = htonl(tp->rcv_nxt);

	tmp = tcp_syn_build_options(skb, sk->mss,
		tp->tstamp_ok, tp->wscale_ok, tp->rcv_wscale);
	skb->csum = 0;
	th->doff = (sizeof(*th) + tmp)>>2;

	tp->af_specific->send_check(sk, th, sizeof(*th)+tmp, skb);

	skb_queue_tail(&sk->write_queue, skb);
	
	buff = skb_clone(skb, GFP_ATOMIC);
	if (buff) {
		skb_set_owner_w(buff, sk);

		tp->packets_out++;
		skb->when = jiffies;

		tp->af_specific->queue_xmit(buff);
		tcp_statistics.TcpOutSegs++;

		tcp_reset_xmit_timer(sk, TIME_RETRANS, TCP_TIMEOUT_INIT);
	}
	return 0;
}

/*
 * Send out a delayed ack, the caller does the policy checking
 * to see if we should even be here.  See tcp_input.c:tcp_ack_snd_check()
 * for details.
 */

void tcp_send_delayed_ack(struct tcp_opt *tp, int max_timeout)
{
	unsigned long timeout;

	/* Stay within the limit we were given */
	timeout = tp->ato;
	if (timeout > max_timeout)
		timeout = max_timeout;
	timeout += jiffies;

	/* Use new timeout only if there wasn't a older one earlier. */
	if ((!tp->delack_timer.prev || !del_timer(&tp->delack_timer)) ||
	    (timeout < tp->delack_timer.expires))
		tp->delack_timer.expires = timeout;

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
		return;	/* We have been reset, we may not send again. */

	/* We need to grab some memory, and put together an ack,
	 * and then put it into the queue to be sent.
	 */
	buff = sock_wmalloc(sk, BASE_ACK_SIZE + tp->tcp_header_len, 1, GFP_ATOMIC);
	if (buff == NULL) {
		/*	Force it to send an ack. We don't have to do this
		 *	(ACK is unreliable) but it's much better use of
		 *	bandwidth on slow links to send a spare ack than
		 *	resend packets.
		 */
		tcp_send_delayed_ack(tp, HZ/2);
		return;
	}

	clear_delayed_acks(sk);

	/* Assemble a suitable TCP frame. */
	buff->csum = 0;

	/* Put in the IP header and routing stuff. */
	tmp = tp->af_specific->build_net_header(sk, buff);
	if (tmp < 0) {
		kfree_skb(buff);
		return;
	}

	th = (struct tcphdr *)skb_put(buff,tp->tcp_header_len);
	memcpy(th, &sk->dummy_th, sizeof(struct tcphdr));

	/* Swap the send and the receive. */
	th->window	= ntohs(tcp_select_window(sk));
	th->seq		= ntohl(tp->snd_nxt);
	tp->last_ack_sent = tp->rcv_nxt;
	th->ack_seq	= htonl(tp->rcv_nxt);
	tcp_build_and_update_options((__u32 *)(th + 1), tp);

  	/* Fill in the packet and send it. */
	tp->af_specific->send_check(sk, th, tp->tcp_header_len, buff);
	tp->af_specific->queue_xmit(buff);
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
		return;	/* After a valid reset we can send no more. */

	/*	Write data can still be transmitted/retransmitted in the
	 *	following states.  If any other state is encountered, return.
	 *	[listen/close will never occur here anyway]
	 */
	if ((1 << sk->state) &
	    ~(TCPF_ESTABLISHED|TCPF_CLOSE_WAIT|TCPF_FIN_WAIT1|TCPF_LAST_ACK|TCPF_CLOSING))
		return;

	if (before(tp->snd_nxt, tp->snd_una + tp->snd_wnd) && (skb=tp->send_head)) {
		struct tcphdr *th;
		unsigned long win_size;

		/* We are probing the opening of a window
	    	 * but the window size is != 0
	    	 * must have been a result SWS avoidance ( sender )
	    	 */
		win_size = tp->snd_wnd - (tp->snd_nxt - tp->snd_una);
		if (win_size < skb->end_seq - skb->seq) {
			if (tcp_fragment(sk, skb, win_size)) {
				printk(KERN_DEBUG "tcp_write_wakeup: "
				       "fragment failed\n");
				return;
			}
		}

		th = skb->h.th;
		tcp_update_options((__u32 *)(th + 1), tp);
		tp->af_specific->send_check(sk, th, th->doff * 4 + win_size, skb);
		buff = skb_clone(skb, GFP_ATOMIC);
		if (buff == NULL)
			return;

		skb_set_owner_w(buff, sk);
		tp->packets_out++;

		clear_delayed_acks(sk);

		if (!tcp_timer_is_set(sk, TIME_RETRANS))
			tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);

		skb->when = jiffies;
		update_send_head(sk);
		tp->snd_nxt = skb->end_seq;
	} else {
		buff = sock_wmalloc(sk, MAX_ACK_SIZE, 1, GFP_ATOMIC);
		if (buff == NULL) 
			return;

		buff->csum = 0;

		/* Put in the IP header and routing stuff. */
		tmp = tp->af_specific->build_net_header(sk, buff);
		if (tmp < 0) {
			kfree_skb(buff);
			return;
		}

		t1 = (struct tcphdr *) skb_put(buff, tp->tcp_header_len);
		memcpy(t1,(void *) &sk->dummy_th, sizeof(*t1));

		/*	Use a previous sequence.
		 *	This should cause the other end to send an ack.
		 */
	 
		t1->seq = htonl(tp->snd_nxt-1);
		t1->ack_seq = htonl(tp->rcv_nxt);
		t1->window = htons(tcp_select_window(sk));
		tcp_build_and_update_options((__u32 *)(t1 + 1), tp);

		tp->af_specific->send_check(sk, t1, tp->tcp_header_len, buff);
	}

	/* Send it. */
	tp->af_specific->queue_xmit(buff);
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

	tcp_write_wakeup(sk);
	tp->pending = TIME_PROBE0;
	tp->backoff++;
	tp->probes_out++;
	tcp_reset_xmit_timer (sk, TIME_PROBE0, 
			      min(tp->rto << tp->backoff, 120*HZ));
}
