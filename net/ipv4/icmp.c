/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Internet Control Message Protocol (ICMP)
 *
 * Version:	@(#)icmp.c	1.0.11	06/02/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Stefan Becker, <stefanb@yello.ping.de>
 *
 * Fixes:	
 *		Alan Cox	:	Generic queue usage.
 *		Gerhard Koerting:	ICMP addressing corrected
 *		Alan Cox	:	Use tos/ttl settings
 *		Alan Cox	:	Protocol violations
 *		Alan Cox	:	SNMP Statistics		
 *		Alan Cox	:	Routing errors
 *		Alan Cox	:	Changes for newer routing code
 *		Alan Cox	:	Removed old debugging junk
 *		Alan Cox	:	Fixed the ICMP error status of net/host unreachable
 *	Gerhard Koerting	:	Fixed broadcast ping properly
 *		Ulrich Kunitz	:	Fixed ICMP timestamp reply
 *		A.N.Kuznetsov	:	Multihoming fixes.
 *		Laco Rusnak	:	Multihoming fixes.
 *		Alan Cox	:	Tightened up icmp_send().
 *		Alan Cox	:	Multicasts.
 *		Stefan Becker   :       ICMP redirects in icmp_send().
 *		Peter Belding	:	Tightened up ICMP redirect handling
 *		Alan Cox	:	Tightened even more.
 *		Arnt Gulbrandsen:	Misplaced #endif with net redirect and break
 *		A.N.Kuznetsov	:	ICMP timestamp still used skb+1
 *		Mike Shaver	:	RFC1122 checks.
 * 
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

/* RFC1122 Status: (boy, are there a lot of rules for ICMP)
   3.2.2 (Generic ICMP stuff)
     MUST discard messages of unknown type. (OK)
     MUST copy at least the first 8 bytes from the offending packet
       when sending ICMP errors. (OK)
     MUST pass received ICMP errors up to protocol level. (OK)
     SHOULD send ICMP errors with TOS == 0. (OK)
     MUST NOT send ICMP errors in reply to:
       ICMP errors (OK)
       Broadcast/multicast datagrams (OK)
       MAC broadcasts (OK)
       Non-initial fragments (OK)
       Datagram with a source address that isn't a single host. (OK)
  3.2.2.1 (Destination Unreachable)
    All the rules govern the IP layer, and are dealt with in ip.c, not here.
  3.2.2.2 (Redirect)
    Host SHOULD NOT send ICMP_REDIRECTs.  (OK)
    MUST update routing table in response to host or network redirects. 
      (host OK, network NOT YET) [Intentionally -- AC]
    SHOULD drop redirects if they're not from directly connected gateway
      (OK -- we drop it if it's not from our old gateway, which is close
       enough)
  3.2.2.3 (Source Quench)
    MUST pass incoming SOURCE_QUENCHs to transport layer (OK)
    Other requirements are dealt with at the transport layer.
  3.2.2.4 (Time Exceeded)
    MUST pass TIME_EXCEEDED to transport layer (OK)
    Other requirements dealt with at IP (generating TIME_EXCEEDED).
  3.2.2.5 (Parameter Problem)
    SHOULD generate these, but it doesn't say for what.  So we're OK. =)
    MUST pass received PARAMPROBLEM to transport layer (NOT YET)
    	[Solaris 2.X seems to assert EPROTO when this occurs] -- AC
  3.2.2.6 (Echo Request/Reply)
    MUST reply to ECHO_REQUEST, and give app to do ECHO stuff (OK, OK)
    MAY discard broadcast ECHO_REQUESTs. (We don't, but that's OK.)
    MUST reply using same source address as the request was sent to.
      We're OK for unicast ECHOs, and it doesn't say anything about
      how to handle broadcast ones, since it's optional.
    MUST copy data from REQUEST to REPLY (OK)
      unless it would require illegal fragmentation (MUST) (NOT YET)
    MUST pass REPLYs to transport/user layer (OK)
    MUST use any provided source route (reversed) for REPLY. (NOT YET)
 3.2.2.7 (Information Request/Reply)
   MUST NOT implement this. (I guess that means silently discard...?) (OK)
 3.2.2.8 (Timestamp Request/Reply)
   MAY implement (OK)
   SHOULD be in-kernel for "minimum variability" (OK)
   MAY discard broadcast REQUESTs.  (OK, but see source for inconsistency)
   MUST reply using same source address as the request was sent to. (OK)
   MUST reverse source route, as per ECHO (NOT YET)
   MUST pass REPLYs to transport/user layer (requires RAW, just like ECHO) (OK)
   MUST update clock for timestamp at least 15 times/sec (OK)
   MUST be "correct within a few minutes" (OK)
 3.2.2.9 (Address Mask Request/Reply)
   MAY implement (OK)
   MUST send a broadcast REQUEST if using this system to set netmask
     (OK... we don't use it)
   MUST discard received REPLYs if not using this system (OK)
   MUST NOT send replies unless specifically made agent for this sort
     of thing. (NOT YET)
*/

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/protocol.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/snmp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <net/checksum.h>


#define min(a,b)	((a)<(b)?(a):(b))


/*
 *	Statistics
 */
 
struct icmp_mib	icmp_statistics={0,};


/* An array of errno for error messages from dest unreach. */
struct icmp_err icmp_err_convert[] = {
  { ENETUNREACH,	0 },	/*	ICMP_NET_UNREACH	*/
  { EHOSTUNREACH,	0 },	/*	ICMP_HOST_UNREACH	*/
  { ENOPROTOOPT,	1 },	/*	ICMP_PROT_UNREACH	*/
  { ECONNREFUSED,	1 },	/*	ICMP_PORT_UNREACH	*/
  { EOPNOTSUPP,		0 },	/*	ICMP_FRAG_NEEDED	*/
  { EOPNOTSUPP,		0 },	/*	ICMP_SR_FAILED		*/
  { ENETUNREACH,	1 },	/* 	ICMP_NET_UNKNOWN	*/
  { EHOSTDOWN,		1 },	/*	ICMP_HOST_UNKNOWN	*/
  { ENONET,		1 },	/*	ICMP_HOST_ISOLATED	*/
  { ENETUNREACH,	1 },	/*	ICMP_NET_ANO		*/
  { EHOSTUNREACH,	1 },	/*	ICMP_HOST_ANO		*/
  { EOPNOTSUPP,		0 },	/*	ICMP_NET_UNR_TOS	*/
  { EOPNOTSUPP,		0 }	/*	ICMP_HOST_UNR_TOS	*/
};


/*
 *	Send an ICMP message in response to a situation
 */
 
void icmp_send(struct sk_buff *skb_in, int type, int code, unsigned long info, struct device *dev)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	int offset;
	struct icmphdr *icmph;
	int len;
	struct device *ndev=NULL;	/* Make this =dev to force replies on the same interface */
	unsigned long our_addr;
	int atype;
	
	/*
	 *	Find the original IP header.
	 */
	 
	iph = (struct iphdr *) skb_in->data;
	
	/*
	 *	No replies to MAC multicast
	 */
	 
	if(skb_in->pkt_type!=PACKET_HOST)
		return;
		
	/*
	 *	No replies to IP multicasting
	 */
	 
	atype=ip_chk_addr(iph->daddr);
	if(atype==IS_BROADCAST || atype==IS_MULTICAST)
		return;

	/*
	 *	Only reply to first fragment.
	 */
	 
	if(ntohs(iph->frag_off)&IP_OFFSET)
		return;
	 		
	/*
	 *	We must NEVER NEVER send an ICMP error to an ICMP error message
	 */
	 
	if(type==ICMP_DEST_UNREACH||type==ICMP_REDIRECT||type==ICMP_SOURCE_QUENCH||type==ICMP_TIME_EXCEEDED)
	{

		/*
		 *	Is the original packet an ICMP packet?
		 */

		if(iph->protocol==IPPROTO_ICMP)
		{
			icmph = (struct icmphdr *) ((char *) iph +
                                                    4 * iph->ihl);
			/*
			 *	Check for ICMP error packets (Must never reply to
			 *	an ICMP error).
			 */
	
			if (icmph->type == ICMP_DEST_UNREACH ||
				icmph->type == ICMP_SOURCE_QUENCH ||
				icmph->type == ICMP_REDIRECT ||
				icmph->type == ICMP_TIME_EXCEEDED ||
				icmph->type == ICMP_PARAMETERPROB)
				return;
		}
	}
	icmp_statistics.IcmpOutMsgs++;
	
	/*
	 *	This needs a tidy.	
	 */
		
	switch(type)
	{
		case ICMP_DEST_UNREACH:
			icmp_statistics.IcmpOutDestUnreachs++;
			break;
		case ICMP_SOURCE_QUENCH:
			icmp_statistics.IcmpOutSrcQuenchs++;
			break;
		case ICMP_REDIRECT:
		/* RFC1122: (3.2.2.2) Sorta bad.  SHOULDN'T send */
		/* ICMP_REDIRECTs unless we're a gateway. -- MS */
		/* We don't .. this path isnt invoked -- AC */
			icmp_statistics.IcmpOutRedirects++;
			break;
		case ICMP_ECHO:
			icmp_statistics.IcmpOutEchos++;
			break;
		case ICMP_ECHOREPLY:
			icmp_statistics.IcmpOutEchoReps++;
			break;
		case ICMP_TIME_EXCEEDED:
			icmp_statistics.IcmpOutTimeExcds++;
			break;
		case ICMP_PARAMETERPROB:
			icmp_statistics.IcmpOutParmProbs++;
			break;
		case ICMP_TIMESTAMP:
			icmp_statistics.IcmpOutTimestamps++;
			break;
		case ICMP_TIMESTAMPREPLY:
			icmp_statistics.IcmpOutTimestampReps++;
			break;
		case ICMP_ADDRESS:
			icmp_statistics.IcmpOutAddrMasks++;
			break;
		case ICMP_ADDRESSREPLY:
			icmp_statistics.IcmpOutAddrMaskReps++;
			break;
	}		
	/*
	 *	Get some memory for the reply. 
	 */
	 
	len = dev->hard_header_len + sizeof(struct iphdr) + sizeof(struct icmphdr) +
		sizeof(struct iphdr) + 32;	/* amount of header to return */
	   
	skb = (struct sk_buff *) alloc_skb(len+15, GFP_ATOMIC);
	if (skb == NULL) 
	{
		icmp_statistics.IcmpOutErrors++;
		return;
	}
	skb->free = 1;

	/*
	 *	Build Layer 2-3 headers for message back to source. 
	 */

	our_addr = dev->pa_addr;

	/* RFC1122: (3.2.2).  MUST NOT send ICMP in reply to */
	/* packet with a source IP address that doesn't define a single */
	/* host. -- MS.  Checked higher up -- AC */

	if (iph->daddr != our_addr && ip_chk_addr(iph->daddr) == IS_MYADDR)
		our_addr = iph->daddr;
	offset = ip_build_header(skb, our_addr, iph->saddr,
			   &ndev, IPPROTO_ICMP, NULL, len,
			   skb_in->ip_hdr->tos,255);
	if (offset < 0) 
	{
		icmp_statistics.IcmpOutErrors++;
		skb->sk = NULL;
		kfree_skb(skb, FREE_READ);
		return;
	}

	/* 
	 *	Re-adjust length according to actual IP header size. 
	 */

	skb_put(skb,sizeof(struct icmphdr) + sizeof(struct iphdr) + 8);
	
	/*
	 *	Fill in the frame
	 */
	 
	/* RFC1122: SHOULD send with TOS == 0, and I guess this does. */
	/* Perhaps it should be explicit? -- MS */

	icmph = (struct icmphdr *) (skb->data + offset);
	icmph->type = type;
	icmph->code = code;
	icmph->checksum = 0;
	icmph->un.gateway = info;	/* This might not be meant for 
					   this form of the union but it will
					   be right anyway */

	/* RFC1122: OK. Copies the minimum 8 bytes unchanged from the offending */
	/* packet (MUST) as per 3.2.2. -- MS */
	memcpy(icmph + 1, iph, sizeof(struct iphdr) + 8);

	icmph->checksum = ip_compute_csum((unsigned char *)icmph,
                         sizeof(struct icmphdr) + sizeof(struct iphdr) + 8);

	/*
	 *	Send it and free it once sent.
	 */
	ip_queue_xmit(NULL, ndev, skb, 1);
}


