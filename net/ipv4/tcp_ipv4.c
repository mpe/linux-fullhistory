/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 *
 *		IPv4 specific functions
 *
 *
 *		code split from:
 *		linux/ipv4/tcp.c
 *		linux/ipv4/tcp_input.c
 *		linux/ipv4/tcp_output.c
 *
 *		See tcp.c for author information
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/random.h>

#include <net/icmp.h>
#include <net/tcp.h>
#include <net/ipv6.h>

#include <asm/segment.h>

static void tcp_v4_send_reset(struct sk_buff *skb);

void tcp_v4_send_check(struct sock *sk, struct tcphdr *th, int len, 
		       struct sk_buff *skb);

/*
 *	Cached last hit socket
 */
 
static volatile unsigned long 	th_cache_saddr, th_cache_daddr;
static volatile unsigned short  th_cache_dport, th_cache_sport;
static volatile struct sock	*th_cache_sk;

void tcp_cache_zap(void)
{
	th_cache_sk=NULL;
}

/*
 *	Find the socket, using the last hit cache if applicable.
 *	The cache is not quite right...
 */

static inline struct sock * get_tcp_sock(u32 saddr, u16 sport,
					 u32 daddr, u16 dport)
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

static __u32 tcp_v4_init_sequence(struct sock *sk, struct sk_buff *skb)
{
	return secure_tcp_sequence_number(sk->saddr, sk->daddr,
					  skb->h.th->dest,
					  skb->h.th->source);
}

/*
 *	From tcp.c
 */

/*
 * Check that a TCP address is unique, don't allow multiple
 * connects to/from the same address
 */

static int tcp_unique_address(u32 saddr, u16 snum, u32 daddr, u16 dnum)
{
	int retval = 1;
	struct sock * sk;

	/* Make sure we are allowed to connect here. */
	cli();
	for (sk = tcp_prot.sock_array[snum & (SOCK_ARRAY_SIZE -1)];
			sk != NULL; sk = sk->next)
	{
		/* hash collision? */
		if (sk->num != snum)
			continue;
		if (sk->saddr != saddr)
			continue;
		if (sk->daddr != daddr)
			continue;
		if (sk->dummy_th.dest != dnum)
			continue;
		retval = 0;
		break;
	}
	sti();
	return retval;
}

/*
 *	This will initiate an outgoing connection. 
 */
 
