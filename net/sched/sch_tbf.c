/*
 * net/sched/sch_tbf.c	Token Bucket Filter.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
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
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/pkt_sched.h>


/*	Simple Token Bucket Filter.
	=======================================

	SOURCE.

	None.

	ALGORITHM.

	Sequence of packets satisfy token bucket filter with
	rate $r$ and depth $b$, if all the numbers defined by:
	\begin{eqnarray*}
	n_0 &=& b, \\
	n_i &=& {\rm max} ( b, n_{i-1} + r*(t_i-t_{i-1}) - L_i ),
	\end{eqnarray*}
	where $t_i$ --- departure time of $i$-th packet and
	$L_i$ -- its length, never less than zero.

	It is convenient to rescale $n_i$ by factor $r$, so
	that the sequence has "canonical" form:
	\[
	n_0 = b/r,
	n_i = max { b/r, n_{i-1} + t_i - t_{i-1} - L_i/r },
	\]

	If a packet has n_i < 0, we throttle filter
	by $-n_i$ usecs.

	NOTES.

	If TBF throttles, it starts watchdog timer, which will wake up it
	after 0...10 msec.
	If no new packets will arrive during this period,
	or device will not be awaken by EOI for previous packet,
	tbf could stop its activity for 10 msec.

	It means that tbf will sometimes introduce pathological
	10msec delays to flow corresponding to rate*10msec bytes.
	For 10Mbit/sec flow it is about 12Kb, on 100Mbit/sec -- ~100Kb.
	This number puts lower reasonbale bound on token bucket depth,
	but even if depth is larger traffic is erratic at large rates.

	This problem is not specific for THIS implementation. Really,
	there exists statement that any attempt to shape traffic
	in transit will increase delays and jitter much more than
	we expected naively.

	Particularily, it means that delay/jitter sensitive traffic
	MUST NOT be shaped. Cf. CBQ (wrong) and CSZ (correct) approaches.
*/

struct tbf_sched_data
{
/* Parameters */
	int		cell_log;	/* 1<<cell_log is quantum of packet size */
	unsigned long	L_tab[256];	/* Lookup table for L/B values */
	unsigned long	depth;		/* Token bucket depth/B: MUST BE >= MTU/B */
	unsigned long	max_bytes;	/* Maximal length of backlog: bytes */

/* Variables */
	unsigned long	bytes;		/* Current length of backlog */
	unsigned long	tokens;		/* Current number of tokens */
	psched_time_t	t_c;		/* Time check-point */
	struct timer_list wd_timer;	/* Watchdog timer */
};

#define L2T(q,L) ((q)->L_tab[(L)>>(q)->cell_log])

static int
tbf_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;

	__skb_queue_tail(&sch->q, skb);
	if ((q->bytes += skb->len) <= q->max_bytes)
		return 1;

	/* Drop action: undo the things that we just made,
	 * i.e. make tail drop
	 */

	__skb_unlink(skb, &sch->q);
	q->bytes -= skb->len;
	kfree_skb(skb);
	return 0;
}

static void tbf_watchdog(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc*)arg;
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;

	q->wd_timer.function = NULL;

	qdisc_wakeup(sch->dev);
}


static struct sk_buff *
tbf_dequeue(struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	struct sk_buff *skb;
	
	skb = __skb_dequeue(&sch->q);

	if (skb) {
		psched_time_t now;
		long toks;

		PSCHED_GET_TIME(now);

		toks = PSCHED_TDIFF_SAFE(now, q->t_c, q->depth, 0)
			+ q->tokens - L2T(q,skb->len);

		if (toks >= 0) {
			q->t_c = now;
			q->tokens = toks <= q->depth ? toks : q->depth;
			q->bytes -= skb->len;
			return skb;
		}

		/* Maybe, we have in queue a shorter packet,
		   which can be sent now. It sounds cool,
		   but, however, wrong in principle.
		   We MUST NOT reorder packets in these curcumstances.

		   Really, if we splitted flow to independent
		   subflows, it would be very good solution.
		   Look at sch_csz.c.
		 */
		__skb_queue_head(&sch->q, skb);

		if (!sch->dev->tbusy) {
			if (q->wd_timer.function)
				del_timer(&q->wd_timer);
			q->wd_timer.function = tbf_watchdog;
			q->wd_timer.expires = jiffies + PSCHED_US2JIFFIE(-toks);
			add_timer(&q->wd_timer);
		}
	}
	return NULL;
}


static void
tbf_reset(struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&sch->q)) != NULL)
		kfree_skb(skb);
	q->bytes = 0;
	PSCHED_GET_TIME(q->t_c);
	q->tokens = q->depth;
	if (q->wd_timer.function) {
		del_timer(&q->wd_timer);
		q->wd_timer.function = NULL;
	}
}

static int tbf_init(struct Qdisc* sch, void *arg)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	struct tbfctl *ctl = (struct tbfctl*)arg;

	PSCHED_GET_TIME(q->t_c);
	init_timer(&q->wd_timer);
	q->wd_timer.function = NULL;
	q->wd_timer.data = (unsigned long)sch;
	if (ctl) {
		q->max_bytes = ctl->bytes;
		q->depth = ctl->depth;
		q->tokens = q->tokens;
		q->cell_log = ctl->cell_log;
		memcpy(q->L_tab, ctl->L_tab, 256*sizeof(unsigned long));
	}
	return 0;
}

struct Qdisc_ops tbf_ops =
{
	NULL,
	"tbf",
	0,
	sizeof(struct tbf_sched_data),
	tbf_enqueue,
	tbf_dequeue,
	tbf_reset,
	NULL,
	tbf_init,
	NULL,
};


#ifdef MODULE
#include <linux/module.h>
int init_module(void)
{
	int err;

	/* Load once and never free it. */
	MOD_INC_USE_COUNT;

	err = register_qdisc(&tbf_ops);
	if (err)
		MOD_DEC_USE_COUNT;
	return err;
}

void cleanup_module(void) 
{
}
#endif
