/*
 *	TCP over IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: tcp_ipv6.c,v 1.35 1997/07/23 15:18:04 freitag Exp $
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
#include <linux/init.h>

#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/random.h>

#include <net/tcp.h>
#include <net/ndisc.h>
#include <net/ipv6.h>
#include <net/transp_v6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>

#include <asm/uaccess.h>

extern int sysctl_tcp_sack;
extern int sysctl_tcp_timestamps;
extern int sysctl_tcp_window_scaling;

static void	tcp_v6_send_reset(struct in6_addr *saddr,
				  struct in6_addr *daddr,
				  struct tcphdr *th, struct proto *prot,
				  struct ipv6_options *opt,
				  struct device *dev, int pri, int hop_limit);

static void	tcp_v6_send_check(struct sock *sk, struct tcphdr *th, int len, 
				  struct sk_buff *skb);

static int	tcp_v6_backlog_rcv(struct sock *sk, struct sk_buff *skb);
static int	tcp_v6_build_header(struct sock *sk, struct sk_buff *skb);
static void	tcp_v6_xmit(struct sk_buff *skb);

static struct tcp_func ipv6_mapped;
static struct tcp_func ipv6_specific;

/* I have no idea if this is a good hash for v6 or not. -DaveM */
static __inline__ int tcp_v6_hashfn(struct in6_addr *laddr, u16 lport,
				    struct in6_addr *faddr, u16 fport)
{
	int hashent = (lport ^ fport);

	hashent ^= (laddr->s6_addr32[0] ^ laddr->s6_addr32[1]);
	hashent ^= (faddr->s6_addr32[0] ^ faddr->s6_addr32[1]);
	hashent ^= (faddr->s6_addr32[2] ^ faddr->s6_addr32[3]);
	return (hashent & ((TCP_HTABLE_SIZE/2) - 1));
}

static __inline__ int tcp_v6_sk_hashfn(struct sock *sk)
{
	struct in6_addr *laddr = &sk->net_pinfo.af_inet6.rcv_saddr;
	struct in6_addr *faddr = &sk->net_pinfo.af_inet6.daddr;
	__u16 lport = sk->num;
	__u16 fport = sk->dummy_th.dest;
	return tcp_v6_hashfn(laddr, lport, faddr, fport);
}

/* Grrr, addr_type already calculated by caller, but I don't want
 * to add some silly "cookie" argument to this method just for that.
 */
static int tcp_v6_verify_bind(struct sock *sk, unsigned short snum)
{
	struct sock *sk2;
	int addr_type = ipv6_addr_type(&sk->net_pinfo.af_inet6.rcv_saddr);
	int retval = 0, sk_reuse = sk->reuse;

	SOCKHASH_LOCK();
	sk2 = tcp_bound_hash[tcp_sk_bhashfn(sk)];
	for(; sk2 != NULL; sk2 = sk2->bind_next) {
		if((sk2->num == snum) && (sk2 != sk)) {
			unsigned char state = sk2->state;
			int sk2_reuse = sk2->reuse;
			if(addr_type == IPV6_ADDR_ANY || (!sk2->rcv_saddr)) {
				if((!sk2_reuse)			||
				   (!sk_reuse)			||
				   (state == TCP_LISTEN)) {
					retval = 1;
					break;
				}
			} else if(!ipv6_addr_cmp(&sk->net_pinfo.af_inet6.rcv_saddr,
						 &sk2->net_pinfo.af_inet6.rcv_saddr)) {
				if((!sk_reuse)			||
				   (!sk2_reuse)			||
				   (state == TCP_LISTEN)) {
					retval = 1;
					break;
				}
			}
		}
	}
	SOCKHASH_UNLOCK();

	return retval;
}

static void tcp_v6_hash(struct sock *sk)
{
	unsigned char state;

	SOCKHASH_LOCK();
	state = sk->state;
	if(state != TCP_CLOSE) {
		struct sock **skp;

		if(state == TCP_LISTEN)
			skp = &tcp_listening_hash[tcp_sk_listen_hashfn(sk)];
		else
			skp = &tcp_established_hash[tcp_v6_sk_hashfn(sk)];
		if((sk->next = *skp) != NULL)
			(*skp)->pprev = &sk->next;
		*skp = sk;
		sk->pprev = skp;
		tcp_sk_bindify(sk);
	}
	SOCKHASH_UNLOCK();
}

static void tcp_v6_unhash(struct sock *sk)
{
	SOCKHASH_LOCK();
	if(sk->pprev) {
		if(sk->next)
			sk->next->pprev = sk->pprev;
		*sk->pprev = sk->next;
		sk->pprev = NULL;
		tcp_sk_unbindify(sk);
	}
	SOCKHASH_UNLOCK();
}

