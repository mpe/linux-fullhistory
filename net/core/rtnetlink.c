/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Routing netlink socket interface: protocol independent part.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/string.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/pkt_sched.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/sock.h>

atomic_t rtnl_rlockct;
struct wait_queue *rtnl_wait;


void rtnl_lock()
{
	rtnl_shlock();
	rtnl_exlock();
}

void rtnl_unlock()
{
	rtnl_exunlock();
	rtnl_shunlock();
}

#ifdef CONFIG_RTNETLINK
struct sock *rtnl;

unsigned long rtnl_wlockct;

struct rtnetlink_link * rtnetlink_links[NPROTO];

#define _S	1	/* superuser privileges required */
#define _X	2	/* exclusive access to tables required */
#define _G	4	/* GET request */

static unsigned char rtm_properties[RTM_MAX-RTM_BASE+1] =
{
	_S|_X,		/* RTM_NEWLINK */
	_S|_X,		/* RTM_DELLINK */
	_G,		/* RTM_GETLINK */
	0,

	_S|_X,		/* RTM_NEWADDR */
	_S|_X,		/* RTM_DELADDR */
	_G,		/* RTM_GETADDR */
	0,

	_S|_X,		/* RTM_NEWROUTE */
	_S|_X,		/* RTM_DELROUTE */
	_G,		/* RTM_GETROUTE */
	0,

	_S|_X,		/* RTM_NEWNEIGH */
	_S|_X,		/* RTM_DELNEIGH */
	_G,		/* RTM_GETNEIGH */
	0,

	_S|_X, 		/* RTM_NEWRULE */
	_S|_X,		/* RTM_DELRULE */
	_G,		/* RTM_GETRULE */
	0
};

static int rtnetlink_get_rta(struct kern_rta *rta, struct rtattr *attr, int attrlen)
{
	void **rta_data = (void**)rta;

	while (RTA_OK(attr, attrlen)) {
		int type = attr->rta_type;
		if (type != RTA_UNSPEC) {
			if (type > RTA_MAX)
				return -EINVAL;
			rta_data[type-1] = RTA_DATA(attr);
		}
		attr = RTA_NEXT(attr, attrlen);
	}
	return 0;
}

static int rtnetlink_get_ifa(struct kern_ifa *ifa, struct rtattr *attr, int attrlen)
{
	void **ifa_data = (void**)ifa;

	while (RTA_OK(attr, attrlen)) {
		int type = attr->rta_type;
		if (type != IFA_UNSPEC) {
			if (type > IFA_MAX)
				return -EINVAL;
			ifa_data[type-1] = RTA_DATA(attr);
		}
		attr = RTA_NEXT(attr, attrlen);
	}
	return 0;
}

static int rtnetlink_get_ga(struct rtattr **rta, int sz,
			    struct rtattr *attr, int attrlen)
{
	while (RTA_OK(attr, attrlen)) {
		int type = attr->rta_type;
		if (type > 0) {
			if (type > sz)
				return -EINVAL;
			rta[type-1] = attr;
		}
		attr = RTA_NEXT(attr, attrlen);
	}
	return 0;
}


void __rta_fill(struct sk_buff *skb, int attrtype, int attrlen, const void *data)
{
	struct rtattr *rta;
	int size = RTA_LENGTH(attrlen);

	rta = (struct rtattr*)skb_put(skb, RTA_ALIGN(size));
	rta->rta_type = attrtype;
	rta->rta_len = size;
	memcpy(RTA_DATA(rta), data, attrlen);
}

static int rtnetlink_fill_ifinfo(struct sk_buff *skb, struct device *dev,
				 int type, pid_t pid, u32 seq)
{
	struct ifinfomsg *r;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, type, sizeof(*r));
	if (pid) nlh->nlmsg_flags |= NLM_F_MULTI;
	r = NLMSG_DATA(nlh);
	r->ifi_addrlen = dev->addr_len;
	r->ifi_address.sa_family = dev->type;
	memcpy(&r->ifi_address.sa_data, dev->dev_addr, dev->addr_len);
	r->ifi_broadcast.sa_family = dev->type;
	memcpy(&r->ifi_broadcast.sa_data, dev->broadcast, dev->addr_len);
	r->ifi_flags = dev->flags;
	r->ifi_mtu = dev->mtu;
	r->ifi_index = dev->ifindex;
	r->ifi_link = dev->iflink;
	strncpy(r->ifi_name, dev->name, IFNAMSIZ-1);
	r->ifi_qdiscname[0] = 0;
	r->ifi_qdisc = dev->qdisc_sleeping->handle;
	if (dev->qdisc_sleeping->ops)
		strcpy(r->ifi_qdiscname, dev->qdisc_sleeping->ops->id);
	if (dev->get_stats) {
		struct net_device_stats *stats = dev->get_stats(dev);
		if (stats)
			RTA_PUT(skb, IFLA_STATS, sizeof(*stats), stats);
	}
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_put(skb, b - skb->tail);
	return -1;
}

