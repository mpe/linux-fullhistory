/*
 *	Generic address resultion entity
 *
 *	Authors:
 *	Pedro Roque	<roque@di.fc.ul.pt>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/socket.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#include <net/neighbour.h>


static void neigh_purge_send_q(struct neighbour *neigh);

void neigh_table_init(struct neigh_table *tbl, struct neigh_ops *ops, int size)
{
	int bmemlen;

	memset(tbl, 0, sizeof(struct neigh_table));
	
	tbl->tbl_size = size;
	tbl->neigh_ops = ops;
	
	/*
	 *	This should only be called on initialization
	 *	And interrupts should be on
	 */

	bmemlen = size * sizeof(struct neighbour *);
	tbl->hash_buckets = kmalloc(bmemlen, GFP_KERNEL);

	if (tbl->hash_buckets == NULL)
	{
		panic("unable to initialize neigh_table");
	}

	memset(tbl->hash_buckets, 0, bmemlen);
}

struct neighbour *neigh_alloc(int size, struct neigh_ops *ops)
{
	struct neighbour *neigh;
	
	neigh = kmalloc(size, GFP_ATOMIC);
	if (neigh == NULL)
	{
		return NULL;
	}

	memset(neigh, 0, size);
	
	skb_queue_head_init(&neigh->arp_queue);
	neigh->ops = ops;
	return neigh;
}

void neigh_queue_ins(struct neigh_table *tbl, struct neighbour *neigh)
{
	struct neighbour *entry, **head;
	entry = tbl->request_queue;

	head = &tbl->request_queue;
	
	for (; entry; entry = entry->next)
	{
		head = &entry->next;
	}

	*head = neigh;
	neigh->next = neigh->prev = NULL;
}

static struct neighbour *neigh_dequeue(struct neigh_table *tbl)
{
	struct neighbour *neigh;

	if ((neigh = tbl->request_queue))
	{
		tbl->request_queue = neigh->next;
	}
	return neigh;
}

void neigh_table_ins(struct neigh_table *tbl, struct neighbour *neigh)
{
        unsigned int hash_val;
	struct neighbour **head;
	
	hash_val = tbl->neigh_ops->hash(neigh->primary_key) % tbl->tbl_size;
	
	neigh->tbl = tbl;
	
	head = &tbl->hash_buckets[hash_val];
	
	if (!(*head))
	{
		neigh->next = neigh;
		neigh->prev = neigh;
	}
	else
	{
		struct neighbour *prev;
		struct neighbour *next;
	
		next = *head;
		prev = next->prev;
		

		neigh->next = next;
		neigh->prev = prev;
		next->prev = neigh;
		prev->next = neigh;
	}
	
	*head = neigh;
}

struct neighbour * neigh_lookup(struct neigh_table *tbl, void *pkey,
				int key_len, struct device *dev)
{
	struct neighbour *neigh, *head;
	unsigned int hash_val;
	
	hash_val = tbl->neigh_ops->hash(pkey) % tbl->tbl_size;
	head = tbl->hash_buckets[hash_val];

	neigh = head;

	if (neigh)
	{
		do {
			if (memcmp(neigh->primary_key, pkey, key_len) == 0)
			{
				if (!dev || dev == neigh->dev)
					return neigh;
			}
			neigh = neigh->next;

		} while (neigh != head);
	}

	return NULL;
}

/*
 *	neighbour must already be out of the table;
 *
 */
void neigh_destroy(struct neighbour *neigh)
{	
	if (neigh->tbl)
	{
		printk(KERN_DEBUG "neigh_destroy: neighbour still in table. "
		       "called from %p\n", __builtin_return_address(0));
	}

	if (neigh->ops->destructor)
	{
		(neigh->ops->destructor)(neigh);
	}

	neigh_purge_send_q(neigh);

	kfree(neigh);
}

void neigh_unlink(struct neighbour *neigh)
{
	struct neigh_table *tbl;
	struct neighbour **head;
	unsigned int hash_val;
	struct neighbour *next, *prev;
	
	tbl = neigh->tbl;
	neigh->tbl = NULL;

	hash_val = neigh->ops->hash(neigh->primary_key) % tbl->tbl_size;

	head = &tbl->hash_buckets[hash_val];
	tbl->tbl_entries--;

	next = neigh->next;
	if (neigh == (*head))
	{
		if (next == neigh)
		{
			*head = NULL;
			goto out;
		}
		*head = next;
	}
	
	prev = neigh->prev;
	next->prev = prev;
	prev->next = next;
  out:	
	neigh->next = neigh->prev = NULL;
}

/*
 *	Must only be called with an exclusive lock and bh disabled
 *
 */

void ntbl_walk_table(struct neigh_table *tbl, ntbl_examine_t func,
		     unsigned long filter, int max, void *args)
{
	int i;

	if (max == 0)
		max = tbl->tbl_size;

	for (i=0; i < max; i++)
	{
		struct neighbour **head;
		struct neighbour *entry;

		head = &tbl->hash_buckets[i];
		entry = *head;

		if (!entry)
			continue;

		do {
			if (entry->flags & (~filter))
			{
				int ret;
				ret = (*func)(entry, args);

				if (ret)
				{
					struct neighbour *curp;

					curp = entry;
					entry = curp->next;

					neigh_unlink(curp);
					neigh_destroy(curp);

					if ((*head) == NULL)
						break;
					continue;
				}
			}
			entry = entry->next;

		} while (entry != *head);
	}
}

void neigh_tbl_run_bh(struct neigh_table *tbl)
{       
	if ((tbl->tbl_bh_mask & NT_MASK_QUEUE))
	{
		struct neighbour *neigh;

		while((neigh = neigh_dequeue(tbl)))
		{
			neigh_table_ins(tbl, neigh);
		}
		tbl->tbl_bh_mask &= ~NT_MASK_QUEUE;
	}
}

/*
 * Purge all linked skb's of the entry.
 */

static void neigh_purge_send_q(struct neighbour *neigh)
{
	struct sk_buff *skb;

	/* Release the list of `skb' pointers. */
	while ((skb = skb_dequeue(&neigh->arp_queue)))
	{
		dev_kfree_skb(skb, FREE_WRITE);
	}
	return;
}
