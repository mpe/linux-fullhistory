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

struct Qdisc_head qdisc_head = { &qdisc_head, &qdisc_head };
spinlock_t qdisc_runqueue_lock = SPIN_LOCK_UNLOCKED;

/* Main qdisc structure lock. 

   However, modifications
   to data, participating in scheduling must be additionally
   protected with dev->queue_lock spinlock.

   The idea is the following:
   - enqueue, dequeue are serialized via top level device
     spinlock dev->queue_lock.
   - tree walking is protected by read_lock(qdisc_tree_lock)
     and this lock is used only in process context.
   - updates to tree are made only under rtnl semaphore,
     hence this lock may be made without local bh disabling.

   qdisc_tree_lock must be grabbed BEFORE dev->queue_lock!
 */
rwlock_t qdisc_tree_lock = RW_LOCK_UNLOCKED;

/* Anti deadlock rules:

   qdisc_runqueue_lock protects main transmission list qdisc_head.
   Run list is accessed only under this spinlock.

   dev->queue_lock serializes queue accesses for this device
   AND dev->qdisc pointer itself.

   dev->xmit_lock serializes accesses to device driver.

   dev->queue_lock and dev->xmit_lock are mutually exclusive,
   if one is grabbed, another must be free.

   qdisc_runqueue_lock may be requested under dev->queue_lock,
   but neither dev->queue_lock nor dev->xmit_lock may be requested
   under qdisc_runqueue_lock.
 */


/* Kick device.
   Note, that this procedure can be called by a watchdog timer, so that
   we do not check dev->tbusy flag here.

   Returns:  0  - queue is empty.
            >0  - queue is not empty, but throttled.
	    <0  - queue is not empty. Device is throttled, if dev->tbusy != 0.

   NOTE: Called under dev->queue_lock with locally disabled BH.
*/

int qdisc_restart(struct device *dev)
{
	struct Qdisc *q = dev->qdisc;
	struct sk_buff *skb;

	if ((skb = q->dequeue(q)) != NULL) {
		/* Dequeue packet and release queue */
		spin_unlock(&dev->queue_lock);

		if (netdev_nit)
			dev_queue_xmit_nit(skb, dev);

		if (spin_trylock(&dev->xmit_lock)) {
			/* Remember that the driver is grabbed by us. */
			dev->xmit_lock_owner = smp_processor_id();
			if (dev->hard_start_xmit(skb, dev) == 0) {
				dev->xmit_lock_owner = -1;
				spin_unlock(&dev->xmit_lock);

				spin_lock(&dev->queue_lock);
				dev->qdisc->tx_last = jiffies;
				return -1;
			}
			/* Release the driver */
			dev->xmit_lock_owner = -1;
			spin_unlock(&dev->xmit_lock);
		} else {
			/* So, someone grabbed the driver. */

			/* It may be transient configuration error,
			   when hard_start_xmit() recurses. We detect
			   it by checking xmit owner and drop the
			   packet when deadloop is detected.
			 */
			if (dev->xmit_lock_owner == smp_processor_id()) {
				kfree_skb(skb);
				if (net_ratelimit())
					printk(KERN_DEBUG "Dead loop on virtual %s, fix it urgently!\n", dev->name);
				spin_lock(&dev->queue_lock);
				return -1;
			}

			/* Otherwise, packet is requeued
			   and will be sent by the next net_bh run.
			 */
			mark_bh(NET_BH);
		}

		/* Device kicked us out :(
		   This is possible in three cases:

		   0. driver is locked
		   1. fastroute is enabled
		   2. device cannot determine busy state
		      before start of transmission (f.e. dialout)
		   3. device is buggy (ppp)
		 */

		spin_lock(&dev->queue_lock);
		q = dev->qdisc;
		q->ops->requeue(skb, q);
		return -1;
	}
	return dev->qdisc->q.qlen;
}

