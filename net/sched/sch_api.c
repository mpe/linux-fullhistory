/*
 * net/sched/sch_api.c	Packet scheduler API.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Fixes:
 *
 * Rani Assaf <rani@magic.metawire.com> :980802: JIFFIES and CPU clock sources are repaired.
 */

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
#include <linux/proc_fs.h>

#include <net/sock.h>
#include <net/pkt_sched.h>

#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>

#define BUG_TRAP(x) if (!(x)) { printk("Assertion (" #x ") failed at " __FILE__ "(%d):" __FUNCTION__ "\n", __LINE__); }

#ifdef CONFIG_RTNETLINK
static int qdisc_notify(struct sk_buff *oskb, struct nlmsghdr *n,
			struct Qdisc *old, struct Qdisc *new);
static int tclass_notify(struct sk_buff *oskb, struct nlmsghdr *n,
			 struct Qdisc *q, unsigned long cl, int event);
#endif

/*

   Short review.
   -------------

   This file consists of two interrelated parts:

   1. queueing disciplines manager frontend.
   2. traffic classes manager frontend.

   Generally, queueing discipline ("qdisc") is a black box,
   which is able to enqueue packets and to dequeue them (when
   device is ready to send something) in order and at times
   determined by algorithm hidden in it.

   qdisc's are divided to two categories:
   - "queues", which have no internal structure visible from outside.
   - "schedulers", which split all the packets to "traffic classes",
     using "packet classifiers" (look at cls_api.c)

   In turn, classes may have child qdiscs (as rule, queues)
   attached to them etc. etc. etc.

   The goal of the routines in this file is to translate
   information supplied by user in the form of handles
   to more intelligible for kernel form, to make some sanity
   checks and part of work, which is common to all qdiscs
   and to provide rtnetlink notifications.

   All real intelligent work is done inside qdisc modules.



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
   if packet was enqueued successfully and <1 if something (not
   necessary THIS packet) was dropped.

   Auxiliary routines:

   ---requeue

   requeues once dequeued packet. It is used for non-standard or
   just buggy devices, which can defer output even if dev->tbusy=0.

   ---reset

   returns qdisc to initial state: purge all buffers, clear all
   timers, counters (except for statistics) etc.

   ---init

   initializes newly created qdisc.

   ---destroy

   destroys resources allocated by init and during lifetime of qdisc.
 */

/************************************************
 *	Queueing disciplines manipulation.	*
 ************************************************/


/* The list of all installed queueing disciplines. */

static struct Qdisc_ops *qdisc_base = NULL;

/* Register/uregister queueing discipline */

int register_qdisc(struct Qdisc_ops *qops)
{
	struct Qdisc_ops *q, **qp;

	for (qp = &qdisc_base; (q=*qp)!=NULL; qp = &q->next)
		if (strcmp(qops->id, q->id) == 0)
			return -EEXIST;

	if (qops->enqueue == NULL)
		qops->enqueue = noop_qdisc_ops.enqueue;
	if (qops->requeue == NULL)
		qops->requeue = noop_qdisc_ops.requeue;
	if (qops->dequeue == NULL)
		qops->dequeue = noop_qdisc_ops.dequeue;

	qops->next = NULL;
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
	q->next = NULL;
	return 0;
}

/* We know handle. Find qdisc among all qdisc's attached to device
   (root qdisc, all its children, children of children etc.)
 */

struct Qdisc *qdisc_lookup(struct device *dev, u32 handle)
{
	struct Qdisc *q;

	for (q = dev->qdisc_list; q; q = q->next) {
		if (q->handle == handle)
			return q;
	}
	return NULL;
}

/* We know classid. Find qdisc among all qdisc's attached to device
   (root qdisc, all its children, children of children etc.)
 */

struct Qdisc *qdisc_lookup_class(struct device *dev, u32 classid)
{
	struct Qdisc *q;

	for (q = dev->qdisc_list; q; q = q->next) {
		if (q->classid == classid)
			return q;
	}
	return NULL;
}


/* Find queueing discipline by name */

