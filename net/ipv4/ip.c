/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The Internet Protocol (IP) module.
 *
 * Version:	@(#)ip.c	1.0.16b	9/1/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald Becker, <becker@super.org>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Richard Underwood
 *		Stefan Becker, <stefanb@yello.ping.de>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		
 *
 * Fixes:
 *		Alan Cox	:	Commented a couple of minor bits of surplus code
 *		Alan Cox	:	Undefining IP_FORWARD doesn't include the code
 *					(just stops a compiler warning).
 *		Alan Cox	:	Frames with >=MAX_ROUTE record routes, strict routes or loose routes
 *					are junked rather than corrupting things.
 *		Alan Cox	:	Frames to bad broadcast subnets are dumped
 *					We used to process them non broadcast and
 *					boy could that cause havoc.
 *		Alan Cox	:	ip_forward sets the free flag on the
 *					new frame it queues. Still crap because
 *					it copies the frame but at least it
 *					doesn't eat memory too.
 *		Alan Cox	:	Generic queue code and memory fixes.
 *		Fred Van Kempen :	IP fragment support (borrowed from NET2E)
 *		Gerhard Koerting:	Forward fragmented frames correctly.
 *		Gerhard Koerting: 	Fixes to my fix of the above 8-).
 *		Gerhard Koerting:	IP interface addressing fix.
 *		Linus Torvalds	:	More robustness checks
 *		Alan Cox	:	Even more checks: Still not as robust as it ought to be
 *		Alan Cox	:	Save IP header pointer for later
 *		Alan Cox	:	ip option setting
 *		Alan Cox	:	Use ip_tos/ip_ttl settings
 *		Alan Cox	:	Fragmentation bogosity removed
 *					(Thanks to Mark.Bush@prg.ox.ac.uk)
 *		Dmitry Gorodchanin :	Send of a raw packet crash fix.
 *		Alan Cox	:	Silly ip bug when an overlength
 *					fragment turns up. Now frees the
 *					queue.
 *		Linus Torvalds/ :	Memory leakage on fragmentation
 *		Alan Cox	:	handling.
 *		Gerhard Koerting:	Forwarding uses IP priority hints
 *		Teemu Rantanen	:	Fragment problems.
 *		Alan Cox	:	General cleanup, comments and reformat
 *		Alan Cox	:	SNMP statistics
 *		Alan Cox	:	BSD address rule semantics. Also see
 *					UDP as there is a nasty checksum issue
 *					if you do things the wrong way.
 *		Alan Cox	:	Always defrag, moved IP_FORWARD to the config.in file
 *		Alan Cox	: 	IP options adjust sk->priority.
 *		Pedro Roque	:	Fix mtu/length error in ip_forward.
 *		Alan Cox	:	Avoid ip_chk_addr when possible.
 *	Richard Underwood	:	IP multicasting.
 *		Alan Cox	:	Cleaned up multicast handlers.
 *		Alan Cox	:	RAW sockets demultiplex in the BSD style.
 *		Gunther Mayer	:	Fix the SNMP reporting typo
 *		Alan Cox	:	Always in group 224.0.0.1
 *	Pauline Middelink	:	Fast ip_checksum update when forwarding
 *					Masquerading support.
 *		Alan Cox	:	Multicast loopback error for 224.0.0.1
 *		Alan Cox	:	IP_MULTICAST_LOOP option.
 *		Alan Cox	:	Use notifiers.
 *		Bjorn Ekwall	:	Removed ip_csum (from slhc.c too)
 *		Bjorn Ekwall	:	Moved ip_fast_csum to ip.h (inline!)
 *		Stefan Becker   :       Send out ICMP HOST REDIRECT
 *	Arnt Gulbrandsen	:	ip_build_xmit
 *		Alan Cox	:	Per socket routing cache
 *		Alan Cox	:	Fixed routing cache, added header cache.
 *		Alan Cox	:	Loopback didnt work right in original ip_build_xmit - fixed it.
 *		Alan Cox	:	Only send ICMP_REDIRECT if src/dest are the same net.
 *		Alan Cox	:	Incoming IP option handling.
 *		Alan Cox	:	Set saddr on raw output frames as per BSD.
 *		Alan Cox	:	Stopped broadcast source route explosions.
 *		Alan Cox	:	Can disable source routing
 *		Takeshi Sone    :	Masquerading didn't work.
 *	Dave Bonn,Alan Cox	:	Faster IP forwarding whenever possible.
 *		Alan Cox	:	Memory leaks, tramples, misc debugging.
 *		Alan Cox	:	Fixed multicast (by popular demand 8))
 *
 *  
 *
 * To Fix:
 *		IP option processing is mostly not needed. ip_forward needs to know about routing rules
 *		and time stamp but that's about all. Use the route mtu field here too
 *		IP fragmentation wants rewriting cleanly. The RFC815 algorithm is much more efficient
 *		and could be made very efficient with the addition of some virtual memory hacks to permit
 *		the allocation of a buffer that can then be 'grown' by twiddling page tables.
 *		Output fragmentation wants updating along with the buffer management to use a single 
 *		interleaved copy algorithm so that fragmenting has a one copy overhead. Actual packet
 *		output should probably do its own fragmentation at the UDP/RAW layer. TCP shouldn't cause
 *		fragmentation anyway.
 *
 *		FIXME: copy frag 0 iph to qp->iph
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
#include <linux/errno.h>
#include <linux/config.h>

#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <net/snmp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <net/raw.h>
#include <net/checksum.h>
#include <linux/igmp.h>
#include <linux/ip_fw.h>

#define CONFIG_IP_DEFRAG

extern int last_retran;
extern void sort_send(struct sock *sk);

#define min(a,b)	((a)<(b)?(a):(b))

/*
 *	SNMP management statistics
 */

#ifdef CONFIG_IP_FORWARD
struct ip_mib ip_statistics={1,64,};	/* Forwarding=Yes, Default TTL=64 */
#else
struct ip_mib ip_statistics={0,64,};	/* Forwarding=No, Default TTL=64 */
#endif

/*
 *	Handle the issuing of an ioctl() request
 *	for the ip device. This is scheduled to
 *	disappear
 */

int ip_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	switch(cmd)
	{
		default:
			return(-EINVAL);
	}
}


/*
 *	Take an skb, and fill in the MAC header.
 */

static int ip_send(struct sk_buff *skb, unsigned long daddr, int len, struct device *dev, unsigned long saddr)
{
	int mac = 0;

	skb->dev = dev;
	skb->arp = 1;
	if (dev->hard_header)
	{
		/*
		 *	Build a hardware header. Source address is our mac, destination unknown
		 *  	(rebuild header will sort this out)
		 */
		skb_reserve(skb,(dev->hard_header_len+15)&~15);	/* 16 byte aligned IP headers are good */
		mac = dev->hard_header(skb, dev, ETH_P_IP, NULL, NULL, len);
		if (mac < 0)
		{
			mac = -mac;
			skb->arp = 0;
			skb->raddr = daddr;	/* next routing address */
		}
	}
	return mac;
}

int ip_id_count = 0;

/*
 * This routine builds the appropriate hardware/IP headers for
 * the routine.  It assumes that if *dev != NULL then the
 * protocol knows what it's doing, otherwise it uses the
 * routing/ARP tables to select a device struct.
 */
int ip_build_header(struct sk_buff *skb, unsigned long saddr, unsigned long daddr,
		struct device **dev, int type, struct options *opt, int len, int tos, int ttl)
{
	struct rtable *rt;
	unsigned long raddr;
	int tmp;
	unsigned long src;
	struct iphdr *iph;

	/*
	 *	See if we need to look up the device.
	 */

#ifdef CONFIG_INET_MULTICAST	
	if(MULTICAST(daddr) && *dev==NULL && skb->sk && *skb->sk->ip_mc_name)
		*dev=dev_get(skb->sk->ip_mc_name);
#endif
	if (*dev == NULL)
	{
		if(skb->localroute)
			rt = ip_rt_local(daddr, NULL, &src);
		else
			rt = ip_rt_route(daddr, NULL, &src);
		if (rt == NULL)
		{
			ip_statistics.IpOutNoRoutes++;
			return(-ENETUNREACH);
		}

		*dev = rt->rt_dev;
		/*
		 *	If the frame is from us and going off machine it MUST MUST MUST
		 *	have the output device ip address and never the loopback
		 */
		if (LOOPBACK(saddr) && !LOOPBACK(daddr))
			saddr = src;/*rt->rt_dev->pa_addr;*/
		raddr = rt->rt_gateway;

	}
	else
	{
		/*
		 *	We still need the address of the first hop.
		 */
		if(skb->localroute)
			rt = ip_rt_local(daddr, NULL, &src);
		else
			rt = ip_rt_route(daddr, NULL, &src);
		/*
		 *	If the frame is from us and going off machine it MUST MUST MUST
		 *	have the output device ip address and never the loopback
		 */
		if (LOOPBACK(saddr) && !LOOPBACK(daddr))
			saddr = src;/*rt->rt_dev->pa_addr;*/

		raddr = (rt == NULL) ? 0 : rt->rt_gateway;
	}

	/*
	 *	No source addr so make it our addr
	 */
	if (saddr == 0)
		saddr = src;

	/*
	 *	No gateway so aim at the real destination
	 */
	if (raddr == 0)
		raddr = daddr;

	/*
	 *	Now build the MAC header.
	 */

	tmp = ip_send(skb, raddr, len, *dev, saddr);

	/*
	 *	Book keeping
	 */

	skb->dev = *dev;
	skb->saddr = saddr;
	if (skb->sk)
		skb->sk->saddr = saddr;

	/*
	 *	Now build the IP header.
	 */

	/*
	 *	If we are using IPPROTO_RAW, then we don't need an IP header, since
	 *	one is being supplied to us by the user
	 */