static __inline__ void
qdisc_stop_run(struct Qdisc *q)
{
	q->h.forw->back = q->h.back;
	q->h.back->forw = q->h.forw;
	q->h.forw = NULL;
}

extern __inline__ void
qdisc_continue_run(struct Qdisc *q)
{
	if (!qdisc_on_runqueue(q) && q->dev) {
		q->h.forw = &qdisc_head;
		q->h.back = qdisc_head.back;
		qdisc_head.back->forw = &q->h;
		qdisc_head.back = &q->h;
	}
}

static __inline__ int
qdisc_init_run(struct Qdisc_head *lh)
{
	if (qdisc_head.forw != &qdisc_head) {
		*lh = qdisc_head;
		lh->forw->back = lh;
		lh->back->forw = lh;
		qdisc_head.forw = &qdisc_head;
		qdisc_head.back = &qdisc_head;
		return 1;
	}
	return 0;
}

/* Scan transmission queue and kick devices.

   Deficiency: slow devices (ppp) and fast ones (100Mb ethernet)
   share one queue. This means that if we have a lot of loaded ppp channels,
   we will scan a long list on every 100Mb EOI.
   I have no idea how to solve it using only "anonymous" Linux mark_bh().

   To change queue from device interrupt? Ough... only not this...

   This function is called only from net_bh.
 */

void qdisc_run_queues(void)
{
	struct Qdisc_head lh, *h;

	spin_lock(&qdisc_runqueue_lock);
	if (!qdisc_init_run(&lh))
		goto out;

	while ((h = lh.forw) != &lh) {
		int res;
		struct device *dev;
		struct Qdisc *q = (struct Qdisc*)h;

		qdisc_stop_run(q);

		dev = q->dev;
		spin_unlock(&qdisc_runqueue_lock);

		res = -1;
		if (spin_trylock(&dev->queue_lock)) {
			while (!dev->tbusy && (res = qdisc_restart(dev)) < 0)
				/* NOTHING */;
			spin_unlock(&dev->queue_lock);
		}

		spin_lock(&qdisc_runqueue_lock);
		/* If qdisc is not empty add it to the tail of list */
		if (res)
			qdisc_continue_run(q);
	}
out:
	spin_unlock(&qdisc_runqueue_lock);
}

/* Periodic watchdog timer to recover from hard/soft device bugs. */

static void dev_do_watchdog(unsigned long dummy);

static struct timer_list dev_watchdog =
	{ NULL, NULL, 0L, 0L, &dev_do_watchdog };

/*   This function is called only from timer */

static void dev_do_watchdog(unsigned long dummy)
{
	struct Qdisc_head lh, *h;

	if (!spin_trylock(&qdisc_runqueue_lock)) {
		/* No hurry with watchdog. */
		mod_timer(&dev_watchdog, jiffies + HZ/10);
		return;
	}

	if (!qdisc_init_run(&lh))
		goto out;

	while ((h = lh.forw) != &lh) {
		struct device *dev;
		struct Qdisc *q = (struct Qdisc*)h;

		qdisc_stop_run(q);

		dev = q->dev;
		spin_unlock(&qdisc_runqueue_lock);

		if (spin_trylock(&dev->queue_lock)) {
			q = dev->qdisc;
			if (dev->tbusy && jiffies - q->tx_last > q->tx_timeo)
				qdisc_restart(dev);
			spin_unlock(&dev->queue_lock);
		}

		spin_lock(&qdisc_runqueue_lock);

		qdisc_continue_run(dev->qdisc);
	}

out:
	mod_timer(&dev_watchdog, jiffies + 5*HZ);
	spin_unlock(&qdisc_runqueue_lock);
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
	TCQ_F_BUILTIN,
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
	noop_dequeue,
	TCQ_F_BUILTIN,
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
	sch->stats.lock = &dev->queue_lock;
	atomic_set(&sch->refcnt, 1);
	if (!ops->init || ops->init(sch, NULL) == 0)
		return sch;

	kfree(sch);
	return NULL;
}

