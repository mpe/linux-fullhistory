/*
 *	RAW sockets for IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Adapted from linux/net/ipv4/raw.c
 *
 *	$Id: raw.c,v 1.8 1997/02/28 09:56:34 davem Exp $
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
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ip.h>
#include <net/udp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>
#include <net/transp_v6.h>

#include <net/rawv6.h>

#include <asm/uaccess.h>

struct sock *raw_v6_htable[RAWV6_HTABLE_SIZE];

static void raw_v6_hash(struct sock *sk)
{
	struct sock **skp;
	int num = sk->num;

	num &= (RAWV6_HTABLE_SIZE - 1);
	skp = &raw_v6_htable[num];
	SOCKHASH_LOCK();
	sk->next = *skp;
	*skp = sk;
	sk->hashent = num;
	SOCKHASH_UNLOCK();
}

static void raw_v6_unhash(struct sock *sk)
{
	struct sock **skp;
	int num = sk->num;

	num &= (RAWV6_HTABLE_SIZE - 1);
	skp = &raw_v6_htable[num];

	SOCKHASH_LOCK();
	while(*skp != NULL) {
		if(*skp == sk) {
			*skp = sk->next;
			break;
		}
		skp = &((*skp)->next);
	}
	SOCKHASH_UNLOCK();
}

static void raw_v6_rehash(struct sock *sk)
{
	struct sock **skp;
	int num = sk->num;
	int oldnum = sk->hashent;

	num &= (RAWV6_HTABLE_SIZE - 1);
	skp = &raw_v6_htable[oldnum];

	SOCKHASH_LOCK();
	while(*skp != NULL) {
		if(*skp == sk) {
			*skp = sk->next;
			break;
		}
		skp = &((*skp)->next);
	}
	sk->next = raw_v6_htable[num];
	raw_v6_htable[num] = sk;
	sk->hashent = num;
	SOCKHASH_UNLOCK();
}

static int __inline__ inet6_mc_check(struct sock *sk, struct in6_addr *addr)
{
	struct ipv6_mc_socklist *mc;
		
	for (mc = sk->net_pinfo.af_inet6.ipv6_mc_list; mc; mc=mc->next) {
		if (ipv6_addr_cmp(&mc->addr, addr) == 0)
			return 1;
	}

	return 0;
}

/* Grumble... icmp and ip_input want to get at this... */
struct sock *raw_v6_lookup(struct sock *sk, unsigned short num,
			   struct in6_addr *loc_addr, struct in6_addr *rmt_addr)
{
	struct sock *s = sk;
	int addr_type = ipv6_addr_type(loc_addr);

	for(s = sk; s; s = s->next) {
		if((s->num == num) 		&&
		   !(s->dead && (s->state == TCP_CLOSE))) {
			struct ipv6_pinfo *np = &s->net_pinfo.af_inet6;

			if (!ipv6_addr_any(&np->daddr) &&
			    ipv6_addr_cmp(&np->daddr, rmt_addr))
				continue;

			if (!ipv6_addr_any(&np->rcv_saddr)) {
				if (ipv6_addr_cmp(&np->rcv_saddr, loc_addr) == 0)
					return(s);
				if ((addr_type & IPV6_ADDR_MULTICAST) &&
				    inet6_mc_check(s, loc_addr))
					return (s);
				continue;
			}
			return(s);
		}
	}
	return NULL;
}

