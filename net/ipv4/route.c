/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		ROUTE - implementation of the IP router.
 *
 * Version:	@(#)route.c	1.0.14	05/31/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Linus Torvalds, <Linus.Torvalds@helsinki.fi>
 *
 * Fixes:
 *		Alan Cox	:	Verify area fixes.
 *		Alan Cox	:	cli() protects routing changes
 *		Rui Oliveira	:	ICMP routing table updates
 *		(rco@di.uminho.pt)	Routing table insertion and update
 *		Linus Torvalds	:	Rewrote bits to be sensible
 *		Alan Cox	:	Added BSD route gw semantics
 *		Alan Cox	:	Super /proc >4K 
 *		Alan Cox	:	MTU in route table
 *		Alan Cox	: 	MSS actually. Also added the window
 *					clamper.
 *		Sam Lantinga	:	Fixed route matching in rt_del()
 *		Alan Cox	:	Routing cache support.
 *		Alan Cox	:	Removed compatibility cruft.
 *		Alan Cox	:	RTF_REJECT support.
 *		Alan Cox	:	TCP irtt support.
 *		Jonathan Naylor	:	Added Metric support.
 *	Miquel van Smoorenburg	:	BSD API fixes.
 *	Miquel van Smoorenburg	:	Metrics.
 *		Alan Cox	:	Use __u32 properly
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>

/*
 *	The routing table list
 */

static struct rtable *rt_base = NULL;
unsigned long rt_stamp = 1;		/* Routing table version stamp for caches ( 0 is 'unset' ) */

/*
 *	Pointer to the loopback route
 */
 
static struct rtable *rt_loopback = NULL;

/*
 *	Remove a routing table entry.
 *	Should we return a status value here ?
 */

static void rt_del(__u32 dst, __u32 mask,
		char *devname, __u32 gtw, short rt_flags, short metric)
{
	struct rtable *r, **rp;
	unsigned long flags;

	rp = &rt_base;
	
	/*
	 *	This must be done with interrupts off because we could take
	 *	an ICMP_REDIRECT.
	 */
	 
	save_flags(flags);
	cli();
	while((r = *rp) != NULL) 
	{
		/*
		 *	Make sure the destination and netmask match.
		 *	metric, gateway and device are also checked
		 *	if they were specified.
		 */
		if (r->rt_dst != dst ||
		    (mask && r->rt_mask != mask) ||
		    (gtw && r->rt_gateway != gtw) ||
		    (metric >= 0 && r->rt_metric != metric) ||
		    (devname && strcmp((r->rt_dev)->name,devname) != 0) )
		{
			rp = &r->rt_next;
			continue;
		}
		*rp = r->rt_next;
		
		/*
		 *	If we delete the loopback route update its pointer.
		 */
		 
		if (rt_loopback == r)
			rt_loopback = NULL;
		kfree_s(r, sizeof(struct rtable));
	} 
	rt_stamp++;		/* New table revision */
	
	restore_flags(flags);
}


/*
 *	Remove all routing table entries for a device. This is called when
 *	a device is downed.
 */
 
void ip_rt_flush(struct device *dev)
{
	struct rtable *r;
	struct rtable **rp;
	unsigned long flags;

	rp = &rt_base;
	save_flags(flags);
	cli();
	while ((r = *rp) != NULL) {
		if (r->rt_dev != dev) {
			rp = &r->rt_next;
			continue;
		}
		*rp = r->rt_next;
		if (rt_loopback == r)
			rt_loopback = NULL;
		kfree_s(r, sizeof(struct rtable));
	} 
	rt_stamp++;		/* New table revision */
	restore_flags(flags);
}

/*
 *	Used by 'rt_add()' when we can't get the netmask any other way..
 *
 *	If the lower byte or two are zero, we guess the mask based on the
 *	number of zero 8-bit net numbers, otherwise we use the "default"
 *	masks judging by the destination address and our device netmask.
 */
 
static __u32 unsigned long default_mask(__u32 dst)
{
	dst = ntohl(dst);
	if (IN_CLASSA(dst))
		return htonl(IN_CLASSA_NET);
	if (IN_CLASSB(dst))
		return htonl(IN_CLASSB_NET);
	return htonl(IN_CLASSC_NET);
}


/*
 *	If no mask is specified then generate a default entry.
 */

static __u32 guess_mask(__u32 dst, struct device * dev)
{
	__u32 mask;

	if (!dst)
		return 0;
	mask = default_mask(dst);
	if ((dst ^ dev->pa_addr) & mask)
		return mask;
	return dev->pa_mask;
}


/*
 *	Find the route entry through which our gateway will be reached
 */
 
static inline struct device * get_gw_dev(__u32 gw)
{
	struct rtable * rt;

	for (rt = rt_base ; ; rt = rt->rt_next) 
	{
		if (!rt)
			return NULL;
		if ((gw ^ rt->rt_dst) & rt->rt_mask)
			continue;
		/* 
		 *	Gateways behind gateways are a no-no 
		 */
		 
		if (rt->rt_flags & RTF_GATEWAY)
			return NULL;
		return rt->rt_dev;
	}
}

/*
 *	Rewrote rt_add(), as the old one was weird - Linus
 *
 *	This routine is used to update the IP routing table, either
 *	from the kernel (ICMP_REDIRECT) or via an ioctl call issued
 *	by the superuser.
 */
 
void ip_rt_add(short flags, __u32 dst, __u32 mask,
	__u32 gw, struct device *dev, unsigned short mtu,
	unsigned long window, unsigned short irtt, short metric)
{
	struct rtable *r, *rt;
	struct rtable **rp;
	unsigned long cpuflags;
	int duplicate = 0;

	/*
	 *	A host is a unique machine and has no network bits.
	 */
	 
	if (flags & RTF_HOST) 
	{
		mask = 0xffffffff;
	} 
	
	/*
	 *	Calculate the network mask
	 */
	 
	else if (!mask) 
	{
		if (!((dst ^ dev->pa_addr) & dev->pa_mask)) 
		{
			mask = dev->pa_mask;
			flags &= ~RTF_GATEWAY;
			if (flags & RTF_DYNAMIC) 
			{
				/*printk("Dynamic route to my own net rejected\n");*/
				return;
			}
		} 
		else
			mask = guess_mask(dst, dev);
		dst &= mask;
	}
	
	/*
	 *	A gateway must be reachable and not a local address
	 */
	 
	if (gw == dev->pa_addr)
		flags &= ~RTF_GATEWAY;
		
	if (flags & RTF_GATEWAY) 
	{
		/*
		 *	Don't try to add a gateway we can't reach.. 
		 */
		 
		if (dev != get_gw_dev(gw))
			return;
			
		flags |= RTF_GATEWAY;
	} 
	else
		gw = 0;
		
	/*
	 *	Allocate an entry and fill it in.
	 */
	 
	rt = (struct rtable *) kmalloc(sizeof(struct rtable), GFP_ATOMIC);
	if (rt == NULL) 
	{
		return;
	}
	memset(rt, 0, sizeof(struct rtable));
	rt->rt_flags = flags | RTF_UP;
	rt->rt_dst = dst;
	rt->rt_dev = dev;
	rt->rt_gateway = gw;
	rt->rt_mask = mask;
	rt->rt_mss = dev->mtu - HEADER_SIZE;
	rt->rt_metric = metric;
	rt->rt_window = 0;	/* Default is no clamping */

	/* Are the MSS/Window valid ? */

	if(rt->rt_flags & RTF_MSS)
		rt->rt_mss = mtu;
		
	if(rt->rt_flags & RTF_WINDOW)
		rt->rt_window = window;
	if(rt->rt_flags & RTF_IRTT)
		rt->rt_irtt = irtt;

	/*
	 *	What we have to do is loop though this until we have
	 *	found the first address which has a higher generality than
	 *	the one in rt.  Then we can put rt in right before it.
	 *	The interrupts must be off for this process.
	 */

	save_flags(cpuflags);
	cli();

	/*
	 *	Remove old route if we are getting a duplicate. 
	 */
	 
	rp = &rt_base;
	while ((r = *rp) != NULL) 
	{
		if (r->rt_dst != dst || 
		    r->rt_mask != mask)
		{
			rp = &r->rt_next;
			continue;
		}
		if (r->rt_metric != metric && r->rt_gateway != gw)
		{
			duplicate = 1;
			rp = &r->rt_next;
			continue;
		}
		*rp = r->rt_next;
		if (rt_loopback == r)
			rt_loopback = NULL;
		kfree_s(r, sizeof(struct rtable));
	}
	
	/*
	 *	Add the new route 
	 */
	 
	rp = &rt_base;
	while ((r = *rp) != NULL) {
		/*
		 * When adding a duplicate route, add it before
		 * the route with a higher metric.
		 */
		if (duplicate &&
		    r->rt_dst == dst &&
		    r->rt_mask == mask &&
		    r->rt_metric > metric)
			break;
		else
		/*
		 * Otherwise, just add it before the
		 * route with a higher generality.
		 */
			if ((r->rt_mask & mask) != mask)
				break;
		rp = &r->rt_next;
	}
	rt->rt_next = r;
	*rp = rt;
	
	/*
	 *	Update the loopback route
	 */
	 
	if ((rt->rt_dev->flags & IFF_LOOPBACK) && !rt_loopback)
		rt_loopback = rt;

	rt_stamp++;		/* New table revision */
		
	/*
	 *	Restore the interrupts and return
	 */
	 
	restore_flags(cpuflags);
	return;
}


/*
 *	Check if a mask is acceptable.
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

/*
 *	Process a route add request from the user
 */
 
static int rt_new(struct rtentry *r)
{
	int err;
	char * devname;
	struct device * dev = NULL;
	unsigned long flags;
	__u32 daddr, mask, gw;
	short metric;

	/*
	 *	If a device is specified find it.
	 */
	 
	if ((devname = r->rt_dev) != NULL) 
	{
		err = getname(devname, &devname);
		if (err)
			return err;
		dev = dev_get(devname);
		putname(devname);
		if (!dev)
			return -EINVAL;
	}
	
	/*
	 *	If the device isn't INET, don't allow it
	 */

	if (r->rt_dst.sa_family != AF_INET)
		return -EAFNOSUPPORT;

	/*
	 *	Make local copies of the important bits
	 *	We decrement the metric by one for BSD compatibility.
	 */
	 
	flags = r->rt_flags;
	daddr = (__u32) ((struct sockaddr_in *) &r->rt_dst)->sin_addr.s_addr;
	mask  = (__u32) ((struct sockaddr_in *) &r->rt_genmask)->sin_addr.s_addr;
	gw    = (__u32) ((struct sockaddr_in *) &r->rt_gateway)->sin_addr.s_addr;
	metric = r->rt_metric > 0 ? r->rt_metric - 1 : 0;

	/*
	 *	BSD emulation: Permits route add someroute gw one-of-my-addresses
	 *	to indicate which iface. Not as clean as the nice Linux dev technique
	 *	but people keep using it... 
	 */
	 
	if (!dev && (flags & RTF_GATEWAY)) 
	{
		struct device *dev2;
		for (dev2 = dev_base ; dev2 != NULL ; dev2 = dev2->next) 
		{
			if ((dev2->flags & IFF_UP) && dev2->pa_addr == gw) 
			{
				flags &= ~RTF_GATEWAY;
				dev = dev2;
				break;
			}
		}
	}

	/*
	 *	Ignore faulty masks
	 */
	 
	if (bad_mask(mask, daddr))
		mask=0;

	/*
	 *	Set the mask to nothing for host routes.
	 */
	 
	if (flags & RTF_HOST)
		mask = 0xffffffff;
	else if (mask && r->rt_genmask.sa_family != AF_INET)
		return -EAFNOSUPPORT;

	/*
	 *	You can only gateway IP via IP..
	 */
	 
	if (flags & RTF_GATEWAY) 
	{
		if (r->rt_gateway.sa_family != AF_INET)
			return -EAFNOSUPPORT;
		if (!dev)
			dev = get_gw_dev(gw);
	} 
	else if (!dev)
		dev = ip_dev_check(daddr);

	/*
	 *	Unknown device.
	 */
	 
	if (dev == NULL)
		return -ENETUNREACH;

	/*
	 *	Add the route
	 */
	 
	ip_rt_add(flags, daddr, mask, gw, dev, r->rt_mss, r->rt_window, r->rt_irtt, metric);
	return 0;
}


/*
 *	Remove a route, as requested by the user.
 */

static int rt_kill(struct rtentry *r)
{
	struct sockaddr_in *trg;
	struct sockaddr_in *msk;
	struct sockaddr_in *gtw;
	char *devname;
	int err;

	trg = (struct sockaddr_in *) &r->rt_dst;
	msk = (struct sockaddr_in *) &r->rt_genmask;
	gtw = (struct sockaddr_in *) &r->rt_gateway;
	if ((devname = r->rt_dev) != NULL) 
	{
		err = getname(devname, &devname);
		if (err)
			return err;
	}
	/*
	 * metric can become negative here if it wasn't filled in
	 * but that's a fortunate accident; we really use that in rt_del.
	 */
	rt_del((__u32)trg->sin_addr.s_addr, (__u32)msk->sin_addr.s_addr, devname,
		(__u32)gtw->sin_addr.s_addr, r->rt_flags, r->rt_metric - 1);
	if ( devname != NULL )
		putname(devname);
	return 0;
}


/* 
 *	Called from the PROCfs module. This outputs /proc/net/route.
 */
 
int rt_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct rtable *r;
	int len=0;
	off_t pos=0;
	off_t begin=0;
	int size;

	len += sprintf(buffer,
		 "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n");
	pos=len;
  
	/*
	 *	This isn't quite right -- r->rt_dst is a struct! 
	 */
	 
	for (r = rt_base; r != NULL; r = r->rt_next) 
	{
        	size = sprintf(buffer+len, "%s\t%08lX\t%08lX\t%02X\t%d\t%lu\t%d\t%08lX\t%d\t%lu\t%u\n",
			r->rt_dev->name, (unsigned long)r->rt_dst, (unsigned long)r->rt_gateway,
			r->rt_flags, r->rt_refcnt, r->rt_use, r->rt_metric,
			(unsigned long)r->rt_mask, (int)r->rt_mss, r->rt_window, (int)r->rt_irtt);
		len+=size;
		pos+=size;
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			break;
  	}
  	
  	*start=buffer+(offset-begin);
  	len-=(offset-begin);
  	if(len>length)
  		len=length;
  	return len;
}

