/*
 * net/sched/sch_cbq.c	Class-Based Queueing discipline.
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
#include <linux/module.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

/*	Class-Based Queueing (CBQ) algorithm.
	=======================================

	Sources: [1] Sally Floyd and Van Jacobson, "Link-sharing and Resource
	         Management Models for Packet Networks",
		 IEEE/ACM Transactions on Networking, Vol.3, No.4, 1995

	         [2] Sally Floyd, "Notes on CBQ and Guaranted Service", 1995

	         [3] Sally Floyd, "Notes on Class-Based Queueing: Setting
		 Parameters", 1996

	Algorithm skeleton is taken from from NS simulator cbq.cc.

	-----------------------------------------------------------------------

	Differences from NS version.

	--- WRR algorith is different. Our version looks more reasonable :-)
	and fair when quanta are allowed to be less than MTU.

	--- cl->aveidle is REALLY limited from below by cl->minidle.
	Seems, it was bug in NS.

	--- Purely lexical change: "depth" -> "level", "maxdepth" -> "toplevel".
	When depth increases we expect, that the thing becomes lower, does not it? :-)
	Besides that, "depth" word is semantically overloaded ---
	"token bucket depth", "sfq depth"... Besides that, the algorithm
	was called "top-LEVEL sharing".

	PROBLEM.

	--- Linux has no EOI event at the moment, so that we cannot
	estimate true class idle time. Three workarounds are possible,
	all of them have drawbacks:

	1. (as now) Consider the next dequeue event as sign that
	previous packet is finished. It is wrong because of ping-pong
	buffers, but on permanently loaded link it is true.
	2. (NS approach) Use as link busy time estimate skb->leb/"physical
	bandwidth". Even more wrong f.e. on ethernet real busy time much
	higher because of collisions.
	3. (seems, the most clever) Split net bh to two parts:
	NETRX_BH (for received packets) and preserve NET_BH for transmitter.
	It will not require driver changes (NETRX_BH flag will be set
	in netif_rx), but will allow to trace EOIs more precisely
	and will save useless checks in net_bh. Besides that we will
	have to eliminate random calling hard_start_xmit with dev->tbusy flag
	(done) and to drop failure_q --- i.e. if !dev->tbusy hard_start_xmit
	MUST succeed; failed packets will be dropped on the floor.
*/

#define CBQ_TOPLEVEL_SHARING
/* #define CBQ_NO_TRICKERY */

#define CBQ_CLASSIFIER(skb, q) ((q)->fallback_class)

struct cbq_class
{
/* Parameters */
	int			priority;	/* priority */
#ifdef CBQ_TOPLEVEL_SHARING
	int			level;		/* level of the class in hierarchy:
						   0 for leaf classes, and maximal
						   level of childrens + 1 for nodes.
						 */
#endif

	long			maxidle;	/* Class paramters: see below. */
	long			minidle;
	int			filter_log;
#ifndef CBQ_NO_TRICKERY
	long			extradelay;
#endif

	long			quantum;	/* Allotment per WRR round */
	long			rquantum;	/* Relative allotment: see below */

	int			cell_log;
	unsigned long		L_tab[256];

	struct Qdisc		*qdisc;		/* ptr to CBQ discipline */
	struct cbq_class	*root;		/* Ptr to root class;
						   root can be not unique.
						 */
	struct cbq_class	*parent;	/* Ptr to parent in the class tree */
	struct cbq_class	*borrow;	/* NULL if class is bandwidth limited;
						   parent otherwise */

	struct Qdisc		*q;		/* Elementary queueing discipline */
	struct cbq_class	*next;		/* next class in this priority band */

	struct cbq_class	*next_alive;	/* next class with backlog in this priority band */

/* Variables */
	psched_time_t		last;
	psched_time_t		undertime;
	long			avgidle;
	long			deficit;	/* Saved deficit for WRR */
	char			awake;		/* Class is in alive list */

#if 0
	void			(*overlimit)(struct cbq_class *cl);
#endif
};