/* 
 *	Handle ICMP_UNREACH and ICMP_QUENCH. 
 */
 
static void icmp_unreach(struct icmphdr *icmph, struct sk_buff *skb)
{
	struct inet_protocol *ipprot;
	struct iphdr *iph;
	unsigned char hash;
	int err;

	err = (icmph->type << 8) | icmph->code;
	iph = (struct iphdr *) (icmph + 1);
	
	switch(icmph->code & 7) 
	{
		case ICMP_NET_UNREACH:
			break;
		case ICMP_HOST_UNREACH:
			break;
		case ICMP_PROT_UNREACH:
#ifdef CONFIG_NET_DEBUG
			printk("ICMP: %s:%d: protocol unreachable.\n",
				in_ntoa(iph->daddr), ntohs(iph->protocol));
#endif
			break;
		case ICMP_PORT_UNREACH:
			break;
		case ICMP_FRAG_NEEDED:
#ifdef CONFIG_NET_DEBUG
			printk("ICMP: %s: fragmentation needed and DF set.\n",
								in_ntoa(iph->daddr));
#endif
			break;
		case ICMP_SR_FAILED:
#ifdef CONFIG_NET_DEBUG
			printk("ICMP: %s: Source Route Failed.\n", in_ntoa(iph->daddr));
#endif
			break;
		default:
			break;
	}

	/*
	 *	Get the protocol(s). 
	 */
	 
	hash = iph->protocol & (MAX_INET_PROTOS -1);

	/*
	 *	This can't change while we are doing it. 
	 */
	 
	ipprot = (struct inet_protocol *) inet_protos[hash];
	while(ipprot != NULL) 
	{
		struct inet_protocol *nextip;

		nextip = (struct inet_protocol *) ipprot->next;
	
		/* 
		 *	Pass it off to everyone who wants it. 
		 */

		/* RFC1122: OK. Passes appropriate ICMP errors to the */
		/* appropriate protocol layer (MUST), as per 3.2.2. */

		if (iph->protocol == ipprot->protocol && ipprot->err_handler) 
		{
			ipprot->err_handler(err, (unsigned char *)(icmph + 1),
					    iph->daddr, iph->saddr, ipprot);
		}

		ipprot = nextip;
  	}
	kfree_skb(skb, FREE_READ);
}


