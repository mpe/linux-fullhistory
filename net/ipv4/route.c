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
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/segment.h>
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
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/netlink.h>
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif

/*
 * Forwarding Information Base definitions.
 */

struct fib_node
{
	struct fib_node		*fib_next;
	__u32			fib_dst;
	unsigned long		fib_use;
	struct fib_info		*fib_info;
	short			fib_metric;
	unsigned char		fib_tos;
};

/*
 * This structure contains data shared by many of routes.
 */	

struct fib_info
{
	struct fib_info		*fib_next;
	struct fib_info		*fib_prev;
	__u32			fib_gateway;
	struct device		*fib_dev;
	int			fib_refcnt;
	unsigned long		fib_window;
	unsigned short		fib_flags;
	unsigned short		fib_mtu;
	unsigned short		fib_irtt;
};

struct fib_zone
{
	struct fib_zone	*fz_next;
	struct fib_node	**fz_hash_table;
	struct fib_node	*fz_list;
	int		fz_nent;
	int		fz_logmask;
	__u32		fz_mask;
};

static struct fib_zone 	*fib_zones[33];
static struct fib_zone 	*fib_zone_list;
static struct fib_node 	*fib_loopback = NULL;
static struct fib_info 	*fib_info_list;

/*
 * Backlogging.
 */

#define RT_BH_REDIRECT		0
#define RT_BH_GARBAGE_COLLECT 	1
#define RT_BH_FREE	 	2

struct rt_req
{
	struct rt_req * rtr_next;
	struct device *dev;
	__u32 dst;
	__u32 gw;
	unsigned char tos;
};

int		    	ip_rt_lock;
unsigned		ip_rt_bh_mask;
static struct rt_req 	*rt_backlog;

/*
 * Route cache.
 */

struct rtable 		*ip_rt_hash_table[RT_HASH_DIVISOR];
static int		rt_cache_size;
static struct rtable 	*rt_free_queue;
struct wait_queue	*rt_wait;

static void rt_kick_backlog(void);
static void rt_cache_add(unsigned hash, struct rtable * rth);
static void rt_cache_flush(void);
static void rt_garbage_collect_1(void);

/* 
 * Evaluate mask length.
 */

static __inline__ int rt_logmask(__u32 mask)
{
	if (!(mask = ntohl(mask)))
		return 32;
	return ffz(~mask);
}

/* 
 * Create mask from length.
 */

static __inline__ __u32 rt_mask(int logmask)
{
	if (logmask >= 32)
		return 0;
	return htonl(~((1<<logmask)-1));
}

static __inline__ unsigned fz_hash_code(__u32 dst, int logmask)
{
	return ip_rt_hash_code(ntohl(dst)>>logmask);
}

/*
 * Free FIB node.
 */

static void fib_free_node(struct fib_node * f)
{
	struct fib_info * fi = f->fib_info;
	if (!--fi->fib_refcnt)
	{
#if RT_CACHE_DEBUG >= 2
		printk("fib_free_node: fi %08x/%s is free\n", fi->fib_gateway, fi->fib_dev->name);
#endif
		if (fi->fib_next)
			fi->fib_next->fib_prev = fi->fib_prev;
		if (fi->fib_prev)
			fi->fib_prev->fib_next = fi->fib_next;
		if (fi == fib_info_list)
			fib_info_list = fi->fib_next;
	}
	kfree_s(f, sizeof(struct fib_node));
}

/*
 * Find gateway route by address.
 */

static struct fib_node * fib_lookup_gateway(__u32 dst)
{
	struct fib_zone * fz;
	struct fib_node * f;

	for (fz = fib_zone_list; fz; fz = fz->fz_next) 
	{
		if (fz->fz_hash_table)
			f = fz->fz_hash_table[fz_hash_code(dst, fz->fz_logmask)];
		else
			f = fz->fz_list;
		
		for ( ; f; f = f->fib_next)
		{
			if ((dst ^ f->fib_dst) & fz->fz_mask)
				continue;
			if (f->fib_info->fib_flags & RTF_GATEWAY)
				return NULL;
			return f;
		}
	}
	return NULL;
}

/*
 * Find local route by address.
 * FIXME: I use "longest match" principle. If destination
 *	  has some non-local route, I'll not search shorter matches.
 *	  It's possible, I'm wrong, but I wanted to prevent following
 *	  situation:
 *	route add 193.233.7.128 netmask 255.255.255.192 gw xxxxxx
 *	route add 193.233.7.0	netmask 255.255.255.0 eth1
 *	  (Two ethernets connected by serial line, one is small and other is large)
 *	  Host 193.233.7.129 is locally unreachable,
 *	  but old (<=1.3.37) code will send packets destined for it to eth1.
 *
 */

static struct fib_node * fib_lookup_local(__u32 dst)
{
	struct fib_zone * fz;
	struct fib_node * f;

	for (fz = fib_zone_list; fz; fz = fz->fz_next) 
	{
		int longest_match_found = 0;

		if (fz->fz_hash_table)
			f = fz->fz_hash_table[fz_hash_code(dst, fz->fz_logmask)];
		else
			f = fz->fz_list;
		
		for ( ; f; f = f->fib_next)
		{
			if ((dst ^ f->fib_dst) & fz->fz_mask)
				continue;
			if (!(f->fib_info->fib_flags & RTF_GATEWAY))
				return f;
			longest_match_found = 1;
		}
		if (longest_match_found)
			return NULL;
	}
	return NULL;
}

/*
 * Main lookup routine.
 *	IMPORTANT NOTE: this algorithm has small difference from <=1.3.37 visible
 *	by user. It doesn't route non-CIDR broadcasts by default.
 *
 *	F.e.
 *		ifconfig eth0 193.233.7.65 netmask 255.255.255.192 broadcast 193.233.7.255
 *	is valid, but if you really are not able (not allowed, do not want) to
 *	use CIDR compliant broadcast 193.233.7.127, you should add host route:
 *		route add -host 193.233.7.255 eth0
 */

