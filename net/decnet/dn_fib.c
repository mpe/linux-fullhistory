/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Routing Forwarding Information Base
 *
 * Author:      Steve Whitehouse <SteveW@ACM.org>
 *
 *
 * Changes:
 *
 */
#include <linux/config.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <asm/spinlock.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/dn.h>
#include <net/dn_fib.h>
#include <net/dn_neigh.h>
#include <net/dn_dev.h>

/*
 * N.B. Some of the functions here should really be inlines, but
 * I'll sort out that when its all working properly, for now the
 * stack frames will be useful for debugging.
 */
#define DN_NUM_TABLES 255
#define DN_MIN_TABLE  1
#define DN_L1_TABLE 1
#define DN_L2_TABLE 2

#ifdef CONFIG_RTNETLINK
static int dn_fib_table_dump(struct dn_fib_table *t, struct sk_buff *skb, struct netlink_callback *cb);
static void dn_rtmsg_fib(int event, int table, struct dn_fib_action *fa, struct nlmsghdr *nlh, struct netlink_skb_parms *req);
#endif /* CONFIG_RTNETLINK */

static void dn_fib_del_tree(struct dn_fib_table *t);

static struct dn_fib_table *dn_fib_tables[DN_NUM_TABLES + 1];
static int dn_fib_allocs = 0;
static int dn_fib_actions = 0;

static struct dn_fib_node *dn_fib_alloc(void)
{
	struct dn_fib_node *fn;

	fn = kmalloc(sizeof(struct dn_fib_node), GFP_KERNEL);

	if (fn) {
		memset(fn, 0, sizeof(struct dn_fib_node));
		dn_fib_allocs++;
	}

	return fn;
}


static __inline__ void dn_fib_free(struct dn_fib_node *fn)
{
	kfree_s(fn, sizeof(struct dn_fib_node));
	dn_fib_allocs--;
}

static struct dn_fib_action *dn_fib_new_action(void)
{
	struct dn_fib_action *fa;

	fa = kmalloc(sizeof(struct dn_fib_action), GFP_KERNEL);

	if (fa) {
		memset(fa, 0, sizeof(struct dn_fib_action));
		dn_fib_actions++;
	}

	return fa;
}

static __inline__ void dn_fib_del_action(struct dn_fib_action *fa)
{
	if ((fa->fa_type == RTN_UNICAST) && fa->fa_neigh)
		neigh_release(fa->fa_neigh);

	kfree_s(fa, sizeof(struct dn_fib_action));
	dn_fib_actions--;
}

static struct dn_fib_node *dn_fib_follow(struct dn_fib_node *fn, dn_address key)
{
	while(fn->fn_action == NULL)
		fn = DN_FIB_NEXT(fn, key);

	return fn;
}


static struct dn_fib_node *dn_fib_follow1(struct dn_fib_node *fn, dn_address key)
{
	while((fn->fn_action == NULL) && (((key ^ fn->fn_key) >> fn->fn_shift) == 0))
		fn = DN_FIB_NEXT(fn, key);

	return fn;
}


static int dn_fib_table_insert1(struct dn_fib_table *t, struct dn_fib_node *leaf)
{
	struct dn_fib_node *fn, *fn1, *fn2;
	int shift = -1;
	dn_address match;
	dn_address cmpmask = 1;

	if (!t->root) {
		t->root = leaf;
		t->count++;
		return 0;
	}

	fn1 = dn_fib_follow1(t->root, leaf->fn_key);
	fn2 = fn1->fn_up;

	if (fn1->fn_key == leaf->fn_key)
		return -EEXIST;

	if ((fn = dn_fib_alloc()) == NULL)
		return -ENOBUFS;

	fn->fn_key = leaf->fn_key;
	match = fn1->fn_key ^ fn->fn_key;

	while(match) {
		match >>= 1;
		shift++;
	}
	cmpmask <<= shift;

	fn->fn_cmpmask = cmpmask;
	fn->fn_shift   = shift;

	if (fn2) {
		DN_FIB_NEXT(fn2, fn->fn_key) = fn;
	} else {
		t->root = fn;
	}

	t->count++;
	fn->fn_up = fn2;
	DN_FIB_NEXT(fn, fn1->fn_key)  = fn1;
	DN_FIB_NEXT(fn, leaf->fn_key) = leaf;

	return 0;
}

static __inline__ int dn_maskcmp(dn_address m1, dn_address m2)
{
	int cmp = 0;

	while(m1 || m2) {
		if (m1 & 0x8000)
			cmp++;
		if (m2 & 0x8000)
			cmp--;
		m1 <<= 1;
		m2 <<= 1;
	}

	return cmp;
}


