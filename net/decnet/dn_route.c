
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
#include <linux/firewall.h>
#include <linux/rtnetlink.h>
#include <linux/string.h>
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
		for(;(rt=*rtp); rtp = &rt->u.rt_next) {
			if (atomic_read(&rt->u.dst.use) ||
					(now - rt->u.dst.lastuse) < expire)
				continue;
			*rtp = rt->u.rt_next;
			rt->u.rt_next = NULL;
			dst_free(&rt->u.dst);
		}

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

	start_bh_atomic();
	for(i = 0; i < DN_HASHBUCKETS; i++) {
		rtp = &dn_route_cache[i];
		for(; (rt=*rtp); rtp = &rt->u.rt_next) {
			if (atomic_read(&rt->u.dst.use) ||
					(now - rt->u.dst.lastuse) < expire)
				continue;
			*rtp = rt->u.rt_next;
			rt->u.rt_next = NULL;
			dst_free(&rt->u.dst);
			break;
		}
	}
	end_bh_atomic();

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

	start_bh_atomic();

	rt->u.rt_next = dn_route_cache[hash];
	dn_route_cache[hash] = rt;
	
	atomic_inc(&rt->u.dst.refcnt);
	atomic_inc(&rt->u.dst.use);
	rt->u.dst.lastuse = now;

	end_bh_atomic();
}

#if defined(CONFIG_DECNET_MODULE)
static void dn_run_flush(unsigned long dummy)
{
	int i;
	struct dn_route *rt, *next;

	for(i = 0; i < DN_HASHBUCKETS; i++) {
		if ((rt = xchg(&dn_route_cache[i], NULL)) == NULL)
			continue;

		for(; rt; rt=next) {
			next = rt->u.rt_next;
			rt->u.rt_next = NULL;
			dst_free((struct dst_entry *)rt);
		}
	}
}
#endif /* CONFIG_DECNET_MODULE */

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

	if (dn_route_input(skb) == 0) {

#ifdef CONFIG_DECNET_FW
		struct neighbour *neigh = skb->dst->neighbour;

		switch(call_in_firewall(PF_DECnet, skb->dev, NULL, NULL, &skb)) {
			case FW_REJECT:
				neigh->ops->error_report(neigh, skb);
				return 0;
			case FW_BLOCK:
			default:
				goto drop_it;
			case FW_ACCEPT:
		}
#endif /* CONFIG_DECNET_FW */

		return skb->dst->input(skb);
	}

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

        if (dn_route_input(skb) == 0) {

#ifdef CONFIG_DECNET_FW
		struct neighbour *neigh = skb->dst->neighbour;

                switch(call_in_firewall(PF_DECnet, skb->dev, NULL, NULL, &skb)) {
			case FW_REJECT:
				neigh->ops->error_report(neigh, skb);
				return 0;
			case FW_BLOCK:
			default:
				goto drop_it;

                        case FW_ACCEPT:
		}
#endif /* CONFIG_DECNET_FW */

		return skb->dst->input(skb);
        }

drop_it:
        kfree_skb(skb);
        return 0;
}

int dn_route_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
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

	if (dn->parms.setsrc)
		dn->parms.setsrc(skb);

	/* printk(KERN_DEBUG "dn_route_rcv: got 0x%02x from %s [%d %d %d]\n", (int)flags,
				(dev) ? dev->name : "???", len, skb->len, padlen);  */

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
	if (net_ratelimit())
		printk(KERN_DEBUG "dn_route_rcv: Dumping packet\n");
	kfree_skb(skb);
	return 0;
}


void dn_send_skb(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct dn_scp *scp = &sk->protinfo.dn;

	if (sk == NULL) {
		dev_queue_xmit(skb);
		return ;
	}

	skb->h.raw = skb->data;

	scp->stamp = jiffies; /* Record time packet was sent */

	/* printk(KERN_DEBUG "dn_send_skb\n"); */

	if (sk->dst_cache && sk->dst_cache->obsolete) {
		dst_release(sk->dst_cache);
		sk->dst_cache = NULL;
	}

	if (sk->dst_cache == NULL) {
		if (dn_route_output(sk) != 0) {
			kfree_skb(skb);
			sk->err = EHOSTUNREACH;
			if (!sk->dead)
				sk->state_change(sk);
			return;
		}
	}

	skb->dst = dst_clone(sk->dst_cache);

	sk->dst_cache->output(skb);
}