static void tcp_v6_rehash(struct sock *sk)
{
	unsigned char state;

	SOCKHASH_LOCK();
	state = sk->state;
	if(sk->pprev) {
		if(sk->next)
			sk->next->pprev = sk->pprev;
		*sk->pprev = sk->next;
		sk->pprev = NULL;
		tcp_sk_unbindify(sk);
	}
	if(state != TCP_CLOSE) {
		struct sock **skp;

		if(state == TCP_LISTEN) {
			skp = &tcp_listening_hash[tcp_sk_listen_hashfn(sk)];
		} else {
			int hash = tcp_v6_sk_hashfn(sk);
			if(state == TCP_TIME_WAIT)
				hash += (TCP_HTABLE_SIZE/2);
			skp = &tcp_established_hash[hash];
		}
		if((sk->next = *skp) != NULL)
			(*skp)->pprev = &sk->next;
		*skp = sk;
		sk->pprev = skp;
		tcp_sk_bindify(sk);
	}
	SOCKHASH_UNLOCK();
}

static struct sock *tcp_v6_lookup_listener(struct in6_addr *daddr, unsigned short hnum)
{
	struct sock *sk;
	struct sock *result = NULL;

	sk = tcp_listening_hash[tcp_lhashfn(hnum)];
	for(; sk; sk = sk->next) {
		if((sk->num == hnum) && (sk->family == AF_INET6)) {
			struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
			if(!ipv6_addr_any(&np->rcv_saddr)) {
				if(!ipv6_addr_cmp(&np->rcv_saddr, daddr))
					return sk; /* Best possible match. */
			} else if(!result)
				result = sk;
		}
	}
	return result;
}

/* Sockets in TCP_CLOSE state are _always_ taken out of the hash, so
 * we need not check it for TCP lookups anymore, thanks Alexey. -DaveM
 */
static inline struct sock *__tcp_v6_lookup(struct tcphdr *th,
					   struct in6_addr *saddr, u16 sport,
					   struct in6_addr *daddr, u16 dport)
{
	unsigned short hnum = ntohs(dport);
	struct sock *sk;
	int hash = tcp_v6_hashfn(daddr, hnum, saddr, sport);

	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.  It is assumed that this code only
	 * gets called from within NET_BH.
	 */
	for(sk = tcp_established_hash[hash]; sk; sk = sk->next)
		/* For IPV6 do the cheaper port and family tests first. */
		if(sk->num		== hnum			&& /* local port     */
		   sk->family		== AF_INET6		&& /* address family */
		   sk->dummy_th.dest	== sport		&& /* remote port    */
		   !ipv6_addr_cmp(&sk->net_pinfo.af_inet6.daddr, saddr)	&&
		   !ipv6_addr_cmp(&sk->net_pinfo.af_inet6.rcv_saddr, daddr))
			goto hit; /* You sunk my battleship! */

	/* Must check for a TIME_WAIT'er before going to listener hash. */
	for(sk = tcp_established_hash[hash+(TCP_HTABLE_SIZE/2)]; sk; sk = sk->next)
		if(sk->num		== hnum			&& /* local port     */
		   sk->family		== AF_INET6		&& /* address family */
		   sk->dummy_th.dest	== sport		&& /* remote port    */
		   !ipv6_addr_cmp(&sk->net_pinfo.af_inet6.daddr, saddr)	&&
		   !ipv6_addr_cmp(&sk->net_pinfo.af_inet6.rcv_saddr, daddr))
			goto hit;

	sk = tcp_v6_lookup_listener(daddr, hnum);
hit:
	return sk;
}

