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
 *		David S. Miller	:	New socket lookup architecture.
 *					Last socket cache retained as it
 *					does have a high hit rate.
 *		Olaf Kirch	:	Don't linearise iovec on sendmsg.
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
#include <linux/ipsec.h>

/*
 *	Snmp MIB for the UDP layer
 */

struct udp_mib		udp_statistics;

struct sock *udp_hash[UDP_HTABLE_SIZE];

static int udp_v4_verify_bind(struct sock *sk, unsigned short snum)
{
	struct sock *sk2;
	int retval = 0, sk_reuse = sk->reuse;

	SOCKHASH_LOCK();
	for(sk2 = udp_hash[snum & (UDP_HTABLE_SIZE - 1)]; sk2 != NULL; sk2 = sk2->next) {
		if((sk2->num == snum) && (sk2 != sk)) {
			unsigned char state = sk2->state;
			int sk2_reuse = sk2->reuse;

			if(!sk2->rcv_saddr || !sk->rcv_saddr) {
				if((!sk2_reuse)			||
				   (!sk_reuse)			||
				   (state == TCP_LISTEN)) {
					retval = 1;
					break;
				}
			} else if(sk2->rcv_saddr == sk->rcv_saddr) {
				if((!sk_reuse)			||
				   (!sk2_reuse)			||
				   (state == TCP_LISTEN)) {
					retval = 1;
					break;
				}
			}
		}
	}
	SOCKHASH_UNLOCK();
	return retval;
}

static inline int udp_lport_inuse(u16 num)
{
	struct sock *sk = udp_hash[num & (UDP_HTABLE_SIZE - 1)];

	for(; sk != NULL; sk = sk->next) {
		if(sk->num == num)
			return 1;
	}
	return 0;
}

/* Shared by v4/v6 tcp. */
unsigned short udp_good_socknum(void)
{
	int result;
	static int start = 0;
	int i, best, best_size_so_far;

	SOCKHASH_LOCK();

	/* Select initial not-so-random "best" */
	best = PROT_SOCK + 1 + (start & 1023);
	best_size_so_far = 32767;	/* "big" num */
	result = best;
	for (i = 0; i < UDP_HTABLE_SIZE; i++, result++) {
		struct sock *sk;
		int size;

		sk = udp_hash[result & (UDP_HTABLE_SIZE - 1)];

		/* No clashes - take it */
		if (!sk)
			goto out;

		/* Is this one better than our best so far? */
		size = 0;
		do {
			if(++size >= best_size_so_far)
				goto next;
		} while((sk = sk->next) != NULL);
		best_size_so_far = size;
		best = result;
next:
	}

	while (udp_lport_inuse(best))
		best += UDP_HTABLE_SIZE;
	result = best;
out:
	start = result;
	SOCKHASH_UNLOCK();
	return result;
}

/* Last hit UDP socket cache, this is ipv4 specific so make it static. */
static u32 uh_cache_saddr, uh_cache_daddr;
static u16 uh_cache_dport, uh_cache_sport;
static struct sock *uh_cache_sk = NULL;

static void udp_v4_hash(struct sock *sk)
{
	struct sock **skp;
	int num = sk->num;

	num &= (UDP_HTABLE_SIZE - 1);
	skp = &udp_hash[num];

	SOCKHASH_LOCK();
	sk->next = *skp;
	*skp = sk;
	sk->hashent = num;
	SOCKHASH_UNLOCK();
}

static void udp_v4_unhash(struct sock *sk)
{
	struct sock **skp;
	int num = sk->num;

	num &= (UDP_HTABLE_SIZE - 1);
	skp = &udp_hash[num];

	SOCKHASH_LOCK();
	while(*skp != NULL) {
		if(*skp == sk) {
			*skp = sk->next;
			break;
		}
		skp = &((*skp)->next);
	}
	if(uh_cache_sk == sk)
		uh_cache_sk = NULL;
	SOCKHASH_UNLOCK();
}

static void udp_v4_rehash(struct sock *sk)
{
	struct sock **skp;
	int num = sk->num;
	int oldnum = sk->hashent;

	num &= (UDP_HTABLE_SIZE - 1);
	skp = &udp_hash[oldnum];

	SOCKHASH_LOCK();
	while(*skp != NULL) {
		if(*skp == sk) {
			*skp = sk->next;
			break;
		}
		skp = &((*skp)->next);
	}
	sk->next = udp_hash[num];
	udp_hash[num] = sk;
	sk->hashent = num;
	if(uh_cache_sk == sk)
		uh_cache_sk = NULL;
	SOCKHASH_UNLOCK();
}

/* UDP is nearly always wildcards out the wazoo, it makes no sense to try
 * harder than this here plus the last hit cache. -DaveM
 */
