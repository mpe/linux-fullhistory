/*
 *	Multicast support for IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Based on linux/ipv4/igmp.c and linux/ipv4/ip_sockglue.c 
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

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/if_inet6.h>
#include <net/ndisc.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>


/*
 *	socket join on multicast group
 */
int ipv6_sock_mc_join(struct sock *sk, struct device *dev, 
		      struct in6_addr *addr)
{
	struct ipv6_mc_socklist *mc_lst;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	int err;

	if (!(ipv6_addr_type(addr) & IPV6_ADDR_MULTICAST))
		return -EINVAL;

	if(!(dev->flags & IFF_MULTICAST))
		return -EADDRNOTAVAIL;

	mc_lst = (struct ipv6_mc_socklist *) 
		kmalloc(sizeof(struct ipv6_mc_socklist), GFP_KERNEL);

	if (mc_lst == NULL)
		return -ENOMEM;

	mc_lst->next = NULL;
	memcpy(&mc_lst->addr, addr, sizeof(struct in6_addr));
	mc_lst->dev  = dev;

	/*
	 *	now add/increase the group membership on the device
	 */

	err = ipv6_dev_mc_inc(dev, addr);

	if (err)
	{
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
	return 0;
}

void ipv6_sock_mc_close(struct sock *sk)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6_mc_socklist *mc_lst;

	for (mc_lst = np->ipv6_mc_list; mc_lst; )
	{
		struct ipv6_mc_socklist *back;

		/*
		 *	leave group
		 */

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
	struct ipv6_mc_list *mc;
	struct inet6_dev    *i6dev;
	char buf[6];
	u8 hash;
	
	for (i6dev = inet6_dev_lst; i6dev; i6dev=i6dev->next)
		if (i6dev->dev == dev)
			break;
		
	if (i6dev == NULL)
	{
		printk(KERN_DEBUG "ipv6_dev_mc_inc: device not found\n");
		return -EINVAL;
	}

	for (mc = i6dev->mc_list; mc; mc = mc->if_next)
		if (ipv6_addr_cmp(&mc->addr, addr) == 0)
		{
			atomic_inc(&mc->users);
			return 0;
		}

	/*
	 *	not found: create a new one.
	 */

	mc = (struct ipv6_mc_list *) kmalloc(sizeof(struct ipv6_mc_list),
					     GFP_ATOMIC);

	if (mc == NULL)
	{
		return -ENOMEM;
	}

	memset(mc, 0, sizeof(struct ipv6_mc_list));

	memcpy(&mc->addr, addr, sizeof(struct in6_addr));
	mc->dev = dev;
	mc->users = 1;

	hash = ipv6_addr_hash(addr);

	mc->next = inet6_mcast_lst[hash];
	inet6_mcast_lst[hash] = mc;
	
	mc->if_next = i6dev->mc_list;
	i6dev->mc_list = mc;

	/*
	 *	multicast mapping is defined in IPv6-over-foo documents
	 */

	switch (dev->type) {
	case ARPHRD_ETHER:
		ipv6_mc_map(addr, buf);
		dev_mc_add(dev, buf, ETH_ALEN, 0);
		break;
		
	default:
		printk(KERN_DEBUG "dev_mc_inc: unkown device type\n");
	}
	

	/*
	 *	FIXME: ICMP report handling
	 */

	return 0;
}

/*
 *	device multicast group del
 */
int ipv6_dev_mc_dec(struct device *dev, struct in6_addr *addr)
{
	return 0;
}

/*
 *	check if the interface/address pair is valid
 */
int ipv6_chk_mcast_addr(struct device *dev, struct in6_addr *addr)
{
	struct ipv6_mc_list *mc;	
	u8 hash;

	hash = ipv6_addr_hash(addr);

	for (mc = inet6_mcast_lst[hash]; mc; mc=mc->next)
		if ((mc->dev == dev) &&
		    ipv6_addr_cmp(&mc->addr, addr) == 0)
		{
			return 1;
		}

	return 0;
}

/*
 *	IGMP handling (alias multicast ICMPv6 messages)
 */

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce -pipe -m486 -DCPU=486 -DMODULE -DMODVERSIONS -include /usr/src/linux/include/linux/modversions.h  -c -o mcast.o mcast.c"
 * End:
 */