int rtnetlink_dump_ifinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx;
	int s_idx = cb->args[0];
	struct device *dev;

	for (dev=dev_base, idx=0; dev; dev = dev->next, idx++) {
		if (idx < s_idx)
			continue;
		if (rtnetlink_fill_ifinfo(skb, dev, RTM_NEWLINK, NETLINK_CB(cb->skb).pid, cb->nlh->nlmsg_seq) <= 0)
			break;
	}
	cb->args[0] = idx;

	return skb->len;
}

int rtnetlink_dump_all(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx;
	int s_idx = cb->family;

	if (s_idx == 0)
		s_idx = 1;
	for (idx=1; idx<NPROTO; idx++) {
		int type = cb->nlh->nlmsg_type-RTM_BASE;
		if (idx < s_idx || idx == AF_PACKET)
			continue;
		if (rtnetlink_links[idx] == NULL ||
		    rtnetlink_links[idx][type].dumpit == NULL)
			continue;
		if (idx > s_idx)
			memset(&cb->args[0], 0, sizeof(cb->args));
		if (rtnetlink_links[idx][type].dumpit(skb, cb) == 0)
			continue;
		if (skb_tailroom(skb) < 256)
			break;
	}
	cb->family = idx;

	return skb->len;
}


void rtmsg_ifinfo(int type, struct device *dev)
{
	struct sk_buff *skb;
	int size = NLMSG_SPACE(sizeof(struct ifinfomsg)+
			       RTA_LENGTH(sizeof(struct net_device_stats)));

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return;

	if (rtnetlink_fill_ifinfo(skb, dev, type, 0, 0) < 0) {
		kfree_skb(skb, 0);
		return;
	}
	NETLINK_CB(skb).dst_groups = RTMGRP_LINK;
	netlink_broadcast(rtnl, skb, 0, RTMGRP_LINK, GFP_KERNEL);
}

static int rtnetlink_done(struct netlink_callback *cb)
{
	if (NETLINK_CREDS(cb->skb)->uid == 0 && cb->nlh->nlmsg_flags&NLM_F_ATOMIC)
		rtnl_shunlock();
	return 0;
}

/* Process one rtnetlink message. */

extern __inline__ int
rtnetlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh, int *errp)
{
	union {
		struct kern_rta rta;
		struct kern_ifa ifa;
		struct rtattr	*ga[RTA_MAX-1];
	} u;
	struct rtmsg *rtm;
	struct ifaddrmsg *ifm;
	struct ndmsg *ndm;
	int exclusive = 0;
	int family;
	int type;
	int err;

	if (!(nlh->nlmsg_flags&NLM_F_REQUEST))
		return 0;
	type = nlh->nlmsg_type;
	if (type < RTM_BASE)
		return 0;
	if (type > RTM_MAX)
		goto err_inval;

	if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct rtgenmsg)))
		return 0;
	family = ((struct rtgenmsg*)NLMSG_DATA(nlh))->rtgen_family;
	if (family > NPROTO || rtnetlink_links[family] == NULL) {
		*errp = -EAFNOSUPPORT;
		return -1;
	}
	if (rtm_properties[type-RTM_BASE]&_S) {
		if (NETLINK_CREDS(skb)->uid) {
			*errp = -EPERM;
			return -1;
		}
	}
	if (rtm_properties[type-RTM_BASE]&_G && nlh->nlmsg_flags&NLM_F_DUMP) {
		if (rtnetlink_links[family][type-RTM_BASE].dumpit == NULL)
			goto err_inval;

		/* Super-user locks all the tables to get atomic snapshot */
		if (NETLINK_CREDS(skb)->uid == 0 && nlh->nlmsg_flags&NLM_F_ATOMIC)
			atomic_inc(&rtnl_rlockct);
		if ((*errp = netlink_dump_start(rtnl, skb, nlh,
						rtnetlink_links[family][type-RTM_BASE].dumpit,
						rtnetlink_done)) != 0) {
			if (NETLINK_CREDS(skb)->uid == 0 && nlh->nlmsg_flags&NLM_F_ATOMIC)
				atomic_dec(&rtnl_rlockct);
			return -1;
		}
		skb_pull(skb, NLMSG_ALIGN(nlh->nlmsg_len));
		return -1;
	}
	if (rtm_properties[type-RTM_BASE]&_X) {
		if (rtnl_exlock_nowait()) {
			*errp = 0;
			return -1;
		}
		exclusive = 1;
	}
	
	memset(&u, 0, sizeof(u));

	switch (nlh->nlmsg_type) {
	case RTM_NEWROUTE:
	case RTM_DELROUTE:
	case RTM_GETROUTE:
	case RTM_NEWRULE:
	case RTM_DELRULE:
	case RTM_GETRULE:
		rtm = NLMSG_DATA(nlh);
		if (nlh->nlmsg_len < sizeof(*rtm))
			goto err_inval;

		if (rtm->rtm_optlen &&
		    rtnetlink_get_rta(&u.rta, RTM_RTA(rtm), rtm->rtm_optlen) < 0)
			goto err_inval;
		break;

	case RTM_NEWADDR:
	case RTM_DELADDR:
	case RTM_GETADDR:
		ifm = NLMSG_DATA(nlh);
		if (nlh->nlmsg_len < sizeof(*ifm))
			goto err_inval;

		if (nlh->nlmsg_len > NLMSG_LENGTH(sizeof(*ifm)) &&
		    rtnetlink_get_ifa(&u.ifa, IFA_RTA(ifm),
				      nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifm))) < 0)
			goto err_inval;
		break;
	case RTM_NEWNEIGH:
	case RTM_DELNEIGH:
	case RTM_GETNEIGH:
		ndm = NLMSG_DATA(nlh);
		if (nlh->nlmsg_len < sizeof(*ndm))
			goto err_inval;

		if (nlh->nlmsg_len > NLMSG_LENGTH(sizeof(*ndm)) &&
		    rtnetlink_get_ga(u.ga, NDA_MAX, NDA_RTA(ndm),
				      nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ndm))) < 0)
			goto err_inval;
		break;

	case RTM_NEWLINK:
	case RTM_DELLINK:
	case RTM_GETLINK:
		/* Not urgent and even not necessary */
	default:
		goto err_inval;
	}

	if (rtnetlink_links[family][type-RTM_BASE].doit == NULL)
		goto err_inval;
	err = rtnetlink_links[family][type-RTM_BASE].doit(skb, nlh, (void *)&u);

	if (exclusive)
		rtnl_exunlock();
	*errp = err;
	return err;

