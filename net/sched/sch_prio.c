/*
 * net/sched/sch_prio.c	Simple 3-band priority "scheduler".
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <linux/config.h>
#include <linux/module.h>
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


struct prio_sched_data
{
	int bands;
	struct tcf_proto *filter_list;
	u8  prio2band[TC_PRIO_MAX+1];
	struct Qdisc *queues[TCQ_PRIO_BANDS];
};


static __inline__ unsigned prio_classify(struct sk_buff *skb, struct Qdisc *sch)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	struct tcf_result res;

	res.classid = skb->priority;
	if (TC_H_MAJ(res.classid) != sch->handle) {
		if (!q->filter_list || tc_classify(skb, q->filter_list, &res)) {
			if (TC_H_MAJ(res.classid))
				res.classid = 0;
			res.classid = q->prio2band[res.classid&TC_PRIO_MAX] + 1;
		}
	}

	return res.classid - 1;
}

static int
prio_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	struct Qdisc *qdisc;

	qdisc = q->queues[prio_classify(skb, sch)];

	if (qdisc->enqueue(skb, qdisc) == 1) {
		sch->stats.bytes += skb->len;
		sch->stats.packets++;
		sch->q.qlen++;
		return 1;
	}
	sch->stats.drops++;
	return 0;
}


static int
prio_requeue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	struct Qdisc *qdisc;

	qdisc = q->queues[prio_classify(skb, sch)];

	if (qdisc->ops->requeue(skb, qdisc) == 1) {
		sch->q.qlen++;
		return 1;
	}
	sch->stats.drops++;
	return 0;
}


static struct sk_buff *
prio_dequeue(struct Qdisc* sch)
{
	struct sk_buff *skb;
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	int prio;
	struct Qdisc *qdisc;

	for (prio = 0; prio < q->bands; prio++) {
		qdisc = q->queues[prio];
		skb = qdisc->dequeue(qdisc);
		if (skb) {
			sch->q.qlen--;
			return skb;
		}
	}
	return NULL;

}

static int
prio_drop(struct Qdisc* sch)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	int prio;
	struct Qdisc *qdisc;

	for (prio = q->bands-1; prio >= 0; prio--) {
		qdisc = q->queues[prio];
		if (qdisc->ops->drop(qdisc)) {
			sch->q.qlen--;
			return 1;
		}
	}
	return 0;
}


static void
prio_reset(struct Qdisc* sch)
{
	int prio;
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;

	for (prio=0; prio<q->bands; prio++)
		qdisc_reset(q->queues[prio]);
	sch->q.qlen = 0;
}

static void
prio_destroy(struct Qdisc* sch)
{
	int prio;
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;

	for (prio=0; prio<q->bands; prio++) {
		qdisc_destroy(q->queues[prio]);
		q->queues[prio] = &noop_qdisc;
	}
	MOD_DEC_USE_COUNT;
}

static int prio_init(struct Qdisc *sch, struct rtattr *opt)
{
	static const u8 prio2band[TC_PRIO_MAX+1] =
	{ 1, 2, 2, 2, 1, 2, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 };
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	unsigned mask = 0;
	int i;

	if (opt == NULL) {
		q->bands = 3;
		memcpy(q->prio2band, prio2band, sizeof(prio2band));
		mask = 7;
	} else {
		struct tc_prio_qopt *qopt = RTA_DATA(opt);

		if (opt->rta_len < RTA_LENGTH(sizeof(*qopt)))
			return -EINVAL;
		if (qopt->bands > TCQ_PRIO_BANDS)
			return -EINVAL;
		q->bands = qopt->bands;
		for (i=0; i<=TC_PRIO_MAX; i++) {
			if (qopt->priomap[i] >= q->bands)
				return -EINVAL;
			q->prio2band[i] = qopt->priomap[i];
			mask |= (1<<qopt->priomap[i]);
		}
	}
	for (i=0; i<TCQ_PRIO_BANDS; i++) {
		if (mask&(1<<i))
			q->queues[i] = qdisc_create_dflt(sch->dev, &pfifo_qdisc_ops);
		if (q->queues[i] == NULL)
			q->queues[i] = &noop_qdisc;
	}
	MOD_INC_USE_COUNT;
	return 0;
}

#ifdef CONFIG_RTNETLINK
static int prio_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	unsigned char	 *b = skb->tail;
	struct tc_prio_qopt opt;

	opt.bands = q->bands;
	memcpy(&opt.priomap, q->prio2band, TC_PRIO_MAX+1);
	RTA_PUT(skb, TCA_OPTIONS, sizeof(opt), &opt);
	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}
#endif

static int prio_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		      struct Qdisc **old)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	unsigned long band = arg - 1;

	if (band >= q->bands)
		return -EINVAL;

	if (new == NULL)
		new = &noop_qdisc;

	*old = xchg(&q->queues[band], new);

	return 0;
}

static unsigned long prio_get(struct Qdisc *sch, u32 classid)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	unsigned long band = TC_H_MIN(classid);

	if (band - 1 >= q->bands)
		return 0;
	return band;
}

static void prio_put(struct Qdisc *q, unsigned long cl)
{
	return;
}

static int prio_change(struct Qdisc *sch, u32 handle, u32 parent, struct rtattr **tca, unsigned long *arg)
{
	unsigned long cl = *arg;
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;

	if (cl - 1 > q->bands)
		return -ENOENT;
	return 0;
}

static int prio_delete(struct Qdisc *sch, unsigned long cl)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	if (cl - 1 > q->bands)
		return -ENOENT;
	return 0;
}


#ifdef CONFIG_RTNETLINK
static int prio_dump_class(struct Qdisc *sch, unsigned long cl, struct sk_buff *skb, struct tcmsg *tcm)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;

	if (cl - 1 > q->bands)
		return -ENOENT;
	return 0;
}
#endif

static void prio_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	int prio;

	if (arg->stop)
		return;

	for (prio = 0; prio < q->bands; prio++) {
		if (arg->count < arg->skip) {
			arg->count++;
			continue;
		}
		if (arg->fn(sch, prio+1, arg) < 0) {
			arg->stop = 1;
			break;
		}
		arg->count++;
	}
}

static struct tcf_proto ** prio_find_tcf(struct Qdisc *sch, unsigned long cl)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;

	if (cl)
		return NULL;
	return &q->filter_list;
}

static struct Qdisc_class_ops prio_class_ops =
{
	prio_graft,
	prio_get,
	prio_put,
	prio_change,
	prio_delete,
	prio_walk,

	prio_find_tcf,
	prio_get,
	prio_put,

#ifdef CONFIG_RTNETLINK
	prio_dump_class,
#endif
};

struct Qdisc_ops prio_qdisc_ops =
{
	NULL,
	&prio_class_ops,
	"prio",
	sizeof(struct prio_sched_data),

	prio_enqueue,
	prio_dequeue,
	prio_requeue,
	prio_drop,

	prio_init,
	prio_reset,
	prio_destroy,

#ifdef CONFIG_RTNETLINK
	prio_dump,
#endif
};

#ifdef MODULE

int init_module(void)
{
	return register_qdisc(&prio_qdisc_ops);
}

void cleanup_module(void) 
{
	unregister_qdisc(&prio_qdisc_ops);
}

#endif