static struct fib_node * fib_lookup(__u32 dst)
{
	struct fib_zone * fz;
	struct fib_node * f;

	for (fz = fib_zone_list; fz; fz = fz->fz_next) 
	{
		if (fz->fz_hash_table)
			f = fz->fz_hash_table[fz_hash_code(dst, fz->fz_logmask)];
		else
			f = fz->fz_list;
		
		for ( ; f; f = f->fib_next)
		{
			if ((dst ^ f->fib_dst) & fz->fz_mask)
				continue;
			return f;
		}
	}
	return NULL;
}

static __inline__ struct device * get_gw_dev(__u32 gw)
{
	struct fib_node * f;
	f = fib_lookup_gateway(gw);
	if (f)
		return f->fib_info->fib_dev;
	return NULL;
}

/*
 *	Check if a mask is acceptable.
 */
 
static inline int bad_mask(__u32 mask, __u32 addr)
{
	if (addr & (mask = ~mask))
		return 1;
	mask = ntohl(mask);
	if (mask & (mask+1))
		return 1;
	return 0;
}


static int fib_del_list(struct fib_node **fp, __u32 dst,
		struct device * dev, __u32 gtw, short flags, short metric, __u32 mask)
{
	struct fib_node *f;
	int found=0;

	while((f = *fp) != NULL) 
	{
		struct fib_info * fi = f->fib_info;

		/*
		 *	Make sure the destination and netmask match.
		 *	metric, gateway and device are also checked
		 *	if they were specified.
		 */
		if (f->fib_dst != dst ||
		    (gtw && fi->fib_gateway != gtw) ||
		    (metric >= 0 && f->fib_metric != metric) ||
		    (dev && fi->fib_dev != dev) )
		{
			fp = &f->fib_next;
			continue;
		}
		cli();
		*fp = f->fib_next;
		if (fib_loopback == f)
			fib_loopback = NULL;
		sti();
		ip_netlink_msg(RTMSG_DELROUTE, dst, gtw, mask, flags, metric, fi->fib_dev->name);
		fib_free_node(f);
		found++;
	}
	return found;
}

static __inline__ int fib_del_1(__u32 dst, __u32 mask,
		struct device * dev, __u32 gtw, short flags, short metric)
{
	struct fib_node **fp;
	struct fib_zone *fz;
	int found=0;

	if (!mask)
	{
		for (fz=fib_zone_list; fz; fz = fz->fz_next)
		{
			int tmp;
			if (fz->fz_hash_table)
				fp = &fz->fz_hash_table[fz_hash_code(dst, fz->fz_logmask)];
			else
				fp = &fz->fz_list;

			tmp = fib_del_list(fp, dst, dev, gtw, flags, metric, mask);
			fz->fz_nent -= tmp;
			found += tmp;
		}
	} 
	else
	{
		if ((fz = fib_zones[rt_logmask(mask)]) != NULL)
		{
			if (fz->fz_hash_table)
				fp = &fz->fz_hash_table[fz_hash_code(dst, fz->fz_logmask)];
			else
				fp = &fz->fz_list;
	
			found = fib_del_list(fp, dst, dev, gtw, flags, metric, mask);
			fz->fz_nent -= found;
		}
	}

	if (found)
	{
		rt_cache_flush();
		return 0;
	}
	return -ESRCH;
}


static struct fib_info * fib_create_info(__u32 gw, struct device * dev,
					 unsigned short flags, unsigned short mss,
					 unsigned long window, unsigned short irtt)
{
	struct fib_info * fi;

	if (!(flags & RTF_MSS))
	{
		mss = dev->mtu;
#ifdef CONFIG_NO_PATH_MTU_DISCOVERY
		/*
		 *	If MTU was not specified, use default.
		 *	If you want to increase MTU for some net (local subnet)
		 *	use "route add .... mss xxx".
		 *
		 * 	The MTU isn't currently always used and computed as it
		 *	should be as far as I can tell. [Still verifying this is right]
		 */
		if ((flags & RTF_GATEWAY) && mss > 576)
			mss = 576;
#endif
	}
	if (!(flags & RTF_WINDOW))
		window = 0;
	if (!(flags & RTF_IRTT))
		irtt = 0;

	for (fi=fib_info_list; fi; fi = fi->fib_next)
	{
		if (fi->fib_gateway != gw ||
		    fi->fib_dev != dev  ||
		    fi->fib_flags != flags ||
		    fi->fib_mtu != mss ||
		    fi->fib_window != window ||
		    fi->fib_irtt != irtt)
			continue;
		fi->fib_refcnt++;
#if RT_CACHE_DEBUG >= 2
		printk("fib_create_info: fi %08x/%s is duplicate\n", fi->fib_gateway, fi->fib_dev->name);
#endif
		return fi;
	}
	fi = (struct fib_info*)kmalloc(sizeof(struct fib_info), GFP_KERNEL);
	if (!fi)
		return NULL;
	memset(fi, 0, sizeof(struct fib_info));
	fi->fib_flags = flags;
	fi->fib_dev = dev;
	fi->fib_gateway = gw;
	fi->fib_mtu = mss;
	fi->fib_window = window;
	fi->fib_refcnt++;
	fi->fib_next = fib_info_list;
	fi->fib_prev = NULL;
	fi->fib_irtt = irtt;
	if (fib_info_list)
		fib_info_list->fib_prev = fi;
	fib_info_list = fi;
#if RT_CACHE_DEBUG >= 2
	printk("fib_create_info: fi %08x/%s is created\n", fi->fib_gateway, fi->fib_dev->name);
#endif
	return fi;
}


