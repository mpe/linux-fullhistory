/*
 *	IPv6 output functions
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Based on linux/net/ipv4/ip_output.c
 *
 *	$Id: ipv6_output.c,v 1.19 1996/10/16 18:34:16 roque Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Andi Kleen		:	exception handling
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/in6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>

static u32	ipv6_fragmentation_id = 1;
int		ipv6_forwarding = 0;		/* default: host */

static int __inline__ ipv6_build_mac_header(struct sk_buff *skb,
					    struct device *dev,
					    struct neighbour *neigh, 
					    int len)
{
	int mac;
	int hdrlen = 0;

	skb->arp = 1;
	skb->nexthop = neigh;

	skb_reserve(skb, (dev->hard_header_len + 15) & ~15);

	if (dev->hard_header_len)
	{

		/*
		 *	FIXME: use cached hardware header if availiable
		 */
		if (dev->hard_header)
		{
			mac = dev->hard_header(skb, dev, ETH_P_IPV6, 
					       NULL, NULL, len);
		
			if (mac < 0)
			{				
				hdrlen = -mac;
				skb->arp = 0;
			}
			else
			{				
				hdrlen = mac;
			}
		}
		else
			hdrlen = dev->hard_header_len;
	}

	skb->mac.raw = skb->data;
	return hdrlen;
}

void ipv6_redo_mac_hdr(struct sk_buff *skb, struct neighbour *neigh, int len)
{
	struct device *dev = neigh->dev;
	int mac;
	
	skb->dev = dev;
	skb->nexthop = neigh;
	skb->arp = 1;

	skb_pull(skb, (unsigned char *) skb->nh.ipv6h - skb->data);

	/*
	 *	neighbour cache should have the ether address
	 *	cached... use it
	 */ 

	if (dev->hard_header)
	{
		/*
		 *	FIXME: use cached hardware header if availiable
		 */

		mac = dev->hard_header(skb, dev, ETH_P_IPV6, 
				       NULL, NULL, len);
		
		if (mac < 0)
		{				
			skb->arp = 0;
		}

	}
	skb->mac.raw = skb->data;
}

void default_output_method(struct sk_buff *skb, struct rt6_info *rt)
{
	struct sock *sk = skb->sk;
	struct device *dev = skb->dev;

	if (dev->flags & IFF_UP)
	{
		/*
		 *	If we have an owner use its priority setting,
		 *	otherwise use NORMAL
		 */

		dev_queue_xmit(skb);
	}
	else
	{
		if(sk)
			sk->err = ENETDOWN;

		ipv6_statistics.Ip6OutDiscards++;
		
		kfree_skb(skb, FREE_WRITE);
	}
}

/*
 *	xmit an sk_buff (used by TCP)
 *	sk can be NULL (for sending RESETs)
 */
int ipv6_xmit(struct sock *sk, struct sk_buff *skb, struct in6_addr *saddr,
	      struct in6_addr *daddr, struct ipv6_options *opt, int proto)
{
	struct ipv6hdr *hdr;
	struct dest_entry *dc;
	struct ipv6_pinfo *np = NULL;
	struct device *dev = skb->dev;
	int seg_len;
	int addr_type;
	int rt_flags = 0;


	addr_type = ipv6_addr_type(daddr);

	if (addr_type & (IPV6_ADDR_LINKLOCAL|IPV6_ADDR_SITELOCAL))
	{
		/*
		 *	force device match on route lookup
		 */
		
		rt_flags |= RTI_DEVRT;
	}

	if (sk && sk->localroute)
		rt_flags |= RTI_GATEWAY;

	hdr = skb->nh.ipv6h;
	

	if (sk)
	{
		np = &sk->net_pinfo.af_inet6;
	}
	
	if (np && np->dest)
	{
		dc = ipv6_dst_check(np->dest, daddr, np->dc_sernum, rt_flags);
	}
	else
	{
		dc = ipv6_dst_route(daddr, dev, rt_flags);
	}

	if (dc == NULL)
	{
		ipv6_statistics.Ip6OutNoRoutes++;
		return(-ENETUNREACH);
	}