/*
 *	Handle ICMP_REDIRECT. 
 */

static void icmp_redirect(struct icmphdr *icmph, struct sk_buff *skb,
	struct device *dev, unsigned long source)
{
#ifndef CONFIG_IP_FORWARD
	struct rtable *rt;
#endif
	struct iphdr *iph;
	unsigned long ip;

	/*
	 *	Get the copied header of the packet that caused the redirect
	 */
	 
	iph = (struct iphdr *) (icmph + 1);
	ip = iph->daddr;

#ifdef CONFIG_IP_FORWARD
	/*
	 *	We are a router. Routers should not respond to ICMP_REDIRECT messages.
	 */
	printk("icmp: ICMP redirect from %s on %s ignored.\n", in_ntoa(source), dev->name);
#else	
	switch(icmph->code & 7) 
	{
		case ICMP_REDIR_NET:
			/*
			 *	This causes a problem with subnetted networks. What we should do
			 *	is use ICMP_ADDRESS to get the subnet mask of the problem route
			 *	and set both. But we don't..
			 */
#ifdef not_a_good_idea
			ip_rt_add((RTF_DYNAMIC | RTF_MODIFIED | RTF_GATEWAY),
				ip, 0, icmph->un.gateway, dev,0, 0, 0);
#endif
			break;
		case ICMP_REDIR_HOST:
			/*
			 *	Add better route to host.
			 *	But first check that the redirect
			 *	comes from the old gateway..
			 *	And make sure it's an ok host address
			 *	(not some confused thing sending our
			 *	address)
			 */
			rt = ip_rt_route(ip, NULL, NULL);
			if (!rt)
				break;
			if (rt->rt_gateway != source || 
				((icmph->un.gateway^dev->pa_addr)&dev->pa_mask) ||
				ip_chk_addr(icmph->un.gateway))
				break;
			printk("ICMP redirect from %s\n", in_ntoa(source));
			ip_rt_add((RTF_DYNAMIC | RTF_MODIFIED | RTF_HOST | RTF_GATEWAY),
				ip, 0, icmph->un.gateway, dev,0, 0, 0, 0);
			break;
		case ICMP_REDIR_NETTOS:
		case ICMP_REDIR_HOSTTOS:
			printk("ICMP: cannot handle TOS redirects yet!\n");
			break;
		default:
			break;
  	}
#endif  	
  	/*
  	 *	Discard the original packet
  	 */
  	 
