/*
 *	Internet Control Message Protocol (ICMPv6)
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on net/ipv4/icmp.c
 *
 *	RFC 1885
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

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/skbuff.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/icmpv6.h>

#include <net/ip.h>
#include <net/sock.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/ndisc.h>
#include <net/raw.h>
#include <net/inet_common.h>
#include <net/transp_v6.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>
#include <net/rawv6.h>

#include <asm/uaccess.h>
#include <asm/system.h>

/*
 *	ICMP socket for flow control.
 */

static struct socket icmpv6_socket;

int icmpv6_rcv(struct sk_buff *skb, struct device *dev,
	       struct in6_addr *saddr, struct in6_addr *daddr,
	       struct ipv6_options *opt, unsigned short len,
	       int redo, struct inet6_protocol *protocol);

static struct inet6_protocol icmpv6_protocol = 
{
	icmpv6_rcv,		/* handler		*/
	NULL,			/* error control	*/
	NULL,			/* next			*/
	IPPROTO_ICMPV6,		/* protocol ID		*/
	0,			/* copy			*/
	NULL,			/* data			*/
	"ICMPv6"	       	/* name			*/
};



struct icmpv6_msg {
	struct icmpv6hdr	icmph;
	__u8 			*data;
	struct in6_addr		*daddr;
	int			len;
	__u32			csum;
};



/*
 *	getfrag callback
 *	not static because it's needed in ndisc.c
 */

static int icmpv6_getfrag(const void *data, struct in6_addr *saddr, 
			   char *buff, unsigned int offset, unsigned int len)
{
	struct icmpv6_msg *msg = (struct icmpv6_msg *) data;
	struct icmpv6hdr *icmph;
	__u32 csum;

	/* 
	 *	in theory offset must be 0 since we never send more 
	 *	than 576 bytes on an error or more than the path mtu
	 *	on an echo reply. (those are the rules on RFC 1883)
	 */

	if (offset)
	{
		csum = csum_partial_copy((void *) msg->data +
					 offset - sizeof(struct icmpv6hdr), 
					 buff, len, msg->csum);
		msg->csum = csum;
		return 0;
	}

	csum = csum_partial_copy((void *) &msg->icmph, buff,
				 sizeof(struct icmpv6hdr), msg->csum);

	csum = csum_partial_copy((void *) msg->data, 
				 buff + sizeof(struct icmpv6hdr),
				 len - sizeof(struct icmpv6hdr), csum);

	icmph = (struct icmpv6hdr *) buff;

	icmph->checksum = csum_ipv6_magic(saddr, msg->daddr, msg->len,
					  IPPROTO_ICMPV6, csum);
	return 0; 
}

/*
 *	an inline helper for the "simple" if statement bellow
 *	checks if parameter problem report is caused by an
 *	unrecognized IPv6 option that has the Option Type 
 *	highest-order two bits set to 10
 */
static __inline__ int opt_unrec(struct sk_buff *skb, __u32 offset)
{
	char *buff = (char *) skb->ipv6_hdr;

	return ( ( *(buff + offset) & 0xC0 ) == 0x80 );
}

/*
 *	Send an ICMP message in response to a packet in error
 */

void icmpv6_send(struct sk_buff *skb, int type, int code, __u32 info, 
		 struct device *dev)
{
	struct ipv6hdr *hdr = skb->ipv6_hdr;
	struct sock *sk = (struct sock *) icmpv6_socket.data;
	struct in6_addr *saddr = NULL;
	struct device *src_dev = NULL;
	struct icmpv6_msg msg;
	int addr_type = 0;
	int optlen;
	int len;

	/*
	 *	sanity check pointer in case of parameter problem
	 */

	if (type == ICMPV6_PARAMETER_PROB && 
	    (info > (skb->tail - ((unsigned char *) hdr))))
	{
		printk(KERN_DEBUG "icmpv6_send: bug! pointer > skb\n");
		return;
	}

	/*
	 *	Make sure we respect the rules 
	 *	i.e. RFC 1885 2.4(e)
	 *	Rule (e.1) is enforced by not using icmpv6_send
	 *	in any code that processes icmp errors.
	 */
	
	addr_type = ipv6_addr_type(&hdr->daddr);

	if (ipv6_chk_addr(&hdr->daddr))
	{
		saddr = &hdr->daddr;
	}

	/*
	 *	Dest addr check
	 */

	if ((addr_type & IPV6_ADDR_MULTICAST || skb->pkt_type != PACKET_HOST))
	{
		if (type != ICMPV6_PKT_TOOBIG &&
		    !(type == ICMPV6_PARAMETER_PROB && 
		      code == ICMPV6_UNK_OPTION && 
		      (opt_unrec(skb, info))))
		{
			return;
		}

		saddr = NULL;
	}

	addr_type = ipv6_addr_type(&hdr->saddr);

	/*
	 *	Source addr check
	 */

	if (addr_type & IPV6_ADDR_LINKLOCAL)
	{
		src_dev = skb->dev;
	}