#define tcp_v6_lookup(sa, sp, da, dp) __tcp_v6_lookup((0),(sa),(sp),(da),(dp))

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

	if (skb->protocol == __constant_htons(ETH_P_IPV6)) {
		si = skb->nh.ipv6h->saddr.s6_addr32[3];
		di = skb->nh.ipv6h->daddr.s6_addr32[3];
	} else {
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
	struct inet6_ifaddr *ifa;
	struct in6_addr *saddr = NULL;
	struct flowi fl;
	struct dst_entry *dst;
	struct tcphdr *th;
	struct sk_buff *buff;
	struct sk_buff *skb1;
	int tmp;
	int addr_type;

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
		return -ENETUNREACH;

	/*
	 *	connect to self not allowed
	 */

	if (ipv6_addr_cmp(&usin->sin6_addr, &np->saddr) == 0 &&
	    usin->sin6_port == sk->dummy_th.source)
		return (-EINVAL);

	memcpy(&np->daddr, &usin->sin6_addr, sizeof(struct in6_addr));

	/*
	 *	TCP over IPv4
	 */

	if (addr_type == IPV6_ADDR_MAPPED) {
		struct sockaddr_in sin;
		int err;

		SOCK_DEBUG(sk, "connect: ipv4 mapped\n");

		sin.sin_family = AF_INET;
		sin.sin_port = usin->sin6_port;
		sin.sin_addr.s_addr = usin->sin6_addr.s6_addr32[3];

		sk->tp_pinfo.af_tcp.af_specific = &ipv6_mapped;
		sk->backlog_rcv = tcp_v4_do_rcv;

		err = tcp_v4_connect(sk, (struct sockaddr *)&sin, sizeof(sin));

		if (err) {
			sk->tp_pinfo.af_tcp.af_specific = &ipv6_specific;
			sk->backlog_rcv = tcp_v6_backlog_rcv;
		}
		
		return err;
	}

	if (!ipv6_addr_any(&np->rcv_saddr))
		saddr = &np->rcv_saddr;

	fl.proto = IPPROTO_TCP;
	fl.nl_u.ip6_u.daddr = &np->daddr;
	fl.nl_u.ip6_u.saddr = saddr;
	fl.dev = NULL;
	fl.uli_u.ports.dport = usin->sin6_port;
	fl.uli_u.ports.sport = sk->dummy_th.source;

	dst = ip6_route_output(sk, &fl);
	
	if (dst->error) {
		dst_release(dst);
		return dst->error;
	}
	
	ip6_dst_store(sk, dst);

	np->oif = dst->dev;
	
	if (saddr == NULL) {
		ifa = ipv6_get_saddr(dst, &np->daddr);
	
		if (ifa == NULL)
			return -ENETUNREACH;
		
		saddr = &ifa->addr;

		/* set the source address */
		ipv6_addr_copy(&np->rcv_saddr, saddr);
		ipv6_addr_copy(&np->saddr, saddr);
	}

	/* FIXME: Need to do tcp_v6_unique_address() here! -DaveM */

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
		return(-ENOMEM);

	lock_sock(sk);

	tcp_v6_build_header(sk, buff);

	/* build the tcp header */
	th = (struct tcphdr *) skb_put(buff,sizeof(struct tcphdr));
	buff->h.th = th;

	memcpy(th, (void *) &(sk->dummy_th), sizeof(*th));
	buff->seq = sk->write_seq++;
	th->seq = htonl(buff->seq);
	tp->snd_nxt = sk->write_seq;
	buff->end_seq = sk->write_seq;
	th->ack = 0;
	th->syn = 1;


	sk->mtu = dst->pmtu;
	sk->mss = sk->mtu - sizeof(struct ipv6hdr) - sizeof(struct tcphdr);

        if (sk->mss < 1) {
                printk(KERN_DEBUG "intial ipv6 sk->mss below 1\n");
                sk->mss = 1;    /* Sanity limit */
        }

	tp->window_clamp = 0;	/* FIXME: shouldn't ipv6 dst cache have this? */
	tcp_select_initial_window(sock_rspace(sk)/2,sk->mss,
		&tp->rcv_wnd,
		&tp->window_clamp,
		sysctl_tcp_window_scaling,
		&tp->rcv_wscale);
	th->window = htons(tp->rcv_wnd);

	/*
	 *	Put in the TCP options to say MTU.
	 */

        tmp = tcp_syn_build_options(buff, sk->mss, sysctl_tcp_sack,
                sysctl_tcp_timestamps,
                sysctl_tcp_window_scaling,tp->rcv_wscale);
        th->doff = sizeof(*th)/4 + (tmp>>2);
	buff->csum = 0;
	tcp_v6_send_check(sk, th, sizeof(struct tcphdr) + tmp, buff);

	tcp_set_state(sk, TCP_SYN_SENT);

	/* Socket identity change complete, no longer
	 * in TCP_CLOSE, so rehash.
	 */
	sk->prot->rehash(sk);

	/* FIXME: should use dcache->rtt if availiable */
	tp->rto = TCP_TIMEOUT_INIT;

	tcp_init_xmit_timers(sk);

	tp->retransmits = 0;

	skb_queue_tail(&sk->write_queue, buff);
	tp->packets_out++;
	buff->when = jiffies;
	skb1 = skb_clone(buff, GFP_KERNEL);
	skb_set_owner_w(skb1, sk);

	tcp_v6_xmit(skb1);

	/* Timer for repeating the SYN until an answer  */

	tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
	tcp_statistics.TcpActiveOpens++;
	tcp_statistics.TcpOutSegs++;

	release_sock(sk);

	return(0);
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
				msg->msg_flags);

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

	sk = tcp_v6_lookup(daddr, th->dest, saddr, th->source);

	if (sk == NULL)
		return;

	np = &sk->net_pinfo.af_inet6;

	if (type == ICMPV6_PKT_TOOBIG) {
		/* icmp should have updated the destination cache entry */

		dst_check(&np->dst, np->dst_cookie);

		if (np->dst == NULL) {
			struct flowi fl;
			struct dst_entry *dst;
			
			fl.proto = IPPROTO_TCP;
			fl.nl_u.ip6_u.daddr = &np->daddr;
			fl.nl_u.ip6_u.saddr = &np->saddr;
			fl.dev = np->oif;
			fl.uli_u.ports.dport = sk->dummy_th.dest;
			fl.uli_u.ports.sport = sk->dummy_th.source;

			dst = ip6_route_output(sk, &fl);

			ip6_dst_store(sk, dst);
		}

		if (np->dst->error)
			sk->err_soft = np->dst->error;
		else
			sk->mtu = np->dst->pmtu;

		return;
	}

	opening = (sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV);
	
	if (icmpv6_err_convert(type, code, &err) || opening) {
		sk->err = err;

		if (opening) {
			tcp_statistics.TcpAttemptFails++;
			tcp_set_state(sk,TCP_CLOSE);
			sk->error_report(sk);
		}
	} else {
		sk->err_soft = err;
	}
}


