/*
 * net/sched/sch_generic.c	Generic packet scheduler routines.
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
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

#define BUG_TRAP(x) if (!(x)) { printk("Assertion (" #x ") failed at " __FILE__ "(%d):" __FUNCTION__ "\n", __LINE__); }

/* Main transmission queue. */

struct Qdisc_head qdisc_head = { &qdisc_head };

/* Kick device.
   Note, that this procedure can be called by a watchdog timer, so that
   we do not check dev->tbusy flag here.

   Returns:  0  - queue is empty.
            >0  - queue is not empty, but throttled.
	    <0  - queue is not empty. Device is throttled, if dev->tbusy != 0.

   NOTE: Called only from NET BH
*/

int qdisc_restart(struct device *dev)
{
	struct Qdisc *q = dev->qdisc;
	struct sk_buff *skb;

	if ((skb = q->dequeue(q)) != NULL) {
		if (netdev_nit)
			dev_queue_xmit_nit(skb, dev);

		if (dev->hard_start_xmit(skb, dev) == 0) {
			q->tx_last = jiffies;
			return -1;
		}

		/* Device kicked us out :(
		   This is possible in three cases:

		   1. fastroute is enabled
		   2. device cannot determine busy state
		      before start of transmission (f.e. dialout)
		   3. device is buggy (ppp)
		 */

		q->ops->requeue(skb, q);
		return -1;
	}
	return q->q.qlen;
}

/* Scan transmission queue and kick devices.

   Deficiency: slow devices (ppp) and fast ones (100Mb ethernet)
   share one queue. This means that if we have a lot of loaded ppp channels,
   we will scan a long list on every 100Mb EOI.
   I have no idea how to solve it using only "anonymous" Linux mark_bh().

   To change queue from device interrupt? Ough... only not this...
 */

void qdisc_run_queues(void)
{
	struct Qdisc_head **hp, *h;

	hp = &qdisc_head.forw;
	while ((h = *hp) != &qdisc_head) {
		int res = -1;
		struct Qdisc *q = (struct Qdisc*)h;
		struct device *dev = q->dev;

		while (!dev->tbusy && (res = qdisc_restart(dev)) < 0)
			/* NOTHING */;

		/* An explanation is necessary here.
		   qdisc_restart called dev->hard_start_xmit,
		   if device is virtual, it could trigger one more
		   dev_queue_xmit and a new device could appear
		   in the active chain. In this case we cannot unlink
		   the empty queue, because we lost the back pointer.
		   No problem, we will unlink it during the next round.
		 */

		if (res == 0 && *hp == h) {
			*hp = h->forw;
			h->forw = NULL;
			continue;
		}
		hp = &h->forw;
	}
}

/* Periodic watchdoc timer to recover from hard/soft device bugs. */

static void dev_do_watchdog(unsigned long dummy);

static struct timer_list dev_watchdog =
	{ NULL, NULL, 0L, 0L, &dev_do_watchdog };

static void dev_do_watchdog(unsigned long dummy)
{
	struct Qdisc_head *h;

	for (h = qdisc_head.forw; h != &qdisc_head; h = h->forw) {
		struct Qdisc *q = (struct Qdisc*)h;
		struct device *dev = q->dev;
		if (dev->tbusy && jiffies - q->tx_last > q->tx_timeo)
			qdisc_restart(dev);
	}
	dev_watchdog.expires = jiffies + 5*HZ;
	add_timer(&dev_watchdog);
}



/* "NOOP" scheduler: the best scheduler, recommended for all interfaces
   under all circumstances. It is difficult to invent anything faster or
   cheaper.
 */

static int
noop_enqueue(struct sk_buff *skb, struct Qdisc * qdisc)
{
	kfree_skb(skb);
	return 0;
}

static struct sk_buff *
noop_dequeue(struct Qdisc * qdisc)
{
	return NULL;
}

static int
noop_requeue(struct sk_buff *skb, struct Qdisc* qdisc)
{
	if (net_ratelimit())
		printk(KERN_DEBUG "%s deferred output. It is buggy.\n", skb->dev->name);
	kfree_skb(skb);
	return 0;
}

