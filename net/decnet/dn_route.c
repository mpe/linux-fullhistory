
/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Routing Functions (Endnode and Router)
 *
 * Authors:     Steve Whitehouse <SteveW@ACM.org>
 *              Eduardo Marcelo Serrat <emserrat@geocities.com>
 *
 * Changes:
 *              Steve Whitehouse : Fixes to allow "intra-ethernet" and
 *                                 "return-to-sender" bits on outgoing
 *                                 packets.
 *		Steve Whitehouse : Timeouts for cached routes.
 *              Steve Whitehouse : Use dst cache for input routes too.
 *              Steve Whitehouse : Fixed error values in dn_send_skb.
 *              Steve Whitehouse : Rework routing functions to better fit
 *                                 DECnet routing design
 *              Alexey Kuznetsov : New SMP locking
 *              Steve Whitehouse : More SMP locking changes & dn_cache_dump()
 *              Steve Whitehouse : Prerouting NF hook, now really is prerouting.
 *				   Fixed possible skb leak in rtnetlink funcs.
 */

/******************************************************************************
    (c) 1995-1998 E.M. Serrat		emserrat@geocities.com
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*******************************************************************************/

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/inet.h>
#include <linux/route.h>
#include <net/sock.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <linux/string.h>
#include <linux/netfilter_decnet.h>
#include <asm/errno.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/dn.h>
#include <net/dn_dev.h>
#include <net/dn_nsp.h>
#include <net/dn_route.h>
#include <net/dn_neigh.h>
#include <net/dn_fib.h>
#include <net/dn_raw.h>

extern struct neigh_table dn_neigh_table;

#define DN_HASHBUCKETS 16

static unsigned char dn_hiord_addr[6] = {0xAA,0x00,0x04,0x00,0x00,0x00};

static int dn_dst_gc(void);
static struct dst_entry *dn_dst_check(struct dst_entry *, __u32);
static struct dst_entry *dn_dst_reroute(struct dst_entry *, struct sk_buff *skb);
static struct dst_entry *dn_dst_negative_advice(struct dst_entry *);
static void dn_dst_link_failure(struct sk_buff *);
static int dn_route_input(struct sk_buff *);

static struct dn_route *dn_route_cache[DN_HASHBUCKETS];
static rwlock_t dn_hash_lock = RW_LOCK_UNLOCKED;

static struct timer_list dn_route_timer = { NULL, NULL, 0, 0L, NULL };
int decnet_dst_gc_interval = 2;

static struct dst_ops dn_dst_ops = {
	PF_DECnet,
	__constant_htons(ETH_P_DNA_RT),
	128,
	dn_dst_gc,
	dn_dst_check,
	dn_dst_reroute,
	NULL,
	dn_dst_negative_advice,
	dn_dst_link_failure,
	sizeof(struct dn_route),
	ATOMIC_INIT(0)
};

static __inline__ unsigned dn_hash(unsigned short dest)
{
	unsigned short tmp = (dest&0xff) ^ (dest>>8);
	return (tmp&0x0f) ^ (tmp>>4);
}

static void dn_dst_check_expire(unsigned long dummy)
{
	int i;
	struct dn_route *rt, **rtp;
	unsigned long now = jiffies;
	unsigned long expire = 120 * HZ;

	for(i = 0; i < DN_HASHBUCKETS; i++) {
		rtp = &dn_route_cache[i];

		write_lock(&dn_hash_lock);
		for(;(rt=*rtp); rtp = &rt->u.rt_next) {
			if (atomic_read(&rt->u.dst.__refcnt) ||
					(now - rt->u.dst.lastuse) < expire)
				continue;
			*rtp = rt->u.rt_next;
			rt->u.rt_next = NULL;
			dst_free(&rt->u.dst);
		}
		write_unlock(&dn_hash_lock);

		if ((jiffies - now) > 0)
			break;
	}

	dn_route_timer.expires = now + decnet_dst_gc_interval * HZ;
	add_timer(&dn_route_timer);
}

