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

struct Qdisc_head qdisc_head = { &qdisc_head };

static struct Qdisc_ops *qdisc_base = NULL;

/* NOTES.

   Every discipline has two major routines: enqueue and dequeue.

   ---dequeue

   dequeue usually returns a skb to send. It is allowed to return NULL,
   but it does not mean that queue is empty, it just means that
   discipline does not want to send anything this time.
   Queue is really empty if q->q.qlen == 0.
   For complicated disciplines with multiple queues q->q is not
   real packet queue, but however q->q.qlen must be valid.

   ---enqueue

   enqueue returns number of enqueued packets i.e. this number is 1,
   if packet was enqueued sucessfully and <1 if something (not
   necessary THIS packet) was dropped.

 */

int register_qdisc(struct Qdisc_ops *qops)
{
	struct Qdisc_ops *q, **qp;
	for (qp = &qdisc_base; (q=*qp)!=NULL; qp = &q->next)
		if (strcmp(qops->id, q->id) == 0)
			return -EEXIST;
	qops->next = NULL;
	qops->refcnt = 0;
	*qp = qops;
	return 0;
}

int unregister_qdisc(struct Qdisc_ops *qops)
{
	struct Qdisc_ops *q, **qp;
	for (qp = &qdisc_base; (q=*qp)!=NULL; qp = &q->next)
		if (q == qops)
			break;
	if (!q)
		return -ENOENT;
	*qp = q->next;
	return 0;
}

struct Qdisc *qdisc_lookup(int handle)
{
	return NULL;
}


/* "NOOP" scheduler: the best scheduler, recommended for all interfaces
   in all curcumstances. It is difficult to invent anything more
   fast or cheap.
 */

static int
noop_enqueue(struct sk_buff *skb, struct Qdisc * qdisc)
{
	kfree_skb(skb, FREE_WRITE);
	return 0;
}

static struct sk_buff *
noop_dequeue(struct Qdisc * qdisc)
{
	return NULL;
}

struct Qdisc noop_qdisc =
{
        { NULL }, 
	noop_enqueue,
	noop_dequeue,
};

struct Qdisc noqueue_qdisc =
{
        { NULL }, 
	NULL,
	NULL,
};


/* 3-band FIFO queue: old style, but should be a bit faster (several CPU insns) */

static int
pfifo_fast_enqueue(struct sk_buff *skb, struct Qdisc* qdisc)
{
	const static u8 prio2band[8] = { 1, 2, 2, 2, 1, 2, 0, 0 };
	struct sk_buff_head *list;

	list = ((struct sk_buff_head*)qdisc->data) + prio2band[skb->priority&7];

	if (list->qlen <= skb->dev->tx_queue_len) {
		skb_queue_tail(list, skb);
		return 1;
	}
	qdisc->dropped++;
	kfree_skb(skb, FREE_WRITE);
	return 0;
}

static struct sk_buff *
pfifo_fast_dequeue(struct Qdisc* qdisc)
{
	int prio;
	struct sk_buff_head *list = ((struct sk_buff_head*)qdisc->data);
	struct sk_buff *skb;

	for (prio = 0; prio < 3; prio++, list++) {
		skb = skb_dequeue(list);
		if (skb)
			return skb;
	}
	return NULL;
}

static void
pfifo_fast_reset(struct Qdisc* qdisc)
{
	int prio;
	struct sk_buff_head *list = ((struct sk_buff_head*)qdisc->data);

	for (prio=0; prio < 3; prio++)
		skb_queue_purge(list+prio);
}

static int pfifo_fast_init(struct Qdisc *qdisc, void *arg)
{
	int i;
	struct sk_buff_head *list;

	list = ((struct sk_buff_head*)qdisc->data);

	for(i=0; i<3; i++)
		skb_queue_head_init(list+i);

	return 0;
}

static struct Qdisc_ops pfifo_fast_ops =
{
	NULL,
	"pfifo_fast",
	1,
	3 * sizeof(struct sk_buff_head),
	pfifo_fast_enqueue,
	pfifo_fast_dequeue,
	pfifo_fast_reset,
	NULL,
	pfifo_fast_init
};