	if(type == IPPROTO_RAW)
		return (tmp);

	/*
	 *	Build the IP addresses
	 */
	 
	iph=(struct iphdr *)skb_put(skb,sizeof(struct iphdr));

	iph->version  = 4;
	iph->ihl      = 5;
	iph->tos      = tos;
	iph->frag_off = 0;
	iph->ttl      = ttl;
	iph->daddr    = daddr;
	iph->saddr    = saddr;
	iph->protocol = type;
	skb->ip_hdr   = iph;

	return(20 + tmp);	/* IP header plus MAC header size */
}


/*
 *	Generate a checksum for an outgoing IP datagram.
 */

void ip_send_check(struct iphdr *iph)
{
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
}

/************************ Fragment Handlers From NET2E **********************************/


/*
 *	This fragment handler is a bit of a heap. On the other hand it works quite
 *	happily and handles things quite well.
 */

static struct ipq *ipqueue = NULL;		/* IP fragment queue	*/

/*
 *	Create a new fragment entry.
 */

static struct ipfrag *ip_frag_create(int offset, int end, struct sk_buff *skb, unsigned char *ptr)
{
	struct ipfrag *fp;

	fp = (struct ipfrag *) kmalloc(sizeof(struct ipfrag), GFP_ATOMIC);
	if (fp == NULL)
	{
		NETDEBUG(printk("IP: frag_create: no memory left !\n"));
		return(NULL);
	}
	memset(fp, 0, sizeof(struct ipfrag));

	/* Fill in the structure. */
	fp->offset = offset;
	fp->end = end;
	fp->len = end - offset;
	fp->skb = skb;
	fp->ptr = ptr;

	return(fp);
}


/*
 *	Find the correct entry in the "incomplete datagrams" queue for
 *	this IP datagram, and return the queue entry address if found.
 */

static struct ipq *ip_find(struct iphdr *iph)
{
	struct ipq *qp;
	struct ipq *qplast;

	cli();
	qplast = NULL;
	for(qp = ipqueue; qp != NULL; qplast = qp, qp = qp->next)
	{
		if (iph->id== qp->iph->id && iph->saddr == qp->iph->saddr &&
			iph->daddr == qp->iph->daddr && iph->protocol == qp->iph->protocol)
		{
			del_timer(&qp->timer);	/* So it doesn't vanish on us. The timer will be reset anyway */
			sti();
			return(qp);
		}
	}
	sti();
	return(NULL);
}


/*
 *	Remove an entry from the "incomplete datagrams" queue, either
 *	because we completed, reassembled and processed it, or because
 *	it timed out.
 */

static void ip_free(struct ipq *qp)
{
	struct ipfrag *fp;
	struct ipfrag *xp;

	/*
	 * Stop the timer for this entry.
	 */

	del_timer(&qp->timer);

	/* Remove this entry from the "incomplete datagrams" queue. */
	cli();
	if (qp->prev == NULL)
	{
		ipqueue = qp->next;
		if (ipqueue != NULL)
			ipqueue->prev = NULL;
	}
	else
	{
		qp->prev->next = qp->next;
		if (qp->next != NULL)
			qp->next->prev = qp->prev;
	}

	/* Release all fragment data. */

	fp = qp->fragments;
	while (fp != NULL)
	{
		xp = fp->next;
		IS_SKB(fp->skb);
		kfree_skb(fp->skb,FREE_READ);
		kfree_s(fp, sizeof(struct ipfrag));
		fp = xp;
	}

	/* Release the IP header. */
	kfree_s(qp->iph, 64 + 8);

	/* Finally, release the queue descriptor itself. */
	kfree_s(qp, sizeof(struct ipq));
	sti();
}


/*
 *	Oops- a fragment queue timed out.  Kill it and send an ICMP reply.
 */

static void ip_expire(unsigned long arg)
{
	struct ipq *qp;

	qp = (struct ipq *)arg;

	/*
	 *	Send an ICMP "Fragment Reassembly Timeout" message.
	 */

	ip_statistics.IpReasmTimeout++;
	ip_statistics.IpReasmFails++;   
	/* This if is always true... shrug */
	if(qp->fragments!=NULL)
		icmp_send(qp->fragments->skb,ICMP_TIME_EXCEEDED,
				ICMP_EXC_FRAGTIME, 0, qp->dev);

	/*
	 *	Nuke the fragment queue.
	 */
	ip_free(qp);
}


/*
 * 	Add an entry to the 'ipq' queue for a newly received IP datagram.
 * 	We will (hopefully :-) receive all other fragments of this datagram
 * 	in time, so we just create a queue for this datagram, in which we
 * 	will insert the received fragments at their respective positions.
 */

static struct ipq *ip_create(struct sk_buff *skb, struct iphdr *iph, struct device *dev)
{
	struct ipq *qp;
	int ihlen;

	qp = (struct ipq *) kmalloc(sizeof(struct ipq), GFP_ATOMIC);
	if (qp == NULL)
	{
		NETDEBUG(printk("IP: create: no memory left !\n"));
		return(NULL);
		skb->dev = qp->dev;
	}
	memset(qp, 0, sizeof(struct ipq));

	/*
	 *	Allocate memory for the IP header (plus 8 octets for ICMP).
	 */

	ihlen = iph->ihl * 4;
	qp->iph = (struct iphdr *) kmalloc(64 + 8, GFP_ATOMIC);
	if (qp->iph == NULL)
	{
		NETDEBUG(printk("IP: create: no memory left !\n"));
		kfree_s(qp, sizeof(struct ipq));
		return(NULL);
	}

	memcpy(qp->iph, iph, ihlen + 8);
	qp->len = 0;
	qp->ihlen = ihlen;
	qp->fragments = NULL;
	qp->dev = dev;

	/* Start a timer for this entry. */
	qp->timer.expires = IP_FRAG_TIME;		/* about 30 seconds	*/
	qp->timer.data = (unsigned long) qp;		/* pointer to queue	*/
	qp->timer.function = ip_expire;			/* expire function	*/
	add_timer(&qp->timer);

	/* Add this entry to the queue. */
	qp->prev = NULL;
	cli();
	qp->next = ipqueue;
	if (qp->next != NULL)
		qp->next->prev = qp;
	ipqueue = qp;
	sti();
	return(qp);
}


/*
 *	See if a fragment queue is complete.
 */

static int ip_done(struct ipq *qp)
{
	struct ipfrag *fp;
	int offset;

	/* Only possible if we received the final fragment. */
	if (qp->len == 0)
		return(0);

	/* Check all fragment offsets to see if they connect. */
	fp = qp->fragments;
	offset = 0;
	while (fp != NULL)
	{
		if (fp->offset > offset)
			return(0);	/* fragment(s) missing */
		offset = fp->end;
		fp = fp->next;
	}

	/* All fragments are present. */
	return(1);
}


/*
 *	Build a new IP datagram from all its fragments.
 *
 *	FIXME: We copy here because we lack an effective way of handling lists
 *	of bits on input. Until the new skb data handling is in I'm not going
 *	to touch this with a bargepole. 
 */

static struct sk_buff *ip_glue(struct ipq *qp)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	struct ipfrag *fp;
	unsigned char *ptr;
	int count, len;

	/*
	 *	Allocate a new buffer for the datagram.
	 */
	len = qp->ihlen + qp->len;

	if ((skb = dev_alloc_skb(len)) == NULL)
	{
		ip_statistics.IpReasmFails++;
		NETDEBUG(printk("IP: queue_glue: no memory for gluing queue %p\n", qp));
		ip_free(qp);
		return(NULL);
	}

	/* Fill in the basic details. */
	skb_put(skb,len);
	skb->h.raw = skb->data;
	skb->free = 1;

	/* Copy the original IP headers into the new buffer. */
	ptr = (unsigned char *) skb->h.raw;
	memcpy(ptr, ((unsigned char *) qp->iph), qp->ihlen);
	ptr += qp->ihlen;

	count = 0;

	/* Copy the data portions of all fragments into the new buffer. */
	fp = qp->fragments;
	while(fp != NULL)
	{
		if(count+fp->len > skb->len)
		{
			NETDEBUG(printk("Invalid fragment list: Fragment over size.\n"));
			ip_free(qp);
			kfree_skb(skb,FREE_WRITE);
			ip_statistics.IpReasmFails++;
			return NULL;
		}
		memcpy((ptr + fp->offset), fp->ptr, fp->len);
		count += fp->len;
		fp = fp->next;
	}

	/* We glued together all fragments, so remove the queue entry. */
	ip_free(qp);

	/* Done with all fragments. Fixup the new IP header. */
	iph = skb->h.iph;
	iph->frag_off = 0;
	iph->tot_len = htons((iph->ihl * 4) + count);
	skb->ip_hdr = iph;

	ip_statistics.IpReasmOKs++;
	return(skb);
}


/*
 *	Process an incoming IP datagram fragment.
 */

static struct sk_buff *ip_defrag(struct iphdr *iph, struct sk_buff *skb, struct device *dev)
{
	struct ipfrag *prev, *next, *tmp;
	struct ipfrag *tfp;
	struct ipq *qp;
	struct sk_buff *skb2;
	unsigned char *ptr;
	int flags, offset;
	int i, ihl, end;

	ip_statistics.IpReasmReqds++;

	/* Find the entry of this IP datagram in the "incomplete datagrams" queue. */
	qp = ip_find(iph);

	/* Is this a non-fragmented datagram? */
	offset = ntohs(iph->frag_off);
	flags = offset & ~IP_OFFSET;
	offset &= IP_OFFSET;
	if (((flags & IP_MF) == 0) && (offset == 0))
	{
		if (qp != NULL)
			ip_free(qp);	/* Huh? How could this exist?? */
		return(skb);
	}

	offset <<= 3;		/* offset is in 8-byte chunks */