struct sock *udp_v4_lookup_longway(u32 saddr, u16 sport, u32 daddr, u16 dport)
{
	struct sock *sk, *result = NULL;
	unsigned short hnum = ntohs(dport);
	int badness = -1;

	for(sk = udp_hash[hnum & (UDP_HTABLE_SIZE - 1)]; sk != NULL; sk = sk->next) {
		if((sk->num == hnum) && !(sk->dead && (sk->state == TCP_CLOSE))) {
			int score = 0;
			if(sk->rcv_saddr) {
				if(sk->rcv_saddr != daddr)
					continue;
				score++;
			}
			if(sk->daddr) {
				if(sk->daddr != saddr)
					continue;
				score++;
			}
			if(sk->dummy_th.dest) {
				if(sk->dummy_th.dest != sport)
					continue;
				score++;
			}
			if(score == 3) {
				result = sk;
				break;
			} else if(score > badness) {
				result = sk;
				badness = score;
			}
		}
	}
	return result;
}

__inline__ struct sock *udp_v4_lookup(u32 saddr, u16 sport, u32 daddr, u16 dport)
{
	struct sock *sk;

	if(uh_cache_sk			&&
	   uh_cache_saddr == saddr	&&
	   uh_cache_sport == sport	&&
	   uh_cache_dport == dport	&&
	   uh_cache_daddr == daddr)
		return uh_cache_sk;

	sk = udp_v4_lookup_longway(saddr, sport, daddr, dport);
	uh_cache_sk	= sk;
	uh_cache_saddr	= saddr;
	uh_cache_daddr	= daddr;
	uh_cache_sport	= sport;
	uh_cache_dport	= dport;
	return sk;
}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
#define secondlist(hpnum, sk, fpass) \
({ struct sock *s1; if(!(sk) && (fpass)--) \
	s1 = udp_hash[(hpnum) & (TCP_HTABLE_SIZE - 1)]; \
   else \
	s1 = (sk); \
   s1; \
})

#define udp_v4_proxy_loop_init(hnum, hpnum, sk, fpass) \
	secondlist((hpnum), udp_hash[(hnum)&(TCP_HTABLE_SIZE-1)],(fpass))

#define udp_v4_proxy_loop_next(hnum, hpnum, sk, fpass) \
	secondlist((hpnum),(sk)->next,(fpass))

struct sock *udp_v4_proxy_lookup(unsigned short num, unsigned long raddr,
				 unsigned short rnum, unsigned long laddr,
				 unsigned long paddr, unsigned short pnum)
{
	struct sock *s, *result = NULL;
	int badness = -1;
	unsigned short hnum = ntohs(num);
	unsigned short hpnum = ntohs(pnum);
	int firstpass = 1;

	SOCKHASH_LOCK();
	for(s = udp_v4_proxy_loop_init(hnum, hpnum, s, firstpass);
	    s != NULL;
	    s = udp_v4_proxy_loop_next(hnum, hpnum, s, firstpass)) {
		if(s->num == hnum || s->num == hpnum) {
			int score = 0;
			if(s->dead && (s->state == TCP_CLOSE))
				continue;
			if(s->rcv_saddr) {
				if((s->num != hpnum || s->rcv_saddr != paddr) &&
				   (s->num != hnum || s->rcv_saddr != laddr))
					continue;
				score++;
			}
			if(s->daddr) {
				if(s->daddr != raddr)
					continue;
				score++;
			}
			if(s->dummy_th.dest) {
				if(s->dummy_th.dest != rnum)
					continue;
				score++;
			}
			if(score == 3 && s->num == hnum) {
				result = s;
				break;
			} else if(score > badness && (s->num == hpnum || s->rcv_saddr)) {
					result = s;
					badness = score;
			}
		}
	}
	SOCKHASH_UNLOCK();
	return result;
}

#undef secondlist
#undef udp_v4_proxy_loop_init
#undef udp_v4_proxy_loop_next

#endif

