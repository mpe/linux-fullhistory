/* netfilter.c: look after the filters for various protocols. 
 * Heavily influenced by the old firewall.c by David Bonn and Alan Cox.
 *
 * Thanks to Rob `CmdrTaco' Malda for not influencing this code in any
 * way.
 *
 * Rusty Russell (C)1998 -- This code is GPL.
 */
#include <linux/config.h>
#include <linux/netfilter.h>
#include <net/protocol.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

/* In this code, we can be waiting indefinitely for userspace to
 * service a packet if a hook returns NF_QUEUE.  We could keep a count
 * of skbuffs queued for userspace, and not deregister a hook unless
 * this is zero, but that sucks.  Now, we simply check when the
 * packets come back: if the hook is gone, the packet is discarded. */
#ifdef CONFIG_NETFILTER_DEBUG
#define NFDEBUG(format, args...)  printk(format , ## args)
#else
#define NFDEBUG(format, args...)
#endif

/* Each queued (to userspace) skbuff has one of these. */
struct nf_info
{
	/* The ops struct which sent us to userspace. */
	struct nf_hook_ops *elem;

	/* If we're sent to userspace, this keeps housekeeping info */
	int pf;
	unsigned long mark;
	unsigned int hook;
	struct net_device *indev, *outdev;
	int (*okfn)(struct sk_buff *);
};

static rwlock_t nf_lock = RW_LOCK_UNLOCKED;
static DECLARE_MUTEX(nf_sockopt_mutex);

struct list_head nf_hooks[NPROTO][NF_MAX_HOOKS];
static LIST_HEAD(nf_sockopts);
static LIST_HEAD(nf_interested);

int nf_register_hook(struct nf_hook_ops *reg)
{
	struct list_head *i;

#ifdef CONFIG_NETFILTER_DEBUG
	if (reg->pf<0 || reg->pf>=NPROTO || reg->hooknum >= NF_MAX_HOOKS) {
		NFDEBUG("nf_register_hook: bad vals: pf=%i, hooknum=%u.\n",
			reg->pf, reg->hooknum);
		return -EINVAL;
	}
#endif
	NFDEBUG("nf_register_hook: pf=%i hook=%u.\n", reg->pf, reg->hooknum);
	
	write_lock_bh(&nf_lock);
	for (i = nf_hooks[reg->pf][reg->hooknum].next; 
	     i != &nf_hooks[reg->pf][reg->hooknum]; 
	     i = i->next) {
		if (reg->priority < ((struct nf_hook_ops *)i)->priority)
			break;
	}
	list_add(&reg->list, i->prev);
	write_unlock_bh(&nf_lock);
	return 0;
}

void nf_unregister_hook(struct nf_hook_ops *reg)
{
#ifdef CONFIG_NETFILTER_DEBUG
	if (reg->pf<0 || reg->pf>=NPROTO || reg->hooknum >= NF_MAX_HOOKS) {
		NFDEBUG("nf_unregister_hook: bad vals: pf=%i, hooknum=%u.\n",
			reg->pf, reg->hooknum);
		return;
	}
#endif
	write_lock_bh(&nf_lock);
	list_del(&reg->list);
	write_unlock_bh(&nf_lock);
}

/* Do exclusive ranges overlap? */
static inline int overlap(int min1, int max1, int min2, int max2)
{
	return (min1 >= min2 && min1 < max2)
		|| (max1 > min2 && max1 <= max2);
}

/* Functions to register sockopt ranges (exclusive). */
int nf_register_sockopt(struct nf_sockopt_ops *reg)
{
	struct list_head *i;
	int ret = 0;

#ifdef CONFIG_NETFILTER_DEBUG
	if (reg->pf<0 || reg->pf>=NPROTO) {
		NFDEBUG("nf_register_sockopt: bad val: pf=%i.\n", reg->pf);
		return -EINVAL;
	}
	if (reg->set_optmin > reg->set_optmax) {
		NFDEBUG("nf_register_sockopt: bad set val: min=%i max=%i.\n", 
			reg->set_optmin, reg->set_optmax);
		return -EINVAL;
	}
	if (reg->get_optmin > reg->get_optmax) {
		NFDEBUG("nf_register_sockopt: bad get val: min=%i max=%i.\n", 
			reg->get_optmin, reg->get_optmax);
		return -EINVAL;
	}
#endif
	if (down_interruptible(&nf_sockopt_mutex) != 0)
		return -EINTR;

	for (i = nf_sockopts.next; i != &nf_sockopts; i = i->next) {
		struct nf_sockopt_ops *ops = (struct nf_sockopt_ops *)i;
		if (ops->pf == reg->pf
		    && (overlap(ops->set_optmin, ops->set_optmax, 
				reg->set_optmin, reg->set_optmax)
			|| overlap(ops->get_optmin, ops->get_optmax, 
				   reg->get_optmin, reg->get_optmax))) {
			NFDEBUG("nf_sock overlap: %u-%u/%u-%u v %u-%u/%u-%u\n",
				ops->set_optmin, ops->set_optmax, 
				ops->get_optmin, ops->get_optmax, 
				reg->set_optmin, reg->set_optmax,
				reg->get_optmin, reg->get_optmax);
			ret = -EBUSY;
			goto out;
		}
	}

	list_add(&reg->list, &nf_sockopts);
out:
	up(&nf_sockopt_mutex);
	return ret;
}

