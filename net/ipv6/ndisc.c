/*
 *	Neighbour Discovery for IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *	Mike Shaver		<shaver@ingenia.com>
 *
 *	$Id: ndisc.c,v 1.14 1997/04/12 04:32:51 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Lars Fenneberg			:	fixed MTU setting on receipt
 *						of an RA.
 *
 *	Janos Farkas			:	kmalloc failure checks
 */

/* Set to 3 to get tracing... */
#define ND_DEBUG 2

#if ND_DEBUG >= 3
#define NDBG(x) printk x
#else
#define NDBG(x)
#endif

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/route.h>

#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>



#include <net/checksum.h>
#include <linux/proc_fs.h>

#define NCACHE_NUM_BUCKETS 32

static struct inode ndisc_inode;
static struct socket *ndisc_socket=&ndisc_inode.u.socket_i;

unsigned long nd_rand_seed = 152L;

struct ndisc_statistics nd_stats;

static struct neigh_table nd_tbl;

unsigned int	ndisc_hash(void *primary_key);
int		ndisc_eth_resolv(unsigned char *h_dest, struct sk_buff *skb);

static struct neigh_ops nd_neigh_ops = {
	ETH_P_IPV6,
	ndisc_hash,
	ndisc_eth_resolv,
	NULL
};

static struct timer_list ndisc_timer;
static struct timer_list ndisc_gc_timer;

/*
 *	Protocol variables
 */

unsigned long	nd_reachable_time		= RECHABLE_TIME;
int		nd_gc_interval			= 5 * HZ;

/* 
 *	garbage collection timeout must be greater than reachable time
 *	since tstamp is updated by reachable confirmations only.
 *	gc_staletime actually means the time after last confirmation
 *	*NOT* after the last time the entry was used.
 */

int	nd_gc_staletime			= 3 * RECHABLE_TIME;


static int  ndisc_event_timer(struct nd_neigh *ndn);

unsigned long ipv6_random(void)
{
	nd_rand_seed=nd_rand_seed*69069L+1;
        return nd_rand_seed^jiffies;
}

static __inline__ unsigned long rand_reach_time(void)
{
	unsigned long val;

	val = ipv6_random() % (MAX_RANDOM_FACTOR *
			       ipv6_config.nd_base_reachable_time);

	if (val < (MIN_RANDOM_FACTOR * ipv6_config.nd_base_reachable_time))
		val+= (MIN_RANDOM_FACTOR * ipv6_config.nd_base_reachable_time);

	return val;
}

unsigned int ndisc_hash(void *primary_key)
{
        struct in6_addr *addr = (struct in6_addr *) primary_key;
        __u32 hash_val;
        
	addr = (struct in6_addr *) primary_key;
	
        hash_val = addr->s6_addr32[2] ^ addr->s6_addr32[3];

        hash_val ^= hash_val >> 16;
        
        return (hash_val & (NCACHE_NUM_BUCKETS - 1));
}

static int ndisc_gc_func(struct neighbour *neigh, void *arg);

static void ndisc_periodic_timer(unsigned long arg)
{
	static unsigned long last_rand = 0;
	unsigned long now = jiffies;
	
	/*
	 *	periodicly compute ReachableTime from random function
	 */
	
	if ((now - last_rand) > REACH_RANDOM_INTERVAL) {
		last_rand = now;
		nd_reachable_time = rand_reach_time();
	}

	neigh_table_lock(&nd_tbl);

	start_bh_atomic();
	if (atomic_read(&nd_tbl.tbl_lock) == 1) {
		ntbl_walk_table(&nd_tbl, ndisc_gc_func, 0, 0, NULL);
		ndisc_gc_timer.expires = now + nd_gc_interval;
	} else {
#if ND_DEBUG >= 2
		printk(KERN_DEBUG "ndisc_gc delayed: table locked\n");
#endif
		ndisc_gc_timer.expires = now + HZ;
	}
	end_bh_atomic();
	
	neigh_table_unlock(&nd_tbl);
	
	add_timer(&ndisc_gc_timer);
}

static int ndisc_gc_func(struct neighbour *neigh, void *arg)
{
	struct nd_neigh *ndn = (struct nd_neigh *) neigh;
        unsigned long now = jiffies;

	if (atomic_read(&ndn->ndn_refcnt) == 0) {
		switch (ndn->ndn_nud_state) {
		
		case NUD_REACHABLE:
		case NUD_STALE:
			if (now - ndn->ndn_tstamp < nd_gc_staletime)
				break;
		case NUD_FAILED:
			return 1;
		default:
		};
	}
	return 0;
}

static __inline__ void ndisc_add_timer(struct nd_neigh *ndn, int timer)
{
	unsigned long now = jiffies;
	unsigned long tval = ~0UL;

	ndn->ndn_expires = now + timer;
	
	if (del_timer(&ndisc_timer))
		tval = ndisc_timer.expires;

	tval = min(tval, ndn->ndn_expires);

	ndisc_timer.expires = tval;
	add_timer(&ndisc_timer);
}
        
static void ndisc_del_timer(struct nd_neigh *ndn)
{
	unsigned long tval = ~0UL;
	unsigned long neigh_val;

	if (del_timer(&ndisc_timer))
		tval = ndisc_timer.expires;

	neigh_val = ndn->ndn_expires;
	ndn->ndn_expires = 0;

	if (tval == neigh_val) {
		int i;
		
		tval = ~0UL;

		neigh_table_lock(&nd_tbl);
		
		/* need to search the entire neighbour cache */
		for (i=0; i < nd_tbl.tbl_size; i++) {
			struct neighbour *neigh, *head;
			head = nd_tbl.hash_buckets[i];
				
			if ((neigh = head) == NULL)
				continue;

			do {
				struct nd_neigh *n;

				n = (struct nd_neigh *) neigh;

				if ((n->ndn_nud_state & NUD_IN_TIMER) &&
				     n->ndn_expires)
					tval = min(tval, n->ndn_expires);

				neigh = neigh->next;

			} while (neigh != head);
		}
		neigh_table_unlock(&nd_tbl);
	}

	if (tval == ~(0UL))
		return;

	ndisc_timer.expires = tval;
	add_timer(&ndisc_timer);
}

static int ndisc_forced_gc(struct neighbour *neigh, void *arg)
{
	struct nd_neigh *ndn = (struct nd_neigh *) neigh;

	if (atomic_read(&ndn->ndn_refcnt) == 0) {
		if (ndn->ndn_nud_state & NUD_IN_TIMER)
			ndisc_del_timer(ndn);
		
		return 1;
	}
	return 0;
}

static struct nd_neigh * ndisc_new_neigh(struct device *dev,
					 struct in6_addr *addr)
{
	struct nd_neigh *ndn;