static __inline__ void fib_add_1(short flags, __u32 dst, __u32 mask,
	__u32 gw, struct device *dev, unsigned short mss,
	unsigned long window, unsigned short irtt, short metric)
{
	struct fib_node *f, *f1;
	struct fib_node **fp;
	struct fib_node **dup_fp = NULL;
	struct fib_zone * fz;
	struct fib_info * fi;
	int logmask;

	/*
	 *	Allocate an entry and fill it in.
	 */
	 
	f = (struct fib_node *) kmalloc(sizeof(struct fib_node), GFP_KERNEL);
	if (f == NULL)
		return;

	memset(f, 0, sizeof(struct fib_node));
	f->fib_dst = dst;
	f->fib_metric = metric;
	f->fib_tos    = 0;

	if  ((fi = fib_create_info(gw, dev, flags, mss, window, irtt)) == NULL)
	{
		kfree_s(f, sizeof(struct fib_node));
		return;
	}
	f->fib_info = fi;

	logmask = rt_logmask(mask);
	fz = fib_zones[logmask];


	if (!fz)
	{
		int i;
		fz = kmalloc(sizeof(struct fib_zone), GFP_KERNEL);
		if (!fz)
		{
			fib_free_node(f);
			return;
		}
		memset(fz, 0, sizeof(struct fib_zone));
		fz->fz_logmask = logmask;
		fz->fz_mask = mask;
		for (i=logmask-1; i>=0; i--)
			if (fib_zones[i])
				break;
		cli();
		if (i<0)
		{
			fz->fz_next = fib_zone_list;
			fib_zone_list = fz;
		}
		else
		{
			fz->fz_next = fib_zones[i]->fz_next;
			fib_zones[i]->fz_next = fz;
		}
		fib_zones[logmask] = fz;
		sti();
	}

	/*
	 * If zone overgrows RTZ_HASHING_LIMIT, create hash table.
	 */

	if (fz->fz_nent >= RTZ_HASHING_LIMIT && !fz->fz_hash_table && logmask<32)
	{
		struct fib_node ** ht;
#if RT_CACHE_DEBUG >= 2
		printk("fib_add_1: hashing for zone %d started\n", logmask);
#endif
		ht = kmalloc(RTZ_HASH_DIVISOR*sizeof(struct rtable*), GFP_KERNEL);

		if (ht)
		{
			memset(ht, 0, RTZ_HASH_DIVISOR*sizeof(struct fib_node*));
			cli();
			f1 = fz->fz_list;
			while (f1)
			{
				struct fib_node * next;
				unsigned hash = fz_hash_code(f1->fib_dst, logmask);
				next = f1->fib_next;
				f1->fib_next = ht[hash];
				ht[hash] = f1;
				f1 = next;
			}
			fz->fz_list = NULL;
			fz->fz_hash_table = ht; 
			sti();
		}
	}

	if (fz->fz_hash_table)
		fp = &fz->fz_hash_table[fz_hash_code(dst, logmask)];
	else
		fp = &fz->fz_list;

	/*
	 * Scan list to find the first route with the same destination
	 */
	while ((f1 = *fp) != NULL)
	{
		if (f1->fib_dst == dst)
			break;
		fp = &f1->fib_next;
	}

	/*
	 * Find route with the same destination and less (or equal) metric.
	 */
	while ((f1 = *fp) != NULL && f1->fib_dst == dst)
	{
		if (f1->fib_metric >= metric)
			break;
		/*
		 *	Record route with the same destination and gateway,
		 *	but less metric. We'll delete it 
		 *	after instantiation of new route.
		 */
		if (f1->fib_info->fib_gateway == gw &&
		    (gw || f1->fib_info->fib_dev == dev))
			dup_fp = fp;
		fp = &f1->fib_next;
	}

	/*
	 * Is it already present?
	 */

	if (f1 && f1->fib_metric == metric && f1->fib_info == fi)
	{
		fib_free_node(f);
		return;
	}
	
	/*
	 * Insert new entry to the list.
	 */

	cli();
	f->fib_next = f1;
	*fp = f;
	if (!fib_loopback && (fi->fib_dev->flags & IFF_LOOPBACK))
		fib_loopback = f;
	sti();
	fz->fz_nent++;
	ip_netlink_msg(RTMSG_NEWROUTE, dst, gw, mask, flags, metric, fi->fib_dev->name);

	/*
	 *	Delete route with the same destination and gateway.
	 *	Note that we should have at most one such route.
	 */
	if (dup_fp)
		fp = dup_fp;
	else
		fp = &f->fib_next;

	while ((f1 = *fp) != NULL && f1->fib_dst == dst)
	{
		if (f1->fib_info->fib_gateway == gw &&
		    (gw || f1->fib_info->fib_dev == dev))
		{
			cli();
			*fp = f1->fib_next;
			if (fib_loopback == f1)
				fib_loopback = NULL;
			sti();
			ip_netlink_msg(RTMSG_DELROUTE, dst, gw, mask, flags, metric, f1->fib_info->fib_dev->name);
			fib_free_node(f1);
			fz->fz_nent--;
			break;
		}
		fp = &f1->fib_next;
	}
	rt_cache_flush();
	return;
}

static int rt_flush_list(struct fib_node ** fp, struct device *dev)
{
	int found = 0;
	struct fib_node *f;

	while ((f = *fp) != NULL) {
/*
 *	"Magic" device route is allowed to point to loopback,
 *	discard it too.
 */
		if (f->fib_info->fib_dev != dev &&
		    (f->fib_info->fib_dev != &loopback_dev || f->fib_dst != dev->pa_addr)) {
			fp = &f->fib_next;
			continue;
		}
		cli();
		*fp = f->fib_next;
		if (fib_loopback == f)
			fib_loopback = NULL;
		sti();
		fib_free_node(f);
		found++;
	}
	return found;
}

