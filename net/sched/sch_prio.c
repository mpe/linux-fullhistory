/*
 * net/sched/sch_prio.c	Simple 3-band priority "scheduler".
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

/* New N-band generic scheduler */

struct prio_sched_data
{
	int qbytes;
	int bands;
	u8  prio2band[8];
	struct Qdisc *queues[8];
};

static int
prio_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;
	int prio = q->prio2band[skb->priority&7];
	struct Qdisc *qdisc;

	qdisc = q->queues[prio];
	if (qdisc->enqueue(skb, qdisc) == 0) {
		q->qbytes += skb->len;
		sch->q.qlen++;
		return 0;
	}
	return 1;
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
			q->qbytes -= skb->len;
			sch->q.qlen--;
			return skb;
		}
	}
	return NULL;

}

static void
prio_reset(struct Qdisc* sch)
{
	int prio;
	struct prio_sched_data *q = (struct prio_sched_data *)sch->data;

	for (prio=0; prio<q->bands; prio++)
		qdisc_reset(q->queues[prio]);
	q->qbytes = 0;
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
}

static int prio_init(struct Qdisc *sch, void *arg)
{
	const static u8 prio2band[8] = { 1, 2, 2, 2, 1, 2, 0, 0 };
	struct prio_sched_data *q;
	int i;

	q = (struct prio_sched_data *)sch->data;
	q->bands = 3;
	memcpy(q->prio2band, prio2band, sizeof(prio2band));
	for (i=0; i<q->bands; i++)
		q->queues[i] = &noop_qdisc;
	return 0;
}

struct Qdisc_ops prio_ops =
{
	NULL,
	"prio",
	0,
	sizeof(struct prio_sched_data),
	prio_enqueue,
	prio_dequeue,
	prio_reset,
	prio_destroy,
	prio_init,
};

#ifdef MODULE
#include <linux/module.h>
int init_module(void)
{
	int err;

	/* Load once and never free it. */
	MOD_INC_USE_COUNT;

	err = register_qdisc(&prio_ops);
	if (err)
		MOD_DEC_USE_COUNT;
	return err;
}

void cleanup_module(void) 
{
}
#endif