struct Qdisc_ops *qdisc_lookup_ops(struct rtattr *kind)
{
	struct Qdisc_ops *q;

	if (kind) {
		for (q = qdisc_base; q; q = q->next) {
			if (rtattr_strcmp(kind, q->id) == 0)
				return q;
		}
	}
	return NULL;
}

static struct qdisc_rate_table *qdisc_rtab_list;

struct qdisc_rate_table *qdisc_get_rtab(struct tc_ratespec *r, struct rtattr *tab)
{
	struct qdisc_rate_table *rtab;

	for (rtab = qdisc_rtab_list; rtab; rtab = rtab->next) {
		if (memcmp(&rtab->rate, r, sizeof(struct tc_ratespec)) == 0) {
			rtab->refcnt++;
			return rtab;
		}
	}

	if (tab == NULL || r->rate == 0 || r->cell_log == 0 || RTA_PAYLOAD(tab) != 1024)
		return NULL;

	rtab = kmalloc(sizeof(*rtab), GFP_KERNEL);
	if (rtab) {
		rtab->rate = *r;
		rtab->refcnt = 1;
		memcpy(rtab->data, RTA_DATA(tab), 1024);
		rtab->next = qdisc_rtab_list;
		qdisc_rtab_list = rtab;
	}
	return rtab;
}

void qdisc_put_rtab(struct qdisc_rate_table *tab)
{
	struct qdisc_rate_table *rtab, **rtabp;

	if (!tab || --tab->refcnt)
		return;

	for (rtabp = &qdisc_rtab_list; (rtab=*rtabp) != NULL; rtabp = &rtab->next) {
		if (rtab == tab) {
			*rtabp = rtab->next;
			kfree(rtab);
			return;
		}
	}
}


/* Allocate an unique handle from space managed by kernel */

u32 qdisc_alloc_handle(struct device *dev)
{
	int i = 0x10000;
	static u32 autohandle = TC_H_MAKE(0x80000000U, 0);

	do {
		autohandle += TC_H_MAKE(0x10000U, 0);
		if (autohandle == TC_H_MAKE(TC_H_ROOT, 0))
			autohandle = TC_H_MAKE(0x80000000U, 0);
	} while	(qdisc_lookup(dev, autohandle) && --i > 0);

	return i>0 ? autohandle : 0;
}

/* Graft qdisc "new" to class "classid" of qdisc "parent" or
   to device "dev".

   Old qdisc is not destroyed but returned in *old.
 */

int qdisc_graft(struct device *dev, struct Qdisc *parent, u32 classid,
		struct Qdisc *new, struct Qdisc **old)
{
	int err = 0;

	if (parent == NULL) {
		BUG_TRAP(classid == TC_H_ROOT);
		if (new) {
			new->parent = NULL;
			new->classid = TC_H_ROOT;
		}
		*old = dev_set_scheduler(dev, new);
	} else {
		struct Qdisc_class_ops *cops = parent->ops->cl_ops;

		BUG_TRAP(classid != TC_H_ROOT);

		err = -EINVAL;

		if (cops) {
			unsigned long cl = cops->get(parent, classid);
			if (cl) {
				err = cops->graft(parent, cl, new, old);
				cops->put(parent, cl);
			}
		}
	}
	return err;
}

#ifdef CONFIG_RTNETLINK

/*
   Allocate and initialize new qdisc.

   Parameters are passed via opt.
 */