	/*
	 * If the queue already existed, keep restarting its timer as long
	 * as we still are receiving fragments.  Otherwise, create a fresh
	 * queue entry.
	 */

	if (qp != NULL)
	{
		del_timer(&qp->timer);
		qp->timer.expires = IP_FRAG_TIME;	/* about 30 seconds */
		qp->timer.data = (unsigned long) qp;	/* pointer to queue */
		qp->timer.function = ip_expire;		/* expire function */
		add_timer(&qp->timer);
	}
	else
	{
		/*
		 *	If we failed to create it, then discard the frame
		 */
		if ((qp = ip_create(skb, iph, dev)) == NULL)
		{
			skb->sk = NULL;
			kfree_skb(skb, FREE_READ);
			ip_statistics.IpReasmFails++;
			return NULL;
		}
	}

	/*
	 *	Determine the position of this fragment.
	 */

	ihl = iph->ihl * 4;
	end = offset + ntohs(iph->tot_len) - ihl;

	/*
	 *	Point into the IP datagram 'data' part.
	 */

	ptr = skb->data + ihl;

	/*
	 *	Is this the final fragment?
	 */

	if ((flags & IP_MF) == 0)
		qp->len = end;

	/*
	 * 	Find out which fragments are in front and at the back of us
	 * 	in the chain of fragments so far.  We must know where to put
	 * 	this fragment, right?
	 */

	prev = NULL;
	for(next = qp->fragments; next != NULL; next = next->next)
	{
		if (next->offset > offset)
			break;	/* bingo! */
		prev = next;
	}

	/*
	 * 	We found where to put this one.
	 * 	Check for overlap with preceding fragment, and, if needed,
	 * 	align things so that any overlaps are eliminated.
	 */
	if (prev != NULL && offset < prev->end)
	{
		i = prev->end - offset;
		offset += i;	/* ptr into datagram */
		ptr += i;	/* ptr into fragment data */
	}

	/*
	 * Look for overlap with succeeding segments.
	 * If we can merge fragments, do it.
	 */

	for(tmp=next; tmp != NULL; tmp = tfp)
	{
		tfp = tmp->next;
		if (tmp->offset >= end)
			break;		/* no overlaps at all */

		i = end - next->offset;			/* overlap is 'i' bytes */
		tmp->len -= i;				/* so reduce size of	*/
		tmp->offset += i;			/* next fragment	*/
		tmp->ptr += i;
		/*
		 *	If we get a frag size of <= 0, remove it and the packet
		 *	that it goes with.
		 */
		if (tmp->len <= 0)
		{
			if (tmp->prev != NULL)
				tmp->prev->next = tmp->next;
			else
				qp->fragments = tmp->next;

			if (tfp->next != NULL)
				tmp->next->prev = tmp->prev;
			
			next=tfp;	/* We have killed the original next frame */

			kfree_skb(tmp->skb,FREE_READ);
			kfree_s(tmp, sizeof(struct ipfrag));
		}
	}

	/*
	 *	Insert this fragment in the chain of fragments.
	 */

	tfp = NULL;
	tfp = ip_frag_create(offset, end, skb, ptr);

	/*
	 *	No memory to save the fragment - so throw the lot
	 */

	if (!tfp)
	{
		skb->sk = NULL;
		kfree_skb(skb, FREE_READ);
		return NULL;
	}
	tfp->prev = prev;
	tfp->next = next;
	if (prev != NULL)
		prev->next = tfp;
	else
		qp->fragments = tfp;

	if (next != NULL)
		next->prev = tfp;

	/*
	 * 	OK, so we inserted this new fragment into the chain.
	 * 	Check if we now have a full IP datagram which we can
	 * 	bump up to the IP layer...
	 */

	if (ip_done(qp))
	{
		skb2 = ip_glue(qp);		/* glue together the fragments */
		return(skb2);
	}
	return(NULL);
}


/*
 *	This IP datagram is too large to be sent in one piece.  Break it up into
 *	smaller pieces (each of size equal to the MAC header plus IP header plus
 *	a block of the data of the original IP data part) that will yet fit in a
 *	single device frame, and queue such a frame for sending by calling the
 *	ip_queue_xmit().  Note that this is recursion, and bad things will happen
 *	if this function causes a loop...
 *
 *	Yes this is inefficient, feel free to submit a quicker one.
 *
 *	**Protocol Violation**
 *	We copy all the options to each fragment. !FIXME!
 */
 
void ip_fragment(struct sock *sk, struct sk_buff *skb, struct device *dev, int is_frag)
{
	struct iphdr *iph;
	unsigned char *raw;
	unsigned char *ptr;
	struct sk_buff *skb2;
	int left, mtu, hlen, len;
	int offset;
	unsigned long flags;

	/*
	 *	Point into the IP datagram header.
	 */

	raw = skb->data;
	iph = (struct iphdr *) (raw + dev->hard_header_len);

	skb->ip_hdr = iph;

	/*
	 *	Setup starting values.
	 */

	hlen = iph->ihl * 4;
	left = ntohs(iph->tot_len) - hlen;	/* Space per frame */
	hlen += dev->hard_header_len;		/* Total header size */
	mtu = (dev->mtu - hlen);		/* Size of data space */
	ptr = (raw + hlen);			/* Where to start from */

	/*
	 *	Check for any "DF" flag. [DF means do not fragment]
	 */

	if (ntohs(iph->frag_off) & IP_DF)
	{
		/*
		 *	Reply giving the MTU of the failed hop.
		 */
		ip_statistics.IpFragFails++;
		icmp_send(skb,ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, dev->mtu, dev);
		return;
	}

	/*
	 *	The protocol doesn't seem to say what to do in the case that the
	 *	frame + options doesn't fit the mtu. As it used to fall down dead
	 *	in this case we were fortunate it didn't happen
	 */

	if(mtu<8)
	{
		/* It's wrong but it's better than nothing */
		icmp_send(skb,ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED,dev->mtu, dev);
		ip_statistics.IpFragFails++;
		return;
	}

	/*
	 *	Fragment the datagram.
	 */

	/*
	 *	The initial offset is 0 for a complete frame. When
	 *	fragmenting fragments it's wherever this one starts.
	 */

	if (is_frag & 2)
		offset = (ntohs(iph->frag_off) & IP_OFFSET) << 3;
	else
		offset = 0;


	/*
	 *	Keep copying data until we run out.
	 */

	while(left > 0)
	{
		len = left;
		/* IF: it doesn't fit, use 'mtu' - the data space left */
		if (len > mtu)
			len = mtu;
		/* IF: we are not sending upto and including the packet end
		   then align the next start on an eight byte boundary */
		if (len < left)
		{
			len/=8;
			len*=8;
		}
		/*
		 *	Allocate buffer.
		 */

		if ((skb2 = alloc_skb(len + hlen+15,GFP_ATOMIC)) == NULL)
		{
			NETDEBUG(printk("IP: frag: no memory for new fragment!\n"));
			ip_statistics.IpFragFails++;
			return;
		}

		/*
		 *	Set up data on packet
		 */

		skb2->arp = skb->arp;
		if(skb->free==0)
			printk("IP fragmenter: BUG free!=1 in fragmenter\n");
		skb2->free = 1;
		skb_put(skb2,len + hlen);
		skb2->h.raw=(char *) skb2->data;
		/*
		 *	Charge the memory for the fragment to any owner
		 *	it might possess
		 */

		save_flags(flags);
		if (sk)
		{
			cli();
			sk->wmem_alloc += skb2->truesize;
			skb2->sk=sk;
		}
		restore_flags(flags);
		skb2->raddr = skb->raddr;	/* For rebuild_header - must be here */

		/*
		 *	Copy the packet header into the new buffer.
		 */

		memcpy(skb2->h.raw, raw, hlen);

		/*
		 *	Copy a block of the IP datagram.
		 */
		memcpy(skb2->h.raw + hlen, ptr, len);
		left -= len;

		skb2->h.raw+=dev->hard_header_len;

		/*
		 *	Fill in the new header fields.
		 */
		iph = (struct iphdr *)(skb2->h.raw/*+dev->hard_header_len*/);
		iph->frag_off = htons((offset >> 3));
		/*
		 *	Added AC : If we are fragmenting a fragment thats not the
		 *		   last fragment then keep MF on each bit
		 */
		if (left > 0 || (is_frag & 1))
			iph->frag_off |= htons(IP_MF);
		ptr += len;
		offset += len;

		/*
		 *	Put this fragment into the sending queue.
		 */

		ip_statistics.IpFragCreates++;

		ip_queue_xmit(sk, dev, skb2, 2);
	}
	ip_statistics.IpFragOKs++;
}



#ifdef CONFIG_IP_FORWARD

/*
 *	Forward an IP datagram to its next destination.
 */

int ip_forward(struct sk_buff *skb, struct device *dev, int is_frag, unsigned long target_addr, int target_strict)
{
	struct device *dev2;	/* Output device */
	struct iphdr *iph;	/* Our header */
	struct sk_buff *skb2;	/* Output packet */
	struct rtable *rt;	/* Route we use */
	unsigned char *ptr;	/* Data pointer */
	unsigned long raddr;	/* Router IP address */
#ifdef CONFIG_IP_FIREWALL
	int fw_res = 0;		/* Forwarding result */	
	
	/* 
	 *	See if we are allowed to forward this.
 	 *	Note: demasqueraded fragments are always 'back'warded.
	 */

	
	if(!(is_frag&4))
	{
		fw_res=ip_fw_chk(skb->h.iph, dev, ip_fw_fwd_chain, ip_fw_fwd_policy, 0);
		switch (fw_res) {
		case 1:
#ifdef CONFIG_IP_MASQUERADE
		case 2:
#endif
			break;
		case -1:
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0, dev);
			/* fall thru */
		default:
			return -1;
		}
	}