#define L2T(cl,len)	((cl)->L_tab[(len)>>(cl)->cell_log])

struct cbq_sched_data
{
	struct cbq_class	*classes[CBQ_MAXPRIO];	/* List of all classes */
	int			nclasses[CBQ_MAXPRIO];
	unsigned		quanta[CBQ_MAXPRIO];
	unsigned		mtu;
	int			cell_log;
	unsigned long		L_tab[256];
	struct cbq_class	*fallback_class;

	unsigned		activemask;
	struct cbq_class	*active[CBQ_MAXPRIO];	/* List of all classes
							   with backlog */
	struct cbq_class	*last_sent;
	int			last_sent_len;

	psched_time_t		now;		/* Cached timestamp */

	struct timer_list	wd_timer;	/* Wathchdog timer, that
						   started when CBQ has
						   backlog, but cannot
						   transmit just now */
	unsigned long		wd_expires;
#ifdef CBQ_TOPLEVEL_SHARING
	struct cbq_class	*borrowed;
	int			toplevel;
#endif
};

/*
   WRR quanta
   ----------

   cl->quantum is number added to class allotment on every round.
   cl->rquantum is "relative" quantum.

   For real-time classes:

   cl->quantum = (cl->rquantum*q->nclasses[prio]*q->mtu)/q->quanta[prio]

   where q->quanta[prio] is sum of all rquanta for given priority.
   cl->rquantum can be identified with absolute rate of the class
   in arbitrary units (f.e. bytes/sec)

   In this case, delay introduced by round-robin was estimated by
   Sally Floyd [2] as:

   D = q->nclasses*q->mtu/(bandwidth/2)

   Note, that D does not depend on class rate (it is very bad),
   but not much worse than Gallager-Parekh estimate for CSZ
   C/R = q->mtu/rate, when real-time classes have close rates.

   For not real-time classes this folmula is not necessary,
   so that cl->quantum can be set to any reasonable not zero value.
   Apparently, it should be proportional to class rate, if the
   rate is not zero.
*/

/*
   maxidle, minidle, extradelay
   ----------------------------

   CBQ estimator calculates smoothed class idle time cl->aveidle,
   considering class as virtual interface with corresponding bandwidth.
   When cl->aveidle wants to be less than zero, class is overlimit.
   When it is positive, class is underlimit.

   * maxidle bounds aveidle from above.
     It controls maximal length of burst in this class after
     long period of idle time. Burstness of active class
     is controlled by filter constant cl->filter_log,
     but this number is related to burst length only indirectly.

   * minidle is a negative number, normally set to zero.
     Setting it to not zero value allows avgidle to drop
     below zero, effectively penalizing class, when it is overlimit.
     When the class load will decrease, it will take a time to
     raise negative avgidle to put the class at limit.
     It should be set to zero for leaf classes.

   * extradelay is penalty in delay, when a class goes overlimit.
     I believe this parameter is useless and confusing.
     Setting it to not zero forces class to accumulate
     its "idleness" for extradelay and then send BURST of packets
     until going to overlimit again. Non-sense.

   For details see [1] and [3].

   Really, minidle and extradelay are irrelevant to real scheduling
   task. As I understand, SF&VJ introduced them to experiment
   with CBQ simulator in attempts to fix erratic behaviour
   of ancestor-only (and, partially, top-level) algorithm.

   WARNING.

   User passes them measured in usecs, but cl->minidle,
   cl->maxidle and cl->aveidle are scaled with cl->filter_log
   in the text of the scheduler.
*/

/*
   A packet has just been enqueued on the empty class.
   cbq_wakeup_class adds it to the tail of active class list
   of its priority band.
 */

static __inline__ void cbq_wakeup_class(struct cbq_class *cl)
{
	struct cbq_sched_data *q = (struct cbq_sched_data*)cl->qdisc->data;
	int prio = cl->priority;
	struct cbq_class *cl_tail;

	cl->awake = 1;

	cl_tail = q->active[prio];
	q->active[prio] = cl;

	if (cl_tail != NULL) {
		cl->next_alive = cl_tail->next_alive;
		cl->deficit = 0;
	} else {
		cl->next_alive = cl;
		q->activemask |= (1<<prio);
		cl->deficit = cl->quantum;
	}
}