	NDBG(("ndisc_new_neigh("));
	if(dev)
		NDBG(("%s,", dev->name));
	else
		NDBG(("[NULL],"));
	NDBG(("[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x]): ",
	      addr->s6_addr16[0], addr->s6_addr16[1], addr->s6_addr16[2],
	      addr->s6_addr16[3], addr->s6_addr16[4], addr->s6_addr16[5],
	      addr->s6_addr16[6], addr->s6_addr16[7]));

	ndn = (struct nd_neigh *) neigh_alloc(sizeof(struct nd_neigh),
					      &nd_neigh_ops);
	if (ndn == NULL) {

#if ND_DEBUG >= 2
		printk(KERN_DEBUG "neigh_alloc: out of memory\n");
#endif

		start_bh_atomic();
		if (atomic_read(&nd_tbl.tbl_lock) == 1) {
#if ND_DEBUG >= 2
			printk(KERN_DEBUG "ndisc_alloc: forcing gc\n");
#endif
			ntbl_walk_table(&nd_tbl, ndisc_forced_gc, 0, 0, NULL);
		}
		
		end_bh_atomic();
#if ND_DEBUG >= 1
		printk(KERN_DEBUG "ndisc_alloc failed\n");
#endif
		return NULL;
	}

	nd_stats.allocs++;

	ipv6_addr_copy(&ndn->ndn_addr, addr);
	ndn->ndn_plen = 128;
	ndn->ndn_type = ipv6_addr_type(addr);
	ndn->ndn_dev = dev;
	ndn->ndn_tstamp = jiffies;

	if ((ndn->ndn_type & IPV6_ADDR_MULTICAST)) {
		NDBG(("MULTICAST(NCF_NOARP) "));
		ndn->ndn_flags |= NCF_NOARP;
	}

	if (dev->type == ARPHRD_LOOPBACK || dev->type == ARPHRD_SIT) {
		NDBG(("%s(NCF_NOARP) ",
		      (dev->type==ARPHRD_LOOPBACK) ? "LOOPBACK" : "SIT"));
		ndn->ndn_flags |= NCF_NOARP;
	}

	neigh_insert(&nd_tbl, (struct neighbour *) ndn);
	NDBG(("returning ndn(%p)\n", ndn));
	return ndn;
}

/*
 *	Called when creating a new dest_cache entry for a given destination
 *	is likely that an entry for the refered gateway exists in cache
 *
 */

struct neighbour * ndisc_get_neigh(struct device *dev, struct in6_addr *addr)
{
	struct nd_neigh *neigh;

	/*
	 *	neighbour cache:
	 *	cached information about nexthop and addr resolution
	 */

	if (dev == NULL) {
#if ND_DEBUG >= 1
		printk(KERN_DEBUG "ndisc_get_neigh: NULL device\n");
#endif
		return NULL;
	}

	neigh_table_lock(&nd_tbl);

        neigh = (struct nd_neigh *) neigh_lookup(&nd_tbl, (void *) addr,
						 sizeof(struct in6_addr), dev);
	if (neigh == NULL) {
		neigh = ndisc_new_neigh(dev, addr);

		if (neigh == NULL)
			return NULL;
	}

	neigh_table_unlock(&nd_tbl);

	return neighbour_clone((struct neighbour *) neigh);
}

/*
 *	return values
 *	0 - Address Resolution succeded, send packet
 *	1 - Address Resolution unfinished / packet queued
 */

int ndisc_eth_resolv(unsigned char *h_dest, struct sk_buff *skb)
{
	struct nd_neigh *ndn = NULL;

	if (skb->dst)
		ndn = (struct nd_neigh *) skb->dst->neighbour;

	if (ndn == NULL) {
#if ND_DEBUG >= 2
		printk(KERN_DEBUG "ndisc_eth_resolv: nexthop is NULL\n");
#endif
		goto discard;
	}

	if ((ndn->ndn_type & IPV6_ADDR_MULTICAST)) {
		struct in6_addr *daddr;

		daddr = &skb->nh.ipv6h->daddr;
		ipv6_mc_map(daddr, h_dest);
		return 0;
	}

	switch (ndn->ndn_nud_state) {	
	case NUD_FAILED:
	case NUD_NONE:
		ndisc_event_send((struct neighbour *)ndn, skb);

	case NUD_INCOMPLETE:			
		if (skb_queue_len(&ndn->neigh.arp_queue) >= NDISC_QUEUE_LEN) {
			struct sk_buff *buff;
			
			buff = ndn->neigh.arp_queue.prev;
			skb_unlink(buff);
			dev_kfree_skb(buff, FREE_WRITE);
		}
		skb_queue_head(&ndn->neigh.arp_queue, skb);
		return 1;
	default:
		ndisc_event_send((struct neighbour *)ndn, skb);
	};

	if ((ndn->ndn_flags & NTF_COMPLETE) == 0) {
#if ND_DEBUG >=1
		/* This shouldn't happen */
		printk(KERN_DEBUG "ND: using incomplete entry\n");
#endif
	}
	memcpy(h_dest, ndn->ndn_ha, skb->dev->addr_len);
	return 0;

  discard:
	
	dev_kfree_skb(skb, FREE_WRITE);
	return 1;
}

/*
 *	Send a Neighbour Advertisement
 */