static __inline__ void fib_flush_1(struct device *dev)
{
	struct fib_zone *fz;
	int found = 0;

	for (fz = fib_zone_list; fz; fz = fz->fz_next)
	{
		if (fz->fz_hash_table)
		{
			int i;
			int tmp = 0;
			for (i=0; i<RTZ_HASH_DIVISOR; i++)
				tmp += rt_flush_list(&fz->fz_hash_table[i], dev);
			fz->fz_nent -= tmp;
			found += tmp;
		}
		else
		{
			int tmp;
			tmp = rt_flush_list(&fz->fz_list, dev);
			fz->fz_nent -= tmp;
			found += tmp;
		}
	}
		
	if (found)
		rt_cache_flush();
}


/* 
 *	Called from the PROCfs module. This outputs /proc/net/route.
 *
 *	We preserve the old format but pad the buffers out. This means that
 *	we can spin over the other entries as we read them. Remember the
 *	gated BGP4 code could need to read 60,000+ routes on occasion (that's
 *	about 7Mb of data). To do that ok we will need to also cache the
 *	last route we got to (reads will generally be following on from
 *	one another without gaps).
 */
 
int rt_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct fib_zone *fz;
	struct fib_node *f;
	int len=0;
	off_t pos=0;
	char temp[129];
	int i;
	
	pos = 128;

	if (offset<128)
	{
		sprintf(buffer,"%-127s\n","Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT");
		len = 128;
  	}
  	
	while  (ip_rt_lock)
		sleep_on(&rt_wait);
	ip_rt_fast_lock();

	for (fz=fib_zone_list; fz; fz = fz->fz_next)
	{
		int maxslot;
		struct fib_node ** fp;

		if (fz->fz_nent == 0)
			continue;

		if (pos + 128*fz->fz_nent <= offset)
		{
			pos += 128*fz->fz_nent;
			len = 0;
			continue;
		}

		if (fz->fz_hash_table)
		{
			maxslot = RTZ_HASH_DIVISOR;
			fp	= fz->fz_hash_table;
		}
		else
		{
			maxslot	= 1;
			fp	= &fz->fz_list;
		}
			
		for (i=0; i < maxslot; i++, fp++)
		{
			
			for (f = *fp; f; f = f->fib_next) 
			{
				struct fib_info * fi;
				/*
				 *	Spin through entries until we are ready
				 */
				pos += 128;

				if (pos <= offset)
				{
					len=0;
					continue;
				}
					
				fi = f->fib_info;
				sprintf(temp, "%s\t%08lX\t%08lX\t%02X\t%d\t%lu\t%d\t%08lX\t%d\t%lu\t%u",
					fi->fib_dev->name, (unsigned long)f->fib_dst, (unsigned long)fi->fib_gateway,
					fi->fib_flags, 0, f->fib_use, f->fib_metric,
					(unsigned long)fz->fz_mask, (int)fi->fib_mtu, fi->fib_window, (int)fi->fib_irtt);
				sprintf(buffer+len,"%-127s\n",temp);

				len += 128;
				if (pos >= offset+length)
					goto done;
			}
		}
        }

done:
	ip_rt_unlock();
	wake_up(&rt_wait);
  	
  	*start = buffer+len-(pos-offset);
  	len = pos - offset;
  	if (len>length)
  		len = length;
  	return len;
}

int rt_cache_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len=0;
	off_t pos=0;
	char temp[129];
	struct rtable *r;
	int i;

	pos = 128;

	if (offset<128)
	{
		sprintf(buffer,"%-127s\n","Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tSource\t\tMTU\tWindow\tIRTT\tHH\tARP");
		len = 128;
  	}
	
  	
	while  (ip_rt_lock)
		sleep_on(&rt_wait);
	ip_rt_fast_lock();

	for (i = 0; i<RT_HASH_DIVISOR; i++)
	{
		for (r = ip_rt_hash_table[i]; r; r = r->rt_next) 
		{
			/*
			 *	Spin through entries until we are ready
			 */
			pos += 128;

			if (pos <= offset)
			{
				len = 0;
				continue;
			}
					
			sprintf(temp, "%s\t%08lX\t%08lX\t%02X\t%d\t%u\t%d\t%08lX\t%d\t%lu\t%u\t%d\t%1d",
				r->rt_dev->name, (unsigned long)r->rt_dst, (unsigned long)r->rt_gateway,
				r->rt_flags, r->rt_refcnt, r->rt_use, 0,
				(unsigned long)r->rt_src, (int)r->rt_mtu, r->rt_window, (int)r->rt_irtt, r->rt_hh ? r->rt_hh->hh_refcnt : -1, r->rt_hh ? r->rt_hh->hh_uptodate : 0);
			sprintf(buffer+len,"%-127s\n",temp);
			len += 128;
			if (pos >= offset+length)
				goto done;
		}
        }

done:
	ip_rt_unlock();
	wake_up(&rt_wait);
  	
  	*start = buffer+len-(pos-offset);
  	len = pos-offset;
  	if (len>length)
  		len = length;
  	return len;
}


static void rt_free(struct rtable * rt)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	if (!rt->rt_refcnt)
	{
		struct hh_cache * hh = rt->rt_hh;
		rt->rt_hh = NULL;
		restore_flags(flags);
		if (hh && atomic_dec_and_test(&hh->hh_refcnt))
			kfree_s(hh, sizeof(struct hh_cache));
		kfree_s(rt, sizeof(struct rt_table));
		return;
	}
	rt->rt_next = rt_free_queue;
	rt->rt_flags &= ~RTF_UP;
	rt_free_queue = rt;
	ip_rt_bh_mask |= RT_BH_FREE;
#if RT_CACHE_DEBUG >= 2
	printk("rt_free: %08x\n", rt->rt_dst);
