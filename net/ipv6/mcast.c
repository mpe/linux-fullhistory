/*
 *	Multicast support for IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: mcast.c,v 1.8 1997/04/12 04:32:48 davem Exp $
 *
 *	Based on linux/ipv4/igmp.c and linux/ipv4/ip_sockglue.c 
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/route.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/if_inet6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>

#include <net/checksum.h>

/* Set to 3 to get tracing... */
#define MCAST_DEBUG 2

#if MCAST_DEBUG >= 3
#define MDBG(x) printk x
#else
#define MDBG(x)
#endif

static struct inode igmp6_inode;
static struct socket *igmp6_socket=&igmp6_inode.u.socket_i;

static void igmp6_join_group(struct ifmcaddr6 *ma);
static void igmp6_leave_group(struct ifmcaddr6 *ma);
void igmp6_timer_handler(unsigned long data);

#define IGMP6_UNSOLICITED_IVAL	(10*HZ)

/*
 *	socket join on multicast group
 */

int ipv6_sock_mc_join(struct sock *sk, struct device *dev, 
		      struct in6_addr *addr)
{
	struct ipv6_mc_socklist *mc_lst;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	int err;

	MDBG(("ipv6_sock_mc_join(%s) addr[", dev ? dev->name : "[NULL]"));
	MDBG(("%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x]\n",
	      addr->s6_addr16[0], addr->s6_addr16[1], addr->s6_addr16[2],
	      addr->s6_addr16[3], addr->s6_addr16[4], addr->s6_addr16[5],
	      addr->s6_addr16[6], addr->s6_addr16[7]));
	if (!(ipv6_addr_type(addr) & IPV6_ADDR_MULTICAST))
		return -EINVAL;

	if(!(dev->flags & IFF_MULTICAST))
		return -EADDRNOTAVAIL;

	mc_lst = kmalloc(sizeof(struct ipv6_mc_socklist), GFP_KERNEL);

	if (mc_lst == NULL)
		return -ENOMEM;

	mc_lst->next = NULL;
	memcpy(&mc_lst->addr, addr, sizeof(struct in6_addr));
	mc_lst->dev = dev;

	/*
	 *	now add/increase the group membership on the device
	 */

	err = ipv6_dev_mc_inc(dev, addr);

	if (err) {
		kfree(mc_lst);
		return err;
	}

	mc_lst->next = np->ipv6_mc_list;
	np->ipv6_mc_list = mc_lst;

	return 0;
}

/*
 *	socket leave on multicast group
 */
int ipv6_sock_mc_drop(struct sock *sk, struct device *dev, 
		      struct in6_addr *addr)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6_mc_socklist *mc_lst, **lnk;

	lnk = &np->ipv6_mc_list;

	MDBG(("ipv6_sock_mc_drop(%s) addr[", dev ? dev->name : "[NULL]"));
	MDBG(("%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x]\n",
	      addr->s6_addr16[0], addr->s6_addr16[1], addr->s6_addr16[2],
	      addr->s6_addr16[3], addr->s6_addr16[4], addr->s6_addr16[5],
	      addr->s6_addr16[6], addr->s6_addr16[7]));

	for (mc_lst = *lnk ; mc_lst; mc_lst = mc_lst->next) {
		if (mc_lst->dev == dev &&
		    ipv6_addr_cmp(&mc_lst->addr, addr) == 0) {
			*lnk = mc_lst->next;
			ipv6_dev_mc_dec(mc_lst->dev, &mc_lst->addr);
			kfree(mc_lst);

			return 0;
		}
		lnk = &mc_lst->next;
	}

	return -ENOENT;
}

void ipv6_sock_mc_close(struct sock *sk)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6_mc_socklist *mc_lst;

	for (mc_lst = np->ipv6_mc_list; mc_lst; ) {
		struct ipv6_mc_socklist *back;

		/*
		 *	leave group
		 */

		ipv6_dev_mc_dec(mc_lst->dev, &mc_lst->addr);

		back = mc_lst;
		mc_lst = mc_lst->next;
		kfree(back);
	}
}