#endif
	/*
	 *	According to the RFC, we must first decrease the TTL field. If
	 *	that reaches zero, we must reply an ICMP control message telling
	 *	that the packet's lifetime expired.
	 *
	 *	Exception:
	 *	We may not generate an ICMP for an ICMP. icmp_send does the
	 *	enforcement of this so we can forget it here. It is however
	 *	sometimes VERY important.
	 */

	iph = skb->h.iph;
	iph->ttl--;

	/*
	 *	Re-compute the IP header checksum.
	 *	This is inefficient. We know what has happened to the header
	 *	and could thus adjust the checksum as Phil Karn does in KA9Q
	 */

	iph->check = ntohs(iph->check) + 0x0100;
	if ((iph->check & 0xFF00) == 0)
		iph->check++;		/* carry overflow */
	iph->check = htons(iph->check);

	if (iph->ttl <= 0)
	{
		/* Tell the sender its packet died... */
		icmp_send(skb, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL, 0, dev);
		return -1;
	}

	/*
	 * OK, the packet is still valid.  Fetch its destination address,
	 * and give it to the IP sender for further processing.
	 */

	rt = ip_rt_route(target_addr, NULL, NULL);
	if (rt == NULL)
	{
		/*
		 *	Tell the sender its packet cannot be delivered. Again
		 *	ICMP is screened later.
		 */
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_NET_UNREACH, 0, dev);
		return -1;
	}


	/*
	 * Gosh.  Not only is the packet valid; we even know how to
	 * forward it onto its final destination.  Can we say this
	 * is being plain lucky?
	 * If the router told us that there is no GW, use the dest.
	 * IP address itself- we seem to be connected directly...
	 */

	raddr = rt->rt_gateway;

	if (raddr != 0)
	{
		/*
		 *	Strict routing permits no gatewaying
		 */
		
		if(target_strict)
		{
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_SR_FAILED, 0, dev);
			return -1;
		}
	
		/*
		 *	There is a gateway so find the correct route for it.
		 *	Gateways cannot in turn be gatewayed.
		 */

		rt = ip_rt_route(raddr, NULL, NULL);
		if (rt == NULL)
		{
			/*
			 *	Tell the sender its packet cannot be delivered...
			 */
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0, dev);
			return -1;
		}
		if (rt->rt_gateway != 0)
			raddr = rt->rt_gateway;
	}
	else
		raddr = target_addr;

	/*
	 *	Having picked a route we can now send the frame out.
	 */

	dev2 = rt->rt_dev;

	/*
	 *	In IP you never have to forward a frame on the interface that it 
	 *	arrived upon. We now generate an ICMP HOST REDIRECT giving the route
	 *	we calculated.
	 */
#ifndef CONFIG_IP_NO_ICMP_REDIRECT
	if (dev == dev2 && !((iph->saddr^iph->daddr)&dev->pa_mask) && (rt->rt_flags&RTF_MODIFIED))
		icmp_send(skb, ICMP_REDIRECT, ICMP_REDIR_HOST, raddr, dev);
#endif		

	/*
	 * We now may allocate a new buffer, and copy the datagram into it.
	 * If the indicated interface is up and running, kick it.
	 */

	if (dev2->flags & IFF_UP)
	{
#ifdef CONFIG_IP_MASQUERADE
		/*
		 * If this fragment needs masquerading, make it so...
		 * (Dont masquerade de-masqueraded fragments)
		 */
		if (!(is_frag&4) && fw_res==2)
			ip_fw_masquerade(&skb, dev2);
#endif

		if(skb_headroom(skb)<dev2->hard_header_len)
			skb2 = alloc_skb(dev2->hard_header_len + skb->len + 15, GFP_ATOMIC);
		else skb2=skb;
		
		/*
		 *	This is rare and since IP is tolerant of network failures
		 *	quite harmless.
		 */
		
		if (skb2 == NULL)
		{
			NETDEBUG(printk("\nIP: No memory available for IP forward\n"));
			return -1;
		}
		

		/* Now build the MAC header. */
		(void) ip_send(skb2, raddr, skb->len, dev2, dev2->pa_addr);

		/*
		 *	We have to copy the bytes over as the new header wouldn't fit
		 *	the old buffer. This should be very rare.
		 */
		 
		if(skb2!=skb)
		{
			ptr = skb_put(skb2,skb->len);
			skb2->free = 1;
			skb2->h.raw = ptr;

			/*
			 *	Copy the packet data into the new buffer.
			 */
			memcpy(ptr, skb->h.raw, skb->len);
		}

		ip_statistics.IpForwDatagrams++;

		/*
		 *	See if it needs fragmenting. Note in ip_rcv we tagged
		 *	the fragment type. This must be right so that
		 *	the fragmenter does the right thing.
		 */

		if(skb2->len > dev2->mtu + dev2->hard_header_len)
		{
			ip_fragment(NULL,skb2,dev2, is_frag);
			kfree_skb(skb2,FREE_WRITE);
		}
		else
		{
#ifdef CONFIG_IP_ACCT		
			/*
			 *	Count mapping we shortcut
			 */
			 
			ip_fw_chk(iph,dev,ip_acct_chain,IP_FW_F_ACCEPT,1);
#endif			
			
			/*
			 *	Map service types to priority. We lie about
			 *	throughput being low priority, but it's a good
			 *	choice to help improve general usage.
			 */
			if(iph->tos & IPTOS_LOWDELAY)
				dev_queue_xmit(skb2, dev2, SOPRI_INTERACTIVE);
			else if(iph->tos & IPTOS_THROUGHPUT)
				dev_queue_xmit(skb2, dev2, SOPRI_BACKGROUND);
			else
				dev_queue_xmit(skb2, dev2, SOPRI_NORMAL);
		}
	}
	else
		return -1;
	
	/*
	 *	Tell the caller if their buffer is free.
	 */
	 
	if(skb==skb2)
		return 0;
	return 1;
}


#endif

/*
 *	This function receives all incoming IP datagrams.
 *
 *	On entry skb->data points to the start of the IP header and
 *	the MAC header has been removed.
 */

int ip_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct iphdr *iph = skb->h.iph;
	struct sock *raw_sk=NULL;
	unsigned char hash;
	unsigned char flag = 0;
	struct inet_protocol *ipprot;
	int brd=IS_MYADDR;
	unsigned long target_addr;
	int target_strict=0;
	int is_frag=0;
#ifdef CONFIG_IP_FIREWALL
	int err;
#endif	

	ip_statistics.IpInReceives++;

	/*
	 *	Tag the ip header of this packet so we can find it
	 */

	skb->ip_hdr = iph;

	/*
	 *	RFC1122: 3.1.2.2 MUST silently discard any IP frame that fails the checksum.
	 *	RFC1122: 3.1.2.3 MUST discard a frame with invalid source address [NEEDS FIXING].
	 *
	 *	Is the datagram acceptable?
	 *
	 *	1.	Length at least the size of an ip header
	 *	2.	Version of 4
	 *	3.	Checksums correctly. [Speed optimisation for later, skip loopback checksums]
	 *	4.	Doesn't have a bogus length
	 *	(5.	We ought to check for IP multicast addresses and undefined types.. does this matter ?)
	 */

	if (skb->len<sizeof(struct iphdr) || iph->ihl<5 || iph->version != 4 || ip_fast_csum((unsigned char *)iph, iph->ihl) !=0
		|| skb->len < ntohs(iph->tot_len))
	{
		ip_statistics.IpInHdrErrors++;
		kfree_skb(skb, FREE_WRITE);
		return(0);
	}

	/*
	 *	Our transport medium may have padded the buffer out. Now we know it
	 *	is IP we can trim to the true length of the frame.
	 *	Note this now means skb->len holds ntohs(iph->tot_len).
	 */

	skb_trim(skb,ntohs(iph->tot_len));
	
	/*
	 *	See if the firewall wants to dispose of the packet. 
	 */

#ifdef	CONFIG_IP_FIREWALL
	
	if ((err=ip_fw_chk(iph,dev,ip_fw_blk_chain,ip_fw_blk_policy, 0))<1)
	{
		if(err==-1)
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0, dev);
		kfree_skb(skb, FREE_WRITE);
		return 0;	
	}

