/*
 *	IPv6 Address [auto]configuration
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: addrconf.c,v 1.18 1997/04/16 05:58:03 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Janos Farkas			:	delete timer on ifdown
 *	<chexum@bankinf.banki.hu>
 */

#include <linux/config.h>
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

#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/sit.h>

#include <asm/uaccess.h>

/* Set to 3 to get tracing... */
#define ACONF_DEBUG 2

#if ACONF_DEBUG >= 3
#define ADBG(x) printk x
#else
#define ADBG(x)
#endif

/*
 *	Configured unicast address list
 */
struct inet6_ifaddr		*inet6_addr_lst[IN6_ADDR_HSIZE];

/*
 *	Hash list of configured multicast addresses
 */
struct ifmcaddr6		*inet6_mcast_lst[IN6_ADDR_HSIZE];

/*
 *	AF_INET6 device list
 */
struct inet6_dev		*inet6_dev_lst[IN6_ADDR_HSIZE];

static atomic_t			addr_list_lock = ATOMIC_INIT(0);

void addrconf_verify(unsigned long);

static struct timer_list addr_chk_timer = {
	NULL, NULL,
	0, 0, addrconf_verify
};

static int addrconf_ifdown(struct device *dev);

static void addrconf_dad_start(struct inet6_ifaddr *ifp);
static void addrconf_dad_timer(unsigned long data);
static void addrconf_dad_completed(struct inet6_ifaddr *ifp);
static void addrconf_rs_timer(unsigned long data);

int ipv6_addr_type(struct in6_addr *addr)
{
	u32 st;

	st = addr->s6_addr32[0];

	/* 
	 * UCast Provider Based Address
	 * 0x4/3
	 */

	if ((st & __constant_htonl(0xE0000000)) == __constant_htonl(0x40000000))
		return IPV6_ADDR_UNICAST;

	if ((st & __constant_htonl(0xFF000000)) == __constant_htonl(0xFF000000)) {
		int type = IPV6_ADDR_MULTICAST;

		switch((st & __constant_htonl(0x00FF0000))) {
			case __constant_htonl(0x00010000):
				type |= IPV6_ADDR_LOOPBACK;
				break;

			case __constant_htonl(0x00020000):
				type |= IPV6_ADDR_LINKLOCAL;
				break;

			case __constant_htonl(0x00050000):
				type |= IPV6_ADDR_SITELOCAL;
				break;
		};
		return type;
	}
	
	if ((st & __constant_htonl(0xFFC00000)) == __constant_htonl(0xFE800000))
		return (IPV6_ADDR_LINKLOCAL | IPV6_ADDR_UNICAST);

	if ((st & __constant_htonl(0xFFC00000)) == __constant_htonl(0xFEC00000))
		return (IPV6_ADDR_SITELOCAL | IPV6_ADDR_UNICAST);

	if ((addr->s6_addr32[0] | addr->s6_addr32[1]) == 0) {
		if (addr->s6_addr32[2] == 0) {
			if (addr->in6_u.u6_addr32[3] == 0)
				return IPV6_ADDR_ANY;

			if (addr->s6_addr32[3] == __constant_htonl(0x00000001))
				return (IPV6_ADDR_LOOPBACK | IPV6_ADDR_UNICAST);

			return (IPV6_ADDR_COMPATv4 | IPV6_ADDR_UNICAST);
		}

		if (addr->s6_addr32[2] == __constant_htonl(0x0000ffff))
			return IPV6_ADDR_MAPPED;
	}

	return IPV6_ADDR_RESERVED;
}

static struct inet6_dev * ipv6_add_dev(struct device *dev)
{
	struct inet6_dev *ndev, **bptr, *iter;
	int hash;

	ndev = kmalloc(sizeof(struct inet6_dev), gfp_any());

	if (ndev) {
		memset(ndev, 0, sizeof(struct inet6_dev));

		ndev->dev = dev;
		hash = ipv6_devindex_hash(dev->ifindex);
		bptr = &inet6_dev_lst[hash];
		iter = *bptr;

		for (; iter; iter = iter->next)
			bptr = &iter->next;

		*bptr = ndev;
	}
	return ndev;
}

void addrconf_forwarding_on(void)
{
	struct inet6_dev *idev;
	int i;

	for (i = 0; i < IN6_ADDR_HSIZE; i++) {
		for (idev = inet6_dev_lst[i]; idev; idev = idev->next) {
#if ACONF_DEBUG >= 2
			printk(KERN_DEBUG "dev %s\n", idev->dev->name);
#endif

			if (idev->dev->type == ARPHRD_ETHER) {
				struct in6_addr maddr;

#if ACONF_DEBUG >= 2
				printk(KERN_DEBUG "joining all-routers\n");
#endif
				idev->router = 1;
				ipv6_addr_all_routers(&maddr);
				ipv6_dev_mc_inc(idev->dev, &maddr);
			}
		}
	}
}