static int dn_output(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct dn_route *rt = (struct dn_route *)dst;
	struct device *dev = dst->dev;
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	int err = -EINVAL;

	if (!dst->neighbour)
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

	/*
	 * Filter through the outgoing firewall
	 */
#ifdef CONFIG_DECNET_FW
        switch(call_out_firewall(PF_DECnet, dst->dev, NULL, NULL, &skb)) {
                case FW_REJECT:
			err = -EPERM;
			goto drop;
                case FW_BLOCK:
                default:
                        err = 0;
                        goto drop;
                case FW_ACCEPT:
        }
#endif /* CONFIG_DECNET_FW */

	return dst->neighbour->output(skb);

error:
	if (net_ratelimit())
		printk(KERN_DEBUG "dn_output: This should not happen\n");

#ifdef CONFIG_DECNET_FW
drop:
#endif
	kfree_skb(skb);

	return err;
}

#ifdef CONFIG_DECNET_ROUTER
static int dn_l2_forward(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	struct dst_entry *dst = skb->dst;
	int err = -EINVAL;

	if (!dst->neighbour)
		goto error;

	/*
	 * Hop count exceeded.
	 */
	err = 0;
	if (++cb->hops > 30)
		goto drop;

	/*
	 * Forwarding firewall
	 */
#ifdef CONFIG_DECNET_FW
	switch(call_fw_firewall(PF_DECnet, dst->dev, NULL, NULL, &skb)) {
		case FW_REJECT:
			dst->neighbour->ops->error_report(dst->neighbour, skb);
			return -EPERM;
		case FW_BLOCK:
		default:
			goto drop;
		case FW_ACCEPT:
	}
#endif /* CONFIG_DECNET_FW */

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

#ifdef CONFIG_DECNET_FW
	switch(call_out_firewall(PF_DECnet, dst->dev, NULL, NULL, &skb)) {
		case FW_REJECT:
			dst->neighbour->ops->error_report(dst->neighbour, skb);
			return -EPERM;
		case FW_BLOCK:
		default:
			goto drop;
		case FW_ACCEPT:
	}
#endif /* CONFIG_DECNET_FW */

	return dst->neighbour->output(skb);


error:
	if (net_ratelimit())
		printk(KERN_DEBUG "dn_forward: This should not happen\n");
drop:
	kfree_skb(skb);

	return err;
}

/*
 * Simple frontend to the l2 routing function which filters
 * traffic not in our area when we should only do l1
 * routing.
 */
