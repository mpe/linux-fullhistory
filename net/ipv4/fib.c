/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 Forwarding Information Base.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *
 *	NOTE:	This file is scheduled to be removed from kernel.
 *		The natural place for router FIB is user level
 *		routing daemon (it has to keep its copy in any case)
 *		
 *		Kernel should keep only interface routes and,
 *		if host is not router, default gateway.
 *
 *		We have good proof that it is feasible and efficient -
 *		multicast routing.
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
#include <linux/skbuff.h>

#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/arp.h>
#include <net/netlink.h>
#include <net/ip_fib.h>
#include <net/dst.h>
#include <linux/net_alias.h>

static struct fib_class local_class = {RT_CLASS_LOCAL, };
static struct fib_class default_class = {RT_CLASS_DEFAULT, };
static struct fib_class main_class = {RT_CLASS_MAIN, };
static struct fib_class *fib_classes[RT_CLASS_MAX+1];

static struct fib_rule  *fib_rules;

static struct fib_info 	*fib_info_list;

static int fib_stamp;

static int rtmsg_process(struct nlmsghdr *n, struct in_rtmsg *r);


#ifdef CONFIG_RTNETLINK

static unsigned rt_nl_flags;
static int rt_nl_owner = -1;

/*
 *	Default mode is delayed for 0.5sec batch delivery.
 *	If someone starts to use user->level calls,
 *	we turn on synchronous message passing.
 */

#define RTMSG_DELAY (HZ/2)

static struct nlmsg_ctl rtmsg_ctl = {
	{ NULL, NULL, 0, 0L, NULL },
	NULL,
	NETLINK_ROUTE,
	RTMSG_DELAY,
	NLMSG_GOODSIZE,
	0, 0, 0, 0
};

static void __rtmsg_ack(struct nlmsghdr *n, int err);

static __inline__ void rtmsg_ack(struct nlmsghdr *n, int err)
{
	if (n->nlmsg_seq && rt_nl_flags&RTCTL_ACK)
		__rtmsg_ack(n, err);
}

static void rtmsg_fib(unsigned long type, struct fib_node *f, int logmask,
		      struct fib_class *class, struct nlmsghdr *n);
static void rtmsg_dev(unsigned long type, struct device *dev, struct nlmsghdr *n);
#define rtmsg_kick() ({ if (rtmsg_ctl.nlmsg_skb) nlmsg_transmit(&rtmsg_ctl); })

#else
#define rtmsg_fib(a,b,c,d,e)
#define rtmsg_dev(a,b,c)
#define rtmsg_ack(a,b)
#define rtmsg_kick()
#endif


/*
 *	FIB locking.
 */

static struct wait_queue *fib_wait;
atomic_t fib_users;

static void fib_lock(void)
{
	while (fib_users)
		sleep_on(&fib_wait);
	atomic_inc(&fib_users);
	dev_lock_list();
}

static void fib_unlock(void)
{
	dev_unlock_list();
	if (atomic_dec_and_test(&fib_users)) {
		rtmsg_kick();
		wake_up(&fib_wait);
	}
}

/*
 *	Check if a mask is acceptable.
 */
 
static __inline__ int bad_mask(u32 mask, u32 addr)
{
	if (addr & (mask = ~mask))
		return 1;
	mask = ntohl(mask);
	if (mask & (mask+1))
		return 1;
	return 0;
}

/* 
 * Evaluate mask length.
 */

static __inline__ int fib_logmask(u32 mask)
{
	if (!(mask = ntohl(mask)))
		return 32;
	return ffz(~mask);
}

/* 
 * Create mask from mask length.
 */

static __inline__ u32 fib_mask(int logmask)
{
	if (logmask >= 32)
		return 0;
	return htonl(~((1<<logmask)-1));
}

static __inline__ u32 fib_netmask(int logmask)
{
	return fib_mask(32-logmask);
}


static struct fib_class *fib_alloc_class(int id)
{
	struct fib_class *class;

	if (fib_classes[id])
		return fib_classes[id];

	class = kmalloc(sizeof(*class), GFP_KERNEL);
	if (!class)
		return NULL;
	memset(class, 0, sizeof(*class));
	class->cl_id = id;
	fib_classes[id] = class;
	return class;
}

static struct fib_class *fib_empty_class(void)
{
	int id;
	for (id = 1; id <= RT_CLASS_MAX; id++)
		if (fib_classes[id] == NULL)
			return fib_alloc_class(id);
	return NULL;
}

static int fib_rule_delete(struct in_rtrulemsg *r, struct device *dev, struct nlmsghdr *n)
{
	u32 src = r->rtrmsg_src.s_addr;
	u32 dst = r->rtrmsg_dst.s_addr;
	u32 srcmask = fib_netmask(r->rtrmsg_srclen);
	u32 dstmask = fib_netmask(r->rtrmsg_dstlen);
	struct fib_rule *cl, **clp;

	for (clp=&fib_rules; (cl=*clp) != NULL; clp=&cl->cl_next) {
		if (src == cl->cl_src &&
		    srcmask == cl->cl_srcmask &&
		    dst == cl->cl_dst &&
		    dstmask == cl->cl_dstmask &&
		    r->rtrmsg_tos == cl->cl_tos &&
		    dev == cl->cl_dev &&
		    r->rtrmsg_action == cl->cl_action &&
		    (!r->rtrmsg_preference || r->rtrmsg_preference == cl->cl_preference) &&
		    (!r->rtrmsg_class || (cl && r->rtrmsg_class == cl->cl_class->cl_id))) {
			cli();
			*clp = cl->cl_next;
			sti();
			if (cl->cl_class)
				cl->cl_class->cl_users--;
			kfree(cl);
			return 0;
		}
	}
	return -ESRCH;
}

static int fib_rule_add(struct in_rtrulemsg *r, struct device *dev, struct nlmsghdr *n)
{
	u32 src = r->rtrmsg_src.s_addr;
	u32 dst = r->rtrmsg_dst.s_addr;
	u32 srcmask = fib_netmask(r->rtrmsg_srclen);
	u32 dstmask = fib_netmask(r->rtrmsg_dstlen);

	struct fib_rule *cl, *new_cl, **clp;
	struct fib_class *class = NULL;

	if ((src&~srcmask) || (dst&~dstmask))
		return -EINVAL;
	if (dev && net_alias_main_dev(dev) != dev)
		return -ENODEV;

	if (!r->rtrmsg_class) {
		if (r->rtrmsg_action==RTP_GO || r->rtrmsg_action==RTP_NAT
		    || r->rtrmsg_action==RTP_MASQUERADE) {
			if ((class = fib_empty_class()) == NULL)
				return -ENOMEM;
			class->cl_auto = 1;
		} else if (r->rtrmsg_rtmsgs)
			return -EINVAL;
	} else if ((class = fib_alloc_class(r->rtrmsg_class)) == NULL)
		return -ENOMEM;

	new_cl = kmalloc(sizeof(*new_cl), GFP_KERNEL);
	if (!new_cl)
		return -ENOMEM;
	new_cl->cl_src = src;
	new_cl->cl_srcmask = srcmask;
	new_cl->cl_dst = dst;
	new_cl->cl_dstmask = dstmask;
	new_cl->cl_dev = dev;
	new_cl->cl_srcmap = r->rtrmsg_srcmap.s_addr;
	new_cl->cl_tos = r->rtrmsg_tos;
	new_cl->cl_action = r->rtrmsg_action;
	new_cl->cl_flags = r->rtrmsg_flags;
	new_cl->cl_preference = r->rtrmsg_preference;
	new_cl->cl_class = class;
	if (class)
		class->cl_users++;

	clp = &fib_rules;

	if (!new_cl->cl_preference) {
		cl = fib_rules;
		if (cl && (cl = cl->cl_next) != NULL) {
			clp = &fib_rules->cl_next;
			if (cl->cl_preference)
				new_cl->cl_preference = cl->cl_preference - 1;
		}
	}

	while ( (cl = *clp) != NULL ) {
		if (cl->cl_preference >= new_cl->cl_preference)
			break;
		clp = &cl->cl_next;
	}

	new_cl->cl_next = cl;
	cli();
	*clp = new_cl;
	sti();

	if (r->rtrmsg_rtmsgs) {
		n->nlmsg_type = RTMSG_NEWROUTE;
		r->rtrmsg_rtmsg->rtmsg_class = class->cl_id;
		return rtmsg_process(n, r->rtrmsg_rtmsg);
	}
	return 0;
}


