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
 *		Jon Peatfield	:	Minor efficiency fix to sendto().
 *		Mike Shaver	:	RFC1122 checks.
 *		Alan Cox	:	Nonblocking error fix.
 *	Willy Konynenberg	:	Transparent proxying support.
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
 
/* RFC1122 Status:
   4.1.3.1 (Ports):
     SHOULD send ICMP_PORT_UNREACHABLE in response to datagrams to 
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
       when application doesn't choose (NOT YET - doesn't seem to be in the BSD API)
       [Does opening a SOCK_PACKET and snooping your output count 8)]
   4.1.3.6 (Invalid Addresses)
     MUST discard invalid source addresses (NOT YET -- will be implemented
       in IP, so UDP will eventually be OK.  Right now it's a violation.)
     MUST only send datagrams with one of our addresses (NOT YET - ought to be OK )
   950728 -- MS
*/

#include <asm/system.h>
#include <asm/uaccess.h>
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

void udp_err(struct sk_buff *skb, unsigned char *dp)
{
	struct iphdr *iph = (struct iphdr*)dp;
	struct udphdr *uh = (struct udphdr*)(dp+(iph->ihl<<2));
	int type = skb->h.icmph->type;
	int code = skb->h.icmph->code;
	struct sock *sk;

	sk = get_sock(&udp_prot, uh->source, iph->daddr, uh->dest, iph->saddr);

	if (sk == NULL)
	  	return;	/* No socket for error */

	if (sk->ip_recverr && !sk->users) {
		struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
		if (skb2 && sock_queue_err_skb(sk, skb2))
			kfree_skb(skb2, FREE_READ);
	}
  	
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

	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED)
	{
		if (sk->ip_pmtudisc != IP_PMTUDISC_DONT) {
			sk->err = EMSGSIZE;
			sk->error_report(sk);
		}
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

	if (code < NR_ICMP_UNREACH && icmp_err_convert[code].fatal)
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
	u32 saddr;
	u32 daddr;
	u32 other;
	const char *from;
	u32 wcheck;
};

/*
 *	Copy and checksum a UDP packet from user space into a buffer. We still have to do the planning to
 *	get ip_build_xmit to spot direct transfer to network card and provide an additional callback mode
 *	for direct user->board I/O transfers. That one will be fun.
 */
 
static int udp_getfrag(const void *p, char * to, unsigned int offset, unsigned int fraglen) 
{
	struct udpfakehdr *ufh = (struct udpfakehdr *)p;
	const char *src;
	char *dst;
	unsigned int len;

	if (offset) {
		len = fraglen;
	 	src = ufh->from+(offset-sizeof(struct udphdr));
	 	dst = to;
	} else {
		len = fraglen-sizeof(struct udphdr);
 		src = ufh->from;
		dst = to+sizeof(struct udphdr);
	}
	ufh->wcheck = csum_partial_copy_fromuser(src, dst, len, ufh->wcheck);
	if (offset == 0) {
 		ufh->wcheck = csum_partial((char *)ufh, sizeof(struct udphdr),
 				   ufh->wcheck);
		ufh->uh.check = csum_tcpudp_magic(ufh->saddr, ufh->daddr, 
					  ntohs(ufh->uh.len),
					  IPPROTO_UDP, ufh->wcheck);
		if (ufh->uh.check == 0)
			ufh->uh.check = -1;
		memcpy(to, ufh, sizeof(struct udphdr));
	}
	return 0;
}

/*
 *	Unchecksummed UDP is sufficiently critical to stuff like ATM video conferencing
 *	that we use two routines for this for speed. Probably we ought to have a CONFIG_FAST_NET
 *	set for >10Mb/second boards to activate this sort of coding. Timing needed to verify if
 *	this is a valid decision.
 */
 
