/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		ROUTE - implementation of the IP router.
 *
 * Version:	$Id: route.c,v 1.33 1997/10/24 17:16:08 kuznet Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Linus Torvalds, <Linus.Torvalds@helsinki.fi>
 *		Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Fixes:
 *		Alan Cox	:	Verify area fixes.
 *		Alan Cox	:	cli() protects routing changes
 *		Rui Oliveira	:	ICMP routing table updates
 *		(rco@di.uminho.pt)	Routing table insertion and update
 *		Linus Torvalds	:	Rewrote bits to be sensible
 *		Alan Cox	:	Added BSD route gw semantics
 *		Alan Cox	:	Super /proc >4K 
 *		Alan Cox	:	MTU in route table
 *		Alan Cox	: 	MSS actually. Also added the window
 *					clamper.
 *		Sam Lantinga	:	Fixed route matching in rt_del()
 *		Alan Cox	:	Routing cache support.
 *		Alan Cox	:	Removed compatibility cruft.
 *		Alan Cox	:	RTF_REJECT support.
 *		Alan Cox	:	TCP irtt support.
 *		Jonathan Naylor	:	Added Metric support.
 *	Miquel van Smoorenburg	:	BSD API fixes.
 *	Miquel van Smoorenburg	:	Metrics.
 *		Alan Cox	:	Use __u32 properly
 *		Alan Cox	:	Aligned routing errors more closely with BSD
 *					our system is still very different.
 *		Alan Cox	:	Faster /proc handling
 *	Alexey Kuznetsov	:	Massive rework to support tree based routing,
 *					routing caches and better behaviour.
 *		
 *		Olaf Erb	:	irtt wasn't being copied right.
 *		Bjorn Ekwall	:	Kerneld route support.
 *		Alan Cox	:	Multicast fixed (I hope)
 * 		Pavel Krauz	:	Limited broadcast fixed
 *	Alexey Kuznetsov	:	End of old history. Splitted to fib.c and
 *					route.c and rewritten from scratch.
 *		Andi Kleen	:	Load-limit warning messages.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/pkt_sched.h>
#include <linux/mroute.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/ip_fib.h>
#include <net/arp.h>
#include <net/tcp.h>
#include <net/icmp.h>

#define RTprint(a...)	printk(KERN_DEBUG a)

static struct timer_list rt_flush_timer =
	{ NULL, NULL, RT_FLUSH_DELAY, 0L, NULL };

/*
 *	Interface to generic destination cache.
 */

static void ipv4_dst_destroy(struct dst_entry * dst);
static struct dst_entry * ipv4_dst_check(struct dst_entry * dst, u32);
static struct dst_entry * ipv4_dst_reroute(struct dst_entry * dst,
					   struct sk_buff *);


struct dst_ops ipv4_dst_ops =
{
	AF_INET,
	ipv4_dst_check,
	ipv4_dst_reroute,
	ipv4_dst_destroy
};

__u8 ip_tos2prio[16] = {
	TC_PRIO_FILLER,
	TC_PRIO_BESTEFFORT,
	TC_PRIO_FILLER,
	TC_PRIO_FILLER,
	TC_PRIO_BULK,
	TC_PRIO_FILLER,
	TC_PRIO_BULK,
	TC_PRIO_FILLER,
	TC_PRIO_INTERACTIVE,
	TC_PRIO_FILLER,
	TC_PRIO_INTERACTIVE,
	TC_PRIO_FILLER,
	TC_PRIO_INTERACTIVE_BULK,
	TC_PRIO_FILLER,
	TC_PRIO_INTERACTIVE_BULK,
	TC_PRIO_FILLER
};

/*
 * Route cache.
 */

static atomic_t		 rt_cache_size = ATOMIC_INIT(0);
static struct rtable 	*rt_hash_table[RT_HASH_DIVISOR];

static struct rtable * rt_intern_hash(unsigned hash, struct rtable * rth, u16 protocol);

static __inline__ unsigned rt_hash_code(u32 daddr, u32 saddr, u8 tos)
{
	unsigned hash = ((daddr&0xF0F0F0F0)>>4)|((daddr&0x0F0F0F0F)<<4);
	hash = hash^saddr^tos;
	hash = hash^(hash>>16);
	return (hash^(hash>>8)) & 0xFF;
}

#ifdef CONFIG_PROC_FS

static int rt_cache_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len=0;
	off_t pos=0;
	char temp[129];
	struct rtable *r;
	int i;

	pos = 128;

	if (offset<128)	{
		sprintf(buffer,"%-127s\n", "Iface\tDestination\tGateway \tFlags\t\tRefCnt\tUse\tMetric\tSource\t\tMTU\tWindow\tIRTT\tTOS\tHHRef\tHHUptod\tSpecDst\tHash");
		len = 128;
  	}
	
  	
	start_bh_atomic();

	for (i = 0; i<RT_HASH_DIVISOR; i++) {
		for (r = rt_hash_table[i]; r; r = r->u.rt_next) {
			/*
			 *	Spin through entries until we are ready
			 */
			pos += 128;

			if (pos <= offset) {
				len = 0;
				continue;
			}
					
			sprintf(temp, "%s\t%08lX\t%08lX\t%8X\t%d\t%u\t%d\t%08lX\t%d\t%u\t%u\t%02X\t%d\t%1d\t%08X\t%02X",
				r->u.dst.dev ? r->u.dst.dev->name : "*",
				(unsigned long)r->rt_dst,
				(unsigned long)r->rt_gateway,
				r->rt_flags,
				atomic_read(&r->u.dst.use),
				atomic_read(&r->u.dst.refcnt),
				0,
				(unsigned long)r->rt_src, (int)r->u.dst.pmtu,
				r->u.dst.window,
				(int)r->u.dst.rtt, r->key.tos,
				r->u.dst.hh ? atomic_read(&r->u.dst.hh->hh_refcnt) : -1,
				r->u.dst.hh ? r->u.dst.hh->hh_uptodate : 0,
				r->rt_spec_dst,
				i);
			sprintf(buffer+len,"%-127s\n",temp);
			len += 128;
			if (pos >= offset+length)
				goto done;
		}
        }

done:
	end_bh_atomic();
  	
  	*start = buffer+len-(pos-offset);
  	len = pos-offset;
  	if (len>length)
  		len = length;
  	return len;
}
#endif
  
static void __inline__ rt_free(struct rtable *rt)
{
	dst_free(&rt->u.dst);
}