#define FZ_MAX_DIVISOR 1024

static __inline__ u32 fib_hash(u32 key, u32 mask)
{
	u32 h;
	h = key^(key>>20);
	h = h^(h>>10);
	h = h^(h>>5);
	return h & mask;
}

static __inline__ struct fib_node ** fz_hash_p(u32 key, struct fib_zone *fz)
{
	return &fz->fz_hash[fib_hash(key, fz->fz_hashmask)];
}

static __inline__ struct fib_node * fz_hash(u32 key, struct fib_zone *fz)
{
	return fz->fz_hash[fib_hash(key, fz->fz_hashmask)];
}

/*
 * Free FIB node.
 */

static void fib_free_node(struct fib_node * f)
{
	struct fib_info * fi = f->fib_info;
	if (fi && !--fi->fib_refcnt) {
#if RT_CACHE_DEBUG >= 2
		printk("fib_free_node: fi %08x/%s is free\n", fi->fib_gateway, fi->fib_dev ? fi->fib_dev->name : "null");
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

static __inline__ int fib_flags_trans(unsigned flags)
{
	if (flags & RTF_BROADCAST)
		return IS_BROADCAST;
	if (flags & RTF_MULTICAST)
		return IS_MULTICAST;
	if (flags & RTF_LOCAL)
		return IS_MYADDR;
	return 0;
}

unsigned ip_fib_chk_addr(u32 addr)
{
	struct fib_zone * fz;
	struct fib_node * f;

	/* 
	 *	Accept both `all ones' and `all zeros' as BROADCAST. 
	 *	(Support old BSD in other words). This old BSD 
	 *	support will go very soon as it messes other things
	 *	up.
	 */

	if (addr == INADDR_ANY || addr == 0xFFFFFFFF)
		return RTF_LOCAL|RTF_BROADCAST;

	if ((addr & htonl(0x7F000000L)) == htonl(0x7F000000L))
		return RTF_LOCAL|RTF_INTERFACE;

	if (MULTICAST(addr))
		return RTF_MULTICAST;

	addr = ntohl(addr);
	for (fz = local_class.fib_zone_list; fz; fz = fz->fz_next) {
		u32 key = (addr&fz->fz_mask)>>fz->fz_logmask;
		for (f = fz_hash(key, fz); f; f = f->fib_next) {
			if (key != f->fib_key || (f->fib_flag & FIBFLG_DOWN))
				continue;
			if (!f->fib_info)
				return 0;
			return f->fib_info->fib_flags&RTF_ADDRCLASSMASK;
		}
	}

	return 0;
}

int __ip_chk_addr(unsigned long addr)
{
	return fib_flags_trans(ip_fib_chk_addr(addr));
}

/*
 *	Find the first device with a given source address.
 */
 
struct device *ip_dev_find(unsigned long addr, char *name)
{
	struct fib_zone * fz = local_class.fib_zones[0];
	u32 key;
	struct fib_node * f;

	key = (ntohl(addr)&fz->fz_mask)>>fz->fz_logmask;
	for (f = fz_hash(key, fz); f; f = f->fib_next) {
		if (key == f->fib_key &&
		    !(f->fib_flag & (FIBFLG_DOWN|FIBFLG_REJECT|FIBFLG_THROW)) &&
		    f->fib_info->fib_flags == (RTF_IFLOCAL&~RTF_UP)) {
			if (!name || strcmp(name, f->fib_info->fib_dev->name) == 0)
				return f->fib_info->fib_dev;
		}
	}

	return NULL;
}

/*
 *	Find tunnel with a given source and destination.
 */
 
struct device *ip_dev_find_tunnel(u32 daddr, u32 saddr)
{
	struct fib_zone * fz = local_class.fib_zones[0];
	u32 key;
	struct fib_node * f;

	key = (ntohl(daddr)&fz->fz_mask)>>fz->fz_logmask;
	for (f = fz_hash(key, fz); f; f = f->fib_next) {
		if (key == f->fib_key &&
		    !(f->fib_flag & (FIBFLG_DOWN|FIBFLG_REJECT|FIBFLG_THROW)) &&
		    f->fib_info->fib_flags == (RTF_IFLOCAL&~RTF_UP)) {
			struct device *dev = f->fib_info->fib_dev;
			if (dev->type == ARPHRD_TUNNEL &&
			    dev->pa_dstaddr == saddr)
				return dev;
		}
		if (!f->fib_info)
			return NULL;
	}

	return NULL;
}


int ip_fib_chk_default_gw(u32 addr, struct device *dev)
{
	struct fib_rule *cl;
	struct fib_node * f;

	for (cl = fib_rules; cl; cl = cl->cl_next) {
		if (cl->cl_srcmask || cl->cl_dstmask || cl->cl_tos ||
		    cl->cl_dev || cl->cl_action != RTP_GO || !cl->cl_class ||
		    !cl->cl_class->fib_zones[32])
			continue;
		for (f = cl->cl_class->fib_zones[32]->fz_hash[0]; f; f = f->fib_next) {
			struct fib_info *fi = f->fib_info;
			if (!(f->fib_flag & (FIBFLG_DOWN|FIBFLG_REJECT|FIBFLG_THROW)) &&
			    fi->fib_gateway == addr &&
			    fi->fib_dev == dev &&
			    fi->fib_flags&RTF_GATEWAY)
				return 0;
		}
	}
	return -1;
}


/*
 * Main lookup routine.
 */


int
fib_lookup(struct fib_result *res, u32 daddr, u32 src, u8 tos,
	   struct device *devin, struct device *devout)
{
	struct fib_node * f;
	struct fib_rule * cl;
	u32		dst;
	int		local = tos & 1;

	tos &= IPTOS_TOS_MASK;
	dst = ntohl(daddr);

	for (cl = fib_rules; cl; cl=cl->cl_next) {
		struct fib_zone * fz;

		if (((src^cl->cl_src) & cl->cl_srcmask) ||
		    ((daddr^cl->cl_dst) & cl->cl_dstmask) ||
		    (cl->cl_tos && cl->cl_tos != tos) ||
		    (cl->cl_dev && cl->cl_dev != devin))
			continue;

		switch (cl->cl_action) {
		case RTP_GO:
		case RTP_NAT:
		case RTP_MASQUERADE:
		default:
			break;
		case RTP_UNREACHABLE:
			return -ENETUNREACH;
		case RTP_DROP:
			return -EINVAL;
		case RTP_PROHIBIT:
			return -EACCES;
		}

		for (fz = cl->cl_class->fib_zone_list; fz; fz = fz->fz_next) {
			u32 key = (dst&fz->fz_mask)>>fz->fz_logmask;

			for (f = fz_hash(key, fz); f; f = f->fib_next) {
				if (key != f->fib_key ||
				    (f->fib_flag & FIBFLG_DOWN) ||
				    (f->fib_tos && f->fib_tos != tos))
					continue;
				if (f->fib_flag & FIBFLG_THROW)
					goto next_class;
				if (f->fib_flag & FIBFLG_REJECT)
					return -ENETUNREACH;
				if (devout && f->fib_info->fib_dev != devout)
					continue;
				if (!local || !(f->fib_info->fib_flags&RTF_GATEWAY)) {
					res->f = f;
					res->fr = cl;
					res->fm = fz->fz_logmask;
					return 0;
				}
			}
		}
next_class:
	}
	return -ENETUNREACH;
}

static int fib_autopublish(int op, struct fib_node *f, int logmask)
{	
	struct fib_zone *fz;
	struct fib_node *f1;
	struct arpreq r;
	u32 addr = htonl(f->fib_key<<logmask);

	if (f->fib_flag || LOOPBACK(addr) ||
	    (!RT_LOCALADDR(f->fib_info->fib_flags) &&
	     !(f->fib_info->fib_flags&RTF_NAT)))
		return 0;

	memset(&r, 0, sizeof(struct arpreq));
	r.arp_flags = ATF_PUBL|ATF_PERM|ATF_MAGIC;
	if (logmask)
		r.arp_flags |= ATF_NETMASK;
	((struct sockaddr_in*)&r.arp_pa)->sin_family = AF_INET;
	((struct sockaddr_in*)&r.arp_pa)->sin_addr.s_addr = addr;
	((struct sockaddr_in*)&r.arp_netmask)->sin_family = AF_INET;
	((struct sockaddr_in*)&r.arp_netmask)->sin_addr.s_addr = fib_mask(logmask);

	if (op)
		return arp_req_set(&r, NULL);
	
	fz = local_class.fib_zones[logmask];

	for (f1 = fz_hash(f->fib_key, fz); f1; f1=f1->fib_next)	{
		if (f->fib_key != f1->fib_key || f1->fib_flag ||
		    (!RT_LOCALADDR(f1->fib_info->fib_flags) &&
		     !(f1->fib_info->fib_flags&RTF_NAT)))
			continue;
		return 0;
	}

	return arp_req_delete(&r, NULL);
}

#define FIB_SCAN(f, fp) \
for ( ; ((f) = *(fp)) != NULL; (fp) = &(f)->fib_next)

#define FIB_SCAN_KEY(f, fp, key) \
for ( ; ((f) = *(fp)) != NULL && (f)->fib_key == (key); (fp) = &(f)->fib_next)

#define FIB_CONTINUE(f, fp) \
{ \
	fp = &f->fib_next; \
	continue; \
}

static int fib_delete(struct in_rtmsg * r, struct device *dev,
		      struct fib_class *class, struct nlmsghdr *n)
{
	struct fib_node **fp, *f;
	struct fib_zone *fz = class->fib_zones[32-r->rtmsg_prefixlen];
	int logmask = 32 - r->rtmsg_prefixlen;
	u32 dst = ntohl(r->rtmsg_prefix.s_addr);
	u32 gw = r->rtmsg_gateway.s_addr;
	short metric = r->rtmsg_metric;
	u8 tos = r->rtmsg_tos;
	u8 fibflg = 0;
	int found=0;
	unsigned flags;
	u32 key;

	flags = r->rtmsg_flags;
	if (flags & RTF_REJECT)
		fibflg |= FIBFLG_REJECT;
	else if (flags & RTF_THROW)
		fibflg |= FIBFLG_THROW;
	flags &= ~(RTF_UP|RTF_REJECT|RTF_THROW);

	if (fz != NULL)	{
		key = (dst&fz->fz_mask)>>logmask;
		fp = fz_hash_p(key, fz);

		FIB_SCAN(f, fp) {
			if (f->fib_key == key)
				break;
		}
		FIB_SCAN_KEY(f, fp, key) {
			if (f->fib_tos == tos)
				break;
		}

		while ((f = *fp) != NULL && f->fib_key == key && f->fib_tos == tos) {
			struct fib_info * fi = f->fib_info;

			/*
			 * If metric was not specified (<0), match all metrics.
			 */
			if (metric >= 0 && f->fib_metric != metric)
				FIB_CONTINUE(f, fp);

			if (flags & RTF_MAGIC) {
				/* "Magic" deletions require exact match */
				if (!fi || (fi->fib_flags^flags) ||
				    fi->fib_dev != dev ||
				    fi->fib_gateway != gw)
					FIB_CONTINUE(f, fp);
			} else {
				/*
				 * Device, gateway, reject and throw are
				 * also checked if specified.
				 */
				if ((dev && fi && fi->fib_dev != dev) ||
				    (gw  && fi && fi->fib_gateway != gw) ||
				    (fibflg && (f->fib_flag^fibflg)&~FIBFLG_DOWN))
					FIB_CONTINUE(f, fp);
			}
			cli();
			/* It's interesting, can this operation be not atomic? */
			*fp = f->fib_next;
			sti();
			if (class == &local_class)
				fib_autopublish(0, f, logmask);
			rtmsg_fib(RTMSG_DELROUTE, f, logmask, class, n);
			fib_free_node(f);
			found++;
		}
		fz->fz_nent -= found;
	}

	if (found) {
		fib_stamp++;
		rt_cache_flush(0);
		rtmsg_ack(n, 0);
		return 0;
	}
	rtmsg_ack(n, ESRCH);
	return -ESRCH;
}

static struct fib_info * fib_create_info(struct device * dev, struct in_rtmsg *r)
{
	struct fib_info * fi;
	unsigned flags = r->rtmsg_flags;
	u32 gw = r->rtmsg_gateway.s_addr;
	unsigned short mtu;
	unsigned short irtt;
	unsigned long  window;

	mtu = dev ? dev->mtu : 0;
	if (flags&RTF_MSS && r->rtmsg_mtu < mtu && r->rtmsg_mtu >= 68)
		mtu = r->rtmsg_mtu;
	window = (flags & RTF_WINDOW) ? r->rtmsg_window : 0;
	irtt = (flags & RTF_IRTT) ? r->rtmsg_rtt : TCP_TIMEOUT_INIT;

	flags &= RTF_FIB;

	for (fi=fib_info_list; fi; fi = fi->fib_next) {
		if (fi->fib_gateway != gw ||
		    fi->fib_dev != dev  ||
		    fi->fib_flags != flags ||
		    fi->fib_mtu != mtu ||
		    fi->fib_window != window ||
		    fi->fib_irtt != irtt)
			continue;
		fi->fib_refcnt++;
#if RT_CACHE_DEBUG >= 2
		printk("fib_create_info: fi %08x/%s/%04x is duplicate\n", fi->fib_gateway, fi->fib_dev ? fi->fib_dev->name : "null", fi->fib_flags);
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
	fi->fib_mtu = mtu;
	fi->fib_window = window;
	fi->fib_refcnt++;
	fi->fib_next = fib_info_list;
	fi->fib_prev = NULL;
	fi->fib_irtt = irtt;
	if (fib_info_list)
		fib_info_list->fib_prev = fi;
	fib_info_list = fi;
#if RT_CACHE_DEBUG >= 2
	printk("fib_create_info: fi %08x/%s/%04x is created\n", fi->fib_gateway, fi->fib_dev ? fi->fib_dev->name : "null", fi->fib_flags);
#endif
	return fi;
}

static __inline__ void fib_rebuild_zone(struct fib_zone *fz,
					struct fib_node **old_ht,
					int old_divisor)
{
	int i;
	struct fib_node **ht = fz->fz_hash;
	u32 hashmask = fz->fz_hashmask;
	struct fib_node *f, **fp, *next;
	unsigned hash;

	for (i=0; i<old_divisor; i++) {
		for (f=old_ht[i]; f; f=next) {
			next = f->fib_next;
			f->fib_next = NULL;
			hash = fib_hash(f->fib_key, hashmask);
			for (fp = &ht[hash]; *fp; fp = &(*fp)->fib_next)
				/* NONE */;
			*fp = f;
		}
	}
}

static void fib_rehash_zone(struct fib_zone *fz)
{
	struct fib_node **ht, **old_ht;
	int old_divisor, new_divisor;
	u32 new_hashmask;
		
	old_divisor = fz->fz_divisor;

	switch (old_divisor) {
	case 16:
		new_divisor = 256;
		new_hashmask = 0xFF;
		break;
	case 256:
		new_divisor = 1024;
		new_hashmask = 0x3FF;
		break;
	default:
		printk(KERN_CRIT "route.c: bad divisor %d!\n", old_divisor);
		return;
	}
#if RT_CACHE_DEBUG >= 2
	printk("fib_rehash_zone: hash for zone %d grows from %d\n", fz->fz_logmask, old_divisor);
#endif

	ht = kmalloc(new_divisor*sizeof(struct rtable*), GFP_KERNEL);

	if (ht)	{
		memset(ht, 0, new_divisor*sizeof(struct fib_node*));
		start_bh_atomic();
		old_ht = fz->fz_hash;
		fz->fz_hash = ht;
		fz->fz_hashmask = new_hashmask;
		fz->fz_divisor = new_divisor;
		fib_rebuild_zone(fz, old_ht, old_divisor);
		fib_stamp++;
		end_bh_atomic();
		kfree(old_ht);
	}
}

static struct fib_zone *
fib_new_zone(struct fib_class *class, int logmask)
{
	int i;
	struct fib_zone *fz = kmalloc(sizeof(struct fib_zone), GFP_KERNEL);
	if (!fz)
		return NULL;

	memset(fz, 0, sizeof(struct fib_zone));
	if (logmask < 32) {
		fz->fz_divisor = 16;
		fz->fz_hashmask = 0xF;
	} else {
		fz->fz_divisor = 1;
		fz->fz_hashmask = 0;
	}
	fz->fz_hash = kmalloc(fz->fz_divisor*sizeof(struct fib_node*), GFP_KERNEL);
	if (!fz->fz_hash) {
		kfree(fz);
		return NULL;
	}
	memset(fz->fz_hash, 0, fz->fz_divisor*sizeof(struct fib_node*));
	fz->fz_logmask = logmask;
	fz->fz_mask = ntohl(fib_mask(logmask));
	for (i=logmask-1; i>=0; i--)
		if (class->fib_zones[i])
			break;
	start_bh_atomic();
	if (i<0) {
		fz->fz_next = class->fib_zone_list;
		class->fib_zone_list = fz;
	} else {
		fz->fz_next = class->fib_zones[i]->fz_next;
		class->fib_zones[i]->fz_next = fz;
	}
	class->fib_zones[logmask] = fz;
	fib_stamp++;
	end_bh_atomic();
	return fz;
}

static int fib_create(struct in_rtmsg *r, struct device *dev,
		      struct fib_class *class, struct nlmsghdr *n)
{
	struct fib_node *f, *f1, **fp;
	struct fib_node **dup_fp = NULL;
	struct fib_zone * fz;
	struct fib_info * fi;

	long logmask = 32L - r->rtmsg_prefixlen;	/* gcc bug work-around: must be "L" and "long" */
	u32 dst = ntohl(r->rtmsg_prefix.s_addr);
	u32 gw  = r->rtmsg_gateway.s_addr;
	short metric = r->rtmsg_metric;
	unsigned flags = r->rtmsg_flags;
	u8 tos = r->rtmsg_tos;
	u8 fibflg = 0;
	u32 key;

	/*
	 *	Allocate an entry and fill it in.
	 */
	 
	f = (struct fib_node *) kmalloc(sizeof(struct fib_node), GFP_KERNEL);
	if (f == NULL) {
		rtmsg_ack(n, ENOMEM);
		return -ENOMEM;
	}

	memset(f, 0, sizeof(struct fib_node));

	if (!(flags & RTF_UP))
		fibflg = FIBFLG_DOWN;
	if (flags & RTF_REJECT)
		fibflg |= FIBFLG_REJECT;
	else if (flags & RTF_THROW)
		fibflg |= FIBFLG_THROW;

	flags &= ~(RTF_UP|RTF_REJECT|RTF_THROW);
	r->rtmsg_flags = flags;

	fi = NULL;
	if (!(fibflg & (FIBFLG_REJECT|FIBFLG_THROW))) {
		if  ((fi = fib_create_info(dev, r)) == NULL) {
			kfree_s(f, sizeof(struct fib_node));
			rtmsg_ack(n, ENOMEM);
			return -ENOMEM;
		}
		f->fib_info = fi;
		flags = fi->fib_flags;
	}

	f->fib_key = key = dst>>logmask;
	f->fib_metric = metric;
	f->fib_tos    = tos;
	f->fib_flag = fibflg;
	fz = class->fib_zones[logmask];

	if (!fz && !(fz = fib_new_zone(class, logmask))) {
		fib_free_node(f);
		rtmsg_ack(n, ENOMEM);
		return -ENOMEM;
	}

	if (fz->fz_nent > (fz->fz_divisor<<2) &&
	    fz->fz_divisor < FZ_MAX_DIVISOR &&
	    (!logmask || (1<<(32-logmask)) > fz->fz_divisor))
		fib_rehash_zone(fz);

	fp = fz_hash_p(key, fz);

	/*
	 * Scan list to find the first route with the same destination
	 */
	FIB_SCAN(f1, fp) {
		if (f1->fib_key == key)
			break;
	}

	/*
	 * Find route with the same destination and tos.
	 */
	FIB_SCAN_KEY(f1, fp, dst) {
		if (f1->fib_tos <= tos)
			break;
	}

	/*
	 * Find route with the same destination/tos and less (or equal) metric.
	 * "Magic" additions go to the end of list.
	 */
	for ( ; (f1 = *fp) != NULL && f1->fib_key == key && f1->fib_tos == tos;
	     fp = &f1->fib_next) {
		if (f1->fib_metric >= metric && metric != MAGIC_METRIC)
			break;

		/*
		 *	Record route with the same destination/tos/gateway/dev,
		 *	but less metric.
		 */
		if (!dup_fp) {
			struct fib_info *fi1 = f1->fib_info;
			
			if ((fibflg^f1->fib_flag) & ~FIBFLG_DOWN)
				continue;
			if (fi == fi1 ||
			    (fi && fi1 &&
			     fi->fib_dev == fi1->fib_dev &&
			     fi->fib_gateway == fi1->fib_gateway &&
			     !(flags&RTF_MAGIC)))
				dup_fp = fp;
		}
	}

	/*
	 * Is it already present?
	 */

	if (f1 && f1->fib_key == key && f1->fib_tos == tos &&
	    f1->fib_metric == metric && f1->fib_info == fi) {
		fib_free_node(f);

		if (fibflg == f1->fib_flag) {
			rtmsg_ack(n, EEXIST);
			return -EEXIST;
		} else {
			fib_stamp++;
			f1->fib_flag = fibflg;
			rt_cache_flush(0);
			rtmsg_ack(n, 0);
			return 0;
		}
	}

	/*
	 * Do not add "magic" route, if better one is already present.
	 */
	if ((flags & RTF_MAGIC) && dup_fp) {
		fib_free_node(f);
		rtmsg_ack(n, EEXIST);
		return -EEXIST;
	}

	/*
	 * Insert new entry to the list.
	 */

	cli();
	f->fib_next = f1;
	*fp = f;
	sti();
	fz->fz_nent++;
	if (class == &local_class && !dup_fp)
		fib_autopublish(1, f, logmask);
	rtmsg_fib(RTMSG_NEWROUTE, f, logmask, class, n);

	if (flags & RTF_MAGIC) {
		fib_stamp++;
		rt_cache_flush(0);
		rtmsg_ack(n, 0);
		return 0;
	}

	/*
	 *	Clean routes with the same destination,tos,gateway and device,
	 *	but different metric.
	 */
	fp = dup_fp ? : &f->fib_next;

	while ((f1 = *fp) != NULL && f1->fib_key == key && f1->fib_tos == tos) {
		if (f1 == f || ((f1->fib_flag^fibflg)&~FIBFLG_DOWN))
			FIB_CONTINUE(f1, fp);

		if (f1->fib_info != fi &&
		    (!fi || !f1->fib_info ||
		     f1->fib_info->fib_gateway != gw ||
		     f1->fib_info->fib_dev != dev))
			FIB_CONTINUE(f1, fp);

		cli();
		*fp = f1->fib_next;
		sti();
		fz->fz_nent--;
		rtmsg_fib(RTMSG_DELROUTE, f1, logmask, class, n);
		fib_free_node(f1);
	}
	fib_stamp++;
	rt_cache_flush(0);
	rtmsg_ack(n, 0);
	return 0;
}

static int fib_flush_list(struct fib_node ** fp, struct device *dev,
			  int logmask, struct fib_class *class)
{
	int found = 0;
	struct fib_node *f;

	while ((f = *fp) != NULL) {
		if (!f->fib_info || f->fib_info->fib_dev != dev)
			FIB_CONTINUE(f, fp);
		cli();
		*fp = f->fib_next;
		sti();
		if (class == &local_class)
			fib_autopublish(0, f, logmask);
#ifdef CONFIG_RTNETLINK
		if (rt_nl_flags&RTCTL_FLUSH)
		    rtmsg_fib(RTMSG_DELROUTE, f, logmask, class, 0);
#endif
		fib_free_node(f);
		found++;
	}
	return found;
}

static void fib_flush(struct device *dev)
{
	struct fib_class *class;
	struct fib_rule *cl, **clp;
	struct fib_zone *fz;
	int found = 0;
	int i, tmp, cl_id;


	for (cl_id = RT_CLASS_MAX; cl_id>=0; cl_id--) {
		if ((class = fib_classes[cl_id])==NULL)
			continue;
		for (fz = class->fib_zone_list; fz; fz = fz->fz_next) {
			tmp = 0;
			for (i=fz->fz_divisor-1; i>=0; i--)
				tmp += fib_flush_list(&fz->fz_hash[i], dev,
						      fz->fz_logmask, class);
			fz->fz_nent -= tmp;
			found += tmp;
		}
	}
	
	clp = &fib_rules;
	while ( (cl=*clp) != NULL) {
		if (cl->cl_dev != dev) {
			clp = &cl->cl_next;
			continue;
		}
		found++;
		cli();
		*clp = cl->cl_next;
		sti();
		kfree(cl);
	}
		
	if (found) {
		fib_stamp++;
		rt_cache_flush(1);
	}
}

#ifdef CONFIG_PROC_FS

static unsigned __inline__ fib_flag_trans(u8 fibflg)
{
	unsigned ret = RTF_UP;
	if (!fibflg)
		return ret;
	if (fibflg & FIBFLG_DOWN)
		ret &= ~RTF_UP;
	if (fibflg & FIBFLG_REJECT)
		ret |= RTF_REJECT;
	if (fibflg & FIBFLG_THROW)
		ret |= RTF_THROW;
	return ret;
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
 
static int fib_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct fib_class *class;
	struct fib_zone *fz;
	struct fib_node *f;
	int len=0;
	off_t pos=0;
	char temp[129];
	int i;
	int cl_id;
	
	pos = 128;

	if (offset<128)
	{
		sprintf(buffer,"%-127s\n","Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\tTOS\tClass");
		len = 128;
  	}
  	
	fib_lock();

	for (cl_id=RT_CLASS_MAX-1; cl_id >= 0; cl_id--) {
		class = fib_classes[cl_id];
		if (!class)
			continue;
		for (fz=class->fib_zone_list; fz; fz = fz->fz_next)
		{
		int maxslot;
		struct fib_node ** fp;

		if (fz->fz_nent == 0)
			continue;

		if (pos + 128*fz->fz_nent <= offset) {
			pos += 128*fz->fz_nent;
			len = 0;
			continue;
		}

		maxslot = fz->fz_divisor;
		fp	= fz->fz_hash;
			
		for (i=0; i < maxslot; i++, fp++) {
			
			for (f = *fp; f; f = f->fib_next) 
			{
				struct fib_info * fi;
				unsigned	flags;

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
				flags = fib_flag_trans(f->fib_flag);

				if (fi)
					flags |= fi->fib_flags;
				sprintf(temp, "%s\t%08lX\t%08X\t%04X\t%d\t%u\t%d\t%08lX\t%d\t%lu\t%u\t%02x\t%02x",
					fi && fi->fib_dev ? fi->fib_dev->name : "*", htonl(f->fib_key<<fz->fz_logmask), fi ? fi->fib_gateway : 0,
					flags, 0, 0, f->fib_metric,
					htonl(fz->fz_mask), fi ? (int)fi->fib_mtu : 0, fi ? fi->fib_window : 0, fi ? (int)fi->fib_irtt : 0, f->fib_tos, class->cl_id);
				sprintf(buffer+len,"%-127s\n",temp);

				len += 128;
				if (pos >= offset+length)
					goto done;
			}
		}
        }
	}

done:
	fib_unlock();
  	
  	*start = buffer+len-(pos-offset);
  	len = pos - offset;
  	if (len>length)
  		len = length;
  	return len;
}

static int fib_local_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
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
		sprintf(buffer,"%-127s\n","Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\tTOS\tClass");
		len = 128;
  	}
  	
	fib_lock();

	for (fz=local_class.fib_zone_list; fz; fz = fz->fz_next)
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

		maxslot = fz->fz_divisor;
		fp	= fz->fz_hash;
			
		for (i=0; i < maxslot; i++, fp++)
		{
			
			for (f = *fp; f; f = f->fib_next) 
			{
				unsigned	flags;
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
				flags = fib_flag_trans(f->fib_flag);

				if (fi)
					flags |= fi->fib_flags;
				sprintf(temp, "%s\t%08lX\t%08X\t%X\t%d\t%u\t%d\t%08lX\t%d\t%lu\t%u\t%02x\t%02x",
					fi && fi->fib_dev ? fi->fib_dev->name : "*",
					htonl(f->fib_key<<fz->fz_logmask),
					fi ? fi->fib_gateway : 0,
					flags, 0, 0, f->fib_metric,
					htonl(fz->fz_mask), fi ? (int)fi->fib_mtu : 0, fi ? fi->fib_window : 0, fi ? (int)fi->fib_irtt : 0, f->fib_tos, RT_CLASS_LOCAL);
				sprintf(buffer+len,"%-127s\n",temp);

				len += 128;
				if (pos >= offset+length)
					goto done;
			}
		}
        }

done:
	fib_unlock();
  	
  	*start = buffer+len-(pos-offset);
  	len = pos - offset;
  	if (len>length)
  		len = length;
  	return len;
}