static int
cbq_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct cbq_sched_data *q = (struct cbq_sched_data *)sch->data;
	struct cbq_class *cl = CBQ_CLASSIFIER(skb, q);

	if (cl->q->enqueue(skb, cl->q) == 1) {
		sch->q.qlen++;

#ifdef CBQ_TOPLEVEL_SHARING
		if (q->toplevel > 0) {
			psched_time_t now;
			PSCHED_GET_TIME(now);
			if (PSCHED_TLESS(cl->undertime, now))
				q->toplevel = 0;
			else if (q->toplevel > 1 && cl->borrow &&
				 PSCHED_TLESS(cl->borrow->undertime, now))
				q->toplevel = 1;
		}
#endif
		if (!cl->awake)
			cbq_wakeup_class(cl);
		return 1;
	}
	return 0;
}

static __inline__ void cbq_delay(struct cbq_sched_data *q, struct cbq_class *cl)
{
	long delay;

	delay = PSCHED_TDIFF(cl->undertime, q->now);
	if (q->wd_expires == 0 || q->wd_expires - delay > 0)
		q->wd_expires = delay;
}

static void cbq_watchdog(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc*)arg;
	struct cbq_sched_data *q = (struct cbq_sched_data*)sch->data;

	q->wd_timer.expires = 0;
	q->wd_timer.function = NULL;
	qdisc_wakeup(sch->dev);
}

static __inline__ void
cbq_update(struct cbq_sched_data *q)
{
	struct cbq_class *cl;

	for (cl = q->last_sent; cl; cl = cl->parent) {
		long avgidle = cl->avgidle;
		long idle;

		/*
		   (now - last) is total time between packet right edges.
		   (last_pktlen/rate) is "virtual" busy time, so that

		         idle = (now - last) - last_pktlen/rate
		 */

		idle = PSCHED_TDIFF(q->now, cl->last)
			- L2T(cl, q->last_sent_len);

		/* true_avgidle := (1-W)*true_avgidle + W*idle,
		   where W=2^{-filter_log}. But cl->avgidle is scaled:
		   cl->avgidle == true_avgidle/W,
		   hence:
		 */
		avgidle += idle - (avgidle>>cl->filter_log);

		if (avgidle <= 0) {
			/* Overlimit or at-limit */
#ifdef CBQ_NO_TRICKERY
			avgidle = 0;
#else
			if (avgidle < cl->minidle)
				avgidle = cl->minidle;
#endif

			/* This line was missing in NS. */
			cl->avgidle = avgidle;

			/* Calculate expected time, when this class
			   will be allowed to send.
			   It will occur, when:
			   (1-W)*true_avgidle + W*delay = 0, i.e.
			   idle = (1/W - 1)*(-true_avgidle)
			   or
			   idle = (1 - W)*(-cl->avgidle);

			   That is not all.
			   We want to set undertime to the moment, when
			   the class is allowed to start next transmission i.e.
			   (undertime + next_pktlen/phys_bandwidth)
			   - now - next_pktlen/rate = idle
			   or
			   undertime = now + idle + next_pktlen/rate
			   - next_pktlen/phys_bandwidth

			   We do not know next packet length, but can
			   estimate it with average packet length
			   or current packet_length.
			 */

			idle = (-avgidle) - ((-avgidle) >> cl->filter_log);
			idle += L2T(q, q->last_sent_len);
			idle -= L2T(cl, q->last_sent_len);
			PSCHED_TADD2(q->now, idle, cl->undertime);
#ifndef CBQ_NO_TRICKERY
			/* Do not forget extra delay :-) */
			PSCHED_TADD(cl->undertime, cl->extradelay);
#endif
		} else {
			/* Underlimit */

			PSCHED_SET_PASTPERFECT(cl->undertime);
			if (avgidle > cl->maxidle)
				cl->avgidle = cl->maxidle;
			else
				cl->avgidle = avgidle;
		}
		cl->last = q->now;
	}

#ifdef CBQ_TOPLEVEL_SHARING
	cl = q->last_sent;

	if (q->borrowed && q->toplevel >= q->borrowed->level) {
		if (cl->q->q.qlen <= 1 || PSCHED_TLESS(q->now, q->borrowed->undertime))
			q->toplevel = CBQ_MAXLEVEL;
		else if (q->borrowed != cl)
			q->toplevel = q->borrowed->level;
	}
#endif

	q->last_sent = NULL;
}

