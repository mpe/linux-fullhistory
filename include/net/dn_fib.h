#ifndef _NET_DN_FIB_H
#define _NET_DN_FIB_H

#include <linux/config.h>

#ifdef CONFIG_DECNET_ROUTER


struct dn_fib_res {
	dn_address res_addr;
	dn_address res_mask;
	int res_ifindex;
	int res_proto;
	int res_cost;
	int res_type;
	struct dn_fib_node *res_fn;
	struct dn_fib_action *res_fa;
};

struct dn_fib_action {
	struct dn_fib_action *fa_next;
	dn_address fa_key;
	dn_address fa_mask;
	int fa_ifindex;
	int fa_proto;
	int fa_cost;
	int fa_type;
	union {
		struct neighbour *fau_neigh;	/* Normal route */
		int fau_error;			/* Reject       */
		int fau_table;			/* Throw        */
	} fa_u;
#define fa_neigh fa_u.fau_neigh
#define fa_error fa_u.fau_error
#define fa_table fa_u.fau_table
};

struct dn_fib_node {
	struct dn_fib_node *fn_up;
	dn_address fn_cmpmask;
	dn_address fn_key;
	int fn_shift;
	struct dn_fib_action *fn_action;
	struct dn_fib_node *fn_children[2];
};

#define DN_FIB_NEXT(fibnode, key) ((fibnode)->fn_children[((key) ^ (fibnode)->fn_cmpmask) >> (fibnode)->fn_shift]) 

struct dn_fib_walker_t;

struct dn_fib_table {
	int n;
	unsigned long count;
	struct dn_fib_node *root;

	int (*insert)(struct dn_fib_table *t, struct dn_fib_action *fa);
	int (*delete)(struct dn_fib_table *t, struct dn_fib_action *fa);
	int (*lookup)(struct dn_fib_table *t, struct dn_fib_res *res);
	int (*walk)(struct dn_fib_walker_t *fwt);
#ifdef CONFIG_RTNETLINK
	int (*dump)(struct dn_fib_table *t, struct sk_buff *skb, struct netlink_callback *cb);
#endif /* CONFIG_RTNETLINK */
};

struct dn_fib_walker_t {
	struct dn_fib_table *table;
	void *arg;
	int (*fxn)(struct dn_fib_walker_t *fwt, struct dn_fib_node *n);
};

extern void dn_fib_init(void);
extern void dn_fib_cleanup(void);

extern int dn_fib_rt_message(struct sk_buff *skb);
extern int dn_fib_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
extern int dn_fib_resolve(struct dn_fib_res *res);

#ifdef CONFIG_RTNETLINK
extern int dn_fib_rtm_delroute(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg);
extern int dn_fib_rtm_newroute(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg);
extern int dn_fib_dump(struct sk_buff *skb, struct netlink_callback *cb);
#endif /* CONFIG_RTNETLINK */
#endif /* CONFIG_DECNET_ROUTER */

#endif /* _NET_DN_FIB_H */