static int dn_dst_gc(void)
{
	struct dn_route *rt, **rtp;
	int i;
	unsigned long now = jiffies;
	unsigned long expire = 10 * HZ;

	write_lock_bh(&dn_hash_lock);
	for(i = 0; i < DN_HASHBUCKETS; i++) {
		rtp = &dn_route_cache[i];
		for(; (rt=*rtp); rtp = &rt->u.rt_next) {
			if (atomic_read(&rt->u.dst.__refcnt) ||
					(now - rt->u.dst.lastuse) < expire)
				continue;
			*rtp = rt->u.rt_next;
			rt->u.rt_next = NULL;
			dst_free(&rt->u.dst);
			break;
		}
	}
	write_unlock_bh(&dn_hash_lock);

	return 0;
}

static struct dst_entry *dn_dst_check(struct dst_entry *dst, __u32 cookie)
{
	dst_release(dst);
	return NULL;
}

static struct dst_entry *dn_dst_reroute(struct dst_entry *dst,
					struct sk_buff *skb)
{
	return NULL;
}

/*
 * This is called through sendmsg() when you specify MSG_TRYHARD
 * and there is already a route in cache.
 */
static struct dst_entry *dn_dst_negative_advice(struct dst_entry *dst)
{
	dst_release(dst);
	return NULL;
}

static void dn_dst_link_failure(struct sk_buff *skb)
{
	return;
}

static void dn_insert_route(struct dn_route *rt)
{
	unsigned hash = dn_hash(rt->rt_daddr);
	unsigned long now = jiffies;

	write_lock_bh(&dn_hash_lock);
	rt->u.rt_next = dn_route_cache[hash];
	dn_route_cache[hash] = rt;
	
	dst_hold(&rt->u.dst);
	rt->u.dst.__use++;
	rt->u.dst.lastuse = now;

	write_unlock_bh(&dn_hash_lock);
}

void dn_run_flush(unsigned long dummy)
{
	int i;
	struct dn_route *rt, *next;

	write_lock_bh(&dn_hash_lock);
	for(i = 0; i < DN_HASHBUCKETS; i++) {
		if ((rt = xchg(&dn_route_cache[i], NULL)) == NULL)
			continue;

		for(; rt; rt=next) {
			next = rt->u.rt_next;
			rt->u.rt_next = NULL;
			dst_free((struct dst_entry *)rt);
		}
	}
	write_unlock_bh(&dn_hash_lock);
}

static int dn_route_rx_packet(struct sk_buff *skb)
{
	int err;

	if ((err = dn_route_input(skb)) == 0)
		return skb->dst->input(skb);

	kfree_skb(skb);
	return err;
}

static int dn_route_rx_long(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	unsigned char *ptr = skb->data;

	if (skb->len < 21) /* 20 for long header, 1 for shortest nsp */
		goto drop_it;

	skb_pull(skb, 20);
	skb->h.raw = skb->data;

        /* Destination info */
        ptr += 2;
	cb->dst = dn_htons(dn_eth2dn(ptr));
        if (memcmp(ptr, dn_hiord_addr, 4) != 0)
                goto drop_it;
        ptr += 6;


        /* Source info */
        ptr += 2;
	cb->src = dn_htons(dn_eth2dn(ptr));
        if (memcmp(ptr, dn_hiord_addr, 4) != 0)
                goto drop_it;
        ptr += 6;
        /* Other junk */
        ptr++;
        cb->hops = *ptr++; /* Visit Count */

	return NF_HOOK(PF_DECnet, NF_DN_PRE_ROUTING, skb, skb->dev, NULL, dn_route_rx_packet);

drop_it:
	kfree_skb(skb);
	return 0;
}



static int dn_route_rx_short(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	unsigned char *ptr = skb->data;

	if (skb->len < 6) /* 5 for short header + 1 for shortest nsp */
		goto drop_it;

	skb_pull(skb, 5);
	skb->h.raw = skb->data;

	cb->dst = *(dn_address *)ptr;
        ptr += 2;
        cb->src = *(dn_address *)ptr;
        ptr += 2;
        cb->hops = *ptr & 0x3f;

	return NF_HOOK(PF_DECnet, NF_DN_PRE_ROUTING, skb, skb->dev, NULL, dn_route_rx_packet);

drop_it:
        kfree_skb(skb);
        return 0;
}

