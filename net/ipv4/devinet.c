/*
 *	NET3	IP device support routines.
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
 */

#include <linux/config.h>	/* For CONFIG_IP_CLASSLESS */
 
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
#include <linux/if_arp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>
#include <linux/notifier.h>
#include <linux/net_alias.h>
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif

extern struct notifier_block *netdev_chain;

/* 
 *	Determine a default network mask, based on the IP address. 
 */

static unsigned long ip_get_mask(unsigned long addr)
{
  	unsigned long dst;

  	if (ZERONET(addr))
  		return(0L);	/* special case */

  	dst = ntohl(addr);
  	if (IN_CLASSA(dst)) 
  		return(htonl(IN_CLASSA_NET));
  	if (IN_CLASSB(dst)) 
  		return(htonl(IN_CLASSB_NET));
  	if (IN_CLASSC(dst)) 
  		return(htonl(IN_CLASSC_NET));
  
  	/*
  	 *	Something else, probably a multicast. 
  	 */
  	 
  	return(0);
}


/*
 *	This checks bitmasks for the ioctl calls for devices.
 */
 
static inline int bad_mask(__u32 mask, __u32 addr)
{
	if (addr & (mask = ~mask))
		return 1;
	mask = ntohl(mask);
	if (mask & (mask+1))
		return 1;
	return 0;
}

 
int devinet_ioctl(unsigned int cmd, void *arg)
{
	struct ifreq ifr;
	struct device *dev;
	__u32 addr;
#ifdef CONFIG_NET_ALIAS
	int err;
#endif

	/*
	 *	Fetch the caller's info block into kernel space
	 */

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	/*
	 *	See which interface the caller is talking about. 
	 */
	 
	/*
	 *
	 *	net_alias_dev_get(): dev_get() with added alias naming magic.
	 *	only allow alias creation/deletion if (getset==SIOCSIFADDR)
	 *
	 */
	 
#ifdef CONFIG_KERNELD
	dev_load(ifr.ifr_name);
#endif	

#ifdef CONFIG_NET_ALIAS
	if ((dev = net_alias_dev_get(ifr.ifr_name, cmd == SIOCSIFADDR, &err, NULL, NULL)) == NULL)
		return(err);
#else
	if ((dev = dev_get(ifr.ifr_name)) == NULL) 	
		return(-ENODEV);
#endif

	if (cmd != SIOCSIFADDR && dev->family != AF_INET)
		return(-EINVAL);

	switch(cmd) 
	{
		case SIOCGIFADDR:	/* Get interface address (and family) */
			if (ifr.ifr_addr.sa_family == AF_UNSPEC)
			{
				memcpy(ifr.ifr_hwaddr.sa_data, dev->dev_addr, MAX_ADDR_LEN);
				ifr.ifr_hwaddr.sa_family = dev->type;			
			}
			else
			{
				(*(struct sockaddr_in *)
					  &ifr.ifr_addr).sin_addr.s_addr = dev->pa_addr;
				(*(struct sockaddr_in *)
					  &ifr.ifr_addr).sin_family = dev->family;
				(*(struct sockaddr_in *)
					  &ifr.ifr_addr).sin_port = 0;
			}
			break;
	
		case SIOCSIFADDR:	/* Set interface address (and family) */
		
			if (!suser())
				return -EPERM;

			/*
			 *	BSDism. SIOCSIFADDR family=AF_UNSPEC sets the
			 *	physical address. We can cope with this now.
			 */
			
			if(ifr.ifr_addr.sa_family==AF_UNSPEC)
			{
				int ret;
				if(dev->set_mac_address==NULL)
					return -EOPNOTSUPP;
				ret = dev->set_mac_address(dev,&ifr.ifr_addr);
				if (!ret)
					notifier_call_chain(&netdev_chain, NETDEV_CHANGEADDR, dev);
				return ret;
			}
			if(ifr.ifr_addr.sa_family!=AF_INET)
				return -EINVAL;

			addr = (*(struct sockaddr_in *)&ifr.ifr_addr).sin_addr.s_addr;

			dev_lock_wait();
			dev_lock_list();

			if (dev->family == AF_INET && addr == dev->pa_addr) {
				dev_unlock_list();
				return 0;
			}

			if (dev->flags & IFF_UP)
				notifier_call_chain(&netdev_chain, NETDEV_DOWN, dev);

			/*
			 *	if dev is an alias, must rehash to update
			 *	address change
			 */

#ifdef CONFIG_NET_ALIAS
			if (net_alias_is(dev))
				net_alias_dev_rehash(dev, &ifr.ifr_addr);
#endif
			dev->pa_addr = addr;
			dev->ip_flags |= IFF_IP_ADDR_OK;
			dev->ip_flags &= ~(IFF_IP_BRD_OK|IFF_IP_MASK_OK);
			dev->family = AF_INET;
			if (dev->flags & IFF_POINTOPOINT) {
				dev->pa_mask = 0xFFFFFFFF;
				dev->pa_brdaddr = 0xFFFFFFFF;
			} else {
				dev->pa_mask = ip_get_mask(dev->pa_addr);
				dev->pa_brdaddr = dev->pa_addr|~dev->pa_mask;
			}
			if (dev->flags & IFF_UP)
				notifier_call_chain(&netdev_chain, NETDEV_UP, dev);
			dev_unlock_list();
			return 0;
			
		case SIOCGIFBRDADDR:	/* Get the broadcast address */
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_addr.s_addr = dev->pa_brdaddr;
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_family = dev->family;
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_port = 0;
			break;

		case SIOCSIFBRDADDR:	/* Set the broadcast address */
			if (!suser())
				return -EPERM;

			addr = (*(struct sockaddr_in *)&ifr.ifr_broadaddr).sin_addr.s_addr;

			if (dev->flags & IFF_UP)
				ip_rt_change_broadcast(dev, addr);
			dev->pa_brdaddr = addr;
			dev->ip_flags |= IFF_IP_BRD_OK;
			return 0;
			
		case SIOCGIFDSTADDR:	/* Get the destination address (for point-to-point links) */
			(*(struct sockaddr_in *)
				&ifr.ifr_dstaddr).sin_addr.s_addr = dev->pa_dstaddr;
			(*(struct sockaddr_in *)
				&ifr.ifr_dstaddr).sin_family = dev->family;
			(*(struct sockaddr_in *)
				&ifr.ifr_dstaddr).sin_port = 0;
			break;
	
		case SIOCSIFDSTADDR:	/* Set the destination address (for point-to-point links) */
			if (!suser())
				return -EPERM;
			addr = (*(struct sockaddr_in *)&ifr.ifr_dstaddr).sin_addr.s_addr;
			if (addr == dev->pa_dstaddr)
				return 0;
			if (dev->flags & IFF_UP)
				ip_rt_change_dstaddr(dev, addr);
			dev->pa_dstaddr = addr;
			return 0;
			
		case SIOCGIFNETMASK:	/* Get the netmask for the interface */
			(*(struct sockaddr_in *)
				&ifr.ifr_netmask).sin_addr.s_addr = dev->pa_mask;
			(*(struct sockaddr_in *)
				&ifr.ifr_netmask).sin_family = dev->family;
			(*(struct sockaddr_in *)
				&ifr.ifr_netmask).sin_port = 0;
			break;

		case SIOCSIFNETMASK: 	/* Set the netmask for the interface */
			if (!suser())
				return -EPERM;
			addr = (*(struct sockaddr_in *)&ifr.ifr_netmask).sin_addr.s_addr;

			if (addr == dev->pa_mask) {
				dev->ip_flags |= IFF_IP_MASK_OK;
				return 0;
			}

			/*
			 *	The mask we set must be legal.
			 */
			if (bad_mask(addr, 0))
				return -EINVAL;
			if (addr == htonl(0xFFFFFFFE))
				return -EINVAL;
			if (dev->flags & IFF_UP)
				ip_rt_change_netmask(dev, addr);
			dev->pa_mask = addr;
			dev->ip_flags |= IFF_IP_MASK_OK;
			dev->ip_flags &= ~IFF_IP_BRD_OK;
			return 0;
		default:
			return -EINVAL;
			
	}
	if (copy_to_user(arg, &ifr, sizeof(struct ifreq)))
		return -EFAULT;
	return 0;
}