struct Qdisc_ops noop_qdisc_ops =
{
	NULL,
	NULL,
	"noop",
	0,

	noop_enqueue,
	noop_dequeue,
	noop_requeue,
};

struct Qdisc noop_qdisc =
{
        { NULL }, 
	noop_enqueue,
	noop_dequeue,
	TCQ_F_DEFAULT|TCQ_F_BUILTIN,
	&noop_qdisc_ops,	
};


struct Qdisc_ops noqueue_qdisc_ops =
{
	NULL,
	NULL,
	"noqueue",
	0,

	noop_enqueue,
	noop_dequeue,
	noop_requeue,

};

struct Qdisc noqueue_qdisc =
{
        { NULL }, 
	NULL,
	NULL,
	TCQ_F_DEFAULT|TCQ_F_BUILTIN,
	&noqueue_qdisc_ops,
};


static const u8 prio2band[TC_PRIO_MAX+1] =
{ 1, 2, 2, 2, 1, 2, 0, 0 , 1, 1, 1, 1, 1, 1, 1, 1 };

/* 3-band FIFO queue: old style, but should be a bit faster than
   generic prio+fifo combination.
 */

static int
pfifo_fast_enqueue(struct sk_buff *skb, struct Qdisc* qdisc)
{
	struct sk_buff_head *list;

	list = ((struct sk_buff_head*)qdisc->data) +
		prio2band[skb->priority&TC_PRIO_MAX];

	if (list->qlen <= skb->dev->tx_queue_len) {
		__skb_queue_tail(list, skb);
		qdisc->q.qlen++;
		return 1;
	}
	qdisc->stats.drops++;
	kfree_skb(skb);
	return 0;
}

static struct sk_buff *
pfifo_fast_dequeue(struct Qdisc* qdisc)
{
	int prio;
	struct sk_buff_head *list = ((struct sk_buff_head*)qdisc->data);
	struct sk_buff *skb;

	for (prio = 0; prio < 3; prio++, list++) {
		skb = __skb_dequeue(list);
		if (skb) {
			qdisc->q.qlen--;
			return skb;
		}
	}
	return NULL;
}

static int
pfifo_fast_requeue(struct sk_buff *skb, struct Qdisc* qdisc)
{
	struct sk_buff_head *list;

	list = ((struct sk_buff_head*)qdisc->data) +
		prio2band[skb->priority&TC_PRIO_MAX];

	__skb_queue_head(list, skb);
	qdisc->q.qlen++;
	return 1;
}

static void
pfifo_fast_reset(struct Qdisc* qdisc)
{
	int prio;
	struct sk_buff_head *list = ((struct sk_buff_head*)qdisc->data);

	for (prio=0; prio < 3; prio++)
		skb_queue_purge(list+prio);
	qdisc->q.qlen = 0;
}

static int pfifo_fast_init(struct Qdisc *qdisc, struct rtattr *opt)
{
	int i;
	struct sk_buff_head *list;

	list = ((struct sk_buff_head*)qdisc->data);

	for (i=0; i<3; i++)
		skb_queue_head_init(list+i);

	return 0;
}

static struct Qdisc_ops pfifo_fast_ops =
{
	NULL,
	NULL,
	"pfifo_fast",
	3 * sizeof(struct sk_buff_head),

	pfifo_fast_enqueue,
	pfifo_fast_dequeue,
	pfifo_fast_requeue,
	NULL,

	pfifo_fast_init,
	pfifo_fast_reset,
};

struct Qdisc * qdisc_create_dflt(struct device *dev, struct Qdisc_ops *ops)
{
	struct Qdisc *sch;
	int size = sizeof(*sch) + ops->priv_size;

	sch = kmalloc(size, GFP_KERNEL);
	if (!sch)
		return NULL;
	memset(sch, 0, size);

	skb_queue_head_init(&sch->q);
	sch->ops = ops;
	sch->enqueue = ops->enqueue;
	sch->dequeue = ops->dequeue;
	sch->dev = dev;
	sch->flags |= TCQ_F_DEFAULT;
	if (ops->init && ops->init(sch, NULL) == 0)
		return sch;

	kfree(sch);
	return NULL;
}

