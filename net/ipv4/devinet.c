/*
 *	NET3	IP device support routines.
 *
 *	Version: $Id: devinet.c,v 1.14 1997/10/10 22:40:44 davem Exp $
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Derived from the IP parts of dev.c 1.0.19
 * 		Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	Changes:
 *	        Alexey Kuznetsov:	pa_* fields are replaced with ifaddr lists.
 */

#include <linux/config.h>
 
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
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif

#include <net/ip.h>
#include <net/route.h>
#include <net/ip_fib.h>

#ifdef CONFIG_RTNETLINK
static void rtmsg_ifa(int event, struct in_ifaddr *);
#else
#define rtmsg_ifa(a,b)	do { } while(0)
#endif

static struct notifier_block *inetaddr_chain;
static void inet_del_ifa(struct in_device *in_dev, struct in_ifaddr **ifap, int destroy);


int inet_ifa_count;
int inet_dev_count;

static struct in_ifaddr * inet_alloc_ifa(void)
{
	struct in_ifaddr *ifa;

	ifa = kmalloc(sizeof(*ifa), GFP_KERNEL);
	if (ifa) {
		memset(ifa, 0, sizeof(*ifa));
		inet_ifa_count++;
	}

	return ifa;
}

static __inline__ void inet_free_ifa(struct in_ifaddr *ifa)
{
	kfree_s(ifa, sizeof(*ifa));
	inet_ifa_count--;
}

struct in_device *inetdev_init(struct device *dev)
{
	struct in_device *in_dev;

	in_dev = kmalloc(sizeof(*in_dev), GFP_KERNEL);
	if (!in_dev)
		return NULL;
	inet_dev_count++;
	memset(in_dev, 0, sizeof(*in_dev));
	in_dev->dev = dev;
	dev->ip_ptr = in_dev;
	ip_mc_init_dev(in_dev);
	return in_dev;
}

static void inetdev_destroy(struct in_device *in_dev)
{
	struct in_ifaddr *ifa;

	ip_mc_destroy_dev(in_dev);

	while ((ifa = in_dev->ifa_list) != NULL) {
		inet_del_ifa(in_dev, &in_dev->ifa_list, 0);
		inet_free_ifa(ifa);
	}

	in_dev->dev->ip_ptr = NULL;
	kfree(in_dev);
}

struct in_ifaddr * inet_addr_onlink(struct in_device *in_dev, u32 a, u32 b)
{
	for_primary_ifa(in_dev) {
		if (inet_ifa_match(a, ifa)) {
			if (!b || inet_ifa_match(b, ifa))
				return ifa;
		}
	} endfor_ifa(in_dev);
	return NULL;
}

static void
inet_del_ifa(struct in_device *in_dev, struct in_ifaddr **ifap, int destroy)
{
	struct in_ifaddr *ifa1 = *ifap;
	struct in_ifaddr *ifa;

	/* 1. Unlink it */

	*ifap = ifa1->ifa_next;

	/* 2. Deleting primary ifaddr forces deletion all secondaries */

	if (!(ifa1->ifa_flags&IFA_F_SECONDARY)) {
		while ((ifa=*ifap) != NULL) {
			if (ifa1->ifa_mask != ifa->ifa_mask ||
			    !inet_ifa_match(ifa1->ifa_address, ifa)) {
				ifap = &ifa->ifa_next;
				continue;
			}
			*ifap = ifa->ifa_next;
			rtmsg_ifa(RTM_DELADDR, ifa);
			notifier_call_chain(&inetaddr_chain, NETDEV_DOWN, ifa);
			inet_free_ifa(ifa);
		}
	}

	/* 3. Announce address deletion */

	/* Send message first, then call notifier.
	   At first sight, FIB update triggered by notifier
	   will refer to already deleted ifaddr, that could confuse
	   netlink listeners. It is not true: look, gated sees
	   that route deleted and if it still thinks that ifaddr
	   is valid, it will try to restore deleted routes... Grr.
	   So that, this order is correct.
	 */
	rtmsg_ifa(RTM_DELADDR, ifa1);
	notifier_call_chain(&inetaddr_chain, NETDEV_DOWN, ifa1);
	if (destroy) {
		inet_free_ifa(ifa1);
		if (in_dev->ifa_list == NULL)
			inetdev_destroy(in_dev);
	}
}

