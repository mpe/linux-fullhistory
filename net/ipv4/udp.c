/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The User Datagram Protocol (UDP).
 *
 * Version:	@(#)udp.c	1.0.13	06/02/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Alan Cox, <Alan.Cox@linux.org>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() calls
 *		Alan Cox	: 	stopped close while in use off icmp
 *					messages. Not a fix but a botch that
 *					for udp at least is 'valid'.
 *		Alan Cox	:	Fixed icmp handling properly
 *		Alan Cox	: 	Correct error for oversized datagrams
 *		Alan Cox	:	Tidied select() semantics. 
 *		Alan Cox	:	udp_err() fixed properly, also now 
 *					select and read wake correctly on errors
 *		Alan Cox	:	udp_send verify_area moved to avoid mem leak
 *		Alan Cox	:	UDP can count its memory
 *		Alan Cox	:	send to an unknown connection causes
 *					an ECONNREFUSED off the icmp, but
 *					does NOT close.
 *		Alan Cox	:	Switched to new sk_buff handlers. No more backlog!
 *		Alan Cox	:	Using generic datagram code. Even smaller and the PEEK
 *					bug no longer crashes it.
 *		Fred Van Kempen	: 	Net2e support for sk->broadcast.
 *		Alan Cox	:	Uses skb_free_datagram
 *		Alan Cox	:	Added get/set sockopt support.
 *		Alan Cox	:	Broadcasting without option set returns EACCES.
 *		Alan Cox	:	No wakeup calls. Instead we now use the callbacks.
 *		Alan Cox	:	Use ip_tos and ip_ttl
 *		Alan Cox	:	SNMP Mibs
 *		Alan Cox	:	MSG_DONTROUTE, and 0.0.0.0 support.
 *		Matt Dillon	:	UDP length checks.
 *		Alan Cox	:	Smarter af_inet used properly.
 *		Alan Cox	:	Use new kernel side addressing.
 *		Alan Cox	:	Incorrect return on truncated datagram receive.
 *	Arnt Gulbrandsen 	:	New udp_send and stuff
 *		Alan Cox	:	Cache last socket
 *		Alan Cox	:	Route cache
 *		Jon Peatfield	:	Minor efficientcy fix to sendto().
 *		Mike Shaver	:	RFC1122 checks.
 *		Alan Cox	:	Nonblocking error fix.
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
 
/* RFC1122 Status:
   4.1.3.1 (Ports):
     SHOULD send ICMP_PORT_UNREACHABLE in reponse to datagrams to 
       an un-listened port. (OK)
   4.1.3.2 (IP Options)
     MUST pass IP options from IP -> application (OK)
     MUST allow application to specify IP options (OK)
   4.1.3.3 (ICMP Messages)
     MUST pass ICMP error messages to application (OK)
   4.1.3.4 (UDP Checksums)
     MUST provide facility for checksumming (OK)
     MAY allow application to control checksumming (OK)
     MUST default to checksumming on (OK)
     MUST discard silently datagrams with bad csums (OK)
   4.1.3.5 (UDP Multihoming)
     MUST allow application to specify source address (OK)
     SHOULD be able to communicate the chosen src addr up to application
       when application doesn't choose (NOT YET - doesnt seem to be in the BSD API)
       [Does opening a SOCK_PACKET and snooping your output count 8)]
   4.1.3.6 (Invalid Addresses)
     MUST discard invalid source addresses (NOT YET -- will be implemented
       in IP, so UDP will eventually be OK.  Right now it's a violation.)
     MUST only send datagrams with one of our addresses (NOT YET - ought to be OK )
   950728 -- MS
*/

#include <asm/system.h>
#include <asm/segment.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/termios.h>
#include <linux/mm.h>
#include <linux/config.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <net/route.h>
#include <net/checksum.h>

/*
 *	Snmp MIB for the UDP layer
 */

struct udp_mib		udp_statistics;

/*
 *	Cached last hit socket
 */
 
volatile unsigned long 	uh_cache_saddr,uh_cache_daddr;
volatile unsigned short  uh_cache_dport, uh_cache_sport;
volatile struct sock *uh_cache_sk;

void udp_cache_zap(void)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	uh_cache_saddr=0;
	uh_cache_daddr=0;
	uh_cache_dport=0;
	uh_cache_sport=0;
	uh_cache_sk=NULL;
	restore_flags(flags);
}