int dn_route_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt)
{
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	unsigned char flags = 0;
	int padlen = 0;
	__u16 len = dn_ntohs(*(__u16 *)skb->data);
	struct dn_dev *dn = (struct dn_dev *)dev->dn_ptr;

	if (dn == NULL)
		goto dump_it;

	cb->stamp = jiffies;
	cb->iif = dev->ifindex;

	skb_pull(skb, 2);

	if (len > skb->len)
		goto dump_it;

	skb_trim(skb, len);

	flags = *skb->data;

	/*
	 * If we have padding, remove it.
	 */
	if (flags & DN_RT_F_PF) {
		padlen = flags & ~DN_RT_F_PF;
		skb_pull(skb, padlen);
		flags = *skb->data;
	}

	skb->nh.raw = skb->data;

	/*
	 * Weed out future version DECnet
	 */
	if (flags & DN_RT_F_VER)
		goto dump_it;

	cb->rt_flags = flags;

	if (decnet_debug_level & 1)
		printk(KERN_DEBUG 
			"dn_route_rcv: got 0x%02x from %s [%d %d %d]\n",
			(int)flags, (dev) ? dev->name : "???", len, skb->len, 
			padlen);

#ifdef CONFIG_DECNET_RAW
	dn_raw_rx_routing(skb);
#endif /* CONFIG_DECNET_RAW */

        if (flags & DN_RT_PKT_CNTL) {
                switch(flags & DN_RT_CNTL_MSK) {
        	        case DN_RT_PKT_INIT:
				dn_dev_init_pkt(skb);
				break;
                	case DN_RT_PKT_VERI:
				dn_dev_veri_pkt(skb);
				break;
		}

		if (dn->parms.state != DN_DEV_S_RU)
			goto dump_it;

		switch(flags & DN_RT_CNTL_MSK) {
                	case DN_RT_PKT_HELO:
				dn_dev_hello(skb);
				dn_neigh_pointopoint_hello(skb);
				return 0;

                	case DN_RT_PKT_L1RT:
                	case DN_RT_PKT_L2RT:
#ifdef CONFIG_DECNET_ROUTER
				return dn_fib_rt_message(skb);
#else
                      		break;
#endif /* CONFIG_DECNET_ROUTER */
                	case DN_RT_PKT_ERTH:
				dn_neigh_router_hello(skb);
                        	return 0;

                	case DN_RT_PKT_EEDH:
				dn_neigh_endnode_hello(skb);
                        	return 0;
                }
        } else {
		if (dn->parms.state != DN_DEV_S_RU)
			goto dump_it;

		skb_pull(skb, 1); /* Pull flags */

                switch(flags & DN_RT_PKT_MSK) {
                	case DN_RT_PKT_LONG:
                        	return dn_route_rx_long(skb);
                	case DN_RT_PKT_SHORT:
                        	return dn_route_rx_short(skb);
		}
        }

dump_it:
	kfree_skb(skb);
	return 0;
}

static int dn_output(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct dn_route *rt = (struct dn_route *)dst;
	struct net_device *dev = dst->dev;
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	struct neighbour *neigh;

	int err = -EINVAL;

	if ((neigh = dst->neighbour) == NULL)
		goto error;

	skb->dev = dev;

	cb->src = rt->rt_saddr;
	cb->dst = rt->rt_daddr;

	/*
	 * Always set the Intra-Ethernet bit on all outgoing packets
	 * originated on this node. Only valid flag from upper layers
	 * is return-to-sender-requested. Set hop count to 0 too.
	 */
	cb->rt_flags &= ~DN_RT_F_RQR;
	cb->rt_flags |= DN_RT_F_IE;
	cb->hops = 0;

	return NF_HOOK(PF_DECnet, NF_DN_LOCAL_OUT, skb, NULL, dev, neigh->output);

error:
	if (net_ratelimit())
		printk(KERN_DEBUG "dn_output: This should not happen\n");

	kfree_skb(skb);

	return err;
}