err_inval:
	if (exclusive)
		rtnl_exunlock();
	*errp = -EINVAL;
	return -1;
}

/* 
 * Process one packet of messages.
 * Malformed skbs with wrong lengths of messages are discarded silently.
 */

extern __inline__ int rtnetlink_rcv_skb(struct sk_buff *skb)
{
	int err;
	struct nlmsghdr * nlh;

	while (skb->len >= NLMSG_SPACE(0)) {
		int rlen;

		nlh = (struct nlmsghdr *)skb->data;
		if (nlh->nlmsg_len < sizeof(*nlh) || skb->len < nlh->nlmsg_len)
			return 0;
		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;
		if (rtnetlink_rcv_msg(skb, nlh, &err)) {
			/* Not error, but we must interrupt processing here:
			 *   Note, that in this case we do not pull message
			 *   from skb, it will be processed later.
			 */
			if (err == 0)
				return -1;
			netlink_ack(skb, nlh, err);
		} else if (nlh->nlmsg_flags&NLM_F_ACK)
			netlink_ack(skb, nlh, 0);
		skb_pull(skb, rlen);
	}

	return 0;
}

/*
 *  rtnetlink input queue processing routine:
 *	- try to acquire shared lock. If it is failed, defer processing.
 *	- feed skbs to rtnetlink_rcv_skb, until it refuse a message,
 *	  that will occur, when a dump started and/or acquisition of
 *	  exclusive lock failed.
 */

static void rtnetlink_rcv(struct sock *sk, int len)
{
	struct sk_buff *skb;

	if (rtnl_shlock_nowait())
		return;

	while ((skb = skb_dequeue(&sk->receive_queue)) != NULL) {
		if (rtnetlink_rcv_skb(skb)) {
			if (skb->len)
				skb_queue_head(&sk->receive_queue, skb);
			else
				kfree_skb(skb, FREE_READ);
			break;
		}
		kfree_skb(skb, FREE_READ);
	}

	rtnl_shunlock();
}

static struct rtnetlink_link link_rtnetlink_table[RTM_MAX-RTM_BASE+1] =
{
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			rtnetlink_dump_ifinfo,	},
	{ NULL,			NULL,			},

	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			rtnetlink_dump_all,	},
	{ NULL,			NULL,			},

	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			rtnetlink_dump_all,	},
	{ NULL,			NULL,			},

	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			neigh_dump_info,	},
	{ NULL,			NULL,			},

	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
};


static int rtnetlink_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev = ptr;
	switch (event) {
	case NETDEV_UNREGISTER:
		rtmsg_ifinfo(RTM_DELLINK, dev);
		break;
	default:
		rtmsg_ifinfo(RTM_NEWLINK, dev);
		break;
	}
	return NOTIFY_DONE;
}

struct notifier_block rtnetlink_dev_notifier = {
	rtnetlink_event,
	NULL,
	0
};


__initfunc(void rtnetlink_init(void))
{
#ifdef RTNL_DEBUG
	printk("Initializing RT netlink socket\n");
#endif
	rtnl = netlink_kernel_create(NETLINK_ROUTE, rtnetlink_rcv);
	if (rtnl == NULL)
		panic("rtnetlink_init: cannot initialize rtnetlink\n");
	register_netdevice_notifier(&rtnetlink_dev_notifier);
	rtnetlink_links[AF_UNSPEC] = link_rtnetlink_table;
	rtnetlink_links[AF_PACKET] = link_rtnetlink_table;
}



#endif
