/*
 *  Syncookies implementation for the Linux kernel
 *
 *  Copyright (C) 1997 Andi Kleen
 *  Based on ideas by D.J.Bernstein and Eric Schenk. 
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 * 
 *  $Id: syncookies.c,v 1.3 1997/09/16 17:16:21 freitag Exp $
 *
 *  Missing: IPv6 support. 
 *           Some counter so that the Administrator can see when the machine
 *           is under a syn flood attack.
 */

#include <linux/config.h>
#if defined(CONFIG_SYN_COOKIES) 
#include <linux/tcp.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <net/tcp.h>

extern int sysctl_tcp_syncookies;

static unsigned long tcp_lastsynq_overflow;

/* 
 * This table has to be sorted. Only 8 entries are allowed and the
 * last entry has to be duplicated.
 * XXX generate a better table.
 * Unresolved Issues: HIPPI with a 64k MSS is not well supported.
 */
static __u16 const msstab[] = {
	64,
	256,	
	512,	
	536,
	1024,	
	1440,	
	1460,	
	4312,
	4312 
};

static __u32 make_syncookie(struct sk_buff *skb,  __u32 counter, __u32 seq)
{
	__u32 z;

	z = secure_tcp_syn_cookie(skb->nh.iph->saddr, skb->nh.iph->daddr,
				  skb->h.th->source, skb->h.th->dest,
				  seq, 
				  counter);

#if 0
	printk(KERN_DEBUG 
	       "msc: z=%u,cnt=%u,seq=%u,sadr=%u,dadr=%u,sp=%u,dp=%u\n",
	       z,counter,seq,
	       skb->nh.iph->saddr,skb->nh.iph->daddr,
	       ntohs(skb->h.th->source), ntohs(skb->h.th->dest));
#endif

	return z;
}

/*
 * Generate a syncookie. 
 */
__u32 cookie_v4_init_sequence(struct sock *sk, struct sk_buff *skb, 
			      __u16 *mssp)
{
	int i; 
	__u32 isn; 
	const __u16 mss = *mssp, *w; 

	tcp_lastsynq_overflow = jiffies;

	isn = make_syncookie(skb, (jiffies/HZ) >> 6, ntohl(skb->h.th->seq));
	
	/* XXX sort msstab[] by probability? */
	w = msstab;
	for (i = 0; i < 8; i++) 
		if (mss >= *w && mss < *++w)
			goto found;
	i--;
found:
	*mssp = w[-1]; 

	isn |= i; 
	return isn; 
}

/* This value should be dependent on TCP_TIMEOUT_INIT and 
 * sysctl_tcp_retries1. It's a rather complicated formula 
 * (exponential backoff) to compute at runtime so it's currently hardcoded
 * here.
 */
#define COUNTER_TRIES 4

/*  
 * Check if a ack sequence number is a valid syncookie. 
 */
static inline int cookie_check(struct sk_buff *skb, __u32 cookie) 
{
	int mssind; 
	int i; 
	__u32 counter; 
	__u32 seq; 

  	if ((jiffies - tcp_lastsynq_overflow) > TCP_TIMEOUT_INIT
	    && tcp_lastsynq_overflow) 
		return 0; 

	mssind = cookie & 7;
	cookie &= ~7;

	counter = (jiffies/HZ)>>6; 
	seq = ntohl(skb->h.th->seq)-1; 
	for (i = 0; i < COUNTER_TRIES; i++)
	    if (make_syncookie(skb, counter-i, seq) == cookie)
		    return msstab[mssind];	

	return 0;
}

extern struct or_calltable or_ipv4;

static inline struct sock *
get_cookie_sock(struct sock *sk, struct sk_buff *skb, struct open_request *req,
		struct dst_entry *dst)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	sk = tp->af_specific->syn_recv_sock(sk, skb, req, dst);
	req->sk = sk; 
	
	/* Queue up for accept() */
	tcp_synq_queue(tp, req);
	
	return sk; 
}

struct sock *
cookie_v4_check(struct sock *sk, struct sk_buff *skb, struct ip_options *opt)
{
	__u32 cookie = ntohl(skb->h.th->ack_seq)-1; 
	struct open_request *req; 
	int mss; 
	struct rtable *rt; 
	__u8 rcv_wscale;

	if (!sysctl_tcp_syncookies)
		return sk;
	if (!skb->h.th->ack)
		return sk; 

	mss = cookie_check(skb, cookie);
	if (mss == 0) 
		return sk;

	req = tcp_openreq_alloc();
	if (req == NULL)
		return NULL;	

	req->rcv_isn = htonl(skb->h.th->seq)-1;
	req->snt_isn = cookie; 
	req->mss = mss;
 	req->rmt_port = skb->h.th->source;
	req->af.v4_req.loc_addr = skb->nh.iph->daddr;
	req->af.v4_req.rmt_addr = skb->nh.iph->saddr;
	req->class = &or_ipv4; /* for savety */

	/* We throwed the options of the initial SYN away, so we hope
	 * the ACK carries the same options again (see RFC1122 4.2.3.8)
	 */
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
	
	req->af.v4_req.opt = NULL;
	req->snd_wscale = req->rcv_wscale = req->tstamp_ok = 0;
	req->wscale_ok = 0; 
	req->expires = 0UL; 
	req->retrans = 0; 
	
	/*
	 * We need to lookup the route here to get at the correct
	 * window size. We should better make sure that the window size
	 * hasn't changed since we received the original syn, but I see
	 * no easy way to do this. 
	 */
	if (ip_route_output(&rt,
			    opt && 
			    opt->srr ? opt->faddr : req->af.v4_req.rmt_addr,
			    req->af.v4_req.loc_addr,
			    sk->ip_tos | RTO_CONN,
			    0)) { 
	    tcp_openreq_free(req);
	    return NULL; 
	}

	/* Try to redo what tcp_v4_send_synack did. */
	req->window_clamp = rt->u.dst.window;  
	tcp_select_initial_window(sock_rspace(sk)/2,req->mss,
				  &req->rcv_wnd, &req->window_clamp, 
				  0, &rcv_wscale);
	req->rcv_wscale = rcv_wscale; 

	return get_cookie_sock(sk, skb, req, &rt->u.dst);
}

#endif