#ifdef CONFIG_DECNET_ROUTER
static int dn_forward(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	struct dst_entry *dst = skb->dst;
	struct neighbour *neigh;
	int err = -EINVAL;

	if ((neigh = dst->neighbour) == NULL)
		goto error;

	/*
	 * Hop count exceeded.
	 */
	err = 0;
	if (++cb->hops > 30)
		goto drop;

	skb->dev = dst->dev;

	/*
	 * If packet goes out same interface it came in on, then set
	 * the Intra-Ethernet bit. This has no effect for short
	 * packets, so we don't need to test for them here.
	 */
	if (cb->iif == dst->dev->ifindex)
		cb->rt_flags |= DN_RT_F_IE;
	else
		cb->rt_flags &= ~DN_RT_F_IE;

	return NF_HOOK(PF_DECnet, NF_DN_FORWARD, skb, skb->rx_dev, skb->dev, neigh->output);


error:
	if (net_ratelimit())
		printk(KERN_DEBUG "dn_forward: This should not happen\n");
drop:
	kfree_skb(skb);

	return err;
}
#endif

/*
 * Drop packet. This is used for endnodes and for
 * when we should not be forwarding packets from
 * this dest.
 */
static int dn_blackhole(struct sk_buff *skb)
{
	kfree_skb(skb);
	return 0;
}

/*
 * Used to catch bugs. This should never normally get
 * called.
 */
static int dn_rt_bug(struct sk_buff *skb)
{
	if (net_ratelimit()) {
		struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;

		printk(KERN_DEBUG "dn_rt_bug: skb from:%04x to:%04x\n",
				cb->src, cb->dst);
	}

	kfree_skb(skb);

	return -EINVAL;
}

static int dn_route_output_slow(struct dst_entry **pprt, dn_address dst, dn_address src, int flags)
{
	struct dn_route *rt = NULL;
	struct net_device *dev = decnet_default_device;
	struct neighbour *neigh = NULL;
	struct dn_dev *dn_db;


#ifdef CONFIG_DECNET_ROUTER
	if (decnet_node_type != DN_RT_INFO_ENDN) {
		struct dn_fib_res res;
		int err = 0;

		res.res_addr = dst;
		res.res_mask = 0;
		res.res_ifindex = 0;
		res.res_proto = RTN_UNSPEC;
		res.res_cost = 0;

		if ((err = dn_fib_resolve(&res)) == 0) {

			if (!res.res_fa || (res.res_fa->fa_type != RTN_UNICAST))
				return -EPROTO;

			if ((neigh = neigh_clone(res.res_fa->fa_neigh)) != NULL)
				goto got_route;

			return -ENOBUFS;
		}

		if (err != -ENOENT)
			return err;
	}
#endif 

	/* Look in On-Ethernet cache first */
	if (!(flags & MSG_TRYHARD)) {
		if ((neigh = dn_neigh_lookup(&dn_neigh_table, &dst)) != NULL)
			goto got_route;
	}

	if (dev == NULL)
		return -EINVAL;

	dn_db = dev->dn_ptr;

	if (dn_db == NULL)
		return -EINVAL;

	/* Try default router */
	if ((neigh = neigh_clone(dn_db->router)) != NULL)
		goto got_route;

	/* Send to default device (and hope for the best) if above fail */
	if ((neigh = __neigh_lookup(&dn_neigh_table, &dst, dev, 1)) != NULL)
		goto got_route;


	return -EINVAL;

got_route:

	if ((rt = dst_alloc(&dn_dst_ops)) == NULL) {
		neigh_release(neigh);
		return -EINVAL;
	}

	dn_db = (struct dn_dev *)neigh->dev->dn_ptr;
	
	rt->rt_saddr = src;
	rt->rt_daddr = dst;
	rt->rt_oif = neigh->dev->ifindex;
	rt->rt_iif = 0;

	rt->u.dst.neighbour = neigh;
	rt->u.dst.dev = neigh->dev;
	rt->u.dst.lastuse = jiffies;
	rt->u.dst.output = dn_output;
	rt->u.dst.input  = dn_rt_bug;

	if (dn_dev_islocal(neigh->dev, rt->rt_daddr))
		rt->u.dst.input = dn_nsp_rx;

	dn_insert_route(rt);
	*pprt = &rt->u.dst;

	return 0;
}