static struct Qdisc *
qdisc_alloc(struct device *dev, struct Qdisc_ops *ops, void *arg)
{
	struct Qdisc *sch;
	int size = sizeof(*sch) + ops->priv_size;

	sch = kmalloc(size, GFP_KERNEL);
	if (!sch)
		return NULL;
	memset(sch, 0, size);

	skb_queue_head_init(&sch->q);
	skb_queue_head_init(&sch->failure_q);
	sch->ops = ops;
	sch->enqueue = ops->enqueue;
	sch->dequeue = ops->dequeue;
	sch->dev = dev;
	if (ops->init && ops->init(sch, arg))
		return NULL;
	ops->refcnt++;
	return sch;
}

void qdisc_reset(struct Qdisc *qdisc)
{
	struct Qdisc_ops *ops = qdisc->ops;
	if (ops) {
		start_bh_atomic();
		if (ops->reset)
			ops->reset(qdisc);
		skb_queue_purge(&qdisc->failure_q);
		end_bh_atomic();
	}
}

void qdisc_destroy(struct Qdisc *qdisc)
{
	struct Qdisc_ops *ops = qdisc->ops;
	if (ops) {
		start_bh_atomic();
		if (ops->reset)
			ops->reset(qdisc);
		if (ops->destroy)
			ops->destroy(qdisc);
		skb_queue_purge(&qdisc->failure_q);
		ops->refcnt--;
		end_bh_atomic();
		kfree(qdisc);
	}
}

static void dev_do_watchdog(unsigned long dummy);

static struct timer_list dev_watchdog =
	{ NULL, NULL, 0L, 0L, &dev_do_watchdog };

static void dev_do_watchdog(unsigned long dummy)
{
	struct Qdisc_head *h;

	for (h = qdisc_head.forw; h != &qdisc_head; h = h->forw) {
		struct Qdisc *q = (struct Qdisc*)h;
		struct device *dev = q->dev;
		if (dev->tbusy && jiffies - q->tx_last > q->tx_timeo) {
			qdisc_restart(dev);
		}
	}
	dev_watchdog.expires = jiffies + 5*HZ;
	add_timer(&dev_watchdog);
}


void dev_activate(struct device *dev)
{
	/* No queueing discipline is attached to device;
	   create default one i.e. pfifo_fast for devices,
	   which need queueing and noqueue_qdisc for
	   virtual intrfaces
	 */

	if (dev->qdisc_sleeping == &noop_qdisc) {
		if (dev->tx_queue_len) {
			struct Qdisc *qdisc;
			qdisc = qdisc_alloc(dev, &pfifo_fast_ops, NULL);
			if (qdisc == NULL)
				return;
			dev->qdisc_sleeping = qdisc;
		} else
			dev->qdisc_sleeping = &noqueue_qdisc;
	}

	start_bh_atomic();
	if ((dev->qdisc = dev->qdisc_sleeping) != &noqueue_qdisc) {
		dev->qdisc->tx_timeo = 5*HZ;
		dev->qdisc->tx_last = jiffies - dev->qdisc->tx_timeo;
		if (!dev_watchdog.expires) {
			dev_watchdog.expires = jiffies + 5*HZ;
			add_timer(&dev_watchdog);
		}
	}
	end_bh_atomic();
}

void dev_deactivate(struct device *dev)
{
	struct Qdisc *qdisc;

	start_bh_atomic();

	qdisc = dev->qdisc;
	dev->qdisc = &noop_qdisc;

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
}

void dev_shutdown(struct device *dev)
{
	struct Qdisc *qdisc;

	start_bh_atomic();
	qdisc = dev->qdisc_sleeping;
	dev->qdisc_sleeping = &noop_qdisc;
	qdisc_destroy(qdisc);	
	end_bh_atomic();
}

void dev_set_scheduler(struct device *dev, struct Qdisc *qdisc)
{
	struct Qdisc *oqdisc;

	if (dev->flags & IFF_UP)
		dev_deactivate(dev);

	start_bh_atomic();
	oqdisc = dev->qdisc_sleeping;

	/* Destroy old scheduler */
	if (oqdisc)
		qdisc_destroy(oqdisc);

	/* ... and attach new one */
	dev->qdisc_sleeping = qdisc;
	dev->qdisc = &noop_qdisc;
	end_bh_atomic();

	if (dev->flags & IFF_UP)
		dev_activate(dev);
}