/* Under dev->queue_lock and BH! */

void qdisc_reset(struct Qdisc *qdisc)
{
	struct Qdisc_ops *ops = qdisc->ops;
	if (ops->reset)
		ops->reset(qdisc);
}

/* Under dev->queue_lock and BH! */

void qdisc_destroy(struct Qdisc *qdisc)
{
	struct Qdisc_ops *ops = qdisc->ops;
	struct device *dev;

	if (!atomic_dec_and_test(&qdisc->refcnt))
		return;

	dev = qdisc->dev;

#ifdef CONFIG_NET_SCHED
	if (dev) {
		struct Qdisc *q, **qp;
		for (qp = &qdisc->dev->qdisc_list; (q=*qp) != NULL; qp = &q->next) {
			if (q == qdisc) {
				*qp = q->next;
				break;
			}
		}
	}
#ifdef CONFIG_NET_ESTIMATOR
	qdisc_kill_estimator(&qdisc->stats);
#endif
#endif
	if (ops->reset)
		ops->reset(qdisc);
	if (ops->destroy)
		ops->destroy(qdisc);
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
		struct Qdisc *qdisc;
		if (dev->tx_queue_len) {
			qdisc = qdisc_create_dflt(dev, &pfifo_fast_ops);
			if (qdisc == NULL) {
				printk(KERN_INFO "%s: activation failed\n", dev->name);
				return;
			}
		} else {
			qdisc =  &noqueue_qdisc;
		}
		write_lock(&qdisc_tree_lock);
		dev->qdisc_sleeping = qdisc;
		write_unlock(&qdisc_tree_lock);
	}

	spin_lock_bh(&dev->queue_lock);
	spin_lock(&qdisc_runqueue_lock);
	if ((dev->qdisc = dev->qdisc_sleeping) != &noqueue_qdisc) {
		dev->qdisc->tx_timeo = 5*HZ;
		dev->qdisc->tx_last = jiffies - dev->qdisc->tx_timeo;
		if (!del_timer(&dev_watchdog))
			dev_watchdog.expires = jiffies + 5*HZ;
		add_timer(&dev_watchdog);
	}
	spin_unlock(&qdisc_runqueue_lock);
	spin_unlock_bh(&dev->queue_lock);
}

void dev_deactivate(struct device *dev)
{
	struct Qdisc *qdisc;

	spin_lock_bh(&dev->queue_lock);
	qdisc = dev->qdisc;
	dev->qdisc = &noop_qdisc;

	qdisc_reset(qdisc);

	spin_lock(&qdisc_runqueue_lock);
	if (qdisc_on_runqueue(qdisc))
		qdisc_stop_run(qdisc);
	spin_unlock(&qdisc_runqueue_lock);
	spin_unlock_bh(&dev->queue_lock);
}

void dev_init_scheduler(struct device *dev)
{
	write_lock(&qdisc_tree_lock);
	spin_lock_bh(&dev->queue_lock);
	dev->qdisc = &noop_qdisc;
	spin_unlock_bh(&dev->queue_lock);
	dev->qdisc_sleeping = &noop_qdisc;
	dev->qdisc_list = NULL;
	write_unlock(&qdisc_tree_lock);
}

void dev_shutdown(struct device *dev)
{
	struct Qdisc *qdisc;

	write_lock(&qdisc_tree_lock);
	spin_lock_bh(&dev->queue_lock);
	qdisc = dev->qdisc_sleeping;
	dev->qdisc = &noop_qdisc;
	dev->qdisc_sleeping = &noop_qdisc;
	qdisc_destroy(qdisc);
	BUG_TRAP(dev->qdisc_list == NULL);
	dev->qdisc_list = NULL;
	spin_unlock_bh(&dev->queue_lock);
	write_unlock(&qdisc_tree_lock);
}
