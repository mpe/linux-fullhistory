/*
 *	Linux INET6 implementation 
 *	Forwarding Information Database
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: ip6_fib.c,v 1.14 1998/05/07 15:43:03 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/route.h>
#include <linux/netdevice.h>
#include <linux/in6.h>

#ifdef 	CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>

#include <net/ip6_fib.h>
#include <net/ip6_route.h>

#define RT_DEBUG 2

struct rt6_statistics	rt6_stats;

/*
 *	A routing update causes an increase of the serial number on the
 *	afected subtree. This allows for cached routes to be asynchronously
 *	tested when modifications are made to the destination cache as a
 *	result of redirects, path MTU changes, etc.
 */

static __u32	rt_sernum	= 0;

static struct timer_list ip6_fib_timer = {
	NULL, NULL,
	0,
	0,
	fib6_run_gc
};

/*
 *	Auxiliary address test functions for the radix tree.
 *
 *	These assume a 32bit processor (although it will work on 
 *	64bit processors)
 */

/*
 *	compare "prefix length" bits of an address
 */

static __inline__ int addr_match(void *token1, void *token2, int prefixlen)
{
	__u32 *a1 = token1;
	__u32 *a2 = token2;
	int pdw;
	int pbi;

	pdw = prefixlen >> 0x05;  /* num of whole __u32 in prefix */
	pbi = prefixlen &  0x1f;  /* num of bits in incomplete u32 in prefix */

	if (pdw)
		if (memcmp(a1, a2, pdw << 2))
			return 0;

	if (pbi) {
		__u32 w1, w2;
		__u32 mask;

		w1 = a1[pdw];
		w2 = a2[pdw];

		mask = htonl((0xffffffff) << (0x20 - pbi));

		if ((w1 ^ w2) & mask)
			return 0;
	}

	return 1;
}

/*
 *	test bit
 */

static __inline__ int addr_bit_set(void *token, int fn_bit)
{
	int dw;
	__u32 b1;
	__u32 mask;
	int bit = fn_bit;
	__u32 *addr = token;

	dw = bit >> 0x05;

	b1 = addr[dw];
	
	bit = ~bit;
	bit &= 0x1f;
	mask = htonl(1 << bit);
	return (b1 & mask);
}



/*
 *	find the first different bit between two addresses
 *	length of address must be a multiple of 32bits
 */

static __inline__ int addr_diff(void *token1, void *token2, int addrlen)
{
	__u32 *a1 = token1;
	__u32 *a2 = token2;
	int i;

	addrlen >>= 2;

	for (i = 0; i < addrlen; i++) {
		__u32 b1, b2;
		__u32 xb;

		b1 = a1[i];
		b2 = a2[i];

		xb = b1 ^ b2;

		if (xb) {
			int res = 0;
			int j=31;

			xb = ntohl(xb);

			while (test_bit(j, &xb) == 0) {
				res++;
				j--;
			}

			return (i * 32 + res);
		}
	}

	/*
	 *	we should *never* get to this point since that 
	 *	would mean the addrs are equal
	 */

	return -1;
}

static __inline__ struct fib6_node * node_alloc(void)
{
	struct fib6_node *fn;

	if ((fn = kmalloc(sizeof(struct fib6_node), GFP_ATOMIC))) {
		memset(fn, 0, sizeof(struct fib6_node));
		rt6_stats.fib_nodes++;
	}

	return fn;
}

static __inline__ void node_free(struct fib6_node * fn)
{
	rt6_stats.fib_nodes--;
	kfree(fn);
}

extern __inline__ void rt6_release(struct rt6_info *rt)
{
	struct dst_entry *dst = (struct dst_entry *) rt;
	if (atomic_dec_and_test(&dst->refcnt)) {
		rt->rt6i_node = NULL;
		dst_free(dst);
	}
}


/*
 *	Routing Table
 *
 *	return the apropriate node for a routing tree "add" operation
 *	by either creating and inserting or by returning an existing
 *	node.
 */