static int
inet_insert_ifa(struct in_device *in_dev, struct in_ifaddr *ifa)
{
	struct in_ifaddr *ifa1, **ifap, **last_primary;

	if (ifa->ifa_local == 0) {
		inet_free_ifa(ifa);
		return 0;
	}

	ifa->ifa_flags &= ~IFA_F_SECONDARY;
	last_primary = &in_dev->ifa_list;

	for (ifap=&in_dev->ifa_list; (ifa1=*ifap)!=NULL; ifap=&ifa1->ifa_next) {
		if (!(ifa1->ifa_flags&IFA_F_SECONDARY) && ifa->ifa_scope <= ifa1->ifa_scope)
			last_primary = &ifa1->ifa_next;
		if (ifa1->ifa_mask == ifa->ifa_mask && inet_ifa_match(ifa1->ifa_address, ifa)) {
			if (ifa1->ifa_local == ifa->ifa_local) {
				inet_free_ifa(ifa);
				return -EEXIST;
			}
			if (ifa1->ifa_scope != ifa->ifa_scope) {
				inet_free_ifa(ifa);
				return -EINVAL;
			}
			ifa->ifa_flags |= IFA_F_SECONDARY;
		}
	}

	if (!(ifa->ifa_flags&IFA_F_SECONDARY))
		ifap = last_primary;

	cli();
	ifa->ifa_next = *ifap;
	*ifap = ifa;
	sti();

	/* Send message first, then call notifier.
	   Notifier will trigger FIB update, so that
	   listeners of netlink will know about new ifaddr */
	rtmsg_ifa(RTM_NEWADDR, ifa);
	notifier_call_chain(&inetaddr_chain, NETDEV_UP, ifa);

	return 0;
}

static int
inet_set_ifa(struct device *dev, struct in_ifaddr *ifa)
{
	struct in_device *in_dev = dev->ip_ptr;

	if (in_dev == NULL) {
		in_dev = inetdev_init(dev);
		if (in_dev == NULL) {
			inet_free_ifa(ifa);
			return -ENOBUFS;
		}
	}
	ifa->ifa_dev = in_dev;
	if (LOOPBACK(ifa->ifa_local))
		ifa->ifa_scope = RT_SCOPE_HOST;
	return inet_insert_ifa(in_dev, ifa);
}

struct in_device *inetdev_by_index(int ifindex)
{
	struct device *dev;
	dev = dev_get_by_index(ifindex);
	if (dev)
		return dev->ip_ptr;
	return NULL;
}

struct in_ifaddr *inet_ifa_byprefix(struct in_device *in_dev, u32 prefix, u32 mask)
{
	for_primary_ifa(in_dev) {
		if (ifa->ifa_mask == mask && inet_ifa_match(prefix, ifa))
			return ifa;
	} endfor_ifa(in_dev);
	return NULL;
}

#ifdef CONFIG_RTNETLINK

/* rtm_{add|del} functions are not reenterable, so that
   this structure can be made static
 */

int
inet_rtm_deladdr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct kern_ifa  *k_ifa = arg;
	struct in_device *in_dev;
	struct ifaddrmsg *ifm = NLMSG_DATA(nlh);
	struct in_ifaddr *ifa, **ifap;

	if ((in_dev = inetdev_by_index(ifm->ifa_index)) == NULL)
		return -EADDRNOTAVAIL;

	for (ifap=&in_dev->ifa_list; (ifa=*ifap)!=NULL; ifap=&ifa->ifa_next) {
		if ((k_ifa->ifa_local && memcmp(k_ifa->ifa_local, &ifa->ifa_local, 4)) ||
		    (k_ifa->ifa_label && strcmp(k_ifa->ifa_label, ifa->ifa_label)) ||
		    (k_ifa->ifa_address &&
		     (ifm->ifa_prefixlen != ifa->ifa_prefixlen ||
		      !inet_ifa_match(*(u32*)k_ifa->ifa_address, ifa))))
			continue;
		inet_del_ifa(in_dev, ifap, 1);
		return 0;
	}

	return -EADDRNOTAVAIL;
}

