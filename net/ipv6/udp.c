/*
 *	UDP over IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Based on linux/ipv4/udp.c
 *
 *	$Id: udp.c,v 1.6 1996/10/16 18:34:16 roque Exp $
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
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>
#include <net/ip.h>
#include <net/udp.h>

#include <net/checksum.h>

struct udp_mib udp_stats_in6;

/*
 *
 */

int udpv6_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in6	*usin = (struct sockaddr_in6 *) uaddr;
	struct in6_addr		*daddr;
	struct dest_entry	*dest;
	struct ipv6_pinfo      	*np;
	struct inet6_ifaddr	*ifa;
	int			addr_type;

	if (addr_len < sizeof(*usin)) 
	  	return(-EINVAL);

	if (usin->sin6_family && usin->sin6_family != AF_INET6) 
	  	return(-EAFNOSUPPORT);

	addr_type = ipv6_addr_type(&usin->sin6_addr);
	np = &sk->net_pinfo.af_inet6;

	if (addr_type == IPV6_ADDR_ANY)
	{
		/*
		 *	connect to self
		 */
		usin->sin6_addr.s6_addr[15] = 0x01;
	}

	daddr = &usin->sin6_addr;

	if (addr_type == IPV6_ADDR_MAPPED)
	{
		struct sockaddr_in sin;
		int err;

		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = daddr->s6_addr32[3];

		err = udp_connect(sk, (struct sockaddr*) &sin, sizeof(sin));
		
		if (err < 0)
		{
			return err;
		}
		
		ipv6_addr_copy(&np->daddr, daddr);
		
		if(ipv6_addr_any(&np->saddr))
		{
			ipv6_addr_set(&np->saddr, 0, 0, 
				      __constant_htonl(0x0000ffff),
				      sk->saddr);

		}

		if(ipv6_addr_any(&np->rcv_saddr))
		{
			ipv6_addr_set(&np->rcv_saddr, 0, 0, 
				      __constant_htonl(0x0000ffff),
				      sk->rcv_saddr);
		}

	}

	ipv6_addr_copy(&np->daddr, daddr);

	/*
	 *	Check for a route to destination an obtain the
	 *	destination cache for it.
	 */

	dest = ipv6_dst_route(daddr, NULL, sk->localroute ? RTI_GATEWAY : 0);

	np->dest = dest;

	if (dest == NULL)
		return -ENETUNREACH;

	/* get the source adddress used in the apropriate device */

	ifa = ipv6_get_saddr((struct rt6_info *) dest, daddr);

	if(ipv6_addr_any(&np->saddr))
	{
		ipv6_addr_copy(&np->saddr, &ifa->addr);
	}

	if(ipv6_addr_any(&np->rcv_saddr))
	{
		ipv6_addr_copy(&np->rcv_saddr, &ifa->addr);
		sk->rcv_saddr = 0xffffffff;
	}

	sk->dummy_th.dest = usin->sin6_port;

	sk->state = TCP_ESTABLISHED;

	return(0);
}

static void udpv6_close(struct sock *sk, unsigned long timeout)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;

	lock_sock(sk);
	sk->state = TCP_CLOSE;

	if (np->dest)
	{
		ipv6_dst_unlock(np->dest);
	}

	release_sock(sk);
	destroy_sock(sk);
}

/*
 * 	This should be easy, if there is something there we
 * 	return it, otherwise we block.
 */

int udpv6_recvmsg(struct sock *sk, struct msghdr *msg, int len,
		  int noblock, int flags, int *addr_len)
{
  	int copied = 0;
  	int truesize;
  	struct sk_buff *skb;
  	int err;
  	

	/*
	 *	Check any passed addresses
	 */
	 
  	if (addr_len) 
  		*addr_len=sizeof(struct sockaddr_in6);
  
	/*
	 *	From here the generic datagram does a lot of the work. Come
	 *	the finished NET3, it will do _ALL_ the work!
	 */
	 	
	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if(skb==NULL)
  		return err;
  
  	truesize = skb->tail - skb->h.raw - sizeof(struct udphdr);
  	
  	copied=truesize;
  	if(copied>len)
  	{
  		copied=len;
  		msg->msg_flags|=MSG_TRUNC;
  	}

  	/*
  	 *	FIXME : should use udp header size info value 
  	 */
  	 
	err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), 
				      msg->msg_iov, copied);
	if (err)
		return err; 
	
	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (msg->msg_name) 
	{
		struct sockaddr_in6 *sin6;
	  
		sin6 = (struct sockaddr_in6 *) msg->msg_name;
		
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = skb->h.uh->source;

		if (skb->protocol == __constant_htons(ETH_P_IP))
		{
			ipv6_addr_set(&sin6->sin6_addr, 0, 0,
				      __constant_htonl(0xffff), skb->nh.iph->daddr);
		}
		else
		{
			memcpy(&sin6->sin6_addr, &skb->nh.ipv6h->saddr,
			       sizeof(struct in6_addr));

			if (msg->msg_control)
			{
				int err;

				err = datagram_recv_ctl(sk, msg, skb);

				if (err < 0)
				{
					copied = err;
				}
			}
		}
  	}
	
  	skb_free_datagram(sk, skb);
  	return(copied);
}

