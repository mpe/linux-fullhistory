/*
 *	TCP over IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: tcp_ipv6.c,v 1.7 1997/01/26 07:14:57 davem Exp $
 *
 *	Based on: 
 *	linux/net/ipv4/tcp.c
 *	linux/net/ipv4/tcp_input.c
 *	linux/net/ipv4/tcp_output.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdevice.h>

#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/random.h>

#include <net/tcp.h>
#include <net/ndisc.h>
#include <net/ipv6.h>
#include <net/transp_v6.h>
#include <net/addrconf.h>
#include <net/ipv6_route.h>

#include <asm/uaccess.h>

static void tcp_v6_send_reset(struct in6_addr *saddr, struct in6_addr *daddr, 
			      struct tcphdr *th, struct proto *prot, 
			      struct ipv6_options *opt,
			      struct device *dev, int pri, int hop_limit);

static void tcp_v6_send_check(struct sock *sk, struct tcphdr *th, int len, 
			      struct sk_buff *skb);

static int tcp_v6_backlog_rcv(struct sock *sk, struct sk_buff *skb);
static int tcp_v6_build_header(struct sock *sk, struct sk_buff *skb);

static struct tcp_func ipv6_mapped;
static struct tcp_func ipv6_specific;

static __inline__ u16 tcp_v6_check(struct tcphdr *th, int len,
				   struct in6_addr *saddr, 
				   struct in6_addr *daddr, 
				   unsigned long base)
{
	return csum_ipv6_magic(saddr, daddr, len, IPPROTO_TCP, base);
}

static __u32 tcp_v6_init_sequence(struct sock *sk, struct sk_buff *skb)
{
	__u32 si;
	__u32 di;

	if (skb->protocol == __constant_htons(ETH_P_IPV6))
	{
		si = skb->nh.ipv6h->saddr.s6_addr32[3];
		di = skb->nh.ipv6h->daddr.s6_addr32[3];
	}
	else
	{
		si = skb->nh.iph->saddr;
		di = skb->nh.iph->daddr;
	}

	return secure_tcp_sequence_number(di, si,
					  skb->h.th->dest,
					  skb->h.th->source);
}

static int tcp_v6_connect(struct sock *sk, struct sockaddr *uaddr, 
			  int addr_len)
{
	struct sockaddr_in6 *usin = (struct sockaddr_in6 *) uaddr;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct dest_entry *dc;
	struct inet6_ifaddr *ifa;
	struct tcphdr *th;
	__u8 *ptr;
	struct sk_buff *buff;
	struct sk_buff *skb1;
	int addr_type;
	int tmp;

	if (sk->state != TCP_CLOSE) 
		return(-EISCONN);

	/*
	 *	Don't allow a double connect.
	 */
	 	
	if(!ipv6_addr_any(&np->daddr))
		return -EINVAL;
	
	if (addr_len < sizeof(struct sockaddr_in6)) 
		return(-EINVAL);

	if (usin->sin6_family && usin->sin6_family != AF_INET6) 
		return(-EAFNOSUPPORT);

	/*
  	 *	connect() to INADDR_ANY means loopback (BSD'ism).
  	 */
  	
  	if(ipv6_addr_any(&usin->sin6_addr))
		usin->sin6_addr.s6_addr[15] = 0x1; 

	addr_type = ipv6_addr_type(&usin->sin6_addr);

	if(addr_type & IPV6_ADDR_MULTICAST)
	{
		return -ENETUNREACH;
	}

	/*
	 *	connect to self not allowed
	 */

	if (ipv6_addr_cmp(&usin->sin6_addr, &np->saddr) == 0 &&
	    usin->sin6_port == sk->dummy_th.source)
	{
		return (-EINVAL);
	}

	memcpy(&np->daddr, &usin->sin6_addr, sizeof(struct in6_addr));

	/*
	 *	TCP over IPv4
	 */

	if (addr_type == IPV6_ADDR_MAPPED)
	{
		struct sockaddr_in sin;
		int err;

		printk(KERN_DEBUG "connect: ipv4 mapped\n");

		sin.sin_family = AF_INET;
		sin.sin_port = usin->sin6_port;
		sin.sin_addr.s_addr = usin->sin6_addr.s6_addr32[3];

		sk->tp_pinfo.af_tcp.af_specific = &ipv6_mapped;
		sk->backlog_rcv = tcp_v4_backlog_rcv;

		err = tcp_v4_connect(sk, (struct sockaddr *)&sin, sizeof(sin));

		if (err)
		{
			sk->tp_pinfo.af_tcp.af_specific = &ipv6_specific;
			sk->backlog_rcv = tcp_v6_backlog_rcv;
		}
		
		return err;
	}

	dc = ipv6_dst_route(&np->daddr, NULL, (sk->localroute ? RTI_GATEWAY : 0));
	
	if (dc == NULL)
	{
		return -ENETUNREACH;
	}
	
	np->dest = dc;
	np->dc_sernum = (dc->rt.fib_node ? dc->rt.fib_node->fn_sernum : 0);

	ifa = ipv6_get_saddr((struct rt6_info *)dc, &np->daddr);
	
	if (ifa == NULL)
	{
		return -ENETUNREACH;
	}

	
	/*
	 *	Init variables
	 */

	lock_sock(sk);

	sk->dummy_th.dest = usin->sin6_port;	
	sk->write_seq = secure_tcp_sequence_number(np->saddr.s6_addr32[3],
						   np->daddr.s6_addr32[3],
						   sk->dummy_th.source,
						   sk->dummy_th.dest);

	tp->snd_wnd = 0;
	tp->snd_wl1 = 0;
	tp->snd_wl2 = sk->write_seq;
	tp->snd_una = sk->write_seq;

	tp->rcv_nxt = 0;

	sk->err = 0;

	release_sock(sk);

	buff = sock_wmalloc(sk, MAX_SYN_SIZE, 0, GFP_KERNEL);	

	if (buff == NULL) 
	{
		return(-ENOMEM);
	}
	lock_sock(sk);
	
	tmp = tcp_v6_build_header(sk, buff);

	/* set the source address */
                
	memcpy(&np->saddr, &ifa->addr, sizeof(struct in6_addr));
	memcpy(&np->rcv_saddr, &ifa->addr, sizeof(struct in6_addr));

	/* build the tcp header */
	th = (struct tcphdr *) skb_put(buff,sizeof(struct tcphdr));
	buff->h.th = th;

	memcpy(th, (void *) &(sk->dummy_th), sizeof(*th));
	buff->seq = sk->write_seq++;
	th->seq = htonl(buff->seq);
	tp->snd_nxt = sk->write_seq;
	buff->end_seq = sk->write_seq;
	th->ack = 0;
	th->window = 2;
	th->syn = 1;
	th->doff = 6;

	sk->window_clamp=0;

	if ((dc->dc_flags & DCF_PMTU))
		sk->mtu = dc->dc_pmtu;
	else
		sk->mtu = dc->rt.rt_dev->mtu;

	sk->mss = sk->mtu - sizeof(struct ipv6hdr) - sizeof(struct tcphdr);

	/*
	 *	Put in the TCP options to say MTU.
	 */

	ptr = skb_put(buff,4);
	ptr[0] = 2;
	ptr[1] = 4;
	ptr[2] = (sk->mss) >> 8;
	ptr[3] = (sk->mss) & 0xff;
	buff->csum = csum_partial(ptr, 4, 0);

	tcp_v6_send_check(sk, th, sizeof(struct tcphdr) + 4, buff);
	
	tcp_set_state(sk, TCP_SYN_SENT);
	
	/* FIXME: should use dcache->rtt if availiable */
	tp->rto = TCP_TIMEOUT_INIT;

	tcp_init_xmit_timers(sk);

	sk->retransmits = 0;

	skb_queue_tail(&sk->write_queue, buff);
	sk->packets_out++;
	buff->when = jiffies;
	skb1 = skb_clone(buff, GFP_KERNEL);
	skb_set_owner_w(skb1, sk);

	tmp = ipv6_xmit(sk, skb1, &np->saddr, &np->daddr, NULL, IPPROTO_TCP);

	/* Timer for repeating the SYN until an answer  */

	tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
	tcp_statistics.TcpActiveOpens++;
	tcp_statistics.TcpOutSegs++;
  
	release_sock(sk);
	
	return(tmp);
}

