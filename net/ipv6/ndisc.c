/*
 *	Neighbour Discovery for IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *	Mike Shaver		<shaver@ingenia.com>
 *
 *	$Id: ndisc.c,v 1.28 1996/10/11 16:03:06 roque Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Interface:
 *
 *	ndisc_lookup will be called from eth.c on dev->(re)build_header
 *
 *	ndisc_rcv
 *	ndisc_validate is called by higher layers when they know a neighbour
 *		       is reachable.
 *
 *	Manages neighbour cache
 *
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/config.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/ndisc.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>


#include <net/checksum.h>
#include <linux/proc_fs.h>

#define NCACHE_NUM_BUCKETS 32

static struct socket ndisc_socket;

unsigned long nd_rand_seed = 152L;

struct ndisc_statistics nd_stats;

static struct neighbour *neighbours[NCACHE_NUM_BUCKETS];
static struct timer_list ndisc_timer;
static struct timer_list ndisc_gc_timer;

static atomic_t	ndisc_lock = 0;

/*
 *	Protocol variables
 */

int	nd_max_multicast_solicit	= 3;
int	nd_max_unicast_solicit		= 3;
int	nd_retrans_timer		= RETRANS_TIMER;
int	nd_reachable_time		= RECHABLE_TIME;
int	nd_base_reachable_time		= RECHABLE_TIME;
int	nd_delay_first_probe		= 5 * HZ;
int	nd_gc_interval			= 5 * HZ;

/* 
 *	garbage collection timeout must be greater than reachable time
 *	since tstamp is updated by reachable confirmations only.
 *	gc_staletime actually means the time after last confirmation
 *	*NOT* after the last time the entry was used.
 */

int	nd_gc_staletime			= 3 * RECHABLE_TIME;

static struct neighbour ndisc_insert_queue = {
	{{{0,}}}, 0, 0, NULL, 0,
	{0,}, NULL, {0,}, 0, 0, 0, 0, 0,
	&ndisc_insert_queue,
	&ndisc_insert_queue
};

static int ndisc_ins_queue_len = 0;

int  ndisc_event_timer(struct neighbour *neigh);

static void ndisc_bh_insert(void);

int ipv6_random(void)
{
	nd_rand_seed=nd_rand_seed*69069L+1;
        return nd_rand_seed^jiffies;
}

static __inline__ unsigned long rand_reach_time(void)
{
	unsigned long val;

	val = ipv6_random() % (MAX_RANDOM_FACTOR * nd_base_reachable_time);
	if (val < (MIN_RANDOM_FACTOR * nd_base_reachable_time))
	{
		val += (MIN_RANDOM_FACTOR * nd_base_reachable_time);
	}

	return val;
}

void ndisc_verify_reachability(struct neighbour * neigh);

/*
 *	(inline) support functions
 */

static __inline__ __u32 ndisc_hash(struct in6_addr *addr)
{
        
        __u32 hash_val;
        
        hash_val = addr->s6_addr32[2] ^ addr->s6_addr32[3];

        hash_val ^= hash_val >> 16;
        
        return (hash_val & (NCACHE_NUM_BUCKETS - 1));
}


static __inline__ void ndisc_neigh_queue(struct neighbour *neigh)
{
	struct neighbour *next = &ndisc_insert_queue;

	ndisc_ins_queue_len++;

	neigh->prev = next->prev;
	neigh->prev->next = neigh;
	next->prev = neigh;
	neigh->next = next;
}

static __inline__ struct neighbour * ndisc_dequeue(void)
{
	struct neighbour *next = &ndisc_insert_queue;
	struct neighbour *head;

	ndisc_ins_queue_len--;

	head = next->next;

	if (head == next)
	{
		return NULL;
	}

	head->next->prev = head->prev;
	next->next = head->next;

	head->next = NULL;
	head->prev = NULL;

	return head;
}

static __inline__ void ndisc_release_lock(void)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	ndisc_lock--;

	if (ndisc_lock == 0 && ndisc_ins_queue_len)
	{
		ndisc_bh_insert();
	}

	restore_flags(flags);
}

static void ndisc_insert_neigh(struct neighbour *neigh)
{

        struct neighbour * bucket;
        __u32 hash_val = ndisc_hash(&neigh->addr);
        
        bucket = neighbours[hash_val];
        
        if (!bucket)
	{
                neighbours[hash_val] = neigh;
                return;
        }

        for (; bucket->next; bucket = bucket->next)
		;
	        
        bucket->next = neigh;
	neigh->prev = bucket;
}

static __inline__ struct neighbour * 
ndisc_retrieve_neigh(struct device *dev, struct in6_addr *addr)
{

        struct neighbour * iter;
        iter = neighbours[ndisc_hash(addr)];

        for (; iter; iter = iter->next)
	{
		if (dev == iter->dev && ipv6_addr_cmp(addr, &iter->addr) == 0)
			return iter;
	}
        return NULL;
}

static void ndisc_unlink_neigh(struct neighbour * neigh) 
{
	if (neigh->prev)
		neigh->prev->next = neigh->next;
	else
	{
		int hash = ndisc_hash(&neigh->addr);
		neighbours[hash] = neigh->next;
	}

	if (neigh->next)
		neigh->next->prev = neigh->prev;
}

static void ndisc_release_neigh(struct neighbour * neigh)
{
	struct sk_buff *skb;

	while((skb=skb_dequeue(&neigh->arp_queue)))
	{
		dev_kfree_skb(skb, FREE_WRITE);
	}

	if (neigh->refcnt == 0)
	{
		ndisc_unlink_neigh(neigh);
		kfree(neigh);
	}
}

static void ndisc_bh_insert(void)
{
	struct neighbour *neigh;

	while((neigh = ndisc_dequeue()))
	{
		ndisc_insert_neigh(neigh);
	}
}


