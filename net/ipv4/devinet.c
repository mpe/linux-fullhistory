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
 
#include <asm/segment.h>
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
#include <net/ip.h>
#include <net/route.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>

/* 
 *	Determine a default network mask, based on the IP address. 
 */
 
unsigned long ip_get_mask(unsigned long addr)
{
  	unsigned long dst;

  	if (addr == 0L) 
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
 *	Check the address for our address, broadcasts, etc. 
 *
 *	I intend to fix this to at the very least cache the last
 *	resolved entry.
 */
 
int ip_chk_addr(unsigned long addr)
{
	struct device *dev;
#ifndef CONFIG_IP_CLASSLESS
	unsigned long mask;
#endif

	/* 
	 *	Accept both `all ones' and `all zeros' as BROADCAST. 
	 *	(Support old BSD in other words). This old BSD 
	 *	support will go very soon as it messes other things
	 *	up.
	 *	Also accept `loopback broadcast' as BROADCAST.
	 */

	if (addr == INADDR_ANY || addr == INADDR_BROADCAST ||
	    addr == htonl(0x7FFFFFFFL))
		return IS_BROADCAST;

#ifndef  CONFIG_IP_CLASSLESS
	mask = ip_get_mask(addr);

	/*
	 *	Accept all of the `loopback' class A net. 
	 */
	 
	if ((addr & mask) == htonl(0x7F000000L))
		return IS_MYADDR;
#else
	if ((addr & htonl(0x7F000000L)) == htonl(0x7F000000L))
		return IS_MYADDR;
#endif

	/*
	 *	OK, now check the interface addresses. We could
	 *	speed this by keeping a dev and a dev_up chain.
	 */
	 
	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		if ((!(dev->flags & IFF_UP)) || dev->family!=AF_INET)
			continue;
		/*
		 *	If the protocol address of the device is 0 this is special
		 *	and means we are address hunting (eg bootp).
		 */
		 
		if (dev->pa_addr == 0)
			return IS_MYADDR;
		/*
		 *	Is it the exact IP address? 
		 */
		 
		if (addr == dev->pa_addr)
			return IS_MYADDR;
		/*
		 *	Is it our broadcast address? 
		 */
		 
		if ((dev->flags & IFF_BROADCAST) && addr == dev->pa_brdaddr)
			return IS_BROADCAST;
		/*
		 *	Nope. Check for a subnetwork broadcast. 
		 */
		 
		if (((addr ^ dev->pa_addr) & dev->pa_mask) == 0) 
		{
			if ((addr & ~dev->pa_mask) == 0)
				return IS_BROADCAST;
			if ((addr & ~dev->pa_mask) == ~dev->pa_mask)
				return IS_BROADCAST;
		}
		
#ifndef CONFIG_IP_CLASSLESS
		/*
	 	 *	Nope. Check for Network broadcast. 
	 	 */
	 	 
		if (((addr ^ dev->pa_addr) & mask) == 0) 
		{
			if ((addr & ~mask) == 0)
				return IS_BROADCAST;
			if ((addr & ~mask) == ~mask)
				return IS_BROADCAST;
		}
#endif
	}
	if(IN_MULTICAST(ntohl(addr)))
		return IS_MULTICAST;
	return 0;		/* no match at all */
}


/*
 *	Retrieve our own address.
 *
 *	Because the loopback address (127.0.0.1) is already recognized
 *	automatically, we can use the loopback interface's address as
 *	our "primary" interface.  This is the address used by IP et
 *	al when it doesn't know which address to use (i.e. it does not
 *	yet know from or to which interface to go...).
 */
 
unsigned long ip_my_addr(void)
{
  	struct device *dev;

  	for (dev = dev_base; dev != NULL; dev = dev->next) 
  	{
		if (dev->flags & IFF_LOOPBACK) 
			return(dev->pa_addr);
  	}
  	return(0);
}

/*
 *	Find an interface that can handle addresses for a certain address. 
 */

struct device * ip_dev_bynet(unsigned long addr, unsigned long mask)
{
	struct device *dev;
	struct device *best_dev = NULL;
	__u32  best_mask = mask;

	for (dev = dev_base; dev; dev = dev->next) 
	{
		if (!(dev->flags & IFF_UP))
			continue;
		if (dev->flags & IFF_POINTOPOINT)
		{
			if (addr == dev->pa_dstaddr)
				return dev;
			continue;
		}
		if (dev->pa_mask & (addr ^ dev->pa_addr))
			continue;
		if (mask == dev->pa_mask)
			return dev;
		if (best_dev && (best_mask & dev->pa_mask) != best_mask)
			continue;
		best_dev = dev;
		best_mask = dev->pa_mask;
	}
	return best_dev;
}

/*
 *	Find the first device with a given source address.
 */
 
struct device *ip_dev_find(unsigned long addr)
{
	struct device *dev;
	for(dev = dev_base; dev; dev=dev->next)
	{
		if((dev->flags&IFF_UP) && dev->pa_addr==addr)
			return dev;
	}
	return NULL;
}

struct device *dev_getbytype(unsigned short type)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		if (dev->type == type && !(dev->flags&(IFF_LOOPBACK|IFF_NOARP)))
			return(dev);
	}
	return(NULL);
}