static int udp_getfrag_nosum(const void *p, char * to, unsigned int offset, unsigned int fraglen) 
{
	struct udpfakehdr *ufh = (struct udpfakehdr *)p;
	const char *src;
	char *dst;
	int err;
	unsigned int len;

	if (offset) {
		len = fraglen;
	 	src = ufh->from+(offset-sizeof(struct udphdr));
	 	dst = to;
	} else {
		len = fraglen-sizeof(struct udphdr);
 		src = ufh->from;
		dst = to+sizeof(struct udphdr);
	}
	err = copy_from_user(dst,src,len);
	if (offset == 0) 
		memcpy(to, ufh, sizeof(struct udphdr));
	return err;
}


static int udp_sendto(struct sock *sk, const unsigned char *from, int len,
		      struct msghdr *msg)
{
	int ulen = len + sizeof(struct udphdr);
	struct device *dev = NULL;
	struct ipcm_cookie ipc;
	struct udpfakehdr ufh;
	struct rtable *rt;
	int free = 0;
	u32 daddr;
	u8  tos;
	int err;

	if (len>65535)
		return -EMSGSIZE;

	/* 
	 *	Check the flags.
	 */

	if (msg->msg_flags&MSG_OOB)		/* Mirror BSD error message compatibility */
		return -EOPNOTSUPP;

	if (msg->msg_flags&~(MSG_DONTROUTE|MSG_DONTWAIT))
	  	return -EINVAL;

	/*
	 *	Get and verify the address. 
	 */
	 
	if (msg->msg_namelen) {
		struct sockaddr_in * usin = (struct sockaddr_in*)msg->msg_name;
		if (msg->msg_namelen < sizeof(*usin))
			return(-EINVAL);
		if (usin->sin_family != AF_INET) {
			static int complained;
			if (!complained++)
				printk(KERN_WARNING "%s forgot to set AF_INET in udp sendmsg. Fix it!\n", current->comm);
			if (usin->sin_family)
				return -EINVAL;
		}
		ufh.daddr = usin->sin_addr.s_addr;
		ufh.uh.dest = usin->sin_port;
		if (ufh.uh.dest == 0)
			return -EINVAL;
	} else {
		if (sk->state != TCP_ESTABLISHED)
			return -EINVAL;
		ufh.daddr = sk->daddr;
		ufh.uh.dest = sk->dummy_th.dest;
  	}

	ipc.addr = sk->saddr;
	ipc.opt = NULL;
	if (msg->msg_controllen) {
		err = ip_cmsg_send(msg, &ipc, &dev);
		if (err)
			return err;
		if (ipc.opt)
			free = 1;
	}
	if (!ipc.opt)
		ipc.opt = sk->opt;

	ufh.saddr = ipc.addr;
	ipc.addr = daddr = ufh.daddr;

	if (ipc.opt && ipc.opt->srr) {
		if (!daddr)
			return -EINVAL;
		daddr = ipc.opt->faddr;
	}
	tos = RT_TOS(sk->ip_tos) | (sk->localroute || (msg->msg_flags&MSG_DONTROUTE) ||
				    (ipc.opt && ipc.opt->is_strictroute));

	if (MULTICAST(daddr) && sk->ip_mc_name[0] && dev == NULL)
		err = ip_route_output_dev(&rt, daddr, ufh.saddr, tos, sk->ip_mc_name);
	else
		err = ip_route_output(&rt, daddr, ufh.saddr, tos, dev);

	if (err) {
		if (free) kfree(ipc.opt);
		return err;
	}

	if (rt->rt_flags&RTF_BROADCAST && !sk->broadcast) {
		if (free) kfree(ipc.opt);
		ip_rt_put(rt);
		return -EACCES;
	}

	ufh.saddr = rt->rt_src;
	if (!ipc.addr)
		ufh.daddr = ipc.addr = rt->rt_dst;
	ufh.uh.source = sk->dummy_th.source;
	ufh.uh.len = htons(ulen);
	ufh.uh.check = 0;
	ufh.other = (htons(ulen) << 16) + IPPROTO_UDP*256;
	ufh.from = from;
	ufh.wcheck = 0;