/*
 *	This is hackish, but results in better code. Use "-S" to see why.
 */
 
#define early_out ({ goto no_route; 1; })

/*
 *	Route a packet. This needs to be fairly quick. Florian & Co. 
 *	suggested a unified ARP and IP routing cache. Done right its
 *	probably a brilliant idea. I'd actually suggest a unified
 *	ARP/IP routing/Socket pointer cache. Volunteers welcome
 */
 
struct rtable * ip_rt_route(__u32 daddr, struct options *opt, __u32 *src_addr)
{
	struct rtable *rt;

	for (rt = rt_base; rt != NULL || early_out ; rt = rt->rt_next) 
	{
		if (!((rt->rt_dst ^ daddr) & rt->rt_mask))
			break;
		/*
		 *	broadcast addresses can be special cases.. 
		 */
		if (rt->rt_flags & RTF_GATEWAY)
			continue;		 
		if ((rt->rt_dev->flags & IFF_BROADCAST) &&
		    (rt->rt_dev->pa_brdaddr == daddr))
			break;
	}
	
	if(rt->rt_flags&RTF_REJECT)
		return NULL;
	
	if(src_addr!=NULL)
		*src_addr= rt->rt_dev->pa_addr;
		
	if (daddr == rt->rt_dev->pa_addr) {
		if ((rt = rt_loopback) == NULL)
			goto no_route;
	}
	rt->rt_use++;
	return rt;
no_route:
	return NULL;
}

