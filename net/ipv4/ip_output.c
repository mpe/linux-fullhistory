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
	ip_send(newskb, skb->ip_hdr->daddr, len, dev, skb->ip_hdr->saddr);
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
	ip_queue_xmit(NULL, dev, newskb, 1);
}



/*
 *	Take an skb, and fill in the MAC header.
 */

int ip_send(struct sk_buff *skb, __u32 daddr, int len, struct device *dev, __u32 saddr)
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

static int ip_send_room(struct sk_buff *skb, __u32 daddr, int len, struct device *dev, __u32 saddr)
{
	int mac = 0;

	skb->dev = dev;
	skb->arp = 1;
	if (dev->hard_header)
	{
		skb_reserve(skb,MAX_HEADER);
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
		struct device **dev, int type, struct options *opt, int len, int tos, int ttl)
{
	struct rtable *rt;
	__u32 raddr;
	int tmp;
	__u32 src;
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

	if(type==IPPROTO_TCP)
		tmp = ip_send_room(skb, raddr, len, *dev, saddr);
	else
		tmp = ip_send(skb, raddr, len, *dev, saddr);

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
	if (opt->is_strictroute && rt && rt->rt_gateway) 
	{
		ip_statistics.IpOutNoRoutes++;
		return -ENETUNREACH;
	}
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
/*	unsigned char *ptr;*/

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

	iph = skb->ip_hdr;
	iph->tot_len = ntohs(skb->len-(((unsigned char *)iph)-skb->data));

#ifdef CONFIG_FIREWALL
	if(call_out_firewall(PF_INET, skb, iph) < FW_ACCEPT)
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

	if(ntohs(iph->tot_len)> dev->mtu)
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
		if(sk)
			sk->err = ENETDOWN;
		ip_statistics.IpOutDiscards++;
		if (free)
			kfree_skb(skb, FREE_WRITE);
	}
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
		   int type) 
{
	struct rtable *rt;
	unsigned int fraglen, maxfraglen, fragheaderlen;
	int offset, mf;
	__u32 saddr;
	unsigned short id;
	struct iphdr *iph;
	int local=0;
	struct device *dev;
	int nfrags=0;
	__u32 true_daddr = daddr;

	if (opt && opt->srr && !sk->ip_hdrincl)
	  daddr = opt->faddr;
	
	ip_statistics.IpOutRequests++;

#ifdef CONFIG_IP_MULTICAST	
	if(sk && MULTICAST(daddr) && *sk->ip_mc_name)
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
		if(!rt || sk->ip_route_stamp != rt_stamp ||
		   daddr!=sk->ip_route_daddr || sk->ip_route_local!=local ||
		   (sk->saddr && sk->saddr != saddr))
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
#ifdef CONFIG_IP_MULTICAST
	}
#endif		
	if (user_saddr)
		saddr = user_saddr;

	/*
	 *	Now compute the buffer space we require
	 */ 
	 
	/*
	 *	Try the simple case first. This leaves broadcast, multicast, fragmented frames, and by
	 *	choice RAW frames within 20 bytes of maximum size(rare) to the long path
	 */

	length += 20;
	if (!sk->ip_hdrincl && opt) 
	{
		length += opt->optlen;
		if (opt->is_strictroute && rt && rt->rt_gateway) 
		{
			ip_statistics.IpOutNoRoutes++;
			return -ENETUNREACH;
		}
	}
	if(length <= dev->mtu && !MULTICAST(daddr) && daddr!=0xFFFFFFFF && daddr!=dev->pa_brdaddr)
	{	
		int error;
		struct sk_buff *skb=sock_alloc_send_skb(sk, length+15+dev->hard_header_len,0, 0,&error);
		if(skb==NULL)
		{
			ip_statistics.IpOutDiscards++;
			return error;
		}
		skb->dev=dev;
		skb->free=1;
		skb->when=jiffies;
		skb->sk=sk;
		skb->arp=0;
		skb->saddr=saddr;
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
			getfrag(frag,saddr,(void *)iph,0,length-20);
		dev_unlock_list();
#ifdef CONFIG_FIREWALL
		if(call_out_firewall(PF_INET, skb, iph)< FW_ACCEPT)
		{
			kfree_skb(skb, FREE_WRITE);
			return -EPERM;
		}
#endif
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
	length-=20;
	if (sk && !sk->ip_hdrincl && opt) 
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

		skb = sock_alloc_send_skb(sk, fraglen+15, 0, 0, &error);
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
		if(!offset && call_out_firewall(PF_INET, skb, iph) < FW_ACCEPT)
		{
			kfree_skb(skb, FREE_WRITE);
			dev_unlock_list();
			return -EPERM;
		}
#endif		
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
			 *
			 *	Always loop back all host messages when running as a multicast router.
			 */
			 
			if(sk==NULL || sk->ip_mc_loop)
			{
				if(skb->daddr==IGMP_ALL_HOSTS || (dev->flags&IFF_ALLMULTI))
					ip_loopback(rt?rt->rt_dev:dev,skb);
				else 
				{
					struct ip_mc_list *imc=rt?rt->rt_dev->ip_mc_list:dev->ip_mc_list;
					while(imc!=NULL) 
					{
						if(imc->multiaddr==daddr) 
						{
							ip_loopback(rt?rt->rt_dev:dev,skb);
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
	netlink_post(NETLINK_ROUTE, skb);
}	

#endif

/*
 *	Device notifier
 */
 
static int ip_rt_event(unsigned long event, void *ptr)
{
	struct device *dev=ptr;
	if(event==NETDEV_DOWN)
	{
		ip_netlink_msg(RTMSG_DELDEVICE, 0,0,0,0,0,dev->name);
		ip_rt_flush(dev);
	}
/*
 *	Join the intial group if multicast.
 */		
	if(event==NETDEV_UP)
	{
#ifdef CONFIG_IP_MULTICAST	
		ip_mc_allhost(dev);
#endif		
		ip_netlink_msg(RTMSG_NEWDEVICE, 0,0,0,0,0,dev->name);
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
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IGMP, 4, "igmp",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_mc_procinfo
	});
#endif
}