int tcp_v4_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sk_buff *buff;
	struct sk_buff *skb1;
	unsigned char *ptr;
	int tmp;
	struct tcphdr *t1;
	struct rtable *rt;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sockaddr_in *usin = (struct sockaddr_in *) uaddr;

	if (sk->state != TCP_CLOSE) 
		return(-EISCONN);

	/*
	 *	Don't allow a double connect.
	 */
	 	
	if (sk->daddr)
		return -EINVAL;
	
	if (addr_len < sizeof(struct sockaddr_in)) 
		return(-EINVAL);

	if (usin->sin_family != AF_INET) {
		static int complained;
		if (usin->sin_family)
			return(-EAFNOSUPPORT);
		if (!complained++)
			printk(KERN_DEBUG "%s forgot to set AF_INET in " __FUNCTION__ "\n", current->comm);
	}

	if (sk->dst_cache) {
		dst_release(sk->dst_cache);
		sk->dst_cache = NULL;
	}
		  
	tmp = ip_route_connect(&rt, usin->sin_addr.s_addr, sk->saddr,
			       RT_TOS(sk->ip_tos)|(sk->localroute || 0));
	if (tmp < 0)
		return tmp;

	if (rt->rt_flags&(RTF_MULTICAST|RTF_BROADCAST)) {
		ip_rt_put(rt);
		return -ENETUNREACH;
	}

	if (!tcp_unique_address(rt->rt_src, sk->num, rt->rt_dst,
				usin->sin_port))
		return -EADDRNOTAVAIL;
  
	lock_sock(sk);
	sk->dst_cache = &rt->u.dst;
	sk->daddr = rt->rt_dst;
	if (!sk->saddr)
		sk->saddr = rt->rt_src;
	sk->rcv_saddr = sk->saddr;

	if (sk->priority == SOPRI_NORMAL)
		sk->priority = rt->u.dst.priority;

	sk->dummy_th.dest = usin->sin_port;
	sk->write_seq = secure_tcp_sequence_number(sk->saddr, sk->daddr,
						   sk->dummy_th.source,
						   usin->sin_port);

	tp->snd_wnd = 0;
	tp->snd_wl1 = 0;
	tp->snd_wl2 = sk->write_seq;
	tp->snd_una = sk->write_seq;

	tp->rcv_nxt = 0;

	sk->err = 0;
	
	buff = sock_wmalloc(sk, MAX_SYN_SIZE, 0, GFP_KERNEL);
	if (buff == NULL) 
	{
		release_sock(sk);
		return(-ENOBUFS);
	}

	/*
	 *	Put in the IP header and routing stuff.
	 */
	
	tmp = ip_build_header(buff, sk);

	if (tmp < 0) 
	{
		kfree_skb(buff, FREE_WRITE);
		release_sock(sk);
		return(-ENETUNREACH);
	}

	t1 = (struct tcphdr *) skb_put(buff,sizeof(struct tcphdr));
	buff->h.th = t1;

	memcpy(t1,(void *)&(sk->dummy_th), sizeof(*t1));
	buff->seq = sk->write_seq++;
	t1->seq = htonl(buff->seq);
	tp->snd_nxt = sk->write_seq;
	buff->end_seq = sk->write_seq;
	t1->ack = 0;
	t1->window = htons(512);
	t1->syn = 1;
	t1->doff = 6;

	/* use 512 or whatever user asked for */

	sk->window_clamp=rt->u.dst.window;

	sk->mtu = rt->u.dst.pmtu;
	if ((sk->ip_pmtudisc == IP_PMTUDISC_DONT ||
	     (sk->ip_pmtudisc == IP_PMTUDISC_WANT &&
	      rt->rt_flags&RTF_NOPMTUDISC)) &&
	    rt->u.dst.pmtu > 576)
		sk->mtu = 576;

	if(sk->mtu < 64)
		sk->mtu = 64;	/* Sanity limit */

	if (sk->user_mss)
		sk->mss = sk->user_mss;
	else
		sk->mss = (sk->mtu - sizeof(struct iphdr) - 
			   sizeof(struct tcphdr));

	/*
	 *	Put in the TCP options to say MSS.
	 */

	ptr = skb_put(buff,4);
	ptr[0] = TCPOPT_MSS;
	ptr[1] = TCPOLEN_MSS;
	ptr[2] = (sk->mss) >> 8;
	ptr[3] = (sk->mss) & 0xff;
	buff->csum = csum_partial(ptr, 4, 0);
	tcp_v4_send_check(sk, t1, sizeof(struct tcphdr) + 4, buff);

	/*
	 *	This must go first otherwise a really quick response 
	 *	will get reset.
	 */

	tcp_cache_zap();
	tcp_set_state(sk,TCP_SYN_SENT);

	tp->rto = rt->u.dst.rtt;

	tcp_init_xmit_timers(sk);
	
	/* Now works the right way instead of a hacked initial setting */
	sk->retransmits = 0;

	skb_queue_tail(&sk->write_queue, buff);

	sk->packets_out++;
	buff->when = jiffies;

	skb1 = skb_clone(buff, GFP_KERNEL);
	ip_queue_xmit(skb1);  

	/* Timer for repeating the SYN until an answer  */
	tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
	tcp_statistics.TcpActiveOpens++;
	tcp_statistics.TcpOutSegs++;
  
	release_sock(sk);
	return(0);
}

static int tcp_v4_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	int retval = -EINVAL;

	/*
	 *	Do sanity checking for sendmsg/sendto/send
	 */

	if (msg->msg_flags & ~(MSG_OOB|MSG_DONTROUTE|MSG_DONTWAIT))
		goto out;
	if (msg->msg_name) {
		struct sockaddr_in *addr=(struct sockaddr_in *)msg->msg_name;

		if (msg->msg_namelen < sizeof(*addr))
			goto out;
		if (addr->sin_family && addr->sin_family != AF_INET)
			goto out;
		retval = -ENOTCONN;
		if(sk->state == TCP_CLOSE)
			goto out;
		retval = -EISCONN;
		if (addr->sin_port != sk->dummy_th.dest)
			goto out;
		if (addr->sin_addr.s_addr != sk->daddr)
			goto out;
	}

	lock_sock(sk);
	retval = tcp_do_sendmsg(sk, msg->msg_iovlen, msg->msg_iov,
				len, msg->msg_flags);

	release_sock(sk);

