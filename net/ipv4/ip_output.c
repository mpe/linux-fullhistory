/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The Internet Protocol (IP) output module.
 *
 * Version:	@(#)ip.c	1.0.16b	9/1/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald Becker, <becker@super.org>
 *		Alan Cox, <Alan.Cox@linux.org>
 *		Richard Underwood
 *		Stefan Becker, <stefanb@yello.ping.de>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *
 *	See ip_input.c for original log
 *
 *	Fixes:
 *		Alan Cox	:	Missing nonblock feature in ip_build_xmit.
 *		Mike Kilburn	:	htons() missing in ip_build_xmit.
 *		Bradford Johnson:	Fix faulty handling of some frames when 
 *					no route is found.
 *		Alexander Demenshin:	Missing sk/skb free in ip_queue_xmit
 *					(in case if packet not accepted by
 *					output firewall rules)
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
#include <linux/proc_fs.h>
#include <linux/stat.h>

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
#include <linux/firewall.h>
#include <linux/mroute.h>
#include <net/netlink.h>

/*
 *	Loop a packet back to the sender.
 */
 
static void ip_loopback(struct device *old_dev, struct sk_buff *skb)
{
	struct device *dev=&loopback_dev;
	int len=ntohs(skb->ip_hdr->tot_len);
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
	ip_send(NULL,newskb, skb->ip_hdr->daddr, len, dev, skb->ip_hdr->saddr);
	/*
	 *	Add the rest of the data space.	
	 */
	newskb->ip_hdr=(struct iphdr *)skb_put(newskb, len);
	memcpy(newskb->proto_priv, skb->proto_priv, sizeof(skb->proto_priv));

	/*
	 *	Copy the data
	 */
	memcpy(newskb->ip_hdr,skb->ip_hdr,len);

	/* Recurse. The device check against IFF_LOOPBACK will stop infinite recursion */
		
	/*printk("Loopback output queued [%lX to %lX].\n", newskb->ip_hdr->saddr,newskb->ip_hdr->daddr);*/
	ip_queue_xmit(NULL, dev, newskb, 2);
}



/*
 *	Take an skb, and fill in the MAC header.
 */

int ip_send(struct rtable * rt, struct sk_buff *skb, __u32 daddr, int len, struct device *dev, __u32 saddr)
{
	int mac = 0;

	skb->dev = dev;
	skb->arp = 1;
	skb->protocol = htons(ETH_P_IP);
	if (dev->hard_header)
	{
		/*
		 *	Build a hardware header. Source address is our mac, destination unknown
		 *  	(rebuild header will sort this out)
		 */
		skb_reserve(skb,(dev->hard_header_len+15)&~15);	/* 16 byte aligned IP headers are good */
		if (rt && dev == rt->rt_dev && rt->rt_hh)
		{
			memcpy(skb_push(skb,dev->hard_header_len),rt->rt_hh->hh_data,dev->hard_header_len);
			if (rt->rt_hh->hh_uptodate)
				return dev->hard_header_len;
#if RT_CACHE_DEBUG >= 2
			printk("ip_send: hh miss %08x via %08x\n", daddr, rt->rt_gateway);
#endif
			skb->arp = 0;
			skb->raddr = daddr;
			return dev->hard_header_len;
		}
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

static int ip_send_room(struct rtable * rt, struct sk_buff *skb, __u32 daddr, int len, struct device *dev, __u32 saddr)
{
	int mac = 0;

	skb->dev = dev;
	skb->arp = 1;
	skb->protocol = htons(ETH_P_IP);
	skb_reserve(skb,MAX_HEADER);
	if (dev->hard_header)
	{
		if (rt && dev == rt->rt_dev && rt->rt_hh)
		{
			memcpy(skb_push(skb,dev->hard_header_len),rt->rt_hh->hh_data,dev->hard_header_len);
			if (rt->rt_hh->hh_uptodate)
				return dev->hard_header_len;
#if RT_CACHE_DEBUG >= 2
			printk("ip_send_room: hh miss %08x via %08x\n", daddr, rt->rt_gateway);
#endif
			skb->arp = 0;
			skb->raddr = daddr;
			return dev->hard_header_len;
		}
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
int ip_build_header(struct sk_buff *skb, __u32 saddr, __u32 daddr,
		struct device **dev, int type, struct options *opt,
		int len, int tos, int ttl, struct rtable ** rp)
{
	struct rtable *rt;
	__u32 raddr;
	int tmp;
	struct iphdr *iph;
	__u32 final_daddr = daddr;


	if (opt && opt->srr)
		daddr = opt->faddr;

	/*
	 *	See if we need to look up the device.
	 */

#ifdef CONFIG_IP_MULTICAST	
	if(MULTICAST(daddr) && *dev==NULL && skb->sk && *skb->sk->ip_mc_name)
		*dev=dev_get(skb->sk->ip_mc_name);
#endif
	if (rp)
	{
		rt = ip_check_route(rp, daddr, skb->localroute);
		/*
		 * If rp != NULL rt_put following below should not
		 * release route, so that...
		 */
		if (rt)
			atomic_inc(&rt->rt_refcnt);
	}
	else
		rt = ip_rt_route(daddr, skb->localroute);


	if (*dev == NULL)
	{
		if (rt == NULL)
		{
			ip_statistics.IpOutNoRoutes++;
			return(-ENETUNREACH);
		}

		*dev = rt->rt_dev;
	}

	if ((LOOPBACK(saddr) && !LOOPBACK(daddr)) || !saddr)
		saddr = rt ? rt->rt_src : (*dev)->pa_addr;

	raddr = rt ? rt->rt_gateway : daddr;

	if (opt && opt->is_strictroute && rt && (rt->rt_flags & RTF_GATEWAY))
	{
		ip_rt_put(rt);
		ip_statistics.IpOutNoRoutes++;
		return -ENETUNREACH;
	}

	/*
	 *	Now build the MAC header.
	 */

	if (type==IPPROTO_TCP)
		tmp = ip_send_room(rt, skb, raddr, len, *dev, saddr);
	else
		tmp = ip_send(rt, skb, raddr, len, *dev, saddr);

	ip_rt_put(rt);

	/*
	 *	Book keeping
	 */

	skb->dev = *dev;
	skb->saddr = saddr;
	
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
	 
	if (opt)
		iph=(struct iphdr *)skb_put(skb,sizeof(struct iphdr) + opt->optlen);
	else
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

	if (!opt || !opt->optlen)
		return sizeof(struct iphdr) + tmp;
	iph->ihl += opt->optlen>>2;
	ip_options_build(skb, opt, final_daddr, (*dev)->pa_addr, 0);
	return iph->ihl*4 + tmp;
}


/*
 *	Generate a checksum for an outgoing IP datagram.
 */

void ip_send_check(struct iphdr *iph)
{
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
}


/*
 *	If a sender wishes the packet to remain unfreed
 *	we add it to his send queue. This arguably belongs
 *	in the TCP level since nobody else uses it. BUT
 *	remember IPng might change all the rules.
 */
static inline void add_to_send_queue(struct sock * sk, struct sk_buff * skb)
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
		sk->send_next = skb;
	}
	else
	{
		sk->send_tail->link3 = skb;
		sk->send_tail = skb;
	}
	restore_flags(flags);
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
	unsigned int tot_len;
	struct iphdr *iph;

	IS_SKB(skb);

	/*
	 *	Do some book-keeping in the packet for later
	 */

	skb->sk = sk;
	skb->dev = dev;
	skb->when = jiffies;

	/*
	 *	Find the IP header and set the length. This is bad
	 *	but once we get the skb data handling code in the
	 *	hardware will push its header sensibly and we will
	 *	set skb->ip_hdr to avoid this mess and the fixed
	 *	header length problem
	 */

	iph = skb->ip_hdr;
	tot_len = skb->len - (((unsigned char *)iph) - skb->data);
	iph->tot_len = htons(tot_len);

	switch (free) {
		/* No reassigning numbers to fragments... */
		default:
			free = 1;
			break;
		case 0:
			add_to_send_queue(sk, skb);
			/* fall through */
		case 1:
			iph->id = htons(ip_id_count++);
	}

	skb->free = free;

	/* Sanity check */
	if (dev == NULL)
		goto no_device;

#ifdef CONFIG_FIREWALL
	if (call_out_firewall(PF_INET, skb->dev, iph, NULL) < FW_ACCEPT)
		goto out;
#endif	

	/*
	 *	Do we need to fragment. Again this is inefficient.
	 *	We need to somehow lock the original buffer and use
	 *	bits of it.
	 */

	if (tot_len > dev->mtu)
		goto fragment;

	/*
	 *	Add an IP checksum
	 */

	ip_send_check(iph);

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
	 *	If the indicated interface is up and running, send the packet.
	 */
	 
	ip_statistics.IpOutRequests++;
#ifdef CONFIG_IP_ACCT
	ip_fw_chk(iph,dev,NULL,ip_acct_chain,0,IP_FW_MODE_ACCT_OUT);
#endif	

#ifdef CONFIG_IP_MULTICAST	

	/*
	 *	Multicasts are looped back for other local users
	 */
	 
	if (MULTICAST(iph->daddr) && !(dev->flags&IFF_LOOPBACK))
	{
		if(sk==NULL || sk->ip_mc_loop)
		{
			if(iph->daddr==IGMP_ALL_HOSTS || (dev->flags&IFF_ALLMULTI))
			{
				ip_loopback(dev,skb);
			}
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
		
		if (iph->ttl==0)
			goto out;
	}
#endif
	if ((dev->flags & IFF_BROADCAST) && !(dev->flags & IFF_LOOPBACK)
	    && (iph->daddr==dev->pa_brdaddr || iph->daddr==0xFFFFFFFF))
		ip_loopback(dev,skb);
		
	if (dev->flags & IFF_UP)
	{
		/*
		 *	If we have an owner use its priority setting,
		 *	otherwise use NORMAL
		 */
		int priority = SOPRI_NORMAL;
		if (sk)
			priority = sk->priority;

		dev_queue_xmit(skb, dev, priority);
		return;
	}
	if(sk)
		sk->err = ENETDOWN;
	ip_statistics.IpOutDiscards++;
out:
	if (free)
		kfree_skb(skb, FREE_WRITE);
	return;

no_device:
	NETDEBUG(printk("IP: ip_queue_xmit dev = NULL\n"));
	goto out;

fragment:
	ip_fragment(sk,skb,dev,0);
	goto out;
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
 *	destination address (char *), the offset to copy from, and the
 *	length to be copied.
 * 
 */

int ip_build_xmit(struct sock *sk,
		   void getfrag (const void *,
				 __u32,
				 char *,
				 unsigned int,	
				 unsigned int),
		   const void *frag,
		   unsigned short int length,
		   __u32 daddr,
		   __u32 user_saddr,
		   struct options * opt,
		   int flags,
		   int type,
		   int noblock) 
{
	struct rtable *rt;
	unsigned int fraglen, maxfraglen, fragheaderlen;
	int offset, mf;
	__u32 saddr;
	unsigned short id;
	struct iphdr *iph;
	__u32 raddr;
	struct device *dev = NULL;
	struct hh_cache * hh=NULL;
	int nfrags=0;
	__u32 true_daddr = daddr;

	if (opt && opt->srr && !sk->ip_hdrincl)
	  daddr = opt->faddr;
	
	ip_statistics.IpOutRequests++;

#ifdef CONFIG_IP_MULTICAST	
	if(MULTICAST(daddr) && *sk->ip_mc_name)
	{
		dev=dev_get(sk->ip_mc_name);
		if(!dev)
			return -ENODEV;
		rt=NULL;
		if (sk->saddr && (!LOOPBACK(sk->saddr) || LOOPBACK(daddr)))
			saddr = sk->saddr;
		else
			saddr = dev->pa_addr;
	}
	else
	{
#endif	
		rt = ip_check_route(&sk->ip_route_cache, daddr,
				    sk->localroute || (flags&MSG_DONTROUTE) ||
				    (opt && opt->is_strictroute));
		if (rt == NULL) 
		{
			ip_statistics.IpOutNoRoutes++;
			return(-ENETUNREACH);
		}
		saddr = rt->rt_src;

		hh = rt->rt_hh;
	
		if (sk->saddr && (!LOOPBACK(sk->saddr) || LOOPBACK(daddr)))
			saddr = sk->saddr;
			
		dev=rt->rt_dev;
#ifdef CONFIG_IP_MULTICAST
	}
	if (rt && !dev)
		dev = rt->rt_dev;
#endif		
	if (user_saddr)
		saddr = user_saddr;

	raddr = rt ? rt->rt_gateway : daddr;
	/*
	 *	Now compute the buffer space we require
	 */ 
	 
	/*
	 *	Try the simple case first. This leaves broadcast, multicast, fragmented frames, and by
	 *	choice RAW frames within 20 bytes of maximum size(rare) to the long path
	 */

	if (!sk->ip_hdrincl) {
		length += sizeof(struct iphdr);
		if(opt) length += opt->optlen;
	}

	if(length <= dev->mtu && !MULTICAST(daddr) && daddr!=0xFFFFFFFF && daddr!=dev->pa_brdaddr)
	{	
		int error;
		struct sk_buff *skb=sock_alloc_send_skb(sk, length+15+dev->hard_header_len,0, noblock, &error);
		if(skb==NULL)
		{
			ip_statistics.IpOutDiscards++;
			return error;
		}
		skb->dev=dev;
		skb->protocol = htons(ETH_P_IP);
		skb->free=1;
		skb->when=jiffies;
		skb->sk=sk;
		skb->arp=0;
		skb->saddr=saddr;
		skb->raddr = raddr;
		skb_reserve(skb,(dev->hard_header_len+15)&~15);
		if (hh)
		{
			skb->arp=1;
			memcpy(skb_push(skb,dev->hard_header_len),hh->hh_data,dev->hard_header_len);
			if (!hh->hh_uptodate)
			{
				skb->arp = 0;
#if RT_CACHE_DEBUG >= 2
				printk("ip_build_xmit: hh miss %08x via %08x\n", rt->rt_dst, rt->rt_gateway);
#endif				
			}
		}
		else if(dev->hard_header)
		{
			if(dev->hard_header(skb,dev,ETH_P_IP,NULL,NULL,0)>0)
				skb->arp=1;
		}
		else
			skb->arp=1;
		skb->ip_hdr=iph=(struct iphdr *)skb_put(skb,length);
		dev_lock_list();
		if(!sk->ip_hdrincl)
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
			if (opt) 
			{
				iph->ihl += opt->optlen>>2;
				ip_options_build(skb, opt,
						 true_daddr, dev->pa_addr, 0);
			}
			iph->check=0;
			iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
			getfrag(frag,saddr,((char *)iph)+iph->ihl*4,0, length-iph->ihl*4);
		}
		else
			getfrag(frag,saddr,(void *)iph,0,length);
		dev_unlock_list();
#ifdef CONFIG_FIREWALL
		if(call_out_firewall(PF_INET, skb->dev, iph, NULL)< FW_ACCEPT)
		{
			kfree_skb(skb, FREE_WRITE);
			return -EPERM;
		}
#endif
#ifdef CONFIG_IP_ACCT
		ip_fw_chk(iph,dev,NULL,ip_acct_chain,0,IP_FW_MODE_ACCT_OUT);
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
	if (!sk->ip_hdrincl)
		length -= sizeof(struct iphdr);
		
	if(opt) 
	{
		length -= opt->optlen;
		fragheaderlen = dev->hard_header_len + sizeof(struct iphdr) + opt->optlen;
		maxfraglen = ((dev->mtu-sizeof(struct iphdr)-opt->optlen) & ~7) + fragheaderlen;
	}
	else 
	{
		fragheaderlen = dev->hard_header_len;
		if(!sk->ip_hdrincl)
			fragheaderlen += 20;
		
		/*
		 *	Fragheaderlen is the size of 'overhead' on each buffer. Now work
		 *	out the size of the frames to send.
		 */
	 
		maxfraglen = ((dev->mtu-20) & ~7) + fragheaderlen;
        }
	
	/*
	 *	Start at the end of the frame by handling the remainder.
	 */
	 
	offset = length - (length % (maxfraglen - fragheaderlen));
	
	/*
	 *	Amount of memory to allocate for final fragment.
	 */
	 
	fraglen = length - offset + fragheaderlen;
	
	if(length-offset==0)
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
	 
	if (sk->ip_hdrincl && offset > 0)
 		return(-EMSGSIZE);

	/*
	 *	Lock the device lists.
	 */

	dev_lock_list();
	
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

		skb = sock_alloc_send_skb(sk, fraglen+15, 0, noblock, &error);
		if (skb == NULL)
		{
			ip_statistics.IpOutDiscards++;
			if(nfrags>1)
				ip_statistics.IpFragCreates++;			
			dev_unlock_list();
			return(error);
		}
		
		/*
		 *	Fill in the control structures
		 */
		 
		skb->dev = dev;
		skb->protocol = htons(ETH_P_IP);
		skb->when = jiffies;
		skb->free = 1; /* dubious, this one */
		skb->sk = sk;
		skb->arp = 0;
		skb->saddr = saddr;
		skb->daddr = daddr;
		skb->raddr = raddr;
		skb_reserve(skb,(dev->hard_header_len+15)&~15);
		data = skb_put(skb, fraglen-dev->hard_header_len);

		/*
		 *	Save us ARP and stuff. In the optimal case we do no route lookup (route cache ok)
		 *	no ARP lookup (arp cache ok) and output. The cache checks are still too slow but
		 *	this can be fixed later. For gateway routes we ought to have a rt->.. header cache
		 *	pointer to speed header cache builds for identical targets.
		 */
		 
		if (hh)
		{
			skb->arp=1;
			memcpy(skb_push(skb,dev->hard_header_len),hh->hh_data,dev->hard_header_len);
			if (!hh->hh_uptodate)
			{
				skb->arp = 0;
#if RT_CACHE_DEBUG >= 2
				printk("ip_build_xmit: hh miss %08x via %08x\n", rt->rt_dst, rt->rt_gateway);
#endif				
			}
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
		 
		if(!sk->ip_hdrincl) 
		{

			iph->version = 4;
			iph->ihl = 5; /* ugh */
			if (opt) {
				iph->ihl += opt->optlen>>2;
				ip_options_build(skb, opt,
						 true_daddr, dev->pa_addr, offset);
			}
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
		 
#ifdef CONFIG_FIREWALL
		if(!offset && call_out_firewall(PF_INET, skb->dev, iph, NULL) < FW_ACCEPT)
		{
			kfree_skb(skb, FREE_WRITE);
			dev_unlock_list();
			return -EPERM;
		}
#endif		
#ifdef CONFIG_IP_ACCT
		if(!offset)
			ip_fw_chk(iph, dev, NULL, ip_acct_chain, 0, IP_FW_MODE_ACCT_OUT);
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
			 *
			 *	Always loop back all host messages when running as a multicast router.
			 */
			 
			if(sk==NULL || sk->ip_mc_loop)
			{
				if(daddr==IGMP_ALL_HOSTS || (dev->flags&IFF_ALLMULTI))
					ip_loopback(dev,skb);
				else 
				{
					struct ip_mc_list *imc=dev->ip_mc_list;
					while(imc!=NULL) 
					{
						if(imc->multiaddr==daddr) 
						{
							ip_loopback(dev,skb);
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
			{
				kfree_skb(skb, FREE_WRITE);
				nfrags++;
				continue;
			}
		}
#endif

		nfrags++;
		
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
			 */
			 
			ip_statistics.IpOutDiscards++;
			if(nfrags>1)
				ip_statistics.IpFragCreates+=nfrags;
			kfree_skb(skb, FREE_WRITE);
			dev_unlock_list();
			/*
			 *	BSD behaviour.
			 */
			if(sk!=NULL)
				sk->err=ENETDOWN;
			return(0); /* lose rest of fragments */
		}
	} 
	while (offset >= 0);
	if(nfrags>1)
		ip_statistics.IpFragCreates+=nfrags;
	dev_unlock_list();
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

#ifdef CONFIG_RTNETLINK

/*
 *	Netlink hooks for IP
 */
 
void ip_netlink_msg(unsigned long msg, __u32 daddr, __u32 gw, __u32 mask, short flags, short metric, char *name)
{
	struct sk_buff *skb=alloc_skb(sizeof(struct netlink_rtinfo), GFP_ATOMIC);
	struct netlink_rtinfo *nrt;
	struct sockaddr_in *s;
	if(skb==NULL)
		return;
	skb->free=1;
	nrt=(struct netlink_rtinfo *)skb_put(skb, sizeof(struct netlink_rtinfo));
	nrt->rtmsg_type=msg;
	s=(struct sockaddr_in *)&nrt->rtmsg_dst;
	s->sin_family=AF_INET;
	s->sin_addr.s_addr=daddr;
	s=(struct sockaddr_in *)&nrt->rtmsg_gateway;
	s->sin_family=AF_INET;
	s->sin_addr.s_addr=gw;
	s=(struct sockaddr_in *)&nrt->rtmsg_genmask;
	s->sin_family=AF_INET;
	s->sin_addr.s_addr=mask;
	nrt->rtmsg_flags=flags;
	nrt->rtmsg_metric=metric;
	strcpy(nrt->rtmsg_device,name);
	if (netlink_post(NETLINK_ROUTE, skb))
		kfree_skb(skb, FREE_WRITE);
}	

#endif

/*
 *	Device notifier
 */
 
static int ip_rt_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev=ptr;
	if(event==NETDEV_DOWN)
	{
		ip_netlink_msg(RTMSG_DELDEVICE, 0,0,0,0,0,dev->name);
		ip_rt_flush(dev);
	}
/*
 *	Join the initial group if multicast.
 */		
	if(event==NETDEV_UP)
	{
#ifdef CONFIG_IP_MULTICAST	
		ip_mc_allhost(dev);
#endif		
		ip_netlink_msg(RTMSG_NEWDEVICE, 0,0,0,0,0,dev->name);
		ip_rt_update(NETDEV_UP, dev);
	}
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

#ifdef CONFIG_IP_MULTICAST
#ifdef CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IGMP, 4, "igmp",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_mc_procinfo
	});
#endif	
#endif
}