/* FIXME: this is substantially similar to the ipv4 code.
 * Can some kind of merge be done? -- erics
 */
static void tcp_v6_send_synack(struct sock *sk, struct open_request *req)
{
	struct sk_buff * skb;
	struct tcphdr *th;
	struct dst_entry *dst;
	struct flowi fl;
	int tmp;

	skb = sock_wmalloc(sk, MAX_SYN_SIZE, 1, GFP_ATOMIC);
	if (skb == NULL)
		return;

	fl.proto = IPPROTO_TCP;
	fl.nl_u.ip6_u.daddr = &req->af.v6_req.rmt_addr;
	fl.nl_u.ip6_u.saddr = &req->af.v6_req.loc_addr;
	fl.dev = req->af.v6_req.dev;
	fl.uli_u.ports.dport = req->rmt_port;
	fl.uli_u.ports.sport = sk->dummy_th.source;

	dst = ip6_route_output(sk, &fl);
	if (dst->error) {
		kfree_skb(skb, FREE_WRITE);
		dst_release(dst);
		return;
	}

	skb->dev = dst->dev;
	skb_reserve(skb, (skb->dev->hard_header_len + 15) & ~15);
	skb->nh.ipv6h = (struct ipv6hdr *) skb_put(skb,sizeof(struct ipv6hdr));

	skb->h.th = th = (struct tcphdr *) skb_put(skb, sizeof(struct tcphdr));

	/* Yuck, make this header setup more efficient... -DaveM */
	memset(th, 0, sizeof(struct tcphdr));
	th->syn = 1;
	th->ack = 1;
	th->source = sk->dummy_th.source;
	th->dest = req->rmt_port;
	skb->seq = req->snt_isn;
	skb->end_seq = skb->seq + 1;
	th->seq = ntohl(skb->seq);
	th->ack_seq = htonl(req->rcv_isn + 1);

	/* Don't offer more than they did.
	 * This way we don't have to memorize who said what.
	 * FIXME: the selection of initial mss here doesn't quite
	 * match what happens under IPV4. Figure out the right thing to do.
	 */
        req->mss = min(sk->mss, req->mss);

        if (req->mss < 1) {
                printk(KERN_DEBUG "initial req->mss below 1\n");
                req->mss = 1;
        }

	if (req->rcv_wnd == 0) {
		/* Set this up on the first call only */
		req->window_clamp = 0; /* FIXME: should be in dst cache */
		tcp_select_initial_window(sock_rspace(sk)/2,req->mss,
			&req->rcv_wnd,
			&req->window_clamp,
			req->wscale_ok,
			&req->rcv_wscale);
	}
	th->window = htons(req->rcv_wnd);

	tmp = tcp_syn_build_options(skb, req->mss, req->sack_ok, req->tstamp_ok,
		req->wscale_ok,req->rcv_wscale);
	skb->csum = 0;
	th->doff = (sizeof(*th) + tmp)>>2;
	th->check = tcp_v6_check(th, sizeof(*th) + tmp,
				 &req->af.v6_req.loc_addr, &req->af.v6_req.rmt_addr,
				 csum_partial((char *)th, sizeof(*th)+tmp, skb->csum));

	ip6_dst_store(sk, dst);
	ip6_xmit(sk, skb, &fl, req->af.v6_req.opt);
	dst_release(dst);

	tcp_statistics.TcpOutSegs++;
}

static void tcp_v6_or_free(struct open_request *req)
{
}

static struct or_calltable or_ipv6 = {
	tcp_v6_send_synack,
	tcp_v6_or_free
};

/* FIXME: this is substantially similar to the ipv4 code.
 * Can some kind of merge be done? -- erics
 */