out:
	return retval;
}

/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.  After adjustment
 * header points to the first 8 bytes of the tcp header.  We need
 * to find the appropriate port.
 */

void tcp_v4_err(struct sk_buff *skb, unsigned char *dp)
{
	struct iphdr *iph = (struct iphdr*)dp;
	struct tcphdr *th = (struct tcphdr*)(dp+(iph->ihl<<2));
	int type = skb->h.icmph->type;
	int code = skb->h.icmph->code;
	struct tcp_opt *tp;
	struct sock *sk;

	sk = get_sock(&tcp_prot, th->source, iph->daddr, th->dest, iph->saddr);

	if (sk == NULL)
		return;

	if (type == ICMP_SOURCE_QUENCH)
	{
		/*
		 * FIXME:
		 * Follow BSD for now and just reduce cong_window to 1 again.
		 * It is possible that we just want to reduce the
		 * window by 1/2, or that we want to reduce ssthresh by 1/2
		 * here as well.
		 */

		tp = &sk->tp_pinfo.af_tcp;

		sk->cong_window = 1;
		tp->high_seq = tp->snd_nxt;
		
		return;
	}

	if (type == ICMP_PARAMETERPROB)
	{
		sk->err=EPROTO;
		sk->error_report(sk);
	}

	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED) {
		if (sk->ip_pmtudisc != IP_PMTUDISC_DONT) {
			int new_mtu = sk->dst_cache->pmtu - sizeof(struct iphdr) - sizeof(struct tcphdr);
			if (new_mtu < sk->mss && new_mtu > 0) {
				sk->mss = new_mtu;
			}
		}
		return;
	}

	/*
	 * If we've already connected we will keep trying
	 * until we time out, or the user gives up.
	 */

	if (code <= NR_ICMP_UNREACH)
	{
		if(icmp_err_convert[code].fatal || sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV)
		{
			sk->err = icmp_err_convert[code].errno;
			if (sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV)
			{
				tcp_statistics.TcpAttemptFails++;
				tcp_set_state(sk,TCP_CLOSE);
				sk->error_report(sk);		/* Wake people up to see the error (see connect in sock.c) */
			}
		}
		else	/* Only an error on timeout */
			sk->err_soft = icmp_err_convert[code].errno;
	}
}

/*
 *	This routine computes a TCP checksum.
 *
 *	Modified January 1995 from a go-faster DOS routine by
 *	Jorge Cwik <jorge@laser.satlink.net>
 */
void tcp_v4_send_check(struct sock *sk, struct tcphdr *th, int len, 
		       struct sk_buff *skb)
{
	__u32 saddr = sk->saddr;
	__u32 daddr = sk->daddr;
#ifdef DEBUG_TCP_CHECK
	u16 check;
#endif
	th->check = 0;
	th->check = tcp_v4_check(th, len, saddr, daddr,
				 csum_partial((char *)th, sizeof(*th), 
					      skb->csum));

#ifdef DEBUG_TCP_CHECK
	check = th->check;
	th->check = 0;
	th->check = tcp_v4_check(th, len, saddr, daddr,
		csum_partial((char *)th,len,0));
	if (check != th->check) {
		static int count = 0;
		if (++count < 10) {
			printk("Checksum %x (%x) from %p\n", th->check, check,
			       __builtin_return_address(0));
			printk("TCP=<off:%d a:%d s:%d f:%d> len=%d\n", th->doff*4, th->ack, th->syn, th->fin, len);
		}
	}
#endif
}

/*
 *	This routine will send an RST to the other tcp.
 *
 *	Someone asks: why I NEVER use socket parameters (TOS, TTL etc.)
 *		      for reset.
 *	Answer: if a packet caused RST, it is not for a socket
 *		existing in our system, if it is matched to a socket,
 *		it is just duplicate segment or bug in other side's TCP.
 *		So that we build reply only basing on parameters
 *		arrived with segment.
 *	Exception: precedence violation. We do not implement it in any case.
 */
 