int dn_route_output(struct dst_entry **pprt, dn_address dst, dn_address src, int flags)
{
	unsigned hash = dn_hash(dst);
	struct dn_route *rt = NULL;

	if (!(flags & MSG_TRYHARD)) {
		read_lock_bh(&dn_hash_lock);
		for(rt = dn_route_cache[hash]; rt; rt = rt->u.rt_next) {
			if ((dst == rt->rt_daddr) &&
					(src == rt->rt_saddr) &&
					(rt->rt_iif == 0) &&
					(rt->rt_oif != 0)) {
				rt->u.dst.lastuse = jiffies;
				dst_hold(&rt->u.dst);
				rt->u.dst.__use++;
				read_unlock_bh(&dn_hash_lock);
				*pprt = &rt->u.dst;
				return 0;
			}
		}
		read_unlock_bh(&dn_hash_lock);
	}

	return dn_route_output_slow(pprt, dst, src, flags);
}

static int dn_route_input_slow(struct sk_buff *skb)
{
	struct dn_route *rt = NULL;
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	struct net_device *dev = skb->dev;
	struct dn_dev *dn_db;
	struct neighbour *neigh = NULL;
	int (*dnrt_input)(struct sk_buff *skb);
	int (*dnrt_output)(struct sk_buff *skb);

	if (dev == NULL)
		return -EINVAL;

	if ((dn_db = dev->dn_ptr) == NULL)
		return -EINVAL;

	/*
	 * In this case we've just received a packet from a source
	 * outside ourselves pretending to come from us. We don't
	 * allow it any further to prevent routing loops, spoofing and
	 * other nasties. Loopback packets already have the dst attached
	 * so this only affects packets which have originated elsewhere.
	 */
	if (dn_dev_islocal(dev, cb->src))
		return -ENOTUNIQ;

	/*
	 * Default is to create a drop everything entry
	 */
	dnrt_input  = dn_blackhole;
	dnrt_output = dn_rt_bug;

	/*
	 * Is the destination us ?
	 */
	if (!dn_dev_islocal(dev, cb->dst))
		goto non_local_input;

	/*
	 * Local input... find source of skb
	 */
	dnrt_input  = dn_nsp_rx;
	dnrt_output = dn_output;

	if ((neigh = neigh_lookup(&dn_neigh_table, &cb->src, dev)) != NULL)
		goto add_entry;

	if (dn_db->router && ((neigh = neigh_clone(dn_db->router)) != NULL))
		goto add_entry;

	if ((neigh = neigh_create(&dn_neigh_table, &cb->src, dev)) != NULL) {
		if (dev->type == ARPHRD_ETHER)
			memcpy(neigh->ha, skb->mac.ethernet->h_source, ETH_ALEN);
		goto add_entry;
	}

	return -ENOBUFS;

non_local_input:

#ifdef CONFIG_DECNET_ROUTER
	/*
	 * Destination is another node... find next hop in
	 * routing table here.
	 */
	if (decnet_node_type == DN_RT_INFO_ENDN)
		goto add_entry;


#endif /* CONFIG_DECNET_ROUTER */

add_entry:

	if ((rt = dst_alloc(&dn_dst_ops)) == NULL) {
                neigh_release(neigh);
                return -EINVAL;
        }

	rt->rt_saddr = cb->dst;
	rt->rt_daddr = cb->src;
	rt->rt_oif = 0;
	rt->rt_iif = dev->ifindex;

	rt->u.dst.neighbour = neigh;
	rt->u.dst.dev = dev;
	rt->u.dst.lastuse = jiffies;
	rt->u.dst.output = dnrt_output;
	rt->u.dst.input = dnrt_input;

	dn_insert_route(rt);
	skb->dst = (struct dst_entry *)rt;

	return 0;
}

int dn_route_input(struct sk_buff *skb)
{
	struct dn_route *rt;
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	unsigned hash = dn_hash(cb->src);

	if (skb->dst)
		return 0;

	read_lock_bh(&dn_hash_lock);
	for(rt = dn_route_cache[hash]; rt != NULL; rt = rt->u.rt_next) {
		if ((rt->rt_saddr == cb->dst) &&
				(rt->rt_daddr == cb->src) &&
				(rt->rt_oif == 0) &&
				(rt->rt_iif == cb->iif)) {
			rt->u.dst.lastuse = jiffies;
			dst_hold(&rt->u.dst);
			rt->u.dst.__use++;
			read_unlock_bh(&dn_hash_lock);
			skb->dst = (struct dst_entry *)rt;
			return 0;
		}
	}
	read_unlock_bh(&dn_hash_lock);

	return dn_route_input_slow(skb);
}

