/*
 *	TCP over IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: tcp_ipv6.c,v 1.78 1998/04/16 16:29:22 freitag Exp $
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

extern int sysctl_max_syn_backlog;

static void	tcp_v6_send_reset(struct sk_buff *skb);
static void	tcp_v6_send_check(struct sock *sk, struct tcphdr *th, int len, 
				  struct sk_buff *skb);

static int	tcp_v6_do_rcv(struct sock *sk, struct sk_buff *skb);
static void	tcp_v6_xmit(struct sk_buff *skb);
static struct open_request *tcp_v6_search_req(struct tcp_opt *tp,
					      struct ipv6hdr *ip6h,
					      struct tcphdr *th,
					      struct open_request **prevp);

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
	__u16 fport = sk->dport;
	return tcp_v6_hashfn(laddr, lport, faddr, fport);
}

/* Grrr, addr_type already calculated by caller, but I don't want
 * to add some silly "cookie" argument to this method just for that.
 * But it doesn't matter, the recalculation is in the rarest path
 * this function ever takes.
 */
static int tcp_v6_verify_bind(struct sock *sk, unsigned short snum)
{
	struct tcp_bind_bucket *tb;
	int result = 0;

	SOCKHASH_LOCK();
	for(tb = tcp_bound_hash[tcp_bhashfn(snum)];
	    (tb && (tb->port != snum));
	    tb = tb->next)
		;
	if(tb && tb->owners) {
		/* Fast path for reuse ports, see include/net/tcp.h for a very
		 * detailed description of why this works, and why it is worth
		 * the effort at all. -DaveM
		 */
		if((tb->flags & TCPB_FLAG_FASTREUSE)	&&
		   (sk->reuse != 0)) {
			goto go_like_smoke;
		} else {
			struct sock *sk2;
			int sk_reuse = sk->reuse;
			int addr_type = ipv6_addr_type(&sk->net_pinfo.af_inet6.rcv_saddr);

			/* We must walk the whole port owner list in this case. -DaveM */
			for(sk2 = tb->owners; sk2; sk2 = sk2->bind_next) {
				if(sk->bound_dev_if == sk2->bound_dev_if) {
					if(!sk_reuse || !sk2->reuse || sk2->state == TCP_LISTEN) {
						if(addr_type == IPV6_ADDR_ANY	||
						   !sk2->rcv_saddr		||
						   !ipv6_addr_cmp(&sk->net_pinfo.af_inet6.rcv_saddr,
								  &sk2->net_pinfo.af_inet6.rcv_saddr))
							break;
					}
				}
			}
			if(sk2 != NULL)
				result = 1;
		}
	}
	if((result == 0) &&
	   (tb == NULL) &&
	   (tcp_bucket_create(snum) == NULL))
		result = 1;
go_like_smoke:
	SOCKHASH_UNLOCK();
	return result;
}

static void tcp_v6_hash(struct sock *sk)
{
	if(sk->state != TCP_CLOSE) {
		struct sock **skp;

		SOCKHASH_LOCK();
		skp = &tcp_established_hash[(sk->hashent = tcp_v6_sk_hashfn(sk))];
		if((sk->next = *skp) != NULL)
			(*skp)->pprev = &sk->next;
		*skp = sk;
		sk->pprev = skp;
		tcp_sk_bindify(sk);
		SOCKHASH_UNLOCK();
	}
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
		tcp_reg_zap(sk);
	}
	SOCKHASH_UNLOCK();
}

static void tcp_v6_rehash(struct sock *sk)
{
	unsigned char state;

	SOCKHASH_LOCK();
	state = sk->state;
	if(sk->pprev != NULL) {
		if(sk->next)
			sk->next->pprev = sk->pprev;
		*sk->pprev = sk->next;
		sk->pprev = NULL;
		tcp_reg_zap(sk);
	}
	if(state != TCP_CLOSE) {
		struct sock **skp;

		if(state == TCP_LISTEN)
			skp = &tcp_listening_hash[tcp_sk_listen_hashfn(sk)];
		else
			skp = &tcp_established_hash[(sk->hashent = tcp_v6_sk_hashfn(sk))];

		if((sk->next = *skp) != NULL)
			(*skp)->pprev = &sk->next;
		*skp = sk;
		sk->pprev = skp;
		if(state == TCP_LISTEN)
			tcp_sk_bindify(sk);
	}
	SOCKHASH_UNLOCK();
}

static struct sock *tcp_v6_lookup_listener(struct in6_addr *daddr, unsigned short hnum, int dif)
{
	struct sock *sk;
	struct sock *result = NULL;
	int score, hiscore;