static void ndisc_garbage_collect(unsigned long arg)
{
        struct neighbour * neigh;
	static unsigned long last_rand = 0;
        unsigned long now = jiffies;
	unsigned long flags;
        int i = 0;


	/*
	 *	periodicly compute ReachableTime from random function
	 */
	if (now - last_rand > REACH_RANDOM_INTERVAL)
	{
		last_rand = now;
		nd_reachable_time = rand_reach_time();
	}

	save_flags(flags);
	cli();

	if (ndisc_lock)
	{
		restore_flags(flags);
		ndisc_gc_timer.expires = now + HZ;
		add_timer(&ndisc_gc_timer);
		return;
	}
		
        for (; i < NCACHE_NUM_BUCKETS; i++)
                for (neigh = neighbours[i]; neigh;)
		{
                        /*
			 *	Release unused entries
			 */
                        if (neigh->refcnt == 0 &&
			    ((neigh->nud_state == NUD_FAILED) ||
			     ((neigh->nud_state == NUD_REACHABLE) &&
			      (neigh->tstamp <= (now - nd_gc_staletime))
			      )
			     )
			    )
			{
				struct neighbour *prev;
				
				prev = neigh;
				neigh = neigh->next;
                                ndisc_release_neigh(prev);
				continue;
                        }
			neigh = neigh->next;
		}

	restore_flags(flags);

        ndisc_gc_timer.expires = now + nd_gc_interval;
        add_timer(&ndisc_gc_timer);
}

static __inline__ void ndisc_add_timer(struct neighbour *neigh, int timer)
{
	unsigned long now = jiffies;
	unsigned long tval;

	neigh->expires = now + timer;
	tval = del_timer(&ndisc_timer);

	if (tval)
	{
		tval = min(tval, neigh->expires);
	}
	else
		tval = neigh->expires;

	ndisc_timer.expires = tval;
	add_timer(&ndisc_timer);
}
        
static void ndisc_del_timer(struct neighbour *neigh)
{
	unsigned long tval;

	if (!(neigh->nud_state & NUD_IN_TIMER))
		return;

	tval = del_timer(&ndisc_timer);
	
	if (tval == neigh->expires)
	{
		int i;
		
		tval = ~0UL;

		/* need to search the entire neighbour cache */
		for (i=0; i < NCACHE_NUM_BUCKETS; i++)
		{
			for (neigh = neighbours[i]; neigh; neigh=neigh->next)
				if (neigh->nud_state & NUD_IN_TIMER)
				{
					tval = min(tval, neigh->expires);
				}
		}

	}

	if (tval == ~(0UL))
		return;

	ndisc_timer.expires = tval;
	add_timer(&ndisc_timer);
}

static struct neighbour * ndisc_new_neigh(struct device *dev,
					  struct in6_addr *addr)
{
	struct neighbour *neigh;
	unsigned long flags;

	neigh = (struct neighbour *) kmalloc(sizeof(struct neighbour),
					     GFP_ATOMIC);

	if (neigh == NULL)
	{
		printk(KERN_DEBUG "ndisc: kmalloc failure\n");
		return NULL;
	}

	nd_stats.allocs++;

	memset(neigh, 0, sizeof (struct neighbour));
	skb_queue_head_init(&neigh->arp_queue);

	ipv6_addr_copy(&neigh->addr, addr);
	neigh->len = 128;
	neigh->type = ipv6_addr_type(addr);
	neigh->dev = dev;
	neigh->tstamp = jiffies;

	if (dev->type == ARPHRD_LOOPBACK || dev->type == ARPHRD_SIT)
	{
		neigh->flags |= NCF_NOARP;
	}

	save_flags(flags);
	cli();

	if (ndisc_lock == 0)
	{
		/* Add to the cache. */
		ndisc_insert_neigh(neigh);
	}
	else
	{
		ndisc_neigh_queue(neigh);
	}

	restore_flags(flags);

	return neigh;
}

/*
 *	Called when creating a new dest_cache entry for a given destination
 *	is likely that an entry for the refered gateway exists in cache
 *
 */

struct neighbour * ndisc_get_neigh(struct device *dev, struct in6_addr *addr)
{
	struct neighbour *neigh;

	/*
	 *	neighbour cache:
	 *	cached information about nexthop and addr resolution
	 */

	if (dev == NULL)
	{
		printk(KERN_DEBUG "ncache_get_neigh: NULL device\n");
		return NULL;
	}

	atomic_inc(&ndisc_lock);

        neigh = ndisc_retrieve_neigh(dev, addr);

	ndisc_release_lock();

	if (neigh == NULL)
	{
		neigh = ndisc_new_neigh(dev, addr);
	}

	atomic_inc(&neigh->refcnt);
	
	return neigh;	
}

/*
 *	return values
 *	0 - Address Resolution succeded, send packet
 *	1 - Address Resolution unfinished / packet queued
 */

int ndisc_eth_resolv(unsigned char *h_dest, struct device *dev,
		     struct sk_buff *skb)
{
	struct neighbour *neigh;

	neigh = skb->nexthop;

	if (neigh == NULL)
	{
		int addr_type;

		addr_type = ipv6_addr_type(&skb->ipv6_hdr->daddr);
		
		if (addr_type & IPV6_ADDR_MULTICAST)
		{
			ipv6_mc_map(&skb->ipv6_hdr->daddr, h_dest);
			return 0;
		}

		printk(KERN_DEBUG "ndisc_eth_resolv: nexthop is NULL\n");
		goto discard;
	}

	if (skb->pkt_type == PACKET_NDISC)
		goto ndisc_pkt;
	
	switch (neigh->nud_state) {	
	case NUD_FAILED:
	case NUD_NONE:
		ndisc_event_send(neigh, skb);

	case NUD_INCOMPLETE:			
		if (skb_queue_len(&neigh->arp_queue) >= NDISC_QUEUE_LEN)
		{
			struct sk_buff *buff;
			
			buff = neigh->arp_queue.prev;
			skb_unlink(buff);
			dev_kfree_skb(buff, FREE_WRITE);
		}
		skb_queue_head(&neigh->arp_queue, skb);
		return 1;
	default:
		ndisc_event_send(neigh, skb);
	}

  ndisc_pkt:

	if (neigh->h_dest == NULL)
	{
		printk(KERN_DEBUG "neigh->h_dest is NULL\n");
		goto discard;
	}

	memcpy(h_dest, neigh->h_dest, dev->addr_len);