int
inet_rtm_newaddr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct kern_ifa *k_ifa = arg;
	struct device *dev;
	struct in_device *in_dev;
	struct ifaddrmsg *ifm = NLMSG_DATA(nlh);
	struct in_ifaddr *ifa;

	if (ifm->ifa_prefixlen > 32 || k_ifa->ifa_local == NULL)
		return -EINVAL;

	if ((dev = dev_get_by_index(ifm->ifa_index)) == NULL)
		return -ENODEV;

	if ((in_dev = dev->ip_ptr) == NULL) {
		in_dev = inetdev_init(dev);
		if (!in_dev)
			return -ENOBUFS;
	}

	if ((ifa = inet_alloc_ifa()) == NULL)
		return -ENOBUFS;

	if (k_ifa->ifa_address == NULL)
		k_ifa->ifa_address = k_ifa->ifa_local;
	memcpy(&ifa->ifa_local, k_ifa->ifa_local, 4);
	memcpy(&ifa->ifa_address, k_ifa->ifa_address, 4);
	ifa->ifa_prefixlen = ifm->ifa_prefixlen;
	ifa->ifa_mask = inet_make_mask(ifm->ifa_prefixlen);
	if (k_ifa->ifa_broadcast)
		memcpy(&ifa->ifa_broadcast, k_ifa->ifa_broadcast, 4);
	if (k_ifa->ifa_anycast)
		memcpy(&ifa->ifa_anycast, k_ifa->ifa_anycast, 4);
	ifa->ifa_flags = ifm->ifa_flags;
	ifa->ifa_scope = ifm->ifa_scope;
	ifa->ifa_dev = in_dev;
	if (k_ifa->ifa_label)
		memcpy(ifa->ifa_label, k_ifa->ifa_label, IFNAMSIZ);
	else
		memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);

	return inet_insert_ifa(in_dev, ifa);
}

#endif

/* 
 *	Determine a default network mask, based on the IP address. 
 */

static __inline__ int inet_abc_len(u32 addr)
{
  	if (ZERONET(addr))
  		return 0;

  	addr = ntohl(addr);
  	if (IN_CLASSA(addr)) 
  		return 8;
  	if (IN_CLASSB(addr)) 
  		return 16;
  	if (IN_CLASSC(addr)) 
  		return 24;

	/*
	 *	Something else, probably a multicast. 
	 */
  	 
  	return -1;
}