#endif
	

	/*
	 *	Next analyse the packet for options. Studies show under one packet in
	 *	a thousand have options....
	 */
	 
	target_addr = iph->daddr;

	if (iph->ihl != 5)
	{ 
		/* Humph.. options. Lots of annoying fiddly bits */
		
		/*
		 *	This is straight from the RFC. It might even be right ;)
		 *
		 * 	RFC 1122: 3.2.1.8 STREAMID option is obsolete and MUST be ignored.
		 *	RFC 1122: 3.2.1.8 MUST NOT crash on a zero length option.
		 *	RFC 1122: 3.2.1.8 MUST support acting as final destination of a source route.
		 */
		 
		int opt_space=4*(iph->ihl-5);
		int opt_size;
		unsigned char *opt_ptr=skb->h.raw+sizeof(struct iphdr);
	
		skb->ip_summed=0;		/* Our free checksum is bogus for this case */
			
		while(opt_space>0)
		{
			if(*opt_ptr==IPOPT_NOOP)
			{
				opt_ptr++;
				opt_space--;
				continue;
			}
			if(*opt_ptr==IPOPT_END)
				break;	/* Done */
			if(opt_space<2 || (opt_size=opt_ptr[1])<2 || opt_ptr[1]>opt_space)
			{
				/*
				 *	RFC 1122: 3.2.2.5  SHOULD send parameter problem reports.
				 */
				icmp_send(skb, ICMP_PARAMETERPROB, 0, 0, skb->dev);
				kfree_skb(skb, FREE_READ);
				return -EINVAL;
			}
			switch(opt_ptr[0])
			{
				case IPOPT_SEC:
					/* Should we drop this ?? */
					break;
				case IPOPT_SSRR:	/* These work almost the same way */
					target_strict=1;
					/* Fall through */
				case IPOPT_LSRR:
#ifdef CONFIG_IP_NOSR
					kfree_skb(skb, FREE_READ);
					return -EINVAL;
#endif					
				case IPOPT_RR:
				/*
				 *	RFC 1122: 3.2.1.8 Support for RR is OPTIONAL.
				 */
					if (iph->daddr!=skb->dev->pa_addr && (brd = ip_chk_addr(iph->daddr)) == 0) 
						break;
					if((opt_size<3) || ( opt_ptr[0]==IPOPT_RR && opt_ptr[2] > opt_size-4 ))
					{
						if(ip_chk_addr(iph->daddr))
							icmp_send(skb, ICMP_PARAMETERPROB, 0, 0, skb->dev);
						kfree_skb(skb, FREE_READ);
						return -EINVAL;
					}
					if(opt_ptr[2] > opt_size-4 )
						break;
					/* Bytes are [IPOPT_xxRR][Length][EntryPointer][Entry0][Entry1].... */
					/* This isn't going to be too portable - FIXME */
					if(opt_ptr[0]!=IPOPT_RR)
					{
						int t;
						target_addr=*(u32 *)(&opt_ptr[opt_ptr[2]]);	/* Get hop */
						t=ip_chk_addr(target_addr);
						if(t==IS_MULTICAST||t==IS_BROADCAST)
						{
							if(ip_chk_addr(iph->daddr))
								icmp_send(skb, ICMP_PARAMETERPROB, 0, 0, skb->dev);
							kfree_skb(skb,FREE_READ);
							return -EINVAL;						
						}
					}
					*(u32 *)(&opt_ptr[opt_ptr[2]])=skb->dev->pa_addr;	/* Record hop */
					break;
				case IPOPT_TIMESTAMP:
				/*
				 *	RFC 1122: 3.2.1.8 The timestamp option is OPTIONAL but if implemented
				 *	MUST meet various rules (read the spec).
				 */
					NETDEBUG(printk("ICMP: Someone finish the timestamp routine ;)\n"));
					break;
				default:
					break;
			}
			opt_ptr+=opt_size;
			opt_space-=opt_size;
		}
					
	}


	/*
	 *	Remember if the frame is fragmented.
	 */
	 
	if(iph->frag_off)
	{
		if (iph->frag_off & htons(IP_MF))
			is_frag|=1;
		/*
		 *	Last fragment ?
		 */
	
		if (iph->frag_off & htons(IP_OFFSET))
			is_frag|=2;
	}
	
	/*
	 *	Do any IP forwarding required.  chk_addr() is expensive -- avoid it someday.
	 *
	 *	This is inefficient. While finding out if it is for us we could also compute
	 *	the routing table entry. This is where the great unified cache theory comes
	 *	in as and when someone implements it
	 *
	 *	For most hosts over 99% of packets match the first conditional
	 *	and don't go via ip_chk_addr. Note: brd is set to IS_MYADDR at
	 *	function entry.
	 */

	if ( iph->daddr == skb->dev->pa_addr || (brd = ip_chk_addr(iph->daddr)) != 0)
	{
#ifdef CONFIG_IP_MULTICAST	

		if(brd==IS_MULTICAST && iph->daddr!=IGMP_ALL_HOSTS && !(dev->flags&IFF_LOOPBACK))
		{
			/*
			 *	Check it is for one of our groups
			 */
			struct ip_mc_list *ip_mc=dev->ip_mc_list;
			do
			{
				if(ip_mc==NULL)
				{	
					kfree_skb(skb, FREE_WRITE);
					return 0;
				}
				if(ip_mc->multiaddr==iph->daddr)
					break;
				ip_mc=ip_mc->next;
			}
			while(1);
		}
#endif

#ifdef CONFIG_IP_MASQUERADE
		/*
		 * Do we need to de-masquerade this fragment?
		 */
		if (ip_fw_demasquerade(skb)) 
		{
			struct iphdr *iph=skb->h.iph;
			if(ip_forward(skb, dev, is_frag|4, iph->daddr, 0))
				kfree_skb(skb, FREE_WRITE);
			return(0);
		}
#endif

		/*
		 *	Account for the packet
		 */
 
#ifdef CONFIG_IP_ACCT
		ip_fw_chk(iph,dev,ip_acct_chain,IP_FW_F_ACCEPT,1);
#endif	

		/*
		 *	Reassemble IP fragments.
		 */

		if(is_frag)
		{
			/* Defragment. Obtain the complete packet if there is one */
			skb=ip_defrag(iph,skb,dev);
			if(skb==NULL)
				return 0;
			skb->dev = dev;
			iph=skb->h.iph;
		}

		/*
		 *	Point into the IP datagram, just past the header.
		 */

		skb->ip_hdr = iph;
		skb->h.raw += iph->ihl*4;

		/*
		 *	Deliver to raw sockets. This is fun as to avoid copies we want to make no surplus copies.
		 *
		 *	RFC 1122: SHOULD pass TOS value up to the transport layer.
		 */
 
		hash = iph->protocol & (SOCK_ARRAY_SIZE-1);

		/* 
		 *	If there maybe a raw socket we must check - if not we don't care less 
		 */
		 
		if((raw_sk=raw_prot.sock_array[hash])!=NULL)
		{
			struct sock *sknext=NULL;
			struct sk_buff *skb1;
			raw_sk=get_sock_raw(raw_sk, hash,  iph->saddr, iph->daddr);
			if(raw_sk)	/* Any raw sockets */
			{
				do
				{
					/* Find the next */
					sknext=get_sock_raw(raw_sk->next, hash, iph->saddr, iph->daddr);
					if(sknext)
						skb1=skb_clone(skb, GFP_ATOMIC);
					else
						break;	/* One pending raw socket left */
					if(skb1)
						raw_rcv(raw_sk, skb1, dev, iph->saddr,iph->daddr);
					raw_sk=sknext;
				}
				while(raw_sk!=NULL);
				
				/*
				 *	Here either raw_sk is the last raw socket, or NULL if none 
				 */
				 
				/*
				 *	We deliver to the last raw socket AFTER the protocol checks as it avoids a surplus copy 
				 */
			}
		}
	
		/*
		 *	skb->h.raw now points at the protocol beyond the IP header.
		 */
	
		hash = iph->protocol & (MAX_INET_PROTOS -1);
		for (ipprot = (struct inet_protocol *)inet_protos[hash];ipprot != NULL;ipprot=(struct inet_protocol *)ipprot->next)
		{
			struct sk_buff *skb2;
	
			if (ipprot->protocol != iph->protocol)
				continue;
		       /*
			* 	See if we need to make a copy of it.  This will
			* 	only be set if more than one protocol wants it.
			* 	and then not for the last one. If there is a pending
			*	raw delivery wait for that
			*/
	
			if (ipprot->copy || raw_sk)
			{
				skb2 = skb_clone(skb, GFP_ATOMIC);
				if(skb2==NULL)
					continue;
			}
			else
			{
				skb2 = skb;
			}
			flag = 1;

		       /*
			*	Pass on the datagram to each protocol that wants it,
			*	based on the datagram protocol.  We should really
			*	check the protocol handler's return values here...
			*/

			ipprot->handler(skb2, dev, NULL, iph->daddr,
				(ntohs(iph->tot_len) - (iph->ihl * 4)),
				iph->saddr, 0, ipprot);

		}

		/*
		 *	All protocols checked.
		 *	If this packet was a broadcast, we may *not* reply to it, since that
		 *	causes (proven, grin) ARP storms and a leakage of memory (i.e. all
		 *	ICMP reply messages get queued up for transmission...)
		 */

		if(raw_sk!=NULL)	/* Shift to last raw user */
			raw_rcv(raw_sk, skb, dev, iph->saddr, iph->daddr);
		else if (!flag)		/* Free and report errors */
		{
			if (brd != IS_BROADCAST && brd!=IS_MULTICAST)
				icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PROT_UNREACH, 0, dev);	
			kfree_skb(skb, FREE_WRITE);
		}

		return(0);
	}

	/*
	 *	Do any IP forwarding required.
	 */
	
	/*
	 *	Don't forward multicast or broadcast frames.
	 */

	if(skb->pkt_type!=PACKET_HOST || brd==IS_BROADCAST)
	{
		kfree_skb(skb,FREE_WRITE);
		return 0;
	}

	/*
	 *	The packet is for another target. Forward the frame
	 */

#ifdef CONFIG_IP_FORWARD
	if(ip_forward(skb, dev, is_frag, target_addr, target_strict))
		kfree_skb(skb, FREE_WRITE);
#else
/*	printk("Machine %lx tried to use us as a forwarder to %lx but we have forwarding disabled!\n",
			iph->saddr,iph->daddr);*/
	ip_statistics.IpInAddrErrors++;
	kfree_skb(skb, FREE_WRITE);
#endif
	return(0);
}
	

/*
 *	Loop a packet back to the sender.
 */
 
static void ip_loopback(struct device *old_dev, struct sk_buff *skb)
{
	extern struct device loopback_dev;
	struct device *dev=&loopback_dev;
	int len=skb->len-old_dev->hard_header_len;
	struct sk_buff *newskb=dev_alloc_skb(len+dev->hard_header_len+15);
	
	if(newskb==NULL)
		return;
		
	newskb->link3=NULL;
	newskb->sk=NULL;
	newskb->dev=dev;
	newskb->saddr=skb->saddr;
	newskb->daddr=skb->daddr;
	newskb->raddr=skb->raddr;
	newskb->free=1;
	newskb->lock=0;
	newskb->users=0;
	newskb->pkt_type=skb->pkt_type;
	
	/*
	 *	Put a MAC header on the packet
	 */
	ip_send(newskb, skb->ip_hdr->daddr, len, dev, skb->ip_hdr->saddr);
	/*
	 *	Add the rest of the data space.	
	 */
	newskb->ip_hdr=(struct iphdr *)skb_put(newskb, len);
	/*
	 *	Copy the data
	 */
	memcpy(newskb->ip_hdr,skb->ip_hdr,len);

	/* Recurse. The device check against IFF_LOOPBACK will stop infinite recursion */
		
	/*printk("Loopback output queued [%lX to %lX].\n", newskb->ip_hdr->saddr,newskb->ip_hdr->daddr);*/
	ip_queue_xmit(NULL, dev, newskb, 1);
}