static int dn_fib_table_insert(struct dn_fib_table *t, struct dn_fib_action *fa)
{
	struct dn_fib_node *fn;
	struct dn_fib_action **fap;
	int err;
	int cmp;

	if (t->root && ((fn = dn_fib_follow(t->root, fa->fa_key)) != NULL) && 
			(fn->fn_key == fa->fa_key))
		goto add_action;

	if ((fn = dn_fib_alloc()) == NULL)
		return -ENOBUFS;

	fn->fn_key = fa->fa_key;
	fn->fn_action = fa;

	if ((err = dn_fib_table_insert1(t, fn)) < 0)
		dn_fib_free(fn);

#ifdef CONFIG_RTNETLINK
	if (!err)
		dn_rtmsg_fib(RTM_NEWROUTE, t->n, fa, NULL, NULL);
#endif /* CONFIG_RTNETLINK */

	return err;

add_action:
	fap = &fn->fn_action;

	for(; *fap; fap = &((*fap)->fa_next)) {
		if ((cmp = dn_maskcmp((*fap)->fa_mask, fa->fa_mask)) > 0)
			break;
		if (cmp < 0)
			continue;
		if ((*fap)->fa_cost > fa->fa_cost)
			break;
	}

	fa->fa_next = *fap;
	*fap = fa;

#ifdef CONFIG_RTNETLINK
	dn_rtmsg_fib(RTM_NEWROUTE, t->n, fa, NULL, NULL);
#endif /* CONFIG_RTNETLINK */

	return 0;
}

static int dn_fib_table_delete1(struct dn_fib_table *t, struct dn_fib_node *fn)
{
	struct dn_fib_node *fn1 = fn->fn_up;
	struct dn_fib_node *fn2;
	struct dn_fib_node *fn3;

	if (fn == t->root) {
		t->root = NULL;
		t->count--;
		return 0;
	}

	if (fn1 == NULL)
		return -EINVAL;

	fn2 = fn1->fn_up;
	fn3 = DN_FIB_NEXT(fn1, ~fn->fn_key);

	if (fn2)
		DN_FIB_NEXT(fn2, fn1->fn_key) = fn3;
	else
		t->root = fn3;

	fn3->fn_up = fn2;

	dn_fib_free(fn1);
	t->count--;
	return 0;
}

static int dn_fib_table_delete(struct dn_fib_table *t, struct dn_fib_action *fa)
{
	struct dn_fib_res res;
	struct dn_fib_node *fn;
	struct dn_fib_action **fap, *old;
	int err;

	res.res_type = 0;
	res.res_addr = fa->fa_key;
	res.res_mask = fa->fa_mask;
	res.res_ifindex = fa->fa_ifindex;
	res.res_proto = fa->fa_proto;
	res.res_cost = fa->fa_cost;

	if ((err = t->lookup(t, &res)) < 0)
		return err;

	fn = res.res_fn;
	fap = &fn->fn_action; 
	while((*fap) != res.res_fa)
		 fap = &((*fap)->fa_next);
	old = *fap;
	*fap = (*fap)->fa_next;

	if (fn->fn_action == NULL)
		dn_fib_table_delete1(t, fn);

	if (t->root == NULL)
		dn_fib_del_tree(t);

#ifdef CONFIG_RTNETLINK
	dn_rtmsg_fib(RTM_DELROUTE, t->n, old, NULL, NULL);
#endif /* CONFIG_RTNETLINK */

	dn_fib_del_action(old);

	return 0;
}

static int dn_fib_search(struct dn_fib_node *fn, struct dn_fib_res *res)
{
	struct dn_fib_action *fa = fn->fn_action;

	for(; fa; fa = fa->fa_next) {
		if ((fa->fa_key ^ res->res_addr) & fa->fa_mask)
			continue;
		if (res->res_ifindex && (res->res_ifindex != fa->fa_ifindex))
			continue;
		if (res->res_mask && (res->res_mask != fa->fa_mask))
			continue;
		if (res->res_proto && (res->res_proto != fa->fa_proto))
			continue;
		if (res->res_cost && (res->res_cost != fa->fa_cost))
			continue;

		res->res_fn = fn;
		res->res_fa = fa;
		return 1;
	}

	return 0;
}