/* This cleans up af_inet6 a bit. -DaveM */
static int rawv6_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in6 *addr = (struct sockaddr_in6 *) uaddr;
	__u32 v4addr = 0;
	int addr_type;

	/* Check these errors. */
	if (sk->state != TCP_CLOSE || (addr_len < sizeof(struct sockaddr_in6)))
		return -EINVAL;

	addr_type = ipv6_addr_type(&addr->sin6_addr);

	/* Check if the address belongs to the host. */
	if (addr_type == IPV6_ADDR_MAPPED) {
		v4addr = addr->sin6_addr.s6_addr32[3];
		if (__ip_chk_addr(v4addr) != IS_MYADDR)
			return(-EADDRNOTAVAIL);
	} else {
		if (addr_type != IPV6_ADDR_ANY) {
			/* ipv4 addr of the socket is invalid.  Only the
			 * unpecified and mapped address have a v4 equivalent.
			 */
			v4addr = LOOPBACK4_IPV6;
			if (!(addr_type & IPV6_ADDR_MULTICAST))	{
				if (ipv6_chk_addr(&addr->sin6_addr) == NULL)
					return(-EADDRNOTAVAIL);
			}
		}
	}

	sk->rcv_saddr = v4addr;
	sk->saddr = v4addr;
	memcpy(&sk->net_pinfo.af_inet6.rcv_saddr, &addr->sin6_addr, 
	       sizeof(struct in6_addr));
	if (!(addr_type & IPV6_ADDR_MULTICAST))
		memcpy(&sk->net_pinfo.af_inet6.saddr, &addr->sin6_addr, 
		       sizeof(struct in6_addr));
	return 0;
}

void rawv6_err(struct sock *sk, int type, int code, unsigned char *buff,
	       struct in6_addr *saddr, struct in6_addr *daddr)
{
	if (sk == NULL) 
		return;

}