void ip_rt_check_expire()
{
	int i;
	static int rover;
	struct rtable *rth, **rthp;
	unsigned long now = jiffies;

	for (i=0; i<RT_HASH_DIVISOR/5; i++) {
		rover = (rover + 1) & (RT_HASH_DIVISOR-1);
		rthp = &rt_hash_table[rover];

		while ((rth = *rthp) != NULL) {
			struct rtable * rth_next = rth->u.rt_next;

			/*
			 * Cleanup aged off entries.
			 */

			if (!atomic_read(&rth->u.dst.use) &&
			    (now - rth->u.dst.lastuse > RT_CACHE_TIMEOUT)) {
				*rthp = rth_next;
				atomic_dec(&rt_cache_size);
#if RT_CACHE_DEBUG >= 2
				printk("rt_check_expire clean %02x@%08x\n", rover, rth->rt_dst);
#endif
				rt_free(rth);
				continue;
			}

			if (!rth_next)
				break;

			if ( rth_next->u.dst.lastuse - rth->u.dst.lastuse > RT_CACHE_BUBBLE_THRESHOLD ||
			    (rth->u.dst.lastuse - rth_next->u.dst.lastuse < 0 &&
			     atomic_read(&rth->u.dst.refcnt) < atomic_read(&rth_next->u.dst.refcnt))) {
#if RT_CACHE_DEBUG >= 2
				printk("rt_check_expire bubbled %02x@%08x<->%08x\n", rover, rth->rt_dst, rth_next->rt_dst);
#endif
				*rthp = rth_next;
 				rth->u.rt_next = rth_next->u.rt_next;
				rth_next->u.rt_next = rth;
				rthp = &rth_next->u.rt_next;
				continue;
			}
			rthp = &rth->u.rt_next;
		}
	}
}

static void rt_run_flush(unsigned long dummy)
{
	int i;
	struct rtable * rth, * next;

	for (i=0; i<RT_HASH_DIVISOR; i++) {
		int nr=0;

		cli();
		if (!(rth = rt_hash_table[i])) {
			sti();
			continue;
		}

		rt_hash_table[i] = NULL;
		sti();

		for (; rth; rth=next) {
			next = rth->u.rt_next;
			atomic_dec(&rt_cache_size);
			nr++;
			rth->u.rt_next = NULL;
			rt_free(rth);
		}
#if RT_CACHE_DEBUG >= 2
		if (nr > 0)
			printk("rt_cache_flush: %d@%02x\n", nr, i);
#endif
	}
}
  
void rt_cache_flush(int delay)
{
	start_bh_atomic();
	if (delay && rt_flush_timer.function &&
	    rt_flush_timer.expires - jiffies < delay) {
		end_bh_atomic();
		return;
	}
	if (rt_flush_timer.function) {
		del_timer(&rt_flush_timer);
		rt_flush_timer.function = NULL;
	}
	if (delay == 0) {
		end_bh_atomic();
		rt_run_flush(0);
		return;
	}
	rt_flush_timer.function = rt_run_flush;
	rt_flush_timer.expires = jiffies + delay;
	add_timer(&rt_flush_timer);
	end_bh_atomic();
}


static void rt_garbage_collect(void)
{
	int i;
	static unsigned expire = RT_CACHE_TIMEOUT>>1;
	static unsigned long last_gc;
	struct rtable *rth, **rthp;
	unsigned long now;

	start_bh_atomic();
	now = jiffies;

	/*
	 * Garbage collection is pretty expensive,
	 * do not make it too frequently, but just increase expire strength.
	 */
	if (now - last_gc < 1*HZ) {
		expire >>= 1;
		end_bh_atomic();
		return;
	}

	expire++;

	for (i=0; i<RT_HASH_DIVISOR; i++) {
		if (!rt_hash_table[i])
			continue;
		for (rthp=&rt_hash_table[i]; (rth=*rthp); rthp=&rth->u.rt_next)	{
			if (atomic_read(&rth->u.dst.use) ||
			    now - rth->u.dst.lastuse < expire)
				continue;
			atomic_dec(&rt_cache_size);
			*rthp = rth->u.rt_next;
			rth->u.rt_next = NULL;
			rt_free(rth);
			break;
		}
	}

	last_gc = now;
	if (atomic_read(&rt_cache_size) < RT_CACHE_MAX_SIZE)
		expire = RT_CACHE_TIMEOUT>>1;
	else
		expire >>= 1;
	end_bh_atomic();
}

static int rt_ll_bind(struct rtable *rt)
{
	struct neighbour *neigh;
	struct hh_cache	*hh = NULL;

	if (rt->u.dst.dev && rt->u.dst.dev->hard_header_cache) {
		neigh = rt->u.dst.neighbour;
		if (!neigh)
			neigh = arp_find_neighbour(&rt->u.dst, 1);

		if (neigh) {
			rt->u.dst.neighbour = neigh;
			for (hh=neigh->hh; hh; hh = hh->hh_next)
				if (hh->hh_type == ETH_P_IP)
					break;
		}

		if (!hh && (hh = kmalloc(sizeof(*hh), GFP_ATOMIC)) != NULL) {
#if RT_CACHE_DEBUG >= 2
			extern atomic_t hh_count;
			atomic_inc(&hh_count);
#endif
			memset(hh, 0, sizeof(struct hh_cache));
			hh->hh_type = ETH_P_IP;
			atomic_set(&hh->hh_refcnt, 0);
			hh->hh_next = NULL;
			if (rt->u.dst.dev->hard_header_cache(&rt->u.dst, neigh, hh)) {
				kfree(hh);
#if RT_CACHE_DEBUG >= 2
				atomic_dec(&hh_count);
#endif
				hh = NULL;
			} else if (neigh) {
				atomic_inc(&hh->hh_refcnt);
				hh->hh_next = neigh->hh;
				neigh->hh = hh;
			}
		}
		if (hh)	{
			atomic_inc(&hh->hh_refcnt);
			rt->u.dst.hh = hh;
			return hh->hh_uptodate;
		}
	}
	return 0;
}


static struct rtable *rt_intern_hash(unsigned hash, struct rtable * rt, u16 protocol)
{
	struct rtable	*rth, **rthp;
	unsigned long	now = jiffies;

	rt->u.dst.priority = rt_tos2priority(rt->key.tos);

	start_bh_atomic();

	rthp = &rt_hash_table[hash];