	/* RFC1122: OK.  Provides the checksumming facility (MUST) as per */
	/* 4.1.3.4. It's configurable by the application via setsockopt() */
	/* (MAY) and it defaults to on (MUST).  Almost makes up for the */
	/* violation above. -- MS */

	lock_sock(sk);
	if (sk->no_check)
		err = ip_build_xmit(sk, udp_getfrag_nosum, &ufh, ulen, 
				    &ipc, rt, msg->msg_flags);
	else
		err = ip_build_xmit(sk, udp_getfrag, &ufh, ulen, 
				    &ipc, rt, msg->msg_flags);
	ip_rt_put(rt);
	release_sock(sk);

	if (free)
		kfree(ipc.opt);
	if (!err) {
		udp_statistics.UdpOutDatagrams++;
		return len;
	}
	return err;
}

/*
 *	Temporary
 */
 
int udp_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	if(msg->msg_iovlen==1)
		return udp_sendto(sk,msg->msg_iov[0].iov_base,len, msg);
	else {
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
		err = memcpy_fromiovec(buf, msg->msg_iov, len);
		if (err)
			err = -EFAULT;
		if (!err)
		{
			fs=get_fs();
			set_fs(get_ds());
			err=udp_sendto(sk,buf,len, msg);
			set_fs(fs);
		}
		kfree_s(buf,len);
		return err;
	}
}

/*
 *	IOCTL requests applicable to the UDP protocol
 */
 
int udp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	switch(cmd) 
	{
		case TIOCOUTQ:
		{
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);
			amount = sock_wspace(sk);
			return put_user(amount, (int *)arg);
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
				amount = skb->len-sizeof(struct udphdr);
			}
			return put_user(amount, (int *)arg);
			return(0);
		}

		default:
			return(-EINVAL);
	}
	return(0);
}


/*
 * 	This should be easy, if there is something there we
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

	if (sk->ip_recverr && (skb = skb_dequeue(&sk->error_queue)) != NULL) {
		er = sock_error(sk);
		if (msg->msg_controllen == 0) {
			skb_free_datagram(sk, skb);
			return er;
		}
		put_cmsg(msg, SOL_IP, IP_RECVERR, skb->len, skb->data);
		skb_free_datagram(sk, skb);
		return 0;
	}
  
	/*
	 *	From here the generic datagram does a lot of the work. Come
	 *	the finished NET3, it will do _ALL_ the work!
	 */
	 	
	skb=skb_recv_datagram(sk,flags,noblock,&er);
	if(skb==NULL)
  		return er;
  
  	truesize = skb->len - sizeof(struct udphdr);
	copied = truesize;
	if (len < truesize)
	{
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

  	/*
  	 *	FIXME : should use udp header size info value 
  	 */
  	 
	er = skb_copy_datagram_iovec(skb,sizeof(struct udphdr),msg->msg_iov,copied);
	if (er)
		return er; 
	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (sin)
	{
		sin->sin_family = AF_INET;
		sin->sin_port = skb->h.uh->source;
		sin->sin_addr.s_addr = skb->nh.iph->saddr;
#ifdef CONFIG_IP_TRANSPARENT_PROXY
		if (flags&MSG_PROXY)
		{
			/*
			 * We map the first 8 bytes of a second sockaddr_in
			 * into the last 8 (unused) bytes of a sockaddr_in.
			 * This _is_ ugly, but it's the only way to do it
			 * easily,  without adding system calls.
			 */
			struct sockaddr_in *sinto =
				(struct sockaddr_in *) sin->sin_zero;

			sinto->sin_family = AF_INET;
			sinto->sin_port = skb->h.uh->dest;
			sinto->sin_addr.s_addr = skb->nh.iph->daddr;
		}
#endif
  	}
	if (sk->ip_cmsg_flags)
		ip_cmsg_recv(msg, skb);
  
  	skb_free_datagram(sk, skb);
  	return(copied);
}