/*
 *	device multicast group inc (add if not found)
 */
int ipv6_dev_mc_inc(struct device *dev, struct in6_addr *addr)
{
	struct ifmcaddr6 *mc;
	struct inet6_dev    *idev;
	char buf[6];
	int hash;

	MDBG(("ipv6_dev_mc_inc(%s) addr[", dev ? dev->name : "[NULL]"));
	MDBG(("%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x]\n",
	      addr->s6_addr16[0], addr->s6_addr16[1], addr->s6_addr16[2],
	      addr->s6_addr16[3], addr->s6_addr16[4], addr->s6_addr16[5],
	      addr->s6_addr16[6], addr->s6_addr16[7]));
	hash = ipv6_devindex_hash(dev->ifindex);

	for (idev = inet6_dev_lst[hash]; idev; idev=idev->next)
		if (idev->dev == dev)
			break;

	if (idev == NULL) {
		printk(KERN_DEBUG "ipv6_dev_mc_inc: device not found\n");
		return -EINVAL;
	}

	hash = ipv6_addr_hash(addr);

	for (mc = inet6_mcast_lst[hash]; mc; mc = mc->next) {
		if (ipv6_addr_cmp(&mc->mca_addr, addr) == 0) {
			atomic_inc(&mc->mca_users);
			return 0;
		}
	}

	/*
	 *	not found: create a new one.
	 */

	mc = kmalloc(sizeof(struct ifmcaddr6), GFP_ATOMIC);

	if (mc == NULL)
		return -ENOMEM;

	MDBG(("create new ipv6 MC entry, "));
	memset(mc, 0, sizeof(struct ifmcaddr6));
	mc->mca_timer.function = igmp6_timer_handler;
	mc->mca_timer.data = (unsigned long) mc;

	memcpy(&mc->mca_addr, addr, sizeof(struct in6_addr));
	mc->dev = dev;
	atomic_set(&mc->mca_users, 1);

	mc->next = inet6_mcast_lst[hash];
	inet6_mcast_lst[hash] = mc;

	mc->if_next = idev->mc_list;
	idev->mc_list = mc;

	/*
	 *	multicast mapping is defined in IPv6-over-foo documents
	 */

	switch (dev->type) {
	case ARPHRD_ETHER:
		ipv6_mc_map(addr, buf);
		MDBG(("ARPHRD_ETHER[%02x:%02x:%02x:%02x:%02x:%02x] dev_mc_add()\n",
		      buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]));
		dev_mc_add(dev, buf, ETH_ALEN, 0);
		break;

	default:
		printk(KERN_DEBUG "dev_mc_inc: unkown device type\n");
	};

	igmp6_join_group(mc);

	return 0;
}

static void ipv6_mca_remove(struct device *dev, struct ifmcaddr6 *ma)
{
	struct inet6_dev *idev;

	idev = ipv6_get_idev(dev);

	if (idev) {
		struct ifmcaddr6 *iter, **lnk;

		lnk = &idev->mc_list;
		
		for (iter = *lnk; iter; iter = iter->if_next) {
			if (iter == ma) {
				*lnk = iter->if_next;
				break;
			}
			lnk = &iter->if_next;
		}
	}
}

/*
 *	device multicast group del
 */
int ipv6_dev_mc_dec(struct device *dev, struct in6_addr *addr)
{
	struct ifmcaddr6 *ma, **lnk;
	int hash;

	hash = ipv6_addr_hash(addr);

	lnk = &inet6_mcast_lst[hash];

	for (ma = inet6_mcast_lst[hash]; ma; ma = ma->next) {
		if (ipv6_addr_cmp(&ma->mca_addr, addr) == 0) {
			if (atomic_dec_and_test(&ma->mca_users)) {
				igmp6_leave_group(ma);
				*lnk = ma->next;
				ipv6_mca_remove(ma->dev, ma);
				kfree(ma);
			}
			return 0;
		}
		lnk = &ma->next;
	}

	return -ENOENT;
}