int devinet_ioctl(unsigned int cmd, void *arg)
{
	struct ifreq ifr;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
	struct in_device *in_dev;
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;
	struct device *dev;
#ifdef CONFIG_IP_ALIAS
	char *colon;
#endif
	int exclusive = 0;
	int ret = 0;

	/*
	 *	Fetch the caller's info block into kernel space
	 */

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;
	ifr.ifr_name[IFNAMSIZ-1] = 0;

#ifdef CONFIG_IP_ALIAS
	colon = strchr(ifr.ifr_name, ':');
	if (colon)
		*colon = 0;
#endif

#ifdef CONFIG_KERNELD
	dev_load(ifr.ifr_name);
#endif

	switch(cmd) {
	case SIOCGIFADDR:	/* Get interface address */
	case SIOCGIFBRDADDR:	/* Get the broadcast address */
	case SIOCGIFDSTADDR:	/* Get the destination address */
	case SIOCGIFNETMASK:	/* Get the netmask for the interface */
	case SIOCGIFPFLAGS:	/* Get per device sysctl controls */	
		/* Note that this ioctls will not sleep,
		   so that we do not impose a lock.
		   One day we will be forced to put shlock here (I mean SMP)
		 */
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		break;

	case SIOCSIFFLAGS:
		if (!suser())
			return -EACCES;
		rtnl_lock();
		exclusive = 1;
		break;
	case SIOCSIFADDR:	/* Set interface address (and family) */
	case SIOCSIFBRDADDR:	/* Set the broadcast address */
	case SIOCSIFDSTADDR:	/* Set the destination address */
	case SIOCSIFNETMASK: 	/* Set the netmask for the interface */
	case SIOCSIFPFLAGS:	/* Set per device sysctl controls */	
		if (!suser())
			return -EACCES;
		if (sin->sin_family != AF_INET)
			return -EINVAL;
		rtnl_lock();
		exclusive = 1;
		break;
	default:
		return -EINVAL;
	}


	if ((dev = dev_get(ifr.ifr_name)) == NULL) {
		ret = -ENODEV;
		goto done;
	}

#ifdef CONFIG_IP_ALIAS
	if (colon)
		*colon = ':';
#endif

	if ((in_dev=dev->ip_ptr) != NULL) {
		for (ifap=&in_dev->ifa_list; (ifa=*ifap) != NULL; ifap=&ifa->ifa_next)
			if (strcmp(ifr.ifr_name, ifa->ifa_label) == 0)
				break;
	}

	if (ifa == NULL && cmd != SIOCSIFADDR && cmd != SIOCSIFFLAGS) {
		ret = -EADDRNOTAVAIL;
		goto done;
	}

	switch(cmd) {
		case SIOCGIFADDR:	/* Get interface address */
			sin->sin_addr.s_addr = ifa->ifa_local;
			goto rarok;

		case SIOCGIFBRDADDR:	/* Get the broadcast address */
			sin->sin_addr.s_addr = ifa->ifa_broadcast;
			goto rarok;

		case SIOCGIFDSTADDR:	/* Get the destination address */
			sin->sin_addr.s_addr = ifa->ifa_address;
			goto rarok;

		case SIOCGIFNETMASK:	/* Get the netmask for the interface */
			sin->sin_addr.s_addr = ifa->ifa_mask;
			goto rarok;

		case SIOCGIFPFLAGS:
			ifr.ifr_flags = in_dev->flags;
			goto rarok;

		case SIOCSIFFLAGS:
#ifdef CONFIG_IP_ALIAS
			if (colon) {
				if (ifa == NULL) {
					ret = -EADDRNOTAVAIL;
					break;
				}
				if (!(ifr.ifr_flags&IFF_UP))
					inet_del_ifa(in_dev, ifap, 1);
				break;
			}
#endif
			ret = dev_change_flags(dev, ifr.ifr_flags);
			break;
	
		case SIOCSIFPFLAGS:
			in_dev->flags = ifr.ifr_flags;
			break;

		case SIOCSIFADDR:	/* Set interface address (and family) */
			if (inet_abc_len(sin->sin_addr.s_addr) < 0) {
				ret = -EINVAL;
				break;
			}

			if (!ifa) {
				if ((ifa = inet_alloc_ifa()) == NULL) {
					ret = -ENOBUFS;
					break;
				}
#ifdef CONFIG_IP_ALIAS
				if (colon)
					memcpy(ifa->ifa_label, ifr.ifr_name, IFNAMSIZ);
				else
#endif
				memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);
			} else {
				ret = 0;
				if (ifa->ifa_local == sin->sin_addr.s_addr)
					break;
				inet_del_ifa(in_dev, ifap, 0);
				ifa->ifa_broadcast = 0;
				ifa->ifa_anycast = 0;
				ifa->ifa_prefixlen = 32;
				ifa->ifa_mask = inet_make_mask(32);
			}

			ifa->ifa_address =
			ifa->ifa_local = sin->sin_addr.s_addr;

			if (!(dev->flags&IFF_POINTOPOINT)) {
				ifa->ifa_prefixlen = inet_abc_len(ifa->ifa_address);
				ifa->ifa_mask = inet_make_mask(ifa->ifa_prefixlen);
				if ((dev->flags&IFF_BROADCAST) && ifa->ifa_prefixlen < 31)
					ifa->ifa_broadcast = ifa->ifa_address|~ifa->ifa_mask;
			}
			ret = inet_set_ifa(dev, ifa);
			break;

		case SIOCSIFBRDADDR:	/* Set the broadcast address */
			if (ifa->ifa_broadcast != sin->sin_addr.s_addr) {
				inet_del_ifa(in_dev, ifap, 0);
				ifa->ifa_broadcast = sin->sin_addr.s_addr;
				inet_insert_ifa(in_dev, ifa);
			}
			break;
	
		case SIOCSIFDSTADDR:	/* Set the destination address */
			if (ifa->ifa_address != sin->sin_addr.s_addr) {
				if (inet_abc_len(sin->sin_addr.s_addr) < 0) {
					ret = -EINVAL;
					break;
				}
				inet_del_ifa(in_dev, ifap, 0);
				ifa->ifa_address = sin->sin_addr.s_addr;
				inet_insert_ifa(in_dev, ifa);
			}
			break;

		case SIOCSIFNETMASK: 	/* Set the netmask for the interface */

			/*
			 *	The mask we set must be legal.
			 */
			if (bad_mask(sin->sin_addr.s_addr, 0)) {
				ret = -EINVAL;
				break;
			}

			if (ifa->ifa_mask != sin->sin_addr.s_addr) {
				inet_del_ifa(in_dev, ifap, 0);
				ifa->ifa_mask = sin->sin_addr.s_addr;
				ifa->ifa_prefixlen = inet_mask_len(ifa->ifa_mask);
				inet_set_ifa(dev, ifa);
			}
			break;
	}