static void tcp_v4_send_reset(struct sk_buff *skb)
{
	struct tcphdr  *th = skb->h.th;
	struct sk_buff *skb1;
	struct tcphdr  *th1;

	if (th->rst)
		return;
  
	skb1 = ip_reply(skb, sizeof(struct tcphdr));
	if (skb1 == NULL)
		return;
 
	skb1->h.th = th1 = (struct tcphdr *)skb_put(skb1, sizeof(struct tcphdr));
	memset(th1, 0, sizeof(*th1));

	/*
	 *	Swap the send and the receive. 
	 */
 
	th1->dest = th->source;
	th1->source = th->dest;
	th1->doff = sizeof(*th1)/4;
	th1->rst = 1;
   
	if (th->ack)
	  	th1->seq = th->ack_seq;
	else {
		th1->ack = 1;
	  	if (!th->syn)
			th1->ack_seq = th->seq;
		else
			th1->ack_seq = htonl(ntohl(th->seq)+1);
	}
 
	skb1->csum = csum_partial((u8 *) th1, sizeof(*th1), 0);
	th1->check = tcp_v4_check(th1, sizeof(*th1), skb1->nh.iph->saddr,
				  skb1->nh.iph->daddr, skb1->csum);
	ip_queue_xmit(skb1);
	tcp_statistics.TcpOutSegs++;
}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
/*
 *	Check whether a received TCP packet might be for one of our
 *	connections.
 */

int tcp_chkaddr(struct sk_buff *skb)
{
	struct iphdr *iph = skb->nh.iph;
	struct tcphdr *th = (struct tcphdr *)(skb->nh.raw + iph->ihl*4);
	struct sock *sk;

	sk = get_sock(&tcp_prot, th->dest, iph->saddr, th->source, iph->daddr);

	if (!sk)
		return 0;

	/* 0 means accept all LOCAL addresses here, not all the world... */

	if (sk->rcv_saddr == 0)
		return 0;

	return 1;
}
#endif

static void tcp_v4_send_synack(struct sock *sk, struct open_request *req)
{
	struct tcp_v4_open_req *af_req = (struct tcp_v4_open_req *) req;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct sk_buff * skb;
	struct tcphdr *th;
	unsigned char *ptr;
	int mss;
	int tmp;

	skb = sock_wmalloc(sk, MAX_SYN_SIZE, 1, GFP_ATOMIC);
	
	if (skb == NULL)
	{
		return;
	}

	tmp = ip_build_pkt(skb, sk, af_req->loc_addr, af_req->rmt_addr,
			   af_req->opt);

	if (tmp < 0)
	{
		kfree_skb(skb, FREE_WRITE);
		return;
	}

	mss = skb->dst->pmtu;

	mss -= sizeof(struct iphdr) + sizeof(struct tcphdr);
	
	if (sk->user_mss)
		mss = min(mss, sk->user_mss);
	
	th =(struct tcphdr *) skb_put(skb, sizeof(struct tcphdr));
	skb->h.th = th;
	memset(th, 0, sizeof(struct tcphdr));
	
	th->syn = 1;
	th->ack = 1;

	th->source = sk->dummy_th.source;
	th->dest = req->rmt_port;
	       
	skb->seq = req->snt_isn;
	skb->end_seq = skb->seq + 1;

	th->seq = ntohl(skb->seq);
	th->ack_seq = htonl(req->rcv_isn + 1);
	th->doff = sizeof(*th)/4 + 1;
	
	th->window = ntohs(tp->rcv_wnd);

	ptr = skb_put(skb, TCPOLEN_MSS);
	ptr[0] = TCPOPT_MSS;
	ptr[1] = TCPOLEN_MSS;
	ptr[2] = (mss >> 8) & 0xff;
	ptr[3] = mss & 0xff;
	skb->csum = csum_partial(ptr, TCPOLEN_MSS, 0);

	th->check = tcp_v4_check(th, sizeof(*th) + TCPOLEN_MSS,
				 af_req->loc_addr, 
				 af_req->rmt_addr,
				 csum_partial((char *)th, sizeof(*th), skb->csum));

	ip_queue_xmit(skb);
	tcp_statistics.TcpOutSegs++;
					      

}