	/*
	 *	Must not send if we know that source is Anycast also.
	 *	for now we don't know that.
	 */
	if ((addr_type == IPV6_ADDR_ANY) || (addr_type & IPV6_ADDR_MULTICAST))
	{
		printk(KERN_DEBUG "icmpv6_send: addr_any/mcast source\n");
		return;
	}

	/*
	 *	ok. kick it. checksum will be provided by the 
	 *	getfrag_t callback.
	 */

	msg.icmph.type = type;
	msg.icmph.code = code;
	msg.icmph.checksum = 0;
	msg.icmph.icmp6_pointer = htonl(info);

	msg.data = (__u8 *) skb->ipv6_hdr;
	msg.csum = 0;
	msg.daddr = &hdr->saddr;
        /*
	if (skb->opt)
		optlen = skb->opt->optlen;
	else
	*/

	optlen = 0;

	len = min(skb->tail - ((unsigned char *) hdr), 
		  576 - sizeof(struct ipv6hdr) - sizeof(struct icmpv6hdr)
		  - optlen);

	if (len < 0)
	{
		printk(KERN_DEBUG "icmp: len problem\n");
		return;
	}

	len += sizeof(struct icmpv6hdr);

	msg.len = len;


	ipv6_build_xmit(sk, icmpv6_getfrag, &msg, &hdr->saddr, len,
			saddr, src_dev, NULL, IPPROTO_ICMPV6, 1);
}

static void icmpv6_echo_reply(struct sk_buff *skb)
{
	struct sock *sk = (struct sock *) icmpv6_socket.data;
	struct ipv6hdr *hdr = skb->ipv6_hdr;
	struct icmpv6hdr *icmph = (struct icmpv6hdr *) skb->h.raw;
	struct in6_addr *saddr;
	struct icmpv6_msg msg;
	unsigned char *data;
	int len;

	data = (char *) (icmph + 1);

	saddr = &hdr->daddr;

	if (ipv6_addr_type(saddr) & IPV6_ADDR_MULTICAST)
		saddr = NULL;

	len = skb->tail - data;
	len += sizeof(struct icmpv6hdr);

	msg.icmph.type = ICMPV6_ECHO_REPLY;
	msg.icmph.code = 0;
	msg.icmph.checksum = 0;
	msg.icmph.icmp6_identifier = icmph->icmp6_identifier;
	msg.icmph.icmp6_sequence = icmph->icmp6_sequence;

	msg.data = data;
	msg.csum = 0;
	msg.len = len;
	msg.daddr = &hdr->saddr;
       
	ipv6_build_xmit(sk, icmpv6_getfrag, &msg, &hdr->saddr, len, saddr,
			skb->dev, NULL, IPPROTO_ICMPV6, 1);
}

static __inline__ int ipv6_ext_hdr(u8 nexthdr)
{
	/* 
	 * find out if nexthdr is an extension header or a protocol
	 */
	return ( (nexthdr == NEXTHDR_HOP)	||
		 (nexthdr == NEXTHDR_ROUTING)	||
		 (nexthdr == NEXTHDR_FRAGMENT)	||
		 (nexthdr == NEXTHDR_ESP)	||
		 (nexthdr == NEXTHDR_AUTH)	||
		 (nexthdr == NEXTHDR_NONE)	||
		 (nexthdr == NEXTHDR_DEST) );
		 
}

static void icmpv6_notify(int type, int code, unsigned char *buff, int len,
			  struct in6_addr *saddr, struct in6_addr *daddr, 
			  struct inet6_protocol *protocol)
{
	struct ipv6hdr *hdr = (struct ipv6hdr *) buff;
	struct inet6_protocol *ipprot;
	struct sock *sk;
	char * pbuff;
	__u32 info = 0;
	int hash;
	u8 nexthdr;

	/* now skip over extension headers */

	nexthdr = hdr->nexthdr;

	pbuff = (char *) (hdr + 1);
	len -= sizeof(struct ipv6hdr);

	while (ipv6_ext_hdr(nexthdr)) 
	{
		int hdrlen;

		if (nexthdr == NEXTHDR_NONE)
			return;

		nexthdr = *pbuff;
		hdrlen = *(pbuff+1);

		if (((hdrlen + 1) << 3) > len)
			return;
		
		pbuff += hdrlen;
		len -= hdrlen;
	}

	hash = nexthdr & (MAX_INET_PROTOS -1);

	for (ipprot = (struct inet6_protocol *) inet6_protos[hash]; 
	     ipprot != NULL; 
	     ipprot=(struct inet6_protocol *)ipprot->next)
	{
		if (ipprot->protocol != nexthdr)
			continue;

		if (ipprot->err_handler) 
		{
			ipprot->err_handler(type, code, pbuff, info,
					    saddr, daddr, ipprot);
		}
		return;
	}

	/* delivery to upper layer protocols failed. try raw sockets */

	sk = rawv6_prot.sock_array[hash];

	if (sk == NULL)
	{
		return;
	}

	while ((sk = inet6_get_sock_raw(sk, nexthdr, daddr, saddr)))
	{
		rawv6_err(sk, type, code, pbuff, saddr, daddr);
		sk = sk->next;
	}

