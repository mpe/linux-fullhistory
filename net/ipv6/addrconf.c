/*
 *	IPv6 Address [auto]configuration
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */


#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/ndisc.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>
#include <net/sit.h>

#include <asm/uaccess.h>

#define HASH_SIZE		16
/*
 *	Configured unicast address list
 */
struct inet6_ifaddr		*inet6_addr_lst[HASH_SIZE];

/*
 *	Hash list of configured multicast addresses
 */
struct ipv6_mc_list		*inet6_mcast_lst[HASH_SIZE];

/*
 *	AF_INET6 device list
 */
struct inet6_dev		*inet6_dev_lst;
int				in6_ifnum = 0;

atomic_t			addr_list_lock = 0;

void addrconf_verify(unsigned long);

static struct timer_list addr_chk_timer = {
	NULL, NULL,
	0, 0, addrconf_verify
};


int DupAddrDetectTransmits = 1;

/*
 *	/proc/sys switch for autoconf (enabled by default)
 */
int addrconf_sys_autoconf  = 1;

static void addrconf_dad_start(struct inet6_ifaddr *ifp);
static void addrconf_rs_timer(unsigned long data);

int ipv6_addr_type(struct in6_addr *addr)
{
	u32 st;

	st = addr->s6_addr32[0];

	/* 
	 * UCast Provider Based Address
	 * 0x4/3
	 */

	if ((st & __constant_htonl(0xE0000000)) == 
	    __constant_htonl(0x40000000))
	{
		return IPV6_ADDR_UNICAST;
	}

	if ((st & __constant_htonl(0xFF000000)) == 
	    __constant_htonl(0xFF000000))
	{
		int type = IPV6_ADDR_MULTICAST;

		switch((st >> 16) & 0x0f)
		{
			case 0x01:
				type |= IPV6_ADDR_LOOPBACK;
				break;
			case 0x02:
				type |= IPV6_ADDR_LINKLOCAL;
				break;
			case 0x05:
				type |= IPV6_ADDR_SITELOCAL;
				break;
		}
		return type;
	}
	
	if ((st & __constant_htonl(0xFFC00000)) == 
	    __constant_htonl(0xFE800000))
	{
		return (IPV6_ADDR_LINKLOCAL | IPV6_ADDR_UNICAST);
	}

	if ((st & __constant_htonl(0xFFC00000)) == 
	    __constant_htonl(0xFEC00000))
	{
		return (IPV6_ADDR_SITELOCAL | IPV6_ADDR_UNICAST);
	}

	if ((addr->s6_addr32[0] | addr->s6_addr32[1]) == 0)
	{
		if (addr->s6_addr32[2] == 0)
		{
			if (addr->in6_u.u6_addr32[3] == 0)
			{
				return IPV6_ADDR_ANY;
			}

			if (addr->s6_addr32[3] == __constant_htonl(0x00000001))
			{
				return (IPV6_ADDR_LOOPBACK | 
					IPV6_ADDR_UNICAST);
			}

			return (IPV6_ADDR_COMPATv4 | IPV6_ADDR_UNICAST);
		}

		if (addr->s6_addr32[2] == __constant_htonl(0x0000ffff))
			return IPV6_ADDR_MAPPED;
	}

	return IPV6_ADDR_RESERVED;
}

struct inet6_dev * ipv6_add_dev(struct device *dev)
{
	struct inet6_dev *dev6;

	/*
	 *	called by netdev notifier from a syscall
	 */
	dev6 = (struct inet6_dev *) kmalloc(sizeof(struct inet6_dev), 
					    GFP_ATOMIC);

	if (dev6 == NULL)
		return NULL;

	memset(dev6, 0, sizeof(struct inet6_dev));
	dev6->dev = dev;
	dev6->if_index = ++in6_ifnum;

	/*
	 *	insert at head.
	 */

	dev6->next = inet6_dev_lst;
	inet6_dev_lst = dev6;

	return dev6;
}

struct inet6_dev * ipv6_dev_by_index(int index)
{
	struct inet6_dev *in6_dev;

	for (in6_dev = inet6_dev_lst; in6_dev; in6_dev = in6_dev->next)
	{
		if (in6_dev->if_index == index)
			return in6_dev;
	}

	return NULL;
}