static void tcp_v4_or_free(struct open_request *req)
{
	struct tcp_v4_open_req *af_req = (struct tcp_v4_open_req *) req;

	if (af_req->req.sk)
		return;
	
	if (af_req->opt)
		kfree_s(af_req->opt, sizeof(struct options) + af_req->opt->optlen);
}

static struct or_calltable or_ipv4 = {
	tcp_v4_send_synack,
	tcp_v4_or_free
};

static int tcp_v4_syn_filter(struct sock *sk, struct sk_buff *skb, __u32 saddr)
{
	return 0;
}

int tcp_v4_conn_request(struct sock *sk, struct sk_buff *skb, void *ptr, __u32 isn)
{
	struct ip_options *opt = (struct ip_options *) ptr;
	struct tcp_v4_open_req *af_req;
	struct open_request *req;
	struct tcphdr *th = skb->h.th;
	__u32 saddr = skb->nh.iph->saddr;
	__u32 daddr = skb->nh.iph->daddr;

	/* If the socket is dead, don't accept the connection.	*/
	if (sk->dead)
	{
		if(sk->debug)
		{
			printk("Reset on %p: Connect on dead socket.\n",sk);
		}
		tcp_statistics.TcpAttemptFails++;
		return -ENOTCONN;		
	}

	if (sk->ack_backlog >= sk->max_ack_backlog || 
	    tcp_v4_syn_filter(sk, skb, saddr))
	{
		printk(KERN_DEBUG "droping syn ack:%d max:%d\n",
		       sk->ack_backlog, sk->max_ack_backlog);
#ifdef CONFIG_IP_TCPSF
		tcp_v4_random_drop(sk);
#endif
		tcp_statistics.TcpAttemptFails++;
		goto exit;
	}


	af_req = kmalloc(sizeof(struct tcp_v4_open_req), GFP_ATOMIC);

	if (af_req == NULL)
	{
		tcp_statistics.TcpAttemptFails++;
		goto exit;
	}

	sk->ack_backlog++;
	req = (struct open_request *) af_req;

	memset(af_req, 0, sizeof(struct tcp_v4_open_req));

	req->rcv_isn = skb->seq;
	req->snt_isn = isn;

	/* mss */
	req->mss = tcp_parse_options(th);

	if (!req->mss)
	{
		req->mss = 536;
	}

	req->rmt_port = th->source;

	af_req->loc_addr = daddr;
	af_req->rmt_addr = saddr;
	
	/*
	 *	options
	 */

	if (opt && opt->optlen)
	{
		af_req->opt = (struct ip_options*) kmalloc(sizeof(struct ip_options) +
							   opt->optlen, GFP_ATOMIC);
		if (af_req->opt) 
		{
			if (ip_options_echo(af_req->opt, skb))
			{
				kfree_s(af_req->opt, sizeof(struct options) + 
					opt->optlen);
				af_req->opt = NULL;
			}
		}
	}

	req->class = &or_ipv4;

	tcp_v4_send_synack(sk, req);
	
	req->expires = jiffies + TCP_TIMEOUT_INIT;
	tcp_inc_slow_timer(TCP_SLT_SYNACK);
	tcp_synq_queue(&sk->tp_pinfo.af_tcp, req);

	sk->data_ready(sk, 0);
  exit:
	kfree_skb(skb, FREE_READ);
	return 0;
}

struct sock * tcp_v4_syn_recv_sock(struct sock *sk, struct sk_buff *skb,
				   struct open_request *req)
{
	struct tcp_v4_open_req *af_req = (struct tcp_v4_open_req *) req;
	struct tcp_opt *newtp;
	struct sock *newsk;
	struct rtable *rt;
	int snd_mss;

	newsk = sk_alloc(GFP_ATOMIC);
	if (newsk == NULL)
	{
		return NULL;
	}

	memcpy(newsk, sk, sizeof(*newsk));
	newsk->opt = NULL;
	newsk->dst_cache  = NULL;
	skb_queue_head_init(&newsk->write_queue);
	skb_queue_head_init(&newsk->receive_queue);
	skb_queue_head_init(&newsk->out_of_order_queue);
	skb_queue_head_init(&newsk->error_queue);
	
	/*
	 *	Unused
	 */