void ndisc_send_na(struct device *dev, struct nd_neigh *ndn,
		   struct in6_addr *daddr, struct in6_addr *solicited_addr,
		   int router, int solicited, int override, int inc_opt) 
{
        struct sock *sk = ndisc_socket->sk;
        struct nd_msg *msg;
        int len, opt_len;
        struct sk_buff *skb;
	int err;

	NDBG(("ndisc_send_na("));
	if(dev)
		NDBG(("%s,", dev->name));
	else
		NDBG(("[NULL]"));
	NDBG(("%p): ", ndn));
	if(daddr)
		NDBG(("daddr[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x] ",
		      daddr->s6_addr16[0], daddr->s6_addr16[1], daddr->s6_addr16[2],
		      daddr->s6_addr16[3], daddr->s6_addr16[4], daddr->s6_addr16[5],
		      daddr->s6_addr16[6], daddr->s6_addr16[7]));
	if(solicited_addr)
		NDBG(("solicit_addr[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x] ",
		      solicited_addr->s6_addr16[0], solicited_addr->s6_addr16[1],
		      solicited_addr->s6_addr16[2], solicited_addr->s6_addr16[3],
		      solicited_addr->s6_addr16[4], solicited_addr->s6_addr16[5],
		      solicited_addr->s6_addr16[6], solicited_addr->s6_addr16[7]));
	NDBG(("rtr(%d)sol(%d)ovr(%d)iopt(%d)\n", router, solicited, override, inc_opt));

	opt_len = ((dev->addr_len + 1) >> 3) + 1;
	len = sizeof(struct icmp6hdr) + sizeof(struct in6_addr);

#if ND_DEBUG >=1
	if (dev == NULL) {
		printk(KERN_DEBUG "send_na: null device\n");
		return;
	}
#endif
	if (inc_opt)
		len += opt_len << 3;

	skb = sock_alloc_send_skb(sk, MAX_HEADER + len + dev->hard_header_len + 15,
				  0, 0, &err);

	if (skb == NULL) {
		printk(KERN_DEBUG "send_na: alloc skb failed\n");
		return;
	}
	/*
	 *	build the MAC header
	 */
	
	if (dev->hard_header_len) {
		skb_reserve(skb, (dev->hard_header_len + 15) & ~15);
		if (dev->hard_header) {
			dev->hard_header(skb, dev, ETH_P_IPV6, ndn->ndn_ha,
					 NULL, len);
			skb->arp = 1;
		}
	}

	ip6_nd_hdr(sk, skb, dev, solicited_addr, daddr, IPPROTO_ICMPV6, len);

	msg = (struct nd_msg *) skb_put(skb, len);

        msg->icmph.icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT;
        msg->icmph.icmp6_code = 0;
        msg->icmph.icmp6_cksum = 0;

        msg->icmph.icmp6_unused = 0;
        msg->icmph.icmp6_router    = router;
        msg->icmph.icmp6_solicited = solicited;
        msg->icmph.icmp6_override  = override;

        /* Set the target address. */
	ipv6_addr_copy(&msg->target, solicited_addr);

	if (inc_opt) {
		/* Set the source link-layer address option. */
		msg->opt.opt_type = ND_OPT_TARGET_LL_ADDR;
		msg->opt.opt_len = opt_len;
		memcpy(msg->opt.link_addr, dev->dev_addr, dev->addr_len);

		if ((opt_len << 3) - (2 + dev->addr_len)) {
			memset(msg->opt.link_addr + dev->addr_len, 0,
			       (opt_len << 3) - (2 + dev->addr_len));
		}
	}

	/* checksum */
	msg->icmph.icmp6_cksum = csum_ipv6_magic(solicited_addr, daddr, len, 
						 IPPROTO_ICMPV6,
						 csum_partial((__u8 *) msg, 
							      len, 0));

	dev_queue_xmit(skb);
}        

void ndisc_send_ns(struct device *dev, struct neighbour *neigh,
		   struct in6_addr *solicit,
		   struct in6_addr *daddr, struct in6_addr *saddr) 
{
	unsigned char ha[MAX_ADDR_LEN];
        struct sock *sk = ndisc_socket->sk;
        struct sk_buff *skb;
        struct nd_msg *msg;
        int len, opt_len;	
	void *h_dest;
	int err;

	NDBG(("ndisc_send_ns(%s,%p): ", (dev ? dev->name : "[NULL]"), neigh));
	if(daddr)
		NDBG(("daddr[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x] ",
		      daddr->s6_addr16[0], daddr->s6_addr16[1], daddr->s6_addr16[2],
		      daddr->s6_addr16[3], daddr->s6_addr16[4], daddr->s6_addr16[5],
		      daddr->s6_addr16[6], daddr->s6_addr16[7]));
	if(saddr)
		NDBG(("saddr[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x] ",
		      saddr->s6_addr16[0], saddr->s6_addr16[1], saddr->s6_addr16[2],
		      saddr->s6_addr16[3], saddr->s6_addr16[4], saddr->s6_addr16[5],
		      saddr->s6_addr16[6], saddr->s6_addr16[7]));
	if(solicit)
		NDBG(("solicit[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x] ",
		      solicit->s6_addr16[0], solicit->s6_addr16[1],
		      solicit->s6_addr16[2], solicit->s6_addr16[3],
		      solicit->s6_addr16[4], solicit->s6_addr16[5],
		      solicit->s6_addr16[6], solicit->s6_addr16[7]));
	NDBG(("\n"));

	/* length of addr in 8 octet groups.*/
	opt_len = ((dev->addr_len + 1) >> 3) + 1;
	len = sizeof(struct icmp6hdr) + sizeof(struct in6_addr) +
                (opt_len << 3);

	skb = sock_alloc_send_skb(sk, MAX_HEADER + len + dev->hard_header_len + 15,
				  0, 0, &err);
	if (skb == NULL) {
#if ND_DEBUG >= 1
		printk(KERN_DEBUG "send_ns: alloc skb failed\n");
#endif
		return;
	}

	skb->pkt_type = PACKET_NDISC;

	if (saddr == NULL) {
		struct inet6_ifaddr *ifa;

		/* use link local address */
		ifa = ipv6_get_lladdr(dev);

		if (ifa)
			saddr = &ifa->addr;
	}

	if ((ipv6_addr_type(daddr) & IPV6_ADDR_MULTICAST)) {
		nd_stats.snt_probes_mcast++;
		ipv6_mc_map(daddr, ha);
		h_dest = ha;
	} else {
		if (neigh == NULL) {
#if ND_DEBUG >= 1
			printk(KERN_DEBUG "send_ns: ucast destination "
			       "with null neighbour\n");
#endif
			return;
		}
		h_dest = neigh->ha;
		nd_stats.snt_probes_ucast++;
	}

	if (dev->hard_header_len) {
		skb_reserve(skb, (dev->hard_header_len + 15) & ~15);
		if (dev->hard_header) {
			dev->hard_header(skb, dev, ETH_P_IPV6, h_dest, NULL,
					 len);
			skb->arp = 1;
		}
	}

	ip6_nd_hdr(sk, skb, dev, saddr, daddr, IPPROTO_ICMPV6, len);
	
	msg = (struct nd_msg *)skb_put(skb, len);
	msg->icmph.icmp6_type = NDISC_NEIGHBOUR_SOLICITATION;
	msg->icmph.icmp6_code = 0;
	msg->icmph.icmp6_cksum = 0;
	msg->icmph.icmp6_unused = 0;

	/* Set the target address. */
	ipv6_addr_copy(&msg->target, solicit);

	/* Set the source link-layer address option. */
	msg->opt.opt_type = ND_OPT_SOURCE_LL_ADDR;
	msg->opt.opt_len = opt_len;

	memcpy(msg->opt.link_addr, dev->dev_addr, dev->addr_len);

	if ((opt_len << 3) - (2 + dev->addr_len)) {
		memset(msg->opt.link_addr + dev->addr_len, 0,
		       (opt_len << 3) - (2 + dev->addr_len));
	}

