/*
 *	IPv6 input
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Ian P. Morris		<I.P.Morris@soton.ac.uk>
 *
 *	Based in linux/net/ipv4/ip_input.c
 *
 *	$Id: ipv6_input.c,v 1.13 1996/10/11 16:03:06 roque Exp $
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
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>

/*
 *	Header processing function list
 *	We process headers in order (as per RFC)
 *	If the processing function returns 0 the packet is considered 
 *	delivered else it returns the value of the nexthdr.
 *	The ptr field of the function points to the previous nexthdr field.
 *	This is allows the processing function to change it if it's sematics
 *	is: return a new packet without this header (like fragmentation).
 *	When a next_header value is not within the list 
 *	the inet protocol list is searched (i.e. to deliver to 
 *	TCP for instance)
 */

static int ipv6_dest_opt(struct sk_buff **skb_ptr, struct device *dev, __u8 *nhptr,
			 struct ipv6_options *opt);


struct hdrtype_proc {
	u8	type;
	int	(*func) (struct sk_buff **, struct device *dev, __u8 *ptr,
			 struct ipv6_options *opt);
} hdrproc_lst[] = {
  /*
	TODO

	{NEXTHDR_HOP,		ipv6_hop_by_hop}
   */
	{NEXTHDR_ROUTING,	ipv6_routing_header},
	{NEXTHDR_FRAGMENT,	ipv6_reassembly},
  
	{NEXTHDR_DEST,		ipv6_dest_opt},
   /*	
	{NEXTHDR_AUTH,		ipv6_auth_hdr},
	{NEXTHDR_ESP,		ipv6_esp_hdr},
    */
	{NEXTHDR_MAX,		NULL}
};

/* New header structures */


struct ipv6_tlvtype {
	u8 type;
	u8 len;
};

struct ipv6_destopt_hdr {
	u8 nexthdr;
	u8 hdrlen;
};


struct tlvtype_proc {
	u8	type;
	int	(*func) (struct sk_buff *, struct device *dev, __u8 *ptr,
			 struct ipv6_options *opt); 
	
	/* these functions do NOT  update skb->h.raw */
			 
} tlvprocdestopt_lst[] = {
	{255,			NULL}
};


static int parse_tlv(struct tlvtype_proc *procs, struct sk_buff *skb,
		     struct device *dev, __u8 *nhptr, struct ipv6_options *opt,
		     void *lastopt)
{
	struct ipv6_tlvtype *hdr;
	struct tlvtype_proc *curr;
	int pos;

	while ((hdr=(struct ipv6_tlvtype *)skb->h.raw) != lastopt)
		switch (hdr->type & 0x3F)
		{
		case 0: /* TLV encoded Pad1 */
			skb->h.raw++;
			break;

		case 1: /* TLV encoded PadN */
			skb->h.raw += hdr->len+2;
			break;

		default: /* Other TLV code so scan list */
			for (curr=procs; curr->type != 255; curr++)
				if (curr->type == (hdr->type & 0x3F))
				{
					curr->func(skb, dev, nhptr, opt);
					skb->h.raw += hdr->len+2;
					break;
				}
		
			if (curr->type==255)
			{ 
				/* unkown type */
				pos= (__u8 *) skb->h.raw - (__u8 *) skb->ipv6_hdr;
				/* I think this is correct please check - IPM */

				switch ((hdr->type & 0xC0) >> 6) {
					case 0: /* ignore */
						skb->h.raw += hdr->len+2;
						break;
						
					case 1: /* drop packet */
						kfree_skb(skb, FREE_READ);
						return 0;

					case 2: /* send ICMP PARM PROB regardless and 
						   drop packet */
						icmpv6_send(skb, ICMPV6_PARAMETER_PROB, 
							    2, pos, dev);
						kfree_skb(skb, FREE_READ);
						return 0;

					case 3: /* Send ICMP if not a multicast address 
						   and drop packet */
						if (!(ipv6_addr_type(&(skb->ipv6_hdr->daddr)) & IPV6_ADDR_MULTICAST) )
							icmpv6_send(skb, ICMPV6_PARAMETER_PROB, 2, pos, dev);
						kfree_skb(skb, FREE_READ);
						return 0;
					}
			}
			break;
		}
	
	return 1;
}
 


static int ipv6_dest_opt(struct sk_buff **skb_ptr, struct device *dev, __u8 *nhptr,
			 struct ipv6_options *opt)
{
	struct sk_buff *skb=*skb_ptr;
	struct ipv6_destopt_hdr *hdr = (struct ipv6_destopt_hdr *) skb->h.raw;
	
	if (parse_tlv(tlvprocdestopt_lst, skb, dev, nhptr, opt,skb->h.raw+hdr->hdrlen))
		return hdr->nexthdr;
	else
		return 0;
}



/*
 *	0 - deliver
 *	1 - block
 */
static __inline__ int icmpv6_filter(struct sock *sk, struct sk_buff *skb)
{
	struct icmpv6hdr *icmph;
	struct raw6_opt *opt;

	opt = &sk->tp_pinfo.tp_raw;
	icmph = (struct icmpv6hdr *) (skb->ipv6_hdr + 1);
	return test_bit(icmph->type, &opt->filter);
}

/*
 *	demultiplex raw sockets.
 *	(should consider queueing the skb in the sock receive_queue
 *	without calling rawv6.c)
 */
static struct sock * ipv6_raw_deliver(struct sk_buff *skb,
				      struct device *dev,
				      struct ipv6_options *opt,
				      __u16 nexthdr,
				      __u16 len,
				      struct in6_addr *saddr,
				      struct in6_addr *daddr)
{
	struct sock *sk, *sk2;
	__u8 hash;

	hash = nexthdr & (SOCK_ARRAY_SIZE-1);