	dev = dc->rt.rt_dev;
	
	if (saddr == NULL)
	{
		struct inet6_ifaddr *ifa;
		
		ifa = ipv6_get_saddr((struct rt6_info *) dc, daddr);
		
		if (ifa == NULL)
		{
			printk(KERN_DEBUG 
			       "ipv6_xmit: get_saddr failed\n");
			return -ENETUNREACH;
		}
		
		saddr = &ifa->addr;
		
		if (np)
		{
			ipv6_addr_copy(&np->saddr, saddr);
		}
	}

	seg_len = skb->tail - ((unsigned char *) hdr);

	/*
	 *	Link Layer headers
	 */

	skb->protocol = __constant_htons(ETH_P_IPV6);
	skb->dev = dev;
	
	ipv6_redo_mac_hdr(skb, dc->dc_nexthop, seg_len);
	
	/*
	 *	Fill in the IPv6 header
	 */

	hdr->version = 6;
	hdr->priority = np ? np->priority : 0;

	if (np)
		memcpy(hdr->flow_lbl, (void *) &np->flow_lbl, 3);
	else
		memset(hdr->flow_lbl, 0, 3);

	hdr->payload_len = htons(seg_len - sizeof(struct ipv6hdr));
	hdr->nexthdr = proto;
	hdr->hop_limit = np ? np->hop_limit : ipv6_hop_limit;
	
	memcpy(&hdr->saddr, saddr, sizeof(struct in6_addr));
	memcpy(&hdr->daddr, daddr, sizeof(struct in6_addr));

	
	/*
	 *	Options
	 */


	/*
	 *	Output the packet
	 */

	ipv6_statistics.Ip6OutRequests++;

	if (dc->rt.rt_output_method)
	{
		(*dc->rt.rt_output_method)(skb, (struct rt6_info *) dc);
	}
	else
		default_output_method(skb, (struct rt6_info *) dc);

	/*
	 *	Update serial number of cached dest_entry or
	 *	release destination cache entry
	 */
	
	if (np)
	{
		np->dest = dc;
		if (dc->rt.fib_node)
		{
			np->dc_sernum = dc->rt.fib_node->fn_sernum;
		}
	}
	else
	{
		ipv6_dst_unlock(dc);
	}

	return 0;
}

/*
 *	To avoid extra problems ND packets are send through this
 *	routine. It's code duplication but i really want to avoid
 *	extra checks since ipv6_build_header is used by TCP (which
 *	is for us performace critical)
 */

int ipv6_bld_hdr_2(struct sock *sk, struct sk_buff *skb, struct device *dev,
		   struct neighbour *neigh,
		   struct in6_addr *saddr, struct in6_addr *daddr,
		   int proto, int len)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6hdr *hdr;
	int hdrlen = 0;

	skb->dev = dev;

	/* build MAC header */
	hdrlen += ipv6_build_mac_header(skb, dev, neigh, len);

	/* build fixed IPv6 header */

	if (proto == IPPROTO_RAW)
		return hdrlen;

	
	hdr = (struct ipv6hdr *) skb_put(skb, sizeof(struct ipv6hdr));
	skb->nh.ipv6h = hdr;
	
	hdr->version  = 6;
	hdr->priority = np->priority & 0x0f;

	memset(hdr->flow_lbl, 0, 3);

	hdr->hop_limit =  np->hop_limit;

	if (saddr == NULL)
	{
		printk(KERN_DEBUG "bug: bld_hdr called with no saddr\n");
		return -ENETUNREACH;
	}

	memcpy(&hdr->saddr, saddr, sizeof(struct in6_addr));
	memcpy(&hdr->daddr, daddr, sizeof(struct in6_addr));
	
	hdrlen += sizeof(struct ipv6hdr);

	hdr->nexthdr = proto;

	return hdrlen;
}