  	kfree_skb(skb, FREE_READ);
}


/*
 *	Handle ICMP_ECHO ("ping") requests. 
 */
 
static void icmp_echo(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev,
	  unsigned long saddr, unsigned long daddr, int len,
	  struct options *opt)
{
	struct icmphdr *icmphr;
	struct sk_buff *skb2;
	struct device *ndev=NULL;
	int size, offset;

	icmp_statistics.IcmpOutEchoReps++;
	icmp_statistics.IcmpOutMsgs++;
	
	size = dev->hard_header_len + 64 + len + 15;
	skb2 = alloc_skb(size, GFP_ATOMIC);

	if (skb2 == NULL) 
	{
		icmp_statistics.IcmpOutErrors++;
		kfree_skb(skb, FREE_READ);
		return;
	}
	skb2->free = 1;

	/* Build Layer 2-3 headers for message back to source */
	offset = ip_build_header(skb2, daddr, saddr, &ndev,
	 	IPPROTO_ICMP, opt, len, skb->ip_hdr->tos,255);
	if (offset < 0) 
	{
		icmp_statistics.IcmpOutErrors++;
		printk("ICMP: Could not build IP Header for ICMP ECHO Response\n");
		kfree_skb(skb2,FREE_WRITE);
		kfree_skb(skb, FREE_READ);
		return;
	}