static int dn_fib_recurse(struct dn_fib_node *fn, struct dn_fib_res *res)
{
	struct dn_fib_node *fn1;
	int err = -ENOENT;

	fn1 = dn_fib_follow(fn, res->res_addr);

	if (dn_fib_search(fn1, res))
		return 0;

	while((fn1 = fn1->fn_up) != fn)
		if ((err = dn_fib_recurse(DN_FIB_NEXT(fn1, ~res->res_addr), res)) == 0)
			break;

	return err;
}

static int dn_fib_table_lookup(struct dn_fib_table *t, struct dn_fib_res *res)
{
	struct dn_fib_node *fn = t->root;
	int err = -ENOENT;

	if (t->root == NULL)
		return err;

	fn = dn_fib_follow(t->root, res->res_addr);

	if (dn_fib_search(fn, res))
		return 0;

	while((fn = fn->fn_up) != NULL)
		if ((err = dn_fib_recurse(DN_FIB_NEXT(fn, ~res->res_addr), res)) == 0)
			break;

	return err;
}

static int dn_fib_table_walk_recurse(struct dn_fib_walker_t *fwt, struct dn_fib_node *fn)
{
	struct dn_fib_table *t = fwt->table;

	if (fn->fn_action) {
		fwt->fxn(fwt, fn);
	} else {
		dn_fib_table_walk_recurse(fwt, t->root->fn_children[0]);
		dn_fib_table_walk_recurse(fwt, t->root->fn_children[1]);
	}

	return 0;
}

static int dn_fib_table_walk(struct dn_fib_walker_t *fwt)
{
	struct dn_fib_table *t = fwt->table;

	if (t->root != NULL) {
		if (t->root->fn_action) {
			fwt->fxn(fwt, t->root);
		} else {
			dn_fib_table_walk_recurse(fwt, t->root->fn_children[0]);
			dn_fib_table_walk_recurse(fwt, t->root->fn_children[1]);
		}
	}

	return 0;
}

static struct dn_fib_table *dn_fib_get_tree(int n, int create)
{
	struct dn_fib_table *t;

	if (n < DN_MIN_TABLE)
		return NULL;

	if (n > DN_NUM_TABLES)
		return NULL;

	if (dn_fib_tables[n])
		return dn_fib_tables[n];

	if (!create)
		return NULL;

	if ((t = kmalloc(sizeof(struct dn_fib_table), GFP_KERNEL)) == NULL)
		return NULL;

	dn_fib_tables[n] = t;
	memset(t, 0, sizeof(struct dn_fib_table));

	t->n = n;
	t->insert = dn_fib_table_insert;
	t->delete = dn_fib_table_delete;
	t->lookup = dn_fib_table_lookup;
	t->walk   = dn_fib_table_walk;
#ifdef CONFIG_RTNETLINK
	t->dump = dn_fib_table_dump;
#endif

	return t;
}

static void dn_fib_del_tree(struct dn_fib_table *t)
{
	dn_fib_tables[t->n] = NULL;

	if (t) {
		kfree_s(t, sizeof(struct dn_fib_table));
	}
}


int dn_fib_resolve(struct dn_fib_res *res)
{
	int table = DN_L1_TABLE;
	int count = 0;
	struct dn_fib_action *fa;
	int err;

	if ((res->res_addr ^ dn_ntohs(decnet_address)) & 0xfc00)
		table = DN_L2_TABLE;

	for(;;) {
		struct dn_fib_table *t = dn_fib_get_tree(table, 0);

		if (t == NULL)
			return -ENOBUFS;

		if ((err = t->lookup(t, res)) < 0)
			return err;

		if ((fa = res->res_fa) == NULL)
			return -ENOENT;

		if (fa->fa_type != RTN_THROW)
			break;

		table = fa->fa_table;

		if (count++ > DN_NUM_TABLES)
			return -ENOENT;
	}

	return (fa->fa_type == RTN_PROHIBIT) ? -fa->fa_error : 0;
}

/*
 * Punt to user via netlink for example, but for now
 * we just drop it.
 */
int dn_fib_rt_message(struct sk_buff *skb)
{
	kfree_skb(skb);

	return 0;
}


#ifdef CONFIG_RTNETLINK
static int dn_fib_convert_rtm(struct dn_fib_action *fa,
					struct rtmsg *r, struct rtattr **rta, 
					struct nlmsghdr *n, 
					struct netlink_skb_parms *req)
{
	dn_address dst, gw, mask = 0xffff;
	int ifindex;
	struct neighbour *neigh;
	struct device *dev;
	unsigned char addr[ETH_ALEN];

	if (r->rtm_family != AF_DECnet)
		return -EINVAL;