static int fib_rules_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len=0;
	off_t pos=0;
	char temp[129];
	struct fib_rule *cl;

	pos = 128;

	if (offset<128) {
		sprintf(buffer,"%-127s\n","Pref\tSource\t\tSrcMask\t\tDst\t\tDstMask\t\tIface\tTOS\tClass\tFlags\tSrcMap\n");
		len = 128;
  	}
	
  	
	fib_lock();

	for (cl = fib_rules; cl; cl = cl->cl_next) {
		/*
		 *	Spin through entries until we are ready
		 */
		pos += 128;

		if (pos <= offset) {
			len = 0;
			continue;
		}
					
		sprintf(temp, "%d\t%08X\t%08X\t%08X\t%08X\t%s\t%02X\t%02x\t%02X\t%02X\t%08X",
			cl->cl_preference,
			cl->cl_src, cl->cl_srcmask,
			cl->cl_dst, cl->cl_dstmask,
			cl->cl_dev ? cl->cl_dev->name : "*",
			cl->cl_tos, cl->cl_class ? cl->cl_class->cl_id : 0,
			cl->cl_flags, cl->cl_action, cl->cl_srcmap
			);
		sprintf(buffer+len,"%-127s\n",temp);
		len += 128;
		if (pos >= offset+length)
			goto done;
	}