done:
	if (exclusive)
		rtnl_unlock();
	return ret;

rarok:
	if (copy_to_user(arg, &ifr, sizeof(struct ifreq)))
		return -EFAULT;
	return 0;
}

static int
inet_gifconf(struct device *dev, char *buf, int len)
{
	struct in_device *in_dev = dev->ip_ptr;
	struct in_ifaddr *ifa;
	struct ifreq ifr;
	int done=0;

	if (in_dev==NULL || (ifa=in_dev->ifa_list)==NULL)
		return 0;

	for ( ; ifa; ifa = ifa->ifa_next) {
		if (!buf) {
			done += sizeof(ifr);
			continue;
		}
		if (len < sizeof(ifr))
			return done;
		memset(&ifr, 0, sizeof(struct ifreq));
		if (ifa->ifa_label)
			strcpy(ifr.ifr_name, ifa->ifa_label);
		else
			strcpy(ifr.ifr_name, dev->name);

		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_family = AF_INET;
		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_addr.s_addr = ifa->ifa_local;

		if (copy_to_user(buf, &ifr, sizeof(struct ifreq)))
			return -EFAULT;
		buf += sizeof(struct ifreq);
		len -= sizeof(struct ifreq);
		done += sizeof(struct ifreq);
	}
	return done;
}

u32 inet_select_addr(struct device *dev, u32 dst, int scope)
{
	u32 addr = 0;
	struct in_device *in_dev = dev->ip_ptr;

	if (in_dev == NULL)
		return 0;

	for_primary_ifa(in_dev) {
		if (ifa->ifa_scope > scope)
			continue;
		addr = ifa->ifa_local;
		if (!dst || inet_ifa_match(dst, ifa))
			return addr;
	} endfor_ifa(in_dev);

	return addr;
}

/*
 *	Device notifier
 */

int register_inetaddr_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&inetaddr_chain, nb);
}

int unregister_inetaddr_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&inetaddr_chain,nb);
}
 
static int inetdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev = ptr;
	struct in_device *in_dev = dev->ip_ptr;

	if (in_dev == NULL)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
		if (in_dev)
			printk(KERN_DEBUG "inetdev_event: bug\n");
		dev->ip_ptr = NULL;
		break;
	case NETDEV_UP:
		if (dev == &loopback_dev) {
			struct in_ifaddr *ifa;
			if ((ifa = inet_alloc_ifa()) != NULL) {
				ifa->ifa_local =
				ifa->ifa_address = htonl(INADDR_LOOPBACK);
				ifa->ifa_prefixlen = 8;
				ifa->ifa_mask = inet_make_mask(8);
				ifa->ifa_dev = in_dev;
				ifa->ifa_scope = RT_SCOPE_HOST;
				inet_insert_ifa(in_dev, ifa);
			}
		}
		ip_mc_up(in_dev);
		break;
	case NETDEV_DOWN:
		ip_mc_down(in_dev);
		break;
	case NETDEV_UNREGISTER:
		inetdev_destroy(in_dev);
		break;
	}

	return NOTIFY_DONE;
}

struct notifier_block ip_netdev_notifier={
	inetdev_event,
	NULL,
	0
};

#ifdef CONFIG_RTNETLINK

static int inet_fill_ifaddr(struct sk_buff *skb, struct in_ifaddr *ifa,
			    pid_t pid, u32 seq, int event)
{
	struct ifaddrmsg *ifm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*ifm));
	ifm = NLMSG_DATA(nlh);
	ifm->ifa_family = AF_INET;
	ifm->ifa_prefixlen = ifa->ifa_prefixlen;
	ifm->ifa_flags = ifa->ifa_flags;
	ifm->ifa_scope = ifa->ifa_scope;
	ifm->ifa_index = ifa->ifa_dev->dev->ifindex;
	if (ifa->ifa_prefixlen)
		RTA_PUT(skb, IFA_ADDRESS, 4, &ifa->ifa_address);
	if (ifa->ifa_local)
		RTA_PUT(skb, IFA_LOCAL, 4, &ifa->ifa_local);
	if (ifa->ifa_broadcast)
		RTA_PUT(skb, IFA_BROADCAST, 4, &ifa->ifa_broadcast);
	if (ifa->ifa_anycast)
		RTA_PUT(skb, IFA_ANYCAST, 4, &ifa->ifa_anycast);
	if (ifa->ifa_label[0])
		RTA_PUT(skb, IFA_LABEL, IFNAMSIZ, &ifa->ifa_label);
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_put(skb, b - skb->tail);
	return -1;
}