static int udp_deliver(struct sock *sk, struct udphdr *uh, struct sk_buff *skb, struct device *dev, long saddr, long daddr, int len);

#define min(a,b)	((a)<(b)?(a):(b))


/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.  
 * Header points to the ip header of the error packet. We move
 * on past this. Then (as it used to claim before adjustment)
 * header points to the first 8 bytes of the udp header.  We need
 * to find the appropriate port.
 */

void udp_err(int type, int code, unsigned char *header, __u32 daddr,
	__u32 saddr, struct inet_protocol *protocol)
{
	struct udphdr *uh;
	struct sock *sk;

	/*
	 *	Find the 8 bytes of post IP header ICMP included for us
	 */  
	
	uh = (struct udphdr *)header;  
   
	sk = get_sock(&udp_prot, uh->source, daddr, uh->dest, saddr);

	if (sk == NULL) 
	  	return;	/* No socket for error */
  	
	if (type == ICMP_SOURCE_QUENCH) 
	{	/* Slow down! */
		if (sk->cong_window > 1) 
			sk->cong_window = sk->cong_window/2;
		return;
	}

	if (type == ICMP_PARAMETERPROB)
	{
		sk->err = EPROTO;
		sk->error_report(sk);
		return;
	}
			
	/*
	 *	Various people wanted BSD UDP semantics. Well they've come 
	 *	back out because they slow down response to stuff like dead
	 *	or unreachable name servers and they screw term users something
	 *	chronic. Oh and it violates RFC1122. So basically fix your 
	 *	client code people.
	 */
	 
	/* RFC1122: OK.  Passes ICMP errors back to application, as per */
	/* 4.1.3.3. */
	/* After the comment above, that should be no surprise. */

	if (code < 13 && icmp_err_convert[code].fatal)
	{
		/*
		 *	4.x BSD compatibility item. Break RFC1122 to
		 *	get BSD socket semantics.
		 */
		if(sk->bsdism && sk->state!=TCP_ESTABLISHED)
			return;
		sk->err = icmp_err_convert[code].errno;
		sk->error_report(sk);
	}
}


static unsigned short udp_check(struct udphdr *uh, int len, unsigned long saddr, unsigned long daddr, unsigned long base)
{
	return(csum_tcpudp_magic(saddr, daddr, len, IPPROTO_UDP, base));
}

struct udpfakehdr 
{
	struct udphdr uh;
	int daddr;
	int other;
	const char *from;
	int wcheck;
};

/*
 *	Copy and checksum a UDP packet from user space into a buffer. We still have to do the planning to
 *	get ip_build_xmit to spot direct transfer to network card and provide an additional callback mode
 *	for direct user->board I/O transfers. That one will be fun.
 */
 
static void udp_getfrag(const void *p, __u32 saddr, char * to, unsigned int offset, unsigned int fraglen) 
{
	struct udpfakehdr *ufh = (struct udpfakehdr *)p;
	const char *src;
	char *dst;
	unsigned int len;

	if (offset) 
	{
		len = fraglen;
	 	src = ufh->from+(offset-sizeof(struct udphdr));
	 	dst = to;
	}
	else 
	{
		len = fraglen-sizeof(struct udphdr);
 		src = ufh->from;
		dst = to+sizeof(struct udphdr);
	}
	ufh->wcheck = csum_partial_copy_fromuser(src, dst, len, ufh->wcheck);
	if (offset == 0) 
	{
 		ufh->wcheck = csum_partial((char *)ufh, sizeof(struct udphdr),
 				   ufh->wcheck);
		ufh->uh.check = csum_tcpudp_magic(saddr, ufh->daddr, 
					  ntohs(ufh->uh.len),
					  IPPROTO_UDP, ufh->wcheck);
		if (ufh->uh.check == 0)
			ufh->uh.check = -1;
		memcpy(to, ufh, sizeof(struct udphdr));
	}
}