struct inet6_dev * ipv6_get_idev(struct device *dev)
{
	struct inet6_dev *idev;
	int hash;

	hash = ipv6_devindex_hash(dev->ifindex);

	for (idev = inet6_dev_lst[hash]; idev; idev = idev->next) {
		if (idev->dev == dev)
			return idev;
	}
	return NULL;
}

struct inet6_ifaddr * ipv6_add_addr(struct inet6_dev *idev, 
				    struct in6_addr *addr, int scope)
{
	struct inet6_ifaddr *ifa;
	int hash;

	ifa = kmalloc(sizeof(struct inet6_ifaddr), gfp_any());

	if (ifa == NULL) {
		ADBG(("ipv6_add_addr: malloc failed\n"));
		return NULL;
	}

	memset(ifa, 0, sizeof(struct inet6_ifaddr));
	memcpy(&ifa->addr, addr, sizeof(struct in6_addr));

	init_timer(&ifa->timer);
	ifa->scope = scope;
	ifa->idev = idev;

	/* Add to list. */
	hash = ipv6_addr_hash(addr);

	ifa->lst_next = inet6_addr_lst[hash];
	inet6_addr_lst[hash] = ifa;

	/* Add to inet6_dev unicast addr list. */
	ifa->if_next = idev->addr_list;
	idev->addr_list = ifa;

	return ifa;
}

void ipv6_del_addr(struct inet6_ifaddr *ifp)
{
	struct inet6_ifaddr *iter, **back;
	int hash;

	if (atomic_read(&addr_list_lock)) {
		ifp->flags |= ADDR_INVALID;
		return;
	}

	hash = ipv6_addr_hash(&ifp->addr);

	iter = inet6_addr_lst[hash];
	back = &inet6_addr_lst[hash];

	for (; iter; iter = iter->lst_next) {
		if (iter == ifp) {
			*back = ifp->lst_next;
			ifp->lst_next = NULL;
			break;
		}
		back = &(iter->lst_next);
	}

	iter = ifp->idev->addr_list;
	back = &ifp->idev->addr_list;

	for (; iter; iter = iter->if_next) {
		if (iter == ifp) {
			*back = ifp->if_next;
			ifp->if_next = NULL;
			break;
		}
		back = &(iter->if_next);
	}
	
	kfree(ifp);
}

/*
 *	Choose an apropriate source address
 *	should do:
 *	i)	get an address with an apropriate scope
 *	ii)	see if there is a specific route for the destination and use
 *		an address of the attached interface 
 *	iii)	don't use deprecated addresses
 *
 *	at the moment I believe only iii) is missing.
 */
struct inet6_ifaddr * ipv6_get_saddr(struct dst_entry *dst,
				     struct in6_addr *daddr)
{
	int scope;
	struct inet6_ifaddr *ifp = NULL;
	struct inet6_ifaddr *match = NULL;
	struct device *dev = NULL;
	struct rt6_info *rt;
	int i;

	rt = (struct rt6_info *) dst;
	if (rt)
		dev = rt->rt6i_dev;
	
	atomic_inc(&addr_list_lock);

	scope = ipv6_addr_scope(daddr);
	if (rt && (rt->rt6i_flags & RTF_ALLONLINK)) {
		/*
		 *	route for the "all destinations on link" rule
		 *	when no routers are present
		 */
		scope = IFA_LINK;
	}

	/*
	 *	known dev
	 *	search dev and walk through dev addresses
	 */

	if (dev) {
		struct inet6_dev *idev;
		int hash;

		if (dev->flags & IFF_LOOPBACK)
			scope = IFA_HOST;

		hash = ipv6_devindex_hash(dev->ifindex);
		for (idev = inet6_dev_lst[hash]; idev; idev=idev->next) {
			if (idev->dev == dev) {
				for (ifp=idev->addr_list; ifp; ifp=ifp->if_next) {
					if (ifp->scope == scope) {
						if (!(ifp->flags & ADDR_STATUS))
							goto out;

						if (!(ifp->flags & ADDR_INVALID))
							match = ifp;
					}
				}
				break;
			}
		}
	}

	if (scope == IFA_LINK)
		goto out;

	/*
	 *	dev == NULL or search failed for specified dev
	 */

	for (i=0; i < IN6_ADDR_HSIZE; i++) {
		for (ifp=inet6_addr_lst[i]; ifp; ifp=ifp->lst_next) {
			if (ifp->scope == scope) {
				if (!(ifp->flags & ADDR_STATUS))
					goto out;

				if (!(ifp->flags & ADDR_INVALID))
					match = ifp;
			}
		}
	}

out:
	if (ifp == NULL && match)
		ifp = match;
	atomic_dec(&addr_list_lock);
	return ifp;
}