/*
 * Queues a packet to be sent, and starts the transmitter
 * if necessary.  if free = 1 then we free the block after
 * transmit, otherwise we don't. If free==2 we not only
 * free the block but also don't assign a new ip seq number.
 * This routine also needs to put in the total length,
 * and compute the checksum
 */

void ip_queue_xmit(struct sock *sk, struct device *dev,
	      struct sk_buff *skb, int free)
{
	struct iphdr *iph;
	unsigned char *ptr;

	/* Sanity check */
	if (dev == NULL)
	{
		NETDEBUG(printk("IP: ip_queue_xmit dev = NULL\n"));
		return;
	}

	IS_SKB(skb);

	/*
	 *	Do some book-keeping in the packet for later
	 */


	skb->dev = dev;
	skb->when = jiffies;

	/*
	 *	Find the IP header and set the length. This is bad
	 *	but once we get the skb data handling code in the
	 *	hardware will push its header sensibly and we will
	 *	set skb->ip_hdr to avoid this mess and the fixed
	 *	header length problem
	 */

	ptr = skb->data;
	ptr += dev->hard_header_len;
	iph = (struct iphdr *)ptr;
	skb->ip_hdr = iph;
	iph->tot_len = ntohs(skb->len-dev->hard_header_len);

#ifdef CONFIG_IP_FIREWALL
	if(ip_fw_chk(iph, dev, ip_fw_blk_chain, ip_fw_blk_policy, 0) != 1)
		/* just don't send this packet */
		return;
#endif	

	/*
	 *	No reassigning numbers to fragments...
	 */

	if(free!=2)
		iph->id      = htons(ip_id_count++);
	else
		free=1;

	/* All buffers without an owner socket get freed */
	if (sk == NULL)
		free = 1;

	skb->free = free;

	/*
	 *	Do we need to fragment. Again this is inefficient.
	 *	We need to somehow lock the original buffer and use
	 *	bits of it.
	 */

	if(skb->len > dev->mtu + dev->hard_header_len)
	{
		ip_fragment(sk,skb,dev,0);
		IS_SKB(skb);
		kfree_skb(skb,FREE_WRITE);
		return;
	}

	/*
	 *	Add an IP checksum
	 */

	ip_send_check(iph);

	/*
	 *	Print the frame when debugging
	 */

	/*
	 *	More debugging. You cannot queue a packet already on a list
	 *	Spot this and moan loudly.
	 */
	if (skb->next != NULL)
	{
		NETDEBUG(printk("ip_queue_xmit: next != NULL\n"));
		skb_unlink(skb);
	}

	/*
	 *	If a sender wishes the packet to remain unfreed
	 *	we add it to his send queue. This arguably belongs
	 *	in the TCP level since nobody else uses it. BUT
	 *	remember IPng might change all the rules.
	 */

	if (!free)
	{
		unsigned long flags;
		/* The socket now has more outstanding blocks */

		sk->packets_out++;

		/* Protect the list for a moment */
		save_flags(flags);
		cli();

		if (skb->link3 != NULL)
		{
			NETDEBUG(printk("ip.c: link3 != NULL\n"));
			skb->link3 = NULL;
		}
		if (sk->send_head == NULL)
		{
			sk->send_tail = skb;
			sk->send_head = skb;
		}
		else
		{
			sk->send_tail->link3 = skb;
			sk->send_tail = skb;
		}
		/* skb->link3 is NULL */

		/* Interrupt restore */
		restore_flags(flags);
	}
	else
		/* Remember who owns the buffer */
		skb->sk = sk;

	/*
	 *	If the indicated interface is up and running, send the packet.
	 */
	 
	ip_statistics.IpOutRequests++;
#ifdef CONFIG_IP_ACCT
	ip_fw_chk(iph,dev,ip_acct_chain,IP_FW_F_ACCEPT,1);
#endif	
	
#ifdef CONFIG_IP_MULTICAST	

	/*
	 *	Multicasts are looped back for other local users
	 */
	 
	if (MULTICAST(iph->daddr) && !(dev->flags&IFF_LOOPBACK))
	{
		if(sk==NULL || sk->ip_mc_loop)
		{
			if(iph->daddr==IGMP_ALL_HOSTS)
				ip_loopback(dev,skb);
			else
			{
				struct ip_mc_list *imc=dev->ip_mc_list;
				while(imc!=NULL)
				{
					if(imc->multiaddr==iph->daddr)
					{
						ip_loopback(dev,skb);
						break;
					}
					imc=imc->next;
				}
			}
		}
		/* Multicasts with ttl 0 must not go beyond the host */
		
		if(skb->ip_hdr->ttl==0)
		{
			kfree_skb(skb, FREE_READ);
			return;
		}
	}
#endif
	if((dev->flags&IFF_BROADCAST) && (iph->daddr==dev->pa_brdaddr||iph->daddr==0xFFFFFFFF) && !(dev->flags&IFF_LOOPBACK))
		ip_loopback(dev,skb);
		
	if (dev->flags & IFF_UP)
	{
		/*
		 *	If we have an owner use its priority setting,
		 *	otherwise use NORMAL
		 */

		if (sk != NULL)
		{
			dev_queue_xmit(skb, dev, sk->priority);
		}
		else
		{
			dev_queue_xmit(skb, dev, SOPRI_NORMAL);
		}
	}
	else
	{
		ip_statistics.IpOutDiscards++;
		if (free)
			kfree_skb(skb, FREE_WRITE);
	}
}



#ifdef CONFIG_IP_MULTICAST

/*
 *	Write an multicast group list table for the IGMP daemon to
 *	read.
 */
 
int ip_mc_procinfo(char *buffer, char **start, off_t offset, int length)
{
	off_t pos=0, begin=0;
	struct ip_mc_list *im;
	unsigned long flags;
	int len=0;
	struct device *dev;
	
	len=sprintf(buffer,"Device    : Count\tGroup    Users Timer\n");  
	save_flags(flags);
	cli();
	
	for(dev = dev_base; dev; dev = dev->next)
	{
                if((dev->flags&IFF_UP)&&(dev->flags&IFF_MULTICAST))
                {
                        len+=sprintf(buffer+len,"%-10s: %5d\n",
					dev->name, dev->mc_count);
                        for(im = dev->ip_mc_list; im; im = im->next)
                        {
                                len+=sprintf(buffer+len,
					"\t\t\t%08lX %5d %d:%08lX\n",
                                        im->multiaddr, im->users,
					im->tm_running, im->timer.expires);
                                pos=begin+len;
                                if(pos<offset)
                                {
                                        len=0;
                                        begin=pos;
                                }
                                if(pos>offset+length)
                                        break;
                        }
                }
	}
	restore_flags(flags);
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;	
	return len;
}


/*
 *	Socket option code for IP. This is the end of the line after any TCP,UDP etc options on
 *	an IP socket.
 *
 *	We implement IP_TOS (type of service), IP_TTL (time to live).
 *
 *	Next release we will sort out IP_OPTIONS since for some people are kind of important.
 */

static struct device *ip_mc_find_devfor(unsigned long addr)
{
	struct device *dev;
	for(dev = dev_base; dev; dev = dev->next)
	{
		if((dev->flags&IFF_UP)&&(dev->flags&IFF_MULTICAST)&&
			(dev->pa_addr==addr))
			return dev;
	}

	return NULL;
}

#endif