static __inline__ int
cbq_under_limit(struct cbq_class *cl)
{
	struct cbq_sched_data *q = (struct cbq_sched_data*)cl->qdisc->data;
	struct cbq_class *this_cl = cl;

	if (PSCHED_IS_PASTPERFECT(cl->undertime) || cl->parent == NULL)
		return 1;

	if (PSCHED_TLESS(cl->undertime, q->now)) {
		q->borrowed = cl;
		return 1;
	}

	while (!PSCHED_IS_PASTPERFECT(cl->undertime) &&
	       PSCHED_TLESS(q->now, cl->undertime)) {
		cl = cl->borrow;
		if (cl == NULL
#ifdef CBQ_TOPLEVEL_SHARING
		    || cl->level > q->toplevel
#endif
		    ) {
#if 0
			this_cl->overlimit(this_cl);
#else
			cbq_delay(q, this_cl);
#endif
			return 0;
		}
	}
	q->borrowed = cl;
	return 1;
}

static __inline__ struct sk_buff *
cbq_dequeue_prio(struct Qdisc *sch, int prio, int fallback)
{
	struct cbq_sched_data *q = (struct cbq_sched_data *)sch->data;
	struct cbq_class *cl_tail, *cl_prev, *cl;
	struct sk_buff *skb;
	int deficit;

	cl_tail = cl_prev = q->active[prio];
	cl = cl_prev->next_alive;

	do {
		deficit = 0;

		/* Start round */
		do {
			/* Class is empty */
			if (cl->q->q.qlen == 0) 
				goto skip_class;
			
			if (fallback) {
				/* Fallback pass: all classes are overlimit;
				   we send from the first class that is allowed
				   to borrow.
				 */

				if (cl->borrow == NULL)
					goto skip_class;
			} else {
				/* Normal pass: check that class is under limit */
				if (!cbq_under_limit(cl))
					goto skip_class;
			}

			if (cl->deficit <= 0) {
				/* Class exhausted its allotment per this
				   round.
				 */
				deficit = 1;
				goto next_class;
			}

			skb = cl->q->dequeue(cl->q);

			/* Class did not give us any skb :-(
			   It could occur if cl->q == "tbf"
			 */
			if (skb == NULL)
				goto skip_class;

			cl->deficit -= skb->len;
			q->last_sent = cl;
			q->last_sent_len = skb->len;

			if (cl->deficit <= 0) {
				q->active[prio] = cl;
				cl = cl->next_alive;
				cl->deficit += cl->quantum;
			}
			return skb;

skip_class:
			cl->deficit = 0;

			if (cl->q->q.qlen == 0) {
				/* Class is empty, declare it dead */
				cl_prev->next_alive = cl->next_alive;
				cl->awake = 0;

				/* Did cl_tail point to it? */
				if (cl == cl_tail) {
					/* Repair it! */
					cl_tail = cl_prev;

					/* Was it the last class in this band? */
					if (cl == cl_tail) {
						/* Kill the band! */
						q->active[prio] = NULL;
						q->activemask &= ~(1<<prio);
						return NULL;
					}
				}
			}

next_class:
			cl_prev = cl;
			cl = cl->next_alive;
			cl->deficit += cl->quantum;
		} while (cl_prev != cl_tail);
	} while (deficit);

	q->active[prio] = cl_prev;
	
	return NULL;
}