done:
	fib_unlock();
  	
  	*start = buffer+len-(pos-offset);
  	len = pos-offset;
  	if (len>length)
  		len = length;
  	return len;
}

static int fib_class_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len=0;
	off_t pos=0;
	char temp[129];
	int i;
	struct fib_class *cl;

	pos = 128;

	if (offset<128)
	{
		sprintf(buffer,"%-127s\n","Class\tSize\n");
		len = 128;
  	}
	
  	
	fib_lock();

	for (i = RT_CLASS_MAX; i>=0; i--) 
	{
		int sz = 0;
		struct fib_zone *fz;

		if ((cl=fib_classes[i])==NULL)
			continue;

		for (fz=cl->fib_zone_list; fz; fz=fz->fz_next)
			sz += fz->fz_nent;

		/*
		 *	Spin through entries until we are ready
		 */
		pos += 128;

		if (pos <= offset)
		{
			len = 0;
			continue;
		}
					
		sprintf(temp, "%d\t%d\n", cl->cl_id, sz);
		sprintf(buffer+len,"%-127s\n",temp);
		len += 128;
		if (pos >= offset+length)
			goto done;
	}

done:
	fib_unlock();
  	
  	*start = buffer+len-(pos-offset);
  	len = pos-offset;
  	if (len>length)
  		len = length;
  	return len;
}