void nf_unregister_sockopt(struct nf_sockopt_ops *reg)
{
#ifdef CONFIG_NETFILTER_DEBUG
	if (reg->pf<0 || reg->pf>=NPROTO) {
		NFDEBUG("nf_register_sockopt: bad val: pf=%i.\n", reg->pf);
		return;
	}
#endif
	/* No point being interruptible: we're probably in cleanup_module() */
	down(&nf_sockopt_mutex);
	list_del(&reg->list);
	up(&nf_sockopt_mutex);
}

#ifdef CONFIG_NETFILTER_DEBUG
#include <net/ip.h>
#include <net/route.h>
#include <net/tcp.h>
#include <linux/netfilter_ipv4.h>

void nf_dump_skb(int pf, struct sk_buff *skb)
{
	printk("skb: pf=%i %s dev=%s len=%u\n", 
	       pf,
	       skb->sk ? "(owned)" : "(unowned)",
	       skb->dev ? skb->dev->name : "(no dev)",
	       skb->len);
	switch (pf) {
	case PF_INET: {
		const struct iphdr *ip = skb->nh.iph;
		__u32 *opt = (__u32 *) (ip + 1);
		int opti;
		__u16 src_port = 0, dst_port = 0;

		if (ip->protocol == IPPROTO_TCP
		    || ip->protocol == IPPROTO_UDP) {
			struct tcphdr *tcp=(struct tcphdr *)((__u32 *)ip+ip->ihl);
			src_port = ntohs(tcp->source);
			dst_port = ntohs(tcp->dest);
		}
	
		printk("PROTO=%d %ld.%ld.%ld.%ld:%hu %ld.%ld.%ld.%ld:%hu"
		       " L=%hu S=0x%2.2hX I=%hu F=0x%4.4hX T=%hu",
		       ip->protocol,
		       (ntohl(ip->saddr)>>24)&0xFF,
		       (ntohl(ip->saddr)>>16)&0xFF,
		       (ntohl(ip->saddr)>>8)&0xFF,
		       (ntohl(ip->saddr))&0xFF,
		       src_port,
		       (ntohl(ip->daddr)>>24)&0xFF,
		       (ntohl(ip->daddr)>>16)&0xFF,
		       (ntohl(ip->daddr)>>8)&0xFF,
		       (ntohl(ip->daddr))&0xFF,
		       dst_port,
		       ntohs(ip->tot_len), ip->tos, ntohs(ip->id),
		       ntohs(ip->frag_off), ip->ttl);

		for (opti = 0; opti < (ip->ihl - sizeof(struct iphdr) / 4); opti++)
			printk(" O=0x%8.8X", *opt++);
		printk("\n");
	}
	}
}

void nf_debug_ip_local_deliver(struct sk_buff *skb)
{
	/* If it's a loopback packet, it must have come through
	 * NF_IP_LOCAL_OUT, NF_IP_RAW_INPUT, NF_IP_PRE_ROUTING and
	 * NF_IP_LOCAL_IN.  Otherwise, must have gone through
	 * NF_IP_RAW_INPUT and NF_IP_PRE_ROUTING.  */
	if (!skb->dev) {
		printk("ip_local_deliver: skb->dev is NULL.\n");
	}
	else if (strcmp(skb->dev->name, "lo") == 0) {
		if (skb->nf_debug != ((1 << NF_IP_LOCAL_OUT)
				      | (1 << NF_IP_POST_ROUTING)
				      | (1 << NF_IP_PRE_ROUTING)
				      | (1 << NF_IP_LOCAL_IN))) {
			printk("ip_local_deliver: bad loopback skb: ");
			debug_print_hooks_ip(skb->nf_debug);
			nf_dump_skb(PF_INET, skb);
		}
	}
	else {
		if (skb->nf_debug != ((1<<NF_IP_PRE_ROUTING)
				      | (1<<NF_IP_LOCAL_IN))) {
			printk("ip_local_deliver: bad non-lo skb: ");
			debug_print_hooks_ip(skb->nf_debug);
			nf_dump_skb(PF_INET, skb);
		}
	}
}

