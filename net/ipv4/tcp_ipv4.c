/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_ipv4.c,v 1.76 1997/12/07 04:44:19 freitag Exp $
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
 *		David S. Miller :	Change semantics of established hash,
 *					half is devoted to TIME_WAIT sockets
 *					and the rest go in the other half.
 *		Andi Kleen :		Add support for syncookies and fixed
 *					some bugs: ip options weren't passed to
 *					the TCP layer, missed a check for an ACK bit.
 *		Andi Kleen :		Implemented fast path mtu discovery.
 *	     				Fixed many serious bugs in the
 *					open_request handling and moved
 *					most of it into the af independent code.
 *					Added tail drop and some other bugfixes.
 *					Added new listen sematics (ifdefed by
 *					NEW_LISTEN for now)
 *	Juan Jose Ciarlante:		ip_dynaddr bits
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/random.h>
#include <linux/ipsec.h>
#include <linux/inet.h>

#include <net/icmp.h>
#include <net/tcp.h>
#include <net/ipv6.h>

#include <asm/segment.h>

extern int sysctl_tcp_sack;
extern int sysctl_tcp_tsack;
extern int sysctl_tcp_timestamps;
extern int sysctl_tcp_window_scaling;
extern int sysctl_tcp_syncookies;
extern int sysctl_ip_dynaddr;

/* Check TCP sequence numbers in ICMP packets. */
#define ICMP_PARANOIA 1 
#ifndef ICMP_PARANOIA
#define ICMP_MIN_LENGTH 4
#else
#define ICMP_MIN_LENGTH 8
#endif

static void tcp_v4_send_reset(struct sk_buff *skb);

void tcp_v4_send_check(struct sock *sk, struct tcphdr *th, int len, 
		       struct sk_buff *skb);

/* This is for sockets with full identity only.  Sockets here will always
 * be without wildcards and will have the following invariant:
 *          TCP_ESTABLISHED <= sk->state < TCP_CLOSE
 *
 * First half of the table is for sockets not in TIME_WAIT, second half
 * is for TIME_WAIT sockets only.
 */
struct sock *tcp_established_hash[TCP_HTABLE_SIZE];

/* All sockets in TCP_LISTEN state will be in here.  This is the only table
 * where wildcard'd TCP sockets can exist.  Hash function here is just local
 * port number.
 */
struct sock *tcp_listening_hash[TCP_LHTABLE_SIZE];

/* Ok, let's try this, I give up, we do need a local binding
 * TCP hash as well as the others for fast bind/connect.
 */
struct sock *tcp_bound_hash[TCP_BHTABLE_SIZE];

/*
 * This array holds the first and last local port number.
 * For high-usage systems, use sysctl to change this to
 * 32768-61000
 */
int sysctl_local_port_range[2] = { 1024, 4999 };