static inline struct sock *udp_v4_mcast_next(struct sock *sk,
					     unsigned short num,
					     unsigned long raddr,
					     unsigned short rnum,
					     unsigned long laddr)
{
	struct sock *s = sk;
	unsigned short hnum = ntohs(num);
	for(; s; s = s->next) {
		if ((s->num != hnum)					||
		    (s->dead && (s->state == TCP_CLOSE))		||
		    (s->daddr && s->daddr!=raddr)			||
		    (s->dummy_th.dest != rnum && s->dummy_th.dest != 0) ||
		    (s->rcv_saddr  && s->rcv_saddr != laddr))
			continue;
		break;
  	}
  	return s;
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

	sk = udp_v4_lookup(iph->daddr, uh->dest, iph->saddr, uh->source);
	if (sk == NULL)
	  	return;	/* No socket for error */

	if (sk->ip_recverr && !sk->sock_readers) {
		struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
		if (skb2 && sock_queue_err_skb(sk, skb2))
			kfree_skb(skb2, FREE_READ);
	}
  	
	if (type == ICMP_SOURCE_QUENCH) {
#if 0 /* FIXME:	If you check the rest of the code, this is a NOP!
       * 	Someone figure out what we were trying to be doing
       * 	here.  Besides, cong_window is a TCP thing and thus
       * 	I moved it out of normal sock and into tcp_opt.
       */
		/* Slow down! */
		if (sk->cong_window > 1) 
			sk->cong_window = sk->cong_window/2;
#endif
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
	struct iovec *iov;
	int nriov;
	u32 wcheck;
};

/*
 *	Copy and checksum a UDP packet from user space into a buffer. We still have
 *	to do the planning to get ip_build_xmit to spot direct transfer to network
 *	card and provide an additional callback mode for direct user->board I/O
 *	transfers. That one will be fun.
 */
 
static int udp_getfrag(const void *p, char * to, unsigned int offset, unsigned int fraglen) 
{
	struct udpfakehdr *ufh = (struct udpfakehdr *)p;
	struct iovec *iov;
	char *src;
	char *dst = to;
	unsigned int len;

	if (offset == 0) {
		fraglen -= sizeof(struct udphdr);
		dst += sizeof(struct udphdr);
	}

	iov = ufh->iov;
	do {
		if ((len = iov->iov_len) > fraglen)
			len = fraglen;
		src = (char *) iov->iov_base + iov->iov_len - len;
		ufh->wcheck = csum_partial_copy_fromuser(src,
						dst + fraglen - len, len,
						ufh->wcheck);
		if ((iov->iov_len -= len) == 0) {
			if (--(ufh->nriov) < 0) {
				printk(KERN_NOTICE "udp_getfrag: nriov = %d\n",
							ufh->nriov);
				return -EINVAL;
			}
			iov--;
		}
		fraglen -= len;
	} while (fraglen);
	ufh->iov = iov;

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
 *	that we use two routines for this for speed. Probably we ought to have a
 *	CONFIG_FAST_NET set for >10Mb/second boards to activate this sort of coding.
 *	Timing needed to verify if this is a valid decision.
 */
 
static int udp_getfrag_nosum(const void *p, char * to, unsigned int offset, unsigned int fraglen) 
{
	struct udpfakehdr *ufh = (struct udpfakehdr *)p;
	struct iovec *iov;
	char *src;
	char *dst = to;
	int err;
	unsigned int len;

	if (offset == 0) {
		fraglen -= sizeof(struct udphdr);
	 	dst += sizeof(struct udphdr);
	}

	iov = ufh->iov;
	do {
		if ((len = iov->iov_len) > fraglen)
			len = fraglen;
		src = (char *) iov->iov_base + iov->iov_len - len;
		err = copy_from_user(dst + fraglen - len, src, len);
		fraglen -= len;
		if ((iov->iov_len -= len) == 0) {
			if (--(ufh->nriov) < 0) {
				printk(KERN_NOTICE "udp_getfrag: nriov = %d\n",
							ufh->nriov);
				return -EINVAL;
			}
			iov--;
		}
	} while (fraglen && err >= 0);
	ufh->iov = iov;

	if (offset == 0) 
		memcpy(to, ufh, sizeof(struct udphdr));
	return err;
}


int udp_sendmsg(struct sock *sk, struct msghdr *msg, int len)
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

	if (MULTICAST(daddr) && sk->ip_mc_index && dev == NULL)
		err = ip_route_output_dev(&rt, daddr, ufh.saddr, tos, sk->ip_mc_index);
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
	ufh.iov = msg->msg_iov + msg->msg_iovlen - 1;
	ufh.nriov = msg->msg_iovlen;
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
		if(uh_cache_sk == sk)
			uh_cache_sk = NULL;
		return 0;
	}

	if (usin->sin_family && usin->sin_family != AF_INET) 
	  	return(-EAFNOSUPPORT);

	err = ip_route_connect(&rt, usin->sin_addr.s_addr, sk->saddr,
			       sk->ip_tos|sk->localroute);
	if (err)
		return err;
	if ((rt->rt_flags&RTF_BROADCAST) && !sk->broadcast) {
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
	if(uh_cache_sk == sk)
		uh_cache_sk = NULL;
	ip_rt_put(rt);
	return(0);
}


static void udp_close(struct sock *sk, unsigned long timeout)
{
	lock_sock(sk);
	sk->state = TCP_CLOSE;
	if(uh_cache_sk == sk)
		uh_cache_sk = NULL;
	sk->dead = 1;
	release_sock(sk);
	udp_v4_unhash(sk);
	destroy_sock(sk);
}

static int udp_queue_rcv_skb(struct sock * sk, struct sk_buff *skb)
{
	/*
	 *	Check the security clearance
	 */
	 
	if(!ipsec_sk_policy(sk,skb))
	{	
		kfree_skb(skb, FREE_WRITE);
		return(0);
	}
	 
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
	if (sk->sock_readers) {
		__skb_queue_tail(&sk->back_log, skb);
		return;
	}
	udp_queue_rcv_skb(sk, skb);
}

/*
 *	Multicasts and broadcasts go to each listener.
 */
static int udp_v4_mcast_deliver(struct sk_buff *skb, struct udphdr *uh,
				 u32 saddr, u32 daddr)
{
	struct sock *sk;
	int given = 0;

	SOCKHASH_LOCK();
	sk = udp_hash[ntohs(uh->dest) & (UDP_HTABLE_SIZE - 1)];
	sk = udp_v4_mcast_next(sk, uh->dest, saddr, uh->source, daddr);
	if(sk) {
		struct sock *sknext = NULL;

		do {
			struct sk_buff *skb1 = skb;

			sknext = udp_v4_mcast_next(sk->next, uh->dest, saddr,
						   uh->source, daddr);
			if(sknext)
				skb1 = skb_clone(skb, GFP_ATOMIC);

			if(skb1)
				udp_deliver(sk, skb1);
			sk = sknext;
		} while(sknext);
		given = 1;
	}
	SOCKHASH_UNLOCK();
	if(!given)
		kfree_skb(skb, FREE_READ);
	return 0;
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

	sk = udp_v4_lookup(iph->saddr, uh->source, iph->daddr, uh->dest);
	if (!sk)
		return 0;

	/* 0 means accept all LOCAL addresses here, not all the world... */
	if (sk->rcv_saddr == 0)
		return 0;

	return 1;
}
#endif

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
		NETDEBUG(printk(KERN_DEBUG "UDP: short packet: %d/%d\n", ulen, len));
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

	if (uh->check &&
	    (((skb->ip_summed==CHECKSUM_HW)&&udp_check(uh,len,saddr,daddr,skb->csum)) ||
	     ((skb->ip_summed==CHECKSUM_NONE) &&
	      (udp_check(uh,len,saddr,daddr, csum_partial((char*)uh, len, 0)))))) {
		/* <mea@utu.fi> wants to know, who sent it, to
		   go and stomp on the garbage sender... */

		/* RFC1122: OK.  Discards the bad packet silently (as far as */
		/* the network is concerned, anyway) as per 4.1.3.4 (MUST). */

		NETDEBUG(printk(KERN_DEBUG "UDP: bad checksum. From %08lX:%d to %08lX:%d ulen %d\n",
		       ntohl(saddr),ntohs(uh->source),
		       ntohl(daddr),ntohs(uh->dest),
		       ulen));
		udp_statistics.UdpInErrors++;
		kfree_skb(skb, FREE_WRITE);
		return(0);
	}


	len = ulen;

	/*
	 *	FIXME:
	 *	Trimming things wrongly. We must adjust the base/end to allow
	 *	for the headers we keep!
	 *		 --ANK 
	 */
	skb_trim(skb,len);


	if(rt->rt_flags & (RTF_BROADCAST|RTF_MULTICAST))
		return udp_v4_mcast_deliver(skb, uh, saddr, daddr);

#ifdef CONFIG_IP_TRANSPARENT_PROXY
	if (IPCB(skb)->redirport)
		sk = udp_v4_proxy_lookup(uh->dest, saddr, uh->source,
					 daddr, skb->dev->pa_addr,
					 IPCB(skb)->redirport);
	else
#endif
	sk = udp_v4_lookup(saddr, uh->source, daddr, uh->dest);
	
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
	(struct sock *)&udp_prot,	/* sklist_next */
	(struct sock *)&udp_prot,	/* sklist_prev */
	udp_close,			/* close */
	udp_connect,			/* connect */
	NULL,				/* accept */
	NULL,				/* retransmit */
	NULL,				/* write_wakeup */
	NULL,				/* read_wakeup */
	datagram_poll,			/* poll */
	udp_ioctl,			/* ioctl */
	NULL,				/* init */
	NULL,				/* destroy */
	NULL,				/* shutdown */
	ip_setsockopt,			/* setsockopt */
	ip_getsockopt,			/* getsockopt */
	udp_sendmsg,			/* sendmsg */
	udp_recvmsg,			/* recvmsg */
	NULL,				/* bind */
	udp_queue_rcv_skb,		/* backlog_rcv */
	udp_v4_hash,			/* hash */
	udp_v4_unhash,			/* unhash */
	udp_v4_rehash,			/* rehash */
	udp_good_socknum,		/* good_socknum */
	udp_v4_verify_bind,		/* verify_bind */
	128,				/* max_header */
	0,				/* retransmits */
	"UDP",				/* name */
	0,				/* inuse */
	0				/* highestinuse */
};