void nf_debug_ip_loopback_xmit(struct sk_buff *newskb)
{
	if (newskb->nf_debug != ((1 << NF_IP_LOCAL_OUT)
				 | (1 << NF_IP_POST_ROUTING))) {
		printk("ip_dev_loopback_xmit: bad owned skb = %p: ", 
		       newskb);
		debug_print_hooks_ip(newskb->nf_debug);
		nf_dump_skb(PF_INET, newskb);
	}
	/* Clear to avoid confusing input check */
	newskb->nf_debug = 0;
}

void nf_debug_ip_finish_output2(struct sk_buff *skb)
{
	/* If it's owned, it must have gone through the
	 * NF_IP_LOCAL_OUT and NF_IP_POST_ROUTING.
	 * Otherwise, must have gone through NF_IP_RAW_INPUT,
	 * NF_IP_PRE_ROUTING, NF_IP_FORWARD and NF_IP_POST_ROUTING.
	 */
	if (skb->sk) {
		if (skb->nf_debug != ((1 << NF_IP_LOCAL_OUT)
				      | (1 << NF_IP_POST_ROUTING))) {
			printk("ip_finish_output: bad owned skb = %p: ", skb);
			debug_print_hooks_ip(skb->nf_debug);
			nf_dump_skb(PF_INET, skb);
		}
	} else {
		if (skb->nf_debug != ((1 << NF_IP_PRE_ROUTING)
#ifdef CONFIG_IP_NETFILTER_RAW_INPUT
				      | (1 << NF_IP_RAW_INPUT)
#endif
				      | (1 << NF_IP_FORWARD)
				      | (1 << NF_IP_POST_ROUTING))) {
			printk("ip_finish_output: bad unowned skb = %p: ",skb);
			debug_print_hooks_ip(skb->nf_debug);
			nf_dump_skb(PF_INET, skb);
		}
	}
}


#endif /*CONFIG_NETFILTER_DEBUG*/

void nf_cacheflush(int pf, unsigned int hook, const void *packet,
		   const struct net_device *indev, const struct net_device *outdev,
		   __u32 packetcount, __u32 bytecount)
{
	struct list_head *i;

	read_lock_bh(&nf_lock);
	for (i = nf_hooks[pf][hook].next; 
	     i != &nf_hooks[pf][hook]; 
	     i = i->next) {
		if (((struct nf_hook_ops *)i)->flush)
			((struct nf_hook_ops *)i)->flush(packet, indev,
							 outdev,
							 packetcount,
							 bytecount);
	}
	read_unlock_bh(&nf_lock);
}

/* Call get/setsockopt() */
static int nf_sockopt(struct sock *sk, int pf, int val, 
		      char *opt, int *len, int get)
{
	struct list_head *i;
	int ret;

	if (down_interruptible(&nf_sockopt_mutex) != 0)
		return -EINTR;

	for (i = nf_sockopts.next; i != &nf_sockopts; i = i->next) {
		struct nf_sockopt_ops *ops = (struct nf_sockopt_ops *)i;
		if (ops->pf == pf) {
			if (get) {
				if (val >= ops->get_optmin
				    && val < ops->get_optmax) {
					ret = ops->get(sk, val, opt, len);
					goto out;
				}
			} else {
				if (val >= ops->set_optmin
				    && val < ops->set_optmax) {
					ret = ops->set(sk, val, opt, *len);
					goto out;
				}
			}
		}
	}
	ret = -ENOPROTOOPT;
 out:
	up(&nf_sockopt_mutex);
	return ret;
}

int nf_setsockopt(struct sock *sk, int pf, int val, char *opt,
		  int len)
{
	return nf_sockopt(sk, pf, val, opt, &len, 0);
}

int nf_getsockopt(struct sock *sk, int pf, int val, char *opt, int *len)
{
	return nf_sockopt(sk, pf, val, opt, len, 1);
}