struct inet6_ifaddr * ipv6_get_lladdr(struct device *dev)
{
	struct inet6_ifaddr *ifp;
	struct inet6_dev *idev;
	int hash;

	hash = ipv6_devindex_hash(dev->ifindex);

	for (idev = inet6_dev_lst[hash]; idev; idev=idev->next) {
		if (idev->dev == dev) {
			for (ifp=idev->addr_list; ifp; ifp=ifp->if_next) {
				if (ifp->scope == IFA_LINK)
					return ifp;
			}
			break;
		}
	}
	return NULL;
}

/*
 *	Retrieve the ifaddr struct from an v6 address
 *	Called from ipv6_rcv to check if the address belongs 
 *	to the host.
 */

struct inet6_ifaddr * ipv6_chk_addr(struct in6_addr *addr)
{
	struct inet6_ifaddr * ifp;
	u8 hash;

	atomic_inc(&addr_list_lock);

	hash = ipv6_addr_hash(addr);
	for(ifp = inet6_addr_lst[hash]; ifp; ifp=ifp->lst_next) {
		if (ipv6_addr_cmp(&ifp->addr, addr) == 0)
			break;
	}

	atomic_dec(&addr_list_lock);
	return ifp;	
}

void addrconf_prefix_rcv(struct device *dev, u8 *opt, int len)
{
	struct prefix_info *pinfo;
	struct rt6_info *rt;
	__u32 valid_lft;
	__u32 prefered_lft;
	int addr_type;
	unsigned long rt_expires;

	pinfo = (struct prefix_info *) opt;
	
	if (len < sizeof(struct prefix_info)) {
		ADBG(("addrconf: prefix option too short\n"));
		return;
	}
	
	/*
	 *	Validation checks ([ADDRCONF], page 19)
	 */

	addr_type = ipv6_addr_type(&pinfo->prefix);

	if (addr_type & IPV6_ADDR_LINKLOCAL)
		return;

	valid_lft = ntohl(pinfo->valid);
	prefered_lft = ntohl(pinfo->prefered);

	if (prefered_lft > valid_lft) {
		printk(KERN_WARNING "addrconf: prefix option has invalid lifetime\n");
		return;
	}

	/*
	 *	If we where using an "all destinations on link" route
	 *	delete it
	 */

	rt6_purge_dflt_routers(RTF_ALLONLINK);

	/*
	 *	Two things going on here:
	 *	1) Add routes for on-link prefixes
	 *	2) Configure prefixes with the auto flag set
	 */

	rt_expires = jiffies + valid_lft * HZ;
	if (rt_expires < jiffies)
		rt_expires = ~0;

	rt = rt6_lookup(&pinfo->prefix, NULL, dev, RTF_LINKRT);

	if (rt && ((rt->rt6i_flags & (RTF_GATEWAY | RTF_DEFAULT)) == 0)) {
		if (pinfo->onlink == 0 || valid_lft == 0) {
			ip6_del_rt(rt);
			rt = NULL;
		} else {
			rt->rt6i_expires = rt_expires;
		}
	} else if (pinfo->onlink && valid_lft) {
		struct in6_rtmsg rtmsg;
		int err;

		memset(&rtmsg, 0, sizeof(rtmsg));
		
		printk(KERN_DEBUG "adding on link route\n");

		ipv6_addr_copy(&rtmsg.rtmsg_dst, &pinfo->prefix);
		rtmsg.rtmsg_dst_len	= pinfo->prefix_len;
		rtmsg.rtmsg_metric	= IP6_RT_PRIO_ADDRCONF;
		rtmsg.rtmsg_ifindex	= dev->ifindex;
		rtmsg.rtmsg_flags	= RTF_UP | RTF_ADDRCONF;
		rtmsg.rtmsg_info	= rt_expires;

		ip6_route_add(&rtmsg, &err);
	}

	if (pinfo->autoconf && ipv6_config.autoconf) {
		struct inet6_ifaddr * ifp;
		struct in6_addr addr;
		int plen;

		plen = pinfo->prefix_len >> 3;

		if (plen + dev->addr_len == sizeof(struct in6_addr)) {
			memcpy(&addr, &pinfo->prefix, plen);
			memcpy(addr.s6_addr + plen, dev->dev_addr,
			       dev->addr_len);
		} else {
			ADBG(("addrconf: prefix_len invalid\n"));
			return;
		}

		ifp = ipv6_chk_addr(&addr);

		if (ifp == NULL && valid_lft) {
			struct inet6_dev *in6_dev = ipv6_get_idev(dev);

			if (in6_dev == NULL)
				ADBG(("addrconf: device not configured\n"));
			
			ifp = ipv6_add_addr(in6_dev, &addr,
					    addr_type & IPV6_ADDR_SCOPE_MASK);

			if (dev->flags & IFF_MULTICAST) {
				struct in6_addr maddr;

				/* Join to solicited addr multicast group. */
				addrconf_addr_solict_mult(&addr, &maddr);
				ipv6_dev_mc_inc(dev, &maddr);
			}

			ifp->prefix_len = pinfo->prefix_len;

			addrconf_dad_start(ifp);
		}

		if (ifp && valid_lft == 0) {
			ipv6_del_addr(ifp);
			ifp = NULL;
		}

		if (ifp) {
			ifp->valid_lft = valid_lft;
			ifp->prefered_lft = prefered_lft;
			ifp->tstamp = jiffies;
		}
	}
}