static int tcp_v6_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	int retval = -EINVAL;

	/*
	 *	Do sanity checking for sendmsg/sendto/send
	 */

	if (msg->msg_flags & ~(MSG_OOB|MSG_DONTROUTE|MSG_DONTWAIT))
		goto out;
	if (msg->msg_name) {
		struct sockaddr_in6 *addr=(struct sockaddr_in6 *)msg->msg_name;

		if (msg->msg_namelen < sizeof(*addr))
			goto out;

		if (addr->sin6_family && addr->sin6_family != AF_INET6)
			goto out;
		retval = -ENOTCONN;

		if(sk->state == TCP_CLOSE)
			goto out;
		retval = -EISCONN;
		if (addr->sin6_port != sk->dummy_th.dest)
			goto out;
		if (ipv6_addr_cmp(&addr->sin6_addr, &np->daddr))
			goto out;
	}

	lock_sock(sk);
	retval = tcp_do_sendmsg(sk, msg->msg_iovlen, msg->msg_iov, 
				len, msg->msg_flags);

	release_sock(sk);

out:
	return retval;
}

void tcp_v6_err(int type, int code, unsigned char *header, __u32 info,
		struct in6_addr *saddr, struct in6_addr *daddr,
		struct inet6_protocol *protocol)
{
	struct tcphdr *th = (struct tcphdr *)header;
	struct ipv6_pinfo *np;
	struct sock *sk;
	int err;
	int opening;

