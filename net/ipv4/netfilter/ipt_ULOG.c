/*
 * netfilter module for userspace packet logging daemons
 *
 * (C) 2000-2002 by Harald Welte <laforge@gnumonks.org>
 *
 * 2000/09/22 ulog-cprange feature added
 * 2001/01/04 in-kernel queue as proposed by Sebastian Zander 
 * 						<zander@fokus.gmd.de>
 * 2001/01/30 per-rule nlgroup conflicts with global queue. 
 *            nlgroup now global (sysctl)
 * 2001/04/19 ulog-queue reworked, now fixed buffer size specified at
 * 	      module loadtime -HW
 *
 * Released under the terms of the GPL
 *
 * This module accepts two parameters: 
 * 
 * nlbufsiz:
 *   The parameter specifies how big the buffer for each netlink multicast
 * group is. e.g. If you say nlbufsiz=8192, up to eight kb of packets will
 * get accumulated in the kernel until they are sent to userspace. It is
 * NOT possible to allocate more than 128kB, and it is strongly discouraged,
 * because atomically allocating 128kB inside the network rx softirq is not
 * reliable. Please also keep in mind that this buffer size is allocated for
 * each nlgroup you are using, so the total kernel memory usage increases
 * by that factor.
 *
 * flushtimeout:
 *   Specify, after how many clock ticks (intel: 100 per second) the queue
 * should be flushed even if it is not full yet.
 *
 * ipt_ULOG.c,v 1.18 2002/04/16 07:33:00 laforge Exp
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/netlink.h>
#include <linux/netdevice.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_ULOG.h>
#include <linux/netfilter_ipv4/lockhelp.h>
#include <net/sock.h>

MODULE_LICENSE("GPL");

#define ULOG_NL_EVENT		111		/* Harald's favorite number */
#define ULOG_MAXNLGROUPS	32		/* numer of nlgroups */

#if 0
#define DEBUGP(format, args...)	printk(__FILE__ ":" __FUNCTION__ ":" \
				       format, ## args)
#else
#define DEBUGP(format, args...)
#endif

#define PRINTR(format, args...) do { if (net_ratelimit()) printk(format, ## args); } while (0)

MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("IP tables userspace logging module");


static unsigned int nlbufsiz = 4096;
MODULE_PARM(nlbufsiz, "i");
MODULE_PARM_DESC(nlbufsiz, "netlink buffer size");

static unsigned int flushtimeout = 10 * HZ;
MODULE_PARM(flushtimeout, "i");
MODULE_PARM_DESC(flushtimeout, "buffer flush timeout");

/* global data structures */

typedef struct {
	unsigned int qlen;		/* number of nlmsgs' in the skb */
	struct nlmsghdr *lastnlh;	/* netlink header of last msg in skb */
	struct sk_buff *skb;		/* the pre-allocated skb */
	struct timer_list timer;	/* the timer function */
} ulog_buff_t;

static ulog_buff_t ulog_buffers[ULOG_MAXNLGROUPS];	/* array of buffers */

static struct sock *nflognl;	/* our socket */
static size_t qlen;		/* current length of multipart-nlmsg */
DECLARE_LOCK(ulog_lock);	/* spinlock */

/* send one ulog_buff_t to userspace */
static void ulog_send(unsigned int nlgroup)
{
	ulog_buff_t *ub = &ulog_buffers[nlgroup];

	if (timer_pending(&ub->timer)) {
		DEBUGP("ipt_ULOG: ulog_send: timer was pending, deleting\n");
		del_timer(&ub->timer);
	}

	/* last nlmsg needs NLMSG_DONE */
	if (ub->qlen > 1)
		ub->lastnlh->nlmsg_type = NLMSG_DONE;

	NETLINK_CB(ub->skb).dst_groups = nlgroup;
	DEBUGP("ipt_ULOG: throwing %d packets to netlink mask %u\n",
		ub->qlen, nlgroup);
	netlink_broadcast(nflognl, ub->skb, 0, nlgroup, GFP_ATOMIC);

	ub->qlen = 0;
	ub->skb = NULL;
	ub->lastnlh = NULL;

}


