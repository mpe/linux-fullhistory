/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP forwarding functionality.
 *		
 * Authors:	see ip.c
 *
 * Fixes:
 *		Many		:	Split from ip.c , see ip_input.c for 
 *					history.
 *		Dave Gregorich	:	NULL ip_rt_put fix for multicast 
 *					routing.
 *		Jos Vos		:	Add call_out_firewall before sending,
 *					use output device for accounting.
 *		Jos Vos		:	Call forward firewall after routing
 *					(always use output device).
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/firewall.h>
#include <linux/ip_fw.h>
#ifdef CONFIG_IP_MASQUERADE
#include <net/ip_masq.h>
#endif
#include <net/checksum.h>
#include <linux/route.h>
#include <net/route.h>
 
#ifdef CONFIG_IP_FORWARD
#ifdef CONFIG_IP_MROUTE

/*
 * 	Encapsulate a packet by attaching a valid IPIP header to it.
 *	This avoids tunnel drivers and other mess and gives us the speed so
 *	important for multicast video.
 */
 
static void ip_encap(struct sk_buff *skb, int len, struct device *out, __u32 daddr)
{
	/*
	 *	There is space for the IPIP header and MAC left.
	 *
	 *	Firstly push down and install the IPIP header.
	 */
	struct iphdr *iph=(struct iphdr *)skb_push(skb,sizeof(struct iphdr));
	if(len>65515)
		len=65515;
	iph->version	= 	4;
	iph->tos	=	skb->ip_hdr->tos;
	iph->ttl	=	skb->ip_hdr->ttl;
	iph->frag_off	=	0;
	iph->daddr	=	daddr;
	iph->saddr	=	out->pa_addr;
	iph->protocol	=	IPPROTO_IPIP;
	iph->ihl	=	5;
	iph->tot_len	=	htons(skb->len);
	iph->id		=	htons(ip_id_count++);
	ip_send_check(iph);

	skb->dev = out;
	skb->arp = 1;
	skb->raddr=daddr;
	/*
	 *	Now add the physical header (driver will push it down).
	 */
	if (out->hard_header && out->hard_header(skb, out, ETH_P_IP, NULL, NULL, len)<0)
			skb->arp=0;
	/*
	 *	Read to queue for transmission.
	 */
}

#endif

/*
 *	Forward an IP datagram to its next destination.
 */

int ip_forward(struct sk_buff *skb, struct device *dev, int is_frag,
	       __u32 target_addr)
{
	struct device *dev2;	/* Output device */
	struct iphdr *iph;	/* Our header */
	struct sk_buff *skb2;	/* Output packet */
	struct rtable *rt;	/* Route we use */
	unsigned char *ptr;	/* Data pointer */
	unsigned long raddr;	/* Router IP address */
	struct   options * opt	= (struct options*)skb->proto_priv;
	struct hh_cache *hh = NULL;
	int encap = 0;		/* Encap length */
#ifdef CONFIG_FIREWALL
	int fw_res = 0;		/* Forwarding result */	
#ifdef CONFIG_IP_MASQUERADE	
	struct sk_buff *skb_in = skb;	/* So we can remember if the masquerader did some swaps */
#endif /* CONFIG_IP_MASQUERADE */
#endif /* CONFIG_FIREWALL */
	
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

#ifdef CONFIG_IP_MROUTE
	if(!(is_frag&IPFWD_MULTICASTING))
	{
#endif	
		/*
		 * OK, the packet is still valid.  Fetch its destination address,
		 * and give it to the IP sender for further processing.
		 */

		rt = ip_rt_route(target_addr, 0);

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
	
		if (opt->is_strictroute && (rt->rt_flags & RTF_GATEWAY)) {
			/*
			 *	Strict routing permits no gatewaying
			 */
	
			ip_rt_put(rt);
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_SR_FAILED, 0, dev);
			return -1;
		}

		/*
		 *	Having picked a route we can now send the frame out
		 *	after asking the firewall permission to do so.
		 */

		dev2 = rt->rt_dev;
		hh = rt->rt_hh;
		/*
		 *	In IP you never have to forward a frame on the interface that it 
		 *	arrived upon. We now generate an ICMP HOST REDIRECT giving the route
		 *	we calculated.
		 */
#ifndef CONFIG_IP_NO_ICMP_REDIRECT
		if (dev == dev2 && 
			!((iph->saddr^dev->pa_addr)&dev->pa_mask) &&
			/* The daddr!=raddr test isn't obvious - what it's doing
			   is avoiding sending a frame the receiver will not 
			   believe anyway.. */
			iph->daddr != raddr/*ANK*/ && !opt->srr)
				icmp_send(skb, ICMP_REDIRECT, ICMP_REDIR_HOST, raddr, dev);
#endif
#ifdef CONFIG_IP_MROUTE
	}
	else
	{
		/*
		 *	Multicast route forward. Routing is already done
		 */
		dev2=skb->dev;
		raddr=skb->raddr;
		if(is_frag&IPFWD_MULTITUNNEL)	/* VIFF_TUNNEL mode */
			encap=20;
		rt=NULL;
	}