	sk = inet6_get_sock(&tcpv6_prot, daddr, saddr, th->source, th->dest);

	if (sk == NULL)
	{
		return;
	}

	np = &sk->net_pinfo.af_inet6;

	if (type == ICMPV6_PKT_TOOBIG)
	{
		/* icmp should have updated the destination cache entry */

		np->dest = ipv6_dst_check(np->dest, &np->daddr, np->dc_sernum,
					  0);

		np->dc_sernum = (np->dest->rt.fib_node ?
				 np->dest->rt.fib_node->fn_sernum : 0);

		if (np->dest->dc_flags & DCF_PMTU)
			sk->mtu = np->dest->dc_pmtu;

		sk->mtu = (sk->mtu - sizeof(struct ipv6hdr) - 
			   sizeof(struct tcphdr));

		return;
	}

	opening = (sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV);
	
	if (icmpv6_err_convert(type, code, &err) || opening)
	{
		sk->err = err;
		if (opening)
		{
			tcp_statistics.TcpAttemptFails++;
			tcp_set_state(sk,TCP_CLOSE);
			sk->error_report(sk);
		}
	}
	else
		sk->err_soft = err;
}


static void tcp_v6_send_synack(struct sock *sk, struct open_request *req)
{
	struct tcp_v6_open_req *af_req = (struct tcp_v6_open_req *) req;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct sk_buff * skb;
	struct tcphdr *th;
	unsigned char *ptr;
	struct dest_entry *dc;
	int mss;

	skb = sock_wmalloc(sk, MAX_SYN_SIZE, 1, GFP_ATOMIC);
	
	if (skb == NULL)
	{
		return;
	}

	skb_reserve(skb, (MAX_HEADER + 15) & ~15);
	skb->nh.ipv6h = (struct ipv6hdr *) skb_put(skb, sizeof(struct ipv6hdr));

	dc = ipv6_dst_route(&af_req->rmt_addr, af_req->dev, 0);

	skb->dev = af_req->dev;
	
	if (dc)
	{
		if (dc->dc_flags & DCF_PMTU)
			mss = dc->dc_pmtu;
		else
			mss = dc->dc_nexthop->dev->mtu;
		mss -= sizeof(struct ipv6hdr) + sizeof(struct tcphdr);

		ipv6_dst_unlock(dc);
	}
	else
		mss = 516;

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

	th->check = tcp_v6_check(th, sizeof(*th) + TCPOLEN_MSS, &af_req->loc_addr, 
				 &af_req->rmt_addr,
				 csum_partial((char *)th, sizeof(*th), skb->csum));

	ipv6_xmit(sk, skb, &af_req->loc_addr, &af_req->rmt_addr, af_req->opt,
		  IPPROTO_TCP);
				 
	tcp_statistics.TcpOutSegs++;
					      
}

static void tcp_v6_or_free(struct open_request *req)
{
}