	/* checksum */
	msg->icmph.icmp6_cksum = csum_ipv6_magic(&skb->nh.ipv6h->saddr,
						 daddr, len, 
						 IPPROTO_ICMPV6,
						 csum_partial((__u8 *) msg, 
							      len, 0));
	/* send it! */
	dev_queue_xmit(skb);
}

void ndisc_send_rs(struct device *dev, struct in6_addr *saddr,
		   struct in6_addr *daddr)
{
	struct sock *sk = ndisc_socket->sk;
        struct sk_buff *skb;
        struct icmp6hdr *hdr;
	__u8 * opt;
        int len, opt_len;
	int err;

	NDBG(("ndisc_send_rs(%s): ", (dev ? dev->name : "[NULL]")));
	if(daddr)
		NDBG(("daddr[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x] ",
		      daddr->s6_addr16[0], daddr->s6_addr16[1], daddr->s6_addr16[2],
		      daddr->s6_addr16[3], daddr->s6_addr16[4], daddr->s6_addr16[5],
		      daddr->s6_addr16[6], daddr->s6_addr16[7]));
	if(saddr)
		NDBG(("saddr[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x] ",
		      saddr->s6_addr16[0], saddr->s6_addr16[1], saddr->s6_addr16[2],
		      saddr->s6_addr16[3], saddr->s6_addr16[4], saddr->s6_addr16[5],
		      saddr->s6_addr16[6], saddr->s6_addr16[7]));
	NDBG(("\n"));

	/* length of addr in 8 octet groups.*/
	opt_len = ((dev->addr_len + 1) >> 3) + 1;
	len = sizeof(struct icmp6hdr) + (opt_len << 3);

        skb = sock_alloc_send_skb(sk, MAX_HEADER + len + dev->hard_header_len + 15,
				  0, 0, &err);
	if (skb == NULL) {
		printk(KERN_DEBUG "send_ns: alloc skb failed\n");
		return;
	}

	if (dev->hard_header_len) {
		skb_reserve(skb, (dev->hard_header_len + 15) & ~15);
		if (dev->hard_header) {
			unsigned char ha[MAX_ADDR_LEN];

			ipv6_mc_map(daddr, ha);
			dev->hard_header(skb, dev, ETH_P_IPV6, ha, NULL, len);
			skb->arp = 1;
		}
	}

	ip6_nd_hdr(sk, skb, dev, saddr, daddr, IPPROTO_ICMPV6, len);
	
        hdr = (struct icmp6hdr *) skb_put(skb, len);
        hdr->icmp6_type = NDISC_ROUTER_SOLICITATION;
        hdr->icmp6_code = 0;
        hdr->icmp6_cksum = 0;
        hdr->icmp6_unused = 0;

	opt = (u8*) (hdr + 1);

        /* Set the source link-layer address option. */
        opt[0] = ND_OPT_SOURCE_LL_ADDR;
        opt[1] = opt_len;

        memcpy(opt + 2, dev->dev_addr, dev->addr_len);

	if ((opt_len << 3) - (2 + dev->addr_len)) {
		memset(opt + 2 + dev->addr_len, 0,
		       (opt_len << 3) - (2 + dev->addr_len));
	}

	/* checksum */
	hdr->icmp6_cksum = csum_ipv6_magic(&skb->nh.ipv6h->saddr, daddr, len,
					   IPPROTO_ICMPV6,
					   csum_partial((__u8 *) hdr, len, 0));

	/* send it! */
	dev_queue_xmit(skb);
}
		   

static int ndisc_store_hwaddr(struct nd_neigh *ndn, __u8 *opt, int opt_len,
			      int option)
{
	while (*opt != option && opt_len) {
		int len;

		len = opt[1] << 3;
		
		if (len == 0)
		{
			printk(KERN_WARNING "nd: option has 0 len\n");
			return -EINVAL;
		}

		opt += len;
		opt_len -= len;
	}

	if (*opt == option) {
		memcpy(ndn->neigh.ha, opt + 2, ndn->ndn_dev->addr_len); 
		return 0;
	}

	return -EINVAL;
}

/* Called when a timer expires for a neighbour entry. */

static void ndisc_timer_handler(unsigned long arg) 
{
	unsigned long now = jiffies;
	unsigned long ntimer = ~0UL;
        int i;

	neigh_table_lock(&nd_tbl);
	
	for (i=0; i < nd_tbl.tbl_size; i++) {
		struct nd_neigh *ndn, *head;

		head = (struct nd_neigh *) nd_tbl.hash_buckets[i];

		if ((ndn = head) == NULL)
			continue;

		do {
                        if (ndn->ndn_nud_state & NUD_IN_TIMER) {
				unsigned long time;

				time = ndn->ndn_expires - now;

				if ((long) time <= 0)
					time = ndisc_event_timer(ndn);
				
				if (time)
					ntimer = min(ntimer, time);
			}
			ndn = (struct nd_neigh *) ndn->neigh.next;

		} while (ndn != head);
	}

	if (ntimer != (~0UL)) {
		ndisc_timer.expires = now + ntimer;
		add_timer(&ndisc_timer);
	}
	
	neigh_table_unlock(&nd_tbl);
}


static int ndisc_event_timer(struct nd_neigh *ndn)
{
	struct in6_addr *daddr;
	struct in6_addr *target;
	struct in6_addr mcaddr;
	struct device *dev;
	int max_probes;

	if (ndn->ndn_nud_state == NUD_DELAY)
		ndn->ndn_nud_state = NUD_PROBE;

	max_probes = (ndn->ndn_nud_state == NUD_PROBE ?
		      ipv6_config.nd_max_ucast_solicit:
		      ipv6_config.nd_max_mcast_solicit);

	if (ndn->ndn_probes == max_probes) {
		struct sk_buff *skb;

		ndn->ndn_nud_state = NUD_FAILED;
		ndn->ndn_flags &= ~NTF_COMPLETE;
		nd_stats.res_failed++;

		while((skb=skb_dequeue(&ndn->neigh.arp_queue))) {
			/*
			 *	"The sender MUST return an ICMP
			 *	 destination unreachable"
			 */
			icmpv6_send(skb, ICMPV6_DEST_UNREACH,
				    ICMPV6_ADDR_UNREACH, 0, ndn->ndn_dev);

			dev_kfree_skb(skb, FREE_WRITE);
		}
		return 0;
	}

	ndn->ndn_probes++;

	dev = ndn->ndn_dev;
	target = &ndn->ndn_addr;

	if (ndn->ndn_nud_state == NUD_INCOMPLETE) {
		addrconf_addr_solict_mult(&ndn->ndn_addr, &mcaddr);
		daddr = &mcaddr;
		ndn = NULL;
	} else {
		daddr = &ndn->ndn_addr;
	}

	ndisc_send_ns(dev, (struct neighbour *) ndn, target, daddr, NULL);

	return ipv6_config.nd_retrans_time;
}

