/*
 * net/sched/sch_sfq.c	Stochastic Fairness Queueing scheduler.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/pkt_sched.h>


/*	Stochastic Fairness Queuing algorithm.
	=======================================

	Source:
	Paul E. McKenney "Stochastic Fairness Queuing",
	IEEE INFOCOMM'90 Proceedings, San Francisco, 1990.

	Paul E. McKenney "Stochastic Fairness Queuing",
	"Interworking: Research and Experience", v.2, 1991, p.113-131.


	See also:
	M. Shreedhar and George Varghese "Efficient Fair
	Queuing using Deficit Round Robin", Proc. SIGCOMM 95.


	It is not the thing that usually called (W)FQ nowadays. It does not
	use any timestamp mechanism, but instead processes queues
	in round-robin order.

	ADVANTAGE:

	- It is very cheap. Both CPU and memory requirements are minimal.

	DRAWBACKS:

	- "Stochastic" -> It is not 100% fair. 
	When hash collisions occur, several flows are considred as one.

	- "Round-robin" -> It introduces larger delays than virtual clock
	based schemes, and should not be used for isolation interactive
	traffic	from non-interactive. It means, that this scheduler
	should be used as leaf of CBQ or P3, which put interactive traffic
	to higher priority band.

	We still need true WFQ for top level CSZ, but using WFQ
	for the best effort traffic is absolutely pointless:
	SFQ is superior for this purpose.

	IMPLEMENTATION:
	This implementation limits maximal queue length to 128;
	maximal mtu to 2^15-1; number of hash buckets to 1024.
	The only goal of this restrictions was that all data
	fitted to one 4K page :-). Struct sfq_sched_data is
	organized in anti-cache manner: all the data for bucket
	scattered over different locations. It is not good,
	but it allowed to put it into 4K.

	It is easy to increase these values.
*/

#define SFQ_DEPTH		128
#define SFQ_HASH_DIVISOR	1024

#define SFQ_HASH(a)		0

/* This type should contain at least SFQ_DEPTH*2 values */
typedef unsigned char sfq_index;

struct sfq_head
{
	sfq_index	next;
	sfq_index	prev;
};

struct sfq_sched_data
{
/* Parameters */
	unsigned	quantum;	/* Allotment per round: MUST BE >= MTU */

/* Variables */
	sfq_index	tail;		/* Index of current slot in round */
	sfq_index	max_depth;	/* Maximal depth */

	sfq_index	ht[SFQ_HASH_DIVISOR];	/* Hash table */
	sfq_index	next[SFQ_DEPTH];	/* Active slots link */
	short		allot[SFQ_DEPTH];	/* Current allotment per slot */
	unsigned short	hash[SFQ_DEPTH];	/* Hash value indexed by slots */
	struct sk_buff_head	qs[SFQ_DEPTH];		/* Slot queue */
	struct sfq_head	dep[SFQ_DEPTH*2];	/* Linked list of slots, indexed by depth */
};

extern __inline__ void sfq_link(struct sfq_sched_data *q, sfq_index x)
{
	sfq_index p, n;
	int d = q->qs[x].qlen;

	p = d;
	n = q->dep[d].next;
	q->dep[x].next = n;
	q->dep[x].prev = p;
	q->dep[p].next = q->dep[n].prev = x;
}

extern __inline__ void sfq_dec(struct sfq_sched_data *q, sfq_index x)
{
	sfq_index p, n;

	n = q->dep[x].next;
	p = q->dep[x].prev;
	q->dep[p].next = n;
	q->dep[n].prev = p;

	if (n == p && q->max_depth == q->qs[x].qlen + 1)
		q->max_depth--;

	sfq_link(q, x);
}

extern __inline__ void sfq_inc(struct sfq_sched_data *q, sfq_index x)
{
	sfq_index p, n;
	int d;

	n = q->dep[x].next;
	p = q->dep[x].prev;
	q->dep[p].next = n;
	q->dep[n].prev = p;
	d = q->qs[x].qlen;
	if (q->max_depth < d)
		q->max_depth = d;

	sfq_link(q, x);
}