static int tcp_v6_conn_request(struct sock *sk, struct sk_buff *skb, void *ptr,
							   __u32 isn)
{
	struct tcp_opt tp;
	struct open_request *req;
	
	/* If the socket is dead, don't accept the connection.	*/
	if (sk->dead) {
		SOCK_DEBUG(sk, "Reset on %p: Connect on dead socket.\n", sk);
		tcp_statistics.TcpAttemptFails++;
		return -ENOTCONN;
	}

	if (skb->protocol == __constant_htons(ETH_P_IP))
		return tcp_v4_conn_request(sk, skb, ptr, isn);

	if (isn == 0) 
		isn = tcp_v6_init_sequence(sk,skb);

	/*
	 *	There are no SYN attacks on IPv6, yet...
	 */
	if (sk->ack_backlog >= sk->max_ack_backlog) {
		printk(KERN_DEBUG "droping syn ack:%d max:%d\n",
		       sk->ack_backlog, sk->max_ack_backlog);
		tcp_statistics.TcpAttemptFails++;
		goto exit;
	}

	req = tcp_openreq_alloc();
	if (req == NULL) {
		tcp_statistics.TcpAttemptFails++;
		goto exit;		
	}

	sk->ack_backlog++;

	req->rcv_wnd = 0;		/* So that tcp_send_synack() knows! */

	req->rcv_isn = skb->seq;
	req->snt_isn = isn;
	tp.tstamp_ok = tp.sack_ok = tp.wscale_ok = tp.snd_wscale = 0;
	tp.in_mss = 536;
	tcp_parse_options(skb->h.th,&tp,0);
	if (tp.saw_tstamp)
                req->ts_recent = tp.rcv_tsval;
        req->mss = tp.in_mss;
        req->tstamp_ok = tp.tstamp_ok;
        req->sack_ok = tp.sack_ok;
        req->snd_wscale = tp.snd_wscale;
        req->wscale_ok = tp.wscale_ok;
	req->rmt_port = skb->h.th->source;
	ipv6_addr_copy(&req->af.v6_req.rmt_addr, &skb->nh.ipv6h->saddr);
	ipv6_addr_copy(&req->af.v6_req.loc_addr, &skb->nh.ipv6h->daddr);
	req->af.v6_req.opt = NULL;	/* FIXME: options */
	req->af.v6_req.dev = skb->dev;  /* So that link locals have meaning */

	req->class = &or_ipv6;
	req->retrans = 0;
	req->sk = NULL;

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
				    csum_partial((char *)th, th->doff<<2, 
						 skb->csum));
}

static struct sock * tcp_v6_syn_recv_sock(struct sock *sk, struct sk_buff *skb,
					  struct open_request *req,
					  struct dst_entry *dst)
{
	struct ipv6_pinfo *np;
	struct flowi fl;
	struct tcp_opt *newtp;
	struct sock *newsk;

	if (skb->protocol == __constant_htons(ETH_P_IP)) {
		/*
		 *	v6 mapped
		 */

		newsk = tcp_v4_syn_recv_sock(sk, skb, req, dst);

		if (newsk == NULL) 
			return NULL;
	
		np = &newsk->net_pinfo.af_inet6;

		ipv6_addr_set(&np->daddr, 0, 0, __constant_htonl(0x0000FFFF),
			      newsk->daddr);

		ipv6_addr_set(&np->saddr, 0, 0, __constant_htonl(0x0000FFFF),
			      newsk->saddr);

		ipv6_addr_copy(&np->rcv_saddr, &np->saddr);

		newsk->tp_pinfo.af_tcp.af_specific = &ipv6_mapped;
		newsk->backlog_rcv = tcp_v4_do_rcv;

		return newsk;
	}

	newsk = sk_alloc(GFP_ATOMIC);
	if (newsk == NULL) {
	        if (dst)
		    dst_release(dst);
		return NULL;
	}

	memcpy(newsk, sk, sizeof(*newsk));

	/* Or else we die! -DaveM */
	newsk->sklist_next = NULL;

	newsk->opt = NULL;
	newsk->dst_cache  = NULL;
	skb_queue_head_init(&newsk->write_queue);
	skb_queue_head_init(&newsk->receive_queue);
	skb_queue_head_init(&newsk->out_of_order_queue);
	skb_queue_head_init(&newsk->error_queue);

	/*
	 *	Unused
	 */

	newtp = &(newsk->tp_pinfo.af_tcp);
	np = &newsk->net_pinfo.af_inet6;

	newtp->send_head = NULL;
	newtp->retrans_head = NULL;

	newtp->pending = 0;

	skb_queue_head_init(&newsk->back_log);

	newsk->prot->init(newsk);