	while ((rth = *rthp) != NULL) {
		if (memcmp(&rth->key, &rt->key, sizeof(rt->key)) == 0) {
			/* Put it first */
			*rthp = rth->u.rt_next;
			rth->u.rt_next = rt_hash_table[hash];
			rt_hash_table[hash] = rth;

			atomic_inc(&rth->u.dst.refcnt);
			atomic_inc(&rth->u.dst.use);
			rth->u.dst.lastuse = now;
			end_bh_atomic();

			ip_rt_put(rt);
			rt_free(rt);
			return rth;
		}

		rthp = &rth->u.rt_next;
	}

	if (atomic_read(&rt_cache_size) >= RT_CACHE_MAX_SIZE)
		rt_garbage_collect();

	rt->u.rt_next = rt_hash_table[hash];
#if RT_CACHE_DEBUG >= 2
	if (rt->u.rt_next) {
		struct rtable * trt;
		printk("rt_cache @%02x: %08x", hash, rt->rt_dst);
		for (trt=rt->u.rt_next; trt; trt=trt->u.rt_next)
			printk(" . %08x", trt->rt_dst);
		printk("\n");
	}
#endif
	rt_hash_table[hash] = rt;
	atomic_inc(&rt_cache_size);

	if (protocol == ETH_P_IP)
		rt_ll_bind(rt);

	end_bh_atomic();
	return rt;
}

void ip_rt_redirect(u32 old_gw, u32 daddr, u32 new_gw,
		    u32 saddr, u8 tos, struct device *dev)
{
	int i, k;
	struct in_device *in_dev = dev->ip_ptr;
	struct rtable *rth, **rthp;
	u32  skeys[2] = { saddr, 0 };
	int  ikeys[2] = { dev->ifindex, 0 };

	tos &= IPTOS_TOS_MASK;

	if (!in_dev || new_gw == old_gw || !IN_DEV_RX_REDIRECTS(in_dev)
	    || MULTICAST(new_gw) || BADCLASS(new_gw) || ZERONET(new_gw))
		goto reject_redirect;

	if (!IN_DEV_SHARED_MEDIA(in_dev)) {
		if (ip_fib_check_default(new_gw, dev))
			goto reject_redirect;
	} else {
		if (inet_addr_type(new_gw) != RTN_UNICAST)
			goto reject_redirect;
	}

	for (i=0; i<2; i++) {
		for (k=0; k<2; k++) {
			unsigned hash = rt_hash_code(daddr, skeys[i]^(ikeys[k]<<5), tos);

			rthp=&rt_hash_table[hash];

			while ( (rth = *rthp) != NULL) {
				struct rtable *rt;

				if (rth->key.dst != daddr ||
				    rth->key.src != skeys[i] ||
				    rth->key.tos != tos ||
				    rth->key.oif != ikeys[k] ||
				    rth->key.iif != 0) {
					rthp = &rth->u.rt_next;
					continue;
				}

				if (rth->rt_dst != daddr ||
				    rth->rt_src != saddr ||
				    rth->u.dst.error ||
				    rth->rt_gateway != old_gw ||
				    rth->u.dst.dev != dev)
					break;

				rt = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
				if (rt == NULL)
					return;

				/*
				 * Copy all the information.
				 */
				*rt = *rth;
				atomic_set(&rt->u.dst.refcnt, 1);
				atomic_set(&rt->u.dst.use, 1);
				rt->u.dst.lastuse = jiffies;
				rt->u.dst.neighbour = NULL;
				rt->u.dst.hh = NULL;

				rt->rt_flags |= RTCF_REDIRECTED;

				/* Gateway is different ... */
				rt->rt_gateway = new_gw;

				if (!rt_ll_bind(rt)) {
					ip_rt_put(rt);
					rt_free(rt);
					break;
				}

				*rthp = rth->u.rt_next;
				rt_free(rth);
				rt = rt_intern_hash(hash, rt, ETH_P_IP);
				ip_rt_put(rt);
				break;
			}
		}
	}
	return;

reject_redirect:
#ifdef CONFIG_IP_ROUTE_VERBOSE
	if (ipv4_config.log_martians && net_ratelimit())
		printk(KERN_INFO "Redirect from %lX/%s to %lX ignored."
		       "Path = %lX -> %lX, tos %02x\n",
		       ntohl(old_gw), dev->name, ntohl(new_gw),
		       ntohl(saddr), ntohl(daddr), tos);
#endif
}


void ip_rt_advice(struct rtable **rp, int advice)
{
	struct rtable *rt;

	if (advice)
		return;

	start_bh_atomic();
	if ((rt = *rp) != NULL && (rt->rt_flags&RTCF_REDIRECTED)) {
#if RT_CACHE_DEBUG >= 1
		printk(KERN_DEBUG "ip_rt_advice: redirect to %08x/%02x dropped\n", rt->rt_dst, rt->key.tos);
#endif
		*rp = NULL;
		ip_rt_put(rt);
		rt_cache_flush(0);
	}
	end_bh_atomic();
	return;
}

/*
 * Algorithm:
 *	1. The first RT_REDIRECT_NUMBER redirects are sent
 *	   with exponential backoff, then we stop sending them at all,
 *	   assuming that the host ignores our redirects.
 *	2. If we did not see packets requiring redirects
 *	   during RT_REDIRECT_SILENCE, we assume that the host
 *	   forgot redirected route and start to send redirects again.
 *
 * This algorithm is much cheaper and more intelligent than dumb load limiting
 * in icmp.c.
 *
 * NOTE. Do not forget to inhibit load limiting for redirects (redundant)
 * and "frag. need" (breaks PMTU discovery) in icmp.c.
 */

void ip_rt_send_redirect(struct sk_buff *skb)
{
	struct rtable *rt = (struct rtable*)skb->dst;

	/* No redirected packets during RT_REDIRECT_SILENCE;
	 * reset the algorithm.
	 */
	if (jiffies - rt->last_error > RT_REDIRECT_SILENCE)
		rt->errors = 0;

	/* Too many ignored redirects; do not send anything
	 * set last_error to the last seen redirected packet.
	 */
	if (rt->errors >= RT_REDIRECT_NUMBER) {
		rt->last_error = jiffies;
		return;
	}

	/* Check for load limit; set last_error to the latest sent
	 * redirect.
	 */
	if (jiffies - rt->last_error > (RT_REDIRECT_LOAD<<rt->errors)) {
		icmp_send(skb, ICMP_REDIRECT, ICMP_REDIR_HOST, rt->rt_gateway);
		rt->last_error = jiffies;
		++rt->errors;
#ifdef CONFIG_IP_ROUTE_VERBOSE
		if (ipv4_config.log_martians && rt->errors == RT_REDIRECT_NUMBER && net_ratelimit())
			printk(KERN_WARNING "host %08x/if%d ignores redirects for %08x to %08x.\n",
			       rt->rt_src, rt->rt_iif, rt->rt_dst, rt->rt_gateway);
#endif
	}
}