	return;
}
  
/*
 *	Handle icmp messages
 */

int icmpv6_rcv(struct sk_buff *skb, struct device *dev,
	       struct in6_addr *saddr, struct in6_addr *daddr,
	       struct ipv6_options *opt, unsigned short len,
	       int redo, struct inet6_protocol *protocol)
{
	struct ipv6hdr *orig_hdr;
	struct icmpv6hdr *hdr = (struct icmpv6hdr *) skb->h.raw;
	int ulen;

	/* perform checksum */


	switch (skb->ip_summed) {	
	case CHECKSUM_NONE:
		skb->csum = csum_partial((char *)hdr, len, 0);
	case CHECKSUM_HW:
		if (csum_ipv6_magic(saddr, daddr, len, IPPROTO_ICMPV6, 
				    skb->csum))
		{
			printk(KERN_DEBUG "icmpv6 checksum failed\n");
			goto discard_it;
		}
	default:
		/* CHECKSUM_UNNECESSARY */
	}

	/*
	 *	length of original packet carried in skb
	 */
	ulen = skb->tail - (unsigned char *) (hdr + 1);
	
	switch (hdr->type) {

	case ICMPV6_ECHO_REQUEST:
		icmpv6_echo_reply(skb);
		break;

	case ICMPV6_ECHO_REPLY:
		/* we coulnd't care less */
		break;

	case ICMPV6_PKT_TOOBIG:
		orig_hdr = (struct ipv6hdr *) (hdr + 1);
		if (ulen >= sizeof(struct ipv6hdr))
		{
			rt6_handle_pmtu(&orig_hdr->daddr,
					ntohl(hdr->icmp6_mtu));
		}

		/*
		 * Drop through to notify
		 */

	case ICMPV6_DEST_UNREACH:
	case ICMPV6_TIME_EXCEEDED:
	case ICMPV6_PARAMETER_PROB:

		icmpv6_notify(hdr->type, hdr->code, (char *) (hdr + 1), ulen,
			      saddr, daddr, protocol);
		break;

	case NDISC_ROUTER_SOLICITATION:
	case NDISC_ROUTER_ADVERTISEMENT:
	case NDISC_NEIGHBOUR_SOLICITATION:
	case NDISC_NEIGHBOUR_ADVERTISEMENT:
	case NDISC_REDIRECT:
		ndisc_rcv(skb, dev, saddr, daddr, opt, len);		
		break;

	case ICMPV6_MEMBERSHIP_QUERY:
	case ICMPV6_MEMBERSHIP_REPORT:
	case ICMPV6_MEMBERSHIP_REDUCTION:
		/* forward the packet to the igmp module */
		break;

	default:
		printk(KERN_DEBUG "icmpv6: msg of unkown type\n");
		
		/* informational */
		if (hdr->type & 0x80)
		{
			goto discard_it;
		}

		/* 
		 * error of unkown type. 
		 * must pass to upper level 
		 */

		icmpv6_notify(hdr->type, hdr->code, (char *) (hdr + 1), ulen,
			      saddr, daddr, protocol);	
	}

  discard_it:

	kfree_skb(skb, FREE_READ);
	return 0;
}

void icmpv6_init(struct proto_ops *ops)
{
	struct sock *sk;
	int err;

	icmpv6_socket.type=SOCK_RAW;
	icmpv6_socket.ops=ops;

	if((err=ops->create(&icmpv6_socket, IPPROTO_ICMPV6))<0)
		printk(KERN_DEBUG 
		       "Failed to create the ICMP control socket.\n");

	MOD_DEC_USE_COUNT;

	sk = icmpv6_socket.data;
	sk->allocation = GFP_ATOMIC;
	sk->num = 256;			/* Don't receive any data */

	inet6_add_protocol(&icmpv6_protocol);
}

static struct icmp6_err {
	int err;
	int fatal;
} tab_unreach[] = {
	{ ENETUNREACH,	0},	/* NOROUTE		*/
	{ EACCES,	1},	/* ADM_PROHIBITED	*/
	{ EOPNOTSUPP,	1},	/* NOT_NEIGHBOUR	*/
	{ EHOSTUNREACH,	0},	/* ADDR_UNREACH		*/
	{ ECONNREFUSED,	1},	/* PORT_UNREACH		*/
};

int icmpv6_err_convert(int type, int code, int *err)
{
	int fatal = 0;

	*err = 0;

	switch (type) {
	case ICMPV6_DEST_UNREACH:
		if (code <= ICMPV6_PORT_UNREACH)
		{
			*err  = tab_unreach[code].err;
			fatal = tab_unreach[code].fatal;
		}
		break;

	case ICMPV6_PKT_TOOBIG:
		*err = EMSGSIZE;
		break;
		
	case ICMPV6_PARAMETER_PROB:
		*err = EPROTO;
		fatal = 1;
		break;
	};

	return fatal;
}

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce -pipe -m486 -DCPU=486 -DMODULE -DMODVERSIONS -include /usr/src/linux/include/linux/modversions.h  -c -o icmp.o icmp.c"
 * End:
 */