#endif
	restore_flags(flags);
}

/*
 * RT "bottom half" handlers. Called with masked interrupts.
 */

static __inline__ void rt_kick_free_queue(void)
{
	struct rtable *rt, **rtp;

	rtp = &rt_free_queue;

	while ((rt = *rtp) != NULL)
	{
		if  (!rt->rt_refcnt)
		{
			struct hh_cache * hh = rt->rt_hh;
#if RT_CACHE_DEBUG >= 2
			__u32 daddr = rt->rt_dst;
#endif
			*rtp = rt->rt_next;
			rt->rt_hh = NULL;
			sti();
			if (hh && atomic_dec_and_test(&hh->hh_refcnt))
				kfree_s(hh, sizeof(struct hh_cache));
			kfree_s(rt, sizeof(struct rt_table));
#if RT_CACHE_DEBUG >= 2
			printk("rt_kick_free_queue: %08x is free\n", daddr);
#endif
			cli();
			continue;
		}
		rtp = &rt->rt_next;
	}
}

void ip_rt_run_bh()
{
	unsigned long flags;
	save_flags(flags);
	cli();
	if (ip_rt_bh_mask && !ip_rt_lock)
	{
		if (ip_rt_bh_mask & RT_BH_REDIRECT)
			rt_kick_backlog();

		if (ip_rt_bh_mask & RT_BH_GARBAGE_COLLECT)
		{
			ip_rt_fast_lock();
			ip_rt_bh_mask &= ~RT_BH_GARBAGE_COLLECT;
			sti();
			rt_garbage_collect_1();
			cli();
			ip_rt_fast_unlock();
		}

		if (ip_rt_bh_mask & RT_BH_FREE)
			rt_kick_free_queue();
	}
	restore_flags(flags);
}


void ip_rt_check_expire()
{
	ip_rt_fast_lock();
	if (ip_rt_lock == 1)
	{
		int i;
		struct rtable *rth, **rthp;
		unsigned long flags;
		unsigned long now = jiffies;

		save_flags(flags);
		for (i=0; i<RT_HASH_DIVISOR; i++)
		{
			rthp = &ip_rt_hash_table[i];

			while ((rth = *rthp) != NULL)
			{
				struct rtable * rth_next = rth->rt_next;

				/*
				 * Cleanup aged off entries.
				 */

				cli();
				if (!rth->rt_refcnt && rth->rt_lastuse + RT_CACHE_TIMEOUT < now)
				{
					*rthp = rth_next;
					sti();
					rt_cache_size--;
#if RT_CACHE_DEBUG >= 2
					printk("rt_check_expire clean %02x@%08x\n", i, rth->rt_dst);
#endif
					rt_free(rth);
					continue;
				}
				sti();

				if (!rth_next)
					break;

				/*
				 * LRU ordering.
				 */

				if (rth->rt_lastuse + RT_CACHE_BUBBLE_THRESHOLD < rth_next->rt_lastuse ||
				    (rth->rt_lastuse < rth_next->rt_lastuse &&
				     rth->rt_use < rth_next->rt_use))
				{
#if RT_CACHE_DEBUG >= 2
					printk("rt_check_expire bubbled %02x@%08x<->%08x\n", i, rth->rt_dst, rth_next->rt_dst);
#endif
					cli();
					*rthp = rth_next;
					rth->rt_next = rth_next->rt_next;
					rth_next->rt_next = rth;
					sti();
					rthp = &rth_next->rt_next;
					continue;
				}
				rthp = &rth->rt_next;
			}
		}
		restore_flags(flags);
		rt_kick_free_queue();
	}
	ip_rt_unlock();
}

static void rt_redirect_1(__u32 dst, __u32 gw, struct device *dev)
{
	struct rtable *rt;
	unsigned long hash = ip_rt_hash_code(dst);

	if (gw == dev->pa_addr)
		return;
	if (dev != get_gw_dev(gw))
		return;
	rt = (struct rtable *) kmalloc(sizeof(struct rtable), GFP_ATOMIC);
	if (rt == NULL) 
		return;
	memset(rt, 0, sizeof(struct rtable));
	rt->rt_flags = RTF_DYNAMIC | RTF_MODIFIED | RTF_HOST | RTF_GATEWAY | RTF_UP;
	rt->rt_dst = dst;
	rt->rt_dev = dev;
	rt->rt_gateway = gw;
	rt->rt_src = dev->pa_addr;
	rt->rt_mtu = dev->mtu;
#ifdef CONFIG_NO_PATH_MTU_DISCOVERY
	if (dev->mtu > 576)
		rt->rt_mtu = 576;
#endif
	rt->rt_lastuse  = jiffies;
	rt->rt_refcnt  = 1;
	rt_cache_add(hash, rt);
	ip_rt_put(rt);
	return;
}

static void rt_cache_flush(void)
{
	int i;
	struct rtable * rth, * next;

	for (i=0; i<RT_HASH_DIVISOR; i++)
	{
		int nr=0;

		cli();
		if (!(rth = ip_rt_hash_table[i]))
		{
			sti();
			continue;
		}

		ip_rt_hash_table[i] = NULL;
		sti();

		for (; rth; rth=next)
		{
			next = rth->rt_next;
			rt_cache_size--;
			nr++;
			rth->rt_next = NULL;
			rt_free(rth);
		}
#if RT_CACHE_DEBUG >= 2
		if (nr > 0)
			printk("rt_cache_flush: %d@%02x\n", nr, i);
#endif
	}
#if RT_CACHE_DEBUG >= 1
	if (rt_cache_size)
	{
		printk("rt_cache_flush: bug rt_cache_size=%d\n", rt_cache_size);
		rt_cache_size = 0;
	}
#endif
}

