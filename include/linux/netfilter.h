#ifndef __LINUX_NETFILTER_H
#define __LINUX_NETFILTER_H

#ifdef __KERNEL__
#include <linux/init.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/net.h>
#include <linux/wait.h>
#include <linux/list.h>
#endif

/* Responses from hook functions. */
#define NF_DROP 0
#define NF_ACCEPT 1
#define NF_STOLEN 2
#define NF_QUEUE 3
#define NF_MAX_VERDICT NF_QUEUE

/* Generic cache responses from hook functions. */
#define NFC_ALTERED 0x8000
#define NFC_UNKNOWN 0x4000

#ifdef __KERNEL__
#include <linux/config.h>
#ifdef CONFIG_NETFILTER

extern void netfilter_init(void);

/* Largest hook number + 1 */
#define NF_MAX_HOOKS 5

struct sk_buff;
struct net_device;

typedef unsigned int nf_hookfn(unsigned int hooknum,
			       struct sk_buff **skb,
			       const struct net_device *in,
			       const struct net_device *out);

typedef unsigned int nf_cacheflushfn(const void *packet,
				     const struct net_device *in,
				     const struct net_device *out,
				     u_int32_t packetcount,
				     u_int32_t bytecount);

struct nf_hook_ops
{
	struct list_head list;

	/* User fills in from here down. */
	nf_hookfn *hook;
	nf_cacheflushfn *flush;
	int pf;
	int hooknum;
	/* Hooks are ordered in ascending priority. */
	int priority;
};

struct nf_setsockopt_ops
{
	struct list_head list;

	int pf;
	int optmin;
	int optmax;
	int (*fn)(int optval, void *user, unsigned int len);
};

/* Function to register/unregister hook points. */
int nf_register_hook(struct nf_hook_ops *reg);
void nf_unregister_hook(struct nf_hook_ops *reg);

/* Functions to register setsockopt ranges (inclusive). */
int nf_register_sockopt(struct nf_setsockopt_ops *reg);
void nf_unregister_sockopt(struct nf_setsockopt_ops *reg);

extern struct list_head nf_hooks[NPROTO][NF_MAX_HOOKS];

/* Activate hook/flush; either okfn or kfree_skb called, unless a hook
   returns NF_STOLEN (in which case, it's up to the hook to deal with
   the consequences).

   Returns -ERRNO if packet dropped.  Zero means queued, stolen or
   accepted.
*/

/* RR:
   > I don't want nf_hook to return anything because people might forget
   > about async and trust the return value to mean "packet was ok".

   AK:
   Just document it clearly, then you can expect some sense from kernel
   coders :)
*/

/* This is gross, but inline doesn't cut it for avoiding the function
   call in fast path: gcc doesn't inline (needs value tracking?). --RR */
#ifdef CONFIG_NETFILTER_DEBUG
#define NF_HOOK nf_hook_slow
#else
#define NF_HOOK(pf, hook, skb, indev, outdev, okfn)			\
(list_empty(&nf_hooks[(pf)][(hook)])					\
 ? (okfn)(skb)								\
 : nf_hook_slow((pf), (hook), (skb), (indev), (outdev), (okfn)))
#endif

int nf_hook_slow(int pf, unsigned int hook, struct sk_buff *skb,
		 struct net_device *indev, struct net_device *outdev,
		 int (*okfn)(struct sk_buff *));

void nf_cacheflush(int pf, unsigned int hook, const void *packet,
		   const struct net_device *indev, const struct net_device *outdev,
		   __u32 packetcount, __u32 bytecount);

/* Call setsockopt() */
int nf_setsockopt(int pf, int optval, char *opt, unsigned int len);

struct nf_wakeme
{
	wait_queue_head_t sleep;
	struct sk_buff_head skbq;
};

/* For netfilter device. */
struct nf_interest
{
	struct list_head list;

	int pf;
	/* Bitmask of hook numbers to match (1 << hooknum). */
	unsigned int hookmask;
	/* If non-zero, only catch packets with this mark. */
	unsigned int mark;
	/* If non-zero, only catch packets of this reason. */
	unsigned int reason;

	struct nf_wakeme *wake;
};

/* For asynchronous packet handling. */
extern void nf_register_interest(struct nf_interest *interest);
extern void nf_unregister_interest(struct nf_interest *interest);
extern void nf_getinfo(const struct sk_buff *skb,
		       struct net_device **indev,
		       struct net_device **outdev,
		       unsigned long *mark);
extern void nf_reinject(struct sk_buff *skb,
			unsigned long mark,
			unsigned int verdict);

#ifdef CONFIG_NETFILTER_DEBUG
extern void nf_dump_skb(int pf, struct sk_buff *skb);
#endif

/* FIXME: Before cache is ever used, this must be implemented for real. */
extern void nf_invalidate_cache(int pf);

#else /* !CONFIG_NETFILTER */
#define NF_HOOK(pf, hook, skb, indev, outdev, okfn) (okfn)(skb)
#endif /*CONFIG_NETFILTER*/

/* From arch/i386/kernel/smp.c:
 *
 *	Why isn't this somewhere standard ??
 *
 * Maybe because this procedure is horribly buggy, and does
 * not deserve to live.  Think about signedness issues for five
 * seconds to see why.		- Linus
 */

/* Two signed, return a signed. */
#define SMAX(a,b) ((ssize_t)(a)>(ssize_t)(b) ? (ssize_t)(a) : (ssize_t)(b))
#define SMIN(a,b) ((ssize_t)(a)<(ssize_t)(b) ? (ssize_t)(a) : (ssize_t)(b))

/* Two unsigned, return an unsigned. */
#define UMAX(a,b) ((size_t)(a)>(size_t)(b) ? (size_t)(a) : (size_t)(b))
#define UMIN(a,b) ((size_t)(a)<(size_t)(b) ? (size_t)(a) : (size_t)(b))

/* Two unsigned, return a signed. */
#define SUMAX(a,b) ((size_t)(a)>(size_t)(b) ? (ssize_t)(a) : (ssize_t)(b))
#define SUMIN(a,b) ((size_t)(a)<(size_t)(b) ? (ssize_t)(a) : (ssize_t)(b))
#endif /*__KERNEL__*/

enum nf_reason {
	/* Do not, NOT, reorder these.  Add at end. */
	NF_REASON_NONE,
	NF_REASON_SET_BY_IPCHAINS,
	NF_REASON_FOR_ROUTING,
	NF_REASON_FOR_CLS_FW,
	NF_REASON_MIN_RESERVED_FOR_CONNTRACK = 1024,
};

#endif /*__LINUX_NETFILTER_H*/