static __inline__ int tcp_hashfn(__u32 laddr, __u16 lport,
				 __u32 faddr, __u16 fport)
{
	return ((laddr ^ lport) ^ (faddr ^ fport)) & ((TCP_HTABLE_SIZE/2) - 1);
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

			/* Two sockets can be bound to the same port if they're
			 * bound to different interfaces.
			 */

			if(sk->bound_dev_if != sk2->bound_dev_if)
				continue;

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
	static int start = 0;
	static int binding_contour = 0;
	int best = 0;
	int size = 32767; /* a big num. */
	int retval = 0, i, end, bc;

	SOCKHASH_LOCK();
        if (start > sysctl_local_port_range[1] || start < sysctl_local_port_range[0])
		start = sysctl_local_port_range[0];
        i = tcp_bhashfn(start);
        end = i + TCP_BHTABLE_SIZE;
        bc = binding_contour;
        do {
                struct sock *sk = tcp_bound_hash[i&(TCP_BHTABLE_SIZE-1)];
                if(!sk) {
                        /* find the smallest value no smaller than start
                         * that has this hash value.
                         */
                        retval = tcp_bhashnext(start-1,i&(TCP_BHTABLE_SIZE-1));

                        /* Check for decreasing load. */
                        if (bc != 0)
                                binding_contour = 0;
                        goto done;
                } else {
                        int j = 0;
                        do { sk = sk->bind_next; } while (++j < size && sk);
                        if (j < size) {
                                best = i&(TCP_BHTABLE_SIZE-1);
                                size = j;
                                if (bc && size <= bc)
                                        goto verify;
                        }
                }
        } while(++i != end);
        i = best;

        /* Socket load is increasing, adjust our load average. */
        binding_contour = size;
verify:
        if (size < binding_contour)
                binding_contour = size;

        retval = tcp_bhashnext(start-1,i);

	best = retval;	/* mark the starting point to avoid infinite loops */
        while(tcp_lport_inuse(retval)) {
               	retval = tcp_bhashnext(retval,i);
		if (retval > sysctl_local_port_range[1]) /* Upper bound */
			retval = tcp_bhashnext(sysctl_local_port_range[0],i);
		if (retval == best) {
			/* This hash chain is full. No answer. */
			retval = 0;
			break;
		}
        }

done:
        start = (retval + 1);
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

		if(state == TCP_LISTEN) {
			skp = &tcp_listening_hash[tcp_sk_listen_hashfn(sk)];
		} else {
			int hash= tcp_sk_hashfn(sk);
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

/* Don't inline this cruft.  Here are some nice properties to
 * exploit here.  The BSD API does not allow a listening TCP
 * to specify the remote port nor the remote address for the
 * connection.  So always assume those are both wildcarded
 * during the search since they can never be otherwise.
 */
static struct sock *tcp_v4_lookup_listener(u32 daddr, unsigned short hnum, int dif)
{
	struct sock *sk;
	struct sock *result = NULL;
	int score, hiscore;

	hiscore=0;
	for(sk = tcp_listening_hash[tcp_lhashfn(hnum)]; sk; sk = sk->next) {
		if(sk->num == hnum) {
			__u32 rcv_saddr = sk->rcv_saddr;

			score = 1;
			if(rcv_saddr) {
				if (rcv_saddr != daddr)
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
 */
static inline struct sock *__tcp_v4_lookup(struct tcphdr *th,
					   u32 saddr, u16 sport, u32 daddr, u16 dport, int dif)
{
	unsigned short hnum = ntohs(dport);
	struct sock *sk;
	int hash = tcp_hashfn(daddr, hnum, saddr, sport);

	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.  It is assumed that this code only
	 * gets called from within NET_BH.
	 */
	for(sk = tcp_established_hash[hash]; sk; sk = sk->next)
		if(sk->daddr		== saddr		&& /* remote address */
		   sk->dummy_th.dest	== sport		&& /* remote port    */
		   sk->num		== hnum			&& /* local port     */
		   sk->rcv_saddr	== daddr		&& /* local address  */
		   (!sk->bound_dev_if || sk->bound_dev_if == dif))
			goto hit; /* You sunk my battleship! */

	/* Must check for a TIME_WAIT'er before going to listener hash. */
	for(sk = tcp_established_hash[hash+(TCP_HTABLE_SIZE/2)]; sk; sk = sk->next)
		if(sk->daddr		== saddr		&& /* remote address */
		   sk->dummy_th.dest	== sport		&& /* remote port    */
		   sk->num		== hnum			&& /* local port     */
		   sk->rcv_saddr	== daddr		&& /* local address  */
		   (!sk->bound_dev_if || sk->bound_dev_if == dif))
			goto hit;

	sk = tcp_v4_lookup_listener(daddr, hnum, dif);
hit:
	return sk;
}

__inline__ struct sock *tcp_v4_lookup(u32 saddr, u16 sport, u32 daddr, u16 dport, int dif)
{
	return __tcp_v4_lookup(0, saddr, sport, daddr, dport, dif);
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

static struct sock *tcp_v4_proxy_lookup(unsigned short num, unsigned long raddr,
					unsigned short rnum, unsigned long laddr,
					struct device *dev, unsigned short pnum,
					int dif)
{
	struct sock *s, *result = NULL;
	int badness = -1;
	u32 paddr = 0;
	unsigned short hnum = ntohs(num);
	unsigned short hpnum = ntohs(pnum);
	int firstpass = 1;

	if(dev && dev->ip_ptr) {
		struct in_device *idev = dev->ip_ptr;

		if(idev->ifa_list)
			paddr = idev->ifa_list->ifa_local;
	}

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
			if(s->bound_dev_if) {
				if(s->bound_dev_if != dif)
					continue;
				score++;
			}
			if(score == 4 && s->num == hnum) {
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

static inline __u32 tcp_v4_init_sequence(struct sock *sk, struct sk_buff *skb)
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
			goto out;
		}
	}

	/* Must check TIME_WAIT'ers too. */
	sk = tcp_established_hash[hashent + (TCP_HTABLE_SIZE/2)];
	for (; sk != NULL; sk = sk->next) {
		if(sk->daddr		== daddr		&& /* remote address */
		   sk->dummy_th.dest	== dnum			&& /* remote port */
		   sk->num		== snum			&& /* local port */
		   sk->saddr		== saddr) {		   /* local address */
			retval = 0;
			goto out;
		}
	}
out:
	SOCKHASH_UNLOCK();
	return retval;
}


/*
 *	This will initiate an outgoing connection. 
 */
 
int tcp_v4_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sk_buff *buff;
	int tmp;
	struct tcphdr *th;
	struct rtable *rt;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sockaddr_in *usin = (struct sockaddr_in *) uaddr;

	if (sk->state != TCP_CLOSE) 
		return(-EISCONN);

	/* Don't allow a double connect. */
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
			       RT_TOS(sk->ip_tos)|(sk->localroute || 0), sk->bound_dev_if);
	if (tmp < 0)
		return tmp;

	if (rt->rt_flags&(RTCF_MULTICAST|RTCF_BROADCAST)) {
		ip_rt_put(rt);
		return -ENETUNREACH;
	}

	if (!tcp_unique_address(rt->rt_src, sk->num, rt->rt_dst,
				usin->sin_port)) {
		ip_rt_put(rt);
		return -EADDRNOTAVAIL;
	}

	lock_sock(sk);

	/* Do this early, so there is less state to unwind on failure. */
	buff = sock_wmalloc(sk, MAX_SYN_SIZE, 0, GFP_KERNEL);
	if (buff == NULL) {
		release_sock(sk);
		ip_rt_put(rt);
		return(-ENOBUFS);
	}

	sk->dst_cache = &rt->u.dst;
	sk->daddr = rt->rt_dst;
	if (!sk->saddr)
		sk->saddr = rt->rt_src;
	sk->rcv_saddr = sk->saddr;

	if (sk->priority == 0)
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
	
	/* Put in the IP header and routing stuff. */
	tmp = ip_build_header(buff, sk);
	if (tmp < 0) {
		/* Caller has done ip_rt_put(rt) and set sk->dst_cache
		 * to NULL.  We must unwind the half built TCP socket
		 * state so that this failure does not create a "stillborn"
		 * sock (ie. future re-tries of connect() would fail).
		 */
		sk->daddr = 0;
		sk->saddr = sk->rcv_saddr = 0;
		kfree_skb(buff, FREE_WRITE);
		release_sock(sk);
		return(-ENETUNREACH);
	}

	/* No failure conditions can result past this point. */

	th = (struct tcphdr *) skb_put(buff,sizeof(struct tcphdr));
	buff->h.th = th;

	memcpy(th,(void *)&(sk->dummy_th), sizeof(*th));
	buff->seq = sk->write_seq++;
	th->seq = htonl(buff->seq);
	tp->snd_nxt = sk->write_seq;
	buff->end_seq = sk->write_seq;
	th->ack = 0;
	th->syn = 1;

	sk->mtu = rt->u.dst.pmtu;
	if ((sk->ip_pmtudisc == IP_PMTUDISC_DONT ||
	     (sk->ip_pmtudisc == IP_PMTUDISC_WANT &&
	      rt->rt_flags&RTCF_NOPMTUDISC)) &&
	    rt->u.dst.pmtu > 576)
		sk->mtu = 576;

	if(sk->mtu < 64)
		sk->mtu = 64;	/* Sanity limit */

	if (sk->user_mss)
		sk->mss = sk->user_mss;
	else
		sk->mss = (sk->mtu - sizeof(struct iphdr) -
			   sizeof(struct tcphdr));

	if (sk->mss < 1) {
		printk(KERN_DEBUG "intial sk->mss below 1\n");
		sk->mss = 1;	/* Sanity limit */
	}

	tp->window_clamp = rt->u.dst.window;
	tcp_select_initial_window(sock_rspace(sk)/2,sk->mss,
		&tp->rcv_wnd,
		&tp->window_clamp,
		sysctl_tcp_window_scaling,
		&tp->rcv_wscale);
	th->window = htons(tp->rcv_wnd);

	tmp = tcp_syn_build_options(buff, sk->mss, sysctl_tcp_sack,
		sysctl_tcp_timestamps,
		sysctl_tcp_window_scaling,tp->rcv_wscale);
	buff->csum = 0;
	th->doff = (sizeof(*th)+ tmp)>>2;

	tcp_v4_send_check(sk, th, sizeof(struct tcphdr) + tmp, buff);

	tcp_set_state(sk,TCP_SYN_SENT);

	/* Socket identity change complete, no longer
	 * in TCP_CLOSE, so rehash.
	 */
	tcp_v4_rehash(sk);

	tp->rto = rt->u.dst.rtt;

	tcp_init_xmit_timers(sk);

	/* Now works the right way instead of a hacked initial setting. */
	tp->retransmits = 0;

	skb_queue_tail(&sk->write_queue, buff);

	tp->packets_out++;
	buff->when = jiffies;

	ip_queue_xmit(skb_clone(buff, GFP_KERNEL));

	/* Timer for repeating the SYN until an answer. */
	tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
	tcp_statistics.TcpActiveOpens++;
	tcp_statistics.TcpOutSegs++;
  
	release_sock(sk);
	return(0);
}

static int tcp_v4_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	int retval = -EINVAL;

	/* Do sanity checking for sendmsg/sendto/send. */
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
 * Do a linear search in the socket open_request list. 
 * This should be replaced with a global hash table.
 */
static struct open_request *tcp_v4_search_req(struct tcp_opt *tp, 
					      struct iphdr *iph,
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
		if (req->af.v4_req.rmt_addr == iph->saddr &&
		    req->af.v4_req.loc_addr == iph->daddr &&
		    req->rmt_port == rport) {
			*prevp = prev; 
			return req; 
		}
		prev = req; 
	}
	return NULL; 
}


/* 
 * This routine does path mtu discovery as defined in RFC1197.
 */
static inline void do_pmtu_discovery(struct sock *sk, struct iphdr *ip)
{
	int new_mtu; 
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	/* Don't interested in TCP_LISTEN and open_requests (SYN-ACKs
	 * send out by Linux are always <576bytes so they should go through
	 * unfragmented).
	 */
	if (sk->state == TCP_LISTEN)
		return; 

	/* We don't check in the destentry if pmtu discovery is forbidden
	 * on this route. We just assume that no packet_to_big packets
	 * are send back when pmtu discovery is not active.
     	 * There is a small race when the user changes this flag in the
	 * route, but I think that's acceptable.
	 */
	if (sk->ip_pmtudisc != IP_PMTUDISC_DONT && sk->dst_cache) {
		new_mtu = sk->dst_cache->pmtu - 
			(ip->ihl<<2) - tp->tcp_header_len; 
		if (new_mtu < sk->mss && new_mtu > 0) {
			sk->mss = new_mtu;
			/* Resend the TCP packet because it's  
			 * clear that the old packet has been
			 * dropped. This is the new "fast" path mtu
			 * discovery.
			 */
			if (!sk->sock_readers)
				tcp_simple_retransmit(sk);
		}
	}
}

/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.  After adjustment
 * header points to the first 8 bytes of the tcp header.  We need
 * to find the appropriate port.
 */

void tcp_v4_err(struct sk_buff *skb, unsigned char *dp, int len)
{
	struct iphdr *iph = (struct iphdr*)dp;
	struct tcphdr *th; 
	struct tcp_opt *tp;
	int type = skb->h.icmph->type;
	int code = skb->h.icmph->code;
	struct sock *sk;
	int opening;
#ifdef ICMP_PARANOIA
	__u32 seq;
#endif

	if (len < (iph->ihl << 2) + ICMP_MIN_LENGTH) { 
		icmp_statistics.IcmpInErrors++; 
		return;
	}

	th = (struct tcphdr*)(dp+(iph->ihl<<2));

	sk = tcp_v4_lookup(iph->daddr, th->dest, iph->saddr, th->source, skb->dev->ifindex);
	if (sk == NULL) {
		icmp_statistics.IcmpInErrors++;
		return; 
	}

	tp = &sk->tp_pinfo.af_tcp;
#ifdef ICMP_PARANOIA
	seq = ntohl(th->seq);
	if (sk->state != TCP_LISTEN && 
   	    !between(seq, tp->snd_una, max(tp->snd_una+32768,tp->snd_nxt))) {
		if (net_ratelimit()) 
			printk(KERN_DEBUG "icmp packet outside the tcp window:"
					  " s:%d %u,%u,%u\n",
			       (int)sk->state, seq, tp->snd_una, tp->snd_nxt); 
		return; 
	}
#endif

	switch (type) {
	case ICMP_SOURCE_QUENCH:
		tp->snd_ssthresh = max(tp->snd_cwnd >> 1, 2);
		tp->snd_cwnd = tp->snd_ssthresh;
		tp->high_seq = tp->snd_nxt;
		return;
	case ICMP_PARAMETERPROB:
		sk->err=EPROTO;
		sk->error_report(sk);
		break; 
	case ICMP_DEST_UNREACH:
		if (code == ICMP_FRAG_NEEDED) { /* PMTU discovery (RFC1191) */
			do_pmtu_discovery(sk, iph); 
			return; 
		}
		break; 
	}

	/* If we've already connected we will keep trying
	 * until we time out, or the user gives up.
	 */
	if (code > NR_ICMP_UNREACH)
		return;
 
	opening = 0; 
	switch (sk->state) {
		struct open_request *req, *prev;
	case TCP_LISTEN:
		/* Prevent race conditions with accept() - 
		 * ICMP is unreliable. 
		 */
		if (sk->sock_readers) {
			/* XXX: add a counter here to profile this. 
			 * If too many ICMPs get dropped on busy
			 * servers this needs to be solved differently.
			 */
			return;
		}
		
		if (!th->syn && !th->ack)
			return;
		req = tcp_v4_search_req(tp, iph, th, &prev); 
		if (!req)
			return;
#ifdef ICMP_PARANOIA
		if (seq != req->snt_isn) {
			if (net_ratelimit())
				printk(KERN_DEBUG "icmp packet for openreq "
				       "with wrong seq number:%d:%d\n",
				       seq, req->snt_isn);
			return;
		}
#endif
		if (req->sk) {	/* not yet accept()ed */
			sk = req->sk; /* report error in accept */
		} else {
			tcp_synq_unlink(tp, req, prev);
			req->class->destructor(req);
			tcp_openreq_free(req);
		}
		/* FALL THOUGH */
	case TCP_SYN_SENT:
	case TCP_SYN_RECV: 
		opening = 1; 
		break;
	}
	
	if(icmp_err_convert[code].fatal || opening) {
		sk->err = icmp_err_convert[code].errno;
		if (opening) {
			tcp_statistics.TcpAttemptFails++;
			if (sk->state != TCP_LISTEN)
				tcp_set_state(sk,TCP_CLOSE);
			sk->error_report(sk);		/* Wake people up to see the error (see connect in sock.c) */
		}
	} else	/* Only an error on timeout */
		sk->err_soft = icmp_err_convert[code].errno;
}

/* This routine computes an IPv4 TCP checksum. */
void tcp_v4_send_check(struct sock *sk, struct tcphdr *th, int len, 
		       struct sk_buff *skb)
{
	th->check = 0;
	th->check = tcp_v4_check(th, len, sk->saddr, sk->daddr,
				 csum_partial((char *)th, th->doff<<2, skb->csum));
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

	/* Swap the send and the receive. */
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
	/* FIXME: should this carry an options packet? */
	ip_queue_xmit(skb1);
	tcp_statistics.TcpOutSegs++;
	tcp_statistics.TcpOutRsts++;
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

	sk = tcp_v4_lookup(iph->saddr, th->source, iph->daddr, th->dest, skb->dev->ifindex);

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
	struct sk_buff * skb;
	struct tcphdr *th;
	int tmp;
	int mss;

	skb = sock_wmalloc(sk, MAX_SYN_SIZE, 1, GFP_ATOMIC);
	if (skb == NULL)
		return;

	if(ip_build_pkt(skb, sk, req->af.v4_req.loc_addr,
			req->af.v4_req.rmt_addr, req->af.v4_req.opt) < 0) {
		kfree_skb(skb, FREE_WRITE);
		return;
	}
	
	mss = (skb->dst->pmtu - sizeof(struct iphdr) - sizeof(struct tcphdr));
	if (sk->user_mss)
		mss = min(mss, sk->user_mss);
	skb->h.th = th = (struct tcphdr *) skb_put(skb, sizeof(struct tcphdr));

	/* Don't offer more than they did.
	 * This way we don't have to memorize who said what.
	 * FIXME: maybe this should be changed for better performance
	 * with syncookies.
	 */
	req->mss = min(mss, req->mss);

	if (req->mss < 1) {
		printk(KERN_DEBUG "initial req->mss below 1\n");
		req->mss = 1;
	}

	/* Yuck, make this header setup more efficient... -DaveM */
	memset(th, 0, sizeof(struct tcphdr));
	th->syn = 1;
	th->ack = 1;
	th->source = sk->dummy_th.source;
	th->dest = req->rmt_port;
	skb->seq = req->snt_isn;
	skb->end_seq = skb->seq + 1;
	th->seq = htonl(skb->seq);
	th->ack_seq = htonl(req->rcv_isn + 1);
	if (req->rcv_wnd == 0) { /* ignored for retransmitted syns */
		__u8 rcv_wscale; 
		/* Set this up on the first call only */
		req->window_clamp = skb->dst->window;
		tcp_select_initial_window(sock_rspace(sk)/2,req->mss,
			&req->rcv_wnd,
			&req->window_clamp,
			req->wscale_ok,
			&rcv_wscale);
		req->rcv_wscale = rcv_wscale; 
	}
	th->window = htons(req->rcv_wnd);

	/* XXX Partial csum of 4 byte quantity is itself! -DaveM
	 * Yes, but it's a bit harder to special case now. It's
	 * now computed inside the tcp_v4_send_check() to clean up
	 * updating the options fields in the mainline send code.
	 * If someone thinks this is really bad let me know and
	 * I'll try to do it a different way. -- erics
	 */

	tmp = tcp_syn_build_options(skb, req->mss, req->sack_ok, req->tstamp_ok,
		req->wscale_ok,req->rcv_wscale);
	skb->csum = 0;
	th->doff = (sizeof(*th) + tmp)>>2;
	th->check = tcp_v4_check(th, sizeof(*th) + tmp,
				 req->af.v4_req.loc_addr, req->af.v4_req.rmt_addr,
				 csum_partial((char *)th, sizeof(*th)+tmp, skb->csum));

	ip_queue_xmit(skb);
	tcp_statistics.TcpOutSegs++;
}

static void tcp_v4_or_free(struct open_request *req)
{
	if(!req->sk && req->af.v4_req.opt)
		kfree_s(req->af.v4_req.opt,
			sizeof(struct ip_options) + req->af.v4_req.opt->optlen);
}

static inline void syn_flood_warning(struct sk_buff *skb)
{
	static unsigned long warntime;
	
	if (jiffies - warntime > HZ*60) {
		warntime = jiffies;
		printk(KERN_INFO 
		       "possible SYN flooding on port %d. Sending cookies.\n",  
		       ntohs(skb->h.th->dest));
	}
}

int sysctl_max_syn_backlog = 1024; 
int sysctl_tcp_syn_taildrop = 1;

struct or_calltable or_ipv4 = {
	tcp_v4_send_synack,
	tcp_v4_or_free,
	tcp_v4_send_reset
};

#ifdef NEW_LISTEN
#define BACKLOG(sk) ((sk)->tp_pinfo.af_tcp.syn_backlog) /* lvalue! */
#define BACKLOGMAX(sk) sysctl_max_syn_backlog
#else
#define BACKLOG(sk) ((sk)->ack_backlog)
#define BACKLOGMAX(sk) ((sk)->max_ack_backlog)
#endif

int tcp_v4_conn_request(struct sock *sk, struct sk_buff *skb, void *ptr, 
						__u32 isn)
{
	struct ip_options *opt = (struct ip_options *) ptr;
	struct tcp_opt tp;
	struct open_request *req;
	struct tcphdr *th = skb->h.th;
	__u32 saddr = skb->nh.iph->saddr;
	__u32 daddr = skb->nh.iph->daddr;
#ifdef CONFIG_SYN_COOKIES
	int want_cookie = 0;
#else
#define want_cookie 0 /* Argh, why doesn't gcc optimize this :( */
#endif

	/* If the socket is dead, don't accept the connection.	*/
	if (sk->dead) 
		goto dead; 

	/* XXX: Check against a global syn pool counter. */
	if (BACKLOG(sk) > BACKLOGMAX(sk)) {
#ifdef CONFIG_SYN_COOKIES
		if (sysctl_tcp_syncookies) {
			syn_flood_warning(skb);
			want_cookie = 1; 
		} else 
#endif
		if (sysctl_tcp_syn_taildrop) {
			struct open_request *req;

			req = tcp_synq_unlink_tail(&sk->tp_pinfo.af_tcp);
			tcp_openreq_free(req);
			tcp_statistics.TcpAttemptFails++;
		} else {
			goto error;
		}
	} else { 
		if (isn == 0)
			isn = tcp_v4_init_sequence(sk, skb);
		BACKLOG(sk)++;
	}

	req = tcp_openreq_alloc();
	if (req == NULL) {
		if (!want_cookie) BACKLOG(sk)--;
		goto error;
	}

	req->rcv_wnd = 0;		/* So that tcp_send_synack() knows! */

	req->rcv_isn = skb->seq;
 	tp.tstamp_ok = tp.sack_ok = tp.wscale_ok = tp.snd_wscale = 0;
	tp.in_mss = 536;
	tcp_parse_options(th,&tp,want_cookie);
	if (tp.saw_tstamp)
		req->ts_recent = tp.rcv_tsval;
	req->mss = tp.in_mss;
	req->tstamp_ok = tp.tstamp_ok;
	req->sack_ok = tp.sack_ok;
	req->snd_wscale = tp.snd_wscale;
	req->wscale_ok = tp.wscale_ok;
	req->rmt_port = th->source;
	req->af.v4_req.loc_addr = daddr;
	req->af.v4_req.rmt_addr = saddr;

	/* Note that we ignore the isn passed from the TIME_WAIT
	 * state here. That's the price we pay for cookies.
	 */
	if (want_cookie)
		isn = cookie_v4_init_sequence(sk, skb, &req->mss);

	req->snt_isn = isn;

	/* IPv4 options */
	req->af.v4_req.opt = NULL;

	if (opt && opt->optlen) {
		int opt_size = sizeof(struct ip_options) + opt->optlen;

		req->af.v4_req.opt = kmalloc(opt_size, GFP_ATOMIC);
		if (req->af.v4_req.opt) {
			if (ip_options_echo(req->af.v4_req.opt, skb)) {
				kfree_s(req->af.v4_req.opt, opt_size);
				req->af.v4_req.opt = NULL;
			}
		}
	}
	req->class = &or_ipv4;
	req->retrans = 0;
	req->sk = NULL;

	tcp_v4_send_synack(sk, req);

	if (want_cookie) {
		if (req->af.v4_req.opt) 
			kfree(req->af.v4_req.opt); 
	   	tcp_openreq_free(req); 
	} else 	{
		req->expires = jiffies + TCP_TIMEOUT_INIT;
		tcp_inc_slow_timer(TCP_SLT_SYNACK);
		tcp_synq_queue(&sk->tp_pinfo.af_tcp, req);
	}

	sk->data_ready(sk, 0);
exit:
	return 0;

dead:
	SOCK_DEBUG(sk, "Reset on %p: Connect on dead socket.\n",sk);
	tcp_statistics.TcpAttemptFails++;
	return -ENOTCONN;
error:
	tcp_statistics.TcpAttemptFails++;
	goto exit;
}

struct sock * tcp_v4_syn_recv_sock(struct sock *sk, struct sk_buff *skb,
				   struct open_request *req,
				   struct dst_entry *dst)
{
	struct tcp_opt *newtp;
	struct sock *newsk;
	int snd_mss;

#ifdef NEW_LISTEN
	if (sk->ack_backlog > sk->max_ack_backlog)
		goto exit; /* head drop */
#endif
	newsk = sk_alloc(AF_INET, GFP_ATOMIC);
	if (!newsk) 
		goto exit;
#ifdef NEW_LISTEN
	sk->ack_backlog++;
#endif
	memcpy(newsk, sk, sizeof(*newsk));

	/* Or else we die! -DaveM */
	newsk->sklist_next = NULL;

	newsk->opt = req->af.v4_req.opt;

	skb_queue_head_init(&newsk->write_queue);
	skb_queue_head_init(&newsk->receive_queue);
	skb_queue_head_init(&newsk->out_of_order_queue);
	skb_queue_head_init(&newsk->error_queue);

	/* Unused */
	newtp = &(newsk->tp_pinfo.af_tcp);
	newtp->send_head = NULL;
	newtp->retrans_head = NULL;

	newtp->pending = 0;

	skb_queue_head_init(&newsk->back_log);

	newsk->prot->init(newsk);

	newtp->snd_cwnd_cnt = 0;
	newtp->backoff = 0;
	newsk->proc = 0;
	newsk->done = 0;
	newsk->pair = NULL;
	atomic_set(&newsk->wmem_alloc, 0);
	atomic_set(&newsk->rmem_alloc, 0);
	newsk->localroute = sk->localroute;

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

	newtp->last_ack_sent = newtp->rcv_nxt = req->rcv_isn + 1;
	newtp->rcv_wup = req->rcv_isn + 1;
	newsk->copied_seq = req->rcv_isn + 1;

	newsk->socket = NULL;

	newsk->daddr = req->af.v4_req.rmt_addr;
	newsk->saddr = req->af.v4_req.loc_addr;
	newsk->rcv_saddr = req->af.v4_req.loc_addr;

	/* options / mss / route_cache */
	if (dst == NULL) { 
		struct rtable *rt;
		
		if (ip_route_output(&rt,
				    newsk->opt && newsk->opt->srr ? 
				    newsk->opt->faddr : newsk->daddr,
				    newsk->saddr, newsk->ip_tos, 0)) {
			sk_free(newsk);
			return NULL;
		}
	        dst = &rt->u.dst;
	} 
	newsk->dst_cache = dst;
	
	snd_mss = dst->pmtu;

	/* FIXME: is mtu really the same as snd_mss? */
	newsk->mtu = snd_mss;
	/* FIXME: where does mtu get used after this? */
	/* sanity check */
	if (newsk->mtu < 64)
		newsk->mtu = 64;

	newtp->sack_ok = req->sack_ok;
	newtp->tstamp_ok = req->tstamp_ok;
	newtp->window_clamp = req->window_clamp;
	newtp->rcv_wnd = req->rcv_wnd;
	newtp->wscale_ok = req->wscale_ok;
	if (newtp->wscale_ok) {
		newtp->snd_wscale = req->snd_wscale;
		newtp->rcv_wscale = req->rcv_wscale;
	} else {
		newtp->snd_wscale = newtp->rcv_wscale = 0;
		newtp->window_clamp = min(newtp->window_clamp,65535);
	}
	if (newtp->tstamp_ok) {
		newtp->ts_recent = req->ts_recent;
		newtp->ts_recent_stamp = jiffies;
		newtp->tcp_header_len = sizeof(struct tcphdr) + 12;	/* FIXME: define constant! */
		newsk->dummy_th.doff += 3;
	} else {
		newtp->tcp_header_len = sizeof(struct tcphdr);
	}

	snd_mss -= sizeof(struct iphdr) + sizeof(struct tcphdr);
	if (sk->user_mss)
		snd_mss = min(snd_mss, sk->user_mss);

	/* Make sure our mtu is adjusted for headers. */
	newsk->mss = min(req->mss, snd_mss) + sizeof(struct tcphdr) - newtp->tcp_header_len;

	tcp_v4_hash(newsk);
	add_to_prot_sklist(newsk);
	return newsk;

exit:
	if (dst) 
		dst_release(dst);
	return NULL;
}

static void tcp_v4_rst_req(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct open_request *req, *prev;

	req = tcp_v4_search_req(tp,skb->nh.iph, skb->h.th, &prev);
	if (!req)
		return;
	/* Sequence number check required by RFC793 */
	if (before(skb->seq, req->snt_isn) || after(skb->seq, req->snt_isn+1))
		return;
	tcp_synq_unlink(tp, req, prev);
	req->class->destructor(req);
	tcp_openreq_free(req); 
}

/* Check for embryonic sockets (open_requests) We check packets with
 * only the SYN bit set against the open_request queue too: This
 * increases connection latency a bit, but is required to detect
 * retransmitted SYNs.  
 */
static inline struct sock *tcp_v4_hnd_req(struct sock *sk,struct sk_buff *skb)
{
	struct tcphdr *th = skb->h.th; 
	u32 flg = ((u32 *)th)[3]; 

	/* Check for RST */
	if (flg & __constant_htonl(0x00040000)) {
		tcp_v4_rst_req(sk, skb);
		return NULL;
	}

	/* Check for SYN|ACK */
	if (flg & __constant_htonl(0x00120000)) {
		struct open_request *req, *dummy; 
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

		/* Find possible connection requests. */
		req = tcp_v4_search_req(tp, skb->nh.iph, th, &dummy); 
		if (req) {
			sk = tcp_check_req(sk, skb, req);
		}
#ifdef CONFIG_SYN_COOKIES
		 else {
			sk = cookie_v4_check(sk, skb, &(IPCB(skb)->opt));
		 }
#endif
	}
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

	if (sk->state == TCP_ESTABLISHED) { /* Fast path */
		if (tcp_rcv_established(sk, skb, skb->h.th, skb->len))
			goto reset;
		release_sock(sk);
		return 0; 
	} 


	if (sk->state == TCP_LISTEN) { 
		struct sock *nsk;
		
		nsk = tcp_v4_hnd_req(sk, skb);
		if (!nsk) 
			goto discard;
		lock_sock(nsk);
		release_sock(sk);
		sk = nsk;
	}
	
	if (tcp_rcv_state_process(sk, skb, skb->h.th, 
				  &(IPCB(skb)->opt), skb->len))
		goto reset;
	release_sock(sk); 
	return 0;

reset:
	tcp_v4_send_reset(skb);
discard:
	kfree_skb(skb, FREE_READ);
	/* Be careful here. If this function gets more complicated and
	 * gcc suffers from register pressure on the x86, sk (in %ebx) 
	 * might be destroyed here. This current version compiles correctly,
	 * but you have been warned.
	 */
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

	if (skb->pkt_type!=PACKET_HOST)
		goto discard_it;

	th = skb->h.th;

	/* Pull up the IP header. */
	__skb_pull(skb, skb->h.raw - skb->data);

	/* Count it even if it's bad */
	tcp_statistics.TcpInSegs++;

	/* Try to use the device checksum if provided. */
	switch (skb->ip_summed) {
	case CHECKSUM_NONE:
		skb->csum = csum_partial((char *)th, len, 0);
	case CHECKSUM_HW:
		if (tcp_v4_check(th,len,skb->nh.iph->saddr,skb->nh.iph->daddr,skb->csum)) {
			printk(KERN_DEBUG "TCPv4 bad checksum from %d.%d.%d.%d:%04x to %d.%d.%d.%d:%04x, len=%d/%d/%d\n",
 			       NIPQUAD(skb->nh.iph->saddr), ntohs(th->source), NIPQUAD(skb->nh.iph->daddr),
			       ntohs(th->dest), len, skb->len, ntohs(skb->nh.iph->tot_len));
			tcp_statistics.TcpInErrs++;
			goto discard_it;
		}
	default:
		/* CHECKSUM_UNNECESSARY */
	}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
	if (IPCB(skb)->redirport)
		sk = tcp_v4_proxy_lookup(th->dest, skb->nh.iph->saddr, th->source,
					 skb->nh.iph->daddr, skb->dev,
					 IPCB(skb)->redirport, skb->dev->ifindex);
	else
#endif
	sk = __tcp_v4_lookup(th, skb->nh.iph->saddr, th->source,
			     skb->nh.iph->daddr, th->dest, skb->dev->ifindex);
	if (!sk)
		goto no_tcp_socket;
	if(!ipsec_sk_policy(sk,skb))
		goto discard_it;

	skb->seq = ntohl(th->seq);
	skb->end_seq = skb->seq + th->syn + th->fin + len - th->doff*4;
	skb->ack_seq = ntohl(th->ack_seq);

	skb->used = 0;

	if (!sk->sock_readers)
		return tcp_v4_do_rcv(sk, skb);

	__skb_queue_tail(&sk->back_log, skb);
	return 0;

no_tcp_socket:
	tcp_v4_send_reset(skb);

discard_it:
	/* Discard frame. */
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
        int want_rewrite = sysctl_ip_dynaddr && sk->state == TCP_SYN_SENT;

	/* Check route */

	rt = (struct rtable*)skb->dst;

	/* Force route checking if want_rewrite */
	if (want_rewrite) {
		int tmp;
		__u32 old_saddr = rt->rt_src;

		/* Query new route */
		tmp = ip_route_connect(&rt, rt->rt_dst, 0, 
					RT_TOS(sk->ip_tos)|(sk->localroute||0),
					sk->bound_dev_if);

		/* Only useful if different source addrs */
		if (tmp == 0 || rt->rt_src != old_saddr ) {
			dst_release(skb->dst);
			skb->dst = &rt->u.dst;
		} else {
			want_rewrite = 0;
			dst_release(&rt->u.dst);
		}
	} else 
	if (rt->u.dst.obsolete) {
		int err;
		err = ip_route_output(&rt, rt->rt_dst, rt->rt_src, rt->key.tos, rt->key.oif);
		if (err) {
			sk->err_soft=-err;
			sk->error_report(skb->sk);
			return -1;
		}
		dst_release(skb->dst);
		skb->dst = &rt->u.dst;
	}

	/* Discard the surplus MAC header. */
	skb_pull(skb, skb->nh.raw-skb->data);

	iph = skb->nh.iph;
	th = skb->h.th;
	size = skb->tail - skb->h.raw;

        if (want_rewrite) {
        	__u32 new_saddr = rt->rt_src;
                
                /*
                 *	Ouch!, this should not happen.
                 */
                if (!sk->saddr || !sk->rcv_saddr) {
                	printk(KERN_WARNING "tcp_v4_rebuild_header(): not valid sock addrs: saddr=%08lX rcv_saddr=%08lX\n",
			       ntohl(sk->saddr), 
			       ntohl(sk->rcv_saddr));
                        return 0;
                }

		/*
		 *	Maybe whe are in a skb chain loop and socket address has
		 *	yet been 'damaged'.
		 */

		if (new_saddr != sk->saddr) {
			if (sysctl_ip_dynaddr > 1) {
				printk(KERN_INFO "tcp_v4_rebuild_header(): shifting sk->saddr from %d.%d.%d.%d to %d.%d.%d.%d\n",
					NIPQUAD(sk->saddr), 
					NIPQUAD(new_saddr));
			}

			sk->saddr = new_saddr;
			sk->rcv_saddr = new_saddr;
			/* sk->prot->rehash(sk); */
			tcp_v4_rehash(sk);
		} 

		if (new_saddr != iph->saddr) {
			if (sysctl_ip_dynaddr > 1) {
				printk(KERN_INFO "tcp_v4_rebuild_header(): shifting iph->saddr from %d.%d.%d.%d to %d.%d.%d.%d\n",
					NIPQUAD(iph->saddr), 
					NIPQUAD(new_saddr));
			}

			iph->saddr = new_saddr;
			ip_send_check(iph);
		} 
        
        }

	return 0;
}

static struct sock * tcp_v4_get_sock(struct sk_buff *skb, struct tcphdr *th)
{
	return tcp_v4_lookup(skb->nh.iph->saddr, th->source,
			     skb->nh.iph->daddr, th->dest, skb->dev->ifindex);
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
	tcp_v4_get_sock,
	ip_setsockopt,
	ip_getsockopt,
	v4_addr2sockaddr,
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

	/* FIXME: tie this to sk->rcvbuf? (May be unnecessary) */
	/* tp->rcv_wnd = 8192; */
	tp->tstamp_ok = 0;
	tp->sack_ok = 0;
	tp->wscale_ok = 0;
	tp->in_mss = 536;
	tp->snd_wscale = 0;
	tp->sacks = 0;
	tp->saw_tstamp = 0;
	tp->syn_backlog = 0;

	/*
	 * See draft-stevens-tcpca-spec-01 for discussion of the
	 * initialization of these values.
	 */
	tp->snd_cwnd = 1;
	tp->snd_ssthresh = 0x7fffffff;	/* Infinity */

	sk->priority = 1;
	sk->state = TCP_CLOSE;

	sk->max_ack_backlog = SOMAXCONN;

	sk->mtu = 576;
	sk->mss = 536;

	/* Speed up by setting some standard state for the dummy_th. */
  	sk->dummy_th.ack=1;
  	sk->dummy_th.doff=sizeof(struct tcphdr)>>2;

	/* Init SYN queue. */
	tcp_synq_init(tp);

	sk->tp_pinfo.af_tcp.af_specific = &ipv4_specific;

	return 0;
}

static int tcp_v4_destroy_sock(struct sock *sk)
{
	struct sk_buff *skb;

	tcp_clear_xmit_timers(sk);

	if (sk->keepopen)
		tcp_dec_slow_timer(TCP_SLT_KEEPALIVE);

	/* Cleanup up the write buffer. */
  	while((skb = skb_dequeue(&sk->write_queue)) != NULL)
		kfree_skb(skb, FREE_WRITE);

	/* Cleans up our, hopefuly empty, out_of_order_queue. */
  	while((skb = skb_dequeue(&sk->out_of_order_queue)) != NULL)
		kfree_skb(skb, FREE_READ);

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