static struct Qdisc *
qdisc_create(struct device *dev, struct Qdisc_ops *ops, u32 handle,
	     u32 parentid, struct rtattr **tca, int *errp)
{
	int err;
	struct rtattr *kind = tca[TCA_KIND-1];
	struct Qdisc *sch = NULL;
	int size;
	int new = 0;

	if (ops == NULL) {
		ops = qdisc_lookup_ops(kind);
		err = -EINVAL;
		if (ops == NULL)
			goto err_out;
		new = 1;
	}

	size = sizeof(*sch) + ops->priv_size;

	sch = kmalloc(size, GFP_KERNEL);
	err = -ENOBUFS;
	if (!sch)
		goto err_out;

	/* Grrr... Resolve race condition with module unload */
	
	err = -EINVAL;
	if (new) {
		if (ops != qdisc_lookup_ops(kind))
			goto err_out;
	} else if (kind) {
		if (rtattr_strcmp(kind, ops->id))
			goto err_out;
	}

	memset(sch, 0, size);

	skb_queue_head_init(&sch->q);
	sch->ops = ops;
	sch->enqueue = ops->enqueue;
	sch->dequeue = ops->dequeue;
	sch->dev = dev;
	if (handle == 0) {
		handle = qdisc_alloc_handle(dev);
		err = -ENOMEM;
		if (handle == 0)
			goto err_out;
	}
	sch->handle = handle;
	sch->classid = parentid;

	if (ops->init && (err = ops->init(sch, tca[TCA_OPTIONS-1])) == 0) {
		sch->next = dev->qdisc_list;
		dev->qdisc_list = sch;
#ifdef CONFIG_NET_ESTIMATOR
		if (tca[TCA_RATE-1])
			qdisc_new_estimator(&sch->stats, tca[TCA_RATE-1]);
#endif
		return sch;
	}

err_out:
	*errp = err;
	if (sch)
		kfree(sch);
	return NULL;
}


/*
   Create/delete/change/get qdisc.
 */

static int tc_ctl_qdisc(struct sk_buff *skb, struct nlmsghdr *n, void *arg)
{
	struct tcmsg *tcm = NLMSG_DATA(n);
	struct rtattr **tca = arg;
	struct device *dev;
	u32 clid = tcm->tcm_parent;
	struct Qdisc *old_q;
	struct Qdisc *q = NULL;
	struct Qdisc *p = NULL;
	struct Qdisc *leaf = NULL;
	struct Qdisc_ops *qops = NULL;
	int err;

	/* Find device */
	if ((dev = dev_get_by_index(tcm->tcm_ifindex)) == NULL)
		return -ENODEV;

	/* If parent is specified, it must exist
	   and tcm_parent selects a class in parent which
	   new qdisc will be attached to.

	   The place may be already busy by another qdisc,
	   remember this fact, if it was not auto-created discipline.
	 */
	if (clid) {
		if (clid != TC_H_ROOT) {
			p = qdisc_lookup(dev, TC_H_MAJ(clid));
			if (p == NULL)
				return -ENOENT;
			leaf = qdisc_lookup_class(dev, clid);
		} else
			leaf = dev->qdisc_sleeping;

		if (leaf && leaf->flags&TCQ_F_DEFAULT && n->nlmsg_type == RTM_NEWQDISC)
			leaf = NULL;

		/*
		   Also, leaf may be exactly that qdisc, which we want
		   to control. Remember this to avoid one more qdisc_lookup.
		 */

		if (leaf && leaf->handle == tcm->tcm_handle)
			q = leaf;
	}

	/* Try to locate the discipline */
	if (tcm->tcm_handle && q == NULL) {
		if (TC_H_MIN(tcm->tcm_handle))
			return -EINVAL;
		q = qdisc_lookup(dev, tcm->tcm_handle);
	}

	/* If discipline already exists, check that its real parent
	   matches to one selected by tcm_parent.
	 */
	   
	if (q) {
		if (clid && p != q->parent)
			return -EINVAL;
		BUG_TRAP(!leaf || leaf == q);
		if (tca[TCA_KIND-1] && rtattr_strcmp(tca[TCA_KIND-1], q->ops->id))
			return -EINVAL;
		clid = q->classid;
		goto process_existing;
	}

	/* The discipline is known not to exist.
	   If parent was not selected too, return error.
	 */
	if (clid == 0)
		return tcm->tcm_handle ? -ENOENT : -EINVAL;

	/* Check for the case when leaf is exactly the thing,
	   that you want.
	 */

	if (leaf && tcm->tcm_handle == 0) {
		q = leaf;
		if (!tca[TCA_KIND-1] || rtattr_strcmp(tca[TCA_KIND-1], q->ops->id) == 0)
			goto process_existing;
	}

	if (n->nlmsg_type != RTM_NEWQDISC || !(n->nlmsg_flags&NLM_F_CREATE))
		return -ENOENT;
	if (leaf && n->nlmsg_flags&NLM_F_EXCL)
		return -EEXIST;

create_and_graft:
	q = qdisc_create(dev, qops, tcm->tcm_handle, clid, tca, &err);
	if (q == NULL)
		return err;

graft:
	err = qdisc_graft(dev, p, clid, q, &old_q);
	if (err) {
		if (q)
			qdisc_destroy(q);
		return err;
	}
	qdisc_notify(skb, n, old_q, q);
	if (old_q)
		qdisc_destroy(old_q);
	return 0;

process_existing:

	switch (n->nlmsg_type) {
	case RTM_NEWQDISC:
		if (n->nlmsg_flags&NLM_F_EXCL)
			return -EEXIST;
		qops = q->ops;
		goto create_and_graft;
	case RTM_GETQDISC:	
		qdisc_notify(skb, n, NULL, q);
		return 0;
	case RTM_DELQDISC:
		q = NULL;
		goto graft;
	default:
		return -EINVAL;
	}
}