	newsk->send_head = NULL;
	newsk->send_tail = NULL;

	newtp = &(newsk->tp_pinfo.af_tcp);
	newtp->send_head = NULL;
	newtp->retrans_head = NULL;

	newtp->pending = 0;

	skb_queue_head_init(&newsk->back_log);

	newsk->prot->init(newsk);

	newsk->cong_count = 0;
	newsk->ssthresh = 0;
	newtp->backoff = 0;
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

	newsk->fin_seq = req->rcv_isn;
	newsk->syn_seq = req->rcv_isn;
	newsk->state = TCP_SYN_RECV;
	newsk->timeout = 0;
	newsk->ip_xmit_timeout = 0;

	newsk->write_seq = req->snt_isn;

	newtp->snd_wnd = ntohs(skb->h.th->window);
	newsk->max_window = newtp->snd_wnd;
	newtp->snd_wl1 = req->rcv_isn;
	newtp->snd_wl2 = newsk->write_seq;
	newtp->snd_una = newsk->write_seq++;
	newtp->snd_nxt = newsk->write_seq;

	newsk->urg_data = 0;
	newsk->packets_out = 0;
	newsk->retransmits = 0;
	newsk->linger=0;
	newsk->destroy = 0;
	init_timer(&newsk->timer);
	newsk->timer.data = (unsigned long) newsk;
	newsk->timer.function = &net_timer;

	tcp_init_xmit_timers(newsk);

	newsk->dummy_th.source = sk->dummy_th.source;
	newsk->dummy_th.dest = req->rmt_port;
	newsk->users=0;
	
	newtp->rcv_nxt = req->rcv_isn + 1;
	newtp->rcv_wup = req->rcv_isn + 1;
	newsk->copied_seq = req->rcv_isn + 1;

	newsk->socket = NULL;

	newsk->daddr = af_req->rmt_addr;
	newsk->saddr = af_req->loc_addr;
	newsk->rcv_saddr = af_req->loc_addr;
	
	/*
	 *	options / mss / route_cache
	 */
	newsk->opt = af_req->opt;

	if (ip_route_output(&rt,
			    newsk->opt && newsk->opt->srr ? newsk->opt->faddr : newsk->daddr,
			    newsk->saddr, newsk->ip_tos, NULL)) {
		kfree(newsk);
		return NULL;
	}

	newsk->dst_cache = &rt->u.dst;
	
	newsk->window_clamp = rt->u.dst.window;
	snd_mss = rt->u.dst.pmtu;
	
	newsk->mtu = snd_mss;
	/* sanity check */
	if (newsk->mtu < 64)
	{
		newsk->mtu = 64;
	}

	snd_mss -= sizeof(struct iphdr) + sizeof(struct tcphdr);

	if (sk->user_mss)
	{
		snd_mss = min(snd_mss, sk->user_mss);
	}
	
	newsk->mss = min(req->mss, snd_mss);
	
	inet_put_sock(newsk->num, newsk);

	tcp_cache_zap();

	return newsk;
}

struct sock *tcp_v4_check_req(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct open_request *req;


	/*
	 *	assumption: the socket is not in use.
	 *	as we checked the user count on tcp_rcv and we're
	 *	running from a soft interrupt.
	 */
		
	req = tp->syn_wait_queue;
	

	if (!req)
	{
		return sk;
	}
	
	do {
		struct tcp_v4_open_req *af_req;

		af_req = (struct tcp_v4_open_req *) req;

		if (af_req->rmt_addr == skb->nh.iph->saddr &&
		    af_req->loc_addr == skb->nh.iph->daddr &&
		    req->rmt_port == skb->h.th->source)
		{
			u32 flg;
				
			if (req->sk)
			{
				printk(KERN_DEBUG "BUG: syn_recv:"
				       "socket exists\n");
				break;
			}

			/* match */

			/*
			 *	Check for syn retransmission
			 */
			flg = *(((u32 *)skb->h.th) + 3);
			flg &= __constant_htonl(0x002f0000);
				
			if ((flg == __constant_htonl(0x00020000)) &&
			    (!after(skb->seq, req->rcv_isn)))
			{
				/*
				 *	retransmited syn
				 *	FIXME: must send an ack
				 */
				return NULL;
			}

			skb_orphan(skb);
			sk = tp->af_specific->syn_recv_sock(sk, skb, req);

			tcp_dec_slow_timer(TCP_SLT_SYNACK);

			if (sk == NULL)
			{
				return NULL;
			}

			skb_set_owner_r(skb, sk);
			req->expires = 0UL;
			req->sk = sk;
			break;
		}

		req = req->dl_next;
	} while (req != tp->syn_wait_queue);
	

	return sk;
}