static int ip_error(struct sk_buff *skb)
{
	struct rtable *rt = (struct rtable*)skb->dst;
	int code;

	switch (rt->u.dst.error) {
	case EINVAL:
	default:
		kfree_skb(skb, FREE_READ);
		return 0;
	case EHOSTUNREACH:
		code = ICMP_HOST_UNREACH;
		break;
	case ENETUNREACH:
		code = ICMP_NET_UNREACH;
		break;
	case EACCES:
		code = ICMP_PKT_FILTERED;
		break;
	}
	if (jiffies - rt->last_error > RT_ERROR_LOAD) {
		icmp_send(skb, ICMP_DEST_UNREACH, code, 0);
		rt->last_error = jiffies;
	}
	kfree_skb(skb, FREE_READ);
	return 0;
} 

/*
 *	The last two values are not from the RFC but
 *	are needed for AMPRnet AX.25 paths.
 */

static unsigned short mtu_plateau[] =
{32000, 17914, 8166, 4352, 2002, 1492, 576, 296, 216, 128 };

static __inline__ unsigned short guess_mtu(unsigned short old_mtu)
{
	int i;
	
	for (i = 0; i < sizeof(mtu_plateau)/sizeof(mtu_plateau[0]); i++)
		if (old_mtu > mtu_plateau[i])
			return mtu_plateau[i];
	return 68;
}

unsigned short ip_rt_frag_needed(struct iphdr *iph, unsigned short new_mtu)
{
	int i;
	unsigned short old_mtu = ntohs(iph->tot_len);
	struct rtable *rth;
	u32  skeys[2] = { iph->saddr, 0, };
	u32  daddr = iph->daddr;
	u8   tos = iph->tos & IPTOS_TOS_MASK;
	unsigned short est_mtu = 0;

	if (ipv4_config.no_pmtu_disc)
		return 0;

	for (i=0; i<2; i++) {
		unsigned hash = rt_hash_code(daddr, skeys[i], tos);

		for (rth = rt_hash_table[hash]; rth; rth = rth->u.rt_next) {
			if (rth->key.dst == daddr &&
			    rth->key.src == skeys[i] &&
			    rth->rt_dst == daddr &&
			    rth->rt_src == iph->saddr &&
			    rth->key.tos == tos &&
			    rth->key.iif == 0 &&
			    !(rth->rt_flags&RTCF_NOPMTUDISC)) {
				unsigned short mtu = new_mtu;

				if (new_mtu < 68 || new_mtu >= old_mtu) {

					/* BSD 4.2 compatibility hack :-( */
					if (mtu == 0 && old_mtu >= rth->u.dst.pmtu &&
					    old_mtu >= 68 + (iph->ihl<<2))
						old_mtu -= iph->ihl<<2;

					mtu = guess_mtu(old_mtu);
				}
				if (mtu < rth->u.dst.pmtu) {
					rth->u.dst.pmtu = mtu;
					est_mtu = mtu;
				}
			}
		}
	}
	return est_mtu;
}


static void ipv4_dst_destroy(struct dst_entry * dst)
{
	struct rtable * rt = (struct rtable*)dst;
	struct hh_cache * hh = rt->u.dst.hh;
	rt->u.dst.hh = NULL;
	if (hh && atomic_dec_and_test(&hh->hh_refcnt)) {
#if RT_CACHE_DEBUG >= 2
		extern atomic_t hh_count;
		atomic_dec(&hh_count);
#endif
		kfree(hh);
	}
}

static struct dst_entry * ipv4_dst_check(struct dst_entry * dst, u32 cookie)
{
	return NULL;
}

static struct dst_entry * ipv4_dst_reroute(struct dst_entry * dst,
					   struct sk_buff *skb)
{
	return NULL;
}

static int ip_rt_bug(struct sk_buff *skb)
{
	printk(KERN_DEBUG "ip_rt_bug: %08x -> %08x, %s\n", skb->nh.iph->saddr,
	       skb->nh.iph->daddr, skb->dev ? skb->dev->name : "?");
	kfree_skb(skb, FREE_WRITE);
	return 0;
}

/*
   We do not cache source address of outgoing interface,
   because it is used only by IP RR, TS and SRR options,
   so that it out of fast path.

   BTW remember: "addr" is allowed to be not aligned
   in IP options!
 */

void ip_rt_get_source(u8 *addr, struct rtable *rt)
{
	u32 src;
	struct fib_result res;

	if (rt->key.iif == 0) {
		memcpy(addr, &rt->rt_src, 4);
		return;
	}
	if (fib_lookup(&rt->key, &res) == 0) {
		src = FIB_RES_PREFSRC(res);
		memcpy(addr, &src, 4);
		return;
	}
	src = inet_select_addr(rt->u.dst.dev, rt->rt_gateway, RT_SCOPE_UNIVERSE);
	memcpy(addr, &src, 4);
}

