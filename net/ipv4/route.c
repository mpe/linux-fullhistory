/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		ROUTE - implementation of the IP router.
 *
 * Version:	@(#)route.c	1.0.14	05/31/93
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
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/arp.h>
#include <net/tcp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <linux/net_alias.h>

/* Compile time configuretion flags */

#define CONFIG_IP_LOCAL_RT_POLICY 1

static void rt_run_flush(unsigned long);
  
static struct timer_list rt_flush_timer =
	{ NULL, NULL, RT_FLUSH_DELAY, 0L, rt_run_flush };

/*
 *	Interface to generic destination cache.
 */

static void ipv4_dst_destroy(struct dst_entry * dst);
static struct dst_entry * ipv4_dst_check(struct dst_entry * dst);
static struct dst_entry * ipv4_dst_reroute(struct dst_entry * dst);


struct dst_ops ipv4_dst_ops =
{
	AF_INET,
	ipv4_dst_check,
	ipv4_dst_reroute,
	ipv4_dst_destroy
};


/*
 * Route cache.
 */

static atomic_t		 rt_cache_size;
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
				r->rt_flags, r->u.dst.refcnt,
				r->u.dst.use, 0,
				(unsigned long)r->rt_src, (int)r->u.dst.pmtu,
				r->u.dst.window,
				(int)r->u.dst.rtt, r->key.tos,
				r->u.dst.hh ? r->u.dst.hh->hh_refcnt : -1,
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

	start_bh_atomic();

	for (i=0; i<RT_HASH_DIVISOR/5; i++) {
		rover = (rover + 1) & (RT_HASH_DIVISOR-1);
		rthp = &rt_hash_table[rover];

		while ((rth = *rthp) != NULL) {
			struct rtable * rth_next = rth->u.rt_next;

			/*
			 * Cleanup aged off entries.
			 */

			if (!rth->u.dst.refcnt && now - rth->u.dst.lastuse > RT_CACHE_TIMEOUT) {
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

			/*
			 * Pseudo-LRU ordering.
			 * Really we should teach it to move
			 * rarely used but permanently living entries
			 * (f.e. rdisc, igmp etc.) to the end of list.
			 */

			if ( rth_next->u.dst.lastuse - rth->u.dst.lastuse > RT_CACHE_BUBBLE_THRESHOLD ||
			    (rth->u.dst.lastuse - rth_next->u.dst.lastuse < 0 &&
			     rth->u.dst.use < rth_next->u.dst.use)) {
#if RT_CACHE_DEBUG >= 2
				printk("rt_check_expire bubbled %02x@%08x<->%08x\n", rover, rth->rt_dst, rth_next->rt_dst);
#endif
				*rthp = rth_next;
 				rth->u.rt_next = rth_next->u.rt_next;
				rth_next->u.rt_next = rth;
				sti();
				rthp = &rth_next->u.rt_next;
				continue;
			}
			rthp = &rth->u.rt_next;
		}
	}

	end_bh_atomic();
}
  
  
void rt_cache_flush(int how)
{
	start_bh_atomic();
	if (rt_flush_timer.expires) {
		if (jiffies - rt_flush_timer.expires > 0 ||
		    rt_flush_timer.expires - jiffies > RT_FLUSH_DELAY/2)
			how = 1;
	}
	if (how) {
		if (rt_flush_timer.expires)
			del_timer(&rt_flush_timer);
		rt_flush_timer.expires = 0;
		end_bh_atomic();
		rt_run_flush(0);
		return;
	}
	if (rt_flush_timer.expires) {
		end_bh_atomic();
		return;
	}
	del_timer(&rt_flush_timer);
	rt_flush_timer.expires = jiffies + RT_FLUSH_DELAY;
	add_timer(&rt_flush_timer);
	end_bh_atomic();
}
  
void rt_run_flush(unsigned long dummy)
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
	 * do not make it too frequently.
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
			if (rth->u.dst.refcnt || now - rth->u.dst.lastuse > expire)
				continue;
			atomic_dec(&rt_cache_size);
			*rthp = rth->u.rt_next;
			rth->u.rt_next = NULL;
			rt_free(rth);
			break;
		}
	}

	last_gc = now;
	if (rt_cache_size < RT_CACHE_MAX_SIZE)
		expire = RT_CACHE_TIMEOUT>>1;
	else
		expire >>= 1;
	end_bh_atomic();
}