void ipv6_queue_xmit(struct sock *sk, struct device *dev, struct sk_buff *skb,
		     int free)
{
	struct ipv6hdr *hdr;
	u32 seg_len;

	hdr = skb->nh.ipv6h;
	skb->protocol = __constant_htons(ETH_P_IPV6);

	seg_len = skb->tail - ((unsigned char *) hdr);

	hdr->payload_len = htons(seg_len - sizeof(struct ipv6hdr));

	if (dev == NULL)
	{
		printk(KERN_DEBUG "ipv6_queue_xmit: unknown device\n");
		return;
	}

	skb->dev = dev;
	
	ipv6_statistics.Ip6OutRequests++;


	/*
	 *	Multicast loopback
	 */

	if (dev->flags & IFF_UP)
	{
		/*
		 *	If we have an owner use its priority setting,
		 *	otherwise use NORMAL
		 */

		dev_queue_xmit(skb);
	}
	else
	{
		if(sk)
			sk->err = ENETDOWN;

		ipv6_statistics.Ip6OutDiscards++;

		kfree_skb(skb, FREE_WRITE);
	}

}


int ipv6_build_xmit(struct sock *sk, inet_getfrag_t getfrag, const void *data,
		    struct in6_addr *dest, unsigned short int length,
		    struct in6_addr *saddr, struct device *dev,
		    struct ipv6_options *opt, int proto, int hlimit,
		    int noblock)
{
	rt6_output_method_t output_method = default_output_method;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct dest_entry *dc = NULL;
	struct in6_addr *daddr = dest;
	struct ipv6hdr *hdr;
	struct neighbour *neigh;	
	int	addr_type;
	int	pktlength;
	int	pmtu = 0;
	int	rt_flags = 0;
	int	error; 
	
	if (opt && opt->srcrt)
	{
		struct rt0_hdr *rt0 = (struct rt0_hdr *) opt->srcrt;
		daddr = rt0->addr;
	}

	addr_type = ipv6_addr_type(daddr);
	
	if (hlimit < 1)
	{
		if (addr_type & IPV6_ADDR_MULTICAST)
		{
			hlimit = np->mcast_hops;
			if (dev == NULL)
			{
				dev = np->mc_if;
			}
		}
		else
			hlimit = np->hop_limit;
	}
		
	if (addr_type & (IPV6_ADDR_LINKLOCAL | IPV6_ADDR_SITELOCAL |
			 IPV6_ADDR_MULTICAST))
	{
		/*
		 *	force device match on route lookup
		 */
		
		rt_flags |= RTI_DEVRT;
	}

	if (sk->localroute)
	{
		rt_flags |= RTI_GATEWAY;
	}

	if (np->dest)
	{
		np->dest = ipv6_dst_check(np->dest, daddr, np->dc_sernum,
					  rt_flags);
		
		dc = np->dest;

		if (dc && dc->rt.fib_node)
		{
			np->dc_sernum = dc->rt.fib_node->fn_sernum;
		}
		else
		{
			printk(KERN_WARNING "dc entry not in table\n");
		}
	}
	else
	{
		dc = ipv6_dst_route(daddr, dev, rt_flags);
	}

	if (dc == NULL)
	{
		if ((addr_type & IPV6_ADDR_MULTICAST) && dev)
		{
			neigh = NULL;
			pmtu = dev->mtu;
		}
		else
		{
			ipv6_statistics.Ip6OutNoRoutes++;
			return(-ENETUNREACH);
		}
	}
	else
	{
		neigh = dc->dc_nexthop;
		dev = neigh->dev;

		if (dc->rt.rt_output_method)
		{
			output_method = dc->rt.rt_output_method;
		}

		if (dc->dc_flags & DCF_PMTU)
			pmtu = dc->dc_pmtu;
		else
			pmtu = dev->mtu;
	}


	if (saddr == NULL)
	{
		struct inet6_ifaddr *ifa;

		ifa = ipv6_get_saddr((struct rt6_info *) dc, daddr);

		if (ifa == NULL)
		{
			printk(KERN_DEBUG 
			       "ipv6_build_xmit: get_saddr failed\n");
			return -ENETUNREACH;
		}

		saddr = &ifa->addr;
	}

	if (dc && np->dest == NULL)
	{
		ipv6_dst_unlock(dc);
	}

	pktlength = length;

	if (!sk->ip_hdrincl)
	{ 
		pktlength += sizeof(struct ipv6hdr);
		if (opt)
		{
			pktlength += opt->opt_flen + opt->opt_nflen;
		}
	}
		

	dev_lock_list();

	/*
	 *	reminder: don't allow fragmentation for IPPROTO_RAW
	 */


	if (pktlength <= pmtu) 
	{
		struct sk_buff *skb =
			sock_alloc_send_skb(sk, pktlength+15+
					    dev->hard_header_len,
					    0, noblock, &error);
		
		if (skb == NULL)
		{
			ipv6_statistics.Ip6OutDiscards++;
			dev_unlock_list();
			return error;

		}

		skb->dev=dev;
		skb->protocol = htons(ETH_P_IPV6);
		skb->when=jiffies;
		skb->arp=0;

		/* build the mac header... */
		ipv6_build_mac_header(skb, dev, neigh, pktlength);

		hdr = (struct ipv6hdr *) skb->tail;
		skb->nh.ipv6h = hdr;

		if (!sk->ip_hdrincl)
		{
			skb_put(skb, sizeof(struct ipv6hdr));

			hdr->version = 6;
			hdr->priority = np->priority;

			memcpy(hdr->flow_lbl, &np->flow_lbl, 3);

			hdr->payload_len = htons(pktlength - 
						 sizeof(struct ipv6hdr));
			
			hdr->hop_limit = hlimit;

			memcpy(&hdr->saddr, saddr, sizeof(struct in6_addr));
			memcpy(&hdr->daddr, daddr, sizeof(struct in6_addr));

			if (opt && opt->srcrt)
			{
				hdr->nexthdr = ipv6opt_bld_rthdr(skb, opt,
								 dest, proto);
								 
			}
			else
				hdr->nexthdr = proto;
		}
			
		skb_put(skb, length);
		error = getfrag(data, &hdr->saddr,
				((char *) hdr) + (pktlength - length),
				0, length);
			
		if (!error) 
		{
			ipv6_statistics.Ip6OutRequests++;
			(*output_method)(skb, (struct rt6_info *) dc);
		} else
		{
			error = -EFAULT;
			kfree_skb(skb, FREE_WRITE);			
		}

		dev_unlock_list();
		return error;
	}
	else
	{
		/*
		 *	Fragmentation
		 */

		/*
		 *	Extension header order:
		 *	Hop-by-hop -> Routing -> Fragment -> rest (...)
		 *	
		 *	We must build the non-fragmented part that
		 *	will be in every packet... this also means
		 *	that other extension headers (Dest, Auth, etc)
		 *	must be considered in the data to be fragmented
		 */

		struct sk_buff  *last_skb;
		struct frag_hdr *fhdr;
		int unfrag_len;
		int payl_len;
		int frag_len;
		int last_len;
		int nfrags;
		int err;
		int fhdr_dist;
		__u32 id;

		if (sk->ip_hdrincl)
		{
			return -EMSGSIZE;
		}

		id = ipv6_fragmentation_id++;

		unfrag_len = sizeof(struct ipv6hdr) + sizeof(struct frag_hdr);
		payl_len = length;

		if (opt)
		{
			unfrag_len += opt->opt_nflen;
			payl_len += opt->opt_flen;
		}

		nfrags = payl_len / ((pmtu - unfrag_len) & ~0x7);

		/* 
		 * Length of fragmented part on every packet but 
		 * the last must be an:
		 * "integer multiple of 8 octects".
		 */

		frag_len = (pmtu - unfrag_len) & ~0x7;

		/*
		 *	We must send from end to start because of 
		 *	UDP/ICMP checksums. We do a funny trick:
		 *	fill the last skb first with the fixed
		 *	header (and its data) and then use it
		 *	to create the following segments and send it
		 *	in the end. If the peer is checking the M_flag
		 *	to trigger the reassembly code then this 
		 *	might be a good idea.
		 */

		last_len = payl_len - (nfrags * frag_len);

		if (last_len == 0)
		{
			last_len = frag_len;
			nfrags--;
		}
		
		last_skb = sock_alloc_send_skb(sk, unfrag_len + frag_len +
					       dev->hard_header_len + 15,
					       0, noblock, &err);

		if (last_skb == NULL)
		{
			dev_unlock_list();
			return err;
		}

		last_skb->dev=dev;
		last_skb->protocol = htons(ETH_P_IPV6);
		last_skb->when=jiffies;
		last_skb->arp=0;

		/* 
		 * build the mac header... 
		 */
		ipv6_build_mac_header(last_skb, dev, neigh,
				      unfrag_len + frag_len);

		hdr = (struct ipv6hdr *) skb_put(last_skb, 
						 sizeof(struct ipv6hdr));
		last_skb->nh.ipv6h = hdr;

		hdr->version = 6;
		hdr->priority = np->priority;

		memcpy(hdr->flow_lbl, &np->flow_lbl, 3);
		hdr->payload_len = htons(unfrag_len + frag_len - 
					 sizeof(struct ipv6hdr));
		
		hdr->hop_limit = hlimit;

		hdr->nexthdr = NEXTHDR_FRAGMENT;

		memcpy(&hdr->saddr, saddr, sizeof(struct in6_addr));
		memcpy(&hdr->daddr, daddr, sizeof(struct in6_addr));
		
		if (opt && opt->srcrt)
		{
			hdr->nexthdr = ipv6opt_bld_rthdr(last_skb, opt, dest,
							 NEXTHDR_FRAGMENT);
		}
	
		fhdr = (struct frag_hdr *)
			skb_put(last_skb, sizeof(struct frag_hdr));

		memset(fhdr, 0, sizeof(struct frag_hdr));

		fhdr->nexthdr  = proto;		
		fhdr->frag_off = ntohs(nfrags * frag_len);
		fhdr->identification = id;

		fhdr_dist = (unsigned char *) fhdr - last_skb->data;

		error = getfrag(data, &hdr->saddr, last_skb->tail,
				nfrags * frag_len, last_len);
		
		if (!error)
		{	
			while (nfrags--)
			{
				struct sk_buff *skb;
			
				struct frag_hdr *fhdr2;
				
				printk(KERN_DEBUG "sending frag %d\n", nfrags);
				skb = skb_copy(last_skb, sk->allocation);
				
				fhdr2 = (struct frag_hdr *)
					(skb->data + fhdr_dist);
				
				/* more flag on */
				fhdr2->frag_off = ntohs(nfrags * frag_len + 1);
				
				/*
				 *	FIXME:
				 *	if (nfrags == 0)
				 *	put rest of headers
				 */
				
				error = getfrag(data, &hdr->saddr,
						skb_put(skb, frag_len), 
						nfrags * frag_len, frag_len);
			
				if (error)
				{
					kfree_skb(skb, FREE_WRITE);
					break;
				}

				ipv6_statistics.Ip6OutRequests++;
				(*output_method)(skb, (struct rt6_info *) dc);
			}
		}

		if (error)
		{
			kfree_skb(last_skb, FREE_WRITE);
			dev_unlock_list();
			return -EFAULT;
		}

		printk(KERN_DEBUG "sending last frag \n");

		hdr->payload_len = htons(unfrag_len + last_len - 
					 sizeof(struct ipv6hdr));

		/*
		 *	update last_skb to reflect the getfrag we did
		 *	on start.
		 */
		last_skb->tail += last_len;
		last_skb->len += last_len;

		/* 
		 * toss the mac header out and rebuild it.
		 * needed because of the different frame length.
		 * ie: not needed for an ethernet.
		 */

		if (dev->type != ARPHRD_ETHER && last_len != frag_len)
		{
			ipv6_redo_mac_hdr(last_skb, neigh,
					  unfrag_len + last_len);
		}

		ipv6_statistics.Ip6OutRequests++;
		(*output_method)(last_skb, (struct rt6_info *) dc);

		dev_unlock_list();
		return 0;
	}
	return -1;
}

