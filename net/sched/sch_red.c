/*
 * net/sched/sch_red.c	Random Early Detection scheduler.
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


/*	Random Early Detection (RED) algorithm.
	=======================================

	Source: Sally Floyd and Van Jacobson, "Random Early Detection Gateways
	for Congestion Avoidance", 1993, IEEE/ACM Transactions on Networking.

	This file codes a "divisionless" version of RED algorithm
	written down in Fig.17 of the paper.

Short description.
------------------

	When new packet arrives we calculate average queue length:

	avg = (1-W)*avg + W*current_queue_len,

	W is filter time constant (choosen as 2^(-Wlog)), controlling
	inertia of algorithm. To allow larger bursts, W should be
	decreased.

	if (avg > th_max) -> packet marked (dropped).
	if (avg < th_min) -> packet passes.
	if (th_min < avg < th_max) we calculate probability:

	Pb = max_P * (avg - th_min)/(th_max-th_min)

	and mark (drop) packet with this probability.
	Pb changes from 0 (at avg==th_min) to max_P (avg==th_max).
	max_P should be small (not 1!).

	NB.	SF&VJ assumed that Pb[avg] is linear function. I think it
	        is wrong. I'd make:
		P[th_min] = 0, P[th_max] = 1;
		dP/davg[th_min] = 0, dP/davg[th_max] = infinity, or a large number.

	I choose max_P as a number between 0.01 and 0.1, so that
	C1 = max_P/(th_max-th_min) is power of two: C1 = 2^(-C1log)

	Parameters, settable by user (with default values):

	qmaxbytes=256K - hard limit on queue length, should be chosen >qth_max
	                 to allow packet bursts. This parameter does not
			 affect algorithm behaviour and can be chosen
			 arbitrarily high (well, less than ram size)
			 Really, this limit will never be achieved
			 if RED works correctly.
	qth_min=32K
	qth_max=128K   - qth_max should be at least 2*qth_min
	Wlog=8	       - log(1/W).
	Alog=Wlog      - fixed point position in th_min and th_max.
	Rlog=10
	C1log=24       - C1log = trueC1log+Alog-Rlog
	                 so that trueC1log=22 and max_P~0.02
	

NOTES:

Upper bound on W.
-----------------

	If you want to allow bursts of L packets of size S,
	you should choose W:

	L + 1 -th_min/S < (1-(1-W)^L)/W

	For th_min/S = 32

	log(W)	L
	-1	33
	-2	35
	-3	39
	-4	46
	-5	57
	-6	75
	-7	101
	-8	135
	-9	190
	etc.
 */

struct red_sched_data
{
/* Parameters */
	unsigned long	qmaxbytes;	/* HARD maximal queue length	*/
	unsigned long	qth_min;	/* Min average length threshold: A scaled */
	unsigned long	qth_max;	/* Max average length threshold: A scaled */
	char		Alog;		/* Point position in average lengths */
	char		Wlog;		/* log(W)		*/
	char		Rlog;		/* random number bits	*/
	char		C1log;		/* log(1/C1)		*/
	char		Slog;
	char		Stab[256];

/* Variables */
	unsigned long	qbytes;		/* Queue length in bytes	*/
	unsigned long	qave;		/* Average queue length: A scaled */
	int		qcount;		/* Packets since last random number generation */
	unsigned	qR;		/* Cached random number [0..1<Rlog) */
	psched_time_t	qidlestart;	/* Start of idle period		*/
};

/* Stolen from igmp.c. */

static __inline__ unsigned red_random(int log)
{
	static unsigned long seed=152L;
	seed=seed*69069L+1;
	return (seed^jiffies)&((1<<log)-1);
}