	hiscore=0;
	sk = tcp_listening_hash[tcp_lhashfn(hnum)];
	for(; sk; sk = sk->next) {
		if((sk->num == hnum) && (sk->family == AF_INET6)) {
			struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
			
			score = 1;
			if(!ipv6_addr_any(&np->rcv_saddr)) {
				if(ipv6_addr_cmp(&np->rcv_saddr, daddr))
					continue;
				score++;
			}
			if (sk->bound_dev_if) {
				if (sk->bound_dev_if != dif)
					continue;
				score++;
			}
			if (score == 3)
				return sk;
			if (score > hiscore) {
				hiscore = score;
				result = sk;
			}
		}
	}
	return result;
}

/* Sockets in TCP_CLOSE state are _always_ taken out of the hash, so
 * we need not check it for TCP lookups anymore, thanks Alexey. -DaveM
 * It is assumed that this code only gets called from within NET_BH.
 */
static inline struct sock *__tcp_v6_lookup(struct tcphdr *th,
					   struct in6_addr *saddr, u16 sport,
					   struct in6_addr *daddr, u16 dport,
					   int dif)
{
	struct sock *sk;
	__u16 hnum = ntohs(dport);
	__u32 ports = TCP_COMBINED_PORTS(sport, hnum);
	int hash;

	/* Check TCP register quick cache first. */
	sk = TCP_RHASH(sport);
	if(sk && TCP_IPV6_MATCH(sk, saddr, daddr, ports, dif))
		goto hit;

	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.
	 */
	hash = tcp_v6_hashfn(daddr, hnum, saddr, sport);
	for(sk = tcp_established_hash[hash]; sk; sk = sk->next) {
		/* For IPV6 do the cheaper port and family tests first. */
		if(TCP_IPV6_MATCH(sk, saddr, daddr, ports, dif)) {
			if (sk->state == TCP_ESTABLISHED)
				TCP_RHASH(sport) = sk;
			goto hit; /* You sunk my battleship! */
		}
	}
	/* Must check for a TIME_WAIT'er before going to listener hash. */
	for(sk = tcp_established_hash[hash+(TCP_HTABLE_SIZE/2)]; sk; sk = sk->next) {
		if(*((__u32 *)&(sk->dport))	== ports	&&
		   sk->family			== AF_INET6) {
			struct tcp_tw_bucket *tw = (struct tcp_tw_bucket *)sk;
			if(!ipv6_addr_cmp(&tw->v6_daddr, saddr)	&&
			   !ipv6_addr_cmp(&tw->v6_rcv_saddr, daddr) &&
			   (!sk->bound_dev_if || sk->bound_dev_if == dif))
				goto hit;
		}
	}
	sk = tcp_v6_lookup_listener(daddr, hnum, dif);
hit:
	return sk;
}

#define tcp_v6_lookup(sa, sp, da, dp, dif) __tcp_v6_lookup((0),(sa),(sp),(da),(dp),(dif))

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