static inline int rawv6_rcv_skb(struct sock * sk, struct sk_buff * skb)
{
	/* Charge it to the socket. */
	
	if (sock_queue_rcv_skb(sk,skb)<0)
	{
		/* ip_statistics.IpInDiscards++; */
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	/* ip_statistics.IpInDelivers++; */
	return 0;
}

/*
 *	This is next to useless... 
 *	if we demultiplex in network layer we don't need the extra call
 *	just to queue the skb... 
 *	maybe we could have the network decide uppon an hint if it 
 *	should call raw_rcv for demultiplexing
 */
int rawv6_rcv(struct sk_buff *skb, struct device *dev,
	      struct in6_addr *saddr, struct in6_addr *daddr,
	      struct ipv6_options *opt, unsigned short len)
{
	struct sock *sk;

	sk = skb->sk;

#if 1
/*
 *	It was wrong for IPv4. It breaks NRL too [ANK]
 *	Actually i think this is the option that  does make more 
 *	sense with IPv6 nested headers. [Pedro]
 */

	if (sk->ip_hdrincl)
	{
		skb->h.raw = skb->nh.raw;
	}
#else
        skb->h.raw = skb->nh.raw;
#endif

	if (sk->users) {
		__skb_queue_tail(&sk->back_log, skb);
		return 0;
	}

	rawv6_rcv_skb(sk, skb);
	return 0;
}


/*
 *	This should be easy, if there is something there
 *	we return it, otherwise we block.
 */

int rawv6_recvmsg(struct sock *sk, struct msghdr *msg, int len,
		  int noblock, int flags,int *addr_len)
{
	struct sockaddr_in6 *sin6=(struct sockaddr_in6 *)msg->msg_name;
	struct sk_buff *skb;
	int copied=0;
	int err;


	if (flags & MSG_OOB)
		return -EOPNOTSUPP;
		
	if (sk->shutdown & RCV_SHUTDOWN) 
		return(0);

	if (addr_len) 
		*addr_len=sizeof(*sin6);

	skb=skb_recv_datagram(sk, flags, noblock, &err);
	if(skb==NULL)
 		return err;
	
	copied = min(len, skb->tail - skb->h.raw);
	
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	sk->stamp=skb->stamp;

	if (err)
		return err;

	/* Copy the address. */
	if (sin6) 
	{
		sin6->sin6_family = AF_INET6;
		memcpy(&sin6->sin6_addr, &skb->nh.ipv6h->saddr, 
		       sizeof(struct in6_addr));

		*addr_len = sizeof(struct sockaddr_in6);
	}

	if (msg->msg_controllen)
		datagram_recv_ctl(sk, msg, skb);

	skb_free_datagram(sk, skb);
	return (copied);
}

/*
 *	Sending...
 */

struct rawv6_fakehdr {
	struct iovec	*iov;
	struct sock	*sk;
	__u32		len;
	__u32		cksum;
	__u32		proto;
	struct in6_addr *daddr;
};

static int rawv6_getfrag(const void *data, struct in6_addr *saddr, 
			  char *buff, unsigned int offset, unsigned int len)
{
	struct iovec *iov = (struct iovec *) data;

	return memcpy_fromiovecend(buff, iov, offset, len);
}

static int rawv6_frag_cksum(const void *data, struct in6_addr *addr,
			     char *buff, unsigned int offset, 
			     unsigned int len)
{
	struct rawv6_fakehdr *hdr = (struct rawv6_fakehdr *) data;
	
	hdr->cksum = csum_partial_copy_fromiovecend(buff, hdr->iov, offset, 
						    len, hdr->cksum);
	
	if (offset == 0)
	{
		struct sock *sk;
		struct raw6_opt *opt;
		struct in6_addr *daddr;
		
		sk = hdr->sk;
		opt = &sk->tp_pinfo.tp_raw;

		if (hdr->daddr)
		{
			daddr = hdr->daddr;
		}
		else
		{
			daddr = addr + 1;
		}
		
		hdr->cksum = csum_ipv6_magic(addr, daddr, hdr->len,
					     hdr->proto, hdr->cksum);
		
		if (opt->offset < len)
		{
			__u16 *csum;

			csum = (__u16 *) (buff + opt->offset);
			*csum = hdr->cksum;
		}
		else
		{
			/* 
			 *  FIXME 
			 *  signal an error to user via sk->err
			 */
			printk(KERN_DEBUG "icmp: cksum offset too big\n");
		}
	}	
	return 0; 
}


static int rawv6_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	struct ipv6_options opt_space;
	struct sockaddr_in6 * sin6 = (struct sockaddr_in6 *) msg->msg_name;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6_options *opt = NULL;
	struct device *dev = NULL;
	struct in6_addr *saddr = NULL;
	int addr_len = msg->msg_namelen;
	struct in6_addr *daddr;
	struct raw6_opt *raw_opt;
	u16 proto;
	int hlimit = 0;
	int err;
	

	/* Mirror BSD error message compatibility */
	if (msg->msg_flags & MSG_OOB)		
		return -EOPNOTSUPP;
			 
	if (msg->msg_flags & ~(MSG_DONTROUTE|MSG_DONTWAIT))
		return(-EINVAL);
	/*
	 *	Get and verify the address. 
	 */

	if (sin6) 
	{
		if (addr_len < sizeof(struct sockaddr_in6)) 
			return(-EINVAL);

		if (sin6->sin6_family && sin6->sin6_family != AF_INET6) 
			return(-EINVAL);
		
		/* port is the proto value [0..255] carried in nexthdr */
		proto = ntohs(sin6->sin6_port);

		if (!proto)
			proto = sk->num;

		if (proto > 255)
			return(-EINVAL);

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
		
		proto = sk->num;
		daddr = &(sk->net_pinfo.af_inet6.daddr);
	}

	if (ipv6_addr_any(daddr))
	{
		/* 
		 * unspecfied destination address 
		 * treated as error... is this correct ?
		 */
		return(-EINVAL);
	}

	/*
	 *	We don't allow > 64K sends yet.		 
	 */
	if (len + (sk->ip_hdrincl ? 0 : sizeof(struct ipv6hdr)) > 65535)
		return -EMSGSIZE;

	if (msg->msg_controllen)
	{
		opt = &opt_space;
		memset(opt, 0, sizeof(struct ipv6_options));

		err = datagram_send_ctl(msg, &dev, &saddr, opt, &hlimit);
		if (err < 0)
		{
			printk(KERN_DEBUG "invalid msg_control\n");
			return err;
		}		
	}

	raw_opt = &sk->tp_pinfo.tp_raw;

	
	if (raw_opt->checksum)
	{
		struct rawv6_fakehdr hdr;
		
		hdr.iov = msg->msg_iov;
		hdr.sk  = sk;
		hdr.len = len;
		hdr.cksum = 0;
		hdr.proto = proto;

		if (opt && opt->srcrt)
		{
			hdr.daddr = daddr;
		}
		else
		{
			hdr.daddr = NULL;
		}

		err = ipv6_build_xmit(sk, rawv6_frag_cksum, &hdr, daddr, len,
				      saddr, dev, opt, proto, hlimit,
				      msg->msg_flags&MSG_DONTWAIT);
	}
	else
	{
		err = ipv6_build_xmit(sk, rawv6_getfrag, msg->msg_iov, daddr,
				      len, saddr, dev, opt, proto, hlimit,
				      msg->msg_flags&MSG_DONTWAIT);
	}

	return err<0?err:len;
}