static int __inline__ tcp_v4_do_rcv(struct sock *sk, struct sk_buff *skb)
{
	skb_set_owner_r(skb, sk);

	if (sk->state == TCP_ESTABLISHED)
	{
		if (tcp_rcv_established(sk, skb, skb->h.th, skb->len))
			goto reset;
		return 0;
	}

	if (sk->state == TCP_LISTEN)
	{
		/*
		 *	find possible connection requests
		 */
		sk = tcp_v4_check_req(sk, skb);

		if (sk == NULL)
		{
			goto discard_it;
		}
	}
	
	if (tcp_rcv_state_process(sk, skb, skb->h.th, NULL, skb->len) == 0)
		return 0;

reset:
	
	tcp_v4_send_reset(skb);

discard_it:
	/*
	 *	Discard frame
	 */
	kfree_skb(skb, FREE_READ);
	return 0;
}

int __inline__ tcp_v4_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	return tcp_v4_do_rcv(sk, skb);
}

/*
 *	From tcp_input.c
 */

int tcp_v4_rcv(struct sk_buff *skb, unsigned short len)
{
	struct tcphdr *th;	
	struct sock *sk;
	u32	saddr = skb->nh.iph->saddr;
	u32	daddr = skb->nh.iph->daddr;

	th = skb->h.th;

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
		if (tcp_v4_check(th,len,saddr,daddr,skb->csum)) {
			struct iphdr * iph = skb->nh.iph;
			printk("TCPv4 bad checksum from %08x:%04x to %08x:%04x, len=%d/%d/%d\n",
			       saddr, ntohs(th->source), daddr,
			       ntohs(th->dest), len, skb->len, ntohs(iph->tot_len));
					goto discard_it;
		}
	default:
		/* CHECKSUM_UNNECESSARY */
	}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
	if (IPCB(skb)->redirport)
		sk = get_sock_proxy(&tcp_prot, th->dest, saddr, th->source, daddr, skb->dev->pa_addr, IPCB(skb)->redirport);
	else
#endif
	sk = get_tcp_sock(saddr, th->source, daddr, th->dest);

	if (!sk)
		goto no_tcp_socket;

	skb->seq = ntohl(th->seq);
	skb->end_seq = skb->seq + th->syn + th->fin + len - th->doff*4;
	skb->ack_seq = ntohl(th->ack_seq);
		
	skb->acked = 0;
	skb->used = 0;

	if (!sk->users)
		return tcp_v4_do_rcv(sk, skb);

	__skb_queue_tail(&sk->back_log, skb);
	return 0;

no_tcp_socket:
	tcp_v4_send_reset(skb);

discard_it:
	/*
 	 *	Discard frame
	 */
	kfree_skb(skb, FREE_READ);
  	return 0;
}
  

int tcp_v4_build_header(struct sock *sk, struct sk_buff *skb)
{
	return ip_build_header(skb, sk);
}

int tcp_v4_rebuild_header(struct sock *sk, struct sk_buff *skb)
{	
	struct rtable *rt;
	struct iphdr *iph;
	struct tcphdr *th;
	int size;

	/* Check route */

	rt = (struct rtable*)skb->dst;
	if (rt->u.dst.obsolete) {
		int err;
		err = ip_route_output(&rt, rt->rt_dst, rt->rt_src, rt->key.tos, rt->key.dst_dev);
		if (err) {
			sk->err_soft=-err;
			sk->error_report(skb->sk);
			return -1;
		}
		dst_release(skb->dst);
		skb->dst = &rt->u.dst;
	}
  
	/*
	 *	Discard the surplus MAC header
	 */
	
	skb_pull(skb, skb->nh.raw-skb->data);

	iph = skb->nh.iph;
	th = skb->h.th;
	size = skb->tail - skb->h.raw;

	return 0;	
}

