/*
 *	IPv6 routing table
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */


#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/route.h>
#include <linux/netdevice.h>
#include <linux/in6.h>

#ifdef 	CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include <net/tcp.h>
#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>

#include <net/netlink.h>

#include <asm/uaccess.h>

/*
 *	Routing Table
 *
 *	simplified version of a radix tree
 *
 *	- every node shares it's acestors prefix
 *	- the tree is ordered from less to most specific mask
 *	- default routes are handled apart
 *
 *	this facilitates recursion a lot
 */

static struct rt6_info null_entry = {
	NULL, NULL, 
	{{{0}}},
	0, 1,
	NULL, NULL,
	0, 0, RTF_REJECT
};

struct fib6_node routing_table = {
	NULL, NULL, NULL, &null_entry, 
	0, RTN_ROOT, 0
};

struct rt6_info		*default_rt_list = NULL;
struct rt6_info		*loopback_rt = NULL;

/*
 *	last_resort_rt - no routers present.
 *	Assume all destinations on link.
 */
struct rt6_info		*last_resort_rt = NULL;

static struct rt6_req request_queue = {
	0, NULL, &request_queue, &request_queue
};


/*
 *	A routing update causes an increase of the serial number on the
 *	afected subtree. This allows for cached routes to be asynchronously
 *	tested when modifications are made to the destination cache as a
 *	result of redirects, path MTU changes, etc.
 */

static __u32	rt_sernum	= 0;

static atomic_t rt6_lock	= 0;
static int	rt6_bh_mask	= 0;

#define RT_BH_REQUEST	1
#define RT_BH_GC	2

static void __rt6_run_bh(void);

typedef void (*f_pnode)(struct fib6_node *fn, void *);

static void	rt6_walk_tree(f_pnode func, void * arg, int filter);
static void	rt6_rt_timeout(struct fib6_node *fn, void *arg);
static int	rt6_msgrcv(struct sk_buff *skb);

struct rt6_statistics rt6_stats = {
	1, 0, 1, 1, 0
};

static atomic_t	rt_clients = 0;

void rt6_timer_handler(unsigned long data);

struct timer_list rt6_gc_timer = {
	NULL,
	NULL,
	0,
	0,
	rt6_timer_handler
};

static __inline__ void rt6_run_bh(void)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	if (rt6_lock == 0 && rt6_bh_mask)
	{
		__rt6_run_bh();
	}
	restore_flags(flags);
}

/*
 *	request queue operations
 *	FIFO queue/dequeue
 */
static __inline__ void rtreq_queue(struct rt6_req * req)
{
	unsigned long flags;
	struct rt6_req *next = &request_queue;

	save_flags(flags);
	cli();

	req->prev = next->prev;
	req->prev->next = req;
	next->prev = req;
	req->next = next;
	restore_flags(flags);
}