/*
 *	Set destination address.
 *	Special case for SIT interfaces where we create a new "virtual"
 *	device.
 */
int addrconf_set_dstaddr(void *arg)
{
	struct in6_ifreq ireq;
	struct device *dev;
	int err = -EINVAL;

	if (copy_from_user(&ireq, arg, sizeof(struct in6_ifreq))) {
		err = -EFAULT;
		goto err_exit;
	}

	dev = dev_get_by_index(ireq.ifr6_ifindex);

	if (dev == NULL) {
		err = -ENODEV;
		goto err_exit;
	}

	if (dev->type == ARPHRD_SIT) {
		struct device *dev;
		
		if (!(ipv6_addr_type(&ireq.ifr6_addr) & IPV6_ADDR_COMPATv4))
			return -EADDRNOTAVAIL;
		
		dev = sit_add_tunnel(ireq.ifr6_addr.s6_addr32[3]);
		
		if (dev == NULL)
			err = -ENODEV;
		else
			err = 0;
	}

err_exit:
	return err;
}

/*
 *	Manual configuration of address on an interface
 */
int addrconf_add_ifaddr(void *arg)
{
	struct inet6_dev *idev;
	struct in6_ifreq ireq;
	struct inet6_ifaddr *ifp;
	struct device *dev;
	int scope;
	
	if (!suser())
		return -EPERM;
	
	if(copy_from_user(&ireq, arg, sizeof(struct in6_ifreq)))
		return -EFAULT;

	if((dev = dev_get_by_index(ireq.ifr6_ifindex)) == NULL)
		return -EINVAL;

	if ((idev = ipv6_get_idev(dev)) == NULL)
		return -EINVAL;

	scope = ipv6_addr_scope(&ireq.ifr6_addr);

	if((ifp = ipv6_add_addr(idev, &ireq.ifr6_addr, scope)) == NULL)
		return -ENOMEM;

	ifp->prefix_len = 128;

	if (dev->flags & IFF_MULTICAST) {
		struct in6_addr maddr;

		/* Join to solicited addr multicast group. */
		addrconf_addr_solict_mult(&ireq.ifr6_addr, &maddr);
		ipv6_dev_mc_inc(dev, &maddr);
	}

	ifp->prefix_len = ireq.ifr6_prefixlen;
	ifp->flags |= ADDR_PERMANENT;

	if (!(dev->flags & (IFF_NOARP|IFF_LOOPBACK)))
		addrconf_dad_start(ifp);
	else
		ip6_rt_addr_add(&ifp->addr, dev);

	return 0;
}