/*
 *	Uncheckummed UDP is sufficiently criticial to stuff like ATM video conferencing
 *	that we use two routines for this for speed. Probably we ought to have a CONFIG_FAST_NET
 *	set for >10Mb/second boards to activate this sort of coding. Timing needed to verify if
 *	this is a valid decision.
 */
 
static void udp_getfrag_nosum(const void *p, __u32 saddr, char * to, unsigned int offset, unsigned int fraglen) 
{
	struct udpfakehdr *ufh = (struct udpfakehdr *)p;
	const char *src;
	char *dst;
	unsigned int len;

	if (offset) 
	{
		len = fraglen;
	 	src = ufh->from+(offset-sizeof(struct udphdr));
	 	dst = to;
	}
	else 
	{
		len = fraglen-sizeof(struct udphdr);
 		src = ufh->from;
		dst = to+sizeof(struct udphdr);
	}
	memcpy_fromfs(dst,src,len);
	if (offset == 0) 
		memcpy(to, ufh, sizeof(struct udphdr));
}


/*
 *	Send UDP frames.
 */

static int udp_send(struct sock *sk, struct sockaddr_in *sin,
		      const unsigned char *from, int len, int rt,
		    __u32 saddr, int noblock) 
{
	int ulen = len + sizeof(struct udphdr);
	int a;
	struct udpfakehdr ufh;
	
	if(ulen>65535-sizeof(struct iphdr))
		return -EMSGSIZE;

	ufh.uh.source = sk->dummy_th.source;
	ufh.uh.dest = sin->sin_port;
	ufh.uh.len = htons(ulen);
	ufh.uh.check = 0;
	ufh.daddr = sin->sin_addr.s_addr;
	ufh.other = (htons(ulen) << 16) + IPPROTO_UDP*256;
	ufh.from = from;
	ufh.wcheck = 0;

	/* RFC1122: OK.  Provides the checksumming facility (MUST) as per */
	/* 4.1.3.4. It's configurable by the application via setsockopt() */
	/* (MAY) and it defaults to on (MUST).  Almost makes up for the */
	/* violation above. -- MS */

	if(sk->no_check)
		a = ip_build_xmit(sk, udp_getfrag_nosum, &ufh, ulen, 
			sin->sin_addr.s_addr, saddr, sk->opt, rt, IPPROTO_UDP, noblock);
	else
		a = ip_build_xmit(sk, udp_getfrag, &ufh, ulen, 
			sin->sin_addr.s_addr, saddr, sk->opt, rt, IPPROTO_UDP, noblock);
	if(a<0)
		return a;
	udp_statistics.UdpOutDatagrams++;
	return len;
}


static int udp_sendto(struct sock *sk, const unsigned char *from, int len, int noblock,
	   unsigned flags, struct sockaddr_in *usin, int addr_len)
{
	struct sockaddr_in sin;
	int tmp;
	__u32 saddr=0;

	/* 
	 *	Check the flags. We support no flags for UDP sending
	 */

	if (flags&~MSG_DONTROUTE) 
	  	return(-EINVAL);
	/*
	 *	Get and verify the address. 
	 */
	 
	if (usin) 
	{
		if (addr_len < sizeof(sin)) 
			return(-EINVAL);
		if (usin->sin_family && usin->sin_family != AF_INET) 
			return(-EINVAL);
		if (usin->sin_port == 0) 
			return(-EINVAL);
	} 
	else 
	{
		if (sk->state != TCP_ESTABLISHED) 
			return(-EINVAL);
		sin.sin_family = AF_INET;
		sin.sin_port = sk->dummy_th.dest;
		sin.sin_addr.s_addr = sk->daddr;
		usin = &sin;
  	}
  
  	/*
  	 *	BSD socket semantics. You must set SO_BROADCAST to permit
  	 *	broadcasting of data.
  	 */
  	 
	/* RFC1122: OK.  Allows the application to select the specific */
	/* source address for an outgoing packet (MUST) as per 4.1.3.5. */
	/* Optional addition: a mechanism for telling the application what */
	/* address was used. (4.1.3.5, MAY) -- MS */

	/* RFC1122: MUST ensure that all outgoing packets have one */
	/* of this host's addresses as a source addr.(4.1.3.6) - bind in  */
	/* af_inet.c checks these. It does need work to allow BSD style */
	/* bind to multicast as is done by xntpd		*/

  	if(usin->sin_addr.s_addr==INADDR_ANY)
  		usin->sin_addr.s_addr=ip_my_addr();
  		
  	if(!sk->broadcast && ip_chk_addr(usin->sin_addr.s_addr)==IS_BROADCAST)
	    	return -EACCES;			/* Must turn broadcast on first */

	sk->inuse = 1;

	/* Send the packet. */
	tmp = udp_send(sk, usin, from, len, flags, saddr, noblock);

	/* The datagram has been sent off.  Release the socket. */
	release_sock(sk);
	return(tmp);
}