static void rt_garbage_collect_1(void)
{
	int i;
	unsigned expire = RT_CACHE_TIMEOUT>>1;
	struct rtable * rth, **rthp;
	unsigned long now = jiffies;

	for (;;)
	{
		for (i=0; i<RT_HASH_DIVISOR; i++)
		{
			if (!ip_rt_hash_table[i])
				continue;
			for (rthp=&ip_rt_hash_table[i]; (rth=*rthp); rthp=&rth->rt_next)
			{
				if (rth->rt_lastuse + expire*(rth->rt_refcnt+1) > now)
					continue;
				rt_cache_size--;
				cli();
				*rthp=rth->rt_next;
				rth->rt_next = NULL;
				sti();
				rt_free(rth);
				break;
			}
		}
		if (rt_cache_size < RT_CACHE_SIZE_MAX)
			return;
		expire >>= 1;
	}
}

static __inline__ void rt_req_enqueue(struct rt_req **q, struct rt_req *rtr)
{
	unsigned long flags;
	struct rt_req * tail;

	save_flags(flags);
	cli();
	tail = *q;
	if (!tail)
		rtr->rtr_next = rtr;
	else
	{
		rtr->rtr_next = tail->rtr_next;
		tail->rtr_next = rtr;
	}
	*q = rtr;
	restore_flags(flags);
	return;
}

/*
 * Caller should mask interrupts.
 */

static __inline__ struct rt_req * rt_req_dequeue(struct rt_req **q)
{
	struct rt_req * rtr;

	if (*q)
	{
		rtr = (*q)->rtr_next;
		(*q)->rtr_next = rtr->rtr_next;
		if (rtr->rtr_next == rtr)
			*q = NULL;
		rtr->rtr_next = NULL;
		return rtr;
	}
	return NULL;
}

/*
   Called with masked interrupts
 */

static void rt_kick_backlog()
{
	if (!ip_rt_lock)
	{
		struct rt_req * rtr;

		ip_rt_fast_lock();

		while ((rtr = rt_req_dequeue(&rt_backlog)) != NULL)
		{
			sti();
			rt_redirect_1(rtr->dst, rtr->gw, rtr->dev);
			kfree_s(rtr, sizeof(struct rt_req));
			cli();
		}

		ip_rt_bh_mask &= ~RT_BH_REDIRECT;

		ip_rt_fast_unlock();
	}
}

/*
 * rt_{del|add|flush} called only from USER process. Waiting is OK.
 */

static int rt_del(__u32 dst, __u32 mask,
		struct device * dev, __u32 gtw, short rt_flags, short metric)
{
	int retval;

	while (ip_rt_lock)
		sleep_on(&rt_wait);
	ip_rt_fast_lock();
	retval = fib_del_1(dst, mask, dev, gtw, rt_flags, metric);
	ip_rt_unlock();
	wake_up(&rt_wait);
	return retval;
}

static void rt_add(short flags, __u32 dst, __u32 mask,
	__u32 gw, struct device *dev, unsigned short mss,
	unsigned long window, unsigned short irtt, short metric)
{
	while (ip_rt_lock)
		sleep_on(&rt_wait);
	ip_rt_fast_lock();
	fib_add_1(flags, dst, mask, gw, dev, mss, window, irtt, metric);
	ip_rt_unlock();
	wake_up(&rt_wait);
}

void ip_rt_flush(struct device *dev)
{
	while (ip_rt_lock)
		sleep_on(&rt_wait);
	ip_rt_fast_lock();
	fib_flush_1(dev);
	ip_rt_unlock();
	wake_up(&rt_wait);
}

/*
   Called by ICMP module.
 */

void ip_rt_redirect(__u32 src, __u32 dst, __u32 gw, struct device *dev)
{
	struct rt_req * rtr;
	struct rtable * rt;

	rt = ip_rt_route(dst, 0);
	if (!rt)
		return;

	if (rt->rt_gateway != src ||
	    rt->rt_dev != dev ||
	    ((gw^dev->pa_addr)&dev->pa_mask) ||
	    ip_chk_addr(gw))
	{
		ip_rt_put(rt);
		return;
	}
	ip_rt_put(rt);

	ip_rt_fast_lock();
	if (ip_rt_lock == 1)
	{
		rt_redirect_1(dst, gw, dev);
		ip_rt_unlock();
		return;
	}

	rtr = kmalloc(sizeof(struct rt_req), GFP_ATOMIC);
	if (rtr)
	{
		rtr->dst = dst;
		rtr->gw = gw;
		rtr->dev = dev;
		rt_req_enqueue(&rt_backlog, rtr);
		ip_rt_bh_mask |= RT_BH_REDIRECT;
	}
	ip_rt_unlock();
}


static __inline__ void rt_garbage_collect(void)
{
	if (ip_rt_lock == 1)
	{
		rt_garbage_collect_1();
		return;
	}
	ip_rt_bh_mask |= RT_BH_GARBAGE_COLLECT;
}