static int tcp_v6_unique_address(struct sock *sk)
{
	struct tcp_bind_bucket *tb;
	unsigned short snum = sk->num;
	int retval = 1;

	/* Freeze the hash while we snoop around. */
	SOCKHASH_LOCK();
	tb = tcp_bound_hash[tcp_bhashfn(snum)];
	for(; tb; tb = tb->next) {
		if(tb->port == snum && tb->owners != NULL) {
			/* Almost certainly the re-use port case, search the real hashes
			 * so it actually scales.  (we hope that all ipv6 ftp servers will
			 * use passive ftp, I just cover this case for completeness)
			 */
			sk = __tcp_v6_lookup(NULL, &sk->net_pinfo.af_inet6.daddr,
					     sk->dport,
					     &sk->net_pinfo.af_inet6.rcv_saddr, snum,
					     sk->bound_dev_if);
			if((sk != NULL) && (sk->state != TCP_LISTEN))
				retval = 0;
			break;
		}
	}
	SOCKHASH_UNLOCK();
	return retval;
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
	struct sk_buff *buff;
	int addr_type;
	int mss;

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
	    usin->sin6_port == sk->sport)
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
			sk->backlog_rcv = tcp_v6_do_rcv;
		}
		
		return err;
	}

	if (!ipv6_addr_any(&np->rcv_saddr))
		saddr = &np->rcv_saddr;

	fl.proto = IPPROTO_TCP;
	fl.nl_u.ip6_u.daddr = &np->daddr;
	fl.nl_u.ip6_u.saddr = saddr;
	fl.oif = sk->bound_dev_if;
	fl.uli_u.ports.dport = usin->sin6_port;
	fl.uli_u.ports.sport = sk->sport;

	dst = ip6_route_output(sk, &fl);
	
	if (dst->error) {
		dst_release(dst);
		return dst->error;
	}

	if (dst->pmtu < 576) {
		dst_release(dst);
		return -EINVAL;
	}

	if (fl.oif == 0 && addr_type&IPV6_ADDR_LINKLOCAL) {
		/* Ough! This guy tries to connect to link local
		 * address and did not specify interface.
		 * Actually we should kick him out, but
		 * we will be patient :) --ANK
		 */
		sk->bound_dev_if = dst->dev->ifindex;
	}

	ip6_dst_store(sk, dst);

	if (saddr == NULL) {
		ifa = ipv6_get_saddr(dst, &np->daddr);
	
		if (ifa == NULL)
			return -ENETUNREACH;
		
		saddr = &ifa->addr;

		/* set the source address */
		ipv6_addr_copy(&np->rcv_saddr, saddr);
		ipv6_addr_copy(&np->saddr, saddr);
	}

	buff = sock_wmalloc(sk, (MAX_HEADER + sk->prot->max_header),
			    0, GFP_KERNEL);

	if (buff == NULL)
		return -ENOBUFS;

	sk->dport = usin->sin6_port;

	if (!tcp_v6_unique_address(sk)) {
		kfree_skb(buff);
		return -EADDRNOTAVAIL;
	}

	/*
	 *	Init variables
	 */

	tp->write_seq = secure_tcp_sequence_number(np->saddr.s6_addr32[3],
						   np->daddr.s6_addr32[3],
						   sk->sport, sk->dport);

	sk->mtu = dst->pmtu;
	mss = sk->mtu - sizeof(struct ipv6hdr);
#if 0
	if (np->opt) {
		/* Adjust mss */
	}
#endif

	tcp_connect(sk, buff, mss);

	return 0;
}

static int tcp_v6_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	struct tcp_opt *tp;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	int retval = -EINVAL;

	/*
	 *	Do sanity checking for sendmsg/sendto/send
	 */

	if (msg->msg_flags & ~(MSG_OOB|MSG_DONTROUTE|MSG_DONTWAIT|MSG_NOSIGNAL))
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
		if (addr->sin6_port != sk->dport)
			goto out;
		if (ipv6_addr_cmp(&addr->sin6_addr, &np->daddr))
			goto out;
	}

	lock_sock(sk);
	retval = tcp_do_sendmsg(sk, msg->msg_iovlen, msg->msg_iov, 
				msg->msg_flags);
	/* Push out partial tail frames if needed. */
	tp = &(sk->tp_pinfo.af_tcp);
	if(tp->send_head && tcp_snd_test(sk, tp->send_head))
		tcp_write_xmit(sk);
	release_sock(sk);

out:
	return retval;
}

void tcp_v6_err(struct sk_buff *skb, int type, int code, unsigned char *header, __u32 info,
		struct in6_addr *saddr, struct in6_addr *daddr,
		struct inet6_protocol *protocol)
{
	struct tcphdr *th = (struct tcphdr *)header;
	struct ipv6_pinfo *np;
	struct sock *sk;
	int err;
	int opening;
	struct tcp_opt *tp; 
	__u32 seq; 

	/* XXX: length check for tcphdr missing here */

	sk = tcp_v6_lookup(daddr, th->dest, saddr, th->source, skb->dev->ifindex);

	if (sk == NULL || sk->state == TCP_TIME_WAIT) {
		/* XXX: Update ICMP error count */
		return;
	}

	tp = &sk->tp_pinfo.af_tcp;
	seq = ntohl(th->seq); 
	if (sk->state != TCP_LISTEN && !between(seq, tp->snd_una, tp->snd_nxt)) {
		if (net_ratelimit()) 
			printk(KERN_DEBUG "icmp packet outside the tcp window:"
					  " s:%d %u,%u,%u\n",
			       (int)sk->state, seq, tp->snd_una, tp->snd_nxt); 
		return; 
	}