static __inline__ struct rt6_req * rtreq_dequeue(void)
{
	struct rt6_req *next = &request_queue;
	struct rt6_req *head;

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

/*
 *	compare "prefix length" bits of an address
 */
static __inline__ int addr_match(struct in6_addr *a1, struct in6_addr *a2,
				 int prefixlen)
{
	int pdw;
	int pbi;

	pdw = prefixlen >> 0x05;  /* num of whole __u32 in prefix */
	pbi = prefixlen &  0x1f;  /* num of bits in incomplete u32 in prefix */

	if (pdw) 
	{
		if (memcmp(a1, a2, pdw << 2))
			return 0;
	}

	if (pbi) 
	{
		__u32 w1, w2;
		__u32 mask;

		w1 = a1->s6_addr32[pdw];
		w2 = a2->s6_addr32[pdw];

		mask = htonl((0xffffffff) << (0x20 - pbi));

		if ((w1 ^ w2) & mask)
			return 0;
	}

	return 1;	
}

/*
 *	test bit. range [0-127]
 */

static __inline__ int addr_bit_set(struct in6_addr *addr, int fn_bit)
{
	int dw;
	__u32 b1;
	__u32 mask;
	int bit = fn_bit;

	dw = bit >> 0x05;

	b1 = addr->s6_addr32[dw];
	
	bit = ~bit;
	bit &= 0x1f;
	mask = htonl(1 << bit);
	return (b1 & mask);
}

static __inline__ int addr_bit_equal(struct in6_addr *a1, struct in6_addr *a2,
				     int fn_bit)
{
	int dw;
	__u32 b1, b2;
	__u32 mask;
	int bit = fn_bit;

	dw = bit >> 0x05;

	b1 = a1->s6_addr32[dw];
	b2 = a2->s6_addr32[dw];
	
	bit = ~bit;
	bit &= 0x1f;
	mask = htonl(1 << bit);
	return !((b1 ^ b2) & mask);
}

/*
 *	find the first different bit between two addresses
 */
static __inline__ int addr_diff(struct in6_addr *a1, struct in6_addr *a2)
{
	int i;

	for (i = 0; i<4; i++)
	{
		__u32 b1, b2;
		__u32 xb;
		
		b1 = a1->s6_addr32[i];
		b2 = a2->s6_addr32[i];
		
		xb = b1 ^ b2;

		if (xb)
		{
			int res = 0;
			int j=31;

			xb = ntohl(xb);

			while (test_bit(j, &xb) == 0)
			{
				res++;
				j--;
			}
			
			return (i * 32 + res);
		}
	}

	/*
	 *	bit values are in range [0-127]
	 *	128 is an ilegal value as we should *never* get to
	 *	this point since that would mean the addrs are equal
	 */
	return 128;
}

/*
 *	add a rt to a node that may already contain routes
 *	sort routes in ascending metric order so that fib lookup
 *	returns the smallest metric by default
 */

static __inline__ void fib6_add_rt2node(struct fib6_node *fn,
					struct rt6_info *rt)
{
	struct rt6_info *iter, **back;

	rt->fib_node = fn;
	back = &fn->leaf;
	
	for (iter = fn->leaf; iter; iter=iter->next)
	{
		if (iter->rt_metric > rt->rt_metric)
		{
			break;
		}

		back = &iter->next;
	}

	rt->next = iter;
	*back = rt;
}

/*
 *	Routing Table
 */

static int fib6_add_1(struct rt6_info *rt)
{
	struct fib6_node *fn;
	struct fib6_node *pn = NULL;
	struct fib6_node *in;
	struct fib6_node *ln;
	struct in6_addr *addr;
	__u32	bit;
	__u32	dir = 0;
	__u32	sernum = ++rt_sernum;
	int pbit = rt->rt_prefixlen - 1;

	addr = &rt->rt_dst;

	/* insert node in tree */

	fn = &routing_table;

	for (;;)
	{
		if (fn == NULL)
		{			
			ln = kmalloc(sizeof(struct fib6_node), GFP_ATOMIC);

			if (ln == NULL)
				return (-ENOMEM);

			memset(ln, 0, sizeof(struct fib6_node));
			ln->fn_bit   = pbit;
			ln->fn_flags = RTN_BACKTRACK;
			
			ln->parent = pn;
			ln->leaf = rt;
			ln->fn_sernum = sernum;
			rt->fib_node = ln;

			atomic_inc(&rt->rt_ref);

			if (dir)
				pn->right = ln;
			else
				pn->left  = ln;

			rt6_stats.fib_nodes++;
			rt6_stats.fib_route_nodes++;
			rt6_stats.fib_rt_entries++;

			return(0);
		}

		if (addr_match(&fn->leaf->rt_dst, addr, fn->fn_bit))
		{
			if (pbit == fn->fn_bit &&
			    addr_bit_equal(addr, &fn->leaf->rt_dst,
					   rt->rt_prefixlen))
			{
				/* clean up an intermediate node */
				if ((fn->fn_flags & RTN_BACKTRACK) == 0) 
				{
					rt_release(fn->leaf);
					fn->leaf = NULL;
					fn->fn_flags |= RTN_BACKTRACK;
				}
			
				fib6_add_rt2node(fn, rt);
				fn->fn_sernum = sernum;
				atomic_inc(&rt->rt_ref);
				
				rt6_stats.fib_route_nodes++;
				rt6_stats.fib_rt_entries++;
				
				return 0;
			}

			if (pbit > fn->fn_bit)
			{
				/* walk down on tree */

				fn->fn_sernum = sernum;

				dir = addr_bit_set(addr, fn->fn_bit); 
				pn = fn;
				fn = dir ? fn->right: fn->left;

				continue;
			}
		}

		/*
		 * split since we don't have a common prefix anymore or 
		 * we have a less significant route.
		 * we've to insert an intermediate node on the list
		 * this new node will point to the one we need to create
		 * and the current
		 */

		pn = fn->parent;

		/* find 1st bit in difference between the 2 addrs */
		bit = addr_diff(addr, &fn->leaf->rt_dst);


		/* 
		 *		(intermediate)	
		 *	          /	   \
		 *	(new leaf node)    (old node)
		 */
		if (rt->rt_prefixlen > bit)
		{
			in = kmalloc(sizeof(struct fib6_node), GFP_ATOMIC);
		
			if (in == NULL)
				return (-ENOMEM);

			memset(in, 0, sizeof(struct fib6_node));

			/* 
			 * new intermediate node. 
			 * RTN_BACKTRACK will
			 * be off since that an address that chooses one of
			 * the branches would not match less specific routes
			 * int the other branch
			 */

			in->fn_bit = bit;
			in->parent = pn;
			in->leaf = rt;
			in->fn_sernum = sernum;
			atomic_inc(&rt->rt_ref);

			/* leaf node */
			ln = kmalloc(sizeof(struct fib6_node), GFP_ATOMIC);

			if (ln == NULL)
			{
				kfree(in);
				return (-ENOMEM);
			}

			/* update parent pointer */
			if (dir)
				pn->right = in;
			else
				pn->left  = in;

			memset(ln, 0, sizeof(struct fib6_node));
			ln->fn_bit   = pbit;
			ln->fn_flags = RTN_BACKTRACK;
			
			ln->parent = in;
			fn->parent = in;

			ln->leaf = rt;
			ln->fn_sernum = sernum;
			atomic_inc(&rt->rt_ref);

			rt->fib_node = ln;

			if (addr_bit_set(addr, bit))
			{
				in->right = ln;
				in->left  = fn;
			}
			else
			{
				in->left  = ln;
				in->right = fn;
			}

			rt6_stats.fib_nodes += 2;
			rt6_stats.fib_route_nodes++;
			rt6_stats.fib_rt_entries++;

			return 0;
		}

		/* 
		 *		(new leaf node)
		 *	          /	   \
		 *	     (old node)    NULL
		 */

		ln = kmalloc(sizeof(struct fib6_node), GFP_ATOMIC);

		if (ln == NULL)
			return (-ENOMEM);

		memset(ln, 0, sizeof(struct fib6_node));
		ln->fn_bit   = pbit;
		ln->fn_flags = RTN_BACKTRACK;
			

		ln->parent = pn;
		ln->leaf = rt;
		ln->fn_sernum = sernum;
		atomic_inc(&rt->rt_ref);

		rt->fib_node = ln;

		if (dir)
			pn->right = ln;
		else
			pn->left  = ln;
		

		if (addr_bit_set(&fn->leaf->rt_dst, pbit))
			ln->right = fn;
		else
			ln->left  = fn; 

		fn->parent = ln;

		rt6_stats.fib_nodes++;
		rt6_stats.fib_route_nodes++;
		rt6_stats.fib_rt_entries++;

		return(0);
	}

	return (-1);
}

static struct rt6_info * fib6_lookup_1(struct in6_addr *addr, int flags)
{
	struct fib6_node *fn, *next;
	int dir;

	fn = &routing_table;

	for (;;)
	{
		dir = addr_bit_set(addr, fn->fn_bit);

		next = dir ? fn->right: fn->left;

		if (next)
		{
			fn = next;
			continue;
		}

		break;
	}


	while ((fn->fn_flags & RTN_ROOT) == 0)
	{
		if (fn->fn_flags & RTN_BACKTRACK)
		{
			if (addr_match(&fn->leaf->rt_dst, addr, 
				       fn->leaf->rt_prefixlen))
			{
				struct rt6_info *rt;
				
				for (rt = fn->leaf; rt; rt = rt->next)
				{
					if ((rt->rt_flags & flags) == 0)
						return rt;
				}
			}
		}
		
		fn = fn->parent;
	}

	return NULL;
}



/*
 *	called to trim the tree of intermediate nodes when possible
 */

static void fib6_del_3(struct fib6_node *fn)
{
	int children = 0;
	int dir = 0;
	int bit;

	/*
	 *	0 or one children:
	 *		delete the node
	 *
	 *	2 children:
	 *		move the bit down
	 */

	if (fn->left)
	{
		children++;
		dir = 0;
	}

	if (fn->right)
	{
		children++;
		dir = 1;
	}

	if (children < 2)
	{
		struct fib6_node *child;

		child = dir ? fn->right : fn->left;

		if (fn->parent->left == fn)
		{
			fn->parent->left = child;
		}
		else
		{
			fn->parent->right = child;
		}

		if (child)
		{
			child->parent = fn->parent;
		}

		/* 
		 *	try to collapse on top
		 */			
		if ((fn->parent->fn_flags & (RTN_BACKTRACK | RTN_ROOT)) == 0)
		{
			if (fn->leaf)
			{
				rt_release(fn->leaf);
				fn->leaf = NULL;
			}
			fib6_del_3(fn->parent);
		}
		if (fn->fn_flags & RTN_BACKTRACK)
		{
			rt6_stats.fib_route_nodes--;
		}
		rt6_stats.fib_nodes--;
		kfree(fn);
		return;
	}

	bit = addr_diff(&fn->left->leaf->rt_dst, &fn->right->leaf->rt_dst);
	
	fn->fn_bit = bit;
	fn->fn_flags &= ~RTN_BACKTRACK;
	fn->leaf = fn->left->leaf;

	rt6_stats.fib_route_nodes--;
}

static struct fib6_node * fib6_del_2(struct in6_addr *addr, __u32 prefixlen, 
				     struct in6_addr *gw, struct device *dev)
{
	struct fib6_node *fn;

	for (fn = &routing_table; fn;) 
	{
		int dir;

		if ((fn->fn_flags & RTN_BACKTRACK) &&
		    prefixlen == fn->leaf->rt_prefixlen &&
		    addr_match(&fn->leaf->rt_dst, addr, fn->leaf->rt_prefixlen)
		    )
		{
			break;
		}

		dir = addr_bit_set(addr, fn->fn_bit);

		fn = dir ? fn->right: fn->left;
	}

	/* 
	 *	if route tree node found
	 *	search among it's entries
	 */

	if (fn)
	{
		struct rt6_info *back = NULL;
		struct rt6_info *lf;

		for(lf = fn->leaf; lf; lf=lf->next)
		{
			if ((gw && (ipv6_addr_cmp(addr, &lf->rt_dst) == 0)) ||
			    (dev && dev == lf->rt_dev))
			{
				/* delete this entry */
				if (back == NULL)
					fn->leaf = lf->next;
				else
					back->next = lf->next;

				lf->fib_node = NULL;
				rt_release(lf);
				return fn;		
			}
			back = lf;
		}
	}

	return NULL;
}

static struct fib6_node * fib6_del_rt_2(struct in6_addr *addr, __u32 prefixlen,
					struct rt6_info *rt)
{
	struct fib6_node *fn;

	for (fn = &routing_table; fn;) 
	{
		int dir;

		if ((fn->fn_flags & RTN_BACKTRACK) &&
		    prefixlen == fn->leaf->rt_prefixlen &&
		    addr_match(&fn->leaf->rt_dst, addr, fn->leaf->rt_prefixlen)
		    )
		{
			break;
		}

		dir = addr_bit_set(addr, fn->fn_bit);

		fn = dir ? fn->right: fn->left;
	}

	/* 
	 *	if route tree node found
	 *	search among its entries
	 */

	if (fn)
	{
		struct rt6_info *back = NULL;
		struct rt6_info *lf;

		for(lf = fn->leaf; lf; lf=lf->next)
		{
			if (rt == lf)
			{
				/* delete this entry */
				if (back == NULL)
					fn->leaf = lf->next;
				else
					back->next = lf->next;

				lf->fib_node = NULL;
				rt_release(lf);
				return fn;
			}
			back = lf;
		}
	}

	return NULL;
}

int fib6_del_1(struct in6_addr *addr, __u32 prefixlen, struct in6_addr *gw, 
	       struct device *dev)
{
	struct fib6_node *fn;

	fn = fib6_del_2(addr, prefixlen, gw, dev);

	if (fn == NULL)
		return -ENOENT;
	
	if (fn->leaf == NULL)
	{
		fib6_del_3(fn);
	}

	return 0;
}

int fib6_del_rt(struct rt6_info *rt)
{
	struct fib6_node *fn;

	fn = fib6_del_rt_2(&rt->rt_dst, rt->rt_prefixlen, rt);

	if (fn == NULL)
		return -ENOENT;
	
	if (fn->leaf == NULL)
	{
		fib6_del_3(fn);
	}

	return 0;
}

static void fib6_flush_1(struct fib6_node *fn, void *p_arg)
{
	struct rt6_info *rt;

	for (rt = fn->leaf; rt;)
	{
		struct rt6_info *itr;

		itr = rt;
		rt = rt->next;
		itr->fib_node = NULL;
		rt_release(itr);
	}
	
	if (fn->fn_flags & RTN_BACKTRACK)
	{
		rt6_stats.fib_route_nodes--;
	}
	rt6_stats.fib_nodes--;
	kfree(fn);
}

void fib6_flush(void)
{
	rt6_walk_tree(fib6_flush_1, NULL, 0);
}

int ipv6_route_add(struct in6_rtmsg *rtmsg)
{
	struct rt6_info *rt;
	struct device * dev = NULL;
	struct rt6_req *request;
	int flags = rtmsg->rtmsg_flags;

	dev = dev_get(rtmsg->rtmsg_device);
	
	rt = (struct rt6_info *) kmalloc(sizeof(struct rt6_info),
					 GFP_ATOMIC);

	rt6_stats.fib_rt_alloc++;

	memset(rt, 0, sizeof(struct rt6_info));
		
	memcpy(&rt->rt_dst, &rtmsg->rtmsg_dst, sizeof(struct in6_addr));
	rt->rt_prefixlen = rtmsg->rtmsg_prefixlen;
	
	if (flags & (RTF_GATEWAY | RTF_NEXTHOP)) 
	{
		/* check to see if its an acceptable gateway */
		if (flags & RTF_GATEWAY)
		{
			struct rt6_info *gw_rt;

			gw_rt = fibv6_lookup(&rtmsg->rtmsg_gateway, NULL,
					     RTI_GATEWAY);

			if (gw_rt == NULL)
			{
				return -EHOSTUNREACH;
			}

			dev = gw_rt->rt_dev;
		}

		rt->rt_nexthop = ndisc_get_neigh(dev, &rtmsg->rtmsg_gateway);

		if (rt->rt_nexthop == NULL)
		{
			printk(KERN_DEBUG "ipv6_route_add: no nexthop\n");
			kfree(rt);
			return -EINVAL;
		}

		rt->rt_dev = dev;

		if (loopback_rt == NULL && (dev->flags & IFF_LOOPBACK))
		{
			loopback_rt = rt;
		}

	}
	else
	{
		if (dev == NULL)
		{
			printk(KERN_DEBUG "ipv6_route_add: NULL dev\n");
			kfree(rt);
			return -EINVAL;
		}

		rt->rt_dev = dev;
		rt->rt_nexthop = NULL;
	}
	
	rt->rt_metric = rtmsg->rtmsg_metric;
	rt->rt_flags = rtmsg->rtmsg_flags;

	if (rt->rt_flags & RTF_ADDRCONF)
	{
		rt->rt_expires = rtmsg->rtmsg_info;
	}

	request = kmalloc(sizeof(struct rt6_req), GFP_ATOMIC);
	if (request == NULL)
	{
		printk(KERN_WARNING "ipv6_route_add: kmalloc failed\n");
		return -ENOMEM;
	}

	request->operation = RT_OPER_ADD;
	request->ptr = rt;
	request->next = request->prev = NULL;
	rtreq_queue(request);
	rt6_bh_mask |= RT_BH_REQUEST;

	rt6_run_bh();

	return 0;
}

int ipv6_route_del(struct in6_rtmsg *rtmsg)
{
	struct rt6_info * rt;

	rt = fib6_lookup_1(&rtmsg->rtmsg_dst, 0);
	if (!rt || (rt && (rt->rt_prefixlen != rtmsg->rtmsg_prefixlen)))
		return -ENOENT;
	return fib6_del_rt(rt);
}

/*
 *	search the routing table
 *	the flags parameter restricts the search to entries where
 *	the flag is *not* set
 */
struct rt6_info * fibv6_lookup(struct in6_addr *addr, struct device *src_dev,
			       int flags)
{
	struct rt6_info *rt;

	if ((rt = fib6_lookup_1(addr, flags)))
	{
		if (src_dev)
		{
			for (; rt; rt=rt->next)
			{
				if (rt->rt_dev == src_dev)
					return rt;
			}
			
			if (flags & RTI_DEVRT)
			{
				return NULL;
			}
		}

		return rt;
	}

	if (!(flags & RTI_GATEWAY))
	{
		if ((rt = dflt_rt_lookup()))
		{
			return rt;
		}

		return last_resort_rt;		
	}

	return NULL;
}

/*
 *	Destination Cache
 */

struct dest_entry * ipv6_dst_route(struct in6_addr * daddr,
				   struct device *src_dev,
				   int flags)
{
	struct dest_entry * dc = NULL;
	struct rt6_info * rt;

	atomic_inc(&rt6_lock);
	
	rt = fibv6_lookup(daddr, src_dev, flags);

	if (rt == NULL)
	{
		goto exit;
	}
	
	if (rt->rt_nexthop)
	{
		/*
		 *	We can use the generic route
		 *	(warning: the pmtu value maybe invalid)
		 */

		dc = (struct dest_entry *) rt;
		atomic_inc(&rt->rt_use);
	}
	else
	{
		struct rt6_req *request;

		if (ipv6_chk_addr(daddr) && !(rt->rt_dev->flags & IFF_LOOPBACK))
		{
			rt = loopback_rt;

			if (rt == NULL)
			{
				goto exit;
			}
		}

		/*
		 *	dynamicly allocate a new route
		 */
		
		dc = (struct dest_entry *) kmalloc(sizeof(struct dest_entry), 
					   GFP_ATOMIC);

		if (dc == NULL)
		{
			printk(KERN_WARNING "dst_route: kmalloc failed\n");
			goto exit;
		}

		rt6_stats.fib_rt_alloc++;
		rt6_stats.fib_dc_alloc++;

		memset(dc, 0, sizeof(struct dest_entry));

		memcpy(&dc->dc_addr, daddr, sizeof(struct in6_addr));
		dc->rt.rt_prefixlen = 128;
		dc->dc_usecnt = 1;
		dc->rt.rt_metric = rt->rt_metric;

		dc->dc_flags = (rt->rt_flags | RTF_HOST | RTI_DYNAMIC |
				RTI_DCACHE | DCF_PMTU);

		dc->dc_pmtu = rt->rt_dev->mtu;
		dc->rt.rt_dev = rt->rt_dev;
		dc->rt.rt_output_method = rt->rt_output_method;
		dc->dc_tstamp = jiffies;
		/* add it to the request queue */
		
		request = kmalloc(sizeof(struct rt6_req), GFP_ATOMIC);

		if (request == NULL)
		{
			printk(KERN_WARNING "dst_route: kmalloc failed\n");
			dc = NULL;
			goto exit;
		}

		dc->dc_nexthop = ndisc_get_neigh(rt->rt_dev, daddr);

		rt6_bh_mask |= RT_BH_REQUEST;
		
		request->operation = RT_OPER_ADD;
		request->ptr = (struct rt6_info *) dc;
		request->next = request->prev = NULL;
		rtreq_queue(request);
	}

	atomic_inc(&rt_clients);

  exit:
       
	atomic_dec(&rt6_lock);
	rt6_run_bh();

	return dc;
}

/*
 *	check cache entry for vality...
 *	this needs to be done as a inline func that calls
 *	ipv6_slow_dst_check if entry is invalid
 */

struct dest_entry * ipv6_dst_check(struct dest_entry *dc,
				   struct in6_addr *daddr,
				   __u32 sernum, int flags)
{
	int uptodate = 0;

	/*
	 *	destination cache becomes invalid when routing
	 *	changes or a more specific dynamic entry is
	 *	created.
	 *	if route is removed from table fib_node will
	 *	become NULL
	 */

	if (dc->rt.fib_node && (dc->rt.fib_node->fn_sernum == sernum))
		uptodate = 1;

	if (uptodate && ((dc->dc_flags & DCF_INVALID) == 0))
	{
		if (dc->dc_nexthop && !(dc->dc_nexthop->flags & NCF_NOARP))
		{
			ndisc_event_send(dc->dc_nexthop, NULL);
		}
		return dc;
	}

	/* route for destination may have changed */

	ipv6_dst_unlock(dc);

	return ipv6_dst_route(daddr, NULL, flags);
}

void ipv6_dst_unlock(struct dest_entry *dc)
{
	/*
	 *	decrement counter and mark entry for deletion
	 *	if counter reaches 0. we delay deletions in hope
	 *	we can reuse cache entries.
	 */

	atomic_dec(&dc->dc_usecnt);
	
	if (dc->dc_usecnt == 0)
	{

		if (dc->dc_flags & RTI_DCACHE)
		{
			/*
			 *	update last usage tstamp
			 */

			dc->dc_tstamp = jiffies;
			rt6_bh_mask |= RT_BH_GC;
		}

		if (dc->rt.rt_ref == 0)
		{
			/*
			 *	entry out of the routing table
			 *	pending to be released on last deref
			 */

			if (dc->dc_nexthop)
			{
				ndisc_dec_neigh(dc->dc_nexthop);
			}
			
			if (dc->dc_flags & RTI_DCACHE)
			{
				rt6_stats.fib_dc_alloc--;
			}

			rt6_stats.fib_rt_alloc--;
			kfree(dc);
		}

	}

	atomic_dec(&rt_clients);
}

/*
 *	Received a packet too big icmp that lowers the mtu for this
 *	address. If the route for the destination is genric we create
 *	a new route with the apropriate MTU info. The route_add
 *	procedure will update the serial number on the generic routes
 *	belonging to the afected tree forcing clients to request a route
 *	lookup.
 */
void rt6_handle_pmtu(struct in6_addr *addr, int pmtu)
{
	struct rt6_info *rt;
	struct rt6_req *req;
	struct dest_entry *dc;

	printk(KERN_DEBUG "rt6_handle_pmtu\n");

	if (pmtu < 0 || pmtu > 65536)
	{
		printk(KERN_DEBUG "invalid MTU value\n");
		return;
	}

	rt = fibv6_lookup(addr, NULL, 0);

	if (rt == NULL)
	{
		printk(KERN_DEBUG "rt6_handle_pmtu: route not found\n");
		return;
	}

	if (rt->rt_flags & RTI_DCACHE)
	{
		/*
		 *	we do have a destination cache entry for this
		 *	address.
		 */
		
		dc = (struct dest_entry *) rt;
		
		/*
		 *	fixme: some sanity checks are likely to be needed
		 *	 here
		 */

		dc->dc_pmtu = pmtu;
		dc->dc_flags |= DCF_PMTU;
		return;
	}

	req = (struct rt6_req *) kmalloc(sizeof(struct rt6_req), GFP_ATOMIC);

	/* now add the new destination cache entry	*/
	
	dc = (struct dest_entry *) kmalloc(sizeof(struct dest_entry),
					   GFP_ATOMIC);
	
	rt6_stats.fib_rt_alloc++;
	rt6_stats.fib_dc_alloc++;

	memset(dc, 0, sizeof(struct dest_entry));
	
	memcpy(&dc->dc_addr, addr, sizeof(struct in6_addr));
	dc->rt.rt_prefixlen = 128;
	dc->rt.rt_metric = rt->rt_metric;

	dc->dc_flags = (rt->rt_flags | RTI_DYNAMIC | RTI_DCACHE | DCF_PMTU |
			RTF_HOST);

	dc->dc_pmtu = pmtu;
	dc->dc_tstamp = jiffies;
	
	dc->dc_nexthop = rt->rt_nexthop;
	atomic_inc(&dc->dc_nexthop->refcnt);

	dc->rt.rt_dev = rt->rt_dev;
	dc->rt.rt_output_method = rt->rt_output_method;

	req->operation = RT_OPER_ADD;
	req->ptr = (struct rt6_info *) dc;
	req->next = req->prev = NULL;

	rtreq_queue(req);

	rt6_bh_mask |= RT_BH_REQUEST;

	rt6_run_bh();
}

/*
 *	Redirect received: target is nexthop for dest
 */
struct rt6_info * ipv6_rt_redirect(struct device *dev, struct in6_addr *dest,
				   struct in6_addr *target, int on_link)
				       
{
	struct rt6_info *rt;
	struct rt6_req *req;
	int metric;

	rt = fibv6_lookup(dest, dev, 0);

	if (rt == NULL)
	{
		printk(KERN_WARNING "rt_redirect: unable to locate route\n");
		return NULL;
	}

	metric = rt->rt_metric;

	if ((rt->rt_flags & RTF_HOST) == 0)
	{
		/* Need to create an host route for this address */
		
		rt = (struct rt6_info *) kmalloc(sizeof(struct rt6_info),
						 GFP_ATOMIC);
		memset(rt, 0, sizeof(struct rt6_info));
		ipv6_addr_copy(&rt->rt_dst, dest);
		rt->rt_prefixlen = 128;
		rt->rt_flags = RTF_HOST | RTF_UP;
		rt->rt_dev = dev;

		/*
		 *	clone rt->rt_output_method ?
		 */

		rt->rt_metric = metric;

		rt6_stats.fib_rt_alloc++;

		req = (struct rt6_req *) kmalloc(sizeof(struct rt6_req),
						 GFP_ATOMIC);
		req->operation = RT_OPER_ADD;
		req->ptr  = rt;
		req->next = req->prev = NULL;
		
		rtreq_queue(req);
		rt6_bh_mask |= RT_BH_REQUEST;
	}
	else
	{
		rt->rt_flags |= RTF_MODIFIED;
	}

	rt->rt_flags |= RTF_DYNAMIC;
 
	if (on_link)
	{
		rt->rt_flags &= ~RTF_GATEWAY;
	}
	else
	{
		rt->rt_flags |= RTF_GATEWAY;
	}

	if (rt->rt_nexthop)
	{
		if (ipv6_addr_cmp(&rt->rt_nexthop->addr, target) == 0)
		{
			atomic_inc(&rt->rt_nexthop->refcnt);
			goto exit;
		}
		else
		{
			ndisc_dec_neigh(rt->rt_nexthop);
		}
	}
	
	rt->rt_nexthop = ndisc_get_neigh(dev, target);

  exit:
	rt6_run_bh();
	return rt;
}

static int dcache_gc_node(struct fib6_node *fn, int timeout)
{
	struct rt6_info *rt, *back;
	int more = 0;
	unsigned long now = jiffies;

	back = NULL;

	for (rt = fn->leaf; rt;)
	{
		if ((rt->rt_flags & RTI_DCACHE) && rt->rt_use == 0)
		{
			struct dest_entry *dc;
			
			dc = (struct dest_entry *) rt;
			
			if (now - dc->dc_tstamp > timeout)
			{
				struct rt6_info *old;

				old = rt;

				rt = rt->next;

				if (back == NULL)
				{
					fn->leaf = rt;
				}
				else
				{
					back->next = rt;
				}

				old->fib_node = NULL;
				rt_release(old);
				rt6_stats.fib_rt_entries--;
				continue;
			}
			else
			{
				more++;
			}
		}

		back = rt;
		rt = rt->next;
	}

	if (fn->leaf == NULL)
	{
		return -1;
	}
	return more;
}

struct dc_gc_args {
	unsigned long	timeout;
	int		more;
};

static void dc_garbage_collect(struct fib6_node *fn, void *p_arg)
{
	struct dc_gc_args * args = (struct dc_gc_args *) p_arg;
	
	if (fn->fn_flags & RTN_BACKTRACK)
	{
		if (fn->fn_bit == 127)
		{
			int more;
						
			more = dcache_gc_node(fn, args->timeout);

			if (more == -1)
			{
				if (fn->parent->left == fn)
					fn->parent->left = NULL;
				else
					fn->parent->right = NULL;
				
				kfree(fn);

				rt6_stats.fib_nodes--;
				rt6_stats.fib_route_nodes--;
				
				return;
			}
			args->more += more;
		}
	}
	else if (!(fn->fn_flags & RTN_ROOT))
	{
		int children = 0;
		struct fib6_node *chld = NULL;

		if (fn->left)
		{
			children++;
			chld = fn->left;			
		}
			
		if (fn->right)
		{
			children++;
			chld = fn->right;
		}
		
		if (children <= 1)
		{			
			struct fib6_node *pn = fn->parent;
			
			if (pn->left == fn)
			{
				pn->left = chld;
			}
			else
			{
				pn->right = chld;
			}
			
			if (chld)
			{
				chld->parent = pn;
			}
			
			rt_release(fn->leaf);
			
			rt6_stats.fib_nodes--;
			kfree(fn);
		}	       
	}
}

/*
 *	called with ints off
 */

static void __rt6_run_bh(void)
{
	static last_gc_run = 0;

	if (rt6_bh_mask & RT_BH_REQUEST)
	{
		struct rt6_req *request;

		while ((request = rtreq_dequeue()))
		{
			struct rt6_info *rt;

			rt = request->ptr;

			switch (request->operation) {
			case RT_OPER_ADD:
				fib6_add_1(rt);
				break;

			case RT_OPER_DEL:
				fib6_del_rt(rt);
				break;

			default:
				printk(KERN_WARNING
				       "rt6_run_bh: bad request in queue\n");
			}

			kfree(request);
		}

		rt6_bh_mask &= ~RT_BH_REQUEST;
	}

	if (rt6_bh_mask & RT_BH_GC)
	{
		if (jiffies - last_gc_run > DC_TIME_RUN)
		{
			struct dc_gc_args args;

			if (rt6_stats.fib_dc_alloc >= DC_WATER_MARK)
				args.timeout = DC_SHORT_TIMEOUT;
			else
				args.timeout = DC_LONG_TIMEOUT;

			args.more = 0;
			rt6_walk_tree(dc_garbage_collect, &args, 0);

			last_gc_run = jiffies;
			
			if (!args.more)
			{
				rt6_bh_mask &= ~RT_BH_GC;
			}
		}
	}
}

/*
 *	Timer for expiring routes learned via addrconf and stale DC 
 *	entries when there is no network actuvity
 */

void rt6_timer_handler(unsigned long data)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	if (rt6_lock == 0)
	{
		if (rt_clients == 0 && rt6_bh_mask)
		{
			__rt6_run_bh();
		}

		/*
		 *	route expiry
		 */
		
		rt6_walk_tree(rt6_rt_timeout, NULL, 1);
	}

	restore_flags(flags);

	rt6_gc_timer.expires = jiffies + 4 * DC_LONG_TIMEOUT;
	add_timer(&rt6_gc_timer);
}