static __inline__ struct sk_buff *
cbq_dequeue_1(struct Qdisc *sch, int fallback)
{
	struct cbq_sched_data *q = (struct cbq_sched_data *)sch->data;
	struct sk_buff *skb;
	unsigned activemask;

	activemask = q->activemask;
	while (activemask) {
		int prio = ffz(~activemask);
		activemask &= ~(1<<prio);
		skb = cbq_dequeue_prio(sch, prio, fallback);
		if (skb)
			return skb;
	}
	return NULL;
}

static struct sk_buff *
cbq_dequeue(struct Qdisc *sch)
{
	struct sk_buff *skb;
	struct cbq_sched_data *q = (struct cbq_sched_data *)sch->data;

	PSCHED_GET_TIME(q->now);

	if (q->last_sent)
		cbq_update(q);

	q->wd_expires = 0;

	skb = cbq_dequeue_1(sch, 0);
	if (skb)
		return skb;

	/* All the classes are overlimit.
	   Search for overlimit class, which is allowed to borrow
	   and use it as fallback case.
	 */

#ifdef CBQ_TOPLEVEL_SHARING
	q->toplevel = CBQ_MAXLEVEL;
#endif

	skb = cbq_dequeue_1(sch, 1);
	if (skb)
		return skb;

	/* No packets in scheduler or nobody wants to give them to us :-(
	   Sigh... start watchdog timer in the last case. */

	if (sch->q.qlen && q->wd_expires) {
		if (q->wd_timer.function)
			del_timer(&q->wd_timer);
		q->wd_timer.function = cbq_watchdog;
		q->wd_timer.expires = jiffies + PSCHED_US2JIFFIE(q->wd_expires);
		add_timer(&q->wd_timer);
	}
	return NULL;
}

/* CBQ class maintanance routines */

static void cbq_adjust_levels(struct cbq_class *this)
{
	struct cbq_class *cl;

	for (cl = this->parent; cl; cl = cl->parent) {
		if (cl->level > this->level)
			return;
		cl->level = this->level + 1;
		this = cl;
	}
}

static void cbq_normalize_quanta(struct cbq_sched_data *q, int prio)
{
	struct cbq_class *cl;

	if (q->quanta[prio] == 0)
		return;

	for (cl = q->classes[prio]; cl; cl = cl->next) {
		if (cl->rquantum)
			cl->quantum = (cl->rquantum*q->mtu*q->nclasses[prio])/
				q->quanta[prio];
	}
}

static __inline__ int cbq_unlink_class(struct cbq_class *this)
{
	struct cbq_class *cl, **clp;
	struct cbq_sched_data *q = (struct cbq_sched_data*)this->qdisc->data;

	for (clp = &q->classes[this->priority]; (cl = *clp) != NULL;
	     clp = &cl->next) {
		if (cl == this) {
			*clp = cl->next;
			return 0;
		}
	}
	return -ENOENT;
}

static int cbq_prune(struct cbq_class *this)
{
	struct cbq_class *cl;
	int prio = this->priority;
	struct cbq_sched_data *q = (struct cbq_sched_data*)this->qdisc->data;

	qdisc_reset(this->q);

	if (cbq_unlink_class(this))
		return -ENOENT;

	if (this->awake) {
		struct cbq_class *cl_prev = q->active[prio];
		do {
			cl = cl_prev->next_alive;
			if (cl == this) {
				cl_prev->next_alive = cl->next_alive;

				if (cl == q->active[prio]) {
					q->active[prio] = cl;
					if (cl == q->active[prio]) {
						q->active[prio] = NULL;
						q->activemask &= ~(1<<prio);
						break;
					}
				}

				cl = cl->next_alive;
				cl->deficit += cl->quantum;
				break;
			}
		} while ((cl_prev = cl) != q->active[prio]);
	}

	--q->nclasses[prio];
	if (this->rquantum) {
		q->quanta[prio] -= this->rquantum;
		cbq_normalize_quanta(q, prio);
	}

	if (q->fallback_class == this)
		q->fallback_class = NULL;

	this->parent = NULL;
	this->borrow = NULL;
	this->root = this;
	this->qdisc = NULL;
	return 0;
}