void addrconf_forwarding_on(void)
{
	struct inet6_dev *in6_dev;
	struct in6_addr maddr;

	for (in6_dev = inet6_dev_lst; in6_dev; in6_dev = in6_dev->next)
	{
		printk(KERN_DEBUG "dev %s\n", in6_dev->dev->name);

		if (in6_dev->dev->type == ARPHRD_ETHER)
		{
			printk(KERN_DEBUG "joining all-routers\n");
			in6_dev->router = 1;
			ipv6_addr_all_routers(&maddr);
			ipv6_dev_mc_inc(in6_dev->dev, &maddr);		
		}
	}

	if (last_resort_rt && (last_resort_rt->rt_flags & RTI_ALLONLINK))
	{
		rt_release(last_resort_rt);
		last_resort_rt = NULL;
	}
}

struct inet6_dev * ipv6_get_idev(struct device *dev)
{
	struct inet6_dev *in6_dev;

	for (in6_dev = inet6_dev_lst; in6_dev; in6_dev = in6_dev->next)
	{
		if (in6_dev->dev == dev)
		{
			return in6_dev;
		}
	}
	return NULL;
}

struct inet6_ifaddr * ipv6_add_addr(struct inet6_dev *idev, 
				    struct in6_addr *addr, int scope)
{
	struct inet6_ifaddr * ifaddr;
	int hash;
	unsigned long flags;

	save_flags(flags);
	cli();

	ifaddr = (struct inet6_ifaddr *) kmalloc(sizeof(struct inet6_ifaddr), 
						 GFP_ATOMIC);

	if (ifaddr == NULL)
	{
		printk(KERN_DEBUG "ipv6_add_addr: malloc failed\n");
		restore_flags(flags);
		return NULL;
	}

	memset(ifaddr, 0, sizeof(struct inet6_ifaddr));
	memcpy(&ifaddr->addr, addr, sizeof(struct in6_addr));

	ifaddr->scope = scope;
	ifaddr->idev = idev;
	

	/* add to list */

	hash = ipv6_addr_hash(addr);

	ifaddr->lst_next = inet6_addr_lst[hash];
	inet6_addr_lst[hash] = ifaddr;


	/* add to inet6_dev unicast addr list */
	ifaddr->if_next = idev->addr_list;
	idev->addr_list = ifaddr;

	restore_flags(flags);
	return ifaddr;
	
}