void qdisc_reset(struct Qdisc *qdisc)
{
	struct Qdisc_ops *ops = qdisc->ops;
	start_bh_atomic();
	if (ops->reset)
		ops->reset(qdisc);
	end_bh_atomic();
}

void qdisc_destroy(struct Qdisc *qdisc)
{
	struct Qdisc_ops *ops = qdisc->ops;
#ifdef CONFIG_NET_SCHED
	if (qdisc->dev) {
		struct Qdisc *q, **qp;
		for (qp = &qdisc->dev->qdisc_list; (q=*qp) != NULL; qp = &q->next)
			if (q == qdisc) {
				*qp = q->next;
				q->next = NULL;
				break;
			}
	}
#ifdef CONFIG_NET_ESTIMATOR
	qdisc_kill_estimator(&qdisc->stats);
#endif
#endif
	start_bh_atomic();
	if (ops->reset)
		ops->reset(qdisc);
	if (ops->destroy)
		ops->destroy(qdisc);
	end_bh_atomic();
	if (!(qdisc->flags&TCQ_F_BUILTIN))
		kfree(qdisc);
}


void dev_activate(struct device *dev)
{
	/* No queueing discipline is attached to device;
	   create default one i.e. pfifo_fast for devices,
	   which need queueing and noqueue_qdisc for
	   virtual interfaces
	 */

	if (dev->qdisc_sleeping == &noop_qdisc) {
		if (dev->tx_queue_len) {
			struct Qdisc *qdisc;
			qdisc = qdisc_create_dflt(dev, &pfifo_fast_ops);
			if (qdisc == NULL) {
				printk(KERN_INFO "%s: activation failed\n", dev->name);
				return;
			}
			dev->qdisc_sleeping = qdisc;
		} else
			dev->qdisc_sleeping = &noqueue_qdisc;
	}

	start_bh_atomic();
	if ((dev->qdisc = dev->qdisc_sleeping) != &noqueue_qdisc) {
		dev->qdisc->tx_timeo = 5*HZ;
		dev->qdisc->tx_last = jiffies - dev->qdisc->tx_timeo;
		if (!del_timer(&dev_watchdog))
			dev_watchdog.expires = jiffies + 5*HZ;
		add_timer(&dev_watchdog);
	}
	end_bh_atomic();
}

void dev_deactivate(struct device *dev)
{
	struct Qdisc *qdisc;

	start_bh_atomic();

	qdisc = xchg(&dev->qdisc, &noop_qdisc);

	qdisc_reset(qdisc);

	if (qdisc->h.forw) {
		struct Qdisc_head **hp, *h;

		for (hp = &qdisc_head.forw; (h = *hp) != &qdisc_head; hp = &h->forw) {
			if (h == &qdisc->h) {
				*hp = h->forw;
				break;
			}
		}
	}

	end_bh_atomic();
}

void dev_init_scheduler(struct device *dev)
{
	dev->qdisc = &noop_qdisc;
	dev->qdisc_sleeping = &noop_qdisc;
	dev->qdisc_list = NULL;
}

void dev_shutdown(struct device *dev)
{
	struct Qdisc *qdisc;

	start_bh_atomic();
	qdisc = dev->qdisc_sleeping;
	dev->qdisc = &noop_qdisc;
	dev->qdisc_sleeping = &noop_qdisc;
	qdisc_destroy(qdisc);
	BUG_TRAP(dev->qdisc_list == NULL);
	dev->qdisc_list = NULL;
	end_bh_atomic();
}

struct Qdisc * dev_set_scheduler(struct device *dev, struct Qdisc *qdisc)
{
	struct Qdisc *oqdisc;

	if (dev->flags & IFF_UP)
		dev_deactivate(dev);

	start_bh_atomic();
	oqdisc = dev->qdisc_sleeping;

	/* Prune old scheduler */
	if (oqdisc)
		qdisc_reset(oqdisc);

	/* ... and graft new one */
	if (qdisc == NULL)
		qdisc = &noop_qdisc;
	dev->qdisc_sleeping = qdisc;
	dev->qdisc = &noop_qdisc;
	end_bh_atomic();

	if (dev->flags & IFF_UP)
		dev_activate(dev);

	return oqdisc;
}

