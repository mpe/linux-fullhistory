#ifndef _NET_NEIGHBOUR_H
#define _NET_NEIGHBOUR_H

/*
 *	Generic neighbour manipulation
 *
 *	authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 */

#ifdef __KERNEL__

#include <asm/atomic.h>
#include <linux/skbuff.h>

/*
 *	flags
 *
 */
#define NTF_COMPLETE	0x02
#define NTF_PERMANENT	0x04

struct neighbour {
	struct neighbour	*next;
	struct neighbour	*prev;
	struct neigh_table	*tbl;
	struct device		*dev;
	unsigned long		lastused;
	unsigned long		flags;
	unsigned char		ha[MAX_ADDR_LEN];
	struct hh_cache		*hh;
	atomic_t		refcnt;
	struct neigh_ops	*ops;
	struct sk_buff_head	arp_queue;
	char			primary_key[0];
};

struct neigh_ops {
	int			family;
	unsigned int		(*hash)(void *primary_key);
	int			(*resolve)(unsigned char *h_dest,
					   struct sk_buff *skb);
	void			(*destructor)(struct neighbour *);
};

extern struct neighbour		*neigh_alloc(int size, struct neigh_ops *);

/*
 *	Neighbour references
 *
 *	When neighbour pointers are passed to "client" code the
 *	reference count is increased. The count is 0 if the node
 *	is only referenced by the corresponding table.
 *
 *	Nodes cannot be unlinked from the table if their
 *	reference count != 0.
 *
 *	i.e. you can't reclaim a neighbour if it is being used by a
 *	dst_cache or routing entry - hopefully those will react
 *	to memory shortage and GC their unused entries
 */


static __inline__ void neigh_release(struct neighbour *neigh)
{
	if (atomic_dec_and_test(&neigh->refcnt))
		neigh->lastused = jiffies;
}

static __inline__ struct neighbour * neighbour_clone(struct neighbour *neigh)
{
	if (neigh)
		atomic_inc(&neigh->refcnt);
	return neigh;
}

#define NT_MASK_QUEUE	0x01
#define NT_MASK_GC	0x02

/*
 *	neighbour table manipulation
 */

struct neigh_table {
	int			tbl_size;	/* num. of hash	buckets	*/
	int			tbl_entries;	/* entry count		*/
	struct neighbour	**hash_buckets;
	atomic_t		tbl_lock;
	unsigned int		tbl_bh_mask;	/* bh mask		*/
	struct neigh_ops	*neigh_ops;
	struct neighbour	*request_queue; /* pending inserts	*/
};

extern void			neigh_table_init(struct neigh_table *tbl,
						 struct neigh_ops *ops,
						 int size);
extern void			neigh_table_destroy(struct neigh_table *tbl);

extern void			neigh_table_run_bh(struct neigh_table *tbl);

extern void			neigh_table_ins(struct neigh_table *tbl,
						struct neighbour *neigh);

extern void			neigh_queue_ins(struct neigh_table *tbl,
						struct neighbour *neigh);

extern void			neigh_unlink(struct neighbour *neigh);

extern struct neighbour *	neigh_lookup(struct neigh_table *tbl,
					     void *pkey, int key_len,
					     struct device *dev);

extern void			neigh_destroy(struct neighbour *neigh);

static __inline__ void neigh_insert(struct neigh_table *tbl,
				    struct neighbour *neigh)
{
	start_bh_atomic();
	if (tbl->tbl_lock == 1)
	{
		neigh_table_ins(tbl, neigh);
	}
	else
	{
		tbl->tbl_bh_mask |= NT_MASK_QUEUE;
		neigh_queue_ins(tbl, neigh);
	}
	end_bh_atomic();	
}



typedef int (*ntbl_examine_t) (struct neighbour *neigh, void *arg);

/*
 *	examine every element of a neighbour table.
 *	For every neighbour the callback function will be called.
 *
 *	parameters:
 *		max	:	max bucket index (<= tbl_size, 0 all)
 *		filter	:	(neigh->flags & (~filter)) -> call func
 *		args	:	opaque pointer
 *
 *	return values
 *		0		nop
 *		!0		unlink node from table and destroy it
 */

extern void			ntbl_walk_table(struct neigh_table *tbl,
						ntbl_examine_t func,
						unsigned long filter,
						int max, void *args);

static __inline__ void neigh_table_lock(struct neigh_table *tbl)
{
	atomic_inc(&tbl->tbl_lock);
}

extern void			neigh_tbl_run_bh(struct neigh_table *tbl);

static __inline__ void neigh_table_unlock(struct neigh_table *tbl)
{
	start_bh_atomic();
	if (atomic_dec_and_test(&tbl->tbl_lock) && tbl->tbl_bh_mask)
	{
		neigh_tbl_run_bh(tbl);
	}
	end_bh_atomic();
}

#endif
#endif