static struct fib6_node * fib6_add_1(struct fib6_node *root, void *addr,
				     int addrlen, int plen,
				     unsigned long offset,
				     struct rt6_info *rt)
				     
{
	struct fib6_node *fn;
	struct fib6_node *pn = NULL;
	struct fib6_node *in;
	struct fib6_node *ln;
	struct rt6key *key;
	__u32	bit;
	__u32	dir = 0;
	__u32	sernum = ++rt_sernum;

	/* insert node in tree */

	fn = root;

	if (plen == 0)
		return fn;

	for (;;) {
		if (fn == NULL) {
			ln = node_alloc();

			if (ln == NULL)
				return NULL;
			ln->fn_bit = plen;
			
			ln->parent = pn;
			ln->fn_sernum = sernum;
			rt->rt6i_node = ln;

			if (dir)
				pn->right = ln;
			else
				pn->left  = ln;

			return ln;
		}

		key = (struct rt6key *)((u8 *)fn->leaf + offset);

		/*
		 *	Prefix match
		 */
		if (addr_match(&key->addr, addr, fn->fn_bit)) {
		
			/*
			 *	Exact match ?
			 */
			 
			if (plen == fn->fn_bit) {
				/* clean up an intermediate node */
				if ((fn->fn_flags & RTN_RTINFO) == 0) {
					rt6_release(fn->leaf);
					fn->leaf = NULL;
				}
			
				fn->fn_sernum = sernum;
				
				return fn;
			}

			/*
			 *	We have more bits to go
			 */
			 
			if (plen > fn->fn_bit) {
				/* Walk down on tree. */
				fn->fn_sernum = sernum;
				dir = addr_bit_set(addr, fn->fn_bit);
				pn = fn;
				fn = dir ? fn->right: fn->left;

				/*
				 *	Round we go. Note if fn has become
				 *	NULL then dir is set and fn is handled
				 *	top of loop.
				 */
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
		bit = addr_diff(addr, &key->addr, addrlen);


		/* 
		 *		(intermediate)	
		 *	          /	   \
		 *	(new leaf node)    (old node)
		 */
		if (plen > bit) {
			in = node_alloc();
		
			if (in == NULL)
				return NULL;

			/* 
			 * new intermediate node. 
			 * RTN_RTINFO will
			 * be off since that an address that chooses one of
			 * the branches would not match less specific routes
			 * int the other branch
			 */

			in->fn_bit = bit;

			in->parent = pn;
			in->leaf = rt;

			in->fn_sernum = sernum;
			atomic_inc(&rt->rt6i_ref);

			/* leaf node */
			ln = node_alloc();

			if (ln == NULL) {
				node_free(in);
				return NULL;
			}

			/* update parent pointer */
			if (dir)
				pn->right = in;
			else
				pn->left  = in;

			ln->fn_bit = plen;

			ln->parent = in;
			fn->parent = in;

			ln->fn_sernum = sernum;

			if (addr_bit_set(addr, bit)) {
				in->right = ln;
				in->left  = fn;
			} else {
				in->left  = ln;
				in->right = fn;
			}

			return ln;
		}

		/* 
		 *		(new leaf node)
		 *	          /	   \
		 *	     (old node)    NULL
		 */

		ln = node_alloc();

		if (ln == NULL)
			return NULL;

		ln->fn_bit = plen;

		ln->parent = pn;

		ln->fn_sernum = sernum;
		
		if (dir)
			pn->right = ln;
		else
			pn->left  = ln;
		

		if (addr_bit_set(&key->addr, plen))
			ln->right = fn;
		else
			ln->left  = fn;

		fn->parent = ln;

		return ln;
	}

	return NULL;
}

/*
 *	Insert routing information in a node.
 */

static int fib6_add_rt2node(struct fib6_node *fn, struct rt6_info *rt)
{
	struct rt6_info *iter = NULL;
	struct rt6_info **ins;

	rt->rt6i_node = fn;
	ins = &fn->leaf;

	for (iter = fn->leaf; iter; iter=iter->u.next) {
		/*
		 *	Search for duplicates
		 */

		if (iter->rt6i_metric == rt->rt6i_metric) {
			/*
			 *	Same priority level
			 */

			if ((iter->rt6i_dev == rt->rt6i_dev) &&
			    (iter->rt6i_flowr == rt->rt6i_flowr) &&
			    (ipv6_addr_cmp(&iter->rt6i_gateway,
					   &rt->rt6i_gateway) == 0)) {
				if (!(iter->rt6i_flags&RTF_EXPIRES))
					return -EEXIST;
				iter->rt6i_expires = rt->rt6i_expires;
				if (!(rt->rt6i_flags&RTF_EXPIRES)) {
					iter->rt6i_flags &= ~RTF_EXPIRES;
					iter->rt6i_expires = rt->rt6i_expires;
				}
				return -EEXIST;
			}
		}

		if (iter->rt6i_metric > rt->rt6i_metric)
			break;

		ins = &iter->u.next;
	}

	/*
	 *	insert node
	 */

	*ins = rt;
	rt->u.next = iter;
	atomic_inc(&rt->rt6i_ref);
#ifdef CONFIG_RTNETLINK
	inet6_rt_notify(RTM_NEWROUTE, rt);
#endif
	rt6_stats.fib_rt_entries++;

	if ((fn->fn_flags & RTN_RTINFO) == 0) {
		rt6_stats.fib_route_nodes++;
		fn->fn_flags |= RTN_RTINFO;
	}

	return 0;
}

static __inline__ void fib6_start_gc(struct rt6_info *rt)
{
	if ((ip6_fib_timer.expires == 0) &&
	    (rt->rt6i_flags & (RTF_ADDRCONF | RTF_CACHE))) {
		del_timer(&ip6_fib_timer);
		ip6_fib_timer.expires = jiffies + ip6_rt_gc_interval;
		add_timer(&ip6_fib_timer);
	}
}

/*
 *	Add routing information to the routing tree.
 *	<destination addr>/<source addr>
 *	with source addr info in sub-trees
 */

int fib6_add(struct fib6_node *root, struct rt6_info *rt)
{
	struct fib6_node *fn;
	int err = -ENOMEM;
	unsigned long offset;
	
	offset = (u8*) &rt->rt6i_dst - (u8*) rt;
	fn = fib6_add_1(root, &rt->rt6i_dst.addr, sizeof(struct in6_addr),
			rt->rt6i_dst.plen, offset, rt);

	if (fn == NULL) {
#if RT_DEBUG >= 2
		printk(KERN_DEBUG "fib6_add: fn == NULL\n");
#endif
		goto out;
	}

	if (rt->rt6i_src.plen) {
		struct fib6_node *sn;

#if RT_DEBUG >= 2
		printk(KERN_DEBUG "fib6_add: src.len > 0\n");
#endif

		if (fn->subtree == NULL) {
			struct fib6_node *sfn;

			if (fn->leaf == NULL) {
				fn->leaf = rt;
				atomic_inc(&rt->rt6i_ref);
			}

			sfn = node_alloc();

			if (sfn == NULL)
				goto out;

			sfn->parent = fn;
			sfn->leaf = &ip6_null_entry;
			sfn->fn_flags = RTN_ROOT;
			sfn->fn_sernum = ++rt_sernum;

			fn->subtree = sfn;
		}

		offset = (u8*) &rt->rt6i_src - (u8*) rt;

		sn = fib6_add_1(fn->subtree, &rt->rt6i_src.addr,
				sizeof(struct in6_addr), rt->rt6i_src.plen,
				offset, rt);

		if (sn == NULL)
			goto out;

		fn = sn;
	}

	err = fib6_add_rt2node(fn, rt);

	if (err == 0)
		fib6_start_gc(rt);
out:
	if (err)
		dst_free(&rt->u.dst);
	return err;
}

/*
 *	Routing tree lookup
 *
 */

struct lookup_args {
	unsigned long	offset;		/* key offset on rt6_info	*/
	struct in6_addr	*addr;		/* search key			*/
};

static struct fib6_node * fib6_lookup_1(struct fib6_node *root,
					struct lookup_args *args)
{
	struct fib6_node *fn;
	int dir;

	/*
	 *	Descend on a tree
	 */

	fn = root;

	for (;;) {
		struct fib6_node *next;

		dir = addr_bit_set(args->addr, fn->fn_bit);

		next = dir ? fn->right : fn->left;

		if (next) {
			fn = next;
			continue;
		}

		break;
	}

	while ((fn->fn_flags & RTN_ROOT) == 0) {
		if (fn->subtree) {
			struct fib6_node *st;
			struct lookup_args *narg;

			narg = args + 1;

			if (narg->addr) {
				st = fib6_lookup_1(fn->subtree, narg);

				if (!(st->fn_flags & RTN_ROOT))
				{
					return st;
				}
			}
		}

		if (fn->fn_flags & RTN_RTINFO) {
			struct rt6key *key;

			key = (struct rt6key *) ((u8 *) fn->leaf +
						 args->offset);

			if (addr_match(&key->addr, args->addr, key->plen))
				return fn;
		}

		fn = fn->parent;
	}

	return NULL;
}

struct fib6_node * fib6_lookup(struct fib6_node *root, struct in6_addr *daddr,
			       struct in6_addr *saddr)
{
	struct lookup_args args[2];
	struct rt6_info *rt = NULL;
	struct fib6_node *fn;

	args[0].offset = (u8*) &rt->rt6i_dst - (u8*) rt;
	args[0].addr = daddr;

	args[1].offset = (u8*) &rt->rt6i_src - (u8*) rt;
	args[1].addr = saddr;

	fn = fib6_lookup_1(root, args);

	if (fn == NULL)
		fn = root;

	return fn;
}

/*
 *	Deletion
 *
 */

static struct rt6_info * fib6_find_prefix(struct fib6_node *fn)
{
	while(fn) {
		if(fn->left)
			return fn->left->leaf;

		if(fn->right)
			return fn->right->leaf;

		fn = fn->subtree;
	}
	return NULL;
}

/*
 *	Called to trim the tree of intermediate nodes when possible. "fn"
 *	is the node we want to try and remove.
 */

static void fib6_del_2(struct fib6_node *fn)
{
	struct rt6_info *rt;

	fn->fn_flags &= ~RTN_RTINFO;
	rt6_stats.fib_route_nodes--;

	/*
	 *	Can't delete a root node
	 */
	 
	if (fn->fn_flags & RTN_TL_ROOT)
		return;

	do {
		struct fib6_node *pn, *child;
		int children = 0;

		child = NULL;

		/*
		 *	We have a child to left
		 */
		 
		if (fn->left) {
			children++;
			child = fn->left;
		}

		/*
		 *	To right
		 */
		 
		if (fn->right) {
			children++;
			child = fn->right;
		}

		/*
		 *	We can't tidy a case of two children.
		 */
		if (children > 1) {
			if (fn->leaf == NULL)
				goto split_repair;
			break;
		}

		if (fn->fn_flags & RTN_RTINFO)
			break;

		/*
		 *	The node we plan to tidy has an stree. Talk about
		 *	making life hard.
		 */
		 
		if (fn->subtree)
			goto stree_node;

		/*
		 *	Up we go
		 */
		 
		pn = fn->parent;

		/*
		 *	Not a ROOT - we can tidy
		 */
		 
		if ((fn->fn_flags & RTN_ROOT) == 0) {
			/*
			 *	Make our child our parents child
			 */
			if (pn->left == fn)
				pn->left = child;
			else
				pn->right = child;

			/*
			 *	Reparent the child
			 */
			if (child)
				child->parent = pn;

			/*
			 *	Discard leaf entries
			 */
			if (fn->leaf)
				rt6_release(fn->leaf);
		} else {
			if (children)
				break;
			/*
			 *	No children so no subtree
			 */

			pn->subtree = NULL;
		}

		/*
		 *	We are discarding 
		 */
		node_free(fn);
		
		/*
		 *	Our merge of entries might propogate further
		 *	up the tree, so move up a level and retry.
		 */
		 
		fn = pn;

	} while (!(fn->fn_flags & RTN_TL_ROOT));

	return;

stree_node:

	rt6_release(fn->leaf);

split_repair:
	rt = fib6_find_prefix(fn);

	if (rt == NULL)
		panic("fib6_del_2: inconsistent tree\n");

	atomic_inc(&rt->rt6i_ref);
	fn->leaf = rt;
}

/*
 *	Remove our entry in the tree. This throws away the route entry
 *	from the list of entries attached to this fib node. It doesn't
 *	expunge from the tree.
 */

static struct fib6_node * fib6_del_1(struct rt6_info *rt)
{
	struct fib6_node *fn;
	
	fn = rt->rt6i_node;

	/* We need a fib node! */
	if (fn) {
		struct rt6_info **back;
		struct rt6_info *lf;

		back = &fn->leaf;
		
		/*
		 *	Walk the leaf entries looking for ourself
		 */
		 
		for(lf = fn->leaf; lf; lf=lf->u.next) {
			if (rt == lf) {
				/*
				 *	Delete this entry.
				 */

				*back = lf->u.next;
#ifdef CONFIG_RTNETLINK
				inet6_rt_notify(RTM_DELROUTE, lf);
#endif			
				rt6_release(lf);
				rt6_stats.fib_rt_entries--;
				return fn;
			}
			back = &lf->u.next;
		}
	}

	return NULL;
}

int fib6_del(struct rt6_info *rt)
{
	struct fib6_node *fn;

	fn = fib6_del_1(rt);

	if (fn == NULL)
		return -ENOENT;

	if (fn->leaf == NULL)
		fib6_del_2(fn);

	return 0;
}

/*
 *	Tree transversal function
 *
 *	Wau... It is NOT REENTERABLE!!!!!!! It is cathastrophe. --ANK
 */

int fib6_walk_count;

void fib6_walk_tree(struct fib6_node *root, f_pnode func, void *arg,
		    int filter)
{
	struct fib6_node *fn;

	fn = root;

	fib6_walk_count++;
	
	do {
		if (!(fn->fn_flags & RTN_TAG)) {
			fn->fn_flags |= RTN_TAG;
			
			if (fn->left) {
				fn = fn->left;
				continue;
			}
		}

		fn->fn_flags &= ~RTN_TAG;

		if (fn->right) {
			fn = fn->right;
			continue;
		}
		
		do {
			struct fib6_node *node;
			
			if (fn->fn_flags & RTN_ROOT)
				break;
			node = fn;
			fn = fn->parent;
			
			if (!(node->fn_flags & RTN_TAG)) {
				if (node->subtree) {
					fib6_walk_tree(node->subtree, func,
						       arg, filter);
				}

				if (!filter ||
				    (node->fn_flags & RTN_RTINFO))
					(*func)(node, arg);
			}
			
		} while (!(fn->fn_flags & RTN_TAG));

	} while (!(fn->fn_flags & RTN_ROOT) || (fn->fn_flags & RTN_TAG));

	fib6_walk_count--;
}

/*
 *	Garbage collection
 */

static int fib6_gc_node(struct fib6_node *fn, int timeout)
{
	struct rt6_info *rt, **back;
	int more = 0;
	unsigned long now = jiffies;

	back = &fn->leaf;

	for (rt = fn->leaf; rt;) {
		if ((rt->rt6i_flags & RTF_CACHE) && atomic_read(&rt->rt6i_use) == 0) {
			if ((long)(now - rt->rt6i_tstamp) >= timeout) {
				struct rt6_info *old;

				old = rt;

				rt = rt->u.next;

				*back = rt;

				old->rt6i_node = NULL;
#ifdef CONFIG_RTNETLINK
				inet6_rt_notify(RTM_DELROUTE, old);
#endif
				old->u.dst.obsolete = 1;
				rt6_release(old);
				rt6_stats.fib_rt_entries--;
				continue;
			}
			more++;
		}

		/*
		 *	check addrconf expiration here.
		 *
		 *	BUGGGG Crossing fingers and ...
		 *	Seems, radix tree walking is absolutely broken,
		 *	but we will try in any case --ANK
		 */
		if ((rt->rt6i_flags&RTF_EXPIRES) && rt->rt6i_expires
		    && (long)(now - rt->rt6i_expires) > 0) {
			struct rt6_info *old;

			old = rt;
			rt = rt->u.next;

			*back = rt;

			old->rt6i_node = NULL;
#ifdef CONFIG_RTNETLINK
			inet6_rt_notify(RTM_DELROUTE, old);
#endif
			old->u.dst.obsolete = 1;
			rt6_release(old);
			rt6_stats.fib_rt_entries--;
			continue;
		}
		back = &rt->u.next;
		rt = rt->u.next;
	}

	return more;
}

struct fib6_gc_args {
	unsigned long	timeout;
	int		more;
};

static void fib6_garbage_collect(struct fib6_node *fn, void *p_arg)
{
	struct fib6_gc_args * args = (struct fib6_gc_args *) p_arg;

	if (fn->fn_flags & RTN_RTINFO) {
		int more;

		more = fib6_gc_node(fn, args->timeout);

		if (fn->leaf) {
			args->more += more;
			return;
		}

		rt6_stats.fib_route_nodes--;
		fn->fn_flags &= ~RTN_RTINFO;
	}

	/*
	 *	tree nodes (with no routing information)
	 */

	if (!fn->subtree && !(fn->fn_flags & RTN_TL_ROOT)) {
		int children = 0;
		struct fib6_node *chld = NULL;

		if (fn->left) {
			children++;
			chld = fn->left;
		}
			
		if (fn->right) {
			children++;
			chld = fn->right;
		}
		
		if ((fn->fn_flags & RTN_ROOT)) {
			if (children == 0) {
				struct fib6_node *pn;

				pn = fn->parent;
				pn->subtree = NULL;

				node_free(fn);
			}
			return;
		}

		if (children <= 1) {
			struct fib6_node *pn = fn->parent;
			
			if (pn->left == fn)
				pn->left = chld;
			else
				pn->right = chld;
			
			if (chld)
				chld->parent = pn;
			
			if (fn->leaf)
				rt6_release(fn->leaf);

			node_free(fn);

			return;
		}
	}

	if (fn->leaf == NULL) {
		struct rt6_info *nrt;
		
		nrt = fib6_find_prefix(fn);

		if (nrt == NULL)
			panic("fib6: inconsistent tree\n");

		atomic_inc(&nrt->rt6i_ref);
		fn->leaf = nrt;
	}
}

void fib6_run_gc(unsigned long dummy)
{
	struct fib6_gc_args arg = {
		ip6_rt_gc_timeout,
		0
	};

	del_timer(&ip6_fib_timer);

	if (dummy)
		arg.timeout = dummy;

	if (fib6_walk_count == 0)
		fib6_walk_tree(&ip6_routing_table, fib6_garbage_collect, &arg, 0);
	else
		arg.more = 1;

	if (arg.more) {
		ip6_fib_timer.expires = jiffies + ip6_rt_gc_interval;
		add_timer(&ip6_fib_timer);
	} else {
		ip6_fib_timer.expires = 0;
	}
}

#ifdef MODULE
void fib6_gc_cleanup(void)
{
	del_timer(&ip6_fib_timer);
}
#endif