/*
 *	Check if routes should be timed out.
 *	Called from rt6_walk_tree for every node.
 */

static void rt6_rt_timeout(struct fib6_node *fn, void *arg)
{
	struct rt6_info *rt;
	unsigned long now = jiffies;

	for (rt = fn->leaf; rt; rt = rt->next)
	{
		if ((rt->rt_flags & RTF_ADDRCONF) && now > rt->rt_expires)
		{
			struct rt6_req *req;

			/*
			 *	request route deletion. routes will only
			 *	be deleted after walk_tree completes
			 */

			req = (struct rt6_req *) kmalloc(sizeof(struct rt6_req),
							 GFP_ATOMIC);
			req->operation = RT_OPER_DEL;
			req->ptr  = rt;
			req->next = req->prev = NULL;
		}
	}
}

int ipv6_route_ioctl(unsigned int cmd, void *arg)
{
	struct in6_rtmsg rtmsg;
	int err;

	switch(cmd) 
	{
		case SIOCADDRT:		/* Add a route */
		case SIOCDELRT:		/* Delete a route */
			if (!suser())
				return -EPERM;
			err = copy_from_user(&rtmsg, arg,
					     sizeof(struct in6_rtmsg));
			if (err)
				return -EFAULT;
			return (cmd == SIOCDELRT) ? ipv6_route_del(&rtmsg) : 
				ipv6_route_add(&rtmsg);
	}

	return -EINVAL;
}