#ifdef CONFIG_RTNETLINK
static int dn_rt_fill_info(struct sk_buff *skb, u32 pid, u32 seq, int event, int nowait)
{
	struct dn_route *rt = (struct dn_route *)skb->dst;
	struct rtmsg *r;
	struct nlmsghdr *nlh;
	unsigned char *b = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*r));
	r = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = nowait ? NLM_F_MULTI : 0;
	r->rtm_family = AF_DECnet;
	r->rtm_dst_len = 16;
	r->rtm_src_len = 16;
	r->rtm_tos = 0;
	r->rtm_table = 0;
	r->rtm_type = 0;
	r->rtm_flags = 0;
	r->rtm_scope = RT_SCOPE_UNIVERSE;
	r->rtm_protocol = RTPROT_UNSPEC;
	RTA_PUT(skb, RTA_DST, 2, &rt->rt_daddr);
	RTA_PUT(skb, RTA_SRC, 2, &rt->rt_saddr);
	if (rt->u.dst.dev)
		RTA_PUT(skb, RTA_OIF, sizeof(int), &rt->u.dst.dev->ifindex);
	if (rt->u.dst.window)
		RTA_PUT(skb, RTAX_WINDOW, sizeof(unsigned), &rt->u.dst.window);
	if (rt->u.dst.rtt)
		RTA_PUT(skb, RTAX_RTT, sizeof(unsigned), &rt->u.dst.rtt);

	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
        skb_trim(skb, b - skb->data);
        return -1;
}

/*
 * This is called by both endnodes and routers now.
 */
int dn_cache_getroute(struct sk_buff *in_skb, struct nlmsghdr *nlh, void *arg)
{
	struct rtattr **rta = arg;
	struct dn_route *rt = NULL;
	struct dn_skb_cb *cb;
	dn_address dst = 0;
	dn_address src = 0;
	int iif = 0;
	int err;
	struct sk_buff *skb;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		return -ENOBUFS;
	skb->mac.raw = skb->data;
	cb = (struct dn_skb_cb *)skb->cb;

	if (rta[RTA_SRC-1])
		memcpy(&src, RTA_DATA(rta[RTA_SRC-1]), 2);
	if (rta[RTA_DST-1])
		memcpy(&dst, RTA_DATA(rta[RTA_DST-1]), 2);
	if (rta[RTA_IIF-1])
		memcpy(&iif, RTA_DATA(rta[RTA_IIF-1]), sizeof(int));

	if (iif) {
		struct net_device *dev;
		if ((dev = dev_get_by_index(iif)) == NULL) {
			kfree_skb(skb);
			return -ENODEV;
		}
		if (!dev->dn_ptr) {
			dev_put(dev);
			kfree_skb(skb);
			return -ENODEV;
		}
		skb->protocol = __constant_htons(ETH_P_DNA_RT);
		skb->dev = dev;
		skb->rx_dev = dev;
		cb->src = src;
		cb->dst = dst;
		err = dn_route_input(skb);
		memset(cb, 0, sizeof(struct dn_skb_cb));
		rt = (struct dn_route *)skb->dst;
	} else {
		err = dn_route_output((struct dst_entry **)&rt, dst, src, 0);
	}

	if (!err && rt->u.dst.error)
		err = rt->u.dst.error;
	if (skb->dev)
		dev_put(skb->dev);
	skb->dev = NULL;
	skb->rx_dev = NULL;
	if (err)
		goto out_free;
	skb->dst = &rt->u.dst;

	NETLINK_CB(skb).dst_pid = NETLINK_CB(in_skb).pid;

	err = dn_rt_fill_info(skb, NETLINK_CB(in_skb).pid, nlh->nlmsg_seq, RTM_NEWROUTE, 0);

	if (err == 0)
		goto out_free;
	if (err < 0) {
		err = -EMSGSIZE;
		goto out_free;
	}

	err = netlink_unicast(rtnl, skb, NETLINK_CB(in_skb).pid, MSG_DONTWAIT);

	return err;

out_free:
	kfree_skb(skb);
	return err;
}