static int
ip_route_input_mc(struct sk_buff *skb, u32 daddr, u32 saddr,
		  u8 tos, struct device *dev, int our)
{
	unsigned hash;
	struct rtable *rth;
	u32 spec_dst;
	struct in_device *in_dev = dev->ip_ptr;

	/* Primary sanity checks. */

	if (MULTICAST(saddr) || BADCLASS(saddr) || LOOPBACK(saddr) ||
	    in_dev == NULL || skb->protocol != __constant_htons(ETH_P_IP))
		return -EINVAL;

	if (ZERONET(saddr)) {
		if (!LOCAL_MCAST(daddr))
			return -EINVAL;
		spec_dst = inet_select_addr(dev, 0, RT_SCOPE_LINK);
	} else if (fib_validate_source(saddr, 0, tos, 0, dev, &spec_dst) < 0)
		return -EINVAL;

	rth = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
	if (!rth)
		return -ENOBUFS;

	rth->u.dst.output= ip_rt_bug;

	atomic_set(&rth->u.dst.use, 1);
	rth->key.dst	= daddr;
	rth->rt_dst	= daddr;
	rth->key.tos	= tos;
	rth->key.src	= saddr;
	rth->rt_src	= saddr;
#ifdef CONFIG_IP_ROUTE_NAT
	rth->rt_dst_map	= daddr;
	rth->rt_src_map	= saddr;
#endif
	rth->rt_iif	=
	rth->key.iif	= dev->ifindex;
	rth->u.dst.dev	= &loopback_dev;
	rth->key.oif	= 0;
	rth->rt_gateway	= daddr;
	rth->rt_spec_dst= spec_dst;
	rth->rt_type	= RTN_MULTICAST;
	rth->rt_flags	= RTCF_MULTICAST;
	if (our) {
		rth->u.dst.input= ip_local_deliver;
		rth->rt_flags |= RTCF_LOCAL;
	}

#ifdef CONFIG_IP_MROUTE
	if (!LOCAL_MCAST(daddr) && IN_DEV_MFORWARD(in_dev))
		rth->u.dst.input = ip_mr_input;
#endif

	hash = rt_hash_code(daddr, saddr^(dev->ifindex<<5), tos);
	skb->dst = (struct dst_entry*)rt_intern_hash(hash, rth, 0);
	return 0;
}

/*
 *	NOTE. We drop all the packets that has local source
 *	addresses, because every properly looped back packet
 *	must have correct destination already attached by output routine.
 *
 *	Such approach solves two big problems:
 *	1. Not simplex devices are handled properly.
 *	2. IP spoofing attempts are filtered with 100% of guarantee.
 */

int ip_route_input_slow(struct sk_buff *skb, u32 daddr, u32 saddr,
			u8 tos, struct device *dev)
{
	struct rt_key	key;
	struct fib_result res;
	struct in_device *in_dev = dev->ip_ptr;
	struct in_device *out_dev;
	unsigned	flags = 0;
	struct rtable * rth;
	unsigned	hash;
	u32		spec_dst;
	int		err = -EINVAL;

	/*
	 *	IP on this device is disabled.
	 */

	if (!in_dev)
		return -EINVAL;

	key.dst = daddr;
	key.src = saddr;
	key.tos = tos;
	key.iif = dev->ifindex;
	key.oif = 0;
	key.scope = RT_SCOPE_UNIVERSE;

	hash = rt_hash_code(daddr, saddr^(key.iif<<5), tos);

	/* Check for the most weird martians, which can be not detected
	   by fib_lookup.
	 */

	if (MULTICAST(saddr) || BADCLASS(saddr) || LOOPBACK(saddr))
		goto martian_source;

	if (daddr == 0xFFFFFFFF)
		goto brd_input;

	/* Accept zero addresses only to limited broadcast;
	 * I even do not know to fix it or not. Waiting for complains :-)
	 */
	if (ZERONET(saddr))
		goto martian_source;

	if (BADCLASS(daddr) || ZERONET(daddr) || LOOPBACK(daddr))
		goto martian_destination;

	/*
	 *	Now we are ready to route packet.
	 */
	if ((err = fib_lookup(&key, &res))) {
		if (!IN_DEV_FORWARD(in_dev))
			return -EINVAL;
		goto no_route;
	}

#ifdef CONFIG_IP_ROUTE_NAT
	/* Policy is applied before mapping destination,
	   but rerouting after map should be made with old source.
	 */

	if (1) {
		u32 src_map = saddr;
		if (res.r)
			src_map = fib_rules_policy(saddr, &res, &flags);

		if (res.type == RTN_NAT) {
			key.dst = fib_rules_map_destination(daddr, &res);
			if (fib_lookup(&key, &res) || res.type != RTN_UNICAST)
				return -EINVAL;
			flags |= RTCF_DNAT;
		}
		key.src = src_map;
	}
#endif

	if (res.type == RTN_BROADCAST)
		goto brd_input;

	if (res.type == RTN_LOCAL) {
		spec_dst = daddr;
		if (inet_addr_type(saddr) != RTN_UNICAST)
			goto martian_source;
		goto local_input;
	}

	if (!IN_DEV_FORWARD(in_dev))
		return -EINVAL;
	if (res.type != RTN_UNICAST)
		goto martian_destination;

#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (res.fi->fib_nhs > 1 && key.oif == 0)
		fib_select_multipath(&key, &res);
#endif
	out_dev = FIB_RES_DEV(res)->ip_ptr;

	err = fib_validate_source(saddr, daddr, tos, FIB_RES_OIF(res), dev, &spec_dst);
	if (err < 0)
		goto martian_source;

	if (err)
		flags |= RTCF_DIRECTSRC;

	if (out_dev == in_dev && err && !(flags&RTCF_NAT) &&
	    (IN_DEV_SHARED_MEDIA(out_dev)
	     || inet_addr_onlink(out_dev, saddr, FIB_RES_GW(res))))
		flags |= RTCF_DOREDIRECT;

	if (skb->protocol != __constant_htons(ETH_P_IP)) {
		/* Not IP (i.e. ARP). Do not make route for invalid
		 * destination or if it is redirected.
		 */
		if (out_dev == in_dev && flags&RTCF_DOREDIRECT)
			return -EINVAL;
	}

	rth = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
	if (!rth)
		return -ENOBUFS;

	atomic_set(&rth->u.dst.use, 1);
	rth->key.dst	= daddr;
	rth->rt_dst	= daddr;
	rth->key.tos	= tos;
	rth->key.src	= saddr;
	rth->rt_src	= saddr;
	rth->rt_gateway	= daddr;
#ifdef CONFIG_IP_ROUTE_NAT
	rth->rt_src_map	= key.src;
	rth->rt_dst_map	= key.dst;
	if (flags&RTCF_DNAT)
		rth->rt_gateway	= key.dst;
#endif
	rth->rt_iif 	=
	rth->key.iif	= dev->ifindex;
	rth->u.dst.dev	= out_dev->dev;
	rth->key.oif 	= 0;
	rth->rt_spec_dst= spec_dst;

	rth->u.dst.input = ip_forward;
	rth->u.dst.output = ip_output;

	rth->u.dst.pmtu	= res.fi->fib_mtu ? : out_dev->dev->mtu;
	rth->u.dst.window=res.fi->fib_window ? : 0;
	rth->u.dst.rtt	= res.fi->fib_rtt ? : TCP_TIMEOUT_INIT;
	if (FIB_RES_GW(res) && FIB_RES_NH(res).nh_scope == RT_SCOPE_LINK)
		rth->rt_gateway	= FIB_RES_GW(res);

	rth->rt_flags = flags;
	rth->rt_type = res.type;

	skb->dst = (struct dst_entry*)rt_intern_hash(hash, rth, ntohs(skb->protocol));
	return 0;

brd_input:
	if (skb->protocol != __constant_htons(ETH_P_IP))
		return -EINVAL;

	if (ZERONET(saddr)) {
		spec_dst = inet_select_addr(dev, 0, RT_SCOPE_LINK);
	} else {
		err = fib_validate_source(saddr, 0, tos, 0, dev, &spec_dst);
		if (err < 0)
			goto martian_source;
		if (err)
			flags |= RTCF_DIRECTSRC;
	}
	flags |= RTCF_BROADCAST;

local_input:
	rth = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
	if (!rth)
		return -ENOBUFS;

	rth->u.dst.output= ip_rt_bug;

	atomic_set(&rth->u.dst.use, 1);
	rth->key.dst	= daddr;
	rth->rt_dst	= daddr;
	rth->key.tos	= tos;
	rth->key.src	= saddr;
	rth->rt_src	= saddr;
#ifdef CONFIG_IP_ROUTE_NAT
	rth->rt_dst_map	= key.dst;
	rth->rt_src_map	= key.src;
#endif
	rth->rt_iif	=
	rth->key.iif	= dev->ifindex;
	rth->u.dst.dev	= &loopback_dev;
	rth->key.oif 	= 0;
	rth->rt_gateway	= daddr;
	rth->rt_spec_dst= spec_dst;
	rth->u.dst.input= ip_local_deliver;
	if (res.type == RTN_UNREACHABLE) {
		rth->u.dst.input= ip_error;
		rth->u.dst.error= err;
	}
	rth->rt_flags 	= flags|RTCF_LOCAL;
	rth->rt_type	= res.type;
	skb->dst = (struct dst_entry*)rt_intern_hash(hash, rth, 0);
	return 0;

no_route:
	spec_dst = inet_select_addr(dev, 0, RT_SCOPE_UNIVERSE);
	res.type = RTN_UNREACHABLE;
	goto local_input;

	/*
	 *	Do not cache martian addresses: they should be logged (RFC1812)
	 */
martian_destination:
#ifdef CONFIG_IP_ROUTE_VERBOSE
	if (ipv4_config.log_martians && net_ratelimit())
		printk(KERN_WARNING "martian destination %08x from %08x, dev %s\n", daddr, saddr, dev->name);
#endif
	return -EINVAL;

martian_source:
#ifdef CONFIG_IP_ROUTE_VERBOSE
	if (ipv4_config.log_martians && net_ratelimit()) {
		/*
		 *	RFC1812 recommenadtion, if source is martian,
		 *	the only hint is MAC header.
		 */
		printk(KERN_WARNING "martian source %08x for %08x, dev %s\n", saddr, daddr, dev->name);
		if (dev->hard_header_len) {
			int i;
			unsigned char *p = skb->mac.raw;
			printk(KERN_WARNING "ll header:");
			for (i=0; i<dev->hard_header_len; i++, p++)
				printk(" %02x", *p);
			printk("\n");
		}
	}
#endif
	return -EINVAL;
}

