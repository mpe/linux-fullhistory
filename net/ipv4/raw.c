/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		RAW - implementation of IP "raw" sockets.
 *
 * Version:	@(#)raw.c	1.0.4	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() fixed up
 *		Alan Cox	:	ICMP error handling
 *		Alan Cox	:	EMSGSIZE if you send too big a packet
 *		Alan Cox	: 	Now uses generic datagrams and shared skbuff
 *					library. No more peek crashes, no more backlogs
 *		Alan Cox	:	Checks sk->broadcast.
 *		Alan Cox	:	Uses skb_free_datagram/skb_copy_datagram
 *		Alan Cox	:	Raw passes ip options too
 *		Alan Cox	:	Setsocketopt added
 *		Alan Cox	:	Fixed error return for broadcasts
 *		Alan Cox	:	Removed wake_up calls
 *		Alan Cox	:	Use ttl/tos
 *		Alan Cox	:	Cleaned up old debugging
 *		Alan Cox	:	Use new kernel side addresses
 *	Arnt Gulbrandsen	:	Fixed MSG_DONTROUTE in raw sockets.
 *		Alan Cox	:	BSD style RAW socket demultiplexing.
 *		Alan Cox	:	Beginnings of mrouted support.
 *		Alan Cox	:	Added IP_HDRINCL option.
 *		Alan Cox	:	Skip broadcast check if BSDism set.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
 
#include <linux/config.h> 
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/mroute.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/checksum.h>

#ifdef CONFIG_IP_MROUTE
struct sock *mroute_socket=NULL;
#endif

/*
 *	Raw_err does not currently get called by the icmp module - FIXME:
 */
 
void raw_err (struct sock *sk, struct sk_buff *skb)
{
	int type = skb->h.icmph->type;
	int code = skb->h.icmph->code;

	if (sk->ip_recverr && !sk->users) {
		struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
		if (skb2 && sock_queue_err_skb(sk, skb2))
			kfree_skb(skb, FREE_READ);
	}

	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED) {
		if (sk->ip_pmtudisc != IP_PMTUDISC_DONT) {
			sk->err = EMSGSIZE;
			sk->error_report(sk);
		}
	}
}

static int raw_rcv_skb(struct sock * sk, struct sk_buff * skb)
{
	/* Charge it to the socket. */
	
	if (__sock_queue_rcv_skb(sk,skb)<0)
	{
		ip_statistics.IpInDiscards++;
		kfree_skb(skb, FREE_READ);
		return -1;
	}

	ip_statistics.IpInDelivers++;
	return 0;
}

/*
 *	This should be the easiest of all, all we do is
 *	copy it into a buffer. All demultiplexing is done
 *	in ip.c
 */

int raw_rcv(struct sock *sk, struct sk_buff *skb)
{
	/* Now we need to copy this into memory. */
	skb_trim(skb, ntohs(skb->nh.iph->tot_len));
	
	skb->h.raw = skb->nh.raw;

	if (sk->users) {
		__skb_queue_tail(&sk->back_log, skb);
		return 0;
	}
	raw_rcv_skb(sk, skb);
	return 0;
}

struct rawfakehdr 
{
	const unsigned char *from;
	u32	saddr;
};

/*
 *	Send a RAW IP packet.
 */

/*
 *	Callback support is trivial for SOCK_RAW
 */
  
static int raw_getfrag(const void *p, char *to, unsigned int offset, unsigned int fraglen)
{
	struct rawfakehdr *rfh = (struct rawfakehdr *) p;
	return copy_from_user(to, rfh->from + offset, fraglen);
}

/*
 *	IPPROTO_RAW needs extra work.
 */
 
static int raw_getrawfrag(const void *p, char *to, unsigned int offset, unsigned int fraglen)
{
	struct rawfakehdr *rfh = (struct rawfakehdr *) p;

	if (copy_from_user(to, rfh->from + offset, fraglen))
		return -EFAULT;
	if (offset==0) {
		struct iphdr *iph = (struct iphdr *)to;
		if (!iph->saddr)
			iph->saddr = rfh->saddr;
		iph->check=0;
		iph->tot_len=htons(fraglen);	/* This is right as you can't frag
						   RAW packets */
		/*
	 	 *	Deliberate breach of modularity to keep 
	 	 *	ip_build_xmit clean (well less messy).
		 */
		if (!iph->id)
			iph->id = htons(ip_id_count++);
		iph->check=ip_fast_csum((unsigned char *)iph, iph->ihl);
	}
	return 0;
}