/*
 *	Temporary
 */
 
static int udp_sendmsg(struct sock *sk, struct msghdr *msg, int len, int noblock, 
	int flags)
{
	if(msg->msg_iovlen==1)
		return udp_sendto(sk,msg->msg_iov[0].iov_base,len, noblock, flags, msg->msg_name, msg->msg_namelen);
	else
	{
		/*
		 *	For awkward cases we linearise the buffer first. In theory this is only frames
		 *	whose iovec's don't split on 4 byte boundaries, and soon encrypted stuff (to keep
		 *	skip happy). We are a bit more general about it.
		 */
		 
		unsigned char *buf;
		int fs;
		int err;
		if(len>65515)
			return -EMSGSIZE;
		buf=kmalloc(len, GFP_KERNEL);
		if(buf==NULL)
			return -ENOBUFS;
		memcpy_fromiovec(buf, msg->msg_iov, len);
		fs=get_fs();
		set_fs(get_fs());
		err=udp_sendto(sk,buf,len, noblock, flags, msg->msg_name, msg->msg_namelen);
		set_fs(fs);
		kfree_s(buf,len);
		return err;
	}
}

/*
 *	IOCTL requests applicable to the UDP protocol
 */
 
int udp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	int err;
	switch(cmd) 
	{
		case TIOCOUTQ:
		{
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);
			amount = sock_wspace(sk);
			err=verify_area(VERIFY_WRITE,(void *)arg,
					sizeof(unsigned long));
			if(err)
				return(err);
			put_fs_long(amount,(unsigned long *)arg);
			return(0);
		}

		case TIOCINQ:
		{
			struct sk_buff *skb;
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);
			amount = 0;
			skb = skb_peek(&sk->receive_queue);
			if (skb != NULL) {
				/*
				 * We will only return the amount
				 * of this packet since that is all
				 * that will be read.
				 */
				amount = skb->len;
			}
			err=verify_area(VERIFY_WRITE,(void *)arg,
						sizeof(unsigned long));
			if(err)
				return(err);
			put_fs_long(amount,(unsigned long *)arg);
			return(0);
		}

		default:
			return(-EINVAL);
	}
	return(0);
}


/*
 * 	This should be easy, if there is something there we\
 * 	return it, otherwise we block.
 */

int udp_recvmsg(struct sock *sk, struct msghdr *msg, int len,
	     int noblock, int flags,int *addr_len)
{
  	int copied = 0;
  	int truesize;
  	struct sk_buff *skb;
  	int er;
  	struct sockaddr_in *sin=(struct sockaddr_in *)msg->msg_name;

	/*
	 *	Check any passed addresses
	 */
	 
  	if (addr_len) 
  		*addr_len=sizeof(*sin);
  
	/*
	 *	From here the generic datagram does a lot of the work. Come
	 *	the finished NET3, it will do _ALL_ the work!
	 */
	 	
	skb=skb_recv_datagram(sk,flags,noblock,&er);
	if(skb==NULL)
  		return er;
  
  	truesize = skb->len - sizeof(struct udphdr);
  	copied = min(len, truesize);

  	/*
  	 *	FIXME : should use udp header size info value 
  	 */
  	 
	skb_copy_datagram_iovec(skb,sizeof(struct udphdr),msg->msg_iov,copied);
	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (sin) 
	{
		sin->sin_family = AF_INET;
		sin->sin_port = skb->h.uh->source;
		sin->sin_addr.s_addr = skb->daddr;
  	}
  
  	skb_free_datagram(skb);
  	release_sock(sk);
  	return(copied);
}

