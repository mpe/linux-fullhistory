/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_ipv4.c,v 1.23 1997/03/17 04:49:38 davem Exp $
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

/*
 * Changes:
 *		David S. Miller	:	New socket lookup architecture.
 *					This code is dedicated to John Dyson.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/random.h>
#include <linux/ipsec.h>

#include <net/icmp.h>
#include <net/tcp.h>
#include <net/ipv6.h>

#include <asm/segment.h>

static void tcp_v4_send_reset(struct sk_buff *skb);

void tcp_v4_send_check(struct sock *sk, struct tcphdr *th, int len, 
		       struct sk_buff *skb);

/* This is for sockets with full identity only.  Sockets here will always
 * be without wildcards and will have the following invariant:
 *          TCP_ESTABLISHED <= sk->state < TCP_CLOSE
 */
struct sock *tcp_established_hash[TCP_HTABLE_SIZE];

/* All sockets in TCP_LISTEN state will be in here.  This is the only table
 * where wildcard'd TCP sockets can exist.  Hash function here is just local
 * port number.  XXX Fix or we'll lose with thousands of IP aliases...
 */
struct sock *tcp_listening_hash[TCP_LHTABLE_SIZE];

/* Ok, let's try this, I give up, we do need a local binding
 * TCP hash as well as the others for fast bind/connect.
 */
struct sock *tcp_bound_hash[TCP_BHTABLE_SIZE];

static __inline__ int tcp_hashfn(__u32 laddr, __u16 lport,
				 __u32 faddr, __u16 fport)
{
	return ((laddr ^ lport) ^ (faddr ^ fport)) & (TCP_HTABLE_SIZE - 1);
}

static __inline__ int tcp_sk_hashfn(struct sock *sk)
{
	__u32 laddr = sk->rcv_saddr;
	__u16 lport = sk->num;
	__u32 faddr = sk->daddr;
	__u16 fport = sk->dummy_th.dest;

	return tcp_hashfn(laddr, lport, faddr, fport);
}