static int
red_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct red_sched_data *q = (struct red_sched_data *)sch->data;

	psched_time_t now;

	if (!PSCHED_IS_PASTPERFECT(q->qidlestart)) {
		long us_idle;
		PSCHED_SET_PASTPERFECT(q->qidlestart);
		PSCHED_GET_TIME(now);
		us_idle = PSCHED_TDIFF_SAFE(now, q->qidlestart, (256<<q->Slog)-1, 0);

/* It is wrong, but I do not think that SF+VJ proposal is reasonable
   and did not invented anything more clever 8)

   The problem: ideally, average length queue recalcultion should
   be done over constant clock intervals. It is too expensive, so that
   calculation is driven by outgoing packets.
   When queue is idle we have to model this clock by hands.

   SF+VJ proposed to "generate" m = (idletime/bandwidth)*average_pkt_size
   dummy packets as burst after idle time, i.e.

          q->qave *= (1-W)^m

   It is apparently overcomplicated solution (f.e. we have to precompute
   a table to make this calculation for reasonable time)
   I believe, that a simpler model may be used here,
   but it is field for experiments.
*/
		q->qave >>= q->Stab[(us_idle>>q->Slog)&0xFF];
	}

	q->qave += ((q->qbytes<<q->Alog) - q->qave) >> q->Wlog;

	if (q->qave < q->qth_min) {
enqueue:
		q->qcount = -1;
		if (q->qbytes <= q->qmaxbytes) {
			skb_queue_tail(&sch->q, skb);
			q->qbytes += skb->len;
			return 1;
		}
drop:
		kfree_skb(skb, FREE_WRITE);
		return 0;
	}
	if (q->qave >= q->qth_max) {
		q->qcount = -1;
		goto drop;
	}
	q->qcount++;
	if (q->qcount++) {
		if ((((q->qave - q->qth_min)*q->qcount)>>q->C1log) < q->qR)
			goto enqueue;
		q->qcount = 0;
		q->qR = red_random(q->Rlog);
		goto drop;
	}
	q->qR = red_random(q->Rlog);
	goto enqueue;
}

static struct sk_buff *
red_dequeue(struct Qdisc* sch)
{
	struct sk_buff *skb;
	struct red_sched_data *q = (struct red_sched_data *)sch->data;

	skb = skb_dequeue(&sch->q);
	if (skb) {
		q->qbytes -= skb->len;
		return skb;
	}
	PSCHED_GET_TIME(q->qidlestart);
	return NULL;
}

static void
red_reset(struct Qdisc* sch)
{
	struct red_sched_data *q = (struct red_sched_data *)sch->data;
	struct sk_buff *skb;

	while((skb=skb_dequeue(&sch->q))!=NULL) {
		q->qbytes -= skb->len;
		kfree_skb(skb,FREE_WRITE);
	}
	if (q->qbytes) {
		printk("red_reset: qbytes=%lu\n", q->qbytes);
		q->qbytes = 0;
	}
	PSCHED_SET_PASTPERFECT(q->qidlestart);
	q->qave = 0;
	q->qcount = -1;
}

static int red_init(struct Qdisc *sch, struct pschedctl *pctl)
{
	struct red_sched_data *q;
	struct redctl *ctl = (struct redctl*)pctl->args;

	q = (struct red_sched_data *)sch->data;

	if (pctl->arglen < sizeof(struct redctl))
		return -EINVAL;

	q->Wlog = ctl->Wlog;
	q->Alog = ctl->Alog;
	q->Rlog = ctl->Rlog;
	q->C1log = ctl->C1log;
	q->Slog = ctl->Slog;
	q->qth_min = ctl->qth_min;
	q->qth_max = ctl->qth_max;
	q->qmaxbytes = ctl->qmaxbytes;
	memcpy(q->Stab, ctl->Stab, 256);

	q->qcount = -1;
	PSCHED_SET_PASTPERFECT(q->qidlestart);
	return 0;
}

struct Qdisc_ops red_ops =
{
	NULL,
	"red",
	0,
	sizeof(struct red_sched_data),
	red_enqueue,
	red_dequeue,
	red_reset,
	NULL,
	red_init,
	NULL
};


#ifdef MODULE
#include <linux/module.h>
int init_module(void)
{
	int err;

	/* Load once and never free it. */
	MOD_INC_USE_COUNT;

	err = register_qdisc(&red_ops);
	if (err)
		MOD_DEC_USE_COUNT;
	return err;
}

void cleanup_module(void) 
{
}
#endif