static struct or_calltable or_ipv6 = {
	tcp_v6_send_synack,
	tcp_v6_or_free
};

static int tcp_v6_conn_request(struct sock *sk, struct sk_buff *skb, void *ptr,
			       __u32 isn)
{
	struct tcp_v6_open_req *af_req;
	struct open_request *req;
	
	/* If the socket is dead, don't accept the connection.	*/
	if (sk->dead)
	{
		SOCK_DEBUG(sk, "Reset on %p: Connect on dead socket.\n",sk);
		tcp_statistics.TcpAttemptFails++;
		return -ENOTCONN;		
	}

	if (skb->protocol == __constant_htons(ETH_P_IP))
	{
		return tcp_v4_conn_request(sk, skb, ptr, isn);
	}

	/*
	 *	There are no SYN attacks on IPv6, yet...
	 */
	if (sk->ack_backlog >= sk->max_ack_backlog)
	{
		printk(KERN_DEBUG "droping syn ack:%d max:%d\n",
		       sk->ack_backlog, sk->max_ack_backlog);
		tcp_statistics.TcpAttemptFails++;
		goto exit;
	}

	af_req = kmalloc(sizeof(struct tcp_v6_open_req), GFP_ATOMIC);
	
	if (af_req == NULL)
	{
		tcp_statistics.TcpAttemptFails++;
		goto exit;		
	}

	sk->ack_backlog++;
	req = (struct open_request *) af_req;

	memset(af_req, 0, sizeof(struct tcp_v6_open_req));

	req->rcv_isn = skb->seq;
	req->snt_isn = isn;

	/* mss */
	req->mss = tcp_parse_options(skb->h.th);

	if (!req->mss)
	{
		req->mss = 536;
	}

	req->rmt_port = skb->h.th->source;

	ipv6_addr_copy(&af_req->rmt_addr, &skb->nh.ipv6h->saddr);
	ipv6_addr_copy(&af_req->loc_addr, &skb->nh.ipv6h->daddr);

	/* FIXME: options */

	/* keep incoming device so that link locals have meaning */
	af_req->dev = skb->dev;

	req->class = &or_ipv6;

	tcp_v6_send_synack(sk, req);

	req->expires = jiffies + TCP_TIMEOUT_INIT;
	tcp_inc_slow_timer(TCP_SLT_SYNACK);
	tcp_synq_queue(&sk->tp_pinfo.af_tcp, req);	

	sk->data_ready(sk, 0);

  exit:
	kfree_skb(skb, FREE_READ);
	return 0;
}

static void tcp_v6_send_check(struct sock *sk, struct tcphdr *th, int len, 
			      struct sk_buff *skb)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	th->check = 0;
	
	th->check = csum_ipv6_magic(&np->saddr, &np->daddr, len, IPPROTO_TCP, 
				    csum_partial((char *)th, sizeof(*th), 
						 skb->csum));
}

static struct sock * tcp_v6_syn_recv_sock(struct sock *sk, struct sk_buff *skb,
					  struct open_request *req)
{
	struct tcp_v6_open_req *af_req = (struct tcp_v6_open_req *) req;
	struct ipv6_pinfo *np;
	struct dest_entry *dc;
	struct tcp_opt *newtp;
	struct sock *newsk;
	

	if (skb->protocol == __constant_htons(ETH_P_IP))
	{
		/* 
		 *	v6 mapped 
		 */
		
		newsk = tcp_v4_syn_recv_sock(sk, skb, req);

		if (newsk == NULL)
			return NULL;
		
		np = &newsk->net_pinfo.af_inet6;

		ipv6_addr_set(&np->daddr, 0, 0, __constant_htonl(0x0000FFFF),
			      newsk->daddr);

		ipv6_addr_set(&np->saddr, 0, 0, __constant_htonl(0x0000FFFF),
			      newsk->saddr);

		ipv6_addr_copy(&np->rcv_saddr, &np->saddr);

		newsk->tp_pinfo.af_tcp.af_specific = &ipv6_mapped;
		newsk->backlog_rcv = tcp_v4_backlog_rcv;

		return newsk;
	}

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
	np = &newsk->net_pinfo.af_inet6;

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