	if (rta[RTA_DST-1])
		memcpy(&dst, RTA_DATA(rta[RTA_DST-1]), 2);

	if (rta[RTA_OIF-1])
		memcpy(&ifindex, RTA_DATA(rta[RTA_OIF-1]), sizeof(int));

	if (rta[RTA_GATEWAY-1])
		memcpy(&gw, RTA_DATA(rta[RTA_GATEWAY-1]), 2);

	fa->fa_key      = dn_ntohs(dst);
	fa->fa_mask     = mask;
	fa->fa_ifindex  = ifindex;
	fa->fa_proto    = r->rtm_protocol;
	fa->fa_type     = r->rtm_type;

	switch(fa->fa_type) {
		case RTN_UNICAST:
			if ((dev = dev_get_by_index(ifindex)) == NULL)
				return -ENODEV;
			dn_dn2eth(addr, gw);
			if ((neigh = __neigh_lookup(&dn_neigh_table, &addr, dev, 1)) == NULL)
				return -EHOSTUNREACH;
			fa->fa_neigh = neigh;
			break;
		case RTN_THROW:
			fa->fa_table = 0;
			break;
		case RTN_PROHIBIT:
			fa->fa_error = 0;
			break;
		case RTN_UNREACHABLE:
			fa->fa_error = EHOSTUNREACH;
			break;
	}

	return 0;
}

static int dn_fib_check_attr(struct rtmsg *r, struct rtattr **rta)
{
	switch(r->rtm_type) {
		case RTN_UNICAST:
		case RTN_BLACKHOLE:
		case RTN_PROHIBIT:
		case RTN_UNREACHABLE:
		case RTN_THROW:
			break;
		default:
			return -1;
	}

	return 0;
}

int dn_fib_rtm_delroute(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct dn_fib_table *t;
	struct rtattr **rta = arg;
	struct rtmsg *r = NLMSG_DATA(nlh);
	struct dn_fib_action *fa;
	int err;

	if (dn_fib_check_attr(r, rta))
		return -EINVAL;

	if ((fa = dn_fib_new_action()) == NULL)
		return -ENOBUFS;

	t = dn_fib_get_tree(r->rtm_table, 0);
	if (t) {
		if ((err = dn_fib_convert_rtm(fa, r, rta, nlh, &NETLINK_CB(skb))) < 0)  {
			dn_fib_del_action(fa);
			return err;
		}
		err = t->delete(t, fa);
		dn_fib_del_action(fa);
		return err;
	}
	return -ESRCH;
}

int dn_fib_rtm_newroute(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct dn_fib_table *t;
	struct rtattr **rta = arg;
	struct rtmsg *r = NLMSG_DATA(nlh);
	struct dn_fib_action *fa;
	int err;

	if (dn_fib_check_attr(r, rta))
		return -EINVAL;

	if ((fa = dn_fib_new_action()) == NULL)
		return -ENOBUFS;

	t = dn_fib_get_tree(r->rtm_table, 1);
	if (t) {
		if ((err = dn_fib_convert_rtm(fa, r, rta, nlh, &NETLINK_CB(skb))) < 0) {
			dn_fib_del_action(fa);
			return err;
		}
		return t->insert(t, fa);
	}
	return -ENOBUFS;
}

int dn_fib_dump_info(struct sk_buff *skb, u32 pid, u32 seq, int event,
			int table, struct dn_fib_action *fa)
{
	struct rtmsg *rtm;
	struct nlmsghdr *nlh;
	unsigned char *b = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*rtm));
	rtm = NLMSG_DATA(nlh);
	rtm->rtm_family = AF_DECnet;
	rtm->rtm_dst_len = 16;
	rtm->rtm_src_len = 16;
	rtm->rtm_tos = 0;
	rtm->rtm_table = table;
	rtm->rtm_type = fa->fa_type;
	rtm->rtm_flags = 0;
	rtm->rtm_protocol = fa->fa_proto;
	RTA_PUT(skb, RTA_DST, 2, &fa->fa_key);
	if (fa->fa_ifindex)
		RTA_PUT(skb, RTA_OIF, sizeof(int), &fa->fa_ifindex);

	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
        skb_trim(skb, b - skb->data);
        return -1;
}

static void dn_rtmsg_fib(int event, int table, struct dn_fib_action *fa,
			struct nlmsghdr *nlh, struct netlink_skb_parms *req)
{
	struct sk_buff *skb;
	u32 pid = req ? req->pid : 0;
	int size = NLMSG_SPACE(sizeof(struct rtmsg) + 256);

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return;