	/*
	 *	Re-adjust length according to actual IP header size. 
	 */
	 
	skb_put(skb2,len);

	/*
	 *	Build ICMP_ECHO Response message. 
	 */
	icmphr = (struct icmphdr *) (skb2->data + offset);
	memcpy((char *) icmphr, (char *) icmph, len);

	/* Are we copying the data from the ECHO datagram? */
	/* We're supposed to, and it looks like we are. -- MS */
	/* We're also supposed to truncate it if it would force */
	/* illegal fragmentation. *sigh*  */

	icmphr->type = ICMP_ECHOREPLY;
	icmphr->code = 0;
	icmphr->checksum = 0;
	icmphr->checksum = ip_compute_csum((unsigned char *)icmphr, len);

	/*
	 *	Ship it out - free it when done 
	 */
	ip_queue_xmit((struct sock *)NULL, ndev, skb2, 1);

	/*
	 *	Free the received frame
	 */
	 
	kfree_skb(skb, FREE_READ);
}

/*
 *	Handle ICMP Timestamp requests. 
 */
 
static void icmp_timestamp(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev,
	  unsigned long saddr, unsigned long daddr, int len,
	  struct options *opt)
{
	struct icmphdr *icmphr;
	struct sk_buff *skb2;
	int size, offset;
	unsigned long *timeptr, midtime;
	struct device *ndev=NULL;

        if (len < 12)
	{
		printk(
		  "ICMP: Size (%d) of ICMP_TIMESTAMP request should be 20!\n",
		  len);
		icmp_statistics.IcmpInErrors++;		
                /* correct answers are possible for everything >= 12 */
	}

	size = dev->hard_header_len + 84 + 15;

	if (! (skb2 = alloc_skb(size, GFP_ATOMIC))) 
	{
		skb->sk = NULL;
		kfree_skb(skb, FREE_READ);
		icmp_statistics.IcmpOutErrors++;		
		return;
	}
	skb2->free = 1;
 
/*
 *	Build Layer 2-3 headers for message back to source 
 */
 
	offset = ip_build_header(skb2, daddr, saddr, &ndev, IPPROTO_ICMP, opt, len, 
				skb->ip_hdr->tos, 255);
	if (offset < 0) 
	{
		printk("ICMP: Could not build IP Header for ICMP TIMESTAMP Response\n");
		kfree_skb(skb2, FREE_WRITE);
		kfree_skb(skb, FREE_READ);
		icmp_statistics.IcmpOutErrors++;
		return;
	}
 
	/*
	 *	Re-adjust length according to actual IP header size. 
	 */
	skb_put(skb2,20);
 
	/*
	 *	Build ICMP_TIMESTAMP Response message. 
	 */

	icmphr = (struct icmphdr *) (skb2->data + offset);
	memcpy((char *) icmphr, (char *) icmph, 12);
	icmphr->type = ICMP_TIMESTAMPREPLY;
	icmphr->code = icmphr->checksum = 0;