static int pri_values[4] =
{
	SOPRI_BACKGROUND,
	SOPRI_NORMAL,
	SOPRI_NORMAL,
	SOPRI_INTERACTIVE
};

void ipv6_forward(struct sk_buff *skb, struct device *dev, int flags)
{
	struct neighbour *neigh;
	struct dest_entry *dest;
	int priority;
	int rt_flags;
	int size;
	int pmtu;

	if (skb->nh.ipv6h->hop_limit <= 1)
	{
		icmpv6_send(skb, ICMPV6_TIME_EXCEEDED, ICMPV6_EXC_HOPLIMIT,
			    0, dev);

		kfree_skb(skb, FREE_READ);
		return;
	}

	skb->nh.ipv6h->hop_limit--;

	if (ipv6_addr_type(&skb->nh.ipv6h->saddr) & IPV6_ADDR_LINKLOCAL)
	{
		printk(KERN_DEBUG "ipv6_forward: link local source addr\n");
		icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_NOT_NEIGHBOUR,
			    0, dev);		
		kfree_skb(skb, FREE_READ);
		return;
	}

	rt_flags = RTF_MODIFIED;

	if ((flags & IP6_FW_STRICT))
	{
		rt_flags |= RTF_GATEWAY;
	}

	dest = ipv6_dst_route(&skb->nh.ipv6h->daddr, NULL, rt_flags);

	if (dest == NULL)
	{
		int code;

		if (flags & IP6_FW_STRICT)
			code = ICMPV6_NOT_NEIGHBOUR;
		else
			code = ICMPV6_NOROUTE;
			
		icmpv6_send(skb, ICMPV6_DEST_UNREACH, code, 0, dev);
			    
		kfree_skb(skb, FREE_READ);
		return;
	}

	neigh = dest->dc_nexthop;

	if (neigh->dev == dev && (dev->flags & IFF_MULTICAST) &&
	    !(flags & IP6_FW_SRCRT))
	{
		struct in6_addr *target = NULL;
		struct nd_neigh *ndn = (struct nd_neigh *) neigh;
		
		/* 
		 *	outgoing device equal to incoming device
		 *	send a redirect
		 */
		
		if ((dest->dc_flags & RTF_GATEWAY))
		{
			target = &ndn->ndn_addr;
		}
		else
		{
			target = &skb->nh.ipv6h->daddr;
		}

		ndisc_send_redirect(skb, neigh, target);
	}

	pmtu = neigh->dev->mtu;

	size = sizeof(struct ipv6hdr) + ntohs(skb->nh.ipv6h->payload_len);
	
	if (size > pmtu)
	{
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, pmtu, dev);
		kfree_skb(skb, FREE_READ);
		return;
	}

	ipv6_dst_unlock(dest);

	if (skb_headroom(skb) < neigh->dev->hard_header_len)
	{
		struct sk_buff *buff;

		buff = alloc_skb(neigh->dev->hard_header_len + skb->len + 15,
				 GFP_ATOMIC);

		if (buff == NULL)
		{
			return;
		}
		
		skb_reserve(buff, (neigh->dev->hard_header_len + 15) & ~15);

		buff->protocol = __constant_htons(ETH_P_IPV6);
		buff->h.raw = skb_put(buff, size);

		memcpy(buff->h.raw, skb->nh.ipv6h, size);
		buff->nh.ipv6h = (struct ipv6hdr *) buff->h.raw;
		kfree_skb(skb, FREE_READ);
		skb = buff;
	}

	ipv6_redo_mac_hdr(skb, neigh, size);

	priority = skb->nh.ipv6h->priority;

	priority = (priority & 0x7) >> 1;
	priority = pri_values[priority];

	if (dev->flags & IFF_UP)
	{
		skb->dev = neigh->dev;
		dev_queue_xmit(skb);
	}
	else
	{
		ipv6_statistics.Ip6OutDiscards++;
		kfree_skb(skb, FREE_READ);
	}
}


/*
 * Local variables:
 * c-file-style: "Linux"
 * End:
 */