static int rawv6_seticmpfilter(struct sock *sk, int level, int optname, 
			       char *optval, int optlen)
{
	struct raw6_opt *opt = &sk->tp_pinfo.tp_raw;
	int err = 0;

	switch (optname) {
		case ICMPV6_FILTER:
			err = copy_from_user(&opt->filter, optval,
				       sizeof(struct icmp6_filter));
			if (err)
				err = -EFAULT;
			break;
		default:
			err = -ENOPROTOOPT;
	};

	return err;
}

static int rawv6_setsockopt(struct sock *sk, int level, int optname, 
			    char *optval, int optlen)
{
	struct raw6_opt *opt = &sk->tp_pinfo.tp_raw;
	int val, err;

	switch(level)
	{
		case SOL_RAW:
			break;

		case SOL_ICMPV6:
			if (sk->num != IPPROTO_ICMPV6)
				return -EOPNOTSUPP;
			return rawv6_seticmpfilter(sk, level, optname, optval,
						   optlen);
		case SOL_IPV6:
			if (optname == IPV6_CHECKSUM)
				break;
		default:
			return ipv6_setsockopt(sk, level, optname, optval,
					       optlen);
	}

  	if (optval == NULL)
  		return(-EINVAL);

  	err = get_user(val, (int *)optval);
  	if(err)
  		return err;

	switch (optname)
	{
		case IPV6_CHECKSUM:
			if (val < 0)
			{
				opt->checksum = 0;
			}
			else
			{
				opt->checksum = 1;
				opt->offset = val;
			}

			return 0;
			break;

		default:
			return(-ENOPROTOOPT);
	}
}

static void rawv6_close(struct sock *sk, unsigned long timeout)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;

	sk->state = TCP_CLOSE;

	if (np->dest)
	{
		ipv6_dst_unlock(np->dest);
	}

	destroy_sock(sk);
}

static int rawv6_init_sk(struct sock *sk)
{
	return(0);
}

struct proto rawv6_prot = {
	(struct sock *)&rawv6_prot,	/* sklist_next */
	(struct sock *)&rawv6_prot,	/* sklist_prev */
	rawv6_close,			/* close */
	udpv6_connect,			/* connect */
	NULL,				/* accept */
	NULL,				/* retransmit */
	NULL,				/* write_wakeup */
	NULL,				/* read_wakeup */
	datagram_poll,			/* poll */
	NULL,				/* ioctl */
	rawv6_init_sk,			/* init */
	NULL,				/* destroy */
	NULL,				/* shutdown */
	rawv6_setsockopt,		/* setsockopt */
	ipv6_getsockopt,		/* getsockopt - FIXME */
	rawv6_sendmsg,			/* sendmsg */
	rawv6_recvmsg,			/* recvmsg */
	rawv6_bind,			/* bind */
	rawv6_rcv_skb,			/* backlog_rcv */
	raw_v6_hash,			/* hash */
	raw_v6_unhash,			/* unhash */
	raw_v6_rehash,			/* rehash */
	NULL,				/* good_socknum */
	NULL,				/* verify_bind */
	128,				/* max_header */
	0,				/* retransmits */
	"RAW",				/* name */
	0,				/* inuse */
	0				/* highestinuse */
};

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce -pipe -m486 -DCPU=486 -DMODULE -DMODVERSIONS -include /usr/src/linux/include/linux/modversions.h  -c -o rawv6.o rawv6.c"
 *  c-file-style: "Linux"
 * End:
 */