void ndisc_event_send(struct neighbour *neigh, struct sk_buff *skb)
{
	struct nd_neigh *ndn = (struct nd_neigh *) neigh;
	struct in6_addr daddr;
	unsigned long now = jiffies;
	struct in6_addr *saddr = NULL;

	if ((ndn->ndn_flags & NCF_NOARP))
		return;

	switch (ndn->ndn_nud_state) {
	case NUD_FAILED:
		ndn->ndn_probes = 0;
	case NUD_NONE:
		if (skb && !skb->stamp.tv_sec) {
			/*
			 *	skb->stamp allows us to know if we are
			 *	originating the skb or forwarding it.
			 *	(it is set on netif_rx)
			 */
			saddr = &skb->nh.ipv6h->saddr;
		}

		ndn->ndn_nud_state = NUD_INCOMPLETE;
		addrconf_addr_solict_mult(&ndn->ndn_addr, &daddr);
		ndisc_send_ns(ndn->ndn_dev, NULL, &ndn->ndn_addr, &daddr,
			      saddr);
		ndisc_add_timer(ndn, ipv6_config.nd_retrans_time);

		break;

	case NUD_REACHABLE:
		if ((now - ndn->ndn_tstamp) < nd_reachable_time)
			break;

	case NUD_STALE:
		ndn->ndn_nud_state = NUD_DELAY;
		ndisc_add_timer(ndn, ipv6_config.nd_delay_probe_time);
	}
}

/*
 *	Received a neighbour announce
 */
void ndisc_event_na(struct nd_neigh *ndn, unsigned char *opt, int opt_len,
		    int solicited, int override)
{
	struct sk_buff *skb;

	NDBG(("ndisc_event_na(%p,%p,%d,%d,%d)\n", ndn, opt, opt_len,
	      solicited, override));

	if (ndn->ndn_nud_state == NUD_NONE)
		ndn->ndn_nud_state = NUD_INCOMPLETE;

	if (ndn->ndn_nud_state == NUD_INCOMPLETE || override) {
		if (opt_len == 0) {
			printk(KERN_DEBUG "no opt on NA\n");
		} else {
			/* Record hardware address. */
			ndn->ndn_flags |= NTF_COMPLETE;

			if (ndisc_store_hwaddr(ndn, opt, opt_len,
					       ND_OPT_TARGET_LL_ADDR)) {
#if ND_DEBUG >= 2
				printk(KERN_DEBUG
				       "event_na: invalid TARGET_LL_ADDR\n");
#endif
				ndn->ndn_flags &= ~NTF_COMPLETE;
				ndn->ndn_nud_state = NUD_NONE;
				return;
			}
		}
	}

	if (solicited || override || ndn->ndn_nud_state == NUD_INCOMPLETE) {
		ndn->ndn_probes = 0;
		ndn->ndn_tstamp = jiffies;

		if (ndn->ndn_nud_state & NUD_IN_TIMER)
			ndisc_del_timer(ndn);

		if (solicited)
			ndn->ndn_nud_state = NUD_REACHABLE;
		else
			ndn->ndn_nud_state = NUD_STALE;
	}
			
	while ((skb=skb_dequeue(&ndn->neigh.arp_queue)))
		dev_queue_xmit(skb);
}

static struct nd_neigh * ndisc_event_ns(struct in6_addr *saddr,
					struct sk_buff *skb)
{
	struct nd_neigh *ndn;
	u8 *opt;
	int len;

	NDBG(("ndisc_event_ns: "));
	if(saddr)
		NDBG(("saddr[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x] ",
		      saddr->s6_addr16[0], saddr->s6_addr16[1], saddr->s6_addr16[2],
		      saddr->s6_addr16[3], saddr->s6_addr16[4], saddr->s6_addr16[5],
		      saddr->s6_addr16[6], saddr->s6_addr16[7]));
	NDBG(("\n"));

	opt = skb->h.raw;
	opt += sizeof(struct icmp6hdr) + sizeof(struct in6_addr);

	len = skb->tail - opt;

	neigh_table_lock(&nd_tbl);
	
	ndn = (struct nd_neigh *) neigh_lookup(&nd_tbl, saddr,
					       sizeof(struct in6_addr),
					       skb->dev);

	if (ndn == NULL)
		ndn = ndisc_new_neigh(skb->dev, saddr);

       	neigh_table_unlock(&nd_tbl);

	if (ndn == NULL)
		return NULL;

	switch(ndn->ndn_nud_state) {
	case NUD_REACHABLE:
	case NUD_STALE:
	case NUD_DELAY:
		if (*opt != ND_OPT_SOURCE_LL_ADDR ||
		    len != ndn->ndn_dev->addr_len ||
		    memcmp(ndn->neigh.ha, opt + 2, len))
			break;

		if (ndn->ndn_nud_state & NUD_IN_TIMER)
			ndisc_del_timer(ndn);

		/* FALLTHROUGH */
	default:
		ndn->ndn_flags |= NTF_COMPLETE;
			
		if (ndisc_store_hwaddr(ndn, opt, len, ND_OPT_SOURCE_LL_ADDR)) {
#if ND_DEBUG >= 1
			printk(KERN_DEBUG
			       "event_ns: invalid SOURCE_LL_ADDR\n");
#endif

			ndn->ndn_flags &= ~NTF_COMPLETE;
			ndn->ndn_nud_state = NUD_NONE;
			return ndn;
		}

		ndn->ndn_nud_state = NUD_STALE;
		ndn->ndn_tstamp = jiffies;
		ndn->ndn_probes = 0;
	};

	return ndn;
}


static void ndisc_ll_addr_update(struct nd_neigh *ndn, u8* opt, int len,
				 int type)
{
	switch(ndn->ndn_nud_state) {
	case NUD_REACHABLE:
	case NUD_STALE:
	case NUD_DELAY:
		if (len == ndn->ndn_dev->addr_len &&
		    memcmp(ndn->neigh.ha, opt + 2, len) == 0)
			break;

		if (ndn->ndn_nud_state & NUD_IN_TIMER)
			ndisc_del_timer(ndn);
	default:
		ndn->ndn_flags |= NTF_COMPLETE;
		
		if (ndisc_store_hwaddr(ndn, opt, len, type)) {
#if ND_DEBUG >=1
			printk(KERN_DEBUG "NDISC: invalid LL_ADDR\n");
#endif
			ndn->ndn_flags &= ~NTF_COMPLETE;
			ndn->ndn_nud_state = NUD_NONE;
			break;
		}
		
		ndn->ndn_nud_state = NUD_STALE;
		ndn->ndn_tstamp = jiffies;
		ndn->ndn_probes = 0;
	};
}