static int tcp_v4_verify_bind(struct sock *sk, unsigned short snum)
{
	struct sock *sk2;
	int retval = 0, sk_reuse = sk->reuse;

	SOCKHASH_LOCK();
	sk2 = tcp_bound_hash[tcp_bhashfn(snum)];
	for(; sk2 != NULL; sk2 = sk2->bind_next) {
		if((sk2->num == snum) && (sk2 != sk)) {
			unsigned char state = sk2->state;
			int sk2_reuse = sk2->reuse;

			if(!sk2->rcv_saddr || !sk->rcv_saddr) {
				if((!sk2_reuse)			||
				   (!sk_reuse)			||
				   (state == TCP_LISTEN)) {
					retval = 1;
					break;
				}
			} else if(sk2->rcv_saddr == sk->rcv_saddr) {
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

static __inline__ int tcp_lport_inuse(int num)
{
	struct sock *sk = tcp_bound_hash[tcp_bhashfn(num)];

	for(; sk != NULL; sk = sk->bind_next) {
		if(sk->num == num)
			return 1;
	}
	return 0;
}

/* Find a "good" local port, this is family independant.
 * There are several strategies working in unison here to
 * get the best possible performance.  The current socket
 * load is kept track of, if it is zero there is a strong
 * likely hood that there is a zero length chain we will
 * find with a small amount of searching, else the load is
 * what we shoot for for when the chains all have at least
 * one entry.  The base helps us walk the chains in an
 * order such that a good chain is found as quickly as possible.  -DaveM
 */
unsigned short tcp_good_socknum(void)
{
	static int start = PROT_SOCK;
	static int binding_contour = 0;
	int best = 0;
	int size = 32767; /* a big num. */
	int retval = 0, i, end, bc;

	SOCKHASH_LOCK();
	i = tcp_bhashfn(start);
	end = i + TCP_BHTABLE_SIZE;
	bc = binding_contour;
	do {
		struct sock *sk = tcp_bound_hash[tcp_bhashfn(i)];
		if(!sk) {
			retval = (start + i);
			start  = (retval + 1);

			/* Check for decreasing load. */
			if(bc != 0)
				binding_contour = 0;
			goto done;
		} else {
			int j = 0;
			do { sk = sk->bind_next; } while(++j < size && sk);
			if(j < size) {
				best = (start + i);
				size = j;
				if(bc && size <= bc) {
					start = best + 1;
					goto verify;
				}
			}
		}
	} while(++i != end);

	/* Socket load is increasing, adjust our load average. */
	binding_contour = size;
verify:
	if(size < binding_contour)
		binding_contour = size;

	if(best > 32767)
		best -= (32768 - PROT_SOCK);

	while(tcp_lport_inuse(best))
		best += TCP_BHTABLE_SIZE;
	retval = best;
done:
	if(start > 32767)
		start -= (32768 - PROT_SOCK);

	SOCKHASH_UNLOCK();

	return retval;
}

static void tcp_v4_hash(struct sock *sk)
{
	unsigned char state;

	SOCKHASH_LOCK();
	state = sk->state;
	if(state != TCP_CLOSE || !sk->dead) {
		struct sock **skp;

		if(state == TCP_LISTEN)
			skp = &tcp_listening_hash[tcp_sk_listen_hashfn(sk)];
		else
			skp = &tcp_established_hash[tcp_sk_hashfn(sk)];

		if((sk->next = *skp) != NULL)
			(*skp)->pprev = &sk->next;
		*skp = sk;
		sk->pprev = skp;
		tcp_sk_bindify(sk);
	}
	SOCKHASH_UNLOCK();
}

static void tcp_v4_unhash(struct sock *sk)
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

static void tcp_v4_rehash(struct sock *sk)
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
	if(state != TCP_CLOSE || !sk->dead) {
		struct sock **skp;

		if(state == TCP_LISTEN)
			skp = &tcp_listening_hash[tcp_sk_listen_hashfn(sk)];
		else
			skp = &tcp_established_hash[tcp_sk_hashfn(sk)];

		if((sk->next = *skp) != NULL)
			(*skp)->pprev = &sk->next;
		*skp = sk;
		sk->pprev = skp;
		tcp_sk_bindify(sk);
	}
	SOCKHASH_UNLOCK();
}

/* Don't inline this cruft.  Here are some nice properties to
 * exploit here.  The BSD API does not allow a listening TCP
 * to specify the remote port nor the remote address for the
 * connection.  So always assume those are both wildcarded
 * during the search since they can never be otherwise.
 *
 * XXX Later on, hash on both local port _and_ local address,
 * XXX to handle a huge IP alias'd box.  Keep in mind that
 * XXX such a scheme will require us to run through the listener
 * XXX hash twice, once for local addresses bound, and once for
 * XXX the local address wildcarded (because the hash is different).
 */
static struct sock *tcp_v4_lookup_longway(u32 daddr, unsigned short hnum)
{
	struct sock *sk = tcp_listening_hash[tcp_lhashfn(hnum)];
	struct sock *result = NULL;

	for(; sk; sk = sk->next) {
		if(sk->num == hnum) {
			__u32 rcv_saddr = sk->rcv_saddr;

			if(rcv_saddr) {
				if(rcv_saddr == daddr)
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
static inline struct sock *__tcp_v4_lookup(struct tcphdr *th,
					   u32 saddr, u16 sport, u32 daddr, u16 dport)
{
	unsigned short hnum = ntohs(dport);
	struct sock *sk;

	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.  It is assumed that this code only
	 * gets called from within NET_BH.
	 */
	sk = tcp_established_hash[tcp_hashfn(daddr, hnum, saddr, sport)];
	for(; sk; sk = sk->next)
		if(sk->daddr		== saddr		&& /* remote address */
		   sk->dummy_th.dest	== sport		&& /* remote port    */
		   sk->num		== hnum			&& /* local port     */
		   sk->rcv_saddr	== daddr)		   /* local address  */
			goto hit; /* You sunk my battleship! */
	sk = tcp_v4_lookup_longway(daddr, hnum);
hit:
	return sk;
}

__inline__ struct sock *tcp_v4_lookup(u32 saddr, u16 sport, u32 daddr, u16 dport)
{
	return __tcp_v4_lookup(0, saddr, sport, daddr, dport);
}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
#define secondlist(hpnum, sk, fpass) \
({ struct sock *s1; if(!(sk) && (fpass)--) \
	s1 = tcp_bound_hash[tcp_bhashfn(hpnum)]; \
   else \
	s1 = (sk); \
   s1; \
})

#define tcp_v4_proxy_loop_init(hnum, hpnum, sk, fpass) \
	secondlist((hpnum), tcp_bound_hash[tcp_bhashfn(hnum)],(fpass))

#define tcp_v4_proxy_loop_next(hnum, hpnum, sk, fpass) \
	secondlist((hpnum),(sk)->bind_next,(fpass))

struct sock *tcp_v4_proxy_lookup(unsigned short num, unsigned long raddr,
				 unsigned short rnum, unsigned long laddr,
				 unsigned long paddr, unsigned short pnum)
{
	struct sock *s, *result = NULL;
	int badness = -1;
	unsigned short hnum = ntohs(num);
	unsigned short hpnum = ntohs(pnum);
	int firstpass = 1;

	/* This code must run only from NET_BH. */
	for(s = tcp_v4_proxy_loop_init(hnum, hpnum, s, firstpass);
	    s != NULL;
	    s = tcp_v4_proxy_loop_next(hnum, hpnum, s, firstpass)) {
		if(s->num == hnum || s->num == hpnum) {
			int score = 0;
			if(s->dead && (s->state == TCP_CLOSE))
				continue;
			if(s->rcv_saddr) {
				if((s->num != hpnum || s->rcv_saddr != paddr) &&
				   (s->num != hnum || s->rcv_saddr != laddr))
					continue;
				score++;
			}
			if(s->daddr) {
				if(s->daddr != raddr)
					continue;
				score++;
			}
			if(s->dummy_th.dest) {
				if(s->dummy_th.dest != rnum)
					continue;
				score++;
			}
			if(score == 3 && s->num == hnum) {
				result = s;
				break;
			} else if(score > badness && (s->num == hpnum || s->rcv_saddr)) {
					result = s;
					badness = score;
			}
		}
	}
	return result;
}

#undef secondlist
#undef tcp_v4_proxy_loop_init
#undef tcp_v4_proxy_loop_next

#endif

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
	int retval = 1, hashent = tcp_hashfn(saddr, snum, daddr, dnum);
	struct sock * sk;

	/* Make sure we are allowed to connect here.
	 * But freeze the hash while we snoop around.
	 */
	SOCKHASH_LOCK();
	sk = tcp_established_hash[hashent];
	for (; sk != NULL; sk = sk->next) {
		if(sk->daddr		== daddr		&& /* remote address */
		   sk->dummy_th.dest	== dnum			&& /* remote port */
		   sk->num		== snum			&& /* local port */
		   sk->saddr		== saddr) {		   /* local address */
			retval = 0;
			break;
		}
	}
	SOCKHASH_UNLOCK();
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

	tcp_set_state(sk,TCP_SYN_SENT);

	/* Socket identity change complete, no longer
	 * in TCP_CLOSE, so rehash.
	 */
	tcp_v4_rehash(sk);

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
				msg->msg_flags);

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
	struct sock *sk;

	sk = tcp_v4_lookup(iph->saddr, th->source, iph->daddr, th->dest);

	if (sk == NULL)
		return;

	if (type == ICMP_SOURCE_QUENCH)
	{
		struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

		sk->ssthresh = max(sk->cong_window >> 1, 2);
		sk->cong_window = sk->ssthresh + 3;
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

/* This routine computes an IPv4 TCP checksum. */
void tcp_v4_send_check(struct sock *sk, struct tcphdr *th, int len, 
		       struct sk_buff *skb)
{
	th->check = 0;
	th->check = tcp_v4_check(th, len, sk->saddr, sk->daddr,
				 csum_partial((char *)th, sizeof(*th), skb->csum));
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

	sk = tcp_v4_lookup(iph->saddr, th->source, iph->daddr, th->dest);

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
		SOCK_DEBUG(sk, "Reset on %p: Connect on dead socket.\n",sk);
		tcp_statistics.TcpAttemptFails++;
		return -ENOTCONN;
	}

	if (sk->ack_backlog >= sk->max_ack_backlog ||
	    tcp_v4_syn_filter(sk, skb, saddr))
	{
		SOCK_DEBUG(sk, "dropping syn ack:%d max:%d\n", sk->ack_backlog,
			   sk->max_ack_backlog);
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

	newsk->send_head = NULL;

	newtp = &(newsk->tp_pinfo.af_tcp);
	newtp->send_head = NULL;
	newtp->retrans_head = NULL;

	newtp->pending = 0;

	skb_queue_head_init(&newsk->back_log);

	newsk->prot->init(newsk);

	newsk->cong_count = 0;
	newsk->ssthresh = 0;
	newtp->backoff = 0;
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
	newsk->sock_readers=0;

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

	tcp_v4_hash(newsk);
	add_to_prot_sklist(newsk);
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
				/*
				 *	socket already created but not
				 *	yet accepted()...
				 */

				sk = req->sk;
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

			sk = tp->af_specific->syn_recv_sock(sk, skb, req);

			tcp_dec_slow_timer(TCP_SLT_SYNACK);

			if (sk == NULL)
			{
				return NULL;
			}

			req->expires = 0UL;
			req->sk = sk;
			break;
		}

		req = req->dl_next;
	} while (req != tp->syn_wait_queue);

	skb_orphan(skb);
	skb_set_owner_r(skb, sk);
	return sk;
}

int tcp_v4_do_rcv(struct sock *sk, struct sk_buff *skb)
{
	skb_set_owner_r(skb, sk);

	/*
	 *	socket locking is here for SMP purposes as backlog rcv
	 *	is currently called with bh processing disabled.
	 */
	lock_sock(sk);

	if (sk->state == TCP_ESTABLISHED)
	{
		if (tcp_rcv_established(sk, skb, skb->h.th, skb->len))
			goto reset;
		goto ok;
	}

	if (sk->state == TCP_LISTEN)
	{
		struct sock *nsk;

		/*
		 *	find possible connection requests
		 */

		nsk = tcp_v4_check_req(sk, skb);

		if (nsk == NULL)
		{
			goto discard_it;
		}

		release_sock(sk);
		lock_sock(nsk);
		sk = nsk;
	}

	if (tcp_rcv_state_process(sk, skb, skb->h.th, NULL, skb->len) == 0)
		goto ok;

reset:
	tcp_v4_send_reset(skb);

discard_it:
	/*
	 *	Discard frame
	 */
	kfree_skb(skb, FREE_READ);

ok:
	release_sock(sk);
	return 0;
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
			printk(KERN_DEBUG "TCPv4 bad checksum from %08x:%04x to %08x:%04x, len=%d/%d/%d\n",
			       saddr, ntohs(th->source), daddr,
			       ntohs(th->dest), len, skb->len, ntohs(iph->tot_len));
					goto discard_it;
		}
	default:
		/* CHECKSUM_UNNECESSARY */
	};

	tcp_statistics.TcpInSegs++;

#ifdef CONFIG_IP_TRANSPARENT_PROXY
	if (IPCB(skb)->redirport)
		sk = tcp_v4_proxy_lookup(th->dest, saddr, th->source, daddr,
					 skb->dev->pa_addr, IPCB(skb)->redirport);
	else
#endif
	sk = __tcp_v4_lookup(th, saddr, th->source, daddr, th->dest);
	if (!sk)
		goto no_tcp_socket;
	if(!ipsec_sk_policy(sk,skb))
		goto discard_it;

	skb->seq = ntohl(th->seq);
	skb->end_seq = skb->seq + th->syn + th->fin + len - th->doff*4;
	skb->ack_seq = ntohl(th->ack_seq);

	skb->acked = 0;
	skb->used = 0;

	if (!sk->sock_readers)
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
	return tcp_v4_lookup(skb->nh.iph->saddr, th->source,
			     skb->nh.iph->daddr, th->dest);
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

	/*
	 *	Speed up by setting some standard state for the dummy_th.
	 */

	sk->dummy_th.doff = sizeof(sk->dummy_th)/4;
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
	(struct sock *)&tcp_prot,	/* sklist_next */
	(struct sock *)&tcp_prot,	/* sklist_prev */
	tcp_close,			/* close */
	tcp_v4_connect,			/* connect */
	tcp_accept,			/* accept */
	NULL,				/* retransmit */
	tcp_write_wakeup,		/* write_wakeup */
	tcp_read_wakeup,		/* read_wakeup */
	tcp_poll,			/* poll */
	tcp_ioctl,			/* ioctl */
	tcp_v4_init_sock,		/* init */
	tcp_v4_destroy_sock,		/* destroy */
	tcp_shutdown,			/* shutdown */
	tcp_setsockopt,			/* setsockopt */
	tcp_getsockopt,			/* getsockopt */
	tcp_v4_sendmsg,			/* sendmsg */
	tcp_recvmsg,			/* recvmsg */
	NULL,				/* bind */
	tcp_v4_do_rcv,			/* backlog_rcv */
	tcp_v4_hash,			/* hash */
	tcp_v4_unhash,			/* unhash */
	tcp_v4_rehash,			/* rehash */
	tcp_good_socknum,		/* good_socknum */
	tcp_v4_verify_bind,		/* verify_bind */
	128,				/* max_header */
	0,				/* retransmits */
	"TCP",				/* name */
	0,				/* inuse */
	0				/* highestinuse */
};