int udp_connect(struct sock *sk, struct sockaddr_in *usin, int addr_len)
{
	struct rtable *rt;
	if (addr_len < sizeof(*usin)) 
	  	return(-EINVAL);

	if (usin->sin_family && usin->sin_family != AF_INET) 
	  	return(-EAFNOSUPPORT);
	if (usin->sin_addr.s_addr==INADDR_ANY)
		usin->sin_addr.s_addr=ip_my_addr();

	if(!sk->broadcast && ip_chk_addr(usin->sin_addr.s_addr)==IS_BROADCAST)
		return -EACCES;			/* Must turn broadcast on first */
  	
  	rt=ip_rt_route((__u32)usin->sin_addr.s_addr, sk->localroute);
  	if (rt==NULL)
  		return -ENETUNREACH;
  	if(!sk->saddr)
	  	sk->saddr = rt->rt_src;		/* Update source address */
	if(!sk->rcv_saddr)
		sk->rcv_saddr = rt->rt_src;
	sk->daddr = usin->sin_addr.s_addr;
	sk->dummy_th.dest = usin->sin_port;
	sk->state = TCP_ESTABLISHED;
	udp_cache_zap();
	sk->ip_route_cache = rt;
	return(0);
}


static void udp_close(struct sock *sk, int timeout)
{
	sk->inuse = 1;
	sk->state = TCP_CLOSE;
	if(uh_cache_sk==sk)
		udp_cache_zap();
	if (sk->dead) 
		destroy_sock(sk);
	else
		release_sock(sk);
}


/*
 *	All we need to do is get the socket, and then do a checksum. 
 */
 