/* Kick the queue "q".
   Note, that this procedure is called by watchdog timer, so that
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

	skb = skb_dequeue(&q->failure_q);
	if (!skb) {
		skb = q->dequeue(q);
		if (netdev_nit && skb)
			dev_queue_xmit_nit(skb,dev);
	}
	if (skb) {
		if (dev->hard_start_xmit(skb, dev) == 0) {
			q->tx_last = jiffies;
			return -1;
		}
#if 0
		if (net_ratelimit())
			printk(KERN_DEBUG "netdevice %s defers output.\n", dev->name);
#endif
		skb_queue_head(&q->failure_q, skb);
		return -1;
	}
	return q->q.qlen;
}

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

		/* The explanation is necessary here.
		   qdisc_restart called dev->hard_start_xmit,
		   if device is virtual, it could trigger one more
		   dev_queue_xmit and new device could appear
		   in active chain. In this case we cannot unlink
		   empty queue, because we lost back pointer.
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


int tc_init(struct pschedctl *pctl)
{
	struct Qdisc *q;
	struct Qdisc_ops *qops;

	if (pctl->handle) {
		q = qdisc_lookup(pctl->handle);
		if (q == NULL)
			return -ENOENT;
		qops = q->ops;
		if (pctl->ifindex && q->dev->ifindex != pctl->ifindex)
			return -EINVAL;
	}
	return -EINVAL;
}

int tc_destroy(struct pschedctl *pctl)
{
	return -EINVAL;
}

int tc_attach(struct pschedctl *pctl)
{
	return -EINVAL;
}

int tc_detach(struct pschedctl *pctl)
{
	return -EINVAL;
}


int psched_ioctl(void *arg)
{
	struct pschedctl ctl;
	struct pschedctl *pctl = &ctl;
	int err;

	if (copy_from_user(&ctl, arg, sizeof(ctl)))
		return -EFAULT;

	if (ctl.arglen > 0) {
		pctl = kmalloc(sizeof(ctl) + ctl.arglen, GFP_KERNEL);
		if (pctl == NULL)
			return -ENOBUFS;
		memcpy(pctl, &ctl, sizeof(ctl));
		if (copy_from_user(pctl->args, ((struct pschedctl*)arg)->args, ctl.arglen)) {
			kfree(pctl);
			return -EFAULT;
		}
	}

	rtnl_lock();

	switch (ctl.command) {
	case PSCHED_TC_INIT:
		err = tc_init(pctl);
		break;
	case PSCHED_TC_DESTROY:
		err = tc_destroy(pctl);
		break;
	case PSCHED_TC_ATTACH:
		err = tc_attach(pctl);
		break;
	case PSCHED_TC_DETACH:
		err = tc_detach(pctl);
		break;
	default:
		err = -EINVAL;
	}

	rtnl_unlock();

	if (pctl != &ctl)
		kfree(pctl);
	return err;
}

__initfunc(int pktsched_init(void))
{
#define INIT_QDISC(name) { \
          extern struct Qdisc_ops name##_ops; \
          register_qdisc(&##name##_ops); \
	}

	skb_queue_head_init(&noop_qdisc.failure_q);
	skb_queue_head_init(&noqueue_qdisc.failure_q);

	register_qdisc(&pfifo_fast_ops);
#ifdef CONFIG_NET_SCH_CBQ
	INIT_QDISC(cbq);
#endif
#ifdef CONFIG_NET_SCH_CSZ
	INIT_QDISC(csz);
#endif
#ifdef CONFIG_NET_SCH_RED
	INIT_QDISC(red);
#endif
#ifdef CONFIG_NET_SCH_SFQ
	INIT_QDISC(sfq);
#endif
#ifdef CONFIG_NET_SCH_TBF
	INIT_QDISC(tbf);
#endif
#ifdef CONFIG_NET_SCH_PFIFO
	INIT_QDISC(pfifo);
	INIT_QDISC(bfifo);
#endif
#ifdef CONFIG_NET_SCH_PRIO
	INIT_QDISC(prio);
#endif
	return 0;
}