static unsigned int nf_iterate(struct list_head *head,
			       struct sk_buff **skb,
			       int hook,
			       const struct net_device *indev,
			       const struct net_device *outdev,
			       struct list_head **i)
{
	for (*i = (*i)->next; *i != head; *i = (*i)->next) {
		struct nf_hook_ops *elem = (struct nf_hook_ops *)*i;
		switch (elem->hook(hook, skb, indev, outdev)) {
		case NF_QUEUE:
			NFDEBUG("nf_iterate: NF_QUEUE for %p.\n", *skb);
			return NF_QUEUE;

		case NF_STOLEN:
			NFDEBUG("nf_iterate: NF_STOLEN for %p.\n", *skb);
			return NF_STOLEN;

		case NF_DROP:
			NFDEBUG("nf_iterate: NF_DROP for %p.\n", *skb);
			return NF_DROP;

#ifdef CONFIG_NETFILTER_DEBUG
		case NF_ACCEPT:
			break;

		default:
			NFDEBUG("Evil return from %p(%u).\n", 
				elem->hook, hook);
#endif
		}
	}
	return NF_ACCEPT;
}

static void nf_queue(struct sk_buff *skb, 
		     struct list_head *elem, 
		     int pf, unsigned int hook,
		     struct net_device *indev,
		     struct net_device *outdev,
		     int (*okfn)(struct sk_buff *))
{
	struct list_head *i;

	struct nf_info *info = kmalloc(sizeof(*info), GFP_ATOMIC);
	if (!info) {
		NFDEBUG("nf_hook: OOM.\n");
		kfree_skb(skb);
		return;
	}

	/* Can't do struct assignments with arrays in them.  Damn. */
	info->elem = (struct nf_hook_ops *)elem;
	info->mark = skb->nfmark;
	info->pf = pf;
	info->hook = hook;
	info->okfn = okfn;
	info->indev = indev;
	info->outdev = outdev;
	skb->nfmark = (unsigned long)info;

	/* Bump dev refs so they don't vanish while packet is out */
	if (indev) dev_hold(indev);
	if (outdev) dev_hold(outdev);

	for (i = nf_interested.next; i != &nf_interested; i = i->next) {
		struct nf_interest *recip = (struct nf_interest *)i;

		if ((recip->hookmask & (1 << info->hook))
		    && info->pf == recip->pf
		    && (!recip->mark || info->mark == recip->mark)
		    && (!recip->reason || skb->nfreason == recip->reason)) {
			/* FIXME: Andi says: use netlink.  Hmmm... --RR */
			if (skb_queue_len(&recip->wake->skbq) >= 100) {
				NFDEBUG("nf_hook: queue to long.\n");
				goto free_discard;
			}
			/* Hand it to userspace for collection */
			skb_queue_tail(&recip->wake->skbq, skb);
			NFDEBUG("Waking up pf=%i hook=%u mark=%lu reason=%u\n",
				pf, hook, skb->nfmark, skb->nfreason);
			wake_up_interruptible(&recip->wake->sleep);

			return;
		}
	}
	NFDEBUG("nf_hook: noone wants the packet.\n");

 free_discard: 
	if (indev) dev_put(indev);
	if (outdev) dev_put(outdev);

	kfree_s(info, sizeof(*info));
	kfree_skb(skb);
}

/* nf_hook() doesn't have lock, so may give false positive. */
int nf_hook_slow(int pf, unsigned int hook, struct sk_buff *skb,
		 struct net_device *indev,
		 struct net_device *outdev,
		 int (*okfn)(struct sk_buff *))
{
	struct list_head *elem;
	unsigned int verdict;
	int ret = 0;

#ifdef CONFIG_NETFILTER_DEBUG
	if (pf < 0 || pf >= NPROTO || hook >= NF_MAX_HOOKS) {
		NFDEBUG("nf_hook: bad vals: pf=%i, hook=%u.\n",
			pf, hook);
		kfree_skb(skb);
		return -EINVAL; /* -ECODERFUCKEDUP ?*/
	}

	if (skb->nf_debug & (1 << hook)) {
		NFDEBUG("nf_hook: hook %i already set.\n", hook);
		nf_dump_skb(pf, skb);
	}
	skb->nf_debug |= (1 << hook);
#endif
	read_lock_bh(&nf_lock);
	elem = &nf_hooks[pf][hook];
	verdict = nf_iterate(&nf_hooks[pf][hook], &skb, hook, indev,
			     outdev, &elem);
	if (verdict == NF_QUEUE) {
		NFDEBUG("nf_hook: Verdict = QUEUE.\n");
		nf_queue(skb, elem, pf, hook, indev, outdev, okfn);
	}
	read_unlock_bh(&nf_lock);

	switch (verdict) {
	case NF_ACCEPT:
		ret = okfn(skb);
		break;

	case NF_DROP:
		kfree_skb(skb);
		ret = -EPERM;
		break;
	}

	return ret;
}