	np = &sk->net_pinfo.af_inet6;
	if (type == ICMPV6_PKT_TOOBIG && sk->state != TCP_LISTEN) {
		/* icmp should have updated the destination cache entry */

		if (sk->dst_cache)
			dst_check(&sk->dst_cache, np->dst_cookie);

		if (sk->dst_cache == NULL) {
			struct flowi fl;
			struct dst_entry *dst;
			
			fl.proto = IPPROTO_TCP;
			fl.nl_u.ip6_u.daddr = &np->daddr;
			fl.nl_u.ip6_u.saddr = &np->saddr;
			fl.oif = sk->bound_dev_if;
			fl.uli_u.ports.dport = sk->dport;
			fl.uli_u.ports.sport = sk->sport;

			dst = ip6_route_output(sk, &fl);

			ip6_dst_store(sk, dst);
		}

		if (sk->dst_cache->error) {
			sk->err_soft = sk->dst_cache->error;
		} else {
			/* FIXME: Reset sk->mss, taking into account TCP option
			 *        bytes for timestamps. -DaveM
			 */
			sk->mtu = sk->dst_cache->pmtu;
		}
		if (sk->sock_readers) { /* remove later */
			printk(KERN_DEBUG "tcp_v6_err: pmtu disc: socket locked.\n");
			return;
		}
		tcp_simple_retransmit(sk);
		return;
	}

	opening = 0; 
	/* Might be for an open_request */
	switch (sk->state) {
		struct open_request *req, *prev;
		struct ipv6hdr hd;
	case TCP_LISTEN:
		if (sk->sock_readers)
			return;

		/* Grrrr - fix this later. */
		ipv6_addr_copy(&hd.saddr, saddr);
		ipv6_addr_copy(&hd.daddr, daddr); 
		req = tcp_v6_search_req(tp, &hd,th, &prev);
		if (!req)
			return;
		if (seq != req->snt_isn) {
			if (net_ratelimit())
				printk(KERN_DEBUG "icmp packet for openreq "
				       "with wrong seq number:%d:%d\n",
				       seq, req->snt_isn);
			return;
		}
		if (req->sk) {
			sk = req->sk; /* report error in accept */
		} else {
			tp->syn_backlog--;
			tcp_synq_unlink(tp, req, prev);
			req->class->destructor(req);
			tcp_openreq_free(req);
		}
		/* FALL THROUGH */ 
	case TCP_SYN_SENT:
	case TCP_SYN_RECV: 
		opening = 1;
		break; 
	}

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


static void tcp_v6_send_synack(struct sock *sk, struct open_request *req)
{
	struct sk_buff * skb;
	struct dst_entry *dst;
	struct flowi fl;
	int mss;

	fl.proto = IPPROTO_TCP;
	fl.nl_u.ip6_u.daddr = &req->af.v6_req.rmt_addr;
	fl.nl_u.ip6_u.saddr = &req->af.v6_req.loc_addr;
	fl.oif = req->af.v6_req.iif;
	fl.uli_u.ports.dport = req->rmt_port;
	fl.uli_u.ports.sport = sk->sport;

	dst = ip6_route_output(sk, &fl);
	if (dst->error) {
		dst_release(dst);
		return;
	}

	mss = dst->pmtu - sizeof(struct ipv6hdr) - sizeof(struct tcphdr);
#if 0
	/* Subtract option length... */
	if (opt) {
		mss -= opt->optlen;
	}
#endif

	skb = tcp_make_synack(sk, dst, req, mss);
	if (skb) {
		struct tcphdr *th = skb->h.th;

		th->check = tcp_v6_check(th, skb->len,
					 &req->af.v6_req.loc_addr, &req->af.v6_req.rmt_addr,
					 csum_partial((char *)th, skb->len, skb->csum));

		ip6_xmit(sk, skb, &fl, req->af.v6_req.opt);
	}
	dst_release(dst);
}

static void tcp_v6_or_free(struct open_request *req)
{
}

static struct or_calltable or_ipv6 = {
	tcp_v6_send_synack,
	tcp_v6_or_free,
	tcp_v6_send_reset
};

#define BACKLOG(sk) ((sk)->tp_pinfo.af_tcp.syn_backlog) /* lvalue! */
#define BACKLOGMAX(sk) sysctl_max_syn_backlog

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
	if (BACKLOG(sk) >= BACKLOGMAX(sk)) {
		printk(KERN_DEBUG "droping syn ack:%d max:%d\n",
		       BACKLOG(sk), BACKLOGMAX(sk));
		goto drop;		
	}

	req = tcp_openreq_alloc();
	if (req == NULL) {
		goto drop;
	}

	BACKLOG(sk)++;

	req->rcv_wnd = 0;		/* So that tcp_send_synack() knows! */