static void sit_route_add(struct device *dev)
{
	struct in6_rtmsg rtmsg;
	struct rt6_info *rt;
	int err;

	ADBG(("sit_route_add(%s): ", dev->name));
	memset(&rtmsg, 0, sizeof(rtmsg));

	rtmsg.rtmsg_type			= RTMSG_NEWROUTE;
	rtmsg.rtmsg_metric			= IP6_RT_PRIO_ADDRCONF;

	if (dev->pa_dstaddr == 0) {
		ADBG(("pa_dstaddr=0, "));
		/* prefix length - 96 bytes "::d.d.d.d" */
		rtmsg.rtmsg_dst_len		= 96;
		rtmsg.rtmsg_flags		= RTF_NONEXTHOP|RTF_UP;
	} else {
		ADBG(("pa_dstaddr=%08x, ", dev->pa_dstaddr));
		rtmsg.rtmsg_dst_len 		= 10;
		rtmsg.rtmsg_dst.s6_addr32[0]	= __constant_htonl(0xfe800000);
		rtmsg.rtmsg_dst.s6_addr32[3]	= dev->pa_dstaddr;
		rtmsg.rtmsg_gateway.s6_addr32[3]= dev->pa_dstaddr;
		rtmsg.rtmsg_flags		= RTF_UP;
	}

	rtmsg.rtmsg_ifindex 			= dev->ifindex; 
	ADBG(("doing ip6_route_add()\n"));
	rt = ip6_route_add(&rtmsg, &err);
	
	if (err) {
#if ACONF_DEBUG >= 1
		printk(KERN_DEBUG "sit_route_add: error %d in route_add\n", err);
#endif
	}

	ADBG(("sit_route_add(cont): "));
	if (dev->pa_dstaddr) {
		struct rt6_info *mrt;

		ADBG(("pa_dstaddr != 0, "));
		rt->rt6i_nexthop = ndisc_get_neigh(dev, &rtmsg.rtmsg_gateway);
		if (rt->rt6i_nexthop == NULL) {
			ADBG(("can't get neighbour\n"));
			printk(KERN_DEBUG "sit_route: get_neigh failed\n");
		}

		/*
		 *	Add multicast route.
		 */
		ADBG(("add MULT, "));
		ipv6_addr_set(&rtmsg.rtmsg_dst, __constant_htonl(0xFF000000), 0, 0, 0);

		rtmsg.rtmsg_dst_len = 8;
		rtmsg.rtmsg_flags = RTF_UP;
		rtmsg.rtmsg_metric = IP6_RT_PRIO_ADDRCONF;

		memset(&rtmsg.rtmsg_gateway, 0, sizeof(struct in6_addr));
		ADBG(("doing ip6_route_add()\n"));
		mrt = ip6_route_add(&rtmsg, &err);

		if (mrt)
			mrt->rt6i_nexthop = ndisc_get_neigh(dev, &rtmsg.rtmsg_dst);
	} else {
		ADBG(("pa_dstaddr==0\n"));
	}
}

static void sit_add_v4_addrs(struct inet6_dev *idev)
{
	struct inet6_ifaddr * ifp;
	struct in6_addr addr;
	struct device *dev;
	int scope;

	memset(&addr, 0, sizeof(struct in6_addr));

	if (idev->dev->pa_dstaddr) {
		addr.s6_addr32[0] = __constant_htonl(0xfe800000);
		scope = IFA_LINK;
	} else {
		scope = IPV6_ADDR_COMPATv4;
	}

        for (dev = dev_base; dev != NULL; dev = dev->next) {
		if (dev->family == AF_INET && (dev->flags & IFF_UP)) {
			int flag = scope;
			
			addr.s6_addr32[3] = dev->pa_addr;

			if (dev->flags & IFF_LOOPBACK) {
				if (idev->dev->pa_dstaddr)
					continue;
				
				flag |= IFA_HOST;
			}

			ifp = ipv6_add_addr(idev, &addr, flag);

			if (ifp == NULL)
				continue;

			ifp->flags |= ADDR_PERMANENT;
			ip6_rt_addr_add(&ifp->addr, dev);
		}
        }
}

static void init_loopback(struct device *dev)
{
	struct in6_addr addr;
	struct inet6_dev  *idev;
	struct inet6_ifaddr * ifp;
	int err;

	/* ::1 */

	memset(&addr, 0, sizeof(struct in6_addr));
	addr.s6_addr[15] = 1;

	idev = ipv6_add_dev(dev);

	if (idev == NULL) {
		printk(KERN_DEBUG "init loopback: add_dev failed\n");
		return;
	}

	ifp = ipv6_add_addr(idev, &addr, IFA_HOST);

	if (ifp == NULL) {
		printk(KERN_DEBUG "init_loopback: add_addr failed\n");
		return;
	}

	ifp->flags |= ADDR_PERMANENT;

	err = ip6_rt_addr_add(&addr, dev);
	if (err)
		printk(KERN_DEBUG "init_loopback: error in route_add\n");
}

static void addrconf_eth_config(struct device *dev)
{
	struct in6_addr addr;
	struct in6_addr maddr;
	struct inet6_ifaddr * ifp;
	struct inet6_dev    * idev;

	memset(&addr, 0, sizeof(struct in6_addr));

	/* Generate link local address. */
	addr.s6_addr[0] = 0xFE;
	addr.s6_addr[1] = 0x80;

	memcpy(addr.s6_addr + (sizeof(struct in6_addr) - dev->addr_len), 
	       dev->dev_addr, dev->addr_len);

	idev = ipv6_add_dev(dev);
	if (idev == NULL)
		return;
	
	ifp = ipv6_add_addr(idev, &addr, IFA_LINK);
	if (ifp == NULL)
		return;

	ifp->flags = ADDR_PERMANENT;
	ifp->prefix_len = 10;

	/* Join to all nodes multicast group. */
	ipv6_addr_all_nodes(&maddr);
	ipv6_dev_mc_inc(dev, &maddr);

	if (ipv6_config.forwarding) {
		idev->router = 1;
		ipv6_addr_all_routers(&maddr);
		ipv6_dev_mc_inc(dev, &maddr);
	}

	/* Join to solicited addr multicast group. */
	addrconf_addr_solict_mult(&addr, &maddr);
	ipv6_dev_mc_inc(dev, &maddr);

	/* Start duplicate address detection. */
	addrconf_dad_start(ifp);
}