void udpv6_err(int type, int code, unsigned char *buff, __u32 info,
	       struct in6_addr *saddr, struct in6_addr *daddr,
	       struct inet6_protocol *protocol)
{
	struct sock *sk;
	struct udphdr *uh;
	int err;
	
	uh = (struct udphdr *) buff;

	sk = inet6_get_sock(&udpv6_prot, daddr, saddr, uh->source, uh->dest);
   
	if (sk == NULL)
	{
		printk(KERN_DEBUG "icmp for unkown sock\n");
		return;
	}

	if (icmpv6_err_convert(type, code, &err))
	{
		if(sk->bsdism && sk->state!=TCP_ESTABLISHED)
			return;
		
		sk->err = err;
		sk->error_report(sk);
	}
	else
		sk->err_soft = err;
}

static inline int udpv6_queue_rcv_skb(struct sock * sk, struct sk_buff *skb)
{

	if (sock_queue_rcv_skb(sk,skb)<0) {
		udp_stats_in6.UdpInErrors++;
		ipv6_statistics.Ip6InDiscards++;
		ipv6_statistics.Ip6InDelivers--;
		skb->sk = NULL;
		kfree_skb(skb, FREE_WRITE);
		return 0;
	}
	udp_stats_in6.UdpInDatagrams++;
	return 0;
}

int udpv6_rcv(struct sk_buff *skb, struct device *dev,
	      struct in6_addr *saddr, struct in6_addr *daddr,
	      struct ipv6_options *opt, unsigned short len,
	      int redo, struct inet6_protocol *protocol)
{
	struct sock *sk;
  	struct udphdr *uh;
	int ulen;

	/*
	 *	check if the address is ours...
	 *	I believe that this is being done in IP layer
	 */

	uh = (struct udphdr *) skb->h.uh;
  	
  	ipv6_statistics.Ip6InDelivers++;

	ulen = ntohs(uh->len);
	
	if (ulen > len || len < sizeof(*uh))
	{
		printk(KERN_DEBUG "UDP: short packet: %d/%d\n", ulen, len);
		udp_stats_in6.UdpInErrors++;
		kfree_skb(skb, FREE_READ);
		return(0);
	}

	if (uh->check == 0)
	{
		printk(KERN_DEBUG "IPv6: udp checksum is 0\n");
		goto discard;
	}

	switch (skb->ip_summed) {
	case CHECKSUM_NONE:
		skb->csum = csum_partial((char*)uh, len, 0);
	case CHECKSUM_HW:
		if (csum_ipv6_magic(saddr, daddr, len, IPPROTO_UDP, skb->csum))
		{
			printk(KERN_DEBUG "IPv6: udp checksum error\n");
			goto discard;
		}
	}
	
	len = ulen;

	/* 
	 *	Multicast receive code 
	 */
	if (ipv6_addr_type(daddr) & IPV6_ADDR_MULTICAST)
	{
		struct sock *sk2;
		int lport;
		
		lport = ntohs(uh->dest);
		sk = udpv6_prot.sock_array[lport & (SOCK_ARRAY_SIZE-1)];

		sk = inet6_get_sock_mcast(sk, lport, uh->source,
					  daddr, saddr);

		if (sk)
		{
			sk2 = sk;
			
			while ((sk2 = inet6_get_sock_mcast(sk2->next, lport,
							   uh->source,
							   daddr, saddr)))
			{
				struct sk_buff *buff;

				buff = skb_clone(skb, GFP_ATOMIC);

				if (sock_queue_rcv_skb(sk, buff) < 0) 
				{
					buff->sk = NULL;
					kfree_skb(buff, FREE_READ);
				}
			}
		}
		if (!sk || sock_queue_rcv_skb(sk, skb) < 0)
		{
			skb->sk = NULL;
			kfree_skb(skb, FREE_READ);
		}
		return 0;
	}

	/* Unicast */
	
	/* 
	 * check socket cache ... must talk to Alan about his plans
	 * for sock caches... i'll skip this for now.
	 */

	sk = inet6_get_sock(&udpv6_prot, daddr, saddr, uh->dest, uh->source);

	if (sk == NULL)
	{
		udp_stats_in6.UdpNoPorts++;

		icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH,
			    0, dev);
		
		kfree_skb(skb, FREE_READ);
		return(0);
	}

	/* deliver */

	if (sk->users)
	{
		__skb_queue_tail(&sk->back_log, skb);
	}
	else
	{
		udpv6_queue_rcv_skb(sk, skb);
	}
	
	return(0);

  discard:
	udp_stats_in6.UdpInErrors++;
	kfree_skb(skb, FREE_READ);
	return(0);	
}

/*
 *	Sending
 */

struct udpv6fakehdr 
{
	struct udphdr	uh;
	struct iovec	*iov;
	__u32		wcheck;
	__u32		pl_len;
	struct in6_addr *daddr;
};

/*
 *	with checksum
 */