#endif	
	
	/* 
	 *	See if we are allowed to forward this.
 	 *	Note: demasqueraded fragments are always 'back'warded.
	 */
	
#ifdef CONFIG_FIREWALL
	if(!(is_frag&IPFWD_MASQUERADED))
	{
#ifdef CONFIG_IP_MASQUERADE
		/* 
		 *	Check that any ICMP packets are not for a 
		 *	masqueraded connection.  If so rewrite them
		 *	and skip the firewall checks
		 */
		if (iph->protocol == IPPROTO_ICMP)
		{
			if ((fw_res = ip_fw_masq_icmp(&skb, dev2)) < 0)
				/* Problem - ie bad checksum */
				return -1;

			if (fw_res)
				/* ICMP matched - skip firewall */
				goto skip_call_fw_firewall;
		}
#endif
		fw_res=call_fw_firewall(PF_INET, dev2, iph, NULL);
		switch (fw_res) {
		case FW_ACCEPT:
		case FW_MASQUERADE:
			break;
		case FW_REJECT:
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0, dev);
			/* fall thru */
		default:
			return -1;
		}

#ifdef CONFIG_IP_MASQUERADE
		skip_call_fw_firewall:
#endif		
	}
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
		 * (Don't masquerade de-masqueraded fragments)
		 */
		if (!(is_frag&IPFWD_MASQUERADED) && fw_res==FW_MASQUERADE)
			if (ip_fw_masquerade(&skb, dev2) < 0)
			{
				/*
				 * Masquerading failed; silently discard this packet.
				 */
				if (rt)
					ip_rt_put(rt);
				return -1;
			}
#endif
		IS_SKB(skb);

		if (skb->len+encap > dev2->mtu && (ntohs(iph->frag_off) & IP_DF)) 
		{
			ip_statistics.IpFragFails++;
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(dev2->mtu), dev);
			if(rt)
				ip_rt_put(rt);
			return -1;
		}