static int rt_ll_bind(struct rtable *rt)
{
	struct dst_entry *neigh;
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
			hh->hh_refcnt = 0;
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

	if (rt_cache_size >= RT_CACHE_MAX_SIZE)
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
	int i;
	int  off_link = 0;
	struct fib_info *fi;
	struct rtable *rth, **rthp;
	u32  skeys[2] = { saddr, 0, };
	struct device *pdev = net_alias_main_dev(dev);

	tos &= IPTOS_TOS_MASK;

	if (new_gw == old_gw || !ipv4_config.accept_redirects
	    || MULTICAST(new_gw) || BADCLASS(new_gw) || ZERONET(new_gw))
		goto reject_redirect;

	if ((new_gw^dev->pa_addr)&dev->pa_mask)
		off_link = 1;

	if (!ipv4_config.rfc1620_redirects) {
		if (off_link)
			goto reject_redirect;
		if (ipv4_config.secure_redirects && ip_fib_chk_default_gw(new_gw, dev))
			goto reject_redirect;
	}

	fi = fib_lookup_info(new_gw, 0, 0, &loopback_dev, NULL);
	if (fi == NULL || fi->fib_flags&(RTF_LOCAL|RTF_BROADCAST|RTF_NAT))
		goto reject_redirect;

	for (i=0; i<2; i++) {
		unsigned hash = rt_hash_code(daddr, skeys[i], tos);

		rthp=&rt_hash_table[hash];

		while ( (rth = *rthp) != NULL) {
			struct rtable *rt;

			if (rth->key.dst != daddr ||
			    rth->key.src != skeys[i] ||
			    rth->key.tos != tos ||
			    rth->key.dst_dev != NULL ||
			    rth->key.src_dev != NULL) {
				rthp = &rth->u.rt_next;
				continue;
			}

			if (rth->rt_dst != daddr ||
			    rth->rt_src != saddr ||
			    rth->rt_flags&RTF_REJECT ||
			    rth->rt_gateway != old_gw ||
			    rth->u.dst.dev != dev)
				break;

			rt = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
			if (rt == NULL)
				return;

			/*
			 * Copy all the information.
			 */
			rt->u.dst.refcnt = 1;
			rt->u.dst.dev = dev;
			rt->u.dst.input = rth->u.dst.input;
			rt->u.dst.output = rth->u.dst.output;
			rt->u.dst.pmtu = dev->mtu;
			rt->u.dst.rtt = TCP_TIMEOUT_INIT;
			rt->u.dst.window = 0;
			rt->u.dst.use = 1;
			rt->u.dst.lastuse = jiffies;

			rt->rt_flags = rth->rt_flags|RTF_DYNAMIC|RTF_MODIFIED;
			rt->rt_flags &= ~RTF_GATEWAY;
			if (new_gw != daddr)
				rt->rt_flags |= RTF_GATEWAY;

			rt->rt_src = rth->rt_src;
			rt->rt_dst = rth->rt_dst;
			rt->rt_src_dev = rth->rt_src_dev;
			rt->rt_spec_dst = rth->rt_spec_dst;
			rt->key = rth->key;

			/* But gateway is different ... */
			rt->rt_gateway = new_gw;

			if (off_link) {
				if (fi->fib_dev != dev &&
				    net_alias_main_dev(fi->fib_dev) == pdev)
					rt->u.dst.dev = fi->fib_dev;
			}

			if (ipv4_config.rfc1620_redirects && !rt_ll_bind(rt)) {
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
	return;

reject_redirect:
	if (ipv4_config.log_martians)
		printk(KERN_INFO "Redirect from %lX/%s to %lX ignored."
		       "Path = %lX -> %lX, tos %02x\n",
		       ntohl(old_gw), dev->name, ntohl(new_gw),
		       ntohl(saddr), ntohl(daddr), tos);
}


void ip_rt_advice(struct rtable **rp, int advice)
{
	struct rtable *rt;

	if (advice)
		return;

	start_bh_atomic();
	if ((rt = *rp) != NULL && (rt->rt_flags&(RTF_DYNAMIC|RTF_MODIFIED))) {
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
 *	2. If we did not see a packets requiring redirects
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
		if (ipv4_config.log_martians && ++rt->errors == RT_REDIRECT_NUMBER)
			printk(KERN_WARNING "host %08x/%s ignores redirects for %08x to %08x.\n",
			       rt->rt_src, rt->rt_src_dev->name, rt->rt_dst, rt->rt_gateway);
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


static __inline__ unsigned short guess_mtu(unsigned short old_mtu)
{
	if (old_mtu > 32000)
		return 32000;
	else if (old_mtu > 17914)
		return 17914;
	else if (old_mtu > 8166)
		return 8166;
	else if (old_mtu > 4352)
		return 4352;
	else if (old_mtu > 2002)
		return 2002;
	else if (old_mtu > 1492)
		return 1492;
	else if (old_mtu > 576)
		return 576;
	else if (old_mtu > 296)
		return 296;
	/*
	 *	These two are not from the RFC but
	 *	are needed for AMPRnet AX.25 paths.
	 */
	else if (old_mtu > 216)
		return 216;
	else if (old_mtu > 128)
		return 128;
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
			    !rth->key.src_dev &&
			    !(rth->rt_flags&RTF_NOPMTUDISC)) {
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

static struct dst_entry * ipv4_dst_check(struct dst_entry * dst)
{
	return NULL;
}

static struct dst_entry * ipv4_dst_reroute(struct dst_entry * dst)
{
	return NULL;
}

int
ip_check_mc(struct device *dev, u32 mc_addr)
{
	struct ip_mc_list *ip_mc;

	if (mc_addr==htonl(INADDR_ALLHOSTS_GROUP))
		return 1;

	for (ip_mc=dev->ip_mc_list; ip_mc; ip_mc=ip_mc->next)
		if (ip_mc->multiaddr == mc_addr)
			return 1;
	return 0;
}

static int ip_rt_bug(struct sk_buff *skb)
{
	kfree_skb(skb, FREE_WRITE);
	printk(KERN_DEBUG "ip_rt_bug: %08x -> %08x, %s\n", skb->nh.iph->saddr,
	       skb->nh.iph->daddr, skb->dev ? skb->dev->name : "?");
	return 0;
}

/*
 *	This function is called ONLY FROM NET BH. No locking!
 *
 *	NOTE. We drop all the packets that has local source
 *	addresses, because every properly looped back packet
 *	must have correct destination already attached by output routine.
 *
 *	Such approach solves two big problems:
 *	1. Not simplex devices (if they exist 8)) are handled properly.
 *	2. IP spoofing attempts are filtered with 100% of guarantee.
 */

int ip_route_input_slow(struct sk_buff *skb, u32 daddr, u32 saddr,
			u8 tos, struct device *pdev)
{
	struct device * dev = pdev;
	struct fib_info *fi = NULL;
	struct fib_info *src_fi = NULL;
	unsigned	flags = 0;
	struct	device	*devout;
	struct rtable * rth;
	unsigned	hash;
	struct fib_result res;
	u32	src_key = saddr;
	u32	dst_key = daddr;
	int	err = -EINVAL;
	int	log = 0;

	hash = rt_hash_code(daddr, saddr^(unsigned long)pdev, tos);

	/*	Check for martians... */

	if (MULTICAST(saddr) || BADCLASS(saddr) || LOOPBACK(saddr))
		goto martian_source;
	if (MULTICAST(daddr) || daddr == 0xFFFFFFFF)
		goto mc_input;

	/* Accept zero addresses only to limited broadcast/multicasts;
	 * I even do not know to fix it or not.
	 */
	if (ZERONET(saddr))
		goto martian_source;
	if (BADCLASS(daddr) || ZERONET(daddr) || LOOPBACK(daddr))
		goto martian_destination;

	/*
	 * Device is not yet initialized, accept all addresses as ours.
	 */
	if (ZERONET(dev->pa_addr))
		goto promisc_ip;

	/*
	 *	Now we are able to route packet.
	 */
	if ((err = fib_lookup(&res, daddr, saddr, tos, pdev, NULL)) < 0) {
		if (!IS_ROUTER)
			return -EINVAL;
		goto no_route;
	}

	fi = res.f->fib_info;
	flags  = fi->fib_flags;
	devout = fi->fib_dev;

	if (flags&RTF_NAT) {
		daddr = htonl((ntohl(daddr)&((1<<res.fm)-1)))|fi->fib_gateway;
		fi = fib_lookup_info(daddr, saddr, tos, pdev, NULL);
		if (!fi || fi->fib_flags&(RTF_NAT|RTF_LOCAL|RTF_MULTICAST|RTF_BROADCAST))
			return -EINVAL;
		devout = fi->fib_dev;
		flags = fi->fib_flags|RTCF_NAT|RTF_NAT;
	}

	switch (res.fr->cl_action) {
	case RTP_NAT:
		/* Packet is from  translated source; remember it */
		saddr = (saddr&~res.fr->cl_srcmask)|res.fr->cl_srcmap;
		flags |= RTCF_NAT;
		break;
	case RTP_MASQUERADE:
		/* Packet is from masqueraded source; remember it */
		flags |= RTCF_MASQ;
		break;
	default:
	}
	log = res.fr->cl_flags&RTRF_LOG;

	if (!(flags & RTF_LOCAL)) {
		if (!IS_ROUTER || flags&RTF_NOFORWARD)
			return -EINVAL;
	} else {
		fi = NULL;
		devout = &loopback_dev;
		if (flags&RTF_BROADCAST)
		    goto mc_input;
	}

#ifndef CONFIG_IP_LOCAL_RT_POLICY
	if (flags&RTF_LOCAL)
		src_fi = fib_lookup_info(src_key, 0, tos, &loopback_dev, NULL);
	else
#endif
	if (fib_lookup(&res, src_key, daddr, tos, net_alias_main_dev(devout), NULL) == 0) {
		src_fi = res.f->fib_info;
		/* Destination is on masqueraded network:
		 * if it is real incoming frame, ip_forward will drop it.
		 */
		if (res.fr->cl_flags&RTRF_VALVE)
			flags |= RTCF_VALVE;
	}

        if (src_fi) {
		if (src_fi->fib_flags&(RTF_LOCAL|RTF_BROADCAST|RTF_MULTICAST|RTF_NAT))
			goto martian_source;

		if (!(src_fi->fib_flags&RTF_GATEWAY))
			flags |= RTCF_DIRECTSRC;

		if (net_alias_main_dev(src_fi->fib_dev) == pdev)
			skb->dev = dev = src_fi->fib_dev;
		else {
			/* Route to packet source goes via
			   different interface; rfc1812 proposes
			   to drop them.
			   It is dangerous on not-stub/transit networks
			   because of path asymmetry.
			 */
			if (ipv4_config.rfc1812_filter >= 2)
				goto martian_source;

			/* Weaker form of rfc1812 filtering.
			   If source is on directly connected network,
			   it can mean either local network configuration error
			   (the most probable case) or real IP spoofing attempt.
			 */
			if (ipv4_config.rfc1812_filter >= 1 && !(flags&RTCF_DIRECTSRC))
				goto martian_source;
		}
	} else if (ipv4_config.rfc1812_filter >= 1)
		goto martian_source;

make_route:
	if (skb->protocol != __constant_htons(ETH_P_IP)) {
		/* ARP request. Do not make route for invalid destination or
		 * if it is redirected.
		 */
		if (flags&(RTF_REJECT|RTF_BROADCAST|RTF_MULTICAST) ||
		    skb->pkt_type == PACKET_OTHERHOST ||
		    (devout == dev && !(flags&(RTF_LOCAL|RTCF_NAT))))
			return -EINVAL;
	}

	rth = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
	if (!rth)
		return -ENOBUFS;

	rth->u.dst.output= ip_rt_bug;

	rth->u.dst.use	= 1;
	rth->key.dst	= dst_key;
	rth->rt_dst	= dst_key;
	rth->rt_dst_map	= daddr;
	rth->key.tos	= tos;
	rth->key.src	= src_key;
	rth->rt_src	= src_key;
	rth->rt_src_map	= saddr;
	rth->rt_src_dev = dev;
	rth->key.src_dev= pdev;
	rth->u.dst.dev	= devout;
	rth->key.dst_dev= NULL;
	rth->rt_gateway	= daddr;
	rth->rt_spec_dst= daddr;

	if (!(flags&RTF_REJECT)) {
		if (flags&RTF_LOCAL)
			rth->u.dst.input= ip_local_deliver;
		if (!(flags&(RTF_NOFORWARD|RTF_BROADCAST))) {
			if (flags&RTF_MULTICAST) {
#ifdef CONFIG_IP_MROUTE
				if (!LOCAL_MCAST(daddr) && ipv4_config.multicast_route) {
					rth->u.dst.input = ip_mr_input;
					rth->u.dst.output = ip_output;
				}
#endif
			} else if (!(flags&RTF_LOCAL)) {
				rth->u.dst.input = ip_forward;
				rth->u.dst.output = ip_output;
			}
		}
	} else if (IS_ROUTER && !(flags&(RTF_MULTICAST|RTF_BROADCAST))) {
		rth->u.dst.input= ip_error;
		rth->u.dst.error= -err;
	}

	if ((flags&(RTF_BROADCAST|RTF_MULTICAST)) || !(flags&RTF_LOCAL))
		rth->rt_spec_dst= dev->pa_addr;

	if (fi) {
		rth->u.dst.pmtu	= fi->fib_mtu;
		rth->u.dst.window=fi->fib_window;
		rth->u.dst.rtt	= fi->fib_irtt;
		if (flags & RTF_GATEWAY)
			rth->rt_gateway	= fi->fib_gateway;
	} else {
		rth->u.dst.pmtu	= devout->mtu;
		rth->u.dst.window=0;
		rth->u.dst.rtt	= TCP_TIMEOUT_INIT;
	}

	if (!(flags&(RTF_LOCAL|RTF_BROADCAST|RTF_MULTICAST|RTCF_NAT)) &&
	    flags&RTCF_DIRECTSRC &&
	    (devout == dev || (ipv4_config.rfc1620_redirects &&
			       net_alias_main_dev(devout) == pdev)))
		flags |= RTCF_DOREDIRECT;

	rth->rt_flags = flags;

	if (log)
		printk(KERN_INFO "installing route %08lX -> %08lX\n", ntohl(rth->rt_src), ntohl(rth->rt_dst));

	if (flags&(RTF_LOCAL|RTF_MULTICAST|RTF_BROADCAST|RTF_REJECT)) {
		skb->dst = (struct dst_entry*)rt_intern_hash(hash, rth, 0);
		return 0;
	}
	skb->dst = (struct dst_entry*)rt_intern_hash(hash, rth, __constant_ntohs(skb->protocol));
	return 0;

mc_input:
	if (skb->protocol != __constant_htons(ETH_P_IP))
		return -EINVAL;

	if (ZERONET(saddr)) {
		if (!ipv4_config.bootp_agent)
			goto martian_source;
		flags |= RTF_NOFORWARD|RTF_LOCAL;
	} else {
		src_fi = fib_lookup_info(saddr, 0, tos, &loopback_dev, NULL);
		if (!src_fi)
			goto martian_source;

		if (src_fi->fib_flags&(RTF_LOCAL|RTF_BROADCAST|RTF_MULTICAST|RTF_NAT))
			goto martian_source;

		if (!(src_fi->fib_flags&RTF_GATEWAY))
			flags |= RTCF_DIRECTSRC;

		if (!MULTICAST(daddr) || !ipv4_config.multicast_route ||
		    LOCAL_MCAST(daddr)) {
			if (net_alias_main_dev(src_fi->fib_dev) == pdev) {
				skb->dev = dev = src_fi->fib_dev;
			} else {
				/* Fascist not-unicast filtering 8) */
				goto martian_source;
			}
		}
	}

	if (!MULTICAST(daddr)) {
		flags |= RTF_LOCAL|RTF_BROADCAST|RTF_NOFORWARD;
		devout = dev;
		goto make_route;
	}

	flags |= RTF_MULTICAST|RTF_LOCAL;

	if (ip_check_mc(dev, daddr) == 0) {
		flags &= ~RTF_LOCAL;

		if (!ipv4_config.multicast_route || !(dev->flags&IFF_ALLMULTI))
			goto no_route;
	}
	devout = dev;
	goto make_route;

promisc_ip:
	flags |= RTF_LOCAL|RTF_NOFORWARD;
	if (MULTICAST(daddr))
		flags |= RTF_MULTICAST;
	else
		flags |= RTF_BROADCAST;
	devout = dev;
	goto make_route;

no_route:
	flags |= RTF_REJECT;
	devout = dev;
	goto make_route;

	/*
	 *	Do not cache martian addresses: they should be logged (RFC1812)
	 */
martian_destination:
	if (ipv4_config.log_martians)
		printk(KERN_WARNING "martian destination %08x from %08x, dev %s\n", daddr, saddr, dev->name);
	return -EINVAL;

martian_source:
	if (ipv4_config.log_martians) {
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
	return -EINVAL;
}

int ip_route_input(struct sk_buff *skb, u32 daddr, u32 saddr,
		   u8 tos, struct device *dev)
{
	struct rtable * rth;
	unsigned	hash;

	if (skb->dst)
		return 0;

#if RT_CACHE_DEBUG >= 1
	if (dev->flags & IFF_LOOPBACK) {
		printk(KERN_DEBUG "ip_route_input: bug: packet is looped back\n");
		return -EINVAL;
	}
	if (net_alias_main_dev(dev) != dev)
		printk(KERN_DEBUG "ip_route_input: bug: packet is received on alias %s\n", dev->name);
#endif

	tos &= IPTOS_TOS_MASK;
	hash = rt_hash_code(daddr, saddr^(unsigned long)dev, tos);
	skb->dev = dev;

	for (rth=rt_hash_table[hash]; rth; rth=rth->u.rt_next) {
		if (rth->key.dst == daddr &&
		    rth->key.src == saddr &&
		    rth->key.src_dev == dev &&
		    rth->key.dst_dev == NULL &&
		    rth->key.tos == tos) {
			rth->u.dst.lastuse = jiffies;
			atomic_inc(&rth->u.dst.use);
			atomic_inc(&rth->u.dst.refcnt);
			skb->dst = (struct dst_entry*)rth;
			skb->dev = rth->rt_src_dev;
			return 0;
		}
	}
	return ip_route_input_slow(skb, daddr, saddr, tos, dev);
}


/*
 * Major route resolver routine.
 */

int ip_route_output_slow(struct rtable **rp, u32 daddr, u32 saddr, u8 tos,
			 struct device *dev_out)
{
	u32 src_key = saddr;
	u32 dst_key = daddr;
	u32 dst_map;
	struct device *dst_dev_key = dev_out;
	unsigned flags = 0;
	struct fib_info *fi = NULL;
	struct rtable *rth;
#ifdef CONFIG_IP_LOCAL_RT_POLICY
	struct fib_result res;
#endif
	unsigned hash;

	tos &= IPTOS_TOS_MASK|1;

	if (saddr) {
		if (MULTICAST(saddr) || BADCLASS(saddr) || ZERONET(saddr) ||
		    __ip_chk_addr(saddr) != IS_MYADDR)
			return -EINVAL;
		if (dev_out == NULL && (MULTICAST(daddr) || daddr == 0xFFFFFFFF))
			dev_out = ip_dev_find(saddr, NULL);
	}
	if (!daddr)
		daddr = saddr;

	if (dev_out) {
		if (!saddr) {
			saddr = dev_out->pa_addr;
			if (!daddr)
				daddr = saddr;
		}
		dst_map = daddr;
		if (MULTICAST(daddr) || daddr == 0xFFFFFFFF)
			goto make_route;
	}

	if (!daddr)
		daddr = htonl(INADDR_LOOPBACK);

#ifdef CONFIG_IP_LOCAL_RT_POLICY
	if (fib_lookup(&res, daddr, saddr, tos, &loopback_dev, dev_out))
		return -ENETUNREACH;
	fi = res.f->fib_info;
	dst_map = daddr;

	if (fi->fib_flags&RTF_NAT)
		return -EINVAL;

	if (!saddr) {
		saddr = fi->fib_dev->pa_addr;

		/*
		 * "Stabilization" of route.
		 * This step is necessary, if locally originated packets
		 * are subjected to source routing, else we could get
		 * route flapping.
		 */
		fi = fib_lookup_info(dst_map, saddr, tos, &loopback_dev, dev_out);
		if (!fi)
			return -ENETUNREACH;
	}
#else
	fi = fib_lookup_info(daddr, 0, tos, &loopback_dev, dev_out);
	if (!fi)
		return -ENETUNREACH;

	if (fi->fib_flags&RTF_NAT)
		return -EINVAL;

	dst_map = daddr;
	if (!saddr)
		saddr = fi->fib_dev->pa_addr;
#endif

	flags |= fi->fib_flags;
	dev_out = fi->fib_dev;

	if (RT_LOCALADDR(flags)) {
		dev_out = &loopback_dev;
		fi = NULL;
	}

	if (dst_dev_key && dev_out != dst_dev_key)
		return -EINVAL;

make_route:
	if (LOOPBACK(saddr) && !(dev_out->flags&IFF_LOOPBACK)) {
		printk(KERN_DEBUG "this guy talks to %08x from loopback\n", daddr);
		return -EINVAL;
	}

	if (daddr == 0xFFFFFFFF)
		flags |= RTF_BROADCAST;
	else if (MULTICAST(daddr))
		flags |= RTF_MULTICAST;
	else if (BADCLASS(daddr) || ZERONET(daddr))
		return -EINVAL;

	if (flags&RTF_BROADCAST && (dev_out->flags&IFF_LOOPBACK ||
	    !(dev_out->flags&IFF_BROADCAST)))
		flags &= ~RTF_LOCAL;
	else if (flags&RTF_MULTICAST) {
		if (ip_check_mc(dev_out, daddr))
			flags |= RTF_LOCAL;
	}
	
	rth = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
	if (!rth)
		return -ENOBUFS;

	rth->u.dst.use	= 1;
	rth->key.dst	= dst_key;
	rth->key.tos	= tos;
	rth->key.src	= src_key;
	rth->key.src_dev= NULL;
	rth->key.dst_dev= dst_dev_key;
	rth->rt_dst	= daddr;
	rth->rt_dst_map	= dst_map;
	rth->rt_src	= saddr;
	rth->rt_src_map	= saddr;
	rth->rt_src_dev = dev_out;
	rth->u.dst.dev	= dev_out;
	rth->rt_gateway = dst_map;
	rth->rt_spec_dst= dev_out->pa_addr;

	rth->u.dst.output=ip_output;

	if (flags&RTF_LOCAL) {
		rth->u.dst.input = ip_local_deliver;
		rth->rt_spec_dst = daddr;
	}
	if (flags&(RTF_BROADCAST|RTF_MULTICAST)) {
		rth->rt_spec_dst = dev_out->pa_addr;
		flags &= ~RTF_GATEWAY;
		if (flags&RTF_LOCAL)
			rth->u.dst.output = ip_mc_output;
		if (flags&RTF_MULTICAST) {
			if (dev_out->flags&IFF_ALLMULTI)
				rth->u.dst.output = ip_mc_output;
#ifdef CONFIG_IP_MROUTE
			if (ipv4_config.multicast_route && !LOCAL_MCAST(daddr))
				rth->u.dst.input = ip_mr_input;
#endif
		}
	}

	if (fi) {
		if (flags&RTF_GATEWAY)
			rth->rt_gateway = fi->fib_gateway;
		rth->u.dst.pmtu	= fi->fib_mtu;
		rth->u.dst.window=fi->fib_window;
		rth->u.dst.rtt	= fi->fib_irtt;
	} else {
		rth->u.dst.pmtu	= dev_out->mtu;
		rth->u.dst.window=0;
		rth->u.dst.rtt	= TCP_TIMEOUT_INIT;
	}
	rth->rt_flags = flags;
	hash = rt_hash_code(dst_key, dst_dev_key ? src_key^(dst_dev_key->ifindex<<5) : src_key, tos);
	*rp = rt_intern_hash(hash, rth, ETH_P_IP);
	return 0;
}

int ip_route_output(struct rtable **rp, u32 daddr, u32 saddr, u8 tos, struct device *dev_out)
{
	unsigned hash;
	struct rtable *rth;

	hash = rt_hash_code(daddr, dev_out ? saddr^(dev_out->ifindex<<5)
			                   : saddr, tos);

	start_bh_atomic();
	for (rth=rt_hash_table[hash]; rth; rth=rth->u.rt_next) {
		if (rth->key.dst == daddr &&
		    rth->key.src == saddr &&
		    rth->key.src_dev == NULL &&
		    rth->key.dst_dev == dev_out &&
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

	return ip_route_output_slow(rp, daddr, saddr, tos, dev_out);
}

int ip_route_output_dev(struct rtable **rp, u32 daddr, u32 saddr, u8 tos, int ifindex)
{
	unsigned hash;
	struct rtable *rth;
	struct device *dev_out;

	hash = rt_hash_code(daddr, saddr^(ifindex<<5), tos);

	start_bh_atomic();
	for (rth=rt_hash_table[hash]; rth; rth=rth->u.rt_next) {
		if (rth->key.dst == daddr &&
		    rth->key.src == saddr &&
		    rth->key.src_dev == NULL &&
		    rth->key.tos == tos &&
		    rth->key.dst_dev &&
		    rth->key.dst_dev->ifindex == ifindex) {
			rth->u.dst.lastuse = jiffies;
			atomic_inc(&rth->u.dst.use);
			atomic_inc(&rth->u.dst.refcnt);
			end_bh_atomic();
			*rp = rth;
			return 0;
		}
	}
	end_bh_atomic();

	dev_out = dev_get_by_index(ifindex);
	if (!dev_out)
		return -ENODEV;
	return ip_route_output_slow(rp, daddr, saddr, tos, dev_out);
}

void ip_rt_multicast_event(struct device *dev)
{
	rt_cache_flush(0);
}

void ip_rt_init()
{
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