	if ((neigh->flags & NCF_HHVALID) == 0)
	{
		/*
		 * copy header to hh_data and move h_dest pointer
		 * this is strictly media dependent.
		 */
	}
	return 0;

  discard:
	
	dev_kfree_skb(skb, FREE_WRITE);
	return 1;
}


/* Send the actual Neighbour Advertisement */

void ndisc_send_na(struct device *dev, struct neighbour *neigh,
		   struct in6_addr *daddr,
		   struct in6_addr *solicited_addr,
		   int router, int solicited, int override, int inc_opt) 
{
        struct sock *sk = (struct sock *)ndisc_socket.data;
        struct nd_msg *msg;
        int len, opt_len;
        struct sk_buff *skb;
	int err;

	opt_len = ((dev->addr_len + 1) >> 3) + 1;
	len = sizeof(struct icmpv6hdr) + sizeof(struct in6_addr);

	if (inc_opt)
	{
		len += opt_len << 3;
	}

	skb = sock_alloc_send_skb(sk, MAX_HEADER + len, 0, 0, &err);

	if (skb == NULL)
	{
		printk(KERN_DEBUG "send_na: alloc skb failed\n");
	}

	skb->free=1;

	if (ipv6_bld_hdr_2(sk, skb, dev, neigh, solicited_addr, daddr,
			   IPPROTO_ICMPV6, len) < 0)
        {
		kfree_skb(skb, FREE_WRITE);
		printk(KERN_DEBUG 
		       "ndisc_send_na: ipv6_build_header returned < 0\n");
		return;
	}

	skb->pkt_type = PACKET_NDISC;
	
	msg = (struct nd_msg *) skb_put(skb, len);

        msg->icmph.type = NDISC_NEIGHBOUR_ADVERTISEMENT;
        msg->icmph.code = 0;
        msg->icmph.checksum = 0;

        msg->icmph.icmp6_unused = 0;
        msg->icmph.icmp6_router    = router;
        msg->icmph.icmp6_solicited = solicited;
        msg->icmph.icmp6_override  = override;

        /* Set the target address. */
	ipv6_addr_copy(&msg->target, solicited_addr);

	if (inc_opt)
	{
		/* Set the source link-layer address option. */
		msg->opt.opt_type = ND_OPT_TARGET_LL_ADDR;
		msg->opt.opt_len = opt_len;
		memcpy(msg->opt.link_addr, dev->dev_addr, dev->addr_len);

		if ((opt_len << 3) - (2 + dev->addr_len))
		{
			memset(msg->opt.link_addr + dev->addr_len, 0,
			       (opt_len << 3) - (2 + dev->addr_len));
		}
	}

	/* checksum */
	msg->icmph.checksum = csum_ipv6_magic(solicited_addr, daddr, len, 
					      IPPROTO_ICMPV6,
					      csum_partial((__u8 *) msg, 
							   len, 0));

	ipv6_queue_xmit(sk, skb->dev, skb, 1);
}        

void ndisc_send_ns(struct device *dev, struct neighbour *neigh,
		   struct in6_addr *solicit,
		   struct in6_addr *daddr, struct in6_addr *saddr) 
{
        struct sock *sk = (struct sock *) ndisc_socket.data;
        struct sk_buff *skb;
        struct nd_msg *msg;
        int len, opt_len;
	int err;

	/* length of addr in 8 octet groups.*/
	opt_len = ((dev->addr_len + 1) >> 3) + 1;
	len = sizeof(struct icmpv6hdr) + sizeof(struct in6_addr) +
                (opt_len << 3);

        skb = sock_alloc_send_skb(sk, MAX_HEADER + len, 0, 0, &err);
	if (skb == NULL)
	{
		printk(KERN_DEBUG "send_ns: alloc skb failed\n");
		return;
	}

	skb->free=1;
	skb->pkt_type = PACKET_NDISC;

	if (saddr == NULL)
	{
		struct inet6_ifaddr *ifa;

		/* use link local address */
		ifa = ipv6_get_lladdr(dev);

		if (ifa)
		{
			saddr = &ifa->addr;
		}
	}

        if(ipv6_addr_type(daddr) == IPV6_ADDR_MULTICAST)
	{
                nd_stats.snt_probes_mcast++;
	}
        else
	{
                nd_stats.snt_probes_ucast++;
	}

        if (ipv6_bld_hdr_2(sk, skb, dev, neigh, saddr, daddr, IPPROTO_ICMPV6,
			   len) < 0 )
	{
                kfree_skb(skb, FREE_WRITE);
                printk(KERN_DEBUG
                       "ndisc_send_ns: ipv6_build_header returned < 0\n");
                return;
        }
	
        msg = (struct nd_msg *)skb_put(skb, len);
        msg->icmph.type = NDISC_NEIGHBOUR_SOLICITATION;
        msg->icmph.code = 0;
        msg->icmph.checksum = 0;
        msg->icmph.icmp6_unused = 0;

        /* Set the target address. */
        ipv6_addr_copy(&msg->target, solicit);

        /* Set the source link-layer address option. */
        msg->opt.opt_type = ND_OPT_SOURCE_LL_ADDR;
        msg->opt.opt_len = opt_len;

        memcpy(msg->opt.link_addr, dev->dev_addr, dev->addr_len);

	if ((opt_len << 3) - (2 + dev->addr_len))
	{
		memset(msg->opt.link_addr + dev->addr_len, 0,
		       (opt_len << 3) - (2 + dev->addr_len));
	}

	/* checksum */
	msg->icmph.checksum = csum_ipv6_magic(&skb->ipv6_hdr->saddr,
					      daddr, len, 
					      IPPROTO_ICMPV6,
					      csum_partial((__u8 *) msg, 
							   len, 0));
	/* send it! */
	ipv6_queue_xmit(sk, skb->dev, skb, 1);
}

void ndisc_send_rs(struct device *dev, struct in6_addr *saddr,
		   struct in6_addr *daddr)
{
	struct sock *sk = (struct sock *) ndisc_socket.data;
        struct sk_buff *skb;
        struct icmpv6hdr *hdr;
	__u8 * opt;
        int len, opt_len;
	int err;