#endif

static int rtmsg_process(struct nlmsghdr *n, struct in_rtmsg *r)
{
	unsigned long cmd=n->nlmsg_type;
	struct device * dev = NULL;
	struct fib_class *class;

	if ((cmd != RTMSG_NEWROUTE && cmd != RTMSG_DELROUTE) ||
	    (r->rtmsg_flags & (RTF_MAGIC|RTF_XRESOLVE|RTF_REINSTATE)) ||
	    r->rtmsg_prefixlen > 32 ||
	    (r->rtmsg_tos & ~IPTOS_TOS_MASK)) {
		rtmsg_ack(n, EINVAL);
		return -EINVAL;
	}

	/* Reject/throw directives have no interface/gateway specification */

	if (r->rtmsg_flags & (RTF_REJECT|RTF_THROW)) {
		r->rtmsg_ifindex = 0;
		r->rtmsg_gateway.s_addr = 0;
		r->rtmsg_flags &= ~RTF_GATEWAY;
	}

	/* Silly metric hack, it is preserved for "compatibility",
	 * though I do not know any program using it.
	 */

	r->rtmsg_metric--;
	if (cmd == RTMSG_NEWROUTE && r->rtmsg_metric < 0)
		r->rtmsg_metric = 0;

	if (cmd == RTMSG_DELROUTE)
		r->rtmsg_flags &= RTF_FIB;

	if (r->rtmsg_ifindex) {
		dev = dev_get_by_index(r->rtmsg_ifindex);
		if (!dev) {
			rtmsg_ack(n, ENODEV);
			return -ENODEV;
		}
	}

	if (r->rtmsg_gateway.s_addr && !(r->rtmsg_flags&RTF_NAT)) {
		struct fib_info *fi;

		fi = fib_lookup_info(r->rtmsg_gateway.s_addr, 0, 1,
				     &loopback_dev, dev);
		if (fi) {
			if (fi->fib_flags&(RTF_BROADCAST|RTF_MULTICAST) &&
			    cmd != RTMSG_DELROUTE)
				return -EINVAL;
			dev = fi->fib_dev;
			if (fi->fib_flags&RTF_LOCAL) {
				r->rtmsg_flags &= ~RTF_GATEWAY;
				r->rtmsg_gateway.s_addr = 0;
			}
		} else if (cmd != RTMSG_DELROUTE)
			return -ENETUNREACH;

		/* If gateway is not found in routing table,
		 * we could assume that user knows that he does.
		 * It is link layer problem to decide reachable
		 * this gateway or not. Good example is tunnel interface.
		 * Another example is ethernet, ARP could (in theory)
		 * resolve addresses, even if we had no routes.
		 */
	}

	if (dev && (dev->flags&IFF_LOOPBACK)) {
		if (r->rtmsg_flags&RTF_GATEWAY)
			return -EINVAL;
		/*
		 * Loopback routes: we declare them local addresses.
		 * It is the only reasonable solution to avoid
		 * loopback routing loops.
		 */
		r->rtmsg_flags |= RTF_LOCAL|RTF_INTERFACE;
	}

	if (r->rtmsg_flags&RTF_GATEWAY) {
		if (!dev && cmd != RTMSG_DELROUTE) {
			rtmsg_ack(n, ENETUNREACH);
			return -ENETUNREACH;
		}
	} else {
		if (!dev && !(r->rtmsg_flags & (RTF_NAT|RTF_REJECT|RTF_THROW)) &&
		    cmd != RTMSG_DELROUTE) {
			rtmsg_ack(n, ENODEV);
			return -ENODEV;
		}
	}

	if (dev && dev->family != AF_INET)
	{
		rtmsg_ack(n, ENODEV);
		return -ENODEV;
	}

	if (r->rtmsg_class == 0) {
		if (r->rtmsg_flags&(RTF_LOCAL|RTF_NAT))
			r->rtmsg_class = RT_CLASS_LOCAL;
		else if ((r->rtmsg_flags&RTF_GATEWAY) &&
			 (ipv4_config.fib_model==2 ||
			  (ipv4_config.fib_model==1 && !r->rtmsg_prefixlen)))
			r->rtmsg_class = RT_CLASS_DEFAULT;
		else
			r->rtmsg_class = RT_CLASS_MAIN;
	}

	if ((class = fib_classes[r->rtmsg_class]) == NULL)
	{
		rtmsg_ack(n, EINVAL);
		return -EINVAL;
	}

	return (cmd == RTMSG_NEWROUTE ? fib_create : fib_delete)(r, dev, class, n);
}