	req->rcv_isn = TCP_SKB_CB(skb)->seq;
	req->snt_isn = isn;
	tp.tstamp_ok = tp.sack_ok = tp.wscale_ok = tp.snd_wscale = 0;
	tp.in_mss = 536;
	tcp_parse_options(NULL, skb->h.th, &tp, 0);
        req->mss = tp.in_mss;
	if (tp.saw_tstamp) {
		req->mss -= TCPOLEN_TSTAMP_ALIGNED;
                req->ts_recent = tp.rcv_tsval;
	}
        req->tstamp_ok = tp.tstamp_ok;
	req->sack_ok = tp.sack_ok;
        req->snd_wscale = tp.snd_wscale;
        req->wscale_ok = tp.wscale_ok;
	req->rmt_port = skb->h.th->source;
	ipv6_addr_copy(&req->af.v6_req.rmt_addr, &skb->nh.ipv6h->saddr);
	ipv6_addr_copy(&req->af.v6_req.loc_addr, &skb->nh.ipv6h->daddr);
	req->af.v6_req.opt = NULL;	/* FIXME: options */
	req->af.v6_req.iif = sk->bound_dev_if;

	/* So that link locals have meaning */
	if (!sk->bound_dev_if && ipv6_addr_type(&req->af.v6_req.rmt_addr)&IPV6_ADDR_LINKLOCAL)
		req->af.v6_req.iif = skb->dev->ifindex;

	req->class = &or_ipv6;
	req->retrans = 0;
	req->sk = NULL;

	tcp_v6_send_synack(sk, req);

	req->expires = jiffies + TCP_TIMEOUT_INIT;
	tcp_inc_slow_timer(TCP_SLT_SYNACK);
	tcp_synq_queue(&sk->tp_pinfo.af_tcp, req);	

	sk->data_ready(sk, 0);

	return 0;

drop:
	tcp_statistics.TcpAttemptFails++;
	return 0; /* don't send reset */
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
	int mss;
      
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


	if (sk->ack_backlog > sk->max_ack_backlog)
		return NULL; 

	if (dst == NULL) {
		/*
		 *	options / mss / route cache
		 */
	    
		fl.proto = IPPROTO_TCP;
		fl.nl_u.ip6_u.daddr = &req->af.v6_req.rmt_addr;
		fl.nl_u.ip6_u.saddr = &req->af.v6_req.loc_addr;
		fl.oif = sk->bound_dev_if;
		fl.uli_u.ports.dport = req->rmt_port;
		fl.uli_u.ports.sport = sk->sport;

		dst = ip6_route_output(sk, &fl);
	}

	if (dst->error || dst->pmtu < 576)
		goto out;
	
	sk->tp_pinfo.af_tcp.syn_backlog--;
	sk->ack_backlog++;

	mss = dst->pmtu - sizeof(struct ipv6hdr);
#if 0
	/* Adjust mss by option size */
#endif

	newsk = tcp_create_openreq_child(sk, req, skb, mss);
	if (newsk == NULL)
		goto out;

	ip6_dst_store(newsk, dst);

	newtp = &(newsk->tp_pinfo.af_tcp);

	np = &newsk->net_pinfo.af_inet6;
	ipv6_addr_copy(&np->daddr, &req->af.v6_req.rmt_addr);
	ipv6_addr_copy(&np->saddr, &req->af.v6_req.loc_addr);
	ipv6_addr_copy(&np->rcv_saddr, &req->af.v6_req.loc_addr);
	newsk->bound_dev_if = req->af.v6_req.iif;
	newsk->mtu = dst->pmtu;
	newsk->opt = NULL;

	newsk->daddr	= LOOPBACK4_IPV6;
	newsk->saddr	= LOOPBACK4_IPV6;
	newsk->rcv_saddr= LOOPBACK4_IPV6;

	newsk->prot->hash(newsk);
	add_to_prot_sklist(newsk);
	return newsk;

out:
	dst_release(dst);
	return NULL;
}