	/* length of addr in 8 octet groups.*/
	opt_len = ((dev->addr_len + 1) >> 3) + 1;
	len = sizeof(struct icmpv6hdr) + (opt_len << 3);

        skb = sock_alloc_send_skb(sk, MAX_HEADER + len, 0, 0, &err);
	if (skb == NULL)
	{
		printk(KERN_DEBUG "send_ns: alloc skb failed\n");
	}

	skb->free=1;

        if (ipv6_bld_hdr_2(sk, skb, dev, NULL, saddr, daddr, IPPROTO_ICMPV6,
			   len) < 0 )
	{
                kfree_skb(skb, FREE_WRITE);
                printk(KERN_DEBUG
                       "ndisc_send_ns: ipv6_build_header returned < 0\n");
                return;
        }
	
        hdr = (struct icmpv6hdr *) skb_put(skb, len);
        hdr->type = NDISC_ROUTER_SOLICITATION;
        hdr->code = 0;
        hdr->checksum = 0;
        hdr->icmp6_unused = 0;

	opt = (u8*) (hdr + 1);

        /* Set the source link-layer address option. */
        opt[0] = ND_OPT_SOURCE_LL_ADDR;
        opt[1] = opt_len;

        memcpy(opt + 2, dev->dev_addr, dev->addr_len);

	if ((opt_len << 3) - (2 + dev->addr_len))
	{
		memset(opt + 2 + dev->addr_len, 0,
		       (opt_len << 3) - (2 + dev->addr_len));
	}

	/* checksum */
	hdr->checksum = csum_ipv6_magic(&skb->ipv6_hdr->saddr, daddr, len,
					IPPROTO_ICMPV6,
					csum_partial((__u8 *) hdr, len, 0));

	/* send it! */
	ipv6_queue_xmit(sk, skb->dev, skb, 1);
}
		   