static void rt6_walk_tree(f_pnode func, void * arg, int filter)
{
	struct fib6_node *fn;
	/*
	 *	adquire lock
	 *	this warranties that the operation will be atomic with
	 *	respect to the garbage collect routine that also does
	 *	a tree transversal and tags nodes with the RTN_TAG flag
	 */
	atomic_inc(&rt6_lock);

	fn = &routing_table;

	do {
		if (!(fn->fn_flags & RTN_TAG))
		{
			fn->fn_flags |= RTN_TAG;

			if (fn->left)
			{
				fn = fn->left;
				continue;
			}
		}

		fn->fn_flags &= ~RTN_TAG;

		if (fn->right)
		{
			fn = fn->right;
			continue;
		}
	       		
		do {
			struct fib6_node *node;
			
			if (fn->fn_flags & RTN_ROOT)
				break;
			node = fn;
			fn = fn->parent;
			
			if (!(node->fn_flags & RTN_TAG) && 
			    (!filter || (node->fn_flags & RTN_BACKTRACK)))
			{
				(*func)(node, arg);
			}

		} while (!(fn->fn_flags & RTN_TAG));
		
	} while (!(fn->fn_flags & RTN_ROOT) || (fn->fn_flags & RTN_TAG));

	atomic_dec(&rt6_lock);
}