int ip_route_input(struct sk_buff *skb, u32 daddr, u32 saddr,
		   u8 tos, struct device *dev)
{
	struct rtable * rth;
	unsigned	hash;
	int iif = dev->ifindex;

	tos &= IPTOS_TOS_MASK;
	hash = rt_hash_code(daddr, saddr^(iif<<5), tos);

	for (rth=rt_hash_table[hash]; rth; rth=rth->u.rt_next) {
		if (rth->key.dst == daddr &&
		    rth->key.src == saddr &&
		    rth->key.iif == iif &&
		    rth->key.oif == 0 &&
		    rth->key.tos == tos) {
			rth->u.dst.lastuse = jiffies;
			atomic_inc(&rth->u.dst.use);
			atomic_inc(&rth->u.dst.refcnt);
			skb->dst = (struct dst_entry*)rth;
			return 0;
		}
	}

	/* Multicast recognition logic is moved from route cache to here.
	   The problem was that too many ethernet cards have broken/missing
	   hardware multicast filters :-( As result the host on multicasting
	   network acquires a lot of useless route cache entries, sort of
	   SDR messages from all the world. Now we try to get rid of them.
	   Really, provided software IP multicast filter is organized
	   reasonably (at least, hashed), it does not result in a slowdown
	   comparing with route cache reject entries.
	   Note, that multicast routers are not affected, because
	   route cache entry is created eventually.
	 */
	if (MULTICAST(daddr)) {
		int our = ip_check_mc(dev, daddr);
		if (!our
#ifdef CONFIG_IP_MROUTE
		    && (LOCAL_MCAST(daddr) || !dev->ip_ptr ||
			!IN_DEV_MFORWARD((struct in_device*)dev->ip_ptr))
#endif
		    ) return -EINVAL;
		return ip_route_input_mc(skb, daddr, saddr, tos, dev, our);
	}
	return ip_route_input_slow(skb, daddr, saddr, tos, dev);
}

/*
 * Major route resolver routine.
 */