int udp_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in *usin = (struct sockaddr_in *) uaddr;
	struct rtable *rt;
	int err;

	
	if (addr_len < sizeof(*usin)) 
	  	return(-EINVAL);

	/*
	 *	1003.1g - break association.
	 */
	 
	if (usin->sin_family==AF_UNSPEC)
	{
		sk->saddr=INADDR_ANY;
		sk->rcv_saddr=INADDR_ANY;
		sk->daddr=INADDR_ANY;
		sk->state = TCP_CLOSE;
		udp_cache_zap();
		return 0;
	}

	if (usin->sin_family && usin->sin_family != AF_INET) 
	  	return(-EAFNOSUPPORT);

	err = ip_route_connect(&rt, usin->sin_addr.s_addr, sk->saddr,
			       sk->ip_tos|sk->localroute);
	if (err)
		return err;
	if (rt->rt_flags&RTF_BROADCAST && !sk->broadcast) {
		ip_rt_put(rt);
		return -EACCES;
	}
  	if(!sk->saddr)
	  	sk->saddr = rt->rt_src;		/* Update source address */
	if(!sk->rcv_saddr)
		sk->rcv_saddr = rt->rt_src;
	sk->daddr = rt->rt_dst;
	sk->dummy_th.dest = usin->sin_port;
	sk->state = TCP_ESTABLISHED;
	udp_cache_zap();
	ip_rt_put(rt);
	return(0);
}


static void udp_close(struct sock *sk, unsigned long timeout)
{
	lock_sock(sk);
	sk->state = TCP_CLOSE;
	if(uh_cache_sk==sk)
		udp_cache_zap();
	release_sock(sk);
	sk->dead = 1;
	destroy_sock(sk);
}

static int udp_queue_rcv_skb(struct sock * sk, struct sk_buff *skb)
{
	/*
	 *	Charge it to the socket, dropping if the queue is full.
	 */

	if (__sock_queue_rcv_skb(sk,skb)<0) {
		udp_statistics.UdpInErrors++;
		ip_statistics.IpInDiscards++;
		ip_statistics.IpInDelivers--;
		kfree_skb(skb, FREE_WRITE);
		return -1;
	}
	udp_statistics.UdpInDatagrams++;
	return 0;
}


static inline void udp_deliver(struct sock *sk, struct sk_buff *skb)
{
	if (sk->users) {
		__skb_queue_tail(&sk->back_log, skb);
		return;
	}
	udp_queue_rcv_skb(sk, skb);
}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
/*
 *	Check whether a received UDP packet might be for one of our
 *	sockets.
 */

int udp_chkaddr(struct sk_buff *skb)
{
	struct iphdr *iph = skb->nh.iph;
	struct udphdr *uh = (struct udphdr *)(skb->nh.raw + iph->ihl*4);
	struct sock *sk;

	sk = get_sock(&udp_prot, uh->dest, iph->saddr, uh->source, iph->daddr);

	if (!sk) return 0;
	/* 0 means accept all LOCAL addresses here, not all the world... */
	if (sk->rcv_saddr == 0) return 0;
	return 1;
}
#endif


static __inline__ struct sock *
get_udp_sock(unsigned short dport, unsigned long saddr, unsigned short sport,
	     unsigned long daddr)
{
	struct sock *sk;

	if (saddr==uh_cache_saddr && daddr==uh_cache_daddr &&
	    dport==uh_cache_dport && sport==uh_cache_sport)
		 return (struct sock *)uh_cache_sk;
	sk = get_sock(&udp_prot, dport, saddr, sport, daddr);
	uh_cache_saddr=saddr;
	uh_cache_daddr=daddr;
	uh_cache_dport=dport;
	uh_cache_sport=sport;
	uh_cache_sk=sk;
	return sk;
}


/*
 *	All we need to do is get the socket, and then do a checksum. 
 */
 