	newtp->snd_cwnd_cnt = 0;
#if 0 /* Don't mess up the initialization we did in the init routine! */
	newtp->snd_ssthresh = 0;
#endif
	newtp->backoff = 0;
	newsk->proc = 0;
	newsk->done = 0;
	newsk->pair = NULL;
	atomic_set(&newsk->wmem_alloc, 0);
	atomic_set(&newsk->rmem_alloc, 0);
	newsk->localroute = sk->localroute;

	newsk->max_unacked = MAX_WINDOW - TCP_WINDOW_DIFF;

	newsk->err = 0;
	newsk->shutdown = 0;
	newsk->ack_backlog = 0;

	newtp->fin_seq = req->rcv_isn;
	newsk->syn_seq = req->rcv_isn;
	newsk->state = TCP_SYN_RECV;
	newsk->timeout = 0;

	newsk->write_seq = req->snt_isn;

	newtp->snd_wnd = ntohs(skb->h.th->window);
	newtp->max_window = newtp->snd_wnd;
	newtp->snd_wl1 = req->rcv_isn;
	newtp->snd_wl2 = newsk->write_seq;
	newtp->snd_una = newsk->write_seq++;
	newtp->snd_nxt = newsk->write_seq;

	newsk->urg_data = 0;
	newtp->packets_out = 0;
	newtp->retransmits = 0;
	newsk->linger=0;
	newsk->destroy = 0;
	init_timer(&newsk->timer);
	newsk->timer.data = (unsigned long) newsk;
	newsk->timer.function = &net_timer;

	tcp_init_xmit_timers(newsk);

	newsk->dummy_th.source = sk->dummy_th.source;
	newsk->dummy_th.dest = req->rmt_port;
	newsk->sock_readers=0;

	newtp->rcv_nxt = req->rcv_isn + 1;
	newtp->rcv_wup = req->rcv_isn + 1;
	newsk->copied_seq = req->rcv_isn + 1;

	newsk->socket = NULL;

	ipv6_addr_copy(&np->daddr, &req->af.v6_req.rmt_addr);
	ipv6_addr_copy(&np->saddr, &req->af.v6_req.loc_addr);
	ipv6_addr_copy(&np->rcv_saddr, &req->af.v6_req.loc_addr);
	np->oif = req->af.v6_req.dev;

	if (dst == NULL) {
	    /*
	     *	options / mss / route cache
	     */
	    
	    fl.proto = IPPROTO_TCP;
	    fl.nl_u.ip6_u.daddr = &np->daddr;
	    fl.nl_u.ip6_u.saddr = &np->saddr;
	    fl.dev = np->oif;
	    fl.uli_u.ports.dport = newsk->dummy_th.dest;
	    fl.uli_u.ports.sport = newsk->dummy_th.source;
	    
	    dst = ip6_route_output(newsk, &fl);
	}

	ip6_dst_store(newsk, dst);

        newtp->sack_ok = req->sack_ok;
        newtp->tstamp_ok = req->tstamp_ok;
        newtp->snd_wscale = req->snd_wscale;
	newtp->wscale_ok = req->wscale_ok;
        newtp->ts_recent = req->ts_recent;
        if (newtp->tstamp_ok) {
                newtp->tcp_header_len = sizeof(struct tcphdr) + 12; /* FIXME: define the contant. */
                newsk->dummy_th.doff += 3;
        } else {
                newtp->tcp_header_len = sizeof(struct tcphdr);
        }
  
	if (dst->error)
		newsk->mtu = req->af.v6_req.dev->mtu;
	else
		newsk->mtu = dst->pmtu;

	newsk->mss = min(req->mss+sizeof(struct tcphdr)-newtp->tcp_header_len,
		(newsk->mtu - sizeof(struct ipv6hdr) - newtp->tcp_header_len));
	/* XXX tp->window_clamp??? -DaveM */

	newsk->daddr	= LOOPBACK4_IPV6;
	newsk->saddr	= LOOPBACK4_IPV6;
	newsk->rcv_saddr= LOOPBACK4_IPV6;

	newsk->prot->hash(newsk);
	add_to_prot_sklist(newsk);
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
	struct flowi fl;

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
  
	if(th->ack) {
	  	t1->seq = th->ack_seq;
	} else {
		t1->ack = 1;
	  	if(!th->syn)
			t1->ack_seq = th->seq;
		else
			t1->ack_seq = htonl(ntohl(th->seq)+1);
	}

	buff->csum = csum_partial((char *)t1, sizeof(*t1), 0);
	
	t1->check = csum_ipv6_magic(saddr, daddr, sizeof(*t1), IPPROTO_TCP,
				    buff->csum);