int addrconf_notify(struct notifier_block *this, unsigned long event, 
		    void * data)
{
	struct device *dev;
	struct inet6_dev    * idev;

	dev = (struct device *) data;

	switch(event) {
	case NETDEV_UP:
		switch(dev->type) {
		case ARPHRD_SIT:

			printk(KERN_DEBUG "sit device up: %s\n", dev->name);

			/* 
			 * Configure the tunnel with one of our IPv4 
			 * addresses... we should configure all of 
			 * our v4 addrs in the tunnel
			 */

			idev = ipv6_add_dev(dev);
			
			sit_add_v4_addrs(idev);

			/*
			 *  we do an hack for now to configure the tunnel
			 *  route.
			 */

			sit_route_add(dev);
			break;

		case ARPHRD_LOOPBACK:
			init_loopback(dev);
			break;

		case ARPHRD_ETHER:
			printk(KERN_DEBUG "Configuring eth interface\n");
			addrconf_eth_config(dev);
			break;
		};

		rt6_sndmsg(RTMSG_NEWDEVICE, NULL, NULL, NULL, dev, 0, 0, 0, 0);
		break;

	case NETDEV_DOWN:
		/*
		 *	Remove all addresses from this interface
		 *	and take the interface out of the list.
		 */
		if (addrconf_ifdown(dev) == 0) {
#if 0
			rt6_ifdown(dev);
#endif
			rt6_sndmsg(RTMSG_DELDEVICE, NULL, NULL, NULL, dev, 0, 0, 0, 0);
		}

		break;
	};
	
	return NOTIFY_OK;
}


static int addrconf_ifdown(struct device *dev)
{
	struct inet6_dev *idev, **bidev;
	struct inet6_ifaddr *ifa, **bifa;
	int i, hash;

	start_bh_atomic();

	hash = ipv6_devindex_hash(dev->ifindex);
	bidev = &inet6_dev_lst[hash];

	for (idev = inet6_dev_lst[hash]; idev; idev = idev->next) {
		if (idev->dev == dev) {
			*bidev = idev->next;
			break;
		}
		bidev = &idev->next;
	}

	if (idev == NULL) {
		printk(KERN_DEBUG "addrconf_ifdown: device not found\n");
		end_bh_atomic();
		return -ENODEV;
	}

	/*
	 *	FIXME: clear multicast group membership
	 */

	/*
	 *	clean addr_list
	 */

	for (i=0; i<16; i++) {
		bifa = &inet6_addr_lst[i];

		for (ifa=inet6_addr_lst[i]; ifa; ) {
			if (ifa->idev == idev) {
				*bifa = ifa->lst_next;
				del_timer(&ifa->timer);
				kfree(ifa);
				ifa = *bifa;
				continue;
			}
			ifa = ifa->lst_next;
			bifa = &ifa->lst_next;
		}
	}

	kfree(idev);
	end_bh_atomic();
	return 0;
}

static void addrconf_rs_timer(unsigned long data)
{
	struct inet6_ifaddr *ifp;

	ifp = (struct inet6_ifaddr *) data;

	if (ipv6_config.forwarding)
		return;

	if (ifp->idev->if_flags & IF_RA_RCVD) {
		/*
		 *	Announcement received after solicitation
		 *	was sent
		 */
		return;
	}

	if (ifp->probes++ <= ipv6_config.rtr_solicits) {
		struct in6_addr all_routers;

		ipv6_addr_set(&all_routers,
			      __constant_htonl(0xff020000U), 0, 0,
			      __constant_htonl(0x2U));

		ndisc_send_rs(ifp->idev->dev, &ifp->addr,
			      &all_routers);
		
		ifp->timer.function = addrconf_rs_timer;
		ifp->timer.expires = (jiffies + 
				      ipv6_config.rtr_solicit_interval);
		add_timer(&ifp->timer);
	} else {
		struct in6_rtmsg rtmsg;
		int err;

#if ACONF_DEBUG >= 2
		printk(KERN_DEBUG "%s: no IPv6 routers present\n",
		       ifp->idev->dev->name);
#endif

		memset(&rtmsg, 0, sizeof(struct in6_rtmsg));
		rtmsg.rtmsg_type = RTMSG_NEWROUTE;
		rtmsg.rtmsg_metric = IP6_RT_PRIO_ADDRCONF;
		rtmsg.rtmsg_flags = (RTF_ALLONLINK | RTF_ADDRCONF | 
				     RTF_DEFAULT | RTF_UP);

		rtmsg.rtmsg_ifindex = ifp->idev->dev->ifindex;

		ip6_route_add(&rtmsg, &err);
	}
}

