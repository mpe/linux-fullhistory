/*
 * net/sched/cls_route.c	Routing table based packet classifier.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

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


static int route_classify(struct sk_buff *skb, struct tcf_proto *tp,
			  struct tcf_result *res)
{
	struct dst_entry *dst = skb->dst;

	if (dst) {
		u32 clid = dst->tclassid;

		if (clid && (TC_H_MAJ(clid) == 0 ||
			     !(TC_H_MAJ(clid^tp->q->handle)))) {
			res->classid = clid;
			res->class = 0;
			return 0;
		}
	}
	return -1;
}

static unsigned long route_get(struct tcf_proto *tp, u32 handle)
{
	return 0;
}

static void route_put(struct tcf_proto *tp, unsigned long f)
{
}

static int route_init(struct tcf_proto *tp)
{
	return 0;
}

static void route_destroy(struct tcf_proto *tp)
{
}

static int route_delete(struct tcf_proto *tp, unsigned long arg)
{
	return -EINVAL;
}

static int route_change(struct tcf_proto *tp, u32 handle,
			struct rtattr **tca,
			unsigned long *arg)
{
	return handle ? -EINVAL : 0;
}

struct tcf_proto_ops cls_route_ops = {
	NULL,
	"route",
	route_classify,
	route_init,
	route_destroy,

	route_get,
	route_put,
	route_change,
	route_delete,
	NULL,
};