static int dn_l1_forward(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;

	if ((dn_ntohs(cb->dst ^ decnet_address) & 0xfc00) == 0)
		return dn_l2_forward(skb);

	kfree_skb(skb);
	return 0;
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

static int dn_route_output_slow(struct sock *sk)
{
	struct dn_scp *scp = &sk->protinfo.dn;
	dn_address dest = dn_saddr2dn(&scp->peer);
	struct dn_route *rt = NULL;
	struct device *dev  = decnet_default_device;
	struct neighbour *neigh = NULL;
	struct dn_dev *dn_db;
	unsigned char addr[6];

#ifdef CONFIG_DECNET_ROUTER
	if ((decnet_node_type == DN_RT_INFO_L1RT) || (decnet_node_type == DN_RT_INFO_L2RT)) {
#if 0
		struct dn_fib_ent *fe = dn_fib_lookup(dest, decnet_address);

		if (fe != NULL) {
			neigh = neigh_clone(fe->neigh);
			dn_fib_release(fe);
			goto got_route;
		}
#endif
	}
#endif 

	dn_dn2eth(addr, dest);

	/* Look in On-Ethernet cache first */
	if ((neigh = dn_neigh_lookup(&dn_neigh_table, &addr)) != NULL)
		goto got_route;

	if (dev == NULL)
		return -EINVAL;

	/* FIXME: We need to change this for routing nodes */
	/* Send to default router if that doesn't work */
	if ((neigh = neigh_lookup(&dn_neigh_table, &addr, dev)) != NULL)
		goto got_route;

	/* Send to default device (and hope for the best) if above fail */
	if ((neigh = __neigh_lookup(&dn_neigh_table, &addr, dev, 1)) != NULL)
		goto got_route;


	return -EINVAL;

got_route:

	if ((rt = dst_alloc(sizeof(struct dn_route), &dn_dst_ops)) == NULL) {
		neigh_release(neigh);
		return -EINVAL;
	}

	dn_db = (struct dn_dev *)neigh->dev->dn_ptr;
	
	rt->rt_saddr = decnet_address;
	rt->rt_daddr = dest;
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
	sk->dst_cache = &rt->u.dst;

	return 0;
}

int dn_route_output(struct sock *sk)
{
	struct dn_scp *scp = &sk->protinfo.dn;
	dn_address dest = dn_saddr2dn(&scp->peer);
	unsigned hash = dn_hash(dest);
	struct dn_route *rt = NULL;
	unsigned short src = dn_saddr2dn(&scp->addr);

	start_bh_atomic();
	for(rt = dn_route_cache[hash]; rt; rt = rt->u.rt_next) {
		if ((dest == rt->rt_daddr) &&
				(src == rt->rt_saddr) &&
				(rt->rt_iif == 0) &&
				(rt->rt_oif != 0)) {
			rt->u.dst.lastuse = jiffies;
			atomic_inc(&rt->u.dst.use);
			atomic_inc(&rt->u.dst.refcnt);
			end_bh_atomic();
			sk->dst_cache = &rt->u.dst;
			return 0;
		}
	}
	end_bh_atomic();

	return dn_route_output_slow(sk);
}

static int dn_route_input_slow(struct sk_buff *skb)
{
	struct dn_route *rt = NULL;
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	struct device *dev = skb->dev;
	struct neighbour *neigh = NULL;
	unsigned char addr[6];

	/*
	 * In this case we've just received a packet from a source
	 * outside ourselves pretending to come from us. We don't
	 * allow it any further to prevent routing loops, spoofing and
	 * other nasties. Loopback packets already have the dst attached
	 * so this only affects packets which have originated elsewhere.
	 */
	if (dn_dev_islocal(dev, cb->src))
		return 1;

#ifdef CONFIG_DECNET_ROUTER
	if ((decnet_node_type == DN_RT_INFO_L1RT) || (decnet_node_type == DN_RT_INFO_L2RT)) {
#if 0
		struct dn_fib_ent *fe = NULL;

		fe = dn_fib_lookup(cb->src, cb->dst);

		/* Try routing table first */
		if (fe != NULL) {
			neigh = neigh_clone(fe->neigh);
			dn_fib_release(fe);
			goto got_route;
		}
#endif
	}
#endif

	dn_dn2eth(addr, cb->src);

	/* Now see if we are directly connected */
	if ((neigh = dn_neigh_lookup(&dn_neigh_table, &addr)) != NULL)
		goto got_route;

	if (dev == NULL)
		return -EINVAL;

	/* FIXME: Try the default router here .... */

	if ((neigh = __neigh_lookup(&dn_neigh_table, &addr, dev, 1)) != NULL)
		goto got_route;

	return -EINVAL;

got_route:

	if ((rt = dst_alloc(sizeof(struct dn_route), &dn_dst_ops)) == NULL) {
                neigh_release(neigh);
                return -EINVAL;
        }

	rt->rt_saddr = cb->dst;
	rt->rt_daddr = cb->src;
	rt->rt_oif = 0;
	rt->rt_iif = neigh->dev->ifindex;

	rt->u.dst.neighbour = neigh;
	rt->u.dst.dev = neigh->dev;
	rt->u.dst.lastuse = jiffies;
	rt->u.dst.output = dn_output;

	switch(decnet_node_type) {
		case DN_RT_INFO_ENDN:
			rt->u.dst.input = dn_blackhole;
			break;
#ifdef CONFIG_DECNET_ROUTER
		case DN_RT_INFO_L1RT:
			rt->u.dst.input = dn_l1_forward;
			break;
		case DN_RT_INFO_L2RT:
			rt->u.dst.input = dn_l2_forward;
			break;
#endif /* CONFIG_DECNET_ROUTER */
		default:
			rt->u.dst.input = dn_blackhole;
			if (net_ratelimit())
				printk(KERN_DEBUG "dn_route_input_slow: What kind of node are we?\n");
	}

	if (dn_dev_islocal(dev, cb->dst))
		rt->u.dst.input = dn_nsp_rx;

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

	for(rt = dn_route_cache[hash]; rt != NULL; rt = rt->u.rt_next) {
		if ((rt->rt_saddr == cb->dst) &&
				(rt->rt_daddr == cb->src) &&
				(rt->rt_oif == 0) &&
				(rt->rt_iif == cb->iif)) {
			rt->u.dst.lastuse = jiffies;
			atomic_inc(&rt->u.dst.use);
			atomic_inc(&rt->u.dst.refcnt);
			skb->dst = (struct dst_entry *)rt;
			return 0;
		}
	}

	return dn_route_input_slow(skb);
}

#ifdef CONFIG_DECNET_ROUTER
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

int dn_fib_rtm_getroute(struct sk_buff *in_skb, struct nlmsghdr *nlh, void *arg)
{
	struct rtattr **rta = arg;
	/* struct rtmsg *rtm = NLMSG_DATA(nlh); */
	struct dn_route *rt = NULL;
	dn_address dst = 0;
	dn_address src = 0;
	int iif = 0;
	int err;
	struct sk_buff *skb;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		return -ENOBUFS;
	skb->mac.raw = skb->data;

	if (rta[RTA_SRC-1])
		memcpy(&src, RTA_DATA(rta[RTA_SRC-1]), 2);
	if (rta[RTA_DST-1])
		memcpy(&dst, RTA_DATA(rta[RTA_DST-1]), 2);
	if (rta[RTA_IIF-1])
		memcpy(&iif, RTA_DATA(rta[RTA_IIF-1]), sizeof(int));

	if (iif) {
		struct device *dev;
		if ((dev = dev_get_by_index(iif)) == NULL)
			return -ENODEV;
		if (!dev->dn_ptr)
			return -ENODEV;
		skb->protocol = __constant_htons(ETH_P_DNA_RT);
		skb->dev = dev;
		start_bh_atomic();
		err = dn_route_input(skb);
		end_bh_atomic();
		rt = (struct dn_route *)skb->dst;
		if (!err && rt->u.dst.error)
			err = rt->u.dst.error;
	} else {
		int oif = 0;
		if (rta[RTA_OIF-1])
			memcpy(&oif, RTA_DATA(rta[RTA_OIF-1]), sizeof(int));
		err = -EOPNOTSUPP;
	}
	if (err) {
		kfree_skb(skb);
		return err;
	}
	skb->dst = &rt->u.dst;

	NETLINK_CB(skb).dst_pid = NETLINK_CB(in_skb).pid;

	err = dn_rt_fill_info(skb, NETLINK_CB(in_skb).pid, nlh->nlmsg_seq, RTM_NEWROUTE, 0);

	if (err == 0)
		return 0;
	if (err < 0)
		return -EMSGSIZE;

	err = netlink_unicast(rtnl, skb, NETLINK_CB(in_skb).pid, MSG_DONTWAIT);

	return err;
}
#endif /* CONFIG_RTNETLINK */
#endif /* CONFIG_DECNET_ROUTER */

#ifdef CONFIG_PROC_FS

static int decnet_cache_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
        int len = 0;
        off_t pos = 0;
        off_t begin = 0;
	struct dn_route *rt;
	int i;
	char buf1[DN_ASCBUF_LEN], buf2[DN_ASCBUF_LEN];

	start_bh_atomic();
	for(i = 0; i < DN_HASHBUCKETS; i++) {
		rt = dn_route_cache[i];
		for(; rt != NULL; rt = rt->u.rt_next) {
			len += sprintf(buffer + len, "%-8s %-7s %-7s %04d %04d %04d\n",
					rt->u.dst.dev ? rt->u.dst.dev->name : "*",
					dn_addr2asc(dn_ntohs(rt->rt_daddr), buf1),
					dn_addr2asc(dn_ntohs(rt->rt_saddr), buf2),
					atomic_read(&rt->u.dst.use),
					atomic_read(&rt->u.dst.refcnt),
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
	end_bh_atomic();

        *start = buffer + (offset - begin);
        len   -= (offset - begin);

        if (len > length) len = length;

        return(len);
} 

static struct proc_dir_entry proc_net_decnet_cache = {
        PROC_NET_DN_CACHE, 12, "decnet_cache",
        S_IFREG | S_IRUGO, 1, 0, 0,
        0, &proc_net_inode_operations,
        decnet_cache_get_info
};

#endif /* CONFIG_PROC_FS */

void __init dn_route_init(void)
{
	memset(dn_route_cache, 0, sizeof(struct dn_route *) * DN_HASHBUCKETS);

	dn_route_timer.function = dn_dst_check_expire;
	dn_route_timer.expires = jiffies + decnet_dst_gc_interval * HZ;
	add_timer(&dn_route_timer);

#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_decnet_cache);
#endif /* CONFIG_PROC_FS */
}

#ifdef CONFIG_DECNET_MODULE
void dn_route_cleanup(void)
{
	del_timer(&dn_route_timer);
	dn_run_flush(0);
#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_DN_CACHE);
#endif /* CONFIG_PROC_FS */
}
#endif /* CONFIG_DECNET_MODULE */