int udp_rcv(struct sk_buff *skb, unsigned short len)
{
  	struct sock *sk;
  	struct udphdr *uh;
	unsigned short ulen;
	struct rtable *rt = (struct rtable*)skb->dst;
	u32 saddr = skb->nh.iph->saddr;
	u32 daddr = skb->nh.iph->daddr;

	/*
	 * First time through the loop.. Do all the setup stuff
	 * (including finding out the socket we go to etc)
	 */

	/*
	 *	Get the header.
	 */
	 
  	uh = skb->h.uh;
  	
  	ip_statistics.IpInDelivers++;

	/*
	 *	Validate the packet and the UDP length.
	 */
	 
	ulen = ntohs(uh->len);
	
	if (ulen > len || len < sizeof(*uh) || ulen < sizeof(*uh)) {
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
		( (skb->ip_summed == CHECKSUM_HW) && udp_check(uh, len, saddr, daddr, skb->csum ) ) ||
		( (skb->ip_summed == CHECKSUM_NONE) && udp_check(uh, len, saddr, daddr,csum_partial((char*)uh, len, 0)))
			  /* skip if CHECKSUM_UNNECESSARY */
		         )
	   ) {
		/* <mea@utu.fi> wants to know, who sent it, to
		   go and stomp on the garbage sender... */

	  /* RFC1122: OK.  Discards the bad packet silently (as far as */
	  /* the network is concerned, anyway) as per 4.1.3.4 (MUST). */

		NETDEBUG(printk("UDP: bad checksum. From %08lX:%d to %08lX:%d ulen %d\n",
		       ntohl(saddr),ntohs(uh->source),
		       ntohl(daddr),ntohs(uh->dest),
		       ulen));
		udp_statistics.UdpInErrors++;
		kfree_skb(skb, FREE_WRITE);
		return(0);
	}


	len = ulen;

	/* Wrong! --ANK */
	skb_trim(skb,len);


	if (rt->rt_flags&(RTF_BROADCAST|RTF_MULTICAST)) {
		/*
		 *	Multicasts and broadcasts go to each listener.
		 */
		struct sock *sknext=NULL;
		sk=get_sock_mcast(udp_prot.sock_array[ntohs(uh->dest)&(SOCK_ARRAY_SIZE-1)], uh->dest,
				saddr, uh->source, daddr);
		if(sk) {		
			do {
				struct sk_buff *skb1;

				sknext=get_sock_mcast(sk->next, uh->dest, saddr, uh->source, daddr);
				if(sknext)
					skb1=skb_clone(skb,GFP_ATOMIC);
				else
					skb1=skb;
				if(skb1)
					udp_deliver(sk, skb1);
				sk=sknext;
			} while(sknext!=NULL);
		} else
			kfree_skb(skb, FREE_READ);
		return 0;
	}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
	if (IPCB(skb)->redirport)
		sk = get_sock_proxy(&udp_prot, uh->dest, saddr, uh->source, daddr, skb->dev->pa_addr, IPCB(skb)->redirport);
	else
#endif
	sk = get_udp_sock(uh->dest, saddr, uh->source, daddr);
	
	if (sk == NULL) {
  		udp_statistics.UdpNoPorts++;
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);

		/*
		 * Hmm.  We got an UDP broadcast to a port to which we
		 * don't wanna listen.  Ignore it.
		 */
		kfree_skb(skb, FREE_WRITE);
		return(0);
  	}
	udp_deliver(sk, skb);
	return 0;
}

struct proto udp_prot = {
	udp_close,
	udp_connect,
	NULL,
	NULL,
	NULL,
	NULL,
	datagram_select,
	udp_ioctl,
	NULL,
	NULL,
	NULL,
	ip_setsockopt,
	ip_getsockopt,
	udp_sendmsg,
	udp_recvmsg,
	NULL,		/* No special bind function */
	udp_queue_rcv_skb,
	128,
	0,
	"UDP",
	0, 0,
	NULL,
};