struct rtable * ip_rt_local(__u32 daddr, struct options *opt, __u32 *src_addr)
{
	struct rtable *rt;

	for (rt = rt_base; rt != NULL || early_out ; rt = rt->rt_next) 
	{
		/*
		 *	No routed addressing.
		 */
		if (rt->rt_flags&RTF_GATEWAY)
			continue;
			
		if (!((rt->rt_dst ^ daddr) & rt->rt_mask))
			break;
		/*
		 *	broadcast addresses can be special cases.. 
		 */
		 
		if ((rt->rt_dev->flags & IFF_BROADCAST) &&
		     rt->rt_dev->pa_brdaddr == daddr)
			break;
	}
	
	if(src_addr!=NULL)
		*src_addr= rt->rt_dev->pa_addr;
		
	if (daddr == rt->rt_dev->pa_addr) {
		if ((rt = rt_loopback) == NULL)
			goto no_route;
	}
	rt->rt_use++;
	return rt;
no_route:
	return NULL;
}

/*
 *	Handle IP routing ioctl calls. These are used to manipulate the routing tables
 */
 
int ip_rt_ioctl(unsigned int cmd, void *arg)
{
	int err;
	struct rtentry rt;

	switch(cmd) 
	{
		case SIOCADDRT:		/* Add a route */
		case SIOCDELRT:		/* Delete a route */
			if (!suser())
				return -EPERM;
			err=verify_area(VERIFY_READ, arg, sizeof(struct rtentry));
			if (err)
				return err;
			memcpy_fromfs(&rt, arg, sizeof(struct rtentry));
			return (cmd == SIOCDELRT) ? rt_kill(&rt) : rt_new(&rt);
	}

	return -EINVAL;
}