static void ndisc_router_discovery(struct sk_buff *skb)
{
        struct ra_msg *ra_msg = (struct ra_msg *) skb->h.raw;
	struct nd_neigh *ndn;
	struct inet6_dev *in6_dev;
	struct rt6_info *rt;
	int lifetime;
	int optlen;

	__u8 * opt = (__u8 *)(ra_msg + 1);

	NDBG(("ndisc_router_discovery(%p)\n", skb));

	optlen = (skb->tail - skb->h.raw) - sizeof(struct ra_msg);

	if (skb->nh.ipv6h->hop_limit != 255) {
		printk(KERN_INFO
		       "NDISC: fake router advertisment received\n");
		return;
	}

	/*
	 *	set the RA_RECV flag in the interface
	 */

	in6_dev = ipv6_get_idev(skb->dev);
	if (in6_dev == NULL) {
		printk(KERN_DEBUG "RA: can't find in6 device\n");
		return;
	}
	
	if (in6_dev->if_flags & IF_RS_SENT) {
		/*
		 *	flag that an RA was received after an RS was sent
		 *	out on this interface.
		 */
		in6_dev->if_flags |= IF_RA_RCVD;
	}

	lifetime = ntohs(ra_msg->icmph.icmp6_rt_lifetime);

	rt = rt6_get_dflt_router(&skb->nh.ipv6h->saddr, skb->dev);

	if (rt && lifetime == 0) {
		ip6_del_rt(rt);
		rt = NULL;
	}

	if (rt == NULL && lifetime) {
#if ND_DEBUG >= 2
		printk(KERN_DEBUG "ndisc_rdisc: adding default router\n");
#endif

		rt = rt6_add_dflt_router(&skb->nh.ipv6h->saddr, skb->dev);

		if (rt == NULL) {
#if ND_DEBUG >= 1
			printk(KERN_DEBUG "route_add failed\n");
#endif
			return;
		}

		ndn = (struct nd_neigh *) rt->rt6i_nexthop;
		if (ndn == NULL) {
#if ND_DEBUG >= 1
			printk(KERN_DEBUG "nd: add default router: null "
			       "neighbour\n");
#endif
			return;
		}
		ndn->ndn_flags |= NCF_ROUTER;
	}

	if (rt)
		rt->rt6i_expires = jiffies + (HZ * lifetime);

	if (ra_msg->icmph.icmp6_hop_limit)
		ipv6_config.hop_limit = ra_msg->icmph.icmp6_hop_limit;

	/*
	 *	Update Reachable Time and Retrans Timer
	 */

	if (ra_msg->retrans_timer)
		ipv6_config.nd_retrans_time = ntohl(ra_msg->retrans_timer);

	if (ra_msg->reachable_time) {
		__u32 rtime = ntohl(ra_msg->reachable_time);

		if (rtime != ipv6_config.nd_base_reachable_time) {
			ipv6_config.nd_base_reachable_time = rtime;
			nd_gc_staletime	= 3 * rtime;
			nd_reachable_time = rand_reach_time();
		}
		
	}

	/*
	 *	Process options.
	 */

        while(optlen > 0) {
                int len;

                len = (opt[1] << 3);

		if (len == 0) {
			printk(KERN_DEBUG "RA: opt has 0 len\n");
			break;
		}

                switch(*opt) {
                case ND_OPT_SOURCE_LL_ADDR:

			if (rt == NULL)
				break;
			
			ndn = (struct nd_neigh *) rt->rt6i_nexthop;

			if (ndn)
				ndisc_ll_addr_update(ndn, opt, len,
						     ND_OPT_SOURCE_LL_ADDR);
			break;

                case ND_OPT_PREFIX_INFO:
			addrconf_prefix_rcv(skb->dev, opt, len);
                        break;

                case ND_OPT_MTU:
			if (rt) {
				int mtu;
				struct device *dev;
				
				mtu = htonl(*(__u32 *)(opt+4));
				dev = rt->rt6i_dev;

				if (dev == NULL)
					break;

				if (mtu < 576) {
					printk(KERN_DEBUG "NDISC: router "
					       "announcement with mtu = %d\n",
					       mtu);
					break;
				}

				if (dev->change_mtu)
					dev->change_mtu(dev, mtu);
				else
					dev->mtu = mtu;
			}
                        break;

		case ND_OPT_TARGET_LL_ADDR:
		case ND_OPT_REDIRECT_HDR:
			printk(KERN_DEBUG "got illegal option with RA");
			break;
		default:
			printk(KERN_DEBUG "unkown option in RA\n");
                };
                optlen -= len;
                opt += len;
        }
}

void ndisc_forwarding_on(void)
{

	/*
	 *	Forwarding was turned on.
	 */

	rt6_purge_dflt_routers(0);
}

void ndisc_forwarding_off(void)
{
	/*
	 *	Forwarding was turned off.
	 */
}

static void ndisc_redirect_rcv(struct sk_buff *skb)
{
	struct icmp6hdr *icmph;
	struct in6_addr *dest;
	struct in6_addr *target;	/* new first hop to destination */
	struct nd_neigh *ndn;
	struct rt6_info *rt;
	int on_link = 0;
	int optlen;
	u8 * opt;

	NDBG(("ndisc_redirect_rcv(%p)\n", skb));

	if (skb->nh.ipv6h->hop_limit != 255) {
		printk(KERN_WARNING
		       "NDISC: fake ICMP redirect received\n");
		return;
	}

	if (!(ipv6_addr_type(&skb->nh.ipv6h->saddr) & IPV6_ADDR_LINKLOCAL)) {
		printk(KERN_WARNING
		       "ICMP redirect: source address is not linklocal\n");
		return;
	}

	optlen = skb->tail - skb->h.raw;
	optlen -= sizeof(struct icmp6hdr) + 2 * sizeof(struct in6_addr);

	if (optlen < 0) {
		printk(KERN_WARNING "ICMP redirect: packet too small\n");
		return;
	}

	icmph = (struct icmp6hdr *) skb->h.raw;
	target = (struct in6_addr *) (icmph + 1);
	dest = target + 1;

	if (ipv6_addr_type(dest) & IPV6_ADDR_MULTICAST) {
		printk(KERN_WARNING "ICMP redirect for multicast addr\n");
		return;
	}

	if (ipv6_addr_cmp(dest, target) == 0) {
		on_link = 1;
	} else if (!(ipv6_addr_type(target) & IPV6_ADDR_LINKLOCAL)) {
		printk(KERN_WARNING
		       "ICMP redirect: target address is not linklocal\n");
		return;
	}

	/* passed validation tests */
	rt = rt6_redirect(dest, &skb->nh.ipv6h->saddr, target, skb->dev,
			  on_link);

	if (rt == NULL) {
		printk(KERN_WARNING "ICMP redirect: no route to host\n");
		return;
	}

	ndn = (struct nd_neigh *) rt->rt6i_nexthop;

	opt = (u8 *) (dest + 1);

	while (optlen > 0) {
		int len;

		len = (opt[1] << 3);

		if (*opt == ND_OPT_TARGET_LL_ADDR)
			ndisc_ll_addr_update(ndn, opt, len,
					     ND_OPT_TARGET_LL_ADDR);

		opt += len;
		optlen -= len;
	}
}