/*
 *	Duplicate Address Detection
 */
static void addrconf_dad_start(struct inet6_ifaddr *ifp)
{
	static int rand_seed = 1;
	struct device *dev;
	unsigned long rand_num;

	dev = ifp->idev->dev;

	if (dev->flags & IFF_MULTICAST) {
		struct in6_rtmsg rtmsg;
		struct rt6_info *mrt;
		int err;

		memset(&rtmsg, 0, sizeof(rtmsg));
		ipv6_addr_set(&rtmsg.rtmsg_dst,
			      __constant_htonl(0xFF000000), 0, 0, 0);

		rtmsg.rtmsg_dst_len = 8;
		rtmsg.rtmsg_metric = IP6_RT_PRIO_ADDRCONF;
		rtmsg.rtmsg_ifindex = dev->ifindex;

		rtmsg.rtmsg_flags = RTF_UP;

		mrt = ip6_route_add(&rtmsg, &err);

		if (err)
			printk(KERN_DEBUG "dad_start: mcast route add failed\n");
		else
			mrt->rt6i_nexthop = ndisc_get_neigh(dev, &rtmsg.rtmsg_dst);
	}

	if (rand_seed) {
		rand_seed = 0;
		nd_rand_seed = ifp->addr.s6_addr32[3];
	}

	init_timer(&ifp->timer);

	ifp->probes = ipv6_config.dad_transmits;
	ifp->flags |= DAD_INCOMPLETE;

	rand_num = ipv6_random() % ipv6_config.rtr_solicit_delay;

	ifp->timer.function = addrconf_dad_timer;
	ifp->timer.data = (unsigned long) ifp;
	ifp->timer.expires = jiffies + rand_num;

	add_timer(&ifp->timer);
}

static void addrconf_dad_timer(unsigned long data)
{
	struct inet6_ifaddr *ifp;
	struct in6_addr unspec;
	struct in6_addr mcaddr;

	ifp = (struct inet6_ifaddr *) data;

	if (ifp->probes == 0) {
		/*
		 * DAD was successful
		 */

		ifp->flags &= ~DAD_INCOMPLETE;
		addrconf_dad_completed(ifp);
		return;
	}

	ifp->probes--;

	/* send a neighbour solicitation for our addr */
	memset(&unspec, 0, sizeof(unspec));
	addrconf_addr_solict_mult(&ifp->addr, &mcaddr);

	ndisc_send_ns(ifp->idev->dev, NULL, &ifp->addr, &mcaddr, &unspec);

	ifp->timer.expires = jiffies + ipv6_config.rtr_solicit_interval;
	add_timer(&ifp->timer);
}

static void addrconf_dad_completed(struct inet6_ifaddr *ifp)
{
	struct device *dev;
	int err;

	dev = ifp->idev->dev;

	if (ipv6_addr_type(&ifp->addr) & IPV6_ADDR_LINKLOCAL) {
		struct in6_rtmsg rtmsg;
		struct in6_addr all_routers;

		/*
		 *	1) configure a link route for this interface
		 *	2) send a (delayed) router solicitation
		 */

		memset(&rtmsg, 0, sizeof(rtmsg));
		
		memcpy(&rtmsg.rtmsg_dst, &ifp->addr, sizeof(struct in6_addr));

		rtmsg.rtmsg_dst_len = ifp->prefix_len;
		rtmsg.rtmsg_metric = IP6_RT_PRIO_ADDRCONF;
		rtmsg.rtmsg_ifindex = dev->ifindex;

		rtmsg.rtmsg_flags = RTF_UP;

		ip6_route_add(&rtmsg, &err);
		
		if (err)
			printk(KERN_DEBUG "dad_complete: error in route_add\n");

		if (ipv6_config.forwarding == 0) {
			ipv6_addr_set(&all_routers,
				      __constant_htonl(0xff020000U), 0, 0,
				      __constant_htonl(0x2U));

			/*
			 *	If a host as already performed a random delay
			 *	[...] as part of DAD [...] there is no need
			 *	to delay again before sending the first RS
			 */
			ndisc_send_rs(ifp->idev->dev, &ifp->addr,
				      &all_routers);

			ifp->probes = 1;
			ifp->timer.function = addrconf_rs_timer;
			ifp->timer.expires = (jiffies +
					      ipv6_config.rtr_solicit_interval);
			ifp->idev->if_flags |= IF_RS_SENT;
			add_timer(&ifp->timer);
		}
	}
	
	/*
	 *	configure the address for reception
	 */

	ip6_rt_addr_add(&ifp->addr, dev);
}