static int tc_fill_qdisc(struct sk_buff *skb, struct Qdisc *q,
			 u32 pid, u32 seq, unsigned flags, int event)
{
	struct tcmsg *tcm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*tcm));
	nlh->nlmsg_flags = flags;
	tcm = NLMSG_DATA(nlh);
	tcm->tcm_family = AF_UNSPEC;
	tcm->tcm_ifindex = q->dev ? q->dev->ifindex : 0;
	tcm->tcm_parent = q->classid;
	tcm->tcm_handle = q->handle;
	tcm->tcm_info = 0;
	RTA_PUT(skb, TCA_KIND, IFNAMSIZ, q->ops->id);
	if (q->ops->dump && q->ops->dump(q, skb) < 0)
		goto rtattr_failure;
	q->stats.qlen = q->q.qlen;
	RTA_PUT(skb, TCA_STATS, sizeof(q->stats), &q->stats);
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int qdisc_notify(struct sk_buff *oskb, struct nlmsghdr *n,
			 struct Qdisc *old, struct Qdisc *new)
{
	struct sk_buff *skb;
	u32 pid = oskb ? NETLINK_CB(oskb).pid : 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (old && !(old->flags&TCQ_F_DEFAULT)) {
		if (tc_fill_qdisc(skb, old, pid, n->nlmsg_seq, 0, RTM_DELQDISC) < 0)
			goto err_out;
	}
	if (new) {
		if (tc_fill_qdisc(skb, new, pid, n->nlmsg_seq, old ? NLM_F_REPLACE : 0, RTM_NEWQDISC) < 0)
			goto err_out;
	}

	if (skb->len)
		return rtnetlink_send(skb, pid, RTMGRP_TC, n->nlmsg_flags&NLM_F_ECHO);

err_out:
	kfree_skb(skb);
	return -EINVAL;
}

static int tc_dump_qdisc(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx, q_idx;
	int s_idx, s_q_idx;
	struct device *dev;
	struct Qdisc *q;

	s_idx = cb->args[0];
	s_q_idx = q_idx = cb->args[1];
	for (dev=dev_base, idx=0; dev; dev = dev->next, idx++) {
		if (idx < s_idx)
			continue;
		if (idx > s_idx)
			s_q_idx = 0;
		for (q = dev->qdisc_list, q_idx = 0; q;
		     q = q->next, q_idx++) {
			if (q_idx < s_q_idx)
				continue;
			if (tc_fill_qdisc(skb, q, NETLINK_CB(cb->skb).pid,
					  cb->nlh->nlmsg_seq, NLM_F_MULTI, RTM_NEWQDISC) <= 0)
				goto done;
		}
	}

done:
	cb->args[0] = idx;
	cb->args[1] = q_idx;

	return skb->len;
}



/************************************************
 *	Traffic classes manipulation.		*
 ************************************************/