	/* fill in the current time as ms since midnight UT: */
	midtime = (xtime.tv_sec % 86400) * 1000 + xtime.tv_usec / 1000;
	timeptr = (unsigned long *) (icmphr + 1);
	/*
	 *	the originate timestamp (timeptr [0]) is still in the copy: 
	 */
	timeptr [1] = timeptr [2] = htonl(midtime);

	icmphr->checksum = ip_compute_csum((unsigned char *) icmphr, 20);

	/*
	 *	Ship it out - free it when done 
	 */

	ip_queue_xmit((struct sock *) NULL, ndev, skb2, 1);
	icmp_statistics.IcmpOutTimestampReps++;
	kfree_skb(skb, FREE_READ);
}
 
 


/*
 *	Handle the ICMP INFORMATION REQUEST. 
 */
 
static void icmp_info(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev,
	  unsigned long saddr, unsigned long daddr, int len,
	  struct options *opt)
{
	/* Obsolete */
	kfree_skb(skb, FREE_READ);
}


/* 
 *	Handle ICMP_ADDRESS_MASK requests. 
 */

/* RFC1122 (3.2.2.9).  A host MUST only send replies to */
/* ADDRESS_MASK requests if it's been configured as an address mask */
/* agent.  Receiving a request doesn't constitute implicit permission to */
/* act as one. Of course, implementing this correctly requires (SHOULD) */
/* a way to turn the functionality on and off.  Another one for sysctl(), */
/* I guess. -- MS */
/* Botched with a CONFIG option for now - Linus add scts sysctl please.. */
 
static void icmp_address(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev,
	  unsigned long saddr, unsigned long daddr, int len,
	  struct options *opt)
{
#ifdef CONFIG_IP_ADDR_AGENT
	struct icmphdr *icmphr;
	struct sk_buff *skb2;
	int size, offset;
	struct device *ndev=NULL;

	icmp_statistics.IcmpOutMsgs++;
	icmp_statistics.IcmpOutAddrMaskReps++;
	
	size = dev->hard_header_len + 64 + len + 15;
	skb2 = alloc_skb(size, GFP_ATOMIC);
	if (skb2 == NULL) 
	{
		icmp_statistics.IcmpOutErrors++;
		kfree_skb(skb, FREE_READ);
		return;
  	}
  	skb2->free = 1;
	
	/* 
	 *	Build Layer 2-3 headers for message back to source 
	 */

	offset = ip_build_header(skb2, daddr, saddr, &ndev,
		 	IPPROTO_ICMP, opt, len, skb->ip_hdr->tos,255);
	if (offset < 0) 
	{
		icmp_statistics.IcmpOutErrors++;
		printk("ICMP: Could not build IP Header for ICMP ADDRESS Response\n");
		kfree_skb(skb2,FREE_WRITE);
		kfree_skb(skb, FREE_READ);
		return;
	}

	/*
	 *	Re-adjust length according to actual IP header size. 
	 */

	skb_put(skb2,len);

	/*
	 *	Build ICMP ADDRESS MASK Response message. 
	 */

	icmphr = (struct icmphdr *) (skb2->data + offset);
	icmphr->type = ICMP_ADDRESSREPLY;
	icmphr->code = 0;
	icmphr->checksum = 0;
	icmphr->un.echo.id = icmph->un.echo.id;
	icmphr->un.echo.sequence = icmph->un.echo.sequence;
	memcpy((char *) (icmphr + 1), (char *) &dev->pa_mask, sizeof(dev->pa_mask));

	icmphr->checksum = ip_compute_csum((unsigned char *)icmphr, len);

	/* Ship it out - free it when done */
	ip_queue_xmit((struct sock *)NULL, ndev, skb2, 1);
#endif
	skb->sk = NULL;
	kfree_skb(skb, FREE_READ);
}


/* 
 *	Deal with incoming ICMP packets. 
 */
 