#ifdef CONFIG_PROC_FS
#define RT6_INFO_LEN (32 + 2 + 32 + 2 + 2 + 2 + 4 + 8 + 7)

struct rt6_proc_arg {
	char *buffer;
	int offset;
	int skip;
	int len;
};

static void rt6_info_node(struct fib6_node *fn, void *p_arg)
{
	struct rt6_info *rt;
	struct rt6_proc_arg *arg = (struct rt6_proc_arg *) p_arg;

	for (rt = fn->leaf; rt; rt = rt->next)
	{
		int i;

		if (arg->skip < arg->offset / RT6_INFO_LEN)
		{
			arg->skip++;
			continue;
		}
	
		for (i=0; i<16; i++)
		{
			sprintf(arg->buffer + arg->len, "%02x",
				rt->rt_dst.s6_addr[i]);
			arg->len += 2;
		}
		arg->len += sprintf(arg->buffer + arg->len, " %02x ",
				    rt->rt_prefixlen);
		if (rt->rt_nexthop)
		{
			for (i=0; i<16; i++)
			{
				sprintf(arg->buffer + arg->len, "%02x",
					rt->rt_nexthop->addr.s6_addr[i]);
				arg->len += 2;
			}
		}
		else
		{
			sprintf(arg->buffer + arg->len,
				"00000000000000000000000000000000");
			arg->len += 32;
		}
		arg->len += sprintf(arg->buffer + arg->len,
				    " %02x %02x %02x %04x %8s\n",
				    rt->rt_metric, rt->rt_use,
				    rt->rt_ref, rt->rt_flags, 
				    rt->rt_dev ? rt->rt_dev->name : "");
	}
}