static void rt_cache_add(unsigned hash, struct rtable * rth)
{
	unsigned long	flags;
	struct rtable	**rthp;
	__u32		daddr = rth->rt_dst;
	unsigned long	now = jiffies;

#if RT_CACHE_DEBUG >= 2
	if (ip_rt_lock != 1)
	{
		printk("rt_cache_add: ip_rt_lock==%d\n", ip_rt_lock);
		return;
	}
#endif

	save_flags(flags);

	if (rth->rt_dev->header_cache_bind)
	{
		struct rtable * rtg = rth;

		if (rth->rt_gateway != daddr)
		{
			ip_rt_fast_unlock();
			rtg = ip_rt_route(rth->rt_gateway, 0);
			ip_rt_fast_lock();
		}

		if (rtg)
		{
			if (rtg == rth)
				rtg->rt_dev->header_cache_bind(&rtg->rt_hh, rtg->rt_dev, ETH_P_IP, rtg->rt_dst);
			else
			{
				if (rtg->rt_hh)
					atomic_inc(&rtg->rt_hh->hh_refcnt);
				rth->rt_hh = rtg->rt_hh;
				ip_rt_put(rtg);
			}
		}
	}

	if (rt_cache_size >= RT_CACHE_SIZE_MAX)
		rt_garbage_collect();

	cli();
	rth->rt_next = ip_rt_hash_table[hash];
#if RT_CACHE_DEBUG >= 2
	if (rth->rt_next)
	{
		struct rtable * trth;
		printk("rt_cache @%02x: %08x", hash, daddr);
		for (trth=rth->rt_next; trth; trth=trth->rt_next)
			printk(" . %08x", trth->rt_dst);
		printk("\n");
	}
#endif
	ip_rt_hash_table[hash] = rth;
	rthp = &rth->rt_next;
	sti();
	rt_cache_size++;

	/*
	 * Cleanup duplicate (and aged off) entries.
	 */

	while ((rth = *rthp) != NULL)
	{

		cli();
		if ((!rth->rt_refcnt && rth->rt_lastuse + RT_CACHE_TIMEOUT < now)
		    || rth->rt_dst == daddr)
		{
			*rthp = rth->rt_next;
			rt_cache_size--;
			sti();
#if RT_CACHE_DEBUG >= 2
			printk("rt_cache clean %02x@%08x\n", hash, rth->rt_dst);
#endif
			rt_free(rth);
			continue;
		}
		sti();
		rthp = &rth->rt_next;
	}
	restore_flags(flags);
}

/*
   RT should be already locked.
   
   We could improve this by keeping a chain of say 32 struct rtable's
   last freed for fast recycling.
   
 */

struct rtable * ip_rt_slow_route (__u32 daddr, int local)
{
	unsigned hash = ip_rt_hash_code(daddr)^local;
	struct rtable * rth;
	struct fib_node * f;
	struct fib_info * fi;
	__u32 saddr;

#if RT_CACHE_DEBUG >= 2
	printk("rt_cache miss @%08x\n", daddr);
#endif

	rth = kmalloc(sizeof(struct rtable), GFP_ATOMIC);
	if (!rth)
	{
		ip_rt_unlock();
		return NULL;
	}

	if (local)
		f = fib_lookup_local(daddr);
	else
		f = fib_lookup (daddr);

	if (f)
	{
		fi = f->fib_info;
		f->fib_use++;
	}

	if (!f || (fi->fib_flags & RTF_REJECT))
	{
#ifdef CONFIG_KERNELD	
		char wanted_route[20];
#endif		
#if RT_CACHE_DEBUG >= 2
		printk("rt_route failed @%08x\n", daddr);
#endif
		ip_rt_unlock();
		kfree_s(rth, sizeof(struct rtable));
#ifdef CONFIG_KERNELD		
		daddr=ntohl(daddr);
		sprintf(wanted_route, "%d.%d.%d.%d",
			(int)(daddr >> 24) & 0xff, (int)(daddr >> 16) & 0xff,
			(int)(daddr >> 8) & 0xff, (int)daddr & 0xff);
		kerneld_route(wanted_route); 	/* Dynamic route request */
#endif		
		return NULL;
	}

	saddr = fi->fib_dev->pa_addr;

	if (daddr == fi->fib_dev->pa_addr)
	{
		f->fib_use--;
		if ((f = fib_loopback) != NULL)
		{
			f->fib_use++;
			fi = f->fib_info;
		}
	}
	
	if (!f)
	{
		ip_rt_unlock();
		kfree_s(rth, sizeof(struct rtable));
		return NULL;
	}

	rth->rt_dst	= daddr;
	rth->rt_src	= saddr;
	rth->rt_lastuse	= jiffies;
	rth->rt_refcnt	= 1;
	rth->rt_use	= 1;
	rth->rt_next	= NULL;
	rth->rt_hh	= NULL;
	rth->rt_gateway	= fi->fib_gateway;
	rth->rt_dev	= fi->fib_dev;
	rth->rt_mtu	= fi->fib_mtu;
	rth->rt_window	= fi->fib_window;
	rth->rt_irtt	= fi->fib_irtt;
	rth->rt_tos	= f->fib_tos;
	rth->rt_flags   = fi->fib_flags | RTF_HOST;
	if (local)
		rth->rt_flags   |= RTF_LOCAL;

	if (!(rth->rt_flags & RTF_GATEWAY))
		rth->rt_gateway = rth->rt_dst;
	/*
	 *	Multicast or limited broadcast is never gatewayed.
	 */
	if (MULTICAST(daddr) || daddr == 0xFFFFFFFF)
		rth->rt_gateway = rth->rt_dst;

	if (ip_rt_lock == 1)
		rt_cache_add(hash, rth);
	else
	{
		rt_free(rth);
#if RT_CACHE_DEBUG >= 1
		printk(KERN_DEBUG "rt_cache: route to %08x was born dead\n", daddr);
#endif
	}

	ip_rt_unlock();
	return rth;
}

void ip_rt_put(struct rtable * rt)
{
	if (rt)
		atomic_dec(&rt->rt_refcnt);
}

struct rtable * ip_rt_route(__u32 daddr, int local)
{
	struct rtable * rth;

	ip_rt_fast_lock();

	for (rth=ip_rt_hash_table[ip_rt_hash_code(daddr)^local]; rth; rth=rth->rt_next)
	{
		if (rth->rt_dst == daddr)
		{
			rth->rt_lastuse = jiffies;
			atomic_inc(&rth->rt_use);
			atomic_inc(&rth->rt_refcnt);
			ip_rt_unlock();
			return rth;
		}
	}
	return ip_rt_slow_route (daddr, local);
}