	if (dn_fib_dump_info(skb, pid, nlh->nlmsg_seq, event, table, fa) < 0) {
		kfree_skb(skb);
		return;
	}
	NETLINK_CB(skb).dst_groups = RTMGRP_DECnet_ROUTE;
	if (nlh->nlmsg_flags & NLM_F_ECHO)
		atomic_inc(&skb->users);
	netlink_broadcast(rtnl, skb, pid, RTMGRP_DECnet_ROUTE, GFP_KERNEL);
	if (nlh->nlmsg_flags & NLM_F_ECHO)
		netlink_unicast(rtnl, skb, pid, MSG_DONTWAIT);
}

static int dn_fib_table_dump(struct dn_fib_table *t, struct sk_buff *skb, struct netlink_callback *cb)
{

	return skb->len;
}

int dn_fib_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int s_t;
	struct dn_fib_table *t;

	for(s_t = cb->args[0]; s_t < DN_NUM_TABLES; s_t++) {
		if (s_t > cb->args[0])
			memset(&cb->args[1], 0, sizeof(cb->args)-sizeof(int));
		t = dn_fib_get_tree(s_t, 0);
		if (t == NULL)
			continue;
		if (t->dump(t, skb, cb) < 0)
			break;
	}

	cb->args[0] = s_t;

	return skb->len;
}
#endif /* CONFIG_RTNETLINK */

int dn_fib_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch(cmd) {
		case SIOCADDRT:
		case SIOCDELRT:
			return 0;
	}

	return -EINVAL;
}

struct dn_fib_procfs {
	int len;
	off_t pos;
	off_t begin;
	off_t offset;
	int length;
	char *buffer;
};

static int dn_proc_action_list(struct dn_fib_walker_t *fwt, struct dn_fib_node *fn)
{
	struct dn_fib_procfs *pinfo = (struct dn_fib_procfs *)fwt->arg;
	struct dn_fib_action *fa;
	char ab[DN_ASCBUF_LEN];

	if (pinfo->pos > pinfo->offset + pinfo->length)
		return 0;

	for(fa = fn->fn_action; fa; fa = fa->fa_next) {

		pinfo->len += sprintf(pinfo->buffer + pinfo->len,
					"%s/%04hx %02x %02x\n", 
					dn_addr2asc(fa->fa_key, ab),
					fa->fa_mask,
					fa->fa_type,
					fa->fa_proto);

		pinfo->pos = pinfo->begin + pinfo->len;
		if (pinfo->pos < pinfo->offset) {
			pinfo->len = 0;
			pinfo->begin = pinfo->pos;
		}
		if (pinfo->pos > pinfo->offset + pinfo->length)
			break;
	}

	return 0;
}

static int decnet_rt_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct dn_fib_procfs pinfo;
	int i;
	struct dn_fib_table *t;
	struct dn_fib_walker_t fwt;

	pinfo.pos = 0;
	pinfo.len = 0;
	pinfo.begin = 0;
	pinfo.offset = offset;
	pinfo.length = length;
	pinfo.buffer = buffer;

	fwt.arg = &pinfo;
	fwt.fxn = dn_proc_action_list;

	start_bh_atomic();
	for(i = 0; i < DN_NUM_TABLES; i++) {
		if ((t = dn_fib_get_tree(i, 0)) == NULL)
			continue;

		fwt.table = t;
		t->walk(&fwt);

		if (pinfo.pos > pinfo.offset + pinfo.length)
			break;
	}
	end_bh_atomic();

	*start = pinfo.buffer + (pinfo.offset - pinfo.begin);
	pinfo.len -= (pinfo.offset - pinfo.begin);

	if (pinfo.len > pinfo.length) 
		pinfo.len = pinfo.length;

	return pinfo.len;
}

static struct proc_dir_entry proc_net_decnet_route = {
	PROC_NET_DN_ROUTE, 12, "decnet_route",
	 S_IFREG | S_IRUGO, 1, 0, 0,
        0, &proc_net_inode_operations,
	decnet_rt_get_info
};

#ifdef CONFIG_DECNET_MODULE
void dn_fib_cleanup(void)
{
#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_DN_ROUTE);
#endif /* CONFIG_PROC_FS */
}
#endif /* CONFIG_DECNET_MODULE */


void __init dn_fib_init(void)
{
	memset(dn_fib_tables, 0, DN_NUM_TABLES * sizeof(struct dn_fib_table *));

#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_decnet_route);
#endif

}