void ipv6_del_addr(struct inet6_ifaddr *ifp)
{
	struct inet6_ifaddr *iter, **back;
	int hash;

	if (addr_list_lock)
	{
		ifp->flags |= ADDR_INVALID;
		return;
	}

	hash = ipv6_addr_hash(&ifp->addr);

	iter = inet6_addr_lst[hash];
	back = &inet6_addr_lst[hash];

	for (; iter; iter = iter->lst_next)
	{
		if (iter == ifp)
		{
			*back = ifp->lst_next;
			ifp->lst_next = NULL;
			break;
		}
		back = &(iter->lst_next);
	}

	iter = ifp->idev->addr_list;
	back = &ifp->idev->addr_list;

	for (; iter; iter = iter->if_next)
	{
		if (iter == ifp)
		{
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
 *	at the moment i believe only iii) is missing.
 */
struct inet6_ifaddr * ipv6_get_saddr(struct rt6_info *rt, struct in6_addr *daddr)
{
	int scope;
	struct inet6_ifaddr * ifp = NULL;
	struct inet6_dev    * i6dev;
	struct inet6_ifaddr * match = NULL;
	struct device *dev = NULL;
	int i;

	if (rt)
	{
		dev = rt->rt_dev;
	}
	
	atomic_inc(&addr_list_lock);

	scope = ipv6_addr_type(daddr);

	scope &= IPV6_ADDR_SCOPE_MASK;

	if (rt && (rt->rt_flags & RTI_ALLONLINK))
	{
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

	if (dev)
	{
		if (dev->flags & IFF_LOOPBACK)
		{
			scope = IFA_HOST;
		}

		for (i6dev = inet6_dev_lst; i6dev; i6dev=i6dev->next)
		{
			if (i6dev->dev == dev)
			{
				for (ifp=i6dev->addr_list; ifp; 
				     ifp=ifp->if_next)
				{
					if (ifp->scope == scope)
					{
						if (!(ifp->flags & ADDR_STATUS))
						{
							goto out;
						}
						if (!(ifp->flags & ADDR_INVALID))
						{
							match = ifp;
						}
					}
				}
				break;
			}
		}
	}

	if (scope == IFA_LINK)
	{
		goto out;
	}

	/*
	 *	dev == NULL or search failed for specified dev
	 */

	for (i=0; i < HASH_SIZE; i++)
	{
		for (ifp=inet6_addr_lst[i]; ifp; ifp=ifp->lst_next)
		{
			if (ifp->scope == scope)
			{
				if (!(ifp->flags & ADDR_STATUS))
				{
					goto out;
				}
				if (!(ifp->flags & ADDR_INVALID))
				{
					match = ifp;
				}
			}
		}
	}

  out:
	if (ifp == NULL && match)
	{
		ifp = match;
	}
	atomic_dec(&addr_list_lock);
	return ifp;
}

struct inet6_ifaddr * ipv6_get_lladdr(struct device *dev)
{
	struct inet6_ifaddr *ifp;
	struct inet6_dev *i6dev;

	for (i6dev = inet6_dev_lst; i6dev; i6dev=i6dev->next)
	{
		if (i6dev->dev == dev)
		{
			for (ifp=i6dev->addr_list; ifp; ifp=ifp->if_next)
			{
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

	for(ifp = inet6_addr_lst[hash]; ifp; ifp=ifp->lst_next)
	{
		if (ipv6_addr_cmp(&ifp->addr, addr) == 0)
		{
			break;
		}
	}

	atomic_dec(&addr_list_lock);
	return ifp;	
}

static void sit_route_add(struct device *dev)
{
	struct in6_rtmsg rtmsg;	
	int err;

	rtmsg.rtmsg_type = RTMSG_NEWROUTE;

	memset(&rtmsg.rtmsg_dst, 0, sizeof(struct in6_addr));
	memset(&rtmsg.rtmsg_gateway, 0, sizeof(struct in6_addr));

	if (dev->pa_dstaddr == 0)
	{
		/* prefix length - 96 bytes "::d.d.d.d" */
		rtmsg.rtmsg_prefixlen = 96;
		rtmsg.rtmsg_metric = 1;
		rtmsg.rtmsg_flags = RTF_NEXTHOP|RTF_UP;
	}
	else
	{
		rtmsg.rtmsg_prefixlen = 128;
		rtmsg.rtmsg_dst.s6_addr32[0] = __constant_htonl(0xfe800000);
		rtmsg.rtmsg_dst.s6_addr32[3] = dev->pa_dstaddr;
		rtmsg.rtmsg_metric = 1;
		rtmsg.rtmsg_flags = RTF_HOST|RTF_UP;
	}

	strcpy(rtmsg.rtmsg_device, dev->name);

	err = ipv6_route_add(&rtmsg);

	if (err)
	{
		printk(KERN_DEBUG "sit_route_add: error in route_add\n");
	}
}

static void init_loopback(struct device *dev)
{
	struct in6_addr addr;
	struct inet6_dev  *idev;
	struct inet6_ifaddr * ifp;
	struct in6_rtmsg rtmsg;
	char devname[] = "lo";
	int err;

	/* ::1 */

	memset(&addr, 0, sizeof(struct in6_addr));
	addr.s6_addr[15] = 1;

	idev = ipv6_add_dev(dev);

	if (idev == NULL)
	{
		printk(KERN_DEBUG "init loopback: add_dev failed\n");
		return;
	}

	ifp = ipv6_add_addr(idev, &addr, IFA_HOST);

	if (ifp == NULL)
	{
		printk(KERN_DEBUG "init_loopback: add_addr failed\n");
		return;
	}

	ifp->flags |= ADDR_PERMANENT;

	memcpy(&rtmsg.rtmsg_dst, &addr, sizeof(struct in6_addr));
	memset(&rtmsg.rtmsg_gateway, 0, sizeof(struct in6_addr));

	rtmsg.rtmsg_prefixlen = 128;
	rtmsg.rtmsg_metric = 1;
	strcpy(rtmsg.rtmsg_device, devname);

	rtmsg.rtmsg_flags = RTF_NEXTHOP|RTF_HOST|RTF_UP;

	err = ipv6_route_add(&rtmsg);

	if (err)
	{
		printk(KERN_DEBUG "init_loopback: error in route_add\n");
	}

	/* add route for ::127.0.0.1 */
}

static void addrconf_eth_config(struct device *dev)
{
	struct in6_addr addr;
	struct in6_addr maddr;
	struct inet6_ifaddr * ifp;
	struct inet6_dev    * idev;

	memset(&addr, 0, sizeof(struct in6_addr));

	/* generate link local address*/
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

	ifp->flags |= (DAD_INCOMPLETE | ADDR_PERMANENT);
	ifp->prefix_len = 10;

	/* join to all nodes multicast group */
	ipv6_addr_all_nodes(&maddr);
	ipv6_dev_mc_inc(dev, &maddr);
	
	if (ipv6_forwarding)
	{
		idev->router = 1;
		ipv6_addr_all_routers(&maddr);
		ipv6_dev_mc_inc(dev, &maddr);		
	}

	/* join to solicited addr multicast group */
	addrconf_addr_solict_mult(&addr, &maddr);
	ipv6_dev_mc_inc(dev, &maddr);
			
	/* start dad */
	addrconf_dad_start(ifp);
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
	
	if (len < sizeof(struct prefix_info))
	{
		printk(KERN_DEBUG "addrconf: prefix option too short\n");
		return;
	}
	
	/*
	 *	Validation checks ([ADDRCONF], page 19)
	 */

	addr_type = ipv6_addr_type(&pinfo->prefix);

	if (addr_type & IPV6_ADDR_LINKLOCAL)
	{
		return;
	}

	valid_lft = ntohl(pinfo->valid);
	prefered_lft = ntohl(pinfo->prefered);

	if (prefered_lft > valid_lft)
	{
		printk(KERN_WARNING
		       "addrconf: prefix option has invalid lifetime\n");
		return;
	}

	/*
	 *	If we where using an "all destinations on link" route
	 *	delete it
	 */

	if (last_resort_rt && (last_resort_rt->rt_flags & RTI_ALLONLINK))
	{
		rt_release(last_resort_rt);
		last_resort_rt = NULL;
	}

	/*
	 *	Two things going on here:
	 *	1) Add routes for on-link prefixes
	 *	2) Configure prefixes with the auto flag set
	 */

	rt_expires = jiffies + valid_lft * HZ;
	if (rt_expires < jiffies)
	{
		rt_expires = ~0;
	}

	rt = fibv6_lookup(&pinfo->prefix, dev, RTI_DYNAMIC|RTI_GATEWAY);
		
	if (rt)
	{
		if (pinfo->onlink == 0 || valid_lft == 0)
		{
			/*
			 *	delete route
			 */
			fib6_del_rt(rt);
			rt = NULL;
		}
		else
		{
			rt->rt_expires = rt_expires;
		}
	}
	else if (pinfo->onlink && valid_lft)
	{
		struct in6_rtmsg rtmsg;

		printk(KERN_DEBUG "adding on link route\n");
		ipv6_addr_copy(&rtmsg.rtmsg_dst, &pinfo->prefix);
		memset(&rtmsg.rtmsg_gateway, 0, sizeof(struct in6_addr));

		rtmsg.rtmsg_prefixlen = pinfo->prefix_len;
		rtmsg.rtmsg_metric = 1;
		memcpy(rtmsg.rtmsg_device, dev->name, strlen(dev->name) + 1);
		rtmsg.rtmsg_flags = RTF_UP | RTF_ADDRCONF;
		rtmsg.rtmsg_info = rt_expires;

		ipv6_route_add(&rtmsg);
	}

	if (pinfo->autoconf && addrconf_sys_autoconf)
	{
		struct inet6_ifaddr * ifp;
		struct in6_addr addr;
		int plen;

		plen = pinfo->prefix_len >> 3;

		if (plen + dev->addr_len == sizeof(struct in6_addr))
		{
			memcpy(&addr, &pinfo->prefix, plen);
			memcpy(addr.s6_addr + plen, dev->dev_addr,
			       dev->addr_len);
		}
		else
		{
			printk(KERN_DEBUG
			       "addrconf: prefix_len invalid\n");
			return;
		}

		ifp = ipv6_chk_addr(&addr);

		if (ifp == NULL && valid_lft)
		{
			/* create */

			struct inet6_dev *in6_dev;

			in6_dev = ipv6_get_idev(dev);

			if (in6_dev == NULL)
			{
				printk(KERN_DEBUG
				       "addrconf: device not configured\n");
			}
			
			ifp = ipv6_add_addr(in6_dev, &addr,
					    addr_type & IPV6_ADDR_SCOPE_MASK);

			if (dev->flags & IFF_MULTICAST)
			{
				struct in6_addr maddr;

				/* join to solicited addr multicast group */
				addrconf_addr_solict_mult(&addr, &maddr);
				ipv6_dev_mc_inc(dev, &maddr);
			}

			ifp->flags |= DAD_INCOMPLETE;
			ifp->prefix_len = pinfo->prefix_len;

			addrconf_dad_start(ifp);
			
		}

		if (ifp && valid_lft == 0)
		{
			ipv6_del_addr(ifp);
			ifp = NULL;
		}

		if (ifp)
		{
			ifp->valid_lft = valid_lft;
			ifp->prefered_lft = prefered_lft;
			ifp->tstamp = jiffies;
		}
	}

}

static int addrconf_ifdown(struct device *dev)
{
	struct inet6_dev *idev, **bidev;
	struct inet6_ifaddr *ifa, **bifa;
	int i;

	start_bh_atomic();

	bidev = &inet6_dev_lst;

	for (idev = inet6_dev_lst; idev; idev = idev->next)
	{
		if (idev->dev == dev)
		{
			*bidev = idev->next;
			break;
		}
		bidev = &idev;
	}

	if (idev == NULL)
	{
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

	for (i=0; i<16; i++)
	{
		bifa = &inet6_addr_lst[i];
		
		for (ifa=inet6_addr_lst[i]; ifa; )
		{
			if (ifa->idev == idev)
			{
				*bifa = ifa->lst_next;
				kfree(ifa);
				ifa = *bifa;
				continue;
			}
			bifa = &ifa;
			ifa = ifa->lst_next;
		}
	}

	kfree(idev);
	end_bh_atomic();
	return 0;
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
	int err;

	err = copy_from_user(&ireq, arg, sizeof(struct in6_ifreq));
	
	if (err)
		return -EFAULT;

	dev = dev_get(ireq.devname);

	if (dev->type == ARPHRD_SIT)
	{
		struct device *dev;
		
		if (!(ipv6_addr_type(&ireq.addr) & IPV6_ADDR_COMPATv4))
		{
			return -EADDRNOTAVAIL;
		}
		
		dev = sit_add_tunnel(ireq.addr.s6_addr32[3]);
		
		if (dev == NULL)
			return -ENOMEM;

		return 0;
	}
	
	return -EINVAL;
}

/*
 *	Manual configuration of address on an interface
 */
int addrconf_add_ifaddr(void *arg)
{
	struct inet6_dev *in6_dev;
	struct in6_ifreq ireq;
	struct inet6_ifaddr *ifp;
	struct device *dev;
	int addr_type;
	int err;
	
	if (!suser())
		return -EPERM;
	
	err = copy_from_user(&ireq, arg, sizeof(struct in6_ifreq));
	if (err)
		return -EFAULT;

	dev = dev_get(ireq.devname);

	if (dev == NULL)
		return -EINVAL;

	in6_dev = ipv6_get_idev(dev);

	if (in6_dev == NULL)
		return -EINVAL;

	addr_type  = ipv6_addr_type(&ireq.addr);
	addr_type &= IPV6_ADDR_SCOPE_MASK;
	
	ifp = ipv6_add_addr(in6_dev, &ireq.addr, addr_type);

	if (ifp == NULL)
		return -ENOMEM;

	ifp->prefix_len = 128;

	if (dev->flags & IFF_MULTICAST)
	{
		struct in6_addr maddr;

		/* join to solicited addr multicast group */
		addrconf_addr_solict_mult(&ireq.addr, &maddr);
		ipv6_dev_mc_inc(dev, &maddr);
	}


	ifp->prefix_len = ireq.prefix_len;
	ifp->flags |= ADDR_PERMANENT;

	if (!(dev->flags & (IFF_NOARP|IFF_LOOPBACK)))
	{
		ifp->flags |= DAD_INCOMPLETE;
		addrconf_dad_start(ifp);
	}
	return 0;
}

static void sit_add_v4_addrs(struct inet6_dev *idev)
{
	struct inet6_ifaddr * ifp;
	struct in6_addr addr;
	struct device *dev;
	int scope;

	memset(&addr, 0, sizeof(struct in6_addr));

	if (idev->dev->pa_dstaddr)
	{
		addr.s6_addr32[0] = __constant_htonl(0xfe800000);
		scope = IFA_LINK;
	}
	else
	{
		scope = IPV6_ADDR_COMPATv4;
	}

        for (dev = dev_base; dev != NULL; dev = dev->next) 
        {
		if (dev->family == AF_INET && (dev->flags & IFF_UP))
		{
			int flag = scope;
			
			addr.s6_addr32[3] = dev->pa_addr;

			if (dev->flags & IFF_LOOPBACK)
			{
				if (idev->dev->pa_dstaddr)
					continue;
				
				flag |= IFA_HOST;
			}

			ifp = ipv6_add_addr(idev, &addr, flag);
			
			if (ifp == NULL)
				continue;

			ifp->flags |= ADDR_PERMANENT;
		}
        }
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
		}
		rt6_sndmsg(RTMSG_NEWDEVICE, NULL, NULL, 0, 0, dev->name, 0);
		break;

	case NETDEV_DOWN:
		/*
		 *	Remove all addresses from this interface
		 *	and take the interface out of the list.
		 */
		if (addrconf_ifdown(dev) == 0)
		{
			rt6_ifdown(dev);
			rt6_sndmsg(RTMSG_NEWDEVICE, NULL, NULL, 0, 0,
				   dev->name, 0);
		}

		break;
	}
	
	return NOTIFY_OK;
}

static void addrconf_dad_completed(struct inet6_ifaddr *ifp)
{
	struct in6_rtmsg rtmsg;
	struct device *dev;
	int err;


	if (ipv6_addr_type(&ifp->addr) & IPV6_ADDR_LINKLOCAL)
	{
		struct in6_addr all_routers;

		/*
		 *	1) configure a link route for this interface
		 *	2) send a (delayed) router solicitation
		 */

		memcpy(&rtmsg.rtmsg_dst, &ifp->addr, sizeof(struct in6_addr));
		memset(&rtmsg.rtmsg_gateway, 0, sizeof(struct in6_addr));

		dev = ifp->idev->dev;

		rtmsg.rtmsg_prefixlen = ifp->prefix_len;
		rtmsg.rtmsg_metric = 1;
		memcpy(rtmsg.rtmsg_device, dev->name, strlen(dev->name) + 1);

		rtmsg.rtmsg_flags = RTF_UP;

		err = ipv6_route_add(&rtmsg);
		
		if (err)
		{
			printk(KERN_DEBUG "dad_complete: error in route_add\n");
		}

		if (ipv6_forwarding == 0)
		{
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
					      RTR_SOLICITATION_INTERVAL);
			ifp->idev->if_flags |= IF_RS_SENT;
			add_timer(&ifp->timer);
		}
	}

}

static void addrconf_dad_timer(unsigned long data)
{
	struct inet6_ifaddr *ifp;
	struct in6_addr unspec;
	struct in6_addr mcaddr;

	ifp = (struct inet6_ifaddr *) data;

	if (ifp->probes-- == 0)
	{
		/*
		 * DAD was successful
		 */

		ifp->flags &= ~DAD_INCOMPLETE;
		addrconf_dad_completed(ifp);
		return;
	}

	/* send a neighbour solicitation for our addr */
	memset(&unspec, 0, sizeof(unspec));
	addrconf_addr_solict_mult(&ifp->addr, &mcaddr);

	ndisc_send_ns(ifp->idev->dev, NULL, &ifp->addr, &mcaddr, &unspec);

	ifp->timer.expires = jiffies + RETRANS_TIMER;
	add_timer(&ifp->timer);
}

static void addrconf_rs_timer(unsigned long data)
{
	struct inet6_ifaddr *ifp;

	ifp = (struct inet6_ifaddr *) data;

	if (ipv6_forwarding)
		return;

	if (ifp->idev->if_flags & IF_RA_RCVD)
	{
		/*
		 *	Announcement received after solicitation
		 *	was sent
		 */
		return;
	}

	if (ifp->probes++ <= MAX_RTR_SOLICITATIONS)
	{
		struct in6_addr all_routers;

		ipv6_addr_set(&all_routers,
			      __constant_htonl(0xff020000U), 0, 0,
			      __constant_htonl(0x2U));

		ndisc_send_rs(ifp->idev->dev, &ifp->addr,
			      &all_routers);
	
		
		ifp->timer.function = addrconf_rs_timer;
		ifp->timer.expires = jiffies + RTR_SOLICITATION_INTERVAL;
		add_timer(&ifp->timer);
	}
	else
	{
		printk(KERN_DEBUG "%s: no IPv6 routers present\n",
		       ifp->idev->dev->name);

		if (!default_rt_list && !last_resort_rt)
		{
			struct rt6_info *rt;

			/*
			 *	create a last resort route with all
			 *	destinations on link
			 */
			rt = kmalloc(sizeof(struct rt6_info), GFP_ATOMIC);

			if (rt)
			{
				memset(rt, 0, sizeof(struct rt6_info));
				rt->rt_dev = ifp->idev->dev;
				rt->rt_ref = 1;
				rt->rt_flags = (RTI_ALLONLINK | RTF_UP);
				last_resort_rt = rt;
			}
		}
	}
}

static void addrconf_dad_start(struct inet6_ifaddr *ifp)
{
	static int rand_seed = 1;
	int rand_num;

	if (rand_seed)
	{
		rand_seed = 0;
		nd_rand_seed = ifp->addr.s6_addr32[3];
	}

	init_timer(&ifp->timer);
	ifp->probes = DupAddrDetectTransmits;

	rand_num = ipv6_random() % MAX_RTR_SOLICITATION_DELAY;

	ifp->timer.function = addrconf_dad_timer;
	ifp->timer.data = (unsigned long) ifp;
	ifp->timer.expires = jiffies + rand_num;

	add_timer(&ifp->timer);
}

static int iface_proc_info(char *buffer, char **start, off_t offset,
			   int length, int dummy)
{
	struct inet6_ifaddr *ifp;
	int i;
	int len = 0;

	for (i=0; i < HASH_SIZE; i++)
		for (ifp=inet6_addr_lst[i]; ifp; ifp=ifp->lst_next)
		{
			int j;

			for (j=0; j<16; j++)
			{
				sprintf(buffer + len, "%02x",
					ifp->addr.s6_addr[j]);
				len += 2;
			}

			len += sprintf(buffer + len,
				       " %02x %02x %02x %02x %8s\n",
				       ifp->idev->if_index,
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


/*
 *	Periodic address status verification
 */

void addrconf_verify(unsigned long foo)
{
	struct inet6_ifaddr *ifp;
	unsigned long now = jiffies;
	int i;

	for (i=0; i < HASH_SIZE; i++)
	{
		for (ifp=inet6_addr_lst[i]; ifp;)
		{
			if (!(ifp->flags & ADDR_PERMANENT))
			{
				struct inet6_ifaddr *bp;
				unsigned long age;

				age = (now - ifp->tstamp) / HZ;

				if (age > ifp->prefered_lft)
				{
					ifp->flags |= ADDR_DEPRECATED;
				}

				bp = ifp;
				ifp=ifp->lst_next;
				
				if (age > bp->valid_lft)
				{
					ipv6_del_addr(bp);
				}
				continue;
			}
			ifp=ifp->lst_next;
		}
	}

	addr_chk_timer.expires = jiffies + ADDR_CHECK_FREQUENCY;
	add_timer(&addr_chk_timer);	
}

void addrconf_init()
{
	struct device *dev;

	/* init addr hash list */	  
	memset(inet6_addr_lst, 0, 16 * sizeof(struct inet6_ifaddr *));

	memset(inet6_mcast_lst,   0, 16 * sizeof(struct ipv6_mc_list *));

	inet6_dev_lst = NULL;

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
	
	proc_register_dynamic(&proc_net, &iface_proc_entry);
	
	addr_chk_timer.expires = jiffies + ADDR_CHECK_FREQUENCY;
	add_timer(&addr_chk_timer);
}

void addrconf_cleanup(void)
{
	struct inet6_dev *idev, *bidev;
	struct inet6_ifaddr *ifa, *bifa;
	int i;

	del_timer(&addr_chk_timer);

	/*
	 *	clean dev list.
	 */

	for (idev = inet6_dev_lst; idev; )
	{
		bidev = idev;
		idev = idev->next;
		kfree(bidev);
	}

	/*
	 *	clean addr_list
	 */

	for (i=0; i<16; i++)
	{
		for (ifa=inet6_addr_lst[i]; ifa; )
		{
			bifa = ifa;
			ifa = ifa->lst_next;
			kfree(bifa);
		}
	}

	proc_unregister(&proc_net, iface_proc_entry.low_ino);
}

/*
 * Local variables:
 * c-file-style: "Linux"
 * End:
 */