static __inline__ void sfq_drop(struct sfq_sched_data *q)
{
	struct sk_buff *skb;
	sfq_index d = q->max_depth;

	/* Queue is full! Find the longest slot and
	   drop a packet from it */

	if (d != 1) {
		sfq_index x = q->dep[d].next;
		skb = q->qs[x].prev;
		__skb_unlink(skb, &q->qs[x]);
		kfree_skb(skb, FREE_WRITE);
		sfq_dec(q, x);
/*
		sch->q.qlen--;
 */
		return;
	}

	/* It is difficult to believe, but ALL THE SLOTS HAVE LENGTH 1. */

	d = q->next[q->tail];
	q->next[q->tail] = q->next[d];
	q->allot[q->next[d]] += q->quantum;
	skb = q->qs[d].prev;
	__skb_unlink(skb, &q->qs[d]);
	kfree_skb(skb, FREE_WRITE);
	sfq_dec(q, d);
/*
	sch->q.qlen--;
 */
	q->ht[q->hash[d]] = SFQ_DEPTH;
	return;
}

static int
sfq_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct sfq_sched_data *q = (struct sfq_sched_data *)sch->data;
	unsigned hash = SFQ_HASH(skb);
	sfq_index x;

	x = q->ht[hash];
	if (x == SFQ_DEPTH) {
		q->ht[hash] = x = q->dep[SFQ_DEPTH].next;
		q->hash[x] = hash;
	}
	__skb_queue_tail(&q->qs[x], skb);
	sfq_inc(q, x);
	if (q->qs[x].qlen == 1) {		/* The flow is new */
		if (q->tail == SFQ_DEPTH) {	/* It is the first flow */
			q->tail = x;
			q->next[x] = x;
			q->allot[x] = q->quantum;
		} else {
			q->next[x] = q->next[q->tail];
			q->next[q->tail] = x;
			q->tail = x;
		}
	}
	if (++sch->q.qlen < SFQ_DEPTH-1)
		return 1;

	sfq_drop(q);
	return 0;
}

static struct sk_buff *
sfq_dequeue(struct Qdisc* sch)
{
	struct sfq_sched_data *q = (struct sfq_sched_data *)sch->data;
	struct sk_buff *skb;
	sfq_index a, old_a;

	/* No active slots */
	if (q->tail == SFQ_DEPTH)
		return NULL;

	a = old_a = q->next[q->tail];

	/* Grab packet */
	skb = __skb_dequeue(&q->qs[a]);
	sfq_dec(q, a);
	sch->q.qlen--;

	/* Is the slot empty? */
	if (q->qs[a].qlen == 0) {
		a = q->next[a];
		if (a == old_a) {
			q->tail = SFQ_DEPTH;
			return skb;
		}
		q->next[q->tail] = a;
		q->allot[a] += q->quantum;
	} else if ((q->allot[a] -= skb->len) <= 0) {
		q->tail = a;
		a = q->next[a];
		q->allot[a] += q->quantum;
	}
	return skb;
}

static void
sfq_reset(struct Qdisc* sch)
{
	struct sk_buff *skb;

	while ((skb = sfq_dequeue(sch)) != NULL)
		kfree_skb(skb, FREE_WRITE);
}


static int sfq_open(struct Qdisc *sch, void *arg)
{
	struct sfq_sched_data *q;
	int i;

	q = (struct sfq_sched_data *)sch->data;

	for (i=0; i<SFQ_HASH_DIVISOR; i++)
		q->ht[i] = SFQ_DEPTH;
	for (i=0; i<SFQ_DEPTH; i++) {
		skb_queue_head_init(&q->qs[i]);
		q->dep[i+SFQ_DEPTH].next = i+SFQ_DEPTH;
		q->dep[i+SFQ_DEPTH].prev = i+SFQ_DEPTH;
	}
	q->max_depth = 0;
	q->tail = SFQ_DEPTH;
	q->quantum = sch->dev->mtu;
	if (sch->dev->hard_header)
		q->quantum += sch->dev->hard_header_len;
	for (i=0; i<SFQ_DEPTH; i++)
		sfq_link(q, i);
	return 0;
}


struct Qdisc_ops sfq_ops =
{
	NULL,
	"sfq",
	0,
	sizeof(struct sfq_sched_data),
	sfq_enqueue,
	sfq_dequeue,
	sfq_reset,
	NULL,
	sfq_open,
};

#ifdef MODULE
int init_module(void)
{
	int err;

	/* Load once and never free it. */
	MOD_INC_USE_COUNT;

	err = register_qdisc(&sfq_ops);
	if (err)
		MOD_DEC_USE_COUNT;
	return err;
}

void cleanup_module(void) 
{
}
#endif