static int tc_ctl_tclass(struct sk_buff *skb, struct nlmsghdr *n, void *arg)
{
	struct tcmsg *tcm = NLMSG_DATA(n);
	struct rtattr **tca = arg;
	struct device *dev;
	struct Qdisc *q = NULL;
	struct Qdisc_class_ops *cops;
	unsigned long cl = 0;
	unsigned long new_cl;
	u32 pid = tcm->tcm_parent;
	u32 clid = tcm->tcm_handle;
	u32 qid = TC_H_MAJ(clid);
	int err;

	if ((dev = dev_get_by_index(tcm->tcm_ifindex)) == NULL)
		return -ENODEV;

	/*
	   parent == TC_H_UNSPEC - unspecified parent.
	   parent == TC_H_ROOT   - class is root, which has no parent.
	   parent == X:0	 - parent is root class.
	   parent == X:Y	 - parent is a node in hierarchy.
	   parent == 0:Y	 - parent is X:Y, where X:0 is qdisc.

	   handle == 0:0	 - generate handle from kernel pool.
	   handle == 0:Y	 - class is X:Y, where X:0 is qdisc.
	   handle == X:Y	 - clear.
	   handle == X:0	 - root class.
	 */

	/* Step 1. Determine qdisc handle X:0 */

	if (pid != TC_H_ROOT) {
		u32 qid1 = TC_H_MAJ(pid);

		if (qid && qid1) {
			/* If both majors are known, they must be identical. */
			if (qid != qid1)
				return -EINVAL;
		} else if (qid1) {
			qid = qid1;
		} else if (qid == 0)
			qid = dev->qdisc_sleeping->handle;

		/* Now qid is genuine qdisc handle consistent
		   both with parent and child.

		   TC_H_MAJ(pid) still may be unspecified, complete it now.
		 */
		if (pid)
			pid = TC_H_MAKE(qid, pid);
	} else {
		if (qid == 0)
			qid = dev->qdisc_sleeping->handle;
	}

	/* OK. Locate qdisc */
	if ((q = qdisc_lookup(dev, qid)) == NULL) 
		return -ENOENT;

	/* An check that it supports classes */
	cops = q->ops->cl_ops;
	if (cops == NULL)
		return -EINVAL;

	/* Now try to get class */
	if (clid == 0) {
		if (pid == TC_H_ROOT)
			clid = qid;
	} else
		clid = TC_H_MAKE(qid, clid);

	if (clid)
		cl = cops->get(q, clid);

	if (cl == 0) {
		err = -ENOENT;
		if (n->nlmsg_type != RTM_NEWTCLASS || !(n->nlmsg_flags&NLM_F_CREATE))
			goto out;
	} else {
		switch (n->nlmsg_type) {
		case RTM_NEWTCLASS:	
			err = -EEXIST;
			if (n->nlmsg_flags&NLM_F_EXCL)
				goto out;
			break;
		case RTM_DELTCLASS:
			err = cops->delete(q, cl);
			if (err == 0)
				tclass_notify(skb, n, q, cl, RTM_DELTCLASS);
			goto out;
		case RTM_GETTCLASS:
			err = tclass_notify(skb, n, q, cl, RTM_NEWTCLASS);
			goto out;
		default:
			err = -EINVAL;
			goto out;
		}
	}

	new_cl = cl;
	err = cops->change(q, clid, pid, tca, &new_cl);
	if (err == 0)
		tclass_notify(skb, n, q, new_cl, RTM_NEWTCLASS);

out:
	if (cl)
		cops->put(q, cl);

	return err;
}


static int tc_fill_tclass(struct sk_buff *skb, struct Qdisc *q,
			  unsigned long cl,
			  u32 pid, u32 seq, unsigned flags, int event)
{
	struct tcmsg *tcm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*tcm));
	nlh->nlmsg_flags = flags;
	tcm = NLMSG_DATA(nlh);
	tcm->tcm_family = AF_UNSPEC;
	tcm->tcm_ifindex = q->dev ? q->dev->ifindex : 0;
	tcm->tcm_parent = q->handle;
	tcm->tcm_handle = q->handle;
	tcm->tcm_info = 0;
	RTA_PUT(skb, TCA_KIND, IFNAMSIZ, q->ops->id);
	if (q->ops->cl_ops->dump && q->ops->cl_ops->dump(q, cl, skb, tcm) < 0)
		goto rtattr_failure;
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int tclass_notify(struct sk_buff *oskb, struct nlmsghdr *n,
			  struct Qdisc *q, unsigned long cl, int event)
{
	struct sk_buff *skb;
	u32 pid = oskb ? NETLINK_CB(oskb).pid : 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tc_fill_tclass(skb, q, cl, pid, n->nlmsg_seq, 0, event) < 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	return rtnetlink_send(skb, pid, RTMGRP_TC, n->nlmsg_flags&NLM_F_ECHO);
}