/*
 *	Process a route add request from the user, or from a kernel
 *	task.
 */
 
int ip_rt_new(struct rtentry *r)
{
	int err;
	char * devname;
	struct device * dev = NULL;
	unsigned long flags;
	__u32 daddr, mask, gw;
	short metric;

	/*
	 *	If a device is specified find it.
	 */
	 
	if ((devname = r->rt_dev) != NULL) 
	{
		err = getname(devname, &devname);
		if (err)
			return err;
		dev = dev_get(devname);
		putname(devname);
		if (!dev)
			return -ENODEV;
	}
	
	/*
	 *	If the device isn't INET, don't allow it
	 */

	if (r->rt_dst.sa_family != AF_INET)
		return -EAFNOSUPPORT;

	/*
	 *	Make local copies of the important bits
	 *	We decrement the metric by one for BSD compatibility.
	 */
	 
	flags = r->rt_flags;
	daddr = (__u32) ((struct sockaddr_in *) &r->rt_dst)->sin_addr.s_addr;
	mask  = (__u32) ((struct sockaddr_in *) &r->rt_genmask)->sin_addr.s_addr;
	gw    = (__u32) ((struct sockaddr_in *) &r->rt_gateway)->sin_addr.s_addr;
	metric = r->rt_metric > 0 ? r->rt_metric - 1 : 0;

	/*
	 *	BSD emulation: Permits route add someroute gw one-of-my-addresses
	 *	to indicate which iface. Not as clean as the nice Linux dev technique
	 *	but people keep using it...  (and gated likes it ;))
	 */
	 
	if (!dev && (flags & RTF_GATEWAY)) 
	{
		struct device *dev2;
		for (dev2 = dev_base ; dev2 != NULL ; dev2 = dev2->next) 
		{
			if ((dev2->flags & IFF_UP) && dev2->pa_addr == gw) 
			{
				flags &= ~RTF_GATEWAY;
				dev = dev2;
				break;
			}
		}
	}

	if (flags & RTF_HOST) 
		mask = 0xffffffff;
	else if (mask && r->rt_genmask.sa_family != AF_INET)
		return -EAFNOSUPPORT;

	if (flags & RTF_GATEWAY) 
	{
		if (r->rt_gateway.sa_family != AF_INET)
			return -EAFNOSUPPORT;

		/*
		 *	Don't try to add a gateway we can't reach.. 
		 *	Tunnel devices are exempt from this rule.
		 */

		if (!dev)
			dev = get_gw_dev(gw);
		else if (dev != get_gw_dev(gw) && dev->type != ARPHRD_TUNNEL)
			return -EINVAL;
		if (!dev)
			return -ENETUNREACH;
	} 
	else
	{
		gw = 0;
		if (!dev)
			dev = ip_dev_bynet(daddr, mask);
		if (!dev)
			return -ENETUNREACH;
		if (!mask)
		{
			if (((daddr ^ dev->pa_addr) & dev->pa_mask) == 0)
				mask = dev->pa_mask;
		}
	}

#ifndef CONFIG_IP_CLASSLESS
	if (!mask)
		mask = ip_get_mask(daddr);
#endif
	
	if (bad_mask(mask, daddr))
		return -EINVAL;

	/*
	 *	Add the route
	 */

	rt_add(flags, daddr, mask, gw, dev, r->rt_mss, r->rt_window, r->rt_irtt, metric);
	return 0;
}


/*
 *	Remove a route, as requested by the user.
 */

int ip_rt_kill(struct rtentry *r)
{
	struct sockaddr_in *trg;
	struct sockaddr_in *msk;
	struct sockaddr_in *gtw;
	char *devname;
	int err;
	struct device * dev = NULL;

	trg = (struct sockaddr_in *) &r->rt_dst;
	msk = (struct sockaddr_in *) &r->rt_genmask;
	gtw = (struct sockaddr_in *) &r->rt_gateway;
	if ((devname = r->rt_dev) != NULL) 
	{
		err = getname(devname, &devname);
		if (err)
			return err;
		dev = dev_get(devname);
		putname(devname);
		if (!dev)
			return -ENODEV;
	}
	/*
	 * metric can become negative here if it wasn't filled in
	 * but that's a fortunate accident; we really use that in rt_del.
	 */
	err=rt_del((__u32)trg->sin_addr.s_addr, (__u32)msk->sin_addr.s_addr, dev,
		(__u32)gtw->sin_addr.s_addr, r->rt_flags, r->rt_metric - 1);
	return err;
}

/*
 *	Handle IP routing ioctl calls. These are used to manipulate the routing tables
 */
 
int ip_rt_ioctl(unsigned int cmd, void *arg)
{
	int err;
	struct rtentry rt;

	switch(cmd) 
	{
		case SIOCADDRT:		/* Add a route */
		case SIOCDELRT:		/* Delete a route */
			if (!suser())
				return -EPERM;
			err=verify_area(VERIFY_READ, arg, sizeof(struct rtentry));
			if (err)
				return err;
			memcpy_fromfs(&rt, arg, sizeof(struct rtentry));
			return (cmd == SIOCDELRT) ? ip_rt_kill(&rt) : ip_rt_new(&rt);
	}

	return -EINVAL;
}

void ip_rt_advice(struct rtable **rp, int advice)
{
	/* Thanks! */
	return;
}

void ip_rt_update(int event, struct device *dev)
{
/*
 *	This causes too much grief to do now.
 */
#ifdef COMING_IN_2_1
	if (event == NETDEV_UP)
		rt_add(RTF_HOST|RTF_UP, dev->pa_addr, ~0, 0, dev, 0, 0, 0, 0);
	else if (event == NETDEV_DOWN)
		rt_del(dev->pa_addr, ~0, dev, 0, RTF_HOST|RTF_UP, 0);
#endif		
}