	ipv6_addr_copy(&np->daddr, &af_req->rmt_addr);
	ipv6_addr_copy(&np->saddr, &af_req->loc_addr);
	ipv6_addr_copy(&np->rcv_saddr, &af_req->loc_addr);
	
	/*
	 *	options / mss
	 */
	
	dc = ipv6_dst_route(&af_req->rmt_addr, af_req->dev, 0);
	np->dest = dc;

	if (np->dest && (np->dest->dc_flags & DCF_PMTU))
		newsk->mtu = np->dest->dc_pmtu;
	else
		newsk->mtu = af_req->dev->mtu;

	newsk->mss = min(req->mss, (newsk->mtu - sizeof(struct ipv6hdr) - 
				    sizeof(struct tcphdr)));
	
	newsk->daddr	= LOOPBACK4_IPV6;
	newsk->saddr	= LOOPBACK4_IPV6;
	newsk->rcv_saddr= LOOPBACK4_IPV6;
	
	inet_put_sock(newsk->num, newsk);

	return newsk;

}

static void tcp_v6_reply_reset(struct sk_buff *skb)
{
}

static void tcp_v6_send_reset(struct in6_addr *saddr, struct in6_addr *daddr, 
			      struct tcphdr *th, struct proto *prot, 
			      struct ipv6_options *opt,
			      struct device *dev, int pri, int hop_limit)
{
	struct sk_buff *buff;
	struct tcphdr *t1;

	if(th->rst)
		return;

	/*
	 * We need to grab some memory, and put together an RST,
	 * and then put it into the queue to be sent.
	 */

	buff = alloc_skb(MAX_RESET_SIZE, GFP_ATOMIC);
	if (buff == NULL) 
	  	return;

	buff->dev = dev;

	tcp_v6_build_header(NULL, buff);

	t1 = (struct tcphdr *) skb_put(buff,sizeof(struct tcphdr));
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

	buff->csum = csum_partial((char *)t1, sizeof(*t1), 0);
	
	t1->check = csum_ipv6_magic(saddr, daddr, sizeof(*t1), IPPROTO_TCP,
				    buff->csum);

	
	ipv6_xmit(NULL, buff, saddr, daddr, NULL, IPPROTO_TCP);
	
	tcp_statistics.TcpOutSegs++;
}

struct sock *tcp_v6_check_req(struct sock *sk, struct sk_buff *skb)
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
		struct tcp_v6_open_req *af_req;

		af_req = (struct tcp_v6_open_req *) req;

		if (!ipv6_addr_cmp(&af_req->rmt_addr, &skb->nh.ipv6h->saddr) &&
		    !ipv6_addr_cmp(&af_req->loc_addr, &skb->nh.ipv6h->daddr) &&
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

int tcp_v6_rcv(struct sk_buff *skb, struct device *dev,
	       struct in6_addr *saddr, struct in6_addr *daddr,
	       struct ipv6_options *opt, unsigned short len,
	       int redo, struct inet6_protocol *protocol)
{
	struct tcphdr *th;	
	struct sock *sk;

	/*
	 * "redo" is 1 if we have already seen this skb but couldn't
	 * use it at that time (the socket was locked).  In that case
	 * we have already done a lot of the work (looked up the socket
	 * etc).
	 */

	th = skb->h.th;

	sk = skb->sk;

	if (!redo)
	{

		if (skb->pkt_type != PACKET_HOST)
			goto discard_it;

		/*
		 *	Pull up the IP header.
		 */
	
		skb_pull(skb, skb->h.raw - skb->data);

		/*
		 *	Try to use the device checksum if provided.
		 */
		
		switch (skb->ip_summed) 
		{
			case CHECKSUM_NONE:
				skb->csum = csum_partial((char *)th, len, 0);
			case CHECKSUM_HW:
				if (tcp_v6_check(th,len,saddr,daddr,skb->csum))
				{
					printk(KERN_DEBUG "tcp csum failed\n");
					goto discard_it;
				}
			default:
				/* CHECKSUM_UNNECESSARY */
		}

		tcp_statistics.TcpInSegs++;
		
		sk = inet6_get_sock(&tcpv6_prot, daddr, saddr, 
				    th->dest, th->source);

		if (!sk) 
		{
			printk(KERN_DEBUG "socket not found\n");
			goto no_tcp_socket;
		}

		skb->sk = sk;
		skb->seq = ntohl(th->seq);
		skb->end_seq = skb->seq + th->syn + th->fin + len - th->doff*4;
		skb->ack_seq = ntohl(th->ack_seq);

		skb->acked = 0;
		skb->used = 0;
	}		

	/*
	 * We may need to add it to the backlog here. 
	 */

	if (sk->users) 
	{
		__skb_queue_tail(&sk->back_log, skb);
		return(0);
	}

	/*
	 *	Signal NDISC that the connection is making
	 *	"forward progress"
	 */
	if (sk->state != TCP_LISTEN)
	{
		struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
		struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);

		if (after(skb->seq, tp->rcv_nxt) ||
		    after(skb->ack_seq, tp->snd_una))
		{
			if (np->dest)
				ndisc_validate(np->dest->dc_nexthop);
		}
	}

	if (!sk->prot) 
	{
		printk(KERN_DEBUG "tcp_rcv: sk->prot == NULL\n");
		return(0);
	}

	skb_set_owner_r(skb, sk);

	if (sk->state == TCP_ESTABLISHED)
	{
		if (tcp_rcv_established(sk, skb, th, len))
			goto no_tcp_socket;
		return 0;
	}
	
	if (sk->state == TCP_LISTEN)
	{
		/*
		 *	find possible connection requests
		 */
		sk = tcp_v6_check_req(sk, skb);

		if (sk == NULL)
		{
			goto discard_it;
		}

	}

	if (tcp_rcv_state_process(sk, skb, th, opt, len) == 0)
		return 0;

no_tcp_socket:

	/*
	 *	No such TCB. If th->rst is 0 send a reset 
	 *	(checked in tcp_send_reset)
	 */

	tcp_v6_send_reset(daddr, saddr, th, &tcpv6_prot, opt, dev, 
			  skb->nh.ipv6h->priority, 255);

discard_it:

	/*
	 *	Discard frame
	 */

	kfree_skb(skb, FREE_READ);
	return 0;

}