#ifdef CONFIG_IP_MROUTE
		if(skb_headroom(skb)-encap<dev2->hard_header_len)
		{
			skb2 = alloc_skb(dev2->hard_header_len + skb->len + encap + 15, GFP_ATOMIC);
#else
		if(skb_headroom(skb)<dev2->hard_header_len)
		{
			skb2 = alloc_skb(dev2->hard_header_len + skb->len + 15, GFP_ATOMIC);
#endif		
			/*
			 *	This is rare and since IP is tolerant of network failures
			 *	quite harmless.
			 */
		
			if (skb2 == NULL)
			{
				NETDEBUG(printk("\nIP: No memory available for IP forward\n"));
				if(rt)
					ip_rt_put(rt);
				return -1;
			}
		
			IS_SKB(skb2);
			/*
			 *	Add the physical headers.
			 */
			skb2->protocol=htons(ETH_P_IP);
#ifdef CONFIG_IP_MROUTE
			if(is_frag&IPFWD_MULTITUNNEL)
			{
				skb_reserve(skb2,(encap+dev2->hard_header_len+15)&~15);	/* 16 byte aligned IP headers are good */
				ip_encap(skb2,skb->len, dev2, raddr);
			}
			else
#endif			
		 		ip_send(rt,skb2,raddr,skb->len,dev2,dev2->pa_addr);

			/*
			 *	We have to copy the bytes over as the new header wouldn't fit
			 *	the old buffer. This should be very rare.
			 */		 
		 	
			ptr = skb_put(skb2,skb->len);
			skb2->free = 1;
			skb2->h.raw = ptr;

			/*
			 *	Copy the packet data into the new buffer.
			 */
			memcpy(ptr, skb->h.raw, skb->len);
			memcpy(skb2->proto_priv, skb->proto_priv, sizeof(skb->proto_priv));
			iph = skb2->ip_hdr = skb2->h.iph;
		}
		else
		{
			/* 
			 *	Build a new MAC header. 
			 */

			skb2 = skb;		
			skb2->dev=dev2;
#ifdef CONFIG_IP_MROUTE
			if(is_frag&IPFWD_MULTITUNNEL)
				ip_encap(skb,skb->len, dev2, raddr);
			else
			{
#endif
				skb->arp=1;
				skb->raddr=raddr;
				if (hh)
				{
					memcpy(skb_push(skb, dev2->hard_header_len), hh->hh_data, dev2->hard_header_len);
					if (!hh->hh_uptodate)
					{
#if RT_CACHE_DEBUG >= 2
						printk("ip_forward: hh miss %08x via %08x\n", target_addr, rt->rt_gateway);
#endif						
						skb->arp = 0;
					}
				}
				else if (dev2->hard_header)
				{
					if(dev2->hard_header(skb, dev2, ETH_P_IP, NULL, NULL, skb->len)<0)
						skb->arp=0;
				}
#ifdef CONFIG_IP_MROUTE
			}				
#endif			
		}
#ifdef CONFIG_FIREWALL
		if((fw_res = call_out_firewall(PF_INET, skb2->dev, iph, NULL)) < FW_ACCEPT)
		{
			/* FW_ACCEPT and FW_MASQUERADE are treated equal:
			   masquerading is only supported via forward rules */
			if (fw_res == FW_REJECT)
				icmp_send(skb2, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0, dev);
			if (skb != skb2)
				kfree_skb(skb2,FREE_WRITE);
			return -1;
		}
#endif
		ip_statistics.IpForwDatagrams++;

		if (opt->optlen) 
		{
			unsigned char * optptr;
			if (opt->rr_needaddr) 
			{
				optptr = (unsigned char *)iph + opt->rr;
				memcpy(&optptr[optptr[2]-5], &dev2->pa_addr, 4);
				opt->is_changed = 1;
			}
			if (opt->srr_is_hit) 
			{
				int srrptr, srrspace;

				optptr = (unsigned char *)iph + opt->srr;

				for ( srrptr=optptr[2], srrspace = optptr[1];
				      srrptr <= srrspace;
				     srrptr += 4
				    ) 
				{
					if (srrptr + 3 > srrspace)
						break;
					if (memcmp(&target_addr, &optptr[srrptr-1], 4) == 0)
						break;
				}
				if (srrptr + 3 <= srrspace) 
				{
					opt->is_changed = 1;
					memcpy(&optptr[srrptr-1], &dev2->pa_addr, 4);
					iph->daddr = target_addr;
					optptr[2] = srrptr+4;
				}
				else
				        printk(KERN_CRIT "ip_forward(): Argh! Destination lost!\n");
			}
			if (opt->ts_needaddr) 
			{
				optptr = (unsigned char *)iph + opt->ts;
				memcpy(&optptr[optptr[2]-9], &dev2->pa_addr, 4);
				opt->is_changed = 1;
			}
			if (opt->is_changed) 
			{
				opt->is_changed = 0;
				ip_send_check(iph);
			}
		}
/*
 * ANK:  this is point of "no return", we cannot send an ICMP,
 *       because we changed SRR option.
 */

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
			 
			ip_fw_chk(iph,dev2,NULL,ip_acct_chain,0,IP_FW_MODE_ACCT_OUT);
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
	{
	        if(rt)
	        	ip_rt_put(rt);
		return -1;
	}
	if(rt)
		ip_rt_put(rt);
	
	/*
	 *	Tell the caller if their buffer is free.
	 */	 
	 
	if(skb==skb2)
		return 0;	

#ifdef CONFIG_IP_MASQUERADE	
	/*
	 *	The original is free. Free our copy and
	 *	tell the caller not to free.
	 */
	if(skb!=skb_in)
	{
		kfree_skb(skb_in, FREE_WRITE);
		return 0;
	}
#endif	
	return 1;
}


#endif