int icmp_rcv(struct sk_buff *skb1, struct device *dev, struct options *opt,
	 unsigned long daddr, unsigned short len,
	 unsigned long saddr, int redo, struct inet_protocol *protocol)
{
	struct icmphdr *icmph;
	unsigned char *buff;

	/*
	 *	Drop broadcast packets. IP has done a broadcast check and ought one day
	 *	to pass on that information.
	 */
	
	icmp_statistics.IcmpInMsgs++;
	 
  	
  	/*
  	 *	Grab the packet as an icmp object
  	 */

	buff = skb1->h.raw;
	icmph = (struct icmphdr *) buff;

	/*
	 *	Validate the packet first 
	 */

	if (ip_compute_csum((unsigned char *) icmph, len)) 
	{
		/* Failed checksum! */
		icmp_statistics.IcmpInErrors++;
		printk("ICMP: failed checksum from %s!\n", in_ntoa(saddr));
		kfree_skb(skb1, FREE_READ);
		return(0);
	}

	/*
	 *	Parse the ICMP message 
	 */

	if (ip_chk_addr(daddr) != IS_MYADDR)
	{
		if (icmph->type != ICMP_ECHO) 
		/* RFC1122: We're allowed to reply to ICMP_TIMESTAMP */
		/* requests in the same manner as ICMP_ECHO (optionally */
		/* drop those to a bcast/mcast), so perhaps we should be */
		/* consistent? -- MS */

		{
			icmp_statistics.IcmpInErrors++;
			kfree_skb(skb1, FREE_READ);
			return(0);
  		}
		daddr=dev->pa_addr;
	}

	switch(icmph->type) 
	{
		case ICMP_TIME_EXCEEDED:
			icmp_statistics.IcmpInTimeExcds++;
			icmp_unreach(icmph, skb1);
			return 0;
		case ICMP_DEST_UNREACH:
			icmp_statistics.IcmpInDestUnreachs++;
			icmp_unreach(icmph, skb1);
			return 0;
		case ICMP_SOURCE_QUENCH:
			icmp_statistics.IcmpInSrcQuenchs++;
			icmp_unreach(icmph, skb1);
			return(0);
		case ICMP_REDIRECT:
			icmp_statistics.IcmpInRedirects++;
			icmp_redirect(icmph, skb1, dev, saddr);
			return(0);
		case ICMP_ECHO: 
			icmp_statistics.IcmpInEchos++;
			icmp_echo(icmph, skb1, dev, saddr, daddr, len, opt);
			return 0;
		case ICMP_ECHOREPLY:
			icmp_statistics.IcmpInEchoReps++;
			kfree_skb(skb1, FREE_READ);
			return(0);
		case ICMP_TIMESTAMP:
			icmp_statistics.IcmpInTimestamps++;
			icmp_timestamp(icmph, skb1, dev, saddr, daddr, len, opt);
			return 0;
		case ICMP_TIMESTAMPREPLY:
		/* RFC1122: MUST pass TIMESTAMPREPLY messages up to app layer, */
		/* just as with ECHOREPLY.  You have to use raw to get that */
		/* functionality, just as with ECHOREPLY. Close enough. -- MS */
			icmp_statistics.IcmpInTimestampReps++;
			kfree_skb(skb1,FREE_READ);
			return 0;
		/* INFO is obsolete and doesn't even feature in the SNMP stats */
		case ICMP_INFO_REQUEST:
			icmp_info(icmph, skb1, dev, saddr, daddr, len, opt);
			return 0;
		case ICMP_INFO_REPLY:
			skb1->sk = NULL;
			kfree_skb(skb1, FREE_READ);
			return(0);
		case ICMP_ADDRESS:
			icmp_statistics.IcmpInAddrMasks++;
			icmp_address(icmph, skb1, dev, saddr, daddr, len, opt);
			return 0;
		case ICMP_ADDRESSREPLY:
			/*
			 *	We ought to set our netmask on receiving this, but 
			 *	experience shows it's a waste of effort.
			 */
			icmp_statistics.IcmpInAddrMaskReps++;
			kfree_skb(skb1, FREE_READ);
			return(0);
		default:
			/* RFC1122: OK.  Silently discarding weird ICMP (MUST), */
			/* as per 3.2.2. -- MS */
			icmp_statistics.IcmpInErrors++;
			kfree_skb(skb1, FREE_READ);
			return(0);
 	}
  /*NOTREACHED*/
	kfree_skb(skb1, FREE_READ);
	return(-1);
}


/*
 *	Perform any ICMP-related I/O control requests. 
 *	[to vanish soon]
 */
 
int icmp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
  	switch(cmd) 
  	{
		default:
			return(-EINVAL);
  	}
 	return(0);
}