static int rtrulemsg_process(struct nlmsghdr *n, struct in_rtrulemsg *r)
{
	unsigned long cmd=n->nlmsg_type;
	struct device * dev = NULL;

	if ((cmd != RTMSG_NEWRULE && cmd != RTMSG_DELRULE) ||
	    r->rtrmsg_srclen > 32 || r->rtrmsg_dstlen > 32 ||
	    (r->rtrmsg_tos & ~IPTOS_TOS_MASK))
		return -EINVAL;

	if (r->rtrmsg_ifindex) {
		dev = dev_get_by_index(r->rtrmsg_ifindex);
		if (!dev)
			return -ENODEV;
		if (dev->family != AF_INET)
			return -ENODEV;
	}

	if (cmd == RTMSG_DELRULE)
		return fib_rule_delete(r, dev, n);

	return fib_rule_add(r, dev, n);
}


static int ifmsg_process(struct nlmsghdr *n, struct in_ifmsg *r)
{
	unsigned long cmd=n->nlmsg_type;

	if (cmd != RTMSG_NEWDEVICE && cmd != RTMSG_DELDEVICE) {
		rtmsg_ack(n, EINVAL);
		return -EINVAL;
	}
	rtmsg_ack(n, EINVAL);
	return -EINVAL;
}

static int rtcmsg_process(struct nlmsghdr *n, struct in_rtctlmsg *r)
{
#ifdef CONFIG_RTNETLINK
    if (r->rtcmsg_flags&RTCTL_DELAY)
	rtmsg_ctl.nlmsg_delay = r->rtcmsg_delay;
    if (r->rtcmsg_flags&RTCTL_OWNER)
	rt_nl_owner = n->nlmsg_pid;
    rt_nl_flags = r->rtcmsg_flags;
    return 0;
#else
    return -EINVAL;
#endif
}

static int get_rt_from_user(struct in_rtmsg *rtm, void *arg)
{
	struct rtentry r;

	if (copy_from_user(&r, arg, sizeof(struct rtentry)))
		return -EFAULT;
	if (r.rt_dev) {
		struct device *dev;
		char   devname[16];

		if (copy_from_user(devname, r.rt_dev, 15))
			return -EFAULT;
		devname[15] = 0;
		dev = dev_get(devname);
		if (!dev)
			return -ENODEV;
		rtm->rtmsg_ifindex = dev->ifindex;
	}

	rtm->rtmsg_flags = r.rt_flags;

	if (r.rt_dst.sa_family != AF_INET)
		return -EAFNOSUPPORT;
	rtm->rtmsg_prefix = ((struct sockaddr_in*)&r.rt_dst)->sin_addr;

	if (rtm->rtmsg_flags&RTF_HOST) {
		rtm->rtmsg_flags &= ~RTF_HOST;
		rtm->rtmsg_prefixlen = 32;
	} else {
		u32 mask = ((struct sockaddr_in*)&r.rt_genmask)->sin_addr.s_addr;
		if (r.rt_genmask.sa_family != AF_INET) {
			printk(KERN_WARNING "%s forgot to specify route netmask.\n", current->comm);
			if (r.rt_genmask.sa_family)
				return -EAFNOSUPPORT;
		}
		if (bad_mask(mask, rtm->rtmsg_prefix.s_addr))
			return -EINVAL;
		rtm->rtmsg_prefixlen = 32 - fib_logmask(mask);
	}
	if ((rtm->rtmsg_flags & RTF_GATEWAY) &&
	    r.rt_gateway.sa_family != AF_INET)
		return -EAFNOSUPPORT;
	rtm->rtmsg_gateway = ((struct sockaddr_in*)&r.rt_gateway)->sin_addr;
	rtm->rtmsg_rtt = r.rt_irtt;
	rtm->rtmsg_window = r.rt_window;
	rtm->rtmsg_mtu = r.rt_mtu;
	rtm->rtmsg_class = r.rt_class;
	rtm->rtmsg_metric = r.rt_metric;
        rtm->rtmsg_tos = r.rt_tos;
	return 0;
}


/*
 *	Handle IP routing ioctl calls. These are used to manipulate the routing tables
 */
 
