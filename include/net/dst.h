/*
 * net/dst.h	Protocol independent destination cache definitions.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 */

#ifndef _NET_DST_H
#define _NET_DST_H

#include <net/neighbour.h>

/*
 * 0 - no debugging messages
 * 1 - rare events and bugs (default)
 * 2 - trace mode.
 */
#ifdef  NO_ANK_FIX
#define RT_CACHE_DEBUG		0
#else
#define RT_CACHE_DEBUG		1
#endif

#define DST_GC_MIN	(1*HZ)
#define DST_GC_INC	(5*HZ)
#define DST_GC_MAX	(120*HZ)

struct sk_buff;

struct dst_entry
{
	struct dst_entry        *next;
	atomic_t		refcnt;		/* tree/hash references	*/
	atomic_t		use;		/* client references	*/
	struct device	        *dev;
	char			obsolete;
	char			priority;
	char			__pad1, __pad2;
	unsigned long		lastuse;
	unsigned		window;
	unsigned		pmtu;
	unsigned		rtt;
	unsigned long		rate_last;	/* rate limiting for ICMP */
	unsigned long		rate_tokens;

	int			error;

	struct neighbour	*neighbour;
	struct hh_cache		*hh;

	int			(*input)(struct sk_buff*);
	int			(*output)(struct sk_buff*);

	struct  dst_ops	        *ops;
		
	char			info[0];
};


struct dst_ops
{
	unsigned short		family;
	struct dst_entry *	(*check)(struct dst_entry *, __u32 cookie);
	struct dst_entry *	(*reroute)(struct dst_entry *,
					   struct sk_buff *);
	void			(*destroy)(struct dst_entry *);
};

#ifdef __KERNEL__

extern struct dst_entry * dst_garbage_list;
extern atomic_t	dst_total;

static __inline__
struct dst_entry * dst_clone(struct dst_entry * dst)
{
	if (dst)
		atomic_inc(&dst->use);
	return dst;
}

static __inline__
void dst_release(struct dst_entry * dst)
{
	if (dst)
		atomic_dec(&dst->use);
}

static __inline__
struct dst_entry * dst_check(struct dst_entry ** dst_p, u32 cookie)
{
	struct dst_entry * dst = *dst_p;
	if (dst && dst->obsolete)
		dst = dst->ops->check(dst, cookie);
	return (*dst_p = dst);
}

static __inline__
struct dst_entry * dst_reroute(struct dst_entry ** dst_p, struct sk_buff *skb)
{
	struct dst_entry * dst = *dst_p;
	if (dst && dst->obsolete)
		dst = dst->ops->reroute(dst, skb);
	return (*dst_p = dst);
}

static __inline__
void dst_destroy(struct dst_entry * dst)
{
	if (dst->neighbour)
		neigh_release(dst->neighbour);
	if (dst->ops->destroy)
		dst->ops->destroy(dst);
	kfree(dst);
	atomic_dec(&dst_total);
}

extern void * dst_alloc(int size, struct dst_ops * ops);
extern void __dst_free(struct dst_entry * dst);

static __inline__
void dst_free(struct dst_entry * dst)
{
	if (!atomic_read(&dst->use)) {
		dst_destroy(dst);
		return;
	}
	__dst_free(dst);
}
#endif

#endif /* _NET_DST_H */