/*
 *	check if the interface/address pair is valid
 */
int ipv6_chk_mcast_addr(struct device *dev, struct in6_addr *addr)
{
	struct ifmcaddr6 *mc;
	int hash;

	hash = ipv6_addr_hash(addr);

	for (mc = inet6_mcast_lst[hash]; mc; mc=mc->next) {
		if ((mc->dev == dev) && ipv6_addr_cmp(&mc->mca_addr, addr) == 0)
			return 1;
	}

	return 0;
}

/*
 *	IGMP handling (alias multicast ICMPv6 messages)
 */

static void igmp6_group_queried(struct ifmcaddr6 *ma, unsigned long resptime)
{
	unsigned long delay;

	ma->mca_flags |= MAF_TIMER_RUNNING;

	delay = ipv6_random() % resptime;
	ma->mca_timer.expires = jiffies + delay;
	add_timer(&ma->mca_timer);
}

int igmp6_event_query(struct sk_buff *skb, struct icmp6hdr *hdr, int len)
{
	struct ifmcaddr6 *ma;
	struct in6_addr *addrp;
	unsigned long resptime;

	if (len < sizeof(struct icmp6hdr) + sizeof(struct ipv6hdr))
		return -EINVAL;

	resptime = hdr->icmp6_maxdelay;

	addrp = (struct in6_addr *) (hdr + 1);

	if (ipv6_addr_any(addrp)) {
		struct inet6_dev *idev;

		idev = ipv6_get_idev(skb->dev);

		if (idev == NULL)
			return 0;

		for (ma = idev->mc_list; ma; ma=ma->if_next)
			igmp6_group_queried(ma, resptime);
	} else {
		int hash = ipv6_addr_hash(addrp);

		for (ma = inet6_mcast_lst[hash]; ma; ma=ma->next) {
			if (ma->dev == skb->dev &&
			    ipv6_addr_cmp(addrp, &ma->mca_addr) == 0) {
				igmp6_group_queried(ma, resptime);
				break;
			}
		}
	}

	return 0;
}


int igmp6_event_report(struct sk_buff *skb, struct icmp6hdr *hdr, int len)
{
	struct ifmcaddr6 *ma;
	struct in6_addr *addrp;
	struct device *dev;
	int hash;

	if (len < sizeof(struct icmp6hdr) + sizeof(struct ipv6hdr))
		return -EINVAL;

	addrp = (struct in6_addr *) (hdr + 1);

	dev = skb->dev;

	/*
	 *	Cancel the timer for this group
	 */

	hash = ipv6_addr_hash(addrp);

	for (ma = inet6_mcast_lst[hash]; ma; ma=ma->next) {
		if ((ma->dev == dev) && ipv6_addr_cmp(&ma->mca_addr, addrp) == 0) {
			if (ma->mca_flags & MAF_TIMER_RUNNING) {
				del_timer(&ma->mca_timer);
				ma->mca_flags &= ~MAF_TIMER_RUNNING;
			}

			ma->mca_flags &= ~MAF_LAST_REPORTER;
			break;
		}
	}

	return 0;
}