	fl.proto = IPPROTO_TCP;
	fl.nl_u.ip6_u.daddr = daddr;
	fl.nl_u.ip6_u.saddr = saddr;
	fl.dev = dev;
	fl.uli_u.ports.dport = th->dest;
	fl.uli_u.ports.sport = th->source;

	ip6_xmit(NULL, buff, &fl, NULL);
	tcp_statistics.TcpOutSegs++;
}

struct sock *tcp_v6_check_req(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct open_request *req = tp->syn_wait_queue;

	/*	assumption: the socket is not in use.
	 *	as we checked the user count on tcp_rcv and we're
	 *	running from a soft interrupt.
	 */
	if (!req)
		return sk;

	while(req) {
		if (!ipv6_addr_cmp(&req->af.v6_req.rmt_addr, &skb->nh.ipv6h->saddr) &&
		    !ipv6_addr_cmp(&req->af.v6_req.loc_addr, &skb->nh.ipv6h->daddr) &&
		    req->rmt_port == skb->h.th->source) {
			u32 flg;

			if (req->sk) {
				printk(KERN_DEBUG "BUG: syn_recv:"
				       "socket exists\n");
				break;
			}

			/* Check for syn retransmission */
			flg = *(((u32 *)skb->h.th) + 3);
			flg &= __constant_htonl(0x001f0000);

			if ((flg == __constant_htonl(0x00020000)) &&
			    (!after(skb->seq, req->rcv_isn))) {
				/*	retransmited syn
				 *	FIXME: must send an ack
				 */
				return NULL;
			}

			skb_orphan(skb);
			sk = tp->af_specific->syn_recv_sock(sk, skb, req, NULL);

			tcp_dec_slow_timer(TCP_SLT_SYNACK);

			if (sk == NULL)
				return NULL;

			skb_set_owner_r(skb, sk);
			req->expires = 0UL;
			req->sk = sk;
			break;
		}
		req = req->dl_next;
	}
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

	if (!redo) {
		if (skb->pkt_type != PACKET_HOST)
			goto discard_it;

		/*
		 *	Pull up the IP header.
		 */

		skb_pull(skb, skb->h.raw - skb->data);

		/*
		 *	Try to use the device checksum if provided.
		 */

		switch (skb->ip_summed) {
		case CHECKSUM_NONE:
			skb->csum = csum_partial((char *)th, len, 0);
		case CHECKSUM_HW:
			if (tcp_v6_check(th,len,saddr,daddr,skb->csum)) {
				printk(KERN_DEBUG "tcp csum failed\n");
				goto discard_it;
			}
		default:
			/* CHECKSUM_UNNECESSARY */
		};

		tcp_statistics.TcpInSegs++;

		sk = __tcp_v6_lookup(th, saddr, th->source, daddr, th->dest);

		if (!sk) {
			printk(KERN_DEBUG "socket not found\n");
			goto no_tcp_socket;
		}

		skb->sk = sk;
		skb->seq = ntohl(th->seq);
		skb->end_seq = skb->seq + th->syn + th->fin + len - th->doff*4;
		skb->ack_seq = ntohl(th->ack_seq);

		skb->used = 0;
	}

	/*
	 * We may need to add it to the backlog here.
	 */

	if (sk->sock_readers) {
		__skb_queue_tail(&sk->back_log, skb);
		return(0);
	}

	/*
	 *	Signal NDISC that the connection is making
	 *	"forward progress"
	 */
	if (sk->state != TCP_LISTEN) {
		struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
		struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);

		if (after(skb->seq, tp->rcv_nxt) ||
		    after(skb->ack_seq, tp->snd_una)) {
			if (np->dst)
				ndisc_validate(np->dst->neighbour);
		}
	}

	if (!sk->prot) {
		printk(KERN_DEBUG "tcp_rcv: sk->prot == NULL\n");
		return(0);
	}

	skb_set_owner_r(skb, sk);

	if (sk->state == TCP_ESTABLISHED) {
		if (tcp_rcv_established(sk, skb, th, len))
			goto no_tcp_socket;
		return 0;
	}

	if (sk->state == TCP_LISTEN) {
		/*
		 *	find possible connection requests
		 */
		sk = tcp_v6_check_req(sk, skb);

		if (sk == NULL)
			goto discard_it;
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

	if (np->dst)
		dst_check(&np->dst, np->dst_cookie);

	if (np->dst == NULL) {
		struct flowi fl;
		struct dst_entry *dst;

		fl.proto = IPPROTO_TCP;
		fl.nl_u.ip6_u.daddr = &np->daddr;
		fl.nl_u.ip6_u.saddr = &np->saddr;
		fl.dev = np->oif;
		fl.uli_u.ports.dport = sk->dummy_th.dest;
		fl.uli_u.ports.sport = sk->dummy_th.source;

		dst = ip6_route_output(sk, &fl);
		ip6_dst_store(sk, dst);
	}

	if (np->dst->error) {
		/*
		 *	lost route to destination
		 */
		return -EHOSTUNREACH;
	}

	skb_pull(skb, skb->nh.raw - skb->data);
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

	saddr = &skb->nh.ipv6h->saddr;
	daddr = &skb->nh.ipv6h->daddr;
	return tcp_v6_lookup(saddr, th->source, daddr, th->dest);
}