void ndisc_send_redirect(struct sk_buff *skb, struct neighbour *neigh,
			 struct in6_addr *target)
{
	struct sock *sk = ndisc_socket->sk;
	int len = sizeof(struct icmp6hdr) + 2 * sizeof(struct in6_addr);
	struct sk_buff *buff;
	struct nd_neigh *ndn = (struct nd_neigh *) neigh;
	struct inet6_ifaddr *ifp;
	struct icmp6hdr *icmph;
	struct in6_addr *addrp;
	struct device *dev;
	struct rt6_info *rt;
	int ta_len = 0;
	u8 *opt;
	int rd_len;
	int err;
	int hlen;

	dev = skb->dev;
	rt = rt6_lookup(&skb->nh.ipv6h->saddr, NULL, dev, 0);

	if (rt == NULL || rt->u.dst.error) {
#if ND_DEBUG >= 1
		printk(KERN_DEBUG "ndisc_send_redirect: hostunreach\n");
#endif
		return;
	}

	if (rt->rt6i_flags & RTF_GATEWAY) {
#if ND_DEBUG >= 1
		printk(KERN_DEBUG "ndisc_send_redirect: not a neighbour\n");
#endif
		return;
	}

	if (ndn->ndn_nud_state == NUD_REACHABLE) {
		ta_len  = ((dev->addr_len + 1) >> 3) + 1;
		len += (ta_len << 3);
	}

	rd_len = min(536 - len, ntohs(skb->nh.ipv6h->payload_len) + 8);
	rd_len &= ~0x7;
	len += rd_len;

	ifp = ipv6_get_lladdr(dev);

	if (ifp == NULL) {
#if ND_DEBUG >= 1
		printk(KERN_DEBUG "redirect: no link_local addr for dev\n");
#endif
		return;
	}

	buff = sock_alloc_send_skb(sk, MAX_HEADER + len + dev->hard_header_len + 15,
				   0, 0, &err);
	if (buff == NULL) {
#if ND_DEBUG >= 2
		printk(KERN_DEBUG "ndisc_send_redirect: alloc_skb failed\n");
#endif
		return;
	}
	
	hlen = 0;

	if (dev->hard_header_len) {
		skb_reserve(buff, (dev->hard_header_len + 15) & ~15);
		if (dev->hard_header) {
			dev->hard_header(buff, dev, ETH_P_IPV6, ndn->ndn_ha,
					 NULL, len);
			buff->arp = 1;
		}
	}
	
	ip6_nd_hdr(sk, buff, dev, &ifp->addr, &skb->nh.ipv6h->saddr,
		   IPPROTO_ICMPV6, len);

	icmph = (struct icmp6hdr *) skb_put(buff, len);

	memset(icmph, 0, sizeof(struct icmp6hdr));
	icmph->icmp6_type = NDISC_REDIRECT;

	/*
	 *	copy target and destination addresses
	 */

	addrp = (struct in6_addr *)(icmph + 1);
	ipv6_addr_copy(addrp, target);
	addrp++;
	ipv6_addr_copy(addrp, &skb->nh.ipv6h->daddr);
	
	opt = (u8*) (addrp + 1);
		
	/*
	 *	include target_address option
	 */

	if (ta_len) {
		int zb;
		
		*(opt++) = ND_OPT_TARGET_LL_ADDR;
		*(opt++) = ta_len;

		memcpy(opt, neigh->ha, neigh->dev->addr_len);
		opt += neigh->dev->addr_len;

		/* 
		 *	if link layer address doesn't end on a 8 byte
		 *	boundary memset(0) the remider
		 */

		zb = (neigh->dev->addr_len + 2) & 0x7; 
		if (zb) {
			int comp;

			comp = 8 - zb;
			memset(opt, 0, comp);
			opt += comp;
		}
	}

	/*
	 *	build redirect option and copy skb over to the new packet.
	 */

	memset(opt, 0, 8);	
	*(opt++) = ND_OPT_REDIRECT_HDR;
	*(opt++) = (rd_len >> 3);
	opt += 6;

	memcpy(opt, &skb->nh.ipv6h, rd_len - 8);
	
	icmph->icmp6_cksum = csum_ipv6_magic(&ifp->addr, &skb->nh.ipv6h->saddr,
					     len, IPPROTO_ICMPV6,
					     csum_partial((u8 *) icmph, len, 0));

	dev_queue_xmit(buff);
}

/* Called by upper layers to validate neighbour cache entries. */

void ndisc_validate(struct neighbour *neigh)
{
	struct nd_neigh *ndn = (struct nd_neigh *) neigh;

	if (neigh == NULL)
		return;

        if (ndn->ndn_nud_state == NUD_INCOMPLETE)
                return;

        if (ndn->ndn_nud_state == NUD_DELAY) 
                ndisc_del_timer(ndn);

        nd_stats.rcv_upper_conf++;
        ndn->ndn_nud_state = NUD_REACHABLE;
        ndn->ndn_tstamp = jiffies;
}

int ndisc_rcv(struct sk_buff *skb, struct device *dev,
	      struct in6_addr *saddr, struct in6_addr *daddr,
	      struct ipv6_options *opt, unsigned short len)
{
	struct nd_msg *msg = (struct nd_msg *) skb->h.raw;
	struct nd_neigh *ndn;
	struct inet6_ifaddr *ifp;