int ip_setsockopt(struct sock *sk, int level, int optname, char *optval, int optlen)
{
	int val,err;
	unsigned char ucval;
#if defined(CONFIG_IP_FIREWALL) || defined(CONFIG_IP_ACCT)
	struct ip_fw tmp_fw;
#endif	
	if (optval == NULL)
		return(-EINVAL);

	err=verify_area(VERIFY_READ, optval, sizeof(int));
	if(err)
		return err;

	val = get_user((int *) optval);
	ucval=get_user((unsigned char *) optval);

	if(level!=SOL_IP)
		return -EOPNOTSUPP;

	switch(optname)
	{
		case IP_TOS:
			if(val<0||val>255)
				return -EINVAL;
			sk->ip_tos=val;
			if(val==IPTOS_LOWDELAY)
				sk->priority=SOPRI_INTERACTIVE;
			if(val==IPTOS_THROUGHPUT)
				sk->priority=SOPRI_BACKGROUND;
			return 0;
		case IP_TTL:
			if(val<1||val>255)
				return -EINVAL;
			sk->ip_ttl=val;
			return 0;
#ifdef CONFIG_IP_MULTICAST
		case IP_MULTICAST_TTL: 
		{
			sk->ip_mc_ttl=(int)ucval;
	                return 0;
		}
		case IP_MULTICAST_LOOP: 
		{
			if(ucval!=0 && ucval!=1)
				 return -EINVAL;
			sk->ip_mc_loop=(int)ucval;
			return 0;
		}
		case IP_MULTICAST_IF: 
		{
			struct in_addr addr;
			struct device *dev=NULL;
			
			/*
			 *	Check the arguments are allowable
			 */

			err=verify_area(VERIFY_READ, optval, sizeof(addr));
			if(err)
				return err;
				
			memcpy_fromfs(&addr,optval,sizeof(addr));
			
			
			/*
			 *	What address has been requested
			 */
			
			if(addr.s_addr==INADDR_ANY)	/* Default */
			{
				sk->ip_mc_name[0]=0;
				return 0;
			}
			
			/*
			 *	Find the device
			 */
			 
			dev=ip_mc_find_devfor(addr.s_addr);
						
			/*
			 *	Did we find one
			 */
			 
			if(dev) 
			{
				strcpy(sk->ip_mc_name,dev->name);
				return 0;
			}
			return -EADDRNOTAVAIL;
		}
		
		case IP_ADD_MEMBERSHIP: 
		{
		
/*
 *	FIXME: Add/Del membership should have a semaphore protecting them from re-entry
 */
			struct ip_mreq mreq;
			unsigned long route_src;
			struct rtable *rt;
			struct device *dev=NULL;
			
			/*
			 *	Check the arguments.
			 */

			err=verify_area(VERIFY_READ, optval, sizeof(mreq));
			if(err)
				return err;

			memcpy_fromfs(&mreq,optval,sizeof(mreq));

			/* 
			 *	Get device for use later
			 */

			if(mreq.imr_interface.s_addr==INADDR_ANY) 
			{
				/*
				 *	Not set so scan.
				 */
				if((rt=ip_rt_route(mreq.imr_multiaddr.s_addr,NULL, &route_src))!=NULL)
				{
					dev=rt->rt_dev;
					rt->rt_use--;
				}
			}
			else
			{
				/*
				 *	Find a suitable device.
				 */
				
				dev=ip_mc_find_devfor(mreq.imr_interface.s_addr);
			}
			
			/*
			 *	No device, no cookies.
			 */
			 
			if(!dev)
				return -ENODEV;
				
			/*
			 *	Join group.
			 */
			 
			return ip_mc_join_group(sk,dev,mreq.imr_multiaddr.s_addr);
		}
		
		case IP_DROP_MEMBERSHIP: 
		{
			struct ip_mreq mreq;
			struct rtable *rt;
                        unsigned long route_src;
			struct device *dev=NULL;

			/*
			 *	Check the arguments
			 */
			 
			err=verify_area(VERIFY_READ, optval, sizeof(mreq));
			if(err)
				return err;

			memcpy_fromfs(&mreq,optval,sizeof(mreq));

			/*
			 *	Get device for use later 
			 */
 
			if(mreq.imr_interface.s_addr==INADDR_ANY) 
			{
				if((rt=ip_rt_route(mreq.imr_multiaddr.s_addr,NULL, &route_src))!=NULL)
			        {
					dev=rt->rt_dev;
					rt->rt_use--;
				}
			}
			else 
			{
			
				dev=ip_mc_find_devfor(mreq.imr_interface.s_addr);
			}
			
			/*
			 *	Did we find a suitable device.
			 */
			 
			if(!dev)
				return -ENODEV;
				
			/*
			 *	Leave group
			 */
			 
			return ip_mc_leave_group(sk,dev,mreq.imr_multiaddr.s_addr);
		}
#endif			
#ifdef CONFIG_IP_FIREWALL
		case IP_FW_ADD_BLK:
		case IP_FW_DEL_BLK:
		case IP_FW_ADD_FWD:
		case IP_FW_DEL_FWD:
		case IP_FW_CHK_BLK:
		case IP_FW_CHK_FWD:
		case IP_FW_FLUSH_BLK:
		case IP_FW_FLUSH_FWD:
		case IP_FW_ZERO_BLK:
		case IP_FW_ZERO_FWD:
		case IP_FW_POLICY_BLK:
		case IP_FW_POLICY_FWD:
			if(!suser())
				return -EPERM;
			if(optlen>sizeof(tmp_fw) || optlen<1)
				return -EINVAL;
			err=verify_area(VERIFY_READ,optval,optlen);
			if(err)
				return err;
			memcpy_fromfs(&tmp_fw,optval,optlen);
			err=ip_fw_ctl(optname, &tmp_fw,optlen);
			return -err;	/* -0 is 0 after all */
			
#endif
#ifdef CONFIG_IP_ACCT
		case IP_ACCT_DEL:
		case IP_ACCT_ADD:
		case IP_ACCT_FLUSH:
		case IP_ACCT_ZERO:
			if(!suser())
				return -EPERM;
			if(optlen>sizeof(tmp_fw) || optlen<1)
				return -EINVAL;
			err=verify_area(VERIFY_READ,optval,optlen);
			if(err)
				return err;
			memcpy_fromfs(&tmp_fw, optval,optlen);
			err=ip_acct_ctl(optname, &tmp_fw,optlen);
			return -err;	/* -0 is 0 after all */
#endif
		/* IP_OPTIONS and friends go here eventually */
		default:
			return(-ENOPROTOOPT);
	}
}

/*
 *	Get the options. Note for future reference. The GET of IP options gets the
 *	_received_ ones. The set sets the _sent_ ones.
 */

int ip_getsockopt(struct sock *sk, int level, int optname, char *optval, int *optlen)
{
	int val,err;
#ifdef CONFIG_IP_MULTICAST
	int len;
#endif
	
	if(level!=SOL_IP)
		return -EOPNOTSUPP;

	switch(optname)
	{
		case IP_TOS:
			val=sk->ip_tos;
			break;
		case IP_TTL:
			val=sk->ip_ttl;
			break;
#ifdef CONFIG_IP_MULTICAST			
		case IP_MULTICAST_TTL:
			val=sk->ip_mc_ttl;
			break;
		case IP_MULTICAST_LOOP:
			val=sk->ip_mc_loop;
			break;
		case IP_MULTICAST_IF:
			err=verify_area(VERIFY_WRITE, optlen, sizeof(int));
			if(err)
  				return err;
  			len=strlen(sk->ip_mc_name);
  			err=verify_area(VERIFY_WRITE, optval, len);
		  	if(err)
  				return err;
  			put_user(len,(int *) optlen);
			memcpy_tofs((void *)optval,sk->ip_mc_name, len);
			return 0;
#endif
		default:
			return(-ENOPROTOOPT);
	}
	err=verify_area(VERIFY_WRITE, optlen, sizeof(int));
	if(err)
		return err;
	put_user(sizeof(int),(int *) optlen);

	err=verify_area(VERIFY_WRITE, optval, sizeof(int));
	if(err)
		return err;
	put_user(val,(int *) optval);

	return(0);
}

/*
 *	Build and send a packet, with as little as one copy
 *
 *	Doesn't care much about ip options... option length can be
 *	different for fragment at 0 and other fragments.
 *
 *	Note that the fragment at the highest offset is sent first,
 *	so the getfrag routine can fill in the TCP/UDP checksum header
 *	field in the last fragment it sends... actually it also helps
 * 	the reassemblers, they can put most packets in at the head of
 *	the fragment queue, and they know the total size in advance. This
 *	last feature will measurable improve the Linux fragment handler.
 *
 *	The callback has five args, an arbitrary pointer (copy of frag),
 *	the source IP address (may depend on the routing table), the 
 *	destination adddress (char *), the offset to copy from, and the
 *	length to be copied.
 * 
 */