struct nf_waitinfo {
	unsigned int verdict;
	struct task_struct *owner;
};

/* For netfilter device. */
void nf_register_interest(struct nf_interest *interest)
{
	/* First in, best dressed. */
	write_lock_bh(&nf_lock);
	list_add(&interest->list, &nf_interested);
	write_unlock_bh(&nf_lock);
}

void nf_unregister_interest(struct nf_interest *interest)
{
	struct sk_buff *skb;

	write_lock_bh(&nf_lock);
	list_del(&interest->list);
	write_unlock_bh(&nf_lock);

	/* Blow away any queued skbs; this is overzealous. */
	while ((skb = skb_dequeue(&interest->wake->skbq)) != NULL)
		nf_reinject(skb, 0, NF_DROP);
}

void nf_getinfo(const struct sk_buff *skb, 
		struct net_device **indev,
		struct net_device **outdev,
		unsigned long *mark)
{
	const struct nf_info *info = (const struct nf_info *)skb->nfmark;

	*indev = info->indev;
	*outdev = info->outdev;
	*mark = info->mark;
}

void nf_reinject(struct sk_buff *skb, unsigned long mark, unsigned int verdict)
{
	struct nf_info *info = (struct nf_info *)skb->nfmark;
	struct list_head *elem = &info->elem->list;
	struct list_head *i;

	read_lock_bh(&nf_lock);

	for (i = nf_hooks[info->pf][info->hook].next; i != elem; i = i->next) {
		if (i == &nf_hooks[info->pf][info->hook]) {
			/* The module which sent it to userspace is gone. */
			verdict = NF_DROP;
			break;
		}
	}

	/* Continue traversal iff userspace said ok, and devices still
           exist... */
	if (verdict == NF_ACCEPT) {
		skb->nfmark = mark;
		verdict = nf_iterate(&nf_hooks[info->pf][info->hook],
				     &skb, info->hook, 
				     info->indev, info->outdev, &elem);
	}

	if (verdict == NF_QUEUE) {
		nf_queue(skb, elem, info->pf, info->hook, 
			 info->indev, info->outdev, info->okfn);
	}
	read_unlock_bh(&nf_lock);

	switch (verdict) {
	case NF_ACCEPT:
		local_bh_disable();
		info->okfn(skb);
		local_bh_enable();
		break;

	case NF_DROP:
		kfree_skb(skb);
		break;
	}

	/* Release those devices we held, or Alexey will kill me. */
	if (info->indev) dev_put(info->indev);
	if (info->outdev) dev_put(info->outdev);

	kfree_s(info, sizeof(*info));
	return;
}

/* FIXME: Before cache is ever used, this must be implemented for real. */
void nf_invalidate_cache(int pf)
{
}

#ifdef CONFIG_NETFILTER_DEBUG

void debug_print_hooks_ip(unsigned int nf_debug)
{
	if (nf_debug & (1 << NF_IP_PRE_ROUTING)) {
		printk("PRE_ROUTING ");
		nf_debug ^= (1 << NF_IP_PRE_ROUTING);
	}
	if (nf_debug & (1 << NF_IP_LOCAL_IN)) {
		printk("LOCAL_IN ");
		nf_debug ^= (1 << NF_IP_LOCAL_IN);
	}
	if (nf_debug & (1 << NF_IP_FORWARD)) {
		printk("FORWARD ");
		nf_debug ^= (1 << NF_IP_FORWARD);
	}
	if (nf_debug & (1 << NF_IP_LOCAL_OUT)) {
		printk("LOCAL_OUT ");
		nf_debug ^= (1 << NF_IP_LOCAL_OUT);
	}
	if (nf_debug & (1 << NF_IP_POST_ROUTING)) {
		printk("POST_ROUTING ");
		nf_debug ^= (1 << NF_IP_POST_ROUTING);
	}
	if (nf_debug)
		printk("Crap bits: 0x%04X", nf_debug);
	printk("\n");
}
#endif /* CONFIG_NETFILTER_DEBUG */

void __init netfilter_init(void)
{
	int i, h;

	for (i = 0; i < NPROTO; i++)
		for (h = 0; h < NF_MAX_HOOKS; h++)
			INIT_LIST_HEAD(&nf_hooks[i][h]);
}