int ip_route_output_slow(struct rtable **rp, u32 daddr, u32 saddr, u8 tos, int oif)
{
	struct rt_key key;
	struct fib_result res;
	unsigned flags = 0;
	struct rtable *rth;
	struct device *dev_out = NULL;
	unsigned hash;

	tos &= IPTOS_TOS_MASK|1;
	key.dst = daddr;
	key.src = saddr;
	key.tos = tos&IPTOS_TOS_MASK;
	key.iif = loopback_dev.ifindex;
	key.oif = oif;
	key.scope = (tos&1) ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
	res.fi = NULL;

	if (saddr) {
		if (MULTICAST(saddr) || BADCLASS(saddr) || ZERONET(saddr))
			return -EINVAL;

		/* It is equivalent to inet_addr_type(saddr) == RTN_LOCAL */
		dev_out = ip_dev_find(saddr);
		if (dev_out == NULL)
			return -EINVAL;

		/* I removed check for oif == dev_out->oif here.
		   It was wrong by three reasons:
		   1. ip_dev_find(saddr) can return wrong iface, if saddr is
		      assigned to multiple interfaces.
		   2. Moreover, we are allowed to send packets with saddr
		      of another iface. --ANK
		 */

		if (oif == 0 && (MULTICAST(daddr) || daddr == 0xFFFFFFFF)) {
			/* Special hack: user can direct multicasts
			   and limited broadcast via necessary interface
			   without fiddling with IP_MULTICAST_IF or IP_TXINFO.
			   This hack is not just for fun, it allows
			   vic,vat and friends to work.
			   They bind socket to loopback, set ttl to zero
			   and expect that it will work.
			   From the viewpoint of routing cache they are broken,
			   because we are not allowed to build multicast path
			   with loopback source addr (look, routing cache
			   cannot know, that ttl is zero, so that packet
			   will not leave this host and route is valid).
			   Luckily, this hack is good workaround.
			 */

			key.oif = dev_out->ifindex;
			goto make_route;
		}
		dev_out = NULL;
	}
	if (oif) {
		dev_out = dev_get_by_index(oif);
		if (dev_out == NULL)
			return -ENODEV;
		if (dev_out->ip_ptr == NULL)
			return -ENODEV;	/* Wrong error code */

		if (LOCAL_MCAST(daddr) || daddr == 0xFFFFFFFF) {
			key.src = inet_select_addr(dev_out, 0, RT_SCOPE_LINK);
			goto make_route;
		}
		if (MULTICAST(daddr)) {
			key.src = inet_select_addr(dev_out, 0, key.scope);
			goto make_route;
		}
		if (!daddr)
			key.src = inet_select_addr(dev_out, 0, RT_SCOPE_HOST);
	}

	if (!key.dst) {
		key.dst = key.src;
		if (!key.dst)
			key.dst = key.src = htonl(INADDR_LOOPBACK);
		dev_out = &loopback_dev;
		key.oif = loopback_dev.ifindex;
		flags |= RTCF_LOCAL;
		goto make_route;
	}

	if (fib_lookup(&key, &res)) {
		res.fi = NULL;
		if (oif) {
			/* Apparently, routing tables are wrong. Assume,
			   that the destination is on link.

			   WHY? DW.
			   Because we are allowed to send to iface
			   even if it has NO routes and NO assigned
			   addresses. When oif is specified, routing
			   tables are looked up with only one purpose:
			   to catch if destination is gatewayed, rather than
			   direct. Moreover, if MSG_DONTROUTE is set,
			   we send packet, no matter of routing tables
			   of ifaddr state. --ANK


			   We could make it even if oif is unknown,
			   likely IPv6, but we do not.
			 */

			printk(KERN_DEBUG "Dest not on link. Forcing...\n");
			if (key.src == 0)
				key.src = inet_select_addr(dev_out, 0, RT_SCOPE_LINK);
			goto make_route;
		}
		return -ENETUNREACH;
	}

	if (res.type == RTN_NAT)
		return -EINVAL;


	if (!key.src) {
		key.src = FIB_RES_PREFSRC(res);

#ifdef CONFIG_IP_MULTIPLE_TABLES
		/*
		 * "Stabilization" of route.
		 * This step is necessary, if locally originated packets
		 * are subjected to policy routing, otherwise we could get
		 * route flapping.
		 */
		if (fib_lookup(&key, &res))
			return -ENETUNREACH;
#endif
	}

#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (res.fi->fib_nhs > 1 && key.oif == 0)
		fib_select_multipath(&key, &res);
#endif

	dev_out = FIB_RES_DEV(res);

	if (res.type == RTN_LOCAL) {
		dev_out = &loopback_dev;
		key.oif = dev_out->ifindex;
		res.fi = NULL;
		flags |= RTCF_LOCAL;
	}

	key.oif = dev_out->ifindex;

make_route:
	if (LOOPBACK(key.src) && !(dev_out->flags&IFF_LOOPBACK)) {
		printk(KERN_DEBUG "this guy talks to %08x from loopback\n", key.dst);
		return -EINVAL;
	}

	if (key.dst == 0xFFFFFFFF)
		res.type = RTN_BROADCAST;
	else if (MULTICAST(key.dst))
		res.type = RTN_MULTICAST;
	else if (BADCLASS(key.dst) || ZERONET(key.dst))
		return -EINVAL;

	if (res.type == RTN_BROADCAST) {
		flags |= RTCF_BROADCAST;
		if (!(dev_out->flags&IFF_LOOPBACK) && dev_out->flags&IFF_BROADCAST)
			flags |= RTCF_LOCAL;
	} else if (res.type == RTN_MULTICAST) {
		flags |= RTCF_MULTICAST;
		if (ip_check_mc(dev_out, daddr))
			flags |= RTCF_LOCAL;
	}

	rth = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
	if (!rth)
		return -ENOBUFS;

	atomic_set(&rth->u.dst.use, 1);
	rth->key.dst	= daddr;
	rth->key.tos	= tos;
	rth->key.src	= saddr;
	rth->key.iif	= 0;
	rth->key.oif	= oif;
	rth->rt_dst	= key.dst;
	rth->rt_src	= key.src;
#ifdef CONFIG_IP_ROUTE_NAT
	rth->rt_dst_map	= key.dst;
	rth->rt_src_map	= key.src;
#endif
	rth->rt_iif	= dev_out->ifindex;
	rth->u.dst.dev	= dev_out;
	rth->rt_gateway = key.dst;
	rth->rt_spec_dst= key.src;

	rth->u.dst.output=ip_output;

	if (flags&RTCF_LOCAL) {
		rth->u.dst.input = ip_local_deliver;
		rth->rt_spec_dst = key.dst;
	}
	if (flags&(RTCF_BROADCAST|RTCF_MULTICAST)) {
		rth->rt_spec_dst = key.src;
		if (flags&RTCF_LOCAL && !(dev_out->flags&IFF_LOOPBACK))
			rth->u.dst.output = ip_mc_output;
#ifdef CONFIG_IP_MROUTE
		if (res.type == RTN_MULTICAST && dev_out->ip_ptr) {
			struct in_device *in_dev = dev_out->ip_ptr;
			if (IN_DEV_MFORWARD(in_dev) && !LOCAL_MCAST(daddr)) {
				rth->u.dst.input = ip_mr_input;
				rth->u.dst.output = ip_mc_output;
			}
		}
#endif
	}

	if (res.fi) {
		if (FIB_RES_GW(res) && FIB_RES_NH(res).nh_scope == RT_SCOPE_LINK)
			rth->rt_gateway = FIB_RES_GW(res);
		rth->u.dst.pmtu	= res.fi->fib_mtu ? : dev_out->mtu;
		rth->u.dst.window=res.fi->fib_window ? : 0;
		rth->u.dst.rtt	= res.fi->fib_rtt ? : TCP_TIMEOUT_INIT;
	} else {
		rth->u.dst.pmtu	= dev_out->mtu;
		rth->u.dst.window=0;
		rth->u.dst.rtt	= TCP_TIMEOUT_INIT;
	}
	rth->rt_flags = flags;
        rth->rt_type = res.type;
	hash = rt_hash_code(daddr, saddr^(oif<<5), tos);
	*rp = rt_intern_hash(hash, rth, ETH_P_IP);
	return 0;
}