int ip_build_xmit(struct sock *sk,
		   void getfrag (void *,
				 int,
				 char *,
				 unsigned int,
				 unsigned int),
		   void *frag,
		   unsigned short int length,
		   int daddr,
		   int flags,
		   int type) 
{
	struct rtable *rt;
	unsigned int fraglen, maxfraglen, fragheaderlen;
	int offset, mf;
	unsigned long saddr;
	unsigned short id;
	struct iphdr *iph;
	int local=0;
	struct device *dev;
	
	ip_statistics.IpOutRequests++;


#ifdef CONFIG_INET_MULTICAST	
	if(sk && MULTICAST(daddr) && *sk->ip_mc_name)
	{
		dev=dev_get(skb->ip_mc_name);
		if(!dev)
			return -ENODEV;
		rt=NULL;
	}
	else
	{
#endif	
		/*
		 *	Perform the IP routing decisions
		 */
	 
		if(sk->localroute || flags&MSG_DONTROUTE)
			local=1;
	
		rt = sk->ip_route_cache;
		
		/*
		 *	See if the routing cache is outdated. We need to clean this up once we are happy it is reliable
		 *	by doing the invalidation actively in the route change and header change.
		 */
	
		saddr=sk->ip_route_saddr;	 
		if(!rt || sk->ip_route_stamp != rt_stamp || daddr!=sk->ip_route_daddr || sk->ip_route_local!=local || sk->saddr!=sk->ip_route_saddr)
		{
			if(local)
				rt = ip_rt_local(daddr, NULL, &saddr);
			else
				rt = ip_rt_route(daddr, NULL, &saddr);
			sk->ip_route_local=local;
			sk->ip_route_daddr=daddr;
			sk->ip_route_saddr=saddr;
			sk->ip_route_stamp=rt_stamp;
			sk->ip_route_cache=rt;
			sk->ip_hcache_ver=NULL;
			sk->ip_hcache_state= 0;
		}
		else if(rt)
		{
			/*
			 *	Attempt header caches only if the cached route is being reused. Header cache
			 *	is not ultra cheap to set up. This means we only set it up on the second packet,
			 *	so one shot communications are not slowed. We assume (seems reasonable) that 2 is
			 *	probably going to be a stream of data.
			 */
			if(rt->rt_dev->header_cache && sk->ip_hcache_state!= -1)
			{
				if(sk->ip_hcache_ver==NULL || sk->ip_hcache_stamp!=*sk->ip_hcache_ver)
					rt->rt_dev->header_cache(rt->rt_dev,sk,saddr,daddr);
				else
					/* Can't cache. Remember this */
					sk->ip_hcache_state= -1;
			}
		}
		
		if (rt == NULL) 
		{
	 		ip_statistics.IpOutNoRoutes++;
			return(-ENETUNREACH);
		}
	
		if (sk->saddr && (!LOOPBACK(sk->saddr) || LOOPBACK(daddr)))
			saddr = sk->saddr;
			
		dev=rt->rt_dev;
#ifdef CONFIG_INET_MULTICAST
	}
#endif		

	/*
	 *	Now compute the buffer space we require
	 */ 
	 
	/*
	 *	Try the simple case first. This leaves broadcast, multicast, fragmented frames, and by
	 *	choice RAW frames within 20 bytes of maximum size(rare) to the long path
	 */
	 
	if(length+20 <= dev->mtu && !MULTICAST(daddr) && daddr!=0xFFFFFFFF && daddr!=dev->pa_brdaddr)
	{	
		int error;
		struct sk_buff *skb=sock_alloc_send_skb(sk, length+20+15+dev->hard_header_len,0,&error);
		if(skb==NULL)
			return error;
		skb->dev=dev;
		skb->free=1;
		skb->when=jiffies;
		skb->sk=sk;
		skb->arp=0;
		skb->saddr=saddr;
		length+=20;	/* We do this twice so the subtract once is quicker */
		skb->raddr=(rt&&rt->rt_gateway)?rt->rt_gateway:daddr;
		skb_reserve(skb,(dev->hard_header_len+15)&~15);
		if(sk->ip_hcache_state>0)
		{
			memcpy(skb_push(skb,dev->hard_header_len),sk->ip_hcache_data,dev->hard_header_len);
			skb->arp=1;
		}
		else if(dev->hard_header)
		{
			if(dev->hard_header(skb,dev,ETH_P_IP,NULL,NULL,0)>0)
				skb->arp=1;
		}
		skb->ip_hdr=iph=(struct iphdr *)skb_put(skb,length);
		if(type!=IPPROTO_RAW)
		{
			iph->version=4;
			iph->ihl=5;
			iph->tos=sk->ip_tos;
			iph->tot_len = htons(length);
			iph->id=htons(ip_id_count++);
			iph->frag_off = 0;
			iph->ttl=sk->ip_ttl;
			iph->protocol=type;
			iph->saddr=saddr;
			iph->daddr=daddr;
			iph->check=0;
			iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
			getfrag(frag,saddr,(void *)(iph+1),0, length-20);
		}
		else
			getfrag(frag,saddr,(void *)iph,0,length);
#ifdef CONFIG_IP_ACCT
		ip_fw_chk((void *)skb->data,dev,ip_acct_chain, IP_FW_F_ACCEPT,1);
#endif		
		if(dev->flags&IFF_UP)
			dev_queue_xmit(skb,dev,sk->priority);
		else
		{
			ip_statistics.IpOutDiscards++;
			kfree_skb(skb, FREE_WRITE);
		}
		return 0;
	}
			
			
	fragheaderlen = dev->hard_header_len;
	if(type != IPPROTO_RAW)
		fragheaderlen += 20;
		
	/*
	 *	Fragheaderlen is the size of 'overhead' on each buffer. Now work
	 *	out the size of the frames to send.
	 */
	 
	maxfraglen = ((dev->mtu-20) & ~7) + fragheaderlen;
	
	/*
	 *	Start at the end of the frame by handling the remainder.
	 */
	 
	offset = length - (length % (maxfraglen - fragheaderlen));
	
	/*
	 *	Amount of memory to allocate for final fragment.
	 */
	 
	fraglen = length - offset + fragheaderlen;
	
	if(fraglen==0)
	{
		fraglen = maxfraglen;
		offset -= maxfraglen-fragheaderlen;
	}
	
	
	/*
	 *	The last fragment will not have MF (more fragments) set.
	 */
	 
	mf = 0;

	/*
	 *	Can't fragment raw packets 
	 */
	 
	if (type == IPPROTO_RAW && offset > 0)
 		return(-EMSGSIZE);

	/*
	 *	Get an identifier
	 */
	 
	id = htons(ip_id_count++);

	/*
	 *	Being outputting the bytes.
	 */
	 
	do 
	{
		struct sk_buff * skb;
		int error;
		char *data;

		/*
		 *	Get the memory we require with some space left for alignment.
		 */

		skb = sock_alloc_send_skb(sk, fraglen+15, 0, &error);
		if (skb == NULL)
			return(error);

		/*
		 *	Fill in the control structures
		 */
		 
		skb->next = skb->prev = NULL;
		skb->dev = dev;
		skb->when = jiffies;
		skb->free = 1; /* dubious, this one */
		skb->sk = sk;
		skb->arp = 0;
		skb->saddr = saddr;
		skb->raddr = (rt&&rt->rt_gateway) ? rt->rt_gateway : daddr;
		skb_reserve(skb,(dev->hard_header_len+15)&~15);
		data = skb_put(skb, fraglen-dev->hard_header_len);

		/*
		 *	Save us ARP and stuff. In the optimal case we do no route lookup (route cache ok)
		 *	no ARP lookup (arp cache ok) and output. The cache checks are still too slow but
		 *	this can be fixed later. For gateway routes we ought to have a rt->.. header cache
		 *	pointer to speed header cache builds for identical targets.
		 */
		 
		if(sk->ip_hcache_state>0)
		{
			memcpy(skb_push(skb,dev->hard_header_len),sk->ip_hcache_data, dev->hard_header_len);
			skb->arp=1;
		}
		else if (dev->hard_header)
		{
			if(dev->hard_header(skb, dev, ETH_P_IP, 
						NULL, NULL, 0)>0)
				skb->arp=1;
		}
		
		/*
		 *	Find where to start putting bytes.
		 */
		 
		skb->ip_hdr = iph = (struct iphdr *)data;

		/*
		 *	Only write IP header onto non-raw packets 
		 */
		 
		if(type != IPPROTO_RAW) 
		{

			iph->version = 4;
			iph->ihl = 5; /* ugh */
			iph->tos = sk->ip_tos;
			iph->tot_len = htons(fraglen - fragheaderlen + iph->ihl*4);
			iph->id = id;
			iph->frag_off = htons(offset>>3);
			iph->frag_off |= mf;
#ifdef CONFIG_IP_MULTICAST
			if (MULTICAST(daddr))
				iph->ttl = sk->ip_mc_ttl;
			else
#endif
				iph->ttl = sk->ip_ttl;
			iph->protocol = type;
			iph->check = 0;
			iph->saddr = saddr;
			iph->daddr = daddr;
			iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
			data += iph->ihl*4;
			
			/*
			 *	Any further fragments will have MF set.
			 */
			 
			mf = htons(IP_MF);
		}
		
		/*
		 *	User data callback
		 */

		getfrag(frag, saddr, data, offset, fraglen-fragheaderlen);
		
		/*
		 *	Account for the fragment.
		 */
		 
#ifdef CONFIG_IP_ACCT
		if(!offset)
			ip_fw_chk(iph, dev, ip_acct_chain, IP_FW_F_ACCEPT, 1);
#endif	
		offset -= (maxfraglen-fragheaderlen);
		fraglen = maxfraglen;

#ifdef CONFIG_IP_MULTICAST

		/*
		 *	Multicasts are looped back for other local users
		 */
	 
		if (MULTICAST(daddr) && !(dev->flags&IFF_LOOPBACK)) 
		{
			/*
			 *	Loop back any frames. The check for IGMP_ALL_HOSTS is because
			 *	you are always magically a member of this group.
			 */
			 
			if(sk==NULL || sk->ip_mc_loop) 
			{
				if(skb->daddr==IGMP_ALL_HOSTS)
					ip_loopback(rt->rt_dev,skb);
				else 
				{
					struct ip_mc_list *imc=rt->rt_dev->ip_mc_list;
					while(imc!=NULL) 
					{
						if(imc->multiaddr==daddr) 
						{
							ip_loopback(rt->rt_dev,skb);
							break;
						}
						imc=imc->next;
					}
				}
			}

			/*
			 *	Multicasts with ttl 0 must not go beyond the host. Fixme: avoid the
			 *	extra clone.
			 */

			if(skb->ip_hdr->ttl==0)
				kfree_skb(skb, FREE_READ);
		}
#endif
		/*
		 *	BSD loops broadcasts
		 */
		 
		if((dev->flags&IFF_BROADCAST) && (daddr==0xFFFFFFFF || daddr==dev->pa_brdaddr) && !(dev->flags&IFF_LOOPBACK))
			ip_loopback(dev,skb);

		/*
		 *	Now queue the bytes into the device.
		 */
		 
		if (dev->flags & IFF_UP) 
		{
			dev_queue_xmit(skb, dev, sk->priority);
		} 
		else 
		{
			/*
			 *	Whoops... 
			 *
			 *	FIXME:	There is a small nasty here. During the ip_build_xmit we could
			 *	page fault between the route lookup and device send, the device might be
			 *	removed and unloaded.... We need to add device locks on this.
			 */
			 
			ip_statistics.IpOutDiscards++;
			kfree_skb(skb, FREE_WRITE);
			return(0); /* lose rest of fragments */
		}
	} 
	while (offset >= 0);
	
	return(0);
}
    

/*
 *	IP protocol layer initialiser
 */

static struct packet_type ip_packet_type =
{
	0,	/* MUTTER ntohs(ETH_P_IP),*/
	NULL,	/* All devices */
	ip_rcv,
	NULL,
	NULL,
};

/*
 *	Device notifier
 */
 
static int ip_rt_event(unsigned long event, void *ptr)
{
	if(event==NETDEV_DOWN)
		ip_rt_flush(ptr);
	return NOTIFY_DONE;
}

struct notifier_block ip_rt_notifier={
	ip_rt_event,
	NULL,
	0
};

/*
 *	IP registers the packet type and then calls the subprotocol initialisers
 */

void ip_init(void)
{
	ip_packet_type.type=htons(ETH_P_IP);
	dev_add_pack(&ip_packet_type);

	/* So we flush routes when a device is downed */	
	register_netdevice_notifier(&ip_rt_notifier);
/*	ip_raw_init();
	ip_packet_init();
	ip_tcp_init();
	ip_udp_init();*/
}