int udp_rcv(struct sk_buff *skb, struct device *dev, struct options *opt,
	__u32 daddr, unsigned short len,
	__u32 saddr, int redo, struct inet_protocol *protocol)
{
  	struct sock *sk;
  	struct udphdr *uh;
	unsigned short ulen;
	int addr_type = IS_MYADDR;
	
	if(!dev || dev->pa_addr!=daddr)
		addr_type=ip_chk_addr(daddr);
		
	/*
	 *	Get the header.
	 */
	 
  	uh = (struct udphdr *) skb->h.uh;
  	
  	ip_statistics.IpInDelivers++;

	/*
	 *	Validate the packet and the UDP length.
	 */
	 
	ulen = ntohs(uh->len);
	
	if (ulen > len || len < sizeof(*uh) || ulen < sizeof(*uh)) 
	{
		NETDEBUG(printk("UDP: short packet: %d/%d\n", ulen, len));
		udp_statistics.UdpInErrors++;
		kfree_skb(skb, FREE_WRITE);
		return(0);
	}

	/* RFC1122 warning: According to 4.1.3.6, we MUST discard any */
	/* datagram which has an invalid source address, either here or */
	/* in IP. */
	/* Right now, IP isn't doing it, and neither is UDP. It's on the */
	/* FIXME list for IP, though, so I wouldn't worry about it. */
	/* (That's the Right Place to do it, IMHO.) -- MS */

	if (uh->check && (
		( skb->ip_summed && udp_check(uh, len, saddr, daddr, skb->csum ) ) ||
		( !skb->ip_summed && udp_check(uh, len, saddr, daddr,csum_partial((char*)uh, len, 0)))
		         )
	   )
	{
		/* <mea@utu.fi> wants to know, who sent it, to
		   go and stomp on the garbage sender... */

	  /* RFC1122: OK.  Discards the bad packet silently (as far as */
	  /* the network is concered, anyway) as per 4.1.3.4 (MUST). */

		NETDEBUG(printk("UDP: bad checksum. From %08lX:%d to %08lX:%d ulen %d\n",
		       ntohl(saddr),ntohs(uh->source),
		       ntohl(daddr),ntohs(uh->dest),
		       ulen));
		udp_statistics.UdpInErrors++;
		kfree_skb(skb, FREE_WRITE);
		return(0);
	}


	len=ulen;

#ifdef CONFIG_IP_MULTICAST
	if (addr_type!=IS_MYADDR)
	{
		/*
		 *	Multicasts and broadcasts go to each listener.
		 */
		struct sock *sknext=NULL;
		sk=get_sock_mcast(udp_prot.sock_array[ntohs(uh->dest)&(SOCK_ARRAY_SIZE-1)], uh->dest,
				saddr, uh->source, daddr);
		if(sk)
		{		
			do
			{
				struct sk_buff *skb1;

				sknext=get_sock_mcast(sk->next, uh->dest, saddr, uh->source, daddr);
				if(sknext)
					skb1=skb_clone(skb,GFP_ATOMIC);
				else
					skb1=skb;
				if(skb1)
					udp_deliver(sk, uh, skb1, dev,saddr,daddr,len);
				sk=sknext;
			}
			while(sknext!=NULL);
		}
		else
			kfree_skb(skb, FREE_READ);
		return 0;
	}	
#endif
	if(saddr==uh_cache_saddr && daddr==uh_cache_daddr && uh->dest==uh_cache_dport && uh->source==uh_cache_sport)
		sk=(struct sock *)uh_cache_sk;
	else
	{
	  	sk = get_sock(&udp_prot, uh->dest, saddr, uh->source, daddr);
  		uh_cache_saddr=saddr;
  		uh_cache_daddr=daddr;
  		uh_cache_dport=uh->dest;
  		uh_cache_sport=uh->source;
  		uh_cache_sk=sk;
	}
	
	if (sk == NULL) 
  	{
  		udp_statistics.UdpNoPorts++;
		if (addr_type == IS_MYADDR) 
		{
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0, dev);
		}
		/*
		 * Hmm.  We got an UDP broadcast to a port to which we
		 * don't wanna listen.  Ignore it.
		 */
		skb->sk = NULL;
		kfree_skb(skb, FREE_WRITE);
		return(0);
  	}
	return udp_deliver(sk,uh,skb,dev, saddr, daddr, len);
}

static int udp_deliver(struct sock *sk, struct udphdr *uh, struct sk_buff *skb, struct device *dev, long saddr, long daddr, int len)
{
	skb->sk = sk;
	skb->dev = dev;
	skb_trim(skb,len);

	/*
	 *	These are supposed to be switched. 
	 */
	 
	skb->daddr = saddr;
	skb->saddr = daddr;


	/*
	 *	Charge it to the socket, dropping if the queue is full.
	 */

	/* I assume this includes the IP options, as per RFC1122 (4.1.3.2). */
	/* If not, please let me know. -- MS */

	if (sock_queue_rcv_skb(sk,skb)<0) 
	{
		udp_statistics.UdpInErrors++;
		ip_statistics.IpInDiscards++;
		ip_statistics.IpInDelivers--;
		skb->sk = NULL;
		kfree_skb(skb, FREE_WRITE);
		release_sock(sk);
		return(0);
	}
  	udp_statistics.UdpInDatagrams++;
	release_sock(sk);
	return(0);
}


struct proto udp_prot = {
	udp_close,
	ip_build_header,
	udp_connect,
	NULL,
	ip_queue_xmit,
	NULL,
	NULL,
	NULL,
	udp_rcv,
	datagram_select,
	udp_ioctl,
	NULL,
	NULL,
	ip_setsockopt,
	ip_getsockopt,
	udp_sendmsg,
	udp_recvmsg,
	NULL,		/* No special bind function */
	128,
	0,
	"UDP",
	0, 0,
	{NULL,}
};