void igmp6_send(struct in6_addr *addr, struct device *dev, int type)
{
	struct sock *sk = igmp6_socket->sk;
        struct sk_buff *skb;
        struct icmp6hdr *hdr;
	struct inet6_ifaddr *ifp;
	struct in6_addr *addrp; 
	int err, len, plen;

	len = sizeof(struct icmp6hdr) + sizeof(struct in6_addr);

	plen = sizeof(struct ipv6hdr) + len;

	skb = sock_alloc_send_skb(sk, dev->hard_header_len + plen, 0, 0, &err);

	if (skb == NULL)
		return;

	if (dev->hard_header_len) {
		skb_reserve(skb, (dev->hard_header_len + 15) & ~15);
		if (dev->hard_header) {
			unsigned char ha[MAX_ADDR_LEN];
			ipv6_mc_map(addr, ha);
			dev->hard_header(skb, dev, ETH_P_IPV6, ha, NULL, plen);
			skb->arp = 1;
		}
	}

	ifp = ipv6_get_lladdr(dev);

	if (ifp == NULL) {
#if MCAST_DEBUG >= 1
		printk(KERN_DEBUG "igmp6: %s no linklocal address\n",
		       dev->name);
#endif
		return;
	}

	ip6_nd_hdr(sk, skb, dev, &ifp->addr, addr, IPPROTO_ICMPV6, len);

	/*
	 *	need hop-by-hop router alert option.
	 */

	hdr = (struct icmp6hdr *) skb_put(skb, sizeof(struct icmp6hdr));
	memset(hdr, 0, sizeof(struct icmp6hdr));
	hdr->icmp6_type = type;

	addrp = (struct in6_addr *) skb_put(skb, sizeof(struct in6_addr));
	ipv6_addr_copy(addrp, addr);

	hdr->icmp6_cksum = csum_ipv6_magic(&ifp->addr, addr, len,
					   IPPROTO_ICMPV6,
					   csum_partial((__u8 *) hdr, len, 0));

	dev_queue_xmit(skb);
}

static void igmp6_join_group(struct ifmcaddr6 *ma)
{
	unsigned long delay;
	int addr_type;

	addr_type = ipv6_addr_type(&ma->mca_addr);

	if ((addr_type & IPV6_ADDR_LINKLOCAL))
		return;

	igmp6_send(&ma->mca_addr, ma->dev, ICMPV6_MGM_REPORT);

	delay = ipv6_random() % IGMP6_UNSOLICITED_IVAL;
	ma->mca_timer.expires = jiffies + delay;

	add_timer(&ma->mca_timer);
	ma->mca_flags |= MAF_TIMER_RUNNING | MAF_LAST_REPORTER;
}

static void igmp6_leave_group(struct ifmcaddr6 *ma)
{
	int addr_type;

	addr_type = ipv6_addr_type(&ma->mca_addr);

	if ((addr_type & IPV6_ADDR_LINKLOCAL))
		return;

	if (ma->mca_flags & MAF_LAST_REPORTER)
		igmp6_send(&ma->mca_addr, ma->dev, ICMPV6_MGM_REDUCTION);

	if (ma->mca_flags & MAF_TIMER_RUNNING)
		del_timer(&ma->mca_timer);
}

void igmp6_timer_handler(unsigned long data)
{
	struct ifmcaddr6 *ma = (struct ifmcaddr6 *) data;

	ma->mca_flags |=  MAF_LAST_REPORTER;
	igmp6_send(&ma->mca_addr, ma->dev, ICMPV6_MGM_REPORT);
	ma->mca_flags &= ~MAF_TIMER_RUNNING;
}

void igmp6_init(struct net_proto_family *ops)
{
	struct sock *sk;
	int err;

	igmp6_inode.i_mode = S_IFSOCK;
	igmp6_inode.i_sock = 1;
	igmp6_inode.i_uid = 0;
	igmp6_inode.i_gid = 0;

	igmp6_socket->inode = &igmp6_inode;
	igmp6_socket->state = SS_UNCONNECTED;
	igmp6_socket->type = SOCK_RAW;

	if((err=ops->create(igmp6_socket, IPPROTO_ICMPV6))<0)
		printk(KERN_DEBUG 
		       "Failed to create the IGMP6 control socket.\n");

	MOD_DEC_USE_COUNT;

	sk = igmp6_socket->sk;
	sk->allocation = GFP_ATOMIC;
	sk->num = 256;			/* Don't receive any data */

	sk->net_pinfo.af_inet6.hop_limit = 1;
}