int ip_route_output(struct rtable **rp, u32 daddr, u32 saddr, u8 tos, int oif)
{
	unsigned hash;
	struct rtable *rth;

	hash = rt_hash_code(daddr, saddr^(oif<<5), tos);

	start_bh_atomic();
	for (rth=rt_hash_table[hash]; rth; rth=rth->u.rt_next) {
		if (rth->key.dst == daddr &&
		    rth->key.src == saddr &&
		    rth->key.iif == 0 &&
		    rth->key.oif == oif &&
		    rth->key.tos == tos) {
			rth->u.dst.lastuse = jiffies;
			atomic_inc(&rth->u.dst.use);
			atomic_inc(&rth->u.dst.refcnt);
			end_bh_atomic();
			*rp = rth;
			return 0;
		}
	}
	end_bh_atomic();

	return ip_route_output_slow(rp, daddr, saddr, tos, oif);
}

#ifdef CONFIG_RTNETLINK

int inet_rtm_getroute(struct sk_buff *in_skb, struct nlmsghdr* nlh, void *arg)
{
	struct kern_rta *rta = arg;
	struct rtmsg *rtm = NLMSG_DATA(nlh);
	struct rtable *rt = NULL;
	u32 dst = 0;
	u32 src = 0;
	int err;
	struct sk_buff *skb;
	u8  *o;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		return -ENOBUFS;

	/* Reserve room for dummy headers, this skb can pass
	   through good chunk of routing engine.
	 */
	skb->mac.raw = skb->data;
	skb_reserve(skb, MAX_HEADER + sizeof(struct iphdr));

	if (rta->rta_dst)
		memcpy(&dst, rta->rta_dst, 4);
	if (rta->rta_src)
		memcpy(&src, rta->rta_src, 4);

	if (rta->rta_iif) {
		struct device *dev;
		dev = dev_get_by_index(*rta->rta_iif);
		if (!dev)
			return -ENODEV;
		skb->protocol = __constant_htons(ETH_P_IP);
		skb->dev = dev;
		start_bh_atomic();
		err = ip_route_input(skb, dst, src, rtm->rtm_tos, dev);
		end_bh_atomic();
		rt = (struct rtable*)skb->dst;
		if (!err && rt->u.dst.error)
			err = rt->u.dst.error;
	} else {
		err = ip_route_output(&rt, dst, src, rtm->rtm_tos,
				      rta->rta_oif ? *rta->rta_oif : 0);
	}
	if (err) {
		kfree_skb(skb, FREE_WRITE);
		return err;
	}

	skb->dst = &rt->u.dst;
	if (rtm->rtm_flags & RTM_F_NOTIFY)
		rt->rt_flags |= RTCF_NOTIFY;

	nlh = NLMSG_PUT(skb, NETLINK_CB(in_skb).pid, nlh->nlmsg_seq,
			RTM_NEWROUTE, sizeof(*rtm));
	rtm = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = 0;
	rtm->rtm_family = AF_INET;
	rtm->rtm_dst_len = 32;
	rtm->rtm_src_len = 32;
	rtm->rtm_tos = rt->key.tos;
	rtm->rtm_table = RT_TABLE_MAIN;
	rtm->rtm_type = rt->rt_type;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_protocol = RTPROT_UNSPEC;
	rtm->rtm_flags = (rt->rt_flags&~0xFFFF) | RTM_F_CLONED;
	rtm->rtm_nhs = 0;

	o = skb->tail;
	RTA_PUT(skb, RTA_DST, 4, &rt->rt_dst);
	RTA_PUT(skb, RTA_SRC, 4, &rt->rt_src);
	if (rt->u.dst.dev)
		RTA_PUT(skb, RTA_OIF, sizeof(int), &rt->u.dst.dev->ifindex);
	if (rt->rt_dst != rt->rt_gateway)
		RTA_PUT(skb, RTA_GATEWAY, 4, &rt->rt_gateway);
	RTA_PUT(skb, RTA_MTU, sizeof(unsigned), &rt->u.dst.pmtu);
	RTA_PUT(skb, RTA_WINDOW, sizeof(unsigned), &rt->u.dst.window);
	RTA_PUT(skb, RTA_RTT, sizeof(unsigned), &rt->u.dst.rtt);
	RTA_PUT(skb, RTA_PREFSRC, 4, &rt->rt_spec_dst);
	rtm->rtm_optlen = skb->tail - o;
	if (rta->rta_iif) {
#ifdef CONFIG_IP_MROUTE
		if (MULTICAST(dst) && !LOCAL_MCAST(dst) && ipv4_config.multicast_route) {
			NETLINK_CB(skb).pid = NETLINK_CB(in_skb).pid;
			err = ipmr_get_route(skb, rtm);
			if (err <= 0)
				return err;
		} else
#endif
		{
			RTA_PUT(skb, RTA_IIF, 4, rta->rta_iif);
			rtm->rtm_optlen = skb->tail - o;
		}
	}
	nlh->nlmsg_len = skb->tail - (u8*)nlh;
	err = netlink_unicast(rtnl, skb, NETLINK_CB(in_skb).pid, MSG_DONTWAIT);
	if (err < 0)
		return err;
	return 0;

nlmsg_failure:
rtattr_failure:
	kfree_skb(skb, FREE_WRITE);
	return -EMSGSIZE;
}

#endif /* CONFIG_RTNETLINK */

void ip_rt_multicast_event(struct in_device *in_dev)
{
	rt_cache_flush(1*HZ);
}

__initfunc(void ip_rt_init(void))
{
	devinet_init();
	ip_fib_init();

#ifdef CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RTCACHE, 8, "rt_cache",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		rt_cache_get_info
	});
#endif
}