static int rt6_proc_info(char *buffer, char **start, off_t offset, int length,
			 int dummy)
{
	struct rt6_proc_arg arg;
	struct fib6_node sfn;
	arg.buffer = buffer;
	arg.offset = offset;
	arg.skip = 0;
	arg.len = 0;

	rt6_walk_tree(rt6_info_node, &arg, 1);
	
	sfn.leaf = default_rt_list;
	rt6_info_node(&sfn, &arg);

	sfn.leaf = last_resort_rt;
	rt6_info_node(&sfn, &arg);
			     
	*start = buffer;

	if (offset)
		*start += offset % RT6_INFO_LEN;

	arg.len -= offset % RT6_INFO_LEN;

	if (arg.len > length)
		arg.len = length;

	return arg.len;
}


static int rt6_proc_stats(char *buffer, char **start, off_t offset, int length,
			  int dummy)
{
	int len;

	len = sprintf(buffer, "%04x %04x %04x %04x %04x\n",
		      rt6_stats.fib_nodes, rt6_stats.fib_route_nodes,
		      rt6_stats.fib_rt_alloc, rt6_stats.fib_rt_entries,
		      rt6_stats.fib_dc_alloc);

	len -= offset;

	if (len > length)
		len = length;

	*start = buffer + offset;

	return len;
}