static int tcp_v6_build_header(struct sock *sk, struct sk_buff *skb)
{
	skb_reserve(skb, (MAX_HEADER + 15) & ~15);
	skb->nh.raw = skb_put(skb, sizeof(struct ipv6hdr));

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
	struct flowi fl;
	int err;

	fl.proto = IPPROTO_TCP;
	fl.nl_u.ip6_u.daddr = &np->daddr;
	fl.nl_u.ip6_u.saddr = &np->saddr;
	fl.dev = np->oif;
	fl.uli_u.ports.sport = sk->dummy_th.source;
	fl.uli_u.ports.dport = sk->dummy_th.dest;

	err = ip6_xmit(sk, skb, &fl, np->opt);

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
	
	/* FIXME: right thing? */
	tp->rcv_wnd = 0;
	tp->in_mss = 536;
	/* tp->rcv_wnd = 8192; */

	/* start with only sending one packet at a time. */
	tp->snd_cwnd = 1;
	tp->snd_ssthresh = 0x7fffffff;

	sk->priority = 1;
	sk->state = TCP_CLOSE;

	/* this is how many unacked bytes we will accept for this socket.  */
	sk->max_unacked = 2048; /* needs to be at most 2 full packets. */
	sk->max_ack_backlog = SOMAXCONN;

	sk->mtu = 576;
	sk->mss = 536;

	sk->dummy_th.doff = sizeof(sk->dummy_th)/4;

	/*
	 *	Speed up by setting some standard state for the dummy_th.
	 */
  	sk->dummy_th.ack=1;
  	sk->dummy_th.doff=sizeof(struct tcphdr)>>2;

	/* Init SYN queue. */
	tp->syn_wait_queue = NULL;
	tp->syn_wait_last = &tp->syn_wait_queue;

	sk->tp_pinfo.af_tcp.af_specific = &ipv6_specific;

	return 0;
}

static int tcp_v6_destroy_sock(struct sock *sk)
{
	struct ipv6_pinfo * np = &sk->net_pinfo.af_inet6;
	struct sk_buff *skb;

	tcp_clear_xmit_timers(sk);

	if (sk->keepopen)
		tcp_dec_slow_timer(TCP_SLT_KEEPALIVE);

	/*
	 *	Cleanup up the write buffer.
	 */

  	while((skb = skb_dequeue(&sk->write_queue)) != NULL)
		kfree_skb(skb, FREE_WRITE);

	/*
	 *  Cleans up our, hopefuly empty, out_of_order_queue
	 */

  	while((skb = skb_dequeue(&sk->out_of_order_queue)) != NULL)
		kfree_skb(skb, FREE_READ);

	/*
	 *	Release destination entry
	 */

	if (np->dst)
		dst_release(np->dst);

	return 0;
}

struct proto tcpv6_prot = {
	(struct sock *)&tcpv6_prot,	/* sklist_next */
	(struct sock *)&tcpv6_prot,	/* sklist_prev */
	tcp_close,			/* close */
	tcp_v6_connect,			/* connect */
	tcp_accept,			/* accept */
	NULL,				/* retransmit */
	tcp_write_wakeup,		/* write_wakeup */
	tcp_read_wakeup,		/* read_wakeup */
	tcp_poll,			/* poll */
	tcp_ioctl,			/* ioctl */
	tcp_v6_init_sock,		/* init */
	tcp_v6_destroy_sock,		/* destroy */
	tcp_shutdown,			/* shutdown */
	tcp_setsockopt,			/* setsockopt */
	tcp_getsockopt,			/* getsockopt */
	tcp_v6_sendmsg,			/* sendmsg */
	tcp_recvmsg,			/* recvmsg */
	NULL,				/* bind */
	tcp_v6_backlog_rcv,		/* backlog_rcv */
	tcp_v6_hash,			/* hash */
	tcp_v6_unhash,			/* unhash */
	tcp_v6_rehash,			/* rehash */
	tcp_good_socknum,		/* good_socknum */
	tcp_v6_verify_bind,		/* verify_bind */
	128,				/* max_header */
	0,				/* retransmits */
	"TCPv6",			/* name */
	0,				/* inuse */
	0				/* highestinuse */
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

__initfunc(void tcpv6_init(void))
{
	/* register inet6 protocol */
	inet6_add_protocol(&tcpv6_protocol);
}