struct qdisc_dump_args
{
	struct qdisc_walker w;
	struct sk_buff *skb;
	struct netlink_callback *cb;
};

static int qdisc_class_dump(struct Qdisc *q, unsigned long cl, struct qdisc_walker *arg)
{
	struct qdisc_dump_args *a = (struct qdisc_dump_args *)arg;

	return tc_fill_tclass(a->skb, q, cl, NETLINK_CB(a->cb->skb).pid,
			      a->cb->nlh->nlmsg_seq, NLM_F_MULTI, RTM_NEWTCLASS);
}

static int tc_dump_tclass(struct sk_buff *skb, struct netlink_callback *cb)
{
	int t;
	int s_t;
	struct device *dev;
	struct Qdisc *q;
	struct tcmsg *tcm = (struct tcmsg*)NLMSG_DATA(cb->nlh);
	struct qdisc_dump_args arg;

	if (cb->nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*tcm)))
		return 0;
	if ((dev = dev_get_by_index(tcm->tcm_ifindex)) == NULL)
		return 0;

	s_t = cb->args[0];

	for (q=dev->qdisc_list, t=0; q; q = q->next, t++) {
		if (t < s_t) continue;
		if (!q->ops->cl_ops) continue;
		if (tcm->tcm_parent && TC_H_MAJ(tcm->tcm_parent) != q->handle
		    && (tcm->tcm_parent != TC_H_ROOT || q->parent != NULL))
			continue;
		if (t > s_t)
			memset(&cb->args[1], 0, sizeof(cb->args)-sizeof(int));
		arg.w.fn = qdisc_class_dump;
		arg.skb = skb;
		arg.cb = cb;
		arg.w.stop  = 0;
		arg.w.skip = cb->args[1];
		arg.w.count = 0;
		q->ops->cl_ops->walk(q, &arg.w);
		cb->args[1] = arg.w.count;
		if (arg.w.stop)
			break;
	}

	cb->args[0] = t;

	return skb->len;
}
#endif

int psched_us_per_tick = 1;
int psched_tick_per_us = 1;

#ifdef CONFIG_PROC_FS
static int psched_read_proc(char *buffer, char **start, off_t offset,
			     int length, int *eof, void *data)
{
	int len;

	len = sprintf(buffer, "%08x %08x\n",
		      psched_tick_per_us, psched_us_per_tick);

	len -= offset;

	if (len > length)
		len = length;
	if(len < 0)
		len = 0;

	*start = buffer + offset;
	*eof = 1;

	return len;
}
#endif

psched_time_t psched_time_base;

#if PSCHED_CLOCK_SOURCE == PSCHED_CPU
psched_tdiff_t psched_clock_per_hz;
int psched_clock_scale;
#endif

#ifdef PSCHED_WATCHER
PSCHED_WATCHER psched_time_mark;

static void psched_tick(unsigned long);

static struct timer_list psched_timer =
	{ NULL, NULL, 0, 0L, psched_tick };

static void psched_tick(unsigned long dummy)
{
#if PSCHED_CLOCK_SOURCE == PSCHED_CPU
	psched_time_t dummy_stamp;
	PSCHED_GET_TIME(dummy_stamp);
	psched_timer.expires = jiffies + 4*HZ;
#else
	unsigned long now = jiffies;
	psched_time_base = ((u64)now)<<PSCHED_JSCALE;
	psched_time_mark = now;
	psched_timer.expires = now + 60*60*HZ;
#endif
	add_timer(&psched_timer);
}
#endif