/* timer function to flush queue in ULOG_FLUSH_INTERVAL time */
static void ulog_timer(unsigned long data)
{
	DEBUGP("ipt_ULOG: timer function called, calling ulog_send\n");

	/* lock to protect against somebody modifying our structure
	 * from ipt_ulog_target at the same time */
	LOCK_BH(&ulog_lock);
	ulog_send(data);
	UNLOCK_BH(&ulog_lock);
}

static void nflog_rcv(struct sock *sk, int len)
{
	printk("ipt_ULOG:nflog_rcv() did receive netlink message ?!?\n");
}

struct sk_buff *ulog_alloc_skb(unsigned int size)
{
	struct sk_buff *skb;

	/* alloc skb which should be big enough for a whole
	 * multipart message. WARNING: has to be <= 131000
	 * due to slab allocator restrictions */

	skb = alloc_skb(nlbufsiz, GFP_ATOMIC);
	if (!skb) {
		PRINTR("ipt_ULOG: can't alloc whole buffer %ub!\n",
			nlbufsiz);

		/* try to allocate only as much as we need for 
		 * current packet */

		skb = alloc_skb(size, GFP_ATOMIC);
		if (!skb)
			PRINTR("ipt_ULOG: can't even allocate %ub\n", size);
	}

	return skb;
}

static unsigned int ipt_ulog_target(struct sk_buff **pskb,
				    unsigned int hooknum,
				    const struct net_device *in,
				    const struct net_device *out,
				    const void *targinfo, void *userinfo)
{
	ulog_buff_t *ub;
	ulog_packet_msg_t *pm;
	size_t size, copy_len;
	struct nlmsghdr *nlh;
	struct ipt_ulog_info *loginfo = (struct ipt_ulog_info *) targinfo;

	/* calculate the size of the skb needed */
	if ((loginfo->copy_range == 0) ||
	    (loginfo->copy_range > (*pskb)->len)) {
		copy_len = (*pskb)->len;
	} else {
		copy_len = loginfo->copy_range;
	}

	size = NLMSG_SPACE(sizeof(*pm) + copy_len);

	ub = &ulog_buffers[loginfo->nl_group];
	
	LOCK_BH(&ulog_lock);

	if (!ub->skb) {
		if (!(ub->skb = ulog_alloc_skb(size)))
			goto alloc_failure;
	} else if (ub->qlen >= loginfo->qthreshold ||
		   size > skb_tailroom(ub->skb)) {
		/* either the queue len is too high or we don't have 
		 * enough room in nlskb left. send it to userspace. */

		ulog_send(loginfo->nl_group);

		if (!(ub->skb = ulog_alloc_skb(size)))
			goto alloc_failure;
	}

	DEBUGP("ipt_ULOG: qlen %d, qthreshold %d\n", ub->qlen, 
		loginfo->qthreshold);

	/* NLMSG_PUT contains a hidden goto nlmsg_failure !!! */
	nlh = NLMSG_PUT(ub->skb, 0, ub->qlen, ULOG_NL_EVENT, 
			size - sizeof(*nlh));
	ub->qlen++;

	pm = NLMSG_DATA(nlh);

	/* copy hook, prefix, timestamp, payload, etc. */
	pm->data_len = copy_len;
	pm->timestamp_sec = (*pskb)->stamp.tv_sec;
	pm->timestamp_usec = (*pskb)->stamp.tv_usec;
	pm->mark = (*pskb)->nfmark;
	pm->hook = hooknum;
	if (loginfo->prefix[0] != '\0')
		strncpy(pm->prefix, loginfo->prefix, sizeof(pm->prefix));
	else
		*(pm->prefix) = '\0';

	if (in && in->hard_header_len > 0
	    && (*pskb)->mac.raw != (void *) (*pskb)->nh.iph
	    && in->hard_header_len <= ULOG_MAC_LEN) {
		memcpy(pm->mac, (*pskb)->mac.raw, in->hard_header_len);
		pm->mac_len = in->hard_header_len;
	}

	if (in)
		strncpy(pm->indev_name, in->name, sizeof(pm->indev_name));
	else
		pm->indev_name[0] = '\0';

	if (out)
		strncpy(pm->outdev_name, out->name, sizeof(pm->outdev_name));
	else
		pm->outdev_name[0] = '\0';

	if (copy_len)
		memcpy(pm->payload, (*pskb)->data, copy_len);
	
	/* check if we are building multi-part messages */
	if (ub->qlen > 1) {
		ub->lastnlh->nlmsg_flags |= NLM_F_MULTI;
	}

	/* if threshold is reached, send message to userspace */
	if (qlen >= loginfo->qthreshold) {
		if (loginfo->qthreshold > 1)
			nlh->nlmsg_type = NLMSG_DONE;
	}

	ub->lastnlh = nlh;

	/* if timer isn't already running, start it */
	if (!timer_pending(&ub->timer)) {
		ub->timer.expires = jiffies + flushtimeout;
		add_timer(&ub->timer);
	}

	UNLOCK_BH(&ulog_lock);

	return IPT_CONTINUE;


nlmsg_failure:
	PRINTR("ipt_ULOG: error during NLMSG_PUT\n");

alloc_failure:
	PRINTR("ipt_ULOG: Error building netlink message\n");

	UNLOCK_BH(&ulog_lock);

	return IPT_CONTINUE;
}