	sk = rawv6_prot.sock_array[hash];
	

	/*
	 *	The first socket found will be delivered after
	 *	delivery to transport protocols.
	 */

	if (sk == NULL)
		return NULL;
	
	sk = inet6_get_sock_raw(sk, nexthdr, daddr, saddr);

	if (sk)
	{
		sk2 = sk;

		while ((sk2 = inet6_get_sock_raw(sk2->next, nexthdr, 
						 daddr, saddr)))
		{
			struct sk_buff *buff;

			if (nexthdr == IPPROTO_ICMPV6 &&
			    icmpv6_filter(sk2, skb))
			{
				continue;
			}
			buff = skb_clone(skb, GFP_ATOMIC);
			buff->sk = sk2;
			rawv6_rcv(buff, dev, saddr, daddr, opt, len);
		}
	}

	if (sk && nexthdr == IPPROTO_ICMPV6 && icmpv6_filter(sk, skb))
	{
		sk = NULL;
	}   
	
	return sk;
}

int ipv6_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct inet6_ifaddr	*ifp;
	struct ipv6_options	*opt = (struct ipv6_options *) skb->proto_priv;
	struct ipv6hdr		*hdr;
	u8			hash;
	u8			addr_type;
	struct inet6_protocol	*ipprot;
	struct sock		*raw_sk;
	int			found = 0;
	int			nexthdr = 0;
	__u8			*nhptr;
	int			pkt_len;

	hdr = skb->ipv6_hdr = (struct ipv6hdr *) skb->h.raw;

	if (skb->len < sizeof(struct ipv6hdr) || hdr->version != 6)
	{
		ipv6_statistics.Ip6InHdrErrors++;
		printk(KERN_DEBUG "ipv6_rcv: broken header\n");
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	pkt_len = ntohs(hdr->payload_len);

	if (pkt_len + sizeof(struct ipv6hdr) > skb->len)
	{
		printk(KERN_DEBUG "ipv6_rcv: invalid payload length\n");
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	skb_trim(skb, pkt_len + sizeof(struct ipv6hdr));

	/* check daddr */

	/* Accounting & Firewall check */

	addr_type = ipv6_addr_type(&hdr->daddr);

	if (addr_type & IPV6_ADDR_MULTICAST)
	{
		/* 
		 * if mcast address is not for one of our groups
		 * either pass it to mcast router or discard it 
		 */

		if (ipv6_chk_mcast_addr(dev, &hdr->daddr) == 0)
		{
			/* something like:
			   if (acting_as_router)
				ipv6_mcast_route(skb, ...)
			   else	
			   */
			kfree_skb(skb, FREE_READ);
			return 0;
		}
	}

	if (addr_type & IPV6_ADDR_MULTICAST ||
	    (ifp = ipv6_chk_addr(&hdr->daddr)))	    
	{

		/* loop in a cicle parsing nexthdrs */

		skb->h.raw   += sizeof(struct ipv6hdr);
 
		/* extension header processing must update skb->h.raw */

		nexthdr = hdr->nexthdr;
		nhptr = &hdr->nexthdr;


		while(1)
		{
			struct hdrtype_proc *hdrt;

			/* check for extension header */

			for (hdrt=hdrproc_lst; hdrt->type != NEXTHDR_MAX; hdrt++)
			{
				if (hdrt->type == nexthdr)
				{
					if ((nexthdr = hdrt->func(&skb, dev, nhptr, opt)))
					{
						nhptr = skb->h.raw;
						hdr = skb->ipv6_hdr;
						continue;
					}
					return 0;
				}
			}
			break;

		}

		/* 
		 * deliver to raw sockets
		 * should we deliver raw after or before parsing 
		 * extension headers ?
		 * delivering after means we do reassembly of datagrams
		 * in ip.
		 */

		pkt_len = skb->tail - skb->h.raw;

		raw_sk = ipv6_raw_deliver(skb, dev, opt, nexthdr, pkt_len,
					  &hdr->saddr, &hdr->daddr);

		/* check inet6_protocol list */

		hash = nexthdr & (MAX_INET_PROTOS -1);
		for (ipprot = (struct inet6_protocol *) inet6_protos[hash]; 
		     ipprot != NULL; 
		     ipprot = (struct inet6_protocol *) ipprot->next)
		{
			struct sk_buff *buff = skb;

			if (ipprot->protocol != nexthdr)
				continue;

			if (ipprot->copy || raw_sk)
				buff = skb_clone(skb, GFP_ATOMIC);


			ipprot->handler(buff, dev, 
					&hdr->saddr, &hdr->daddr,
					opt, pkt_len,
					0, ipprot);
			found = 1;
		}

		if (raw_sk)
		{
			skb->sk = raw_sk;
			rawv6_rcv(skb, dev, &hdr->saddr, &hdr->daddr, opt,
				  htons(hdr->payload_len));
			found = 1;
		}
 	       
		/* not found: send ICMP parameter problem back */

		if (!found)
		{
			printk(KERN_DEBUG "proto not found %d\n", nexthdr);
			skb->sk = NULL;
			kfree_skb(skb, FREE_READ);
		}
			
	}
	else
	{
		if (ipv6_forwarding)
		{
			if (addr_type & IPV6_ADDR_LINKLOCAL)
			{
				printk(KERN_DEBUG
				       "link local pkt to forward\n");
				kfree_skb(skb, FREE_READ);
				return 0;
			}
			ipv6_forward(skb, dev, 0);
		}
		else
		{
			printk(KERN_WARNING "IPV6: packet to forward -"
			       "host not configured as router\n");
			kfree_skb(skb, FREE_READ);
		}
	}

	return 0;
}

/*
 * Local variables:
 * c-file-style: "Linux"
 * End:
 */