static int cbq_graft(struct cbq_class *this, struct cbq_class *parent)
{
	struct cbq_class *cl, **clp;
	int prio = this->priority;
	struct cbq_sched_data *q = (struct cbq_sched_data*)this->qdisc->data;

	qdisc_reset(this->q);


	for (clp = &q->classes[prio]; (cl = *clp) != NULL; clp = &cl->next) {
		if (cl == this)
			return -EBUSY;
	}

	cl->next = NULL;
	*clp = cl;
	
	cl->parent = parent;
	cl->borrow = parent;
	cl->root = parent ? parent->root : cl;

	++q->nclasses[prio];
	if (this->rquantum) {
		q->quanta[prio] += this->rquantum;
		cbq_normalize_quanta(q, prio);
	}
	
	cbq_adjust_levels(this);

	return 0;
}


static void
cbq_reset(struct Qdisc* sch)
{
	struct cbq_sched_data *q = (struct cbq_sched_data *)sch->data;
	struct cbq_class *cl;
	int prio;

	q->activemask = 0;
	q->last_sent = NULL;
	if (q->wd_timer.function) {
		del_timer(&q->wd_timer);
		q->wd_timer.expires = 0;
		q->wd_timer.function = NULL;
	}
#ifdef CBQ_TOPLEVEL_SHARING
	q->toplevel = CBQ_MAXLEVEL;
#endif

	for (prio = 0; prio < CBQ_MAXPRIO; prio++) {
		q->active[prio] = NULL;
		
		for (cl = q->classes[prio]; cl; cl = cl->next) {
			qdisc_reset(cl->q);

			cl->next_alive = NULL;
			PSCHED_SET_PASTPERFECT(cl->undertime);
			cl->avgidle = 0;
			cl->deficit = 0;
			cl->awake = 0;
		}
	}
}

static void
cbq_destroy(struct Qdisc* sch)
{
	struct cbq_sched_data *q = (struct cbq_sched_data *)sch->data;
	struct cbq_class *cl, **clp;
	int prio;

	for (prio = 0; prio < CBQ_MAXPRIO; prio++) {
		struct cbq_class *cl_head = q->classes[prio];
		
		for (clp = &cl_head; (cl=*clp) != NULL; clp = &cl->next) {
			qdisc_destroy(cl->q);
			kfree(cl);
		}
	}
}

static int cbq_control(struct Qdisc *sch, void *arg)
{
	struct cbq_sched_data *q;

	q = (struct cbq_sched_data *)sch->data;

	/* Do attachment here. It is the last thing to do. */

	return -EINVAL;
}

static int cbq_init(struct Qdisc *sch, void *arg)
{
	struct cbq_sched_data *q;
	struct cbqctl *ctl = (struct cbqctl*)arg;

	q = (struct cbq_sched_data *)sch->data;
	init_timer(&q->wd_timer);
	q->wd_timer.data = (unsigned long)sch;
#ifdef CBQ_TOPLEVEL_SHARING
	q->toplevel = CBQ_MAXLEVEL;
#endif

	return 0;
}


struct Qdisc_ops cbq_ops =
{
	NULL,
	"cbq",
	0,
	sizeof(struct cbq_sched_data),
	cbq_enqueue,
	cbq_dequeue,
	cbq_reset,
	cbq_destroy,
	cbq_init,
	cbq_control,
};

#ifdef MODULE
int init_module(void)
{
	int err;

	/* Load once and never free it. */
	MOD_INC_USE_COUNT;

	err = register_qdisc(&cbq_ops);
	if (err)
		MOD_DEC_USE_COUNT;
	return err;
}

void cleanup_module(void) 
{
}
#endif