#ifdef CONFIG_PROC_FS
static int iface_proc_info(char *buffer, char **start, off_t offset,
			   int length, int dummy)
{
	struct inet6_ifaddr *ifp;
	int i;
	int len = 0;

	for (i=0; i < IN6_ADDR_HSIZE; i++)
		for (ifp=inet6_addr_lst[i]; ifp; ifp=ifp->lst_next) {
			int j;

			for (j=0; j<16; j++) {
				sprintf(buffer + len, "%02x",
					ifp->addr.s6_addr[j]);
				len += 2;
			}

			len += sprintf(buffer + len,
				       " %02x %02x %02x %02x %8s\n",
				       ifp->idev->dev->ifindex,
				       ifp->prefix_len,
				       ifp->scope,
				       ifp->flags,
				       ifp->idev->dev->name);
		}

	*start = buffer + offset;

	len -= offset;

	if (len > length)
		len = length;
	return len;
}

struct proc_dir_entry iface_proc_entry =
{
        0, 8, "if_inet6",
        S_IFREG | S_IRUGO, 1, 0, 0,
        0, NULL,
        &iface_proc_info
};
#endif	/* CONFIG_PROC_FS */

/*
 *	Periodic address status verification
 */

void addrconf_verify(unsigned long foo)
{
	struct inet6_ifaddr *ifp;
	unsigned long now = jiffies;
	int i;

	for (i=0; i < IN6_ADDR_HSIZE; i++) {
		for (ifp=inet6_addr_lst[i]; ifp;) {
			if (!(ifp->flags & ADDR_PERMANENT)) {
				struct inet6_ifaddr *bp;
				unsigned long age;

				age = (now - ifp->tstamp) / HZ;

				if (age > ifp->prefered_lft)
					ifp->flags |= ADDR_DEPRECATED;

				bp = ifp;
				ifp=ifp->lst_next;
				
				if (age > bp->valid_lft)
					ipv6_del_addr(bp);

				continue;
			}
			ifp=ifp->lst_next;
		}
	}

	addr_chk_timer.expires = jiffies + ADDR_CHECK_FREQUENCY;
	add_timer(&addr_chk_timer);	
}

/*
 *	Init / cleanup code
 */

void addrconf_init()
{
	struct device *dev;

	/*
	 *	init address and device hash lists
	 */

	memset(inet6_addr_lst, 0, IN6_ADDR_HSIZE * sizeof(struct inet6_ifaddr *));

	memset(inet6_mcast_lst, 0, IN6_ADDR_HSIZE * sizeof(struct ifmcaddr6 *));

	memset(inet6_dev_lst, 0, IN6_ADDR_HSIZE * sizeof(struct inet6_dev *));

	/*
	 *	Init loopback device
	 */

	dev = dev_get("lo");

	if (dev && (dev->flags & IFF_UP))
		init_loopback(dev);

	/*
	 *	and maybe:
	 *	search availiable AF_INET devs and try to configure them
	 */

	dev = dev_get("eth0");

	if (dev && (dev->flags & IFF_UP))
		addrconf_eth_config(dev);
	
#ifdef CONFIG_PROC_FS
	proc_net_register(&iface_proc_entry);
#endif
	
	addr_chk_timer.expires = jiffies + ADDR_CHECK_FREQUENCY;
	add_timer(&addr_chk_timer);
}

#ifdef MODULE
void addrconf_cleanup(void)
{
 	struct inet6_dev *idev;
 	struct inet6_ifaddr *ifa;
	int i;

	del_timer(&addr_chk_timer);

	/*
	 *	clean dev list.
	 */

	for (i=0; i < IN6_ADDR_HSIZE; i++) {
		for (idev = inet6_dev_lst[i]; idev; ) {
			struct inet6_dev *back;

			back = idev;
			idev = idev->next;
			kfree(back);
		}
	}

	/*
	 *	clean addr_list
	 */

	for (i=0; i < IN6_ADDR_HSIZE; i++) {
		for (ifa=inet6_addr_lst[i]; ifa; ) {
			struct inet6_ifaddr *bifa;

			bifa = ifa;
			ifa = ifa->lst_next;
			kfree(bifa);
		}
	}

#ifdef CONFIG_PROC_FS
	proc_net_unregister(iface_proc_entry.low_ino);
#endif
}
#endif	/* MODULE */