static struct sock * tcp_v4_get_sock(struct sk_buff *skb, struct tcphdr *th)
{
	struct sock *sk;
	sk = get_tcp_sock(skb->nh.iph->saddr, th->source, skb->nh.iph->daddr, th->dest);
	return sk;
}

static void v4_addr2sockaddr(struct sock *sk, struct sockaddr * uaddr)
{
	struct sockaddr_in *sin = (struct sockaddr_in *) uaddr;
	
	sin->sin_family		= AF_INET;
	sin->sin_addr.s_addr	= sk->daddr;
	sin->sin_port		= sk->dummy_th.dest;

}

struct tcp_func ipv4_specific = {
	tcp_v4_build_header,
	ip_queue_xmit,
	tcp_v4_send_check,
	tcp_v4_rebuild_header,
	tcp_v4_conn_request,
	tcp_v4_syn_recv_sock,
	tcp_v4_init_sequence,
	tcp_v4_get_sock,
	ip_setsockopt,
	ip_getsockopt,
	v4_addr2sockaddr,
	tcp_v4_send_reset,
	sizeof(struct sockaddr_in)
};

static int tcp_v4_init_sock(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	skb_queue_head_init(&sk->out_of_order_queue);
	tcp_init_xmit_timers(sk);

	tp->srtt  = 0;
	tp->rto  = TCP_TIMEOUT_INIT;		/*TCP_WRITE_TIME*/
	tp->mdev = TCP_TIMEOUT_INIT;

	tp->ato = 0;
	tp->iat = (HZ/5) << 3;

	tp->rcv_wnd = 8192;

	/*
	 * See draft-stevens-tcpca-spec-01 for discussion of the
	 * initialization of these values.
	 */
	sk->cong_window = 1;
	sk->ssthresh = 0x7fffffff;
	
	sk->priority = 1;
	sk->state = TCP_CLOSE;

	/* this is how many unacked bytes we will accept for this socket.  */
	sk->max_unacked = 2048; /* needs to be at most 2 full packets. */
	sk->max_ack_backlog = SOMAXCONN;
	
	sk->mtu = 576;
	sk->mss = 536;

	sk->dummy_th.doff = sizeof(sk->dummy_th)/4;
	

	/*
	 *	Speed up by setting some standard state for the dummy_th
	 *	if TCP uses it (maybe move to tcp_init later)
	 */
  	
  	sk->dummy_th.ack=1;	
  	sk->dummy_th.doff=sizeof(struct tcphdr)>>2;

	sk->tp_pinfo.af_tcp.af_specific = &ipv4_specific;

	return 0;
}

static int tcp_v4_destroy_sock(struct sock *sk)
{
	struct sk_buff *skb;

	tcp_clear_xmit_timers(sk);

	if (sk->keepopen)
	{
		tcp_dec_slow_timer(TCP_SLT_KEEPALIVE);
	}
	
	/*
	 *	Cleanup up the write buffer. 
	 */
	 
  	while((skb = skb_dequeue(&sk->write_queue)) != NULL) {
		IS_SKB(skb);
		kfree_skb(skb, FREE_WRITE);
  	}

	/*
	 *  Cleans up our, hopefuly empty, out_of_order_queue
	 */

  	while((skb = skb_dequeue(&sk->out_of_order_queue)) != NULL) {
		IS_SKB(skb);
		kfree_skb(skb, FREE_READ);
  	}

	return 0;
}

struct proto tcp_prot = {
	tcp_close,
	tcp_v4_connect,
	tcp_accept,
	NULL,
	tcp_write_wakeup,
	tcp_read_wakeup,
	tcp_select,
	tcp_ioctl,
	tcp_v4_init_sock,
	tcp_v4_destroy_sock,
	tcp_shutdown,
	tcp_setsockopt,
	tcp_getsockopt,
	tcp_v4_sendmsg,
	tcp_recvmsg,
	NULL,		/* No special bind()	*/
	tcp_v4_backlog_rcv,
	128,
	0,
	"TCP",
	0, 0,
	NULL
};

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce -pipe -m486 -DCPU=486 -c -o tcp_ipv4.o tcp_ipv4.c"
 * c-file-style: "Linux"
 * End:
 */