static int tcp_v6_rebuild_header(struct sock *sk, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	
	if (np->dest)
	{
		np->dest = ipv6_dst_check(np->dest, &np->daddr,
					  np->dc_sernum, 0);
					  
	}
	else
	{
		np->dest = ipv6_dst_route(&np->daddr, NULL, 0);
	}

	if (!np->dest)
	{
		/*
		 *	lost route to destination
		 */
		return -1;
	}
	
	np->dc_sernum = (np->dest->rt.fib_node ?
			 np->dest->rt.fib_node->fn_sernum : 0);

	ipv6_redo_mac_hdr(skb, np->dest->dc_nexthop,
			  skb->tail - (u8*) skb->nh.ipv6h);
	return 0;
}

static int tcp_v6_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	int res;

	res = tcp_v6_rcv(skb, skb->dev,
			 &skb->nh.ipv6h->saddr, &skb->nh.ipv6h->daddr,
			 (struct ipv6_options *) skb->cb,
			 skb->len, 1, 
			 (struct inet6_protocol *) sk->pair);
	return res;
}

static struct sock * tcp_v6_get_sock(struct sk_buff *skb, struct tcphdr *th)
{
	struct in6_addr *saddr;
	struct in6_addr *daddr;
	struct sock *sk;

	saddr = &skb->nh.ipv6h->saddr;
	daddr = &skb->nh.ipv6h->daddr;

	sk = inet6_get_sock(&tcpv6_prot, daddr, saddr, th->source, th->dest);

	return sk;
}
	
static int tcp_v6_build_header(struct sock *sk, struct sk_buff *skb)
{
	skb_reserve(skb, (MAX_HEADER + 15) & ~15);
	skb->nh.ipv6h = (struct ipv6hdr *) skb_put(skb, sizeof(struct ipv6hdr));

	/*
	 *	FIXME: reserve space for option headers
	 *	length member of np->opt
	 */

	return 0;
}

static void tcp_v6_xmit(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct ipv6_pinfo * np = &sk->net_pinfo.af_inet6;
	int err;

	err = ipv6_xmit(sk, skb, &np->saddr, &np->daddr, NULL, IPPROTO_TCP);
	
	/*
	 *	FIXME: check error handling.
	 */

	sk->err_soft = err;
}
		     