int ip_rt_ioctl(unsigned int cmd, void *arg)
{
	int err;
	union
	{
		struct in_rtmsg rtmsg;
		struct in_ifmsg ifmsg;
		struct in_rtrulemsg rtrmsg;
	        struct in_rtctlmsg rtcmsg;
	} m;
	struct nlmsghdr dummy_nlh;

	memset(&m, 0, sizeof(m));
	dummy_nlh.nlmsg_seq = 0;
	dummy_nlh.nlmsg_pid = current->pid;

	switch (cmd)
	{
		case SIOCADDRT:		/* Add a route */
		case SIOCDELRT:		/* Delete a route */
			if (!suser())
				return -EPERM;
			err = get_rt_from_user(&m.rtmsg, arg);
			if (err)
				return err;
			fib_lock();
			dummy_nlh.nlmsg_type = cmd == SIOCDELRT ? RTMSG_DELROUTE
					    : RTMSG_NEWROUTE;
			err = rtmsg_process(&dummy_nlh, &m.rtmsg);
			fib_unlock();
			return err;
		case SIOCRTMSG:
			if (!suser())
				return -EPERM;
			if (copy_from_user(&dummy_nlh, arg, sizeof(dummy_nlh)))
				return -EFAULT;
			switch (dummy_nlh.nlmsg_type)
			{
			case RTMSG_NEWROUTE:
			case RTMSG_DELROUTE:
				if (dummy_nlh.nlmsg_len < sizeof(m.rtmsg) + sizeof(dummy_nlh))
					return -EINVAL;
				if (copy_from_user(&m.rtmsg, arg+sizeof(dummy_nlh), sizeof(m.rtmsg)))
					return -EFAULT;
				fib_lock();
				err = rtmsg_process(&dummy_nlh, &m.rtmsg);
				fib_unlock();
				return err;
			case RTMSG_NEWRULE:
			case RTMSG_DELRULE:
				if (dummy_nlh.nlmsg_len < sizeof(m.rtrmsg) + sizeof(dummy_nlh))
					return -EINVAL;
				if (copy_from_user(&m.rtrmsg, arg+sizeof(dummy_nlh), sizeof(m.rtrmsg)))
					return -EFAULT;
				fib_lock();
				err = rtrulemsg_process(&dummy_nlh, &m.rtrmsg);
				fib_unlock();
				return err;
			case RTMSG_NEWDEVICE:
			case RTMSG_DELDEVICE:
				if (dummy_nlh.nlmsg_len < sizeof(m.ifmsg) + sizeof(dummy_nlh))
					return -EINVAL;
				if (copy_from_user(&m.ifmsg, arg+sizeof(dummy_nlh), sizeof(m.ifmsg)))
					return -EFAULT;
				fib_lock();
				err = ifmsg_process(&dummy_nlh, &m.ifmsg);
				fib_unlock();
				return err;
			case RTMSG_CONTROL:
				if (dummy_nlh.nlmsg_len < sizeof(m.rtcmsg) + sizeof(dummy_nlh))
					return -EINVAL;
				if (copy_from_user(&m.rtcmsg, arg+sizeof(dummy_nlh), sizeof(m.rtcmsg)))
					return -EFAULT;
				fib_lock();
				err = rtcmsg_process(&dummy_nlh, &m.rtcmsg);
				fib_unlock();
				return err;
			default:
				return -EINVAL;
			}
	}

	return -EINVAL;
}

#ifdef CONFIG_RTNETLINK

/*
 *	Netlink hooks for IP
 */


static void
rtmsg_fib(unsigned long type, struct fib_node *f, int logmask,
	  struct fib_class *class, struct nlmsghdr *n)
{
	struct in_rtmsg *r;
	struct fib_info *fi;

	if (n && !(rt_nl_flags&RTCTL_ECHO) && rt_nl_owner == n->nlmsg_pid)
	        return;

	start_bh_atomic();
	r = nlmsg_send(&rtmsg_ctl, type, sizeof(*r), n ? n->nlmsg_seq : 0,
		       n ? n->nlmsg_pid : 0);
	if (r) {
		r->rtmsg_prefix.s_addr = htonl(f->fib_key<<logmask);
		r->rtmsg_prefixlen = 32 - logmask;
		r->rtmsg_metric= f->fib_metric;
		r->rtmsg_tos = f->fib_tos;
		r->rtmsg_class=class->cl_id;
		r->rtmsg_flags = fib_flag_trans(f->fib_flag);

		if ((fi = f->fib_info) != NULL)	{
			r->rtmsg_gateway.s_addr = fi->fib_gateway;
			r->rtmsg_flags |= fi->fib_flags;
			r->rtmsg_mtu = fi->fib_mtu;
			r->rtmsg_window = fi->fib_window;
			r->rtmsg_rtt = fi->fib_irtt;
			r->rtmsg_ifindex = fi->fib_dev ? fi->fib_dev->ifindex : 0;
		}
	}
	end_bh_atomic();
}

static void
__rtmsg_ack(struct nlmsghdr *n, int err)
{
	nlmsg_ack(&rtmsg_ctl, n->nlmsg_seq, n->nlmsg_pid, err);
}


static void
rtmsg_dev(unsigned long type, struct device *dev, struct nlmsghdr *n)
{
	struct in_ifmsg *r;

	start_bh_atomic();
	r = nlmsg_send(&rtmsg_ctl, type, sizeof(*r), n ? n->nlmsg_seq : 0,
		       n ? n->nlmsg_pid : 0);
	if (r)
	{
		memset(r, 0, sizeof(*r));
		r->ifmsg_lladdr.sa_family = dev->type;
		memcpy(&r->ifmsg_lladdr.sa_data, dev->dev_addr, dev->addr_len);
		r->ifmsg_prefix.s_addr = dev->pa_addr;
		if (dev->flags & IFF_POINTOPOINT || dev->type == ARPHRD_TUNNEL)
			r->ifmsg_brd.s_addr = dev->pa_dstaddr;
		else
			r->ifmsg_brd.s_addr = dev->pa_brdaddr;
		r->ifmsg_flags = dev->flags;
		r->ifmsg_mtu = dev->mtu;
		r->ifmsg_metric = dev->metric;
		r->ifmsg_prefixlen = 32 - fib_logmask(dev->pa_mask);
		r->ifmsg_index = dev->ifindex;
		strcpy(r->ifmsg_name, dev->name);
	}
	end_bh_atomic();
}

static int fib_netlink_call(int minor, struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	int    totlen = 0;
	int    err = 0;

	fib_lock();
	while (skb->len >= sizeof(*nlh)) {
		int rlen;
		nlh = (struct nlmsghdr *)skb->data;
		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (skb->len < rlen)
			break;
		totlen += rlen;
		err = 0;
		skb_pull(skb, rlen);
		switch (nlh->nlmsg_type) {
		case RTMSG_NEWROUTE:
		case RTMSG_DELROUTE:
			if (nlh->nlmsg_len < sizeof(*nlh)+sizeof(struct in_rtmsg)) {
				rtmsg_ack(nlh, EINVAL);
				err = -EINVAL;
				break;
			}
			err = rtmsg_process(nlh, (struct in_rtmsg*)nlh->nlmsg_data);
			break;
		case RTMSG_NEWRULE:
		case RTMSG_DELRULE:
			if (nlh->nlmsg_len < sizeof(*nlh)+sizeof(struct in_rtrulemsg)) {
				rtmsg_ack(nlh, EINVAL);
				err = -EINVAL;
				break;
			}
			err = rtrulemsg_process(nlh, (struct in_rtrulemsg*)nlh->nlmsg_data);
			break;
		case RTMSG_NEWDEVICE:
		case RTMSG_DELDEVICE:
			if (nlh->nlmsg_len < sizeof(*nlh)+sizeof(struct in_ifmsg)) {
				rtmsg_ack(nlh, EINVAL);
				err = -EINVAL;
				break;
			}
			err = ifmsg_process(nlh, (struct in_ifmsg*)nlh->nlmsg_data);
			break;
		case RTMSG_CONTROL:
		        if (nlh->nlmsg_len < sizeof(*nlh)+sizeof(struct in_rtctlmsg)) {
				rtmsg_ack(nlh, EINVAL);
				err = -EINVAL;
				break;
			}
			err = rtcmsg_process(nlh, (struct in_rtctlmsg*)nlh->nlmsg_data);
			break;
		default:
			break;
		}
	}
	kfree_skb(skb, FREE_READ);
	fib_unlock();
	if (!err || rt_nl_flags&RTCTL_ACK)
	    return totlen;
	return err;
}

#endif


static int fib_magic(int op, unsigned flags, u32 dst, u32 mask, struct device *dev)
{
	struct nlmsghdr n;
	struct in_rtmsg r;
	memset(&r, 0, sizeof(r));
	n.nlmsg_seq=0;
	n.nlmsg_pid=0;
	r.rtmsg_metric = MAGIC_METRIC;
	r.rtmsg_prefix.s_addr = dst;
	if (dev->flags&IFF_LOOPBACK)
		flags |= RTF_LOCAL;
	r.rtmsg_flags = flags;
	r.rtmsg_prefixlen = 32 - fib_logmask(mask);

	return (op == RTMSG_NEWROUTE ? fib_create : fib_delete)
		(&r, dev, (flags&RTF_LOCAL) ? &local_class : &main_class, &n);
}

static void ip_rt_del_broadcasts(struct device *dev)
{
	u32 net = dev->pa_addr&dev->pa_mask;

	fib_magic(RTMSG_DELROUTE, RTF_IFBRD, dev->pa_brdaddr, ~0, dev);
	fib_magic(RTMSG_DELROUTE, RTF_IFBRD, net, ~0, dev);
	fib_magic(RTMSG_DELROUTE, RTF_IFBRD, net|~dev->pa_mask, ~0, dev);
}