static int ndisc_store_hwaddr(struct device *dev, __u8 *opt, int opt_len,
			      __u8 *h_addr, int option)
{
	while (*opt != option && opt_len)
	{
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

	if (*opt == option)
	{
		memcpy(h_addr, opt + 2, dev->addr_len); 
		return 0;
	}

	return -EINVAL;
}

/* Called when a timer expires for a neighbour entry. */

static void ndisc_timer_handler(unsigned long arg) 
{
	unsigned long now = jiffies;
        struct neighbour * neigh;
	unsigned long ntimer = ~0UL;
        int i;

	atomic_inc(&ndisc_lock);

	for (i=0; i < NCACHE_NUM_BUCKETS; i++)
	{
                for (neigh = neighbours[i]; neigh;)
		{
                        if (neigh->nud_state & NUD_IN_TIMER)
			{
				int time;

				if (neigh->expires <= now)
				{
					time = ndisc_event_timer(neigh);
				}
				else
					time = neigh->expires - now;

				if (time == 0)
				{
					unsigned long flags;

					save_flags(flags);
					cli();

					if (ndisc_lock == 1)
					{
						struct neighbour *old = neigh;

						neigh = neigh->next;
						ndisc_release_neigh(old);
						restore_flags(flags);
						continue;
					}

					restore_flags(flags);
				}

				ntimer = min(ntimer, time);
			}
			neigh = neigh->next;
		}
	}

	if (ntimer != (~0UL))
	{
		ndisc_timer.expires = jiffies + ntimer;
		add_timer(&ndisc_timer);
	}
	ndisc_release_lock();
}


int ndisc_event_timer(struct neighbour *neigh)
{
	struct in6_addr *daddr;
	struct in6_addr *target;
	struct in6_addr mcaddr;
	struct device *dev;
	int max_probes;

	if (neigh->nud_state == NUD_DELAY)
	{
		neigh->nud_state = NUD_PROBE;
	}

	max_probes = (neigh->nud_state == NUD_PROBE ? nd_max_unicast_solicit:
		      nd_max_multicast_solicit);

	if (neigh->probes == max_probes)
	{
		struct sk_buff *skb;

		neigh->nud_state = NUD_FAILED;
		neigh->flags |= NCF_INVALID;
		nd_stats.res_failed++;

		while((skb=skb_dequeue(&neigh->arp_queue)))
		{
			/*
			 *	"The sender MUST return an ICMP
			 *	 destination unreachable"
			 */
			icmpv6_send(skb, ICMPV6_DEST_UNREACH,
				    ICMPV6_ADDR_UNREACH, 0, neigh->dev);

			dev_kfree_skb(skb, FREE_WRITE);
		}
		return 0;
	}
		
	neigh->probes++;

	dev = neigh->dev;
	target = &neigh->addr;

	if (neigh->nud_state == NUD_INCOMPLETE)
	{
		addrconf_addr_solict_mult(&neigh->addr, &mcaddr);
		daddr = &mcaddr;		
		neigh = NULL;
	}
	else
	{
		daddr = &neigh->addr;		
	}

	ndisc_send_ns(dev, neigh, target, daddr, NULL);

	return nd_retrans_timer;	
}

void ndisc_event_send(struct neighbour *neigh, struct sk_buff *skb)
{
	unsigned long now = jiffies;
	struct in6_addr daddr;
	struct in6_addr *saddr = NULL;

	switch (neigh->nud_state) {
	case NUD_FAILED:
		neigh->probes = 0;
	case NUD_NONE:

		if (skb && !skb->stamp.tv_sec)
		{
			/*
			 *	skb->stamp allows us to know if we are
			 *	originating the skb or forwarding it.
			 *	(it is set on netif_rx)
			 */
			saddr = &skb->ipv6_hdr->saddr;
		}

		neigh->nud_state = NUD_INCOMPLETE;
		addrconf_addr_solict_mult(&neigh->addr, &daddr);
		ndisc_send_ns(neigh->dev, NULL, &neigh->addr, &daddr, saddr);
		ndisc_add_timer(neigh, nd_retrans_timer);

		break;

	case NUD_REACHABLE:
		if (now - neigh->tstamp < nd_reachable_time)
			break;

	case NUD_STALE:
		neigh->nud_state = NUD_DELAY;
		ndisc_add_timer(neigh, nd_delay_first_probe);
	}
}

/*
 *	Received a neighbour announce
 */
void ndisc_event_na(struct neighbour *neigh, unsigned char * opt, int opt_len,
		    int solicited, int override)
{
	struct sk_buff *skb;

	if (neigh->nud_state == NUD_NONE)
	{
		neigh->nud_state = NUD_INCOMPLETE;
	}

	if (neigh->nud_state == NUD_INCOMPLETE || override)
	{

		if (opt_len == 0)
		{
			printk(KERN_DEBUG "no opt on NA\n");
		}
		else
		{
			/* record hardware address */

			neigh->h_dest = neigh->hh_data;
			neigh->flags &= ~NCF_HHVALID;

			if (ndisc_store_hwaddr(neigh->dev, opt, opt_len,
					       neigh->h_dest, 
					       ND_OPT_TARGET_LL_ADDR))
			{
				printk(KERN_DEBUG
				       "event_na: invalid TARGET_LL_ADDR\n");
				neigh->h_dest = NULL;
				neigh->nud_state = NUD_NONE;
				return;
			}
		}
	}


	if (solicited || override || neigh->nud_state == NUD_INCOMPLETE)
	{

		neigh->probes = 0;
		neigh->tstamp = jiffies;

		if (neigh->nud_state & NUD_IN_TIMER)
		{
			ndisc_del_timer(neigh);
		}

		if (solicited)
		{
			neigh->nud_state = NUD_REACHABLE;
		}
		else
		{
			neigh->nud_state = NUD_STALE;
		}
	}
			
	while ((skb=skb_dequeue(&neigh->arp_queue)))
	{
		int priority = SOPRI_NORMAL;

		if (skb->sk)
			priority = skb->sk->priority;
		
		dev_queue_xmit(skb, neigh->dev, priority);
	}
}

static void ndisc_event_ns(struct in6_addr *saddr, struct sk_buff *skb)
{
	struct neighbour *neigh;
	u8 *opt;
	int len;

	opt = skb->h.raw;
	opt += sizeof(struct icmpv6hdr) + sizeof(struct in6_addr);

	len = skb->tail - opt;

	neigh = ndisc_retrieve_neigh(skb->dev, saddr);

	if (neigh == NULL)
	{
		neigh = ndisc_new_neigh(skb->dev, saddr);
	}
       	
	switch(neigh->nud_state) {
		case NUD_REACHABLE:
		case NUD_STALE:
		case NUD_DELAY:
			if (*opt != ND_OPT_SOURCE_LL_ADDR ||
			    len != neigh->dev->addr_len ||
			    memcmp(neigh->h_dest, opt + 2, len))
			{
				break;
			}

			if (neigh->nud_state & NUD_IN_TIMER)
			{
				ndisc_del_timer(neigh);
			}
		default:
			neigh->flags &= ~NCF_HHVALID;
			neigh->h_dest = neigh->hh_data;
			
			if (ndisc_store_hwaddr(neigh->dev, opt, len,
					       neigh->h_dest,
					       ND_OPT_SOURCE_LL_ADDR))
			{
				printk(KERN_DEBUG 
				       "event_ns: invalid SOURCE_LL_ADDR\n");
				neigh->h_dest = NULL;
				neigh->nud_state = NUD_NONE;
				return;
			}

			neigh->nud_state = NUD_STALE;
			neigh->tstamp = jiffies;
			neigh->probes = 0;
	}

}

static struct rt6_info *ndisc_get_dflt_router(struct device *dev,
					      struct in6_addr *addr)
{	
	struct rt6_info *iter;

	for (iter = default_rt_list; iter; iter=iter->next)
	{
		if (dev == iter->rt_dev &&
		    ipv6_addr_cmp(&iter->rt_dst, addr) == 0)
		{
			return iter;
		}
	}
	return NULL;
}

static void ndisc_add_dflt_router(struct rt6_info *rt)
{
	struct rt6_info *iter;

	rt->rt_ref++;
	rt->fib_node = &routing_table;
	rt6_stats.fib_rt_alloc++;

	if (default_rt_list == NULL)
	{
		default_rt_list = rt;
		return;
	}

	for (iter = default_rt_list; iter->next; iter=iter->next)
		;

	iter->next = rt;
}

static void ndisc_del_dflt_router(struct rt6_info *rt)
{
	struct rt6_info *iter, *back;

	if (rt == default_rt_list)
	{
		default_rt_list = rt->next;
	}
	else
	{
		back = NULL;
		for (iter = default_rt_list; iter; iter=iter->next)
		{
			if (iter == rt)
			{
				back->next = rt->next;
				break;
			}
			back = iter;
		}
	}

	rt->fib_node = NULL;
	rt_release(rt);
}

static void ndisc_purge_dflt_routers(void)
{
	struct rt6_info *iter, *rt;

	for (iter = default_rt_list; iter; )
	{
		rt = iter;
		iter=iter->next;
		rt_release(rt);
	}
	default_rt_list = NULL;
}

static void ndisc_ll_addr_update(struct neighbour *neigh, u8* opt, int len,
				 int type)
{
	switch(neigh->nud_state) {
	case NUD_REACHABLE:
	case NUD_STALE:
	case NUD_DELAY:
		if (len == neigh->dev->addr_len &&
		    memcmp(neigh->h_dest, opt + 2, len) == 0)
		{
			break;
		}

		if (neigh->nud_state & NUD_IN_TIMER)
		{
			ndisc_del_timer(neigh);
		}
	default:
		neigh->flags &= ~NCF_HHVALID;
		neigh->h_dest = neigh->hh_data;
		
		if (ndisc_store_hwaddr(neigh->dev, opt, len, neigh->h_dest,
				       type))
		{
			printk(KERN_DEBUG "NDISC: invalid LL_ADDR\n");
			neigh->h_dest = NULL;
			neigh->nud_state = NUD_NONE;
			break;
		}
		
		neigh->nud_state = NUD_STALE;
		neigh->tstamp = jiffies;
		neigh->probes = 0;
	}
	
}

struct rt6_info * dflt_rt_lookup(void)
{
	struct rt6_info *match = NULL;
	struct rt6_info *rt;
	int score = -1;
	unsigned long now = jiffies;

	for (rt = default_rt_list; rt; rt=rt->next)
	{
		struct neighbour *neigh = rt->rt_nexthop;
		
		if (score < 0)
		{
			score = 0;
			match = rt;
		}

		if (neigh->nud_state == NUD_REACHABLE)
		{
			if (score < 1)
			{
				score = 1;
				match = rt;
			}

			if (now  - neigh->tstamp < nd_reachable_time)
			{
				return rt;
			}
		}

	}

	return match;
}

static void ndisc_router_discovery(struct sk_buff *skb)
{
        struct ra_msg *ra_msg = (struct ra_msg *) skb->h.raw;
	struct neighbour *neigh;
	struct inet6_dev *in6_dev;
	struct rt6_info *rt;
	int lifetime;
	int optlen;

	__u8 * opt = (__u8 *)(ra_msg + 1);

	optlen = (skb->tail - skb->h.raw) - sizeof(struct ra_msg);

	if (skb->ipv6_hdr->hop_limit != 255)
	{
		printk(KERN_WARNING
		       "NDISC: fake router advertisment received\n");
		return;
	}

	/*
	 *	set the RA_RECV flag in the interface
	 */

	in6_dev = ipv6_get_idev(skb->dev);
	if (in6_dev == NULL)
	{
		printk(KERN_DEBUG "RA: can't find in6 device\n");
		return;
	}
	
	if (in6_dev->if_flags & IF_RS_SENT)
	{
		/*
		 *	flag that an RA was received after an RS was sent
		 *	out on this interface.
		 */
		in6_dev->if_flags |= IF_RA_RCVD;
	}

	lifetime = ntohs(ra_msg->icmph.icmp6_rt_lifetime);

	rt = ndisc_get_dflt_router(skb->dev, &skb->ipv6_hdr->saddr);

	if (rt && lifetime == 0)
	{
		ndisc_del_dflt_router(rt);
		rt = NULL;
	}

	if (rt == NULL && lifetime)
	{
		printk(KERN_DEBUG "ndisc_rdisc: new default router\n");

		rt = (struct rt6_info *)kmalloc(sizeof(struct rt6_info),
						GFP_ATOMIC);

		neigh = ndisc_retrieve_neigh(skb->dev, &skb->ipv6_hdr->saddr);

		if (neigh == NULL)
		{
			neigh = ndisc_new_neigh(skb->dev,
						&skb->ipv6_hdr->saddr);
		}

		atomic_inc(&neigh->refcnt);
		neigh->flags |= NCF_ROUTER;

		memset(rt, 0, sizeof(struct rt6_info));

		ipv6_addr_copy(&rt->rt_dst, &skb->ipv6_hdr->saddr);
		rt->rt_metric = 1;
		rt->rt_flags = RTF_GATEWAY | RTF_DYNAMIC;
		rt->rt_dev = skb->dev;
		rt->rt_nexthop = neigh;

		ndisc_add_dflt_router(rt);
	}

	if (rt)
	{
		rt->rt_expires = jiffies + (HZ * lifetime);
        }

	if (ra_msg->icmph.icmp6_hop_limit)
	{
		ipv6_hop_limit = ra_msg->icmph.icmp6_hop_limit;
	}

	/*
	 *	Update Reachable Time and Retrans Timer
	 */

	if (ra_msg->retrans_timer)
	{
		nd_retrans_timer = ntohl(ra_msg->retrans_timer);
	}

	if (ra_msg->reachable_time)
	{
		__u32 rtime = ntohl(ra_msg->reachable_time);

		if (rtime != nd_base_reachable_time)
		{
			nd_base_reachable_time = rtime;
			nd_gc_staletime	= 3 * nd_base_reachable_time;
			nd_reachable_time = rand_reach_time();
		}
		
	}

	/*
	 *	Process options.
	 */

        while(optlen > 0) {
                int len;

                len = (opt[1] << 3);

		if (len == 0)
		{
			printk(KERN_DEBUG "RA: opt has 0 len\n");
			break;
		}

                switch(*opt) {
                case ND_OPT_SOURCE_LL_ADDR:
			
			if (rt == NULL)
				break;
			
			neigh = rt->rt_nexthop;

			ndisc_ll_addr_update(neigh, opt, len,
					     ND_OPT_SOURCE_LL_ADDR);
			break;

                case ND_OPT_PREFIX_INFO:
			addrconf_prefix_rcv(skb->dev, opt, len);
                        break;

                case ND_OPT_MTU:

			if (rt)
			{
				int mtu;
				struct device *dev;
				
				mtu = htonl(*(__u32 *)opt+4);
				dev = rt->rt_nexthop->dev;

				if (mtu < 576)
				{
					printk(KERN_DEBUG "NDISC: router "
					       "announcement with mtu = %d\n",
					       mtu);
					break;
				}

				if (dev->change_mtu)
				{
					dev->change_mtu(dev, mtu);
				}
				else
				{
					dev->mtu = mtu;
				}
			}
                        break;

		case ND_OPT_TARGET_LL_ADDR:
		case ND_OPT_REDIRECT_HDR:
			printk(KERN_DEBUG "got illegal option with RA");
			break;
		default:
			printk(KERN_DEBUG "unkown option in RA\n");
                }
                optlen -= len;
                opt += len;
        }
        
}

void ndisc_forwarding_on(void)
{
	/*
	 *	forwarding was turned on
	 */

	ndisc_purge_dflt_routers();
}

void ndisc_forwarding_off(void)
{
	/*
	 *	forwarding was turned off
	 */
}

static void ndisc_redirect_rcv(struct sk_buff *skb)
{
	struct icmpv6hdr *icmph;
	struct in6_addr *dest;
	struct in6_addr *target;	/* new first hop to destination */
	struct neighbour *neigh;
	struct rt6_info *rt;
	int on_link = 0;
	int optlen;
	u8 * opt;

	if (skb->ipv6_hdr->hop_limit != 255)
	{
		printk(KERN_WARNING
		       "NDISC: fake ICMP redirect received\n");
		return;
	}

	if (!(ipv6_addr_type(&skb->ipv6_hdr->saddr) & IPV6_ADDR_LINKLOCAL))
	{
		printk(KERN_WARNING
		       "ICMP redirect: source address is not linklocal\n");
		return;
	}

	optlen = skb->tail - skb->h.raw;
	optlen -= sizeof(struct icmpv6hdr) + 2 * sizeof(struct in6_addr);

	if (optlen < 0)
	{
		printk(KERN_WARNING "ICMP redirect: packet too small\n");
		return;
	}

	icmph = (struct icmpv6hdr *) skb->h.raw;
	target = (struct in6_addr *) (icmph + 1);
	dest = target + 1;

	if (ipv6_addr_type(dest) & IPV6_ADDR_MULTICAST)
	{
		printk(KERN_WARNING "ICMP redirect for multicast addr\n");
		return;
	}

	if (ipv6_addr_cmp(dest, target) == 0)
	{
		on_link = 1;
	}
	else if (!(ipv6_addr_type(target) & IPV6_ADDR_LINKLOCAL))
	{
		printk(KERN_WARNING
		       "ICMP redirect: target address is not linklocal\n");
		return;
	}

	/* passed validation tests */

	rt = ipv6_rt_redirect(skb->dev, dest, target, on_link);

	if (rt == NULL)
	{
		printk(KERN_WARNING "ICMP redirect: no route to host\n");
		return;
	}

	neigh = rt->rt_nexthop;

	opt = (u8 *) (dest + 1);

	while (optlen > 0)
	{
		int len;

		len = (opt[1] << 3);

		if (*opt == ND_OPT_TARGET_LL_ADDR)
		{
			ndisc_ll_addr_update(neigh, opt, len,
					     ND_OPT_TARGET_LL_ADDR);
		}

		opt += len;
		optlen -= len;
	}
}

void ndisc_send_redirect(struct sk_buff *skb, struct neighbour *neigh,
			 struct in6_addr *target)
{
	struct sock *sk = (struct sock *) ndisc_socket.data;
	int len = sizeof(struct icmpv6hdr) + 2 * sizeof(struct in6_addr);
	struct sk_buff *buff;
	struct inet6_ifaddr *ifp;
	struct icmpv6hdr *icmph;
	struct in6_addr *addrp;
	struct rt6_info *rt;
	int ta_len = 0;
	u8 *opt;
	int rd_len;
	int err;
	int hlen;

	rt = fibv6_lookup(&skb->ipv6_hdr->saddr, skb->dev, 0);
	
	if (rt->rt_flags & RTF_GATEWAY)
	{
		printk(KERN_DEBUG "ndisc_send_redirect: not a neighbour\n");
		return;
	}

	if (neigh->nud_state == NUD_REACHABLE)
	{
		ta_len  = ((neigh->dev->addr_len + 1) >> 3) + 1;
		len += (ta_len << 3);
	}

	rd_len = min(536 - len, ntohs(skb->ipv6_hdr->payload_len) + 8);
	rd_len &= ~0x7;
	len += rd_len;

	ifp = ipv6_get_lladdr(skb->dev);

	if (ifp == NULL)
	{
		printk(KERN_DEBUG "redirect: no link_local addr for dev\n");
		return;
	}

	buff = sock_alloc_send_skb(sk, MAX_HEADER + len, 0, 0, &err);

	if (buff == NULL)
	{
		printk(KERN_DEBUG "ndisc_send_redirect: alloc_skb failed\n");
		return;
	}

	
	hlen = 0;
	if (skb->dev->hard_header_len)
	{
		hlen = (skb->dev->hard_header_len + 15) & ~15;
	}

	skb_reserve(buff, hlen + sizeof(struct ipv6hdr));
	
	icmph = (struct icmpv6hdr *) skb_put(buff, len);

	memset(icmph, 0, sizeof(struct icmpv6hdr));
	icmph->type = NDISC_REDIRECT;

	/*
	 *	copy target and destination addresses
	 */

	addrp = (struct in6_addr *)(icmph + 1);
	ipv6_addr_copy(addrp, target);
	addrp++;
	ipv6_addr_copy(addrp, &skb->ipv6_hdr->daddr);
	
	opt = (u8*) (addrp + 1);
		
	/*
	 *	include target_address option
	 */

	if (ta_len)
	{
		int zb;
		
		*(opt++) = ND_OPT_TARGET_LL_ADDR;
		*(opt++) = ta_len;

		memcpy(opt, neigh->h_dest, neigh->dev->addr_len);
		opt += neigh->dev->addr_len;

		/* 
		 *	if link layer address doesn't end on a 8 byte
		 *	boundary memset(0) the remider
		 */

		zb = (neigh->dev->addr_len + 2) & 0x7; 
		if (zb)
		{
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

	memcpy(opt, &skb->ipv6_hdr, rd_len - 8);
	
	icmph->checksum = csum_ipv6_magic(&ifp->addr, &skb->ipv6_hdr->saddr,
					  len, IPPROTO_ICMPV6,
					  csum_partial((u8 *) icmph, len, 0));

	ipv6_xmit(sk, buff, &ifp->addr, &skb->ipv6_hdr->saddr, NULL, IPPROTO_ICMPV6);
}

/* Called by upper layers to validate neighbour cache entries. */

void ndisc_validate(struct neighbour *neigh)
{
        if (neigh->nud_state == NUD_INCOMPLETE)
                return;

        if (neigh->nud_state == NUD_DELAY) 
	{
                ndisc_del_timer(neigh);
        }

        nd_stats.rcv_upper_conf++;
        neigh->nud_state = NUD_REACHABLE;
        neigh->tstamp = jiffies;
}

int ndisc_rcv(struct sk_buff *skb, struct device *dev,
	      struct in6_addr *saddr, struct in6_addr *daddr,
	      struct ipv6_options *opt, unsigned short len)
{
	struct nd_msg *msg = (struct nd_msg *) skb->h.raw;
	struct neighbour *neigh;
	struct inet6_ifaddr *ifp;

	switch (msg->icmph.type) {
	case NDISC_NEIGHBOUR_SOLICITATION:
		if ((ifp = ipv6_chk_addr(&msg->target)))
		{
			int addr_type;

			if (ifp->flags & DAD_INCOMPLETE)
			{
				/*
				 *	DAD failed 
				 */

				printk(KERN_DEBUG "duplicate address\n");
				del_timer(&ifp->timer);
				return 0;
			}

			addr_type = ipv6_addr_type(saddr);
			if (addr_type & IPV6_ADDR_UNICAST)
			{
				int inc;

				/* 
				 *	update / create cache entry
				 *	for the source adddress
				 */

				nd_stats.rcv_probes_ucast++;
				ndisc_event_ns(saddr, skb);

				/* answer solicitation */
				neigh = ndisc_retrieve_neigh(dev, saddr);

				inc = ipv6_addr_type(daddr);
				inc &= IPV6_ADDR_MULTICAST;

				ndisc_send_na(dev, neigh, saddr, &ifp->addr, 
					      ifp->idev->router, 1, inc, inc);
			}
			else
			{
				/* FIXME */
				printk(KERN_DEBUG "ns: non unicast saddr\n");
			}
		}
		break;

	case NDISC_NEIGHBOUR_ADVERTISEMENT:

		neigh = ndisc_retrieve_neigh(skb->dev, &msg->target);
		if (neigh)
		{
			if (neigh->flags & NCF_ROUTER)
			{
				if (msg->icmph.icmp6_router == 0)
				{
					/*
					 *	Change: router to host
					 */
					
					struct rt6_info *rt;
					rt = ndisc_get_dflt_router(skb->dev,
								   saddr);
					if (rt)
					{
						ndisc_del_dflt_router(rt);
					}
				}
			}
			else
			{
				if (msg->icmph.icmp6_router)
				{
					neigh->flags |= NCF_ROUTER;
				}
			}
			ndisc_event_na(neigh, (unsigned char *) &msg->opt,
				       skb->tail - (u8 *)&msg->opt /*opt_len*/,
				       msg->icmph.icmp6_solicited,
				       msg->icmph.icmp6_override);
		}
		break;

	}

	if (ipv6_forwarding == 0)
	{
		switch (msg->icmph.type) {
		case NDISC_ROUTER_ADVERTISEMENT:
			ndisc_router_discovery(skb);
			break;

		case NDISC_REDIRECT:
			ndisc_redirect_rcv(skb);
			break;
		}
	}

	return 0;
}

int ndisc_get_info(char *buffer, char **start, off_t offset, int length,
		   int dummy)
{
	struct neighbour *neigh;
	unsigned long now = jiffies;
	int len = 0;
	int i;

	atomic_inc(&ndisc_lock);

	for (i = 0; i < NCACHE_NUM_BUCKETS; i++)
	{
		for(neigh = neighbours[i]; neigh; neigh=neigh->next)
		{
			int j;

			for (j=0; j<16; j++)
			{
				sprintf(buffer + len, "%02x",
					neigh->addr.s6_addr[j]);
				len += 2;
			}

			len += sprintf(buffer + len,
				       " %02x %02x %08lx %08lx %04x %04x ",
				       i,
				       neigh->nud_state,
				       neigh->expires - now,
				       now - neigh->tstamp,
				       neigh->refcnt,
				       neigh->flags);

			if (neigh->h_dest)
			{
				for (j=0; j< neigh->dev->addr_len; j++)
				{
					sprintf(buffer + len, "%02x",
						neigh->h_dest[j]);
					len += 2;
				}
			}
			else
                                len += sprintf(buffer + len, "000000000000");
			len += sprintf(buffer + len, "\n");
				       
		}
	}

	ndisc_release_lock();

	*start = buffer + offset;

	len -= offset;

	if (len > length)
		len = length;
	return len;
}

struct proc_dir_entry ndisc_proc_entry =
{
        0, 11, "ndisc_cache",
        S_IFREG | S_IRUGO, 1, 0, 0,
        0, NULL,
        &ndisc_get_info
};

void ndisc_init(struct proto_ops *ops)
{
	struct sock *sk;
        int i = 0;
        int err;

	/*
	 *	Init ndisc_socket
	 */
	ndisc_socket.type=SOCK_RAW;
	ndisc_socket.ops=ops;

	if((err=ops->create(&ndisc_socket, IPPROTO_ICMPV6))<0)
		printk(KERN_DEBUG 
		       "Failed to create the NDISC control socket.\n");

	MOD_DEC_USE_COUNT;

	sk = ndisc_socket.data;
	sk->allocation = GFP_ATOMIC;
	sk->net_pinfo.af_inet6.hop_limit = 255;
	sk->net_pinfo.af_inet6.priority  = 15;
	sk->num = 256;				/* Don't receive any data */

        /*
         * Initialize the neighbours hash buckets.
         */

        for (; i < NCACHE_NUM_BUCKETS; i++)
                neighbours[i] = NULL;
 
        /* General ND state machine timer. */
	init_timer(&ndisc_timer);
	ndisc_timer.function = ndisc_timer_handler;
	ndisc_timer.data = 0L;
	ndisc_timer.expires = 0L;

        /* ND GC timer */
        init_timer(&ndisc_gc_timer);
        ndisc_gc_timer.function = ndisc_garbage_collect;
        ndisc_gc_timer.data = 0L;
        ndisc_gc_timer.expires = jiffies + nd_gc_interval;

	add_timer(&ndisc_gc_timer);

#ifdef CONFIG_IPV6_MODULE
	ndisc_eth_hook = ndisc_eth_resolv;
        proc_register_dynamic(&proc_net, &ndisc_proc_entry);
#endif
}

#ifdef CONFIG_IPV6_MODULE
void ndisc_cleanup(void)
{
	ndisc_eth_hook = NULL;
        proc_unregister(&proc_net, ndisc_proc_entry.low_ino);
	del_timer(&ndisc_gc_timer);
	del_timer(&ndisc_timer);
}
#endif

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce -pipe -m486 -DCPU=486 -DMODULE -DMODVERSIONS -include /usr/src/linux/include/linux/modversions.h  -c -o ndisc.o ndisc.c"
 * c-file-style: "Linux"
 * End:
 */