	NDBG(("ndisc_rcv(type=%d) ", msg->icmph.icmp6_type));
	switch (msg->icmph.icmp6_type) {
	case NDISC_NEIGHBOUR_SOLICITATION:
		NDBG(("NS "));
		if ((ifp = ipv6_chk_addr(&msg->target))) {
			int addr_type;

			if (ifp->flags & DAD_INCOMPLETE) {
				/*
				 *	DAD failed
				 */

				/* XXX Check if this came in over same interface
				 * XXX we just sent an NS from!  That is valid! -DaveM
				 */

				printk(KERN_DEBUG "%s: duplicate address\n",
				       ifp->idev->dev->name);
				del_timer(&ifp->timer);
				return 0;
			}

			addr_type = ipv6_addr_type(saddr);
			if (addr_type & IPV6_ADDR_UNICAST) {
				int inc;

				/* 
				 *	update / create cache entry
				 *	for the source adddress
				 */

				nd_stats.rcv_probes_ucast++;

				ndn = ndisc_event_ns(saddr, skb);

				if (ndn == NULL)
					return 0;

				inc = ipv6_addr_type(daddr);
				inc &= IPV6_ADDR_MULTICAST;

				ndisc_send_na(dev, ndn, saddr, &ifp->addr, 
					      ifp->idev->router, 1, inc, inc);
			} else {
#if ND_DEBUG >= 1
				/* FIXME */
				printk(KERN_DEBUG "ns: non unicast saddr\n");
#endif
			}
		}
		break;

	case NDISC_NEIGHBOUR_ADVERTISEMENT:
		NDBG(("NA "));
		neigh_table_lock(&nd_tbl);	       
		ndn = (struct nd_neigh *) 
			neigh_lookup(&nd_tbl, (void *) &msg->target,
				     sizeof(struct in6_addr), skb->dev);
		neigh_table_unlock(&nd_tbl);

		if (ndn) {
			if (ndn->ndn_flags & NCF_ROUTER) {
				if (msg->icmph.icmp6_router == 0) {
					/*
					 *	Change: router to host
					 */
#if 0					
					struct rt6_info *rt;
					rt = ndisc_get_dflt_router(skb->dev,
								   saddr);
					if (rt)
						ndisc_del_dflt_router(rt);
#endif
				}
			} else {
				if (msg->icmph.icmp6_router)
					ndn->ndn_flags |= NCF_ROUTER;
			}
			ndisc_event_na(ndn, (unsigned char *) &msg->opt,
				       skb->tail - (u8 *)&msg->opt /*opt_len*/,
				       msg->icmph.icmp6_solicited,
				       msg->icmph.icmp6_override);
		}
		break;

	};

	if (ipv6_config.forwarding == 0) {
		switch (msg->icmph.icmp6_type) {
		case NDISC_ROUTER_ADVERTISEMENT:
			NDBG(("RA "));
			if (ipv6_config.accept_ra)
				ndisc_router_discovery(skb);
			break;

		case NDISC_REDIRECT:
			NDBG(("REDIR "));
			if (ipv6_config.accept_redirects)
				ndisc_redirect_rcv(skb);
			break;
		};
	}

	return 0;
}

#ifdef CONFIG_PROC_FS
int ndisc_get_info(char *buffer, char **start, off_t offset, int length,
		   int dummy)
{
	unsigned long now = jiffies;
	int len = 0;
	int i;

	neigh_table_lock(&nd_tbl);

	for (i = 0; i < nd_tbl.tbl_size; i++) {
		struct neighbour *neigh, *head;
		head = nd_tbl.hash_buckets[i];
		
		if ((neigh = head) == NULL)
			continue;

		do {
			struct nd_neigh *ndn = (struct nd_neigh *) neigh;
			int j;

			for (j=0; j<16; j++) {
				sprintf(buffer + len, "%02x",
					ndn->ndn_addr.s6_addr[j]);
				len += 2;
			}

			len += sprintf(buffer + len,
				       " %02x %02x %02x %02x %08lx %08lx %08lx %04x %04x %04lx %8s ", i,
				       ndn->ndn_plen,
				       ndn->ndn_type,
				       ndn->ndn_nud_state,
				       ndn->ndn_expires ? ndn->ndn_expires - now : 0,
				       now - ndn->ndn_tstamp,
				       nd_reachable_time,
				       nd_gc_staletime,
				       atomic_read(&ndn->ndn_refcnt),
				       ndn->ndn_flags,
				       ndn->ndn_dev ? ndn->ndn_dev->name : "NULLDEV");

			if ((ndn->ndn_flags & NTF_COMPLETE)) {
				for (j=0; j< neigh->dev->addr_len; j++) {
					sprintf(buffer + len, "%02x",
						neigh->ha[j]);
					len += 2;
				}
			} else {
                                len += sprintf(buffer + len, "000000000000");
			}
			len += sprintf(buffer + len, "\n");
			
			neigh = neigh->next;
		} while (neigh != head);
	}

	neigh_table_unlock(&nd_tbl);
	
	*start = buffer + offset;

	len -= offset;

	if (len > length)
		len = length;
	return len;
}

struct proc_dir_entry ndisc_proc_entry =
{
        PROC_NET_NDISC, 5, "ndisc",
        S_IFREG | S_IRUGO, 1, 0, 0,
        0, NULL,
        &ndisc_get_info
};
#endif	/* CONFIG_PROC_FS */

void ndisc_init(struct net_proto_family *ops)
{
	struct sock *sk;
        int err;

        ndisc_inode.i_mode = S_IFSOCK;
        ndisc_inode.i_sock = 1;
        ndisc_inode.i_uid = 0;
        ndisc_inode.i_gid = 0;

        ndisc_socket->inode = &ndisc_inode;
        ndisc_socket->state = SS_UNCONNECTED;
        ndisc_socket->type=SOCK_RAW;

	if((err=ops->create(ndisc_socket, IPPROTO_ICMPV6))<0)
		printk(KERN_DEBUG 
		       "Failed to create the NDISC control socket.\n");

	MOD_DEC_USE_COUNT;

	sk = ndisc_socket->sk;
	sk->allocation = GFP_ATOMIC;
	sk->net_pinfo.af_inet6.hop_limit = 255;
	sk->net_pinfo.af_inet6.priority  = 15;
	sk->num = 256;

        /*
         * Initialize the neighbour table
         */
	
	neigh_table_init(&nd_tbl, &nd_neigh_ops, NCACHE_NUM_BUCKETS);
 
        /* General ND state machine timer. */
	init_timer(&ndisc_timer);
	ndisc_timer.function = ndisc_timer_handler;
	ndisc_timer.data = 0L;
	ndisc_timer.expires = 0L;

        /* ND GC timer */
        init_timer(&ndisc_gc_timer);
        ndisc_gc_timer.function = ndisc_periodic_timer;
        ndisc_gc_timer.data = 0L;
        ndisc_gc_timer.expires = jiffies + nd_gc_interval;

	add_timer(&ndisc_gc_timer);

#ifdef CONFIG_PROC_FS
	proc_net_register(&ndisc_proc_entry);
#endif
}

#ifdef MODULE
void ndisc_cleanup(void)
{
#ifdef CONFIG_PROC_FS
        proc_net_unregister(ndisc_proc_entry.low_ino);
#endif
	del_timer(&ndisc_gc_timer);
	del_timer(&ndisc_timer);
}
#endif