static int udpv6_getfrag(const void *data, struct in6_addr *addr,
			 char *buff, unsigned int offset, unsigned int len)
{
	struct udpv6fakehdr *udh = (struct udpv6fakehdr *) data;
	char *dst;
	int final = 0;
	int clen = len;

	dst = buff;

	if (offset)
	{
		offset -= sizeof(struct udphdr);
	}
	else
	{
		dst += sizeof(struct udphdr);
		final = 1;
		clen -= sizeof(struct udphdr);
	}

	udh->wcheck = csum_partial_copy_fromiovecend(dst, udh->iov, offset,
						     clen, udh->wcheck);

	if (final)
	{
		struct in6_addr *daddr;
		
		udh->wcheck = csum_partial((char *)udh, sizeof(struct udphdr),
					   udh->wcheck);

		if (udh->daddr)
		{
			daddr = udh->daddr;
		}
		else
		{
			/*
			 *	use packet destination address
			 *	this should improve cache locality
			 */
			daddr = addr + 1;
		}
		udh->uh.check = csum_ipv6_magic(addr, daddr,
						udh->pl_len, IPPROTO_UDP,
						udh->wcheck);
		if (udh->uh.check == 0)
			udh->uh.check = -1;

		memcpy(buff, udh, sizeof(struct udphdr));
	}
	return 0;
}

static int udpv6_sendmsg(struct sock *sk, struct msghdr *msg, int ulen)
{
	
	struct ipv6_options opt_space;
	struct udpv6fakehdr udh;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) msg->msg_name;
	struct ipv6_options *opt = NULL;
	struct device *dev = NULL;
	int addr_len = msg->msg_namelen;
	struct in6_addr *daddr;
	struct in6_addr *saddr = NULL;
	int len = ulen + sizeof(struct udphdr);
	int addr_type;
	int hlimit = 0;
	int err;

	
	if (msg->msg_flags & ~(MSG_DONTROUTE|MSG_DONTWAIT))
		return(-EINVAL);

	if (sin6)
	{
		if (addr_len < sizeof(*sin6))
			return(-EINVAL);
		
		if (sin6->sin6_family && sin6->sin6_family != AF_INET6)
			return(-EINVAL);

		if (sin6->sin6_port == 0)
			return(-EINVAL);
	       
		udh.uh.dest = sin6->sin6_port;
		daddr = &sin6->sin6_addr;

		if (np->dest && ipv6_addr_cmp(daddr, &np->daddr))
		{
			ipv6_dst_unlock(np->dest);
			np->dest = NULL;
		}
	}
	else
	{
		if (sk->state != TCP_ESTABLISHED)
			return(-EINVAL);
		
		udh.uh.dest = sk->dummy_th.dest;
		daddr = &sk->net_pinfo.af_inet6.daddr;
	}

	addr_type = ipv6_addr_type(daddr);

	if (addr_type == IPV6_ADDR_MAPPED)
	{
		struct sockaddr_in sin;
		
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = daddr->s6_addr32[3];

		return udp_sendmsg(sk, msg, len);
	}

	udh.daddr = NULL;
	
	if (msg->msg_control)
	{
		opt = &opt_space;
		memset(opt, 0, sizeof(struct ipv6_options));

		err = datagram_send_ctl(msg, &dev, &saddr, opt, &hlimit);
		if (err < 0)
		{
			printk(KERN_DEBUG "invalid msg_control\n");
			return err;
		}
		
		if (opt->srcrt)
		{			
			udh.daddr = daddr;
		}
	}
	
	udh.uh.source = sk->dummy_th.source;
	udh.uh.len = htons(len);
	udh.uh.check = 0;
	udh.iov = msg->msg_iov;
	udh.wcheck = 0;
	udh.pl_len = len;
	
	err = ipv6_build_xmit(sk, udpv6_getfrag, &udh, daddr, len,
			      saddr, dev, opt, IPPROTO_UDP, hlimit,
			      msg->msg_flags&MSG_DONTWAIT);
	
	if (err < 0)
		return err;

	udp_stats_in6.UdpOutDatagrams++;
	return ulen;
}

static struct inet6_protocol udpv6_protocol = 
{
	udpv6_rcv,		/* UDP handler		*/
	udpv6_err,		/* UDP error control	*/
	NULL,			/* next			*/
	IPPROTO_UDP,		/* protocol ID		*/
	0,			/* copy			*/
	NULL,			/* data			*/
	"UDPv6"			/* name			*/
};


struct proto udpv6_prot = {
	udpv6_close,
	udpv6_connect,
	NULL,
	NULL,
	NULL,
	NULL,
	datagram_select,
	udp_ioctl,
	NULL,
	NULL,
	NULL,
	ipv6_setsockopt,
	ipv6_getsockopt,
	udpv6_sendmsg,
	udpv6_recvmsg,
	NULL,		/* No special bind function */
	udpv6_queue_rcv_skb,
	128,
	0,
	"UDP",
	0, 0,
	NULL
};

void udpv6_init(void)
{
	inet6_add_protocol(&udpv6_protocol);
}