static void tcp_v6_send_reset(struct sk_buff *skb)
{
	struct tcphdr *th = skb->h.th, *t1; 
	struct sk_buff *buff;
	struct flowi fl;

	if (th->rst)
		return;

	/*
	 * We need to grab some memory, and put together an RST,
	 * and then put it into the queue to be sent.
	 */

	buff = alloc_skb(MAX_HEADER + sizeof(struct ipv6hdr), GFP_ATOMIC);
	if (buff == NULL) 
	  	return;

	skb_reserve(buff, MAX_HEADER + sizeof(struct ipv6hdr));

	t1 = (struct tcphdr *) skb_push(buff,sizeof(struct tcphdr));

	/* Swap the send and the receive. */
	memset(t1, 0, sizeof(*t1));
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

	fl.nl_u.ip6_u.daddr = &skb->nh.ipv6h->saddr;
	fl.nl_u.ip6_u.saddr = &skb->nh.ipv6h->daddr;

	t1->check = csum_ipv6_magic(fl.nl_u.ip6_u.saddr,
				    fl.nl_u.ip6_u.daddr, 
				    sizeof(*t1), IPPROTO_TCP,
				    buff->csum);

	fl.proto = IPPROTO_TCP;
	fl.oif = skb->dev->ifindex;
	fl.uli_u.ports.dport = t1->dest;
	fl.uli_u.ports.sport = t1->source;

	/* sk = NULL, but it is safe for now. RST socket required. */
	buff->dst = ip6_route_output(NULL, &fl);

	if (buff->dst->error == 0) {
		ip6_xmit(NULL, buff, &fl, NULL);
		tcp_statistics.TcpOutSegs++;
		tcp_statistics.TcpOutRsts++;
		return;
	}

	kfree_skb(buff);
}

static struct open_request *tcp_v6_search_req(struct tcp_opt *tp,
					      struct ipv6hdr *ip6h,
					      struct tcphdr *th,
					      struct open_request **prevp)
{
	struct open_request *req, *prev; 
	__u16 rport = th->source;

	/*	assumption: the socket is not in use.
	 *	as we checked the user count on tcp_rcv and we're
	 *	running from a soft interrupt.
	 */
	prev = (struct open_request *) (&tp->syn_wait_queue); 
	for (req = prev->dl_next; req; req = req->dl_next) {
		if (!ipv6_addr_cmp(&req->af.v6_req.rmt_addr, &ip6h->saddr) &&
		    !ipv6_addr_cmp(&req->af.v6_req.loc_addr, &ip6h->daddr) &&
		    req->rmt_port == rport) {
			*prevp = prev; 
			return req; 
		}
		prev = req; 
	}
	return NULL; 
}

static void tcp_v6_rst_req(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct open_request *req, *prev;

	req = tcp_v6_search_req(tp,skb->nh.ipv6h,skb->h.th,&prev);
	if (!req)
		return;
	/* Sequence number check required by RFC793 */
	if (before(TCP_SKB_CB(skb)->seq, req->snt_isn) ||
	    after(TCP_SKB_CB(skb)->seq, req->snt_isn+1))
		return;
	if(req->sk)
		sk->ack_backlog--;
	else
		tp->syn_backlog--;
	tcp_synq_unlink(tp, req, prev);
	req->class->destructor(req);
	tcp_openreq_free(req); 
	net_statistics.EmbryonicRsts++; 
}

static inline struct sock *tcp_v6_hnd_req(struct sock *sk, struct sk_buff *skb)
{
	struct tcphdr *th = skb->h.th; 
	u32 flg = ((u32 *)th)[3]; 

	/* Check for RST */
	if (flg & __constant_htonl(0x00040000)) {
		tcp_v6_rst_req(sk, skb);
		return NULL;
	}
		
	/* Check SYN|ACK */
	if (flg & __constant_htonl(0x00120000)) {
		struct open_request *req, *dummy;
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
			
		req = tcp_v6_search_req(tp, skb->nh.ipv6h,th, &dummy);
		if (req) {
			sk = tcp_check_req(sk, skb, req);
		}
#if 0 /*def CONFIG_SYN_COOKIES */
		 else {
			sk = cookie_v6_check(sk, skb, (struct ipv6_options *) skb->cb);
		 }
#endif
	}
	return sk;
}