#if PSCHED_CLOCK_SOURCE == PSCHED_CPU
__initfunc(int psched_calibrate_clock(void))
{
	psched_time_t stamp, stamp1;
	struct timeval tv, tv1;
	psched_tdiff_t delay;
	long rdelay;
	unsigned long stop;

#if CPU == 586 || CPU == 686
	if (!(boot_cpu_data.x86_capability & X86_FEATURE_TSC))
		return -1;
#endif

	start_bh_atomic();
#ifdef PSCHED_WATCHER
	psched_tick(0);
#endif
	stop = jiffies + HZ/10;
	PSCHED_GET_TIME(stamp);
	do_gettimeofday(&tv);
	while (time_before(jiffies, stop))
		barrier();
	PSCHED_GET_TIME(stamp1);
	do_gettimeofday(&tv1);
	end_bh_atomic();

	delay = PSCHED_TDIFF(stamp1, stamp);
	rdelay = tv1.tv_usec - tv.tv_usec;
	rdelay += (tv1.tv_sec - tv.tv_sec)*1000000;
	if (rdelay > delay)
		return -1;
	delay /= rdelay;
	psched_tick_per_us = delay;
	while ((delay>>=1) != 0)
		psched_clock_scale++;
	psched_us_per_tick = 1<<psched_clock_scale;
	psched_clock_per_hz = (psched_tick_per_us*(1000000/HZ))>>psched_clock_scale;
	return 0;
}
#endif

__initfunc(int pktsched_init(void))
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *ent;
#endif

#if PSCHED_CLOCK_SOURCE == PSCHED_CPU
	if (psched_calibrate_clock() < 0)
		return -1;
#elif PSCHED_CLOCK_SOURCE == PSCHED_JIFFIES
	psched_tick_per_us = HZ<<PSCHED_JSCALE;
	psched_us_per_tick = 1000000;
#endif

#ifdef CONFIG_RTNETLINK
	struct rtnetlink_link *link_p = rtnetlink_links[PF_UNSPEC];

	/* Setup rtnetlink links. It is made here to avoid
	   exporting large number of public symbols.
	 */

	if (link_p) {
		link_p[RTM_NEWQDISC-RTM_BASE].doit = tc_ctl_qdisc;
		link_p[RTM_DELQDISC-RTM_BASE].doit = tc_ctl_qdisc;
		link_p[RTM_GETQDISC-RTM_BASE].doit = tc_ctl_qdisc;
		link_p[RTM_GETQDISC-RTM_BASE].dumpit = tc_dump_qdisc;
		link_p[RTM_NEWTCLASS-RTM_BASE].doit = tc_ctl_tclass;
		link_p[RTM_DELTCLASS-RTM_BASE].doit = tc_ctl_tclass;
		link_p[RTM_GETTCLASS-RTM_BASE].doit = tc_ctl_tclass;
		link_p[RTM_GETTCLASS-RTM_BASE].dumpit = tc_dump_tclass;
	}
#endif

#define INIT_QDISC(name) { \
          extern struct Qdisc_ops name##_qdisc_ops; \
          register_qdisc(&##name##_qdisc_ops); \
	}

	INIT_QDISC(pfifo);
	INIT_QDISC(bfifo);

#ifdef CONFIG_NET_SCH_CBQ
	INIT_QDISC(cbq);
#endif
#ifdef CONFIG_NET_SCH_CSZ
	INIT_QDISC(csz);
#endif
#ifdef CONFIG_NET_SCH_HPFQ
	INIT_QDISC(hpfq);
#endif
#ifdef CONFIG_NET_SCH_HFSC
	INIT_QDISC(hfsc);
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
#ifdef CONFIG_NET_SCH_TEQL
	teql_init();
#endif
#ifdef CONFIG_NET_SCH_PRIO
	INIT_QDISC(prio);
#endif
#ifdef CONFIG_NET_CLS
	tc_filter_init();
#endif

#ifdef CONFIG_PROC_FS
	ent = create_proc_entry("net/psched", 0, 0);
	ent->read_proc = psched_read_proc;
#endif

	return 0;
}