static int inet_dump_ifaddr(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx, ip_idx;
	int s_idx, s_ip_idx;
	struct device *dev;
	struct in_device *in_dev;
	struct in_ifaddr *ifa;

	s_idx = cb->args[0];
	s_ip_idx = ip_idx = cb->args[1];
	for (dev=dev_base, idx=0; dev; dev = dev->next, idx++) {
		if (idx < s_idx)
			continue;
		if (idx > s_idx)
			s_ip_idx = 0;
		if ((in_dev = dev->ip_ptr) == NULL)
			continue;
		for (ifa = in_dev->ifa_list, ip_idx = 0; ifa;
		     ifa = ifa->ifa_next, ip_idx++) {
			if (ip_idx < s_ip_idx)
				continue;
			if (inet_fill_ifaddr(skb, ifa, NETLINK_CB(cb->skb).pid,
					     cb->nlh->nlmsg_seq, RTM_NEWADDR) <= 0)
				goto done;
		}
	}
done:
	cb->args[0] = idx;
	cb->args[1] = ip_idx;

	return skb->len;
}

static void rtmsg_ifa(int event, struct in_ifaddr * ifa)
{
	struct sk_buff *skb;
	int size = NLMSG_SPACE(sizeof(struct ifaddrmsg)+128);

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb) {
		netlink_set_err(rtnl, 0, RTMGRP_IPV4_IFADDR, ENOBUFS);
		return;
	}
	if (inet_fill_ifaddr(skb, ifa, 0, 0, event) < 0) {
		kfree_skb(skb, 0);
		netlink_set_err(rtnl, 0, RTMGRP_IPV4_IFADDR, EINVAL);
		return;
	}
	NETLINK_CB(skb).dst_groups = RTMGRP_IPV4_IFADDR;
	netlink_broadcast(rtnl, skb, 0, RTMGRP_IPV4_IFADDR, GFP_KERNEL);
}


static struct rtnetlink_link inet_rtnetlink_table[RTM_MAX-RTM_BASE+1] =
{
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			rtnetlink_dump_ifinfo,	},
	{ NULL,			NULL,			},

	{ inet_rtm_newaddr,	NULL,			},
	{ inet_rtm_deladdr,	NULL,			},
	{ NULL,			inet_dump_ifaddr,	},
	{ NULL,			NULL,			},

	{ inet_rtm_newroute,	NULL,			},
	{ inet_rtm_delroute,	NULL,			},
	{ inet_rtm_getroute,	inet_dump_fib,		},
	{ NULL,			NULL,			},

	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},

#ifdef CONFIG_IP_MULTIPLE_TABLES
	{ inet_rtm_newrule,	NULL,			},
	{ inet_rtm_delrule,	NULL,			},
	{ NULL,			inet_dump_rules,	},
	{ NULL,			NULL,			},
#else
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
#endif
};

#endif /* CONFIG_RTNETLINK */

#ifdef CONFIG_IP_PNP_BOOTP

/*
 *	Addition and deletion of fake interface addresses
 *	for sending of BOOTP packets. In this case, we must
 *	set the local address to zero which is not permitted
 *	otherwise.
 */

__initfunc(int inet_add_bootp_addr(struct device *dev))
{
	struct in_device *in_dev = dev->ip_ptr;
	struct in_ifaddr *ifa;

	if (!in_dev && !(in_dev = inetdev_init(dev)))
		return -ENOBUFS;
	if (!(ifa = inet_alloc_ifa()))
		return -ENOBUFS;
	ifa->ifa_dev = in_dev;
	in_dev->ifa_list = ifa;
	rtmsg_ifa(RTM_NEWADDR, ifa);
	notifier_call_chain(&inetaddr_chain, NETDEV_UP, ifa);
	return 0;
}

__initfunc(void inet_del_bootp_addr(struct device *dev))
{
	if (dev->ip_ptr)
		inetdev_destroy(dev->ip_ptr);
}

#endif

__initfunc(void devinet_init(void))
{
	register_gifconf(AF_INET, inet_gifconf);
	register_netdevice_notifier(&ip_netdev_notifier);
#ifdef CONFIG_RTNETLINK
	rtnetlink_links[AF_INET] = inet_rtnetlink_table;
#endif
}