static int tcp_v6_do_rcv(struct sock *sk, struct sk_buff *skb)
{
	/* Imagine: socket is IPv6. IPv4 packet arrives,
	   goes to IPv4 receive handler and backlogged.
	   From backlog it always goes here. Kerboom...
	   Fortunately, tcp_rcv_established and rcv_established
	   handle them correctly, but it is not case with
	   tcp_v6_hnd_req and tcp_v6_send_reset().   --ANK
	 */

	if (skb->protocol == __constant_htons(ETH_P_IP))
		return tcp_v4_do_rcv(sk, skb);

	/*
	 *	socket locking is here for SMP purposes as backlog rcv
	 *	is currently called with bh processing disabled.
	 */

	/* XXX We need to think more about socket locking
	 * XXX wrt. backlog queues, __release_sock(), etc.  -DaveM
	 */
	lock_sock(sk); 

	/* 
	 * This doesn't check if the socket has enough room for the packet.
	 * Either process the packet _without_ queueing it and then free it,
	 * or do the check later.
	 */
	skb_set_owner_r(skb, sk);

	if (sk->state == TCP_ESTABLISHED) { /* Fast path */
		if (tcp_rcv_established(sk, skb, skb->h.th, skb->len))
			goto reset;
		release_sock(sk);
		return 0;
	}

	if (sk->state == TCP_LISTEN) { 
		struct sock *nsk;
		
		nsk = tcp_v6_hnd_req(sk, skb);
		if (!nsk)
			goto discard;
		lock_sock(nsk);
		release_sock(sk);
		sk = nsk;
	}

	if (tcp_rcv_state_process(sk, skb, skb->h.th, skb->cb, skb->len))
		goto reset;
	release_sock(sk);
	return 0;

reset:
	tcp_v6_send_reset(skb);
discard:
	kfree_skb(skb);
	release_sock(sk);  
	return 0;
}

int tcp_v6_rcv(struct sk_buff *skb, struct device *dev,
	       struct in6_addr *saddr, struct in6_addr *daddr,
	       struct ipv6_options *opt, unsigned short len,
	       int redo, struct inet6_protocol *protocol)
{
	struct tcphdr *th;	
	struct sock *sk;

	th = skb->h.th;

	if (skb->pkt_type != PACKET_HOST)
		goto discard_it;

	/*
	 *	Pull up the IP header.
	 */

	__skb_pull(skb, skb->h.raw - skb->data);

	/*
	 *	Count it even if it's bad.
	 */

	tcp_statistics.TcpInSegs++;

	/*
	 *	Try to use the device checksum if provided.
	 */

	switch (skb->ip_summed) {
	case CHECKSUM_NONE:
		skb->csum = csum_partial((char *)th, len, 0);
	case CHECKSUM_HW:
		if (tcp_v6_check(th,len,saddr,daddr,skb->csum)) {
			printk(KERN_DEBUG "tcp csum failed\n");
			tcp_statistics.TcpInErrs++;
			goto discard_it;
		}
	default:
		/* CHECKSUM_UNNECESSARY */
	};

	sk = __tcp_v6_lookup(th, saddr, th->source, daddr, th->dest, dev->ifindex);

	if (!sk)
		goto no_tcp_socket;

	TCP_SKB_CB(skb)->seq = ntohl(th->seq);
	TCP_SKB_CB(skb)->end_seq = (TCP_SKB_CB(skb)->seq + th->syn + th->fin +
				    len - th->doff*4);
	TCP_SKB_CB(skb)->ack_seq = ntohl(th->ack_seq);
	skb->used = 0;
	if(sk->state == TCP_TIME_WAIT)
		goto do_time_wait;

	if (!sk->sock_readers)
		return tcp_v6_do_rcv(sk, skb);

	__skb_queue_tail(&sk->back_log, skb);
	return(0);

no_tcp_socket:
	tcp_v6_send_reset(skb);

discard_it:

	/*
	 *	Discard frame
	 */

	kfree_skb(skb);
	return 0;

do_time_wait:
	if(tcp_timewait_state_process((struct tcp_tw_bucket *)sk,
				      skb, th, &(IPCB(skb)->opt), skb->len))
		goto no_tcp_socket;
	goto discard_it;
}

static int tcp_v6_rebuild_header(struct sock *sk)
{
	struct dst_entry *dst = NULL;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;

	if (sk->dst_cache)
		dst = dst_check(&sk->dst_cache, np->dst_cookie);

	if (dst == NULL) {
		struct flowi fl;

		fl.proto = IPPROTO_TCP;
		fl.nl_u.ip6_u.daddr = &np->daddr;
		fl.nl_u.ip6_u.saddr = &np->saddr;
		fl.oif = sk->bound_dev_if;
		fl.uli_u.ports.dport = sk->dport;
		fl.uli_u.ports.sport = sk->sport;

		dst = ip6_route_output(sk, &fl);

		if (dst->error) {
			dst_release(dst);
			return dst->error;
		}

		ip6_dst_store(sk, dst);
	}

	return dst->error;
}

static struct sock * tcp_v6_get_sock(struct sk_buff *skb, struct tcphdr *th)
{
	struct in6_addr *saddr;
	struct in6_addr *daddr;

	saddr = &skb->nh.ipv6h->saddr;
	daddr = &skb->nh.ipv6h->daddr;
	return tcp_v6_lookup(saddr, th->source, daddr, th->dest, skb->dev->ifindex);
}