#endif			/* CONFIG_PROC_FS */

void ipv6_route_init(void)
{
#ifdef 	CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RT6, 6, "route6",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		rt6_proc_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RT6_STATS, 9, "rt6_stats",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		rt6_proc_stats
	});

#endif
	rt6_gc_timer.expires = jiffies + 4 * DC_LONG_TIMEOUT;
	add_timer(&rt6_gc_timer);
	netlink_attach(NETLINK_ROUTE6, rt6_msgrcv);
}

#ifdef MODULE
void ipv6_route_cleanup(void)
{
	proc_net_unregister(PROC_NET_RT6);
	proc_net_unregister(PROC_NET_RT6_STATS);
	netlink_detach(NETLINK_ROUTE6);
	del_timer(&rt6_gc_timer);
	fib6_flush();
}
#endif

/*
 *	NETLINK interface
 *	routing socket moral equivalent
 */

static int rt6_msgrcv(struct sk_buff *skb)
{
	int count = 0;
	struct in6_rtmsg *rtmsg;
	
	while (skb->len)
	{
		if (skb->len < sizeof(struct in6_rtmsg))
		{
			count = -EINVAL;
			goto out;
		}
		
		rtmsg = (struct in6_rtmsg *) skb->data;
		skb_pull(skb, sizeof(struct in6_rtmsg));
		count += sizeof(struct in6_rtmsg);

		switch (rtmsg->rtmsg_type) {
		case RTMSG_NEWROUTE:
			ipv6_route_add(rtmsg);
			break;
		case RTMSG_DELROUTE:
			ipv6_route_del(rtmsg);
			break;
		default:
			count = -EINVAL;
			goto out;
		}
	}

  out:
	kfree_skb(skb, FREE_READ);	
	return count;
}

void rt6_sndmsg(__u32 type, struct in6_addr *dst, struct in6_addr *gw,
		__u16 plen, __u16 metric, char *devname, __u16 flags)
{
	struct sk_buff *skb;
	struct in6_rtmsg *msg;
	
	skb = alloc_skb(sizeof(struct in6_rtmsg), GFP_ATOMIC);
	msg = (struct in6_rtmsg *) skb_put(skb, sizeof(struct in6_rtmsg));
	
	msg->rtmsg_type = type;

	if (dst)
	{
		ipv6_addr_copy(&msg->rtmsg_dst, dst);
	}
	else
		memset(&msg->rtmsg_dst, 0, sizeof(struct in6_addr));

	if (gw)
	{
		ipv6_addr_copy(&msg->rtmsg_gateway, gw);
	}
	else
		memset(&msg->rtmsg_gateway, 0, sizeof(struct in6_addr));

	msg->rtmsg_prefixlen = plen;
	msg->rtmsg_metric = metric;
	strcpy(msg->rtmsg_device, devname);
	msg->rtmsg_flags = flags;

	if (netlink_post(NETLINK_ROUTE6, skb))
	{
		kfree_skb(skb, FREE_WRITE);
	}
}