/*
 * For routers, this is called from dn_fib_dump, but for endnodes its
 * called directly from the rtnetlink dispatch table.
 */
int dn_cache_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct dn_route *rt;
	int h, s_h;
	int idx, s_idx;

	if (NLMSG_PAYLOAD(cb->nlh, 0) < sizeof(struct rtmsg))
		return -EINVAL;
	if (!(((struct rtmsg *)NLMSG_DATA(cb->nlh))->rtm_flags&RTM_F_CLONED))
		return 0;

	s_h = cb->args[0];
	s_idx = idx = cb->args[1];
	for(h = 0; h < DN_HASHBUCKETS; h++) {
		if (h < s_h)
			continue;
		if (h > s_h)
			s_idx = 0;
		read_lock_bh(&dn_hash_lock);
		for(rt = dn_route_cache[h], idx = 0; rt; rt = rt->u.rt_next, idx++) {
			if (idx < s_idx)
				continue;
			skb->dst = dst_clone(&rt->u.dst);
			if (dn_rt_fill_info(skb, NETLINK_CB(cb->skb).pid,
					cb->nlh->nlmsg_seq, RTM_NEWROUTE, 1) <= 0) {
				dst_release(xchg(&skb->dst, NULL));
				read_unlock_bh(&dn_hash_lock);
				goto done;
			}
			dst_release(xchg(&skb->dst, NULL));
		}
		read_unlock_bh(&dn_hash_lock);
	}

done:
	cb->args[0] = h;
	cb->args[1] = idx;
	return skb->len;
}
#endif /* CONFIG_RTNETLINK */

#ifdef CONFIG_PROC_FS

static int decnet_cache_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
        int len = 0;
        off_t pos = 0;
        off_t begin = 0;
	struct dn_route *rt;
	int i;
	char buf1[DN_ASCBUF_LEN], buf2[DN_ASCBUF_LEN];

	read_lock_bh(&dn_hash_lock);
	for(i = 0; i < DN_HASHBUCKETS; i++) {
		rt = dn_route_cache[i];
		for(; rt != NULL; rt = rt->u.rt_next) {
			len += sprintf(buffer + len, "%-8s %-7s %-7s %04d %04d %04d\n",
					rt->u.dst.dev ? rt->u.dst.dev->name : "*",
					dn_addr2asc(dn_ntohs(rt->rt_daddr), buf1),
					dn_addr2asc(dn_ntohs(rt->rt_saddr), buf2),
					atomic_read(&rt->u.dst.__refcnt),
					rt->u.dst.__use,
					(int)rt->u.dst.rtt
					);

	                pos = begin + len;
	
        	        if (pos < offset) {
                	        len   = 0;
                        	begin = pos;
                	}
              		if (pos > offset + length)
                	        break;
		}
		if (pos > offset + length)
			break;
	}
	read_unlock_bh(&dn_hash_lock);

        *start = buffer + (offset - begin);
        len   -= (offset - begin);

        if (len > length) len = length;

        return(len);
} 

#endif /* CONFIG_PROC_FS */

void __init dn_route_init(void)
{
	memset(dn_route_cache, 0, sizeof(struct dn_route *) * DN_HASHBUCKETS);

	dn_dst_ops.kmem_cachep = kmem_cache_create("dn_dst_cache",
						   sizeof(struct dn_route),
						   0, SLAB_HWCACHE_ALIGN,
						   NULL, NULL);

	dn_route_timer.function = dn_dst_check_expire;
	dn_route_timer.expires = jiffies + decnet_dst_gc_interval * HZ;
	add_timer(&dn_route_timer);

#ifdef CONFIG_PROC_FS
	proc_net_create("decnet_cache",0,decnet_cache_get_info);
#endif /* CONFIG_PROC_FS */
}

#ifdef CONFIG_DECNET_MODULE
void dn_route_cleanup(void)
{
	del_timer(&dn_route_timer);
	dn_run_flush(0);
#ifdef CONFIG_PROC_FS
	proc_net_remove("decnet_cache");
#endif /* CONFIG_PROC_FS */
}
#endif /* CONFIG_DECNET_MODULE */