static int raw_sendto(struct sock *sk, const unsigned char *from, 
		      int len, struct msghdr *msg)
{
	struct device *dev = NULL;
	struct ipcm_cookie ipc;
	struct rawfakehdr rfh;
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

	if (msg->msg_flags & MSG_OOB)		/* Mirror BSD error message compatibility */
		return -EOPNOTSUPP;
			 
	if (msg->msg_flags & ~(MSG_DONTROUTE|MSG_DONTWAIT))
		return(-EINVAL);

	/*
	 *	Get and verify the address. 
	 */

	if (msg->msg_namelen) {
		struct sockaddr_in *usin = (struct sockaddr_in*)msg->msg_name;
		if (msg->msg_namelen < sizeof(*usin))
			return(-EINVAL);
		if (usin->sin_family != AF_INET) {
			static int complained;
			if (!complained++)
				printk("%s forgot to set AF_INET in raw sendmsg. Fix it!\n", current->comm);
			if (usin->sin_family)
				return -EINVAL;
		}
		daddr = usin->sin_addr.s_addr;
		/* ANK: I did not forget to get protocol from port field.
		 * I just do not know, who uses this weirdness.
		 * IP_HDRINCL is much more convenient.
		 */
	} else {
		if (sk->state != TCP_ESTABLISHED) 
			return(-EINVAL);
		daddr = sk->daddr;
	}

	ipc.addr = sk->saddr;
	ipc.opt = NULL;

	if (msg->msg_controllen) {
		int tmp = ip_cmsg_send(msg, &ipc, &dev);
		if (tmp)
			return tmp;
		if (ipc.opt && sk->ip_hdrincl) {
			kfree(ipc.opt);
			return -EINVAL;
		}
		if (ipc.opt)
			free=1;
	}

	rfh.saddr = ipc.addr;
	ipc.addr = daddr;

	if (!ipc.opt)
		ipc.opt = sk->opt;
	if (ipc.opt && ipc.opt->srr) {
		if (!daddr)
			return -EINVAL;
		daddr = ipc.opt->faddr;
	}
	tos = RT_TOS(sk->ip_tos) | (sk->localroute || (msg->msg_flags&MSG_DONTROUTE));

	if (MULTICAST(daddr) && sk->ip_mc_name[0] && dev==NULL)
		err = ip_route_output_dev(&rt, daddr, rfh.saddr, tos, sk->ip_mc_name);
	else
		err = ip_route_output(&rt, daddr, rfh.saddr, tos, dev);

	if (err) {
		if (free) kfree(ipc.opt);
		return err;
	}

	if (rt->rt_flags&RTF_BROADCAST && !sk->broadcast) {
		if (free) kfree(ipc.opt);
		ip_rt_put(rt);
		return -EACCES;
	}

	rfh.from = from;
	rfh.saddr = rt->rt_src;
	if (!ipc.addr)
		ipc.addr = rt->rt_dst;
	if(sk->ip_hdrincl)
		err=ip_build_xmit(sk, raw_getrawfrag, &rfh, len, &ipc, rt, msg->msg_flags);
	else {
		if (len>65535-sizeof(struct iphdr))
			err = -EMSGSIZE;
		else
			err=ip_build_xmit(sk, raw_getfrag, &rfh, len, &ipc, rt, msg->msg_flags);
	}

	if (free)
		kfree(ipc.opt);
	ip_rt_put(rt);

	return err<0 ? err : len;
}

/*
 *	Temporary
 */
 
static int raw_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	if (msg->msg_iovlen==1)
		return raw_sendto(sk, msg->msg_iov[0].iov_base,len, msg);
	else {
		/*
		 *	For awkward cases we linearise the buffer first. In theory this is only frames
		 *	whose iovec's don't split on 4 byte boundaries, and soon encrypted stuff (to keep
		 *	skip happy). We are a bit more general about it.
		 */
		 
		unsigned char *buf;
		int err;
		if(len>65515)
			return -EMSGSIZE;
		buf=kmalloc(len, GFP_KERNEL);
		if(buf==NULL)
			return -ENOBUFS;
		err = memcpy_fromiovec(buf, msg->msg_iov, len);
		if (!err)
		{
			unsigned short fs;
			fs=get_fs();
			set_fs(get_ds());
			err=raw_sendto(sk,buf,len, msg);
			set_fs(fs);
		}
		else
			err = -EFAULT;
		
		kfree_s(buf,len);
		return err;
	}
}

static void raw_close(struct sock *sk, unsigned long timeout)
{
	sk->state = TCP_CLOSE;
#ifdef CONFIG_IP_MROUTE	
	if(sk==mroute_socket)
	{
		ipv4_config.multicast_route = 0;
		mroute_close(sk);
		mroute_socket=NULL;
	}
#endif	
	sk->dead=1;
	destroy_sock(sk);
}


static int raw_init(struct sock *sk)
{
	return(0);
}


/*
 *	This should be easy, if there is something there
 *	we return it, otherwise we block.
 */

int raw_recvmsg(struct sock *sk, struct msghdr *msg, int len,
     int noblock, int flags,int *addr_len)
{
	int copied=0;
	struct sk_buff *skb;
	int err;
	struct sockaddr_in *sin=(struct sockaddr_in *)msg->msg_name;

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;
		
	if (sk->shutdown & RCV_SHUTDOWN) 
		return(0);

	if (addr_len)
		*addr_len=sizeof(*sin);

	if (sk->ip_recverr && (skb = skb_dequeue(&sk->error_queue)) != NULL) {
		err = sock_error(sk);
		if (msg->msg_controllen == 0) {
			skb_free_datagram(sk, skb);
			return err;
		}
		put_cmsg(msg, SOL_IP, IP_RECVERR, skb->len, skb->data);
		skb_free_datagram(sk, skb);
		return 0;
	}

	skb=skb_recv_datagram(sk,flags,noblock,&err);
	if(skb==NULL)
 		return err;

	copied = skb->len;
	if (len < copied)
	{
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}
	
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (sin) {
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = skb->nh.iph->saddr;
	}
	if (sk->ip_cmsg_flags)
		ip_cmsg_recv(msg, skb);
	skb_free_datagram(sk, skb);
	return err ? err : (copied);
}


struct proto raw_prot = {
	raw_close,
	udp_connect,
	NULL,
	NULL,
	NULL,
	NULL,
	datagram_select,
#ifdef CONFIG_IP_MROUTE	
	ipmr_ioctl,
#else
	NULL,
#endif		
	raw_init,
	NULL,
	NULL,
	ip_setsockopt,
	ip_getsockopt,
	raw_sendmsg,
	raw_recvmsg,
	NULL,		/* No special bind */
	raw_rcv_skb,
	128,
	0,
	"RAW",
	0, 0,
	NULL
};