static int ipt_ulog_checkentry(const char *tablename,
			       const struct ipt_entry *e,
			       void *targinfo,
			       unsigned int targinfosize,
			       unsigned int hookmask)
{
	struct ipt_ulog_info *loginfo = (struct ipt_ulog_info *) targinfo;

	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_ulog_info))) {
		DEBUGP("ipt_ULOG: targinfosize %u != 0\n", targinfosize);
		return 0;
	}

	if (loginfo->prefix[sizeof(loginfo->prefix) - 1] != '\0') {
		DEBUGP("ipt_ULOG: prefix term %i\n",
		       loginfo->prefix[sizeof(loginfo->prefix) - 1]);
		return 0;
	}

	if (loginfo->qthreshold > ULOG_MAX_QLEN) {
		DEBUGP("ipt_ULOG: queue threshold %i > MAX_QLEN\n",
			loginfo->qthreshold);
		return 0;
	}

	return 1;
}

static struct ipt_target ipt_ulog_reg =
    { {NULL, NULL}, "ULOG", ipt_ulog_target, ipt_ulog_checkentry, NULL,
THIS_MODULE
};

static int __init init(void)
{
	int i;

	DEBUGP("ipt_ULOG: init module\n");

	if (nlbufsiz >= 128*1024) {
		printk("Netlink buffer has to be <= 128kB\n");
		return -EINVAL;
	}

	/* initialize ulog_buffers */
	for (i = 0; i < ULOG_MAXNLGROUPS; i++) {
		memset(&ulog_buffers[i], 0, sizeof(ulog_buff_t));
		init_timer(&ulog_buffers[i].timer);
		ulog_buffers[i].timer.function = ulog_timer;
		ulog_buffers[i].timer.data = i;
	}

	nflognl = netlink_kernel_create(NETLINK_NFLOG, nflog_rcv);
	if (!nflognl)
		return -ENOMEM;

	if (ipt_register_target(&ipt_ulog_reg) != 0) {
		sock_release(nflognl->socket);
		return -EINVAL;
	}

	return 0;
}

static void __exit fini(void)
{
	ulog_buff_t *ub;
	int i;

	DEBUGP("ipt_ULOG: cleanup_module\n");

	ipt_unregister_target(&ipt_ulog_reg);
	sock_release(nflognl->socket);

	/* remove pending timers and free allocated skb's */
	for (i = 0; i < ULOG_MAXNLGROUPS; i++) {
		ub = &ulog_buffers[i];
		if (timer_pending(&ub->timer)) {
			DEBUGP("timer was pending, deleting\n");
			del_timer(&ub->timer);
		}

		if (ub->skb) {
			kfree_skb(ub->skb);
			ub->skb = NULL;
		}
	}

}

module_init(init);
module_exit(fini);