static void v6_addr2sockaddr(struct sock *sk, struct sockaddr * uaddr)
{
	struct ipv6_pinfo * np = &sk->net_pinfo.af_inet6;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) uaddr;
	
	sin6->sin6_family = AF_INET6;
	memcpy(&sin6->sin6_addr, &np->daddr, sizeof(struct in6_addr));
	sin6->sin6_port	= sk->dummy_th.dest;

}

static struct tcp_func ipv6_specific = {
	tcp_v6_build_header,
	tcp_v6_xmit,
	tcp_v6_send_check,
	tcp_v6_rebuild_header,
	tcp_v6_conn_request,
	tcp_v6_syn_recv_sock,
	tcp_v6_init_sequence,
	tcp_v6_get_sock,
	ipv6_setsockopt,
	ipv6_getsockopt,
	v6_addr2sockaddr,
	tcp_v6_reply_reset,
	sizeof(struct sockaddr_in6)
};

/*
 *	TCP over IPv4 via INET6 API
 */

static struct tcp_func ipv6_mapped = {
	tcp_v4_build_header,
	ip_queue_xmit,
	tcp_v4_send_check,
	tcp_v4_rebuild_header,
	tcp_v6_conn_request,
	tcp_v6_syn_recv_sock,
	tcp_v6_init_sequence,
	tcp_v6_get_sock,
	ipv6_setsockopt,
	ipv6_getsockopt,
	v6_addr2sockaddr,
	tcp_v6_reply_reset,
	sizeof(struct sockaddr_in6)
};

static int tcp_v6_init_sock(struct sock *sk)
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

	/* start with only sending one packet at a time. */
	sk->cong_window = 1;
	sk->ssthresh = 0x7fffffff;

	sk->priority = 1;
	sk->state = TCP_CLOSE;

	/* this is how many unacked bytes we will accept for this socket.  */
	sk->max_unacked = 2048; /* needs to be at most 2 full packets. */
	sk->max_ack_backlog = SOMAXCONN;
	
	sk->mtu = 576;
	sk->mss = 516;

	sk->dummy_th.doff = sizeof(sk->dummy_th)/4;
	

	/*
	 *	Speed up by setting some standard state for the dummy_th
	 *	if TCP uses it (maybe move to tcp_init later)
	 */
  	
  	sk->dummy_th.ack=1;	
  	sk->dummy_th.doff=sizeof(struct tcphdr)>>2;

	sk->tp_pinfo.af_tcp.af_specific = &ipv6_specific;

	return 0;
}

static int tcp_v6_destroy_sock(struct sock *sk)
{
	struct ipv6_pinfo * np = &sk->net_pinfo.af_inet6;
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

	/*
	 *	Release destination entry
	 */

	if (np->dest)
	{
		ipv6_dst_unlock(np->dest);
	}

	return 0;
}


struct proto tcpv6_prot = {
	tcp_close,
	tcp_v6_connect,
	tcp_accept,
	NULL,
	tcp_write_wakeup,
	tcp_read_wakeup,
	tcp_poll,
	tcp_ioctl,
	tcp_v6_init_sock,
	tcp_v6_destroy_sock,
	tcp_shutdown,
	tcp_setsockopt,
	tcp_getsockopt,
	tcp_v6_sendmsg,
	tcp_recvmsg,
	NULL,			/* No special bind()	*/
	tcp_v6_backlog_rcv,
	128,
	0,
	"TCPv6",
	0, 0,
	NULL
};

static struct inet6_protocol tcpv6_protocol = 
{
	tcp_v6_rcv,		/* TCP handler		*/
	tcp_v6_err,		/* TCP error control	*/
	NULL,			/* next			*/
	IPPROTO_TCP,		/* protocol ID		*/
	0,			/* copy			*/
	NULL,			/* data			*/
	"TCPv6"			/* name			*/
};


void tcpv6_init(void)
{
	/* register inet6 protocol */
	inet6_add_protocol(&tcpv6_protocol);
}

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce -pipe -m486 -DCPU=486 -DMODULE -DMODVERSIONS -include /usr/src/linux/include/linux/modversions.h  -c -o tcp_ipv6.o tcp_ipv6.c"
 * c-file-style: "Linux"
 * End:
 */
