/*
 * net/sched/sch_fifo.c	Simple FIFO "scheduler"
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

/* 1 band FIFO pseudo-"scheduler" */

struct fifo_sched_data
{
	int	qmaxbytes;
	int	qmaxlen;
	int	qbytes;
};

static int
bfifo_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct fifo_sched_data *q = (struct fifo_sched_data *)sch->data;

	if (q->qbytes <= q->qmaxbytes) {
		skb_queue_tail(&sch->q, skb);
		q->qbytes += skb->len;
		return 0;
	}
	kfree_skb(skb);
	return 1;
}

static struct sk_buff *
bfifo_dequeue(struct Qdisc* sch)
{
	struct fifo_sched_data *q = (struct fifo_sched_data *)sch->data;
	struct sk_buff *skb;

	skb = skb_dequeue(&sch->q);
	if (skb)
		q->qbytes -= skb->len;
	return skb;
}

static void
bfifo_reset(struct Qdisc* sch)
{
	struct fifo_sched_data *q = (struct fifo_sched_data *)sch->data;
	struct sk_buff *skb;

	while((skb=skb_dequeue(&sch->q)) != NULL) {
		q->qbytes -= skb->len;
		kfree_skb(skb);
	}
	if (q->qbytes) {
		printk("fifo_reset: qbytes=%d\n", q->qbytes);
		q->qbytes = 0;
	}
}

static int
pfifo_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct fifo_sched_data *q = (struct fifo_sched_data *)sch->data;

	if (sch->q.qlen <= q->qmaxlen) {
		skb_queue_tail(&sch->q, skb);
		return 0;
	}
	kfree_skb(skb);
	return 1;
}

static struct sk_buff *
pfifo_dequeue(struct Qdisc* sch)
{
	return skb_dequeue(&sch->q);
}

static void
pfifo_reset(struct Qdisc* sch)
{
	struct sk_buff *skb;

	while((skb=skb_dequeue(&sch->q))!=NULL)
		kfree_skb(skb);
}


static int fifo_init(struct Qdisc *sch, void *arg /* int bytes, int pkts */)
{
	struct fifo_sched_data *q;
/*
	struct device *dev = sch->dev;
 */

	q = (struct fifo_sched_data *)sch->data;
/*
	if (pkts<0)
		pkts = dev->tx_queue_len;
	if (bytes<0)
		bytes = pkts*dev->mtu;
	q->qmaxbytes = bytes;
	q->qmaxlen = pkts;
 */
	return 0;
}

struct Qdisc_ops pfifo_ops =
{
	NULL,
	"pfifo",
	0,
	sizeof(struct fifo_sched_data),
	pfifo_enqueue,
	pfifo_dequeue,
	pfifo_reset,
	NULL,
	fifo_init,
};

struct Qdisc_ops bfifo_ops =
{
	NULL,
	"pfifo",
	0,
	sizeof(struct fifo_sched_data),
	bfifo_enqueue,
	bfifo_dequeue,
	bfifo_reset,
	NULL,
	fifo_init,
};

#ifdef MODULE
#include <linux/module.h>
int init_module(void)
{
	int err;

	/* Load once and never free it. */
	MOD_INC_USE_COUNT;

	err = register_qdisc(&pfifo_ops);
	if (err == 0) {
		err = register_qdisc(&bfifo_ops);
		if (err)
			unregister_qdisc(&pfifo_ops);
	}
	if (err)
		MOD_DEC_USE_COUNT;
	return err;
}

void cleanup_module(void) 
{
}
#endif