static void ip_rt_add_broadcasts(struct device *dev, u32 brd, u32 mask)
{
	u32 net = dev->pa_addr&mask;

	if (dev->flags&IFF_BROADCAST)
		fib_magic(RTMSG_NEWROUTE, RTF_IFBRD, brd, ~0, dev);

	if (net && !(mask&htonl(1))) {
		fib_magic(RTMSG_NEWROUTE, RTF_IFBRD, net, ~0, dev);
		fib_magic(RTMSG_NEWROUTE, RTF_IFBRD, net|~mask, ~0, dev);
	}
}

void ip_rt_change_broadcast(struct device *dev, u32 new_brd)
{
	fib_lock();
	printk(KERN_DEBUG "%s changes brd %08lX -> %08X\n",
	       dev->name, dev->pa_brdaddr, new_brd);
	if (!ZERONET(dev->pa_addr) && dev->flags&IFF_BROADCAST) {
		fib_magic(RTMSG_DELROUTE, RTF_IFBRD, dev->pa_brdaddr, ~0, dev);
		rtmsg_dev(RTMSG_DELDEVICE, dev, NULL);
		rtmsg_dev(RTMSG_NEWDEVICE, dev, NULL);
		ip_rt_add_broadcasts(dev, new_brd, dev->pa_mask);
	}
	fib_unlock();
}

void ip_rt_change_dstaddr(struct device *dev, u32 dstaddr)
{
	fib_lock();
	if (!ZERONET(dev->pa_addr) && (dev->flags&IFF_POINTOPOINT) && dev->type != ARPHRD_TUNNEL) {
		printk(KERN_DEBUG "%s changes dst %08lX -> %08X\n",
		       dev->name, dev->pa_dstaddr, dstaddr);
		fib_magic(RTMSG_DELROUTE, RTF_IFPREFIX, dev->pa_dstaddr, ~0, dev);
		rtmsg_dev(RTMSG_DELDEVICE, dev, NULL);
		rtmsg_dev(RTMSG_NEWDEVICE, dev, NULL);
		if (dstaddr)
			fib_magic(RTMSG_NEWROUTE, RTF_IFPREFIX, dstaddr, ~0, dev);
	}
	fib_unlock();
}

void ip_rt_change_netmask(struct device *dev, u32 mask)
{
	u32 net;

	fib_lock();
	printk(KERN_DEBUG "%s changes netmask %08lX -> %08X\n",
	       dev->name, dev->pa_mask, mask);
	if (ZERONET(dev->pa_addr)) {
		fib_unlock();
		return;
	}
	net = dev->pa_addr&dev->pa_mask;
	fib_magic(RTMSG_DELROUTE, RTF_IFPREFIX, net, dev->pa_mask, dev);
	ip_rt_del_broadcasts(dev);
	if (mask != 0xFFFFFFFF && dev->flags&IFF_POINTOPOINT)
		fib_magic(RTMSG_DELROUTE, RTF_IFPREFIX, dev->pa_dstaddr, ~0, dev);
	rtmsg_dev(RTMSG_DELDEVICE, dev, NULL);

	if (mask != 0xFFFFFFFF)
		dev->flags &= ~IFF_POINTOPOINT;

	rtmsg_dev(RTMSG_NEWDEVICE, dev, NULL);
	net = dev->pa_addr&mask;
	if (net)
		fib_magic(RTMSG_NEWROUTE, RTF_IFPREFIX, net, mask, dev);
	ip_rt_add_broadcasts(dev, dev->pa_addr, mask);
	fib_unlock();
}

int ip_rt_event(int event, struct device *dev)
{
	fib_lock();
	if (event == NETDEV_DOWN) {
		fib_flush(dev);
		rtmsg_dev(RTMSG_DELDEVICE, dev, NULL);
		fib_unlock();
		return NOTIFY_DONE;
	}
	if (event == NETDEV_CHANGE) {
		printk(KERN_DEBUG "%s(%s) changes state fl=%08x pa=%08lX/%08lX brd=%08lX dst=%08lX\n",
		       dev->name, current->comm, dev->flags, dev->pa_addr, dev->pa_mask,
		       dev->pa_brdaddr, dev->pa_dstaddr);
		if (!(dev->flags&IFF_BROADCAST))
			fib_magic(RTMSG_DELROUTE, RTF_IFBRD, dev->pa_brdaddr, ~0, dev);
		if (!(dev->flags&IFF_POINTOPOINT))
			fib_magic(RTMSG_DELROUTE, RTF_IFPREFIX, dev->pa_dstaddr, ~0, dev);
		else {
			u32 net = dev->pa_addr&dev->pa_mask;
			fib_magic(RTMSG_DELROUTE, RTF_IFPREFIX, net, dev->pa_mask, dev);
			ip_rt_del_broadcasts(dev);
		}
		rtmsg_dev(RTMSG_DELDEVICE, dev, NULL);
	}

	if ((event == NETDEV_UP || event == NETDEV_CHANGE) && !ZERONET(dev->pa_addr)) {
		if (dev->flags&IFF_POINTOPOINT) {
			dev->pa_mask = 0xFFFFFFFF;
			dev->ip_flags &= ~IFF_IP_MASK_OK;
			dev->flags &= ~IFF_BROADCAST;
			dev->pa_brdaddr = 0;
		}

		if (event == NETDEV_UP)
			printk(KERN_DEBUG "%s UP fl=%08x pa=%08lX/%08lX brd=%08lX dst=%08lX\n",
			       dev->name, dev->flags, dev->pa_addr,
			       dev->pa_mask, dev->pa_brdaddr, dev->pa_dstaddr);

		rtmsg_dev(RTMSG_NEWDEVICE, dev, NULL);

		if (dev->flags&IFF_POINTOPOINT) {
			if (dev->pa_dstaddr && dev->type != ARPHRD_TUNNEL)
				fib_magic(RTMSG_NEWROUTE, RTF_IFPREFIX, dev->pa_dstaddr, ~0, dev);
		} else {
			u32 net = dev->pa_addr&dev->pa_mask;

			if (net)
				fib_magic(RTMSG_NEWROUTE, RTF_IFPREFIX, net, dev->pa_mask, dev);
			ip_rt_add_broadcasts(dev, dev->pa_brdaddr, dev->pa_mask);
		}
		fib_magic(RTMSG_NEWROUTE, RTF_IFLOCAL, dev->pa_addr, ~0, dev);
		if (dev == &loopback_dev) {
			if (dev->pa_addr != htonl(INADDR_LOOPBACK)) {
				u32 mask = htonl(0xFF000000);
				fib_magic(RTMSG_NEWROUTE, RTF_IFPREFIX,
					  htonl(INADDR_LOOPBACK)&mask,
					  mask, dev);
				fib_magic(RTMSG_NEWROUTE, RTF_IFLOCAL,
					  htonl(INADDR_LOOPBACK),
					  mask, dev);
			}
		}
	}
	if (event == NETDEV_CHANGEMTU || event == NETDEV_CHANGEADDR)
		rtmsg_dev(RTMSG_NEWDEVICE, dev, NULL);
	fib_unlock();
	return NOTIFY_DONE;
}


void ip_fib_init()
{
	struct in_rtrulemsg r;

#ifdef CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_ROUTE, 5, "route",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		fib_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RTCLASSES, 10, "rt_classes",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		fib_class_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RTRULES, 8, "rt_local",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		fib_local_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RTRULES, 8, "rt_rules",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		fib_rules_get_info
	});
#endif		/* CONFIG_PROC_FS */

	fib_classes[RT_CLASS_LOCAL] = &local_class;
	fib_classes[RT_CLASS_MAIN] = &main_class;
	fib_classes[RT_CLASS_DEFAULT] = &default_class;

	memset(&r, 0, sizeof(r));
	r.rtrmsg_class = RT_CLASS_LOCAL;
	r.rtrmsg_preference = 0;
	fib_rule_add(&r, NULL, NULL);

	memset(&r, 0, sizeof(r));
	r.rtrmsg_class = RT_CLASS_DEFAULT;
	r.rtrmsg_preference = 255;
	fib_rule_add(&r, NULL, NULL);

	memset(&r, 0, sizeof(r));
	r.rtrmsg_class = RT_CLASS_MAIN;
	r.rtrmsg_preference = 254;
	fib_rule_add(&r, NULL, NULL);

#ifdef  CONFIG_RTNETLINK
	netlink_attach(NETLINK_ROUTE, fib_netlink_call);
#endif
}