static void tcp_v6_xmit(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct ipv6_pinfo * np = &sk->net_pinfo.af_inet6;
	struct flowi fl;
	struct dst_entry *dst = sk->dst_cache;

	fl.proto = IPPROTO_TCP;
	fl.nl_u.ip6_u.daddr = &np->daddr;
	fl.nl_u.ip6_u.saddr = &np->saddr;
	fl.oif = sk->bound_dev_if;
	fl.uli_u.ports.sport = sk->sport;
	fl.uli_u.ports.dport = sk->dport;

	if (sk->dst_cache)
		dst = dst_check(&sk->dst_cache, np->dst_cookie);

	if (dst == NULL) {
		dst = ip6_route_output(sk, &fl);

		if (dst->error) {
			sk->err_soft = dst->error;
			dst_release(dst);
			return;
		}

		ip6_dst_store(sk, dst);
	}

	skb->dst = dst_clone(dst);

	ip6_xmit(sk, skb, &fl, np->opt);
}

static void v6_addr2sockaddr(struct sock *sk, struct sockaddr * uaddr)
{
	struct ipv6_pinfo * np = &sk->net_pinfo.af_inet6;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) uaddr;

	sin6->sin6_family = AF_INET6;
	memcpy(&sin6->sin6_addr, &np->daddr, sizeof(struct in6_addr));
	sin6->sin6_port	= sk->dport;
}

static struct tcp_func ipv6_specific = {
	tcp_v6_xmit,
	tcp_v6_send_check,
	tcp_v6_rebuild_header,
	tcp_v6_conn_request,
	tcp_v6_syn_recv_sock,
	tcp_v6_get_sock,
	ipv6_setsockopt,
	ipv6_getsockopt,
	v6_addr2sockaddr,
	sizeof(struct sockaddr_in6)
};

/*
 *	TCP over IPv4 via INET6 API
 */

static struct tcp_func ipv6_mapped = {
	ip_queue_xmit,
	tcp_v4_send_check,
	tcp_v4_rebuild_header,
	tcp_v6_conn_request,
	tcp_v6_syn_recv_sock,
	tcp_v6_get_sock,
	ipv6_setsockopt,
	ipv6_getsockopt,
	v6_addr2sockaddr,
	sizeof(struct sockaddr_in6)
};

/* NOTE: A lot of things set to zero explicitly by call to
 *       sk_alloc() so need not be done here.
 */
static int tcp_v6_init_sock(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	skb_queue_head_init(&tp->out_of_order_queue);
	tcp_init_xmit_timers(sk);

	tp->rto  = TCP_TIMEOUT_INIT;		/*TCP_WRITE_TIME*/
	tp->mdev = TCP_TIMEOUT_INIT;
	tp->in_mss = 536;

	/* See draft-stevens-tcpca-spec-01 for discussion of the
	 * initialization of these values.
	 */
	tp->snd_cwnd = (1 << TCP_CWND_SHIFT);
	tp->snd_ssthresh = 0x7fffffff;

	sk->priority = 1;
	sk->state = TCP_CLOSE;
	sk->max_ack_backlog = SOMAXCONN;
	sk->mtu = 576;
	sk->mss = 536;

	/* Init SYN queue. */
	tcp_synq_init(tp);

	sk->tp_pinfo.af_tcp.af_specific = &ipv6_specific;

	return 0;
}

static int tcp_v6_destroy_sock(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb;

	tcp_clear_xmit_timers(sk);

	if (sk->keepopen)
		tcp_dec_slow_timer(TCP_SLT_KEEPALIVE);

	/*
	 *	Cleanup up the write buffer.
	 */

  	while((skb = __skb_dequeue(&sk->write_queue)) != NULL)
		kfree_skb(skb);

	/*
	 *  Cleans up our, hopefuly empty, out_of_order_queue
	 */

  	while((skb = __skb_dequeue(&tp->out_of_order_queue)) != NULL)
		kfree_skb(skb);

	/*
	 *	Release destination entry
	 */

	dst_release(xchg(&sk->dst_cache,NULL));

	/* Clean up a locked TCP bind bucket, this only happens if a
	 * port is allocated for a socket, but it never fully connects.
	 * In which case we will find num to be non-zero and daddr to
	 * be zero.
	 */
	if(ipv6_addr_any(&(sk->net_pinfo.af_inet6.daddr)) && sk->num != 0)
		tcp_bucket_unlock(sk);

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
	tcp_v6_do_rcv,			/* backlog_rcv */
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
